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
#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */

/****************************************************************************
    PC_DISKFLUSH -  Flush the FAT and all files on a disk

 Description

    If an application may call this functions to force all files
    to be flushed and the fat to be flushed. After this call returns
    the disk image is synchronized with RTFSs internal view of the
    voulme.

Returns
    TRUE if the disk flushed else no

    errno is set to one of the following
    0               - No error
    PEINVALIDDRIVEID- Drive component is invalid
    An ERTFS system error
****************************************************************************/

BOOLEAN pc_diskflush(byte *path)                                        /*__apifn__*/
{
    int driveno;
    DDRIVE *pdrive;
    BOOLEAN ret_val;
    CHECK_MEM(BOOLEAN, 0)   /* Make sure memory is initted */

    rtfs_clear_errno();  /* pc_diskflush: clear error status */
    ret_val = FALSE;
    driveno = check_drive_name_mount(path, CS_CHARSET_NOT_UNICODE);
    /*  if error, errno was set by check_drive */
    if (driveno >= 0)
    { /* This is a top level call so override the AUTOFLUSH policy */
        /* Find the drive   */
        pdrive = pc_drno2dr(driveno);
        if (pdrive)
        {
            ret_val = _pc_diskflush(pdrive); /* Call Pro or ProPlus versions */
#if (INCLUDE_RTFS_PROPLUS)
			/* The user called this routine explicitly so tell the devie driver to flush the cache for this volume if it has one */
            pdrive->pmedia_info->device_ioctl(pdrive->pmedia_info->devhandle, (void *) pdrive, RTFS_IOCTL_FLUSHCACHE, 0 , 0);
#endif
        }
        if (!release_drive_mount_write(driveno))/* Release lock, unmount if aborted */
            ret_val = FALSE;
    }
    return(ret_val);
}

/* Internal flush, called by pc_diskflush() API call and internally by Failsafe */
BOOLEAN _pc_diskflush(DDRIVE *pdrive)                                        /*__apifn__*/
{
    BOOLEAN ret_val;

    ret_val = FALSE;

    if (pdrive)
    {
        if (pc_flush_all_fil(pdrive))
		{
#if (INCLUDE_EXFATORFAT64)
			if (ISEXFATORFAT64(pdrive))
				return pcexfat_flush(pdrive);
#endif
            /* Call pc_flush_fat_blocks() instead of fatop_flushfat() because fatop_flushfat() tests
               DRVPOL_NO_AUTOFLUSH and returns without flushing if it is true. .. pc_flush_fat_blocks() forces a
               flush regardless of the auto-flush policy */
            if (pc_flush_fat_blocks(pdrive))
                ret_val = TRUE;
		}
    }
    return(ret_val);
}
#endif /* Exclude from build if read only */
