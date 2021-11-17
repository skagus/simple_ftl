
#include "types.h"
#include "config.h"
#include "scheduler.h"
#include "nfc.h"
#include "ftl.h"
#include "buf.h"
#include "io.h"
#include "map_log.h"
#include "meta_manager.h"

#define PRINTF				SIM_Print

struct Meta
{
	BlkMap astMap[NUM_USER_BLK];
	LogMap astLog[NUM_LOG_BLK];
	uint16 nFreePBN;
};
static_assert(sizeof(Meta) <= BYTE_PER_CHUNK);

struct MetaCtx
{
	uint16 nCurBN;
	uint16 nNextWL;
	uint32 nAge;
};

Meta gstMeta;
MetaCtx gstMetaCtx;

enum BootStep
{
	Boot_Init,
	Boot_Open,		///< In openning.
	Boot_Format,	///< In formatting.
	Boot_Done,
};
struct BootCtx
{
	BootStep eStep;
};
static uint8 anContext[4096];		///< Stack like meta context.
BootCtx* gpBootCtx;


enum FormatStep
{
	FMT_BlkMap,
	FMT_LogMap,
	FMT_Done,
};

struct FormatCtx
{
	FormatStep eStep;
	uint16 nBN;
};

BootStep meta_Format(FormatCtx* pFmtCtx)
{
	BootStep eRet;
	switch (pFmtCtx->eStep)
	{
		case FMT_BlkMap:
		{
			uint16 nBN = NUM_META_BLK;
			for (uint16 nIdx = 0; nIdx < NUM_USER_BLK; nIdx++)
			{
				gstMeta.astMap[nIdx].nPBN = nBN;
				gstMeta.astMap[nIdx].bLog = 0;
				nBN++;
			}
			pFmtCtx->eStep = FMT_LogMap;
			pFmtCtx->nBN = nBN;
			Sched_Wait(0, 1);
			eRet = Boot_Format;
			break;
		}

		case FMT_LogMap:
		{
			uint16 nBN = pFmtCtx->nBN;
			for (uint16 nIdx = 0; nIdx < NUM_LOG_BLK; nIdx++)
			{
				gstMeta.astLog[nIdx].nLBN = 0xFFFF;
				gstMeta.astLog[nIdx].nPBN = nBN;
				nBN++;
			}
			gstMeta.nFreePBN = nBN;
			gstMetaCtx.nAge = NUM_WL;	/// Not tobe zero.

			pFmtCtx->eStep = FMT_Done;
			eRet = Boot_Done;
			break;
		}
		default:
		{
			assert(false);
		}
	}
	return eRet;
}


bool META_Ready()
{
	return gpBootCtx->eStep == Boot_Done;
}


void META_SetFreePBN(uint16 nPBN)
{
	gstMeta.nFreePBN = nPBN;
}

uint16 META_GetFreePBN()
{
	return gstMeta.nFreePBN;
}

LogMap* META_GetLogMap(uint16 nIdx)
{
	return gstMeta.astLog + nIdx;
}

BlkMap* META_GetBlkMap(uint16 nLBN)
{
	return gstMeta.astMap + nLBN;
}

LogMap* META_SearchLogMap(uint16 nLBN)
{
	if (gstMeta.astMap[nLBN].bLog)
	{
		for (uint16 nIdx = 0; nIdx < NUM_LOG_BLK; nIdx++)
		{
			if (gstMeta.astLog[nIdx].nLBN == nLBN)
			{
				return gstMeta.astLog + nIdx;
			}
		}
	}
	return nullptr;
}


void META_Save()
{
	PRINTF("MetaSave\n");
	CmdInfo* pCmd;
	////// Save Meta data. ////////
	if (0 == gstMetaCtx.nNextWL)
	{
		pCmd = IO_Alloc(IOCB_Meta);
		IO_Erase(pCmd, gstMetaCtx.nCurBN, 0);
		IO_WaitDone(pCmd);
		IO_Free(pCmd);
	}
	uint16 nBuf = BM_Alloc();
	*(uint32*)BM_GetSpare(nBuf) = gstMetaCtx.nAge;
	uint8* pMain = BM_GetMain(nBuf);
	memcpy(pMain, &gstMeta, sizeof(gstMeta));
	pCmd = IO_Alloc(IOCB_Meta);
	IO_Program(pCmd, gstMetaCtx.nCurBN, gstMetaCtx.nNextWL, nBuf, 0);
	IO_WaitDone(pCmd);
	IO_Free(pCmd);
	BM_Free(nBuf);

	/////// Setup Next Address ///////////
	gstMetaCtx.nAge++;
	gstMetaCtx.nNextWL++;
	if (gstMetaCtx.nNextWL >= NUM_WL)
	{
		gstMetaCtx.nNextWL = 0;
		gstMetaCtx.nCurBN++;
		if (gstMetaCtx.nCurBN >= NUM_META_BLK)
		{
			gstMetaCtx.nCurBN = 0;
		}
	}
}

