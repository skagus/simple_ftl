
#include "types.h"
#include "config.h"
#include "scheduler.h"
#include "nfc.h"
#include "ftl.h"
#include "buf.h"
#include "io.h"
#include "log_ftl.h"
#include "log_meta.h"

#define PRINTF			//	SIM_Print

#define PAGE_PER_META	(4)
static_assert(sizeof(Meta) <= BYTE_PER_PPG* PAGE_PER_META);

Meta gstMeta;
bool gbRequest;

struct MetaCtx
{
	uint16 nCurBN;
	uint16 nNextWL;
	uint32 nAge;
};

MetaCtx gstMetaCtx;

enum MtStep
{
	Mt_Init,
	Mt_Open,		///< In openning.
	Mt_Format,	///< In formatting.
	Mt_Ready,
	Mt_Saving,
};
struct MtCtx
{
	MtStep eStep;
};
MtCtx* gpMetaCtx;

enum FormatStep
{
	FMT_BlkMap,
	FMT_LogMap,
	FMT_Done,
};

struct FormatCtx
{
	FormatStep eStep;
	uint16 nBN;
};

bool meta_Format(FormatCtx* pFmtCtx, bool b1st)
{
	bool bRet = false;

	if (b1st)
	{
		pFmtCtx->nBN = 0;
		pFmtCtx->eStep = FMT_BlkMap;
	}
	switch (pFmtCtx->eStep)
	{
		case FMT_BlkMap:
		{
			uint16 nBN = NUM_META_BLK;
			for (uint16 nIdx = 0; nIdx < NUM_USER_BLK; nIdx++)
			{
				gstMeta.astMap[nIdx].nPBN = nBN;
				gstMeta.astMap[nIdx].bLog = 0;
				nBN++;
			}
			pFmtCtx->eStep = FMT_LogMap;
			pFmtCtx->nBN = nBN;
			Sched_Yield();
			break;
		}
		case FMT_LogMap:
		{
			uint16 nBN = pFmtCtx->nBN;
			for (uint16 nIdx = 0; nIdx < NUM_LOG_BLK; nIdx++)
			{
				gstMeta.astLog[nIdx].nLBN = INV_BN;
				gstMeta.astLog[nIdx].nPBN = nBN;
				nBN++;
			}
			gstMeta.nFreePBN = nBN;
			gstMetaCtx.nAge = NUM_WL;	/// Not tobe zero.
			bRet = true;
			break;
		}
		default:
		{
			assert(false);
		}
	}
	return bRet;
}


bool META_Ready()
{
	return gpMetaCtx->eStep == Mt_Ready;
}

BlkMap* META_GetBlkMap(uint16 nLBN)
{
	return gstMeta.astMap + nLBN;
}

LogMap* META_SearchLogMap(uint16 nLBN)
{
	if (gstMeta.astMap[nLBN].bLog)
	{
		for (uint16 nIdx = 0; nIdx < NUM_LOG_BLK; nIdx++)
		{
			LogMap* pLMap = gstMeta.astLog + nIdx;
			if ((pLMap->nLBN == nLBN)
				&& (true == pLMap->bReady))
			{
				return pLMap;
			}
		}
	}
	return nullptr;
}


enum MtSaveStep
{
	MS_Erase,
	MS_Program,
	MS_Done,
};

