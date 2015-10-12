/****************************************************************************
*Filename: portkern.c - RTFS Kernel interface
*
* ERTFS Porting layer. The user must port some or all of the routines
* in this file to his RTOS and hardware environment according to the
* rules set forth in the ERTFS porting specification.
*
*   Copyright EBS Inc , 1993-2006
*   All rights reserved.
*   This code may not be redistributed in source or linkable object form
*   without the consent of its author.
*
*   Generic unpopulated kernel porting layer
*
*
*/

#include "rtfs.h"

/*
This routine takes no arguments and returns an unsigned long. The routine
must allocate and initialize a Mutex, setting it to the "not owned" state. It
must return an unsigned long value that will be used as a handle. ERTFS will
not interpret the value of the return value. The handle will only used as
an argument to the rtfs_port_claim_mutex() and rtfs_port_release_mutex()
calls. The handle may be used as an index into a table or it may be cast
internally to an RTOS specific pointer. If the mutex allocation function
fails this routine must return 0 and the ERTFS calling function will return
failure.
*/
dword rtfs_port_alloc_mutex(char *sem_name)
{
#define ALLOC_RTOS_MUTEX() 1
	return(ALLOC_RTOS_MUTEX());
}
/* This routine takes as an argument a mutex handle that was returned by
   rtfs_port_alloc_mutex(). If the mutex is already claimed it must wait for
   it to be released and then claim the mutex and return.
*/

void rtfs_port_claim_mutex(dword handle)
{
#define CLAIM_RTOS_MUTEX()
    CLAIM_RTOS_MUTEX()
}

/* This routine takes as an argument a mutex handle that was returned by
rtfs_port_alloc_mutex() that was previously claimed by a call to
rtfs_port_claim_mutex(). It must release the handle and cause a caller
blocked in rtfs_port_claim_mutex() for that same handle to unblock.
*/

void rtfs_port_release_mutex(dword handle)
{
#define RELEASE_RTOS_MUTEX(X)
    RELEASE_RTOS_MUTEX(handle)
}

/* This routine takes no arguments and returns an unsigned long. The routine
must allocate and initialize a signalling device (typically a counting
semaphore) and set it to the "not signalled" state. It must return an
unsigned long value that will be used as a handle. ERTFS will
not interpret the value of the return value. The handle will only used as
an argument to the rtfs_port_clear_signal(), rtfs_port_test_signal()
and rtfs_port_set_signal() calls.
Only required for the supplied floppy disk and ide device driver if the
ide driver is running in interrupt mode. Otherwise leave this function as
it is, it will not be used. */

dword rtfs_port_alloc_signal(void)
{
#define ALLOC_RTOS_SIGNAL() (dword) 1
	return(ALLOC_RTOS_SIGNAL());
}

/* This routine takes as an argument a handle that was returned by
rtfs_port_alloc_signal(). It must place the signal in an unsignalled state
such that a subsequant call to rtfs_port_test_signal() will not return
success until rtfs_port_set_signal() has been called. This clear function
is neccessary since it is possible although unlikely that an interrupt
service routine could call rtfs_port_set_signal() after the intended call
to rtfs_port_test_signal() timed out. A typical implementation of this
function for a counting semaphore is to set the count value to zero or
to poll it until it returns failure. */
void rtfs_port_clear_signal(dword handle)
{
#define CLEAR_RTOS_SIGNAL()
    CLEAR_RTOS_SIGNAL()
}

/* This routine takes as an argument a handle that was returned by
rtfs_port_alloc_signal() and a timeout value in milliseconds. It must
block until timeout milliseconds have elapsed or  rtfs_port_set_signal()
has been called. If the test succeeds must return 0, if it times out it
must return a non-zero value.
Only required for the supplied floppy disk and ide device driver if the
ide driver is running in interrupt mode. Otherwise leave this function as
it is, it will not be used. */


int rtfs_port_test_signal(dword handle, int timeout)
{
#define TEST_RTOS_SIGNAL(X,Y) 1
    return(TEST_RTOS_SIGNAL(handle, timeout));
}
/*
This routine takes as an argument a handle that was returned by
rtfs_port_alloc_signal(). It must set the signal such that a subsequant
call to rtfs_port_test_signal() or a call currently blocked in
rtfs_port_test_signal() will return success.
This routine is always called from the device driver interrupt service
routine while the processor is executing in the interrupt context.
Only required for the supplied floppy disk and ide device driver if the
ide driver is running in interrupt mode. Otherwise leave this function as
it is, it will not be used. */


void rtfs_port_set_signal(dword handle)
{
#define SET_RTOS_SIGNAL(X)
    SET_RTOS_SIGNAL(handle)
}

/*
This routine takes as an argument a sleeptime value in milliseconds. It
must not return to the caller until at least sleeptime milliseconds have
elapsed. In a mutitasking environment this call should yield the task cpu.
*/

void rtfs_port_sleep(int sleeptime)
{
#define RTOS_SLEEP(X)
    RTOS_SLEEP(sleeptime)
}

/* This routine takes no arguments and returns an unsigned long. The routine
must return an unsigned long value that will later be passed to
rtfs_port_elapsed_check() to test if a given number of milliseconds or
more have elapsed. A typical implementation of this routine would read the
system tick counter and return it as an unsigned long. ERTFS makes no
assumptions about the value that is returned.
*/
#define RTOS_GET_MILLISECOND_TICKS() 1

