#include "sim.h"
//// HW..
#include "nfc.h"
#include "timer.h"
#include "power.h"
//// CPU
#include "test.h"
#include "ftl.h"

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