struct MtSaveCtx
{
	MtSaveStep eStep;
	uint8 nIssue;
	uint8 nDone;
};
bool meta_Save(MtSaveCtx* pCtx, bool b1st)
{
	if (b1st)
	{
		pCtx->nIssue = 0;
		pCtx->nDone = 0;

		if (0 == gstMetaCtx.nNextWL)
		{
			pCtx->eStep = MS_Erase;
		}
		else
		{
			pCtx->eStep = MS_Program;
		}
	}

	bool bRet = false;
	CmdInfo* pDone;
	while (pDone = IO_GetDone(IOCB_Meta))
	{
		pCtx->nDone++;
		if (NC_ERB == pDone->eCmd)
		{
			assert(MS_Erase == pCtx->eStep);
			pCtx->nIssue = 0;
			pCtx->nDone = 0;
			pCtx->eStep = MS_Program;
		}
		else // PGM done.
		{
			if (PAGE_PER_META == pCtx->nDone)
			{
				gstMetaCtx.nAge++;
				gstMetaCtx.nNextWL += PAGE_PER_META;
				if (gstMetaCtx.nNextWL >= NUM_WL)
				{
					gstMetaCtx.nNextWL = 0;
					gstMetaCtx.nCurBN++;
					if (gstMetaCtx.nCurBN >= NUM_META_BLK)
					{
						gstMetaCtx.nCurBN = 0;
					}
				}
				pCtx->eStep = MS_Done;
				bRet = true;
			}
			BM_Free(pDone->stPgm.anBufId[0]);
		}
		IO_Free(pDone);
	}
	
	CmdInfo* pCmd;
	if (MS_Erase == pCtx->eStep)
	{
		if (0 == pCtx->nIssue)
		{
			PRINTF("[MT] ERS %X\n", gstMetaCtx.nCurBN);
			pCmd = IO_Alloc(IOCB_Meta);
			IO_Erase(pCmd, gstMetaCtx.nCurBN, 0);
			pCtx->nIssue++;
		}
	}
	else if (MS_Program == pCtx->eStep)
	{
		if (PAGE_PER_META > pCtx->nIssue)
		{
			uint16 nWL = gstMetaCtx.nNextWL + pCtx->nIssue;
			uint16 nBuf = BM_Alloc();
			*(uint32*)BM_GetSpare(nBuf) = gstMetaCtx.nAge;
			uint8* pMain = BM_GetMain(nBuf);
			uint8* pSrc = (uint8*)(&gstMeta) + (pCtx->nIssue * BYTE_PER_PPG);
			uint16 nRest = 0;
			if (sizeof(gstMeta) >= (int16)(pCtx->nIssue * BYTE_PER_PPG))
			{
				nRest = sizeof(gstMeta) - (int16)(pCtx->nIssue * BYTE_PER_PPG);
				nRest = nRest > BYTE_PER_PPG ? BYTE_PER_PPG : nRest;
			}
			memcpy(pMain, pSrc, nRest);
			PRINTF("[MT] PGM (%X,%X) Age:%X\n", gstMetaCtx.nCurBN, nWL, gstMetaCtx.nAge);
			pCmd = IO_Alloc(IOCB_Meta);
			IO_Program(pCmd, gstMetaCtx.nCurBN, nWL, nBuf, 0);
			pCtx->nIssue++;
		}
	}
	if (pCtx->nIssue > pCtx->nDone)
	{
		Sched_Wait(BIT(EVT_NAND_CMD), LONG_TIME);
	}

	return bRet;
}
// =========================================
struct UserScanCtx
{
	uint8 nLogIdx;
	uint16 nIssue;
	uint16 nDone;
	bool bErsFound;
};

bool open_UserScan(UserScanCtx* pCtx, bool b1st)
{
	bool bRet = false;
	if (b1st)
	{
		LogMap* pLMap = gstMeta.astLog + 0;
		pCtx->nLogIdx = 0;
		pCtx->nIssue = pLMap->nCPO;
		pCtx->nDone = pLMap->nCPO;
		pCtx->bErsFound = false;
	}

	CmdInfo* pDone = IO_GetDone(CbKey::IOCB_Meta);
	if (nullptr != pDone)
	{
		pCtx->nDone++;
		uint16 nBuf = pDone->stRead.anBufId[0];
		uint32* pnSpare = (uint32*)BM_GetSpare(nBuf);
		uint16 nPO = pDone->nTag;
		LogMap* pLMap = gstMeta.astLog + pCtx->nLogIdx;

		if (MARK_ERS != *pnSpare)
		{
			uint32 nLPO = (*pnSpare) % CHUNK_PER_PBLK;
			PRINTF("[Open] MapUpdate: LPN:%X to PHY:(%X, %X)\n", *pnSpare, pLMap->nPBN, nPO);
			pLMap->anMap[nLPO] = nPO;
		}
		else if (false == pCtx->bErsFound)
		{
			pCtx->bErsFound = true;
			pLMap->nCPO = nPO;
		}
		BM_Free(nBuf);
		IO_Free(pDone);
	}

	if((pCtx->nDone == pCtx->nIssue)
		&& (pCtx->bErsFound || (pCtx->nDone >= NUM_WL)))
	{
		LogMap* pLMap = gstMeta.astLog + pCtx->nLogIdx;
		pLMap->bReady = true;
		if (NOT(pCtx->bErsFound))
		{
			pLMap->nCPO = NUM_WL;
		}
		pCtx->nLogIdx++;
		if (pCtx->nLogIdx < NUM_LOG_BLK)
		{
			LogMap* pLMap = gstMeta.astLog + pCtx->nLogIdx;
			pCtx->bErsFound = false;
			pCtx->nDone = pLMap->nCPO;
			pCtx->nIssue = pLMap->nCPO;
			Sched_Yield();
		}
		else
		{
			bRet = true;
		}
	}

	if ((false == pCtx->bErsFound)
		&& (pCtx->nIssue < NUM_WL)
		&& (pCtx->nIssue < pCtx->nDone + 2))
	{
		LogMap* pLMap = gstMeta.astLog + pCtx->nLogIdx;
		uint16 nBuf = BM_Alloc();
		CmdInfo* pCmd = IO_Alloc(IOCB_Meta);
		IO_Read(pCmd, pLMap->nPBN, pCtx->nIssue, nBuf, pCtx->nIssue);
		pCtx->nIssue++;
	}
	if (pCtx->nIssue != pCtx->nDone)
	{
		Sched_Wait(BIT(EVT_NAND_CMD), LONG_TIME);
	}
	return bRet;
}
// =================== Meta Page Scan ========================
struct MtPageScanCtx
{
	uint16 nMaxBN;	// Input
	uint16 nCPO;	// Output.
	uint16 nIssued;	// Internal.
	uint16 nDone;	// Internal.
};

