
#include "sim.h"
#include "timer.h"
#include "cpu.h"
#include "uart.h"
#include "power.h"

void tmr_End(uint32 tag, uint32 result)
{
	SIM_Print("TMR Expire: %d\n", SIM_GetTick());
}

void start_cpu(void* pParam)
{
	UART_Init(19200);
	TMR_Init();
	TMR_Add(0, SIM_MSEC(7), tmr_End, true);
	uint32 nLoop = 5;
	while (nLoop-- > 0)
	{
		SIM_Print("In Cpu loop rest %d\n", nLoop);
		CPU_TimePass(SIM_MSEC(8));
		SIM_Print("rest 2\n");
		UART_PutsDMA((char*)"Hello");
		CPU_TimePass(SIM_MSEC(100));
		UART_Puts((char*)"Night");
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