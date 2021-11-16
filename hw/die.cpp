#include <string.h>
#include "macro.h"
#include "die.h"

struct Chunk
{
	uint8 aMain[BYTE_PER_CHUNK];
	uint8 aSpare[BYTE_PER_SPARE];
	void Store(uint8* pMain, uint8* pSpare)
	{
		memcpy(aMain, pMain, sizeof(aMain));
		memcpy(aSpare, pSpare, sizeof(aSpare));
	}
	void Load(uint8* pMain, uint8* pSpare)
	{
		memcpy(pMain, aMain, sizeof(aMain));
		memcpy(pSpare, aSpare, sizeof(aSpare));
	}
};

struct WordLine
{
	bool bPgm;
	Chunk aChunk[CHUNK_PER_PPG];
};

struct PBlk
{
	uint32 nNextPgm;	///< Erased index.(for debugging)
	WordLine astWL[NUM_WL];
	void Erase()
	{
		nNextPgm = 0;
		MEMSET_ARRAY(astWL, 0x00);
	}
};

class MyDie : public Die
{
	Chunk* ap4DOut[NUM_PLN][CHUNK_PER_PPG];	///< Out용은 pointer만 set.
	Chunk a4DIn[NUM_PLN][CHUNK_PER_PPG];		///< In은 Data를 받을 수 있도록...

	PBlk astPBlk[PBLK_PER_DIE];

public:
	MyDie()
	{
		MEMSET_ARRAY(ap4DOut, 0);
		MEMSET_ARRAY(a4DIn, 0);
		MEMSET_ARRAY(astPBlk, 0);
	}

	bool DoErase(uint16 anBBN[NUM_PLN], uint8 bmPln) override
	{
		for (uint32 nPln = 0; nPln < NUM_PLN; nPln++)
		{
			if (BIT(nPln) & bmPln)
			{
				uint32 nPBN = anBBN[nPln] * NUM_PLN + nPln;
				astPBlk[nPBN].Erase();
			}
		}
		return true;
	}

	void DataIn(uint8 nOffset, uint8* pMain, uint8* pSpare, bool bFull) override
	{
		a4DIn[nOffset / CHUNK_PER_PPG][nOffset % CHUNK_PER_PPG].Store(pMain, pSpare);
		return;
	}

	bool DoProg(uint16 anBBN[NUM_PLN], uint32 nWL, uint8 bmPln, uint8* pbmFail) override
	{
		for (uint32 nPln = 0; nPln < NUM_PLN; nPln++)
		{
			if (BIT(nPln) & bmPln)
			{
				uint32 nPBN = anBBN[nPln] * NUM_PLN + nPln;
				WordLine* pWL = astPBlk[nPBN].astWL + nWL;
				assert(astPBlk[nPBN].nNextPgm <= nWL);
				for (uint8 nChunk = 0; nChunk < CHUNK_PER_PPG; nChunk++)
				{
					pWL->aChunk[nChunk] = a4DIn[nPln][nChunk];
				}
				pWL->bPgm = true;
				astPBlk[nPBN].nNextPgm = nWL + 1;
			}
		}
		return true;
	}

	void DoRead(uint16 anBBN[NUM_PLN], uint32 nWL, uint8 bmPln) override
	{
		for (uint32 nPln = 0; nPln < NUM_PLN; nPln++)
		{
			WordLine* pWL = nullptr;
			bool bReady = false;
			if (BIT(nPln) & bmPln)
			{
				uint32 nPBN = anBBN[nPln] * NUM_PLN + nPln;
				pWL = astPBlk[nPBN].astWL + nWL;
				bReady = pWL->bPgm;
			}
			if (bReady)
			{
				for (uint8 nChunk = 0; nChunk < CHUNK_PER_PPG; nChunk++)
				{
					ap4DOut[nPln][nChunk] = pWL->aChunk + nChunk;
				}
			}
			else
			{
				for (uint8 nChunk = 0; nChunk < CHUNK_PER_PPG; nChunk++)
				{
					ap4DOut[nPln][nChunk] = nullptr;
				}
			}
		}
		return;
	}

	NErr DataOut(uint8 nOffset, uint8* pMain, uint8* pSpare, bool* pbFull) override
	{
		Chunk* pChunk = ap4DOut[nOffset / CHUNK_PER_PPG][nOffset % CHUNK_PER_PPG];
		if (nullptr == pChunk)
		{
			memset(pMain, 0xFF, BYTE_PER_CHUNK);
			memset(pSpare, 0xFF, BYTE_PER_SPARE);
			return NErr::ERASED;
		}

		pChunk->Load(pMain, pSpare);
		return NErr::OK;
	}

	void Reset() override
	{
		MEMSET_ARRAY(ap4DOut, 0);
		MEMSET_ARRAY(a4DIn, 0);
	}
};

Die* gaDies[NUM_DIE];

void DIE_InitSim()
{
	for (uint8 nDie = 0; nDie < NUM_DIE; nDie++)
	{
		gaDies[nDie] = new MyDie();
	}
}
