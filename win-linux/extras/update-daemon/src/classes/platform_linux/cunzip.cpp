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

#include "cunzip.h"
#include "platform_linux/utils.h"
#include <signal.h>


int unzipArchive(const string &zipFilePath, const string &folderPath, std::atomic_bool &run)
{
    if (!NS_File::fileExists(zipFilePath) || !NS_File::dirExists(folderPath))
        return UNZIP_ERROR;

    string cmd = "unzip -o -d ";
    cmd += folderPath + " ";
    cmd += zipFilePath;

    FILE *pipe = popen(cmd.c_str(), "r");
    if (!pipe)
        return UNZIP_ERROR;

    pid_t pid = fileno(pipe);
    char buffer[256];
    int res = UNZIP_OK;
    while (fgets(buffer, sizeof(buffer), pipe)) {
        if (!run) {
            res = UNZIP_ABORT;
            kill(pid, SIGINT);
            break;
        }
    }
    if (pclose(pipe) != 0 && res != UNZIP_ABORT)
        res = UNZIP_ERROR;

    return res;
}

CUnzip::CUnzip()
{
    m_run = false;
}

CUnzip::~CUnzip()
{
    m_run = false;
    if (m_future.valid())
        m_future.wait();
}

void CUnzip::extractArchive(const string &zipFilePath, const string &folderPath)
{
    m_run = false;
    if (m_future.valid())
        m_future.wait();
    m_run = true;
    m_future = std::async(std::launch::async, [=]() {
        int res = unzipArchive(zipFilePath, folderPath, m_run);
        if (m_complete_callback)
            m_complete_callback(res);
    });
}

void CUnzip::stop()
{
    m_run = false;
}

void CUnzip::onComplete(FnVoidInt callback)
{
    m_complete_callback = callback;
}
