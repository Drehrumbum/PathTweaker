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


// A node inside of the config-file looks like this...
//    <rawOut value="C:\Users\User\AppData\Local/qirx4/Raw/" maxSize="0" />
//    __________     _ <--- We need these two offsets ---> _
//    nodeNeedle          from the beginnig of the file
// This simple parser will do the job

int ProcessQirxXMLFile(char* szInOutNodeContent, const char* nodeNeedle, int configReadWrite) {
    int retryCount = 10, ret = 0;
    HANDLE hfi;
    DWORD dNumBytesRead = 0;
    char* pFileContent, *pLeft, *pRight;
    LARGE_INTEGER liFileSize{};


// Never write empty strings to the config-file
    if (CONFIG_WRITE == configReadWrite && *szInOutNodeContent == 0)
        return 0; 

// We are not urgent and can wait a little to get access to QIRX's
// config-file. Normally, we'll get a file-handle without retry-stuff.
// See "qirx4.config is in use" (#122) in qirx-issues @github.
    do {
        hfi = CreateFile(pPTM->szQirxFullConfigFileName, 
            GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING,
            FILE_FLAG_SEQUENTIAL_SCAN, 0);
        retryCount--;

        if (INVALID_HANDLE_VALUE == hfi)
            Sleep(50);
        
    } while ((INVALID_HANDLE_VALUE == hfi) && (retryCount != 0));

    if (INVALID_HANDLE_VALUE != hfi) {
        
        GetFileSizeEx(hfi, &liFileSize);
        pFileContent = (char*)VCALLOC(liFileSize.LowPart + 1);
            
        if (pFileContent) {
            ReadFile(hfi, pFileContent, liFileSize.LowPart, &dNumBytesRead, NULL);
                
            pLeft = strstr(pFileContent, nodeNeedle);

            if (pLeft) { // needle found, the two q-marks will follow...
                pLeft = strchr(pLeft, needleQm) + 1;
                pRight = strchr(pLeft, needleQm);

                if (pLeft && pRight) {
                    if (CONFIG_WRITE == configReadWrite) {
                        SetFilePointer(hfi, pLeft - pFileContent, NULL, FILE_BEGIN);
                        WriteFile(hfi, szInOutNodeContent,
                            lstrlen(szInOutNodeContent), &dNumBytesRead, NULL);
                        WriteFile(hfi, pRight,
                            pFileContent + liFileSize.QuadPart - pRight, &dNumBytesRead, NULL);
                        SetEndOfFile(hfi);
                    }
                    else {
                        memset(szInOutNodeContent, 0, MAX_PATH_BUFFER_SIZE);
                        memcpy(szInOutNodeContent, pLeft, pRight - pLeft);
                    }
                    ret++;
                }
            }
            VFREE(pFileContent);
        }
        CloseHandle(hfi);
// It seems there is a filechange-notify active in QIRX, which means
// QIRX tries to read the config-file immediately after it was changed
// and closed by other QIRX-threads or PathTweaker. If we re-open the
// file w/o a short break in between, QIRX may get no file-handle, 
// gives up, shows a last MessageBox and quits.
// Here is the best place for waiting, just to catch all calls to
// this procedure at once. This little time-lag does not hurt us.
        Sleep(100);
    }
    return ret;
}
