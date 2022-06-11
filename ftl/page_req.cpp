
#include "templ.h"
#include "cpu.h"
#include "buf.h"
#include "os.h"
#include "io.h"
#include "page_gc.h"
#include "page_meta.h"

#define PRINTF			SIM_Print
#define CMD_PRINTF		SIM_Print

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

#if 0
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
#endif

void req_Done(NCmd eCmd, uint32 nTag)
{
	RunInfo* pRun = gaIssued + nTag;
	ReqInfo* pReq = pRun->pReq;
	uint32* pnVal = (uint32*)BM_GetSpare(pReq->nBuf);
	pRun->nDone++;

	if (NC_READ == eCmd)
	{
		CMD_PRINTF("[R] Done LPN:%X SPR:%X\n", pReq->nLPN, *pnVal);
	}
	else
	{
		CMD_PRINTF("[W] Done LPN:%X SPR:%X\n", pReq->nLPN, *pnVal);
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

void req_Write_OS(ReqInfo* pReq, uint8 nTag)
{
	uint32 nLPN = pReq->nLPN;
	bool bRet = false;
	OpenBlk* pDst = META_GetOpen(OPEN_USER);
	if (nullptr == pDst || pDst->stNextVA.nWL >= NUM_WL)
	{
		uint16 nBN = GC_ReqFree_Blocking(OPEN_USER);
		assert(FF16 != nBN);
		GC_BlkErase_OS(OPEN_USER, nBN);
		META_SetOpen(OPEN_USER, nBN);
	}
	*(uint32*)BM_GetSpare(pReq->nBuf) = pReq->nLPN;
	assert(pReq->nLPN == *(uint32*)BM_GetMain(pReq->nBuf));
	JnlRet eJRet;
	do
	{
		eJRet = META_Update(pReq->nLPN, pDst->stNextVA, OPEN_USER);
	} while (JR_Busy == eJRet);

	CmdInfo* pCmd = IO_Alloc(IOCB_User);
	IO_Program(pCmd, pDst->stNextVA.nBN, pDst->stNextVA.nWL, pReq->nBuf, nTag);

	CMD_PRINTF("[W] %X: {%X,%X}\n", pReq->nLPN, pDst->stNextVA.nBN, pDst->stNextVA.nWL);
	pDst->stNextVA.nWL++;
	
	if (JR_Filled == eJRet)
	{
		uint32 nWaitAge = META_ReqSave();	// wait till meta save.
		while (META_GetAge() <= nWaitAge)
		{
			OS_Wait(BIT(EVT_META), LONG_TIME);
		}
	}
}

/**
* Unmap read인 경우, sync response, 
* normal read는 nand IO done에서 async response.
*/
bool req_Read_OS(ReqInfo* pReq, uint8 nTag)
{
	uint32 nLPN = pReq->nLPN;
	VAddr stAddr = META_GetMap(nLPN);

	if (FF32 != stAddr.nDW)
	{
		CmdInfo* pCmd = IO_Alloc(IOCB_User);
		IO_Read(pCmd, stAddr.nBN, stAddr.nWL, pReq->nBuf, nTag);
		CPU_TimePass(SIM_USEC(3));
	}
	else
	{
		uint32* pnVal = (uint32*)BM_GetSpare(pReq->nBuf);
		*pnVal = nLPN;
		req_Done(NC_READ, nTag);
	}
	return true;
}

/**
* Shutdown command는 항상 sync로 처리한다.
*/
void req_Shutdown_OS(ReqInfo* pReq, uint8 nTag)
{
	CMD_PRINTF("[SD] %d\n", pReq->eOpt);
	GC_Stop();
	while (IO_CountFree() >= NUM_NAND_CMD)
	{
		OS_Wait(BIT(EVT_IO_FREE), LONG_TIME);
	}

	if (SD_Safe == pReq->eOpt)
	{
		uint32 nWaitAge = META_ReqSave();
		while (META_GetAge() <= nWaitAge)
		{
			OS_Wait(BIT(EVT_META), LONG_TIME);
		}
	}

	gfCbf(pReq);
	gstReqInfoPool.PushTail(nTag);
	CMD_PRINTF("[SD] Done\n");
	CPU_Wakeup(CPU_WORK, SIM_USEC(2));
}

void req_Run(void* pParam)
{
	while (false == META_Ready())
	{
		OS_Wait(BIT(EVT_OPEN), LONG_TIME);
	}

	while (true)
	{
		if (gstReqQ.Count() <= 0)
		{
			OS_Wait(BIT(EVT_USER_CMD), LONG_TIME);
			continue;
		}

		uint8 nCurSlot = gstReqInfoPool.PopHead();
		RunInfo* pRun = gaIssued + nCurSlot;
		ReqInfo* pReq = gstReqQ.PopHead();
		pRun->pReq = pReq;
		switch (pReq->eCmd)
		{
			case CMD_READ:
			{
				req_Read_OS(pReq, nCurSlot);
				break;
			}
			case CMD_WRITE:
			{
				req_Write_OS(pReq, nCurSlot);
				break;
			}
			case CMD_SHUTDOWN:
			{
				req_Shutdown_OS(pReq, nCurSlot);
				break;
			}
			default:
			{
				assert(false);
			}
		}
	}
}

/**
* Error는 response task에서 처리하도록 하자.
*/
void reqResp_Run(void* pParam)
{
	while (true)
	{
		CmdInfo* pCmd = IO_PopDone(IOCB_User);
		if (nullptr == pCmd)
		{
			OS_Wait(BIT(EVT_NAND_CMD), LONG_TIME);
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
			OS_Wait(0, 0);
		}
	}
}

void REQ_Init()
{
	gstReqInfoPool.Init();
	for (uint8 nIdx = 0; nIdx < SIZE_REQ_QUE; nIdx++)
	{
		gstReqInfoPool.PushTail(nIdx);
	}
	OS_CreateTask(req_Run, nullptr, nullptr, 0xFF);
	OS_CreateTask(reqResp_Run, nullptr, nullptr, 0xFF);
}
