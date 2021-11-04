#include "types.h"
#include "config.h"
#include "nfc.h"
#include "ftl.h"
#include "buf.h"


static bool gbDone;

CmdInfo* io_GetDone()
{
	while (false == gbDone)
	{
		SIM_CpuTimePass(100);
	}
	gbDone = false;
	return NFC_GetDone();
}

void io_CbDone(uint32 nDie, uint32 nTag)
{
	gbDone = true;
}

void io_Read(uint16 nPBN, uint16 nPage, uint16 nBufId)
{
	CmdInfo stCmd;
	stCmd.eCmd = NCmd::NC_READ;
	stCmd.nDie = 0;
	stCmd.nWL = nPage;
	stCmd.anBBN[0] = nPBN / NUM_PLN;
	stCmd.bmPln = BIT(nPBN % NUM_PLN);
	stCmd.stRead.bmChunk = 1;
	stCmd.stRead.anBufId[0] = nBufId;
	
	NFC_Issue(&stCmd);
	CmdInfo* pstDone = io_GetDone();
	SIM_CpuTimePass(3);
}

void io_Program(uint16 nPBN, uint16 nPage, uint16 nBufId)
{
	CmdInfo stCmd;
	stCmd.eCmd = NCmd::NC_PGM;
	stCmd.nDie = 0;
	stCmd.nWL = nPage;
	stCmd.anBBN[0] = nPBN / NUM_PLN;
	stCmd.bmPln = BIT(nPBN % NUM_PLN);
	stCmd.stPgm.bmChunk = 1;
	stCmd.stPgm.anBufId[0] = nBufId;

	NFC_Issue(&stCmd);
	CmdInfo* pstDone = io_GetDone();
	SIM_CpuTimePass(3);
}

void io_Erase(uint16 nPBN)
{
	CmdInfo stCmd;
	stCmd.eCmd = NCmd::NC_ERB;
	stCmd.nDie = 0;
	stCmd.nWL = 0;
	stCmd.anBBN[0] = nPBN / NUM_PLN;
	stCmd.bmPln = BIT(nPBN % NUM_PLN);

	NFC_Issue(&stCmd);
	CmdInfo* pstDone = io_GetDone();
	SIM_CpuTimePass(3);
}

