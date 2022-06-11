#include "thread.h"

static ThreadId gnCurThread;

ThreadId CreateThread(uint32 nStackSize, TFunc pfFunc, void* pArg)
{
	return CreateFiber(nStackSize, pfFunc, pArg);
}

ThreadId CreateThread()
{
	return ConvertThreadToFiber(nullptr);
}

void FinThread(ThreadId nThread)
{
	DeleteFiber(nThread);
}

ThreadId GetCurrentTread()
{
	return gnCurThread;
}

void SwitchTo(ThreadId nThread)
{
	gnCurThread = nThread;
	SwitchToFiber(nThread);
}