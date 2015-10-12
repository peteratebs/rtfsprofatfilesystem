/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* PRASYINT.C - Contains asynchronous state machine .

  The following public API routines are provided by this module:

*/

#include "rtfs.h"

#if (INCLUDE_ASYNCRONOUS_API)

int pc_efilio_async_flush_continue(PC_FILE *pefile);
int pc_queue_file_unlink(PC_FILE *pefile);
int _fs_api_async_commit_continue(DDRIVE *pdrive);
int _fs_api_async_flush_journal(DDRIVE *pdrive);

static int _pc_efilio_async_continue(PC_FILE *pefile);
static int  pc_async_open_continue(PC_FILE *pefile);
static void  pc_async_open_error(PC_FILE *pefile);
static BOOLEAN pc_async_open_complete(PC_FILE *pefile);
dword _get_operating_flags(PC_FILE *pefile);
int pc_efilio_async_open32_continue(PC_FILE *pefile);


/* #define ASYNC_ANNOUNCE(A) RTFS_PRINT_STRING_1((byte *)A, PRFLG_NL); */
#define ASYNC_ANNOUNCE(A)

static void _pc_drive_announce_complete(DDRIVE *pdrive, int op, int success)
{
	if (prtfs_cfg->test_drive_async_complete_cb)	/* Call the override function if provided, only used for testing */
		prtfs_cfg->test_drive_async_complete_cb(pdrive->driveno, op, success);
	else
		rtfs_app_callback(RTFS_CBA_ASYNC_DRIVE_COMPLETE, pdrive->driveno, op, success, 0);
}



static int _pc_async_transition(DDRIVE *pdrive, int target_state);