struct DataScanCtx
{
	uint8 nLogIdx;
	bool bStop;
	uint16 nIssue;
	uint16 nDone;
};

bool ftl_Scan(DataScanCtx* pCtx)
{
	for (uint32 nLogIdx = 0; nLogIdx < NUM_LOG_BLK; nLogIdx++)
	{
		LogMap* pLMap = gstMeta.astLog + nLogIdx;
		uint16 nCPO = pLMap->nCPO;
		uint16 nBuf = BM_Alloc();
		uint32* pnSpare = (uint32*)BM_GetSpare(nBuf);
		uint8* pMain = BM_GetMain(nBuf);
		uint16 nPO;
		CmdInfo* pCmd;
		PRINTF("Log Scan from (%X,%X) \n", pLMap->nPBN, pLMap->nCPO);
		for (nPO = nCPO; nPO < NUM_WL; nPO++)
		{
			pCmd = IO_Alloc(IOCB_Meta);
			IO_Read(pCmd, pLMap->nPBN, nPO, nBuf, 0);
			IO_WaitDone(pCmd);
			IO_Free(pCmd);
			if (0xFFFFFFFF == *pnSpare)
			{
				break;
			}
			else
			{
				uint32 nLPO = *pnSpare % CHUNK_PER_PBLK;
				PRINTF("MapUpdate: %X (%X, %X)\n", *pnSpare, pLMap->nPBN, nPO);
				pLMap->anMap[nLPO] = nPO;
			}
		}
		BM_Free(nBuf);
		pLMap->nCPO = nPO;
	}
	return true;
}

enum OpenStep
{
	Open_Init,
	Open_BlkScan,
	Open_PageScan,
	Open_DataScan,
};

struct OpenCtx
{
	OpenStep eOpenStep;
	uint32 nMaxAge;
	uint16 nCPO;
	uint16 nMaxBN;
	uint16 nIssued;
	uint16 nDone;
};

