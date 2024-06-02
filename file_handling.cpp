/*
* This file is part of
*
* PathTweaker, a small tool for tweaking the recording paths of QIRX-SDR
*
*
* This program is free software; you can redistribute it and/or modify it
* under the terms of the GNU General Public License as published by the Free
* Software Foundation; either version 2 of the License, or (at your option)
* any later version.
*
* This program is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
* or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
* more details.
*
* You should have received a copy of the GNU General Public License along with
* this program; if not, write to the Free Software Foundation, Inc., 59 Temple
* Place - Suite 330, Boston, MA 02111-1307, USA and it is distributed under the
* GNU General Public License (GPL).
*
*
* (c) 2024 Heiko Vogel <hevog@gmx.de>
*
*/

#include "PathTweaker.h"

int  GetUserLocalAppDataBasePath(char* szLocalAppDataBasePath);
int  GetSubFolder(char* szInoutBasePath, const char* szSubFolder);
int  CreateSubFolder(char* szInoutBasePath, const char* szNewFolder);
int  GetQirxConfigFileName(char* szQirxFullConfigFileName);



void CollectPaths() {
    char temp[MAX_PATH];
    int status;

    pPTM->haveAppDataBasePath = GetUserLocalAppDataBasePath(pPTM->szLocalAppDataBasePath);
    
    if (pPTM->haveAppDataBasePath) {
       
        pPTM->haveQirxConfig = GetQirxConfigFileName(pPTM->szQirxFullConfigFileName);
        
        if (pPTM->haveQirxConfig) {
            lstrcpyn(temp, pPTM->szLocalAppDataBasePath, MAX_PATH);
            status = GetSubFolder(temp, szAppName);
            if (!status)
                CreateSubFolder(temp, szAppName);
            status = GetSubFolder(temp, pPTM->szQirxVersion);
            if (!status)
                CreateSubFolder(temp, pPTM->szQirxVersion);
            sprintf(pPTM->szDlgFullConfigFileName, "%s\\%s", temp, szDlgConfigFile);
            sprintf(pPTM->szQirxFullConfigBackupFileName, "%s\\%s%s", 
                temp, pPTM->szQirxVersion, szQirxConfigExt);
            pPTM->haveDlgConfig = 1;           
        }
    }
}


int GetUserLocalAppDataBasePath(char* szLocalAppDataBasePath) {
    char tempbuf[MAX_PATH];
    int pathLen, ret = 1; // assume success

    // If GetEnvironmentVariable() fails or if the current path is too long
    // for appending our directory and the filename ("\pathtweaker\qirx4\dlg.dat")
    // later, we'll better fail at this point.
    pathLen = GetEnvironmentVariable("LOCALAPPDATA", tempbuf, MAX_PATH - 29);

    if (!pathLen || pathLen >= MAX_PATH - 29) {
        *szLocalAppDataBasePath = 0;
        ret = 0;
    }
    else
        lstrcpyn(szLocalAppDataBasePath, tempbuf, MAX_PATH);
    return ret;
}

int CheckPathExists(char* path) {
    int ret = 0;

    if (path[0] == 0)
        return ret;

    DWORD att = GetFileAttributes(path);

    if (INVALID_FILE_ATTRIBUTES != att) {
        if (att & FILE_ATTRIBUTE_DIRECTORY)
            ret++;
    }
    return ret;

}

int GetSubFolder(char* szInoutPath, const char* szSubFolder) {
    char buff[MAX_PATH];
    int ret = 0;

    sprintf(buff, "%s\\%s", szInoutPath, szSubFolder);
    if (CheckPathExists(buff)) {
        lstrcpyn(szInoutPath, buff, MAX_PATH); // return the existing path
        ret++;
    }
    return ret;
}

int CreateSubFolder(char* szInoutPath, const char* szNewFolder) {
    int ret = 0;
    char buff[MAX_PATH];
    sprintf(buff, "%s\\%s", szInoutPath, szNewFolder);

    if (CreateDirectory(buff, NULL)) {
        lstrcpyn(szInoutPath, buff, MAX_PATH); // return the new path
        ret++;
    }
    return ret;
}
// Returns version-string (like "qirx4" ) from qirx.bat inside
// of the program-folder. Needed to find QIRXs config-file. Works 
// for Qirx versions 3 and 4. QIRX2 needs a 'faked' qirx.bat inside
// of its program-direcrory.

