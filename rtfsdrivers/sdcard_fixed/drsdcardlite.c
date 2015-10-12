/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc 1987-2012
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* drsdcard.c - SD card disk driver for rtfs version 6.

Summary

 Description
    Rtfs device driver entry points for MMC/SD card driver.
*/

#include "rtfs.h"


#if (INCLUDE_SDCARD)
#define TRUE  1
#define FALSE 0

#define MAXSDCARDDRIVE 1            /* Do not change */


/* File system independent routines in sdmmc.c  */
typedef void (* INSERTCBFN)(int unit);
typedef void (* REMOVECBFN)(int unit);
extern void rtsdcard_driver_attach(INSERTCBFN pInsertFn, REMOVECBFN pReMoveFn);
extern int rtsdcard_device_media_parms(int unit_number, unsigned long *nSectors, unsigned long *BytesPerSector, int *isReadOnly);
extern int rtsdcard_device_read(int unit_number, unsigned long Sector, unsigned long nSectors, unsigned char *pData);
extern int rtsdcard_device_write(int unit_number, unsigned long Sector, unsigned long nSectors, unsigned char *pData);
extern int rtsdcard_device_open(int unit_number);



/* Local variables, defines and prototypes */
static int rtfs_sdcard_is_initialized;
static unsigned char sdcard_driveflags[MAXSDCARDDRIVE];
#define SDCARD_INSERTED 0x1
#define SDCARD_CHANGED  0x2
static void sdcard_insert_cb(int sdcard_unit);
static void sdcard_remove_cb(int sdcard_unit);
static int SDCARD_insert(int unit_number);

void SDCARDPollDeviceReady(void);

void TimerProcess()
{
}
/****************************************************************
*                       MOUNTING DISK
****************************************************************/
/* Call by Rtfs intialization to start an MMC/SD card driver on SDCARDDRIVELETTER (S:) */
void SDCARD_Mount(void)
{
    if (!rtfs_sdcard_is_initialized)
    {
        int i;
        for (i = 0; i < MAXSDCARDDRIVE; i++)
            sdcard_driveflags[i] = 0;
        /* Assign insert/remove callbacks and initialize the sdcard and SCDARD BSP drivers */
        rtsdcard_driver_attach(sdcard_insert_cb, sdcard_remove_cb);
        rtsdcard_device_open(0);
        rtfs_sdcard_is_initialized=1;
        /* Call SDCARDPollDeviceReady() once, this will mount the drive if rtsdcard_driver_attach()
           detected a card in the slot when it initialized */
        SDCARDPollDeviceReady();
    }
}

/* This function is called from rtfs_sys_callback() on entry to every API call to check for a status change.
   If an insert is detected Rtfs is called and passed buffering, and driver entery points
   If a remove is detected RTfs is alerted so it can invalidate the drive letter */
void SDCARDPollDeviceReady(void)
{
int sdcard_unit=0;
    /* If MMC/SD card is not opened - return */
    if (!rtfs_sdcard_is_initialized)
      return;
    if (!sdcard_driveflags[sdcard_unit] & SDCARD_CHANGED)
      return;
    sdcard_driveflags[sdcard_unit] &= ~SDCARD_CHANGED;

    if (sdcard_driveflags[sdcard_unit] & SDCARD_INSERTED)
    {
      // HEREHERE Change 2
      rtp_printf("Card insert detected\n");
    }
    else
    {
      rtp_printf("Card reoval detected\n");
    }

}


#define SD_MAX_COUNT 127
/* Perform reads and writes on the SD card
   The address of this function is passed to Rtfs when the device is inserted */
int SDCARD_blkmedia_io(void  *devhandle, void *pdrive, unsigned long sector, void  *buffer, unsigned long count, int reading)
{
int r;
unsigned long n_to_xfer;
unsigned char *_buffer;
 //   RTFS_ARGSUSED_PVOID((void *)devhandle);
 //   RTFS_ARGSUSED_PVOID((void *)pdrive);

    SDCARD_Mount();
    _buffer = (unsigned char *) buffer;
    do
    {
          if (count > SD_MAX_COUNT)
            n_to_xfer=SD_MAX_COUNT;
          else
            n_to_xfer=count;
          if (reading)
            r=rtsdcard_device_read(0, sector, n_to_xfer, _buffer);
          else
            r=rtsdcard_device_write(0, sector, n_to_xfer, _buffer);
          if (r<0)
              return -1;
          count-=n_to_xfer;
          if (count)
          {
            _buffer += n_to_xfer*512;
            sector += n_to_xfer;
          }
    } while (count);
    return 0;
}


/* The addresses of these callbacks are passed to the device independent SDCARD layer by the attach routine
   They cooperate with logic in SDCARDPollDeviceReady to implement hot-swap */
static void sdcard_insert_cb(int sdcard_unit)
{
    if ((sdcard_driveflags[sdcard_unit] & SDCARD_INSERTED) == 0)
        sdcard_driveflags[sdcard_unit] |= SDCARD_CHANGED;
    sdcard_driveflags[sdcard_unit] |= SDCARD_INSERTED;
}

