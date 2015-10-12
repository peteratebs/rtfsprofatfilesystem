/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2008
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* DRDYNAMIC.C - Dynamic device driver*/
/*****************************************************************************
*Filename: RTFS to block dev interface
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
* int pc_rtfs_media_insert(struct rtfs_media_insert_args *pmedia_parms)
* void pc_rtfs_media_alert(void *devicehandle, int alertcode, void *vargs)
*
* BOOLEAN pc_rtfs_media_remount(void *devicehandle)
* void pc_rtfs_regen_insert_parms(struct rtfs_media_insert_args *prtfs_insert_parms, RTFS_DEVI_MEDIA_PARMS *pmedia_info)
* BOOLEAN RTFS_DEVI_process_mount_parms(DDRIVE *pdr)
* void  RTFS_DEVI_restore_mount_parms(DDRIVE *pdr)
* BLKBUFF *RTFS_DEVI_alloc_filebuffer(DDRIVE *pdr)
* void RTFS_DEVI_release_filebuffer(BLKBUFF *pblk)
* void pc_free_disk_configuration(int drive_number)
*
*
****************************************************************************/



#include "rtfs.h"

static RTFS_DEVI_MEDIA_PARMS *pc_find_mediaparms_structure(void *devicehandle);
static void RTFS_DEVI_device_dismount(RTFS_DEVI_MEDIA_PARMS *pmedia_info);
static int RTFS_DEVI_device_mount(RTFS_DEVI_MEDIA_PARMS *prtfs_media_parms,int driveid, int max_partitions);
static void _release_excess_drives(int driveid, int max_drives, int drives_used);
static void _init_media_info(DDRIVE *pdr, RTFS_DEVI_MEDIA_PARMS *prtfs_media_parms);
static BOOLEAN RTFS_DEVI_retrieve_mount_parms(DDRIVE *pdr);
static int RTFS_DEVI_reserve_driveids(int drive_id, int use_fixed_id, int ndrives);
static void pc_reset_default_drive_id(int driveid, BOOLEAN clear);
static int pc_rtfs_allocate_device_buffers(struct rtfs_media_resource_reply *pmedia_config_block);
static void pc_rtfs_release_ddrive_memory(DDRIVE *pdr);
#if (INCLUDE_V_1_0_DEVICES == 1)
BOOLEAN v1_0_release_reserved_drive(DDRIVE *pdrive);
BOOLEAN v1_0_check_reserved_drives(DDRIVE **pdrive_stuctures,int drive_id, int ndrives);
#endif

void pc_rtfs_free(void *p);

#define RTFS_DEVI_ST_RESERVEID_FAIL      -1
#define RTFS_DEVI_ST_RESERVESTRUCT_FAIL  -2
#define RTFS_DEVI_ST_RESOURCE_FAIL       -3
#define RTFS_DEVI_ST_IO_FAIL             -3
#define RTFS_DEVI_ST_SECTORALLOC_FAIL    -4
#define RTFS_DEVI_ST_INVAL               -5


/* int pc_rtfs_media_insert(struct rtfs_media_insert_args *pmedia_parms)

   Rtfs device mount entry point. This function is to be called by the blk_dev driver when a new media device
   is activated.

   All fields in the struct rtfs_media_insert_args *pmedia_parms structure must be initialized before this function is called.

   It instructs Rtfs to bind the assigned drive numbers to partitions on this device.

   Returns:

	 0 == success
	-1 == Media layer passed invalid arguments
	-2 == Already inserted
	-3 == Rtfs out of media structures
	-4 == Unsupported device type
	-5 == Adaptation layer out of resources
	-6 == Adaptation layer returned invalid values
	-7 == Driveid already in use or no drive ids availabl
	-8 == Rtfs out of drive semaphores
	-9 == IO error on first access to device
    -10== Error allocating buffers to access device
*/

int pc_rtfs_media_insert(struct rtfs_media_insert_args *pmedia_parms)
{
RTFS_DEVI_MEDIA_PARMS *pmedia_info;
int ret_val, ndrives_allocated, first_drive_allocated;
struct rtfs_media_resource_reply media_config_block;

	rtfs_memset(&media_config_block, 0, sizeof(media_config_block));
	ret_val = ndrives_allocated =  first_drive_allocated = 0;

	/* Check arguments */
	if (!pmedia_parms->devhandle ||
        !pmedia_parms->device_type ||
        !pmedia_parms->device_io ||
        !pmedia_parms->device_ioctl ||
        !pmedia_parms->device_configure_media ||
        !pmedia_parms->device_configure_volume ||
        !pmedia_parms->media_size_sectors ||
        !pmedia_parms->numheads ||
        !pmedia_parms->numcyl ||
        !pmedia_parms->secptrk ||
        !pmedia_parms->sector_size_bytes || (pmedia_parms->eraseblock_size_sectors && pmedia_parms->device_erase==0) )
			return(-1);
	/* Check if handle is already mappped to a media parms structure */
	pmedia_info = pc_find_mediaparms_structure(pmedia_parms->devhandle);
	if (pmedia_info)
		return(-2);	/* Already inserted */

	/* Find a free media parms structure */
	pmedia_info = pc_find_mediaparms_structure((void *)0);
	if (!pmedia_info)
	    return(-3); /* Out of media structures */
	/* Initiailize media information */
	{
		dword ltemp;
		ltemp = pmedia_info->access_semaphore;
		rtfs_memset(pmedia_info, 0, sizeof(*pmedia_info));
		pmedia_info->access_semaphore = ltemp;
	}

    pmedia_info->devhandle                = pmedia_parms->devhandle;
    pmedia_info->media_size_sectors       = pmedia_parms->media_size_sectors;
    pmedia_info->numheads                 = pmedia_parms->numheads;
    pmedia_info->numcyl                   = pmedia_parms->numcyl;
    pmedia_info->secptrk                  = pmedia_parms->secptrk;
    pmedia_info->sector_size_bytes        = pmedia_parms->sector_size_bytes;
    pmedia_info->eraseblock_size_sectors  = pmedia_parms->eraseblock_size_sectors;
    pmedia_info->is_write_protect         = pmedia_parms->write_protect;
    pmedia_info->unit_number              = pmedia_parms->unit_number;
    pmedia_info->device_type              = pmedia_parms->device_type;
    pmedia_info->device_io                = pmedia_parms->device_io;
	if (pmedia_parms->eraseblock_size_sectors == 0 )
	    pmedia_info->device_erase             = 0;
	else
	    pmedia_info->device_erase         = pmedia_parms->device_erase;
    pmedia_info->device_ioctl             = pmedia_parms->device_ioctl;
    pmedia_info->device_configure_media   = pmedia_parms->device_configure_media;
    pmedia_info->device_configure_volume  = pmedia_parms->device_configure_volume;

	/* Call rtfs_devcfg_configure_device() to get device wide parameters
	    Note: rtfs_devcfg_configure_device() returns
		0  if successful
		-1 if unsupported device type
		-2 if out of resources
	*/
	{
		int devcfgret_val, sector_buffer_required;

		if (prtfs_cfg->rtfs_exclusive_semaphore)
			sector_buffer_required = 0;
		else
			sector_buffer_required = 1;
		devcfgret_val = pmedia_info->device_configure_media(pmedia_parms, &media_config_block, sector_buffer_required);
		if (media_config_block.use_dynamic_allocation)
			devcfgret_val = pc_rtfs_allocate_device_buffers(&media_config_block);
		if (devcfgret_val == -1) /* unsupported device type */
		{
	    	ret_val = -4; /* Unsupported device type */
			goto error_exit;
		}
		if (devcfgret_val == -2) /* out of resources */
		{
	    	ret_val = -5; /* Out of resources */
			goto error_exit;
		}
		if (media_config_block.requested_max_partitions < 1 ||
		 media_config_block.requested_driveid < 0 ||
		  media_config_block.requested_driveid > (int) ('Z'-'A') )
		{
	    	ret_val = -6; /* == Adaptation layer returned invalid values */
			goto error_exit;
		}
	}
	/* Update Rtfs internal configuration structure from returned parameters */
    /* June 2013 - bug fix - If use_dynamic_allocation is false set device_sector_buffer_base is to zero so it will not be freed when the device is removed */
    if (media_config_block.use_dynamic_allocation)
	    pmedia_info->device_sector_buffer_base = media_config_block.device_sector_buffer_base;
    else
        pmedia_info->device_sector_buffer_base = 0;
    pmedia_info->device_sector_buffer = media_config_block.device_sector_buffer_data;
	pmedia_info->device_sector_buffer_size = media_config_block.device_sector_buffer_size_bytes;

    {
         int ndrives;
         int first_drive=0;

         /* Allocate drive ids and reserve drive structures from Rtfs */
         for (ndrives = media_config_block.requested_max_partitions; ndrives > 0; ndrives--)
         {
             if ((first_drive = RTFS_DEVI_reserve_driveids(media_config_block.requested_driveid, (BOOLEAN) media_config_block.use_fixed_drive_id, ndrives)) >= 0)
                 break;
			 if (first_drive == RTFS_DEVI_ST_RESOURCE_FAIL)
			 {
	    		ret_val = -8; /* Rtfs out of drive semaphores */
	    		goto error_exit;
			 }
			 else if (media_config_block.use_fixed_drive_id)
			 {
	    		ret_val = -7;  /* Driveid already in use */
	    		goto error_exit;
			 }
         }
         ndrives_allocated = ndrives;
         first_drive_allocated = first_drive;
		 if (!ndrives_allocated)
		 {
		 	ret_val = -7;        /* == Driveid already in use or no drive ids available */
	    	goto error_exit;
		 }
	}

	/* Now read the device and mount volumes */
	ret_val = RTFS_DEVI_device_mount(pmedia_info, first_drive_allocated, ndrives_allocated);
	if (ret_val < 0)
	{
	    goto error_exit;
	}
	/* If get here we are okay */
	return(0);
error_exit:
	if (media_config_block.use_dynamic_allocation && media_config_block.device_sector_buffer_base)
		pc_rtfs_free(media_config_block.device_sector_buffer_base);
	if (ndrives_allocated)
		_release_excess_drives(first_drive_allocated, ndrives_allocated, 0 /* drives_used */);
   	pmedia_info->devhandle          = 0;
	return(ret_val);
}

