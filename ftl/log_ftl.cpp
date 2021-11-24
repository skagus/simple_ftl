#include "templ.h"
#include "sim.h"
#include "buf.h"
#include "nfc.h"
#include "timer.h"
#include "power.h"
#include "scheduler.h"
#include "test.h"
#include "io.h"
#include "ftl.h"
#include "io.h"
#include "log_gc.h"
#include "log_req.h"
#include "log_meta.h"

#define PRINTF		//	SIM_Print

Queue<ReqInfo*, SIZE_REQ_QUE> gstReqQ;

void FTL_Request(ReqInfo* pReq)
{
	gstReqQ.PushTail(pReq);
	Sched_TrigSyncEvt(BIT(EVT_USER_CMD));
}

uint32 FTL_GetNumLPN(CbfReq pfCbf)
{
	REQ_SetCbf(pfCbf);
	SIM_CpuTimePass(SIM_MSEC(1000));	// Wait open time.
	return NUM_USER_BLK * LPN_PER_USER_BLK;
}

void FTL_Main(void* pParam)
{
	TMR_Init();
	BM_Init();

	Cbf pfTickIsr = Sched_Init();
	TMR_Add(0, SIM_MSEC(MS_PER_TICK), pfTickIsr, true);

	gstReqQ.Init();
	IO_Init();
	REQ_Init();
	META_Init();
	GC_Init();

	PRINTF("[FTL] Init done\n");
	while (true)
	{
		Sched_Run();
	}
}

