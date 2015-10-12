/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*
* Some portions Copyright (c) 1998,2000 * On-Time software
*  With Permission
*
* drflsftl.c - Flash file translation layer
*/

#include "rtfs.h"
#if (INCLUDE_FLASH_FTL)
#include "drflash.h"

/* Set INCLUDE_SECTOR_MAP to 1 to use a ram based sector map to
   speed up sector mapping.
   The sector map requires entry for each 512 byte block in the
   flash. For example a 1 megabyte flash contains 2048 512 byte
   data blocks so the sector map must contain at least 2048 elements.
   Each element is 4 bytes, so this would require 8 K.

   set USE_STATIC_SECTOR_MAP to 1 if you wish to use a static sector map
   buffer.
   If USE_STATIC_SECTOR_MAP enabled then you must set the constant
   STATIC_SECTOR_MAP_SIZE to at least the number of 512 byte blocks
   in your device. This will force the Sector map to be declared
   as a static array StaticSectorMap[STATIC_SECTOR_MAP_SIZE];

   Either USE_DYNAMIC_SECTOR_MAP or USE_STATIC_SECTOR_MAP must
   be set to 1, but not both.


   set USE_DYNAMIC_SECTOR_MAP to 1 if you wish to dynamically allocate
   the sector map at startup. If this is set to 1, malloc() called to
   allocate the sector map. If your runtime environment uses a different
   dynamic memory allocator you must modify the source code to call that
   function. There is one malloc call.

   If INCLUDE_SECTOR_MAP is enabled and the malloc call failed in
   dynamic mode or if in static mode the value STATIC_SECTOR_MAP_SIZE is
   too small the the device WARMSTART routine will fail.



*/


#define INCLUDE_SECTOR_MAP 1
#define USE_DYNAMIC_ALLOCATION 1

#if(USE_DYNAMIC_ALLOCATION)
#define USE_STATIC_SECTOR_MAP 0
#define USE_DYNAMIC_SECTOR_MAP 1
#else
#define USE_STATIC_SECTOR_MAP 1
#define USE_DYNAMIC_SECTOR_MAP 0
#endif

#if(INCLUDE_SECTOR_MAP)
/* Use an else here.. this will cause a compile error later
   if both DYNAMIC and STATIC are on */
#if (USE_STATIC_SECTOR_MAP)
#define STATIC_SECTOR_MAP_SIZE 2048 //this corresponds to the FLASHEMUTOTALSIZE in drflsmtd.c
PhysicalSectorMap StaticSectorMap[STATIC_SECTOR_MAP_SIZE];
#endif
#endif /* #if (INCLUDE_SECTOR_MAP) */



int       mtd_MountDevice  (RTFDrvFlashData * DriveData, RTF_MTD_FlashInfo * FlashInfo);
void *    mtd_MapWindow    (RTFDrvFlashData * DriveData, dword BlockIndex, dword WindowIndex);
int       mtd_EraseBlock   (RTFDrvFlashData * DriveData, dword BlockIndex);
int       mtd_ProgramData (RTFDrvFlashData * DriveData, byte * Address, byte * Data, unsigned int Length);
RTFDrvFlashData FlashData;
static int  ReadSectors(void * DriveData, dword Sector, dword Sectors, void * Buffer);
static int  WriteSectors(void * DriveData, dword Sector, dword Sectors, void * Buffer);
int  LowLevelFormat(void * DriveData, const char * DeviceName, dword Flags);
static int  MapSectors(void * DriveData);
static dword  LocateSector(RTFDrvFlashData * D, dword LogicalSector);


/* BOOLEAN flashdisk_io(dword block, void *buffer, word count, BOOLEAN reading)
*
*   Perform io to and from the flashdisk.
*
*   If the reading flag is true copy data from the flashdisk (read).
*   else copy to the flashdisk. (write). called by pc_gblock and pc_pblock
*
*   This routine is called by pc_rdblock.
*
*/
BOOLEAN flashdisk_io(int driveno, dword block, void  *buffer, word count, BOOLEAN reading) /*__fn__*/
{

    RTFS_ARGSUSED_INT(driveno);
    if(reading)
    {
        if(ReadSectors(&FlashData, (dword)block, count, buffer)==0)
            return(TRUE);
    }
    else
    {
        if(WriteSectors(&FlashData, (dword)block, count, buffer)==0)
            return(TRUE);
    }
    return(FALSE);
}
/*  With Permission                              Copyright (c) 1998,2000 *
*  Version: 2.0                                 On Time Informatik GmbH  */

/* Uncomment this line to disable debug mode */
#define NODEBUG

/* Uncomment this line for extra diagnostics */
#define LOGGING

#ifndef NODEBUG
   #define DEBUG
#else
   #ifdef LOGGING
      #undef LOGGING
   #endif
#endif

#define RTF_DISK_FULL -2
#define RTF_DATA_ERROR -3
#define RTF_DEVICE_RESOURCE_ERROR -4

/* Error reporting macros. See exception_trap() in flashdrv.c */
#define XRAISE(e)   exception_trap(0, __LINE__, e)
void exception_trap( int message, int line_no, int e)
{
char *pm;
        RTFS_ARGSUSED_INT(message);
        if (e == RTF_DISK_FULL)
         pm = "DISK FULL"; /* "DISK FULL" */
        else if (e == RTF_DATA_ERROR)
        pm = "DATA ERROR"; /* "DATA ERROR" */
        else if (e == RTF_DEVICE_RESOURCE_ERROR)
        pm = "DEVICE RESOURCE ERROR"; /* "DEVICE RESOURCE ERROR" */
        else /* if (e == RTF_INTERNAL_ERROR) */
        pm = "INTERNAL ERROR"; /* "INTERNAL ERROR" */
        RTFS_PRINT_STRING_1((byte *)pm, 0);
        RTFS_PRINT_STRING_1((byte *)"Flash: Exception at line ", 0); /* "Flash: Exception at line " */
        RTFS_PRINT_LONG_1((dword) line_no, PRFLG_NL);
}

#ifdef DEBUG
   #define ERASE_THRESHOLD            10
#else
   #define ERASE_THRESHOLD          1000
#endif

#define INVALID_BLOCK_INDEX   0xFFFFFFFF
#define INVALID_SECTOR_INDEX  0x00FFFFFF /* we only have 24 bits */
#define INVALID_ERASE_COUNT   0x00FFFFFF /* ditto */
#define MAX_ERASE_COUNT       (INVALID_ERASE_COUNT-1)

/* block state Ids */

