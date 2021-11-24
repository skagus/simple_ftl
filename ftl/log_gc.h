
#pragma once
#include "types.h"
#include "log_map.h"

void GC_Init();
LogMap* GC_GetLog(uint16 nLBN);

void GC_ReqLog(uint16 nLBN);
LogMap* GC_MakeNewLog(uint16 nLBN, LogMap* pOrgMap);

