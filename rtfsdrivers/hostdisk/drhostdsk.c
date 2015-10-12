/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1993-2005
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* winhdisk.c - Simulate disk drive in a file on the host operating system

Summary

 Description
    Provides a device driver that reads and writes data to a file on the
    host disk. This version of the driver creates a file named
    HOSTDISK.DAT. This is targeted for hosts running Win95/NT but it can
    be migrated to other hosts.  It uses mostly POSIX calls.

    This driver is used primarily for the MKROM utility but it is
    fully functional and can be used for other purposes.

*/

/* Define RTFS_LINUX or RTFS_WINDOWS if we are running in an emulation environment
   select between linux and windows
*/
#ifdef __GNUC__
#define RTFS_LINUX
#endif
#ifdef _MSC_VER
#define RTFS_WINDOWS
#endif

#if (!defined(RTFS_WINDOWS) && !defined(RTFS_LINUX))
/* Try ANSI IO routines if Windows or linux are not defined */
#define RTFS_STDIO
#endif

#ifdef RTFS_WINDOWS
#include <windows.h>
#include <winioctl.h>
#include <io.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>


#endif

#include "rtfs.h"

#ifdef RTFS_LINUX
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#endif

#ifdef RTFS_STDIO
#include <stdio.h>
#endif

#define HOSTDISK_SECTORSIZEBYTES 512  /* Must be 512 */

#define BUFFER_VIRTUAL_DISK_IO 0

/* Works on win32 for accesses < 1 GIG since the cache is limitted */
#define USE_SHADOWRAM 0
#define NUM_SHADOW_SEGMENTS 4
#define MAX_ZERO_SEGMENT 1
#if(USE_SHADOWRAM)
void malloc_64bit_volume(int unit, dword nblocks, BOOLEAN fill_file, byte fill_pattern);
BOOLEAN shadow_in_ram=TRUE;
#endif

#ifdef RTFS_WINDOWS
#define NSECTORSPERBUFFER 64
byte winbuff[512*NSECTORSPERBUFFER];
int savesectorstofile(byte *filename, byte *drive_name, dword start, dword end)
{
int fd,driveno;
dword blockno,nxferred;

DDRIVE *pdr;

    /* Get the drive and make sure it is mounted   */
    driveno = check_drive_name_mount(drive_name, CS_CHARSET_NOT_UNICODE);
    /* if error check_drive errno was set by check_drive */
    if (driveno < 0)
		return(-1);
    pdr = pc_drno2dr(driveno);

	fd = _open((char *)filename, O_BINARY|O_RDWR|O_CREAT|_O_TRUNC,0666);
	if (fd < 0)
		goto error;

	nxferred = 0;
	for (blockno = start; blockno <= end; blockno += nxferred)
	{
		if ((end - blockno) >= NSECTORSPERBUFFER)
			nxferred = NSECTORSPERBUFFER;
		else
			nxferred = 1;
        if (!raw_devio_xfer(pdr, blockno, &winbuff[0] , nxferred, TRUE, TRUE))
			goto error;
		if (_write(fd, winbuff, nxferred*512) != (int) (nxferred*512))
			goto error;
	}
	_close(fd);
    release_drive_mount(driveno);/* Release lock, unmount if aborted */
error:
	return 0;
}

int restoresectorsfromfile(byte *filename, byte *drive_name, dword start, dword end)
{
int fd,driveno;
dword blockno,bulkstart,nxferred,total;
DDRIVE *pdr;

    /* Get the drive, don't check mounted   */
    if (pc_parsedrive( &driveno, drive_name, CS_CHARSET_NOT_UNICODE))
    {
    	if (!check_drive_by_number(driveno, FALSE))
        	return(-1);
    }

    /* if error check_drive errno was set by check_drive */
    if (driveno < 0)
		return(-1);
    pdr = pc_drno2dr(driveno);

	fd = _open((char *)filename, O_BINARY|O_RDONLY,0);
	if (fd < 0)
		goto error;

	/* If we are restoring sector zero do it last. If you do it first windows will not let you
	   write other sectors */
	bulkstart = start;
	if (bulkstart == 0 && end != 0)
	{
		int r;
		if ((r = _read(fd, winbuff, 512)) != 512)
			goto error;
		bulkstart = 1;
	}

	total = 0;
	nxferred = 0;
	for (blockno = bulkstart; blockno <= end; blockno += nxferred)
	{
		int n_read = 0;
		if ((end - blockno)+1 >= NSECTORSPERBUFFER)
			nxferred = NSECTORSPERBUFFER;
		else
			nxferred = 1;
		n_read = _read(fd, winbuff, (512*nxferred));
		if (n_read != (int) (512*nxferred))
			goto error;
	total += n_read;
        if (!raw_devio_xfer(pdr, blockno, winbuff , nxferred, TRUE, FALSE))
			goto error;
	}
	_close(fd);

	if (bulkstart != start)
	{
		fd = _open((char *)filename, O_BINARY|O_RDONLY,0);
		if (fd < 0)
			goto error;
		if (_read(fd, winbuff, 512) != 512)
			goto error;
        if (!raw_devio_xfer(pdr, start, winbuff , 1, TRUE, FALSE))
			goto error;
		_close(fd);
	}

    release_drive_mount(driveno);/* Release lock, unmount if aborted */
	return 0;
error:
	_close(fd);
    release_drive_mount(driveno);/* Release lock, unmount if aborted */
	printf("failure\n");
	return -1;
}
#endif



#if (INCLUDE_HOSTDISK)
#define NANDSIM_FILE_NAME "NAND0"
#define HOSTDISK_FILE_NAME "Hostdisk"
// #define HOSTDISK_FILE_NAME "E:\\demotest\\Hostdisk"

/* Emulate a disk drive using file IO, A additional abstraction layer is provided to allow emulation
   of large disks > 4 Gygabyte using 32 bit host files */

#if (defined(RTFS_STDIO) && defined(RTFS_WINDOWS))
/* See above */
#error Please duplicate RTFS_WINDOWS definition inside hostdisk
#endif

#if (defined(RTFS_STDIO) && defined(RTFS_LINUX))
/* See above */
#error Please duplicate RTFS_LINUX definition inside hostdisk
#endif

/* hostdisk - Device driver that uses a DOS file as a virtual disk.

 Description
    Provides a virtual volume that resides in a (host) DOS file. This
    volume can be used to prototype applications. It can also be used to build
    the image of a rom disk which can then be accessed through the
    rom disk driver. (see winmkrom.c)

    To enable this driver set INCLUDE_HOSTDISK to 1 in portconf.h
*/


int hostdisk_perform_device_ioctl(int driveno, int opcode, void * pargs);
static int simulated_dynamic_ioctl(int driveno, int opcode, void * pargs);
BOOLEAN hostdisk_io_64(int unit, dword block, void  *buffer, word _count, BOOLEAN reading); /*__fn__*/

#define DEFAULT_HOST_DISK_SIZE 10240 /* 5M, FAT16 */