bool open_PageScan(MtPageScanCtx* pCtx, bool b1st)
{
	bool bRet = false;
	if (b1st)
	{
		pCtx->nCPO = NUM_WL;
		pCtx->nIssued = 0;
		pCtx->nDone = 0;
	}

	if ((NUM_WL == pCtx->nCPO)
		&& (pCtx->nIssued < NUM_WL / PAGE_PER_META)
		&& ((pCtx->nIssued - pCtx->nDone) < 2))
	{
		uint16 nWL = pCtx->nIssued * PAGE_PER_META;
		uint16 nBuf = BM_Alloc();
		CmdInfo* pCmd = IO_Alloc(IOCB_Meta);
		IO_Read(pCmd, pCtx->nMaxBN, nWL, nBuf, pCtx->nIssued);
		PRINTF("[OPEN] PageScan Issue (%X,%X)\n", pCmd->anBBN[0], nWL);
		pCtx->nIssued++;
		Sched_Wait(BIT(EVT_NAND_CMD), LONG_TIME);
	}
	// Check phase.
	CmdInfo* pDone = IO_GetDone(CbKey::IOCB_Meta);
	if (nullptr != pDone)
	{
		pCtx->nDone++;
		uint16 nBuf = pDone->stRead.anBufId[0];
		uint32* pnSpare = (uint32*)BM_GetSpare(nBuf);
		if (MARK_ERS != *pnSpare)
		{
			assert(NUM_WL == pCtx->nCPO);
			uint8* pMain = BM_GetMain(nBuf);
			gstMetaCtx.nAge = *pnSpare;
			memcpy(&gstMeta, pMain, sizeof(gstMeta));
		}
		else if (NUM_WL == pCtx->nCPO)
		{
			pCtx->nCPO = pDone->nTag * PAGE_PER_META;
		}
		BM_Free(nBuf);
		IO_Free(pDone);
		PRINTF("[OPEN] PageScan Done (%X,%X)\n", pDone->anBBN[0], pDone->nWL);

		if ((pCtx->nDone == pCtx->nIssued)
			&& ((pCtx->nCPO != NUM_WL) || (pCtx->nDone >= NUM_WL / PAGE_PER_META)))
		{
			bRet = true;
			PRINTF("[OPEN] Clean MtPage (%X,%X))\n", pCtx->nMaxBN, pCtx->nCPO);
		}
	}
	return bRet;
}

bool open_MtLoad(MtPageScanCtx* pCtx, bool b1st)
{
	bool bRet = false;
	if (b1st)
	{
		pCtx->nCPO -= PAGE_PER_META;
		pCtx->nIssued = 0;
		pCtx->nDone = 0;
	}

	// Check phase.
	CmdInfo* pDone = IO_GetDone(CbKey::IOCB_Meta);
	if (nullptr != pDone)
	{
		pCtx->nDone++;
		uint16 nBuf = pDone->stRead.anBufId[0];
		uint32* pnSpare = (uint32*)BM_GetSpare(nBuf);
		uint8* pMain = BM_GetMain(nBuf);
		uint8* pDst = (uint8*)(&gstMeta) + (pDone->nTag * BYTE_PER_PPG);
		uint16 nSize = 0;
		if (sizeof(gstMeta) > (pDone->nTag * BYTE_PER_PPG))
		{
			nSize = sizeof(gstMeta) - (pDone->nTag * BYTE_PER_PPG);
		}
		memcpy(pDst, pMain, nSize);

		if (pCtx->nDone >= PAGE_PER_META)
		{
			bRet = true;
		}
		BM_Free(nBuf);
		IO_Free(pDone);
	}
	if (pCtx->nIssued < PAGE_PER_META)
	{
		uint16 nWL = pCtx->nCPO + pCtx->nIssued;
		uint16 nBuf = BM_Alloc();
		CmdInfo* pCmd = IO_Alloc(IOCB_Meta);
		IO_Read(pCmd, pCtx->nMaxBN, nWL, nBuf, pCtx->nIssued);
		PRINTF("[OPEN] PageScan Issue (%X,%X)\n", pCmd->anBBN[0], nWL);
		pCtx->nIssued++;
		Sched_Wait(BIT(EVT_NAND_CMD), LONG_TIME);
	}

	if (pCtx->nIssued > pCtx->nDone)
	{
		Sched_Wait(BIT(EVT_NAND_CMD), LONG_TIME);
	}
	return bRet;
}


