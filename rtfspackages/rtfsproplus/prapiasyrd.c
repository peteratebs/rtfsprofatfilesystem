/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* PRAPIASY.C - Contains user api level asynchronous IO source code.

  The following public API routines are provided by this module:


*/

#include "rtfs.h"

int _pc_efilio_async_flush_start(PC_FILE *pefile, BOOLEAN do_close);

#if (INCLUDE_ASYNCRONOUS_API)

/* pc_async_continue

   Function

   Process functions queued for asynchronous completion

   Summary


   int pc_async_continue(int driveno, int target_state, int steps)
      int driveno       - Drive number (A: == 0, B: == 1, etc
      int target_state  - Level of processing to complete (see below)
      int steps         - Number of iterations, 0 == finish

   Description

   This routine is called by application to complete one or more passes on
   asynchronous routines that have been queued for completion by the
   subroutines described in this section.

   pc_async_continue() may be used as a background asynchronous process manager
   when called by a background thread on a periodic basis or when called from a
   foreground thread periodically to process outstanding asynchronous operations.


   Possible target states:

   DRV_ASYNC_DONE_MOUNT         - Process until outstanding mount operation completes
   DRV_ASYNC_DONE_FILES         - Process until all outstanding asynchronous file operations complete
   DRV_ASYNC_DONE_FATFLUSH      - Process until all outstanding asynchronous file operations complete, and FATS are flushed.
   DRV_ASYNC_DONE_JOURNALFLUSH  - Process until all outstanding asynchronous file operations complete, FATS are flushed and the Journal file is flushed.
   DRV_ASYNC_DONE_RESTORE       - Process until all outstanding asynchronous file operations complete, FATS are flushed, the Journal file is flushed and the FAT volume is synchronized with the journal.
   DRV_ASYNC_IDLE               - Process until all outstanding asynchronous operations complete

   Example:

    Execute one iteration per 100 Miliseconds
    for (;;) {
        pc_async_continue(0, DRV_ASYNC_IDLE, 1);
        Sleep(100);
    }
    Execute as many iterations as necessary to complete all outstanding file operations
    pc_async_continue(0, DRV_ASYNC_DONE_FILES, 0);

    Execute as many iterations as necessary to complete all outstanding file operations
       and Flush the FAT buffers
    pc_async_continue(0, DRV_ASYNC_DONE_FATFLUSH, 0);

    Execute as many iterations as necessary to complete all outstanding file operations
       Flush the FAT buffers and FLUSH the Journal file
    pc_async_continue(0, DRV_ASYNC_DONE_JOURNALFLUSH, 0);

    Execute as many iterations as necessary to complete all outstanding file operations
       Flush the FAT buffers, FLUSH the Journal file, and syncronize the FAT volume
    pc_async_continue(0, DRV_ASYNC_DONE_RESTORE, 0);


   Returns
      PC_ASYNC_COMPLETE   - Target state succesfully reached
      PC_ASYNC_CONTINUE   - Target state not reached in step_count iterations, continue calling pc_async_continue()
      PC_ASYNC_ERROR      - An error Target state not reached in step_count iterations, continue calling pc_async_continue()

      Errnos DO this
*/

int pc_async_continue(int driveno, int target_state, int steps)
{
    DDRIVE *pdrive;
    int ret_val;

    CHECK_MEM(int, PC_ASYNC_ERROR)   /* Make sure memory is initted */
    rtfs_clear_errno();  /* clear error status */
    pdrive = check_drive_by_number(driveno, FALSE);
    if (pdrive)
    {
        if (pdrive->drive_state.drive_async_state == DRV_ASYNC_MOUNT)
        {
            ret_val = _pc_async_step(pdrive, DRV_ASYNC_IDLE, steps);
            rtfs_release_media_and_buffers(driveno);
        }
        else
        {
            ret_val = _pc_async_step(pdrive, target_state, steps);
            if (!release_drive_mount_write(driveno))/* Release lock, unmount if aborted */
                ret_val = PC_ASYNC_ERROR;
        }
        return(ret_val);
    }
    else
    {
        return(PC_ASYNC_ERROR);
    }
}

