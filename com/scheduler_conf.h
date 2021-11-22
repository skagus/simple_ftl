#pragma once

#define MS_PER_TICK     (1)
#define SCHED_MSEC(x)	((x)/MS_PER_TICK)
#define LONG_TIME		(SCHED_MSEC(3000))	// 3 sec.

typedef enum
{
	EVT_OPEN,
	EVT_META,
	EVT_BUF,
	EVT_NAND_CMD,
	EVT_USER_CMD,
	EVT_BLOCK,
	NUM_EVT
} evt_id;

/**
* Run mode는 특정 상태에서 runnable task관리를 용이하도록 하기 위함.
*/
typedef enum
{
	MODE_NORMAL,	// default mode
	MODE_SLEEP,
	MODE_FAIL_SAFE,
	NUM_MODE,
} RunMode;

typedef enum
{
	TID_REQ,
	TID_REQ_RESP,
	TID_GC,
	TID_GC_RESP,
	TID_META,
	TID_META_RESP,
	TID_WRITE,
	TID_WRITE_RESP,
	NUM_TASK,
} TaskId;
