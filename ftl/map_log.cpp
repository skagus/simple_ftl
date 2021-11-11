#include "types.h"
#include "config.h"
#include "nfc.h"
#include "ftl.h"
#include "buf.h"
#include "io.h"

#if (MAPPING == FTL_LOG_MAP)

#define PRINTF		//	SIM_Print

#define NUM_LOG_BLK		(PBLK_PER_DIE - NUM_USER_BLK - 1)
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
BlkMap gastMap[NUM_USER_BLK];
LogMap gastLog[NUM_LOG_BLK];
uint16 gnFreePBN;

void FTL_Init()
{
	BM_Init();
	NFC_Init(io_CbDone);

	for (uint16 nBN = 0; nBN < NUM_USER_BLK; nBN++)
	{
		gastMap[nBN].nPBN = nBN;
		gastMap[nBN].bLog = 0;
	}

	for (uint16 nIdx = 0; nIdx < NUM_LOG_BLK; nIdx++)
	{
		gastLog[nIdx].nLBN = 0xFFFF;
		gastLog[nIdx].nPBN = NUM_USER_BLK + nIdx;
	}
	gnFreePBN = NUM_USER_BLK + NUM_LOG_BLK;
}

LogMap* getLogMap(uint16 nLBN)
{
	if (gastMap[nLBN].bLog)
	{
		for (uint16 nIdx = 0; nIdx < NUM_LOG_BLK; nIdx++)
		{
			if (gastLog[nIdx].nLBN == nLBN)
			{
				return gastLog + nIdx;
			}
		}
	}
	return nullptr;
}

LogMap* getVictim()
{
	for (uint16 nIdx = 0; nIdx < NUM_LOG_BLK; nIdx++)
	{
		if (gastLog[nIdx].nLBN == 0xFFFF)
		{
			return gastLog + nIdx;
		}
	}
	// free가 없으면, random victim.
	return gastLog + rand() % NUM_LOG_BLK;
}

void migrate(LogMap* pVictim)
{
	uint16 nOrgBN = gastMap[pVictim->nLBN].nPBN;
	uint16 nLogBN = pVictim->nPBN;
	uint16 nBuf4Copy = BM_Alloc();
	uint16 nDstBN = gnFreePBN;
	uint32* pSpare = (uint32*)BM_GetSpare(nBuf4Copy);
	uint32* pMain = (uint32*)BM_GetMain(nBuf4Copy);

	IO_Erase(nDstBN);	// Erase before program.
	for (uint16 nPO = 0; nPO < NUM_WL; nPO++)
	{
		if (0xFFFFFFFF != pVictim->anMap[nPO])
		{
			IO_Read(nLogBN, pVictim->anMap[nPO], nBuf4Copy);
		}
		else
		{
			IO_Read(nOrgBN, nPO, nBuf4Copy);
		}
		PRINTF("Mig: %X\n", *pSpare);
		if (*pSpare & 0xF == nPO)
		{
			assert((*pMain & 0xF) == nPO);
		}
		assert(*pSpare == *(uint32*)BM_GetMain(nBuf4Copy));

		IO_Program(nDstBN, nPO, nBuf4Copy);
	}
	gnFreePBN = gastMap[pVictim->nLBN].nPBN;
	gastMap[pVictim->nLBN].bLog = 0;
	gastMap[pVictim->nLBN].nPBN = nDstBN;
	BM_Free(nBuf4Copy);
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
	gastMap[nLBN].bLog = 1;
	pSrcLog->nLBN = nLBN;
	pSrcLog->nCPO = 0;
	IO_Erase(pSrcLog->nPBN);
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
		IO_Read(gastMap[nLBN].nPBN, nLPO, nBufId);
	}

	uint32* pnVal = (uint32*)BM_GetSpare(nBufId);
	PRINTF("Read: %X, %X\n", nLPN, *pnVal);
	if (0 != *pnVal)
	{
		assert(nLPN == *pnVal);
	}
}

#endif
