
#include "templ.h"
#include "buf.h"
#include "os.h"
#include "io.h"
#include "buf_cache.h"
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

void REQ_Done(uint32 nTag)
{
	RunInfo* pRun = gaIssued + nTag;
	ReqInfo* pReq = pRun->pReq;
	Spare* pSpare = BM_GetSpare(pReq->nBuf);
	pRun->nDone++;

	if (MARK_ERS != pSpare->User.nLPN)
	{
		ASSERT(pReq->nLPN == pSpare->User.nLPN);
	}
	// Calls CPU_WORK cpu function --> treat as ISR.
	if (pRun->nDone == pRun->nTotal)
	{
		gfCbf(pReq);
		gstReqInfoPool.PushTail(nTag);
	}
}

void req_Write_OS(ReqInfo* pReq, uint8 nTag)
{
	uint32 nLPN = pReq->nLPN;
	uint16 nBuf = pReq->nBuf;
#if EN_BUF_CACHE
	BC_AddWrite(nLPN, nBuf, nTag);
#else
	OpenBlk* pDst = META_GetOpen(OPEN_USER);
	if (nullptr == pDst || pDst->stNextVA.nWL >= NUM_WL)
	{
		uint16 nBN = GC_ReqFree_Blocking(OPEN_USER);
		ASSERT(FF16 != nBN);
		GC_BlkErase_OS(OPEN_USER, nBN);
		META_SetOpen(OPEN_USER, nBN);
	}
	BM_GetSpare(nBuf)->User.nLPN = nLPN;
	ASSERT(nLPN == *(uint32*)BM_GetMain(nBuf));
	VAddr stVA = pDst->stNextVA;

	CmdInfo* pCmd = IO_Alloc(IOCB_URead);
	IO_Program(pCmd, stVA.nBN, stVA.nWL, nBuf, nTag);
	pDst->stNextVA.nWL++;

	JnlRet eJRet;
	while (true)
	{
		eJRet = META_Update(nLPN, stVA, OPEN_USER);
		if (JR_Busy != eJRet)
		{
			break;
		}
		OS_Wait(BIT(EVT_META), LONG_TIME);
	}

	if (JR_Filled == eJRet)
	{
		META_ReqSave(false);	// wait till meta save.
	}
#endif
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
		CmdInfo* pCmd = IO_Alloc(IOCB_URead);
		IO_Read(pCmd, stAddr.nBN, stAddr.nWL, pReq->nBuf, nTag);
	}
	else
	{
		BM_GetSpare(pReq->nBuf)->User.nLPN = nLPN;
		REQ_Done(nTag);
	}
	return true;
}

/**
* Shutdown command는 항상 sync로 처리한다.
*/
void req_Shutdown_OS(ReqInfo* pReq, uint8 nTag)
{
	PRINTF("[SD] %d\n", pReq->eOpt);
#if EN_BUF_CACHE
	BC_ReqFlush(true);
#endif
	IO_SetStop(CbKey::IOCB_Mig, true);
	OS_Idle(OS_MSEC(5));

	if (SD_Safe == pReq->eOpt)
	{
		META_ReqSave(true);
	}

	gfCbf(pReq);
	gstReqInfoPool.PushTail(nTag);
	PRINTF("[SD] Done\n");
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
		pRun->nIssued = 0;
		pRun->nDone = 0;
		pRun->nTotal = 1;
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
				ASSERT(false);
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
		CmdInfo* pCmd = IO_PopDone(IOCB_URead);
		if (nullptr == pCmd)
		{
			OS_Wait(BIT(EVT_NAND_CMD), LONG_TIME);
		}
		else
		{
#if EN_BUF_CACHE
			ASSERT(NC_READ == pCmd->eCmd);
			REQ_Done(pCmd->nTag);
#else
			if (NC_READ == pCmd->eCmd)
			{
				REQ_Done(pCmd->nTag);
			}
			else
			{
				if (pCmd->nWL == (NUM_DATA_PAGE - 1))
				{
					META_SetBlkState(pCmd->anBBN[0], BS_Closed);
				}
				REQ_Done(pCmd->nTag);
			}
#endif
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
	OS_CreateTask(req_Run, nullptr, nullptr, "req");
	OS_CreateTask(reqResp_Run, nullptr, nullptr, "req_resp");
}
