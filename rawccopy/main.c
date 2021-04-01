#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>

#include <stdio.h>
#include "context.h"
#include "processor.h"

#include "regex.h"

#define RCC_VERSION "0.1.7"

uint64_t TimeStamp();
uint64_t ElapsedTime(uint64_t since);


int main(int argc, char* argv[])
{
	printf("RawCCopy v%s\n\n", RCC_VERSION);
	uint64_t start = TimeStamp();
	execution_context cont = SetupContext(argc, argv);
	if (!cont)
		exit(-1);

	if (!PerformOperation(cont))
	{
		CleanUp(cont);
		exit(-2);
	}
	CleanUp(cont);
	uint64_t duration = ElapsedTime(start);
	printf("Job took %.2f seconds.\n", ((double)duration)/1000.0);
}


uint64_t TimeStamp()
{
	return (uint64_t)GetTickCount();
}

uint64_t ElapsedTime(uint64_t since)
{
	uint64_t now = TimeStamp();
	if (now < since)
	{
		//Assume the timer has wrapped around:
		//this will fail if the operation takes more than 49.7 days
		now += 0xFFFFFFFF;
	}
	return now - since;
}
