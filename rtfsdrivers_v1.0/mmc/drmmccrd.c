/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* drmmccrd.c - Portable multimedia card (MMC) device driver.
*
*
*/

#include "rtfs.h"
#include "portconf.h"   /* For included devices */

#if (INCLUDE_MMCCARD)

/* Functions provided by the porting later */
word read_mmc_word(dword bus_master_address);
void write_mmc_word(dword bus_master_address, word value);
BOOLEAN pcmctrl_init(void);
BOOLEAN pcmctrl_card_installed(int socket);

/* These are some porting-oriented defines.  If you are using a polled
   environment, make sure to toggle on the corresponding #define.  If you are
   using a PCI adapter to connect to the MultiMedia Card, then make sure
   USE_PCI_ADAPTER is 1. */
#define POLLED_DOS 0
#define POLLED_WINDOWS  1
#define POLLED_RTTARGET 0
#define USE_PCI_ADAPTER 0

/* Interrupts are not supported but there is some supporting code in place */

#define SUPPORT_INTERRUPTS 0

#define STANDBY_STATE           3
#define XFER_STATE              4

#define DEFAULT_BLOCK_LEN       512
#define MAX_RESPONSE_LEN        16
#define CID_LEN                 16
#define CSD_LEN                 16
#define DEF_OCR                 0xffc000 /*operating conditions register*/

#define OCR_RDY_STATE           0x80

#define STOP_CLOCK              0x01
#define START_CLOCK             0x02


/* STATUS Register values */
#define TIME_OUT_RD             0x0001
#define TIME_OUT_RESP           0x0002
#define CRC_WR_ERR              0x0004
#define CRC_RD_ERR              0x0008
#define SPI_RD_ERR              0x0010
#define RESP_CRC_ERR            0x0020
#define RESP_ERR                (RESP_CRC_ERR | SPI_RD_ERR | CRC_RD_ERR | \
                                CRC_WR_ERR | TIME_OUT_RESP | TIME_OUT_RD)
#define FIFO_BUFFER_EMPTY       0x0040
#define FIFO_BUFFER_FULL        0x0080
#define CLOCK_DISABLE           0x0100
#define RD_DATA_AVAILABLE       0x0800
#define DONE_WDATA_XFER         0x0800
#define DONE_PROG_RDWR          0x1000
#define END_CMD_RESP            0x2000

/* Clock Rate Values */
#define CLK_FULL                0x00    /* Master clock */
#define CLK_HALF                0x01    /* 1/2 Master clock */
#define CLK_FOUR                0x02    /* 1/4 Master clock */
#define CLK_EIGHT               0x03    /* 1/8 Master clock */
#define CLK_SIXTEEN             0x04    /* 1/16 Master clock */
#define CLK_THIRTYTWO           0x05    /* 1/32 Master clock */
#define CLK_SIXTYFOUR           0x06    /* 1/64 Master clock */


/* CMD_DAT_CONT register bit values*/

/*R0 indicates no response
  R1 indicates a 6-byte response -- used by SET_RELATIVE_ADDR, SEND_STATUS,
 STOP-TRANSMISSION, READ_SINGLE_BLOCK, and WRITER_BLOCK
  R2 indicates a 17-byte response -- used by SEND_CSD AND SEND_CID
  R3 indicates a 6-byte response -- used by SEND_OP_COND

  */

#define R0                      0x00
#define R1                      0x01
#define R2                      0x02
#define R3                      0x03
#define DATA_ENABLE             0x04
#define DATA_READ_SET           0x00
#define DATA_WRITE_SET          0x08
#define DATA_STREAM_BLK         0x10
#define BUSY_SET                0x20
#define SEND_80_CLOCKS          0x40

#define GO_IDLE_STATE           0x0
#define SEND_OP_COND            0x1
#define ALL_SEND_CID            0x2
#define SET_RELATIVE_ADDR       0x3
#define SET_DSR                 0x4
#define SELECT_DESELECT_CARD    0x7
#define SEND_CSD                0x9
#define SEND_CID                0xa
#define READ_DAT_UNTIL_STOP     0xb
#define STOP_TRANSMISSION       0xc
#define SEND_STATUS             0xd
#define SET_BUS_WIDTH_REGISTER  0xe
#define GO_INACTIVE_STATE       0xf
#define SET_BLOCKLEN            0x10
#define READ_BLOCK              0x11
#define READ_MULTIPLE_BLOCK     0x12
#define WRITE_DAT_UNTIL_STOP    0x14
#define WRITE_BLOCK             0x18
#define WRITE_MULTIPLE_BLOCK    0x19

/* Register access functions. These functions are provided in the porting layer in order to
   maximize portability and flexibility. See the implementation section at the end of this file
   for more details.
*/

word mmc_rd_rev_reg(dword register_file_address);
word mmc_rd_status_reg(dword register_file_address);
word mmc_rd_res_fifo_reg(dword register_file_address);
word mmc_rd_data_fifo_reg(dword register_file_address);
void mmc_wr_status_reg(dword register_file_address, word value);
void mmc_wr_rev_reg(dword register_file_address, word value);
void mmc_wr_clk_rate_reg(dword register_file_address, word value);
void mmc_wr_response_to_reg(dword register_file_address, word value);
void mmc_wr_read_to_reg(dword register_file_address, word value);
void mmc_wr_data_cont_reg(dword register_file_address, word value);
void mmc_wr_stop_clock_reg(dword register_file_address, word value);
void mmc_wr_blk_len_reg(dword register_file_address, word value);
void mmc_wr_nob_reg(dword register_file_address, word value);
void mmc_wr_data_fifo_reg(dword register_file_address, word value);
void mmc_wr_data_cmd_reg(dword register_file_address, word value);
void mmc_wr_arg_hi_reg(dword register_file_address, word value);
void mmc_wr_arg_lo_reg(dword register_file_address, word value);


#define NUM_MMC 1
#define DEBUG_MMC 0
#define SINGLE_SECTOR_READS  1        /*Keep set, multiple reads seem flakey*/
#define SINGLE_SECTOR_WRITES 0
#define DUMMY_BYTE 0
#define DUMMY_WORD 0
#define DUMMY_LONG 0

