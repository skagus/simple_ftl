#include "types.h"
#include "config.h"
#include "nfc.h"
#include "ftl.h"
#include "buf.h"
#include "io.h"

#if (MAPPING == FTL_HYBRID)

#define NUM_BUF_BLK		(PBLK_PER_DIE - NUM_USER_BLK)
#define PRINTF			// SIM_Print

struct BAddr
{
	uint16 nBufBN;
	uint16 nPage;
};

struct LBlkInfo
{
	uint16 nPBN;
	uint16 nInBuf;	///< sector count in buffer block.
	uint32 bmInBuf;	///< buffer block에 있는 sector개수.
};

struct BufBlkInfo
{
	uint16 nPBN;
	uint16 nValid;	///< 해당 buffer block내에 valid LPN개수.
	uint32 bmValid;	///< Valid sector bitmap.
	uint32 anLPN[CHUNK_PER_PBLK];	///< 해당 buffer block내에 있는  LPN.

	void Add(uint32 nOff, uint32 nLPN)
	{
		assert(nOff < CHUNK_PER_PBLK);
		nValid++;
		bmValid |= BIT(nOff);
		anLPN[nOff] = nLPN;
		assert(nValid <= CHUNK_PER_PBLK);
	}

	void Remove(uint32 nOff)
	{
		assert(nOff < CHUNK_PER_PBLK);
		assert(nValid > 0);
		nValid--;
		bmValid &= ~BIT(nOff);
		anLPN[nOff] = 0xFFFF;
	}
};

LBlkInfo gaLBlk[NUM_USER_BLK];
BufBlkInfo gaBufBlk[NUM_BUF_BLK];
BufBlkInfo* gpCurOpen;
uint16 gnOpenCPO;

uint16 _scanBufBlk(uint32 nLPN, uint16* pnOff)
{
	for (uint32 nBN = 0; nBN < NUM_BUF_BLK; nBN++)
	{
		BufBlkInfo* pBI = gaBufBlk + nBN;
		for (uint32 nPg = 0; nPg < CHUNK_PER_PBLK; nPg++)
		{
			if ((pBI->bmValid & BIT(nPg)) && (pBI->anLPN[nPg] == nLPN))
			{
				*pnOff = nPg;
				return nBN;
			}
		}
	}
	return 0xFFFF; // invalid.
}

uint16 _getVictim()
{
	uint32 nMaxInBuf = 0;
	uint16 nMaxLBN = PBLK_PER_DIE;
	for (uint32 nBN = 0; nBN < NUM_USER_BLK; nBN++)
	{
		if (gaLBlk[nBN].nInBuf > nMaxInBuf)
		{
			nMaxInBuf = gaLBlk[nBN].nInBuf;
			nMaxLBN = nBN;
		}
	}
	return nMaxLBN;
}

/**
migration대상 LBN에 포함된 page중 buffer block에 있는 것을 추려준다.
*/
void _scanMigPage(uint16 nLBN, BAddr* aSrc, uint32* pbmValid)
{
	uint32 nMinLPN = nLBN * LPN_PER_USER_BLK;
	uint32 nMaxLPN = (nLBN + 1) * LPN_PER_USER_BLK;
	uint32 bmValid = 0;
	// Scan all buffer block.
	for (uint32 nBN = 0; nBN < NUM_BUF_BLK; nBN++)
	{
		BufBlkInfo* pBI = gaBufBlk + nBN;
		for (uint32 nPg = 0; nPg < CHUNK_PER_PBLK; nPg++)
		{
			if ((pBI->bmValid & BIT(nPg))
				&& (pBI->anLPN[nPg] >= nMinLPN)
				&& (pBI->anLPN[nPg] < nMaxLPN))
			{
				uint32 nOff = pBI->anLPN[nPg] % CHUNK_PER_PBLK;
				aSrc[nOff].nBufBN = nBN;
				aSrc[nOff].nPage = nPg;
				bmValid |= BIT(nOff);
			}
		}
	}
	*pbmValid = bmValid;
}

