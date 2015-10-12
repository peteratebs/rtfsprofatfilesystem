/*              EBS - RTFS (Real Time File Manager)
* FAILSSAFEV3 - Checked
* Copyright EBS Inc. 1987-2005
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* PRFSNVIO.C - ERTFS-PRO FailSafe journal file routines */
/* Contains source code to implement journal file accesses read, write,
   open and create.
    The following routines are exported:

    failsafe_reopen_nv_buffer()
    failsafe_create_nv_buffer()
    failsafe_nv_buffer_io()

    Note: These routines create and maintains a journal file on the
    disk. If you have a closed system and you prefer to use system non
    volatile ram instead please modify these for functions to use that
    resource instead of a disk based file.

    BOOLEAN failsafe_reopen_nv_buffer(FAILSAFECONTEXT *pfscntxt)
    BOOLEAN failsafe_create_nv_buffer(FAILSAFECONTEXT *pfscntxt)
    BOOLEAN failsafe_nv_buffer_io(FAILSAFECONTEXT *pfscntxt, dword block_no, dword nblocks, byte *pblock,BOOLEAN reading)

*/

#include "rtfs.h"
#if (INCLUDE_FAILSAFE_CODE)

/* Routines to open and close a volume based failsafe file */
static BOOLEAN create_failsafe_in_freespace(DDRIVE *pdrive, FAILSAFECONTEXT *pfscntxt);
static BOOLEAN reopen_failsafe_in_freespace(DDRIVE *pdrive, FAILSAFECONTEXT *pfscntxt, byte *work_buffer);
void fs_zero_reserved(FAILSAFECONTEXT *pfscntxt);
static dword fs_check_master_for_size(DDRIVE *pdrive, dword first_block, byte *work_buffer, BOOLEAN raw_mode);
static dword fs_retrieve_failsafe_location(DDRIVE *pdrive, byte *pbuffer);
static dword fs_get_set_cluster(DDRIVE *pdrive, byte *pbuffer, dword in_value, BOOLEAN reading);
static BOOLEAN fs_get_set_buffer(DDRIVE *pdrive, byte *pbuffer, int which_fat, BOOLEAN reading);


/*
* failsafe_reopen_nv_buffer - Open the failsafe buffer.
*
* Summary:
*   BOOLEAN failsafe_reopen_nv_buffer(FAILSAFECONTEXT *pfscntxt)
*
* Description:
*
* This routine must check for the existence of a failsafe buffer on the
* current disk or in system non volatile ram and return TRUE if one exists,
* or FALSE if one does not.
* It may use the field nv_buffer_handle in the structure pointed to
* by pfscntxt to store a handle for later access by failsafe_nv_buffer_io.
*
*/

BOOLEAN failsafe_reopen_nv_buffer(FAILSAFECONTEXT *pfscntxt, dword raw_start_sector, dword file_size_sectors)
{
BOOLEAN ret_val;
DDRIVE *pdrive;
byte *work_buffer;
BLKBUFF * scratch;
BLKBUFF bbuf_scratch;   /* Used by pc_sys_sector build a buffer structure with core from the driver level */

    pdrive = pfscntxt->pdrive;

    scratch = pc_sys_sector(pdrive, &bbuf_scratch);    /* Use a scratch sector buffer */
    if (!scratch)
        return(FALSE);
    work_buffer = scratch->data;

    pfscntxt->nv_cluster_handle = 0;    /* Set only if journaling to freespace */
    if (raw_start_sector)
    { /* Raw sector and sector count were passed in, Set handle, make sure reserved region is clear,
         set raw mode to access sectors from the satrt of the device */
        fs_zero_reserved(pfscntxt);
        pfscntxt->nv_raw_mode = TRUE;
        pfscntxt->nv_buffer_handle = raw_start_sector;
        pfscntxt->journal_file_size = file_size_sectors;
        /* Read the start sector, make sure it is a valid header and then check
           stored size in the header against the size request */
        if (pfscntxt->journal_file_size == fs_check_master_for_size(pdrive, raw_start_sector, work_buffer, TRUE))
        {
            ret_val = TRUE;
        }
        else
        {
            pfscntxt->journal_file_size = pfscntxt->nv_buffer_handle = 0;
            ret_val = FALSE;
        }
    }
    else
    {
        pfscntxt->nv_raw_mode = FALSE;
        /* re-open the file, or query reserved blocks for a failsafe file */
        ret_val =  reopen_failsafe_in_freespace(pdrive,pfscntxt,work_buffer);
        /* zero reserved segments. this is read-only.. failsafe_create_nv_buffer()
           will be called if we are journaling */
        fs_zero_reserved(pfscntxt);
    }
    pc_free_sys_sector(scratch);
    return(ret_val);
}

