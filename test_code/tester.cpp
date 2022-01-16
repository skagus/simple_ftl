
#include "sim.h"
#include "timer.h"
#include "cpu.h"
#include "power.h"

void tmr_End(uint32 tag, uint32 result)
{
	SIM_Print("TMR Expire: %d\n", SIM_GetTick());
}

void start_cpu(void* pParam)
{
	TMR_Init();
	TMR_Add(0, 7, tmr_End, true);
	uint32 nLoop = 5;
	while (nLoop-- > 0)
	{
		SIM_Print("In Cpu loop rest %d\n", nLoop);
		CPU_TimePass(SIM_USEC(8));
		SIM_Print("rest 2\n");
		CPU_TimePass(SIM_USEC(1));
		SIM_Print("rest 1\n");
		CPU_TimePass(SIM_USEC(1));
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
//	CPU_Add(0, start_cpu, nullptr);
	CPU_Add(1, start_cpu, nullptr);
	SIM_Run();
	return 0;
}