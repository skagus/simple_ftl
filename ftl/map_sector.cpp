#include "types.h"
#include "config.h"
#include "nfc.h"
#include "ftl.h"
#include "buf.h"
#include "io.h"

#if (MAPPING == FTL_SECTOR_MAP)
#define PRINTF			// SIM_Print

#define NUM_USER_PAGE		(NUM_USER_BLK * LPN_PER_USER_BLK)

struct Addr
{
	uint16 nPBN;
	uint16 nPage;
};


struct PBlkInfo 
{
	uint16 nValid;
	uint32 bmValid;
	static_assert(NUM_WL <= 32);	// bmValid bit수를 늘려야 함.
};

struct OpenBlk
{
	uint16 nPBN;
	uint16 nCPO;
};

Addr gastL2P[NUM_USER_PAGE];	///< L2P Address table.
PBlkInfo gaBlkInfo[PBLK_PER_DIE];	///< Valid page count.
OpenBlk gstOpen;

void FTL_Init()
{
	BM_Init();
	NFC_Init(io_CbDone);

	memset(gastL2P, 0xFF, sizeof(gastL2P));
	for (uint32 nBN = 0; nBN < PBLK_PER_DIE; nBN++)
	{
		gaBlkInfo[nBN].nValid = 0;
		gaBlkInfo[nBN].bmValid = 0;
	}
	gstOpen.nPBN = 0xFFFF;
	gstOpen.nCPO = 0xFFFF;
}

/**
최소 valid block찾기.
*/
uint16 getMinValid(uint16 nExcept, uint16* pnValid)
{
	uint16 nMinValid = 0xFFFF;
	uint16 nMinBN = 0xFFFF;
	for (uint32 nBN = 0; nBN < PBLK_PER_DIE; nBN++)
	{
		PBlkInfo* pBI = gaBlkInfo + nBN;
		if ((pBI->nValid < nMinValid) && (nBN != nExcept))
		{
			nMinBN = nBN;
			nMinValid = pBI->nValid;
		}
	}
	*pnValid = nMinValid;
	return nMinBN;
}

void mapUpdate(uint32 nLPN, uint16 nPBN, uint16 nPage)
{
	Addr stOld = gastL2P[nLPN];

	gastL2P[nLPN].nPBN = nPBN;
	gastL2P[nLPN].nPage = nPage;
	PBlkInfo* pDst = gaBlkInfo + nPBN;
	pDst->bmValid |= BIT(nPage);
	pDst->nValid++;

	if (stOld.nPBN < PBLK_PER_DIE)
	{
		PBlkInfo* pDst = gaBlkInfo + stOld.nPBN;
		pDst->bmValid &= ~BIT(stOld.nPage);
		pDst->nValid--;
	}
}

void makeNewOpen(OpenBlk* pstOpen)
{
	uint16 nValid;
	uint16 nDstBN = getMinValid(0xFFFF, &nValid);
	assert(nValid == 0);
	uint16 nSrcBN = getMinValid(nDstBN, &nValid);
	IO_Erase(nDstBN);
	if (0 == nValid) // New free exit.
	{
		pstOpen->nPBN = nDstBN;
		pstOpen->nCPO = 0;
		return;
	}
	else
	{
		PBlkInfo* pSrcBI = gaBlkInfo + nSrcBN;
		uint16 nCopyBuf = BM_Alloc();
		uint16 nCPO = 0;
		uint32* pnLPN = (uint32*)BM_GetSpare(nCopyBuf);
		assert(nSrcBN != nDstBN);
		for (uint16 nPage = 0; nPage < CHUNK_PER_PBLK; nPage++)
		{
			if (pSrcBI->bmValid & BIT(nPage))
			{
				IO_Read(nSrcBN, nPage, nCopyBuf);
				assert(*pnLPN < NUM_USER_PAGE);
				assert(gastL2P[*pnLPN].nPBN == nSrcBN);
				assert(gastL2P[*pnLPN].nPage == nPage);
				IO_Program(nDstBN, nCPO, nCopyBuf);
				mapUpdate(*pnLPN, nDstBN, nCPO);
				nCPO++;
			}
		}
		assert(pSrcBI->nValid == 0);
		pstOpen->nPBN = nDstBN;
		pstOpen->nCPO = nCPO;
		BM_Free(nCopyBuf);
	}
}

void FTL_Write(uint32 nLPN, uint16 nNewBuf)
{
	if (gstOpen.nCPO >= CHUNK_PER_PBLK)
	{
		makeNewOpen(&gstOpen);
	}
	*(uint32*)BM_GetSpare(nNewBuf) = nLPN;
	IO_Program(gstOpen.nPBN, gstOpen.nCPO, nNewBuf);
	mapUpdate(nLPN, gstOpen.nPBN, gstOpen.nCPO);
	gstOpen.nCPO++;
}

void FTL_Read(uint32 nLPN, uint16 nBufId)
{
	Addr* pAddr = gastL2P + nLPN;
	if (pAddr->nPBN < PBLK_PER_DIE)
	{
		IO_Read(pAddr->nPBN, pAddr->nPage, nBufId);
	}
	else
	{
		uint32* pBuf = (uint32*)BM_GetMain(nBufId);
		pBuf[0] = 0;
		pBuf[1] = 0;
	}
}

#endif
