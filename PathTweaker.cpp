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
#include "resource1.h"

#pragma comment(lib, "Comctl32.lib")

extern INT_PTR CALLBACK MainDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
extern void CollectPaths();
extern int  ReadDlgConfigFile();
extern void WriteDlgConfigFile();
extern int ProcessQirxXMLFile(char* szInOutNodeContent, const char* nodeNeedle, int configReadWrite);


int WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    MSG msg;
    BOOL retval;
    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_WIN95_CLASSES;

    if (!InitCommonControlsEx(&icc))
        return 1;

    pPTM = (PATHTWEAKERMEM*)VCALLOC(sizeof(PATHTWEAKERMEM));
    if (!pPTM)
        return 2;

    CollectPaths();

    if (pPTM->haveQirxConfig) {
        _strupr_s(pPTM->szQirxVersion, 16);
        sprintf(pPTM->szMyWindowTitle, "%s (%s)", szAppName, pPTM->szQirxVersion);

// More than one instance for QIRX 2, 3 and 4 makes no sense. Quit silently.
        if (!FindWindow(NULL, pPTM->szMyWindowTitle)) {
// Make sure the dialog is visible, if config not exists
            pPTM->mDlgSet.transparency = 255; 
            pPTM->mDlgSet.topMost = 1;
            ReadDlgConfigFile();

            CopyFile(pPTM->szQirxFullConfigFileName, pPTM->szQirxFullConfigBackupFileName, 0);

// Event for stopping the disc_space_thread at the top of its loop
            pPTM->hWaitPathSwitch = CreateEvent( NULL,  // default security attributes
                                                 true,  // manual-reset event
                                                 true,  // initial state is signaled (no blocking)
                                                 NULL);	// object name


            pPTM->hWndDialog = CreateDialog(hInstance,
                MAKEINTRESOURCE(IDD_MAIN), 0, MainDlgProc);

            while ((retval = GetMessage(&msg, 0, 0, 0)) != 0) {
                if (retval == -1)
                    goto err;

                if (!IsDialogMessage(pPTM->hWndDialog, &msg)) {
                    TranslateMessage(&msg);
                    DispatchMessage(&msg);
                }
            }

err:
            if (pPTM->hDiskSpaceThread) {
                pPTM->finishThread = 1;
                WaitForSingleObject(pPTM->hDiskSpaceThread, 1000);
                CloseHandle(pPTM->hDiskSpaceThread);
            }

            CloseHandle(pPTM->hWaitPathSwitch);

// restore the default paths, if ext. paths are set
            if (pPTM->flagRawDriveSet)
                ProcessQirxXMLFile(pPTM->szOriginalRawPath, needleRawOut, CONFIG_WRITE);

            if (pPTM->flagAudDriveSet)
                ProcessQirxXMLFile(pPTM->szOriginalAudPath, needleAudOut, CONFIG_WRITE);
            
            if (pPTM->flagTiiDriveSet)
                ProcessQirxXMLFile(pPTM->szOriginalTiiPath, needleTiiLog, CONFIG_WRITE);

            WriteDlgConfigFile();
        }
    }
    else // No qirx.bat, no fun.
        MessageBox(0, szMsgNotFound, szAppName, MB_ICONSTOP | MB_SETFOREGROUND); 

    VFREE(pPTM);
    return 0;
}