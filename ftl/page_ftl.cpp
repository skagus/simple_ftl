#include "templ.h"
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

/**
* Called by other CPU.
*/
void FTL_Request(ReqInfo* pReq)
{
	pReq->nSeqNo = SIM_GetSeqNo();
	gstReqQ.PushTail(pReq);
	Sched_TrigAsyncEvt(BIT(EVT_USER_CMD));
	CPU_TimePass(SIM_USEC(5));
	CPU_Wakeup(CPU_FTL, SIM_USEC(1));
}

/**
* Called by other CPU.
*/
uint32 FTL_GetNumLPN(CbfReq pfCbf)
{
	REQ_SetCbf(pfCbf);
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
	Sched_Run();
}

