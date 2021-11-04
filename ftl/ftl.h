#pragma once
#include "types.h"
#include "config.h"

#define NUM_USER_BLK		(PBLK_PER_DIE - 5)
#define LPN_PER_USER_BLK	(CHUNK_PER_PBLK)

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

uint32 FTL_GetNumLPN();

void FTL_Request(ReqInfo* pReq);

void FTL_InitSim();
