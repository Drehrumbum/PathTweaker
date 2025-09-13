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
* (c) 2024-25 Heiko Vogel <hevog@gmx.de>
*
*/

#include "PathTweaker.h"
#include "resource1.h"
#include <Dbt.h>
#include <Shlwapi.h>
#include <intrin.h>

#pragma comment(lib, "Shlwapi.lib")

#define CLICK_DLG_CONTROL(CONTROL_ID) SendMessage(GetDlgItem(hDlg, CONTROL_ID), BM_CLICK, 0, 0)
#define TIMER_ON  SetTimer(hDlg, 123, 2000, NULL);
#define TIMER_OFF KillTimer(hDlg,123);
#define COLREF_QIRXBLUE (RGB(25, 88, 132))
#define COLREF_HALFRED (RGB(128, 0, 0))
#define PT_DRIVE_REMOVED 0
#define PT_DRIVE_ARRIVED 1


extern int ProcessQirxXMLFile(char* szInOutNodeContent, const char* nodeNeedle, int configReadWrite);
extern DWORD WINAPI DiskSpaceThread(LPVOID param);
extern DWORD WINAPI SelectFolderThread(LPVOID param);
extern int CheckPathExists(char* path);
extern void WriteDlgConfigFile();


void UpdateDlgControls();
int SerialCheck(char* szStoredExternalPath, DWORD storedSerial);
DWORD GetVolumeSerial(char* pathOnDrive);
int ValidatePath(_DEV_BROADCAST_VOLUME* dbv, int mode, char* szStoredExternalPath, int storedSerial);
void MakePathCurrent();
void ResetPath();



// a little margin around the path looks better than the path 
// ellipsis stuff in the proporties

void CompactPathAndSetLabelText(int controlID, char* szText) {
    char tmp[MAX_PATH_BUFFER_SIZE];
    lstrcpyn(tmp, szText, MAX_PATH_BUFFER_SIZE);
    PathCompactPath(NULL, tmp, pPTM->labelWidth);
    SetDlgItemText(pPTM->hWndDialog, controlID, tmp);
}

// size/position of control relative to parent
void GetRelativeCtrlRect(HWND hWnd, RECT* rc) {
    GetWindowRect(hWnd, rc);
    ScreenToClient(GetParent(hWnd), (LPPOINT) & ((LPPOINT)rc)[0]);
    ScreenToClient(GetParent(hWnd), (LPPOINT) & ((LPPOINT)rc)[1]);
}

void InitTransparencySlider(HWND hSlider, int value) {
    SendMessage(hSlider, TBM_SETRANGE, true, MAKELONG(64, 255));
    SendMessage(hSlider, TBM_SETPOS, true, value);
}


void StopSpaceThread() {
    ResetEvent(pPTM->hWaitPathSwitch); // block disk_space_thread
    pPTM->flagNewPath = 1;
}

