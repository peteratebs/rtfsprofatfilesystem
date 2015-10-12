/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* v10glue.c - Media check function, device io wrapper functions and others needed to glue version1.0 and version1.1 */

#include "rtfs.h"
#include "v10wrapper.h"

int v1_0_n_media_structures;
#if (INCLUDE_V_1_0_CONFIG == 0)
/* Required for v10/v11 emulation - If not using a localized configuration we don't know what ndrives is so use 26 */
struct v1_0_n_media_structure v1_0_n_media_structure_array[26];
#else
extern struct v1_0_n_media_structure v1_0_n_media_structure_array[];
#endif
DDRIVE *v1_0_drno_to_dr_map[26]; /* MAPS DRIVE structure to DRIVE: */
DRIVE_CONFIGURE *get_default_disk_configuration(int drive_number);

static int v1_0_device_configure_media(struct rtfs_media_insert_args *pmedia_parms, struct rtfs_media_resource_reply *pmedia_config_block, int sector_buffer_required);
static int v1_0_device_configure_volume(struct rtfs_volume_resource_request *prequest_block, struct rtfs_volume_resource_reply *pvolume_config_block);
static int v1_0_device_ioctl(void  *devhandle, void *pdrive, int opcode, int iArgs, void *vargs);
static int v1_0_device_io(void  *devhandle, void *pdrive, dword sector, void  *buffer, dword count, BOOLEAN reading);

#define V10DEVTYPE 998

static void v1_0_insert_device(int device_number);
static int v1_0_count_partitions(int start_drive_id);
static dword _pc_calculate_chs(dword total_sectors, dword *cylinders, int *heads, int *secptrack);




/* Device Polling -


	In rtfiles1.0 device polling was done by the rtfs devio layer which called the device driver's
	DEVCTL_CHECKSTATUS service call to check if the driver has detected a device insert or remove.

	rtfiles1.1 does not work. The new model requires the device driver to detect media insertion or removal
	and call pc_rtfs_media_alert() when media ejected and to call pc_rtfs_media_insert() when media is installed.

	In rtfiles1.1 all of the configuration information needed for that media are provided as parameters to
	pc_rtfs_media_insert().

	.. v1_0_poll_devices(void) - This is called by rtfsile1.1 every time the API is entered.
	It uses the method provided by rtfiles1.0 device drivers to detect media change evants and
	then calls methods introduced in version 1.1 to process those events.

	.. v1_0_insert_device() - Called by v1_0_poll_devices(void) when a device insert is detected.
	Uses values that were provided by the version1.0 apirun.c configuration method to initialize
	a version 1.1 rtfs_media_insert_args parametr block and calls v1_0_insert_device().

	.. v1_0_device_configure_media() - This is a version1.1 configure media callback function (called once per insertion)
	that provides configuration data to version 1.1 using data that was assigned using the method in apiinit
	and apirun.c in version1.0.

	.. v1_0_device_configure_volume() - This is a version1.1 configure media callback function that
	provides configuration data to version 1.1 using when volume on a device are mounted. It returns data
	that was assigned using the method in apiinit and apirun.c in version1.0.

    .. v1_0_device_ioctl() - This is a version1.1 device io control function. When there is a matching
    function it calls the version 1.0 ioctl function.

	..  v1_0_device_io() - This is a version1.1 device io (read/write) function. It calls the
	device_io function.

	.. pc_v1_0_diskio_configure() - Simulates the function of the same name that was provided in version1.0. Called
	by the version1.0 apirun methodology. It saves the configuration information in a structure similar to the
	configuration structure used in version1.0. Values form this saved structure are later passed in a version1.1
	comapitble configurationo block.

	.. v1_0_check_reserved_drives() - Version 1.0 device drivers must reserve drive structures for their use when the
	system is started and never release them. The version 1.1 mount manager needs to know when it is processing
	a device insertion event for a version 1.0 device driver with reserved drive structures.

	.. v1_0_check_reserved_drives() and its companion function v1_0_release_reserved_drives() are used to support
	the version1.1 method that dynamically assign drive structures when media is inserted while supporting
	version 1.0 device drivers that must reserve drive structures for their use when the system is started
	and never release them.

	.. pc_calculate_chs() - This is a function borrowed from version 1.0. In version 1.1 device drivers are required to
	return valid h, c and n values if they return a valid lba value. In version 1.0 h, c and n were calculated. This
	function is used to convert lba only responses returned from version 1.0 drivers to include valid values for h, c and n.

*/

