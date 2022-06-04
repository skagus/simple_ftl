
#pragma once
#include "types.h"
#include "page_ftl.h"

#define SET_CHECK(x)		((x) |= BIT(31))
#define GET_INDEX(x)		((x) & (FF16))
#define GET_CHECK(x)		((x) & BIT(31))

#define GC_TRIG_BLK_CNT		(3)
#define SIZE_FREE_POOL		(5)

static_assert(SIZE_FREE_POOL > GC_TRIG_BLK_CNT);

enum ErbState
{
	ES_Init,
	ES_WaitErb,
	ES_WaitJnlAdd,
	ES_WaitMtSave,
};

struct ErbStk
{
	ErbState eStep;
	OpenType eOpen;	///< requester.
	uint16 nBN;
	uint32 nMtAge;
};


void GC_Init();
uint16 GC_ReqFree(OpenType eOpen);
void GC_VictimUpdate(VAddr stOld);
void GC_Stop();
bool GC_BlkErase_SM(ErbStk* pCtx, bool b1st);
