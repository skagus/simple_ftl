#include <queue>
#include <list>
#include <random>

#include "sim.h"

using namespace std;

#define NUM_EVENT				(NUM_HW * 2)	// HW의 개수에 비례하도록...
#define CPU_STACK_SIZE			(4096)

struct CpuContext
{
	CpuEntry pfEntry;
	void* pParam;

	HANDLE pfTask;		///< CPU entry point.
	uint64 nRunTime;	///< Run time accumulation.
};

struct Evt
{
public:
	uint64	nTick;			///< Event발생할 시간.
	HwID	nOwner;			///< Event발생한 HW.
	uint8	params[10];		///< Event information for each HW.

	/// sorting을 위해서 comparator필요함.
	bool operator()(const Evt* lhs, const Evt* rhs) const
	{
		return lhs->nTick > rhs->nTick;
	}
};

CpuContext gaCpu[NUM_CPU];
uint32 gnCurCpu;		///< Current running CPU.

HANDLE ghEngine;		///< Engine용 Task.
uint64 gnHwTick;		///< HW tick time.
EvtHdr gfEvtHdr[NUM_HW];	///< HW별 Event handler.
bool gbPowerOn;			///< Power on state.

mt19937_64* gpRand;

/// Event repository.
static Evt gaEvts[NUM_EVENT];
static std::priority_queue<Evt*, std::vector<Evt*>, Evt> gEvtQue;
static list<Evt*> gEvtPool;

/**
HW는 event driven으로만 동작하며,
SIM에 Event handler를 등록한다.
*/
void SIM_AddHW(HwID id, EvtHdr pfEvtHandler)
{
	gfEvtHdr[id] = pfEvtHandler;
}

void SIM_AddCPU(CpuID eID, CpuEntry pfEntry, void* pParam)
{
	gaCpu[eID].pfEntry = pfEntry;
	gaCpu[eID].pParam = pParam;
}

void sim_StartCPU()
{
	for (uint32 nIdx = 0; nIdx < CpuID::NUM_CPU; nIdx++)
	{
		if (nullptr != gaCpu[nIdx].pfTask)
		{
			DeleteFiber(gaCpu[nIdx].pfTask);
			gaCpu[nIdx].nRunTime = 0;
		}
		gaCpu[nIdx].pfTask = CreateFiber(CPU_STACK_SIZE, 
			(LPFIBER_START_ROUTINE)gaCpu[nIdx].pfEntry, gaCpu[nIdx].pParam);
	}
}

void* SIM_NewEvt(HwID eOwn, uint32 nTick)
{
	Evt* evt = gEvtPool.front();
	gEvtPool.pop_front();
	evt->nTick = gnHwTick + nTick;
	evt->nOwner = eOwn;

	gEvtQue.push(evt);
	return evt->params;
}

uint64 SIM_GetTick()
{
	return gnHwTick;
}

#define MAX_BUF_SIZE	(128)
void SIM_Print(const char *szFormat, ...)
{
	va_list stAP;
	char aBuf[MAX_BUF_SIZE];
	va_start(stAP, szFormat);
	vsprintf_s(aBuf, MAX_BUF_SIZE, szFormat, stAP);
	va_end(stAP);
	fprintf(stdout, "%8lld: %s", gnHwTick, aBuf);
}

uint32 SIM_GetRand(uint32 nMod)
{
	return (*gpRand)() % nMod;
}

inline void sim_Switch(HANDLE hFiber)
{
	SwitchToFiber(hFiber);
}

void SIM_CpuTimePass(uint32 nTick)
{
	gaCpu[gnCurCpu].nRunTime += nTick;
	if (gaCpu[gnCurCpu].nRunTime > gnHwTick)
	{
		sim_Switch(ghEngine);
	}
}

static uint64 sim_GetMinCpuTime()
{
	uint64 nTime = (uint64)(-1);
	for (uint32 nCpu = 0; nCpu < NUM_CPU; nCpu++)
	{
		if (gaCpu[nCpu].nRunTime <= nTime)
		{
			nTime = gaCpu[nCpu].nRunTime;
		}
	}
	return nTime;
}

