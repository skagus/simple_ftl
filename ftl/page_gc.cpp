
#include "templ.h"
#include "buf.h"
#include "os.h"
#include "io.h"
#include "page_gc.h"
#include "page_meta.h"

#define PRINTF			SIM_Print
#define MAX_GC_READ		(2)

Queue<uint16, SIZE_FREE_POOL> gstFreePool;

struct GcInfo
{
	uint16 nDstBN;
	uint16 nDstWL;
	uint16 nSrcBN;
	uint16 nSrcWL;
	uint8 nPgmRun;
	uint8 nReadRun;
};

uint8 gc_ScanFree();

void gc_HandlePgm(CmdInfo* pDone)
{
	uint16 nBuf = pDone->stPgm.anBufId[0];
	uint32* pSpare = (uint32*)BM_GetSpare(nBuf);
	BM_Free(nBuf);
}

/**
* 
*/
void gc_HandleRead(CmdInfo* pDone, GcInfo* pGI)
{
	bool bDone = true;
	uint16 nBuf = pDone->stRead.anBufId[0];
	uint32* pSpare = (uint32*)BM_GetSpare(nBuf);
	PRINTF("[GCR] {%X, %X}, LPN:%X\n", pDone->anBBN[0], pDone->nWL, *pSpare);

	if ((*pSpare != MARK_ERS) &&(pGI->nDstWL < NUM_WL))
	{
		VAddr stOld = META_GetMap(*pSpare);
		if ((pGI->nSrcBN == stOld.nBN) // Valid
			&& (pDone->nWL == stOld.nWL))
		{
			VAddr stAddr(0, pGI->nDstBN, pGI->nDstWL);
			CmdInfo* pNewPgm = IO_Alloc(IOCB_Mig);
			IO_Program(pNewPgm, pGI->nDstBN, pGI->nDstWL, nBuf, *pSpare);
			JnlRet eJRet;
			while(true)
			{
				eJRet = META_Update(*pSpare, stAddr, OPEN_GC);
				if (JR_Busy != eJRet)
				{
					break;
				}
				OS_Wait(BIT(EVT_META), LONG_TIME);
			}
			// User Data.
			if ((*pSpare & 0xF) == pDone->nTag)
			{
				uint32* pMain = (uint32*)BM_GetMain(nBuf);
				ASSERT((*pMain & 0xF) == pDone->nTag);
			}
			PRINTF("[GCW] {%X, %X}, LPN:%X\n", pGI->nDstBN, pGI->nDstWL, *pSpare);
			pGI->nDstWL++;
			pGI->nPgmRun++;

			if (JR_Filled == eJRet)
			{
				META_ReqSave(true);
			}
		}
		else
		{
			BM_Free(nBuf);
		}
	}
	else
	{
		BM_Free(nBuf);
	}
}


void gc_Move_OS(uint16 nDstBN, uint16 nDstWL)
{
	bool bRun = true;
	GcInfo stGI;

	stGI.nDstBN = nDstBN;
	stGI.nDstWL = nDstWL;
	stGI.nSrcBN = FF16;
	stGI.nSrcWL = FF16;
	stGI.nPgmRun = 0;
	stGI.nReadRun = 0;

	CmdInfo* apReadRun[MAX_GC_READ];
	MEMSET_ARRAY(apReadRun, 0x0);

	while (bRun)
	{
		////////////// Process done command. ///////////////
		CmdInfo* pDone;
		while (nullptr != (pDone = IO_PopDone(IOCB_Mig)))
		{
			if (NC_PGM == pDone->eCmd)
			{
				gc_HandlePgm(pDone);
				stGI.nPgmRun--;
			}
			else
			{
				gc_HandleRead(pDone, &stGI);
				stGI.nReadRun--;
			}
			IO_Free(pDone);
		}

		////////// Issue New command. //////////////////
		if (NUM_WL <= stGI.nDstWL) // Check end condition.
		{
			if ((0 == stGI.nPgmRun) && (0 == stGI.nReadRun))
			{
				PRINTF("[GC] Dst fill: %X\n", stGI.nDstBN);
				if (FF16 != stGI.nSrcBN)
				{
					META_SetBlkState(stGI.nSrcBN, BS_Closed);
				}
				bRun = false;
			}
		}
		// Issue more!.
		else if (stGI.nReadRun < MAX_GC_READ)
		{
			if ((FF16 == stGI.nSrcBN) || (FF16 == stGI.nSrcWL)) // No valid victim.
			{
				META_GetMinVPC(&stGI.nSrcBN);
				PRINTF("[GC] New Victim: %X\n", stGI.nSrcBN);
				META_SetBlkState(stGI.nSrcBN, BS_Victim);
				stGI.nSrcWL = 0;
			}
			else if (stGI.nSrcWL >= NUM_WL) // Source End.
			{
				if ((0 == stGI.nPgmRun) && (0 == stGI.nReadRun))
				{
					META_SetBlkState(stGI.nSrcBN, BS_Closed);
					PRINTF("[GC] Close victim: %X\n", stGI.nSrcBN);
					if (gc_ScanFree() > 0)
					{
						OS_SyncEvt(BIT(EVT_NEW_BLK));
					}
					stGI.nSrcBN = FF16;
					stGI.nSrcWL = FF16;
				}
			}
			else
			{
				uint16 nBuf4Copy = BM_Alloc();
				CmdInfo* pCmd = IO_Alloc(IOCB_Mig);
				IO_Read(pCmd, stGI.nSrcBN, stGI.nSrcWL, nBuf4Copy, 0);
				stGI.nSrcWL++;
				stGI.nReadRun++;
			}
		}

		if ((stGI.nReadRun > 0) || (stGI.nPgmRun > 0))
		{
			OS_Wait(BIT(EVT_NAND_CMD), LONG_TIME);
		}
	}
}

