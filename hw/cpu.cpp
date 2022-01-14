
#include "sim.h"
#include "cpu.h"

struct CpuEvt
{
	CpuID eCpuId;
};

static_assert(sizeof(CpuEvt) < BYTE_PER_EVT);
static_assert(MAX_ROUTINE >= NUM_CPU);

struct CpuCtx
{
	CpuEvt* pEvt;
	uint64 nRunTime;
};

static CpuCtx gaCpu[NUM_CPU];	///< CPU(FW) information.
CpuID gnCurCpu;		///< Current running CPU.

/**
CPU execution entry.
*/
void cpu_HandleEvt(void* pEvt)
{
	CpuEvt* pCurEvt = (CpuEvt*)pEvt;
	CpuID nCpuId = pCurEvt->eCpuId;
	gnCurCpu = nCpuId;
	CpuCtx* pCpuCtx = gaCpu + nCpuId;
	assert(pEvt == pCpuCtx->pEvt);
	//	printf("END %d, %X\n", gnCurCpu, pCurEvt);
	pCpuCtx->pEvt = nullptr;
	CO_Switch(nCpuId);
}

void CPU_Add(CpuID eID, Routine pfEntry, void* pParam)
{
	SIM_AddHW(HW_CPU, cpu_HandleEvt);
	CO_RegTask(eID, pfEntry, pParam);
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
	CO_ToMain();
}

/**
* CPU wait상태...
*/
void CPU_Sleep()
{
	assert(nullptr == gaCpu[gnCurCpu].pEvt);
	gnCurCpu = NUM_CPU;
	CO_ToMain();
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


void CPU_Start()
{
	CO_Start();
	for (uint32 nIdx = 0; nIdx < CpuID::NUM_CPU; nIdx++)
	{
		CpuEvt* pEvt = (CpuEvt*)SIM_NewEvt(HW_CPU, 0);
		gaCpu[nIdx].pEvt = pEvt;
		gaCpu[nIdx].nRunTime = 0;
		pEvt->eCpuId = (CpuID)nIdx;
	}
}

void dummy_Routine(void* pParam)
{
	while (true)
	{
		CPU_Sleep();
		SIM_Print("Dummy CPU woken up!!!\n");
	}
	END_RUN
}

void CPU_InitSim()
{
	// SET_JMP의 경우 처음 시작할 때, 1회 호출된다.
	for(uint32 nIdx = 0; nIdx < NUM_CPU; nIdx++)
	{
		CO_RegTask(nIdx, dummy_Routine, nullptr);
	}
}
