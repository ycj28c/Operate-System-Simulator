/*********************************************************************

 z502.c

 This is the start of the code and declarations for the Z502 simulator.

 Revision History:
 1.0 August      1990  Initial coding
 1.1 December    1990: Lots of minor problems cleaned up.
 Portability attempted.
 1.4 December    1992  Lots of minor changes.
 1.5 August      1993  Lots of minor changes.
 1.6 June        1995  Significant debugging aids added.
 1.7 Sept.       1999  Minor compile issues.
 2.0 Jan         2000  A number of fixes and improvements.
 2.1 May         2001  Bug Fixes:
 Disks no longer make cylinders visible
 Hardware displays resource usage
 2.2 July        2002  Make appropriate for undergraduates.
 2.3 Dec         2002  Allow Mem calls from OS - conditional on POP_THE_STACK
 Clear the STAT_VECTOR at the end of SoftwareTrap
 3.0 August      2004: Modified to support memory mapped IO
 3.1 August      2004: hardware interrupt runs on separate thread
 3.11 August     2004: Support for OS level locking
 3.12 Sept.      2004: Make threads schedule correctly
 3.14 November   2004: Impliment DO_DEVICE_DEBUG in meaningful way.
 3.15 November   2004: More DO_DEVICE_DEBUG to handle disks
 3.20 May        2005: Get threads working on windows
 3.26 October    2005: Make the hardware thread safe.
 3.30 July       2006: Threading works on multiprocessor
 3.40 July       2008: Minor improvements
 3.50 August     2009: Fixes for 64 bit machines and multiprocessor
 3.51 Sept.      2011: Fixed locking issue due to MemoryCommon - Linux
 3.52 Sept.      2011: Fixed locking issue due to MemoryCommon - Linux
 Release the lock when leaving MemoryCommon
 3.60  August    2012: Used student supplied code to add support
 for Mac machines
 4.00  May       2013: Major revision to the way the simulator works.
                       It now runs off of multiple threads, and those
                       threads start in the test code avoiding many
                       many hacks.
 4.02 December   2013: STAT_VECTOR not thread safe.  Defined a method that
                       uses thread info to keep things sorted out.
 4.03 December   2013: Store Z502_MODE on context save
 ************************************************************************/

/************************************************************************

 GLOBAL VARIABLES:  These declarations are visible to both the
 Z502 simulator and the OS502 if the OS declares them
 extern.

 ************************************************************************/

#define                   HARDWARE_VERSION  "4.03"

// By uncommenting these, we can get traces of what's happening in
// the hardware.
// #define                 __USE_UNIX98
//#define                  DEBUG_LOCKS
//#define                  DEBUG_CONDITION
// #define                  DEBUG_USER_THREADS

#include                 "global.h"
#include                 "syscalls.h"
#include                 "z502.h"
#include                 "protos.h"
#include                 <stdio.h>
#include                 <stdlib.h>
#include                 <memory.h>
#ifdef NT
#include                 <windows.h>
#include                 <winbase.h>
#include                 <sys/types.h>
#endif

#ifdef LINUX
#include                 <pthread.h>
#include                 <unistd.h>
#include                 <asm/errno.h>
#include                 <sys/time.h>
#include                 <sys/resource.h>
#endif

#ifdef MAC
#include                 <pthread.h>
#include                 <unistd.h>
#include                 <errno.h>
#include                 <sys/time.h>
#include                 <sys/resource.h>
#endif

//  These are routines internal to the hardware, not visible to the OS
//  Prototypes that allow the OS to get to this hardware are in protos.h

void AddEventToInterruptQueue(INT32, INT16, INT16, EVENT **);
void AssociateContextWithProcess(Z502CONTEXT *Context);
void ChargeTimeAndCheckEvents(INT32);
int  CreateAThread(void *ThreadStartAddress, INT32 *data);
void CreateLock(INT32 *, char *CallingRoutine);
void CreateCondition(UINT32 *);
void CreateSectorStruct(INT16, INT16, char **);
void DequeueItemFromEventQueue(EVENT *, INT32 *);
void DoMemoryDebug(INT16, INT16);
void DoSleep(INT32 millisecs);
int  GetLock(UINT32 RequestedMutex, char *CallingRoutine);
void GetNextEventTime(INT32 *);
void GetSectorStructure(INT16, INT16, char **, INT32 *);
void GetNextOrderedEvent(INT32 *, INT16 *, INT16 *, INT32 *);
int GetMyTid();
int GetTryLock(UINT32 RequestedMutex, char *CallingRoutine);
void GoToExit(int);
void HandleWindowsError();
void HardwareClock(INT32 *);
void HardwareTimer(INT32);
void HardwareReadDisk(INT16, INT16, char *);
void HardwareWriteDisk(INT16, INT16, char *);
void HardwareInterrupt(void);
void HardwareFault(INT16, INT16);
void HardwareInternalPanic(INT32);
void MemoryCommon(INT32, char *, BOOL);
void PhysicalMemoryCommon(INT32, char *, BOOL);
void MemoryMappedIO(INT32, INT32 *, BOOL);
void PrintRingBuffer(void);
void PrintHardwareStats(void);
void PrintEventQueue();
void PrintLockDebug(int Action, char *LockCaller, int Mutex, int Return);
void PrintThreadTable(char *Explanation);
int ReleaseLock(UINT32 RequestedMutex, char* CallingRoutine);
void ResumeProcessExecution(Z502CONTEXT *Context);
int SignalCondition(UINT32 Condition, char* CallingRoutine);
void SoftwareTrap(void);
void SuspendProcessExecution(Z502CONTEXT *Context);
int WaitForCondition(UINT32 Condition, UINT32 Mutex, INT32 WaitTime,
        char * Caller);
void Z502Init();

//
//      This is Physical Memory which is used in part 2 of the project. 
//  

char MEMORY[PHYS_MEM_PGS * PGSIZE ];

//
//      Declaration of Z502 Registers                 
//      Most of these can be manipulated by the OS.
//

Z502CONTEXT *Z502_CURRENT_CONTEXT;   // What Context is running
UINT16 *Z502_PAGE_TBL_ADDR;     // Location of the page table
INT16 Z502_PAGE_TBL_LENGTH;    // Length of the page table
INT16 Z502_MODE;               // Kernel or user - hardware only

long Z502_REG1;
long Z502_REG2;
long Z502_REG3;
long Z502_REG4;
long Z502_REG5;
long Z502_REG6;
long Z502_REG7;
long Z502_REG8;
long Z502_REG9;
INT32 STAT_VECTOR[SV_DIMENSION][LARGEST_STAT_VECTOR_INDEX + 1];
void *TO_VECTOR[TO_VECTOR_TYPES ];

/*****************************************************************

 LOCAL VARIABLES:  These declarations should be visible only
 to the Z502 simulator.

 *****************************************************************/
INT16 Z502Initialized = FALSE;
UINT32 CurrentSimulationTime = 0;
INT16 event_ring_buffer_index = 0;

EVENT EventQueue;
INT32 NumberOfInterruptsStarted = 0;
INT32 NumberOfInterruptsCompleted = 0;
SECTOR sector_queue[MAX_NUMBER_OF_DISKS + 1];
DISK_STATE disk_state[MAX_NUMBER_OF_DISKS + 1];
TIMER_STATE timer_state;
HARDWARE_STATS HardwareStats;

RING_EVENT event_ring_buffer[EVENT_RING_BUFFER_SIZE];
INT32 InterlockRecord[MEMORY_INTERLOCK_SIZE];
INT32 EventLock = -1;                          // Change from UINT32 - 08/2012
INT32 InterruptLock = -1;
INT32 HardwareLock = -1;
INT32 ThreadTableLock = -1;

UINT32 InterruptCondition = 0;
int NextConditionToAllocate = 1;    // This was 0 and seemed to work
int BaseTid;
int InterruptTid;

// Contains info about all the threads created
THREAD_INFO ThreadTable[MAX_NUMBER_OF_USER_THREADS];

#ifdef   NT
HANDLE LocalEvent[100];
#endif

#if defined LINUX || defined MAC
pthread_mutex_t LocalMutex[300];
pthread_cond_t LocalCondition[100];
int NextMutexToAllocate = 0;
#endif

/*****************************************************************
 MemoryCommon

 This code simulates a memory access.  Actions include:
 o Take a page fault if any of the following occur;
 + Illegal virtual address,
 + Page table doesn't exist,
 + Address is larger than page table,
 + Page table entry exists, but page is invalid.
 o The page exists in physical memory, so get the physical address.
 Be careful since it may wrap across frame boundaries.
 o Copy data to/from caller's location.
 o Set referenced/modified bit in page table.
 o Advance time and see if an interrupt has occurred.
 *****************************************************************/

void MemoryCommon(INT32 VirtualAddress, char *data_ptr, BOOL read_or_write) {
    INT16 VirtualPageNumber;
    INT32 phys_pg;
    INT16 PhysicalAddress[4];
    INT32 page_offset;
    INT16 index;
    INT32 ptbl_bits;
    INT16 invalidity;
    BOOL page_is_valid;
    char Debug_Text[32];

    strcpy(Debug_Text, "MemoryCommon");
    GetLock(HardwareLock, Debug_Text);
    if (VirtualAddress >= Z502MEM_MAPPED_MIN) {
        MemoryMappedIO(VirtualAddress, (INT32 *) data_ptr, read_or_write);
        ReleaseLock(HardwareLock, Debug_Text);
        return;
    }
    VirtualPageNumber = (INT16) (
            (VirtualAddress >= 0) ? VirtualAddress / PGSIZE : -1);
    page_offset = VirtualAddress % PGSIZE;

    page_is_valid = FALSE;

    /*  Loop until the virtual page passes all the tests        */

    while (page_is_valid == FALSE ) {
        invalidity = 0;
        if (VirtualPageNumber >= VIRTUAL_MEM_PGS)
            invalidity = 1;
        if (VirtualPageNumber < 0)
            invalidity = 2;
        if (Z502_PAGE_TBL_ADDR == NULL )
            invalidity = 3;
        if (VirtualPageNumber >= Z502_PAGE_TBL_LENGTH)
            invalidity = 4;
        if ((invalidity == 0)
                && (Z502_PAGE_TBL_ADDR[(UINT16) VirtualPageNumber]
                        & PTBL_VALID_BIT) == 0)
            invalidity = 5;

        DoMemoryDebug(invalidity, VirtualPageNumber);
        if (invalidity > 0) {
            if (Z502_CURRENT_CONTEXT->structure_id != CONTEXT_STRUCTURE_ID) {
                printf("Z502_CURRENT_CONTEXT is invalid in MemoryCommon\n");
                printf("Something in the OS has destroyed this location.\n");
                HardwareInternalPanic(ERR_OS502_GENERATED_BUG);
            }
            Z502_CURRENT_CONTEXT->fault_in_progress = TRUE;
            // The fault handler will do it's own locking - 11/13/11
            ReleaseLock(HardwareLock, Debug_Text);
            HardwareFault(INVALID_MEMORY, VirtualPageNumber);
            // Regain the lock to protect the memory check - 11/13/11
            GetLock(HardwareLock, Debug_Text);
        } else
            page_is_valid = TRUE;
    } /* END of while         */

    phys_pg = Z502_PAGE_TBL_ADDR[VirtualPageNumber] & PTBL_PHYS_PG_NO;
    PhysicalAddress[0] = (INT16) (phys_pg * (INT32) PGSIZE + page_offset);
    PhysicalAddress[1] = PhysicalAddress[0] + 1; /* first guess */
    PhysicalAddress[2] = PhysicalAddress[0] + 2; /* first guess */
    PhysicalAddress[3] = PhysicalAddress[0] + 3; /* first guess */

    page_is_valid = FALSE;
    if (page_offset > PGSIZE - 4) /* long int wraps over page */
    {
        while (page_is_valid == FALSE ) {
            invalidity = 0;
            if (VirtualPageNumber + 1 >= VIRTUAL_MEM_PGS)
                invalidity = 6;
            if (VirtualPageNumber + 1 >= Z502_PAGE_TBL_LENGTH)
                invalidity = 7;
            if ((Z502_PAGE_TBL_ADDR[(UINT16) VirtualPageNumber + 1]
                    & PTBL_VALID_BIT) == 0)
                invalidity = 8;
            DoMemoryDebug(invalidity, (short) (VirtualPageNumber + 1));
            if (invalidity > 0) {
                if (Z502_CURRENT_CONTEXT->structure_id != CONTEXT_STRUCTURE_ID) {
                    printf("Z502_CURRENT_CONTEXT invalid in MemoryCommon\n");
                    printf("The OS has destroyed this location.\n");
                    HardwareInternalPanic(ERR_OS502_GENERATED_BUG);
                }
                Z502_CURRENT_CONTEXT->fault_in_progress = TRUE;

                HardwareFault(INVALID_MEMORY, (INT16) (VirtualPageNumber + 1));
            } else
                page_is_valid = TRUE;
        } /* End of while         */

        phys_pg = Z502_PAGE_TBL_ADDR[VirtualPageNumber + 1] & PTBL_PHYS_PG_NO;
        for (index = PGSIZE - (INT16) page_offset; index <= 3; index++)
            PhysicalAddress[index] = (INT16) ((phys_pg - 1) * (INT32) PGSIZE
                    + page_offset + (INT32) index);
    } /* End of if page       */

    if (phys_pg < 0 || phys_pg > PHYS_MEM_PGS - 1) {
        printf("The physical address is invalid in MemoryCommon\n");
        printf("Physical page = %d, Virtual Page = %d\n", phys_pg,
                VirtualPageNumber);
        HardwareInternalPanic(ERR_OS502_GENERATED_BUG);
    }
    if (Z502_CURRENT_CONTEXT->structure_id != CONTEXT_STRUCTURE_ID) {
        printf("Z502_CURRENT_CONTEXT is invalid in MemoryCommon\n");
        printf("Something in the OS has destroyed this location.\n");
        HardwareInternalPanic(ERR_OS502_GENERATED_BUG);
    }
    Z502_CURRENT_CONTEXT->fault_in_progress = FALSE;

    if (read_or_write == SYSNUM_MEM_READ) {
        data_ptr[0] = MEMORY[PhysicalAddress[0]];
        data_ptr[1] = MEMORY[PhysicalAddress[1]];
        data_ptr[2] = MEMORY[PhysicalAddress[2]];
        data_ptr[3] = MEMORY[PhysicalAddress[3]];
        ptbl_bits = PTBL_REFERENCED_BIT;
    }

    if (read_or_write == SYSNUM_MEM_WRITE) {
        MEMORY[PhysicalAddress[0]] = data_ptr[0];
        MEMORY[PhysicalAddress[1]] = data_ptr[1];
        MEMORY[PhysicalAddress[2]] = data_ptr[2];
        MEMORY[PhysicalAddress[3]] = data_ptr[3];
        ptbl_bits = PTBL_REFERENCED_BIT | PTBL_MODIFIED_BIT;
    }

    Z502_PAGE_TBL_ADDR[VirtualPageNumber] |= ptbl_bits;
    if (page_offset > PGSIZE - 4)
        Z502_PAGE_TBL_ADDR[VirtualPageNumber + 1] |= ptbl_bits;

    ChargeTimeAndCheckEvents(COST_OF_MEMORY_ACCESS);

    ReleaseLock(HardwareLock, Debug_Text);
}                      // End of MemoryCommon

/*****************************************************************
 DoMemoryDebug

 Print out details about why a page fault occurred.

 *****************************************************************/

