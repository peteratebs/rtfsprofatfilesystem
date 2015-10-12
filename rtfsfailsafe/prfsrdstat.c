/*               EBS - RTFS (Real Time File Manager)
* FAILSSAFEV3 -
* Copyright EBS Inc. 1987-2005
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* PRFSRESTORE.C - ERTFS-PRO FailSafe internal restore and synchronize routines

*/
#include "rtfs.h"

#if (INCLUDE_FAILSAFE_CODE)

static void fs_get_fsrdstats(DDRIVE *pdrive, FAILSAFE_RUNTIME_STATS *pstat);

BOOLEAN pc_diskio_failsafe_stats(byte *drive_name, FAILSAFE_RUNTIME_STATS *pstats)
{
int driveno;
DDRIVE *pdr;
BOOLEAN ret_val;
    CHECK_MEM(BOOLEAN, 0)  /* Make sure memory is initted */

    rtfs_clear_errno();  /* clear error status */

    if (!pstats)
    {
        rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
        return(FALSE);
    }
    ret_val = TRUE;
    /* Get the drive and make sure it is mounted   */
    driveno = check_drive_name_mount(drive_name, CS_CHARSET_NOT_UNICODE);
    /* if error check_drive errno was set by check_drive */
    if (driveno >= 0)
    {
        pdr = pc_drno2dr(driveno);
        if (!pdr)
        {
            rtfs_set_errno(PEINVALIDDRIVEID, __FILE__, __LINE__);
            ret_val = FALSE;
        }
        else if (pdr->drive_state.failsafe_context)
            fs_get_fsrdstats(pdr, pstats);
        else
            rtfs_memset(pstats, 0, sizeof(*pstats));
        release_drive_mount(driveno);/* Release lock, unmount if aborted */
    }
    return(ret_val);
}
static void fs_get_fsrdstats(DDRIVE *pdrive, FAILSAFE_RUNTIME_STATS *pstat)
{
FAILSAFECONTEXT *pfscntxt;
FSJOURNAL *pjournal;
FSRESTORE *prestore;
    if (!pdrive->drive_state.failsafe_context)
        return;
    pfscntxt = (FAILSAFECONTEXT *) pdrive->drive_state.failsafe_context;
    prestore = CONTEXT_TO_RESTORE(pfscntxt);
    pjournal = CONTEXT_TO_JOURNAL(pfscntxt);
#if (INCLUDE_DEBUG_RUNTIME_STATS)
    copybuff((void *)pstat, (void *) &pfscntxt->stats, sizeof(*pstat));
#endif

    pstat->journaling_active = 1;
    if (prestore->restoring_start_record)
        pstat->sync_in_progress = 1;
    pstat->journal_file_size    = pfscntxt->journal_file_size;
    pstat->journal_file_used    = pfscntxt->journal_file_size-pjournal->frames_free;
    pstat->journal_max_used     = pfscntxt->journal_file_size-pfscntxt->min_free_blocks;
    pstat->restore_buffer_size  = pfscntxt->user_restore_transfer_buffer_size;
    pstat->num_blockmaps        = pfscntxt->blockmap_size;
    pstat->num_blockmaps_used   = pjournal->num_blockmaps_used;
    pstat->max_blockmaps_used   = pjournal->max_blockmaps_used;
    pstat->cluster_frees_pending=
        pc_fraglist_count_clusters(pjournal->open_free_fragments,0) +
        pc_fraglist_count_clusters(pjournal->flushed_free_fragments,0) +
        pc_fraglist_count_clusters(pjournal->restoring_free_fragments,0);
    pstat->reserved_free_clusters = 0;
    if (pfscntxt->nv_reserved_fragment.start_location)
        pstat->reserved_free_clusters  = pc_fraglist_count_clusters(&pfscntxt->nv_reserved_fragment,0);

    if (pjournal->open_current_frame == 0 &&
        pjournal->open_current_index == 0)
    {
        pstat->current_frame  = pjournal->next_free_frame;
        pstat->current_index  =     pjournal->open_current_index;
    }
    else
    {
        pstat->current_frame  =     pjournal->open_current_frame;
        pstat->current_index  =     pjournal->open_current_index;
    }

    pstat->flushed_blocks = pjournal->flushed_remapped_blocks;
    pstat->open_blocks    = pjournal->open_remapped_blocks +
                            pjournal->open_current_index;

    if (pstat->sync_in_progress)
    {
        if (prestore->restoring_last_block >= prestore->restoring_start_record)
            pstat->restoring_blocks =prestore->restoring_last_block - prestore->restoring_start_record;
        else
            pstat->restoring_blocks = prestore->restoring_last_block +
        pstat->journal_file_size - prestore->restoring_start_record;
        if (prestore->replacement_queue.file_current_index_block >= prestore->restoring_start_record)
              pstat->restored_blocks = prestore->replacement_queue.file_current_index_block -
                                        prestore->restoring_start_record;
        else
            pstat->restored_blocks = prestore->replacement_queue.file_current_index_block +
                    pstat->journal_file_size - prestore->restoring_start_record;
        pstat->current_restoring_block = prestore->replacement_queue.file_current_index_block;
    }
}
#endif
