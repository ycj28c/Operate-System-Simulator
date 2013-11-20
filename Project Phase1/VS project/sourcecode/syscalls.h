/*********************************************************************
 syscalls.h

 This include file is used by only the OS502.

 Revision History:
 1.0 August 1990:        Initial release
 1.1 Jan 1991:           Make system calls portable
 by using union of pointer
 and long.  Add incls for
 scheduler_printer.
 1.2 Dec 1991;           Allow interrupts to occur in user code and in CALL
                         statements.
 1.5 Aug 1993;           Add READ_MODIFY & DEFINE_SHARED_AREA support.
 2.0 Jan 2000;           Small changes
 2.1 May 2001;           Fix STEP macro.  DISK macros.
 2.2 Jul 2002;           Make code appropriate for undergrads.
 2.3 Aug 2004;           Modify Memory defines to work in kernel
 3.1 Aug 2004:           hardware interrupt runs on separate thread
 3.11 Aug 2004:          Support for OS level locking
 3.30 July 2006:         Modify POP_THE_STACK to apply to base only
 *********************************************************************/
#ifndef  SYSCALLS_H
#define  SYSCALLS_H

#include        "stdio.h"

/* Definition of System Call numbers                        */

#define         SYSNUM_MEM_READ                        0
#define         SYSNUM_MEM_WRITE                       1
#define         SYSNUM_READ_MODIFY                     2
#define         SYSNUM_GET_TIME_OF_DAY                 3
#define         SYSNUM_SLEEP                           4
#define         SYSNUM_GET_PROCESS_ID                  5
#define         SYSNUM_CREATE_PROCESS                  6
#define         SYSNUM_TERMINATE_PROCESS               7
#define         SYSNUM_SUSPEND_PROCESS                 8
#define         SYSNUM_RESUME_PROCESS                  9
#define         SYSNUM_CHANGE_PRIORITY                 10
#define         SYSNUM_SEND_MESSAGE                    11
#define         SYSNUM_RECEIVE_MESSAGE                 12
#define         SYSNUM_DISK_READ                       13
#define         SYSNUM_DISK_WRITE                      14
#define         SYSNUM_DEFINE_SHARED_AREA              15

// This structure defines the format used for all system calls.
// For each call, the structure is filled in and then its address
// is used as an argument to call SVC.

#define         MAX_NUMBER_ARGUMENTS                   8
typedef struct    {
    int  NumberOfArguments;          // This includes the System Call Number
    int  SystemCallNumber;
    long *Argument[MAX_NUMBER_ARGUMENTS];
} SYSTEM_CALL_DATA;


extern void ChargeTimeAndCheckEvents(INT32);
extern int BaseThread();

#ifndef COST_OF_CALL
#define         COST_OF_CALL                            2L
#endif
#ifndef COST_OF_SOFTWARE_TRAP
#define         COST_OF_SOFTWARE_TRAP                   5L
#endif
#ifndef         COST_OF_CPU_INSTRUCTION
#define         COST_OF_CPU_INSTRUCTION                 1L
#endif

#define         CALL( fff )       {                             \
                ChargeTimeAndCheckEvents( COST_OF_CALL );   \
                fff;                                            \
                }                                               \


//      Macros used to make the test programs more readable

/*
 * In C, we can do a #define.  This produces a macro.  Then every
 * place the macro name is used, it expands to include all the code
 * defined here.  It greatly improves the readability of the code.
 */


#define    MEM_READ( arg1, arg2 )    Z502MemoryRead( arg1, (INT32 *)arg2 )

#define    MEM_WRITE( arg1, arg2 )   Z502MemoryWrite( arg1, (INT32 *)arg2 )

#define    READ_MODIFY( arg1, arg2, arg3, arg4 )                               \
	         Z502MemoryReadModify( arg1, arg2, arg3, arg4 )



#define         GET_TIME_OF_DAY( arg1 )      {                                 \
                SYSTEM_CALL_DATA *SystemCallData =                             \
                     (SYSTEM_CALL_DATA *)calloc(1, sizeof (SYSTEM_CALL_DATA)); \
                SystemCallData->NumberOfArguments = 2;                         \
                SystemCallData->SystemCallNumber = SYSNUM_GET_TIME_OF_DAY;     \
                SystemCallData->Argument[0] = (long *)arg1;                    \
                ChargeTimeAndCheckEvents( COST_OF_SOFTWARE_TRAP );             \
                Z502_MODE = KERNEL_MODE;                                       \
                svc(SystemCallData);                                           \
                Z502_MODE = USER_MODE;                                         \
                free(SystemCallData);                                          \
                }                                                              \

