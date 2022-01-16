
#pragma once

#include "sim_conf.h"
#include "types.h"
#include "macro.h"

#define SIM_USEC(x)		(x)
#define SIM_MSEC(x)		SIM_USEC(1000)
#define SIM_SEC(x)		SIM_MSEC(1000)

/**
HW ID: 같은 handler를 사용하게 된다.
*/
enum HwID
{
	HW_CPU,
	HW_HIC,		///< Host side DMA done.
	HW_NFC,		///< NFC HW state chages.
	HW_NAND,	///< NAND state changes busy to idle.
	HW_TIMER,	///< Timer.
	HW_POWER,
	NUM_HW,
};

typedef void(*CbFunc)(uint32 nParam, uint32 nTag);	/// for Callback.

typedef void (*EvtHdr)(void* pEvt);

//////////////////////////////////////

void SIM_PowerDown();
void SIM_AddHW(HwID id, EvtHdr pfEvtHandler);

uint64 SIM_GetTick();
uint32 SIM_GetCycle();
void SIM_Print(const char *szFormat, ...);

void* SIM_NewEvt(HwID eOwn, uint32 time);
void SIM_Run();	// infinite running.

bool SIM_PeekTick(uint32 nTick);
uint32 SIM_GetRand(uint32 nMod);
uint32 SIM_GetSeqNo();

void SIM_UtilInit();