#define BLOCK_VIRGINE   0xFF
#define BLOCK_DATA      0xE0 /* (BLOCK_VIRGINE  & ~0x1F) */
#define BLOCK_COPYING   0xC0 /*(BLOCK_DATA     & ~0x20) */
#define BLOCK_SPARE     0x80 /* (BLOCK_COPYING  & ~0x40) */

/* sector status Ids */

#define SECTOR_AVAIL    0xFF
#define SECTOR_WRITING  0xE0 /* (SECTOR_AVAIL   & ~0x1F) */
#define SECTOR_VALID    0xC0 /* (SECTOR_WRITING & ~0x20) */
#define SECTOR_DELETED  0x80 /* (SECTOR_VALID   & ~0x40) */

/* b0 b1 b2 | b3
   Count    | Id

     b3       b2    b1    b0
   31..24 | 23..16 15..8 7..0
   Id     | Count

*/

#pragma pack(1)
dword ThreeByteToDword(byte *threebyte)
{
dword d;
byte ltb[4];
    ltb[0] = *threebyte++;
    ltb[1] = *threebyte++;
    ltb[2] = *threebyte++;
    ltb[3] = 0;
    d = to_DWORD (ltb);
    return(d);
}

#define GetPhysicalSector(X) ThreeByteToDword((X).PhysicalSectorAddr)
#define GetMustUpdate(X) (dword) ((X).MustUpdateAddr[0])

void SetPhysicalSector(byte *threebyte, dword d)
{
byte ltb[4];
    fr_DWORD (ltb,d);
    *threebyte++ = ltb[0];
    *threebyte++ = ltb[1];
    *threebyte++ = ltb[2];
}


typedef struct {
      byte EraseCountAddr[3];
      byte BlockIdAddr[1];
} BlockHeader;

#define GetEraseCount(X) ThreeByteToDword((X).EraseCountAddr)
#define GetBlockId(X) (dword) ((X).BlockIdAddr[0])

void SetEraseCount(byte *threebyte, dword d)
{
byte ltb[4];
    fr_DWORD (ltb,d);
    *threebyte++ = ltb[0];
    *threebyte++ = ltb[1];
    *threebyte++ = ltb[2];
}



typedef struct {
       byte LogicalSectorAddr[3];
      byte SectorStatusAddr[1];
 } SectorInfo;

#define GetLogicalSector(X) ThreeByteToDword((X).LogicalSectorAddr)

void SetLogicalSector(byte *threebyte, dword d)
{
byte ltb[4];
    fr_DWORD (ltb,d);
    *threebyte++ = ltb[0];
    *threebyte++ = ltb[1];
    *threebyte++ = ltb[2];
}

#define GetSectorStatus(X) (byte) ((X).SectorStatusAddr[0])

typedef struct {
   BlockHeader Header;
   SectorInfo  SectorMap[1]; /* open array */
} FlashHeader;

/* #pragma pack() */

/*-----------------------------------*/
static void *  MapWindow(RTFDrvFlashData * D, dword Block, dword Window)
{
   void * Temp = mtd_MapWindow(D, Block, Window);
   return Temp;
}

/*-----------------------------------*/
static void  EraseBlock(RTFDrvFlashData * D, dword Block)
{
   int Result = mtd_EraseBlock(D, Block);
   if (Result < 0)
      XRAISE(Result);
}

/*-----------------------------------*/
static void  ProgramData(RTFDrvFlashData * D, void * Address, void * Data, unsigned int Length)
{
   int Result = mtd_ProgramData(D, (byte *)Address,(byte *)Data, Length);
   if (Result < 0)
      XRAISE(Result);
}

/*-----------------------------------*/
static dword  LocateSector(RTFDrvFlashData * D, dword LogicalSector)
/* translate a logical to a physical sector index */
{
   if (D->SectorMap)
      return (GetPhysicalSector(D->SectorMap[LogicalSector]));
   else
   {
      dword i, j;
      FlashHeader * H;
      for (i=0; i<D->FlashInfo.TotalBlocks; i++)
      {
         H = (FlashHeader *) MapWindow(D, i, 0);
         switch (GetBlockId(H->Header))
         {
            case BLOCK_SPARE:
               break;
            case BLOCK_DATA:
               for (j=0; j<D->SectorsPerBlock; j++)
                switch (GetSectorStatus(H->SectorMap[j]))
                  {
                     case SECTOR_AVAIL:
                        goto NextBlock;
                     case SECTOR_VALID:
                        if (GetLogicalSector(H->SectorMap[j]) == LogicalSector)
                           return i * D->SectorsPerBlock + j;
                        break;
                     case SECTOR_DELETED:
                        break;
                     default:
                        XRAISE(RTF_DATA_ERROR);
                  }
NextBlock:     break;
            default:
               XRAISE(RTF_DATA_ERROR);
         }
      }
   }
   return INVALID_SECTOR_INDEX;
}

/*-----------------------------------*/
static dword  CheckAvail(RTFDrvFlashData * D, dword BlockIndex)
{
   FlashHeader * H;
   dword j;

   if (BlockIndex >= D->FlashInfo.TotalBlocks)
      return INVALID_SECTOR_INDEX;

   H = (FlashHeader *)MapWindow(D, BlockIndex, 0);
   switch (GetBlockId(H->Header))
   {
      case BLOCK_SPARE:
         break;
      case BLOCK_DATA:
         if (GetSectorStatus(H->SectorMap[D->SectorsPerBlock-1]) != SECTOR_AVAIL)
            break;
         for (j=0; j<D->SectorsPerBlock; j++)
            if (GetSectorStatus(H->SectorMap[j]) == SECTOR_AVAIL)
               return BlockIndex * D->SectorsPerBlock + j;
         break;
      default:
         XRAISE(RTF_DATA_ERROR);
   }
   return INVALID_SECTOR_INDEX;
}

#ifdef DEBUG
/*-----------------------------------*/
static void  CheckAvailCount(RTFDrvFlashData * D)
{
   FlashHeader * H;
   dword i, j, Result=0;

   for (i=0; i<D->FlashInfo.TotalBlocks; i++)
   {
      H = MapWindow(D, i, 0);
      switch (GetBlockId(H->Header))
      {
         case BLOCK_SPARE:
            break;
         case BLOCK_DATA:
            for (j=D->SectorsPerBlock-1; j<D->SectorsPerBlock; j--)
               if (GetSectorStatus(H->SectorMap[j]) == SECTOR_AVAIL)
                  Result++;
               else
                  break;
            break;
         default:
            XRAISE(RTF_DATA_ERROR);
      }
   }
   if (Result != D->AvailSectors)
   {
     RTFS_PRINT_STRING_1((byte *)"Avail count messed up\n", PRFLG_NL); /* "Avail count messed up\n" */
   }
}
#else
#define CheckAvailCount(D)
#endif

