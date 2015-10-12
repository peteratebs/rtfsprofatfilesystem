/*               EBS - RTFS (Real Time File Manager)
* FAILSSAFEV3 -
* Copyright EBS Inc. 1987-2005
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* PRFSJOURNAL.C - ERTFS-PRO FailSafe internal journaling routines */

#include "rtfs.h"
#if (INCLUDE_FAILSAFE_CODE)

typedef struct fsmapreq {
BOOLEAN is_mapped;
dword volume_blockno;
dword n_blocks;
dword journal_blockno;
dword n_replacement_blocks;
FSBLOCKMAP *adjacent_pbm;
} FSMAPREQ;

static FAILSAFECONTEXT *fs_failsafe_enable(DDRIVE *pdrive, BOOLEAN clear_errors);
static BOOLEAN fs_failsafe_start_journaling(FAILSAFECONTEXT *pfscntxt, dword start_sector, dword size_in_sectors);
static BOOLEAN fs_flush_internal(FAILSAFECONTEXT *pfscntxt,BOOLEAN synch_fats);
static BOOLEAN fs_devio_write(FAILSAFECONTEXT *pfscntxt, DDRIVE *pdrive, dword blockno, dword nblocks, byte *data, BOOLEAN is_fat);
static BOOLEAN fs_devio_read(FAILSAFECONTEXT *pfscntxt, DDRIVE *pdrive, dword blockno, dword nblocks, byte *data, BOOLEAN is_fat);
static BOOLEAN fs_check_mapped_read(FAILSAFECONTEXT *pfscntxt, FSMAPREQ *preq);
static FSBLOCKMAP *fs_alloc_blockmap(FSJOURNAL *pjournal);
static BOOLEAN fs_journal_alloc_replacement_blocks(FAILSAFECONTEXT *pfscntxt, FSMAPREQ *preq);
static BOOLEAN fs_flush_current_session(FAILSAFECONTEXT *pfscntxt);
static BOOLEAN fs_file_open_new_frame(FAILSAFECONTEXT *pfscntxt, BOOLEAN close_current);
static BOOLEAN fs_flush_current_frame(FAILSAFECONTEXT *pfscntxt, int ioframetype);
static void fs_invalidate_flushed_blocks(FSJOURNAL *pjournal, dword first_block, dword n_blocks);
static FSBLOCKMAP *fs_insert_map_record(FSBLOCKMAP *p_list, FSBLOCKMAP *p_this);
static BOOLEAN fs_scan_map_cache(struct fsblockmap *pbm, FSMAPREQ *preq);
void fs_show_journal_session(FAILSAFECONTEXT *pfscntxt);
static BOOLEAN fs_config_check(FAILSAFECONTEXT *pfscntxt);

/* Catch endless loops (if TRUE a cataclismic error) */
#define GUARD_ENDLESS 32768 /* Arbitrarilly large for our purposes */
static BOOLEAN journal_check_endless_loop(dword *loop_guard)
{
	if (*loop_guard == 0)
	{
        FS_DEBUG_SHOWNL("journal - endless loop")
        ERTFS_ASSERT(rtfs_debug_zero())
		return(TRUE);
	}
	*loop_guard = *loop_guard - 1;
	return(FALSE);
}

static struct rtfs_failsafe_cfg failsafe_cfg;


static BOOLEAN fsblock_devio_read(DDRIVE *pdrive, dword blockno, byte * buf);
static BOOLEAN fsblock_devio_write(BLKBUFF *pblk);
static BOOLEAN fsblock_devio_xfer(DDRIVE *pdrive, dword blockno, byte * buf, dword n_to_xfer, BOOLEAN reading);
static BOOLEAN fsfat_devio_write(DDRIVE *pdrive, dword fat_blockno, dword nblocks, byte *fat_data, int fatnumber);
static BOOLEAN fsfat_devio_read(DDRIVE *pdrive, dword fat_blockno, dword nblocks, byte *fat_data);
static BOOLEAN fs_recover_free_clusters(DDRIVE *pdrive, dword required_clusters);
static BOOLEAN fs_add_free_region(DDRIVE *pdr, dword cluster, dword n_contig);
static BOOLEAN fs_failsafe_dskopen(DDRIVE *pdrive);
static BOOLEAN fs_dynamic_mount_volume_check(DDRIVE *pdr);
static void fs_claim_buffers(DDRIVE *pdr);
static BOOLEAN fs_allocate_volume_buffers(struct rtfs_volume_resource_reply *pvolume_config_block, dword sector_size_bytes);
static void fs_free_disk_configuration(DDRIVE *pdr);
static BOOLEAN fs_dynamic_configure_volume(DDRIVE *pdr, struct rtfs_volume_resource_reply *preply);
static dword free_fallback_find_contiguous_free_clusters(DDRIVE *pdr, dword startpt, dword endpt, dword min_clusters, dword max_clusters, dword *p_contig, int *is_error);


BOOLEAN pc_failsafe_init(void)
{
    failsafe_cfg.block_devio_read = fsblock_devio_read;
    failsafe_cfg.block_devio_write = fsblock_devio_write;
    failsafe_cfg.block_devio_xfer = fsblock_devio_xfer;
    failsafe_cfg.fat_devio_read = fsfat_devio_read;
    failsafe_cfg.fat_devio_write = fsfat_devio_write;
    failsafe_cfg.fs_recover_free_clusters = fs_recover_free_clusters;
    failsafe_cfg.fs_failsafe_autocommit = fs_failsafe_autocommit;
    failsafe_cfg.fs_add_free_region = fs_add_free_region;
    failsafe_cfg.fs_failsafe_dskopen = fs_failsafe_dskopen;
    failsafe_cfg.fs_dynamic_mount_volume_check = fs_dynamic_mount_volume_check;
    failsafe_cfg.fs_claim_buffers = fs_claim_buffers;
    failsafe_cfg.fs_allocate_volume_buffers = fs_allocate_volume_buffers;
    failsafe_cfg.fs_free_disk_configuration = fs_free_disk_configuration;
    failsafe_cfg.fs_dynamic_configure_volume = fs_dynamic_configure_volume;
    failsafe_cfg.free_fallback_find_contiguous_free_clusters = free_fallback_find_contiguous_free_clusters;
	prtfs_cfg->pfailsafe = &failsafe_cfg;
	return(TRUE);
}


static BOOLEAN fs_allocate_volume_buffers(struct rtfs_volume_resource_reply *pvolume_config_block, dword sector_size_bytes)
{

    pvolume_config_block->fsfailsafe_context_memory        	= 0;
    pvolume_config_block->fsjournal_blockmap_memory        	= 0;
    pvolume_config_block->failsafe_buffer_base        		= 0;
    pvolume_config_block->failsafe_indexbuffer_base    		= 0;


	if (pvolume_config_block->fsjournal_n_blockmaps)
	{
		if (!pvolume_config_block->fsindex_buffer_size_sectors)
		{  /* Index buffer size in sectors must be specified */
    		rtfs_set_errno(PEDYNAMIC, __FILE__, __LINE__);
			return(FALSE);
		}
    	/* If not in shared buffer mode then restor buffer must be specified */
    	if (!prtfs_cfg->rtfs_exclusive_semaphore && !pvolume_config_block->fsrestore_buffer_size_sectors)
		{
    		rtfs_set_errno(PEDYNAMIC, __FILE__, __LINE__);
			return(FALSE);
		}
    	pvolume_config_block->fsfailsafe_context_memory	= pc_rtfs_malloc(sizeof(FAILSAFECONTEXT));
    	if (!pvolume_config_block->fsfailsafe_context_memory)
			goto cleanup_and_fail;
    	pvolume_config_block->fsjournal_blockmap_memory	= pc_rtfs_malloc(pvolume_config_block->fsjournal_n_blockmaps*sizeof(FSBLOCKMAP));
    	if (!pvolume_config_block->fsjournal_blockmap_memory)
			goto cleanup_and_fail;
    	pvolume_config_block->failsafe_buffer_base     	= pc_rtfs_iomalloc(pvolume_config_block->fsrestore_buffer_size_sectors * sector_size_bytes, &pvolume_config_block->failsafe_buffer_memory);
    	if (!pvolume_config_block->failsafe_buffer_base)
			goto cleanup_and_fail;
    	pvolume_config_block->failsafe_indexbuffer_base	= pc_rtfs_iomalloc(pvolume_config_block->fsindex_buffer_size_sectors * sector_size_bytes, &pvolume_config_block->failsafe_indexbuffer_memory);
    	if (!pvolume_config_block->failsafe_indexbuffer_base)
			goto cleanup_and_fail;
	}
	return(TRUE);
cleanup_and_fail:
    if (pvolume_config_block->fsfailsafe_context_memory)
        rtfs_port_free(pvolume_config_block->fsfailsafe_context_memory);
    if (pvolume_config_block->fsjournal_blockmap_memory)
        rtfs_port_free(pvolume_config_block->fsjournal_blockmap_memory);
    if (pvolume_config_block->failsafe_buffer_base)
        rtfs_port_free(pvolume_config_block->failsafe_buffer_base);
    if (pvolume_config_block->failsafe_indexbuffer_base)
        rtfs_port_free(pvolume_config_block->failsafe_indexbuffer_base);
	rtfs_set_errno(PEDYNAMIC, __FILE__, __LINE__);
	return(FALSE);
}
static void fs_free_disk_configuration(DDRIVE *pdr)
{
    if (pdr->mount_parms.fsfailsafe_context_memory) rtfs_port_free(pdr->mount_parms.fsfailsafe_context_memory);
    if (pdr->mount_parms.fsjournal_blockmap_memory) rtfs_port_free(pdr->mount_parms.fsjournal_blockmap_memory);
    if (pdr->mount_parms.failsafe_buffer_base)      rtfs_port_free(pdr->mount_parms.failsafe_buffer_base);
    if (pdr->mount_parms.failsafe_indexbuffer_base) rtfs_port_free(pdr->mount_parms.failsafe_indexbuffer_base);
	/* July 2012 - clear pdr->du.user_failsafe_context;
	    pdr->du.user_failsafe_context shadows pdr->mount_parms.fsfailsafe_context_memory
	    so it must be cleared. Usage dependant caused problems for failsafe test suite,
	    not a problem for normal operation */
	pdr->du.user_failsafe_context=0;
	pdr->mount_parms.fsfailsafe_context_memory=0;
    pdr->mount_parms.fsjournal_blockmap_memory=0;
    pdr->mount_parms.failsafe_buffer_base=0;
    pdr->mount_parms.failsafe_indexbuffer_base=0;
}

#if (INCLUDE_RTFS_FREEMANAGER)
/* The free manager is included but there aren't enough region structures to hold the free space.
   Scan the FATs for free space but exclude space reserved by Failsafe - this code is
   unnecessary and conditionally excluded if Failsafe is not included
   It is only called if Failsafe is enabled for the drive */
static dword failsafe_first_not_reserved(FAILSAFECONTEXT *pfscntxt, dword free_start);
static dword failsafe_first_reserved(FAILSAFECONTEXT *pfscntxt, dword free_start, dword range_end);

static dword free_fallback_find_contiguous_free_clusters(DDRIVE *pdr, dword startpt, dword endpt, dword min_clusters, dword max_clusters, dword *p_contig, int *is_error)
{
dword new_start_range, start_range;
FAILSAFECONTEXT *pfscntxt;

    if (!min_clusters)
        min_clusters = 1;

    start_range = startpt;

    /* Find the initial start point by excluding start point if it is reserved */
    pfscntxt= (FAILSAFECONTEXT *) pdr->drive_state.failsafe_context;
    new_start_range = failsafe_first_not_reserved(pfscntxt, start_range);
    if (new_start_range && new_start_range > start_range)
        start_range = new_start_range;
    /* Now move up the range searching unreserved sections */

    while (start_range <= endpt)
    {
    dword end_range,new_end_range;
        end_range = endpt;
        new_end_range = failsafe_first_reserved(pfscntxt, start_range, end_range);
        if (new_end_range && new_end_range < end_range)
            end_range = new_end_range;
        if ((start_range+min_clusters-1) <= end_range)
        { /* Search the FAT table for free clusters in this range */
        dword cluster_found;
            *is_error = 0;
            cluster_found = _fatop_find_contiguous_free_clusters(pdr, start_range, end_range, min_clusters, max_clusters, p_contig, is_error);
            if (*is_error)
                return (0);
            if (cluster_found)
                return(cluster_found);
        }
        /* Find the first unreserved cluster beyond where we just looked and continue */
        start_range = end_range + 1;
        new_start_range = failsafe_first_not_reserved(pfscntxt, start_range);
        if (new_start_range && new_start_range > start_range)
            start_range = new_start_range;
    }
    rtfs_set_errno(PENOSPC, __FILE__, __LINE__);
    return (0);
}

static dword find_first_free(REGION_FRAGMENT *pflist, dword range_start);

/* Find the first range within pfrag_range >= min_clusters that are not reserved */
static dword failsafe_first_not_reserved(FAILSAFECONTEXT *pfscntxt, dword free_start)
{
FSJOURNAL *pjournal;
    pjournal = CONTEXT_TO_JOURNAL(pfscntxt);
    /* Exclude clusters that were freed but can't be used because they are not synchronized */
    free_start = find_first_free(pjournal->open_free_fragments, free_start);
    free_start = find_first_free(pjournal->flushed_free_fragments, free_start);
    free_start = find_first_free(pjournal->restoring_free_fragments, free_start);
    /* Exclude clusters that are free but are reserved for the journal file contents */
    free_start = find_first_free(&(pfscntxt->nv_reserved_fragment), free_start);
    return(free_start);
}

static dword exclude_first_occupied(REGION_FRAGMENT *pflist, dword range_start,dword range_end);
/* Find the first range within pfrag_range >= min_clusters that are not reserved */
static dword failsafe_first_reserved(FAILSAFECONTEXT *pfscntxt, dword free_start, dword range_end)
{
FSJOURNAL *pjournal;
    pjournal = CONTEXT_TO_JOURNAL(pfscntxt);
    /* Exclude clusters that were freed but can't be used because they are not synchronized */
    range_end = exclude_first_occupied(pjournal->open_free_fragments, free_start,range_end);
    range_end = exclude_first_occupied(pjournal->flushed_free_fragments, free_start,range_end);
    range_end = exclude_first_occupied(pjournal->restoring_free_fragments, free_start,range_end);
    /* Exclude clusters that are free but are reserved for the journal file contents */
    range_end = exclude_first_occupied(&(pfscntxt->nv_reserved_fragment), free_start,range_end);
    return(range_end);
}

/* Find the first reserved cluster beyond range_start if it is less than the current range
   return it as the new range end  */
static dword exclude_first_occupied(REGION_FRAGMENT *pflist, dword range_start,dword range_end)
{
    while (pflist)
    {
        if (pflist->start_location > range_end)
            break;
        if (pflist->start_location >= range_start)
        { /* Take it if it shrinks the region */
            if (pflist->start_location <= range_end)
                range_end = pflist->start_location-1;
            break;
        }
        pflist = pflist->pnext;
    }
    return(range_end);
}
/* If range_start is occupied in the list, return the first un-reserved cluster */
static dword find_first_free(REGION_FRAGMENT *pflist, dword range_start)
{
    while (pflist)
    {
        if (pflist->start_location > range_start)
            break;
        if (pflist->end_location >= range_start)
            range_start = pflist->end_location+1; /* and continue but the next element should be pflist->start_location > range_start */
       pflist = pflist->pnext;
    }
    return(range_start);
}
#endif /* (INCLUDE_RTFS_FREEMANAGER) */

