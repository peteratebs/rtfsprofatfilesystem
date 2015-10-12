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

#include "portconf.h"   /* For included devices */
#if (INCLUDE_WINDEV)
#undef INCLUDE_IDE
#undef INCLUDE_PCMCIA
#undef INCLUDE_PCMCIA_SRAM
#undef INCLUDE_COMPACT_FLASH
#undef INCLUDE_FLASH_FTL
#undef INCLUDE_ROMDISK
#undef INCLUDE_RAMDISK
#undef INCLUDE_MMCCARD
#undef INCLUDE_SMARTMEDIA
#undef INCLUDE_FLOPPY
#undef INCLUDE_HOSTDISK
#undef INCLUDE_WINDEV
#undef INCLUDE_UDMA
#undef INCLUDE_82365_PCMCTRL

#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include "rtfs.h"
#include "portconf.h"   /* For included devices */

/* windev - Device driver that acceses XP devices directly */
#include <stdio.h>
#include <io.h>
#include <fcntl.h>
#include <sys\types.h>
#include <stdlib.h>
#include <string.h>

#define WINDOWS_WINDEV_SPRINTF sprintf
char print_buffer[128];

void calculate_hcn(long n_blocks, PDEV_GEOMETRY pgeometry);


/*
*
*   Perform io to and from the win dev disk.
*
*   If the reading flag is true copy data from the hostdisk (read).
*   else copy to the hostdisk. (write).
*
*/

BOOLEAN win_dev_seek(int logical_unit_number, dword block);
BOOLEAN win_dev_write(int logical_unit_number, void  *buffer, word count);
BOOLEAN win_dev_read(int logical_unit_number, void  *buffer, word count);
BOOLEAN win_dev_get_geometry(int logical_unit_number, DEV_GEOMETRY *gc);
BOOLEAN win_dev_check_change(int logical_unit_number);
BOOLEAN win_dev_enum_devices(void);
HANDLE  map_win_logical_unit(int logical_unit_number);
int win_select_device_for_lun(int logical_unit_number);

BOOLEAN windev_io(int driveno, dword block, void  *buffer, word count, BOOLEAN reading) /*__fn__*/
{
    DDRIVE *pdr;
    pdr = pc_drno_to_drive_struct(driveno);
    if (!pdr)
        return(FALSE);
    if (!win_dev_seek(pdr->logical_unit_number, block))
    {
        ERTFS_ASSERT(rtfs_debug_zero())
        return(FALSE);
    }
    if (reading)
        return(win_dev_read(pdr->logical_unit_number, buffer, count));
    else
        return(win_dev_write(pdr->logical_unit_number, buffer, count));
}


