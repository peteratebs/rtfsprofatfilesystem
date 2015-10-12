/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 2000
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
#include "rtfs.h"
#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */

BOOLEAN _pc_get_media_parms(DDRIVE *pdr, PDEV_GEOMETRY pgeometry);

static BOOLEAN pc_mkfs_calculated(DDRIVE *pdr, PDEV_GEOMETRY pgeometry, FMTPARMS *pfmt);
static BOOLEAN _scrub_volume(DDRIVE *pdr, FMTPARMS *pfmt, byte *pbuffer, dword buffer_size_sectors, dword buffer_size_bytes);

#define INOPBLOCK               16   /* FAT dirents in a 512 byte sector */
#define DEBUG_FORMATTING        0
#define DEBUG_PRINTF printf

#define MAX_HEADS 256				/* Maximum heads, sectors per cylinder and cylinders according to the MBR spec. */
#define MAX_SECTORS 63
#define MAX_MBR_CYLINDERS 1024


/***************************************************************************
    PC_GET_MEDIA_PARMS -  Get media parameters.

Description
    Queries the drive's associated device driver for a description of the
    installed media. This information is used by the pc_format_media,
    pc_partition_media and pc_format_volume routines. The application may
    use the results of this call to calculate how it wishes the media to
    be partitioned.

    Note that the floppy device driver uses a back door to communicate
    with the format routine through the geometry structure. This allows us
    to not have floppy specific code in the format routine but still use the
    exact format parameters that DOS uses when it formats a floppy.

    See the following definition of the pgeometry structure:
        typedef struct dev_geometry {
            int dev_geometry_heads;     -- - Must be < 256
            int dev_geometry_cylinders; -- - Must be < 1024
            int dev_geometry_secptrack; -- - Must be < 64
            BOOLEAN fmt_parms_valid;    -- If the device io control call sets this
                                        -- TRUE then it it telling the applications
                                        -- layer that these format parameters should
                                        -- be used. This is a way to format floppy
                                        -- disks exactly as they are fromatted by dos.
            FMTPARMS fmt;
        } DEV_GEOMETRY;

typedef struct dev_geometry  *PDEV_GEOMETRY;


Returns
    Returns TRUE if it was able to get the parameters otherwise
    it returns FALSE.

    errno is set to one of the following
    0               - No error
    PEINVALIDDRIVEID- Drive component is invalid
    PEDEVICEFAILURE - Device driver get device geometry request failed
    PEINVALIDPARMS  - Device driver returned bad values
****************************************************************************/




#if (INCLUDE_CS_UNICODE)
BOOLEAN pc_get_media_parms_cs(byte *path, PDEV_GEOMETRY pgeometry, int use_charset)
#else
BOOLEAN pc_get_media_parms(byte *path, PDEV_GEOMETRY pgeometry)
#endif
{
int driveno;
DDRIVE *pdr;
    CHECK_MEM(BOOLEAN, 0)

    rtfs_clear_errno();  /* pc_get_media_parms: clear error status */
    if (!path  || !pgeometry)
    {
        rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
        return(FALSE);
    }
    /* Make sure its a valid drive number */
    driveno = pc_parse_raw_drive(path, CS_CHARSET_ARGS);
    if (driveno < 0)
    {
inval:
        rtfs_set_errno(PEINVALIDDRIVEID, __FILE__, __LINE__);
        return(FALSE);
    }
    pdr = pc_drno_to_drive_struct(driveno);
    if (!pdr)
        goto inval;
    return(_pc_get_media_parms(pdr, pgeometry));
}




static BOOLEAN pc_mkfs_dynamic(DDRIVE *pdr,PDEV_GEOMETRY pgeometry, FMTPARMS *pfmt);
BOOLEAN _pc_get_media_parms(DDRIVE *pdr, PDEV_GEOMETRY pgeometry) /* __apifn__*/
{
    pgeometry->bytespsector             = (int) pdr->pmedia_info->sector_size_bytes;
    pgeometry->dev_geometry_cylinders   = pdr->pmedia_info->numcyl;
    pgeometry->dev_geometry_heads       = (int) pdr->pmedia_info->numheads;
    pgeometry->dev_geometry_secptrack   = (int) pdr->pmedia_info->secptrk;
    pgeometry->dev_geometry_lbas        = pdr->pmedia_info->media_size_sectors;
    pgeometry->fmt_parms_valid          = FALSE;
    return(TRUE);
}


/***************************************************************************
    PC_FORMAT_MEDIA -  Device level format

Description
    This routine performs a device level format on the specified
    drive.

Returns
    Returns TRUE if it was able to perform the operation otherwise
    it returns FALSE.

    Note: The the logical drive must be claimed before this routine is
    called and later released by the caller.

    errno is set to one of the following
    0               - No error
    PEINVALIDDRIVEID- Drive component is invalid
    PEDEVICEFAILURE - Device driver format request failed
****************************************************************************/

#if (INCLUDE_CS_UNICODE)
BOOLEAN pc_format_media_cs(byte *path, int use_charset)
#else
BOOLEAN pc_format_media(byte *path)
#endif
{
int driveno;
DDRIVE *pdr;
DEV_GEOMETRY geometry;
    CHECK_MEM(BOOLEAN, 0)

    rtfs_clear_errno(); /* pc_format_media: clear error status */
    if (!path)
        goto inval;
    /* Make sure its a valid drive number */
    driveno = pc_parse_raw_drive(path, CS_CHARSET_ARGS);
    if (driveno < 0)
    {
inval:
        rtfs_set_errno(PEINVALIDDRIVEID, __FILE__, __LINE__);
        return(FALSE);
    }

    pdr = pc_drno_to_drive_struct(driveno);
    if (!pdr)
        goto inval;
    /* Call the device driver to retrieve parameters */
    if (!_pc_get_media_parms(pdr, &geometry))
        goto device_failure;
    /* Make sure that is it closed up */
    pc_dskfree(driveno);

    /* Format the device geometry */
    if (pdr->pmedia_info->device_ioctl(pdr->pmedia_info->devhandle, (void *) pdr, RTFS_IOCTL_FORMAT, 0 , 0))
    {
device_failure:
        rtfs_set_errno(PEDEVICEFAILURE, __FILE__, __LINE__);
        return(FALSE);
    }
    return(TRUE);
}


static BOOLEAN _get_partition_information(DDRIVE *pdr, FMTPARMS *pfmt, PDEV_GEOMETRY pgeometry);
static BOOLEAN _get_format_parms_by_size(dword nsectors, FMTPARMS *pfmt);
static BOOLEAN _pc_calculate_fat_size_sectors(FMTPARMS *pfmt);
static BOOLEAN _pc_format_volume_ex(DDRIVE *pdr, RTFSFMTPARMSEX *pappfmt);


