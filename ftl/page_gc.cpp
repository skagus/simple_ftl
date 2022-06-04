
#include "templ.h"
#include "cpu.h"
#include "scheduler.h"
#include "buf.h"
#include "io.h"
#include "page_gc.h"
#include "page_meta.h"

#define PRINTF			SIM_Print
#define SIZE_FREE_POOL	(3)
#define GC_TRIG_BLK_CNT	(2)


/**
* GC move는 
*/
enum GcState
{
	GS_WaitOpen,
	GS_WaitReq,
	GS_GetDst,	
	GS_ErsDst,
	GS_Move,
	GS_Stop,	// Stop on shutdown.
};

struct GcCtx
{
	GcState eState;
	uint16 nReqLBN;		///< Requested BN.
	uint16 nDstBN;		///< Orignally Free block.
};

GcCtx* gpGcCtx;
bool gbVictimChanged;
VAddr gstChanged;
Queue<uint16, SIZE_FREE_POOL> gstFreePool;

struct GcErsCtx
{
	bool bIssued;
	uint16 nBN;
};



bool gc_Erase(GcErsCtx* pCtx, bool b1st)
{
	bool bRet = false;
	if (b1st)
	{
		pCtx->bIssued = false;
	}
	if (false == pCtx->bIssued)
	{
		CmdInfo* pCmd = IO_Alloc(IOCB_Mig);
		IO_Erase(pCmd, pCtx->nBN, 0);
		pCtx->bIssued = true;
		Sched_Wait(BIT(EVT_NAND_CMD), LONG_TIME);
	}
	else
	{
		CmdInfo* pCmd = IO_GetDone(IOCB_Mig);
		if (nullptr != pCmd)
		{
			PRINTF("[GC] ERB Done %X\n", pCtx->nBN);
			assert(pCmd->anBBN[0] == pCtx->nBN);
			IO_Free(pCmd);
			bRet = true;
		}
		else
		{
			Sched_Wait(BIT(EVT_NAND_CMD), LONG_TIME);
		}
	}
	return bRet;
}

#define MAX_GC_READ		(2)

struct GcMoveCtx
{
	uint16 nDstBN;	// Input.
	uint16 nDstWL;
#if (EN_P2L_IN_DATA == 1)
	uint32 aDstLPN[NUM_WL];
#endif

	uint16 nSrcBN;	// Block map PBN.
	uint16 nSrcWL;
#if (EN_P2L_IN_DATA == 1)
	uint32 aSrcLPN[NUM_WL];
	bool bReadReady;
#endif
	uint8 nReadRun;
	uint8 nPgmRun;
	uint16 nDataRead; // Total data read: to check read end.

	uint8 nRdSlot;
	CmdInfo* apReadRun[MAX_GC_READ];
};

/**
* Next를 알아내는 방법
* 1안:
*	- Source선택 직후, P2L map은 filtering하는 방법.
* 2안:
*	- 매번 Next read를 찾아낼 때마다, Map query하는 방법.
* 
* 어차피 memory를 random read하는 것이기에, 1안의 장점은 없다.
* 2안으로 하는 경우, move중에 update된 최신 map정보를 기반으로 동작 가능하다.
* 
* 추후 2안으로 수정할 것.
*/
uint16 gc_GetNextRead(uint16 nCurBN, uint16 nCurPage, uint32* aLPN)
{
#if (EN_P2L_IN_DATA == 0)
	if (nCurPage < NUM_DATA_PAGE)
	{
		return nCurPage;
	}
#else
	while (nCurPage < NUM_WL)
	{
		uint32 nLPN = aLPN[nCurPage];
		if (nLPN < NUM_LPN)
		{
			VAddr stAddr = META_GetMap(nLPN);
			if ((stAddr.nBN == nCurBN) && (stAddr.nWL == nCurPage))
			{
				return nCurPage;
			}
		}
		nCurPage++;
	}
#endif
	return FF16;
}

