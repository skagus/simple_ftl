
#pragma once
#include "types.h"
#include "ftl.h"

#define NUM_META_BLK		(2)
#define NUM_LOG_BLK		(PBLK_PER_DIE - NUM_USER_BLK - NUM_META_BLK - 1)
static_assert(NUM_LOG_BLK > 1);


struct LogMap
{
	bool bReady;
	uint8 nLBN;
	uint8 nPBN;
	uint8 nCPO;
	uint8 anMap[CHUNK_PER_PBLK];
};

struct BlkMap
{
	uint8 bLog : 1;
	uint8 nPBN : 7;
};


struct Meta
{
	BlkMap astMap[NUM_USER_BLK];
	LogMap astLog[NUM_LOG_BLK];
	uint8 nFreePBN;
};

extern Meta gstMeta;

