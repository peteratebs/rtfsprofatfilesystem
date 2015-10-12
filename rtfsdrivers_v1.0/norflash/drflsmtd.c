/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*
* drflsmtd.c - Flash chip memory technology drivers
*
*/
/* Hardware level flash chip drivers
*  This file contains flash chip drivers for several popular flash chips
*  the file implements these drivers for a proprietary 68332 evaluation
*  board. It must be migrated to support other architectures.
****************************************************************************/

/*
ERTFS Linear Flash Support.

ERTFS linear flash device support is provided through a portable Flash
Translation layer (FTL) implemented in drfldftl.c.

The flash subsystem is included if the following line is true in portconf.h:

#define INCLUDE_FLASH_FTL       1  - Include the linear flash driver

If INCLUDE_FLASH_FTL is zero the flash subsystem is not included.

The FTL layer supports maps logical block addresses to physical block
addresses and manages block replacement, spare block management and
block wear leveling. A simple interface to underlying device specific
Memory Technology Drivers (MTS's) is provided.

MTD's are implementedt in drflsmtd.c. The requirements of MTD's are provided
in the next section.


Flash Memory technology drivers, Introduction.
============================================================================

The file drflsmtd.c contains two Memory Technology drivers. One is a driver
that implements flash emulation in RAM, the other is a driver for Intel
28Fxxx flash parts. Other drivers may be implemented by editting three or
four routines if drflsmtd.c. This section describes the required routines
and the provided sample implementation for RAM emulation of flash and for
Intel flash chips.


Adding your own Flash Memory technology drivers
============================================================================
To implement a new mtd driver you must implement custom versions of
these four functions in this file.

flash_probe()       - must report if flash is present and the total size,
the erase block size and the address and memory window width of the flash.
flash_eraseblock()  - must initialize one erase block of the flash.
mtd_window()        - must assure that a region of the flash is addressable.
flash_write_bytes() - must program a region of the flash.

Functions that must be provided to support a flash device
============================================================================
int flash_probe(void)

Flash_probe() must determine if a flash chip is present and if so determine
the address of the flash, the total size of the flash, the size of an erase
block and the window of the flash that is addressable at any one time.

These values that must be set by the flash_probe routine:

flashchip_TotalSize - Set this to the total size of the flash in bytes.
flashchip_BlockSize - Set this to the size of one erase block in bytes.
flashchip_WindowSize - Set this to the addressable window. If the part is
fully addressable, set it to the size of the part in bytes.

flashchip_start - Set this byte pointer to the start of the flash.

Flash_probe() must return 1 if a device is found zero otherwise.

Functions that must be provided to support a flash device (continued)
============================================================================
void *  mtd_MapWindow (
    RTFDrvFlashData *DriveData
    dword BlockIndex
    dword WindowIndex)

mtd_MapWindow must map in a flashchip_WindowSize'ed region of the flash
flash memory for reading and writing. The location of the flash region to
map in is calculated by multiplying the BlockIndex times the erase block size
(flashchip_BlockSize) and adding in the WindowIndex times the window map
size (flashchip_WindowSize).

The included version of mtd_MapWindow assumes that the flash part is fully
addressable, it simply multiplies these values together, adds them to the
start of flash address and returns the result. This version will not
need to be changed in most flat 32 bit target environments.
In some environments it may be necessary to add software to perform some
sort of bank register selection in this routine.

Functions that must be provided to support a flash device (continued)
============================================================================
flash_erase_block - Erase one flash erase-block.

int flash_erase_block(dword BlockIndex)

flash_erase_block() must set the erase block of size flashchip_BlockSize
at BlockIndex to the erased (all 1's) state.

flash_erase_block() must return 0 on success -1 on failure.

Functions that must be provided to support a flash device (continued)
============================================================================
int flash_write_bytes(byte volatile * dest, byte * src, int nbytes);

flash_write_bytes() must take nbytes of data from the buffer at src and write
it to the flash memory at address dest.

Dest is an address pointer for a location in a region of the flash that was
returned by mtd_MapWindow(). The region between dest and dest plus nbytes is
guaranteed to reside within flashchip_WindowSize bytes of the pointer
returned by mtd_MapWindow().

flash_write_bytes must return 0 on success -1 on failure.

Sample MTD drivers (Introdcution)
============================================================================

Two sample MTD drivers are provided. One is a simple FLASH emulator
implemented in RAM, the other is a driver for the Intel 28FXX flash series.

Sample MTD drivers (Flash emulation in RAM)
============================================================================
Flash emulation in RAM

If #define USE_EMULATED_FLASH is set to 1 the ram flash emulator is enabled.
The size of the emulated Flash can be changed by changing the constant,
FLASHEMUTOTALSIZE. The default is 64K.
The flash memory is emulated in the array FlashEmuBuffer[]. The total size
of FlashEmuBuffer[] is FLASHEMUTOTALSIZE bytes. The Ram emulator is very
simple and may be used as a starting point for other flash device drivers.

Sample MTD drivers (Intel Flash Chip Driver)
============================================================================
Intel Flash Chip Driver.

If #define USE_INTEL_FLASH is set to 1 the INTEL flash chip driver is enabled.

This driver is for intel several 28FXXX components between 2 and 8 megabytes
in size. Other components from the series may be added by modifiing the
routine flash_probe() to recognize the device and to correctly set report its
total size, erase block size, and it's address.

Two compile time constants must be changed when porting the Intel MTD
driver to a new target.

Two compile time constants tell the device driver the address of the Intel
flash part and the width of the address range window through which the part
may be read and written.

The defaults are arbitrarilly set to ten million and 64 K respectively.

#define FLASH_STARTING_ADDRESS 0x10000000
#define FLASHWINDOWSIZE   64*1024L

These must be changed, set FLASH_STARTING_ADDRESS to the base of your
flash memory and set FLASHWINDOWSIZE to one of the following:
If your flash part is fully linearlry addressble from FLASH_STARTING_ADDRESS
then set Set FLASHWINDOWSIZE to the size of the flash (in bytes). If it is
addressable only through a memory bank that is smaller then the whole part
then set FLASHWINDOWSIZE to the width of the window. The routine
mtd_MapWindow() will be called to "seek" to the appropriate window
each time a region of the flash is accessed.

*/