/**
Buffer block에 있는 데이터를 user block으로 옮긴다.
victim은 buffer block에 가장 많은 데이터가 있는 user block.
*/
void ftl_migrate2UserBlk(BufBlkInfo* pFree)
{
	uint16 nMaxLBN = _getVictim();
	if (nMaxLBN >= NUM_USER_BLK)
	{
		return;
	}
	LBlkInfo* pDataVictim = gaLBlk + nMaxLBN;
	BAddr aInBuf[LPN_PER_USER_BLK];
	uint32 bmValid = 0;
	memset(aInBuf, 0xFF, sizeof(aInBuf));
	
	_scanMigPage(nMaxLBN, aInBuf, &bmValid);
	assert(bmValid == pDataVictim->bmInBuf);
	PRINTF("Mig: %X, %X\n", nMaxLBN, bmValid);
	uint16 nCopyBuf = BM_Alloc();
	uint32* pnLPN = (uint32*)BM_GetSpare(nCopyBuf);
	IO_Erase(pFree->nPBN);
	for (uint32 nPg = 0; nPg < CHUNK_PER_PBLK; nPg++)
	{
		if (BIT(nPg) & bmValid)
		{
			BufBlkInfo* pBI = gaBufBlk + aInBuf[nPg].nBufBN;
			IO_Read(pBI->nPBN, aInBuf[nPg].nPage, nCopyBuf);
			assert(pBI->nValid > 0);
			assert(*pnLPN == nMaxLBN * CHUNK_PER_PBLK + nPg);
			pBI->Remove(aInBuf[nPg].nPage);
		}
		else
		{
			IO_Read(pDataVictim->nPBN, nPg, nCopyBuf);
		}
		IO_Program(pFree->nPBN, nPg, nCopyBuf);
	}
	BM_Free(nCopyBuf);

	uint16 nTmpBN = pDataVictim->nPBN;
	pDataVictim->nPBN = pFree->nPBN;
	pFree->nPBN = nTmpBN;
	pDataVictim->nInBuf = 0;
	pDataVictim->bmInBuf = 0;
}


uint16 _getVictimBuf()
{
	uint32 nMinValid = CHUNK_PER_PBLK + 1;
	uint32 nVictim;
	uint16 nFree = 0;
	for (uint32 nBN = 0; nBN < NUM_BUF_BLK; nBN++)
	{
		if (0 == gaBufBlk[nBN].nValid)
		{
			nFree++;
		}
		else if (gaBufBlk[nBN].nValid < nMinValid)
		{
			nMinValid = gaBufBlk[nBN].nValid;
			nVictim = nBN;
		}
	}
	if (nFree > 2) // Open & Free.
	{
		return NUM_BUF_BLK;
	}
	else
	{
		return nVictim;
	}
}

// Buffer block중 valid page가 가장 적은 것을 골라서, open block로 옮긴다.
// 이후 free --> open, victim --> free.
void ftl_compactBufBlk()
{
	uint16 nVicBN = _getVictimBuf();
	if (nVicBN >= NUM_BUF_BLK)
	{
		return;
	}
	BufBlkInfo* pSrc = gaBufBlk + nVicBN;
	uint16 nCopyBuf = BM_Alloc();
	uint16 nCPO = 0;
	uint32* pnLPN = (uint32*)BM_GetSpare(nCopyBuf);
	for (uint32 nPg = 0; nPg < CHUNK_PER_PBLK; nPg++)
	{
		if (pSrc->bmValid & BIT(nPg))
		{
			IO_Read(pSrc->nPBN, nPg, nCopyBuf);
			pSrc->Remove(nPg);
			IO_Program(gpCurOpen->nPBN, nCPO, nCopyBuf);
			gpCurOpen->Add(nCPO, *pnLPN);
			nCPO++;
		}
	}
	BM_Free(nCopyBuf);
	PRINTF("Compact %d page\n", nCPO);
	gnOpenCPO = nCPO;
}

