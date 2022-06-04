
#include "types.h"
#include "config.h"
#include "templ.h"
#include "macro.h"
#include "cpu.h"
#include "scheduler.h"
#include "buf.h"
#include "io.h"
#include "page_gc.h"
#include "page_meta.h"

#define PRINTF			SIM_Print

#define PAGE_PER_META	(1)

Meta gstMeta;
bool gbRequest;
OpenBlk gaOpen[NUM_OPEN];
MetaCtx gstMetaCtx;
JnlSet gstJnlSet;

struct MtSaveCtx
{
	enum MtSaveStep
	{
		MS_Erase,
		MS_Program,
		MS_Done,
	};
	MtSaveStep eStep;
	uint8 nIssue;	///< count of issued NAND operation.
	uint8 nDone;	///< count of done NAND operation.
};



struct MtCtx
{
	enum MtStep
	{
		Mt_Init,
		Mt_Open,		///< In openning.
		Mt_Format,	///< In formatting.
		Mt_Ready,
		Mt_Saving,
	};
	MtStep eStep;
};
MtCtx* gpMetaCtx;

uint16 meta_MtBlk2PBN(uint16 nMetaBN)
{
	return nMetaBN + BASE_META_BLK;
}

enum FormatStep
{
	FMT_Memset,
	FMT_Save,
	FMT_Done,
};
struct FormatCtx
{
	FormatStep eStep;
};
bool meta_Format(FormatCtx* pFmtCtx, bool b1st)
{
	bool bRet = false;

	if (b1st)
	{
		pFmtCtx->eStep = FMT_Memset;
	}
	switch (pFmtCtx->eStep)
	{
		case FMT_Memset:
		{
			for (uint16 nIdx = 0; nIdx < NUM_USER_BLK; nIdx++)
			{
				gstMeta.astBI[nIdx].eState = BS_Closed;
				gstMeta.astBI[nIdx].nVPC = 0;
			}
			pFmtCtx->eStep = FMT_Done;
			bRet = true;
			gstMetaCtx.nNextWL = 0;
			gstMetaCtx.nAge = 1;
			gstMetaCtx.nCurBO = 0;
			gstMetaCtx.nNextSlice = 0;
			break;
		}
		case FMT_Save:
		{
			// TODO: Map save.
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

void dbg_MapIntegrity()
{
	uint16 anVPC[NUM_USER_BLK];
	MEMSET_ARRAY(anVPC, 0x0);
	for (uint32 nLPN = 0; nLPN < NUM_LPN; nLPN++)
	{
		if (gstMeta.astL2P[nLPN].nBN < NUM_USER_BLK)
		{
			anVPC[gstMeta.astL2P[nLPN].nBN]++;
		}
	}
	for (uint16 nBN = 0; nBN < NUM_USER_BLK; nBN++)
	{
		assert(gstMeta.astBI[nBN].nVPC == anVPC[nBN]);
	}
}

bool META_Ready()
{
	return (gpMetaCtx->eStep == MtCtx::Mt_Ready);
}

VAddr META_GetMap(uint32 nLPN)
{
	return gstMeta.astL2P[nLPN];
}

BlkInfo* META_GetFree(uint16* pnBN, bool bFirst)
{
	for (uint16 nIdx = 0; nIdx < NUM_USER_BLK; nIdx++)
	{
		BlkInfo* pBI = gstMeta.astBI + nIdx;
		if ((BS_Closed == pBI->eState)
			&& (0 == pBI->nVPC))
		{
			if (NOT(bFirst))
			{
				bFirst = true;
				continue;
			}
			if (nullptr != pnBN)
			{
				*pnBN = nIdx;
			}
			return pBI;
		}
	}
	return nullptr;
}

void META_SetOpen(OpenType eType, uint16 nBN)
{
	OpenBlk* pOpen = gaOpen + eType;
	pOpen->stNextVA.nBN = nBN;
	pOpen->stNextVA.nWL = 0;
	gstMeta.astBI[nBN].eState = BS_Open;
	gstMeta.astBI[nBN].nEC++;
}

void META_SetBlkState(uint16 nBN, BlkState eState)
{
	gstMeta.astBI[nBN].eState = eState;
}

JnlRet META_AddErbJnl(OpenType eOpen, uint16 nBN)
{
	return gstJnlSet.AddErase(nBN, eOpen);
}

void META_StartJnl(OpenType eOpen, uint16 nBN)
{
	gstJnlSet.Start(eOpen, nBN);
}

JnlRet META_Update(uint32 nLPN, VAddr stNew, OpenType eOpen)
{
	JnlRet eJRet = JR_Done;
	if (nLPN < NUM_LPN)
	{
		VAddr stOld = gstMeta.astL2P[nLPN];
		eJRet = gstJnlSet.AddWrite(nLPN, stNew, eOpen);
		if (JR_Busy != eJRet)
		{
			gstMeta.astL2P[nLPN] = stNew;
			if (FF32 != stOld.nDW)
			{
				gstMeta.astBI[stOld.nBN].nVPC--;
				if ((OPEN_USER == eOpen) && (BS_Victim == gstMeta.astBI[stOld.nBN].eState))
				{
					GC_VictimUpdate(stOld);
				}
			}
			if (FF32 != stNew.nDW)
			{
				gstMeta.astBI[stNew.nBN].nVPC++;
			}
		}
	}
	dbg_MapIntegrity();
	return eJRet;
}

bool META_ReqMapUpdate(UpdateCtx* pCtx)
{
	bool bDone = false;
	switch (pCtx->eState)
	{
		case US_Init:
		{
			JnlRet eJRet = META_Update(pCtx->nLPN, pCtx->stVA, pCtx->eOpen);
			if (JR_Done == eJRet)
			{
				bDone = true;
			}
			else if (JR_Filled == eJRet)
			{
				pCtx->eState = US_WaitMeta;
				pCtx->nMtAge = META_ReqSave();
				Sched_Wait(BIT(EVT_META), LONG_TIME);
			}
			else
			{
				Sched_Wait(BIT(EVT_META), LONG_TIME);
			}
			break;
		}
		case US_WaitMeta:
		{
			if (META_GetAge() > pCtx->nMtAge)
			{
				bDone = true;
			}
			else
			{
				Sched_Wait(BIT(EVT_META), LONG_TIME);
			}
			break;
		}
	}
	return bDone;
}

BlkInfo* META_GetMinVPC(uint16* pnBN)
{
	uint16 nMinVPC = FF16;
	uint16 nMinBlk = FF16;
	for (uint16 nIdx = 0; nIdx < NUM_USER_BLK; nIdx++)
	{
		BlkInfo* pBI = gstMeta.astBI + nIdx;
		if ((BS_Closed == pBI->eState)
			&& (0 != pBI->nVPC)
			&& (nMinVPC > pBI->nVPC))
		{
			nMinBlk = nIdx;
			nMinVPC = pBI->nVPC;
		}
	}
	if (nullptr != pnBN)
	{
		*pnBN = nMinBlk;
	}
	return gstMeta.astBI + nMinBlk;
}

/***************************************************************************
* Meta Open/Save sequence.
***************************************************************************/

OpenBlk* META_GetOpen(OpenType eOpen)
{
	return gaOpen + eOpen;
}



bool meta_Save(MtSaveCtx* pCtx, bool b1st)
{
	if (b1st)
	{
#if 0
		gstJnlSet.anActBlk[OPEN_USER] = gaOpen[OPEN_USER];
		// Backup scan point.
		gstMeta.astOpen[OPEN_USER].nBN = gaOpen[OPEN_USER].nBN;
		gstMeta.astOpen[OPEN_USER].nWL = gaOpen[OPEN_USER].nNextPage;
		gstMeta.astOpen[OPEN_GC].nBN = gaOpen[OPEN_GC].nBN;
		gstMeta.astOpen[OPEN_GC].nWL = gaOpen[OPEN_GC].nNextPage;
#endif
		pCtx->nIssue = 0;
		pCtx->nDone = 0;

		if (0 == gstMetaCtx.nNextWL)
		{
			pCtx->eStep = MtSaveCtx::MS_Erase;
		}
		else
		{
			pCtx->eStep = MtSaveCtx::MS_Program;
		}
	}

	bool bRet = false;
	CmdInfo* pDone;
	while (pDone = IO_PopDone(IOCB_Meta))
	{
		pCtx->nDone++;
		if (NC_ERB == pDone->eCmd)
		{
			assert(MtSaveCtx::MS_Erase == pCtx->eStep);
			pCtx->nIssue = 0;
			pCtx->nDone = 0;
			pCtx->eStep = MtSaveCtx::MS_Program;
		}
		else // PGM done.
		{
			if (PAGE_PER_META == pCtx->nDone)
			{
				gstMetaCtx.nAge++;
				gstMetaCtx.nNextSlice++;
				if (gstMetaCtx.nNextSlice >= NUM_MAP_SLICE)
				{
					gstMetaCtx.nNextSlice = 0;
				}
				gstMetaCtx.nNextWL += PAGE_PER_META;
				if (gstMetaCtx.nNextWL >= NUM_WL)
				{
					gstMetaCtx.nNextWL = 0;
					gstMetaCtx.nCurBO++;
					if (gstMetaCtx.nCurBO >= NUM_META_BLK)
					{
						gstMetaCtx.nCurBO = 0;
					}
				}
				pCtx->eStep = MtSaveCtx::MS_Done;
				bRet = true;
			}
			BM_Free(pDone->stPgm.anBufId[0]);
		}
		IO_Free(pDone);
	}
	
	CmdInfo* pCmd;
	if (MtSaveCtx::MS_Erase == pCtx->eStep)
	{
		if (0 == pCtx->nIssue)
		{
			gstMetaCtx.nCurBN = meta_MtBlk2PBN(gstMetaCtx.nCurBO);
			PRINTF("[MT] ERS BO:%X, BN:%X\n", gstMetaCtx.nCurBO, gstMetaCtx.nCurBN);
			pCmd = IO_Alloc(IOCB_Meta);
			IO_Erase(pCmd, gstMetaCtx.nCurBN, 0);
			pCtx->nIssue++;
		}
	}
	else if (MtSaveCtx::MS_Program == pCtx->eStep)
	{
		if (PAGE_PER_META > pCtx->nIssue)
		{
			uint16 nBuf = BM_Alloc();
			uint32* pSpare = (uint32*)BM_GetSpare(nBuf);
			pSpare[0] = gstMetaCtx.nAge;
			pSpare[1] = MARK_META;
			uint8* pDst = BM_GetMain(nBuf);
			if (0 == pCtx->nIssue)
			{
				memcpy(pDst, &gstJnlSet, sizeof(gstJnlSet));
				pDst += sizeof(gstJnlSet);
				uint8* pSrc = (uint8*)(&gstMeta) + (gstMetaCtx.nNextSlice * SIZE_MAP_PER_SAVE);
				memcpy(pDst, pSrc, SIZE_MAP_PER_SAVE);
			}
			else
			{
				assert(false);
			}

			uint16 nWL = gstMetaCtx.nNextWL + pCtx->nIssue;
			PRINTF("[MT] PGM (%X,%X) Age:%X, Slice: %X\n", gstMetaCtx.nCurBN, nWL, gstMetaCtx.nAge, gstMetaCtx.nNextSlice);
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
#if 0
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
			if (nLPO != nPO)
			{
				pLMap->bInPlace = false;
			}
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
#endif
	return bRet;
}

// =================== Meta Page Scan ========================
struct MtPageScanCtx
{
	uint16 nMaxBO;	// Input
	uint16 nMaxBN;	// == meta_MtBlk2PBN(nMaxBO)
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
	CmdInfo* pDone = IO_PopDone(CbKey::IOCB_Meta);
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
	CmdInfo* pDone = IO_PopDone(CbKey::IOCB_Meta);
	if (nullptr != pDone)
	{
		pCtx->nDone++;
		uint16 nBuf = pDone->stRead.anBufId[0];
		uint32* pnSpare = (uint32*)BM_GetSpare(nBuf);
		uint8* pMain = BM_GetMain(nBuf);
		uint8* pDst = (uint8*)(&gstMeta) + (pDone->nTag * BYTE_PER_PPG);
		uint32 nSize = 0;
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
	uint16 nMaxBO;	// for return.
	uint32 nMaxAge;
	uint16 nIssued;
	uint16 nDone;
};

bool open_BlkScan(MtBlkScanCtx* pCtx, bool b1st)
{
	bool bRet = false;
	if (b1st)
	{
		pCtx->nMaxBO = INV_BN;
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
		uint16 nBN = meta_MtBlk2PBN(pCtx->nIssued);
		pCmd = IO_Alloc(IOCB_Meta);
		IO_Read(pCmd, nBN, 0, nBuf, pCtx->nIssued);
		PRINTF("[OPEN] BlkScan Issue %X\n", pCtx->nIssued);
		pCtx->nIssued++;
		Sched_Wait(BIT(EVT_NAND_CMD), LONG_TIME);
	}
	else
	{
		Sched_Wait(BIT(EVT_NAND_CMD), LONG_TIME);
	}
	// Check phase.
	CmdInfo* pDone = IO_PopDone(CbKey::IOCB_Meta);
	if (nullptr != pDone)
	{
		pCtx->nDone++;
		uint16 nBuf = pDone->stRead.anBufId[0];
		uint32* pnSpare = (uint32*)BM_GetSpare(nBuf);

		PRINTF("[OPEN] BlkScan Done %X\n", pDone->nTag);

		if ((*pnSpare > pCtx->nMaxAge) && (*pnSpare != MARK_ERS))
		{
			pCtx->nMaxAge = *pnSpare;
			pCtx->nMaxBO = pDone->nTag;
		}
		BM_Free(nBuf);
		IO_Free(pDone);

		if (NUM_META_BLK == pCtx->nDone)	// All done.
		{
			bRet = true;
			if (INV_BN != pCtx->nMaxBO)
			{
				PRINTF("[OPEN] BlkScan Latest BN: %X\n", pCtx->nMaxBO);
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
	uint16 nMaxBO;	// for return.
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
			pCtx->nMaxBO = INV_BN;
			MtBlkScanCtx* pChildCtx = (MtBlkScanCtx*)(pCtx + 1);
			bRet = open_BlkScan(pChildCtx, true);
			break;
		}
		case Open_BlkScan:
		{
			MtBlkScanCtx* pChildCtx = (MtBlkScanCtx*)(pCtx + 1);
			if (open_BlkScan(pChildCtx, false))
			{
				pCtx->nMaxBO = pChildCtx->nMaxBO;
				if (INV_BN != pCtx->nMaxBO)
				{
					pCtx->eOpenStep = Open_PageScan;
					MtPageScanCtx* pNextChild = (MtPageScanCtx*)(pCtx + 1);
					pNextChild->nMaxBN = meta_MtBlk2PBN(pCtx->nMaxBO);
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
					gstMetaCtx.nCurBO = pChildCtx->nMaxBO;
					gstMetaCtx.nNextWL = pChildCtx->nCPO;
				}
				else
				{
					gstMetaCtx.nCurBO = (pChildCtx->nMaxBO + 1) % NUM_META_BLK;
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
	case MtCtx::Mt_Init:
		{
			OpenCtx* pChildCtx = (OpenCtx*)(pCtx + 1);
			meta_Open((OpenCtx*)(pCtx + 1), true);
			pCtx->eStep = MtCtx::Mt_Open;
			break;
		}

		case MtCtx::Mt_Open:
		{
			OpenCtx* pChildCtx = (OpenCtx*)(pCtx + 1);
			if (meta_Open(pChildCtx, false))
			{
				if (INV_BN == pChildCtx->nMaxBO)
				{
					FormatCtx* pNextCtx = (FormatCtx*)(pCtx + 1);
					if (meta_Format(pNextCtx, true))
					{
						pCtx->eStep = MtCtx::Mt_Ready;
						Sched_TrigSyncEvt(BIT(EVT_OPEN));
						Sched_Yield();
					}
					else
					{
						pCtx->eStep = MtCtx::Mt_Format;
					}
					Sched_Yield();
				}
				else
				{
					pCtx->eStep = MtCtx::Mt_Ready;
					Sched_TrigSyncEvt(BIT(EVT_OPEN));
					Sched_Yield();
				}
			}
			break;
		}
		case MtCtx::Mt_Format:
		{
			FormatCtx* pChildCtx = (FormatCtx*)(pCtx + 1);
			if(meta_Format(pChildCtx, false))
			{
				pCtx->eStep = MtCtx::Mt_Ready;
				Sched_TrigSyncEvt(BIT(EVT_OPEN));
				Sched_Yield();
			}
			break;
		}
		case MtCtx::Mt_Ready:
		{
			if (gbRequest)
			{
				gbRequest = false;
				MtSaveCtx* pChild = (MtSaveCtx*)(pCtx + 1);
				meta_Save(pChild, true);
				pCtx->eStep = MtCtx::Mt_Saving;
			}
			else
			{
				Sched_Wait(BIT(EVT_META), LONG_TIME);
			}
			break;
		}
		case MtCtx::Mt_Saving:
		{
			MtSaveCtx* pChild = (MtSaveCtx*)(pCtx + 1);
			if (meta_Save(pChild, false))
			{
				META_StartJnl(OPEN_GC, 0);
				Sched_TrigSyncEvt(BIT(EVT_META));
				pCtx->eStep = MtCtx::Mt_Ready;
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

uint32 META_ReqSave()
{
	gbRequest = true;
	Sched_TrigSyncEvt(BIT(EVT_META));
	return gstMetaCtx.nAge;
}

static uint8 anContext[4096];		///< Stack like meta context.
void META_Init()
{
	gaOpen[0].stNextVA.nBN = 0;
	gaOpen[0].stNextVA.nWL = NUM_WL;	// Invalid user block.

	gpMetaCtx = (MtCtx*)anContext;
	MEMSET_ARRAY(gstMeta.astBI, 0);
	MEMSET_ARRAY(gstMeta.astL2P, 0xFF);
	MEMSET_OBJ(gstMetaCtx, 0);
	MEMSET_ARRAY(anContext, 0);
	Sched_Register(TID_META, meta_Run, anContext, BIT(MODE_NORMAL));
//	gLRU.Init();
}