int _pc_async_step(DDRIVE *pdrive, int target_state, int steps)
{
    int ret_val;

    ret_val = PC_ASYNC_CONTINUE;
    if (steps == 0) /* Steps == 0 is INF */
        steps =-1;

    /* Handle async mount seperately */
    if (pdrive->drive_state.drive_async_state == DRV_ASYNC_MOUNT)
    {

        while(steps--)
        {
            UPDATE_RUNTIME_STATS(pdrive, async_steps, 1)
#if (INCLUDE_EXFATORFAT64)
			if (ISEXFATORFAT64(pdrive))	/* exFAT mount completes synchronously, fall through and report sucess */
				ret_val= PC_ASYNC_COMPLETE;
			else
#endif
				ret_val = fatop_page_continue_check_freespace(pdrive);
            if (ret_val == PC_ASYNC_COMPLETE)
            {
                pc_i_dskopen_complete(pdrive);
                /* Alert the user that the async process is completed */
                _pc_drive_announce_complete(pdrive, DRV_ASYNC_MOUNT, 1);
                pdrive->drive_state.drive_async_state = DRV_ASYNC_IDLE;
                rtfs_app_callback(RTFS_CBA_INFO_MOUNT_COMPLETE, pdrive->driveno, 0, 0, 0);
                break;
             }
            else if (ret_val == PC_ASYNC_ERROR)
            {
                /* Alert the user that the async process is completed */
                pdrive->drive_state.drive_async_state = DRV_ASYNC_IDLE;
                _pc_drive_announce_complete(pdrive, DRV_ASYNC_MOUNT, 0);
                rtfs_app_callback(RTFS_CBA_INFO_MOUNT_FAILED, pdrive->driveno, 0, 0, 0);
				break;
            }
        }
        return(ret_val);
    }
    /* We're already past the done mount target state so return if that's
       the target */
    if (target_state == DRV_ASYNC_DONE_MOUNT)
        return(PC_ASYNC_COMPLETE);

    ret_val = PC_ASYNC_CONTINUE;
    while(steps--)
    {
        UPDATE_RUNTIME_STATS(pdrive, async_steps, 1)
        ret_val = _pc_async_transition(pdrive, target_state);
        if (ret_val != PC_ASYNC_CONTINUE)
            return(ret_val);
        if (pdrive->drive_state.drive_async_state == DRV_ASYNC_IDLE)
        { /* We are idle so return */
            return(PC_ASYNC_COMPLETE);
        }
#if (INCLUDE_FAILSAFE_CODE)
        if (pdrive->drive_state.drive_async_state == DRV_ASYNC_RESTORE)
        { /* We are restoring the FAT from failafe */
            ASYNC_ANNOUNCE("\nIn ASYNC RESTORE\n")
            ret_val = _fs_api_async_commit_continue(pdrive);
            if (ret_val == PC_ASYNC_ERROR)
            {
                _pc_drive_announce_complete(pdrive, DRV_ASYNC_RESTORE, 0);
                pdrive->drive_state.drive_async_state = DRV_ASYNC_IDLE;
                break;
            }
            else if (ret_val == PC_ASYNC_COMPLETE)
            {
                ASYNC_ANNOUNCE("\nCompleted ASYNC RESTORE\n")
                _pc_drive_announce_complete(pdrive, DRV_ASYNC_RESTORE, 1);
                pdrive->drive_state.drive_async_state = DRV_ASYNC_IDLE;
                /* If target completed return otherwise if another target was
                   requested keep looping until we complete */
                if (target_state == DRV_ASYNC_DONE_RESTORE)
                    return(PC_ASYNC_COMPLETE);
            }
        }
        else if (pdrive->drive_state.drive_async_state == DRV_ASYNC_JOURNALFLUSH)
        { /* We are flushing the Faisafe Journal */
            ASYNC_ANNOUNCE("\nIn ASYNC JOURNALFLUSH\n")
            ret_val = _fs_api_async_flush_journal(pdrive);
            if (ret_val == PC_ASYNC_ERROR)
            {
                _pc_drive_announce_complete(pdrive, DRV_ASYNC_JOURNALFLUSH, 0);
                pdrive->drive_state.drive_async_state = DRV_ASYNC_IDLE;
                break;
            }
            else if (ret_val == PC_ASYNC_COMPLETE)
            {
                ASYNC_ANNOUNCE("\nIn ASYNC JOURNALFLUSH\n")
                _pc_drive_announce_complete(pdrive, DRV_ASYNC_JOURNALFLUSH, 1);
                pdrive->drive_state.drive_async_state = DRV_ASYNC_IDLE;
                /* If target completed return otherwise if another target was
                   requested keep looping until we complete */
                if (target_state == DRV_ASYNC_DONE_JOURNALFLUSH)
                    return(PC_ASYNC_COMPLETE);
            }
        }
        else
#endif
        {
            if (pdrive->drive_state.drive_async_state == DRV_ASYNC_FATFLUSH)
            { /* We are flushing the FAT from failafe */
                ASYNC_ANNOUNCE("\nIn ASYNC FLUSH\n")
#if (RTFS_CFG_READONLY) /* Read only file system, set flush complete status */
                ret_val = PC_ASYNC_COMPLETE;
#else
                ret_val = pc_async_flush_fat_blocks(pdrive,1);
#endif
                if (ret_val == PC_ASYNC_ERROR)
                {
                    _pc_drive_announce_complete(pdrive, DRV_ASYNC_FATFLUSH, 0);
                    pdrive->drive_state.drive_async_state = DRV_ASYNC_IDLE;
                    break;
                }
                else if (ret_val == PC_ASYNC_COMPLETE)
                {
                    ASYNC_ANNOUNCE("\nDone ASYNC FLUSH\n")
                    _pc_drive_announce_complete(pdrive, DRV_ASYNC_FATFLUSH, 1);
                    pdrive->drive_state.drive_async_state = DRV_ASYNC_IDLE;
                    /* If target completed return otherwise if another target was
                    requested keep looping until we complete */
                    if (target_state == DRV_ASYNC_DONE_FATFLUSH)
                        return(PC_ASYNC_COMPLETE);
                }
            }
            else if (pdrive->drive_state.drive_async_state == DRV_ASYNC_FILES)
            { /* We are processing file operations */
                ASYNC_ANNOUNCE("\nIn ASYNC FILES\n")
                ret_val = _pc_efilio_async_continue(pdrive->drive_state.asy_file_pfirst);
                if (ret_val == PC_ASYNC_ERROR)
                {
                    pdrive->drive_state.drive_async_state = DRV_ASYNC_IDLE;
                    break;
                }
                else if (ret_val == PC_ASYNC_COMPLETE)
                {
                    ASYNC_ANNOUNCE("\nDone ASYNC FILES\n")
                    pdrive->drive_state.drive_async_state = DRV_ASYNC_IDLE;
                    /* __pc_async_transition() will test if we are done */
                }
            }
        }
    }
    /* Make the IDLE handler the only one who can return COMPLETE */
    if (ret_val != PC_ASYNC_ERROR)
        ret_val = PC_ASYNC_CONTINUE;
    return(ret_val);
}

