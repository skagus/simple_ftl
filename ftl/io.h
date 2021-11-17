#pragma once
#include "types.h"
#include "nfc.h"

enum CbKey
{
	IOCB_User,
	IOCB_Meta,
	IOCB_Mig,
	NUM_IOCB,
};

typedef void (*IoCbf) (CmdInfo* pDone);

void FTL_Init();
void IO_RegCbf(CbKey eId, IoCbf pfCb);

void FTL_Write(uint32 nLPN, uint16 nBufId, uint8 nTag);
void FTL_Read(uint32 nLPN, uint16 nBufId, uint8 nTag);

void io_CbDone(uint32 nDie, uint32 nTag);

void IO_Free(CmdInfo* pCmd);

CmdInfo* IO_GetDone(CbKey eCbId);
void IO_WaitDone(CmdInfo* pCmd);
CmdInfo* IO_Alloc(CbKey eKey);
void IO_Read(CmdInfo* pCmd, uint16 nPBN, uint16 nPage, uint16 nBufId, uint32 nTag);
void IO_Program(CmdInfo* pCmd, uint16 nPBN, uint16 nPage, uint16 nBufId, uint32 nTag);
void IO_Erase(CmdInfo* pCmd, uint16 nPBN, uint32 nTag);

void IO_Init();