#define HOSTDISK_SPRINTF sprintf

#define MAXSEGMENTS_PER_UNIT 16
#define MAX_UNITS 2
#define HOSTDISK_HOSTDISK_UNIT                    0
#define NANDSIM_HOSTDISK_UNIT                     1

struct file64 {
	char basename[255];
	int num_segments;
#if(USE_SHADOWRAM)
	byte *memory_handles[MAXSEGMENTS_PER_UNIT];
#endif
#ifdef RTFS_WINDOWS
	HANDLE segment_handles[MAXSEGMENTS_PER_UNIT];
#endif
#ifdef RTFS_LINUX
	int segment_handles[MAXSEGMENTS_PER_UNIT];
#endif
#ifdef RTFS_STDIO
	FILE *segment_handles[MAXSEGMENTS_PER_UNIT];
#endif
};

BOOLEAN flush_all_writes;
static int segment_table_initialized = 0;
struct file64 sixty_four_bit_volumes[MAX_UNITS];

/*  there are 0x200000 blocks per gigabyte segment */
#define BLOCKS_PER_GIG 0x200000

static dword size_64bit_volume(int unit);

static void init_segment_table(void)
{
int i;
    if (segment_table_initialized)
        return;
    for (i = 0 ;i < MAX_UNITS; i++)
        sixty_four_bit_volumes[i].num_segments = 0;
    segment_table_initialized = 1;
}

int alloc_64bit_unit(byte *basename)
{
int i;
    init_segment_table();
    for (i = 0 ;i < MAX_UNITS; i++)
    {
        if (!sixty_four_bit_volumes[i].num_segments)
        {
            rtfs_cs_strcpy((byte *) &(sixty_four_bit_volumes[i].basename[0]), basename, CS_CHARSET_NOT_UNICODE);
            return(i);
        }
    }
    return(-1);
}

int open_64bit_volume(int unit, char *basename)
{
dword i;
char segment_file_name[256];

    sixty_four_bit_volumes[unit].num_segments = 0;
    for (i = 0;;i++)
    {
        HOSTDISK_SPRINTF(segment_file_name,"%s_SEGMENT_%x.HDK", basename,(unsigned int)i);
        /* reopen the file segments */
        sixty_four_bit_volumes[unit].segment_handles[i] =
#ifdef RTFS_WINDOWS
                CreateFile(TEXT(segment_file_name),    // file to open
                   GENERIC_READ|GENERIC_WRITE,
                   FILE_SHARE_READ|FILE_SHARE_WRITE,    // share for reading
                   NULL,                  // default security
                   OPEN_EXISTING,         // existing file only
#if(BUFFER_VIRTUAL_DISK_IO)
                   FILE_ATTRIBUTE_NORMAL,
#else
                   FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH|FILE_FLAG_NO_BUFFERING,
#endif
                   NULL);                 // no attr. template

        if (sixty_four_bit_volumes[unit].segment_handles[i] == INVALID_HANDLE_VALUE)
#endif
#ifdef RTFS_LINUX
        		open((char*)segment_file_name, O_RDWR/*|O_BINARY*/, S_IREAD | S_IWRITE);

        if (sixty_four_bit_volumes[unit].segment_handles[i] < 0)
#endif
#ifdef RTFS_STDIO
        		fopen((char*)segment_file_name, "rb+");
         if (!sixty_four_bit_volumes[unit].segment_handles[i])
#endif
        {
            if (i == 0)
                return(-1); /* Nothing there */
            else
			{
#if(USE_SHADOWRAM)
			if (shadow_in_ram)
				malloc_64bit_volume(unit,size_64bit_volume(unit), TRUE, 0);
#endif
             return(0);
			}
        }
        else
            sixty_four_bit_volumes[unit].num_segments += 1;
    }
}

int create_64bit_volume(int unit, char *basename, dword nblocks, BOOLEAN fill_file, byte fill_pattern)
{
dword i;
char segment_file_name[256];

	printf("Creating a host disk volume of %d sectors. (may take some time)\n", nblocks);

    sixty_four_bit_volumes[unit].num_segments = (nblocks + BLOCKS_PER_GIG - 1)/BLOCKS_PER_GIG;

#if(USE_SHADOWRAM)
	if (shadow_in_ram)
		malloc_64bit_volume(unit, nblocks, TRUE, 0);
#endif
    for (i = 0 ;i < (dword)sixty_four_bit_volumes[unit].num_segments; i++)
    {
        HOSTDISK_SPRINTF(segment_file_name,"%s_SEGMENT_%x.HDK",basename, (unsigned int)i);
        sixty_four_bit_volumes[unit].segment_handles[i] =
#ifdef RTFS_WINDOWS
                CreateFile(TEXT(segment_file_name),    // file to open
                   GENERIC_READ|GENERIC_WRITE,
                   FILE_SHARE_READ|FILE_SHARE_WRITE,       // share for reading
                   NULL,                  // default security
                   CREATE_ALWAYS,
                   FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH|FILE_FLAG_NO_BUFFERING,
                   NULL);                 // no attr. template

        if (sixty_four_bit_volumes[unit].segment_handles[i] == INVALID_HANDLE_VALUE)
                return(-1);
#endif
#ifdef RTFS_LINUX
				open((char*)segment_file_name,O_RDWR|/*O_BINARY|*/O_CREAT|O_TRUNC, S_IREAD | S_IWRITE);
        		if (sixty_four_bit_volumes[unit].segment_handles[i] < 0)
                	return(-1);
#endif
#ifdef RTFS_STDIO
        		fopen((char*)segment_file_name, "wb+");
         if (!sixty_four_bit_volumes[unit].segment_handles[i])
            return(-1);
#endif
    }
    if (fill_file)
    {
        dword block, blocks_left;
        byte *buff;

        buff = (byte *) rtfs_port_malloc(0x10000);
        rtfs_memset(buff, fill_pattern, 0x10000);
        block = 0;
        blocks_left = nblocks;
        while (blocks_left)
        {
        word nblocks = 128;
            if (blocks_left < 128)
                nblocks = (word) blocks_left;
            if (!hostdisk_io_64(unit, block, buff, nblocks, FALSE))
                return(FALSE);
            blocks_left -= nblocks;
            block += nblocks;
        }
        rtfs_port_free(buff);
    }

    return(0);
}


void close_64bit_volume(int unit)
{
dword i;
    for (i = 0 ;i < (dword)sixty_four_bit_volumes[unit].num_segments; i++)
#ifdef RTFS_WINDOWS
        CloseHandle(sixty_four_bit_volumes[unit].segment_handles[i]);
#endif
#ifdef RTFS_LINUX
		close(sixty_four_bit_volumes[unit].segment_handles[i]);
#endif
#ifdef RTFS_STDIO
		fclose(sixty_four_bit_volumes[unit].segment_handles[i]);
#endif
}

