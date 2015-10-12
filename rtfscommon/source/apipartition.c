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

/***************************************************************************
    PC_PARTITION_MEDIA -  Write partition table

    BOOLEAN pc_partition_media(byte *path, struct mbr_specification *pmbrspec)

Description

    Given a drive ID and one or more mbr specifications, write the information in the
    mbr specification to the media.

    If the underlying device driver provides dynamic partitioning instructions then pmbrspec
    is ignored and is provided by the device driver.

    Typically one specification structure is provided. This is used to initialize the primary boot record.

    struct mbr_specification {
    int    device_mbr_count      - Only used in the first specification. This must contain 1 if there is only one
                                   partition table. If extended partitons required this must be 1 plus the
                                   number of EBR (extended boot record) specifications to follow
    dword  mbr_sector_location   - Location of this primary or extended boot record. (0 for the primary)
                                 - An array of four structures specifying the filed for this record
    struct mbr_entry_specification entry_specifications[4];
    where (struct mbr_entry_specification) contains the following fields:
        dword partition_start;
        dword partition_size;
        byte  partition_type;
        byte  partition_boot;

    If extended partitions are desired then one additional mbr_specification structure is required per virtual
    volume in the extended partition.

    If the user is providing the specifications they must be provided in a contiguous array pointed to by pmbrspec.

    If the device driver is dynamically providing the specifications it will be called once for each specification it
    needs, passing the index number as an argument.


Returns
    Returns TRUE if it was able to perform the operation otherwise FALSE.

    errno is set to one of the following
    0               - No error
    PEINVALIDDRIVEID- Drive component is invalid
    PEINVALIDPARMS  - Inconsistent or missing parameters
    PEIOERRORWRITE  - Error writing partition table
    An ERTFS system error
****************************************************************************/


static BOOLEAN partition_provided(DDRIVE *pdr, DEV_GEOMETRY *pgeometry,struct mbr_specification *pmbrspec);

BOOLEAN _pc_get_media_parms(DDRIVE *pdr, PDEV_GEOMETRY pgeometry);

#if (INCLUDE_CS_UNICODE)
BOOLEAN pc_partition_media_cs(byte *path , struct mbr_specification *pmbrspec, int use_charset)
#else
BOOLEAN pc_partition_media(byte *path, struct mbr_specification *pmbrspec)
#endif
{
DEV_GEOMETRY geometry;
DDRIVE *pdr;
void *devhandle;
BOOLEAN ret_val;
    CHECK_MEM(BOOLEAN, 0)
    rtfs_clear_errno(); /* pc_partition_media: clear error status */
    if (!path || !pmbrspec)    /* Make sure it s a valid drive number */
    {
        rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
        return(FALSE);
    }
    pdr = check_drive_by_name(path, CS_CHARSET_ARGS);
    if (!pdr)
        return(FALSE);
	devhandle = pdr->pmedia_info->devhandle;
    /* Get media size, we ignore the size values but use heads and sectors per track. _pc_get_media_parms returns valid
       values (either real or synthesized) */
    if (!_pc_get_media_parms(pdr, &geometry))
        ret_val = FALSE;
	else
    	ret_val = partition_provided(pdr,&geometry,pmbrspec);

    rtfs_release_media_and_buffers(pdr->driveno);
	if (devhandle) /* re-mount the media, re-read partition tables */
		pc_rtfs_media_remount(devhandle);
	return(ret_val);
}

static BOOLEAN pc_write_mbr_spec(DDRIVE *pdr,  PDEV_GEOMETRY pgeometry, struct mbr_specification *pspec);

/* static BOOLEAN partition_provided(DDRIVE *pdr, DEV_GEOMETRY *pgeometry,struct mbr_specification *pmbrspec)

   Format and write master boot record and any extended boot records.

   The mbr specifications were provided by the user
*/

static BOOLEAN partition_provided(DDRIVE *pdr, DEV_GEOMETRY *pgeometry,struct mbr_specification *pmbrspec)
{
    if (!pmbrspec)
    {
        rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
        return(FALSE);
    }
    if (!pmbrspec->device_mbr_count) /*  device_mbr_count == 0 is a paramater option to clear the MBR */
        return(pc_write_mbr_spec(pdr, pgeometry, 0));
    else
    {   /* primary mbr specified */
        if (!pc_write_mbr_spec(pdr,  pgeometry, pmbrspec))
            return(FALSE);
        /* Check if more mbrs to follow, if this is true then the caller is partitioning the
           drive with extended partitions. Each of these additional mbrs must reside inside an extended partition */
        if (pmbrspec->device_mbr_count > 1)
        {
        int    extended_mbr_count, mbr_index;
            extended_mbr_count = pmbrspec->device_mbr_count-1;
            pmbrspec++;
            for (mbr_index = 1; mbr_index <=  extended_mbr_count; mbr_index++, pmbrspec++)
            {   /* write extended mbrs */
                if (!pc_write_mbr_spec(pdr,  pgeometry, pmbrspec))
                    return(FALSE);
            }
        }
    }
    return(TRUE);
}


static word pack_cylinder_field(word cyl, word sec);
static word _lba_to_cylinder(dword lba_val, PDEV_GEOMETRY pgeometry);
static void mbr_entry_specification_to_mbr(byte *ptable,PDEV_GEOMETRY pgeometry, struct mbr_specification *pspec);

