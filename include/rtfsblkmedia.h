/*

        Header files.

        The media interface sits between Rtfs and a block media driver. It is not desirable for the media driver to include
        Rtfs header files or for Rtfs to include media media driver header files, so a single header file, rtfsblkmedia.h, is provided.
        This file is included by both systems and includes all prototype and interface parameter definitions.


                Data types used to interface between Rtfs and the media layer.
                        - As much as possible the native types int and void * are used.
                        - Unsigned 32 bit values are passed using the metatype "dword".

        Note: Use fixed bas, max partitions for driveid allocs..

        Functions provided by The Rtfs to be called by block media drivers:

        Functions provided by Rtfs to be called by block media drivers:

        The following functions are to be called by the media driver to interface with Rtfs. Detailed descriptions of the functions and
        int pc_ertfs_init(void);
Note: init must call config
            - Must be called before the blkmedia interface is used. May be called in the initialization code of the media driver
        void pc_rtfs_shutdown(void);
                - Called by the media layer to request Rtfs to release all memory and operating system resources.
        int pc_rtfs_media_insert(struct rtfs_media_insert_args *pargs);
            - Must be called by the media driver when a fixed device is detected at start up or when a removable device is inserted.
                  the rtfs_media_insert_args structure is described below below.
        void pc_rtfs_media_alert(void *devicehandle, int alertcode, void *vargs);
                - Called by the device driver to alert Rtfs of status changes for the the device.
                        Currently called when:
                        RTFS_ALERT_EJECT   - A removable device is ejected or a fixed device is shut down.
                                        RTFS_ALERT_WPSET   - When write protect status changes to set.
                                        RTFS_ALERT_WPCLEAR - When write protect status changes to clear.
                            - Other uses such as power management handshaking may be added at a later time.

        Functions provided by the block media driver to be called by Rtfs. These functions are not called directly by Rtfs but are called through
        function pointers provided by the media driver in pc_rtfs_media_insert().

        int  (*device_io)   (void  *devhandle, void *pdrive, dword sector, void  *buffer, dword nsectors, int reading);
        int  (*device_erase)(void  *devhandle, dword start_sector, dword nsectors);
        int  (*device_ioctl)(void  *devhandle, void *pdrive, int opcode, int iArgs, void *vargs);

        Detailed interface definitions:

        int pc_rtfs_ertfs_init(void) - Instruct Rtfs to allocate global structures and buffers and to allocate and initialize operating system
        resources.
                        One or more two mutex semaphores, depending on configuration values, are allocated by calling rtfs_port_alloc_mutex()
                If static allocation is  used, (RTFS_CFG_MEM == RTFS_CFG_STATIC), existing structures are simply initialized.
                If dynamic allocation is used, (RTFS_CFG_MEM == RTFS_CFG_DYNAMIC), structures are alocated by calling rtfs_port_malloc() and are
                then initialized.

                        Returns 0 if all resources were succesfully initialized.
                        Returns -1 if resource or memory allocation failed.

        void pc_rtfs_shutdown(void) - May be called by the media layer to request Rtfs to release all memory and operating system resources.
                        Rtfs releases all memory and operating system resources that were allocated by pc_rtfs_init().
                        Rtfs releases all memory and operating system resources that were allocated by any outstanding mounts.
                        Rtfs may be reinitialized by calling pc_rtfs_init().


        int pc_rtfs_media_insert(struct rtfs_media_insert_args *pargs) - Must be called by the media driver when a fixed device is detected at start
        up or when a removable device is inserted.

                  Media driver must provide a properly initialized rtfs_media_insert_args structure.

                        Returns 1 if the device mount succeeded.
                        Returns 0 if rtfs_media_insert_args contains invalid values or if too many devices are mounted for the current configuration to
                        support, if a calll to rtfs_port_alloc_mutex() fails, or, if RTFS_CFG_MEM == RTFS_CFG_DYNAMIC and a calll to rtfs_port_malloc() faails.

                        The following fields must be initialized in the rtfs_media_insert_args structure, prior to passing it to pc_rtfs_media_insert().

            void  *devhandle;        This value may not be zero and it must be unique for each device.
                                                         It is used as a handle when Rtfs calls the device_io(), device_ioctl() and device_erase() functions.
                                                         A convenient pointer or integer indexing scheme should be used calls can be efficiently redirected to the
                                                         appropriate underlying device drivers.
                                                                         The media driver must pass this handle to Rtfs when calling pc_rtfs_media_alert().

                int   device_type;               This field must be initialized with a device type. The device type field is used by code in rtfsdeviceconfig.c
                                         to configure buffering for the device when it is mounted. No other Rtfs modules rely on or know about the
                                         device type filed.

                                                The currently set of device type constants includes BLK_DEVICE_TYPE_MMC, BLK_DEVICE_TYPE_NAND and
                                                BLK_DEVICE_TYPE_USB. To add a new device types you must add a new constant to rtfsblkmedia.h and modify
                                                                                rtfsdeviceconfig.c.

                int   unit_number;               This field contains the instance of the device type that was just installed. For example if two MMC cards are
                                         installed then pc_rtfs_media_insert() is called twice. In both instance the device_type filed contains BLK_DEVICE_TYPE_MMC,
                                                                         but the unit_number filed contains 0 and 1 respectively.

                int   write_protect;     This field contains the initial device write protect status. If the device is write protected it should
                                         be set to one, if not it should be cleared. The media driver may inform Rtfs of a change in write
                                         write protect status status by calling pc_rtfs_media_alert() using the arguments RTFS_ALERT_WPSET,
                                         or RTFS_ALERT_WPCLEAR.


            int  (*device_io)   (..) This field must contain a pointer to a function that Rtfs will use to read and write sectors to the media.
                                                                         The function protoype is:
                                                                                int device_io(void  *devhandle, dword Sector, void  *buffer, dword SectorCount, int reading);
                                                                                        - devhandle will be the value passed in the devhandle field of this structure.
                                                                                        - pdrive is void pointer that may be cast to and Rtfs (DDRIVE *) pointer. Intelligient device drivers may
                                                                                          access Rtfs drive structure fields to modify behavior based on the volume region beign accessed.
                                                                                        - Sector, buffer and SectorCount are the sector number buffer to transfer to or from and number
                                                                                        of sectors, respectively.
                                                                                        - reading is 1 if the request is a read or zero iif the request is a write.

                                                                                The function must return 1 if the request succeeded.
                                                                                The function must return 0 if the request failed.

            int  (*device_erase)(..) This field must contain a pointer to a function that Rtfs will use to erase sectors on the media. The function is
                                                                         only needed for devices with erase blocks, like nand, and it is called only if the eraseblock_size_sectors field
                                                                         is initialized with a non zero value. For media that does not support erase blocks you may set this value to
                                                                         zero.
                                                                         The function protoype is:
                                                                                int  device_erase(void  *devhandle, dword start_sector, dword nsectors);
                                                                                         - devhandle is the value passed in the devhandle field of this structure.
                                                                                        - pdrive is void pointer that may be cast to and Rtfs (DDRIVE *) pointer. Intelligient device drivers may
                                                                                          access Rtfs drive structure fields to modify behavior based on the volume region beign accessed.
                                                                                         - Sector, and SectorCount are the sector range to erase. They are guaranteed to be on erase block
                                                                                           boundaries if the media was partitioned and formatted by Rtfs, but the media layer should verify
                                                                                           that the sectors are erase block bound before erasing. If they are not it should return success.

                                                                                         The function must return 1 if the request succeeded.
                                                                                         The function must return 0 if the request failed.

            int  (*device_ioctl)   (..) This field must contain a pointer to a function that Rtfs will call when special operations are
                                        required.
                                                                         The function protoype is:
                                                                                int  device_ioctl(void  *devhandle, int opcode, int iArgs, void *vargs);
                                                                                        - devhandle will be the value passed in the devhandle field of this structure.
                                                                                        - pdrive is void pointer that may be cast to and Rtfs (DDRIVE *) pointer. Intelligient device drivers may
                                                                                          access Rtfs drive structure fields to modify behavior based on the volume region beign accessed.
                                                                                        - opcode is the request.
                                                                                            Current opcodes are:
                                                                                                   RTFS_IOCTL_FORMAT - Physically format the device. Just return 1 if no format is needed.
                                                                                                                                NAND drivers may erase the media from this call.
                                                                                                   RTFS_IOCTL_FLUSH -  pc_diskflush() was called, flush caches if the driver is caching sectors
                                                                                        - iArgs and vargs are for passing argument from Rtfs to the media driver, they are currently not used.

                                                                                The function must return 1 if the request succeeded.
                                                                                The function must return 0 if the request failed.
            int  (*device_configure)(..)
                                                                         The function protoype is:
                                                                                int  (*device_configure)(struct rtfs_media_insert_args *pmedia_parms,
                                                                                                                                 struct rtfs_media_resource_request *media_config_block, int sector_buffer_required);

            dword media_size_sectors      -  The total number of addressable sectors logical sectors on the media
            dword numheads                -  HCN reprenenation of media_size_sectors, These values are not used by Rtfs but they are written into
            dword numcyl                  -  MBR and BPB records during partitioning and formatting and thus must be valid FAT HCN values.
            dword secptrk                            -  The maximum allowable value for numheads is 255, for secptrk is 63 and for numcy is 1023.

            dword sector_size_bytes       -  The sector size in bytes: 512, 2048, etc. Must be >= 512 and a power of 2
            dword eraseblock_size_sectors -  Sectors per erase block for nand flahs device, 0 for media without erase blocks


*/

