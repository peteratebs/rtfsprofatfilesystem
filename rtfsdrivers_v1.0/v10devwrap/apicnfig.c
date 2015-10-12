/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS inc,   2006
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/

#include "rtfs.h"
#include "v10wrapper.h"

#if (INCLUDE_V_1_0_CONFIG == 1)

/* Configuations constants for RTFS. This top set of configuration values
   is common to both RTFSPro and Rtfs ProPlus

   RtfsProPlus each has additional configuration constants. These are provided in
   a conditionally compiled segments later in this file.


*/


#define NDRIVES           8   /* Number of drives */



#if (RTFS_CFG_LEAN)
#define NBLKBUFFS         8
#else
#define NBLKBUFFS         20  /* Number of blocks in the buffer pool.
                                 Uses 532 bytes per block. Impacts
                                 performance during directory traversals
                                 must be at least 4 */
#endif

#if (RTFS_CFG_LEAN)
#define NUSERFILES       2    /* Maximum Number of open Files at a time */
#else
#define NUSERFILES       20   /* Maximum Number of open Files at a time */
#endif


/* Default shared buffers. These are larger buffers that are required to execute
   certain operations. Private copies can be assigned to a drive when it is initialized
   or the buffers may be shared by all drives.

   To share buffers you must enable the constant RTFS_CFG_SHARE_BUFFERS in rtfsconf.h
   and you must configure the following values in apicnfg.c

   DEFAULT_SHARED_USER_BUFFER_SIZE
   DEFAULT_SHARED_RESTORE_BUFFER_SIZE
   DEFAULT_SHARED_USER_BUFFER_SIZE
   DEFAULT_SHARED_RESTORE_BUFFER_SIZE

   Consult the documentation for pc_rtfs_run for a description of these buffers.

   Note: When RTFS_CFG_SHARE_BUFFERS is enabled, drives that are not assigned their own
   private buffers must have exclusive access to the shared buffers. This mutual exclusivity
   is managed by RTFS internally using a mutex semaphore.
*/
#if (RTFS_CFG_LEAN)
#define DEFAULT_SHARED_USER_BUFFER_SIZE        0                      /* Use scratch buffers */
#define DEFAULT_SHARED_RESTORE_BUFFER_SIZE     2                     /* 2 default sector sized bufferes */
#else
#define DEFAULT_SHARED_USER_BUFFER_SIZE        128      /* In blocks == 64 k */
#define DEFAULT_SHARED_RESTORE_BUFFER_SIZE     70      /* In blocks == 35 k */
#endif


/*Number of 12 byte region management objects */
#if (INCLUDE_RTFS_FREEMANAGER)
#define FREE_MANAGER_REGIONS            10000
#else
#define FREE_MANAGER_REGIONS                0 /* The free manager is excluded, don't reserve space for it */
#endif
#if (RTFS_CFG_LEAN)
#define NREGIONS          (FREE_MANAGER_REGIONS+(NUSERFILES*10))
#else
#define NREGIONS          (FREE_MANAGER_REGIONS+500)
#endif


/* Extra directory entries to allocate if certain operations such as
   recursive directory entries are to be performed. (minimum 4) */
//#if (RTFS_CFG_LEAN)
//#define EXTRADIRENTS 4
//#else
//#define EXTRADIRENTS 64
//#endif

/* Directory Object Needs. Conservative configuration is One CWD per user per drive
    + One per file + one per User for directory traversal */
//#define NDROBJS    (EXTRADIRENTS + RTFS_CFG_NUM_USERS*NDRIVES + RTFS_CFG_NUM_USERS + NUSERFILES)

//#define NFAT64FINODES 0
//#if (INCLUDE_RTFS_PROPLUS)      /* ProPlus specific configuration constants */
//#if (INCLUDE_FAT64)
/* Note for FAT64 enabled system we use the very conservative estimation that
   all user files may be 64 bit metafiles, this consumes more resources than
   necessary for many applications and may be reduced if ram memory is a precious
   resource. */
//#undef NFAT64FINODES
//#define NFAT64FINODES (NUSERFILES*MAX_SEGMENTS_64)
//#endif
//#endif

/* For FAT64 create additional finodes for segments */
//#define NFINODES   (NDROBJS + NFAT64FINODES)

