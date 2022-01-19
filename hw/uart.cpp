#include "sim.h"
#include "cpu.h"
#include "uart.h"

#define MAX_TX_BUF		(128)
enum Act
{
	UART_SingleTx,
	UART_DmaTx,
	UART_Rx,
};

struct UartEvt
{
	Act eAct;
	char nChar;
};

struct UartConfig
{
	int nEvtPeriod;
	char nRxData;	// CPU RX.
	uint16 nTxData;	// CPU TX.
	///// ISR related ////
	uint32 nTxIsrCpu;
	Cbf pfRxIsr;
	uint32 nRxIsrCpu;
	Cbf pfTxIsr;
};

struct UartDma
{
	char anTxDma[MAX_TX_BUF];
	bool bTxRun;
	uint8 nCurTx;
	uint8 nTxLen;
};

UartConfig gstCfg;
UartDma gstDma;

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
	if(UART_SingleTx == pstEvt->eAct)
	{
		putchar(pstEvt->nChar);
		gstCfg.nTxData = 0xFFFF;
	}
	else if (UART_DmaTx == pstEvt->eAct)
	{
		putchar(pstEvt->nChar);
		gstDma.nCurTx++;
		if (gstDma.nCurTx < gstDma.nTxLen)
		{
			uart_NewEvt(UART_DmaTx, gstDma.anTxDma[gstDma.nCurTx]);
		}
		else
		{
			gstDma.bTxRun = false;
			gstCfg.nTxData = 0xFFFF;
		}
	}
	else if (UART_Rx == pstEvt->eAct)
	{
		gstCfg.nRxData = pstEvt->nChar;
	}
}

uint8 UART_RxD(char* pCh)
{
	if (0xFFFF == gstCfg.nRxData)
	{
		return 0;
	}
	*pCh = gstCfg.nRxData;
	gstCfg.nRxData = 0xFFFF;
	return 1;
}

uint8 UART_TxD(char nCh)
{
	while(gstCfg.nTxData != 0xFFFF)
	{
		CPU_TimePass(SIM_USEC(1));
	}
	gstCfg.nTxData = nCh;
	uart_NewEvt(UART_SingleTx, nCh);
	return 0;
}

void UART_PutsDMA(char* szString)
{
	while (0xFFFF != gstCfg.nTxData)
	{
		CPU_TimePass(SIM_USEC(1));
	}
	gstDma.bTxRun = true;
	gstDma.nTxLen = strlen(szString);
	gstDma.nCurTx = 0;
	gstCfg.nTxData = gstDma.anTxDma[0];
	memcpy(gstDma.anTxDma, szString, gstDma.nTxLen);
	uart_NewEvt(UART_DmaTx, gstDma.anTxDma[0]);
}

void UART_Puts(char* szString)
{
	while (0 != *szString)
	{
		UART_TxD(*szString);
		szString++;
	}
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
	gstCfg.nEvtPeriod = SIM_SEC(10) / nBps;
	gstCfg.nTxData = 0xFFFF;
}

void UART_InitSim()
{
	SIM_AddHW(HW_UART, uart_HandleEvt);
}