/*
* failsafe_create_nv_buffer - Create the failsafe buffer.
*
* Summary:
*   BOOLEAN failsafe_create_nv_buffer(FAILSAFECONTEXT *pfscntxt)
*
* Description:
*
* This routine must create a failsafe buffer on the current disk or
* in system NV ram and return TRUE if successful, FALSE if it is
* unsuccessful.
* It may use the field nv_buffer_handle in the sructure pointed to
* by pfscntxt to store a handle for later access by failsafe_nv_buffer_io.
*
* The failsafe buffer must contain space for:
*   (pfscntxt->num_remap_blocks + pfscntxt->num_index_blocks) sector sized blocks.
*
* The source code in prfailsf.c implements the failsafe buffer in a hidden
* file named \FAILSAFE on the disk.
* The reference implementation is convenient and should be adequate for
* most applications. If it is more desirable to implement the failsafe
* buffer in flash or NVRAM then these functions should be modified to access
* that media instead.
*
*/

BOOLEAN failsafe_create_nv_buffer(FAILSAFECONTEXT *pfscntxt, dword raw_start_sector, dword file_size_sectors)
{
BOOLEAN ret_val;
DDRIVE *pdrive;

    pdrive = pfscntxt->pdrive;
    pfscntxt->nv_cluster_handle = 0;    /* Set only if journaling to freespace */
    if (raw_start_sector)
    { /* Raw sector and sector count passed in set handle and make sure reserved region is clear
         Set raw mode to access sectors from the start of the device */
        pfscntxt->nv_raw_mode = TRUE;
        fs_zero_reserved(pfscntxt);
        pfscntxt->nv_buffer_handle = raw_start_sector;
        pfscntxt->journal_file_size = file_size_sectors;
        ret_val = TRUE;
    }
    else
    {
        pfscntxt->nv_raw_mode = FALSE;
        /* Find freespace blocks for the Journal */
        ret_val = create_failsafe_in_freespace(pdrive, pfscntxt);
        /* Claim reserved clusters so we don't hand them out. This does not change the free cluster count which
           we will put in the Failsafe header. */
#if (INCLUDE_RTFS_FREEMANAGER)
        if (ret_val)
        {
            free_manager_claim_clusters(pdrive, pfscntxt->nv_reserved_fragment.start_location,
                                pfscntxt->nv_reserved_fragment.end_location - pfscntxt->nv_reserved_fragment.start_location + 1);
        }
#endif
    }
    return(ret_val);
}

/*
* failsafe_nv_buffer_io - Read or write blocks to the failsafe buffer.
*
* Summary:
*  BOOLEAN  failsafe_nv_buffer_io(
*           FAILSAFECONTEXT *pfscntxt,
*           dword block_no,
*           dword nblocks, byte
*           *pblock,BOOLEAN reading)
*
* Description:
*
* This routine must reado or write one or more blocks to the block at offset
* block_no in the failsafe buffer on disk or in system NV ram.
*
* returns TRUE if successful, FALSE if it is unsuccessful.
*
* If using on-disk failsafe files as opposed to NVRAM there is no need
* to modify the routine
*
*/

