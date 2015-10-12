/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1996
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author. 
*/
/*  With Permission                              Copyright (c) 1998,2000 *
*  Version: 2.0                                 On Time Informatik GmbH  */


#ifndef __FLASH__
#define __FLASH__

typedef struct {
    byte  PhysicalSectorAddr[3];
    byte  MustUpdateAddr[1];
} PhysicalSectorMap, *PhysicalSectorMapPtr;

typedef struct {      /* The FashInfo structure is used only as an element 
                         of the FlashData structure */
    dword TotalBlocks;
    dword BlockSize;
    dword  WindowSize;
} RTF_MTD_FlashInfo; 

typedef struct {
   RTF_MTD_FlashInfo FlashInfo;
   dword     HeaderSize;        /* offset to first sector data in flash block */
   dword     SectorsPerBlock;
   dword     TotalSectors;
   dword     AvailSectors;
   dword     LastAvailBlock;
   dword     StartSector;
   dword     Sectors;
   dword     SectorsLeft;
   BOOLEAN   SectorsAreMapped;
   PhysicalSectorMapPtr SectorMap;
   dword      MiniSectorMap[16]; /* bit map */
   byte      Buffer[512];
} RTFDrvFlashData;

#endif  /*#ifndef __FLASH__ */

