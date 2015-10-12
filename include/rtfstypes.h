/*****************************************************************************
*Filename: RTFSTYPES.H - RTFS common function types
*
*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS, 2007
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*
*
* Description:
*
*
*
*
****************************************************************************/

#ifndef __RTFSTYPES__
#define __RTFSTYPES__ 1

/* llword type used for ProPlus feature set, all arithmetic on files sizes and file offsets uses the llword type.
   Unlike in the basic exFAT package which USES 64 bit arithmetic macros, some ProPlus arithmetic is performed
   using C operators (+,- == etc) and require native 64 bit support if exFAT is enabled */
/* This ProPlus union is included in the RTFS Finode structure */
typedef union _llword {
        dword  val32;            /* File size */
#if (INCLUDE_EXFATORFAT64)
        ddword val64;            /* File size */
#endif
    } llword;

/* Structure for run length encoding cluster chains, currently used by
   ProPlus only but will be used in Pro */
typedef struct region_fragment {
        dword start_location;
        dword end_location;
        struct region_fragment *pnext;
        } REGION_FRAGMENT;
/* Give the size of an individual fragment in clusters */
#define PC_FRAGMENT_SIZE_CLUSTERS(PF) ((PF)->end_location - (PF)->start_location + 1)

/* ProPlus extensions to RTFS Finode structure */
typedef struct finode_extended {
        REGION_FRAGMENT *plast_fragment;
        REGION_FRAGMENT *pfirst_fragment;
        llword   alloced_size_bytes;
        dword   alloced_size_clusters;
        dword   clusters_to_process;
        dword   last_processed_cluster;
        dword   clusters_to_delete;
        dword   last_deleted_cluster;
        REGION_FRAGMENT *ptofree_fragment; /* fragments to in the FAT on flush */
        } FINODE_EXTENDED;

/* This ProPlus union is included in the RTFS Finode structure */
typedef union finode_extension {
        FINODE_EXTENDED *x;
    } FINODE_EXTENSION;

/* This union is used as a device for declaring or allocating all extended finode structures in the same pool.
   the FINODE_EXTENDED and FINODE_META64 are basically the same size and so decaring them together simplified
   startup configuration procedures */
typedef union finode_extension_memory {
        union finode_extension_memory *pnext_freelist;
        FINODE_EXTENDED x;
    } FINODE_EXTENSION_MEMORY;

/* ProPlus extensions to RTFS File structure */
#if (INCLUDE_CIRCULAR_FILES)
typedef struct remap_record
{
    ddword remap_fpoff_start_ddw;    /* byte offset to fp offset of region */
    ddword remap_fpoff_end_ddw;      /* byte length of fp remap region */
    ddword remap_linear_offset_ddw;  /* raw offset to data in linear file */
    struct pc_file *premapfile;
    struct remap_record *pnext;
} REMAP_RECORD;
#endif

/* Date stamping buffer */
typedef struct datestr {
        unsigned short date;
        unsigned short time;
        } DATESTR;

/* This ProPlus structure is included in the RTFS file structure */
typedef struct pc_file_proplus {
    struct finode *    ffinode;        /* Info for getting at the inode for file access */
    struct pc_file *asy_file_pnext;
#define AF_CONTIGUOUS_MODE_FORCE     0x02
    word   allocflags;           /* For Files special allocation instructions */
    dword allocation_policy;
    dword allocation_hint;
    dword  async_file_segment64;
    dword  segment_size;
    llword file_pointer;
    llword region_byte_base;
    dword region_block_base;
    REGION_FRAGMENT *pcurrent_fragment;
    dword min_alloc_bytes;
#if (INCLUDE_TRANSACTION_FILES)
    byte  *transaction_buffer;
    dword current_transaction_cluster;
    dword current_transaction_blockno;
#endif
#if (INCLUDE_CIRCULAR_FILES)
    ddword circular_file_size_ddw;
    ddword circular_file_base_ddw;
    ddword circular_max_offset_ddw;
    int   sibling_fd;
    struct pc_file *psibling;
    REMAP_RECORD *remapped_regions;
    REMAP_RECORD *remap_freelist;
#endif
    } PC_FILE_PROPLUS;

/* ProPlus extensions to RTFS drive structure */
/* These ProPlus structures are included in the RTFS file structure */

/* Constants used for logging IO region accesses and forcing IO errors
   Only needed for test - Some tests monitor the IO operations that
   occuring and force read or write error during specific IO operations
   to ensure that the system responds properly
   For now this methodolgy is used only to test the journaling system
*/
#define RTFS_DEBUG_IO_J_NOT_USED                   0
#define RTFS_DEBUG_IO_J_ROOT_WRITE               1
#define RTFS_DEBUG_IO_J_INDEX_OPEN               2
#define RTFS_DEBUG_IO_J_INDEX_FLUSH              3
#define RTFS_DEBUG_IO_J_INDEX_CLOSE              4
#define RTFS_DEBUG_IO_J_INDEX_CLOSE_EMPTY        5
#define RTFS_DEBUG_IO_J_REMAP_WRITE              6
#define RTFS_DEBUG_IO_J_REMAP_READ               7
#define RTFS_DEBUG_IO_J_ROOT_READ                8
#define RTFS_DEBUG_IO_J_RESTORE_READ             9
#define RTFS_DEBUG_IO_J_RESTORE_START            10
#define RTFS_DEBUG_IO_J_RESTORE_END              11
#define RTFS_DEBUG_IO_J_INDEX_READ               12
#define RTFS_DEBUG_IO_J_RESTORE_INFO_WRITE       13
#define RTFS_DEBUG_IO_J_RESTORE_FAT_WRITE        14
#define RTFS_DEBUG_IO_J_RESTORE_BLOCK_WRITE      15
#define RTFS_DEBUG_IO_J_RANGES                   16

#if (INCLUDE_DEBUG_TEST_CODE)
typedef struct io_runtime_stats {
		dword    blocks_transferred[RTFS_DEBUG_IO_J_RANGES];
        int      force_io_error_on_type;
        dword    force_io_error_when;
        int      io_errors_forced;
        dword    force_io_error_now;
} IO_RUNTIME_STATS;
#endif

/*  values for drive_async_state */
#define DRV_ASYNC_IDLE         0
#define DRV_ASYNC_MOUNT        1
#define DRV_ASYNC_RESTORE      2
#define DRV_ASYNC_FATFLUSH     3
#define DRV_ASYNC_JOURNALFLUSH 4
#define DRV_ASYNC_FILES        5
#define DRV_ASYNC_DONE_MOUNT        101
#define DRV_ASYNC_DONE_RESTORE      102
#define DRV_ASYNC_DONE_FATFLUSH     103
#define DRV_ASYNC_DONE_JOURNALFLUSH 104
#define DRV_ASYNC_DONE_FILES        105


/* removed typedef struct drive_configure { */



#if (INCLUDE_VFAT||INCLUDE_EXFAT)
/* Trag lfn segments. segblock[0] is the block that contains beginning of the
   file name. segindex is the segment in that block where the beginning is
   stored. If the lfn spans > 1 block the next block # is in segblock[2] */
typedef struct segdesc {
    int nsegs;      /* # segs in the lfn */
    int segindex;
    dword segblock[3];
    byte ncksum;    /* checksum of the associated DOSINODE */
    byte fill[3];   /* nice */
    } SEGDESC;

#define LFNRECORD SEGDESC
#endif



#if (INCLUDE_EXFAT)
/* exfat Format control structure */

typedef struct rtfsexfatparms {
		ddword MediaLengthSectors;
		dword  BytesPerSector;
		dword  SectorsPerCluster;
		int	 	ExtendedBootCodeCountSectors;
		byte 	*ExtendedBootCodeData;
		BOOLEAN ScrubVolume;	        /* If true erase or zero the section of media to contain the volume formatting */
		dword 	BadSectorCount;
		dword 	*BadSectorList;

		ddword MediaLengthBytes;
		dword BoundaryUnit;
		dword FirstClusterOfUpCaseTable;
		dword FirstClusterOfBam;
		dword UpCaseSizeClusters;
		dword BamSizeClusters;
		ddword BamSizeBytes;
		dword ReservedSectors;



		byte JumpBoot[3]; 				/*  EBh, 76h, 90h */
		byte FileSystemName[8]; 			/*  “EXFAT “ */
		ddword PartitionOffset;
		ddword VolumeLength;
		dword FatOffset;
		dword FatLength;
		dword ClusterHeapOffset;
		dword ClusterCount;
		dword FirstClusterOfRootDirectory;
		dword VolumeSerialNumber;
		byte FileSystemRevision[2];
		word VolumeFlags;
		byte BytesPerSectorShift;
		byte SectorsPerClusterShift;
		byte NumberOfFats;
		byte DriveSelect; /*  80h */
		byte PercentInUse;
// byte 7 Reserved All 00h
		byte BootCode[390];
		byte BootSignature[2];


} RTFSEXFATFMTPARMS;


/* exfat FINODE structure extensions */
typedef struct exfatfinodext {
		word 			SetChecksum;
		word 			FileAttributes;
//		dword			CreateTimeStamp;
//		dword			LastModifiedTimeStamp;
//		dword			LastAccessedTimeStamp;
		byte		   	Create10msIncrement;
		byte		  	LastModified10msIncrement;
		byte		   	CreateUtcOffset;
		byte		   	LastModifiedUtcOffset;
		byte		   	LastAccessedUtcOffset;
		byte 			GeneralSecondaryFlags;
		byte 			SecondaryCount;
		byte  			NameLen;
		word 			NameHash;
} EXFATFINODEEXT;
#endif


/* This ProPlus union is included in the RTFS Finode structure */
typedef union finode_size {
        dword  fsize;            /* File size */
#if (INCLUDE_EXFATORFAT64)
        ddword fsize64;            /* File size */
#endif
    } FINODE_SIZE;


