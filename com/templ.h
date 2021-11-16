#pragma once
#include <assert.h>
#include "types.h"

/*************************************************************
Queue는 pool로서 사용할 수도 있고, 
communication channel로 사용할 수도 있다.
*************************************************************/

template <typename T>
class LinkedQueue
{
	uint32 nCount;
	T* pHead;
	T* pTail;

public:
	uint32 Count() { return nCount; }
	void Init()
	{
		nCount = 0;
		pHead = nullptr;
		pTail = nullptr;
	}
	T* GetHead()
	{
		return pHead;
	}
	void PushTail(T* pEntry)
	{
		pEntry->pNext = nullptr;
		if (0 == nCount)
		{
			assert(nullptr == pHead);
			pHead = pEntry;
			pTail = pEntry;
		}
		else
		{
			pTail->pNext = pEntry;
			pTail = pEntry;
		}
		nCount++;
	}
	T* PopHead()
	{
		if (0 == nCount)
		{
			return nullptr;
		}
		T* pPop = pHead;
		pHead = pHead->pNext;
		nCount--;
		if (0 == nCount)
		{
			assert(nullptr == pHead);
			pTail = nullptr;
		}
		pPop->pNext = nullptr;
		return pPop;
	}
};

template <typename T, int SIZE>
class Queue
{
	T data[SIZE];
	int head;
	int tail;

public:
	int Count(){ return (head + SIZE - tail) % SIZE; }
	bool IsEmpty(){ return head == tail; }
	bool IsFull(){ return (Count() == SIZE); }
	T GetHead()
	{
		return data[head];
	}
	T PopHead()
	{
		T entry = data[head];
		head = (head + 1) % SIZE;
		return entry;
	}
	void PushTail(T entry)
	{
		data[tail] = entry;
		tail = (tail + 1) % SIZE;
	}
	void Init()
	{
		head = 0;
		tail = 0;
	}
};

