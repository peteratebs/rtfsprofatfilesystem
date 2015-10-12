/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* RTCOMMONGLUE.C - ProPlus Pro miscelaneous common functions */

#include "rtfs.h"

#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */


dword pc_alloc_dir(DDRIVE *pdrive, DROBJ *pmom)  /* __fatfn__ */
{
dword clbase;
        if ( pdrive->drive_info.fasize != 8 && pc_isroot(pmom))
            clbase = 0;
        else
        {
            if (pc_isroot(pmom))
                clbase = pc_sec2cluster(pmom->pdrive, pmom->pdrive->drive_info.rootblock);
            else
                clbase = pc_finode_cluster(pmom->pdrive,pmom->finode);
        }
        return(fatop_alloc_dir(pdrive, clbase));
}

dword pc_grow_dir(DDRIVE *pdrive, DROBJ *pobj, dword *previous_end) /* __fatfn__ */
{
dword tmpcl, cluster;
    if ( pdrive->drive_info.fasize != 8 && pc_isroot(pobj))
    {
        rtfs_set_errno(PENOSPC, __FILE__, __LINE__);
        cluster = 0;
    }
    else
    {
        if ( pc_isroot(pobj))
           tmpcl = pc_sec2cluster(pdrive, pdrive->drive_info.rootblock);
        else
            tmpcl = pc_finode_cluster(pdrive,pobj->finode);
        cluster = fatop_clgrow(pdrive, tmpcl, previous_end);
    }
    return(cluster);
}

BOOLEAN pc_update_inode(DROBJ *pobj, BOOLEAN set_archive, int set_date_mask) /*__fn__*/
{
    if ( pc_isroot(pobj))
		return(FALSE);
	else
    	return(pc_update_by_finode(pobj->finode, pobj->finode->my_index, set_archive, set_date_mask));
}



/*  pc_flush_file_buffer(FINODE *pfinode) -
* If the finode structure that the file points to contains a file buffer
* that has been modified, but not written to disk, write the data to disk.
*/
BOOLEAN pc_flush_file_buffer(FINODE *pfinode)
{
BLKBUFF *pfile_buffer;


    pfile_buffer = pfinode->pfile_buffer;
    /* write it if it changed */
    if (pfile_buffer && pc_check_file_buffer_dirty(pfinode))
    {
    dword save_drive_filio,data_size_sectors;
        save_drive_filio = pfile_buffer->pdrive->drive_flags & DRIVE_FLAGS_FILEIO;
        pfile_buffer->pdrive->drive_flags |= DRIVE_FLAGS_FILEIO;

        data_size_sectors = pfile_buffer->data_size_bytes>>pfile_buffer->pdrive->drive_info.log2_bytespsec;
        ERTFS_ASSERT(data_size_sectors != 0)
        if (!data_size_sectors)
        {
            rtfs_set_errno(PEINTERNAL, __FILE__, __LINE__); /* pc_flush_file_buffer: Internal error*/
            return(FALSE);
        }
        if (!raw_devio_xfer(pfile_buffer->pdrive,pfile_buffer->blockno,
                pfile_buffer->data, data_size_sectors, FALSE, FALSE) )
        {
            if (!save_drive_filio)
                pfile_buffer->pdrive->drive_flags &= ~DRIVE_FLAGS_FILEIO;
            /* set errno to IO error unless devio set PEDEVICE */
            if (!get_errno())
                rtfs_set_errno(PEIOERRORWRITEBLOCK, __FILE__, __LINE__); /* device write error */
            return(FALSE);
        }
        UPDATE_RUNTIME_STATS(pfile_buffer->pdrive, file_buff_writes, 1)
        if (!save_drive_filio)
             pfile_buffer->pdrive->drive_flags &= ~DRIVE_FLAGS_FILEIO;
        /* Clear dirty condition */
        pc_set_file_buffer_dirty(pfinode, FALSE);
    }
    return(TRUE);
}
/* Used only by check disk */
FINODE *pc_file2_finode(PC_FILE * pfile)
{
    return(pfile->pobj->finode);
}


#if (!INCLUDE_FAT32)

BOOLEAN pc_mkfs32(int driveno, FMTPARMS *pfmt, BOOLEAN use_raw)                         /*__fn__*/
{
    RTFS_ARGSUSED_INT(driveno);
    RTFS_ARGSUSED_PVOID((void *) pfmt);
    RTFS_ARGSUSED_INT((int) use_raw);
    return(FALSE);
}
BOOLEAN fat_flushinfo(DDRIVE *pdr)
{
    RTFS_ARGSUSED_PVOID((void *) pdr);
    return(TRUE);
}
#endif /* !(INCLUDE_FAT32) */

#if (!INCLUDE_FAT16)
BOOLEAN pc_mkfs16(int driveno, FMTPARMS *pfmt, BOOLEAN use_raw, int nibs_per_entry)
{
    RTFS_ARGSUSED_INT(driveno);
    RTFS_ARGSUSED_INT(nibs_per_entry);
    RTFS_ARGSUSED_PVOID((void *) pfmt);
    RTFS_ARGSUSED_INT((int) use_raw);
    return(FALSE);
}
#endif /* #if ((!INCLUDE_FAT16) */
#endif /* Exclude from build if read only */
