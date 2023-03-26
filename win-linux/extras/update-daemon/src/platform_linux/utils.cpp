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

#include "platform_linux/utils.h"
#include "version.h"
#include <cstring>
#include <iostream>
#include <fstream>
#include <regex>
#include <cstdio>
#include <cerrno>
#include <vector>
#include <unistd.h>
#include <sstream>
#include <stdlib.h>
#include <sys/stat.h>
#include <dirent.h>
#include <gtk/gtk.h>
#include <openssl/md5.h>

#define BUFSIZE 1024

namespace NS_Utils
{
    string GetLastErrorAsString()
    {        
        char buff[1024];
        memset(buff, 0, sizeof(buff));
        strerror_r(errno, buff, sizeof(buff));
        return string(buff);
    }

    int ShowMessage(string str, bool showError)
    {
        if (showError)
            str += " " + GetLastErrorAsString();
        const char *title = VER_PRODUCTNAME_STR;

        gtk_init(NULL, NULL);
        GtkWidget *dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                                                      "%s", str.c_str());
        gtk_window_set_title(GTK_WINDOW(dialog), title);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        int res = 0;
        return res;
    }
}

namespace NS_File
{
    string app_path;

    void setAppPath(const std::string &path)
    {
        app_path = parentPath(path);
    }

