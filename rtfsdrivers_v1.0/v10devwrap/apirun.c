/*****************************************************************************
*Filename: APIRUN.C - Initialize RTFS and run test shells or initialize drives
*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc, 2006
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*
* Description:
*   This file contains contains the top level entry point helper functions
*   pc_ertfs_run(). And for RtfsPro users it also provides a backward compatible
*   version rtfs_app_entry().
*
*   If CALL_TEST_SHELL is set to 1 this routine calls the interactive
*   test shell program, otherwise it returns with Rtfs configured and
*   initialized
*
*
*   The function invokes pc_ertfs_init() which initializes Rtfs and configures
*   the drive structures.
*
*
*   This file, like apiinit.c and apiconfig.c is designed to be edited.
*   pc_ertfs_run() first calls pc_ertfs_init() to perform run time memory
*   configuration and attach device drivers, It then calls the interactive
*   test shell if the compile time contant named CALL_TEST_SHELL
*   (defined in this file) is set to 1.
*
*   This routine performs default initialization for all drive letters with
*   an attached device. (see pc_ertf_init().)
*
*   If CALL_TEST_SHELL is set to 1 this routine calls the interactive
*   test shell program. Otherwise it returns and the application may then access
*   any drives using the default configuration.
*
*************************************************************************/
#include "rtfs.h"
#include "v10wrapper.h"
#define CALL_TEST_SHELL 1

static BOOLEAN initialize_drives(void);
static void _pc_free_all_disk_configurations(void);

DRIVE_CONFIGURE *get_default_disk_configuration(int drive_number);
static DRIVE_CONFIGURE *default_disk_configuration[26];
extern DDRIVE *v1_0_drno_to_dr_map[26]; /* MAPS DRIVE structure to DRIVE: */


void tst_shell(void);

/* RtfsPro rtfs_app_entry() routine, legacy api is the same as aiprun */
int rtfs_app_entry(void)
{
    return(pc_ertfs_run());
}

#if (RTFS_CFG_ALLOC_FROM_HEAP)
void rtfs_port_malloc_stats();
#endif

int pc_ertfs_run(void)                             /* __fn__ */
{
int ret_val;

    ret_val = _pc_ertfs_run();
    if (ret_val)
    {
/* Rtfs is now initialized, if CALL_TEST_SHELL is true start the shell..
   otherwise return with Rtfs initialized */
/* For RtfsProPlus the shell provides it's own drive initialization so call the shell
      and bypass drive initialization */
#if (CALL_TEST_SHELL)
    /* jump into the shell now if requested */
    tst_shell();
#endif
    }
    return(ret_val);
}

int _pc_ertfs_run(void)                             /* __fn__ */
{
    /* Initialize ertfs  */
    if (!pc_ertfs_init())
    {
      RTFS_PRINT_STRING_1((byte *)"pc_ertfs_init failed", PRFLG_NL); /* "pc_ertfs_init failed" */
      return(0);
    }
    rtfs_memset(&default_disk_configuration[0], 0, sizeof(default_disk_configuration));
    if (!pc_v1_0_ertfs_init())
    {
      RTFS_PRINT_STRING_1((byte *)"pc_ertfs_init failed", PRFLG_NL); /* "pc_ertfs_init failed" */
      return(0);
    }
    /* For convenience, configure drives, now */
    if (!initialize_drives())
        return(0);
#if (RTFS_CFG_ALLOC_FROM_HEAP)
    rtfs_port_malloc_stats();
#endif
    return(1);
}

// void pc_ertfs_shutdown(void)                             /* __fn__ */
// {
//     int i;
//     DDRIVE *pdr;
//
//     /* Tell all device drivers we are sutting down */
//     for (i = 0; i < 26; i++)
//     {
//         pdr = prtfs_cfg->drno_to_dr_map[i];
//         if (pdr)
//             pdr->dev_table_perform_device_ioctl(pdr->driveno, DEVCTL_SHUTDOWN, (void *) 0);
//     }
//     /* Tell the porting layer to release all signals and semaphores */
//     rtfs_port_shutdown();
//     /* Free all core that may have been allocated by pc_ertfs_run() */
//     pc_free_all_disk_configurations();
//     /* Free all core that may have been allocated by pc_rtfs_init() */
//     pc_ertfs_free_config();
// }

static BOOLEAN set_default_disk_configuration(int drive_number);

