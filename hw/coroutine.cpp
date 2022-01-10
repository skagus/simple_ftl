#include <stdio.h>
#include "sim.h"
#include "cpu.h"
#include "coroutine.h"

#define STK_SIZE	(16 * 1024)

typedef unsigned long long u64;

/* coroutine functions */
extern "C" void yield_(stack_t *token);
extern "C" void enter_(stack_t *token, user_t arg);

stack_t* gpCurStack;

struct Task
{
	stack_t pStkTop;
	char aStack[STK_SIZE];
};

Task gaTask[NUM_CPU];

/* prepare a coroutine stack */
void prepare(stack_t *token, void *stack, u64 size, cofunc_t func)
{
	u64 *s64 = (u64*)((char*)stack + size);
	s64 -= 10;                   // 10 items exist on stack
	s64[0] = 15;                  // R15
	s64[1] = 14;                  // R14
	s64[2] = 13;                  // R13
	s64[3] = 12;                  // R12
	s64[4] = 11;                  // RSI
	s64[5] = 10;                  // RDI
	s64[6] = (u64)s64 + 64;      // RBP
	s64[7] = 9;                  // RBX
	s64[8] = (u64)func;          // return address
	s64[9] = (u64)yield_;        // coroutine return address
	*token = (stack_t)s64;       // save the stack for yield
}

void CO_Start(int nIdx, cofunc_t pfEntry)
{
	assert(nIdx < NUM_CPU);
	/* prepare the stack */
	prepare(&(gaTask[nIdx].pStkTop), gaTask[nIdx].aStack, STK_SIZE, pfEntry);
	gpCurStack = &(gaTask[nIdx].pStkTop);
	enter_(gpCurStack, (void*)0x11);
}

/**
* Return main thread.
*/
void CO_Yield()
{
	yield_(gpCurStack);
}

/**
* Switch coroutine (indexed at start)
*/
void CO_Switch(int nIdx)
{
	/**
	1. 너의 stack top을 pStkTop에 저장해서 건내줄께,
	2. 거기에 나의 stack top을 저장하고,
		너의 stack top으로 실행을 해.
	3. 실행을 끝나면,
		너가 저장했던 나의 stack top으로 switching하고,
		pStkTop에는 너의 stack top을 저장해줘.
	*/
	gpCurStack = &(gaTask[nIdx].pStkTop);
	yield_(gpCurStack);
}
