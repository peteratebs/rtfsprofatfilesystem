/******************************************************************
   File Name:        SmartMed.c

      This file contains the mid level, non-hardware dependant
      routines for the SmartMedia interface.

 ******************************************************************/
#include <rtfs.h>
#include <portconf.h>

#if (INCLUDE_SMARTMEDIA)

#include "SmartMed.h"


/********************* External Function Prototypes ************************/
word Log2PhyGet(word log_block, char zone);
void Log2PhyPut(word phy_block, word log_block, char zone);

#define Log2PhyGet(BLOCK, ZONE) Log2Phy[(ZONE)][(BLOCK)]
#define Log2PhyPut(VAL, BLOCK, ZONE) Log2Phy[(ZONE)][(BLOCK)]=(VAL)


/*********************** Local Function Prototypes *************************/
char Check_LogCHS(word *,byte *,byte *);
char Check_MediaExist(void);
char Check_MediaWP(void);
char Check_MediaFmt(void);
char Check_MediaFmtForEraseAll(void);
char Conv_MediaAddr(dword);
char Inc_MediaAddr(void);
char Check_FirstSect(void);
char Check_LastSect(void);
char Media_ReadOneSect(byte *);
char Media_WriteOneSect(byte *);
char Media_CopyBlockHead(void);
char Media_CopyBlockTail(void);
char Media_EraseOneBlock(void);
char Media_EraseAllBlock(void);

char Copy_BlockAll(char);
char Copy_BlockHead(void);
char Copy_BlockTail(void);
char Reassign_BlockHead(void);

char Assign_WriteBlock(void);
char Release_ReadBlock(void);
char Release_WriteBlock(void);

char Copy_PhyOneSect(void);
char Read_PhyOneSect(byte *);
char Write_PhyOneSect(byte *);
char Erase_PhyOneBlock(void);

char Set_PhyFmtValue(void);
char Search_CIS(void);
char Make_LogTable(void);

char MarkFail_PhyOneBlock(void);

/************************ Banked Data **************************************/
//word Log2Phy[MAX_ZONENUM][MAX_LOGBLOCK];  // Logical to Physical
                                            //   * block address look-up table. When a logical address
                                            //   * is given, the physical block address is:
                                            //   *    Log2Phy[Zone][Logical Block Address].
                                            //   * Used in Conv_MediaAddr(), Inc_MediaAddr(),
                                            //   *         Media_EraseOneBlock(), Release_ReadBlock()
                                            //   *         Make_LogTable().
word Log2Phy[2][MAX_LOGBLOCK];              // Declare only the first two zones (they're banked two at a time)


byte Assign[MAX_ZONENUM][MAX_BLOCKNUM/8];// Used to flag physical
                                         //   * blocks as being used "1" or unused "0"
/************************* Local Data **************************************/
word ErrCode;               // ErrCode is one of the following:
                            //     *  SmartMediaError_NoError 0
                            //     *  SmartMediaError_NoSmartMedia 0x003A   Medium Not Present
                            //     *  SmartMediaError_WriteFault   0x0003   Peripheral Device Write Fault
                            //     *  SmartMediaError_HwError      0x0004   Hardware Error
                            //     *  SmartMediaError_DataStatus   0x0010   DataStatus Error
                            //     *  SmartMediaError_EccReadErr   0x0011   Unrecovered Read Error
                            //     *  SmartMediaError_CorReadErr   0x0018   Recovered Read Data with ECC
                            //     *  SmartMediaError_OutOfLBA     0x0021   Illegal Logical Block Address
                            //     *  SmartMediaError_WrtProtect   0x0027   Write Protected
                            //     *  SmartMediaError_ChangedMedia 0x0028   Medium Changed
                            //     *  SmartMediaError_UnknownMedia 0x0030   Incompatible Medium Installed
                            //     *  SmartMediaError_IllegalFmt   0x0031   Medium Format Corrupted
byte SectBuf[SECTSIZE];     // Sector data buffer is used to transfer
                            //     * data between the SmartCard and this code and is
                            //     * used to make ECC corrections.
byte WorkBuf[SECTSIZE];     // Sector data buffer used as temporary
                            //     * work space.
byte Redundant[REDTSIZE];   // Sector redundant area buffer
byte WorkRedund[REDTSIZE];  // Sector redundant area buffer used as a temp space for data copying
word AssignStart[MAX_ZONENUM];           // Start address of Assign[][]
word ReadBlock;                          // Physical Read  Block in Copy mode
word WriteBlock;                         // Physical Write Block in Copy mode
word MediaChange;                        // Flag used to detect change in SmartCard presence
                                         //   * 0 => No change
                                         //   * non-zero => some change, i.e. card pulled out
word SectCopyMode;                       // Flag used to signal status of write


/*********************** BIT Controll Macro ********************************/
const byte BitData[] = { 0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80 };
#define Set_Bit(a,b) (a[(byte)((b)/8)]|= BitData[(b)%8])
#define Clr_Bit(a,b) (a[(byte)((b)/8)]&=~BitData[(b)%8])
#define Chk_Bit(a,b) (a[(byte)((b)/8)] & BitData[(b)%8])