static BOOLEAN initialize_drives(void)
{
int drive_number;
//byte drive_name[8]; /* D: ASCII is 3 bytes, unicode is 6 */
    /* Loop through all possible drive letters. If they are assigned
       set up a default configuration */
    for (drive_number = 0; drive_number < 26; drive_number++)
    {
        /* pc_validate_driveno(drive_number) will fail if the drive letter
           is not assigned */
        if (v1_0_drno_to_dr_map[drive_number])
        {
//            pc_drno_to_drname(drive_number, drive_name, CS_CHARSET_NOT_UNICODE);
            if (!set_default_disk_configuration(drive_number))
                return(FALSE);
			if (v1_0_drno_to_dr_map[drive_number]->partition_number == 0)
			{
            	if (!pc_v1_0_diskio_configure(drive_number, get_default_disk_configuration(drive_number)))
                	return(FALSE);
			}
        }
    }
    return(TRUE);
}

/* The following constants are used to configure the FAT buffering   */

/* The default configuration we want:

    DEFAULT_FAT_BUFFERS_PER_DRIVE    2 FAT buffers per drive
    DEFAULT_FAT_BUFFER_PAGE_SIZE     Each buffer holding 4 blocks

*/

#if (RTFS_CFG_LEAN)
#define DEFAULT_FAT_BUFFERS_PER_DRIVE       1
#define DEFAULT_FAT_BUFFER_PAGE_SIZE        1
#else
#define DEFAULT_FAT_BUFFERS_PER_DRIVE       2
#define DEFAULT_FAT_BUFFER_PAGE_SIZE        4
#endif


/*
 The following constants are used to configure RtfsProPlus disk access
 policies and use buffering. They are compromises to ensure reasonable performance
 without excessive memory consumption.
 The appendix of the RtfsProPlus User's guide includes several discussions on
 optimizing these setting. You should review those discusions.
*/

#define DEFAULT_USER_BUFFER_SIZE        64      /* In blocks == 32 k */

#if (RTFS_CFG_SHARE_BUFFERS || RTFS_CFG_LEAN)
/* If shared buffers are enabled we won't assign a user buffer to any drive. If we want
   we could assign buffers to certain drives and share buffers amongst the others
   if lean is enabled just use scratch buffers */
#undef DEFAULT_USER_BUFFER_SIZE
#define DEFAULT_USER_BUFFER_SIZE         0
#endif

/*
 The following constants are used to configure Failsafe. They are compromises
 to ensure reasonable performance without excessive memory consumption.
 The appenix of The Failsafe User's guide includes several discussions on
 optimizing these setting. You should review those discusiions.

 The setting chosen for DEFAULT_BLOCKMAPSIZE is probably adequate
 but you should review the discussions and use the techniques described to
 validate that this setting is adequate.

 The setting chosen for DEFAULT_RESTORE_BUFFER_SIZE is adequate but if
 you are working with files greater than 100 megabytes or so you will
 see some perfromance imporvements by increasing this value to somewhere
 around 130. You should review the discussions and use the techniques
 described to establish and verify this setting is optimal.


*/
#define DEFAULT_BLOCKMAPSIZE            512     /* 512 block map sttructures, not sector size dependent (12 bytes per structure) */
#if (RTFS_CFG_LEAN)
#define DEFAULT_RESTORE_BUFFER_SIZE     2       /* In blocks == 1 k*/
#else
#define DEFAULT_RESTORE_BUFFER_SIZE     70      /* In blocks == 35 k */
#endif
#if (RTFS_CFG_SHARE_BUFFERS)
/* If shared buffers are enabled we won't assign a user buffer to any drive. If we want
   we could assign buffers to certain drives and share buffers amongst the others */
#undef DEFAULT_RESTORE_BUFFER_SIZE
#define DEFAULT_RESTORE_BUFFER_SIZE         0
#endif

#define DEFAULT_INDEX_BUFFER_SIZE		1

#define DEFAULT_NUM_BLOCK_BUFFERS_PER_DRIVE 10

#if (RTFS_CFG_ALLOC_FROM_HEAP)

