/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/

/* drfloppy.c - Generic floppy disk driver routines.

    Routines in this file are generic routines to control an NEC765 class
    floppy disk controller in AT mode. The routines should be independent
    of executive environment and of the system hosting the 765 controller.

Public routines:
  These routines are  pointed to by the RTFS bdevsw table.

    floppy_ioctl        -   IO control function. Does nothing
    floppy_open         -   Drive open routine. Initializes the controller and
                            establishes interrupt vector if open count is zero.
                            Increments the open count.
    floppy_close        -   Decreases the open count. If the open count goes
                            to zero it releases interrupts.
    floppy_io           -   Performs reads/writes on the drives

  This routine is public but is not in the device table. You may call it
  directly.

    fl_format           -

  The rest of the routines are internal to the driver.

    fl_claim            -   Claims the controller for a drive. Establishes
                            the media type if need be and sets the data rate
                            so the disk may be read/written.
    fl_establish_media  -   Determines the media type of the floppy disk in
                            the drive by varying the data rate and attempting to
                            read the sector ID off of the disk. Also seeks the
                            head to clear the disk changed line.
    fl_sense_interrupt  -   Requests sense (staust registers) from the floppy
                            after reset,recal and seek
    fl_read_id          -   Reads sector IDs from a floppy. Used by establish
                            media
    fl_seek             -   Seeks the head.
    fl_recalibrate      -   Seeks the head to track 0 and resets chips internal
                            head position counter
    fl_specify          -   Specify head load/unload times and seek step rate
    fl_controller_init  -   Clears internal floppy tables, establishes interrupts
                            and resets the 765 chip.
    fl_reset_controller -   Called by fl_controller_init. Issues a reset to
                            the chip.
    fl_motor_on         -   Turns the floppy motor on and starts a daemon to
                            shut it off.
    fl_motor_off        -   The daemon calls this to shut off the floppy motor
    fl_command_phase    -   Sends command phase values to the chip. Called by
                            higher level routines like fl_read_id, fl_io etc.
    fl_results_phase    -   Reads results phase values from the chip. Called by
                            higher level routines like fl_read_id, fl_io etc.
    fl_ready            -   Determines what data transfer state the floppy
                            controller s state machine is in. TO, FROM or
                            none.
    fl_change_line      -   Called by floppy_io to check the drive s change
                            line to see if the floppy disk has been removed.
                            If the drive has been removed we call establish
                            media to clear it. We should probaly add a call
                            back mechanism here to alert the user to replace the
                            disk and possibly to give the file system a
                            chance to recover.
    fl_cp_parms         -   Copy parameters from the parameter table to the
                            _floppy structure.
    fl_dma_chk          -   Takes an address and returns the number of blocks
                            that may be transferd to that address before a
                            dma segment wrap occurs.
    fl_dma_init         -   Sets up the dma controller for a to or from memory
                            transfer from/to the floppy controller
    fl_read_data        -   Reads a byte from the 765 data register
    fl_read_drr         -   Reads a byte from the 765 data rate register
    fl_read_msr         -   Reads a byte from the 765 master status register
    fl_write_data       -   Writes a byte to the 765 data register
    fl_write_dor        -   Writes a byte to the 765 digital output register
    fl_write_drr        -   Writes a byte to the 765 data rate register
    fl_report_error     -   Converts floppy errors to strings and prints them
    fl_waitdma          -   Waits for dma to complete and checks status
*/

#include "rtfs.h"

#if (INCLUDE_FLOPPY)

//==================
// Section lifted from old port conf.h
/* These opcodes must be supported by the device driver's
   perform_device_ioctl() routine.
*/

#define DEVCTL_CHECKSTATUS 0
#define DEVCTL_WARMSTART        1
#define DEVCTL_POWER_RESTORE    2
#define DEVCTL_POWER_LOSS       3
#define DEVCTL_FORMAT           4
#define DEVCTL_GET_GEOMETRY     5
#define DEVCTL_REPORT_REMOVE    6
#define DEVCTL_SHUTDOWN         7



/* These codes are to be returned from the device driver's
   perform_device_ioctl() function when presented with
   DEVCTL_CHECKSTATUS as an opcode.
*/

#define DEVTEST_NOCHANGE        0 /* Status is 'UP' */
#define DEVTEST_NOMEDIA         1 /*  The device is empty */
#define DEVTEST_UNKMEDIA        2 /*  Contains unknown media */
#define DEVTEST_CHANGED         3 /*  Controller recognized and cleared a */
                                  /*  change condition */
#define BIN_VOL_LABEL 0x12345678L
// End section lifted from old port conf.h
//==============

#define N_FLOPPY_UNITS          2  /* If USE_FLOPPY is 1 this is the number of units */

#if (!(defined(__BORLANDC__) || defined(_MSC_VER)))
#error Floppy Driver supports x86 only
#endif

#include <dos.h>
#include <conio.h>
#include <ctype.h>
#if (defined(__BORLANDC__))
    #define _outp outp
    #define _inp inp
    #define POINTER_SEGMENT(X) FP_SEG(X)
    #define POINTER_OFFSET(X)  FP_OFF(X)
#else
    #define POINTER_SEGMENT(X) _FP_SEG(X)
    #define POINTER_OFFSET(X)  _FP_OFF(X)
#endif

#if (defined(__WIN32__) || (defined(_MSC_VER) && (_MSC_VER >= 800) && (_M_IX86 >= 300)) )
#define IS_FLAT 1
#else
#define IS_FLAT 0
#endif

/* If running in real mode on a PC and the BIOS clock ISR is enabled then
   this routine sets the bios motor timer counter to a high value so it does
   not turn off the floppy motors behind our back */

void shut_bios_timer_off(void)
{
#if (!IS_FLAT)
byte far *bios_motor_timer = 0;
    if (!bios_motor_timer)
    {
        bios_motor_timer = (byte far *) (MK_FP(0x40, 0x40));
    }
    *bios_motor_timer = 0xff;
#endif
}

void io_delay(void)     /*__fn__*/
{
    /* read the NMI Status Register - this delays ~1 usec; */
    /* PC specific */
    _inp(0x61);
}




/* Handy bit definitions */
#define BIT7 (byte) 0x80
#define BIT6 (byte) 0x40
#define BIT5 (byte) 0x20
#define BIT4 (byte) 0x10
#define BIT3 (byte) 0x08
#define BIT2 (byte) 0x04
#define BIT1 (byte) 0x02
#define BIT0 (byte) 0x01

/* Timing parameters */
#define FL_SPINUP       (1000)              /* Milliseconds to delay on spin up */
#define MOTOR_TIMEOUT   4                    /* Seconds until we shut the motor off */
                                             /* Note: The units (4) are correct
                                                for MOTOR_TIMEOUT since the
                                                floppy timer callback is called
                                                once per second */


/* Timeout values in milliiseconds for various operations */
#define FLTMO_IO        (6000)        /* 6 sec Read or write */
#define FLTMO_FMTTRACK  (6000)        /* 6 sec Format a track */
#define FLTMO_READID    (3000)       /* 3 sec Read ID during establish media */
#define FLTMO_SEEK      (1000)        /* 1 sek Seek to a track */
#define FLTMO_RECAL     (3000)        /* 3 sec Recalibrate (seek to 0) */
#define FLTMO_RESET     (6000)        /* 6 sec Wait for interrupt after reset */

/* Digital output register bits */
#define DORB_DSEL       BIT0
#define DORB_SRSTBAR    BIT2
#define DORB_DMAEN      BIT3
#define DORB_MOEN1      BIT4
#define DORB_MOEN2      BIT5
#define DORB_MODESEL    BIT7

/* Commands to the controller */
#define FL_SPECIFY           0x3    /* Specify drive timing */
#define FL_SENSE_STATUS      0x4    /* Read status register 3 */
#define FL_WRITE             0x5    /* Write block(s)  */
#define FL_READ              0x6    /* Read block(s)  */
#define FL_RECALIBRATE       0x7    /* Recalibrate */
#define FL_SENSE_INTERRUPT   0x8    /* Sense interrupt status */
#define FL_READID            0xa    /* Read sector id under head */
#define FL_FORMAT            0xd    /* Format track */
#define FL_SEEK              0xf    /* Seek to a cylinder */

/* Qualifier bits to read/write commands */
#define MFMBIT BIT6
#define MTBIT  BIT7

/* Arguments to specify command */
#define SP_1    (byte) 0xdf    /* step rate 3 milsec, head uload 240 */
#define SP_2    (byte) 0x2     /* head load = 2ms, dma enabled */
/* #define SP_2    (byte) 0x1      head load = 2ms, dma disabled */

/* Filler byte for use during format */
#define FORMAT_FILLER   0xf6

/* Error values */
#define FLERR_ABN_TERM                  1
#define FLERR_CHIP_HUNG                 2
#define FLERR_DMA                       3
#define FLERR_FORMAT                    4
#define FLERR_INVALID_INTERLEAVE        5
#define FLERR_IO_SECTOR                 6
#define FLERR_IO_TMO                    7
#define FLERR_MEDIA                     8
#define FLERR_RECAL                     9
#define FLERR_RESET                    10
#define FLERR_SEEK                     11
#define FLERR_SPECIFY                  12
#define FLERR_UNK_DRIVE                13
#define FLERR_EXEC_EST                 14


/* Drive types as stored in the AT cmos eeprom. We use the same constants for
   established media type in the drive. */
#define DT_NONE 0
#define DT_360  1
#define DT_12   2
#define DT_720  3
#define DT_144  4
#define DT_288  5

