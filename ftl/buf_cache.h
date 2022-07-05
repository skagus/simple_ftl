#pragma once

#include "types.h"
#include "buf.h"
#include "os.h"
#include "page_req.h"
#include "io.h"
#include "page_meta.h"

void BC_ReqFlush(bool bSync);
void BC_AddWrite(uint32 nLPN, uint16 nBuf, uint16 nTag);
void BC_Init();

