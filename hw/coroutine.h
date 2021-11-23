#pragma once

typedef void * user_t;
typedef void *stack_t;
typedef void(*cofunc_t)(stack_t *token, user_t arg);

void CO_Start(int nIdx, cofunc_t pfEntry);
void CO_Switch(int nIdx);
void CO_Yield();
