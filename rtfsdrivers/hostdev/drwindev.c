/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1993-2005
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* drwindev.c - Access windows XP block devices directly

Summary

 Description
    Provides a device driver that reads and writes data to devices
    mounted under Windows XP such as the system's hard disk or removable
    devices sdcard, compact flash, a usb drive or floppy disk.

*/

// Use this to force exact match on disk size for format testing otherwise the vaules returned from Windows are used.
//  Required for format testing because windows does not return exact correct values
// #define FORCE_LBAVALUE  (0x77B0000 + 0x8000)  // 64 GB SDXC card
// #define FORCE_LBAVALUE  (0x58F8000)  // 48 GB SDXC card
// #define FORCE_LBAVALUE  (0x77B0000 + 0x8000)  // 64 GB SDXC card
// #define FORCE_LBAVALUE     (0xED4000) /* 8 gig */
// #define FORCE_LBAVALUE    (0xED4000/2) /* 4 gig */
// #define FORCE_LBAVALUE    (0xED4000/4) /* 2 gig */

#ifdef _MSC_VER
#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include "rtfs.h"
/* windev - Device driver that acceses XP devices directly */
#include <stdio.h>
#include <io.h>
#include <fcntl.h>
#include <sys\types.h>
#include <stdlib.h>
#include <string.h>

#else
#include "rtfs.h"
#define HOST_INVALID_HANDLE_VALUE -1
#define HOST_HANDLE int

#endif

#if (INCLUDE_WINDEV)

#define HOST_INVALID_HANDLE_VALUE INVALID_HANDLE_VALUE
#define HOST_HANDLE HANDLE


// interval to check media presence
#define WINDOWS_WINDEV_SPRINTF sprintf
#define MAX_WINDEV_DEVICES 10

//if you do not want non-removable media to be accessible for safety reasons set this to 1
#define REMOVABLE_MEDIA_ONLY 0

int num_windev_devices = -1;
struct windev_device {
    int device_number;  // windev_device_names[device_number] is device name
    HOST_HANDLE hDevice;
    BOOLEAN media_installed;
    int num_partitions;
    char *partition_names[26];
    DEV_GEOMETRY gc; /* RTFS Geometry structure */
    BOOLEAN is_removable;
    dword   bytes_per_sector;
	unsigned long long  mediasize;
    int driveno; // which drive we're associated with
    int logical_unit_number;    // assign logical uint number (index in map_lun_to_windev_devices[] */
};
struct windev_device windev_devices[MAX_WINDEV_DEVICES];
struct windev_device *currentDevice;
struct windev_device *map_lun_to_windev_devices[MAX_WINDEV_DEVICES];


static int BLK_DEV_VIRT_blkmedia_io(void  *devhandle, void *pdrive, dword sector, void  *buffer, dword count, BOOLEAN reading);
static int BLK_DEV_VIRT_device_configure_volume(struct rtfs_volume_resource_request *prequest_block, struct rtfs_volume_resource_reply *pvolume_config_block);
static int  BLK_DEV_VIRT_blkmedia_ioctl(void  *devhandle, void *pdrive, int opcode, int iArgs, void *vargs);
static int BLK_DEV_VIRT_device_configure_media(struct rtfs_media_insert_args *pmedia_parms, struct rtfs_media_resource_reply *pmedia_config_block, int sector_buffer_required);
static void insert_win_device_to_rtfs(struct windev_device *pdevice);

char print_buffer[128];

/*
*
*   Perform io to and from the win dev disk.
*
*   If the reading flag is true copy data from the hostdisk (read).
*   else copy to the hostdisk. (write).
*
*/

// public function prototypes
BOOLEAN windev_io(int driveno, dword block, void  *buffer, word count, BOOLEAN reading);
int windev_perform_device_ioctl(int driveno, int opcode, void * pargs);

// private function prototypes
static BOOLEAN win_dev_seek(int logical_unit_number, dword block);
static BOOLEAN win_dev_write(int logical_unit_number, void  *buffer, word count);
static BOOLEAN win_dev_read(int logical_unit_number, void  *buffer, word count);
//static BOOLEAN win_dev_check_change(int logical_unit_number);
static BOOLEAN win_dev_enum_devices(void);
static HOST_HANDLE  map_win_logical_unit(int logical_unit_number);
static int win_select_device_for_lun(int logical_unit_number);
static BOOLEAN win_dev_get_layout(HOST_HANDLE hDevice,int device_number,struct windev_device *pdevice_info);