#define         SLEEP( arg1 )                {                                 \
                SYSTEM_CALL_DATA *SystemCallData =                             \
                     (SYSTEM_CALL_DATA *)calloc(1, sizeof (SYSTEM_CALL_DATA)); \
                SystemCallData->NumberOfArguments = 2;                         \
                SystemCallData->SystemCallNumber = SYSNUM_SLEEP;               \
                SystemCallData->Argument[0] = (long *)arg1;                    \
                ChargeTimeAndCheckEvents( COST_OF_SOFTWARE_TRAP );             \
                Z502_MODE = KERNEL_MODE;                                       \
                svc(SystemCallData);                                           \
                Z502_MODE = USER_MODE;                                         \
                free(SystemCallData);                                          \
                }                                                              \

#define         CREATE_PROCESS( arg1, arg2, arg3, arg4, arg5 )   {             \
                SYSTEM_CALL_DATA *SystemCallData =                             \
                     (SYSTEM_CALL_DATA *)calloc(1, sizeof (SYSTEM_CALL_DATA)); \
                SystemCallData->NumberOfArguments = 6;                         \
                SystemCallData->SystemCallNumber = SYSNUM_CREATE_PROCESS;      \
                SystemCallData->Argument[0] = (long *)arg1;                    \
                SystemCallData->Argument[1] = (long *)arg2;                    \
                SystemCallData->Argument[2] = (long *)arg3;                    \
                SystemCallData->Argument[3] = (long *)arg4;                    \
                SystemCallData->Argument[4] = (long *)arg5;                    \
                ChargeTimeAndCheckEvents( COST_OF_SOFTWARE_TRAP );             \
                Z502_MODE = KERNEL_MODE;                                       \
                svc(SystemCallData);                                           \
                Z502_MODE = USER_MODE;                                         \
                free(SystemCallData);                                          \
                }                                                              \


#define         GET_PROCESS_ID( arg1, arg2, arg3)   {                          \
                SYSTEM_CALL_DATA *SystemCallData =                             \
                     (SYSTEM_CALL_DATA *)calloc(1, sizeof (SYSTEM_CALL_DATA)); \
                SystemCallData->NumberOfArguments = 4;                         \
                SystemCallData->SystemCallNumber = SYSNUM_GET_PROCESS_ID;      \
                SystemCallData->Argument[0] = (long *)arg1;                    \
                SystemCallData->Argument[1] = (long *)arg2;                    \
                SystemCallData->Argument[2] = (long *)arg3;                    \
                ChargeTimeAndCheckEvents( COST_OF_SOFTWARE_TRAP );             \
                Z502_MODE = KERNEL_MODE;                                       \
                svc(SystemCallData);                                           \
                Z502_MODE = USER_MODE;                                         \
                free(SystemCallData);                                          \
                }                                                              \


#define         TERMINATE_PROCESS( arg1, arg2 )      {                        \
                SYSTEM_CALL_DATA *SystemCallData =                            \
                     (SYSTEM_CALL_DATA *)calloc(1, sizeof(SYSTEM_CALL_DATA)); \
                SystemCallData->NumberOfArguments = 3;                        \
                SystemCallData->SystemCallNumber = SYSNUM_TERMINATE_PROCESS;  \
                SystemCallData->Argument[0] = (long *)arg1;                   \
                SystemCallData->Argument[1] = (long *)arg2;                   \
                ChargeTimeAndCheckEvents( COST_OF_SOFTWARE_TRAP );            \
                Z502_MODE = KERNEL_MODE;                                      \
                svc(SystemCallData);                                          \
                Z502_MODE = USER_MODE;                                        \
                free(SystemCallData);                                         \
                }                                                             \


