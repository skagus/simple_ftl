#include "sim.h"
#include "cpu.h"
#include "uart.h"

#define EN_HALT_ON_BUSY		(1)		// Sleep CPU on waiting TX DMA.


enum Act
{
	UART_Idle,
	UART_Tx,
	UART_DmaTx,
	UART_Rx,	///< Received data
	UART_DmaRx,
};

struct UartEvt
{
	Act eAct;
	char nChar;
};

struct RxCtx
{
	Act eMode;	// receive into DMA buffer or nData.

	bool bHasData;
	uint8 nData;	// received data in 

	uint8* aDmaBuf; // used on DMA.
	uint32 nDmaBufSize;
	uint32 nDmaBufIdx;
	uint8 nWaitCpu;

	void SetNewBuf(uint8* aBuf, uint32 nSize)
	{
		aDmaBuf = aBuf;
		nDmaBufSize = nSize;
		nDmaBufIdx = 0;
		eMode = UART_DmaRx;
	}
	bool Push(uint8 nRxData)
	{
		if (UART_DmaRx == eMode)
		{
			if (nDmaBufIdx < nDmaBufSize)
			{
				aDmaBuf[nDmaBufIdx] = nRxData;
				nDmaBufIdx++;
			}
			if (nDmaBufIdx == nDmaBufSize)
			{
				eMode = UART_Idle;
				return true;
			}
		}
		else if(UART_Rx == eMode)
		{
			eMode = UART_Idle;
			bHasData = true;
			nData = nRxData;
			return true;
		}
		return false;
	}
};

struct TxCtx
{
	Act eAct;
#if EN_HALT_ON_BUSY
	uint8 nCpuId;   ///< Cpu ID that waits current done.
#endif
	uint8* aDmaBuf; // used on DMA.
	uint32 nDmaBufSize;
	uint32 nDmaBufIdx;

	void AddNewDatas(uint8* aData, uint32 nSize)
	{
		aDmaBuf = aData;
		nDmaBufSize = nSize;
		nDmaBufIdx = 0;
	}

	bool PopNext(uint8* pnData)
	{
		if (nDmaBufSize > nDmaBufIdx)
		{
			*pnData = aDmaBuf[nDmaBufIdx];
			nDmaBufIdx++;
			return true;
		}
		return false;
	}
};


struct UartConfig
{
	int nEvtPeriod;
	///// ISR related ////
	uint32 nTxIsrCpu;
	Cbf pfTxIsr;
	uint32 nRxIsrCpu;
	Cbf pfRxIsr;
};

UartConfig gstCfg;
TxCtx gstTxCtx;
RxCtx gstRxCtx;

TxCtx gstSimTxCtx;

UartEvt* uart_NewEvt(Act eAct, char nCh)
{
	UartEvt* pEvt = (UartEvt*)SIM_NewEvt(HW_UART, gstCfg.nEvtPeriod);
	pEvt->eAct = eAct;
	pEvt->nChar = nCh;
	return pEvt;
}

void uart_HandleEvt(void* pEvt)
{
	UartEvt* pstEvt = (UartEvt*)pEvt;
	if(UART_Tx == pstEvt->eAct)
	{
		putchar(pstEvt->nChar);
		gstTxCtx.eAct = UART_Idle;
	}
	else if (UART_DmaTx == pstEvt->eAct)
	{
		putchar(pstEvt->nChar);
		uint8 nData;
		if (gstTxCtx.PopNext(&nData))
		{
			uart_NewEvt(UART_DmaTx, nData);
		}
		else
		{
#if EN_HALT_ON_BUSY
			if (NUM_CPU != gstTxCtx.nCpuId)
			{
				CPU_Wakeup(gstTxCtx.nCpuId);
				gstTxCtx.nCpuId = NUM_CPU;
			}
#endif
			gstTxCtx.eAct = UART_Idle;
		}
	}
	else if (UART_Rx == pstEvt->eAct)
	{
		if (gstRxCtx.Push(pstEvt->nChar) || ('\n' == pstEvt->nChar))
		{
			CPU_Wakeup(gstRxCtx.nWaitCpu);
		}
		uint8 nData;
		if (gstSimTxCtx.PopNext(&nData))
		{
			UartEvt* pEvt = (UartEvt*)SIM_NewEvt(HW_UART, gstCfg.nEvtPeriod);
			pEvt->eAct = UART_Rx;
			pEvt->nChar = nData;
		}
	}
}