void v1_0_poll_devices(void)
{
int i;
DDRIVE *pdr;
	for (i = 0; i < v1_0_n_media_structures;i++)
	{
		pdr = v1_0_drno_to_dr_map[v1_0_n_media_structure_array[i].media_config.requested_driveid];
		if (pdr)
		{
		BOOLEAN should_be_non_empty, should_be_empty;
		int media_status,new_media_status;


			should_be_non_empty = should_be_empty = FALSE;

			/* If the device is removable check status to see if we must recover or remount */
			if (pdr->drive_flags & DRIVE_FLAGS_REMOVABLE)
			{
				new_media_status = 0;
				media_status = v1_0_call_device_ioctl(pdr, DEVCTL_CHECKSTATUS, 0);
				/* If it reported a change call it again. This should clear it. It will return the new status
					zero will mean an insert
					DEVTEST_NOMEDIA will mean a removal
					any other replies we act as if it is a removal because we can't access it
				*/
				if (media_status == DEVTEST_CHANGED)
					new_media_status = v1_0_call_device_ioctl(pdr, DEVCTL_CHECKSTATUS, 0);
			}
			else
				media_status = new_media_status = 0;

			switch (media_status)
			{
				case 0:            /* Device is up insert it if it is not already inserted. */
					should_be_non_empty = TRUE;
					break;
				case DEVTEST_CHANGED: /* Device changed force it to de-insert it if it is already inserted and
				                         then re-insert if the device is up */
					should_be_empty = TRUE;
					if (new_media_status == 0)
						should_be_non_empty = TRUE;
					break;
				case DEVTEST_NOMEDIA:
				case DEVTEST_UNKMEDIA:
				default:
					should_be_empty = TRUE;
					break;
			}
			/* First process ejects */
			if (should_be_empty && prtfs_cfg->drno_to_dr_map[pdr->driveno])
			{/* Pass (i+1) as handle instead of i, because it was mounted with i+1 as a handle */
				pc_rtfs_media_alert((void *) (i+1), RTFS_ALERT_EJECT, 0);
			}
			/* Now inserts */
			if (should_be_non_empty && !prtfs_cfg->drno_to_dr_map[pdr->driveno])
			{
 				v1_0_insert_device(i);
			}
		}
	}
}

static void v1_0_insert_device(int device_number)
{
DDRIVE *pdr;
DEV_GEOMETRY gc;
struct rtfs_media_insert_args media_parms;
int ret_val, ndrives_allocated, first_drive_allocated;

	rtfs_memset(&media_parms, 0, sizeof(media_parms));
	ret_val = ndrives_allocated =  first_drive_allocated = 0;

	/* Check arguments */
	media_parms.devhandle 	 				= (void *) (device_number + 1);	 /* Use 1 to v1_0_n_media_structures because 0 is not allowed */
    media_parms.device_type  				= V10DEVTYPE;
    media_parms.device_io    				= v1_0_device_io;
    media_parms.device_ioctl 				= v1_0_device_ioctl;
    media_parms.device_configure_media 		= v1_0_device_configure_media;
    media_parms.device_configure_volume 	= v1_0_device_configure_volume;
    media_parms.eraseblock_size_sectors = 0;
    media_parms.device_erase = 0;

	pdr = v1_0_drno_to_dr_map[v1_0_n_media_structure_array[device_number].media_config.requested_driveid];

    /* Call the old school get geometry function if it fails return and generate no insert event, like nothing ever happened. */
    if ( v1_0_call_device_ioctl(pdr, DEVCTL_GET_GEOMETRY, (void *) &gc) != 0)
	{
		return;
	}

    /* Convert old old school get geometry values to new scheme. */
    if (gc.dev_geometry_lbas && (!gc.dev_geometry_heads || !gc.dev_geometry_secptrack || !gc.dev_geometry_cylinders))
	{
		dword cylinders;
		int heads, secptrack;
		_pc_calculate_chs(gc.dev_geometry_lbas, &cylinders, &heads, &secptrack);
		gc.dev_geometry_cylinders = cylinders;
		gc.dev_geometry_heads = heads;
		gc.dev_geometry_secptrack = secptrack;
	}
    media_parms.numheads = (dword) gc.dev_geometry_heads;
    media_parms.secptrk  = (dword) gc.dev_geometry_secptrack;
    if (!media_parms.numheads || !media_parms.secptrk)
		return;
	if (gc.dev_geometry_lbas)
	{
    	media_parms.media_size_sectors = gc.dev_geometry_lbas;
    	media_parms.numcyl   = (dword) (media_parms.media_size_sectors/(media_parms.numheads * media_parms.secptrk));
	}
	else
	{
    	media_parms.media_size_sectors = dword (media_parms.numheads * media_parms.secptrk);
    	media_parms.numcyl   = gc.dev_geometry_cylinders;
    	media_parms.media_size_sectors *= media_parms.numcyl;
	}
   	if (!media_parms.numcyl)
		return;
   	if (media_parms.numcyl > 1023)
       	media_parms.numcyl = 1023;

    media_parms.sector_size_bytes = RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES;
    ret_val = pc_rtfs_media_insert(&media_parms);
}