/* There is one of these per supported card. Even if we support only one at */
/* first we must do this */
typedef struct mmc_drive_desc
{
    word                open_count;
    byte                num_heads;
    byte                sec_p_track;
    word                sec_p_cyl;
    word                num_cylinders;
    dword               total_lba;
    byte                response[MAX_RESPONSE_LEN +1];
    dword               rca;
    word                last_status;
    int                 driveno;
}   MMC_DRIVE_DESC;

MMC_DRIVE_DESC mmc_drive_array[NUM_MMC];

void mmc_report_remove(int unit);
void hook_mmc_interrupt(int irq);
BOOLEAN find_mmc_adapter(DDRIVE *pdr);
BOOLEAN reset_adapter(DDRIVE *pdr);
BOOLEAN init_unit(DDRIVE *pdr);
BOOLEAN set_clock_rate(DDRIVE *pdr, word clock_rate);
void send80clocks(DDRIVE *pdr);
int SetupCmdAndSend(DDRIVE *pdr, dword cmd_arg, word cmd, word num_blocks, word resp_type);
BOOLEAN stop_clock(DDRIVE *pdr);
BOOLEAN start_clock(DDRIVE *pdr);
void issue_command(DDRIVE *pdr, dword cmd_arg, word cmd);
BOOLEAN get_response(DDRIVE *pdr, word resp_type);
BOOLEAN receive_data(DDRIVE *pdr, byte * read_buffer, word cur_count);
BOOLEAN send_data(DDRIVE *pdr, byte * write_buffer);
BOOLEAN set_xfer_state(DDRIVE *pdr);
BOOLEAN set_standby_state(DDRIVE *pdr);
BOOLEAN wait_status(DDRIVE *pdr, word desired_status, int wait_millis);


/* Open function. Call by WARMSTAART IOCTL Call*/
/*
* Note: This routine is called with the drive already locked so
*       in several cases there is no need for critical section code handling
*/


BOOLEAN mmc_drive_open(DDRIVE *pdr)                         /*__fn__*/
{
dword id_rca;
word drive_no;
dword blocknr;
dword wheads, wcyl;
word hwrk, swrk, dwrk;
word * buf;

    drive_no=(word) pdr->logical_unit_number;


    if (mmc_drive_array[drive_no].open_count)
        return(FALSE);

    id_rca=mmc_drive_array[drive_no].rca;

    if(!set_standby_state(pdr)) return FALSE;


    if (!SetupCmdAndSend(pdr, (id_rca << 16), SEND_CSD, 0, R2)) return(FALSE);

    /*Derive geometry from CSD data:

         Need to pull out C_SIZE and C_SIZE_MULT. Unfortunately, these aren't
         evenly boundaried within the CSD data. Once these are assembled by
         some fun bit twiddling, BLOCKNR calculation is performed. See
         MultiMediaCard product manual's description of OCR for breakdown
         on the elements, bit fields, etc. */

        buf=(word *) &mmc_drive_array[drive_no].response[0];

#if (KS_LITTLE_ENDIAN)
        swrk = (word)buf[3];
        blocknr = (dword)(swrk << 8) & 0x300;
        blocknr += (swrk >> 8);
        dwrk = ((word)buf[4]);
        blocknr <<= 2;
        blocknr += ((dword)dwrk & 3L) + 1L;

        hwrk = (word)(buf[4] >> 8);
        swrk = (word)(buf[5] >> 7);
        hwrk &= 3;
        dwrk = (word)((hwrk << 1)+ (swrk & 0x1));
#else
        swrk = (word)buf[3];
        blocknr = (dword)(swrk & 0x3FFL);
        blocknr <<= 2;
        swrk = (word)(buf[4]) >> 14) & 3;
        blocknr += (dword)(swrk & 3L) + 1L;

        hwrk = (word)(buf[4] & 3);
          hwrk <<= 1;
        swrk = (word)(buf[5]>> 15);
        dwrk = (hwrk + (swrk & 1));
#endif

   /* Now have capacity in sectors */
    blocknr <<= (dwrk + 2);

    mmc_drive_array[pdr->logical_unit_number].total_lba = blocknr;

    mmc_drive_array[pdr->logical_unit_number].sec_p_track = 32;

    wheads=2;

    if (blocknr > 0xffff) wheads=4;
    if (blocknr > 0x1ffff) wheads=8;
    mmc_drive_array[pdr->logical_unit_number].num_heads = (byte) wheads;

    wcyl=wheads * mmc_drive_array[pdr->logical_unit_number].sec_p_track;

    mmc_drive_array[pdr->logical_unit_number].sec_p_cyl   =  (word) wcyl;
    mmc_drive_array[pdr->logical_unit_number].num_cylinders = (word) (blocknr / wcyl);
    mmc_drive_array[pdr->logical_unit_number].open_count++;
    mmc_drive_array[pdr->logical_unit_number].driveno = pdr->driveno;

    return(TRUE);
}

/* Read/write function: */