/*  RTFS_DEVI_device_mount(RTFS_DEVI_MEDIA_PARMS *prtfs_media_parms,int driveid, int max_partitions)

   Rtfs device mount entry point. This function is to be called by the blk_dev driver when a new media device
   is activated.

   All fields in the RTFS_DEVI_MEDIA_PARMS structure must be initialized before this function is called.

   It instructs Rtfs to bind the assigned drive numbers to partitions on this device.

   Returns:
    > 0             - The number of mount partitions configured.

        If the return value is <= 0, one of the following
		-7 == Driveid already in use or no drive ids available
        -9 == IO error on first access to device
        -10== Error allocating buffers to access device


*/

static int RTFS_DEVI_device_mount(RTFS_DEVI_MEDIA_PARMS *prtfs_media_parms,int driveid, int max_partitions)
{
int drive_id, max_drives, partition_number, partitions_required, ret_val;
struct mbr_entry_specification mbr_specs[RTFS_MAX_PARTITIONS];
DDRIVE *pdr;

    max_drives = max_partitions;

    drive_id = driveid;
    /* Process partition count arguments, read partition table if required */
    /* The drive media structure is not completely set up so set it up.   This will be repeated later */
    /* Copy information from media parms and initialize function dispatch tables */
	pdr = prtfs_cfg->drno_to_dr_map[drive_id];
    _init_media_info(pdr, prtfs_media_parms);
    pdr->driveno          	= drive_id;

	/* Start by assuming the whole media contains one volume */
    pdr->partition_number 	= 0;
    pdr->dyn_partition_base = 0;
    pdr->dyn_partition_size = prtfs_media_parms->media_size_sectors;
    /* Set flags so media check is not performed  */
    /* pdr->drive_flags = DRIVE_FLAGS_INSERTED;  (this not needed with dynamic insertion method) */

    partitions_required = pc_read_partition_table(pdr, &mbr_specs[0]);

	/* Sometimes windev reports mbr_specs[0].partition_size >= prtfs_media_parms->media_size_sectors. and we can't fix it */
    if (partitions_required == 0 || partitions_required == READ_PARTITION_NO_TABLE )
    {
        /* No partitions. use the whole media for the first drive id */
        partitions_required = 1;
        mbr_specs[0].partition_start = 0;
        mbr_specs[0].partition_size  = prtfs_media_parms->media_size_sectors;
    }
    if (partitions_required < 0)
    {
    	if (partitions_required == READ_PARTITION_ERR)
			ret_val  = -10; /* Error allocating buffers to access device */
    	else /* if (partitions_required == READ_PARTITION_IOERROR) */
			ret_val = -9; /* IO error on first access to device */
        goto ex_it;
    }
    /* Only initialize as many drives as the the driver reserved */
    if (partitions_required > max_partitions)
        partitions_required = max_partitions;

    /* Initialize all drive structures */
	rtfs_port_claim_mutex(prtfs_cfg->mountlist_semaphore);
    for (partition_number = 0; partition_number < partitions_required; partition_number++, drive_id++)
    {
        pdr = prtfs_cfg->drno_to_dr_map[drive_id];
        if (!pdr)
        {
        	rtfs_port_release_mutex(prtfs_cfg->mountlist_semaphore);
            ret_val = 	-7; /* Driveid already in use or no drive ids availabl  */
            goto ex_it;
        }
        /* Copy information from mount parms and initialize function dispatch tables */
        _init_media_info(pdr, prtfs_media_parms);
        pdr->driveno          = drive_id;
        pdr->partition_number = partition_number;
        pdr->dyn_partition_base = mbr_specs[partition_number].partition_start;
        pdr->dyn_partition_size = mbr_specs[partition_number].partition_size;
		pdr->dyn_partition_type = mbr_specs[partition_number].partition_type;
		/* Make sure we populate redundant fields, remove redundant fields */
        pdr->drive_info.partition_base = pdr->dyn_partition_base;
        pdr->drive_info.partition_size = pdr->dyn_partition_size;

        /* Set the flags to valid and to removable so Rtfs checks status when it accesses the drive */
        /* pdr->drive_flags    = DRIVE_FLAGS_PARTITIONED|DRIVE_FLAGS_REMOVABLE; not needed with dynamic installation */
    }
	rtfs_port_release_mutex(prtfs_cfg->mountlist_semaphore);
    /* If we get here we have succeeded */
    ret_val = partitions_required;
ex_it:
	_release_excess_drives(driveid, max_drives, ret_val);
	return(ret_val);
}

/* Okay to call more than once, does not release drives that are not allocated */
static void _release_excess_drives(int driveid, int max_drives, int drives_used)
/* Release drive structures that were not used */
{
int i, drive_to_free, n_to_free;
    if (drives_used <= 0)
    {
        drive_to_free = driveid;
        n_to_free = max_drives;
    }
    else
    {
        drive_to_free = driveid + drives_used;
        n_to_free = max_drives-drives_used;
    }

	rtfs_port_claim_mutex(prtfs_cfg->mountlist_semaphore);
    /* Free unused drive structures and release unused drive ids */
    for (i = 0; i < n_to_free; i++, drive_to_free++)
    {
    DDRIVE *pdr;
        pdr = prtfs_cfg->drno_to_dr_map[drive_to_free];
		if (pdr)
		{
			prtfs_cfg->drno_to_dr_map[drive_to_free] = 0;
        	pc_rtfs_release_ddrive_memory(pdr);
        	pc_reset_default_drive_id(drive_to_free, TRUE); /* clears global default drive map if needed */
		}
    }
	rtfs_port_release_mutex(prtfs_cfg->mountlist_semaphore);
}


/* pc_rtfs_media_alert()

	Called from the device driver when write protect status changes or when a device is ejected.

	RTFS_ALERT_EJECT:
	RTFS_ALERT_WPSET:
	RTFS_ALERT_WPCLEAR:

    If the alert code is RTFS_ALERT_EJECT, all drive identifiers, control structures,semaphores and buffers associated with the
    device are released.

    Note: pc_rtfs_media_alert(RTFS_ALERT_EJECT) must be called from the task level because it claims mutex semaphores.

*/

void pc_rtfs_media_alert(void *devicehandle, int alertcode, void *vargs)
{
RTFS_DEVI_MEDIA_PARMS *pmedia_info;

    RTFS_ARGSUSED_PVOID(vargs);

	pmedia_info = pc_find_mediaparms_structure(devicehandle);
	if (!pmedia_info)
		return;

	switch (alertcode) {
		case RTFS_ALERT_EJECT:
			RTFS_DEVI_device_dismount(pmedia_info);
			break;
		case RTFS_ALERT_WPSET:
			pmedia_info->is_write_protect = 1;
			break;
		case RTFS_ALERT_WPCLEAR:
			pmedia_info->is_write_protect = 0;
			break;
	}
}

