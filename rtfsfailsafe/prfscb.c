/*
* EBS - RTFS (Real Time File Manager)
* FAILSSAFEV3 - Need reworking
* Copyright EBS Inc. 1987-2005
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* PRFSCB.C - Contains user callback ailsafe functions.
   The following routines are included:

   dword fs_api_cb_journal_size(dword drive_size_blocks)
   int fs_api_cb_restore(int driveno)
   BOOLEAN fs_api_cb_error_restore_continue(int driveno)
   BOOLEAN fs_api_cb_enable(int driveno)
   int fs_api_cb_flush(int driveno)
*/

#include "rtfs.h"

#if (INCLUDE_FAILSAFE_CODE)

/* Return TRUE if the DRVPOL_DISABLE_AUTOFAILSAFE policy is set for driveno */
static BOOLEAN fs_auto_disabled(int driveno)
{
 DDRIVE *pdrive;

    pdrive = pc_drno_to_drive_struct(driveno);
	ERTFS_ASSERT(pdrive)
    if (pdrive && (pdrive->du.drive_operating_policy & DRVPOL_DISABLE_AUTOFAILSAFE))
		return(TRUE);
	else
		return(FALSE);
}
/* fs_api_cb_journal_fixed - Select Fixed Journal placement and size.

    int fs_api_cb_journal_fixed(DDRIVE *pdrive, dword *raw_start_sector, dword *file_size_sectors)

    This routine is called by Failsafe when it is about to create or re-open the Journal.

    It may be used to place the Journal in a fixed reserved location on the disk.

    To place the Journal in a fixed location:
        set *raw_start_sector to the raw sector offset on the drive
        set *file_size_sectors to the size of the reserved area.
        return 1

    To use the internal default journal file placement algorithm
        return 0

	fs_api_cb_journal_fixed() calls rtfs_failsafe_callback(RTFS_CB_FS_RETRIEVE_FIXED_JOURNAL_LOCATION,..); to retrieve values from the application

 Returns:
   1 to specify a fixed placement journal
   0 to use the internal default journal file placement algorithm

*/
#if (INCLUDE_DEBUG_TEST_CODE)
dword failsafe_fixed_start = 0;
dword failsafe_fixed_size = 0;
#endif
int fs_api_cb_journal_fixed(DDRIVE *pdrive, dword *raw_start_sector, dword *file_size_sectors)
{
dword start_sector, size_sectors;

    RTFS_ARGSUSED_PVOID((void *) pdrive);
#if (INCLUDE_DEBUG_TEST_CODE)
    if (failsafe_fixed_start && failsafe_fixed_size)
    {
        *raw_start_sector = failsafe_fixed_start;
        *file_size_sectors = failsafe_fixed_size;
        return(1);
    }
#endif
	if (rtfs_failsafe_callback(RTFS_CB_FS_RETRIEVE_FIXED_JOURNAL_LOCATION, pdrive->driveno, 0, (void *) &start_sector, (void *) &size_sectors))
    {
        *raw_start_sector = start_sector;
        *file_size_sectors = size_sectors;
        return(1);
    }
    else
        return(0);
}


/* fs_api_cb_check_fail_on_journal_resize - Return true if journal file resizing is allowed

    int  fs_api_cb_check_fail_on_journal_resize(int driveno)

    This routine is called by Failsafe when it is about to resize the journal file
    because the drive does not have enough space to hold the journal and
    new content.

    If the application does not wish to allow reducing the size of the journal file below the size that was originally assigned it
    can return 1 to not allow resizing of the file.


 Returns:
    failsafe_min_journal_size

*/

BOOLEAN  fs_api_cb_check_fail_on_journal_resize(int driveno)
{
	if (fs_auto_disabled(driveno)) /* default to allow resizing for test environment */
		return(FALSE);
	else
		return(rtfs_failsafe_callback(RTFS_CB_FS_FAIL_ON_JOURNAL_RESIZE, driveno, 0, 0, 0));
}


/* fs_api_cb_disable_on_full - Determine full disk strategy

    BOOLEAN fs_api_cb_disable_on_full(DDRIVE *pdrive)

    This routine is called by Failsafe when it is can not resize or create
    a reduced journal file because the drive does not have enough space to hold
    the the minimum journal file that was specified by fs_api_cb_min_journal_size().

    It returns TRUE if Rtfs should disable Failsafe and continue operating without Journaling.
    If TRUE is returned all clusters on the drive will be available but Journaling will
    be disabled.

	fs_api_cb_disable_on_full() calls rtfs_failsafe_callback(RTFS_CB_FS_FAIL_ON_JOURNAL_FULL,..); to retrieve instruction from the application
	as to whether it should force an error, and then inverts the return value for internal processing

 Returns:
        TRUE to disable Failsafe and proceed
        FALSE to Fail with errno set to PENOSPC
*/


