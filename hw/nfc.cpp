#include "sim.h"
#include "templ.h"
#include "buf.h"
#include "die.h"
#include "nfc.h"

#define PRINT			//SIM_Print

/**
NFC Simulation module.
*/

enum NStep
{
	NS_WAIT_NEW_CMD,
	NS_WAIT_CMD_IN,		///< Channel & Die 점유
	NS_WAIT_BUSY,		///< Die 점유.
	NS_WAIT_DMA_IN,		///< Channel & Die 점유.
	NS_WAIT_DMA_OUT,	///< Die 점유중..
};


struct NFCEvt
{
	bool bNewCmd;	///< 새로운 명령 처리는 Sync때문에 try하는 형식으로 해야 한다.
	uint8 nDie;
	uint8 nOffset;
	uint64 nTick;
};


struct CurRun
{
	NStep eStep;
	CmdInfo* pCmd;
	uint32 bmRest;	// Rest DMA.
};

Queue<CmdInfo*, 100> gstDieQue[NUM_DIE];
Queue<CmdInfo*, 100> gstDoneQue;

CbFunc gfCbDone;
CurRun gastRun[NUM_DIE];

/**
Start new command if available.
*/
void nfc_Trigger(uint8 nDie)
{
	CurRun* pRun = gastRun + nDie;
	if (NStep::NS_WAIT_NEW_CMD == pRun->eStep)
	{
		// 겹칠 수 있다...
		if (gstDieQue[nDie].Count() > 0)
		{
			NFCEvt* pNewEvt = (NFCEvt*)SIM_NewEvt(HW_NFC, TIME_CMD_GAB);
			pNewEvt->bNewCmd = true;
			pNewEvt->nDie = nDie;
			pNewEvt->nTick = SIM_GetTick();
		}
	}
}

void nfc_DoneCmd(CmdInfo* pCmd)
{
	gstDoneQue.PushTail(pCmd);
	if (nullptr != gfCbDone)
	{
		gfCbDone(pCmd->nDie, 0);
	}
}

void nfc_HandleRead(NFCEvt* pOldEvt, CurRun* pRun)
{
	CmdInfo* pCmd = pRun->pCmd;
	Die* pDie = gaDies[pOldEvt->nDie];

	switch (pRun->eStep)
	{
		case NStep::NS_WAIT_CMD_IN:
		{
			pRun->bmRest = pCmd->stRead.bmChunk;
			pRun->eStep = NS_WAIT_BUSY;
			NFCEvt* pNewEvt = (NFCEvt*)SIM_NewEvt(HW_NFC, TIME_READ_BUSY);
			pNewEvt->bNewCmd = false;
			pNewEvt->nDie = pOldEvt->nDie;
			pNewEvt->nTick = SIM_GetTick();
			break;
		}
		case NStep::NS_WAIT_BUSY:
		{
			pRun->bmRest = pCmd->stRead.bmChunk;
			pRun->eStep = NS_WAIT_DMA_OUT;
			pDie->DoRead(pCmd->anBBN, pCmd->nWL, pCmd->bmPln);
			
			NFCEvt* pNewEvt = (NFCEvt*)SIM_NewEvt(HW_NFC, TIME_CHUNK_DMA);
			pNewEvt->bNewCmd = false;
			pNewEvt->nDie = pOldEvt->nDie;
			pNewEvt->nOffset = BIT_SCAN_LSB(pRun->bmRest);
			pNewEvt->nTick = SIM_GetTick();
			break;
		}
		case NStep::NS_WAIT_DMA_OUT:
		{
			uint8* pMain = BM_GetMain(pCmd->stRead.anBufId[pOldEvt->nOffset]);
			uint8* pSpare = BM_GetSpare(pCmd->stRead.anBufId[pOldEvt->nOffset]);
			pDie->DataOut(pOldEvt->nOffset, pMain, pSpare);

			pRun->bmRest &= ~BIT(pOldEvt->nOffset);
			if (0 != pRun->bmRest)
			{
				NFCEvt* pNewEvt = (NFCEvt*)SIM_NewEvt(HW_NFC, TIME_CHUNK_DMA);
				pNewEvt->bNewCmd = false;
				pNewEvt->nDie = pOldEvt->nDie;
				pNewEvt->nOffset = BIT_SCAN_LSB(pRun->bmRest);
				pNewEvt->nTick = SIM_GetTick();
			}
			else
			{
				nfc_DoneCmd(pCmd);
				pRun->eStep = NS_WAIT_NEW_CMD;
				pRun->pCmd = nullptr;
				nfc_Trigger(pOldEvt->nDie);
			}
			break;
		}

		case NStep::NS_WAIT_NEW_CMD:
		case NStep::NS_WAIT_DMA_IN:
		default:
		{
			assert(false);
		}
	}
}