/* Simulate a disk insertion. Called by FDISK to force reload of partition tables */
BOOLEAN pc_rtfs_media_remount(void *devicehandle)
{
RTFS_DEVI_MEDIA_PARMS *pmedia_info;
struct rtfs_media_insert_args rtfs_insert_parms;

	pmedia_info = pc_find_mediaparms_structure(devicehandle);
	if (pmedia_info)
	{
		pc_rtfs_regen_insert_parms(&rtfs_insert_parms, pmedia_info);
		pc_rtfs_media_alert(devicehandle, RTFS_ALERT_EJECT, 0);
   		if (pc_rtfs_media_insert(&rtfs_insert_parms) < 0)
			return(FALSE);
	}
	return(TRUE);
}

void pc_rtfs_free_mutex(dword semaphore)
{
	rtfs_port_free_mutex(semaphore);
}

dword pc_rtfs_alloc_mutex(char* name)
{
dword sem;
	sem = rtfs_port_alloc_mutex(name);
	if (!sem)
		rtfs_set_errno(PERESOURCESEMAPHORE, __FILE__, __LINE__);
	return(sem);
}
void *pc_rtfs_malloc(dword alloc_size)
{
void *p;
	p = rtfs_port_malloc(alloc_size);
	if (!p)
		rtfs_set_errno(PERESOURCEHEAP, __FILE__, __LINE__);
	return(p);
}

void pc_rtfs_free(void *p)
{
	if (p)
		rtfs_port_free(p);
}



/* Allocate a buffer and return it. Return IO aligned address in paligned data .
   If no IO alignment is required the two values will be the same */
byte *pc_rtfs_iomalloc(dword alloc_size, void **paligned_data)
{
byte *p;
	*paligned_data = 0;
	p = (byte *) pc_rtfs_malloc(alloc_size + RTFS_CACHE_LINE_SIZE_IN_BYTES);
	if (p)
		*paligned_data = RTFS_CACHE_ALIGN_POINTER(p);
	return(p);
}

/* Regenerate the insert arguments used for simulated inserts when the drive is repartitioned or reset */
void pc_rtfs_regen_insert_parms(struct rtfs_media_insert_args *prtfs_insert_parms, RTFS_DEVI_MEDIA_PARMS *pmedia_info)
{
    prtfs_insert_parms->devhandle = pmedia_info->devhandle;
    prtfs_insert_parms->device_type = pmedia_info->device_type;
    prtfs_insert_parms->unit_number = pmedia_info->unit_number;
    prtfs_insert_parms->write_protect = pmedia_info->is_write_protect;
    prtfs_insert_parms->device_io = pmedia_info->device_io;
    prtfs_insert_parms->device_erase = pmedia_info->device_erase;
    prtfs_insert_parms->device_ioctl = pmedia_info->device_ioctl;
    prtfs_insert_parms->device_configure_media = pmedia_info->device_configure_media;
    prtfs_insert_parms->device_configure_volume = pmedia_info->device_configure_volume;
    prtfs_insert_parms->media_size_sectors = pmedia_info->media_size_sectors;
    prtfs_insert_parms->numheads = pmedia_info->numheads;
    prtfs_insert_parms->numcyl = pmedia_info->numcyl;
    prtfs_insert_parms->secptrk = pmedia_info->secptrk;
    prtfs_insert_parms->sector_size_bytes = pmedia_info->sector_size_bytes;
    prtfs_insert_parms->eraseblock_size_sectors = pmedia_info->eraseblock_size_sectors;
}

static void pc_ertfs_free_dynamic_config(void);

static RTFS_CFG rtfs_cfg_core; /* Static block to point to */
RTFS_CFG * prtfs_cfg;
static RTFS_DEVI_MEDIA_PARMS  * saved_media_info[26];
BOOLEAN rtfs_dynamic_init_configuration(struct rtfs_init_resource_reply *preply)
{
int calculated_dirents;

    prtfs_cfg = 0;
	/* Check input arguments, fail if incorrect */
    if (!preply->max_drives          		||
        !preply->max_scratch_buffers 		||
        !preply->max_files           		||
        !preply->max_user_contexts   		||
        !preply->max_region_buffers  		||
        !preply->spare_drive_directory_objects ||
        !preply->spare_user_directory_objects || (preply->run_single_threaded && !preply->single_thread_buffer_size))
		return(FALSE);


    prtfs_cfg = &rtfs_cfg_core;
    /* Zero the configuration block */
    rtfs_memset(prtfs_cfg, 0, sizeof(*prtfs_cfg));
    rtfs_memset(saved_media_info, 0, sizeof(saved_media_info));

	calculated_dirents = ((preply->max_drives*preply->spare_drive_directory_objects) +
                          (preply->max_user_contexts*preply->spare_user_directory_objects) +
                           preply->max_files);

    /* Set Configuration values */
	prtfs_cfg->dynamically_allocated 	= preply->use_dynamic_allocation;
    prtfs_cfg->cfg_NDRIVES              = preply->max_drives;
    prtfs_cfg->cfg_NBLKBUFFS            = preply->max_scratch_buffers;
    prtfs_cfg->cfg_NUSERFILES           = preply->max_files;
    prtfs_cfg->cfg_NDROBJS              = calculated_dirents;
    prtfs_cfg->cfg_NFINODES             = calculated_dirents;
    prtfs_cfg->cfg_NUM_USERS            = preply->max_user_contexts;
    prtfs_cfg->cfg_NREGIONS             = preply->max_region_buffers;

	if (preply->run_single_threaded)
	{
    	prtfs_cfg->rtfs_exclusive_semaphore = pc_rtfs_alloc_mutex("Rtfs_exclusive");
    	if (!prtfs_cfg->rtfs_exclusive_semaphore)
        	return(FALSE);
	}
    prtfs_cfg->shared_user_buffer_size = preply->single_thread_buffer_size;

#if (INCLUDE_FAILSAFE_RUNTIME)
    prtfs_cfg->shared_restore_transfer_buffer_size = preply->single_thread_fsbuffer_size;
#endif

#if (INCLUDE_RTFS_PROPLUS)      /* ProPlus specific configuration constants */
    prtfs_cfg->cfg_NFINODES_UEX          = preply->max_files;
#endif

    /* Assign or allocate memory */
    if (preply->use_dynamic_allocation==0)
	{
        prtfs_cfg->mem_drive_pool         = (DDRIVE          		*)preply->mem_drive_pool;
        prtfs_cfg->mem_mediaparms_pool    = (RTFS_DEVI_MEDIA_PARMS	*)preply->mem_mediaparms_pool;
        prtfs_cfg->mem_block_pool         = (BLKBUFF         		*)preply->mem_block_pool;
        prtfs_cfg->mem_block_data         = (byte            		*)preply->mem_block_data;
        prtfs_cfg->mem_finode_pool        = (FINODE          		*)preply->mem_finode_pool;
        prtfs_cfg->mem_file_pool          = (PC_FILE         		*)preply->mem_file_pool;
        prtfs_cfg->mem_drobj_pool         = (DROBJ           		*)preply->mem_drobj_pool;
        prtfs_cfg->rtfs_user_table        = (RTFS_SYSTEM_USER 		*)preply->mem_user_pool;
        prtfs_cfg->rtfs_user_cwd_pointers = (void **                 )preply->mem_user_cwd_pool;
        prtfs_cfg->mem_region_pool        = (REGION_FRAGMENT 		*)preply->mem_region_pool;
#if (INCLUDE_RTFS_PROPLUS)      /* ProPlus specific configuration constatnts */
        prtfs_cfg->mem_finode_uex_pool     = (FINODE_EXTENSION_MEMORY *) preply->mem_finodeex_pool;
#endif
#if (INCLUDE_FAILSAFE_RUNTIME)
        prtfs_cfg->shared_restore_transfer_buffer = (byte *) preply->single_thread_fsbuffer;
#endif
        prtfs_cfg->shared_user_buffer = (byte *) preply->single_thread_buffer;
	}
	else
	{	/* Use Malloc to allocated ERTFS data */
    	prtfs_cfg->mem_drive_pool           = (DDRIVE *) pc_rtfs_malloc(prtfs_cfg->cfg_NDRIVES*sizeof(DDRIVE));
    	prtfs_cfg->mem_mediaparms_pool      = (RTFS_DEVI_MEDIA_PARMS *) pc_rtfs_malloc(prtfs_cfg->cfg_NDRIVES*sizeof(RTFS_DEVI_MEDIA_PARMS));
    	prtfs_cfg->mem_block_pool           = (BLKBUFF*) pc_rtfs_malloc(prtfs_cfg->cfg_NBLKBUFFS*sizeof(BLKBUFF));
    	prtfs_cfg->mem_block_data         	= (byte *) pc_rtfs_malloc(prtfs_cfg->cfg_NBLKBUFFS*RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES);
    	prtfs_cfg->mem_file_pool            = (PC_FILE *)pc_rtfs_malloc(prtfs_cfg->cfg_NUSERFILES* sizeof(PC_FILE));
    	prtfs_cfg->mem_drobj_pool           = (DROBJ *)    pc_rtfs_malloc(prtfs_cfg->cfg_NDROBJS* sizeof(DROBJ));
    	prtfs_cfg->mem_finode_pool          = (FINODE *)   pc_rtfs_malloc(prtfs_cfg->cfg_NFINODES* sizeof(FINODE));
    	prtfs_cfg->rtfs_user_table          = (RTFS_SYSTEM_USER *) pc_rtfs_malloc(prtfs_cfg->cfg_NUM_USERS * sizeof(RTFS_SYSTEM_USER));
        prtfs_cfg->rtfs_user_cwd_pointers   = (void **) pc_rtfs_malloc(prtfs_cfg->cfg_NUM_USERS*prtfs_cfg->cfg_NDRIVES*sizeof(void *));


    	prtfs_cfg->mem_region_pool          = (REGION_FRAGMENT *)  pc_rtfs_malloc(prtfs_cfg->cfg_NREGIONS * sizeof(REGION_FRAGMENT));

#if (INCLUDE_RTFS_PROPLUS)      /* ProPlus specific configuration constatnts */
    	prtfs_cfg->mem_finode_uex_pool       = (FINODE_EXTENSION_MEMORY *) pc_rtfs_malloc(prtfs_cfg->cfg_NFINODES_UEX * sizeof(FINODE_EXTENSION_MEMORY));
#endif

    	if (preply->run_single_threaded)
    	{
#if (INCLUDE_FAILSAFE_RUNTIME)
			if (preply->single_thread_fsbuffer_size)
   				prtfs_cfg->shared_restore_transfer_buffer_base = pc_rtfs_iomalloc(preply->single_thread_fsbuffer_size, (void **)&prtfs_cfg->shared_restore_transfer_buffer);
#endif
			if (preply->single_thread_buffer_size)
   				prtfs_cfg->shared_user_buffer_base = pc_rtfs_iomalloc(preply->single_thread_buffer_size, (void **) &prtfs_cfg->shared_user_buffer);
		}
    }
	/* Check results of allocations or verify user passed initialized buffers */
	if (
    !prtfs_cfg->mem_drive_pool        ||
    !prtfs_cfg->mem_mediaparms_pool   ||
    !prtfs_cfg->mem_block_pool        ||
    !prtfs_cfg->mem_block_data        ||
    !prtfs_cfg->mem_finode_pool       ||
    !prtfs_cfg->mem_file_pool         ||
    !prtfs_cfg->mem_drobj_pool        ||
    !prtfs_cfg->rtfs_user_table       ||
    !prtfs_cfg->rtfs_user_cwd_pointers||
#if (INCLUDE_RTFS_PROPLUS)      /* ProPlus specific configuration constatnts */
    !prtfs_cfg->mem_finode_uex_pool   ||
#endif
    !prtfs_cfg->mem_region_pool)
    	goto rtfs_config_error;
   	if (preply->run_single_threaded)
   	{
#if (INCLUDE_FAILSAFE_RUNTIME)
		if (preply->single_thread_fsbuffer_size && !prtfs_cfg->shared_restore_transfer_buffer)
    		goto rtfs_config_error;
#endif
		if (!prtfs_cfg->shared_user_buffer)
    		goto rtfs_config_error;
	}
 	return(TRUE);
rtfs_config_error:
	pc_ertfs_free_dynamic_config();
    return(FALSE);
}


