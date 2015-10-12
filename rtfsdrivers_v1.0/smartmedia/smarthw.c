/**********************************************************************
               File Name: SmartHw.c

   This file contains the lowest level SmartMedia hardware routines
   that control the signals of the SmartMedia Card.  In addition to
   these two functions there are several functions implemented as
   macros in SmartHw.h

   Most of this file is dedicated to emulating SmartMedia hardware
   for testing purposes.
 **********************************************************************/
#include <rtfs.h>
#include <portconf.h>

#if (INCLUDE_SMARTMEDIA)

#include "smartHw.h"


/* _Hw_InBuf() reads "count" bytes of data from the SmartMedia card */
void _Hw_InBuf(unsigned char *databuf, int count)
{
     while(count--)
        *databuf++ = _Hw_InData();      /* Read in data */
}

/* _Hw_OutBuf() writes "count" bytes of data to the SmartMedia card */
void _Hw_OutBuf(unsigned char *databuf, int count)
{
     while(count--)
         _Hw_OutData(*databuf++);       /* Write out data */
}

#if (SMARTMEDIA_EMULATION)

/* This code is only used to test SmartMedia without actually using a
   SmartMedia card.  It is not required when using a real card.  To
   look at porting issues and what you need to change to integrate
   the driver with your hardware, look at smarthw.h and portio.c. */

#include "smart.h"
#include "smartecc.h"
#include <stdio.h>


/* Change this to change the size of the emulated SmartMedia */
#define EMULATED_TYPE SSFDC2MB


#if (EMULATED_TYPE == SSFDC1MB)
#define MYBOOTSECT 13
#define MYSECTSIZE 256
#define SM_RAM_MULT 0x1000
#define SM_SECPBLOCK 16
#define SM_ID 0xEC
#elif (EMULATED_TYPE == SSFDC2MB)
#define MYBOOTSECT 11
#define MYSECTSIZE 256
#define SM_RAM_MULT 0x2000
#define SM_SECPBLOCK 16
#define SM_ID 0xEA
#elif (EMULATED_TYPE == SSFDC4MB)
#define MYBOOTSECT 27
#define MYSECTSIZE 512
#define SM_RAM_MULT 0x2000
#define SM_SECPBLOCK 16
#define SM_ID 0xE5
#elif (EMULATED_TYPE == SSFDC8MB)
#define MYBOOTSECT 25
#define MYSECTSIZE 512
#define SM_RAM_MULT 0x4000
#define SM_SECPBLOCK 16
#define SM_ID 0xE6
#elif (EMULATED_TYPE == SSFDC16MB)
#define MYBOOTSECT 41
#define MYSECTSIZE 512
#define SM_RAM_MULT 0x8000
#define SM_SECPBLOCK 32
#define SM_ID 0x73
#elif (EMULATED_TYPE == SSFDC32MB)
#define MYBOOTSECT 35
#define MYSECTSIZE 512
#define SM_RAM_MULT 0x10000
#define SM_SECPBLOCK 32
#define SM_ID 0x75
#elif (EMULATED_TYPE == SSFDC64MB)
#define MYBOOTSECT 55
#define MYSECTSIZE 512
#define SM_RAM_MULT 0x20000
#define SM_SECPBLOCK 32
#define SM_ID 0x76
#elif (EMULATED_TYPE == SSFDC128MB)
#define MYBOOTSECT 47
#define MYSECTSIZE 512
#define SM_RAM_MULT 0x40000
#define SM_SECPBLOCK 32
#define SM_ID 0x79
#elif (EMULATED_TYPE == SSFDC256MB)
#define MYBOOTSECT 47
#define MYSECTSIZE 512
#define SM_RAM_MULT 0x80000
#define SM_SECPBLOCK 32
#define SM_ID 0x79 /* Don't know */
#elif (EMULATED_TYPE == SSFDC512MB)
#define MYBOOTSECT 47
#define MYSECTSIZE 512
#define SM_RAM_MULT 0x100000
#define SM_SECPBLOCK 32
#define SM_ID 0x79 /* Don't know */
#elif (EMULATED_TYPE == SSFDC1GB)
#define MYBOOTSECT 47
#define MYSECTSIZE 2048
#define SM_RAM_MULT 0x200000
#define SM_SECPBLOCK 64
#define SM_ID 0x79 /* Don't know */
#elif (EMULATED_TYPE == SSFDC2GB)
#define MYBOOTSECT 47
#define MYSECTSIZE 2048
#define SM_RAM_MULT 0x400000
#define SM_SECPBLOCK 64
#define SM_ID 0x79 /* Don't know */
#endif