BootStep meta_Open(OpenCtx* pCtx)
{
	BootStep eRet = Boot_Open;
	switch (pCtx->eOpenStep)
	{
		case Open_Init:
		{
			pCtx->nMaxAge = 0;
			pCtx->nMaxBN = 0xFFFF;
			pCtx->nIssued = 0;
			pCtx->nDone = 0;
			pCtx->eOpenStep = Open_BlkScan;
			Sched_Wait(0, 1);
			break;
		}
		case Open_BlkScan:
		{
			// Issue phase.
			if ((pCtx->nIssued < NUM_META_BLK) 
				&& ((pCtx->nIssued - pCtx->nDone) < 2 ))
			{
				uint16 nBuf = BM_Alloc();
				CmdInfo* pCmd;
				pCmd = IO_Alloc(IOCB_Meta);
				IO_Read(pCmd, pCtx->nIssued, 0, nBuf, pCtx->nIssued);
				PRINTF("[OPEN] BlkScan Issue %X\n", pCtx->nIssued);
				pCtx->nIssued++;
				Sched_Wait(BIT(EVT_NAND_CMD), 100);
			}
			// Check phase.
			CmdInfo* pDone = IO_GetDone(CbKey::IOCB_Meta);
			if (nullptr != pDone)
			{
				pCtx->nDone++;
				uint16 nBuf = pDone->stRead.anBufId[0];
				uint32* pnSpare = (uint32*)BM_GetSpare(nBuf);

				PRINTF("[OPEN] BlkScan Done %X\n", pDone->nTag);

				if ((*pnSpare > pCtx->nMaxAge) && (*pnSpare != 0xFFFFFFFF))
				{
					pCtx->nMaxAge = *pnSpare;
					pCtx->nMaxBN = pDone->nTag;
				}
				BM_Free(nBuf);
				IO_Free(pDone);

				if (NUM_META_BLK == pCtx->nDone)	// All done.
				{
					if (0xFFFF != pCtx->nMaxBN)
					{
						PRINTF("[OPEN] BlkScan Latest BN: %X\n", pCtx->nMaxBN);
						pCtx->eOpenStep = Open_PageScan;
						pCtx->nIssued = 0;
						pCtx->nDone = 0;
						pCtx->nCPO = 0;
						Sched_Wait(0, 1);	// Call again.
					}
					else
					{
						PRINTF("[OPEN] All erased --> Format\n");
						eRet = Boot_Format;
					}
				}
			}
			break;
		}
		case Open_PageScan:
		{
			if ((0 == pCtx->nCPO)
				&& (pCtx->nIssued < NUM_WL)
				&& ((pCtx->nIssued - pCtx->nDone) < 2))
			{
				uint16 nBuf = BM_Alloc();
				CmdInfo* pCmd = IO_Alloc(IOCB_Meta);
				IO_Read(pCmd, pCtx->nMaxBN, pCtx->nIssued, nBuf, pCtx->nIssued);
				PRINTF("[OPEN] PageScan Issue %X\n", pCtx->nIssued);
				pCtx->nIssued++;
				Sched_Wait(BIT(EVT_NAND_CMD), 100);
			}
			// Check phase.
			CmdInfo* pDone = IO_GetDone(CbKey::IOCB_Meta);
			if (nullptr != pDone)
			{
				pCtx->nDone++;
				uint16 nBuf = pDone->stRead.anBufId[0];
				uint32* pnSpare = (uint32*)BM_GetSpare(nBuf);
				if (0xFFFFFFFF != *pnSpare)
				{
					assert(0 == pCtx->nCPO);
					uint8* pMain = BM_GetMain(nBuf);
					gstMetaCtx.nAge = *pnSpare;
					memcpy(&gstMeta, pMain, sizeof(gstMeta));
				}
				else if(0 == pCtx->nCPO)
				{
					pCtx->nCPO = pDone->nTag;
				}
				BM_Free(nBuf);
				IO_Free(pDone);
				if ((pCtx->nDone == pCtx->nIssued)
					&& ((pCtx->nCPO > 0) || (pCtx->nDone >= NUM_WL)))
				{
					PRINTF("[OPEN] MetaPage CPO:%X\n", pCtx->nCPO);
					if (pCtx->nCPO < NUM_WL)
					{
						gstMetaCtx.nCurBN = pCtx->nMaxBN;
						gstMetaCtx.nNextWL = pCtx->nCPO;
					}
					else
					{
						gstMetaCtx.nNextWL = 0;
						gstMetaCtx.nCurBN = (pCtx->nMaxBN + 1) % NUM_META_BLK;
					}
					pCtx->eOpenStep = Open_DataScan;
					pCtx->nDone = 0;
					pCtx->nIssued = 0;
					pCtx->nCPO = 0;
					MEMSET_PTR((DataScanCtx*)(pCtx + 1), 0);
					Sched_Wait(0, 1);
				}
			}
			break;
		}
		case Open_DataScan:
		{
			if (ftl_Scan((DataScanCtx*)(pCtx + 1)))
			{
				eRet = Boot_Done;
			}
			break;
		}
	}
	return eRet;
}


void meta_Run(Evts bmEvt)
{
RETRY:

	switch (gpBootCtx->eStep)
	{
		case Boot_Init:
		{
			MEMSET_PTR((OpenCtx*)(gpBootCtx + 1), 0);
			gpBootCtx->eStep = meta_Open((OpenCtx*)(gpBootCtx + 1));
			if(Boot_Format == gpBootCtx->eStep)
			{
				goto RETRY;
			}
			break;
		}
		case Boot_Open:
		{
			switch (meta_Open((OpenCtx*)(gpBootCtx + 1)))
			{
				case Boot_Open: // Open running.
				{
					break;
				}
				case Boot_Done:
				{
					Sched_TrigSyncEvt(BIT(EVT_OPEN));
					gpBootCtx->eStep = Boot_Done;
					break;
				}
				case Boot_Format:
				{
					MEMSET_PTR((gpBootCtx + 1), 0);
					meta_Format((FormatCtx*)(gpBootCtx + 1));
					gpBootCtx->eStep = Boot_Format;
					break;
				}
			}
			break;
		}
		case Boot_Format:
		{
			switch (meta_Format((FormatCtx*)(gpBootCtx + 1)))
			{
				case Boot_Format:	// Continue....
				{
					break;
				}
				case Boot_Done:
				{
					gpBootCtx->eStep = Boot_Done;
					Sched_TrigSyncEvt(BIT(EVT_OPEN));
					break;
				}
			}
			break;
		}
		case Boot_Done:
		default:
		{
			assert(false);
		}
	}
}


void META_Init()
{
	gpBootCtx = (BootCtx*)anContext;
	MEMSET_OBJ(gstMeta, 0);
	MEMSET_OBJ(gstMetaCtx, 0);
	MEMSET_ARRAY(anContext, 0);
	Sched_Register(TID_META, meta_Run, BIT(MODE_NORMAL));
}
