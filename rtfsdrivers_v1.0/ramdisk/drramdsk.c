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
#include "portconf.h"   /* For included devices */
#if (INCLUDE_RAMDISK)

/* Define the size of your ram disk.
   PAGE_SIZE in 512 byte blocks times number of pages */
#define NUM_RAMDISK_CYLINDERS     64
#define NUM_RAMDISK_SECPTRACK     16
#define NUM_RAMDISK_HEADS         2

typedef struct block_alloc {
        byte    core[512];
        } BLOCK_ALLOC;

#define RAMDISK_TRACKSIZE (NUM_RAMDISK_SECPTRACK*NUM_RAMDISK_HEADS)
BLOCK_ALLOC ram_disk_pages[NUM_RAMDISK_CYLINDERS][RAMDISK_TRACKSIZE];



/* This routine converts a block offset to a memory pointer to the 512 bytes
   that contain the block */

static byte  *ramdisk_block(word block)                                       /*__fn__*/
{
int page_number;
int block_number;
dword ltemp;
    /* Get the page number */
    ltemp = block / RAMDISK_TRACKSIZE;
    page_number = (word) ltemp;
    /* Check. This should not happen */
    if (page_number >= NUM_RAMDISK_CYLINDERS)
        return(0);
    /* Get the offset */
    block_number = block % RAMDISK_TRACKSIZE;
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




int ramdisk_perform_device_ioctl(int driveno, int opcode, void * pargs)
{
DDRIVE *pdr;
DEV_GEOMETRY gc;        /* used by DEVCTL_GET_GEOMETRY */

    pdr = pc_drno_to_drive_struct(driveno);
    if (!pdr)
        return (-1);

    switch (opcode)
    {
    /* Get geometry and format share common code */
    case DEVCTL_GET_GEOMETRY:
        rtfs_memset(&gc, (byte) 0, sizeof(gc));
        /* Now set the geometry section */

        gc.dev_geometry_heads       = NUM_RAMDISK_HEADS;
        gc.dev_geometry_cylinders   = NUM_RAMDISK_CYLINDERS;
        gc.dev_geometry_secptrack   = NUM_RAMDISK_SECPTRACK;

        copybuff(pargs, &gc, sizeof(gc));
        return (0);

    case DEVCTL_FORMAT:
        {
            byte  *p;
            dword block;
            int i;
            for (block = 0; block < (NUM_RAMDISK_CYLINDERS * RAMDISK_TRACKSIZE); block++)
            {
                p = ramdisk_block((word)block);
                for (i = 0 ; i < 512; i++)
                    *p++ = 0;
            }
        }
        return(0);

    case DEVCTL_CHECKSTATUS:
        /* Check device status and return */
        /*  DEVTEST_NOCHANGE */
        /*  DEVTEST_NOMEDIA, DEVTEST_UNKMEDIA or DEVTEST_CHANGED */
        return(DEVTEST_NOCHANGE);

    case DEVCTL_WARMSTART:
        /* Set the device VALID and instruct RTFSINIT to autoformat */
        pdr->drive_flags |= DRIVE_FLAGS_VALID|DRIVE_FLAGS_FORMAT;
        return(0);

    case DEVCTL_POWER_RESTORE:
        /* Fall through */
    case DEVCTL_POWER_LOSS:
        /* Fall through */
    default:
        break;
    }
    return(0);
}
#endif /* (INCLUDE_RAMDISK) */
