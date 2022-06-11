
#include "sim.h"
#include "cpu.h"
#include "timer.h"
#include "power.h"
#include "buf.h"
#include "ftl.h"
#include "test.h"

#define PRINTF			SIM_Print
#define CMD_PRINTF		//SIM_Print

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
		CPU_TimePass(SIM_USEC(4));
		while (false == gbDone)
		{
			CPU_Sleep();
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
		CPU_TimePass(SIM_USEC(3));
		while (false == gbDone)
		{
			CPU_Sleep();
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
		CPU_TimePass(SIM_USEC(6));
		while (false == gbDone)
		{
			CPU_Sleep();
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
		CPU_TimePass(SIM_USEC(4));
		while (false == gbDone)
		{
			CPU_Sleep();
		}
		_CheckData(stReq.nBuf, stReq.nLPN);
		BM_Free(stReq.nBuf);
	}
}

void tc_StreamWrite(uint32 nMaxLPN)
{
	PRINTF("[TC] =========== %s ==========\n", __FUNCTION__);
	uint32 anLPN[3];
	anLPN[0] = 0;
	anLPN[1] = nMaxLPN / 4;
	anLPN[2] = nMaxLPN / 2;

	ReqInfo stReq;
	stReq.eCmd = CMD_WRITE;
	uint32 nCnt = nMaxLPN / 4;
	for (uint32 nCur = 0; nCur < nCnt; nCur++)
	{
		for (uint32 nStream = 0; nStream < 3; nStream++)
		{
			stReq.nLPN = anLPN[nStream];
			anLPN[nStream]++;
			stReq.nBuf = BM_Alloc();
			_FillData(stReq.nBuf, stReq.nLPN);
			gbDone = false;
			CMD_PRINTF("[TC] Write Req: %d\n", stReq.nLPN);
			FTL_Request(&stReq);
			CPU_TimePass(SIM_USEC(5));
			while (false == gbDone)
			{
				CPU_Sleep();
			}
			BM_Free(stReq.nBuf);
		}
	}
}


void tc_Shutdown(ShutdownOpt eOpt)
{
	ReqInfo stReq;
	stReq.eCmd = CMD_SHUTDOWN;
	stReq.eOpt = eOpt;
	gbDone = false;
	CMD_PRINTF("[TC] Shutdown Req\n");
	FTL_Request(&stReq);
	CPU_TimePass(SIM_USEC(5));
	while (false == gbDone)
	{
		CPU_Sleep();
	}
}

void sc_Long()
{
	uint32 nNumUserLPN = FTL_GetNumLPN(test_DoneCmd);

	for(uint32 nLoop = 0; nLoop < 2; nLoop++)
	{
		if (0 == (SIM_GetCycle() % 10))
		{
			tc_SeqWrite(0, nNumUserLPN);
		}
		tc_SeqRead(0, nNumUserLPN);
		while(true)
		{
			for (uint32 nLoop = 0; nLoop < 10; nLoop++)
			{
				tc_RandRead(0, nNumUserLPN, nNumUserLPN / 4);
				tc_RandWrite(0, nNumUserLPN, nNumUserLPN / 4);
			}
			tc_StreamWrite(nNumUserLPN);

			tc_SeqRead(0, nNumUserLPN);
		}
	}

	tc_Shutdown(SD_Safe);

	PRINTF("All Test Done\n");
	POWER_SwitchOff();
	END_RUN;
}

void sc_Short()
{
	uint32 nNumUserLPN = FTL_GetNumLPN(test_DoneCmd);

	tc_SeqRead(0, nNumUserLPN);
	if (0 == SIM_GetCycle())
	{
		tc_SeqWrite(0, nNumUserLPN);
	}
	else
	{
		tc_RandWrite(0, nNumUserLPN, nNumUserLPN / 8);
	}

	tc_Shutdown((SIM_GetRand(10) < 5) ? SD_Sudden : SD_Safe);
	PRINTF("All Test Done\n");
	POWER_SwitchOff();
	END_RUN;
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
	sc_Short();
}


