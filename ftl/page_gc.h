
#pragma once
#include "types.h"
#include "page_ftl.h"

#define SET_CHECK(x)		((x) |= BIT(31))
#define GET_INDEX(x)		((x) & (FF16))
#define GET_CHECK(x)		((x) & BIT(31))

enum ErbState
{
	ES_Init,
	ES_WaitErb,
	ES_WaitJnlAdd,
	ES_WaitMtSave,
};

struct ErsCtx
{
	uint16 nBN;
	ErbState eStep;
	uint32 nMtAge;
};


void GC_Init();
uint16 GC_ReqFree(OpenType eOpen);
void GC_VictimUpdate(VAddr stOld);
void GC_Stop();
bool GC_BlkErase(ErsCtx* pCtx, bool b1st);
