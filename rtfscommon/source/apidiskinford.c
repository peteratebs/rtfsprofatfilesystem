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

BOOLEAN pc_diskio_info(byte *drive_name, DRIVE_INFO *pinfo, BOOLEAN extended)
{
int driveno;
DDRIVE *pdr;
BOOLEAN ret_val;

    CHECK_MEM(BOOLEAN, 0)  /* Make sure memory is initted */

    rtfs_clear_errno();  /* clear error status */

    if (!pinfo)
    {
        rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
        return(FALSE);
    }
    rtfs_memset(pinfo, 0, sizeof(*pinfo));
    /* assume failure to start   */
    ret_val = FALSE;
    /* Get the drive and make sure it is mounted   */
    driveno = check_drive_name_mount(drive_name, CS_CHARSET_NOT_UNICODE);
    /* if error check_drive errno was set by check_drive */
    if (driveno >= 0)
    {
        pdr = pc_drno2dr(driveno);
        if (!pdr)
            rtfs_set_errno(PEINVALIDDRIVEID, __FILE__, __LINE__); /* pc_free: no valid drive present */
        else
        {
            rtfs_memset((void *) pinfo, 0, sizeof(*pinfo));
            pinfo->drive_operating_policy       = pdr->du.drive_operating_policy;
            pinfo->cluster_size                 = pdr->drive_info.secpalloc;
            pinfo->sector_size                  = (dword) pdr->drive_info.bytespsector;

#if (INCLUDE_NAND_DRIVER) /* Implement pc_mkfs_dynamic() for dynamic mode */
            pinfo->erase_block_size =           pdr->pmedia_info->eraseblock_size_sectors;
#else
            pinfo->erase_block_size =           0;
#endif
            pinfo->total_clusters = pdr->drive_info.maxfindex-1;
            pinfo->free_clusters  = pdr->drive_info.known_free_clusters;
            pinfo->fat_entry_size = (dword) (pdr->drive_info.fasize * 4); /* Store in nibbles, change to bits */
            pinfo->drive_opencounter = pdr->drive_opencounter;
#if (INCLUDE_RTFS_FREEMANAGER)
            pinfo->free_manager_enabled         = (BOOLEAN) CHECK_FREEMG_OPEN(pdr);
            pinfo->region_buffers_total         = (dword)prtfs_cfg->cfg_NREGIONS;
            pinfo->region_buffers_free          = prtfs_cfg->region_buffers_free;
            pinfo->region_buffers_low_water     = prtfs_cfg->region_buffers_low_water;
#endif
#if (INCLUDE_EXFATORFAT64)
			if (ISEXFATORFAT64(pdr))
            	pinfo->is_exfat     				= TRUE;
#endif
            if (extended)
            {
#if (INCLUDE_RTFS_FREEMANAGER)
                pinfo->free_fragments = free_manager_count_frags(pdr);
#endif
            }
            ret_val = TRUE;
        }
        release_drive_mount(driveno);/* Release lock, unmount if aborted */
    }
    return(ret_val);
}

BOOLEAN pc_diskio_runtime_stats(byte *drive_name, DRIVE_RUNTIME_STATS *pstats, BOOLEAN clear)
{
BOOLEAN ret_val;
    CHECK_MEM(BOOLEAN, 0)  /* Make sure memory is initted */
    rtfs_clear_errno();  /* clear error status */
    rtfs_memset(pstats, 0, sizeof(*pstats));
    if (!pstats)
    {
        rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
        return(FALSE);
    }
    ret_val = TRUE;
#if (INCLUDE_DEBUG_RUNTIME_STATS)
{
int driveno;
DDRIVE *pdr;
    /* assume failure to start   */
    /* Get the drive and make sure it is mounted   */
    driveno = check_drive_name_mount(drive_name, CS_CHARSET_NOT_UNICODE);
    /* if error check_drive errno was set by check_drive */
    if (driveno >= 0)
    {
        pdr = pc_drno2dr(driveno);
        if (!pdr)
        {
            rtfs_set_errno(PEINVALIDDRIVEID, __FILE__, __LINE__); /* pc_free: no valid drive present */
            ret_val = FALSE;
        }
        else
        {
            copybuff((void *)pstats, (void *) &pdr->drive_rtstats, sizeof(*pstats));
            /* Now clear the statistics if requesst */
            if (clear)
            {
                rtfs_memset((void *) &pdr->drive_rtstats, 0, sizeof(pdr->drive_rtstats));
            }
        }
        release_drive_mount(driveno);/* Release lock, unmount if aborted */
    }
}
#else
    RTFS_ARGSUSED_INT((int) clear);
    RTFS_ARGSUSED_PVOID((void *) drive_name);
#endif
    return(ret_val);
}
