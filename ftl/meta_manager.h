
#pragma once

#pragma once
#include "types.h"
#include "map_log.h"

void META_Init();
void META_Format();
void META_Save();

LogMap* META_GetLogMap(uint16 nIdx);
BlkMap* META_GetBlkMap(uint16 nLBN);
LogMap* META_SearchLogMap(uint16 nLBN);
void META_SetFreePBN(uint16 nPBN);
uint16 META_GetFreePBN();
bool META_Ready();
