/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* drmmcsd.c - MMC/SD card disk driver.

Summary

 Description
    Provides a configurable MMC/SD drive capability.
*/

#include "rtfs.h"


#if (INCLUDE_SDCARD)

#include "rtfssdmmc.h"

/* Define the size of your ram disk.
   PAGE_SIZE in 512 byte blocks times number of pages */
#define NUM_MMCSDDISK       3  /* number supported disks */

/*
* IOCTL COMMANDS used from user layer to mmcsd driver
*/
#define MAX_LBA_AT_ONCE     2


#define MMCSD_DRV_GET_MMCSD_STATUS  10
#define MMCSD_DRV_GET_MMCSD_TYPE    11
#define MMCSD_DRV_SET_MMCSD_NUMBER  12
#define MMCSD_DRV_GET_MMCSD_INC     13
#define MMCSD_DRV_DATA_TRANSFER     14


static BOOLEAN driverInitialized;
static BOOLEAN gDiskWasMounted;

//typedef struct MMCSD_device
//{
//    int driveno;
//    int driverInitialized;
//    int gDiskWasMounted;
//    MMCSDParams_t Params;
//} tMMCSD_device;

// static tMMCSD_device gMMCSDDisk[NUM_MMCSDDISK];
static dword gMMCSDDisk;


/***************************************
*   Open MMC/SD node
***************************************/
static BOOLEAN OpenMMCSD(void)
{
    BOOLEAN ret_val;

    /* Open MMC/SD device */
    if(driverInitialized)
    	TRUE;

    ret_val = MMCSDLLDriverOpen();
    if(!ret_val)
    {
        rtfs_print_one_string((byte *)"Error in Opening the MMC/SD card node", PRFLG_NL);
    }

    driverInitialized = ret_val;

    return ret_val;
}

/***************************************
*   Close MMC/SD node
***************************************/
static void CloseMMCSD(void)
{
}

/***************************************
*   Check MMC/SD node is opened
***************************************/
int CheckMMCSD(void)
{
    /* If MMC/SD is opened */
    return(driverInitialized);
}

/***************************************
*   Get information
***************************************/
BOOLEAN MMCSDMSGetInc(MMCSDParams_t *Params)
{
BOOLEAN ret_val;

    ret_val = MMCSDLLDriverGetInc(Params);
    if(!ret_val)
    {
      rtfs_print_one_string((byte *)"Error in Calling MMCSDLLDriverGetInc", PRFLG_NL);
      return(FALSE);
    }
    rtfs_print_one_string((byte *)"MMC/SD sectorSize : ", PRFLG_NL);
    rtfs_print_long_1((dword)Params->SectorSize, PRFLG_NL);

    rtfs_print_one_string((byte *)"MMC/SD sectorNumber : ", PRFLG_NL);
    rtfs_print_long_1((dword)Params->SectorNumber, PRFLG_NL);

    return(TRUE);
}

/***************************************
*   GET Status
***************************************/
BOOLEAN GetMMCSDStat(void)
{
    BOOLEAN ret_val;
    dword  status;

    ret_val = MMCSDLLDriverGetStatus(&status);
    if(!ret_val)
    {
      rtfs_print_one_string((byte *)"Error in Calling MMCSDLLDriverGetStatus", PRFLG_NL);
      return(FALSE);
    }
    if(status == 0)
    {
        /*rtfs_print_one_string((byte *)"GetMMCSDStat : not found MMC/SD card", PRFLG_NL);*/
        return(FALSE);
    } else
    {
        /*rtfs_print_one_string((byte *)"GetMMCSDStat : found MMC/SD card", PRFLG_NL);*/
        return(TRUE);
    }
}