#ifdef DEBUG_DISPLAY_MEMORY_USE
/* DEBUG_DISPLAY_MEMORY_USE */
static void display_default_disk_configuration_heap(int drivenumber, DRIVE_CONFIGURE *pdisk_config)
{
dword total_allocated = 0;
    /* DEBUG_DISPLAY_MEMORY_USE */        printf("Memory allocation during init.. for drive number %d \n",drivenumber);
    /* DEBUG_DISPLAY_MEMORY_USE */        printf("DEFAULT_FAT_BUFFERS_PER_DRIVE \n");
    /* DEBUG_DISPLAY_MEMORY_USE */        printf("                     (fat buffers) %6d bytesper %6d total %6d\n",DEFAULT_FAT_BUFFERS_PER_DRIVE , sizeof(struct fatbuff),DEFAULT_FAT_BUFFERS_PER_DRIVE * sizeof(struct fatbuff));
    total_allocated += DEFAULT_FAT_BUFFERS_PER_DRIVE * sizeof(struct fatbuff);
    /* DEBUG_DISPLAY_MEMORY_USE */        printf("                        (fat data) %6d bytesper %6d total %6d\n",pdisk_config->fat_buffer_pagesize*DEFAULT_FAT_BUFFERS_PER_DRIVE,RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES,pdisk_config->fat_buffer_pagesize*DEFAULT_FAT_BUFFERS_PER_DRIVE*RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES);
    total_allocated += (pdisk_config->fat_buffer_pagesize*DEFAULT_FAT_BUFFERS_PER_DRIVE*RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES);
#if (INCLUDE_FAILSAFE_CODE)
    /* DEBUG_DISPLAY_MEMORY_USE */        printf("Failsafe Context %6d bytesper %6d total %6d\n", 1, sizeof(FAILSAFECONTEXT),sizeof(FAILSAFECONTEXT));
    total_allocated += sizeof(FAILSAFECONTEXT);
    /* DEBUG_DISPLAY_MEMORY_USE */        printf("DEFAULT_BLOCKMAPSIZE               %6d bytesper %6d total %6d\n",DEFAULT_BLOCKMAPSIZE, sizeof(FSBLOCKMAP), sizeof(FSBLOCKMAP)*DEFAULT_BLOCKMAPSIZE);
    total_allocated += sizeof(FSBLOCKMAP)*DEFAULT_BLOCKMAPSIZE);
    if (pdisk_config->fs_user_restore_transfer_buffer_size)
    {
    /* DEBUG_DISPLAY_MEMORY_USE */            printf("DEFAULT_RESTORE_BUFFER_SIZE        %6d bytesper %6d total %6d\n", DEFAULT_RESTORE_BUFFER_SIZE, RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES,DEFAULT_RESTORE_BUFFER_SIZE*RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES);
        total_allocated += DEFAULT_RESTORE_BUFFER_SIZE*RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES;
    }
#endif
    if (pdisk_config->user_buffer_size_sectors)
    {
    /* DEBUG_DISPLAY_MEMORY_USE */            printf("DEFAULT_USER_BUFFER_SIZE %6d bytesper     %6d total %6d\n", DEFAULT_USER_BUFFER_SIZE, RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES,DEFAULT_USER_BUFFER_SIZE*RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES);
        total_allocated += DEFAULT_USER_BUFFER_SIZE*RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES;
    }
    /* DEBUG_DISPLAY_MEMORY_USE */    printf("Total memory allocated by pc_rtfs_run for drive %d == %6d\n", drivenumber, total_allocated );
}
#endif /* DEBUG_DISPLAY_MEMORY_USE */