static int media_is_writable=0;
static int driver_is_initialized=0;

int show_block_reads = 0;
dword break_on = 0xffffffff;
BOOLEAN windev_io(int driveno, dword block, void  *buffer, word count, BOOLEAN reading) /*__fn__*/
{
    DDRIVE *pdr;
	BOOLEAN ret_val;
    pdr = pc_drno_to_drive_struct(driveno);
    if (!pdr)
        return(FALSE);
#if (0)
	if (reading)
	{
		printf("Reading sector %d to %d \n", block, block+count-1);
	}
	else
	{

		printf("Writing sector %d to %d \n", block, block+count-1);
	}
#endif
    if (!win_dev_seek(pdr->logical_unit_number, block))
    {
        //ERTFS_ASSERT(rtfs_debug_zero())
        return(FALSE);
    }
    if (reading)
        ret_val = win_dev_read(pdr->logical_unit_number, buffer, count);
    else
	{
		if (!media_is_writable)
		{
			if (block==0 &&  (*(((byte *)buffer)+510) == 0x66 && *(((byte *)buffer)+511) == 0xAA))
			{
				printf("Writing invalid signature to media to make it writable\n");
				printf("You must remove and re-insert the media before you can write\n\
					   To it ..... \n");
			}
			else
			{
				printf("Rtfs tried to write to write protected media\n");
				printf("Use the HACKWIN7 shell command to make it writable with Rtfs or..\n");
				printf("Failsafe may be trying to create a journal\n");
				printf("Re-start Rtfs with Failsafe AUTO disabled\n");
				return FALSE;
			}
		}
        ret_val = win_dev_write(pdr->logical_unit_number, buffer, count);
	}
	if (reading && block==0)
	{
	 if (*(((byte *)buffer)+510) == 0x66 && *(((byte *)buffer)+511) == 0xAA)
	 {
		 printf("The media is writable with Rtfs but not visible to Windows\n");
		 printf("Use the HACKWIN7 shell command to make it visible to windows\n");
		 media_is_writable=1;
		 if (reading)
			*((byte *)buffer+510) = 0x55;
	 }
	 else if (*(((byte *)buffer)+510) == 0x55 && *(((byte *)buffer)+511) == 0xAA)
	 {
		 printf("The media is readable with Rtfs and visible to Windows\n");
		 printf("Use the HACKWIN7 shell command to make it writable with Rtfs\n");
		 printf("Use the HACKWIN7 again to make it visible to windows\n");
		 media_is_writable=0;
	 }
	}
	return ret_val;
}

/**
*	This function "inserts" a windows device into the rtfs system. If this is not called the device may be
*	inserted but RTFS will not be aware of it.
*/
static void insert_win_device_to_rtfs(struct windev_device *pdevice){
	/* Set up mount parameters and call Rtfs to mount the device    */
extern int show_block_reads;
	struct rtfs_media_insert_args rtfs_insert_parms;
	/* register with Rtfs File System */
	rtfs_insert_parms.devhandle = (void*)pdevice; /* Not used just a handle */
	rtfs_insert_parms.device_type = 999;	/* not used because private versions of configure and release, but must be non zero */
	rtfs_insert_parms.unit_number = 0;

	rtfs_insert_parms.numheads = pdevice->gc.dev_geometry_heads;
	rtfs_insert_parms.secptrk  = pdevice->gc.dev_geometry_secptrack;
	rtfs_insert_parms.numcyl   = pdevice->gc.dev_geometry_cylinders;
	rtfs_insert_parms.media_size_sectors = pdevice->gc.dev_geometry_lbas;

	rtfs_insert_parms.sector_size_bytes =  (dword) 512;
	rtfs_insert_parms.eraseblock_size_sectors =   0;
	rtfs_insert_parms.write_protect    =          0;

	rtfs_insert_parms.device_io                = BLK_DEV_VIRT_blkmedia_io;
	rtfs_insert_parms.device_ioctl             = BLK_DEV_VIRT_blkmedia_ioctl;
	rtfs_insert_parms.device_erase             = 0;
	rtfs_insert_parms.device_configure_media    = BLK_DEV_VIRT_device_configure_media;
	rtfs_insert_parms.device_configure_volume   = BLK_DEV_VIRT_device_configure_volume;

     pc_rtfs_media_insert(&rtfs_insert_parms);
}

