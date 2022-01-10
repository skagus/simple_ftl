
#include <queue>
#include <list>
#include <random>

#include "templ.h"
#include "sim.h"
#include "cpu.h"

using namespace std;

#define NUM_EVENT				(NUM_HW * 2)	// NFC는 Die개수에 비례하는 값이 필요함.


struct Evt
{
public:
	uint64	nTick;			///< Event발생할 시간.
	HwID	nOwner;			///< Event발생한 HW.
	uint32	nSeqNo;
	uint8	aParams[BYTE_PER_EVT];		///< Event information for each HW.

	/// sorting을 위해서 comparator필요함.
	bool operator()(const Evt* lhs, const Evt* rhs) const
	{
		return lhs->nTick > rhs->nTick;
	}
};


#if (0 == EN_COROUTINE)
static HANDLE ghEngine;		///< Engine용 Task.
#endif
static uint64 gnTick;			///< Simulation time.
static EvtHdr gfEvtHdr[NUM_HW];	///< HW별 Event handler.
static bool gbPowerOn;			///< Power on state.
static uint32 gnCycle;

/// Event repository.
static Evt gaEvts[NUM_EVENT];
static std::priority_queue<Evt*, std::vector<Evt*>, Evt> gEvtQue;
static Queue<Evt*, NUM_EVENT + 1> gEvtPool;

void sim_CpuHandler(void* pEvt);

/**
HW는 event driven으로만 동작하며,
SIM에 Event handler를 등록한다.
*/
void SIM_AddHW(HwID id, EvtHdr pfEvtHandler)
{
	gfEvtHdr[id] = pfEvtHandler;
}


void* SIM_NewEvt(HwID eOwn, uint32 nTick)
{
	Evt* pEvt = gEvtPool.PopHead();
	pEvt->nTick = gnTick + nTick;
	pEvt->nOwner = eOwn;
	pEvt->nSeqNo = SIM_GetSeqNo();
	gEvtQue.push(pEvt);
	return pEvt->aParams;
}

uint64 SIM_GetTick()
{
	return gnTick;
}

uint32 SIM_GetCycle()
{
	return gnCycle;
}

#define MAX_BUF_SIZE	(128)
void SIM_Print(const char *szFormat, ...)
{
	va_list stAP;
	char aBuf[MAX_BUF_SIZE];
	va_start(stAP, szFormat);
	vsprintf_s(aBuf, MAX_BUF_SIZE, szFormat, stAP);
	va_end(stAP);
	fprintf(stdout, "%8lld: %s", gnTick, aBuf);
}

void SIM_SwitchToEngine()
{
#if EN_COROUTINE
	CO_Yield();
#else
	SwitchToFiber(ghEngine);
#endif
}


/**
nEndTick 이내의 최근 tick의 event를 실행한다.
최근 event중에 가장 빠른 미래의 event를 실행하는데,
일반적으로 한개이지만, 동시에 발생될 event라면 한번에 실행한다.
*/
static void sim_ProcEvt()
{
	assert(!gEvtQue.empty());
	Evt* pEvt = gEvtQue.top();
	gnTick = pEvt->nTick;
	gEvtQue.pop();
	gfEvtHdr[pEvt->nOwner](pEvt->aParams);
	gEvtPool.PushTail(pEvt);
}

/**
Simulation engine을 처음으로 되돌린다.
(즉, simulating event를 모두 clear한다.)
반드시 Simulator fiber에서 호출되어야 한다. 
(권장: HW event에서 호출하면 좋을 듯..)
*/
static void sim_PowerUp()
{
	gbPowerOn = true;
	while (gEvtQue.size())
	{
		gEvtQue.pop();
	}
	gEvtPool.Init();
	for (uint32 nIdx = 0; nIdx < NUM_EVENT; nIdx++)
	{
		gEvtPool.PushTail(gaEvts + nIdx);
	}
	gnTick = 0;
	CPU_Start();
}

void SIM_PowerDown()
{
	gbPowerOn = false;
}

/**
sim의 실행 방법은, 
여러 HW의 시간을 나타내는 Event Queue의 가장 작은 시간과,
여러 CPU의 누적 실행 시간중 가장 작은 놈을 실행하는 구조로 동작한다.
*/
void SIM_Run()
{
#if (0 == EN_COROUTINE)
	ghEngine = ConvertThreadToFiber(nullptr);
#endif
	SIM_UtilInit();

#if	EN_BENCHMARK
	LARGE_INTEGER stBegin;
	QueryPerformanceCounter(&stBegin);
#endif
	uint32 nCnt = 10;
	while (nCnt-- > 0)
	{
		sim_PowerUp();
		SIM_Print("[SIM] ============== Power up %d =================\n", gnCycle);
		while (gbPowerOn)
		{
			sim_ProcEvt();	// 내부에서 gnHwTick을 update한다.
		}
		gnCycle++;
	}
#if	EN_BENCHMARK
	LARGE_INTEGER stEnd;
	QueryPerformanceCounter(&stEnd);
	printf("Time: %lld\n", stEnd.QuadPart - stBegin.QuadPart);
#endif
}