/****************************************************************************
    pc_diskio_async_mount_start - Start a non-blocking disk mount

Summary
    int pc_diskio_async_mount_start(driveid)
        byte *driveid - Name of the  volume "A:", "B:" etc.

 Description:

    Queue a disk mount in subsequant call to pc_acynch_continue()

    The asynchronous disk mount separates the three initial reads
    into one call and the subsequant FAT scan operation into
    multiple calls to pc_async_continue().

    With this method the system can continue to perform useful work
    while the disk is beign mounted.


    Notes:
     . If any other API call is attempted on the drive before the
     asynchronous mount is completed the call will fail and errno will
     be set to PEEINPROGRESS.
     . By default ERTFS will automatically (synchronously) mount a volume
     the first time it is accessed by the API. This feature provides
     transparent mounting of media the first time it is accessed and
     also transparent remounting of changed removable media, but it also
     eliminates API determinism since many API's could ultimately
     call the internal synchronous mount procedure.

     To eliminate this source of non-determinism the auto-mount policy may be
     eliminated (see pc_diskio_configure()


 Returns
    a value >= 0 to be passed to pc_diskio_async_continue().
    -1  on an error

    errno is set to one of the following
    0               - No error
    PEEFIOILLEGALFD - To be fixed
    An ERTFS system error

    more about errno values:
     If pc_diskio_async_continue() returns PC_ASYNC_ERROR
     while trying to complete the call to pc_diskio_async_mount_start()
     errno will be set to one of these values:

     PEEFIOILLEGALFD - To be fixed

****************************************************************************/

int pc_diskio_async_mount_start(byte *diskid)
{
    DDRIVE *pdr;
    int ret_val;

    CHECK_MEM(int, -1)   /* Make sure memory is initted */

    rtfs_clear_errno();  /* clear error status */
    ret_val = -1;
    /* Get the drive and set mount mode to asynchronous */
   	pdr = check_drive_by_name( diskid, CS_CHARSET_NOT_UNICODE);
    /* Check drive number   */
    if (pdr)
    {
        if (chk_mount_valid(pdr))   /* Already mounted, return success */
            ret_val = pdr->driveno;
        else
        {
            if (pc_i_dskopen(pdr->driveno,TRUE))
            {   /* Change 10/23/2007 - Don't set   DRV_ASYNC_MOUNT on 12 bit fat, was mounted synchronously */
                if (pdr->drive_info.fasize == 3)
                    pdr->drive_state.drive_async_state = DRV_ASYNC_IDLE;
                else
                    pdr->drive_state.drive_async_state = DRV_ASYNC_MOUNT;
                ret_val = pdr->driveno;
            }  /* else ret_val = -1 */
        }
        rtfs_release_media_and_buffers(pdr->driveno); /* check_drive_name_mount release */
    }
    return(ret_val);
}

/****************************************************************************
    pc_efilio_async_flush_start - Start a non-blocking file flush

Summary
    int pc_efilio_async_flush_start(int fd)
       int fd - A file descriptor that was returned from a succesful call
        to pc_efilio_open.

 Description

    Initiate flushing a modified file's cluster map to the file allocation
    table. After the file has been flushed it may continue to be used
    or it may be closed. If the file is closeed, the operation will proceed
    quickly because the flush operation has already been completed.

    After pc_efilio_async_flush_start has been called you must complete
    the flush operation by calling pc_async_continue() until
    it no longer returns PC_ASYNC_CONTINUE. While the flush is in progress
    no API calls other than pc_async_continue() will succeed.


 Returns
    PC_ASYNC_CONTINUE - No failure has occured. Continue calling
                        pc_async_continue().

    PC_ASYNC_ERROR    - Could not initiate the flush operation
                        because of some sort of parameter problem
                        consult errno for the cause. A call to
                        pc_async_continue() will fail.



    errno is set to one of the following
    0               - No error
    PEEFIOILLEGALFD - To be fixed
    An ERTFS system error

    more on errno values

    If pc_async_continue() returns PC_ASYNC_ERROR while completing
    pc_efilio_async_flush_start() errno will be set to one of these values.

    0               - No error
    PEEFIOILLEGALFD - To be fixed
    An ERTFS system error

****************************************************************************/