BOOLEAN mmc_io(int driveno, dword sector, void *buffer, word count, BOOLEAN reading)    /*__fn__*/
{
word x;
DDRIVE *pdr;
dword io_address;
byte * io_buffer;

    io_buffer=(byte *) buffer;
    io_address=sector * DEFAULT_BLOCK_LEN;
    pdr = pc_drno_to_drive_struct(driveno);
    if (!pdr)
        return(FALSE);

    if (!count)                      /* Must have a count */
        return(FALSE);

    if(reading)
    {
/*Read and return TRUE if ok FALSE if failure */
#if (SINGLE_SECTOR_READS)
        for (x=0;x<count;x++)
        {
            if (!set_xfer_state(pdr)) return (FALSE);
            #if (DEBUG_MMC)
            rtfs_kern_puts((byte *)"did set xfer\n");
            #endif
            if (!SetupCmdAndSend(pdr, (dword) io_address, READ_BLOCK, 1, R1)) return(FALSE);
            #if (DEBUG_MMC)
            rtfs_kern_puts((byte *)"did setupcmdandsend\n");
            #endif
            if (!receive_data(pdr, io_buffer, 1)) return(FALSE);
            #if (DEBUG_MMC)
            rtfs_kern_puts((byte *)"did receive\n");
            #endif
            io_address+=DEFAULT_BLOCK_LEN;
            io_buffer+=DEFAULT_BLOCK_LEN;
        }
            return(TRUE);
#else
        word current_count  = count;

        if (!set_xfer_state(pdr)) return (FALSE);
#if (DEBUG_MMC)
        rtfs_kern_puts((byte *)"did set xfer\n");
#endif

        if (count > 1)
        {
            if (!SetupCmdAndSend(pdr, (dword) io_address, READ_MULTIPLE_BLOCK, count, R1)) return(FALSE);
#if (DEBUG_MMC)
            rtfs_kern_puts((byte *)"did setupcmdandsend (read multiple)\n");
#endif
        }
        else
        {
            if (!SetupCmdAndSend(pdr, (dword) io_address, READ_BLOCK, count, R1)) return(FALSE);
#if (DEBUG_MMC)
            rtfs_kern_puts((byte *)"did setupcmdandsend (read single) \n");
#endif
        }

        for (x=0;x<count;x++)
        {
            if (!receive_data(pdr, io_buffer, current_count)) return(FALSE);
#if (DEBUG_MMC)
            rtfs_kern_puts((byte *)"did receive\n");
#endif
            io_address+=DEFAULT_BLOCK_LEN;
            io_buffer+=DEFAULT_BLOCK_LEN;
            --current_count;
            if (x+1 != count) start_clock(pdr);

        }
        /*if did read multiple, must stop transmission */
        if (count > 1)
            if (!SetupCmdAndSend(pdr, 0, STOP_TRANSMISSION, 0, R1)) return(FALSE);

        return(TRUE);
#endif /* (SINGLE_SECTOR_READS) */
    }
    else    /*it's a write*/
    {
#if (SINGLE_SECTOR_WRITES)
        for (x=0;x<count;x++)
        {
            if (!set_xfer_state(pdr)) return (FALSE);
#if (DEBUG_MMC)
            rtfs_kern_puts((byte *)"did set xfer for write\n");
#endif
            if (!SetupCmdAndSend(pdr, (dword) io_address, WRITE_BLOCK, 1, R1)) return(FALSE);
#if (DEBUG_MMC)
            rtfs_kern_puts((byte *)"did setupcmdandsend for write\n");
#endif
            if (!send_data(pdr, io_buffer)) return(FALSE);
#if (DEBUG_MMC)
            rtfs_kern_puts((byte *)"did send of data\n");
#endif
            io_address+=DEFAULT_BLOCK_LEN;
            io_buffer+=DEFAULT_BLOCK_LEN;
        }
            return(TRUE);
#else
        if (!set_xfer_state(pdr)) return (FALSE);
#if (DEBUG_MMC)
        rtfs_kern_puts((byte *)"did set xfer for write\n");
#endif

        if (count > 1)
        {
            if (!SetupCmdAndSend(pdr, (dword) io_address, WRITE_MULTIPLE_BLOCK, count, R1)) return(FALSE);
#if (DEBUG_MMC)
            rtfs_kern_puts((byte*)"did setupcmdandsend (write multiple)\n");
#endif
        }
        else
        {
        if (!SetupCmdAndSend(pdr, (dword) io_address, WRITE_BLOCK, 1, R1)) return(FALSE);
#if (DEBUG_MMC)
        rtfs_kern_puts((byte*)"did setupcmdandsend (single block) for write\n");
#endif
        }

/*Write and return TRUE if ok FALSE if failure */
        for (x=0;x<count;x++)
        {
            if (!send_data(pdr, io_buffer)) return(FALSE);
#if (DEBUG_MMC)
            rtfs_kern_puts((byte*)"did send of data\n");
#endif
            start_clock(pdr);
            io_address+=DEFAULT_BLOCK_LEN;
            io_buffer+=DEFAULT_BLOCK_LEN;
        }

        if (!wait_status(pdr, DONE_PROG_RDWR, 2000)) return(FALSE);
        /*if did write  multiple, must stop transmission */
        if (!wait_status(pdr, DONE_WDATA_XFER , 2000)) return(FALSE);
          if (count > 1)
              if (!SetupCmdAndSend(pdr, 0, STOP_TRANSMISSION, 0, R1)) return(FALSE);
        if (!wait_status(pdr, DONE_PROG_RDWR, 2000)) return(FALSE);
            return(TRUE);
#endif /* (SINGLE_SECTOR_WRITES) */
    }

}

