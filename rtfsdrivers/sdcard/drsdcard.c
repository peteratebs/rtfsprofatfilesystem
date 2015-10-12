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

/* Drive letter to assign to the SDCARD */
#define SDCARDDRIVELETTER 'S'
#define MAXSDCARDDRIVE 1            /* Do not change */


/* File system independent routines in sdmmc.c  */
typedef void (* INSERTCBFN)(int unit);
typedef void (* REMOVECBFN)(int unit);
extern void rtsdcard_driver_attach(INSERTCBFN pInsertFn, REMOVECBFN pReMoveFn);
extern int rtsdcard_device_media_parms(int unit_number, unsigned long *nSectors, unsigned long *BytesPerSector, int *isReadOnly);
extern int rtsdcard_device_read(int unit_number, unsigned long Sector, unsigned long nSectors, unsigned char *pData);
extern int rtsdcard_device_write(int unit_number, unsigned long Sector, unsigned long nSectors, unsigned char *pData);
extern int rtsdcard_device_open(int unit_number);



/* ======================================== */
/* Callback routines passed to Rtfs from SDCARD_insert when a device is inserted  */
static int SDCARD_blkmedia_io(void  *devhandle, void *pdrive, dword sector, void  *buffer, dword count, BOOLEAN reading);
static int SDCARD_blkmedia_ioctl(void  *handle_or_drive, void *pdrive, int opcode, int iArgs, void *vargs);
static int SDCARD_configure_device(struct rtfs_media_insert_args *pmedia_parms, struct rtfs_media_resource_reply *pmedia_config_block, int sector_buffer_required);
static int SDCARD_configure_volume(struct rtfs_volume_resource_request *prequest_block, struct rtfs_volume_resource_reply *pvolume_config_block);



/* ======================================== */
/* Buffering and policy instructions passed back to Rtfs by SDCARD_configure_device and SDCARD_configure_volume callbacks */
/* Single sector buffer for reading MBR, BSP etc. */
static byte _sectorbuffer[512];

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

/* Local variables, defines and prototypes */
static int rtfs_sdcard_is_initialized;
static unsigned char sdcard_driveflags[MAXSDCARDDRIVE];
#define SDCARD_INSERTED 0x1
#define SDCARD_REMOVED  0x2
#define SDCARD_CHANGED  0x4
static void sdcard_insert_cb(int sdcard_unit);
static void sdcard_remove_cb(int sdcard_unit);
static BOOLEAN SDCARD_insert(int unit_number);

static void SDCARDPollDeviceReady(void);

/****************************************************************
*                       MOUNTING DISK
****************************************************************/
/* Call by Rtfs intialization to start an MMC/SD card driver on SDCARDDRIVELETTER (S:) */

static RTFS_DEVI_POLL_REQUEST_VECTOR poll_device_vector_storage;
BOOLEAN BLK_DEV_SDCARD_Mount(void)
{
    if (!rtfs_sdcard_is_initialized)
    {
        int i;
        for (i = 0; i < MAXSDCARDDRIVE; i++)
            sdcard_driveflags[i] = 0;
        /* Assign insert/remove callbacks and initialize the sdcard and SCDARD BSP drivers */
        rtsdcard_driver_attach(sdcard_insert_cb, sdcard_remove_cb);
        rtfs_sdcard_is_initialized=1;
        /* register a callback to poll for media changes */
        pc_rtfs_register_poll_devices_ready_handler(&poll_device_vector_storage, SDCARDPollDeviceReady);
        /* Call SDCARDPollDeviceReady() once, this will mount the drive if rtsdcard_driver_attach()
           detected a card in the slot when it initialized */
        SDCARDPollDeviceReady();
    }
    return TRUE;
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
    if ((sdcard_driveflags[sdcard_unit] & SDCARD_CHANGED)==0)
      return;
    sdcard_driveflags[sdcard_unit] &= ~SDCARD_CHANGED;

    if (sdcard_driveflags[sdcard_unit] & SDCARD_REMOVED)
    {
        pc_rtfs_media_alert((void *) &rtfs_sdcard_is_initialized,RTFS_ALERT_EJECT,NULL);
        sdcard_driveflags[sdcard_unit] &= ~SDCARD_REMOVED;
    }
    if (sdcard_driveflags[sdcard_unit] & SDCARD_INSERTED)
    {
      if (rtsdcard_device_open(0)==0)    /* Initialize the card */
        SDCARD_insert(sdcard_unit);   /* Perform Rtfs insert */
      else
      {
            rtp_printf("\nrtsdcard_device_open  failed\n");
      }
    }
}


