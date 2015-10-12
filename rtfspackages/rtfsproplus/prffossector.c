/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* PRFPOS.C - Routines to facilitate raw and DMA based file IO

    The following routines are included:
        BOOLEAN pc_efilio_fpos_sector(int fd, BOOLEAN isreadfp, BOOLEAN raw, dword *psectorno, dword *psectorcount)
*/

#include "rtfs.h"


static BOOLEAN _pc_check_fpos_open_mode(PC_FILE *pefile)
{
    RTFS_ARGSUSED_PVOID((void *) pefile);
#if (INCLUDE_ASYNCRONOUS_API)
    if (_pc_check_if_async(pefile))
    {
        rtfs_set_errno(PEEINPROGRESS, __FILE__, __LINE__);
        return(FALSE);
    }
#endif
#if (INCLUDE_CIRCULAR_FILES)
    if ( (pefile->fc.plus.allocation_policy & (PCE_CIRCULAR_FILE|PCE_CIRCULAR_BUFFER)) &&
              (!pefile->fc.plus.psibling || !pefile->fc.plus.psibling->pobj))
    {
        rtfs_set_errno(PEEFIOILLEGALFD, __FILE__, __LINE__);
        return(FALSE);
    }
#endif
    return(TRUE);
}

#if (INCLUDE_CIRCULAR_FILES)
static BOOLEAN _pc_cfilio_fpos_sector(PC_FILE *pwriterefile, BOOLEAN isreadfp, BOOLEAN raw, dword *psectorno, dword *psectorcount);
#endif
static BOOLEAN _pc_efilio_fpos_sector(PC_FILE *pefile, BOOLEAN isreadfp, BOOLEAN raw, dword *psectorno, dword *psectorcount);

BOOLEAN pc_efilio_fpos_sector(int fd, BOOLEAN isreadfp, BOOLEAN raw, dword *psectorno, dword *psectorcount)
{
BOOLEAN ret_val;
PC_FILE *pefile;
    *psectorcount = 0;
    *psectorno = 0;
    ret_val = FALSE;
    pefile = pc_fd2file(fd, 0);
    if (pefile)
    {
        if (_pc_check_fpos_open_mode(pefile)) /* _pc_check_fpos_open_mode sets errno */
        {
#if (INCLUDE_CIRCULAR_FILES)
            if (pefile->fc.plus.allocation_policy & (PCE_CIRCULAR_FILE|PCE_CIRCULAR_BUFFER))
                ret_val = _pc_cfilio_fpos_sector(pefile, isreadfp, raw, psectorno, psectorcount);
            else
#endif
                ret_val = _pc_efilio_fpos_sector(pefile, isreadfp, raw, psectorno, psectorcount);
        }
        release_drive_mount(pefile->pobj->pdrive->driveno);
    }
    return(ret_val);
}

static BOOLEAN _pc_fpos_prolog(PC_FILE *pefile)
{
    if (!pefile->pobj || !pefile->fc.plus.ffinode)
        return(FALSE);
    _pc_efiliocom_sync_current_fragment(pefile, pefile->fc.plus.ffinode);
    return(TRUE);
}

