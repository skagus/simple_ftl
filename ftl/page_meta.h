
#pragma once

#pragma once
#include "types.h"
#include "page_ftl.h"

#define MARK_META		(0xFABCBEAF)
#if 0
enum JnlType
{
	JT_UserW,
	JT_GcW,
	JT_ERB,
};

union Jnl
{
	struct
	{
		uint32 eJType : 2;
		uint32 nValue : 30;
	} Com;
	struct
	{
		uint32 eJType : 2;		// JT_GC, JT_User
		uint32 nWL : 10;
		uint32 nLPN : 20;
	} Wrt;
	struct
	{
		uint32 eJType : 2;	// JT_ERB
		uint32 eOpenType : 1;
		uint32 nBN : 29;
	} Erb;
};

#define MAX_JNL_ENTRY		(32)
struct JnlSet
{
	uint32 nCnt;	///< Valid count or Jnl.
	uint16 anActBlk[NUM_OPEN];
	Jnl aJnl[MAX_JNL_ENTRY];
public:
	bool AddWrite(OpenType eOpen, uint16 nWL, uint32 nLPN)
	{
		aJnl[nCnt].Wrt.eJType = (OPEN_GC == eOpen) ? JT_GcW : JT_UserW;
		aJnl[nCnt].Wrt.nWL = nWL;
		aJnl[nCnt].Wrt.nLPN = nLPN;
		nCnt++;
		return (MAX_JNL_ENTRY == nCnt);
	}
	bool AddErase(OpenType eOpen, uint16 nBN)
	{
		aJnl[nCnt].Erb.eJType = JT_ERB;
		aJnl[nCnt].Erb.eOpenType = eOpen;
		aJnl[nCnt].Erb.nBN = nBN;
		nCnt++;
		return (MAX_JNL_ENTRY == nCnt);
	}
};
#endif

void META_Init();
uint32 META_ReqSave();	// Age return.
uint32 META_GetAge();

void META_SetOpen(OpenType eType, uint16 nBN);
OpenBlk* META_GetOpen(OpenType eOpen);
VAddr META_GetMap(uint32 nLPN);
BlkInfo* META_GetFree(uint16* pnBN, bool bFirst);
BlkInfo* META_GetMinVPC(uint16* pnBN);
void META_SetBlkState(uint16 nBN, BlkState eState);

bool META_Ready();
void META_Update(uint32 nLPN, VAddr stVA, OpenType eOpen);