typedef struct finode {
        byte   fname[8];
        byte   fext[3];
        byte   fattribute;       /* File attributes */
        byte    reservednt;
        byte    create10msincrement;
        word    ctime;            /* time & date create */
        word    cdate;
        word    atime;            /* time & date modified */
        word    adate;
        word   fclusterhi; /* This is where fat32 stores file location */
        word   ftime;              /* time & date lastmodified */
        word   fdate;
        word   fcluster;        /* Cluster for data file */
		FINODE_SIZE fsizeu;
        int   opencount;
/* If the finode is an open file the following flags control the sharing.
   they are maintained by po__open                                    */
#ifdef OF_WRITE
/* The watcom Windows include files define OF_WRITE too */
#undef OF_WRITE
#endif
#define OF_WRITE            0x01    /* File is open for write by someone */
#define OF_WRITEEXCLUSIVE   0x02    /* File is open for write by someone
                                       they wish exclusive write access */
#define OF_EXCLUSIVE        0x04    /* File is open with exclusive access not
                                       sharing write or read */
#define OF_BUFFERED         0x10    /* Non block alligned data is buffered */
#if (INCLUDE_RTFS_PROPLUS)  /* ProPlus specific open flags */
#define OF_TRANSACTION     0x80    /* Opened as a transaction file */
#endif
        dword   openflags;            /* For Files. Track how files have it open */
        struct blkbuff *pfile_buffer; /* If the file is opened in buffered
                                         mode this is the last buffered
                                         item */
        struct ddrive *my_drive;
        dword  my_block;
        int   my_index;
        struct finode *pnext;
        struct finode *pprev;
        BOOLEAN is_free;        /* True if on the free list */
#if (INCLUDE_VFAT)
        LFNRECORD s;    /* Defined as SEGDESC for VFAT */
#endif

/* Flags tell if buffer associted with the finode or the directory entry needs flush */
#define FIOP_BUFFER_DIRTY   0x01
#define FIOP_NEEDS_FLUSH    0x02

/* The following flags are proplus only */
#define FIOP_ASYNC_FLUSH    0x04
#define FIOP_ASYNC_UNLINK   0x08
#define FIOP_ASYNC_OPEN     0x10
#define FIOP_ASYNC_CLOSE    0x20
#define FIOP_ASYNC_ALL_OPS (FIOP_ASYNC_CLOSE|FIOP_ASYNC_FLUSH|FIOP_ASYNC_UNLINK|FIOP_ASYNC_OPEN)

#define FIOP_LOAD_AS_NEEDED 0x40

        dword   operating_flags;    /* For Files. Track how files have it open */
        REGION_FRAGMENT *pbasic_fragment;

#if (INCLUDE_EXFAT)    	/* exfat FINODE structure extensions */
        EXFATFINODEEXT    exfatinode;
#endif

#if (INCLUDE_RTFS_PROPLUS)    /* ProPlus FINODE structure extensions */
        dword   extended_attribute_dstart;
        FINODE_EXTENSION e;         /* finode extension */
#endif
        } FINODE;


#define PCDELETE (byte) 0xE5       /* MS-DOS file deleted char */
#define PCDELETEESCAPE (byte) 0xFF /* Escape character rmode
                                      passes to pc_ino2dos to really
                                      delete a directory entry */
#define FAT_EOF_RVAL 0xffffffff   /* Symbolic constant. if returned from clnext
                                      means end of chain */
#define LARGEST_DWORD             0xffffffff    /* Use when we want MAX dword */

typedef struct pcmbuff {
        dword first_blockno;
        int  block_count;        /* up to 32 */
        int  dirty_count;        /* 0 if no blocks need flush, othe4rwise count of dirty blocks */
        int  dirty_block_offset; /* Offset to last block marked dirty (if dirty_count is 1, this is it */
        dword dirty_bitmap;      /* Bitmap of offsets of all dirty blocks (max is 32 */
        byte  *pdata;
        } PCMBUFF;

typedef struct fatbuff {
        struct fatbuff *pnext;
        struct fatbuff *pprev;
        struct fatbuff *pnext_hash; /* All hash table entries start one of these */
        struct fatbuff *pprev_hash;
		dword	lru_counter;
        PCMBUFF fat_data_buff;
        } FATBUFF;

/* Fat buffer context */
typedef struct fatbuffcntxt {
    // Suggest supporting on ProPlus
    dword   stat_primary_cache_hits;
    dword   stat_secondary_cache_hits;
    dword   stat_secondary_cache_loads;
    dword   stat_secondary_cache_swaps;

    // Suggest supporting on ProPlus, point to drive.du values
    struct fatbuff *pfat_buffers;        /* address of buffer pool */
    struct fatbuff *pcommitted_buffers;   /* uses pnext/pprev */
    struct fatbuff *pfree_buffers;        /* uses pnext */
    dword  fat_buffer_page_size_sectors;  /* Set during context setup */
    dword  fat_buffer_page_mask;
    int     hash_size;
    dword   hash_mask;
	dword	lru_counter;
	int		num_dirty;
#define FAT_HASH_SIZE 32
    FATBUFF *fat_blk_hash_tbl_core[FAT_HASH_SIZE];
    dword    primary_sectormap_core[FAT_HASH_SIZE];
    FATBUFF *primary_buffmap_core[FAT_HASH_SIZE];
    FATBUFF **fat_blk_hash_tbl;         /* ProPlus points to fat_blk_hash_tbl_core Pro passed in */
    /* Merged primary mapping scheme uses block cache instead of pointers */
    dword *primary_mapped_sectors;
    FATBUFF **primary_mapped_buffers;
} FATBUFFCNTXT;

 /* Dos Directory Entry Memory Image of Disk Entry */



typedef struct dosinode {
        byte    fname[8];
        byte    fext[3];
        byte    fattribute;      /* File attributes */
        byte    reservednt;
        byte    create10msincrement;
        word    ctime;            /* time & date create */
        word    cdate;
        word    adate;			  /* Date last modified */
        word    fclusterhi;     /* This is where fat32 stores file location */
        word  ftime;            /* time & date lastmodified */
        word  fdate;
        word  fcluster;         /* Cluster for data file */
        dword   fsize;            /* File size */
        } DOSINODE;




/* contain location information for a directory */
    typedef struct dirblk {
        dword  my_frstblock;      /* First block in this directory */
        dword  my_block;          /* Current block number */
#if (INCLUDE_EXFAT)	 /* Exfat dirblk extensions */
        dword  my_exNOFATCHAINfirstcluster;    /* ExFat - if non-zero we are scanning a contiguous region with no FAT chain */
        dword  my_exNOFATCHAINlastcluster;     /* ExFat - if non-zero we are scanning a contiguous region with no FAT chain */
#endif
        int     my_index;         /* dirent number in my block   */
    } DIRBLK;

/* Block buffer */
typedef struct blkbuff {
        struct blkbuff *pnext;  /* Used to navigate free and populated lists */
        struct blkbuff *pprev;  /* the populated list is double linked. */
                                /* Free list is not */
        struct blkbuff *pnext2; /* Each hash table entry starts a chain of these */
#define DIRBLOCK_FREE           0
#define DIRBLOCK_ALLOCATED      1
#define DIRBLOCK_UNCOMMITTED    2
        int  block_state;
        int  use_count;
        struct ddrive *pdrive;  /* Used during IO */
        struct ddrive *pdrive_owner; /* Used to distinguish scratch allocation from common pool or device */
        dword blockno;
        dword    data_size_bytes;     /* Size of the data at pointer */
        byte  *data;
        } BLKBUFF;

/* Block buffer context */
typedef struct blkbuffcntxt {
        dword   stat_cache_hits;
        dword   stat_cache_misses;
        struct blkbuff *ppopulated_blocks; /* uses pnext/pprev */
        struct blkbuff *pfree_blocks;      /* uses pnext */
        struct blkbuff *assigned_free_buffers;
#if (INCLUDE_DEBUG_LEAK_CHECKING)
        struct blkbuff *pscratch_blocks;      /* uses pnext */
#endif
        int     num_blocks;
        int     num_free;
        int     scratch_alloc_count;
        int     low_water;
        int     num_alloc_failures;
#define BLOCK_HASHSIZE 16
#define BLOCK_HASHMASK 0xf
        struct blkbuff *blk_hash_tbl[BLOCK_HASHSIZE];  /* uses pnext2 */
        } BLKBUFFCNTXT;




#define DRVOP_MOUNT_VALID        0x1 /* True if mounted */
#define DRVOP_MOUNT_ABORT        0x2 /* True if error handler requests abort */
#define DRVOP_FAT_IS_DIRTY       0x4 /* True if fat needs flush */
/* The states below are for ProPlus only */
#define DRVOP_FS_NEEDS_FLUSH     0x8 /* True if journal needs flush */
#define DRVOP_FS_NEEDS_RESTORE   0x10/* True if FAT volume needs restore */
#define DRVOP_ASYNC_FFLUSH       0x20
#define DRVOP_ASYNC_JFLUSH       0x40
#define DRVOP_ASYNC_JRESTORE     0x80
#define DRVOP_FS_CLEAR_ON_MOUNT	0x100	/* fs_api_enable() called with a request to clear the journal. It is cleared when journaling resumes */
#define DRVOP_FS_START_ON_MOUNT	0x200	/* fs_api_enable() called enable journaling on the next mount regardless of callback policy */
#define DRVOP_FS_DISABLE_ON_MOUNT 0x400	/* fs_api_disable() was called disable autorestore and journaling on the next mount regardless of callback policy */
#define DRVOP_BAM_IS_DIRTY        0x800 /* EXFAT True if BAM needs flush */


#if (INCLUDE_EXFAT)    	/* exfat drive structure extensions */
typedef struct exfatddrive {
        byte JumpBoot[3]; /* (BP 0 to 2) */
        byte FileSystemName[8]; /* (BP 3 to 10) */
/*        byte MustBeZero[53]; (BP 11 to 63) */
        ddword PartitionOffset; /* (BP 64 to 71) */
        ddword VolumeLength; /* (BP 72 to 79) */
        dword FatOffset; /* (BP 80 to 83) */
        dword FatLength; /* (BP 84 to 87) */
        dword ClusterHeapOffset; /* (BP 88 to 91) */
        dword ClusterCount; /* (BP 92 to 95) */
        dword FirstClusterOfRootDirectory; /* (BP 96 to 99) */
        dword VolumeSerialNumber; /* (BP 100 to 103) */
        word FileSystemRevision; /* (BP 104 and 105) */
        word VolumeFlags; /* (BP 106 and 107) */
        byte BytesPerSectorShift; /* (BP 108) */
        byte SectorsPerClusterShift; /* (BP 109) */
        byte NumberOfFats; /* (BP 110) */
        byte DriveSelect; /* (BP 111) */
        byte PercentInUse; /* (BP 112) */
/*
        byte Reserved[7];  (BP 113 to 119)
        byte BootCode[390]; (BP 120 to 509)
*/
        byte BootSignature[2]; /* (BP 510 and 511) */

/* === Calculated and retrieved exfat information. */
		word  PrimaryFlagsOfBitMap[2];	/* Two possible bit maps so 2 values of each */
        dword FirstClusterOfBitMap[2];
        dword SizeOfBitMap[2];
        dword FirstClusterOfUpCase;
		dword SizeOfUpcase;
        dword TableChecksum;
/* Maximum unicode value to be upcased (128 for Mandatory else 65536 */
		word UpCaseMaxTranskey;

/* Raw configuration values returned from RTFS_CBS_GETEXFATBUFFERS
   Released by RTFS_CBS_RELEASEEXFATBUFFERS */
   		int   BitMapBufferPageSizeSectors;
	 	dword BitMapBufferSizeSectors;
	 	void *BitMapBufferControlCore;
	 	void *BitMapBufferCore;
	 	void *UpCaseBufferCore;

		/* Use a FATBUFFER structure and a modified version of FAT buffering
		   code to manage the bit allocation map */
        FATBUFFCNTXT bambuffcntxt;
        dword volume_label_sector;   /* Location of the volume entry in the root directory or zero. */
        int   volume_label_index;    /* index into volume_label_sector */
} EXFATDDRIVE;

