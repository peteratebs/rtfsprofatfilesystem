/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* drramdsk.c - Ram disk device driver.

Summary

 Description
    Provides a configurable ram drive capability.

    To use this utility you must provide two constants.

    The constants are:

    NUM_RAMDISK_PAGES - Number of pages of memory to allocate
    RAMDISK_PAGE_SIZE - Page size in 512 bytes blocks. On INTEL this may not
                        exceed 128.

*/
#include "rtfs.h"

#if (INCLUDE_RAMDISK)

/* Define the size of your ram disk.
   PAGE_SIZE in 512 byte blocks times number of pages */
#define NUM_RAMDISK_PAGES     16  /* 16*2 512-byte blocks is a 16K ram disk */
#define RAMDISK_PAGE_SIZE     156 // 156 == 1 meg 2   /* use 768 for a 6 Megabyte ram disk */

/* Note: (RAMDISK_HEADS * RAMDISK_CYLINDERS * RAMDISK_SECPTRACK) must be <= (NUM_RAMDISK_PAGES * RAMDISK_PAGE_SIZE) */

#define RAMDISK_HEADS          1
#define RAMDISK_CYLINDERS      RAMDISK_PAGE_SIZE
#define RAMDISK_SECPTRACK      NUM_RAMDISK_PAGES

#if (RAMDISK_SECPTRACK > 63)
#error INVALID HCN Value
#endif
#if (RAMDISK_CYLINDERS > 1023)
#error INVALID HCN Value
#endif
#if (RAMDISK_HEADS > 255)
#error INVALID HCN Value
#endif

typedef struct block_alloc {
        byte    core[512];
        } BLOCK_ALLOC;

static BLOCK_ALLOC ram_disk_pages[NUM_RAMDISK_PAGES][RAMDISK_PAGE_SIZE];
static void _format_ramdisk(void);



/* This routine converts a block offset to a memory pointer to the 512 bytes
   that contain the block */

static byte  *ramdisk_block(word block)                                       /*__fn__*/
{
int page_number;
int block_number;
dword ltemp;
    /* Get the page number */
    ltemp = block / RAMDISK_PAGE_SIZE;
    page_number = (word) ltemp;
    /* Check. This should not happen */
    if (page_number >= NUM_RAMDISK_PAGES)
        return(0);
    /* Get the offset */
    block_number = block % RAMDISK_PAGE_SIZE;
    return((byte  *) &ram_disk_pages[page_number][block_number].core[0]);
}

/* BOOLEAN ramdisk_io(BLOCKT block, void *buffer, word count, BOOLEAN reading)
*
*   Perform io to and from the ramdisk.
*
*   If the reading flag is true copy data from the ramdisk (read).
*   else copy to the ramdisk. (write). called by pc_gblock and pc_pblock
*
*   This routine is called by pc_rdblock.
*
*/
BOOLEAN ramdisk_io(int driveno, dword block, void  *buffer, word count, BOOLEAN reading) /*__fn__*/
{
    byte  *p;
    int i;
    byte  *pbuffer;

    RTFS_ARGSUSED_INT(driveno);

    pbuffer = (byte  *)buffer;

    while (count)
    {
        p = ramdisk_block((word)block);

        if (!p)
            return(FALSE);

        if (reading)
        {
            for (i = 0; i < 512; i++)
                *pbuffer++ = *p++;
        }
        else
        {
            for (i = 0; i < 512; i++)
                *p++ = *pbuffer++;
        }
        count--;
        block++;
    }
    return(TRUE);
}

static void _format_ramdisk(void)
{
    byte  *p;
    dword block;
    int i;
    for (block = 0; block < (NUM_RAMDISK_PAGES * RAMDISK_PAGE_SIZE); block++)
    {
        p = ramdisk_block((word)block);
        for (i = 0 ; i < 512; i++)
            *p++ = 0;
    }
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

    rtfs_print_one_string((byte *)"Attaching a Ram Disk driver at drive M:", PRFLG_NL);

	pmedia_config_block->requested_driveid = (int) ('M'-'A');
	pmedia_config_block->requested_max_partitions =  1;
	pmedia_config_block->use_fixed_drive_id = 1;
	pmedia_config_block->device_sector_buffer_size_bytes = 512;
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


/* Call after Rtfs is intialized to start a ram disk driver on M: */
BOOLEAN BLK_DEV_RD_Ramdisk_Mount(BOOLEAN doformat)
{
    /* Set up mount parameters and call Rtfs to mount the device    */
struct rtfs_media_insert_args rtfs_insert_parms;

    if (doformat)
        _format_ramdisk();      /* Inititil device mount formats. Remounts do not so we can test partitoning etc */


    /* register with Rtfs File System */
    rtfs_insert_parms.devhandle = (void *) &ram_disk_pages[0][0]; /* Not used just a handle */
    rtfs_insert_parms.device_type = 999;	/* not used because private versions of configure and release, but must be non zero */
    rtfs_insert_parms.unit_number = 0;
    rtfs_insert_parms.media_size_sectors = (dword) (RAMDISK_PAGE_SIZE*NUM_RAMDISK_PAGES);
    rtfs_insert_parms.numheads = (dword) RAMDISK_HEADS;
    rtfs_insert_parms.secptrk  = (dword) RAMDISK_SECPTRACK;
    rtfs_insert_parms.numcyl   = (dword) RAMDISK_CYLINDERS;
    rtfs_insert_parms.sector_size_bytes =  (dword) 512;
    rtfs_insert_parms.eraseblock_size_sectors =   0;
    rtfs_insert_parms.write_protect    =          0;

    rtfs_insert_parms.device_io                = BLK_DEV_RD_blkmedia_io;
    rtfs_insert_parms.device_ioctl             = BLK_DEV_RD_blkmedia_ioctl;
    rtfs_insert_parms.device_erase             = 0;
    rtfs_insert_parms.device_configure_media    = BLK_DEV_RD_configure_device;
    rtfs_insert_parms.device_configure_volume   = BLK_DEV_RD_configure_volume;

    if (pc_rtfs_media_insert(&rtfs_insert_parms) < 0)
    	return(FALSE);
	else
    	return(TRUE);
}

static int BLK_DEV_RD_blkmedia_io(void  *devhandle, void *pdrive, dword sector, void  *buffer, dword count, BOOLEAN reading)
{
    RTFS_ARGSUSED_PVOID((void *)devhandle);
    RTFS_ARGSUSED_PVOID((void *)pdrive);
    return((int)ramdisk_io(0 /* driveno */, sector, buffer, (word) count, reading));
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
        	_format_ramdisk();
			break;
		case RTFS_IOCTL_INITCACHE:
        case RTFS_IOCTL_FLUSHCACHE:
			break;
        default:
            return(-1);
    }
    return(0);
}


#endif /* (INCLUDE_RAMDISK) */
