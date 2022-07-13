
#pragma once

#include "sim_conf.h"
#include "types.h"
#include "macro.h"

//////////////////////////////////////


void SIM_PowerDown();
uint64 SIM_GetTick();
uint32 SIM_GetCycle();
void SIM_Print(const char *szFormat, ...);

uint32 SIM_GetRand(uint32 nMod);
uint32 SIM_GetSeqNo();
uint32 SIM_IncSeqNo();

void SIM_Init(uint32 nSeed, uint32 nBrkNo = 0);
void SIM_Run();	// infinite running.
