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
#include <shlobj_core.h>

/// <summary>
/// The SelectFolder thread opens the SHBrowseForFolders dialog.
/// The thread posts the message VITMSG_FOLDER_SELECTION_READY if the user
/// has selected a folder and pushed the OK button. In this case the array
/// pointed to by "param" contains the full path to the selected folder. If the
/// user cancels the dialog, the message VITMSG_FOLDER_SELECTION_CANCEL will
/// be sent instead and the array remains unchanged.
/// The message PTMSG_FOLDER_SELECTION_ERROR will be posted if 
/// SHGetPathFromIDList() fails.
/// </summary>
/// <param name="param">Pointer to an array of char with a size of MAX_PATH.</param>
/// <returns>Nothing</returns>
DWORD WINAPI SelectFolderThread(LPVOID param) {
    char szTemp[MAX_PATH];
    BROWSEINFO bi{};
    PIDLIST_ABSOLUTE pIDL;
    bi.lpszTitle = szMsgSelectFolder;


    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_DONTGOBELOWDOMAIN | BIF_NEWDIALOGSTYLE;
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    if (SUCCEEDED(hr)) {
        pIDL = SHBrowseForFolder(&bi);

        if (pIDL != NULL) {
            if (SHGetPathFromIDList(pIDL, szTemp)) {
                if (lstrlen(szTemp) > 3)
                    lstrcat(szTemp, "\\");
                memset(param, 0, MAX_PATH_BUFFER_SIZE);
                lstrcpyn((char*)param, szTemp, MAX_PATH);
                PostMessage(pPTM->hWndDialog, PTMSG_FOLDER_SELECTION_READY, 0, 0);
            }
            else 
                PostMessage(pPTM->hWndDialog, PTMSG_FOLDER_SELECTION_ERROR, 0, 0);

            CoTaskMemFree((LPVOID)pIDL);
        }
        else
            PostMessage(pPTM->hWndDialog, PTMSG_FOLDER_SELECTION_CANCEL, 0, 0);

        CoUninitialize();
    }
    return 0;
}
