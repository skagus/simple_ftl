
#include "cpu.h"
#include "scheduler.h"
#include "buf.h"
#include "nfc.h"
#include "io.h"
#include "log_ftl.h"
#include "log_gc.h"
#include "log_meta.h"

#define PRINTF			//	SIM_Print
uint16 gnNewLBN;

inline void gc_SetFreePBN(uint16 nPBN)
{
	gstMeta.nFreePBN = nPBN;
}

inline LogMap* gc_GetLogMap(uint16 nIdx)
{
	return gstMeta.astLog + nIdx;
}

inline uint16 gc_GetFreePBN()
{
	return gstMeta.nFreePBN;
}

LogMap* gc_GetVictim()
{
	for (uint16 nIdx = 0; nIdx < NUM_LOG_BLK; nIdx++)
	{
		LogMap* pLogMap = gc_GetLogMap(nIdx);
		if (pLogMap->nLBN == INV_BN)
		{
			PRINTF("[GC] Victim: %X\n", nIdx);
			return pLogMap;
		}
	}
	// free가 없으면, random victim.
	return META_GetOldLog();
//	return gc_GetLogMap(SIM_GetRand(NUM_LOG_BLK));
}

// ////// Move sub state machine ////////////////
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
		if (INV_PPO != pVictim->anMap[nLPO])
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


//-----------------------------------------
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
			if (INV_BN != gnNewLBN)
			{
				LogMap* pSrc;
				pCtx->nSeqNo = SIM_GetSeqNo();
				GcErsCtx* pChild = (GcErsCtx*)(pCtx + 1);
				pCtx->nReqLBN = gnNewLBN;
				gnNewLBN = INV_BN;
				pCtx->nDstPBN = gc_GetFreePBN();
				pSrc = META_FindLog(pCtx->nReqLBN, false);
				if (nullptr == pSrc)
				{
					pSrc = gc_GetVictim();
				}
				pCtx->pSrcLog = pSrc;

				if (INV_BN == pSrc->nLBN)  // Available Log block.
				{
					pSrc->nLBN = pCtx->nReqLBN;		// 
					pSrc->nCPO = 0;
					pSrc->bInPlace = true;

					MEMSET_ARRAY(pSrc->anMap, 0xFF);
					BlkMap* pReqBM = META_GetBlkMap(pCtx->nReqLBN);
					pReqBM->bLog = 1;
					PRINTF("[GC] Free Log : %X\n", pSrc->nPBN);

					pCtx->nMtAge = META_GetAge();
					pCtx->eState = GS_MetaSave;
					META_ReqSave();
					Sched_Wait(BIT(EVT_META), LONG_TIME);
				}
				else if ((NUM_WL == pSrc->nCPO) && pSrc->bInPlace)	// Swap case.
				{
					BlkMap* pBM = META_GetBlkMap(pSrc->nLBN);
					uint16 nOrg = pBM->nPBN;
					pBM->nPBN = pSrc->nPBN;
					pBM->bLog = 0;
					pSrc->nPBN = nOrg;
					PRINTF("[GC] Inplace PBN : %X\n", pBM->nPBN);

					pSrc->bReady = false;
					pSrc->nLBN = pCtx->nReqLBN;		// 
					pSrc->nCPO = 0;
					pSrc->bInPlace = true;
					MEMSET_ARRAY(pSrc->anMap, 0xFF);
					BlkMap* pReqBM = META_GetBlkMap(pCtx->nReqLBN);
					pReqBM->bLog = 1;

					pCtx->nMtAge = META_GetAge();
					pCtx->eState = GS_MetaSave;
					META_ReqSave();
					Sched_Wait(BIT(EVT_META), LONG_TIME);
				}
				else
				{
					pCtx->pSrcBM = META_GetBlkMap(pSrc->nLBN);
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
				LogMap* pSrc = pCtx->pSrcLog;
				pMoveCtx->nDst = pCtx->nDstPBN;
				pMoveCtx->pSrcLog = pSrc;
				pMoveCtx->nSrcBlk = pCtx->pSrcBM->nPBN;
				if (INV_BN != pSrc->nLBN)
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
				gc_SetFreePBN(pNewLog->nPBN);		// Log to Free.
				pNewLog->bReady = false;
				pNewLog->nLBN = pCtx->nReqLBN;		// 
				pNewLog->nPBN = pSrcBM->nPBN;			// BM to Log.
				pNewLog->nCPO = 0;
				pNewLog->bInPlace = true;
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
	gnNewLBN = INV_BN;
	GcCtx* pCtx = (GcCtx*)anContext;
	MEMSET_PTR(pCtx, 0);
	Sched_Register(TID_GC, gc_Run, anContext, BIT(MODE_NORMAL));
}