void DoMemoryDebug(INT16 invalidity, INT16 vpn) {
    if (DO_MEMORY_DEBUG == 0)
        return;
    printf("MEMORY DEBUG: ");
    if (invalidity == 0) {
        printf("Virtual Page Number %d was successful\n", vpn);
    }
    if (invalidity == 1) {
        printf("You asked for a virtual page, %d, greater than the\n", vpn);
        printf("\t\tmaximum number of virtual pages, %d\n", VIRTUAL_MEM_PGS);
    }
    if (invalidity == 2) {
        printf("You asked for a virtual page, %d, less than\n", vpn);
        printf("\t\tzero.  What are you thinking?\n");
    }
    if (invalidity == 3) {
        printf("You have not yet defined a page table that is visible\n");
        printf("\t\tto the hardware.  Z502_PAGE_TBL_ADDR must\n");
        printf("\t\tcontain the address of the page table.\n");
    }
    if (invalidity == 4) {
        printf("You have requested a page, %d, that is larger\n", vpn);
        printf("\t\tthan the size of the page table you defined.\n");
        printf("\t\tThe hardware thinks that the length of this table\n");
        printf("\t\tis %d\n", Z502_PAGE_TBL_LENGTH);
    }
    if (invalidity == 5) {
        printf("You have not initialized the slot in the page table\n");
        printf("\t\tcorresponding to virtual page %d\n", vpn);
        printf("\t\tYou must aim this virtual page at a physical frame\n");
        printf("\t\tand mark this page table slot as valid.\n");
    }
    if (invalidity == 6) {
        printf("The address you asked for crosses onto a second page.\n");
        printf("\t\tThis second page took a fault.\n");
        printf("You asked for a virtual page, %d, greater than the\n", vpn);
        printf("\t\tmaximum number of virtual pages, %d\n", VIRTUAL_MEM_PGS);
    }

    if (invalidity == 7) {
        printf("The address you asked for crosses onto a second page.\n");
        printf("\t\tThis second page took a fault.\n");
        printf("You have requested a page, %d, that is larger\n", vpn);
        printf("\t\tthan the size of the page table you defined.\n");
        printf("\t\tThe hardware thinks that the length of this table\n");
        printf("\t\tis %d\n", Z502_PAGE_TBL_LENGTH);
    }
    if (invalidity == 8) {
        printf("The address you asked for crosses onto a second page.\n");
        printf("\t\tThis second page took a fault.\n");
        printf("You have not initialized the slot in the page table\n");
        printf("\t\tcorresponding to virtual page %d\n", vpn);
        printf("\t\tYou must aim this virtual page at a physical frame\n");
        printf("\t\tand mark this page table slot as valid.\n");
    }
}                        // End of DoMemoryDebug         

/*****************************************************************
 Z502MemoryRead   and   Z502_MEM_WRITE

 Set a flag and call common code

 *****************************************************************/

void Z502MemoryRead(INT32 VirtualAddress, INT32 *data_ptr) {

    MemoryCommon(VirtualAddress, (char *) data_ptr, (BOOL) SYSNUM_MEM_READ);
}                  // End  Z502MemoryRead

void Z502MemoryWrite(INT32 VirtualAddress, INT32 *data_ptr) {

    MemoryCommon(VirtualAddress, (char *) data_ptr, (BOOL) SYSNUM_MEM_WRITE);
}                  // End  Z502MemoryWrite

/*************************************************************************
 Z502MemoryReadModify

 Do atomic modify of a memory location.  If the input parameters are
 incorrect, then return SuccessfulAction = FALSE.
 If the memory location
 is already locked by someone else, and we're asked to lock
 return SuccessfulAction = FALSE.
 If the lock was obtained here, return  SuccessfulAction = TRUE.
 If the lock was held by us or someone else, and we're asked to UNlock,
 return SuccessfulAction = TRUE.
 NOTE:  There are 10 lock locations set aside for the hardware's use.
 It is assumed that the hardware will have initialized these locks
 early on so that they don't interfere with this mechanism.
 *************************************************************************/

void Z502MemoryReadModify(INT32 VirtualAddress, INT32 NewLockValue,
        INT32 Suspend, INT32 *SuccessfulAction) {
    int WhichRecord;
    // GetLock( HardwareLock, "Z502_READ_MODIFY" );   JB - 7/26/06
    if (VirtualAddress < MEMORY_INTERLOCK_BASE
            || VirtualAddress
                    >= MEMORY_INTERLOCK_BASE + MEMORY_INTERLOCK_SIZE + 10
            || (NewLockValue != 0 && NewLockValue != 1)
            || (Suspend != TRUE && Suspend != FALSE )) {
        *SuccessfulAction = FALSE;
        return;
    }
    WhichRecord = VirtualAddress - MEMORY_INTERLOCK_BASE + 10;
    if (InterlockRecord[WhichRecord] == -1)
        CreateLock(&(InterlockRecord[WhichRecord]), "Z502MemoryReadModify");
    if (NewLockValue == 1 && Suspend == FALSE)
        *SuccessfulAction = GetTryLock(InterlockRecord[WhichRecord],
                "Z502MemReadMod");
    if (NewLockValue == 1 && Suspend == TRUE) {
        *SuccessfulAction = GetLock(InterlockRecord[WhichRecord],
                "Z502_READ_MODIFY");
    }
    if (NewLockValue == 0) {
        *SuccessfulAction = ReleaseLock(InterlockRecord[WhichRecord],
                "Z502_READ_MODIFY");
    }
    // ReleaseLock( HardwareLock, "Z502_READ_MODIFY" );   JB - 7/26/06

}             // End  Z502MemoryReadModify

/*************************************************************************
 MemoryMappedIO

 We talk to devices via certain memory addresses found in memory
 hyperspace.  In other words, these memory addresses don't point to
 real physical memory.  You must be privileged to touch this hardware.

 *************************************************************************/

void MemoryMappedIO(INT32 address, INT32 *data, BOOL read_or_write) {
    // static INT32 MemoryMappedIOInterruptDevice = -1;
    static INT32 MemoryMappedIODiskDevice = -1;
    static MEMORY_MAPPED_DISK_STATE MemoryMappedDiskState;
    INT32 index;

    // GetLock ( HardwareLock, "memory_mapped_io" );
    // We need to be in kernel mode or be in interrupt handler
    if (Z502_MODE != KERNEL_MODE && InterruptTid != GetMyTid()) {
        HardwareFault(PRIVILEGED_INSTRUCTION, 0);
        return;
    }
    ChargeTimeAndCheckEvents(COST_OF_MEMORY_MAPPED_IO);
    switch (address) {
    /*  Here we either get the device that's caused the interrupt, or
     *  we set the device id that we want to query further.  */

    case Z502InterruptDevice: {
            if (read_or_write == SYSNUM_MEM_READ) {
                *data = -1;
                for (index = 0; index <= LARGEST_STAT_VECTOR_INDEX; index++) {
                    if ( (STAT_VECTOR[SV_ACTIVE ][index] != 0)
                      && (STAT_VECTOR[SV_TID    ][index] == GetMyTid() ) )
                        *data = index;
                }
            } 
            /* else if (*data >= 0 && *data <= LARGEST_STAT_VECTOR_INDEX)
                MemoryMappedIOInterruptDevice = *data;
            else
                MemoryMappedIOInterruptDevice = -1;
                */
        break;
    }

    case Z502InterruptStatus: {
            *data = ERR_BAD_DEVICE_ID;
            for (index = 0; index <= LARGEST_STAT_VECTOR_INDEX; index++) {
                if ( (STAT_VECTOR[SV_ACTIVE ][index] != 0)
                  && (STAT_VECTOR[SV_TID    ][index] == GetMyTid() ) )   {
                    *data = STAT_VECTOR[SV_VALUE ][index];
                    // MemoryMappedIOInterruptDevice = -1;
                }
            }
        break;
    }

        case Z502InterruptClear: {
            for (index = 0; index <= LARGEST_STAT_VECTOR_INDEX; index++) {
                if ( (STAT_VECTOR[SV_ACTIVE ][index] != 0)
                  && (STAT_VECTOR[SV_TID    ][index] == GetMyTid() ) )   {
                    STAT_VECTOR[SV_VALUE  ][index] = 0;
                    STAT_VECTOR[SV_ACTIVE ][index] = 0;
                    STAT_VECTOR[SV_TID    ][index] = 0;
                    // MemoryMappedIOInterruptDevice = -1;
                }
            }
        break;
    }
    case Z502ClockStatus: {
        HardwareClock(data);
        break;
    }
    case Z502TimerStart: {
        HardwareTimer(*data);
        break;
    }
    case Z502TimerStatus: {
        if (timer_state.timer_in_use == TRUE)
            *data = DEVICE_IN_USE;
        else
            *data = DEVICE_FREE;
        break;
    }
        /*  When we get the disk ID, set up the structure that we will
         *  use to keep track of its state as user inputs the data.  */
    case Z502DiskSetID: {
        MemoryMappedIODiskDevice = -1;
        if (*data >= 1 && *data <= MAX_NUMBER_OF_DISKS) {
            MemoryMappedIODiskDevice = *data;
            MemoryMappedDiskState.sector = -1;
            MemoryMappedDiskState.action = -1;
            MemoryMappedDiskState.buffer = (char *) -1;
        } else {
            if (DO_DEVICE_DEBUG) {
                printf( "------ BEGIN DO_DEVICE DEBUG - IN Z502DiskSetID ---------------- \n");
                printf( "ERROR:  Trying to set device ID of the disk, but gave an invalid ID\n");
                printf( "You gave data of %d but it must be in the range of  1 =<  data  <= %d\n",
                        *data, MAX_NUMBER_OF_DISKS);
                printf( "-------- END DO_DEVICE DEBUG - ----------------------------------\n");
            }
        }
        break;
    }
    case Z502DiskSetSector: {
        if (MemoryMappedIODiskDevice != -1)
            MemoryMappedDiskState.sector = (short) *data;
        else {
            if (DO_DEVICE_DEBUG) {
                printf(
                        "------ BEGIN DO_DEVICE DEBUG - IN Z502DiskSetSector ------------ \n");
                printf(
                        "ERROR:  You must define the Device ID before setting the sector\n");
                printf(
                        "-------- END DO_DEVICE DEBUG - ----------------------------------\n");
            }
        }

        break;
    }
    case Z502DiskSetup4: {
        break;
    }
    case Z502DiskSetAction: {
        if (MemoryMappedIODiskDevice != -1)
            MemoryMappedDiskState.action = (INT16) *data;
        else {
            if (DO_DEVICE_DEBUG) {
                printf(
                        "------ BEGIN DO_DEVICE DEBUG - IN Z502DiskSetAction ------------ \n");
                printf(
                        "ERROR:  You must define the Device ID before setting the action\n");
                printf(
                        "-------- END DO_DEVICE DEBUG - ----------------------------------\n");
            }
        }
        break;
    }
    case Z502DiskSetBuffer: {
        if (MemoryMappedIODiskDevice != -1)
            MemoryMappedDiskState.buffer = (char *) data;
        else {
            if (DO_DEVICE_DEBUG) {
                printf(
                        "------ BEGIN DO_DEVICE DEBUG - IN Z502DiskSetBuffer ------------ \n");
                printf(
                        "ERROR:  You must define the Device ID before setting the buffer\n");
                printf(
                        "-------- END DO_DEVICE DEBUG - ----------------------------------\n");
            }
        }
        break;
    }
        /*  Make sure we have the state properly prepared
         *  and then do a read or write.  Clear the state. */
    case Z502DiskStart: {
        if (*data == 0 && MemoryMappedIODiskDevice != -1
                && MemoryMappedDiskState.action != -1
                && MemoryMappedDiskState.buffer != (char *) -1
                && MemoryMappedDiskState.sector != -1) {
            if (MemoryMappedDiskState.action == 0)
                HardwareReadDisk((INT16) MemoryMappedIODiskDevice,
                        MemoryMappedDiskState.sector,
                        MemoryMappedDiskState.buffer);
            if (MemoryMappedDiskState.action == 1)
                HardwareWriteDisk((INT16) MemoryMappedIODiskDevice,
                        MemoryMappedDiskState.sector,
                        MemoryMappedDiskState.buffer);
        } else {
            if (DO_DEVICE_DEBUG) {
                printf(
                        "------ BEGIN DO_DEVICE DEBUG - IN Z502DiskSetStart ------------- \n");
                printf(
                        "ERROR:  You have not specified all the disk pre-conditions before\n");
                printf(
                        "        starting the disk,  OR you didn't put the correct parameter\n");
                printf("        on this start command.\n");
                printf(
                        "-------- END DO_DEVICE DEBUG - ----------------------------------\n");
            }
        }
        MemoryMappedIODiskDevice = -1;
        MemoryMappedDiskState.action = -1;
        MemoryMappedDiskState.buffer = (char *) -1;
        MemoryMappedDiskState.sector = -1;
        break;
    }
    case Z502DiskStatus: {
        if (MemoryMappedIODiskDevice == -1)
            *data = ERR_BAD_DEVICE_ID;
        else {
            if (disk_state[MemoryMappedIODiskDevice].disk_in_use == TRUE)
                *data = DEVICE_IN_USE;
            else
                *data = DEVICE_FREE;
        }
        break;
    }
    default:
        break;
    } /* End of switch */
    // ReleaseLock ( HardwareLock, "memory_mapped_io" );

} /* End MemoryMappedIO  */

/*****************************************************************
 PhysicalMemoryCommon

 This code is designed to let an operating system touch physical
 memory - in fact if a user tries to enter this routine, a
 fault occurs.

 The routine reads or writes an entire page of physical memory
 from or to a buffer containing PGSIZE number of bytes.

 This allows the OS to do physical memory accesses without worrying
 about the page table.
 *****************************************************************/

void PhysicalMemoryCommon(INT32 PhysicalPageNumber, char *data_ptr,
        BOOL read_or_write) {
    INT16 PhysicalPageAddress;
    INT16 index;
    char Debug_Text[32];

    strcpy(Debug_Text, "PhysicalMemoryCommon");
    GetLock(HardwareLock, Debug_Text);

    // If a user tries to do this call from user mode, a fault occurs
    if (Z502_MODE != KERNEL_MODE) {
        HardwareFault(PRIVILEGED_INSTRUCTION, 0);
        return;
    }
    // If the user has asked for an illegal physical page, take a fault
    // then return with no modification to the user's buffer.
    if (PhysicalPageNumber < 0 || PhysicalPageNumber > PHYS_MEM_PGS) {
        ReleaseLock(HardwareLock, Debug_Text);
        HardwareFault(INVALID_PHYSICAL_MEMORY, PhysicalPageNumber);
        return;
    }
    PhysicalPageAddress = PGSIZE * PhysicalPageNumber;

    if (read_or_write == SYSNUM_MEM_READ) {
        for (index = 0; index < PGSIZE ; index++)
            data_ptr[index] = MEMORY[PhysicalPageAddress + index];
    }

    if (read_or_write == SYSNUM_MEM_WRITE) {
        for (index = 0; index < PGSIZE ; index++)
            MEMORY[PhysicalPageAddress + index] = data_ptr[index];
    }

    ChargeTimeAndCheckEvents(COST_OF_MEMORY_ACCESS);
    ReleaseLock(HardwareLock, Debug_Text);
}                      // End of PhysicalMemoryCommon

/*****************************************************************
 Z502ReadPhysicalMemory and  Z502WritePhysicalMemory

 This code reads physical memory.  It doesn't matter what you have
 the page table set to, this code will ignore the page table and
 read/write the physical memory.

 *****************************************************************/

void Z502ReadPhysicalMemory(INT32 PhysicalPageNumber, char *PhysicalDataPointer) {

    PhysicalMemoryCommon(PhysicalPageNumber, PhysicalDataPointer,
            (BOOL) SYSNUM_MEM_READ);
}                  // End  Z502ReadPhysicalMemory

void Z502WritePhysicalMemory(INT32 PhysicalPageNumber,
        char *PhysicalDataPointer) {

    PhysicalMemoryCommon(PhysicalPageNumber, PhysicalDataPointer,
            (BOOL) SYSNUM_MEM_WRITE);
}                  // End  Z502WritePhysicalMemory

/*************************************************************************

 HardwareReadDisk

 This code simulates a disk read.  Actions include:
 o If not in KERNEL_MODE, then cause priv inst trap.
 o Do range check on disk_id, sector; give
 interrupt error = ERR_BAD_PARAM if illegal.
 o If an event for this disk already exists ( the disk
 is already busy ), then give interrupt error ERR_DISK_IN_USE.
 o Search for sector structure off of hashed value.
 o If search fails give interrupt error = ERR_NO_PREVIOUS_WRITE
 o Copy data from sector to buffer.
 o From disk_state information, determine how long this request will take.
 o Request a future interrupt for this event.
 o Advance time and see if an interrupt has occurred.

 **************************************************************************/

