/**************************************************************************

 This routine, "sample", is designed to show you how to interface
 to various hardware functions and how to use scheduler_printer.
 It is provided solely as an example; it is NOT part of the normal
 code involved in building your Operating System.

 Revision History:
 1.3 July     1992: Initial code written.
 1.4 December 1992: Add test to size host machine memory.
 2.2 July     2002: Make code appropriate for undergrads.
 3.0 August   2004: Modified to support memory mapped IO
 3.1 August   2004: hardware interrupt runs on separate thread
 3.11 August  2004: Support for OS level locking
 3.12 Sept.   2004: Minor changes
 3.30 Aug     2006: Change threading and disk tests to be
 consistant with current hardware.
 3.41         2008: Fix bug associated with running on 64-bit
 machines.
 **************************************************************************/

#include                 "global.h"
#include                 "syscalls.h"
#include                 "z502.h"
#include                 "protos.h"
#include                 "stdio.h"
#include                 "stdlib.h"
#include                 "string.h"
#ifdef NT
#include                 <windows.h>
#include                 <winbase.h>
#include                 <sys/types.h>
#endif

#define                  NUM_RAND_BUCKETS          128
#define                  DO_LOCK                     1
#define                  DO_UNLOCK                   0
#define                  SUSPEND_UNTIL_LOCKED        TRUE
#define                  DO_NOT_SUSPEND              FALSE

//  Prototypes For Functions In This File
void starting_point_for_new_context(void);
void DoOnelock(void);
void DoOneTrylock(void);
void DoOneUnlock(void);

//  This is a prototype for test.c
void get_skewed_random_number(long *, long);

// This is a prototype for Z502.c
void DoSleep(INT32 millisecs);
int CreateAThread(void *ThreadStartAddress, INT32 *data);

extern UINT16 *Z502_PAGE_TBL_ADDR;
extern INT16 Z502_PAGE_TBL_LENGTH;

char Success[] = "      Action Failed\0        Action Succeeded";
#define          SPART          22

