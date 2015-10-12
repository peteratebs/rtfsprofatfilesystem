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
#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */

#if (INCLUDE_ASYNCRONOUS_API)


/****************************************************************************
    pc_diskio_async_diskflush -  Non blocking disk flush

Summary
    int pc_diskio_async_diskflush(driveid)
        byte *driveid - Name of a mounted volume "A:", "B:" etc.

 Description:

    Queue a disk flush in subsequant call to pc_acynch_continue()

    Note: pc_diskio_async_diskflush() may be called at anytime as long
    as the device is mounted. If no FAT buffers have been modified
    since the last succesful flush completion then no block writes
    are made and PC_ASYNC_COMPLETE is returned immediately. If any
    FAT blocks have been modified the first ones are selected and
    written.


 Returns
    PC_ASYNC_CONTINUE - One or more blocks were successfully flushed
                        but more calls are needed to flush other
                        modified FAT blocks.

    PC_ASYNC_COMPLETE - The operation was a success and no more calls
                        are needed because the FAT buffers have all been
                        flushed.

    PC_ASYNC_ERROR    - The operation failed, errno reflects the error
                        condition.

    errno is set to one of the following

    0               - No error
    PEEFIOILLEGALFD - To be fixed
    An ERTFS system error
****************************************************************************/

int pc_diskio_async_flush_start(byte *path)
{
    int driveno;
    DDRIVE *pdrive;
    int ret_val;
    CHECK_MEM(int, PC_ASYNC_ERROR)   /* Make sure memory is initted */

    rtfs_clear_errno();  /* pc_diskflush: clear error status */
    ret_val = PC_ASYNC_ERROR;
    driveno = check_drive_name_mount(path, CS_CHARSET_NOT_UNICODE);
    /*  if error, errno was set by check_drive */
    if (driveno >= 0)
    {
        /* Find the drive   */
        pdrive = pc_drno2dr(driveno);
        if (pdrive)
        {
            if (pdrive->drive_info.drive_operating_flags & DRVOP_FAT_IS_DIRTY)
            {
                pdrive->drive_info.drive_operating_flags |= DRVOP_ASYNC_FFLUSH;
                ret_val = PC_ASYNC_CONTINUE;
            }
            else
                ret_val = PC_ASYNC_COMPLETE;
        }
        release_drive_mount(driveno);
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

int _pc_efilio_async_flush_start(PC_FILE *pefile, BOOLEAN do_close);

int pc_efilio_async_flush_start(int fd)
{
    PC_FILE *pefile;
    int driveno;


    CHECK_MEM(int, PC_ASYNC_ERROR)   /* Make sure memory is initted */

    rtfs_clear_errno();  /* po_flush: clear error status */

    /* Get the FILE. must be open for write   */
    /* Get the file structure and semaphore lock the drive */
    pefile = pc_fd2file(fd, PO_WRONLY|PO_RDWR);
    if (!pefile)
        return(PC_ASYNC_ERROR); /* fd2file set errno */
    if (!_pc_check_efile_open_mode(pefile))
    {
        driveno = pefile->pobj->pdrive->driveno;
        release_drive_mount_write(driveno);
        return(PC_ASYNC_ERROR);
    }
    else
        return(_pc_efilio_async_flush_start(pefile, FALSE));
}

int _pc_efilio_async_flush_start(PC_FILE *pefile, BOOLEAN do_close)
{
    int ret_val, driveno;

    driveno = pefile->pobj->pdrive->driveno;
    if (!_pc_efilio_flush_file_buffer(pefile))
        ret_val = PC_ASYNC_ERROR;
    else
    {
	dword operating_flags;
        ret_val = PC_ASYNC_CONTINUE;
            operating_flags = _get_operating_flags(pefile);
            operating_flags |= FIOP_ASYNC_FLUSH;
            if (do_close)
                operating_flags |= FIOP_ASYNC_CLOSE;
            _set_asy_operating_flags(pefile,operating_flags, 1);
    }
    if (!release_drive_mount_write(driveno))/* Release lock, unmount if aborted */
        ret_val = PC_ASYNC_ERROR;
    return(ret_val);
}

/****************************************************************************
    pc_efilio_async_unlink_start - Start a non-blocking file delete

Summary
    int pc_efilio_async_unlink_start(byte *filename)
       byte *filename - The name of the file to delete. The file may be
       either a 32 bit native FAT file or a FAT64 metafile.

 Description

    Initiate deleting a file. The whole process involves loading the
    file's chain representation from the FAT into an internal form and
    then generating the disk write operations necessary to overwrite
    the on-disk cluster chains with zero and finally clearing the
    directory entry.

    After pc_efilio_async_unlink_start has been called you must complete
    the flush operation by calling pc_async_continue() until
    it no longer returns PC_ASYNC_CONTINUE. While the flush is in progress
    no API calls other than pc_async_continue() will succeed on
    the file.


 Returns
     a value >= 0 to be passed to pc_async_continue().
    -1  on an error

    errno is set to one of the following
    0               - No error
    PEEFIOILLEGALFD - To be fixed
    An ERTFS system error


    more about errno values:

    more on errno values

    If pc_async_continue() returns PC_ASYNC_ERROR while completing
    pc_efilio_async_unlink_start() errno will be set to one of these values.

    0               - No error
    PEEFIOILLEGALFD - To be fixed
    An ERTFS system error

****************************************************************************/

#if (INCLUDE_CS_UNICODE)
int pc_efilio_async_unlink_start_cs(byte *filename,int use_charset)
#else
int pc_efilio_async_unlink_start(byte *filename)
#endif
{
    PC_FILE *pefile;
    int fd, driveno;
    dword operating_flags;
    EFILEOPTIONS my_options;

    CHECK_MEM(int, -1)   /* Make sure memory is initted */

    rtfs_clear_errno();  /* po_flush: clear error status */
    driveno = check_drive_name_mount(filename, CS_CHARSET_ARGS);
    if (driveno < 0)   /*  errno was set by check_drive */
        return(-1);

    /* Do low level file open */
    rtfs_memset(&my_options, 0, sizeof(my_options));
    /* open asynchronous */
    my_options.allocation_policy = PCE_ASYNC_OPEN;
    fd = _pc_efilio_open(pc_drno2dr(driveno), filename, (word) (PO_WRONLY|PO_NOSHAREANY),0,&my_options,CS_CHARSET_NOT_UNICODE);
    if (fd >= 0)
    {
       /* Get the file structure */
        pefile = prtfs_cfg->mem_file_pool+fd;
    /* Queue the unlink operation and return fd,
        async_continue will transition from open to UNLINK */
        operating_flags = _get_operating_flags(pefile);
        _set_asy_operating_flags(pefile,operating_flags | FIOP_ASYNC_UNLINK, 1);
    }
    /* If the open failed with a sharing violation, map errno to PEACCESS which is the correct
       error for unlink */
    if (fd == -1)
    {
        if (get_errno()==PESHARE)
            rtfs_set_errno(PEACCES, __FILE__, __LINE__);
    }
    if (!release_drive_mount_write(driveno))/* Release lock, unmount if aborted */
        fd = -1;
    return(fd);
}



#endif /* (INCLUDE_ASYNCRONOUS_API) */
#endif /* Exclude from build if read only */
