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

#include "utils.h"
#include "version.h"
#include <Windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <shlobj_core.h>
#include <combaseapi.h>
#include <comutil.h>
#include <oleauto.h>
#include <iostream>
#include <fstream>
#include <regex>
#include <cstdio>
#include <Wincrypt.h>
#include <WtsApi32.h>
#include <Softpub.h>
#include <vector>
#include <sstream>

#define BUFSIZE 1024

namespace NS_Utils
{
    wstring GetLastErrorAsString()
    {
        DWORD errorMessageID = ::GetLastError();
        if (errorMessageID == 0)
            return L"";

        LPWSTR messageBuffer = NULL;
        size_t size = FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                    FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                    NULL, errorMessageID,
                                    MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                    (LPWSTR)&messageBuffer, 0, NULL);

        wstring message(messageBuffer, (int)size);
        LocalFree(messageBuffer);
        return message;
    }

    void ShowMessage(wstring str, bool showError)
    {
        if (showError)
            str += L" " + GetLastErrorAsString();
        MessageBoxW(NULL, str.c_str(), TEXT(VER_PRODUCTNAME_STR),
                    MB_ICONERROR | MB_SERVICE_NOTIFICATION_NT3X | MB_SETFOREGROUND);
    }
}

namespace NS_File
{
    bool GetFilesList(const wstring &path, list<wstring> *lst, wstring &error)
    {
        WCHAR szDir[MAX_PATH];
        wstring searchPath = path + L"/*";
        wcscpy_s(szDir, MAX_PATH, searchPath.c_str());

        WIN32_FIND_DATA ffd;
        HANDLE hFind = FindFirstFile(szDir, &ffd);
        if (hFind == INVALID_HANDLE_VALUE) {
            error = wstring(L"FindFirstFile invalid handle value: ") + szDir;
            return false;
        }

        do {
            if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (!wcscmp(ffd.cFileName, L".")
                        || !wcscmp(ffd.cFileName, L".."))
                    continue;
                if (!GetFilesList(path + L"/" + wstring(ffd.cFileName), lst, error)) {
                    FindClose(hFind);
                    return false;
                }
            } else
                lst->push_back(path + L"/" + wstring(ffd.cFileName));

        } while (FindNextFile(hFind, &ffd) != 0);

