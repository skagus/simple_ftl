#include "templ.h"
#include "sim.h"
#include "cpu.h"
#include "buf.h"
#include "timer.h"

#include "scheduler.h"
#include "io.h"
#include "page_gc.h"
#include "page_req.h"
#include "page_meta.h"

#define PRINTF		//	SIM_Print

Queue<ReqInfo*, SIZE_REQ_QUE> gstReqQ;

void FTL_Request(ReqInfo* pReq)
{
	pReq->nSeqNo = SIM_GetSeqNo();
	gstReqQ.PushTail(pReq);
	Sched_TrigAsyncEvt(BIT(EVT_USER_CMD));
	CPU_Wakeup(CPU_FTL, SIM_USEC(1));
}

uint32 FTL_GetNumLPN(CbfReq pfCbf)
{
	REQ_SetCbf(pfCbf);
	CPU_TimePass(SIM_MSEC(1));	// Wait Init time.
	return NUM_LPN;
}

void FTL_Main(void* pParam)
{
	TMR_Init();
	BM_Init();

	Sched_Init();

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