/***************************************************************************
    PC_FORMAT_VOLUME -  Format a volume

Description
    This routine formats the volume referred to by drive letter.
    drive structure is queried to determine if the device is parttioned
    or not. If the device is partitioned then the partition table is read
    and the volume within the partition is formatted. If it is a non
    partitioned device the device is formatted according to the supplied
    pgeometry parameters. The pgeometry parameter contains the the media
    size in HCN format. It also contains a

    Note: The the logical drive must be claimed before this routine is
    called and later released by the caller.

Returns
    Returns TRUE if it was able to perform the operation otherwise
    it returns FALSE.

    errno is set to one of the following
    0               - No error
    PEINVALIDDRIVEID- Drive component is invalid
    PEIOERRORREADMBR- Partitioned device. IO error reading
    PEINVALIDMBR    - Partitioned device has no master boot record
    PEINVALIDMBROFFSET - Requested partition has no entry in master boot record
    PEINVALIDPARMS  - Inconsistent or missing parameters
    PEIOERRORWRITE  - Error writing during format
    An ERTFS system error
****************************************************************************/

/*
    pc_format_volume() fills a format parameter structure for the requested volume and then calls a
    generic format utility named _pc_mkfs().

    Format parameters are calculated from several sources:

        a. Media parameters - These are returned from the device driver (heads, sectors, cylinders, disk size)
        b. Additional parameters may come from one of 3 sources:
           1. The device driver may have returned a valid format with the media information. This is
              an old method used primarily for floppy disks.
           2. The device driver may be dynamic and provide format parameters via an extended io control call.
              This method is used for media with non standard sector sizes and erase blocks.

           If The device driver is not dynamic and format parameters were not returned with the media parameters then:
                . If the volume resides in a partition, parameters are calculated based on the partition size and start.
                . If the volume does not reside in a partition, paramters are calculated from information provided
                  by the device driver's DEVCTL_GET_GEOMETRY IO control call.

    The section describes the format parameters and how they are initialized.

    A - Labels - These are initialized by the pc_format_volume() before the device driver or other Rtfs internal functions
                 are called.

                 If a dynamic device driver is providing format parameters these values may overridden
                 by the device driver or they may be used unchanged.

                 The default values are:

        pfmt->oemname[9]            - Initialized by Rtfs to “MSWIN4.1”, the setting least likely to cause compatibility problems.
        pfmt->mediadesc             - Initialized by Rtfs to 0xF8
        pfmt->physical_drive_no     - Initialized by Rtfs to the drive number (0,1,2,3..)
        pfmt->binary_volume_label   - Initialized by Rtfs to 0x12345678L
        pfmt->text_volume_label[12] - Initialized by Rtfs to "VOLUMELABEL"

    B - Sector size and partition offset
        pfmt->bytes_per_sector      -  Sector size
                                        Dynamic device drivers must provide this value.
                                        If_not using a dynamic device driver it is set to RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES (usually 512)
        pfmt->numhide              -   Must be set to the raw sector offset of the beginning of the partition. If the device has
                                       no partition it must be zero.
                                        Dynamic device drivers must provide this value.
                                        If_not using a dynamic device driver it is set to the start of the current partition.

    C - Volume format parameters based on the number of sectors in the volume.

       Dynamic device drivers must provide these values.
       If_not using a dynamic device driver
            If the floppy disk legacy mode of DEVCTL_GET_GEOMETRY is being used, these values are copied from the return media structure.
            Otherwise, normally, the routine named get_format_parms_by_size() is called to look the parameters up in a table.

        pfmt->secpalloc            - Cluster size in sectors
        pfmt->secreserved          - Reserved sectors before the FAT, including the BPB
                                          Note: Dynamic device drivers can adjust this value to align the start of FAT on an erase block.
        pfmt->nibs_per_entry       - 3, 4 or 8
        pfmt->numfats              - Number of FAT copies
        pfmt->numroot              - Number of root directory entries

    D - Volume size parameters in lba and hcn mode

       Dynamic device drivers must provide these values.
        Note: Rtfs uses only the pfmt->total_sectors field, the other fields,
              pfmt->secptrk, pfmt->numhead, and pfmt->numcyl are placed direclty in the BPB without checking for validity.

       If_not using a dynamic device driver Rtfs will either:

        Call calculate_volume_size_parms() to look up the following values in a table.
            pfmt->secptrk
            pfmt->numhead
            pfmt->numcyl
            pfmt->total_sectors
            pfmt->secpfat

        Or, if the floppy disk legacy mode support for DEVCTL_GET_GEOMETRY is being used, the values
        are copied/derived from the return media structure.


    E - Post processing
        pfmt->file_sys_type[12]    - Initialized by Rtfs to FAT12, FAT16 or FAT32 after pfmt->nibs_per_entry has been determined

*/




#define BIN_VOL_LABEL 0x12345678L

#if (INCLUDE_CS_UNICODE)
BOOLEAN pc_format_volume_ex_cs(byte *path, RTFSFMTPARMSEX *pappfmt, int use_charset)
#else
BOOLEAN pc_format_volume_ex(byte *path, RTFSFMTPARMSEX *pappfmt)
#endif
{
DDRIVE *pdr;
int driveno;

    CHECK_MEM(BOOLEAN, 0)
    rtfs_clear_errno(); /* pc_format_volume: clear error status */
	if (!path || !pappfmt)
	{
        rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
        return(FALSE);
    }
    /* Make sure it s a valid drive number */
    driveno = pc_parse_raw_drive(path, CS_CHARSET_ARGS);
    if (driveno < 0)
    {
        rtfs_set_errno(PEINVALIDDRIVEID, __FILE__, __LINE__);
        return(FALSE);
    }
    pdr = pc_drno_to_drive_struct(driveno);

    if (!pdr)
    {
        rtfs_set_errno(PEINVALIDDRIVEID, __FILE__, __LINE__); /* pc_format_volume: bad arguments */
        return(FALSE);
    }
    return (_pc_format_volume_ex(pdr, pappfmt));
}



#if (INCLUDE_CS_UNICODE)
BOOLEAN pc_format_volume_cs(byte *path, int use_charset)
#else
BOOLEAN pc_format_volume(byte *path)
#endif
{
DDRIVE *pdr;
BOOLEAN ret_val;

    CHECK_MEM(BOOLEAN, 0)
    rtfs_clear_errno(); /* pc_format_volume: clear error status */
	if (!path)
	{
        rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
        return(FALSE);
    }
    /* Make sure it s a valid drive number */
    pdr = check_drive_by_name(path, CS_CHARSET_ARGS);
    if (!pdr)
        return(FALSE);
    ret_val = _pc_format_volume_ex(pdr, 0);

    rtfs_release_media_and_buffers(pdr->driveno);
	return(ret_val);
}