#include "rtfs.h"
#include "portconf.h"   /* For included devices */
#if (INCLUDE_FLASH_FTL)
#include "drflash.h"

/* FlashCHP.C - Drivers for real flash chips Select only one */
#define USE_EMULATED_FLASH 1
#define USE_INTEL_FLASH 0


/* These values that must be set by the flash_probe routine */
dword flashchip_TotalSize;  /* Total size of the flash */
dword flashchip_BlockSize; /* Erase block size in bytes */
byte *flashchip_start;     /* dword representation of the address of flash */
dword flashchip_WindowSize;

/* Used internally by mapwindow */
byte * ftl_CurrAddr;
byte * ftl_BaseAddr;

/* These functions are called from the mtd interface to actually implement
   the driver. We supply two versions. One for intel flash parts and
   one for flash emulated in ram */

int flash_erase_block(dword sector);
int flash_write_bytes(byte volatile * dest, byte * src, int nbytes);
int flash_probe(void);

/*This routine is called from the FTL and is chip independent */
void *  mtd_MapWindow    (RTFDrvFlashData * DriveData, dword BlockIndex, dword WindowIndex)
{
    dword BlockOffset;
    RTFS_ARGSUSED_PVOID((void *) DriveData);

    BlockOffset = WindowIndex * flashchip_WindowSize;
    ftl_CurrAddr = (byte *) (flashchip_start + flashchip_BlockSize * BlockIndex + BlockOffset);
    return ftl_CurrAddr;
}

/*This routine is called from the FTL layer and is chip independent */
int  mtd_EraseBlock  (RTFDrvFlashData * DriveData, dword BlockIndex)
{
    RTFS_ARGSUSED_PVOID((void *) DriveData);
    if (!flash_erase_block(BlockIndex))
        return(0);
    else
        return(-1);
}

