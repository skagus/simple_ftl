#pragma once
#include "types.h"
#include "config.h"

#define NUM_USER_BLK		(PBLK_PER_DIE - 5)
#define LPN_PER_USER_BLK	(CHUNK_PER_PBLK)

#define SIZE_REQ_QUE	(16)


enum Cmd
{
	CMD_WRITE,
	CMD_READ,
};

struct ReqInfo
{
	Cmd eCmd;
	uint32 nLPN;
	uint16 nBuf;
};

typedef void (*CbfReq)(ReqInfo* pReq);

uint32 FTL_GetNumLPN(CbfReq pfCbf);
void FTL_Request(ReqInfo* pReq);

#ifdef EN_SIM
void FTL_Main(void* pParam);
#endif
