/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1993-2005
* All rights reserved.
* This code may not be redistributed in sourceor linkable object form
* without the consent of its author.
*/
/* drwindev.c - Access windows XP block devices directly

Summary

 Description
    Provides a device driver that reads and writes data to devices
    mounted under Windows XP such as the system's hard disk or removable
    devices sdcard, compact flash, a usb drive or floppy disk.





*/

#define _LARGEFILE64_SOURCE
#include "rtfs.h"

#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <linux/unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>


#if (INCLUDE_WINDEV)

int linux_fd;
#define ddword unsigned long long
static void PollDeviceReady(void);
static int calculate_hcn(PDEV_GEOMETRY pgeometry);

// these defines will probably need to be changed
#define USE_DYNAMIC_ALLOCATION 1

#define DEFAULT_OPERATING_POLICY    				0
#define DEFAULT_NUM_SECTOR_BUFFERS 					10
#define DEFAULT_NUM_FAT_BUFFERS     				2
#define DEFAULT_FATBUFFER_PAGESIZE_SECTORS  		8

#define DEFAULT_NUM_FILE_BUFFERS    				0
#define DEFAULT_FILE_BUFFER_SIZE_SECTORS 			0

#define DEFAULT_FAILSAFE_RESTORE_BUFFERSIZE_SECTORS 64
#define DEFAULT_NUM_FAILSAFE_BLOCKMAPS     			1024
#define DEFAULT_FAILSAFE_INDEX_BUFFERSIZE_SECTORS	128

static byte windevdisk_sectorbuffer[512];

#if(!USE_DYNAMIC_ALLOCATION)

/* === pparms->blkbuff_memory one for each sector buffer */
static BLKBUFF _blkbuff_memory[DEFAULT_NUM_SECTOR_BUFFERS];
static byte _sector_buffer_memory[DEFAULT_NUM_SECTOR_BUFFERS*512];

/* === pparms->fatbuff_memory one for each FAT page */
static FATBUFF _fatbuff_memory[DEFAULT_NUM_FAT_BUFFERS];
static byte _fat_buffer_memory[DEFAULT_NUM_FAT_BUFFERS*512];
static byte windevdisk_sectorbuffer



#if (INCLUDE_FAILSAFE_CODE)
static FAILSAFECONTEXT fs_context;
static byte fs_buffer[DEFAULT_FAILSAFE_RESTORE_BUFFERSIZE_SECTORS*512];	/*(512(sector_size) * 64(DEFAULT_FAILSAFE_RESTORE_BUFFERSIZE_SECTORS)) */
static byte fs_index_buffer[DEFAULT_FAILSAFE_INDEX_BUFFERSIZE_SECTORS*512];	/*(512(sector_size) * 64(DEFAULT_FAILSAFE_RESTORE_BUFFERSIZE_SECTORS)) */
#endif

#endif


/* IO routine installed by BLK_DEV_hostdisk_Mount */
static BOOLEAN lindev_io(int driveno, dword block, void  *buffer, word count, BOOLEAN reading);
static BOOLEAN win_dev_seek(int logical_unit_number, dword block);
static BOOLEAN win_dev_write(int logical_unit_number, void  *buffer, word count);
static BOOLEAN win_dev_read(int logical_unit_number, void  *buffer, word count);
static int calculate_hcn(PDEV_GEOMETRY pgeometry);

static int BLK_DEV_VIRT_blkmedia_io(void  *devhandle, void *pdrive, dword sector, void  *buffer, dword count, BOOLEAN reading)
{
    RTFS_ARGSUSED_PVOID(devhandle);
    RTFS_ARGSUSED_PVOID(pdrive);
	return(lindev_io(((DDRIVE*)pdrive)->driveno, sector, buffer, (word)count, reading));
}
static BOOLEAN lindev_io(int driveno, dword block, void  *buffer, word count, BOOLEAN reading) /*__fn__*/
{
    if (!win_dev_seek(0, block))
    {
        ERTFS_ASSERT(rtfs_debug_zero())
        return(FALSE);
    }
    if (reading)
        return(win_dev_read(0, buffer, count));
    else
        return(win_dev_write(0, buffer, count));
}

static int  BLK_DEV_VIRT_blkmedia_ioctl(void  *devhandle, void *pdrive, int opcode, int iArgs, void *vargs)
{

    RTFS_ARGSUSED_INT(iArgs);
    RTFS_ARGSUSED_PVOID(vargs);
    switch(opcode)
    {
        case RTFS_IOCTL_FORMAT:
			break;
		case RTFS_IOCTL_INITCACHE:
		case RTFS_IOCTL_FLUSHCACHE:
			break;
    }
    return(0);

}

