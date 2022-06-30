
#pragma once
#include <assert.h>
#include <string.h>
#include "types.h"

/*************************************************************
Queue는 pool로서 사용할 수도 있고, 
communication channel로 사용할 수도 있다.
*************************************************************/

template <typename T, int SIZE>
class LRU
{
	T nAge;
	T anAge[SIZE];
public:
	void Init()
	{
		memset(anAge, 0x0, sizeof(anAge));
		nAge = 0;
	}
	void SetOld(int nIdx)
	{
		anAge[nIdx] = (nAge > 1000) ? (nAge - 1000) : 0;
	}
	void Touch(int nIdx)
	{
		anAge[nIdx] = nAge;
		nAge++;
	}
	int GetOld()
	{
		int nOldIdx = 0;
		int nOldAge = anAge[0];
		for (int nIdx = 1; nIdx < SIZE; nIdx++)
		{
			if (nOldAge > anAge[nIdx])
			{
				nOldAge = anAge[nIdx];
				nOldIdx = nIdx;
			}
		}
		return nOldIdx;
	}
};

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
		assert(nullptr != pEntry);
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
		if (0 == nCount)
		{
			assert(nullptr == pHead);
			pTail = nullptr;
		}
		nCount--;
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
	int count;

public:
	int Count() { return count; }
	bool IsEmpty() { return 0 == count; }
	bool IsFull(){ return (count == SIZE); }
	T GetHead()
	{
		assert(count > 0);
		return data[head];
	}
	T PopHead()
	{
		assert(count > 0);
		T entry = data[head];
		head = (head + 1) % SIZE;
		count--;
		return entry;
	}
	void PushTail(T entry)
	{
		data[tail] = entry;
		tail = (tail + 1) % SIZE;
		count++;
		assert(count <= SIZE);
	}
	void Init()
	{
		count = 0;
		head = 0;
		tail = 0;
	}
};

