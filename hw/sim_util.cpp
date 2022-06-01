
#include <random>

#include "types.h"
#include "sim.h"

static std::mt19937_64 gRand;		///< Random number generator.
static uint32 gnSeqNo;			///< Sequence number for debug.
static FILE* fpLog;

uint32 SIM_GetRand(uint32 nMod)
{
	return gRand() % nMod;
}


uint32 SIM_GetSeqNo()
{
	gnSeqNo++;
	if ((0 == gnSeqNo) && (gnSeqNo != 0))
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

void SIM_UtilInit()
{
	gnSeqNo = 0;
	gRand.seed(10);
	fopen_s(&fpLog, "sim.log", "w");
}

