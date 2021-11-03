#include "types.h"
#include "config.h"
#include "nfc.h"
#include "ftl.h"
#include "buf.h"
#include "io.h"

#if (MAPPING == FTL_BLOCK_MAP)
#define PRINTF			// SIM_Print

uint16 ganMap[NUM_USER_BLK];
uint16 gnLogPBN;

void FTL_Init()
{
	NFC_Init(io_CbDone);

	for (uint16 nBN = 0; nBN < NUM_USER_BLK; nBN++)
	{
		ganMap[nBN] = nBN;
	}
	gnLogPBN = NUM_USER_BLK;
}


void FTL_Write(uint32 nLPN, uint16 nNewBuf)
{
	uint16 nLBN = nLPN / LPN_PER_USER_BLK;
	uint16 nNewPage = nLPN % LPN_PER_USER_BLK;
	uint16 nDstBN = gnLogPBN;
	uint16 nSrcBN = ganMap[nLBN];
	uint16 nBuf4Copy = BM_Alloc();
	io_Erase(nDstBN);	// Erase before program.
	for (uint16 nPage = 0; nPage < NUM_WL; nPage++)
	{
		if (nNewPage == nPage)
		{
			uint32* pnVal = (uint32*)BM_GetSpare(nNewBuf);
			*pnVal = nLPN;
			io_Program(nDstBN, nPage, nNewBuf);
		}
		else
		{
			io_Read(nSrcBN, nPage, nBuf4Copy);
			io_Program(nDstBN, nPage, nBuf4Copy);
		}
	}
	BM_Free(nBuf4Copy);
	ganMap[nLBN] = nDstBN;
	gnLogPBN = nSrcBN;
}

void FTL_Read(uint32 nLPN, uint16 nBufId)
{
	uint16 nLBN = nLPN / LPN_PER_USER_BLK;
	uint16 nPBN = ganMap[nLBN];
	uint16 nPage = nLPN % LPN_PER_USER_BLK;
	io_Read(nPBN, nPage, nBufId);
	uint32* pnVal = (uint32*)BM_GetSpare(nBufId);
	PRINTF("Read: %X, %X\n", nLPN, *pnVal);
	if (0 != *pnVal)
	{
		assert(nLPN == *pnVal);
	}
}

#endif
