
#include "templ.h"
#include "cpu.h"
#include "scheduler.h"
#include "buf.h"
#include "io.h"
#include "page_gc.h"
#include "page_meta.h"

#define PRINTF			SIM_Print
#define P2L_MARK		(0xFFAAFFAA)

extern Queue<ReqInfo*, SIZE_REQ_QUE> gstReqQ;

/// Requested
struct RunInfo
{
	ReqInfo* pReq;
	uint16 nIssued;	///< Issued count.
	uint16 nDone; ///< Done count.
	uint16 nTotal;
};
Queue<uint8, SIZE_REQ_QUE> gstReqInfoPool;
RunInfo gaIssued[SIZE_REQ_QUE];


CbfReq gfCbf;

void REQ_SetCbf(CbfReq pfCbf)
{
	gfCbf = pfCbf;
}

enum ReqState
{
	RS_WaitOpen,
	RS_WaitUser,
	RS_WaitIssue,
};

struct ReqRunCtx
{
	ReqState eState;
	uint8 nCurSlot;
};

enum ReqStep
{
	RS_Init,
	RS_Run,
	RS_BlkWait,
};

struct ReqCtx
{
	ReqStep eStep;
	ReqInfo* pReq;	// input.
	uint32 nTag;
	uint16 nIssued;
	uint16 nDone;
};


void req_Done(NCmd eCmd, uint32 nTag)
{
	RunInfo* pRun = gaIssued + nTag;
	ReqInfo* pReq = pRun->pReq;
	uint32* pnVal = (uint32*)BM_GetSpare(pReq->nBuf);
	pRun->nDone++;

	if (NC_READ == eCmd)
	{
		PRINTF("[REQ] Read: LPN:%X == %X\n", pReq->nLPN, *pnVal);
	}
	else
	{
		PRINTF("[REQ] Write: LPN:%X (%X)\n", pReq->nLPN, *pnVal);
	}

	if (MARK_ERS != *pnVal)
	{
		assert(pReq->nLPN == *pnVal);
	}
	// Calls CPU_WORK cpu function --> treat as ISR.
	if (pRun->nDone == pRun->nTotal)
	{
		gfCbf(pReq);
		gstReqInfoPool.PushTail(nTag);
		CPU_Wakeup(CPU_WORK, SIM_USEC(2));
	}
}

bool req_Write(ReqCtx* pCtx, bool b1st)
{
	if (b1st)
	{
		pCtx->eStep = RS_Init;
		pCtx->nDone = 0;
		pCtx->nIssued = 0;
	}
	ReqInfo* pReq = pCtx->pReq;
	uint32 nLPN = pReq->nLPN;
	bool bRet = false;
	OpenBlk* pDst = META_GetOpen(OPEN_USER);
	if (nullptr == pDst || pDst->nCWO >= NUM_WL)
	{
		uint16 nNewOpen = GC_ReqFree(OPEN_USER);
		if (FF16 != nNewOpen)
		{
			META_SetOpen(OPEN_USER, nNewOpen);
			Sched_Yield();
		}
		else
		{
			Sched_Wait(BIT(EVT_BLOCK), LONG_TIME);
		}
	}
	else if (pDst->nCWO == NUM_WL - 1)// P2L program in Last P2L.
	{
		uint16 nBuf = BM_Alloc();
		*(uint32*)BM_GetSpare(nBuf) = P2L_MARK;
		uint8* pMain = BM_GetMain(nBuf);
		assert(sizeof(pDst->anP2L) <= BYTE_PER_PPG);
		memcpy(pMain, pDst->anP2L, sizeof(pDst->anP2L));
		CmdInfo* pCmd = IO_Alloc(IOCB_User);
		IO_Program(pCmd, pDst->nBN, pDst->nCWO, nBuf, P2L_MARK);
		pDst->nCWO++;
		Sched_Wait(BIT(EVT_BLOCK), LONG_TIME); ///< Wait P2L program done.
	}
	else
	{
		*(uint32*)BM_GetSpare(pReq->nBuf) = pReq->nLPN;
		assert(pReq->nLPN == *(uint32*)BM_GetMain(pReq->nBuf));
		CmdInfo* pCmd = IO_Alloc(IOCB_User);
		IO_Program(pCmd, pDst->nBN, pDst->nCWO, pReq->nBuf, pCtx->nTag);
		pDst->anP2L[pDst->nCWO] = pReq->nLPN;
		pDst->nCWO++;
		bRet = true;
	}
	return bRet;
}

