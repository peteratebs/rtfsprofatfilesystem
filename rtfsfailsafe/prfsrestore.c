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

#define RS_READ_START_MARK              1
#define RS_WRITE_START_MARK             2
#define RS_READ_END_MARK                3
#define RS_WRITE_END_MARK               4
#define RS_START_RESTORE                5
#define RS_START_LOAD_QUEUE             6
#define RS_LOAD_QUEUE                   7
#define RS_READ_REPLACEMENTS            8
#define RS_WRITE_REPLACEMENTS           9
#define RS_CLEAR_LOCATION               10

typedef struct fsframeinfo {
        dword  frame_index;
        BOOLEAN frame_format_error;
        dword  stored_frame_type;
        dword stored_frame_sequence;
        dword  stored_session_id;
        dword  stored_segment_checksum;
        dword  stored_frame_checksum;
        dword  stored_frame_fat_freespace;
        dword  stored_frame_replacement_records;
        dword  *pfirst_replacement_record;
        dword  calculated_frame_checksum;
        } FSFRAMEINFO;


typedef struct fssegmentinfo {
        int    segment_type;
        int    segment_error_type;
        dword  segment_checksum;
        dword  segment_session_id;
        dword  segment_fat_free_space;
        dword  segment_length;
        dword  segment_frames;
        dword  segment_replacement_records;
        dword  segment_starting_index;
        dword  segment_ending_index;
        dword  segment_next_index;
        } FSSEGMENTINFO;

#define FS_SEGMENT_TYPE_WRAP        1
#define FS_SEGMENT_TYPE_FLUSHED     2
#define FS_SEGMENT_TYPE_RESTORING   3
#define FS_SEGMENT_TYPE_RESTORED    4
#define FS_SEGMENT_TYPE_NULL        5
#define FS_SEGMENT_TYPE_ERROR       6


#define FS_SEGMENT_ERROR_FORMAT     1
#define FS_SEGMENT_ERROR_CHECKSUM   2
#define FS_SEGMENT_ERROR_OPEN       3

typedef struct fssessioninfo {
        int    session_type;
        int    session_error_type;
        dword  session_id;
        dword  next_frame_index;
        dword  next_frame_sequence;
        dword  session_fat_free_space;
        dword  session_start_frame;
        dword  session_start_restored;
        dword  session_end_restored;
        dword  session_start_restoring;
        dword  session_end_restoring;
        dword  session_start_flushed;
        dword  session_end_flushed;
        dword  session_replacement_records;
        } FSSESSIONINFO;

#define FS_SESSION_ERROR_FORMAT   FS_SEGMENT_ERROR_FORMAT
#define FS_SESSION_ERROR_CHECKSUM FS_SEGMENT_ERROR_CHECKSUM
#define FS_SESSION_ERROR_OPEN     FS_SEGMENT_ERROR_OPEN

#define FS_SESSION_TYPE_FLUSHED     1
#define FS_SESSION_TYPE_RESTORING   2
#define FS_SESSION_TYPE_RESTORED    3
#define FS_SESSION_TYPE_ERROR       4


typedef struct fsfileinfo {
        BOOLEAN journal_file_valid;
        BOOLEAN no_journal_file_found;
        BOOLEAN not_journal_file;
        BOOLEAN bad_journal_version;
        BOOLEAN check_sum_fails;
        BOOLEAN bad_journal_format;
        BOOLEAN journal_left_open;
        BOOLEAN restore_required;
        BOOLEAN restore_recommended;
        dword journal_file_version;
        dword journal_file_size;
        dword journal_file_start;
        dword journal_file_blocks_remapped;
        dword journal_file_stored_freespace;
        dword journal_start_frame;
        dword journal_start_restoring;
        dword journal_end_restoring;
        dword journal_start_restored;
        dword journal_end_restored;
        dword journal_start_flushed;
        dword journal_end_flushed;
} FSFILEINFO;


static dword fs_file_get_next_frame(FAILSAFECONTEXT *pfscntxt,FSFRAMEINFO *pframe);
static BOOLEAN fs_restore_from_file(FAILSAFECONTEXT *pfscntxt,dword start_record, dword end_record);
static void fs_restore_from_session_complete(FAILSAFECONTEXT *pfscntxt);
static int fs_restore_start(FAILSAFECONTEXT *pfscntxt);
static int fs_restore_continue(FAILSAFECONTEXT *pfscntxt);
static void fs_get_replacements_from_queue(FAILSAFECONTEXT *pfscntxt, dword *plowest_block);
static BOOLEAN fs_load_replacement_records(FAILSAFECONTEXT *pfscntxt);
static dword *fs_restore_map_record(FAILSAFECONTEXT *pfscntxt, dword record);
static BOOLEAN fs_init_restore_buffer(FAILSAFECONTEXT *pfscntxt);
static BOOLEAN fs_load_restore_buffer(FAILSAFECONTEXT *pfscntxt,dword block_no,  dword n_blocks);
static BOOLEAN fs_restore_load_record(FAILSAFECONTEXT *pfscntxt,dword record);
static BOOLEAN fs_restore_write_record(FAILSAFECONTEXT *pfscntxt, dword record, byte *pbuffer);
static BOOLEAN fs_file_frame_info(FAILSAFECONTEXT *pfscntxt,FSFRAMEINFO *pframe, dword frame_index, BOOLEAN do_checksum);
static BOOLEAN fs_restore_file_info(FAILSAFECONTEXT *pfscntxt, FSFILEINFO *pfileinfo);
static void fs_invalidate_replacement_blocks(FSRESTORE *prestore, dword first_block, dword n_blocks);
static FSBLOCKMAP *fs_insert_replacement_record(FSBLOCKMAP *preplacement_list, FSBLOCKMAP *preplacement_this);
static dword fs_blocks_in_region(dword block_no, dword n_blocks, dword fat_start, dword fat_end);
static void fs_free_replacement_record(FSRESTORE *prestore,FSBLOCKMAP *preplacement_this);
static FSBLOCKMAP *fs_alloc_replacement_record(FSRESTORE *prestore);
static BOOLEAN fs_asy_write_to_volume(FAILSAFECONTEXT *pfscntxt);

/* Catch endless loops (if TRUE a cataclismic error) */
#define GUARD_ENDLESS 32768 /* Arbitrarilly large for our purposes */
static BOOLEAN restore_check_endless_loop(dword *loop_guard)
{
	if (*loop_guard == 0)
	{
        FS_DEBUG_SHOWNL("restore - endless loop")
        ERTFS_ASSERT(rtfs_debug_zero())
		return(TRUE);
	}
	*loop_guard = *loop_guard - 1;
	return(FALSE);
}

/* Initialize a buffer with master record information */
void fs_mem_master_init(FAILSAFECONTEXT *pfscntxt, dword *pdw, dword journal_file_size,dword session_id, dword session_start_record)
{
    rtfs_memset(pdw,0, pfscntxt->fs_frame_size*4);
    *(pdw+FS_MASTER_OFFSET_SIGNATURE_1) = FS_MASTER_SIGNATURE_1;
    *(pdw+FS_MASTER_OFFSET_SIGNATURE_2) = FS_MASTER_SIGNATURE_2;
    *(pdw+FS_MASTER_OFFSET_VERSION) = FS_CURRENT_VERSION;
    *(pdw+FS_MASTER_OFFSET_FSIZE) = journal_file_size;
    *(pdw+FS_MASTER_OFFSET_SESSION) = session_id;
    *(pdw+FS_MASTER_OFFSET_START_RECORD) = session_start_record;
}

/* Retrieve master record information from a buffer. Check if legal */
void fs_mem_master_info(dword *pdw,FSMASTERINFO *pmaster)
{
    rtfs_memset(pmaster,0, sizeof(*pmaster));
    pmaster->master_type = FS_MASTER_TYPE_ERROR;
    /* Always grab the session id (whatever was written at this location */
    pmaster->master_file_session_id = *(pdw+FS_MASTER_OFFSET_SESSION);


    if (*(pdw+FS_MASTER_OFFSET_SIGNATURE_1) != FS_MASTER_SIGNATURE_1)
        pmaster->master_error_type = FS_MASTER_ERROR_NOT_FAILSAFE;
    else if (*(pdw+FS_MASTER_OFFSET_SIGNATURE_2) != FS_MASTER_SIGNATURE_2)
        pmaster->master_error_type = FS_MASTER_ERROR_NOT_FAILSAFE;
    else
    {
        pmaster->master_file_version = *(pdw+FS_MASTER_OFFSET_VERSION);
        pmaster->master_file_size = *(pdw+FS_MASTER_OFFSET_FSIZE);
        pmaster->master_start_record = *(pdw+FS_MASTER_OFFSET_START_RECORD);
        if (pmaster->master_file_version != FS_CURRENT_VERSION)
        {
            pmaster->master_error_type = FS_MASTER_ERROR_WRONG_VERSION;
        }
        else
        {
            pmaster->master_error_type = 0;
            pmaster->master_type = FS_MASTER_TYPE_VALID;
        }
     }
}
/* Advance the frame pointer based on the contents of the current frame */
static dword fs_file_get_next_frame(FAILSAFECONTEXT *pfscntxt,FSFRAMEINFO *pframe)
{
dword next_frame_index;

    if (!pframe->frame_format_error)
    {
        next_frame_index = pframe->frame_index + 1 + pframe->stored_frame_replacement_records;
       /* If not room for the frame header and one block we wrap around */
        if (next_frame_index >= pfscntxt->journal_file_size)
            next_frame_index = 1;
    }
    else
        next_frame_index =  0;
    return(next_frame_index);
}

/* Retrieve FRAME record information from a buffer. Check if legal */
static void fs_mem_frame_info(FAILSAFECONTEXT *pfscntxt, dword *pdw,FSFRAMEINFO *pframe, dword frame_index, BOOLEAN do_checksum)
{
    rtfs_memset(pframe,0, sizeof(*pframe));
    pframe->frame_index = frame_index;
    pframe->stored_frame_type          =           *(pdw + FS_FRAME_OFFSET_TYPE);
    pframe->stored_frame_sequence      =           *(pdw + FS_FRAME_OFFSET_FRAME_SEQUENCE);

    pframe->stored_session_id        =              *(pdw + FS_FRAME_OFFSET_SESSION_ID);
    pframe->stored_frame_replacement_records =    *(pdw + FS_FRAME_OFFSET_FRAME_RECORDS);
    pframe->stored_segment_checksum    =           *(pdw + FS_FRAME_OFFSET_SEGMENT_CHECKSUM);
    pframe->stored_frame_checksum      =           *(pdw + FS_FRAME_OFFSET_FRAME_CHECKSUM);
    pframe->stored_frame_fat_freespace =          *(pdw + FS_FRAME_OFFSET_FAT_FREESPACE);
    pframe->pfirst_replacement_record =         pdw + FS_FRAME_HEADER_SIZE; /* pointer */
    pframe->calculated_frame_checksum = 0;

    if ((pframe->stored_frame_replacement_records > pfscntxt->fs_frame_max_records) ||
        (pframe->stored_frame_type < FS_FRAME_TYPE_NULL) ||
        (pframe->stored_frame_type > FS_FRAME_TYPE_RESTORED) )
        pframe->frame_format_error = TRUE;
    else
    {
        if (do_checksum)
        {
        dword i;
        dword check_sum;
            check_sum = 0;
            pdw = pframe->pfirst_replacement_record;
            for (i = 0; i < pfscntxt->fs_frame_max_records;i++)
                check_sum += *pdw++;
             pframe->calculated_frame_checksum = check_sum;
        }
    }
#if (FS_DEBUG)
#if (0)
    FS_DEBUG_SHOWINTNL("frame_index == ", pframe->frame_index)
    FS_DEBUG_SHOWINTNL("stored_frame_type          == ", pframe->stored_frame_type         )
    FS_DEBUG_SHOWINTNL("stored_frame_sequence      == ", pframe->stored_frame_sequence        )
    FS_DEBUG_SHOWINTNL("stored_session_id          == ", pframe->stored_session_id       )
    FS_DEBUG_SHOWINTNL("frame_replacement_records  == ", pframe->stored_frame_replacement_records)
    FS_DEBUG_SHOWINTNL("stored_segment_checksum    == ", pframe->stored_segment_checksum   )
    FS_DEBUG_SHOWINTNL("stored_frame_checksum      == ", pframe->stored_frame_checksum     )
    FS_DEBUG_SHOWINTNL("calculated next cksum      == ", pframe->stored_frame_checksum+pframe->stored_segment_checksum)
    FS_DEBUG_SHOWINTNL("stored_frame_fat_freespace == ", pframe->stored_frame_fat_freespace)
    FS_DEBUG_SHOWINTNL("calculated_frame_checksum == ", pframe->calculated_frame_checksum)
#endif
#endif
}