#ifndef __RTFSBLKMEDIA__
#define __RTFSBLKMEDIA__

/* To avoid namespace clashes we allow device drivers to build without requiring Rtfs. We use simple types except for 32 bit values.
   If rtfs.h is included use the dword declaration. If not typedef dword here */

#ifndef __RTFSARCH__
#ifdef _TMS320C6X
typedef unsigned int dword;
#else
typedef unsigned long dword;
#endif
#endif


/* rtfs_init_resource_reply, Passed by rtfs to rtfs_init_configuration(). Must be initialized and returned Rtfs. */
struct rtfs_init_resource_reply
{
    int max_drives;                    /* The number of drives to support */
    int max_scratch_buffers;           /* The number of scratch block buffers */
    int max_files;                     /* The maximum number files */
    int max_user_contexts;             /* The number of user context (seperate current working directory, and errno contexts) */
    int max_region_buffers;            /* The number of cluster region management objects */
    int spare_user_directory_objects;  /* Spare directory objects, not user configurable */
    int spare_drive_directory_objects; /* Spare directory objects, not user configurable */

        int use_dynamic_allocation;                /* Set to one to request Rtfs to dynamically allocate system wide resources, otherwise resources are
                                              assigned from static arrays by the function rtfs_init_structures_and_buffers() contained in the
                                              same source file as rtfs_init_configuration() */
        int run_single_threaded;                   /* Set to one to force Rtfs to run single threaded. In single threaded mode all api calls of all drives
                                              use the same semaphore and thus execute sequentially. This eliminates the need for indifual user
                                              buffers and failsafe restore buffers per drive, resulting in reduced memory consumption, with
                                              marginal to no performance degradation in most system */
        /* run_single_threaded is selected these values must be provided, single_thread_buffer_size refers to the size of the "user buffer"
          to share amongst all drives, while single_thread_fsbuffer_size refers to the failsafe shared restor buffer size.
          Note: these values should be large enough to efficiently accomodate the device with the largest buffering requirements.
          For best performance systems with nand flash should set single_thread_buffer_size to at least as large as an erase block
          and single_thread_fsbuffer_size to at least as large an and erase block plus one sector. */

