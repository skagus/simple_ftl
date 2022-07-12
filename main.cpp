
#include "sim.h"
#include "cpu.h"
//// HW..
#include "hw.h"
//// CPU
#include "test.h"
#include "ftl.h"

int main()
{
	SIM_Init(100, 0xE080EC);
	HW_InitSim();

	CPU_Add(CPU_FTL, FTL_Main, (void*)4);
	CPU_Add(CPU_WORK, TEST_Main, (void*)4);

	SIM_Run();

	return 0;
}