#define         SUSPEND_PROCESS( arg1, arg2 )      {                          \
                SYSTEM_CALL_DATA *SystemCallData =                            \
                     (SYSTEM_CALL_DATA *)calloc(1, sizeof(SYSTEM_CALL_DATA)); \
                SystemCallData->NumberOfArguments = 3;                        \
                SystemCallData->SystemCallNumber = SYSNUM_SUSPEND_PROCESS;    \
                SystemCallData->Argument[0] = (long *)arg1;                   \
                SystemCallData->Argument[1] = (long *)arg2;                   \
                ChargeTimeAndCheckEvents( COST_OF_SOFTWARE_TRAP );            \
                Z502_MODE = KERNEL_MODE;                                      \
                svc(SystemCallData);                                          \
                Z502_MODE = USER_MODE;                                        \
                free(SystemCallData);                                         \
                }                                                             \

#define         RESUME_PROCESS( arg1, arg2 )      {                           \
                SYSTEM_CALL_DATA *SystemCallData =                            \
                     (SYSTEM_CALL_DATA *)calloc(1, sizeof(SYSTEM_CALL_DATA)); \
                SystemCallData->NumberOfArguments = 3;                        \
                SystemCallData->SystemCallNumber = SYSNUM_RESUME_PROCESS;     \
                SystemCallData->Argument[0] = (long *)arg1;                   \
                SystemCallData->Argument[1] = (long *)arg2;                   \
                ChargeTimeAndCheckEvents( COST_OF_SOFTWARE_TRAP );            \
                Z502_MODE = KERNEL_MODE;                                      \
                svc(SystemCallData);                                          \
                Z502_MODE = USER_MODE;                                        \
                free(SystemCallData);                                         \
                }                                                             \


#define         CHANGE_PRIORITY( arg1, arg2, arg3)   {                         \
                SYSTEM_CALL_DATA *SystemCallData =                             \
                     (SYSTEM_CALL_DATA *)calloc(1, sizeof (SYSTEM_CALL_DATA)); \
                SystemCallData->NumberOfArguments = 4;                         \
                SystemCallData->SystemCallNumber = SYSNUM_CHANGE_PRIORITY;     \
                SystemCallData->Argument[0] = (long *)arg1;                    \
                SystemCallData->Argument[1] = (long *)arg2;                    \
                SystemCallData->Argument[2] = (long *)arg3;                    \
                ChargeTimeAndCheckEvents( COST_OF_SOFTWARE_TRAP );             \
                Z502_MODE = KERNEL_MODE;                                       \
                svc(SystemCallData);                                           \
                Z502_MODE = USER_MODE;                                         \
                free(SystemCallData);                                          \
                }                                                              \



#define         SEND_MESSAGE( arg1, arg2, arg3, arg4 )   {                     \
                SYSTEM_CALL_DATA *SystemCallData =                             \
                     (SYSTEM_CALL_DATA *)calloc(1, sizeof (SYSTEM_CALL_DATA)); \
                SystemCallData->NumberOfArguments = 5;                         \
                SystemCallData->SystemCallNumber = SYSNUM_SEND_MESSAGE;        \
                SystemCallData->Argument[0] = (long *)arg1;                    \
                SystemCallData->Argument[1] = (long *)arg2;                    \
                SystemCallData->Argument[2] = (long *)arg3;                    \
                SystemCallData->Argument[3] = (long *)arg4;                    \
                ChargeTimeAndCheckEvents( COST_OF_SOFTWARE_TRAP );             \
                Z502_MODE = KERNEL_MODE;                                       \
                svc(SystemCallData);                                           \
                Z502_MODE = USER_MODE;                                         \
                free(SystemCallData);                                          \
                }                                                              \


#define         RECEIVE_MESSAGE( arg1, arg2, arg3, arg4, arg5, arg6 )   {      \
                SYSTEM_CALL_DATA *SystemCallData =                             \
                     (SYSTEM_CALL_DATA *)calloc(1, sizeof (SYSTEM_CALL_DATA)); \
                SystemCallData->NumberOfArguments = 7;                         \
                SystemCallData->SystemCallNumber = SYSNUM_RECEIVE_MESSAGE;     \
                SystemCallData->Argument[0] = (long *)arg1;                    \
                SystemCallData->Argument[1] = (long *)arg2;                    \
                SystemCallData->Argument[2] = (long *)arg3;                    \
                SystemCallData->Argument[3] = (long *)arg4;                    \
                SystemCallData->Argument[4] = (long *)arg5;                    \
                SystemCallData->Argument[5] = (long *)arg6;                    \
                ChargeTimeAndCheckEvents( COST_OF_SOFTWARE_TRAP );             \
                Z502_MODE = KERNEL_MODE;                                       \
                svc(SystemCallData);                                           \
                Z502_MODE = USER_MODE;                                         \
                free(SystemCallData);                                          \
                }                                                              \


