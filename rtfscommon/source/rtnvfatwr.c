/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* RTNVFAT.C - Contains routines specific to the non-vfat implementation */

#include "rtfs.h"
#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */

#if (!INCLUDE_VFAT)


/***************************************************************************
    PC_INSERT_INODE - Insert a new inode into an existing directory inode.

Description
    Take mom , a fully defined DROBJ, and pobj, a DROBJ with a finode
    containing name, ext, etc, but not yet stitched into the inode buffer
    pool, and fill in pobj and its inode, write it to disk and make the inode
    visible in the inode buffer pool. (see also pc_mknode() )


Returns
    Returns TRUE if all went well, FALSE on a write error, disk full error or
    root directory full.

**************************************************************************/
/* Note: the parent directory is locked before this routine is called   */

BOOLEAN pc_insert_inode(DROBJ *pobj , DROBJ *pmom, byte attr, dword initcluster, byte *filename, byte *fileext, int use_charset)
{
    BLKBUFF *pbuff;
    DIRBLK *pd;
    DOSINODE *pi;
    int i;
    dword cluster,prev_end_cluster,entries_processed;
    DDRIVE *pdrive;
    DATESTR crdate;

    entries_processed = 0;

    RTFS_ARGSUSED_INT(use_charset);

    pc_init_inode( pobj->finode, filename, fileext,
                    attr, initcluster, /*size*/ 0L ,pc_getsysdate(&crdate) );

    /* Set up pobj      */
    pdrive = pobj->pdrive = pmom->pdrive;
    pobj->isroot = FALSE;
    pd = &pobj->blkinfo;

    /* Now get the start of the dir   */
    pd->my_block = pd->my_frstblock = pc_firstblock(pmom);


    if (!pd->my_block)
    {
        rtfs_set_errno(PEINTERNAL, __FILE__, __LINE__);   /* pc_insert_inode: Internal error, invalid block id */
        return (FALSE);
    }
    else
        pd->my_index = 0;

    /* Read the data   */
    pobj->pblkbuff = pbuff = pc_read_blk(pobj->pdrive, pobj->blkinfo.my_block);
    if (!pbuff)
        return(FALSE);
    while (pbuff)
    {
        i = pd->my_index = 0;
        pi = (DOSINODE *) pbuff->data;
        /* look for a slot   */
        while ( i < pobj->pdrive->drive_info.inopblock )
        {
            /* End of dir if name is 0  or 8 ff s  */
            if ( pc_check_dir_end(pi->fname) || (pi->fname[0] == PCDELETE) )
            {
                pd->my_index = (word)i;
                /* Update the DOS disk   */
                pc_ino2dos( pi, pobj->finode );
                /* Write the data   */
                if (pc_write_blk(pbuff))
                {
                    /* Mark the inode in the inode buffer   */
                    pc_marki(pobj->finode , pobj->pdrive , pd->my_block,
                            pd->my_index );
                    pc_release_buf(pbuff);
                    return(TRUE);
                }
                else
                {
                    pc_discard_buf(pbuff);
                    return(FALSE);
                }
            }
            i++;
            pi++;
        }
        /* Not in that block. Try again   */
        pc_release_buf(pbuff);
        entries_processed += pobj->pdrive->drive_info.inopblock;
        if (entries_processed >= RTFS_CFG_MAX_DIRENTS)
        {
            rtfs_set_errno(PEINVALIDDIR, __FILE__, __LINE__);
            return(FALSE);
        }

        /* Update the objects block pointer   */
        rtfs_clear_errno();  /* Clear errno to be safe */
        if (!pc_next_block(pobj))
        {
            if (get_errno())
                return(FALSE);
            else
                break;
        }
        pobj->pblkbuff = pbuff = pc_read_blk(pobj->pdrive, pobj->blkinfo.my_block);
    }

    cluster = pc_grow_dir(pdrive, pmom, &prev_end_cluster);
    if (!cluster)
        return (FALSE);
    /* Do not forget where the new item is   */
    pd->my_block = pc_cl2sector(pobj->pdrive , cluster);
    pd->my_index = 0;

    /* Zero out the cluster    */
    if (!pc_clzero( pobj->pdrive , cluster ) )
        goto clean_and_fail;
    /* Copy the item into the first block   */
    pbuff = pc_init_blk( pobj->pdrive , pd->my_block);
    if (!pbuff)
        goto clean_and_fail;
    pc_ino2dos (  (DOSINODE *) pbuff->data ,  pobj->finode ) ;
    /* Write it out   */
    if ( !pc_write_blk ( pbuff ) )
    {
        pc_discard_buf(pbuff);
        goto clean_and_fail;
    }

    /* We made a new slot. Mark the inode as belonging there   */
    pc_marki(pobj->finode , pobj->pdrive , pd->my_block, pd->my_index );
    pc_release_buf(pbuff);
    return (TRUE);
clean_and_fail:
    fatop_truncate_dir(pdrive, pmom, cluster, prev_end_cluster);
    return (FALSE);
}

/* Byte oriented */
BOOLEAN _illegal_lfn_char(byte ch)
{
    RTFS_ARGSUSED_INT((int) ch);
    return(FALSE);
}

BOOLEAN pc_delete_lfn_info(DROBJ *pobj)
{
    RTFS_ARGSUSED_PVOID((void *) pobj);
     return(TRUE);
}
void pc_zero_lfn_info(FINODE *pdir)
{
    RTFS_ARGSUSED_PVOID((void *) pdir);
}

#endif /* #if (!INCLUDE_VFAT) */
#endif /* Exclude from build if read only */
