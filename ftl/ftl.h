
#pragma once
#include "types.h"
#include "config.h"

#define NUM_META_BLK		(7)
#define NUM_USER_BLK		(PBLK_PER_DIE - NUM_META_BLK)

#define BASE_META_BLK		(NUM_USER_BLK)

#define LPN_PER_USER_BLK	(CHUNK_PER_PBLK)

#define NUM_LPN				((NUM_USER_BLK - 12) * (LPN_PER_USER_BLK))

#define SIZE_REQ_QUE		(16)
#define INV_BN				(0xFF)
#define INV_LPN				(0xFFFF)
#define INV_PPO				(0xFF)
#define MARK_ERS			(0xFFFFFFFF)

#if (EN_P2L_IN_DATA == 1)
#define NUM_DATA_PAGE		(NUM_WL - 1)
#else 
#define NUM_DATA_PAGE		(NUM_WL)
#endif


enum Cmd
{
	CMD_WRITE,
	CMD_READ,
	CMD_SHUTDOWN,
	NUM_CMD,
};

struct ReqInfo
{
	Cmd eCmd;
	uint32 nLPN;
	uint16 nBuf;
	uint32 nSeqNo;
};

typedef void (*CbfReq)(ReqInfo* pReq);

uint32 FTL_GetNumLPN(CbfReq pfCbf);
void FTL_Request(ReqInfo* pReq);

void FTL_Main(void* pParam);