void shutdown_64bit_volume(int unit)
{
    /* Close the file */
    if (sixty_four_bit_volumes[unit].num_segments)
        close_64bit_volume(unit);
    /* Make the unit available again */
    sixty_four_bit_volumes[unit].num_segments = 0;
}

static dword size_64bit_volume(int unit)
{
dword size, s;

    if (!sixty_four_bit_volumes[unit].num_segments)
        return(0);

    size = (sixty_four_bit_volumes[unit].num_segments-1) * BLOCKS_PER_GIG;
#ifdef RTFS_WINDOWS
    s = SetFilePointer(sixty_four_bit_volumes[unit].segment_handles[sixty_four_bit_volumes[unit].num_segments-1],
            0,0, FILE_END);
    if (s == INVALID_SET_FILE_POINTER)
        return(0);
#endif
#ifdef RTFS_LINUX
	s = lseek(sixty_four_bit_volumes[unit].segment_handles[sixty_four_bit_volumes[unit].num_segments-1],
			0, SEEK_END);
    if (s < 0)
        return(0);
#endif
#ifdef RTFS_STDIO
      if (fseek(sixty_four_bit_volumes[unit].segment_handles[sixty_four_bit_volumes[unit].num_segments-1],
      0, SEEK_END ) == 0)
      {
      long l;
        l = ftell(sixty_four_bit_volumes[unit].segment_handles[sixty_four_bit_volumes[unit].num_segments-1]);
        if (l < 0)
        {
            printf("ftell failed\n");
            return(0);
        }
        else
            s = (dword) l;
      }
      else
      {
        printf("seek end failed\n");
        return(0);
      }

#endif
    size += s/HOSTDISK_SECTORSIZEBYTES;
    return((dword)size);
}




BOOLEAN hostdisk_io_64(int unit, dword block, void  *buffer, word _count, BOOLEAN reading) /*__fn__*/
{
dword segment_number;
dword max_count,block_offset,count;
dword ltemp;
#ifdef RTFS_WINDOWS
dword s;
HANDLE hFile;
#endif
#ifdef RTFS_LINUX
int fd;
#endif
dword nbytes,nblocks;
byte *bbuffer;


#if (0)
    if (reading)
        printf("Reading block == %d count == %d buffer %X\n", block, _count, (dword) buffer);
    else
        printf("Writing block == %d count == %d buffer %X\n", block, _count, (dword) buffer);
#endif
    bbuffer = (byte *) buffer;

    count = (dword) _count;
    while (count)
    {
        segment_number = block/BLOCKS_PER_GIG;
        block_offset = block%BLOCKS_PER_GIG;
#ifdef RTFS_WINDOWS
        hFile = sixty_four_bit_volumes[unit].segment_handles[segment_number];
        ltemp = (dword)(block_offset * HOSTDISK_SECTORSIZEBYTES);
        s = SetFilePointer(hFile,
            ltemp,0, FILE_BEGIN);
        if (s == INVALID_SET_FILE_POINTER)
            return(FALSE);
		if (s != ltemp)
        {
            return(FALSE);
        }
#endif
#ifdef RTFS_LINUX
		fd = sixty_four_bit_volumes[unit].segment_handles[segment_number];
        ltemp = (long)(block_offset * HOSTDISK_SECTORSIZEBYTES);

        if (lseek(fd, ltemp, SEEK_SET) != ltemp)
		{
			return(FALSE);
		}
#endif
#ifdef RTFS_STDIO
      ltemp = (long)(block_offset * HOSTDISK_SECTORSIZEBYTES);
      if (fseek(sixty_four_bit_volumes[unit].segment_handles[sixty_four_bit_volumes[unit].num_segments-1],
      (int) ltemp, SEEK_SET) != 0)
      {
            printf("fseek failed during IO to %d\n", ltemp);
			return(FALSE);
      }
#endif
        max_count = (BLOCKS_PER_GIG - block_offset);
        if (count > max_count)
        {
            nblocks = max_count;
        }
        else
        {
            nblocks = (int) count;
        }

        block += (dword)nblocks;
        count -= (dword)nblocks;
        nbytes = nblocks*HOSTDISK_SECTORSIZEBYTES;

#ifdef RTFS_WINDOWS
#if(USE_SHADOWRAM)
		if (shadow_in_ram && segment_number < NUM_SHADOW_SEGMENTS)
		{
			if (reading)
				memcpy(bbuffer,sixty_four_bit_volumes[unit].memory_handles[segment_number]+(block_offset * HOSTDISK_SECTORSIZEBYTES),nbytes);
		else
				memcpy(sixty_four_bit_volumes[unit].memory_handles[segment_number]+(block_offset * HOSTDISK_SECTORSIZEBYTES),bbuffer,nbytes);
		}
		else
#endif
		{
        if (reading)
        {
        dword nread;
            if (!ReadFile(hFile,bbuffer,nbytes,&nread,0) ||    nread != nbytes)
                return(FALSE);
        }
        else
        {
        dword nwrote;
            if (!WriteFile(hFile,bbuffer,nbytes,&nwrote,0) || nwrote != nbytes)
			{
    // Retrieve the system error message for the last-error code
				LPVOID lpMsgBuf;
				LPVOID lpDisplayBuf;
				DWORD dw = GetLastError();

			    FormatMessage(
					FORMAT_MESSAGE_ALLOCATE_BUFFER |
					FORMAT_MESSAGE_FROM_SYSTEM |
					FORMAT_MESSAGE_IGNORE_INSERTS,
					NULL,
					dw,
					MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
					(LPTSTR) &lpMsgBuf,
					0, NULL );

					printf("Error: %s\n", lpMsgBuf);
				   // Display the error message and exit the process
				    LocalFree(lpMsgBuf);
                return(FALSE);
			}
        }
		}
#endif
#ifdef RTFS_LINUX
        if (reading)
        {
        dword nread;
			if ((nread = read(fd,bbuffer,nbytes)) != nbytes)
			{
				return(FALSE);
			}
        }
        else
        {
        	int dup_fd;
			if (write(fd,bbuffer,(int)nbytes) != (int)nbytes)
			{
				return(FALSE);
			}

			/* Flush the file after each write */
			if (flush_all_writes)
			{
				dup_fd = dup(fd);
				if (dup_fd >= 0)
					close(dup_fd);
			}
        }
#endif
#ifdef RTFS_STDIO
        if (reading)
        {
            if (fread((void *)bbuffer, (size_t) 512, nblocks, sixty_four_bit_volumes[unit].segment_handles[segment_number])!= nblocks)
            {
                printf("Hostdisk Read failure nblocks == %d \n", nblocks);
                return(FALSE);
            }
        }
        else
        {
            if (fwrite((void *)bbuffer, (size_t) 512, nblocks, sixty_four_bit_volumes[unit].segment_handles[segment_number])!= nblocks)
            {
                printf("Hostdisk Write failure nblocks == %d \n", nblocks);
                return(FALSE);
            }
            fflush(sixty_four_bit_volumes[unit].segment_handles[segment_number]);
        }
#endif
        bbuffer += nbytes;
    }
    return(TRUE);
}



