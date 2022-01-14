#pragma once
#include "sim_conf.h"

#define CO_FIBER		1
#define CO_SETJMP		2

#define OPT_CO			(CO_SETJMP)		///< Selection between coroutine type.

typedef void(*Routine)(void* pParam);
void CO_RegTask(int nIdx, Routine pfEntry, void* pParam);
void CO_Start();
void CO_Switch(int nIdx);
void CO_ToMain();
