/******************************************************************
   File Name:        SmartApi.c
      This file contains the low level, non-hardware dependant
      routines for the SmartMedia interface.

 ******************************************************************/
#include <rtfs.h>

#if (INCLUDE_SMARTMEDIA)

#include "smarthw.h"
#include "smartapi.h"
#include "smartecc.h"

/* SmartMedia Busy Times */
/*#define BUSY_PROG    (( 20*MSEC_100)/100) */  /* tPROG : 20ms ----- Program Time */
/*#define BUSY_ERASE   ((400*MSEC_100)/100) */  /* tBERASE: 400ms ----- Block Erase Time */
/*#define BUSY_READ    ((  1*MSEC_100)/100) */  /* tR : 100us ----- Data transfer Time */
/*#define BUSY_RESET   ((  6*MSEC_100)/100) */  /* tRST : 6ms ----- Device Resetting Time */
/* Hardware Timer */
/*#define TIME_PON     ((300*MSEC_100)/100) */  /* 300ms ------ Power On Wait Time */
/*#define TIME_CDCHK   (( 20*MSEC_100)/100) */  /* 20ms ------ Card Check Interval Timer */
/*#define TIME_WPCHK   ((  5*MSEC_100)/100) */  /* 5ms ------ WP Check Interval Timer */

/* SmartMedia Busy Time in ms */
#define BUSY_PROG    (20)  	/* tPROG : 20ms ----- Program Time */
#define BUSY_ERASE   (400)  /* tBERASE: 400ms ----- Block Erase Time */
#define BUSY_READ    (1)  	/* tR : 100us ----- Data transfer Time */
#define BUSY_RESET   (6)  	/* tRST : 6ms ----- Device Resetting Time */
/* Hardware Timer */
#define TIME_PON     (300)  /* 300ms ------ Power On Wait Time */
#define TIME_CDCHK   (20)  	/* 20ms ------ Card Check Interval Timer */
#define TIME_WPCHK   (5)  	/* 5ms ------ WP Check Interval Timer */


/*********************** Local Function Prototypes *************************/
void _Set_SsfdcRdCmd(byte);
void _Set_SsfdcRdAddr(byte);
void _Set_SsfdcRdChip(void);
void _Set_SsfdcRdStandby(void);
void _Set_SsfdcWrCmd(byte);
void _Set_SsfdcWrAddr(byte);
void _Set_SsfdcWrBlock(void);
void _Set_SsfdcWrStandby(void);
char _Check_SsfdcBusy(int);
char _Check_SsfdcStatus(void);
void _Reset_SsfdcErr(void);
void _Read_SsfdcBuf(byte *);
void _Write_SsfdcBuf(byte *);
void _Read_SsfdcByte(byte *);
void _ReadRedt_SsfdcBuf(byte *);
void _WriteRedt_SsfdcBuf(byte *);
byte _Check_DevCode(byte);
void _Set_ECCdata(byte,byte *);
void _Calc_ECCdata(byte *);

/************************* Local Data **************************************/
struct SSFDCTYPE    Ssfdc;
struct ADDRESS      Media;
struct CIS_AREA     CisArea;
static byte         EccBuf[6];

/************************** Definitions ************************************/
#define EVEN 0      // Even Page for 256byte/page
#define ODD  1      // Odd  Page for 256byte/page


/* Wait interval time units. Each uint is .1 milliseconds */
/* Do the best we can with our clock granularity */
void SmartWait(int interval)
{
    rtfs_port_sleep(interval * 10);
}