    bool GetFilesList(const string &path, list<string> *lst, string &error)
    {
        DIR *dir = opendir(path.c_str());
        if (!dir) {
            error = string("FindFirstFile invalid handle value: ") + path;
            return false;
        }

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;

            char _path[PATH_MAX];
            snprintf(_path, sizeof(_path), "%s/%s", path.c_str(), entry->d_name);
            struct stat info;
            if (stat(_path, &info) != 0) {
                error = string("Error getting file information: ") + _path;
                closedir(dir);
                return false;
            }

            if (S_ISDIR(info.st_mode)) {
                if (!GetFilesList(_path, lst, error)) {
                    closedir(dir);
                    return false;
                }
            } else
                lst->push_back(_path);

        }
        closedir(dir);
        return true;
    }

    bool moveSingleFile(const string &source, const string &dest, const string &temp, bool useTmp)
    {
        if (fileExists(dest)) {
            if (useTmp) {
                // Create a backup
                if (!dirExists(parentPath(temp)) && !makePath(parentPath(temp))) {
                    NS_Logger::WriteLog("Can't create path: " + parentPath(temp));
                    return false;
                }
                if (!replaceFile(dest, temp)) {
                    NS_Logger::WriteLog("Can't move file from " + dest + " to " + temp + ". " + NS_Utils::GetLastErrorAsString());
                    return false;
                }
            }
        } else {
            if (!dirExists(parentPath(dest)) && !makePath(parentPath(dest))) {
                NS_Logger::WriteLog("Can't create path: " + parentPath(dest));
                return false;
            }
        }

        if (!replaceFile(source, dest)) {
            NS_Logger::WriteLog("Can't move file from " + source + " to " + dest + ". " + NS_Utils::GetLastErrorAsString());
            return false;
        }
        return true;
    }

    bool readFile(const string &filePath, list<string> &linesList)
    {
        std::ifstream file(filePath.c_str(), std::ios_base::in);
        if (!file.is_open()) {
            NS_Logger::WriteLog("An error occurred while opening: " + filePath);
            return false;
        }
        string line;
        while (std::getline(file, line))
            linesList.push_back(line);

        file.close();
        return true;
    }

    bool writeToFile(const string &filePath, list<string> &linesList)
    {
        std::ofstream file(filePath.c_str(), std::ios_base::out);
        if (!file.is_open()) {
            NS_Logger::WriteLog("An error occurred while writing: " + filePath);
            return false;
        }
        for (auto &line : linesList)
            file << line << std::endl;

        file.close();
        return true;
    }

    bool replaceListOfFiles(const list<string> &filesList, const string &from,
                                const string &to, const string &tmp)
    {
        bool useTmp = !tmp.empty() && from != tmp && to != tmp;
        for (const string &relFilePath: filesList) {
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

    bool replaceFolderContents(const string &from, const string &to, const string &tmp)
    {
        list<string> filesList;
        string error;
        if (!GetFilesList(from, &filesList, error))
            return false;

        const size_t sourceLength = from.length();
        bool useTmp = !tmp.empty() && from != tmp && to != tmp;
        for (const string &sourcePath : filesList) {
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

    bool runProcess(const string &fileName, const string &args)
    {
        return system((fileName + " " + args + " &").c_str()) == 0 ? true: false;
    }

    bool isProcessRunning(const string &fileName)
    {      
        DIR *proc_dir = opendir("/proc");
        if (!proc_dir)
            return false;

        struct dirent* entry;
        while ((entry = readdir(proc_dir)) != NULL) {
            if (entry->d_type == DT_DIR && strtol(entry->d_name, NULL, 10) > 0) {
                char cmd_file[256];
                snprintf(cmd_file, sizeof(cmd_file), "%s/%s/cmdline", "/proc", entry->d_name);

                FILE* cmd_file_ptr = fopen(cmd_file, "r");
                if (!cmd_file_ptr)
                    continue;

                char cmd_line[256];
                fgets(cmd_line, sizeof(cmd_line), cmd_file_ptr);
                fclose(cmd_file_ptr);

                if (strncmp(cmd_line, fileName.c_str(), strlen(fileName.c_str())) == 0) {
                    closedir(proc_dir);
                    return true;
                }
            }
        }

        closedir(proc_dir);
        return false;
    }

    bool fileExists(const string &filePath)
    {
        struct stat st;
        if (stat(filePath.c_str(), &st) != 0)
            return false;
        return S_ISREG(st.st_mode);
    }

    bool dirExists(const string &dirName) {
        struct stat st;
        if (stat(dirName.c_str(), &st) != 0)
            return false;
        return S_ISDIR(st.st_mode);
    }

    bool dirIsEmpty(const string &dirName)
    {
        DIR *dir = opendir(dirName.c_str());
        if (!dir)
            return true;

        int count = 0;
        struct dirent *entry;
        while (count == 0 && (entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
                count++;
        }
        closedir(dir);
        return (count == 0);
    }

    bool makePath(const string &path)
    {
        list<string> pathsList;
        string last_path(path);
        while (!last_path.empty() && !dirExists(last_path)) {
            pathsList.push_front(last_path);
            last_path = parentPath(last_path);
        }
        for (list<string>::iterator it = pathsList.begin(); it != pathsList.end(); ++it) {
            if (mkdir(it->c_str(), S_IRWXU | S_IRWXG | S_IRWXO) != 0)
                return false;
        }
        return true;
    }

    bool replaceFile(const string &oldFilePath, const string &newFilePath)
    {
        return rename(oldFilePath.c_str(), newFilePath.c_str()) == 0 ? true: false;
    }

    bool removeFile(const string &filePath)
    {
        return (remove(filePath.c_str()) == 0) ? true: false;
    }

    bool removeDirRecursively(const string &dir)
    {
        DIR *_dir = opendir(dir.c_str());
        if (!_dir) {
            // Error opening directory
            return false;
        }

        struct dirent *entry;
        while ((entry = readdir(_dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;

            char path[PATH_MAX];
            snprintf(path, sizeof(path), "%s/%s", dir.c_str(), entry->d_name);
            struct stat info;
            if (stat(path, &info) != 0) {
                // Error getting file information
                closedir(_dir);
                return false;
            }

            if (S_ISDIR(info.st_mode))
                removeDirRecursively(path);
            else {
                if (remove(path) != 0) {
                    // File deletion error
                    closedir(_dir);
                    return false;
                }
            }
        }

        closedir(_dir);
        if (rmdir(dir.c_str()) != 0) {
            // Directory deletion error
            return false;
        }
        return true;
    }

//    string fromNativeSeparators(const string &path)
//    {
//        return std::regex_replace(path, std::regex("\\\\"), "/");
//    }

//    string toNativeSeparators(const string &path)
//    {
//        return std::regex_replace(path, std::regex("\\/"), "\\");
//    }

    string parentPath(const string &path)
    {
        string::size_type delim = path.find_last_of("\\/");
        return (delim == string::npos) ? "" : path.substr(0, delim);
    }

    string tempPath()
    {
        const char *path = getenv("TMP");
        if (!path)
            path = getenv("TEMP");
        if (!path)
            path = getenv("TMPDIR");
        if (!path)
            path = "/tmp";
        return string(path);
    }

    string appPath()
    {
        return app_path;
    }

    string getFileHash(const string &fileName)
    {
        FILE *file = fopen(fileName.c_str(), "rb");
        if (!file)
            return "";

        int bytes;
        unsigned char data[1024];
        unsigned char digest[MD5_DIGEST_LENGTH];
        MD5_CTX mdContext;
        MD5_Init(&mdContext);
        while ((bytes = fread(data, 1, 1024, file)) != 0)
            MD5_Update(&mdContext, data, bytes);

        MD5_Final(digest, &mdContext);
        fclose(file);

        std::ostringstream oss;
        for (int i = 0; i < MD5_DIGEST_LENGTH; i++) {
            oss.fill('0');
            oss.width(2);
            oss << std::hex << static_cast<const int>(digest[i]);
        }
        return oss.str();
    }

//    bool verifyEmbeddedSignature(const string &fileName)
//    {
//        return false;
//    }
}

namespace NS_Logger
{
    bool allow_write_log = false;

    void AllowWriteLog()
    {
        allow_write_log = true;
    }

    void WriteLog(const string &log, bool showMessage)
    {
        if (allow_write_log) {
            string filpPath(NS_File::appPath() + "/service_log.txt");
            std::ofstream file(filpPath.c_str(), std::ios::app);
            if (!file.is_open()) {
                return;
            }
            file << log << std::endl;
            file.close();
        }
        if (showMessage)
            NS_Utils::ShowMessage(log);
    }
}
