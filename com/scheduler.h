
#include <stdint.h>
#include "types.h"
#include "scheduler_conf.h"

#if (NUM_TASK <= 8)
typedef uint8 TaskBtm;
#elif (NUM_TASK <= 16)
typedef uint16 TaskBtm;
#elif (NUM_TASK <= 32)
typedef uint32 TaskBtm;
#endif

#if (NUM_EVT <= 8)
typedef uint8 Evts;
#elif (NUM_EVT <= 16)
typedef uint16 Evts;
#elif (NUM_EVT <= 32)
typedef uint32 Evts;
#endif

typedef void(*Entry)(void* pParam);

Cbf Sched_Init();
void Sched_Register(uint8 nTaskID, Entry pfTask, void* pParam, uint8 bmRunMode); ///< Register tasks.
void Sched_SetMode(RunMode eMode);
RunMode Sched_GetMode();
void Sched_Run();
void Sched_Wait(Evts bmEvt, uint16 nTick);
void Sched_TrigSyncEvt(Evts bmEvt);
void Sched_TrigAsyncEvt(Evts bmEvt);

#define Sched_Yield()	Sched_Wait(0, 0)
