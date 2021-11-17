
#pragma once
#include "types.h"

uint8* BM_GetMain(uint16 nBufId);
uint8* BM_GetSpare(uint16 nBufId);

uint16 BM_Alloc();
void BM_Free(uint16 nBuf);
uint16 BM_CountFree();
void BM_Init();
