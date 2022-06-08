
#include "templ.h"
#include "cpu.h"
#include "scheduler.h"
#include "buf.h"
#include "io.h"
#include "page_gc.h"
#include "page_meta.h"

#define PRINTF			SIM_Print


/**
* GC move는 
*/


struct GcStk
{
	enum GcState
	{
		WaitOpen,
		WaitReq,
		GetDst,	
		ErsDst,
		Move,
		Stop,	// Stop on shutdown.
	};
	GcState eState;
};

GcStk* gpGcStk;
bool gbVictimChanged;
VAddr gstChanged;
Queue<uint16, SIZE_FREE_POOL> gstFreePool;


#define MAX_GC_READ		(2)

struct MoveStk
{
	enum MoveState
	{
		MS_Init,
		MS_Run,
	};
	MoveState eState;

	uint16 nDstBN;	// Input.
	uint16 nDstWL;

	uint16 nSrcBN;	// Block map PBN.
	uint16 nSrcWL;

	uint8 nReadRun;
	uint8 nPgmRun;
	uint16 nDataRead; // Total data read: to check read end.

	uint8 nRdSlot;
	CmdInfo* apReadRun[MAX_GC_READ];
};


uint8 gc_ScanFree();

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
	if (nCurPage < NUM_DATA_PAGE)
	{
		return nCurPage;
	}
	return FF16;
}

void gc_HandlePgm(CmdInfo* pDone, MoveStk* pCtx)
{
	uint16 nBuf = pDone->stPgm.anBufId[0];
	uint32* pSpare = (uint32*)BM_GetSpare(nBuf);
	BM_Free(nBuf);
}

bool gc_HandleRead(CmdInfo* pDone, MoveStk* pCtx)
{
	bool bDone = true;
	uint16 nBuf = pDone->stRead.anBufId[0];
	uint32* pSpare = (uint32*)BM_GetSpare(nBuf);
	PRINTF("[GCR] {%X, %X}, LPN:%X\n", pDone->anBBN[0], pDone->nWL, *pSpare);

	if ((*pSpare != MARK_ERS) &&(pCtx->nDstWL < NUM_WL))
	{
		VAddr stOld = META_GetMap(*pSpare);
		if ((pCtx->nSrcBN == stOld.nBN) // Valid
			&& (pDone->nWL == stOld.nWL))
		{
			VAddr stAddr(0, pCtx->nDstBN, pCtx->nDstWL);
			JnlRet eJRet = META_Update(*pSpare, stAddr, OPEN_GC);
			if (JnlRet::JR_Busy != eJRet)
			{
				CmdInfo* pNewPgm = IO_Alloc(IOCB_Mig);
				IO_Program(pNewPgm, pCtx->nDstBN, pCtx->nDstWL, nBuf, *pSpare);
				// User Data.
				if ((*pSpare & 0xF) == pDone->nTag)
				{
					uint32* pMain = (uint32*)BM_GetMain(nBuf);
					assert((*pMain & 0xF) == pDone->nTag);
				}
				PRINTF("[GCW] {%X, %X}, LPN:%X\n", pCtx->nDstBN, pCtx->nDstWL, *pSpare);
				pCtx->nDstWL++;
				pCtx->nPgmRun++;
				if (JR_Filled == eJRet)
				{
					META_ReqSave();
				}
			}
			else
			{
				bDone = false;
			}
		}
		else
		{
			pCtx->nDataRead--;
			PRINTF("[GC] Moved LPN:%X\n", *pSpare);
			BM_Free(nBuf);
		}
	}
	else
	{
		pCtx->nDataRead--;
		BM_Free(nBuf);
	}
	uint32 nIdx = GET_INDEX(pDone->nTag);
	assert(pCtx->apReadRun[nIdx] == pDone);
	if (bDone)
	{
		pCtx->apReadRun[nIdx] = nullptr;
	}
	return bDone;
}

extern void dbg_MapIntegrity();

void gc_SetupNewSrc(MoveStk* pCtx)
{
	META_GetMinVPC(&pCtx->nSrcBN);
	PRINTF("[GC] New Victim: %X\n", pCtx->nSrcBN);
	META_SetBlkState(pCtx->nSrcBN, BS_Victim);
	pCtx->nSrcWL = 0;
}