/*-----------------------------------*/
static dword  LocateAvailSector(RTFDrvFlashData * D)
{
   dword i;
   dword Result;

   CheckAvailCount(D);

   if ((D->AvailSectors == 0) || (D->LastAvailBlock == INVALID_BLOCK_INDEX))
      return INVALID_SECTOR_INDEX;

   Result = CheckAvail(D, D->LastAvailBlock);

   if (Result == INVALID_SECTOR_INDEX)
   {
      for (i=0; i<D->FlashInfo.TotalBlocks; i++)
      {
         Result = CheckAvail(D, i);
         if (Result != INVALID_SECTOR_INDEX)
         {
            D->LastAvailBlock = i;
            return Result;
         }
      }
      D->LastAvailBlock = INVALID_BLOCK_INDEX;
   }
   return Result;
}

/*-----------------------------------*/
static void *  MapSector(RTFDrvFlashData * D, dword PhysicalSector)
{
   dword Block       = PhysicalSector / D->SectorsPerBlock;
   dword SectorIndex = PhysicalSector % D->SectorsPerBlock;
   dword AddrInBlock = D->HeaderSize + SectorIndex * 512;
   return (void*) (((byte*) MapWindow(D, Block, AddrInBlock / D->FlashInfo.WindowSize)) + (AddrInBlock % D->FlashInfo.WindowSize));
}

/*-----------------------------------*/
static void  ReadFlashSector(RTFDrvFlashData * D, dword LogicalSector, void * Buffer)
{
   dword PhysicalSector = LocateSector(D, LogicalSector);
    if (PhysicalSector != INVALID_SECTOR_INDEX)
      copybuff(Buffer, MapSector(D, PhysicalSector), 512);
   else
      rtfs_memset(Buffer, 0, 512);
}

/*-----------------------------------*/
static void  WriteSector(RTFDrvFlashData * D, dword PhysicalSector, dword LogicalSector, void * Buffer)
{
   SectorInfo S;
   dword Block       = PhysicalSector / D->SectorsPerBlock;
   dword SectorIndex = PhysicalSector % D->SectorsPerBlock;
   FlashHeader * H;

   SetLogicalSector(S.LogicalSectorAddr, LogicalSector);
   S.SectorStatusAddr[0] = SECTOR_WRITING;

   /* record that this sector is being updated in the block header sector map */
   H = (FlashHeader *)MapWindow(D, Block, 0);
   ProgramData(D, &H->SectorMap[SectorIndex], &S, sizeof(H->SectorMap[SectorIndex]));

   /* write the sector s data */
   ProgramData(D, MapSector(D, PhysicalSector), Buffer, 512);

   /* record that this sector is mow valid in the block header sector map */
   S.SectorStatusAddr[0] = SECTOR_VALID;
   H = (FlashHeader *)MapWindow(D, Block, 0);
   ProgramData(D, &H->SectorMap[SectorIndex].SectorStatusAddr, &S.SectorStatusAddr, sizeof(H->SectorMap[SectorIndex].SectorStatusAddr));

   if (D->SectorMap)
   {
       SetPhysicalSector(D->SectorMap[LogicalSector].PhysicalSectorAddr, PhysicalSector);
   }

   D->AvailSectors--;
}

/*-----------------------------------*/
static void  DeletePhysicalSector(RTFDrvFlashData * D, dword PhysicalSector)
{
   byte S = SECTOR_DELETED;
   dword Block = PhysicalSector / D->SectorsPerBlock;
   dword Index = PhysicalSector % D->SectorsPerBlock;
   FlashHeader * H = (FlashHeader *)MapWindow(D, Block, 0);

   ProgramData(D, &H->SectorMap[Index].SectorStatusAddr, &S, sizeof(H->SectorMap[Index].SectorStatusAddr));
}

/*-----------------------------------*/
static void  DeleteLogicalSector(RTFDrvFlashData * D, dword LogicalSector)
{
   dword PhysicalSector = LocateSector(D, LogicalSector);

   if (PhysicalSector != INVALID_SECTOR_INDEX)
   {
      DeletePhysicalSector(D, PhysicalSector);
      if (D->SectorMap)
      {
        SetPhysicalSector(D->SectorMap[LogicalSector].PhysicalSectorAddr, INVALID_SECTOR_INDEX);
      }

   }
}

/*-----------------------------------*/
static void  MarkToProcess(RTFDrvFlashData * D, dword LogicalSector, dword Sectors)
{
   dword i;

   D->StartSector = LogicalSector;
   D->SectorsLeft = D->Sectors = Sectors;

   if (D->SectorMap == 0)
      for (i=0; i<Sectors; i++)
      {
         unsigned int Index = (unsigned int) (i / (8*sizeof(D->MiniSectorMap[0])));
         unsigned int BitIndex  = (unsigned int) (i % (8*sizeof(D->MiniSectorMap[0])));

         D->MiniSectorMap[Index] |= 1 << BitIndex;
      }
   else
      for (i=0; i<Sectors; i++)
        D->SectorMap[LogicalSector+i].MustUpdateAddr[0] = 1;
}

/*-----------------------------------*/
static void  MarkProcessed(RTFDrvFlashData * D, dword LogicalSector)
{
   D->SectorsLeft--;
   if (D->SectorMap == 0)
   {
      unsigned int Index = (unsigned int) ((LogicalSector - D->StartSector) / (8*sizeof(D->MiniSectorMap[0])));
      unsigned int BitIndex  = (unsigned int) ((LogicalSector - D->StartSector) % (8*sizeof(D->MiniSectorMap[0])));

      D->MiniSectorMap[Index] &= ~(1 << BitIndex);
   }
   else
        D->SectorMap[LogicalSector].MustUpdateAddr[0] = 0;
}

/*-----------------------------------*/
static int  MustProcess(RTFDrvFlashData * D, dword LogicalSector)
{
   if (LogicalSector < D->StartSector)
      return 0;

   if (LogicalSector >= (D->StartSector + D->Sectors))
      return 0;

   if (D->SectorMap == 0)
   {
      unsigned int Index = (unsigned int) ((LogicalSector - D->StartSector) / (8*sizeof(D->MiniSectorMap[0])));
      unsigned int BitIndex  = (unsigned int) ((LogicalSector - D->StartSector) % (8*sizeof(D->MiniSectorMap[0])));

      return (int) (D->MiniSectorMap[Index] & (1 << BitIndex));
   }
   else
    return (int) (GetMustUpdate(D->SectorMap[LogicalSector]));
}

