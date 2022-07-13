#include "templ.h"
#include "cpu.h"
#include "buf.h"
#include "timer.h"

#include "os.h"
#include "io.h"
#include "page_gc.h"
#include "page_req.h"
#include "page_meta.h"
#include "buf_cache.h"

#define PRINTF		//	SIM_Print

Queue<ReqInfo*, SIZE_REQ_QUE> gstReqQ;

/**
* Called by other CPU.
*/
void FTL_Request(ReqInfo* pReq)
{
	pReq->nSeqNo = SIM_IncSeqNo();
	gstReqQ.PushTail(pReq);
	OS_AsyncEvt(BIT(EVT_USER_CMD));
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

	OS_Init();

	gstReqQ.Init();
	IO_Init();
	REQ_Init();
	META_Init();
	GC_Init();
#if EN_BUF_CACHE
	BC_Init();
#endif

	PRINTF("[FTL] Init done\n");
	OS_Start();
}

