#pragma once

#include "sim.h"
//#define MS_PER_TICK     (1)
//#define SCHED_MSEC(x)	((x)/MS_PER_TICK)
//#define LONG_TIME		(SCHED_MSEC(3000))	// 3 sec.
#define MAX_OS			(1)

#define TICK_SIM_PER_OS			(SIM_MSEC(1))	///< sim tick period per OS tick.
#define OS_MSEC(msec)			(1)	///< tick per msec.
#define OS_SEC(sec)				((sec) * OS_MSEC(1000UL))	///< tick per sec.

#define LONG_TIME				(OS_SEC(3))	// 3 sec.

typedef enum
{
	EVT_OPEN,
	EVT_META,
	EVT_BUF,
	EVT_CACHE,
	EVT_IO_FREE,
	EVT_NAND_CMD,
	EVT_USER_CMD,
	EVT_BLK_REQ,
	EVT_NEW_BLK,
	NUM_EVT
} evt_id;

typedef enum
{
	TID_REQ,
	TID_REQ_RESP,
	TID_GC,
	TID_META,
	NUM_TASK,
} TaskId;