void gc_HandlePgm(CmdInfo* pDone, GcMoveCtx* pCtx)
{
	uint16 nBuf = pDone->stPgm.anBufId[0];
	uint32* pSpare = (uint32*)BM_GetSpare(nBuf);
	BM_Free(nBuf);
}

void gc_HandleRead(CmdInfo* pDone, GcMoveCtx* pCtx)
{
	uint16 nBuf = pDone->stRead.anBufId[0];
	uint32* pSpare = (uint32*)BM_GetSpare(nBuf);
	PRINTF("[GCR] {%X, %X}, LPN:%X\n", pDone->anBBN[0], pDone->nWL, *pSpare);
#if (EN_P2L_IN_DATA == 1)
	assert(*pSpare == pCtx->aSrcLPN[pDone->nWL]);
#endif

	uint32 nIdx = GET_INDEX(pDone->nTag);
	assert(pCtx->apReadRun[nIdx] == pDone);
	pCtx->apReadRun[nIdx] = nullptr;

#if (EN_P2L_IN_DATA == 0)
	VAddr stOld = META_GetMap(*pSpare);
	if((pCtx->nSrcBN == stOld.nBN) // Valide.
		&&(pDone->nWL == stOld.nWL))
#else
	bool bUnchanged = (0 == GET_CHECK(pDone->nTag));
	if (NOT(bUnchanged))
	{
		bUnchanged = (pCtx->nSrcBN == META_GetMap(*pSpare).nBN);
	}
	if (bUnchanged)
#endif
	{
		CmdInfo* pNewPgm = IO_Alloc(IOCB_Mig);
		IO_Program(pNewPgm, pCtx->nDstBN, pCtx->nDstWL, nBuf, *pSpare);
		// User Data.
		if ((*pSpare & 0xF) == pDone->nTag)
		{
			uint32* pMain = (uint32*)BM_GetMain(nBuf);
			assert((*pMain & 0xF) == pDone->nTag);
		}
		VAddr stAddr(0, pCtx->nDstBN, pCtx->nDstWL);
		META_Update(*pSpare, stAddr, OPEN_GC);
#if (EN_P2L_IN_DATA == 1)
		pCtx->aDstLPN[pCtx->nDstWL] = *pSpare;
#endif
		PRINTF("[GCW] {%X, %X}, LPN:%X\n", pCtx->nDstBN, pCtx->nDstWL, *pSpare);

		pCtx->nDstWL++;
		pCtx->nPgmRun++;
	}
	else
	{
		pCtx->nDataRead--;
		PRINTF("[GC] Moved LPN:%X\n", *pSpare);
		BM_Free(nBuf);
	}
}

extern void dbg_MapIntegrity();

#if (EN_P2L_IN_DATA == 1)
void gc_HandleReadP2L(CmdInfo* pDone, GcMoveCtx* pCtx)
{
	uint16 nBuf = pDone->stPgm.anBufId[0];
	uint32* pSpare = (uint32*)BM_GetSpare(nBuf);
	assert(*pSpare == P2L_MARK);
	PRINTF("[GC] P2L read from {%X, %X}\n", pDone->anBBN[0], pDone->nWL, *pSpare);
	uint32* pMain = (uint32*)BM_GetMain(nBuf);
	uint32 nSize = sizeof(pCtx->aSrcLPN);
	memcpy(pCtx->aSrcLPN, pMain, nSize);
	dbg_MapIntegrity();
	BM_Free(nBuf);
	pCtx->bReadReady = true;
}
#endif

