

#include "types.h"
#include "config.h"
#include "scheduler.h"
#include "nfc.h"
#include "ftl.h"
#include "buf.h"
#include "io.h"
#include "map_log.h"
#include "meta_manager.h"

#define PRINTF				// SIM_Print

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
		pCmd = IO_Erase(gstMetaCtx.nCurBN);
		IO_WaitDone(pCmd);
		IO_Free(pCmd);
	}
	uint16 nBuf = BM_Alloc();
	*(uint32*)BM_GetSpare(nBuf) = gstMetaCtx.nAge;
	uint8* pMain = BM_GetMain(nBuf);
	memcpy(pMain, &gstMeta, sizeof(gstMeta));
	pCmd = IO_Program(gstMetaCtx.nCurBN, gstMetaCtx.nNextWL, nBuf);
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

void ftl_Scan(uint32 nLogIdx)
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
		pCmd = IO_Read(pLMap->nPBN, nPO, nBuf);
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

enum OpenStep
{
	Open_BlkScan,
	Open_PageScan,
	Open_DataScan,
};

struct OpenCtx
{
	OpenStep eOpenStep;
	uint16 nBN;
	uint16 nWL;
};

BootStep meta_Open(OpenCtx* pCtx)
{
	BootStep eRet;
	switch (pCtx->eOpenStep)
	{
		case Open_BlkScan:
		{
			uint16 nBuf = BM_Alloc();
			uint32* pnSpare = (uint32*)BM_GetSpare(nBuf);
			uint8* pMain = BM_GetMain(nBuf);
			// Find Latest Blk.
			uint32 nMaxAge = 0;
			uint16 nMaxBN = 0xFFFF;
			CmdInfo* pCmd;
			for (uint16 nBN = 0; nBN < NUM_META_BLK; nBN++)
			{
				pCmd = IO_Read(nBN, 0, nBuf);
				IO_WaitDone(pCmd);
				IO_Free(pCmd);
				if ((*pnSpare > nMaxAge) && (*pnSpare != 0xFFFFFFFF))
				{
					nMaxAge = *pnSpare;
					nMaxBN = nBN;
				}
			}
			BM_Free(nBuf);
			pCtx->nBN = nMaxBN;
			if (0xFFFF != pCtx->nBN)
			{
				pCtx->eOpenStep = Open_PageScan;
				Sched_Wait(0, 1);
				eRet = Boot_Open;
			}
			else
			{
				Sched_Wait(0, 1);
				eRet = Boot_Format;
			}
			break;
		}
		case Open_PageScan:
		{
			uint16 nBN = pCtx->nBN;
			uint16 nBuf = BM_Alloc();
			uint32* pnSpare = (uint32*)BM_GetSpare(nBuf);
			uint8* pMain = BM_GetMain(nBuf);
			CmdInfo* pCmd;
			uint16 nCPO;
			for (nCPO = 0; nCPO < NUM_WL; nCPO++)
			{
				pCmd = IO_Read(nBN, nCPO, nBuf);
				IO_WaitDone(pCmd);
				IO_Free(pCmd);

				if (0xFFFFFFFF == *pnSpare)
				{
					break;
				}
				gstMetaCtx.nAge = *pnSpare;
				memcpy(&gstMeta, pMain, sizeof(gstMeta));
			}
			BM_Free(nBuf);
			if (nCPO < NUM_WL)
			{
				gstMetaCtx.nCurBN = nBN;
				gstMetaCtx.nNextWL = nCPO;
			}
			else
			{
				gstMetaCtx.nNextWL = 0;
				gstMetaCtx.nCurBN = (nBN + 1) % NUM_META_BLK;
			}
			pCtx->eOpenStep = Open_DataScan;
			Sched_Wait(0, 1);
			eRet = Boot_Open;
			break;
		}
		case Open_DataScan:
		{
			for (uint32 nIdx = 0; nIdx < NUM_LOG_BLK; nIdx++)
			{
				ftl_Scan(nIdx);
			}
			eRet = Boot_Done;
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
			MEMSET_PTR((gpBootCtx + 1), 0);
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