/*-----------------------------------*/
static dword  NextUnprocessed(RTFDrvFlashData * D)
{
   dword i;

   if (D->SectorsLeft == 0)
      return INVALID_SECTOR_INDEX;

   if (D->SectorMap == 0)
   {
      unsigned int Index = 0;
      while (D->MiniSectorMap[Index] == 0)
         Index++;
      for (i=Index*sizeof(D->MiniSectorMap[0])*8; i<D->Sectors; i++)
         if (MustProcess(D, D->StartSector+i))
            return D->StartSector+i;
   }
   else
   {
      for (i=0; i<D->Sectors; i++)
       if (GetMustUpdate(D->SectorMap[D->StartSector+i]))
            return D->StartSector+i;
   }
   return INVALID_SECTOR_INDEX;
}

/*-----------------------------------*/
static void  CopyBlock(RTFDrvFlashData * D, byte * Buffer, dword Source, dword Target, dword MinEraseCount)
{
   /* we have to copy from NewSpareBlock to SpareBlock */
   /* get the erase count from target and store it in source */
   /* also, store target block index in target */
   FlashHeader * H;
   dword EraseCount;
   byte  BlockId;
   dword i;
   dword NextFreeTarget = 0;
   dword S;
   byte * P;


#ifdef LOGGING
   dword InitialFree = D->AvailSectors;
   dword R = 0;
#endif

   H = (FlashHeader *)MapWindow(D, Target, 0);
   EraseCount = GetEraseCount(H->Header);

   H = (FlashHeader *)MapWindow(D, Source, 0);

   if (EraseCount == INVALID_ERASE_COUNT)  /* erase count has been lost, use the source s erase count */
   {
         EraseCount = GetEraseCount(H->Header);
   }
   else
      EraseCount++;

   if (EraseCount < MinEraseCount)
   {
#ifdef LOGGING
      RTFS_PRINT_STRING_1((byte *)"*** adjusting EraseCount by ", 0); /* "*** adjusting EraseCount by " */
      RTFS_PRINT_LONG_1((dword) (MinEraseCount - EraseCount), PRFLG_NL);
#endif
      EraseCount = MinEraseCount;
   }

   if (EraseCount > MAX_ERASE_COUNT)
     EraseCount = MAX_ERASE_COUNT;

   BlockId = BLOCK_COPYING;

   /* mark the source to be copied */
   ProgramData(D, &H->Header.BlockIdAddr, &BlockId, sizeof(H->Header.BlockIdAddr));

   /* erase the target and update erase count */
   H = (FlashHeader *)MapWindow(D, Target, 0);
   if ((GetEraseCount(H->Header)) != INVALID_ERASE_COUNT) /* do not erase if erased already */
   {
      EraseBlock(D, Target);
   }
   ProgramData(D, &H->Header.EraseCountAddr, &EraseCount, sizeof(H->Header.EraseCountAddr));

   D->AvailSectors += D->SectorsPerBlock;

   /* copy each valid sector from Source to Target */
   /* use user supplied sectors, if encountered */

   H = (FlashHeader *)MapWindow(D, Source, 0); /* select source */

   for (i=0; i<D->SectorsPerBlock; i++)
   {
      switch (GetSectorStatus(H->SectorMap[i]))
      {
         case SECTOR_AVAIL:
            D->AvailSectors--;
            break;
         case SECTOR_DELETED:
            break;
         case SECTOR_VALID:
            S = GetLogicalSector(H->SectorMap[i]);
            if (MustProcess(D, GetLogicalSector(H->SectorMap[i])))
            {
               P = Buffer + (GetLogicalSector(H->SectorMap[i]) - D->StartSector) * 512;
               MarkProcessed(D, GetLogicalSector(H->SectorMap[i]));
#ifdef LOGGING
               R++;
#endif
            }
            else
            {
               /* read the sectors data into our private buffer */
               copybuff(D->Buffer, MapSector(D, Source * D->SectorsPerBlock + i), 512);
               P = D->Buffer;
            }
            /* and write */
            WriteSector(D, Target * D->SectorsPerBlock + NextFreeTarget++, S, P);
            /* go back to source block */
            H = (FlashHeader *)MapWindow(D, Source, 0);
            break;
         default:
            XRAISE(RTF_DATA_ERROR);
      }
   }
   /* set source to spare block */
   BlockId = BLOCK_SPARE;
   ProgramData(D, &H->Header.BlockIdAddr, &BlockId, sizeof(H->Header.BlockIdAddr));

   /* set target to data block */
   BlockId = BLOCK_DATA;
   H = (FlashHeader *)MapWindow(D, Target, 0);
   ProgramData(D, &H->Header.BlockIdAddr, &BlockId, sizeof(H->Header.BlockIdAddr));

   if (D->SectorsPerBlock > NextFreeTarget)
      D->LastAvailBlock = Target;

#ifdef LOGGING
   RTFS_PRINT_STRING_1((byte *)"CopyBlock from ", 0); /* "CopyBlock from " */
   RTFS_PRINT_LONG_1((dword) Source, 0);
   RTFS_PRINT_STRING_1((byte *)" To ", 0); /* " To " */
   RTFS_PRINT_LONG_1((dword) Target, PRFLG_NL);

   RTFS_PRINT_STRING_1((byte *)"Replaced ", 0); /* "Replaced " */
   RTFS_PRINT_LONG_1((dword) R, 0);
   RTFS_PRINT_STRING_1((byte *)" Copied ", 0); /* " Copied " */
   RTFS_PRINT_LONG_1((dword) (NextFreeTarget - R), PRFLG_NL);

   RTFS_PRINT_STRING_1((byte *)"Avail ", 0); /* "Avail " */
   RTFS_PRINT_LONG_1((dword) D->SectorsPerBlock - NextFreeTarget, 0);
   RTFS_PRINT_STRING_1((byte *)" Recovered ", 0); /* " Recovered " */
   RTFS_PRINT_LONG_1((dword) (D->AvailSectors - InitialFree + R), PRFLG_NL);
#endif
}

/*-----------------------------------*/
static void  FindNewSpare(RTFDrvFlashData * D, dword * SpareBlock,
            dword * NewSpareBlock, dword * MinEraseCount)
