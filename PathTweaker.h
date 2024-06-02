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



#define WINDOWS_LEAN_AND_MEAN
#undef UNICODE

#include <Windows.h>
#include <commctrl.h>
#include <stdio.h>

#define VCALLOC(size) VirtualAlloc(0, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#define VFREE(x) VirtualFree(x, 0, MEM_RELEASE);
#define MAX_PATH_BUFFER_SIZE (272)  // rounded up to the next multiple of 16

#define CONFIG_READ  0
#define CONFIG_WRITE 1

#define NODE_RAW 0
#define NODE_AUD 1
#define NODE_TII 2

// Some private messages
#define PTMSG_FOLDER_SELECTION_READY   WM_APP
#define PTMSG_FOLDER_SELECTION_CANCEL (WM_APP + 1)
#define PTMSG_FOLDER_SELECTION_ERROR  (WM_APP + 2)

inline const char
szAppName[] = "PathTweaker",
qirx[] = "qirx.exe",
szDlgConfigFile[] = "dlg.dat",
szQirxConfigExt[] = ".config",
needleRawOut[] = "<rawOut value",
needleAudOut[] = "<DAB value",
needleTiiLog[] = "<TIILogger val",
needleQm = '"',
szPathNotSet[] = "SELECT A PATH AT FIRST",
szPathNotAvailable[] = "PATH NOT AVAILABLE",
chDriveBase = 'A',
szRaw[] = "RAW",
szAudio[] = "AUDIO",
szTii[] = "TII",
szGroupBoxLabel[] = "rec. path && drive info ",

szMsgSelectFolder[] =
"Select a folder from an external drive (HDD, USB-Stick, CF-Card) or from another "
"(fixed) disk. Do not select the system-drive or network-drives.",

szMsgSelectFolderErr[] =
"Your selection failed. Try again and select another drive.",

szMsgSerialCheck[] = 
"The path (if any) saved by PathTweaker is present on the drive you connected, "
"but the drive's serial number does not match the serial number also saved.\n\n"
"Use it anyway?",

szMsgNotFound[] = 
"QIRX not found. Copy the program into the program-directory of QIRX version 2, 3 or 4!",

szMsgQuit[] = 
"Do you really want to quit?\n\n"
"All recording paths will be set to their defaults.";


// MAINDLGSETTINGS contains the user-selected options for the dialog.
// Struct goes to the config-file
struct MAINDLGSETTINGS {
    int  left;
    int  top;
    BYTE topMost;
    BYTE transparency;
    BYTE autoPathSwap;
    BYTE collapsed;
    int  z_iReserved[2];
    DWORD  rawPathDriveSerial;
    DWORD  audPathDriveSerial;
    DWORD  tiiPathDriveSerial;
    char szExtRawPath[MAX_PATH_BUFFER_SIZE];
    char szExtAudPath[MAX_PATH_BUFFER_SIZE];
    char szExtTiiPath[MAX_PATH_BUFFER_SIZE];
};

struct PATHTWEAKERMEM {
    char szOriginalRawPath[MAX_PATH_BUFFER_SIZE];
    char szOriginalAudPath[MAX_PATH_BUFFER_SIZE];
    char szOriginalTiiPath[MAX_PATH_BUFFER_SIZE];
    char szCurrentRawPath[MAX_PATH_BUFFER_SIZE];
    char szCurrentAudPath[MAX_PATH_BUFFER_SIZE];
    char szCurrentTiiPath[MAX_PATH_BUFFER_SIZE];
    char szLocalAppDataBasePath[MAX_PATH_BUFFER_SIZE];
    char szDlgFullConfigFileName[MAX_PATH_BUFFER_SIZE];
    char szQirxFullConfigFileName[MAX_PATH_BUFFER_SIZE];
    char szQirxFullConfigBackupFileName[MAX_PATH_BUFFER_SIZE];
    char szQirxVersion[16];
    char szMyWindowTitle[16]; 
    int  haveAppDataBasePath;
    int  haveDlgConfig;
    int  haveQirxConfig;
    int  labelWidth;

    HWND hWndDialog;
    HWND hWndLbRemRecTime;
    HWND hWndLbWriteSpeed;
    HWND hWndCbNodeSel;
    HANDLE hWaitPathSwitch;
    HANDLE hDiskSpaceThread;
    MAINDLGSETTINGS mDlgSet;
    int finishThread;
    int currentNodeSelection;
    int flagRawDriveOnline;
    int flagAudDriveOnline;
    int flagTiiDriveOnline;
    int flagRawDriveSet;
    int flagAudDriveSet;
    int flagTiiDriveSet;
    int flagNewPath;
};
inline PATHTWEAKERMEM* pPTM;
