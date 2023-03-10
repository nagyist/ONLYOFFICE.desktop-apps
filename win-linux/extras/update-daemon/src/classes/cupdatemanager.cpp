/*
 * (c) Copyright Ascensio System SIA 2010-2019
 *
 * This program is a free software product. You can redistribute it and/or
 * modify it under the terms of the GNU Affero General Public License (AGPL)
 * version 3 as published by the Free Software Foundation. In accordance with
 * Section 7(a) of the GNU AGPL its Section 15 shall be amended to the effect
 * that Ascensio System SIA expressly excludes the warranty of non-infringement
 * of any third-party rights.
 *
 * This program is distributed WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR  PURPOSE. For
 * details, see the GNU AGPL at: http://www.gnu.org/licenses/agpl-3.0.html
 *
 * You can contact Ascensio System SIA at 20A-12 Ernesta Birznieka-Upisha
 * street, Riga, Latvia, EU, LV-1050.
 *
 * The  interactive user interfaces in modified source and object code versions
 * of the Program must display Appropriate Legal Notices, as required under
 * Section 5 of the GNU AGPL version 3.
 *
 * Pursuant to Section 7(b) of the License you must retain the original Product
 * logo when distributing the program. Pursuant to Section 7(e) we decline to
 * grant you any rights under trademark law for use of our trademarks.
 *
 * All the Product's GUI elements, including illustrations and icon sets, as
 * well as technical writing content are licensed under the terms of the
 * Creative Commons Attribution-ShareAlike 4.0 International. See the License
 * terms at http://creativecommons.org/licenses/by-sa/4.0/legalcode
 *
 */

#include "cupdatemanager.h"
#include <algorithm>
#include <functional>
#include <locale>
#include <codecvt>
#include <vector>
#include "utils.h"
#include "../../src/defines.h"
#include <Windows.h>
#include <shlwapi.h>
#include <tchar.h>
#include <sstream>

#define UPDATE_PATH      TEXT("/" REG_APP_NAME "Updates")
#define BACKUP_PATH      TEXT("/" REG_APP_NAME "Backup")
#define APP_LAUNCH_NAME  L"/DesktopEditors.exe"
#define DAEMON_NAME      L"/update-daemon.exe"
#define TEMP_DAEMON_NAME L"/~update-daemon.exe"
#define DELETE_LIST      L"/.delete_list.lst"
#define REPLACEMENT_LIST L"/.replacement_list.lst"
#define SUCCES_UNPACKED  L"/.success_unpacked"

using std::vector;


auto currentArch()->wstring
{
#ifdef _WIN64
    return L"_x64";
#else
    return L"_x86";
#endif
}

auto generateTmpFileName(const wstring &ext)->wstring
{
    wstring uuid_wstr;
    UUID uuid = {0};
    RPC_WSTR wszUuid = NULL;
    if (UuidCreate(&uuid) == RPC_S_OK && UuidToStringW(&uuid, &wszUuid) == RPC_S_OK) {
        uuid_wstr = ((wchar_t*)wszUuid);
        RpcStringFreeW(&wszUuid);
    } else
        uuid_wstr = L"00000000-0000-0000-0000-000000000000";
    return NS_File::tempPath() + L"/" + TEXT(FILE_PREFIX) + uuid_wstr + currentArch() + ext;
}

auto isSuccessUnpacked(const wstring &successFilePath, const wstring &version)->bool
{
    list<wstring> lines;
    if (NS_File::readFile(successFilePath, lines)) {
        if (std::find(lines.begin(), lines.end(), version) != lines.end())
            return true;
    }
    return false;
}