int windev_perform_device_ioctl(int driveno, int opcode, void * pargs)
{
DDRIVE *pdr;

    pdr = pc_drno_to_drive_struct(driveno);
    if (!pdr)
        return (-1);

    switch (opcode)
    {
        case DEVCTL_GET_GEOMETRY:
        {
            DEV_GEOMETRY gc;

            rtfs_memset(&gc, 0, sizeof(gc));

            if (!win_dev_get_geometry(pdr->logical_unit_number, &gc))
            {
                ERTFS_ASSERT(rtfs_debug_zero())
				return(-1);
            }
/*           calculate_hcn(gc.dev_geometry_lbas, &gc); */
            copybuff(pargs, &gc, sizeof(gc));
            return (0);
        }
        case DEVCTL_FORMAT:
            break;
        case DEVCTL_REPORT_REMOVE:
            pdr->drive_flags &= ~DRIVE_FLAGS_INSERTED;
            return(0);
        case DEVCTL_CHECKSTATUS:
			if (win_dev_check_change(pdr->logical_unit_number))
				return(DEVTEST_CHANGED);
			else
                return(DEVTEST_NOCHANGE);

/*           if (pdr->drive_flags & DRIVE_FLAGS_INSERTED) */
/*              return(DEVTEST_NOCHANGE); */
/*            else */
/*           { */
/*               pdr->drive_flags |= DRIVE_FLAGS_INSERTED; */
/*               return(DEVTEST_CHANGED); */
/*           } */
        case DEVCTL_WARMSTART:
        {
			int win_dev_table_index;

			if (!win_dev_enum_devices())
				return(-1);
			win_dev_table_index = win_select_device_for_lun(pdr->logical_unit_number);
			/* See if it already exists */
            if (map_win_logical_unit(pdr->logical_unit_number) == INVALID_HANDLE_VALUE)
            {
               return(-1);
            }
            else
                pdr->drive_flags |= (DRIVE_FLAGS_VALID|DRIVE_FLAGS_INSERTED|DRIVE_FLAGS_REMOVABLE);
            return(0);
        }
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


BOOLEAN win_dev_seek(int logical_unit_number, dword block)
{

    LARGE_INTEGER liDistanceToMove;
    LARGE_INTEGER NewFilePointer;
	HANDLE hDevice;
    hDevice = map_win_logical_unit(logical_unit_number);

	if (hDevice == INVALID_HANDLE_VALUE)
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
 	HANDLE hDevice;
    hDevice = map_win_logical_unit(logical_unit_number);

	if (hDevice == INVALID_HANDLE_VALUE)
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
	HANDLE hDevice;
    hDevice = map_win_logical_unit(logical_unit_number);
	if (hDevice == INVALID_HANDLE_VALUE)
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


BOOLEAN win_dev_check_change(int logical_unit_number)
{
dword dwBytesReturned;
	HANDLE hDevice;

    hDevice = map_win_logical_unit(logical_unit_number);

if (DeviceIoControl(hDevice,
	IOCTL_STORAGE_CHECK_VERIFY ,
	NULL, 0,
	NULL, 0,
	&dwBytesReturned,
	NULL))
		return(FALSE); /* " the media is inside the drive") */
	else
	{
		DWORD dw = GetLastError();
		rtfs_kern_puts((byte *)"media is not inside the drive\n");
		dw = GetLastError();
		return(TRUE);
	}

}


#define MAX_WINDEV_DEVICES 10
int num_windev_devices;
struct windev_device {
	int device_number;
	HANDLE hDevice;
	BOOLEAN media_installed;
	int num_partitions;
	int logical_unit_plus_one; /* Zero menas not used */
	char *partition_names[26];
    DEV_GEOMETRY gc; /* RTFS Geometry structure */
};





struct windev_device windev_devices[MAX_WINDEV_DEVICES];
struct windev_device *map_lun_to_windev_devices[MAX_WINDEV_DEVICES];
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

HANDLE  map_win_logical_unit(int logical_unit_number)
{
	if (map_lun_to_windev_devices[logical_unit_number])
	{
		return(map_lun_to_windev_devices[logical_unit_number]->hDevice);
	}
	return(INVALID_HANDLE_VALUE);
}

BOOLEAN win_dev_get_geometry(int logical_unit_number, DEV_GEOMETRY *gc)
{
	HANDLE hDevice;
    hDevice = map_win_logical_unit(logical_unit_number);

	if (hDevice == INVALID_HANDLE_VALUE)
		return(FALSE);
	*gc = map_lun_to_windev_devices[logical_unit_number]->gc;
    return(TRUE);
}

BOOLEAN win_dev_get_layout(HANDLE hDevice,int device_number,struct windev_device *pdevice_info);

BOOL GetDriveGeometry(HANDLE hDevice,struct windev_device *pdevice_info);
BOOLEAN win_dev_enum_devices(void)
{
	int i;
	HANDLE hDevice;
	if (num_windev_devices)
		return(TRUE);

	memset(map_lun_to_windev_devices, 0, sizeof(map_lun_to_windev_devices));
	num_windev_devices = 0;
	memset(windev_devices, 0, sizeof(windev_devices));
	for (i = 0; i < MAX_WINDEV_DEVICES; i++)
		windev_devices[i].hDevice = INVALID_HANDLE_VALUE;

	for (i = 0; i < MAX_WINDEV_DEVICES; i++)
	{
		hDevice = CreateFile(windev_device_names[i],  /* (G:) ?  drive to open */
		                GENERIC_READ|GENERIC_WRITE,  /* read write access to the drive */
			            FILE_SHARE_READ | /* share mode */
				        FILE_SHARE_WRITE,
					    NULL,             /* default security attributes */
						OPEN_EXISTING,    /* disposition */
						FILE_FLAG_DELETE_ON_CLOSE|FILE_FLAG_NO_BUFFERING|FILE_FLAG_WRITE_THROUGH,	/* file attributes */
						NULL);            /* do not copy file attributes */
			if (hDevice != INVALID_HANDLE_VALUE)
            {
                GetDriveGeometry(hDevice,&windev_devices[num_windev_devices]); /* TEST */
            }
			win_dev_get_layout(hDevice,num_windev_devices,&windev_devices[num_windev_devices]);
			if (hDevice != INVALID_HANDLE_VALUE)
            {
				num_windev_devices += 1;
            }
	}
	return(TRUE);
}

BOOLEAN win_dev_get_layout(HANDLE hDevice,int device_number,struct windev_device *pdevice_info)
{
byte buffer[2048];
dword BytesReturned;

	if (hDevice != INVALID_HANDLE_VALUE)
	{
		pdevice_info->hDevice = hDevice;
		pdevice_info->device_number = device_number;
		pdevice_info->num_partitions = 0;

		WINDOWS_WINDEV_SPRINTF(print_buffer,"[Device # %d contains...]\n", device_number);
        rtfs_kern_puts((byte *)print_buffer);

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
			return(TRUE);
		}
		else
		{
			PDRIVE_LAYOUT_INFORMATION playout;
			PPARTITION_INFORMATION ppartitions;
			dword i;
			pdevice_info->media_installed = 1;

			playout = (PDRIVE_LAYOUT_INFORMATION) buffer;
			ppartitions = playout->PartitionEntry;
			pdevice_info->num_partitions = 0;
			if (ppartitions->RecognizedPartition)
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
						pdevice_info->partition_names[i] = "An IFS partition. ";
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
						pdevice_info->num_partitions -= 1;
						WINDOWS_WINDEV_SPRINTF(print_buffer,"[Partition # %d] Unknown Type [%d]\n", i, ppartitions->PartitionType );
						rtfs_kern_puts((byte *)print_buffer);
						pdevice_info->partition_names[i] = "Unknown partiton type";
 						break;
					}
			}
			}
		}
	}
	return(TRUE);
}