static int format_hostdisk(int logical_unit_number, byte *basename, PDEV_GEOMETRY pgc)
{
    /* Fill the file with zeroes */
    dword l;
    int fd, answer;
    byte buf[64];


    fd = logical_unit_number;
    /* The file should be open already */
    if (fd < 0)
        return(-1);
    l = 0;

    answer = '1';
    do
    {
        if (answer != '1')
        {
            rtfs_kern_puts((byte *)"I didn't understand your choice.\n");
        }
        /* Here we want to prompt the user so that he or she can
        change the size of the hostdisk to several presets. */
        rtfs_kern_puts((byte *)"What size do you want the hostdisk to be?\n");
        rtfs_kern_puts((byte *)"    1) FAT12 (4M)\n"); /* 8192 blocks */
        rtfs_kern_puts((byte *)"    2) FAT16 (16M)\n"); /* 32768 blocks */
        rtfs_kern_puts((byte *)"    3) FAT32 (1G)\n"); /* 2097152 blocks */
        rtfs_kern_puts((byte *)"    4) Custom\n");
        rtfs_kern_puts((byte *)"    5) FAT32 (16G)\n"); /* 33554432 blocks */
        rtfs_kern_puts((byte *)": ");
        rtfs_kern_gets(buf);
        if (strlen((char*)buf) == 1)
            answer = buf[0];
        else
            answer = 'X'; /* wrong */
    }  while (answer < '1' || answer > '5');

    if (answer == '4')
    {
        l = 1;
        do
        {
            if (l == 0)
            {
                rtfs_kern_puts((byte *)"I didn't understand your choice.\n");
            }
            rtfs_kern_puts((byte *)"How many megabytes would you like the hostdisk to contain :");
            rtfs_kern_gets(buf);
            l = atoi((char*)buf);
        }  while (l == 0);
        l *= 2048;
    }
    switch (answer)
    {
        case '1': l = 8192; break;
        case '2': l = 32768; break;
        case '3': l = 2097152; break;
        case '5': l = 33554432; break;
        case '4': /* fall through */
        default: /* shouldn't happen */
            break;
    }
    HOSTDISK_SPRINTF((char *)buf,"Making disk %iM.\n", l/2048);
    rtfs_kern_puts(buf);
    close_64bit_volume(fd);

    if (create_64bit_volume(fd, (char *)basename, l, TRUE, 0) != 0)
        return(-1);

    /* Update caller's idea of geometry */
    pgc->dev_geometry_lbas = l;

    return(0);
}


#define USE_DSPBIOS_SIMULATION                 0 /* Set to one to use block media confgiguration instead of BLK_DEV_VIRT_device_configure et al */
int FSADAPTMEM_device_configure_media(struct rtfs_media_insert_args *pmedia_parms, struct rtfs_media_resource_reply *pmedia_config_block, int sector_buffer_required);
int FSADAPTMEM_device_configure_volume(struct rtfs_volume_resource_request *prequest_block, struct rtfs_volume_resource_reply *pvolume_config_block);

#define NANDSIM_MEDIASIZE_KBYTES			    0x100000 /* 0x100000 sectors * 0x400 bytes per kbyte = 0x40000000 bytes == 1 GIG */

#if (USE_DSPBIOS_SIMULATION)
#include "..\..\rtfstargets\dspbios\fsadapt\fsadaptmem.h"
#define NANDSIMTYPE								RTFS_FSADAPTMEM_DEVICE_TYPE_NAND
#define HOSTDISKTYPE							RTFS_FSADAPTMEM_DEVICE_TYPE_MMC   /* or RTFS_FSADAPTMEM_DEVICE_TYPE_USB */
#define NANDSIM_ERASEBLOCK_SIZE_SECTORS         NAND_CFG_ERASEBLOCK_SIZE
#define NANDSIM_SECTORSIZE						NAND_CFG_SECTORSIZE

#else
#define NANDSIMTYPE								1
#define HOSTDISKTYPE							2
#define SIMULATE_MLC 1
#if (SIMULATE_MLC)
#define NANDSIM_SECTORSIZE                     2048
#define NANDSIM_ERASEBLOCK_SIZE_SECTORS          64
#else
#define NANDSIM_SECTORSIZE                      512
#define NANDSIM_ERASEBLOCK_SIZE_SECTORS          32
#endif
#endif


#define NANDSIM_SECTORBUFFER_SIZE			NANDSIM_ERASEBLOCK_SIZE_SECTORS
#define NANDSIM_MEDIASIZE_SECTORS			(NANDSIM_MEDIASIZE_KBYTES/NANDSIM_SECTORSIZE) * 1024

#define HOSTDISK_SECTORBUFFER_SIZE			64


#define NANDSIM_FILE_NAME "NAND0"
#define NANDSIM_HEADS                           255
#define NANDSIM_SECPTRACK                        32



BOOLEAN BLK_DEV_nandsim_Mount(void);
static struct rtfs_media_insert_args nandsim_media_parms;   /* Media parameter structure intitialized by BLK_DEV_nandsim_Mount() */

BOOLEAN BLK_DEV_hostdisk_Mount(void);
static struct rtfs_media_insert_args hostdisk_media_parms; /* Media parameter structure intitialized by BLK_DEV_hostdisk_Mount() */

#if (INCLUDE_WINDEV)
//  #error BROKEN
#endif


int  BLK_DEV_VIRT_blkmedia_ioctl(void  *handle_or_drive, void *pdrive, int opcode, int iArgs, void *vargs);
static int  BLK_DEV_VIRT_device_configure_media	   (struct rtfs_media_insert_args *pmedia_parms, struct rtfs_media_resource_reply *media_config_block, int sector_buffer_required);
static int  BLK_DEV_VIRT_device_configure_volume   (struct rtfs_volume_resource_request *prequest_block, struct rtfs_volume_resource_reply *preply_block);