static void do_sd_iotest(DDRIVE *pdr);
#define DO_IO_TEST 0
#define SD_MAX_COUNT 127
/* Perform reads and writes on the SD card
   The address of this function is passed to Rtfs when the device is inserted */
static int SDCARD_blkmedia_io(void  *devhandle, void *pdrive, dword sector, void  *buffer, dword count, BOOLEAN reading)
{
int r;
dword n_to_xfer;
unsigned char *_buffer;
    RTFS_ARGSUSED_PVOID((void *)devhandle);
    RTFS_ARGSUSED_PVOID((void *)pdrive);
#if (DO_IO_TEST)
static int once=0;
if (!once)
{
    once=1;
    do_sd_iotest((DDRIVE *)pdrive);
}
#endif
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
              return FALSE;
          count-=n_to_xfer;
          if (count)
          {
            _buffer += n_to_xfer*512;
            sector += n_to_xfer;
          }
    } while (count);
    return TRUE;
}



/* Perform IOCTL functions on the SD card (none are needed)
   The address of this function is passed to Rtfs when the device is inserted */
static int  SDCARD_blkmedia_ioctl(void  *handle, void *pdrive, int opcode, int iArgs, void *vargs)
{
    RTFS_ARGSUSED_PVOID(handle);
    RTFS_ARGSUSED_PVOID(pdrive);
    RTFS_ARGSUSED_PVOID(vargs);
    RTFS_ARGSUSED_INT(iArgs);

    switch(opcode)
    {
        case RTFS_IOCTL_FORMAT:
            break;
        case RTFS_IOCTL_INITCACHE:
            break;
        case RTFS_IOCTL_FLUSHCACHE:
            break;
        default:
            return(-1);
    }
    return(0);
}

/* Called by SDCARDPollDeviceReady when an SDCARD is inserted. */
static BOOLEAN SDCARD_insert(int unit_number)
{
    /* Set up mount parameters and call Rtfs to mount the device    */
    struct rtfs_media_insert_args rtfs_insert_parms;
    dword totallba,secptrack, numheads, secpcyl, numcylinders, sectorsize;
    int   isReadOnly;

    /* If MMC/SD card presented - get the main parameters about the disk */
    if (rtsdcard_device_media_parms(unit_number, &totallba, &sectorsize, &isReadOnly)<0)
    {
        rtfs_print_one_string((byte *)"MMC/SD card is not found", PRFLG_NL);
        return(FALSE);
    }

    secptrack = 32; /* or 16 */
    numheads=2;
    if (totallba > 0xffff) numheads=4;
    if (totallba > 0x1ffff) numheads=8;
    if (totallba > 0x1fffff) numheads=16;
    if (totallba > 0x1ffffff) numheads=32;
    if (totallba > 0x1fffffff) numheads=64;

    secpcyl = numheads * secptrack;

    numcylinders = (word) (totallba / secpcyl);

    /* register with Rtfs File System */
    rtfs_insert_parms.devhandle = (void *) &rtfs_sdcard_is_initialized; /* Not used just a handle */
    rtfs_insert_parms.device_type = 999;    /* not used because private versions of configure and release, but must be non zero */
    rtfs_insert_parms.unit_number = 0;
    rtfs_insert_parms.media_size_sectors        = (dword) totallba;
    rtfs_insert_parms.numheads                  = (dword) numheads;
    rtfs_insert_parms.secptrk                   = (dword) secptrack;
    rtfs_insert_parms.numcyl                    = (dword) numcylinders;
    rtfs_insert_parms.sector_size_bytes         = (dword) sectorsize;
    rtfs_insert_parms.eraseblock_size_sectors   =  0;
    rtfs_insert_parms.write_protect             =  isReadOnly;

    rtfs_insert_parms.device_io                 = SDCARD_blkmedia_io;
    rtfs_insert_parms.device_ioctl              = SDCARD_blkmedia_ioctl;
    rtfs_insert_parms.device_erase              = 0;
    rtfs_insert_parms.device_configure_media    = SDCARD_configure_device;
    rtfs_insert_parms.device_configure_volume   = SDCARD_configure_volume;

    if (pc_rtfs_media_insert(&rtfs_insert_parms) < 0)
    {
        return(FALSE);
    }
    else
    {
        return(TRUE);
    }
}