static int _pc_async_transition(DDRIVE *pdrive, int target_state)
{
    if (pdrive->drive_state.drive_async_state != DRV_ASYNC_IDLE)
        return(PC_ASYNC_CONTINUE);

    /* Process pending files first */
    if (pdrive->drive_state.asy_file_pfirst)
    {
        pdrive->drive_state.drive_async_state = DRV_ASYNC_FILES;
        if (target_state == DRV_ASYNC_FILES)
            return(PC_ASYNC_COMPLETE);
        else
            return(PC_ASYNC_CONTINUE);
    }
    else
    {
        if (target_state == DRV_ASYNC_DONE_FILES)
            return(PC_ASYNC_COMPLETE);
     }
    /* Next process FAT flushes if FAT is dirty and if ASYNC FAT Flush
       or async Journal Flush is enabled. If Async Journal flush is
       enabled we flush fat regardless of FAT Flush state */
    if (pdrive->drive_info.drive_operating_flags & DRVOP_FAT_IS_DIRTY  &&
        ( (pdrive->drive_info.drive_operating_flags & (DRVOP_ASYNC_FFLUSH|DRVOP_ASYNC_JFLUSH)) ||
          (pdrive->du.drive_operating_policy & (DRVPOL_ASYNC_FATFLUSH|DRVPOL_ASYNC_JOURNALFLUSH))))
    {
        pdrive->drive_state.drive_async_state = DRV_ASYNC_FATFLUSH;
        pdrive->drive_info.drive_operating_flags &= ~DRVOP_ASYNC_FFLUSH;
        if (target_state == DRV_ASYNC_FATFLUSH)
            return(PC_ASYNC_COMPLETE);
        else
            return(PC_ASYNC_CONTINUE);
    }
    else
    {
        if (target_state == DRV_ASYNC_DONE_FATFLUSH)
            return(PC_ASYNC_COMPLETE);
     }
#if (INCLUDE_FAILSAFE_CODE)
    /* Next process Journal flushes */
    if (pdrive->drive_info.drive_operating_flags & DRVOP_FS_NEEDS_FLUSH &&
        ( (pdrive->du.drive_operating_policy & DRVPOL_ASYNC_JOURNALFLUSH) ||
           (pdrive->drive_info.drive_operating_flags & DRVOP_ASYNC_JFLUSH) ))
    {
        pdrive->drive_state.drive_async_state = DRV_ASYNC_JOURNALFLUSH;
        pdrive->drive_info.drive_operating_flags &= ~DRVOP_ASYNC_JFLUSH;
        if (target_state == DRV_ASYNC_JOURNALFLUSH)
            return(PC_ASYNC_COMPLETE);
        else
            return(PC_ASYNC_CONTINUE);
    }
    else
    {
        if (target_state == DRV_ASYNC_DONE_JOURNALFLUSH)
            return(PC_ASYNC_COMPLETE);
    }
    /* Next process Journal restores */
    if ( pdrive->drive_info.drive_operating_flags & DRVOP_FS_NEEDS_RESTORE &&
        ( (pdrive->du.drive_operating_policy & DRVPOL_ASYNC_RESTORE) ||
           (pdrive->drive_info.drive_operating_flags & DRVOP_ASYNC_JRESTORE) ) )
    {
        pdrive->drive_state.drive_async_state = DRV_ASYNC_RESTORE;
        pdrive->drive_info.drive_operating_flags &= ~DRVOP_ASYNC_JRESTORE;
        if (target_state == DRV_ASYNC_RESTORE)
            return(PC_ASYNC_COMPLETE);
        else
            return(PC_ASYNC_CONTINUE);
    }
    else
    {
        if (target_state == DRV_ASYNC_DONE_RESTORE)
            return(PC_ASYNC_COMPLETE);
    }
#endif
    return(PC_ASYNC_COMPLETE); /* No transition from idle */
}