/* IO routine installed by BLK_DEV_hostdisk_Mount */
static int BLK_DEV_HD_blkmedia_io(void  *devhandle, void *pdrive, dword sector, void  *buffer, dword count, BOOLEAN reading)
{
    RTFS_ARGSUSED_PVOID(devhandle);
    RTFS_ARGSUSED_PVOID(pdrive);
    return(hostdisk_io_64(HOSTDISK_HOSTDISK_UNIT, sector, buffer, (word)count, reading));
}
/* Call after Rtfs is intialized to start a host disk driver on C: */
BOOLEAN BLK_DEV_hostdisk_Mount(void)
{

    /* re-open or create a hostdisk file to hold contain the simulation */
    if (open_64bit_volume(HOSTDISK_HOSTDISK_UNIT,(char *)HOSTDISK_FILE_NAME) < 0)
    {
        DEV_GEOMETRY gc;
        if (format_hostdisk(HOSTDISK_HOSTDISK_UNIT, (byte *)HOSTDISK_FILE_NAME, &gc) < 0)
            return(FALSE);
    }

    /* Set up mount parameters and call Rtfs to mount the device    */

    /* register with Rtfs File System */
    hostdisk_media_parms.devhandle = (void *) &hostdisk_media_parms;
    hostdisk_media_parms.device_type = HOSTDISKTYPE;	/* not used because private versions of configure and release, but must be non zero */
    hostdisk_media_parms.unit_number = 0;
    hostdisk_media_parms.media_size_sectors = size_64bit_volume(HOSTDISK_HOSTDISK_UNIT);
    hostdisk_media_parms.numheads = (dword) 16;
    hostdisk_media_parms.secptrk  = (dword) 63;
    hostdisk_media_parms.numcyl   = (dword) (hostdisk_media_parms.media_size_sectors/(hostdisk_media_parms.numheads * hostdisk_media_parms.secptrk));
    if (hostdisk_media_parms.numcyl > 1023)
        hostdisk_media_parms.numcyl = 1023;

    hostdisk_media_parms.sector_size_bytes =  (dword) 512;
    hostdisk_media_parms.eraseblock_size_sectors =    0;
    hostdisk_media_parms.write_protect =           0;

    hostdisk_media_parms.device_io                =  BLK_DEV_HD_blkmedia_io;
    hostdisk_media_parms.device_ioctl             =  BLK_DEV_VIRT_blkmedia_ioctl;
    hostdisk_media_parms.device_erase             =  0;
#if (USE_DSPBIOS_SIMULATION)
    hostdisk_media_parms.device_configure_media   =  FSADAPTMEM_device_configure_media;
    hostdisk_media_parms.device_configure_volume  =  FSADAPTMEM_device_configure_volume;
#else
    hostdisk_media_parms.device_configure_media   =  BLK_DEV_VIRT_device_configure_media;
    hostdisk_media_parms.device_configure_volume  =  BLK_DEV_VIRT_device_configure_volume;
#endif


    /* Provide a sector buffer for system operations like reading partition table, also used for multisector IO */
    if (pc_rtfs_media_insert(&hostdisk_media_parms) < 0)
    	return(FALSE);
	else
    	return(TRUE);
}


/* =================================================================================================== */
/* Used internally to erase sectors */
static byte erase_buffer[NANDSIM_ERASEBLOCK_SIZE_SECTORS * NANDSIM_SECTORSIZE];
static int BLK_DEV_NS_blkmedia_io(void  *devhandle, void *pdrive, dword sector, void  *buffer, dword count, BOOLEAN reading);
static int BLK_DEV_NS_blkmedia_erase_sectors(void  *devhandle, void *pdrive, dword start_sector, dword n_sectors);

/* Call after Rtfs is intialized to start a nand simulator on drive G: */
BOOLEAN BLK_DEV_nandsim_Mount(void)
{

	/* re-open or create a hostdisk file to hold contain the simulation */
    if (open_64bit_volume(NANDSIM_HOSTDISK_UNIT,(char *)NANDSIM_FILE_NAME) < 0)
    {
    dword media_size;
        /* Create and fill a 64 bit volume of this size and fill it with FF */
        media_size = NANDSIM_MEDIASIZE_KBYTES*2;
        if (create_64bit_volume(NANDSIM_HOSTDISK_UNIT, NANDSIM_FILE_NAME, media_size , TRUE, 0xff) != 0)
            return(FALSE);
    }

    /* Set up mount parameters and call Rtfs to mount the device    */

    /* register with Rtfs File System */
    nandsim_media_parms.devhandle = (void *) &nandsim_media_parms;
    nandsim_media_parms.device_type = NANDSIMTYPE;	/* not used because private versions of configure and release, but must be non zero */
    nandsim_media_parms.unit_number = 0;
    nandsim_media_parms.media_size_sectors = (dword) NANDSIM_MEDIASIZE_SECTORS;
    nandsim_media_parms.numheads = NANDSIM_HEADS;
    nandsim_media_parms.secptrk  = NANDSIM_SECPTRACK;
    nandsim_media_parms.numcyl   = (nandsim_media_parms.media_size_sectors/(nandsim_media_parms.numheads * nandsim_media_parms.secptrk));
    if (nandsim_media_parms.numcyl > 1023)
        nandsim_media_parms.numcyl = 1023;

    nandsim_media_parms.sector_size_bytes 		= 	(dword) NANDSIM_SECTORSIZE;
    nandsim_media_parms.eraseblock_size_sectors = 	NANDSIM_SECTORBUFFER_SIZE;
    nandsim_media_parms.write_protect 			= 	0;

    nandsim_media_parms.device_io                =  BLK_DEV_NS_blkmedia_io;
    nandsim_media_parms.device_ioctl             =  BLK_DEV_VIRT_blkmedia_ioctl;
    nandsim_media_parms.device_erase             =  BLK_DEV_NS_blkmedia_erase_sectors;

#if (USE_DSPBIOS_SIMULATION)
    nandsim_media_parms.device_configure_media   =  FSADAPTMEM_device_configure_media;
    nandsim_media_parms.device_configure_volume  =  FSADAPTMEM_device_configure_volume;
#else
    nandsim_media_parms.device_configure_media   =  BLK_DEV_VIRT_device_configure_media;
    nandsim_media_parms.device_configure_volume  =  BLK_DEV_VIRT_device_configure_volume;
#endif


    /* Provide a sector buffer for system operations like reading partition table, also used for multisector IO */
    if (pc_rtfs_media_insert(&nandsim_media_parms) < 0)
    	return(FALSE);
	else
		return(TRUE);
}

static int BLK_DEV_NS_blkmedia_io(void  *devhandle, void *pdrive, dword sector, void  *buffer, dword count, BOOLEAN reading)
{
    dword media_sector;
    word media_count;

    RTFS_ARGSUSED_PVOID(devhandle);
    RTFS_ARGSUSED_PVOID(pdrive);

    media_sector = sector * (NANDSIM_SECTORSIZE/512);
    media_count =  (word) (count * ((NANDSIM_SECTORSIZE/512)));
    return(hostdisk_io_64(NANDSIM_HOSTDISK_UNIT, media_sector, buffer, media_count, reading));
}


static int BLK_DEV_NS_blkmedia_erase_sectors(void  *devhandle, void *pdrive, dword start_sector, dword n_sectors)
{
dword media_sector;
int mediasec_persector,mediasec_pereblock;

    RTFS_ARGSUSED_PVOID(devhandle);
    RTFS_ARGSUSED_PVOID(pdrive);
    mediasec_persector = (NANDSIM_SECTORSIZE/512);
    mediasec_pereblock = (NANDSIM_ERASEBLOCK_SIZE_SECTORS * NANDSIM_SECTORSIZE/512);
    media_sector = start_sector * mediasec_persector;
    rtfs_memset(erase_buffer, 0xff, NANDSIM_ERASEBLOCK_SIZE_SECTORS*NANDSIM_SECTORSIZE);
    while (n_sectors >= NANDSIM_ERASEBLOCK_SIZE_SECTORS)
    {
    	hostdisk_io_64(NANDSIM_HOSTDISK_UNIT, media_sector, erase_buffer, (word)mediasec_pereblock, FALSE);
        n_sectors -= NANDSIM_ERASEBLOCK_SIZE_SECTORS;
        media_sector += mediasec_pereblock;
    }
    return(1);
}

