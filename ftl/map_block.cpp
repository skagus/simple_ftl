#include "types.h"
#include "config.h"
#include "nfc.h"
#include "ftl.h"
#include "buf.h"
#include "io.h"

#if (MAPPING == FTL_BLOCK_MAP)
#define PRINTF				// SIM_Print

#define NUM_META_BLK		(3)

const uint16 gaMetaBlk[NUM_META_BLK] = { 0,1,2, };
struct MetaCtx
{
	uint16 nCurBN;
	uint16 nNextWL;
	uint32 nAge;
};

struct Meta
{
	uint16 ganMap[NUM_USER_BLK];
	uint16 gnLogPBN;
};
static Meta gstMeta;
static MetaCtx gstMetaCtx;

void ftl_MetaSave()
{
	////// Save Meta data. ////////
	if (0 == gstMetaCtx.nNextWL)
	{
		IO_Erase(gstMetaCtx.nCurBN);
	}
	uint16 nBuf = BM_Alloc();
	*(uint32*)BM_GetSpare(nBuf) = gstMetaCtx.nAge;
	uint8* pMain = BM_GetMain(nBuf);
	memcpy(pMain, &gstMeta, sizeof(gstMeta));
	IO_Program(gstMetaCtx.nCurBN, gstMetaCtx.nNextWL, nBuf);
	BM_Free(nBuf);

	/////// Setup Next Address ///////////
	gstMetaCtx.nAge++;
	gstMetaCtx.nNextWL++;
	if (gstMetaCtx.nNextWL >= NUM_WL)
	{
		gstMetaCtx.nNextWL = 0;
		gstMetaCtx.nCurBN++;
		if (gstMetaCtx.nCurBN >= NUM_META_BLK)
		{
			gstMetaCtx.nCurBN = 0;
		}
	}
}

void ftl_Format()
{
	uint16 nBN = NUM_META_BLK;
	for (uint16 nIdx = 0; nIdx < NUM_USER_BLK; nIdx++)
	{
		gstMeta.ganMap[nIdx] = nBN;
		nBN++;
	}
	gstMeta.gnLogPBN = nBN;

	gstMetaCtx.nCurBN = gaMetaBlk[0];
	gstMetaCtx.nNextWL = 0;
}

bool ftl_Open()
{
	uint16 nBuf = BM_Alloc();
	uint32* pnSpare = (uint32*)BM_GetSpare(nBuf);
	uint8* pMain = BM_GetMain(nBuf);
	// Find Latest Blk.
	uint32 nMinAge = 0xFFFFFFFF;
	uint16 nMinBN = 0xFFFF;
	for (uint16 nBN = 0; nBN < NUM_META_BLK; nBN++)
	{
		IO_Read(nBN, 0, nBuf);
		if (*pnSpare < nMinAge)
		{
			nMinAge = *pnSpare;
			nMinBN = nBN;
		}
	}
	if (0xFFFF != nMinBN)
	{	// Find Latest WL
		uint16 nCPO;
		for (nCPO = 0; nCPO < NUM_WL; nCPO++)
		{
			IO_Read(nMinBN, nCPO, nBuf);
			if (0xFFFFFFFF != *pnSpare)
			{
				gstMetaCtx.nAge = *pnSpare;
				memcpy(&gstMeta, pMain, sizeof(gstMeta));
			}
			else
			{
				break;
			}
		}
		gstMetaCtx.nAge++;
		if (nCPO < NUM_WL)
		{
			gstMetaCtx.nCurBN = nMinBN;
			gstMetaCtx.nNextWL = nCPO;
		}
		else
		{
			gstMetaCtx.nNextWL = 0;
			gstMetaCtx.nCurBN = (nMinBN + 1) % NUM_META_BLK;
		}
		BM_Free(nBuf);
		return true;
	}
	BM_Free(nBuf);
	return false;
}

void FTL_Init()
{
	BM_Init();
	NFC_Init(io_CbDone);
	MEMSET_OBJ(gstMeta, 0);
	MEMSET_OBJ(gstMetaCtx, 0);

	if (false == ftl_Open())
	{
		ftl_Format();
	}
}

void FTL_Write(uint32 nLPN, uint16 nNewBuf)
{
	uint16 nLBN = nLPN / LPN_PER_USER_BLK;
	uint16 nNewPage = nLPN % LPN_PER_USER_BLK;
	uint16 nDstBN = gstMeta.gnLogPBN;
	uint16 nSrcBN = gstMeta.ganMap[nLBN];
	uint16 nBuf4Copy = BM_Alloc();
	IO_Erase(nDstBN);	// Erase before program.
	for (uint16 nPage = 0; nPage < NUM_WL; nPage++)
	{
		if (nNewPage == nPage)
		{
			uint32* pnVal = (uint32*)BM_GetSpare(nNewBuf);
			*pnVal = nLPN;
			IO_Program(nDstBN, nPage, nNewBuf);
		}
		else
		{
			IO_Read(nSrcBN, nPage, nBuf4Copy);
			IO_Program(nDstBN, nPage, nBuf4Copy);
		}
	}
	BM_Free(nBuf4Copy);
	gstMeta.ganMap[nLBN] = nDstBN;
	gstMeta.gnLogPBN = nSrcBN;
	ftl_MetaSave();
}

void FTL_Read(uint32 nLPN, uint16 nBufId)
{
	uint16 nLBN = nLPN / LPN_PER_USER_BLK;
	uint16 nPBN = gstMeta.ganMap[nLBN];
	uint16 nPage = nLPN % LPN_PER_USER_BLK;
	IO_Read(nPBN, nPage, nBufId);
	uint32* pnVal = (uint32*)BM_GetSpare(nBufId);
	PRINTF("Read: %X, %X\n", nLPN, *pnVal);
	if (0 != *pnVal)
	{
		assert(nLPN == *pnVal);
	}
}

#endif
