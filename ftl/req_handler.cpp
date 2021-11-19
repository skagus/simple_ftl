
#include "templ.h"
#include "power.h"
#include "buf.h"
#include "ftl.h"
#include "io.h"
#include "test.h"
#include "meta_manager.h"
#include "req_handler.h"
#include "scheduler.h"

#define PRINTF		//	SIM_Print

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
	RS_WaitMerge,
	RS_WaitIssue,
};

struct ReqRunCtx
{
	ReqState eState;
	uint8 nCurSlot;
};

static uint8 anContext[4096];		///< Stack like meta context.
ReqRunCtx* pCtx;
void req_Run(void* pParam)
{
	pCtx = (ReqRunCtx*)anContext;

RETRY:

	switch (pCtx->eState)
	{
		case RS_WaitOpen:
		{
			if (META_Ready())
			{
				pCtx->eState = RS_WaitUser;
				goto RETRY;
			}
			else
			{
				Sched_Wait(BIT(EVT_OPEN), 0);
			}
			break;
		}
		case RS_WaitUser:
		{
			if (gstReqQ.Count() <= 0)
			{
				Sched_Wait(BIT(EVT_USER_CMD), 100);
				break;
			}
			pCtx->nCurSlot = gstReqInfoPool.PopHead();
			pCtx->eState = RS_WaitIssue;
			RunInfo* pRun = gaIssued + pCtx->nCurSlot;
			pRun->pReq = gstReqQ.PopHead();
			pRun->nDone = 0;
			pRun->nIssued = 0;
			pRun->nTotal = 1; //
			Sched_Wait(0, 1);	// Call me without Event.
			break;
		}
		case RS_WaitIssue:
		{
			RunInfo* pRun = gaIssued + pCtx->nCurSlot;
			ReqInfo* pReq = pRun->pReq;
			switch (pReq->eCmd)
			{
				case CMD_WRITE:
				{
					FTL_Write(pReq->nLPN, pReq->nBuf, pCtx->nCurSlot);
					break;
				}
				case CMD_READ:
				{
					FTL_Read(pReq->nLPN, pReq->nBuf, pCtx->nCurSlot);
					break;
				}
				default:
				{
					assert(false);
				}
			}
			pRun->nIssued++;
			if (pRun->nIssued == pRun->nTotal)
			{
				pCtx->eState = RS_WaitUser;
				goto RETRY;
			}
			else
			{
				assert(false);
				goto RETRY;
			}
			break;
		}
		case RS_WaitMerge:
		default:
		{
			break;
		}
	}
}

void reqResp_Run(void* pParam)
{
RETRY:

	CmdInfo* pCmd = IO_GetDone(IOCB_User);
	if (nullptr == pCmd)
	{
		Sched_Wait(BIT(EVT_NAND_CMD), 100);
	}
	else
	{
		RunInfo* pRun = gaIssued + pCmd->nTag;
		ReqInfo* pReq = pRun->pReq;
		uint32* pnVal = (uint32*)BM_GetSpare(pReq->nBuf);
		pRun->nDone++;
		PRINTF("Read: %X, %X\n", pReq->nLPN, *pnVal);
		if (0xFFFFFFFF != *pnVal)
		{
			assert(pReq->nLPN == *pnVal);
		}

		IO_Free(pCmd);
		if (pRun->nDone == pRun->nTotal)
		{
			gfCbf(pReq);
			gstReqInfoPool.PushTail(pCmd->nTag);
		}
		goto RETRY;
	}
}

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
