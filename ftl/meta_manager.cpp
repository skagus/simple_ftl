
#include "types.h"
#include "config.h"
#include "scheduler.h"
#include "nfc.h"
#include "ftl.h"
#include "buf.h"
#include "io.h"
#include "map_log.h"
#include "meta_manager.h"

#define PRINTF			//	SIM_Print


Meta gstMeta;

struct MetaCtx
{
	uint16 nCurBN;
	uint16 nNextWL;
	uint32 nAge;
};

MetaCtx gstMetaCtx;

enum BootStep
{
	Boot_Init,
	Boot_Open,		///< In openning.
	Boot_Format,	///< In formatting.
	Boot_Done,
};
struct BootCtx
{
	BootStep eStep;
};
static uint8 anContext[4096];		///< Stack like meta context.
BootCtx* gpBootCtx;

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
			Sched_Wait(0, 1);
			break;
		}
		case FMT_LogMap:
		{
			uint16 nBN = pFmtCtx->nBN;
			for (uint16 nIdx = 0; nIdx < NUM_LOG_BLK; nIdx++)
			{
				gstMeta.astLog[nIdx].nLBN = 0xFFFF;
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
	return gpBootCtx->eStep == Boot_Done;
}


void META_SetFreePBN(uint16 nPBN)
{
	gstMeta.nFreePBN = nPBN;
}

uint16 META_GetFreePBN()
{
	return gstMeta.nFreePBN;
}

LogMap* META_GetLogMap(uint16 nIdx)
{
	return gstMeta.astLog + nIdx;
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
			if (gstMeta.astLog[nIdx].nLBN == nLBN)
			{
				return gstMeta.astLog + nIdx;
			}
		}
	}
	return nullptr;
}


