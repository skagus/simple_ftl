
#pragma once
#include "types.h"
#include "map_log.h"

void GC_Init();
LogMap* GC_GetLog(uint16 nLBN);

LogMap* GC_MakeNewLog(uint16 nLBN, LogMap* pOrgMap);

