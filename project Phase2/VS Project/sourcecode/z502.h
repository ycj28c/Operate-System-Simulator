/*********************************************************************

        z502.h

   This include file is used only by the Z502.

   Revision History:
   1.3  July    1992:   Make structures smaller. Reduce size of SECTOR.
   2.0  January 2000:   Small changes
   2.1  May     2001:   Redefine disk layout.  Define HARDWARE_STATS
   2.2  July    2002:   Make code appropriate for undergrads.
   3.0 August   2004:   Modified to support memory mapped IO
   3.1 August   2004:   hardware interrupt runs on separate thread
   3.11 August  2004:   Support for OS level locking
   3.53 NOVEMBER 2011:  Changed CONTEXT so the space allocated for
                        REGs is long - didn't matter until trying
                        to store addresses.
*********************************************************************/

#ifndef  Z502_H
#define  Z502_H

#define         COST_OF_MEMORY_ACCESS           1L
#define         COST_OF_MEMORY_MAPPED_IO        1L
#define         COST_OF_DISK_ACCESS             8L
#define         COST_OF_DELAY                   2L
#define         COST_OF_CLOCK                   3L
#define         COST_OF_TIMER                   2L
#define         COST_OF_MAKE_CONTEXT            20L
#define         COST_OF_DESTROY_CONTEXT         10L
#define         COST_OF_SWITCH_CONTEXT          15L
#define         COST_OF_SOFTWARE_TRAP           5L
#define         COST_OF_CPU_INSTRUCTION         1L
#define         COST_OF_CALL                    2L

#ifndef NULL
#define         NULL                            0
#endif

#define         EVENT_STRUCTURE_ID              (unsigned char)124
#define         SECTOR_STRUCTURE_ID             (unsigned char)125
#define         CONTEXT_STRUCTURE_ID            (unsigned char)126

#define         EVENT_RING_BUFFER_SIZE          16

/*  STAT_VECTOR is a two dimensional array.  The first
    dimension can take on values shown here.  The
    second dimension holds the error or device type.     */

#define         SV_ACTIVE                       (short)0
#define         SV_VALUE                        (short)1
#define         SV_TID                          (short)2
#define         SV_DIMENSION                    (short)3


typedef struct
    {
    INT32               *queue;
    INT32               time_of_event;
    INT16               ring_buffer_location;
    INT16               event_error;
    INT16               event_type;
    unsigned char       structure_id;
} EVENT;

/* Supports history which is dumped on a hardware panic */

typedef struct
    {
    INT32               time_of_request;
    INT32               expected_time_of_event;
    INT32               real_time_of_event;
    INT16               event_error;
    INT16               event_type;
} RING_EVENT;


typedef struct
{
    INT32               context_switches;
    INT32               disk_reads[MAX_NUMBER_OF_DISKS];
    INT32               disk_writes[MAX_NUMBER_OF_DISKS];
    INT32               time_disk_busy[MAX_NUMBER_OF_DISKS];
    INT32               number_charge_times;
    INT32               number_mask_set_seen;
    INT32               number_faults;
} HARDWARE_STATS;

typedef struct
    {
    INT32               *queue;
    INT16               structure_id;
    INT16               disk_id;
    INT16               sector;
    char                sector_data[PGSIZE];
} SECTOR;

typedef struct
    {
    unsigned char       structure_id;
    void                *entry;
    UINT16              *page_table_ptr;
    INT16               page_table_len;
    INT16               pc;
    INT32               call_type;
    long                reg1, reg2, reg3, reg4, reg5;
    long                reg6, reg7, reg8, reg9;
    INT16               program_mode;
    INT16               mode_at_first_interrupt;
    BOOL                fault_in_progress;
} Z502CONTEXT;

// We create a thread for every potential process a user might create.
// This is the information we need for each thread.

typedef struct {
	int OurLocalID;
	int ThreadID;
	int CurrentState;
	Z502CONTEXT *Context;
	UINT32 Condition;
	UINT32 Mutex;
} THREAD_INFO;

// These are the states defined for a thread and stored in CurrentState
#define         UNINITIALIZED                      0
#define         CREATED                            1
#define         SUSPENDED_WAITING_FOR_CONTEXT      2
#define         SUSPENDED_WAITING_FOR_FIRST_SCHED  3
#define         ACTIVE                             4


typedef struct
    {
    EVENT               *event_ptr;
    INT16               last_sector;
    INT16               disk_in_use;
    INT16               action;
} DISK_STATE;

typedef struct
    {
    INT16               sector;
    INT16               action;
    char                *buffer;
} MEMORY_MAPPED_DISK_STATE;

typedef struct
    {
    EVENT               *event_ptr;
    INT16               timer_in_use;
} TIMER_STATE;

#endif
