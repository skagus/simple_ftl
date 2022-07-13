#include "types.h"
#include "templ.h"
#include "config.h"
#include "nfc.h"
#include "buf.h"
#include "os.h"
#include "ftl.h"
#include "io.h"

#define PRINTF			SIM_Print

CmdInfo gaCmds[NUM_NAND_CMD];
CbKey gaKeys[NUM_NAND_CMD];
LinkedQueue<CmdInfo> gNCmdPool;
IoCbf gaCbf[NUM_IOCB];
LinkedQueue<CmdInfo> gaDone[NUM_IOCB];
bool gabStop[NUM_IOCB];

const char* gaIoName[NUM_IOCB] = { "UR", "UW", "OP", "MT", "GC", "EB" };	// to print.

CmdInfo* IO_PopDone(CbKey eCbId)
{
	CmdInfo* pRet = gaDone[eCbId].PopHead();
	return pRet;
}

CmdInfo* IO_GetDone(CbKey eCbId)
{
	CmdInfo* pRet = gaDone[eCbId].GetHead();
	return pRet;
}


void io_Print(CmdInfo* pCmd)
{
	uint8 nId = pCmd - gaCmds;
	switch (pCmd->eCmd)
	{
		case NCmd::NC_ERB:
		{
			PRINTF("[IO:%X] %s E {%X}\n",
				pCmd->nDbgSN, gaIoName[gaKeys[nId]],
				pCmd->anBBN[0]);
			break;
		}
		case NCmd::NC_READ:
		{
			Spare* pSpare = BM_GetSpare(pCmd->stRead.anBufId[0]);
			PRINTF("[IO:%X] %s R {%X,%X} SPR [%X,%X]\n", 
				pCmd->nDbgSN, gaIoName[gaKeys[nId]],
				pCmd->anBBN[0], pCmd->nWL, pSpare->Com.nDW0, pSpare->Com.nDW1);
			break;
		}
		case NCmd::NC_PGM:
		{
			Spare* pSpare = BM_GetSpare(pCmd->stPgm.anBufId[0]);
			PRINTF("[IO:%X] %s P {%X,%X} SPR [%X,%X]\n", 
				pCmd->nDbgSN, gaIoName[gaKeys[nId]],
				pCmd->anBBN[0], pCmd->nWL, pSpare->Com.nDW0, pSpare->Com.nDW1);
			break;
		}
		default:
		{
			assert(false);
		}
	}
}

void io_CbDone(uint32 nDie, uint32 nTag)
{
	CmdInfo* pRet = NFC_GetDone();
	if (nullptr != pRet)
	{
//		io_Print(pRet);

		uint8 nId = pRet - gaCmds;
		uint8 nTag = gaKeys[nId];
		gaDone[nTag].PushTail(pRet);
		OS_AsyncEvt(BIT(EVT_NAND_CMD));
	}
}

void IO_Free(CmdInfo* pCmd)
{
	gaKeys[pCmd - gaCmds] = NUM_IOCB;
	gNCmdPool.PushTail(pCmd);
	OS_SyncEvt(BIT(EVT_IO_FREE));
}

CmdInfo* IO_Alloc(CbKey eKey)
{
	while (true == gabStop[eKey])
	{
		OS_Wait(BIT(EVT_IO_FREE), LONG_TIME);
	}
	if (gNCmdPool.Count() > 0)
	{
		CmdInfo* pRet = gNCmdPool.PopHead();
		pRet->nDbgSN = SIM_IncSeqNo();
		gaKeys[pRet - gaCmds] = eKey;
		return pRet;
	}
	return nullptr;
}

uint32 IO_CountFree()
{
	return gNCmdPool.Count();
}

void IO_SetReadBuf(CmdInfo* pstCmd, uint16* anBuf, uint32 bmValid)
{
	pstCmd->stRead.anBufId[0] = anBuf[0];
	pstCmd->stRead.bmChunk = bmValid;
}

void IO_Read(CmdInfo* pstCmd, uint16 nPBN, uint16 nPage, uint32 nTag)
{
	pstCmd->eCmd = NCmd::NC_READ;
	pstCmd->nDie = 0;
	pstCmd->nWL = nPage;
	pstCmd->anBBN[0] = nPBN / NUM_PLN;
	pstCmd->bmPln = BIT(nPBN % NUM_PLN);
	pstCmd->nTag = nTag;

	NFC_Issue(pstCmd);
}

void IO_SetPgmBuf(CmdInfo* pstCmd, uint16* anBufId, uint32 bmValid)
{
	pstCmd->stPgm.anBufId[0] = anBufId[0];
	pstCmd->stPgm.bmChunk = bmValid;
}

void IO_Program(CmdInfo* pstCmd, uint16 nPBN, uint16 nPage, uint32 nTag)
{
	pstCmd->eCmd = NCmd::NC_PGM;
	pstCmd->nDie = 0;
	pstCmd->nWL = nPage;
	pstCmd->anBBN[0] = nPBN / NUM_PLN;
	pstCmd->bmPln = BIT(nPBN % NUM_PLN);
	pstCmd->nTag = nTag;

	NFC_Issue(pstCmd);
}

void IO_Erase(CmdInfo* pstCmd, uint16 nPBN, uint32 nTag)
{
	pstCmd->eCmd = NCmd::NC_ERB;
	pstCmd->nDie = 0;
	pstCmd->nWL = 0;
	pstCmd->anBBN[0] = nPBN / NUM_PLN;
	pstCmd->bmPln = BIT(nPBN % NUM_PLN);
	pstCmd->nTag = nTag;
	NFC_Issue(pstCmd);
}

void IO_SetStop(CbKey eKey, bool bStop)
{
	gabStop[eKey] = bStop;
	if (NOT(bStop))
	{
		OS_SyncEvt(BIT(EVT_IO_FREE));
	}
}


void IO_RegCbf(CbKey eId, IoCbf pfCb)
{
	gaCbf[eId] = pfCb;
}


void IO_Init()
{
	NFC_Init(io_CbDone);
	gNCmdPool.Init();
	MEMSET_ARRAY(gabStop, 0x0);
	MEMSET_ARRAY(gaDone, 0x0);
	for (uint16 nIdx = 0; nIdx < NUM_NAND_CMD; nIdx++)
	{
		IO_Free(gaCmds + nIdx);
	}
}
