/*****************************************************************************
*Filename: RTFSCONF.H - RTFS tuning constants
*
*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc, 2006
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*
* Description:
*   This file contains tuning constants for configuring RTFS.
*   It is included by rtfs.h
*
****************************************************************************/
/* Code sizes.. */

#ifndef __RTFSCONF__
#define __RTFSCONF__ 1

/* This is the release configuration, rtfspackage.h is configured for purchased options */
// #include "rtfspackage.h"


#if (1) /* Option selections for inernal development */
/*  Identify purchased options: Use the column of values underneath the package name for the named package.
                                          Rtfs  Rtfs Rtfs         Rtfs    Rtfs     Rtfs          Rtfs      Rtfs           Rtfs
                                          BASIC PRO  PRO          PROPLUS PROPLUS  PROPLUS64 PROPLUS64 PROPLUSDVR PROPLUSDVR
                                                     FAILSAFE         FAILSAFE           FAILSAFE             FAILSAFE     */
#define INCLUDE_RTFS_BASIC_ONLY      0  /* 1     0    0        0       0        0         0         0          0            */
#define INCLUDE_RTFS_PROPLUS         0  /* 0     0    0        1       1        1         1         1          1            */
#define INCLUDE_RTFS_FAILSAFE_OPTION 0  /* 0     0    1        0       1        0         1         0          1            */
#define INCLUDE_RTFS_DVR_OPTION      0  /* 0     0    0        0       0        0         0         1          1            */
#define INCLUDE_EXFAT 	0
#define INCLUDE_MATH64 	0
#define INCLUDE_FAT64 	0
#define INCLUDE_EXFATORFAT64 (INCLUDE_EXFAT||INCLUDE_FAT64)

#if (INCLUDE_EXFATORFAT64||INCLUDE_RTFS_DVR_OPTION)
#undef INCLUDE_MATH64
#define INCLUDE_MATH64 1
#endif
#endif
#if (INCLUDE_RTFS_DVR_OPTION)
#define INCLUDE_EXTENDED_ATTRIBUTES 1
#else
#define INCLUDE_EXTENDED_ATTRIBUTES 0
#endif



#if (INCLUDE_EXFATORFAT64&&(INCLUDE_RTFS_PROPLUS==0))
#if (INCLUDE_RTFS_FAILSAFE_OPTION)
#error Failsafe-exFAT-RtfsPro only is untested
#else
// #error Known error in the regresion test for this combination
#endif
#endif

#define INCLUDE_FLASH_MANAGER 0
#define EXRAM

#define EXFAT_FAVOR_CONTIGUOUS_FILES 		1	/* exFat only should be one, set to zero to force exfat to always scavenge clusters from the beginning
												   otherwise pdr->drive_info.free_contig_pointer is updated as a hint where to allocate from */

/* Note: Not all options are supported for all Rtfs configurations. Consult the porting and
   configuration guided for documentation on individual configurations */

/* Character set support */
#define INCLUDE_CS_JIS       1                      /* Set to 1 to support JIS (kanji)  */
#define INCLUDE_CS_UNICODE   0                      /* Set to 1 to support unicode */
#define INCLUDE_CS_ASCII     (!INCLUDE_CS_JIS)      /* Do not change  support ASCII if JIS not enabled */

#if (INCLUDE_EXFATORFAT64)		 	/* Unicode must be on for exFat */
#undef INCLUDE_CS_UNICODE
#define INCLUDE_CS_UNICODE   1                      /* Do not change UNICODE must be enabled if Exfat enabled */
#endif


#define INCLUDE_NAND_DRIVER             1           /* See porting and configuration guide for explanation */


#define INCLUDE_TRANSACTION_FILES   0               /* Define 1 to include RTFS PRO PLUS transaction file. INCLUDE_FAILSAFE_CODE must also be enabled */

/* Debug settings */
#define INCLUDE_DEBUG_RUNTIME_STATS   1         /* See porting and configuration guide for explanation */
#define INCLUDE_DEBUG_LEAK_CHECKING   0         /* See porting and configuration guide for explanation */
#define INCLUDE_DEBUG_VERBOSE_ERRNO   0         /* See porting and configuration guide for explanation */
#define INCLUDE_DEBUG_TEST_CODE       1         /* See porting and configuration guide for explanation */

#define INCLUDE_FAT16                 1         /* Set to 1 to support 12 and 16 bit FATs */
#define INCLUDE_FAT32                 1         /* Set to 1 to support 32 bit FATs */
/* Note: After we implemented VFAT we learned that Microsoft patented
   the Win95 VFS implementation. US PATENT # 5,758,352.
   Leaving VFAT set to zero will exclude potential patent infringment  problems. */