        if (GetLastError() != ERROR_NO_MORE_FILES) {
            FindClose(hFind);
            error = wstring(L"Error while FindFile: ") + szDir;
            return false;
        }
        FindClose(hFind);
        return true;
    }

    bool moveSingleFile(const wstring &source, const wstring &dest, const wstring &temp, bool useTmp)
    {
        if (fileExists(dest)) {
            if (useTmp) {
                // Create a backup
                if (!dirExists(parentPath(temp)) && !makePath(parentPath(temp))) {
                    NS_Logger::WriteLog(DEFAULT_LOG_FILE, L"Can't create path: " + parentPath(temp));
                    return false;
                }
                if (!replaceFile(dest, temp)) {
                    NS_Logger::WriteLog(DEFAULT_LOG_FILE, L"Can't move file from " + dest + L" to " + temp + L". " + NS_Utils::GetLastErrorAsString());
                    return false;
                }
            }
        } else {
            if (!dirExists(parentPath(dest)) && !makePath(parentPath(dest))) {
                NS_Logger::WriteLog(DEFAULT_LOG_FILE, L"Can't create path: " + parentPath(dest));
                return false;
            }
        }

        if (!replaceFile(source, dest)) {
            NS_Logger::WriteLog(DEFAULT_LOG_FILE, L"Can't move file from " + source + L" to " + dest + L". " + NS_Utils::GetLastErrorAsString());
            return false;
        }
        return true;
    }

    bool readFile(const wstring &filePath, list<wstring> &linesList)
    {
        std::wifstream file(filePath.c_str(), std::ios_base::in);
        if (!file.is_open()) {
            NS_Logger::WriteLog(DEFAULT_LOG_FILE, L"An error occurred while opening: " + filePath);
            return false;
        }
        wstring line;
        while (std::getline(file, line))
            linesList.push_back(line);

        file.close();
        return true;
    }

    bool writeToFile(const wstring &filePath, list<wstring> &linesList)
    {
        std::wofstream file(filePath.c_str(), std::ios_base::out);
        if (!file.is_open()) {
            NS_Logger::WriteLog(DEFAULT_LOG_FILE, L"An error occurred while writing: " + filePath);
            return false;
        }
        for (auto &line : linesList)
            file << line << std::endl;

        file.close();
        return true;
    }

    bool replaceListOfFiles(const list<wstring> &filesList, const wstring &from,
                                const wstring &to, const wstring &tmp)
    {
        bool useTmp = !tmp.empty() && from != tmp && to != tmp;
        for (const wstring &relFilePath: filesList) {
            if (!relFilePath.empty()) {
                if (!moveSingleFile(from + relFilePath,
                                    to + relFilePath,
                                    tmp + relFilePath,
                                    useTmp)) {
                    return false;
                }
            }
        }
        return true;
    }

    bool replaceFolderContents(const wstring &from, const wstring &to, const wstring &tmp)
    {
        list<wstring> filesList;
        wstring error;
        if (!GetFilesList(from, &filesList, error))
            return false;

        const size_t sourceLength = from.length();
        bool useTmp = !tmp.empty() && from != tmp && to != tmp;
        for (const wstring &sourcePath : filesList) {
            if (!sourcePath.empty()) {
                if (!moveSingleFile(sourcePath,
                                    to + sourcePath.substr(sourceLength),
                                    tmp + sourcePath.substr(sourceLength),
                                    useTmp)) {
                    return false;
                }
            }
        }
        return true;
    }

    bool runProcess(const wstring &fileName, const wstring &args)
    {
        DWORD dwSessionId = WTSGetActiveConsoleSessionId();
        if (dwSessionId == 0xFFFFFFFF) {
            return false;
        }

        HANDLE hUserToken = NULL;
        if (!WTSQueryUserToken(dwSessionId, &hUserToken)) {
            return false;
        }

        STARTUPINFO si;
        ZeroMemory(&si, sizeof(STARTUPINFO));
        si.cb = sizeof(STARTUPINFO);
        si.lpDesktop = const_cast<LPWSTR>(L"winsta0\\default");
        PROCESS_INFORMATION pi;
        if (CreateProcessAsUser(hUserToken, fileName.c_str(),
                                const_cast<LPWSTR>(args.c_str()),
                                NULL, NULL, FALSE,
                                CREATE_NEW_CONSOLE | CREATE_UNICODE_ENVIRONMENT,
                                NULL, NULL, &si, &pi))
        {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            CloseHandle(hUserToken);
            return true;
        }
        CloseHandle(hUserToken);
        return false;
    }

    bool fileExists(const wstring &filePath)
    {
        DWORD attr = ::GetFileAttributes(filePath.c_str());
        return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
    }

    bool dirExists(const wstring &dirName) {
        DWORD attr = ::GetFileAttributes(dirName.c_str());
        return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
    }

    bool dirIsEmpty(const wstring &dirName)
    {
        return PathIsDirectoryEmpty(dirName.c_str());
    }

    bool makePath(const wstring &path)
    {
        list<wstring> pathsList;
        wstring last_path(path);
        while (!last_path.empty() && !dirExists(last_path)) {
            pathsList.push_front(last_path);
            last_path = parentPath(last_path);
        }
        for (list<wstring>::iterator it = pathsList.begin(); it != pathsList.end(); ++it) {
            if (::CreateDirectory(it->c_str(), NULL) == 0)
                return false;
        }
        return true;
    }

    bool replaceFile(const wstring &oldFilePath, const wstring &newFilePath)
    {
        return MoveFileExW(oldFilePath.c_str(), newFilePath.c_str(),
                           MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0 ? true : false;
    }

    bool removeFile(const wstring &filePath)
    {
        return DeleteFile(filePath.c_str()) != 0 ? true : false;
    }

    bool removeDirRecursively(const wstring &dir)
    {
        WCHAR pFrom[_MAX_PATH + 1];
        swprintf_s(pFrom, sizeof(pFrom)/sizeof(WCHAR), L"%s%c", dir.c_str(), '\0');
        SHFILEOPSTRUCT fop = {
            NULL,
            FO_DELETE,
            pFrom,
            NULL,
            FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT,
            FALSE,
            0,
            NULL
        };
        return (SHFileOperation(&fop) == 0) ? true : false;
    }

    wstring fromNativeSeparators(const wstring &path)
    {
        return std::regex_replace(path, std::wregex(L"\\\\"), L"/");
    }

    wstring toNativeSeparators(const wstring &path)
    {
        return std::regex_replace(path, std::wregex(L"\\/"), L"\\");
    }

    wstring parentPath(const wstring &path)
    {
        wstring::size_type delim = path.find_last_of(L"\\/");
        return (delim == wstring::npos) ? L"" : path.substr(0, delim);
    }

    wstring tempPath()
    {
        WCHAR buff[MAX_PATH];
        DWORD res = ::GetTempPathW(MAX_PATH, buff);
        if (res != 0) {
            return fromNativeSeparators(parentPath(wstring(buff)));
        }
        return L"";
    }

    wstring appPath()
    {
        WCHAR buff[MAX_PATH];
        DWORD res = ::GetModuleFileName(NULL, buff, MAX_PATH);
        if (res != 0) {
            return fromNativeSeparators(parentPath(wstring(buff)));
        }
        return L"";
    }

    string getFileHash(const wstring &fileName)
    {
        HANDLE hFile = NULL;
        hFile = CreateFile(fileName.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ,
            NULL,
            OPEN_EXISTING,
            FILE_FLAG_SEQUENTIAL_SCAN,
            NULL);

        if (hFile == INVALID_HANDLE_VALUE) {
            return "";
        }

        // Get handle to the crypto provider
        HCRYPTPROV hProv = 0;
        if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
            CloseHandle(hFile);
            return "";
        }

        HCRYPTHASH hHash = 0;
        if (!CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash)) {
            CloseHandle(hFile);
            CryptReleaseContext(hProv, 0);
            return "";
        }

        DWORD cbRead = 0;
        BYTE rgbFile[BUFSIZE];
        BOOL bResult = FALSE;
        while ((bResult = ReadFile(hFile, rgbFile, BUFSIZE, &cbRead, NULL))) {
            if (cbRead == 0)
                break;

            if (!CryptHashData(hHash, rgbFile, cbRead, 0)) {
                CryptReleaseContext(hProv, 0);
                CryptDestroyHash(hHash);
                CloseHandle(hFile);
                return "";
            }
        }

        if (!bResult) {
            CryptReleaseContext(hProv, 0);
            CryptDestroyHash(hHash);
            CloseHandle(hFile);
            return "";
        }

        DWORD cbHashSize = 0,
              dwCount = sizeof(DWORD);
        if (!CryptGetHashParam( hHash, HP_HASHSIZE, (BYTE*)&cbHashSize, &dwCount, 0)) {
            CryptReleaseContext(hProv, 0);
            CryptDestroyHash(hHash);
            CloseHandle(hFile);
            return "";
        }

        std::vector<BYTE> buffer(cbHashSize);
        if (!CryptGetHashParam(hHash, HP_HASHVAL, reinterpret_cast<BYTE*>(&buffer[0]), &cbHashSize, 0)) {
            CryptReleaseContext(hProv, 0);
            CryptDestroyHash(hHash);
            CloseHandle(hFile);
            return "";
        }

        std::ostringstream oss;
        for (std::vector<BYTE>::const_iterator it = buffer.begin(); it != buffer.end(); ++it) {
            oss.fill('0');
            oss.width(2);
            oss << std::hex << static_cast<const int>(*it);
        }

        CryptReleaseContext(hProv, 0);
        CryptDestroyHash(hHash);
        CloseHandle(hFile);
        return oss.str();
    }

    bool unzipArchive(const wstring &zipFilePath, const wstring &folderPath)
    {
        wstring file = toNativeSeparators(zipFilePath);
        wstring path = toNativeSeparators(folderPath);
        _bstr_t lpZipFile(file.c_str());
        _bstr_t lpFolder(path.c_str());

        IShellDispatch *pISD;

        Folder  *pZippedFile = 0L;
        Folder  *pDestination = 0L;

        long FilesCount = 0;
        IDispatch* pItem = 0L;
        FolderItems *pFilesInside = 0L;

        VARIANT Options, OutFolder, InZipFile, Item;
        CoInitialize(NULL);
    //    __try {
            if (CoCreateInstance(CLSID_Shell, NULL, CLSCTX_INPROC_SERVER, IID_IShellDispatch, (void**)&pISD) != S_OK) {
                CoUninitialize();
                return false;
            }

            InZipFile.vt = VT_BSTR;
            InZipFile.bstrVal = lpZipFile.GetBSTR();
            pISD->NameSpace(InZipFile, &pZippedFile);
            if (!pZippedFile)
            {
                pISD->Release();
                CoUninitialize();
                return false;
            }

            OutFolder.vt = VT_BSTR;
            OutFolder.bstrVal = lpFolder.GetBSTR();
            pISD->NameSpace(OutFolder, &pDestination);
            if (!pDestination)
            {
                pZippedFile->Release();
                pISD->Release();
                CoUninitialize();
                return false;
            }

            pZippedFile->Items(&pFilesInside);
            if (!pFilesInside)
            {
                pDestination->Release();
                pZippedFile->Release();
                pISD->Release();
                CoUninitialize();
                return false;
            }

            pFilesInside->get_Count(&FilesCount);
            if (FilesCount < 1)
            {
                pFilesInside->Release();
                pDestination->Release();
                pZippedFile->Release();
                pISD->Release();
                CoUninitialize();
                return false;
            }

            pFilesInside->QueryInterface(IID_IDispatch,(void**)&pItem);

            Item.vt = VT_DISPATCH;
            Item.pdispVal = pItem;

            Options.vt = VT_I4;
            Options.lVal = 1024 | 512 | 16 | 4;
            bool retval = pDestination->CopyHere( Item, Options) == S_OK;
            pItem->Release(); pItem = 0L;
            pFilesInside->Release(); pFilesInside = 0L;
            pDestination->Release(); pDestination = 0L;
            pZippedFile->Release(); pZippedFile = 0L;
            pISD->Release(); pISD = 0L;
            CoUninitialize();
            return retval;
    //    }
    //    __finally
    //    {
    //        CoUninitialize();
    //    }
    }

    bool verifyEmbeddedSignature(const wstring &fileName)
    {
        WINTRUST_FILE_INFO fileInfo;
        ZeroMemory(&fileInfo, sizeof(fileInfo));
        fileInfo.cbStruct = sizeof(WINTRUST_FILE_INFO);
        fileInfo.pcwszFilePath = fileName.c_str();
        fileInfo.hFile = NULL;
        fileInfo.pgKnownSubject = NULL;

        GUID guidAction = WINTRUST_ACTION_GENERIC_VERIFY_V2;
        WINTRUST_DATA winTrustData;
        ZeroMemory(&winTrustData, sizeof(winTrustData));
        winTrustData.cbStruct = sizeof(WINTRUST_DATA);
        winTrustData.pPolicyCallbackData = NULL;
        winTrustData.pSIPClientData = NULL;
        winTrustData.dwUIChoice = WTD_UI_NONE;
        winTrustData.fdwRevocationChecks = WTD_REVOKE_NONE;
        winTrustData.dwUnionChoice = WTD_CHOICE_FILE;
        winTrustData.dwStateAction = WTD_STATEACTION_VERIFY;
        winTrustData.hWVTStateData = NULL;
        winTrustData.pwszURLReference = NULL;
        winTrustData.dwUIContext = 0;
        winTrustData.pFile = &fileInfo;

        LONG lStatus = WinVerifyTrust(NULL, &guidAction, &winTrustData);
        if (lStatus == ERROR_SUCCESS)
            return true;
        return false;
    }
}

namespace NS_Logger
{
    bool allow_write_log = false;

    void AllowWriteLog()
    {
        allow_write_log = true;
    }

    void WriteLog(const wstring &filePath, const wstring &log)
    {
        if (allow_write_log) {
            std::wofstream file(filePath.c_str(), std::ios::app);
            if (!file.is_open()) {
                return;
            }
            file << log << std::endl;
            file.close();
        }
    }
}