void pc_ertfs_shutdown(void)                             /* __fn__ */
{
    int i;
    DDRIVE *pdr;
	if (!prtfs_cfg)	/* Defensive */
		return;
    /* Tell all device drivers we are sutting down and release core allocated on a device basis */
    for (i = 0; i < 26; i++)
    {
        pdr = prtfs_cfg->drno_to_dr_map[i];
        if (pdr)
		{	/* Release core allocated on a device basis
		       note: ejecting any drive on the same media will clear all others from prtfs_cfg->drno_to_dr_map[i] */
			if (pdr->pmedia_info)
        		pc_rtfs_media_alert(pdr->pmedia_info->devhandle, RTFS_ALERT_EJECT, 0);
		}
    }
    /* Free all core that may have been allocated by pc_rtfs_init() */
    pc_ertfs_free_dynamic_config();

    /* Tell the porting layer to release all signals and semaphores */
    rtfs_port_shutdown();
}


static void pc_ertfs_free_dynamic_config(void)
{
	if (prtfs_cfg)
	{
		RTFS_DEVI_MEDIA_PARMS *pmediaparms;
		int i;
		/* Release semaphores attached to media structures */
		for (pmediaparms=prtfs_cfg->mem_mediaparms_pool,i = 0; i < prtfs_cfg->cfg_NDRIVES; i++, pmediaparms++)
		{
			pc_rtfs_free_mutex(pmediaparms->access_semaphore);
			pmediaparms->access_semaphore = 0;
		}
		/* Release dynamic memory */
		if (prtfs_cfg->dynamically_allocated)
		{
            pc_rtfs_free(prtfs_cfg->mem_drive_pool);
            pc_rtfs_free(prtfs_cfg->mem_mediaparms_pool);
            pc_rtfs_free(prtfs_cfg->mem_block_pool);
            pc_rtfs_free(prtfs_cfg->mem_block_data);
            pc_rtfs_free(prtfs_cfg->mem_file_pool);
            pc_rtfs_free(prtfs_cfg->mem_drobj_pool);
            pc_rtfs_free(prtfs_cfg->mem_finode_pool);
            pc_rtfs_free(prtfs_cfg->rtfs_user_table);
            pc_rtfs_free(prtfs_cfg->rtfs_user_cwd_pointers);
            pc_rtfs_free(prtfs_cfg->mem_region_pool);
            prtfs_cfg->mem_drive_pool=0;
            prtfs_cfg->mem_mediaparms_pool=0;
            prtfs_cfg->mem_block_pool=0;
            prtfs_cfg->mem_block_data=0;
            prtfs_cfg->mem_file_pool=0;
            prtfs_cfg->mem_drobj_pool=0;
            prtfs_cfg->mem_finode_pool=0;
            prtfs_cfg->rtfs_user_table=0;
            prtfs_cfg->rtfs_user_cwd_pointers=0;
            prtfs_cfg->mem_region_pool=0;
#if (INCLUDE_RTFS_PROPLUS)      /* ProPlus specific configuration constatnts */
            pc_rtfs_free(prtfs_cfg->mem_finode_uex_pool);
			prtfs_cfg->mem_finode_uex_pool=0;
#endif
#if (INCLUDE_FAILSAFE_RUNTIME)
            pc_rtfs_free(prtfs_cfg->shared_restore_transfer_buffer_base);
			prtfs_cfg->shared_restore_transfer_buffer_base=0;
#endif
            pc_rtfs_free(prtfs_cfg->shared_user_buffer_base);
			prtfs_cfg->shared_user_buffer_base=0;
		}

		/* Release semaphores attached to media structures */
		pc_rtfs_free_mutex(prtfs_cfg->rtfs_exclusive_semaphore);
		pc_rtfs_free_mutex(prtfs_cfg->mountlist_semaphore);
		pc_rtfs_free_mutex(prtfs_cfg->critical_semaphore);
		prtfs_cfg = 0;  /* Rtfs will not initialize if prtfs_cfg is 0 */
    }
}