BOOLEAN  failsafe_nv_buffer_io(FAILSAFECONTEXT *pfscntxt, dword block_no, dword nblocks, byte *pblock, BOOLEAN reading)
{
DDRIVE *pdrive;
BOOLEAN ret_val;
    if (!pfscntxt->nv_buffer_handle || (pfscntxt->journal_file_size <= block_no))
    {
        if (reading)
            rtfs_set_errno(PEFSIOERRORREADJOURNAL, __FILE__, __LINE__);
        else
            rtfs_set_errno(PEFSIOERRORWRITEJOURNAL, __FILE__, __LINE__);
        return(FALSE);
    }
    pdrive = pfscntxt->pdrive;
    ret_val = FALSE;
    if (pdrive)
    {
        if (!raw_devio_xfer(pdrive, pfscntxt->nv_buffer_handle+block_no,
            pblock, nblocks, pfscntxt->nv_raw_mode, reading))
            ret_val = FALSE;
        else
            ret_val = TRUE;
    }
    return(ret_val);
}

/* Read the First block of the FAT and update the Journal cluster
   if it is not already set.
   If more than one FAT use the first block in the second FAT
   If only one FAT use the first block in the first FAT
*/
BOOLEAN failsafe_nv_buffer_mark(FAILSAFECONTEXT *pfscntxt, byte *pbuffer)
{
DDRIVE *pdrive;
    /* Journal is in a fixed location no need to mark its location in the FAT */
    if (!pfscntxt->nv_cluster_handle)
        return(TRUE);
    pdrive = pfscntxt->pdrive;
    /* Read the block */
    if (!fs_get_set_buffer(pdrive, pbuffer, 1, TRUE))
        return(FALSE);
    /* Set the handle if it is not already set */
    if (fs_get_set_cluster(pdrive, pbuffer, pfscntxt->nv_cluster_handle, TRUE) != pfscntxt->nv_cluster_handle)
    {
        /* Set the handle */
        fs_get_set_cluster(pdrive, pbuffer, pfscntxt->nv_cluster_handle, FALSE);
        /* Write the block */
        if (!fs_get_set_buffer(pdrive, pbuffer, 1, FALSE))
            return(FALSE);
    }
    return(TRUE);
}

/* Read the First block of the first copy of the FAT and
   write it to the first block of the second FAT
   If there is only one FAT, update the value at cluster 1 to be fff, ffff  or ffffffff
*/

BOOLEAN failsafe_nv_buffer_clear(FAILSAFECONTEXT *pfscntxt, byte *pbuffer)
{
DDRIVE *pdrive;
    /* Journal is in a fixed location no need to mark its location in the FAT */
    if (!pfscntxt->nv_cluster_handle)
        return(TRUE);
    pdrive = pfscntxt->pdrive;
    /* Read the first block of FAT copy 0 */
	if (!fs_get_set_buffer(pdrive, pbuffer, 0, TRUE))
		return(FALSE);
	if (pdrive->drive_info.numfats>1)
	{
		/* Write the block to FAT copy 1, or overwrite the first block of FAT copy 0 */
		if (!fs_get_set_buffer(pdrive, pbuffer, 1, FALSE))
			return(FALSE);
	}
	else
	{
		/* Clear it */
        fs_get_set_cluster(pdrive, pbuffer, 0xffffffff, FALSE);
		if (!fs_get_set_buffer(pdrive, pbuffer, 0, FALSE))
			return(FALSE);
	}
    return(TRUE);
}


static BOOLEAN fs_set_journal_placement(DDRIVE *pdrive, FAILSAFECONTEXT *pfscntxt, dword required_size_clusters);

/* reopen_failsafe_in_freespace()
*
*    Open an existing Failsafe file and return TRUE if succesful.
*
*    Find free clusters where the file should be
*    Read the Failsafe header determine if it is a valid failsafe file.
* Sets the following fields
*        pfscntxt->journal_file_size
*        pfscntxt->nv_buffer_handle
*        pfscntxt->nv_reserved_fragment
*/