void HardwareReadDisk(INT16 disk_id, INT16 sector, char *buffer_ptr) {
    INT32 local_error;
    char *sector_ptr = 0;
    INT32 access_time;
    INT16 error_found;

    error_found = 0;
    // We need to be in kernel mode or be in interrupt handler
    if (Z502_MODE != KERNEL_MODE && InterruptTid != GetMyTid()) {
        HardwareFault(PRIVILEGED_INSTRUCTION, 0);
        return;
    }

    if (disk_id < 1 || disk_id > MAX_NUMBER_OF_DISKS) {
        disk_id = 1; /* To aim at legal vector  */
        error_found = ERR_BAD_PARAM;
    }
    if (sector < 0 || sector >= NUM_LOGICAL_SECTORS)
        error_found = ERR_BAD_PARAM;

    if (error_found == 0) {
        GetSectorStructure(disk_id, sector, &sector_ptr, &local_error);
        if (local_error != 0)
            error_found = ERR_NO_PREVIOUS_WRITE;

        if (disk_state[disk_id].disk_in_use == TRUE)
            error_found = ERR_DISK_IN_USE;
    }

    /* If we found an error, add an event that will cause an immediate
     hardware interrupt.                                              */

    if (error_found != 0) {
        if (DO_DEVICE_DEBUG) {
            printf("--- BEGIN DO_DEVICE DEBUG - IN read_disk ----- \n");
            printf("ERROR:  Something screwed up   The error\n");
            printf("      code is %d that you can look up in global.h\n",
                    error_found);
            printf("     The disk will cause an interrupt to tell \n");
            printf("      you about that error.\n");
            printf("--- END DO_DEVICE DEBUG - ---------------------\n");
        }
        AddEventToInterruptQueue(CurrentSimulationTime,
                (INT16) (DISK_INTERRUPT + disk_id - 1), error_found,
                &disk_state[disk_id].event_ptr);
    } else {
        memcpy(buffer_ptr, sector_ptr, PGSIZE);

        access_time = CurrentSimulationTime + 100
                + abs(disk_state[disk_id].last_sector - sector) / 20;
        HardwareStats.disk_reads[disk_id]++;
        HardwareStats.time_disk_busy[disk_id] += access_time
                - CurrentSimulationTime;
        if (DO_DEVICE_DEBUG) {
            printf("--- BEGIN DO_DEVICE DEBUG - IN read_disk ----- \n");
            printf("Time now = %d: ", CurrentSimulationTime);
            printf("  Disk will interrupt at time = %d\n", access_time);
            printf("---- END DO_DEVICE DEBUG - --------------------\n");
        }
        AddEventToInterruptQueue(access_time,
                (INT16) (DISK_INTERRUPT + disk_id - 1), (INT16) ERR_SUCCESS,
                &disk_state[disk_id].event_ptr);
        disk_state[disk_id].last_sector = sector;
    }
    disk_state[disk_id].disk_in_use = TRUE;
    // printf("1. Setting %d TRUE\n", disk_id );
    ChargeTimeAndCheckEvents(COST_OF_DISK_ACCESS);

}               // End of HardwareReadDisk   

/*****************************************************************

 HardwareWriteDisk

 This code simulates a disk write.  Actions include:
 o If not in KERNEL_MODE, then cause priv inst trap.
 o Do range check on disk_id, sector; give interrupt error 
 = ERR_BAD_PARAM if illegal.
 o If an event for this disk already exists ( the disk is already busy ), 
 then give interrupt error ERR_DISK_IN_USE.
 o Search for sector structure off of hashed value.
 o If search fails give create a sector on the simulated disk.
 o Copy data from buffer to sector.
 o From disk_state information, determine how long this request will take.
 o Request a future interrupt for this event.
 o Advance time and see if an interrupt has occurred.

 *****************************************************************/

void HardwareWriteDisk(INT16 disk_id, INT16 sector, char *buffer_ptr) {
    INT32 local_error;
    char *sector_ptr;
    INT32 access_time;
    INT16 error_found;

    error_found = 0;
    // We need to be in kernel mode or be in interrupt handler
    if (Z502_MODE != KERNEL_MODE && InterruptTid != GetMyTid()) {
        HardwareFault(PRIVILEGED_INSTRUCTION, 0);
        return;
    }

    if (disk_id < 1 || disk_id > MAX_NUMBER_OF_DISKS) {
        disk_id = 1; /* To aim at legal vector  */
        error_found = ERR_BAD_PARAM;
    }
    if (sector < 0 || sector >= NUM_LOGICAL_SECTORS)
        error_found = ERR_BAD_PARAM;

    if (disk_state[disk_id].disk_in_use == TRUE)
        error_found = ERR_DISK_IN_USE;

    if (error_found != 0) {
        if (DO_DEVICE_DEBUG) {
            printf("---- BEGIN DO_DEVICE DEBUG - IN write_disk --- \n");
            printf("ERROR:  in your disk request.  The error\n");
            printf("     code is %d that you can look up in global.h\n",
                    error_found);
            printf("    The disk will cause an interrupt to tell \n");
            printf("     you about that error.\n");
            printf("---- END DO_DEVICE DEBUG - --------------------\n");
        }
        AddEventToInterruptQueue(CurrentSimulationTime,
                (INT16) (DISK_INTERRUPT + disk_id - 1), error_found,
                &disk_state[disk_id].event_ptr);
    } else {
        GetSectorStructure(disk_id, sector, &sector_ptr, &local_error);
        if (local_error != 0) /* No structure for this sector exists */
            CreateSectorStruct(disk_id, sector, &sector_ptr);

        memcpy(sector_ptr, buffer_ptr, PGSIZE);

        access_time = (INT32) CurrentSimulationTime + 100
                + abs(disk_state[disk_id].last_sector - sector) / 20;
        HardwareStats.disk_writes[disk_id]++;
        HardwareStats.time_disk_busy[disk_id] += access_time
                - CurrentSimulationTime;
        if (DO_DEVICE_DEBUG) {
            printf("--- BEGIN DO_DEVICE DEBUG - IN write_disk ---- \n");
            printf("Time now = %d:  ", CurrentSimulationTime);
            printf("Disk write will cause interrupt at time = %d\n",
                    access_time);
            printf("----- END DO_DEVICE DEBUG - -------------------\n");
        }
        AddEventToInterruptQueue(access_time,
                (INT16) (DISK_INTERRUPT + disk_id - 1), (INT16) ERR_SUCCESS,
                &disk_state[disk_id].event_ptr);
        disk_state[disk_id].last_sector = sector;
    }
    disk_state[disk_id].disk_in_use = TRUE;
    // printf("2. Setting %d TRUE\n", disk_id );
    ChargeTimeAndCheckEvents(COST_OF_DISK_ACCESS);

}                           // End of HardwareWriteDisk   

/*****************************************************************

 HardwareTimer()

 This is the routine that sets up a clock to interrupt
 in the future.  Actions include:
 o If not in KERNEL_MODE, then cause priv inst trap.
 o If time is illegal, generate an interrupt immediately.
 o Purge any outstanding timer events.
 o Request a future event.
 o Advance time and see if an interrupt has occurred.

 *****************************************************************/

void HardwareTimer(INT32 time_to_delay) {
    INT32 error;
    UINT32 CurrentTimerExpirationTime = 9999999;

    // We need to be in kernel mode or be in interrupt handler
    if (Z502_MODE != KERNEL_MODE && InterruptTid != GetMyTid()) {
        HardwareFault(PRIVILEGED_INSTRUCTION, 0);
        return;
    }

    // Bugfix 4.0 - if the time is 0 on Linux, the interrupt may occur
    // too soon.  We really need a lock here and on HardwareInterrupt
    if (time_to_delay == 0)
        time_to_delay = 1;

    if (DO_DEVICE_DEBUG) {           // Print lots of info
        printf("------ BEGIN DO_DEVICE DEBUG - START TIMER --------- \n");
        if (timer_state.timer_in_use == TRUE) {
            printf("The timer is already in use - ");
            printf("you will destroy previous timer request\n");
        } else
            printf("The timer is not currently running\n");
        if (time_to_delay < 0)
            printf("TROUBLE - you want to delay for negative time!!\n");
        if (timer_state.timer_in_use == TRUE)
            CurrentTimerExpirationTime = timer_state.event_ptr->time_of_event;
        if (CurrentTimerExpirationTime
                < CurrentSimulationTime + time_to_delay) {
            printf("TROUBLE - you are replacing the current timer ");
            printf("value of %d with a time of %d\n",
                    CurrentTimerExpirationTime,
                    CurrentSimulationTime + time_to_delay);
            printf("which is even further in the future.  This is not wise\n");
        }
        printf(
                "Time Now = %d, Delaying for time = %d,  Interrupt will occur at = %d\n",
                CurrentSimulationTime, time_to_delay,
                CurrentSimulationTime + time_to_delay);
        printf("-------- END DO_DEVICE DEBUG - ---------------------- \n");
    }     // End of if DO_DEVICE_DEBUG

    if (timer_state.timer_in_use == TRUE) {
        DequeueItemFromEventQueue(timer_state.event_ptr, &error);
        if (error != 0) {
            printf("Internal error - we tried to retrieve a timer\n");
            printf("event, but failed in HardwareTimer.\n");
            HardwareInternalPanic(ERR_Z502_INTERNAL_BUG);
        }
        timer_state.timer_in_use = FALSE;
    }

    if (time_to_delay < 0) {   // Illegal time  
        AddEventToInterruptQueue(CurrentSimulationTime, TIMER_INTERRUPT,
                (INT16) ERR_BAD_PARAM, &timer_state.event_ptr);
        return;
    }

    AddEventToInterruptQueue(CurrentSimulationTime + time_to_delay,
            TIMER_INTERRUPT, (INT16) ERR_SUCCESS, &timer_state.event_ptr);
    timer_state.timer_in_use = TRUE;
    ChargeTimeAndCheckEvents(COST_OF_TIMER);

}                                       // End of HardwareTimer  

/*****************************************************************

 HardwareClock()

 This is the routine that makes the current simulation
 time visible to the OS502.  Actions include:
 o If not in KERNEL_MODE, then cause priv inst trap.
 o Read the simulation time from
 "CurrentSimulationTime"
 o Return it to the caller.

 *****************************************************************/

void HardwareClock(INT32 *current_time_returned) {
    // We need to be in kernel mode or be in interrupt handler
    if (Z502_MODE != KERNEL_MODE && InterruptTid != GetMyTid()) {
        *current_time_returned = -1; /* return bogus value      */
        HardwareFault(PRIVILEGED_INSTRUCTION, 0);
        return;
    }

    ChargeTimeAndCheckEvents(COST_OF_CLOCK);
    *current_time_returned = (INT32) CurrentSimulationTime;

}           // End of HardwareClock      

/*****************************************************************

 Z502Halt()

 This is the routine that ends the simulation.
 Actions include:
 o If not in KERNEL_MODE, then cause priv inst trap.
 o Wrapup any outstanding work and terminate.

 *****************************************************************/

void Z502Halt(void) {
    // We need to be in kernel mode or be in interrupt handler
    if (Z502_MODE != KERNEL_MODE && InterruptTid != GetMyTid()) {
        HardwareFault(PRIVILEGED_INSTRUCTION, 0);
        return;
    }
    PrintHardwareStats();

    printf("The Z502 halts execution and Ends at Time %d\n",
            CurrentSimulationTime);
    GoToExit(0);
}                     // End of Z502Halt

/*****************************************************************

 Z502Idle()

 This is the routine that idles the Z502 until the next
 interrupt.  Actions include:
 o If not in KERNEL_MODE, then cause priv inst trap.
 o If there's nothing to wait for, print a message
 and halt the machine.
 o Get the next event and cause an interrupt.

 *****************************************************************/

void Z502Idle(void) {
    INT32 time_of_next_event;
    static INT32 NumberOfIdlesWithNothingOnEventQueue = 0;

    GetLock(HardwareLock, "Z502Idle");
    // We need to be in kernel mode or be in interrupt handler
    if (Z502_MODE != KERNEL_MODE && InterruptTid != GetMyTid()) {
        HardwareFault(PRIVILEGED_INSTRUCTION, 0);
        return;
    }

    GetNextEventTime(&time_of_next_event);
    if (DO_DEVICE_DEBUG) {
        printf("---- BEGIN DO_DEVICE DEBUG - IN Z502Idle ------------ \n");
        printf("The time is now = %d: ", CurrentSimulationTime);
        if (time_of_next_event < 0)
            printf("There's no event enqueued - timer not started\n");
        else
            printf("Starting interrupt handler for event at time = %d\n",
                    time_of_next_event);
        printf("----- END DO_DEVICE DEBUG - --------------------------\n");
    }
    if (time_of_next_event < 0)
        NumberOfIdlesWithNothingOnEventQueue++;
    else
        NumberOfIdlesWithNothingOnEventQueue = 0;
    if (NumberOfIdlesWithNothingOnEventQueue > 10) {
        printf("ERROR in Z502Idle.  IDLE will wait forever since\n");
        printf("there is no event that will cause an interrupt.\n");
        printf("This occurs because when you decided to call 'Z502Idle'\n");
        printf("there was an event waiting - but the event was triggered\n");
        printf("too soon. Avoid this error by:\n");
        printf("1. using   'ZCALL' instead of CALL for all routines between\n");
        printf("   the event-check and Z502Idle\n");
        printf("2. limiting the work or routines (like printouts) between\n");
        printf("   the event-check and Z502Idle\n");
        HardwareInternalPanic(ERR_OS502_GENERATED_BUG);
    }
    if ((time_of_next_event > 0)
            && (CurrentSimulationTime < (UINT32) time_of_next_event))
        CurrentSimulationTime = time_of_next_event;
    ReleaseLock(HardwareLock, "Z502Idle");
    SignalCondition(InterruptCondition, "Z502Idle");
}                    // End of Z502Idle

/*****************************************************************

 Z502MakeContext()

 This is the routine that sets up a new context.
 Actions include:
 o If not in KERNEL_MODE, then cause priv inst trap.
 o Allocate a structure for a context.  The "calloc" sets the contents 
 of this memory to 0.
 o Ensure that memory was actually obtained.
 o Initialize the structure.
 o Associate the Context with a thread that will run it
 o Advance time and see if an interrupt has occurred.
 o Return the structure pointer to the caller.

 *****************************************************************/

void Z502MakeContext(void **ReturningContextPointer, void *starting_address,
        BOOL user_or_kernel) {
    Z502CONTEXT *our_ptr;

    if (Z502Initialized == FALSE) {
        Z502Init();
    }

    GetLock(HardwareLock, "Z502MakeContext");
    // We need to be in kernel mode or be in interrupt handler
    if (Z502_MODE != KERNEL_MODE && InterruptTid != GetMyTid()) {
        HardwareFault(PRIVILEGED_INSTRUCTION, 0);
        return;
    }

    our_ptr = (Z502CONTEXT *) calloc(1, sizeof(Z502CONTEXT));
    if (our_ptr == NULL ) {
        printf("We didn't complete the calloc in MAKE_CONTEXT.\n");
        HardwareInternalPanic(ERR_OS502_GENERATED_BUG);
    }

    our_ptr->structure_id = CONTEXT_STRUCTURE_ID;
    our_ptr->entry = (void *) starting_address;
    our_ptr->page_table_ptr = NULL;
    our_ptr->page_table_len = 0;
    our_ptr->pc = 0;
    our_ptr->program_mode = user_or_kernel;
    our_ptr->fault_in_progress = FALSE;
    *ReturningContextPointer = (void *) our_ptr;

    // Attach the Context to a thread
    AssociateContextWithProcess(our_ptr);

    ChargeTimeAndCheckEvents(COST_OF_MAKE_CONTEXT);
    ReleaseLock(HardwareLock, "Z502MakeContext");

}                    // End of Z502MakeContext 

