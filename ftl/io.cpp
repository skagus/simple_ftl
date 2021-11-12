#include "types.h"
#include "templ.h"
#include "scheduler.h"
#include "config.h"
#include "nfc.h"
#include "ftl.h"
#include "buf.h"

#define PRINTF			// SIM_Print
#define NUM_NAND_CMD	(5)
static bool gbDone;


CmdInfo gaCmds[NUM_NAND_CMD];
Queue<CmdInfo*, NUM_NAND_CMD + 1> gNCmdPool;

CmdInfo* IO_GetDone(bool bWait)
{
	CmdInfo* pRet = NFC_GetDone();
	while (bWait && (nullptr == pRet))
	{
		pRet = NFC_GetDone();
		SIM_CpuTimePass(10);
	}
	return pRet;
}

void io_CbDone(uint32 nDie, uint32 nTag)
{
	Sched_TrigAsyncEvt(BIT(EVT_NAND_CMD));
}

void IO_Free(CmdInfo* pCmd)
{
	gNCmdPool.PushTail(pCmd);
}

CmdInfo* IO_Alloc()
{
	if (gNCmdPool.Count() > 0)
	{
		CmdInfo* pRet = gNCmdPool.PopHead();
		pRet->nDbgSN = SIM_GetSeqNo();
		return pRet;
	}
	return nullptr;
}

void IO_WaitDone(CmdInfo* pCmd)
{
	CmdInfo* pDone = IO_GetDone(true);
	assert(pDone == pCmd);
}

CmdInfo* IO_Read(uint16 nPBN, uint16 nPage, uint16 nBufId)
{
	CmdInfo* pstCmd = IO_Alloc();
	pstCmd->eCmd = NCmd::NC_READ;
	pstCmd->nDie = 0;
	pstCmd->nWL = nPage;
	pstCmd->anBBN[0] = nPBN / NUM_PLN;
	pstCmd->bmPln = BIT(nPBN % NUM_PLN);
	pstCmd->stRead.bmChunk = 1;
	pstCmd->stRead.anBufId[0] = nBufId;
	
	NFC_Issue(pstCmd);
	PRINTF("IO Rd (%X,%X)-->%X\n", nPBN, nPage, *(uint32*)BM_GetSpare(nBufId));
	SIM_CpuTimePass(3);
	return pstCmd;
}

CmdInfo* IO_Program(uint16 nPBN, uint16 nPage, uint16 nBufId)
{
	CmdInfo* pstCmd = IO_Alloc();
	pstCmd->eCmd = NCmd::NC_PGM;
	pstCmd->nDie = 0;
	pstCmd->nWL = nPage;
	pstCmd->anBBN[0] = nPBN / NUM_PLN;
	pstCmd->bmPln = BIT(nPBN % NUM_PLN);
	pstCmd->stPgm.bmChunk = 1;
	pstCmd->stPgm.anBufId[0] = nBufId;
	PRINTF("IO Pgm (%X,%X) %X\n", nPBN, nPage, *(uint32*)BM_GetSpare(nBufId));

	NFC_Issue(pstCmd);
	SIM_CpuTimePass(3);
	return pstCmd;
}

CmdInfo* IO_Erase(uint16 nPBN)
{
	CmdInfo* pstCmd = IO_Alloc();
	pstCmd->eCmd = NCmd::NC_ERB;
	pstCmd->nDie = 0;
	pstCmd->nWL = 0;
	pstCmd->anBBN[0] = nPBN / NUM_PLN;
	pstCmd->bmPln = BIT(nPBN % NUM_PLN);

	NFC_Issue(pstCmd);
	SIM_CpuTimePass(3);
	return pstCmd;
}

void IO_Init()
{
	NFC_Init(io_CbDone);
	gNCmdPool.Init();
	for (uint16 nIdx = 0; nIdx < NUM_NAND_CMD; nIdx++)
	{
		gNCmdPool.PushTail(gaCmds + nIdx);
	}
}