static BOOLEAN _pc_format_volume_ex(DDRIVE *pdr, RTFSFMTPARMSEX *pappfmt)
{
FMTPARMS fmt;
DEV_GEOMETRY geometry;
int driveno;

	driveno = pdr->driveno;
    pc_dskfree(driveno);   /* Make sure all is flushed */
    /* Call the device driver to retrieve parameters */
    if (!_pc_get_media_parms(pdr, &geometry))
    {
        rtfs_set_errno(PEDEVICEFAILURE, __FILE__, __LINE__);
        return(FALSE);
    }
    rtfs_memset(&fmt, 0,  sizeof(fmt));

	/* If the application provided instructions move them into the format control structure */
	if (pappfmt)
	{
        fmt.scrub_volume = pappfmt->scrub_volume;
        fmt.fixed_bits_per_cluster = pappfmt->bits_per_cluster;
        fmt.fixed_numroot = pappfmt->numroot;
        fmt.fixed_numfats = pappfmt->numfats;
        fmt.fixed_secpalloc = pappfmt->secpalloc;
        fmt.fixed_numfats = pappfmt->numfats;
        fmt.fixed_secreserved = pappfmt->secreserved;
	}

    /* Initialize A Group of format parameters */
    rtfs_cs_strcpy(&fmt.oemname[0], (byte *)pustring_sys_oemname, CS_CHARSET_NOT_UNICODE);
    fmt.physical_drive_no 	=  (byte) driveno;
    fmt.mediadesc           = 0xF8;
    fmt.binary_volume_label = BIN_VOL_LABEL;
    rtfs_cs_strcpy(fmt.text_volume_label, (byte *)pustring_sys_volume_label, CS_CHARSET_NOT_UNICODE);

    /* format parameters are filled in by the device driver */
    return(pc_mkfs_dynamic(pdr, &geometry, &fmt));
}

static BOOLEAN _pc_mkfs(DDRIVE *pdr, FMTPARMS *pfmt, byte *pbuffer, dword buffer_size_bytes);

/* Round v up to an ebsized boundary */
dword _eb_align(dword v, dword ebsize)
{
dword mask,mask_not;
    if (ebsize)
    {
        mask = ebsize - 1;
        mask_not = ~mask;
        if (v & mask)
        {
            v &= mask_not;
            v += ebsize;
        }
    }
    return(v);
}

/* Format the volume using fixed format values retrieved from the device driver's pc_ioctl_get_format_parms()_extended ioctl call. */
static BOOLEAN pc_mkfs_dynamic(DDRIVE *pdr,PDEV_GEOMETRY pgeometry, FMTPARMS *pfmt)
{
byte    *pubuff;
BOOLEAN ret_val;

    ret_val = FALSE;

    /* Populate pfmt->bytes_per_sector */
    pfmt->bytes_per_sector  = pdr->pmedia_info->sector_size_bytes;

    /* If no erase blocks and standard sector sizes just resort to normal format */
    if (pfmt->bytes_per_sector == RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES && pdr->pmedia_info->eraseblock_size_sectors == 0)
        return(pc_mkfs_calculated(pdr, pgeometry, pfmt));

    /* Retrieve from partiton table or media values : pfmt->total_sectors, pfmt->numhide,  pfmt->numcyl, pfmt->numhead, pfmt->secptrk */
    if (!_get_partition_information(pdr, pfmt, pgeometry))
        return(FALSE);

    /* Retrieve from format rules table: pfmt->secpalloc, pfmt->secreserved, pfmt->nibs_per_entry, pfmt->numfats, pfmt->numroot */
    if (!_get_format_parms_by_size(pfmt->total_sectors, pfmt))
        return(FALSE);


    /* Adjust values for erase block and sector size */
    {
        dword ltemp, lorig_rootsectors,ladjusted_rootsectors;
        dword lorig_reserved,ladjusted_reserved;
        dword lorig_secpalloc,ladjusted_secpalloc,entries_per_sector;
        dword ltotal_clusters;
		unsigned char orig_nibs_per_entry,adjusted_nibs_per_entry;   /* 3, 4, 8 == FAT12, FAT16, FAT32 */

        lorig_reserved = (dword) pfmt->secreserved;
        lorig_reserved &= 0xfffful;
        lorig_secpalloc = (dword) pfmt->secpalloc;
        lorig_secpalloc &= 0xfful;
		orig_nibs_per_entry = pfmt->nibs_per_entry;

		entries_per_sector = (dword) pfmt->bytes_per_sector/32;
		if (!entries_per_sector)
        	return(FALSE);
        /* How many root sectors do we need */
        lorig_rootsectors = (dword) (pfmt->numroot+entries_per_sector-1)/entries_per_sector;

        /* Start with no adjustments */
        ladjusted_reserved    = lorig_reserved;
        ladjusted_secpalloc   = lorig_secpalloc;
        ladjusted_rootsectors = lorig_rootsectors;
        adjusted_nibs_per_entry = orig_nibs_per_entry;

		if (!pfmt->fixed_bits_per_cluster)
		{
            /* Adjust reserved sectors and cluster size if fixed values were not provided  */
            if (pdr->pmedia_info->eraseblock_size_sectors)
            {  /* Adjust reserved sectors to eb alignment */
                ladjusted_reserved = _eb_align(lorig_reserved, pdr->pmedia_info->eraseblock_size_sectors);
                /* Adjust sectors peralloc to eb alignment */
                ladjusted_secpalloc = _eb_align(lorig_secpalloc, pdr->pmedia_info->eraseblock_size_sectors);
                /* If adjusted sectors peralloc too large reduce it until it is legal */
                while (ladjusted_secpalloc > 64)
                    ladjusted_secpalloc >>= 1;
                ladjusted_rootsectors = _eb_align(lorig_rootsectors, pdr->pmedia_info->eraseblock_size_sectors);
            }
            else if (pfmt->bytes_per_sector >= RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES)
            {   /* Reduce cluster size because sectors are larger */
                ladjusted_secpalloc  = lorig_secpalloc / (pfmt->bytes_per_sector/RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES);
                if (!ladjusted_secpalloc)
                    ladjusted_secpalloc = 1;
            }
            else /* Can only happen if pfmt->bytes_per_sector < RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES) (illegal condition) */
                return(FALSE);  /* Won't happen */
		}
        if (!ladjusted_secpalloc || !ladjusted_reserved || (ladjusted_rootsectors==0 && lorig_rootsectors != 0) )
            return(FALSE);  /* Won't happen */
		{
			BOOLEAN changed_root_sectors = FALSE;
			int loop_count = 0;
    		do
    		{
				loop_count += 1;
				if (loop_count > 2)
					return(FALSE);
                /* Calculate total clusters. This does not consider the FAT tables so it will be larger than the actual */
                ltotal_clusters = (pfmt->total_sectors - ladjusted_rootsectors - ladjusted_reserved)/ladjusted_secpalloc;

                /* Pick a format based on ltotal_clusters. */
        		if (!pfmt->fixed_bits_per_cluster)
        		{
                    if (ltotal_clusters <= 4084L)
                        adjusted_nibs_per_entry = 3;
                    else if (ltotal_clusters <= 65524L)
                    	adjusted_nibs_per_entry = 4;
                    else
                    {
                        adjusted_nibs_per_entry = 8;
                    }
        		}
				if ( (adjusted_nibs_per_entry == 8 && orig_nibs_per_entry != 8) || (adjusted_nibs_per_entry != 8 && orig_nibs_per_entry == 8) )
				{
					changed_root_sectors = TRUE;
					if (adjusted_nibs_per_entry == 8)
					{
                        ladjusted_rootsectors = 0;
                        ladjusted_reserved	= 32;
					}
					else
					{
                        ladjusted_rootsectors = (dword) (512+entries_per_sector-1)/entries_per_sector;
                        if (pdr->pmedia_info->eraseblock_size_sectors) /* Adjust reserved sectors to eb alignment */
							ladjusted_rootsectors = _eb_align(ladjusted_rootsectors, pdr->pmedia_info->eraseblock_size_sectors);
                        ladjusted_reserved	  = 1;
					}
					if (pdr->pmedia_info->eraseblock_size_sectors) /* Adjust reserved sectors to eb alignment */
						ladjusted_reserved = _eb_align(ladjusted_reserved, pdr->pmedia_info->eraseblock_size_sectors);
					orig_nibs_per_entry = adjusted_nibs_per_entry;
				}
				else
					changed_root_sectors = FALSE;
    		} while (changed_root_sectors);
		}
		pfmt->nibs_per_entry = adjusted_nibs_per_entry;
        /* pfmt->nibs_per_entry was just calculated. now finish calculations */
        pfmt->secpalloc      = (byte) (ladjusted_secpalloc & 0xff);
        pfmt->secreserved    = (word) (ladjusted_reserved & 0xffff);
        ltemp = ladjusted_rootsectors * entries_per_sector;
        pfmt->numroot = (word)(ltemp & 0xffff);
        /* Done adjusting */
    }
    if (!_pc_calculate_fat_size_sectors(pfmt)) /* Retrieve pfmt->secpfat */
        return(FALSE);
    /* erase block align secpfat */
    if (pdr->pmedia_info->eraseblock_size_sectors)
        pfmt->secpfat = _eb_align(pfmt->secpfat, pdr->pmedia_info->eraseblock_size_sectors);

    pubuff = 0;
    {   /* Use the user buffer */
    dword buffer_size_sectors;
    dword buffer_size_bytes;
        buffer_size_sectors = buffer_size_bytes = 0;

        pubuff = pc_claim_user_buffer(pdr, &buffer_size_sectors, 0); /* released on cleanup */
        if (!pubuff)
            return(FALSE);
        /* If volume has erase blocks the user buffer is guaranteed to be large enough, but we
           truncate to an erase block size */
        if (pdr->pmedia_info->eraseblock_size_sectors && buffer_size_sectors >= pdr->pmedia_info->eraseblock_size_sectors)
            buffer_size_sectors = pdr->pmedia_info->eraseblock_size_sectors;
        buffer_size_bytes = pdr->pmedia_info->sector_size_bytes;
        ret_val = _pc_mkfs(pdr, pfmt, pubuff, buffer_size_sectors*buffer_size_bytes);
        pc_release_user_buffer(pdr, pubuff);
    }
    return(ret_val);
}



