#pragma once
#include "types.h"
#include "ftl.h"

#define NUM_META_BLK		(2)
#define NUM_LOG_BLK		(PBLK_PER_DIE - NUM_USER_BLK - NUM_META_BLK - 1)
static_assert(NUM_LOG_BLK > 1);


struct LogMap
{
	uint16 nLBN;
	uint16 nPBN;
	uint16 nCPO;
	uint32 anMap[CHUNK_PER_PBLK];
};

struct BlkMap
{
	uint16 bLog : 1;
	uint16 nPBN : 15;
};