BufBlkInfo* _getFree(BufBlkInfo* pExcept, uint32* pnAccValid = nullptr)
{
	BufBlkInfo* pFound = nullptr;
	uint32 nAccValid = 0;
	for (uint32 nBN = 0; nBN < NUM_BUF_BLK; nBN++)
	{
		BufBlkInfo* pBI = gaBufBlk + nBN;
		nAccValid += pBI->nValid;
		if ((0 == pBI->nValid) && (pExcept != pBI))
		{
			pFound = pBI;
		}
	}
	if (nullptr != pnAccValid)
	{
		*pnAccValid = nAccValid;
	}
	return pFound;
}

void FTL_Write(uint32 nLPN, uint16 nNewBuf)
{
	PRINTF("Write:%X\n", nLPN);
	if (gnOpenCPO >= LPN_PER_USER_BLK)
	{
		uint32 nAccValid;
		BufBlkInfo* pFree = _getFree(gpCurOpen, &nAccValid);
		while(nAccValid >= (NUM_BUF_BLK - 1) * LPN_PER_USER_BLK)
		{
			ftl_migrate2UserBlk(pFree);
			assert(0 == pFree->nValid);
			pFree = _getFree(gpCurOpen, &nAccValid);
		}
		gpCurOpen = pFree;
		gnOpenCPO = 0;
		IO_Erase(gpCurOpen->nPBN);
		if (nullptr == _getFree(gpCurOpen, &nAccValid))
		{
			ftl_compactBufBlk();
		}
	}
	uint32 nLBN = nLPN / LPN_PER_USER_BLK;
	uint16 nOff = nLPN % LPN_PER_USER_BLK;

	if (gaLBlk[nLBN].bmInBuf & BIT(nOff)) // 원래  buffer block에 있는 경우.
	{
		uint32 nBufIdx = _scanBufBlk(nLPN, &nOff);
		assert(nBufIdx < NUM_BUF_BLK);
		BufBlkInfo* pBufB = gaBufBlk + nBufIdx;
		pBufB->Remove(nOff);
	}
	else // data block에 있는 경우.
	{
		gaLBlk[nLBN].bmInBuf |= BIT(nOff);
		gaLBlk[nLBN].nInBuf++;
	}
	uint32* pnLPN = (uint32*)BM_GetSpare(nNewBuf);
	*pnLPN = nLPN;
	IO_Program(gpCurOpen->nPBN, gnOpenCPO, nNewBuf);
	gpCurOpen->Add(gnOpenCPO, nLPN);
	gnOpenCPO++;
}


void FTL_Read(uint32 nLPN, uint16 nBufId)
{
	uint16 nLBN = nLPN / LPN_PER_USER_BLK;
	uint16 nOff = nLPN % LPN_PER_USER_BLK;
	LBlkInfo* pLBlk = gaLBlk + nLBN;
	if (pLBlk->bmInBuf & BIT(nOff))
	{
		uint16 nBufIdx = _scanBufBlk(nLPN, &nOff);
		BufBlkInfo* pBI = gaBufBlk + nBufIdx;
		if (pBI->nPBN < PBLK_PER_DIE)
		{
			IO_Read(pBI->nPBN, nOff, nBufId);
		}
	}
	else
	{
		IO_Read(pLBlk->nPBN, nOff, nBufId);
	}
}

void FTL_Init()
{
	BM_Init();
	NFC_Init(io_CbDone);

	for (uint32 nBN = 0; nBN < NUM_USER_BLK; nBN++)
	{
		gaLBlk[nBN].nPBN = nBN;
	}
	for (uint32 nBN = 0; nBN < NUM_BUF_BLK; nBN++)
	{
		gaBufBlk[nBN].nPBN = NUM_USER_BLK + nBN;
	}
	gpCurOpen = gaBufBlk + 0;
}

#endif