static BOOLEAN set_default_disk_configuration(int drive_number)
{
    DRIVE_CONFIGURE *pdisk_config;

    pdisk_config = (DRIVE_CONFIGURE *) rtfs_port_malloc(sizeof(DRIVE_CONFIGURE));
    if (!pdisk_config)
        return(FALSE);
    rtfs_memset(pdisk_config, 0, sizeof(DRIVE_CONFIGURE));
    pdisk_config->num_fat_buffers           = DEFAULT_FAT_BUFFERS_PER_DRIVE;
    pdisk_config->fat_buffer_pagesize       = DEFAULT_FAT_BUFFER_PAGE_SIZE;

    pdisk_config->fat_buffer_structures  = (struct fatbuff *)
                     rtfs_port_malloc(DEFAULT_FAT_BUFFERS_PER_DRIVE * sizeof(struct fatbuff));
    if (!pdisk_config->fat_buffer_structures)
        return(FALSE);
    pdisk_config->fat_buffer_data = (byte *) rtfs_port_malloc(pdisk_config->fat_buffer_pagesize*DEFAULT_FAT_BUFFERS_PER_DRIVE*RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES);
    if (!pdisk_config->fat_buffer_data)
        return(FALSE);
#if (INCLUDE_FAILSAFE_CODE)
    {
    FAILSAFECONTEXT *fs_context;
    fs_context = (FAILSAFECONTEXT *) rtfs_port_malloc(sizeof(FAILSAFECONTEXT));
    if (!fs_context)
        return(FALSE);
    rtfs_memset(fs_context, 0, sizeof(FAILSAFECONTEXT));
    pdisk_config->user_failsafe_context = (void *) fs_context;

    pdisk_config->fs_blockmap_size = DEFAULT_BLOCKMAPSIZE;
    pdisk_config->fs_blockmap_core = (FSBLOCKMAP *)
                            rtfs_port_malloc(sizeof(FSBLOCKMAP)*DEFAULT_BLOCKMAPSIZE);
    if (!pdisk_config->fs_blockmap_core)
        return(FALSE);
    pdisk_config->fs_user_restore_transfer_buffer_size = DEFAULT_RESTORE_BUFFER_SIZE;
    if (pdisk_config->fs_user_restore_transfer_buffer_size)
    {
        pdisk_config->fs_user_restore_transfer_buffer = (byte *)
                                rtfs_port_malloc(DEFAULT_RESTORE_BUFFER_SIZE*RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES);
        if (!pdisk_config->fs_user_restore_transfer_buffer)
            return(FALSE);
    }
    else
        pdisk_config->fs_user_restore_transfer_buffer = 0;
    }
    pdisk_config->fs_indexbuffer_size =  DEFAULT_INDEX_BUFFER_SIZE;  /* Failsafe index buffer size in sectors must be at least 1 */
    pdisk_config->fs_indexbuffer_core = (byte *) rtfs_port_malloc(DEFAULT_INDEX_BUFFER_SIZE*RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES);
    if (!pdisk_config->fs_indexbuffer_core)
       return(FALSE);
#endif
    pdisk_config->drive_operating_policy    = 0; /* Defaults */

    /* Set user buffer to a default size */
    pdisk_config->user_buffer_size_sectors  = DEFAULT_USER_BUFFER_SIZE;
    if (pdisk_config->user_buffer_size_sectors)
    {
        pdisk_config->user_buffer = (byte *) rtfs_port_malloc(DEFAULT_USER_BUFFER_SIZE*RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES);
        if (!pdisk_config->user_buffer)
            return(FALSE);
    }
    else
        pdisk_config->user_buffer = 0;

   pdisk_config->num_block_buffers           = DEFAULT_NUM_BLOCK_BUFFERS_PER_DRIVE;
   pdisk_config->block_buffer_structures  = (struct blkbuff *)
                     rtfs_port_malloc(DEFAULT_NUM_BLOCK_BUFFERS_PER_DRIVE * sizeof(struct blkbuff));
    if (!pdisk_config->block_buffer_structures)
        return(FALSE);
    pdisk_config->block_buffer_data = (byte *) rtfs_port_malloc(DEFAULT_NUM_BLOCK_BUFFERS_PER_DRIVE*RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES);
    if (!pdisk_config->block_buffer_data)
        return(FALSE);

    default_disk_configuration[drive_number] = pdisk_config;

#ifdef DEBUG_DISPLAY_MEMORY_USE
/* DEBUG_DISPLAY_MEMORY_USE */
    display_default_disk_configuration_heap(drive_number, pdisk_config);
#endif
    return(TRUE);
}
#else   /*  RTFS_CFG_ALLOC_FROM_HEAP    */

/*
When RTFS_CFG_ALLOC_FROM_HEAP is not enabled initialization sequence is complicated by the need to use static arrays.
A set of subroutines are provided for each configuration value and buffer. The routines are passed the drive
number and they return the proper configuration value or bufer.

The default supports 4 drives, A:, B:, C: and Z:. Each drive is configured the same, but the case statements in the
source code may be modified to support a different number of drives, a differnet mix of drive identifiers,or a different mix
of buffers and configurations. For example, different buffer sizes may be assigned to individual drives.

Note: apirun contains statically allocated arrays for 4 drives:
If you have more than four drives you must add additional array declarations.
If you have fewer you should reduce the number of buffers
All buffer sizes are the same for this example but they can be modified to support different sizes for individual drives

*/
static DRIVE_CONFIGURE *_get_config_structure(int drive_number);
static int _get_num_fat_buffers(int drive_number);
static byte *get_fat_buffer_structures(int drive_number);
static byte *_get_fat_buffer_data(int drive_number);
static int _get_user_buffer_size(int drive_number);
static byte*  _get_user_buffer(int drive_number);
static int _get_num_block_buffers(int drive_number);
static byte *get_block_buffer_structures(int drive_number);
static byte *_get_block_buffer_data(int drive_number);

