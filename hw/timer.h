#pragma once
#include "types.h"
#include "sim.h"

#define NUM_TIMER	(5)

void TMR_InitSim();	// Sim 전용.

void TMR_Add(uint32 nTimerId, uint32 nTimeOut, Cbf pfCbf, bool bRepeat);
void TMR_Remove(uint32 nTimerId);
void TMR_Init();