/*<BCI>*****************************************************************
Name:   char Bit_Count(byte cdata)

Parameters: a byte
Returns:    return value between 0 & 8 is the number of '1's in the input
Description:
            Calculates bits in a byte

Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Bit_Count(byte cdata)
{
    char bitcount=0;
    while(cdata)
    {
        bitcount = (char) (bitcount + (char)(cdata &0x01));
        cdata /=2;
    }
    return(bitcount);
}


/*<BCI>*****************************************************************
Name:   char Bit_CountWord(word cdata)

Parameters: a 16 bit number
Returns:    return value between 0 & 16 is the number of '1's in the input
Description:
            Calculates bits in a word

Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Bit_CountWord(word cdata)
{
    char bitcount=0;
    while(cdata)
    {
        bitcount = (char) (bitcount + (char)(cdata &0x01));
        cdata /=2;
    }
    return(bitcount);
}


/*<BCI>*****************************************************************
Name:   void StringCopy(char *stringA, char *stringB, int count)

Parameters: two strings and a character count
Returns:    none
Description:
            copies stringB to stringA

Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
void StringCopy(char *stringA, char *stringB, int count)
{
    int i;
    for(i=0; i<count; i++)
        *stringA++ = *stringB++;
}


/*<BCI>*****************************************************************
Name:   char StringCmp(char *stringA, char *stringB, int count)

Parameters: compares strings of length "count"
Returns:    zero if they are the same, -1 if not
Description:
            compares stringB to stringA

Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char StringCmp(char *stringA, char *stringB, int count)
{
    int i;
    for (i=0;i<count;i++)
    {
        if (*stringA++ != *stringB++)
            return(ERROR);
    }
    return(SUCCESS);
}


/*<BCI>*****************************************************************
Name:   char Check_DataBlank(byte *redundant)

Parameters: pointer to 16-byte redundant area
Returns:    All 16 bytes == 0xFF => Unused/Blank => return SUCCESS
                         != 0xFF => In use       => return ERROR
Description:
            checks the redundant area of a block to determine
            whether the block is used or not.

Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Check_DataBlank(byte *redundant)
{
    char i;
    for(i=0; i<REDTSIZE; i++)
        if(*redundant++!=0xFF) return(ERROR);
    return(SUCCESS);
}


/*<BCI>*****************************************************************
Name:   char Check_FailBlock(byte *redundant)

Parameters: pointer to 16-byte redundant area
Returns:    Block_Status_Flag == 0xFF OR only 1 '0' bit => Valid   => return SUCCESS
                              has 2 or more '0's        => Invalid => return ERROR
Description:
            checks the redundant area of a block to determine
            whether the block is valid or not.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Check_FailBlock(byte *redundant)
{
    byte Block_Status_Flag;
    Block_Status_Flag = redundant[REDT_BLOCK];
    if(Block_Status_Flag==0xFF)         return(SUCCESS);
    if(! Block_Status_Flag)             return(ERROR);
    if(Bit_Count(Block_Status_Flag)<7)  return(ERROR);
    return(SUCCESS);
}


/*<BCI>*****************************************************************
Name:   char Check_DataStatus(byte *redundant)

Parameters: pointer to 16-byte redundant area
Returns:    Data_Status_Flag == 0xFF OR less than 4 '0's => Valid   => return SUCCESS
                                has 4 or more '0's       => Invalid => return ERROR
Description:
            checks the redundant area of a block for Data Status Error.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Check_DataStatus(byte *redundant)
{
    byte Data_Status_Flag;
    Data_Status_Flag = redundant[REDT_DATA];
    if(Data_Status_Flag == 0xFF)        return(SUCCESS);
    if(! Data_Status_Flag)              return(ERROR);
    if(Bit_Count(Data_Status_Flag)<5)   return(ERROR);
    return(SUCCESS);
}


/*<BCI>*****************************************************************
Name:   char Load_LogBlockAddr(byte *redundant)

Parameters: pointer to 16-byte redundant area
Returns:    Media.LogBlock is set to the logical block address
            If  (Block address fields 0 and 1 are the same AND valid) return SUCCESS
Description:
            extracts the logical address from the redundant area of
            a block.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Load_LogBlockAddr(byte *redundant)
{
    word addr1;
    word addr2;
    addr1 = (word) ((word)(redundant[REDT_ADDR1H] * 0x0100) + (word)redundant[REDT_ADDR1L]);
    addr2 = (word) ((word)(redundant[REDT_ADDR2H] * 0x0100) + (word)redundant[REDT_ADDR2L]);

    if (addr1==addr2)
    {
        if((addr1 &0xF000)==0x1000)
        { Media.LogBlock=(word)((addr1 &0x0FFF)/2); return(SUCCESS); }
    }
    if (Bit_CountWord((word)(addr1^addr2))!=0x01) return(ERROR);
    if ((addr1 &0xF000)==0x1000)
    {
        if(! (Bit_CountWord(addr1) &0x01))
        { Media.LogBlock=(word)((addr1 &0x0FFF)/2); return(SUCCESS); }
    }
    if ((addr2 &0xF000)==0x1000)
    {
        if (! (Bit_CountWord(addr2) &0x01))
        { Media.LogBlock=(word)((addr2 &0x0FFF)/2); return(SUCCESS); }
    }
    return(ERROR);
}


/*<BCI>*****************************************************************
Name:   void Clr_RedundantData(byte *redundant)

Parameters: pointer to 16-byte redundant area
Returns:    none
Description:
            initializes the redundant block area to 0xFFs
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
void Clr_RedundantData(byte *redundant)
{
    char i;
    for(i=0; i<REDTSIZE; i++)
       redundant[i] = 0xFF;
}


/*<BCI>*****************************************************************
Name:   void Set_LogBlockAddr(byte *redundant)

Parameters: pointer to 16-byte redundant area
Returns:    none
Description:
            redundant area is initialized and logical block address is set.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
void Set_LogBlockAddr(byte *redundant)
{
    word addr;
    redundant[REDT_BLOCK]=0xFF;
    redundant[REDT_DATA] =0xFF;
    addr=(word)(Media.LogBlock*2+0x1000);
    if((Bit_CountWord(addr)%2)) addr++;
    redundant[REDT_ADDR1H] = redundant[REDT_ADDR2H] = (byte)(addr/0x0100);
    redundant[REDT_ADDR1L] = redundant[REDT_ADDR2L] = (byte)addr;
}


/*<BCI>*****************************************************************
Name:   void Set_FailBlock(byte *redundant)

Parameters: pointer to 16-byte redundant area
Returns:    none
Description:
            Initializes the redundant area and sets the Block Status Flag
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
void Set_FailBlock(byte *redundant)
{
    char i;
    for(i=0; i<REDTSIZE; i++)
        redundant[i] = (byte)( (i==REDT_BLOCK) ? 0xF0 : 0xFF);
}


/*<BCI>*****************************************************************
Name:   void Set_DataStaus(byte *redundant)

Parameters: pointer to 16-byte redundant area
Returns:    none
Description:
            redundant data status byte initialized.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
void Set_DataStaus(byte *redundant)
{
    redundant[REDT_DATA] = 0x00;
}


/*<BCI>*****************************************************************
Name:   char Ssfdc_ReadCisSect(byte *buf,byte *redundant)

Parameters: none
Returns:    error status 0=SUCCESS, -1=ERROR
Description:
            reads CIS information in the SmartCard into a buffer
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Ssfdc_ReadCisSect(byte *buf,byte *redundant)
{
    byte zone,sector;
    word block;
    char result;

    zone=Media.Zone; block=Media.PhyBlock; sector=Media.Sector;
    Media.Zone=0;
    Media.PhyBlock=CisArea.PhyBlock;
    Media.Sector=CisArea.Sector;

    result = Ssfdc_ReadSect(buf,redundant);
    Media.Zone=zone; Media.PhyBlock=block; Media.Sector=sector;

    return result;
}


/*  Above here is all memory architecture independent */