/*****************************************************************

 Z502DestroyContext()

 This is the routine that removes a context.
 Actions include:
 o If not in KERNEL_MODE, then cause priv inst trap.
 o Validate structure_id on context.  If bogus, return
 fault error = ERR_ILLEGAL_ADDRESS.
 o Free the memory pointed to by the pointer.
 o Advance time and see if an interrupt has occurred.

 *****************************************************************/

void Z502DestroyContext(void **IncomingContextPointer) {
    Z502CONTEXT **context_ptr = (Z502CONTEXT **) IncomingContextPointer;

    GetLock(HardwareLock, "Z502DestroyContext");
    // We need to be in kernel mode or be in interrupt handler
    if (Z502_MODE != KERNEL_MODE && InterruptTid != GetMyTid()) {
        HardwareFault(PRIVILEGED_INSTRUCTION, 0);
        return;
    }

    if (*context_ptr == Z502_CURRENT_CONTEXT) {
        printf("PANIC:  Attempt to destroy context of the currently ");
        printf("running process.\n");
        HardwareInternalPanic(ERR_OS502_GENERATED_BUG);
    }

    if ((*context_ptr)->structure_id != CONTEXT_STRUCTURE_ID)
        HardwareFault(CPU_ERROR, (INT16) ERR_ILLEGAL_ADDRESS);

    (*context_ptr)->structure_id = 0;
    free(*context_ptr);
    ReleaseLock(HardwareLock, "Z502DestroyContext");

}                   // End of Z502DestroyContext

/*****************************************************************

 Z502SwitchContext()

 This is the routine that sets up a new context.
 Actions include:
 o Disable interrupts - strange things happen if we
 interrupt while we're scheduling.
 o If not in KERNEL_MODE, then cause priv inst trap.
 o Validate structure_id on context.  If bogus, return
 fault error = ERR_ILLEGAL_ADDRESS.
 o Validate kill_or_save parameter.  If bogus, return
 fault error = ERR_BAD_PARAM.
 n Save the context in a well known place so the hardware
 always knows who's running.   (in Z502_CURRENT_CONTEXT)
 o If this is the initial process, ignore KILL/SAVE.
 o If KILL_SELF, invalidate the context ID and free
 o If SAVE_SELF, put the registers into the context.
 o Advance time and see if an interrupt has occurred.
 o If "next_context_ptr" is null, run last process again.
 o Move stuff from new context to registers.
 o If this context took a memory fault, that fault
 is unresolved.  Instead of going back to the user
 context, try the memory reference again.
 o Call the starting address.

 *****************************************************************/

void Z502SwitchContext(BOOL kill_or_save, void **IncomingContextPointer) {
    Z502CONTEXT *curr_ptr;      // The context we're CURRENTLY running on
    Z502CONTEXT *callers_ptr;   // The context we're CURRENTLY running on
    Z502CONTEXT **context_ptr = (Z502CONTEXT **) IncomingContextPointer;
    //void            (*routine)( void );

    GetLock(HardwareLock, "Z502SwitchContext");
    // We need to be in kernel mode or be in interrupt handler
    if (Z502_MODE != KERNEL_MODE && InterruptTid != GetMyTid()) {
        ReleaseLock(HardwareLock, "Z502SwitchContext");
        HardwareFault(PRIVILEGED_INSTRUCTION, 0);
        return;
    }

    if ((*context_ptr)->structure_id != CONTEXT_STRUCTURE_ID) {
        ReleaseLock(HardwareLock, "Z502SwitchContext");
        HardwareFault(CPU_ERROR, (INT16) ERR_ILLEGAL_ADDRESS);
    }
    if (kill_or_save != SWITCH_CONTEXT_KILL_MODE
            && kill_or_save != SWITCH_CONTEXT_SAVE_MODE) {
        ReleaseLock(HardwareLock, "Z502SwitchContext");
        HardwareFault(CPU_ERROR, (INT16) ERR_BAD_PARAM);
    }
    curr_ptr = Z502_CURRENT_CONTEXT;
    callers_ptr = Z502_CURRENT_CONTEXT;
    HardwareStats.context_switches++;

    // If we're switching to the same thread, then we could have a problem
    // because we are resuming ourselves (not suspended!) and then suspending
    // ourselves which will cause us to hang -- it does so on LINUX.
    // BUT make sure that the suspend is the last instruction in this routine
    //    and we don't do any work after it.
    //  printf("Z502Switch... curr = %lX, Incoming = %lX\n",
    //         (unsigned long)curr_ptr, (unsigned long)*context_ptr);
    if (curr_ptr == *context_ptr) {
        ReleaseLock(HardwareLock, "Z502SwitchContext");
        //      printf("Z502Switch... - returning with no switch\n");
        return;
    }
    // This could be the first context we want to run, so the
    // CURRENT_CONTEXT might be NULL.
    if (Z502_CURRENT_CONTEXT != NULL ) {
        if (curr_ptr->structure_id != CONTEXT_STRUCTURE_ID) {
            printf("CURRENT_CONTEXT is invalid in SwitchContext\n");
            HardwareInternalPanic(ERR_OS502_GENERATED_BUG);
        }
        if (kill_or_save == SWITCH_CONTEXT_KILL_MODE) {
            curr_ptr->structure_id = 0;
            free(curr_ptr);
        }

        if (kill_or_save == SWITCH_CONTEXT_SAVE_MODE) {
            //        curr_ptr->call_type = SYS_CALL_CALL_TYPE;
            curr_ptr->program_mode = Z502_MODE; // Added Bugfix 12/8/13
            curr_ptr->reg1 = Z502_REG1;
            curr_ptr->reg2 = Z502_REG2;
            curr_ptr->reg3 = Z502_REG3;
            curr_ptr->reg4 = Z502_REG4;
            curr_ptr->reg5 = Z502_REG5;
            curr_ptr->reg6 = Z502_REG6;
            curr_ptr->reg7 = Z502_REG7;
            curr_ptr->reg8 = Z502_REG8;
            curr_ptr->reg9 = Z502_REG9;
            curr_ptr->page_table_ptr = Z502_PAGE_TBL_ADDR;
            curr_ptr->page_table_len = Z502_PAGE_TBL_LENGTH;
        }
    }                           // End of current context not null

    //  NOW we will be working with the NEXT context
    curr_ptr = *context_ptr;    // This will be the NEW context

    // 6/25/13 - not clear that this works as it should.  Verify that the
    //   interruptTid is actually getting set.
    if (InterruptTid == GetMyTid()) { // Are we running on hardware thread
        printf("Trying to switch context while at interrupt level > 0.\n");
        printf("This is NOT advisable and will lead to strange results.\n");
    }

    Z502_CURRENT_CONTEXT = curr_ptr;
    Z502_PAGE_TBL_ADDR = curr_ptr->page_table_ptr;
    Z502_PAGE_TBL_LENGTH = curr_ptr->page_table_len;
    Z502_MODE = curr_ptr->program_mode;
    Z502_REG1 = curr_ptr->reg1;
    Z502_REG2 = curr_ptr->reg2;
    Z502_REG3 = curr_ptr->reg3;
    Z502_REG4 = curr_ptr->reg4;
    Z502_REG5 = curr_ptr->reg5;
    Z502_REG6 = curr_ptr->reg6;
    Z502_REG7 = curr_ptr->reg7;
    Z502_REG8 = curr_ptr->reg8;
    Z502_REG9 = curr_ptr->reg9;

    // Go wake up the new thread.  If it's a first time schedule for this
    // thread, it will start up in the Z502PrepareProcessForExecution
    // code.  Otherwise it will continue down at the bottom of this routine.
    ResumeProcessExecution(curr_ptr);

    // OK - we're free to unlock our work here - it's done.
    ReleaseLock(HardwareLock, "Z502SwitchContext");

    // Go suspend the original thread - the one that called SwitchContext
    // That means when this thread is later awakened, it will resume
    // execution at THIS point and will return from Z502SwitchContext
    SuspendProcessExecution(callers_ptr);

    //  MAKE SURE NO SIGNIFICANT WORK IS INSERTED AT THIS POINT

    // Release 4.0 - we come to this point when we are Resumed by a process.
}                               // End of Z502SwitchContext

/*****************************************************************

 ChargeTimeAndCheckEvents()

 This is the routine that will increment the simulation
 clock and then check that no event has occurred.
 Actions include:
 o Increment the clock.
 o IF interrupts are masked, don't even think about
 trying to do an interrupt.
 o If interrupts are NOT masked, determine if an interrupt
 should occur.  If so, then signal the interrupt thread.

 ******************************************************************/

void ChargeTimeAndCheckEvents(INT32 time_to_charge) {
    INT32 time_of_next_event;

    CurrentSimulationTime += time_to_charge;
    HardwareStats.number_charge_times++;

    //printf( "Charge_Time... -- current time = %ld\n", CurrentSimulationTime );
    GetNextEventTime(&time_of_next_event);
    if (time_of_next_event > 0
            && time_of_next_event <= (INT32) CurrentSimulationTime) {
        SignalCondition(InterruptCondition, "Charge_Time");
    }
}              // End of ChargeTimeAndCheckEvents      

/*****************************************************************

 HardwareInterrupt()

 NOTE:  This code runs as a separate thread.
 This is the routine that will cause the hardware interrupt
 and will call OS502.      Actions include:
 o Wait for a signal from base level.
 o Get the next event - we expect the time has expired, but if
 it hasn't do nothing.
 o If it's a device, show that the device is no longer busy.
 o Set up registers which user interrupt handler will see.
 o Call the interrupt handler.

 Simply return if no event can be found.
 *****************************************************************/

void HardwareInterrupt(void) {
    INT32 time_of_event;
    INT32 index;
    INT16 event_type;
    INT16 event_error;
    INT32 local_error;
    INT32 TimeToWaitForCondition = 30; // Millisecs before Condition will go off
    void (*interrupt_handler)(void);

    InterruptTid = GetMyTid();
    while (TRUE ) {
        GetNextEventTime(&time_of_event);
        while (time_of_event < 0
                || time_of_event > (INT32) CurrentSimulationTime) {
            GetLock(InterruptLock, "HardwareInterrupt-1");
            WaitForCondition(InterruptCondition, InterruptLock,
                    TimeToWaitForCondition, "HardwareInterrupt");
            ReleaseLock(InterruptLock, "HardwareInterrupt-1");
            GetNextEventTime(&time_of_event);
            // PrintEventQueue( );
#ifdef DEBUG_CONDITION
            printf("Hardware_Interrupt: time = %d: next event = %d\n",
                    CurrentSimulationTime, time_of_event);
#endif
        }

        // We got here because there IS an event that needs servicing.

        GetLock(HardwareLock, "HardwareInterrupt-2");
        NumberOfInterruptsStarted++;
        GetNextOrderedEvent(&time_of_event, &event_type, &event_error,
                &local_error);
        if (local_error != 0) {
            printf("In HardwareInterrupt we expected to find an event\n");
            printf("Something in the OS has destroyed this location.\n");
            HardwareInternalPanic(ERR_OS502_GENERATED_BUG);
        }

        if (event_type >= DISK_INTERRUPT
                && event_type <= DISK_INTERRUPT + MAX_NUMBER_OF_DISKS - 1) {
            /* Note - if we get a disk error, we simply enqueued an event
             and incremented (hopefully momentarily) the disk_in_use value */
            if (disk_state[event_type - DISK_INTERRUPT + 1].disk_in_use == FALSE) {
                printf("False interrupt - the Z502 got an interrupt from a\n");
                printf("DISK - but that disk wasn't in use.\n");
                HardwareInternalPanic(ERR_Z502_INTERNAL_BUG);
            }

            //  NOTE:  Here when we take a disk interrupt, we clear the busy of ALL
            //  disks because we assume that the user will handle all of them with
            //  the interrupt that's about to be done.
            for (index = DISK_INTERRUPT ;
                    index <= DISK_INTERRUPT + MAX_NUMBER_OF_DISKS - 1;
                    index++) {
                if ( (STAT_VECTOR[SV_ACTIVE ][index] != 0) 
                  && (STAT_VECTOR[SV_TID    ][index] == GetMyTid()  ) ) {
                    // Bugfix 08/2012 - disk_state contains MAX_NUMBER_OF_DISKS elements
                    // We were spraying some unknown memory locations
                    disk_state[index - DISK_INTERRUPT ].disk_in_use = FALSE;
                    // printf("3. Setting %d FALSE\n", index );
                }
            }
            //  We MAYBE should be clearing all these as well - and not just the current one.

            disk_state[event_type - DISK_INTERRUPT + 1].disk_in_use = FALSE;
            // printf("3. Setting %d FALSE\n", event_type );
            disk_state[event_type - DISK_INTERRUPT + 1].event_ptr = NULL;
        }
        if (event_type == TIMER_INTERRUPT && event_error == ERR_SUCCESS) {
            if (timer_state.timer_in_use == FALSE) {
                printf("False interrupt - the Z502 got an interrupt from a\n");
                printf("TIMER - but that timer wasn't in use.\n");
                HardwareInternalPanic(ERR_Z502_INTERNAL_BUG);
            }
            timer_state.timer_in_use = FALSE;
            timer_state.event_ptr = NULL;
        }

        /*  NOTE: The hardware clears these in main, but not after that     */
        STAT_VECTOR[SV_ACTIVE ][event_type] = 1;
        STAT_VECTOR[SV_VALUE  ][event_type] = event_error;
        STAT_VECTOR[SV_TID    ][event_type] = GetMyTid();

        if (DO_DEVICE_DEBUG) {
            printf( "------ BEGIN DO_DEVICE DEBUG - CALLING INTERRUPT HANDLER --------- \n");
            printf( "The time is now = %d: Handling event that was scheduled to happen at = %d\n",
                    CurrentSimulationTime, time_of_event);
            printf( "The hardware is now about to enter your interrupt_handler in base.c\n");
            printf("-------- END DO_DEVICE DEBUG - ---------------------- \n");
        }

        //  If we've come here from Z502_IDLE, then the current time may be
        // less than the event time. Then we must increase the
        // CurrentSimulationTime to match the time given by the event.  
        //
        // if ( ( INT32 )CurrentSimulationTime < time_of_event )
        // CurrentSimulationTime              = time_of_event;
        //
        if (Z502_CURRENT_CONTEXT->structure_id != CONTEXT_STRUCTURE_ID) {
            printf("Z502_CURRENT_CONTEXT is invalid in hard_interrupt\n");
            printf("Something in the OS has destroyed this location.\n");
            HardwareInternalPanic(ERR_OS502_GENERATED_BUG);
        }
        ReleaseLock(HardwareLock, "HardwareInterrupt-2");

        interrupt_handler =
                (void (*)(void)) TO_VECTOR[TO_VECTOR_INT_HANDLER_ADDR ];
        (*interrupt_handler)();

        /* Here we clean up after returning from the user's interrupt handler */

        GetLock(HardwareLock, "HardwareInterrupt-3"); // I think this is needed
        if (Z502_CURRENT_CONTEXT->structure_id != CONTEXT_STRUCTURE_ID) {
            printf("Z502_REGCURRENT_CONTEXT is invalid in hard_interrupt\n");
            printf("Something in the OS has destroyed this location.\n");
            HardwareInternalPanic(ERR_OS502_GENERATED_BUG);
        }

        ReleaseLock(HardwareLock, "HardwareInterrupt-3");
        NumberOfInterruptsCompleted++;
    }         // End of while TRUE       
}                 // End of HardwareInterrupt  

/*****************************************************************

 HardwareFault()

 This is the routine that will cause the hardware fault and will call OS502.
 Actions include:
 o Set up the registers which the fault handler will see.
 o Call the fault_handler.

 *****************************************************************/

void HardwareFault(INT16 fault_type, INT16 argument) {
    void (*fault_handler)(void);

    STAT_VECTOR[SV_ACTIVE ][fault_type] = 1;
    STAT_VECTOR[SV_VALUE  ][fault_type] = (INT16) argument;
    STAT_VECTOR[SV_TID    ][fault_type] = GetMyTid();
    Z502_MODE = KERNEL_MODE;
    HardwareStats.number_faults++;
    fault_handler = (void (*)(void)) TO_VECTOR[TO_VECTOR_FAULT_HANDLER_ADDR ];

    //  We're about to get out of the hardware - release the lock
    // ReleaseLock( HardwareLock );
    (*fault_handler)();
    // GetLock( HardwareLock, "HardwareFault" );
    if (Z502_CURRENT_CONTEXT->structure_id != CONTEXT_STRUCTURE_ID) {
        printf("Z502_REGCURRENT_CONTEXT is invalid in HardwareFault\n");
        printf("Something in the OS has destroyed this location.\n");
        HardwareInternalPanic(ERR_OS502_GENERATED_BUG);
    }
    Z502_MODE = Z502_CURRENT_CONTEXT->program_mode;
}              // End of HardwareFault       

