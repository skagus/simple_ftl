
#include "types.h"
#include "config.h"
#include "templ.h"
#include "macro.h"
#include "buf.h"

#include "os.h"
#include "io.h"
#include "page_gc.h"
#include "page_meta.h"

#define PRINTF			SIM_Print
#define MAP_PRINTF

#define PAGE_PER_META	(1)

Meta gstMeta;
bool gbRequest;
OpenBlk gaOpen[NUM_OPEN];
MetaCtx gstMetaCtx;
JnlSet gstJnlSet;
bool gbReady;



void open_PostMtLoad();


uint16 meta_MtBlk2PBN(uint16 nMetaBN)
{
	return nMetaBN + BASE_META_BLK;
}


void meta_Format()
{
	for (uint16 nIdx = 0; nIdx < NUM_USER_BLK; nIdx++)
	{
		gstMeta.astBI[nIdx].eState = BS_Closed;
		gstMeta.astBI[nIdx].nVPC = 0;
	}
	gstMetaCtx.nNextWL = 0;
	gstMetaCtx.nAge = 1;
	gstMetaCtx.nCurBO = 0;
	gstMetaCtx.nNextSlice = 0;
	// TODO: Map save sequence.
}

void dbg_MapIntegrity()
{
	uint16 anVPC[NUM_USER_BLK];
	MEMSET_ARRAY(anVPC, 0x0);
	for (uint32 nLPN = 0; nLPN < NUM_LPN; nLPN++)
	{
		if (gstMeta.astL2P[nLPN].nBN < NUM_USER_BLK)
		{
			anVPC[gstMeta.astL2P[nLPN].nBN]++;
		}
	}
	for (uint16 nBN = 0; nBN < NUM_USER_BLK; nBN++)
	{
		ASSERT(gstMeta.astBI[nBN].nVPC == anVPC[nBN]);
	}
}

bool META_Ready()
{
	return gbReady;
}

VAddr META_GetMap(uint32 nLPN)
{
	if (nLPN < NUM_LPN)
	{
		return gstMeta.astL2P[nLPN];
	}
	else
	{
		VAddr stAddr;
		stAddr.nDW = FF32;
		return stAddr;
	}
}

BlkInfo* META_GetFree(uint16* pnBN, bool bFirst)
{
	for (uint16 nIdx = 0; nIdx < NUM_USER_BLK; nIdx++)
	{
		BlkInfo* pBI = gstMeta.astBI + nIdx;
		if ((BS_Closed == pBI->eState)
			&& (0 == pBI->nVPC))
		{
			if (NOT(bFirst))
			{
				bFirst = true;
				continue;
			}
			if (nullptr != pnBN)
			{
				*pnBN = nIdx;
			}
			return pBI;
		}
	}
	return nullptr;
}

void META_SetOpen(OpenType eType, uint16 nBN, uint16 nWL)
{
	OpenBlk* pOpen = gaOpen + eType;
	pOpen->stNextVA.nBN = nBN;
	pOpen->stNextVA.nWL = nWL;
	gstMeta.astBI[nBN].eState = BS_Open;
	if (0 == nWL)
	{
		gstMeta.astBI[nBN].nEC++;
	}
}

void META_SetBlkState(uint16 nBN, BlkState eState)
{
	gstMeta.astBI[nBN].eState = eState;
}

JnlRet META_AddErbJnl(OpenType eOpen, uint16 nBN)
{
	return gstJnlSet.AddErase(nBN, eOpen);
}

void META_StartJnl(OpenType eOpen, uint16 nBN)
{
	gstJnlSet.Start(eOpen, nBN);
}