int win_select_device_for_lun(int logical_unit_number)
{
	int i;
	int selection;
	byte buf[20];


	if (!num_windev_devices)
	{
		rtfs_kern_puts((byte *)"No windev devices available\n");
		return(-1);
	}
	WINDOWS_WINDEV_SPRINTF(print_buffer,"Please select a device To associate with win_dev logical unit # <%d>\n", logical_unit_number);
    rtfs_kern_puts((byte *)print_buffer);


	for (i = 0; i < num_windev_devices; i++)
	{
		if (windev_devices[i].hDevice != INVALID_HANDLE_VALUE)
		{
			if (!windev_devices[i].media_installed)
            {
				WINDOWS_WINDEV_SPRINTF(print_buffer,"%d, <%s> (%s)\n", i, windev_device_names[windev_devices[i].device_number], "No Media Installed");
				rtfs_kern_puts((byte *)print_buffer);
            }
			else
			{
				if (windev_devices[i].num_partitions==1)
                {
					WINDOWS_WINDEV_SPRINTF(print_buffer,"%d, <%s> (%s)\n", i, windev_device_names[windev_devices[i].device_number], windev_devices[i].partition_names[0]);
					rtfs_kern_puts((byte *)print_buffer);
                }
				else
				{ int j;
					WINDOWS_WINDEV_SPRINTF(print_buffer,"%d, <%s> (Multiple Partitions)\n", i, windev_device_names[windev_devices[i].device_number], windev_devices[i].partition_names[0]);
					rtfs_kern_puts((byte *)print_buffer);
					for (j = 0; j < windev_devices[i].num_partitions;j++)
                    {
						WINDOWS_WINDEV_SPRINTF(print_buffer,"      (%d)      %s\n", j, windev_devices[i].partition_names[j]);
						rtfs_kern_puts((byte *)print_buffer);
                    }
				}
			}
		}
	}
	do
	{
		selection = -1;
		rtfs_kern_puts((byte *)"Select >");
        rtfs_kern_gets(buf);
		selection = atoi((char*)buf);
	} while (selection < 0 || selection >= num_windev_devices);
	map_lun_to_windev_devices[logical_unit_number] = &windev_devices[selection];
	return(selection);
}

BOOL GetDriveGeometry(HANDLE hDevice,struct windev_device *pdevice_info)
{
  DISK_GEOMETRY dg;
  BOOL bResult;                 /* results flag */
  DWORD junk;                   /* discard results */

  pdevice_info->gc.dev_geometry_heads = 0;
  pdevice_info->gc.dev_geometry_cylinders = 0;
  pdevice_info->gc.dev_geometry_secptrack = 0;
  pdevice_info->gc.dev_geometry_lbas = 0;
  pdevice_info->gc.fmt_parms_valid = 0;


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
  }
  return (bResult);
}

#endif /* (INCLUDE_WINDEV) */
