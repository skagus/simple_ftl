
#include "nfc.h"
#include "timer.h"
#include "power.h"
#include "cpu.h"

#include "hw.h"

void HW_InitSim()
{
	CPU_InitSim();
	NFC_InitSim();
	TMR_InitSim();
	POWER_InitSim();
}