        dword single_thread_buffer_size;
        dword single_thread_fsbuffer_size;      /* Set to zero if not using failsafe */

        /* The following fields are populated with the addresses of free memory blocks large enough to accomodate the configurations
           prescribed in the previous fields.
           If use_dynamic_allocation is enabled then Rtfs calls the internal subroutine rtfs_dynamic_init_configuration() to allocate the memory.
           If use_dynamic_allocation is not enabled then Rtfs calls the subroutine rtfs_init_structures_and_buffers() to assign the pointers from
           statically declared memory. */

        void *single_thread_buffer;
        void *single_thread_fsbuffer;
    void *mem_drive_pool;
        void *mem_mediaparms_pool;
    void *mem_block_pool;
    void *mem_block_data;
    void *mem_file_pool;
    void *mem_finode_pool;
#if (INCLUDE_RTFS_PROPLUS)      /* ProPlus specific configuration constatnts */
    void *mem_finodeex_pool;
#endif
    void *mem_drobj_pool;
    void *mem_region_pool;
    void *mem_user_pool;
        void *mem_user_cwd_pool;
};



/* rtfs_media_resource_reply, Passed by rtfs to rtfs_devcfg_configure_device(). Must be initialized and returned to Rtfs. */
struct rtfs_media_resource_reply
{
        int  use_dynamic_allocation;
        int requested_driveid;
        int requested_max_partitions;
        int use_fixed_drive_id;
        dword device_sector_buffer_size_bytes;
        byte *device_sector_buffer_base;
        void *device_sector_buffer_data;
};