/*This routine is called from the FTL layer and is chip independent
  this routine calls flash_probe() to initialize and probe for the flash
  device
*/
int      mtd_MountDevice (RTFDrvFlashData * DriveData, RTF_MTD_FlashInfo * FlashInfo)
{
    RTFS_ARGSUSED_PVOID((void *) DriveData);
    /* Call probe to size and Identify flash parts */
    if (!flash_probe())
        return(-1);
    else
    {
        ftl_CurrAddr = (byte *)flashchip_start;
        FlashInfo->BlockSize  = flashchip_BlockSize;
        FlashInfo->WindowSize = flashchip_WindowSize;
        if (FlashInfo->BlockSize)
            FlashInfo->TotalBlocks = flashchip_TotalSize/flashchip_BlockSize;
        else
        {
            FlashInfo->TotalBlocks = 0;
            return(-1);
        }
    }
    return(0);
}

/*This routine is called from the FTL layer and is chip independent
  this routine is currently hardwired for intel parts.
*/
int  mtd_ProgramData (RTFDrvFlashData * DriveData, byte * Address, byte * Data, unsigned int Length)
{
    RTFS_ARGSUSED_PVOID((void *) DriveData);
    if (flash_write_bytes(Address, Data, Length))
        return -1;
    else
        return 0;
}

#if (USE_EMULATED_FLASH)
#define USE_DISK_EMULATOR 0
#if (USE_DISK_EMULATOR)
void disk_emulator_probe(void);
void disk_emulator_write(byte volatile *dest, byte * src, dword bytes);
#define DISK_PROBE() disk_emulator_probe();
#define DISK_WRITE(DEST, SRC, NBYTES) disk_emulator_write(DEST,SRC, NBYTES);
#else
#define DISK_PROBE()
#define DISK_WRITE(DEST, SRC, NBYTES)
#endif

/* flash_probe -
    Return 1 if a flash device is found 0 otherwise.
*/
#define FLASHEMUTOTALSIZE    1024*1024L /* These are in K (1024 bytes) */
#define FLASHEMUBLOCKSIZE    8*1024L
#define FLASHWINDOWSIZE   8*1024L

byte   FlashEmuBuffer[FLASHEMUTOTALSIZE];

int flash_probe(void)
{
/*    rtfs_memset(FlashEmuBuffer, 0xff, FLASHEMUTOTALSIZE);  */
    flashchip_TotalSize = FLASHEMUTOTALSIZE; /* Total size of the flash */
    flashchip_BlockSize = FLASHEMUBLOCKSIZE; /* Erase block size in bytes */
    flashchip_start = (byte *) &FlashEmuBuffer[0];
    flashchip_WindowSize = FLASHWINDOWSIZE;
    DISK_PROBE()
    return(1);
}

int flash_erase_block(dword BlockIndex)
{
byte *p;
dword ltemp;
    RTFS_ARGSUSED_INT((int) BlockIndex);
    if(ftl_CurrAddr<flashchip_start)
    {
        RTFS_PRINT_STRING_1((byte *)"Curr > Base hit",PRFLG_NL); /* "Curr > Base hit" */
        return(-1);
    }
    /* (flashchip_BlockSize is long so this is
       memset(ftl_CurrAddr, 0xff, flashchip_BlockSize); */
    p = ftl_CurrAddr;
    ltemp = flashchip_BlockSize;
    while (ltemp--) *p++ = 0xff;
    DISK_WRITE(ftl_CurrAddr,ftl_CurrAddr, flashchip_BlockSize)
    return(0);
}