int mmc_perform_device_ioctl(int driveno, int opcode, void * pargs)
{
DDRIVE *pdr;
DEV_GEOMETRY gc;        /* used by DEVCTL_GET_GEOMETRY */


    pdr = pc_drno_to_drive_struct(driveno);
    if (!pdr)
        return(-1);

    switch (opcode)
    {
        case DEVCTL_GET_GEOMETRY:
/*DUMMY - This should be OK if they were set up in the open */
        rtfs_memset(&gc, 0, sizeof(gc));
        gc.dev_geometry_heads       =       mmc_drive_array[pdr->logical_unit_number].num_heads;
        gc.dev_geometry_cylinders   =       mmc_drive_array[pdr->logical_unit_number].num_cylinders;
        gc.dev_geometry_secptrack   =       mmc_drive_array[pdr->logical_unit_number].sec_p_track;
        gc.dev_geometry_lbas        =       mmc_drive_array[pdr->logical_unit_number].total_lba;
        copybuff(pargs, &gc, sizeof(gc));
        return (0);
        case DEVCTL_FORMAT:
/*DUMMY - Format the mmc media at mmc_drive_array[pdr->logical_unit_number] if needed */
/*DUMMY - If successful        return (0); */
/*DUMMY - Else                  return (-1); */
/*        return(-1); */
          return (0);
        case DEVCTL_REPORT_REMOVE:
/*DUMMY - This should be OK needs to be called from PCMCIA */
        pdr->drive_flags &= ~DRIVE_FLAGS_INSERTED;
        /* Close out the drive so we re-open */
        mmc_drive_array[pdr->logical_unit_number].open_count = 0;
        return(0);

        case DEVCTL_CHECKSTATUS:
            if (!(pdr->drive_flags & DRIVE_FLAGS_REMOVABLE))
                return(DEVTEST_NOCHANGE);
            if (pdr->drive_flags & DRIVE_FLAGS_INSERTED)
                return(DEVTEST_NOCHANGE);
            /* If the drive is open but the inserted is clear
               that means another partition accessed the drive
               and succeed so return CHANGED to force a remount
               with no low level drive initialization */
            if (mmc_drive_array[pdr->logical_unit_number].open_count)
            {
                pdr->drive_flags |= DRIVE_FLAGS_INSERTED;
                return(DEVTEST_CHANGED);
            }
#if (INCLUDE_PCMCIA)
            else if (pdr->drive_flags & DRIVE_FLAGS_PCMCIA)
            {
/*DUMMY - This should be OK */
                if (!pcmctrl_card_installed(pdr->pcmcia_slot_number))
                {
                    pdr->drive_flags &= ~DRIVE_FLAGS_INSERTED;
                    return(DEVTEST_NOMEDIA);
                }
/*DUMMY - Need to check this but should be okay */
                if (!init_unit(pdr) || !mmc_drive_open(pdr))
                   return(DEVTEST_UNKMEDIA);
                else
                {
                    pdr->drive_flags |= DRIVE_FLAGS_INSERTED;
                    return(DEVTEST_CHANGED);
                }
            }
#endif /* (INCLUDE_PCMCIA) */
            else
            {
                if (!init_unit(pdr) || !mmc_drive_open(pdr))
                {
                    return(DEVTEST_NOMEDIA);
                }
                else
                {
                    pdr->drive_flags |= DRIVE_FLAGS_INSERTED;
                    return(DEVTEST_CHANGED);
                }
            }

        case DEVCTL_WARMSTART:
            if (!pdr->partition_number && !pdr->logical_unit_number)    /* Only initialize the controller once */
            {


                /*Locate host adapter. If found, fill in (via pdr) base
                  address and interrupt used with the device*/

                if (!find_mmc_adapter(pdr)) return (-1);
#if (SUPPORT_INTERRUPTS)
                /* Set up interrupt service routines   */
                if (pdr->interrupt_number >= 0)
                {
                    hook_mmc_interrupt(pdr->interrupt_number);
                }
#endif
            }
#if (INCLUDE_PCMCIA)
            if (pdr->drive_flags & DRIVE_FLAGS_PCMCIA)
            {
/*DUMMY - Initialize pcmcia here */
                pcmctrl_init();
                /* Note that we are initialized */
                pdr->drive_flags |= DRIVE_FLAGS_VALID;
                pdr->drive_flags |= DRIVE_FLAGS_REMOVABLE;
            }
            else
#endif
            {
                if (!reset_adapter(pdr)) return (-1);

                pdr->drive_flags |= DRIVE_FLAGS_VALID | DRIVE_FLAGS_REMOVABLE;

                if (init_unit(pdr) && mmc_drive_open(pdr))
                {
                    pdr->drive_flags |= DRIVE_FLAGS_INSERTED;
                }
/*
  If your adapter does not implement an interrupt to detect media removal,
   try setting up a scheme where any given sector is read
   at some appropriate time that doesn't impact performance. If read
   fails, it could be because card to removed or it was removed and
   replaced.  Call mmc_report_remove(unit) where unit is the index
   of the card removed (0 to NUM_MMC - 1).

  If your adapter does implement an interrupt to detect media removal,
   set SUPPORT_INTERRUPTS to 1 and in find_mmc_adapter make sure to set
   pdr->interrupt_number.  The routines at the bottom of this function like
   hook_mmc_interrupt() should also be tweaked.  The end result of the
   interrupt should be a call to mmc_report_remove().

  If you do not want to support removing at all, just remove the
   DRIVE_FLAGS_REMOVABLE flag above.

*/
            }

            return(0);
            /* Fall through */
        case DEVCTL_POWER_RESTORE:
            /* Fall through */
        case DEVCTL_POWER_LOSS:
            /* Fall through */
        default:
            break;
    }
    return(0);

}


/* Call this function to indicate to the driver that the card has been removed. */
/* Could also call mmc_perform_device_ioctl() yourself, but this may be more convenient. */
void mmc_report_remove(int unit)
{
    if (unit >= 0 && unit < NUM_MMC)
    {
        mmc_perform_device_ioctl(mmc_drive_array[unit].driveno, DEVCTL_REPORT_REMOVE, 0);
    }
}


BOOLEAN reset_adapter(DDRIVE *pdr)
{
word revision_data;

    revision_data=mmc_rd_rev_reg(pdr->register_file_address);
    mmc_wr_rev_reg(pdr->register_file_address, revision_data);
    rtfs_port_sleep(4);
    return(TRUE);
}


/* init_unit() - Get bus started and reset all devices.
*
*
*/
BOOLEAN init_unit(DDRIVE *pdr)
{
dword id_rca;
word wtimer;

    wtimer=0x3;

    if (!stop_clock(pdr)) return (FALSE);

    /*For Identification process, need to run clock at <=250 mhz*/
    if (!set_clock_rate(pdr, CLK_THIRTYTWO)) return (FALSE);

    send80clocks(pdr);      /*starts the bus*/

    /*Now, reset all devices */

    if (!SetupCmdAndSend(pdr, 0, GO_IDLE_STATE, 0, 0)) return(FALSE);
#if (DEBUG_MMC)
    rtfs_kern_puts((byte*)"did go idle\n");
#endif


    while (wtimer--)
    {

        /*Send CMD1*/

        rtfs_port_sleep(10);             /*give time for settling after reset*/

        if (!SetupCmdAndSend(pdr, DEF_OCR, SEND_OP_COND, 0, R3)) continue;
#if (DEBUG_MMC)
        rtfs_kern_puts((byte*)"did sendop\n");
#endif

        if (mmc_drive_array[pdr->logical_unit_number].response[0] & OCR_RDY_STATE) break;
    }
    if (!wtimer) return(FALSE);


    /*Get CID*/

    if (!SetupCmdAndSend(pdr, 0, ALL_SEND_CID, 0, R2)) return(FALSE);
#if (DEBUG_MMC)
    rtfs_kern_puts((byte*)"did send cid\n");
#endif

    /*If more than one MMC is to be supported, all devices would be assigned
      rcas here*/

    id_rca=1;
    mmc_drive_array[pdr->logical_unit_number].rca=id_rca;
    rtfs_port_sleep(2);              /*give time for settling after reset*/
    if (!SetupCmdAndSend(pdr, id_rca << 16, SET_RELATIVE_ADDR, 0, R1)) return(FALSE);
#if (DEBUG_MMC)
    rtfs_kern_puts((byte*)"did set relative\n");
#endif
    rtfs_port_sleep(2);              /*give time for settling after reset*/
    set_clock_rate(pdr, CLK_FULL);
    return(TRUE);
}

