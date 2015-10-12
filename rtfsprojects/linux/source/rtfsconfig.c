/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS inc,   2006
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/

#include "rtfs.h"

#if (INCLUDE_V_1_0_CONFIG == 0)

/* Include configuration file, modify rtfsconfig.h to reconfigure Rtfs */
#include "rtfsconfig.h"

#if (RTFS_CFG_INIT_DYNAMIC_ALLOCATION == 0)
static void rtfs_init_structures_and_buffers(struct rtfs_init_resource_reply *preply);
#endif

/*
	void rtfs_init_configuration(struct rtfs_init_resource_reply *preply)

	Called by pc_ertfs_init() to retrieve system wide configuration values.
	If (RTFS_CFG_INIT_DYNAMIC_ALLOCATION == 0) this function also provides system wide structures and buffers
*/

/*======================= Rtfs Static allocation block: Do not modify. ======================================================= */

void rtfs_init_configuration(struct rtfs_init_resource_reply *preply)
{
    preply->max_drives          		= RTFS_CFG_MAX_DRIVES;                     /* The number of drives to support */
    preply->max_scratch_buffers 		= RTFS_CFG_MAX_SCRATCH_BUFFERS;            /* The number of scratch block buffers */
    preply->max_files           		= RTFS_CFG_MAX_FILES;                      /* The maximum number files */
    preply->max_user_contexts   		= RTFS_CFG_MAX_USER_CONTEXTS;              /* The number of user context (seperate current working directory, and errno contexts) */
    preply->max_region_buffers  		= RTFS_CFG_MAX_REGION_BUFFERS;             /* The number of cluster region management objects */
    preply->use_dynamic_allocation		= RTFS_CFG_INIT_DYNAMIC_ALLOCATION;
    preply->run_single_threaded			= RTFS_CFG_SINGLE_THREADED;
    preply->single_thread_buffer_size	= RTFS_CFG_SINGLE_THREADED_USER_BUFFER_SIZE;
    preply->single_thread_fsbuffer_size	= RTFS_CFG_SINGLE_THREADED_FAILSAFE_BUFFER_SIZE;
    preply->spare_drive_directory_objects=RTFS_CFG_DIRS_PER_DRIVE; 					/* Spare directory objects, not user configurable */
    preply->spare_user_directory_objects =RTFS_CFG_DIRS_PER_USER_CONTEXT;  			/* Spare directory objects, not user configurable */

#if (RTFS_CFG_INIT_DYNAMIC_ALLOCATION == 0)
	rtfs_init_structures_and_buffers(preply);
#endif
}


/* ======================= Static allocation block: Do not modify. ======================================================= */

#if (RTFS_CFG_INIT_DYNAMIC_ALLOCATION == 0)

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
#define PRINT_MEM_USAGE 0
#if (PRINT_MEM_USAGE)
printf("%s == %d\n", "__mem_drive_pool[RTFS_CFG_MAX_DRIVES]", sizeof(__mem_drive_pool));
printf("%s == %d\n", "__rtfs_mediaparms[RTFS_CFG_MAX_DRIVES]", sizeof(__rtfs_mediaparms));
printf("%s == %d\n", "__mem_block_pool[RTFS_CFG_MAX_SCRATCH_BUFFERS]", sizeof(__mem_block_pool));
printf("%s == %d\n", "__mem_block_data[RTFS_CFG_MAX_SCRATCH_BUFFERS*RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES]", sizeof(__mem_block_data));
printf("%s == %d\n", "__mem_file_pool[RTFS_CFG_MAX_FILES]", sizeof(__mem_file_pool));
printf("%s == %d\n", "__mem_drobj_pool[RTFS_CALCULATED_DIRS]", sizeof(__mem_drobj_pool));
printf("%s == %d\n", "__mem_finode_pool[RTFS_CALCULATED_DIRS]", sizeof(__mem_finode_pool));
printf("%s == %d\n", "__rtfs_user_table[RTFS_CFG_MAX_USER_CONTEXTS]", sizeof(__rtfs_user_table));
printf("%s == %d\n", "__rtfs_user_cwd_pool[RTFS_CFG_MAX_USER_CONTEXTS*RTFS_CFG_MAX_DRIVES]", sizeof(__rtfs_user_cwd_pool));
printf("%s == %d\n", "__mem_region_pool[RTFS_CFG_MAX_REGION_BUFFERS]", sizeof(__mem_region_pool));
{
dword TotalRam;
TotalRam = sizeof(__mem_drive_pool)+sizeof(__rtfs_mediaparms)+sizeof(__mem_block_pool)+sizeof(__mem_block_data)+sizeof(__mem_file_pool)
+sizeof(__mem_drobj_pool)+sizeof(__mem_finode_pool)+sizeof(__rtfs_user_table)+sizeof(__rtfs_user_cwd_pool)+sizeof(__mem_region_pool);
printf("%s == %d\n", "Total Fixed ram usage: ", TotalRam);
}
#endif
}

#endif

#endif /* #if (INCLUDE_V_1_0_CONFIG == 0) */
