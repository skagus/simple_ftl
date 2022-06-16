
#include "sim_hw.h"
#include "sim.h"
#include "cpu.h"		// for CPU_Wakeup
#include "timer.h"

struct TmrEvt
{
	uint32 nTimerId;
	bool bValid;
};
static_assert(sizeof(TmrEvt) < BYTE_PER_EVT);

struct TimerInfo
{
	bool bRepeat;
	uint32 nTimeout;
	Cbf pfCbf;
	TmrEvt* pstEvt;
	uint32 eCpu;
};

TimerInfo gaTimer[NUM_TIMER];

TmrEvt* tmr_NewEvt(uint32 nTimerId, uint32 nTimeout)
{
	TmrEvt* pNew = (TmrEvt*)SIM_NewEvt(HW_TIMER, nTimeout);
	pNew->nTimerId = nTimerId;
	pNew->bValid = true;
	return pNew;
}

void tmr_HandleEvt(void* pEvt)
{
	TmrEvt* pstEvt = (TmrEvt*)pEvt;
	if (true == pstEvt->bValid)
	{
		TimerInfo* pTI = gaTimer + pstEvt->nTimerId;
		if (pTI->bRepeat)
		{
			pTI->pstEvt = tmr_NewEvt(pstEvt->nTimerId, pTI->nTimeout);
		}
		else
		{
			pTI->nTimeout = 0;
		}
		pTI->pfCbf(0, 0);
		CPU_Wakeup(pTI->eCpu, SIM_USEC(1)); // processing time of timer ISR  is 1 usec.
	}
}

void TMR_InitSim()
{
	SIM_AddHW(HW_TIMER, tmr_HandleEvt);
}

///////////////////////////////////// Timer LLD ///////////////////////////////////////////

void TMR_Add(uint32 nTimerId, uint32 nTimeout, Cbf pfCbf, bool bRepeat)
{
	TimerInfo* pTimer = gaTimer + nTimerId;
	pTimer->nTimeout = nTimeout;
	pTimer->pfCbf = pfCbf;
	pTimer->bRepeat = bRepeat;
	pTimer->eCpu = CPU_GetCpuId();
	if (nullptr != pTimer->pstEvt)
	{
		pTimer->pstEvt->bValid = false;
	}
	pTimer->pstEvt = tmr_NewEvt(nTimerId, nTimeout);
}

void TMR_Remove(uint32 nTimerId)
{
	TimerInfo* pTimer = gaTimer + nTimerId;
	if (nullptr != pTimer->pstEvt)
	{
		pTimer->pstEvt->bValid = false;
	}
	pTimer->pstEvt = nullptr;
	MEMSET_PTR(pTimer, 0);
}

// Initialize by FW.
void TMR_Init()
{
	MEMSET_ARRAY(gaTimer, 0);
}
