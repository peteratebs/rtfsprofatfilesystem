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
*   Implementation for the Windows kernel porting layer
*
*   See template kernel porting layer file for more information
*
*/

#include <rtptime.h>
#include <rtpmem.h>

#define WINDOWSPRINTF printf

#include "rtfs.h"
#if (INCLUDE_V_1_0_DEVICES)     /* [FIX 17/May/2010 H.Mitsukude/AIC] */
#include "portconf.h"
#endif
/*
Rtfs does not use dynamic allocation while it is running except for when it is executing the RtfsProPlus test code
and command shell applications.

Rtfs will use dynamic allocation during run-time initialization if it is instructed to.

If the configuration constant RTFS_CFG_ALLOC_FROM_HEAP is enabled, RTFS will use dynamic allocation when it is
initialized to allocate system wide buffer and structure pools and to allocate structures and buffers associated
with individual drive ids.

If RTFS_CFG_ALLOC_FROM_HEAP is enabled pc_rtfs_config() calls rtfs_port_malloc() to pre-allocate pools of buffers
and structures that are then managed internally by rtfs resource management functions.

If RTFS_CFG_ALLOC_FROM_HEAP is not enabled pc_rtfs_config() uses static arrays of pools of buffers and structures that
are then managed internally by rtfs resource management functions.

If RTFS_CFG_ALLOC_FROM_HEAP is pc_rtfs_run() calls rtfs_port_malloc() to pre-allocate buffers and structures to be
passed to the pc_diskio_init() function.

If RTFS_CFG_ALLOC_FROM_HEAP is not enabled pc_rtfs_run() uses static arrays of pools of buffers and structures that it
passes to the pc_diskio_init() function.

Note: pc_rtfs_run() behaves differently when RTFS_CFG_ALLOC_FROM_HEAP is not enabled. When RTFS_CFG_ALLOC_FROM_HEAP
is not enabled pc_rtfs_run() only initializes one drive, the drive with the lowest device id. If more than one drive
is enabled the routine in apirun.c named set_default_disk_configuration() must be modified to support additional
drives.

Note: pc_diskclose() and pc_ertfs_shutdown() behave differently too when RTFS_CFG_ALLOC_FROM_HEAP is not enabled.
They can not release memory because they did not allocate any.

*/



void *rtfs_port_malloc(int nbytes)
{
    return (rtp_malloc(nbytes));
}
void rtfs_port_free(void *pbytes)
{
    rtp_free(pbytes);
}
void rtfs_port_malloc_stats()
{
}

/* Free all mutex semaphores and signals allocated by RTFS */
void  rtfs_port_shutdown(void)
{
}

dword rtfs_port_alloc_mutex(char *sem_name)
{
	return(1);
}

/* This routine takes as an argument a mutex handle that was returned by
   rtfs_port_alloc_mutex() and frees it by closing the handle and clearing
   the handle index
*/
void rtfs_port_free_mutex(dword handle)
{
}

/* This routine takes as an argument a mutex handle that was returned by
   rtfs_port_alloc_mutex(). If the mutex is already claimed it must wait for
   it to be released and then claim the mutex and return.
*/

void rtfs_port_claim_mutex(dword handle)
{
}

/* This routine takes as an argument a mutex handle that was returned by
rtfs_port_alloc_mutex() that was previously claimed by a call to
rtfs_port_claim_mutex(). It must release the handle and cause a caller
blocked in rtfs_port_claim_mutex() for that same handle to unblock.
*/

void rtfs_port_release_mutex(dword handle)
{
}
/* This routine takes no arguments and returns an dword. The routine
must allocate and initialize a signalling device (typically a counting
semaphore) and set it to the "not signalled" state. It must return an
dword value that will be used as a handle. ERTFS will
not interpret the value of the return value. The handle will only used as
an argument to the rtfs_port_clear_signal(), rtfs_port_test_signal()
and rtfs_port_set_signal() calls.
Only required for the supplied floppy disk and ide device driver if the
ide driver is running in interrupt mode. Otherwise leave this function as
it is, it will not be used. */