/* int pc_rtfs_allocate_device_buffers(struct rtfs_media_resource_reply *pmedia_config_block)

   Dynamically allocate device buffers.

   Helper function for device driver's device_configure_media() function when using dynamic memory allocation to allocate volume buffering


    0  if successful
	-1 if unsupported device type
	-2 if out of resources
*/
static int pc_rtfs_allocate_device_buffers(struct rtfs_media_resource_reply *pmedia_config_block)
{
	if (pmedia_config_block->device_sector_buffer_size_bytes)
	{
		pmedia_config_block->device_sector_buffer_base = pc_rtfs_iomalloc(pmedia_config_block->device_sector_buffer_size_bytes, &pmedia_config_block->device_sector_buffer_data);
		if (!pmedia_config_block->device_sector_buffer_base)
			return(-2);
	}
	return(0);
}

/* int pc_rtfs_allocate_volume_buffers(struct rtfs_volume_resource_request *prequest_block, dword sector_size_bytes)

   Dynamically allocate volume buffers.

   Helper function for device driver's device_configure_volume() function when using dynamic memory allocation to allocate volume buffering

   Returns:
   	0  if succsesful
	-1 if bad arguments where provided
	-2 if allocation failed

	Sets errno to PEDYNAMIC if an error occurs

*/

static int pc_rtfs_allocate_volume_buffers(struct rtfs_volume_resource_reply *pvolume_config_block, dword sector_size_bytes)
{
	int ret_val = 0;
	/* Initialize and check arguments */
	if (!sector_size_bytes || !pvolume_config_block || !pvolume_config_block->n_sector_buffers || !pvolume_config_block->n_fat_buffers || !pvolume_config_block->fat_buffer_page_size_sectors)
		goto bad_args;
    if (pvolume_config_block->n_file_buffers && !pvolume_config_block->file_buffer_size_sectors)
		goto bad_args;

    pvolume_config_block->blkbuff_memory        			= 0;
    pvolume_config_block->filebuff_memory        			= 0;
    pvolume_config_block->fatbuff_memory        			= 0;
    pvolume_config_block->sector_buffer_base        		= 0;
    pvolume_config_block->file_buffer_base        			= 0;
    pvolume_config_block->fat_buffer_base        			= 0;

    pvolume_config_block->blkbuff_memory 				= pc_rtfs_malloc(pvolume_config_block->n_sector_buffers * sizeof(BLKBUFF));
    if (!pvolume_config_block->blkbuff_memory)
		goto cleanup_and_fail;

   	pvolume_config_block->sector_buffer_base 			= pc_rtfs_iomalloc(pvolume_config_block->n_sector_buffers * sector_size_bytes, &pvolume_config_block->sector_buffer_memory);
   	if (!pvolume_config_block->sector_buffer_base)
		goto cleanup_and_fail;

    if (pvolume_config_block->n_file_buffers)
	{
  		pvolume_config_block->filebuff_memory 		   	= pc_rtfs_malloc(pvolume_config_block->n_file_buffers * sizeof(BLKBUFF));
  		if (!pvolume_config_block->filebuff_memory)
  			goto cleanup_and_fail;
   		pvolume_config_block->file_buffer_base	 		= pc_rtfs_iomalloc(pvolume_config_block->n_file_buffers * pvolume_config_block->file_buffer_size_sectors * sector_size_bytes, &pvolume_config_block->file_buffer_memory);
   		if (!pvolume_config_block->file_buffer_base)
			goto cleanup_and_fail;
	}
    pvolume_config_block->fatbuff_memory 				= pc_rtfs_malloc(pvolume_config_block->n_fat_buffers * sizeof(FATBUFF));
    if (!pvolume_config_block->fatbuff_memory)
		goto cleanup_and_fail;
    pvolume_config_block->fat_buffer_base    			= pc_rtfs_iomalloc(pvolume_config_block->n_fat_buffers * pvolume_config_block->fat_buffer_page_size_sectors * sector_size_bytes, &pvolume_config_block->fat_buffer_memory);
    if (!pvolume_config_block->fat_buffer_base)
		goto cleanup_and_fail;

#if (INCLUDE_FAILSAFE_RUNTIME)
    if (prtfs_cfg->pfailsafe && !prtfs_cfg->pfailsafe->fs_allocate_volume_buffers(pvolume_config_block, sector_size_bytes))
    	goto cleanup_and_fail;
#endif

	return(ret_val);

bad_args:
	ret_val = -1;
	goto ex_it;
cleanup_and_fail:
	ret_val = -2;
    pc_rtfs_free(pvolume_config_block->blkbuff_memory);
    pc_rtfs_free(pvolume_config_block->filebuff_memory);
    pc_rtfs_free(pvolume_config_block->fatbuff_memory);
    pc_rtfs_free(pvolume_config_block->sector_buffer_base);
    pc_rtfs_free(pvolume_config_block->file_buffer_base);
    pc_rtfs_free(pvolume_config_block->fat_buffer_base);
ex_it:
	rtfs_set_errno(PEDYNAMIC, __FILE__, __LINE__);
	return(-2);
}

/* All Rtfs IO requests come through this routine. This validates arguments and routes requests to the installed device driver */
BOOLEAN RTFS_DEVI_io(int driveno, dword sector, void  *buffer, word count, BOOLEAN reading) /*__fn__*/
{
DDRIVE *pdr;
dword  dwcount;

    dwcount = (dword) count & 0x0000ffff;

    pdr = pc_drno_to_drive_struct(driveno);
    if (!pdr)
        return(FALSE);
	/* Check for write protect */
    if (pdr->pmedia_info->is_write_protect && !reading)
	{
		rtfs_set_errno(PEDEVICEWRITEPROTECTED, __FILE__, __LINE__);
		return(FALSE);
	}
	if (sector + dwcount > pdr->pmedia_info->media_size_sectors)
	{
		rtfs_set_errno(PEDEVICEADDRESSERROR, __FILE__, __LINE__);
		return(FALSE);
	}
    if (pdr->pmedia_info->device_io(pdr->pmedia_info->devhandle, (void *) pdr, sector, buffer, dwcount, (int) reading))
        return(TRUE);
    else
        return(FALSE);
}



static RTFS_DEVI_MEDIA_PARMS *pc_find_mediaparms_structure(void *devicehandle)
{
RTFS_DEVI_MEDIA_PARMS *pmediaparms;
int i;
	rtfs_port_claim_mutex(prtfs_cfg->mountlist_semaphore);
    for (pmediaparms=prtfs_cfg->mem_mediaparms_pool,i = 0;
        i < prtfs_cfg->cfg_NDRIVES; i++, pmediaparms++)
    {
		if (pmediaparms->devhandle == devicehandle)
		{
			rtfs_port_release_mutex(prtfs_cfg->mountlist_semaphore);
			return(pmediaparms);
		}
    }
	rtfs_port_release_mutex(prtfs_cfg->mountlist_semaphore);
	return(0);
}




/* static void RTFS_DEVI_device_dismount(RTFS_DEVI_MEDIA_PARMS *pmedia_info)

	Called from pc_rtfs_media_alert() when the alert code is RTFS_ALERT_EJECT:

	pc_rtfs_media_alert(RTFS_ALERT_EJECT) is called from the device driver when it detects a removal event.

    This function releases all drive identifiers, control structures,semaphores  and buffers associated with the device is removed.

    Note: pc_rtfs_media_alert(RTFS_ALERT_EJECT) must be called from the task level because it claims mutex semaphores.

*/
static void RTFS_DEVI_device_release_volumes(RTFS_DEVI_MEDIA_PARMS *pmedia_info);

static void RTFS_DEVI_device_dismount(RTFS_DEVI_MEDIA_PARMS *pmedia_info)
{
	/* Mark the device for dismount processing by us and then claim the semaphore
	   DEVICE_DISMOUNT_SCHEDULED is set so if a foreground thread gets the semaphore first it will
	   return error to the API */
	pmedia_info->mount_flags = DEVICE_REMOVE_EVENT;

	/* we could just return from here and have the foreground processes handle it */

	/* Claim the semaphore. If the remove event is not already processed then process it now */
	rtfs_port_claim_mutex(pmedia_info->access_semaphore);

	if (pmedia_info->mount_flags & DEVICE_REMOVE_EVENT)
	{
		RTFS_DEVI_device_release_volumes(pmedia_info); /* devhandle */
	}
	rtfs_port_release_mutex(pmedia_info->access_semaphore);

}
/* Called by device driver initialization routine to register a function to check the device status and report device change events to Rtfs */
void pc_rtfs_register_poll_devices_ready_handler(RTFS_DEVI_POLL_REQUEST_VECTOR *poll_device_vector, void (*poll_device_ready)(void))
{
	poll_device_vector->poll_device_ready=poll_device_ready;
	poll_device_vector->pnext = prtfs_cfg->device_poll_list;
	prtfs_cfg->device_poll_list = poll_device_vector;
}
/* Allow device drivers to poll on-line status and register or deregister themselves if their status changes */
void pc_rtfs_poll_devices_ready(void)
{
#if (INCLUDE_V_1_0_DEVICES == 1)
	v1_0_poll_devices();
#endif
	/* July 2012 - Added code to call register device ready/change handlers check for device change */
	{
	RTFS_DEVI_POLL_REQUEST_VECTOR *poll_device;
		for (poll_device=prtfs_cfg->device_poll_list; poll_device; poll_device=poll_device->pnext)
			poll_device->poll_device_ready();
	}
	rtfs_sys_callback(RTFS_CBS_POLL_DEVICE_READY, 0);
}