BOOLEAN fs_api_cb_disable_on_full(DDRIVE *pdrive)
{
    if (pdrive->du.drive_operating_policy & DRVPOL_DISABLE_AUTOFAILSAFE)	/* default to fail on journal full for test environment */
		return(FALSE);
	if (rtfs_failsafe_callback(RTFS_CB_FS_FAIL_ON_JOURNAL_FULL, pdrive->driveno, 0, 0, 0))
		return(FALSE);
	else
		return(TRUE);
}


/* fs_api_cb_journal_size - Select Journal File size.

    dword fs_api_cb_journal_size(dword drive_size_blocks)

    This routine is called by Failsafe when it is about to create
    a Journal file for a volume mount. Failsafe passes the volume
    size in blocks, and the routine returns a recommended Journal
    file size in blocks.

    The default implementation assigns the journal file size to be 1 / 128th the volume size

    These are the default values

    Volume Size                 Journal File Size

    < 2      Megabytes        64  Kilobytes
    2-5      Megabytes        128 Kilobytes
    5-10     Megabytes        256 Kilobytes
    10-64    Megabytes        512 Kilobytes
    64-512   Megabytes        1   Megabyte
    .5 to 2  Gigabytes        2   Megabytes
    2 to 100 Gigabyte         20  Megabytes
    >100 Gigabytes            40  Megabytes


    Depending on how Failsafe will be used these defaults may be much
    larger than the space needed by the application. They may be
    modified after experimenting and measuring the amount of sapce
    actually being used.

 Returns:
   The size of the journal to create

*/
#if (INCLUDE_DEBUG_TEST_CODE)
extern dword fs_test_journal_size; /* If non zero, use instead of table */
#endif

dword fs_api_cb_journal_size(DDRIVE *pdrive)
{
dword size_in_sectors, returned_file_size_sectors;

#if (INCLUDE_DEBUG_TEST_CODE)
	/* Used by test code to create artificially small journal files */
	if (fs_test_journal_size)
		return(fs_test_journal_size);
#endif
	size_in_sectors = pdrive->drive_info.numsecs;
	/* Ask the callback to fill in returned_file_size_sectors */
	if (!rtfs_failsafe_callback(RTFS_CB_FS_RETRIEVE_JOURNAL_SIZE, pdrive->driveno, 0, (void *) &size_in_sectors, (void *) &returned_file_size_sectors))
	{	/* No use a default of drive sisize divided by 128. */
        returned_file_size_sectors = size_in_sectors/128;
	}
	/* We need at least four sectors */
	if (returned_file_size_sectors < 4)
		returned_file_size_sectors = 4;
	/* clip it at 0x100000 sectors, 4 gig with 4096 sector size */
	if (returned_file_size_sectors > 0x100000)
		returned_file_size_sectors = 0x100000;
   	return(returned_file_size_sectors);
}

/* fs_api_cb_restore - Determine auto restore strategy

    int fs_api_cb_restore(int driveno)

    This routine is called by Failsafe when it is about to mount
    a volume and it detects that the volume needs to be restored
    from the journal file.

    This routine may return one of 3 values.

      FS_CB_RESTORE  - Tells Failsafe to proceded and restore the volume
      FS_CB_CONTINUE - Tells Failsafe to proceded and not restore the volume.
      FS_CB_ABORT    - Tells Failsafe to terminate the mount, causing the
                       API call to fail with errno set to PEFSRESTORENEEDED.

	fs_api_cb_disable_on_full() calls rtfs_failsafe_callback(RTFS_CB_FS_RETRIEVE_RESTORE_STRATEGY,..); to retrieve instruction from the application.

    The default fallthrough return value value from rtfs_failsafe_callback() is FS_CB_RESTORE (zero), This instructs Failsafe to automatically restore
    the volume.

Returns:
      FS_CB_RESTORE  - Tells Failsafe to proceded and restore the volume
      FS_CB_CONTINUE - Tells Failsafe to proceded and not restore the volume.
      FS_CB_ABORT    - Tells Failsafe to terminate the mount, causing the
                       API call to fail with errno set to PEFSRESTORENEEDED.

*/

