#include "timer.h"

struct TmrEvt
{
	uint32 nTimerId;
	bool bValid;
};

struct TimerInfo
{
	bool bRepeat;
	uint32 nTimeout;
	CbFunc pfCbf;
	TmrEvt* pstEvt;
};

TimerInfo gaTimer[NUM_TIMER];
uint32 gnCntTimer;

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
	}
}

void TMR_InitSim()
{
	SIM_AddHW(HW_TIMER, tmr_HandleEvt);
	gnCntTimer = 0;
}

///////////////////////////////////// Timer LLD ///////////////////////////////////////////

void TMR_Add(uint32 nTimerId, uint32 nTimeout, CbFunc pfCbf, bool bRepeat)
{
	TimerInfo* pTimer = gaTimer + nTimerId;
	pTimer->nTimeout = nTimeout;
	pTimer->pfCbf = pfCbf;
	pTimer->bRepeat = bRepeat;
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

}