#define INCLUDE_VFAT                1           /* Set to 1 to support long filenames */

#define INCLUDE_BASIC_POSIX_EMULATION (INCLUDE_RTFS_PROPLUS == 0)  /* Must select BASIC Posix emulation for Basic Only */

#define INCLUDE_CIRCULAR_FILES        INCLUDE_RTFS_DVR_OPTION

#define INCLUDE_FAILSAFE_CODE         INCLUDE_RTFS_FAILSAFE_OPTION
#define INCLUDE_FAILSAFE_RUNTIME          INCLUDE_RTFS_FAILSAFE_OPTION

#if (!INCLUDE_RTFS_FAILSAFE_OPTION)
#undef INCLUDE_TRANSACTION_FILES
#define INCLUDE_TRANSACTION_FILES    0
#endif

/* Set to one to include high performance freespace manager */
#define INCLUDE_RTFS_FREEMANAGER       1

/* Set to one to include RTFS asyncronous API */
#define INCLUDE_ASYNCRONOUS_API        1

/* Set to 1 to support extended DOS partitions */
#define SUPPORT_EXTENDED_PARTITIONS    1
#if (SUPPORT_EXTENDED_PARTITIONS)
#define RTFS_MAX_PARTITIONS 16      /* Should be no need to change */
#else
#define RTFS_MAX_PARTITIONS 4       /* Do not change */
#endif

/* Set to 1 to support reverse directory enumeration */
#define INCLUDE_REVERSEDIR          0

#define RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES    512 /* Should be no need to change */


#define RTFS_CFG_LEAN               0           /* See porting and configuration guide for explanation */
#define RTFS_CFG_READONLY           0           /* See porting and configuration guide for explanation */


#define RTFS_CFG_MAX_DIRENTS       32768        /* See porting and configuration guide for explanation */


#if (!INCLUDE_RTFS_PROPLUS)
#undef INCLUDE_TRANSACTION_FILES
#undef INCLUDE_ASYNCRONOUS_API
#define INCLUDE_TRANSACTION_FILES   0
#define INCLUDE_ASYNCRONOUS_API     0
#endif



#if (INCLUDE_RTFS_BASIC_ONLY)
#undef INCLUDE_CS_UNICODE
#undef INCLUDE_VFAT
#undef INCLUDE_FAT16
#undef INCLUDE_FAT32
#undef INCLUDE_RTFS_FREEMANAGER
#define INCLUDE_CS_UNICODE          0
#define INCLUDE_VFAT                0
#define INCLUDE_FAT16               1
#define INCLUDE_FAT32               0
#define INCLUDE_RTFS_FREEMANAGER    0
#endif


#include "rtfsarch.h"

#if (INCLUDE_EXFATORFAT64&&INCLUDE_RTFS_PROPLUS)
#if (INCLUDE_NATIVE_64_TYPE==0)
#error PROPlus for 64 bit file system needS native 64 bit support
#endif
#endif
#define RTFS_FREE_MANAGER_HASHSIZE 32  /* Do not change */


/*
Note: These 2 constants are rather arcane and apply only to
   RtfsPro style (po_xxx) functions. They can be left as is
   The maximum file size RtfsPro style functions may create
   When po_chsize() is called with a size request larger than this it
   fails and set errnoto PETOOLARGE. When po_write() is asked to extend
   the file beyond this maximum the behavior is determined by the value of
   RTFS_TRUNCATE_WRITE_TO_MAX */
#define RTFS_MAX_FILE_SIZE      0xffffffff
/* #define RTFS_MAX_FILE_SIZE      0x80000000 */
/* Set to 1 to force RTFS to truncate po_write() requests to fit within
   RTFS_MAX_FILE_SIZE or 0 trigger an error */
#define RTFS_TRUNCATE_WRITE_TO_MAX    1

#if (RTFS_CFG_READONLY)         /* Read only file system, disable failsafe, free manager, test code and D=dvr support */
#undef INCLUDE_FAILSAFE_CODE
#define INCLUDE_FAILSAFE_CODE 0

#undef INCLUDE_RTFS_FREEMANAGER
#define INCLUDE_RTFS_FREEMANAGER 0

#undef INCLUDE_DEBUG_TEST_CODE
#define INCLUDE_DEBUG_TEST_CODE 0

#undef INCLUDE_TRANSACTION_FILES
#define INCLUDE_TRANSACTION_FILES 0

#undef INCLUDE_CIRCULAR_FILES
#define INCLUDE_CIRCULAR_FILES 0
#endif


#if (RTFS_CFG_LEAN)         /* Lean build set compile time code exclusion. Additional conditional code is
                               included in the software source code */