BOOLEAN fs_api_restore(byte *drive_name)
{
int driveno;
FAILSAFECONTEXT *pfscntxt=0;
DDRIVE *pdr;
FSFILEINFO file_info;
BOOLEAN ret_val;

    /* Get the drive and make sure it is mounted   */
    driveno = check_drive_name_mount(drive_name, CS_CHARSET_NOT_UNICODE);
    if (driveno < 0)
        return(FALSE);
    pfscntxt = 0;
    ret_val = FALSE;
    pdr = pc_drno_to_drive_struct(driveno);
    rtfs_clear_errno();
	if (pdr)
	{
    	if (pdr->pmedia_info->is_write_protect)
    	{
        	rtfs_set_errno(PEDEVICEWRITEPROTECTED, __FILE__, __LINE__);
        	goto ex_it;
    	}
    	pfscntxt = (FAILSAFECONTEXT *)pdr->du.user_failsafe_context;
	}
    if (!pfscntxt)
    { /* They need to assign a Failsafe control block */
        rtfs_set_errno(PENOINIT, __FILE__, __LINE__);
        goto ex_it;
    }
    else if (pdr->drive_state.failsafe_context && pfscntxt->operating_flags & FS_OP_ASYNCRESTORE)
    { /* Can't do it if currently restoring, buffers are in use */
        rtfs_set_errno(PEEINPROGRESS, __FILE__, __LINE__);
        goto ex_it;
    }
    if (!fs_restore_file_info(pfscntxt, &file_info))/* :LB: - Adjust index block scale factors and retrieve file information */

    {/* System level failure */
        goto ex_it;
    }
    if (!file_info.journal_file_valid)
    {
        FS_DEBUG_SHOWNL("fs_api_restore - Bad or missing journal")
        ret_val = FALSE;
        goto ex_it;
    }
    if ( !(file_info.restore_recommended || file_info.restore_required))
    {
        FS_DEBUG_SHOWNL("fs_api_restore - No restore required")
        ret_val = TRUE;
        goto ex_it;
    }
    /* Restore is recomended, means flushed but, not restoring check freespace */
    if (file_info.restore_recommended)
    {
        if (file_info.journal_file_stored_freespace != (dword) pfscntxt->pdrive->drive_info.known_free_clusters)
        {
            FS_DEBUG_SHOWNL("fs_api_restore - Freespace missmatch")
            ret_val = FALSE;
            goto ex_it;
        }
    }
    /* Clear the directory and FAT cache */
    pc_free_all_blk(pfscntxt->pdrive);
    pc_free_all_fat_buffers(pfscntxt->pdrive);
#if (INCLUDE_EXFATORFAT64)
    if (ISEXFATORFAT64(pfscntxt->pdrive)) 
    /* Initialize the exfat bit allocation map (bam) block buffer pool */
		pc_free_all_bam_buffers(pfscntxt->pdrive);
#endif
    ret_val = TRUE;
    /* Complete currently RESTORING process */
    if (file_info.journal_start_restoring)
        ret_val = fs_restore_from_file(pfscntxt,file_info.journal_start_restoring, file_info.journal_end_restoring);
    /* Restore FLUSHED segments */
    if (ret_val && file_info.journal_start_flushed)
        ret_val = fs_restore_from_file(pfscntxt,file_info.journal_start_flushed, file_info.journal_end_flushed);
    pc_dskfree(driveno);
ex_it:
    rtfs_release_media_and_buffers(driveno);  /* fs_api_restore, release claim by check_drive_name_mount() */
    return(ret_val);
}

/* Check if the volume needs restoring and if so call the callback
   function to ask if restore is desired, if so restore, and then remount
   called by fs_failsafe_dskopen() during the rtfs mount procedure */

BOOLEAN fs_failsafe_autorestore(FAILSAFECONTEXT *pfscntxt)
{
int restore_strategy;
BOOLEAN continue_mount,out_of_date,remount_now;
FSFILEINFO file_info;

    continue_mount=TRUE;
    remount_now=FALSE;

    if (!fs_restore_file_info(pfscntxt, &file_info)) /* :LB: - Adjust index block scale factors and retrieve file information */
    {/* System level failure */
        continue_mount=FALSE;
        goto ex_it;
    }
    /* benign errors */
    if (!file_info.journal_file_valid)
        goto ex_it;
    /* Restore is recomended, means flushed but, not restoring check freespace */
    out_of_date=FALSE;
    if (file_info.restore_recommended)
    {
        if (file_info.journal_file_stored_freespace != (dword) pfscntxt->pdrive->drive_info.known_free_clusters)
            out_of_date = TRUE;
    }
    if (out_of_date)
    {
        FS_DEBUG_SHOWNL("fs_failsafe_autorestore - Out of date journal detected")
        /* Bad journal file, tell the user, ask to abort or continue */
        if (!fs_api_cb_error_restore_continue(pfscntxt->pdrive->driveno))
        {
            continue_mount=FALSE;
            fs_failsafe_disable(pfscntxt->pdrive,PEFSRESTOREERROR);
            rtfs_set_errno(PEFSRESTOREERROR, __FILE__, __LINE__);
        }
        goto ex_it;
    }
    /* Journal file is in synch with FAT */
    if ( !(file_info.restore_recommended || file_info.restore_required))
    {
        FS_DEBUG_SHOWNL("fs_failsafe_autorestore - No restore required")
        continue_mount = TRUE;
        goto ex_it;
    }
    FS_DEBUG_SHOWNL("fs_failsafe_autorestore - Restore required")

    /* Restore required, ask the user what to do.
        FS_CB_ABORT    terminate the mount, causing the API call to fail with errno set
                        to PEFSRESTORENEEDED.
        FS_CB_RESTORE  Tells Failsafe to proceded and restore the volume
        FS_CB_CONTINUE Tells Failsafe to proceded and not restore the volume.
    */

    restore_strategy = fs_api_cb_restore(pfscntxt->pdrive->driveno);
    if (restore_strategy == FS_CB_CONTINUE)
    {
        FS_DEBUG_SHOWNL("fs_failsafe_autorestore - Ignore and resume")
        continue_mount = TRUE;
        goto ex_it;
    }
    else if (restore_strategy != FS_CB_RESTORE)
    {
        FS_DEBUG_SHOWNL("fs_failsafe_autorestore - Force mount error")
        fs_failsafe_disable(pfscntxt->pdrive,PEFSRESTORENEEDED);
        rtfs_set_errno(PEFSRESTORENEEDED, __FILE__, __LINE__);
        goto ex_it;    /* Don't restore, just report and fail */
    }
    else
    {
        BOOLEAN ret_val;
        FS_DEBUG_SHOWNL("fs_failsafe_autorestore - Restoring volume")
        /* User gave the go ahead to auto restore */
        /* Clear the directory and FAT cache */
        pc_free_all_blk(pfscntxt->pdrive);
        pc_free_all_fat_buffers(pfscntxt->pdrive);
#if (INCLUDE_EXFATORFAT64)
    if (ISEXFATORFAT64(pfscntxt->pdrive)) 
    /* Initialize the exfat bit allocation map (bam) block buffer pool */
		pc_free_all_bam_buffers(pfscntxt->pdrive);
#endif

        ret_val = TRUE;
        /* Complete currently RESTORING process */
        if (file_info.journal_start_restoring)
            ret_val = fs_restore_from_file(pfscntxt,file_info.journal_start_restoring, file_info.journal_end_restoring);
        /* Restore FLUSHED segments */
        if (ret_val && file_info.journal_start_flushed)
            ret_val = fs_restore_from_file(pfscntxt,file_info.journal_start_flushed, file_info.journal_end_flushed);
        if (ret_val)
        {
            continue_mount = TRUE;
            remount_now = TRUE;
        }
        else
            continue_mount = TRUE;
    }
ex_it:
    if (remount_now)
    {
        /* We changed the FATS from the journal so close and reopen */
        pc_dskfree(pfscntxt->pdrive->driveno);
        return (pc_i_dskopen(pfscntxt->pdrive->driveno,FALSE));
    }
    else
        return(continue_mount);
}

/* Synchronous restore from Journal file routine.
   Reads from the Journal file and restores the FAT volume from its
   contents. While operating the journal file status is FS_FRAME_TYPE_RESTORING
   When complete the journal file status is chenged to FS_FRAME_TYPE_RESTORED,
   If the restore is interrupted from the FS_FRAME_TYPE_RESTORING state the
   restore must be repeated and allowed to run to completion. */

static BOOLEAN fs_restore_from_file(FAILSAFECONTEXT *pfscntxt,dword start_record, dword end_record)
{
int status;
FSRESTORE *prestore;
    prestore = CONTEXT_TO_RESTORE(pfscntxt);
    rtfs_memset(prestore,0, sizeof(*prestore));

    prestore->restoring_start_record = start_record;
    prestore->restoring_terminal_record = end_record;
    prestore->restore_from_file = TRUE;

    status = fs_restore_start(pfscntxt);
    if (status == PC_ASYNC_ERROR)
	{
        prestore->restore_from_file = FALSE;
        return(FALSE);
	}
    else
    {
        while (status == PC_ASYNC_CONTINUE)
            status = fs_restore_continue(pfscntxt);
        prestore->restore_from_file = FALSE;
        if (status == PC_ASYNC_COMPLETE)
            return(TRUE);
        else
            return(FALSE);
    }
}

void fs_show_journal_session(FAILSAFECONTEXT *pfscntxt);

