/*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* PRAPILINEXT.C - Contains user api level linear file extract function
    pc_efilio_extract()  -  Extract data from one linear to another linear file
*/
#include "rtfs.h"
#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */


#if (INCLUDE_EXFATORFAT64)
/* This is old FAT64 code, chage it to support exfat */
dword pc_byte2clmod64(DDRIVE *pdr, dword nbytes_hi, dword nbytes_lo);
#endif
static void pc_linext_fraglist_coalesce(REGION_FRAGMENT *pfstart);
static REGION_FRAGMENT *pc_linext_concatenate_fragments(PC_FILE *pefile);
static REGION_FRAGMENT *pc_linext_append_dup_fragments(DDRIVE *pdr, REGION_FRAGMENT *pappend_to, REGION_FRAGMENT *p_to_dup);
static void pc_linext_rebuild_file(PC_FILE *pefile, REGION_FRAGMENT *pnew_chain);
static void pc_linext_rebuild_finode(FINODE *pefinode, REGION_FRAGMENT *pnew_chain);
static void pc_linext_queue_delete(PC_FILE *pefile, REGION_FRAGMENT *pfrag_delete);
static void pc_linext_set_file_size(PC_FILE *pefile, dword length_hi, dword length_lo);
void pc_subtract_64(dword hi_1, dword lo_1, dword hi_0, dword lo_0, dword *presult_hi, dword *presult_lo);
void pc_add_64(dword hi_1, dword lo_1, dword hi_0, dword lo_0, dword *presult_hi, dword *presult_lo);
#define EFEXT_REMOVE    1
#define EFEXT_SWAP      2
#define EFEXT_EXTRACT   3

static BOOLEAN _pc_efilio_extract(int op_code, int fd1, int fd2, dword clusters_to_move);

BOOLEAN pc_efilio_extract(int fd1, int fd2, dword n_clusters)
{
    return(_pc_efilio_extract(EFEXT_EXTRACT, fd1, fd2, n_clusters));
}

BOOLEAN pc_efilio_swap(int fd1, int fd2, dword n_clusters)
{
    return(_pc_efilio_extract(EFEXT_SWAP, fd1, fd2, n_clusters));
}

BOOLEAN pc_efilio_remove(int fd1,  dword n_clusters)
{
    return(_pc_efilio_extract(EFEXT_REMOVE, fd1, 0, n_clusters));
}