/*<BCI>*****************************************************************
Name:   void Ssfdc_Reset(void)

Parameters: none
Returns:    none
Description:
            sends reset commands to SmartCard.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
void Ssfdc_Reset(void)
{
    _Set_SsfdcRdCmd(RST_CHIP);
    _Check_SsfdcBusy(BUSY_RESET);
    _Set_SsfdcRdCmd(READ);
    _Check_SsfdcBusy(BUSY_READ);
    _Set_SsfdcRdStandby();
}



/*<BCI>*****************************************************************
Name:   void Ssfdc_WriteRedtMode(void)

Parameters: none
Returns:    none
Description:
            sets the SmartCard to "program redundant area" mode
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
void Ssfdc_WriteRedtMode(void)
{
    _Set_SsfdcRdCmd(RST_CHIP);
    _Check_SsfdcBusy(BUSY_RESET);
    _Set_SsfdcRdCmd(READ_REDT);
    _Check_SsfdcBusy(BUSY_READ);
    _Set_SsfdcRdStandby();
}


/*<BCI>*****************************************************************
Name:   void Ssfdc_ReadID(byte *buf)

Parameters: Pointer to 4 bytes of space.
Returns:    Pointer points to MANUFACTURER CODE (Byte 1)
                              DEVICE CODE       (Byte 2)
Description:
            gets the ID CODE of the SmartCard.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
void Ssfdc_ReadID(byte *buf)
{
    _Set_SsfdcRdCmd(READ_ID);
    _Set_SsfdcRdChip();
    _Read_SsfdcByte(buf);
    _Read_SsfdcByte(buf+1);
    _Read_SsfdcByte(buf+2);
    _Read_SsfdcByte(buf+3);
    _Set_SsfdcRdStandby();
}


/*<BCI>*****************************************************************
Name:   char Ssfdc_ReadSect(byte *buf,byte *redundant)

Parameters: Pointers to enough allocated memory:
            buf should point to 512 bytes and redundant to 16 bytes.
Returns:    Buf points to data and redundant points to redundant data
            error status returned.
Description:
            reads 1 sector (512 bytes) of data from the SmartCard.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Ssfdc_ReadSect(byte *buf,byte *redundant)
{
    _Set_SsfdcRdCmd(READ);
    _Set_SsfdcRdAddr(EVEN);
    if(_Check_SsfdcBusy(BUSY_READ))
    {
        _Reset_SsfdcErr();
        return(ERROR);
    }
    _Read_SsfdcBuf(buf);
    _ReadRedt_SsfdcBuf(redundant);
    if(_Check_SsfdcBusy(BUSY_READ))
    {
        _Reset_SsfdcErr();
        return(ERROR);
    }
    if((Ssfdc.Attribute &MPS)==PS256)
    {
        _Set_SsfdcRdCmd(READ);
        _Set_SsfdcRdAddr(ODD);
        if(_Check_SsfdcBusy(BUSY_READ))
        {
            _Reset_SsfdcErr();
            return(ERROR);
        }
        _Read_SsfdcBuf(buf+0x0100);
        _ReadRedt_SsfdcBuf(redundant+0x0008);
        if(_Check_SsfdcBusy(BUSY_READ))
        {
            _Reset_SsfdcErr();
            return(ERROR);
        }
    }
    _Calc_ECCdata(buf);
    _Set_SsfdcRdStandby();
    return(SUCCESS);
}


/*<BCI>*****************************************************************
Name:   char Ssfdc_WriteSect(byte *buf,byte *redundant)

Parameters: buf points to 512 bytes of data to be written to disk.
Returns:    error status returned;
            if there was a long busy period, abnormal termination.
Description:
            writes a sector of data to SmartCard with ECC.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Ssfdc_WriteSect(byte *buf,byte *redundant)
{
    _Calc_ECCdata(buf);
    _Set_SsfdcWrCmd(WRDATA);
    _Set_SsfdcWrAddr(EVEN);
    _Write_SsfdcBuf(buf);
    _Set_ECCdata(EVEN,redundant);
    _WriteRedt_SsfdcBuf(redundant);
    _Set_SsfdcWrCmd(WRITE);
    if(_Check_SsfdcBusy(BUSY_PROG))
    {
        _Reset_SsfdcErr();
        return(ERROR);
    }
    if((Ssfdc.Attribute &MPS)==PS256)
    {
        _Set_SsfdcWrCmd(RDSTATUS);
        if(_Check_SsfdcStatus())                /* Patch */
        {
            _Set_SsfdcWrStandby();              /* for 256byte/page */
            _Set_SsfdcRdStandby();              /* Next Status Check is ERROR */
            return(SUCCESS);
        }
        _Set_SsfdcWrCmd(WRDATA);
        _Set_SsfdcWrAddr(ODD);
        _Write_SsfdcBuf(buf+0x0100);
        _Set_ECCdata(ODD,redundant);
        _WriteRedt_SsfdcBuf(redundant+0x0008);
        _Set_SsfdcWrCmd(WRITE);
        if(_Check_SsfdcBusy(BUSY_PROG))
        {
            _Reset_SsfdcErr();
            return(ERROR);
        }
    }
    _Set_SsfdcWrStandby();
    _Set_SsfdcRdStandby();
    return(SUCCESS);
}