/* Data rate register values */
#define DR_250  0x02
#define DR_300  0x01
#define DR_500  0x00
#define DR_1000 0x03

/* Values returned from fl_ready. - If it returns 0 the controller is not
   ready to accept or send data */
#define FL_FRHOST   1           /* Controller expecting data from host */
#define FL_TOHOST   2           /* Controller has data for host */

/* Gap values. nemonics make the drive description table easier to read */
/* GPF is gpl_format */
#define GPF_50 0x50
#define GPF_54 0x54
#define GPF_6C 0x6C
/* GPL is gpl_read */
#define GPL_2A 0x2A
#define GPL_1B 0x1B

typedef struct _fl_devparms {
    int drive_size;           /* 360,720,1200,1440,2880 */
    byte drive_type;           /* DT_360 et al. Drive type from AT cmos */
    byte media_type;           /* DT_360 et al. Media installed in drive */
    byte specify_1;            /* StepRate:Head Unload */
    byte specify_2;            /* HeadLoad:NonDma      */
    byte data_rate;            /* DR_250 et al */
    byte sectors_per_track;
    byte cylinders_per_disk;
    byte gpl_read;             /* Gap durong read write */
    byte gpl_format;           /* Gap during format     */
} _FL_DEVPARMS;

typedef struct command_block
{
    byte data_rate_reg; /* Written to data rate register each time */
    int n_to_send;     /* Number of bytes to send in the command phase */
    int n_to_get;      /* Number of bytes expected in the results phase */
    int n_gotten;      /* Number gotten in the results phase */
    byte commands[10];  /* Sent during command phase */
    byte results[10];   /* Returned during results phase */
} COMMAND_BLOCK;

/* Each drive has one of these structures that we update one we know what the media type is */
typedef struct _floppy
{
    byte drive_type;           /* DT_360 et al. Drive type from AT cmos */
    byte media_type;           /* DT_NONE if not established */
    byte specify_1;            /* StepRate:Head Unload */
    byte specify_2;            /* HeadLoad:NonDma      */
    byte data_rate;            /* DR_250 et al */
    byte sectors_per_track;
    byte sectors_per_cyl;
    byte cylinders_per_disk;
    byte gpl_read;             /* Gap durong read write */
    BOOLEAN  double_step;
    word    last_cyl;
    word    last_head;
} _FLOPPY;


void hook_floppy_interrupt(int irq);
void rtfs_clear_floppy_signal(void);

typedef byte  * DMAPTYPE;
#define WriteRealMem(TO, FROM, N) far_copybuf((TO), (FROM), (N))
#define ReadRealMem(TO, FROM, N) far_copybuf((TO), (FROM), (N))
byte _dma_buffer[512];
DMAPTYPE dma_buffer;

DMAPTYPE alloc_dma_buffer(void)
{
    return((DMAPTYPE) &_dma_buffer[0]);
}

static void far_copybuf(DMAPTYPE to, DMAPTYPE from, int size)
{
    while (size--)
        *to++ = *from++;
}


/* Table of device paramters, The first field specifies the drive itself
   the second specifies the media installed, the rest are operating
   parameters. Note: the order of the table is important. When trynig to
   determine media type we find the drive type and then try each media
   type for that drive type in ascending order until one works. */

