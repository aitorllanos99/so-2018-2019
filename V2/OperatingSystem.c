#include "OperatingSystem.h"
#include "OperatingSystemBase.h"
#include "MMU.h"
#include "Processor.h"
#include "Buses.h"
#include "Heap.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>
int numberOfClockInterrupts= 0;
// Functions prototypes
void OperatingSystem_PrepareDaemons();
void OperatingSystem_PCBInitialization(int, int, int, int, int);
void OperatingSystem_MoveToTheREADYState(int);
void OperatingSystem_Dispatch(int);
void OperatingSystem_RestoreContext(int);
void OperatingSystem_SaveContext(int);
void OperatingSystem_TerminateProcess();
int OperatingSystem_LongTermScheduler();
void OperatingSystem_PreemptRunningProcess();
int OperatingSystem_CreateProcess(int);
int OperatingSystem_ObtainMainMemory(int, int);
int OperatingSystem_ShortTermScheduler();
int OperatingSystem_ExtractFromReadyToRun(int);
void OperatingSystem_HandleException();
void OperatingSystem_HandleSystemCall();
void OperatingSystem_PrintReadyToRunQueue();
void OperatingSystem_PrintQueque(int);
void OperatingSystem_ShowTime(char);
void OperatingSystem_HandleClockInterrupt();
void OperatingSystem_MoveToTheBlockedState(int);
int OperatingSystem_ExtractFromBlockedToReady();
	// The process table
	PCB processTable[PROCESSTABLEMAXSIZE];

// Address base for OS code in this version
int OS_address_base = PROCESSTABLEMAXSIZE * MAINMEMORYSECTIONSIZE;

// Identifier of the current executing process
int executingProcessID = NOPROCESS;

// Identifier of the System Idle Process
int sipID;

// Begin indes for daemons in programList
int baseDaemonsInProgramList;

// Array that contains the identifiers of the READY processes
int readyToRunQueue[NUMBEROFQUEUES][PROCESSTABLEMAXSIZE];
int numberOfReadyToRunProcesses[NUMBEROFQUEUES] = {0, 0};
char *queueNames[NUMBEROFQUEUES] = {"USER", "DAEMONS"};
// Variable containing the number of not terminated user processes
int numberOfNotTerminatedUserProcesses = 0;
char *statesNames[5] = {"NEW", "READY", "EXECUTING", "BLOCKED", "EXIT"};
// In OperatingSystem.c Exercise 5-b of V2
// Heap with blocked processes sort by when to wakeup
int sleepingProcessesQueue[PROCESSTABLEMAXSIZE];
int numberOfSleepingProcesses=0;
// Initial set of tasks of the OS
void OperatingSystem_Initialize(int daemonsIndex)
{

	int i, selectedProcess;
	FILE *programFile; // For load Operating System Code

	// Obtain the memory requirements of the program
	int processSize = OperatingSystem_ObtainProgramSize(&programFile, "OperatingSystemCode");

	// Load Operating System Code
	OperatingSystem_LoadProgram(programFile, OS_address_base, processSize);

	// Process table initialization (all entries are free)
	for (i = 0; i < PROCESSTABLEMAXSIZE; i++)
		processTable[i].busy = 0;

	// Initialization of the interrupt vector table of the processor
	Processor_InitializeInterruptVectorTable(OS_address_base + 1);

	// Create all system daemon processes
	OperatingSystem_PrepareDaemons(daemonsIndex);

	// Create all user processes from the information given in the command line
	//Ejercicio 14.
	int procesos = OperatingSystem_LongTermScheduler();

	
	if (procesos <= 1) //El DAEMON siempre lo carga
	{
		OperatingSystem_ReadyToShutdown();
	}
	if (strcmp(programList[processTable[sipID].programListIndex]->executableName, "SystemIdleProcess"))
	{
		// Show message "ERROR: Missing SIP program!\n"
		OperatingSystem_ShowTime( SHUTDOWN);
		ComputerSystem_DebugMessage(21, SHUTDOWN);
		exit(1);
	}

	// At least, one user process has been created
	// Select the first process that is going to use the processor
	selectedProcess = OperatingSystem_ShortTermScheduler();

	// Assign the processor to the selected process
	OperatingSystem_Dispatch(selectedProcess);

	// Initial operation for Operating System
	Processor_SetPC(OS_address_base);
}