auto unzipArchive(const wstring &zipFilePath, const wstring &updPath,
                  const wstring &appPath, const wstring &version, wstring &error)->bool
{
    if (!NS_File::dirExists(updPath) && !NS_File::makePath(updPath)) {
        error = L"An error occurred while creating dir: " + updPath;
        return false;
    }

    // Extract files
    if (!NS_File::unzipArchive(zipFilePath, updPath)) {
        error = L"An error occurred while unpacking zip file!";
        return false;
    }

    auto fillSubpathVector = [&error](const wstring &path, vector<wstring> &vec)->bool {
        list<wstring> filesList;
        wstring _error;
        if (!NS_File::GetFilesList(path, &filesList, _error)) {
            error = L"An error occurred while get files list: " + _error;
            return false;
        }
        for (auto &filePath : filesList) {
            wstring subPath = filePath.substr(path.length());
            vec.push_back(std::move(subPath));
        }
        return true;
    };

    vector<wstring> updVec, appVec;
    if (!fillSubpathVector(appPath, appVec) || !fillSubpathVector(updPath, updVec))
        return false;

#ifdef ALLOW_DELETE_UNUSED_FILES
    // Create a list of files to delete
    {
        list<wstring> delList;
        for (auto &appFile : appVec) {
            auto it_appFile = std::find(updVec.begin(), updVec.end(), appFile);
            if (it_appFile == updVec.end() && appFile != DAEMON_NAME
                    && appFile != L"/unins000.dat" && appFile != L"/unins000.exe")
                delList.push_back(appFile);
        }
        if (!NS_File::writeToFile(updPath + DELETE_LIST, delList))
            return false;
    }
#endif

    // Create a list of files to replacement
    {
        list<wstring> replList;
        for (auto &updFile : updVec) {
            auto it_appFile = std::find(appVec.begin(), appVec.end(), updFile);
            if (it_appFile != appVec.end()) {
                auto updFileHash = NS_File::getFileHash(updPath + updFile);
                if (updFileHash.empty() || updFileHash != NS_File::getFileHash(appPath + *it_appFile))
                    replList.push_back(updFile);

            } else
                replList.push_back(updFile);
        }
        if (!NS_File::writeToFile(updPath + REPLACEMENT_LIST, replList))
            return false;
    }   

    // Сreate a file about successful unpacking for use in subsequent launches
    {
        list<wstring> successList{version};
        if (!NS_File::writeToFile(updPath + SUCCES_UNPACKED, successList))
            return false;
    }
    return true;
}

CUpdateManager::CUpdateManager(CObject *parent):
    CObject(parent),
    m_downloadMode(Mode::CHECK_UPDATES),
    m_socket(new CSocket(APP_PORT, SVC_PORT)),
    m_pDownloader(new CDownloader)
{
    init();
}

CUpdateManager::~CUpdateManager()
{
    if (m_future_clear.valid())
        m_future_clear.wait();
    if (m_future_unzip.valid())
        m_future_unzip.wait();
    delete m_pDownloader, m_pDownloader = nullptr;
    delete m_socket, m_socket = nullptr;
}

void CUpdateManager::init()
{
    m_pDownloader->onComplete([=](int error) {
        onCompleteSlot(error, m_pDownloader->GetFilePath());
    });
    m_pDownloader->onProgress([=](int percent) {
        onProgressSlot(percent);
    });
    m_socket->onMessageReceived([=](void *data, size_t size) {
        wstring str((const wchar_t*)data), tmp;
        vector<wstring> params;
        std::wstringstream wss(str);
        while (std::getline(wss, tmp, L'|'))
            params.push_back(std::move(tmp));

        if (params.size() == 4) {
            switch (std::stoi(params[0])) {
            case MSG_CheckUpdates: {
                m_downloadMode = Mode::CHECK_UPDATES;
                if (m_pDownloader)
                    m_pDownloader->downloadFile(params[1], generateTmpFileName(L".json"));
                break;
            }
            case MSG_LoadUpdates: {
                m_downloadMode = Mode::DOWNLOAD_UPDATES;
                if (m_pDownloader)
                    m_pDownloader->downloadFile(params[1], generateTmpFileName(L".zip"));
                break;
            }
            case MSG_StopDownload: {
                m_downloadMode = Mode::CHECK_UPDATES;
                if (m_pDownloader)
                    m_pDownloader->stop();
                break;
            }
            case MSG_UnzipIfNeeded:
                unzipIfNeeded(params[1], params[2]);
                break;

            case MSG_StartReplacingFiles:
                startReplacingFiles();
                break;

            case MSG_ClearTempFiles:
                clearTempFiles(params[1], params[2]);
                break;

            default:
                break;
            }
        }
    });

    m_socket->onError([](const char* error) {
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
        NS_Logger::WriteLog(DEFAULT_LOG_FILE, converter.from_bytes(error));
    });
}

