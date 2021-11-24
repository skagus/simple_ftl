
#include "sim.h"
#include "scheduler.h"
#include "buf.h"
#include "nfc.h"
#include "io.h"
#include "log_gc.h"
#include "log_meta.h"

#define PRINTF			//	SIM_Print

void migrate(LogMap* pVictim)
{
	BlkMap* pVictimMap = META_GetBlkMap(pVictim->nLBN);
	uint16 nOrgBN = pVictimMap->nPBN;
	uint16 nLogBN = pVictim->nPBN;
	uint16 nBuf4Copy = BM_Alloc();
	uint16 nDstBN = META_GetFreePBN();
	uint32* pSpare = (uint32*)BM_GetSpare(nBuf4Copy);
	uint32* pMain = (uint32*)BM_GetMain(nBuf4Copy);
	CmdInfo* pCmd = IO_Alloc(IOCB_Mig);
	IO_Erase(pCmd, nDstBN, 0);	// Erase before program.
	IO_WaitDone(pCmd);
	IO_Free(pCmd);
	for (uint16 nPO = 0; nPO < NUM_WL; nPO++)
	{
		pCmd = IO_Alloc(IOCB_Mig);
		if (0xFFFFFFFF != pVictim->anMap[nPO])
		{
			IO_Read(pCmd, nLogBN, pVictim->anMap[nPO], nBuf4Copy, 0);
			IO_WaitDone(pCmd);
			IO_Free(pCmd);
		}
		else
		{
			IO_Read(pCmd, nOrgBN, nPO, nBuf4Copy, 0);
			IO_WaitDone(pCmd);
			IO_Free(pCmd);
		}
		PRINTF("Mig: %X\n", *pSpare);
		if ((*pSpare & 0xF) == nPO)
		{
			assert((*pMain & 0xF) == nPO);
		}
		assert(*pSpare == *(uint32*)BM_GetMain(nBuf4Copy));
		pCmd = IO_Alloc(IOCB_Mig);
		IO_Program(pCmd, nDstBN, nPO, nBuf4Copy, 0);
		IO_WaitDone(pCmd);
		IO_Free(pCmd);
	}
	META_SetFreePBN(pVictimMap->nPBN);
	pVictimMap->bLog = 0;
	pVictimMap->nPBN = nDstBN;
	BM_Free(nBuf4Copy);
	//	META_Save();
}


LogMap* getVictim()
{
	for (uint16 nIdx = 0; nIdx < NUM_LOG_BLK; nIdx++)
	{
		LogMap* pLogMap = META_GetLogMap(nIdx);
		if (pLogMap->nLBN == 0xFFFF)
		{
			return pLogMap;
		}
	}
	// free가 없으면, random victim.
	return META_GetLogMap(SIM_GetRand(NUM_LOG_BLK));
}

uint16 gnNewLBN;

LogMap* GC_MakeNewLog(uint16 nLBN, LogMap* pSrcLog)
{
	if (nullptr == pSrcLog)
	{
		pSrcLog = getVictim();
	}
	if (pSrcLog->nCPO > 0)
	{
		// Migrate.
		migrate(pSrcLog);
	}
	memset(pSrcLog->anMap, 0xFF, sizeof(pSrcLog->anMap));
	META_GetBlkMap(nLBN)->bLog = 1;
	pSrcLog->nLBN = nLBN;
	pSrcLog->nCPO = 0;
	CmdInfo* pCmd = IO_Alloc(IOCB_Mig);
	IO_Erase(pCmd, pSrcLog->nPBN, 0);
	IO_WaitDone(pCmd);
	IO_Free(pCmd);
	META_Save();
	return pSrcLog;
}

struct GcMoveCtx
{
	uint16 nDst;	// Input.
	LogMap* pSrcLog;	// Log map.
	uint16 nSrcBlk;	// Block map PBN.
	uint16 nCleanWL;
	uint16 nReadIssue;
	uint16 nPgmDone;
};