DDRIVE *rtfs_claim_media_and_buffers(int driveno)
{
RTFS_DEVI_MEDIA_PARMS  *pmedia_info;
DDRIVE *pdr;

	/* Allow device drivers to poll on-line status and register or deregister themselves if their status changes */
	pc_rtfs_poll_devices_ready();
	/* Get the drive structure from the drive list. */
	rtfs_port_claim_mutex(prtfs_cfg->mountlist_semaphore);
	pdr = prtfs_cfg->drno_to_dr_map[driveno];
	rtfs_port_release_mutex(prtfs_cfg->mountlist_semaphore);
	/* No device in the list means no media present */
	if (!pdr)
	{
		/* Allow device drivers to poll on-line status and register or deregister themselves if their status changes */
		pc_rtfs_poll_devices_ready();
		rtfs_port_claim_mutex(prtfs_cfg->mountlist_semaphore);
		pdr = prtfs_cfg->drno_to_dr_map[driveno];
		rtfs_port_release_mutex(prtfs_cfg->mountlist_semaphore);
	}
	if (!pdr)
	{
		rtfs_set_errno(PEDEVICENOMEDIA, __FILE__, __LINE__);
		return(0);
	}
	pmedia_info = pdr->pmedia_info;

	rtfs_port_claim_mutex(pmedia_info->access_semaphore);

	/* Now that we own the semaphore make sure that media is still present if media was removed and remounted before we got
	   here grab the new drive structure.. make sure drive numbers are still the same (unlikely case that they would change) */
	rtfs_port_claim_mutex(prtfs_cfg->mountlist_semaphore);
	pdr = prtfs_cfg->drno_to_dr_map[driveno];
	rtfs_port_release_mutex(prtfs_cfg->mountlist_semaphore);
	if (!pdr || pdr->driveno != driveno)
	{
		rtfs_port_release_mutex(pmedia_info->access_semaphore);
		rtfs_set_errno(PEDEVICENOMEDIA, __FILE__, __LINE__);
		return(0);
	}
	saved_media_info[driveno] = pdr->pmedia_info; /*- Save the media access semaphore in case we free the drive before release is called. */

#if (0)
	/* Check if the media is present, just in case this is not handled by interrupts */
	if (pdr->pmedia_info->device_ioctl(pdr->pmedia_info->devhandle, (void *) pdr, RTFS_IOCTL_CHECKPRESENT, 0 , 0) < 0)
		pmedia_info->mount_flags |= DEVICE_REMOVE_EVENT;

	/* If the DEVICE_REMOVE_EVENT is set process it and return no media */
	if (pmedia_info->mount_flags & DEVICE_REMOVE_EVENT)
	{
		RTFS_DEVI_device_release_volumes(pmedia_info); /* devhandle */
		goto nomedia_error;
	}
#endif
	/* The media access semaphore is claimed, check if we are in single threaded mode and claim the shared buffer semaphore if so */
    if (prtfs_cfg->rtfs_exclusive_semaphore)
        rtfs_port_claim_mutex(prtfs_cfg->rtfs_exclusive_semaphore);

    /* Set up Failsafe buffers Failsafe will use either shared buffers or drive specific
       buffers but if either the user bufffer or the failsafe buffers are shared all
       access to RTFS is single threaded, controlled by rtfs_exclusive_semaphore*/
#if (INCLUDE_FAILSAFE_RUNTIME)
    if (prtfs_cfg->pfailsafe)
    	prtfs_cfg->pfailsafe->fs_claim_buffers(pdr);
#endif
	return(pdr);
}

void rtfs_release_media_and_buffers(int driveno)
{
DDRIVE *pdr;
RTFS_DEVI_MEDIA_PARMS  *pmedia_info;

	pdr = prtfs_cfg->drno_to_dr_map[driveno];
	pmedia_info = 0;
	if (pdr)
		pmedia_info = pdr->pmedia_info;
	else
	{
		if (saved_media_info[driveno]) // PVO: The drive isn't mapped but check if we freed the device with the media claimed.
		{
			pmedia_info = saved_media_info[driveno];
		}
	}
	saved_media_info[driveno] = 0; /* Make sure the semaphore reference is clear */

	/* Process dismount requests if one was queued for application level servicing by a device driver call or event handler */
	if (pmedia_info && pmedia_info->mount_flags & DEVICE_REMOVE_EVENT)
	{
		RTFS_DEVI_device_release_volumes(pmedia_info); /* devhandle */
	}
	/* Release the shared buffer semaphore if we are running in single threaded mode */
    if (prtfs_cfg->rtfs_exclusive_semaphore)
        rtfs_port_release_mutex(prtfs_cfg->rtfs_exclusive_semaphore);
	/* Release the semaphore for the device */
	rtfs_port_release_mutex(pmedia_info->access_semaphore);
}


static void RTFS_DEVI_device_release_volumes(RTFS_DEVI_MEDIA_PARMS *pmedia_info)
{
int driveno,first_drive,n_drives;

	first_drive = 27;
	n_drives = 0;
	/* Unmount all volumes, releasing their buffers */
    for (driveno = 0; driveno < 26; driveno++)
    {
    DDRIVE *pdr;
        pdr = prtfs_cfg->drno_to_dr_map[driveno];
        if (pdr && pdr->pmedia_info == pmedia_info)
        {
			if (first_drive > driveno)
				first_drive = driveno;
			n_drives += 1;
			pc_dskfree(driveno);    /* Release buffers */
			rtfs_memset(&pdr->du, 0, sizeof(pdr->du));
        }
    }

	/* NULL the drive map and deallocate buffers with the mountlist semaphore claimed */
	rtfs_port_claim_mutex(prtfs_cfg->mountlist_semaphore);
	if (n_drives)
	{
		int i;
		for (driveno=first_drive,i = 0; i <  n_drives; i++,driveno++)
		{
			pc_free_disk_configuration(driveno);
			pc_rtfs_release_ddrive_memory(prtfs_cfg->drno_to_dr_map[driveno]);
        	prtfs_cfg->drno_to_dr_map[driveno] = 0;
        	pc_reset_default_drive_id(driveno, TRUE); /* clears global default drive map if needed */
        }
    }
    /* June 2013 - bug fix - If device_sector_buffer_base is non -zero then it was dynamically allocated and must be freed  */
    if (pmedia_info->device_sector_buffer_base)
    {
        pc_rtfs_free(pmedia_info->device_sector_buffer_base);
    }

	/* Clear remove status and release the media info structure for re-use */
	pmedia_info->mount_flags &= ~DEVICE_REMOVE_EVENT;
	pmedia_info->devhandle = 0;
    rtfs_port_release_mutex(prtfs_cfg->mountlist_semaphore);
}



/* pc_free_disk_configuration() implemented for dynamic drives */
void pc_free_disk_configuration(int drive_number)
{
    DDRIVE *pdr;
    pdr = prtfs_cfg->drno_to_dr_map[drive_number];
	if (pdr && pdr->mount_parms.dynamically_allocated)
	{
        pc_rtfs_free(pdr->mount_parms.blkbuff_memory);
        pc_rtfs_free(pdr->mount_parms.filebuff_memory);
        pc_rtfs_free(pdr->mount_parms.fatbuff_memory);
        pc_rtfs_free(pdr->mount_parms.sector_buffer_base);
        pc_rtfs_free(pdr->mount_parms.file_buffer_base);
        pc_rtfs_free(pdr->mount_parms.fat_buffer_base);
        pdr->mount_parms.blkbuff_memory=0;
        pdr->mount_parms.filebuff_memory=0;
        pdr->mount_parms.fatbuff_memory=0;
        pdr->mount_parms.sector_buffer_base=0;
        pdr->mount_parms.file_buffer_base=0;
        pdr->mount_parms.fat_buffer_base=0;
#if (INCLUDE_FAILSAFE_RUNTIME)
    	if (prtfs_cfg->pfailsafe)
    		prtfs_cfg->pfailsafe->fs_free_disk_configuration(pdr);
#endif
	}
}