/****************************************************************************
    pc_efilio_async_close_start - Start a non-blocking file close

Summary
    int pc_efilio_async_close_start(int fd)
       int fd - A file descriptor that was returned from a succesful call
        to pc_efilio_open.

 Description

    Initiate flushing a modified file's cluster map and directory
    entry. After the file has been flushed close the file.

    After pc_efilio_async_close_start has been called you must complete
    the flush operation by calling pc_async_continue() until
    it no longer returns PC_ASYNC_CONTINUE. While the flush is in progress
    no API calls other than pc_async_continue() will succeed.


 Returns
    PC_ASYNC_CONTINUE - No failure has occured. Continue calling
                        pc_async_continue().

    PC_ASYNC_ERROR    - Could not initiate the flush/close operation
                        because of some sort of parameter problem
                        consult errno for the cause. A call to
                        pc_async_continue() will fail.



    errno is set to one of the following
    0               - No error
    PEEFIOILLEGALFD - To be fixed
    An ERTFS system error

    more on errno values

    If pc_async_continue() returns PC_ASYNC_ERROR while completing
    pc_efilio_async_close_start() errno will be set to one of these values.

    0               - No error
    PEEFIOILLEGALFD - To be fixed
    An ERTFS system error

****************************************************************************/

int pc_efilio_async_close_start(int fd)
{
    PC_FILE *pefile;
    BOOLEAN is_aborted;
    CHECK_MEM(int, PC_ASYNC_ERROR)  /* Make sure memory is initted */

    /* Get the file structure. If found semaphore lock the drive */
    is_aborted = FALSE;
    pefile = pc_efilio_start_close(fd, &is_aborted);

    if (!pefile)
    {
        if (is_aborted) /* TRUE (OK) if aborted, else an error */
            return(PC_ASYNC_COMPLETE);
        else
            return(PC_ASYNC_ERROR);
    }
    else
    {
#if (RTFS_CFG_READONLY) /* If read only file system, just close the file, do not flush */
{
    int driveno;
        driveno = pefile->pobj->pdrive->driveno;
        _pc_efilio_close(pefile);
        release_drive_mount(driveno);
        return(PC_ASYNC_COMPLETE);
}
#else
        return(_pc_efilio_async_flush_start(pefile, TRUE));
#endif
    }
}


BOOLEAN _pc_check_if_async(PC_FILE *pefile)
{
    if (_get_operating_flags(pefile) & FIOP_ASYNC_ALL_OPS)
        return(TRUE);
    else
        return(FALSE);
}

dword _get_operating_flags(PC_FILE *pefile)
{
    return(pefile->pobj->finode->operating_flags);
}
/* Set file operating flag and, if necessary,  add or remove file from async
   processing list */
void _set_asy_operating_flags(PC_FILE *pefile, dword new_operating_flags, int success)
{
PC_FILE *pscan;
dword current_operating_flags;
DDRIVE *pdrive;

    pdrive = pefile->pobj->pdrive;
    current_operating_flags = pefile->pobj->finode->operating_flags;
    if (current_operating_flags & FIOP_ASYNC_ALL_OPS)
    {
        if ((new_operating_flags & FIOP_ASYNC_ALL_OPS) == 0)
        { /* Done with async op , remove from outstanding async file list */
            /* If this is the currently processing item advance */
            pscan = pdrive->drive_state.asy_file_pfirst;
            if (pscan == pefile)
                pdrive->drive_state.asy_file_pfirst = pefile->fc.plus.asy_file_pnext;
            else
            {
                while (pscan)
                {
                    if (pscan->fc.plus.asy_file_pnext == pefile)
                    {
                        pscan->fc.plus.asy_file_pnext = pefile->fc.plus.asy_file_pnext;
                        break;
                    }
                    pscan = pscan->fc.plus.asy_file_pnext;
                }
            }
            /* Alert the user that the async process is completed */
            rtfs_app_callback(RTFS_CBA_ASYNC_FILE_COMPLETE, pefile->my_fd, success, 0, 0);
        }
    }
    else if (new_operating_flags & FIOP_ASYNC_ALL_OPS)
    { /* Starting with async op , add to outstanding async file list */
         pefile->fc.plus.asy_file_pnext = 0;
         pscan = pdrive->drive_state.asy_file_pfirst;
         if (!pscan)
         {
            pdrive->drive_state.asy_file_pfirst = pefile;
         }
         else
         {
            while (pscan)
            {
                /* Debug mode, verify not already on the list */
                ERTFS_ASSERT(pscan != pefile)
                if (pscan->fc.plus.asy_file_pnext)
                    pscan = pscan->fc.plus.asy_file_pnext;
                else
                {
                    pscan->fc.plus.asy_file_pnext = pefile;
                    break;
                }
            }
         }
    }
    pefile->pobj->finode->operating_flags = new_operating_flags;
}

#endif /* (INCLUDE_ASYNCRONOUS_API) */
