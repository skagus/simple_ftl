#include "types.h"
#include "templ.h"
#include "cpu.h"
#include "nfc.h"
#include "buf.h"
#include "scheduler.h"
#include "config.h"
#include "ftl.h"
#include "io.h"

#define PRINTF			// SIM_Print

CmdInfo gaCmds[NUM_NAND_CMD];
CbKey gaKeys[NUM_NAND_CMD];
LinkedQueue<CmdInfo> gNCmdPool;
IoCbf gaCbf[NUM_IOCB];
LinkedQueue<CmdInfo> gaDone[NUM_IOCB];

CmdInfo* IO_PopDone(CbKey eCbId)
{
	CmdInfo* pRet = gaDone[eCbId].PopHead();
	return pRet;
}

CmdInfo* IO_GetDone(CbKey eCbId)
{
	CmdInfo* pRet = gaDone[eCbId].GetHead();
	return pRet;
}


void io_CbDone(uint32 nDie, uint32 nTag)
{
	CmdInfo* pRet = NFC_GetDone();
	if (nullptr != pRet)
	{
		uint8 nId = pRet - gaCmds;
		uint8 nTag = gaKeys[nId];
		gaDone[nTag].PushTail(pRet);
		Sched_TrigAsyncEvt(BIT(EVT_NAND_CMD));
	}
}

void IO_Free(CmdInfo* pCmd)
{
	gaKeys[pCmd - gaCmds] = NUM_IOCB;
	gNCmdPool.PushTail(pCmd);
	Sched_TrigSyncEvt(BIT(EVT_IO_FREE));
}

CmdInfo* IO_Alloc(CbKey eKey)
{
	if (gNCmdPool.Count() > 0)
	{
		CmdInfo* pRet = gNCmdPool.PopHead();
		pRet->nDbgSN = SIM_GetSeqNo();
		gaKeys[pRet - gaCmds] = eKey;
		return pRet;
	}
	return nullptr;
}

uint32 IO_CountFree()
{
	return gNCmdPool.Count();
}

void IO_WaitDone(CmdInfo* pCmd)
{
	CbKey eKey = gaKeys[pCmd - gaCmds];
	CmdInfo* pDone = gaDone[eKey].PopHead();
	while (nullptr == pDone)
	{
		pDone = gaDone[eKey].PopHead();
		CPU_TimePass(SIM_USEC(10));
	}
	assert(pDone == pCmd);
}

void IO_Read(CmdInfo* pstCmd, uint16 nPBN, uint16 nPage, uint16 nBufId, uint32 nTag)
{
	pstCmd->eCmd = NCmd::NC_READ;
	pstCmd->nDie = 0;
	pstCmd->nWL = nPage;
	pstCmd->anBBN[0] = nPBN / NUM_PLN;
	pstCmd->bmPln = BIT(nPBN % NUM_PLN);
	pstCmd->stRead.bmChunk = 1;
	pstCmd->stRead.anBufId[0] = nBufId;
	pstCmd->nTag = nTag;

	NFC_Issue(pstCmd);
	PRINTF("IO Rd (%X,%X)-->%X\n", nPBN, nPage, *(uint32*)BM_GetSpare(nBufId));
	CPU_TimePass(SIM_USEC(3));
}

void IO_Program(CmdInfo* pstCmd, uint16 nPBN, uint16 nPage, uint16 nBufId, uint32 nTag)
{
	pstCmd->eCmd = NCmd::NC_PGM;
	pstCmd->nDie = 0;
	pstCmd->nWL = nPage;
	pstCmd->anBBN[0] = nPBN / NUM_PLN;
	pstCmd->bmPln = BIT(nPBN % NUM_PLN);
	pstCmd->stPgm.bmChunk = 1;
	pstCmd->stPgm.anBufId[0] = nBufId;
	pstCmd->nTag = nTag;

	PRINTF("IO Pgm (%X,%X) %X\n", nPBN, nPage, *(uint32*)BM_GetSpare(nBufId));

	NFC_Issue(pstCmd);
	CPU_TimePass(SIM_USEC(3));
}

void IO_Erase(CmdInfo* pstCmd, uint16 nPBN, uint32 nTag)
{
	pstCmd->eCmd = NCmd::NC_ERB;
	pstCmd->nDie = 0;
	pstCmd->nWL = 0;
	pstCmd->anBBN[0] = nPBN / NUM_PLN;
	pstCmd->bmPln = BIT(nPBN % NUM_PLN);
	pstCmd->nTag = nTag;
	NFC_Issue(pstCmd);
	CPU_TimePass(SIM_USEC(3));
}

void IO_RegCbf(CbKey eId, IoCbf pfCb)
{
	gaCbf[eId] = pfCb;
}


void IO_Init()
{
	NFC_Init(io_CbDone);
	gNCmdPool.Init();
	for (uint16 nIdx = 0; nIdx < NUM_NAND_CMD; nIdx++)
	{
		IO_Free(gaCmds + nIdx);
	}
}