/* BOOLEAN mmcsd_io(BLOCKT block, void *buffer, word count, BOOLEAN reading)
*
*   Perform io to and from the ramdisk.
*
*   If the reading flag is true copy data from the MMC/SD card (read).
*   else copy to the MMC/SD card. (write). called by pc_gblock and pc_pblock
*
*   This routine is called by pc_rdblock.
*
*/
BOOLEAN mmcsd_io(int driveno, dword block, void  *buffer, word count, BOOLEAN reading) /*__fn__*/
{
    DDRIVE *pdr;
    byte  *p;
    word  i;
    byte  *pbuffer;
    dword curCnt;
    dword LBANum;
    dword LBASize;
    dword curBlock;
	BOOLEAN ret_val = FALSE;
    RTFS_ARGSUSED_INT(driveno);
    pdr = pc_drno_to_drive_struct(driveno);

    LBASize = pdr->pmedia_info->sector_size_bytes;

    pbuffer = (byte  *)buffer;
    curCnt = count;
    curBlock = block;
    i = 0;

    while (curCnt)
    {
        if(curCnt >= MAX_LBA_AT_ONCE)
        {
            LBANum = MAX_LBA_AT_ONCE;
        } else
        {
            LBANum = 1;
        }


        ret_val = MMCSDLLDriverIO(curBlock, (byte *)buffer, LBANum, LBASize, reading);
		if (!ret_val)
		{
        	if(reading)
        	{
                rtfs_print_one_string((byte *)"Error Reading", PRFLG_NL);
                return(FALSE);
            }
            else
            {
                rtfs_print_one_string((byte *)"Error Writing", PRFLG_NL);
                return(FALSE);
            }
        }

        curCnt-=LBANum;
        curBlock+=LBANum;
        i++;
        pbuffer += (LBASize*LBANum);
    }

    return(TRUE);
}

/* ======================================== */
static int BLK_DEV_RD_blkmedia_io(void  *devhandle, void *pdrive, dword sector, void  *buffer, dword count, BOOLEAN reading);
static int  BLK_DEV_RD_blkmedia_ioctl(void  *handle_or_drive, void *pdrive, int opcode, int iArgs, void *vargs);

/* Sector buffer provided by BLK_DEV_RD_Ramdisk_Mount */
static byte ramdisk_sectorbuffer[512];


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


static int BLK_DEV_RD_configure_device(struct rtfs_media_insert_args *pmedia_parms, struct rtfs_media_resource_reply *pmedia_config_block, int sector_buffer_required)
{
    RTFS_ARGSUSED_PVOID((void *)pmedia_parms);

    rtfs_print_one_string((byte *)"Attaching a MMC/SD card driver at drive S:", PRFLG_NL);

	pmedia_config_block->requested_driveid = (int) ('S'-'A');
	pmedia_config_block->requested_max_partitions =  1;
	pmedia_config_block->use_fixed_drive_id = 1;
	pmedia_config_block->device_sector_buffer_size_bytes = pmedia_parms->sector_size_bytes;
	pmedia_config_block->use_dynamic_allocation = 0;

	if (sector_buffer_required)
	{
		pmedia_config_block->device_sector_buffer_base = (byte *) &ramdisk_sectorbuffer[0];
		pmedia_config_block->device_sector_buffer_data  = (void *) &ramdisk_sectorbuffer[0];
	}
	/*	0  if successful
		-1 if unsupported device type
		-2 if out of resources
	*/
	return(0);
}