static BOOLEAN pc_write_mbr_spec(DDRIVE *pdr,  PDEV_GEOMETRY pgeometry, struct mbr_specification *pspec)
{
BLKBUFF *pscratch;
BLKBUFF bbuf_scratch;   /* Used by pc_sys_sector build a buffer structure with core from the driver level */
BOOLEAN ret_val;
dword mbr_sector_location;
    /* Grab some working space - pass the drive parameter so it allocates a sector buffer  */
    pscratch = pc_sys_sector(pdr,&bbuf_scratch);
    if (!pscratch)
        return(FALSE);   /* pc_scratch_blk set errno */
    ret_val = FALSE;
    rtfs_memset(pscratch->data, 0, pgeometry->bytespsector);

    /* Now copy the partition information into the buffer  */
    /* The info starts at buf[1be]           */
    if (pgeometry && pspec) /* if they are zero just clear the partition table */
    {
        mbr_entry_specification_to_mbr((pscratch->data + 0x1be), pgeometry, pspec);
        mbr_sector_location = pspec->mbr_sector_location;
#if (INCLUDE_WINDEV)
		if (pdr->driveno==('P'-'A')) /* P: is the host drive */
		{
   			rtfs_print_one_string((byte *)"INCLUDE_WINDEV is enabled", PRFLG_NL);
			rtfs_print_one_string((byte*)"Use private boot signature, call hackwin7 to make the volume accessable on windows", PRFLG_NL);
			fr_WORD((pscratch->data + 510), 0xAA66);
		}
#endif
    }
    else
        mbr_sector_location = 0;    /* Clearing the MBR. mbr_specification is not valid */
    /* write to pspec->mbr_sector_location   */
    if (raw_devio_xfer(pdr, mbr_sector_location, pscratch->data, 1, TRUE, FALSE))
        ret_val = TRUE;
    else
    {
        if (!get_errno())
            rtfs_set_errno(PEIOERRORWRITE, __FILE__, __LINE__);  /* pc_partition_media: write failed */
    }
    pc_free_sys_sector(pscratch);
    return(ret_val);
}

static word pack_cylinder_field(word cyl, word sec)
{
word utemp, utemp2;
    utemp = (word)((cyl & 0xff) << 8);   /* Low 8 bit to hi bite */
    utemp2 = (word)((cyl >> 2) & 0xc0);  /* Hi 2 bits to bits 6 + 7 */
    utemp |= utemp2;
    utemp |= sec;
    return(utemp);
}

/* Given a 32 bit sector address, divide it by (heads * secptrack) to convert it to the cylinder number.
   If the result is > 1023, truncate it to 1023.
   The value will be correct if "lba_val" is an exact multiple of (heads * secptrack), and <= 1023, otherwise it won't */
static word _lba_to_cylinder(dword lba_val, PDEV_GEOMETRY pgeometry)
{
dword ltemp;

    if (pgeometry->dev_geometry_heads == 0  || pgeometry->dev_geometry_secptrack == 0) /* Won't happen */
        ltemp = 1023;
    else
    {
        ltemp = lba_val;
        ltemp /= pgeometry->dev_geometry_heads;
        ltemp /= pgeometry->dev_geometry_secptrack;
        if (ltemp > 1023)
            ltemp = 1023;
    }
    return((word) (ltemp & 0xffff) );
}

/* Convert an lba value to hcn.
    cyl  is between 1 and 123
    sec  is the same as in the geomerty stucture (1 <= sec <= 63)_
    head is between the same as in the geomerty stucture (0 <= head <= 255)_

    Note: sec and head will always be correct. cyl will be truncated to 1023 if sector is too
    large to represent as (1023 * 63 * 255)
*/
static void _lba_to_chs(dword sector, PDEV_GEOMETRY pgeometry, word *cyl, word *sec, byte *head)
{
    *cyl = _lba_to_cylinder(sector, pgeometry);
    *sec  = (word) (pgeometry->dev_geometry_secptrack & 0x3f);
    *head = (byte) (pgeometry->dev_geometry_heads & 0xff);

}


static void mbr_entry_specification_to_mbr(byte *ptable,PDEV_GEOMETRY pgeometry, struct mbr_specification *pspec)
{
int i;
word cyl, sec;
byte head;
byte *pentry;

    /* The table has four 16 bit partition table entries followed by the signature
       Each entry in the table has the followin fields
          byte  Name   width
          0     boot   1
          1     s_head 1
          2     s_cyl  2
          4     p_typ  1
          5     e_head 1
          6     e_cyl; 2
          8     r_sec  4
          12    p_size 4
   */

    pentry = ptable;
    for(i = 0; i < 4; i++)
    {
        if (pspec->entry_specifications[i].partition_size)
        {
            *pentry              =  pspec->entry_specifications[i].partition_boot;
            /* calculate valid values for starting cyl, sec. head */
            _lba_to_chs(pspec->entry_specifications[i].partition_start, pgeometry, &cyl, &sec, &head);
            *(pentry+1)              =  head;
            fr_WORD((byte *)(pentry+2), pack_cylinder_field(cyl, sec));
            *(pentry+4) = pspec->entry_specifications[i].partition_type;
            /* calculate partition end and valid values for ending cyl, sec. head */
            { dword partition_end;
                 partition_end = pspec->entry_specifications[i].partition_start + pspec->entry_specifications[i].partition_size - 1;
                 _lba_to_chs(partition_end, pgeometry, &cyl, &sec, &head);
            }
            *(pentry+5) = head;
            fr_WORD(pentry+6, pack_cylinder_field(cyl, sec));
            fr_DWORD(pentry+8, pspec->entry_specifications[i].partition_start);
            fr_DWORD(pentry+12, pspec->entry_specifications[i].partition_size);
        }
        pentry += 16;
    }
    /* Now for the signature   */
    fr_WORD(ptable+64, 0xAA55);
}



#endif /* Exclude from build if read only */