uint8 gc_ScanFree()
{
	uint16 nFree;
	while (gstFreePool.Count() < SIZE_FREE_POOL)
	{
		BlkInfo* pstBI = META_GetFree(&nFree, true);
		if (nullptr != pstBI)
		{
			gstFreePool.PushTail(nFree);
			pstBI->eState = BS_InFree;
		}
		else
		{
			break;
		}
	}
	return gstFreePool.Count();
}

void gc_Run(void* pParam)
{
	while (false == META_Ready())
	{
		OS_Wait(BIT(EVT_OPEN), LONG_TIME);
	}

	OpenBlk* pOpen = META_GetOpen(OPEN_GC);
	// Prev open block case.
	if (pOpen->stNextVA.nWL < NUM_WL)
	{
		gc_ScanFree();
		gc_Move_OS(pOpen->stNextVA.nBN, pOpen->stNextVA.nWL);
	}

	while (true)
	{
		uint8 nFree = gstFreePool.Count();
		if (nFree < SIZE_FREE_POOL)
		{
			nFree = gc_ScanFree();
			if (nFree > 0)
			{
				OS_SyncEvt(BIT(EVT_NEW_BLK));
			}
		}

		if (nFree < SIZE_FREE_POOL)
		{
			uint16 nFree = gstFreePool.PopHead();
			ASSERT(FF16 != nFree);
			GC_BlkErase_OS(OPEN_GC, nFree);
			META_SetOpen(OPEN_GC, nFree);
			gc_Move_OS(nFree, 0);
			META_SetBlkState(nFree, BS_Closed);
			OS_SyncEvt(BIT(EVT_NEW_BLK));
		}
		if (nFree >= SIZE_FREE_POOL)
		{
			OS_Wait(BIT(EVT_BLK_REQ), LONG_TIME);
		}
	}
}

uint16 GC_ReqFree_Blocking(OpenType eType)
{
	if(gstFreePool.Count() <= GC_TRIG_BLK_CNT)
	{
		OS_SyncEvt(BIT(EVT_BLK_REQ));
	}
	while (gstFreePool.Count() <= 1)
	{
		OS_Wait(BIT(EVT_NEW_BLK), LONG_TIME);
	}

	uint16 nBN = gstFreePool.PopHead();

	PRINTF("[GC:%X] Alloc %X (free: %d)\n", SIM_GetSeqNo(),  nBN, gstFreePool.Count());
	return nBN;
}


void GC_BlkErase_OS(OpenType eOpen, uint16 nBN)
{
	CbKey eCbKey = eOpen == OPEN_GC ? CbKey::IOCB_Mig : CbKey::IOCB_UErs;


	// Erase block.
	CmdInfo* pCmd = IO_Alloc(eCbKey);
	PRINTF("[GC:%X] ERB: %X by %s\n", pCmd->nDbgSN, nBN, eOpen == OPEN_GC ? "GC" : "User");
	IO_Erase(pCmd, nBN, FF32);
	CmdInfo* pDone;
	while (true)
	{
		pDone = IO_PopDone(eCbKey);
		if (nullptr != pDone)
		{
			break;
		}
		OS_Wait(BIT(EVT_NAND_CMD), LONG_TIME);
	}
	ASSERT(pCmd == pDone);
	IO_Free(pDone);

	// Add Journal.
	JnlRet eJRet;
	while (true)
	{
		eJRet = META_AddErbJnl(eOpen, nBN);
		if (JR_Busy != eJRet)
		{
			break;
		}
		OS_Wait(BIT(EVT_META), LONG_TIME);
	}

	// Meta save.
	META_ReqSave(true);
}

void GC_Init()
{
	gstFreePool.Init();
	OS_CreateTask(gc_Run, nullptr, nullptr, "gc");
}