BOOLEAN set_clock_rate(DDRIVE *pdr, word clock_rate)
{

    mmc_wr_clk_rate_reg(pdr->register_file_address, clock_rate);
    mmc_wr_response_to_reg(pdr->register_file_address, 0xFFFF);
    mmc_wr_read_to_reg(pdr->register_file_address, 0xFFFF);
    return(TRUE);
}

void send80clocks(DDRIVE *pdr)
{
    mmc_wr_data_cont_reg(pdr->register_file_address, SEND_80_CLOCKS);
}

BOOLEAN stop_clock(DDRIVE *pdr)
{
word status;
dword time_zero;

    time_zero = rtfs_port_elapsed_zero();

    mmc_wr_stop_clock_reg(pdr->register_file_address, STOP_CLOCK);

    /*Wait 1 second until clock is stopped*/

    while (!rtfs_port_elapsed_check(time_zero, 1000))
    {
        status = mmc_rd_status_reg(pdr->register_file_address);
        if ( status & CLOCK_DISABLE )
                return(TRUE);    /*It's disabled like it should be -- OK*/
    }

    return(FALSE); /*Never disabled; there's evidently a problem*/
}

BOOLEAN start_clock(DDRIVE *pdr)
{
    mmc_wr_stop_clock_reg(pdr->register_file_address, START_CLOCK);
    return(TRUE);
}



int SetupCmdAndSend(DDRIVE *pdr, dword cmd_arg, word cmd, word num_blocks, word resp_type)
{

word dat_reg_val, blk_len, x;

    if (!stop_clock(pdr)) return(FALSE);
    issue_command(pdr, cmd_arg, cmd);

    switch (cmd)
    {
    case READ_BLOCK:
    case READ_MULTIPLE_BLOCK:

        dat_reg_val=(DATA_ENABLE + DATA_READ_SET);
        blk_len=DEFAULT_BLOCK_LEN;
        break;

    case WRITE_BLOCK:
    case WRITE_MULTIPLE_BLOCK:

        dat_reg_val=(DATA_ENABLE + DATA_WRITE_SET);
        blk_len=DEFAULT_BLOCK_LEN;
        break;

    case STOP_TRANSMISSION:

        dat_reg_val=(DATA_WRITE_SET + BUSY_SET);
        blk_len=0;
        break;

    default:

        /*This is per Bob Chang at SanDisk */
        if (resp_type == R2)
        {
            dat_reg_val=0;
            blk_len=CID_LEN;
            num_blocks=1;
            break;
        }

        dat_reg_val=0;
        blk_len=0;
        break;

    }


    if (cmd != STOP_TRANSMISSION)
    {
        if (blk_len)
        {
            mmc_wr_blk_len_reg(pdr->register_file_address, blk_len);
            mmc_wr_nob_reg(pdr->register_file_address, num_blocks);
        }
    }
        dat_reg_val |= resp_type;

    mmc_wr_data_cont_reg(pdr->register_file_address, dat_reg_val);

    start_clock(pdr);

    for (x=0;x<MAX_RESPONSE_LEN;x++)
            mmc_drive_array[pdr->logical_unit_number].response[x]=0;

    return(get_response(pdr, resp_type));

}


BOOLEAN get_response(DDRIVE *pdr, word resp_type)
{
word wfifo, status, resp_len, x;
byte * resp_ptr;
word wdeb[17], z=0;
dword time_zero;

    time_zero = rtfs_port_elapsed_zero();



    if (resp_type == 0) return(TRUE);   /*no data involved, we're done*/

    resp_ptr=&mmc_drive_array[pdr->logical_unit_number].response[0];

    resp_len=6;     /*default to R3 and R1*/

    if (resp_type == R2) resp_len=CID_LEN; /*(note CSD also same length*/

    status = mmc_rd_status_reg(pdr->register_file_address);      /*gives a little delay*/

    while (!rtfs_port_elapsed_check(time_zero, 1000))
    {
        if (status & RESP_ERR) return(FALSE);

        if (status & END_CMD_RESP)
        {
            for (x=0;x<resp_len;x+=2)
            {
                wfifo=mmc_rd_res_fifo_reg(pdr->register_file_address);
                wdeb[z++]=wfifo;
                if (x)  /*first byte is trash they say*/
                    *resp_ptr++=(byte) (wfifo >> 8);

                *resp_ptr++=(byte) (wfifo & 0xff);
            }
            return(TRUE);       /*got it -- all is well*/
        }

        status = mmc_rd_status_reg(pdr->register_file_address);
    }

    return(FALSE); /*Never was able to get a reponse*/
}


BOOLEAN receive_data(DDRIVE *pdr, byte * read_buf_ptr, word cur_count)
{
word wfifo, status,x;
dword time_zero;

    time_zero = rtfs_port_elapsed_zero();

    while (!rtfs_port_elapsed_check(time_zero, 2000))
    {

        status = mmc_rd_status_reg(pdr->register_file_address);      /*gives a little delay*/
        if (status & RESP_ERR) return(FALSE);

        if ( (cur_count == 1 && (status & DONE_WDATA_XFER )) ||
               (cur_count > 1 && (status & FIFO_BUFFER_FULL )))
        {

            for (x=0;x<DEFAULT_BLOCK_LEN;x+=2)
            {

                wfifo= mmc_rd_data_fifo_reg(pdr->register_file_address);
                *read_buf_ptr++=(byte) (wfifo >> 8);
                *read_buf_ptr++=(byte) (wfifo & 0xff);
            }

        return(TRUE);       /*got it -- all is well*/
        }

    }

    return(FALSE); /*Never was able to get a reponse*/
}


BOOLEAN send_data(DDRIVE *pdr, byte * write_buf_ptr)
{
word wfifo, status,x;
dword time_zero;

    time_zero = rtfs_port_elapsed_zero();

    while (!rtfs_port_elapsed_check(time_zero, 1000))
    {
        status = mmc_rd_status_reg(pdr->register_file_address);      /*gives a little delay*/
        if (status & RESP_ERR) return(FALSE);
        if (status & FIFO_BUFFER_EMPTY)
        {
            for (x=0;x<DEFAULT_BLOCK_LEN;x+=2)
            {
                wfifo=(word) *write_buf_ptr++;
                wfifo <<= 8;
                wfifo |= (word) *write_buf_ptr++;
                mmc_wr_data_fifo_reg(pdr->register_file_address, wfifo);
            }


        return(TRUE);       /*sent it -- all is well*/
        }

    }

    return(FALSE); /*Never was able to get a FIFO empty reponse*/
}

