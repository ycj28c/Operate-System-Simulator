/***************************************************************************

  global.h

      This include file is used by both the OS502 and
      the Z502 Simulator.

      Revision History:
        1.0 August 1990:        first release
        1.1 Jan    1991:        Additional disks added
        1.3 July   1992:        Make as many defines as possible
                                into shorts since PCs are happier.
        1.6 June   1995:        Minor changes
        2.0 Jan    2000:        A large number of small changes.
        2.1 May    2001:        Redefine disk layout
        2.2 July   2002:        Make code appropriate for undergrads.
        3.0 August 2004:        Modified to support memory mapped IO
        3.1 August 2004:        Implement Memory Mapped IO
        3.11 August 2004:       Implement OS level locking capability
        3.13 Nov.   2004        Change priorities of LINUX threads
        3.40 August 2008        Fix code for 64 bit addresses
        3.41 Sept.  2008        Change Z502_ARG to support 64 bit addr.
        3.50 August 2009        Minor cosmetics
        3.60 August 2012        Updates with student generated code to
                                support MACs
****************************************************************************/
#ifndef GLOBAL_H_
#define GLOBAL_H_

#define         CURRENT_REL                     "4.00"
#define         NT
//#define        LINUX
// #define           MAC

        /*      These are Portability enhancements              */

typedef         int                             INT32;
typedef         unsigned int                   UINT32;
typedef         short                           INT16;
typedef         unsigned short                  UINT16;
typedef         int                             BOOL;

#ifdef  NT
#define THREAD_PRIORITY_LOW           THREAD_PRIORITY_BELOW_NORMAL
#define THREAD_PRIORITY_HIGH          THREAD_PRIORITY_TIME_CRITICAL
#define LOCK_TYPE                     HANDLE
// Eliminates warnings of deprecated functions with Visual C++
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef NT
#define THREAD_PRIORITY_LOW                 1
#define THREAD_PRIORITY_HIGH                2
#define LOCK_TYPE                       pthread_mutex_t
#endif
#define LESS_FAVORABLE_PRIORITY             -5
#define MORE_FAVORABLE_PRIORITY              5
#define MAX_NUMBER_OF_USER_THREADS               30

#define         FALSE                           (BOOL)0
#define         TRUE                            (BOOL)1

#define         PHYS_MEM_PGS                    (short)64
#define         PGSIZE                          (short)16
#define         PGBITS                          (short)4
#define         VIRTUAL_MEM_PGS                 1024
#define         VMEMPGBITS                      10
#define         MEMSIZE                         PHYS_MEM_PGS * PGSIZE

/*****************************************************************
        The next two variables have special meaning.  They allow the
        Z502 processor to report information about its state.  From
        this, you can find what the hardware thinks is going on.
        The information produced when this debugging is on is NOT
        something that should be handed in with the project.
******************************************************************/

#define         DO_DEVICE_DEBUG                 FALSE
#define         DO_MEMORY_DEBUG                 FALSE


        /* Meaning of locations in a page table entry          */

#define         PTBL_VALID_BIT                  0x8000
#define         PTBL_MODIFIED_BIT               0x4000
#define         PTBL_REFERENCED_BIT             0x2000
#define         PTBL_PHYS_PG_NO                 0x0FFF

        /*  The maximum number of disks we will support:        */

#define         MAX_NUMBER_OF_DISKS             (short)12


/*      These are the memory mapped IO addresses                */

#define      Z502InterruptDevice       Z502InterruptStatus+1
#define      Z502InterruptStatus       Z502InterruptClear+1
#define      Z502InterruptClear        Z502ClockStatus+1
#define      Z502ClockStatus           Z502TimerStart+1
#define      Z502TimerStart            Z502TimerStatus+1
#define      Z502TimerStatus           Z502DiskSetID+1
#define      Z502DiskSetID             Z502DiskSetSector+1
#define      Z502DiskSetSector         Z502DiskSetBuffer+1
#define      Z502DiskSetBuffer         Z502DiskSetAction+1
#define      Z502DiskSetAction         Z502DiskSetup4+1
#define      Z502DiskSetup4            Z502DiskStart+1
#define      Z502DiskStart             Z502DiskStatus+1
#define      Z502DiskStatus            Z502MEM_MAPPED_MIN+1
#define      Z502MEM_MAPPED_MIN        0x7FF00000

/*  These are the allowable locations for hardware synchronization support */

#define      MEMORY_INTERLOCK_BASE     0x7FE00000
#define      MEMORY_INTERLOCK_SIZE     0x00000100

/*  These are the device IDs that are produced when an interrupt
    or fault occurs.                                            */
        /* Definition of trap types.                            */

#define         SOFTWARE_TRAP                   (short)0

        /* Definition of fault types.                           */

#define         CPU_ERROR                       (short)1
#define         INVALID_MEMORY                  (short)2
#define         INVALID_PHYSICAL_MEMORY         (short)3
#define         PRIVILEGED_INSTRUCTION          (short)4

        /* Definition of interrupt types.                       */

#define         TIMER_INTERRUPT                 (short)4
#define         DISK_INTERRUPT                  (short)5
#define         DISK_INTERRUPT_DISK1            (short)5
#define         DISK_INTERRUPT_DISK2            (short)6
/*      ... we could define other explicit names here           */

#define         LARGEST_STAT_VECTOR_INDEX       DISK_INTERRUPT + \
                                                MAX_NUMBER_OF_DISKS - 1


/*      Definition of the TO_VECTOR array.  The TO_VECTOR
        contains pointers to the routines which will handle
        hardware exceptions.  The pointers are accessed with
        these indices:                                          */

#define         TO_VECTOR_INT_HANDLER_ADDR              (short)0
#define         TO_VECTOR_FAULT_HANDLER_ADDR            (short)1
#define         TO_VECTOR_TRAP_HANDLER_ADDR             (short)2
#define         TO_VECTOR_TYPES                         (short)3

        /* Definition of return codes.                           */

#define         ERR_SUCCESS                             0L
#define         ERR_BAD_PARAM                           1L
#define         ERR_NO_PREVIOUS_WRITE                   2L
#define         ERR_ILLEGAL_ADDRESS                     3L
#define         ERR_DISK_IN_USE                         4L
#define         ERR_BAD_DEVICE_ID                       5L
#define         DEVICE_IN_USE                           6L
#define         DEVICE_FREE                             7L
#define         ERR_Z502_INTERNAL_BUG                   20L
#define         ERR_OS502_GENERATED_BUG                 21L

        /* Miscellaneous                                        */

#define         NUM_LOGICAL_SECTORS                     (short)1600

#define         SWITCH_CONTEXT_KILL_MODE                (short)0
#define         SWITCH_CONTEXT_SAVE_MODE                (short)1

#define         USER_MODE                               (short)0
#define         KERNEL_MODE                             (short)1



#endif /* GLOBAL_H_ */
