#pragma once
#include "types.h"

void UART_InitSim();
void UART_Init(uint32 nBps);
void UART_Puts(char* szString);
void UART_PutsDMA(char* szString);