int flash_write_bytes(byte volatile * dest, byte * src, int bytes)
{
byte volatile* p;
byte * V;
int i;

    p = dest;
    V = src;

    if(ftl_CurrAddr<flashchip_start)
    {
        RTFS_PRINT_STRING_1((byte *)"Curr > Base hit",PRFLG_NL); /* "Curr > Base hit" */
        return(-1);
    }
   if ((dest < ftl_CurrAddr) ||(((byte*)ftl_CurrAddr + flashchip_WindowSize) < ((byte*)dest + bytes)))
   {
        RTFS_PRINT_STRING_1((byte *)"ProgramData: outside Window",PRFLG_NL); /* "ProgramData: outside Window" */
        return -1;
   }
   for (i=0; i<bytes; i++)
   {
        if ((~p[i]) & V[i])
        {
            RTFS_PRINT_STRING_1((byte *)"attempt to unset bits in flash!", 1); /* "attempt to unset bits in flash!" */
            return -1;
       }
   }
   copybuff((void *)dest, (void *)src, i);
   DISK_WRITE(dest,src,i)
   return(0);
}

#if (USE_DISK_EMULATOR)
#include <stdio.h>
#include <io.h>
#include <fcntl.h>
#include <sys\types.h>
#include <stdlib.h>
int fl_fd = -1;
dword fl_location;
void disk_emulator_probe()
{
byte *p; dword i;
    if (fl_fd != -1) close(fl_fd);
    fl_fd = open("FLASHDISK.DAT",O_RDWR|O_BINARY, S_IREAD | S_IWRITE);
    if (fl_fd < 0)
    {
        rtfs_kern_puts((byte *)"Creating new flash \n");
        fl_fd = open("FLASHDISK.DAT",O_RDWR|O_BINARY|O_CREAT|O_TRUNC, S_IREAD | S_IWRITE);
        if (fl_fd < 0) {rtfs_kern_puts((byte *)"Error opening FLASHDISK.DAT\n");return;}
        p = FlashEmuBuffer;
        for (i = 0; i < FLASHEMUTOTALSIZE; i++) *p++ = 0xff;
        if (write(fl_fd, FlashEmuBuffer, FLASHEMUTOTALSIZE)!=FLASHEMUTOTALSIZE)
        {rtfs_kern_puts((byte *)"Error initializing FLASHDISK.DAT\n");fl_fd = -1;return;}
        close(dup(fl_fd));
    }
    if (fl_fd != -1)
    {
        rtfs_kern_puts((byte *)"Loading flash \n");
		if (lseek(fl_fd, 0, SEEK_SET) != 0)
        {rtfs_kern_puts((byte *)"Seek, failed in disk_emulator_probe\n");fl_fd = -1;return;};
        if (read(fl_fd, FlashEmuBuffer, FLASHEMUTOTALSIZE)!=FLASHEMUTOTALSIZE)
            {rtfs_kern_puts((byte *)"Error loading FLASHDISK.DAT\n");fl_fd = -1;return;}
    }
}

void disk_emulator_write(byte volatile *dest, byte * src, dword bytes)
{
long fl_location;
    fl_location = (long) (dest-flashchip_start);
    if (fl_location < 0) {rtfs_kern_puts((byte *)"Write, Bad destination\n");return;};
    if (fl_fd == -1) {rtfs_kern_puts((byte *)"Write, No FLASHDISK.DAT\n");return;};
    if (lseek(fl_fd, fl_location, SEEK_SET) != fl_location)
        {rtfs_kern_puts((byte *)"Seek, failed in disk_emulator_write\n");fl_fd = -1;return;};
    if (write(fl_fd, src, bytes)!=(long)bytes)
        {rtfs_kern_puts((byte *)"write, failed in disk_emulator_write\n");fl_fd = -1;return;};
    close(dup(fl_fd));
}
#endif /* (USE_DISK_EMULATOR) */

#endif /* (USE_EMULATED_FLASH) */

#if (USE_INTEL_FLASH)

/* These must be changed. Assume that the flash memory resides at 0x10000000
and we have a 64K address window into that location. If your flash is one
hundred percent memory mapped then set FLASH_STARTING_ADDRESS to the location
of the flash and FLASHWINDOWSIZE to the size of the flash (in bytes). */

#define FLASH_STARTING_ADDRESS 0x10000000
#define FLASHWINDOWSIZE   64*1024L


