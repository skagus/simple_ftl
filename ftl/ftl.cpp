#include "sim.h"
#include "buf.h"
#include "templ.h"
#include "nfc.h"
#include "test.h"
#include "io.h"
#include "ftl.h"

#define PRINTF			SIM_Print

#define SIZE_REQ_QUE	(16)

Queue<ReqInfo*, SIZE_REQ_QUE> gstReqQ;
void FTL_Request(ReqInfo* pReq)
{
	gstReqQ.PushTail(pReq);
}

uint32 FTL_GetNumLPN()
{
	return NUM_USER_BLK * LPN_PER_USER_BLK;
}

void FTL_Main(void* pParam)
{
	FTL_Init();

	while (true)
	{
		if (gstReqQ.Count() > 0)
		{
			ReqInfo* pReq = gstReqQ.PopHead();
			switch (pReq->eCmd)
			{
				case CMD_WRITE:
				{
//					PRINTF("Do Write: %d\n", pReq->nLPN);
					FTL_Write(pReq->nLPN, pReq->nBuf);
					break;
				}
				case CMD_READ:
				{
//					PRINTF("Do Read: %d\n", pReq->nLPN);
					FTL_Read(pReq->nLPN, pReq->nBuf);
					break;
				}
				default:
				{
					assert(false);
				}
			}
			TEST_DoneCmd(pReq);
		}
		SIM_CpuTimePass(1);
	}
}


void FTL_InitSim()
{
	BM_Init();
	SIM_AddCPU(CPU_FTL, FTL_Main, (void*)4);
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

