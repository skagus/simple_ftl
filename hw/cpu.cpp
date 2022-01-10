
#include "sim.h"
#include "cpu.h"
#include <windows.h>


struct CpuEvt
{
	CpuID eCpuId;
};
static_assert(sizeof(CpuEvt) < BYTE_PER_EVT);

struct CpuCtx
{
	CpuEntry pfEntry;
	void* pParam;

	HANDLE pfTask;		///< CPU entry point.
	CpuEvt* pEvt;
	uint64 nRunTime;
};



static CpuCtx gaCpu[NUM_CPU];	///< CPU(FW) information.
static CpuID gnCurCpu;		///< Current running CPU.

inline void sim_SwitchToCpu(uint32 nCpu)
{
#if EN_COROUTINE
	CO_Switch(nCpu);
#else
	SwitchToFiber(gaCpu[nCpu].pfTask);
#endif
}

/**
CPU execution entry.
*/
void sim_CpuHandler(void* pEvt)
{
	CpuEvt* pCurEvt = (CpuEvt*)pEvt;
	CpuID nCpuId = pCurEvt->eCpuId;
	gnCurCpu = nCpuId;
	CpuCtx* pCpuCtx = gaCpu + nCpuId;
	assert(pEvt == pCpuCtx->pEvt);
	//	printf("END %d, %X\n", gnCurCpu, pCurEvt);
	pCpuCtx->pEvt = nullptr;
	sim_SwitchToCpu(nCpuId);
}

void CPU_Add(CpuID eID, CpuEntry pfEntry, void* pParam)
{
	SIM_AddHW(HW_CPU, sim_CpuHandler);
	gaCpu[eID].pfEntry = pfEntry;
	gaCpu[eID].pParam = pParam;
}


void CPU_TimePass(uint32 nTick)
{
	CpuEvt* pEvt = (CpuEvt*)SIM_NewEvt(HW_CPU, nTick);
	pEvt->eCpuId = gnCurCpu;
	CpuCtx* pCpuCtx = gaCpu + gnCurCpu;
	pCpuCtx->pEvt = pEvt;
	pCpuCtx->nRunTime += nTick;
	//	printf("NEW %d, %X\n", gnCurCpu, pEvt);
	gnCurCpu = NUM_CPU;
	SIM_SwitchToEngine();
}

/**
* CPU wait상태...
*/
void CPU_Sleep()
{
	assert(nullptr == gapCpuEvt[gnCurCpu]);
	gnCurCpu = NUM_CPU;
	SIM_SwitchToEngine();
}

/**
* ISR 등에서 CPU를 깨우는 함수.
*/
void CPU_Wakeup(CpuID eCpu)
{
	CpuCtx* pCtx = gaCpu + eCpu;
	if (nullptr == pCtx->pEvt) // if sleep state.
	{
		CpuEvt* pEvt = (CpuEvt*)SIM_NewEvt(HW_CPU, 0);
		pEvt->eCpuId = eCpu;
		pCtx->pEvt = pEvt;
	}
}

CpuID CPU_GetCpuId()
{
	return gnCurCpu;
}

#define CPU_STACK_SIZE			(4096)

void CPU_Start()
{
	for (uint32 nIdx = 0; nIdx < CpuID::NUM_CPU; nIdx++)
	{
#if EN_COROUTINE
		gnCurCpu = (CpuID)nIdx;
		CO_Start(nIdx, gaCpu[nIdx].pfEntry);
#else
		if (nullptr != gaCpu[nIdx].pfTask)
		{
			DeleteFiber(gaCpu[nIdx].pfTask);
			gaCpu[nIdx].nRunTime = 0;
		}
		gaCpu[nIdx].pfTask = CreateFiber(CPU_STACK_SIZE,
			(LPFIBER_START_ROUTINE)gaCpu[nIdx].pfEntry, gaCpu[nIdx].pParam);

		CpuEvt* pEvt = (CpuEvt*)SIM_NewEvt(HW_CPU, 0);
		gaCpu[nIdx].pEvt = pEvt;
		pEvt->eCpuId = (CpuID)nIdx;
#endif
	}
}
