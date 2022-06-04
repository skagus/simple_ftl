
#include "templ.h"
#include "cpu.h"
#include "scheduler.h"
#include "buf.h"
#include "io.h"
#include "page_gc.h"
#include "page_meta.h"

#define PRINTF			SIM_Print

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

struct ReqStk
{
	ReqState eState;
	uint8 nCurSlot;
};

enum ReqStep
{
	RS_Init,
	RS_Run,
	RS_BlkErsWait,
};

struct CmdStk
{
	ReqStep eStep;
	ReqInfo* pReq;	// input.
	uint32 nWaitAge; ///< Meta save check.
	uint16 nNextBlk;
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
#if 0
	if (NC_READ == eCmd)
	{
		PRINTF("[R] LPN:%X == %X\n", pReq->nLPN, *pnVal);
	}
	else
	{
		PRINTF("[W] LPN:%X (%X)\n", pReq->nLPN, *pnVal);
	}
#endif
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

bool req_Write_SM(CmdStk* pCtx, bool b1st)
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
	if (nullptr == pDst || pDst->stNextVA.nWL >= NUM_WL)
	{
		ErbStk* pChild = (ErbStk*)(pCtx + 1);
		if (RS_Init == pCtx->eStep)
		{
			uint16 nBN = GC_ReqFree(OPEN_USER);
			if (FF16 != nBN)
			{
				pChild->nBN = nBN;
				GC_BlkErase_SM(pChild, true);
				pCtx->eStep = RS_BlkErsWait;
			}
			else
			{
				Sched_Wait(BIT(EVT_NEW_BLK), LONG_TIME);
			}
		}
		else
		{
			assert(RS_BlkErsWait == pCtx->eStep);
			if (GC_BlkErase_SM((ErbStk*)(pCtx + 1), false))
			{
				META_SetOpen(OPEN_USER, pChild->nBN);
				pCtx->eStep = RS_Run;
				Sched_Yield();
			}
		}
	}
#if (EN_P2L_IN_DATA == 1)
	else if (pDst->nNextPage == NUM_DATA_PAGE)// P2L program in Last P2L.
	{
		uint16 nBuf = BM_Alloc();
		*(uint32*)BM_GetSpare(nBuf) = P2L_MARK;
		uint8* pMain = BM_GetMain(nBuf);
		assert(sizeof(pDst->anP2L) <= BYTE_PER_PPG);
		memcpy(pMain, pDst->anP2L, sizeof(pDst->anP2L));
		CmdInfo* pCmd = IO_Alloc(IOCB_User);
		IO_Program(pCmd, pDst->nBN, pDst->nNextPage, nBuf, P2L_MARK);
		pDst->nNextPage++;
		Sched_Wait(BIT(EVT_NAND_CMD), LONG_TIME); ///< Wait P2L program done.
		bRet = true;
	}
#endif
	else
	{
		*(uint32*)BM_GetSpare(pReq->nBuf) = pReq->nLPN;
		assert(pReq->nLPN == *(uint32*)BM_GetMain(pReq->nBuf));
		JnlRet eJRet = META_Update(pReq->nLPN, pDst->stNextVA, OPEN_USER);
		if (JR_Busy != eJRet)
		{
			CmdInfo* pCmd = IO_Alloc(IOCB_User);
			IO_Program(pCmd, pDst->stNextVA.nBN, pDst->stNextVA.nWL, pReq->nBuf, pCtx->nTag);

			PRINTF("[W] %X: {%X,%X}\n", pReq->nLPN, pDst->stNextVA.nBN, pDst->stNextVA.nWL);
#if (EN_P2L_IN_DATA == 1)
			pDst->anP2L[pDst->nNextPage] = pReq->nLPN;
			pDst->nNextPage++;
			if (pDst->nNextPage != NUM_DATA_PAGE)
			{
				bRet = true;
			}
			else
			{
				Sched_Yield();
			}
#else
			pDst->stNextVA.nWL++;
			bRet = true;
			if (JR_Filled == eJRet)
			{
				META_ReqSave();
			}
		}
		else
		{
			Sched_Wait(BIT(EVT_META), LONG_TIME);
		}
#endif
	}
	return bRet;
}

bool req_Read_SM(CmdStk* pCtx, bool b1st)
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
	PRINTF("[R] %X: {%X,%X}\n", nLPN, stAddr.nBN, stAddr.nWL);
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

