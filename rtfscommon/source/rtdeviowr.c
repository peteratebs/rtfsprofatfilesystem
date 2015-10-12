/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* rtdevio.c - Media check functions and device io wrapper functions
*
*
*/
#include "rtfs.h"
#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */


BOOLEAN release_drive_mount_write(int driveno)
{
DDRIVE *pdr;
BOOLEAN ret_val;

    ret_val = TRUE;
    pdr = pc_drno_to_drive_struct(driveno);
    if (pdr)
    {
        if (chk_mount_abort(pdr))
            pc_dskfree(driveno);
        else
        {
            /* If Failsafe autocommit is enabled, commit if needed and return status
               If Failsafe autocommit is not enabled returns success */
#if (INCLUDE_FAILSAFE_RUNTIME) /* Call failsafe autocommit */
			if (prtfs_cfg->pfailsafe)
			{
				ret_val = prtfs_cfg->pfailsafe->fs_failsafe_autocommit(pdr);
				if (chk_mount_abort(pdr)) /* If an IO error was detected during autocommit free the drive so we re-mount */
					pc_dskfree(driveno);
			}
#endif
        }
    }
    rtfs_release_media_and_buffers(driveno); /* release_drive_mount release */
    return(ret_val);
}


#endif /* Exclude from build if read only */
