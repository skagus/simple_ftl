#include "templ.h"
#include "sim.h"
#include "buf.h"
#include "nfc.h"
#include "timer.h"
#include "power.h"
#include "scheduler.h"
#include "test.h"
#include "io.h"
#include "ftl.h"
#include "io.h"
#include "log_gc.h"
#include "log_req.h"
#include "log_meta.h"

#define PRINTF		//	SIM_Print

Queue<ReqInfo*, SIZE_REQ_QUE> gstReqQ;

void FTL_Request(ReqInfo* pReq)
{
	gstReqQ.PushTail(pReq);
	Sched_TrigSyncEvt(BIT(EVT_USER_CMD));
}

uint32 FTL_GetNumLPN(CbfReq pfCbf)
{
	REQ_SetCbf(pfCbf);
	SIM_CpuTimePass(SIM_MSEC(1000));	// Wait open time.
	return NUM_USER_BLK * LPN_PER_USER_BLK;
}

void FTL_Main(void* pParam)
{
	TMR_Init();
	BM_Init();
	NFC_Init(io_CbDone);

	Cbf pfTickIsr = Sched_Init();
	TMR_Add(0, SIM_MSEC(MS_PER_TICK), pfTickIsr, true);

	gstReqQ.Init();
	IO_Init();
	REQ_Init();
	META_Init();
	GC_Init();

	PRINTF("[FTL] Init done\n");
	while (true)
	{
		Sched_Run();
	}
}


#if 0 // Test..

void _IssueErase(CmdInfo* pCmd, uint8 nDie, uint16 nPBN)
{
	pCmd->bmPln = BIT(nPBN % NUM_PLN);
	pCmd->eCmd = NCmd::NC_ERB;
	pCmd->anBBN[0] = nPBN / NUM_PLN;
	pCmd->nDie = nDie;
	NFC_Issue(pCmd);
}

void _IssuePgm(CmdInfo* pCmd, uint8 nDie, uint16 nPBN, uint16 nWL, uint8 nPattern)
{
	uint8 nPln = nPBN % NUM_PLN;
	pCmd->bmPln = BIT(nPln);
	pCmd->eCmd = NCmd::NC_PGM;
	pCmd->anBBN[0] = nPBN / NUM_PLN;
	pCmd->nDie = nDie;
	pCmd->nWL = 0;
	pCmd->stPgm.anBufId;
	pCmd->stPgm.bmChunk = (BIT(CHUNK_PER_PPG) - 1) << (CHUNK_PER_PPG * nPln);
	for (uint32 nIdx = 0; nIdx < CHUNK_PER_BPG; nIdx++)
	{
		pCmd->stPgm.anBufId[nIdx] = nIdx;
		memset(BM_GetMain(nIdx), nPattern, BYTE_PER_CHUNK);
		memset(BM_GetSpare(nIdx), nPattern, BYTE_PER_SPARE);
	}
	NFC_Issue(pCmd);
}

void _IssueRead(CmdInfo* pCmd, uint8 nDie, uint16 nPBN, uint16 nWL, uint8 nPattern)
{
	uint8 nPln = nPBN % NUM_PLN;
	pCmd->bmPln = BIT(nPln);
	pCmd->eCmd = NCmd::NC_READ;
	pCmd->anBBN[0] = nPBN / NUM_PLN;
	pCmd->nDie = nDie;
	pCmd->nWL = 0;
	pCmd->stPgm.anBufId;
	pCmd->stPgm.bmChunk = (BIT(CHUNK_PER_PPG) - 1) << (CHUNK_PER_PPG * nPln);
	for (uint32 nIdx = 0; nIdx < CHUNK_PER_BPG; nIdx++)
	{
		pCmd->stRead.anBufId[nIdx] = nIdx;
	}
	NFC_Issue(pCmd);
}


void FTL_Main(void* pParam)
{
	NFC_Init(io_CbDone);

	CmdInfo* pstDone;
	CmdInfo stCmd;
	_IssueErase(&stCmd, 0, 0);
	pstDone = _GetDone();
	_IssuePgm(&stCmd, 0, 0, 0, 0xAA);
	pstDone = _GetDone();
	_IssueRead(&stCmd, 0, 0, 0, 0xAA);
	pstDone = _GetDone();

	END_RUN;
}

#endif