void gc_SetupNewSrc(GcMoveCtx* pCtx)
{
	META_GetMinVPC(&pCtx->nSrcBN);
	PRINTF("[GC] New Victim: %X\n", pCtx->nSrcBN);
	META_SetBlkState(pCtx->nSrcBN, BS_Victim);
	pCtx->nSrcWL = 0;
#if (EN_P2L_IN_DATA == 1)
	uint16 nBuf4Copy = BM_Alloc();
	CmdInfo* pCmd = IO_Alloc(IOCB_Mig);
	IO_Read(pCmd, pCtx->nSrcBN, NUM_DATA_PAGE, nBuf4Copy, FF32);
	pCtx->nReadRun++;
#endif
}

bool gc_Move(GcMoveCtx* pCtx, bool b1st)
{
	bool bRet = false;
	if (b1st)
	{
		pCtx->nDstWL = 0;
		pCtx->nReadRun = 0;
		pCtx->nPgmRun = 0;
		pCtx->nDataRead = 0;
#if (EN_P2L_IN_DATA == 1)
		pCtx->bReadReady = false;
#endif
		pCtx->nRdSlot = 0;
		MEMSET_ARRAY(pCtx->apReadRun, 0x0);
		PRINTF("[GC] Start Move to %X\n", pCtx->nDstBN);
	}
	if (gbVictimChanged)
	{
		for (uint32 nIdx = 0; nIdx < MAX_GC_READ; nIdx++)
		{
			CmdInfo* pCmd = pCtx->apReadRun[nIdx];
			if ((nullptr != pCmd) && (pCmd->nWL == gstChanged.nWL))
			{
				SET_CHECK(pCmd->nTag);
			}
		}
		gbVictimChanged = false;
	}
	////////////// Process done command. ///////////////
	CmdInfo* pDone;
	while (pDone = IO_GetDone(IOCB_Mig))
	{
		if (NC_PGM == pDone->eCmd)
		{
			gc_HandlePgm(pDone, pCtx);
			pCtx->nPgmRun--;
		}
		else
		{
			pCtx->nReadRun--;
#if (EN_P2L_IN_DATA == 1)
			if (FF32 == pDone->nTag) // Read P2L.
			{
				gc_HandleReadP2L(pDone, pCtx);
			}
			else
#endif
			{
				gc_HandleRead(pDone, pCtx);
			}
		}
		IO_Free(pDone);
	}
	////////// Issue New command. //////////////////
	if (NUM_WL == pCtx->nDstWL)
	{
		if (0 == pCtx->nPgmRun)
		{
			PRINTF("[GC] Dst fill: %X\n", pCtx->nDstBN);
			if (FF16 != pCtx->nSrcBN)
			{
				META_SetBlkState(pCtx->nSrcBN, BS_Closed);
			}
			bRet = true;
		}
	}
#if (EN_P2L_IN_DATA == 1)
	else if (NUM_DATA_PAGE == pCtx->nDstWL)
	{
		uint16 nBuf = BM_Alloc();
		*(uint32*)BM_GetSpare(nBuf) = P2L_MARK;
		uint8* pMain = BM_GetMain(nBuf);
		assert(sizeof(pCtx->aDstLPN) <= BYTE_PER_PPG);
		memcpy(pMain, pCtx->aDstLPN, sizeof(pCtx->aDstLPN));
		CmdInfo* pCmd = IO_Alloc(IOCB_Mig);
		PRINTF("[GC] Pgm P2L\n");
		IO_Program(pCmd, pCtx->nDstBN, pCtx->nDstWL, nBuf, P2L_MARK);
		pCtx->nDstWL++;
		pCtx->nPgmRun++;
	}
#endif
	else if((pCtx->nReadRun < MAX_GC_READ) && (pCtx->nDataRead < NUM_DATA_PAGE))
	{
		if (FF16 == pCtx->nSrcBN)
		{
#if (EN_P2L_IN_DATA == 1)
			if ((false == pCtx->bReadReady) && (0 == pCtx->nReadRun))
#else
			if (0 == pCtx->nReadRun)
#endif
			{
				gc_SetupNewSrc(pCtx);
				Sched_Yield();
			}
		}
#if (EN_P2L_IN_DATA == 1)
		else if(true == pCtx->bReadReady)
#else
		else
#endif
		{
#if (EN_P2L_IN_DATA == 1)
			uint16 nReadWL = gc_GetNextRead(pCtx->nSrcBN, pCtx->nSrcWL, pCtx->aSrcLPN);
#else
			uint16 nReadWL = gc_GetNextRead(pCtx->nSrcBN, pCtx->nSrcWL, nullptr);
#endif
			if (FF16 != nReadWL) // Issue Read.
			{
				uint16 nBuf4Copy = BM_Alloc();
				CmdInfo* pCmd = IO_Alloc(IOCB_Mig);
				PRINTF("[GC] RD:{%X,%X}\n", pCtx->nSrcBN, nReadWL);
				IO_Read(pCmd, pCtx->nSrcBN, nReadWL, nBuf4Copy, pCtx->nRdSlot);
				pCtx->nSrcWL = nReadWL + 1;
				pCtx->nReadRun++;
				pCtx->nDataRead++;
				pCtx->apReadRun[pCtx->nRdSlot] = pCmd;
				pCtx->nRdSlot = (pCtx->nRdSlot + 1) % MAX_GC_READ;
			}
			else if ((0 == pCtx->nReadRun) && (0 == pCtx->nPgmRun))
			{// After all program done related to read.(SPO safe)
				META_SetBlkState(pCtx->nSrcBN, BS_Closed);
				PRINTF("[GC] Close victim: %X\n", pCtx->nSrcBN);
				pCtx->nSrcBN = FF16;
#if (EN_P2L_IN_DATA == 1)
				pCtx->bReadReady = false;
#endif
				Sched_Yield();
				ASSERT(false == bRet);	// return false;
			}
		}
	}

	if ((pCtx->nReadRun > 0)|| (pCtx->nPgmRun > 0))
	{
		Sched_Wait(BIT(EVT_NAND_CMD), LONG_TIME);
	}
	return bRet;
}

