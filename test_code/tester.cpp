#include "sim.h"
#include "timer.h"
#include "cpu.h"
#include "uart.h"
#include "power.h"

void tmr_End(uint32 tag, uint32 result)
{
	SIM_Print("TMR Expire: %d\n", SIM_GetTick());
}

const char* gaSendData = "Received\n Data";
void start_cpu(void* pParam)
{
	UART_Init(19200);
	TMR_Init();
	TMR_Add(0, SIM_MSEC(7), tmr_End, true);
	uint32 nLoop = 5;
	SIM_FromOther(SIM_MSEC(10), (uint8*)gaSendData, strlen(gaSendData));
	uint8 aBuf[10];
	while (nLoop-- > 0)
	{
		SIM_Print("In Cpu loop rest %d\n", nLoop);
		CPU_TimePass(SIM_MSEC(8));
		uint32 nByte = UART_Gets(aBuf, 10);
		SIM_Print("Rcv Byte: %d\n", nByte);
		UART_Puts((uint8*)"Hello", strlen("Hello"), true);
		SIM_Print("rest 2\n");
		CPU_TimePass(SIM_MSEC(1));
		SIM_Print("rest 11\n"); 
		UART_Puts((uint8*)"Night", strlen("Night"), true);
		SIM_Print("rest 1\n");
		CPU_TimePass(SIM_MSEC(1));
	}
	POWER_SwitchOff();
	END_RUN;
}


int gnAddCheck;

int main()
{
	int nAddCheck;
#ifdef _WIN64
	printf("Add Chk: %llX, %llX, %llX\n", &gnAddCheck, &nAddCheck, main);
#else
	printf("Add Chk: %X, %X, %X\n", &gnAddCheck, &nAddCheck, main);
#endif
	CPU_InitSim();
	TMR_InitSim();
	POWER_InitSim();
	UART_InitSim();
//	CPU_Add(0, start_cpu, nullptr);
	CPU_Add(1, start_cpu, nullptr);
	SIM_Run();
	return 0;
}
