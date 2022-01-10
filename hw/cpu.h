#pragma once

#include "types.h"
#include "sim_conf.h"
#include "sim.h"

#if EN_COROUTINE
#include "coroutine.h"
typedef void(*CpuEntry)(stack_t* token, user_t arg);
#else
typedef void(*CpuEntry)(void* pParam);
#endif

enum CpuID
{
	CPU_FTL,
	CPU_WORK,
	NUM_CPU,
};



//////////////////////
// CPU�� running�� ������ �ȵǹǷ�, �������� END_RUN�� �߰����ִ°� ����.
#define END_RUN			while(true){CPU_TimePass(100000000);}

void CPU_Add(CpuID eID, CpuEntry pfEntry, void* pParam);
CpuID CPU_GetCpuId();
void CPU_Start();	///< Initiate System. (normally called by simulator core)
void CPU_TimePass(uint32 nTick);
void CPU_Sleep();
void CPU_Wakeup(CpuID eCpu);			///< Wait timepass.
