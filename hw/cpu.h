#pragma once

#include "types.h"
#include "coroutine.h"

// CPU는 running이 끝나면 안되므로, 마지막에 END_RUN을 추가해주는게 좋다.
#define END_RUN			while(true){CPU_TimePass(100000000);}

enum CpuID
{
	CPU_FTL,
	CPU_WORK,
	NUM_CPU,
};

///// 시작할 때, 호출하는 함수들 //////
void CPU_Add(CpuID eID, Routine pfEntry, void* pParam);
void CPU_Start();	///< Initiate System. (normally called by simulator core)
void CPU_InitSim();

///// 실행 중에 호출되는 함수들 /////
CpuID CPU_GetCpuId();
void CPU_TimePass(uint32 nTick);
void CPU_Sleep();
void CPU_Wakeup(CpuID eCpu);			///< Wait timepass.