bool req_Shutdown_SM(CmdStk* pCtx, bool b1st)
{
	if (b1st)
	{
		pCtx->eStep = RS_Init;
		pCtx->nDone = 0;
		pCtx->nIssued = 0;
		GC_Stop();
		pCtx->nWaitAge = META_ReqSave();
	}

	if (META_GetAge() > pCtx->nWaitAge)
	{
		gfCbf(pCtx->pReq);
		gstReqInfoPool.PushTail(pCtx->nTag);
		CPU_Wakeup(CPU_WORK, SIM_USEC(2));
		return true;
	}
	else
	{
		Sched_Wait(BIT(EVT_META), LONG_TIME);
		return false;
	}
}

CmdStk* gpDbgReqCtx;

void req_Run(void* pParam)
{
	ReqStk*  pCtx = (ReqStk*)pParam;
	switch (pCtx->eState)
	{
		case RS_WaitOpen:
		{
			gpDbgReqCtx = (CmdStk*)(pCtx + 1);

			if (META_Ready())
			{
				pCtx->eState = RS_WaitUser;
				Sched_Yield();
			}
			else
			{
				Sched_Wait(BIT(EVT_OPEN), LONG_TIME);
			}
			break;
		}
		case RS_WaitUser:
		{
			if (gstReqQ.Count() <= 0)
			{
				Sched_Wait(BIT(EVT_USER_CMD), LONG_TIME);
				break;
			}
			pCtx->nCurSlot = gstReqInfoPool.PopHead();
			RunInfo* pRun = gaIssued + pCtx->nCurSlot;
			pRun->pReq = gstReqQ.PopHead();
			pRun->nDone = 0;
			pRun->nIssued = 0;
			pRun->nTotal = 1; //
			pCtx->eState = RS_WaitIssue;
			
			CmdStk* pChild = (CmdStk*)(pCtx + 1);
			pChild->nTag = pCtx->nCurSlot;
			pChild->pReq = pRun->pReq;
			switch (pRun->pReq->eCmd)
			{
				case CMD_READ:
				{
					if (req_Read_SM(pChild, true))
					{
						pCtx->eState = RS_WaitUser;
						Sched_Yield();
					}
					break;
				}
				case CMD_WRITE:
				{
					if (req_Write_SM(pChild, true))
					{
						pCtx->eState = RS_WaitUser;
						Sched_Yield();
					}
					break;
				}
				case CMD_SHUTDOWN:
				{
					if (req_Shutdown_SM(pChild, true))
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
		case RS_WaitIssue:
		{
			RunInfo* pRun = gaIssued + pCtx->nCurSlot;
			ReqInfo* pReq = pRun->pReq;
			CmdStk* pChild = (CmdStk*)(pCtx + 1);
			switch (pReq->eCmd)
			{
				case CMD_WRITE:
				{
					if (req_Write_SM(pChild, false))
					{
						pCtx->eState = RS_WaitUser;
						Sched_Yield();
					}
					break;
				}
				case CMD_READ:
				{
					if (req_Read_SM(pChild, false))
					{
						pCtx->eState = RS_WaitUser;
						Sched_Yield();
					}
					break;
				}
				case CMD_SHUTDOWN:
				{
					if (req_Shutdown_SM(pChild, false))
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
	CmdInfo* pCmd = IO_PopDone(IOCB_User);
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
#if (EN_P2L_IN_DATA == 1)
			if (P2L_MARK == pCmd->nTag)
			{
				META_SetBlkState(pCmd->anBBN[0], BS_Closed);
				BM_Free(pCmd->stPgm.anBufId[0]);
			}
			else
#else
			if (pCmd->nWL == (NUM_DATA_PAGE - 1))
			{
				META_SetBlkState(pCmd->anBBN[0], BS_Closed);
			}
#endif
			{
				req_Done(pCmd->eCmd, pCmd->nTag);
			}
		}
		IO_Free(pCmd);
		Sched_Yield();
	}
}

static uint8 aStateCtx[4096];		///< Stack like meta context.
ReqStk* gpReqRunCtx;	// for Debug.

void REQ_Init()
{
	MEMSET_ARRAY(aStateCtx, 0);
	gstReqInfoPool.Init();
	for (uint8 nIdx = 0; nIdx < SIZE_REQ_QUE; nIdx++)
	{
		gstReqInfoPool.PushTail(nIdx);
	}
	gpReqRunCtx = (ReqStk*)aStateCtx;
	Sched_Register(TID_REQ, req_Run, aStateCtx, BIT(MODE_NORMAL));
	Sched_Register(TID_REQ_RESP, reqResp_Run, nullptr, BIT(MODE_NORMAL));
}
