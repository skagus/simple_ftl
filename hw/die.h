#pragma once

#include "types.h"
#include "nfc.h"

enum NErr
{
	OK,
	UECC,
	ERASED,
};

class Die
{
public:
	virtual bool DoErase(uint16 anBBN[NUM_PLN], uint8 bmPln) = 0;
	virtual bool DoProg(uint16 anBBN[NUM_PLN], uint32 nWL, uint8 bmPln, uint8* pbmFail) = 0;
	virtual void DoRead(uint16 anBBN[NUM_PLN], uint32 nWL, uint8 bmPln) = 0;
	virtual void DataIn(uint8 nOffset, uint8* pMainBuf, uint8* pSpare, bool bFull = true) = 0;
	virtual NErr DataOut(uint8 nOffset, uint8* pMainBuf, uint8* pSpare, bool* pbFull = nullptr) = 0;
	virtual void Reset() = 0;
};

extern Die* gaDies[NUM_DIE];

void DIE_InitSim();