BOOLEAN wait_status(DDRIVE *pdr, word desired_status, int wait_millis)
{
word status;
dword time_zero;

    time_zero = rtfs_port_elapsed_zero();
    while (!rtfs_port_elapsed_check(time_zero, wait_millis))
    {
        status = mmc_rd_status_reg(pdr->register_file_address);      /*gives a little delay*/
        if (status & desired_status) return(TRUE);
    }

    return(FALSE); /*Never was able to get desired status*/
}

void issue_command(DDRIVE *pdr, dword cmd_arg, word cmd)
{
word wtemp;
    mmc_wr_data_cmd_reg(pdr->register_file_address, cmd);
    wtemp=(word) (cmd_arg >> 16);
    mmc_wr_arg_hi_reg(pdr->register_file_address, wtemp);
    mmc_wr_arg_lo_reg(pdr->register_file_address, (word)(cmd_arg & 0xffff));
}

BOOLEAN set_xfer_state(DDRIVE *pdr)
{
dword id_rca;
byte card_state;
byte rcount=0;

    retry:
    id_rca=mmc_drive_array[pdr->logical_unit_number].rca;
#if (DEBUG_MMC)
    rtfs_kern_puts((byte*)"in xfer: about to do send status\n");
#endif

    if (!SetupCmdAndSend(pdr, (id_rca << 16), SEND_STATUS, 0, R1)) return(FALSE);
#if (DEBUG_MMC)
    rtfs_kern_puts((byte*)"in xfer: successfully did send status\n");
/*  stat1=mmc_drive_array[pdr->logical_unit_number].response[0];
    stat2=mmc_drive_array[pdr->logical_unit_number].response[1];
    stat3=mmc_drive_array[pdr->logical_unit_number].response[2];
*/
#endif

    /*Format of R1 returned data is:

      bit    0      end bit
      bits  1:7     CRC7
      bits  8:32    card status
      bits 40:45    cmd index
      bit   46      transmission bit
      bit   47      start bit

      card status is broken down as follows:

      bits 0:7 reserved
      bit  8   RDY=1

      bits 9-12 contains binary values:

               0=idle
               1=ready
               2=ident
               3=standby
               4=transfer mode etc.  */
    /*Go toggle to xfer state*/

    card_state= (byte)(mmc_drive_array[pdr->logical_unit_number].response[2] >> 1);
    if ( (card_state != (byte) STANDBY_STATE) && (card_state != (byte) XFER_STATE))
    {
        ++rcount;
        if (rcount < 10000) goto retry;
        return(FALSE);
    }

    if (card_state == (byte) STANDBY_STATE)
    {
#if (DEBUG_MMC)
        rtfs_kern_puts((byte*)"in xfer: attempting to do select\n");
#endif
        if (!SetupCmdAndSend(pdr, (id_rca << 16), SELECT_DESELECT_CARD, 0, R1)) return(FALSE);
#if (DEBUG_MMC)
        rtfs_kern_puts((byte*)"in xfer: successfully did select\n");
#endif
    }

    return(TRUE);
}

BOOLEAN set_standby_state(DDRIVE *pdr)
{
dword id_rca;
byte card_state;

    id_rca=mmc_drive_array[pdr->logical_unit_number].rca;

    if (!SetupCmdAndSend(pdr, (id_rca << 16), SEND_STATUS, 0, R1)) return(FALSE);

    card_state= (byte)(mmc_drive_array[pdr->logical_unit_number].response[2] >> 1);

    /*Format of R1 returned data is:

      bit    0      end bit
      bits  1:7     CRC7
      bits  8:32    card status
      bits 40:45    cmd index
      bit   46      transmission bit
      bit   47      start bit

      card status is broken down as follows:

      bits 0:7 reserved
      bit  8   RDY=1

      bits 9-12 contains binary values:

               0=idle
               1=ready
               2=ident
               3=standby
               4=transfer mode etc.  */

    /*Go toggle to standby state*/

    if (card_state != STANDBY_STATE )
    {
        if (!SetupCmdAndSend(pdr, (id_rca << 16), SELECT_DESELECT_CARD, 0, R1)) return(FALSE);
    }
    return(TRUE);
}
/* Porting section
   ==================

Register access functions.. The following functions read and write mmc registers. All values
sent to and from the register set are 16 bit words. The functions are segregated in order
to allow adjustments to the code to support BIG Enmdian architecture, architectures
with non contiguous access schemes to the mmc card and architectures like some DSPs that
do not support 16 bit bus accesses.

Note: These functions are passed the register_file_address (dword) that is retrieved by
find_mmc_adaptor. They are the only functions that use this value. If it is more convenient
to use another approach such as hardwiring each register address then use that method

    word mmc_rd_rev_reg(dword register_file_address)
    word mmc_rd_status_reg(dword register_file_address)
    word mmc_rd_res_fifo_reg(dword register_file_address)
    word mmc_rd_data_fifo_reg(dword register_file_address)
    void mmc_wr_status_reg(dword register_file_address, word value)
    void mmc_wr_rev_reg(dword register_file_address, word value)
    void mmc_wr_clk_rate_reg(dword register_file_address, word value)
    void mmc_wr_response_to_reg(dword register_file_address, word value)
    void mmc_wr_read_to_reg(dword register_file_address, word value)
    void mmc_wr_data_cont_reg(dword register_file_address, word value)
    void mmc_wr_stop_clock_reg(dword register_file_address, word value)
    void mmc_wr_blk_len_reg(dword register_file_address, word value)
    void mmc_wr_nob_reg(dword register_file_address, word value)
    void mmc_wr_data_fifo_reg(dword register_file_address, word value)
    void mmc_wr_data_cmd_reg(dword register_file_address, word value)
    void mmc_wr_arg_hi_reg(dword register_file_address, word value)
    void mmc_wr_arg_lo_reg(dword register_file_address, word value)

Find Adapter fuction - This function is called to determine if an MMC device is present.
If an adapter is present it must return TRUE. If not it must return FALSE.

The routine must fill in the following fields in the provided drive structure:
register_file_address - The base address of the mmc register file. This value is only used by
the register access functions in the porting layer. If the functions don't use this value then
it does not need to be set.
interrupt_number - The interrupt number to use for mmc interrupts. If set to -1 the device
driver runs in polled mode, otherwise the interrupt number in register_file_address is used.

Note: - The driver does not support interrupts so please set interrupt_number to -1.

BOOLEAN find_mmc_adapter(DDRIVE *pdr)

Read the pci configuration space. This routine is called only by find_mmc_adapter() when using
the PCI adapter code. Implement it for your PCI bios implementation and environment if needed.

BOOLEAN read_PCI_config(word bus, word device, word func, PCIcfg *pcfg)

Interrupt management - The driver does not support interrupts but a prototype implementation is
included. If interrupt support is added the following routines must be enabled.

    mmc_isr - This is the interrupt handler. It should be modified to your environment.
    hook_mmc_interrupt - This routine must establish mmc_isr as the interrupt service routine.

   ==================
*/