#undef INCLUDE_RTFS_FREEMANAGER
#define INCLUDE_RTFS_FREEMANAGER 0
#undef INCLUDE_DEBUG_TEST_CODE
#define INCLUDE_DEBUG_TEST_CODE 0
#undef INCLUDE_TRANSACTION_FILES
#define INCLUDE_TRANSACTION_FILES 0
#undef INCLUDE_CIRCULAR_FILES
#define INCLUDE_CIRCULAR_FILES 0
#undef INCLUDE_DEBUG_VERBOSE_ERRNO
#define INCLUDE_DEBUG_VERBOSE_ERRNO  0
#undef INCLUDE_ASYNCRONOUS_API
#define INCLUDE_ASYNCRONOUS_API 0
#undef SUPPORT_EXTENDED_PARTITIONS
#define SUPPORT_EXTENDED_PARTITIONS 0
#endif

#if (INCLUDE_VFAT)
#define FILENAMESIZE_CHARS    255
#else
#if (INCLUDE_CS_UNICODE)
#error - Unicode requires INCLUDE_VFAT
#endif
#define FILENAMESIZE_CHARS      8
#endif

/* When scanning a directory cluster chain fail if more than this many
   clusters are in the chain. (Indicates endless loop) */
#define MAX_CLUSTERS_PER_DIR 4096

#if (INCLUDE_VFAT)
#define EMAXPATH_CHARS        255  /* Maximum path length.Change carefully */
#else
#define EMAXPATH_CHARS        148  /* Maximum path length.Change carefully */
#endif

/* Declare buffer sizes, leave room for terminating NULLs, allign to
   four bytes for good form. */
#if (INCLUDE_VFAT)
#if (INCLUDE_CS_UNICODE || INCLUDE_CS_JIS)
#define EMAXPATH_BYTES 524
#define FILENAMESIZE_BYTES 512
#else
#define EMAXPATH_BYTES 264
#define FILENAMESIZE_BYTES 256
#endif
#else /* Not INCLUDE_VFAT */
#if (INCLUDE_CS_UNICODE || INCLUDE_CS_JIS)
#define EMAXPATH_BYTES 300
#define FILENAMESIZE_BYTES 20
#else
#define EMAXPATH_BYTES 152
#define FILENAMESIZE_BYTES 12
#endif
#endif

/* Check for required declarations */
/* Make sure at least one FAT size is enabled */
#if !(INCLUDE_FAT16) && !(INCLUDE_FAT32)
#error At least one FAT size must be selected
#endif

#if (INCLUDE_DEBUG_TEST_CODE)
#if (!INCLUDE_DEBUG_RUNTIME_STATS)
#error If INCLUDE_DEBUG_TEST_CODE is set you must also set INCLUDE_DEBUG_RUNTIME_STATS
#endif
#endif

/* Compile time constants to control device inclusion and inclusion of porting layer subroutines
   to reduce the header file count these were moved from a segregated header file.
   These constants are only used to configure device driver selection and device driver specific
   services in the porting layer, they are not and must not be used by core library source mdoules */

#define INCLUDE_IDE             1 /* - Include the IDE driver */
#define INCLUDE_PCMCIA          0 /* - Include the pcmcia driver */
#define INCLUDE_PCMCIA_SRAM     0 /* - Include the pcmcia static ram card driver */
#define INCLUDE_COMPACT_FLASH   0 /* - Support compact flash (requires IDE and PCMCIA) */
#define INCLUDE_FLASH_FTL       0 /* - Include the linear flash driver */
#define INCLUDE_ROMDISK         0 /* - Include the rom disk driver */
#define INCLUDE_RAMDISK         0 /* - Include the rom disk driver */
#define INCLUDE_MMCCARD         0 /* - Include the multi media flash card driver */
#define INCLUDE_SDCARD			0 /* - Include SD card driver */
#define INCLUDE_SMARTMEDIA      0 /* - Include the smart media flash card driver */
#define INCLUDE_FLOPPY          0 /* - Include the floppy disk driver */

#ifndef _MSC_VER
#define INCLUDE_HOSTDISK        0 /* - Include the host disk disk simulator */
#define INCLUDE_WINDEV          0 /* - Include windows direct device access */
#else
#define INCLUDE_HOSTDISK        1 /* - Include the host disk disk simulator */
#define INCLUDE_WINDEV          1 /* - Include windows direct device access */
#endif
#define INCLUDE_UDMA            0 /* - Include ultra dma support for the ide driver */
#define INCLUDE_82365_PCMCTRL   0 /* - Include the 82365 pcmcia controller driver */

#define INCLUDE_V_1_0_DEVICES   0
#define INCLUDE_V_1_0_CONFIG    0

#if (INCLUDE_V_1_0_DEVICES)

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

#include "portconf.h"
#endif


#endif /* __RTFSCONF__ */
