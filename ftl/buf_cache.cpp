
#include "types.h"
#include "templ.h"
#include "buf.h"
#include "page_gc.h"
#include "buf_cache.h"


#define NUM_CACHE_ENTRY		(2)
#define BUF_PER_FLUSH		(CHUNK_PER_PPG)
#define MAX_WRITE_CMD		(4)

Queue<uint32, NUM_CACHE_ENTRY> gstCache;
Queue<CmdInfo*, MAX_WRITE_CMD> gstIssued;
bool gbReqFlush;

union TagUW
{
	uint32 nDW;
	struct
	{
		uint32 nCetID : 8;
		uint32 nDone : 1;
		uint32 nDummy : 32 - 9;
	};
};

void BC_ReqFlush(bool bSync)
{
	gbReqFlush = true;
	OS_SyncEvt(BIT(EVT_CACHE));
	while (gstCache.Count() > 0)
	{
		OS_Wait(BIT(EVT_CACHE), LONG_TIME);
	}
}

/**
* LPN을 찾아서, ref count ++ 및 buffer id를 return.
*/
uint16 BC_SetRef(uint32 nLPN)
{
	return INV_BUF;
}

void BC_AddWrite(uint32 nLPN, uint16 nBuf, uint16 nTag)
{
	while (gstCache.IsFull())
	{
		OS_Wait(BIT(EVT_CACHE), LONG_TIME);
	}
	BM_GetSpare(nBuf)->User.nLPN = nLPN;
	gstCache.PushTail((nBuf << 16) | nTag);
	if (gstCache.Count() >= BUF_PER_FLUSH)
	{
		OS_SyncEvt(BIT(EVT_CACHE));
	}
}

void bc_Flush()
{
	uint32 nDW = gstCache.PopHead();
	uint16 nBuf = nDW >> 16;
	uint16 nTag = nDW & 0xFFFF;

	Spare* pSpare = BM_GetSpare(nBuf);

	OpenBlk* pDst = META_GetOpen(OPEN_USER);
	if (nullptr == pDst || pDst->stNextVA.nWL >= NUM_WL)
	{
		uint16 nBN = GC_ReqFree_Blocking(OPEN_USER);
		ASSERT(FF16 != nBN);
		GC_BlkErase_OS(OPEN_USER, nBN);
		META_SetOpen(OPEN_USER, nBN);
	}
	ASSERT(pSpare->User.nLPN == *(uint32*)BM_GetMain(nBuf));
	VAddr stVA = pDst->stNextVA;

	CmdInfo* pCmd = IO_Alloc(IOCB_UWrite);
	IO_Program(pCmd, stVA.nBN, stVA.nWL, nBuf, nTag);
	pDst->stNextVA.nWL++;
	gstIssued.PushTail(pCmd);

	JnlRet eJRet;
	while (true)
	{
		eJRet = META_Update(pSpare->User.nLPN, stVA, OPEN_USER);
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
}

void bc_Run(void* pParam)
{
	while (false == META_Ready())
	{
		OS_Wait(BIT(EVT_OPEN), LONG_TIME);
	}

	while (true)
	{
		while (gstCache.Count() >= BUF_PER_FLUSH)
		{
			bc_Flush();
		}
		if (gbReqFlush)
		{
			while (NOT(gstCache.IsEmpty()))
			{
				bc_Flush();
			}
			gbReqFlush = false;
			OS_SyncEvt(BIT(EVT_CACHE));
		}
		OS_Wait(BIT(EVT_CACHE), LONG_TIME);
	}
}

void bc_RespRun(void* pParam)
{
	while (true)
	{
		CmdInfo* pCmd = IO_PopDone(IOCB_UWrite);
		if (nullptr == pCmd)
		{
			OS_Wait(BIT(EVT_NAND_CMD), LONG_TIME);
		}
		else
		{
			CmdInfo* pOrdered = gstIssued.PopHead();
			ASSERT(pOrdered == pCmd);
			ASSERT(NC_PGM == pCmd->eCmd);
			if (pCmd->nWL == (NUM_DATA_PAGE - 1))
			{
				META_SetBlkState(pCmd->anBBN[0], BS_Closed);
			}
			REQ_Done(pCmd->nTag);
			BM_Free(pCmd->stPgm.anBufId[0]);
			IO_Free(pCmd);
			OS_Wait(0, 0);
		}
	}
}


void BC_Init()
{
	gstCache.Init();
	gstIssued.Init();
	OS_CreateTask(bc_Run, nullptr, nullptr, "req");
	OS_CreateTask(bc_RespRun, nullptr, nullptr, "req_resp");
}