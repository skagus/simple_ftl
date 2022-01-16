
#include "sim.h"
#include "cpu.h"
//// HW..
#include "nfc.h"
#include "timer.h"
#include "power.h"
//// CPU
#include "test.h"
#include "ftl.h"

#if EN_COROUTINE
void WRP_Test(stack_t* token, user_t arg)
{
	TEST_Main(arg);
}

void WRP_Ftl(stack_t* token, user_t arg)
{
	FTL_Main(arg);
}

void TEST_InitSim()
{
	CPU_Add(CPU_WORK, WRP_Test, (void*)4);
}

void FTL_InitSim()
{
	CPU_Add(CPU_FTL, WRP_Ftl, (void*)4);
}
#else
void TEST_InitSim()
{
	CPU_Add(CPU_WORK, TEST_Main, (void*)4);
}

void FTL_InitSim()
{
	CPU_Add(CPU_FTL, FTL_Main, (void*)4);
}

#endif

#define EN_BENCHMARK	(1)		///< Simulation performance benchmark.

int main()
{
	CPU_InitSim();
	NFC_InitSim();
	TMR_InitSim();
	POWER_InitSim();

	FTL_InitSim();
	TEST_InitSim();

#if	EN_BENCHMARK
	LARGE_INTEGER stBegin;
	QueryPerformanceCounter(&stBegin);
	uint32 nCnt = 200;
#endif

	SIM_UtilInit();
	SIM_Run();

#if	EN_BENCHMARK
	LARGE_INTEGER stEnd;
	QueryPerformanceCounter(&stEnd);
	printf("Time: %.3e\n", float(stEnd.QuadPart - stBegin.QuadPart));
#endif

	return 0;
}

