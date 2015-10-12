/*
* EBS - RTFS (Real Time File Manager)
* FAILSSAFEV3 - Need reworking
* Copyright EBS Inc. 1987-2005
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* PRFSAPI.C - Contains user api level source code for ERTFS-Pro
   failsafe functions.

   The following routines are included:

   BOOLEAN fs_api_enable(byte *drivename, BOOLEAN clear_journal)
   BOOLEAN fs_api_disable(byte *drive_name, BOOLEAN abort)
   BOOLEAN fs_api_commit(byte *path,BOOLEAN synch_fats)
   int _fs_api_async_flush_journal(DDRIVE *pdrive)
   int _fs_api_async_commit_continue(DDRIVE *pdrive)
   BOOLEAN fs_api_info(byte *drive_name,FSINFO *pinfo)
*/

#include "rtfs.h"

#if (INCLUDE_FAILSAFE_CODE)

/* fs_api_restore - Restore the volume from the failsafe journal
*
*   Summary:
*     int fs_api_restore(byte *drive_name)
*
*
* Description:
*
* This outine may be called from the application layer to restore a volume.
*
* If the application is responsible for executing retore procedures it
* should use fs_api_info() to determine if a restore is required and
* then call this routine to perform the operation.
*
*  Alternatively the application may use choose to use the
*  callback routine fs_api_cb_restore() to instruct RTFS to
*  automatically perform the restore if ot is needed.
*
*
* Returns:
*   TRUE         - Success
*   FALSE        - Failure
*
*   If FALSE is returned errno will be set to one of the following.
*
*   PEINVALIDDRIVEID        - Drive argument invalid
*   PENOINIT                - fs_api_enable must be called first
*   PEEINPROGRESS			- Cant perform operation because ASYNC
*                             commit operation in progress
*   PEFSNOJOURNAL           - No Journal file found
*   PEIOERRORWRITEJOURNAL   - Error writing journal file or NVRAM section
*   PEIOERRORREADJOURNAL    - Error writing journal file or NVRAM section
*   PEIOERRORWRITEFAT       - Error writing fat area
*   PEIOERRORWRITEBLOCK     - Error writing directory area
*   PEINTERNAL              - internal error, only occurs if ERTFS is not configured.
*
*   An ERTFS system error
*
*
*/
/* See prfsfile */

/* fs_api_info - Returns stats on the Failsafe session or Journal File
*
*   Summary:
*       BOOLEAN fs_api_info(byte *drive_name,FSINFO *pinfo)
*
*
* Description:
*
* If Failsafe is currently Journaling, this routine returns the status of the
* current journaling session and the fields of the info structure contain
* the following values
*
*   BOOLEAN journaling;         - Always TRUE
*   BOOLEAN journal_file_valid; - TRUE if the journal is flushed
*   dword version;              - 3
*   dword status;               - FS_STATUS_JOURNALING
*   dword numindexblocks;       - Index block in the Journal
*   dword totalremapblocks;     - Total number of remap blocks
*   dword numblocksremapped;    - Total number of blocks remapped
*   dword journaledfreespace;   - Current Freesapce
*   dword currentfreespace;     - Current Freesapce
*   dword currentchecksum;      - Current Index Check Sum
*   dword indexchecksum;        - Current Index Check Sum
*   dword journal_block_number; - Physical location of the Journal
*   dword filesize;             - Size of the Journal in blocks
*   BOOLEAN needsflush;         - TRUE is Journal must be flushed
*   BOOLEAN out_of_date;        - FALSE
*   BOOLEAN check_sum_fails;    - FALSE
*   BOOLEAN restore_required;   - FALSE
*   BOOLEAN restore_recommended;- FALSE
*
* If Failsafe is not currently Journaling, this routine returns the status
* of the current Journal file and the fields of the info structure contain
* the following values .
*
*   BOOLEAN journaling;         - FALSE
*   BOOLEAN journal_file_valid; - TRUE if the journal is valid
*   dword version;              - 3
*   dword status;               -
*       FS_STATUS_JOURNALING    - A Journalling session was interrupted
*                                 before the Journal file was flushed,
*                                 a restore can not be  done from the file.
*       FS_STATUS_RESTORING     - A restore operation was interrupted and
*                                 should be completed.
*       FS_STATUS_NEEDS_RESTORE - The Journal is flushed but the FAT
*                                 needs to be synchronized by restoring.
*       FS_STATUS_RESTORED      - The journal file is valid but the
*                                 Journal has been flushed and the FAT
*                                 has been synchronized so no restore
*                                 is needed.
*   dword numindexblocks;       - Index blocks in the Journal
*   dword totalremapblocks;     - Total number of remap blocks
*   dword numblocksremapped;    - Total number of blocks remapped
*   dword journaledfreespace;   - Freesapce Signature when jounal was created
*   dword currentfreespace;     - Current volume Freespace
*   dword currentchecksum;      - Stored checksum of the index in the file
*   dword indexchecksum;        - Calculated checksum of the index blocks
*   dword journal_block_number; - Physical location of the Journal
*   dword filesize;             - Size of the Journal in blocks
*   BOOLEAN needsflush;         - FALSE
*   BOOLEAN out_of_date;        - TRUE if Freespace signature does not
*                                 match current freespace
*   BOOLEAN check_sum_fails;    - TRUE if stored checksum does not match
*                                 calculated checksum.
*   BOOLEAN restore_required;   - TRUE if restore must be done to restore
*                                 volume integrity
*   BOOLEAN restore_recommended;- TRUE if restore must be done to commit
*                                 Journalled transactions to the FAT
*
*
* Returns:
*   TRUE         - The info structure is valid
*   FALSE        - Failure, either Fasilsafe is not initialized or
*                  no Journal found
*
*   If FALSE is returned errno will be set to one of the following.
*
*   PEINVALIDDRIVEID        - Drive argument invalid
*   PENOINIT                - Failsafe not configured
*   PEFSNOJOURNAL           - No Journal file found
*
*   An ERTFS system error
*
*/


BOOLEAN fs_api_info(byte *drive_name,FSINFO *pinfo)
{
int driveno;
DDRIVE *pdr;
BOOLEAN ret_val;
FAILSAFECONTEXT *pfscntxt;

    /* Get the drive and make sure it is mounted   */
    driveno = check_drive_name_mount(drive_name, CS_CHARSET_NOT_UNICODE);
    if (driveno < 0)
        return(FALSE); /* check_drive set errno */
    rtfs_clear_errno();
    pdr = pc_drno_to_drive_struct(driveno);
    ret_val = FALSE;
	if (pdr)
	{
    	pfscntxt = (FAILSAFECONTEXT *) pdr->drive_state.failsafe_context;
        if (pfscntxt) /* We are on-line so give up the status */
            ret_val = fs_sessioninfo_internal(pfscntxt, pinfo);
        else
        { /* Not on-line see if check status of Failsafe file */
            pfscntxt = (FAILSAFECONTEXT *) pdr->du.user_failsafe_context;
            if (!pfscntxt)
            { /* They need to assign a Failsafe control block */
                rtfs_set_errno(PENOINIT, __FILE__, __LINE__);
                ret_val = FALSE;
            }
            else
                ret_val = fs_fileinfo_internal(pfscntxt, pinfo);
        }
	}
    release_drive_mount(driveno);
    return(ret_val);
}



#endif /* (INCLUDE_FAILSAFE_CODE) */