// Daemon processes are system processes, that is, they work together with the OS.
// The System Idle Process uses the CPU whenever a user process is able to use it
void OperatingSystem_PrepareDaemons(int programListDaemonsBase)
{

	// Include a entry for SystemIdleProcess at 0 position
	programList[0] = (PROGRAMS_DATA *)malloc(sizeof(PROGRAMS_DATA));

	programList[0]->executableName = "SystemIdleProcess";
	programList[0]->arrivalTime = 0;
	programList[0]->type = DAEMONPROGRAM; // daemon program

	sipID = INITIALPID % PROCESSTABLEMAXSIZE; // first PID for sipID

	// Prepare aditionals daemons here
	// index for aditionals daemons program in programList
	baseDaemonsInProgramList = programListDaemonsBase;
}

// The LTS is responsible of the admission of new processes in the system.
// Initially, it creates a process from each program specified in the
// 			command lineand daemons programs
int OperatingSystem_LongTermScheduler()
{

	int PID, i,
		numberOfSuccessfullyCreatedProcesses = 0;

	for (i = 0; programList[i] != NULL && i < PROGRAMSMAXNUMBER; i++)
	{
		PID = OperatingSystem_CreateProcess(i);
		switch (PID)
		{

		case NOFREEENTRY:
		OperatingSystem_ShowTime( ERROR);
			ComputerSystem_DebugMessage(103, ERROR, programList[i]->executableName);
			break;
		case PROGRAMNOTVALID:
		OperatingSystem_ShowTime( ERROR);
			ComputerSystem_DebugMessage(104, ERROR, programList[i]->executableName, "invalid priority or size"); //El string es para el mensaje
			break;

		case PROGRAMDOESNOTEXIST:
		OperatingSystem_ShowTime( ERROR);
			ComputerSystem_DebugMessage(104, ERROR, programList[i]->executableName, "it does not exist"); //El string es para el mensaje
			break;
		case TOOBIGPROCESS:
			OperatingSystem_ShowTime( ERROR);
			ComputerSystem_DebugMessage(105, ERROR, programList[i]->executableName);
			break;
		default:
			numberOfSuccessfullyCreatedProcesses++;
			if (programList[i]->type == USERPROGRAM)
				numberOfNotTerminatedUserProcesses++;
			// Move process to the ready state
			OperatingSystem_MoveToTheREADYState(PID);
			break;
		}
		
	}
		if (numberOfSuccessfullyCreatedProcesses > 0)
		OperatingSystem_PrintStatus();
	// Return the number of succesfully created processes
	return numberOfSuccessfullyCreatedProcesses;
}

// This function creates a process from an executable program
int OperatingSystem_CreateProcess(int indexOfExecutableProgram)
{

	int PID;
	int processSize;
	int loadingPhysicalAddress;
	int priority;
	FILE *programFile;
	PROGRAMS_DATA *executableProgram = programList[indexOfExecutableProgram];

	// Obtain a process ID
	PID = OperatingSystem_ObtainAnEntryInTheProcessTable();

	if (PID == NOFREEENTRY)
	{
		return PID;
	}

	// Obtain the memory requirements of the program
	processSize = OperatingSystem_ObtainProgramSize(&programFile, executableProgram->executableName);

	if (processSize < 0)
	{
		return processSize;
	} //PROGRAMDOESNOTEXIST == -1 || PROGRAMNOTVALID == -2

	// Obtain the priority for the process
	priority = OperatingSystem_ObtainPriority(programFile);

	if (priority < 0)
	{
		return priority;
	} //PROGRAMNOTVALID == -2

	// Obtain enough memory space
	loadingPhysicalAddress = OperatingSystem_ObtainMainMemory(processSize, PID);

	if (loadingPhysicalAddress < 0)
	{
		return loadingPhysicalAddress;
	} //TOOBIGPROCESS == -4
	int direccionMem = OperatingSystem_LoadProgram(programFile, loadingPhysicalAddress, processSize);
	if (direccionMem < 0)
	{
		return direccionMem;
	} //TOOBIGPROCESS == -4

	// Load program in the allocated memory
	OperatingSystem_LoadProgram(programFile, loadingPhysicalAddress, processSize);

	// PCB initialization
	OperatingSystem_PCBInitialization(PID, loadingPhysicalAddress, processSize, priority, indexOfExecutableProgram);

	// Show message "Process [PID] created from program [executableName]\n"
	 
	OperatingSystem_ShowTime( SYSPROC);
	ComputerSystem_DebugMessage(111, SYSPROC, PID, executableProgram->executableName, statesNames[0]);
	OperatingSystem_ShowTime( INIT);
	ComputerSystem_DebugMessage(22, INIT, PID, executableProgram->executableName);


	return PID;
}