#endif


typedef struct ddrive_info {
        dword    drive_operating_flags;
        dword volume_serialno;      /* Volume serial number block 0 */
        byte  volume_label[24];     /* Volume entry from block 0 11 ascii bytes for fat 22 unicode characters for exFat */
        int   bytespcluster;        /*  */
        dword byte_into_cl_mask;    /* And this with file pointer to get the
                                       byte offset into the cluster */
        int     fasize;             /* Nibbles per fat entry. (3,4 or 8) */
        dword   rootblock;          /* First block of root dir */
        dword   firstclblock;       /* First block of cluster area */
        dword   maxfindex;          /* Last element in the fat - fat32*/
        dword   fatblock;           /* First block in fat */
        int     secproot;           /* blocks in root dir */
        dword   bootaddr;
        byte    oemname[9];
        int     bytespsector;       /* Multiple of 512 usually is 512 */
        dword   bytemasksec;        /* Mask to get byte offset (bytespsector-1) */
        int     log2_bytespsec;     /* Log of bytes per sector */
        int     inopblock;          /* 16 for 512 sector size */
        int     blockspsec;         /*1 for 512 sector size, 2 for 1024 sector size, 8 for 4096 */

#if (INCLUDE_EXFATORFAT64)    	/* exfat FINODE structure extensions */
        dword   secpalloc;          /* Sectors per cluster */
#else
        byte  secpalloc;          /* Sectors per cluster */
#endif
        int     log2_secpalloc;     /* Log of sectors per cluster */
        int     log2_bytespalloc;   /* Log of bytes per cluster */

        int     clpfblock32;        /* Cluster indeces in a FAT block for FAT32 (128, 256, 512  etc */
        dword   cl32maskblock;      /* Mask to get cluster index offset (clpfblock32-1) */
        int     clpfblock16;        /* Cluster indeces in a FAT block for FAT32 (256, 512, 1204 etc */
        dword   cl16maskblock;      /* Mask to get cluster index offset (clpfblock16-1) */

        word    secreserved;        /* Reserved sectors before the FAT */
        byte    numfats;            /* Number of FATS on the disk */
        word    numroot;            /* Maximum # of root dir entries */
        dword   numsecs;            /* Total # sectors on the disk */
        byte    mediadesc;          /* Media descriptor byte */
        dword   secpfat;            /* Size of each fat */
        word    secptrk;            /* sectors per track */
        word    numhead;            /* number of heads */
        dword   numhide;            /* # hidden sectors */
        dword   free_contig_base;   /* Guess of where file data would most ProPlus does not use*/
        dword   free_contig_pointer;/* Efficiently stored  ProPlus does not use*/
        dword   known_free_clusters;/* free clusters on the disk */
        word    infosec;            /* Only used for fat32 */
        dword  partition_base;      /* Start of the partition */
        dword  partition_size;      /* Size of the partition  */
        int    partition_type;      /* Partition type */
#define SETISEXFAT(P) P->drive_info.isExfat = 1
#define ISEXFAT(P) P->drive_info.isExfat
#define ISEXFATORFAT64(P) P->drive_info.isExfat
        int    isExfat;
    } DDRIVE_INFO;


typedef struct drive_info {
        /* The following fields are always updated by pc_diskio_info */
		BOOLEAN  free_manager_enabled;
		BOOLEAN is_exfat;
        dword    drive_opencounter;
		dword    sector_size;
        dword    erase_block_size;   /* sectors per erase block, zero if no erase blocks */
		dword    cluster_size;
		dword    total_clusters;
		dword    free_clusters;
        dword    fat_entry_size;
        dword    region_buffers_total;
		dword    region_buffers_free;
		dword    region_buffers_low_water;
        dword    drive_operating_policy;
        /* The following field is updated by pc_diskio_stats when extended is specified */
		dword    free_fragments;
} DRIVE_INFO;

typedef struct drive_runtime_stats {
		dword    async_steps;

		dword    fat_reads;
		dword    fat_blocks_read;
		dword    fat_writes;
		dword    fat_blocks_written;
		dword    fat_buffer_swaps;

		dword    dir_buff_hits;
		dword    dir_buff_reads;
		dword    dir_buff_writes;
		dword    dir_direct_reads;
		dword    dir_direct_blocks_read;
		dword    dir_direct_writes;
		dword    dir_direct_blocks_written;

        dword    file_buff_hits;
		dword    file_buff_reads;
		dword    file_buff_writes;
		dword    file_direct_reads;
		dword    file_direct_blocks_read;
		dword    file_direct_writes;
		dword    file_direct_blocks_written;
} DRIVE_RUNTIME_STATS;

/* This structure is included in the RTFS drive structure for Pro and ProPlus
   fields required for ProPlus only are conditionally included */
typedef struct ddrive_state {
    struct blkbuff *allocated_user_buffer;
    struct blkbuffcntxt *pbuffcntxt;
#if (INCLUDE_RTFS_FREEMANAGER)
    /* freelist context */
    dword free_ctxt_slot_size;
    dword free_ctxt_cluster_shifter;
    REGION_FRAGMENT *free_ctxt_hash_tbl[RTFS_FREE_MANAGER_HASHSIZE];
#endif
#if (INCLUDE_FAILSAFE_RUNTIME)
    void    *failsafe_context;  /* Failsafe context block if enabled */
#endif

#if (INCLUDE_RTFS_PROPLUS)    /* ProPlus drive_state extensions */
    int      drive_async_state;
    dword    drv_async_current_cluster;
    dword    drv_async_current_fatblock;
    dword    drv_async_region_start;
    dword    drv_async_region_length;
    struct pc_file *asy_file_pfirst;   /* list of currently active async files */
#endif
} DDRIVE_STATE;

#if (INCLUDE_RTFS_FREEMANAGER)
#define CHECK_FREEMG_OPEN(PDR)    (PDR->drive_state.free_ctxt_cluster_shifter != 0)
#define CHECK_FREEMG_CLOSED(PDR)    (PDR->drive_state.free_ctxt_cluster_shifter == 0)
#else
int free_manager_disabled(int);
#define CHECK_FREEMG_OPEN(PDR)    free_manager_disabled(0)  /* Call routines that return 0 so compilers do not complain */
#define CHECK_FREEMG_CLOSED(PDR)  free_manager_disabled(1)
#endif

/* This structure is included in the RTFS drive structure and contains drive configuration
   values passed through pc_diskio_configure.
   For ProPlus  additional fields are conditionally included */

typedef struct driveusercontext {
#if (INCLUDE_FAILSAFE_CODE)
    /* Failsafe context provided to pc_diskio_configure */
    void    *user_failsafe_context;
#endif
    /* Fat buffer context provided to pc_diskio_configure */
	dword   user_num_fat_buffers;
    struct fatbuff *fat_buffer_structures;          /* address of buffer pool */
    int     fat_buffer_pagesize;
    byte   *user_fat_buffer_data;                   /* address of buffer pool */

    /* Current user buffer, same as assigned_user_buffer unless overriddedn by a call to pc_efilio_setbuff */
    dword  drive_operating_policy;    /* by user */
#if (INCLUDE_RTFS_PROPLUS)    /* ProPlus drive_user context extensions */
#if (INCLUDE_DEBUG_TEST_CODE)
        IO_RUNTIME_STATS    iostats;
#endif
#endif /* ProPlus drive_state extensions */
} DRIVEUSERCONTEXT;

typedef struct rtfsfmtparmsex {
		BOOLEAN 	   	scrub_volume;	        /* If true erase or zero the section of media to contain the volume formatting */
		/* Fixed format parameters. If bits_per_cluster is set to a non-zero value then numroot, numfats, and secpalloc must
		   also be provided. Rtfs will not calculate these parameters and instead use these values. Rtfs does not check the values
		   for correctness, porper values must be provided */
		unsigned char	bits_per_cluster; /* 12, 16, 32 == FAT12, FAT16, FAT32 */
        unsigned short  numroot;          /* # of root dir entries (normally 512 for FAT12, FAT16, must be 0 for FAT32*/
        unsigned char   numfats;          /* Number of FATS on the disk, Must be 2 if using Failsafe  */
        unsigned char   secpalloc;        /* Sectors per cluster */
        unsigned short  secreserved;	  /* # of root reserved sectors. usually 32 for FAT32, 1 for not FAT32 */

} RTFSFMTPARMSEX;


/* Parameter block for formatting: Used by format.c */
typedef struct fmtparms {
        unsigned char    oemname[9];      /* Only first 8 bytes are used */
        unsigned char   secpalloc;        /* Sectors per cluster */
        unsigned short  secreserved;      /* Reserved sectors before the FAT */
		unsigned char	nibs_per_entry;   /* 3, 4, 8 == FAT12, FAT16, FAT32 */
        unsigned char   numfats;          /* Number of FATS on the disk */
        dword           secpfat;          /* Sectors per fat */
        dword           numhide;          /* Hidden sectors */
        unsigned short  numroot;          /* Maximum # of root dir entries */
        unsigned char   mediadesc;        /* Media descriptor byte */
        unsigned short  secptrk;          /* sectors per track */
        unsigned short  numhead;          /* number of heads */
        unsigned short  numcyl;           /* number of cylinders */
        dword           total_sectors;    /* Total number of sectors (secptrk*numhead*numcyl) before any truncation */
        unsigned char physical_drive_no;
        dword binary_volume_label;
        unsigned char  text_volume_label[12];
        unsigned char  file_sys_type[12];
		unsigned long  bytes_per_sector;
		/* Additional extended format parameters */
		BOOLEAN 	   	scrub_volume;	        /* If true erase or zero the media before formatting */
		unsigned char	fixed_bits_per_cluster; /* 12, 16, 32 == FAT12, FAT16, FAT32 */
        unsigned short  fixed_numroot;          /* Maximum # of root dir entries */
        unsigned char   fixed_numfats;          /* Number of FATS on the disk */
        unsigned char   fixed_secpalloc;        /* Sectors per cluster */
        unsigned short  fixed_secreserved;      /* Reserved sectors before the FAT */
        } FMTPARMS;

struct mbr_entry_specification {
    dword partition_start;
    dword partition_size;
    byte  partition_type;
    byte  partition_boot;   /* 0x80 for bootable */
};
struct mbr_specification {
    int    device_mbr_count;
    dword  mbr_sector_location;
    struct mbr_entry_specification entry_specifications[4];
};