static BOOLEAN reopen_failsafe_in_freespace(DDRIVE *pdrive, FAILSAFECONTEXT *pfscntxt, byte *work_buffer)
{
    /* Get the handle from the location stored in the FAT */
    pfscntxt->nv_buffer_handle = 0;
    pfscntxt->journal_file_size = 0;
    pfscntxt->nv_cluster_handle = 0;
    /* Requires a second FAT copy to operate if only one FAT copy disable Failsafe and proceed */
#if (INCLUDE_EXFAT)  /* FAT64 does not have a FAT, does not store the Journal file loacation in the FAT */
	if (ISEXFAT(pdrive))
	{ /* Allow single FAT operation for exFAT because that's how it's done.. */
		;
	}
	else
#endif
    if (pdrive->drive_info.numfats == 1)
    {
        pfscntxt->nv_disable_failsafe = TRUE;
        return(FALSE);
    }
    pfscntxt->nv_cluster_handle = fs_retrieve_failsafe_location(pdrive,work_buffer);
    if (!pfscntxt->nv_cluster_handle)
        return(FALSE);
    pfscntxt->nv_buffer_handle = pc_cl2sector(pdrive,pfscntxt->nv_cluster_handle);
    if (pfscntxt->nv_buffer_handle)
    {
        pfscntxt->journal_file_size = fs_check_master_for_size(pdrive, pfscntxt->nv_buffer_handle, work_buffer, FALSE);
    }
    if (!pfscntxt->journal_file_size)
    {
        pfscntxt->nv_buffer_handle = 0;
        pfscntxt->nv_cluster_handle = 0;
        fs_zero_reserved(pfscntxt);
        return(FALSE);
    }
    return(TRUE);
}

/*
* create_failsafe_in_freespace - low level create of failsafe journal file
*
* Summary:
*  BOOLEAN create_failsafe_in_freespace(DDRIVE *pdrive, FAILSAFECONTEXT *pfscntxt)
*
* Description:
*
* This finds enough contiguous free blocks to hold the journal file.
* If reduces the file if the recommended number of blocks are not available
* If at least fs_api_cb_min_journal_size(pdrive) clusters are not available it fails
*
* Sets the following fields
*        pfscntxt->journal_file_size
*        pfscntxt->nv_buffer_handle
*        pfscntxt->nv_reserved_fragment
*
* returns TRUE if successful, FALSE if it is unsuccessful.
*
*/
static dword fs_find_free_blocks_for_journal(DDRIVE *pdrive, FAILSAFECONTEXT *pfscntxt);

static BOOLEAN create_failsafe_in_freespace(DDRIVE *pdrive, FAILSAFECONTEXT *pfscntxt)
{
    BOOLEAN ret_val;

    pfscntxt->nv_buffer_handle = 0;
    fs_zero_reserved(pfscntxt);
    if (!pdrive)
        return(FALSE);


#if (INCLUDE_EXFAT)	  /* FAT64 does not have a FAT, does not store the Journal file loacation in the FAT */
	if (ISEXFAT(pdrive))
	{ /* Allow single FAT operation for exFAT because that's how it's done.. */
		;
	}
	else
#endif
    /* Requires a second FAT copy to operate */
    /* Requires a second FAT copy to operate if only one FAT copy disable Failsafe and proceed */
    if (pdrive->drive_info.numfats == 1)
    {
        pfscntxt->nv_disable_failsafe = TRUE;
        return(FALSE);
    }
    /* Find contiguous blocks and place the start block of the file in nv_handle
        place the start cluster in nv_cluster_handle reserve the clusters */
    pfscntxt->journal_file_size = fs_find_free_blocks_for_journal(pdrive, pfscntxt);
    if (pfscntxt->journal_file_size)
    {
        ret_val = TRUE;
    }
    else
    {
        fs_zero_reserved(pfscntxt);
        ret_val = FALSE;
    }
    return(ret_val);
}
/* Called only by create_failsafe_in_freespace() */
static dword fs_find_free_blocks_for_journal(DDRIVE *pdrive, FAILSAFECONTEXT *pfscntxt)
{
    dword  desired_size_clusters,desired_size_sectors;
   /* Create the journal file */
    desired_size_sectors   = pfscntxt->journal_file_size;
    desired_size_clusters = (desired_size_sectors+(dword)pdrive->drive_info.secpalloc-1) >> pdrive->drive_info.log2_secpalloc;
    /* Create a journal file with at least "required_size_bytes" contiguous bytes and
       place the start block of the file in nv_handle */
    pfscntxt->nv_buffer_handle = 0;
    pfscntxt->nv_cluster_handle = 0;
    /* Find free clusters to journal to and update  pfscntxt->nv_reserved_fragment  */
    if (fs_set_journal_placement(pdrive, pfscntxt, desired_size_clusters))
    {
        dword available_size_sectors, available_size_clusters;
        /* Retrieve the placement made by fs_set_journal_placement from pfscntxt->nv_reserved_fragment */
        /* Retrieve the size in sectors */
        available_size_clusters = pfscntxt->nv_reserved_fragment.end_location - pfscntxt->nv_reserved_fragment.start_location + 1;
        available_size_sectors   = available_size_clusters << pdrive->drive_info.log2_secpalloc;
        pfscntxt->nv_cluster_handle = pfscntxt->nv_reserved_fragment.start_location;
        pfscntxt->nv_buffer_handle = pc_cl2sector(pdrive,pfscntxt->nv_cluster_handle);
        return(available_size_sectors);
    }
    return(0);
}