#define STR_STP_CLK_REG         0x00
#define STATUS_REG              0x02
#define CLK_RATE_REG            0x04
#define ADAPTER_REV_REG         0x06
#define CMD_DAT_CONT_REG        0x0A
#define RESPONSE_TO_REG         0x0c
#define READ_TO_REG             0x0e
#define BLK_LEN_REG             0x10
#define NOB_REG                 0x12
#define INT_MASK_REG            0x1a
#define CMD_REG                 0x1C
#define ARG_HI_REG              0x1E
#define ARG_LO_REG              0x20
#define RES_FIFO                0x22
#define DATA_FIFO               0x26

word mmc_rd_rev_reg(dword register_file_address)
{
    return(read_mmc_word(register_file_address+ADAPTER_REV_REG));
}
word mmc_rd_status_reg(dword register_file_address)
{
    return(read_mmc_word(register_file_address+STATUS_REG));
}
word mmc_rd_res_fifo_reg(dword register_file_address)
{
    return(read_mmc_word(register_file_address+RES_FIFO));
}
word mmc_rd_data_fifo_reg(dword register_file_address)
{
    return(read_mmc_word(register_file_address+DATA_FIFO));
}
void mmc_wr_status_reg(dword register_file_address, word value)
{
    write_mmc_word(register_file_address+STATUS_REG, value);
}
void mmc_wr_rev_reg(dword register_file_address, word value)
{
    write_mmc_word(register_file_address+ADAPTER_REV_REG, value);
}
void mmc_wr_clk_rate_reg(dword register_file_address, word value)
{
    write_mmc_word(register_file_address + CLK_RATE_REG, value);
}
void mmc_wr_response_to_reg(dword register_file_address, word value)
{
    write_mmc_word(register_file_address + RESPONSE_TO_REG, value);
}
void mmc_wr_read_to_reg(dword register_file_address, word value)
{
    write_mmc_word(register_file_address+READ_TO_REG, value);
}
void mmc_wr_data_cont_reg(dword register_file_address, word value)
{
    write_mmc_word(register_file_address+CMD_DAT_CONT_REG, value);
}
void mmc_wr_stop_clock_reg(dword register_file_address, word value)
{
    write_mmc_word(register_file_address+STR_STP_CLK_REG, value);
}
void mmc_wr_blk_len_reg(dword register_file_address, word value)
{
    write_mmc_word(register_file_address+BLK_LEN_REG, value);
}
void mmc_wr_nob_reg(dword register_file_address, word value)
{
    write_mmc_word(register_file_address+NOB_REG, value);
}
void mmc_wr_data_fifo_reg(dword register_file_address, word value)
{
    write_mmc_word(register_file_address+DATA_FIFO, value);
}
void mmc_wr_data_cmd_reg(dword register_file_address, word value)
{
    write_mmc_word(register_file_address+CMD_REG, value);
}
void mmc_wr_arg_hi_reg(dword register_file_address, word value)
{
    write_mmc_word(register_file_address+ARG_HI_REG, value);
}
void mmc_wr_arg_lo_reg(dword register_file_address, word value)
{
    write_mmc_word(register_file_address+ARG_LO_REG, value);
}


#if (USE_PCI_ADAPTER)

/* This is an implementation of find_mmc_adapter() for use with a PCI adapter
   card for Multimedia Cards.  We use it for testing. */

#define MAX_DEVICE 32       /*max devices on PCI bus*/
#define MAX_FUNC   8        /*max functions per device*/

#define SD_DEVICE_ID            0x1100
#define SD_VENDOR_ID            0x15B7

/*PCI configuration register layouts*/

typedef struct PCIcfg
{
   word  vendorID ;
   word  deviceID ;
   word  command_reg ;
   word  status_reg ;
   byte  revisionID ;
   byte  progIF ;
   byte  subclass ;
   byte  classcode ;
   byte  cacheline_size ;
   byte  latency ;
   byte  header_type ;
   byte  BIST ;
   union
   {
      struct
      {
     dword base_address0 ;
     dword base_address1 ;
     dword base_address2 ;
     dword base_address3 ;
     dword base_address4 ;
     dword base_address5 ;
     dword CardBus_CIS ;
     word subsystem_vendorID ;
     word subsystem_deviceID ;
     dword expansion_ROM ;
     byte cap_ptr ;
     byte reserved1[3] ;
     dword reserved2[1] ;
     byte interrupt_line ;
     byte interrupt_pin ;
     byte min_grant ;
     byte max_latency ;
     dword device_specific[48] ;
      } nonbridge ;
      struct
      {
     dword base_address0 ;
     dword base_address1 ;
     byte primary_bus ;
     byte secondary_bus ;
     byte subordinate_bus ;
     byte secondary_latency ;
     byte IO_base_low ;
     byte IO_limit_low ;
     word secondary_status ;
     word memory_base_low ;
     word memory_limit_low ;
     word prefetch_base_low ;
     word prefetch_limit_low ;
     dword prefetch_base_high ;
     dword prefetch_limit_high ;
     word IO_base_high ;
     word IO_limit_high ;
     dword reserved2[1] ;
     dword expansion_ROM ;
     byte interrupt_line ;
     byte interrupt_pin ;
     word bridge_control ;
     dword device_specific[48] ;
      } bridge ;
   } pciBus;  /*union*/
} PCIcfg;