KS_CONSTANT _FL_DEVPARMS fl_devparms[7] = {
{360,  DT_360, DT_360, SP_1, SP_2, DR_250,  9, 40,  GPL_2A, GPF_50},
{1200, DT_12 , DT_12 , SP_1, SP_2, DR_500, 15, 80,  GPL_1B, GPF_54},
{360,  DT_12 , DT_360, SP_1, SP_2, DR_300,  9, 40,  GPL_2A, GPF_50},
{720,  DT_720, DT_720, SP_1, SP_2, DR_250,  9, 80,  GPL_2A, GPF_50},
{1440, DT_144, DT_144, SP_1, SP_2, DR_500, 18, 80,  GPL_1B, GPF_6C},
{720,  DT_144, DT_720, SP_1, SP_2, DR_250,  9, 80,  GPL_2A, GPF_50},
{0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
};


/*
* Return the drive type for the floppy disk.
* this routine may return any of the following value.
*   DT_NONE No drive
*   DT_360  360 K 5.25" drive
*   DT_12   1.2 M 5.25" drive
*   DT_720  720 K 3.50" drive
*   DT_144  1.44M 3.50" drive
*   DT_288  2.88M 3.50" drive
* It is currently hardwired to DT1_44, the most typical configuration
* some code to read the floppy type from CMOS in a PC environment is
* included but commented out.
*/

static byte get_floppy_type(int driveno)                             /*__fn__*/
{

    RTFS_ARGSUSED_INT(driveno);
    /* Return 1.44 MB by default. since almost everyone uses it */
    return(DT_144);
    /* Uncomment this code to read irt from the CMOS on a PC */
    /* Read the CMOS at offset 90 */
    /*                                      */
    /* _outp(0x70,(word) (0x90 | 0x80));    */
    /* utemp = _inp(0x71);              */
    /* if (driveno)                         */
    /*    return((byte)(utemp&0x0f));       */
    /* else                                 */
    /*    return((byte) (utemp >> 4));      */
}


/* Shadow of the digital output register. We write to this each time we
   write to the dig. output reg. */
byte shadow_dig_out;
int shut_mo_off_in;
/* Current selected floppy for use by fl_claim.
 Note: We use the following confusing convention:
    0 = None selected
    1 = Drive one selected
    2 = Drive two selected
  This is confusing because usually driveno == 0, 1 for the first two drives */
int selected_floppy;
/* Open function. This is pointed to by the bdevsw[] table */
int fl_controller_open_count;
_FLOPPY floppy[N_FLOPPY_UNITS];
BOOLEAN change_checked[N_FLOPPY_UNITS];

BOOLEAN floppy_open(void);
BOOLEAN floppy_io(int driveno, dword l_sector, void  *in_buffer, word count, BOOLEAN reading);
BOOLEAN floppy_format(int  driveno, int drivesize, int interleave);
static BOOLEAN fl_controller_init(void);
static BOOLEAN fl_reset_controller(void);
static BOOLEAN fl_claim(int driveno);
BOOLEAN fl_establish_media(int  driveno);
static BOOLEAN fl_sense_interrupt(COMMAND_BLOCK *p);
static BOOLEAN fl_read_id(int  driveno, COMMAND_BLOCK *p);
static BOOLEAN fl_seek(int  driveno, word cyl, word head);
static BOOLEAN fl_recalibrate(int  driveno);
static BOOLEAN fl_specify(int  driveno);
void fl_motor_off(void);
static void fl_motor_on(int  driveno);
static BOOLEAN fl_command_phase(COMMAND_BLOCK *p);
static BOOLEAN fl_results_phase(COMMAND_BLOCK *p);
static int fl_ready(void);
static BOOLEAN fl_change_line(int driveno);
static void fl_cp_parms(int  driveno, int parmindex);
static BOOLEAN fl_report_error(int error, BOOLEAN return_me);
static BOOLEAN fl_waitdma(int millis);
static BOOLEAN fl_dma_init(DMAPTYPE in_address, word length, BOOLEAN reading);
static word fl_dma_chk(DMAPTYPE in_address);
static void  fl_write_drr(byte value);
static byte fl_read_drr(void);
static void  fl_write_dor(byte value);
static void  fl_write_data(byte value);
static byte fl_read_data(void);
static byte fl_read_msr(void);

/* floppy_isr() - Implementation specific ISR routines.
*
*   This routine is the IBM-PC specific interupt service routine. It
*   calls ide_interrupt(controller_number), the platform independent
*   portion of the interrupt routine, and then clears the PIC
*
*/
void   floppy_isr()                          /*__fn__*/
{
    /* Signal interrupt complete */
    rtfs_port_set_signal(prtfs_cfg->floppy_signal);
}


BOOLEAN _floppy_open(void);


BOOLEAN floppy_open(void)                                    /*__fn__*/
{
    int i;

    for (i = 0; i < 3; i++)
        if (_floppy_open())
            return(TRUE);
    return(FALSE);
}

BOOLEAN _floppy_open(void)                                           /*__fn__*/
{
    /* Make sure we have a dma buffer in real memory. if not fail */
    if (!dma_buffer)
        dma_buffer = alloc_dma_buffer();
    if (!dma_buffer)
        return(FALSE);
    /* Initialize the controller */
    if (!fl_controller_open_count)
    {
        if (!fl_controller_init())
            return(FALSE);
    }
    fl_controller_open_count += 1;
    return(TRUE);
}

/* floppy_down() - Note that the floppy disk either not installed or
*  not formatted. Sets the media type to NONE.
*
*
*/
void floppy_down(int driveno)                                   /*__fn__*/
{
    floppy[driveno].media_type = (byte) DT_NONE;
    selected_floppy = 0;
}

/* Check if a floppy disk is installed. */

BOOLEAN floppy_installed(int driveno)                                   /*__fn__*/
{
    if (fl_claim(driveno))
        return(TRUE);
    else
        floppy_down(driveno);
    return(FALSE);
}



/* floppy_io - Floppy disk read write routine (in bdevsw[] table.)
*
* Summary:
*
* BOOLEAN floppy_io(DDRIVE *pdr, l_sector, in_buffer,  count, reading)
*
*
* Inputs:
*   word driveno      - 0 or 1 = a: b:
*   dword l_sector      - absolute sector to read/write
*   void *in_buffer     - address of data buffer
*   word count        - sectors to transfer
*   BOOLEAN reading        - TRUE if a read FALSE if a write
*
* Returns:
*   TRUE on success else FALSE.
*
* This routine performs read and writes from to the floppy disk. It calculates
* head:sector:track from the physical sector number, then it reads/write of
* track or count, whichever is less. If more data needs to be transfered it
* adjusts the current sector number, recalculates head:sector:track and repeats
* the IO procedure. This occurs until the process is completed.
*
* Note: On a PC AT the dma controller is a 16 BIT device. A 4 bit bank
*       switch register is used to address the lower 1 MEG. A segment
*       wrap occurs when a transfer spans two banks. When this routine
*       senses this condition it breaks the request into two or
*       more operations so the wrap does not occur.
*
* Note: This routine is basically portable except for the the DMA wrap
*       calculation. Also in protected mode 386/486 systems a double
*       buffering scheme needs to be added since we can only dma to the
*       first 1 meg. This double buffer logic will be identical to the
*       logic used for double buffering in the DMA segment wrap situation,
*       the only exception being the transfer should be larger than 1.
*
*/

BOOLEAN _floppy_io(DDRIVE *pdr, dword l_sector, void  *in_buffer, word count, BOOLEAN reading);

BOOLEAN floppy_io(DDRIVE *pdr, dword l_sector, void  *in_buffer, word count, BOOLEAN reading)   /*__fn__*/
{
int i;
    rtfs_port_claim_mutex(prtfs_cfg->floppy_semaphore);
    if (pdr)
    {
        for (i = 0; i < 3; i++)
            if(_floppy_io(pdr, l_sector, in_buffer, count, reading))
            {
                rtfs_port_release_mutex(prtfs_cfg->floppy_semaphore);
                return(TRUE);
            }
    }
    rtfs_port_release_mutex(prtfs_cfg->floppy_semaphore);
    return(FALSE);
}

BOOLEAN _floppy_io(DDRIVE *pdr, dword l_sector, void  *in_buffer, word count, BOOLEAN reading)   /*__fn__*/
{
    COMMAND_BLOCK b;
    word cyl;
    word head;
    word sec;
    word final_sec;
    word final_cyl;
    word final_head;
    word utemp;
    word sector;
    word nleft_track;
    word n_todo;
    byte   *buffer = (byte  *) in_buffer;
    int  unit_number;
    BOOLEAN   use_dma_buff;

    unit_number = pdr->logical_unit_number;

    if (!count)                         /* Must have a count */
        return(FALSE);

    /* Establish media if the drive is different from before */
    /* fl_claim has its own error reporting */
    if (!fl_claim(unit_number))
        return(FALSE);
    sector = (word) l_sector;

    /* cylinder == sector / sectors/cylinder */
    cyl =   (word) (sector/floppy[unit_number].sectors_per_cyl);
    utemp = (word) (sector%floppy[unit_number].sectors_per_cyl);
    head =  (word) (utemp/floppy[unit_number].sectors_per_track);
    sec =   (word) (utemp%floppy[unit_number].sectors_per_track);

    nleft_track = (word) (floppy[unit_number].sectors_per_track - sec);
    if (count > nleft_track)
        n_todo =  nleft_track;
    else
        n_todo = count;

    while (count)
    {
        if (fl_change_line(unit_number))
        {
            /* establish media. It has a hack in here to clear change line */
            /* establish media has its own error reporting */
            if (!fl_establish_media(unit_number))
                return(FALSE);
        }

        /* see how many blocks are left in the dma page containing buffer */
        utemp = fl_dma_chk((DMAPTYPE) buffer);
        if (!utemp)
        {
            /* None left in this page. use a local buffer for 1 block */
            n_todo = 1;
            use_dma_buff = TRUE;
        }
        else
        {
            /* Dma right to user space. Do not wrap the segment */
            use_dma_buff = FALSE;
            if (utemp < n_todo)
                n_todo = utemp;
        }

        utemp = cyl;
        /* Double step on 360 K disks in 1.2M drives */
        if (floppy[unit_number].double_step)
            utemp <<= 1;

        fl_motor_on(unit_number);

        if (!fl_seek(unit_number, utemp, head))
            return(fl_report_error(FLERR_SEEK, FALSE));
        /* Set up the dma controller */
        if (use_dma_buff)
        {
            if (!reading)       /* copy to a local buf before writing */
                WriteRealMem(dma_buffer, buffer, (n_todo << 9));
            if (!fl_dma_init((DMAPTYPE)dma_buffer, (word) (n_todo << 9), reading))
                return(fl_report_error(FLERR_DMA, FALSE));
        }
        else
        {
            /* Never run in protoected mode */
            if (!fl_dma_init((DMAPTYPE) buffer,(word) (n_todo << 9), reading))
                return(fl_report_error(FLERR_DMA, FALSE));
        }
        /* Send the command to the 765 floppy controller */
        b.n_to_send = 9;
        b.n_to_get = 7;
        if (reading)
            b.commands[0] = MTBIT|MFMBIT|FL_READ;
        else
            b.commands[0] = MTBIT|MFMBIT|FL_WRITE;
        b.commands[1] = (byte) ((head << 2)|unit_number);
        b.commands[2] = (byte) cyl;
        b.commands[3] = (byte) head;
        b.commands[4] = (byte) (sec+1);
        b.commands[5] = (byte) 2;               /* byte length 512 */
        b.commands[6] = (byte) (floppy[unit_number].sectors_per_track);/*  EOT */
        b.commands[7] = (byte) floppy[unit_number].gpl_read;    /*  GPL */
        b.commands[8] = (byte) 0xff;            /*  DTL = ff since N == 512 */

        fl_motor_on(unit_number);
        /* Clear any outstanding interrupt complete messages */
        rtfs_port_clear_signal(prtfs_cfg->floppy_signal);
        if (!fl_command_phase(&b))
        {
            return(fl_report_error(FLERR_CHIP_HUNG, FALSE));
        }

        /* Wait for the transfer to complete. (In programmed IO situations
           do the actual transfer) */
        if (!fl_waitdma((int)FLTMO_IO))
            return(fl_report_error(FLERR_IO_TMO, FALSE));

        /* If we read into a local buffer we need to copy it now to user space */
        if (use_dma_buff && reading)
            ReadRealMem(buffer, dma_buffer, (n_todo << 9));

        if (!fl_results_phase(&b))
            return(fl_report_error(FLERR_CHIP_HUNG, FALSE));
            /* Check status registers. We will elaborate on this later */
        if (b.results[0] & (BIT7|BIT6))
            return(fl_report_error(FLERR_ABN_TERM, FALSE));

        /* Check the head/sector/track against what it should be */
        final_sec = (word) (sec + n_todo);
        final_cyl = cyl;
        final_head = head;

        if (final_sec > floppy[unit_number].sectors_per_track)
        {
            final_head = 1;
            final_sec = (word) (final_sec - floppy[unit_number].sectors_per_track);
        }

        if (final_sec < floppy[unit_number].sectors_per_track)
            final_sec += (word)1;
        else if (final_sec == floppy[unit_number].sectors_per_track)
        {
            final_sec = 1;
            if (final_head == 1)
            {
                final_cyl += (word)1;
                final_head = 0;
            }
            else
                final_head = 1;
        }

        if ( (b.results[3] != (byte) final_cyl)  ||
             (b.results[4] != (byte) final_head) ||
             (b.results[5] != (byte) final_sec) )
        {
            return(fl_report_error(FLERR_IO_SECTOR, FALSE));
        }
         if (!count)
            break;

        /* Add n transfered to the sector.
           If we wrapped the track reset the sector pointer.
           This wrap will occur almost every time. When we have a dma
           wrap condition we truncated the sector count and will not
           wrap
        */

        sec = (word) (sec + n_todo);
        if (sec >= floppy[unit_number].sectors_per_track)
        {
            sec = 0;
            if (head)
            {
                head = 0;
                cyl += (word)1;
            }
            else
                head = 1;
        }

        count = (word) (count - n_todo);
        buffer += (n_todo<<9);

        /* Read the rest of the blocks or to the end of track. Whichever is less */
        /* Note: sec will usually be 0. unless a dma segment wrap occured. */
        utemp = (word) (floppy[unit_number].sectors_per_track - sec);
        if (count < utemp)
            n_todo = count;
        else
            n_todo = utemp;
    }

    return (TRUE);
}


/* floppy_format - Format a floppy disk.
*
* Summary:
*
* BOOLEAN floppy_format(driveno, drivesize, interleave)
*
*
* Inputs:
*   word driveno      - 0 or 1 = a: b:
*   int drivesize     - 360,1200,1440,720
*   int interleave    - interleave factor. (1 ==s no interleave gap)
*
* Returns:
*   TRUE on success else FALSE.
*
*   This routine formats every track on the drive. Before doing so it
*   validates the interleave and returns if it is wrong. The interleave
*   must be either 1 or it must follow these two rules.
*   It may not divide evenly into sectors/track and if sectors/track is an
*   even number interleave must be odd. If  sectors/track is an odd number
*   interleave must be even.
*
*   This routine is portable
*
*/

BOOLEAN floppy_format(int  driveno, int drivesize, int interleave) /*__fn__*/
{
COMMAND_BLOCK b;
int parmindex;
byte cylinder;
byte sector;
byte head;
int index;
byte *sec_info;

word utemp;
BLKBUFF *buf;
byte *parms_buffer;

    if (!interleave)
        return(FALSE);

    buf = pc_scratch_blk();
    parms_buffer = buf->data;

    for (parmindex = 0; fl_devparms[parmindex].drive_size; parmindex++)
    {
        if ( (fl_devparms[parmindex].drive_size == drivesize) &&
            (floppy[driveno].drive_type == fl_devparms[parmindex].drive_type) )
            break;
    }
    if (!fl_devparms[parmindex].drive_size)
     {
        pc_free_scratch_blk(buf);
        return(fl_report_error(FLERR_UNK_DRIVE, FALSE));
     }

    if ((interleave > 1))
    {
        /* Interleave can not be an even multiple of sec/per track */
        if ( (fl_devparms[parmindex].sectors_per_track%interleave == 0) ||
        /* Must be odd/even  or even/odd */
           ((fl_devparms[parmindex].sectors_per_track & 0x01) == (interleave & 0x01) ) )
              {
                pc_free_scratch_blk(buf);
                return(fl_report_error(FLERR_INVALID_INTERLEAVE, FALSE));
              }
    }

    /* load the floppy table from the device desc table. */
    fl_cp_parms(driveno, parmindex);

    fl_write_drr(floppy[driveno].data_rate);
    if (!fl_specify(driveno))
     {
        pc_free_scratch_blk(buf);
        return(fl_report_error(FLERR_SPECIFY, FALSE));
     }

    fl_motor_on(driveno);
    if (!fl_recalibrate(driveno))
     {
        pc_free_scratch_blk(buf);
        return(fl_report_error(FLERR_RECAL, FALSE));
     }

    for (cylinder = 0; cylinder < fl_devparms[parmindex].cylinders_per_disk; cylinder++)
    {
     /* print running status */
        RTFS_PRINT_STRING_1((byte *)"Formatting ", 0); /* "Formatting " */
        RTFS_PRINT_LONG_1  ( (dword)cylinder, 0);
        RTFS_PRINT_STRING_1((byte *)" of ", 0); /* " of " */
        RTFS_PRINT_LONG_1  ((dword)(fl_devparms[parmindex].cylinders_per_disk-1), 0);
        RTFS_PRINT_STRING_1((byte *)" total tracks ", PRFLG_CR); /* " total tracks " */

        fl_motor_on(driveno);
        for (head = 0; head < 2; head++)
        {
            utemp = cylinder;
            if (floppy[driveno].double_step)
                utemp <<= 1;
            if (!fl_seek(driveno, utemp, head))
                {
                pc_free_scratch_blk(buf);
                return(fl_report_error(FLERR_SEEK, FALSE));
                }
            index = 0;
            /* build of the sector IDs. Include interleave calculation */
            for (sector = 1; sector <= fl_devparms[parmindex].sectors_per_track; sector++)
            {
                sec_info    = &parms_buffer[(index*4)];
                *sec_info++ = cylinder;
                *sec_info++ = head;
                *sec_info++ = sector;
                *sec_info   = 0x02;         /* N = 2 for 512 byte sectors */
                /* These three line do the interleave calulation */
                index = (int)(index + interleave);
                if (index >= (int) fl_devparms[parmindex].sectors_per_track)
                    index = (int) (index - fl_devparms[parmindex].sectors_per_track);
            }

            b.n_to_send = 6;
            b.n_to_get = 7;
            b.commands[0] = (byte) (MFMBIT|FL_FORMAT);
            b.commands[1] = (byte) ((head << 2)|driveno);
            b.commands[2] = (byte) 0x02;   /* bytes/sector */
            b.commands[3] = (byte) fl_devparms[parmindex].sectors_per_track;
            b.commands[4] = (byte) fl_devparms[parmindex].gpl_format;
            b.commands[5] = (byte) FORMAT_FILLER;
            /* Set up to write sec_per_track * 4 bytes */
            /* Copy the format parameters to a real mode buffer */
            WriteRealMem(dma_buffer, parms_buffer, 512);
            if (!fl_dma_init((DMAPTYPE) dma_buffer,(word)((fl_devparms[parmindex].sectors_per_track)<<2), FALSE))
                {
                pc_free_scratch_blk(buf);
                return(fl_report_error(FLERR_DMA, FALSE));
                }

            /* Clear any outstanding interrupt complete messages */
            rtfs_port_clear_signal(prtfs_cfg->floppy_signal);

            fl_motor_on(driveno);

            if (!fl_command_phase(&b))
            {
                goto fmterr;
            }
            /* Wait for the io to complete */
            if (!fl_waitdma((int)FLTMO_FMTTRACK))
                goto fmterr;

            if (!fl_results_phase(&b))
                goto fmterr;

            /* Check status register 0 for abnormal termination */
            if (b.results[0] & (BIT7|BIT6))
                goto fmterr;


        }
    }
    pc_free_scratch_blk(buf);
    return(TRUE);

fmterr:
    pc_free_scratch_blk(buf);
    return(fl_report_error(FLERR_FORMAT, FALSE));
}



/*  fl_controller_init - Initialize the floppy controller
*
* summary
* BOOLEAN fl_controller_init()
*
*
* Inputs:
*
* Returns:
*   TRUE on success else FALSE.
*
* This routine zeroes out our data structures (floppy[0,1]). Then calls
* the host specific initialization code fl_establish() (sets up vectors/
* watchdogs). Then it attempts to reset the controller. If all is succesful
* it returns TRUE.
*
* Called by floppy_open()
*
*/

static BOOLEAN fl_controller_init()                                    /* __fn__ */
{
    /* Initialize interrupt vectors, timer, motor timer etc */
    hook_floppy_interrupt(6);
    prtfs_cfg->floppy_signal = rtfs_port_alloc_signal();
    if (!prtfs_cfg->floppy_signal)
        return(FALSE);

    /* Clear any pending signals */
    rtfs_port_clear_signal(prtfs_cfg->floppy_signal);

    floppy[0].drive_type = get_floppy_type(0);
    floppy[0].media_type = (byte) DT_NONE;
#if (N_FLOPPY_UNITS == 2)
    floppy[1].drive_type = get_floppy_type(1);
    floppy[1].media_type = (byte) DT_NONE;
#endif
    return(fl_reset_controller());
}

/* Issue a reset through the digital output register and wait for
   an interrupt. */
static BOOLEAN    fl_reset_controller()                                  /*__fn__*/
{
COMMAND_BLOCK b;

    /* Clear any outstanding interrupt complete messages */
    rtfs_port_clear_signal(prtfs_cfg->floppy_signal);

     /* reset the floppy by toggling the reset line (BIT 3) */
    shadow_dig_out = 0;

    fl_write_dor(shadow_dig_out);
    /* Clear reset. Enable DMA and interrupts */
    shadow_dig_out |= (DORB_SRSTBAR|DORB_DMAEN);
    fl_write_dor(shadow_dig_out);

    /* Wait up to 5 seconds for the interrupt to occur.*/
    if (rtfs_port_test_signal(prtfs_cfg->floppy_signal,(int)FLTMO_RESET)!=0)
    {
        return(fl_report_error(FLERR_RESET, FALSE));
    }
    /* Delay a little otherwise sense interrupt fails */
    rtfs_port_sleep(FL_SPINUP);
    if (!fl_sense_interrupt(&b))
        return(fl_report_error(FLERR_RESET, FALSE));

    return(TRUE);
}

/* fl_claim - Claim the controller for IO to a drive
*
* Summary:
*
* BOOLEAN fl_claim(driveno)
*
*
* Inputs:
*   int  driveno      - 0 or 1 = a: b:
*
* Returns:
*   TRUE on success else FALSE.
*
*   This routine is called by floppy_io each time it will do io to a drive.
*   if the drive is the current active drive it writes the data rate reg and
*   returns. Otherwise the other drive will be accessed. If the medai type
*   has not been established this is done. The controller timing parameters
*   are specified and the data rate is set.
*
*/

static BOOLEAN fl_claim(int driveno)                                  /*__fn__*/
{
    if (selected_floppy != (driveno+1))
    {
        selected_floppy = (int ) (driveno+1);
        if (floppy[driveno].media_type == DT_NONE)
        {
            if (!fl_establish_media(driveno))
                return(FALSE);
        }
        else if (!fl_specify(driveno))
            return(fl_report_error(FLERR_SPECIFY, FALSE));
        selected_floppy = (int ) (driveno+1);
    }
    fl_write_drr(floppy[driveno].data_rate);
    return(TRUE);
}

/* fl_establish_media - Vary the data rate and read until succesful
*
* Summary:
*
* BOOLEAN fl_establish_media(driveno)
*
*
* Inputs:
*   int  driveno      - 0 or 1 = a: b:
*
* Returns:
*   TRUE on success else FALSE.
*
* For each possible media type for this drive type in the devparms table.
* Set the data rate, recal ,seek and read sector ID information from the
* drive. If the read ID works return TRUE. If all possible media are exhausted
* return FALSE.
*
* NOTE: The floppy change line is cleared by the seek operations.
*/

BOOLEAN fl_establish_media(int  driveno)                        /*__fn__*/
{
int i;
COMMAND_BLOCK b;

    /* Find the first drive description entry for the drive type */
    for (i = 0; fl_devparms[i].drive_type; i++)
    {
        if (fl_devparms[i].drive_type == floppy[driveno].drive_type)
            break;
    }
    /* Now scan through all drive descriptions for this drive type until
      we get get one we can read */
    while (fl_devparms[i].drive_type == floppy[driveno].drive_type)
    {
        /* Copy params from the drive table to the device */
        fl_cp_parms(driveno, i);
        /* Set the data rate */
        fl_write_drr(floppy[driveno].data_rate);
        if (fl_specify(driveno))
        {
            fl_motor_on(driveno);
            if (fl_recalibrate(driveno))
            {
                /* read sector markers. if we get a normal terminaton we are ok */
                fl_motor_on(driveno);
                /* See if we can read the sector ID field */
                if (fl_read_id(driveno, &b))
                {
                    if ((b.results[0] & (BIT6|BIT7)) == 0)
                    {
                        /* Now we seek to cyl 2 and back to zero. This will
                           clear the change line if needed */
                        fl_motor_on(driveno);
                        if (!(fl_seek(driveno, 2, 0) && fl_seek(driveno, 0, 0)))
                            break;
                        else
                            return(TRUE);
                    }
                }
            }
        }
        i++;
    }
    floppy[driveno].media_type = DT_NONE;
    return(fl_report_error(FLERR_MEDIA, FALSE));
}

/*  fl_sense_interrupt - Issue a sense interrupt command
*
* Summary:
*
* BOOLEAN fl_sense_interrupt(COMMAND_BLOCK *p)
*
*
* Inputs:
*   COMMAND_BLOCK *p - storage area. On return the results[0,1,2] will contain
*                      status registers 0,1,2
*
* Returns:
*   TRUE on success else FALSE.
*
* This routine issues a sense interrupt command. It is called after
* seeks/recals and resets. The status registers are interpreted by
* the calling routine
*
*/

static BOOLEAN fl_sense_interrupt(COMMAND_BLOCK *p)                      /*__fn__*/
{
    p->n_to_send = 1;
    p->n_to_get = 2;
    p->commands[0] = FL_SENSE_INTERRUPT;
    /* commands to the chip (always) */
    if (fl_command_phase(p))
        return(fl_results_phase(p));
    return(FALSE);
}

/*  fl_read_id - Read sector ID
*
* Summary:
*
* BOOLEAN fl_read_id(int  driveno, COMMAND_BLOCK *p)
*
*
* Inputs:
*   int  driveno      - 0 or 1 = a: b:
*   COMMAND_BLOCK *p - storage area. On return the results[0,1,N] will
*                      be valid.
*
* Returns:
*   TRUE on success else FALSE.
*
* This routine is called by fl_establish_media. If the data rate is set
* right this routine should work. If it does work we know what the
* installed media is. fl_establish_media checks the return codes in the
* status registers.
*
*/

static BOOLEAN fl_read_id(int  driveno, COMMAND_BLOCK *p)              /*__fn__*/
{
    p->n_to_send = 2;
    p->n_to_get = 7;
    p->commands[0] = MFMBIT|FL_READID;
    p->commands[1] = (byte) driveno;

    /* Clear any outstanding interrupt complete messages */
    rtfs_port_clear_signal(prtfs_cfg->floppy_signal);

    /* commands to the chip (always) */
    if (fl_command_phase(p))
    {
        if (rtfs_port_test_signal(prtfs_cfg->floppy_signal, (int)FLTMO_READID)==0)
            return(fl_results_phase(p));
    }

    return(FALSE);
}

/*  fl_seek - Seek to cylinder/head
*
* Summary:
* BOOLEAN fl_seek(int  driveno, word cyl, word head)
*
*
* Inputs:
*   int  driveno      - 0 or 1 = a: b:
*   word cyl
*   word head
*
* Returns:
*   TRUE on success else FALSE.
*
* This routine issues a seek waits for an interupt and then checks
* the status by issueing a sense interupt command. If all went
* well it returns TRUE.
*
*/

static BOOLEAN fl_seek(int  driveno, word cyl, word head)          /*__fn__*/
{
COMMAND_BLOCK b;

/*AM: Only seek if moving to different track.  In that case, recalibrate. */
/*AM: You may speed up floppy by removing the recalibrate call which was inserted */
/*AM: to remedy problems with certain floppy controllers.   */
#if (0)
/* Removed because it was causing problems */
   if ((cyl == floppy[driveno].last_cyl) && (head == floppy[driveno].last_head))
      return(TRUE);
   fl_recalibrate(driveno);
#endif

    b.n_to_send = 3;
    b.n_to_get = 0;
    b.commands[0] = FL_SEEK;
    b.commands[1] = (byte) ((head<<2)| ((byte) driveno));
    b.commands[2] = (byte) cyl;

    /* Clear any outstanding interrupt complete messages */
    rtfs_port_clear_signal(prtfs_cfg->floppy_signal);

    if (fl_command_phase(&b))
    {
        if (rtfs_port_test_signal(prtfs_cfg->floppy_signal,(int)FLTMO_SEEK)==0)
        {
            if (fl_sense_interrupt(&b))
            {
                /* Should have BIT5 (seek end == 1) and bits 6 & 7 == 0
                   the cylinder should be the same as passed in */
                if ( ((b.results[0] & (BIT5|BIT6|BIT7)) == BIT5) &&
                     (b.results[1] == (byte) cyl) )
                     {
                     /*AM: save information for last successful seek so will know */
                     /*AM: to recalibrate if seeking new cylinder/head. */
                     floppy[driveno].last_cyl = (byte)cyl;
                     floppy[driveno].last_head = (byte)head;
                     return(TRUE);
                     }
            }
        }
    }

    return(FALSE);
}

/*  fl_recalibrate - Seek to cylinder 0, clear 765 s internal PCN register
*
* Summary:
* BOOLEAN fl_reclaibrate(int  driveno)
*
*
* Inputs:
*   int  driveno      - 0 or 1 = a: b:
*
* Returns:
*   TRUE on success else FALSE.
*
* This routine issues a recal, waits for an interupt and then checks
* the status by issueing a sense interupt command. If all went
* well it returns TRUE.
*
*/

static BOOLEAN fl_recalibrate(int  driveno)                           /*__fn__*/
{
COMMAND_BLOCK b;
int i;
byte status_0;

    floppy[driveno].last_cyl = 0;      /*AM: clear saved sector. */
    floppy[driveno].last_head = 0;

    for (i = 0; i < 2; i++)
    {
        b.n_to_send = 2;
        b.n_to_get = 0;
        b.commands[0] = FL_RECALIBRATE;
        b.commands[1] = (byte) driveno;

        /* Clear any outstanding interrupt complete messages */
        rtfs_port_clear_signal(prtfs_cfg->floppy_signal);

        if (fl_command_phase(&b))
        {
            if (rtfs_port_test_signal(prtfs_cfg->floppy_signal,(int)FLTMO_RECAL)==0)
            {
                if (fl_sense_interrupt(&b))
                {
                    status_0 = b.results[0];
                    if (status_0 & BIT4)        /* Missed track 0 on recal */
                    {
                        continue;
                    }
                    if (!(status_0 & BIT5))
                        continue;
                    if (status_0 & (BIT6|BIT7)) /* Not normal termination */
                        continue;
                    if (b.results[1])      /* PCN Should be 0 */
                        continue;
                    return(TRUE);
                }
            }
        }
    }
    return(FALSE);
}


/*  fl_specify - Issue a specify command to the drive
*
* Summary:
* BOOLEAN fl_specify(int  driveno)
*
*
* Inputs:
*   int  driveno      - 0 or 1 = a: b:
*
* Returns:
*   TRUE on success else FALSE.
*
* This routine issues a specify command for the drive to the controller
* this process sets the head load/unload delays. The delay values come
* from the _floppy[] structure which was copied from the devtable.
*
*/

static BOOLEAN fl_specify(int  driveno)                          /*__fn__*/
{
COMMAND_BLOCK b;

    b.n_to_send = 3;
    b.n_to_get = 0;
    b.commands[0] = FL_SPECIFY;
    b.commands[1] = floppy[driveno].specify_1;
    b.commands[2] = floppy[driveno].specify_2;

    if (fl_command_phase(&b))
        return(TRUE);
    else
        return(FALSE);
}

/* This is a callback that allows the floppy to shut the motor off */
void fl_motor_off(void)                                      /*__fn__*/
{
    if (shut_mo_off_in && !(--shut_mo_off_in))
    {
        shadow_dig_out &= ~(DORB_DSEL|DORB_MOEN2|DORB_MOEN1);
        fl_write_dor(shadow_dig_out);
    }
}

static void fl_motor_on(int  driveno)                               /*__fn__*/
{
BOOLEAN must_sleep;
    shut_bios_timer_off();

    /* rearm the motor timeout daemon for 3 seconds. Do this first so we are
       sure that the daemon wo not shut off the motors while we are in here */
    shut_mo_off_in = MOTOR_TIMEOUT;
    must_sleep = FALSE;
    if (driveno==1)
    {
        /* Turn on motor 2 and off motor 1 */
        if (!(shadow_dig_out & DORB_MOEN2))
        {
            shadow_dig_out |= DORB_DSEL|DORB_MOEN2;
            shadow_dig_out &= ~DORB_MOEN1;
            must_sleep = TRUE;
        }
    }
    else
    {
        /* Turn on motor 1 and off motor 2 */
        if (!(shadow_dig_out & DORB_MOEN1))
        {
            shadow_dig_out |= DORB_MOEN1;
            shadow_dig_out &= ~(DORB_DSEL|DORB_MOEN2);
            must_sleep = TRUE;
        }
    }
    fl_write_dor(shadow_dig_out);
    if (must_sleep)
    {
        /* Wait for the motor to come up to speed */
        rtfs_port_sleep(FL_SPINUP);

        /* Set the daemon again (3 seconds) */
        shut_mo_off_in = MOTOR_TIMEOUT;
    }
}


/*  Send command phase info to the floppy chip */
static BOOLEAN  fl_command_phase(COMMAND_BLOCK *p)                      /*__fn__*/
{
int  i;
int ready = 0;

    /* If the floppy is in the results phase clear it */
    for (i = 0; i < 10; i++)
    {
        ready = fl_ready();
        if (ready == FL_TOHOST)
            fl_read_data();
        else
            break;
    }
    for (i = 0; i < p->n_to_send; i++)
    {
        if (ready != FL_FRHOST)
            return(FALSE);
        fl_write_data(p->commands[i]);
        ready = fl_ready();
    }
    return(TRUE);
}

/*  Get results phase info from the floppy chip Return TRUE if results match expected */
static BOOLEAN  fl_results_phase(COMMAND_BLOCK *p)                       /*__fn__*/
{
int  i;

    p->n_gotten = 0;
    for (i = 0; i < p->n_to_get; i++)
    {
        if (fl_ready() != FL_TOHOST)
            return(FALSE);
        p->results[i] = fl_read_data();
    }
    p->n_gotten = p->n_to_get;
    return(TRUE);
}


/* Check the master status register. Return FL_TOHOST if the chip has data for
   the CPU, FL_FRHOST if it expects data, 0 IF The chip does not assert RQM */
static int fl_ready()                                              /*__fn__*/
{
byte main_status_register; /* Master status register */
int timeout;
int i;

    timeout = 32000;
/* Check the master status register for RQM. */
    main_status_register = 0;
    while (!(main_status_register & BIT7))
    {
        if (!timeout--)
        {
            return(0);
        }

        main_status_register = fl_read_msr();
        if (!(main_status_register & BIT7))
        {
            /* Sleep at least 25 uSeconds after reading MSR */
            for (i = 0; i < 25; i++) io_delay();
        }
    }

    /* If bit 6 is high the chip will send data to the CPU. */
    if (main_status_register & BIT6)
        return(FL_TOHOST);
    else
        return(FL_FRHOST);
}

/* Check the floppy disk change line. Return TRUE if the line has changed */
/* Note: We use change_checked[] to test if we just came up. Need to do
   this because change is false at first but we still need to repower */
static BOOLEAN fl_change_line(int  driveno)                            /*__fn__*/
{
    fl_motor_on(driveno);
    if (fl_read_drr() & BIT7)
    {
        change_checked[driveno] = TRUE;
        return(TRUE);
    }
    else
    {
        if (!change_checked[driveno])
        {
            change_checked[driveno] = TRUE;
            return(TRUE);
        }
        else
        {
            change_checked[driveno] = TRUE;
            return(FALSE);
        }
    }
}

/* copy values from the parameter table to the floppy structure */
static void fl_cp_parms(int  driveno, int parmindex)                /*__fn__*/
{
    floppy[driveno].drive_type = fl_devparms[parmindex].drive_type;
    floppy[driveno].media_type = fl_devparms[parmindex].media_type;
    floppy[driveno].specify_1 =  fl_devparms[parmindex].specify_1;
    floppy[driveno].specify_2 =  fl_devparms[parmindex].specify_2;
    floppy[driveno].data_rate =  fl_devparms[parmindex].data_rate;
    floppy[driveno].sectors_per_track = fl_devparms[parmindex].sectors_per_track;
    floppy[driveno].sectors_per_cyl = (byte) (2 * fl_devparms[parmindex].sectors_per_track);
    floppy[driveno].cylinders_per_disk = fl_devparms[parmindex].cylinders_per_disk;
    floppy[driveno].gpl_read = fl_devparms[parmindex].gpl_read;
    if ((fl_devparms[parmindex].drive_type == DT_12)&&( fl_devparms[parmindex].media_type == DT_360))
        floppy[driveno].double_step = TRUE;
    else
        floppy[driveno].double_step = FALSE;
}



/*
* Name:
*    fl_report_error()                Report and clear an error
*
* Summary:
*    BOOLEAN fl_report_error(int error, BOOLEAN return_me)
*
* Inputs:
*   error       - Error number
*   return_me   - This is returned
* Returns:
*   return_me
*
* Description:
*   This routine reports and clears errors. The table of error strings and
*   the printing of the errors is optional. Clearing the error consists of
*   reseting the controller and clearing the known media type. When IO is
*   is resumed the media type will be re-established.
*
* Porting considerations:
*
*/

KS_CONSTANT char * fl_errors[] = {
  "",   /* "" */
  "Abnormal Termination Error FLERR_ABN_TERM", /* "Abnormal Termination Error FLERR_ABN_TERM" */
  "Floppy controller not responding FLERR_CHIP_HUNG ", /* "Floppy controller not responding FLERR_CHIP_HUNG " */
  "Dma Error FLERR_DMA ", /* "Dma Error FLERR_DMA " */
  "Error during format FLERR_FORMAT ", /* "Error during format FLERR_FORMAT " */
  "Invalid Interleave For Media Type FLERR_INVALID_INTERLEAVE", /* "Invalid Interleave For Media Type FLERR_INVALID_INTERLEAVE" */
  "Sector Not Found FLERR_IO_SECTOR", /* "Sector Not Found FLERR_IO_SECTOR" */
  "Time out FLERR_IO_TMO ", /* "Time out FLERR_IO_TMO " */
  "Can not determine Media FLERR_MEDIA ", /* "Can not determine Media FLERR_MEDIA " */
  "Error During Recal FLERR_RECAL ", /* "Error During Recal FLERR_RECAL " */
  "Error resetting drive FLERR_RESET ", /* "Error resetting drive FLERR_RESET " */
  "Error during seek FLERR_SEEK ", /* "Error during seek FLERR_SEEK " */
  "Error during specify FLERR_SPECIFY", /* "Error during specify FLERR_SPECIFY" */
  "Unknown Drive type FLERR_UNK_DRIVE", /* "Unknown Drive type FLERR_UNK_DRIVE" */
};

static BOOLEAN fl_report_error(int error, BOOLEAN return_me)      /*__fn__*/
{
    switch(error)
    {
    case FLERR_ABN_TERM:
    case FLERR_CHIP_HUNG:
    case FLERR_DMA:
    case FLERR_FORMAT:
    case FLERR_IO_SECTOR:
    case FLERR_IO_TMO:
    case FLERR_MEDIA:
    case FLERR_RECAL:
    case FLERR_SEEK:
    case FLERR_SPECIFY:
         floppy[0].media_type = (byte) DT_NONE;
#if (N_FLOPPY_UNITS == 2)
         floppy[1].media_type = (byte) DT_NONE;
#endif
         fl_reset_controller();
/*         break; */
    case FLERR_RESET:
    case FLERR_INVALID_INTERLEAVE:
    case FLERR_UNK_DRIVE:
    case FLERR_EXEC_EST:
        RTFS_PRINT_STRING_1((byte *)fl_errors[error], PRFLG_NL);
        break;
    default:
        RTFS_PRINT_STRING_1((byte *)"Unspecified floppy error", PRFLG_NL); /* "Unspecified floppy error" */
        break;
    }
    return(return_me);
}

/*
* Name:
*    fl_waitdma() - Wait for a dma transfer to complete
*
* Summary:
*   fl_waitdma(int millis)
*
* Inputs:
*   millis - Fail if more than millis elapses
*
*
* Returns:
*   TRUE if the dma transfer was sucessful.
*
* Description:
*   This function is called when the driver is waiting for a dma transfer to
*   complete. The transfer was set up by fl_dma_setup().
*
*   This routine calls pc_wait_int(millis) for the floppy to interrupt
*   and then verifies that the dma channel has reach terminal count.
*   need to do is wait for the floppy interrupt. We seperated the two
*
*   Note: In a system using programmed transfer the transfer loop
*   would go inside here. The addresses and could would come from
*   fl_dma_init().
*
*/

/* DMA registers */
#define STATUS_REGISTER     0x08
#define MASK_REGISTER       0x0a
#define MODE_REGISTER       0x0b
#define FLIPFLOP_REGISTER   0x0c
#define BANK_REGISTER       0x81

/* Channel 2 register bank */
#define ADDRESS_REGISTER    0x04
#define int_REGISTER       0x05

static BOOLEAN fl_waitdma(int millis)                                     /*__fn__*/
{
byte status;
    if (rtfs_port_test_signal(prtfs_cfg->floppy_signal, millis) == 0)
    {
        /*      Check terminal count flag  */
        status = (byte) _inp(STATUS_REGISTER);
        if (status & BIT2)
        {
            return(TRUE);
        }
        else
        {
            return(FALSE);
        }
    }
    return(FALSE);
}

/*
* Name:
*    fl_dma_init() - Initialize a dma transfer
*
* Summary:
*    BOOLEAN fl_dma_init(DMAPTYPE in_address, word length, BOOLEAN reading)
*
* Inputs:
*   in_address - Address to transfer data to/from
*   length     - Length in bytes to transfer
*   reading    - If TRUE, floppy to memory, else mem to floppy
* Returns:
*   TRUE if the setup was successful. FALSE on a boundary wrap.
*
* Description:
*   This function is called to set up a dma transfer.
*
* Porting instructions:
*   This routine should be ported to support your dma controller. If you are
*   using programmed IO the input values should be stored away for use by
*   the transfer routine in fl_waitdma()
*/

dword kvtodma(DMAPTYPE in_address)
{
dword laddress;
#if (IS_FLAT)
    laddress = (dword) in_address;
#else
    laddress = (dword) POINTER_SEGMENT(in_address);
    laddress <<= 4;
    laddress += (dword) POINTER_OFFSET(in_address);
#endif
    return(laddress);
}

static BOOLEAN fl_dma_init(DMAPTYPE in_address, word length, BOOLEAN reading) /*__fn__*/
{
byte dma_page;
byte addr_low;
byte addr_high;
dword laddress;
word *p;
byte mode;


    length -= (word)1;
    laddress = kvtodma(in_address);


    p = (word *) &laddress;
/*    dma_page = (byte) (*(p+1) & 0xf); */
    dma_page = (byte) (*(p+1) & 0xff);
    addr_high = (byte) ((*p) >> 8);
    addr_low = (byte) ((*p) & 0xff);

    /* Convert from generic xfer request to dma processor specific */
    if (reading)
        mode = 0x46;        /* SINGLE MODE, READ, CHANNEL 2 */
    else
        mode = 0x4a;        /* SINGLE MODE, WRITE, CHANNEL 2 */


    rtfs_port_disable();
    _outp(MASK_REGISTER,BIT2|0x02);  /* disable channel 2 */
    rtfs_port_enable();

    _outp(MODE_REGISTER,mode);

    /* Write the bank switch register */
    _outp(BANK_REGISTER,dma_page);
    /* Write the address */
    _outp(FLIPFLOP_REGISTER,0x00);  /* clear flfl before writing address */
    /* Write the address. low byte high byte */
    _outp(ADDRESS_REGISTER,addr_low);
    _outp(ADDRESS_REGISTER,addr_high);

    /* Write the length. low byte high byte */
    _outp(FLIPFLOP_REGISTER,0x00);  /* clear flfl before writing count */
    _outp(int_REGISTER, (byte) (length & 0xff));
    _outp(int_REGISTER, (byte) (length >> 8));
    _outp(MASK_REGISTER,0x02);  /* re-enable channel 2 */
    return (TRUE);
}

/*
* Name:
*    fl_dma_chk() -           Returns the number of blocks beyond the current
*                               addesss in the address s dma page
* Summary:
*   word fl_dma_chk(byte far *in_address)
*
*
* Inputs:
*   in_address - Address to check
*
*
* Returns:
*   The number of blocks beyond the current in the dma page.
*
* Description:
*   This function is called when the driver is preparing for a dma transfer.
*   It returns the number of blocks left in the page beyond the current
*   address.
*
*/

static word fl_dma_chk(DMAPTYPE in_address)                    /*__fn__*/
{
word utemp;
dword laddress;

    laddress = kvtodma(in_address);
    utemp = (word) (laddress & 0xffff);

    if (utemp == 0)
        return(128);
    else
    {
        /* bytes between here and the top */
        utemp = (word) (0xffff - utemp);
        utemp += (word)1;
        return((word)(utemp >> 9));
    }
}
/* Register access functions -
*
*   The following functions access the the 37c65 floppy disk registers
*   We segregated these into simple routines to make it easier on NON-AT
*   systems.
*
*
*/
/* Write data rate register */
static void  fl_write_drr(byte value)                             /*__fn__*/
{
    _outp(0x3f7, value);
}
/* Read data rate register */
static byte fl_read_drr()                                           /*__fn__*/
{
    return((byte) _inp(0x3f7));
}
/* Write to digital output register */
static void  fl_write_dor(byte value)                             /*__fn__*/
{
    _outp(0x3f2, value);
}
/* Write to data register */
static void  fl_write_data(byte value)                             /*__fn__*/
{
    _outp(0x3f5, value);
}
/* Read data register */
static byte fl_read_data()                                        /*__fn__*/
{
    return((byte) _inp(0x3f5));
}
/* Read master status register */
static byte fl_read_msr()                                        /*__fn__*/
{
    return((byte) _inp(0x3f4));
}


int ph_first_floppy(void)
{
    return(0);  /* This ca not happen */
}
int ph_last_floppy(void)
{
    return(0);
}




static int floppy_perform_device_ioctl(DDRIVE *pdr, int opcode, void * pargs)
{
DEV_GEOMETRY gc;        /* used by DEVCTL_GET_GEOMETRY */
int size;
byte fltype;


    switch (opcode)
    {
        /* Getgeometry and format share common code */
        case DEVCTL_GET_GEOMETRY:
        case DEVCTL_FORMAT:

        /* Prepare to return an initialized format parameter structure */
        rtfs_memset(&gc, 0, sizeof(gc));
        /* Force format routine to use format params that we specify */
        gc.fmt_parms_valid = TRUE;
        rtfs_cs_strcpy(&gc.fmt.oemname[0], (byte *) pustring_sys_oemname, CS_CHARSET_NOT_UNICODE);
        gc.fmt.physical_drive_no =   0;
        gc.fmt.binary_volume_label = BIN_VOL_LABEL;
        rtfs_cs_strcpy(gc.fmt.text_volume_label, (byte *) pustring_sys_volume_label, CS_CHARSET_NOT_UNICODE);

        /* Get the floppy type from the OS and format based on it   */
        fltype = get_floppy_type(pdr->logical_unit_number);

        switch (fltype)
        {
            case DT_360:
            gc.fmt.secpalloc =      (byte)  2;
            gc.fmt.secreserved =    (word) 1;
            gc.fmt.numfats     =    (byte)  2;
            gc.fmt.secpfat     =    (word) 2;
            gc.fmt.numroot     =    (word) 112;
            gc.fmt.mediadesc =      (byte)  0xFD;
            gc.fmt.secptrk     =    (word) 9;
            gc.fmt.numhead     =    (word) 2;
            gc.fmt.numcyl     =     (word) 40;
            size = 360;
            break;
            case DT_12:
            gc.fmt.secpalloc =      (byte)     1;
            gc.fmt.secreserved =    (word)    1;
            gc.fmt.numfats     =    (byte)     2;
            gc.fmt.secpfat     =    (word)    7;
            gc.fmt.numroot     =    (word)    224;
            gc.fmt.mediadesc =      (byte)     0xF9;
            gc.fmt.secptrk     =    (word)    15;
            gc.fmt.numhead     =    (word)    2;
            gc.fmt.numcyl     =     (word)    80;
            size = 1200;
            break;
            case DT_720:
            gc.fmt.secpalloc =      (byte)  2;
            gc.fmt.secreserved =    (word) 1;
            gc.fmt.numfats     =    (byte)  2;
            gc.fmt.secpfat     =    (word) 3;
            gc.fmt.numroot     =    (word) 0x70;
            gc.fmt.mediadesc =      (byte)  0xF9;
            gc.fmt.secptrk     =    (word) 9;
            gc.fmt.numhead     =    (word) 2;
            gc.fmt.numcyl     =     (word)  80;
            size = 720;
            break;
            case DT_144:
            gc.fmt.secpalloc =      (byte)     1;
            gc.fmt.secreserved =    (word)    1;
            gc.fmt.numfats     =    (byte)     2;
            gc.fmt.secpfat     =    (word)    9;
            gc.fmt.numroot     =    (word)    224;
            gc.fmt.mediadesc =      (byte)     0xF0;
            gc.fmt.secptrk     =    (word)    18;
            gc.fmt.numhead     =    (word)    2;
            gc.fmt.numcyl     =     (word)    80;
            size = 1440;
            break;
            default:
                return(-1);
        }
        /* They are all 12 bit fats */
        gc.fmt.nibs_per_entry       = 3;
        /* Now set the geometry section */
        gc.dev_geometry_heads       = gc.fmt.numhead;
        gc.dev_geometry_cylinders   = gc.fmt.numcyl;
        gc.dev_geometry_secptrack   = gc.fmt.secptrk;

        if (opcode == DEVCTL_GET_GEOMETRY)
        {
            copybuff(pargs, &gc, sizeof(gc));
            return (0);
        }
        else
        {
            if (floppy_format((word)pdr->logical_unit_number, size, 1))
            {
                /* Resync the device so we do not get a change event */
                floppy_down(pdr->logical_unit_number);
                fl_claim(pdr->logical_unit_number);

                return(0);
            }
        }
        return(-1);
        case DEVCTL_CHECKSTATUS:
        /* Check device status and return */
        /*  DEVTEST_NOCHANGE */
        /*  DEVTEST_NOMEDIA, DEVTEST_UNKMEDIA or DEVTEST_CHANGED */

            /* Check if changed or just starting */
            if (fl_change_line(pdr->logical_unit_number) || !selected_floppy)
            {
                floppy_down(pdr->logical_unit_number);
                /* Note: We return changed but the next request should work */
                if (floppy_installed(pdr->logical_unit_number))
                    return(DEVTEST_CHANGED);
                else
                    return(DEVTEST_NOMEDIA);
            }
            else
                return(DEVTEST_NOCHANGE);

        case DEVCTL_WARMSTART:
            if (!prtfs_cfg->floppy_semaphore)
                prtfs_cfg->floppy_semaphore = pc_rtfs_alloc_mutex("floppy");
            if (!prtfs_cfg->floppy_semaphore)
                return(-1);
            shadow_dig_out = 0;
            shut_mo_off_in = 0;
            selected_floppy = 0;
            fl_controller_open_count = 0;
            rtfs_memset(floppy, 0, sizeof(floppy));
            rtfs_memset(change_checked, 0, sizeof(change_checked));

            /* Warm start function. Initialize the controller and set flags */
            /* Make sure we detect a change at first access */
            floppy_down(pdr->logical_unit_number);
            /* Only initialize the controller once */
            if (pdr->logical_unit_number)
                return(0);
            if (floppy_open())
            {
                pdr->drive_flags |= (DRIVE_FLAGS_VALID|DRIVE_FLAGS_REMOVABLE);
                return(0);
            }
            else
            {
                return(-1);
            }
        case DEVCTL_POWER_RESTORE:
            /* Fall through */
        case DEVCTL_POWER_LOSS:
            /* Fall through */
        default:
            break;
    }
    return(0);

}
/* ===================================== */
/* ======================================== */
#define SUPPORT_HOTSWAP 0

static int BLK_DEV_FLOPPY_blkmedia_io(void  *devhandle, void *pdrive, dword sector, void  *buffer, dword count, BOOLEAN reading);
static int  BLK_DEV_FLOPPY_blkmedia_ioctl(void  *handle_or_drive, void *pdrive, int opcode, int iArgs, void *vargs);

/* Sector buffer provided by BLK_DEV_FLOPPY_Ramdisk_Mount */
static byte floppydisk_sectorbuffer[512];

static DDRIVE dummy_floppy_drivestructure;



#define DEFAULT_NUM_SECTOR_BUFFERS  4
#define DEFAULT_NUM_DIR_BUFFERS     4
#define DEFAULT_NUM_FAT_BUFFERS     1
#define DEFAULT_FATBUFFER_PAGE      1
#define DEFAULT_OPERATING_POLICY    0

/* === pparms->blkbuff_memory one for each sector buffer */
static BLKBUFF _blkbuff_memory[DEFAULT_NUM_SECTOR_BUFFERS];
static byte _sector_buffer_memory[DEFAULT_NUM_SECTOR_BUFFERS*512];

/* === pparms->fatbuff_memory one for each FAT page */
static FATBUFF _fatbuff_memory[DEFAULT_NUM_FAT_BUFFERS];
static byte _fat_buffer_memory[DEFAULT_NUM_FAT_BUFFERS*DEFAULT_FATBUFFER_PAGE*512];


static int BLK_DEV_FLOPPY_configure_device(struct rtfs_media_insert_args *pmedia_parms, struct rtfs_media_resource_reply *pmedia_config_block, int sector_buffer_required)
{
    RTFS_ARGSUSED_PVOID((void *)pmedia_parms);

    rtfs_print_one_string((byte *)"Attaching a Floppy Disk driver at drive A:", PRFLG_NL);

	pmedia_config_block->requested_driveid = (int) ('A'-'A');
	pmedia_config_block->requested_max_partitions =  1;
	pmedia_config_block->use_fixed_drive_id = 1;
	pmedia_config_block->device_sector_buffer_size_bytes = 512;
	pmedia_config_block->use_dynamic_allocation = 0;

	if (sector_buffer_required)
	{
		pmedia_config_block->device_sector_buffer_base = (byte *) &floppydisk_sectorbuffer[0];
		pmedia_config_block->device_sector_buffer_data  = (void *) &floppydisk_sectorbuffer[0];
	}
	/*	0  if successful
		-1 if unsupported device type
		-2 if out of resources
	*/
	return(0);
}

static int BLK_DEV_FLOPPY_configure_volume(struct rtfs_volume_resource_request *prequest_block, struct rtfs_volume_resource_reply *pvolume_config_block)
{
    pvolume_config_block->drive_operating_policy 		= DEFAULT_OPERATING_POLICY;
    pvolume_config_block->n_sector_buffers 				= DEFAULT_NUM_SECTOR_BUFFERS;
    pvolume_config_block->n_fat_buffers    				= DEFAULT_NUM_FAT_BUFFERS;
    pvolume_config_block->fat_buffer_page_size_sectors = DEFAULT_FATBUFFER_PAGE;
    pvolume_config_block->n_file_buffers 				= 0;
    pvolume_config_block->file_buffer_size_sectors 		= 0;
    pvolume_config_block->blkbuff_memory 				= &_blkbuff_memory[0];
    pvolume_config_block->filebuff_memory 				= 0;
    pvolume_config_block->fatbuff_memory 				= &_fatbuff_memory[0];
    pvolume_config_block->sector_buffer_base 			= 0;
    pvolume_config_block->file_buffer_base 				= 0;
    pvolume_config_block->fat_buffer_base 				= 0;
    pvolume_config_block->sector_buffer_memory 			= (void *) &_sector_buffer_memory[0];
    pvolume_config_block->file_buffer_memory 			= 0;
    pvolume_config_block->fat_buffer_memory 			= (void *) &_fat_buffer_memory[0];

    if (prequest_block->failsafe_available)
	{
    	pvolume_config_block->fsrestore_buffer_size_sectors = 0;
    	pvolume_config_block->fsindex_buffer_size_sectors = 0;
    	pvolume_config_block->fsjournal_n_blockmaps 		= 0;
    	pvolume_config_block->fsfailsafe_context_memory 	= 0;
    	pvolume_config_block->fsjournal_blockmap_memory 	= 0;
    	pvolume_config_block->failsafe_buffer_base 			= 0; /* Only used for dynamic allocation */
    	pvolume_config_block->failsafe_buffer_memory 		= 0;
    	pvolume_config_block->failsafe_indexbuffer_base 	= 0; /* Only used for dynamic allocation */
    	pvolume_config_block->failsafe_indexbuffer_memory 	= 0;
	}
	return(0);
}


/* Call after Rtfs is intialized to start a floppy disk driver on A: */
BOOLEAN BLK_DEV_FLOPPY_Insert(void)
{
    /* Set up mount parameters and call Rtfs to mount the device    */
struct rtfs_media_insert_args rtfs_insert_parms;
DEV_GEOMETRY gc; 

    /* register with Rtfs File System */
    rtfs_insert_parms.devhandle = (void *) &dummy_floppy_drivestructure; /* Not used just a handle */
    rtfs_insert_parms.device_type = 999;	/* not used because private versions of configure and release, but must be non zero */
    rtfs_insert_parms.unit_number = 0;

    floppy_perform_device_ioctl(&dummy_floppy_drivestructure, DEVCTL_GET_GEOMETRY, (void *) &gc);

    rtfs_insert_parms.numheads = (dword) gc.dev_geometry_heads;
    rtfs_insert_parms.secptrk  = (dword) gc.dev_geometry_secptrack;
    rtfs_insert_parms.numcyl   = (dword) gc.dev_geometry_cylinders;
    rtfs_insert_parms.media_size_sectors = rtfs_insert_parms.numheads*gc.dev_geometry_secptrack*gc.dev_geometry_cylinders; 
    rtfs_insert_parms.sector_size_bytes =  (dword) 512;
    rtfs_insert_parms.eraseblock_size_sectors =   0;
    rtfs_insert_parms.write_protect    =          0;

    rtfs_insert_parms.device_io                = BLK_DEV_FLOPPY_blkmedia_io;
    rtfs_insert_parms.device_ioctl             = BLK_DEV_FLOPPY_blkmedia_ioctl;
    rtfs_insert_parms.device_erase             = 0;
    rtfs_insert_parms.device_configure_media    = BLK_DEV_FLOPPY_configure_device;
    rtfs_insert_parms.device_configure_volume   = BLK_DEV_FLOPPY_configure_volume;

    if (pc_rtfs_media_insert(&rtfs_insert_parms) < 0)
    	return(FALSE);
	else
    	return(TRUE);
}



static RTFS_DEVI_POLL_REQUEST_VECTOR poll_device_floppy_storage;
static void PollFloppyDeviceReady(void);

BOOLEAN BLK_DEV_FLOPPY_Floppydisk_Mount(void)
{

	dummy_floppy_drivestructure.logical_unit_number=0;
	dummy_floppy_drivestructure.drive_flags |= (DRIVE_FLAGS_VALID|DRIVE_FLAGS_REMOVABLE);
#if (SUPPORT_HOTSWAP)
	pc_rtfs_register_poll_devices_ready_handler(&poll_device_floppy_storage, PollFloppyDeviceReady);
	if (floppy_perform_device_ioctl(&dummy_floppy_drivestructure,  DEVCTL_CHECKSTATUS, (void *) &dummy_floppy_drivestructure)==DEVTEST_NOMEDIA)
		return TRUE;
#endif
	return BLK_DEV_FLOPPY_Insert();
}
static int BLK_DEV_FLOPPY_blkmedia_io(void  *devhandle, void *pdrive, dword sector, void  *buffer, dword count, BOOLEAN reading)
{
    RTFS_ARGSUSED_PVOID((void *)devhandle);
    RTFS_ARGSUSED_PVOID((void *)pdrive);

    return (int) floppy_io(&dummy_floppy_drivestructure, sector, buffer, (word)count, reading);
}


static int  BLK_DEV_FLOPPY_blkmedia_ioctl(void  *handle, void *pdrive, int opcode, int iArgs, void *vargs)
{
    RTFS_ARGSUSED_PVOID(handle);
    RTFS_ARGSUSED_PVOID(pdrive);
    RTFS_ARGSUSED_PVOID(vargs);
    RTFS_ARGSUSED_INT(iArgs);

    switch(opcode)
    {
        case RTFS_IOCTL_FORMAT:
            return floppy_perform_device_ioctl(&dummy_floppy_drivestructure, DEVCTL_FORMAT, (void *) 0);
			break;
		case RTFS_IOCTL_INITCACHE:
        case RTFS_IOCTL_FLUSHCACHE:
			break;
        default:
            return(-1);
    }
    return(0);
}
#if (SUPPORT_HOTSWAP)
static void PollFloppyDeviceReady()
{
	if (floppy_perform_device_ioctl(&dummy_floppy_drivestructure, DEVCTL_CHECKSTATUS, (void *) &dummy_floppy_drivestructure)==DEVTEST_CHANGED)
		BLK_DEV_FLOPPY_Insert();
}
#endif


#endif /* INCLUDE_FLOPPY */