#define         DISK_READ( arg1, arg2, arg3)   {                               \
                SYSTEM_CALL_DATA *SystemCallData =                             \
                     (SYSTEM_CALL_DATA *)calloc(1, sizeof (SYSTEM_CALL_DATA)); \
                SystemCallData->NumberOfArguments = 4;                         \
                SystemCallData->SystemCallNumber = SYSNUM_DISK_READ;           \
                SystemCallData->Argument[0] = (long *)arg1;                    \
                SystemCallData->Argument[1] = (long *)arg2;                    \
                SystemCallData->Argument[2] = (long *)arg3;                    \
                ChargeTimeAndCheckEvents( COST_OF_SOFTWARE_TRAP );             \
                Z502_MODE = KERNEL_MODE;                                       \
                svc(SystemCallData);                                           \
                Z502_MODE = USER_MODE;                                         \
                free(SystemCallData);                                          \
                }                                                              \


#define         DISK_WRITE( arg1, arg2, arg3)   {                              \
                SYSTEM_CALL_DATA *SystemCallData =                             \
                     (SYSTEM_CALL_DATA *)calloc(1, sizeof (SYSTEM_CALL_DATA)); \
                SystemCallData->NumberOfArguments = 4;                         \
                SystemCallData->SystemCallNumber = SYSNUM_DISK_WRITE;          \
                SystemCallData->Argument[0] = (long *)arg1;                    \
                SystemCallData->Argument[1] = (long *)arg2;                    \
                SystemCallData->Argument[2] = (long *)arg3;                    \
                ChargeTimeAndCheckEvents( COST_OF_SOFTWARE_TRAP );             \
                Z502_MODE = KERNEL_MODE;                                       \
                svc(SystemCallData);                                           \
                Z502_MODE = USER_MODE;                                         \
                free(SystemCallData);                                          \
                }                                                              \


#define         DEFINE_SHARED_AREA( arg1, arg2, arg3, arg4, arg5)   {          \
                SYSTEM_CALL_DATA *SystemCallData =                             \
                     (SYSTEM_CALL_DATA *)calloc(1, sizeof (SYSTEM_CALL_DATA)); \
                SystemCallData->NumberOfArguments = 6;                         \
                SystemCallData->SystemCallNumber = SYSNUM_DEFINE_SHARED_AREA;  \
                SystemCallData->Argument[0] = (long *)arg1;                    \
                SystemCallData->Argument[1] = (long *)arg2;                    \
                SystemCallData->Argument[2] = (long *)arg3;                    \
                SystemCallData->Argument[3] = (long *)arg4;                    \
                SystemCallData->Argument[4] = (long *)arg5;                    \
                ChargeTimeAndCheckEvents( COST_OF_SOFTWARE_TRAP );             \
                Z502_MODE = KERNEL_MODE;                                       \
                svc(SystemCallData);                                           \
                Z502_MODE = USER_MODE;                                         \
                free(SystemCallData);                                          \
                }                                                              \


/*      This section includes items needed in the scheduler printer.
 It's also useful for those routines that want to communicate
 with the scheduler printer.                                       */

#define         SP_FILE_MODE            (INT16)0
#define         SP_TIME_MODE            (INT16)1
#define         SP_ACTION_MODE          (INT16)2
#define         SP_TARGET_MODE          (INT16)3
#define         SP_STATE_MODE_START     (INT16)4
#define         SP_NEW_MODE             (INT16)4
#define         SP_RUNNING_MODE         (INT16)5
#define         SP_READY_MODE           (INT16)6
#define         SP_WAITING_MODE         (INT16)7
#define         SP_SUSPENDED_MODE       (INT16)8
#define         SP_SWAPPED_MODE         (INT16)9
#define         SP_TERMINATED_MODE      (INT16)10

#define         SP_NUMBER_OF_STATES     SP_TERMINATED_MODE-SP_NEW_MODE+1
#define         SP_MAX_NUMBER_OF_PIDS   (INT16)10
#define         SP_LENGTH_OF_ACTION     (INT16)8

/*      This string is printed out when requested as the header         */

#define         SP_HEADER_STRING        \
" Time Target Action  Run New Done       State Populations \n"

#endif