/*<BCI>*****************************************************************
Name:   char Ssfdc_WriteSectForCopy(byte *buf,byte *redundant)

Parameters: buf points to 512 bytes of data to be written to disk.
Returns:    error status returned;
            if there was a long busy period, abnormal termination.
Description:
            writes a sector of data to SmartCard with ECC.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Ssfdc_WriteSectForCopy(byte *buf,byte *redundant)
{
    _Set_SsfdcWrCmd(WRDATA);
    _Set_SsfdcWrAddr(EVEN);
    _Write_SsfdcBuf(buf);
    _WriteRedt_SsfdcBuf(redundant);
    _Set_SsfdcWrCmd(WRITE);
    if(_Check_SsfdcBusy(BUSY_PROG))
    {
        _Reset_SsfdcErr();
        return(ERROR);
    }
    if((Ssfdc.Attribute &MPS)==PS256)
    {
        _Set_SsfdcWrCmd(RDSTATUS);
        if(_Check_SsfdcStatus())                    /* Patch */
        {
            _Set_SsfdcWrStandby();                  /* for 256byte/page */
            _Set_SsfdcRdStandby();                  /* Next Status Check is ERROR */
            return(SUCCESS);
        }
        _Set_SsfdcWrCmd(WRDATA);
        _Set_SsfdcWrAddr(ODD);
        _Write_SsfdcBuf(buf+0x0100);
        _WriteRedt_SsfdcBuf(redundant+0x0008);
        _Set_SsfdcWrCmd(WRITE);
        if(_Check_SsfdcBusy(BUSY_PROG))
        {
            _Reset_SsfdcErr();
            return(ERROR);
        }
    }
    _Set_SsfdcWrStandby();
    _Set_SsfdcRdStandby();
    return(SUCCESS);
}


/*<BCI>*****************************************************************
Name:   char Ssfdc_EraseBlock(void)

Parameters: none
Returns:    error status, 0=SUCCESS, -1=ERROR
Description:
            erases one block of SmartCard memory.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Ssfdc_EraseBlock(void)
{
    _Set_SsfdcWrCmd(ERASE1);
    _Set_SsfdcWrBlock();
    _Set_SsfdcWrCmd(ERASE2);
    if(_Check_SsfdcBusy(BUSY_ERASE))
    {
        _Reset_SsfdcErr();
        return(ERROR);
    }
    _Set_SsfdcWrStandby();
    _Set_SsfdcRdStandby();
    return(SUCCESS);
}


/*<BCI>*****************************************************************
Name:   char Ssfdc_ReadRedtData(byte *redundant)

Parameters: pointer to 16 bytes of memory.
Returns:    error status, 0=SUCCESS, -1=ERROR
Description:
            memory set to redundant data,
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Ssfdc_ReadRedtData(byte *redundant)
{
    _Set_SsfdcRdCmd(READ_REDT);
    _Set_SsfdcRdAddr(EVEN);
    if(_Check_SsfdcBusy(BUSY_READ))
    {
        _Reset_SsfdcErr();
        return(ERROR);
    }
    _ReadRedt_SsfdcBuf(redundant);
    if(_Check_SsfdcBusy(BUSY_READ))
    {
        _Reset_SsfdcErr();
        return(ERROR);
    }
    if((Ssfdc.Attribute &MPS)==PS256)
    {
        _Set_SsfdcRdCmd(READ_REDT);
        _Set_SsfdcRdAddr(ODD);
        if(_Check_SsfdcBusy(BUSY_READ))
        {
              _Reset_SsfdcErr();
              return(ERROR);
        }
        _ReadRedt_SsfdcBuf(redundant+0x0008);
        if(_Check_SsfdcBusy(BUSY_READ))
        {
              _Reset_SsfdcErr();
              return(ERROR);
        }
    }
    _Set_SsfdcRdStandby();
    return(SUCCESS);
}


/*<BCI>*****************************************************************
Name:   char Ssfdc_WriteRedtData(byte *redundant)

Parameters: pointer to 16 bytes of memory.
Returns:    error status, 0=SUCCESS, -1=ERROR
Description:
            writes to the redundant part of a SmartCard sector.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Ssfdc_WriteRedtData(byte *redundant)
{
    _Set_SsfdcWrCmd(WRDATA);
    _Set_SsfdcWrAddr(EVEN);
    _WriteRedt_SsfdcBuf(redundant);
    _Set_SsfdcWrCmd(WRITE);
    if(_Check_SsfdcBusy(BUSY_PROG))
        { _Reset_SsfdcErr(); return(ERROR); }
    if((Ssfdc.Attribute &MPS)==PS256)
    {
        _Set_SsfdcWrCmd(RDSTATUS);
        if(_Check_SsfdcStatus())
        {
            _Set_SsfdcWrStandby();
            _Set_SsfdcRdStandby();
            return(SUCCESS);
        }
        _Set_SsfdcWrCmd(WRDATA);
        _Set_SsfdcWrAddr(ODD);
        _WriteRedt_SsfdcBuf(redundant+0x0008);
        _Set_SsfdcWrCmd(WRITE);
        if(_Check_SsfdcBusy(BUSY_PROG))
            { _Reset_SsfdcErr(); return(ERROR); }
    }
    _Set_SsfdcWrStandby();
    _Set_SsfdcRdStandby();
    return(SUCCESS);
}


/*<BCI>*****************************************************************
Name:   char Ssfdc_CheckStatus(void)

Parameters: none
Returns:    error status, 0=SUCCESS, -1=ERROR
Description:
            checks for SmartCard programming worked.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Ssfdc_CheckStatus(void)
{
    _Set_SsfdcRdCmd(RDSTATUS);
    if(_Check_SsfdcStatus())
    {
        _Set_SsfdcRdStandby();
        return(ERROR);
    }
    _Set_SsfdcRdStandby();
    return(SUCCESS);
}


/*<BCI>*****************************************************************
Name:   void _Set_SsfdcRdCmd(byte cmd)

Parameters: cmd should be one of the following:
                RST_CHIP        Reset
                READ_ID         Read Device ID
                READ            Read Data
                READ_REDT       Read Redundant Data
                RDSTATUS        Read Status
Returns:    none
Description:
            sends a Write Protected control command to SmartCard.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
void _Set_SsfdcRdCmd(byte cmd)
{
    _Hw_SetRdCmd();
    _Hw_OutData(cmd);
    _Hw_SetRdData();
}


/*<BCI>*****************************************************************
Name:   void _Set_SsfdcRdAddr(byte add)

Parameters: cmd is used only for 256 byte/page case.
            add == EVEN => set even page addresses
            add == ODD  => set odd  page addresses
Returns:    none
Description:
            sets the address for the SmartCard to that found in
            the "Media" struct. Used when Write Protect is enabled.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
void _Set_SsfdcRdAddr(byte add)
{
    word addr;
    addr = (word)(Media.Zone*Ssfdc.MaxBlocks+Media.PhyBlock);
    addr=(word)(addr*(word)Ssfdc.MaxSectors+Media.Sector);
    if((Ssfdc.Attribute &MPS)==PS256)             /* for 256byte/page */
         addr=(word)(addr*2+(word)add);

    _Hw_SetRdAddr();
    _Hw_OutData(0x00);
    _Hw_OutData((byte)addr);
    _Hw_OutData((byte)(addr/0x0100));
    if((Ssfdc.Attribute &MADC)==AD4CYC)
         _Hw_OutData((byte)(Media.Zone/2));
    _Hw_SetRdData();
}