INT_PTR CALLBACK MainDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    static HBRUSH backgroundQirx, backgroundHalfRed;
    static BOOL doubleClickSwitcher = 0, timerSwitcher = 0;
    HDC hdc;
    RECT rc;
    INT_PTR ret = 0;
    HANDLE ht;
    static HFONT hFont;
    int answer;
    char buf[64];

    switch (message) {
    case WM_CTLCOLORSTATIC: { // red background for ext. path label
        if ((pPTM->currentNodeSelection == NODE_RAW && !pPTM->flagRawDriveOnline) ||
            (pPTM->currentNodeSelection == NODE_AUD && !pPTM->flagAudDriveOnline) ||
            (pPTM->currentNodeSelection == NODE_ETI && !pPTM->flagEtiDriveOnline) ||
            (pPTM->currentNodeSelection == NODE_TII && !pPTM->flagTiiDriveOnline)) {
            if (GetDlgItem(hDlg, IDC_LBL_EXTERNAL_PATH) == (HWND)lParam) {
                hdc = (HDC)wParam;
                SetBkMode(hdc, TRANSPARENT);
                SetBkColor(hdc, COLREF_HALFRED);
                SetTextColor(hdc, RGB(255, 255, 0));
                return  (INT_PTR)(backgroundHalfRed);
            }
        }
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSCROLLBAR: {
        hdc = (HDC)wParam;
        SetBkMode(hdc, TRANSPARENT);
        SetBkColor(hdc, COLREF_QIRXBLUE);
        SetTextColor(hdc, RGB(255, 255, 255));
        ret = (INT_PTR)(backgroundQirx);
        break;
    }


    case WM_TIMER: {
// One timer-message may still wait in the message-queue after killing
// the timer, so the wrong text may be written into the label.
// Make sure we always write the correct text into the label.
        switch (pPTM->currentNodeSelection) {
        case NODE_RAW:
            if (timerSwitcher || (pPTM->flagRawDriveOnline)) {
                CompactPathAndSetLabelText(IDC_LBL_EXTERNAL_PATH, pPTM->mDlgSet.szExtRawPath);
                timerSwitcher = 0;
            }
            else {
                if (pPTM->mDlgSet.rawPathDriveSerial)
                    SetDlgItemText(hDlg, IDC_LBL_EXTERNAL_PATH, szPathNotAvailable);
                else
                    SetDlgItemText(hDlg, IDC_LBL_EXTERNAL_PATH, szPathNotSet);
                timerSwitcher = 1;
            }
            break;

        case NODE_AUD:
            if (timerSwitcher || (pPTM->flagAudDriveOnline)) {
                CompactPathAndSetLabelText(IDC_LBL_EXTERNAL_PATH, pPTM->mDlgSet.szExtAudPath);
                timerSwitcher = 0;
            }
            else {
                if (pPTM->mDlgSet.audPathDriveSerial)
                    SetDlgItemText(hDlg, IDC_LBL_EXTERNAL_PATH, szPathNotAvailable);
                else
                    SetDlgItemText(hDlg, IDC_LBL_EXTERNAL_PATH, szPathNotSet);
                timerSwitcher = 1;
            }
            break;

        case NODE_ETI:
            if (timerSwitcher || (pPTM->flagEtiDriveOnline)) {
                CompactPathAndSetLabelText(IDC_LBL_EXTERNAL_PATH, pPTM->mDlgSet.szExtEtiPath);
                timerSwitcher = 0;
            }
            else {
                if (pPTM->mDlgSet.audPathDriveSerial)
                    SetDlgItemText(hDlg, IDC_LBL_EXTERNAL_PATH, szPathNotAvailable);
                else
                    SetDlgItemText(hDlg, IDC_LBL_EXTERNAL_PATH, szPathNotSet);
                timerSwitcher = 1;
            }
            break;

        default:
            if (timerSwitcher || (pPTM->flagTiiDriveOnline)) {
                CompactPathAndSetLabelText(IDC_LBL_EXTERNAL_PATH, pPTM->mDlgSet.szExtTiiPath);
                timerSwitcher = 0;
            }
            else {
                if (pPTM->mDlgSet.tiiPathDriveSerial)
                    SetDlgItemText(hDlg, IDC_LBL_EXTERNAL_PATH, szPathNotAvailable);
                else
                    SetDlgItemText(hDlg, IDC_LBL_EXTERNAL_PATH, szPathNotSet);
                timerSwitcher = 1;
            }
            break;
        }
        break;
    }


    case WM_COMMAND: {
        if (HIWORD(wParam) == BN_CLICKED) {
            switch (LOWORD(wParam)) {
                case IDC_CHECK_TOPMOST: {
                    pPTM->mDlgSet.topMost = IsDlgButtonChecked(hDlg, IDC_CHECK_TOPMOST);
                    if (pPTM->mDlgSet.topMost)
                        SetWindowPos(hDlg, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
                    else
                        SetWindowPos(hDlg, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
                    break;
                }
                case IDC_BUTTON_SELECT_FOLDER: {
                    EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_SELECT_FOLDER), 0); // avoid re-entry
                    EnableWindow(GetDlgItem(hDlg, IDC_COMBO_PATH_SELECTOR), 0); // avoid switching
                    switch (pPTM->currentNodeSelection) {
                    case NODE_RAW:
                        ht = CreateThread(NULL, 0, SelectFolderThread, pPTM->mDlgSet.szExtRawPath, 0, NULL);
                        break;
                    case NODE_AUD:
                        ht = CreateThread(NULL, 0, SelectFolderThread, pPTM->mDlgSet.szExtAudPath, 0, NULL);
                        break;
                    case NODE_ETI:
                        ht = CreateThread(NULL, 0, SelectFolderThread, pPTM->mDlgSet.szExtEtiPath, 0, NULL);
                        break;
                    default:
                        ht = CreateThread(NULL, 0, SelectFolderThread, pPTM->mDlgSet.szExtTiiPath, 0, NULL);
                        break;
                    }
                    CloseHandle(ht);
                    break;
                }
                case IDC_BUTTON_SET_PATH: {
                    StopSpaceThread();
                    MakePathCurrent();
                    UpdateDlgControls();
                    SetEvent(pPTM->hWaitPathSwitch); // unblock disk_space_thread
                    break;
                }
                case IDC_BUTTON_RESET_PATH: {
                    StopSpaceThread();
                    ResetPath();
                    UpdateDlgControls();
                    SetEvent(pPTM->hWaitPathSwitch);
                    break;
                }
                case IDC_CHECK_AUTO_PATH_SWAP: {
                    pPTM->mDlgSet.autoPathSwap = IsDlgButtonChecked(hDlg, IDC_CHECK_AUTO_PATH_SWAP);

                    if (pPTM->mDlgSet.autoPathSwap) {
                        StopSpaceThread();
                        answer = pPTM->currentNodeSelection; // temp save
                        pPTM->currentNodeSelection = NODE_RAW;
                        if (pPTM->flagRawDriveOnline)
                            MakePathCurrent();

                        pPTM->currentNodeSelection = NODE_AUD;
                        if (pPTM->flagAudDriveOnline)
                            MakePathCurrent();

                        pPTM->currentNodeSelection = NODE_TII;
                        if (pPTM->flagTiiDriveOnline)
                            MakePathCurrent();

                        if (pPTM->flagIsQ5) {
                            pPTM->currentNodeSelection = NODE_ETI;
                            if (pPTM->flagEtiDriveOnline)
                                MakePathCurrent();
                        }

                        pPTM->currentNodeSelection = answer;
                        UpdateDlgControls();
                        SetEvent(pPTM->hWaitPathSwitch); // unblock disk_space_thread
                    }
                    break;
                }
                case IDCLOSE: {
                    PostMessage(hDlg, WM_CLOSE, 0, 0);
                    break;
                }
            } // LOWORD(wParam)
        } // BN_CLICKED
        else if (HIWORD(wParam) == CBN_SELCHANGE) {
            if (LOWORD(wParam) == IDC_COMBO_PATH_SELECTOR) {
                StopSpaceThread();
                pPTM->currentNodeSelection = SendMessage(pPTM->hWndCbNodeSel, CB_GETCURSEL, 0, 0);
                switch (pPTM->currentNodeSelection) {
                case NODE_RAW:
                    sprintf(buf, " %s %s ", szRaw, szGroupBoxLabel);
                    break;
                case NODE_AUD:
                    sprintf(buf, " %s %s ", szAudio, szGroupBoxLabel);
                    break;
                case NODE_ETI:
                    sprintf(buf, " %s %s ", szEti, szGroupBoxLabel);
                    break;
                default:
                    sprintf(buf, " %s %s ", szTii, szGroupBoxLabel);
                    break;
                }
                SetDlgItemText(hDlg, IDC_GROUP_DRIVE, buf);
                UpdateDlgControls();
                SetEvent(pPTM->hWaitPathSwitch);
            }
        }
        break;
    }


    case WM_DEVICECHANGE: {
        switch (wParam) {
            case DBT_DEVICEARRIVAL: {
                StopSpaceThread();

                if (ValidatePath((_DEV_BROADCAST_VOLUME*)lParam, PT_DRIVE_ARRIVED,
                    pPTM->mDlgSet.szExtRawPath, pPTM->mDlgSet.rawPathDriveSerial)) {
                    pPTM->flagRawDriveOnline = 1;
                    pPTM->flagRawDriveSet = 0;

                    if (pPTM->mDlgSet.autoPathSwap) {
                        if (ProcessQirxXMLFile(pPTM->mDlgSet.szExtRawPath, needleRawOut, CONFIG_WRITE)) {
                            memcpy(pPTM->szCurrentRawPath, pPTM->mDlgSet.szExtRawPath, MAX_PATH_BUFFER_SIZE);
                            pPTM->flagRawDriveSet = 1;
                        }
                    }
                }

                if (ValidatePath((_DEV_BROADCAST_VOLUME*)lParam, PT_DRIVE_ARRIVED,
                    pPTM->mDlgSet.szExtAudPath, pPTM->mDlgSet.audPathDriveSerial)) {
                    pPTM->flagAudDriveOnline = 1;
                    pPTM->flagAudDriveSet = 0;

                    if (pPTM->mDlgSet.autoPathSwap) {
                        if (ProcessQirxXMLFile(pPTM->mDlgSet.szExtAudPath, needleAudOut, CONFIG_WRITE)) {
                            memcpy(pPTM->szCurrentAudPath, pPTM->mDlgSet.szExtAudPath, MAX_PATH_BUFFER_SIZE);
                            pPTM->flagAudDriveSet = 1;
                        }
                    }
                }

                if (ValidatePath((_DEV_BROADCAST_VOLUME*)lParam, PT_DRIVE_ARRIVED,
                    pPTM->mDlgSet.szExtTiiPath, pPTM->mDlgSet.tiiPathDriveSerial)) {
                    pPTM->flagTiiDriveOnline = 1;
                    pPTM->flagTiiDriveSet = 0;

                    if (pPTM->mDlgSet.autoPathSwap) {
                        if (ProcessQirxXMLFile(pPTM->mDlgSet.szExtTiiPath, needleTiiLog, CONFIG_WRITE)) {
                            memcpy(pPTM->szCurrentTiiPath, pPTM->mDlgSet.szExtTiiPath, MAX_PATH_BUFFER_SIZE);
                            pPTM->flagTiiDriveSet = 1;
                        }
                    }
                }
  
                if (pPTM->flagIsQ5) {

                    if (ValidatePath((_DEV_BROADCAST_VOLUME*)lParam, PT_DRIVE_ARRIVED,
                        pPTM->mDlgSet.szExtEtiPath, pPTM->mDlgSet.etiPathDriveSerial)) {
                        pPTM->flagEtiDriveOnline = 1;
                        pPTM->flagEtiDriveSet = 0;
                        
                        if (pPTM->mDlgSet.autoPathSwap) {
                            if (ProcessQirxXMLFile(pPTM->mDlgSet.szExtEtiPath, needleEtiOut, CONFIG_WRITE)) {
                                memcpy(pPTM->szCurrentEtiPath, pPTM->mDlgSet.szExtEtiPath, MAX_PATH_BUFFER_SIZE);
                                pPTM->flagEtiDriveSet = 1;
                            }
                        }
                    }

                    if (pPTM->flagAudDriveOnline && pPTM->flagRawDriveOnline
                        && pPTM->flagTiiDriveOnline && pPTM->flagEtiDriveOnline)
                        TIMER_OFF;
                }
                else {

                    if (pPTM->flagAudDriveOnline && pPTM->flagRawDriveOnline
                        && pPTM->flagTiiDriveOnline)
                        TIMER_OFF;
                }

                UpdateDlgControls();
                SetEvent(pPTM->hWaitPathSwitch); // go, disk_space_thread
                ret = true;
                break;
            }

            case DBT_DEVICEREMOVECOMPLETE: {
                StopSpaceThread();

                if (ValidatePath((_DEV_BROADCAST_VOLUME*)lParam, PT_DRIVE_REMOVED,
                    pPTM->mDlgSet.szExtRawPath, pPTM->mDlgSet.rawPathDriveSerial)) {
                    pPTM->flagRawDriveOnline = 0;

                    if (ProcessQirxXMLFile(pPTM->szOriginalRawPath, needleRawOut, CONFIG_WRITE)) {
                        memcpy(pPTM->szCurrentRawPath, pPTM->szOriginalRawPath, MAX_PATH_BUFFER_SIZE);
                        pPTM->flagRawDriveSet = 0;
                    }
                }

                if (ValidatePath((_DEV_BROADCAST_VOLUME*)lParam, PT_DRIVE_REMOVED,
                    pPTM->mDlgSet.szExtAudPath, pPTM->mDlgSet.audPathDriveSerial)) {
                    pPTM->flagAudDriveOnline = 0;

                    if (ProcessQirxXMLFile(pPTM->szOriginalAudPath, needleAudOut, CONFIG_WRITE)) {
                        memcpy(pPTM->szCurrentAudPath, pPTM->szOriginalAudPath, MAX_PATH_BUFFER_SIZE);
                        pPTM->flagAudDriveSet = 0;
                    }
                }

                if (ValidatePath((_DEV_BROADCAST_VOLUME*)lParam, PT_DRIVE_REMOVED,
                    pPTM->mDlgSet.szExtTiiPath, pPTM->mDlgSet.tiiPathDriveSerial)) {
                    pPTM->flagTiiDriveOnline = 0;

                    if (ProcessQirxXMLFile(pPTM->szOriginalTiiPath, needleTiiLog, CONFIG_WRITE)) {
                        memcpy(pPTM->szCurrentTiiPath, pPTM->szOriginalTiiPath, MAX_PATH_BUFFER_SIZE);
                        pPTM->flagTiiDriveSet = 0;
                    }
                }

                if (pPTM->flagIsQ5) {
                    
                    if (ValidatePath((_DEV_BROADCAST_VOLUME*)lParam, PT_DRIVE_REMOVED,
                        pPTM->mDlgSet.szExtEtiPath, pPTM->mDlgSet.etiPathDriveSerial)) {
                        pPTM->flagEtiDriveOnline = 0;
                        
                        if (ProcessQirxXMLFile(pPTM->szOriginalEtiPath, needleEtiOut, CONFIG_WRITE)) {
                            memcpy(pPTM->szCurrentEtiPath, pPTM->szOriginalEtiPath, MAX_PATH_BUFFER_SIZE);
                            pPTM->flagEtiDriveSet = 0;
                        }
                    }

                    if (!pPTM->flagAudDriveOnline || !pPTM->flagRawDriveOnline ||
                        !pPTM->flagTiiDriveOnline || !pPTM->flagEtiDriveOnline)
                        TIMER_ON;
                }
                else {
                    if (!pPTM->flagAudDriveOnline || !pPTM->flagRawDriveOnline ||
                        !pPTM->flagTiiDriveOnline)
                        TIMER_ON;
                }

                UpdateDlgControls();
                SetEvent(pPTM->hWaitPathSwitch); // go, disk_space_thread
                ret = true;
                break;
            }
        }
        break;
    }
    
    
    case WM_HSCROLL: {
        switch (LOWORD(wParam)) {
        case TB_THUMBPOSITION:
        case TB_THUMBTRACK:
        case TB_LINEUP:
        case TB_LINEDOWN:
        case TB_PAGEUP:
        case TB_PAGEDOWN:
            pPTM->mDlgSet.transparency = SendMessage(
                GetDlgItem(hDlg, IDC_SLIDER_TRANSP), TBM_GETPOS, 0, 0);
            SetLayeredWindowAttributes(
                hDlg, 0, pPTM->mDlgSet.transparency, LWA_ALPHA);
            break;
        }
        break;
    }
    case WM_LBUTTONDBLCLK: {
        GetWindowRect(hDlg, &rc);

        if (doubleClickSwitcher) {
            pPTM->mDlgSet.collapsed = 0;
            SetWindowPos(hDlg, HWND_TOP, 0, 0,
                rc.right - rc.left, 308, SWP_NOMOVE);

            GetRelativeCtrlRect(GetDlgItem(hDlg, IDC_GROUP_DRIVE), &rc);
            SetWindowPos(GetDlgItem(hDlg, IDC_GROUP_DRIVE), HWND_TOP, 0, 0,
                rc.right - rc.left, 186, SWP_NOMOVE);
            doubleClickSwitcher = 0;
        }
        else {
            pPTM->mDlgSet.collapsed = 1;
            SetWindowPos(hDlg, HWND_TOP, 0, 0,
                rc.right - rc.left, 85, SWP_NOMOVE);
            GetRelativeCtrlRect(GetDlgItem(hDlg, IDC_GROUP_DRIVE), &rc);
            SetWindowPos(GetDlgItem(hDlg, IDC_GROUP_DRIVE), HWND_TOP, 0, 0,
                rc.right - rc.left, 41, SWP_NOMOVE);
            doubleClickSwitcher = 1;
        }
        break;
    }


    case PTMSG_FOLDER_SELECTION_READY: {
        switch (pPTM->currentNodeSelection) {
        case NODE_RAW:
            pPTM->flagRawDriveOnline = 1;
            pPTM->mDlgSet.rawPathDriveSerial = GetVolumeSerial(pPTM->mDlgSet.szExtRawPath);
            break;

        case NODE_AUD:
            pPTM->flagAudDriveOnline = 1;
            pPTM->mDlgSet.audPathDriveSerial = GetVolumeSerial(pPTM->mDlgSet.szExtAudPath);
            break;

        case NODE_ETI:
            pPTM->flagEtiDriveOnline = 1;
            pPTM->mDlgSet.etiPathDriveSerial = GetVolumeSerial(pPTM->mDlgSet.szExtEtiPath);
            break;

        default:
            pPTM->flagTiiDriveOnline = 1;
            pPTM->mDlgSet.tiiPathDriveSerial = GetVolumeSerial(pPTM->mDlgSet.szExtTiiPath);
            break;
        }

        EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_SELECT_FOLDER), 1);
        EnableWindow(GetDlgItem(hDlg, IDC_COMBO_PATH_SELECTOR), 1);

        if (pPTM->mDlgSet.autoPathSwap) {
            StopSpaceThread();
            MakePathCurrent();
            SetEvent(pPTM->hWaitPathSwitch);
        }

        UpdateDlgControls();
        WriteDlgConfigFile();

        if (pPTM->flagIsQ5) {
            if (pPTM->flagAudDriveOnline && pPTM->flagRawDriveOnline &&
                pPTM->flagTiiDriveOnline && pPTM->flagEtiDriveOnline)
                TIMER_OFF;
        }
        else {
            if (pPTM->flagAudDriveOnline && pPTM->flagRawDriveOnline &&
                pPTM->flagTiiDriveOnline && pPTM->flagEtiDriveOnline)
                TIMER_OFF;
        }

        break;
    }
    case PTMSG_FOLDER_SELECTION_ERROR: {
        MessageBox(hDlg, szMsgSelectFolderErr, szAppName, MB_ICONERROR | MB_SETFOREGROUND);
    }
    case PTMSG_FOLDER_SELECTION_CANCEL: {
        EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_SELECT_FOLDER), 1);
        EnableWindow(GetDlgItem(hDlg, IDC_COMBO_PATH_SELECTOR), 1);
        break;
    }


    case WM_INITDIALOG: {
        pPTM->hWndDialog = hDlg;
        pPTM->hWndLbRemRecTime = GetDlgItem(hDlg, IDC_LBL_REM_REC_TIME);
        pPTM->hWndLbWriteSpeed = GetDlgItem(hDlg, IDC_LBL_WRITE_SPEED);
        pPTM->hWndCbNodeSel = GetDlgItem(hDlg, IDC_COMBO_PATH_SELECTOR);

        SetWindowText(hDlg, pPTM->szMyWindowTitle);
        sprintf(buf, " %s %s ", szRaw, szGroupBoxLabel);
        SetDlgItemText(hDlg, IDC_GROUP_DRIVE, buf);

        hFont = CreateFont(22, 0, 0, 0, FW_BOLD, FALSE, FALSE,
            0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
            DEFAULT_PITCH | FF_MODERN, NULL);
        SendMessage(pPTM->hWndLbRemRecTime, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(pPTM->hWndLbWriteSpeed, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(pPTM->hWndCbNodeSel, CB_ADDSTRING, 0, (LPARAM)szRaw);
        SendMessage(pPTM->hWndCbNodeSel, CB_ADDSTRING, 0, (LPARAM)szAudio);
        SendMessage(pPTM->hWndCbNodeSel, CB_ADDSTRING, 0, (LPARAM)szTii);
        // Read the original paths from the config-file.
        if (ProcessQirxXMLFile(pPTM->szOriginalRawPath, needleRawOut, CONFIG_READ))
            memcpy(pPTM->szCurrentRawPath, pPTM->szOriginalRawPath, MAX_PATH_BUFFER_SIZE);
        if (ProcessQirxXMLFile(pPTM->szOriginalAudPath, needleAudOut, CONFIG_READ))
            memcpy(pPTM->szCurrentAudPath, pPTM->szOriginalAudPath, MAX_PATH_BUFFER_SIZE);
        if (ProcessQirxXMLFile(pPTM->szOriginalTiiPath, needleTiiLog, CONFIG_READ))
            memcpy(pPTM->szCurrentTiiPath, pPTM->szOriginalTiiPath, MAX_PATH_BUFFER_SIZE);

        if (pPTM->flagIsQ5) { // skip entry if version !5
            SendMessage(pPTM->hWndCbNodeSel, CB_ADDSTRING, 0, (LPARAM)szEti);
            if (ProcessQirxXMLFile(pPTM->szOriginalEtiPath, needleEtiOut, CONFIG_READ))
                memcpy(pPTM->szCurrentEtiPath, pPTM->szOriginalEtiPath, MAX_PATH_BUFFER_SIZE);
        }
        
        SendMessage(pPTM->hWndCbNodeSel, CB_SETCURSEL, 0, 0);

        backgroundQirx = CreateSolidBrush(COLREF_QIRXBLUE);
        backgroundHalfRed = CreateSolidBrush(COLREF_HALFRED);

        SetWindowLong(hDlg, GWL_EXSTYLE, GetWindowLong(hDlg, GWL_EXSTYLE) | WS_EX_LAYERED);
        SetWindowPos(hDlg, HWND_TOP, pPTM->mDlgSet.left, pPTM->mDlgSet.top, 0, 0, SWP_NOSIZE);
        InitTransparencySlider(GetDlgItem(hDlg, IDC_SLIDER_TRANSP), pPTM->mDlgSet.transparency);
        SetLayeredWindowAttributes(hDlg, 0, pPTM->mDlgSet.transparency, LWA_ALPHA);

        pPTM->flagRawDriveOnline = CheckPathExists(pPTM->mDlgSet.szExtRawPath);
        if (pPTM->flagRawDriveOnline)
            pPTM->flagRawDriveOnline =
            SerialCheck(pPTM->mDlgSet.szExtRawPath, pPTM->mDlgSet.rawPathDriveSerial);

        pPTM->flagAudDriveOnline = CheckPathExists(pPTM->mDlgSet.szExtAudPath);
        if (pPTM->flagAudDriveOnline)
            pPTM->flagAudDriveOnline =
            SerialCheck(pPTM->mDlgSet.szExtAudPath, pPTM->mDlgSet.audPathDriveSerial);

        pPTM->flagTiiDriveOnline = CheckPathExists(pPTM->mDlgSet.szExtTiiPath);
        if (pPTM->flagTiiDriveOnline)
            pPTM->flagTiiDriveOnline =
            SerialCheck(pPTM->mDlgSet.szExtTiiPath, pPTM->mDlgSet.tiiPathDriveSerial);
        
        if (pPTM->flagIsQ5) {
            pPTM->flagEtiDriveOnline = CheckPathExists(pPTM->mDlgSet.szExtEtiPath);
            if (pPTM->flagEtiDriveOnline)
                pPTM->flagEtiDriveOnline =
                SerialCheck(pPTM->mDlgSet.szExtEtiPath, pPTM->mDlgSet.etiPathDriveSerial);
            
            if (!pPTM->flagAudDriveOnline || !pPTM->flagRawDriveOnline ||
                !pPTM->flagTiiDriveOnline || !pPTM->flagEtiDriveOnline)
                TIMER_ON;
        }
        else {
            if (!pPTM->flagAudDriveOnline || !pPTM->flagRawDriveOnline ||
                !pPTM->flagTiiDriveOnline)
                TIMER_ON;
        }


        // simulating clicks
        if (pPTM->mDlgSet.autoPathSwap)
            CLICK_DLG_CONTROL(IDC_CHECK_AUTO_PATH_SWAP);
        if (pPTM->mDlgSet.topMost)
            CLICK_DLG_CONTROL(IDC_CHECK_TOPMOST);
        if (pPTM->mDlgSet.collapsed)
            SendMessage(hDlg, WM_LBUTTONDBLCLK, 0, 0);

        GetWindowRect(GetDlgItem(hDlg, IDC_LBL_CURRENT_PATH), &rc);
        pPTM->labelWidth = rc.right - rc.left + 48; // needs to be revisited

        UpdateDlgControls();
        pPTM->hDiskSpaceThread = CreateThread(NULL, 0, DiskSpaceThread, NULL, 0, NULL);
        ret = true;
        break;
    }
    case WM_CLOSE: {
        answer = MessageBox(hDlg, szMsgQuit, szAppName,
            MB_ICONQUESTION | MB_YESNO | MB_SETFOREGROUND);

        if (IDYES == answer) {
            if (!pPTM->flagAudDriveOnline || !pPTM->flagRawDriveOnline ||
                !pPTM->flagTiiDriveOnline || !pPTM->flagEtiDriveOnline)
                TIMER_OFF;
            // SC_RESTORE to get useful screen coordinates, if the dialog is currently minimized 
            SendMessage(hDlg, WM_SYSCOMMAND, SC_RESTORE, 0);
            GetWindowRect(hDlg, &rc);
            pPTM->mDlgSet.top = rc.top;
            pPTM->mDlgSet.left = rc.left;
            DeleteObject(backgroundQirx);
            DeleteObject(backgroundHalfRed);
            DeleteObject(hFont);
            DestroyWindow(hDlg);
        }
        break;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    } // end of switch message
    return ret;
}


// The message from the system may report not only one volume on 
// a physical drive, so we have to loop through the unitmask. 

int ValidatePath(_DEV_BROADCAST_VOLUME* dbv, int mode,
    char* szStoredExternalPath, int storedSerial) {
    int ret = 0;
    char driveLetter;
    DWORD mask, idx;

    if (DBT_DEVTYP_VOLUME == dbv->dbcv_devicetype) {
        mask = dbv->dbcv_unitmask;
        int numDevs = __popcntd(mask); // count the volumes

        for (int i = 0; i < numDevs; i++) {
            idx = _bit_scan_forward(mask);
            driveLetter = chDriveBase + idx;
            if (toupper(driveLetter) == toupper(szStoredExternalPath[0])) {
                if (PT_DRIVE_ARRIVED == mode) {
                    if (CheckPathExists(szStoredExternalPath)) {
                        if (SerialCheck(szStoredExternalPath, storedSerial))
                            ret++; // all is fine, go out
                        goto out;
                    }
                    else // path not found on drive, go out
                        goto out;
                }
                else {
                    ret++;
                    goto out;
                }
            }
            // reset the lowest bit and check the next drive-letter (if any)
            _bittestandreset((LONG*)&mask, idx);
        }
    }
out:
    return ret;
}

void MakePathCurrent() {
    switch (pPTM->currentNodeSelection) {
    case NODE_RAW:
        if (ProcessQirxXMLFile(pPTM->mDlgSet.szExtRawPath, needleRawOut, CONFIG_WRITE)) {
            memcpy(pPTM->szCurrentRawPath, pPTM->mDlgSet.szExtRawPath, MAX_PATH_BUFFER_SIZE);
            pPTM->flagRawDriveSet = 1;
        }
        break;

    case NODE_AUD:
        if (ProcessQirxXMLFile(pPTM->mDlgSet.szExtAudPath, needleAudOut, CONFIG_WRITE)) {
            memcpy(pPTM->szCurrentAudPath, pPTM->mDlgSet.szExtAudPath, MAX_PATH_BUFFER_SIZE);
            pPTM->flagAudDriveSet = 1;
        }
        break;

    case NODE_ETI:
        if (ProcessQirxXMLFile(pPTM->mDlgSet.szExtEtiPath, needleEtiOut, CONFIG_WRITE)) {
            memcpy(pPTM->szCurrentEtiPath, pPTM->mDlgSet.szExtEtiPath, MAX_PATH_BUFFER_SIZE);
            pPTM->flagEtiDriveSet = 1;
        }
        break;

    default:
        if (ProcessQirxXMLFile(pPTM->mDlgSet.szExtTiiPath, needleTiiLog, CONFIG_WRITE)) {
            memcpy(pPTM->szCurrentTiiPath, pPTM->mDlgSet.szExtTiiPath, MAX_PATH_BUFFER_SIZE);
            pPTM->flagTiiDriveSet = 1;
        }
        break;
    }
}

void ResetPath() {
    switch (pPTM->currentNodeSelection) {
    case NODE_RAW:
        if (ProcessQirxXMLFile(pPTM->szOriginalRawPath, needleRawOut, CONFIG_WRITE)) {
            memcpy(pPTM->szCurrentRawPath, pPTM->szOriginalRawPath, MAX_PATH_BUFFER_SIZE);
            pPTM->flagRawDriveSet = 0;
        }
        break;
    case NODE_AUD:
        if (ProcessQirxXMLFile(pPTM->szOriginalAudPath, needleAudOut, CONFIG_WRITE)) {
            memcpy(pPTM->szCurrentAudPath, pPTM->szOriginalAudPath, MAX_PATH_BUFFER_SIZE);
            pPTM->flagAudDriveSet = 0;
        }
        break;

    case NODE_ETI:
        if (ProcessQirxXMLFile(pPTM->szOriginalEtiPath, needleEtiOut, CONFIG_WRITE)) {
            memcpy(pPTM->szCurrentEtiPath, pPTM->szOriginalEtiPath, MAX_PATH_BUFFER_SIZE);
            pPTM->flagEtiDriveSet = 0;
        }
        break;
    default:
        if (ProcessQirxXMLFile(pPTM->szOriginalTiiPath, needleTiiLog, CONFIG_WRITE)) {
            memcpy(pPTM->szCurrentTiiPath, pPTM->szOriginalTiiPath, MAX_PATH_BUFFER_SIZE);
            pPTM->flagTiiDriveSet = 0;
        }
        break;
    }

    if (pPTM->mDlgSet.autoPathSwap) {
        CheckDlgButton(pPTM->hWndDialog, IDC_CHECK_AUTO_PATH_SWAP, BST_UNCHECKED);
        pPTM->mDlgSet.autoPathSwap = 0;
    }
}

void DisableAllButtons() {
    EnableWindow(GetDlgItem(pPTM->hWndDialog, IDC_BUTTON_RESET_PATH), 0);
    EnableWindow(GetDlgItem(pPTM->hWndDialog, IDC_BUTTON_SET_PATH), 0);
}

void EnableSetButton() {
    EnableWindow(GetDlgItem(pPTM->hWndDialog, IDC_BUTTON_RESET_PATH), 0);
    EnableWindow(GetDlgItem(pPTM->hWndDialog, IDC_BUTTON_SET_PATH), 1);
}

void EnableAllButtons() {
    EnableWindow(GetDlgItem(pPTM->hWndDialog, IDC_BUTTON_RESET_PATH), 1);
    EnableWindow(GetDlgItem(pPTM->hWndDialog, IDC_BUTTON_SET_PATH), 1);
}

void UpdateDlgControls() {
    int fOn=0, fSet=0;
    switch (pPTM->currentNodeSelection) {
    case NODE_RAW:
        CompactPathAndSetLabelText(IDC_LBL_CURRENT_PATH, pPTM->szCurrentRawPath);
        CompactPathAndSetLabelText(IDC_LBL_EXTERNAL_PATH, pPTM->mDlgSet.szExtRawPath);
        fOn = pPTM->flagRawDriveOnline;
        fSet = pPTM->flagRawDriveSet;
        break;

    case NODE_AUD:
        CompactPathAndSetLabelText(IDC_LBL_CURRENT_PATH, pPTM->szCurrentAudPath);
        CompactPathAndSetLabelText(IDC_LBL_EXTERNAL_PATH, pPTM->mDlgSet.szExtAudPath);
        fOn = pPTM->flagAudDriveOnline;
        fSet = pPTM->flagAudDriveSet;
        break;

    case NODE_ETI:
        CompactPathAndSetLabelText(IDC_LBL_CURRENT_PATH, pPTM->szCurrentEtiPath);
        CompactPathAndSetLabelText(IDC_LBL_EXTERNAL_PATH, pPTM->mDlgSet.szExtEtiPath);
        fOn = pPTM->flagEtiDriveOnline;
        fSet = pPTM->flagEtiDriveSet;
        break;

    default:
        CompactPathAndSetLabelText(IDC_LBL_CURRENT_PATH, pPTM->szCurrentTiiPath);
        CompactPathAndSetLabelText(IDC_LBL_EXTERNAL_PATH, pPTM->mDlgSet.szExtTiiPath);
        fOn = pPTM->flagTiiDriveOnline;
        fSet = pPTM->flagTiiDriveSet;
        break;
    }

    if (fOn)
        if (fSet)
            EnableAllButtons();
        else
            EnableSetButton();
    else
        DisableAllButtons();
}

DWORD GetVolumeSerial(char* pathOnDrive) {
    char drive[8]{};
    DWORD serial = 0;
    memcpy(drive, pathOnDrive, 3);
    GetVolumeInformation(drive, NULL, 0, &serial, NULL, NULL, NULL, 0);
    return serial;
}

int SerialCheck(char* szStoredExternalPath, DWORD storedSerial) {
    int answer, ret = 0;
    char msg[384];

    if (GetVolumeSerial(szStoredExternalPath) == storedSerial)
        ret++;
    else {
        sprintf(msg, "%s\n\n%s", szStoredExternalPath, szMsgSerialCheck);
        answer = MessageBox(pPTM->hWndDialog, msg,
            "PathTweaker - drive serial check", MB_YESNO | MB_SETFOREGROUND | MB_ICONQUESTION);
        if (IDYES == answer)
            ret++;
    }
    return ret;
}
