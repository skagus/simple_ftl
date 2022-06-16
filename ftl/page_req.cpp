
#include "templ.h"
#include "buf.h"
#include "scheduler.h"
#include "io.h"
#include "page_gc.h"
#include "page_meta.h"

#define PRINTF			SIM_Print
#define CMD_PRINTF

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

struct ReqStk
{
	enum ReqState
	{
		WaitOpen,
		WaitCmd,
		Run,
	};
	ReqState eState;
	uint8 nCurSlot;
};

struct CmdStk
{
	enum ReqStep
	{
		Init,
		Run,
		BlkErsWait,
		WaitIoDone,		///< wait all IO done.
		WaitMtSave,
		Done,
	};
	ReqStep eStep;
	ReqInfo* pReq;	// input.
	uint32 nWaitAge; ///< Meta save check.
	uint32 nTag;
};


void req_Done(NCmd eCmd, uint32 nTag)
{
	RunInfo* pRun = gaIssued + nTag;
	ReqInfo* pReq = pRun->pReq;
	uint32* pnVal = (uint32*)BM_GetSpare(pReq->nBuf);
	pRun->nDone++;

	if (MARK_ERS != *pnVal)
	{
		ASSERT(pReq->nLPN == *pnVal);
	}
	// Calls CPU_WORK cpu function --> treat as ISR.
	if (pRun->nDone == pRun->nTotal)
	{
		gfCbf(pReq);
		gstReqInfoPool.PushTail(nTag);
	}
}

