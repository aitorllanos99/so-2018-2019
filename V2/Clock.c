#include "Clock.h"
#include "Processor.h"

int tics=0;

void Clock_Update() {

	tics++;
	if(tics%INTERVALBETWEENINTERRUPS ==0){
		Processor_RaiseInterrupt(CLOCKINT_BIT);
	}
}


int Clock_GetTime() {

	return tics;
}