bool gc_Move(GcMoveCtx* pCtx, bool b1st)
{
	bool bRet = false;

	if (b1st)
	{
		pCtx->nCleanWL = NUM_WL;
		pCtx->nReadIssue = 0;
		pCtx->nPgmDone = 0;
		PRINTF("[GC] Start Move %X + %X -> %X\n", 
			pCtx->pSrcLog->nPBN, pCtx->nSrcBlk, pCtx->nDst);
	}

	CmdInfo* pDone;
	while (pDone = IO_GetDone(IOCB_Mig))
	{
		if (NC_PGM == pDone->eCmd)
		{
			pCtx->nPgmDone++;
			uint16 nBuf = pDone->stPgm.anBufId[0];
			uint32* pSpare = (uint32*)BM_GetSpare(nBuf);
			PRINTF("[GC] PGM Done to {%X, %X}, LPN:%X\n",
				pDone->anBBN[0], pDone->nWL, *pSpare);
			if ((*pSpare & 0xF) == pDone->nTag)
			{
				uint32* pMain = (uint32*)BM_GetMain(nBuf);
				assert((*pMain & 0xF) == pDone->nTag);
			}
			BM_Free(nBuf);
		}
		else // Read done --> Pgm.
		{
			uint16 nBuf = pDone->stRead.anBufId[0];
			uint32* pSpare = (uint32*)BM_GetSpare(nBuf);
			uint16 nLPO = pDone->nTag;
			PRINTF("[GC] Read Done from {%X, %X}, LPN:%X\n",
				pDone->anBBN[0], pDone->nWL, *pSpare);
			CmdInfo* pNewPgm = IO_Alloc(IOCB_Mig);
			IO_Program(pNewPgm, pCtx->nDst, nLPO, nBuf, nLPO);
		}
		IO_Free(pDone);
	}

	if ((pCtx->nReadIssue < NUM_WL)
		&& (pCtx->nReadIssue <= pCtx->nPgmDone + 2))
	{
		LogMap* pVictim = pCtx->pSrcLog;
		uint16 nLPO = pCtx->nReadIssue;
		uint16 nBuf4Copy = BM_Alloc();
		CmdInfo* pCmd = IO_Alloc(IOCB_Mig);
		if (0xFFFFFFFF != pVictim->anMap[nLPO])
		{
			IO_Read(pCmd, pVictim->nPBN, pVictim->anMap[nLPO], nBuf4Copy, nLPO);
		}
		else
		{
			IO_Read(pCmd, pCtx->nSrcBlk, nLPO, nBuf4Copy, nLPO);
		}
		pCtx->nReadIssue++;
	}
	else if (NUM_WL == pCtx->nPgmDone)
	{
		PRINTF("[GC] Move done\n");
		bRet = true;
	}

	if (pCtx->nReadIssue != pCtx->nPgmDone)
	{
		Sched_Wait(BIT(EVT_NAND_CMD), LONG_TIME);
	}
	return bRet;
}

struct GcErsCtx
{
	bool bIssued;
	uint16 nBN;
};

bool gc_Erase(GcErsCtx* pCtx, bool b1st)
{
	bool bRet = false;
	if (b1st)
	{
		pCtx->bIssued = false;
	}
	if (false == pCtx->bIssued)
	{
		CmdInfo* pCmd = IO_Alloc(IOCB_Mig);
		IO_Erase(pCmd, pCtx->nBN, 0);
		pCtx->bIssued = true;
		Sched_Wait(BIT(EVT_NAND_CMD), LONG_TIME);
	}
	else
	{
		CmdInfo* pCmd = IO_GetDone(IOCB_Mig);
		if (nullptr != pCmd)
		{
			PRINTF("[GC] ERB Done %X\n", pCtx->nBN);
			assert(pCmd->anBBN[0] == pCtx->nBN);
			IO_Free(pCmd);
			bRet = true;
		}
		else
		{
			Sched_Wait(BIT(EVT_NAND_CMD), LONG_TIME);
		}
	}
	return bRet;
}


/**
* GC move는 
* Victim LBN의 Log와 main을 Free로 merge한 후, 
* Free --> main blk.
* Log PBN --> Free.
* Main --> New Log PBN.
*/
enum GcState
{
	GS_WaitReq,
	GS_ErsDst,
	GS_Move,
	GS_MetaSave,
	GS_ErsNewLog,
};

struct GcCtx
{
	GcState eState;
	uint16 nReqLBN;		///< Requested BN.
	LogMap* pSrcLog;	///< Orignally dirty.
	BlkMap* pSrcBM;		///< 
	uint16 nDstPBN;		///< Orignally Free block.
	uint32 nMtAge;		///< Meta저장 확인용 Age값.
	uint32 nSeqNo;
};


