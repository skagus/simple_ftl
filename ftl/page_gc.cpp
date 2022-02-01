
#include "cpu.h"
#include "scheduler.h"
#include "buf.h"
#include "io.h"
#include "page_gc.h"
#include "page_meta.h"

#define PRINTF			SIM_Print
uint16 gbmGcReq;
bool gbVictimChanged;
VAddr gstChanged;
/**
* GC move는 
*/
enum GcState
{
	GS_WaitReq,
	GS_GetDst,	
	GS_ErsDst,
	GS_Move,
};

struct GcCtx
{
	GcState eState;
	uint16 nReqLBN;		///< Requested BN.
	uint16 nDstBN;		///< Orignally Free block.
};


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
	uint32 aDstLPN[NUM_WL];

	uint16 nSrcBN;	// Block map PBN.
	uint16 nSrcWL;
	uint32 aSrcLPN[NUM_WL];

	bool bReadReady;
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
	assert(*pSpare == pCtx->aSrcLPN[pDone->nWL]);

	uint32 nIdx = GET_INDEX(pDone->nTag);
	assert(pCtx->apReadRun[nIdx] == pDone);
	pCtx->apReadRun[nIdx] = nullptr;

	bool bUnchanged = (0 == GET_CHECK(pDone->nTag));
	if (NOT(bUnchanged))
	{
		bUnchanged = (pCtx->nSrcBN == META_GetMap(*pSpare).nBN);
	}
	if (bUnchanged)
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
		pCtx->aDstLPN[pCtx->nDstWL] = *pSpare;
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

void gc_SetupNewSrc(GcMoveCtx* pCtx)
{
	uint16 nBuf4Copy = BM_Alloc();
	META_GetMinVPC(&pCtx->nSrcBN);
	PRINTF("[GC] New Victim: %X\n", pCtx->nSrcBN);
	META_SetBlkState(pCtx->nSrcBN, BS_Victim);
	pCtx->nSrcWL = 0;
	CmdInfo* pCmd = IO_Alloc(IOCB_Mig);
	IO_Read(pCmd, pCtx->nSrcBN, NUM_WL - 1, nBuf4Copy, FF32);
	pCtx->nReadRun++;
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
		pCtx->bReadReady = false;
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
			if (FF32 == pDone->nTag) // Read P2L.
			{
				gc_HandleReadP2L(pDone, pCtx);
			}
			else
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
	else if ((NUM_WL - 1) == pCtx->nDstWL)
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
	else if((pCtx->nReadRun < MAX_GC_READ) && (pCtx->nDataRead < (NUM_WL - 1)))
	{
		if (FF16 == pCtx->nSrcBN) // Load P2L map
		{
			if ((false == pCtx->bReadReady) && (0 == pCtx->nReadRun))
			{
				gc_SetupNewSrc(pCtx);
			}
		}
		else if(true == pCtx->bReadReady)
		{
			uint16 nReadWL = gc_GetNextRead(pCtx->nSrcBN, pCtx->nSrcWL, pCtx->aSrcLPN);
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
				pCtx->bReadReady = false;
				Sched_Yield();
				return false;
			}
		}
	}

	if ((pCtx->nReadRun > 0)|| (pCtx->nPgmRun > 0))
	{
		Sched_Wait(BIT(EVT_NAND_CMD), LONG_TIME);
	}
	return bRet;
}


void gc_Run(void* pParam)
{
	GcCtx* pCtx = (GcCtx*)pParam;

	switch (pCtx->eState)
	{
		case GS_WaitReq:
		{
			if (0 != gbmGcReq)
			{
				pCtx->eState = GS_GetDst;
				Sched_Yield();
			}
			else
			{
				Sched_Wait(BIT(EVT_BLK_REQ), LONG_TIME);
			}
			break;
		}
		case GS_GetDst:
		{
			uint16 nFree;
			if (nullptr != META_GetFree(&nFree, true))
			{
				META_SetOpen(OPEN_GC, nFree);
				pCtx->nDstBN = nFree;
				pCtx->eState = GS_ErsDst;
				GcErsCtx* pChild = (GcErsCtx*)(pCtx + 1);
				pChild->nBN = nFree;
				gc_Erase(pChild, true);
			}
			else
			{
				Sched_Yield();	// 여기서 기다린다고 뭐가 되나??
			}
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
				gbmGcReq = 0;
				Sched_TrigSyncEvt(BIT(EVT_NEW_BLK));
				pCtx->eState = GS_WaitReq;
				Sched_Yield();
			}
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
	static uint16 nNewFree = FF16;
	PRINTF("[GC] Request Free Blk: %X\n", eType);
	if (OPEN_GC == eType)
	{
		uint16 nBN;
		BlkInfo* pBI = META_GetFree(&nBN, true);
		assert(nullptr != pBI);
		return nBN;
	}
	else
	{
		if (FF16 != nNewFree)
		{
			CmdInfo* pCmd = IO_GetDone(IOCB_UErs);
			if (nullptr != pCmd)
			{
				uint16 nBN = nNewFree;
				nNewFree = FF16;
				IO_Free(pCmd);
				return nBN;
			}
		}
		else
		{
			uint16 nBN;
			BlkInfo* pBI = META_GetFree(&nBN, false);
			if (nullptr == pBI)
			{
				gbmGcReq |= BIT(eType);
				Sched_TrigSyncEvt(BIT(EVT_BLK_REQ));
			}
			else
			{
				nNewFree = nBN;
				CmdInfo* pCmd = IO_Alloc(IOCB_UErs);
				IO_Erase(pCmd, nBN, FF32);
			}
		}
	}
	return FF16;
}

static uint8 anContext[4096];		///< Stack like meta context.

void GC_Init()
{
	gbmGcReq = 0;
	GcCtx* pCtx = (GcCtx*)anContext;
	MEMSET_PTR(pCtx, 0);
	Sched_Register(TID_GC, gc_Run, anContext, BIT(MODE_NORMAL));
}
