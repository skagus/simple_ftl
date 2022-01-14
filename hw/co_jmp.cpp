
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <setjmp.h>
#include "sim.h"
#if (OPT_CO == CO_SETJMP)

#define NUM_ROUTINE (3)
#define PRINT_DBG // printf 

#define TASK_STACK_SIZE (4 * 1024)

typedef void(*routine)();
volatile routine gaRoutines[NUM_ROUTINE];
jmp_buf gaContext[NUM_ROUTINE];
jmp_buf gMainCtx;
volatile int gnTaskId;

void co_Recusive(int nDepth, int nTaskId)
{
	volatile char aStack[TASK_STACK_SIZE]; // Stacks for each Task. 
	nDepth--;
	if (0 == nDepth)
	{
		if (0 == setjmp(gaContext[nTaskId]))
		{
			return;
		}
		PRINT_DBG(" Start Task:%d \n ", gnTaskId);
		gaRoutines[gnTaskId]();
	}
	else
	{
		co_Recusive(nDepth, nTaskId);
	}
	aStack[0] = 10; // to keep aStack from optimize. 
}

void CO_Start()
{
	for (int i = 0; i < NUM_ROUTINE; i++)
	{
		co_Recusive(i + 1, i);
	}
	gnTaskId = NUM_ROUTINE;
}

void CO_ToMain()
{
	if (0 == setjmp(gaContext[gnTaskId]))
	{
		PRINT_DBG(" To Main: %d \n ", gnTaskId);
		longjmp(gMainCtx, 1);
	}
	PRINT_DBG(" From Main: %d \n ", gnTaskId);
}

void CO_Schedule(int nTaskId)
{
	gnTaskId = nTaskId;
	if (0 == setjmp(gMainCtx))
	{
		PRINT_DBG(" To Task: %d \n ", gnTaskId);
		longjmp(gaContext[gnTaskId], 1);
	}
	gnTaskId = NUM_ROUTINE;
}

void CO_RegTask(int nTaskId, routine pfTask)
{
	gaRoutines[nTaskId] = pfTask;
}

void co_DummyTask()
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

void Ping()
{
	int nCalls = 0;
	printf(" Start Ping: %X \n ", &nCalls);
	while (true)
	{
		printf(" Ping %d \n ", nCalls++);
		CO_ToMain();
	}
}

void Pong()
{
	int nCalls = 0;
	printf(" Start Pong: %X \n ", &nCalls);
	while (true)
	{
		printf(" \t\t Pong %d \n ", nCalls++);
		CO_ToMain();
	}
}

void coroutine_test()
{
	srand(0);
	CO_Init(nullptr);

	CO_RegTask(0, Ping);
	CO_RegTask(1, Pong);

	CO_Start();
	int nSel = 0;
	while (true)
	{
		assert(gnTaskId == NUM_ROUTINE);
		CO_Schedule(nSel % NUM_ROUTINE);
		assert(gnTaskId == NUM_ROUTINE);
		nSel++;
	}
}

int main()
{
	coroutine_test();
}
#endif