/*****************************************************************

 SoftwareTrap()

 This is the routine that will cause the software trap
 and will call OS502.
 Actions include:
 o Set up the registers which the OS trap handler
 will see.
 o Call the trap_handler.

 *****************************************************************/

void SoftwareTrap(void) {
    void (*trap_handler)(void);

    Z502_MODE = KERNEL_MODE;
    ChargeTimeAndCheckEvents(COST_OF_SOFTWARE_TRAP);
    trap_handler = (void (*)(void)) TO_VECTOR[TO_VECTOR_TRAP_HANDLER_ADDR ];
    (*trap_handler)();
    STAT_VECTOR[SV_ACTIVE ][SOFTWARE_TRAP ] = 0;
    STAT_VECTOR[SV_VALUE  ][SOFTWARE_TRAP ] = 0;
    STAT_VECTOR[SV_TID    ][SOFTWARE_TRAP ] = 0;

}             // End of SoftwareTrap

/*****************************************************************

 HardwareInternalPanic()

 When we come here, it's all over.  The hardware will come
 to a grinding halt.
 Actions include:
 o Print out who caused the problem.
 o Get out of the simulation.

 *****************************************************************/

void HardwareInternalPanic(INT32 panic_type) {
    if (panic_type == ERR_Z502_INTERNAL_BUG)
        printf("PANIC: Occurred because of bug in simulator.\n");
    if (panic_type == ERR_OS502_GENERATED_BUG)
        printf("PANIC: Because OS502 used hardware wrong.\n");
    PrintRingBuffer();
    GoToExit(0);
}                  // End of HardwareInternalPanic

/*****************************************************************

 AddEventToInterruptQueue()

 This is the routine that will add an event to the queue.
 Actions include:
 o Do lots of sanity checks.
 o Allocate a structure for the event.
 o Fill in the structure.
 o Enqueue it.
 Store data in ring buffer for possible debugging.

 *****************************************************************/

void AddEventToInterruptQueue(INT32 time_of_event, INT16 event_type,
        INT16 event_error, EVENT **returned_event_ptr) {
    EVENT *ep;
    EVENT *temp_ptr;
    EVENT *last_ptr;
    INT16 erbi; /* Short for event_ring_buffer_index    */

    if (time_of_event < (INT32) CurrentSimulationTime) {
        printf("time_of_event < current_sim.._time in AddEvent\n");
        printf("time_of_event = %d,  CurrentSimulationTime = %d\n",
                time_of_event, CurrentSimulationTime);
        HardwareInternalPanic(ERR_Z502_INTERNAL_BUG);
    }
    if (event_type < 0 || event_type > LARGEST_STAT_VECTOR_INDEX) {
        printf("Illegal event_type= %d  in AddEvent.\n", event_type);
        HardwareInternalPanic(ERR_Z502_INTERNAL_BUG);
    }
    ep = (EVENT *) malloc(sizeof(EVENT));
    if (ep == NULL ) {
        printf("We didn't complete the malloc in AddEvent.\n");
        HardwareInternalPanic(ERR_OS502_GENERATED_BUG);
    }
    GetLock(EventLock, "AddEvent");

    ep->queue = (INT32 *) NULL;
    ep->time_of_event = time_of_event;
    ep->ring_buffer_location = event_ring_buffer_index;
    ep->structure_id = EVENT_STRUCTURE_ID;
    ep->event_type = event_type;
    ep->event_error = event_error;
    *returned_event_ptr = ep;

    erbi = event_ring_buffer_index;
    event_ring_buffer[erbi].time_of_request = CurrentSimulationTime;
    event_ring_buffer[erbi].expected_time_of_event = time_of_event;
    event_ring_buffer[erbi].real_time_of_event = -1;
    event_ring_buffer[erbi].event_type = event_type;
    event_ring_buffer[erbi].event_error = event_error;
    event_ring_buffer_index = (++erbi) % EVENT_RING_BUFFER_SIZE;

    temp_ptr = &EventQueue; /* The header queue  */
    temp_ptr->time_of_event = -1;
    last_ptr = temp_ptr;

    while (1) {
        if (temp_ptr->time_of_event > time_of_event) /* we're past */
        {
            ep->queue = last_ptr->queue;
            last_ptr->queue = (INT32 *) ep;
            break;
        }
        if (temp_ptr->queue == NULL ) {
            temp_ptr->queue = (INT32 *) ep;
            break;
        }
        last_ptr = temp_ptr;
        temp_ptr = (EVENT *) temp_ptr->queue;
    } /* End of while     */
    if (ReleaseLock(EventLock, "AddEvent") == FALSE)
        printf("Took error on ReleaseLock in AddEvent\n");
    // PrintEventQueue();
    if ((time_of_event > 0)
            && (time_of_event <= (INT32) CurrentSimulationTime)) {
        // Bugfix 09/2011 - There are situations where the hardware lock
        // is held in MemoryCommon - but we need to release it so that
        // we can do the signal and the interrupt thread will not be
        // stuck when it tries to get the lock.
        // Here we try to get the lock.  After we try, we will now
        //   hold the lock so we can then release it.
        GetTryLock(HardwareLock, "AddEventToIntQ");
        if (ReleaseLock(HardwareLock, "AddEvent") == FALSE)
            printf("Took error on ReleaseLock in AddEvent\n");
        SignalCondition(InterruptCondition, "AddEvent");
    }
    return;
}             // End of  AddEventToInterruptQueue

/*****************************************************************

 GetNextOrderedEvent()

 This is the routine that will remove an event from the queue.  
 Actions include:
 o Gets the next item from the event queue.
 o Fills in the return arguments.
 o Frees the structure.
 We come here only when we KNOW time is past.  We take an error
 if there's nothing on the queue.
 *****************************************************************/

void GetNextOrderedEvent(INT32 *time_of_event, INT16 *event_type,
        INT16 *event_error, INT32 *local_error)

{
    EVENT *ep;
    INT16 rbl; /* Ring Buffer Location                */

    GetLock(EventLock, "get_next_ordered_ev");
    if (EventQueue.queue == NULL ) {
        *local_error = ERR_Z502_INTERNAL_BUG;
        if (ReleaseLock(EventLock, "get_next_ordered_ev") == FALSE)
            printf("Took error on ReleaseLock in GetNextOrderedEvent\n");
        return;
    }
    ep = (EVENT *) EventQueue.queue;
    EventQueue.queue = ep->queue;
    ep->queue = NULL;

    if (ep->structure_id != EVENT_STRUCTURE_ID) {
        printf("Bad structure id read in GetNextOrderedEvent.\n");
        HardwareInternalPanic(ERR_Z502_INTERNAL_BUG);
    }
    *time_of_event = ep->time_of_event;
    *event_type = ep->event_type;
    *event_error = ep->event_error;
    *local_error = ERR_SUCCESS;
    rbl = ep->ring_buffer_location;

    if (event_ring_buffer[rbl].expected_time_of_event == *time_of_event)
        event_ring_buffer[rbl].real_time_of_event = CurrentSimulationTime;
//        else
//                printf( "XXX %d %d\n", CurrentSimulationTime, *time_of_event );

    if (ReleaseLock(EventLock, "GetNextOrderedEvent") == FALSE)
        printf("Took error on ReleaseLock in GetNextOrderedEvent\n");
    ep->structure_id = 0; /* make sure this isn't mistaken */
    free(ep);

}                       // End of GetNextOrderedEvent            

/*****************************************************************

 PrintEventQueue()

 Print out the times that are on the event Q.
 *****************************************************************/

void PrintEventQueue() {
    EVENT *ep;

    GetLock(EventLock, "PrintEventQueue");
    printf("Event Queue: ");
    ep = (EVENT *) EventQueue.queue;
    while (ep != NULL ) {
        printf("  %d", ep->time_of_event);
        ep = (EVENT *) ep->queue;
    }
    printf("  NULL\n");
    ReleaseLock(EventLock, "PrintEventQueue");
    return;
}             // End of PrintEventQueue            

/*****************************************************************

 DequeueItemFromEventQueue()

 Deque a specified item from the event queue.
 Actions include:
 o Start at the head of the queue.
 o Hunt along until we find a matching identifier.
 o Dequeue it.
 o Return the pointer to the structure to the caller.

 error not 0 means the event wasn't found;
 *****************************************************************/

void DequeueItemFromEventQueue(EVENT *event_ptr, INT32 *error) {
    EVENT *last_ptr;
    EVENT *temp_ptr;

    // It's possible that HardwareTimer will call us when it
    // thinks there's a timer event, but in fact the
    // HardwareInterrupt has handled the timer event.
    // in that case, the event_ptr may be null.
    // If so, eat the error.
    //if ( event_ptr == NULL )   {  // It's already Dequeued
    //    return;
    //}
    if (event_ptr->structure_id != EVENT_STRUCTURE_ID) {
        printf("Bad structure id read in DequeueItem.\n");
        HardwareInternalPanic(ERR_Z502_INTERNAL_BUG);
    }

    GetLock(EventLock, "DequeueItem");
    *error = 0;
    temp_ptr = (EVENT *) EventQueue.queue;
    last_ptr = &EventQueue;
    while (1) {
        if (temp_ptr == NULL ) {
            *error = 1;
            break;
        }
        if (temp_ptr == event_ptr) {
            last_ptr->queue = temp_ptr->queue;
            event_ptr->queue = (INT32 *) NULL;
            break;
        }
        last_ptr = temp_ptr;
        temp_ptr = (EVENT *) temp_ptr->queue;
    } /* End while                */
    if (ReleaseLock(EventLock, "DequeueItem") == FALSE)
        printf("Took error on ReleaseLock in DequeueItem\n");

}                     // End   DequeueItemFromEventQueue

/*****************************************************************

 GetNextEventTime()

 Look in the event queue.  Don't dequeue anything,
 but just read the time of the first event.

 return a -1 if there's nothing on the queue
 - the caller must check for this.
 *****************************************************************/

void GetNextEventTime(INT32 *time_of_next_event) {
    EVENT *ep;

    GetLock(EventLock, "GetNextEventTime");
    *time_of_next_event = -1;
    if (EventQueue.queue == NULL ) {
        if (ReleaseLock(EventLock, "GetNextEventTime") == FALSE)
            printf("Took error on ReleaseLock in GetNextEventTime\n");
        return;
    }
    ep = (EVENT *) EventQueue.queue;
    if (ep->structure_id != EVENT_STRUCTURE_ID) {
        printf("Bad structure id read in GetNextEventTime.\n");
        HardwareInternalPanic(ERR_Z502_INTERNAL_BUG);
    }
    *time_of_next_event = ep->time_of_event;
    if (ReleaseLock(EventLock, "GetNextEventTime") == FALSE)
        printf("Took error on ReleaseLock in GetNextEventTime\n");

}                   // End of GetNextEventTime    

/*****************************************************************

 PrintHardwareStats()

 This routine is called when the simulation halts.  It prints out
 the various usages of the hardware that have occurred during the
 simulation.
 *****************************************************************/

void PrintHardwareStats(void) {
    INT32 i, temp;
    double util; /* This is in range 0 - 1       */

    printf("Hardware Statistics during the Simulation\n");
    for (i = 0; i < MAX_NUMBER_OF_DISKS ; i++) {
        temp = HardwareStats.disk_reads[i] + HardwareStats.disk_writes[i];
        if (temp > 0) {
            printf("Disk %2d: Disk Reads = %5d: Disk Writes = %5d: ", i,
                    HardwareStats.disk_reads[i], HardwareStats.disk_writes[i]);
            util = (double) HardwareStats.time_disk_busy[i]
                    / (double) CurrentSimulationTime;
            printf("Disk Utilization = %6.3f\n", util);
        }
    }
    if (HardwareStats.number_faults > 0)
        printf("Faults = %5d:  ", HardwareStats.number_faults);
    if (HardwareStats.context_switches > 0)
        printf("Context Switches = %5d:  ", HardwareStats.context_switches);
    printf("CALLS = %5d:  ", HardwareStats.number_charge_times);
    printf("Masks = %5d\n", HardwareStats.number_mask_set_seen);

}               // End of PrintHardwareStats   
/*****************************************************************

 PrintRingBuffer()

 This is called by the panic code to print out what's
 been happening with events and interrupts.
 *****************************************************************/

void PrintRingBuffer(void) {
    INT16 index;
    INT16 next_print;

    if (event_ring_buffer[0].time_of_request == 0)
        return; /* Never used - ignore       */
    GetLock(EventLock, "PrintRingBuffer");
    next_print = event_ring_buffer_index;

    printf("Current time is %d\n\n", CurrentSimulationTime);
    printf("Record of Hardware Requests:\n\n");
    printf("This is a history of the last events that were requested.  It\n");
    printf("is a ring buffer so note that the times are not in order.\n\n");
    printf("Column A: Time at which the OS made a request of the hardware.\n");
    printf("Column B: Time at which the hardware was expected to give an\n");
    printf("          interrupt.\n");
    printf("Column C: The actual time at which the hardware caused an\n");
    printf("          interrupt.  This should be the same or later than \n");
    printf("          Column B.  If this number is -1, then the event \n");
    printf("          occurred because the request was superceded by a \n");
    printf("          later event.\n");
    printf("Column D: Device Type.  4 = Timer, 5... are disks \n");
    printf("Column E: Device Status.  0 indicates no error.  You should\n");
    printf("          worry about anything that's not 0.\n\n");
    printf("Column A    Column B      Column C      Column D   Column E\n\n");

    for (index = 0; index < EVENT_RING_BUFFER_SIZE; index++) {
        next_print++;
        next_print = next_print % EVENT_RING_BUFFER_SIZE;

        printf("%7d    %7d    %8d     %8d    %8d\n",
                event_ring_buffer[next_print].time_of_request,
                event_ring_buffer[next_print].expected_time_of_event,
                event_ring_buffer[next_print].real_time_of_event,
                event_ring_buffer[next_print].event_type,
                event_ring_buffer[next_print].event_error);
    }
    if (ReleaseLock(EventLock, "PrintRingBuffer") == FALSE)
        printf("Took error on ReleaseLock in PrintRingBuffer\n");

}                    // End of PrintRingBuffer

/*****************************************************************

 GetSectorStructure()

 Determine if the requested sector exists, and if so hand back the
 location in memory where we've stashed data for this sector.

 Actions include:
 o Hunt along the sector data until we find the appropriate sector.
 o Return the address of the sector data.

 Error not 0 means the structure wasn't found.  This means that
 noone has used this particular sector before and thus it hasn't
 been written to.
 *****************************************************************/

void GetSectorStructure(INT16 disk_id, INT16 sector, char **sector_ptr,
        INT32 *error) {
    SECTOR *temp_ptr;

    *error = 0;
    temp_ptr = (SECTOR *) sector_queue[disk_id].queue;
    while (1) {
        if (temp_ptr == NULL ) {
            *error = 1;
            break;
        }

        if (temp_ptr->structure_id != SECTOR_STRUCTURE_ID) {
            printf("Bad structure id read in GetSectorStructure.\n");
            HardwareInternalPanic(ERR_Z502_INTERNAL_BUG);
        }

        if (temp_ptr->sector == sector) {
            *sector_ptr = (temp_ptr->sector_data);
            break;
        }
        temp_ptr = (SECTOR *) temp_ptr->queue;
    }        // End while
}                // End GetSectorStructure

/*****************************************************************

 CreateSectorStruct()

 This is the routine that will create a sector structure and add it
 to the list of valid sectors.

 Actions include:
 o Allocate a structure for the event.
 o Fill in the structure.
 o Enqueue it.
 o Pass back the pointer to the sector data.

 WARNING: NO CHECK is made to ensure a structure for this sector
 doesn't exist.  The assumption is that the caller has previously
 failed a call to GetSectorStructure.
 *****************************************************************/