/* Structure passed to BLK_DEV_volume_mount_parms, to retrieve mount instructions and buffers */
typedef struct rtfsdevi_volume_mount_parms
{
	BOOLEAN dynamically_allocated;

    dword drive_operating_policy;       /* drive operating policy, see Rtfs manual */
    dword n_sector_buffers;             /* Total number of sector sized buffers (used for director buffers and scratch sectors */
    dword n_fat_buffers;                /* Total number of FAT buffers */
    dword fat_buffer_page_size_sectors; /* Number of sectors per FAT buffer */

    dword n_file_buffers;               /* Total number of file buffers */
    dword file_buffer_size_sectors;     /* file buffer size in sectors */


    /* Memory returned values */
    /* Note: Rtfs never references blkbuff_memory or fatbuff_memory, they are provided for the bllk_dev layer so it
       may allocate and release multiple control structures in single calls */
    void *blkbuff_memory;               /* 1 element must be sizeof(BLKBUFF)(sizeof(BLKBUFF)=40) * (n_sector_buffers) bytes wide */
    void *filebuff_memory;               /* 1 element must be sizeof(BLKBUFF)(sizeof(BLKBUFF)=40) * (n_file_buffers) bytes wide */
    void *fatbuff_memory;               /* 1 element must be sizeof(FATBUFF)(sizeof(FATBUFF)=40) * n_fat_buffers) bytes wide */

    byte *sector_buffer_base;         /* Unaligned sector buffer heap, returns from malloc */
    byte *file_buffer_base;           /* Unaligned file buffer heap, returns from malloc */
    byte *fat_buffer_base;            /* Unaligned fat buffer heap, returns from malloc */


    void *sector_buffer_memory;         /* n_sector_buffers elements (must be a linked list, each element sector_size bytes wide */
    void *file_buffer_memory;           /* n_file_buffers  elements (must be a linked list, each element file_buffer_size_sectors * sector_size bytes wide */
    void *fat_buffer_memory;            /* n_fat_buffers elements (must be a linked list, each element fat_buffer_page_size_sectors * sector_size bytes wide */

#if (INCLUDE_FAILSAFE_CODE)
    dword fsrestore_buffer_size_sectors;/* Failsafe restore buffer size in sectors */
    dword fsjournal_n_blockmaps;        /* number of Failsafe sector remap records provided.
                                           Determines the number of outstanding remapped sectors permitted */
    dword fsindex_buffer_size_sectors;  /* Failsafe index buffer size in sectors */
    void *fsfailsafe_context_memory;    /* Failsafe context block */
    void *fsrestore_buffer_memory;      /* 1 element must be (fsrestore_buffer_size_sectors * sector_size) bytes */
    void *fsjournal_blockmap_memory;    /* 1 element must be (fsjournal_n_blockmaps * sizeof(FSBLOCKMAP)) bytes. sizeof(FSBLOCKMAP) equals 16 */
    void *fsindex_buffer_memory;		/* 1 element must be sector_size bytes */
    byte *failsafe_buffer_base;
    byte *failsafe_indexbuffer_base;
#endif

} RTFS_DEVI_VOLUME_MOUNT_PARMS;


/* Structure passed from the blk_dev layer to RTFS_DEVI_device_mount() when new media comes on line */
typedef struct rtfs_devi_media_parms
{
    void  *devhandle;                   /* Handle Rtfs will pass to device_io() and other functions. devhandle is opaque to rtfs */
#define DEVICE_REMOVE_EVENT		0x01
	dword mount_flags;
    dword access_semaphore;	            /* Access semaphore for the device. */
    dword media_size_sectors;           /* Total number of addressable sectors on the media */
    dword numheads;                     /* cylinder, head, sector representation of the media. */
    dword numcyl;                       /* Note: must be valid FAT HCN values. max cyl = 1023, max heads == 255, max sectors = 63 */
    dword secptrk;
    dword sector_size_bytes;            /* Sector size in bytes: 512, 2048, etc */
    dword eraseblock_size_sectors;      /* Sectors per erase block. Set to zero for media without erase blocks */
    int   is_write_protect;             /* Set to one if the media is write protected */
    byte  *device_sector_buffer_base;
    void  *device_sector_buffer;
    dword device_sector_buffer_size;

	int   unit_number;			/* which instance of this device */
	int   device_type;			/* Used by blk dev driver layer. device mount sets it, volume mount may use it to configure buffering */

    int  (*device_io)(void  *devhandle, void * pdrive, dword sector, void  *buffer, dword count, BOOLEAN reading);
    int  (*device_erase)(void  *devhandle, void *pdrive, dword start_sector, dword nsectors);
    int  (*device_ioctl)(void  *devhandle, void *pdrive, int opcode, int iArgs, void *vargs);
    int  (*device_configure_media)(struct rtfs_media_insert_args *pmedia_parms, struct rtfs_media_resource_reply *media_config_block, int sector_buffer_required);
    int  (*device_configure_volume)(struct rtfs_volume_resource_request *prequest_block, struct rtfs_volume_resource_reply *preply_block);
} RTFS_DEVI_MEDIA_PARMS;

/* A placeholder vector must be provided by the device driver if it registers a function to poll for device changes.  */
typedef struct rtfs_devi_poll_request_vector {
	struct rtfs_devi_poll_request_vector *pnext;
    void (*poll_device_ready)(void);
} RTFS_DEVI_POLL_REQUEST_VECTOR;


typedef struct ddrive {
        struct ddrive *pnext_free; /* Free list */
        int drive_opencounter;      /* Value of global opencounter when we mounted */
        int     driveno;            /* Driveno. Set when open succeeds */
        DDRIVE_INFO drive_info;
        DDRIVE_STATE drive_state;
#if (INCLUDE_DEBUG_RUNTIME_STATS)
        DRIVE_RUNTIME_STATS drive_rtstats;
#endif
        /* These buffers are used for parsing arguments to API calls.
           they are large, so rather then use the stack we allocate
           scratch buffers for the duration of the API call that needs then */
        byte   *pathname_buffer;
        byte   *filename_buffer;
        BLKBUFF *pathname_blkbuff;
        BLKBUFF *filename_blkbuff;

        /* Beyond this is initialized at run time and must not be cleared by ertfs */
        DRIVEUSERCONTEXT            du;

        FATBUFFCNTXT fatcontext;    /* Controls fat cache */

/*
DRIVE_FLAGS_PARTITIONED     YES. Used but set unconditionally. could be eliminated.
DRIVE_FLAGS_PCMCIA          ONLY In PCMCIA segments which are disabled.
DRIVE_FLAGS_PCMCIA_ATA      Not used
DRIVE_FLAGS_FAILSAFE        Not used
DRIVE_FLAGS_VALID           YES - Usage between device dirivers and common code is different. should be 2.
DRIVE_FLAGS_REMOVABLE       Not used by v1.1 core code. needs to be removed where it is sert. used by 1.0 drivers
DRIVE_FLAGS_INSERTED        Not used by v1.1 core code. needs to be removed where it is sert. used by 1.0 drivers
DRIVE_FLAGS_FORMAT          Not used by v1.1 core code. Used by 1.0 drivers.
DRIVE_FLAGS_CDFS            Only used in IDE driver should remove
DRIVE_FLAGS_RDONLY          Not used
DRIVE_FLAGS_FILEIO          Used
*/

/* user init is required for the following elements required */
/* Flags - These must be set by the pc_ertfs_init */
#define DRIVE_FLAGS_PARTITIONED     0x0002  /* Partitioned device  */
#define DRIVE_FLAGS_PCMCIA          0x0004  /* Pcmcia device */
#define DRIVE_FLAGS_PCMCIA_ATA      0x0008
#define DRIVE_FLAGS_FAILSAFE        0x0800  /* Automatically initialize  Pro Only
                                               failsafe operations for
                                               the device. */
/* Flags - These must be set by the warmstrt IOCTL call to the driver */
/* VALID is set by the device driver as a result of a successful call to
   the device ioctl call DEVCTL_WARMSTART. If the driver does not set this
   flag then it i assumed that the driver probe or init sequence failed */
#define DRIVE_FLAGS_VALID           0x0001  /* Flags have been set */
#define DRIVE_FLAGS_REMOVABLE       0x0040  /* Device is removable */
#define DRIVE_FLAGS_INSERTED        0x0080  /* Device drivers use to
                                               remember states */
#define DRIVE_FLAGS_FORMAT          0x0100  /* If set by the driver then
                                               rtfs_init must format
                                               the device before it
                                               is usable. */
#define DRIVE_FLAGS_CDFS            0x0200
#define DRIVE_FLAGS_RDONLY          0x0400  /* Device is read only */
#define DRIVE_FLAGS_FILEIO          0x0020  /* Set by RTFS when the current
                                               block transfer is file io */
                                            /* only used by the driver */
        dword drive_flags;                  /* Note: the upper byte is reserved
                                               for private use by device drivers */
/* Note: look for redundancy in mount parms and legacy methods */
        RTFS_DEVI_MEDIA_PARMS  *pmedia_info;    /* Each mount drive structure on a devices points to media info provided by the blk device driver */
        RTFS_DEVI_VOLUME_MOUNT_PARMS mount_parms;
        BLKBUFFCNTXT _buffctxt;
        BLKBUFF *file_buffer_freelist;
        dword dyn_partition_base;
        dword dyn_partition_size;
		byte dyn_partition_type;
        int     controller_number;
        int     logical_unit_number;
        int     partition_number;
#if (INCLUDE_EXFAT)	/* Include eXfat drive extension in drive structure */
        EXFATDDRIVE exfatextension;
#endif

#if (INCLUDE_V_1_0_DEVICES == 1)
        int     pcmcia_slot_number;
        int     pcmcia_controller_number;
        byte    pcmcia_cfg_opt_value;
        dword   register_file_address;
        int     interrupt_number;      /* note -1 is polled for IDE */
        /* These two routines are attached to device driver specific routines */
        BOOLEAN (*dev_table_drive_io)(int driveno, dword sector, void  *buffer, word count, BOOLEAN readin);
        int (*dev_table_perform_device_ioctl)(int driveno, int opcode, void * arg);
#endif
    } DDRIVE;

/* Drive configure options for ProPlus */
#define DRVPOL_DISABLE_AUTOFLUSH          0x01
#define DRVPOL_DISABLE_AUTOFAILSAFE       0x02
#define DRVPOL_DISABLE_FREEMANAGER   	  0x04
#define DRVPOL_ASYNC_FATFLUSH         	  0x08
#define DRVPOL_ASYNC_JOURNALFLUSH         0x10
#define DRVPOL_ASYNC_RESTORE       	  	  0x20
#define DRVPOL_NAND_SPACE_OPTIMIZE   	  0x40
#define DRVPOL_NAND_SPACE_RECOVER    	  0x80
#define DRVPOL_DISABLE_AUTOMOUNT         0x100 /* Used for testing. If it is set the automount feature is disabled and the application layer performs the mount */


#define DRVPOL_ALL_VALID_PRO_OPTIONS (DRVPOL_DISABLE_FREEMANAGER|DRVPOL_NAND_SPACE_OPTIMIZE|DRVPOL_NAND_SPACE_RECOVER|DRVPOL_DISABLE_AUTOMOUNT|DRVPOL_DISABLE_AUTOFAILSAFE)