// ========================== Meta Block Scan ====================================
struct MtBlkScanCtx
{
	uint16 nMaxBN;	// for return.
	uint32 nMaxAge;
	uint16 nIssued;
	uint16 nDone;
};

bool open_BlkScan(MtBlkScanCtx* pCtx, bool b1st)
{
	bool bRet = false;
	if (b1st)
	{
		pCtx->nMaxBN = INV_BN;
		pCtx->nIssued = 0;
		pCtx->nDone = 0;
		pCtx->nMaxAge = 0;
	}
	// Issue phase.
	if ((pCtx->nIssued < NUM_META_BLK)
		&& ((pCtx->nIssued - pCtx->nDone) < 2))
	{
		uint16 nBuf = BM_Alloc();
		CmdInfo* pCmd;
		pCmd = IO_Alloc(IOCB_Meta);
		IO_Read(pCmd, pCtx->nIssued, 0, nBuf, pCtx->nIssued);
		PRINTF("[OPEN] BlkScan Issue %X\n", pCtx->nIssued);
		pCtx->nIssued++;
		Sched_Wait(BIT(EVT_NAND_CMD), LONG_TIME);
	}
	else
	{
		Sched_Wait(BIT(EVT_NAND_CMD), LONG_TIME);
	}
	// Check phase.
	CmdInfo* pDone = IO_GetDone(CbKey::IOCB_Meta);
	if (nullptr != pDone)
	{
		pCtx->nDone++;
		uint16 nBuf = pDone->stRead.anBufId[0];
		uint32* pnSpare = (uint32*)BM_GetSpare(nBuf);

		PRINTF("[OPEN] BlkScan Done %X\n", pDone->nTag);

		if ((*pnSpare > pCtx->nMaxAge) && (*pnSpare != MARK_ERS))
		{
			pCtx->nMaxAge = *pnSpare;
			pCtx->nMaxBN = pDone->nTag;
		}
		BM_Free(nBuf);
		IO_Free(pDone);

		if (NUM_META_BLK == pCtx->nDone)	// All done.
		{
			bRet = true;
			if (INV_BN != pCtx->nMaxBN)
			{
				PRINTF("[OPEN] BlkScan Latest BN: %X\n", pCtx->nMaxBN);
			}
			else
			{
				PRINTF("[OPEN] All erased --> Format\n");
			}
		}
	}
	return bRet;
}


// =====================================================
enum MetaStep
{
	Open_Init,
	Open_BlkScan,
	Open_PageScan,
	Open_MtLoad,
	Open_DataScan,
};

struct OpenCtx
{
	MetaStep eOpenStep;
	uint16 nMaxBN;	// for return.
};

