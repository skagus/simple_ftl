
#include <queue>
#include <list>
#include <random>

#include "templ.h"
#include "sim.h"
#include "sim_hw.h"
#include "cpu.h"	// for CPU_Start()

using namespace std;

/**
* Normally, one Event for each HW. (caution: count instance! NOT type)
*/
#define NUM_EVENT				(NUM_HW * 2)


struct Evt
{
public:
	uint64	nTick;			///< Time of event occur.
	HwID	nOwner;			///< HW of the event.
	uint32	nSeqNo;
	uint8	aParams[BYTE_PER_EVT];		///< Event information for each HW.

	/**
		Event queue must be sorted with time(tick) 
	*/
	bool operator()(const Evt* lhs, const Evt* rhs) const
	{
		return lhs->nTick > rhs->nTick;
	}
};


static uint64 gnTick;			///< Simulation time.
static EvtHdr gfEvtHdr[NUM_HW];	///< Event handler of each HW.
static bool gbPowerOn;			///< Power on state.
static uint32 gnCycle;
static HANDLE ghSimThread;

/// Event repository.
static Evt gaEvts[NUM_EVENT];
static std::priority_queue<Evt*, std::vector<Evt*>, Evt> gEvtQue;
static Queue<Evt*, NUM_EVENT + 1> gEvtPool;

/**
* Add Hardware. 
* HW is defined by handler.
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

/**
* Check whether it can bypass tick & then bypass tick.
* @return true if peek the tick.
*/
bool SIM_PeekTick(uint32 nTick)
{
	Evt* pEvt = gEvtQue.top();
	if (gnTick + nTick < pEvt->nTick)
	{
		gnTick += nTick;
		return true;
	}
	return false;
}

/**
Initiate system. (aka. power on)
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

void SIM_SwitchToSim()
{
	SwitchToFiber(ghSimThread);
}


void SIM_PowerDown()
{
	gbPowerOn = false;
}


void SIM_Run()
{
	ghSimThread = ConvertThreadToFiber(nullptr);
	while (true)
	{
		sim_PowerUp();
		SIM_Print("[SIM] ============== Power up %d =================\n", gnCycle);
		while (gbPowerOn)
		{
			assert(!gEvtQue.empty());
			Evt* pEvt = gEvtQue.top();
			gnTick = pEvt->nTick;
			gEvtQue.pop();
			gfEvtHdr[pEvt->nOwner](pEvt->aParams);
			gEvtPool.PushTail(pEvt);
		}
		gnCycle++;
	}
}


////////////////////////////////////////


static std::mt19937_64 gRand;		///< Random number generator.
static uint32 gnSeqNo;			///< Sequence number for debug.
static uint32 gnBrkSN;
static FILE* fpLog;

uint32 SIM_GetRand(uint32 nMod)
{
	return gRand() % nMod;
}


uint32 SIM_GetSeqNo()
{
	gnSeqNo++;
	if (gnBrkSN == gnSeqNo)
	{
		__debugbreak();
	}
	return gnSeqNo;
}

#define MAX_BUF_SIZE	(128)
void SIM_Print(const char* szFormat, ...)
{
	va_list stAP;
	char aBuf[MAX_BUF_SIZE];
	va_start(stAP, szFormat);
	vsprintf_s(aBuf, MAX_BUF_SIZE, szFormat, stAP);
	va_end(stAP);
	fprintf(stdout, "%8lld: %s", SIM_GetTick(), aBuf);
	fprintf(fpLog, "%8lld: %s", SIM_GetTick(), aBuf);
	fflush(fpLog);
}

void SIM_Init(uint32 nSeed, uint32 nBrkNo)
{
	gnSeqNo = 0;
	gnBrkSN = nBrkNo;
	gRand.seed(nSeed);

	time_t cur;
	time(&cur);
	struct tm tm2;
	localtime_s(&tm2, &cur);
	char szName[20];
	sprintf_s(szName, 20, "sim_%02d%02d%02d.log", tm2.tm_hour, tm2.tm_min, tm2.tm_sec);
	//	fopen_s(&fpLog, szName, "w");
	fpLog = _fsopen(szName, "w", _SH_DENYWR);
}

