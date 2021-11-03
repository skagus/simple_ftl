#pragma once
#include "sim.h"
#include "config.h"

#define TIME_CMD_GAB			(3)	///< Command간의 시간차.
#define TIME_CMD_ISSUE			(5)
#define TIME_CHUNK_DMA			(30)
#define TIME_READ_BUSY			(60)
#define TIME_PGM_BUSY			(800)
#define TIME_ERASE_BUSY			(1000)

enum NCmd
{
	NC_READ,
	NC_PGM,
	NC_ERB,
};

struct NAddr
{
	uint32 nDie;
	uint32 nBlk;
	uint32 nWL;
};

struct PgmInfo
{
	uint32 bmChunk;
	uint16 anBufId[CHUNK_PER_BPG];
	uint8 bmErrPln;
};

struct ReadInfo
{
	uint32 bmChunk;
	uint16 anBufId[CHUNK_PER_BPG];
	uint32 bmUECC;
	uint32 bmERS;
};

struct ErbInfo
{
	uint8 bmErrPln;
};

struct CmdInfo
{
	NCmd eCmd;
	uint8 nDie;
	uint16 anBBN[NUM_PLN];
	uint16 nWL;
	uint32 bmPln;	// Address Info.
	union
	{
		PgmInfo stPgm;
		ReadInfo stRead;
		ErbInfo stErb;
	};
};


void NFC_InitSim(); // Sim 전용.

void NFC_Init(CbFunc pfCbfDone);
void NFC_Issue(CmdInfo* pCmd);
CmdInfo* NFC_GetDone();