/**
nEndTick 이내의 최근 tick의 event를 실행한다.
최근 event중에 가장 빠른 미래의 event를 실행하는데,
일반적으로 한개이지만, 동시에 발생될 event라면 한번에 실행한다.
*/
static void sim_ProcEvt(uint64 nEndTick)
{
	gnHwTick = nEndTick;
	while (!gEvtQue.empty())
	{
		Evt* evt = gEvtQue.top();
		if (evt->nTick > gnHwTick)
		{
			break;
		}
		gnHwTick = evt->nTick;
		gEvtQue.pop();
		gfEvtHdr[evt->nOwner](evt->params);
		gEvtPool.push_front(evt);
	}
}

/**
Simulation engine을 처음으로 되돌린다.
(즉, simulating event를 모두 clear한다.)
반드시 Simulator fiber에서 호출되어야 한다. 
(권장: HW event에서 호출하면 좋을 듯..)
*/
void SIM_Reset()
{
	gbPowerOn = false;
	gEvtPool.resize(0);
	while (gEvtQue.size())
	{
		gEvtQue.pop();
	}
	for (int i = 0; i < NUM_EVENT; i++)
	{
		gEvtPool.push_back(gaEvts + i);
	}
	gnHwTick = 0;
	sim_StartCPU();
}

/**
sim의 실행 방법은, 
여러 HW의 시간을 나타내는 Event Queue의 가장 작은 시간과,
여러 CPU의 누적 실행 시간중 가장 작은 놈을 실행하는 구조로 동작한다.
*/
void SIM_Run()
{
	ghEngine = ConvertThreadToFiber(nullptr);
	gpRand = new mt19937_64();
	SIM_Reset();

	while (true)
	{
		gbPowerOn = true;
		while (gbPowerOn)
		{
			for (uint32 nCpu = 0; nCpu < NUM_CPU; nCpu++)
			{
				gnCurCpu = nCpu;
				while (gaCpu[nCpu].nRunTime <= gnHwTick)
				{	// HW 시간까지 계속 실행함.
					sim_Switch(gaCpu[nCpu].pfTask);
				}
			}
			uint64 nCpuTick = sim_GetMinCpuTime();
			sim_ProcEvt(nCpuTick);	// 내부에서 gnHwTick을 update한다.
		}
	}
}

///////////// Example //////////////////
#if 0

struct DummyEvt
{
	uint64 nTick;
	HwID nId;
};

void cpu_Entry0(void* pParam)
{
	DummyEvt* pEvt = (DummyEvt*)SIM_NewEvt(HW_NFC, 9);
	pEvt->nTick = SIM_GetTick();
	pEvt->nId = HW_NFC;

	for (uint32 i = 0; i < 100; i++)
	{
		SIM_Print("Cpu0\n");
		SIM_CpuTimePass(1);
	}
	while (true)
	{
		SIM_CpuTimePass(100000);
	}
}

void cpu_Entry1(void* pParam)
{
	DummyEvt* pEvt = (DummyEvt*)SIM_NewEvt(HW_NAND, 20);
	pEvt->nTick = SIM_GetTick();
	pEvt->nId = HW_NAND;

	for (uint32 i = 0; i < 10; i++)
	{
		SIM_Print("Cpu1\n");
		SIM_CpuTimePass(10);
	}
	while (true)
	{
		SIM_CpuTimePass(100000);
	}
}

void hw_Handle(void* pEvt)
{
	DummyEvt* pstCurEvt = (DummyEvt*)pEvt;
	SIM_Print("HW %d Issued:%d\n", pstCurEvt->nId, pstCurEvt->nTick);
	DummyEvt* pNewEvt = (DummyEvt*)SIM_NewEvt(pstCurEvt->nId, rand() % 20);
	pNewEvt->nTick = SIM_GetTick();
	pNewEvt->nId = pstCurEvt->nId;
}

int main()
{
	SIM_Reset();

	SIM_AddCPU(CPU_FIL, cpu_Entry0, (void*)3);
	SIM_AddCPU(CPU_FTL, cpu_Entry1, (void*)4);

	SIM_AddHW(HW_NFC, hw_Handle);
	SIM_AddHW(HW_NAND, hw_Handle);

	SIM_Run();

	return 0;
}

#endif