void gc_Run(void* pParam)
{
	GcCtx* pCtx = (GcCtx*)pParam;

	switch (pCtx->eState)
	{
		case GS_WaitReq:
		{
			if (0xFFFF != gnNewLBN)
			{
				pCtx->nSeqNo = SIM_GetSeqNo();
				GcErsCtx* pChild = (GcErsCtx*)(pCtx + 1);
				pCtx->nReqLBN = gnNewLBN;
				gnNewLBN = 0xFFFF;
				pCtx->nDstPBN = META_GetFreePBN();
				pCtx->pSrcLog = META_SearchLogMap(pCtx->nReqLBN);
				if (nullptr == pCtx->pSrcLog)
				{
					pCtx->pSrcLog = getVictim();
				}
				if (0xFFFF == pCtx->pSrcLog->nLBN)  // Available Log block.
				{
					LogMap* pNewLog = pCtx->pSrcLog;
					pNewLog->nLBN = pCtx->nReqLBN;		// 
					pNewLog->nCPO = 0;
					MEMSET_ARRAY(pNewLog->anMap, 0xFF);
					BlkMap* pReqBM = META_GetBlkMap(pCtx->nReqLBN);
					pReqBM->bLog = 1;
					PRINTF("[GC] Free Log : %X\n", pNewLog->nPBN);

					pCtx->nMtAge = META_GetAge();
					pCtx->eState = GS_MetaSave;
					META_ReqSave();
					Sched_Wait(BIT(EVT_META), LONG_TIME);
				}
				else
				{
					pCtx->pSrcBM = META_GetBlkMap(pCtx->pSrcLog->nLBN);
					pChild->nBN = pCtx->nDstPBN;
					gc_Erase(pChild, true);	// Erase dest blk.
					pCtx->eState = GS_ErsDst;
				}
			}
			else
			{
				Sched_Wait(BIT(EVT_BLOCK), LONG_TIME);
			}
			break;
		}
		case GS_ErsDst:
		{
			GcErsCtx* pChild = (GcErsCtx*)(pCtx + 1);
			if (gc_Erase(pChild, false)) // End.
			{
				GcMoveCtx* pMoveCtx = (GcMoveCtx*)(pCtx + 1);
				pMoveCtx->nDst = pCtx->nDstPBN;
				pMoveCtx->pSrcLog = pCtx->pSrcLog;
				pMoveCtx->nSrcBlk = pCtx->pSrcBM->nPBN;
				if (0xFFFF != pMoveCtx->pSrcLog->nLBN)
				{
					gc_Move(pMoveCtx, true);
					pCtx->eState = GS_Move;
				}
				else
				{
					GcErsCtx* pChild = (GcErsCtx*)(pCtx + 1);
					pChild->nBN = pCtx->pSrcLog->nPBN;
					gc_Erase(pChild, true);
					pCtx->eState = GS_ErsNewLog;
				}
			}
			break;
		}
		case GS_Move:
		{
			GcMoveCtx* pMoveCtx = (GcMoveCtx*)(pCtx + 1);
			if (gc_Move(pMoveCtx, false))
			{
				BlkMap* pSrcBM = pCtx->pSrcBM;
				LogMap* pNewLog = pCtx->pSrcLog;
				META_SetFreePBN(pNewLog->nPBN);		// Log to Free.
				pNewLog->bReady = false;
				pNewLog->nLBN = pCtx->nReqLBN;		// 
				pNewLog->nPBN = pSrcBM->nPBN;			// BM to Log.
				pNewLog->nCPO = 0;
				MEMSET_ARRAY(pNewLog->anMap, 0xFF);
				pSrcBM->bLog = 0;
				pSrcBM->nPBN = pCtx->nDstPBN;				// Free to BM.
				BlkMap* pReqBM = META_GetBlkMap(pCtx->nReqLBN);
				pReqBM->bLog = 1;
				
				pCtx->nMtAge = META_GetAge();
				pCtx->eState = GS_MetaSave;
				META_ReqSave();
				Sched_Wait(BIT(EVT_META), LONG_TIME);
			}
			break;
		}
		case GS_MetaSave:
		{
			if (pCtx->nMtAge < META_GetAge())
			{
				PRINTF("[GC] Mt Save done: %X\n", pCtx->nMtAge);
				GcErsCtx* pChild = (GcErsCtx*)(pCtx + 1);
				pChild->nBN = pCtx->pSrcLog->nPBN;
				gc_Erase(pChild, true);
				pCtx->eState = GS_ErsNewLog;
			}
			else
			{
				Sched_Wait(BIT(EVT_META), LONG_TIME);
			}
			break;
		}
		case GS_ErsNewLog:
		{
			GcErsCtx* pChild = (GcErsCtx*)(pCtx + 1);
			if (gc_Erase(pChild, false)) // End.
			{
				LogMap* pNewLog = pCtx->pSrcLog;
				pNewLog->bReady = true;
				pCtx->eState = GS_WaitReq;
				Sched_TrigSyncEvt(BIT(EVT_BLOCK));
				Sched_Yield();
			}
		}
	}
}

void GC_ReqLog(uint16 nLBN)
{
	PRINTF("[GC] Request Log Blk: %X\n", nLBN);
	gnNewLBN = nLBN;
	Sched_TrigSyncEvt(BIT(EVT_BLOCK));
	Sched_Wait(BIT(EVT_BLOCK), LONG_TIME);
}

static uint8 anContext[4096];		///< Stack like meta context.

void GC_Init()
{
	gnNewLBN = 0xFFFF;
	GcCtx* pCtx = (GcCtx*)anContext;
	MEMSET_PTR(pCtx, 0);
	Sched_Register(TID_GC, gc_Run, anContext, BIT(MODE_NORMAL));
}