#if (INCLUDE_RTFS_PROPLUS)
/* ProPlus support all options */
#define DRVPOL_ALL_VALID_USER_OPTIONS (DRVPOL_ALL_VALID_PRO_OPTIONS|DRVPOL_DISABLE_AUTOFLUSH|DRVPOL_ASYNC_FATFLUSH|DRVPOL_ASYNC_JOURNALFLUSH|DRVPOL_ASYNC_RESTORE)
#else
/* No drive options supported basic */
#define DRVPOL_ALL_VALID_USER_OPTIONS DRVPOL_ALL_VALID_PRO_OPTIONS
#endif


/* Object used to find a dirent on a disk and its parent's */
typedef struct drobj {
        struct ddrive  *pdrive;
        struct finode  *finode;
        DIRBLK  blkinfo;
        BOOLEAN isroot;      /* True if this is the root */
        BOOLEAN is_free;     /* True if on the free list */
        BLKBUFF *pblkbuff;
        } DROBJ;
/* Internal file representation */

/* RTFS Pro Internal file representation, included in PC_FILE structure
   If ProPlus is enabled an additional structure is included */
typedef union pc_file_basic {
    dword       file_pointer;    /* Current file pointer */
#if (INCLUDE_MATH64)          /* ddword type, ProPlus only */
    ddword      file_pointer64;  /* Current file pointer */
#endif
    } PC_FILE_BASIC;

union file_control {
    PC_FILE_BASIC basic;
#if (INCLUDE_RTFS_PROPLUS)   /* ProPlus file structure extensions */
    PC_FILE_PROPLUS plus;
#endif
    };

typedef struct pc_file {
    BOOLEAN     is_free;        /* If TRUE this FILE may be used (see pc_memry.c) */
    int         my_fd;          /* Accessed through this file descriptor */
    DROBJ *     pobj;           /* Info for getting at the inode */
    word        flag;           /* Acces flags from po_open(). */
    union file_control fc;
    } PC_FILE;


/* INTERNAL !! */
/* Structure to contain block 0 image from the disk */
struct pcblk0 {
        byte  jump;               /* Should be E9 or EB on formatted disk */
        int   fasize;             /* Nibbles per fat entry if determined from bpb. (3,4 or 8) 0 if could not be determined */
        byte  oemname[9];
        word  bytspsector;        /* Must be 512 for this implementation */
        byte  secpalloc;          /* Sectors per cluster */
        word  secreserved;        /* Reserved sectors before the FAT */
        byte  numfats;            /* Number of FATS on the disk */
        word  numroot;            /* Maximum # of root dir entries */
        word  numsecs;            /* Total # sectors on the disk */
        byte  mediadesc;          /* Media descriptor byte */
        word  secpfat;            /* Size of each fat */
        word  secptrk;            /* sectors per track */
        word  numhead;            /* number of heads */
        word  numhide;            /* # hidden sectors High word if DOS4 */
        word  numhide2;           /* # hidden sectors Low word if DOS 4 */
        dword numsecs2;           /* # secs if numhid+numsec > 32M (4.0) */
        dword secpfat2;           /* Size of FAT in sectors (fat32 only) */
        byte  physdrv;            /* Physical Drive No. (4.0) */
        byte  xtbootsig;          /* Extended signt 29H if 4.0 stuf valid */
        dword volid;              /* Unique number per volume (4.0) */
        byte  vollabel[11];       /* Volume label (4.0) */
        word  flags;              /* Defined below (fat32 only) */
#define NOFATMIRROR 0x0080
#define ACTIVEFAT   0x000F
        word    fs_version;         /* Version of fat32 used (fat32 only) */
        dword   rootbegin;          /* Location of 1st cluster in root dir(fat32 only) */
        word    infosec;            /* Location of information sector (fat32 only) */
        word    backup;             /* Location of backup boot sector (fat32 only) */
        dword   free_alloc;         /* Free clusters on drive (-1 if unknown) (fat32 only) */
        dword   next_alloc;         /* Most recently allocated cluster (fat32 only) */
        };

#define CS_CHARSET_NOT_UNICODE  0
#define CS_CHARSET_UNICODE      1
#define WHICH_CHARSET CS_CHARSET_NOT_UNICODE

#define CS_OP_CP_CHR(TO,FR,CS) rtfs_cs_char_copy((TO),(FR), CS)
#define CS_OP_INC_PTR(P,CS) P = rtfs_cs_increment((P), CS)
#define CS_OP_CMP_CHAR(P1, P2,CS) rtfs_cs_compare((P1), (P2), CS)
#define CS_OP_CMP_CHAR_NC(P1, P2,CS) rtfs_cs_compare_nc((P1), (P2), CS)
#define CS_OP_ASCII_INDEX(P,C,CS) rtfs_cs_ascii_index(P,C, CS)
#define CS_OP_TO_LFN(TO, FROM,CS) rtfs_cs_to_unicode(TO, FROM, CS)
#define CS_OP_LFI_TO_TXT(TO, FROM,CS) rtfs_cs_unicode_to_cs(TO, FROM, CS)
#define CS_OP_IS_EOS(P,CS) rtfs_cs_is_eos(P, CS)
#define CS_OP_IS_NOT_EOS(P,CS) rtfs_cs_is_not_eos(P, CS)
#define CS_OP_TERM_STRING(P,CS) rtfs_cs_term_string(P, CS)
#define CS_OP_CMP_ASCII(P,C,CS) rtfs_cs_cmp_to_ascii_char((P),C,CS)
#define CS_OP_ASSIGN_ASCII(P,C,CS) rtfs_cs_assign_ascii_char((P),C,CS)
#define CS_OP_GOTO_EOS(P,CS) rtfs_cs_goto_eos((P),CS)

/* If mask specifies forcing asci characters to lower case or in 0x20 to ascii letter to force lower case */
#define CS_APPLY_NT_CASEMASK(MASK,C) (MASK && (C >= 'A') && (C <= 'Z'))?C|0x20:C

/* Make sure memory is initted prolog for api functions */
#define CHECK_MEM(TYPE, RET)  if (!prtfs_cfg) {return((TYPE) RET);}
#define VOID_CHECK_MEM()  if (!prtfs_cfg) {return;}
#define IS_AVOLORDIR(X) ((X->isroot) || (X->finode->fattribute & AVOLUME|ADIRENT))

/* Extra arguments to pc_get_inode() and pc_findin() */
#define GET_INODE_MATCH  0 /* Must match the pattern exactly */
#define GET_INODE_WILD   1 /* Pattern may contain wild cards */
#define GET_INODE_STAR   2 /* Like he passed *.* (pattern will be null) */
#define GET_INODE_DOTDOT 3 /* Like he past .. (pattern will be null */

#define FATPAGEREAD  0x00000001ul
#define FATPAGEWRITE 0x80000000ul
// HEREHERE - reduceme and clear current drive for all users if ejected
/* User structure management */
typedef struct rtfs_system_user
{
    dword         task_handle;     /* Task this is for */
    int           rtfs_errno;       /* current errno value for the task */
#if (INCLUDE_DEBUG_VERBOSE_ERRNO)
    char          *rtfs_errno_caller; /* If diagnostics enabled File name */
    long          rtfs_errno_line_number; /* If diagnostics enabled File line number */
#endif

    int           dfltdrv;          /* Default drive to use if no drive specified  1-26 == a-z */
    void *        plcwd;            /* current working directory, allocated at init time to hold NDRIVES pointers */
#if (INCLUDE_EXFATORFAT64)		/* ExFat cwd strings per drive (13 K) */
	word		  cwd_string[26][EMAXPATH_CHARS];
#endif
} RTFS_SYSTEM_USER;

typedef struct rtfs_system_user  *PRTFS_SYSTEM_USER;


#define ARDONLY 0x1  /* MS-DOS File attributes */
#define AHIDDEN 0x2
#define ASYSTEM 0x4
#define AVOLUME 0x8
#define ADIRENT 0x10
#define ARCHIVE 0x20
#define ANORMAL 0x00
#define CHICAGO_EXT 0x0f    /* Chicago extended filename attribute */

/*  Information buffer for pc_get_dirent_info and pc_set_dirent_info */
typedef struct dirent_info {
    byte   fattribute;
    dword   fcluster;
    word   ftime;
    word   fdate;
    dword  fsize;
#if (INCLUDE_EXFATORFAT64)
	ddword  fsize64;
#endif
    dword  my_block;
    int   my_index;
} DIRENT_INFO;

/* Structure for use by pc_gfirst, pc_gnext */
typedef struct dstat {
        unsigned char    fname[10];           /* Null terminated file and extension only 9 bytes used */
        unsigned char    fext[4];
#if (INCLUDE_VFAT)
        unsigned char    lfname[FILENAMESIZE_BYTES];         /* Long file name for vfat. */
#else
        unsigned char    lfname[14];                   /* Long file name non-vfat. */
#endif
        unsigned char    filename[14];       /* Null terminated file.ext only 13 bytes used */
        unsigned char    fattribute;         /* File attributes */
        unsigned short    ftime;              /* time & date lastmodified. See date */
        unsigned short    fdate;              /* and time handlers for getting info */
        unsigned short    ctime;              /* time & date created */
        unsigned short    cdate;
        unsigned short    atime;              /* time & date accessed */
        unsigned short    adate;			  /* Date last accessed */
        dword   fsize;              /* File size */
        dword   fsize_hi;            /* File size hi if exFat file */
        /* INTERNAL */
        int     driveno;
        int drive_opencounter;      /* Value of drive structures opencounter */
        unsigned char    pname[FILENAMESIZE_BYTES];
        unsigned char    pext[4];
        unsigned char    path[EMAXPATH_BYTES];
        dword  my_block;              /* used by pc_gread() to acces the directory entry */
        int   my_index;
        void   *pobj;                 /* Info for getting at the inode */
        void   *pmom;                 /* Info for getting at parent inode */
#define SEARCH_BACKWARDS_MAGIC_NUMBER 0x12214343
#define SYS_SCAN_MAGIC_NUMBER 0x43432121
		dword  search_backwards_if_magic;
        }
DSTAT;

/* Structure for use by pc_stat and pc_fstat */
/* Portions of this structure and the macros were lifted from BSD sources.
   See the RTFS ANSI library for BSD terms & conditions */
typedef struct ertfs_stat
{
    int st_dev;      /* (drive number, rtfs) */
    int st_ino;      /* inode number (0) */
    dword   st_mode;        /* (see S_xxxx below) */
    int st_nlink;      /* (always 1) */
    int st_rdev;        /* (drive number, rtfs) */
    dword   st_size;        /* file size, in bytes */
#ifdef st_atime
#undef st_atime
#undef st_mtime
#undef st_ctime
#endif
    DATESTR  st_atime;     /* last access (all times are the same) */
    DATESTR  st_mtime;     /* last modification */
    DATESTR  st_ctime;     /* last file status change */
    dword    st_blksize;   /* optimal blocksize for I/O (cluster size) */
    dword    st_blocks;     /* blocks allocated for file */
    unsigned char   fattribute;    /* File attributes - DOS attributes
                                (non standard but useful) */
    dword   st_size_hi;         /* File size hi always zero for Pro*/
} ERTFS_STAT;