/* fs_api_enable - Initiate a failsafe session
*
*   Summary:
*    BOOLEAN fs_api_enable(byte *drive_name, BOOLEAN clear_journal)
*
*
* Description:
*
* This routine clears Failsafe errors and activates Failsafe for the
* specified drive.
* If the volume is already mounted Journaling begins imediately.
* If the volume is not already mounted and clear_journal is TRUE then
* auto-restore processing is not executed when the volume is mounted
* and Journaling begins imediately.
*
*
* This routine should be used to activate Failsafe under the following
* conditions:
*
* 1. If you are manually controlling Failsafe and not utilizing the
* automatic enable feature provided by the fs_api_cb_enable()
* callback function.
*
* 2. If you are utilizing the automatic enable feature provided by the
* fs_api_cb_enable() but a Failsafe error has occured that has caused
* Failsafe to disable itself. When this occurs you must re-enable Failsafe
* from the applications layer to acknowledge and clear the error.
*
*
* Before fs_api_enable() enters Failsafe mode it calls the ERTFS internal
* disk flush routine to insure that all buffers are flushed before
* initiating Journaling.
*
* Once activated, Failsafe operates using the policies and buffering
* capabilities provided to it by pc_diskio_configure()
*
*
* Inputs:
*    drive_name - Null terminated drive designator, for example "A:"
*
*
* Returns:
* TRUE          - Success
* FALSE         - Error
*
* If it returns FALSE errno will be set to one of the following:
*
* PEFSNOINIT       - Failsafe not initialized by pc_diskio_configure
* PEFSBUSY         - Failsafe already enabled. If you wish to restart
*                    Failsafe, first call fs_api_disable.
* PEINVALIDDRIVEID - Invalid drive
*
* An ERTFS system error
*
*/

BOOLEAN fs_api_enable(byte *drivename, BOOLEAN clear_journal)
{
DDRIVE *pdrive;
FAILSAFECONTEXT *pfscntxt;
BOOLEAN ret_val;

    pdrive = check_drive_by_name( drivename, CS_CHARSET_NOT_UNICODE);
    if (!pdrive)
    	return(FALSE);

    ret_val = FALSE;
    if (pdrive->pmedia_info->is_write_protect)
    {
#if (FS_DEBUG)
        FS_DEBUG_SHOWNL("fs_api_enable - Device is write protected")
#endif
        rtfs_set_errno(PEDEVICEWRITEPROTECTED, __FILE__, __LINE__);
    }
    else if (pdrive->drive_state.failsafe_context)
    {
#if (FS_DEBUG)
        FS_DEBUG_SHOWNL("fs_api_enable - already enabled")
#endif
        rtfs_set_errno(PEFSBUSY, __FILE__, __LINE__); /* Already enabled */
    }
    else
    {
		/* Clear any temporary remount parms they will be made stale */
		pdrive->drive_info.drive_operating_flags &= ~(DRVOP_FS_START_ON_MOUNT|DRVOP_FS_DISABLE_ON_MOUNT|DRVOP_FS_CLEAR_ON_MOUNT);
        /* Get the context block, clear errors */
        pfscntxt = fs_failsafe_enable(pdrive, TRUE);
        if (!pfscntxt)
        {
#if (FS_DEBUG)
            FS_DEBUG_SHOWNL("fs_api_enable - fs_failsafe_enable failed")
#endif
            rtfs_set_errno(PENOINIT, __FILE__, __LINE__);
        }
        else
        {
        /* If the drive is open now, flush all buffers and open the
           failsafe file */
            if (pdrive->drive_info.drive_operating_flags & DRVOP_MOUNT_VALID)
            {
                /* Flush anything dirty */
                if (!_pc_diskflush(pdrive))
                    ret_val = FALSE;
                else  /* open the journal */
                    ret_val = fs_failsafe_start_journaling(pfscntxt, 0, 0);
            }
            else
            {
                /* Rtfs preserves these bits and passes them to failsafe on the next mount call after a manual enable */
                if (clear_journal)
                    pdrive->drive_info.drive_operating_flags |= DRVOP_FS_CLEAR_ON_MOUNT;
                /* Force failsafe init next time */
                pdrive->drive_info.drive_operating_flags |= DRVOP_FS_START_ON_MOUNT;
                ret_val = TRUE;
            }
        }
    }
    rtfs_release_media_and_buffers(pdrive->driveno);
    return(ret_val);
}

/* fs_api_disable - Gracefully close or abort FailSafe Jurnalling
*
*   Summary:
*       BOOLEAN fs_api_disable(byte *drive_name, BOOLEAN abort)
*
* Description:
*
* Call this routine to disable Failsafe Journaling.
*
*
* If parameter "abort" is FALSE Journal file is flushed and the
* FAT volume is synchronized before Journaling is terminated.
*
* fs_api_disable will return failure if the parameter "abort" is
* FALSE and Failsafe is currently performing an asynchronous commit
* operation.
* If the parameter "abort" is TRUE and Failsafe is currently performing an
* asynchronous commit operations, the asynchronous commit will be aborted.
*
*
* If parameter abort is TRUE then the current Journaling session is
* aborted and un-commited operations are lost.
*
*
* Returns:
*  TRUE  - If Success
*  FALSE - Failure
*
* If FALSE is returned errno will be set to one of the following.
*
*   PEINVALIDDRIVEID        - Drive argument invalid
*   PEEINPROGRESS			- Cant perform operation because ASYNC
*                             commit operation in progress
*   An ERTFS system error
*
*/

BOOLEAN fs_api_disable(byte *drive_name, BOOLEAN abort)
{
BOOLEAN ret_val;
int driveno;
DDRIVE *pdr;
FAILSAFECONTEXT *pfscntxt;

    if (abort) /* unilaterally suspend */
    {
	    pdr = check_drive_by_name( drive_name, CS_CHARSET_NOT_UNICODE);
		if (pdr)
		{
        	driveno = pdr->driveno;
        	/* Clear any temporary remount parms they will be made stale */
        	pdr->drive_info.drive_operating_flags &= ~(DRVOP_FS_START_ON_MOUNT|DRVOP_FS_DISABLE_ON_MOUNT|DRVOP_FS_CLEAR_ON_MOUNT);
        	fs_failsafe_disable(pdr, 0);
        	pdr->drive_info.drive_operating_flags |= DRVOP_FS_DISABLE_ON_MOUNT;
        	if (pdr->drive_info.drive_operating_flags & DRVOP_MOUNT_VALID)
        		pc_dskfree(driveno);
        	rtfs_release_media_and_buffers(driveno);
		}
		return(TRUE);
    }
    else
    {
    	/* Clear any temporary remount parms they will be made stale */
    	if (pc_parsedrive( &driveno, drive_name, CS_CHARSET_NOT_UNICODE))
		{
        	pdr = pc_drno_to_drive_struct(driveno);
			if (pdr)
				pdr->drive_info.drive_operating_flags &= ~(DRVOP_FS_START_ON_MOUNT|DRVOP_FS_DISABLE_ON_MOUNT|DRVOP_FS_CLEAR_ON_MOUNT);
		}

        /* Get the drive and make sure it is mounted   */
        driveno = check_drive_name_mount(drive_name, CS_CHARSET_NOT_UNICODE);
        if (driveno < 0)
            return(FALSE); /* check_drive set errno */
        rtfs_clear_errno();
        pdr = pc_drno_to_drive_struct(driveno);

        ret_val = TRUE;
		if (pdr)
		{
            pfscntxt = (FAILSAFECONTEXT *) pdr->drive_state.failsafe_context;
            if (pfscntxt)
            {
                if (!fs_flush_internal(pfscntxt, TRUE))
                    ret_val = FALSE;
                fs_failsafe_disable(pdr, 0);
                pdr->drive_info.drive_operating_flags |= DRVOP_FS_DISABLE_ON_MOUNT;
            }
		}
        if (!release_drive_mount_write(driveno))/* Release lock, unmount if aborted */
            ret_val = FALSE;
        return(ret_val);
    }
}

/* fs_api_commit - Commit failsafe buffers to disk
*
*   Summary:
*       BOOLEAN fs_api_commit(byte *drive_id, BOOLEAN synch_fat)
*
*   byte *drive_id  - Drive identifier, A:, B:, C: etc
*   synch_fat       - If TRUE synchronize the FAT volume with the
*                     journal file, set the journal file status
*                     to FS_STATUS_RESTORED and rewind the journal
*                     file.
*                     If synch_fat is not TRUE then the failsafe buffers
*                     are flushed to the journal file and the status is
*                     set to FS_STATUS_NEEDS_RESTORE. The next time a change
*                     is made to the volume the journal file will be
*                     re-opened, status will revert to FS_STATUS_JOURNALING
*                     and new journal entries will be appended.
*
* Description:
*
* This routine updates the Journal file such that when it returns it is
* guaranteed that the disk volume may be restored from the Journal file.
*
* If synch_fat is FALSE the Journal file is simply flushed to disk in such
* a way that the current session is guaranteed to be fully journaled and
* restorable up until this point and Failsafe journalling continues.
*
*
* If synch_fat is TRUE the Journal file is flushed to disk, the FAT
* file system is synchronized with the current Journalled state and
* the journal file is reset to the beginning. If a system power or
* media error occurs during the synchronization phase the failsafe
* file satus will be either FS_STATUS_RESTORING, or FS_STATUS_NEEDS_RESTORE
* both statuses will trigger a restore during remount.
*
* Note: fs_api_commit() will perform a number of disk reads and disk writes.
* the amount of disk activity depends on the number and size of the
* transactions performed and thus does not have deterministic
* characteristics.
*     If better control over latency is desired, consider using the
* asynchronous Journal file commit and FAT volume synchronization
* capabilities provided by fs_api_async_commit_continue() and
* fs_api_async_commit_start().
*
* Note: Call fs_api_commit() with synch_fat set to FALSE to perform
*       a fast Flush. When this routine returns succesfully it is guaranteed
*       that the FAT may restored to be consistent with the current
*       applications view.
*
* Note: Call fs_api_commit() with synch_fat set to TRUE to perform
*       a fast Flush and a volume synchronization. When this routine returns
*       succesfully it is guaranteed the on disk volume is consistent with
*       the current applications view. If this routine does not return
*       because of a power outage or removal event, the FAT may be
*       restored if at least the the Journal File fast flush procedure
*       succeeded. And the file status
*
* Note: If you wish to have application level confirmation that the Journal
*       file was properly flushed call fs_api_commit() twice
*       in a row. The first time call it with sync_fat set to FALSE, when
*       this call returns you are assured that the volume may be restored
*       to the current applications view of the the volume. The second
*       time call it with sync_fat set to TRUE. When this returns you
*       are assured that the disk volume itself is the current applications
*       view of the the volume.
*
* Returns:
*   TRUE         - Success
*   FALSE        - Failure
*   If FALSE is returned errno will be set to one of the following.
*
*   PEINVALIDDRIVEID        - Drive argument invalid
*   PENOINIT                - fs_api_enable must be called first
*   PEEINPROGRESS			- Cant perform operation because ASYNC
*                             commit operation in progress
*   PEIOERRORWRITEJOURNAL   - Error writing journal file or NVRAM section
*   PEIOERRORREADJOURNAL    - Error writing journal file or NVRAM section
*   PEIOERRORWRITEFAT       - Error writing fat area
*   PEIOERRORWRITEBLOCK     - Error writing directory area
*   PEINTERNAL              - internal error, only occurs if ERTFS is not configured.
*   An ERTFS system error
*/

BOOLEAN fs_api_commit(byte *path,BOOLEAN synch_fats)
{
FAILSAFECONTEXT *pfscntxt = 0;
int driveno;
BOOLEAN ret_val;
DDRIVE *pdr;

    driveno = check_drive_name_mount(path, CS_CHARSET_NOT_UNICODE);
    if (driveno < 0)
        return(FALSE); /* Check_drive set errno */
    pdr = pc_drno_to_drive_struct(driveno);
    rtfs_clear_errno();
	if (pdr)
    	pfscntxt = (FAILSAFECONTEXT *) pdr->drive_state.failsafe_context;
    if (!pfscntxt)
    {
        rtfs_set_errno(PENOINIT, __FILE__, __LINE__);
        ret_val = FALSE;
    }
    /* Flush any directory buffers */
    else if (fs_flush_internal(pfscntxt, synch_fats))
       ret_val = TRUE;
    else
       ret_val = FALSE;
    if (!release_drive_mount_write(driveno))/* Release lock, unmount if aborted */
        ret_val = FALSE;
    return (ret_val);
}

/* Stop failsafe logging, If an error occurred set the Failsafe error value
   to error. If no error set error to zero, Failsafe logging is restarted
   by calling fs_failsafe_enable(). If an error occured fs_failsafe_enable()
   must be called with clear_error set to TRUE */
void fs_failsafe_disable(DDRIVE *pdrive, int error)
{
FAILSAFECONTEXT *pfscntxt;

    pfscntxt= (FAILSAFECONTEXT *) pdrive->drive_state.failsafe_context;
    pdrive->drive_state.failsafe_context = 0;
    if (pfscntxt)
    {
    FSJOURNAL *pjournal;
        pjournal = CONTEXT_TO_JOURNAL(pfscntxt);
        /* Release lists associated with the context block (TRUE means claim semaphore) */
        pc_fraglist_free_list(pjournal->open_free_fragments);
        pc_fraglist_free_list(pjournal->flushed_free_fragments);
        pc_fraglist_free_list(pjournal->restoring_free_fragments);
        pjournal->open_free_fragments =
        pjournal->flushed_free_fragments =
        pjournal->restoring_free_fragments = 0;
        /* Release any clusters reserved for Failsafe */
#if (INCLUDE_RTFS_FREEMANAGER)
        if (pfscntxt->nv_reserved_fragment.start_location)
            free_manager_release_clusters(pdrive, pfscntxt->nv_reserved_fragment.start_location,
                    pfscntxt->nv_reserved_fragment.end_location - pfscntxt->nv_reserved_fragment.start_location +1,FALSE);
#endif
        pfscntxt->nv_reserved_fragment.start_location = 0;
        pfscntxt->error_val = error;
    }
}


static FAILSAFECONTEXT *fs_failsafe_enable(DDRIVE *pdrive, BOOLEAN clear_errors)
{
FAILSAFECONTEXT *pfscntxt;
    pfscntxt= (FAILSAFECONTEXT *) pdrive->du.user_failsafe_context;
    if (pfscntxt)
    {
        if (pfscntxt->error_val)
        {
            if (clear_errors)
                pfscntxt->error_val = 0;
            else
                pfscntxt = 0;
        }
    }
    pdrive->drive_state.failsafe_context = (void *)pfscntxt;
    return(pfscntxt);
}


/* BOOLEAN fs_sessioninfo_internal(FAILSAFECONTEXT *pfscntxt, FSINFO *pinfo)

   Called by fs_failsafe_info() when Jounalling is
   active.

   Populates the following structure:

typedef struct fsinfo {
        BOOLEAN journaling;         - Always TRUE
        BOOLEAN journal_file_valid; - TRUE if the journal is flushed
        dword version;              - 3
        dword status;               - FS_STATUS_JOURNALING
        dword numindexblocks;       - Index block in the Journal
        dword totalremapblocks;     - Total number of remap blocks
        dword numblocksremapped;    - Total number of blocks remapped
        dword journaledfreespace;   - Current Freesapce
        dword currentfreespace;     - Current Freesapce
        dword journal_block_number; - Physical location of the Journal
        dword filesize;             - Size of the Journal in blocks
        BOOLEAN needsflush;         - TRUE is Journal must be flushed
        BOOLEAN out_of_date;        - FALSE
        BOOLEAN check_sum_fails;    - FALSE
        BOOLEAN restore_required;   - FALSE
        BOOLEAN restore_recommended;- FALSE
 } FSINFO;

*/

