
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
	CmdInfo* pCmd = IO_Alloc(IOCB_Mig);
	IO_Erase(pCmd, pSrcLog->nPBN, 0);
	IO_WaitDone(pCmd);
	IO_Free(pCmd);
	META_Save();
	return pSrcLog;
}

void FTL_Write(uint32 nLPN, uint16 nNewBuf, uint8 nTag)
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
	CmdInfo* pCmd = IO_Alloc(IOCB_User);
	IO_Program(pCmd, pMap->nPBN, pMap->nCPO, nNewBuf, nTag);
	pMap->anMap[nLPO] = pMap->nCPO;
	pMap->nCPO++;
}

void FTL_Read(uint32 nLPN, uint16 nBufId, uint8 nTag)
{
	uint16 nLBN = nLPN / CHUNK_PER_PBLK;
	uint16 nLPO = nLPN % CHUNK_PER_PBLK;
	uint16 nPPO = 0xFFFF;

	LogMap* pMap = META_SearchLogMap(nLBN);
	if (nullptr != pMap)
	{
		nPPO = pMap->anMap[nLPO];
	}
	CmdInfo* pCmd = IO_Alloc(IOCB_User);
	if (0xFFFF != nPPO)	// in Log block.
	{
		IO_Read(pCmd, pMap->nPBN, nPPO, nBufId, nTag);
	}
	else
	{
		BlkMap* pBMap = META_GetBlkMap(nLBN);
		IO_Read(pCmd, pBMap->nPBN, nLPO, nBufId, nTag);
	}

	SIM_CpuTimePass(3);
}

#endif