/* Format the volume using fixed format values retrieved from the device drivers DEVCTL_GET_GEOMETRY
   ioctl call. This method is recomended for floppy disk media only */
static BOOLEAN pc_mkfs_calculated(DDRIVE *pdr, PDEV_GEOMETRY pgeometry, FMTPARMS *pfmt)
{
byte *pubuff;
dword buffer_size_sectors;
BOOLEAN ret_val;

    pfmt->bytes_per_sector = RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES;

    /* Already initialized:
        pfmt->bytes_per_sector
        pfmt->oemname
        pfmt->physical_drive_no
        pfmt->mediadesc
        pfmt->binary_volume_label
        pfmt->text_volume_label

     Retrieve:
        pfmt->total_sectors
        pfmt->numhide
        pfmt->numcyl
        pfmt->numhead
        pfmt->secptrk
    */
    if (!_get_partition_information(pdr, pfmt, pgeometry))
        return(FALSE);

    /* Retrieve:
            pfmt->secpalloc                pfmt->secreserved
            pfmt->nibs_per_entry           pfmt->numfats
            pfmt->numroot
    */
    if (!_get_format_parms_by_size(pfmt->total_sectors, pfmt))
        return(FALSE);

    if (!_pc_calculate_fat_size_sectors(pfmt)) /* Retrieve pfmt->secpfat */
        return(FALSE);

    pubuff = pc_claim_user_buffer(pdr, &buffer_size_sectors, 0); /* released on cleanup */
    if (!pubuff)
        return(FALSE);
    /* Call the common format routine */
    ret_val = _pc_mkfs(pdr, pfmt, pubuff, buffer_size_sectors * RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES);
    pc_release_user_buffer(pdr, pubuff);
    return(ret_val);
}


static BOOLEAN _check_fat_size(FMTPARMS *pfmt, dword volume_size_clusters);
static BOOLEAN devio_write_format(DDRIVE *pdr, dword blockno, byte * buf, dword n_to_write);
#if (INCLUDE_FAT32)
static BOOLEAN devio_read_format(DDRIVE *pdr, dword blockno, byte * buf, dword n_to_read);
#endif
static void _format_fsinfo_sector(byte *p, dword volume_size_clusters, int bytes_per_sector);
static dword _get_volume_size_clusters(FMTPARMS *pfmt,dword media_inopblock);

