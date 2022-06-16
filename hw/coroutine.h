#pragma once
#include "types.h"

#define MAX_CO_ROUTINE			(33)

typedef uint32 SimTaskId;
typedef void(*Routine)(void* pParam);

SimTaskId CO_RegTask(Routine pfEntry, void* pParam);
SimTaskId CO_GetCurTask();
void CO_Switch(SimTaskId nIdx);
void CO_Fine();
void CO_Start();