/* find the block with the highest number of recoverable blocks for copying */
{
   dword MaxRecoverBlock = INVALID_BLOCK_INDEX;
   dword MaxRecoverCount = 0;
   dword MaxRecoverMinEraseCount = 0xFFFFFFFF;
   dword MinEraseBlock = INVALID_BLOCK_INDEX;
   dword MaxEraseCount = 0;
   dword SparesEraseCount = 0;
   dword i, j, RecoverCount;
   FlashHeader * H;
   *MinEraseCount = 0xFFFFFFFF;
   *SpareBlock = INVALID_BLOCK_INDEX;

   for (i=0; i<D->FlashInfo.TotalBlocks; i++)
   {
      H = (FlashHeader *)MapWindow(D, i, 0);
      switch (GetBlockId(H->Header))
      {
         case BLOCK_SPARE:
            *SpareBlock = i;
            SparesEraseCount = GetEraseCount(H->Header) + 1;
            if (SparesEraseCount > MaxEraseCount)
               MaxEraseCount = SparesEraseCount;
            break;
         case BLOCK_DATA:
            RecoverCount = 0;
            for (j=0; j<D->SectorsPerBlock; j++)
               switch (GetSectorStatus(H->SectorMap[j]))
               {
                  case SECTOR_AVAIL:
                     goto EndBlock;
                  case SECTOR_VALID:
                     if (MustProcess(D, GetLogicalSector(H->SectorMap[j])))
                        RecoverCount++;
                     break;
                  case SECTOR_DELETED:
                     RecoverCount++;
                     break;
                  default:
                     XRAISE(RTF_DATA_ERROR);
               }
EndBlock:
            if (RecoverCount > MaxRecoverCount)
            {
               MaxRecoverBlock = i;
               MaxRecoverCount = RecoverCount;
               MaxRecoverMinEraseCount = GetEraseCount(H->Header);
            }
            else
            {
               if ((RecoverCount > 0) && (RecoverCount == MaxRecoverCount))
               {
                  if ((GetEraseCount(H->Header)) < MaxRecoverMinEraseCount)
                  {
                     MaxRecoverBlock = i;
                     MaxRecoverMinEraseCount = GetEraseCount(H->Header);
                  }
                  else
                  {
                     if (GetEraseCount(H->Header) == MaxRecoverMinEraseCount)
                     {
                        /* make a pseudo random choice */
                        static int TakeIt;
                        TakeIt = TakeIt == 0;
                        if (TakeIt)
                           MaxRecoverBlock = i;
                     }
                  }
               }
            }
            if ((GetEraseCount(H->Header)) > MaxEraseCount)
               MaxEraseCount = GetEraseCount(H->Header);
            if ((GetEraseCount(H->Header)) < *MinEraseCount)
            {
               MinEraseBlock = i;
               *MinEraseCount = GetEraseCount(H->Header);
            }
            break;
         default:
            XRAISE(RTF_DATA_ERROR);
      }
   }

   if (MaxRecoverCount == 0)
   {
      XRAISE(RTF_DISK_FULL);
    }
   *NewSpareBlock = MaxRecoverBlock;

   if ((SparesEraseCount >= MaxEraseCount) &&
       ((MaxEraseCount - *MinEraseCount) >= ERASE_THRESHOLD) &&
       (*NewSpareBlock != MinEraseBlock))
   {
      *NewSpareBlock = MinEraseBlock;
#ifdef LOGGING
      RTFS_PRINT_STRING_1((byte *)"*** Wareleveling block ", 0); /* "*** Wareleveling block " */
      RTFS_PRINT_LONG_1( (dword) *NewSpareBlock, PRFLG_NL);
#endif
   }

   *MinEraseCount = MaxEraseCount - ERASE_THRESHOLD + 1;
   if (*MinEraseCount >= MaxEraseCount)
      *MinEraseCount = 0;

#ifdef DEBUG
   if (*SpareBlock == INVALID_BLOCK_INDEX)
     RTFS_PRINT_STRING_1((byte *)"spare block not found\n", PRFLG_NL); /* "spare block not found\n" */
   if (*NewSpareBlock == INVALID_BLOCK_INDEX)
     RTFS_PRINT_STRING_1((byte *)"new spare block not found\n", PRFLG_NL);  /* "new spare block not found\n" */
#endif
}

/*-----------------------------------*/
static unsigned int  NotAllZero(unsigned int * Buffer)
{
   unsigned int Result = 0;
   unsigned int i;

   for (i=0; i<(512/sizeof(unsigned int)); i++, Buffer++)
    {
      if (*Buffer)
        {
        Result = *Buffer;
          break;
        }
    }
   return Result;
}

/*-----------------------------------*/
static unsigned int  Equal(unsigned int * Buffer1, unsigned int * Buffer2)
{
   unsigned int i;

   for (i=0; i<(512/sizeof(unsigned int)); i++)
      if (*Buffer1++ != *Buffer2++)
         return 0;
   return 1;
}

/*-----------------------------------*/
static void  WriteFlashSectors(RTFDrvFlashData * D, dword LogicalSector, dword Sectors, byte * Buffer)
{
   dword PhysicalSector, Avail, i;

   MarkToProcess(D, LogicalSector, Sectors);

#ifdef LOGGING
      RTFS_PRINT_STRING_1((byte *)"Writing Sector ", 0); /* "Writing Sector " */
      RTFS_PRINT_LONG_1( (dword) LogicalSector, 0);
      RTFS_PRINT_STRING_1((byte *)" count ", 0); /* " count " */
      RTFS_PRINT_LONG_1( (dword) Sectors, PRFLG_NL);
#endif

   for (i=0; i<Sectors; i++)
      if (NotAllZero((unsigned int*) (Buffer + i * 512)) == 0)
      {
         DeleteLogicalSector(D, LogicalSector+i);
         MarkProcessed(D, LogicalSector+i);
      }
      else
         if (D->SectorMap)
         {
            PhysicalSector = LocateSector(D, LogicalSector+i);
            if ((PhysicalSector != INVALID_SECTOR_INDEX) &&
                (Equal((unsigned int*) (Buffer + i * 512), (unsigned int*) MapSector(D, PhysicalSector))))
               MarkProcessed(D, LogicalSector+i);
         }

   while (D->AvailSectors < D->SectorsLeft)
   {
      dword SpareBlock, NewSpareBlock, MinEraseCount;

      FindNewSpare(D, &SpareBlock, &NewSpareBlock, &MinEraseCount);
      CheckAvailCount(D);
      CopyBlock(D, Buffer, NewSpareBlock, SpareBlock, MinEraseCount);
      CheckAvailCount(D);
   }

   while (D->SectorsLeft > 0)
   {
      Avail = LocateAvailSector(D);
      i     = NextUnprocessed(D);

      if (Avail == INVALID_SECTOR_INDEX)
      {
        RTFS_PRINT_STRING_1((byte *)"flash driver: can not find a free sector", PRFLG_NL); /* "flash driver: can not find a free sector" */
      }
      if (i == INVALID_SECTOR_INDEX)
        RTFS_PRINT_STRING_1((byte *)"flash driver: can not find sector to write", PRFLG_NL);  /* "flash driver: can not find sector to write" */

      PhysicalSector = LocateSector(D, i);
      WriteSector(D, Avail, i, Buffer + (i-LogicalSector) * 512);
      if (PhysicalSector != INVALID_SECTOR_INDEX)
         DeletePhysicalSector(D, PhysicalSector);
      MarkProcessed(D, i);
   }
}