dword rtfs_port_alloc_signal(void)
{
    return 1;
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
  return 0;
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
}

/*
This routine takes as an argument a sleeptime value in milliseconds. It
must not return to the caller until at least sleeptime milliseconds have
elapsed. In a mutitasking environment this call should yield the task cpu.
*/

void rtfs_port_sleep(int sleeptime)
{
dword zero = rtfs_port_elapsed_zero();

	while (rtfs_port_elapsed_check(zero, sleeptime)==0)
		;
}

/* This routine takes no arguments and returns an dword. The routine
must return an dword value that will later be passed to
rtfs_port_elapsed_check() to test if a given number of milliseconds or
more have elapsed. A typical implementation of this routine would read the
system tick counter and return it as an dword. ERTFS makes no
assumptions about the value that is returned.
*/

dword rtfs_port_elapsed_zero(void)
{
    return rtp_get_system_msec();
}

/* This routine takes as arguments an dword value that was returned by
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
    curr_time = (dword) rtp_get_system_msec();
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



/*
This function must return an dword number that is unique to the
currently executing task such that each time this function is called from
the same task it returns this same unique number. A typical implementation
of this function would get address of the current task control block, cast
it to a long, and return it.
*/

dword rtfs_port_get_taskid(void)
{
    return(1);
}


/*
This function must exit the RTOS session and return to the user prompt. It
is only necessary when an RTOS is running inside a shell environment like
Windows.
*/

void rtfs_port_exit(void)
{

}

/* This routine must establish an interrupt handler that will call the
plain 'C' routine void mgmt_isr(void) when the the chip's management
interrupt event occurs. The value of the argument 'irq' is the interrupt
number that was put into the 82365 management interrupt selection register
and is between 0 and 15. This is controlled by the constant
"MGMT_INTERRUPT" which is defined in pcmctrl.c
*/

#define clear_pic(IRQ)
#define KS_INTFN_DECLARE(x)   void _cdecl x(void)
#define KS_INTFN_POINTER(x)   void (RTTAPI * x)(void)

#if (INCLUDE_82365_PCMCTRL)
extern void mgmt_isr(void);
int pcmcia_isr_no;

KS_INTFN_DECLARE(pcmcia_isr)
{
    mgmt_isr();
    clear_pic(pcmcia_isr_no);
}

void hook_82365_pcmcia_interrupt(int irq)
{
    pcmcia_isr_no = irq;
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

int ide_isr_no[2];
extern void ide_isr(int);
/* This is the interrupt service routine for controller 0 */
KS_INTFN_DECLARE(ide_isr_0)
{
    ide_isr(0);
    clear_pic(ide_isr_no[0]);
}

/* This is the interrupt service routine for IDE controller number one
   it calls the ide_isr() routine in ide_drv.c which calls rtfs_invoke_ide_interrupt(0)
   in this file
*/
/* This is the interrupt service routine for controller 1 */
KS_INTFN_DECLARE(ide_isr_1)
{
    ide_isr(1);
    clear_pic(ide_isr_no[1]);
}

void hook_ide_interrupt(int irq, int controller_number)
{
    ide_isr_no[controller_number] = irq;
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

int floppy_isr_no;
extern void floppy_isr();

KS_INTFN_DECLARE(floppy_isr_0)
{
    floppy_isr();
    clear_pic(floppy_isr_no);
}
void hook_floppy_interrupt(int irq)
{
    floppy_isr_no = irq;
}
#endif /* (INCLUDE_FLOPPY) */

#if (INCLUDE_RTFS_PROPLUS) /* Include get_perf_tick() and get_perf_frequency() RtfsPro Plus specific function */
ddword get_perf_tick(void)
{
    return(1);
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
    return(1);
}
#endif