/* ======================================== */



static int BLK_DEV_VIRT_device_format(void  *devhandle);


int  BLK_DEV_VIRT_blkmedia_ioctl(void  *devhandle, void *pdrive, int opcode, int iArgs, void *vargs)
{
    RTFS_ARGSUSED_PVOID(pdrive);
    RTFS_ARGSUSED_INT(iArgs);
    RTFS_ARGSUSED_PVOID(vargs);

    switch(opcode)
    {
        case RTFS_IOCTL_FORMAT:
        	return(BLK_DEV_VIRT_device_format(devhandle));
		case RTFS_IOCTL_INITCACHE:
		case RTFS_IOCTL_FLUSHCACHE:
			break;
    }
    return(0);

}

static int BLK_DEV_VIRT_device_format(void  *devhandle)
{
    if (devhandle == (void *) &nandsim_media_parms)
    {
        dword media_size;
        media_size = NANDSIM_MEDIASIZE_SECTORS * (NANDSIM_SECTORSIZE/512);
        /* Create and fill a 64 bit volume of this size and fill it with FF */
        if (create_64bit_volume(NANDSIM_HOSTDISK_UNIT, NANDSIM_FILE_NAME, media_size , TRUE, 0xff) != 0)
            return(-1);
        else
            return(0);
    }
    else if (devhandle == (void *) &hostdisk_media_parms)
    {
        DEV_GEOMETRY gc;
        return(format_hostdisk(HOSTDISK_HOSTDISK_UNIT, (byte *) HOSTDISK_FILE_NAME, &gc));
    }
    else   /* Windev device does not require formatting */
        return(0);
}


static int BLK_DEV_VIRT_device_configure_media(struct rtfs_media_insert_args *pmedia_parms, struct rtfs_media_resource_reply *pmedia_config_block, int sector_buffer_required)
{
    if (pmedia_parms->device_type == NANDSIMTYPE)
	{
    	rtfs_print_one_string((byte *)"Attaching a NAND simulator driver at drive G:", PRFLG_NL);

		pmedia_config_block->requested_driveid = (int) ('G'-'A');
		pmedia_config_block->requested_max_partitions =  2;
		pmedia_config_block->use_fixed_drive_id = 1;

		if (sector_buffer_required)
			pmedia_config_block->device_sector_buffer_size_bytes = pmedia_parms->sector_size_bytes * pmedia_parms->eraseblock_size_sectors;
	}
    else if (pmedia_parms->device_type == HOSTDISKTYPE)
	{
    	rtfs_print_one_string((byte *)"Attaching a fixed disk simulator driver at drive C:", PRFLG_NL);
		pmedia_config_block->requested_driveid = (int) ('C'-'A');
		pmedia_config_block->requested_max_partitions =  4;
		pmedia_config_block->use_fixed_drive_id = 1;

		if (sector_buffer_required)
			pmedia_config_block->device_sector_buffer_size_bytes = pmedia_parms->sector_size_bytes * HOSTDISK_SECTORBUFFER_SIZE;
	}
	else
		return(-1);
	/* Dynamically allocating so use Rtfs helper function */
	pmedia_config_block->use_dynamic_allocation = 1;

    /*	0  if successful
		-1 if unsupported device type
		-2 if out of resources
	*/
	return(0);
}

#define DEFAULT_NAND_OPERATING_POLICY    			0
#define DEFAULT_NAND_NUM_SECTOR_BUFFERS  			4
#define DEFAULT_NAND_NUM_FILE_BUFFERS    			2
#define DEFAULT_NAND_NUM_FAT_BUFFERS     			1

#define DEFAULT_OPERATING_POLICY    				0
#define DEFAULT_NUM_SECTOR_BUFFERS 					10
#define DEFAULT_NUM_FAT_BUFFERS     				2
#define DEFAULT_FATBUFFER_PAGESIZE_SECTORS  		8

#define DEFAULT_NUM_FILE_BUFFERS    				0
#define DEFAULT_FILE_BUFFER_SIZE_SECTORS 			0

#define DEFAULT_FAILSAFE_RESTORE_BUFFERSIZE_SECTORS 64
#define DEFAULT_NUM_FAILSAFE_BLOCKMAPS     			1024
#define DEFAULT_FAILSAFE_INDEX_BUFFERSIZE_SECTORS	128 // 2
#if (INCLUDE_FAILSAFE_CODE)
/* In evaluation mode this flag instructs host disk and host dev device drivers whether to
   autoenable falisafe */
extern BOOLEAN auto_failsafe_mode;
#endif
static int BLK_DEV_VIRT_device_configure_volume(struct rtfs_volume_resource_request *prequest_block, struct rtfs_volume_resource_reply *pvolume_config_block)
{
    if (prequest_block->device_type == NANDSIMTYPE)
	{
    	pvolume_config_block->drive_operating_policy 			= DEFAULT_NAND_OPERATING_POLICY;
    	pvolume_config_block->n_sector_buffers 					= DEFAULT_NAND_NUM_SECTOR_BUFFERS;
    	pvolume_config_block->n_fat_buffers    					= DEFAULT_NAND_NUM_FAT_BUFFERS;
    	pvolume_config_block->fat_buffer_page_size_sectors  	= prequest_block->eraseblock_size_sectors;
    	pvolume_config_block->n_file_buffers 					= DEFAULT_NAND_NUM_FILE_BUFFERS;
    	pvolume_config_block->file_buffer_size_sectors 			= prequest_block->eraseblock_size_sectors;
        if (prequest_block->failsafe_available)
    	{
			pvolume_config_block->fsindex_buffer_size_sectors 	= prequest_block->eraseblock_size_sectors;
        	pvolume_config_block->fsrestore_buffer_size_sectors = prequest_block->eraseblock_size_sectors;
        	pvolume_config_block->fsjournal_n_blockmaps 		= DEFAULT_NUM_FAILSAFE_BLOCKMAPS;
    	}
	}
    else if (prequest_block->device_type == HOSTDISKTYPE)
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
        	pvolume_config_block->fsindex_buffer_size_sectors 	= DEFAULT_FAILSAFE_INDEX_BUFFERSIZE_SECTORS;
        	pvolume_config_block->fsrestore_buffer_size_sectors = DEFAULT_FAILSAFE_RESTORE_BUFFERSIZE_SECTORS;
        	pvolume_config_block->fsjournal_n_blockmaps 		= DEFAULT_NUM_FAILSAFE_BLOCKMAPS;
    	}
	}
	/* Tell Rtfs to dynamically allocate buffers */
	pvolume_config_block->use_dynamic_allocation = 1;
	return(0);
}