JnlRet META_Update(uint32 nLPN, VAddr stNew, OpenType eOpen, bool bOnOpen)
{
	JnlRet eJRet = JR_Done;
	if (nLPN < NUM_LPN)
	{
		VAddr stOld = gstMeta.astL2P[nLPN];
		if (false == bOnOpen)
		{
			eJRet = gstJnlSet.AddWrite(nLPN, stNew, eOpen);
		}
		if (JR_Busy != eJRet)
		{
			gstMeta.astL2P[nLPN] = stNew;
			if (FF32 != stOld.nDW)
			{
				gstMeta.astBI[stOld.nBN].nVPC--;
			}
			if (FF32 != stNew.nDW)
			{
				gstMeta.astBI[stNew.nBN].nVPC++;
			}
		}
		MAP_PRINTF("[MAP] %X --> {%X,%X}\n", nLPN, stNew.nBN, stNew.nWL);
	}
	if (false == bOnOpen)
	{
		dbg_MapIntegrity();
	}
	return eJRet;
}

BlkInfo* META_GetMinVPC(uint16* pnBN)
{
	uint16 nMinVPC = FF16;
	uint16 nMinBlk = FF16;
	for (uint16 nIdx = 0; nIdx < NUM_USER_BLK; nIdx++)
	{
		BlkInfo* pBI = gstMeta.astBI + nIdx;
		if ((BS_Closed == pBI->eState)
			&& (0 != pBI->nVPC)
			&& (nMinVPC > pBI->nVPC))
		{
			nMinBlk = nIdx;
			nMinVPC = pBI->nVPC;
		}
	}
	if (nullptr != pnBN)
	{
		*pnBN = nMinBlk;
	}
	return gstMeta.astBI + nMinBlk;
}

/***************************************************************************
* Meta Open/Save sequence.
***************************************************************************/

OpenBlk* META_GetOpen(OpenType eOpen)
{
	return gaOpen + eOpen;
}

void meta_Save_OS()
{
	if (0 == gstMetaCtx.nNextWL)
	{
		gstMetaCtx.nCurBN = meta_MtBlk2PBN(gstMetaCtx.nCurBO);
		PRINTF("[MT] ERS BO:%X, BN:%X\n", gstMetaCtx.nCurBO, gstMetaCtx.nCurBN);
		CmdInfo* pCmd = IO_Alloc(IOCB_Meta);
		IO_Erase(pCmd, gstMetaCtx.nCurBN, 0);
		while (nullptr == (pCmd = IO_PopDone(IOCB_Meta)))
		{
			OS_Wait(BIT(EVT_NAND_CMD), LONG_TIME);
		}
		IO_Free(pCmd);
	}

	uint32 nIssue = 0;
	for (nIssue = 0; nIssue < PAGE_PER_META; nIssue++)
	{
		uint16 nBuf = BM_Alloc();
		uint32* pSpare = (uint32*)BM_GetSpare(nBuf);
		pSpare[0] = gstMetaCtx.nAge;
		pSpare[1] = gstMetaCtx.nNextSlice;
		uint8* pDst = BM_GetMain(nBuf);
		if (0 == nIssue)
		{
			memcpy(pDst, &gstJnlSet, sizeof(gstJnlSet));
			pDst += sizeof(gstJnlSet);
			uint8* pSrc = (uint8*)(&gstMeta) + (gstMetaCtx.nNextSlice * SIZE_MAP_PER_SAVE);
			memcpy(pDst, pSrc, SIZE_MAP_PER_SAVE);
		}

		uint16 nWL = gstMetaCtx.nNextWL + nIssue;
		CmdInfo* pCmd = IO_Alloc(IOCB_Meta);
		PRINTF("[MT:%X] \t==== PGM (%X,%X) Age:%X, Slice: %X ====\n", pCmd->nDbgSN, gstMetaCtx.nCurBN, nWL, gstMetaCtx.nAge, gstMetaCtx.nNextSlice);
		IO_Program(pCmd, gstMetaCtx.nCurBN, nWL, nBuf, 0);
	}

	while (true)
	{
		CmdInfo* pDone;
		while (pDone = IO_PopDone(IOCB_Meta))
		{
			BM_Free(pDone->stPgm.anBufId[0]);
			IO_Free(pDone);
			nIssue--;
		}
		if (nIssue == 0)
		{
			break;
		}
		OS_Wait(BIT(EVT_NAND_CMD), LONG_TIME);
	}

	gstMetaCtx.nAge++;
	gstMetaCtx.nNextSlice++;
	if (gstMetaCtx.nNextSlice >= NUM_MAP_SLICE)
	{
		gstMetaCtx.nNextSlice = 0;
	}
	gstMetaCtx.nNextWL += PAGE_PER_META;
	if (gstMetaCtx.nNextWL >= NUM_WL)
	{
		gstMetaCtx.nNextWL = 0;
		gstMetaCtx.nCurBO++;
		if (gstMetaCtx.nCurBO >= NUM_META_BLK)
		{
			gstMetaCtx.nCurBO = 0;
		}
	}
}