struct rtfs_volume_resource_request
{
    void  *devhandle;           /* Device driver access Handle */
        int   device_type;                      /* Device type returned by device_configure_media() */
        int   unit_number;                      /* Unit number type returned by device_configure_media() */
    int   driveid;              /* Drive letter (0 - 25) */
        int   partition_number;         /* Which partition is it */

    dword volume_size_sectors;    /* Total number of addressable sectors on the partition or media containing the volume */
    dword sector_size_bytes;      /* Sector size in bytes: 512, 2048, etc */
    dword eraseblock_size_sectors;/* Sectors per erase block. Zero for media without erase blocks */

        int   buffer_sharing_enabled; /* If 1, Rtfs is configured to shared sector buffers and failsafe restore buffers and these buffers are not required */
        int   failsafe_available;         /* If 1, failsafe is availabe and operating policy and failsafe buffering may select failsafe */
};

/* rtfs_volume_resource_reply, Passed by rtfs to rtfs_devcfg_configure_volume(). Must be initialized and returned to Rtfs. */
struct rtfs_volume_resource_reply
{
        int   use_dynamic_allocation;
    dword drive_operating_policy;       /* drive operating policy, see Rtfs manual */
    dword n_sector_buffers;             /* Total number of sector sized buffers (used for director buffers and scratch sectors */
    dword n_fat_buffers;                /* Total number of FAT buffers */
    dword fat_buffer_page_size_sectors; /* Number of sectors per FAT buffer */
    dword n_file_buffers;               /* Total number of file buffers */
    dword file_buffer_size_sectors;     /* file buffer size in sectors */

    dword fsrestore_buffer_size_sectors;/* Failsafe restore buffer size in sectors */
    dword fsindex_buffer_size_sectors;  /* Failsafe index buffer size in sectors must be at least 1 */
    dword fsjournal_n_blockmaps;        /* number of Failsafe sector remap records provided.
                                           Determines the number of outstanding remapped sectors permitted */
    /* Memory returned values */
        /* These arrays do not require address alignment */
    void *blkbuff_memory;               /* 1 element must be sizeof(BLKBUFF)(sizeof(BLKBUFF)=40) * (n_sector_buffers) bytes wide */
    void *fatbuff_memory;               /* 1 element must be sizeof(FATBUFF)(sizeof(FATBUFF)=40) * n_fat_buffers) bytes wide */

    void *filebuff_memory;               /* 1 element must be sizeof(BLKBUFF)(sizeof(BLKBUFF)=40) * (n_file_buffers) bytes wide */
    void *fsfailsafe_context_memory;    /* Failsafe context block */
    void *fsjournal_blockmap_memory;    /* 1 element must be (fsjournal_n_blockmaps * sizeof(FSBLOCKMAP)) bytes. sizeof(FSBLOCKMAP) equals 16 */

        /* These arrays do require IO address alignment if that is a system requirement
           If dynamic allocation is being used these pointers contain the addresses that were returned from os_port_malloc()
           If dynamic allocation is not used these pointers contain addresses of properly alligned data blocks */
    byte *sector_buffer_base;         /* Unaligned sector buffer heap, returns from malloc */
    byte *file_buffer_base;           /* Unaligned file buffer heap, returns from malloc */
    byte *fat_buffer_base;            /* Unaligned fat buffer heap, returns from malloc */
    byte *failsafe_buffer_base;
    byte *failsafe_indexbuffer_base;


