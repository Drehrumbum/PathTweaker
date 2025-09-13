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

#define AVG_UPDATE_INTERVAL 20 // one speed meassurement every AVG_UPDATE_INTERVAL seconds
const double cdOneMillionByte = 1'000'000.0; // We want to see the speed in million bytes per sec. ("4.096") 
const double cdMinRawWriteSpeed = 4'000'000.0; // ADS-B (2 MSpl/s and at least two bytes per sample)
const double cdMinEtiWriteSpeed = 250'000.0;   // A little bit below 2 Mbit/s



struct MOVINGAVERAGEARRAY {
    double* pArrayValues;
    unsigned int arrayInsertIndex;
    unsigned int numElements;
};



int InitMovingAverageArray(MOVINGAVERAGEARRAY* pAvgArray, unsigned int numElements);
void InsertMovingAverageValue(MOVINGAVERAGEARRAY* pAvgArray, double newVal);
void SetAllMovingAverageValues(MOVINGAVERAGEARRAY* pAvgArray, double value);
double GetMovingAverage(MOVINGAVERAGEARRAY* pAvgArray);
void FreeMovingAverageArray(MOVINGAVERAGEARRAY* pAvgArray);


DWORD WINAPI DiskSpaceThread(LPVOID DTM) {
    char buff[32];
    int a, b, displayCounter = 0;
    double displaySpeed, deltaTime, qpfPeriod;
    LARGE_INTEGER qpf, liOldTime, liCurTime;
    ULARGE_INTEGER ullFreeToCaller, ullDisk, ullFree, ullOldSpace;
    unsigned long long remTime, hours, minutes;
    MOVINGAVERAGEARRAY speedAvgArr, displaySpeedAvgArr;

    GetDiskFreeSpaceEx(pPTM->szCurrentRawPath, &ullFreeToCaller, &ullDisk, &ullFree);
    ullOldSpace = ullFreeToCaller;
 
    a = InitMovingAverageArray(&speedAvgArr, 20);
    b = InitMovingAverageArray(&displaySpeedAvgArr, 3);

    if (a && b) {
        SetAllMovingAverageValues(&speedAvgArr, cdMinRawWriteSpeed);


        QueryPerformanceFrequency(&qpf);
        qpfPeriod = 1.0 / qpf.QuadPart;
        QueryPerformanceCounter(&liOldTime);
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_IDLE);

        while (!pPTM->finishThread) {
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
            case NODE_ETI:
                GetDiskFreeSpaceEx(pPTM->szCurrentEtiPath, &ullFreeToCaller, &ullDisk, &ullFree);
                break;
            default:
                GetDiskFreeSpaceEx(pPTM->szCurrentTiiPath, &ullFreeToCaller, &ullDisk, &ullFree);
                break;
            }

            if (pPTM->flagNewPath) {
                ullOldSpace = ullFreeToCaller;

                if (pPTM->currentNodeSelection == NODE_ETI)
                    SetAllMovingAverageValues(&speedAvgArr, cdMinEtiWriteSpeed);
                else
                    SetAllMovingAverageValues(&speedAvgArr, cdMinRawWriteSpeed);

                displayCounter = AVG_UPDATE_INTERVAL;
                pPTM->flagNewPath = 0;
            }

            if (displayCounter == AVG_UPDATE_INTERVAL) {
                if (ullFreeToCaller.QuadPart <= ullOldSpace.QuadPart) {

                    QueryPerformanceCounter(&liCurTime);
                    deltaTime = (liCurTime.QuadPart - liOldTime.QuadPart) * qpfPeriod;
                    displaySpeed = (ullOldSpace.QuadPart - ullFreeToCaller.QuadPart) / deltaTime;

                    InsertMovingAverageValue(&displaySpeedAvgArr, displaySpeed);
                    sprintf(buff, "%.3f", GetMovingAverage(&displaySpeedAvgArr) / cdOneMillionByte);
                    SetWindowText(pPTM->hWndLbWriteSpeed, buff);

                    if (pPTM->currentNodeSelection == NODE_ETI) {
                        if (displaySpeed < cdMinEtiWriteSpeed)
                            displaySpeed = cdMinEtiWriteSpeed;
                    }
                    else {
                        if (displaySpeed < cdMinRawWriteSpeed)
                            displaySpeed = cdMinRawWriteSpeed;
                    }
                }
                else {
                    if (pPTM->currentNodeSelection == NODE_ETI) {
                        SetAllMovingAverageValues(&speedAvgArr, cdMinEtiWriteSpeed);
                        displaySpeed = cdMinEtiWriteSpeed;
                    }
                    else {
                        SetAllMovingAverageValues(&speedAvgArr, cdMinRawWriteSpeed);
                        displaySpeed = cdMinRawWriteSpeed;
                    }
                }
                InsertMovingAverageValue(&speedAvgArr, displaySpeed);
                ullOldSpace = ullFreeToCaller;
                liOldTime = liCurTime;
                displayCounter = 0;
            }

            remTime =  (unsigned long long)(ullFreeToCaller.QuadPart / GetMovingAverage(&speedAvgArr));
            hours = remTime / 3600;
            remTime -= hours * 3600;
            minutes = remTime / 60;
            remTime -= minutes * 60;

            sprintf(buff, "%02llu:%02llu:%02llu", hours, minutes, remTime);
            SetWindowText(pPTM->hWndLbRemRecTime, buff);
            displayCounter++;
            Sleep(1000);
        }
    }

    FreeMovingAverageArray(&speedAvgArr);
    FreeMovingAverageArray(&displaySpeedAvgArr);
    return 0;
}

inline int InitMovingAverageArray(MOVINGAVERAGEARRAY* pAvgArray, unsigned int numElements) {
    int ret = 0;
    memset(pAvgArray, 0, sizeof(MOVINGAVERAGEARRAY));    
    
    if ((pAvgArray->pArrayValues = (double*)_aligned_malloc(numElements * sizeof(double), 16))) {
        memset(pAvgArray->pArrayValues, 0, numElements * sizeof(double));
        pAvgArray->numElements = numElements;
        ret++;
    }
    return ret;
}


inline void InsertMovingAverageValue(MOVINGAVERAGEARRAY* pAvgArray, double newVal) {
    pAvgArray->pArrayValues[pAvgArray->arrayInsertIndex] = newVal;
    if (++pAvgArray->arrayInsertIndex == pAvgArray->numElements)
        pAvgArray->arrayInsertIndex = 0;
}

inline double GetMovingAverage(MOVINGAVERAGEARRAY* pAvgArray) {
    double tmp = 0.;

    for (int i = 0; i < pAvgArray->numElements; i++) {
        tmp += pAvgArray->pArrayValues[i];
    }
    return tmp / pAvgArray->numElements;
}

inline void SetAllMovingAverageValues(MOVINGAVERAGEARRAY* pAvgArray, double value) {
    for (int i = 0; i < pAvgArray->numElements; i++)
        pAvgArray->pArrayValues[i] = value;
}

void FreeMovingAverageArray(MOVINGAVERAGEARRAY* pAvgArray) {
    if(pAvgArray->pArrayValues)
        _aligned_free(pAvgArray->pArrayValues);
}