static int v1_0_device_configure_media(struct rtfs_media_insert_args *pmedia_parms, struct rtfs_media_resource_reply *pmedia_config_block, int sector_buffer_required)
{
int device_number;

	device_number = (int )pmedia_parms->devhandle;
	device_number -= 1;			/* Subtract 1 because we use 1 to v1_0_n_media_structures because 0 is not allowed */

	if (device_number < 0 || device_number >= v1_0_n_media_structures)
		return(-1);
	*pmedia_config_block = v1_0_n_media_structure_array[device_number].media_config;
	if (!sector_buffer_required)
	{
		pmedia_config_block->device_sector_buffer_size_bytes = 0;
		pmedia_config_block->device_sector_buffer_base = 0;
		pmedia_config_block->device_sector_buffer_data = 0;
	}

    /*	0  if successful
		-1 if unsupported device type
		-2 if out of resources
	*/
	return(0);
}

static int v1_0_device_configure_volume(struct rtfs_volume_resource_request *prequest_block, struct rtfs_volume_resource_reply *pvolume_config_block)
{
DRIVE_CONFIGURE *pdisk_config;

	pdisk_config = get_default_disk_configuration(prequest_block->driveid);
	if (!pdisk_config)
		return(-1);

    rtfs_memset(pvolume_config_block, 0, sizeof(*pvolume_config_block));

	pvolume_config_block->use_dynamic_allocation = 0;

    pvolume_config_block->drive_operating_policy = pdisk_config->drive_operating_policy;       /* Default drive operating policy, see Rtfs manual */
    pvolume_config_block->n_sector_buffers = pdisk_config->num_block_buffers;
    pvolume_config_block->sector_buffer_base = pdisk_config->block_buffer_data;
    pvolume_config_block->sector_buffer_memory = (void *) pdisk_config->block_buffer_data;
    pvolume_config_block->blkbuff_memory = pdisk_config->block_buffer_structures;

    pvolume_config_block->n_fat_buffers = pdisk_config->num_fat_buffers;
    pvolume_config_block->fat_buffer_page_size_sectors = pdisk_config->fat_buffer_pagesize;
    pvolume_config_block->fatbuff_memory = pdisk_config->fat_buffer_structures;
    pvolume_config_block->fat_buffer_memory  = (void *) pdisk_config->fat_buffer_data;
    pvolume_config_block->fat_buffer_base    =  pdisk_config->fat_buffer_data;

    pvolume_config_block->n_file_buffers  = 0;
    pvolume_config_block->file_buffer_size_sectors = 0;
    pvolume_config_block->filebuff_memory = 0;
    pvolume_config_block->file_buffer_base = 0;

#if (INCLUDE_FAILSAFE_CODE)
{
    FAILSAFECONTEXT *fs_context;
    fs_context = (FAILSAFECONTEXT *) pdisk_config->user_failsafe_context;

    pvolume_config_block->fsrestore_buffer_size_sectors = pdisk_config->fs_user_restore_transfer_buffer_size;/* Failsafe restore buffer size in sectors */
    pvolume_config_block->failsafe_buffer_base   = pdisk_config->fs_user_restore_transfer_buffer;
    pvolume_config_block->failsafe_buffer_memory = (void *) pdisk_config->fs_user_restore_transfer_buffer;     /* 1 element must be (fsrestore_buffer_size_sectors * sector_size) bytes */
    pvolume_config_block->fsjournal_n_blockmaps = pdisk_config->fs_blockmap_size;
    pvolume_config_block->fsfailsafe_context_memory = (FAILSAFECONTEXT *) pdisk_config->user_failsafe_context;/* Failsafe context block */
    pvolume_config_block->fsjournal_blockmap_memory = pdisk_config->fs_blockmap_core;
    pvolume_config_block->fsindex_buffer_size_sectors = pdisk_config->fs_indexbuffer_size;  /* Failsafe index buffer size in sectors must be at least 1 */
    pvolume_config_block->failsafe_indexbuffer_base = pdisk_config->fs_indexbuffer_core;
    pvolume_config_block->failsafe_indexbuffer_memory = (void *)pdisk_config->fs_indexbuffer_core;
}
#endif  /*  INCLUDE_FAILSAFE_CODE   */

    return(0);
}