BOOLEAN fs_sessioninfo_internal(FAILSAFECONTEXT *pfscntxt, FSINFO *pinfo)
{
FSJOURNAL *pjournal;
FSRESTORE *prestore;
    rtfs_memset(pinfo, 0, sizeof(*pinfo));

    pjournal = CONTEXT_TO_JOURNAL(pfscntxt);
    prestore = CONTEXT_TO_RESTORE(pfscntxt);

    pinfo->version              = FS_CURRENT_VERSION;
    pinfo->numblocksremapped    =   pjournal->open_remapped_blocks +
                                    pjournal->flushed_remapped_blocks +
                                    pjournal->restoring_remapped_blocks;

    pinfo->journaledfreespace   = pjournal->session_current_freespace;
	pinfo->currentfreespace     = (dword) pfscntxt->pdrive->drive_info.known_free_clusters;
    /* If using failsafe file journalling this will be true */
    pinfo->journal_block_number = pfscntxt->nv_buffer_handle;
    pinfo->filesize             = pfscntxt->journal_file_size;
    pinfo->journaling           = TRUE;

    if (pjournal->open_remapped_blocks)
        pinfo->needsflush        = TRUE;
    else
        pinfo->needsflush        = FALSE;
    pinfo->out_of_date          = FALSE;
    pinfo->check_sum_fails      = FALSE;
    pinfo->restore_required     = FALSE;
    pinfo->restore_recommended  = FALSE;


     /* Set internal values */
    pinfo->_start_session_frame  = pjournal->session_start_record;    /* First record in the session */

    pinfo->_start_restored_frame = 0;   /* Records already restored */
    pinfo->_last_restored_frame  = 0;   /* Not sure what to do here */

    pinfo->_first_restoring_frame = prestore->restoring_start_record;
    if (pinfo->_first_restoring_frame)
        pinfo->_last_restoring_frame  = prestore->restoring_terminal_record;
    else
        pinfo->_last_restoring_frame  = 0;
    pinfo->_first_flushed_frame = pjournal->flushed_start_record;
    pinfo->_last_flushed_frame  = pjournal->flushed_terminal_record;
    pinfo->_records_to_restore  = pjournal->flushed_remapped_blocks + pjournal->restoring_remapped_blocks;

    /* Set file valid if it has been been flushed */
    if (pjournal->flushed_remapped_blocks+pjournal->restoring_remapped_blocks)
        pinfo->journal_file_valid  = TRUE;
    else
        pinfo->journal_file_valid  = FALSE;
    return(TRUE);
}


/* fs_failsafe_dskopen - Open failsafe operations on a disk volume
*
*
*   Summary:
*       BOOLEAN fs_failsafe_dskopen(DDRIVE *pdrive)
*
* Description:
*
* This routine is called automatically by the automount logic in check_media
* after it has sucessfully automounted a volume.
* This routine does the following things:
*  1. Checks the Failsafe file to see if a volume restore is needed
*     If so, calls a user callback fs_api_cb_restore() to ask the
*     the user for the desired strategy:
*       FS_CB_ABORT    Cause Mount to Fail, set errno to PEFSRESTORENEEDED
*       FS_CB_RESTORE  Restore
*       FS_CB_CONTINUE Ignore the Failsafe file and continue
*     If a restore is requested if restores the Volume from the Journal
*  2. Next the routine calls a user callback fs_api_cb_enable() to
*     ask if Failsafe should be enabled.
*     If so, it opens the Journal File and initiates Journalling
*
* Returns:
*   TRUE  - Success
*   FALSE - Error
*
* If FALSE is returned errno is set to one of these values
*
*   PEFSCREATE       - Error creating the journal file
*   PEIOERRORWRITEJOURNAL - Error initializing the journal file
*
*/

static BOOLEAN fs_failsafe_dskopen(DDRIVE *pdrive)
{
FAILSAFECONTEXT *pfscntxt;
BOOLEAN do_enable;


    /* Make sure Failsafe is disabled if the media is write protected, If we try journaling or restoring we will
       run into write protect errors. */
    if (pdrive->pmedia_info->is_write_protect)
	{
		fs_failsafe_disable(pdrive, 0);
		return(TRUE);
	}

    /* Try to enable failsafe don't clear errors */
    pfscntxt = fs_failsafe_enable(pdrive, FALSE);
    if (!pfscntxt)      /* Failsafe not enabled */
        return(TRUE);
    do_enable = FALSE;
    /* If remounting after a disable or after an enable with clear, do not restore */
    if ((pdrive->drive_info.drive_operating_flags & (DRVOP_FS_DISABLE_ON_MOUNT|DRVOP_FS_CLEAR_ON_MOUNT)) == 0)
    {
    	if ( (pdrive->du.drive_operating_policy & DRVPOL_DISABLE_AUTOFAILSAFE) == 0)   /* default to application level restore for test environment */
    	{
        	if (!fs_failsafe_autorestore(pfscntxt))
        		return(FALSE);
		}
    }
    if (pdrive->drive_info.drive_operating_flags & DRVOP_FS_DISABLE_ON_MOUNT)
        do_enable = FALSE;
    else if (pdrive->drive_info.drive_operating_flags & DRVOP_FS_START_ON_MOUNT)
    {
        /* fs_api_enable() was called with the disk not mounted
           this forces journaling to comence immediately, bypassing
           the user callback */
        do_enable = TRUE;
     }
     else if ( (pdrive->du.drive_operating_policy & DRVPOL_DISABLE_AUTOFAILSAFE) == 0)	/* default to application level journal enable for test environment */
     {
        /* Call callback to see if "auto-enable" is needed */
        do_enable = fs_api_cb_enable(pfscntxt->pdrive->driveno);
     }
	 /* Clear parameter flags that may have been set if fs_api_enable() or fs_api_disable() the triggered a remount */
	 /* Do not clear DRVOP_FS_DISABLE_ON_MOUNT here, this was set by fs_api_disable and can only be cleared by pr_fs_enable */
     pdrive->drive_info.drive_operating_flags &= ~(DRVOP_FS_START_ON_MOUNT|DRVOP_FS_CLEAR_ON_MOUNT);
    /* If Failsafe is not enabled disable FailSafe and return */
    if (!do_enable)
    {
        fs_failsafe_disable(pdrive,0);
        return(TRUE);
    }
    return(fs_failsafe_start_journaling(pfscntxt, 0, 0));
}
void fs_failsafe_scaleto_blocksize(FAILSAFECONTEXT *pfscntxt) /* :LB: Adjust index block scale factors */
{
        pfscntxt->fs_frame_size = 128 * pfscntxt->pdrive->drive_info.blockspsec; /* Number of dwords that fit in a disk block, 128 for five hundred 12 byte blocks, 256 for 1024 etc. */
        pfscntxt->fs_frame_max_records = pfscntxt->fs_frame_size-FS_FRAME_HEADER_SIZE;  /*  Number of indeces that fit in a disk block after the header */
}

/* Clear the clusters reserved for journaling.*/
void fs_zero_reserved(FAILSAFECONTEXT *pfscntxt)
{
    pfscntxt->nv_reserved_fragment.start_location =
    pfscntxt->nv_reserved_fragment.end_location   = 0;
    pfscntxt->nv_reserved_fragment.pnext = 0;
}

static void fs_check_file_lowwater(FAILSAFECONTEXT *pfscntxt, FSJOURNAL *pjournal)
{

    if (pjournal->frames_free < pfscntxt->min_free_blocks ||
        pfscntxt->min_free_blocks > pfscntxt->journal_file_size ||
        pfscntxt->min_free_blocks == 0)
        pfscntxt->min_free_blocks = pjournal->frames_free;
}

static BOOLEAN fs_failsafe_start_journaling(FAILSAFECONTEXT *pfscntxt, dword start_sector, dword size_in_sectors)
{
FSJOURNAL *pjournal;

    pjournal = CONTEXT_TO_JOURNAL(pfscntxt);
    fs_failsafe_scaleto_blocksize(pfscntxt); /* :LB: Adjust index block scale factors */
    /* Make sure the index buffer is initialized */
	pjournal->open_index_buffer = (dword *) pfscntxt->user_open_index_buffer;
    pjournal->journal_buffer_start = (dword *) pfscntxt->user_open_index_buffer;

    if (start_sector) /* If start was passed in we are re-opening with a smaller size */
    {
        pfscntxt->journal_file_size  = size_in_sectors;
        pfscntxt->nv_buffer_handle   = start_sector;
        pfscntxt->nv_raw_mode = FALSE;
    }
    else /* Call a callback function to get the preferred journal file size for this volume size */
    {
        dword raw_start_sector, file_size_sectors;
        /* First call to determine if the journal is in a fixed location
           if it is use that, otherwise call back to retrieve the size */
        raw_start_sector = file_size_sectors = 0;
        if (fs_api_cb_journal_fixed(pfscntxt->pdrive, &raw_start_sector, &file_size_sectors))
            pfscntxt->journal_file_size = file_size_sectors;
        else
        {
            pfscntxt->journal_file_size = fs_api_cb_journal_size(pfscntxt->pdrive);
        }
        /* Create the failsafe file */
        /* If the nvio layer sets nv_disable_failsafe TRUE when:
            . the disk is too fulls to hold a minimum sized journal and callbacks instruct it to proceed without Journaling.
            . or.. there is only one FAT copy and the automatic journal placement algoritm requires 2
           */
        pfscntxt->nv_disable_failsafe = FALSE;
        if (!failsafe_create_nv_buffer(pfscntxt, raw_start_sector, file_size_sectors))
        {
            /* Disable Failsafe */
            fs_failsafe_disable(pfscntxt->pdrive,PEFSCREATE);
            /* Check if we should transparently continue without Journaling */
            if (pfscntxt->nv_disable_failsafe)
			{
            	fs_failsafe_disable(pfscntxt->pdrive,0);
                return(TRUE);
			}
            /* and set error, Failsafe can't be used until cleared at the application layer */
            rtfs_set_errno(PEFSCREATE, __FILE__, __LINE__);
            return(FALSE);
        }
    }
    RTFS_DEBUG_LOG_DEVIO(pfscntxt->pdrive, RTFS_DEBUG_IO_J_ROOT_READ, (pfscntxt->nv_buffer_handle+0), 1)
    /* Read the master block and create a new session id */
    if (!failsafe_nv_buffer_io(pfscntxt, 0, 1, (byte *) pjournal->journal_buffer_start,TRUE))
	{	/* If it failed clear it by writing zeros and try again */
		rtfs_memset(pjournal->journal_buffer_start, 0, pfscntxt->pdrive->drive_info.bytespsector);
    	if (!failsafe_nv_buffer_io(pfscntxt, 0, 1, (byte *) pjournal->journal_buffer_start,FALSE))
        	return(FALSE);
    	if (!failsafe_nv_buffer_io(pfscntxt, 0, 1, (byte *) pjournal->journal_buffer_start,TRUE))
        	return(FALSE);
	}
    UPDATE_FSRUNTIME_STATS(pfscntxt,journal_index_reads, 1)

    pfscntxt->session_id = *(pjournal->journal_buffer_start+FS_MASTER_OFFSET_SESSION) + 1;

    rtfs_memset(pjournal, 0, sizeof(*pjournal));

    /* Reset the index buffer space pointers after clearing the journal structure */
    /* Make sure the index buffer is initialized */
	pjournal->open_index_buffer = (dword *) pfscntxt->user_open_index_buffer;
    pjournal->journal_buffer_start = (dword *) pfscntxt->user_open_index_buffer;

    /* Start the new session at block 1 */
    pjournal->session_start_record = pjournal->next_free_frame = 1;
    pjournal->frames_free = pfscntxt->journal_file_size - 1;
    /* Update low water stats */
    fs_check_file_lowwater(pfscntxt, pjournal);

    pjournal->wrap_counter = 0;
    pjournal->frame_sequence = 0;
    /* Create and write the master block to block zero in the journal */

    fs_mem_master_init(pfscntxt, pjournal->journal_buffer_start, pfscntxt->journal_file_size, pfscntxt->session_id,pjournal->session_start_record);

    RTFS_DEBUG_LOG_DEVIO(pfscntxt->pdrive, RTFS_DEBUG_IO_J_ROOT_READ, (pfscntxt->nv_buffer_handle+0), 1)


   	if (pfscntxt->user_index_buffer_size_sectors <= 2)
	{	/* Update the master record if we are not buffering otherwise leave the master info initialized and we'll flush it when the first frame is flushed */
    	if (!failsafe_nv_buffer_io(pfscntxt, 0, 1,
                (byte *) pjournal->journal_buffer_start,FALSE))
                return(FALSE);
    	UPDATE_FSRUNTIME_STATS(pfscntxt, journal_index_writes, 1)
	}

    /* Create the block freelist */
    pjournal->blockmap_freelist = pfscntxt->blockmap_core;
    {
    dword i;
    FSBLOCKMAP *pbm;
        pbm = pfscntxt->blockmap_core;
        for (i = 1; i < pfscntxt->blockmap_size; i++,pbm++)
            pbm->pnext = pbm + 1;
        pbm->pnext = 0;

    }
    fs_clear_session_vars(pfscntxt);
    /* Open a new frame, set the start record and write it */
    return(fs_file_open_new_frame(pfscntxt,FALSE));
}
/* Initialize session variables on init or when journaling is
   starting after a restore isd queued */
void fs_clear_session_vars(FAILSAFECONTEXT *pfscntxt)
{
FSJOURNAL *pjournal;

    pjournal = CONTEXT_TO_JOURNAL(pfscntxt);
    pjournal->session_current_checksum = 0;
    pjournal->session_current_freespace = pfscntxt->pdrive->drive_info.known_free_clusters;
    pjournal->open_start_record = 0;
    pjournal->open_current_frame = 0;
    pjournal->buffer_sector_start = 0;
    pjournal->open_current_index = 0;
    pjournal->open_current_free = 0;
    pjournal->open_current_used = 0;
    pjournal->open_current_checksum = 0;
    pjournal->flushed_start_record = 0;
    pjournal->flushed_terminal_record = 0;
    pjournal->flushed_last_block = 0;
    pjournal->flushed_remapped_blocks = 0;
    pjournal->open_remapped_blocks = 0;
}