bool req_Write_SM(CmdStk* pCtx)
{
	if (CmdStk::Init == pCtx->eStep)
	{
		pCtx->eStep = CmdStk::Run;
	}
	ReqInfo* pReq = pCtx->pReq;
	uint32 nLPN = pReq->nLPN;
	bool bRet = false;
	OpenBlk* pDst = META_GetOpen(OPEN_USER);
	if (nullptr == pDst || pDst->stNextVA.nWL >= NUM_WL)
	{
		ErbStk* pChild = (ErbStk*)(pCtx + 1);
		if (CmdStk::Run == pCtx->eStep)
		{
			uint16 nBN = GC_ReqFree(OPEN_USER);
			if (FF16 != nBN)
			{
				pChild->nBN = nBN;
				pChild->eOpen = OPEN_USER;
				pChild->eStep = ErbStk::Init;
				GC_BlkErase_SM(pChild);
				pCtx->eStep = CmdStk::BlkErsWait;
			}
			else
			{
				Sched_Wait(BIT(EVT_NEW_BLK), LONG_TIME);
			}
		}
		else
		{
			assert(CmdStk::BlkErsWait == pCtx->eStep);
			if (GC_BlkErase_SM((ErbStk*)(pCtx + 1)))
			{
				META_SetOpen(OPEN_USER, pChild->nBN);
				pCtx->eStep = CmdStk::Run;
				Sched_Yield();
			}
		}
	}
	else
	{
		*(uint32*)BM_GetSpare(pReq->nBuf) = pReq->nLPN;
		ASSERT(pReq->nLPN == *(uint32*)BM_GetMain(pReq->nBuf));
		JnlRet eJRet = META_Update(pReq->nLPN, pDst->stNextVA, OPEN_USER);
		if (JR_Busy != eJRet)
		{
			CmdInfo* pCmd = IO_Alloc(IOCB_User);
			IO_Program(pCmd, pDst->stNextVA.nBN, pDst->stNextVA.nWL, pReq->nBuf, pCtx->nTag);

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
	}
	return bRet;
}

/**
* Unmap read인 경우, sync response, 
* normal read는 nand IO done에서 async response.
*/
bool req_Read_SM(CmdStk* pCtx)
{
	ReqInfo* pReq = pCtx->pReq;
	uint32 nLPN = pReq->nLPN;
	VAddr stAddr = META_GetMap(nLPN);
	if (CmdStk::Init == pCtx->eStep)
	{
		pCtx->eStep = CmdStk::Run;
	}
	if (FF32 != stAddr.nDW)
	{
		CmdInfo* pCmd = IO_Alloc(IOCB_User);
		IO_Read(pCmd, stAddr.nBN, stAddr.nWL, pReq->nBuf, pCtx->nTag);
	}
	else
	{
		uint32* pnVal = (uint32*)BM_GetSpare(pReq->nBuf);
		*pnVal = nLPN;
		req_Done(NC_READ, pCtx->nTag);
	}
	return true;
}

/**
* Shutdown command는 항상 sync로 처리한다.
*/
bool req_Shutdown_SM(CmdStk* pCtx)
{
	ShutdownOpt eOpt = pCtx->pReq->eOpt;
	bool bRet = false;
	switch (pCtx->eStep)
	{
		case CmdStk::Init:
		{
			CMD_PRINTF("[SD] %d\n", eOpt);
			GC_Stop();
			if (IO_CountFree() >= NUM_NAND_CMD)
			{
				if (SD_Sudden == eOpt)
				{
					gfCbf(pCtx->pReq);
					gstReqInfoPool.PushTail(pCtx->nTag);
					bRet = true;
				}
				else
				{
					pCtx->nWaitAge = META_ReqSave();
					pCtx->eStep = CmdStk::WaitMtSave;
					Sched_Wait(BIT(EVT_META), LONG_TIME);
				}
			}
			else
			{
				pCtx->eStep = CmdStk::WaitIoDone;
				Sched_Wait(BIT(EVT_IO_FREE), LONG_TIME);
			}
			break;
		}
		case CmdStk::WaitIoDone:
		{
			if (IO_CountFree() >= NUM_NAND_CMD)
			{
				if (SD_Sudden == eOpt)
				{
					gfCbf(pCtx->pReq);
					gstReqInfoPool.PushTail(pCtx->nTag);
					bRet = true;
				}
				else
				{
					pCtx->nWaitAge = META_ReqSave();
					pCtx->eStep = CmdStk::WaitMtSave;
					Sched_Wait(BIT(EVT_META), LONG_TIME);
				}
			}
			else
			{
				Sched_Wait(BIT(EVT_IO_FREE), LONG_TIME);
			}
			break;
		}
		case CmdStk::WaitMtSave:
		{
			assert(eOpt >= SD_Safe);
			if (META_GetAge() > pCtx->nWaitAge)
			{
				gfCbf(pCtx->pReq);
				gstReqInfoPool.PushTail(pCtx->nTag);
				CMD_PRINTF("[SD] Done\n");
				bRet = true;
			}
			else
			{
				Sched_Wait(BIT(EVT_META), LONG_TIME);
			}
			break;
		}
		default:
		{
			assert(false);
			break;
		}
	}
	return bRet;
}

CmdStk* gpDbgReqCtx;

void req_Run(void* pParam)
{
	ReqStk*  pReqStk = (ReqStk*)pParam;
	switch (pReqStk->eState)
	{
		case ReqStk::WaitOpen:
		{
			gpDbgReqCtx = (CmdStk*)(pReqStk + 1);

			if (META_Ready())
			{
				pReqStk->eState = ReqStk::WaitCmd;
				Sched_Yield();
			}
			else
			{
				Sched_Wait(BIT(EVT_OPEN), LONG_TIME);
			}
			break;
		}
		case ReqStk::WaitCmd:
		{
			if (gstReqQ.Count() <= 0)
			{
				Sched_Wait(BIT(EVT_USER_CMD), LONG_TIME);
				break;
			}
			pReqStk->nCurSlot = gstReqInfoPool.PopHead();
			RunInfo* pRun = gaIssued + pReqStk->nCurSlot;
			pRun->pReq = gstReqQ.PopHead();
			pRun->nDone = 0;
			pRun->nIssued = 0;
			pRun->nTotal = 1; //
			pReqStk->eState = ReqStk::Run;
			
			CmdStk* pCmdStk = (CmdStk*)(pReqStk + 1);
			pCmdStk->eStep = CmdStk::Init;
			pCmdStk->nTag = pReqStk->nCurSlot;
			pCmdStk->pReq = pRun->pReq;
			switch (pRun->pReq->eCmd)
			{
				case CMD_READ:
				{
					if (req_Read_SM(pCmdStk))
					{
						pReqStk->eState = ReqStk::WaitCmd;
						Sched_Yield();
					}
					break;
				}
				case CMD_WRITE:
				{
					if (req_Write_SM(pCmdStk))
					{
						pReqStk->eState = ReqStk::WaitCmd;
						Sched_Yield();
					}
					break;
				}
				case CMD_SHUTDOWN:
				{
					if (req_Shutdown_SM(pCmdStk))
					{
						pReqStk->eState = ReqStk::WaitCmd;
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
		case ReqStk::Run:
		{
			RunInfo* pRun = gaIssued + pReqStk->nCurSlot;
			ReqInfo* pReq = pRun->pReq;
			CmdStk* pCmdStk = (CmdStk*)(pReqStk + 1);
			switch (pReq->eCmd)
			{
				case CMD_WRITE:
				{
					if (req_Write_SM(pCmdStk))
					{
						pReqStk->eState = ReqStk::WaitCmd;
						Sched_Yield();
					}
					break;
				}
				case CMD_READ:
				{
					if (req_Read_SM(pCmdStk))
					{
						pReqStk->eState = ReqStk::WaitCmd;
						Sched_Yield();
					}
					break;
				}
				case CMD_SHUTDOWN:
				{
					if (req_Shutdown_SM(pCmdStk))
					{
						pReqStk->eState = ReqStk::WaitCmd;
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
			if (pCmd->nWL == (NUM_DATA_PAGE - 1))
			{
				META_SetBlkState(pCmd->anBBN[0], BS_Closed);
			}
			req_Done(pCmd->eCmd, pCmd->nTag);
		}
		IO_Free(pCmd);
		Sched_Yield();
	}
}

static uint8 aStateCtx[4096];		///< Stack like meta context.
ReqStk* gpReqStk;	// for Debug.

void REQ_Init()
{
	MEMSET_ARRAY(aStateCtx, 0);
	gstReqInfoPool.Init();
	for (uint8 nIdx = 0; nIdx < SIZE_REQ_QUE; nIdx++)
	{
		gstReqInfoPool.PushTail(nIdx);
	}
	gpReqStk = (ReqStk*)aStateCtx;
	gpReqStk->eState = ReqStk::WaitOpen;
	Sched_Register(TID_REQ, req_Run, aStateCtx, BIT(MODE_NORMAL));
	Sched_Register(TID_REQ_RESP, reqResp_Run, nullptr, BIT(MODE_NORMAL));
}
