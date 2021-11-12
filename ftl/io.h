#pragma once
#include "types.h"
#include "nfc.h"

void FTL_Init();

void FTL_Write(uint32 nLPN, uint16 nBufId);
void FTL_Read(uint32 nLPN, uint16 nBufId);

void io_CbDone(uint32 nDie, uint32 nTag);

void IO_Free(CmdInfo* pCmd);

CmdInfo* IO_GetDone(bool bWait);
void IO_WaitDone(CmdInfo* pCmd);

CmdInfo* IO_Read(uint16 nPBN, uint16 nPage, uint16 nBufId);
CmdInfo* IO_Program(uint16 nPBN, uint16 nPage, uint16 nBufId);
CmdInfo* IO_Erase(uint16 nPBN);

void IO_Init();