static int  v1_0_device_ioctl(void  *devhandle, void *pdrive, int opcode, int iArgs, void *vargs)
{
DDRIVE *pdr;
int rval;

	pdr = (DDRIVE *) pdrive;

    RTFS_ARGSUSED_INT(iArgs);
    RTFS_ARGSUSED_PVOID(vargs);
    RTFS_ARGSUSED_PVOID(devhandle);

	switch(opcode)
    {
        case RTFS_IOCTL_FORMAT:
		{
			DEV_GEOMETRY gc;
			rval = v1_0_call_device_ioctl(pdr, DEVCTL_GET_GEOMETRY, (void *) &gc);
			if (rval == 0)
       			rval = v1_0_call_device_ioctl(pdr, DEVCTL_FORMAT, (void *) &gc);
		}
		break;
		case RTFS_IOCTL_SHUTDOWN:
        	v1_0_call_device_ioctl(pdr, DEVCTL_SHUTDOWN, 0);
		case RTFS_IOCTL_INITCACHE:
		case RTFS_IOCTL_FLUSHCACHE:
		default:
			rval = 0;
			break;
    }
    return(rval);
}

static int v1_0_device_io(void  *devhandle, void *pdrive, dword sector, void  *buffer, dword count, BOOLEAN reading)
{
DDRIVE *pdr;
int rval;
DDRIVE *saved_pdr;

    RTFS_ARGSUSED_PVOID(devhandle);
	pdr = (DDRIVE *) pdrive;
	saved_pdr = prtfs_cfg->drno_to_dr_map[pdr->driveno];
	prtfs_cfg->drno_to_dr_map[pdr->driveno] = pdr;
	rval = pdr->dev_table_drive_io(pdr->driveno, sector, buffer, (word) count, reading);
	prtfs_cfg->drno_to_dr_map[pdr->driveno] = saved_pdr;
	return(rval);
}



/* Called after all devices have been attached by apiinit.c, creates media structures to be shared by all
   drives that share the same device (multiple partitions).

   Device sector buffers (user buffers) are provided by pc_v1_0_diskio_configure() which is called
   later by apirun.c
*/
BOOLEAN v1_0_assign_media_structures(void)
{
int i;
	v1_0_n_media_structures = 0;
	for (i = 0; i < 26;)
	{
		DDRIVE *pdr;
		pdr = v1_0_drno_to_dr_map[i];
		if (pdr)
		{
		struct v1_0_n_media_structure *pmedia_structure;
			pmedia_structure = &v1_0_n_media_structure_array[v1_0_n_media_structures];
			rtfs_memset(pmedia_structure, 0, sizeof(*pmedia_structure));

		    v1_0_n_media_structures += 1;

		    pmedia_structure->media_config.requested_driveid = pdr->driveno; /* is alsop i */
		    pmedia_structure->media_config.requested_max_partitions = v1_0_count_partitions(i);
		    pmedia_structure->media_config.use_fixed_drive_id = TRUE;

		    /* Device sector buffer (user buffer) is set by apirun */
		    pmedia_structure->media_config.use_dynamic_allocation =  FALSE;
		    i += pmedia_structure->media_config.requested_max_partitions;
		}
		else
			i += 1;
	}
	return(TRUE);
}

