#include "types.h"
#include "config.h"
#include "nfc.h"
#include "ftl.h"
#include "buf.h"
#include "io.h"

#if (MAPPING == FTL_LOG_MAP)

#define PRINTF				// SIM_Print
#define NUM_META_BLK		(2)
#define NUM_LOG_BLK		(PBLK_PER_DIE - NUM_USER_BLK - NUM_META_BLK - 1)
static_assert(NUM_LOG_BLK > 1);

struct LogMap
{
	uint16 nLBN;
	uint16 nPBN;
	uint16 nCPO;
	uint32 anMap[CHUNK_PER_PBLK];
};

struct BlkMap
{
	uint16 bLog : 1;
	uint16 nPBN : 15;
};

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

void ftl_Format()
{
	uint16 nBN = NUM_META_BLK;
	for (uint16 nIdx = 0; nIdx < NUM_USER_BLK; nIdx++)
	{
		gstMeta.astMap[nIdx].nPBN = nBN;
		gstMeta.astMap[nIdx].bLog = 0;
		nBN++;
	}

	for (uint16 nIdx = 0; nIdx < NUM_LOG_BLK; nIdx++)
	{
		gstMeta.astLog[nIdx].nLBN = 0xFFFF;
		gstMeta.astLog[nIdx].nPBN = nBN;
		nBN++;
	}
	gstMeta.nFreePBN = nBN;
	gstMetaCtx.nAge = NUM_WL;	/// Not tobe zero.
}

void ftl_MetaSave()
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

bool ftl_Open()
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
	if (0xFFFF != nMaxBN)
	{	// Find Latest WL
		uint16 nCPO;
		for (nCPO = 0; nCPO < NUM_WL; nCPO++)
		{
			pCmd = IO_Read(nMaxBN, nCPO, nBuf);
			IO_WaitDone(pCmd);
			IO_Free(pCmd);

			if (0xFFFFFFFF != *pnSpare)
			{
				gstMetaCtx.nAge = *pnSpare;
				memcpy(&gstMeta, pMain, sizeof(gstMeta));
			}
			else
			{
				break;
			}
		}
		if (nCPO < NUM_WL)
		{
			gstMetaCtx.nCurBN = nMaxBN;
			gstMetaCtx.nNextWL = nCPO;
		}
		else
		{
			gstMetaCtx.nNextWL = 0;
			gstMetaCtx.nCurBN = (nMaxBN + 1) % NUM_META_BLK;
		}
		for (uint32 nIdx = 0; nIdx < NUM_LOG_BLK; nIdx++)
		{
			ftl_Scan(nIdx);
		}
		BM_Free(nBuf);
		return true;
	}
	BM_Free(nBuf);
	return false;
}

void FTL_Init()
{
	MEMSET_OBJ(gstMeta, 0);
	MEMSET_OBJ(gstMetaCtx, 0);

	if (false == ftl_Open())
	{
		ftl_Format();
	}
}

LogMap* getLogMap(uint16 nLBN)
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

LogMap* getVictim()
{
	for (uint16 nIdx = 0; nIdx < NUM_LOG_BLK; nIdx++)
	{
		if (gstMeta.astLog[nIdx].nLBN == 0xFFFF)
		{
			return gstMeta.astLog + nIdx;
		}
	}
	// free가 없으면, random victim.
	return gstMeta.astLog + rand() % NUM_LOG_BLK;
}

void migrate(LogMap* pVictim)
{
	uint16 nOrgBN = gstMeta.astMap[pVictim->nLBN].nPBN;
	uint16 nLogBN = pVictim->nPBN;
	uint16 nBuf4Copy = BM_Alloc();
	uint16 nDstBN = gstMeta.nFreePBN;
	uint32* pSpare = (uint32*)BM_GetSpare(nBuf4Copy);
	uint32* pMain = (uint32*)BM_GetMain(nBuf4Copy);
	CmdInfo* pCmd;

	pCmd = IO_Erase(nDstBN);	// Erase before program.
	IO_WaitDone(pCmd);
	IO_Free(pCmd);
	for (uint16 nPO = 0; nPO < NUM_WL; nPO++)
	{
		if (0xFFFFFFFF != pVictim->anMap[nPO])
		{
			pCmd = IO_Read(nLogBN, pVictim->anMap[nPO], nBuf4Copy);
			IO_WaitDone(pCmd);
			IO_Free(pCmd);
		}
		else
		{
			pCmd = IO_Read(nOrgBN, nPO, nBuf4Copy);
			IO_WaitDone(pCmd);
			IO_Free(pCmd);
		}
		PRINTF("Mig: %X\n", *pSpare);
		if (*pSpare & 0xF == nPO)
		{
			assert((*pMain & 0xF) == nPO);
		}
		assert(*pSpare == *(uint32*)BM_GetMain(nBuf4Copy));

		pCmd = IO_Program(nDstBN, nPO, nBuf4Copy);
		IO_WaitDone(pCmd);
		IO_Free(pCmd);
	}
	gstMeta.nFreePBN = gstMeta.astMap[pVictim->nLBN].nPBN;
	gstMeta.astMap[pVictim->nLBN].bLog = 0;
	gstMeta.astMap[pVictim->nLBN].nPBN = nDstBN;
	BM_Free(nBuf4Copy);
//	ftl_MetaSave();
}

LogMap* makeNewLog(uint16 nLBN, LogMap* pSrcLog)
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
	gstMeta.astMap[nLBN].bLog = 1;
	pSrcLog->nLBN = nLBN;
	pSrcLog->nCPO = 0;
	CmdInfo* pCmd = IO_Erase(pSrcLog->nPBN);
	IO_WaitDone(pCmd);
	IO_Free(pCmd);
	ftl_MetaSave();
	return pSrcLog;
}

void FTL_Write(uint32 nLPN, uint16 nNewBuf)
{
	uint16 nLBN = nLPN / CHUNK_PER_PBLK;
	uint16 nLPO = nLPN % CHUNK_PER_PBLK;

	LogMap* pMap = getLogMap(nLBN);
	if (nullptr == pMap || pMap->nCPO >= CHUNK_PER_PBLK)
	{
		pMap = makeNewLog(nLBN, pMap);
	}
	*(uint32*)BM_GetSpare(nNewBuf) = nLPN;
	assert(nLPN == *(uint32*)BM_GetMain(nNewBuf));
	IO_Program(pMap->nPBN, pMap->nCPO, nNewBuf);
	pMap->anMap[nLPO] = pMap->nCPO;
	pMap->nCPO++;
}

void FTL_Read(uint32 nLPN, uint16 nBufId)
{
	uint16 nLBN = nLPN / CHUNK_PER_PBLK;
	uint16 nLPO = nLPN % CHUNK_PER_PBLK;
	uint16 nPPO = 0xFFFF;

	LogMap* pMap = getLogMap(nLBN);
	if (nullptr != pMap)
	{
		nPPO = pMap->anMap[nLPO];
	}

	if (0xFFFF != nPPO)	// in Log block.
	{
		IO_Read(pMap->nPBN, nPPO, nBufId);
	}
	else
	{
		IO_Read(gstMeta.astMap[nLBN].nPBN, nLPO, nBufId);
	}

	SIM_CpuTimePass(3);
}

#endif
