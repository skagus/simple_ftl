#include <stdio.h>
#include <windows.h>
#include <assert.h>
#include "coroutine.h"


#define STK_SIZE	(16 * 1024)

struct RoutineInfo
{
	HANDLE hTask;		///< CPU entry point.
};

static HANDLE gnMain;		///< Task for engine..
static RoutineInfo gaRoutines[MAX_CO_ROUTINE];
static SimTaskId gnCntTask;
static SimTaskId gnCurTask;

SimTaskId CO_RegTask(Routine pfEntry, void* pParam)
{
	assert(gnCntTask < MAX_CO_ROUTINE);

	SimTaskId nTaskId = gnCntTask;
	gaRoutines[nTaskId].hTask = CreateFiber(STK_SIZE,
			(LPFIBER_START_ROUTINE)pfEntry, pParam);
	gnCntTask++;
	return nTaskId;
}

void CO_Fine()
{
	for (uint32 nIdx = 0; nIdx < MAX_CO_ROUTINE; nIdx++)
	{
		if (nullptr != gaRoutines[nIdx].hTask)
		{
			DeleteFiber(gaRoutines[nIdx].hTask);
		}
	}
	gnCntTask = 0;
}

void CO_Switch(SimTaskId nIdx)
{
	gnCurTask = nIdx;
	SwitchToFiber(gaRoutines[nIdx].hTask);
}

SimTaskId CO_GetCurTask()
{
	return gnCurTask;
}