void CreateSectorStruct(INT16 disk_id, INT16 sector, char **returned_sector_ptr) {
    SECTOR *ssp;

    ssp = (SECTOR *) malloc(sizeof(SECTOR));
    if (ssp == NULL ) {
        printf("We didn't complete the malloc in CreateSectorStruct.\n");
        printf("A malloc returned with a NULL pointer.\n");
        HardwareInternalPanic(ERR_OS502_GENERATED_BUG);
    }
    ssp->structure_id = SECTOR_STRUCTURE_ID;
    ssp->disk_id = disk_id;
    ssp->sector = sector;
    *returned_sector_ptr = (ssp->sector_data);

    /* Enqueue this new structure on the HEAD of the queue for this disk   */
    ssp->queue = sector_queue[disk_id].queue;
    sector_queue[disk_id].queue = (INT32 *) ssp;

}                                    // End of CreateSectorStruct

/**************************************************************************
 **************************************************************************
 THREAD MANAGER
 What follows is a series of routines that manage the threads and
 synchronization for both Windows and LINUX.

 PrintThreadTable - Used for debugging - prints out all the info for
 the thread table that contains all thread info.
 Z502CreateUserThread - Called only by test.c to establish the initial 
 threads that will later have the ability to act as processes.

 +++ These routines associate a thread with a context and run it +++
 Z502PrepareProcessForExecution - A thread has been created.  Here we give
 it the ability to be suspended by assigning it a Condition
 and a Mutex.  We then suspend it until it's ready to be
 scheduled.
 AssociateContextWithProcess - Called by Z502MakeContext - it takes the thread
 to the next stage of preparation by giving it a context.
 Meanwhile, the thread continues to be Suspended.

 ResumeProcessExecution - A thread tells the target thread to wake up.  At
 this point in Rev 4.0, that thread will then suspend itself.
 SuspendProcessExecution - Suspend ourself.  A thread that wants to resume
 us can do so because it can find our Context, our Condition,
 and our Mutex.
 CreateAThread - Called by Z502CreateUserThread and also by the Z502 in
 order to create the thread used as the interrupt thread.
 DestroyThread - There's code here to destroy a thread, but in Rev 4.0
 noone is calling it and its success is unknown.
 ChangeThreadPriority - Used by Z502Init to change the priority of the user
 thread.  Perhaps ALL user threads should have their
 priority adjusted!
 GetMyTid -     Returns the thread ID of the calling thread.
 BaseThread -   Currently not used - Probably the logic is no longer correct
 given multiple threads in Rev 4.0.

 **************************************************************************
 **************************************************************************/

/**************************************************************************
 PrintThreadTable
 Used for debugging.  Prints out the state of the user threads - where
 they are in the creation process.  Once they get going, we assume
 that the threads will take care of themselves.
 have the ability to act as processes.

 **************************************************************************/
void PrintThreadTable(char *Explanation) {
#ifdef     DEBUG_USER_THREADS
    int i = 0;
    printf("\n\n");
    printf("%s", Explanation);
    for (i = 0; i < MAX_NUMBER_OF_USER_THREADS; i++) {
        if (ThreadTable[i].CurrentState > 2) {
            printf(
                    "LocalID: %d  ThreadID:  %d   CurrentState:  %d   Context:  %x  Condition: %d   Mutex  %d\n",
                    ThreadTable[i].OurLocalID, ThreadTable[i].ThreadID,
                    ThreadTable[i].CurrentState,(unsigned long) ThreadTable[i].Context,
                    ThreadTable[i].Condition, ThreadTable[i].Mutex);
        }
    }
#endif
}                           // End of PrintThreadTable

/**************************************************************************
 Z502CreateUserThread
 Called only by test.c to establish the initial threads that will later
 have the ability to act as processes.
 We set up a table that associates our local thread number, the ThreadID
 of the thread, and later when we have a CONTEXT, we associate that
 as well.
 The thread that's created goes off to whereever it was supposed to start.
 **************************************************************************/

void Z502CreateUserThread(void *ThreadStartAddress) {
    int ourLocalID = -1;
    int i;
    // If this is our first time in the hardware, do some initializations
    if (Z502Initialized == FALSE)
        Z502Init();
    GetLock(ThreadTableLock, "Z502CreateUserThread");
    // Find out the first uninitialized thread
    for (i = 0; i < MAX_NUMBER_OF_USER_THREADS; i++) {
        if (ThreadTable[i].OurLocalID == -1) {
            ourLocalID = i;
            break;
        }
    }
    // We are responsible for the call of this routine in main of test.c,
    // so if we run out of structures, it's our own fault.  So call this a
    // hardware error.
    if (ourLocalID == -1) {
        printf("Error in Z502CreateUserThread\n");
        HardwareInternalPanic(ERR_Z502_INTERNAL_BUG);
    }

    ThreadTable[ourLocalID].OurLocalID = ourLocalID;
    ThreadTable[ourLocalID].ThreadID = CreateAThread(ThreadStartAddress,
            &ourLocalID);
    ThreadTable[ourLocalID].Context = (Z502CONTEXT *) -1;
    ThreadTable[ourLocalID].CurrentState = CREATED;
    PrintThreadTable("Z502CreateUserThread\n");
    ReleaseLock(ThreadTableLock, "Z502CreateUserThread");
}                          // End of Z502CreateUserThread

/**************************************************************************
 Z502PrepareProcessForExecution()
 A new thread is generated by Z502CreateUserThread - called by main in
 test.c.  That starts up the thread, again at a routine in test.c.
 That new thread immediately calls in here.
 We perform the following actions here:

 1.  Check that we indeed know about this thread - bogus actions do occur.
 2.  Set the CurrentState as SUSPENDED_WAITING_FOR_CONTEXT.
 3.  Do a CreateLock
 4.  Do a CreateCondition
 5.  Suspend the thread by doing a WaitForCondition()
 6.  Unknown to this code, the Z502MakeContext code will call
 AssociateContextWithProcess() which will look in the
 ThreadTable and find a thread with state SUSPENDED_WAITING_FOR_CONTEXT.
 7.  Z502MakeContext will fill in the context in ThreadTable and set a
 thread's CurrentState to SUSPENDED_WAITING_FOR_FIRST_SCHED.
 8.  SwitchContext will match the context it has to the context matched
 with a particular thread.  That thread should have
 SUSPENDED_WAITING_FOR_FIRST_SCHED if it came through this code.
 9.  SwitchContext sets CurrentState to ACTIVE.
 10. SwitchContext does a SignalCondition on this thread.
 11. That means the thread continues in THIS routine.
 12. The thread looks in its Context, finds the address where it is
 to execute, and returns that address to the caller in test.c
 **************************************************************************/
void *Z502PrepareProcessForExecution() {
    int myTid = GetMyTid();
    int ourLocalID = -1;
    int i;
    UINT32 RequestedCondition;
    INT32 RequestedMutex;

    GetLock(ThreadTableLock, "Z502PrepareProcessForExecution");
    PrintThreadTable("Entering -> PrepareProcessForExecution\n");
    // Find my TID in the table & make sure all is OK
    for (i = 0; i < MAX_NUMBER_OF_USER_THREADS; i++) {
        if (ThreadTable[i].ThreadID == myTid) {
            ourLocalID = i;
            break;
        }
    }
    if (ourLocalID == -1) {
        printf("Error 1 in Z502PrepareProcessForExecution\n");
        HardwareInternalPanic(ERR_Z502_INTERNAL_BUG);
    }

    // Set our state here
    ThreadTable[ourLocalID].CurrentState = SUSPENDED_WAITING_FOR_CONTEXT;

    // And get a condition that we'll wait on and a lock
    CreateCondition(&RequestedCondition);
    ThreadTable[ourLocalID].Condition = RequestedCondition;
    CreateLock(&RequestedMutex, "Z502PrepareProcessForExecution");
    ThreadTable[ourLocalID].Mutex = RequestedMutex;
    ReleaseLock(ThreadTableLock, "Z502PrepareProcessForExecution");
    // Suspend ourselves and don't wake up until we're ready to do real work
    while (ThreadTable[ourLocalID].CurrentState == SUSPENDED_WAITING_FOR_CONTEXT) {
        //ReleaseLock( ThreadTableLock, "Z502PrepareProcessForExecution" );
        WaitForCondition(ThreadTable[ourLocalID].Condition,
                ThreadTable[ourLocalID].Mutex, 30,
                "Z502PrepareProcessForExecution");
    }
    // Now "magically", when we are awakened, we have a Context associated
    // with us and our state should be  ACTIVE
    GetLock(ThreadTableLock, "Z502PrepareProcessForExecution");
    PrintThreadTable("Exiting -> PrepareProcessForExecution\n");
    if (ThreadTable[ourLocalID].Context == (Z502CONTEXT *) -1) {
        printf("Error 2 in Z502PrepareProcessForExecution\n");
        HardwareInternalPanic(ERR_Z502_INTERNAL_BUG);
    }
    ReleaseLock(ThreadTableLock, "Z502PrepareProcessForExecution");
    return (void *) ThreadTable[ourLocalID].Context->entry;
}                                       // End of Z502PrepareProcessForExecution

/**************************************************************************
 AssociateContextWithProcess

 Find a thread that's waiting for a Context and give it one.
 **************************************************************************/
void AssociateContextWithProcess(Z502CONTEXT *Context) {
    int ourLocalID = -1;
    int i;
    //GetLock( ThreadTableLock, "AssociateContextWithProcess" );
    PrintThreadTable("Entering -> AssociateContextWithProcess\n");
    // Find a thread that needs a context
    for (i = 0; i < MAX_NUMBER_OF_USER_THREADS; i++) {
        if (ThreadTable[i].CurrentState == SUSPENDED_WAITING_FOR_CONTEXT) {
            ourLocalID = i;
            break;
        }
    }
    if (ourLocalID == -1) {
        printf("Error in AssociateContextWithProcess()\n");
        HardwareInternalPanic(ERR_Z502_INTERNAL_BUG);
    }
    ThreadTable[ourLocalID].Context = Context;
    ThreadTable[ourLocalID].CurrentState = SUSPENDED_WAITING_FOR_FIRST_SCHED;
    PrintThreadTable("Exiting -> AssociateContextWithProcess\n");
    //ReleaseLock( ThreadTableLock, "AssociateContextWithProcess" );
}                          // End of AssociateContextWithProcess

/**************************************************************************
 ResumeProcessExecution

 This wakes up a target thread
 **************************************************************************/
void ResumeProcessExecution(Z502CONTEXT *Context) {
    int ourLocalID = -1;
    int i;
    // Find target Context in the table & make sure all is OK
    GetLock(ThreadTableLock, "ResumeProcessExecution");
    for (i = 0; i < MAX_NUMBER_OF_USER_THREADS; i++) {
        if (ThreadTable[i].Context == Context) {
            ourLocalID = i;
            break;
        }
    }
    if (ourLocalID == -1) {
        printf("Error in ResumeProcessExecuton\n");
        HardwareInternalPanic(ERR_Z502_INTERNAL_BUG);
    }
    ThreadTable[ourLocalID].CurrentState = ACTIVE;
    PrintThreadTable("ResumeProcessExecution\n");
    SignalCondition(ThreadTable[ourLocalID].Condition,
            "ResumeProcessExecution");
    ReleaseLock(ThreadTableLock, "ResumeProcessExecution");
}                               // End of ResumeProcessExecution

/**************************************************************************
 SuspendProcessExecution

 This suspends a thread - most likely ourselves - don't know about that
 **************************************************************************/
void SuspendProcessExecution(Z502CONTEXT *Context) {
    int ourLocalID = -1;
    int i;
    UINT32 RequestedCondition;
    INT32 RequestedMutex;
    // If the context we're handed here is NULL (0), then it's the
    // original thread that we were running on when the program started.
    // Get a condition and a lock, and suspend ourselves forever
    //GetLock( ThreadTableLock, "SuspendProcessExecution" );
    if (Context == NULL ) {
        // And get a condition that we'll wait on and a lock
        CreateCondition(&RequestedCondition);
        //ThreadTable[ourLocalID].Condition = RequestedCondition;
        CreateLock(&RequestedMutex, "SuspendProcessExecution");
        //ThreadTable[ourLocalID].Mutex = RequestedMutex;
        // Note that the wait time is not used by this callee
        WaitForCondition(RequestedCondition, RequestedMutex, -1,
                "SuspendProcessExecution");
        printf("SERIOUS ERROR:  The initial thread has become unsuspended\n");
        return;
    }
    // Find target Context in the table & make sure all is OK
    for (i = 0; i < MAX_NUMBER_OF_USER_THREADS; i++) {
        if (ThreadTable[i].Context == Context) {
            ourLocalID = i;
            break;
        }
    }
    if (ourLocalID == -1) {
        printf("Error in SuspendProcessExecution\n");
        HardwareInternalPanic(ERR_Z502_INTERNAL_BUG);
    }
    PrintThreadTable("SuspendProcessExecution\n");
    //ReleaseLock( ThreadTableLock, "SuspendProcessExecution" );
    WaitForCondition(ThreadTable[ourLocalID].Condition,
            ThreadTable[ourLocalID].Mutex, 30, "SuspendProcessExecution");
}
/**************************************************************************
 CreateAThread
 There are Linux and Windows dependencies here.  Set up the threads
 for the two systems.
 We return the Thread Handle to the caller.
 **************************************************************************/

int CreateAThread(void *ThreadStartAddress, INT32 *data) {
#ifdef  NT
    DWORD ThreadID;
    HANDLE ThreadHandle;
    if ((ThreadHandle = CreateThread(NULL, 0,
            (LPTHREAD_START_ROUTINE) ThreadStartAddress, (LPVOID) data,
            (DWORD) 0, &ThreadID)) == NULL ) {
        printf("Unable to create thread in CreateAThread\n");
        GoToExit(0);
    }
    return ((int) ThreadID);
#endif

#if defined LINUX || defined MAC
    int ReturnCode;
    // int                  SchedPolicy = SCHED_FIFO;   // not used
    int policy;
    struct sched_param param;
    pthread_t Thread;
    pthread_attr_t Attribute;

    ReturnCode = pthread_attr_init( &Attribute );
    if ( ReturnCode != FALSE )
    printf( "Error in pthread_attr_init in CreateAThread\n" );
    ReturnCode = pthread_attr_setdetachstate( &Attribute, PTHREAD_CREATE_JOINABLE );
    if ( ReturnCode != FALSE )
    printf( "Error in pthread_attr_setdetachstate in CreateAThread\n" );
    ReturnCode = pthread_create( &Thread, &Attribute, ThreadStartAddress, data );
    if ( ReturnCode == EINVAL ) /* Will return 0 if successful */
    printf( "ERROR doing pthread_create - The Thread, attr or sched param is wrong\n");
    if ( ReturnCode == EAGAIN ) /* Will return 0 if successful */
    printf( "ERROR doing pthread_create - Resources not available\n");
    if ( ReturnCode == EPERM ) /* Will return 0 if successful */
    printf( "ERROR doing pthread_create - No privileges to do this sched type & prior.\n");

    ReturnCode = pthread_attr_destroy( &Attribute );
    if ( ReturnCode ) /* Will return 0 if successful */
    printf( "Error in pthread_mutexattr_destroy in CreateAThread\n" );
    ReturnCode = pthread_getschedparam( Thread, &policy, &param);
    return( (int)Thread );
#endif
}                                // End of CreateAThread 
/**************************************************************************
 DestroyThread
 **************************************************************************/

void DestroyThread(INT32 ExitCode) {
#ifdef   NT
    ExitThread((DWORD) ExitCode);
#endif

#if defined LINUX || defined MAC
//    struct  thr_rtn  msg;
//    strcpy( msg.out_msg, "");
//    msg.completed = ExitCode;
//    pthread_exit( (void *)msg );
#endif
} /* End of DestroyThread   */