// =========================================
void open_UserScan_OS(OpenType eOpen)
{
	OpenBlk* pOpen = META_GetOpen(eOpen);
	uint16 nBN = pOpen->stNextVA.nBN;
	uint16 nNextWL = pOpen->stNextVA.nWL;
	uint16 nErasedWL = FF16;
	uint8 nRun = 0;
	bool bRun = true;
	PRINTF("[OPEN] Data scan %s {%X,%X}\n", eOpen == OpenType::OPEN_GC ? "GC" : "User", nBN, nNextWL);

	while (bRun)
	{
		CmdInfo* pDone;
		if (nullptr != (pDone = IO_PopDone(CbKey::IOCB_Meta)))
		{
			nRun--;
			uint16 nBuf = pDone->stRead.anBufId[0];
			uint32* pnSpare = (uint32*)BM_GetSpare(nBuf);
			uint16 nWL = pDone->nTag;

			if (MARK_ERS != *pnSpare)
			{
				uint32 nLPN = *pnSpare;
				VAddr stCur(0, pDone->anBBN[0], pDone->nWL);
				PRINTF("[SCAN] MapUpdate: LPN:%X to (%X, %X), %c\n",
					*pnSpare, pDone->anBBN[0], pDone->nWL, eOpen == OPEN_GC ? 'G' : 'U');
				if (JR_Filled == META_Update(*pnSpare, stCur, eOpen))
				{
					META_ReqSave(false);
				}
			}
			else if (FF16 == nErasedWL)
			{
				PRINTF("[SCAN] Erased detect: (%X, %X), %c\n",
					pDone->anBBN[0], pDone->nWL, eOpen == OPEN_GC ? 'G' : 'U');
				nErasedWL = pDone->nWL;
			}
			BM_Free(nBuf);
			IO_Free(pDone);
		}

		while ((FF16 == nErasedWL) && (nNextWL < NUM_WL) && (nRun < 2))
		{
			uint16 nBuf = BM_Alloc();
			CmdInfo* pCmd = IO_Alloc(IOCB_Meta);
			IO_Read(pCmd, nBN, nNextWL, nBuf, nNextWL);
			nNextWL++;
			nRun++;
		}

		if (nRun > 0)
		{
			OS_Wait(BIT(EVT_NAND_CMD), LONG_TIME);
		}
		else if ((nNextWL >= NUM_WL) || (FF16 != nErasedWL)) // All done.
		{
			OpenBlk* pOpen = META_GetOpen(eOpen);
			if (FF16 != nErasedWL)
			{
				pOpen->stNextVA.nWL = nErasedWL;
			}
			else
			{
				pOpen->stNextVA.nWL = NUM_WL;
			}
			bRun = false;
		}
	}
}

