
#include "templ.h"
#include "config.h"
#include "macro.h"
#include "buf.h"

#define NUM_BUF		(128)

uint8 gaMBuf[NUM_BUF][BYTE_PER_CHUNK];
Spare gaSBuf[NUM_BUF];
bool gbAlloc[NUM_BUF];
Queue<uint16, NUM_BUF + 1> gFreeQue;

uint8* BM_GetMain(uint16 nBufId)
{
	ASSERT(gbAlloc[nBufId]);
	return gaMBuf[nBufId];
}


Spare* BM_GetSpare(uint16 nBufId)
{
	ASSERT(gbAlloc[nBufId]);
	return gaSBuf + nBufId;
}

uint16 BM_CountFree()
{
	return gFreeQue.Count();
}

uint16 BM_Alloc()
{
	ASSERT(gFreeQue.Count() > 0);
	uint16 nBuf = gFreeQue.PopHead();
	ASSERT(gbAlloc[nBuf] == false);
	gbAlloc[nBuf] = true;
	return nBuf;
}

void BM_Free(uint16 nBuf)
{
	assert(gbAlloc[nBuf] == true);
	gFreeQue.PushTail(nBuf);
	gbAlloc[nBuf] = false;
}

void BM_Init()
{
	MEMSET_ARRAY(gbAlloc, 0);
	gFreeQue.Init();
	for (uint16 nBuf = 0; nBuf < NUM_BUF; nBuf++)
	{
		gFreeQue.PushTail(nBuf);
	}
}