#if (INCLUDE_FAILSAFE_CODE)
static FAILSAFECONTEXT *_get_fs_context(int drive_number);
static int _get_blockmap_size(int drive_number);
static FSBLOCKMAP *_get_blockmap_core(int drive_number);
static int _get_restore_transfer_size(int drive_number);
static byte*  _get_restore_transfer_buffer(int drive_number);
static int _get_index_buffer_size(int drive_number);
static byte*  _get_index_buffer(int drive_number);

#endif  /*  INCLUDE_FAILSAFE_CODE   */

/*
Statically allocated arrays for 4 drives,
If you have more than four drives you must add additional array declarations.
If you have fewer you should reduce the number of buffers
All buffer sizes are the same for this example but they can be modified to support different sizes for indivdual drives

*/

DRIVE_CONFIGURE _disk_config[4]; /* Change 4 if you change NDRIVES */

struct fatbuff _fat_buffers_0[DEFAULT_FAT_BUFFERS_PER_DRIVE];
struct fatbuff _fat_buffers_1[DEFAULT_FAT_BUFFERS_PER_DRIVE];
struct fatbuff _fat_buffers_2[DEFAULT_FAT_BUFFERS_PER_DRIVE];
struct fatbuff _fat_buffers_3[DEFAULT_FAT_BUFFERS_PER_DRIVE];

byte _fat_buffer_data_0[DEFAULT_FAT_BUFFER_PAGE_SIZE*DEFAULT_FAT_BUFFERS_PER_DRIVE*RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES];
byte _fat_buffer_data_1[DEFAULT_FAT_BUFFER_PAGE_SIZE*DEFAULT_FAT_BUFFERS_PER_DRIVE*RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES];
byte _fat_buffer_data_2[DEFAULT_FAT_BUFFER_PAGE_SIZE*DEFAULT_FAT_BUFFERS_PER_DRIVE*RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES];
byte _fat_buffer_data_3[DEFAULT_FAT_BUFFER_PAGE_SIZE*DEFAULT_FAT_BUFFERS_PER_DRIVE*RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES];

#if (DEFAULT_USER_BUFFER_SIZE)
byte _user_buffer_0[DEFAULT_USER_BUFFER_SIZE*RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES];
byte _user_buffer_1[DEFAULT_USER_BUFFER_SIZE*RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES];
byte _user_buffer_2[DEFAULT_USER_BUFFER_SIZE*RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES];
byte _user_buffer_3[DEFAULT_USER_BUFFER_SIZE*RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES];
#endif

#if (INCLUDE_FAILSAFE_CODE)
FAILSAFECONTEXT _fs_context[4]; /* Change 4 if you change NDRIVES */
FSBLOCKMAP _blockmap_core_0[DEFAULT_BLOCKMAPSIZE];
FSBLOCKMAP _blockmap_core_1[DEFAULT_BLOCKMAPSIZE];
FSBLOCKMAP _blockmap_core_2[DEFAULT_BLOCKMAPSIZE];
FSBLOCKMAP _blockmap_core_3[DEFAULT_BLOCKMAPSIZE];

#if (DEFAULT_RESTORE_BUFFER_SIZE)
byte _restore_transfer_buffer_0[DEFAULT_RESTORE_BUFFER_SIZE*RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES];
byte _restore_transfer_buffer_1[DEFAULT_RESTORE_BUFFER_SIZE*RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES];
byte _restore_transfer_buffer_2[DEFAULT_RESTORE_BUFFER_SIZE*RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES];
byte _restore_transfer_buffer_3[DEFAULT_RESTORE_BUFFER_SIZE*RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES];
#endif

byte _index_buffer_0[DEFAULT_INDEX_BUFFER_SIZE*RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES];
byte _index_buffer_1[DEFAULT_INDEX_BUFFER_SIZE*RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES];
byte _index_buffer_2[DEFAULT_INDEX_BUFFER_SIZE*RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES];
byte _index_buffer_3[DEFAULT_INDEX_BUFFER_SIZE*RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES];


#endif  /*  INCLUDE_FAILSAFE_CODE   */

struct blkbuff _block_buffers_0[DEFAULT_NUM_BLOCK_BUFFERS_PER_DRIVE];
struct blkbuff _block_buffers_1[DEFAULT_NUM_BLOCK_BUFFERS_PER_DRIVE];
struct blkbuff _block_buffers_2[DEFAULT_NUM_BLOCK_BUFFERS_PER_DRIVE];
struct blkbuff _block_buffers_3[DEFAULT_NUM_BLOCK_BUFFERS_PER_DRIVE];