void UART_InitSim()
{
	SIM_AddHW(HW_UART, uart_HandleEvt);
}
//////////////////// Other CPU API //////////////

/**
* Send to 
*/
void SIM_FromOther(uint64 nSkipTime, uint8* aBuf, uint32 nBytes)
{
	gstSimTxCtx.AddNewDatas(aBuf, nBytes);
	uint8 nData;
	if (gstSimTxCtx.PopNext(&nData))
	{
		UartEvt* pEvt = (UartEvt*)SIM_NewEvt(HW_UART, nSkipTime);
		pEvt->eAct = UART_Rx;
		pEvt->nChar = nData;
	}
}
//////////////////// FW API /////////////////////

bool UART_RxD(uint8* pCh)
{
	gstRxCtx.eMode = UART_Rx;
	gstRxCtx.bHasData = false;
	while(false == gstRxCtx.bHasData)
	{
		CPU_TimePass(SIM_USEC(1));
	}
	*pCh = gstRxCtx.nData;
	gstRxCtx.eMode = UART_Idle;
	return true;
}

void UART_TxD(uint8 nCh)
{
	while(UART_Idle != gstTxCtx.eAct)
	{
		CPU_TimePass(SIM_USEC(1));
	}
	gstTxCtx.eAct = UART_Tx;
	uart_NewEvt(UART_Tx, nCh);
}

void UART_Puts(uint8* szString, uint32 nBytes, bool bDMA)
{
	if (bDMA)
	{
		while (UART_Idle != gstTxCtx.eAct)
		{
			CPU_TimePass(SIM_USEC(1));
#if EN_HALT_ON_BUSY
			gstTxCtx.nCpuId = CPU_GetCpuId();
			CPU_Sleep();
#endif
		}
		gstTxCtx.eAct = UART_DmaTx;
		gstTxCtx.AddNewDatas(szString, nBytes);
		uint8 nData;
		gstTxCtx.PopNext(&nData);
		uart_NewEvt(UART_DmaTx, nData);
	}
	else
	{
		while (0 != *szString)
		{
			UART_TxD(*szString);
			szString++;
		}
	}
}

uint32 UART_Gets(uint8* aBuf, uint32 nBufSize)
{
	gstRxCtx.eMode = UART_DmaRx;
	gstRxCtx.SetNewBuf(aBuf, nBufSize);
	gstRxCtx.nWaitCpu = CPU_GetCpuId();
	while(true)
	{
		CPU_Sleep();
		if ((gstRxCtx.nDmaBufIdx >= nBufSize)
			|| ((gstRxCtx.nDmaBufIdx > 0) && ('\n' == aBuf[gstRxCtx.nDmaBufIdx - 1])))
		{
			break;
		}
	}
	uint32 nBytes = gstRxCtx.nDmaBufIdx;
	return nBytes;
}


void UART_SetCbf(Cbf pfRx, Cbf pfTx)
{
	if (nullptr != pfRx)
	{
		gstCfg.pfRxIsr = pfRx;
		gstCfg.nRxIsrCpu = CPU_GetCpuId();
	}
	if (nullptr != pfTx)
	{
		gstCfg.pfTxIsr = pfTx;
		gstCfg.nTxIsrCpu = CPU_GetCpuId();
	}
}

void UART_Init(uint32 nBps)
{
	MEMSET_PTR(&gstCfg, 0x00);
	MEMSET_PTR(&gstTxCtx, 0x00);
	MEMSET_PTR(&gstRxCtx, 0x00);
	gstCfg.nEvtPeriod = SIM_SEC(10) / nBps;
}
