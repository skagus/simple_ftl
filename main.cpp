
#include "sim.h"
#include "cpu.h"
//// HW..
#include "hw.h"
#include "os.h"
//// CPU
#include "test.h"
#include "ftl.h"

int main()
{
	HW_InitSim();
	CPU_Add(CPU_FTL, FTL_Main, (void*)4);
	CPU_Add(CPU_WORK, TEST_Main, (void*)4);
	SIM_Run();

	return 0;
}