void nfc_HandleProgram(NFCEvt* pOldEvt, CurRun* pRun)
{
	CmdInfo* pCmd = pRun->pCmd;
	Die* pDie = gaDies[pOldEvt->nDie];

	switch (pRun->eStep)
	{
		case NStep::NS_WAIT_CMD_IN:
		{
			pRun->bmRest = pCmd->stPgm.bmChunk;
			pRun->eStep = NS_WAIT_DMA_IN;

			NFCEvt* pNewEvt = (NFCEvt*)SIM_NewEvt(HW_NFC, TIME_CHUNK_DMA);
			pNewEvt->bNewCmd = false;
			pNewEvt->nDie = pOldEvt->nDie;
			pNewEvt->nOffset = BIT_SCAN_LSB(pRun->bmRest);
			pNewEvt->nTick = SIM_GetTick();
			break;
		}

		case NStep::NS_WAIT_DMA_IN:
		{
			uint8* pMain = BM_GetMain(pCmd->stPgm.anBufId[pOldEvt->nOffset]);
			uint8* pSpare = BM_GetSpare(pCmd->stPgm.anBufId[pOldEvt->nOffset]);
			pDie->DataIn(pOldEvt->nOffset, pMain, pSpare);
			pRun->bmRest &= ~BIT(pOldEvt->nOffset);
			if (0 != pRun->bmRest) // continue;
			{
				pRun->eStep = NS_WAIT_DMA_IN;

				NFCEvt* pNewEvt = (NFCEvt*)SIM_NewEvt(HW_NFC, TIME_CHUNK_DMA);
				pNewEvt->bNewCmd = false;
				pNewEvt->nDie = pOldEvt->nDie;
				pNewEvt->nOffset = BIT_SCAN_LSB(pRun->bmRest);
				pNewEvt->nTick = SIM_GetTick();
			}
			else
			{
				pRun->eStep = NS_WAIT_BUSY;

				NFCEvt* pNewEvt = (NFCEvt*)SIM_NewEvt(HW_NFC, TIME_PGM_BUSY);
				pNewEvt->bNewCmd = false;
				pNewEvt->nDie = pOldEvt->nDie;
				pNewEvt->nTick = SIM_GetTick();
			}
			break;
		}

		case NStep::NS_WAIT_BUSY:
		{
			pDie->DoProg(pCmd->anBBN, pCmd->nWL, pCmd->bmPln, nullptr);
			nfc_DoneCmd(pCmd);

			pRun->eStep = NS_WAIT_NEW_CMD;
			pRun->pCmd = nullptr;
			nfc_Trigger(pOldEvt->nDie);
			break;
		}

		case NStep::NS_WAIT_NEW_CMD:
		case NStep::NS_WAIT_DMA_OUT:
		default:
		{
			assert(false);
		}
	}
}

void nfc_HandleErase(NFCEvt* pOldEvt, CurRun* pRun)
{
	CmdInfo* pCmd = pRun->pCmd;
	Die* pDie = gaDies[pOldEvt->nDie];

	switch (pRun->eStep)
	{
		case NStep::NS_WAIT_CMD_IN:
		{
			pRun->eStep = NS_WAIT_BUSY;
			NFCEvt* pNewEvt = (NFCEvt*)SIM_NewEvt(HW_NFC, TIME_ERASE_BUSY);
			pNewEvt->bNewCmd = false;
			pNewEvt->nDie = pOldEvt->nDie;
			pNewEvt->nTick = SIM_GetTick();
			break;
		}
		case NStep::NS_WAIT_BUSY:
		{
			pDie->DoErase(pCmd->anBBN, pCmd->bmPln);
			nfc_DoneCmd(pCmd);
			pRun->eStep = NS_WAIT_NEW_CMD;
			pRun->pCmd = nullptr;
			nfc_Trigger(pOldEvt->nDie);
			break;
		}

		case NStep::NS_WAIT_NEW_CMD:
		case NStep::NS_WAIT_DMA_IN:
		default:
		{
			assert(false);
		}
	}
}


void nfc_HandleEvt(void* pEvt)
{
	NFCEvt* pCurEvt = (NFCEvt*)pEvt;
	uint8 nDie = pCurEvt->nDie;

	PRINT("NFCEvt Rcv <-- %d\n", pCurEvt->nTick);

	CurRun* pRun = gastRun + nDie;
	if (NStep::NS_WAIT_NEW_CMD == pRun->eStep)
	{
		if ( pCurEvt->bNewCmd
			&& (gstDieQue[nDie].Count() > 0))
		{
			pRun->eStep = NS_WAIT_CMD_IN;
			pRun->pCmd = gstDieQue[nDie].PopHead();
			pRun->bmRest = (uint32)(-1);

			NFCEvt* pNewEvt = (NFCEvt*)SIM_NewEvt(HW_NFC, TIME_CMD_ISSUE);
			pNewEvt->bNewCmd = false;
			pNewEvt->nDie = nDie;
			pNewEvt->nTick = SIM_GetTick();
		}
	}
	else
	{
		CmdInfo* pCmd = pRun->pCmd;
		switch (pCmd->eCmd)
		{
			case NC_READ:
			{
				nfc_HandleRead(pCurEvt, pRun);
				break;
			}
			case NC_PGM:
			{
				nfc_HandleProgram(pCurEvt, pRun);
				break;
			}
			case NC_ERB:
			{
				nfc_HandleErase(pCurEvt, pRun);
				break;
			}
		}
	}
}


void NFC_InitSim()
{
	SIM_AddHW(HW_NFC, nfc_HandleEvt);
	DIE_InitSim();
}

////////////////////////////////////////// NFC LLD ///////////////////////////////////////////////////////


void NFC_Issue(CmdInfo* pCmd)
{
	uint8 nDie = pCmd->nDie;
	gstDieQue[nDie].PushTail(pCmd);

	nfc_Trigger(nDie);
}

CmdInfo* NFC_GetDone()
{
	if (gstDoneQue.Count() > 0)
	{
		return gstDoneQue.PopHead();
	}
	return nullptr;
}

// Initialize by FW.
void NFC_Init(CbFunc pfCbfDone)
{
	memset(gastRun, 0, sizeof(gastRun));
	gfCbDone = pfCbfDone;
}