/* Perform format operation - this is called after the format parameter block has been initialized  */
static BOOLEAN _pc_mkfs(DDRIVE *pdr, FMTPARMS *pfmt, byte *pbuffer, dword buffer_size_bytes)
{
    byte  *b_, *bpb;
    dword  n_left, n_to_write;
    dword  volume_size_clusters,buffer_size_sectors;
    dword blockno;
    BOOLEAN ret_val;
    dword media_inopblock;

    if (!pfmt || (pfmt->bytes_per_sector < 512))
        return(FALSE);
    /* Finalize the format parameter block */
    if (pfmt->nibs_per_entry == 3)
        pc_cppad( &(pfmt->file_sys_type[0]), (byte*)"FAT12",8);
    else if (pfmt->nibs_per_entry == 4)
        pc_cppad( &(pfmt->file_sys_type[0]), (byte*)"FAT16",8);
#if (INCLUDE_FAT32)
    else if (pfmt->nibs_per_entry == 8)
        pc_cppad( &(pfmt->file_sys_type[0]), (byte*)"FAT32",8);
#endif
    else
        return(FALSE);


    if (!pdr)
        return(FALSE);

    ret_val = FALSE;

    /* Calculate things from bytes per sector */
    buffer_size_sectors      = buffer_size_bytes/pfmt->bytes_per_sector; /* pfmt->bytes_per_sector is at least 512 */

	/* Scrub the volume clean if requested */
	if (pfmt->scrub_volume && !_scrub_volume(pdr, pfmt, pbuffer, buffer_size_sectors, buffer_size_bytes))
		return(FALSE);

    media_inopblock          = pfmt->bytes_per_sector/32;

/* printf("Formatting with the following parameters: \n"); */
/* printf("        total_sectors == %d\n", (dword) pfmt->total_sectors); */
/* printf("        bytes_per_sector == %d\n", (dword) pfmt->bytes_per_sector); */
/* printf("        secpalloc == %d\n", (dword) pfmt->secpalloc); */
/* printf("        secreserved == %d\n", (dword) pfmt->secreserved); */
/* printf("        nibs_per_entry == %d\n", (dword) pfmt->nibs_per_entry); */
/* printf("        numfats  == %d\n", (dword) pfmt->numfats); */
/* printf("        secpfat  == %d\n", (dword) pfmt->secpfat); */
/* printf("        numhide  == %d\n", (dword) pfmt->numhide); */
/* printf("        numroot  == %d\n", (dword) pfmt->numroot); */
/* printf("        rootsecs == %d\n", (dword) pfmt->numroot/media_inopblock); */
/* printf("        secptrk  == %d\n", (dword) pfmt->secptrk); */
/* printf("        numhead  == %d\n", (dword) pfmt->numhead); */
/* printf("        numcyl   == %d\n", (dword) pfmt->numcyl); */

    /* bpb at b_ are both aliases for pbuffer */
    bpb = b_ = pbuffer;

    /* Build up a block 0   */
    rtfs_memset(bpb, 0, buffer_size_bytes);
    /* start bpb locations 0x0 -  0x14 */
    bpb[0x00] = (byte) 0xe9;    /* Jump vector. Used to id MS-DOS disk */
    bpb[0x01] = (byte) 0x00;
    bpb[0x02] = (byte) 0x00;
    /* Copy the OEM name   */
    pc_cppad(&bpb[0x03], pfmt->oemname, 8);
    /* bytes per sector   */
    {   word w;
        w = (word) pfmt->bytes_per_sector;
        fr_WORD ( &(bpb[0x0b]), w);   /*X*/
    }
    /* sectors / cluster   */
    bpb[0x0d] = pfmt->secpalloc;
    /* Number of reserved sectors. (Including block 0)   */
    fr_WORD ( &(bpb[0x0e]), pfmt->secreserved); /*X*/
    bpb[0x10] = pfmt->numfats;
    /* number of dirents in root   */
    fr_WORD ( &(bpb[0x11]), pfmt->numroot);     /*X*/
    /* total sectors in the volume. A dword at offset 0x20 if >= 64K, a word at offset 0x13 if < 64K */
    if (pfmt->total_sectors > 0xffffL)
    {
        fr_DWORD ( &(bpb[0x20]), pfmt->total_sectors);   /* HUGE partition   */
        fr_WORD ( &(bpb[0x13]), 0);
    }
    else
    {word totsecs;
        totsecs = (word) (pfmt->total_sectors & 0xffffL);
        fr_DWORD ( &(bpb[0x20]), 0);
        fr_WORD ( &(bpb[0x13]), totsecs);
    }
    bpb[0x15] = pfmt->mediadesc;

    /* start "version 3.0 bpb" fields   */
    fr_WORD ( &(bpb[0x18]), pfmt->secptrk);
    fr_WORD ( &(bpb[0x1a]), pfmt->numhead);
    fr_DWORD ( &(bpb[0x1c]), pfmt->numhide);


    /* Use MS-DOS 4.0 Extended parameter block for FAT12 and Fat16 */
    if (pfmt->nibs_per_entry != 8)
    {
        fr_WORD ( &(bpb[0x16]), (word)pfmt->secpfat);
        bpb[0x24] = pfmt->physical_drive_no;
        bpb[0x25] = 0;                          /* chkdsk flags  */
        bpb[0x26] = 0x29;                       /* extended boot signature */
        fr_DWORD(&(bpb[0x27]) , pfmt->binary_volume_label); /*X*/
        pc_cppad( &(bpb[0x2b]), pfmt->text_volume_label, 11);
        pc_cppad( &(bpb[0x36]), &pfmt->file_sys_type[0],8);
    }
    else /* if (pfmt->nibs_per_entry == 8) */
    {    /* Use MS-DOS 7.0 Extended parameter block FAT32 */
        fr_DWORD ( &(bpb[0x24]), (dword)pfmt->secpfat);
        fr_DWORD ( &(bpb[0x28]), (dword)0);                             /* flags and version */
        fr_DWORD ( &(bpb[0x2c]), (dword)2);                             /* FAT32 root dir starting cluster */
        fr_WORD (  &(bpb[0x30]), (word)1);                              /* FAT32 info sector */
        fr_WORD (  &(bpb[0x32]), (word)6);                              /* FAT32 backup boot sector */
        bpb[0x40]               /* Disk unit number,                    same 0x24 in version 4*/
                    = pfmt->physical_drive_no;
        bpb[0x41] = 0x00;       /* Chkdsk flags, unused                 same 0x25 in version 4 */
        bpb[0x42] = 0x29;       /* Indicates MS/PC-DOS version 4.0 BPB  same 0x26 in version 4 */
        fr_DWORD( &(bpb[0x43]), pfmt->binary_volume_label);          /* same 0x27 in version 4 */
        pc_cppad( &(bpb[0x47]), (byte*)pfmt->text_volume_label, 11); /* same 0x2b in version 4 */
        pc_cppad( &(bpb[0x52]), &pfmt->file_sys_type[0],8);          /* same 0x36 in version 4 */
    }
    fr_WORD(&(bpb[0x01fe]), (word)0xaa55);                          /* Signature word */


    /* Calculate the number of clusters that the volume will contain. The value is calulated using pfmt->total_sectors,
    pfmt->numfats, pfmt->secpfat, pfmt->secreserved, pfmt->numroot, pfmt->secpalloc, and media_inopblock */
    volume_size_clusters = _get_volume_size_clusters(pfmt, media_inopblock);
    if (!volume_size_clusters)
        return(FALSE);          /* Defensive, won't happen */

    /* Verify the the format paramter block is correct for a volume of this size */
    if (!_check_fat_size(pfmt, volume_size_clusters))
    {
        rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
        goto errex;
    }

    /* Write the bpb to sector zero and zero as many reserved sectors as possible per write call.
       Using as few write calls as possible eliminates excess block replacements for flash.
       The buffer is currenlty zeroed except for the bpb in the first sector
    */
#if (INCLUDE_FAT32)
    if (pfmt->nibs_per_entry == 8 && buffer_size_sectors >=  8)
    {  /* If all FAT32 reserved sectors fit in the write buffer, initialize the buffer with their contents  */
       /*Make a copy the boot sector to sector 6 and initialize two copies of the info sector at sector offset 1 and 7 in the buffer */
       copybuff((bpb + (6*pfmt->bytes_per_sector)), bpb, pfmt->bytes_per_sector);
       _format_fsinfo_sector((bpb + (1*pfmt->bytes_per_sector)), volume_size_clusters, pfmt->bytes_per_sector);
       _format_fsinfo_sector((bpb + (7*pfmt->bytes_per_sector)), volume_size_clusters, pfmt->bytes_per_sector);
    }
#endif

    /* write the bpb and reserved sectors */
    blockno = pfmt->numhide;        /* pfmt->numhide is the block address of the start of the partition */
    n_left = pfmt->secreserved;
    while(n_left)
    {
        if (buffer_size_sectors > n_left)
            n_to_write = n_left;
        else
            n_to_write = buffer_size_sectors;
        if (!devio_write_format(pdr, (dword) blockno, bpb, n_to_write) )
            goto errex;
        n_left = n_left - n_to_write;
        blockno += n_to_write;
        if (n_left) /* If any left after pass 1 clear the buffer so all the rest are zero */
            rtfs_memset(bpb, 0, buffer_size_bytes);
    }
#if (INCLUDE_FAT32)
    if (pfmt->nibs_per_entry == 8 && buffer_size_sectors < 8)
    {   /* If the write buffer wasn't large enough to contain all FAT32 reserved sectors then individually write them here */
        /* Write copies of fsinfo to sector 1 and 7 */
        _format_fsinfo_sector(b_, volume_size_clusters, pfmt->bytes_per_sector); /* Initialize fsinfo */
        if (!devio_write_format(pdr,  pfmt->numhide+1, b_, 1) )
            goto errex;
        if (!devio_write_format(pdr,  pfmt->numhide+7, b_, 1) )
            goto errex;
        /* Read the bpb from sector zero and write it to sector 6 */
        if (!devio_read_format(pdr,  pfmt->numhide, b_, 1))
            goto errex;
        if (!devio_write_format(pdr,  pfmt->numhide+6, b_, 1) )
            goto errex;
    }
#endif

    /* Now clear the fats in as few write operations as possible */
    { dword fat_copy_offset;
      word i;
        fat_copy_offset = 0; /* Contains the base of the fat copy we are initializing */
        for (i = 0; i < pfmt->numfats; i++, fat_copy_offset += pfmt->secpfat)
        {
            rtfs_memset(b_, 0, buffer_size_bytes);
            /*   First 2 clusters (0 and 1) are reserved.
                 If FAT32, cluster 2 is allocated for the root
                   FAT12 = MEDIADESC,FF,FF
                   FAT16 = MEDIADESC,FF,FF,FF
                   FAT32 = MEDIADESC,FF,FF,FF , 0F,FF,FF,FF, FF,FF,FF,FF   */
            b_[0] = pfmt->mediadesc;
            b_[1] = (byte) 0xff;
            b_[2] = (byte) 0xff;
            /* The next 1 to 9 bytes depend on nibbles per entry */
            if (pfmt->nibs_per_entry == 4)
                b_[3] = (byte) 0xff;
#if (INCLUDE_FAT32)
            else if (pfmt->nibs_per_entry == 8)
            { int j;
                b_[3] = (byte) 0xff;
                b_[4] = (byte) 0x0f;
                for (j = 5; j < 12; j++)
                    b_[j] = (byte) 0xff;
            }
#endif
            /* Get the block number by adding reserved sectors plus the copy offset of the table plus the partition base in numhide */
            blockno = pfmt->secreserved + fat_copy_offset;
            blockno += pfmt->numhide;
            n_left = pfmt->secpfat;
            /* Now zero fill the rest */
            while(n_left)
            {
                if (buffer_size_sectors > n_left)
                    n_to_write = n_left;
                else
                    n_to_write = buffer_size_sectors;
                if (!devio_write_format(pdr, (dword) blockno, &(b_[0]), n_to_write) )
                {
                    goto errex;
                }
                n_left  -= n_to_write;
                blockno += n_to_write;
                rtfs_memset(&b_[0], 0, 16); /* only really need a maximum of 12 because that's all we write to */
            }
        }
    }
    /* Now write the root directory sectors   */
    rtfs_memset(&b_[0], 0, 16); /* clear first 16 bytes (clears the first few bytes which were affected */
    blockno = pfmt->secreserved + (pfmt->numfats * pfmt->secpfat);
    blockno += pfmt->numhide;
    if (pfmt->nibs_per_entry == 8)
        n_left = pfmt->secpalloc;                   /* Clear one cluster */
    else
        n_left = pfmt->numroot/media_inopblock;     /* Clear the reserved root directory area. media_inopblock can't be zero */
    while(n_left)
    {
        if (buffer_size_sectors > n_left)
            n_to_write = n_left;
        else
            n_to_write = buffer_size_sectors;
        if (!devio_write_format(pdr, blockno, b_, n_to_write) )
        {
            goto errex;
        }
        n_left  -= n_to_write;
        blockno += n_to_write;
    }
    ret_val = TRUE;
errex:      /* Not only errors return through here. Everything does. */
    return(ret_val);
}


