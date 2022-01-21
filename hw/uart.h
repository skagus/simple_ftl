#pragma once
#include "types.h"

void UART_InitSim();
void UART_Init(uint32 nBps);
void UART_Puts(uint8* szString, uint32 nBytes, bool bDMA = false);
uint32 UART_Gets(uint8* aBuf, uint32 nBufSize);
bool UART_RxD(uint8* pnData);
void UART_TxD(uint8 nData);

// for emulation someone send something.
void SIM_FromOther(uint64 nSkipTime, uint8* aBuf, uint32 nBytes);