/* This routine is called by RTFS API calls before they return to the user.
   This routine calls a user callback funtion to ask

   If the API call changed the buffers or the Journal file then call
   the callback function fs_api_cb_flush() and ask for instructions.

   The callback can return:
      FS_CB_FLUSH    - Tells Failsafe to flush the journal file
      FS_CB_SYNC     - Tells Failsafe to flush the journal file and
                       synchronize the FAT volume
      FS_CB_CONTINUE - Tells Failsafe to proceded and flush.
   If this function returns 1 or 2 every time it is called then
   the API becomes transactional in nature.
*/
BOOLEAN fs_failsafe_autocommit(DDRIVE *pdrive)
{
FAILSAFECONTEXT *pfscntxt;

#if (INCLUDE_RTFS_PROPLUS) /* fs_failsafe_autocommit() - If Async in progress don't autocommit */
    if (pdrive->drive_state.drive_async_state != DRV_ASYNC_IDLE)
    {
        return(TRUE);
    }
#endif
	/* If auto flush is enabled don't ask the user for a flush option and do not flush */
    if (pdrive->du.drive_operating_policy & DRVPOL_DISABLE_AUTOFAILSAFE)
        return(TRUE);

    pfscntxt= (FAILSAFECONTEXT *) pdrive->drive_state.failsafe_context;
    /* Ask user if failsafe is enabled */
    if (pfscntxt)
    {
    int user_request;

        /*  0 = No flush, 1 = Flush Journal, 2 = Flush Journal and Synchronize volume */
        user_request = fs_api_cb_flush(pfscntxt->pdrive->driveno);

        /* Flush journal only, don't commit FAT */
        if (user_request == FS_CB_FLUSH)
        {
            return(fs_flush_internal(pfscntxt,FALSE));
        }
        else if (user_request == FS_CB_SYNC) /* Flush journal and synchronoze FAT */
        {
            return(fs_flush_internal(pfscntxt,TRUE));
        }
    }
    return(TRUE);
}


#if (INCLUDE_RTFS_PROPLUS && INCLUDE_ASYNCRONOUS_API) /* fs_api_async_commit_start - Commit a volume asynchronously */
/* fs_api_async_commit_start - Commit a volume asynchronously
*
*   Summary:
*       BOOLEAN fs_api_async_commit_start(drive_id)
*                                     blocks.
*
* Description:
*
* Returns:
*   PC_ASYNC_COMPLETE - No commit required
*   PC_ASYNC_CONTINUE - Commit required, continue calling
*   fs_api_async_commit_continue() until it returns PC_ASYNC_COMPLETE
*   or PC_ASYNC_CONTINUE
*
*   If PC_ASYNC_ERROR is returned errno will be set to one of the following.
*
*   PEINVALIDDRIVEID        - Drive argument invalid
*   PENOINIT                - fs_api_enable must be called first
*   PEEINPROGRESS			- Cant perform operation because ASYNC
*                             commit operation in progress
*
*   An ERTFS system error
*
*/

int fs_api_async_commit_start(byte *path)
{
    int driveno;
    DDRIVE *pdr;
    int status;
    FAILSAFECONTEXT *pfscntxt;

    CHECK_MEM(BOOLEAN, 0)   /* Make sure memory is initted */

    rtfs_clear_errno();  /* pc_diskflush: clear error status */
    driveno = check_drive_name_mount(path, CS_CHARSET_NOT_UNICODE);
    if (driveno < 0)
        return(PC_ASYNC_ERROR);
    /* Find the drive   */
    pdr = pc_drno2dr(driveno);
    pfscntxt = (FAILSAFECONTEXT *) pdr->drive_state.failsafe_context;
    if (!pfscntxt)
    {
        rtfs_set_errno(PENOINIT, __FILE__, __LINE__);
        status = PC_ASYNC_ERROR;
    }
    else
    {
        /* Turn on one shot flush triggers so async continue will complete these operations
            even if not enabled as aoutomatic options */
        pdr->drive_info.drive_operating_flags |= (DRVOP_ASYNC_FFLUSH|DRVOP_ASYNC_JFLUSH|DRVOP_ASYNC_JRESTORE);
        status = PC_ASYNC_CONTINUE;
    }
    release_drive_mount(driveno);/* Release lock, unmount if aborted */
    return(status);
}

/* Called by async manager.. needs another look */
int _fs_api_async_flush_journal(DDRIVE *pdrive)
{
    int status;
    FAILSAFECONTEXT *pfscntxt;
	dword loop_guard = GUARD_ENDLESS;


    pfscntxt = (FAILSAFECONTEXT *) pdrive->drive_state.failsafe_context;

    /* If in asynchronous restore it is an error */
    if (pfscntxt->operating_flags & FS_OP_ASYNCRESTORE)
    {
        ERTFS_ASSERT(rtfs_debug_zero())
        return(PC_ASYNC_ERROR);
    }
    /* Flush the fat synchronously if it is dirty, should
       be flushed already but just in case */
    if (pdrive->drive_info.drive_operating_flags & DRVOP_FAT_IS_DIRTY)
    {
        do {
			if (journal_check_endless_loop(&loop_guard))
				return(PC_ASYNC_ERROR);
            status = pc_async_flush_fat_blocks(pdrive,0);
        } while (status == PC_ASYNC_CONTINUE);
        if (status != PC_ASYNC_COMPLETE)
            return(status);
    }
    /* Flush the journal file synchronously if it is dirty */
    if (fs_flush_current_session(pfscntxt))
    {
        pfscntxt->pdrive->drive_info.drive_operating_flags &= ~DRVOP_FS_NEEDS_FLUSH;
        pfscntxt->pdrive->drive_info.drive_operating_flags |= DRVOP_FS_NEEDS_RESTORE;
        status = PC_ASYNC_COMPLETE;
    }
    else
        status = PC_ASYNC_ERROR;
    return(status);
}

/* Called by async manager.. needs another look */
int _fs_api_async_commit_continue(DDRIVE *pdrive)
{
    int status;
    FAILSAFECONTEXT *pfscntxt;

    pfscntxt = (FAILSAFECONTEXT *) pdrive->drive_state.failsafe_context;
    if (!pfscntxt ||
        !pfscntxt->user_restore_transfer_buffer || pfscntxt->user_restore_transfer_buffer_size < 2)
    {
        rtfs_set_errno(PENOINIT, __FILE__, __LINE__);
        status = PC_ASYNC_ERROR;
    }
    else if (pfscntxt->operating_flags & FS_OP_ASYNCRESTORE)
    {
        status = fs_restore_from_session_continue(pfscntxt);
    }
    else
    {
        /* Make sure the journal is flushed */
        status = _fs_api_async_flush_journal(pdrive);
        if (status == PC_ASYNC_COMPLETE)
            if (pdrive->drive_info.drive_operating_flags & DRVOP_FS_NEEDS_RESTORE)
              status = fs_restore_from_session_start(pfscntxt);
    }
    /* Clear async flags */
    if (status != PC_ASYNC_CONTINUE)
    {
        if (pfscntxt)
            pfscntxt->operating_flags &= FS_OP_CLEAR_ASYNC;
    }
    return(status);
}


/* Synchronous flush routine. Makes sure buffer structures are flushed
   to theJournal file,  and optionally synchronize the FAT folume. */
static BOOLEAN fs_flush_internal(FAILSAFECONTEXT *pfscntxt,BOOLEAN synch_fats)
{
    if (!pfscntxt)
        return(FALSE);
    /* Turn on one shot flush triggers so async continue will complete these operations
         even if not enabled as aoutomatic options */
    pfscntxt->pdrive->drive_info.drive_operating_flags |= (DRVOP_ASYNC_FFLUSH|DRVOP_ASYNC_JFLUSH);
    if (synch_fats)
        pfscntxt->pdrive->drive_info.drive_operating_flags |= DRVOP_ASYNC_JRESTORE;
    /* Finish outsanding ProPlus File operations */
    if (_pc_async_step(pfscntxt->pdrive, DRV_ASYNC_DONE_FILES, 0) != PC_ASYNC_COMPLETE)
        return(FALSE);
    /* Flush RTFS FATS and files if needed */
    if (!_pc_diskflush(pfscntxt->pdrive))
        return(FALSE);
    /* Flush Failsafe Journal if needed */
    if (_pc_async_step(pfscntxt->pdrive, DRV_ASYNC_DONE_JOURNALFLUSH, 0) != PC_ASYNC_COMPLETE)
        return(FALSE);
    if (synch_fats)
    {
        if (_pc_async_step(pfscntxt->pdrive, DRV_ASYNC_DONE_RESTORE, 0) != PC_ASYNC_COMPLETE)
            return(FALSE);
    }
    return(TRUE);
}
#else
/* Synchronous flush routine. Makes sure buffer structures are flushed
   to theJournal file,  and optionally synchronize the FAT folume. */
static BOOLEAN _fs_api_sync_commit(FAILSAFECONTEXT *pfscntxt);

static BOOLEAN fs_flush_internal(FAILSAFECONTEXT *pfscntxt,BOOLEAN synch_fats)
{
    if (!pfscntxt)
        return(FALSE);

    /* Flush RTFS FATS and files if needed */
    if (!_pc_diskflush(pfscntxt->pdrive))
        return(FALSE);
    /* Flush the journal file synchronously if it is dirty */
    if (pfscntxt->pdrive->drive_info.drive_operating_flags & DRVOP_FS_NEEDS_FLUSH)
    {
        if (!fs_flush_current_session(pfscntxt))
            return(FALSE);
        pfscntxt->pdrive->drive_info.drive_operating_flags &= ~DRVOP_FS_NEEDS_FLUSH;
        pfscntxt->pdrive->drive_info.drive_operating_flags |= DRVOP_FS_NEEDS_RESTORE;
    }
    if (synch_fats && (pfscntxt->pdrive->drive_info.drive_operating_flags & DRVOP_FS_NEEDS_RESTORE))
        return(_fs_api_sync_commit(pfscntxt));

    return(TRUE);
}

static BOOLEAN _fs_api_sync_commit(FAILSAFECONTEXT *pfscntxt)
{
    int status;

    if (!pfscntxt || !pfscntxt->user_restore_transfer_buffer || pfscntxt->user_restore_transfer_buffer_size < 2)
    {
        rtfs_set_errno(PENOINIT, __FILE__, __LINE__);
        return(FALSE);
    }
    status = fs_restore_from_session_start(pfscntxt);

    while (status == PC_ASYNC_CONTINUE)
    {
        status = fs_restore_from_session_continue(pfscntxt);
    }
    pfscntxt->pdrive->drive_info.drive_operating_flags &= ~DRVOP_FS_NEEDS_RESTORE;
    if (status == PC_ASYNC_COMPLETE)
        return(TRUE);
    else
        return(FALSE);
}


#endif /* (INCLUDE_RTFS_PROPLUS && INCLUDE_ASYNCRONOUS_API) */

/* Called by the transaction file manager - make sure the journal file is flushed */
BOOLEAN fs_flush_transaction(DDRIVE *pdrive)
{
    return(fs_flush_internal((FAILSAFECONTEXT *)pdrive->drive_state.failsafe_context, FALSE));
}

static BOOLEAN fs_dynamic_configure_volume(DDRIVE *pdr, struct rtfs_volume_resource_reply *preply)
{
    pdr->mount_parms.fsrestore_buffer_size_sectors 		= preply->fsrestore_buffer_size_sectors;
    pdr->mount_parms.fsjournal_n_blockmaps 				= preply->fsjournal_n_blockmaps;
    pdr->mount_parms.fsfailsafe_context_memory 			= preply->fsfailsafe_context_memory;
    pdr->mount_parms.fsindex_buffer_size_sectors		= preply->fsindex_buffer_size_sectors;

    pdr->mount_parms.fsrestore_buffer_memory 			= preply->failsafe_buffer_memory;
    pdr->mount_parms.fsjournal_blockmap_memory 			= preply->fsjournal_blockmap_memory;
    pdr->mount_parms.fsindex_buffer_memory 				= preply->failsafe_indexbuffer_memory;

    pdr->mount_parms.failsafe_buffer_base 				= preply->failsafe_buffer_base;
    pdr->mount_parms.failsafe_indexbuffer_base 			= preply->failsafe_indexbuffer_base;
	if (pdr->mount_parms.fsfailsafe_context_memory)
	{
		if (!pdr->mount_parms.fsindex_buffer_size_sectors || !pdr->mount_parms.fsjournal_n_blockmaps || !pdr->mount_parms.fsjournal_blockmap_memory || !pdr->mount_parms.fsindex_buffer_memory)
			return(FALSE);
		if (!prtfs_cfg->rtfs_exclusive_semaphore && (pdr->mount_parms.fsrestore_buffer_size_sectors < 2 || !pdr->mount_parms.fsrestore_buffer_memory) )
			return(FALSE);
	}
	return(TRUE);
}

static BOOLEAN fs_dynamic_mount_volume_check(DDRIVE *pdr)
{
    pdr->du.user_failsafe_context      = (void *) pdr->mount_parms.fsfailsafe_context_memory;
    if (pdr->du.user_failsafe_context)
    {
    FAILSAFECONTEXT *fs_context;
        fs_context = (FAILSAFECONTEXT *) pdr->du.user_failsafe_context;
        rtfs_memset(fs_context, 0, sizeof(FAILSAFECONTEXT));

        fs_context->blockmap_size = pdr->mount_parms.fsjournal_n_blockmaps;
        fs_context->blockmap_core = (FSBLOCKMAP *) pdr->mount_parms.fsjournal_blockmap_memory;
        if (!fs_context->blockmap_core)
            return(FALSE);
        fs_context->user_restore_transfer_buffer_size = pdr->mount_parms.fsrestore_buffer_size_sectors;
        if (fs_context->user_restore_transfer_buffer_size)
        {
            fs_context->user_restore_transfer_buffer = (byte *) pdr->mount_parms.fsrestore_buffer_memory;
            if (!fs_context->user_restore_transfer_buffer)
                return(FALSE);
        }
        else
            fs_context->user_restore_transfer_buffer = 0;
        fs_context->user_index_buffer_size_sectors = pdr->mount_parms.fsindex_buffer_size_sectors;
	    fs_context->user_open_index_buffer = (byte *) pdr->mount_parms.fsindex_buffer_memory;
		if (!fs_context->user_open_index_buffer)
			return(FALSE);
        fs_context->pdrive = pdr;
        /* call fs_config_check() to validate and post process context  */
        if (!fs_config_check(fs_context))
            return(FALSE);
    }
	return(TRUE);
}

/* Called by fs_dynamic_mount_volume_check() */
static BOOLEAN fs_config_check(FAILSAFECONTEXT *pfscntxt)
{
void *buff;
dword buff_size;
    if (prtfs_cfg->rtfs_exclusive_semaphore)
    {
        buff        = prtfs_cfg->shared_restore_transfer_buffer;
        buff_size   = prtfs_cfg->shared_restore_transfer_buffer_size/pc_get_media_sector_size(pfscntxt->pdrive);
        pfscntxt->assigned_restore_transfer_buffer = 0;
        pfscntxt->assigned_restore_transfer_buffer_size = 0;
    }
    else
    {
        buff        =  pfscntxt->user_restore_transfer_buffer;
        buff_size   =  pfscntxt->user_restore_transfer_buffer_size;
        pfscntxt->assigned_restore_transfer_buffer =  (byte *) buff;
        pfscntxt->assigned_restore_transfer_buffer_size  =	buff_size;
    }
    if (    pfscntxt->blockmap_size &&
            pfscntxt->blockmap_core &&
            buff &&
            buff_size
       )
    {
        /* Remember buffers that were assigned - these aren't shared */
        return(TRUE);
    }
    else
    {
#if (FS_DEBUG)
        FS_DEBUG_SHOWNL("fs_config_check - Failed")
#endif
        pfscntxt->assigned_restore_transfer_buffer = 0;
        pfscntxt->assigned_restore_transfer_buffer_size = 0;
        rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
        return (FALSE);
    }
}

