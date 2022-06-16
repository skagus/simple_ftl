#pragma once
#include "types.h"
#include "nfc.h"

#define NUM_NAND_CMD	(8)

enum CbKey
{
	IOCB_User,
	IOCB_Meta,
	IOCB_Mig,
	IOCB_UErs,
	NUM_IOCB,
};

typedef void (*IoCbf) (CmdInfo* pDone);

void IO_RegCbf(CbKey eId, IoCbf pfCb);

void IO_Free(CmdInfo* pCmd);
CmdInfo* IO_Alloc(CbKey eKey);
uint32 IO_CountFree();

CmdInfo* IO_PopDone(CbKey eCbId);
CmdInfo* IO_GetDone(CbKey eCbId);
void IO_Read(CmdInfo* pCmd, uint16 nPBN, uint16 nPage, uint16 nBufId, uint32 nTag);
void IO_Program(CmdInfo* pCmd, uint16 nPBN, uint16 nPage, uint16 nBufId, uint32 nTag);
void IO_Erase(CmdInfo* pCmd, uint16 nPBN, uint32 nTag);

void IO_Init();
