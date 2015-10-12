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


/* Duplicate the definition of RTFS_WINDOWS or RTFS_LINUX that was
   made in rtfsarch.h. This eliminates header file problems */
#ifndef _TMS320C6X
#define RTFS_WINDOWS
#endif
/* #define RTFS_LINUX */
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

#include "rtfs.h"
#include "portconf.h"   /* For included devices */

#if (INCLUDE_HOSTDISK)

#define HOSTDISK_SECTORSIZEBYTES 512  /* Must be 512 */


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
#define MAX_UNITS 8

struct file64 {
	char basename[255];
	int num_segments;

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
                   FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH|FILE_FLAG_NO_BUFFERING,
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
             return(0);
        }
        else
            sixty_four_bit_volumes[unit].num_segments += 1;
    }
}

int create_64bit_volume(int unit, char *basename, dword nblocks, BOOLEAN fill_file, byte fill_pattern)
{
dword i;
char segment_file_name[256];

    sixty_four_bit_volumes[unit].num_segments = (nblocks + BLOCKS_PER_GIG - 1)/BLOCKS_PER_GIG;
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

dword size_64bit_volume(int unit)
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
                return(FALSE);
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

/*
*
*   Perform io to and from the hostdisk.
*
*   If the reading flag is true copy data from the hostdisk (read).
*   else copy to the hostdisk. (write).
*
*/


BOOLEAN hostdisk_io(int driveno, dword block, void  *buffer, word count, BOOLEAN reading) /*__fn__*/
{
    DDRIVE *pdr;


    pdr = pc_drno_to_drive_struct(driveno);
    if (!pdr)
        return(FALSE);
    return(hostdisk_io_64(pdr->logical_unit_number, block, buffer, count, reading));
}

void _pc_check_media_parms(PDEV_GEOMETRY pgeometry);


int hostdisk_perform_device_ioctl(int driveno, int opcode, void * pargs)
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

            if (pdr->logical_unit_number < 0)
                return(-1);

            /* Now set the geometry section */
            /* This is a simple default condition. It is overriden by
            code in MKROM when the hostdisk is being populated from
            a disk sub-tree */
            gc.bytespsector = HOSTDISK_SECTORSIZEBYTES;          /* Sector size is 512 */
            gc.fmt_parms_valid = FALSE;
            /* Try to get the volume size in sectors. If none there return a default falue */
            gc.dev_geometry_lbas = size_64bit_volume(pdr->logical_unit_number);
            if (!gc.dev_geometry_lbas)
            {
                rtfs_kern_puts((byte *)" No hostdisk present defaulting to 20 mbyte simulated drive \n");
                gc.dev_geometry_lbas        =  20480; /* 20 meg */
            }
            copybuff(pargs, &gc, sizeof(gc));
            return (0);
        }

        case DEVCTL_FORMAT:
        {
            /* Fill the file with zeroes */
            dword l;
            int fd, answer;
            PDEV_GEOMETRY pgc;
            byte buf[64];

            pgc = (PDEV_GEOMETRY) pargs;

            fd = pdr->logical_unit_number;
            /* The file should be open already */
            if (fd < 0)
                return(-1);

            l = (dword) pgc->dev_geometry_heads;
            l = (dword) l * pgc->dev_geometry_cylinders;
            l = (dword) l * pgc->dev_geometry_secptrack;
            if (pgc->dev_geometry_lbas)
                l = pgc->dev_geometry_lbas;

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
                HOSTDISK_SPRINTF((char *)buf,"    4) Current (~%iM)\n", l/2048);
                rtfs_kern_puts((byte *)buf);
                rtfs_kern_puts((byte *)"    5) Custom\n");
                rtfs_kern_puts((byte *)"    6) FAT32 (16G)\n"); /* 33554432 blocks */
                rtfs_kern_puts((byte *)": ");
                rtfs_kern_gets(buf);
                if (strlen((char*)buf) == 1)
                    answer = buf[0];
                else
                    answer = 'X'; /* wrong */
            }
            while (answer < '1' || answer > '6');
            if (answer == '5')
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
                }
                while (l == 0);
                l *= 2048;
            }
            switch (answer)
            {
            case '1': l = 8192; break;
            case '2': l = 32768; break;
            case '3': l = 2097152; break;
            case '6': l = 33554432; break;
            case '4': /* fall through */
            case '5': /* fall through */
            default: /* shouldn't happen */
                break;
            }
            HOSTDISK_SPRINTF((char *)buf,"Making disk %iM.\n", l/2048);
            rtfs_kern_puts(buf);
            close_64bit_volume(fd);

            if (create_64bit_volume(fd, sixty_four_bit_volumes[pdr->logical_unit_number].basename, l, TRUE, 0) != 0)
                return(-1);

            /* Update caller's idea of geometry */
            pgc->dev_geometry_lbas = l;

            return(0);
        }
        break;
        case DEVCTL_REPORT_REMOVE:
            pdr->drive_flags &= ~DRIVE_FLAGS_INSERTED;
            return(0);
        case DEVCTL_CHECKSTATUS:
            if (pdr->drive_flags & DRIVE_FLAGS_INSERTED)
                return(DEVTEST_NOCHANGE);
            else
            {
                pdr->drive_flags |= DRIVE_FLAGS_INSERTED;
                return(DEVTEST_CHANGED);
            }
        case DEVCTL_WARMSTART:
        {
            flush_all_writes = TRUE; /* Enable flush after each write by default */
            pdr->logical_unit_number = alloc_64bit_unit((byte *) pargs);
            if (pdr->logical_unit_number < 0)
                return(-1);
            /* See if it already exists */
            if (open_64bit_volume(pdr->logical_unit_number,(char *)sixty_four_bit_volumes[pdr->logical_unit_number].basename) < 0)
            {
                pdr->drive_flags |= DRIVE_FLAGS_FORMAT;
                if (create_64bit_volume(pdr->logical_unit_number, sixty_four_bit_volumes[pdr->logical_unit_number].basename, DEFAULT_HOST_DISK_SIZE, TRUE, 0) != 0)
                    return(-1);
            }
            pdr->drive_flags |= (DRIVE_FLAGS_VALID|DRIVE_FLAGS_INSERTED|DRIVE_FLAGS_REMOVABLE);
            return(0);
        }
        case DEVCTL_SHUTDOWN:
            if (pdr->logical_unit_number >= 0)
                shutdown_64bit_volume(pdr->logical_unit_number);
            break;
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


#endif /* (INCLUDE_HOSTDISK) */