void sample_code(void) {
	INT32 i, j, k; /* Index & counters       */
	long Value;

	INT32 disk_id, sector; /* Used for disk requests */
	char disk_buffer_write[PGSIZE ];
	char disk_buffer_read[PGSIZE ];
	char physical_memory_write[PGSIZE ];
	char physical_memory_read[PGSIZE ];

	void *context_pointer; /* Used for context commands*/
	void *starting_address;
	BOOL kernel_or_user;
	short random_buckets[NUM_RAND_BUCKETS];
	INT32 LockResult;

	INT32 Status;
	INT32 Temp, Temp1;

	/*********************************************************************
	 Show the interface to the delay timer.
	 Eventually the timer will interrupt ( in base.c there's a handler for
	 this ), but here in sample_code.c we're merely showing the interface
	 to start the call.
	 *********************************************************************/
	MEM_READ(Z502TimerStatus, &Status);
	if (Status == DEVICE_FREE)
		printf("Got expected result for Status of Timer\n");
	else
		printf("Got erroneous result for Status of Timer\n");

	Temp = 777; /* You pick the time units */
	MEM_WRITE(Z502TimerStart, &Temp);
	MEM_READ(Z502TimerStatus, &Status);
	if (Status == DEVICE_IN_USE)
		printf("Got expected result for Status of Timer\n");
	else
		printf("Got erroneous result for Status of Timer\n");
	printf("The next output from the Interrupt Handler should report that \n");
	printf("   interrupt of device 4 has occurred with no error.\n");
	Z502Idle();                //  Let the interrupt for this timer occur

	//  Now we're going to try an illegal time and ensure that the fault
	// handler reports an illegal status.
	Temp = -77; /* You pick the time units */
	printf("The next output from the Interrupt Handler should report that \n");
	printf(
			"   interrupt of device 4 has occurred with an ERR_BAD_PARAM = 1.\n");
	MEM_WRITE(Z502TimerStart, &Temp);
	MEM_READ(Z502TimerStatus, &Status);
	if (Status == DEVICE_FREE)          // Bogus value shouldn't start timer
		printf("Got expected result for Status of Timer\n");
	else
		printf("Got erroneous result for Status of Timer\n");
	fflush(stdout);
	//   ZCALL( Z502_IDLE() );           //  Let the interrupt for this timer occur

	/*  The interrupt handler will have exercised the code doing
	 Z502InterruptDevice,  Z502InterruptStatus, and Z502InterruptClear.
	 But they will have been tested only for "correct" usage.
	 Let's try a few erroneous/illegal operations.          */

	// Set an illegal device as target of our query
	Temp = LARGEST_STAT_VECTOR_INDEX + 1;
	MEM_WRITE(Z502InterruptDevice, &Temp);
	// Now read the status of this device
	MEM_READ(Z502InterruptStatus, &Status);
	if (Status == ERR_BAD_DEVICE_ID)
		printf("Got expected result for Status of Illegal Device\n");
	else
		printf("Got erroneous result for Status of Illegal Device\n");

	/*********************************************************************
	 Show the interface to the Z502_CLOCK.
	 This is easy - all we're going to do is read it twice and make sure
	 the time is incrementing.
	 *********************************************************************/

	MEM_READ(Z502ClockStatus, &Temp);
	MEM_READ(Z502ClockStatus, &Temp1);
	if (Temp1 > Temp)
		printf("The clock time incremented correctly - %d1, %d2\n", Temp,
				Temp1);
	else
		printf("The clock time did NOT increment correctly - %d1, %d2\n", Temp,
				Temp1);

	/*********************************************************************
	 Show the interface to the disk read and write.
	 Eventually the disk will interrupt ( in base.c there's a handler for
	 this ), but here in sample_code.c we're merely showing the interface
	 to start the call.
	 *********************************************************************/

	disk_id = 1; /* Pick arbitrary disk location             */
	sector = 3;
	/* Put data into the buffer being written   */
	strncpy(disk_buffer_write, "123456789abcdef", 15);
	/* Do the hardware call to put data on disk */
	MEM_WRITE(Z502DiskSetID, &disk_id);
	MEM_READ(Z502DiskStatus, &Temp);
	if (Temp == DEVICE_FREE)        // Disk hasn't been used - should be free
		printf("Got expected result for Disk Status\n");
	else
		printf("Got erroneous result for Disk Status - Device not free.\n");
	MEM_WRITE(Z502DiskSetSector, &sector);
	MEM_WRITE(Z502DiskSetBuffer, (INT32 * )disk_buffer_write);
	Temp = 1;                        // Specify a write
	MEM_WRITE(Z502DiskSetAction, &Temp);
	Temp = 0;                        // Must be set to 0
	MEM_WRITE(Z502DiskStart, &Temp);
	// Disk should now be started - let's see
	MEM_WRITE(Z502DiskSetID, &disk_id);
	MEM_READ(Z502DiskStatus, &Temp);
	if (Temp == DEVICE_IN_USE)        // Disk should report being used
		printf("Got expected result for Disk Status\n");
	else
		printf("Got erroneous result for Disk Status\n");

	/* Wait until the disk "finishes" the write. the write is an
	 "unpended-io", meaning the call returns before the work is
	 completed.  By doing the IDLE here, we wait for the disk
	 action to complete.    */
	MEM_WRITE(Z502DiskSetID, &disk_id);
	MEM_READ(Z502DiskStatus, &Temp);
	while (Temp != DEVICE_FREE) {
		Z502Idle();
		MEM_READ(Z502DiskStatus, &Temp);
	}
	/* Now we read the data back from the disk.  If we're lucky,
	 we'll read the same thing we wrote!                     */

	MEM_WRITE(Z502DiskSetSector, &sector);
	MEM_WRITE(Z502DiskSetBuffer, (INT32 * )disk_buffer_read);
	Temp = 0;                        // Specify a read
	MEM_WRITE(Z502DiskSetAction, &Temp);
	Temp = 0;                        // Must be set to 0
	MEM_WRITE(Z502DiskStart, &Temp);

	/* wait for the disk action to complete.  */
	MEM_WRITE(Z502DiskSetID, &disk_id);
	MEM_READ(Z502DiskStatus, &Temp);
	while (Temp != DEVICE_FREE) {
		Z502Idle();
		MEM_READ(Z502DiskStatus, &Temp);
	}

	printf("\n\nThe disk data written is: %s\n", disk_buffer_write);
	printf("The disk data read    is: %s\n", disk_buffer_read);

	/*********************************************************************
	 Let's try some intentional errors to see what happens
	 *********************************************************************/
	Temp = 0;                        // Must be set to 0
	MEM_WRITE(Z502DiskStart, &Temp);
	// Try reading the status without setting an ID
	MEM_READ(Z502DiskStatus, &Temp);
	if (Temp == ERR_BAD_DEVICE_ID)
		printf("Got expected result for Disk Status when using no ID\n");
	else
		printf("Got erroneous result for Disk Status when using no ID\n");

	// Try entering a bad ID and then reading the status
	disk_id = 999;
	MEM_WRITE(Z502DiskSetID, &disk_id);
	MEM_READ(Z502DiskStatus, &Temp);
	if (Temp == ERR_BAD_DEVICE_ID)
		printf("Got expected result for Disk Status when using bad ID\n");
	else
		printf("Got erroneous result for Disk Status when using bad ID\n");

	//  Try doing everything right EXCEPT entering the buffer address,
	disk_id = 1; /* Pick arbitrary disk location             */
	sector = 3;

	MEM_WRITE(Z502DiskSetID, &disk_id);
	MEM_WRITE(Z502DiskSetSector, &sector);
// Don't do this ->    MEM_WRITE( Z502DiskSetBuffer, (INT32 *)disk_buffer_write );
	Temp = 1;                        // Specify a write
	MEM_WRITE(Z502DiskSetAction, &Temp);
	Temp = 0;                        // Must be set to 0
	MEM_WRITE(Z502DiskStart, &Temp);
	// Disk should now not be started - it was missing vital info
	MEM_WRITE(Z502DiskSetID, &disk_id);
	MEM_READ(Z502DiskStatus, &Temp);
	if (Temp == DEVICE_FREE)        // Disk should report being free
		printf("Got expected result for Disk Status when missing data\n");
	else
		printf("Got erroneous result for Disk Status when missing data\n");
	/*********************************************************************
	 Some of the tests put thousands of pages of data on the disk.  Let's
	 see if we can do that here.   The pages ARE being written to the disk,
	 but the interrupt handler doesn't show all of them happening because
	 it's not catching multiple interrupts.
	 *********************************************************************/

	disk_id = 1;
	sector = 0;
	printf("The following section will take a few seconds\n");
	for (j = 0; j < VIRTUAL_MEM_PGS + 100 /* arbitrary # */; j++) {
		MEM_WRITE(Z502DiskSetID, &disk_id);
		MEM_WRITE(Z502DiskSetSector, &sector);
		MEM_WRITE(Z502DiskSetBuffer, (INT32 * )disk_buffer_write);
		Temp = 1;                        // Specify a write
		MEM_WRITE(Z502DiskSetAction, &Temp);
		Temp = 0;                        // Start the disk
		MEM_WRITE(Z502DiskStart, &Temp);
		MEM_WRITE(Z502DiskSetID, &disk_id);
		MEM_READ(Z502DiskStatus, &Temp);
		while (Temp == DEVICE_IN_USE)        // Disk should report being used
		{
			//printf( "Got erroneous result for Disk Status when writing lots of blocks\n" );
			Temp = 5; /* You pick the time units */
			MEM_WRITE(Z502TimerStart, &Temp);
			MEM_READ(Z502DiskStatus, &Temp);
		}
		sector++;
		if (sector >= NUM_LOGICAL_SECTORS) {
			sector = 0;
			disk_id++;
		}
	}
	/*********************************************************************
	 Do a physical memory access to check out that it works.
	 *********************************************************************/
	printf("\nStarting test of physical memory write and read.\n");
	for (i = 0; i < PGSIZE ; i++) {
		physical_memory_write[i] = i;
	}
	Z502WritePhysicalMemory(17, (char *) physical_memory_write);
	Z502ReadPhysicalMemory(17, (char *) physical_memory_read);
	for (i = 0; i < PGSIZE ; i++) {
		if (physical_memory_write[i] != physical_memory_read[i])
			printf("Error in Physical Memory Access\n");
	}
	printf("Completed test of physical memory write and read.\n");
	/*********************************************************************
	 Start all of the disks at the same time and see what happens.
	 *********************************************************************/
	/*
	 sector   = 0;
	 for ( disk_id = 1; disk_id <= MAX_NUMBER_OF_DISKS  ; disk_id++ )
	 {
	 MEM_WRITE( Z502DiskSetID, &disk_id );
	 MEM_WRITE( Z502DiskSetSector, &sector );
	 MEM_WRITE( Z502DiskSetBuffer, (INT32 *)disk_buffer_write );
	 Temp = 1;                        // Specify a write
	 MEM_WRITE( Z502DiskSetAction, &Temp );
	 Temp = 0;                        // Start the disk
	 MEM_WRITE( Z502DiskStart, &Temp );
	 sector++;
	 }
	 //  Now wait until all disks have finished
	 for ( disk_id = 1; disk_id <= MAX_NUMBER_OF_DISKS  ; disk_id++ )
	 {
	 MEM_WRITE( Z502DiskSetID, &disk_id );
	 MEM_READ( Z502DiskStatus, &Temp );
	 while ( Temp == DEVICE_IN_USE )        // Disk should report being used
	 {
	 //ZCALL( Z502_IDLE() );
	 DoSleep(50);
	 }
	 }
	 */
	printf("Disk multiple-block test is complete\n\n");

	/*********************************************************************
	 Show the interface to read and write of real memory
	 It turns out, that though these are hardware calls, the Z502
	 assumes the calls are being made in user mode.  Because the
	 process we're running here in "sample" is in kernel mode,
	 the calls don't work correctly.  For working examples of
	 these calls, see test2a.
	 *********************************************************************/

	Z502_PAGE_TBL_LENGTH = 64;
	Z502_PAGE_TBL_ADDR = (UINT16 *) calloc(sizeof(UINT16),
			Z502_PAGE_TBL_LENGTH);
	i = PTBL_VALID_BIT;
	Z502_PAGE_TBL_ADDR[0] = (UINT16) i;
	i = 73;
	MEM_WRITE(0, &i);
	MEM_READ(0, &j);
	/*  WE expect the data read back to be the same as what we wrote  */
	if (i == j)
		printf("Memory write and read completed successfully\n");
	else
		printf("Memory write and read were NOT successful.\n");

	/*********************************************************************
	 This is the interface to the locking mechanism.  These are hardware
	 interlocks.  We need to test that they work here.  This is the
	 interface we'll be using.

	 void    Z502_READ_MODIFY( INT32 VirtualAddress, INT32 NewLockValue,
	 INT32 Suspend, INT32 *LockResult )

	 We've defined these above to help remember them.
	 #define                  DO_LOCK                     1
	 #define                  DO_UNLOCK                   0
	 #define                  SUSPEND_UNTIL_LOCKED        TRUE
	 #define                  DO_NOT_SUSPEND              FALSE

	 *********************************************************************/

	printf("++++  Starting Hardware Interlock Testing   ++++\n");

	//  TRY A SERIES OF CALLS AS DESCRIBED HERE
	printf("These tests map into the matrix describing the Z502_READ_MODIFY\n");
	printf("     described in Appendix A\n\n");

	printf(
			"1.  Start State = Unlocked:  Action (Thread 1) = Lock: End State = Locked\n");
	READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,
			&LockResult);
	printf("%s\n", &(Success[SPART * LockResult]));

	printf(
			"2.  Start State = locked(1): Action (Thread 1) = unLock: End State = UnLocked\n");
	READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,
			&LockResult);
	printf("%s\n", &(Success[SPART * LockResult]));

	printf(
			"3.  Start State = Unlocked:  Action (Thread 1) = unLock: End State = UnLocked\n");
	printf("    An Error is Expected\n");
	READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,
			&LockResult);
	printf("%s\n", &(Success[SPART * LockResult]));

	printf(
			"4.  Start State = unlocked:  Action (Thread 1) = tryLock: End State = Locked\n");
	READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, DO_NOT_SUSPEND, &LockResult);
	printf("%s\n", &(Success[SPART * LockResult]));

	printf(
			"5.  Start State = Locked(1): Action (Thread 1) = tryLock: End State = Locked\n");
	READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, DO_NOT_SUSPEND, &LockResult);
	printf("%s\n", &(Success[SPART * LockResult]));

	printf(
			"6.  Start State = locked(1): Action (Thread 1) = unLock: End State = UnLocked\n");
	READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,
			&LockResult);
	printf("%s\n", &(Success[SPART * LockResult]));

	printf(
			"7.  Start State = Unlocked:  Action (Thread 1) = Lock: End State = Locked\n");
	READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,
			&LockResult);
	printf("%s\n", &(Success[SPART * LockResult]));

	//  A thread that locks an item it has already locked will succeed
	printf(
			"8.  Start State = locked(1): Action (Thread 1) = Lock: End State = Locked\n");
	READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,
			&LockResult);
	printf("%s\n", &(Success[SPART * LockResult]));

	// Locking a thread that's already locked - but do it with a thread
	// other than the one that did the locking.
	printf(
			"9.  Start State = Locked(1): Action (Thread 2) = tryLock: End State = Locked\n");
	printf("    An Error is Expected\n");
	Status = CreateAThread((int *) DoOnelock, &Temp);
	DoSleep(100); /*  Wait for that thread to finish   */

	// Unlock a thread that's already locked - but unlock it with a thread
	// other than the one that did the locking.
	printf(
			"10. Start State = Locked(1): Action (Thread 2) = unLock: End State = Locked\n");
	printf("    An Error is Expected\n");
	Status = CreateAThread((int *) DoOneUnlock, &Temp);
	DoSleep(100); /*  Wait for that thread to finish   */

	// The second thread will try to get the lock held by the first thread.  This is OK
	// but the second thread will suspend until the first thread releases the lock.
	printf(
			"11. Start State = Locked(1): Action (Thread 2) = Lock: End State = Locked\n");
	Status = CreateAThread((int *) DoOneTrylock, &Temp);
	DoSleep(100); /*  Wait for that thread to finish   */

	//  The first thread does an unlock.  This means the second thread is now able to get
	//  the lock so there is a "relock" when thread two succeeds.
	printf(
			"12. Start State = locked(1): Action (Thread 1) = unLock: End State = Locked(by 2)\n");
	READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,
			&LockResult);
	printf("%s\n", &(Success[SPART * LockResult]));
	DoSleep(100); /*  Wait for locking action of 2nd thread to finish   */

	printf(
			"13. Start State = Locked(2): Action (Thread 3) = unLock: End State = Locked(2)\n");
	printf("    An Error is Expected\n");
	Status = CreateAThread((int *) DoOneUnlock, &Temp);
	DoSleep(100); /*  Wait for that thread to finish   */

	printf(
			"14. Start State = Locked(2): Action (Thread 1) = tryLock: End State = Locked(2)\n");
	READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, DO_NOT_SUSPEND, &LockResult);
	printf("%s\n", &(Success[SPART * LockResult]));

	printf(
			"15. Start State = locked(2): Action (Thread 1) = unLock: End State = Locked(2)\n");
	printf("    An Error is Expected\n");
	READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,
			&LockResult);
	printf("%s\n", &(Success[SPART * LockResult]));

	printf("++++  END of hardware interlock code  ++++\n\n");

	/*********************************************************************
	 Show the interface to the CONTEXT calls.   We aren't going to do a
	 SWITCH_CONTEXT here, because that would cause us to start a
	 process in a strange place and we might never return here.

	 But we do all the setup required.
	 *********************************************************************/

	/* The context_pointer is returned by the MAKE_CONTEXT call.        */

	starting_address = (void *) starting_point_for_new_context;
	kernel_or_user = USER_MODE;
	Z502MakeContext(&context_pointer, starting_address, kernel_or_user);
	Z502DestroyContext(&context_pointer);

	/*********************************************************************
	 Show the interface to the scheduler printer.
	 *********************************************************************/

	CALL(SP_setup( SP_TIME_MODE, 99999 ));
	CALL(SP_setup_action( SP_ACTION_MODE, "CREATE" ));
	CALL(SP_setup( SP_TARGET_MODE, 99L ));
	CALL(SP_setup( SP_RUNNING_MODE, 99L ));
	for (j = 0; j < SP_MAX_NUMBER_OF_PIDS ; j++)
		CALL(SP_setup( SP_READY_MODE, j ));
	for (j = 0; j < SP_MAX_NUMBER_OF_PIDS ; j++)
		CALL(SP_setup( SP_WAITING_MODE, j+20 ));
	for (j = 0; j < SP_MAX_NUMBER_OF_PIDS ; j++)
		CALL(SP_setup( SP_SUSPENDED_MODE, j+40 ));
	for (j = 0; j < SP_MAX_NUMBER_OF_PIDS ; j++)
		CALL(SP_setup( SP_SWAPPED_MODE, j+60 ));
	for (j = 0; j < SP_MAX_NUMBER_OF_PIDS ; j++)
		CALL(SP_setup( SP_TERMINATED_MODE, j+80 ));
	for (j = 0; j < SP_MAX_NUMBER_OF_PIDS ; j++)
		CALL(SP_setup( SP_NEW_MODE, j+50 ));
	CALL(SP_print_header());
	CALL(SP_print_line());

	/*********************************************************************
	 Show the interface to the memory_printer.
	 *********************************************************************/

	for (j = 0; j < 64; j = j + 2) {
		MP_setup((INT32) (j), (INT32) (j / 2) % 10, (INT32) j * 16 + 10,
				(INT32) (j / 2) % 8);
	}
	MP_print_line();

	/*********************************************************************
	 Show how the skewed random numbers work on this platform.
	 *********************************************************************/

	for (j = 0; j < NUM_RAND_BUCKETS; j++)
		random_buckets[j] = 0;

	for (j = 0; j < 100000; j++) {
		get_skewed_random_number(&Value, NUM_RAND_BUCKETS);
		random_buckets[Value]++;
	}
	printf("\nTesting that your platform produces correctly skewed random\n");
	printf("numbers.  Each row should have higher count than the previous.\n");
	printf("    Range:   Counts in this range.\n");
	for (j = 0; j < NUM_RAND_BUCKETS; j = j + 8) {
		k = 0;
		for (i = j; i < j + 8; i++)
			k += random_buckets[i];
		printf("%3d - %3d:  %d\n", j, j + 7, k);
	}

	/*********************************************************************
	 Show the interface to the Z502Halt.
	 Note that the program will end NOW, since we don't return
	 from the command.
	 *********************************************************************/

	Z502Halt();

}

void starting_point_for_new_context(void) {
}

void DoOnelock(void) {
	INT32 LockResult;
	printf("      Thread 2 - about to do a lock\n");
	READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,
			&LockResult);
	printf("      Thread 2 Lock:  %s\n", &(Success[SPART * LockResult]));
//    DestroyThread( 0 );
}
void DoOneTrylock(void) {
	INT32 LockResult;
	READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, DO_NOT_SUSPEND, &LockResult);
	printf("      Thread 2 TryLock:  %s\n", &(Success[SPART * LockResult]));
//    DestroyThread( 0 );
}
void DoOneUnlock(void) {
	INT32 LockResult;
	READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,
			&LockResult);
	printf("      Thread 2 UnLock:  %s\n", &(Success[SPART * LockResult]));
//    DestroyThread( 0 );
}
