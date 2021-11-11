#pragma once
#include "types.h"

void FTL_Init();

void FTL_Write(uint32 nLPN, uint16 nBufId);
void FTL_Read(uint32 nLPN, uint16 nBufId);

CmdInfo* io_GetDone();
void io_CbDone(uint32 nDie, uint32 nTag);
void IO_Read(uint16 nPBN, uint16 nPage, uint16 nBufId);
void IO_Program(uint16 nPBN, uint16 nPage, uint16 nBufId);
void IO_Erase(uint16 nPBN);