/* second byte tells type of smartmedia */
byte ID[4] = {0, SM_ID, 0, 0}; /* Not sure what goes in other parts of ID */
#define MYREALSIZE (MYSECTSIZE + MYREDTSIZE)
#define MYREDTSIZE (8 * (MYSECTSIZE / 256))
#define SM_RAM_SIZE (SM_RAM_MULT * MYREALSIZE + MYBOOTSECT)

int CurrentRdAddr = 0;
int CurrentWrAddr = 0;
#define SETTING_RD    0x1
#define SETTING_WR    0x2
#define SETTING_RDCMD 0x3
#define SETTING_WRCMD 0x4
#define SETTING_INVAL 0x5
#define SETTING_DATA  0x0
int SettingAddr = SETTING_DATA;
int SettingCount = 0;
byte ReadCmd = 0;
byte WriteCmd = 0;
int ReadCount = 0;
int WriteCount = 0;
int initialized = 0;
BOOLEAN inserted = TRUE;

#if (SMARTMEDIA_EMULATION_ON_DISK)

#include <stdio.h>
#include <string.h>

#define DISK_FILENAME "smartmedia.dat"
FILE *disk_file = 0;

static byte smread(int location)
{
    byte data = 0xFF;
    fseek(disk_file, location, SEEK_SET);
    fread(&data, 1, 1, disk_file);
    return(data);
}

static void smwrite(int location, byte *data, size_t size)
{
    fseek(disk_file, location, SEEK_SET);
    fwrite(data, 1, size, disk_file);
    fflush(disk_file);
}

static void smset(int location, byte data, size_t size)
{
    byte tmp[528];
    memset(tmp, data, 528);

    fseek(disk_file, location, SEEK_SET);
    while(size > 0)
    {
        size_t towrite = (size < 528) ? size : 528;
        size_t written;
        written = fwrite(tmp, 1, towrite, disk_file);
        if (written <= 0)
            break;
        size -= written;
    }
    fflush(disk_file);
}

static BOOLEAN sminit(void)
{
    /* If the disk file exists, load it. */
    disk_file = fopen(DISK_FILENAME, "r+b");
    if (!disk_file)
    {
        byte cis[10] = {0x01,0x03,0xD9,0x01,0xFF,0x18,0x02,0xDF,0x01,0x20};
        byte ecc[3] = {0xA9,0xAA,0xA7};

        /* Doesn't exist...  Create it. */
        disk_file = fopen(DISK_FILENAME, "w+b");
        if (!disk_file)
            return(FALSE);

        smset(0, 0xFF, SM_RAM_SIZE); /* All bits start with 1 */

        smwrite(0, cis, 10); /* CIS */

        /* Set ECC for first sector */
        smwrite(525, ecc, 3);
    }

    return(TRUE);
}

BOOLEAN sm_need_to_format(void)
{
    FILE *tmp;
    BOOLEAN rv = TRUE;

    tmp = fopen(DISK_FILENAME, "r+b");
    if (tmp)
    {
        rv = FALSE;
        fclose(tmp);
    }

    return(rv);
}

#else

#include <string.h>

byte SmartMediaRamBuf[SM_RAM_SIZE] = {0};

static byte smread(int location)
{
    return(SmartMediaRamBuf[location]);
}

static void smwrite(int location, byte *data, size_t size)
{
    memcpy(&SmartMediaRamBuf[location], data, size);
}

static void smset(int location, byte data, size_t size)
{
    memset(&SmartMediaRamBuf[location], data, size);
}

static BOOLEAN sminit(void)
{
    byte cis[10] = {0x01,0x03,0xD9,0x01,0xFF,0x18,0x02,0xDF,0x01,0x20};
    byte ecc[3] = {0xA9,0xAA,0xA7};

    smset(0, 0xFF, SM_RAM_SIZE); /* All bits start with 1 */

    smwrite(0, cis, 10); /* CIS */

    /* Set ECC for first sector */
    smwrite(525, ecc, 3);

    return(TRUE);
}

