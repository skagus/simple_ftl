
#include "types.h"
#include "config.h"
#include "map_log.h"
#include "nfc.h"
#include "ftl.h"
#include "buf.h"
#include "io.h"
#include "gc.h"
#include "meta_manager.h"
#if (MAPPING == FTL_LOG_MAP)

#define PRINTF				// SIM_Print


void FTL_Init()
{
	META_Init();

}

void FTL_Write(uint32 nLPN, uint16 nNewBuf, uint8 nTag)
{
	uint16 nLBN = nLPN / CHUNK_PER_PBLK;
	uint16 nLPO = nLPN % CHUNK_PER_PBLK;

	LogMap* pMap = META_SearchLogMap(nLBN);
	if (nullptr == pMap || pMap->nCPO >= CHUNK_PER_PBLK)
	{
		pMap = GC_MakeNewLog(nLBN, pMap);
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