void CUpdateManager::onCompleteSlot(const int error, const wstring &filePath)
{
    if (error == 0) {
        switch (m_downloadMode) {
        case Mode::CHECK_UPDATES:
            sendMessage(MSG_LoadCheckFinished, filePath);
            break;
        case Mode::DOWNLOAD_UPDATES:
            sendMessage(MSG_LoadUpdateFinished, filePath);
            break;
        default:
            break;
        }
    } else
    if (error == 1) {
        // Pause or Stop
    } else
    if (error == -1) {
        sendMessage(MSG_OtherError, L"Update download failed: out of memory!");
    } else {
        sendMessage(MSG_OtherError, L"Server connection error!");
    }
}

void CUpdateManager::onProgressSlot(const int percent)
{
    if (m_downloadMode == Mode::DOWNLOAD_UPDATES)
        sendMessage(MSG_Progress, to_wstring(percent));
}

void CUpdateManager::unzipIfNeeded(const wstring &filePath, const wstring &newVersion)
{
    if (m_lock)
        return;
    m_lock = true;
    const wstring updPath = NS_File::tempPath() + UPDATE_PATH;
    auto unzip = [=]()->void {
        wstring error(L"unzipArchive() error");
        if (!unzipArchive(filePath, updPath, NS_File::appPath(), newVersion , error)) {
            m_lock = false;
            if (!sendMessage(MSG_OtherError, error)) {
                NS_Logger::WriteLog(DEFAULT_LOG_FILE, DEFAULT_ERROR_MESSAGE);
            }
            return;
        }
        m_lock = false;
        if (!sendMessage(MSG_ShowStartInstallMessage)) {
            NS_Logger::WriteLog(DEFAULT_LOG_FILE, DEFAULT_ERROR_MESSAGE);
        }
    };

    if (!NS_File::dirExists(updPath) || NS_File::dirIsEmpty(updPath)) {
        m_future_unzip = std::async(std::launch::async, unzip);
    } else {
        if (isSuccessUnpacked(updPath + SUCCES_UNPACKED, newVersion)) {
            m_lock = false;
            if (!sendMessage(MSG_ShowStartInstallMessage)) {
                NS_Logger::WriteLog(DEFAULT_LOG_FILE, DEFAULT_ERROR_MESSAGE);
            }
        } else {
            if (!NS_File::removeDirRecursively(updPath)) {
                NS_Logger::WriteLog(DEFAULT_LOG_FILE, DEFAULT_ERROR_MESSAGE);
            }
            m_future_unzip = std::async(std::launch::async, unzip);
        }
    }
}

void CUpdateManager::clearTempFiles(const wstring &prefix, const wstring &except)
{
    m_future_clear = std::async(std::launch::async, [=]() {
        list<wstring> filesList;
        wstring _error;
        if (!NS_File::GetFilesList(NS_File::tempPath(), &filesList, _error)) {
            NS_Logger::WriteLog(DEFAULT_LOG_FILE, DEFAULT_ERROR_MESSAGE);
            return;
        }
        for (auto &filePath : filesList) {
            if (PathMatchSpec(filePath.c_str(), L"*.json") || PathMatchSpec(filePath.c_str(), L"*.zip")) {
                wstring lcFilePath(filePath);
                std::transform(lcFilePath.begin(), lcFilePath.end(), lcFilePath.begin(), ::tolower);
                if (lcFilePath.find(prefix) != wstring::npos && filePath != except)
                    NS_File::removeFile(filePath);
            }
        }
    });
}

void CUpdateManager::restoreFromBackup(const wstring &appPath, const wstring &updPath, const wstring &tmpPath)
{
    // Restore from backup
    if (!NS_File::replaceFolderContents(tmpPath, appPath))
        NS_Logger::WriteLog(DEFAULT_LOG_FILE, L"An error occurred while restore files from backup!");
    else
        NS_File::removeDirRecursively(tmpPath);

    // Restore executable name
    if (!NS_File::replaceFile(appPath + TEMP_DAEMON_NAME, appPath + DAEMON_NAME))
        NS_Logger::WriteLog(DEFAULT_LOG_FILE, L"An error occurred while restore daemon file name!");

    NS_File::removeDirRecursively(updPath);
}