BOOLEAN read_PCI_config(word bus, word device, word func, PCIcfg *pcfg);

BOOLEAN find_mmc_adapter(DDRIVE *pdr)
{
dword board_base = 0;
int board_int = -1;
word device, func, bus, max_bus;
PCIcfg *pcfg;
PCIcfg mycfg;

    pcfg=&mycfg;
    max_bus=0xff;
    board_base=0;

    for (bus=0;bus<max_bus; bus++)
    {
        for(device=0;device < MAX_DEVICE; device++)
        {
            for(func=0;func<MAX_FUNC;func++)
            {
                /*go read the entire config reg area into our buffer*/
                /*note: could also replace this code with pcibios.asm stuff*/
                if (!read_PCI_config(bus, device, func, pcfg)) return(FALSE);

                if (pcfg && pcfg->vendorID == SD_VENDOR_ID &&
                     pcfg->deviceID == SD_DEVICE_ID)
                {
                    switch(pcfg->header_type & 0x7f)
                    {
                        case 0:
                            board_base=(word) (pcfg->pciBus.nonbridge.base_address2 & 0xfffe);
                            board_int=(int) (pcfg->pciBus.nonbridge.interrupt_line & 0xff);
                            break;

                        case 1:
                            board_base=(word) (pcfg->pciBus.bridge.base_address1 & 0xfffe);
                            board_int=(int) (pcfg->pciBus.bridge.interrupt_line & 0xff);
                            break;
                    }
                }       /*if SanDisk board*/


                /*Got it? If so, then all done*/
                if (board_base != 0)
                {
                    pdr->register_file_address=board_base;
                    pdr->interrupt_number=(int)board_int;
                    return (TRUE);
                }


            } /*functions*/

        }   /*devices*/

    }   /*buses*/


    return(FALSE);

}


#if (POLLED_RTTARGET)
#include <rttarget.h>
#include <rttbios.h>

BOOLEAN read_PCI_config(word bus, word device, word func, PCIcfg *pcfg)
{
int i;
dword * pcdata;
byte devfunc;

    devfunc=(byte) (((device << 3) & 0xF8) | (func & 0x07));

    for (i = 0, pcdata = (dword*)pcfg; i < sizeof(PCIcfg); i+=4, pcdata++)
    {
        if (RTT_BIOS_ReadConfigData((byte)bus, devfunc, i, 4, pcdata) != 0)
            return(FALSE);
    }

    return (TRUE);
}
#elif (POLLED_DOS)
#include <dos.h>        /*for regs x86 calls*/

/*software interrupt 0x1a PCI bios functions*/
#define PCI_FUNCTION_ID               0xB1
#define PCI_BIOS_PRESENT              0x01
#define PCI_FIND_DEVICE_ID            0x02
#define PCI_FIND_DEVICE_CLASS         0x03
#define PCI_READ_CONFIG_BYTE          0x08
#define PCI_READ_CONFIG_WORD          0x09
#define PCI_READ_CONFIG_DWORD         0x0A
#define PCI_WRITE_CONFIG_BYTE         0x0B
#define PCI_WRITE_CONFIG_WORD         0x0C
#define PCI_WRITE_CONFIG_DWORD        0x0D

BOOLEAN read_PCI_config(word bus, word device, word func, PCIcfg *pcfg)
{
word i;
word * pcdata;
    union REGS regs, outregs;

    pcdata=(word *) pcfg;
    regs.h.bh = (byte)bus;
    regs.h.bl = (byte)((device << 3) | (func & 0x07));
    regs.h.al = PCI_READ_CONFIG_WORD;
    regs.x.di = 0;                  /*start with register zero*/

/*for (i = 0; i < (sizeof(PCIcfg)/sizeof(dword)); i++)*/
    for (i = 0; i < 8 ; i++)
    {
        regs.h.ah = PCI_FUNCTION_ID;
        int86(0x1A, &regs, &outregs);

        if (outregs.x.cflag != 0) return (FALSE);
        *pcdata++ = (word)outregs.x.cx;

        regs.x.di += 2;                 /*next config reg*/
        regs.h.ah = PCI_FUNCTION_ID;    /*must reset this as ah is returned*/
        int86(0x1A, &regs, &outregs);

        if (outregs.x.cflag != 0) return (FALSE);
        *pcdata++ = (word)outregs.x.cx;
        regs.x.di += 2;                 /*next config reg*/
    }

    return (TRUE);

}
#else
/*  Dummy implementaion. */
BOOLEAN read_PCI_config(word bus, word device, word func, PCIcfg *pcfg)
{
    RTFS_ARGSUSED_INT((int)bus);
    RTFS_ARGSUSED_INT((int)device);
    RTFS_ARGSUSED_INT((int)func);
    RTFS_ARGSUSED_PVOID((void *)pcfg);
    return(FALSE);
}
#endif /* (POLLED_RTTARGET) */

#else /* (USE_PCI_ADAPTER) */

/* Just a dummy implementation; replace with whatever method your system
   uses to connect to the MMC.  It needs to set pdr->register_file_address and
   pdr->interrupt_number. */
BOOLEAN find_mmc_adapter(DDRIVE *pdr)
{
    RTFS_ARGSUSED_PVOID((void *)pdr);
    return(FALSE);
}

#endif /* (USE_PCI_ADAPTER) */

#if (SUPPORT_INTERRUPTS)
/* mmc_isr() - MMC ISR routine.
*
*   This routine is a portable interrupt service routine.
*
*/

void   mmc_isr(void)                           /*__fn__*/
{
    mmc_report_remove(0);
}

void hook_mmc_interrupt(int irq)
{
#if (POLLED_DOS)
int vector_no;
    /* map the interrupt number to a position in the int table */
    if ( irq > 7) vector_no = 0x70 + ( irq - 8);
    else vector_no =  irq + 8;
    _dos_setvect (vector_no, mmc_isr);
#elif (POLLED_RTTARGET)
        RTSetIntVector((BYTE)(RTIRQ0Vector+irq),
                       (RTIntHandler)mmc_isr);
    RTEnableIRQ(irq);                             /* tell PIC to enable this IRQ */
#elif (POLLED_WINDOWS)
#endif
}
#endif /* (SUPPORT_INTERRUPTS) */

#endif /* (INCLUDE_MMCCARD) */
