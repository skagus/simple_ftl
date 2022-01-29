
#pragma once

#pragma once
#include "types.h"
#include "page_ftl.h"

void META_Init();
void META_ReqSave();	// Age return.
uint32 META_GetAge();

OpenBlk* META_GetOpen(OpenType eOpen);
VAddr META_GetMap(uint32 nLPN);
BlkInfo* META_GetFree(uint16* pnBN, bool bFirst);
BlkInfo* META_GetMinVPC(uint16* pnBN);
void META_SetOpen(OpenType eType, uint16 nBN);
bool META_Ready();
void META_FilterP2L(uint16 nBN, uint32* aLPN);
void META_Update(uint32 nLPN, VAddr stVA);
