#include <stdio.h>
#include "coroutine.h"


#define STK_SIZE	(16 * 1024)

#if (OPT_CO == CO_FIBER)
#include <windows.h>

struct RoutineInfo
{
	HANDLE hTask;		///< CPU entry point.
	Routine fEntry;
	void* pParam;
};

static HANDLE ghEngine;		///< Task for engine..
static RoutineInfo gaRoutines[MAX_ROUTINE];

void CO_RegTask(int nIdx, Routine pfEntry, void* pParam)
{
	gaRoutines[nIdx].fEntry = pfEntry;
	gaRoutines[nIdx].pParam = pParam;
}

void CO_Start()
{
	if (nullptr == ghEngine) // to support multiple power cycle.
	{
		ghEngine = ConvertThreadToFiber(nullptr);
	}
	for (uint32 nIdx = 0; nIdx < MAX_ROUTINE; nIdx++)
	{
		if (nullptr != gaRoutines[nIdx].hTask)
		{
			DeleteFiber(gaRoutines[nIdx].hTask);
		}
		gaRoutines[nIdx].hTask = CreateFiber(STK_SIZE,
			(LPFIBER_START_ROUTINE)gaRoutines[nIdx].fEntry, gaRoutines[nIdx].pParam);
	}
}

void CO_Switch(int nIdx)
{
	SwitchToFiber(gaRoutines[nIdx].hTask);
}

void CO_ToMain()
{
	SwitchToFiber(ghEngine);
}

#elif (OPT_CO == CO_SETJMP)
#include <setjmp.h>

#define PRINT_DBG		//printf 

#if _M_AMD64
#define PATCH_SET_JMP(jbuf)			{((_JUMP_BUFFER*)&(jbuf))->Frame = 0;}
#else
#define PATCH_SET_JMP(jbuf)
#endif

#define TASK_STACK_SIZE (16 * 1024)

typedef void(*routine)(void* nParam);
volatile routine gaRoutines[MAX_ROUTINE];
void* ganParam[MAX_ROUTINE];
jmp_buf gaContext[MAX_ROUTINE];

jmp_buf gMainCtx;
volatile int gnTaskId;

void co_Recusive(int nDepth, int nTaskId)
{
	volatile char aStack[TASK_STACK_SIZE]; // Stacks for each Task. 
	nDepth--;
	if (0 == nDepth)
	{
		volatile int a = nTaskId * 10;
		PRINT_DBG("Set Stack:%d: %X, %X, %d\n", nTaskId, aStack, &a, *&a);
		size_t nRet = setjmp(gaContext[nTaskId]);
		PATCH_SET_JMP(gaContext[gnTaskId]);
		if (0 == nRet)
		{
			return;
		}
		PRINT_DBG("Start Task:%d, %X, %d\n", gnTaskId, &a, *&a);
		gaRoutines[gnTaskId](ganParam[gnTaskId]);
	}
	else
	{
		co_Recusive(nDepth, nTaskId);
	}
	aStack[0] = 10; // to keep aStack from optimize. 
}

void CO_Start()
{
	/**
	reserved deeper stack 1st to keep some local variable.(actually no meaning..  :) )
	*/
	for (int i = MAX_ROUTINE - 1; i >= 0; i--)
	{
		co_Recusive(i + 1, i);
	}
	gnTaskId = MAX_ROUTINE;
}

void CO_ToMain()
{
	size_t nRet = setjmp(gaContext[gnTaskId]);
	PATCH_SET_JMP(gMainCtx);
	if (0 == nRet)
	{
		PRINT_DBG("To Main: %d\n", gnTaskId);
		longjmp(gMainCtx, 1);
	}
	PRINT_DBG("From Main: %d\n", gnTaskId);
}

void CO_Switch(int nTaskId)
{
	gnTaskId = nTaskId;
	size_t nRet = setjmp(gMainCtx);
	PATCH_SET_JMP(gaContext[gnTaskId]);
	if (0 == nRet)
	{
		PRINT_DBG("To Task: %d\n", gnTaskId);
		longjmp(gaContext[gnTaskId], 1);
	}
	gnTaskId = MAX_ROUTINE;
}

void CO_RegTask(int nTaskId, routine pfTask, void* nParam)
{
	gaRoutines[nTaskId] = pfTask;
	ganParam[nTaskId] = nParam;
}

void co_DummyTask(void* nParam)
{
	while (true)
	{
		CO_ToMain();
	}
}

void CO_Init(routine pfDummy)
{
	if (nullptr == pfDummy)
	{
		pfDummy = co_DummyTask;
	}
	for (int i = 0; i < MAX_ROUTINE; i++)
	{
		gaRoutines[i] = pfDummy;
	}
}

#endif
