#include <stdio.h>
#include "sim.h"
#include "cpu.h"
#include "coroutine.h"


#define STK_SIZE	(16 * 1024)

#if (OPT_CO == CO_FIBER)
struct RoutineInfo
{
	HANDLE hTask;		///< CPU entry point.
	Routine fEntry;
	void* pParam;
};

static HANDLE ghEngine;		///< Engine용 Task.
static RoutineInfo gaRoutines[NUM_CPU];

void CO_RegTask(int nIdx, Routine pfEntry, void* pParam)
{
	gaRoutines[nIdx].fEntry = pfEntry;
	gaRoutines[nIdx].pParam = pParam;
}

void CO_Start()
{
	if (nullptr == ghEngine) // 몇번 반복할 수도 있으니까...
	{
		ghEngine = ConvertThreadToFiber(nullptr);
	}
	for (uint32 nIdx = 0; nIdx < CpuID::NUM_CPU; nIdx++)
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

#define NUM_ROUTINE		(NUM_CPU)
#define PRINT_DBG		//printf 

#if _M_AMD64
#define PATCH_SET_JMP(jbuf)			{((_JUMP_BUFFER*)&(jbuf))->Frame = 0;}
#else
#define PATCH_SET_JMP(jbuf)
#endif

#define TASK_STACK_SIZE (16 * 1024)

typedef void(*routine)(void* nParam);
volatile routine gaRoutines[NUM_ROUTINE];
void* ganParam[NUM_ROUTINE];
jmp_buf gaContext[NUM_ROUTINE];

jmp_buf gMainCtx;
volatile int gnTaskId;

#pragma optimize("", off)

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
	stack 설정시 local변수가 보존되게 하려면, 깊은 stack을 먼저 할당해야 한다.
	*/
	for (int i = NUM_ROUTINE - 1; i >= 0; i--)
	{
		co_Recusive(i + 1, i);
	}
	gnTaskId = NUM_ROUTINE;
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
	gnTaskId = NUM_ROUTINE;
}

void CO_RegTask(int nTaskId, routine pfTask, void* nParam)
{
	gaRoutines[nTaskId] = pfTask;
	ganParam[nTaskId] = nParam;
}
#pragma optimize("", on)

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
	for (int i = 0; i < NUM_ROUTINE; i++)
	{
		gaRoutines[i] = pfDummy;
	}
}

#endif