dword flashchip_FlashEnd;  /*  Address of end of part */
dword flashchip_readmask;  /*  ~((dword)flashchip_BlockSize - 1); */

/*This routine is called from the FTL layer and is chip dependent
  this routine is currently hardwired for intel parts.
*/
/*INTEL Flash Parts: */
void clear_lockbits(void);

/*This routine is called from the FTL layer and is chip dependent
  this routine is currently hardwired for intel parts.
*/
/* flash_probe -
    Return 1 if a flash device is found 0 otherwise.

    If a flash device is found flash_probe must set initialize the following
    global variables with correct values
     flashchip_start     -
     flashchip_BlockSize
     flashchip_TotalSize
     flashchip_FlashEnd
     flashchip_readmask
*/

int flash_probe(void)
{
word manufacturer_ID;
word device_ID;

    flashchip_WindowSize = FLASHWINDOWSIZE;
    /************************* */
    /*Flash always starts here */
    /*************************    */
    flashchip_start = (byte *) FLASH_STARTING_ADDRESS;

    /************************************* */
    /*Now find out which flash part we got */
    /************************************* */
    /******************************** */
    /*Tell device to give up the code */
    /******************************** */
    *(word *)flashchip_start = 0x00ff;*(word *)flashchip_start = 0x00ff; /*go back to read mode */
    *(word *)flashchip_start = 0x0090;
    manufacturer_ID  = *(word *)flashchip_start;
    device_ID        = *(word *)(flashchip_start + 2);
    *(word *)flashchip_start = 0x00ff;*(word *)flashchip_start = 0x00ff; /*go back to read mode */

    /********************************************************************************* */
    /*INTEL Flash Identifiers: */
    /*MfgID = 0x89, Device ID = 0xA0 ======= 28F016SA/28F016SV    == 2Mbytes */
    /*MfgID = 0xB0, Device ID = 0xD0 ======= 28F160S3/S5          == 2Mbytes */
    /*MfgID = 0xB0, Device ID = 0xD4 ======= 28F320S3/S5          == 4Mbytes */
    /*MfgID = 0x89, Device ID = 0x14 ======= 28F320J5             == 4Mbytes */
    /*MfgID = 0x89, Device ID = 0x15 ======= 28F640J5             == 8Mbytes */
    /********************************************************************************* */
    switch((device_ID & 0x00ff))
    {
    case 0x00A0: /*MfgID = 0x89, Device ID = 0xA0 ======= 28F016SA/28F016SV == 2Mbytes */
    case 0x00D0: /*MfgID = 0xB0, Device ID = 0xD0 ======= 28F160S3/S5          == 2Mbytes */
        flashchip_BlockSize =  0x10000; /*64kbyte sectors */
        flashchip_TotalSize   =  0x200000; /*512k */
        flashchip_FlashEnd    =  0x200000;/*2Mbytes total */
        /******************************************************************************** */
        /************************************ */
        /*Clear lock bits if not 28F016 parts */
        /************************************ */
        if ((device_ID & 0x00ff) != 0x00A0)
        {
            clear_lockbits();
        }
        break;
    case 0x00D4:/*MfgID = 0xB0, Device ID = 0xD4 ======= 28F320S3/S5== 4Mbytes */
        flashchip_BlockSize =  0x10000; /*64kbyte sectors */
        flashchip_TotalSize   =  0x400000; /* */
        flashchip_FlashEnd    =  0x400000;/*4Mbytes total */
        clear_lockbits();
        break;
    case 0x0014:/*MfgID = 0x89, Device ID = 0x14 ======= 28F320J5== 4Mbytes */
        flashchip_BlockSize   =  0x20000; /*128kbyte sectors */
        flashchip_TotalSize   =  0x400000; /* */
        flashchip_FlashEnd    =  0x400000;/*4Mbytes total */
        clear_lockbits();
        break;
    case 0x0015:/*MfgID = 0x89, Device ID = 0x15 ======= 28F640J5 == 8Mbytes */
        flashchip_BlockSize =  0x20000; /*128kbyte sectors */
        flashchip_TotalSize   =  0x800000; /* */
        flashchip_FlashEnd    =  0x800000;/*8Mbytes total */
        clear_lockbits();
        break;
    default: /*we do not know what this is at this time - error */
        return(0);
    }
    /******************************************** */
    /*Compute a read mask for use later for speed */
    /******************************************** */
    flashchip_readmask = ~((dword)flashchip_BlockSize - 1);
    return(1);
}