/* Internal masks to dictate which date fileds to be update when a directory entry is written */
#define DATESETCREATE	1
#define DATESETUPDATE	2
#define DATESETACCESS	4

typedef struct chkdisk_stats {
	dword n_sectors_total;					/* Total sectors occupied by the volume */
    dword n_clusters_total; 				/* Total clusters in the voume */
    dword n_sectorspercluster;				/* Number of sectors pper cluster */
    dword n_reservedrootsectors; 			/* Number or sectors reserved for the root directory (0 for FAT32) */
    dword n_bytespersector;					/* Bytes per sector */
    dword  n_user_files;                    /* Total #user files found */
    dword  n_hidden_files;                  /* Total #hidden files found */
    dword  n_user_directories;              /* Total #directories found */
    dword  n_free_clusters;                 /* # free available clusters */
    dword  n_bad_clusters;                  /* # clusters marked bad */
    dword  n_file_clusters;                 /* Clusters in non hidden files */
    dword  n_hidden_clusters;               /* Clusters in hidden files */
    dword  n_dir_clusters;                  /* Clusters in directories */
    BOOLEAN has_errors;               		/* Set to TRUE by check disk if it detected any errors. (set if any of the following 8 errors are reported) */
    BOOLEAN has_endless_loop;               /* Set to TRUE by check disk if it stopped because it detected and endless loop */
    dword  n_crossed_chains;                /* Number of crossed chains. */
    dword  n_lost_chains;                   /* # lost chains */
    dword  n_lost_clusters;                 /* # lost clusters */
    dword  n_unterminated_chains;           /* # of chains that were not terminated */
    dword  n_badcluster_values;             /* # of invalid cluster values found in chains */
    dword  n_bad_dirents;                   /* # of directory entries containing bad cluster values or cluster values pointing to a free clusters */
    dword  n_bad_lfns;                      /* # corrupt/disjoint win95 lfn chains */
    dword  n_directories_removed;           /* # of subdirectories removed because of invalid contents */
    dword  n_files_removed;                 /* # of files removed because of invalid contents */
    dword  n_clusters_freed;                /* # of clusters free, includes lost clusters and any clusters freed when files or subdirectories where removed */
    dword  n_checkfiles_created;            /* # of check files created from lost cluster chains */
    dword  n_directory_scans;				/* # of times the directory tree was traversed */
} CHKDISK_STATS;

/************************************************************************
*
* CHKDSK data structure
 *
* pc_check_disk_ex requires a context block to store information it acquires as it scans the volume.
*
*   The caller must pass the address of a CHKDSK_CONTEXT structure to pc_check_disk_ex. pc_check_disk_ex will zero the data structure
*   and the use it.
*   If pc_check_disk_ex succeeds, this structure may then be passed to print_chkdsk_statistics()
************************************************************************/
#define CHKDSK_VERBOSE	   				0x01
#define CHKDSK_FIXPROBLEMS				0x02
#define CHKDSK_FREELOSTCLUSTERS			0x08
#define CHKDSK_FREEFILESWITHERRORS		0x10
#define CHKDSK_FREESUBDIRSWITHERRORS	0x20

typedef struct chkdsk_context {
	dword chkdsk_opmode;
    DDRIVE *drive_structure;                /* The drive we are working on */
	BOOLEAN has_endless_loop;				/* If 1 and endless loop in a cluster chain was detected, call again with fix_problems to clear */
	CHKDISK_STATS *pstats;
/* State machine logic controls rescanning options to identify directory objects with crossed cluster chains */
#define SC_STATE_PERFORM_SCAN	0
#define SC_STATE_REQUEST_RESCAN_FOR_CROSSED_CHAIN 1
#define SC_STATE_PERFORM_RESCAN_FOR_CROSSED_CHAIN 2
#define SC_STATE_FOUND_RESCAN_CROSSED_CHAIN 	  3
	int scan_state;
	dword crossed_cluster;
	DROBJ *scan_crossed_drobj;
	DROBJ *rescan_crossed_drobj;
    void *scratch_memory;					/* User supplied memory, optional for version 6 */
    void *scratch_segment_heap;				/* User supplied memory, threaded into a free list */
    void *lost_chain_list;					/* Type is actually REGION_FRAGMENT * */
    void *used_segment_list;				/* Type is actually REGION_FRAGMENT * */

    /* Use a global buffer so we do not load the stack up during recursion */
    byte gl_file_name[26];
    byte gl_file_path[EMAXPATH_BYTES];
    int recursion_depth;                  /* how deep in the stack we are */
	dword current_file_no;				  /* Used to build check files in multiple passes */
    dword n_bad_lfns;               /* # corrupt/disjoint win95 lfn chains */

    /* These fields bound the cluster map processing so we can process
    disks with very large FATs by making multiple passes */
    dword cl_start;
    dword cl_end;
    int on_which_pass;
} CHKDSK_CONTEXT;

/* Values for the st_mode field */
#define S_IFMT   0170000        /* type of file mask */
#define S_IFCHR  0020000       /* character special (unused) */
#define S_IFDIR  0040000       /* directory */
#define S_IFBLK  0060000       /* block special  (unused) */
#define S_IFREG  0100000       /* regular */
#define S_IREAD  0000400    /* Read permitted. (Always true anyway)*/
#define S_IWRITE 0000200    /* Write permitted  */

#define DEFFILEMODE (S_IREAD|S_IWRITE)
#define S_ISDIR(m)  ((m & 0170000) == 0040000)  /* directory */
#define S_ISCHR(m)  ((m & 0170000) == 0020000)  /* char special */
#define S_ISBLK(m)  ((m & 0170000) == 0060000)  /* block special */
#define S_ISREG(m)  ((m & 0170000) == 0100000)  /* regular file */
#define S_ISFIFO(m) ((m & 0170000) == 0010000)  /* fifo */

/* File creation permissions for open */
/* Note: OCTAL */
#define PS_IREAD  0000400   /* Read permitted. (Always true anyway)*/
#define PS_IWRITE 0000200   /* Write permitted  */


/* File access flags */
#define PO_RDONLY 0x0000        /* Open for read only*/
#define PO_WRONLY 0x0001        /* Open for write only*/
#define PO_RDWR   0x0002        /* Read/write access allowed.*/
#define PO_APPEND 0x0008        /* Seek to eof on each write*/
#define PO_BUFFERED 0x0010      /* Non-Block alligned File IO is buffered */
#define PO_AFLUSH   0x0020      /* Auto-Flush. File is flushed automatically
                                   each time po_write changes the size */
#define PO_CREAT  0x0100        /* Create the file if it does not exist.*/
#define PO_TRUNC  0x0200        /* Truncate the file if it already exists*/
#define PO_EXCL   0x0400        /* Fail if creating and already exists*/
#define PO_TEXT   0x4000        /* Ignored*/
#define PO_BINARY 0x8000        /* Ignored. All file access is binary*/
#define PO_NOSHAREANY   0x0004   /* Wants this open to fail if already open.
                                      Other opens will fail while this open
                                      is active */
#define PO_NOSHAREWRITE 0x0800   /* Wants this opens to fail if already open
                                      for write. Other open for write calls
                                      will fail while this open is active. */

/* Arguments to both po_lcseek and pc_efilio_lseek */
#define PSEEK_SET   0   /* offset from begining of file*/
#define PSEEK_CUR   1   /* offset from current file pointer*/
#define PSEEK_END   2   /* offset from end of file*/
#define PSEEK_CUR_NEG   3  /* negative offset from end of file*/

#if (INCLUDE_RTFS_PROPLUS)
/* Arguments to both po_lcseek and pc_efilio_lseek - these values start above
   the standard seek arguments */
#define PSEEK_SET_RAW      (PSEEK_CUR_NEG+1)   /* internal true offset from beginning of file */
/* Arguments available to pc_efilio_lseek only */
#define PSEEK_SET_CIRC     (PSEEK_SET_RAW+1)   /* offset from begining of file*/
#define PSEEK_CUR_CIRC     (PSEEK_SET_RAW+2)   /* offset from current file pointer*/
#define PSEEK_END_CIRC     (PSEEK_SET_RAW+3)   /* offset from end of file*/
#define PSEEK_CUR_NEG_CIRC (PSEEK_SET_RAW+4)  /* pc_efilio_seek only: negative offset from current*/
#endif


/* Partition table descriptions. */
/* One disk partition table */
typedef struct ptable_entry {
    byte  boot;
    byte  s_head;
    word  s_cyl;
    byte  p_typ;
    byte  e_head;
    word e_cyl;
    dword  r_sec;   /* Relative sector of start of part */
    dword  p_size;  /* Size of partition */
    } PTABLE_ENTRY;

typedef struct ptable {
    PTABLE_ENTRY ents[4];
    word signature; /* should be 0xaa55 */
    } PTABLE;



typedef struct dev_geometry {
    int     bytespsector;           /* 0 or 512 for 512 byte sectors otherwise, 1024, 2048, 4096 etc */
    int     dev_geometry_heads;      /*- Must be < 256 */
    dword   dev_geometry_cylinders; /*- Must be < 1024 */
    int     dev_geometry_secptrack;  /*- Must be < 64 */
    dword   dev_geometry_lbas;      /* For oversized media that supports logical */
                                    /* block addressing. If this is non zero */
                                    /* dev_geometry_cylinders is ignored */
                                    /* but dev_geometry_heads and */
                                    /* dev_geometry_secptrack must still be valid */
    int    fmt_parms_valid;         /*If the device io control call sets this */
                                    /*TRUE then it it telling the applications */
                                    /*layer that these format parameters should */
                                    /*be used. This is a way to format floppy */
                                    /*disks exactly as they are formatted by dos. */
    FMTPARMS fmt;
} DEV_GEOMETRY;

typedef struct dev_geometry  *PDEV_GEOMETRY;





/*        Terminal input/output macros.
*           RTFS_PRINT_LONG_1
*           RTFS_PRINT_STRING_1
*           RTFS_PRINT_STRING_2
*
*  These macros by default are defined as calls to functions in rtfsterm.c as seen here
*
*       #define RTFS_PRINT_LONG_1(L1,FLAGS) rtfs_print_long_1(L1,FLAGS)
*       #define RTFS_PRINT_STRING_1(STR1ID,FLAGS) rtfs_print_string_1(STR1ID,FLAGS)
*       #define RTFS_PRINT_STRING_2(STR1ID,STR2,FLAGS) rtfs_print_string_2(STR1ID,STR2,FLAGS)
*
*  If no console output available thy may be dfined as seen here
*
*       #define RTFS_PRINT_LONG_1(L1,FLAGS)
*       #define RTFS_PRINT_STRING_1(STR1ID,FLAGS)
*       #define RTFS_PRINT_STRING_2(STR1ID,STR2,FLAGS)
*
*/
/* String printing control characters used throughout the library */
#define PRFLG_NL        0x0001  /* Newline Carriage return at end */
#define PRFLG_CR        0x0002  /* Carriage Return only at end    */
/* Macros that are used to access the print routines */
#define RTFS_PRINT_LONG_1(L1,FLAGS) rtfs_print_long_1(L1,FLAGS)
#define RTFS_PRINT_STRING_1(STR1ID,FLAGS) rtfs_print_string_1(STR1ID,FLAGS)
#define RTFS_PRINT_STRING_2(STR1ID,STR2,FLAGS) rtfs_print_string_2(STR1ID,STR2,FLAGS)
/* These STUBBED versions may be used instead if output is not available */
/*
#define RTFS_PRINT_LONG_1(L1,FLAGS)
#define RTFS_PRINT_STRING_1(STR1ID,FLAGS)
#define RTFS_PRINT_STRING_2(STR1ID,STR2,FLAGS)
*/