#if (INCLUDE_FAT32)
/* Initialize the fat32 info sector */
static void _format_fsinfo_sector(byte *p, dword volume_size_clusters, int bytes_per_sector)
{ /* Initialize fsinfo */
    rtfs_memset(p, 0, bytes_per_sector);
    fr_DWORD( p, (dword) 0x41615252ul);
    fr_DWORD( p+0x01e4, (dword) FSINFOSIG);
    fr_DWORD( p+0x01e8, (dword)(volume_size_clusters-1));   /* Free clusters */
    fr_DWORD( p+0x01ec, (dword)0x00000003);                 /* First free cluster (2 is used for the root */
    fr_WORD(  p+0x01fe, (word)0xaa55);                      /* Signature */
}
#endif

/* Calculate the size of the area managed by the fat.   */
static dword _get_volume_size_clusters(FMTPARMS *pfmt,dword media_inopblock)
{
dword ldata_area, volume_size_clusters;
    volume_size_clusters = 0;
    ldata_area = pfmt->total_sectors;
    ldata_area -= pfmt->numfats * pfmt->secpfat;
    ldata_area -= pfmt->secreserved;
    /* Note: numroot must be an even multiple of INOPBLOCK will, be zero for FAT32
       Note: defensive. */
    if (pfmt->numroot && media_inopblock)
        ldata_area -= pfmt->numroot/media_inopblock;
    if (pfmt->secpalloc)
        volume_size_clusters =  ldata_area/pfmt->secpalloc;
    return(volume_size_clusters);
}


