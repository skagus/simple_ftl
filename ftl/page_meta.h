#pragma once

#include "types.h"
#include "page_ftl.h"

#define MARK_META		(0xFABCBEAF)


union Jnl
{
	enum JnlType
	{
		JT_UserW,
		JT_GcW,
		JT_ERB,
	};

	struct
	{
		uint32 eJType : 2;
		uint32 nValue : 30;
	} Com;
	struct
	{
		uint32 eJType : 2;		// JT_GC, JT_User
		uint32 nLPN : 30;
		VAddr stAddr;
	} Wrt;
	struct
	{
		uint32 eJType : 2;	// JT_ERB
		uint32 eOpenType : 1;
		uint32 nBN : 29;
	} Erb;
	Jnl() {}
};

enum JnlRet
{
	JR_Done,	///< Done with no event.
	JR_Busy,	///< On Jnl saving.
	JR_Filled,	///< Just filled, need to save.
};

#define MAX_JNL_ENTRY		(20)
struct JnlSet
{
	bool bBusy;
	uint32 nCnt;	///< Valid count or Jnl.
	VAddr anActBlk[NUM_OPEN];	///< Open block information at JnlSet starting.
	Jnl aJnl[MAX_JNL_ENTRY];
public:
	void Start(OpenType eOpen, uint16 nBN)
	{
		bBusy = false;
		anActBlk[eOpen].nBN = nBN;
		anActBlk[eOpen].nWL = 0;
		memset(aJnl, 0, sizeof(aJnl));
		nCnt = 0;
	}
	JnlRet AddWrite(uint32 nLPN, VAddr stVA, OpenType eOpen)
	{
		if ((false == bBusy) && (MAX_JNL_ENTRY > nCnt))
		{
			aJnl[nCnt].Wrt.eJType = (OPEN_GC == eOpen) ? Jnl::JT_GcW : Jnl::JT_UserW;
			aJnl[nCnt].Wrt.nLPN = nLPN;
			aJnl[nCnt].Wrt.stAddr = stVA;
			nCnt++;
			bBusy = (MAX_JNL_ENTRY == nCnt);
			return (MAX_JNL_ENTRY > nCnt) ? JR_Done : JR_Filled;
		}
		return JR_Busy;
	}
	JnlRet AddErase(uint16 nBN, OpenType eOpen)
	{
		if ((false == bBusy) && (MAX_JNL_ENTRY > nCnt))
		{
			aJnl[nCnt].Erb.eJType = Jnl::JT_ERB;
			aJnl[nCnt].Erb.eOpenType = eOpen;
			aJnl[nCnt].Erb.nBN = nBN;
			nCnt++;
			bBusy = true;
			return JR_Filled;
		}
		return JR_Busy;
	}
};

enum UpdateState
{
	US_Init,
	US_WaitMeta, ///< 이번 update로 meta data save가 일어난 경우.
};

struct UpdateCtx
{
	UpdateState eState;
	uint32 nMtAge;
	uint32 nLPN;
	VAddr stVA;
	OpenType eOpen;
};

struct Meta
{
	//	VAddr astOpen[NUM_OPEN];
	BlkInfo astBI[NUM_USER_BLK];
	VAddr astL2P[NUM_LPN];
};


struct MetaCtx
{
	uint16 nCurBO;
	uint16 nCurBN; // == meta_MtBlk2PBN(nCurBO)
	uint16 nNextWL;
	uint16 nNextSlice;
	uint32 nAge;
};

#define SIZE_MAP_PER_SAVE		(BYTE_PER_PPG - sizeof(JnlSet))
#define NUM_MAP_SLICE			DIV_CEIL(sizeof(Meta), SIZE_MAP_PER_SAVE)


void META_Init();
uint32 META_ReqSave();	// Age return.
uint32 META_GetAge();

void META_SetOpen(OpenType eType, uint16 nBN, uint16 nWL = 0);
OpenBlk* META_GetOpen(OpenType eOpen);
VAddr META_GetMap(uint32 nLPN);
BlkInfo* META_GetFree(uint16* pnBN, bool bFirst);
BlkInfo* META_GetMinVPC(uint16* pnBN);
void META_SetBlkState(uint16 nBN, BlkState eState);

bool META_Ready();
JnlRet META_AddErbJnl(OpenType eOpen, uint16 nBN);
void META_StartJnl(OpenType eOpen, uint16 nBN);
JnlRet META_Update(uint32 nLPN, VAddr stVA, OpenType eOpen, bool bOnOpen = false);