BOOLEAN sm_need_to_format(void)
{
    return(TRUE);
}

#endif


#define SETVAR(var, val) (var = (var & (~(0xff << SettingCount))) | (val << SettingCount), SettingCount+=8)

void sm_insert_or_remove(BOOLEAN ins)
{
    inserted = ins;
}

byte input(int port)
{
    if (!initialized)
    {
        if (!sminit())
            return(0);
        initialized = 1;
    }

    if (!inserted && port != SMART_STAT)
        return(0);

    if (port == SMART_STAT)
    {
        if (inserted)
            return(0x0);
        else
            return(0xFF);
    }
    else if (port == SMART_DATA)
    {
        int address = (CurrentRdAddr >> 8) * MYREALSIZE + ReadCount++;

        if (ReadCmd == RDSTATUS || WriteCmd == RDSTATUS)
            return(0);

        if (ReadCmd == READ_ID)
            return(ID[ReadCount - 1]);

        if (ReadCmd == READ_REDT) /* Skip past sector */
            address += MYSECTSIZE;

        if (address >= SM_RAM_SIZE)
        {
            rtfs_kern_puts((byte *)"Error: SmartMedia driver tried to go past size of media.\n");
            return(0);
        }

        return(smread(address));
    }
    else
    {
        return(0);
    }
}

void output(int port, byte value)
{
    if (!initialized)
    {
        if (!sminit())
            return;
        initialized = 1;
    }

    if (!inserted)
        return;

    if (port == SMART_CTL)
    {
        switch(value)
        {
        case 0x00: SettingAddr = SETTING_INVAL; ReadCount = 0; break;
        case 0x01: SettingAddr = SETTING_INVAL; ReadCmd = 0; WriteCmd = 0; break;
        case 0x02: SettingAddr = SETTING_DATA; WriteCount = 0; break;
        case 0x04: SettingAddr = SETTING_RDCMD; break;
        case 0x06: SettingAddr = SETTING_WRCMD; break;
        case 0x08: SettingAddr = SETTING_RD; CurrentRdAddr = 0; SettingCount = 0; break;
        case 0x0a: SettingAddr = SETTING_WR; CurrentWrAddr = 0; SettingCount = 0; break;
        }
        return;
    }
    else if (port == SMART_DATA)
    {
        int address;
        byte current;
        switch(SettingAddr)
        {
        case SETTING_DATA:
            address = (CurrentWrAddr >> 8) * MYREALSIZE + WriteCount++;

            /* Apparently, ReadCmd is also used here */
            if (ReadCmd == READ_REDT) /* Skip past sector */
                address += MYSECTSIZE;

            if (address >= SM_RAM_SIZE)
            {
                rtfs_kern_puts((byte *)"Error: SmartMedia driver tried to go past size of media.\n");
                return;
            }

            current = smread(address);
            if ((current ^ value) & value)
            {
                rtfs_kern_puts((byte *)"Error: SmartMedia driver tried to write on an un-erased block\n");
                return;
            }

            smwrite(address, &value, 1);
            break;
        case SETTING_RD: SETVAR(CurrentRdAddr,value); break;
        case SETTING_WR: SETVAR(CurrentWrAddr,value); break;
        case SETTING_RDCMD: ReadCmd = value;
            /* On some commands, we do stuff now */
            if (ReadCmd == RST_CHIP)
            {
                /* Not really sure this does anything with the current code. */
                CurrentRdAddr = 0;
                CurrentWrAddr = 0;
                SettingCount = 0;
                ReadCount = 0;
                WriteCount = 0;
            }
            break;
        case SETTING_WRCMD: WriteCmd = value;
            /* On some commands, we do stuff now */
            if (WriteCmd == ERASE2)
            {
                if (MYREALSIZE * (CurrentWrAddr + SM_SECPBLOCK) > SM_RAM_SIZE)
                {
                    rtfs_kern_puts((byte *)"Error: SmartMedia driver tried to go past size of media.\n");
                    return;
                }

                /* We have to erase a block */
                smset(MYREALSIZE * CurrentWrAddr, 0xFF, MYREALSIZE * SM_SECPBLOCK);
            }
            break;
        }
        return;
    }
    else
    {
        return;
    }
}

#endif /* (SMARTMEDIA_EMULATION) */

#endif /* (INCLUDE_SMARTMEDIA) */