/* Called  by initialize_drives() in apirun.c. The disk config structure is stored by the caller and accessed
   later but at this point we test for the user buffer assignment and copy it to the media configuration
   structure - V1.1 assigns one user buffer per media, 1.0 assigned one per volume */
BOOLEAN pc_v1_0_diskio_configure(int driveid, DRIVE_CONFIGURE *pdisk_config)
{
	int i;
	for (i = 0; i < v1_0_n_media_structures;i++)
	{
	int drivehi;
		drivehi = 	v1_0_n_media_structure_array[i].media_config.requested_driveid + v1_0_n_media_structure_array[i].media_config.requested_max_partitions - 1;
		if (v1_0_n_media_structure_array[i].media_config.requested_driveid <= driveid && driveid <= drivehi)
		{
			if (v1_0_n_media_structure_array[i].media_config.requested_driveid == driveid)
			{
				v1_0_n_media_structure_array[i].media_config.device_sector_buffer_size_bytes = pdisk_config->user_buffer_size_sectors * RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES;
		    	v1_0_n_media_structure_array[i].media_config.device_sector_buffer_base = pdisk_config->user_buffer;
		    	v1_0_n_media_structure_array[i].media_config.device_sector_buffer_data = pdisk_config->user_buffer;
		    	return(TRUE);
			}
			else if (!v1_0_n_media_structure_array[i].media_config.device_sector_buffer_size_bytes)
				return(FALSE);
		}
	}
	return(FALSE);
}


/* Called by the dynamic mount manager to release drive structures, if the drive structure was reserved
   for a v10 driver it is not returned to the heap because pre-inititialzed values must be retained */
BOOLEAN v1_0_release_reserved_drive(DDRIVE *pdrive)
{ /* Return TRUE if we released the drive back to pre-reserved devices */
  /* Return FALSE if not pre-reserved so the caller may returned it to the heap */

  if (v1_0_drno_to_dr_map[pdrive->driveno])
  	return(TRUE);
  else
  	return(FALSE);
}
/* Called by the dynamic mount manager when it allocates drive structures, if the drive structure was reserved
   for a v10 driver it is not allocated from the heap because pre-inititialzed values must be retained

   See if the requested drive IDs and drive structures reserved at startup by old school sevice drivers
   if so initialize pdrive_stuctures woith pointers to drive structures for ndrives volumes

*/
BOOLEAN v1_0_check_reserved_drives(DDRIVE **pdrive_stuctures,int drive_id, int ndrives)
{
int i;
DDRIVE *pdr;
DDRIVE drcopy;
    for (i = 0; i < ndrives; i++)
        pdrive_stuctures[i] = 0;
   	if (!v1_0_drno_to_dr_map[drive_id])
		return(TRUE);	/* Not reserved but not an error */
    for (i = 0; i < ndrives; i++,drive_id++)
	{
    	pdr = v1_0_drno_to_dr_map[drive_id];
    	if (!pdr)
			return(FALSE);	/* All must be reserved otherwise it is an error */
		/* Clear the drive structure but preserve information that must outlive an insert remove cycle */
    	drcopy = *pdr;
    	rtfs_memset(pdr, 0, sizeof(DDRIVE));
    	pdr->pcmcia_slot_number = drcopy.pcmcia_slot_number;
    	pdr->pcmcia_controller_number = drcopy.pcmcia_controller_number;
    	pdr->pcmcia_cfg_opt_value = drcopy.pcmcia_cfg_opt_value;
    	pdr->register_file_address = drcopy.register_file_address;
    	pdr->interrupt_number = drcopy.interrupt_number;
    	pdr->dev_table_drive_io = drcopy.dev_table_drive_io;
    	pdr->dev_table_perform_device_ioctl = drcopy.dev_table_perform_device_ioctl;

    	pdr->drive_flags = drcopy.drive_flags;

		pdr->controller_number = drcopy.controller_number;
		pdr->logical_unit_number = drcopy.logical_unit_number;
		pdr->partition_number = drcopy.partition_number;
    	pdr->driveno = drive_id;
        pdrive_stuctures[i] = pdr;
	}
	return(TRUE);
}


/* Required by v1_0_assign_media_structures() */
static int v1_0_count_partitions(int start_drive_id)
{
int i,n_parts;
	n_parts = 1;
	for (i = start_drive_id+1; i < prtfs_cfg->cfg_NDRIVES; i++)
	{
		if (v1_0_drno_to_dr_map[i] && v1_0_drno_to_dr_map[i]->partition_number == n_parts)
			n_parts+= 1;
		else
			break;
	}
	return(n_parts);
}