static void fs_restore_from_session_complete(FAILSAFECONTEXT *pfscntxt)
{
    REGION_FRAGMENT *pf;
    FSJOURNAL *pjournal;
    FSRESTORE *prestore;
    dword loop_guard;

    prestore = CONTEXT_TO_RESTORE(pfscntxt);
    pjournal = CONTEXT_TO_JOURNAL(pfscntxt);
    /* Now that this sessin is synchronized with the FAT we can allow
       the freed clusters from this session to be reallocated */
    if (pjournal->restoring_free_fragments)
    {
		loop_guard = GUARD_ENDLESS;
        pf = pjournal->restoring_free_fragments;
        while (pf)
        {
        	if (restore_check_endless_loop(&loop_guard))
            	return;

            fatop_add_free_region(pfscntxt->pdrive,
                                pf->start_location,
                                PC_FRAGMENT_SIZE_CLUSTERS(pf), TRUE);
            pf = pf->pnext;
        }
        /* Release free fragment list */
        pc_fraglist_free_list(pjournal->restoring_free_fragments);
        pjournal->restoring_free_fragments = 0;
    }
    /* The remap blocks in the "restoring" state are no longer remapped
       since they're recorded on the FAT volume. So release the remap records */
    if (pjournal->restoring_remapped_blocks)
    {
        struct fsblockmap *pbm,*pbm_next;
        pbm = pjournal->restoring_blockmap_cache;
		loop_guard = GUARD_ENDLESS;
        while(pbm)
        {
        	if (restore_check_endless_loop(&loop_guard))
            	return;
            pbm_next = pbm->pnext;
            fs_free_blockmap(pjournal,pbm);
            pbm = pbm_next;
        }
        pjournal->restoring_blockmap_cache = 0;
        pjournal->restoring_remapped_blocks = 0;
    }
    /* Free frames that were restored */
    if (prestore->restoring_last_block >= prestore->restoring_start_record)
    { /* Simple contiguous group */
        pjournal->frames_free += (1 + prestore->restoring_last_block -
                                     prestore->restoring_start_record);
    }
    else
    { /* Wraps the end */
        /* Blocks to the end */
        pjournal->frames_free +=
            (pfscntxt->journal_file_size - prestore->restoring_start_record);
        /* Blocks in front note: this calucation is correct because
           the first block is reserved */
        pjournal->frames_free += prestore->restoring_last_block;
    }
#if (FS_DEBUG)
    FS_DEBUG_SHOWNL("fs_restore_from_session_commplete - completed")
    fs_show_journal_session(pfscntxt);
#endif
}

int fs_restore_from_session_start(FAILSAFECONTEXT *pfscntxt)
{
FSJOURNAL *pjournal;
FSRESTORE *prestore;
    pjournal = CONTEXT_TO_JOURNAL(pfscntxt);

    prestore = CONTEXT_TO_RESTORE(pfscntxt);
    rtfs_memset(prestore,0, sizeof(*prestore));
    /* If no records flushed so far in this session just return complete */
    if (pjournal->flushed_start_record==0 || pjournal->flushed_terminal_record==0)
        return(PC_ASYNC_COMPLETE);
#if (FS_DEBUG)
    FS_DEBUG_SHOWNL("fs_restore_from_session_start - started")
    fs_show_journal_session(pfscntxt);
#endif
    prestore->restore_from_file 			= FALSE;
    prestore->restoring_start_record 		= pjournal->flushed_start_record;
    prestore->restoring_terminal_record 	= pjournal->flushed_terminal_record;
    prestore->restoring_last_block 			= pjournal->flushed_last_block;
    pjournal->restoring_blockmap_cache      = pjournal->flushed_blockmap_cache;
    pjournal->restoring_remapped_blocks     = pjournal->flushed_remapped_blocks;
    pjournal->flushed_blockmap_cache        = 0;
    pjournal->flushed_remapped_blocks       = 0;
    pjournal->restoring_free_fragments      = pjournal->flushed_free_fragments;
    pjournal->flushed_free_fragments        = 0;
#if (INCLUDE_DEBUG_RUNTIME_STATS)
    pfscntxt->stats.frames_restoring        = pfscntxt->stats.frames_flushed;
    pfscntxt->stats.frames_flushed 			= 0;
    pfscntxt->stats.frames_closed  			= 0;
    pfscntxt->stats.restore_data_reads      = 0;
    pfscntxt->stats.restore_data_blocks_read= 0;
    pfscntxt->stats.volume_block_writes     = 0;
    pfscntxt->stats.volume_blocks_written   = 0;
    pfscntxt->stats.restore_pass_count      = 0;
#endif
    /* reset journal flush control session variables */
    fs_clear_session_vars(pfscntxt);
#if (FS_DEBUG)
    FS_DEBUG_SHOWNL("fs_restore_from_session_start - completed")
    fs_show_journal_session(pfscntxt);
#endif
    return(fs_restore_start(pfscntxt));
}

int fs_restore_from_session_continue(FAILSAFECONTEXT *pfscntxt)
{
int status;
FSRESTORE *prestore;
    prestore = CONTEXT_TO_RESTORE(pfscntxt);
    /* If no records queued for restore */
    if (prestore->restoring_start_record == 0)
    {
        status = PC_ASYNC_COMPLETE;
    }
    else
    {
        status = fs_restore_continue(pfscntxt);
        /* The FAT is synched up so any clusters queued to be freed
           may now be returned to freespace */
        if (status == PC_ASYNC_COMPLETE)
        {
            fs_restore_from_session_complete(pfscntxt);
        }
    }
    if (status != PC_ASYNC_CONTINUE)
    {
        pfscntxt->operating_flags &= ~FS_OP_ASYNCRESTORE;
        prestore->restoring_start_record = 0;
        prestore->restoring_terminal_record = 0;
		/* Clear needs restore condition since we are done */
        pfscntxt->pdrive->drive_info.drive_operating_flags &= ~DRVOP_FS_NEEDS_RESTORE;
    }
    return(status);
}

static int fs_restore_start(FAILSAFECONTEXT *pfscntxt)
{
int status;
FSRESTORE *prestore;
    prestore = CONTEXT_TO_RESTORE(pfscntxt);

    /* Initialize the restore buffer and replacement record free list */
    if (!fs_init_restore_buffer(pfscntxt))
        return(PC_ASYNC_ERROR);
    pfscntxt->operating_flags |= FS_OP_ASYNCRESTORE;
    /* Start the load process */
    prestore->restore_state = RS_READ_START_MARK;
    prestore->restore_loop_guard = GUARD_ENDLESS;
    status = fs_restore_continue(pfscntxt);
    return(status);
}

