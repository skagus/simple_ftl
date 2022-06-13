#pragma once

#include "types.h"

// Add this on FW end. (because, if exit on running coroutine, some problem)
#define END_RUN			while(true){CPU_TimePass(100000000);}

typedef void(*Routine)(void* pParam);


enum CpuIDs
{
	CPU_FTL,
	CPU_WORK,
	NUM_CPU,
};

///// called on starting. //////
void CPU_Add(uint32 nCpuId, Routine pfEntry, void* pParam);
void CPU_Start();	///< Initiate System. (normally called by simulator core)
void CPU_InitSim();

///// called on running. /////
uint32 CPU_GetCpuId();
void CPU_TimePass(uint32 nTick);
void CPU_Sleep();
void CPU_Wakeup(uint32 nCpuId, uint32 nAfterTick = 0);			///< Wait timepass.