/*
*	Remove the device from Rtfs and label it not installed so that other drives may insert it to Rtfs
*/
static void win_device_remove_event(struct windev_device *pdevice_info)
{
	pdevice_info->media_installed = 0;
	pc_rtfs_media_alert((void*)pdevice_info,RTFS_ALERT_EJECT,NULL);
}


HOST_HANDLE  map_win_logical_unit(int logical_unit_number)
{
    if (map_lun_to_windev_devices[logical_unit_number])
    {
        return(map_lun_to_windev_devices[logical_unit_number]->hDevice);
    }
    return(HOST_INVALID_HANDLE_VALUE);
}

//void dismount_win_device(void *devhandle)
//{
//    shutdown_win_device(devhandle);
//}

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
static int BLK_DEV_VIRT_blkmedia_io(void  *devhandle, void *pdrive, dword sector, void  *buffer, dword count, BOOLEAN reading)
{
    RTFS_ARGSUSED_PVOID(devhandle);
    RTFS_ARGSUSED_PVOID(pdrive);
	return(windev_io(((DDRIVE*)pdrive)->driveno, sector, buffer, (word)count, reading));
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
static void PollDeviceReady(void);
/**
*	This is an initialization routine for the windows driver for Rtfs.
*/
static RTFS_DEVI_POLL_REQUEST_VECTOR poll_device_vector_storage;
BOOLEAN BLK_DEV_windev_Mount(void)
{
#if(!REMOVABLE_MEDIA_ONLY)
	printf("Mounting WINDEV DRIVER on P:\n\n");
	printf("WARNING: REMOVABLE_MEDIA_ONLY is not set. All drives will be accessable, please be careful what you write to.\n\n");
#else
	printf("Mounting WINDEV DRIVER on P:\n\n");
	printf("REMOVABLE_MEDIA_ONLY is set. Only removable media will be accessable.\n\n");
#endif
	driver_is_initialized=1;
	pc_rtfs_register_poll_devices_ready_handler(&poll_device_vector_storage, PollDeviceReady);
	return TRUE;
}


#ifdef _MSC_VER





BOOLEAN win_dev_seek(int logical_unit_number, dword block)
{
    LARGE_INTEGER liDistanceToMove;
    LARGE_INTEGER NewFilePointer;
    HOST_HANDLE hDevice;
    hDevice = map_win_logical_unit(logical_unit_number);

    if (hDevice == HOST_INVALID_HANDLE_VALUE)
        return(FALSE);


    liDistanceToMove.QuadPart = block;
    liDistanceToMove.QuadPart *= 512;
    hDevice = map_win_logical_unit(logical_unit_number);

     if (!SetFilePointerEx(hDevice,
          liDistanceToMove,
          &NewFilePointer,
          FILE_BEGIN) )
    {
        ERTFS_ASSERT(rtfs_debug_zero())
        return(FALSE);
    }
    if (liDistanceToMove.QuadPart != NewFilePointer.QuadPart)
    {
        ERTFS_ASSERT(rtfs_debug_zero())
        return(FALSE);
    }
    return(TRUE);
 }

BOOLEAN win_dev_write(int logical_unit_number, void  *buffer, word count)
{
    unsigned long n_to_write;
    unsigned long bytes_written;
     HOST_HANDLE hDevice;
    hDevice = map_win_logical_unit(logical_unit_number);

//	printf("Not writing.. U crazy \n");
//	if (1)
//		return(FALSE);
    if (hDevice == HOST_INVALID_HANDLE_VALUE)
        return(FALSE);

    n_to_write = (unsigned long) count;
    n_to_write *= 512;
    if (!WriteFile(hDevice,
                   buffer,
                   n_to_write,
                   &bytes_written,
                   NULL))
    {

        ERTFS_ASSERT(rtfs_debug_zero())
        return(FALSE);
    }
    if (n_to_write != bytes_written)
    {
        ERTFS_ASSERT(rtfs_debug_zero())
        return(FALSE);
    }
    return(TRUE);
}


BOOLEAN win_dev_read(int logical_unit_number, void  *buffer, word count)
{
    unsigned long n_to_read;
    unsigned long bytes_read;
    HOST_HANDLE hDevice;
    hDevice = map_win_logical_unit(logical_unit_number);
    if (hDevice == HOST_INVALID_HANDLE_VALUE)
        return(FALSE);
    n_to_read = (unsigned long) count;
    n_to_read *= 512;

    if (!ReadFile(hDevice,
                   buffer,
                   n_to_read,
                   &bytes_read,
                   NULL))
    {
        ERTFS_ASSERT(rtfs_debug_zero())
        return(FALSE);
    }
    if (n_to_read != bytes_read)
    {
        ERTFS_ASSERT(rtfs_debug_zero())
        return(FALSE);
    }
    return(TRUE);
}


char *windev_device_names[] =
{
"\\\\.\\PhysicalDrive0",
"\\\\.\\PhysicalDrive1",
"\\\\.\\PhysicalDrive2",
"\\\\.\\PhysicalDrive3",
"\\\\.\\PhysicalDrive4",
"\\\\.\\PhysicalDrive5",
"\\\\.\\PhysicalDrive6",
"\\\\.\\PhysicalDrive7",
"\\\\.\\PhysicalDrive8",
"\\\\.\\PhysicalDrive9",
};


static BOOL get_drive_geometry(HOST_HANDLE hDevice,struct windev_device *pdevice_info)
{
  DISK_GEOMETRY dg;
  BOOL bResult;                 /* results flag */
  DWORD junk;                   /* discard results */

  pdevice_info->gc.dev_geometry_heads = 0;
  pdevice_info->gc.dev_geometry_cylinders = 0;
  pdevice_info->gc.dev_geometry_secptrack = 0;
  pdevice_info->gc.dev_geometry_lbas = 0;
  pdevice_info->gc.fmt_parms_valid = 0;

  if (0)
  {
   DISK_GEOMETRY_EX dg;

	bResult = DeviceIoControl(hDevice,  /* device to be queried */
      IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,  /* operation to perform */
                             NULL, 0, /* no input buffer */
                            &dg, sizeof(dg),     /* output buffer */
                            &junk,                 /* # bytes returned */
                            (LPOVERLAPPED) NULL);  /* synchronous I/O */
      if (bResult)
      {
      	pdevice_info->gc.dev_geometry_cylinders = (dword) dg.Geometry.Cylinders.LowPart;
      	pdevice_info->gc.dev_geometry_secptrack = (int) dg.Geometry.SectorsPerTrack;
      	pdevice_info->gc.dev_geometry_heads = (int) dg.Geometry.TracksPerCylinder;
      	pdevice_info->gc.dev_geometry_lbas = (dword)(dg.DiskSize.QuadPart/512);
      	printf("lbas by _ex method %d %X\n", pdevice_info->gc.dev_geometry_lbas, pdevice_info->gc.dev_geometry_lbas);
      }
  }
  bResult = DeviceIoControl(hDevice,  /* device to be queried */
      IOCTL_DISK_GET_DRIVE_GEOMETRY,  /* operation to perform */
                             NULL, 0, /* no input buffer */
                            &dg, sizeof(dg),     /* output buffer */
                            &junk,                 /* # bytes returned */
                            (LPOVERLAPPED) NULL);  /* synchronous I/O */
  if (bResult)
  {
    pdevice_info->gc.dev_geometry_cylinders = (dword) dg.Cylinders.LowPart;
    pdevice_info->gc.dev_geometry_secptrack = (int) dg.SectorsPerTrack;
    pdevice_info->gc.dev_geometry_heads = (int) dg.TracksPerCylinder;
    pdevice_info->gc.dev_geometry_lbas = (dword) (dg.Cylinders.LowPart * dg.TracksPerCylinder * dg.SectorsPerTrack);
    pdevice_info->bytes_per_sector = dg.BytesPerSector;
#ifdef FORCE_LBAVALUE
  	printf("lbas by _hcn method %d %X\n", pdevice_info->gc.dev_geometry_lbas, pdevice_info->gc.dev_geometry_lbas);
	pdevice_info->gc.dev_geometry_lbas =  FORCE_LBAVALUE;
	printf("Forced lbas to %d %X\n", pdevice_info->gc.dev_geometry_lbas, pdevice_info->gc.dev_geometry_lbas);
#endif
    if (dg.MediaType == RemovableMedia)
        pdevice_info->is_removable = TRUE;
    else
        pdevice_info->is_removable = FALSE;
  }

  return (bResult);
}

int device_is_fixed[MAX_WINDEV_DEVICES];

/**
*	This function creates and maintains a list of devices that are currently attached to the computer and
*   can be accessed.  If a new device has been inserted then this function returns TRUE indicating that a
*   change has occured.  If REMOVABLE MEDIA ONLY is set then this function ignores media that is not removable.
*/
BOOLEAN win_dev_enum_devices()
{
    int i;
    HOST_HANDLE hDevice;
    BOOLEAN retVal = FALSE;
	DWORD dwBytesReturned=0;
	//on the first run initialize all structures to a starting, invalid, state.
	if(num_windev_devices == -1){
		memset(map_lun_to_windev_devices, 0, sizeof(map_lun_to_windev_devices));
		num_windev_devices = 0;
		memset(windev_devices, 0, sizeof(windev_devices));

		for (i = 0; i < MAX_WINDEV_DEVICES; i++)
		{
		    windev_devices[i].hDevice = HOST_INVALID_HANDLE_VALUE;
			windev_devices[i].media_installed = 0;
		}
		num_windev_devices = 0;
		retVal = TRUE;
	}

	for(i = 0; i < MAX_WINDEV_DEVICES; i++){

		if (device_is_fixed[i])
			continue;

		//if a device exists in the list but is no longer accesible the release the handle and invalidate the
		//stucture
		if(windev_devices[i].hDevice != HOST_INVALID_HANDLE_VALUE){
			if (!DeviceIoControl(windev_devices[i].hDevice, IOCTL_STORAGE_CHECK_VERIFY, NULL, 0, NULL, 0,
			     &dwBytesReturned, NULL))
			{
				CloseHandle(windev_devices[i].hDevice);
				windev_devices[i].hDevice = HOST_INVALID_HANDLE_VALUE;
				num_windev_devices -= 1;
			}
		}
		else{//else if a structure does not currenlty contain a device try to fill it with one

			hDevice = CreateFile(windev_device_names[i],  /* */
							 GENERIC_READ|GENERIC_WRITE,  /* read write access to the drive */
							 FILE_SHARE_READ|FILE_SHARE_WRITE, /* share mode */
							 NULL,             /* default security attributes */
							 OPEN_EXISTING,    /* disposition */
							 FILE_FLAG_DELETE_ON_CLOSE|FILE_FLAG_NO_BUFFERING|FILE_FLAG_WRITE_THROUGH,    /* file attributes */
							 NULL);            /* do not copy file attributes */
			if (hDevice != HOST_INVALID_HANDLE_VALUE)//if a new device exists
			{
				if (get_drive_geometry(hDevice,&windev_devices[num_windev_devices]))//get information about the device
				{
#if(REMOVABLE_MEDIA_ONLY)
					if(!windev_devices[num_windev_devices].is_removable){
						CloseHandle(hDevice);
						device_is_fixed[i] = 1;
					}
					else
#endif
					{
						win_dev_get_layout(hDevice,i,&windev_devices[num_windev_devices]);
						num_windev_devices += 1;
						retVal = TRUE;
					}

				}

			}
		}
	}




    return(retVal);
}

static BOOLEAN win_dev_get_layout(HOST_HANDLE hDevice,int device_number,struct windev_device *pdevice_info)
{
    byte buffer[2048];
    dword BytesReturned;

    if (hDevice != HOST_INVALID_HANDLE_VALUE)
    {
        pdevice_info->hDevice = hDevice;
        pdevice_info->device_number = device_number;
        pdevice_info->num_partitions = 0;

        if (!DeviceIoControl(hDevice,
             IOCTL_DISK_GET_DRIVE_LAYOUT, /* dwIoControlCode */
             NULL,                        /* lpInBuffer */
             0,                           /* nInBufferSize */
             (LPVOID) buffer,        /* output buffer */
             (DWORD) 2048,      /* size of output buffer */
            (LPDWORD) &BytesReturned,   /* number of bytes returned */
            NULL))
        {
            DWORD dw = GetLastError();
            pdevice_info->media_installed = 0;
            dw = GetLastError();
            return(FALSE);
        }
        else
        {
            PDRIVE_LAYOUT_INFORMATION playout;
            PPARTITION_INFORMATION ppartitions;
            dword i;
            pdevice_info->media_installed = 0;

            playout = (PDRIVE_LAYOUT_INFORMATION) buffer;
            ppartitions = playout->PartitionEntry;
            pdevice_info->mediasize = playout->PartitionEntry->PartitionLength.QuadPart;

            if (!ppartitions->RecognizedPartition)
            {
                pdevice_info->num_partitions =  1;
                pdevice_info->partition_names[0] = "No partition table found.";
            }
            else
            {

                for (i = 0; i < playout->PartitionCount; i++, ppartitions++)
                {
                    pdevice_info->num_partitions += 1;

                    switch(ppartitions->PartitionType)
                    {
                    case 1: /* PARTITION_FAT_12: */
                            pdevice_info->partition_names[i] = "A FAT12 file system partition. ";
                            break;
                    case 4: /* PARTITION_FAT_16: */
                    case 6:
                            pdevice_info->partition_names[i] = "A FAT16 file system partition. ";
                            break;
                    case 0xe:
                    case 0xc:
                    case 0x55:
                    case 0xb: /* PARTITION_FAT32: */
                            pdevice_info->partition_names[i] = "A FAT32 file system partition. ";
                            break;
                    case PARTITION_EXTENDED:
                            pdevice_info->partition_names[i] = "An extended partition. ";
                            break;
                    case PARTITION_IFS:
                            pdevice_info->partition_names[i] = "An IFS exFat partition. ";
                            break;
                    case PARTITION_LDM:
                            pdevice_info->partition_names[i] = "A logical disk manager (LDM) partition. ";
                            break;
                    case PARTITION_NTFT:
                            pdevice_info->partition_names[i] = "An NTFT partition. ";
                            break;
                    case VALID_NTFT:
                            pdevice_info->partition_names[i] = "A valid NTFT petition.";
                             break;
                    default:
                            pdevice_info->partition_names[i] = "No partiton";
                            pdevice_info->num_partitions -= 1;
                            if (ppartitions->PartitionType != 0)
                            {
                                WINDOWS_WINDEV_SPRINTF(print_buffer,"[Partition # %d] Unknown Type [%d]\n", i, ppartitions->PartitionType );
                                rtfs_kern_puts((byte *)print_buffer);
                                pdevice_info->partition_names[i] = "Unknown partiton type";
                            }
                             break;
                    }
                }
            }
        }
    }
    return(TRUE);
}

/*
*	This method is a thread.  One of these threads is spawned for each Drive that is available in Rtfs(HEREHERE
*   this is not currently implemented). The method polls Windows for insertion events.  If a media is inserted to
*   the machine and is chosen to be inserted into Rtfs then all this thread does is wait until the media is removed
*   from the machine.  Once this occurs the user is asked if they would like to insert one of the remaining media
*   into Rtfs.  If they choose a media then it is inserted and listens for a removal of that media.  If they do not
*   choose to insert any new media (-1) then the thread waits until a new media is inserted then asks the user to
*   select the media they would like to insert into Rtfs.
*/
bool recentRemoval = FALSE;
int inserted_one_shot = 0;
static void PollDeviceReady(void)
{
    int logical_unit_number = (int) 0;
	int device_number = -1;
	bool changed = TRUE;
    char cont[5];
	if (!driver_is_initialized)
		return;
// 	for(;;)
    {
        DWORD dwBytesReturned=0;
		//check to see if we have a media to check
        if(currentDevice != NULL && currentDevice->hDevice != NULL &&
									currentDevice->hDevice != HOST_INVALID_HANDLE_VALUE){
            // if this is false then the device is no longer present
            if (!DeviceIoControl(currentDevice->hDevice, IOCTL_STORAGE_CHECK_VERIFY, NULL, 0, NULL, 0,
				                 &dwBytesReturned, NULL))
            {
                // tell the rest of the world that media is not present
                printf("\nDrive [%s] was removed. Press Return.\n", windev_device_names[currentDevice->device_number]);
				//remove from rtfs
				currentDevice->media_installed = 0;
				pc_rtfs_media_alert((void*)currentDevice,RTFS_ALERT_EJECT,NULL);

				//close the handle cause it no longer exists
				CloseHandle(currentDevice->hDevice);
				currentDevice->hDevice = HOST_INVALID_HANDLE_VALUE;

				//no longer have a current device
				currentDevice = NULL;

				//decrease device count
				num_windev_devices -= 1;
				//indicate a removal has occured and we should enumerate the devices
				recentRemoval = TRUE;
            }
        }
        else // have no media so see if a change has occured and ask the user if they would like to insert something
        {

			changed = win_dev_enum_devices();
			/*recentRemoval is needed so that when an inserted drive is removed
			since we already removed it in the if statement win_dev_enum_devices will
			not detect a change.  But if there was a recentRemoval we need to ask if
			we need to mount again.*/
			if(changed || recentRemoval ){
				recentRemoval = FALSE;
				currentDevice = NULL;

				if(num_windev_devices == 0){
					printf("There is no media currently available, press Return.");
					rtfs_kern_gets((byte*)cont);
					device_number = -1;
				}
				else{
                    int i;
					printf("\n\nChange in inserted media detected.\n\n");
					for (i = 0; i < num_windev_devices; i++)//for each device list some info about it
					{

						if (windev_devices[i].num_partitions==1)
						{
							printf("Device #%d, <%15.15s> contains 1 partition size == %I64d\n", i, windev_device_names[windev_devices[i].device_number], windev_devices[i].mediasize);
							printf("...Partition: %s\n\n", windev_devices[i].partition_names[0]);
						}
						else
						{
							int j;
							printf("Device #%d, <%15.15s> contains multiple partitions size ==  %I64d\n", i, windev_device_names[windev_devices[i].device_number],  windev_devices[i].mediasize);
							for (j = 0; j < windev_devices[i].num_partitions;j++)
							{
								printf("...Partition (%d): %s\n", j, windev_devices[i].partition_names[j]);
							}
							printf("\n");
						}
					}
					printf("Select which device you would like to mount on Rtfs, select -1 to not mount a device. Device #: ");
					rtfs_kern_gets((byte*)cont);
					device_number = atoi(cont);
#if(!REMOVABLE_MEDIA_ONLY)
					/* Don't allow device zero. that's probably the primary device */
					while (device_number == 0)
					{
						printf("You selected zero (that\'s too dangerous ? please select again) or -1 #: ");
						rtfs_kern_gets((byte*)cont);
						device_number = atoi(cont);
					}
#endif
				}

			}

			if(device_number != -1 && //make sure they selected the device
			   windev_devices[device_number].hDevice != NULL && //make sure their selection was valid
			   windev_devices[device_number].hDevice != HOST_INVALID_HANDLE_VALUE &&//make sure their selection was valid
			   !windev_devices[device_number].media_installed)//make sure the media is not already installed on another drive
			{
				map_lun_to_windev_devices[logical_unit_number] = &windev_devices[device_number];
				windev_devices[device_number].logical_unit_number = logical_unit_number;

				currentDevice = &windev_devices[device_number];

				/* It isn;t writable until we detect AA66 in the first sector */
				media_is_writable=0;

				printf("\nDrive [%s] was inserted \n", windev_device_names[currentDevice->device_number]);
	            //insert into Rtfs
				insert_win_device_to_rtfs(currentDevice);

				currentDevice->media_installed = 1;

				printf("Should this device be write protected? (y/n) ");
				rtfs_kern_gets((byte*)cont);
				if(cont[0] == 'y' || cont[0] == 'Y'){
					pc_rtfs_media_alert((void*)currentDevice,RTFS_ALERT_WPSET,NULL);
				}
				else{
					pc_rtfs_media_alert((void*)currentDevice,RTFS_ALERT_WPCLEAR,NULL);
				}
				inserted_one_shot = 1;

	        }
			else if(device_number != -1){//if we did not choose to skip insertion but still missed if then the media is not avail
				printf("Invalid Selection: The media selected is not available. If you think there is an error please re-insert the media.");
			}
        }

    }
}

/* Note: hostdevice_media_parms is initialized here but declare in hostdisk.c, this is done to allow the
   common handlers for all "virtual" devices to reside in hostdisk.c */

void shutdown_win_device(void *devhandle)
{
struct windev_device *pDevice;

    pDevice = (struct windev_device *)devhandle;
    printf("shutdown_win_device() setting installed to 0, and closing \n");
    pDevice->media_installed = 0;

    // media's gone, so the handle's no good
   if (pDevice->hDevice != HOST_INVALID_HANDLE_VALUE && pDevice->hDevice != NULL)
       CloseHandle(pDevice->hDevice);
   pDevice->hDevice = HOST_INVALID_HANDLE_VALUE;
 }

#else
BOOLEAN win_dev_seek(int logical_unit_number, dword block)
{
	return(0);
}
BOOLEAN win_dev_write(int logical_unit_number, void  *buffer, word count)
{

    return(TRUE);
}

BOOLEAN win_dev_read(int logical_unit_number, void  *buffer, word count)
{
    return(TRUE);
}
void PollDeviceReady(void)
{
}
#endif /* _MSC_VER */

#endif /* (INCLUDE_WINDEV) */