dword rtfs_port_elapsed_zero(void)
{
    return((dword) RTOS_GET_MILLISECOND_TICKS());
}

/* This routine takes as arguments an unsigned long value that was returned by
a previous call to rtfs_port_elapsed_zero() and a timeout value in
milliseconds. If "timeout" milliseconds have not elapsed it should return
0. If "timeout" milliseconds have elapsed it should return 1. A typical
implementation of this routine would read the system tick counter, subtract
the zero value, scale the difference to milliseconds and compare that to
timeout. If the scaled difference is greater or equal to timeout it should
return 1, if less than timeout it should return 0.
*/

int rtfs_port_elapsed_check(dword zero_val, int timeout)
{
dword curr_time;
    curr_time = (dword) RTOS_GET_MILLISECOND_TICKS();
    if ( (curr_time - zero_val) > (dword) timeout)
        return(1);
    else
        return(0);
}

/* ddword get_perf_tick(void)

   This routine take no arguments and returns a 64 bit integer containing
   the current high speed timer value. This routine is used only for
   diagnostic purposes to benchmark performance of certain operations.
   Values returned by this routine are send to the remote rdconsole
   program and it uses them to calculate elpsed time and rates for
   various operations.

   Note: To get the best value from the remote diagnostics and benchmarking
   tool you should implement the high resolution timer support function if
   your hardware has such a resource. If one is not available then the default
   implementation using the system tick should be used. The units of the system
   tick are miliseconds, but the timer granularity is, typically 10 to 100
   miliseconds, which is not adequate for most purposes.


   Note: You must also implement get_perf_frequency() to return the
   frequency of the clock.

*/

ddword get_perf_tick(void)
{
ddword tick_val;
#define RTOS_GET_MICROS_HI() (dword) 0
#define RTOS_GET_MICROS_LO() (dword) 1
    tick_val = M64SET32(RTOS_GET_MICROS_HI(), RTOS_GET_MICROS_LO());
    return(tick_val);
}

/* dword get_perf_frequency(void)

   This routine takes no arguments and returns a 32 bit integer containing
   the frequency of the high speed timer. This routine, like get_perf_tick()
   is used only for diagnostic purposes to benchmark performance of
   certain operations. Values returned by this routine are send to the
   remote rdconsole program and it uses them to calculate elpsed time and
   rates for various operations.

   Example values that should be returned by the function.

   1000000 - If the clock increments one million times per second.
   10000   - If the clock increments ten thousand times per second.
   1000    - If the clock increments one thousand times per second.

*/

dword get_perf_frequency(void)
{
dword frequency;
    frequency = 1000000;
    return(frequency);
}



/*
This function must return an unsigned long number that is unique to the
currently executing task such that each time this function is called from
the same task it returns this same unique number. A typical implementation
of this function would get address of the current task control block, cast
it to a long, and return it.
*/

dword rtfs_port_get_taskid(void)
{
#define GET_RTOS_TASKID() 1
    return((dword)GET_RTOS_TASKID());
}


/*
This function must exit the RTOS session and return to the user prompt. It
is only necessary when an RTOS is running inside a shell environment like
Windows.
*/

void rtfs_port_exit(void)
{
#define RTOS_EXIT()
    RTOS_EXIT()
}

/* This routine must establish an interrupt handler that will call the
plain 'C' routine void mgmt_isr(void) when the the chip's management
interrupt event occurs. The value of the argument 'irq' is the interrupt
number that was put into the 82365 management interrupt selection register
and is between 0 and 15. This is controlled by the constant
"MGMT_INTERRUPT" which is defined in pcmctrl.c
*/


#if (INCLUDE_82365_PCMCTRL)
void hook_82365_pcmcia_interrupt(int irq)
{
    /* See polled.krn\portkern.c for example */
}
#endif /* (INCLUDE_82365_PCMCTRL) */

#if (INCLUDE_IDE)
/* hook_ide_interrupt() is called with the interrupt number in the argument
irq taken from the user's setting of pdr->interrupt_number in pc_ertfs_init().
Controller number is taken from the pdr->controller_number field as set in
pc_ertfs_init() by the user. Hook_ide_interrupt() must establish an interrupt
 handler such that the plain 'C' function "void ide_isr(int controller_number)"
is called when the IDE interrupt occurs. The argument to ide_isr() must be
the controller number that was passed to hook_ide_interrupt(), this value
is typically zero for single controller system.
*/

void hook_ide_interrupt(int irq, int controller_number)
{
    /* See polled.krn\portkern.c for example */
}
#endif /* (INCLUDE_IDE) */

/*  This routine is called by the floppy disk device driver. It must
establish an interrupt handler such that the plain 'C' function void
floppy_isr(void) is called when the floppy disk interrupt occurs.
The value in "irq" is always 6, this is the PC's standard mapping of
the floppy interrupt. If this is not correct for your system just ignore
the irq argument.
*/
#if (INCLUDE_FLOPPY)
void hook_floppy_interrupt(int irq)
{
    /* See polled.krn\portkern.c for example */
}
#endif /* (INCLUDE_FLOPPY) */