// Main memory is assigned in chunks. All chunks are the same size. A process
// always obtains the chunk whose position in memory is equal to the processor identifier
int OperatingSystem_ObtainMainMemory(int processSize, int PID)
{

	if (processSize > MAINMEMORYSECTIONSIZE)
		return TOOBIGPROCESS;

	return PID * MAINMEMORYSECTIONSIZE;
}

// Assign initial values to all fields inside the PCB
void OperatingSystem_PCBInitialization(int PID, int initialPhysicalAddress, int processSize, int priority, int processPLIndex)
{

	processTable[PID].busy = 1;
	processTable[PID].initialPhysicalAddress = initialPhysicalAddress;
	processTable[PID].processSize = processSize;
	processTable[PID].state = NEW;
	processTable[PID].priority = priority;
	processTable[PID].programListIndex = processPLIndex;
	processTable[PID].nombre = programList[processPLIndex]->executableName;
	// Daemons run in protected mode and MMU use real address
	if (programList[processPLIndex]->type == DAEMONPROGRAM)
	{
		processTable[PID].copyOfPCRegister = initialPhysicalAddress;
		processTable[PID].copyOfPSWRegister = ((unsigned int)1) << EXECUTION_MODE_BIT;
		processTable[PID].queueID = DAEMONSQUEUE;
	}
	else
	{
		processTable[PID].copyOfPCRegister = 0;
		processTable[PID].copyOfPSWRegister = 0;
		processTable[PID].queueID = USERPROCESSQUEUE;
	}
	processTable[PID].copyOfAccumulator = 0;
}

// Move a process to the READY state: it will be inserted, depending on its priority, in
// a queue of identifiers of READY processes
void OperatingSystem_MoveToTheREADYState(int PID)
{

	if (Heap_add(PID, readyToRunQueue[processTable[PID].queueID], QUEUE_PRIORITY, &numberOfReadyToRunProcesses[processTable[PID].queueID], PROCESSTABLEMAXSIZE) >= 0)
	{
		 OperatingSystem_ShowTime( SYSPROC);
		ComputerSystem_DebugMessage(110, SYSPROC, PID, processTable[PID].nombre, statesNames[processTable[PID].state], statesNames[1]);

		processTable[PID].state = READY;
		OperatingSystem_PrintReadyToRunQueue();
	}
}

// The STS is responsible of deciding which process to execute when specific events occur.
// It uses processes priorities to make the decission. Given that the READY queue is ordered
// depending on processes priority, the STS just selects the process in front of the READY queue
int OperatingSystem_ShortTermScheduler()
{

	int selectedProcess, queueID;
	if (numberOfReadyToRunProcesses[USERPROCESSQUEUE] > 0)
	{
		queueID = USERPROCESSQUEUE;
	}
	else
	{
		queueID = DAEMONSQUEUE;
	}

	//int i;
	//int selectedProcess = NOPROCESS;
	//for(i=0; i<NUMBEROFQUEQUES && selectedPreocess == NOPROCESS; i++){
		//selectedProcess = OperatingSystem_ExtractFromReadyToRun(i);
	//}

	selectedProcess = OperatingSystem_ExtractFromReadyToRun(queueID);

	return selectedProcess;
}

// Return PID of more priority process in the READY queue
int OperatingSystem_ExtractFromReadyToRun(int queueID)
{

	int selectedProcess = NOPROCESS;

	selectedProcess = Heap_poll(readyToRunQueue[queueID], QUEUE_PRIORITY, &numberOfReadyToRunProcesses[queueID]);

	// Return most priority process or NOPROCESS if empty queue
	return selectedProcess;
}

