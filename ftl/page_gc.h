#pragma once

#include "types.h"
#include "page_ftl.h"

#define GC_TRIG_BLK_CNT		(3)
#define SIZE_FREE_POOL		(5)

static_assert(SIZE_FREE_POOL > GC_TRIG_BLK_CNT);

void GC_Init();
uint16 GC_ReqFree_Blocking(OpenType eOpen);
void GC_BlkErase_OS(OpenType eOpen, uint16 nBN);