#endif /* (INCLUDE_HOSTDISK || INCLUDE_SIMULATOR) */

#if (!INCLUDE_HOSTDISK)
BOOLEAN BLK_DEV_hostdisk_Mount(void)
{
	printf("Host disk is disabled, define \"INCLUDE_HOSTDISK\" in portconf.h\n");
	return(FALSE);
}

BOOLEAN BLK_DEV_nandsim_Mount(void)
{
	printf("Nand simulator is disabled, define \"INCLUDE_HOSTDISK\" in portconf.h\n");
	return(FALSE);
}

#endif


#ifdef RTFS_WINDOWS
#if (INCLUDE_HOSTDISK)

//#include <conio.h>
//#include <dos.h>
#include <io.h>
#include <direct.h>
#include <string.h>

/* Utilities for traversing a windows subtree */

int get_sizes = 0;
int do_import = 0;

unsigned long dircount;
unsigned long filecount;
unsigned long long bytecount;
unsigned long sizes[3]; // 512, 1024, 2048
unsigned long root_sizes[3]; // 512, 1024, 2048

unsigned long rounduptocl(unsigned long l, unsigned long clsize)
{
unsigned long up;
	up = (l + clsize -1);
	if (up < l)
		up = l;
	else
		up = (up/clsize);
	return(up);
}

void add_subdirectory(int segs, int level)
{
	if (get_sizes)
	{
		unsigned long l = ((segs + 31)/32)*32;
		dircount += 1;
		if (level == 0)
		{
		root_sizes[0] += rounduptocl(l, 512);
		root_sizes[1] += rounduptocl(l, 1024);
		root_sizes[2] += rounduptocl(l, 2048);
		}
		sizes[0] += rounduptocl(l, 512);
		sizes[1] += rounduptocl(l, 1024);
		sizes[2] += rounduptocl(l, 2048);
	}
}

void add_file(unsigned long l)
{
	if (get_sizes)
	{
		filecount += 1;
		bytecount += l;
		sizes[0] += rounduptocl(l, 512);
		sizes[1] += rounduptocl(l, 1024);
		sizes[2] += rounduptocl(l, 2048);
	}
}

void recommend(void)
{
unsigned long ltotal_clusters,fat_size_sectors,root_size_sectors,bits,disk_size_sectors;

	if (sizes[0]-root_sizes[0] <= 4084L)
	{
		ltotal_clusters = sizes[0]-root_sizes[0];
		fat_size_sectors = 2 * ((ltotal_clusters + 341)/341);
		root_size_sectors = root_sizes[0]; /* since clusters equal sectors */
		bits = 12;
	}
	else if (sizes[0]-root_sizes[0] <= 65524L)
	{
		ltotal_clusters = sizes[0]-root_sizes[0];
		fat_size_sectors = 2 * ((ltotal_clusters + 255)/256);
		root_size_sectors = root_sizes[0]; /* since clusters equal sectors */
		bits = 16;
	}
    else
	{
		ltotal_clusters = sizes[0];
		fat_size_sectors = 2 * ((ltotal_clusters + 127)/128);
		root_size_sectors = 0;
        bits = 32;
	}

	disk_size_sectors = fat_size_sectors + root_size_sectors+ltotal_clusters;
	printf("Format FAT (%d) secpfat (1) Root sectors (%d) disk_size %d\n ", bits, root_size_sectors, disk_size_sectors);
}

void summarize(char *pathname)
{
	printf("For ====================== %s\n", pathname);

	printf("Files   == %d\n", filecount);
	printf("Subdirs == %d\n", dircount);
	printf("Bytes == %8I64u\n", bytecount);


	printf("Clusters   512 == %d\n", sizes[0]);
	printf("Clusters  1024 == %d\n", sizes[1]);
	printf("Clusters  2048 == %d\n", sizes[2]);

	recommend();
}
char *rootpath = 0;

#define FILECOPYSIZE 131072 /* Copy in 128 K chunks */
int	copy_winfile_to_rtfs(unsigned char *rtfspath, unsigned char *winpath)
{
byte *buf;
int  fd;
int res,res2;
int fi;


	printf("copying %s to rtfs %s\n", winpath,  rtfspath);

    if ( (fi = _open((char *)winpath,O_RDONLY|O_BINARY)) < 0)    /* Open binary read */
    {
        printf("Cant open %s\n",winpath);
        return -1;
    }
	buf = (byte *)malloc(FILECOPYSIZE);
#if (1)
	buf[0] = 0;
	if (rootpath)
		strcpy((char *)buf,rootpath);
	strcat((char *)buf,(char *)"\\");
	strcat((char *)buf,(char *)rtfspath);
    printf("Copying to Rtfs file named %s\n",buf);
    if ((fd = po_open(buf,(PO_BINARY|PO_RDWR|PO_CREAT|PO_TRUNC),
                             (PS_IWRITE | PS_IREAD) ) ) < 0)
    {
        printf("Cant open %s error = %i\n",buf, -1);
        free(buf);
        return -1;
    }
#else
    if ((fd = po_open(rtfspath,(PO_BINARY|PO_RDWR|PO_CREAT|PO_TRUNC),
                             (PS_IWRITE | PS_IREAD) ) ) < 0)
    {
        printf("Cant open %s error = %i\n",rtfspath, -1);
        free(buf);
        return -1;
    }
#endif
    /* Read from host until EOF */
    while ( (res = _read(fi,buf,FILECOPYSIZE)) > 0)
    {
   		if ( (res2 = (int)po_write(fd,buf,res)) != res)
        {
        	printf("Cant write %x %x \n",res,res2);
		    po_close(fd);
			_close(fi);
        	free(buf);
            return -1;
        }
    }
    free(buf);
    po_close(fd);
    _close(fi);
	return(0);
}