/* Called by Rtfs once after SDCARD_insert returns . */
static int SDCARD_configure_device(struct rtfs_media_insert_args *pmedia_parms, struct rtfs_media_resource_reply *pmedia_config_block, int sector_buffer_required)
{
    RTFS_ARGSUSED_PVOID((void *)pmedia_parms);

    rtfs_print_one_string((byte *)"Attaching a MMC/SD card driver at drive S:", PRFLG_NL);

    pmedia_config_block->requested_driveid = (int) (SDCARDDRIVELETTER-'A');
    pmedia_config_block->requested_max_partitions =  1;
    pmedia_config_block->use_fixed_drive_id = 1;
    pmedia_config_block->device_sector_buffer_size_bytes = pmedia_parms->sector_size_bytes;
    pmedia_config_block->use_dynamic_allocation = 0;

    if (sector_buffer_required)
    {
        pmedia_config_block->device_sector_buffer_base = (byte *) &_sectorbuffer[0];
        pmedia_config_block->device_sector_buffer_data  = (void *) &_sectorbuffer[0];
    }
    /*    0  if successful
        -1 if unsupported device type
        -2 if out of resources
    */
    return(0);
}

/* Called by Rtfs to after SDCARD_configure_device returns. */
static int SDCARD_configure_volume(struct rtfs_volume_resource_request *prequest_block, struct rtfs_volume_resource_reply *pvolume_config_block)
{
    rtfs_print_one_string((byte *)"MMC/SD card: configure_volume", PRFLG_NL);

    pvolume_config_block->drive_operating_policy            = DEFAULT_OPERATING_POLICY;
    pvolume_config_block->n_sector_buffers                  = DEFAULT_NUM_SECTOR_BUFFERS;
    pvolume_config_block->n_fat_buffers                     = DEFAULT_NUM_FAT_BUFFERS;
    pvolume_config_block->fat_buffer_page_size_sectors      = DEFAULT_FATBUFFER_PAGE;
    pvolume_config_block->n_file_buffers                    = 0;
    pvolume_config_block->file_buffer_size_sectors          = 0;
    pvolume_config_block->blkbuff_memory                    = &_blkbuff_memory[0];
    pvolume_config_block->filebuff_memory                   = 0;
    pvolume_config_block->fatbuff_memory                    = &_fatbuff_memory[0];
    pvolume_config_block->sector_buffer_base                = 0;
    pvolume_config_block->file_buffer_base                  = 0;
    pvolume_config_block->fat_buffer_base                   = 0;
    pvolume_config_block->sector_buffer_memory              = (void *) &_sector_buffer_memory[0];
    pvolume_config_block->file_buffer_memory                = 0;
    pvolume_config_block->fat_buffer_memory                 = (void *) &_fat_buffer_memory[0];

    if (prequest_block->failsafe_available)
    {
        pvolume_config_block->fsrestore_buffer_size_sectors     = 0;
        pvolume_config_block->fsindex_buffer_size_sectors       = 0;
        pvolume_config_block->fsjournal_n_blockmaps             = 0;
        pvolume_config_block->fsfailsafe_context_memory         = 0;
        pvolume_config_block->fsjournal_blockmap_memory         = 0;
        pvolume_config_block->failsafe_buffer_base              = 0; /* Only used for dynamic allocation */
        pvolume_config_block->failsafe_buffer_memory            = 0;
        pvolume_config_block->failsafe_indexbuffer_base         = 0; /* Only used for dynamic allocation */
        pvolume_config_block->failsafe_indexbuffer_memory       = 0;
    }
    return(0);
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
        sdcard_driveflags[sdcard_unit] |= (SDCARD_CHANGED|SDCARD_REMOVED);
}

#endif /* (INCLUDE_SDCARD) */
#if (DO_IO_TEST)
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
dword user_buffer_size,maxcount, count,sector;
unsigned long i,j;
dword time_zero, elapsed_time;
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