static int BLK_DEV_RD_configure_volume(struct rtfs_volume_resource_request *prequest_block, struct rtfs_volume_resource_reply *pvolume_config_block)
{
    rtfs_print_one_string((byte *)"MMC/SD card: configure_volume", PRFLG_NL);

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

/****************************************************************
*                       MOUNTING DISK
****************************************************************/
/* Call after Rtfs is intialized to start a MMC/SD card driver on S: */
BOOLEAN BLK_DEV_RD_MMCSDdisk_Mount(BOOLEAN doformat)
{
    /* Set up mount parameters and call Rtfs to mount the device    */
    struct rtfs_media_insert_args rtfs_insert_parms;
    MMCSDParams_t Params;
    dword totallba;
    dword secptrack;
    dword numheads;
    dword secpcyl;
    dword numcylinders;
    dword sectorsize;

    /* Open /dev/mmc0 and check MMC/SD card presents */
    if(OpenMMCSD() == -1) return(FALSE);

    /* If MMC/SD card presented - get the main parameters about the disk */
    if(!GetMMCSDStat())
    {
        rtfs_print_one_string((byte *)"MMC/SD card is not found", PRFLG_NL);
        return(FALSE);
    }

    /* Define number of heads, cylinders and tracks */
    if(!MMCSDMSGetInc(&Params)) return(FALSE);

    sectorsize = Params.SectorSize;
    totallba = Params.SectorNumber;
    secptrack = 32; /* or 16 */

    numheads=2;
    if (totallba > 0xffff) numheads=4;
    if (totallba > 0x1ffff) numheads=8;
    if (totallba > 0x1fffff) numheads=16;
    if (totallba > 0x1ffffff) numheads=32;
    if (totallba > 0x1fffffff) numheads=64;

    secpcyl = numheads * secptrack;

    numcylinders = (word) (totallba / secpcyl);

    /*printf("sectorsize=%d totallba=%d numheads=%d secpcyl=%d numcylinders=%d\n",sectorsize,totallba,numheads,secpcyl,numcylinders);*/
    /* register with Rtfs File System */
    rtfs_insert_parms.devhandle = (void *) &gMMCSDDisk; /* Not used just a handle */
    rtfs_insert_parms.device_type = 999;	/* not used because private versions of configure and release, but must be non zero */
    rtfs_insert_parms.unit_number = 0;
    rtfs_insert_parms.media_size_sectors = (dword) totallba;
    rtfs_insert_parms.numheads = (dword) numheads;
    rtfs_insert_parms.secptrk  = (dword) secptrack;
    rtfs_insert_parms.numcyl   = (dword) numcylinders;
    rtfs_insert_parms.sector_size_bytes =  (dword) sectorsize;
    rtfs_insert_parms.eraseblock_size_sectors =   0;
    rtfs_insert_parms.write_protect    =          0;

    rtfs_insert_parms.device_io                = BLK_DEV_RD_blkmedia_io;
    rtfs_insert_parms.device_ioctl             = BLK_DEV_RD_blkmedia_ioctl;
    rtfs_insert_parms.device_erase             = 0;
    rtfs_insert_parms.device_configure_media    = BLK_DEV_RD_configure_device;
    rtfs_insert_parms.device_configure_volume   = BLK_DEV_RD_configure_volume;

    if (pc_rtfs_media_insert(&rtfs_insert_parms) < 0)
    {
        gDiskWasMounted = FALSE;
        return(FALSE);
    }
	else
    {
        gDiskWasMounted = TRUE;
        return(TRUE);
    }
}

static int BLK_DEV_RD_blkmedia_io(void  *devhandle, void *pdrive, dword sector, void  *buffer, dword count, BOOLEAN reading)
{
    RTFS_ARGSUSED_PVOID((void *)devhandle);
    RTFS_ARGSUSED_PVOID((void *)pdrive);
    return((int)mmcsd_io(((DDRIVE*)pdrive)->driveno, sector, buffer, (word) count, reading));
}


static int  BLK_DEV_RD_blkmedia_ioctl(void  *handle, void *pdrive, int opcode, int iArgs, void *vargs)
{
    RTFS_ARGSUSED_PVOID(handle);
    RTFS_ARGSUSED_PVOID(pdrive);
    RTFS_ARGSUSED_PVOID(vargs);
    RTFS_ARGSUSED_INT(iArgs);

    switch(opcode)
    {
        case RTFS_IOCTL_FORMAT:
            rtfs_print_one_string((byte *)"MMC/SD card: RTFS_IOCTL_FORMAT", PRFLG_NL);
			break;
		case RTFS_IOCTL_INITCACHE:
            rtfs_print_one_string((byte *)"MMC/SD card: RTFS_IOCTL_INITCACHE", PRFLG_NL);
            break;
        case RTFS_IOCTL_FLUSHCACHE:
            rtfs_print_one_string((byte *)"MMC/SD card: RTFS_IOCTL_FLUSHCACHE", PRFLG_NL);
			break;
        default:
            return(-1);
    }
    return(0);
}

/*
* CALLBACK function is called from rtfs_sys_callback()
*/
void MMCSDPollDeviceReady(void)
{
    /* If MMC/SD card is not opened - return */
    if(!CheckMMCSD()) return;

    /* If MMC/SD card presented - get the main parameters about the disk */
    if(!GetMMCSDStat())
    {
        if(gDiskWasMounted)
        {
            /* Umount disk from here */
            pc_rtfs_media_alert((void *) &gMMCSDDisk,RTFS_ALERT_EJECT,NULL);
            gDiskWasMounted = FALSE;
        }
        return;
    }
    else
    {
        if(!gDiskWasMounted)
        {
            /* Mount new disk from here */
            if(!BLK_DEV_RD_MMCSDdisk_Mount(FALSE))
            {
                printf(" Mounting fail\n");
            } else
            {
                printf(" Mount new device\n");
            }
        }
        return;
    }
}

// ... PVO These are top level stub functions called by Rtfs driver layer -
// I've diocumented what I think the proper functioning should be.
// We also need to study the linux driver to see how these calls are dispatched.
/* MMCSDLLDriverOpen(void) Probably should call SDAttach(). Current calling order may only work for non removable. (okay for testing)
   may be used wrong here, may need to move to polldeviceready for removable support). */
BOOLEAN MMCSDLLDriverOpen(void){ return(FALSE);}
/* MMCSDLLDriverGetInc Needs to access csd->capacity initialized by calls made from SDAttach() */
BOOLEAN  MMCSDLLDriverGetInc(MMCSDParams_t *Params){ return(FALSE);}  /* Should use csd->capacity initialized by SDAttach() (init())*/
/* MMCSDLLDriverGetStatus probably needs to call SDDetect(struct mmc_host *host) ?? */
BOOLEAN  MMCSDLLDriverGetStatus(dword *pstatus){ return(FALSE);}
/* MMCSDLLDriverIO - Probably needs to call MMCSDReadWriteData(host->card, ext_buf, 0, 0); */
BOOLEAN  MMCSDLLDriverIO(dword curBlock, byte *buffer, dword sectornum, dword numsectors, BOOLEAN reading){ return(FALSE);}


// ... PVO functions called by the driver but not available in windows.
// ffs and fls .. These we know about and must implement
int ffs(dword a) {return 0;};		/*  find first bit set The ffs() function returns the position of the first (least significant) bit set in the word i. The least significant bit is position 1 and the most significant position e.g. 32 or 64. */
int fls(dword a) {return 0;};		 /* find last bit set */


/* wait_for_completion - This is a rendezvous, to the complementery "complete(&thread_complete)" call.
    The complete call looks to be made in MMCSDWaitDonewhich is passed as a completion handler .
	!!! search for mrq->done = MMCSDWaitDone;
*/
void wait_for_completion(int *x) {};


/* MCIRequest - Low level routine that kicks off a SD requests and copmpletes the opreatioon.
MCIRequest is called by MMCSDStartRequest after the nested mmc_request struture is set up.
.. mmc_request 	-  includes handler links and parameter values for start, stop, and data, wakeup
.. mci_data Looks like mci_data has the byte count and address of bytes being transfered (not always used)
*/
void MCIRequest(struct mmc_host *mmc, struct mmc_request *mrq, struct MCI_data *mci_data){}


/* MMCSDClaimHost and MMCSDReleaseHost - Not sure if these claim and release the bus or if they are linux things not to worry about. */
int MMCSDClaimHost(struct mmc_host *host){return 0;};
void MMCSDReleaseHost(struct mmc_host *host){};

/* Note sure: struct mmc_ios has clock and power fields, probably a wrapper around other calls */
void MCISetCLKPWR(struct mmc_host *host, struct mmc_ios *ios){};




struct mmc_host *mmc_glob;
int gCardInserted;

// ... PVO end comments




#endif /* (INCLUDE_SDCARD) */