// =================== Meta Page Scan ========================
uint16 open_PageScan_OS(uint16 nBN)
{
	uint16 nIssued = 0;
	uint16 nCPO = NUM_WL;
	uint8 nDone = 0;
	while (true)
	{
		CmdInfo* pDone;
		while (nullptr != (pDone = IO_PopDone(CbKey::IOCB_Meta)))
		{
			nDone++;
			uint16 nBuf = pDone->stRead.anBufId[0];
			uint32* pnSpare = (uint32*)BM_GetSpare(nBuf);
			PRINTF("[OPEN] PageScan (%X,%X) -> Age:%X, Slice:%X\n",
				pDone->anBBN[0], pDone->nWL, pnSpare[0], pnSpare[1]);
			if (MARK_ERS != *pnSpare)
			{
				ASSERT(NUM_WL == nCPO);
				gstMetaCtx.nAge = pnSpare[0];
				gstMetaCtx.nNextSlice = (pnSpare[1] + NUM_MAP_SLICE + 1) % NUM_MAP_SLICE;
			}
			else if (NUM_WL == nCPO)
			{
				nCPO = pDone->nTag * PAGE_PER_META;
			}
			BM_Free(nBuf);
			IO_Free(pDone);
		}
		
		while ((NUM_WL == nCPO)
			&& (nIssued < NUM_WL / PAGE_PER_META)
			&& ((nIssued - nDone) < 2))
		{
			uint16 nWL = nIssued * PAGE_PER_META;
			uint16 nBuf = BM_Alloc();
			CmdInfo* pCmd = IO_Alloc(IOCB_Meta);
			IO_Read(pCmd, nBN, nWL, nBuf, nIssued);
			nIssued++;
		}
		if (nIssued > nDone)
		{
			OS_Wait(BIT(EVT_NAND_CMD), LONG_TIME);
		}
		else if ((nCPO != NUM_WL) || (nDone >= NUM_WL / PAGE_PER_META))
		{
			PRINTF("[OPEN] Clean MtPage (%X,%X))\n", nBN, nCPO);
			break;
		}
	}
	return nCPO;
}

void open_ReplayJnl(JnlSet* pJnlSet, uint32 nAge)
{
	PRINTF("[OPEN] Replay Jnl Age:%d, Cnt: %d, start with %X\n", nAge, pJnlSet->nCnt, pJnlSet->aJnl[0].Com.nValue);
	for (uint16 nIdx = 0; nIdx < pJnlSet->nCnt; nIdx++)
	{
		Jnl* pJnl = pJnlSet->aJnl + nIdx;
		switch (pJnl->Com.eJType)
		{
			case Jnl::JT_UserW:
			{
				META_Update(pJnl->Wrt.nLPN, pJnl->Wrt.stAddr, OpenType::OPEN_USER, true);
				OpenBlk* pOpen = META_GetOpen(OpenType::OPEN_USER);
				pOpen->stNextVA = pJnl->Wrt.stAddr;
				pOpen->stNextVA.nWL++;
				break;
			}
			case Jnl::JT_GcW:
			{
				META_Update(pJnl->Wrt.nLPN, pJnl->Wrt.stAddr, OpenType::OPEN_GC, true);
				OpenBlk* pOpen = META_GetOpen(OpenType::OPEN_GC);
				pOpen->stNextVA = pJnl->Wrt.stAddr;
				pOpen->stNextVA.nWL++;
				break;
			}
			case Jnl::JT_ERB:
			{
				OpenBlk* pOpen;
				if (OpenType::OPEN_GC == pJnl->Erb.eOpenType)
				{
					pOpen = META_GetOpen(OpenType::OPEN_GC);
				}
				else
				{
					pOpen = META_GetOpen(OpenType::OPEN_USER);
				}
				pOpen->stNextVA.nBN = pJnl->Erb.nBN;
				pOpen->stNextVA.nWL = 0;
				break;
			}
			default:
			{
				ASSERT(false);
			}
		}
	}
}