byte _block_buffer_data_0[DEFAULT_NUM_BLOCK_BUFFERS_PER_DRIVE*RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES];
byte _block_buffer_data_1[DEFAULT_NUM_BLOCK_BUFFERS_PER_DRIVE*RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES];
byte _block_buffer_data_2[DEFAULT_NUM_BLOCK_BUFFERS_PER_DRIVE*RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES];
byte _block_buffer_data_3[DEFAULT_NUM_BLOCK_BUFFERS_PER_DRIVE*RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES];


static BOOLEAN set_default_disk_configuration(int drive_number)
{
    DRIVE_CONFIGURE *pdisk_config;

    if (default_disk_configuration[drive_number])
        return(TRUE);

    pdisk_config = _get_config_structure(drive_number);
    if (!pdisk_config)
        return(FALSE);
    rtfs_memset(pdisk_config, 0, sizeof(DRIVE_CONFIGURE));
    pdisk_config->num_fat_buffers           = _get_num_fat_buffers(drive_number);
    pdisk_config->fat_buffer_pagesize       = DEFAULT_FAT_BUFFER_PAGE_SIZE;
    pdisk_config->fat_buffer_structures     = (struct fatbuff *)get_fat_buffer_structures(drive_number);
    pdisk_config->fat_buffer_data           = _get_fat_buffer_data(drive_number);
#if (INCLUDE_FAILSAFE_CODE)
    {
        FAILSAFECONTEXT *fs_context;
        fs_context = _get_fs_context(drive_number);
            if (fs_context)
            {
                rtfs_memset(fs_context, 0, sizeof(FAILSAFECONTEXT));
                pdisk_config->user_failsafe_context = (void *) fs_context;
                pdisk_config->fs_blockmap_size = _get_blockmap_size(drive_number);
                pdisk_config->fs_blockmap_core = _get_blockmap_core(drive_number);
                pdisk_config->fs_user_restore_transfer_buffer = _get_restore_transfer_buffer(drive_number);
                pdisk_config->fs_user_restore_transfer_buffer_size = _get_restore_transfer_size(drive_number);
                pdisk_config->fs_indexbuffer_size = _get_index_buffer_size(drive_number); /* Failsafe index buffer size in sectors must be at least 1 */
                pdisk_config->fs_indexbuffer_core = (byte *) _get_index_buffer(drive_number); /* Failsafe index buffer size in sectors must be at least 1 */
            }
    }
#endif  /*  INCLUDE_FAILSAFE_CODE   */
    pdisk_config->drive_operating_policy    = 0; /* Defaults */
    /* Set user buffer to a default size */
    pdisk_config->user_buffer_size_sectors   = _get_user_buffer_size(drive_number);
    pdisk_config->user_buffer = _get_user_buffer(drive_number);

    pdisk_config->num_block_buffers           = _get_num_block_buffers(drive_number);
    pdisk_config->block_buffer_structures  = (struct blkbuff *)  get_block_buffer_structures(drive_number);
    pdisk_config->block_buffer_data = _get_block_buffer_data(drive_number);

    default_disk_configuration[drive_number] = pdisk_config;
    return(TRUE);
}


/* Hardwired to A:, B:, C: Z: - change if you change device mix */
static DRIVE_CONFIGURE *_get_config_structure(int drive_number)
{
    DRIVE_CONFIGURE *pconfig = 0;
    switch(drive_number)
    {
        case    0:
            pconfig = &_disk_config[0];
            break;
        case    1:
            pconfig = &_disk_config[1];
            break;
        case    2:
            pconfig = &_disk_config[2];
            break;
        case    3:
        case    25:
            pconfig = &_disk_config[3];
            break;
    }
    return(pconfig);
}