static int BLK_DEV_VIRT_device_configure_media(struct rtfs_media_insert_args *pmedia_parms, struct rtfs_media_resource_reply *pmedia_config_block, int sector_buffer_required)
{

	pmedia_config_block->requested_driveid = (int) ('P'-'A');
	pmedia_config_block->requested_max_partitions =  2;
	pmedia_config_block->use_fixed_drive_id = 1;
    /* June 2013 - this was incorrect */
	/* pmedia_config_block->device_sector_buffer_size_bytes = 64; */

	if (sector_buffer_required)
        pmedia_config_block->device_sector_buffer_size_bytes = 64*512;
    /* June 2013 - this was incorrect */
		/* pmedia_config_block->device_sector_buffer_size_bytes = pmedia_parms->sector_size_bytes * pmedia_parms->eraseblock_size_sectors; */
#if(USE_DYNAMIC_ALLOCATION == 0)
#error - Fix me..
#else
	pmedia_config_block->use_dynamic_allocation = 1;
#endif
    /*	0  if successful
		-1 if unsupported device type
		-2 if out of resources
	*/
	return(0);
}
#if (INCLUDE_FAILSAFE_CODE)
/* In evaluation mode this flag instructs host disk and host dev device drivers whether to
   autoenable falisafe */
extern BOOLEAN auto_failsafe_mode;
#endif
static int BLK_DEV_VIRT_device_configure_volume(struct rtfs_volume_resource_request *prequest_block, struct rtfs_volume_resource_reply *pvolume_config_block)
{

#if (INCLUDE_FAILSAFE_CODE)
	if (!auto_failsafe_mode)
		pvolume_config_block->drive_operating_policy 			=DRVPOL_DISABLE_AUTOFAILSAFE;
	else
#endif
		pvolume_config_block->drive_operating_policy 			= DEFAULT_OPERATING_POLICY;
    pvolume_config_block->n_sector_buffers 					= DEFAULT_NUM_SECTOR_BUFFERS;
    pvolume_config_block->n_fat_buffers    					= DEFAULT_NUM_FAT_BUFFERS;
    pvolume_config_block->fat_buffer_page_size_sectors  	= DEFAULT_FATBUFFER_PAGESIZE_SECTORS;
    pvolume_config_block->n_file_buffers 					= DEFAULT_NUM_FILE_BUFFERS;
    pvolume_config_block->file_buffer_size_sectors 			= DEFAULT_FILE_BUFFER_SIZE_SECTORS;
    if (prequest_block->failsafe_available)
    {
		pvolume_config_block->fsrestore_buffer_size_sectors = DEFAULT_FAILSAFE_RESTORE_BUFFERSIZE_SECTORS;
		pvolume_config_block->fsindex_buffer_size_sectors 	= DEFAULT_FAILSAFE_INDEX_BUFFERSIZE_SECTORS;
    	pvolume_config_block->fsjournal_n_blockmaps 		= DEFAULT_NUM_FAILSAFE_BLOCKMAPS;
#if (INCLUDE_FAILSAFE_CODE)
#if(USE_DYNAMIC_ALLOCATION == 0)
    	pvolume_config_block->fsfailsafe_context_memory     = &fs_context;
		pvolume_config_block->failsafe_buffer_memory        = &fs_buffer[0];
		pvolume_config_block->failsafe_indexbuffer_memory   = &fs_index_buffer[0];
#endif
#endif
	}
	/* Tell Rtfs to dynamically allocate buffers */
#if(USE_DYNAMIC_ALLOCATION)
		pvolume_config_block->use_dynamic_allocation = 1;
#else
		pvolume_config_block->blkbuff_memory 				= &_blkbuff_memory[0];
		pvolume_config_block->fatbuff_memory 				= &_fatbuff_memory[0];
		pvolume_config_block->sector_buffer_memory 			= (void *) &_sector_buffer_memory[0];
		pvolume_config_block->fat_buffer_memory 			= (void *) &_fat_buffer_memory[0];
#endif

	return(0);
}



/**
*	This is an initialization routine for the windows driver for Rtfs.
*/
static RTFS_DEVI_POLL_REQUEST_VECTOR poll_device_vector_storage;
static BOOLEAN win_dev_open(void);

BOOLEAN BLK_DEV_windev_Mount(void)
{
	printf("Mounting RAW DEVICE DRIVER on P:\n\n");
	return win_dev_open();
}



extern char *linux_device_name;

static void insert_linux_device_to_rtfs(void);