#define MATCH_DIR       0x01
#define MATCH_VOL       0x02
#define MATCH_FILES     0x04
#define MATCH_DOT       0x08
#define MATCH_DOTDOT    0x10
#define MATCH_SYSSCAN   0x20 /* Internal*/
typedef int (*PENUMCALLBACK)(byte *path, DSTAT *pstat);

/* Flags to fatop routines */
#define FOP_RMTELL      0x01 /* Tell the regioon manager */
#define FOP_LINK        0x02 /* Link the fragment else null it */
#define FOP_LINK_PREV   0x04 /* Link the cluster argument to this fragment */
#define FOP_LINK_NEXT   0x08 /* Link this fragment to the cluster argument */
#define FOP_TERM_PREV   0x10 /* Terminat the cluster argument */
#define FOP_EXF_CHAIN   0x20 /* EXFAT ignores LINK TERM, but if this is set, do it */

/* File seg info structure. An array of these structures is passed
    to pc_get_file_extents(). The extents of the file are returned
    in this array */
typedef struct fileseginfo {
        dword    block;          /* Block number of the current extent */
        dword   nblocks;        /* Number of blocks in the extent */
        } FILESEGINFO;

/* Free list info structure. An array of these structures is passed
    to pc_get_free_list(). The list of free clusters is returned in
    this array */
typedef struct freelistinfo {
    dword       cluster;        /* Cluster where the free region starts */
    dword       nclusters;      /* Number of free clusters the free segment */
    } FREELISTINFO;



#define READ_PARTITION_ERR          -1 /* Internal error (couldn't allocate buffers ?) */
#define READ_PARTITION_NO_TABLE     -2 /* No partition table  */
#define READ_PARTITION_IOERROR      -3 /* Device IO error  */

/* System wide critical region semaphore */
#define OS_CLAIM_FSCRITICAL()   rtfs_port_claim_mutex(prtfs_cfg->critical_semaphore);
#define OS_RELEASE_FSCRITICAL() rtfs_port_release_mutex(prtfs_cfg->critical_semaphore);
#if (INCLUDE_EXFATORFAT64)
BOOLEAN pcexfat_rmnode(DROBJ *pobj);
BOOLEAN pcexfat_mvnode(DROBJ *old_parent_obj,DROBJ *old_obj,DROBJ *new_parent_obj, byte *filename,int use_charset);
BOOLEAN pcexfat_set_volume(DDRIVE *pdrive, byte *volume_label,int use_charset);
BOOLEAN pcexfat_get_volume(DDRIVE *pdrive, byte *volume_label,int use_charset);
BOOLEAN pcexfat_get_cwd(DDRIVE *pdrive, byte *path, int use_charset);
BOOLEAN pcexfat_set_cwd(DDRIVE *pdrive, byte *name, int use_charset);
BOOLEAN pcexfat_update_by_finode(FINODE *pfi, int entry_index, BOOLEAN set_archive, int set_date_mask, BOOLEAN do_delete);
BOOLEAN pcexfat_flush(DDRIVE *pdrive);
dword   exfatop_find_contiguous_free_clusters(DDRIVE *pdr, dword startpt, dword endpt, dword min_clusters, dword max_clusters, dword *p_contig, int *is_error);
BOOLEAN rtexfat_i_dskopen(DDRIVE *pdr);
void    pc_release_exfat_buffers(DDRIVE *pdr);
BOOLEAN pcexfat_findin( DROBJ *pobj, byte *filename, int action, BOOLEAN oneshot, int use_charset);
byte *  pcexfat_seglist2text(DDRIVE * pdrive, SEGDESC *s, byte *lfn, int use_charset);
dword   exFatfatop_getdir_frag(DROBJ *pobj, dword startpt, dword *pnext_cluster, dword n_clusters, int *end_of_chain);
dword   exFatfatop_getfile_frag(FINODE *pfi, dword startpt, dword *pnext_cluster, dword n_clusters, int *end_of_chain);
BOOLEAN pcexfat_gread(DSTAT *statobj, int blocks_to_read, byte *buffer, int *blocks_read);
BOOLEAN pcexfat_parse_path(DDRIVE *pdrive, byte *outpath, byte *inpath, int use_charset);
BOOLEAN rtexfat_gblk0(DDRIVE *pdr, struct pcblk0 *pbl0b, byte *b);
DROBJ * pcexfat_get_root( DDRIVE *pdrive);
BOOLEAN pc_exfatrfindin( DROBJ *pobj, byte *filename, int action, int use_charset, BOOLEAN starting);
#endif

#if (INCLUDE_FAILSAFE_RUNTIME)
struct rtfs_failsafe_cfg {
    BOOLEAN (*block_devio_read)(DDRIVE *pdrive, dword blockno, byte * buf);
    BOOLEAN (*block_devio_write)(BLKBUFF *pblk);
    BOOLEAN (*block_devio_xfer)(DDRIVE *pdrive, dword blockno, byte * buf, dword n_to_xfer, BOOLEAN reading);
    BOOLEAN (*fat_devio_read)(DDRIVE *pdrive, dword sectorno,dword nsectors, byte *fat_data);
    BOOLEAN (*fat_devio_write)(DDRIVE *pdrive, dword fat_blockno, dword nblocks, byte *fat_data, int fatnumber);
    BOOLEAN (*fs_recover_free_clusters)(DDRIVE *pdrive, dword required_clusters);
    BOOLEAN (*fs_failsafe_autocommit)(DDRIVE *pdr);
    BOOLEAN (*fs_add_free_region)(DDRIVE *pdr, dword cluster, dword n_contig);
    BOOLEAN (*fs_failsafe_dskopen)(DDRIVE *pdrive);
    BOOLEAN (*fs_dynamic_mount_volume_check)(DDRIVE *pdr);
    void 	(*fs_claim_buffers)(DDRIVE *pdr);
    BOOLEAN (*fs_allocate_volume_buffers)(struct rtfs_volume_resource_reply *pvolume_config_block, dword sector_size_bytes);
    void 	(*fs_free_disk_configuration)(DDRIVE *pdr);
    BOOLEAN (*fs_dynamic_configure_volume)(DDRIVE *pdr, struct rtfs_volume_resource_reply *preply);
    dword   (*free_fallback_find_contiguous_free_clusters)(DDRIVE *pdr, dword startpt, dword endpt, dword min_clusters, dword max_clusters, dword *p_contig, int *is_error);

};
#endif

/* Configuration structure. Must be filled in by the user.
   see rtfscfg.c */
typedef struct rtfs_cfg {
	int  dynamically_allocated;
    /* Configuration values */
    int cfg_NDRIVES;                    /* The number of drives to support */
    int cfg_NBLKBUFFS;                  /* The number of block buffers */
    int cfg_NUSERFILES;                 /* The number of user files */
    int cfg_NDROBJS;                    /* The number of directory objects */
    int cfg_NFINODES;                   /* The number of directory inodes */
    int cfg_NUM_USERS;                  /* The number of users to support */
    int cfg_NREGIONS;                   /* The number of region management objects to support */
    dword   region_buffers_free;
    dword   region_buffers_low_water;
#if (INCLUDE_RTFS_PROPLUS)  /* config structure: ProPlus specific element*/
    int cfg_NFINODES_UEX;                /* The number of combined extended and extended 64 directory inodes */
#endif
    /* Core that must be provided by the user */
    struct ddrive   *mem_drive_pool;           /* Point at cfg_NDRIVES * sizeof(DDRIVE) bytes*/
	RTFS_DEVI_MEDIA_PARMS *mem_mediaparms_pool; /* Point at cfg_NDRIVES * sizeof(RTFS_DEVI_MEDIA_PARMS) bytes*/
    BLKBUFF  *mem_block_pool;               /* Point at cfg_NBLKBUFFS * sizeof(BLKBUFF) bytes*/
    byte     *mem_block_data;               /* Point at NBLKBUFFS*RTFS_CFG_DEFAULT_BLOCK_SIZE bytes */
    struct pc_file  *mem_file_pool;            /* Point at cfg_USERFILES * sizeof(PC_FILE) bytes*/
    struct finode   *mem_finode_pool;          /* Point at cfg_NFINODE * sizeof(FINODE) bytes*/
    DROBJ    *mem_drobj_pool;           /* Point at cfg_NDROBJ * sizeof(DROBJ) bytes*/
    struct region_fragment *mem_region_pool;    /* Point at cfg_NREGIONS * sizeof(REGION_FRAGMENT) bytes*/
    RTFS_SYSTEM_USER *rtfs_user_table;      	/* Point at cfg_NUM_USERS * sizeof(RTFS_SYSTEM_USER) bytes*/
    void **           rtfs_user_cwd_pointers;   /* Point at cfg_NUM_USERS * cfg_NDRIVES * sizeof(void *) bytes*/

    struct region_fragment *mem_region_freelist;



#if (INCLUDE_RTFS_PROPLUS)  /* config structure: ProPlus specific element*/
    FINODE_EXTENSION_MEMORY *mem_finode_uex_pool;  /* Point at cfg_NFINODES_UEX * sizeof(FINODE_EXTENSION_MEMORY) bytes*/
    FINODE_EXTENSION_MEMORY *mem_finode_uex_freelist;
#endif

    dword  rtfs_exclusive_semaphore;  /* Used by Rtfs to run single threaded so buffers may be shared */
#if (INCLUDE_FAILSAFE_RUNTIME)
	struct rtfs_failsafe_cfg *pfailsafe;		/* Zero unless pc_failsafe_init() was called */
    dword shared_restore_transfer_buffer_size;
    byte  *shared_restore_transfer_buffer;
    byte  *shared_restore_transfer_buffer_base;
#endif


    dword shared_user_buffer_size;
    byte *shared_user_buffer_base;
    byte *shared_user_buffer;

    /* These pointers are internal no user setup is needed */
    BLKBUFFCNTXT buffcntxt;             /* Systemwide shared buffer pool */
    struct finode   *inoroot;          /* Begining of inode pool */
    struct finode   *mem_finode_freelist;
    DROBJ    *mem_drobj_freelist;
    struct ddrive   *mem_drive_freelist;
    struct ddrive   *drno_to_dr_map[26];
    dword  userlist_semaphore;  /* Used by ERTFS for accessing the user structure list */
    dword  critical_semaphore;  /* Used by ERTFS for critical sections */
    dword  mountlist_semaphore; /* Used by ERTFS for accessing the mount list */
/* Note: cfg_NDRIVES semaphores are allocated and assigned to the individual
   drive structure within routine pc_ertfs_init() */
    /* This value is set in pc_rtfs_init(). It is the drive number of the
       lowest (0-25) == A: - Z:. valid drive identifier in the system.
       If the user does not set a default drive, this value will be used. */
    int    default_drive_id;
    /* Counter used to uniquely identify a drive mount. Each time an open
       succeeds this value is incremented and stored in the drive structure.
       it is used by gfirst gnext et al to ensure that the drive was not
       closed and remounted between calls */
    int drive_opencounter;
    dword rtfs_driver_errno;   /* device driver can set driver specific errno value */
	RTFS_DEVI_POLL_REQUEST_VECTOR *device_poll_list;	/* Functions registered on this are called every time the API is entered to check for status change.  */
	/* Private callback functions that overrides calls to the user's callback handler */
	void   (* test_drive_async_complete_cb)(int driveno, int operation, int success);

#if (1) /* ||INCLUDE_V_1_0_DEVICES == 1) */
	dword 	floppy_semaphore;
	dword 	floppy_signal;
	dword 	ide_semaphore;
#endif

} RTFS_CFG;