// Function that assigns the processor to a process
void OperatingSystem_Dispatch(int PID)
{

	// The process identified by PID becomes the current executing process
	executingProcessID = PID;
	// Change the process' state
	 OperatingSystem_ShowTime( SYSPROC);
	ComputerSystem_DebugMessage(110, SYSPROC, PID, processTable[PID].nombre, statesNames[processTable[PID].state], statesNames[2]);

	processTable[PID].state = EXECUTING;
	// Modify hardware registers with appropriate values for the process identified by PID
	OperatingSystem_RestoreContext(PID);
}

// Modify hardware registers with appropriate values for the process identified by PID
void OperatingSystem_RestoreContext(int PID)
{

	Processor_SetAccumulator(processTable[PID].copyOfAccumulator);
	// New values for the CPU registers are obtained from the PCB
	Processor_CopyInSystemStack(MAINMEMORYSIZE - 1, processTable[PID].copyOfPCRegister);
	Processor_CopyInSystemStack(MAINMEMORYSIZE - 2, processTable[PID].copyOfPSWRegister);

	// Same thing for the MMU registers
	MMU_SetBase(processTable[PID].initialPhysicalAddress);
	MMU_SetLimit(processTable[PID].processSize);
}

// Function invoked when the executing process leaves the CPU
void OperatingSystem_PreemptRunningProcess()
{

	// Save in the process' PCB essential values stored in hardware registers and the system stack
	OperatingSystem_SaveContext(executingProcessID);
	// Change the process' state
	OperatingSystem_MoveToTheREADYState(executingProcessID);
	// The processor is not assigned until the OS selects another process
	executingProcessID = NOPROCESS;
}

// Save in the process' PCB essential values stored in hardware registers and the system stack
void OperatingSystem_SaveContext(int PID)
{
	processTable[PID].copyOfAccumulator = Processor_GetAccumulator();
	// Load PC saved for interrupt manager
	processTable[PID].copyOfPCRegister = Processor_CopyFromSystemStack(MAINMEMORYSIZE - 1);

	// Load PSW saved for interrupt manager
	processTable[PID].copyOfPSWRegister = Processor_CopyFromSystemStack(MAINMEMORYSIZE - 2);
}

// Exception management routine
void OperatingSystem_HandleException()
{

	// Show message "Process [executingProcessID] has generated an exception and is terminating\n"
	 OperatingSystem_ShowTime( SYSPROC);
	ComputerSystem_DebugMessage(23, SYSPROC, executingProcessID, programList[processTable[executingProcessID].programListIndex]->executableName);

	OperatingSystem_TerminateProcess();
	OperatingSystem_PrintStatus();
}

// All tasks regarding the removal of the process
void OperatingSystem_TerminateProcess()
{

	int selectedProcess;

	 OperatingSystem_ShowTime( SYSPROC);
	ComputerSystem_DebugMessage(110, SYSPROC, executingProcessID, processTable[executingProcessID].nombre, statesNames[processTable[executingProcessID].state], statesNames[4]);

	processTable[executingProcessID].state = EXIT;

	if (programList[processTable[executingProcessID].programListIndex]->type == USERPROGRAM)
		// One more user process that has terminated
		numberOfNotTerminatedUserProcesses--;

	if (numberOfNotTerminatedUserProcesses <= 0)
	{
		// Simulation must finish
		OperatingSystem_ReadyToShutdown();
	}
	// Select the next process to execute (sipID if no more user processes)
	selectedProcess = OperatingSystem_ShortTermScheduler();
	// Assign the processor to that process
	OperatingSystem_Dispatch(selectedProcess);
}