/* Asyncronous restore continue procedure. use by both synchronous and
   asyncronous restore routines.
    Reads the journal file and writes the volume as required
    to complete the restore.
    If we are restoring from existing buffers only then writes
    the volume as required. Each pass makes either one read call
    or one write call

        returns:
            PC_ASYNC_COMPLETE,PC_ASYNC_CONTINUE,PC_ASYNC_ERROR
*/
static int fs_restore_continue(FAILSAFECONTEXT *pfscntxt)
{
int status;
FSRESTORE *prestore;
dword *pdw,journal_block_to_read;

    prestore = CONTEXT_TO_RESTORE(pfscntxt);
    status = PC_ASYNC_ERROR;
    UPDATE_FSRUNTIME_STATS(pfscntxt, restore_pass_count, 1)

    if (restore_check_endless_loop(&(prestore->restore_loop_guard)))
        goto ex_it;

    switch (prestore->restore_state)
    {
    case RS_READ_START_MARK:
      /* Read the end record.. next pass we'll write it */
        if (!fs_restore_load_record(pfscntxt,prestore->restoring_terminal_record))
            goto ex_it; /* IO error */
        prestore->restore_state = RS_WRITE_START_MARK;
        status = PC_ASYNC_CONTINUE;
        break;
    case RS_WRITE_START_MARK:
      /* Set the state in the end record.. to restoring
         and intialize the restore process state machine */
        pdw = fs_restore_map_record(pfscntxt,prestore->restoring_terminal_record);
        if (!pdw)
        {
            ERTFS_ASSERT(rtfs_debug_zero())
            goto ex_it; /* Shouldn't happen */
        }
        *(pdw+FS_FRAME_OFFSET_TYPE) = FS_FRAME_TYPE_RESTORING;
        RTFS_DEBUG_LOG_DEVIO(pfscntxt->pdrive, RTFS_DEBUG_IO_J_RESTORE_START, (pfscntxt->nv_buffer_handle+prestore->restoring_terminal_record), 1)
        if (!fs_restore_write_record(pfscntxt, prestore->restoring_terminal_record, (byte *) pdw))
            goto ex_it; /* IO error */
        prestore->restore_state = RS_START_RESTORE;
        status = PC_ASYNC_CONTINUE;
        break;
    case RS_READ_END_MARK:
        if (!fs_restore_load_record(pfscntxt,prestore->restoring_terminal_record))
            goto ex_it; /* IO error */
        prestore->restore_state = RS_WRITE_END_MARK;
        status = PC_ASYNC_CONTINUE;
        break;
    case RS_WRITE_END_MARK:
        pdw = fs_restore_map_record(pfscntxt,prestore->restoring_terminal_record);
        if (!pdw)
        {
            ERTFS_ASSERT(rtfs_debug_zero())
            goto ex_it; /* Shouldn't happen */
        }
        *(pdw+FS_FRAME_OFFSET_TYPE) = FS_FRAME_TYPE_RESTORED;
        RTFS_DEBUG_LOG_DEVIO(pfscntxt->pdrive, RTFS_DEBUG_IO_J_RESTORE_END, (pfscntxt->nv_buffer_handle+prestore->restoring_terminal_record), 1)
        if (!fs_restore_write_record(pfscntxt,prestore->restoring_terminal_record,(byte *) pdw))
            goto ex_it; /* IO error */
        /* Next time clear the journal location */
        prestore->restore_state = RS_CLEAR_LOCATION;
        status = PC_ASYNC_CONTINUE;
        break;
    case RS_CLEAR_LOCATION:
    {   /* Clear the journal location in free clusters from reserved area of the FAT.
          (NO-OP if journaling to specified reserved sectors) */
        BOOLEAN clear_handle;
        if(prestore->restore_from_file) /* We restored from the file, Journaling is off, clear the handle */
            clear_handle = TRUE;
        else
        {   /* Journaling is on, clear the handle only if we haven't Flushed any frames.
               If we did clear after first flush we would overwrite the handle
               But if we did not clear and a flush never occured we would leave a stale handle */
            FSJOURNAL *pjournal;
            pjournal = CONTEXT_TO_JOURNAL(pfscntxt);
            if (pjournal->flushed_remapped_blocks)
                clear_handle = FALSE;
            else
                clear_handle = TRUE;
        }
        if (clear_handle)
        {
            if (!failsafe_nv_buffer_clear(pfscntxt,prestore->restore_buffer.transfer_buffer))
            {
                ERTFS_ASSERT(rtfs_debug_zero())
                goto ex_it; /* Shouldn't happen */
            }
        }
        FS_DEBUG_SHOWNL("Restore completed")
        status = PC_ASYNC_COMPLETE;
        break;
    }
    case RS_START_RESTORE:
        /* Start the restore process */
        /* Clear replacement list EOF condition and lists */
        prestore->replacement_queue.all_replacements_queued = FALSE;
        prestore->replacement_queue.preplacements_queued 		= 0;
        prestore->replacement_queue.preplacements_buffered    	= 0;
        prestore->replacement_queue.preplacement_fat          	= 0;

        if(prestore->restore_from_file)
        {   /* Read the journal file, we will the create replacements from the index blocks */
            prestore->replacement_queue.file_current_index_block  = prestore->restoring_start_record;
            prestore->replacement_queue.file_current_index_offset = 0;
            prestore->restore_state = RS_START_LOAD_QUEUE;
			status = PC_ASYNC_CONTINUE;
            break;
        }
        else
        {   /* Build replacement blocks from the remap cache */
        FSJOURNAL *pjournal;

            pjournal = CONTEXT_TO_JOURNAL(pfscntxt);
            prestore->replacement_queue.mem_current_blockmap  = pjournal->restoring_blockmap_cache;
            if (!prestore->replacement_queue.mem_current_blockmap)
                prestore->restore_state = RS_READ_END_MARK; /* goto closing state */
            else
                prestore->restore_state = RS_START_LOAD_QUEUE;
            status = PC_ASYNC_CONTINUE;
            break;
        }
    case RS_START_LOAD_QUEUE:
       /* Clear the flag fs_load_replacement_records() uses to tell us if it
	      used all allocated replacement records. (Telling us to
		  free up the records by reading and writing the data and freeing the records */
        prestore->replacement_queue.used_all_replacement_records = FALSE;
		/* Fall through */
    case RS_LOAD_QUEUE:
       if (prestore->replacement_queue.all_replacements_queued)
       {
            prestore->restore_state = RS_READ_END_MARK; /* goto closing state */
       }
       else
       {
            if (!fs_load_replacement_records(pfscntxt))
                break; /* The load exitted prematurely, return error */
          /* 1-22-07 changed
             from:
             if (!prestore->replacement_queue.preplacement_freelist || prestore->replacement_queue.all_replacements_queued)
			 to:
             if (prestore->replacement_queue.used_all_replacement_records || prestore->replacement_queue.all_replacements_queued)
             This error caused the state machine not to advance when all replacement records were
             exhausted because prestore->replacement_queue.preplacement_freelist never goes to
             zero because we reserve two records for later on. This error could only occur if the
			 number of replacement records allocated from the transfer buffer were fewer than
			 the number of seperate regions of disk to be restored. Not a very likely scenario but
			 possible */
            /* If we used up all replacement records or we queued all possible replacements then
               switch to the read replacement state */
            if (prestore->replacement_queue.used_all_replacement_records ||	prestore->replacement_queue.all_replacements_queued)
            {
                prestore->restore_state = RS_READ_REPLACEMENTS;
            }
        }
        status = PC_ASYNC_CONTINUE;
        break;
    case RS_READ_REPLACEMENTS:
        if (!prestore->replacement_queue.preplacements_queued)
        {
            prestore->restore_state = RS_READ_END_MARK; /* goto closing state */
            status = PC_ASYNC_CONTINUE;
            break;
        }
        else
        {
            /* Get replacement record at the lowest offset in the journal file
               that has not yet been restored. Move all replacement
               records that can fit in a single read of the restore buffer
               to prestore->replacement_queue.preplacements_buffered */
            fs_get_replacements_from_queue(pfscntxt,&journal_block_to_read);
        }
        if (!prestore->replacement_queue.preplacements_buffered)
        {
            ERTFS_ASSERT(rtfs_debug_zero())
            goto ex_it; /* Shouldn't happen */
        }
        if (!fs_load_restore_buffer(pfscntxt,
                journal_block_to_read,
                prestore->restore_buffer.transfer_buffer_size))
                goto ex_it;
        /* Clear the sweep pointers for handling multiple FAT copies */
        prestore->replacement_queue.preplacement_fat = 0;
        prestore->replacement_queue.num_fats_restored = 0;
        prestore->restore_state = RS_WRITE_REPLACEMENTS;
        status = PC_ASYNC_CONTINUE;
        break;
    case RS_WRITE_REPLACEMENTS:
        if (!fs_asy_write_to_volume(pfscntxt))
            break;
        /* If we used all of the buffers then get more replacement instructions */
        /* Otherwise stay in the write state */
        if (!prestore->replacement_queue.preplacements_buffered)
        {
            if (!prestore->replacement_queue.preplacements_queued) /* Go read the index for more */
                prestore->restore_state = RS_START_LOAD_QUEUE;
            else /* Go take another set from prestore->preplacements_queued
                    and move it to prestore->replacement_queue.preplacements_buffered
                    and read another buffer full from the Journal */
                prestore->restore_state = RS_READ_REPLACEMENTS;
        }
        status = PC_ASYNC_CONTINUE;
        break;
    default:
        ERTFS_ASSERT(rtfs_debug_zero())
        status = PC_ASYNC_ERROR;
        break;
    }
 ex_it:
    if (status != PC_ASYNC_CONTINUE)
        pfscntxt->operating_flags &= ~FS_OP_ASYNCRESTORE;
    return(status);
}

/* Scan the replacement queue, get the record with the lowest journal file offset
   then move all records that can fit into the buffered list. */
static void fs_get_replacements_from_queue(FAILSAFECONTEXT *pfscntxt, dword *plowest_block)
{
FSRESTORE *prestore;
FSBLOCKMAP *pbuffered,*preplacement;   /* Queued from file index headers or from blockmap */
FSBLOCKMAP *pprev;
dword out_block,loop_guard;

    *plowest_block = 0;
    prestore = CONTEXT_TO_RESTORE(pfscntxt);
    preplacement = prestore->replacement_queue.preplacements_queued;
    *plowest_block = preplacement->journal_blockno;
	loop_guard = GUARD_ENDLESS;
    while (preplacement)
    {
        if (restore_check_endless_loop(&loop_guard))
       		return;

        if (*plowest_block > preplacement->journal_blockno)
            *plowest_block = preplacement->journal_blockno;
        preplacement = preplacement->pnext;
    }
    /* Now traverse again extracting blocks that fit in our range */
    pprev = 0;
    pbuffered = 0;
    preplacement = prestore->replacement_queue.preplacements_queued;
    out_block = *plowest_block + prestore->restore_buffer.transfer_buffer_size;
	loop_guard = GUARD_ENDLESS;
    while (preplacement)
    {
        if (restore_check_endless_loop(&loop_guard))
       		return;
        if (preplacement->journal_blockno < out_block)
        { /* Take all or part of it */
            if (preplacement->journal_blockno+preplacement->n_replacement_blocks <= out_block)
            {   /* Put the whole record on the buffered list */
                if (pbuffered)
                    pbuffered->pnext = preplacement;
                else
                    prestore->replacement_queue.preplacements_buffered = preplacement;
                /* Remove from queued list */
                if (pprev) /* The last replacement record we saw that was not in the buffer */
                    pprev->pnext = preplacement->pnext;
                else
                    prestore->replacement_queue.preplacements_queued = preplacement->pnext;
                pbuffered = preplacement; /* Make this record the end of the buffered list */
                preplacement = preplacement->pnext; /* look at the next replacement record */
                pbuffered->pnext = 0;
            }
            else
            { /* Take Part of it */
            FSBLOCKMAP *ptemp;
                /* Allocate a new record, initialize it with the blocks from the current
                   replacement record the fit in the buffer */
                /* It won't fail we made sure of that */
                ptemp = fs_alloc_replacement_record(prestore);
				if (!ptemp)
				{ /* Not expected under any circumstances */
					ERTFS_ASSERT(rtfs_debug_zero())
					return;
				}
                *ptemp = *preplacement;
                ptemp->pnext = 0;
                ptemp->n_replacement_blocks = out_block - ptemp->journal_blockno;
                if (pbuffered)
                    pbuffered->pnext = ptemp;
                else
                {
                    prestore->replacement_queue.preplacements_buffered = ptemp;
                }
                pbuffered = ptemp;
                /* Shrink it but don't remove from the queue - note that we do not
                   advance preplacement, we'll process this again but it will fall
                   of bounds */
                preplacement->n_replacement_blocks -= ptemp->n_replacement_blocks;
                preplacement->journal_blockno += ptemp->n_replacement_blocks;
                preplacement->volume_blockno += ptemp->n_replacement_blocks;
            }
        }
        else
        {
            pprev = preplacement;
            preplacement = preplacement->pnext;
        }
    }
}

/* Build up a list of block copy instructions, Stop when we run out of replacement structures or we run off
   the end of the current frame
   Populate: prestore->replacement_queue.preplacements_queued;
   Update:  prestore->replacement_queue.all_replacements_queued;
   If restoring from a file:
   Using:   prestore->replacement_queue.file_current_index_block;
            prestore->replacement_queue.file_current_index_offset;
   If restoring from block buffers:
   Using:   prestore->replacement_queue.mem_current_blockmap

   Note: Replacement records will reside wholly inside or outside
         of the fat region
*/