void CUpdateManager::startReplacingFiles()
{
    wstring appPath = NS_File::appPath();
    wstring appFilePath = NS_File::appPath() + DAEMON_NAME;
    wstring updPath = NS_File::tempPath() + UPDATE_PATH;
    wstring tmpPath = NS_File::tempPath() + BACKUP_PATH;
    if (!NS_File::dirExists(updPath)) {
        NS_Logger::WriteLog(DEFAULT_LOG_FILE, L"An error occurred while searching dir: " + updPath);
        return;
    }
    if (NS_File::dirExists(tmpPath) && !NS_File::dirIsEmpty(tmpPath)
            && !NS_File::removeDirRecursively(tmpPath)) {
        NS_Logger::WriteLog(DEFAULT_LOG_FILE, L"An error occurred while deleting Backup dir: " + tmpPath);
        return;
    }
    if (!NS_File::dirExists(tmpPath) && !NS_File::makePath(tmpPath)) {
        NS_Logger::WriteLog(DEFAULT_LOG_FILE, L"An error occurred while creating dir: " + tmpPath);
        return;
    }

    // Remove old update-daemon
    if (NS_File::fileExists(appPath + TEMP_DAEMON_NAME)
            && !NS_File::removeFile(appPath + TEMP_DAEMON_NAME)) {
        NS_Logger::WriteLog(DEFAULT_LOG_FILE, L"Unable to remove temp file: " + appPath + TEMP_DAEMON_NAME);
        return;
    }

    list<wstring> repList;
    if (!NS_File::readFile(updPath + REPLACEMENT_LIST, repList))
        return;

#ifdef ALLOW_DELETE_UNUSED_FILES
    list<wstring> delList;
    if (!NS_File::readFile(updPath + DELETE_LIST, delList))
        return;
#endif

    // Rename current executable
    wstring appFileRenamedPath = appPath + TEMP_DAEMON_NAME;
    if (!NS_File::replaceFile(appFilePath, appFileRenamedPath)) {
        NS_Logger::WriteLog(DEFAULT_LOG_FILE, L"An error occurred while renaming the daemon file!");
        return;
    }

#ifdef ALLOW_DELETE_UNUSED_FILES
    // Replace unused files to Backup
    if (!NS_File::replaceListOfFiles(delList, appPath, tmpPath)) {
        NS_Logger::WriteLog(DEFAULT_LOG_FILE, L"An error occurred while replace unused files! Restoring from the backup will start.");
        restoreFromBackup(appPath, updPath, tmpPath);
        return;
    }
#endif

    // Move update files to app path
    if (!NS_File::replaceListOfFiles(repList, updPath, appPath, tmpPath)) {
        NS_Logger::WriteLog(DEFAULT_LOG_FILE, L"An error occurred while copy files! Restoring from the backup will start.");

        // Remove new update-daemon.exe if exist
        if (NS_File::fileExists(appFilePath))
            NS_File::removeFile(appFilePath);

        restoreFromBackup(appPath, updPath, tmpPath);
        return;
    }

    // Remove Update and Temp dirs
    NS_File::removeDirRecursively(updPath);
    NS_File::removeDirRecursively(tmpPath);

    // Restore executable name if there was no new version
    if (std::find(repList.begin(), repList.end(), DAEMON_NAME) == repList.end())
        if (!NS_File::replaceFile(appFileRenamedPath, appFilePath))
            NS_Logger::WriteLog(DEFAULT_LOG_FILE, L"An error occurred while restore daemon file name: " + appFileRenamedPath);

    // Restart program
    if (!NS_File::runProcess(appPath + APP_LAUNCH_NAME, L""))
        NS_Logger::WriteLog(DEFAULT_LOG_FILE, L"An error occurred while restarting the program!");
}

bool CUpdateManager::sendMessage(int cmd, const wstring &param1, const wstring &param2, const wstring &param3)
{
    wstring str = to_wstring(cmd) + L"|" + param1 + L"|" + param2 + L"|" + param3;
    size_t sz = str.size() * sizeof(str.front());
    return m_socket->sendMessage((void*)str.c_str(), sz);
}