static dword fs_check_master_for_size(DDRIVE *pdrive, dword first_block, byte *work_buffer, BOOLEAN raw_mode)
{
dword ret_val;
dword *pdw;
FSMASTERINFO master_info;

    ret_val = 0;
    /* Read the first block in the LARGE FILE REGION */
    if (raw_devio_xfer(pdrive, first_block, work_buffer, 1, raw_mode, TRUE))
    {
        /* Check the block for the Failsafe signature */
        pdw = (dword *) work_buffer;
        fs_mem_master_info(pdw,&master_info);
        if (master_info.master_type == FS_MASTER_TYPE_VALID)
            ret_val = master_info.master_file_size;
    }
    return(ret_val);
}

static  BOOLEAN fs_set_journal_placement(DDRIVE *pdrive, FAILSAFECONTEXT *pfscntxt, dword required_size_clusters)
{
    int is_error;
    dword first_cluster, contig_clusters, current_cluster_size_request;

    /* Try to find free space large enough to hold requested size ..
       If that isn't available keep dividing by two until we find space */
    current_cluster_size_request = required_size_clusters;
    first_cluster = 0;
    while (current_cluster_size_request)
    {
#if (INCLUDE_EXFATORFAT64)
	if (ISEXFATORFAT64(pdrive))
	{
		first_cluster =
			exfatop_find_contiguous_free_clusters(pdrive, 2, pdrive->drive_info.maxfindex, current_cluster_size_request, current_cluster_size_request, &contig_clusters, &is_error);
	}
	else
#endif
        first_cluster = fatop_find_contiguous_free_clusters(pdrive, 2, pdrive->drive_info.maxfindex, current_cluster_size_request, current_cluster_size_request, &contig_clusters, &is_error, ALLOC_CLUSTERS_PACKED);
        if (is_error)
            return(FALSE);
        if (first_cluster != 0 && contig_clusters == current_cluster_size_request)
            break;
		/* On the first pass check if resizing is supported, abort if not */
        if (current_cluster_size_request == required_size_clusters && fs_api_cb_check_fail_on_journal_resize(pfscntxt->pdrive->driveno))
		{  /* If journal resize is not allowed break out and exit */
        	current_cluster_size_request = 0;
			break;
		}
        /* Not enough clusters, subtract one and try again */
        current_cluster_size_request -= 1;
    }
    /* Check if we are completely out */
    if (!current_cluster_size_request)
    {   /* Ask the callback routine if we should continue without journaling */
        if (fs_api_cb_disable_on_full(pdrive))
            pfscntxt->nv_disable_failsafe = TRUE;
        return(FALSE);
    }
    /* Mark this cluster range reserved in the failsafe context so we don't allocate them */
    pfscntxt->nv_reserved_fragment.start_location = first_cluster;
    pfscntxt->nv_reserved_fragment.end_location   = first_cluster + current_cluster_size_request -1;
    pfscntxt->nv_reserved_fragment.pnext = 0;
    return(TRUE);
}



static dword fs_retrieve_failsafe_location(DDRIVE *pdrive, byte *pbuffer)
{
dword nv_cluster_handle;
    /* Read the block */
    if (!fs_get_set_buffer(pdrive, pbuffer, 1,TRUE))
        return(0);
    nv_cluster_handle = fs_get_set_cluster(pdrive, pbuffer, 0, TRUE);
    return(nv_cluster_handle);
}