/*<BCI>*****************************************************************
Name:   void _Set_SsfdcRdChip(void)

Parameters: none
Returns:    none
Description:
            sets address when doing an ID read.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
void _Set_SsfdcRdChip(void)
{
    _Hw_SetRdAddr();
    _Hw_OutData(0x00);
    _Hw_SetRdData();
}


/*<BCI>*****************************************************************
Name:   void _Set_SsfdcRdStandby(void)

Parameters: none
Returns:    none
Description:
            calls _Hw_SetRdStandby()
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
void _Set_SsfdcRdStandby(void)
{
    _Hw_SetRdStandby();
}


/*<BCI>*****************************************************************
Name:   void _Set_SsfdcWrCmd(byte cmd)

Parameters: cmd should be one of the following:
               RST_CHIP        Reset
               READ_ID         Read Device ID
               READ            Read Data
               READ_REDT       Read Redundant Data
               RDSTATUS        Read Status
               WRDATA          Write Data Input
               WRITE           Write
               RDSTATUS        Read Status
               ERASE1          1st Erase
               ERASE2          2nd Erase
Returns:    none
Description:
            sends a Non-Write-Protect control command to SmartCard.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
void _Set_SsfdcWrCmd(byte cmd)
{
    _Hw_SetWrCmd();
    _Hw_OutData(cmd);
    _Hw_SetWrData();
}


/*<BCI>*****************************************************************
Name:   void _Set_SsfdcWrAddr(byte add)

Parameters: cmd is used only for 256 byte/page case.
            add == EVEN => set even page addresses
            add == ODD  => set odd  page addresses
Returns:    none
Description:
            sets the address for the SmartCard to that found in
            the "Media" struct. Used when Write Protect is diabled.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
void _Set_SsfdcWrAddr(byte add)
{
    word addr;
    addr=(word)(Media.Zone*Ssfdc.MaxBlocks+(word)Media.PhyBlock);
    addr=(word)(addr*(word)Ssfdc.MaxSectors+(word)Media.Sector);
    if((Ssfdc.Attribute &MPS)==PS256)             /* for 256byte/page */
        addr=(word)(addr*2+(word)add);
    _Hw_SetWrAddr();
    _Hw_OutData(0x00);
    _Hw_OutData((byte)addr);
    _Hw_OutData((byte)(addr/0x0100));
    if((Ssfdc.Attribute &MADC)==AD4CYC)
        _Hw_OutData((byte)(Media.Zone/2));
    _Hw_SetWrData();
}


/*<BCI>*****************************************************************
Name:   void _Set_SsfdcWrBlock(void)

Parameters: none
Returns:    none
Description:
            sets the block address to that found in the "Media" struct.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
void _Set_SsfdcWrBlock(void)
{
    word addr;
    addr=(word)((word)Media.Zone*Ssfdc.MaxBlocks+(word)Media.PhyBlock);
    addr=(word)(addr*(word)Ssfdc.MaxSectors);
    if((Ssfdc.Attribute &MPS)==PS256)             /* for 256byte/page */
        addr=(word)(addr*2);

    _Hw_SetWrAddr();
    _Hw_OutData((byte)addr);
    _Hw_OutData((byte)(addr/0x0100));
    if((Ssfdc.Attribute &MADC)==AD4CYC)
        _Hw_OutData((byte)(Media.Zone/2));
    _Hw_SetWrData();
}