bool req_Read(ReqCtx* pCtx, bool b1st)
{
	if (b1st)
	{
		pCtx->eStep = RS_Init;
		pCtx->nDone = 0;
		pCtx->nIssued = 0;
	}
	ReqInfo* pReq = pCtx->pReq;
	uint32 nLPN = pReq->nLPN;

	VAddr stAddr = META_GetMap(nLPN);
	if (FF32 != stAddr.nDW)
	{
		CmdInfo* pCmd = IO_Alloc(IOCB_User);
		IO_Read(pCmd, stAddr.nBN, stAddr.nWL, pReq->nBuf, pCtx->nTag);
		CPU_TimePass(SIM_USEC(3));
	}
	else
	{
		req_Done(NC_READ, pCtx->nTag);
	}
	return true;
}


void req_Run(void* pParam)
{
	ReqRunCtx*  pCtx = (ReqRunCtx*)pParam;

	switch (pCtx->eState)
	{
		case RS_WaitOpen:
		{
#if 1
			pCtx->eState = RS_WaitUser;
			Sched_Yield();
#else
			if (META_Ready())
			{
				pCtx->eState = RS_WaitUser;
				Sched_Yield();
			}
			else
			{
				Sched_Wait(BIT(EVT_OPEN), LONG_TIME);
			}
#endif
			break;
		}
		case RS_WaitUser:
		{
			if (gstReqQ.Count() <= 0)
			{
				Sched_Wait(BIT(EVT_USER_CMD), LONG_TIME);
				break;
			}
			PRINTF("[REQ] Req Rcv\n");

			pCtx->nCurSlot = gstReqInfoPool.PopHead();
			RunInfo* pRun = gaIssued + pCtx->nCurSlot;
			pRun->pReq = gstReqQ.PopHead();
			pRun->nDone = 0;
			pRun->nIssued = 0;
			pRun->nTotal = 1; //
			pCtx->eState = RS_WaitIssue;
			
			ReqCtx* pChild = (ReqCtx*)(pCtx + 1);
			pChild->nTag = pCtx->nCurSlot;
			pChild->pReq = pRun->pReq;
			switch (pRun->pReq->eCmd)
			{
				case CMD_READ:
				{
					if (req_Read(pChild, true))
					{
						pCtx->eState = RS_WaitUser;
						Sched_Yield();
					}
					break;
				}
				case CMD_WRITE:
				{
					if (req_Write(pChild, true))
					{
						pCtx->eState = RS_WaitUser;
						Sched_Yield();
					}
					break;
				}
			}
			break;
		}
		case RS_WaitIssue:
		{
			RunInfo* pRun = gaIssued + pCtx->nCurSlot;
			ReqInfo* pReq = pRun->pReq;
			ReqCtx* pChild = (ReqCtx*)(pCtx + 1);
			switch (pReq->eCmd)
			{
				case CMD_WRITE:
				{
					if (req_Write(pChild, false))
					{
						pCtx->eState = RS_WaitUser;
						Sched_Yield();
					}
					break;
				}
				case CMD_READ:
				{
					if (req_Read(pChild, false))
					{
						pCtx->eState = RS_WaitUser;
						Sched_Yield();
					}
					break;
				}
				default:
				{
					assert(false);
				}
			}

			break;
		}
		default:
		{
			break;
		}
	}
}

/**
* Error는 response task에서 처리하도록 하자.
*/
void reqResp_Run(void* pParam)
{
	CmdInfo* pCmd = IO_GetDone(IOCB_User);
	if (nullptr == pCmd)
	{
		Sched_Wait(BIT(EVT_NAND_CMD), LONG_TIME);
	}
	else
	{
		if (NC_READ == pCmd->eCmd)
		{
			req_Done(pCmd->eCmd, pCmd->nTag);
		}
		else
		{
			if (P2L_MARK == pCmd->nTag)
			{
				BM_Free(pCmd->stPgm.anBufId[0]);
			}
			else
			{
				VAddr stVA;
				stVA.nDW = 0;
				stVA.nBN = pCmd->anBBN[0];
				stVA.nWL = pCmd->nWL;
				uint32* pnVal = (uint32*)BM_GetSpare(pCmd->stPgm.anBufId[0]);
				META_Update(*pnVal, stVA);
				req_Done(pCmd->eCmd, pCmd->nTag);
			}
		}
		IO_Free(pCmd);
		Sched_Yield();
	}
}

static uint8 anContext[4096];		///< Stack like meta context.

void REQ_Init()
{
	MEMSET_ARRAY(anContext, 0);
	gstReqInfoPool.Init();
	for (uint8 nIdx = 0; nIdx < SIZE_REQ_QUE; nIdx++)
	{
		gstReqInfoPool.PushTail(nIdx);
	}
	Sched_Register(TID_REQ, req_Run, anContext, BIT(MODE_NORMAL));
	Sched_Register(TID_REQ_RESP, reqResp_Run, nullptr, BIT(MODE_NORMAL));
}