static void _init_media_info(DDRIVE *pdr, RTFS_DEVI_MEDIA_PARMS *prtfs_media_parms)
{
    /* Initialize one  drive structure */
    /* Point the drive structure to the media info structure provided by the driver */
    pdr->pmedia_info               = prtfs_media_parms;
}



/*
   int RTFS_DEVI_reserve_driveids(int drive_id, int use_fixed_id, int npartitions);

   Reserve drive ids (drive letters) and allocate volume control structures for npartitons volumes.

   int RTFS_DEVI_reserve_driveids(
   int drive_id         - Preferred start drive (0 to 25)
   int use_fixed_id     - If this value is 1, Rtfs returns failure npartitions drive id are not available starting at
                          drive_id
   int ndrives          - Number of contiguous drive numbers to reserve and drive control structures to allocate

   Returns:

        >= 0 - The first drive id (0 - 26) assigned.

        If the return value is < 0, one of the following

        RTFS_DEVI_ST_RESERVEID_FAIL     - Couldn't allocate drive ids using the method dictated.
        RTFS_DEVI_ST_RESERVESTRUCT_FAIL - Couldn't allocate enough drive structures.
        RTFS_DEVI_ST_RESOURCE_FAIL      - Couldn't allocate an access semaphore for the device.
*/

static void pc_rtfs_release_ddrive_memory(DDRIVE *pdr)
{
#if (INCLUDE_V_1_0_DEVICES == 1)
	if (v1_0_release_reserved_drive(pdr))
		return;
#endif
	pc_memory_ddrive(pdr);

}

static int RTFS_DEVI_reserve_driveids(int drive_id, int use_fixed_id, int ndrives)
{
int i, ret_val, first_drive_free, n_drives_free;
DDRIVE *drive_stuctures[26];

    first_drive_free = 0;
    n_drives_free = 0;
    ret_val = 0;

    for (i = 0; i < ndrives; i++)
        drive_stuctures[i] = 0;
#if (INCLUDE_V_1_0_DEVICES == 1)
	/* See if the requested drive IDs and drive structures reserved at startup by old school sevice drivers */
    if (use_fixed_id && v1_0_check_reserved_drives(&drive_stuctures[0],drive_id, 1))
    {
	   /* if one is reserved they all must be, if not enough are reserved then it is an error */
        drive_stuctures[0] = 0;
    	if (!v1_0_check_reserved_drives(&drive_stuctures[0],drive_id, ndrives))
        {
            ret_val = RTFS_DEVI_ST_RESERVESTRUCT_FAIL;
            goto exit_fail;
        }
	}
	/* else fall through and allocate dynamically */
#endif
	if (!drive_stuctures[0])
	{
    	for (i = 0; i < ndrives; i++)
    	{
        	drive_stuctures[i] = pc_memory_ddrive(0);
        	if (!drive_stuctures[i])
        	{
            	ret_val = RTFS_DEVI_ST_RESERVESTRUCT_FAIL;
            	goto exit_fail;
        	}
        	rtfs_memset(drive_stuctures[i], 0, sizeof(DDRIVE));
    	}
    }

    /* Find contiguous drive  numbers */
	rtfs_port_claim_mutex(prtfs_cfg->mountlist_semaphore);
    for (i = drive_id; i < 26; i++)
    {
    	if (prtfs_cfg->drno_to_dr_map[i])
            n_drives_free = 0;
        else
        {
            if (!n_drives_free)
                first_drive_free = i;
            n_drives_free++;
            if (n_drives_free == ndrives)
                break;
        }
    }

    /* Fail if we didn't get enough drives or use fixed and the first drive id is not available */
    if (n_drives_free != ndrives || (use_fixed_id && first_drive_free != drive_id))
    {
       ret_val = RTFS_DEVI_ST_RESERVEID_FAIL;
       rtfs_port_release_mutex(prtfs_cfg->mountlist_semaphore);
       goto exit_fail;
    }

    ret_val = first_drive_free;
    /* Allocate drive structures */
    for (i = 0, drive_id = first_drive_free; i < n_drives_free; i++, drive_id++)
    {
		drive_stuctures[i]->driveno = drive_id;
        prtfs_cfg->drno_to_dr_map[drive_id] =  drive_stuctures[i];
        pc_reset_default_drive_id(drive_id, FALSE); /* Set the global default drive if we don't have one */
    }
    rtfs_port_release_mutex(prtfs_cfg->mountlist_semaphore);
    return(ret_val);
exit_fail:
    for (i = 0; i < ndrives; i++)
    {
        if (drive_stuctures[i])
        {
            pc_rtfs_release_ddrive_memory(drive_stuctures[i]);
        }
    }
    return(ret_val);
}

static void pc_reset_default_drive_id(int driveid, BOOLEAN clear)
{
int drive_index;
	if (clear)
	{
		if (prtfs_cfg->default_drive_id == driveid+1)
		{
			prtfs_cfg->default_drive_id = 0;

		}
	}
	if (!prtfs_cfg->default_drive_id)
	{
        for (drive_index = 0;drive_index < 26; drive_index++)
        {
            if (prtfs_cfg->drno_to_dr_map[drive_index])
            {
            	prtfs_cfg->default_drive_id = drive_index+1;
                break;
            }
        }
    }
}

/* Call device driver's device_configure_volume() subroutine to retrieve operating and buffering instructions for a volume being mounted */
BOOLEAN RTFS_DEVI_copy_volume_resource_reply(DDRIVE *pdr, struct rtfs_volume_resource_reply *preply, dword sector_size_bytes);

static BOOLEAN RTFS_DEVI_retrieve_mount_parms(DDRIVE *pdr)
{
struct rtfs_volume_resource_request request;
struct rtfs_volume_resource_reply	reply;
int    response;

    rtfs_memset(&request, 0, sizeof(request));
    rtfs_memset(&reply, 0, sizeof(reply));
    rtfs_memset(&pdr->mount_parms, 0, sizeof(pdr->mount_parms));

	/* Set up request block the configuration driver can use to decide on parameters */
    request.devhandle 				= pdr->pmedia_info->devhandle;              /* Device driver access Handle */
    request.device_type 			= pdr->pmedia_info->device_type;		    /* Device type returned by device_configure_media() */
    request.unit_number 			= pdr->pmedia_info->unit_number;			/* Unit number type returned by device_configure() */
    request.driveid 				= pdr->driveno;                         	/* Drive letter (0 - 25) */
    request.partition_number 		= pdr->partition_number;					/* Which partition is it */
    request.volume_size_sectors     = pdr->dyn_partition_size;    				/* Total number of addressable sectors on the partition or media containing the volume */
    request.sector_size_bytes 		= pdr->pmedia_info->sector_size_bytes;      /* Sector size in bytes: 512, 2048, etc */
    request.eraseblock_size_sectors = pdr->pmedia_info->eraseblock_size_sectors;/* Sectors per erase block. Zero for media without erase blocks */

	if (prtfs_cfg->rtfs_exclusive_semaphore != 0)
    	request.buffer_sharing_enabled = 1; 									/* If 1, Rtfs is configured to shared sector buffers and failsafe restore buffers and these buffers are not required */
	else
    	request.buffer_sharing_enabled = 0;

#if (INCLUDE_FAILSAFE_RUNTIME)
    if (prtfs_cfg->pfailsafe)
    	request.failsafe_available		= 1;
	else
    	request.failsafe_available		= 0;
#endif

	response = pdr->pmedia_info->device_configure_volume(&request, &reply);

	if (response == 0 && RTFS_DEVI_copy_volume_resource_reply(pdr, &reply, request.sector_size_bytes))
    	return(TRUE);
	else
	{
		rtfs_set_errno(PEDYNAMIC, __FILE__, __LINE__);
		return(FALSE);
	}
}

/* Perform post processing on the rtfs_volume_resource_reply structure and intitialize the the drive's mount_parms structure
   also performs dynamic resource allocation if needed and failsafe parameter processing.
   This routine is called by RTFS_DEVI_retrieve_mount_parms() after it has retrieved mount parameters from the media driver's device_configure_volume() function.
   Rtfs coverage tests also call this routine directly to override the media driver's mount parameters and assign test specific volume configurations2. */

