
#include "sim.h"
#include "cpu.h"

struct CpuEvt
{
	uint32 nCpuId;
	uint32 nAddTick;	///< Additional processing time by ISR.
};

static_assert(sizeof(CpuEvt) < BYTE_PER_EVT);
static_assert(MAX_ROUTINE >= NUM_CPU);

struct CpuCtx
{
	CpuEvt* pEvt;
	uint64 nRunTime;
};

static CpuCtx gaCpu[NUM_CPU];	///< CPU(FW) information.
uint32 gnCurCpu;		///< Current running CPU.

static CpuEvt* cpu_NewEvent(uint32 nTick, uint32 nCpu)
{
	CpuEvt* pEvt = (CpuEvt*)SIM_NewEvt(HW_CPU, nTick);
	pEvt->nCpuId = nCpu;
	pEvt->nAddTick = (uint32)0;
	return pEvt;
}

/**
CPU execution entry.
*/
void cpu_HandleEvt(void* pEvt)
{
	CpuEvt* pCurEvt = (CpuEvt*)pEvt;
	uint32 nCpuId = pCurEvt->nCpuId;
	CpuCtx* pCpuCtx = gaCpu + nCpuId;
	assert(pEvt == pCpuCtx->pEvt);
	if (true) // 0 == pCurEvt->nAddTick)
	{
		//	printf("END %d, %X\n", gnCurCpu, pCurEvt);
		pCpuCtx->pEvt = nullptr;
		gnCurCpu = nCpuId;
		CO_Switch(nCpuId);
	}
	else
	{
		// Prospond cpu run if additional tick is.
		// additional tick is added by ISR called by HW.
		pCpuCtx->pEvt = cpu_NewEvent(pCurEvt->nAddTick, nCpuId);
	}
}

void CPU_Add(uint32 eID, Routine pfEntry, void* pParam)
{
	SIM_AddHW(HW_CPU, cpu_HandleEvt);
	CO_RegTask(eID, pfEntry, pParam);
}

void CPU_TimePass(uint32 nTick)
{
	uint32 nCpu = gnCurCpu;
	CpuCtx* pCpuCtx = gaCpu + nCpu;
	pCpuCtx->nRunTime += nTick;
	if (NOT(SIM_PeekTick(nTick)))
	{
		pCpuCtx->pEvt = cpu_NewEvent(nTick, nCpu);
		//	printf("NEW %d, %X\n", gnCurCpu, pEvt);
		gnCurCpu = NUM_CPU;
		CO_ToMain();
		ASSERT(gnCurCpu == nCpu);
	}
}

/**
* Makes CPU as wait interrupt.(wakeup)
*/
void CPU_Sleep()
{
	assert(nullptr == gaCpu[gnCurCpu].pEvt);
	gnCurCpu = NUM_CPU;
	CO_ToMain();
}

/**
* Wakeup CPU on ISR or something.
*/
void CPU_Wakeup(uint32 nCpu, uint32 nAddTick)
{
	CpuCtx* pCtx = gaCpu + nCpu;
	if (nullptr == pCtx->pEvt) // if sleep state.
	{
		pCtx->pEvt = cpu_NewEvent(nAddTick, nCpu);
	}
	else
	{
		pCtx->pEvt->nAddTick += nAddTick;
	}
}

uint32 CPU_GetCpuId()
{
	return gnCurCpu;
}


void CPU_Start()
{
	CO_Start();
	for (uint32 nIdx = 0; nIdx < NUM_CPU; nIdx++)
	{
		gaCpu[nIdx].pEvt = cpu_NewEvent(0, nIdx);
		gaCpu[nIdx].nRunTime = 0;
	}
}

/**
* Need to Coroutine initialize.
*/
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
	for(uint32 nIdx = 0; nIdx < NUM_CPU; nIdx++)
	{
		CO_RegTask(nIdx, dummy_Routine, nullptr);
	}
}
