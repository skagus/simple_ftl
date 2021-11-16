#include "types.h"
#include "config.h"
#include "map_log.h"
#include "nfc.h"
#include "ftl.h"
#include "buf.h"
#include "io.h"
#include "meta_manager.h"
#if (MAPPING == FTL_LOG_MAP)

#define PRINTF				// SIM_Print


void FTL_Init()
{
	META_Init();

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

void migrate(LogMap* pVictim)
{
	BlkMap* pVictimMap = META_GetBlkMap(pVictim->nLBN);
	uint16 nOrgBN = pVictimMap->nPBN;
	uint16 nLogBN = pVictim->nPBN;
	uint16 nBuf4Copy = BM_Alloc();
	uint16 nDstBN = META_GetFreePBN();
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
		if ((*pSpare & 0xF) == nPO)
		{
			assert((*pMain & 0xF) == nPO);
		}
		assert(*pSpare == *(uint32*)BM_GetMain(nBuf4Copy));

		pCmd = IO_Program(nDstBN, nPO, nBuf4Copy);
		IO_WaitDone(pCmd);
		IO_Free(pCmd);
	}
	META_SetFreePBN(pVictimMap->nPBN);
	pVictimMap->bLog = 0;
	pVictimMap->nPBN = nDstBN;
	BM_Free(nBuf4Copy);
//	META_Save();
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
	META_GetBlkMap(nLBN)->bLog = 1;
	pSrcLog->nLBN = nLBN;
	pSrcLog->nCPO = 0;
	CmdInfo* pCmd = IO_Erase(pSrcLog->nPBN);
	IO_WaitDone(pCmd);
	IO_Free(pCmd);
	META_Save();
	return pSrcLog;
}

void FTL_Write(uint32 nLPN, uint16 nNewBuf)
{
	uint16 nLBN = nLPN / CHUNK_PER_PBLK;
	uint16 nLPO = nLPN % CHUNK_PER_PBLK;

	LogMap* pMap = META_SearchLogMap(nLBN);
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

	LogMap* pMap = META_SearchLogMap(nLBN);
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
		BlkMap* pBMap = META_GetBlkMap(nLBN);
		IO_Read(pBMap->nPBN, nLPO, nBufId);
	}

	SIM_CpuTimePass(3);
}

#endif
