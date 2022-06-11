
#include "templ.h"
#include "cpu.h"
#include "buf.h"
#include "os.h"
#include "io.h"
#include "page_gc.h"
#include "page_meta.h"

#define PRINTF			SIM_Print


/**
* GC move는 
*/


struct GcStk
{
	enum GcState
	{
		WaitOpen,
		WaitReq,
		GetDst,	
		ErsDst,
		Move,
		Stop,	// Stop on shutdown.
	};
	GcState eState;
};

bool gbStop;
Queue<uint16, SIZE_FREE_POOL> gstFreePool;


#define MAX_GC_READ		(2)

struct MoveStk
{
	enum MoveState
	{
		MS_Init,
		MS_Run,
	};
	MoveState eState;

	uint16 nDstBN;	// Input.
	uint16 nDstWL;

	uint16 nSrcBN;	// Block map PBN.
	uint16 nSrcWL;

	uint8 nReadRun;
	uint8 nPgmRun;
	uint16 nDataRead; // Total data read: to check read end.

	uint8 nRdSlot;
	CmdInfo* apReadRun[MAX_GC_READ];
};


uint8 gc_ScanFree();


struct GcInfo
{
	uint16 nDstBN;
	uint16 nDstWL;
	uint16 nSrcBN;
	uint16 nSrcWL;
	uint8 nPgmRun;
	uint8 nReadRun;
};

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
			JnlRet eJRet;
			while (JnlRet::JR_Busy != (eJRet = META_Update(*pSpare, stAddr, OPEN_GC)))
			{
				OS_Wait(BIT(EVT_META), LONG_TIME);
			}
			CmdInfo* pNewPgm = IO_Alloc(IOCB_Mig);
			IO_Program(pNewPgm, pGI->nDstBN, pGI->nDstWL, nBuf, *pSpare);
			// User Data.
			if ((*pSpare & 0xF) == pDone->nTag)
			{
				uint32* pMain = (uint32*)BM_GetMain(nBuf);
				assert((*pMain & 0xF) == pDone->nTag);
			}
			PRINTF("[GCW] {%X, %X}, LPN:%X\n", pGI->nDstBN, pGI->nDstWL, *pSpare);
			pGI->nDstWL++;
			pGI->nPgmRun++;
			if (JR_Filled == eJRet)
			{
				uint32 nAge = META_ReqSave();
				while (META_GetAge() <= nAge)
				{
					OS_Wait(BIT(EVT_META), LONG_TIME);
				}
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

extern void dbg_MapIntegrity();


void gc_Move_OS(uint16 nDstBN, uint16 nDstWL)
{
	bool bRun = true;
	GcInfo stGI;

	stGI.nDstBN = nDstBN;
	stGI.nDstWL = nDstWL;
	stGI.nSrcBN = FF16;
	stGI.nSrcWL = FF16;

	CmdInfo* apReadRun[MAX_GC_READ];
	MEMSET_ARRAY(apReadRun, 0x0);

	while (bRun)
	{
		////////////// Process done command. ///////////////
		CmdInfo* pDone;
		while (pDone = IO_PopDone(IOCB_Mig))
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
		if (NUM_WL == nDstWL)
		{
			if (0 == stGI.nPgmRun)
			{
				PRINTF("[GC] Dst fill: %X\n", stGI.nDstBN);
				if (FF16 != stGI.nSrcBN)
				{
					META_SetBlkState(stGI.nSrcBN, BS_Closed);
				}
				bRun = false;
			}
		}
		else if ((stGI.nReadRun < MAX_GC_READ) && (stGI.nSrcWL < NUM_DATA_PAGE))
		{
			if (FF16 == stGI.nSrcBN)
			{
				if (0 == stGI.nReadRun)
				{
					META_GetMinVPC(&stGI.nSrcBN);
					PRINTF("[GC] New Victim: %X\n", stGI.nSrcBN);
					META_SetBlkState(stGI.nSrcBN, BS_Victim);
					stGI.nSrcWL = 0;
				}
			}
			else
			{
				if(stGI.nSrcWL <= NUM_WL)
				{
					uint16 nBuf4Copy = BM_Alloc();
					CmdInfo* pCmd = IO_Alloc(IOCB_Mig);
					PRINTF("[GC] RD:{%X,%X}\n", stGI.nSrcBN, stGI.nSrcWL);
					IO_Read(pCmd, stGI.nSrcBN, stGI.nSrcWL, nBuf4Copy, 0);
					stGI.nSrcWL++;
					stGI.nReadRun++;
				}
				else if ((0 == stGI.nReadRun) && (0 == stGI.nPgmRun))
				{// After all program done related to read.(SPO safe)
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

STOP:
	while (true)
	{
		while (true)
		{
			CmdInfo* pDone = IO_PopDone(IOCB_Mig);
			if (nullptr == pDone)
			{
				break;
			}
			if (NC_PGM == pDone->eCmd)
			{
				BM_Free(pDone->stPgm.anBufId[0]);
			}
			else if (NC_READ == pDone->eCmd)
			{
				BM_Free(pDone->stRead.anBufId[0]);
			}
			IO_Free(pDone);
		}
		OS_Wait(BIT(EVT_NAND_CMD), LONG_TIME);
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

	PRINTF("[GC] Alloc %X (free: %d)\n", nBN, gstFreePool.Count());
	return nBN;
}


void GC_BlkErase_OS(OpenType eOpen, uint16 nBN)
{
	CbKey eCbKey = eOpen == OPEN_GC ? CbKey::IOCB_Mig : CbKey::IOCB_UErs;

	PRINTF("[GC] ERB: %X by %s\n", nBN, eOpen == OPEN_GC ? "GC" : "User");

	// Erase block.
	CmdInfo* pCmd = IO_Alloc(eCbKey);
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
	assert(pCmd == pDone);
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
	uint32 nAge = META_ReqSave();
	while (META_GetAge() <= nAge)
	{
		OS_Wait(BIT(EVT_META), LONG_TIME);
	}
}


void GC_Stop()
{
	gbStop = true;
}

void GC_Init()
{
	gstFreePool.Init();
	OS_CreateTask(gc_Run, nullptr, nullptr, 0xFF);
}

