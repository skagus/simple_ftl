
#include "types.h"
#include "config.h"
#include "map_log.h"
#include "nfc.h"
#include "ftl.h"
#include "buf.h"
#include "io.h"
#include "gc.h"
#include "meta_manager.h"
#if (MAPPING == FTL_LOG_MAP)

#define PRINTF				// SIM_Print


void FTL_Init()
{
	META_Init();
}

#endif