static BOOLEAN _pc_efilio_extract(int op_code, int fd1, int fd2, dword clusters_to_move)
{
PC_FILE *pefile1,*pefile2;
BOOLEAN ret_val;
DDRIVE *pdr;
REGION_FRAGMENT *pfrag1_cat,*pfrag1_middle,*pfrag1_to_delete;
REGION_FRAGMENT *pfrag1_start, *pfrag1_end;
REGION_FRAGMENT *pfrag2_cat,*pfrag2_middle;
REGION_FRAGMENT *pfrag2_start, *pfrag2_end;
dword ltemp_hi, ltemp_lo;
dword fp1hi, fp1lo, fp1_cluster, size1_hi, size1_lo;
dword fp2hi, fp2lo, fp2_cluster, size2_hi, size2_lo;
dword two_gig_in_clusters;
int is_error = 0;

    CHECK_MEM(BOOLEAN,0) /* Make sure memory is initted */
    rtfs_clear_errno();    /* clear errno */


    /* Check initial arguments.. swap must specify cluster count,
       the other operations can take to the end of file */
    if (op_code == EFEXT_SWAP && clusters_to_move == 0)
    {
        rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
        return(FALSE);
    }
    /* Null all lists we may create */
    pfrag1_cat = 0;
    pfrag1_middle =  pfrag1_start = pfrag1_end = pfrag1_to_delete = 0;
    pfrag2_cat = 0;
    pfrag2_middle = pfrag2_start= pfrag2_end = 0;

    pefile1 = pc_fd2file(fd1, PO_WRONLY|PO_RDWR);
    if (!pefile1)
        return(FALSE);
    pdr = pefile1->pobj->pdrive;

    pefile2 = 0;
    /* Access the second file for swap and move operations */
    if (op_code == EFEXT_SWAP || op_code == EFEXT_EXTRACT)
    {
        release_drive_mount(pefile1->pobj->pdrive->driveno);/* Release lock */
        pefile2 = pc_fd2file(fd2, PO_WRONLY|PO_RDWR);
        if (!pefile2)
            return(FALSE);
        /* If the two files aren't on the same volume it's no good */
        if (pefile2->pobj->pdrive != pdr)
        { // Note.. fix bug in cfilio_extract
            release_drive_mount(pefile2->pobj->pdrive->driveno);
            rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
            return(FALSE);
        }
    }

    ret_val = FALSE;
    pdr = pefile1->pobj->pdrive;

    /* Calculate the minimum number of clusters to hold 2 Gigabytes */
    two_gig_in_clusters = pc_byte2clmod(pdr, 0x80000000);

    /* Get the current file pointer for file 1 */
    if (!_pc_efilio_lseek(pefile1, 0, 0, PSEEK_CUR, &fp1hi, &fp1lo))
        goto return_locked;
    /* Seek to the end and restore to get the size and insure the cluster chain is loaded */
    if (!_pc_efilio_lseek(pefile1, 0, 0, PSEEK_END, &size1_hi, &size1_lo))
        goto return_locked;
    if (!_pc_efilio_lseek(pefile1, fp1hi, fp1lo, PSEEK_SET, &ltemp_hi, &ltemp_lo))
        goto return_locked;
    if (size1_hi == 0 && size1_lo == 0)
    { /* Nothing to do but not an error */
        ret_val = TRUE;
        goto return_locked;
    }
    else
    {
        /* Make a copy of all fragments in fd1 */
        pfrag1_cat = pc_linext_concatenate_fragments(pefile1);
        if (!pfrag1_cat)
            goto return_locked;
        /* Join all adjacent fragments in file 1 list */
        pc_linext_fraglist_coalesce(pfrag1_cat);
    }
    /* Calculate the cluster ofset of fp1 */
#if (INCLUDE_EXFATORFAT64)
	if (ISEXFATORFAT64(pdr))
		fp1_cluster = pc_byte2clmod64(pdr, fp1hi, fp1lo);
	else
#endif
		fp1_cluster = pc_byte2clmod(pdr, fp1lo);


    if (op_code == EFEXT_SWAP || op_code == EFEXT_EXTRACT)
    {
        /* Get the current file pointer for file 2  */
        if (!_pc_efilio_lseek(pefile2, 0, 0, PSEEK_CUR, &fp2hi, &fp2lo))
            goto return_locked;
        /* Get the file size for file 2  */
        if (!_pc_efilio_lseek(pefile2, 0, 0, PSEEK_END, &size2_hi, &size2_lo))
            goto return_locked;
        if (size2_hi || size2_lo)
        {
            /* Make a copy of all fragments in fd2 */
            pfrag2_cat = pc_linext_concatenate_fragments(pefile2);
            if (!pfrag2_cat)
                goto return_locked;
            /* Join all adjacent fragments in file 1 list */
            pc_linext_fraglist_coalesce(pfrag2_cat);
        }
#if (INCLUDE_EXFATORFAT64)
		if (ISEXFATORFAT64(pdr))
			fp2_cluster = pc_byte2clmod64(pdr, fp2hi, fp2lo);
		else
#endif
			fp2_cluster = pc_byte2clmod(pdr, fp2lo);

        /* Restore the file pointer  */
        if (!_pc_efilio_lseek(pefile2, fp2hi, fp2lo, PSEEK_SET, &ltemp_hi, &ltemp_lo))
            goto return_locked;
    }
    else
    {
        size2_hi = size2_lo = fp2hi = fp2lo = fp2_cluster = 0;
    }

    /* Modify fragment lists as prescribed by the opcode and clusters_to_move */

    /* Split file1 into start and middle if required */
    switch (op_code)
    {
    case EFEXT_REMOVE:
    case EFEXT_SWAP:
    case EFEXT_EXTRACT:
    {
        /* Split file1 at the cluster immediately following the file pointer  */
        if (fp1_cluster)
        {
            pfrag1_start = pfrag1_cat;
            pfrag1_middle = pc_fraglist_split(pdr, pfrag1_start, fp1_cluster, &is_error);
            if (!pfrag1_middle)     /* error must be out of frag structures */
                goto return_locked;
        }
        else
        {
            pfrag1_start =  0;
            pfrag1_middle = pfrag1_cat;
        }
        if (!pfrag1_middle)
        { /* Asking to move more clusters than are available or out of frag structures */
            if (!is_error)
                rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
            goto return_locked;
        }
    }
    break;
    default:
        break;
    };

    /* Split file1 middle into middle and end if required */
    /* if (clusters_to_move == 0) don't split because we are moving all */
    if (clusters_to_move)
    {
        switch (op_code)
        {
        case EFEXT_REMOVE:
        case EFEXT_SWAP:
        case EFEXT_EXTRACT:
        {
            /* if (clusters_to_move == 0) we take all clusters until the end */
            if (clusters_to_move != 0)
            {
                dword size_second_chain = 0;
                size_second_chain = pc_fraglist_count_clusters(pfrag1_middle, 0);
                if (size_second_chain < clusters_to_move)
                { /* Asking to move more clusters than are available */
                    rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
                    goto return_locked;
                }
                else if (size_second_chain > clusters_to_move)
                {   /* We are moving a portion. Split it so we can join start and end */
                    pfrag1_end = pc_fraglist_split(pdr, pfrag1_middle, clusters_to_move, &is_error);
                    if (!pfrag1_end)
                        goto return_locked;
                }
                /* Else we are moving rest of the file exactly */
            }
        }
        break;
        default:
            break;
        };
    }

    /* Split file2 into start and middle if required */
    /* Sets pfrag2_cat to start point for rebuilding */
    switch (op_code)
    {
    case EFEXT_SWAP:
    case EFEXT_EXTRACT:
    {
        /* Split file2 at the cluster immediately following the file pointer  */
        if (fp2_cluster)
        {
            pfrag2_start = pfrag2_cat;
            pfrag2_middle = pc_fraglist_split(pdr, pfrag2_start, fp2_cluster, &is_error);
            if (!pfrag2_middle)
            {
                if (is_error)       /* error must be out of frag structures */
                    goto return_locked;
                /* There must be a middle for a swap, but for an extract we can append to the end */
                if (op_code == EFEXT_SWAP)
                    goto return_locked;
            }
        }
        else
        {
            pfrag2_start =  0;
            pfrag2_middle = pfrag2_cat;
        }
    }
    break;
    default:
        break;
    };

    /* Split file2 middle into middle and end if required */
    if (op_code == EFEXT_SWAP)
    {
        dword size_second_chain = 0;
        size_second_chain = pc_fraglist_count_clusters(pfrag2_middle, 0);
        if (size_second_chain < clusters_to_move)
        { /* Asking to swap more clusters than are available */
            rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
            goto return_locked;
        }
        else if (size_second_chain > clusters_to_move)
        {   /* We are moving a portion. Split it so we can join start and end */
            pfrag2_end = pc_fraglist_split(pdr, pfrag2_middle, clusters_to_move, &is_error);
            if (!pfrag2_end)
                goto return_locked;
        }
    }


    /* Join the start and end fragments if delete or extract is removing the middle of the file */
    switch (op_code)
    {
    case EFEXT_REMOVE:
    case EFEXT_EXTRACT:
    {
        /* Rejoin the start and end fragments */
        if (pfrag1_end)
        {
            if (pfrag1_start)
                pc_end_fragment_chain(pfrag1_start)->pnext = pfrag1_end;
            else
                pfrag1_start = pfrag1_end;
        }
        /* prepare to reassemble file 1.. */
        pfrag1_cat = pfrag1_start;
    }
    break;
    default:
    break;
    }

    /* Insert the middle fragment of file 1 in between start and middle it is extract */
    switch (op_code)
    {
    case EFEXT_EXTRACT:
    {
        if (pfrag1_middle)
            pc_end_fragment_chain(pfrag1_middle)->pnext = pfrag2_middle;
        /* Rejoin the start and end fragments */
        if (pfrag2_start)
            pc_end_fragment_chain(pfrag2_start)->pnext = pfrag1_middle;
        else
            pfrag2_start = pfrag1_middle;
        /* prepare to reassemble file 2 */
        pfrag2_cat = pfrag2_start;
    }
    break;
    default:
    break;
    }

    /* Insert the middle fragment of file 1 in between start and middle of file 2 it is a swap */
    switch (op_code)
    {
    case EFEXT_SWAP:
    {
        if (pfrag1_middle)
            pc_end_fragment_chain(pfrag1_middle)->pnext = pfrag2_end;
        /* Rejoin the start and end fragments */
        if (pfrag2_start)
            pc_end_fragment_chain(pfrag2_start)->pnext = pfrag1_middle;
        else
            pfrag2_start = pfrag1_middle;
        /* prepare to reassemble file 2 */
        pfrag2_cat = pfrag2_start;
    }
    break;
    default:
    break;
    }

    /* Insert the middle fragment of file 2 in between start and middle of file 1 it is a swap */
    switch (op_code)
    {
    case EFEXT_SWAP:
    {
        if (pfrag2_middle)
            pc_end_fragment_chain(pfrag2_middle)->pnext = pfrag1_end;
        /* Rejoin the start and end fragments */
        if (pfrag1_start)
            pc_end_fragment_chain(pfrag1_start)->pnext = pfrag2_middle;
        else
            pfrag1_start = pfrag2_middle;
        /* prepare to reassemble file 1 */
        pfrag1_cat = pfrag1_start;
    }
    break;
    default:
    break;
    }

    /* Check that extract does not make the target file too large */
#if (INCLUDE_EXFATORFAT64)
        /* No limit for exFAT */
	if (!ISEXFATORFAT64(pefile1->pobj->pdrive))
#endif
    if (op_code == EFEXT_EXTRACT)
    {
        dword new_fd2_size_clusters;
        dword max_fd2_size_clusters;

        new_fd2_size_clusters = pc_fraglist_count_clusters(pfrag2_cat, 0);
        /* default max file size is 4 gig */
        max_fd2_size_clusters = two_gig_in_clusters*2;
        /* We can not append to file 2 if it is 32 bit and our new fragment is larger */
        if (new_fd2_size_clusters > max_fd2_size_clusters)
        {
            rtfs_set_errno(PETOOLARGE, __FILE__, __LINE__);
            goto return_locked;
        }
    }


    /* pfrag1_cat and pfrag2_cat are rethreaded and contain chains that must be built into files */
    /* Save middle of fd1 in case we are freeing it */
    if (op_code == EFEXT_REMOVE && pfrag1_middle)
        pfrag1_to_delete = pfrag1_middle;
    else
        pfrag1_to_delete = 0;

    /* clear other fragment pointers so we clean up correctly */
    pfrag1_middle = pfrag1_start = pfrag1_end =
    pfrag2_middle = pfrag2_start = pfrag2_end = 0;

    /* pfrag1_cat and pfrag2_cat now contain chains that must be built into files */
    /* Join all adjacent fragments in file 1 list again, in case a swap operation inserted an adjacent segment */
    pc_linext_fraglist_coalesce(pfrag1_cat);

    /* Rethread files from the fragment lists and update sizes */
    pc_linext_rebuild_file(pefile1, pfrag1_cat);
    pc_set_file_dirty(pefile1, TRUE);
    _pc_efilio_reset_seek(pefile1);
    pfrag1_cat = 0;         /* Used, don't release on future errors */

    /* pfrag1_middle will be non_zero if we are freeing a section of the file
       queue it for release when the file is flushed */
    if (op_code == EFEXT_REMOVE && pfrag1_to_delete)
    {
        pc_linext_queue_delete(pefile1, pfrag1_to_delete);
        pfrag1_to_delete = 0;
    }

    /* Rebuild file 2 on swap or extract */
    if (op_code == EFEXT_SWAP || op_code == EFEXT_EXTRACT)
    {
        /* Join all adjacent fragments in file 2 list */
        pc_linext_fraglist_coalesce(pfrag2_cat);
        /* Rethread the file list and update sizes */
        pc_linext_rebuild_file(pefile2, pfrag2_cat);
        pc_set_file_dirty(pefile2, TRUE);
        _pc_efilio_reset_seek(pefile2);
        pfrag2_cat = 0;         /* Used, don't release on future errors */
    }

    ret_val = TRUE;
    if (!_pc_efilio_lseek(pefile1, fp1hi, fp1lo, PSEEK_SET, &ltemp_hi, &ltemp_lo))
        ret_val = FALSE;
    if (op_code == EFEXT_SWAP || op_code == EFEXT_EXTRACT)
    {
        if (!_pc_efilio_lseek(pefile2, fp2hi, fp2lo, PSEEK_SET, &ltemp_hi, &ltemp_lo))
            ret_val = FALSE;
    }

    if (op_code == EFEXT_SWAP)
    {
        /* Restore origninal file sizes for both files */
        pc_linext_set_file_size(pefile1, size1_hi, size1_lo);
        pc_linext_set_file_size(pefile2, size2_hi, size2_lo);
    }
    else if (op_code == EFEXT_REMOVE || op_code == EFEXT_EXTRACT)
    {
        dword bytesmoved_hi,bytesmoved_lo,newsize_hi,newsize_lo ;

        /* Get the byte count of clusters moved */
        pc_clusters2bytes64(pdr, clusters_to_move, &bytesmoved_hi, &bytesmoved_lo);
        /* Reduce the size of fd1 by bytesmoved_hi:bytesmoved_lo*/
        pc_subtract_64(size1_hi, size1_lo, bytesmoved_hi, bytesmoved_lo, &newsize_hi, &newsize_lo);
        pc_linext_set_file_size(pefile1, newsize_hi, newsize_lo);

        /* If it was an extract increase the size of fd2 by bytesmoved_hi:bytesmoved_lo */
        if (op_code == EFEXT_EXTRACT)
        {
            /* Increase the size of fd2 by bytesmoved_hi:bytesmoved_lo*/
            pc_add_64(size2_hi, size2_lo, bytesmoved_hi, bytesmoved_lo, &newsize_hi, &newsize_lo);
            pc_linext_set_file_size(pefile2, newsize_hi, newsize_lo);

        }
    }

return_locked:
    if (!ret_val)
    { /* If we failed free all fragments that we allocated */
        if (pfrag1_to_delete) /* Only set if rebuild of fd1 failed before queing the delete */
            pc_fraglist_free_list(pfrag1_to_delete);
        if (pfrag1_start)
        {
            pc_fraglist_free_list(pfrag1_start);
            pfrag1_cat = 0; /* If _start or _middle or _end are defined then cat was split and must not be freed */
        }
        if (pfrag1_middle)
        {
            pc_fraglist_free_list(pfrag1_middle);
            pfrag1_cat = 0;
        }
        if (pfrag1_end)
        {
            pc_fraglist_free_list(pfrag1_end);
            pfrag1_cat = 0;
        }
        if (pfrag1_cat)
            pc_fraglist_free_list(pfrag1_cat);

        if (pfrag2_start)
        {
            pc_fraglist_free_list(pfrag2_start);
            pfrag2_cat = 0;
        }
        if (pfrag2_middle)
        {
            pc_fraglist_free_list(pfrag2_middle);
            pfrag2_cat = 0;
        }
        if (pfrag2_end)
        {
            pc_fraglist_free_list(pfrag2_end);
            pfrag2_cat = 0;
        }
        if (pfrag2_cat)
            pc_fraglist_free_list(pfrag2_cat);
    }
    if (!release_drive_mount_write(pdr->driveno))/* Release lock, unmount if aborted */
        ret_val = FALSE;

    return(ret_val);
}


