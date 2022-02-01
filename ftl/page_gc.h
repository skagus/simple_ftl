
#pragma once
#include "types.h"
#include "page_ftl.h"

#define SET_CHECK(x)		((x) |= BIT(31))
#define GET_INDEX(x)		((x) & (FF16))
#define GET_CHECK(x)		((x) & BIT(31))

void GC_Init();
uint16 GC_ReqFree(OpenType eOpen);
void GC_VictimUpdate(VAddr stOld);