static BOOLEAN fs_load_replacement_records(FAILSAFECONTEXT *pfscntxt)
{
FSBLOCKMAP *preplacement_one,*preplacement_two,*preplacement_reserved[2];
dword blocks_in_region, fat_start, fat_end, volume_blockno, journal_blockno,n_blocks;
BOOLEAN ret_val;
FSFRAMEINFO frame_info;
FSRESTORE *prestore;
dword loop_guard;

    prestore = CONTEXT_TO_RESTORE(pfscntxt);

    preplacement_two = 0;
    preplacement_one = 0;
    /* We will run until we use up all replacement records or we queue up
       all the the records that may need replacement, but a later process
       fs_get_replacements_from_queue() may need to allocate up to two
       replacements so replacement records lie completely in or out of the
       transfer buffer, which are then freed by fs_asy_write_to_volume
       So we reserve a two replacement records for later use. */
    preplacement_reserved[0] = fs_alloc_replacement_record(prestore);
    preplacement_reserved[1] = fs_alloc_replacement_record(prestore);
	if (!preplacement_reserved[0] || !preplacement_reserved[1])
	{ /* Not expected under any circumstances */
		ERTFS_ASSERT(rtfs_debug_zero())
		return(FALSE);
	}
    fat_start =  pfscntxt->pdrive->drive_info.fatblock;
    fat_end =    pfscntxt->pdrive->drive_info.fatblock+pfscntxt->pdrive->drive_info.secpfat-1;

    ret_val = TRUE;

	loop_guard = GUARD_ENDLESS;
    while(!prestore->replacement_queue.all_replacements_queued)
    {
        if (restore_check_endless_loop(&loop_guard))
       		return(FALSE);
        if (!preplacement_one)
            preplacement_one = fs_alloc_replacement_record(prestore);
        if (!preplacement_two)
            preplacement_two = fs_alloc_replacement_record(prestore);

        if (!preplacement_one || !preplacement_two)
        {
            prestore->replacement_queue.used_all_replacement_records = TRUE;
            break; /* Out of records, just break out */
        }
        if(prestore->restore_from_file)
        {
             if (!prestore->replacement_queue.file_current_index_block ||
                 !fs_file_frame_info(pfscntxt,&frame_info, prestore->replacement_queue.file_current_index_block,FALSE))
             {
			 	/* Current index block number is invalid or it points to an invlide frame
			 	   or there was an IO error, these are not expected so make the top level break
			 	   out with an error */
                ret_val = FALSE;
                break;
             }
             if (prestore->replacement_queue.file_current_index_offset >= frame_info.stored_frame_replacement_records)
             {  /* If we finished a frame or there are no records in this frame we move on to the next */
                if (prestore->replacement_queue.file_current_index_block == prestore->restoring_terminal_record)
                { /* We just processed the terminal record so we are done */
                    prestore->replacement_queue.all_replacements_queued = TRUE; /* we're done */
                    break;
                }
                else
                { /* The frame at current index block is empty or we processed all of its records */
					/* This will always return a record number unless the frame_info is bad */
                    prestore->replacement_queue.file_current_index_block =
                        fs_file_get_next_frame(pfscntxt, &frame_info);
                    prestore->replacement_queue.file_current_index_offset = 0;
                    /* Go load the new frame */
                    continue;
                }
             }
             else
             {  /* Get next contiguous group of replacements */
                volume_blockno = *(frame_info.pfirst_replacement_record+prestore->replacement_queue.file_current_index_offset);
                journal_blockno= prestore->replacement_queue.file_current_index_block +
                                 prestore->replacement_queue.file_current_index_offset + 1;
                /* Calculate n_blocks: the number of contiguous replacements at the current offset */
                n_blocks = 1;
                {
                dword num_records;
                dword *pdw;
                    pdw = frame_info.pfirst_replacement_record+prestore->replacement_queue.file_current_index_offset;
                    num_records = frame_info.stored_frame_replacement_records -
                                prestore->replacement_queue.file_current_index_offset;
					if (num_records > pfscntxt->fs_frame_max_records)
					{ /* Not expected under any circumstances */
						ERTFS_ASSERT(rtfs_debug_zero())
						return(FALSE);
					}
                    while (--num_records && ((*pdw)+1 == *(pdw+1)))
                    {
                        n_blocks += 1;
                        pdw++;
                    }
                }
                prestore->replacement_queue.file_current_index_offset += n_blocks;
                /* If any blocks are queued to be overwritten already, purge the existing
                   request, we will superceed it */
                fs_invalidate_replacement_blocks(prestore, volume_blockno,n_blocks);
             }
        }
        else
        {
            FSBLOCKMAP *pbm;
            if (!prestore->replacement_queue.mem_current_blockmap)
            {
                prestore->replacement_queue.all_replacements_queued = TRUE;
                break;
            }
            else
            {
                pbm = prestore->replacement_queue.mem_current_blockmap;
                prestore->replacement_queue.mem_current_blockmap = pbm->pnext;
                volume_blockno      = pbm->volume_blockno;
                journal_blockno     = pbm->journal_blockno;
                n_blocks            = pbm->n_replacement_blocks;
            }
        }
        /* Make sure the replacement blocks are either completelety in the fat region
            or completely out. but not both (eases the restore process) */

        blocks_in_region = fs_blocks_in_region(volume_blockno, n_blocks, fat_start, fat_end);
        preplacement_one->journal_blockno = journal_blockno;
        preplacement_one->volume_blockno  =  volume_blockno;
        preplacement_one->n_replacement_blocks        = blocks_in_region;

        prestore->replacement_queue.preplacements_queued =
            fs_insert_replacement_record(prestore->replacement_queue.preplacements_queued, preplacement_one);
		/* we used this record so null the pointer so we don't free it */
        preplacement_one = 0;
        if (blocks_in_region < n_blocks)
        {
		   /* The replacement blocks straddled an edge of the FAT and was shrunk to reside either
		   	 completely inside or completely outside the FAT. So create a new one that consists of
		   	 the excluded blocks */
            preplacement_two->journal_blockno = journal_blockno + blocks_in_region;
            preplacement_two->volume_blockno = volume_blockno + blocks_in_region;
            preplacement_two->n_replacement_blocks = n_blocks - blocks_in_region;
            prestore->replacement_queue.preplacements_queued =
                fs_insert_replacement_record(prestore->replacement_queue.preplacements_queued,preplacement_two);
            /* we used this record so null the pointer so we don't free it */
            preplacement_two = 0;
        }
    }
    /* Free records we didn't use, if pointers are NULL it's a NOOP */
    fs_free_replacement_record(prestore,preplacement_one);
    fs_free_replacement_record(prestore,preplacement_two);
    /* Put the reserved records back on the freelist */
    fs_free_replacement_record(prestore,preplacement_reserved[0]);
    fs_free_replacement_record(prestore,preplacement_reserved[1]);
    return(ret_val);
}


/* Return a pointer to data of journal file block number "record" if it
   is in the restore buffer */
static dword *fs_restore_map_record(FAILSAFECONTEXT *pfscntxt, dword record)
{
FSRESTORE *prestore;
    prestore = CONTEXT_TO_RESTORE(pfscntxt);
    if (record < prestore->restore_buffer.first_block_in_buffer
        || record > prestore->restore_buffer.last_block_in_buffer)
    {
        return(0);
    }
    else
    {
        dword ltemp;
        byte *pdata;
        ltemp = record - prestore->restore_buffer.first_block_in_buffer;
        ltemp *= pfscntxt->pdrive->drive_info.bytespsector;
        pdata = prestore->restore_buffer.transfer_buffer;
        pdata += ltemp;
        return((dword *) pdata);
    }
}


/* BOOLEAN fs_fileinfo_internal(FAILSAFECONTEXT *pfscntxt, FSINFO *pinfo)

   Called by fs_failsafe_info() when Jounalling is not active.

   Populates the following structure:

typedef struct fsinfo {
        BOOLEAN journaling;         - FALSE
        BOOLEAN journal_file_valid; - TRUE if the journal is valid
        dword version;              - 3
        dword status;               -
            FS_STATUS_JOURNALING    -
            FS_STATUS_JOURNALING    -
            FS_STATUS_RESTORING     -
            FS_STATUS_NEEDS_RESTORE -
            FS_STATUS_RESTORED      -
        dword numindexblocks;       - Index blocks in the Journal
        dword totalremapblocks;     - Total number of remap blocks
        dword numblocksremapped;    - Total number of blocks remapped
        dword journaledfreespace;   - Freesapce Signature when jounal was craeated
        dword currentfreespace;     - Current volume Freesapce
        dword currentchecksum;      - Stored checksum
        dword indexchecksum;        - Calculated checksum
        dword journal_block_number; - Physical location of the Journal
        dword filesize;             - Size of the Journal in blocks
        BOOLEAN needsflush;         - FALSE
        BOOLEAN out_of_date;        - TRUE if Freespace signature does not
                                      match current freespace
        BOOLEAN check_sum_fails;    - TRUE if stored checksum does not match
                                      calculated checksum.
        BOOLEAN restore_required;   - TRUE if restore must be done to restore
                                      volume integrity
        BOOLEAN restore_recommended;- TRUE if restore must be done to commit
                                      Journalled transactions to the FAT
 } FSINFO;
*/

BOOLEAN fs_fileinfo_internal(FAILSAFECONTEXT *pfscntxt, FSINFO *pinfo)
{
FSFILEINFO file_info;

    rtfs_memset(pinfo, 0, sizeof(*pinfo));
    if (!pfscntxt)
        return(FALSE);
    if (!fs_restore_file_info(pfscntxt, &file_info)) /* :LB: - Adjust index block scale factors and retrieve file information */
    {
        rtfs_set_errno(PEFSNOJOURNAL, __FILE__, __LINE__);
        return(FALSE);
    }
    pinfo->journaling           = FALSE;
    pinfo->journal_file_valid   = file_info.journal_file_valid;
    pinfo->filesize  = pfscntxt->journal_file_size;
    pinfo->journal_block_number = pfscntxt->nv_buffer_handle;

	pinfo->out_of_date = FALSE;
	pinfo->version            = file_info.journal_file_version;
    pinfo->check_sum_fails    = file_info.check_sum_fails;

    pinfo->numblocksremapped  = file_info.journal_file_blocks_remapped;
	pinfo->currentfreespace     = (dword) pfscntxt->pdrive->drive_info.known_free_clusters;
	pinfo->journaledfreespace   = file_info.journal_file_stored_freespace;
    pinfo->restore_required     = file_info.restore_required;
    pinfo->restore_recommended  = file_info.restore_recommended;

    pinfo->_start_session_frame   = file_info.journal_start_frame;
    pinfo->_start_restored_frame  = file_info.journal_start_restored;
    pinfo->_last_restored_frame   = file_info.journal_end_restored;
    pinfo->_first_restoring_frame = file_info.journal_start_restoring;
    pinfo->_last_restoring_frame  = file_info.journal_end_restoring;
    pinfo->_first_flushed_frame   = file_info.journal_start_flushed;
    pinfo->_last_flushed_frame    = file_info.journal_end_flushed;
    pinfo->_records_to_restore    = file_info.journal_file_blocks_remapped;


    if (!pinfo->journal_file_valid)
    {
        pinfo->restore_required     = FALSE;
        pinfo->restore_recommended  = FALSE;
    }
    /* If we are not in the middle of a restore we can check if the
       Freesapce at the time of the last Flush matches the freespace
       when the flushed segment was started. */
    if (!pinfo->restore_required)
    {
        if (pinfo->restore_recommended || pinfo->restore_required)
        {
            if (pinfo->journaledfreespace != pinfo->currentfreespace)
            {
                FS_DEBUG_SHOWNL("fs_fileinfo_internal - FS_STATUS_OUT_OF_DATE")
                pinfo->restore_required     = FALSE;
                pinfo->restore_recommended  = FALSE;
                pinfo->out_of_date = TRUE;
            }
        }
    }
    return(TRUE);
}


static BOOLEAN fs_init_restore_buffer(FAILSAFECONTEXT *pfscntxt)
{
dword total_replacement_records, total_transfer_sectors;
FSBLOCKMAP *preplacement_records;
FSRESTORE *prestore;
byte *ptransfer_buffer;
dword transfer_buffer_size_sectors;
dword loop_guard;

    prestore = CONTEXT_TO_RESTORE(pfscntxt);

    ptransfer_buffer = pfscntxt->user_restore_transfer_buffer;
    transfer_buffer_size_sectors = pfscntxt->user_restore_transfer_buffer_size;

    /* use one sector of the transfer buffer for block replacestructures The rest is used for data transfer buffering */
    if (transfer_buffer_size_sectors<2)
        return(FALSE);


    preplacement_records = (FSBLOCKMAP *) ptransfer_buffer;
    /* The rest is used for data transfer buffering */
    ptransfer_buffer += pfscntxt->pdrive->drive_info.bytespsector;
    total_transfer_sectors = transfer_buffer_size_sectors - 1;
    /* Use up more blocks if we have to until we have at least one
       replacement record per block.. this way we can guarantee that
       we will not run out of replacement records */
    total_replacement_records = pfscntxt->pdrive->drive_info.bytespsector/sizeof(FSBLOCKMAP);
    loop_guard = GUARD_ENDLESS;
    while (total_replacement_records < total_transfer_sectors)
    {
      	if (restore_check_endless_loop(&loop_guard))
            return(FALSE);
        total_transfer_sectors -= 1;
        total_replacement_records += pfscntxt->pdrive->drive_info.bytespsector/sizeof(FSBLOCKMAP);
        ptransfer_buffer += pfscntxt->pdrive->drive_info.bytespsector;
    }
    ERTFS_ASSERT(total_transfer_sectors)
    ERTFS_ASSERT(total_replacement_records >= total_transfer_sectors)
    /* Create our freelist */
    prestore->replacement_queue.preplacement_freelist = 0;
    prestore->replacement_queue.replacement_records_free = 0;
    prestore->replacement_queue.total_replacement_records = total_replacement_records;
    loop_guard = GUARD_ENDLESS;
    while(total_replacement_records--)
    {
      	if (restore_check_endless_loop(&loop_guard))
            return(FALSE);
        fs_free_replacement_record(prestore,preplacement_records++);
    }
    prestore->replacement_queue.file_current_index_block  = 0;
    prestore->replacement_queue.file_current_index_offset = 0;

    prestore->restore_buffer.transfer_buffer      = ptransfer_buffer;
    prestore->restore_buffer.transfer_buffer_size = total_transfer_sectors;
    prestore->restore_buffer.first_block_in_buffer = 0;
    prestore->restore_buffer.num_blocks_in_buffer  = 0;
    prestore->restore_buffer.last_block_in_buffer  = 0;
    return(TRUE);
}

