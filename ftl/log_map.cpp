
#include "types.h"
#include "config.h"
#include "nfc.h"
#include "ftl.h"
#include "buf.h"
#include "io.h"
#include "log_meta.h"
#if (MAPPING == FTL_LOG_MAP)

#define PRINTF				// SIM_Print


void FTL_Init()
{
	META_Init();
}

#endif