inline int GetQirxVersionString(char* szVersion) {
    HANDLE hIn;
    LARGE_INTEGER size;
    DWORD dNumBytesRead;
    char* pVersion;
    int ret = 0;
    char input[256];
    memset(input, 0, 256);

    hIn = CreateFile("qirx.bat", GENERIC_READ, 0, 0, OPEN_EXISTING, 0, 0);

    if (INVALID_HANDLE_VALUE != hIn) {
        GetFileSizeEx(hIn, &size);
        if (size.QuadPart < 256) {
            ReadFile(hIn, &input, size.LowPart, &dNumBytesRead, NULL);
            pVersion = strstr(input, qirx);
            if (pVersion) {
                pVersion = strchr(pVersion, 0x20);
                pVersion++;
                memcpy(szVersion, pVersion, 5);
                ret++;
            }
        }
        CloseHandle(hIn);
    }
    return ret;
}

inline int WritePathTest(char* szPath) {
    int ret = 0;
    char tmp[MAX_PATH];
    HANDLE hFi;
    DWORD att = GetFileAttributes(szPath);

    if (INVALID_FILE_ATTRIBUTES != att) {

        if (att & FILE_ATTRIBUTE_DIRECTORY) {
            sprintf(tmp, "%s\\%s", szPath, szDlgConfigFile);
            hFi = CreateFile(tmp, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
            if (INVALID_HANDLE_VALUE != hFi) {
                CloseHandle(hFi);
                DeleteFile(tmp);
                ret++;
            }
        }
    }
    return ret;
}

// Returns the full filename to QIRX's config file.
int GetQirxConfigFileName(char* szQirxFullConfigFileName) {
    char szQirx[16]{};
    char tmp[MAX_PATH];
    int ret = 0;
    if (GetQirxVersionString(szQirx)) {
        lstrcpyn(tmp, pPTM->szLocalAppDataBasePath, MAX_PATH);
        lstrcpyn(pPTM->szQirxVersion, szQirx, 16);
        if (GetSubFolder(tmp, szQirx)) {
            if (WritePathTest(tmp)) {
                lstrcat(tmp, "\\");
                lstrcat(tmp, szQirx);
                lstrcat(tmp, szQirxConfigExt);
                lstrcpyn(szQirxFullConfigFileName, tmp, MAX_PATH);
                ret++;
            }
        }
    }
    return ret;
}

// ReadDlgConfigFile() reads the dialog-settings from disk. 
// If the file does not exists (first run) or the size is not correct
// due to updates, it fails and some defaults will be used.
int ReadDlgConfigFile() {
    DWORD dNumBytesRead = 0;
    LARGE_INTEGER size;
    MAINDLGSETTINGS dummy;
    HANDLE hIni;
    int ret = 0;

    if (pPTM->haveDlgConfig) {
        hIni = CreateFile(pPTM->szDlgFullConfigFileName, GENERIC_READ,
            0, 0, OPEN_EXISTING, 0, 0);
        if (INVALID_HANDLE_VALUE != hIni) {
            GetFileSizeEx(hIni, &size);

            if (sizeof(MAINDLGSETTINGS) == size.LowPart)
                ReadFile(hIni, &dummy, sizeof(MAINDLGSETTINGS), &dNumBytesRead, NULL);

            CloseHandle(hIni);

            if (dNumBytesRead == sizeof(MAINDLGSETTINGS)) {
                memcpy(&pPTM->mDlgSet, &dummy, sizeof(MAINDLGSETTINGS));
                ret++;
            }
        }
    }
    return ret;
}

void WriteDlgConfigFile() {
    DWORD dNumBytesWritten;
    HANDLE hIni;

    if (pPTM->haveDlgConfig) {
        hIni = CreateFile(pPTM->szDlgFullConfigFileName, GENERIC_WRITE,
            0, 0, CREATE_ALWAYS, 0, 0);
        if (INVALID_HANDLE_VALUE != hIni) {
            WriteFile(hIni, &pPTM->mDlgSet, sizeof(MAINDLGSETTINGS),
                &dNumBytesWritten, NULL);
            CloseHandle(hIni);
        }
    }
}