static void fs_claim_buffers(DDRIVE *pdr)
{
FAILSAFECONTEXT *pfscntxt;
    pfscntxt= (FAILSAFECONTEXT *) pdr->du.user_failsafe_context;
    if (pfscntxt)
    {
        if (pfscntxt->assigned_restore_transfer_buffer)
        {
            pfscntxt->user_restore_transfer_buffer = pfscntxt->assigned_restore_transfer_buffer;
            pfscntxt->user_restore_transfer_buffer_size = pfscntxt->assigned_restore_transfer_buffer_size;
        }
        else
        {
       /* Shared buffer size is defined in bytes and must be converted to the sector size of the media */
            pfscntxt->user_restore_transfer_buffer = prtfs_cfg->shared_restore_transfer_buffer;
            /* Shared buffer size is defined in bytes and must be converted to the sector size of the media */
            /* divide by pc_get_media_sector_size(pdr), always returns a value > 0 */
            pfscntxt->user_restore_transfer_buffer_size = prtfs_cfg->shared_restore_transfer_buffer_size/pc_get_media_sector_size(pdr);
        }
    }
}
/* Called by fatop_plus_link_frag() when clusters are to be returned
   to freespace.

  If Journalling is not enebled call fatop_add_free_region() to
  immediately release the clusters. Otherwise
  queue the clusters to be put back in free space as soon as the FAT
  volume structure is synchronized with the journal file.
  This is necessary because we do not want clusters that are free in
  the Journalled view of the meta-structure but are still part of the
  on disk meta structures to be re-used and overwritten. Once the
  on-disk FAT structure is synchronized failsafe will dequeueu these
  records and call fatop_add_free_region() */

static BOOLEAN fs_add_free_region(DDRIVE *pdr, dword cluster, dword n_contig)
{
FAILSAFECONTEXT *pfscntxt;

    pfscntxt = (FAILSAFECONTEXT *) pdr->drive_state.failsafe_context;
    if (!pfscntxt)
    {
#if (INCLUDE_EXFATORFAT64)
		if (ISEXFATORFAT64(pdr))
			 return exfatop_add_free_region(pdr, cluster, n_contig, TRUE);
#endif
        return(fatop_add_free_region(pdr, cluster, n_contig, TRUE));
    }
    else
    {
    /* Allocate a fragment structure and put it on
      the journaling_fragments_to_free list */
    REGION_FRAGMENT *pf;
    FSJOURNAL *pjournal;
       pjournal = CONTEXT_TO_JOURNAL(pfscntxt);
        /* Fold the newly freed cluster range into an existing record if they are adjacent */
        {
        dword loop_guard=prtfs_cfg->cfg_NREGIONS;
        dword right_sib_cluster = cluster+n_contig;
        dword left_sib_cluster  = cluster-1;
            pf = pjournal->open_free_fragments;
            while (pf && loop_guard--)
            {
                if (pf->start_location==right_sib_cluster)
                {
                    pf->start_location=cluster;
                    return(TRUE);
                }
                else if (pf->end_location==left_sib_cluster)
                {
                    pf->end_location = cluster+n_contig-1;
                    return(TRUE);
                }
                pf = pf->pnext;
            }
        }
        /* Allocate a fragment. If none available recycle freelists. If we fail we we are out of luck */
        pf = pc_fraglist_frag_alloc(pdr, cluster, cluster+n_contig-1, 0);
        ERTFS_ASSERT(pf)
        if (!pf)
            return(FALSE);
        pf->pnext = pjournal->open_free_fragments;
        pjournal->open_free_fragments = pf;
        return(TRUE);
    }
}

static BOOLEAN fsfat_devio_write(DDRIVE *pdrive, dword fat_blockno, dword nblocks, byte *fat_data, int fatnumber)
{
FAILSAFECONTEXT *pfscntxt;

    /* Write to the journal file if enabled */
    pfscntxt = (FAILSAFECONTEXT *) pdrive->drive_state.failsafe_context;

    if (fatnumber)      /* Don't journal second FAT copy */
    {
        if (pfscntxt)
            return (TRUE);
        else
        {
        dword blockno;
            UPDATE_RUNTIME_STATS(pdrive, fat_writes, 1)
            UPDATE_RUNTIME_STATS(pdrive, fat_blocks_written, nblocks)
            blockno = fat_blockno+(pdrive->drive_info.secpfat*fatnumber);
            return(raw_devio_xfer(pdrive,blockno, fat_data, nblocks, FALSE, FALSE));
        }
    }
    else
    {
           return(fs_devio_write(pfscntxt,pdrive,fat_blockno, nblocks, fat_data, TRUE));
    }
}

static BOOLEAN fsfat_devio_read(DDRIVE *pdrive, dword fat_blockno, dword nblocks, byte *fat_data)
{
    return(fs_devio_read((FAILSAFECONTEXT *) pdrive->drive_state.failsafe_context,
                pdrive,fat_blockno, nblocks, fat_data, TRUE));
}

static BOOLEAN fsblock_devio_read(DDRIVE *pdrive, dword blockno, byte * buf)
{
    return(fs_devio_read((FAILSAFECONTEXT *) pdrive->drive_state.failsafe_context,
             pdrive,blockno, 1, buf, FALSE));
}
static BOOLEAN fsblock_devio_write(BLKBUFF *pblk)
{
    return(fs_devio_write((FAILSAFECONTEXT *) pblk->pdrive->drive_state.failsafe_context,
            pblk->pdrive,pblk->blockno, 1, pblk->data, FALSE));
}


static BOOLEAN fsblock_devio_xfer(DDRIVE *pdrive, dword blockno, byte * buf, dword n_to_xfer, BOOLEAN reading) /* __fn__ */
{
FAILSAFECONTEXT *pfscntxt;

    pfscntxt= (FAILSAFECONTEXT *) pdrive->drive_state.failsafe_context;
    if (reading)
        return(fs_devio_read(pfscntxt,pdrive,blockno, n_to_xfer, buf, FALSE));
    else
        return(fs_devio_write(pfscntxt,pdrive,blockno, n_to_xfer, buf, FALSE));
}



static BOOLEAN fs_devio_write(FAILSAFECONTEXT *pfscntxt, DDRIVE *pdrive, dword blockno, dword nblocks, byte *data, BOOLEAN in_fat)
{
FSMAPREQ map_info;
FSJOURNAL *pjournal;
dword loop_guard  = GUARD_ENDLESS;

    /* Write to the journal file if enabled */
    if (!pfscntxt)
    {
#if (INCLUDE_DEBUG_RUNTIME_STATS)
        if (in_fat)
        {
            UPDATE_RUNTIME_STATS(pdrive, fat_writes, 1)
            UPDATE_RUNTIME_STATS(pdrive, fat_blocks_written, nblocks)
        }
        else
        {
            if (nblocks==1)
            {
                UPDATE_RUNTIME_STATS(pdrive, dir_buff_writes, 1)
            }
            else
            {
                UPDATE_RUNTIME_STATS(pdrive, dir_direct_writes, 1)
                UPDATE_RUNTIME_STATS(pdrive, dir_direct_blocks_written, nblocks)
            }
        }
#else
    RTFS_ARGSUSED_INT((int) in_fat);
#endif
        return(raw_devio_xfer(pdrive,blockno, data, nblocks, FALSE, FALSE));
    }
    else
    {
        dword n_blocks_left;
        FSBLOCKMAP *pbm;
        pjournal = CONTEXT_TO_JOURNAL(pfscntxt);
        pbm = 0;
        n_blocks_left = nblocks;

        while (n_blocks_left)
        {
			BOOLEAN do_copy=FALSE;
			if (journal_check_endless_loop(&loop_guard))
				return(FALSE);
            /* Set up the map request */
            map_info.volume_blockno  = blockno;
            map_info.n_blocks = n_blocks_left;

            /* See if the first N blocks are mapped already */
            fs_scan_map_cache(pjournal->open_blockmap_cache, &map_info);
            if (map_info.is_mapped)
            {
                /* if the block is mapped we won't be expandig the
                   current blockmap so flush it */
                if (pbm)
                {
                    pjournal->open_blockmap_cache =
                            fs_insert_map_record(pjournal->open_blockmap_cache,pbm);
                    pbm = 0;
                }
            }
            else
            {  /* It's not mapped */
                /* Set n_blocks to the number of unmapped blocks in the range
                   and then go allocate them */
                map_info.n_blocks = map_info.n_replacement_blocks;
               /* Allocate replacement blocks from the journal file */
               if (!fs_journal_alloc_replacement_blocks(pfscntxt, &map_info))
                    return(FALSE);
#if (FS_DEBUG)
               FS_DEBUG_SHOWINT("mapping volume blocks :", map_info.volume_blockno)
               FS_DEBUG_SHOWINT(" - to :", map_info.volume_blockno+map_info.n_replacement_blocks-1)
               FS_DEBUG_SHOWINT("  remapped to :", map_info.journal_blockno)
               FS_DEBUG_SHOWINTNL(" - to :", map_info.journal_blockno+map_info.n_replacement_blocks-1)
#endif
               /* If we have an open map block see if it is contiguous. if so
                   just make it larger. Otherwise queue the current open map
                   and fall trough to create a new one */
                pjournal->open_remapped_blocks += map_info.n_replacement_blocks;
                if (pbm)
                { /* Check if the blocks are contiguous in volume space and journal space
                     if juoranl is broken by a frame header, this won't matrch */
                    if ( (pbm->volume_blockno+pbm->n_replacement_blocks == map_info.volume_blockno) &&
                         (pbm->journal_blockno+pbm->n_replacement_blocks == map_info.journal_blockno))
                        pbm->n_replacement_blocks += map_info.n_replacement_blocks;
                    else
                    {
                        pjournal->open_blockmap_cache =
                            fs_insert_map_record(pjournal->open_blockmap_cache,pbm);
                        pbm = 0; /* Fall through */
                    }

                }
                /* If there is no block map open, now open one */
                if (!pbm)
                {
                    /* If we are contiguous with a the previously mapped region
                       and we fit in the previous frame expand the existing region */
                    /* Check if the blocks are contiguous in volume space and journal space
                       if journal is broken by a frame header, this won't matrch */
                    if ( map_info.adjacent_pbm &&
                        (map_info.adjacent_pbm->volume_blockno+map_info.adjacent_pbm->n_replacement_blocks == map_info.volume_blockno) &&
                         (map_info.adjacent_pbm->journal_blockno+map_info.adjacent_pbm->n_replacement_blocks == map_info.journal_blockno))
                    {
                        map_info.adjacent_pbm->n_replacement_blocks += map_info.n_replacement_blocks;
                    }
                    else
                    {
                        pbm = fs_alloc_blockmap(pjournal);
                        if (!pbm)
                        {
                            rtfs_set_errno(PEFSMAPFULL, __FILE__, __LINE__);
                            return(FALSE);
                        }
                        pbm->volume_blockno = map_info.volume_blockno;
                        pbm->journal_blockno = map_info.journal_blockno;
                        pbm->n_replacement_blocks = map_info.n_replacement_blocks;
                        pbm->pnext = 0;
                    }
                }
            }
            /* response is in  map_info.n_replacement_blocks,
              map_info.replacement_blockno,
            */
			/* Bug fix July 2012 sector caching algorithm was not correct for
			   was improperly merging sectors outside the buffer's range with the buffer
			   pfscntxt->user_index_buffer_size_sectors > 2 */
			if (pfscntxt->user_index_buffer_size_sectors <= 2)
				do_copy=FALSE;
			else
			{ /* Buffered mode, decide if we should copy into the buffer or write to the journal,
			     clip the range to stay inside a purely buffered or unbuffer area */
				if (!map_info.is_mapped)
				{ /* Put all to the buffer if we are not mapped. n_replacement_blocks is already clipped properly  */
					do_copy=TRUE;
				}
				else /* One or more sectors is mapped */
				{
					if (pjournal->replacement_sectors_dirty==0)
						do_copy=FALSE; /* Current buffer is empty write all sectors to the journal volume */
					else
					{
						dword buffer_sector_end = pjournal->buffer_sector_start + pjournal->replacement_sectors_dirty-1;
						if (map_info.journal_blockno < pjournal->buffer_sector_start)
						{ /* Write to the journal file any or all sectors out of range because they are before the buffer range
						     reduce the range on this pass so we are exclusively to the left of the buffer frame */
							dword n=pjournal->buffer_sector_start-map_info.journal_blockno;
							if (n < map_info.n_replacement_blocks)
								map_info.n_replacement_blocks=n;
							do_copy=FALSE;
						}
						else if (map_info.journal_blockno > buffer_sector_end)
						{ /* beyond our buffer range to the right, do not copy. */
							do_copy=FALSE;
						}
						else
						{ /* Copy sectors to the current buffer, but clip */
							/* Clip the replacement range to the end of our bufferable range */
							if (pjournal->replacement_sectors_dirty+map_info.n_replacement_blocks > pfscntxt->user_index_buffer_size_sectors)
							{
								map_info.n_replacement_blocks=
								pfscntxt->user_index_buffer_size_sectors-pjournal->replacement_sectors_dirty;
							}
							do_copy=TRUE;
						}
					}
				}
			}
			if (do_copy)
			{	/* If we are buffering, copy the user data to the buffer and update the buffer pointer we'll write when we close or flush the frame */
			int n_replacement_bytes;
  			byte *pbuffer_data;
  			dword byte_offset;
				ERTFS_ASSERT(pfscntxt->user_index_buffer_size_sectors>=pjournal->replacement_sectors_dirty);
				/* Bug fix July 2012 - Don't update the dirty count if we overwrote and already mapped block. */
				if (!map_info.is_mapped)
					pjournal->replacement_sectors_dirty += map_info.n_replacement_blocks;
				ERTFS_ASSERT(pfscntxt->user_index_buffer_size_sectors>=pjournal->replacement_sectors_dirty);
				n_replacement_bytes = (int) (map_info.n_replacement_blocks * pfscntxt->pdrive->drive_info.bytespsector);
				byte_offset = pfscntxt->pdrive->drive_info.bytespsector;
				byte_offset *= (map_info.journal_blockno - pjournal->buffer_sector_start);
				pbuffer_data = (byte *)pjournal->journal_buffer_start;
				pbuffer_data += byte_offset;
	  			copybuff(pbuffer_data,data,n_replacement_bytes);
			}
			else
			{
            	/* Write to the journal
            		Check for map_info.n_replacement_blocks == 0 first
            		should never happen but would be an endless loop so guard */
            	RTFS_DEBUG_LOG_DEVIO(pfscntxt->pdrive, RTFS_DEBUG_IO_J_REMAP_WRITE, (pfscntxt->nv_buffer_handle+map_info.journal_blockno), map_info.n_replacement_blocks)

            	if (!map_info.n_replacement_blocks ||
                	!failsafe_nv_buffer_io(pfscntxt,
                	map_info.journal_blockno,
                	map_info.n_replacement_blocks,data,FALSE))
                {
                	rtfs_set_errno(PEFSIOERRORWRITEJOURNAL, __FILE__, __LINE__);
                	return(FALSE);
                }
                UPDATE_FSRUNTIME_STATS(pfscntxt, journal_data_writes, 1)
                UPDATE_FSRUNTIME_STATS(pfscntxt, journal_data_blocks_written, map_info.n_replacement_blocks)
			}
            blockno += map_info.n_replacement_blocks;
            data += (map_info.n_replacement_blocks << pdrive->drive_info.log2_bytespsec);
            n_blocks_left -= map_info.n_replacement_blocks;
        }
        /* If we have an open map block queue it */
        if (pbm)
        {
            pjournal->open_blockmap_cache =
                            fs_insert_map_record(pjournal->open_blockmap_cache,pbm);
            pbm = 0;
        }
        /* Note that the journal has data and needs flush */
        pdrive->drive_info.drive_operating_flags |= DRVOP_FS_NEEDS_FLUSH;
        return(TRUE);
    }
}


