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
#include <malloc.h>
#include <pthread.h>
#include <errno.h>
#include <stdlib.h>
#define LINUXPRINTF printf

/*
This routine takes no arguments and returns an dword. The routine
must allocate and initialize a Mutex, setting it to the "not owned" state. It
must return an dword value that will be used as a handle. ERTFS will
not interpret the value of the return value. The handle will only used as
an argument to the rtfs_port_claim_mutex() and rtfs_port_release_mutex()
calls. The handle may be used as an index into a table or it may be cast
internally to an RTOS specific pointer. If the mutex allocation function
fails this routine must return 0 and the ERTFS calling function will return
failure.
*/
static pthread_mutex_t * handle_array[256];
static int handle_claim_count[256];
static int total_semaphores_allocated;

static dword map_handle_set(pthread_mutex_t *h)
{
dword i;
	if (!h)
		return(0);
	for (i = 1; i < 256; i++)
	{
		if (!handle_array[i])
		{
			handle_array[i] = h;
			return(i);
		}
	}
	return(0);
}

/* Free all mutex semaphores and signals allocated by RTFS */
static  void rtp_threads_shutdown (void);

void  rtfs_port_shutdown(void)
{
int i;
	for (i = 1; i < 256; i++)
	{
		if (handle_array[i])
		{
			pthread_mutex_destroy(handle_array[i]);
			free((void *)handle_array[i]);
			handle_array[i] = 0;
		}
    }
    rtp_threads_shutdown ();
}


dword rtfs_port_alloc_mutex(char *sem_name)
{
    pthread_mutex_t *linMutex;
    pthread_mutexattr_t attrs;

    linMutex = (pthread_mutex_t *) malloc (sizeof (pthread_mutex_t));
    if (!linMutex)
    {
        return(0);
    }

    pthread_mutexattr_init (&attrs);
    pthread_mutexattr_settype (&attrs, PTHREAD_MUTEX_RECURSIVE_NP);
    pthread_mutex_init (linMutex, &attrs);
    pthread_mutexattr_destroy (&attrs);

	total_semaphores_allocated += 1;
	return(map_handle_set(linMutex));
}

/* This routine takes as an argument a mutex handle that was returned by
   rtfs_port_alloc_mutex() and frees it by closing the handle and clearing
   the handle index
*/
void rtfs_port_free_mutex(dword handle)
{
    if (handle_array[handle])
	{
    	if (pthread_mutex_destroy((pthread_mutex_t *)handle_array[handle]) == 0)
    		free((void *)handle_array[handle]);
    	handle_array[handle] = 0;
    }
}

/* This routine takes as an argument a mutex handle that was returned by
   rtfs_port_alloc_mutex(). If the mutex is already claimed it must wait for
   it to be released and then claim the mutex and return.
*/

void rtfs_port_claim_mutex(dword handle)
{
    handle_claim_count[handle] += 1;
    if (handle_claim_count[handle] != 1)
    {
        LINUXPRINTF("Claim error, handle %d, claim count == %d\n", handle, handle_claim_count[handle]);
    }
    pthread_mutex_lock((pthread_mutex_t *)handle_array[handle]);
}

/* This routine takes as an argument a mutex handle that was returned by
rtfs_port_alloc_mutex() that was previously claimed by a call to
rtfs_port_claim_mutex(). It must release the handle and cause a caller
blocked in rtfs_port_claim_mutex() for that same handle to unblock.
*/

void rtfs_port_release_mutex(dword handle)
{
	handle_claim_count[handle] -= 1;
    if (handle_claim_count[handle] != 0)
    {
        LINUXPRINTF("Release error, handle %d, claim count == %d\n", handle, handle_claim_count[handle]);
    }
    pthread_mutex_unlock((pthread_mutex_t *)handle_array[handle]);
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
    usleep(sleeptime*1000);
}

/* This routine takes no arguments and returns an unsigned long. The routine
must return an unsigned long value that will later be passed to
rtfs_port_elapsed_check() to test if a given number of milliseconds or
more have elapsed. A typical implementation of this routine would read the
system tick counter and return it as an unsigned long. ERTFS makes no
assumptions about the value that is returned.
*/

static unsigned long RTOS_GET_MILLISECOND_TICKS (void)
{
unsigned long elapsed_msec;
struct timeval timeval;

    gettimeofday (&timeval, 0);
    elapsed_msec = timeval.tv_sec * 1000 + timeval.tv_usec / 1000;
    return (elapsed_msec);
}

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

void *rtfs_port_malloc(int nbytes)
{
void *p;
    p = malloc(nbytes);
 //   log_malloc(p,nbytes);
	return(p);
}
void rtfs_port_free(void *pbytes)
{
    //total_free_calls += 1;
   // log_free(pbytes);
    free(pbytes);
}

/*
This function must return an unsigned long number that is unique to the
currently executing task such that each time this function is called from
the same task it returns this same unique number. A typical implementation
of this function would get address of the current task control block, cast
it to a long, and return it.
*/

static pthread_key_t threadKey;

/*----------------------------------------------------------------------*
                            rtp_threads_init
 *----------------------------------------------------------------------*/
static void rtp_threads_init (void)
{
	if (threadKey)
		return;
    pthread_key_create( &threadKey, NULL);
}


static  void rtp_threads_shutdown (void)
{
	pthread_key_delete( threadKey );
}

dword rtfs_port_get_taskid(void)
{
	rtp_threads_init ();
    return((dword) pthread_getspecific (threadKey));
}


/*
This function must exit the RTOS session and return to the user prompt. It
is only necessary when an RTOS is running inside a shell environment like
Windows.
*/

void rtfs_port_exit(void)
{
	exit(0);
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
