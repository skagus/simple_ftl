
#include "cpu.h"
#include "scheduler.h"
#include "buf.h"
#include "io.h"
#include "page_gc.h"
#include "page_meta.h"

#define PRINTF			//	SIM_Print
uint16 gbmGcReq;

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

	uint32 nMtAge;		///< Meta저장 확인용 Age값.
	uint32 nSeqNo;
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

struct GcMoveCtx
{
	uint16 nDstBN;	// Input.
	uint16 nDstWL;
	uint32 aDstLPN[NUM_WL];

	uint16 nSrcBN;	// Block map PBN.
	uint16 nSrcWL;
	uint32 aSrcLPN[NUM_WL];

	bool bRunP2L;
	uint8 nReadRun;
	uint8 nPgmRun;
};

uint16 gc_GetNextRead(uint16 nCur, uint32* aLPN)
{
	while (nCur < NUM_WL)
	{
		if (FF32 != aLPN[nCur])
		{
			return nCur;
		}
		nCur++;
	}
	return FF16;
}

void gc_HandlePgm(CmdInfo* pDone, GcMoveCtx* pCtx)
{
	uint16 nBuf = pDone->stPgm.anBufId[0];
	uint32* pSpare = (uint32*)BM_GetSpare(nBuf);
	PRINTF("[GC] PGM Done to {%X, %X}, LPN:%X\n",
		pDone->anBBN[0], pDone->nWL, *pSpare);
	VAddr stAddr;
	stAddr.nDW = 0;
	stAddr.nBN = pDone->anBBN[0];
	stAddr.nWL = pDone->nWL;
	pCtx->aDstLPN[pDone->nWL] = *pSpare;
	META_Update(*pSpare, stAddr);
	if ((*pSpare & 0xF) == pDone->nTag)
	{
		uint32* pMain = (uint32*)BM_GetMain(nBuf);
		assert((*pMain & 0xF) == pDone->nTag);
	}
	BM_Free(nBuf);
}

void gc_HandleRead(CmdInfo* pDone, GcMoveCtx* pCtx)
{
	uint16 nBuf = pDone->stRead.anBufId[0];
	uint32* pSpare = (uint32*)BM_GetSpare(nBuf);
	uint16 nLPO = pDone->nTag;
	PRINTF("[GC] Read Done from {%X, %X}, LPN:%X\n",
		pDone->anBBN[0], pDone->nWL, *pSpare);
	assert(*pSpare == pCtx->aSrcLPN[pDone->nWL]);
	CmdInfo* pNewPgm = IO_Alloc(IOCB_Mig);
	IO_Program(pNewPgm, pCtx->nDstBN, nLPO, nBuf, nLPO);
}

void gc_HandleReadP2L(CmdInfo* pDone, GcMoveCtx* pCtx)
{
	uint16 nBuf = pDone->stPgm.anBufId[0];
	uint32* pSpare = (uint32*)BM_GetSpare(nBuf);
	PRINTF("[GC] P2L Done to {%X, %X}, LPN:%X\n",
		pDone->anBBN[0], pDone->nWL, *pSpare);
	uint32* pMain = (uint32*)BM_GetMain(nBuf);
	//			uint32 nIdx = *pSpare;	// Index of P2L chunk.
	uint32 nSize = sizeof(pCtx->aSrcLPN);
	memcpy(pCtx->aSrcLPN, pMain, nSize);
	META_FilterP2L(pCtx->nSrcBN, pCtx->aSrcLPN);
	BM_Free(nBuf);
}

bool gc_Move(GcMoveCtx* pCtx, bool b1st)
{
	bool bRet = false;

	if (b1st)
	{
		pCtx->nDstWL = 0;
		pCtx->nReadRun = 0;
		pCtx->nPgmRun = 0;
		PRINTF("[GC] Start Move to %X\n", pCtx->nDstBN);
	}
	/// Process done command.
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
				pCtx->bRunP2L = false;
			}
			else
			{
				gc_HandleRead(pDone, pCtx);
			}
		}
		IO_Free(pDone);
	}
	/// Issue New command.
	if (NUM_WL == pCtx->nDstWL)
	{
		PRINTF("[GC] Move done\n");
		bRet = true;
	}
	else if((pCtx->nReadRun < 2) && (pCtx->nPgmRun < 2))
	{
		uint16 nBuf4Copy = BM_Alloc();
		if (FF16 == pCtx->nSrcBN) // Load P2L map
		{
			if (false == pCtx->bRunP2L)
			{
				META_GetMinVPC(&pCtx->nSrcBN);
				pCtx->nSrcWL = 0;
				CmdInfo* pCmd = IO_Alloc(IOCB_Mig);
				IO_Read(pCmd, pCtx->nSrcBN, NUM_WL - 1, nBuf4Copy, FF32);
				pCtx->nReadRun++;
				pCtx->bRunP2L = true;
			}
		}
		else
		{
			uint16 nLPO = gc_GetNextRead(pCtx->nSrcWL, pCtx->aSrcLPN);
			if (FF16 == nLPO)
			{
				pCtx->nSrcBN = FF16;
				Sched_Yield();
				return false;
			}
			CmdInfo* pCmd = IO_Alloc(IOCB_Mig);
			IO_Read(pCmd, pCtx->nSrcBN, nLPO, nBuf4Copy, nLPO);
			pCtx->nReadRun++;
			pCtx->nSrcWL++;
		}
	}

	if ((pCtx->nReadRun > 0)|| (pCtx->nReadRun > 0))
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
				Sched_Wait(BIT(EVT_BLOCK), LONG_TIME);
			}
			break;
		}
		case GS_GetDst:
		{
			uint16 nFree;
			if (nullptr != META_GetFree(&nFree, true))
			{
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
				pCtx->eState = GS_WaitReq;
				Sched_Yield();
			}
			break;
		}
	}
}

uint16 GC_ReqFree(OpenType eType)
{
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
		uint16 nBN;
		BlkInfo* pBI = META_GetFree(&nBN, false);
		if (nullptr == pBI)
		{
			gbmGcReq |= BIT(eType);
			Sched_TrigSyncEvt(BIT(EVT_BLOCK));
			Sched_Wait(BIT(EVT_BLOCK), LONG_TIME);
			return FF16;
		}
		else
		{
			return nBN;
		}
	}
}

static uint8 anContext[4096];		///< Stack like meta context.

void GC_Init()
{
	gbmGcReq = 0;
	GcCtx* pCtx = (GcCtx*)anContext;
	MEMSET_PTR(pCtx, 0);
	Sched_Register(TID_GC, gc_Run, anContext, BIT(MODE_NORMAL));
}
