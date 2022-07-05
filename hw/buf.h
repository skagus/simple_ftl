
#pragma once
#include "types.h"

#define INV_BUF		(FF16)

union Spare
{
	struct _Com
	{
		uint32 nDW0;
		uint32 nDW1;
	} Com;
	struct _User
	{
		uint32 nLPN;
		uint32 nDummy;
		///////
		uint32 nCet : 8;
	} User;
	struct _Meta
	{
		uint32 nAge;
		uint32 nSlice;
		////
	} Meta;
};

uint8* BM_GetMain(uint16 nBufId);
Spare* BM_GetSpare(uint16 nBufId);

uint16 BM_Alloc();
void BM_Free(uint16 nBuf);
uint16 BM_CountFree();
void BM_Init();
