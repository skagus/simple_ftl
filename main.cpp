
#include "sim.h"
//// HW..
#include "nfc.h"
#include "timer.h"
#include "power.h"
//// CPU
#include "test.h"
#include "ftl.h"

void TEST_InitSim()
{
	SIM_AddCPU(CPU_WORK, TEST_Main, (void*)4);
}

void FTL_InitSim()
{
	SIM_AddCPU(CPU_FTL, FTL_Main, (void*)4);
}

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

