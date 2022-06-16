
#include <random>

#include "types.h"
#include "sim.h"

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

void SIM_UtilInit(uint32 nSeed, uint32 nBrkNo)
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