/* Hardwired to A:, B:, C: Z: - change if you change device mix */
static int _get_num_fat_buffers(int drive_number)
{
    switch(drive_number)
    {
        case    0:
        case    1:
        case    2:
        case    3:
        case    25:
         return(DEFAULT_FAT_BUFFERS_PER_DRIVE);
    }
    return(0);
}
/* Hardwired to A:, B:, C: Z: - change if you change device mix */
static byte *get_fat_buffer_structures(int drive_number)
{
    byte *ret_val = 0;
    switch(drive_number)
    {
        case    0:
            ret_val = (byte *)&_fat_buffers_0[0];
            break;
        case    1:
            ret_val = (byte *)&_fat_buffers_1[0];
            break;
        case    2:
            ret_val = (byte *)&_fat_buffers_2[0];
            break;
        case    3:
        case    25:
            ret_val = (byte *)&_fat_buffers_3[0];
            break;
    }
    return(ret_val);
}
/* Hardwired to A:, B:, C: Z: - change if you change device mix */
static byte *_get_fat_buffer_data(int drive_number)
{
    byte *ret_val = 0;
    switch(drive_number)
    {
        case    0:
            ret_val = &_fat_buffer_data_0[0];
            break;
        case    1:
            ret_val = &_fat_buffer_data_1[0];
            break;
        case    2:
            ret_val = &_fat_buffer_data_2[0];
            break;
        case    3:
        case    25:
            ret_val = &_fat_buffer_data_3[0];
            break;
    }
    return(ret_val);
}
/* Hardwired to A:, B:, C: Z: - change if you change device mix */
static int _get_user_buffer_size(int drive_number)
{
    switch(drive_number)
    {
        case    0:
        case    1:
        case    2:
        case    3:
        case    25:
         return(DEFAULT_USER_BUFFER_SIZE);
    }
    return(0);
}
/* Hardwired to A:, B:, C: Z: - change if you change device mix */
static byte*  _get_user_buffer(int drive_number)
{
    byte *pbuffer = 0;
    switch(drive_number)
    {
#if (DEFAULT_USER_BUFFER_SIZE)
        case    0:
            pbuffer  = &_user_buffer_0[0];
            break;
        case    1:
            pbuffer  = &_user_buffer_1[0];
            break;
        case    2:
            pbuffer  = &_user_buffer_2[0];
            break;
        case    3:
        case    25:
            pbuffer  = &_user_buffer_3[0];
            break;
#endif
        default:
            break;
    }
    return(pbuffer);
}


#if (INCLUDE_FAILSAFE_CODE)
/* Hardwired to A:, B:, C: Z: - change if you change device mix */
static FAILSAFECONTEXT *_get_fs_context(int drive_number)
{
    FAILSAFECONTEXT *pfscontext = 0;
    switch(drive_number)
    {
        case    0:
            pfscontext = &_fs_context[0];
            break;
        case    1:
            pfscontext = &_fs_context[1];
            break;
        case    2:
            pfscontext = &_fs_context[2];
            break;
        case    3:
        case    25:
            pfscontext = &_fs_context[3];
            break;
    }
    return(pfscontext);
}

/* Hardwired to A:, B:, C: Z: - change if you change device mix */
static int _get_blockmap_size(int drive_number)
{
    switch(drive_number)
    {
        case    0:
        case    1:
        case    2:
        case    3:
        case    25:
         return(DEFAULT_BLOCKMAPSIZE);
         break;
    }
    return(0);
}
/* Hardwired to A:, B:, C: Z: - change if you change device mix */
static FSBLOCKMAP *_get_blockmap_core(int drive_number)
{
    FSBLOCKMAP *pblockmap = 0;
    switch(drive_number)
    {
        case    0:
            pblockmap  = &_blockmap_core_0[0];
            break;
        case    1:
            pblockmap  = &_blockmap_core_1[0];
            break;
        case    2:
            pblockmap  = &_blockmap_core_2[0];
            break;
        case    3:
        case    25:
            pblockmap  = &_blockmap_core_3[0];
            break;
    }
    return(pblockmap);
}
/* Hardwired to A:, B:, C: Z: - change if you change device mix */
static int _get_restore_transfer_size(int drive_number)
{
    switch(drive_number)
    {
        case    0:
        case    1:
        case    2:
        case    3:
        case    25:
         return(DEFAULT_RESTORE_BUFFER_SIZE);
         break;
    }
    return(0);
}
/* Hardwired to A:, B:, C: Z: - change if you change device mix */
static byte*  _get_restore_transfer_buffer(int drive_number)
{
    byte *pbuffer = 0;
    switch(drive_number)
    {
#if (DEFAULT_RESTORE_BUFFER_SIZE)
        case    0:
            pbuffer  = &_restore_transfer_buffer_0[0];
            break;
        case    1:
            pbuffer  = &_restore_transfer_buffer_1[0];
            break;
        case    2:
            pbuffer  = &_restore_transfer_buffer_2[0];
            break;
        case    3:
        case    25:
            pbuffer  = &_restore_transfer_buffer_3[0];
            break;
#endif
        default:
            break;
    }
    return(pbuffer);
}