        /* These pointers contain arrays do require IO address alignment if that is a system requirement */
    void *sector_buffer_memory;         /* n_sector_buffers * sector_size bytes wide */
    void *file_buffer_memory;           /* n_file_buffers  * sector_size bytes wide */
    void *fat_buffer_memory;            /* n_fat_buffers * fat_buffer_page_size_sectors * sector_size bytes wide */
    void *failsafe_buffer_memory;      /* 1 element must be (fsrestore_buffer_size_sectors * sector_size) bytes */
    void *failsafe_indexbuffer_memory;          /* 1 element must be sector_size bytes */

};



/* rtfs_media_insert_args structure, must be initialized prior to passing it to pc_rtfs_media_insert(). */
struct rtfs_media_insert_args
{
    void  *devhandle;                   /* Handle Rtfs will pass to device_io() and other functions. devhandle is opaque to rtfs */
        int   device_type;                              /* Used by blk dev driver layer. device mount sets it, volume mount may use it to configure buffering */
        int   unit_number;                              /* which instance of this device */
        int   write_protect;                            /* initial write protect state of the device. Rtfs will not write if this is non zero */
    int  (*device_io)   (void  *devhandle, void *pdrive, dword sector, void  *buffer, dword count, BOOLEAN reading);
    int  (*device_erase)(void  *devhandle, void *pdrive, dword start_sector, dword nsectors);
    int  (*device_ioctl)(void  *devhandle, void *pdrive, int opcode, int iArgs, void *vargs);
    int  (*device_configure_media) (struct rtfs_media_insert_args *pmedia_parms, struct rtfs_media_resource_reply *media_config_block, int sector_buffer_required);
    int  (*device_configure_volume)(struct rtfs_volume_resource_request *prequest_block, struct rtfs_volume_resource_reply *preply_block);

    dword media_size_sectors;           /* Total number of addressable sectors on the media */
    dword numheads;                     /* cylinder, head, sector representation of the media. */
    dword numcyl;                       /* Note: must be valid FAT HCN values. max cyl = 1023, max heads == 255, max sectors = 63 */
    dword secptrk;
    dword sector_size_bytes;            /* Sector size in bytes: 512, 2048, etc */
    dword eraseblock_size_sectors;      /* Sectors per erase block. Set to zero for media without erase blocks */

};


#ifdef __cplusplus
extern "C" {
#endif
#ifndef __RTFSPROTOS__
int pc_rtfs_ertfs_init(void);
void  pc_ertfs_shutdown(void);
#endif
BOOLEAN RTFS_DEVI_io(int driveno, dword sector, void  *buffer, word count, BOOLEAN reading);
int pc_rtfs_media_insert(struct rtfs_media_insert_args *pmedia_parms);
void pc_rtfs_media_alert(void *devicehandle, int alertcode, void *vargs);
void rtfs_init_configuration(struct rtfs_init_resource_reply *preply);
void pc_rtfs_free_mutex(dword semaphore);
dword pc_rtfs_alloc_mutex(char* name);
void *pc_rtfs_malloc(dword alloc_size);
#ifdef __cplusplus
}
#endif

/* Alert codes that may be passed to Rtfs from the device driver by pc_rtfs_media_alert() */
#define RTFS_ALERT_EJECT                        1
#define RTFS_ALERT_WPSET                        2
#define RTFS_ALERT_WPCLEAR                      3

/* OPCODES codes passed to the device driver from Rtfs by (*device_io)(*device_io) */

#define RTFS_IOCTL_FORMAT               1
#define RTFS_IOCTL_INITCACHE            2
#define RTFS_IOCTL_FLUSHCACHE           3
#define RTFS_IOCTL_SHUTDOWN				4
#endif /* __RTFSBLKMEDIA__ */
/*
 *  @(#) ti.rtfs.config; 1, 0, 0, 0,17; 1-20-2009 17:04:20; /db/vtree/library/trees/rtfs/rtfs-a18x/src/
 */