/* Get or set the value at of cluster index 1 in the buffer */
static dword fs_get_set_cluster(DDRIVE *pdrive, byte *pbuffer, dword in_value, BOOLEAN reading)
{
dword cluster_value = 0;

#if (INCLUDE_EXFAT)   /* FAT64 does not have a FAT, does not store the Journal file loacation in the FAT */
	if (ISEXFAT(pdrive))
	{ /* Store exfat cluster value in the top dword of the highest page in the fat */

       if (reading)
            cluster_value = to_DWORD (pbuffer+(pdrive->drive_info.bytespsector-4));       /* Get */
        else
		{ /* Reverse the sense from FAT because the high word is by default zero */
			if (in_value==0xffffffff)
				in_value=0;
            fr_DWORD ( pbuffer+(pdrive->drive_info.bytespsector-4), in_value);
	   }
	}
	else
#endif
    if (pdrive->drive_info.fasize == 8)
    {
        if (reading)
            cluster_value = to_DWORD (pbuffer+4);       /* Get */
        else
            fr_DWORD ( pbuffer+4, in_value);
    }
    else if (pdrive->drive_info.fasize == 4)
    {
        word w_cluster_value;
        if (reading)
        {
            w_cluster_value = to_WORD (pbuffer+2);       /* Get */
            cluster_value = (dword) w_cluster_value;
            cluster_value &= 0xffff;
        }
        else
        {
            w_cluster_value = (word) (in_value & 0xffff);
            fr_WORD ( pbuffer+2, w_cluster_value);  /* Put */
        }
    }
    else if (pdrive->drive_info.fasize == 3)
    {
        /* FAT12.. high nibble of [1], contains low nibble of value
                   [2], contains hi 2 nibbles of value */
        byte c, c1;
        dword lobyte,hibyte;
        if (reading)
        { /* Get */
            c = *(pbuffer+1); lobyte = (dword) c;
            lobyte >>= 4; lobyte &= 0xff;
            c = *(pbuffer+2); hibyte = (dword) c;
            hibyte &= 0xff;   hibyte <<= 4;
            hibyte |= lobyte;
            cluster_value = hibyte | lobyte;
        }
        else
        { /* Put */
            hibyte = in_value;
            hibyte >>= 4;
            hibyte &= 0xff;
            c = (byte) hibyte;
            *(pbuffer+2) = c;

            lobyte = in_value;
            lobyte <<= 4;
            lobyte &= 0xff;
            c = (byte) lobyte; c &= 0xf0;
            c1 = *(pbuffer+1); c1 &= 0x0f;
            c |= c1;
            *(pbuffer+1) = c;
        }
    }
    if (reading)
    {
        if (cluster_value < 2 || cluster_value > pdrive->drive_info.maxfindex)
            cluster_value = 0;
    }
    return(cluster_value);
}

static BOOLEAN fs_get_set_buffer(DDRIVE *pdrive, byte *pbuffer, int which_fat, BOOLEAN reading)
{
dword  fat_mark_block;
    /* Access the second FAT if it is requested */
#if (INCLUDE_EXFAT)	  /* FAT64 does not have a FAT, does not store the Journal file loacation in the FAT */
	if (ISEXFAT(pdrive))
	{/* Store exfat cluster value in the top dword of the highest page in the fat */
		fat_mark_block =  pdrive->drive_info.fatblock+pdrive->drive_info.secpfat-1;
	}
	else
#endif
    if (which_fat == 1)
        fat_mark_block =  pdrive->drive_info.fatblock+pdrive->drive_info.secpfat;
    else
        fat_mark_block =  pdrive->drive_info.fatblock;

    if (!raw_devio_xfer(pdrive, fat_mark_block, pbuffer, 1, FALSE, reading))
    {
        if (reading)
            rtfs_set_errno(PEIOERRORREADFAT, __FILE__, __LINE__);
        else
            rtfs_set_errno(PEIOERRORWRITEFAT, __FILE__, __LINE__);
       return(FALSE);
    }
    return(TRUE);
}
#endif /* (INCLUDE_FAILSAFE_CODE) */