/*<BCI>*****************************************************************
Name:   void _Set_SsfdcWrStandby(void)

Parameters: none
Returns:    none
Description:
            calls _Hw_SetWrStandby
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
void _Set_SsfdcWrStandby(void)
{
    _Hw_SetWrStandby();
}


/*<BCI>*****************************************************************
Name:   char _Check_SsfdcBusy(int time)

Parameters: Wait period for checking for busy signal: 0.1 milliseconds per unit
Returns:    return value is
            SUCCESS if the SmartCard doesn't have the Busy signal on.
            ERROR   if the SmartCard is busy for
Description:
            Checks if the SmartMedia Adapter Busy signal stays on for
            the requested wait period.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char _Check_SsfdcBusy(int time)
{
    if(! _Hw_ChkBusy())
        return(SUCCESS);
    while(time-- >= 0)
    {
        if(! _Hw_ChkBusy())
            return(SUCCESS);
        SmartWait(1);
    }
    return(ERROR);
}


/*<BCI>*****************************************************************
Name:   char _Check_SsfdcStatus(void)

Parameters: none
Returns:    return value is 0=SUCCESS, -1=ERROR
Description:
            checks current error status when a byte is sent to the SmartCard
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char _Check_SsfdcStatus(void)
{
    if(_Hw_InData() &WR_FAIL) return(ERROR);
    return(SUCCESS);
}


/*<BCI>*****************************************************************
Name:   void _Reset_SsfdcErr(void)

Parameters: none
Returns:    none
Description:
            resets the SmartCard after an error.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
void _Reset_SsfdcErr(void)
{
    word time;
    _Hw_SetRdCmd();
    _Hw_OutData(RST_CHIP);
    _Hw_SetRdData();
    time = BUSY_RESET; /* (word)((BUSY_RESET * MSEC_100)/100); */
    while(time-- > 0)
    {
        if(! _Hw_ChkBusy()) break;
        SmartWait(1);
    }
}


/*<BCI>*****************************************************************
Name:   void _Read_SsfdcBuf(byte *databuf)

Parameters: pointer to allocated memory.
Returns:    none
Description:
            Reads one page of data from SmartCard.
            Memory set to data from SmartCard.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
void _Read_SsfdcBuf(byte *databuf)
{
    _Hw_InBuf(databuf, (((Ssfdc.Attribute &MPS)==PS256)?0x0100:0x0200));
}


/*<BCI>*****************************************************************
Name:   void _Write_SsfdcBuf(byte *databuf)

Parameters: pointer to allocated memory.
Returns:    none
Description:
            writes a page of data to SmartCard.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
void _Write_SsfdcBuf(byte *databuf)
{
    _Hw_OutBuf(databuf, (((Ssfdc.Attribute &MPS)==PS256)?0x100:0x200));
}


/*<BCI>*****************************************************************
Name:   void _Read_SsfdcByte(byte *databuf)

Parameters: pointer to 1 byte of allocated memory.
Returns:    none
Description:
            Read a byte from SmartCard.
            Memory is set to one byte from SmartCard.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
void _Read_SsfdcByte(byte *databuf)
{
    *databuf=(byte)_Hw_InData();
}


/*<BCI>*****************************************************************
Name:   void _ReadRedt_SsfdcBuf(byte *redundant)

Parameters: pointer to 8 or 16 bytes of memory.
Returns:    none
Description:
            Reads in 8 or 16 bytes of redundant memory.
            Memory is set to redundant area from current page of SmartCard
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
void _ReadRedt_SsfdcBuf(byte *redundant)
{
    _Hw_InBuf(redundant, (((Ssfdc.Attribute &MPS)==PS256)?0x08:0x010));
}


/*<BCI>*****************************************************************
Name:   void _WriteRedt_SsfdcBuf(byte *redundant)

Parameters: pointer to 8 or 16 bytes of memory.
Returns:    none
Description:
            writes 8 or 16 bytes of redundant area memory to SmartCard.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
void _WriteRedt_SsfdcBuf(byte *redundant)
{
    _Hw_OutBuf(redundant, (((Ssfdc.Attribute &MPS)==PS256)?0x08:0x010));
}


/*<BCI>*****************************************************************
Name:   char Set_SsfdcModel(byte dcode)

Parameters: 1-byte ID code.
Returns:    error status: SUCCESS => valid ID code
                          ERROR   => unrecognized ID code
Description:
            sets fields of the Ssfdc struct depending on
            id code of SmartCard.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Set_SsfdcModel(byte dcode)
{
    byte devcode = _Check_DevCode(dcode);
    switch(devcode)
    {
        case SSFDC1MB:
            Ssfdc.Model = devcode;
            Ssfdc.Attribute = FLASH | AD3CYC | BS16 | PS256;
            Ssfdc.MaxZones = 1;
            Ssfdc.MaxBlocks = 256;
            Ssfdc.MaxLogBlocks = 250;
            Ssfdc.MaxSectors = 8;
            Ssfdc.BootSector = 13;
            break;
        case SSFDC2MB:
            Ssfdc.Model = devcode;
            Ssfdc.Attribute = FLASH | AD3CYC | BS16 | PS256;
            Ssfdc.MaxZones = 1;
            Ssfdc.MaxBlocks = 512;
            Ssfdc.MaxLogBlocks = 500;
            Ssfdc.MaxSectors = 8;
            Ssfdc.BootSector = 11;
            break;
        case SSFDC4MB:
            Ssfdc.Model = devcode;
            Ssfdc.Attribute = FLASH | AD3CYC | BS16 | PS512;
            Ssfdc.MaxZones = 1;
            Ssfdc.MaxBlocks = 512;
            Ssfdc.MaxLogBlocks = 500;
            Ssfdc.MaxSectors = 16;
            Ssfdc.BootSector = 27;
            break;
        case SSFDC8MB:
            Ssfdc.Model = devcode;
            Ssfdc.Attribute = FLASH | AD3CYC | BS16 | PS512;
            Ssfdc.MaxZones = 1;
            Ssfdc.MaxBlocks = 1024;
            Ssfdc.MaxLogBlocks = 1000;
            Ssfdc.MaxSectors = 16;
            Ssfdc.BootSector = 25;
            break;
        case SSFDC16MB:
            Ssfdc.Model = devcode;
            Ssfdc.Attribute = FLASH | AD3CYC | BS32 | PS512;
            Ssfdc.MaxZones = 1;
            Ssfdc.MaxBlocks = 1024;
            Ssfdc.MaxLogBlocks = 1000;
            Ssfdc.MaxSectors = 32;
            Ssfdc.BootSector = 41;
            break;
#if (MAX_ZONENUM) >= 2
      case SSFDC32MB:
          Ssfdc.Model = devcode;
          Ssfdc.Attribute = FLASH | AD3CYC | BS32 | PS512;
          Ssfdc.MaxZones = 2;
          Ssfdc.MaxBlocks = 1024;
          Ssfdc.MaxLogBlocks = 1000;
          Ssfdc.MaxSectors = 32;
          Ssfdc.BootSector = 35;
          break;
#endif
#if (MAX_ZONENUM) >= 4
      case SSFDC64MB:
          Ssfdc.Model = devcode;
          Ssfdc.Attribute = FLASH | AD4CYC | BS32 | PS512;
          Ssfdc.MaxZones = 4;
          Ssfdc.MaxBlocks = 1024;
          Ssfdc.MaxLogBlocks = 1000;
          Ssfdc.MaxSectors = 32;
          Ssfdc.BootSector = 55;
          break;
#endif
#if (MAX_ZONENUM) >= 8
      case SSFDC128MB:
          Ssfdc.Model = devcode;
          Ssfdc.Attribute = FLASH | AD4CYC | BS32 | PS512;
          Ssfdc.MaxZones = 8;
          Ssfdc.MaxBlocks = 1024;
          Ssfdc.MaxLogBlocks = 1000;
          Ssfdc.MaxSectors = 32;
          Ssfdc.BootSector = 47;
          break;
#endif
#if (MAX_ZONENUM) >= 16
      /* Not sure about these */
      case SSFDC256MB:
          Ssfdc.Model = devcode;
          Ssfdc.Attribute = FLASH | AD4CYC | BS32 | PS512;
          Ssfdc.MaxZones = 16;
          Ssfdc.MaxBlocks = 1024;
          Ssfdc.MaxLogBlocks = 1000;
          Ssfdc.MaxSectors = 32;
          Ssfdc.BootSector = 47;
          break;
