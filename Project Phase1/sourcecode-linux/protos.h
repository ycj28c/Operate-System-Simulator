/*********************************************************************
protos.h

     This include file contains prototypes needed by the builder of an
     Operating System.

     Revision History:
        1.0    August 1990     Initial Release.
        2.1    May    2001     Add memory_printer.
        2.2    July   2002     Make code appropriate for undergrads.
        3.1  August   2004:    hardware interrupt runs on separate thread
        3.11  August  2004:    Support for OS level locking
        4.0   May     2013:    Support for new testing mechanism

*********************************************************************/
#ifndef  PROTOS_H
#define  PROTOS_H

#include     "syscalls.h"

//                      ENTRIES in base.c

void   interrupt_handler( void );
void   fault_handler( void );
void   svc( SYSTEM_CALL_DATA * );
void   osInit (int argc, char *argv[] );

 //declare OScreateProcess function

//                     ENTRIES in sample.c

void   sample_code(void );

//                      ENTRIES in state_printer.c

void   SP_setup( INT16, INT32 );
void   SP_setup_file( INT16, FILE * );
void   SP_setup_action( INT16, char * );
void   SP_print_header( void );
void   SP_print_line( void );
void   SP_do_output( char * );
void   MP_setup( INT32, INT32, INT32, INT32 );
void   MP_print_line( void );

//                      ENTRIES in test.c

void   test0( void );
void   test1a( void );
void   test1b( void );
void   test1c( void );
void   test1d( void );
void   test1e( void );
void   test1f( void );
void   test1g( void );
void   test1h( void );
void   test1i( void );
void   test1j( void );
void   test1k( void );
void   test1l( void );
void   test1m( void );
void   test2a( void );
void   test2b( void );
void   test2c( void );
void   test2d( void );
void   test2e( void );
void   test2f( void );
void   test2g( void );
void   test2h( void );


//                      ENTRIES in z502.c

// This is the only way that the Operating System can access the hardware.
// Some of these entries are used only by the main() in test.c

void   Z502CreateUserThread( void *);
void   Z502DestroyContext( void ** );
void   Z502Halt( void );
void   Z502Idle( void );
void   Z502MemoryRead(INT32, INT32 * );
void   Z502MemoryWrite(INT32, INT32 * );
void   Z502ReadPhysicalMemory( INT32, char *);
void   Z502WritePhysicalMemory( INT32, char *);
void   Z502MakeContext( void **, void *, BOOL );
void   Z502SwitchContext( BOOL, void ** );
void   *Z502PrepareProcessForExecution( void );
void   Z502MemoryReadModify( INT32, INT32, INT32, INT32 * );

#endif // PROTOS_H_