static BOOLEAN win_dev_open(void)
{
  printf ("Opening %s\n", linux_device_name);
  linux_fd = open((char*)linux_device_name, O_RDWR|O_LARGEFILE, S_IREAD | S_IWRITE);
  printf ("Open returned %d\n", linux_fd);
  if (linux_fd>=0)
  {
    insert_linux_device_to_rtfs();
    return(TRUE);
  }
  else
  {
    printf ("Errno == %d\n", errno);
    perror("open failed with");
  }
  return(FALSE);
}
/**
*	This function "inserts" a windows device into the rtfs system. If this is not called the device may be
*	inserted but RTFS will not be aware of it.
*/
static void insert_linux_device_to_rtfs(void){
	/* Set up mount parameters and call Rtfs to mount the device    */
extern int show_block_reads;
DEV_GEOMETRY gc;

	struct rtfs_media_insert_args rtfs_insert_parms;
	/* register with Rtfs File System */
	rtfs_insert_parms.devhandle = (void*)linux_fd; /* Not used just a handle */
	rtfs_insert_parms.device_type = 999;	/* not used because private versions of configure and release, but must be non zero */
	rtfs_insert_parms.unit_number = 0;

    rtfs_memset(&gc, 0, sizeof(gc));
    if (!calculate_hcn(&gc))
    {
        ERTFS_ASSERT(rtfs_debug_zero())
	    return;
    }

	rtfs_insert_parms.numheads = gc.dev_geometry_heads;
	rtfs_insert_parms.secptrk  = gc.dev_geometry_secptrack;
	rtfs_insert_parms.numcyl   = gc.dev_geometry_cylinders;
	rtfs_insert_parms.media_size_sectors = gc.dev_geometry_lbas;

	rtfs_insert_parms.sector_size_bytes =  (dword) 512;
	rtfs_insert_parms.eraseblock_size_sectors =   0;
	rtfs_insert_parms.write_protect    =          0;

	rtfs_insert_parms.device_io                = BLK_DEV_VIRT_blkmedia_io;
	rtfs_insert_parms.device_ioctl             = BLK_DEV_VIRT_blkmedia_ioctl;
	rtfs_insert_parms.device_erase             = 0;
	rtfs_insert_parms.device_configure_media    = BLK_DEV_VIRT_device_configure_media;
	rtfs_insert_parms.device_configure_volume   = BLK_DEV_VIRT_device_configure_volume;

    printf("Insert linux mount point\n");
     pc_rtfs_media_insert(&rtfs_insert_parms);
}





// _syscall5(int,  _llseek, uint, fd, ulong, hi, ulong, lo, loff_t *, res, uint, wh)
/*
*
*   Perform io to and from the win dev disk.
*
*   If the reading flag is true copy data from the hostdisk (read).
*   else copy to the hostdisk. (write).
*
*/






static BOOLEAN win_dev_seek(int logical_unit_number, dword block)
{
dword hi, lo;
ddword lhi, llo, result,llbytes;

   llbytes = (ddword) block;
   llbytes *= 512;

   lhi = llbytes >> 32;
   llo = llbytes & 0xffffffff;
   lo = (dword) llo;
   hi = (dword) lhi;

   if (lseek64(linux_fd, llbytes, SEEK_SET)!= llbytes)
    return(FALSE);

    return(TRUE);
 }

static BOOLEAN win_dev_write(int logical_unit_number, void  *buffer, word nblocks)
{
dword nbytes, nwritten;
        nbytes = (dword)nblocks;
        nbytes *= 512;

      if ((nwritten = write(linux_fd,buffer,nbytes)) != nbytes)
			{
				return(FALSE);
			}
      else
        return(TRUE);
}


static BOOLEAN win_dev_read(int logical_unit_number, void  *buffer, word nblocks)
{
dword nbytes, nread;
        nbytes = (dword)nblocks;
        nbytes *= 512;

      if ((nread = read(linux_fd,buffer,nbytes)) != nbytes)
			{
				return(FALSE);
			}
      else
        return(TRUE);
}

static int calculate_hcn(PDEV_GEOMETRY pgeometry)
{
long cylinders;  /*- Must be < 1024 */
long heads;      /*- Must be < 256 */
long secptrack;  /*- Must be < 64 */
long residual_h;
long residual_s;
dword n_blocks;
long long llblocks;

    printf ("Sizing \n");

    llblocks = lseek64(linux_fd, 0, SEEK_END);
    printf ("Sizing bites == %d\n", llblocks);

    if (llblocks < 0)
      return(0);

    llblocks >>= 9; /* divide by 512 */

    n_blocks = (dword) llblocks;
    printf ("Sizing blocks == %d\n", n_blocks);

    pgeometry->dev_geometry_lbas = n_blocks;
    secptrack = 1;
    while (n_blocks/secptrack > (1023L*255L))
        secptrack += 1;
    residual_h = (n_blocks+secptrack-1)/secptrack;
    heads = 1;
    while (residual_h/heads > 1023L)
        heads += 1;
    residual_s = (residual_h+heads-1)/heads;
    cylinders = residual_s;
    pgeometry->dev_geometry_cylinders = (dword) cylinders;
    pgeometry->dev_geometry_heads = (int) heads;
    pgeometry->dev_geometry_secptrack = (int) secptrack;
    return(1);

}
#endif /* (INCLUDE_WINDEV) */