/* Check that the FAT tables and FAT type selection are correct */
static dword _clusters_per_fat_sector(FMTPARMS *pfmt)
{
dword clusters_per_fat_sector;

    if (pfmt->nibs_per_entry == 3)
    {   /* Calculate clusters in a FAT sector. rounding down. previous version rounded up, was a bug */
       clusters_per_fat_sector = (pfmt->bytes_per_sector*2)/3;
    }
    else if (pfmt->nibs_per_entry==4 || pfmt->nibs_per_entry==8)
        clusters_per_fat_sector = (pfmt->bytes_per_sector*2)/pfmt->nibs_per_entry;
    else
        clusters_per_fat_sector = 0;  /* Defensive won't happen */
    return(clusters_per_fat_sector);
}
/* Check that the FAT tables and FAT type selection are correct */
static BOOLEAN _check_fat_size(FMTPARMS *pfmt, dword volume_size_clusters)
{
dword clusters_per_fat_sector,fat_sectors_required;

    if ((pfmt->nibs_per_entry == 3 && volume_size_clusters > 4084L) || (pfmt->nibs_per_entry == 4 && volume_size_clusters > 65524L))
        return(FALSE);

    clusters_per_fat_sector =  _clusters_per_fat_sector(pfmt);
    if (!clusters_per_fat_sector)  /* Defensive won't happen */
        return(FALSE);

    fat_sectors_required = (volume_size_clusters + clusters_per_fat_sector-1)/clusters_per_fat_sector;

    if (fat_sectors_required > pfmt->secpfat)
        return(FALSE);
    else
        return(TRUE);
}

/* These are special versions of devio_write and read that are used by the format utility
   They are the same as their counterparts except they do not automount the
   drive or load the partition table or add partition offsets.
*/

static BOOLEAN devio_write_format(DDRIVE *pdr, dword blockno, byte * buf, dword n_to_write)
{
    if (RTFS_DEVI_io(pdr->driveno, blockno, buf, (word) n_to_write, FALSE))
        return(TRUE);
    else
    {
        rtfs_set_errno(PEIOERRORWRITE, __FILE__, __LINE__);/* devio_write_format: write failed */
        return(FALSE);
    }
}

#if (INCLUDE_FAT32)
static BOOLEAN devio_read_format(DDRIVE *pdr, dword blockno, byte * buf, dword n_to_read)
{
    if (RTFS_DEVI_io(pdr->driveno, blockno, buf, (word) n_to_read, TRUE))
        return(TRUE);
    else
    {
        rtfs_set_errno(PEIOERRORREAD, __FILE__, __LINE__);/* devio_read_format: write failed */
        return(FALSE);
    }
}
#endif
/*  static BOOLEAN _get_partition_information(DDRIVE *pdr, FMTPARMS *pfmt, PDEV_GEOMETRY pgeometry)

    If a partition table is found, appropriate format parameters are built from a combination of
    the values in the partition table and information in the disk geometry structure.

    If no partition table is found, appropriate format parameters are built from values in the disk geometry
    structure only.

    Retrieves the following fields:
     pfmt->numhide
     pfmt->total_sectors
     pfmt->numcyl
     pfmt->numhead
     pfmt->secptrk
*/
static BOOLEAN _get_partition_information(DDRIVE *pdr, FMTPARMS *pfmt, PDEV_GEOMETRY pgeometry)
{
int    partition_status;
struct mbr_entry_specification mbr_specs[RTFS_MAX_PARTITIONS];

    if (!pgeometry->dev_geometry_heads || !pgeometry->dev_geometry_secptrack)
        return(FALSE);   /* Defensive, won't happen */

    /* Read the partition table. If one is found the following fields will be set in the drive structure:
        pdr->drive_info.partition_base, pdr->drive_info.partition_size, pdr->drive_info.partition_type */
    partition_status = pc_read_partition_table(pdr, &mbr_specs[0]);
    if (partition_status >= pdr->partition_number)
    {
        if ((dword)pgeometry->dev_geometry_secptrack > pdr->drive_info.partition_size)
            return(FALSE);   /* We need at least one track */
        /* calculate a simulated numcyl value by dividing the partition size by the number of sectors per cylinder
           These values are either true vlues reported by driver or they are the the simulated values that are calculated
           for the size of the whole media, not just this partition */
#if (DEBUG_FORMATTING)
        DEBUG_PRINTF("pc_read_partition_table() for partion %d start sector=%d size==%d \n", (dword) pdr->partition_number, pdr->drive_info.partition_base, pdr->drive_info.partition_size);
#endif
        pfmt->total_sectors =    pdr->drive_info.partition_size;
        pfmt->numhead       =    (word)  pgeometry->dev_geometry_heads;
        pfmt->secptrk       =    (word)  pgeometry->dev_geometry_secptrack;
        pfmt->numcyl        =    (word) (pfmt->total_sectors / (pgeometry->dev_geometry_heads*pgeometry->dev_geometry_secptrack));
        /* Calculate numhide (the address of the bpb and total_sectors in the volume */
        pfmt->numhide       = pdr->drive_info.partition_base;
        pfmt->total_sectors = pdr->drive_info.partition_size;
        return(TRUE);
    }
    else if (partition_status == READ_PARTITION_NO_TABLE)
    {  /* No partition table, use values from the geometry structure hcn and lba values are known to be legal  */
        if (pdr->partition_number == 0)
        {
#if (DEBUG_FORMATTING)
            DEBUG_PRINTF("No partition table found using default geometry \n");
#endif
            pfmt->numhide =     0;
            pfmt->total_sectors = pgeometry->dev_geometry_lbas;
            pfmt->numcyl  =     (word) pgeometry->dev_geometry_cylinders;
            pfmt->numhead  =    (word) pgeometry->dev_geometry_heads;
            pfmt->secptrk  =    (word) pgeometry->dev_geometry_secptrack;
            return(TRUE);
        }
        else
        {
            rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
        }
    }
#if (DEBUG_FORMATTING)
    DEBUG_PRINTF("Error reading partition\n");
#endif
    /* errno was set by pc_read_partition_table to PEDEVICE or we set PEINVAL */
    return(FALSE);
}

/*
    static BOOLEAN _pc_calculate_fat_size_sectors(FMTPARMS *pfmt)

   Calculate the size of the fat in sectors.

    Sets the field: pfmt->secpfat

    These fields must be initialized already
        pfmt->nibs_per_entry
        pfmt->secpalloc
        pfmt->bytes_per_sector
        pfmt->total_sectors;
        pfmt->secreserved;
        pfmt->numroot
*/



static BOOLEAN _check_secpfat_condition(FMTPARMS fmt, dword SecFAT, dword SecRDE);
static BOOLEAN _pc_calculate_fat_size_sectors(FMTPARMS *pfmt)
{
   dword SecRDE, SecUser, SecFAT;

   if (!pfmt->nibs_per_entry || !pfmt->secpalloc || pfmt->bytes_per_sector < 512)
        return(FALSE); /* Defensive, won't happen */
   SecRDE = (32 * pfmt->numroot) + (pfmt->bytes_per_sector - 1);
   SecRDE /= pfmt->bytes_per_sector;

   SecUser = pfmt->total_sectors;
   SecUser -= (pfmt->secreserved + SecRDE);
   SecUser /= pfmt->secpalloc;

   SecFAT = (SecUser + 2);
   SecFAT /= ((2 * pfmt->bytes_per_sector) / pfmt->nibs_per_entry);
   SecFAT++;


   do
   {
      if (SecFAT == 0)	 /* PVO - Added code to break if the counter would underflow */
	   break;
      SecFAT--;
   } while( _check_secpfat_condition(*pfmt, SecFAT, SecRDE) );

   SecFAT++;


   if( !_check_secpfat_condition(*pfmt, SecFAT, SecRDE) )
   	return(FALSE);

   pfmt->secpfat = SecFAT;
   return(TRUE);
}