bool gc_Move_SM(MoveStk* pStk)
{
	bool bRet = false;
	if (MoveStk::MS_Init == pStk->eState)
	{
		pStk->nReadRun = 0;
		pStk->nPgmRun = 0;
		pStk->nDataRead = pStk->nDstWL;
		pStk->nRdSlot = 0;
		pStk->eState = MoveStk::MS_Run;
		MEMSET_ARRAY(pStk->apReadRun, 0x0);
		PRINTF("[GC:%X] Start Move to %X, %X\n", SIM_GetSeqNo(), pStk->nDstBN, pStk->nDstWL);
	}
	if (gbVictimChanged)
	{
		for (uint32 nIdx = 0; nIdx < MAX_GC_READ; nIdx++)
		{
			CmdInfo* pCmd = pStk->apReadRun[nIdx];
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
		bool bDone = true;
		if (NC_PGM == pDone->eCmd)
		{
			gc_HandlePgm(pDone, pStk);
			pStk->nPgmRun--;
		}
		else
		{
			bDone = gc_HandleRead(pDone, pStk);
			if (bDone)
			{
				pStk->nReadRun--;
			}
		}
		if (bDone)
		{
			IO_PopDone(IOCB_Mig);
			IO_Free(pDone);
		}
		else
		{
			break;
		}
	}
	////////// Issue New command. //////////////////
	if (NUM_WL == pStk->nDstWL)
	{
		if (0 == pStk->nPgmRun)
		{
			PRINTF("[GC] Dst fill: %X\n", pStk->nDstBN);
			if (FF16 != pStk->nSrcBN)
			{
				META_SetBlkState(pStk->nSrcBN, BS_Closed);
			}
			bRet = true;
		}
	}
	else if((pStk->nReadRun < MAX_GC_READ) && (pStk->nDataRead < NUM_DATA_PAGE))
	{
		if (FF16 == pStk->nSrcBN)
		{
			if (0 == pStk->nReadRun)
			{
				gc_SetupNewSrc(pStk);
				Sched_Yield();
			}
		}
		else
		{
			uint16 nReadWL = gc_GetNextRead(pStk->nSrcBN, pStk->nSrcWL, nullptr);
			if (FF16 != nReadWL) // Issue Read.
			{
				uint16 nBuf4Copy = BM_Alloc();
				CmdInfo* pCmd = IO_Alloc(IOCB_Mig);
				PRINTF("[GC] RD:{%X,%X}\n", pStk->nSrcBN, nReadWL);
				IO_Read(pCmd, pStk->nSrcBN, nReadWL, nBuf4Copy, pStk->nRdSlot);
				pStk->nSrcWL = nReadWL + 1;
				pStk->nReadRun++;
				pStk->nDataRead++;
				pStk->apReadRun[pStk->nRdSlot] = pCmd;
				pStk->nRdSlot = (pStk->nRdSlot + 1) % MAX_GC_READ;
			}
			else if ((0 == pStk->nReadRun) && (0 == pStk->nPgmRun))
			{// After all program done related to read.(SPO safe)
				META_SetBlkState(pStk->nSrcBN, BS_Closed);
				PRINTF("[GC] Close victim: %X\n", pStk->nSrcBN);
				if (gc_ScanFree() > 0)
				{
					Sched_TrigSyncEvt(BIT(EVT_NEW_BLK));
				}
				pStk->nSrcBN = FF16;
				Sched_Yield();
				ASSERT(false == bRet);	// return false;
			}
		}
	}

	if ((pStk->nReadRun > 0)|| (pStk->nPgmRun > 0))
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
	GcStk* pGcStk = (GcStk*)pParam;

	switch (pGcStk->eState)
	{
		case GcStk::WaitOpen:
		{
			if (META_Ready())
			{
				OpenBlk* pOpen = META_GetOpen(OPEN_GC);
				// Prev open block case.
				if (pOpen->stNextVA.nWL < NUM_WL)
				{
					gc_ScanFree();
					MoveStk* pMoveStk = (MoveStk*)(pGcStk + 1);
					pMoveStk->eState = MoveStk::MS_Init;
					pMoveStk->nDstBN = pOpen->stNextVA.nBN;
					pMoveStk->nSrcBN = FF16;
					pMoveStk->nDstWL = pOpen->stNextVA.nWL;
					gc_Move_SM(pMoveStk);
					pGcStk->eState = GcStk::Move;
					break;
				}
				else
				{
					pGcStk->eState = GcStk::WaitReq;
					Sched_Yield();
				}
			}
			else
			{
				Sched_Wait(BIT(EVT_OPEN), LONG_TIME);
			}
			break;
		}
		case GcStk::WaitReq:
		{
			uint8 nFree = gstFreePool.Count();
			if(nFree < SIZE_FREE_POOL)
			{
				nFree = gc_ScanFree();
				if (nFree > 0)
				{
					Sched_TrigSyncEvt(BIT(EVT_NEW_BLK));
				}
			}
			if (nFree <= GC_TRIG_BLK_CNT)
			{
				pGcStk->eState = GcStk::GetDst;
				Sched_Yield();
			}
			else
			{
				Sched_TrigSyncEvt(BIT(EVT_NEW_BLK));
				Sched_Wait(BIT(EVT_BLK_REQ), LONG_TIME);
			}
			break;
		}
		case GcStk::GetDst:
		{
			uint16 nFree = gstFreePool.PopHead();
			ASSERT(FF16 != nFree);
			ErbStk* pErbStk = (ErbStk*)(pGcStk + 1);
			pErbStk->nBN = nFree;
			pErbStk->eOpen = OPEN_GC;
			pErbStk->eStep = ErbStk::Init;
			GC_BlkErase_SM(pErbStk);
			pGcStk->eState = GcStk::ErsDst;
			break;
		}
		case GcStk::ErsDst:
		{
			ErbStk* pChild = (ErbStk*)(pGcStk + 1);
			if (GC_BlkErase_SM(pChild)) // End.
			{
				META_SetOpen(OPEN_GC, pChild->nBN);
				MoveStk* pMoveStk = (MoveStk*)(pGcStk + 1);
				pMoveStk->eState = MoveStk::MS_Init;
				pMoveStk->nDstBN = pChild->nBN;
				pMoveStk->nSrcBN = FF16;
				pMoveStk->nDstWL = 0;
				gc_Move_SM(pMoveStk);
				pGcStk->eState = GcStk::Move;
			}
			break;
		}
		case GcStk::Move:
		{
			MoveStk* pMoveStk = (MoveStk*)(pGcStk + 1);
			if (gc_Move_SM(pMoveStk))
			{
				META_SetBlkState(pMoveStk->nDstBN, BS_Closed);
				Sched_TrigSyncEvt(BIT(EVT_NEW_BLK));
				pGcStk->eState = GcStk::WaitReq;
				Sched_Yield();
			}
			break;
		}
		case GcStk::Stop:
		{
			while (true)
			{
				CmdInfo* pDone = IO_PopDone(IOCB_Mig);
				if (nullptr == pDone)
				{
					break;
				}
				if(NC_PGM == pDone->eCmd)
				{
					BM_Free(pDone->stPgm.anBufId[0]);
				}
				else if (NC_READ == pDone->eCmd)
				{
					BM_Free(pDone->stRead.anBufId[0]);
				}
				IO_Free(pDone);
			}
			Sched_TrigSyncEvt(BIT(EVT_NEW_BLK));
			Sched_Wait(BIT(EVT_NAND_CMD), LONG_TIME);

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
	uint16 nBN = FF16;
	if (gstFreePool.Count() <= GC_TRIG_BLK_CNT)
	{
		Sched_TrigSyncEvt(BIT(EVT_BLK_REQ));
	}
	if(gstFreePool.Count() > 1)
	{
		nBN = gstFreePool.PopHead();
	}
	PRINTF("[GC] Alloc %X (free: %d)\n", nBN, gstFreePool.Count());
	return nBN;
}


bool GC_BlkErase_SM(ErbStk* pErbStk)
{
	bool bRet = false;

	CbKey eCbKey = pErbStk->eOpen == OPEN_GC ? CbKey::IOCB_Mig : CbKey::IOCB_UErs;
	switch (pErbStk->eStep)
	{
		case ErbStk::Init:
		{
			PRINTF("[GC] ERB: %X by %s\n", pErbStk->nBN, pErbStk->eOpen == OPEN_GC ? "GC" : "User");
			CmdInfo* pCmd = IO_Alloc(eCbKey);
			IO_Erase(pCmd, pErbStk->nBN, FF32);
			pErbStk->eStep = ErbStk::WaitErb;
			Sched_Wait(BIT(EVT_NAND_CMD), LONG_TIME);
			break;
		}
		case ErbStk::WaitErb:
		{
			CmdInfo* pCmd = IO_PopDone(eCbKey);
			if (nullptr != pCmd)
			{
				IO_Free(pCmd);
				if (JR_Busy != META_AddErbJnl(pErbStk->eOpen, pErbStk->nBN))
				{
					pErbStk->nMtAge = META_ReqSave();
					pErbStk->eStep = ErbStk::WaitMtSave;
				}
				else
				{
					pErbStk->eStep = ErbStk::WaitJnlAdd;
				}
				Sched_Wait(BIT(EVT_META), LONG_TIME);
			}
			else
			{
				Sched_Wait(BIT(EVT_NAND_CMD), LONG_TIME);
			}
			break;
		}
		case ErbStk::WaitJnlAdd:
		{
			if (JR_Busy != META_AddErbJnl(pErbStk->eOpen, pErbStk->nBN))
			{
				pErbStk->nMtAge = META_ReqSave();
				pErbStk->eStep = ErbStk::WaitMtSave;
			}
			Sched_Wait(BIT(EVT_META), LONG_TIME);
			break;
		}
		case ErbStk::WaitMtSave:
		{
			if (META_GetAge() > pErbStk->nMtAge)
			{
				pErbStk->eStep = ErbStk::Init;
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
	gpGcStk->eState = GcStk::Stop;
}

static uint8 aGcStack[4096];		///< Stack like meta context.

void GC_Init()
{
	gpGcStk = (GcStk*)aGcStack;
	MEMSET_ARRAY(aGcStack, 0);
	gstFreePool.Init();
	gpGcStk->eState = GcStk::WaitOpen;
	Sched_Register(TID_GC, gc_Run, aGcStack, BIT(MODE_NORMAL));
}