static int _calculate_flash_size_info(RTFDrvFlashData* D){
	  int retVal = mtd_MountDevice(D, &D->FlashInfo);

	  if (retVal < 0)
          return retVal;

       if (D->FlashInfo.BlockSize < (sizeof(FlashHeader) + 512))
          return RTF_DEVICE_RESOURCE_ERROR;

       if (D->FlashInfo.TotalBlocks < 2)
          return RTF_DEVICE_RESOURCE_ERROR;

       D->HeaderSize = 512;
       D->SectorsPerBlock = (D->FlashInfo.BlockSize - D->HeaderSize) / 512;
       while ((sizeof(BlockHeader) + sizeof(SectorInfo)*D->SectorsPerBlock) > D->HeaderSize)
       {
          D->HeaderSize += 512;
          D->SectorsPerBlock--;
       }

       if (D->FlashInfo.WindowSize < D->HeaderSize)
          return RTF_DEVICE_RESOURCE_ERROR;

       D->TotalSectors = D->SectorsPerBlock * (D->FlashInfo.TotalBlocks-1);

       if (D->TotalSectors >= INVALID_SECTOR_INDEX)
         RTFS_PRINT_STRING_1((byte *)"flash media too large", PRFLG_NL);   /* "flash media too large" */

	   return retVal;
}

/*-----------------------------------*/
/*
*	To provide caching when accessing sectors on the drive.  Ensure that size data
*   for the Drive has been initialized by calling _calculate_flash_size_info first and
*   mallocing or providing a static buffer for DriveData->SectorMap.
*/
static int  MapSectors(void * DriveData)
{
   RTFDrvFlashData * volatile D = (RTFDrvFlashData *)DriveData;
   FlashHeader * H;
   if(!D->SectorsAreMapped)
   {
       /*Result = _calculate_flash_size_info(D);
	   if(Result < 0){
			return Result;
	   }*/

       if (D->SectorMap != 0)
       { /* Initialize to all invalid */
            rtfs_memset((byte *) D->SectorMap, 0xFF, (int) (D->TotalSectors * sizeof(D->SectorMap[0])));
       }
       {
/*        dword i, j; */
          dword i;
          dword Spare=0, Spare2=0, CountSpare=0;
          dword Virgine=0, CountVirgine=0;
          dword ToCopy=0, CountToCopy=0;
          dword CountData=0;

          /*  block level analysis */

          for (i=0; i<D->FlashInfo.TotalBlocks; i++)
          {
             H = (FlashHeader *) MapWindow(D, i, 0);
             switch (GetBlockId(H->Header))
             {
                case BLOCK_VIRGINE:
                   CountVirgine++;
                   Virgine = i;
                   break;
                case BLOCK_DATA:
                   CountData++;
                   break;
                case BLOCK_COPYING:
                   CountToCopy++;
                   ToCopy = i;
                   break;
                case BLOCK_SPARE:
                   CountSpare++;
                   if (CountSpare == 1)
                      Spare = i;
                   else
                      Spare2 = i;
                   break;
                default:
                   CountSpare = 1000; /* this cannot be fixed */
             }
          }
          /* have to do consistency checks here */
          if ((CountSpare == 1) && (CountData == (D->FlashInfo.TotalBlocks-1)))
          {
    #ifdef LOGGING
             RTFS_PRINT_STRING_1((byte *)"flash disk OK",PRFLG_NL); /* "flash disk OK" */
    #endif
          }
          else if ((CountToCopy == 1) && (CountSpare == 1))
          {
    #ifdef LOGGING
             RTFS_PRINT_STRING_1((byte *)"Fixing 1",PRFLG_NL); /* "Fixing 1" */
    #endif
             CopyBlock(D, 0, ToCopy, Spare, 0);
          }
          else if ((CountToCopy == 1) && (CountVirgine == 1))
          {
    #ifdef LOGGING
             RTFS_PRINT_STRING_1((byte *)"Fixing 2",PRFLG_NL); /* "Fixing 2" */
    #endif
             CopyBlock(D, 0, ToCopy, Virgine, 0);
          }
          else if ((CountSpare == 1) && (CountVirgine == 1))
          {
             byte BlockId = BLOCK_DATA;
    #ifdef LOGGING
             RTFS_PRINT_STRING_1((byte *)"Fixing 4",PRFLG_NL); /* "Fixing 4" */
    #endif
             H = (FlashHeader *)MapWindow(D, Virgine, 0);
             ProgramData(D, &H->Header.BlockIdAddr, &BlockId, sizeof(H->Header.BlockIdAddr));
          }
          else if (CountSpare == 2)
          {
             dword Avail1=0, Avail2=0;
             byte BlockId;

    #ifdef LOGGING
             RTFS_PRINT_STRING_1((byte *)"Fixing 3",PRFLG_NL); /* "Fixing 3" */
    #endif
             H = (FlashHeader *)MapWindow(D, Spare, 0);
             for (i=0; i<D->SectorsPerBlock; i++)
                Avail1+= GetSectorStatus(H->SectorMap[i]) == SECTOR_AVAIL;
             H = (FlashHeader *)MapWindow(D, Spare2, 0);
             for (i=0; i<D->SectorsPerBlock; i++)
                Avail2+= GetSectorStatus(H->SectorMap[i]) == SECTOR_AVAIL;

             if (Avail1 > Avail2)
                H = (FlashHeader *)MapWindow(D, Spare, 0);
             BlockId = BLOCK_DATA;
             ProgramData(D, &H->Header.BlockIdAddr, &BlockId, sizeof(H->Header.BlockIdAddr));
          }
          else /* it is really messed up, need low level format */
          {
                 LowLevelFormat(DriveData, 0, 0);
          }
       }

       /* now do sector level analysis and collect statistics */

       {
          dword i, j;

          D->LastAvailBlock = INVALID_BLOCK_INDEX;
          D->AvailSectors = 0;

          for (i=0; i<D->FlashInfo.TotalBlocks; i++)
          {
             H = (FlashHeader *)MapWindow(D, i, 0);
             switch (GetBlockId(H->Header))
             {
                case BLOCK_SPARE:
                   break;
                case BLOCK_DATA:
                   for (j=0; j<D->SectorsPerBlock; j++)
                      switch (GetSectorStatus(H->SectorMap[j]))
                      {
                         case SECTOR_VALID:
                            if (D->SectorMap)
                            {
                               if ((GetLogicalSector(H->SectorMap[j])) >= D->TotalSectors)
                               {
       #ifdef LOGGING
                                  RTFS_PRINT_STRING_1((byte *)"Invalid sector value in flash", PRFLG_NL); /* "Invalid sector value in flash" */
       #endif
                                  DeletePhysicalSector(D, i*D->SectorsPerBlock + j);
                               }
                               else
                               {
                                  if (GetPhysicalSector(D->SectorMap[GetLogicalSector(H->SectorMap[j])]) != INVALID_SECTOR_INDEX)
                                  {
       #ifdef LOGGING
                                      RTFS_PRINT_STRING_1((byte *)"Sector found twice", PRFLG_NL); /* "Sector found twice" */
       #endif
                                     DeletePhysicalSector(D, i*D->SectorsPerBlock + j);
                                  }
                                  else
                                    {
                                    SetPhysicalSector(D->SectorMap[GetLogicalSector(H->SectorMap[j])].PhysicalSectorAddr, i * D->SectorsPerBlock + j);
                                    D->SectorMap[GetLogicalSector(H->SectorMap[j])].MustUpdateAddr[0]=0;
                                  }
                                }
                                    }
                            break;
                         case SECTOR_AVAIL:
                            D->LastAvailBlock = i;
                            D->AvailSectors++;
                            break;
                         case SECTOR_DELETED:
                            break;
                         default:
       #ifdef LOGGING
                            RTFS_PRINT_STRING_1((byte *)"Invalid Sector status", PRFLG_NL); /* "Invalid Sector status" */
       #endif
                            DeletePhysicalSector(D, i*D->SectorsPerBlock + j);
                      }
                   break;
                default:
                   return RTF_DATA_ERROR;
             }
          }
       }

       /* check if we have a valid file system and self format, if required and allowed */
       D->SectorsAreMapped=TRUE;
       }
   return 512;
}

