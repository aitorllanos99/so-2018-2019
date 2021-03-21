#include <stdio.h>
#include <stdlib.h>
#include "ComputerSystem.h"
#include "OperatingSystem.h"
#include "ComputerSystemBase.h"
#include "Processor.h"
#include "Messages.h"
#include "Asserts.h"
#include "Clock.h"

// Functions prototypes

// Array that contains basic data about all daemons
// and all user programs specified in the command line
PROGRAMS_DATA *programList[PROGRAMSMAXNUMBER];
int arrivalTimeQueue[PROGRAMSMAXNUMBER];
int numberOfProgramsInArrivalTimeQueue=0;
void ComputerSystem_PrintProgramList();
void ComputerSystem_ShowTime(char);
// Powers on of the Computer System.
void ComputerSystem_PowerOn(int argc, char *argv[]) {

	// Load debug messages
	int nm=0;
	nm=Messages_Load_Messages(nm,TEACHER_MESSAGES_FILE);
	nm=Messages_Load_Messages(nm,STUDENT_MESSAGES_FILE);
	printf("%d Messages Loaded\n",nm);

	// Obtain a list of programs in the command line and debus sections
	int daemonsBaseIndex = ComputerSystem_ObtainProgramList(argc, argv);

	int na=Asserts_LoadAsserts();
	if (na==-1)
		// printf("Asserts file unavailable\n");
		ComputerSystem_DebugMessage(84,POWERON);
	else
		// printf("%d Asserts Loaded\n",na);
		ComputerSystem_DebugMessage(85,POWERON,na);

	ComputerSystem_PrintProgramList();
	// Request the OS to do the initial set of tasks. The last one will be
	// the processor allocation to the process with the highest priority
	OperatingSystem_Initialize(daemonsBaseIndex);
	
	// Tell the processor to begin its instruction cycle 
	Processor_InstructionCycleLoop();
	
}

// Powers off the CS (the C program ends)
void ComputerSystem_PowerOff() {
	ComputerSystem_ShowTime(SHUTDOWN);
	// Show message in red colour: "END of the simulation\n" 
	ComputerSystem_DebugMessage(99,SHUTDOWN); 
	exit(0);
}

/////////////////////////////////////////////////////////
//  New functions below this line  //////////////////////
void ComputerSystem_PrintProgramList(){
	ComputerSystem_ShowTime(INIT);
	ComputerSystem_DebugMessage(101,INIT); 
	int i;
	for(i = 0; i<PROGRAMSMAXNUMBER;i++){
		if(programList[i]!=NULL){
			ComputerSystem_DebugMessage(102,INIT,
				programList[i]->executableName,  //cogemos su nombre
				programList[i]->arrivalTime);  //cogemos el ciclo de reloj
		}
	}
}
// Show time messages
void ComputerSystem_ShowTime(char section) {
	ComputerSystem_DebugMessage(Processor_PSW_BitState(EXECUTION_MODE_BIT)?5:4,section,Clock_GetTime());
}