static BOOLEAN fs_load_restore_buffer(FAILSAFECONTEXT *pfscntxt,dword block_no,  dword n_blocks)
{
FSRESTORE *prestore;
    prestore = CONTEXT_TO_RESTORE(pfscntxt);
    if (block_no >= pfscntxt->journal_file_size)
        return(FALSE);
    if (n_blocks > prestore->restore_buffer.transfer_buffer_size)
        n_blocks = prestore->restore_buffer.transfer_buffer_size;
    if ((block_no+n_blocks) > pfscntxt->journal_file_size)
        n_blocks = pfscntxt->journal_file_size-block_no;
    RTFS_DEBUG_LOG_DEVIO(pfscntxt->pdrive, RTFS_DEBUG_IO_J_RESTORE_READ, (pfscntxt->nv_buffer_handle+block_no), n_blocks)
    if (!failsafe_nv_buffer_io(pfscntxt,block_no,n_blocks,
            prestore->restore_buffer.transfer_buffer,TRUE))
        return(FALSE);
    UPDATE_FSRUNTIME_STATS(pfscntxt, restore_data_reads, 1)
    UPDATE_FSRUNTIME_STATS(pfscntxt, restore_data_blocks_read, n_blocks)

    prestore->restore_buffer.first_block_in_buffer = block_no;
    prestore->restore_buffer.num_blocks_in_buffer  = n_blocks;
    prestore->restore_buffer.last_block_in_buffer  = block_no+n_blocks-1;
    return(TRUE);
}

static BOOLEAN fs_restore_load_record(FAILSAFECONTEXT *pfscntxt,dword record)
{
FSRESTORE *prestore;
    prestore = CONTEXT_TO_RESTORE(pfscntxt);
    if (!record || record >= pfscntxt->journal_file_size)
        return(FALSE);
    if (record < prestore->restore_buffer.first_block_in_buffer
        || record > prestore->restore_buffer.last_block_in_buffer)
    {
        return(fs_load_restore_buffer(pfscntxt,record,1));
    }
    else
        return(TRUE);
}

static BOOLEAN fs_restore_write_record(FAILSAFECONTEXT *pfscntxt, dword record, byte *pbuffer)
{
    return(failsafe_nv_buffer_io(pfscntxt,record,1, pbuffer,FALSE));
}


static BOOLEAN fs_file_frame_info(FAILSAFECONTEXT *pfscntxt,FSFRAMEINFO *pframe, dword frame_index, BOOLEAN do_checksum)
{
dword *pdw;
    pdw = fs_restore_map_record(pfscntxt, frame_index);
    if (!pdw)
    {
        if (fs_restore_load_record(pfscntxt, frame_index))
            pdw = fs_restore_map_record(pfscntxt, frame_index);
    }
    if (!pdw)
        return(FALSE);

    fs_mem_frame_info(pfscntxt, pdw, pframe, frame_index, do_checksum);
    return(TRUE);
}

#define TEST_TRALING_CLOSE_FIX 1
static BOOLEAN fs_file_segment_info(FAILSAFECONTEXT *pfscntxt,FSSEGMENTINFO *psegment,FSSESSIONINFO *psession)
{
FSFRAMEINFO frame_info;
dword loop_guard, count_closed_segments, count_flushed_segments;
#if (TEST_TRALING_CLOSE_FIX)
FSSEGMENTINFO current_flushed_segment;
    rtfs_memset(&current_flushed_segment,0, sizeof(current_flushed_segment));
#endif
    /* Initialize segment record */
    rtfs_memset(psegment,0, sizeof(*psegment));

    loop_guard = pfscntxt->journal_file_size;/* Can never have more frames than blocks */
    count_closed_segments = count_flushed_segments             = 0;

    while(psegment->segment_type == 0)
    {
        if (restore_check_endless_loop(&loop_guard))
       		return(FALSE);
        /* Load the frame at psession->next_frame_index- will only fail on IO error */
        if (!fs_file_frame_info(pfscntxt,&frame_info, psession->next_frame_index, TRUE))
            return(FALSE);
        /* Check if it is corrupt, random, or if session id or sequence number don't match */
        if (frame_info.frame_format_error ||
            psession->session_id != frame_info.stored_session_id ||
            psession->next_frame_sequence != frame_info.stored_frame_sequence ||
            frame_info.stored_frame_type == FS_FRAME_TYPE_OPEN)
        {   /* It is random as far as we are concerned. not in sequence so it acts as
               a terminator */
            break;
        }

        if (!psegment->segment_starting_index)
        { /* Just starting (first time in loop) so mark the start point */
            psegment->segment_ending_index   =
            psegment->segment_starting_index = psession->next_frame_index;
            psegment->segment_session_id     = psession->session_id;
            psegment->segment_fat_free_space    = frame_info.stored_frame_fat_freespace;
        }
        /* Check frame type */
        if (frame_info.stored_frame_type == FS_FRAME_TYPE_FLUSHED)
            count_flushed_segments += 1;    /* This plus preceeding frames are flushed */
        else if (frame_info.stored_frame_type == FS_FRAME_TYPE_CLOSED)
            count_closed_segments += 1;     /* This frame was flushed but the session hasn't been yet */
        else if (frame_info.stored_frame_type == FS_FRAME_TYPE_RESTORING)
            psegment->segment_type   = FS_SEGMENT_TYPE_RESTORING; /* This plus preceeding frames were being restored
                                                                     but the process didn't finish */
        else if (frame_info.stored_frame_type == FS_FRAME_TYPE_RESTORED) /* This plus preceeding frames were already restored */
            psegment->segment_type   = FS_SEGMENT_TYPE_RESTORED;
        else
        {    /* Unexpected frame */
            psegment->segment_error_type = FS_SEGMENT_ERROR_FORMAT;
            psegment->segment_type       = FS_SEGMENT_TYPE_ERROR;
            break;
        }
        /* The frame is expected - now check contents */
        if (psegment->segment_fat_free_space != frame_info.stored_frame_fat_freespace)
        { /* freespace doen't match .. can't rely on it */
           psegment->segment_error_type = FS_SEGMENT_ERROR_FORMAT;
           psegment->segment_type       = FS_SEGMENT_TYPE_ERROR;
           break;
        }
        if (frame_info.stored_frame_checksum != frame_info.calculated_frame_checksum)
        {
           psegment->segment_error_type = FS_SEGMENT_ERROR_CHECKSUM;
           psegment->segment_type       = FS_SEGMENT_TYPE_ERROR;
           break;
        }

        /* The frame is valid so include it in our inventory */
        psegment->segment_checksum += frame_info.stored_frame_checksum;
        psegment->segment_replacement_records += frame_info.stored_frame_replacement_records;
	    /* note, the length includes frame header plus replacements */
        psegment->segment_length += (1+frame_info.stored_frame_replacement_records);
	    /* It is the last valid frame in this segment that we know about */
        psegment->segment_ending_index = frame_info.frame_index;


        /* Get the next frame number. we know it will be valid because the frame is valid */
        psegment->segment_next_index =
        psession->next_frame_index = fs_file_get_next_frame(pfscntxt, &frame_info);
        if (!psegment->segment_next_index)
        { /* Not expected under any circumstances */
        	ERTFS_ASSERT(rtfs_debug_zero())
        	return(FALSE);
	    }
	    /* Next time we expect the sequence number to be psession->next_frame_sequence + 1; */
        psession->next_frame_sequence = psession->next_frame_sequence + 1;
#if (TEST_TRALING_CLOSE_FIX)
        /* If this is a flushed frame, save the session info up to this point */
        if (frame_info.stored_frame_type == FS_FRAME_TYPE_FLUSHED)
        {
            current_flushed_segment = *psegment;
        }
#endif
    }

    /* Done looping.. if we didn't hit an error frame or a restoring or restored frame
       then set the segemtn type to flushed if one or more frames where flushed
       (these records are created when the journal is flushed */
    if (!psegment->segment_type)
    {
        if (count_flushed_segments)
        {
#if (TEST_TRALING_CLOSE_FIX)
            *psegment = current_flushed_segment;
#endif
            psegment->segment_type = FS_SEGMENT_TYPE_FLUSHED; /*return FLUSHED if any */
        }
        else
        {
            psegment->segment_type = FS_SEGMENT_TYPE_NULL;  /*return NULL if any */
        }
    }
	/*
	   Return with:
       	psegment->segment_type       = FS_SEGMENT_TYPE_ERROR||FS_SEGMENT_TYPE_NULL||FS_SEGMENT_TYPE_FLUSHED
									||FS_FRAME_TYPE_RESTORING||FS_FRAME_TYPE_RESTORED

       	if psegment->segment_type is
       		FS_SEGMENT_TYPE_FLUSHED||FS_FRAME_TYPE_RESTORING||FS_FRAME_TYPE_RESTORED
			the following variables are valid and point to the
			next record. If that recvord is a valid frame header and it's
			sequence number and session number match we will continue
			processing the next time we are called. Otherwise we will return
			FS_SEGMENT_TYPE_ERROR or FS_SEGMENT_TYPE_NULL

       	psession->next_frame_index	  ==  Next frame record number to process
       	psession->next_frame_sequence ==  Sequence number it should have
	*/
    return(TRUE);
}