void META_Save()
{
	CmdInfo* pCmd;
	////// Save Meta data. ////////
	if (0 == gstMetaCtx.nNextWL)
	{
		pCmd = IO_Alloc(IOCB_Meta);
		IO_Erase(pCmd, gstMetaCtx.nCurBN, 0);
		IO_WaitDone(pCmd);
		IO_Free(pCmd);
	}
	uint16 nBuf = BM_Alloc();
	*(uint32*)BM_GetSpare(nBuf) = gstMetaCtx.nAge;
	uint8* pMain = BM_GetMain(nBuf);
	memcpy(pMain, &gstMeta, sizeof(gstMeta));
	pCmd = IO_Alloc(IOCB_Meta);
	IO_Program(pCmd, gstMetaCtx.nCurBN, gstMetaCtx.nNextWL, nBuf, 0);
	IO_WaitDone(pCmd);
	IO_Free(pCmd);
	BM_Free(nBuf);

	PRINTF("[MT] Save (%X,%X)\n", gstMetaCtx.nCurBN, gstMetaCtx.nNextWL);
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

		if (0xFFFFFFFF != *pnSpare)
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
		if (NOT(pCtx->bErsFound))
		{
			LogMap* pLMap = gstMeta.astLog + pCtx->nLogIdx;
			pLMap->nCPO = NUM_WL;
		}
		pCtx->nLogIdx++;
		if (pCtx->nLogIdx < NUM_LOG_BLK)
		{
			LogMap* pLMap = gstMeta.astLog + pCtx->nLogIdx;
			pCtx->bErsFound = false;
			pCtx->nDone = pLMap->nCPO;
			pCtx->nIssue = pLMap->nCPO;
			Sched_Wait(0, 1);
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
		Sched_Wait(BIT(EVT_NAND_CMD), 100);
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

bool meta_PageScan(MtPageScanCtx* pCtx, bool b1st)
{
	bool bRet = false;
	if (b1st)
	{
		pCtx->nCPO = 0;
		pCtx->nIssued = 0;
		pCtx->nDone = 0;
	}

	if ((0 == pCtx->nCPO)
		&& (pCtx->nIssued < NUM_WL)
		&& ((pCtx->nIssued - pCtx->nDone) < 2))
	{
		uint16 nBuf = BM_Alloc();
		CmdInfo* pCmd = IO_Alloc(IOCB_Meta);
		IO_Read(pCmd, pCtx->nMaxBN, pCtx->nIssued, nBuf, pCtx->nIssued);
		PRINTF("[OPEN] PageScan Issue (%X,%X)\n", pCmd->anBBN[0], pCmd->nWL);
		pCtx->nIssued++;
		Sched_Wait(BIT(EVT_NAND_CMD), 100);
	}
	// Check phase.
	CmdInfo* pDone = IO_GetDone(CbKey::IOCB_Meta);
	if (nullptr != pDone)
	{
		pCtx->nDone++;
		uint16 nBuf = pDone->stRead.anBufId[0];
		uint32* pnSpare = (uint32*)BM_GetSpare(nBuf);
		if (0xFFFFFFFF != *pnSpare)
		{
			assert(0 == pCtx->nCPO);
			uint8* pMain = BM_GetMain(nBuf);
			gstMetaCtx.nAge = *pnSpare;
			memcpy(&gstMeta, pMain, sizeof(gstMeta));
		}
		else if (0 == pCtx->nCPO)
		{
			pCtx->nCPO = pDone->nTag;
		}
		BM_Free(nBuf);
		IO_Free(pDone);
		PRINTF("[OPEN] PageScan Done (%X,%X)\n", pDone->anBBN[0], pDone->nWL);

		if ((pCtx->nDone == pCtx->nIssued)
			&& ((pCtx->nCPO > 0) || (pCtx->nDone >= NUM_WL)))
		{
			bRet = true;
			PRINTF("[OPEN] Clean MtPage (%X,%X))\n", pCtx->nMaxBN, pCtx->nCPO);
			if (pCtx->nCPO < NUM_WL)
			{
				gstMetaCtx.nCurBN = pCtx->nMaxBN;
				gstMetaCtx.nNextWL = pCtx->nCPO;
			}
			else
			{
				gstMetaCtx.nNextWL = 0;
				gstMetaCtx.nCurBN = (pCtx->nMaxBN + 1) % NUM_META_BLK;
			}
		}
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

bool meta_BlkScan(MtBlkScanCtx* pCtx, bool b1st)
{
	bool bRet = false;
	if (b1st)
	{
		pCtx->nMaxBN = 0xFFFF;
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
		Sched_Wait(BIT(EVT_NAND_CMD), 100);
	}
	// Check phase.
	CmdInfo* pDone = IO_GetDone(CbKey::IOCB_Meta);
	if (nullptr != pDone)
	{
		pCtx->nDone++;
		uint16 nBuf = pDone->stRead.anBufId[0];
		uint32* pnSpare = (uint32*)BM_GetSpare(nBuf);

		PRINTF("[OPEN] BlkScan Done %X\n", pDone->nTag);

		if ((*pnSpare > pCtx->nMaxAge) && (*pnSpare != 0xFFFFFFFF))
		{
			pCtx->nMaxAge = *pnSpare;
			pCtx->nMaxBN = pDone->nTag;
		}
		BM_Free(nBuf);
		IO_Free(pDone);

		if (NUM_META_BLK == pCtx->nDone)	// All done.
		{
			bRet = true;
			if (0xFFFF != pCtx->nMaxBN)
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
enum OpenStep
{
	Open_Init,
	Open_BlkScan,
	Open_PageScan,
	Open_DataScan,
};

struct OpenCtx
{
	OpenStep eOpenStep;
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
			pCtx->nMaxBN = 0xFFFF;
			MtBlkScanCtx* pChildCtx = (MtBlkScanCtx*)(pCtx + 1);
			bRet = meta_BlkScan(pChildCtx, true);
			break;
		}
		case Open_BlkScan:
		{
			MtBlkScanCtx* pChildCtx = (MtBlkScanCtx*)(pCtx + 1);
			if (meta_BlkScan(pChildCtx, false))
			{
				pCtx->nMaxBN = pChildCtx->nMaxBN;
				if (0xFFFF != pCtx->nMaxBN)
				{
					pCtx->eOpenStep = Open_PageScan;
					MtPageScanCtx* pNextChild = (MtPageScanCtx*)(pCtx + 1);
					pNextChild->nMaxBN = pCtx->nMaxBN;
					bRet = meta_PageScan(pNextChild, true);
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
			if (meta_PageScan(pChildCtx, false))
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
	switch (gpBootCtx->eStep)
	{
		case Boot_Init:
		{
			OpenCtx* pChildCtx = (OpenCtx*)(gpBootCtx + 1);
			meta_Open((OpenCtx*)(gpBootCtx + 1), true);
			gpBootCtx->eStep = Boot_Open;
			break;
		}

		case Boot_Open:
		{
			OpenCtx* pChildCtx = (OpenCtx*)(gpBootCtx + 1);
			if (meta_Open(pChildCtx, false))
			{
				if (0xFFFF == pChildCtx->nMaxBN)
				{
					FormatCtx* pNextCtx = (FormatCtx*)(gpBootCtx + 1);
					meta_Format(pNextCtx, true);
					gpBootCtx->eStep = Boot_Format;
					Sched_Wait(0, 1);
				}
				else
				{
					gpBootCtx->eStep = Boot_Done;
					Sched_TrigSyncEvt(BIT(EVT_OPEN));
				}
			}
			break;
		}
		case Boot_Format:
		{
			FormatCtx* pChildCtx = (FormatCtx*)(gpBootCtx + 1);
			if(meta_Format(pChildCtx, false))
			{
				gpBootCtx->eStep = Boot_Done;
				Sched_TrigSyncEvt(BIT(EVT_OPEN));
				break;
			}
			break;
		}
		case Boot_Done:
		default:
		{
			assert(false);
		}
	}

	assert(Sched_WillRun() || (Boot_Done == gpBootCtx->eStep));
}


void META_Init()
{
	gpBootCtx = (BootCtx*)anContext;
	MEMSET_OBJ(gstMeta, 0);
	MEMSET_OBJ(gstMetaCtx, 0);
	MEMSET_ARRAY(anContext, 0);
	Sched_Register(TID_META, meta_Run, anContext, BIT(MODE_NORMAL));
}