void open_MtLoad_OS(uint16 nMaxBO, uint16 nCPO)
{
	uint16 nStartWL;
	if (nCPO >= NUM_MAP_SLICE)
	{
		nStartWL = nCPO - NUM_MAP_SLICE;
	}
	else
	{
		nStartWL = (nCPO + NUM_WL - NUM_MAP_SLICE) % NUM_WL;
		nMaxBO = (nMaxBO + NUM_META_BLK - 1) % NUM_META_BLK;
	}
	uint16 nMaxBN = meta_MtBlk2PBN(nMaxBO);

	uint8 nIssued = 0;
	uint8 nDone = 0;
	MEMSET_OBJ(gstMeta, 0xFF);	// initialize as FF32;
	while (true)
	{
		// Check phase.
		CmdInfo* pDone;
		while (nullptr != (pDone = IO_PopDone(CbKey::IOCB_Meta)))
		{
			nDone++;
			uint16 nBuf = pDone->stRead.anBufId[0];
			uint8* pMain = BM_GetMain(nBuf);
			uint32* pSpare = (uint32*)BM_GetSpare(nBuf);
			uint32 nSlice = pSpare[1];
			PRINTF("[OPEN] Mt Loaded {%X,%X} (%d,%d)\n", pDone->anBBN[0], pDone->nWL, pSpare[0], pSpare[1]);
			ASSERT(nSlice < NUM_MAP_SLICE);
			open_ReplayJnl((JnlSet*)pMain, pSpare[0]);
			uint8* pSrc = pMain + sizeof(JnlSet);
			uint8* pDst = (uint8*)(&gstMeta) + (nSlice * SIZE_MAP_PER_SAVE);
			uint32 nSize = SIZE_MAP_PER_SAVE;
			if (nSlice >= (NUM_MAP_SLICE - 1))
			{
				nSize = sizeof(gstMeta) - (nSlice * SIZE_MAP_PER_SAVE);
			}
			memcpy(pDst, pSrc, nSize);
			BM_Free(nBuf);
			IO_Free(pDone);
		}

		while ((nIssued < NUM_MAP_SLICE) && (nIssued - nDone < 2))
		{
			uint16 nWL = nStartWL + nIssued;
			uint16 nBN = nMaxBN;
			if (nWL >= NUM_WL)
			{
				nWL = nWL % NUM_WL;
				nBN = meta_MtBlk2PBN((nMaxBO + 1) % NUM_META_BLK);
			}
			uint16 nBuf = BM_Alloc();
			CmdInfo* pCmd = IO_Alloc(IOCB_Meta);
			IO_Read(pCmd, nBN, nWL, nBuf, nIssued);
			nIssued++;
		}

		if (nDone >= NUM_MAP_SLICE)
		{
			break;
		}
		else if (nIssued > nDone)
		{
			OS_Wait(BIT(EVT_NAND_CMD), LONG_TIME);
		}
	}
}


// ========================== Meta Block Scan ====================================

uint16 open_BlkScan_SM()
{
	uint8 nIssued = 0;
	uint8 nDone = 0;
	uint16 nMaxBO = FF16;
	uint32 nMaxAge = 0;

	while (true)
	{
		// Issue phase.
		while ((nIssued < NUM_META_BLK) && ((nIssued - nDone) < 2))
		{
			uint16 nBuf = BM_Alloc();
			CmdInfo* pCmd;
			uint16 nBN = meta_MtBlk2PBN(nIssued);
			pCmd = IO_Alloc(IOCB_Meta);
			IO_Read(pCmd, nBN, 0, nBuf, nIssued);
			PRINTF("[OPEN] BlkScan Issue %X\n", nIssued);
			nIssued++;
		}
		// Check phase.
		CmdInfo* pDone;
		while (nullptr != (pDone = IO_PopDone(CbKey::IOCB_Meta)))
		{
			nDone++;
			uint16 nBuf = pDone->stRead.anBufId[0];
			uint32* pnSpare = (uint32*)BM_GetSpare(nBuf);

			PRINTF("[OPEN] BlkScan BO:%2X, SPR:%2X\n", pDone->nTag, *pnSpare);

			if ((*pnSpare > nMaxAge) && (*pnSpare != MARK_ERS))
			{
				nMaxAge = *pnSpare;
				nMaxBO = pDone->nTag;
			}
			BM_Free(nBuf);
			IO_Free(pDone);
		}
		if (NUM_META_BLK == nDone)	// All done.
		{
			PRINTF("[OPEN] Latest Blk Offset: %X\n", nMaxBO);
			break;
		}
		if (nIssued > nDone)
		{
			OS_Wait(BIT(EVT_NAND_CMD), LONG_TIME);
		}
	}
	return nMaxBO;
}