static BOOLEAN fs_devio_read(FAILSAFECONTEXT *pfscntxt, DDRIVE *pdrive, dword blockno, dword nblocks, byte *data, BOOLEAN in_fat)
{
FSMAPREQ map_info;
dword loop_guard  = GUARD_ENDLESS;

    /* Read from the journal file if enabled and the block is mapped */
    if (!pfscntxt)
    {
    BOOLEAN ret_val;
#if (INCLUDE_DEBUG_RUNTIME_STATS)
        if (in_fat)
        {
            UPDATE_RUNTIME_STATS(pdrive, fat_reads, 1)
            UPDATE_RUNTIME_STATS(pdrive, fat_blocks_read, nblocks)
        }
        else
        {
            if (nblocks==1)
            {
                UPDATE_RUNTIME_STATS(pdrive, dir_buff_reads, 1)
            }
            else
            {
                UPDATE_RUNTIME_STATS(pdrive, dir_direct_reads, 1)
                UPDATE_RUNTIME_STATS(pdrive, dir_direct_blocks_read, nblocks)
            }
        }
#else
    RTFS_ARGSUSED_INT((int) in_fat);
    RTFS_ARGSUSED_PVOID((void *) pdrive);
#endif
         /* not journalling or not mapped so read from the volume */
         ret_val = raw_devio_xfer(pdrive,  blockno, data,
                nblocks, FALSE, TRUE);
        return(ret_val);
    }
    else
    {
        dword n_blocks_left;
        FSJOURNAL *pjournal;

        n_blocks_left = nblocks;
        pjournal = CONTEXT_TO_JOURNAL(pfscntxt);

        while (n_blocks_left)
        {
			if (journal_check_endless_loop(&loop_guard))
				return(FALSE);
            /* Request the status from blockno up to blockno+n_blocks_left */
            map_info.volume_blockno  = blockno;
            map_info.n_blocks = n_blocks_left;

            if (fs_check_mapped_read(pfscntxt, &map_info))
            {
            /*    map_info.n_replacement_blocks - number of contiguous blocks
                  map_info.replacement_blockno  - first contiguous replacement block
                                                  number
                  checking for map_info.n_replacement_blocks==0 (but should never happen)
            */
            	dword journal_blockno;
            	dword n_replacement_blocks;
            	byte *pdata;

                if (!map_info.n_replacement_blocks)
                {
                    rtfs_set_errno(PEFSIOERRORREADJOURNAL, __FILE__, __LINE__);
                    return(FALSE);
                }

                journal_blockno = map_info.journal_blockno;
                n_replacement_blocks = map_info.n_replacement_blocks;
				pdata = data;

				while (n_replacement_blocks)
				{
			  		dword n_to_read;
					if (pfscntxt->user_index_buffer_size_sectors <= 2)
				  		n_to_read = n_replacement_blocks;			/* No buffering */
				  	else if (pjournal->replacement_sectors_dirty == 0)
				  		n_to_read = n_replacement_blocks;			/* Buffering but none are currently buffered */
					else
				  	{	/* We have buffering see if the current sector is in the buffer */
				  		dword buffer_sector_end;

				  		buffer_sector_end = pjournal->buffer_sector_start + pjournal->replacement_sectors_dirty-1;
				  		if (journal_blockno >= pjournal->buffer_sector_start && journal_blockno <= buffer_sector_end)
						{	/* Inside the buffer, copy one sector and continue */
				  			byte *pbuffer_data;
							dword byte_offset;

							byte_offset = pfscntxt->pdrive->drive_info.bytespsector;
							byte_offset *= (journal_blockno - pjournal->buffer_sector_start);
							pbuffer_data = (byte *)pjournal->journal_buffer_start;
							pbuffer_data += byte_offset;
				  			copybuff(pdata, pbuffer_data,pfscntxt->pdrive->drive_info.bytespsector);


				  			journal_blockno += 1;
				  			n_replacement_blocks -= 1;
				  			pdata += pfscntxt->pdrive->drive_info.bytespsector;
				  			n_to_read = 0;
						}
						else if (journal_blockno > buffer_sector_end)
							n_to_read = n_replacement_blocks;			/* All sectors to the right of the buffer */
						else
						{
							if (journal_blockno + n_replacement_blocks > pjournal->buffer_sector_start)
								n_to_read = pjournal->buffer_sector_start - journal_blockno;
							else
								n_to_read = n_replacement_blocks;			/* All sectors to the left of the buffer */
						}
					}
					if (n_to_read)
					{
						RTFS_DEBUG_LOG_DEVIO(pfscntxt->pdrive, RTFS_DEBUG_IO_J_REMAP_READ, (pfscntxt->nv_buffer_handle+journal_blockno), n_to_read)
                    	if (!failsafe_nv_buffer_io(pfscntxt,
                               journal_blockno,
                               n_to_read, pdata,TRUE))
                    	{
                    		rtfs_set_errno(PEFSIOERRORREADJOURNAL, __FILE__, __LINE__);
                    		return(FALSE);
                    	}
                    	UPDATE_FSRUNTIME_STATS(pfscntxt, journal_data_reads, 1)
                    	UPDATE_FSRUNTIME_STATS(pfscntxt, journal_data_blocks_read, n_to_read)
                    	journal_blockno += n_to_read;
				  		n_replacement_blocks -= n_to_read;
				  		pdata += (n_to_read << pdrive->drive_info.log2_bytespsec);
					}
                }
            }
            else
            {
            /*    map_info.n_replacement_blocks - number of contiguous blocks
                  map_info.replacement_blockno  - first block number (will equal)
                                                  the input block no
                  checking for map_info.n_replacement_blocks==0 (but should never happen)
            */
                if (!map_info.n_replacement_blocks ||
                    !raw_devio_xfer(pdrive,  blockno, data,
                    map_info.n_replacement_blocks, FALSE, TRUE))
                    return(FALSE);
#if (INCLUDE_DEBUG_RUNTIME_STATS)
                if (in_fat)
                {
                    UPDATE_RUNTIME_STATS(pdrive, fat_reads, 1)
                    UPDATE_RUNTIME_STATS(pdrive, fat_blocks_read, map_info.n_replacement_blocks)
                }
                else
                {
                    if (nblocks==1)
                    {
                        UPDATE_RUNTIME_STATS(pdrive, dir_buff_reads, 1)
                    }
                    else
                    {
                        UPDATE_RUNTIME_STATS(pdrive, dir_direct_reads, 1)
                        UPDATE_RUNTIME_STATS(pdrive, dir_direct_blocks_read, map_info.n_replacement_blocks)
                    }
                }
#endif
            }
            blockno += map_info.n_replacement_blocks;
            data += (map_info.n_replacement_blocks << pdrive->drive_info.log2_bytespsec);
            n_blocks_left -= map_info.n_replacement_blocks;
        }
    }
    return(TRUE);
}


/* BOOLEAN fs_check_mapped_read(FAILSAFECONTEXT *pfscntxt, FSMAPREQ *preq)

     Check if a range of blocks are mapped.
        Input fields in preq:
            dword blockno     - first block to check
            dword n_blocks    - number of blocks to check

        Output fields in preq:
            BOOLEAN is_mapped - True if the first block is mapped
            dword replacement_blockno - if is_mappped is TRUE contains
                                        the first replacement block number
                                        (the replacement for blockno)
            dword replacement_blockno - if is_mappped is FALSE contains
                                        blockno
            dword n_replacement_blocks  - if is_mappped is TRUE contains
                                        the number of contiguous replacement blocks
                                        up to n_blocks.
                                        - if is_mappped is FALSE contains
                                        the number of contiguous unmapped blocks
                                        up to n_blocks.
        Returns:
            TRUE True if the first block is mapped
            FALSE if the first block is not mapped
*/
static BOOLEAN fs_check_mapped_read(FAILSAFECONTEXT *pfscntxt, FSMAPREQ *preq)
{
FSJOURNAL *pjournal;
FSMAPREQ map_req;
dword unmapped_blocks;

    pjournal = CONTEXT_TO_JOURNAL(pfscntxt);
    /* Scan all map caches. return the first mapped region or return
       the total width of the region that is not mapped */
    map_req = *preq;
    unmapped_blocks = map_req.n_blocks;
    fs_scan_map_cache(pjournal->open_blockmap_cache, &map_req);
    if (!map_req.is_mapped)
    {
        unmapped_blocks = map_req.n_replacement_blocks;
        if (pjournal->flushed_blockmap_cache)
        {
            map_req = *preq;
            fs_scan_map_cache(pjournal->flushed_blockmap_cache, &map_req);
            if (!map_req.is_mapped)
            {
                if (map_req.n_replacement_blocks < unmapped_blocks)
                    unmapped_blocks = map_req.n_replacement_blocks;
            }
        }
    }
    if (!map_req.is_mapped)
    {
        if (pjournal->restoring_blockmap_cache)
        {
            map_req = *preq;
            fs_scan_map_cache(pjournal->restoring_blockmap_cache, &map_req);
            if (!map_req.is_mapped)
            {
                if (map_req.n_replacement_blocks < unmapped_blocks)
                    unmapped_blocks = map_req.n_replacement_blocks;
            }
        }
    }
    if (map_req.is_mapped)
    {
        *preq = map_req;
#if (FS_DEBUG)
        FS_DEBUG_SHOWINT("fs_check_mapped_read  :", preq->volume_blockno)
        FS_DEBUG_SHOWINT(" - to block  :", preq->volume_blockno+preq->n_replacement_blocks-1)
        FS_DEBUG_SHOWINTNL(" remapped to :", preq->journal_blockno)
#endif
    }
    else
    {
        /* Fall through to none mapped state */
        preq->is_mapped = FALSE;
        preq->journal_blockno = preq->volume_blockno;
        preq->n_replacement_blocks = unmapped_blocks;
    }
    return(preq->is_mapped);
}

void fs_free_blockmap(FSJOURNAL *pjournal,FSBLOCKMAP *pbm)
{
    /* Allocate a replacement record */
    if (pbm)
    {
        pbm->pnext = pjournal->blockmap_freelist;
        pjournal->blockmap_freelist = pbm;
        pjournal->num_blockmaps_used -= 1;
    }
}


static FSBLOCKMAP *fs_alloc_blockmap(FSJOURNAL *pjournal)
{
FSBLOCKMAP *pbm;
    /* Allocate a replacement record */
    pbm = pjournal->blockmap_freelist;
    if (pbm)
    {
        pjournal->blockmap_freelist = pbm->pnext;
        pjournal->num_blockmaps_used += 1;
        /* Keep track of worst case blockmap allocation */
        if (pjournal->num_blockmaps_used > pjournal->max_blockmaps_used)
        {
            pjournal->max_blockmaps_used = pjournal->num_blockmaps_used;
        }
    }
    return(pbm);
}


static BOOLEAN fs_journal_alloc_replacement_blocks(FAILSAFECONTEXT *pfscntxt, FSMAPREQ *preq)
{
dword n_remap_blocks;
FSJOURNAL *pjournal;

    pjournal = CONTEXT_TO_JOURNAL(pfscntxt);

	/* July 2012 bug fix - Open a new frame if we are buffering Failsafe data and the number of dirty sectors
	   is equal to the frame size.  */
	if (pfscntxt->user_index_buffer_size_sectors > 2)
	{
		ERTFS_ASSERT(pfscntxt->user_index_buffer_size_sectors>=pjournal->replacement_sectors_dirty);
		/* First flush and open a new frame if the buffer is full */
		if (pjournal->replacement_sectors_dirty >= pfscntxt->user_index_buffer_size_sectors)
		{
			if (!fs_file_open_new_frame(pfscntxt,TRUE))
				return(FALSE);
		}
		/* Clip the replacement range to the end of our bufferable range */
		if (pjournal->replacement_sectors_dirty+preq->n_blocks > pfscntxt->user_index_buffer_size_sectors)
		{
			preq->n_blocks=pfscntxt->user_index_buffer_size_sectors-pjournal->replacement_sectors_dirty;
		}
		ERTFS_ASSERT(pfscntxt->user_index_buffer_size_sectors>=pjournal->replacement_sectors_dirty);
	}
    /* If the current frame is full, start a new one */
    if (!pjournal->open_current_free)
    {
        if (!fs_file_open_new_frame(pfscntxt,TRUE))
            return(FALSE);
    }

    /* Get the number of blocks available in the current frame */
    n_remap_blocks = pjournal->open_current_free;
    if (n_remap_blocks > preq->n_blocks)
        n_remap_blocks = preq->n_blocks;

    if (pjournal->frames_free < n_remap_blocks)
    {
        rtfs_set_errno(PEFSJOURNALFULL, __FILE__, __LINE__);
        return(FALSE);
    }
    /* First replacement block in the range */
    preq->journal_blockno = 1 + pjournal->open_current_frame + pjournal->open_current_used;
    preq->n_replacement_blocks = n_remap_blocks;
    pjournal->open_current_used += n_remap_blocks;
    pjournal->open_current_free -= n_remap_blocks;
    pjournal->frames_free -= n_remap_blocks;
    /* Update low water stats */
    fs_check_file_lowwater(pfscntxt, pjournal);
    pjournal->next_free_frame += n_remap_blocks;

    {
    dword i, blockno;
        i = pjournal->open_current_index;
        pjournal->open_current_index += n_remap_blocks;
        blockno = preq->volume_blockno;
        while(n_remap_blocks--)
        {
            *(pjournal->open_index_buffer+i) = blockno;
            i++;
            pjournal->open_current_checksum += blockno;
            blockno += 1;
        }
    }
    return(TRUE);
}