// System call management routine
void OperatingSystem_HandleSystemCall()
{
	int systemCallID;

	// Register A contains the identifier of the issued system call
	systemCallID = Processor_GetRegisterA();

	
	switch (systemCallID)
	{
	case SYSCALL_PRINTEXECPID:
		// Show message: "Process [executingProcessID] has the processor assigned\n"
		 OperatingSystem_ShowTime( SYSPROC);
		ComputerSystem_DebugMessage(24, SYSPROC, executingProcessID, programList[processTable[executingProcessID].programListIndex]->executableName);
		break;

	case SYSCALL_END:
		// Show message: "Process [executingProcessID] has requested to terminate\n"
		 OperatingSystem_ShowTime( SYSPROC);
		ComputerSystem_DebugMessage(25, SYSPROC, executingProcessID, programList[processTable[executingProcessID].programListIndex]->executableName);
		OperatingSystem_TerminateProcess();
		OperatingSystem_PrintStatus();
		break;
	case SYSCALL_YIELD:
		// Compruebo que hay más procesos en la cola
		if (numberOfReadyToRunProcesses[processTable[executingProcessID].queueID] > 0)
		{
			// Selecciona el primero de la cola, él más prioritatio
			int prioritario = readyToRunQueue[processTable[executingProcessID].queueID][0];
			// Compruebo que tiene la misma prioridad que el proceso que esta ejecutando
			if (processTable[executingProcessID].priority == processTable[prioritario].priority)
			{
				// Imprimo mensaje
				 OperatingSystem_ShowTime( SHORTTERMSCHEDULE);
				ComputerSystem_DebugMessage(115, SHORTTERMSCHEDULE, executingProcessID, processTable[executingProcessID].nombre,
											prioritario, processTable[prioritario].nombre);
				// Preparo el primer proceso de la cola
				prioritario = OperatingSystem_ExtractFromReadyToRun(processTable[executingProcessID].queueID);
				// Saco el proceso que se ejecutando en el sistema operativo
				OperatingSystem_PreemptRunningProcess();
				// Empiezo la ejecucion del nuevo proceso
				OperatingSystem_Dispatch(prioritario);
				OperatingSystem_PrintStatus();
			}
		}
		break;
	case SYSCALL_SLEEP:
		// Guardas el estado del proceso
		OperatingSystem_SaveContext(executingProcessID);
		// Cambias al estado Bloquedo y lo mueves a la cola de processos dormidos
		OperatingSystem_MoveToTheBlockedState(executingProcessID);
		// Deasignas el proceso actual
		executingProcessID = NOPROCESS;
		// Lanzas otro proceso
		OperatingSystem_Dispatch(OperatingSystem_ShortTermScheduler());
		// V2 Ejercicio 5e
		OperatingSystem_PrintStatus();
		break;
	}
}

//	Implement interrupt logic calling appropriate interrupt handle
void OperatingSystem_InterruptLogic(int entryPoint)
{
	switch (entryPoint)
	{
	case SYSCALL_BIT: // SYSCALL_BIT=2
		OperatingSystem_HandleSystemCall();
		break;
	case EXCEPTION_BIT: // EXCEPTION_BIT=6
		OperatingSystem_HandleException();
		break;
	case CLOCKINT_BIT:
		OperatingSystem_HandleClockInterrupt();
		break;
	}
}

void OperatingSystem_PrintReadyToRunQueue()
{
	 OperatingSystem_ShowTime( SHORTTERMSCHEDULE);
	ComputerSystem_DebugMessage(106, SHORTTERMSCHEDULE);
	OperatingSystem_PrintQueque(USERPROCESSQUEUE);
	OperatingSystem_PrintQueque(DAEMONSQUEUE);
}

void OperatingSystem_PrintQueque(int queue)
{
	int i;
	for (i = 0; i < numberOfReadyToRunProcesses[queue]; i++)
	{
		if (i != 0)
		{
			if (i == numberOfReadyToRunProcesses[queue] - 1)
			{
				ComputerSystem_DebugMessage(107, SHORTTERMSCHEDULE, "","", readyToRunQueue[queue][i], processTable[readyToRunQueue[queue][i]].priority, "\n");
			}
			else
			{
				ComputerSystem_DebugMessage(107, SHORTTERMSCHEDULE, "","", readyToRunQueue[queue][i], processTable[readyToRunQueue[queue][i]].priority, ",");
			}
		}
		else
		{
			if (numberOfReadyToRunProcesses[queue] == 1)
			{
				
				ComputerSystem_DebugMessage(107, SHORTTERMSCHEDULE, queueNames[queue], ":", readyToRunQueue[queue][i], processTable[readyToRunQueue[queue][i]].priority, "\n");
			}
			else
			{
				ComputerSystem_DebugMessage(107, SHORTTERMSCHEDULE,queueNames[queue], ":", readyToRunQueue[queue][i], processTable[readyToRunQueue[queue][i]].priority, ",");
			}
		}
	}
}

