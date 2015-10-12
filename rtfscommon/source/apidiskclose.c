/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* APIDISK.C - Contains drive oriented api function */

#include "rtfs.h"
/****************************************************************************
    PC_DISKCLOSE -  Unmount a drive

 Description

    If an application may call this functions to force an unconditional
    close, "unmount" of a disk. Buffers are not flush and all shared buffers
    are released.

    If you wish to flush the disk before closing it call pc_diskflush() first.

    BOOLEAN clear_init      -  If clear_init is TRUE, all buffers and configuration
                               values provided by pc_diskio_configure() are cleared and
                               the buffers provided to pc_diskio_configure() may be freed.
                               pc_diskio_configure() must be called again to use the
                               drive.

Returns
    TRUE if the drive id was valid.

    errno is set to one of the following
    0               - No error
    PEINVALIDDRIVEID- Drive component is invalid

****************************************************************************/

BOOLEAN pc_diskclose(byte *driveid, BOOLEAN clear_init)
{
    int driveno;
    DDRIVE *pdr;
    CHECK_MEM(BOOLEAN, 0)   /* Make sure memory is initted */

    /* Make sure its a valid drive number */
    rtfs_clear_errno();  /* pc_diskflush: clear error status */
    driveno = check_drive_name_mount(driveid, CS_CHARSET_NOT_UNICODE);
    if (driveno < 0)
        return(FALSE);
    pdr = pc_drno_to_drive_struct(driveno);
    pc_dskfree(driveno);    /* Release buffers */
    if (clear_init)
    {
        rtfs_memset(&pdr->du, 0, sizeof(pdr->du));
        /* Release configuration if the drive was configure through pc_ertfs_run or dynamically */
        pc_free_disk_configuration(driveno);
    }
    rtfs_release_media_and_buffers(driveno);
    return(TRUE);
}
