#pragma once

#include "types.h"
#include "macro.h"
#define TICK_PER_SEC	(1000000LL)		// us unit.

/**
HW ID: 같은 handler를 사용하게 된다.
*/
enum HwID
{
	HW_HIC,		///< Host side DMA done.
	HW_NFC,		///< NFC HW state chages.
	HW_NAND,	///< NAND state changes busy to idle.
	HW_TIMER,	///< Timer.
	HW_POWER,
	NUM_HW,
};

enum CpuID
{
	CPU_FTL,
	CPU_WORK,
	NUM_CPU,
};

typedef void(*CbFunc)(uint32 nParam, uint32 nTag);	/// for Callback.

typedef void (*EvtHdr)(void* pEvt);
typedef void(*CpuEntry)(void* pParam);
//////////////////////
// CPU는 running이 끝나면 안되므로, 마지막에 END_RUN을 추가해주는게 좋다.
#define END_RUN			while(true){SIM_CpuTimePass(100000000);}

//////////////////////////////////////

void SIM_Reset();
void SIM_AddHW(HwID id, EvtHdr pfEvtHandler);
void SIM_AddCPU(CpuID eID, CpuEntry pfEntry, void* pParam);

uint64 SIM_GetTick();
void SIM_Print(const char *szFormat, ...);

void* SIM_NewEvt(HwID eOwn, uint32 time);
void SIM_CpuTimePass(uint32 nTick);
void SIM_Run();	// infinite running.

uint32 SIM_GetRand(uint32 nMod);