#endif
#if (MAX_ZONENUM) >= 32
      /* Not sure about these */
      case SSFDC512MB:
          Ssfdc.Model = devcode;
          Ssfdc.Attribute = FLASH | AD4CYC | BS32 | PS512;
          Ssfdc.MaxZones = 32;
          Ssfdc.MaxBlocks = 1024;
          Ssfdc.MaxLogBlocks = 1000;
          Ssfdc.MaxSectors = 32;
          Ssfdc.BootSector = 47;
          break;
#endif
#if (MAX_ZONENUM) >= 64
      /* Not sure about these */
      case SSFDC1GB:
          Ssfdc.Model = devcode;
          Ssfdc.Attribute = FLASH | AD4CYC | BS64 | PS2048;
          Ssfdc.MaxZones = 64;
          Ssfdc.MaxBlocks = 1024;
          Ssfdc.MaxLogBlocks = 1000;
          Ssfdc.MaxSectors = 32;
          Ssfdc.BootSector = 47;
          break;
#endif
#if (MAX_ZONENUM) >= 128
      /* Not sure about these */
      case SSFDC2GB:
          Ssfdc.Model = devcode;
          Ssfdc.Attribute = FLASH | AD4CYC | BS64 | PS2048;
          Ssfdc.MaxZones = 128;
          Ssfdc.MaxBlocks = 1024;
          Ssfdc.MaxLogBlocks = 1000;
          Ssfdc.MaxSectors = 32;
          Ssfdc.BootSector = 47;
          break;
#endif
        default:
            Ssfdc.Model = NOSSFDC;
            return(ERROR);
    }
    return(SUCCESS);
}


/*<BCI>*****************************************************************
Name:   byte _Check_DevCode(byte dcode)

Parameters: 1-byte ID code.
Returns:    Size of SmartCard.
Description:
            decides what size SmartCard is based on 1-byte ID code
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
byte _Check_DevCode(byte dcode)
{
    switch(dcode)
    {
        case 0x6E:
        case 0xE8:
        case 0xEC: return(SSFDC1MB);              /*   8Mbit (1M) NAND */
        case 0x64:
        case 0xEA: return(SSFDC2MB);              /*  16Mbit (2M) NAND */
        case 0x6B:
        case 0xE3:
        case 0xE5: return(SSFDC4MB);              /*  32Mbit (4M) NAND */
        case 0xE6: return(SSFDC8MB);              /*  64Mbit (8M) NAND */
        case 0x73: return(SSFDC16MB);             /* 128Mbit (16M)NAND */
        case 0x75: return(SSFDC32MB);             /* 256Mbit (32M)NAND */
        case 0x76: return(SSFDC64MB);             /* 512Mbit (64M)NAND */
        case 0x79: return(SSFDC128MB);            /*   1Gbit(128M)NAND */
        default: return(NOSSFDC);
    }
}


