/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* RTDBLOCK.C - ERTFS-PRO and ProPlus Directory and scrath block buffering routines */

#include "rtfs.h"

#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */


BOOLEAN block_devio_write(BLKBUFF *pblk)
{
#if (INCLUDE_FAILSAFE_RUNTIME)
	if (prtfs_cfg->pfailsafe)
		return(prtfs_cfg->pfailsafe->block_devio_write(pblk));
#endif
    UPDATE_RUNTIME_STATS(pblk->pdrive, dir_buff_writes, 1)
    return(raw_devio_xfer(pblk->pdrive,pblk->blockno, pblk->data, (int) 1, FALSE, FALSE));
}


/***************************************************************************
    PC_WRITE_BLK - Flush a BLKBUFF to disk.
Description
    Use pdrive and blockno information in pblk to flush its data buffer
    to disk.

 Returns
    Returns TRUE if the write succeeded.
****************************************************************************/

/* Write */
BOOLEAN pc_write_blk(BLKBUFF *pblk)                                 /*__fn__*/
{
        if (!block_devio_write(pblk))
        {
            /* set errno to IO error unless devio set PEDEVICE */
            if (!get_errno())
                rtfs_set_errno(PEIOERRORWRITEBLOCK, __FILE__, __LINE__); /* pc_write_blk device write error */
            return(FALSE);
        }
        else
        {
            return(TRUE);
        }
}

/* Tomo */
/* Traverse a cluster chain and make sure that all blocks in the cluster
 chain are flushed from the buffer pool. This is required when deleting
 a directory since it is possible, although unlikely, that blocks used in
 the directory may be used in a file. This may cause the buffered
 block to be different from on-disk block.
 Called by pc_rmnode
*/
void pc_flush_chain_blk(DDRIVE *pdrive, dword cluster)
{
dword i;
dword blockno;
BLKBUFF *pblk;

    if ( cluster < 2 || cluster > pdrive->drive_info.maxfindex)
        return;
    while (cluster != FAT_EOF_RVAL) /* End of chain */
    {
        blockno = pc_cl2sector(pdrive, cluster);
        if (blockno)
        {
            for (i = 0; i < pdrive->drive_info.secpalloc; i++, blockno++)
            {
            /* Find it in the buffer pool */
                OS_CLAIM_FSCRITICAL()
                pblk = pc_find_blk(pdrive, blockno);
                if (pblk)
                    pblk->use_count += 1;
                OS_RELEASE_FSCRITICAL()
                if (pblk)
                {
                    pc_discard_buf(pblk);
                }
            }
        }
         /* Consult the fat for the next cluster   */
        cluster = fatop_next_cluster(pdrive, cluster);
        if (cluster == 0) /* clnext detected error */
            break;
     }
}
#endif /* Exclude from build if read only */
