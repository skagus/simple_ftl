
#include "sim.h"
#include "timer.h"
#include "buf.h"
#include "ftl.h"
#include "test.h"
#include "power.h"

#define PRINTF			SIM_Print
#define CMD_PRINTF		// SIM_Print
static uint32* gaDict;
static bool gbDone;

void _FillData(uint16 nBuf, uint32 nLPN)
{
	gaDict[nLPN]++;
	uint32* pnData = (uint32*)BM_GetMain(nBuf);
	pnData[0] = nLPN;
	pnData[1] = gaDict[nLPN];
}

void _CheckData(uint16 nBuf, uint32 nLPN)
{
	uint32* pnData = (uint32*)BM_GetMain(nBuf);
	if (gaDict[nLPN] > 0)
	{
		assert(pnData[0] == nLPN);
		assert(pnData[1] == gaDict[nLPN]);
	}
}

void test_DoneCmd(ReqInfo* pReq)
{
//	PRINTF("Done\n");
	gbDone = true;
}

void tc_SeqWrite(uint32 nStart, uint32 nSize)
{
	PRINTF("[TC] =========== %s ==========\n", __FUNCTION__);
	ReqInfo stReq;
	stReq.eCmd = CMD_WRITE;
	uint32 nCur = nStart;
	for (uint32 nCur = nStart; nCur < nStart + nSize; nCur++)
	{
		stReq.nLPN = nCur;
		stReq.nBuf = BM_Alloc();
		_FillData(stReq.nBuf, stReq.nLPN);
		gbDone = false;
		FTL_Request(&stReq);
		CMD_PRINTF("[TC] Write Req: %d\n", nCur);
		while (false == gbDone)
		{
			SIM_CpuTimePass(SIM_USEC(10));
		}
		BM_Free(stReq.nBuf);
	}
}

void tc_SeqRead(uint32 nStart, uint32 nSize)
{
	PRINTF("[TC] =========== %s ==========\n", __FUNCTION__);
	ReqInfo stReq;
	stReq.eCmd = CMD_READ;
	uint32 nCur = nStart;
	for (uint32 nCur = nStart; nCur < nStart + nSize; nCur++)
	{
		stReq.nLPN = nCur;
		stReq.nBuf = BM_Alloc();
		gbDone = false;
		FTL_Request(&stReq);
		CMD_PRINTF("[TC] Read Req: %d\n", nCur);
		while (false == gbDone)
		{
			SIM_CpuTimePass(SIM_USEC(10));
		}
		_CheckData(stReq.nBuf, stReq.nLPN);
		BM_Free(stReq.nBuf);
	}
}


void tc_RandWrite(uint32 nBase, uint32 nRange, uint32 nCount)
{
	PRINTF("[TC] =========== %s ==========\n", __FUNCTION__);
	ReqInfo stReq;
	stReq.eCmd = CMD_WRITE;
	while(nCount--)
	{
		stReq.nLPN = nBase + SIM_GetRand(nRange);
		stReq.nBuf = BM_Alloc();
		_FillData(stReq.nBuf, stReq.nLPN);
		gbDone = false;
		FTL_Request(&stReq);
		CMD_PRINTF("[TC] Write Req\n");
		while (false == gbDone)
		{
			SIM_CpuTimePass(SIM_USEC(10));
		}
		BM_Free(stReq.nBuf);
	}
}


void tc_RandRead(uint32 nBase, uint32 nRange, uint32 nCount)
{
	PRINTF("[TC] =========== %s ==========\n", __FUNCTION__);
	ReqInfo stReq;
	stReq.eCmd = CMD_READ;

	while (nCount--)
	{
		stReq.nLPN = nBase + SIM_GetRand(nRange);
		stReq.nBuf = BM_Alloc();
		gbDone = false;
		FTL_Request(&stReq);
		CMD_PRINTF("[TC] Read Req\n");
		while (false == gbDone)
		{
			SIM_CpuTimePass(SIM_USEC(10));
		}
		_CheckData(stReq.nBuf, stReq.nLPN);
		BM_Free(stReq.nBuf);
	}
}

/**
Workload 생성역할.
*/
void TEST_Main(void* pParam)
{
	uint32 nNumUserLPN = FTL_GetNumLPN(test_DoneCmd);
	if (nullptr == gaDict)
	{
		gaDict = new uint32[nNumUserLPN];
		memset(gaDict, 0, sizeof(uint32) * nNumUserLPN);
	}
	if (0 == (SIM_GetCycle() % 10))
	{
		tc_SeqWrite(0, nNumUserLPN);
	}
	tc_SeqRead(0, nNumUserLPN);
	for (uint32 nLoop = 0; nLoop < 1; nLoop++)
	{
		tc_RandRead(0, nNumUserLPN, nNumUserLPN * 2);
		tc_RandWrite(0, nNumUserLPN, nNumUserLPN / 128);
	}
	PRINTF("All Test Done\n");
	POWER_SwitchOff();
	END_RUN;
}