// =====================================================

/**
* @return success, if failure format again.
*/
bool meta_Open_OS()
{
	bool bRet = false;
	uint16 nMaxBO = open_BlkScan_SM();
	if (FF16 != nMaxBO)
	{
		gstMetaCtx.nCurBO = nMaxBO;
		gstMetaCtx.nCurBN = meta_MtBlk2PBN(nMaxBO);
		gstMetaCtx.nNextWL = open_PageScan_OS(gstMetaCtx.nCurBN);
		if (gstMetaCtx.nNextWL >= NUM_WL)
		{
			nMaxBO = (nMaxBO + 1) % NUM_META_BLK;
			gstMetaCtx.nCurBO = nMaxBO;
			gstMetaCtx.nCurBN = meta_MtBlk2PBN(nMaxBO);
			gstMetaCtx.nNextWL = 0;
			PRINTF("[OPEN] Mt Blk boundary {%X,%X}\n", gstMetaCtx.nCurBN, gstMetaCtx.nNextWL);
		}
		open_MtLoad_OS(nMaxBO, gstMetaCtx.nNextWL);
		open_PostMtLoad();

		gstJnlSet.Start(OPEN_USER, 0); // Prepare Jnl to add on data scan.
		open_UserScan_OS(OPEN_GC);
		open_UserScan_OS(OPEN_USER);
	}

	return FF16 != nMaxBO;
}

void open_PostMtLoad()
{
	uint16 anVPC[NUM_USER_BLK];
	MEMSET_ARRAY(anVPC, 0x0);
	for (uint32 nLPN = 0; nLPN < NUM_LPN; nLPN++)
	{
		if (gstMeta.astL2P[nLPN].nBN < NUM_USER_BLK)
		{
			anVPC[gstMeta.astL2P[nLPN].nBN]++;
		}
	}
	for (uint16 nBN = 0; nBN < NUM_USER_BLK; nBN++)
	{
//		ASSERT(gstMeta.astBI[nBN].nVPC == anVPC[nBN]);
		gstMeta.astBI[nBN].nVPC = anVPC[nBN];
		gstMeta.astBI[nBN].eState = BlkState::BS_Closed;
	}
	for (uint32 nOpen = 0; nOpen < NUM_OPEN; nOpen++)
	{
		if (gaOpen[nOpen].stNextVA.nWL < NUM_WL)
		{
			META_SetOpen((OpenType)nOpen, gaOpen[nOpen].stNextVA.nBN, gaOpen[nOpen].stNextVA.nWL);
		}
	}
}


void meta_Run(void* pParam)
{
	if (false == meta_Open_OS())
	{
		meta_Format();
	}
	gbReady = true;
	OS_SyncEvt(BIT(EVT_OPEN));

	while (true)
	{
		if (gbRequest)
		{
			gbRequest = false;
			meta_Save_OS();
			META_StartJnl(OPEN_GC, 0);
			OS_SyncEvt(BIT(EVT_META));
			continue;
		}

		OS_Wait(BIT(EVT_META), LONG_TIME);
	}
}

uint32 META_GetAge()
{
	return gstMetaCtx.nAge;
}

uint32 META_ReqSave(bool bSync)
{
	gbRequest = true;
	OS_SyncEvt(BIT(EVT_META));
	uint32 nAge = gstMetaCtx.nAge;
	while (bSync && nAge >= META_GetAge())
	{
		OS_Wait(BIT(EVT_META), LONG_TIME);
	}
	return gstMetaCtx.nAge;
}

void META_Init()
{
	gbReady = false;
	gbRequest = false;
	MEMSET_PTR(&gstJnlSet, 0);
	MEMSET_ARRAY(gaOpen, 0xFF);
	MEMSET_ARRAY(gstMeta.astBI, 0);
	MEMSET_ARRAY(gstMeta.astL2P, 0xFF);
	MEMSET_OBJ(gstMetaCtx, 0);
	OS_CreateTask(meta_Run, nullptr, nullptr, "meta");
}