#ifdef __cplusplus
extern "C" RTFS_CFG *prtfs_cfg;
#else
extern RTFS_CFG *prtfs_cfg;
#endif

/* Async operation status */
#define PC_ASYNC_COMPLETE    1
#define PC_ASYNC_CONTINUE    2
#define PC_ASYNC_ERROR      -1

/* Hint for how to allocate clusters, when ALLOC_CLUSTERS_ALIGNED and the media is erase block based
   then priority is given to allocating clusters from empty erase blocks */
#define ALLOC_CLUSTERS_PACKED     1
#define ALLOC_CLUSTERS_ALIGNED    2
#define ALLOC_CLUSTERS_UNALIGNED  3

#if (INCLUDE_DEBUG_TEST_CODE)
#define RTFS_DEBUG_LOG_DEVIO(DRIVESTRUCT, IOCATEGORY, BLOCKNO, NBLOCK) rtfs_devio_log_io(DRIVESTRUCT, IOCATEGORY, BLOCKNO, NBLOCK);
void rtfs_devio_log_io(DDRIVE *pdrive, int iocategory, dword blockno, dword nblock);
#else
#define RTFS_DEBUG_LOG_DEVIO(DRIVESTRUCT, IOCATEGORY, BLOCKNO, NBLOCK)
#endif

struct fat32_info {
        dword   fs_sig;             /* Signature of FAT32 (0x61417272) */
#define FSINFOSIG 0x61417272ul
        dword   free_alloc;         /* Free clusters on drive (-1 if unknown) */
        dword   next_alloc;         /* Most recently allocated cluster */
        dword   reserved;           /* Reserved - ignore */
        };

extern KS_CONSTANT byte * pustring_sys_oemname;
extern KS_CONSTANT byte * pustring_sys_volume_label;
extern KS_CONSTANT byte * pustring_sys_badlfn;
extern KS_CONSTANT byte * pustring_sys_badalias;
extern KS_CONSTANT byte * pustring_sys_ucreserved_names;
extern KS_CONSTANT byte * pustring_sys_lcreserved_names;

#if (INCLUDE_MATH64)
#if (INCLUDE_NATIVE_64_TYPE)
#define M64EQ(A,B)   ((A)==(B))
#define M64LT(A,B)   ((A)<(B))
#define M64LTEQ(A,B) ((A)<=(B))
#define M64GT(A,B)   ((A)>(B))
#define M64GTEQ(A,B) ((A)>=(B))
#define M64NOTZERO(A)((A)!=0)
#define M64ISZERO(A) ((A)==0)
#define M64IS64(A)   ((A)>=0x100000000)
#define M64HIGHDW(A) ((dword)((A) >> 32))
#define M64LOWDW(A)  ((dword)((A) & 0xffffffff))
#define M64MINUS(A,B)   ((A)-(B))
#define M64MINUS32(A,B) ((A)-(ddword)(B))
#define M64PLUS(A,B)    ((A)+(B))
#define M64PLUS32(A,B)  ((A)+(ddword)(B))
#define M64SET32(A,B)   m64_native_set32((A), (B))
#define M64LSHIFT(A,B)  ((A)<<(B))
#define M64RSHIFT(A,B)  ((A)>>(B))
#define M64MULT32(A,B)  ((A)*(ddword)(B))
#else
#define M64EQ(A,B)   ( ((A).hi==(B).hi) && ((A).lo==(B).lo) )
#define M64LT(A,B)   ( ((A).hi<(B).hi) || ( ((A).hi==(B).hi) && ((A).lo<(B).lo)  ))
#define M64LTEQ(A,B) ( ((A).hi<(B).hi) || ( ((A).hi==(B).hi) && ((A).lo<=(B).lo) ))
#define M64GT(A,B)   ( ((A).hi>(B).hi) || ( ((A).hi==(B).hi) && ((A).lo>(B).lo)  ))
#define M64GTEQ(A,B) ( ((A).hi>(B).hi) || ( ((A).hi==(B).hi) && ((A).lo>=(B).lo) ))
#define M64NOTZERO(A)( (A).hi||(A).lo )
#define M64ISZERO(A) ( ((A).hi==0)&&((A).lo==0) )
#define M64IS64(A)   ( ((A).hi!=0) )
#define M64HIGHDW(A) ( (A).hi )
#define M64LOWDW(A)  ( (A).lo )
#define M64MINUS(A,B)   m64_minus((A), (B))
#define M64MINUS32(A,B) m64_minus32((A), (B))
#define M64PLUS(A,B)    m64_plus((A), (B))
#define M64PLUS32(A,B)  m64_plus32((A), (B))
#define M64SET32(A,B)   m64_set32((A), (B))
#define M64LSHIFT(A,B)   m64_lshift((A), (B))
#define M64RSHIFT(A,B)   m64_rshift((A), (B))
#endif
#endif

#define PCE_FIRST_FIT        0x01
#define PCE_FORCE_FIRST      0x02
#define PCE_FORCE_CONTIGUOUS 0x04
#define PCE_KEEP_PREALLOC    0x08
#define PCE_CIRCULAR_FILE    0x10
#define PCE_CIRCULAR_BUFFER  0x20
#define PCE_REMAP_FILE       0x40
#define PCE_ASYNC_OPEN       0x100
#define PCE_TEMP_FILE        0x200
#define PCE_EXTENDED_FILE    0x400
#define PCE_TRANSACTION_FILE 0x800
#define PCE_LOAD_AS_NEEDED   0x1000


typedef struct efileoptions {
    dword allocation_policy;
    dword min_clusters_per_allocation;
    dword allocation_hint;
    /* The following field is for transaction files only */
#if (INCLUDE_TRANSACTION_FILES)
    byte  *transaction_buffer;
    dword transaction_buffer_size;
#endif
#if (INCLUDE_CIRCULAR_FILES)
    /* The following fields are for circular files only */
    dword circular_file_size_hi;    /* Loop back to zero when this is reached */
	dword circular_file_size_lo;    /* Loop back to zero when this is reached */
    int   n_remap_records;       /* User supplied space for remapping circular */
	REMAP_RECORD *remap_records; /* Regions to linear files */
#endif
} EFILEOPTIONS;


typedef struct ertfs_efilio_stat
{
    /* Minimum number of bytes that will are pre-allocated at one time
       when the file is extended- by default this is the cluster size
       of the volume but it may be effected by the
       extended file open option "min_clusters_per_allocation" */
    dword minimum_allocation_size;
    /* These are the policy bits that were set in the allocation_policy
    field of the extended file open call */
    dword allocation_policy;
	 /* NEW element 10/31/05 */
    /* The count of seperate disjoint fragment in the file */
    dword fragments_in_file;
	/* First cluster in the file */
    dword first_cluster;
	/* cluster used for data in the file */
    dword allocated_clusters;
	/* cluster pre-allocated for data in the file */
    dword preallocated_clusters;
	/* cluster to link */
    dword clusters_to_link;
	 /* End new element 10/31/05 */

    /* The file size in bytes */
    dword file_size_hi;
    dword file_size_lo;
    /* The number of bytes currently allocated to the file including
       the file contents (current_file_size) and any additional
       preallocated blocks due to minimum allocation guidelines */
    dword allocated_size_hi;
    dword allocated_size_lo;
    /* current file pointer */
    dword file_pointer_hi;
    dword file_pointer_lo;
    /* The file's cluster chain */
    REGION_FRAGMENT *pfirst_fragment[1];
    ERTFS_STAT stat_struct; /* Traditional stat structure */
} ERTFS_EFILIO_STAT;


/* From prapicsk.c */
#define CFREAD_POINTER		1 /* Arguments to circular file seek */
#define CFWRITE_POINTER		2
#define DEFAULT_SEGMENT_SIZE 0x80000000 /* 2 gigabyte */



#if (INCLUDE_DEBUG_LEAK_CHECKING)
struct mem_report {
        int  lost_count;            /* Sum of all lost fileds, if zero, none lost, otherwise look at individual fields */
        int  nusers_configured;
        int  nusers_free;

        int  nfiles_configured;
        int  nfiles_free;

        int  ndrives_configured;
        int  ndrives_free;
        int  ndrives_mapped;
        int  ndrives_lost;

        int  ndrobjs_configured;
        int  ndrobjs_free;
        int  ndrobjs_in_files;
        int  ndrobjs_in_users;
        int  ndrobjs_lost;

        int  nfinodes_configured;
        int  nfinodes_free;
        int  nfinodes_in_drobj;
        int  nfinodes_in_pool;
        int  nfinodes_reference_errors;
        int  nfinodes_lost;

        int  nfinodeex_configured;
        int  nfinodeex_free;
        int  nfinodeex_lost;

        int  nglblkbuff_configured;
        int  nglblkbuff_free;
        int  nglblkbuff_in_filebuff;
        int  nglblkbuff_in_dirbuff;
        int  nglblkbuff_in_scratchbuff;
        int  nglblkbuff_lost;

        int  nrgnbuff_configured;
        int  nrgnbuff_free;
        int  nrgnbuff_in_files;
        int  nrgnbuff_in_freemap;
        int  nrgnbuff_in_failsafe;
        int  nrgnbuff_lost;

        int  nfatbuff_free;
        int  nfatbuff_committed;
        int  nfatbuff_uncommitted;
        int  nfatbuff_lost;
};
void pc_leak_test(struct mem_report * preport);
#endif  /* INCLUDE_DEBUG_LEAK_CHECKING */


#endif      /* ___RTFSTYPES___ */
