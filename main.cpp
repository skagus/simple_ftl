
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

int main()
{
	NFC_InitSim();
	TMR_InitSim();
	POWER_InitSim();

	FTL_InitSim();
	TEST_InitSim();

	SIM_Run();

	return 0;
}