/****************************** */
/*Erase rouutine for INTEL chip */
/****************************** */
int flash_erase_block(dword sector)
{
    word volatile *ptr;
    /*********************************** */
    /*Compute address of sector to erase */
    /*********************************** */
    ptr =  (word volatile *) (flashchip_start + (flashchip_BlockSize * sector));
    /**********************  */
    /*erase sector sequence  */
    /**********************  */
    rtfs_port_disable();
    *ptr = 0x0020;
    *ptr = 0x00d0;
    rtfs_port_enable();
    /*************************** */
    /*Wait for erase to complete */
    /*************************** */
    while (!(*ptr & 0x0080));
    /************************** */
    /*Make sure erase completed */
    /************************** */
    if ((*ptr & 0x0030) == 0x0030)
    {
        /********************************* */
        /*Did not work - try ONE more time */
        /********************************* */
        rtfs_port_disable();
        *ptr = 0x0050; /*clear CSR */
        *ptr = 0x0020;
        *ptr = 0x00d0;
        rtfs_port_enable();
        while (!(*ptr & 0x0080));
        if ((*ptr & 0x0030) == 0x0030)
            return(-1);
    }
    /************************** */
    /*Lastly, set the read mode */
    /************************** */
    *ptr = 0x00ff;*ptr = 0x00ff;
    return(0);
}
/* *********************************************************************************************** */
/*This is the write routine for the INTEL flash parts */
/*Do some fancy footwork since we allow byte writes, but part is configured for word writes, only */
/************************************************************************************************ */
int flash_write_bytes(byte volatile * dest, byte * src, int nbytes)
{
    word volatile *status;
    word word2burn;
    /************************ */
    /*Make sure burn is valid */
    /************************ */
    if (!nbytes || dest >= (byte *)flashchip_FlashEnd || dest < flashchip_start)
    {
        return(-1); /*Bad address / data */
    }
    /**************************************** */
    /*Status register is word accessible only */
    /**************************************** */
    status = (word *)((dword)dest & 0xfffffffe);
    /**************************** */
    /*Deal with odd start address */
    /**************************** */
    if ((dword)dest & 1)
    {
        /*************************************** */
        /*Move destination pointer back one byte */
        /*************************************** */
        dest = (byte volatile *)status;
        *(((byte *)&word2burn)  ) = *dest; /*grab MSB of word already there */
        *(((byte *)&word2burn)+1) = *src++;/*grab LSB of word to burn */
        /******************** */
        /*Burn first ODD byte */
        /******************** */
        *((word volatile *)dest) = 0x0040;    /*write mode */
        *((word volatile *)dest) = word2burn; /*latch in new data */
        /********************************************* */
        /*Increment pointers and wait for data to burn */
        /********************************************* */
        nbytes--;
        while(!(*((word volatile *)dest) & 0x0080));
        dest+=2;
    }

    /************************************************* */
    /*Loop through and burn all bytes on even adresses */
    /************************************************* */
    while(nbytes > 1)
    {
        /************************************************************** */
        /*Do not write FF s since flash is already erased (or should be) */
        /************************************************************** */
        if(*src != 0xff || *(src+1) != 0xff)
        {
            /***************************************** */
            /*Make sure we do not pass the end of flash */
            /***************************************** */
            if ((dword)dest >= flashchip_FlashEnd)
            {
                /******************************** */
                /*Place block back into read mode */
                /******************************** */
                *((word volatile *) ((dword)dest & flashchip_readmask)) = 0x00ff;
                *((word volatile *) ((dword)dest & flashchip_readmask)) = 0x00ff;
                return(-1);   /*we FULL */
            }
            /******************* */
            /*Construct the word */
            /******************* */
            *(((byte *)&word2burn)  ) = *src++; /*grab MSB of word to burn */
            *(((byte *)&word2burn)+1) = *src++; /*grab LSB of word to burn */
            /************** */
            /*Burn the word */
            /************** */
            *(((volatile word *)dest)) = 0x0040;    /*write mode */
            *(((volatile word *)dest)) = word2burn; /*latch in new data */
            /************************************************ */
            /*Increment src pointer and wait for data to burn */
            /************************************************ */
            nbytes -= 2;
            while(!(*((word volatile *)dest) & 0x0080));
            dest +=2;
        }
        else
        {
            /************************* */
            /*No need to burn 0xffff s */
            /************************* */
            src    += 2;
            dest   += 2;
            nbytes -= 2;
        }
        /******************************************************** */
        /*If this new block, place last block back into read mode */
        /******************************************************** */
        if (!((dword)dest % (dword)flashchip_BlockSize))
        {
            *((word volatile *) (((dword)(dest-2)) & flashchip_readmask)) = 0x00ff;
            *((word volatile *) (((dword)(dest-2)) & flashchip_readmask)) = 0x00ff;
            /*********************************** */
            /*Compute address to new status word */
            /*********************************** */
            status = (word *)dest;
        }
    }
    /*************************************************** */
    /*Deal with last byte if left over after word writes */
    /*************************************************** */
    if (nbytes)
    {
        *(((byte *)&word2burn)  ) = *src; /*grab last byte burn */
        *(((byte *)&word2burn)+1) = 0xff; /*make last LSB an empty 0xff */
        /************** */
        /*Burn the word */
        /************** */
        *(((word volatile *)dest)) = 0x0040;    /*write mode */
        *(((word volatile *)dest)) = word2burn; /*latch in new data */
        while(!(*((word volatile *)dest) & 0x0080));
    }
    /****************************************** */
    /*Success - place block back into read mode */
    /****************************************** */
    *((word volatile *) ((dword)dest & flashchip_readmask)) = 0x00ff;
    *((word volatile *) ((dword)dest & flashchip_readmask)) = 0x00ff;
    return(0);
}
/* ******************************** */
/* Clear locknbits on INTEL devices */
/* ******************************** */
void clear_lockbits(void)
{
    word volatile *block_ptr;

    /****************************** */
    /*Start at start and end at end */
    /****************************** */
    block_ptr = (word volatile *)flashchip_start;
    do
    {
        *block_ptr = 0x0098; /*Read Query command */
        if (*(block_ptr+1) & 1)
        {
            /******************************** */
            /*Place block back into read mode */
            /******************************** */
            *block_ptr = 0x00ff;*block_ptr = 0x00ff;
            /*********************************************************************** */
            /*We found a block which is locked - unlock all blocks and exit the loop */
            /*********************************************************************** */
            *((word volatile *)flashchip_start) = 0x0060;
            *((word volatile *)flashchip_start) = 0x00D0;
            /*********** */
            /*Force exit */
            /*********** */
            block_ptr = (word volatile *)flashchip_FlashEnd;
            /*************************** */
            /*Wait for clear to complete */
            /*************************** */
            while(!(*(word volatile *)flashchip_start & 0x0080));
            /******************************** */
            /*Place block back into read mode */
            /******************************** */
            *(word volatile *)flashchip_start = 0x00ff;*(word volatile *)flashchip_start = 0x00ff;
        }
        else
        {
            /******************************** */
            /*Place block back into read mode */
            /******************************** */
            *block_ptr = 0x00ff;*block_ptr = 0x00ff;
            /*************************** */
            /*Keep looking at each block */
            /*************************** */
            block_ptr = (word *)((dword)block_ptr + (dword)flashchip_BlockSize);
        }
    }
    while ((dword)block_ptr < flashchip_FlashEnd);
}
#endif /* (USE_INTEL_FLASH) */

#endif /* (INCLUDE_FLASH_FTL) */