static BOOLEAN _check_secpfat_condition(FMTPARMS fmt, dword SecFAT, dword SecRDE)
{
   dword numCan, numMust;

   /* The number of entries which FAT can manage */
   numCan = 2 * fmt.bytes_per_sector * SecFAT;
   numCan /= fmt.nibs_per_entry;
   numCan -= 2;

   /* the number of entries which must manage"*/
   numMust = fmt.total_sectors + (fmt.secpalloc-1); // PVO - Round up to cluster boundary before dividing
   numMust -= (fmt.secreserved + fmt.numfats * SecFAT + SecRDE);
   numMust /= fmt.secpalloc;

   return( numCan >= numMust );
}


/*
static BOOLEAN _get_format_parms_by_size(dword nsectors, FMTPARMS *pfmt)

 Choose format parameters based on the number of sectors in the volume

    Consults the table below and finds the first record that is >= nsectors
    Initializes the following values:
        pfmt->secpalloc
        pfmt->numroot
        pfmt->nibs_per_entry
        pfmt->secreserved
        pfmt->numfats

    Returns FALSE if nsectors is too large for FAT16 (> 2 gigabyte) and FAT32 is not enabled
    Returns TRUE in all other cases
*/


struct fmtparmsbysize {
    dword to_size_sectors;
    byte sectors_per_cluster;
    unsigned short num_root_entries;
    byte nibs_per_entry;
};

/* Table of block size versus recomended  sectors_per_cluster, root directory_entries, and nibs_per_entry; (3,4,8) */

struct fmtparmsbysize parm_table[] = {
{   4086L, 1, 512, 3},        /* <=  4086 sectors (2 meg)  == FAT12 Cluster size   1 sector , 512 entries in root directory, FAT12 */
{   8192L, 2, 512, 3},        /* <=  8192 sectors (4 meg)   == FAT12 Cluster size  2 sector , 512 entries in root directory, FAT12 */
{  32680L, 2, 512, 4},        /* <=  16 meg  == FAT16 Cluster size  2 sectors, 512 entries in root directory, FAT16 */
{ 262144L, 4, 512, 4},        /* <= 128 meg  == FAT16 Cluster size  4 sectors, 512 entries in root directory, FAT16 */
#if (INCLUDE_FAT32) /* Volumes over 128 MB are formatted FAT32 if it is available */
{532480L,     1,   0,  8},    /* <= 260 meg  == FAT32 Cluster size  1 sectors, 0 entries in root directory */
{16777216L,   8,   0,  8},    /* <=   8 GB   == FAT32 Cluster size  8 sectors, 0 entries in root directory */
{33554432L,  16,   0,  8},    /* <=  16 GB   == FAT32 Cluster size 16 sectors, 0 entries in root directory */
{67108864L,  32,   0,  8},    /* <=  32 GB   == FAT32 Cluster size 32 sectors, 0 entries in root directory */
{0xffffffff, 64,   0,  8},    /* >   32 GB   == FAT32 Cluster size 64 sectors, 0 entries in root directory */
#endif
{524288L,  8, 512, 4},        /* <= 256 meg  == FAT16 Cluster size  8 sectors, 512 entries in root directory, FAT16 */
{104587L, 16, 512, 4},        /* <= 512 meg  == FAT16 Cluster size 16 sectors, 512 entries in root directory, FAT16 */
{2097152L,32, 512, 4},        /* <=   1  GB  == FAT16 Cluster size 32 sectors, 512 entries in root directory, FAT16 */
{4194144L,64, 512, 4},        /* <=   2  GB  == FAT16 Cluster size 64 sectors, 512 entries in root directory, FAT16 */
{0L,0, 0, 0},    /* Can't do it, FAT32 is disabled and the volume is too large for FAT16 */
};


static BOOLEAN _get_format_parms_by_size(dword nsectors, FMTPARMS *pfmt)
{
    int i;


	/* Use fixed parameters if they were specified */
	if (pfmt->fixed_secpalloc)
	{
    	pfmt->secpalloc 		= pfmt->fixed_secpalloc;
    	pfmt->numroot 			= pfmt->fixed_numroot;
    	pfmt->nibs_per_entry 	= pfmt->fixed_bits_per_cluster/4;
    	pfmt->numfats 			= pfmt->fixed_numfats;
        pfmt->secreserved       = pfmt->fixed_secreserved;
        return(TRUE);
	}
	/* Otherwise calculate parameters from a table lookup */
    for (i = 0; parm_table[i].to_size_sectors; i++)
    {
        if (parm_table[i].to_size_sectors >= nsectors)
        {
            pfmt->secpalloc         = parm_table[i].sectors_per_cluster;
            pfmt->numroot           = parm_table[i].num_root_entries;
            pfmt->nibs_per_entry    = parm_table[i].nibs_per_entry;
            if (pfmt->nibs_per_entry == 8)  /* The table was derived with number reserved sectors and FAT copies fixed to these values */
                pfmt->secreserved = 32;
            else
                pfmt->secreserved =  1;
            pfmt->numfats =  2;
            return(TRUE);
        }
    }
    /* No match, must be too large for FAT16 */
    rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
    return(FALSE);
}


static BOOLEAN _scrub_volume(DDRIVE *pdr, FMTPARMS *pfmt, byte *pbuffer, dword buffer_size_sectors, dword buffer_size_bytes)
{
dword blockno, n_left, n_to_write;

#if (INCLUDE_NAND_DRIVER)   /* Use device erase call if it is availabe */
	if (pdr->pmedia_info->device_erase)
	{
		if (pdr->pmedia_info->device_erase(pdr->pmedia_info->devhandle, (void *) pdr, pfmt->numhide, pfmt->total_sectors))
			return(TRUE);
		else
			return(FALSE);
	}
#endif
    rtfs_memset(pbuffer, 0, buffer_size_bytes);
    blockno = pfmt->numhide;
    n_left  = pfmt->total_sectors;
    /* zero fill the volume */
    while(n_left)
    {
        if (buffer_size_sectors > n_left)
            n_to_write = n_left;
        else
            n_to_write = buffer_size_sectors;
        if (!devio_write_format(pdr, blockno, pbuffer, n_to_write) )
			return(FALSE);
        n_left  -= n_to_write;
        blockno += n_to_write;
    }
    return(TRUE);
}


#endif /* Exclude from build if read only */
