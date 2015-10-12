/*****************************************************************************
*Filename: V10WRAPPER.H -
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

#ifndef __V10WRAPPER__
#define __V10WRAPPER__ 1


BOOLEAN pc_v1_0_ertfs_init(void);


typedef struct v1_0_ddrive {
        BOOLEAN is_free;      /* Value of global opencounter when we mounted */
        int     driveno;            /* Driveno. Set when open succeeds */
        dword drive_flags;                  /* Note: the upper byte is reserved	 */

        int     controller_number;
        int     logical_unit_number;
        int     partition_number;
        int     pcmcia_slot_number;
        int     pcmcia_controller_number;
        byte    pcmcia_cfg_opt_value;
        dword   register_file_address;
        int     interrupt_number;      /* note -1 is polled for IDE */
        /* These two routines are attached to device driver specific routines */
        BOOLEAN (*dev_table_drive_io)(int driveno, dword sector, void  *buffer, word count, BOOLEAN readin);
        int (*dev_table_perform_device_ioctl)(int driveno, int opcode, void * arg);
        struct rtfs_volume_resource_reply config;
		struct rtfs_media_resource_reply  media_config;

    } V1_0_DDRIVE;

typedef struct drive_configure {
    dword  num_fat_buffers;
    struct fatbuff *fat_buffer_structures;
    byte   *fat_buffer_data;
    int    fat_buffer_pagesize;
    void   *user_failsafe_context;
    dword  user_buffer_size_sectors;
    byte   *user_buffer;
    dword  drive_operating_policy;
    dword  num_block_buffers;
    struct blkbuff *block_buffer_structures;
    byte   *block_buffer_data;
#if (INCLUDE_FAILSAFE_CODE)
    int fs_blockmap_size;
    FSBLOCKMAP *fs_blockmap_core;
    int fs_user_restore_transfer_buffer_size;
    byte *fs_user_restore_transfer_buffer;
    int fs_indexbuffer_size;
    byte *fs_indexbuffer_core;
#endif
} DRIVE_CONFIGURE;


struct v1_0_n_media_structure {
	struct rtfs_media_resource_reply media_config;
};


void init_v1_0_ddrive_array(void);
BOOLEAN v1_0_assign_media_structures(void);
BOOLEAN pc_v1_0_diskio_configure(int driveid, DRIVE_CONFIGURE *pdisk_config);
int v1_0_call_device_ioctl(DDRIVE *pdr, int command, void *pargs);


#define RTFS_CFG_LEAN 0
#define RTFS_CFG_SHARE_BUFFERS     1 /* If 1, share user buffer and failsafe buffers */
#define RTFS_CFG_ALLOC_FROM_HEAP   1 /* If 1 dynamically allocate system wide resources */
#define RTFS_CFG_NUM_USERS 		   1 /* Minimum 1 see application notes */
#define RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES 512
// Test matrix
// #define RTFS_CFG_SHARE_BUFFERS     1 0  1 0
// #define RTFS_CFG_ALLOC_FROM_HEAP   1 1  0 0
// #define RTFS_CFG_NUM_USERS 		  1 1  1 1

#endif