static BOOLEAN fs_flush_current_session(FAILSAFECONTEXT *pfscntxt)
{
FSJOURNAL *pjournal;
dword loop_guard = GUARD_ENDLESS;

    pjournal = CONTEXT_TO_JOURNAL(pfscntxt);

    if (pjournal->open_remapped_blocks)
    {
#if (FS_DEBUG)
            FS_DEBUG_SHOWINT("Flushing frame         :", pjournal->open_current_frame)
            FS_DEBUG_SHOWINTNL(" - to block  :", pjournal->open_current_frame+pjournal->open_current_used-1)
#endif
        pjournal->session_current_checksum += pjournal->open_current_checksum;
        *(pjournal->open_index_buffer+FS_FRAME_OFFSET_TYPE)             = FS_FRAME_TYPE_FLUSHED;
        *(pjournal->open_index_buffer+FS_FRAME_OFFSET_FRAME_SEQUENCE)  = pjournal->frame_sequence;
        *(pjournal->open_index_buffer+FS_FRAME_OFFSET_SESSION_ID)       = pfscntxt->session_id;
        *(pjournal->open_index_buffer+FS_FRAME_OFFSET_FRAME_RECORDS)    = pjournal->open_current_used;
        *(pjournal->open_index_buffer+FS_FRAME_OFFSET_SEGMENT_CHECKSUM) = pjournal->session_current_checksum;
        *(pjournal->open_index_buffer+FS_FRAME_OFFSET_FRAME_CHECKSUM)   = pjournal->open_current_checksum;
        *(pjournal->open_index_buffer+FS_FRAME_OFFSET_FAT_FREESPACE)    = pjournal->session_current_freespace;

        if (!fs_flush_current_frame(pfscntxt,RTFS_DEBUG_IO_J_INDEX_FLUSH))
            return(FALSE);
        /* If the journal is hidden in the volume, write the cluster of the journal start to a reserved
           cluster in the FAT */
        if (!failsafe_nv_buffer_mark(pfscntxt, (byte *) pjournal->journal_buffer_start))
            return(FALSE);

        /* Keep track of the first flushed record */
        if (!pjournal->flushed_start_record)
            pjournal->flushed_start_record = pjournal->open_start_record;
        /* Keep track of the last flushed record and block */
        pjournal->flushed_terminal_record = pjournal->open_current_frame;
        pjournal->flushed_last_block = pjournal->open_current_frame+pjournal->open_current_used;
        /* update counters */
        pjournal->flushed_remapped_blocks += pjournal->open_remapped_blocks;

#if (INCLUDE_DEBUG_RUNTIME_STATS)
        pfscntxt->stats.frames_flushed += (pfscntxt->stats.frames_closed + 1);
        pfscntxt->stats.frames_closed = 0;
#endif
        /* Move all open block maps to the flushed queue
           but first invalidate existing overlaps */
        if (pjournal->open_blockmap_cache)
        {
        struct fsblockmap *pbm, *pbm_next;

            pbm = pjournal->open_blockmap_cache;
            while (pbm)
            {
				if (journal_check_endless_loop(&loop_guard))
					return(FALSE);
                /* Volume blocks that are currently remapped */
                fs_invalidate_flushed_blocks(pjournal, pbm->volume_blockno, pbm->n_replacement_blocks);
                pbm = pbm->pnext;
            }
            pbm = pjournal->open_blockmap_cache;
            while (pbm)
            {
				if (journal_check_endless_loop(&loop_guard))
					return(FALSE);
                pbm_next = pbm->pnext;
                pjournal->flushed_blockmap_cache =
                    fs_insert_map_record(pjournal->flushed_blockmap_cache,pbm);
                pbm = pbm_next;
            }
            pjournal->open_blockmap_cache = 0;
        }
        /* Move all open fragments_to_free to the flushed queue */
        if (pjournal->open_free_fragments)
        {
            REGION_FRAGMENT   *pf;
            pf = pjournal->open_free_fragments;
            while(pf->pnext)
			{
				if (journal_check_endless_loop(&loop_guard))
					return(FALSE);
                pf = pf->pnext;
			}
            pf->pnext = pjournal->flushed_free_fragments;
            pjournal->flushed_free_fragments = pjournal->open_free_fragments;
            pjournal->open_free_fragments = 0;
        }
#if (FS_DEBUG)
        fs_show_journal_session(pfscntxt);
#endif

        /* Clear the open current variables so we create a new frame next time we write */
		pjournal->replacement_sectors_dirty =  0;
        pjournal->open_current_frame = 0;
        pjournal->buffer_sector_start = 0;
        pjournal->open_current_free = 0;
        pjournal->open_current_index = 0;
        pjournal->open_current_used = 0;
        pjournal->open_remapped_blocks = 0;
    }
    return(TRUE);
}

static BOOLEAN fs_file_open_new_frame(FAILSAFECONTEXT *pfscntxt, BOOLEAN close_current)
{
FSJOURNAL *pjournal;
FSRESTORE *prestore;
dword blocks_to_end;
    pjournal = CONTEXT_TO_JOURNAL(pfscntxt);
    prestore = CONTEXT_TO_RESTORE(pfscntxt);

	/* July 2012 Removed setting session_current_freespace in fs_file_open_new_frame
	   It should not be updated everytime. This was a bug that caused restore to get
	   a missmatch when comparing the volume freespace with the session freespace.
	*/
    if (close_current)
    {
        if (pjournal->open_current_frame)
        {   /* Flush the current frame if there is one */
#if (FS_DEBUG)
            FS_DEBUG_SHOWINT("closing frame         :", pjournal->open_current_frame)
            FS_DEBUG_SHOWINTNL(" - to block  :", pjournal->open_current_frame+pjournal->open_current_used)
            FS_DEBUG_SHOWINTNL(" frames free == :", pjournal->frames_free)
#endif

            pjournal->session_current_checksum += pjournal->open_current_checksum;
            *(pjournal->open_index_buffer+FS_FRAME_OFFSET_TYPE)             = FS_FRAME_TYPE_CLOSED;
            *(pjournal->open_index_buffer+FS_FRAME_OFFSET_FRAME_SEQUENCE)  = pjournal->frame_sequence;
            *(pjournal->open_index_buffer+FS_FRAME_OFFSET_SESSION_ID)       = pfscntxt->session_id;
            *(pjournal->open_index_buffer+FS_FRAME_OFFSET_FRAME_RECORDS)    = pjournal->open_current_used;
            *(pjournal->open_index_buffer+FS_FRAME_OFFSET_SEGMENT_CHECKSUM) = pjournal->session_current_checksum;
            *(pjournal->open_index_buffer+FS_FRAME_OFFSET_FRAME_CHECKSUM)   = pjournal->open_current_checksum;
            *(pjournal->open_index_buffer+FS_FRAME_OFFSET_FAT_FREESPACE)    = pjournal->session_current_freespace;
            if (!fs_flush_current_frame(pfscntxt,RTFS_DEBUG_IO_J_INDEX_CLOSE))
                return(FALSE);
            UPDATE_FSRUNTIME_STATS(pfscntxt, frames_closed, 1)
        }
    }
    if (pjournal->frames_free < 2)
        goto full_journal;

    if (pjournal->next_free_frame >= pfscntxt->journal_file_size)
        blocks_to_end = 0;
    else
        blocks_to_end = pfscntxt->journal_file_size - pjournal->next_free_frame;

    /* Changed 10/01/2009 - added test so we do not reserve a sector at the end if we are just starting
	   this was causing a drop in the apparent file size by one sector each time the condition occured.

    changed from: if (blocks_to_end == 1)
	to:
    			if (blocks_to_end == 1 && close_current && pjournal->open_current_frame)
	*/
    if (blocks_to_end == 1 && close_current)
    {
        /* Write an empty closed record */
        pjournal->open_current_frame = pjournal->next_free_frame;
        pjournal->buffer_sector_start = pjournal->open_current_frame;

        pjournal->frame_sequence += 1;
#if (FS_DEBUG)
        FS_DEBUG_SHOWINT("writing NULL frame         :", pjournal->open_current_frame)
        FS_DEBUG_SHOWINTNL(" - to block  :", pjournal->open_current_frame+pjournal->open_current_used)
        fs_show_journal_session(pfscntxt);
#endif
        rtfs_memset(pjournal->open_index_buffer,0,pfscntxt->fs_frame_size*4);
        *(pjournal->open_index_buffer+FS_FRAME_OFFSET_TYPE)             = FS_FRAME_TYPE_CLOSED;
        *(pjournal->open_index_buffer+FS_FRAME_OFFSET_FRAME_SEQUENCE)  = pjournal->frame_sequence;
        *(pjournal->open_index_buffer+FS_FRAME_OFFSET_SESSION_ID)       = pfscntxt->session_id;
        *(pjournal->open_index_buffer+FS_FRAME_OFFSET_FRAME_RECORDS)    = 0;
        *(pjournal->open_index_buffer+FS_FRAME_OFFSET_SEGMENT_CHECKSUM) = pjournal->session_current_checksum;
        *(pjournal->open_index_buffer+FS_FRAME_OFFSET_FRAME_CHECKSUM)   = 0;
        *(pjournal->open_index_buffer+FS_FRAME_OFFSET_FAT_FREESPACE)    = pjournal->session_current_freespace;
		pjournal->replacement_sectors_dirty =  1;	/* Force a write of just the first sector */
        if (!fs_flush_current_frame(pfscntxt,RTFS_DEBUG_IO_J_INDEX_CLOSE_EMPTY))
            return(FALSE);
        pjournal->open_current_frame = 0;
        pjournal->buffer_sector_start = 0;
        pjournal->frames_free -= 1;  /* Consume discarded frame */
        if (pjournal->frames_free < 2)
        {
full_journal:
			pjournal->open_current_frame = 0;
			pjournal->open_remapped_blocks = 0;
            rtfs_set_errno(PEFSJOURNALFULL, __FILE__, __LINE__);
            return(FALSE);
        }
        /* Update low water stats */
        fs_check_file_lowwater(pfscntxt, pjournal);
    }

    if (blocks_to_end <= 1)
    { /* Bug fix, 1-9-07 was not wrapping on blocks_to_end == 0 */
        pjournal->wrap_counter += 1;
        pjournal->next_free_frame = 1;
        blocks_to_end = pjournal->frames_free;
    }


    /* blocks to end must hold the next frame header plus replacement blocks */
	/* Set up buffering for this frame */
	{
    	dword frame_max_records;
    	/* Set the dirty count to 1 so we write the index */
		pjournal->replacement_sectors_dirty =  1;
    	if (pfscntxt->user_index_buffer_size_sectors > 2)
		{  /* If buffering for replacement sectors is provided set the buffer pointer and set frame max records to the number of
		      sectors that will fit in the buffer */
    		/* Set up for a normal frame, the first sector of the buffer contains indeces, the rest are for replacement sector buffering */
    		pjournal->open_index_buffer = pjournal->journal_buffer_start;
    		frame_max_records = pfscntxt->user_index_buffer_size_sectors - 1;
    		if (!close_current)
			{ /* If just starting the buffer has been initialized with the master info so advance 1 sector */
			byte *b;
    			frame_max_records -= 1;
				b =  (byte *) pjournal->journal_buffer_start;
				b +=  pfscntxt->pdrive->drive_info.bytespsector;
    			pjournal->replacement_sectors_dirty +=  1;
				ERTFS_ASSERT(pfscntxt->user_index_buffer_size_sectors>=pjournal->replacement_sectors_dirty);
    			pjournal->open_index_buffer = (dword *) b;
				/* Now the first sector contains the master the second contains indices and the rest are replacements */
			}

			if (frame_max_records > pfscntxt->fs_frame_max_records)
				frame_max_records = pfscntxt->fs_frame_max_records;
			/* July 2012 fix, we were previously zeroing more bytes than we needed, was not a bug */
			rtfs_memset((byte *) pjournal->open_index_buffer, 0, pfscntxt->pdrive->drive_info.bytespsector);
		}
		else
			frame_max_records = pfscntxt->fs_frame_max_records;

        if (blocks_to_end > frame_max_records)
        	pjournal->open_current_free = frame_max_records;
        else
        	pjournal->open_current_free = blocks_to_end-1;

	}

	/* Set the sector buffer start variable, if we closed a frame we are not on the first frame so set the buffer edge the the sector
	   containing the index. Otherwise we are just starting and the file header is included so the buffer starts at zero. */
    pjournal->open_current_frame = pjournal->next_free_frame;
	if (close_current)
		pjournal->buffer_sector_start = pjournal->open_current_frame; /* start at index sector for the frame */
	else
		pjournal->buffer_sector_start = 0; /* start at the begining */
    pjournal->next_free_frame += 1;
    pjournal->frames_free -= 1;

    /* Update low water stats */
    fs_check_file_lowwater(pfscntxt, pjournal);

    if (!pjournal->open_start_record)
        pjournal->open_start_record = pjournal->open_current_frame;

    /* If we are allocating records that overwrite the session start_record that's stored in
       the header, then update the header */
    if (pjournal->wrap_counter &&
        pjournal->open_current_frame <= pjournal->session_start_record &&
        pjournal->open_current_frame+pjournal->open_current_free >= pjournal->session_start_record)
    {
        /* If we are restoring previously flushed records, make the start point the new
           session start record, otherwise if we are currently flushing records
           make the start point the new session start record, otherwise make
           the new start record the first open record that has not yet been flushed */
            if (prestore->restoring_start_record)
                pjournal->session_start_record = prestore->restoring_start_record;
            else if (pjournal->flushed_start_record)
                pjournal->session_start_record = pjournal->flushed_start_record;
            else
                pjournal->session_start_record = pjournal->open_start_record;

            /* Read the master block and update the start record */

            RTFS_DEBUG_LOG_DEVIO(pfscntxt->pdrive, RTFS_DEBUG_IO_J_ROOT_READ, (pfscntxt->nv_buffer_handle+0), 1)
            if (!failsafe_nv_buffer_io(pfscntxt, 0, 1,
                (byte *) pjournal->journal_buffer_start,TRUE))
                return(FALSE);
            UPDATE_FSRUNTIME_STATS(pfscntxt, journal_index_reads, 1)
            *(pjournal->open_index_buffer+FS_MASTER_OFFSET_START_RECORD) = pjournal->session_start_record;
            RTFS_DEBUG_LOG_DEVIO(pfscntxt->pdrive, RTFS_DEBUG_IO_J_ROOT_WRITE, (pfscntxt->nv_buffer_handle+0), 1)
            if (!failsafe_nv_buffer_io(pfscntxt, 0, 1,
                (byte *) pjournal->journal_buffer_start,FALSE))
                return(FALSE);
            UPDATE_FSRUNTIME_STATS(pfscntxt, journal_index_writes, 1)
			/* Zero the section of buffer we just used */
			rtfs_memset((byte *) pjournal->journal_buffer_start, 0, pfscntxt->pdrive->drive_info.bytespsector);
    }

    /* Initialize frame variables */
    pjournal->frame_sequence += 1;
    pjournal->open_current_checksum = 0;
    pjournal->open_current_used = 0;
    rtfs_memset(pjournal->open_index_buffer,0,pfscntxt->fs_frame_size*4);
    *(pjournal->open_index_buffer+FS_FRAME_OFFSET_TYPE) = FS_FRAME_TYPE_OPEN;
    *(pjournal->open_index_buffer+FS_FRAME_OFFSET_FRAME_SEQUENCE)  = pjournal->frame_sequence;
    *(pjournal->open_index_buffer+FS_FRAME_OFFSET_SESSION_ID) = pfscntxt->session_id;
    *(pjournal->open_index_buffer+FS_FRAME_OFFSET_SEGMENT_CHECKSUM) = pjournal->session_current_checksum;
    *(pjournal->open_index_buffer+FS_FRAME_OFFSET_FRAME_CHECKSUM) = pjournal->open_current_checksum;
    *(pjournal->open_index_buffer+FS_FRAME_OFFSET_FRAME_RECORDS) = pjournal->open_current_used;
    *(pjournal->open_index_buffer+FS_FRAME_OFFSET_FAT_FREESPACE)  = pjournal->session_current_freespace;
    pjournal->open_current_index = FS_FIRST_INDEX_IN_FRAME;
    pjournal->open_current_checksum = 0;
#if (FS_DEBUG)
    FS_DEBUG_SHOWINT("fs_file_open_new_frame:", pjournal->open_current_frame)
    FS_DEBUG_SHOWINTNL(" - to block  :", pjournal->open_current_frame+pjournal->open_current_free);
#endif

	/* Flush the frame unless we are buffering the whole frame */
   	if (pfscntxt->user_index_buffer_size_sectors <= 2)
	{
    	pjournal->replacement_sectors_dirty = 1;
    	if (!fs_flush_current_frame(pfscntxt,RTFS_DEBUG_IO_J_INDEX_OPEN))
        	return(FALSE);
	}
    return(TRUE);
}
#if (FS_DEBUG)
static void fs_show_blockmap_list(char *list_name, FSBLOCKMAP *pbm)
{
dword loop_guard = GUARD_ENDLESS;

    if (!pbm)
        return;
    FS_DEBUG_SHOW("start: ")
    FS_DEBUG_SHOWNL(list_name)
    while(pbm)
    {
		if (journal_check_endless_loop(&loop_guard))
			return;
        FS_DEBUG_SHOWINT("mapping volume blocks :", pbm->volume_blockno)
        FS_DEBUG_SHOWINT(" - to :", pbm->volume_blockno+pbm->n_replacement_blocks)
        FS_DEBUG_SHOWINT("  remapped to :", pbm->journal_blockno)
        FS_DEBUG_SHOWINTNL(" - to :", pbm->journal_blockno+pbm->n_replacement_blocks)
        pbm = pbm->pnext;
    }
    FS_DEBUG_SHOW("end: ")
    FS_DEBUG_SHOWNL(list_name)
}
void fs_show_journal_session(FAILSAFECONTEXT *pfscntxt)
{
FSJOURNAL *pjournal;
    pjournal = CONTEXT_TO_JOURNAL(pfscntxt);

    FS_DEBUG_SHOWINT  ("session_start_record     :",pjournal->session_start_record)
    FS_DEBUG_SHOWINTNL("        session_current_freespace:",pjournal->session_current_freespace)
    FS_DEBUG_SHOWINT  ("frames_free low water    :",pfscntxt->min_free_blocks);
    FS_DEBUG_SHOWINT  ("frames_free              :",pjournal->frames_free)
    FS_DEBUG_SHOWINTNL("        next_free_frame          :",pjournal->next_free_frame)
    FS_DEBUG_SHOWINT  ("open_start_record        :",pjournal->open_start_record)
    FS_DEBUG_SHOWINTNL("        wrap_counter               :",pjournal->wrap_counter)
    FS_DEBUG_SHOWINT  ("open_current_frame       :",pjournal->open_current_frame)
    FS_DEBUG_SHOWINTNL("        open_current_index       :",pjournal->open_current_index)
    FS_DEBUG_SHOWINT  ("open_current_free        :",pjournal->open_current_free)
    FS_DEBUG_SHOWINTNL("        open_current_used        :",pjournal->open_current_used)
    FS_DEBUG_SHOWINT  ("open_remapped_blocks     :",pjournal->open_remapped_blocks)
    FS_DEBUG_SHOWINTNL("        flushed_remapped_blocks  :",pjournal->flushed_remapped_blocks)
    FS_DEBUG_SHOWINT  ("restoring_remapped_blocks:",pjournal->restoring_remapped_blocks)
    FS_DEBUG_SHOWINTNL("        flushed_start_record     :",pjournal->flushed_start_record)
    FS_DEBUG_SHOWINT  ("flushed_terminal_record  :",pjournal->flushed_terminal_record)
    FS_DEBUG_SHOWINTNL("        flushed_last_block       :",pjournal->flushed_last_block)
    FS_DEBUG_SHOWINT  ("num_blockmaps_used       :",pjournal->num_blockmaps_used)
    FS_DEBUG_SHOWINTNL("        max_blockmaps_used       :",pjournal->max_blockmaps_used)
    fs_show_blockmap_list("open remapps    :",pjournal->open_blockmap_cache);
    fs_show_blockmap_list("flushed remaps  :", pjournal->flushed_blockmap_cache);
    fs_show_blockmap_list("restoring remaps:",pjournal->restoring_blockmap_cache);
}
#endif