/* Scan the journal file starting at session_start frame and return */
static BOOLEAN fs_file_session_info(FAILSAFECONTEXT *pfscntxt,dword session_id, dword session_start_frame, FSSESSIONINFO *psession)
{
FSSEGMENTINFO segment_info;
FSFRAMEINFO frame_info;
dword loop_guard;

    rtfs_memset(psession,0, sizeof(*psession));
    /* Start assuming RESTORED.. A NOOP condition */
    psession->session_start_frame    = session_start_frame;
    psession->session_type           = FS_SESSION_TYPE_RESTORED;


    /* Read the first frame , don't checksum verify */
    if (!fs_file_frame_info(pfscntxt,&frame_info, psession->session_start_frame, FALSE))
        return(FALSE);
    /* If it's a bad frame or does not match our segment if just return FS_SESSION_TYPE_RESTORED */
    if (frame_info.frame_format_error || frame_info.stored_session_id != session_id)
    {
        return(TRUE);
    }
    /* We'll re-read the first segment so set frame index and session id and the next
       frame_sequence that we expect to the sequence of the first frame.
       We'll increment the sequence from there since stored sequence numbers
       should increment by one for each frame */
    psession->next_frame_sequence = frame_info.stored_frame_sequence;
    psession->next_frame_index    = session_start_frame;
    psession->session_id          = session_id;

    if (!fs_file_segment_info(pfscntxt,&segment_info, psession))
        return(FALSE);
    /* Nothing interesting  - return "RESTORED" status */
    if ((segment_info.segment_type != FS_SEGMENT_TYPE_RESTORING) &&
        (segment_info.segment_type != FS_SEGMENT_TYPE_RESTORED)  &&
        (segment_info.segment_type != FS_SEGMENT_TYPE_FLUSHED))
        return(TRUE);

    /* Process leading restored segments if any.
       If there are they will always be sequentially before
       restoring or flushed segments in the same session */
    if (segment_info.segment_type == FS_SEGMENT_TYPE_RESTORED)
    {
        psession->session_start_restored = segment_info.segment_starting_index;

        loop_guard = pfscntxt->journal_file_size;/* Can never have more frames than blocks */
        while (segment_info.segment_type == FS_SEGMENT_TYPE_RESTORED)
        {
       		if (restore_check_endless_loop(&loop_guard))
       			return(FALSE);
            psession->session_end_restored = segment_info.segment_ending_index;
            if (!fs_file_segment_info(pfscntxt,&segment_info, psession))
                return(FALSE);
        }
    }
    /* Process restoring segments if any. If there are they will always be sequentially before
       flushed segments in the same session */
    if (segment_info.segment_type == FS_SEGMENT_TYPE_RESTORING)
	{
        psession->session_type = FS_SESSION_TYPE_RESTORING;
    	psession->session_start_restoring = segment_info.segment_starting_index;

    	loop_guard = pfscntxt->journal_file_size;/* Can never have more frames than blocks */
    	while (segment_info.segment_type == FS_SEGMENT_TYPE_RESTORING)
    	{
   			if (restore_check_endless_loop(&loop_guard))
   				return(FALSE);
            psession->session_end_restoring   = segment_info.segment_ending_index;
            psession->session_replacement_records += segment_info.segment_replacement_records;
            /* Get the next segment - fails only on IO or unexpected errors */
            if (!fs_file_segment_info(pfscntxt,&segment_info, psession))
            	return(FALSE);
	   	}
    }
    /* Process flushed segments if any. If there are they will always be sequentially the
       last segments in the session */
    if (segment_info.segment_type == FS_SEGMENT_TYPE_FLUSHED)
	{	/* Set to flushed (optionally restore) if not already
	       in the restoring state, which requires a restore */
        if (psession->session_type != FS_SESSION_TYPE_RESTORING)
		{	/* remember freespace of the system before the flush */
        	psession->session_fat_free_space = segment_info.segment_fat_free_space;
        	psession->session_type = FS_SESSION_TYPE_FLUSHED;
		}
    	psession->session_start_flushed = segment_info.segment_starting_index;
    	loop_guard = pfscntxt->journal_file_size;/* Can never have more frames than blocks */

    	while (segment_info.segment_type == FS_SEGMENT_TYPE_FLUSHED)
    	{
   			if (restore_check_endless_loop(&loop_guard))
   				return(FALSE);
            psession->session_end_flushed   = segment_info.segment_ending_index;
            psession->session_replacement_records += segment_info.segment_replacement_records;
            /* Get the next segment - fails only on IO or unexpected errors */
            if (!fs_file_segment_info(pfscntxt,&segment_info, psession))
            	return(FALSE);
	    }
	}
    /* After looping through we should reach a NULL terminator. Meaning
	   a frame that would start a segment but it is either not in sequence
	   or it has errors */
    if (segment_info.segment_type != FS_SEGMENT_TYPE_NULL)
    {

            psession->session_type  = FS_SESSION_TYPE_ERROR;

            /* It should only be FS_SEGMENT_TYPE_ERROR) */
            ERTFS_ASSERT((segment_info.segment_type == FS_SEGMENT_TYPE_ERROR))
            if (segment_info.segment_type == FS_SEGMENT_TYPE_ERROR)
            	psession->session_error_type = segment_info.segment_error_type;
			else
            	psession->session_error_type = FS_SEGMENT_ERROR_FORMAT;
    }
    return(TRUE);
}

static BOOLEAN fs_restore_file_info(FAILSAFECONTEXT *pfscntxt, FSFILEINFO *pfileinfo)
{
dword *pdw;
FSRESTORE *prestore;
FSSESSIONINFO session_info;
FSMASTERINFO master_info;
dword raw_start_sector, file_size_sectors;

    prestore = CONTEXT_TO_RESTORE(pfscntxt);
    rtfs_memset(pfileinfo,0, sizeof(*pfileinfo));

    fs_failsafe_scaleto_blocksize(pfscntxt); /* Adjust index block scale factors */

    /* Check if the journal file is in reserved blocks */
    raw_start_sector = file_size_sectors = 0;
    if (!fs_api_cb_journal_fixed(pfscntxt->pdrive, &raw_start_sector, &file_size_sectors))
    {
        raw_start_sector = file_size_sectors = 0;
        /* We'll need the requested size so we can find the file */
        pfscntxt->journal_file_size = fs_api_cb_journal_size(pfscntxt->pdrive);
    }
    /* Reopen the file if it already exists */
    if (!failsafe_reopen_nv_buffer(pfscntxt, raw_start_sector, file_size_sectors))
    {
        pfileinfo->no_journal_file_found = TRUE;
        return(TRUE);
    }
    /* Initialize the restore buffer and replacement records */
    if (!fs_init_restore_buffer(pfscntxt))
        return(FALSE);
    /* Load the restore buffer starting at the journal start (header record) */
    if (!fs_load_restore_buffer(pfscntxt,
            0, prestore->restore_buffer.transfer_buffer_size))
        return(FALSE);

	 /* Get a pointer to the header record & process its contents */
     pdw = fs_restore_map_record(pfscntxt, 0);
     fs_mem_master_info(pdw,&master_info);
     pfileinfo->journal_file_version = master_info.master_file_version;
     pfileinfo->journal_file_size = master_info.master_file_size;
     pfileinfo->journal_file_start = pfscntxt->nv_buffer_handle;
     if (master_info.master_type == FS_MASTER_TYPE_ERROR)
     {
        if (master_info.master_error_type == FS_MASTER_ERROR_NOT_FAILSAFE)
            pfileinfo->not_journal_file = TRUE;
        else if (master_info.master_error_type == FS_MASTER_ERROR_WRONG_VERSION)
            pfileinfo->bad_journal_version = TRUE;
        pfileinfo->journal_file_valid = FALSE;
        return(TRUE);
     }
	 /* Scan the journal frame records to retrieve the session type of the file
	 	ERROR, RESTORED, OPEN, RESTORING, FLUSHED
		Also retrieves the start and end frames of restored, restoring and
		flush segments from the previous session.
	 */
     if (!fs_file_session_info(pfscntxt,master_info.master_file_session_id, master_info.master_start_record, &session_info))
        return(FALSE);

     if (session_info.session_type == FS_SESSION_TYPE_ERROR)
     {
        pfileinfo->journal_file_valid = FALSE;
        if (session_info.session_error_type == FS_SESSION_ERROR_CHECKSUM)
            pfileinfo->check_sum_fails = TRUE;
        /* Note - these two erros never occur. Should remove */
        if (session_info.session_error_type == FS_SESSION_ERROR_FORMAT)
            pfileinfo->bad_journal_format = TRUE;
        if (session_info.session_error_type == FS_SESSION_ERROR_OPEN)
            pfileinfo->journal_left_open = TRUE;
     }
     else
     {
        pfileinfo->journal_file_valid = TRUE;
        pfileinfo->journal_start_frame    = session_info.session_start_frame;
        pfileinfo->journal_file_stored_freespace = session_info.session_fat_free_space;
        pfileinfo->journal_file_blocks_remapped = session_info.session_replacement_records;
        pfileinfo->journal_start_restoring = session_info.session_start_restoring;
        pfileinfo->journal_end_restoring = session_info.session_end_restoring;
        pfileinfo->journal_start_flushed = session_info.session_start_flushed;
        pfileinfo->journal_end_flushed = session_info.session_end_flushed;
        pfileinfo->journal_start_restored = session_info.session_start_restored;
        pfileinfo->journal_end_restored   = session_info.session_end_restored;
        if (session_info.session_type == FS_SESSION_TYPE_FLUSHED)
            pfileinfo->restore_recommended = TRUE;
        else if (session_info.session_type == FS_SESSION_TYPE_RESTORING)
            pfileinfo->restore_required = TRUE;
     }
     return(TRUE);
}


/* When restoring from a file, that volume blocks may be journalled several times in sepearate
   sessions so this invlidates existing records before new ones are added */
static void fs_invalidate_replacement_blocks(FSRESTORE *prestore, dword first_block, dword n_blocks)
{
FSBLOCKMAP *pscan,*pprev;
dword last_block,last_scan_block,loop_guard;

    last_block = first_block + n_blocks - 1;

	loop_guard = GUARD_ENDLESS;
    pscan = prestore->replacement_queue.preplacements_queued;
    pprev = 0;
    while (pscan)
    {
   		if (restore_check_endless_loop(&loop_guard))
           	return;

        last_scan_block = pscan->volume_blockno + pscan->n_replacement_blocks - 1;

        if (pscan->volume_blockno > last_block)
        {
            break; /* we're done */
        }
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
                    prestore->replacement_queue.preplacements_queued = pscan->pnext;
                fs_free_replacement_record(prestore,pscan);
            }
            else
            {

                pscan->journal_blockno += (first_keeper - pscan->volume_blockno);
                pscan->volume_blockno = first_keeper;
                pscan->n_replacement_blocks = last_keeper-first_keeper + 1;
            }
            /* Process the list again until we go all the way through */
            fs_invalidate_replacement_blocks(prestore, first_block, n_blocks);
            break;
        }
    }
}

/* Insert a replacement record sorted by volume block number */
static FSBLOCKMAP *fs_insert_replacement_record(FSBLOCKMAP *preplacement_list, FSBLOCKMAP *preplacement_this)
{
FSBLOCKMAP *pscan,*pprev;
dword loop_guard;
    preplacement_this->pnext = 0;

	loop_guard = GUARD_ENDLESS;
    pscan = preplacement_list;
    pprev = 0;
    while (pscan)
    {
   		if (restore_check_endless_loop(&loop_guard))
           	return(0);
        if (preplacement_this->volume_blockno < pscan->volume_blockno)
        {   /* The new record is before the current record so insert it */
            preplacement_this->pnext = pscan;
            if (pprev)
                pprev->pnext = preplacement_this;
            else
                preplacement_list = preplacement_this;
            return(preplacement_list);
       }
       else
        { /* The new record is to the right */
           pprev = pscan;
           pscan = pscan->pnext;
        }
    }
    /* If we get here we have to put it at the end of the list */
    if (pprev)
        pprev->pnext = preplacement_this;
    else
        preplacement_list = preplacement_this;
    return(preplacement_list);
}