/* ProPlus specific configuration constants, not used by Pro */
//#define NFINODES_EX   NFINODES
//#define NFINODES_EX64 NUSERFILES

/* End configuration constants now initialize memory */


#define RTFS_CFG_SINGLE_THREADED                            RTFS_CFG_SHARE_BUFFERS /* If 1, share user buffer and failsafe buffers */
#define RTFS_CFG_MAX_DRIVES                                 NDRIVES /* Maximum number of mounted volumes */
#define RTFS_CFG_MAX_FILES                                  NUSERFILES /* Maximum number of open files */
#define RTFS_CFG_MAX_SCRATCH_BUFFERS                        NBLKBUFFS /* Minimum four see application notes */
#define RTFS_CFG_MAX_SCRATCH_DIRS                           8 /* Minimum one see application notes */
#define RTFS_CFG_MAX_USER_CONTEXTS                          RTFS_CFG_NUM_USERS /* Minimum 1 see application notes */
#define RTFS_CFG_MAX_REGION_BUFFERS                         NREGIONS /* See application notes */
#define RTFS_CFG_SINGLE_THREADED_USER_BUFFER_SIZE           (DEFAULT_SHARED_USER_BUFFER_SIZE*RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES)   /* Only used if SAMPLE_APP_SINGLE_THREADED == 1*/
#define RTFS_CFG_SINGLE_THREADED_FAILSAFE_BUFFER_SIZE       (DEFAULT_SHARED_RESTORE_BUFFER_SIZE*RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES) /* Only used if SAMPLE_APP_SINGLE_THREADED == 1*/
#define RTFS_CFG_DIRS_PER_DRIVE         					 16 /* Extra directory objects required for internal processing */
#define RTFS_CFG_DIRS_PER_USER_CONTEXT   					  4 /* Do not reduce unless application is fixed and can be verified */



/*
	void rtfs_init_configuration(struct rtfs_init_resource_reply *preply)

	Called by pc_ertfs_init() to retrieve system wide configuration values.
	If (RTFS_CFG_INIT_DYNAMIC_ALLOCATION == 0) this function also provides system wide structures and buffers
*/

/*======================= Rtfs Static allocation block: Do not modify. ======================================================= */

static void rtfs_init_structures_and_buffers(struct rtfs_init_resource_reply *preply);
void rtfs_init_configuration(struct rtfs_init_resource_reply *preply)
{
    preply->max_drives          		= RTFS_CFG_MAX_DRIVES;                     /* The number of drives to support */
    preply->max_scratch_buffers 		= RTFS_CFG_MAX_SCRATCH_BUFFERS;            /* The number of scratch block buffers */
    preply->max_files           		= RTFS_CFG_MAX_FILES;                      /* The maximum number files */
    preply->max_user_contexts   		= RTFS_CFG_MAX_USER_CONTEXTS;              /* The number of user context (seperate current working directory, and errno contexts) */
    preply->max_region_buffers  		= RTFS_CFG_MAX_REGION_BUFFERS;             /* The number of cluster region management objects */
    preply->use_dynamic_allocation		= 0;									  /* Always zero. v10 wrapper layer does all allocation */
    preply->run_single_threaded		= RTFS_CFG_SINGLE_THREADED;
    preply->single_thread_buffer_size	= RTFS_CFG_SINGLE_THREADED_USER_BUFFER_SIZE;
    preply->single_thread_fsbuffer_size	= RTFS_CFG_SINGLE_THREADED_FAILSAFE_BUFFER_SIZE;
    preply->spare_drive_directory_objects=RTFS_CFG_DIRS_PER_DRIVE; 					/* Spare directory objects, not user configurable */
    preply->spare_user_directory_objects =RTFS_CFG_DIRS_PER_USER_CONTEXT;  			/* Spare directory objects, not user configurable */

	rtfs_init_structures_and_buffers(preply);
}

/* Required for v10/v11 emulation - See v10glue.c for usage */
struct v1_0_n_media_structure v1_0_n_media_structure_array[NDRIVES];

/* ======================= Static allocation block: Do not modify. ======================================================= */