uint8 gc_ScanFree()
{
	uint16 nFree;
	while (gstFreePool.Count() < SIZE_FREE_POOL)
	{
		BlkInfo* pstBI = META_GetFree(&nFree, true);
		if (nullptr != pstBI)
		{
			gstFreePool.PushTail(nFree);
			pstBI->eState = BS_InFree;
		}
		else
		{
			break;
		}
	}
	return gstFreePool.Count();
}

void gc_Run(void* pParam)
{
	GcCtx* pCtx = (GcCtx*)pParam;

	switch (pCtx->eState)
	{
		case GS_WaitOpen:
		{
			if (NOT(META_Ready()))
			{
				Sched_Wait(BIT(EVT_BLK_REQ), LONG_TIME);
			}
			else
			{
				pCtx->eState = GS_WaitReq;
				Sched_Yield();
			}
			break;
		}
		case GS_WaitReq:
		{
			uint8 nFree = gstFreePool.Count();
			if(nFree < SIZE_FREE_POOL)
			{
				nFree = gc_ScanFree();
			}
			if (nFree <= GC_TRIG_BLK_CNT)
			{
				pCtx->eState = GS_GetDst;
				Sched_Yield();
			}
			else
			{
				Sched_TrigSyncEvt(BIT(EVT_NEW_BLK));
				Sched_Wait(BIT(EVT_BLK_REQ), LONG_TIME);
			}
			break;
		}
		case GS_GetDst:
		{
			uint16 nFree = gstFreePool.PopHead();
			ASSERT(FF16 != nFree);
			META_SetOpen(OPEN_GC, nFree);
			pCtx->nDstBN = nFree;
			pCtx->eState = GS_ErsDst;
			GcErsCtx* pChild = (GcErsCtx*)(pCtx + 1);
			pChild->nBN = nFree;
			gc_Erase(pChild, true);
			break;
		}
		case GS_ErsDst:
		{
			GcErsCtx* pChild = (GcErsCtx*)(pCtx + 1);
			if (gc_Erase(pChild, false)) // End.
			{
				GcMoveCtx* pMoveCtx = (GcMoveCtx*)(pCtx + 1);
				pMoveCtx->nDstBN = pCtx->nDstBN;
				pMoveCtx->nSrcBN = FF16;
				gc_Move(pMoveCtx, true);
				pCtx->eState = GS_Move;
			}
			break;
		}
		case GS_Move:
		{
			GcMoveCtx* pMoveCtx = (GcMoveCtx*)(pCtx + 1);
			if (gc_Move(pMoveCtx, false))
			{
				META_SetBlkState(pMoveCtx->nDstBN, BS_Closed);
				Sched_TrigSyncEvt(BIT(EVT_NEW_BLK));
				pCtx->eState = GS_WaitReq;
				Sched_Yield();
			}
			break;
		}
		case GS_Stop:
		{
			break;
		}
	}
}