/*<BCI>*****************************************************************
Name:   char SmartMedia_Initialize(void)

Parameters: none
Returns:    Error Status
Description: Initialize SmartMedia Adapter and software flags
             for smartmed.c
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char SmartMedia_Initialize(void)
{
     ErrCode=SmartMediaError_NoError;
     MediaChange=(word)ERROR;
     SectCopyMode=COMPLETED;
     Cnt_Reset();

     if (Check_MediaFmt())
          return SmartMediaError_IllegalFmt;

     return SmartMediaError_NoError;
}


/*<BCI>*****************************************************************
Name:   char SmartMedia_Check_Media(void)

Parameters: none
Returns:    Error Status
Description: Checks if Smartcard is present
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char SmartMedia_Check_Media(void)
{
    if(Check_MediaExist())
         return((char)ErrCode);
    return (char)SmartMediaError_NoError;
}


/*<BCI>*****************************************************************
Name:   char SmartMedia_Check_Parameter(word* cylinder, byte* head, byte* sector)

Parameters: none
Returns:    cylinder == number of cylinders
            head     == number of heads
            sector   == number of sectors
Description: check the SmartCard capacity/parameters,
             and return CHS parameters.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char SmartMedia_Check_Parameter(word*  cylinder,
                                          byte*  head,
                                          byte*  sector)
{
     *cylinder = 0;
     *head     = 0;
     *sector   = 0;

     if(Check_MediaFmt())
          return((char)ErrCode);

     if(Check_LogCHS(cylinder,head,sector))
          return((char)ErrCode);

     return (char) SmartMediaError_NoError;
}


/*<BCI>*****************************************************************
Name:   char SmartMedia_Check_Media(void)

Parameters: start: start sector
            count: number of sectors to be read
            destination: transfer buffer for storing data.
Returns:    Error Status
Description: reads data from the selected SmartCard logical address.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char SmartMedia_ReadSector(dword  start,
                                    word count,
                                    byte* destination)
{
     int i;

     if(Check_MediaFmt())
          return((char)ErrCode);

     if(Conv_MediaAddr(start))
          return((char)ErrCode);

     for(;;)
     {
          if(Media_ReadOneSect(SectBuf))
          {
                ErrCode=SmartMediaError_EccReadErr;
                return((char)ErrCode);
          }

          for(i=0;i<SECTSIZE;i++)
                *destination++=SectBuf[i];

          if(--count<=0) break;

          if(Inc_MediaAddr()) return((char)ErrCode);
     }

     return (char) SmartMediaError_NoError;
}


/*<BCI>*****************************************************************
Name:   char SmartMedia_WriteSector(dword  start, word count, byte* source)

Parameters: start: start sector for writing to
            count: number of sectors to be written
            source: points to buffer containing data to be written.
Returns:    Error Status
Description: Write data to selected SmartCard Logical addresses
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char SmartMedia_WriteSector(dword  start, word count, byte* source)
{
     int i;

     if(Check_MediaFmt())
          return((char)ErrCode);

     if(Check_MediaWP())
          return((char)ErrCode);

     if(Conv_MediaAddr(start))
          return((char)ErrCode);

     if(Check_FirstSect())
          if(Media_CopyBlockHead())
          {
                ErrCode=SmartMediaError_WriteFault;
                return((char)ErrCode);
          }

     for(;;)
     {
          if(! Check_FirstSect())
                if(Assign_WriteBlock())
                     return((char)ErrCode);

          for(i=0;i<SECTSIZE;i++)
                SectBuf[i]=*source++;

          if(Media_WriteOneSect(SectBuf))
          {
                ErrCode=SmartMediaError_WriteFault;
                return((char)ErrCode);
          }

          if(! Check_LastSect())
          {
                if(Release_ReadBlock())
                     if(ErrCode==SmartMediaError_HwError)
                     {
                          ErrCode=SmartMediaError_WriteFault;
                          return((char)ErrCode);
                     }
          }

          if(--count<=0)
                break;

          if(Inc_MediaAddr())
                return((char)ErrCode);
     }
     if(! Check_LastSect())
          return(SmartMediaError_NoError);

     if(Inc_MediaAddr())
          return((char)ErrCode);

     if (Media_CopyBlockTail())
     {
          ErrCode=SmartMediaError_WriteFault;
          return((char)ErrCode);
     }

     return (char)SmartMediaError_NoError;
}


/*<BCI>*****************************************************************
Name:   char SmartMedia_EraseBlock(dword  start, word count)

Parameters: start: start sector to be erased.
            count: number of sectors to be erased.
Returns:    Error Status
Description: erases data in selected SmartCard logical addresses.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char SmartMedia_EraseBlock(dword  start, word count)
{
     if(Check_MediaFmt())
          return((char)ErrCode);

     if(Check_MediaWP())
          return((char)ErrCode);

     if(Conv_MediaAddr(start))
          return((char)ErrCode);

     while(Check_FirstSect())
     {
          if(Inc_MediaAddr())
                return((char)ErrCode);

          if(--count<=0)
                return((char)SmartMediaError_NoError);
     }
     for(;;)
     {
          if(! Check_LastSect())
                if(Media_EraseOneBlock())
                     if(ErrCode==SmartMediaError_HwError)
                     {
                          ErrCode=SmartMediaError_WriteFault;
                          return((char)ErrCode);
                     }
          if(Inc_MediaAddr())
                return((char)ErrCode);

          if(--count<=0)
                return((char)SmartMediaError_NoError);
     }
}


/*<BCI>*****************************************************************
Name:   char SmartMedia_EraseAll(void)

Parameters: none
Returns:    Error Status
Description: erases all logical blocks of SmartCard.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char SmartMedia_EraseAll(void)
{
     if(Check_MediaFmtForEraseAll())
          return((char) ErrCode);

     if(Check_MediaWP())
          return((char) ErrCode);

     if(Media_EraseAllBlock())
          return((char) ErrCode);

     return SmartMediaError_NoError;
}


/*<BCI>*****************************************************************
Name:   char Media_OneSectWriteStart(dword start,byte *buf)

Parameters: start : start sector buf points to data to be written.
Returns:    Error Status
Description: write one sector of data at the logical sector
             address of SmartCard.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Media_OneSectWriteStart(dword start,byte *buf)
{
     int i;

     if(Check_MediaFmt())
          return((char) ErrCode);

     if(Check_MediaWP())
          return((char) ErrCode);

     if(Conv_MediaAddr(start))
          return((char) ErrCode);

     if(Check_FirstSect())
          if(Media_CopyBlockHead())
          {
                ErrCode=SmartMediaError_WriteFault;
                return((char) ErrCode);
          }
     if(! Check_FirstSect())
          if(Assign_WriteBlock())
                return((char) ErrCode);

     for(i=0;i<SECTSIZE;i++)
          SectBuf[i]=*buf++;

     if(Media_WriteOneSect(SectBuf))
     {
          ErrCode=SmartMediaError_WriteFault;
          return((char) ErrCode);
     }

     if(! Check_LastSect())
     {
          if(Release_ReadBlock())
                if(ErrCode==SmartMediaError_HwError)
                {
                     ErrCode=SmartMediaError_WriteFault;
                     return((char) ErrCode);
                }
     }
     return(SmartMediaError_NoError);
}


/*<BCI>*****************************************************************
Name:   char Media_OneSectWriteNext(byte *buf)

Parameters: buf points to data to be written.
Returns:    Error Status
Description: writes to the sector after the one that was just
             written to.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Media_OneSectWriteNext(byte *buf)
{
     int i;

     if(Inc_MediaAddr())
          return((char) ErrCode);

     if(! Check_FirstSect())
          if(Assign_WriteBlock())
                return((char) ErrCode);

     for(i=0;i<SECTSIZE;i++)
          SectBuf[i]=*buf++;

     if(Media_WriteOneSect(SectBuf))
     {
          ErrCode=SmartMediaError_WriteFault;
          return((char) ErrCode);
     }

     if(! Check_LastSect())
     {
          if(Release_ReadBlock())
                if(ErrCode==SmartMediaError_HwError)
                {
                     ErrCode=SmartMediaError_WriteFault;
                     return((char) ErrCode);
                }
     }

     return(SmartMediaError_NoError);
}


/*<BCI>*****************************************************************
Name:   char Media_OneSectWriteFlush(void)

Parameters: none
Returns:    Error Status
Description: Clean up writing after calls to
             Media_OneSectWriteStart or Media_OneSectWriteNext.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Media_OneSectWriteFlush(void)
{
     if(! Check_LastSect())
          return(SmartMediaError_NoError);

     if(Inc_MediaAddr())
          return((char) ErrCode);

     if(Media_CopyBlockTail())
     {
          ErrCode=SmartMediaError_WriteFault;
          return((char) ErrCode);
     }

     return(SmartMediaError_NoError);
}


/*<BCI>*****************************************************************
Name:   char Check_LogCHS(word *c,byte *h,byte *s)

Parameters: none
Returns:    c == number of cylinders
            h == number of heads
            s == number of sectors.
Description: sets the parameters of the logical format.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Check_LogCHS(word *c,byte *h,byte *s)
{
     switch(Ssfdc.Model)
     {
          case SSFDC1MB:   *c= 125; *h=  4; *s= 4; break;
          case SSFDC2MB:   *c= 125; *h=  4; *s= 8; break;
          case SSFDC4MB:   *c= 250; *h=  4; *s= 8; break;
          case SSFDC8MB:   *c= 250; *h=  4; *s=16; break;
          case SSFDC16MB:  *c= 500; *h=  4; *s=16; break;
          case SSFDC32MB:  *c= 500; *h=  8; *s=16; break;
          case SSFDC64MB:  *c= 500; *h=  8; *s=32; break;
          case SSFDC128MB: *c= 500; *h= 16; *s=32; break;
          case SSFDC256MB: *c= 500; *h= 32; *s=32; break;
          case SSFDC512MB: *c= 500; *h= 64; *s=32; break;
          case SSFDC1GB:   *c= 500; *h=128; *s=32; break;
          case SSFDC2GB:   *c=1000; *h=128; *s=32; break;
          default:         *c=  0; *h= 0; *s= 0;
                ErrCode=SmartMediaError_NoSmartMedia;
                return(ERROR);
     }
     return(SUCCESS);
}


/*<BCI>*****************************************************************
Name:   char Check_MediaExist(void)

Parameters: none
Returns:    Error Status
Description: checks if SmartCard is in.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Check_MediaExist(void)
{
     MediaChange = SUCCESS;
     if(Check_CardStsChg()) MediaChange=(word)ERROR;
     if(! Check_CardExist())
     {
          if(! MediaChange) return(SUCCESS);
          ErrCode=SmartMediaError_ChangedMedia;
          return(ERROR);
     }
     ErrCode=SmartMediaError_NoSmartMedia;
     return(ERROR);
}


/*<BCI>*****************************************************************
Name:   char Check_MediaWP(void)

Parameters: none
Returns:    WP status returned.
Description: checks if Write Protect is enabled
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Check_MediaWP(void)
{
     if(Ssfdc.Attribute &MWP)
     {
          ErrCode=SmartMediaError_WrtProtect;
          return(ERROR);
     }
     return(SUCCESS);
}


/*<BCI>*****************************************************************
Name:   char Check_MediaFmt(void)

Parameters: none
Returns:    Error Status
Description: Checks the physical format of the SmartCard and set up
             the look-up table.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Check_MediaFmt(void)
{
     if(! MediaChange) return(SUCCESS);

     MediaChange=(word)ERROR;
     SectCopyMode=COMPLETED;
     if(Set_PhyFmtValue())
     {
          ErrCode=SmartMediaError_UnknownMedia;
          return(ERROR);
     }

     if(Search_CIS())
     {
          ErrCode=SmartMediaError_IllegalFmt;
          return(ERROR);
     }

     if(Make_LogTable())
     {
          ErrCode=SmartMediaError_IllegalFmt;
          return(ERROR);
     }

     MediaChange=SUCCESS;
     return(SUCCESS);
}


/*<BCI>*****************************************************************
Name:   char Check_MediaFmtForEraseAll(void)

Parameters: none
Returns:    Error Status
Description: checks the physical format of the SmartCard.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Check_MediaFmtForEraseAll(void)
{
     MediaChange=(word)ERROR;
     SectCopyMode=COMPLETED;
     if(Set_PhyFmtValue())
     {
          ErrCode=SmartMediaError_UnknownMedia;
          return(ERROR);
     }

     if(Search_CIS())
     {
          ErrCode=SmartMediaError_IllegalFmt;
          return(ERROR);
     }

     return(SUCCESS);
}


/*<BCI>*****************************************************************
Name:   char Conv_MediaAddr(dword addr)

Parameters: addr - logical block address
Returns:    Error Status
Description: sets the Media struct to physical block addresses
             corresponding to selected logical block address.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Conv_MediaAddr(dword addr)
{
     dword temp = addr/Ssfdc.MaxSectors;

     Media.Sector   = (byte)(addr%Ssfdc.MaxSectors);
     Media.LogBlock = (word)(temp%Ssfdc.MaxLogBlocks);
     Media.Zone     = (byte)(temp/Ssfdc.MaxLogBlocks);

     if(Media.Zone<Ssfdc.MaxZones)
     {
          Clr_RedundantData(Redundant);
          Set_LogBlockAddr(Redundant);
          Media.PhyBlock=Log2PhyGet(Media.LogBlock,Media.Zone);
          return(SUCCESS);
     }
     ErrCode=SmartMediaError_OutOfLBA;
     return(ERROR);
}


/*<BCI>*****************************************************************
Name:   char Inc_MediaAddr(void)

Parameters: none
Returns:    Error Status
Description: steps the Media struct to a physical block addresses
             corresponding to the next logical block address.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Inc_MediaAddr(void)
{
     if(++Media.Sector<Ssfdc.MaxSectors)
          return(SUCCESS);

     Media.Sector=0;
     if(++Media.LogBlock<Ssfdc.MaxLogBlocks)
     {
          Clr_RedundantData(Redundant);
          Set_LogBlockAddr(Redundant);
          Media.PhyBlock=Log2PhyGet(Media.LogBlock,Media.Zone);
          return(SUCCESS);
     }

     Media.LogBlock=0;
     if(++Media.Zone<Ssfdc.MaxZones)
     {
          Clr_RedundantData(Redundant);
          Set_LogBlockAddr(Redundant);
          Media.PhyBlock=Log2PhyGet(Media.LogBlock,Media.Zone);
          return(SUCCESS);
     }

     Media.Zone=0;
     ErrCode=SmartMediaError_OutOfLBA;
     return(ERROR);
}


/*<BCI>*****************************************************************
Name:   char Check_FirstSect(void)

Parameters: none
Returns:    Error Status
Description: checks if the Media struct points to the first sector.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Check_FirstSect(void)
{
     if(! Media.Sector)
          return(SUCCESS);
     return(ERROR);
}


/*<BCI>*****************************************************************
Name:   char Check_LastSect(void)

Parameters: none
Returns:    Error Status
Description: checks if the Media struct points to the last sector.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Check_LastSect(void)
{
     if(Media.Sector<(Ssfdc.MaxSectors-1))
          return(ERROR);
     return(SUCCESS);
}


/*<BCI>*****************************************************************
Name:   char Media_ReadOneSect(byte *buf)

Parameters: buf points to allocated memory to be used as transfer area for
            data from the SmartCard.
Returns:    Error Status
Description: reads data in the current logical sector to memory.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Media_ReadOneSect(byte *buf)
{
     unsigned int err, retry;

     if(! Read_PhyOneSect(buf))
          return(SUCCESS);

     if(ErrCode==SmartMediaError_HwError)
          return(ERROR);

     if(ErrCode==SmartMediaError_DataStatus)
          return(ERROR);

     #ifdef RDERR_REASSIGN
     if(Ssfdc.Attribute &MWP)
     {
          if(ErrCode==SmartMediaError_CorReadErr) return(SUCCESS);
          return(ERROR);
     }
     err=ErrCode;
     for(retry=0; retry<2; retry++)
     {
          if(Copy_BlockAll((char)((err==SmartMediaError_EccReadErr)?REQ_FAIL:REQ_ERASE)))
          {
                if(ErrCode==SmartMediaError_HwError) return(ERROR);
                continue;
          }
          ErrCode=(word)err;
          if(ErrCode==SmartMediaError_CorReadErr) return(SUCCESS);
          return(ERROR);
     }
     MediaChange=(word)ERROR;
     #else
     if(ErrCode==SmartMediaError_CorReadErr) return(SUCCESS);
     #endif

     return(ERROR);
}


/*<BCI>*****************************************************************
Name:   char Media_WriteOneSect(byte *buf)

Parameters: buf points to data to be written to SmartCard.
Returns:    Error Status
Description: writes data in memory to the current logical sector.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Media_WriteOneSect(byte *buf)
{
     unsigned int retry;
     if(! Write_PhyOneSect(buf)) return(SUCCESS);
     if(ErrCode==SmartMediaError_HwError) return(ERROR);
     for(retry=1; retry<2; retry++)
     {
          if(Reassign_BlockHead())
          {
                if(ErrCode==SmartMediaError_HwError) return(ERROR);
                continue;
          }
          if(! Write_PhyOneSect(buf)) return(SUCCESS);
          if(ErrCode==SmartMediaError_HwError) return(ERROR);
     }
     if(Release_WriteBlock()) return(ERROR);
     ErrCode=SmartMediaError_WriteFault;
     MediaChange=(word)ERROR;
     return(ERROR);
}


/*<BCI>*****************************************************************
Name:   char Media_CopyBlockHead(void)

Parameters: none
Returns:    Error Status
Description: calls Copy_BlockHead() with retries.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Media_CopyBlockHead(void)
{
     unsigned int retry;
     for(retry=0; retry<2; retry++)
     {
          if(! Copy_BlockHead())
                return(SUCCESS);
          if(ErrCode==SmartMediaError_HwError)
                return(ERROR);
     }

     MediaChange=(word)ERROR;
     return(ERROR);
}


/*<BCI>*****************************************************************
Name:   char Media_CopyBlockTail(void)

Parameters: none
Returns:    Error Status
Description: calls Copy_BlockTail() with retries.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Media_CopyBlockTail(void)
{
     unsigned int retry;

     if(! Copy_BlockTail())
          return(SUCCESS);

     if(ErrCode==SmartMediaError_HwError)
          return(ERROR);

     for(retry=1; retry<2; retry++)
     {
          if(Reassign_BlockHead())
          {
                if(ErrCode==SmartMediaError_HwError) return(ERROR);
                continue;
          }
          if(! Copy_BlockTail())
                return(SUCCESS);
          if(ErrCode==SmartMediaError_HwError)
                return(ERROR);
     }
     if(Release_WriteBlock())
          return(ERROR);

     ErrCode=SmartMediaError_WriteFault;
     MediaChange=(word)ERROR;
     return(ERROR);
}


/*<BCI>*****************************************************************
Name:   char Media_EraseOneBlock(void)

Parameters: none
Returns:    Error Status
Description: erases current logical block.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Media_EraseOneBlock(void)
{
     if(Media.PhyBlock==NO_ASSIGN)
          return(SUCCESS);

     Log2PhyPut(NO_ASSIGN, Media.LogBlock,Media.Zone);
     if(Erase_PhyOneBlock())
     {
          if(ErrCode==SmartMediaError_HwError)
                return(ERROR);
          if(MarkFail_PhyOneBlock())
                return(ERROR);

          ErrCode=SmartMediaError_WriteFault;
          return(ERROR);
     }
     Clr_Bit(Assign[Media.Zone],Media.PhyBlock);
     Media.PhyBlock=NO_ASSIGN;
     return(SUCCESS);
}


/*<BCI>*****************************************************************
Name:   char Media_EraseAllBlock(void)

Parameters: none
Returns:    Error Status
Description: erases all logical blocks.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Media_EraseAllBlock(void)
{
     word cis=0;

     MediaChange=(word)ERROR;
     Media.Sector=0;
     for(Media.Zone=0; Media.Zone<Ssfdc.MaxZones; Media.Zone++)
          for(Media.PhyBlock=0; Media.PhyBlock<Ssfdc.MaxBlocks; Media.PhyBlock++)
          {
                if(Ssfdc_ReadRedtData(Redundant))
                {
                     Ssfdc_Reset();
                     return(ERROR);
                }
                Ssfdc_Reset();
                if(! Check_FailBlock(Redundant))
                {
                     if(cis)
                     {
                          if(Ssfdc_EraseBlock())
                          {
                                ErrCode=SmartMediaError_HwError;
                                return(ERROR);
                          }
                          if(Ssfdc_CheckStatus())
                          {
                                if(MarkFail_PhyOneBlock())
                                     return(ERROR);
                          }
                          continue;
                     }
                     if(Media.PhyBlock!=CisArea.PhyBlock)
                     {
                          ErrCode=SmartMediaError_IllegalFmt;
                          return(ERROR);
                     }
                     cis++;
                }
          }

     return(SUCCESS);
}


/*<BCI>*****************************************************************
Name:   char Copy_BlockAll(char mode)

Parameters: mode: REQ_FAIL  => failure in blocks
                  REQ_ERASE => erase blocks
Returns:    Error Status
Description: transfer ReadBlock sectors to WriteBlock.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Copy_BlockAll(char mode)
{
     byte sect = Media.Sector;

     if(Assign_WriteBlock())
          return(ERROR);

     if(mode==REQ_FAIL) SectCopyMode=REQ_FAIL;
     for(Media.Sector=0; Media.Sector<Ssfdc.MaxSectors; Media.Sector++)
          if(Copy_PhyOneSect())
          {
                if(ErrCode==SmartMediaError_HwError)
                     return(ERROR);
                if(Release_WriteBlock())
                     return(ERROR);
                ErrCode=SmartMediaError_WriteFault;
                Media.PhyBlock=ReadBlock;
                Media.Sector=sect;
                return(ERROR);
          }

     if(Release_ReadBlock())
          return(ERROR);

     Media.PhyBlock=WriteBlock;
     Media.Sector=sect;
     return(SUCCESS);
}


/*<BCI>*****************************************************************
Name:   char Copy_BlockHead(void)

Parameters: none
Returns:    Error Status
Description: allocate unused block to WriteBlock, transfers data
             from (current sector to Media.Sector-1) in ReadBlock to
             WriteBlock.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Copy_BlockHead(void)
{
     byte sect;
     sect=Media.Sector;
     if(Assign_WriteBlock()) return(ERROR);
     for(Media.Sector=0; Media.Sector<sect; Media.Sector++)
          if (Copy_PhyOneSect())
          {
                if(ErrCode==SmartMediaError_HwError)
                     return(ERROR);
                if(Release_WriteBlock())
                     return(ERROR);
                ErrCode=SmartMediaError_WriteFault;
                Media.PhyBlock=ReadBlock;
                Media.Sector=sect;
                return(ERROR);
          }

     Media.PhyBlock=WriteBlock;
     Media.Sector=sect;
     return(SUCCESS);
}


/*<BCI>*****************************************************************
Name:   char Copy_BlockTail(void)

Parameters: none
Returns:    Error Status
Description: transfers data from Media.Sector to the end Sector in
             ReadBlock to WriteBlock.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Copy_BlockTail(void)
{
     byte sect;
     for(sect=Media.Sector; Media.Sector<Ssfdc.MaxSectors; Media.Sector++)
          if(Copy_PhyOneSect())
          {
                if(ErrCode==SmartMediaError_HwError) return(ERROR);
                Media.PhyBlock=WriteBlock;
                Media.Sector=sect;
                return(ERROR);
          }

     if(Release_ReadBlock())
          return(ERROR);

     Media.PhyBlock=WriteBlock;
     Media.Sector=sect;
     return(SUCCESS);
}


/*<BCI>*****************************************************************
Name:   char Reassign_BlockHead(void)

Parameters: none
Returns:    Error Status
Description: allocates unused block, transfers data from
             (current logical sector to Media.Sector-1) in WriteBlock
             to new block.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Reassign_BlockHead(void)
{
     unsigned int mode;
     word block;
     byte sect;

     mode=SectCopyMode;
     block=ReadBlock;
     sect=Media.Sector;
     if(Assign_WriteBlock())
          return(ERROR);

     SectCopyMode=REQ_FAIL;
     for(Media.Sector=0; Media.Sector<sect; Media.Sector++)
          if(Copy_PhyOneSect())
          {
                if(ErrCode==SmartMediaError_HwError)
                     return(ERROR);
                if(Release_WriteBlock())
                     return(ERROR);
                ErrCode=SmartMediaError_WriteFault;
                SectCopyMode=(word)mode;
                WriteBlock=ReadBlock;
                ReadBlock=block;
                Media.Sector=sect;
                Media.PhyBlock=WriteBlock;
                return(ERROR);
          }

     if(Release_ReadBlock())
          return(ERROR);

     SectCopyMode=(word)mode;
     ReadBlock=block;
     Media.Sector=sect;
     Media.PhyBlock=WriteBlock;
     return(SUCCESS);
}


/*<BCI>*****************************************************************
Name:   char Assign_WriteBlock(void)

Parameters: none
Returns:    Error Status
Description: assigns unused block to WriteBlock
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Assign_WriteBlock(void)
{
     ReadBlock=Media.PhyBlock;
     for(WriteBlock=AssignStart[Media.Zone]; WriteBlock<Ssfdc.MaxBlocks; WriteBlock++)
     {
          if(! Chk_Bit(Assign[Media.Zone],WriteBlock))
          {
                Set_Bit(Assign[Media.Zone],WriteBlock);
                AssignStart[Media.Zone]=(word)(WriteBlock+1);
                Media.PhyBlock=WriteBlock;
                SectCopyMode=REQ_ERASE;
                return(SUCCESS);
          }
     }

     for(WriteBlock=0; WriteBlock<AssignStart[Media.Zone]; WriteBlock++)
     {
          if(! Chk_Bit(Assign[Media.Zone],WriteBlock))
          {
                Set_Bit(Assign[Media.Zone],WriteBlock);
                AssignStart[Media.Zone]=(word)(WriteBlock+1);
                Media.PhyBlock=WriteBlock;
                SectCopyMode=REQ_ERASE;
                return(SUCCESS);
          }
     }

     WriteBlock=NO_ASSIGN;
     ErrCode=SmartMediaError_WriteFault;
     return(ERROR);
}


/*<BCI>*****************************************************************
Name:   char Release_ReadBlock(void)

Parameters: none
Returns:    Error Status
Description: erases ReadBlock.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Release_ReadBlock(void)
{
     unsigned int mode = SectCopyMode;

     SectCopyMode=COMPLETED;
     if(mode==COMPLETED)
          return(SUCCESS);

     Log2PhyPut(WriteBlock, Media.LogBlock,Media.Zone);
     Media.PhyBlock=ReadBlock;
     if(Media.PhyBlock==NO_ASSIGN)
     {
          Media.PhyBlock=WriteBlock;
          return(SUCCESS);
     }
     if(mode==REQ_ERASE)
     {
          if(Erase_PhyOneBlock())
          {
                if(ErrCode==SmartMediaError_HwError)
                     return(ERROR);
                if(MarkFail_PhyOneBlock())
                     return(ERROR);
          }
          else
                Clr_Bit(Assign[Media.Zone],Media.PhyBlock);
     }
     else
          if(MarkFail_PhyOneBlock())
                return(ERROR);

     Media.PhyBlock=WriteBlock;
     return(SUCCESS);
}


/*<BCI>*****************************************************************
Name:   char Release_WriteBlock(void)

Parameters: none
Returns:    Error Status
Description: erases WriteBlock => marked as fail.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Release_WriteBlock(void)
{
     SectCopyMode=COMPLETED;
     Media.PhyBlock=WriteBlock;
     if(MarkFail_PhyOneBlock()) return(ERROR);
     Media.PhyBlock=ReadBlock;
     return(SUCCESS);
}


/*<BCI>*****************************************************************
Name:   char Copy_PhyOneSect(void)

Parameters: none
Returns:    Error Status
Description: copies data in physical sector.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Copy_PhyOneSect(void)
{
     int i;
     unsigned int retry;
     int err;

	 err=ERROR; // Initialize


     if(ReadBlock!=NO_ASSIGN)
     {
          Media.PhyBlock=ReadBlock;
          for(retry=0; retry<2; retry++)
          {
                if(retry!=0)
                {
                     Ssfdc_Reset();
                     if(Ssfdc_ReadCisSect(WorkBuf,WorkRedund))
                     {
                          ErrCode=SmartMediaError_HwError;
                          MediaChange=(word)ERROR;
                          return(ERROR);
                     }
                     if(Check_CISdata(WorkBuf,WorkRedund))
                     {
                          ErrCode=SmartMediaError_HwError;
                          MediaChange=(word)ERROR;
                          return(ERROR);
                     }
                }
                if(Ssfdc_ReadSect(WorkBuf,WorkRedund))
                {
                     ErrCode=SmartMediaError_HwError;
                     MediaChange=(word)ERROR;
                     return(ERROR);
                }
                if(Check_DataStatus(WorkRedund))
                {
                     err=ERROR;
                     break;
                }
                if(! Check_ReadError(WorkRedund))
                {
                     err=SUCCESS;
                     break;
                }
                if(! Check_Correct(WorkBuf,WorkRedund))
                {
                     err=SUCCESS;
                     break;
                }
                err=ERROR;
                SectCopyMode=REQ_FAIL;
          }
     }
     else
     {
          err=SUCCESS;
          for(i=0; i<SECTSIZE; i++)
                WorkBuf[i]=DUMMY_DATA;
          Clr_RedundantData(WorkRedund);
     }
     Set_LogBlockAddr(WorkRedund);
     if(err==ERROR)
     {
          Set_RightECC(WorkRedund);
          Set_DataStaus(WorkRedund);
     }
     Media.PhyBlock=WriteBlock;
     if(Ssfdc_WriteSectForCopy(WorkBuf,WorkRedund))
     {
          ErrCode=SmartMediaError_HwError;
          MediaChange=(word)ERROR;
          return(ERROR);
     }
     if(Ssfdc_CheckStatus())
     {
          ErrCode=SmartMediaError_WriteFault;
          return(ERROR);
     }
     Media.PhyBlock=ReadBlock;
     return(SUCCESS);
}


/*<BCI>*****************************************************************
Name:   char Read_PhyOneSect(byte *buf)

Parameters: buf points to data buffer for bytes that are read
Returns:    Error Status
Description: read data from physical sector
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Read_PhyOneSect(byte *buf)
{
     int i;
     unsigned int retry;
     if(Media.PhyBlock==NO_ASSIGN)
     {
          for(i=0; i<SECTSIZE; i++)
                *buf++=DUMMY_DATA;
          return(SUCCESS);
     }
     for(retry=0; retry<2; retry++)
     {
          if(retry!=0)
          {
                Ssfdc_Reset();
                if(Ssfdc_ReadCisSect(WorkBuf,WorkRedund))
                {
                     ErrCode=SmartMediaError_HwError;
                     MediaChange=(word)ERROR;
                     return(ERROR);
                }
                if(Check_CISdata(WorkBuf,WorkRedund))
                {
                     ErrCode=SmartMediaError_HwError;
                     MediaChange=(word)ERROR;
                     return(ERROR);
                }
          }
          if(Ssfdc_ReadSect(buf,Redundant))
          {
                ErrCode=SmartMediaError_HwError;
                MediaChange=(word)ERROR;
                return(ERROR);
          }
          if(Check_DataStatus(Redundant))
          {
                ErrCode=SmartMediaError_DataStatus;
                return(ERROR);
          }
          if(! Check_ReadError(Redundant)) return(SUCCESS);
          if(! Check_Correct(buf,Redundant))
          {
                ErrCode=SmartMediaError_CorReadErr;
                return(ERROR);
          }
     }
     ErrCode=SmartMediaError_EccReadErr;
     return(ERROR);
}


/*<BCI>*****************************************************************
Name:   char Write_PhyOneSect(byte *buf)

Parameters: buf points to data to be written.
Returns:    Error Status
Description: writes data to physical sector
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Write_PhyOneSect(byte *buf)
{
     if(Ssfdc_WriteSect(buf,Redundant))
     {
         ErrCode=SmartMediaError_HwError;
         MediaChange=(word)ERROR;
         return(ERROR);
     }
     if(Ssfdc_CheckStatus())
     {
         ErrCode=SmartMediaError_WriteFault;
         return(ERROR);
     }
     return(SUCCESS);
}


/*<BCI>*****************************************************************
Name:   char Erase_PhyOneBlock(void)

Parameters: none
Returns:    Error Status
Description: erase a physical block.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Erase_PhyOneBlock(void)
{
     if(Ssfdc_EraseBlock())
     {
         ErrCode=SmartMediaError_HwError;
         MediaChange=(word)ERROR;
         return(ERROR);
     }
     if(Ssfdc_CheckStatus())
     {
         ErrCode=SmartMediaError_WriteFault;
         return(ERROR);
     }
     return(SUCCESS);
}


/*<BCI>*****************************************************************
Name:   char Set_PhyFmtValue(void)

Parameters: none
Returns:    Error Status
Description: sets Ssfdc struct based on SmartCard parameters.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Set_PhyFmtValue(void)
{
     byte idcode[4];
     Ssfdc_ReadID(idcode);
     if(Set_SsfdcModel(idcode[1]))
          return(ERROR);
     if(Check_SsfdcWP())
          Ssfdc.Attribute|=WP;
     return(SUCCESS);
}


/*<BCI>*****************************************************************
Name:   char Search_CIS(void)

Parameters: none
Returns:    Error Status
Description: searches for CIS information in SmartCard.
             Then Cis struct is initialized.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Search_CIS(void)
{
     Media.Zone=0; Media.Sector=0;
     for(Media.PhyBlock=0; Media.PhyBlock<(Ssfdc.MaxBlocks-Ssfdc.MaxLogBlocks-1); Media.PhyBlock++)
     {
          if(Ssfdc_ReadRedtData(Redundant))
          {
              Ssfdc_Reset();
              return(ERROR);
          }
          if(! Check_FailBlock(Redundant))
                break;
     }
     if(Media.PhyBlock==(Ssfdc.MaxBlocks-Ssfdc.MaxLogBlocks-1))
     {
          Ssfdc_Reset();
          return(ERROR);
     }
     while(Media.Sector<CIS_SEARCH_SECT)
     {
          if(Media.Sector)
                if(Ssfdc_ReadRedtData(Redundant))
                {
                     Ssfdc_Reset();
                     return(ERROR);
                }
                if(! Check_DataStatus(Redundant))
                {
                     if(Ssfdc_ReadSect(WorkBuf,Redundant))
                     {
                          Ssfdc_Reset();
                          return(ERROR);
                     }
                     if(Check_CISdata(WorkBuf,Redundant))
                     {
                          Ssfdc_Reset();
                          return(ERROR);
                     }
                     CisArea.PhyBlock=Media.PhyBlock;
                     CisArea.Sector=Media.Sector;
                     Ssfdc_Reset();
                     return(SUCCESS);
                }
          Media.Sector++;
     }
     Ssfdc_Reset();
     return(ERROR);
}


/*<BCI>*****************************************************************
Name:   char Make_LogTable(void)

Parameters: none
Returns:    Error Status
Description: Initializes the Log2Phy table, a look-up table for
             converting from logical addresses to physical ones.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char Make_LogTable(void)
{
     word phyblock,logblock;

     Media.Sector=0;
     for(Media.Zone=0; Media.Zone<Ssfdc.MaxZones; Media.Zone++) // process each zone
     {
          for(Media.LogBlock=0; Media.LogBlock<Ssfdc.MaxLogBlocks; Media.LogBlock++)
                Log2PhyPut(NO_ASSIGN, Media.LogBlock,Media.Zone); // mark all log blocks unassigned
          for(Media.PhyBlock=0; Media.PhyBlock<(MAX_BLOCKNUM/8); Media.PhyBlock++)
                Log2PhyPut(0x00, Media.LogBlock,Media.Zone);    // mark all phy blocks empty
          for(Media.PhyBlock=0; Media.PhyBlock<Ssfdc.MaxBlocks; Media.PhyBlock++)
          {                                                     // check each phy block on the smartmedia
                if((! Media.Zone) && (Media.PhyBlock<=CisArea.PhyBlock))
                {                                               // mark all CIS blocks as used (zone 0 only)
                     Set_Bit(Assign[Media.Zone],Media.PhyBlock);
                     continue;
                }
                if(Ssfdc_ReadRedtData(Redundant))               // read the redundant area of the sector
                {
                    Ssfdc_Reset();
                    return(ERROR);
                }
                if(! Check_DataBlank(Redundant))                // if it's blank then leave it unassigned
                     continue;
                Set_Bit(Assign[Media.Zone],Media.PhyBlock);     // if it's non-blank then assign it
                if(Check_FailBlock(Redundant))                  // if it's faulty then leave it un-mapped
                     continue;
                if(Load_LogBlockAddr(Redundant))                // if you can't get a log address leave it unmapped
                     continue;
                if(Media.LogBlock>=Ssfdc.MaxLogBlocks)          // if we're out of log space leave it unmapped
                     continue;
                if(Log2PhyGet(Media.LogBlock,Media.Zone)==NO_ASSIGN)
                {                                               // if it's not already mapped then map it and leave
                     Log2PhyPut(Media.PhyBlock, Media.LogBlock,Media.Zone);
                     continue;
                }
                phyblock=Media.PhyBlock;                        // somethings wrong: two phys for one log
                logblock=Media.LogBlock;
                Media.Sector=(byte)(Ssfdc.MaxSectors-1);
                if(Ssfdc_ReadRedtData(Redundant))               // re-read the redundant data from other end of block
                {
                    Ssfdc_Reset();
                    return(ERROR);
                }
                if(! Load_LogBlockAddr(Redundant))              // if there is a log address there
                    if(Media.LogBlock==logblock)                // compare with the first one
                    {
                         Media.PhyBlock=Log2PhyGet(Media.LogBlock,Media.Zone);
                         if(Ssfdc_ReadRedtData(Redundant))      // read the redundant data from end of first phy
                         {
                              Ssfdc_Reset();
                              return(ERROR);
                         }
                         Media.PhyBlock=phyblock;
                         if(! Load_LogBlockAddr(Redundant))
                         {
                              if(Media.LogBlock!=logblock)      // no good first phy then use 2nd
                              {
                                    Media.PhyBlock=Log2PhyGet(Media.LogBlock,Media.Zone);
                                    Log2PhyPut(phyblock, Media.LogBlock,Media.Zone);
                              }
                         }
                         else                                   // no good first phy them use 2nd
                         {
                              Media.PhyBlock=Log2PhyGet(Media.LogBlock,Media.Zone);
                              Log2PhyPut(phyblock, Media.LogBlock,Media.Zone);
                         }
                    }
                Media.Sector=0;
                #ifdef L2P_ERR_ERASE                            // erase the bad phy mapped to the same log
                if(!(Ssfdc.Attribute &MWP))
                {
                     Ssfdc_Reset();
                     if(Ssfdc_EraseBlock()) return(ERROR);
                     if(Ssfdc_CheckStatus())
                     {
                         if(MarkFail_PhyOneBlock())
                             return(ERROR);
                     }
                     else Clr_Bit(Assign[Media.Zone],Media.PhyBlock);
                }
                #else                                           // mark as protected any bad phy mapped to the same log
                Ssfdc.Attribute|=MWP;
                #endif
                Media.PhyBlock=phyblock;
          }
          AssignStart[Media.Zone]=0;
     }
     Ssfdc_Reset();
     return(SUCCESS);
}


/*<BCI>*****************************************************************
Name:   char MarkFail_PhyOneBlock(void)

Parameters: none
Returns:    Error Status
Description: marks block status flag of a fail block.
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
char MarkFail_PhyOneBlock(void)
{
     byte sect;
     sect=Media.Sector;
     Set_FailBlock(WorkRedund);
     Ssfdc_WriteRedtMode();
     for(Media.Sector=0; Media.Sector<Ssfdc.MaxSectors; Media.Sector++)
          if(Ssfdc_WriteRedtData(WorkRedund))
          {
              Ssfdc_Reset();
              Media.Sector=sect;
              ErrCode=SmartMediaError_HwError;
              MediaChange=(word)ERROR;
              return(ERROR);
          }                                             /* NO Status Check */
     Ssfdc_Reset();
     Media.Sector=sect;
     return(SUCCESS);
}

#endif /* (INCLUDE_SMARTMEDIA) */
