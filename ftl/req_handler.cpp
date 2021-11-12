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
	RS_WaitNand,
};

struct ReqRunCtx
{
	ReqState eState;
	ReqInfo* pCurRun;
};

static uint8 anContext[4096];		///< Stack like meta context.
ReqRunCtx* pCtx;
void req_Run(Evts bmEvt)
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
			pCtx->pCurRun = gstReqQ.PopHead();
			pCtx->eState = RS_WaitIssue;
			Sched_Wait(0, 1);	// Call me without Event.
			break;
		}
		case RS_WaitIssue:
		{
			ReqInfo* pReq = pCtx->pCurRun;
			switch (pReq->eCmd)
			{
				case CMD_WRITE:
				{
					FTL_Write(pReq->nLPN, pReq->nBuf);
					break;
				}
				case CMD_READ:
				{
					FTL_Read(pReq->nLPN, pReq->nBuf);
					break;
				}
				default:
				{
					assert(false);
				}
			}
			pCtx->eState = RS_WaitNand;
			Sched_Wait(BIT(EVT_NAND_CMD), 100);
			break;
		}
		case RS_WaitNand:
		{
			CmdInfo* pCmd = IO_GetDone(false);
			if (nullptr == pCmd)
			{
				Sched_Wait(BIT(EVT_NAND_CMD), 100);
			}
			else
			{
				ReqInfo* pReq = pCtx->pCurRun;
				uint32* pnVal = (uint32*)BM_GetSpare(pReq->nBuf);
				PRINTF("Read: %X, %X\n", pReq->nLPN, *pnVal);
				if (0xFFFFFFFF != *pnVal)
				{
					assert(pReq->nLPN == *pnVal);
				}

				IO_Free(pCmd);
				gfCbf(pCtx->pCurRun);
				pCtx->pCurRun = nullptr;
				pCtx->eState = RS_WaitUser;
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

void REQ_Init()
{
	MEMSET_ARRAY(anContext, 0);
	Sched_Register(TID_REQ, req_Run, BIT(MODE_NORMAL));
}