/* Hardwired to A:, B:, C: Z: - change if you change device mix */
static int _get_index_buffer_size(int drive_number)
{
    switch(drive_number)
    {
        case    0:
        case    1:
        case    2:
        case    3:
        case    25:
         return(DEFAULT_INDEX_BUFFER_SIZE);
         break;
    }
    return(0);
}
/* Hardwired to A:, B:, C: Z: - change if you change device mix */
static byte*  _get_index_buffer(int drive_number)
{
    byte *pbuffer = 0;
    switch(drive_number)
    {
        case    0:
            pbuffer  = &_index_buffer_0[0];
            break;
        case    1:
            pbuffer  = &_index_buffer_1[0];
            break;
        case    2:
            pbuffer  = &_index_buffer_2[0];
            break;
        case    3:
        case    25:
            pbuffer  = &_index_buffer_3[0];
            break;
    }
    return(pbuffer);
}

#endif  /*  INCLUDE_FAILSAFE_CODE   */

/* Hardwired to A:, B:, C: Z: - change if you change device mix */
static int _get_num_block_buffers(int drive_number)
{
    switch(drive_number)
    {
        case    0:
        case    1:
        case    2:
        case    3:
        case    25:
         return(DEFAULT_NUM_BLOCK_BUFFERS_PER_DRIVE);
    }
    return(0);
}
/* Hardwired to A:, B:, C: Z: - change if you change device mix */
static byte *get_block_buffer_structures(int drive_number)
{
    byte *ret_val = 0;
    switch(drive_number)
    {
        case    0:
            ret_val = (byte *)&_block_buffers_0[0];
            break;
        case    1:
            ret_val = (byte *)&_block_buffers_1[0];
            break;
        case    2:
            ret_val = (byte *)&_block_buffers_2[0];
            break;
        case    3:
        case    25:
            ret_val = (byte *)&_block_buffers_3[0];
            break;
    }
    return(ret_val);
}
/* Hardwired to A:, B:, C: Z: - change if you change device mix */
static byte *_get_block_buffer_data(int drive_number)
{
    byte *ret_val = 0;
    switch(drive_number)
    {
        case    0:
            ret_val = &_block_buffer_data_0[0];
            break;
        case    1:
            ret_val = &_block_buffer_data_1[0];
            break;
        case    2:
            ret_val = &_block_buffer_data_2[0];
            break;
        case    3:
        case    25:
            ret_val = &_block_buffer_data_3[0];
            break;
    }
    return(ret_val);
}

#endif  /*  RTFS_CFG_ALLOC_FROM_HEAP    */

static void _pc_free_disk_configuration(int drive_number);
void pc_free_all_disk_configurations(void)
{
    int drive_number;
    for (drive_number = 0; drive_number < 26; drive_number++)
        _pc_free_disk_configuration(drive_number);
}

static void _pc_free_disk_configuration(int drive_number)
{
#if (RTFS_CFG_ALLOC_FROM_HEAP)
    DRIVE_CONFIGURE *pdisk_config;

    pdisk_config = default_disk_configuration[drive_number];
    if (pdisk_config)
    {
        rtfs_port_free(pdisk_config->fat_buffer_structures);
        rtfs_port_free(pdisk_config->fat_buffer_data);
#if (INCLUDE_FAILSAFE_CODE)
        {
            FAILSAFECONTEXT *fs_context;
            fs_context = (FAILSAFECONTEXT *) pdisk_config->user_failsafe_context;
            if (fs_context)
            {
                rtfs_port_free(fs_context->blockmap_core);
                if (fs_context->assigned_restore_transfer_buffer)
                    rtfs_port_free(fs_context->assigned_restore_transfer_buffer);
                rtfs_port_free(fs_context);
            }
        }
#endif  /*  INCLUDE_FAILSAFE_CODE   */
        if (pdisk_config->user_buffer)
            rtfs_port_free(pdisk_config->user_buffer);
        rtfs_port_free(pdisk_config);
    }
    default_disk_configuration[drive_number] = 0;
#else   /*  RTFS_CFG_ALLOC_FROM_HEAP    */
    default_disk_configuration[drive_number] = 0;
    RTFS_ARGSUSED_INT((int) drive_number);
#endif  /*  RTFS_CFG_ALLOC_FROM_HEAP    */
}

/* The test shell uses this routine when it restores a drive configuration */
DRIVE_CONFIGURE *get_default_disk_configuration(int drive_number)
{
    return(default_disk_configuration[drive_number]);
}
