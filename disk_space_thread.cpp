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

#define ONE_MEGABYTE (1'000'000.0)
#define MIN_RAW_WRITE_SPEED (4'000'000) // ADS-B (2 MSpl/s and at least two bytes per sample)
#define AVG_UPDATE_INTERVAL 5 // one speed meassurement every 5 seconds

// moving average array for a 'smoother' remaining recording time.
#define AVG_ARRAY_POWER 4
#define AVG_ARRAY_ELEMENTS (1 << AVG_ARRAY_POWER) // 16 elements

struct MOVINGAVERAGEARRAY {
    long long arrayValues[AVG_ARRAY_ELEMENTS];
    long long arrayInsertIndex;
};

long long GetMovingAverage(MOVINGAVERAGEARRAY* pAvgArray);
void InsertMovingAverageValue(MOVINGAVERAGEARRAY* pAvgArray, long long newVal);
void SetAllMovingAverageValues(MOVINGAVERAGEARRAY* pAvgArray, long long value);
void ClearMovingAverageArray(MOVINGAVERAGEARRAY* pAvgArray);



DWORD WINAPI DiskSpaceThread(LPVOID DTM) {
    char buff[32];
    int timeDivider = 2, displayCounter = 0;
    long long speed;
    double dispSpeed, deltaTime, qpfPeriod;
    LARGE_INTEGER qpf, liOldTime, liCurTime;
    ULARGE_INTEGER ullFreeToCaller, ullDisk, ullFree, ullOldSpace;
    unsigned long long remTime, hours, minutes;
    MOVINGAVERAGEARRAY speedAvgArr;

    GetDiskFreeSpaceEx(pPTM->szCurrentRawPath, &ullFreeToCaller, &ullDisk, &ullFree);
    ullOldSpace = ullFreeToCaller;
    ClearMovingAverageArray(&speedAvgArr);
    SetAllMovingAverageValues(&speedAvgArr, MIN_RAW_WRITE_SPEED);
    QueryPerformanceFrequency(&qpf);
    qpfPeriod = 1.0 / qpf.QuadPart;    
    QueryPerformanceCounter(&liOldTime);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_IDLE);

    while (!pPTM->finishThread) {
        if ((2 == timeDivider)) {
            timeDivider = 0;

// Wait here until possible path-switching stuff is over.
// pPTM->flagNewPath is set when we can go again.
            WaitForSingleObject(pPTM->hWaitPathSwitch, INFINITE);
 
            switch (pPTM->currentNodeSelection) {
            case NODE_RAW:
                GetDiskFreeSpaceEx(pPTM->szCurrentRawPath, &ullFreeToCaller, &ullDisk, &ullFree);
                break;
            case NODE_AUD:
                GetDiskFreeSpaceEx(pPTM->szCurrentAudPath, &ullFreeToCaller, &ullDisk, &ullFree);
                break;
            default:
                GetDiskFreeSpaceEx(pPTM->szCurrentTiiPath, &ullFreeToCaller, &ullDisk, &ullFree);
                break;
            }
            
            if (pPTM->flagNewPath) {
                ullOldSpace = ullFreeToCaller;
                SetAllMovingAverageValues(&speedAvgArr, MIN_RAW_WRITE_SPEED);
                displayCounter = 0;
                pPTM->flagNewPath = 0;
            }

            if (displayCounter == AVG_UPDATE_INTERVAL) {
                if (ullFreeToCaller.QuadPart <= ullOldSpace.QuadPart) {

                    QueryPerformanceCounter(&liCurTime);
                    deltaTime = (liCurTime.QuadPart - liOldTime.QuadPart) * qpfPeriod;
                    dispSpeed = (double)(ullOldSpace.QuadPart - ullFreeToCaller.QuadPart) / deltaTime;
                    speed = (long long)dispSpeed;
                    sprintf(buff, "%4.2f", dispSpeed / ONE_MEGABYTE);
                    SetWindowText(pPTM->hWndLbWriteSpeed, buff);

                    if (speed < MIN_RAW_WRITE_SPEED)
                        speed = MIN_RAW_WRITE_SPEED;
                }
                else {
                    SetAllMovingAverageValues(&speedAvgArr, MIN_RAW_WRITE_SPEED);
                    speed = MIN_RAW_WRITE_SPEED;
                }

                InsertMovingAverageValue(&speedAvgArr, speed);
                ullOldSpace = ullFreeToCaller;
                liOldTime = liCurTime;
                displayCounter = 0;               
            }

            remTime  = ullFreeToCaller.QuadPart / GetMovingAverage(&speedAvgArr);
            hours    = remTime / 3600;
            remTime -= hours * 3600;
            minutes  = remTime / 60;
            remTime -= minutes * 60;

            sprintf(buff, "%02i:%02i:%02i", (int)hours, (int)minutes, (int)remTime);
            SetWindowText(pPTM->hWndLbRemRecTime, buff);
            displayCounter++;            
        }
        timeDivider++;
        Sleep(500);
    }
    return 0;
}


inline void InsertMovingAverageValue(MOVINGAVERAGEARRAY* pAvgArray, long long newVal) {
    pAvgArray->arrayValues[pAvgArray->arrayInsertIndex] = newVal;
    ++pAvgArray->arrayInsertIndex &= (AVG_ARRAY_ELEMENTS - 1);
}

inline long long GetMovingAverage(MOVINGAVERAGEARRAY* pAvgArray) {
    long long tmp1, tmp2;
    tmp1 = 0;
    tmp2 = 0;
    for (long long i = 0; i < AVG_ARRAY_ELEMENTS; i += 2) {
        tmp1 += pAvgArray->arrayValues[i];
        tmp2 += pAvgArray->arrayValues[i + 1];
    }
    return (tmp1 + tmp2) / AVG_ARRAY_ELEMENTS;
}

inline void SetAllMovingAverageValues(MOVINGAVERAGEARRAY* pAvgArray, long long value) {
    for (int i = 0; i < AVG_ARRAY_ELEMENTS; i++)
        pAvgArray->arrayValues[i] = value;
}

inline void ClearMovingAverageArray(MOVINGAVERAGEARRAY* pAvgArray) {
    memset(pAvgArray, 0, sizeof(MOVINGAVERAGEARRAY));
}