void GC_VictimUpdate(VAddr stOld)
{
	gbVictimChanged = true;
	gstChanged = stOld;
}

uint16 GC_ReqFree(OpenType eType)
{
	if (gstFreePool.Count() <= 2)
	{
		Sched_TrigSyncEvt(BIT(EVT_BLK_REQ));
	}
	if(gstFreePool.Count() > 1)
	{
		return gstFreePool.PopHead();
	}
	return FF16;
}


bool GC_BlkErase(ErsCtx* pCtx, bool b1st)
{
	bool bRet = false;
	if (b1st)
	{
		pCtx->eStep = ES_Init;
	}
	switch (pCtx->eStep)
	{
		case ES_Init:
		{
			PRINTF("[REQ] Alloc Free: %X\n", pCtx->nBN);
			CmdInfo* pCmd = IO_Alloc(IOCB_UErs);
			IO_Erase(pCmd, pCtx->nBN, FF32);
			pCtx->eStep = ES_WaitErb;
			Sched_Wait(BIT(EVT_NAND_CMD), LONG_TIME);
			break;
		}
		case ES_WaitErb:
		{
			CmdInfo* pCmd = IO_GetDone(IOCB_UErs);
			if (nullptr != pCmd)
			{
				IO_Free(pCmd);
				if (JR_Busy != META_AddErbJnl(pCtx->nBN, OpenType::OPEN_USER))
				{
					pCtx->eStep = ES_WaitMtSave;
					pCtx->nMtAge = META_ReqSave();
				}
				else
				{
					pCtx->eStep = ES_WaitJnlAdd;
				}
				Sched_Wait(BIT(EVT_META), LONG_TIME);
			}
			else
			{
				Sched_Wait(BIT(EVT_NAND_CMD), LONG_TIME);
			}
			break;
		}
		case ES_WaitJnlAdd:
		{
			if (JR_Busy != META_AddErbJnl(pCtx->nBN, OpenType::OPEN_USER))
			{
				pCtx->eStep = ES_WaitMtSave;
				pCtx->nMtAge = META_ReqSave();
			}
			Sched_Wait(BIT(EVT_META), LONG_TIME);
			break;
		}
		case ES_WaitMtSave:
		{
			if (META_GetAge() > pCtx->nMtAge)
			{
				pCtx->eStep = ES_Init;
				bRet = true;
			}
			else
			{
				Sched_Wait(BIT(EVT_META), LONG_TIME);
			}
			break;
		}
	}
	return bRet;
}


void GC_Stop()
{
	gpGcCtx->eState = GS_Stop;
}

static uint8 anContext[4096];		///< Stack like meta context.

void GC_Init()
{
	MEMSET_ARRAY(anContext, 0);
	gstFreePool.Init();
	gpGcCtx = (GcCtx*)anContext;
	Sched_Register(TID_GC, gc_Run, anContext, BIT(MODE_NORMAL));
}

