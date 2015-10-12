/**************************************************************************
    APPCMDFS.C   - Interactive failsafe commands. Called by the command
                   shell for RtfsPro and RtfsProPlus
*****************************************************************************
<TEST>  Test File:   rtfscommon/apps/appcmdshfs.c
<TEST>  Description: Interactive command commands that call Rtfs ProPlus failsafe subroutines.
*/

#include "rtfs.h"
#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */



/* Control failsafe

failsafe DRIVEID: command

        Control Failsafe operations for the disk.

       usage:
        fs command D:

*/
#if (INCLUDE_FAILSAFE_CODE)

#if (INCLUDE_ASYNCRONOUS_API) /* ProPlus use async API to commit */
extern int do_async; /* If TRUE use asynchs versions of API calls */
BOOLEAN _do_async_complete(int driveno, BOOLEAN manual);
#endif

#if (INCLUDE_DEBUG_TEST_CODE)
void pc_fstest_main(byte *pdriveid);
#endif
/* See apputil.c */
void show_status(char *prompt, dword val, int flags);
long rtfs_atol(byte * s);
int rtfs_atoi(byte * s);
int parse_args(int agc, byte **agv, char *input_template);
int rtfs_args_arg_count(void);
dword rtfs_args_val_hi(int this_arg);
dword rtfs_args_val_lo(int this_arg);
byte *rtfs_args_val_text(int this_arg);
void use_args(int agc, byte **agv);
byte *pc_ltoa(dword num, byte *dest, int number_base);

#define _rtfs_cs_strcmp(A,B,C) rtfs_cs_strcmp((byte *) (A) , (byte *) (B), C)

extern FAILSAFECONTEXT *gl_failsafe_contexts[26];

/*
<TEST>  Procedure:   do_efile_failsafe()
<TEST>  Description: Manually perform Failsafe commands
<TEST>   Invoke by typing "fs" in the extended command shell or "FS" in the basic command shell with the following options
<TEST>   abort       - Abort the current mount without flushing
<TEST>   exit        - Flush journal file and FAT stop journalling
<TEST>   enter       - Begin Journalling
<TEST>   jcommit     - Flush journal file but not FAT
<TEST>                 Stops journalling and aborts
<TEST>                 Leaves a restorable Journal
<TEST>   jflush      - Flush journal file but not FAT
<TEST>                 Continue journalling
<TEST>   commit      - Flush journal file and synchronize FAT volume
<TEST>   info        - Display information about current Failsafe Session
<TEST>   clear       - Reset Journal IO statistics
<TEST>   restore     - Restore the volume from current Journal File
*/
extern int fs_flush_behavior; /* In user callback section but used by shell to modify behavior */

