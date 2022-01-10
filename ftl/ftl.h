
#pragma once
#include "types.h"
#include "config.h"
#include "cpu.h"

#define NUM_USER_BLK		(PBLK_PER_DIE - 7)
#define LPN_PER_USER_BLK	(CHUNK_PER_PBLK)

#define SIZE_REQ_QUE		(16)
#define INV_BN				(0xFF)
#define INV_LPN				(0xFFFF)
#define INV_PPO				(0xFF)
#define MARK_ERS			(0xFFFFFFFF)

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
	uint32 nSeqNo;
};

typedef void (*CbfReq)(ReqInfo* pReq);

uint32 FTL_GetNumLPN(CbfReq pfCbf);
void FTL_Request(ReqInfo* pReq);

void FTL_Main(void* pParam);