bool meta_Open(OpenCtx* pCtx, bool b1st)
{
	bool bRet = false;

	if (b1st)
	{
		pCtx->eOpenStep = Open_Init;
	}

	switch (pCtx->eOpenStep)
	{
		case Open_Init:
		{
			pCtx->eOpenStep = Open_BlkScan;
			pCtx->nMaxBN = INV_BN;
			MtBlkScanCtx* pChildCtx = (MtBlkScanCtx*)(pCtx + 1);
			bRet = open_BlkScan(pChildCtx, true);
			break;
		}
		case Open_BlkScan:
		{
			MtBlkScanCtx* pChildCtx = (MtBlkScanCtx*)(pCtx + 1);
			if (open_BlkScan(pChildCtx, false))
			{
				pCtx->nMaxBN = pChildCtx->nMaxBN;
				if (INV_BN != pCtx->nMaxBN)
				{
					pCtx->eOpenStep = Open_PageScan;
					MtPageScanCtx* pNextChild = (MtPageScanCtx*)(pCtx + 1);
					pNextChild->nMaxBN = pCtx->nMaxBN;
					bRet = open_PageScan(pNextChild, true);
					assert(false == bRet);
				}
				else
				{	// All done in this function.
					bRet = true;
				}
			}
			break;
		}
		case Open_PageScan:
		{
			MtPageScanCtx* pChildCtx = (MtPageScanCtx*)(pCtx + 1);
			if (open_PageScan(pChildCtx, false))
			{
				if (pChildCtx->nCPO != NUM_WL)
				{
					gstMetaCtx.nCurBN = pChildCtx->nMaxBN;
					gstMetaCtx.nNextWL = pChildCtx->nCPO;
				}
				else
				{
					gstMetaCtx.nCurBN = (pChildCtx->nMaxBN + 1) % NUM_META_BLK;
					gstMetaCtx.nNextWL = 0;
				}
				pCtx->eOpenStep = Open_MtLoad;
				open_MtLoad(pChildCtx, true);
			}
			break;
		}
		case Open_MtLoad:
		{
			MtPageScanCtx* pChildCtx = (MtPageScanCtx*)(pCtx + 1);
			if (open_MtLoad(pChildCtx, false))
			{
				pCtx->eOpenStep = Open_DataScan;
				UserScanCtx* pNextChild = (UserScanCtx*)(pCtx + 1);
				bRet = open_UserScan(pNextChild, true);
			}
			break;
		}
		case Open_DataScan:
		{
			bRet = open_UserScan((UserScanCtx*)(pCtx + 1), false);
			break;
		}
	}
	return bRet;
}


void meta_Run(void* pParam)
{
	MtCtx* pCtx = (MtCtx*)pParam;

	switch (pCtx->eStep)
	{
		case Mt_Init:
		{
			OpenCtx* pChildCtx = (OpenCtx*)(pCtx + 1);
			meta_Open((OpenCtx*)(pCtx + 1), true);
			pCtx->eStep = Mt_Open;
			break;
		}

		case Mt_Open:
		{
			OpenCtx* pChildCtx = (OpenCtx*)(pCtx + 1);
			if (meta_Open(pChildCtx, false))
			{
				if (INV_BN == pChildCtx->nMaxBN)
				{
					FormatCtx* pNextCtx = (FormatCtx*)(pCtx + 1);
					meta_Format(pNextCtx, true);
					pCtx->eStep = Mt_Format;
					Sched_Yield();
				}
				else
				{
					pCtx->eStep = Mt_Ready;
					Sched_TrigSyncEvt(BIT(EVT_OPEN));
					Sched_Yield();
				}
			}
			break;
		}
		case Mt_Format:
		{
			FormatCtx* pChildCtx = (FormatCtx*)(pCtx + 1);
			if(meta_Format(pChildCtx, false))
			{
				pCtx->eStep = Mt_Ready;
				Sched_TrigSyncEvt(BIT(EVT_OPEN));
				Sched_Yield();
			}
			break;
		}
		case Mt_Ready:
		{
			if (gbRequest)
			{
				gbRequest = false;
				MtSaveCtx* pChild = (MtSaveCtx*)(pCtx + 1);
				meta_Save(pChild, true);
				pCtx->eStep = Mt_Saving;
			}
			else
			{
				Sched_Wait(BIT(EVT_META), LONG_TIME);
			}
			break;
		}
		case Mt_Saving:
		{
			MtSaveCtx* pChild = (MtSaveCtx*)(pCtx + 1);
			if (meta_Save(pChild, false))
			{
				Sched_TrigSyncEvt(BIT(EVT_META));
				pCtx->eStep = Mt_Ready;
				Sched_Yield();
			}
			break;
		}
		default:
		{
			assert(false);
		}
	}
}

uint32 META_GetAge()
{
	return gstMetaCtx.nAge;
}

void META_ReqSave()
{
	gbRequest = true;
	Sched_TrigSyncEvt(BIT(EVT_META));
}

static uint8 anContext[4096];		///< Stack like meta context.
void META_Init()
{
	gpMetaCtx = (MtCtx*)anContext;
	MEMSET_OBJ(gstMeta, 0);
	MEMSET_OBJ(gstMetaCtx, 0);
	MEMSET_ARRAY(anContext, 0);
	Sched_Register(TID_META, meta_Run, anContext, BIT(MODE_NORMAL));
}
