
#include <random>

#include "types.h"

static std::mt19937_64 gRand;		///< Random number generator.
static uint32 gnSeqNo;			///< Sequence number for debug.


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


void SIM_UtilInit()
{
	gnSeqNo = 0;
	gRand.seed(10);
}