static void pc_linext_queue_delete(PC_FILE *pefile, REGION_FRAGMENT *pfrag_delete)
{
    if (pfrag_delete)
    {
    FINODE *pefinode;
        pefinode = pefile->fc.plus.ffinode;
        if (pefinode)
        {
            if (pefinode->e.x->ptofree_fragment)
                pc_end_fragment_chain(pefinode->e.x->ptofree_fragment)->pnext = pfrag_delete;
            else
                pefinode->e.x->ptofree_fragment = pfrag_delete;
            pefinode->operating_flags |= FIOP_NEEDS_FLUSH;
        }
    }
}


static REGION_FRAGMENT *pc_linext_concatenate_fragments(PC_FILE *pefile)
{
	return(pc_linext_append_dup_fragments(pefile->pobj->pdrive, 0, pefile->fc.plus.ffinode->e.x->pfirst_fragment));
}

/* Duplicate a chain and attach it to the end of an existing chain if one is provided
   return zero on error or the begining og the chain */
static REGION_FRAGMENT *pc_linext_append_dup_fragments(DDRIVE *pdr, REGION_FRAGMENT *pappend_to, REGION_FRAGMENT *p_to_dup)
{
    REGION_FRAGMENT *pf, *pfnew, *pfnew_start;
    pfnew = pfnew_start = 0;
    while (p_to_dup)
    {
        pf = pc_fraglist_frag_alloc(pdr, p_to_dup->start_location, p_to_dup->end_location, 0);
        if (!pf)
        {
            if (pfnew_start)
            {
                pc_fraglist_free_list(pfnew_start);
                return(0);
            }
        }
        if (!pfnew)
            pfnew_start =  pf;
        else
            pfnew->pnext = pf;
        pfnew = pf;
        p_to_dup = p_to_dup->pnext;
    }
    if (pappend_to)
    {
        pc_end_fragment_chain(pappend_to)->pnext = pfnew_start;
        return(pappend_to);
    }
    else
    {
         return(pfnew_start);
    }
}

