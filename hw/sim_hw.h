#pragma once
/**
* HW 전용 include file.
* 
*/

#include "types.h"

#define BYTE_PER_EVT	(20)	///< Max sizeof event.

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
	HW_UART,
	NUM_HW,
};

typedef void (*EvtHdr)(void* pEvt);

//////////////////////////////////////

void SIM_AddHW(HwID id, EvtHdr pfEvtHandler);
void* SIM_NewEvt(HwID eOwn, uint32 time);

bool SIM_PeekTick(uint32 nTick);
void SIM_SwitchToSim();		///< used by CPU.