int traverse(char *name, int level)
{
struct _finddata_t statobj;
unsigned char *pkind;
char path[512];
int  i,n_segs;
intptr_t dd;
static int root_name_pathlen = 0;
	n_segs = 0;

    for (i = 0; i < level; i++)
        printf(" ");
    printf("Processing directory %s\n", name);

    if (do_import)
	{
		if (level == 0)
		{
			root_name_pathlen = strlen(name)+1;
			if (rootpath)
			{
				if (!pc_set_cwd((byte*)rootpath))
				{
					printf("Error traversing root\n");
					return(-1);
				}
			}
			else
			{
				if (!pc_set_cwd((byte*)"\\"))
				{
					printf("Error traversing root\n");
					return(-1);
				}
			}
		}
		else
		{
#if (1)
	byte buf[512];
	buf[0] = 0;
	if (rootpath)
		strcpy((char *)buf,rootpath);
	strcat((char *)buf,(char *)"\\");
	strcat((char *)buf,(char *) name+root_name_pathlen);
    printf("Making directory named %s\n",buf);

		if (!pc_mkdir((unsigned char *) buf))
		{
			printf("Could not create subdir %s at level %d\n", buf, level);
			return(-1);
		}
		if (!pc_set_cwd((unsigned char *) buf))
		{
			printf("Error traversing path %s\n", buf);
			return(-1);
		}
#else

			if (!pc_mkdir((unsigned char *) name+root_name_pathlen))
			{
				printf("Could not create subdir %s at level %d\n", name+root_name_pathlen, level);
				return(-1);
			}
			if (!pc_set_cwd((unsigned char *) name+root_name_pathlen))
			{
				printf("Error traversing path %s\n", name+root_name_pathlen);
				return(-1);
			}
#endif

		}
	}

    strcpy((char *)path, (char *)name);
    strcat((char *)path, "\\*.*");

    /* Print them all */
    if ((dd = _findfirst((char *)path, &statobj)) < 0)
    {
        return(0);
    }
    else
    {
        do
        {

			n_segs += (strlen(statobj.name) + 12 )/13;

            if ( (strcmp(".", statobj.name)==0) ||
                 (strcmp("..", statobj.name)==0)  )
                pkind = (unsigned char *)"d";
            else if( statobj.attrib & _A_SUBDIR )
            {
				char filepath[512];
				filepath[0] = 0;
			    strcat(filepath, name);
			    strcat(filepath, "\\");
			    strcat(filepath, statobj.name);
				printf("DIR[%s]\n",filepath);
                pkind = (unsigned char *)"d";
            }
            else
            {

				add_file(statobj.size);
				{
					char filepath[512];
					filepath[0] = 0;
				    strcat(filepath, name);
				    strcat(filepath, "\\");
				    strcat(filepath, statobj.name);
					printf("file[%s]\n",filepath);
					if (do_import)
					{
						copy_winfile_to_rtfs((unsigned char *)filepath+root_name_pathlen, (unsigned char *)filepath);
					}

				}
                pkind = (unsigned char *)"-";
            }
            /* matches unix output. */
            for (i = 0; i < level; i++)
                printf(" ");
            printf("%s  %8ld  %-12s\n", pkind, statobj.size,statobj.name);


        } while (_findnext(dd, &statobj) == 0);
		add_subdirectory(n_segs, level);

        _findclose(dd);
    }

    /* Now traverse */
    if ((dd=_findfirst((char *)path, &statobj))<0)
    {
        return(0);
    }
    else
    {
        do
        {
            if( statobj.attrib & _A_SUBDIR )
            {
                if ( (strcmp(".", statobj.name)!=0) &&
                     (strcmp("..", statobj.name)!=0)  )
                {
                    strcpy((char *)path, (char *)name);
                    strcat((char *)path, "\\");
                    strcat((char *)path, statobj.name);
                    traverse(path, level+1);
                    strcpy((char *)path, (char *)name);
                    strcat((char *)path, "\\*.*");
                }
            }
        } while (_findnext(dd, &statobj) == 0);
        _findclose(dd);
    }
    return(1);
}

void scanwindir(unsigned char *winpath)
{
get_sizes = 1;
do_import = 0;
bytecount = 0;

	dircount = filecount = sizes[0] = root_sizes[0] = 0;
	traverse((char *) winpath, 0);
	summarize((char *) winpath);
}

void importwindir(unsigned char *winpath,unsigned char *rtfspath)
{
get_sizes = 0;
do_import = 1;

	rootpath = (char *) rtfspath;

	dircount = filecount = sizes[0] = root_sizes[0] = 0;
	traverse((char *) winpath, 0);
}


#endif

#if(USE_SHADOWRAM)
#if (0)
byte x_00[(1024*1024*1024)];
byte x_01[(1024*1024*1024)];
byte x_02[(1024*1024*1024)];
byte x_03[(1024*1024*1024)];
byte x_04[(1024*1024*1024)];
byte x_05[(1024*1024*1024)];
byte x_06[(1024*1024*1024)];
byte x_07[(1024*1024*1024)];
byte x_08[(1024*1024*1024)];
byte x_09[(1024*1024*1024)];
byte x_10[(1024*1024*1024)];
byte x_11[(1024*1024*1024)];
byte x_12[(1024*1024*1024)];
byte x_13[(1024*1024*1024)];
byte x_14[(1024*1024*1024)];
byte x_15[(1024*1024*1024)];
#endif
void malloc_64bit_volume(int unit, dword nblocks, BOOLEAN fill_file, byte fill_pattern)
{
dword i;
char segment_file_name[256];
 	shadow_in_ram=TRUE;
	printf("Creating a host disk memory image of %d sectors.\n", nblocks);
#if (0)
	sixty_four_bit_volumes[unit].memory_handles[ 0] = &x_00[0];
	sixty_four_bit_volumes[unit].memory_handles[ 1] = &x_01[0];
	sixty_four_bit_volumes[unit].memory_handles[ 2] = &x_02[0];
	sixty_four_bit_volumes[unit].memory_handles[ 3] = &x_03[0];
 	sixty_four_bit_volumes[unit].memory_handles[ 4] = &x_04[0];
 	sixty_four_bit_volumes[unit].memory_handles[ 5] = &x_05[0];
 	sixty_four_bit_volumes[unit].memory_handles[ 6] = &x_06[0];
 	sixty_four_bit_volumes[unit].memory_handles[ 7] = &x_07[0];
 	sixty_four_bit_volumes[unit].memory_handles[ 8] = &x_08[0];
 	sixty_four_bit_volumes[unit].memory_handles[ 9] = &x_09[0];
 	sixty_four_bit_volumes[unit].memory_handles[ 1] = &x_10[0];
 	sixty_four_bit_volumes[unit].memory_handles[ 11] = &x_11[0];
 	sixty_four_bit_volumes[unit].memory_handles[ 12] = &x_12[0];
 	sixty_four_bit_volumes[unit].memory_handles[ 13] = &x_13[0];
 	sixty_four_bit_volumes[unit].memory_handles[ 14] = &x_14[0];
 	sixty_four_bit_volumes[unit].memory_handles[ 15] = &x_15[0];
#endif
    sixty_four_bit_volumes[unit].num_segments = (nblocks + BLOCKS_PER_GIG - 1)/BLOCKS_PER_GIG;
    for (i = 0 ;i < (dword)NUM_SHADOW_SEGMENTS /* ixty_four_bit_volumes[unit].num_segments*/; i++)
    {
		printf("Allocating segment %d\n", i);
		sixty_four_bit_volumes[unit].memory_handles[i] = (byte *) malloc((nblocks/sixty_four_bit_volumes[unit].num_segments)*512);
		if (!sixty_four_bit_volumes[unit].memory_handles[i])
		{
			printf("Shadow ram allocation failed, disable shadow ram \n");
			shadow_in_ram=FALSE;
			return;
		}
		if (fill_file && i < MAX_ZERO_SEGMENT)
		{
			printf("Filling %d\n", i);
			rtfs_memset(sixty_four_bit_volumes[unit].memory_handles[i], fill_pattern,(nblocks/sixty_four_bit_volumes[unit].num_segments)*512);
		}
	}
}
#endif

#endif