/*  Driver API functions */

/*-----------------------------------*/
int  RealFlashEraseBlock  (void * DriveData, dword BlockIndex);
static int  ReadSectors(void * DriveData, dword Sector, dword Sectors, void * Buffer)
{
byte *b;

   RTFDrvFlashData * D = (RTFDrvFlashData *)DriveData;

    MapSectors(D);
   while (Sectors > 0)
   {
      ReadFlashSector(D, Sector, Buffer);
      Sectors--;
      Sector++;
      b = (byte*) Buffer;
      b += 512;
      Buffer = (void *) b;
   }
   return 0;
}

/*-----------------------------------*/
static int  WriteSectors(void * DriveData, dword Sector, dword Sectors, void * Buffer)
{
   RTFDrvFlashData * D = (RTFDrvFlashData *)DriveData;
   byte *b;

    MapSectors(D);
   while (Sectors > 0)
   {
/*      dword MaxSectors = D->SectorMap ? Sectors : min(sizeof(D->MiniSectorMap) * 8, Sectors); */
      dword MaxSectors;

      if (D->SectorMap)
            MaxSectors = Sectors;
        else
        {
            if (sizeof(D->MiniSectorMap) * 8 <  Sectors)
                MaxSectors = sizeof(D->MiniSectorMap) * 8;
            else
                MaxSectors = Sectors;
        }

      WriteFlashSectors(D, Sector, MaxSectors, (byte *)Buffer);
      Sectors -= MaxSectors;
      Sector  += MaxSectors;
      b = (byte*) Buffer;
      b += MaxSectors * 512;
      Buffer = (void *) b;
   }
   return 0;
}

/*-----------------------------------*/
int  LowLevelFormat(void * DriveData, const char * DeviceName, dword Flags)
{
   RTFDrvFlashData * D = (RTFDrvFlashData *)DriveData;
   dword i;
   BlockHeader Header;
   FlashHeader * H;

   RTFS_ARGSUSED_PVOID((void *)DeviceName);
   RTFS_ARGSUSED_INT((int)Flags);


   SetEraseCount(Header.EraseCountAddr, 1);
   for (i=0; i<D->FlashInfo.TotalBlocks; i++)
   {
      H = (FlashHeader *) MapWindow(D, i, 0);
      EraseBlock(D, i);
      Header.BlockIdAddr[0] = (byte) (i ? BLOCK_DATA : BLOCK_SPARE);
      ProgramData(D, &H->Header, &Header, sizeof(H->Header));
   }
   return 0;
}


#define DEFAULT_OPERATING_POLICY    				0
#define DEFAULT_NUM_SECTOR_BUFFERS 					10
#define DEFAULT_NUM_FAT_BUFFERS     				2
#define DEFAULT_FATBUFFER_PAGESIZE_SECTORS  		8

#define DEFAULT_NUM_FILE_BUFFERS    				0
#define DEFAULT_FILE_BUFFER_SIZE_SECTORS 			0

#define DEFAULT_FAILSAFE_RESTORE_BUFFERSIZE_SECTORS 64
#define DEFAULT_NUM_FAILSAFE_BLOCKMAPS     			1024
#define DEFAULT_FAILSAFE_INDEX_BUFFERSIZE_SECTORS	1

#if(!USE_DYNAMIC_ALLOCATION)

/* === pparms->blkbuff_memory one for each sector buffer */
static BLKBUFF _blkbuff_memory[DEFAULT_NUM_SECTOR_BUFFERS];
static byte _sector_buffer_memory[DEFAULT_NUM_SECTOR_BUFFERS*512];

/* === pparms->fatbuff_memory one for each FAT page */
static FATBUFF _fatbuff_memory[DEFAULT_NUM_FAT_BUFFERS];
static byte _fat_buffer_memory[DEFAULT_NUM_FAT_BUFFERS*512];


static byte flsdisk_sectorbuffer[512];