int do_efile_failsafe(int agc, byte **agv)
{
    FSINFO fsinfo;
    byte *pdriveid,*pcommandstring;
    int drivenumber;
     DDRIVE *pdr;
    FAILSAFECONTEXT *pfscntxt;

    /* delete filename */
    if (!parse_args(agc, agv,"TT"))
    {
usage:
       rtfs_print_one_string((byte *)"       usage:", PRFLG_NL);
       rtfs_print_one_string((byte *)"        fs command D:", PRFLG_NL);
       rtfs_print_one_string((byte *)"        where command is:", PRFLG_NL);
       rtfs_print_one_string((byte *)"    ", PRFLG_NL);
       rtfs_print_one_string((byte *)"        autodisable - Disable auto Failsafe. Failsafe is controlled by FS command", PRFLG_NL);
       rtfs_print_one_string((byte *)"        autoenable  - Return control to auto Failsafe.", PRFLG_NL);
       rtfs_print_one_string((byte *)"        abort       - Abort the current mount without flushing", PRFLG_NL);
       rtfs_print_one_string((byte *)"        exit        - Flush journal file and FAT stop journalling", PRFLG_NL);
       rtfs_print_one_string((byte *)"        enter       - Begin Journalling ", PRFLG_NL);
       rtfs_print_one_string((byte *)"        jcommit     - Flush journal file but not FAT ", PRFLG_NL);
       rtfs_print_one_string((byte *)"                      Stops journalling and aborts   ", PRFLG_NL);
       rtfs_print_one_string((byte *)"                      Leaves a restorable Journal    ", PRFLG_NL);
       rtfs_print_one_string((byte *)"        jflush      - Flush journal file but not FAT ", PRFLG_NL);
       rtfs_print_one_string((byte *)"                      Continue journalling           ", PRFLG_NL);
       rtfs_print_one_string((byte *)"        commit      - Flush journal file and synchronize FAT volume ", PRFLG_NL);
       rtfs_print_one_string((byte *)"        info        - Display information about current Failsafe Session", PRFLG_NL);
       rtfs_print_one_string((byte *)"        clear       - Reset Journal IO statistics", PRFLG_NL);
       rtfs_print_one_string((byte *)"        restore     - Restore the volume from current Journal File", PRFLG_NL);
#if (INCLUDE_DEBUG_TEST_CODE)
       rtfs_print_one_string((byte *)"        test        - Run Failsafe test suite", PRFLG_NL);
#endif
       return(-1);
    }

    pcommandstring   = rtfs_args_val_text(0);
    if (!pcommandstring)
        goto usage;
    if (rtfs_args_arg_count() == 2)
    {
        pdriveid   = rtfs_args_val_text(1);
        drivenumber = *pdriveid - 'A';
        if (drivenumber < 0 || drivenumber > 25)
            goto usage;
    }
    else
    {
            goto usage;
    }

    /* Make sure we are mounted so we can access the pdr structure */
    pc_set_default_drive(rtfs_args_val_text(1));
    pc_set_cwd((byte *)"\\");

    pdr = pc_drno_to_drive_struct(drivenumber);
    if (pdr)
        pfscntxt = (FAILSAFECONTEXT *) pdr->du.user_failsafe_context;
    else
        pfscntxt = 0;

    if (_rtfs_cs_strcmp(pcommandstring,"autodisable", CS_CHARSET_NOT_UNICODE) == 0)
	{
    	if (!pdr)
		{
        	rtfs_print_one_string((byte *)" No drive found : ", PRFLG_NL);
        	return(0);
		}
        rtfs_print_one_string((byte *)" Warning ..............: ", PRFLG_NL);
        rtfs_print_one_string((byte *)"         To return to automatic mode you must", PRFLG_NL);
        rtfs_print_one_string((byte *)"         restart or invoke the autoenable command ", PRFLG_NL);
		fs_flush_behavior = FS_CB_CONTINUE;

    	pdr->du.drive_operating_policy |= DRVPOL_DISABLE_AUTOFAILSAFE;
    	pdr->drive_info.drive_operating_flags |= DRVPOL_DISABLE_AUTOFAILSAFE;
        /* Abort Journalling and current mount */
        fs_api_disable(pdriveid, TRUE);
        return(0);
	}
    else if (_rtfs_cs_strcmp(pcommandstring,"autoenable", CS_CHARSET_NOT_UNICODE) == 0)
	{
    	if (!pdr)
		{
        	rtfs_print_one_string((byte *)" No drive found : ", PRFLG_NL);
        	return(0);
		}
    	pdr->du.drive_operating_policy &= ~DRVPOL_DISABLE_AUTOFAILSAFE;
    	pdr->drive_info.drive_operating_flags &= ~DRVPOL_DISABLE_AUTOFAILSAFE;
		fs_flush_behavior = FS_CB_SYNC;

        /* Abort Journalling and current mount */
        fs_api_disable(pdriveid, TRUE);
        return(0);
	}
    else if (pdr && !(pdr->du.drive_operating_policy & DRVPOL_DISABLE_AUTOFAILSAFE))
    {
        rtfs_print_one_string((byte *)" Warning ..............: ", PRFLG_NL);
        rtfs_print_one_string((byte *)" enter, abort, exit, jcommit, jflush, commit and restore commands may not do", PRFLG_NL);
        rtfs_print_one_string((byte *)" what you expect until you invoke the autodisable command", PRFLG_NL);
	}

#if (INCLUDE_DEBUG_TEST_CODE)
    if (_rtfs_cs_strcmp(pcommandstring,"test", CS_CHARSET_NOT_UNICODE) == 0)
	{
    	pc_fstest_main(pdriveid);
        return(0);
	}
#endif
    if (_rtfs_cs_strcmp(pcommandstring,"abort", CS_CHARSET_NOT_UNICODE) == 0)
    {
        /* Abort Journalling and current mount */
        fs_api_disable(pdriveid, TRUE);
        return(0);
    }
    if (_rtfs_cs_strcmp(pcommandstring,"exit", CS_CHARSET_NOT_UNICODE) == 0)
    {
        /* Commit Failsafe then exit failsafe mode */
        fs_api_disable(pdriveid, FALSE);
        return(0);
    }
    else if (_rtfs_cs_strcmp(pcommandstring,"enter", CS_CHARSET_NOT_UNICODE) == 0)
    {
/*        _do_efile_apply_config(); */
        fs_api_enable(pdriveid, TRUE);
    }
    else if (_rtfs_cs_strcmp(pcommandstring,"jcommit", CS_CHARSET_NOT_UNICODE) == 0)
    {
    /* Two arguments are
        1 Update FAT structures
        2 Clear Journal File (only valid if update FAT is true
    */
        /* Flush and close journal file but don't update FAT Don't clear the
           journal file */
        fs_api_commit(pdriveid, FALSE);
        /* Abort Journalling and current mount */
        fs_api_disable(pdriveid, TRUE);
    }
    else if (_rtfs_cs_strcmp(pcommandstring,"jflush", CS_CHARSET_NOT_UNICODE) == 0)
    {
    /* Two arguments are
        1 Update FAT structures
        2 Clear Journal File (only valid if update FAT is true
    */
        /* Flush and close journal file but don't update FAT Don't clear the
           journal file */
        fs_api_commit(pdriveid, FALSE);
    }
    else if (_rtfs_cs_strcmp(pcommandstring,"clear", CS_CHARSET_NOT_UNICODE) == 0)
    { /* Clear IO statistics */
        if (pfscntxt)
        {
#if (INCLUDE_DEBUG_RUNTIME_STATS)
            rtfs_memset(&pfscntxt->stats, 0, sizeof(pfscntxt->stats));
#endif
        }
    }
    else if (_rtfs_cs_strcmp(pcommandstring,"commit", CS_CHARSET_NOT_UNICODE) == 0)
    {
    /* Three arguments are
        1 FLUSH FailSafe file,
        2 Update FAT structures
        3 Clear Journal File
    */
        /* Flush journal file and update FAT so the journal and the
           disk volume are in sync, don't clear the journal file, we
           can still look at it.
        */
#if (INCLUDE_ASYNCRONOUS_API) /* ProPlus use async API to commit */
        if (do_async)
        {
        int ret_val;
            ret_val = fs_api_async_commit_start(pdriveid);
            if (ret_val == PC_ASYNC_ERROR)
                rtfs_print_one_string((byte *)"    Cannot start async", PRFLG_NL);
            else if (ret_val == PC_ASYNC_COMPLETE)
                rtfs_print_one_string((byte *)"    Up to date", PRFLG_NL);
            else if (ret_val == PC_ASYNC_CONTINUE)
            {
                if (!_do_async_complete(pc_path_to_driveno(pdriveid, CS_CHARSET_NOT_UNICODE),FALSE))
                    rtfs_print_one_string((byte *)"    async commit complete failed", PRFLG_NL);
            }
            return(0);
        }
#endif
        fs_api_commit(pdriveid, TRUE);
        return(0);
    }
    else if (_rtfs_cs_strcmp(pcommandstring,"info", CS_CHARSET_NOT_UNICODE) == 0)
    {
          /* Not enable, stat the journal file */
        if (!fs_api_info(pdriveid, &fsinfo))
        {
            rtfs_print_one_string((byte *)"Journal not active and No Journal File Found", PRFLG_NL);
            return(0);
        }
        if (fsinfo.journaling)
        {
            rtfs_print_one_string((byte *)"Failsafe Journalling active", PRFLG_NL);
            rtfs_print_one_string((byte *)" Needs Flush      ..............: ", 0);
            if (fsinfo.needsflush)
                rtfs_print_one_string((byte *)" YES", PRFLG_NL);
            else
                rtfs_print_one_string((byte *)" NO", PRFLG_NL);
        }
        else
        {
            rtfs_print_one_string((byte *)"Failsafe Journalling not active", PRFLG_NL);
            if (!fsinfo.journal_file_valid)
            {
                rtfs_print_one_string((byte *)"No Journal File", PRFLG_NL);
                if (fsinfo.check_sum_fails)
                    rtfs_print_one_string((byte *)"Checksum Error", PRFLG_NL);
                return(0);
            }
        }
        {
        byte version_in_hex[32];
        pc_ltoa(fsinfo.version, &version_in_hex[0], 16);
        rtfs_print_one_string((byte *)" Failsafe Version ..............: ", 0);
        rtfs_print_one_string((byte *)version_in_hex, PRFLG_NL);
        }
        show_status(" File Size .....................: ",
            fsinfo.filesize, PRFLG_NL);
        show_status(" Occupied replacement Blocks ...: ",
            fsinfo.numblocksremapped, PRFLG_NL);
        show_status(" File Drive Freespace Signature.: ",
            fsinfo.journaledfreespace, PRFLG_NL);
        show_status(" Current Drive Freespace .......: ",
            fsinfo.currentfreespace, PRFLG_NL);
        show_status(" Journal File Location   .......: ",
            fsinfo.journal_block_number, PRFLG_NL);
         if (fsinfo.out_of_date)
            rtfs_print_one_string((byte *)"Journal File older than FAT", PRFLG_NL);


        if (pfscntxt)
        {
#if (!INCLUDE_DEBUG_RUNTIME_STATS)
            rtfs_print_one_string((byte *)"INCLUDE_DEBUG_RUNTIME_STATS is disabled", PRFLG_NL);
#else
            rtfs_print_one_string((byte *)" Journal File IO History .......: ", PRFLG_NL);
            show_status(" Index block read calls  .......: ", pfscntxt->stats.journal_index_reads, PRFLG_NL);
            show_status(" Index block write calls .......: ", pfscntxt->stats.journal_index_writes, PRFLG_NL);
            show_status(" Data  block read calls  .......: ", pfscntxt->stats.journal_data_reads, PRFLG_NL);
            show_status(" Data  blocks read       .......: ", pfscntxt->stats.journal_data_blocks_read, PRFLG_NL);
            show_status(" Data  block write calls .......: ", pfscntxt->stats.journal_data_writes, PRFLG_NL);
            show_status(" Data  blocks written    .......: ", pfscntxt->stats.journal_data_blocks_written, PRFLG_NL);
            show_status(" Restore Read Calls     .......: ",  pfscntxt->stats.restore_data_reads, PRFLG_NL);
            show_status(" Restore blocks read    .......: ",  pfscntxt->stats.restore_data_blocks_read, PRFLG_NL);
            show_status(" Restore write calls    .......: ",  pfscntxt->stats.restore_write_calls, PRFLG_NL);
            show_status(" Restore blocks written .......: ",  pfscntxt->stats.restore_blocks_written, PRFLG_NL);
#endif
        }
        return(0);
    }
    else if (_rtfs_cs_strcmp(pcommandstring,"restore", CS_CHARSET_NOT_UNICODE) == 0)
    {
        /* do restore, do clear journal after restore */
        fs_api_restore(pdriveid);
        return(0);
    }
    else
    {
        goto usage;
    }
    return(0);
}

#endif /* (INCLUDE_FAILSAFE_CODE) */
#endif /* Exclude from build if read only */