/**************************************************************************
 ChangeThreadPriority
 On LINUX, there are two kinds of priority.  The static priority
 requires root privileges so we aren't doing that here.
 The dynamic priority changes based on various scheduling needs -
 we have modist control over the dynamic priority.
 On LINUX, the dynamic priority ranges from -20 (most favorable) to
 +20 (least favorable) so we add and subtract here appropriately.
 Again, in our lab without priviliges, this thread can only make
 itself less favorable.
 For Windows, we will be using two classifications here:
 THREAD_PRIORITY_ABOVE_NORMAL
 Indicates 1 point above normal priority for the priority class.
 THREAD_PRIORITY_BELOW_NORMAL
 Indicates 1 point below normal priority for the priority class.

 **************************************************************************/

void ChangeThreadPriority(INT32 PriorityDirection) {
#ifdef   NT
    INT32 ReturnValue;
    HANDLE MyThreadID;
    MyThreadID = GetCurrentThread();
    if (PriorityDirection == MORE_FAVORABLE_PRIORITY)
        ReturnValue = (INT32) SetThreadPriority(MyThreadID,
                THREAD_PRIORITY_ABOVE_NORMAL);
    if (PriorityDirection == LESS_FAVORABLE_PRIORITY)
        ReturnValue = (INT32) SetThreadPriority(MyThreadID,
                THREAD_PRIORITY_BELOW_NORMAL);
    if (ReturnValue == 0) {
        printf("ERROR:  SetThreadPriority failed in ChangeThreadPriority\n");
        HandleWindowsError();
    }

#endif
#if defined LINUX || defined MAC
    // 09/2011 - I have attempted to make the interrupt thread a higher priority
    // than the base thread but have not been successful.  It's possible to change
    // the "nice" value for the whole process, but not for individual threads.
    //int                  policy;
    //struct sched_param   param;
    //int                  CurrentPriority;
    //CurrentPriority = getpriority( PRIO_PROCESS, 0 );
    //ReturnValue = setpriority( PRIO_PROCESS, 0, CurrentPriority - PriorityDirection );
    //ReturnValue = setpriority( PRIO_PROCESS, 0, 15 );
    //CurrentPriority = getpriority( PRIO_PROCESS, 0 );
    //ReturnValue = pthread_getschedparam( GetMyTid(), &policy, &param);

    //if ( ReturnValue == ESRCH || ReturnValue == EINVAL || ReturnValue == EPERM )
    //    printf( "ERROR in ChangeThreadPriority - Input parameters are wrong\n");
    //if ( ReturnValue == EACCES )
    //    printf( "ERROR in ChangeThreadPriority - Not privileged to do this!!\n");

#endif
}                         // End of ChangeThreadPriority   

/**************************************************************************
 GetMyTid
 Returns the current Thread ID
 **************************************************************************/
int GetMyTid() {
#ifdef   NT
    return ((int) GetCurrentThreadId());
#endif
#ifdef   LINUX
    return( (int)pthread_self() );
#endif
#ifdef   MAC
    return( (unsigned long int)pthread_self() );
#endif
}                                   // End of GetMyTid

/**************************************************************************
 BaseThread
 Returns TRUE if the caller is the base thread,
 FALSE if not (for instance if it's the interrupt thread).
 **************************************************************************/
int BaseThread() {
    if (GetMyTid() == BaseTid)
        return (TRUE );
    return (FALSE );
}                                    // End of BaseThread

/**************************************************************************
 **************************************************************************
 LOCKS MANAGER
 What follows is a series of routines that manage the threads and
 synchronization for both Windows and LINUX.

 CreateLock  - Generate a new lock.
 GetTryLock - GetTryLock tries to get a lock.  If it is successful, it 
 returns 1.  If it is not successful, and the lock is held by someone
 else, a 0 is returned.
 GetLock     - Can handle locks for either Windows or LINUX.
 Assumes the lock has been established by the caller.
 ReleaseLock - Can handle locks for either Windows or LINUX.
 Assumes the lock has been established by the caller.
 PrintLockDebug - prints entrances and exits to the locks.

 **************************************************************************
 **************************************************************************/
#define     LOCK_ENTER              1
#define     LOCK_EXIT               0

#define     LOCK_CREATE             0
#define     LOCK_TRY                1
#define     LOCK_GET                2
#define     LOCK_RELEASE            3

/**************************************************************************
 CreateLock
 **************************************************************************/
void CreateLock(INT32 *RequestedMutex, char *CallingRoutine) {
    int ErrorFound = FALSE;
#ifdef NT
    HANDLE MemoryMutex;
    // Create with no security, no initial ownership, and no name
    if ((MemoryMutex = CreateMutex(NULL, FALSE, NULL )) == NULL )
        ErrorFound = TRUE;
    *RequestedMutex = (UINT32) MemoryMutex;
#endif
#if defined LINUX || defined MAC

    pthread_mutexattr_t Attribute;

    ErrorFound = pthread_mutexattr_init( &Attribute );
    if ( ErrorFound != FALSE )
    printf( "Error in pthread_mutexattr_init in CreateLock\n" );
    ErrorFound = pthread_mutexattr_settype( &Attribute, PTHREAD_MUTEX_ERRORCHECK_NP );
    if ( ErrorFound != FALSE )
    printf( "Error in pthread_mutexattr_settype in CreateLock\n" );
    ErrorFound = pthread_mutex_init( &(LocalMutex[NextMutexToAllocate]), &Attribute );
    if ( ErrorFound ) /* Will return 0 if successful */
    printf( "Error in pthread_mutex_init in CreateLock\n" );
    ErrorFound = pthread_mutexattr_destroy( &Attribute );
    if ( ErrorFound ) /* Will return 0 if successful */
    printf( "Error in pthread_mutexattr_destroy in CreateLock\n" );
    *RequestedMutex = NextMutexToAllocate;
    NextMutexToAllocate++;
#endif
    if (ErrorFound == TRUE) {
        printf("We were unable to create a mutex in CreateLock\n");
        GoToExit(0);
    }
    PrintLockDebug(LOCK_CREATE, CallingRoutine, *RequestedMutex, LOCK_EXIT);
}                               // End of CreateLock

/**************************************************************************
 GetTryLock
 GetTryLock tries to get a lock.  If it is successful, it returns 1.
 If it is not successful, and the lock is held by someone else,
 a 0 is returned.

 The pthread_mutex_trylock() function tries to lock the specified mutex.
 If the mutex is already locked, an error is returned.
 Otherwise, this operation returns with the mutex in the locked state
 with the calling thread as its owner.
 **************************************************************************/

int GetTryLock(UINT32 RequestedMutex, char *CallingRoutine) {
    int ReturnValue = FALSE;
    int LockReturn;
#ifdef   NT
    HANDLE MemoryMutex;
#endif

    PrintLockDebug(LOCK_TRY, CallingRoutine, RequestedMutex, LOCK_ENTER);
#ifdef   NT
    MemoryMutex = (HANDLE) RequestedMutex;
    LockReturn = (int) WaitForSingleObject(MemoryMutex, 1);
//      printf( "Code Returned in GetTryLock is %d\n", LockReturn );
    if (LockReturn == WAIT_FAILED ) {
        printf("Internal error in GetTryLock\n");
        HandleWindowsError();
        GoToExit(0);
    }
    if (LockReturn == WAIT_TIMEOUT)   // Timeout occurred with no lock
        ReturnValue = FALSE;
    if (LockReturn == WAIT_OBJECT_0)   // Lock was obtained
        ReturnValue = TRUE;
#endif
#if defined LINUX || defined MAC
    LockReturn = pthread_mutex_trylock( &(LocalMutex[RequestedMutex]) );
//    printf( "Code Returned in GetTRyLock is %d\n", LockReturn );

    if ( LockReturn == EINVAL )
    printf( "PANIC in GetTryLock - mutex isn't initialized\n");
    if ( LockReturn == EFAULT )
    printf( "PANIC in GetTryLock - illegal address for mutex\n");
    if ( LockReturn == EBUSY )//  Already locked by another thread
    ReturnValue = FALSE;
    if ( LockReturn == EDEADLK )//  Already locked by this thread
    ReturnValue = TRUE;// Here we eat this error
    if ( LockReturn == 0 )//  Not previously locked - all OK
    ReturnValue = TRUE;
#endif
    PrintLockDebug(LOCK_TRY, CallingRoutine, RequestedMutex, LOCK_EXIT);
    return (ReturnValue);
}                    // End of GetTryLock     

/**************************************************************************
 GetLock
 This routine should normally only return when the lock has been gotten.
 It should normally return TRUE - and returns FALSE only on an error.
 Note that Windows and Linux operate differently here.  Windows does
 NOT take an error if a lock attempt is made by a thread already holding
 the lock.
 **************************************************************************/

int GetLock(UINT32 RequestedMutex, char *CallingRoutine) {
    INT32 LockReturn;
    int ReturnValue = FALSE;
#ifdef   NT
    HANDLE MemoryMutex = (HANDLE) RequestedMutex;
#endif
    PrintLockDebug(LOCK_GET, CallingRoutine, RequestedMutex, LOCK_ENTER);
#ifdef   NT
    LockReturn = WaitForSingleObject(MemoryMutex, INFINITE);
    if (LockReturn != 0) {
        printf("Internal error waiting for a lock in GetLock\n");
        HandleWindowsError();
        GoToExit(0);
    }
    if (LockReturn == 0)    //  Not previously locked - all OK
        ReturnValue = TRUE;
#endif

#if defined LINUX || defined MAC
//        printf( "GetLock %d\n",  LocalMutex[RequestedMutex].__data.__owner );
//        if ( LocalMutex[RequestedMutex].__data.__owner > 0 )   {
//            printf("GetLock:  %d %d %d\n", RequestedMutex, 
//                    (int)LocalMutex[RequestedMutex], GetMyTid() );
//        }
    LockReturn = pthread_mutex_lock( &(LocalMutex[RequestedMutex]) );
    if ( LockReturn == EINVAL )
    printf( "PANIC in GetLock - mutex isn't initialized\n");
    if ( LockReturn == EFAULT )
    printf( "PANIC in GetLock - illegal address for mutex\n");

    // Note that LINUX acts differently from Windows.  We will eat the
    // error here to get compatibility.
    if ( LockReturn == EDEADLK ) {  //  Already locked by this thread
        LockReturn = 0;
        // printf( "ERROR - Already locked by this thread\n");
    }
    if ( LockReturn == 0 )  //  Not previously locked - all OK
    ReturnValue = TRUE;
#endif
    PrintLockDebug(LOCK_GET, CallingRoutine, RequestedMutex, LOCK_EXIT);
    return (ReturnValue);
}                              // End of GetLock  

/**************************************************************************
 ReleaseLock
 If the function succeeds, the return value is TRUE.
 If the function fails, and the Mutex is NOT unlocked,
 then FALSE is returned.
 **************************************************************************/
int ReleaseLock(UINT32 RequestedMutex, char* CallingRoutine) {
    int ReturnValue = FALSE;
    int LockReturn;
#ifdef   NT
    HANDLE MemoryMutex = (HANDLE) RequestedMutex;
#endif
    PrintLockDebug(LOCK_RELEASE, CallingRoutine, RequestedMutex, LOCK_ENTER);

#ifdef   NT
    LockReturn = ReleaseMutex(MemoryMutex);

    if (LockReturn != 0)    // Lock was released
        ReturnValue = TRUE;
#endif
#if defined LINUX || defined MAC
    LockReturn = pthread_mutex_unlock( &(LocalMutex[RequestedMutex]) );
//    printf( "Return Code in Release Lock = %d\n", LockReturn );

    if ( LockReturn == EINVAL )
    printf( "PANIC in ReleaseLock - mutex isn't initialized\n");
    if ( LockReturn == EFAULT )
    printf( "PANIC in ReleaseLock - illegal address for mutex\n");
    if ( LockReturn == EPERM )//  Not owned by this thread
    printf( "ERROR - Lock is not currently locked by this thread.\n");
    if ( LockReturn == 0 )//  Successfully unlocked - all OK
    ReturnValue = TRUE;
#endif
    PrintLockDebug(LOCK_RELEASE, CallingRoutine, RequestedMutex, LOCK_EXIT);
    return (ReturnValue);
}            // End of ReleaseLock    
/**************************************************************************
 PrintLockDebug
 Print out message indicating what's happening with locks
 This code updated 06/2013 to account for many possible user/base level
 threads
 **************************************************************************/
#define     MAX_NUMBER_OF_LOCKS    64
#define     MAX_NUMBER_OF_LOCKERS  10

typedef struct {
    int NumberOfLocks;
    int LockID[MAX_NUMBER_OF_LOCKS];
    int LockCount[MAX_NUMBER_OF_LOCKS];
    //int LockOwner[MAX_NUMBER_OF_LOCKS];
    int LockOwners[MAX_NUMBER_OF_LOCKS][MAX_NUMBER_OF_LOCKERS];
} LOCKING_DB;