/*<BCI>*****************************************************************
Name:   void Cnt_Reset(void)

Parameters: none
Returns:    none
Description:
            calls _Hw_SetRdStandby
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
void Cnt_Reset(void)
{
    _Hw_SetRdStandby();
}


/*<BCI>*****************************************************************
Name:   char Check_CardExist(void)

Parameters: none
Returns:     0=SUCCESS => card is in
            -1=ERROR   => no card
Description:
            checks for the SmartCard with debouncing of card contacts.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Check_CardExist(void)
{
    char i,j,k;
    if(! _Hw_ChkStatus())                         /* No Status Change */
        if(_Hw_ChkCardIn()) return(SUCCESS);      /* Card exist in Slot */
    for(i=0,j=0,k=0; i<16; i++)
    {
        if(_Hw_ChkCardIn())                       /* Status Change */
            { j++; k=0; }
            else { j=0; k++; }
            if(j>3) return(SUCCESS);              /* Card exist in Slot */
        if(k>3) return(ERROR);                    /* NO Card exist in Slot */
        SmartWait(TIME_CDCHK);
    }
    return(ERROR);
}


/*<BCI>*****************************************************************
Name:   char Check_CardStsChg(void)

Parameters: none
Returns:     0=SUCCESS => no status change
            -1=ERROR   => status change
Description:
            calls  _Hw_ChkStatus
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Check_CardStsChg(void)
{
    if(_Hw_ChkStatus())
        return(ERROR);                          /* Status Change */
    return(SUCCESS);                            /* Not Status Change */
}


/*<BCI>*****************************************************************
Name:   char Check_SsfdcWP(void)

Parameters: none
Returns:     0=SUCCESS => WP disabled
            -1=ERROR   => WP enabled
Description:
            checks if Write Protect is disabled
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Check_SsfdcWP(void)
{                                                 /* ERROR: WP, SUCCESS: Not WP */
    char i;
    for(i=0; i<8; i++)
    {
        if(_Hw_ChkWP())
            return(ERROR);
        SmartWait(TIME_WPCHK);
    }
    return(SUCCESS);
}


/*<BCI>*****************************************************************
Name:   char Check_ReadError(byte *redundant)

Parameters: pointer to copy of redundant area data.
Returns:     0=SUCCESS => match
            -1=ERROR   => discrepancy
Description:
            compare the calculated ECC data with that in the redundant area.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Check_ReadError(byte *redundant)
{
    if(! StringCmp((char *)(redundant+0x0D),(char *)EccBuf,3))
        if(! StringCmp((char *)(redundant+0x08),(char *)(EccBuf+0x03),3))
            return(SUCCESS);
    return(ERROR);
}


/*<BCI>*****************************************************************
Name:   char Check_Correct(byte *buf,byte *redundant)

Parameters: data and redundant data
Returns:     0=SUCCESS => correctable
            -1=ERROR   => fatal
Description:
            checks whether error can be corrected
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Check_Correct(byte *buf,byte *redundant)
{
    if(StringCmp((char *)(redundant+0x0D),(char *)EccBuf,3))
        if(_Correct_SwECC(buf,redundant+0x0D,EccBuf))
            return(ERROR);
    buf+=0x100;
    if(StringCmp((char *)(redundant+0x08),(char *)(EccBuf+0x03),3))
        if(_Correct_SwECC(buf,redundant+0x08,EccBuf+0x03))
            return(ERROR);
    return(SUCCESS);
}


/*<BCI>*****************************************************************
Name:   char Check_CISdata(byte *buf, byte *redundant)

Parameters: data and redundant data
Returns:     0=SUCCESS => CIS data is good.
            -1=ERROR   => CIS bad
Description:
            check if the CIS data is correct.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Check_CISdata(byte *buf, byte *redundant)
{
    const byte cis[]={ 0x01,0x03,0xD9,0x01,0xFF,0x18,0x02,0xDF,0x01,0x20 };
    if(! StringCmp((char *)(redundant+0x0D),(char *)EccBuf,3))
         return(StringCmp((char *)buf,(char *)cis,10));
    if(! _Correct_SwECC(buf,redundant+0x0D,EccBuf))
         return(StringCmp((char *)buf,(char *)cis,10));
    buf+=0x100;
    if(! StringCmp((char *)(redundant+0x08),(char *)(EccBuf+0x03),3))
         return(StringCmp((char *)buf,(char *)cis,10));
    if(! _Correct_SwECC(buf,redundant+0x08,EccBuf+0x03))
         return(StringCmp((char *)buf,(char *)cis,10));
    return(ERROR);
}


/*<BCI>*****************************************************************
Name:   void Set_RightECC(byte *redundant)

Parameters: pointer to redundant area data
Returns:    none
Description:
            Set_RightECC()is used when there is an error in copying data.
            Set the calculated ECC data
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
void Set_RightECC(byte *redundant)
{
    StringCopy((char *)(redundant+0x0D),(char *)EccBuf,3);
    StringCopy((char *)(redundant+0x08),(char *)(EccBuf+0x03),3);
}


/*<BCI>*****************************************************************
Name:   void _Calc_ECCdata(byte *buf)

Parameters: pointer to data
Returns:    ECC stored in EccBuf.
Description:
            calculates the ECC for data.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
void _Calc_ECCdata(byte *buf)
{
    _Calculate_SwECC(buf,EccBuf);
    buf+=0x0100;
    _Calculate_SwECC(buf,EccBuf+0x03);
}


/*<BCI>*****************************************************************
Name:   void _Set_ECCdata(byte add,byte *redundant)

Parameters: add ==> mode for 256 byte pages
Returns:    ECC stored in redundant area.
Description:
            sets the calculated ECC data in redundant data area.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
void _Set_ECCdata(byte add,byte *redundant)
{
    if(add==EVEN && (Ssfdc.Attribute &MPS)==PS256) return;
    /* for 256byte/page */
    StringCopy((char *)(redundant+0x0D),(char *)EccBuf,3);
    StringCopy((char *)(redundant+0x08),(char *)(EccBuf+0x03),3);
}
#endif /* (INCLUDE_SMARTMEDIA) */