static BOOLEAN _pc_efilio_fpos_sector(PC_FILE *pefile, BOOLEAN isreadfp, BOOLEAN raw, dword *psectorno, dword *psectorcount)
{
BOOLEAN ret_val;
llword max_byte_count, bytes_left_in_region, byte_offset_in_region;

    ret_val = FALSE;
    if (!_pc_fpos_prolog(pefile))
        goto ex_it;
    /* Plain linear file */
    if (isreadfp)
	{
#if (INCLUDE_EXFATORFAT64)
	    if (ISEXFATORFAT64(pefile->pobj->pdrive))
			max_byte_count.val64 = pefile->fc.plus.ffinode->fsizeu.fsize64 - pefile->fc.plus.file_pointer.val64;
		else
#endif
			max_byte_count.val32 = pefile->fc.plus.ffinode->fsizeu.fsize - pefile->fc.plus.file_pointer.val32;
	}
    else
	{
#if (INCLUDE_EXFATORFAT64)
	    if (ISEXFATORFAT64(pefile->pobj->pdrive))
			max_byte_count.val64 = 0xffffffffffffffff;
        else
#endif
         max_byte_count.val32 = LARGEST_DWORD;
	}
    ret_val = TRUE;
    /* Get the number of blocks left in the current fragment beyond
       the file pointer.. If if there are preallocated clusters
       there may be more than the file size. For all files this is
       not ok for reads, but max_byte_count will bound it for reads
       For writes it is okay to return preallocated blocks, but not
       beyond the wrap pouint for circular files, but max_byte_count
       bounds us to the wrap point for circular files  */
    if (pefile->fc.plus.ffinode->e.x->pfirst_fragment) /* If NULL, no contents */
    {
#if (INCLUDE_EXFATORFAT64)
	    if (ISEXFATORFAT64(pefile->pobj->pdrive))
		{
			byte_offset_in_region.val64 = pefile->fc.plus.file_pointer.val64 - pefile->fc.plus.region_byte_base.val64;
			bytes_left_in_region.val64 = pc_fragment_size_64(pefile->pobj->pdrive, pefile->fc.plus.pcurrent_fragment);
			bytes_left_in_region.val64 -= byte_offset_in_region.val64;
			 *psectorcount = (dword)(bytes_left_in_region.val64>>pefile->pobj->pdrive->drive_info.log2_bytespsec);
			 *psectorno = pefile->fc.plus.region_block_base+((dword)(byte_offset_in_region.val64 >> pefile->pobj->pdrive->drive_info.log2_bytespsec));

		}
        else
#endif
		{
			byte_offset_in_region.val32 = pefile->fc.plus.file_pointer.val32 - pefile->fc.plus.region_byte_base.val32;
			bytes_left_in_region.val32 = pc_fragment_size_32(pefile->pobj->pdrive, pefile->fc.plus.pcurrent_fragment);
			bytes_left_in_region.val32 -= byte_offset_in_region.val32;
			if (bytes_left_in_region.val32 > max_byte_count.val32)
				bytes_left_in_region = max_byte_count;
			*psectorcount = bytes_left_in_region.val32>>pefile->pobj->pdrive->drive_info.log2_bytespsec;
			*psectorno = pefile->fc.plus.region_block_base+(byte_offset_in_region.val32 >> pefile->pobj->pdrive->drive_info.log2_bytespsec);


		}

        if (raw)
            *psectorno += pefile->pobj->pdrive->drive_info.partition_base;
    }
ex_it:
    return(ret_val);
}