INT32 PLDLock;
LOCKING_DB LockDB;
void Quickie(int LockID, int x) {
    int j;
    for (j = 0; j < MAX_NUMBER_OF_LOCKERS - 1; j++) {
        printf("%X  ",LockDB.LockOwners[LockID][j]);
    };
    printf(" %d\n", x);
}
void PrintLockDebug(int Action, char *LockCaller, int Mutex, int Return) {
#ifdef  DEBUG_LOCKS
    int MyTid, i, j, LockID;
    char TaskID[120], WhichLock[120];
    char Direction[120];
    char Output[120];
    char ProblemReport[120];           // Reports something not-normal
    char LockAction[120];              // What type of lock is calling us
    static int FirstTime = TRUE;

    // Don't do anything if we're logging ourself
    if ( strncmp(LockCaller, "PLD", 3) == 0 )
        return;
    // This is the initialization the first time we come in here
    if (FirstTime) {
        FirstTime = FALSE;
        CreateLock( &PLDLock, "PLD");
        for (i = 0; i < MAX_NUMBER_OF_LOCKS; i++) {
            LockDB.LockCount[i] = 0;
            //    LockDB.LockOwner[i] = 0;
            LockDB.LockID[i] = 0;
            for (j = 0; j < MAX_NUMBER_OF_LOCKERS; j++) {
                LockDB.LockOwners[i][j] = 0;
            }
        }
        LockDB.NumberOfLocks = 0;
    }

    GetLock(PLDLock, "PLD");
    // Find the string for the type of lock we're doing.
    if (Action == LOCK_CREATE)
        strcpy(LockAction, "CreLock");
    if (Action == LOCK_TRY)
        strcpy(LockAction, "TryLock");
    if (Action == LOCK_GET)
        strcpy(LockAction, "GetLock");
    if (Action == LOCK_RELEASE)
        strcpy(LockAction, "RelLock");

    // Determine if we are "Base" or "interrupt" - Actually there are many
    // Possible base threads - and we will print out which one.
    MyTid = GetMyTid();
    strcpy(TaskID, "Base ");
    if (MyTid == InterruptTid) {
        strcpy(TaskID, "Int  ");
    }

    // Identify which lock we're working on.  Some of the common ones are named,
    //   But many are associated with individual threads, or with locks created
    //   by students in which case they will be named  "Oth...".

    sprintf(WhichLock, "Oth%d   ", Mutex);
    if (Mutex == EventLock)
        strcpy(WhichLock, "Event  ");
    if (Mutex == InterruptLock)
        strcpy(WhichLock, "Int    ");
    if (Mutex == HardwareLock)
        strcpy(WhichLock, "Hard   ");
    if (Mutex == ThreadTableLock)
        strcpy(WhichLock, "T-Tbl  ");

    // We maintain a record of all locks in the order in which they are first
    //  accessed here.  The order doesn't matter, since we can identify that
    //  lock every time we enter here.
    LockID = -1;
    for (i = 0; i < LockDB.NumberOfLocks; i++) {
        if (LockDB.LockID[i] == Mutex)
            LockID = i;
    }
    if (LockID == -1) {
        LockDB.LockID[LockDB.NumberOfLocks] = Mutex;
        LockDB.NumberOfLocks++;
    }

    strcpy(ProblemReport, "");
    // We are ENTERING a TryLock OR Lock - record everything
    // We have the following situations:
    // Case 1: The Lock is not currently held
    //         GOOD - increment the count and record the thread
    // Case 2: The lock is currently held by the new requester
    //         BAD - because we'll have to release it multiple times
    // Case 3: The lock is currently held by someone else
    //         OK - we assume the current holder will release it -
    //         after all, this is what locks are for.
    if (Return == LOCK_ENTER && (Action == LOCK_TRY || Action == LOCK_GET)) {
        strcpy(Direction, " Enter");
        LockDB.LockCount[LockID]++;
        if (LockDB.LockCount[LockID] > MAX_NUMBER_OF_LOCKERS) {
            LockDB.LockCount[LockID] = MAX_NUMBER_OF_LOCKERS;
            printf("LOCK: BAD:  Exceeding number of lockers\n");
        }

        if (LockDB.LockCount[LockID] == 1) {    // Case 1: First lock
            LockDB.LockOwners[LockID][LockDB.LockCount[LockID] - 1] = MyTid;
        } else {                                   // Already locked
            if (MyTid == LockDB.LockOwners[LockID][0]) { // Case 2: Already owned by me - BAD
                sprintf(ProblemReport,
                        "LOCK: BAD#1: Thread %X is RELOCKING %s:  Count = %d\n",
                        MyTid, WhichLock, LockDB.LockCount[LockID]);
            }
            if (MyTid != LockDB.LockOwners[LockID][0]) { // Case 3: owned by someone else - OK
                sprintf(ProblemReport,
                        "LOCK: OK: Thread %X is LOCKING %s Held by %X:  Count = %d\n",
                        MyTid, WhichLock, LockDB.LockOwners[LockID][0],
                        LockDB.LockCount[LockID]);
                LockDB.LockOwners[LockID][LockDB.LockCount[LockID] - 1] = MyTid;
            }
        }           // Lock count NOT 1
    }               // End of entering LOCK

    // We are ENTERING a ReleaseLock
    // We have the following situations:
    // Case 1: The Lock is  currently held by the releaser
    //         GOOD - decrement the count - if there's someone waiting,
    //         do some bookkeeping
    // Case 1A: The lock count is still > 0 - it's still locked.  Report it.
    // Case 2: The lock is currently held by someone else
    //         BAD - We shouldn't be releasing a lock we don't hold
    // Case 3: The lock is currently not held
    //         BAD - this should NOT happen

    if (Return == LOCK_ENTER && Action == LOCK_RELEASE) {
        strcpy(Direction, " Enter");
        Quickie(LockID, 1);

        // Case 1: A thread is releasing a lock it holds
        if (LockDB.LockCount[LockID] > 0
                && (MyTid == LockDB.LockOwners[LockID][0])) {
            LockDB.LockCount[LockID]--;

            for (j = 0; j < MAX_NUMBER_OF_LOCKERS - 1; j++) {
                LockDB.LockOwners[LockID][j] = LockDB.LockOwners[LockID][j + 1];
            }
            for (j = 0; j < MAX_NUMBER_OF_LOCKERS - 1; j++) {
                if (j >= LockDB.LockCount[LockID])
                    LockDB.LockOwners[LockID][j] = 0;
            }
            //Quickie(LockID, 2);
            // If I've done multiple locks, there may be nobody else locking
            if (LockDB.LockOwners[LockID][0] == 0)
                LockDB.LockOwners[LockID][0] = MyTid;

            if (LockDB.LockCount[LockID] > 0) {    // Case 1A:
                if (MyTid == LockDB.LockOwners[LockID][0]) {
                    //Quickie(LockID, 3);
                    sprintf(ProblemReport,
                            "LOCK: BAD#2: Thread %X is RELEASING %s  But count is = %d\n",
                            MyTid, WhichLock, LockDB.LockCount[LockID]);
                }
            }
        }    // End of Case 1

        // Case 2: Make sure a thread only releases a lock it holds
        else if (LockDB.LockCount[LockID] > 0
                && (MyTid != LockDB.LockOwners[LockID][0])) {
            Quickie(LockID, 3);
            sprintf(ProblemReport,
                    "LOCK: BAD#3: Thread %X is RELEASING %s Held by %X:  Count = %d\n",
                    MyTid, WhichLock, LockDB.LockOwners[LockID][0],
                    LockDB.LockCount[LockID]);
            //LockDB.LockNextOwner[i] = MyTid;
        }

        // Case 3:  Lock not held but still trying to release
        else if (LockDB.LockCount[LockID] == 0) { // First release - this is good
            sprintf(ProblemReport,
                    "LOCK: BAD#4: Thread %X is RELEASING %s:  Count = %d\n",
                    MyTid, WhichLock, LockDB.LockCount[LockID]);
        } else {
            sprintf(ProblemReport,
                    "LOCK: BAD#5: Thread %X is RELEASING %s:  With a condition we didn't account fo: Count = %d\n",
                    MyTid, WhichLock, LockDB.LockCount[LockID]);
        }
    }    // End of entering UNLOCK

    // Leaving the lock or unlock routine
    if (Return == LOCK_EXIT) {
        strcpy(Direction, " Exit ");
        // With multiple requesters, there's no assurance that the release will be in FIFO order
        // Check the exit to assure that the locker leaving the lock, and now the owner is
        // in fact the first one listed in the database
        if ((Action == LOCK_TRY || Action == LOCK_GET)
                && MyTid != LockDB.LockOwners[LockID][0] ){
            i = 0;
            for ( j = 1; j <= LockDB.LockCount[LockID]; j++){
                if (MyTid == LockDB.LockOwners[LockID][j])
                    i = j;
            }
            if ( i == 0 ) {
                sprintf(ProblemReport,
                        "LOCK: BAD#6: On exit from a lock, Thread %X couldn't find itself in the lockDB for %s\n",
                        MyTid, WhichLock);
            }
            else {   // Switch the order so this Task is the owner in our DB
                j = LockDB.LockOwners[LockID][i];
                LockDB.LockOwners[LockID][i] = LockDB.LockOwners[LockID][0];
                LockDB.LockOwners[LockID][0] =j;
                Quickie(LockID, 6);
            }
        }             // It's a lock action and we're not the lock owner
    }                 // END of Return == EXIT

    sprintf(Output, "LOCKS: %s: %s %s  %s Time = %d TID = %X  %s\n", LockAction,
            Direction, TaskID, WhichLock, CurrentSimulationTime, MyTid,
            LockCaller);
    printf("%s", Output);

// If something has gone wrong, print out a report.
    if (strlen(ProblemReport) > 0)
        printf("%s", ProblemReport);
    ReleaseLock(PLDLock, "PLD");
#endif
}                                 // End of PrintLockDebug

/**************************************************************************
 Condition Management
 Conditions are use for signals - the mechanism for putting threads to
 sleep and waking them up.  On Windows these are called events, and the
 routines here are OS dependent.

 CreateCondition - Get an Event/Condition and store it away.
 WaitForCondition - We're handed the condition for our thread and told to
 wait until some other thread wakes us up.
 SignalCondition - wake up some other thread based on the condition we have.
 **************************************************************************/
/**************************************************************************
 CreateCondition
 **************************************************************************/
void CreateCondition(UINT32 *RequestedCondition) {
    int ConditionReturn;
#ifdef NT
    LocalEvent[NextConditionToAllocate] = CreateEvent(NULL, // no security attributes
            FALSE,     // auto-reset event
            FALSE,     // initial state is NOT signaled
            NULL );     // object not named
    ConditionReturn = 0;
    if (LocalEvent[NextConditionToAllocate] == NULL ) {
        printf("Internal error Creating an Event in CreateCondition\n");
        HandleWindowsError();
        GoToExit(0);
    }
#endif

#if defined LINUX || defined MAC
    *RequestedCondition = -1;
    ConditionReturn
    = pthread_cond_init( &(LocalCondition[NextConditionToAllocate]), NULL );

    if ( ConditionReturn == EAGAIN || ConditionReturn == ENOMEM )
    printf( "PANIC in CreateCondition - No System Resources\n");
    if ( ConditionReturn == EINVAL || ConditionReturn == EFAULT)
    printf( "PANIC in CreateCondition - illegal input\n");
    if ( ConditionReturn == EBUSY ) //  Already locked by another thread
    printf( "PANIC in CreateCondition - Already initialized\n");
#endif
    if (ConditionReturn == 0) {
        *RequestedCondition = NextConditionToAllocate;
        NextConditionToAllocate++;
    }
#ifdef  DEBUG_CONDITION
    printf("CreateCondition # %d\n", *RequestedCondition);
#endif
}                               // End of CreateCondition

/**************************************************************************
 WaitForCondition
 It is assumed that the caller enters here with the mutex locked.
 The result of this call is that the mutex is unlocked.  The caller
 doesn't return from this call until the condition is signaled.
 **************************************************************************/
int WaitForCondition(UINT32 Condition, UINT32 Mutex, INT32 WaitTime,
        char* CallingRoutine) {
    int ReturnValue = 0;
    int ConditionReturn;
#ifdef DEBUG_CONDITION
    printf(
            "WaitForCondition - Enter - time = %d My-Cond = %d  Thread = %X  %s\n",
            CurrentSimulationTime, Condition, GetMyTid(), CallingRoutine);
#endif
#ifdef NT
    ConditionReturn = (int) WaitForSingleObject(LocalEvent[Condition],
            INFINITE);
//ConditionReturn = (int) WaitForSingleObject(LocalEvent[Condition],WaitTime);
    if (ConditionReturn == WAIT_FAILED ) {
        printf("Internal error waiting for an event in WaitForCondition\n");
        HandleWindowsError();
        GoToExit(0);
    }
    ReturnValue = 0;

#endif
#if defined LINUX || defined MAC
//        if ( LocalMutex[Mutex].__data.__owner == 0 )   {
//            printf("WaitForCondition:  %d %d %d\n", Mutex, 
//                    (int)LocalMutex[Mutex], GetMyTid() );
//        }
    pthread_mutex_lock( &(LocalMutex[Mutex]) );
    ConditionReturn
    = pthread_cond_wait( &(LocalCondition[Condition]),
            &(LocalMutex[Mutex]) );
    if ( ConditionReturn == EINVAL )
    printf( "In WaitForCondition, An illegal argument value was found\n");
    if ( ConditionReturn == EPERM )
    printf( "In WaitForCondition, The mutex was not locked by the caller\n");
    if ( ConditionReturn == 0 )
    ReturnValue = TRUE;// Success
#endif
#ifdef DEBUG_CONDITION
    printf(
            "WaitForCondition - Exit - time = %d My-Cond = %d  Thread = %X  %s\n",
            CurrentSimulationTime, Condition, GetMyTid(), CallingRoutine);
#endif
    return (ReturnValue);
}                               // End of WaitForCondition

/**************************************************************************
 SignalCondition
 Used to wake up a thread that's waiting on a condition.
 **************************************************************************/
int SignalCondition(UINT32 Condition, char* CallingRoutine) {
    int ReturnValue = 0;
#ifdef NT
    static int NumberOfSignals = 0;
#endif
#if defined LINUX || defined MAC
    int ConditionReturn;
#endif

#ifdef DEBUG_CONDITION
    printf(
            "SignalCondition - Enter - time = %d Target-Cond = %d  Thread = %X  %s\n",
            CurrentSimulationTime, Condition, GetMyTid(), CallingRoutine);
#endif
    if (InterruptTid == GetMyTid())  // We don't want to signal ourselves
            {
        ReturnValue = TRUE;
        return (ReturnValue);
    }
#ifdef NT
    if (!SetEvent(LocalEvent[Condition])) {
        printf("Internal error signalling  an event in SignalCondition\n");
        HandleWindowsError();
        GoToExit(0);
    }
    ReturnValue = TRUE;
    if (NumberOfSignals % 3 == 0)
        DoSleep(1);
    NumberOfSignals++;
#endif
#if defined LINUX || defined MAC

    ConditionReturn
    = pthread_cond_signal( &(LocalCondition[Condition]) );
    if ( ConditionReturn == EINVAL || ConditionReturn == EFAULT )
    printf( "In SignalCondition, An illegal value or status was found\n");
    if ( ConditionReturn == 0 )
    ReturnValue = TRUE;          // Success
    ConditionReturn = sched_yield();
#endif
#ifdef DEBUG_CONDITION
    printf(
            "SignalCondition - Exit - time = %d Target-Cond = %d  Thread = %X  %s\n",
            CurrentSimulationTime, Condition, GetMyTid(), CallingRoutine);
#endif
    return (ReturnValue);
}                               // End of SignalCondition

/**************************************************************************
 DoSleep
 The argument is the sleep time in milliseconds.
 **************************************************************************/

void DoSleep(INT32 millisecs) {

#ifdef NT
    Sleep(millisecs);
#endif
#ifndef NT
    usleep((unsigned long) (millisecs * 1000));
#endif
}                              // End of DoSleep
/**************************************************************************
 HandleWindowsError
 **************************************************************************/

#ifdef  NT
void HandleWindowsError() {
    LPVOID lpMsgBuf;
    char OutputString[256];

    FormatMessage(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
                    | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, GetLastError(),
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR) &lpMsgBuf, 0,
            NULL );
    sprintf(OutputString, "%s\n", (char *) lpMsgBuf);
    printf(OutputString);
}                                     // End HandleWindowsError
#endif

/**************************************************************************
 GoToExit
 This is a good place to put a breakpoint.  It helps you find
 places where the code is diving.
 **************************************************************************/
void GoToExit(int Value) {
    printf("Exiting the program\n");
    exit(Value);
}             // End of GoToExit

/*****************************************************************
 Z502Init()

 We come here in order to initialize the hardware.
 The first call that the OS does to the hardware we want to
 come here.  There are several locations that will get us here.
 *****************************************************************/

void Z502Init() {
    INT16 i;

    if (Z502Initialized == FALSE) {
    // Show that we've been in this code.
        Z502Initialized = TRUE;

        printf("This is Simulation Version %s and Hardware Version %s.\n\n",
                CURRENT_REL, HARDWARE_VERSION);
        EventQueue.queue = NULL;
        BaseTid = GetMyTid();
        CreateLock(&EventLock, "Z502Init");
        CreateLock(&InterruptLock, "Z502Init");
        CreateLock(&HardwareLock, "Z502Init");
        CreateLock(&ThreadTableLock, "Z502Init");
        CreateCondition(&InterruptCondition);
        for (i = 1; i < MAX_NUMBER_OF_DISKS ; i++) {
            sector_queue[i].queue = NULL;
            disk_state[i].last_sector = 0;
            disk_state[i].disk_in_use = FALSE;
            disk_state[i].event_ptr = NULL;
            HardwareStats.disk_reads[i] = 0;
            HardwareStats.disk_writes[i] = 0;
            HardwareStats.time_disk_busy[i] = 0;
        }
        HardwareStats.context_switches = 0;
        HardwareStats.number_charge_times = 0;
        HardwareStats.number_faults = 0;
        HardwareStats.number_mask_set_seen = 0;

        for (i = 0; i <= LARGEST_STAT_VECTOR_INDEX; i++) {
            STAT_VECTOR[SV_ACTIVE ][i] = 0;
            STAT_VECTOR[SV_VALUE ][i] = 0;
        }
        for (i = 0; i < MEMORY_INTERLOCK_SIZE; i++)
            InterlockRecord[i] = -1;

        for (i = 0; i < sizeof(MEMORY); i++)
            MEMORY[i] = i % 256;

        timer_state.timer_in_use = FALSE;
        timer_state.event_ptr = NULL;

        Z502_MODE = KERNEL_MODE;

        //Z502MakeContext( &starting_context_ptr,
        //                                  ( void *)os_init, KERNEL_MODE );
        Z502_CURRENT_CONTEXT = NULL;
        //z502_machine_next_context_ptr       = starting_context_ptr;

        CreateAThread((int *) HardwareInterrupt, &EventLock);
        DoSleep(100);
        ChangeThreadPriority(LESS_FAVORABLE_PRIORITY);

        // Set  up the user thread structure
        for (i = 0; i < MAX_NUMBER_OF_USER_THREADS; i++) {
            ThreadTable[i].OurLocalID = -1;
        }

    }             // End of 5502Initialized = FALSE
}                     // End of Z502Init
