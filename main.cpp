#include "sim.h"
//// HW..
#include "nfc.h"
#include "timer.h"
//// CPU
#include "test.h"
#include "ftl.h"

int main()
{
	SIM_Reset();

	NFC_InitSim();
	TMR_InitSim();

	FTL_InitSim();
	TEST_InitSim();
	
	SIM_Run();

	return 0;
}