static int _pc_efilio_async_continue(PC_FILE *pefile)
{
    int ret_val;
    dword operating_flags;

    if (!pefile)
        return(PC_ASYNC_COMPLETE);

    operating_flags = _get_operating_flags(pefile);
    ret_val = PC_ASYNC_COMPLETE;
    if ((operating_flags & FIOP_ASYNC_ALL_OPS) == 0)
        goto ex_it; /* nothing to do */
    else if (operating_flags & FIOP_ASYNC_OPEN)
        ret_val = pc_async_open_continue(pefile);
#if (!RTFS_CFG_READONLY)    /* Read-only file system. do not flush file */
    else if (operating_flags & FIOP_ASYNC_FLUSH)
        ret_val = pc_efilio_async_flush_continue(pefile);
#endif
    if (ret_val == PC_ASYNC_ERROR)
    { /* Common Error processing. Set async error state and free structures if need be */
        operating_flags &= ~FIOP_NEEDS_FLUSH; /* Make sure close doesn't try
                                                 to flush */
        _set_asy_operating_flags(pefile,operating_flags & ~FIOP_ASYNC_ALL_OPS,0);
        if (operating_flags & FIOP_ASYNC_OPEN)
        {
            pc_async_open_error(pefile);
        }
    }
    else if (ret_val == PC_ASYNC_COMPLETE)
    { /* Complete processing */
        if (operating_flags & FIOP_ASYNC_FLUSH)
        { /* Clear async processing and tell the user success */
            operating_flags &= ~FIOP_NEEDS_FLUSH; /* Was done already but to be sure */
#if (RTFS_CFG_READONLY) /* Read only file system make sure FIOP_ASYNC_UNLINK not executed */
            operating_flags &= ~FIOP_ASYNC_UNLINK;
#endif
            _set_asy_operating_flags(pefile,
                        operating_flags & ~FIOP_ASYNC_ALL_OPS, 1);
            /* If flush completed, see if we are deleting too */
            if (operating_flags & FIOP_ASYNC_UNLINK)
            { /* Complete the UNLINK now */
#if (!RTFS_CFG_READONLY) /* Read only file system FIOP_ASYNC_UNLINK not executed */
                dword loc_op_flags;
                loc_op_flags = operating_flags;
                loc_op_flags &= ~FIOP_NEEDS_FLUSH; /* So close doesn't try to flush */
                loc_op_flags &= ~FIOP_ASYNC_ALL_OPS;
                if (pc_rmnode(pefile->pobj))
                {
                    _set_asy_operating_flags(pefile,loc_op_flags, 1);
                }
                else
                {
                    ret_val = PC_ASYNC_ERROR;
                    _set_asy_operating_flags(pefile,loc_op_flags, 0);
                }
                /* free the file structure */
                _pc_efilio_close(pefile);
#endif
            }
            /* If flush completed, see if we are closing too */
            else  if (operating_flags & FIOP_ASYNC_CLOSE)
            {  /* free the file structure */
                _pc_efilio_close(pefile);
            }
        }
        else if (operating_flags & FIOP_ASYNC_OPEN)
        { /* Finish open processing */
            if (pc_async_open_complete(pefile))
            {/* transition to UNLINK if it is the next step */
            dword loc_op_flags;
                loc_op_flags = operating_flags;
                loc_op_flags &= ~FIOP_ASYNC_OPEN;
                if (operating_flags & FIOP_ASYNC_UNLINK)
                {
#if (RTFS_CFG_READONLY) /* Read only file system don't enter UNLINK state */
                    /* This will not happen because the state is never entered */
                    operating_flags &= ~FIOP_ASYNC_UNLINK;
                    loc_op_flags &= ~FIOP_ASYNC_UNLINK;
                    ret_val = PC_ASYNC_ERROR;
#else
                    /* Queue all segments for delete (calls truncate) */
                    ret_val = pc_queue_file_unlink(pefile);
                    if (ret_val != PC_ASYNC_ERROR)
                    {
                        /* If no free manager available free clusters now */
                        if (CHECK_FREEMG_CLOSED(pefile->pobj->pdrive))
                        {
                            if (!_pc_efilio_flush(pefile))
	                            ret_val = PC_ASYNC_ERROR;
                        }
                    }
                    if (ret_val != PC_ASYNC_ERROR)
                    {
                        /*  Change to Flush state, (needs flush should be set already but be sure) */
                        ret_val = PC_ASYNC_CONTINUE; /* next time unlink */
                        loc_op_flags |= FIOP_NEEDS_FLUSH|FIOP_ASYNC_FLUSH;
                    }
#endif
                }
                /* Now either resume with unlink or report open done */
                _set_asy_operating_flags(pefile,loc_op_flags, 1);
            }
            else
                ret_val = PC_ASYNC_ERROR;
        }
        else
        { /* Default COMPLETE processing */
            _set_asy_operating_flags(pefile,operating_flags&~FIOP_ASYNC_ALL_OPS, 1);
        }
    }
ex_it:
    return(ret_val);
}




static int  pc_async_open_continue(PC_FILE *pefile)
{
    return(pc_efilio_async_open32_continue(pefile));
}


static void  pc_async_open_error(PC_FILE *pefile)
{
    pc_efilio32_open_error(pefile);
}

static BOOLEAN pc_async_open_complete(PC_FILE *pefile)
{
dword operating_flags;
    operating_flags = _get_operating_flags(pefile);
    {
        if (!pc_efilio32_open_complete(pefile))
        {
            _set_asy_operating_flags(pefile,
                operating_flags & ~FIOP_ASYNC_ALL_OPS, 0);
            pc_efilio32_open_error(pefile);
            return(FALSE);
        }
    }
    return(TRUE);
}
#endif /* (INCLUDE_ASYNCRONOUS_API) */
