
#include "templ.h"
#include "power.h"
#include "buf.h"
#include "ftl.h"
#include "io.h"
#include "test.h"
#include "gc.h"
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

bool req_Write(ReqCtx* pCtx, bool b1st)
{
	if (b1st)
	{
		pCtx->eStep = RS_Init;
		pCtx->nDone = 0;
		pCtx->nIssued = 0;
	}
	ReqInfo* pReq = pCtx->pReq;
	uint16 nLBN = pReq->nLPN / CHUNK_PER_PBLK;
	uint16 nLPO = pReq->nLPN % CHUNK_PER_PBLK;
	bool bRet = false;
	switch (pCtx->eStep)
	{
		case RS_Init:
		{
			LogMap* pMap = META_SearchLogMap(nLBN);
			if (nullptr == pMap || pMap->nCPO >= CHUNK_PER_PBLK)
			{
				pMap = GC_MakeNewLog(nLBN, pMap);
			}
			pCtx->eStep = RS_BlkWait;
			Sched_Wait(0, 1);
			break;
		}
		case RS_BlkWait:
		{
			LogMap* pMap = META_SearchLogMap(nLBN);
			*(uint32*)BM_GetSpare(pReq->nBuf) = pReq->nLPN;
			assert(pReq->nLPN == *(uint32*)BM_GetMain(pReq->nBuf));
			CmdInfo* pCmd = IO_Alloc(IOCB_User);
			IO_Program(pCmd, pMap->nPBN, pMap->nCPO, pReq->nBuf, pCtx->nTag);
			pMap->anMap[nLPO] = pMap->nCPO;
			pMap->nCPO++;
			bRet = true;
			break;
		}
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
	uint16 nLBN = pReq->nLPN / CHUNK_PER_PBLK;
	uint16 nLPO = pReq->nLPN % CHUNK_PER_PBLK;
	uint16 nPPO = 0xFFFF;

	LogMap* pMap = META_SearchLogMap(nLBN);
	if (nullptr != pMap)
	{
		nPPO = pMap->anMap[nLPO];
	}
	CmdInfo* pCmd = IO_Alloc(IOCB_User);
	if (0xFFFF != nPPO)	// in Log block.
	{
		IO_Read(pCmd, pMap->nPBN, nPPO, pReq->nBuf, pCtx->nTag);
	}
	else
	{
		BlkMap* pBMap = META_GetBlkMap(nLBN);
		IO_Read(pCmd, pBMap->nPBN, nLPO, pReq->nBuf, pCtx->nTag);
	}

	SIM_CpuTimePass(3);
	return true;
}


void req_Run(void* pParam)
{
	ReqRunCtx*  pCtx = (ReqRunCtx*)pParam;

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
						Sched_Wait(0, 1); // do Next.
					}
					break;
				}
				case CMD_WRITE:
				{
					if (req_Write(pChild, true))
					{
						pCtx->eState = RS_WaitUser;
						Sched_Wait(0, 1); // do Next.
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
						Sched_Wait(0, 1);
					}
					break;
				}
				case CMD_READ:
				{
					if (req_Read(pChild, false))
					{
						pCtx->eState = RS_WaitUser;
						Sched_Wait(0, 1);
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
	assert(Sched_WillRun());
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
