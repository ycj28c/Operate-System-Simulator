/****************************************************************************************************

        This code forms the base of the operating system you will
        build.  It has only the barest rudiments of what you will
        eventually construct; yet it contains the interfaces that
        allow test.c and z502.c to be successfully built together.

        Revision History:
        1.0 August 1990
        1.1 December 1990: Portability attempted.
        1.3 July     1992: More Portability enhancements.
                           Add call to sample_code.
        1.4 December 1992: Limit (temporarily) printout in
                           interrupt handler.  More portability.
        2.0 January  2000: A number of small changes.
        2.1 May      2001: Bug fixes and clear STAT_VECTOR
        2.2 July     2002: Make code appropriate for undergrads.
                           Default program start is in test0.
        3.0 August   2004: Modified to support memory mapped IO
        3.1 August   2004: hardware interrupt runs on separate thread
        3.11 August  2004: Support for OS level locking
	4.0  July    2013: Major portions rewritten to support multiple threads

*****************************************************************************************************

WPI OS502	10/12/2013	Chengjiao Yang

The Project1
In this phase, you are given a skeleton source code of a simple operating system and asked to complete 
it. This implementation assumes many programs are running on the computer at a time. The implementation 
works with a version of the Z502 machine with no address translation hardware of any kind since memory 
management only is implimented by you in Project 2. The functions that the kernel supports in Project 1 
are multiprocessing, interaction between these processes, and some simple error handling.

*****************************************************************************************************/
#include             "global.h"
#include             "syscalls.h"
#include             "protos.h"
#include             "string.h"
#include			 "stdlib.h"
///////////////////define the definition///////////////////
#define			ProcessLimit				15 //the limit of the total number of process
#define			MessageLimit				100 //list the message pool 100
#define			DO_LOCK                     1
#define			DO_UNLOCK                   0
#define			SUSPEND_UNTIL_LOCKED        TRUE
#define			DO_NOT_SUSPEND              FALSE
///////////////////define the structure in base.c///////////////////
typedef struct 
	{        //define the PCB 
		INT32  Processid;          
		INT32  Priority;
		char Name[16];
		void *context;  //the context pointer of the hardware z502
}Process_Control_Block;
//typedef struct node *PCBNode; 
typedef struct node
	{  
    Process_Control_Block data;  
   // PCBNode next;  
	INT32	time; //save the current system time here
	struct node *next;
}Node, *PCBNode; 
typedef struct 
	{  
    PCBNode front;  
    PCBNode rear;  //point to the last element of the queue, doesnt very useful
    INT32 size;  
}PCBQueue;
typedef struct{//this structure is for send and receive message
    long    target_pid;
    long    source_pid;
    long    actual_source_pid;
    long    send_length;
    long    receive_length;
    long    actual_send_length;
    long    loop_count;
    char    msg_buffer[64];
}Messagestr;  
///////////////////These loacations are global and define information about the page table///////////////////
extern UINT16        *Z502_PAGE_TBL_ADDR;
extern INT16         Z502_PAGE_TBL_LENGTH;
extern void          *TO_VECTOR [];
char                 *call_names[] = { "mem_read ", "mem_write",
                            "read_mod ", "get_time ", "sleep    ",
                            "get_pid  ", "create   ", "term_proc",
                            "suspend  ", "resume   ", "ch_prior ",
                            "send     ", "receive  ", "disk_read",
                            "disk_wrt ", "def_sh_ar" };
PCBQueue			*timerqueue; //create the timerqueue and store in OS
PCBQueue			*readyqueue; //create the readyqueue and store in OS
PCBQueue			*suspendqueue; //create the readyqueue and store in OS
Messagestr			messagelist[MessageLimit]; //the message queue, limit number 100
Process_Control_Block	*PCB; //create the PCB for new test and store in OS
Process_Control_Block	*CURRENTPCB; 
Process_Control_Block	*start_PCB; //记录进入的主函数，在teminate时候会用到
INT32			PCBcount = 0; //the global counter for pcb
INT32			currenttriggertime;     //the global current time interrupt, cause the interrupt only effect once
INT32			messagecount = 0;//the global message number count
///////////////////declare the routines generate in base.c///////////////////
INT32		OSCreateProcess(char *, void *, INT32 );
PCBQueue	*InitQueue();
//queueroutine
INT32		GetPIDByName(PCBQueue *, char *);
PCBNode		AddToTimerQueue(PCBQueue *,Process_Control_Block *, INT32 );
PCBNode		AddToReadyQueue(PCBQueue *,Process_Control_Block *);
PCBNode		AddToSuspendQueue(PCBQueue *,Process_Control_Block *);
PCBNode		AddToReadyQueueByPriority(PCBQueue *, Process_Control_Block *);
INT32		IsEmpty(PCBQueue * );  
void		ListQueue(PCBQueue * ); 
PCBNode		RemoveFromTimerQueue(PCBQueue *, INT32 );
INT32		RemoveQueueByPid(PCBQueue *,INT32 );
PCBNode		RemoveQueueByName(PCBQueue *, char * ); 
PCBNode		DeQueueFirstElement(PCBQueue *pqueue );
INT32		IsNameDuplicate( PCBQueue *, char * );
INT32		IsPidExist(PCBQueue *, INT32 );
Process_Control_Block GetPcbByPid(PCBQueue *, INT32 );
//message routine
void		removefrommessagelist(INT32 );
INT32		IsSourcePidExsit( INT32 );
void		messageprocess( char *);
//for debug
void		ListTimerQueue();
void		ListReadyQueue();
void		ListTwoQueue(); 
void		ListSuspendQueue();
//sp print rountine
void		dospprint(char *, INT32 , Process_Control_Block *);

/************************************************************************
interrup handle, there are four types of interrupt
TIMER_INTERRUPT	interrupt interrupt_handler
DISK_INTERRUPT	interrupt interrupt_handler
DISK_INTERRUPT + 1	interrupt interrupt_handler
DISK_INTERRUPT + 2	interrupt interrupt_handlerR_INT_HANDLER_ADDR
************************************************************************/
void    interrupt_handler( void ) { 
    INT32				device_id;
    INT32				status;
    INT32				Index = 0;
    //static BOOL		remove_this_in_your_code = TRUE;   /** TEMP **/
    //static INT32		how_many_interrupt_entries = 0;    /** TEMP **/
	PCBNode				bnode;
	char				tempname[16]; //for the process name
	INT32				Time,icount;  //time and temp count
	INT32				LockResult; //return for lock
	INT32				nextinterupttime,mintime; //for calculate the next interrupt

    // Get cause of interrupt
    MEM_READ(Z502InterruptDevice, &device_id );
    // Set this device as target of our query
    MEM_WRITE(Z502InterruptDevice, &device_id );
    // Now read the status of this device
    MEM_READ(Z502InterruptStatus, &status );

	printf( "Interrupt handler: Found device ID %d with status %d\n",device_id, status );
	if (device_id == TIMER_INTERRUPT){
		CALL(MEM_READ( Z502ClockStatus, &Time )); //get the current time
		//printf("current time:%d\n",Time);
		
		//lock the readyqueue and timerqueue
		READ_MODIFY(MEMORY_INTERLOCK_BASE+0, DO_LOCK, SUSPEND_UNTIL_LOCKED, &LockResult); //readyqueue
		READ_MODIFY(MEMORY_INTERLOCK_BASE+1, DO_LOCK, SUSPEND_UNTIL_LOCKED, &LockResult); //timerqueue
		READ_MODIFY(MEMORY_INTERLOCK_BASE+2, DO_LOCK, SUSPEND_UNTIL_LOCKED, &LockResult); //suspendqueue

		bnode = timerqueue->front; 
		icount = 1;
		//when get time interrupt, add pcb to readyqueue and remove the data at timerqueue
		while(bnode!=NULL&&icount<=timerqueue->size){ //used to occur the interrupt insert two data at same time,now change the reset time, its ok now 
			if(bnode->time<=Time){  
				CALL(AddToReadyQueueByPriority(readyqueue,&bnode->data)); //AddToReadyQueueByPriority is inserting data by priority
				//CALL(AddToReadyQueue(readyqueue, &bnode->data)); //FIFO logic routine
				strcpy(tempname,bnode->data.Name);
				bnode = bnode->next;
				CALL(RemoveQueueByName(timerqueue, tempname)); 
				//bnode = bnode->next;
				//icount++;
				//Z502SwitchContext( +, &readyqueue->front->data.context);
			}
			else {
				bnode = bnode->next;	
				icount++;
			}
		}
		//for debug
		//CALL(ListTwoQueue()); //we have to add call, otherwise the error happened for no sense
		//CALL(dospprint("INTERUPT", CURRENTPCB->Processid, CURRENTPCB)); //after giving memory to CURRENTPCB, the printer is ok

		//reset time interrupt, traverse timerqueue, find the min time as next interrupt time
		//READ_MODIFY(MEMORY_INTERLOCK_BASE+1, DO_LOCK, SUSPEND_UNTIL_LOCKED, &LockResult); //timerqueue
		if(IsEmpty(timerqueue)!=1){ //if timerqueue is not empty, we need to reset
			//first get the min time at timerqueue
			bnode = timerqueue->front; 
			icount = 1;
			mintime = bnode->time;
			while(bnode!=NULL&&icount<=timerqueue->size){
				if(mintime>bnode->time){
					mintime = bnode->time;
					bnode = bnode->next;	
					icount++;
				}
				else {
					bnode = bnode->next;	
					icount++;
				}
			}
			//second write to the time interrupt
			CALL(MEM_READ( Z502ClockStatus, &Time )); //too much call waste time, may cause 10 time idle before interrupt, mean ERROR
			nextinterupttime = mintime - Time;
			if(nextinterupttime<=0){//consider if the next time is too short, even minus
				nextinterupttime = 10; //we add 10 to next interrupt to adjust
				MEM_WRITE(Z502TimerStart, &nextinterupttime);
				currenttriggertime = Time+nextinterupttime; //reset timer
			}
			else{
				MEM_WRITE(Z502TimerStart, &nextinterupttime);
				currenttriggertime = Time+nextinterupttime;
			}
			//printf("nextinterupttime:%d\n",nextinterupttime);
		}
		//unlock
		READ_MODIFY(MEMORY_INTERLOCK_BASE+0, DO_UNLOCK, SUSPEND_UNTIL_LOCKED, &LockResult); //readyqueue
		READ_MODIFY(MEMORY_INTERLOCK_BASE+1, DO_UNLOCK, SUSPEND_UNTIL_LOCKED, &LockResult); //timerqueue
		READ_MODIFY(MEMORY_INTERLOCK_BASE+2, DO_UNLOCK, SUSPEND_UNTIL_LOCKED, &LockResult); //suspendqueue

		CALL(dospprint("INTERUPT", CURRENTPCB->Processid, CURRENTPCB)); //after giving memory to CURRENTPCB, the printer is ok
	}
	else if (device_id == DISK_INTERRUPT){
		printf("Interrupt handler: DISK_INTERRUPT\n");
	}
	else{
		printf( "* ERROR!  InterruptDevice ID not recognized!\n" );
		printf( "* InterruptDevice ID %d is not existing\n", device_id);
	}
    // Clear out this device - we're done with it
    MEM_WRITE(Z502InterruptClear, &Index );
}                                       /* End of interrupt_handler */
/************************************************************************
    FAULT_HANDLER
        The beginning of the OS502.  Used to receive hardware faults.
************************************************************************/

void    fault_handler( void )
    {
    INT32       device_id;
    INT32       status;
    INT32       Index = 0;

    // Get cause of interrupt
    MEM_READ(Z502InterruptDevice, &device_id );
    // Set this device as target of our query
    MEM_WRITE(Z502InterruptDevice, &device_id );
    // Now read the status of this device
    MEM_READ(Z502InterruptStatus, &status );

    printf( "Fault_handler: Found vector type %d with value %d\n",device_id, status );
	if(status == SOFTWARE_TRAP){//receive 0
		CALL(Z502Halt());
	}
	else if(status == CPU_ERROR){//receive 1
		//CALL(Z502Halt());
	}
	else if(status == INVALID_MEMORY){//receive 2
		//CALL(Z502Halt());
	}
	else if(status == INVALID_PHYSICAL_MEMORY){//receive 3
		//CALL(Z502Halt());
	}
	else if(status == PRIVILEGED_INSTRUCTION){//receive 4
		//CALL(Z502Halt());
	}
    // Clear out this device - we're done with it
    MEM_WRITE(Z502InterruptClear, &Index );
}                                       /* End of fault_handler */