/* Calculate the number of internal directory entry structures we will need */
#define RTFS_CALCULATED_DIRS ((RTFS_CFG_MAX_DRIVES*RTFS_CFG_DIRS_PER_DRIVE) + \
                              (RTFS_CFG_MAX_USER_CONTEXTS*RTFS_CFG_DIRS_PER_USER_CONTEXT) + \
                              RTFS_CFG_MAX_FILES)

static DDRIVE          			__mem_drive_pool[RTFS_CFG_MAX_DRIVES];
static RTFS_DEVI_MEDIA_PARMS 	__rtfs_mediaparms[RTFS_CFG_MAX_DRIVES];
static BLKBUFF         			__mem_block_pool[RTFS_CFG_MAX_SCRATCH_BUFFERS];
static byte            			__mem_block_data[RTFS_CFG_MAX_SCRATCH_BUFFERS*RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES];
static PC_FILE         			__mem_file_pool[RTFS_CFG_MAX_FILES];
static DROBJ           			__mem_drobj_pool[RTFS_CALCULATED_DIRS];
static FINODE          			__mem_finode_pool[RTFS_CALCULATED_DIRS];
static RTFS_SYSTEM_USER 		__rtfs_user_table[RTFS_CFG_MAX_USER_CONTEXTS];
static void *					__rtfs_user_cwd_pool[RTFS_CFG_MAX_USER_CONTEXTS*RTFS_CFG_MAX_DRIVES];
static REGION_FRAGMENT 			__mem_region_pool[RTFS_CFG_MAX_REGION_BUFFERS];

#if (INCLUDE_RTFS_PROPLUS)      /* ProPlus specific configuration constatnts */
FINODE_EXTENSION_MEMORY 		__mem_finode_uex_pool[RTFS_CFG_MAX_FILES];
#endif

/* Declare single instance of shared buffers if RTFS_CFG_SINGLE_THREADED is configured */
#if (RTFS_CFG_SINGLE_THREADED)
#ifdef _TMS320C6X
#pragma DATA_ALIGN(__mem_shared_user_buffer, RTFS_CACHE_LINE_SIZE_IN_BYTES)
#endif
byte __mem_shared_user_buffer[RTFS_CFG_SINGLE_THREADED_USER_BUFFER_SIZE];
#if (INCLUDE_FAILSAFE_CODE)
#ifdef _TMS320C6X
#pragma DATA_ALIGN(__mem_shared_restore_transfer_buffer, RTFS_CACHE_LINE_SIZE_IN_BYTES)
#endif
byte __mem_shared_restore_transfer_buffer[RTFS_CFG_SINGLE_THREADED_FAILSAFE_BUFFER_SIZE];
#endif
#endif


static void rtfs_init_structures_and_buffers(struct rtfs_init_resource_reply *preply)
{
#if (RTFS_CFG_SINGLE_THREADED)
    preply->single_thread_buffer       	= (void *) &__mem_shared_user_buffer[0];
#if (INCLUDE_FAILSAFE_CODE)
    preply->single_thread_fsbuffer     	= (void *) &__mem_shared_restore_transfer_buffer[0];
#endif
#endif
    preply->mem_drive_pool       		= (void *) &__mem_drive_pool[0];
    preply->mem_mediaparms_pool       	= (void *) &__rtfs_mediaparms[0];
    preply->mem_block_pool       		= (void *) &__mem_block_pool[0];
    preply->mem_block_data       		= (void *) &__mem_block_data[0];
    preply->mem_finode_pool       		= (void *) &__mem_finode_pool[0];
    preply->mem_file_pool       		= (void *) &__mem_file_pool[0];
    preply->mem_drobj_pool       		= (void *) &__mem_drobj_pool[0];
    preply->mem_user_pool       		= (void *) &__rtfs_user_table[0];
	preply->mem_user_cwd_pool			= (void *) &__rtfs_user_cwd_pool[0];

    preply->mem_region_pool       		= (void *) &__mem_region_pool[0];
#if (INCLUDE_RTFS_PROPLUS)      /* ProPlus specific configuration constatnts */
    preply->mem_finodeex_pool      		= (void *) &__mem_finode_uex_pool[0];
#endif
}

#endif /* #if(INCLUDE_V_1_0_CONFIG == 1) */
