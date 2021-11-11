#include "buf.h"
#include "nfc.h"
#include "templ.h"

#define NUM_BUF		(128)

uint8 gaMBuf[NUM_BUF][BYTE_PER_CHUNK];
uint8 gaSBuf[NUM_BUF][BYTE_PER_SPARE];
bool gbAlloc[NUM_BUF];
Queue<uint16, NUM_BUF + 1> gFreeQue;

uint8* BM_GetMain(uint16 nBufId)
{
	assert(gbAlloc[nBufId]);
	return gaMBuf[nBufId];
}


uint8* BM_GetSpare(uint16 nBufId)
{
	assert(gbAlloc[nBufId]);
	return gaSBuf[nBufId];
}

uint16 BM_CountFree()
{
	return gFreeQue.Count();
}

uint16 BM_Alloc()
{
	assert(gFreeQue.Count() > 0);
	uint16 nBuf = gFreeQue.PopHead();
	assert(gbAlloc[nBuf] == false);
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

