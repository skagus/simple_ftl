
#pragma once

#pragma once
#include "types.h"
#include "log_ftl.h"

void META_Init();
void META_ReqSave();	// Age return.
uint32 META_GetAge();

BlkMap* META_GetBlkMap(uint16 nLBN);
LogMap* META_SearchLogMap(uint16 nLBN);
bool META_Ready();