static void sdcard_remove_cb(int sdcard_unit)
{
    if (sdcard_driveflags[sdcard_unit] & SDCARD_INSERTED)
        sdcard_driveflags[sdcard_unit] |= SDCARD_CHANGED;
    sdcard_driveflags[sdcard_unit] &= ~SDCARD_INSERTED;
}

#endif /* (INCLUDE_SDCARD) */
#if (0)
/* Test routines */
void wait_for_card_insert(void)
{
    while ((sdcard_driveflags[0] & SDCARD_INSERTED)==0)
    {
        rtp_printf("Now re-insert the card card\r");
    }
    rtp_printf("\n");
}
void wait_for_card_swap(void)
{
    while (sdcard_driveflags[0] & SDCARD_INSERTED)
    {
        rtp_printf("Now remove the card card\r");
    }
    wait_for_card_insert();
    while ((sdcard_driveflags[0] & SDCARD_INSERTED)==0)
    {
        rtp_printf("Now re-insert the card card\r");
    }

}


static void do_sd_iotest(DDRIVE *pdr)
{
byte *user_buffer;
unsigned long user_buffer_size,maxcount, count,sector;
unsigned long i,j;
unsigned long time_zero, elapsed_time;
unsigned long *pl;

    user_buffer = pc_claim_user_buffer(pdr, &user_buffer_size, 0); /* released at cleanup */
    if (!user_buffer) {ERTFS_ASSERT(rtfs_debug_zero())}

#define TEST_SECTOR             40000
#define TEST_SIZE               64
#define READ_LOOP_COUNT         32
#define WRITE_LOOP_COUNT        1
#define TEST_WRITE_PATTERN      1

    if (user_buffer_size > TEST_SIZE)
        maxcount = TEST_SIZE;
    else
        maxcount = user_buffer_size;
    sector = TEST_SECTOR;

    for (count = maxcount;count > 0; sector += count, count-- )
    {
#if (WRITE_LOOP_COUNT&&TEST_WRITE_PATTERN)
    pl=(unsigned long *)user_buffer;
    for (i = 0; i < (count*512)/4;i++)
        *pl++=0;
    if (!SDCARD_blkmedia_io((void  *)&rtfs_sdcard_is_initialized, (void *) pdr, sector, user_buffer, count, FALSE))
    {
        rtp_printf("Failed on initial write\n");
        goto ex_it;
    }
    if (SDCARD_blkmedia_io((void  *)&rtfs_sdcard_is_initialized, (void *) pdr, sector, user_buffer, count, TRUE))
    {
          pl=(unsigned long *)user_buffer;
          for (j = 0; j < (count*512)/4;j++,pl++)
          {
            if (*pl!=0)
            {
              rtp_printf("Initial compare to zero failed at %d\n",j);
              break;
            }
          }
    }
    else
    {
          rtp_printf("Failed on initial write\n");
        goto ex_it;
    }
    pl=(unsigned long *)user_buffer;
    for (i = 0; i < (count*512)/4;i++)
        *pl++=i;
#endif

#if (WRITE_LOOP_COUNT!=0)
    rtp_printf("Printf writing sector==%d bytes == %d\n", sector, (count * WRITE_LOOP_COUNT)*512);
    time_zero = rtfs_port_elapsed_zero();
    for (i = 0; i < WRITE_LOOP_COUNT;i++)
    {
        if (SDCARD_blkmedia_io((void  *)&rtfs_sdcard_is_initialized, (void *) pdr, sector, user_buffer, count, FALSE))
        {
                      ;
        }
        else
        {
            rtp_printf("Write Failure\n");
            goto ex_it;
        }
    }
    elapsed_time = (rtfs_port_elapsed_zero() - time_zero);
    rtp_printf("Sucess elapsed(msec)== %d, bytespsec==%d \n",elapsed_time,((count * WRITE_LOOP_COUNT)*512/elapsed_time)*1000);
#endif

    pl=(unsigned long *)user_buffer;
    for (i = 0; i < (count*512)/4;i++,pl++)
        *pl=0;
    rtp_printf("Printf reading sector==%d bytes == %d\n", sector, (count * READ_LOOP_COUNT)*512);
    time_zero = rtfs_port_elapsed_zero();

    for (i = 0; i < READ_LOOP_COUNT;i++)
    {
        if (SDCARD_blkmedia_io((void  *)&rtfs_sdcard_is_initialized, (void *) pdr, sector, user_buffer, count, TRUE))
        {
#if (1||TEST_WRITE_PATTERN)
             pl=(unsigned long *)user_buffer;
             for (j = 0; j < (count*512)/4;j++,pl++)
            {
                if (*pl!=j)
                {
                    rtp_printf("Read test compare failed at offset %d\n",  4*j);
                    break;
                }
            }
#endif
            ;
        }
        else
        {
            rtp_printf("Read Failure\n");
            goto ex_it;
        }
    }
    elapsed_time = (rtfs_port_elapsed_zero() - time_zero);
    rtp_printf("Sucess elapsed(msec)== %d, bytespsec==%d \n",elapsed_time,(((count * READ_LOOP_COUNT)*512)/elapsed_time)*1000);
    wait_for_card_swap();
    SDCARDPollDeviceReady();
    }
ex_it:
    while(1)
        rtp_printf("Kill me now\r");
}
#endif