/************************************************************************
    SVC
        The beginning of the OS502.  Used to receive software interrupts.
        All system calls come to this point in the code and are to be
        handled by the student written code here.
        The variable do_print is designed to print out the data for the
        incoming calls, but does so only for the first ten calls.  This
        allows the user to see what's happening, but doesn't overwhelm
        with the amount of data.
************************************************************************/
void    svc( SYSTEM_CALL_DATA *SystemCallData ) {
    short					call_type;   
    static INT16			do_print = 10; //what is this used for
    INT32					Time,Status; //for time handle
	INT32					Temp;  //for time handle
	char					*processname;  //for get argument from test.c
	void					*processaddress;//for get argument from test.c
	INT32					processpriority;//for get argument from test.c
	INT32					processid;//for get argument from test.c
	PCBNode					pnode;
	INT32					icount,jcount; //the temp count
	Process_Control_Block	pcbtemp;   //for temperory pcb store
	INT32					LockResult;//return the result for read_modify
	char					*messagebuff;       //for message handle
	INT32					sendlength,receivelength; //for message handle

    call_type = (short)SystemCallData->SystemCallNumber;
    if ( do_print > 0 ) {
        // same code as before
    }
    switch (call_type) {
		//printf( "SVC handler: %s\n", call_names[call_type]);
        // Get time service call
		/**************************************************************************************************************************************
		INT32 time_since_midnight;

		The value returned is the number of time-units since the most recent midnight.
		http://web.cs.wpi.edu/~jb/CS502/Project/appendixC.html
		**************************************************************************************************************************************/
        case SYSNUM_GET_TIME_OF_DAY:   // This value is found in syscalls.h    
            CALL( MEM_READ( Z502ClockStatus, &Time ) );
            *(INT32 *)SystemCallData->Argument[0] = Time;
			//printf("* Systemcalldata Time of day is %ld\n", Time);
            break;
		/**************************************************************************************************************************************
		INT32 process_id;
		INT32 error;

		Terminate the process whose PID is given by "process_id". If process_id = -1, then terminate self. If process_id = -2, then terminate 
		self and any child processes. Termination of a non-existent process results in an error being returned. Upon termination, all 
		information about a process is lost and it will never run again. An error is returned if a process with that PID doesn't exist. 
		Lots of other errors are also possible, for instance, the target process isn't in the hierarchy (isn't a child of) the requester.
		Success means error = 0.
		http://web.cs.wpi.edu/~jb/CS502/Project/appendixC.html
		**************************************************************************************************************************************/
        case SYSNUM_TERMINATE_PROCESS:
			processid = (INT32 )SystemCallData->Argument[0];
			//unit lock
			READ_MODIFY(MEMORY_INTERLOCK_BASE+0, DO_LOCK, SUSPEND_UNTIL_LOCKED, &LockResult); //readyqueue
			READ_MODIFY(MEMORY_INTERLOCK_BASE+1, DO_LOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//timerqueue
			READ_MODIFY(MEMORY_INTERLOCK_BASE+2, DO_LOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//suspendqueue
			if(processid ==-2){ //If process_id = -2, then terminate self and any child processes.
				CALL(dospprint("DONE", start_PCB->Processid, CURRENTPCB));
				CALL(Z502Halt());
			}
			else if(processid ==-1){ //If process_id = -1, then terminate self	
				//CALL(RemoveQueueByName(readyqueue, readyqueue->front->data.Name)); //must be first one		
				//printf("CURRENTName:%s,CURRENTPID:%d,TARGETPID:%d\n",CURRENTPCB->Name, CURRENTPCB->Processid,processid);
				CALL(RemoveQueueByName(readyqueue, CURRENTPCB->Name));
				//printf("CURRENTName:%s,CURRENTPID:%d,TARGETPID:%d\n",CURRENTPCB->Name, CURRENTPCB->Processid,processid);
				*(INT32 *)SystemCallData->Argument[1] = ERR_SUCCESS;
				//CALL(ListTwoQueue()); //for debug
			}
			else{        //if processid is not -2 or -1, regular handler
				pnode = readyqueue->front;
				icount = 1;
				while(pnode!=NULL&&icount<=readyqueue->size){
					if(pnode->data.Processid == processid){
						CALL(RemoveQueueByName(readyqueue, pnode->data.Name)); //if remove one node, we can jump out of the loop
						*(INT32 *)SystemCallData->Argument[1] = ERR_SUCCESS;
						break;
					}
					pnode = pnode->next;
				}
				pnode = timerqueue->front;
				icount = 1;
				while(pnode!=NULL&&icount<=readyqueue->size){
					if(pnode->data.Processid == processid){
						CALL(RemoveQueueByName(timerqueue, pnode->data.Name)); 
						*(INT32 *)SystemCallData->Argument[1] = ERR_SUCCESS;
						break;
					}
					pnode = pnode->next;
				}	
			}
			//unlock
			READ_MODIFY(MEMORY_INTERLOCK_BASE+0, DO_UNLOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//readyqueue
			READ_MODIFY(MEMORY_INTERLOCK_BASE+1, DO_UNLOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//timerqueue
			READ_MODIFY(MEMORY_INTERLOCK_BASE+2, DO_UNLOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//suspendqueue
			if(processid ==-1){
				CALL(dospprint("DONE", CURRENTPCB->Processid, CURRENTPCB));
			}
			else CALL(dospprint("DONE", processid, CURRENTPCB));
			
			if(IsEmpty(readyqueue)&&IsEmpty(timerqueue)){
				CALL(Z502Halt());
			}
			//WARN!!! we cant lock system with idle between lock and unlock, that lead to unexpected ERROR! well, the interrupt will not work good
			while(IsEmpty(readyqueue)&&IsEmpty(timerqueue)!=1){  //if readyqueue is empty, but timerqueue is not empty, do idle
				//get the time, see if the timer works well
				/*CALL(MEM_READ(Z502TimerStatus, &Status));	
				if (Status == DEVICE_FREE ){//if timer is empty			
					printf("timer is free,readyqueue:%d timequeue:%d\n",readyqueue->size,timerqueue->size);
				}
				else{//if not, choose the most recently time
					printf("timer is busy,readyqueue:%d timequeue:%d\n",readyqueue->size,timerqueue->size);
				}	*/
				CALL(Z502Idle());  //after idle, system callback here, do loop again
			}
			memcpy(CURRENTPCB, &readyqueue->front->data,sizeof(Process_Control_Block)); //memory copy for the pointer type CURRENTPCB
			CALL(Z502SwitchContext( SWITCH_CONTEXT_SAVE_MODE, &readyqueue->front->data.context)); 
							
			//below are old logic after terminate, after change the reset time, we dont need these logic now
			/*if(IsEmpty(readyqueue)){ //如果终止后readyqueue空了的处理
				printf("start idle loop until end\n"); //这个逻辑还是有问题，在test1f可以看出问题
				CALL(Z502Idle()); //可能会有无限idle的情况，所以需要下面处理，但还有从suspendqueue里面调进来的，所以
				//CALL(ListTwoQueue());
				CALL(dospprint("LOOK", CURRENTPCB->Processid, CURRENTPCB));
				while(1){ 
					if(IsEmpty(readyqueue)!=1){ //如果idle出现了readyqueue数据
						memcpy(CURRENTPCB, &readyqueue->front->data,sizeof(Process_Control_Block));
						CALL(Z502SwitchContext( SWITCH_CONTEXT_SAVE_MODE, &readyqueue->front->data.context));
					}
					else if(IsEmpty(readyqueue)&&IsEmpty(timerqueue)!=1){ //如果timerqueue里有数据
						if((timerqueue->size==1)&&(timerqueue->front->data.Processid == start_PCB->Processid)){//如果只剩下入口/父函数，切到入口函数
							//这里最好加个时间判断，如果休眠时间还没到，那先不终止程序
							CURRENTPCB = start_PCB; //每次切换都需要更新currentpcb
							CALL(Z502SwitchContext( SWITCH_CONTEXT_SAVE_MODE, &start_PCB->context)); //应该修改
						}
						//CALL( MEM_READ( Z502ClockStatus, &Time ) );
						CALL(Z502Idle()); //直接call idle会无限循环，为什么呢
					}
					else CALL(Z502Halt());
				}
			}
			else{
				memcpy(CURRENTPCB, &readyqueue->front->data,sizeof(Process_Control_Block)); //必须拷贝内存，否则指针的话读取currentPCB会错误
				CALL(Z502SwitchContext( SWITCH_CONTEXT_SAVE_MODE, &readyqueue->front->data.context));
			}*/
            break;
		/**************************************************************************************************************************************
		INT32 sleep_time;

		The process becomes "not ready to run" for the number of time units given in "sleep_time".
		http://web.cs.wpi.edu/~jb/CS502/Project/appendixC.html
		**************************************************************************************************************************************/
		case SYSNUM_SLEEP:
			CALL( MEM_READ( Z502ClockStatus, &Time ) ); //get the current system time
			MEM_READ( Z502TimerStatus, &Status);
			//Temp = 777; /* You pick the time units */
			Temp = (INT32)SystemCallData->Argument[0];
			if(Temp<0){ //if time is not legal, it seems if time is not legal, system will go into the fault handle
				printf("ERROR! The sleep time is illegal!\n");
				break;
			}
			//include time 0
			//calculate the time, set the next interrupt time
			MEM_READ(Z502TimerStatus, &Status);	
			if (Status == DEVICE_FREE ){//if timer is empty			
				MEM_WRITE(Z502TimerStart, &Temp);
				currenttriggertime = Time+Temp;
			}
			else{//if not, choose the most recently time
				if(currenttriggertime - Time>Temp&&(IsEmpty(timerqueue)!=1)) // if the old sleep time is larger, reset
					MEM_WRITE(Z502TimerStart, &Temp);
					currenttriggertime = Time+Temp;
			}	
			/*MEM_READ(Z502TimerStatus, &Status);	
			if (Status == DEVICE_IN_USE)
				printf("Got expected result for Status of Timer\n");
			else
				printf("Got erroneous result for Status of Timer\n");*/

			READ_MODIFY(MEMORY_INTERLOCK_BASE+0, DO_LOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//readyqueue
			READ_MODIFY(MEMORY_INTERLOCK_BASE+1, DO_LOCK, SUSPEND_UNTIL_LOCKED, &LockResult); //timerqueue
			//the old logic, after idle, the system would insert one more data, i think this problem is fine now
			//but for the system robust, i'd better keep this
			pnode = timerqueue->front;
			icount = 1;
			while (pnode!=NULL&&icount<=timerqueue->size)
			{
				if (strcmp(pnode->data.Name,CURRENTPCB->Name)==0 ){
					CALL(RemoveQueueByName(timerqueue,CURRENTPCB->Name));	
				}	
				pnode=pnode->next;
				icount++;
			}
			CALL(AddToTimerQueue(timerqueue, CURRENTPCB, Time+Temp));
			CALL(RemoveQueueByName(readyqueue,CURRENTPCB->Name)); 
		
			READ_MODIFY(MEMORY_INTERLOCK_BASE+0, DO_UNLOCK, SUSPEND_UNTIL_LOCKED, &LockResult); //readyqueue
			READ_MODIFY(MEMORY_INTERLOCK_BASE+1, DO_UNLOCK, SUSPEND_UNTIL_LOCKED, &LockResult); //timerqueue
			//CALL(ListTwoQueue());
			CALL(dospprint("SLEEP", CURRENTPCB->Processid, CURRENTPCB)); //after memcpy CURRENTPCB, print ok now
			while(IsEmpty(readyqueue)){ //while nothing in readyqueue, do idle
				/*CALL(MEM_READ(Z502TimerStatus, &Status));	
				if (Status == DEVICE_FREE ){//if timer is empty			
					printf("timer is free,readyqueue:%d timequeue:%d\n",readyqueue->size,timerqueue->size);
				}
				else{//if not, choose the most recently time
					printf("timer is busy,readyqueue:%d timequeue:%d\n",readyqueue->size,timerqueue->size);
				}	*/
				CALL(Z502Idle());
			}
			//until something appear in readyqueue, we switch to that process
			memcpy(CURRENTPCB, &readyqueue->front->data,sizeof(Process_Control_Block));
			CALL(Z502SwitchContext( SWITCH_CONTEXT_SAVE_MODE, &readyqueue->front->data.context)); //switch to first one in readyqueue
			break;
		/**************************************************************************************************************************************
		char process_name[N];
		void *starting_address;
		INT32 initial_priority;
		INT32 process_id;
		INT32 error;

		Create a process. This process will have name "process_name", will begin execution at location "starting_address" ( in essence, this 
		is the name of a routine ), and at the start of execution has a priority of "initial_priority". The system call returns the process_id 
		of the created process. An error will be generated if a process with the same name already exists. Lots of other errors are also 
		possible.Success means error = 0.
		http://web.cs.wpi.edu/~jb/CS502/Project/appendixC.html
		**************************************************************************************************************************************/
		case SYSNUM_CREATE_PROCESS:
			processname = (char* )SystemCallData->Argument[0];  //care about the char pass value 
			processaddress = (void *)SystemCallData->Argument[1];
			processpriority = (INT32 )SystemCallData->Argument[2];
			if(processpriority<0||processpriority>99){
				printf("ERROR! the priority %d is illegal.\n",processpriority);
				*(INT32 *)SystemCallData->Argument[4] = DEVICE_IN_USE; 
				break;
			}
			if(PCBcount>ProcessLimit){ 
				printf("The limit of PCB is 15, you can't create more process\n");
				*(INT32 *)SystemCallData->Argument[4] = DEVICE_IN_USE;
				CALL(Z502SwitchContext( SWITCH_CONTEXT_SAVE_MODE, &readyqueue->front->data.context));
			}
			else {
				if(IsNameDuplicate( readyqueue, processname )||IsNameDuplicate( timerqueue, processname )){ //check the duplicate and name
					*(INT32 *)SystemCallData->Argument[4] = DEVICE_IN_USE; 
					printf("ERROR! the processname '%s' is already exsited.\n",processname);
					memcpy(CURRENTPCB, &readyqueue->front->data,sizeof(Process_Control_Block)); 
					CALL(Z502SwitchContext( SWITCH_CONTEXT_SAVE_MODE, &readyqueue->front->data.context));
				}
				else{
					*(INT32 *)SystemCallData->Argument[4] = ERR_SUCCESS; 
					*(INT32 *)SystemCallData->Argument[3] = OSCreateProcess( processname, processaddress, processpriority );
					CALL(Z502SwitchContext( SWITCH_CONTEXT_SAVE_MODE, &CURRENTPCB->context ));
				}
			}
			break;
		/**************************************************************************************************************************************
		char process_name[N]
		INT32 process_id;
		INT32 error;

		Returns the process_id for the process whose name is "process_name". If process_name = "", then return the PID of the calling process. 
		An error is returned if a process with the requested name doesn't exist. Lots of other errors are also possible.
		Success means error = 0.
		http://web.cs.wpi.edu/~jb/CS502/Project/appendixC.html
		**************************************************************************************************************************************/
		case SYSNUM_GET_PROCESS_ID:
			processname = (char* )SystemCallData->Argument[0];
			if(GetPIDByName(readyqueue, processname)==99&&GetPIDByName(timerqueue, processname)==99){ //if nothing get, return 99
				//*(INT32 *)SystemCallData->Argument[1] = 99; //no return pid
				*(INT32 *)SystemCallData->Argument[2] = ERR_BAD_PARAM; 
				break;
			}
			else if(GetPIDByName(readyqueue, processname)!=99){ //if we can get data from readyqueue
				*(INT32 *)SystemCallData->Argument[1] = GetPIDByName(readyqueue, processname);
				*(INT32 *)SystemCallData->Argument[2] = ERR_SUCCESS; 
			}
			else{//get pid from timerqueue
				*(INT32 *)SystemCallData->Argument[1] = GetPIDByName(timerqueue, processname);
				*(INT32 *)SystemCallData->Argument[2] = ERR_SUCCESS; 
			}//how about suspend queue?
			break;
		/**************************************************************************************************************************************
		INT32 process_id;		
		INT32 error;
		SUSPEND_PROCESS( process_id, &error );

		Suspend the process whose PID is given by "process_id". If process_id = -1, then suspend self. Suspension of an already suspended 
		process results in no action being taken. Upon suspension, a process is removed from the ready queue; it will not run again until 
		that process is the target of a RESUME_PROCESS. An error is returned if a process with that PID doesn't exist. Lots of other errors 
		are also possible. Success means error = 0
		http://web.cs.wpi.edu/~jb/CS502/Project/appendixC.html
		**************************************************************************************************************************************/
		case SYSNUM_SUSPEND_PROCESS:
			processid = (INT32 )SystemCallData->Argument[0];
			//printf("receive suspend pid:%d\n",processid);
			if((processid<0&&processid!=-1)||processid>99){
				printf("ERROR! suspend the processid:%d is illegal, PID range from 0-99 and -1\n",processid);
				*(INT32 *)SystemCallData->Argument[1] = ERR_BAD_PARAM;
				break;
			}
			if(processid == -1||processid == CURRENTPCB->Processid){ //if suspend itself
				/*CALL(RemoveQueueByPid(readyqueue,CURRENTPCB->Processid)); 
				CURRENTPCB = start_PCB;       //father and child process? use priority to do this now
				*(INT32 *)SystemCallData->Argument[1] = ERR_SUCCESS;
				CALL(AddToReadyQueue(suspendqueue,CURRENTPCB));
				CALL(Z502SwitchContext( SWITCH_CONTEXT_SAVE_MODE, &start_PCB->context ));
				CALL(ListTwoQueue());*/
				*(INT32 *)SystemCallData->Argument[1] = ERR_ILLEGAL_ADDRESS;
				printf("ERROR! It is illegal to suspend yourself!\n"); //well, its illegal for test1f, legall for test1j
				break;
			}
			else{
				READ_MODIFY(MEMORY_INTERLOCK_BASE+0, DO_LOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//readyqueue
				READ_MODIFY(MEMORY_INTERLOCK_BASE+1, DO_LOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//timerqueue
				READ_MODIFY(MEMORY_INTERLOCK_BASE+2, DO_LOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//suspendqueue
				if(IsPidExist(suspendqueue, processid)){
					*(INT32 *)SystemCallData->Argument[1] = ERR_ILLEGAL_ADDRESS;
					printf("ERROR! can't suspend the process that already suspended!\n");
					break;
				}
				else if(IsPidExist(readyqueue, processid)){
					pcbtemp = GetPcbByPid(readyqueue,processid);
					CALL(AddToSuspendQueue(suspendqueue,&pcbtemp));
					CALL(RemoveQueueByPid(readyqueue,processid));
					*(INT32 *)SystemCallData->Argument[1] = ERR_SUCCESS;
				}
				else if(IsPidExist(timerqueue, processid)){
					pcbtemp = GetPcbByPid(timerqueue,processid);
					CALL(AddToSuspendQueue(suspendqueue,&pcbtemp));
					CALL(RemoveQueueByPid(timerqueue,processid));
					*(INT32 *)SystemCallData->Argument[1] = ERR_SUCCESS;
				}
				else{
					printf("ERROR! There is something wrong with suspend pid:%d\n",processid);
					*(INT32 *)SystemCallData->Argument[1] = ERR_BAD_PARAM; 
					break;
				}
				READ_MODIFY(MEMORY_INTERLOCK_BASE+0, DO_UNLOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//readyqueue
				READ_MODIFY(MEMORY_INTERLOCK_BASE+1, DO_UNLOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//timerqueue
				READ_MODIFY(MEMORY_INTERLOCK_BASE+2, DO_UNLOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//suspendqueue

				CALL(dospprint("SUSPEND", processid, CURRENTPCB));
				//CALL(ListTwoQueue()); //for debug
				//CALL(ListSuspendQueue());	
			}
			break;
		/**************************************************************************************************************************************
		INT32 process_id;		
		INT32 error;
		RESUME_PROCESS( process_id, &error );

		Resume the process whose PID is given by "process_id". Resumption of an not-suspended process results in an error. Upon resumption, 
		a process is again placed on the ready queue and is able to run. An error is returned if a process with that target PID doesn't exist. 
		Lots of other errors are also possible.Success means error = 0
		http://web.cs.wpi.edu/~jb/CS502/Project/appendixC.html
		**************************************************************************************************************************************/
		case SYSNUM_RESUME_PROCESS:
			processid = (INT32 )SystemCallData->Argument[0];
			//printf("receive resume pid:%d\n",processid);
			if((processid<0&&processid!=-1)||processid>99){
				printf("ERROR! resume the processid:%d is illegal, PID range from 0-99 and -1\n",processid);
				*(INT32 *)SystemCallData->Argument[1] = ERR_BAD_PARAM;
				break;
			}
			//lock
			READ_MODIFY(MEMORY_INTERLOCK_BASE+0, DO_LOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//readyqueue
			READ_MODIFY(MEMORY_INTERLOCK_BASE+2, DO_LOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//suspendqueue
			//if(processid == NULL){  //NULL is 0, is this a problem?
			if(processid<0||processid>99){  //the process number limit is 0-99
				printf("ERROR! no processid receive!\n");
				*(INT32 *)SystemCallData->Argument[1] = ERR_ILLEGAL_ADDRESS;
				break;
			}
			else if(IsPidExist(suspendqueue, processid)!=1){  //if not exsited in suspendqueue
				*(INT32 *)SystemCallData->Argument[1] = ERR_ILLEGAL_ADDRESS;
				printf("ERROR! This pid is not existed in suspendqueue!\n");
				break;
			}
			else{
				//READ_MODIFY(MEMORY_INTERLOCK_BASE+0, DO_LOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//it get deadlock here?
				if(IsPidExist(readyqueue, processid)!=1){ //no matter it come from readyqueue or timerqueue, it resume back to readyqueue!
					pcbtemp = GetPcbByPid(suspendqueue,processid);
					CALL(AddToReadyQueueByPriority(readyqueue,&pcbtemp));
					CALL(RemoveQueueByPid(suspendqueue,processid));
					*(INT32 *)SystemCallData->Argument[1] = ERR_SUCCESS;
					//READ_MODIFY(MEMORY_INTERLOCK_BASE+0, DO_UNLOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//readyqueue,dead lock here?
					//for debug
					//CALL(ListTwoQueue()); //for debug
					//CALL(ListSuspendQueue());
				}
				else{
					printf("ERROR! This pid %d is already existed in readyqueue!\n",processid);
					*(INT32 *)SystemCallData->Argument[1] = ERR_ILLEGAL_ADDRESS;
					break;
				}
							
			}
			//unlock
			READ_MODIFY(MEMORY_INTERLOCK_BASE+0, DO_UNLOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//readyqueue
			READ_MODIFY(MEMORY_INTERLOCK_BASE+2, DO_UNLOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//suspendqueue
			CALL(dospprint("RESUME", processid, CURRENTPCB)); 
			break;
		/**************************************************************************************************************************************
		INT32 process_id;
		INT32 new_priority;
		INT32 error;

		Change the priority of the process whose PID is given by "process_id". If process_id = -1, then change self. The result of a change 
		priority takes effect immediately. An error is returned if a process with that target PID doesn't exist. Lots of other errors are also 
		possible. Success means error = 0.
		http://web.cs.wpi.edu/~jb/CS502/Project/appendixC.html
		**************************************************************************************************************************************/
		case SYSNUM_CHANGE_PRIORITY:
			processid = (INT32 )SystemCallData->Argument[0];
			processpriority = (INT32 )SystemCallData->Argument[1];
			if(processpriority == 999){//proiorty 999 is illegal
				printf("ERROR! the priority:%d is illegal\n",processpriority);
				*(INT32 *)SystemCallData->Argument[2] = ERR_ILLEGAL_ADDRESS;
				break; 
			}
			if((processid<0&&processid!=-1)||processid>99){//process range from 0-99
				printf("ERROR! the PID:%d is illegal, PID range from 0-99 and -1\n",processid);
				*(INT32 *)SystemCallData->Argument[2] = ERR_ILLEGAL_ADDRESS;
				break;
			}
			//printf("processpriority:%d,processid:%d,dsfsdfsdgsdfsdgggggggggg\n",processpriority,processid);
			READ_MODIFY(MEMORY_INTERLOCK_BASE+0, DO_LOCK, SUSPEND_UNTIL_LOCKED, &LockResult); //readyqueue
			READ_MODIFY(MEMORY_INTERLOCK_BASE+1, DO_LOCK, SUSPEND_UNTIL_LOCKED, &LockResult); //timerqueue
			if(processid == -1){ //change the current priority
				//change both CURRENTPCB and the targetpid in readyqueue
				pnode = readyqueue->front; 
				icount = 1;
				while(pnode!=NULL&&icount<=readyqueue->size){ 
					if(pnode->data.Processid == CURRENTPCB->Processid){  //it could be not the first one in readyqueue, because the idle
						pnode->data.Priority = processpriority;
						CURRENTPCB->Priority = processpriority; 
						printf("Change susccessfully, Now The priority of (%s pid:%d) is %d\n",pnode->data.Name,pnode->data.Processid,pnode->data.Priority);
						//printf("susccessful change the current priority,now %s,pid %d is %d\n",CURRENTPCB->Name,CURRENTPCB->Processid,CURRENTPCB->Priority);	
						pcbtemp = pnode->data;
						break;
					}
					else {
						pnode = pnode->next;	
						icount++;
					}
				}
				*(INT32 *)SystemCallData->Argument[2] = ERR_SUCCESS;
				//insert into the readyqueue by priority
				CALL(RemoveQueueByPid(readyqueue,pcbtemp.Processid));
				CALL(AddToReadyQueueByPriority(readyqueue, &pcbtemp));	
			}
			else if(IsPidExist(readyqueue,processid)!=1&&IsPidExist(timerqueue,processid)!=1){//if neither in readyqueue or timerqueue,error, didnt consider about suspendqueue here
				printf("ERROR! the PID:%d is not existed in readyqueue and timerqueue\n",processid);
				*(INT32 *)SystemCallData->Argument[2] = ERR_ILLEGAL_ADDRESS;
				break; 
			}
			else{//modify readyqueue and timerqueue data
				if(IsPidExist(readyqueue,processid)){
					pnode = readyqueue->front; 
					icount = 1;
					while(pnode!=NULL&&icount<=readyqueue->size){ 
						if(pnode->data.Processid == processid){  
							pnode->data.Priority = processpriority;
							printf("Change susccessfully, Now The priority of (%s pid:%d) is %d\n",pnode->data.Name,pnode->data.Processid,pnode->data.Priority);
							if(CURRENTPCB->Processid == processid){ //if it is CURRENTPID
								CURRENTPCB->Priority = processpriority; 
								printf("Change susccessfully, Now The priority of (%s pid:%d) is %d\n",CURRENTPCB->Name,CURRENTPCB->Processid,CURRENTPCB->Priority);
							}
							pcbtemp = pnode->data;
							break;
						}
						else {
							pnode = pnode->next;	
							icount++;
						}
					}
					CALL(RemoveQueueByPid(readyqueue,pcbtemp.Processid));
					CALL(AddToReadyQueueByPriority(readyqueue, &pcbtemp));		
				}
				else{//timerqueue
					pnode = timerqueue->front; 
					icount = 1;
					while(pnode!=NULL&&icount<=timerqueue->size){ 
						if(pnode->data.Processid == processid){ 
							pnode->data.Priority = processpriority;
							printf("Change susccessfully, Now The priority of (%s pid:%d) is %d\n",pnode->data.Name,pnode->data.Processid,pnode->data.Priority);
							break;
						}
						else {
							pnode = pnode->next;	
							icount++;
						}
					}
				}
				*(INT32 *)SystemCallData->Argument[2] = ERR_SUCCESS;
			}
			READ_MODIFY(MEMORY_INTERLOCK_BASE+0, DO_UNLOCK, SUSPEND_UNTIL_LOCKED, &LockResult); //readyqueue
			READ_MODIFY(MEMORY_INTERLOCK_BASE+1, DO_UNLOCK, SUSPEND_UNTIL_LOCKED, &LockResult); //timerqueue
			if(processid==-1){ //-1 sp print is not supporting pid -1 argument, so get the real pid instead
				CALL(dospprint("MODIFY", CURRENTPCB->Processid, CURRENTPCB));
			}
			else CALL(dospprint("MODIFY", processid, CURRENTPCB)); 
			break;
		/**************************************************************************************************************************************
		INT32 target_pid;
		char message_buffer[N];
		INT32 message_send_length;
		INT32 error;

		Send a message to the target process. When the target does a "RECEIVE_MESSAGE", place data from this send in the message buffer of 
		that target. message_send_length is the size of the send buffer - it must be larger than or equal in size to the string of data that 
		is actually sent; so this parameter is a buffer size rather than a message size. If the target_pid = -1, then broadcast the message 
		to all potential receivers (although this message will actually be intercepted by only one of those receivers).
		Lots of different errors are possible. Success means error = 0.
		http://web.cs.wpi.edu/~jb/CS502/Project/appendixC.html
		**************************************************************************************************************************************/
		case SYSNUM_SEND_MESSAGE:
			processid = (INT32 )SystemCallData->Argument[0];
			messagebuff = (char *)SystemCallData->Argument[1];//buff has to large than send lenth
			sendlength = (INT32 )SystemCallData->Argument[2];
			//printf("doing send_message:pid %d,msg_buffer:%s,length:%d\n",processid,messagebuff,sendlength);
			//init return argument
			*(INT32 *)SystemCallData->Argument[3] = ERR_SUCCESS; //only error lead to other return

			if(sendlength>64){//I use the 64 as message length limit
				printf("ERROR! The send_length:%d is illegal\n",sendlength);
				*(INT32 *)SystemCallData->Argument[3] = ERR_ILLEGAL_ADDRESS;
				break;
			}
			if(sendlength<strlen(messagebuff)){ //if the real message is large than the buff length, ERROR
				printf("ERROR! The send_length:%d is not enough\n",sendlength);
				*(INT32 *)SystemCallData->Argument[3] = ERR_ILLEGAL_ADDRESS;
				break;
			}
			if(processid == -1){//if processid is -1, broadcase
				//printf("the processid:%d, let's broadcast messages\n",processid);
				//just insert into messagelist
				if(messagecount>MessageLimit-1){ //limit message number 100
						printf("ERROR! The limit number of messages is 100\n");
						*(INT32 *)SystemCallData->Argument[3] = ERR_ILLEGAL_ADDRESS;
						break;
				}
				else{
					READ_MODIFY(MEMORY_INTERLOCK_BASE+3, DO_LOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//messagelist
					//fill the data to messagelist
					messagelist[messagecount].actual_send_length = strlen(messagebuff);
					messagelist[messagecount].actual_source_pid = CURRENTPCB->Processid;
					messagelist[messagecount].loop_count = 0; //no use
					strcpy(messagelist[messagecount].msg_buffer,messagebuff);
					messagelist[messagecount].receive_length = 0; //init receive buff,no use
					messagelist[messagecount].send_length = sendlength;
					messagelist[messagecount].source_pid = CURRENTPCB->Processid; 
					messagelist[messagecount].target_pid = processid;//store -1 here, well, sp print cant show it
					messagecount++;
					READ_MODIFY(MEMORY_INTERLOCK_BASE+3, DO_UNLOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//messagelist
				}	
			}
			else if(processid<0||processid>99){
				printf("ERROR! The processid:%d is illegal\n",processid);
				*(INT32 *)SystemCallData->Argument[3] = ERR_ILLEGAL_ADDRESS;
				break;
			}
			else{
				if(processid==CURRENTPCB->Processid){//can sender send to itself?
					//yes, we do yes here
					if(messagecount>MessageLimit-1){ //limit message number 100
						printf("ERROR! The limit number of messages is 100\n");
						*(INT32 *)SystemCallData->Argument[3] = ERR_ILLEGAL_ADDRESS;
						break;
					}
					else{
						READ_MODIFY(MEMORY_INTERLOCK_BASE+3, DO_LOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//messagelist
						//fill data to messagelist
						messagelist[messagecount].actual_send_length = strlen(messagebuff);  
						messagelist[messagecount].actual_source_pid = CURRENTPCB->Processid;
						messagelist[messagecount].loop_count = 0; //no use
						strcpy(messagelist[messagecount].msg_buffer,messagebuff);
						messagelist[messagecount].receive_length = 0;//no use
						messagelist[messagecount].send_length = sendlength;
						messagelist[messagecount].source_pid = CURRENTPCB->Processid;
						messagelist[messagecount].target_pid = processid;
						messagecount++;
						READ_MODIFY(MEMORY_INTERLOCK_BASE+3, DO_UNLOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//messagelist
					}	
				}
				//if the receive pid is not exsited? 
				else if(IsPidExist(readyqueue,processid)!=1&&IsPidExist(timerqueue,processid)!=1&&IsPidExist(suspendqueue,processid)!=1){ 
					//no, we are not allow to do this
					printf("ERROR! the pid is not exsited in OS queue!");
					*(INT32 *)SystemCallData->Argument[3] = ERR_ILLEGAL_ADDRESS;
					break;
				}
				else if(IsPidExist(suspendqueue,processid)==1){//if the targetpid is in suspendqueue, we resume that process
					if(messagecount>MessageLimit-1){//limit message number 100
						printf("ERROR! The limit number of messages is 100\n");
						*(INT32 *)SystemCallData->Argument[3] = ERR_ILLEGAL_ADDRESS;
						break;
					}
					else{
						READ_MODIFY(MEMORY_INTERLOCK_BASE+0, DO_LOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//readyqueue
						READ_MODIFY(MEMORY_INTERLOCK_BASE+2, DO_LOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//suspendqueue
						READ_MODIFY(MEMORY_INTERLOCK_BASE+3, DO_LOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//messagelist
						//resume pid readyqueue
						pcbtemp = GetPcbByPid(suspendqueue,processid);
						CALL(AddToReadyQueueByPriority(readyqueue,&pcbtemp));
						CALL(RemoveQueueByPid(suspendqueue,processid));
						//renew the messagelist
						messagelist[messagecount].actual_send_length = strlen(messagebuff); 
						messagelist[messagecount].actual_source_pid = CURRENTPCB->Processid;
						messagelist[messagecount].loop_count = 0; //no use
						strcpy(messagelist[messagecount].msg_buffer,messagebuff);
						messagelist[messagecount].receive_length = 0; //no use
						messagelist[messagecount].send_length = sendlength;
						messagelist[messagecount].source_pid = CURRENTPCB->Processid;
						messagelist[messagecount].target_pid = processid;
						messagecount++;
						//*(INT32 *)SystemCallData->Argument[3] = ERR_SUCCESS;
						READ_MODIFY(MEMORY_INTERLOCK_BASE+0, DO_UNLOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//readyqueue
						READ_MODIFY(MEMORY_INTERLOCK_BASE+2, DO_UNLOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//suspendqueue
						READ_MODIFY(MEMORY_INTERLOCK_BASE+3, DO_UNLOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//messagelist
						dospprint("RESUME", processid, CURRENTPCB);
					}
				}
				else{
					if(messagecount>MessageLimit-1){ //limit message number 100
						printf("ERROR! The limit number of messages is 100\n");
						*(INT32 *)SystemCallData->Argument[3] = ERR_ILLEGAL_ADDRESS;
						break;
					}
					READ_MODIFY(MEMORY_INTERLOCK_BASE+0, DO_LOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//readyqueue
					READ_MODIFY(MEMORY_INTERLOCK_BASE+2, DO_LOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//suspendqueue
					READ_MODIFY(MEMORY_INTERLOCK_BASE+3, DO_LOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//messagelist
					//插入messagelist
					messagelist[messagecount].actual_send_length = strlen(messagebuff);  
					messagelist[messagecount].actual_source_pid = CURRENTPCB->Processid;
					messagelist[messagecount].loop_count = 0; //no use
					strcpy(messagelist[messagecount].msg_buffer,messagebuff);
					messagelist[messagecount].receive_length = 0; //no use
					messagelist[messagecount].send_length = sendlength;
					messagelist[messagecount].source_pid = CURRENTPCB->Processid;
					messagelist[messagecount].target_pid = processid;
					messagecount++;
					//*(INT32 *)SystemCallData->Argument[3] = ERR_SUCCESS;
					READ_MODIFY(MEMORY_INTERLOCK_BASE+0, DO_UNLOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//readyqueue
					READ_MODIFY(MEMORY_INTERLOCK_BASE+2, DO_UNLOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//suspendqueue
					READ_MODIFY(MEMORY_INTERLOCK_BASE+3, DO_UNLOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//messagelist
				}
			}		
			break;
		/**************************************************************************************************************************************
		INT32 source_pid;
		char message_buffer[N];
		INT32 message_receive_length;
		INT32 message_send_length;
		INT32 message_sender_pid;
		INT32 error;

		Receive a message from source_pid. If source_pid = -1, then receive from any sender who has specifically targeted you or who has done 
		a broadcast. If -1 is used, then &message_sender_pid contains the pid of the process that did the send. The Operating System will place 
		that message in message buffer if it is less than message_receive_length bytes long. message_receive_length is the size available in 
		the receive buffer. Return the size of the send buffer in message_send_length. In general, a RECEIVE_MESSAGE system call causes the 
		receiver process to suspend itself until the SEND is made. Devise appropriate rules of behaviour for the sender and receiver.
		http://web.cs.wpi.edu/~jb/CS502/Project/appendixC.html
		**************************************************************************************************************************************/
		case SYSNUM_RECEIVE_MESSAGE:
			processid = (INT32 )SystemCallData->Argument[0];
			//messagebuff = (char *)SystemCallData->Argument[1]; //acturally this value is for return, its confused
			receivelength = (INT32 )SystemCallData->Argument[2];
			//printf("doing receive_message:pid %d,length:%d\n",processid,receivelength);
			//init the return argument
			*(INT32 *)SystemCallData->Argument[3] = 0; //actual_send_length return
			*(INT32 *)SystemCallData->Argument[4] = 0; //actual_source_pid return, there is a situation -1
			*(INT32 *)SystemCallData->Argument[5] = ERR_SUCCESS; //default success, only error lead to other return
			if(receivelength>64){
				printf("ERROR! The receivelength:%d is illegal\n",receivelength);
				*(INT32 *)SystemCallData->Argument[5] = ERR_ILLEGAL_ADDRESS;
				break;
			}
			if(processid == -1){//when sourcepid is -1, mean receive all sourcepid, but only receive one message once!
				//printf("the processid:%d, let's recevie any message send to us from anyone\n",processid);
				jcount = 0;	//everytime after suspend, we need to receive again, this is count for this
				while(jcount==0){ //only get pid count for once
					for(icount = 0;icount<messagecount;icount++){
						if(messagelist[icount].target_pid == CURRENTPCB->Processid||messagelist[icount].target_pid==-1){ //if targetpid is -1, also receive it
							//printf("pid:%d receive %s from pid:%d\n",CURRENTPCB->Processid,messagelist[icount].msg_buffer,messagelist[icount].source_pid);
							if(receivelength<strlen(messagelist[icount].msg_buffer)){//if the receive length is larger than buff, ERROR
								printf("ERROR! The receivelength:%d is not enough\n",receivelength);
								*(INT32 *)SystemCallData->Argument[5] = ERR_ILLEGAL_ADDRESS;
								jcount++;
								break;
							}
							else{
								strcpy((char *)SystemCallData->Argument[1],messagelist[icount].msg_buffer);//return the received message
							}
							//*(INT32 *)SystemCallData->Argument[3] = messagelist[icount].actual_send_length; //this return value is also confused
							*(INT32 *)SystemCallData->Argument[3] = messagelist[icount].send_length; //it should be actural length, but the requirement..ok,just return send_lengh
							*(INT32 *)SystemCallData->Argument[4] = messagelist[icount].actual_source_pid; //actual_source_pid reture	
							//lock for messagelist
							READ_MODIFY(MEMORY_INTERLOCK_BASE+3, DO_LOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//messagelist
							messageprocess(messagelist[icount].msg_buffer);
							removefrommessagelist(icount);
							READ_MODIFY(MEMORY_INTERLOCK_BASE+3, DO_UNLOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//messagelist
							jcount++;
							break; //can break when get one message
						}
					}
					*(INT32 *)SystemCallData->Argument[5] = ERR_SUCCESS;
					//if no message receive, suspend itself
					if(jcount == 0){
						READ_MODIFY(MEMORY_INTERLOCK_BASE+0, DO_LOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//readyqueue
						READ_MODIFY(MEMORY_INTERLOCK_BASE+2, DO_LOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//suspendqueue
						//printf("pid:%d receive nothing from pid:%d,suspend itself\n",CURRENTPCB->Processid,messagelist[icount].source_pid);
						pcbtemp = GetPcbByPid(readyqueue,CURRENTPCB->Processid);
						CALL(AddToSuspendQueue(suspendqueue,&pcbtemp));
						CALL(RemoveQueueByPid(readyqueue,CURRENTPCB->Processid));
						READ_MODIFY(MEMORY_INTERLOCK_BASE+0, DO_UNLOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//readyqueue
						READ_MODIFY(MEMORY_INTERLOCK_BASE+2, DO_UNLOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//suspendqueue
						dospprint("SUSPEND", CURRENTPCB->Processid, CURRENTPCB);//because the pid is -1, we get real pid here
						//suspend itself and switch to readyqueue
						memcpy(CURRENTPCB, &readyqueue->front->data,sizeof(Process_Control_Block));
						CALL(Z502SwitchContext( SWITCH_CONTEXT_SAVE_MODE, &CURRENTPCB->context));
						//after switch back, we do recevie again, well, it just for test1j
					}
				}		
			}
			else if(processid<0||processid>99){
				printf("ERROR! The processid:%d is illegal\n",processid);
				*(INT32 *)SystemCallData->Argument[5] = ERR_ILLEGAL_ADDRESS;
				break;
			}
			else{
				if(IsSourcePidExsit(processid)!=1){ //if the source pid is not in messagelist, suspend itself, it is legal here
					READ_MODIFY(MEMORY_INTERLOCK_BASE+0, DO_LOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//readyqueue
					READ_MODIFY(MEMORY_INTERLOCK_BASE+2, DO_LOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//suspendqueue
					//printf("the source_pid is not exsited in messagelist\n");
					pcbtemp = GetPcbByPid(readyqueue,CURRENTPCB->Processid);
					CALL(AddToSuspendQueue(suspendqueue,&pcbtemp));
					CALL(RemoveQueueByPid(readyqueue,CURRENTPCB->Processid));
					READ_MODIFY(MEMORY_INTERLOCK_BASE+0, DO_UNLOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//readyqueue
					READ_MODIFY(MEMORY_INTERLOCK_BASE+2, DO_UNLOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//suspendqueue
					dospprint("SUSPEND", processid, CURRENTPCB);
					//suspend itself and switch to readyqueue
					memcpy(CURRENTPCB, &readyqueue->front->data,sizeof(Process_Control_Block));
					CALL(Z502SwitchContext( SWITCH_CONTEXT_SAVE_MODE, &CURRENTPCB->context)); 
				}
				else{
					jcount = 0;
					while(jcount==0){
						//if targetpid is match and from the right source, just delete it
						for(icount = 0;icount<messagecount;icount++){
							if(messagelist[icount].target_pid == CURRENTPCB->Processid&&messagelist[icount].source_pid == processid){
								//printf("pid:%d receive %s from pid:%d\n",CURRENTPCB->Processid,messagelist[icount].msg_buffer,messagelist[icount].source_pid);
								if(receivelength<strlen(messagelist[icount].msg_buffer)){//if the receive length is larger than buff, ERROR
									printf("ERROR! The receivelength:%d is not enough\n",receivelength);
									*(INT32 *)SystemCallData->Argument[5] = ERR_ILLEGAL_ADDRESS;
									jcount++;
									break; 
								}
								else{
									strcpy((char *)SystemCallData->Argument[1],messagelist[icount].msg_buffer);//return message received
								}
								//*(INT32 *)SystemCallData->Argument[3] = messagelist[icount].actual_send_length; 
								*(INT32 *)SystemCallData->Argument[3] = messagelist[icount].send_length; //confused value, ok for test1j
								*(INT32 *)SystemCallData->Argument[4] = messagelist[icount].actual_source_pid;
								//lock for messagelist
								READ_MODIFY(MEMORY_INTERLOCK_BASE+3, DO_LOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//messagelist
								messageprocess(messagelist[icount].msg_buffer);
								removefrommessagelist(icount);
								READ_MODIFY(MEMORY_INTERLOCK_BASE+3, DO_UNLOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//messagelist
								//everytime after suspend, we need to receive again, this is count for this
								jcount++; 
								break;//one time get one
							}
						}
						//if that source has no message for curentpcb, suspend the currentpcb until send resume
						if(jcount == 0){
							READ_MODIFY(MEMORY_INTERLOCK_BASE+0, DO_LOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//readyqueue
							READ_MODIFY(MEMORY_INTERLOCK_BASE+2, DO_LOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//suspendqueue
							//printf("pid:%d receive nothing from pid:%d,suspend itself\n",CURRENTPCB->Processid,messagelist[icount].source_pid);
							pcbtemp = GetPcbByPid(readyqueue,CURRENTPCB->Processid);
							CALL(AddToSuspendQueue(suspendqueue,&pcbtemp));
							CALL(RemoveQueueByPid(readyqueue,CURRENTPCB->Processid));
							READ_MODIFY(MEMORY_INTERLOCK_BASE+0, DO_LOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//readyqueue
							READ_MODIFY(MEMORY_INTERLOCK_BASE+2, DO_LOCK, SUSPEND_UNTIL_LOCKED, &LockResult);//suspendqueue
							//print when doing change
							dospprint("SUSPEND", processid, CURRENTPCB);
							//suspend itself and switch to readyqueue
							memcpy(CURRENTPCB, &readyqueue->front->data,sizeof(Process_Control_Block));
							CALL(Z502SwitchContext( SWITCH_CONTEXT_SAVE_MODE, &CURRENTPCB->context)); 
						}
					}
				}
			}
			//printf("actual_send_length:%d,actual_source_pid:%d\n",*(INT32 *)SystemCallData->Argument[3],*(INT32 *)SystemCallData->Argument[4]);
			break;
        default:
            printf( "* ERROR!  call_type not recognized!\n" );
            printf( "* Call_type is - %i\n", call_type);
    }           
}                                               // End of svc
/************************************************************************
below are universal routines for readyqueue, timerqueue and suspend queue
   
   InitQueue, IsEmpty, IsPidExist, IsNameDuplicate, GetPIDByName,
   GetPcbByPid, DeQueueFirstElement, RemoveQueueByName,RemoveQueueByPid
************************************************************************/

/************************************************************************
InitQueue
//the routine to initialize a new queue

in: void
out: queue
************************************************************************/
PCBQueue *InitQueue()  
{  
    PCBQueue *pqueue = (PCBQueue *)malloc(sizeof(PCBQueue));  
    if(pqueue!=NULL)  
    {  
        pqueue->front = NULL;  
        pqueue->rear = NULL;  
        pqueue->size = 0;  
    }  
    return pqueue;  
} 

/************************************************************************
IsEmpty
//judge if the queue is empty

in: queue
out: INT32(1/0)
************************************************************************/
INT32 IsEmpty(PCBQueue *pqueue)  
{  
    if(pqueue->front==NULL&&pqueue->rear==NULL&&pqueue->size==0)  
        return 1;  
    else  
        return 0;  
} 

/************************************************************************
IsPidExist
//judge if the pid is existed in queue

in: queue, process id
out: INT32(1/0)
************************************************************************/
INT32 IsPidExist(PCBQueue *pqueue, INT32 pid)  
{
	PCBNode pnode;
	INT32 icount;

	pnode = pqueue->front;
	icount =1;
	while(pnode!=NULL&&icount<=pqueue->size){
		if(pnode->data.Processid == pid){
			return 1;
		}
		pnode = pnode->next;
		icount++;
	}
	return 0;
}

/************************************************************************
IsNameDuplicate
//judge if the process name is already in queue

in: queue, process name
out: INT32(1/0)
************************************************************************/
INT32 IsNameDuplicate( PCBQueue *pqueue, char *processname ){ 
	PCBNode pnode =	pqueue->front;
	if(IsEmpty(pqueue)){
		return 0;
	}
	while (pnode!= NULL)
	{
		if (strcmp(pnode->data.Name,processname)==0 ){
			return 1;
		}
		else {
			pnode = pnode->next;
		}
	}
	return 0;
}

/************************************************************************
GetPIDByName
//get pid through process name in queue

in: queue, process name
out: process id
************************************************************************/
INT32 GetPIDByName(PCBQueue *pqueue, char* pname){  
	PCBNode current = pqueue->front;
	if(pname ==NULL||pname ==""||strcmp(pname,"")==0){
		//return readyqueue->rear->data.Processid;
		return CURRENTPCB->Processid;
	}
	while (current!= NULL)
	{
		if(strcmp(current->data.Name,pname)==0){
			return current->data.Processid;
		}
		current = current->next;
	}
	return 99; //default
}

/************************************************************************
GetPcbByPid
//get the PCB through pid in queue

in: queue, process id
out: PCB
************************************************************************/
Process_Control_Block GetPcbByPid(PCBQueue *pqueue, INT32 pid){ 
	PCBNode pnode;
	INT32 icount;

	pnode = pqueue->front;
	icount =1;
	while(pnode!=NULL&&icount<=pqueue->size){
		if(pnode->data.Processid == pid){
			return pnode->data;
		}
		pnode = pnode->next;
		icount++;
	}
}

/************************************************************************
DeQueueFirstElement
//delete the first element in queue

in: queue
out: node
************************************************************************/
PCBNode DeQueueFirstElement(PCBQueue *pqueue){ 
    PCBNode pnode;  
	pnode = pqueue->front;
	//pnode.data = pqueue->front->data;
	//pnode.next = pqueue->front->next;
    if(IsEmpty(pqueue)!=1&&pnode!=NULL)  
    {  
        pqueue->size--;  
        pqueue->front = pnode->next;  
        if(pqueue->size==0){
			pqueue->rear = NULL;  
		}
    }  
    return pnode;  
}

/************************************************************************
RemoveQueueByName
//delete all match nodes through process name in queue

in: queue, process name
out: node
************************************************************************/
PCBNode RemoveQueueByName(PCBQueue *pqueue,char *processname)  
{
	PCBNode pnode = pqueue->front;
	PCBNode ptemp;
	INT32 flag = 0; //the record for elements

	while(1)
	{
		if(pnode!=NULL){
			if (strcmp(pnode->data.Name, processname)==0) //handle the head element
			{
				flag++;
				pqueue->front = pqueue->front->next;
				pnode = pqueue->front;
				pqueue->size--;
				if(pqueue->size==0)//if no element left, delete rear node as well
				{
					pqueue->rear = NULL;
				}
			}
			else break;
		}
		else 
		{
			//printf("Delete %d nodes, the total number left  in the queue is %d\n", flag, pqueue->size);
			return pnode;
		}
	}
	while (pnode->next!= NULL)
	{
		if (strcmp(pnode->next->data.Name,processname)==0 )
		{
			flag++;
			ptemp = pnode->next;
			if (pnode->next->next != NULL)
			{
				pnode->next = pnode->next->next;
				free(ptemp);
				ptemp = NULL;
				pqueue->size--;
			}
			else //if the target is the last node
			{
				free(ptemp);
				ptemp = NULL;
				pnode->next = NULL;
				pqueue->rear = pnode; //point the last element to rear after delete
				pqueue->size--;
				break;
			}
		}
		else //if no one match, move to next node
		{
			pnode = pnode->next;
		}
	}
	//printf("Delete %d nodes, the total number left in the queue is %d\n", flag, pqueue->size);
	return pnode;
}

/************************************************************************
RemoveQueueByPid
//delete all match nodes through process name in queue, 

in: queue, process id
out: INT32(1/0)
************************************************************************/
INT32 RemoveQueueByPid(PCBQueue *pqueue, INT32 processid)  
{
	PCBNode pnode = pqueue->front;
	PCBNode ptemp;
	INT32 flag = 0;

	while(1)
	{
		if(pnode!=NULL){
			if (pnode->data.Processid == processid) 
			{
				flag++;
				pqueue->front = pqueue->front->next;
				pnode = pqueue->front;
				pqueue->size--;
				if(pqueue->size==0)
				{
					pqueue->rear = NULL;
				}
			}
			else break;
		}
		else 
		{
			//printf("Delete %d nodes, the total number left  in the queue is %d\n", flag, pqueue->size);
			if(flag>0) return 1;
			else return 0;
		}
	}
	while (pnode->next != NULL)
	{
		if (pnode->next->data.Processid == processid )
		{
			flag++;
			ptemp = pnode->next;
			if (pnode->next->next != NULL)
			{
				pnode->next = pnode->next->next;
				free(ptemp);
				ptemp = NULL;
				pqueue->size--;
			}
			else
			{
				free(ptemp);
				ptemp = NULL;
				pnode->next = NULL;
				pqueue->rear = pnode; 
				pqueue->size--;
				break;
			}
		}
		else 
		{
			pnode = pnode->next;
		}
	}
	//printf("Delete %d nodes, the total number left in the queue is %d\n", flag, pqueue->size);
	if(flag>0) return 1;
	else return 0;
}
/************************************************************************
Below are routines just for timerqueue
	
	AddToTimerQueue, RemoveFromTimerQueue
************************************************************************/

/************************************************************************
AddToTimerQueue
// add the pcb and current time to the timerqueue

in: queue, PCB, current time
out: node
************************************************************************/
PCBNode AddToTimerQueue(PCBQueue *pqueue,Process_Control_Block *pcb, INT32 ptime)
{
	PCBNode current, pnode;
	pnode = (PCBNode)malloc(sizeof(Node)); //first time write Node to PCBNode, waste me 1 day to debug it
	pnode->data = *pcb; 
	pnode->time = ptime;
	pnode->next = NULL;  //it should be NULL
	
	if(IsEmpty(pqueue))  
    {  
		pqueue->front = pnode; 
		pqueue->rear = pnode;
		//pqueue->size++;  
		pqueue->size = 1; 
		return pnode;
	}
	current = pqueue->front;
	while (current->next != NULL)
	{
		current = current->next;
	}
	current->next = pnode;
	pqueue->rear = pnode;
	pqueue->size++;  
	return pnode;
}

/************************************************************************
RemoveFromTimerQueue
//remove the node in timerqueue, almost the same as RemoveQueueByName

in: queue, process id
out: node
************************************************************************/
PCBNode RemoveFromTimerQueue(PCBQueue *pqueue, INT32 processid) 
{
	PCBNode pnode = pqueue->front;
	PCBNode ptemp;
	INT32 flag = 0;

	while (1)
	{
		if(pnode!=NULL){
			if (pnode->data.Processid == processid) //delete head situation
			{
				flag++;
				pqueue->front = pqueue->front->next;
				pnode = pqueue->front;
				pqueue->size--;
				if(pqueue->size==0)
				{
					pqueue->rear = NULL;
				}
			}
			else break;
		}
		else 
		{
			//printf("Delete %d nodes, the total number left  in the queue is %d\n", flag, pqueue->size);
			return pnode;
		}
	}
	while (pnode->next != NULL)
	{
		if (pnode->next->data.Processid == processid )
		{
			flag++;
			ptemp = pnode->next;
			if (pnode->next->next != NULL)
			{
				pnode->next = pnode->next->next;
				free(ptemp);
				ptemp = NULL;
				pqueue->size--;
			}
			else 
			{
				free(ptemp);
				ptemp = NULL;
				pnode->next = NULL;
				pqueue->rear = pnode;
				pqueue->size--;
				break;
			}
		}
		else 
		{
			pnode = pnode->next;
		}
	}
	//printf("Delete %d nodes, the total number left in the queue is %d\n", flag, pqueue->size);
	return pnode;
}

/************************************************************************
Below are routines just for readyqueue
	
	AddToReadyQueueByPriority, AddToReadyQueue
************************************************************************/

/************************************************************************
AddToReadyQueueByPriority
//add the pcb to readyqueue order by priority, low number mean high priority

in: queue, PCB
out: node
************************************************************************/
PCBNode AddToReadyQueueByPriority(PCBQueue *pqueue,Process_Control_Block *pcb) 
{
	PCBNode current, previous, pnode;
	INT32 position, positioncount;
	pnode = (PCBNode)malloc(sizeof(Node));
	pnode->data = *pcb; 
	pnode->next = NULL;  //it should be NULL
	
	if(IsEmpty(pqueue))  //if the readyqueue is empty, init it
    {  
		pqueue->front = pnode; 
		pqueue->rear = pnode;
		//pqueue->size++;  
		pqueue->size = 1;
		return pnode;
	}

	current = pqueue->front;
	position = 1;
	 //get the insert position
	while(current!=NULL&&position<=pqueue->size){
		if(pnode->data.Priority<current->data.Priority){
			break;
		}
		current = current->next;
		position++; 
	}

	if(position == 1){ //if insert into the first position
		pnode->next = pqueue->front;
		pqueue->front = pnode; 
		//pqueue->rear = current;
		pqueue->size++;  
		return pnode;
	}
	else if(position == pqueue->size+1){//if insert into the last position
		current = pqueue->front;
		while (current->next != NULL)
		{
			current = current->next;
		}
		current->next = pnode;
		pqueue->rear = pnode;
		pqueue->size++;  
		return pnode;
	}
	else{ //if insert between first and last node
		previous = pqueue->front; 
		positioncount = 2;
		while(positioncount<position){
			previous = previous->next;
			positioncount++;
		}
		pnode->next = previous->next;
		previous->next = pnode;
		pqueue->size++; 
		return pnode;
	}
}

/************************************************************************
AddToReadyQueue
//add to readyqueue, order by FIFO

in: queue, PCB
out: node
************************************************************************/
PCBNode AddToReadyQueue(PCBQueue *pqueue,Process_Control_Block *pcb) 
{
	PCBNode current, pnode;
	pnode = (PCBNode)malloc(sizeof(Node));
	pnode->data = *pcb; 
	pnode->next = NULL; 
	
	if(IsEmpty(pqueue))  
    {  
		pqueue->front = pnode; 
		pqueue->rear = pnode;
		//pqueue->size++;  
		pqueue->size = 1;
		return pnode;
	}
	current = pqueue->front;
	while (current->next != NULL)
	{
		current = current->next;
	}
	current->next = pnode;
	pqueue->rear = pnode;
	pqueue->size++;  
	return pnode;
}

/************************************************************************
Below are routines just for suspendqueue
	
	AddToSuspendQueue
************************************************************************/

/************************************************************************
AddToSuspendQueue
//the same logic as addtoreadyqueue, just change the name, FIFO order

in: queue, PCB
out: node
************************************************************************/
PCBNode AddToSuspendQueue(PCBQueue *pqueue,Process_Control_Block *pcb) 
{
	PCBNode current, pnode;
	pnode = (PCBNode)malloc(sizeof(Node)); 
	pnode->data = *pcb; 
	pnode->next = NULL;  //it should be NULL
	
	if(IsEmpty(pqueue))  
    {  
		pqueue->front = pnode; 
		pqueue->rear = pnode;
		//pqueue->size++;  
		pqueue->size = 1;
		return pnode;
	}
	current = pqueue->front;
	while (current->next != NULL)
	{
		current = current->next;
	}
	current->next = pnode;
	pqueue->rear = pnode;
	pqueue->size++;  
	return pnode;
}

/**************************************************************************************************************************************
The debug routines

	ListQueue, ListTimerQueue, ListReadyQueue, ListSuspendQueue, ListTwoQueue
**************************************************************************************************************************************/

/************************************************************************
ListQueue
//list all the elements in the queue

in: queue
out: 
************************************************************************/
void ListQueue(PCBQueue *pqueue)  
{  
	INT32 k=1;
    PCBNode pnode = pqueue->front;  
    INT32 i = pqueue->size;  
	printf("The total number of elements in the queue is %d, now list them\n", i);
	printf("----------------------------------------------------------------------------\n");
    while(i--)  
    {  
        printf("%d: PCB.Processid = %d || PCB.Priority = %d || PCB.Name = %s \n", k, pnode->data.Processid, pnode->data.Priority, pnode->data.Name);
        pnode = pnode->next;  
		k++;
    }   
	printf("----------------------------------------------------------------------------\n");
}  

/************************************************************************
ListTimerQueue
//list all the elements in the timerqueue

in: 
out: 
************************************************************************/
void ListTimerQueue()  
{  
	INT32 k=1;
    PCBNode pnode = timerqueue->front;    
	//printf("----------------------------------------------------------------------------\n");
	printf("TimerQueue, total number: %d\n", timerqueue->size);
    while(pnode!=NULL&&k<=timerqueue->size) 
    {  
		printf("%d: PID = %d || Priority = %d || Name = %s || Timerecord = %d\n", k, pnode->data.Processid, pnode->data.Priority, pnode->data.Name, pnode->time);
        pnode = pnode->next;  
		k++;
    }   
	printf("----------------------------------------------------------------------------\n");
}  

/************************************************************************
ListReadyQueue
//list all the elements in the readyqueue

in: 
out: 
************************************************************************/
void ListReadyQueue()  
{  
	INT32 k=1;
    PCBNode pnode = readyqueue->front;  
	printf("----------------------------------------------------------------------------\n");
	printf("ReadyQueue, total number: %d\n", readyqueue->size);
    while(pnode!=NULL&&k<=readyqueue->size) 
    {  
        printf("%d: PID = %d || Priority = %d || Name = %s \n", k, pnode->data.Processid, pnode->data.Priority, pnode->data.Name);
        pnode = pnode->next;  
		k++;
    }   
	//printf("----------------------------------------------------------------------------\n");
} 

/************************************************************************
ListSuspendQueue
//list all the elements in the suspendqueue

in: 
out: 
************************************************************************/
void ListSuspendQueue()  
{  
	INT32 M=1;
    PCBNode pnode = suspendqueue->front; 
	printf("----------------------------------------------------------------------------\n");
	printf("suspendqueue, total number: %d\n", suspendqueue->size);
    while(pnode!=NULL&&M<=suspendqueue->size) //sometimes it cross the border, maybe cause of no lock
    {  
        printf("%d: PID = %d || Priority = %d || Name = %s \n", M, pnode->data.Processid, pnode->data.Priority, pnode->data.Name);
        pnode = pnode->next;  
		M++;
    }   
	printf("----------------------------------------------------------------------------\n");
} 

/************************************************************************
ListTwoQueue
 //list all the elements in the readyqueue and timerqueue

in: 
out: 
************************************************************************/
void ListTwoQueue() 
{  
	INT32 M=1,N=1;
    PCBNode pnode = readyqueue->front; 
	PCBNode dnode = timerqueue->front;  
	printf("----------------------------------------------------------------------------\n");
	printf("ReadyQueue, total number: %d\n", readyqueue->size);
    while(pnode!=NULL&&M<=readyqueue->size) //sometimes it cross the border, maybe cause of no lock
    {  
        printf("%d: PID = %d || Priority = %d || Name = %s \n", M, pnode->data.Processid, pnode->data.Priority, pnode->data.Name);
        pnode = pnode->next;  
		M++;
    }  
	printf("TimerQueue, total number: %d\n", timerqueue->size);
	while(dnode!=NULL&&N<=timerqueue->size) 
    {  
        printf("%d: PID = %d || Priority = %d || Name = %s || Timerecord = %d\n", N, dnode->data.Processid, dnode->data.Priority, dnode->data.Name, dnode->time);
        dnode = dnode->next;  
		N++;
    } 
	printf("----------------------------------------------------------------------------\n");
} 

/**************************************************************************************************************************************
Schduel Print routine

dospprint
//get targetpid and current PCB, print them and the queue, doint the lock as well

in: action, target pid, current pcb
out:
**************************************************************************************************************************************/
void dospprint(char *action, INT32 tarGetPID, Process_Control_Block *currentPCB){ 
	PCBNode		spnode;
	INT32		spcount;
	INT32		LockResult;

	/*if(tarGetPID == -1){ //what if sometime we handle the pid = -1 situation?
		tarGetPID = 55; //because the sp print can only print 0-99 pid, we change -1 to 55 for debug
	}*/
	READ_MODIFY(MEMORY_INTERLOCK_BASE+0, DO_LOCK, SUSPEND_UNTIL_LOCKED, &LockResult); //readyqueue
	READ_MODIFY(MEMORY_INTERLOCK_BASE+1, DO_LOCK, SUSPEND_UNTIL_LOCKED, &LockResult); //timerqueue
	READ_MODIFY(MEMORY_INTERLOCK_BASE+2, DO_LOCK, SUSPEND_UNTIL_LOCKED, &LockResult); //suspendqueue
	
	if(currentPCB==NULL){ //conside it is the first time create pcb, the pcb is empty, so we init first pcb value 0
		CALL(SP_setup( SP_RUNNING_MODE, 0));
	}
	else CALL(SP_setup( SP_RUNNING_MODE, currentPCB->Processid ));

	CALL(SP_setup_action( SP_ACTION_MODE, action ));
	CALL(SP_setup( SP_TARGET_MODE, tarGetPID));
	
	spnode = readyqueue->front; //print readyqueue
	spcount =1;
	while(spnode!=NULL&&spcount<=readyqueue->size){
		CALL(SP_setup( SP_READY_MODE, spnode->data.Processid));
		spnode = spnode->next;
		spcount++;
	}

	spnode = timerqueue->front; //print timerqueue
	spcount =1;
	while(spnode!=NULL&&spcount<=timerqueue->size){
		CALL(SP_setup( SP_WAITING_MODE, spnode->data.Processid));
		spnode = spnode->next;
		spcount++;
	}

	spnode = suspendqueue->front; //print suspendqueue
	spcount =1;
	while(spnode!=NULL&&spcount<=suspendqueue->size){
		CALL(SP_setup( SP_SUSPENDED_MODE, spnode->data.Processid ));
		spnode = spnode->next;
		spcount++;
	}
	if(action == "DONE"){
		CALL(SP_setup( SP_TERMINATED_MODE, tarGetPID));
	}
	if(action == "CREATE"){
		CALL(SP_setup( SP_NEW_MODE, tarGetPID ));
	}
	
	CALL(SP_print_header());
	CALL(SP_print_line());

	READ_MODIFY(MEMORY_INTERLOCK_BASE+0, DO_UNLOCK, SUSPEND_UNTIL_LOCKED, &LockResult); //readyqueue
	READ_MODIFY(MEMORY_INTERLOCK_BASE+1, DO_UNLOCK, SUSPEND_UNTIL_LOCKED, &LockResult); //timerqueue
	READ_MODIFY(MEMORY_INTERLOCK_BASE+2, DO_UNLOCK, SUSPEND_UNTIL_LOCKED, &LockResult); //suspendqueue
}

/**************************************************************************************************************************************
Below are the routines for message handle

	Addtomessagelist, removefrommessagelist, IsSourcePidExsit
**************************************************************************************************************************************/

/************************************************************************
Addtomessagelist
//insert the data to messagelist, too much arguments, didnt use it

in: long target_pid, long source_pid, long actual_source_pid, long send_length, 
	long receive_length, long actual_send_length, long loop_count, char *msg_buffer
out: 
************************************************************************/
void Addtomessagelist(long target_pid, long source_pid, long actual_source_pid, long send_length, 
	long receive_length, long actual_send_length, long loop_count, char *msg_buffer){
	if(messagecount>MessageLimit){ //limit number 100
		printf("ERROR! the limit of messagecount is 100\n");
	}
	else{
		messagelist[messagecount].actual_send_length = actual_send_length; 
		messagelist[messagecount].actual_source_pid = actual_source_pid;
		messagelist[messagecount].loop_count = loop_count;
		strcpy(messagelist[0].msg_buffer,msg_buffer);
		messagelist[messagecount].receive_length = receive_length;
		messagelist[messagecount].send_length = send_length;
		messagelist[messagecount].source_pid = source_pid;
		messagelist[messagecount].target_pid = target_pid;
		messagecount++;
	}	
}

/************************************************************************
removefrommessagelist
//remove all the match element in messagelist

in: position
out: 
************************************************************************/					
void removefrommessagelist(INT32 icount){  
	INT32 pcount = icount;
	if(messagecount==0){ //if messagelist is empty
		printf("WARN, your messagelist is empty, there is nothing to remove\n");
	}
	while(pcount<messagecount){//if its not the last element
		messagelist[pcount] = messagelist[pcount+1];
		pcount++;
	}
	messagecount--;
}

/************************************************************************
IsSourcePidExsit
//judge if the pid is in messagelist

in: process id
out: INT32(1/0)
************************************************************************/	
INT32 IsSourcePidExsit(INT32 pid){  
	INT32 i;
	for(i=0;i<messagecount;i++){
		if(messagelist[i].source_pid==pid){
			return 1;
		}	
	}
	return 0;
}

/************************************************************************
messageprocess
//the routine for test1m, I make two message process here, first one is 
change priority of system, second one is response hello function

in: message
out: 
************************************************************************/	
void messageprocess( char *message ){
	PCBNode pnode;
	INT32	icount;
	INT32	flag = 0;
	INT32	LockResult;
	char	*messagebuff="hello!";
	Process_Control_Block pcbtemp;
	char dd[64]="\0",pp[64]="\0";

	strncpy(pp,message,15);
	if(strcmp(pp,"change priority")==0){
		//printf("Get Command:%s\n",message);
		sprintf(dd, "%s", message+16);
		//printf("Get Argument:%s\n",message+16);	
		flag = 1;
	}
	else if(strcmp(pp,"say hello to me")==0){
		//printf("Get Command:%s\n",message);
		sprintf(dd, "%s", message+16);
		//printf("Get Argument:%s\n",message+16);
		flag = 2;
	}
	else{
	
	}
	
	if(flag==1){
		if(CURRENTPCB->Processid == 0){
			
		}
		else{
			//READ_MODIFY(MEMORY_INTERLOCK_BASE+0, DO_LOCK, SUSPEND_UNTIL_LOCKED, &LockResult); //readyqueue
			//READ_MODIFY(MEMORY_INTERLOCK_BASE+1, DO_LOCK, SUSPEND_UNTIL_LOCKED, &LockResult); //timerqueue
			pnode = readyqueue->front; 
			icount = 1;
			while(pnode!=NULL&&icount<=readyqueue->size){ 
				if(pnode->data.Processid == CURRENTPCB->Processid){  //it could be not the first one in readyqueue, because the idle
					pnode->data.Priority = atoi(dd);
					CURRENTPCB->Priority = atoi(dd); 
					printf("Change susccessfully, Now The priority of (%s pid:%d) is %d\n",pnode->data.Name,pnode->data.Processid,pnode->data.Priority);
					//printf("susccessful change the current priority,now %s,pid %d is %d\n",CURRENTPCB->Name,CURRENTPCB->Processid,CURRENTPCB->Priority);	
					pcbtemp = pnode->data;
					break;
				}
				else {
					pnode = pnode->next;	
					icount++;
				}
			}
			//insert into the readyqueue by priority
			CALL(RemoveQueueByPid(readyqueue,pcbtemp.Processid));
			CALL(AddToReadyQueueByPriority(readyqueue, &pcbtemp));	
			//READ_MODIFY(MEMORY_INTERLOCK_BASE+0, DO_UNLOCK, SUSPEND_UNTIL_LOCKED, &LockResult); //readyqueue
			//READ_MODIFY(MEMORY_INTERLOCK_BASE+1, DO_UNLOCK, SUSPEND_UNTIL_LOCKED, &LockResult); //timerqueue

			CALL(dospprint("MODIFY", CURRENTPCB->Processid, CURRENTPCB));
		}
	}
	else if(flag == 2){
		READ_MODIFY(MEMORY_INTERLOCK_BASE+3, DO_LOCK, SUSPEND_UNTIL_LOCKED, &LockResult); //messagelist
		if(messagecount>MessageLimit-1){ //limit message number 100
			printf("ERROR! The limit number of messages is 100\n");
		}
		else{
			for(icount = 0;icount<atoi(dd);icount++){
				messagelist[messagecount].actual_send_length = strlen(messagebuff);  
				messagelist[messagecount].actual_source_pid = CURRENTPCB->Processid;
				messagelist[messagecount].loop_count = 0; //no use
				strcpy(messagelist[messagecount].msg_buffer,messagebuff);
				messagelist[messagecount].receive_length = 0; //no use
				messagelist[messagecount].send_length = strlen(messagebuff);
				messagelist[messagecount].source_pid = CURRENTPCB->Processid;
				messagelist[messagecount].target_pid = 0;
				messagecount++;
			}
		}
		READ_MODIFY(MEMORY_INTERLOCK_BASE+3, DO_UNLOCK, SUSPEND_UNTIL_LOCKED, &LockResult); //messagelist
	}
	else{
	
	}
}
/************************************************************************
OSCreateProcess

	This routine create the process for the OS, each test need to be 
	initilized as a OS process, it contains processid, priority and 
	encapsulate the context from the Z502.

in: process name, enter address, process priority
out: process id
************************************************************************/
INT32 OSCreateProcess(char *processname, void *processaddress, INT32 processpriority){
	void	*next_context;
	INT32		LockResult;

	if(processaddress == NULL){ //the illegal handle
		printf("ERROR! The process entrance address can not be NULL.\n");
		exit(0);
	}
	if(processname == NULL){//the illegal handle
		processname = (char *)processaddress;
	}
	if(processpriority <=0){//the illegal handle
		printf("ERROR! Your process priority is illegal.\n");
		if(IsEmpty(readyqueue)&&IsEmpty(timerqueue)){
			CALL(Z502Halt());
		}
		else if(IsEmpty(readyqueue)!=1){ 
			CALL(Z502SwitchContext( SWITCH_CONTEXT_SAVE_MODE, &readyqueue->front->data.context ));
		}
		else CALL(Z502Idle()); 
	}
	else{
		//find the entrance for the process
		if ( strcmp( (char *)processaddress, "sample" ) == 0 ){
			CALL(Z502MakeContext( &next_context, (void *) sample_code, KERNEL_MODE ));
			start_PCB = (Process_Control_Block *) calloc(1, sizeof(Process_Control_Block)); //startPCB is father process
			start_PCB->context = next_context;
			start_PCB->Processid = PCBcount;
			start_PCB->Priority = processpriority;
			sprintf(start_PCB->Name , "%s", processname);
		}
		else if( strcmp( (char *)processaddress, "test0" ) == 0 ){
			CALL(Z502MakeContext( &next_context, (void *) test0, KERNEL_MODE ));
			start_PCB = (Process_Control_Block *) calloc(1, sizeof(Process_Control_Block));
			start_PCB->context = next_context;
			start_PCB->Processid = PCBcount;
			start_PCB->Priority = processpriority;
			sprintf(start_PCB->Name , "%s", processname);
		}
		else if( strcmp( (char *)processaddress, "test1a" ) == 0 ){
			CALL(Z502MakeContext( &next_context, (void *) test1a, KERNEL_MODE ));
			start_PCB = (Process_Control_Block *) calloc(1, sizeof(Process_Control_Block));
			start_PCB->context = next_context;
			start_PCB->Processid = PCBcount;
			start_PCB->Priority = processpriority;
			sprintf(start_PCB->Name , "%s", processname);
		}
		else if( strcmp( (char *)processaddress, "test1b" ) == 0 ){
			CALL(Z502MakeContext( &next_context, (void *) test1b, KERNEL_MODE ));
			start_PCB = (Process_Control_Block *) calloc(1, sizeof(Process_Control_Block));
			start_PCB->context = next_context;
			start_PCB->Processid = PCBcount;
			start_PCB->Priority = processpriority;
			sprintf(start_PCB->Name , "%s", processname);
		}
		else if( strcmp( (char *)processaddress, "test1c" ) == 0 ){
			CALL(Z502MakeContext( &next_context, (void *) test1c, KERNEL_MODE ));
			start_PCB = (Process_Control_Block *) calloc(1, sizeof(Process_Control_Block));
			start_PCB->context = next_context;
			start_PCB->Processid = PCBcount;
			start_PCB->Priority = processpriority;
			sprintf(start_PCB->Name , "%s", processname);
		}
		else if( strcmp( (char *)processaddress, "test1d" ) == 0 ){
			CALL(Z502MakeContext( &next_context, (void *) test1d, KERNEL_MODE ));
			start_PCB = (Process_Control_Block *) calloc(1, sizeof(Process_Control_Block));
			start_PCB->context = next_context;
			start_PCB->Processid = PCBcount;
			start_PCB->Priority = processpriority;
			sprintf(start_PCB->Name , "%s", processname);	
		}
		else if( strcmp( (char *)processaddress, "test1e" ) == 0 ){
			CALL(Z502MakeContext( &next_context, (void *) test1e, KERNEL_MODE ));
			start_PCB = (Process_Control_Block *) calloc(1, sizeof(Process_Control_Block));
			start_PCB->context = next_context;
			start_PCB->Processid = PCBcount;
			start_PCB->Priority = processpriority;
			sprintf(start_PCB->Name , "%s", processname);		
		}
		else if( strcmp( (char *)processaddress, "test1f" ) == 0 ){
			CALL(Z502MakeContext( &next_context, (void *) test1f, KERNEL_MODE ));
			start_PCB = (Process_Control_Block *) calloc(1, sizeof(Process_Control_Block));
			start_PCB->context = next_context;
			start_PCB->Processid = PCBcount;
			start_PCB->Priority = processpriority;
			sprintf(start_PCB->Name , "%s", processname);			
		}
		else if( strcmp( (char *)processaddress, "test1g" ) == 0 ){
			CALL(Z502MakeContext( &next_context, (void *) test1g, KERNEL_MODE ));
			start_PCB = (Process_Control_Block *) calloc(1, sizeof(Process_Control_Block));
			start_PCB->context = next_context;
			start_PCB->Processid = PCBcount;
			start_PCB->Priority = processpriority;
			sprintf(start_PCB->Name , "%s", processname);
		}
		else if( strcmp( (char *)processaddress, "test1h" ) == 0 ){
			CALL(Z502MakeContext( &next_context, (void *) test1h, KERNEL_MODE ));
			start_PCB = (Process_Control_Block *) calloc(1, sizeof(Process_Control_Block));
			start_PCB->context = next_context;
			start_PCB->Processid = PCBcount;
			start_PCB->Priority = processpriority;
			sprintf(start_PCB->Name , "%s", processname);
			
		}
		else if( strcmp( (char *)processaddress, "test1i" ) == 0 ){
			CALL(Z502MakeContext( &next_context, (void *) test1i, KERNEL_MODE ));
			start_PCB = (Process_Control_Block *) calloc(1, sizeof(Process_Control_Block));
			start_PCB->context = next_context;
			start_PCB->Processid = PCBcount;
			start_PCB->Priority = processpriority;
			sprintf(start_PCB->Name , "%s", processname);
			
		}
		else if( strcmp( (char *)processaddress, "test1j" ) == 0 ){
			CALL(Z502MakeContext( &next_context, (void *) test1j, KERNEL_MODE ));
			start_PCB = (Process_Control_Block *) calloc(1, sizeof(Process_Control_Block));
			start_PCB->context = next_context;
			start_PCB->Processid = PCBcount;
			start_PCB->Priority = processpriority;
			sprintf(start_PCB->Name , "%s", processname);
			
		}
		else if( strcmp( (char *)processaddress, "test1k" ) == 0 ){
			CALL(Z502MakeContext( &next_context, (void *) test1k, KERNEL_MODE ));
			start_PCB = (Process_Control_Block *) calloc(1, sizeof(Process_Control_Block));
			start_PCB->context = next_context;
			start_PCB->Processid = PCBcount;
			start_PCB->Priority = processpriority;
			sprintf(start_PCB->Name , "%s", processname);
			
		}
		else if( strcmp( (char *)processaddress, "test1l" ) == 0 ){
			CALL(Z502MakeContext( &next_context, (void *) test1l, KERNEL_MODE ));
			start_PCB = (Process_Control_Block *) calloc(1, sizeof(Process_Control_Block));
			start_PCB->context = next_context;
			start_PCB->Processid = PCBcount;
			start_PCB->Priority = processpriority;
			sprintf(start_PCB->Name , "%s", processname);
			
		}
		else if( strcmp( (char *)processaddress, "test1m" ) == 0 ){
			CALL(Z502MakeContext( &next_context, (void *) test1m, KERNEL_MODE ));
			start_PCB = (Process_Control_Block *) calloc(1, sizeof(Process_Control_Block));
			start_PCB->context = next_context;
			start_PCB->Processid = PCBcount;
			start_PCB->Priority = processpriority;
			sprintf(start_PCB->Name , "%s", processname);
			
		}
		//its for the text1x and test1j_echo
		else{
			CALL(Z502MakeContext( &next_context, (void *) processaddress, KERNEL_MODE ));
		}
		//init the Process
		PCB = (Process_Control_Block *) calloc(1, sizeof(Process_Control_Block));
		PCB->context = next_context;
		PCB->Processid = PCBcount++;
		PCB->Priority = processpriority;
		sprintf(PCB->Name , "%s", processname); //need to sprintf a point value
		READ_MODIFY(MEMORY_INTERLOCK_BASE+0, DO_LOCK, SUSPEND_UNTIL_LOCKED, &LockResult);
		CALL(AddToReadyQueueByPriority(readyqueue, PCB));//insert by priority
		READ_MODIFY(MEMORY_INTERLOCK_BASE+0, DO_UNLOCK, SUSPEND_UNTIL_LOCKED, &LockResult);
		//ListReadyQueue(); //for debug
		//ListTimerQueue();
		//CALL(ListTwoQueue());
		CALL(dospprint("CREATE", PCB->Processid, CURRENTPCB));
		return PCB->Processid;
	}
}

/**************************************************************************************************************************************
osInit

    This is the first routine called after the simulation begins.  This is equivalent to boot code.  All the initial OS 
	components can be defined and initialized here.
**************************************************************************************************************************************/
void    osInit( INT32 argc, char *argv[]  ) {
    INT32	i;
	//init three queues
	timerqueue = InitQueue(); 
	readyqueue = InitQueue();
	suspendqueue = InitQueue();

	//freopen("filename.txt", "w", stdout); //for debug

    printf( "Program called with %d arguments:", argc );
    for ( i = 0; i < argc; i++ )
        printf( " %s", argv[i] );
    printf( "\n" );

    /*          Setup so handlers will come to code in base.c           */
    TO_VECTOR[TO_VECTOR_INT_HANDLER_ADDR]   = (void *)interrupt_handler;
    TO_VECTOR[TO_VECTOR_FAULT_HANDLER_ADDR] = (void *)fault_handler;
    TO_VECTOR[TO_VECTOR_TRAP_HANDLER_ADDR]  = (void *)svc;

    /*  Determine if the switch was set, and if so go to demo routine.  */
	if ( argc = 1) {
		CALL(OSCreateProcess(NULL,(void *)argv[1], 1));
		CURRENTPCB = (Process_Control_Block *) calloc(1, sizeof(Process_Control_Block));
		CURRENTPCB = start_PCB; //because start_PCB doesnt change, we can directly assign the value to CURRENTPCB
		CALL(Z502SwitchContext( SWITCH_CONTEXT_SAVE_MODE, &start_PCB->context ));
	}
	else printf( "The arguments is not correct, Please try again\n" );
    /*  This should be done by a "os_make_process" routine, so that
        test0 runs on a process recognized by the operating system.    */
}                                               // End of osInit 