#if (INCLUDE_CIRCULAR_FILES)
#if (INCLUDE_EXFATORFAT64)
static BOOLEAN _exfat_cfilio_fpos_sector(PC_FILE *pwriterefile, BOOLEAN isreadfp, BOOLEAN raw, dword *psectorno, dword *psectorcount);
#endif
dword _pc_cfilio_get_max_read_count(PC_FILE *pefile, dword count);
dword _pc_cfilio_get_max_write_count(PC_FILE *pwriterefile, dword count);
static BOOLEAN _pc_cfilio_fpos_sector(PC_FILE *pwriterefile, BOOLEAN isreadfp, BOOLEAN raw, dword *psectorno, dword *psectorcount)
{
BOOLEAN ret_val;
PC_FILE *preaderefile, *pefile;
dword logical_bytes_left, bytes_left_in_region, byte_offset_in_region;

    ret_val = FALSE;
    preaderefile = pwriterefile->fc.plus.psibling;
    if (!preaderefile)
        goto ex_it;
#if (INCLUDE_EXFATORFAT64)
	if (ISEXFATORFAT64(pwriterefile->pobj->pdrive))
		return _exfat_cfilio_fpos_sector(pwriterefile, isreadfp, raw, psectorno, psectorcount);
#endif
    if (!_pc_fpos_prolog(pwriterefile))
        goto ex_it;
    if (!_pc_fpos_prolog(preaderefile))
        goto ex_it;
    /* Get information from logical file pointers file sizes etc */
    if (isreadfp)
        logical_bytes_left = _pc_cfilio_get_max_read_count(pwriterefile, LARGEST_DWORD);
    else
        logical_bytes_left = _pc_cfilio_get_max_write_count(pwriterefile, LARGEST_DWORD);
    /* Get information from the current fragment */
    if (isreadfp)
        pefile = preaderefile;
    else
        pefile = pwriterefile;
	byte_offset_in_region = pefile->fc.plus.file_pointer.val32 - pefile->fc.plus.region_byte_base.val32;
	*psectorno = pefile->fc.plus.region_block_base+(byte_offset_in_region >> pefile->pobj->pdrive->drive_info.log2_bytespsec);

	if (raw)
        *psectorno += pefile->pobj->pdrive->drive_info.partition_base;
    /* Get number of bytes in the fragment */
    bytes_left_in_region = pc_fragment_size_32(pefile->pobj->pdrive, pefile->fc.plus.pcurrent_fragment);
    bytes_left_in_region -= byte_offset_in_region;

    /* Take the lesser of the two */
    if (logical_bytes_left < bytes_left_in_region)
        bytes_left_in_region = logical_bytes_left;
    *psectorcount = bytes_left_in_region>>pefile->pobj->pdrive->drive_info.log2_bytespsec;
    ret_val = TRUE;
ex_it:
    return(ret_val);
}
#if (INCLUDE_EXFATORFAT64)
static BOOLEAN _exfat_cfilio_fpos_sector(PC_FILE *pwriterefile, BOOLEAN isreadfp, BOOLEAN raw, dword *psectorno, dword *psectorcount)
{
BOOLEAN ret_val;
PC_FILE *preaderefile, *pefile;
ddword logical_bytes_left, bytes_left_in_region, byte_offset_in_region;

    ret_val = FALSE;
    preaderefile = pwriterefile->fc.plus.psibling;
    if (!preaderefile)
        goto ex_it;
    if (!_pc_fpos_prolog(pwriterefile))
        goto ex_it;
    if (!_pc_fpos_prolog(preaderefile))
        goto ex_it;
    /* Get information from logical file pointers file sizes etc */
    if (isreadfp)
        logical_bytes_left = _pc_cfilio_get_max_read_count(pwriterefile, LARGEST_DWORD);
    else
        logical_bytes_left = _pc_cfilio_get_max_write_count(pwriterefile, LARGEST_DWORD);
    /* Get information from the current fragment */
    if (isreadfp)
        pefile = preaderefile;
    else
        pefile = pwriterefile;
	byte_offset_in_region = pefile->fc.plus.file_pointer.val64 - pefile->fc.plus.region_byte_base.val64;
	*psectorno = pefile->fc.plus.region_block_base+(dword)(byte_offset_in_region >> pefile->pobj->pdrive->drive_info.log2_bytespsec);

    if (raw)
        *psectorno += pefile->pobj->pdrive->drive_info.partition_base;
    /* Get number of bytes in the fragment */
    bytes_left_in_region = pc_fragment_size_64(pefile->pobj->pdrive, pefile->fc.plus.pcurrent_fragment);
    bytes_left_in_region -= byte_offset_in_region;

    /* Take the lesser of the two */
    if (logical_bytes_left < bytes_left_in_region)
        bytes_left_in_region = logical_bytes_left;
    *psectorcount = (dword)(bytes_left_in_region>>pefile->pobj->pdrive->drive_info.log2_bytespsec);
    ret_val = TRUE;
ex_it:
    return(ret_val);
}
#endif

#endif /*  (INCLUDE_CIRCULAR_FILES) */