int fs_api_cb_restore(int driveno)
{
	if (fs_auto_disabled(driveno)) /* default to mount without restoring for test environment, restore is done through the Failsafe API */
		return(FS_CB_CONTINUE);
	else
		return(rtfs_failsafe_callback(RTFS_CB_FS_RETRIEVE_RESTORE_STRATEGY, driveno, 0, 0, 0));
}


/* fs_api_cb_error_restore_continue - Determine strategy when
   auto-restore fails.

    BOOLEAN fs_api_cb_error_restore_continue(int driveno)

    Called by Rtfs when the volume mount procedure detects a that a restore from journal file procedure is required but it also detects that
    the volume was modified since the Journal file was created.

    fs_api_cb_error_restore_continue calls rtfs_failsafe_callback(RTFS_CB_FS_FAIL_ON_JOURNAL_CHANGED,..); to retrieve instruction from the application
	as to whether it should force an error, and then inverts the return value for internal processing

    If this routine returns FALSE the error will not be ignored, the
    restore will be aborted, the mount will fail causing the API call
    to fail with errno set to PEFSRESTOREERROR.

    The default fallbak behavior of rtfs_failsafe_callback(RTFS_CB_FS_FAIL_ON_JOURNAL_CHANGED,..); is zero, do not restor and continue with
	the mount.

Returns:
    TRUE - Ignore the error and abort the restore but proceed to mount the volume.
    FALSE - Abort the restore and the mount. Instruct the API call
            to fail and set errno set to PEFSRESTOREERROR.
*/

/*
      By default we return TRUE, to force the auto-restore to fail and
      instruct the mount to continue
*/
BOOLEAN fs_api_cb_error_restore_continue(int driveno)
{
	if (fs_auto_disabled(driveno)) /* default to mount after a botched auto restore */
	{
		ERTFS_ASSERT(rtfs_debug_zero())		/* This should be imossible */
		return(TRUE);
	}
	else if (rtfs_failsafe_callback(RTFS_CB_FS_FAIL_ON_JOURNAL_CHANGED, driveno, 0, 0, 0))
		return(FALSE);
	else
		return(TRUE);
}

/* fs_api_cb_enable - User callback to instruct RTFS whether to automatically
                       enable failsafe Journaling when the volume is mounted.

   BOOLEAN fs_api_cb_enable(driveno)
        int driveno

    This callback routine is called by Failsafe when a volume is mounted as
    the result of power up or a media insertion event.
    The routine may instruct RTFS to automatically enable journalling
    by returning TRUE.

	Calls rtfs_failsafe_callback(RTFS_CB_FS_CHECK_JOURNAL_BEGIN_NOT,..) for instructions


Returns:
    TRUE - Instructs RTFS to automatically enable journalling
    FALSE - Instructs RTFS to leave journalling disabled.
*/

BOOLEAN fs_api_cb_enable(int driveno)
{
	if (fs_auto_disabled(driveno)) /* default to not auto enabling journaling */
		return(FALSE);
	if (!rtfs_failsafe_callback(RTFS_CB_FS_CHECK_JOURNAL_BEGIN_NOT, driveno, 0, 0, 0))
		return(TRUE);
	else
		return(FALSE);
}

/* fs_api_cb_flush - Determine API exit point Failsafe Flush strategy

    int fs_api_cb_flush(int driveno)

    This callback routine is called by Failsafe when an API call has changed the volume and is about to return to the user.

    returns:

    This routine must return one of the folowing 3 values.

      FS_CB_FLUSH    - Tell Failsafe to flush the journal file.
                       When this value is returned the API call will not return
                       until the Journal file is flushed.



      FS_CB_SYNC     - Tell Failsafe to flush the journal file and
                       synchronize the FAT volume.


      FS_CB_CONTINUE - Tell the API call to return without any flushing

	fs_api_cb_flush() calls rtfs_failsafe_callback(RTFS_CB_FS_RETRIEVE_FLUSH_STRATEGY..) to retrieve instructions.


*/

/* Enable Automatic Journal file policy at the completion of API calls
   By default we return FS_CB_CONTINUE, requiring the application to
   call fs_api_commit() to flush the journal */

int fs_api_cb_flush(int driveno)
{
	if (fs_auto_disabled(driveno)) /* default to not auto flushing for test environment */
		return(FS_CB_CONTINUE);
	return (rtfs_failsafe_callback(RTFS_CB_FS_RETRIEVE_FLUSH_STRATEGY, driveno, 0, 0, 0));
}


#endif /* (INCLUDE_FAILSAFE_CODE)*/