/* segregate block ranges into "before fat", "in fat" and "after fat". Used to guarantee that
   a replacement records will either be completeley inside or completely outside of the FAT.
   but will not straddle an edge. This simplifies the restore process */
static dword fs_blocks_in_region(dword block_no, dword n_blocks, dword fat_start, dword fat_end)
{
dword last_block;
    last_block = block_no + n_blocks - 1;
    if (block_no > fat_end)     /* Past fat so they are all in the same region */
        return(n_blocks);
    else if (block_no < fat_start) /* Preceeds fat */
    {
        if (last_block < fat_start)
            return(n_blocks);		/* They all preceed the FAT */
        else
            return(fat_start-block_no);
    }
    else /* inside fat */
    {
        if (last_block <= fat_end)
            return(n_blocks);
        else
        { /* 1-22-07 changed from return(fat_end-block_no); */
          /* This error caused the last last block in the FAT not
		     to be restored if it was dirty along with the first block
			 beyond the FAT, which is usually the first block in
			 the root directory */
            return(1+fat_end-block_no);
		}
    }
}
static void fs_free_replacement_record(FSRESTORE *prestore,FSBLOCKMAP *preplacement_this)
{
    if (preplacement_this)
    {
        ERTFS_ASSERT((prestore->replacement_queue.replacement_records_free < prestore->replacement_queue.total_replacement_records))
        prestore->replacement_queue.replacement_records_free += 1;
        preplacement_this->pnext = prestore->replacement_queue.preplacement_freelist;
        prestore->replacement_queue.preplacement_freelist = preplacement_this;
    }
}

static FSBLOCKMAP *fs_alloc_replacement_record(FSRESTORE *prestore)
{
FSBLOCKMAP *preplacement_this;

    preplacement_this = prestore->replacement_queue.preplacement_freelist;
    if (preplacement_this)
    {
    	if (prestore->replacement_queue.replacement_records_free==0)
    	{ /* Not expected under any circumstances */
        	ERTFS_ASSERT(rtfs_debug_zero())
        	return(0);
    	}
        prestore->replacement_queue.replacement_records_free -= 1;
        prestore->replacement_queue.preplacement_freelist = preplacement_this->pnext;
        return(preplacement_this);
    }
	else
	{
   		ERTFS_ASSERT((prestore->replacement_queue.replacement_records_free == 0))
        return(0);
	}
}

/* helper function -
   Write one record of the replacement list to the volume, and advance the
   list.

   Smart about rewinding to make multiple FAT block copies

*/

static BOOLEAN fs_asy_write_to_volume(FAILSAFECONTEXT *pfscntxt)
{
dword ltemp,fat_start,fat_end,block_offset;
BOOLEAN inside_fat;
byte *pdata;
FSRESTORE *prestore;
FSBLOCKMAP *preplacement;

    inside_fat = FALSE;
    fat_start =  pfscntxt->pdrive->drive_info.fatblock;
    fat_end =    pfscntxt->pdrive->drive_info.fatblock+pfscntxt->pdrive->drive_info.secpfat-1;
    prestore = CONTEXT_TO_RESTORE(pfscntxt);

    /* Nothing to do ? */
    preplacement = prestore->replacement_queue.preplacements_buffered;
    if (!preplacement)
        return(TRUE);

    /* Is the record inside the FAT ?
       It's guaranteed to be either completely in the FAT or completely out */
    if (fat_start <= preplacement->volume_blockno &&
        fat_end >= preplacement->volume_blockno)
    {
        inside_fat = TRUE;

        if ((preplacement->volume_blockno + preplacement->n_replacement_blocks-1) > fat_end)
		{ /* Not expected under any circumstances */
		  /* The first block is inside the fat, the last block is not */
        	ERTFS_ASSERT(rtfs_debug_zero())
			return(FALSE);
		}

        if (prestore->replacement_queue.num_fats_restored >= (int) pfscntxt->pdrive->drive_info.numfats)
		{ /* Not expected under any circumstances */
          /* The FAT we are writing to should be <= the number of FATs. */
        	ERTFS_ASSERT(rtfs_debug_zero())
			return(FALSE);
		}
        if (prestore->replacement_queue.num_fats_restored == 0)
        {
            if (!prestore->replacement_queue.preplacement_fat)
            {/* This is the first replacement in the queue for the FAT so save the first one
                so we can rewind and write additional fat copies later */
                prestore->replacement_queue.preplacement_fat = preplacement;
			}
            block_offset = 0;
        }
        else
        { /* Doing a FAT copy, add the earlier FAT sizes ti the block number when we write it */

            block_offset = (prestore->replacement_queue.num_fats_restored*pfscntxt->pdrive->drive_info.secpfat);
        }
        UPDATE_FSRUNTIME_STATS(pfscntxt, fat_synchronize_writes, 1)
        UPDATE_FSRUNTIME_STATS(pfscntxt, fat_synchronize_blocks_written, preplacement->n_replacement_blocks)
    }
    else
    {
        /* It is not in the FAT make sure block_offset and replacement QUEUE is zero */
        block_offset = 0;
        /* We expect it to already be zero but we clear it to be safe */
        ERTFS_ASSERT((prestore->replacement_queue.preplacement_fat==0))
        prestore->replacement_queue.preplacement_fat = 0;
        UPDATE_FSRUNTIME_STATS(pfscntxt, dir_synchronize_writes, 1)
        UPDATE_FSRUNTIME_STATS(pfscntxt, dir_synchronize_blocks_written, preplacement->n_replacement_blocks)
    }
#if (FS_DEBUG)
    FS_DEBUG_SHOWINT("fs_asy_write_to_volume - writing ", preplacement->n_replacement_blocks)
    FS_DEBUG_SHOWINT(" from location ", preplacement->journal_blockno)
    FS_DEBUG_SHOWINTNL(" to location ", preplacement->volume_blockno+block_offset)
#endif

    /* Write the blocks to the volume */
    /* Get the data address from the block number */
    ERTFS_ASSERT(preplacement->journal_blockno >= prestore->restore_buffer.first_block_in_buffer)
    ltemp = preplacement->journal_blockno - prestore->restore_buffer.first_block_in_buffer;

    if (!preplacement->n_replacement_blocks ||
    	(ltemp > prestore->restore_buffer.transfer_buffer_size) ||
    	((ltemp+preplacement->n_replacement_blocks) > prestore->restore_buffer.transfer_buffer_size))
	{ /* Not expected under any circumstances */
	  /* We expect at lest one block to write and the whole block range to be inside our buffer */
       	ERTFS_ASSERT(rtfs_debug_zero())
		return(FALSE);
	}
    ltemp *= pfscntxt->pdrive->drive_info.bytespsector;
    pdata = prestore->restore_buffer.transfer_buffer + ltemp;

#if (INCLUDE_DEBUG_TEST_CODE)
{
int debug_index = 0;
    if (inside_fat)
    {   /* Notify the diagostics when writing to the FAT. first copy */
        if (prestore->replacement_queue.num_fats_restored == 0)
            debug_index = RTFS_DEBUG_IO_J_RESTORE_FAT_WRITE;
    }
    else
    {   /* Notify the diagostics it is a block write unless it is
           a write to the infoblock. */
        debug_index = RTFS_DEBUG_IO_J_RESTORE_BLOCK_WRITE;
        if (pfscntxt->pdrive->drive_info.fasize == 8)
        {
            if (pfscntxt->pdrive->drive_info.infosec == preplacement->volume_blockno)
                debug_index = RTFS_DEBUG_IO_J_RESTORE_INFO_WRITE;
            else if (pfscntxt->pdrive->drive_info.infosec == preplacement->volume_blockno-6)
                debug_index = 0;    /* Don't notify on 2nd copy of info block */
        }
    }
    if (debug_index)
    {
        RTFS_DEBUG_LOG_DEVIO(pfscntxt->pdrive, debug_index, (preplacement->volume_blockno+block_offset),preplacement->n_replacement_blocks)
    }
}
#endif
    if (inside_fat && preplacement->volume_blockno == fat_start && pfscntxt->nv_cluster_handle && prestore->replacement_queue.num_fats_restored==1)
    {
    byte *_pdata;
    dword blockno, nblocks;
        /* Special case. If writing to the first block of the second copy of the FAT.  And the journal is hidden in
           freespace do not write the block, because it will overwrite our handle. We will update it when we are done
           inside failsafe_nv_buffer_clear */
        nblocks = preplacement->n_replacement_blocks - 1;
        if (nblocks)
        {   /* Write all but the first block */
            blockno = preplacement->volume_blockno+block_offset+1;
            _pdata = pdata + pfscntxt->pdrive->drive_info.bytespsector;
            if (!(raw_devio_xfer(pfscntxt->pdrive,blockno,_pdata, nblocks,FALSE, FALSE) ))
                return(FALSE);
        }
    }
    else
    {
        if (!(raw_devio_xfer(pfscntxt->pdrive,
            preplacement->volume_blockno+block_offset,pdata, preplacement->n_replacement_blocks,FALSE, FALSE) ))
            return(FALSE);
    }
    UPDATE_FSRUNTIME_STATS(pfscntxt, volume_block_writes, 1)
    UPDATE_FSRUNTIME_STATS(pfscntxt, volume_blocks_written, preplacement->n_replacement_blocks)
    if (inside_fat)
    {
        if (!preplacement->pnext || (preplacement->pnext->volume_blockno > fat_end))
        {   /* If off the end of list or past the fat region, reset the
               list pointer to the fat region pointer  */
            prestore->replacement_queue.num_fats_restored += 1;
            if (prestore->replacement_queue.num_fats_restored < (int) pfscntxt->pdrive->drive_info.numfats)
            {
                prestore->replacement_queue.preplacements_buffered = prestore->replacement_queue.preplacement_fat;
            }
            else
            { /* We finished writing the records at prestore->replacement_queue.preplacement_fat
                 release them all including preplacement */
            FSBLOCKMAP *ptemp,*pnext;
            dword loop_guard = GUARD_ENDLESS;

                prestore->replacement_queue.preplacements_buffered = preplacement->pnext;
                ptemp = prestore->replacement_queue.preplacement_fat;
                prestore->replacement_queue.preplacement_fat = 0;
               	while(ptemp)
               	{
					if (restore_check_endless_loop(&loop_guard))
						break;
					pnext = ptemp->pnext;
					fs_free_replacement_record(prestore,ptemp);
					if (ptemp == preplacement)
                      	break;
					ptemp = pnext;
               	}
               	if (ptemp != preplacement)
               	{ /* Not expected under any circumstances */
               		ERTFS_ASSERT(rtfs_debug_zero())
               		return(FALSE);
               	}
            }
        }
        else
            prestore->replacement_queue.preplacements_buffered = preplacement->pnext;
    }
    else
    {
        prestore->replacement_queue.preplacements_buffered = preplacement->pnext;
        fs_free_replacement_record(prestore,preplacement);
    }
    return(TRUE);
}


#endif /* (INCLUDE_FAILSAFE_CODE) */