#if (INCLUDE_FAILSAFE_CODE)
static FAILSAFECONTEXT fs_context;
static byte fs_buffer[32768];	/*(512(sector_size) * 64(DEFAULT_FAILSAFE_RESTORE_BUFFERSIZE_SECTORS)) */
static byte fs_index_buffer[DEFAULT_FAILSAFE_INDEX_BUFFERSIZE_SECTORS*512];	/*(512(sector_size) * 64(DEFAULT_FAILSAFE_RESTORE_BUFFERSIZE_SECTORS)) */
#endif

#endif

/* IO routine installed by BLK_DEV_hostdisk_Mount */
static int BLK_DEV_RD_blkmedia_io(void  *devhandle, void *pdrive, dword sector, void  *buffer, dword count, BOOLEAN reading)
{
    RTFS_ARGSUSED_PVOID(devhandle);
    RTFS_ARGSUSED_PVOID(pdrive);
	return(flashdisk_io(((DDRIVE*)pdrive)->driveno, sector, buffer, (word)count, reading));
}

int  BLK_DEV_RD_blkmedia_ioctl(void  *devhandle, void *pdrive, int opcode, int iArgs, void *vargs)
{

    RTFS_ARGSUSED_INT(iArgs);
    RTFS_ARGSUSED_PVOID(vargs);

    switch(opcode)
    {
        case RTFS_IOCTL_FORMAT:
			if (LowLevelFormat(&FlashData, 0, 0) == 0)
			{
				MapSectors((void *) &FlashData);
			}
			else
				return -1;
			break;
		case RTFS_IOCTL_INITCACHE:
		case RTFS_IOCTL_FLUSHCACHE:
			break;
    }
    return(0);

}

static int BLK_DEV_RD_device_configure_media(struct rtfs_media_insert_args *pmedia_parms, struct rtfs_media_resource_reply *pmedia_config_block, int sector_buffer_required)
{

	pmedia_config_block->requested_driveid = (int) ('I'-'A');
	pmedia_config_block->requested_max_partitions =  1;
	pmedia_config_block->use_fixed_drive_id = 1;
	pmedia_config_block->device_sector_buffer_size_bytes = 512;


	/* Dynamically allocating so use Rtfs helper function */
	#if(USE_DYNAMIC_ALLOCATION)
		pmedia_config_block->use_dynamic_allocation = 1;
	#else
		pmedia_config_block->use_dynamic_allocation = 0;
		if (sector_buffer_required)
			pmedia_config_block->device_sector_buffer_data = flsdisk_sectorbuffer;
	#endif
    /*	0  if successful
		-1 if unsupported device type
		-2 if out of resources
	*/
	return(0);
}

static int BLK_DEV_RD_device_configure_volume(struct rtfs_volume_resource_request *prequest_block, struct rtfs_volume_resource_reply *pvolume_config_block)
{
    pvolume_config_block->drive_operating_policy 			= DEFAULT_OPERATING_POLICY;
    pvolume_config_block->n_sector_buffers 					= DEFAULT_NUM_SECTOR_BUFFERS;
    pvolume_config_block->n_fat_buffers    					= DEFAULT_NUM_FAT_BUFFERS;
    pvolume_config_block->fat_buffer_page_size_sectors  	= DEFAULT_FATBUFFER_PAGESIZE_SECTORS;
    pvolume_config_block->n_file_buffers 					= DEFAULT_NUM_FILE_BUFFERS;
    pvolume_config_block->file_buffer_size_sectors 			= DEFAULT_FILE_BUFFER_SIZE_SECTORS;
    if (prequest_block->failsafe_available)
    {
		pvolume_config_block->fsrestore_buffer_size_sectors = DEFAULT_FAILSAFE_RESTORE_BUFFERSIZE_SECTORS;
    	pvolume_config_block->fsjournal_n_blockmaps 		= DEFAULT_NUM_FAILSAFE_BLOCKMAPS;
		pvolume_config_block->fsindex_buffer_size_sectors 	= DEFAULT_FAILSAFE_INDEX_BUFFERSIZE_SECTORS;
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

/* Call after Rtfs is intialized to start a ram disk driver on I: */
BOOLEAN BLK_DEV_RD_fls_Mount(void)
{
    /* Set up mount parameters and call Rtfs to mount the device    */
struct rtfs_media_insert_args rtfs_insert_parms;

if(_calculate_flash_size_info(&FlashData) < 0){
	return FALSE;
}
#if(INCLUDE_SECTOR_MAP)
#if(USE_DYNAMIC_SECTOR_MAP)

       FlashData.SectorMap = (PhysicalSectorMapPtr) rtfs_port_malloc(FlashData.TotalSectors * sizeof(FlashData.SectorMap[0]));
       if (!FlashData.SectorMap)
           return FALSE;

#elif(USE_STATIC_SECTOR_MAP)
       FlashData.SectorMap = &StaticSectorMap[0];
       if (FlashData.TotalSectors > STATIC_SECTOR_MAP_SIZE)
       {   /* Sector MAP is too small */
		   printf("The Static Sector Map is too small, please ensure that the sector map is the size of the available sectors. Sectors Needed: %d", FlashData.TotalSectors);
           return FALSE;
       }
#else
        FlashData.SectorMap = 0;
#endif
#endif
	if(FlashData.SectorMap != 0){
		MapSectors(&FlashData);
	}

	/* register with Rtfs File System */
    rtfs_insert_parms.devhandle = (void*)&FlashData; /* Not used just a handle */
    rtfs_insert_parms.device_type = 999;	/* not used because private versions of configure and release, but must be non zero */
    rtfs_insert_parms.unit_number = 0;

	rtfs_insert_parms.media_size_sectors = FlashData.TotalSectors;
    rtfs_insert_parms.numheads = 1;
    rtfs_insert_parms.secptrk  = FlashData.SectorsPerBlock;
	rtfs_insert_parms.numcyl   = FlashData.TotalSectors/FlashData.SectorsPerBlock;

	rtfs_insert_parms.sector_size_bytes =  (dword) 512;
    rtfs_insert_parms.eraseblock_size_sectors =   0;
    rtfs_insert_parms.write_protect    =          0;

    rtfs_insert_parms.device_io                = BLK_DEV_RD_blkmedia_io;
    rtfs_insert_parms.device_ioctl             = BLK_DEV_RD_blkmedia_ioctl;
    rtfs_insert_parms.device_erase             = 0;
    rtfs_insert_parms.device_configure_media    = BLK_DEV_RD_device_configure_media;
    rtfs_insert_parms.device_configure_volume   = BLK_DEV_RD_device_configure_volume;

	if (pc_rtfs_media_insert(&rtfs_insert_parms) < 0)
    	return(FALSE);
	else
    	return(TRUE);
}


#endif /* (INCLUDE_FLASH_FTL) */
