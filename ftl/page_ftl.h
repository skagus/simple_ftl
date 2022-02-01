
#pragma once
#include "types.h"
#include "ftl.h"

#define NUM_META_BLK		(2)
#define NUM_LOG_BLK		(PBLK_PER_DIE - NUM_USER_BLK - NUM_META_BLK - 1)
static_assert(NUM_LOG_BLK > 1);

#define P2L_MARK		(0xFFAAFFAA)


union VAddr
{
	VAddr() {}
	VAddr(uint32 nDie, uint32 nBN, uint32 nWL)
	{
		this->nDW = 0;
		this->nDie = nDie;
		this->nBN = nBN;
		this->nWL = nWL;
	}
	struct
	{
		uint32 nDie : 2;
		uint32 nBN : 10;
		uint32 nWL : 15;
		uint32 nDummy : 5;
	};
	uint32 nDW;
};

enum OpenType
{
	OPEN_USER,
	OPEN_GC,
	NUM_OPEN,
};

enum BlkState
{
	BS_Closed,
	BS_Open,
	BS_Victim,
	NUM_BS,
};

struct OpenBlk
{
	uint16 nBN;		///< Block Number.
	uint32 nNextPage;	// Clean WL.
	uint32 anP2L[NUM_WL];
};

struct BlkInfo
{
	BlkState eState;
	uint16 nVPC;
	uint32 nAge;
};

struct Meta
{
	VAddr astL2P[NUM_LPN];
	BlkInfo astBI[NUM_USER_BLK];
};

extern Meta gstMeta;

