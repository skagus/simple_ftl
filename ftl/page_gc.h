#pragma once

#include "types.h"
#include "page_ftl.h"

#define GC_TRIG_BLK_CNT		(3)
#define SIZE_FREE_POOL		(5)

static_assert(SIZE_FREE_POOL > GC_TRIG_BLK_CNT);



struct ErbStk
{
	enum ErbState
	{
		Init,
		WaitErb,
		WaitJnlAdd,
		WaitMtSave,
	};
	ErbState eStep;
	OpenType eOpen;	///< requester.
	uint16 nBN;
	uint32 nMtAge;
};


void GC_Init();
uint16 GC_ReqFree(OpenType eOpen);
void GC_Stop();
bool GC_BlkErase_SM(ErbStk* pCtx);