// In OperatingSystem.c Exercise 2-b of V2
void OperatingSystem_HandleClockInterrupt(){ 
	int numWakeUpProcess = 0;
	int copyOfExecutingProcessID, selectedProcess;
	numberOfClockInterrupts++;
	OperatingSystem_ShowTime(INTERRUPT);
	ComputerSystem_DebugMessage(120, ERROR, numberOfClockInterrupts); 
	while(numberOfSleepingProcesses >0 && processTable[sleepingProcessesQueue[0]].whenToWakeUp == numberOfClockInterrupts)
	{
		selectedProcess = OperatingSystem_ExtractFromBlockedToReady();
		OperatingSystem_MoveToTheREADYState(selectedProcess);
		numWakeUpProcess++;
	}
	if(numWakeUpProcess > 0){
		OperatingSystem_PrintStatus();
		//COMPROBAR PRIORIDADES
		if(processTable[executingProcessID].queueID == DAEMONSQUEUE){
			if(numberOfReadyToRunProcesses[USERPROCESSQUEUE] > 0){
				copyOfExecutingProcessID = executingProcessID;
				selectedProcess = OperatingSystem_ShortTermScheduler();
				OperatingSystem_ShowTime(SHORTTERMSCHEDULE);
				ComputerSystem_DebugMessage(121,SHORTTERMSCHEDULE,copyOfExecutingProcessID,processTable[copyOfExecutingProcessID].nombre, selectedProcess,processTable[copyOfExecutingProcessID].nombre);
				OperatingSystem_PreemptRunningProcess();
				OperatingSystem_Dispatch(selectedProcess);
				OperatingSystem_PrintStatus();
			}
			else if(numberOfReadyToRunProcesses[DAEMONSQUEUE] > 0 && processTable[readyToRunQueue[DAEMONSQUEUE][0]].priority < processTable[executingProcessID].priority){
				copyOfExecutingProcessID = executingProcessID;
				selectedProcess = OperatingSystem_ShortTermScheduler();
				OperatingSystem_ShowTime(SHORTTERMSCHEDULE);
				ComputerSystem_DebugMessage(121,SHORTTERMSCHEDULE,copyOfExecutingProcessID,processTable[copyOfExecutingProcessID].nombre, selectedProcess,processTable[copyOfExecutingProcessID].nombre);
				OperatingSystem_PreemptRunningProcess();
				OperatingSystem_Dispatch(selectedProcess);
				OperatingSystem_PrintStatus();
			}
				
		}else if(processTable[executingProcessID].queueID == USERPROCESSQUEUE){
			if(numberOfReadyToRunProcesses[USERPROCESSQUEUE] > 0 && processTable[readyToRunQueue[USERPROCESSQUEUE][0]].priority < processTable[executingProcessID].priority){
				copyOfExecutingProcessID = executingProcessID;
				selectedProcess = OperatingSystem_ShortTermScheduler();
				OperatingSystem_ShowTime(SHORTTERMSCHEDULE);
				ComputerSystem_DebugMessage(121,SHORTTERMSCHEDULE,copyOfExecutingProcessID,processTable[copyOfExecutingProcessID].nombre, selectedProcess,processTable[copyOfExecutingProcessID].nombre);
				OperatingSystem_PreemptRunningProcess();
				OperatingSystem_Dispatch(selectedProcess);
				OperatingSystem_PrintStatus();
			}
		}		
	} 

	   return; } 

void OperatingSystem_MoveToTheBlockedState(int PID)
{
	int whenToWakeUp;
	whenToWakeUp = 	abs(Processor_GetAccumulator()) + numberOfClockInterrupts + 1 ;
	processTable[PID].whenToWakeUp = whenToWakeUp;
	if (Heap_add(PID, sleepingProcessesQueue, QUEUE_WAKEUP, &numberOfSleepingProcesses, PROCESSTABLEMAXSIZE) >= 0)
	{
		OperatingSystem_ShowTime( SYSPROC);
		ComputerSystem_DebugMessage(110, SYSPROC, PID, processTable[PID].nombre, statesNames[processTable[PID].state], statesNames[1]);

		processTable[PID].state = BLOCKED;
		OperatingSystem_PrintStatus();
	}
}
int OperatingSystem_ExtractFromBlockedToReady()
{
	return Heap_poll(sleepingProcessesQueue, QUEUE_WAKEUP,
					 &numberOfSleepingProcesses);
}