BOOLEAN RTFS_DEVI_copy_volume_resource_reply(DDRIVE *pdr, struct rtfs_volume_resource_reply *preply, dword sector_size_bytes)
{
    if (preply->drive_operating_policy & ~DRVPOL_ALL_VALID_USER_OPTIONS)
		goto bad_resonse;
	/* Dynamically allocate buffers if required */
	if (preply->use_dynamic_allocation)
		if (pc_rtfs_allocate_volume_buffers(preply, sector_size_bytes) != 0)
			goto bad_resonse;

	/* Validate response and copy to media_parms structure */
	/* Required for all volumes */
	if (!preply->n_sector_buffers ||
        !preply->n_fat_buffers ||
        !preply->fat_buffer_page_size_sectors)
			goto bad_resonse;

	/* If number specified file buffer size be present */
	if (preply->n_file_buffers && !preply->file_buffer_size_sectors)
		goto bad_resonse;

	if (!preply->fatbuff_memory ||
        !preply->blkbuff_memory ||
        !preply->sector_buffer_memory ||
        !preply->fat_buffer_memory)
			goto bad_resonse;

	/* If file buffer size or number specified the buffers and control blocks must be present */
	if (preply->n_file_buffers && (!preply->filebuff_memory || !preply->file_buffer_memory))
       		goto bad_resonse;

	/* Copy response to mount_parms structure */
    pdr->mount_parms.drive_operating_policy = preply->drive_operating_policy;
    pdr->mount_parms.n_sector_buffers = preply->n_sector_buffers;
    pdr->mount_parms.n_fat_buffers = preply->n_fat_buffers;
    pdr->mount_parms.fat_buffer_page_size_sectors = preply->fat_buffer_page_size_sectors;
    pdr->mount_parms.n_file_buffers = preply->n_file_buffers;
    pdr->mount_parms.file_buffer_size_sectors = preply->file_buffer_size_sectors;
    pdr->mount_parms.blkbuff_memory = preply->blkbuff_memory;
    pdr->mount_parms.filebuff_memory = preply->filebuff_memory;
    pdr->mount_parms.fatbuff_memory = preply->fatbuff_memory;
    pdr->mount_parms.sector_buffer_base = preply->sector_buffer_base;
    pdr->mount_parms.file_buffer_base = preply->file_buffer_base;
    pdr->mount_parms.fat_buffer_base = preply->fat_buffer_base;
    pdr->mount_parms.sector_buffer_memory = preply->sector_buffer_memory;
    pdr->mount_parms.file_buffer_memory = preply->file_buffer_memory;
    pdr->mount_parms.fat_buffer_memory = preply->fat_buffer_memory;

	pdr->mount_parms.dynamically_allocated = preply->use_dynamic_allocation;

#if (INCLUDE_FAILSAFE_RUNTIME)
    if (prtfs_cfg->pfailsafe && !prtfs_cfg->pfailsafe->fs_dynamic_configure_volume(pdr, preply))
  		goto bad_resonse;
#endif

    return(TRUE);

bad_resonse:
	return(FALSE);
}


/* Called by Rtfs when a volume is beign opened or reopened, if the mount parameter
   block is not already configure call the device driver to retrieve instructions
   then initializes drive structure from values stored in values the mount parameter block
   that where retrieved from the device driver earlier */
BOOLEAN RTFS_DEVI_process_mount_parms(DDRIVE *pdr)
{
BLKBUFF *pblk;
FATBUFF *pfblk;
byte *pdata;
dword i, ltemp;

	/* if the mount parameter block is not already configure call the device driver to retrieve instructions */
    if (!pdr->mount_parms.n_sector_buffers)	/* Already configured from a previous call ? */
 		if (!RTFS_DEVI_retrieve_mount_parms(pdr))
			return(FALSE);

    /* Attach sector buffers to control structures */
    pblk  = (BLKBUFF *) pdr->mount_parms.blkbuff_memory;
    pdata = (byte *) pdr->mount_parms.sector_buffer_memory;
    if (!pdata)
        return(FALSE);
    for (i = 0; i < pdr->mount_parms.n_sector_buffers; i++, pblk++)
    {
        pblk->data = (byte *) pdata;
        pblk->data_size_bytes = pdr->pmedia_info->sector_size_bytes;
        pdata += pdr->pmedia_info->sector_size_bytes;
    }
    /* And initialize the drive structure's block buffer pool */
    pc_initialize_block_pool(&pdr->_buffctxt, (int) pdr->mount_parms.n_sector_buffers, (BLKBUFF *) pdr->mount_parms.blkbuff_memory, 0, pdr->pmedia_info->sector_size_bytes);


    /* Attach file buffers to control structures */
    if (pdr->mount_parms.n_file_buffers)
    {
        ltemp = pdr->mount_parms.file_buffer_size_sectors * pdr->pmedia_info->sector_size_bytes;
        pblk  = (BLKBUFF *) pdr->mount_parms.filebuff_memory;
        pdata = (byte *) pdr->mount_parms.file_buffer_memory;
        if (!pdata)
            return(FALSE);
        for (i = 0; i < pdr->mount_parms.n_file_buffers; i++, pblk++)
        {
            pblk->data = (byte *) pdata;
            pblk->data_size_bytes = ltemp;
            pdata += ltemp;
            pblk->pnext = pblk+1;
        }
        pblk  = (BLKBUFF *) pdr->mount_parms.filebuff_memory;
        for (i = 0; i < (pdr->mount_parms.n_file_buffers-1); i++, pblk++)
            pblk->pnext = pblk+1;
        pblk->pnext = 0;
        /* And initialize the drive structure's file_buffer_freelist */
        pdr->file_buffer_freelist = (BLKBUFF *) pdr->mount_parms.filebuff_memory;
    }
    else
        pdr->file_buffer_freelist = 0;

    /* Attach fat buffers to control structures */
    /* When the disk is opened pc_free_all_fat_buffers() will set
            pfatbuffcntxt->pfree_buffers = pdrive->mount_parms.fatbuff_memory;
       and call pc_link_buffer_and_freelist()    */
    pfblk  = (FATBUFF *) pdr->mount_parms.fatbuff_memory;
    pdata = (byte *) pdr->mount_parms.fat_buffer_memory;
	if (!pdata)
        return(FALSE);
    ltemp = pdr->mount_parms.fat_buffer_page_size_sectors * pdr->pmedia_info->sector_size_bytes;
    for (i = 0; i < pdr->mount_parms.n_fat_buffers; i++, pfblk++)
    {

        pfblk->fat_data_buff.pdata = (byte *) pdata;
        pdata += ltemp;
    }
    /* ================ */
    /* Assign configuration values to the drive */
    pdr->du.user_num_fat_buffers       = pdr->mount_parms.n_fat_buffers;
    /* For now these are reserved for pc_diskio_configure() */
    pdr->du.fat_buffer_structures      = 0;
    pdr->du.user_fat_buffer_data       = 0;
    pdr->du.fat_buffer_pagesize        = pdr->mount_parms.fat_buffer_page_size_sectors;

    /* Get the operating policy bit mask that was provided */
    pdr->du.drive_operating_policy     = pdr->mount_parms.drive_operating_policy;

#if (INCLUDE_FAILSAFE_RUNTIME)
    if (prtfs_cfg->pfailsafe && !prtfs_cfg->pfailsafe->fs_dynamic_mount_volume_check(pdr))
		return(FALSE);
#endif
    /* ===============  */
    return(TRUE);
}


BLKBUFF *RTFS_DEVI_alloc_filebuffer(DDRIVE *pdr)
{
BLKBUFF *pblk;
    pblk = pdr->file_buffer_freelist;
    if (pblk)
    {
        pdr->file_buffer_freelist = pblk->pnext;
        pblk->pnext = 0;
        pblk->pdrive = pdr;
        pblk->pdrive_owner = pdr;
    }
    else
    {
        pblk = pc_allocate_blk(pdr, &pdr->_buffctxt);
        if (pblk)
            pblk->pdrive_owner = 0; /* use this here */
    }
	return(pblk);
}

void RTFS_DEVI_release_filebuffer(BLKBUFF *pblk)
{
    if (pblk)
    {
        if (pblk->pdrive_owner)
        {
            pblk->pnext = pblk->pdrive_owner->file_buffer_freelist;
            pblk->pdrive_owner->file_buffer_freelist = pblk;
        }
        else
            pc_discard_buf(pblk);
    }

}