static BOOLEAN fs_flush_current_frame(FAILSAFECONTEXT *pfscntxt, int ioframetype)
{
FSJOURNAL *pjournal;
dword sector_to_write;
dword nsectors_to_write;
byte *pdata_to_write;
    RTFS_ARGSUSED_INT((int) ioframetype); /* If diagnostics are disabled */

    pjournal = CONTEXT_TO_JOURNAL(pfscntxt);
	/* Flush the frame unless we are buffering the whole frame */
   	if (pfscntxt->user_index_buffer_size_sectors <= 2)
	{
    	nsectors_to_write = 1;
    	sector_to_write = pjournal->open_current_frame;
    	pdata_to_write = (byte *) pjournal->open_index_buffer;

	}
	else
	{
    	nsectors_to_write = pjournal->replacement_sectors_dirty;
    	sector_to_write = pjournal->buffer_sector_start;
    	pdata_to_write = (byte *) pjournal->journal_buffer_start;
		ERTFS_ASSERT(pfscntxt->user_index_buffer_size_sectors>=nsectors_to_write)
        ERTFS_ASSERT(nsectors_to_write > 0)
        if (nsectors_to_write < 1)
			return(TRUE);
	}
    pjournal->replacement_sectors_dirty = 0;
    RTFS_DEBUG_LOG_DEVIO(pfscntxt->pdrive, ioframetype, (pfscntxt->nv_buffer_handle+sector_to_write), nsectors_to_write)

    /* Write to the journal */
    if (failsafe_nv_buffer_io(pfscntxt,
                sector_to_write, nsectors_to_write,
                pdata_to_write,FALSE))
    {
        UPDATE_FSRUNTIME_STATS(pfscntxt, journal_index_writes, 1)
        return(TRUE);
    }
    else
    {
        rtfs_set_errno(PEFSIOERRORWRITEJOURNAL, __FILE__, __LINE__);
        return(FALSE);
    }
}

static void fs_invalidate_flushed_blocks(FSJOURNAL *pjournal, dword first_block, dword n_blocks)
{
FSBLOCKMAP *pscan,*pprev;
dword last_block,last_scan_block;
dword loop_guard = GUARD_ENDLESS;

    last_block = first_block + n_blocks-1;
    pscan = pjournal->flushed_blockmap_cache;
    pprev = 0;
    while (pscan)
    {
		if (journal_check_endless_loop(&loop_guard))
			return;
        last_scan_block = pscan->volume_blockno+pscan->n_replacement_blocks -1;
        if (pscan->volume_blockno > last_block) /* Done ? */
            return;
        else if (first_block > last_scan_block)
        { /* We're left, no overlap yet */
           pprev = pscan;
           pscan = pscan->pnext;
        }
        else
        { /* We overlap, discard replacements contained in [first_block,last_block] */
        dword first_keeper, last_keeper;
            first_keeper = pscan->volume_blockno;
            last_keeper  = last_scan_block;
            /* If our start is to the left of his start and our end is to
               the right of his start then his first valid block will be
               to the right of our end */
            if (first_block <= pscan->volume_blockno)
                first_keeper = last_block+1;
            /* If our end is to the right of his end and our start is to the the left of
               his end, then his last valid block will be one before our start */
            if (last_block >= last_scan_block)
                last_keeper = first_block-1;
            if (last_keeper < first_keeper)
            { /* we consumed the whole thing */
                if (pprev)
                    pprev->pnext = pscan->pnext;
                else
                    pjournal->flushed_blockmap_cache = pscan->pnext;
                fs_free_blockmap(pjournal,pscan);
            }
            else
            {
                pscan->journal_blockno += (first_keeper - pscan->volume_blockno);
                pscan->volume_blockno = first_keeper;
                pscan->n_replacement_blocks = last_keeper-first_keeper + 1;
            }
            /* Process the list again until we go all the way through */
            fs_invalidate_flushed_blocks(pjournal, first_block, n_blocks);
            break;
        }
    }
}
/* Insert a replacement record in a sorted list return the new
   beginning of list */
static FSBLOCKMAP *fs_insert_map_record(FSBLOCKMAP *p_list, FSBLOCKMAP *p_this)
{
FSBLOCKMAP *pscan,*pprev;
dword last_blockno;
dword loop_guard = GUARD_ENDLESS;

    p_this->pnext = 0;
    last_blockno = p_this->volume_blockno + p_this->n_replacement_blocks-1;
    pscan = p_list;
    pprev = 0;
    while (pscan)
    {
		if (journal_check_endless_loop(&loop_guard))
			return(0);
        if (last_blockno < pscan->volume_blockno)
        {   /* If at the end or the block we are inserting
               is before then the current value */
            p_this->pnext = pscan;
            if (pprev)
                pprev->pnext = p_this;
            else
                p_list = p_this;
            return(p_list);
       }
       else
       {
           pprev = pscan;
           pscan = pscan->pnext;
       }
    }
    /* If we get here we have to put it at the end of the list */
    if (pprev)
        pprev->pnext = p_this;
    else
        p_list = p_this;
    return(p_list);
}



/* fs_scan_map_cache(FAILSAFECONTEXT *pfscntxt, FSMAPREQ *preq)

    Check if a range of blocks are mapped in the cache
    Input fields in preq:
        dword blockno     - first block to check
        dword n_blocks    - number of blocks to check

    Output fields in preq:
        BOOLEAN is_mapped - True if the first block is mapped
        dword replacement_blockno - if is_mappped is TRUE contains
                                    the first replacement block number
                                    (the replacement for blockno)
        dword replacement_blockno - if is_mappped is FALSE contains
                                    blockno
        dword n_replacement_blocks  - if is_mappped is TRUE contains
                                    the number of contiguous replacement blocks
                                    up to n_blocks.
                                    - if is_mappped is FALSE contains
                                    the number of contiguous unmapped blocks
                                    up to n_blocks.

    Returns:
        TRUE - Always
*/
static BOOLEAN fs_scan_map_cache(struct fsblockmap *pbm, FSMAPREQ *preq)
{
dword last_blockno;
dword loop_guard  = GUARD_ENDLESS;

    preq->adjacent_pbm = 0;
    preq->is_mapped = FALSE;
    while (pbm)
    {
		if (journal_check_endless_loop(&loop_guard))
			return(FALSE);
        if (pbm->volume_blockno > preq->volume_blockno)
        { /* We know we are not mapped. return how many */
        dword ltemp;
           preq->journal_blockno = preq->volume_blockno;
           ltemp = pbm->volume_blockno - preq->volume_blockno;
           if (ltemp < preq->n_blocks)
           {
               preq->n_replacement_blocks = ltemp;
           }
           else
               preq->n_replacement_blocks = preq->n_blocks;
            return(TRUE);
        }
        else
        {
            last_blockno = pbm->volume_blockno + pbm->n_replacement_blocks -1;
            if ( pbm->volume_blockno <= preq->volume_blockno &&
                 last_blockno >= preq->volume_blockno)
            {
            dword n_replacement_blocks;
                preq->is_mapped = TRUE;

                /* get the number of blocks between the request block
                  and the end of the record */
                n_replacement_blocks = last_blockno - preq->volume_blockno + 1;
                if (n_replacement_blocks > preq->n_blocks)
                    n_replacement_blocks = preq->n_blocks;
                preq->n_replacement_blocks = n_replacement_blocks;
                /* Get the first replacement block in the record
                   and add the offset to the search block */
                preq->journal_blockno = pbm->journal_blockno;
                preq->journal_blockno += preq->volume_blockno-pbm->volume_blockno;
                return(TRUE);
            }
            if ((last_blockno + 1) == preq->volume_blockno)
                preq->adjacent_pbm = pbm;
            else
                preq->adjacent_pbm = 0;
            pbm = pbm->pnext;
        }
    }
    /* Off the list return all */
    preq->is_mapped = FALSE;
    preq->journal_blockno = preq->volume_blockno;
    preq->n_replacement_blocks = preq->n_blocks;
    return(TRUE);
}

/*
    This routine is called by Rtfs when it has Failed in an attempt to allocate clusters.
    It's job is to try to release clusters that are reserved by Failsafe.
    When this routine returns Rtfs retries the allocation. If enough clusters were released then
    the retry will succeed, but if not, the retry will fail.

    The retry algorithm performs the following steps:
    1. Forces a flush of the Journal file and a sync of the volume
    2. Restarts journaling but with the Journal file size reduced by
       the larger of, half it's original size or the amount requested.
    3. If the shrinking the journal file would reduced it below a practical limit
       then a compile time option determines if journaling should be disabled
       to allow the whole disk to be used for user data.

    The function only fails on an IO error. If enough clusters can't be released
    it returns TRUE, but the higher level allocator fuction will fail and report
    no space.

*/
static BOOLEAN fs_recover_free_clusters(DDRIVE *pdrive, dword required_clusters)
{
    FAILSAFECONTEXT *pfscntxt;
    dword reserved_free_clusters,file_size_clusters,file_size_sectors,first_to_free,file_start_sector;
    FSJOURNAL *pjournal;


    pfscntxt = (FAILSAFECONTEXT *) pdrive->drive_state.failsafe_context;

    /* Nothing to free. return true, subsequant alloc requests will fail */
    if (!pfscntxt)
        return(TRUE);

    pjournal = CONTEXT_TO_JOURNAL(pfscntxt);
    reserved_free_clusters = 0;
    reserved_free_clusters += pc_fraglist_count_clusters(pjournal->open_free_fragments,0);
    reserved_free_clusters += pc_fraglist_count_clusters(pjournal->flushed_free_fragments,0);
    reserved_free_clusters += pc_fraglist_count_clusters(pjournal->restoring_free_fragments,0);

    /* Flush failsafe and synchronize the volume */
    if (!fs_flush_internal(pfscntxt, TRUE))
        return(FALSE);

    /* Check if synchronizing released enough clusters that were freed but not synchronized */
    if (reserved_free_clusters >= required_clusters)
        return(TRUE);

    /* reduce by the amount we freed by synchronizing */
    required_clusters -= reserved_free_clusters;

    /* If not journaling to freespace, we can't help */
    if (pfscntxt->nv_reserved_fragment.start_location < 2)
        return(TRUE);
    file_start_sector  = pfscntxt->nv_buffer_handle;
    /* Get the current size in clusters */
    file_size_clusters = pfscntxt->nv_reserved_fragment.end_location - pfscntxt->nv_reserved_fragment.start_location+1;
    /* file_start_sector should always be non-zero
       If not enough to free. return true, subsequant alloc requests will fail */
    if (!file_start_sector || file_size_clusters <= required_clusters)
         return(TRUE);

    /* Check if journal resizing is supported */
    if (fs_api_cb_check_fail_on_journal_resize(pfscntxt->pdrive->driveno))
    {
        /* Disable journaling if that's the policy.. we'll be able to allocate a few more clusters that way */
        if (fs_api_cb_disable_on_full(pdrive))
            fs_failsafe_disable(pdrive, 0);
        return(TRUE);
    }

    /* Reduce the size by the amount needed */
    file_size_clusters = file_size_clusters - required_clusters;

    file_size_sectors = file_size_clusters << pdrive->drive_info.log2_secpalloc;

    /* Resize the reserved area owned by the journal file */
    first_to_free = pfscntxt->nv_reserved_fragment.start_location + file_size_clusters;
#if (INCLUDE_RTFS_FREEMANAGER)
/*    if(!free_manager_release_clusters(pdrive, first_to_free, file_size_clusters, FALSE))  BUGBUGBUGBUG */
    if(!free_manager_release_clusters(pdrive, first_to_free, required_clusters, FALSE))
        return(FALSE);
#endif
    pfscntxt->nv_reserved_fragment.end_location = first_to_free - 1;

    /* Restart journaling with the smaller journal. pass in journal location and size */
    if (!fs_failsafe_start_journaling(pfscntxt, file_start_sector, file_size_sectors))
        return(FALSE);
     return(TRUE);
}

#endif /* (INCLUDE_FAILSAFE_CODE) */
