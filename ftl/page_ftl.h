
#pragma once
#include "types.h"
#include "ftl.h"

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
	BS_InFree,
	NUM_BS,
};

struct OpenBlk
{
	VAddr stNextVA;
};

struct BlkInfo
{
	BlkState eState;
	uint16 nVPC;
	uint16 nEC;
};