int v1_0_call_device_ioctl(DDRIVE *pdr, int command, void *pargs)
{
int rval;
DDRIVE *saved_pdr;

	saved_pdr = prtfs_cfg->drno_to_dr_map[pdr->driveno];
	prtfs_cfg->drno_to_dr_map[pdr->driveno] = pdr;
	rval = pdr->dev_table_perform_device_ioctl(pdr->driveno, command, pargs);
	prtfs_cfg->drno_to_dr_map[pdr->driveno] = saved_pdr;

	return(rval);
}


/*
    static dword pc_calculate_chs(dword total_sectors, dword *cylinders, int *heads, int *secptrack)

    Given total sectors, fill in values for *cylinders, *heads, *secptrack such that
    *cylinders, *heads, and *secptrack are all legal values according to the hcn spec.

    Return the total volume size in sectors.

    If the volume size must be truncated to follow the hcn spec, then return the
    maximum values for cylinders, heads and secptrack but return the input rounded to the cylinder boundary.

    If the size is not truncated in order to follow the hcn spec, then return the
    (cylinders time heads times secptrackand

*/

#define MAX_HEADS               255  /* Maximum heads, sectors per cylinder and cylinders according to the MBR spec. */
#define MAX_SECTORS              63
#define MAX_MBR_CYLINDERS      1023

static dword _pc_calculate_chs(dword total_sectors, dword *cylinders, int *heads, int *secptrack)
{
    dword max_cylinder_size,min_cylinder_size,sectors_per_track,heads_per_cylinder,total_cylinders;
    dword return_total_sectors,total_tracks;

    /* If this value is non-zero, it means h was truncated, return_total_sectors contains total sectors on a cylinder
       boundary, h*c*n does not equal return_total_sectors */
    return_total_sectors = 0;
    /* Calculate the maximum legal cylinder size */
    max_cylinder_size =  MAX_SECTORS;
    max_cylinder_size *= MAX_HEADS;
    /* Calculate the minimum cylinder size needed to hold all sectors */
    min_cylinder_size = total_sectors/MAX_MBR_CYLINDERS;
    if (!min_cylinder_size)
        min_cylinder_size = 1;

    /* Calculate a good value for secptrack. MAX_SECTORS (63) is preferred because it is typically used */
    if (total_sectors >= MAX_SECTORS)
        sectors_per_track = MAX_SECTORS;
    else
        sectors_per_track = 1;

    /* Calculate cylinders and heads */
    total_tracks = total_sectors/sectors_per_track;

    /* Divide total tracks by maximum cylinder count, this will yield minimum heads */
    if (total_tracks > MAX_MBR_CYLINDERS)
    { /* More tracks than the maximum cylinder size, so calculate number of heads needed to require <= MAX_MBR_CYLINDERS */
        total_cylinders = MAX_MBR_CYLINDERS;
        heads_per_cylinder = (total_tracks/MAX_MBR_CYLINDERS);
        if (heads_per_cylinder > MAX_HEADS)
        { /* We have to truncate MAX_HEADS so H*C*N will not be accurate.
            calulate and return an accurate cylinder alligned value from the untruncated values */
            heads_per_cylinder = MAX_HEADS;
            {dword uc=0; /* Untruncated cylinder count */
             dword sectors_per_cyl;
                sectors_per_cyl = sectors_per_track * heads_per_cylinder;
                if (sectors_per_cyl)    /* Defensive */
                    uc = total_sectors/sectors_per_cyl;
                return_total_sectors =  uc * sectors_per_cyl;
            }
        }
    }
    else
    {
        heads_per_cylinder = 1;
        total_cylinders = total_tracks;
    }
    *cylinders = total_cylinders;
    *heads     = (int) (heads_per_cylinder & 0xff); /* Maximum value 255 */
    *secptrack = (int) (sectors_per_track & 0x3f);  /* Maximum value 63 */
    /* Now if a return value was not already calculated because we truncated, calculate one from our new hcn values */
    if (!return_total_sectors)
        return_total_sectors = total_cylinders * heads_per_cylinder * sectors_per_track;
    return(return_total_sectors);
}
