#include "power.h"

struct PowerEvt
{
	uint32 nOffDelay;
};

PowerEvt* power_NewEvt(uint32 nTimeout)
{
	PowerEvt* pNew = (PowerEvt*)SIM_NewEvt(HW_POWER, nTimeout);
	pNew->nOffDelay = nTimeout;
	return pNew;
}

void power_HandleEvt(void* pEvt)
{
	PowerEvt* pstEvt = (PowerEvt*)pEvt;
	SIM_Reset();
}

void POWER_InitSim()
{
	SIM_AddHW(HwID::HW_POWER, power_HandleEvt);
}

void POWER_SwitchOff()
{
	power_NewEvt(1);
}
