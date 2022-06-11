#pragma once

#include <Windows.h>
#include "types.h"

#if EN_SIM
typedef HANDLE ThreadId;
#endif
typedef void (*TFunc)(void* pParam);

ThreadId CreateThread(uint32 nStackSize, TFunc pfFunc, void* pArg);
ThreadId CreateThread();
void SwitchTo(ThreadId nThread);

#if EN_SIM
void FinThread(ThreadId nThread);
ThreadId GetCurrentTread();
#endif