static void pc_linext_rebuild_file(PC_FILE *pefile,  REGION_FRAGMENT *pnew_chain)
{
    pc_linext_rebuild_finode(pefile->fc.plus.ffinode, pnew_chain);
}

static void pc_linext_rebuild_finode(FINODE *pefinode, REGION_FRAGMENT *pnew_chain)
{
    DDRIVE *pdr;

    pdr = pefinode->my_drive;

    /* Release existing fragments */
    if (pefinode->e.x->pfirst_fragment)
        pc_fraglist_free_list(pefinode->e.x->pfirst_fragment);
    if (pnew_chain)
    {
        dword ltemp;
        ltemp = pc_fraglist_count_clusters(pnew_chain,0);
#if (INCLUDE_EXFATORFAT64)
		if (ISEXFATORFAT64(pefinode->my_drive))
			pefinode->fsizeu.fsize64= (((ddword)ltemp) << pdr->drive_info.log2_bytespalloc);
		else
#endif
			pefinode->fsizeu.fsize = ltemp << pdr->drive_info.log2_bytespalloc;
        pc_pfinode_cluster(pdr, pefinode, pnew_chain->start_location);
    }
    else
    {
#if (INCLUDE_EXFATORFAT64)
		if (ISEXFATORFAT64(pefinode->my_drive))
			pefinode->fsizeu.fsize64 = 0;
		else
#endif
			pefinode->fsizeu.fsize = 0;

        pc_pfinode_cluster(pdr, pefinode, 0);
    }
#if (INCLUDE_EXFATORFAT64)
        /* No limit for exFAT */
	if (ISEXFATORFAT64(pefinode->my_drive))
		pefinode->e.x->alloced_size_bytes.val64 = pefinode->fsizeu.fsize64;
	else
#endif
		pefinode->e.x->alloced_size_bytes.val32 = pefinode->fsizeu.fsize;

    /* Set new chain and last processed cluster to zero to force a re-link */
    pefinode->e.x->pfirst_fragment = pnew_chain;
    pefinode->e.x->plast_fragment = pc_end_fragment_chain(pnew_chain);
    pefinode->e.x->last_processed_cluster = 0;
    pefinode->operating_flags |= FIOP_NEEDS_FLUSH;
}

/* This is equivalent to pc_fraglist_coalesce().. Using a private copy
   because pc_fraglist_coalesce() in version 600c was in adequate */
static void pc_linext_fraglist_coalesce(REGION_FRAGMENT *pfstart)
{
REGION_FRAGMENT *pf,*pfnext;
    pf = pfstart;
    while (pf && pf->pnext)
    {
        pfnext = pf->pnext;
        if (pfnext->start_location == pf->end_location+1)
        {
            pf->end_location = pfnext->end_location;
            pf->pnext = pfnext->pnext;
            pc_fraglist_frag_free(pfnext);
            /* Keep pf where it is in case pf->pnext is adjacent */
        }
        else
            pf = pf->pnext;
    }
}

static void pc_linext_set_file_size(PC_FILE *pefile, dword length_hi, dword length_lo)
{
    _pc_efinode_set_file_size(pefile->pobj->finode, length_hi, length_lo);
}

#endif /* Exclude from build if read only */
