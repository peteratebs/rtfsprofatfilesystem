/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* APIGREAD.C - Contains user api level source code.

    The following routines are included:

    pc_gread        - Read bytes from file or directory in DSTAT
*/

#include "rtfs.h"


/****************************************************************************
    PC_GREAD - Read bytes from file or directory in DSTAT

 Description
    Given a pointer to a DSTAT structure that has been set up by a call to
    pc_gfirst(), or pc_gnext() read original path. Return TRUE if found and update statobj for subsequent
    calls to pc_gnext.

 Returns
    Returns TRUE if a match was found otherwise FALSE.

    errno is set to one of the following
    0               - No error
    PEINVALIDPARMS - statobj argument is not valid
    PENOENT        - Not found, no match (normal termination of scan)
    An ERTFS system error
****************************************************************************/

BOOLEAN pc_gread(DSTAT *statobj, int blocks_to_read, byte *buffer, int *blocks_read)     /*__apifn__*/
{
    BOOLEAN ret_val;
    DDRIVE *pdrive;
    FINODE *pfi;
#if (INCLUDE_RTFS_PROPLUS)
    REGION_FRAGMENT *pf = 0;
#endif
    dword cluster_no;
    int bytes_per_cluster,blocks_per_cluster,blocks_left;
    CHECK_MEM(BOOLEAN, 0)   /* Make sure memory is initted */
    /* see if the drive is still mounted. */
    if (!buffer || !blocks_read || !statobj || !statobj->pmom)
    {
        rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__); /* pc_gnext: statobj is not valid */
        return(FALSE);
    }
    pdrive = check_drive_by_number(statobj->driveno, TRUE);
    if (!pdrive)
        return(FALSE);
    if (statobj->drive_opencounter != pdrive->drive_opencounter)
    {
        rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__); /* pc_gnext: statobj is not valid */
        release_drive_mount(statobj->driveno);/* Release lock, unmount if aborted */
        return(FALSE);
    }
    rtfs_clear_errno();  /* po_gread: clear error status */
    ret_val = TRUE;

#if (INCLUDE_EXFATORFAT64)
	if (ISEXFATORFAT64(pdrive))
	{
    	ret_val = pcexfat_gread(statobj, blocks_to_read, buffer, blocks_read);
    	release_drive_mount(statobj->driveno);/* Release lock, unmount if aborted */
    	return(ret_val);
	}
#endif
    *blocks_read = 0;
    cluster_no = 0;

    pfi = pc_scani(pdrive, statobj->my_block, statobj->my_index);
    if (pfi)
    {
        cluster_no = pc_finode_cluster(pdrive, pfi);
#if (INCLUDE_RTFS_PROPLUS)
        /* If it's an extended finode use the fragment list.. */
        if (pfi->e.x)
        {
            if (pfi->operating_flags & FIOP_LOAD_AS_NEEDED)
            {
                llword offset;
				offset.val32 = blocks_to_read << pdrive->drive_info.log2_bytespsec;
                if (!load_efinode_fragments_until(pfi, offset))
                    ret_val = FALSE;
                else
                    pf = pfi->e.x->pfirst_fragment;

            }
            else
                pf = pfi->e.x->pfirst_fragment;
            if (pf)
                cluster_no = pf->start_location;
        }
#endif
        pc_freei(pfi);
    }
    else
    {   /* No inode in the inode list. read the entry from disk */

    BLKBUFF *pblk;
        pblk = pc_read_blk(pdrive, statobj->my_block);
        if (!pblk)
        {
            ret_val = FALSE;
        }
        else
        {
        FINODE scratch_finode;
        DOSINODE *pi;
            pi = (DOSINODE *) pblk->data;
            pi += statobj->my_index;
            pc_dos2inode (&scratch_finode, pi);
            cluster_no = pc_finode_cluster(pdrive, &scratch_finode);
            pc_release_buf(pblk);
        }
    }

    blocks_per_cluster = pdrive->drive_info.secpalloc;
    bytes_per_cluster = blocks_per_cluster << pdrive->drive_info.log2_bytespsec;
    blocks_left = blocks_to_read;
    while (cluster_no && blocks_left)
    {
        dword blockno;
        int n_to_read;

        n_to_read = blocks_per_cluster;
        if (n_to_read > blocks_left)
            n_to_read = blocks_left;

        blockno = pc_cl2sector(pdrive,cluster_no);
        if (!blockno || !block_devio_xfer(pdrive,  blockno, buffer, n_to_read, TRUE))
        {
            ret_val = FALSE;
            break;
        }
        *blocks_read += n_to_read;
        blocks_left -= n_to_read;
        if (!blocks_left)
            break;
        buffer += bytes_per_cluster;
#if (INCLUDE_RTFS_PROPLUS)
        if (pf)
        {
            cluster_no += 1;
            if (cluster_no > pf->end_location)
            {
                pf = pf->pnext;
                if (pf)
                    cluster_no = pf->start_location;
                else
                    cluster_no = 0;
            }
        }
        else
#endif
        {
        dword next_cluster;
        int   end_of_chain;
            end_of_chain = 0;
            if (!fatop_get_frag(pdrive, 0, 0, cluster_no, &next_cluster, 1, &end_of_chain))
            {
                ret_val = FALSE;
                break;
            }
            if (end_of_chain)
                cluster_no = 0;
            else
                cluster_no = next_cluster;
        }
    }
    release_drive_mount(statobj->driveno);/* Release lock, unmount if aborted */
    return(ret_val);
}
