/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* PREFINODE.C - Contains internal 32 bit enhanced finode source code.*/

#include "rtfs.h"
#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */


/* Finode will not always be pefile->pobj->finode so they are passed seperately */
dword _pc_efinode_count_to_link(FINODE *pefinode,dword allocation_policy)
{
dword clusters_to_process,start_contig,start_offset;
REGION_FRAGMENT *pf;

    start_contig = start_offset = 0;
    clusters_to_process = 0;
    if (pefinode->e.x->last_processed_cluster)
    {
        if (pefinode->e.x->plast_fragment &&
            pefinode->e.x->plast_fragment->end_location ==
            pefinode->e.x->last_processed_cluster)
                return(0);
        /* Get offset to last linked and number left. Also works if last_processed_cluster is zero */
        pf = pc_fraglist_find_next_cluster(pefinode->e.x->pfirst_fragment,0,pefinode->e.x->last_processed_cluster,&start_contig,&start_offset);
    }
    else if (pefinode->e.x->pfirst_fragment)/* Get offset to first cluster. This will set up the return code correctly */
    {
        pf = pefinode->e.x->pfirst_fragment;
        start_contig = pefinode->e.x->pfirst_fragment->start_location;
        start_offset = 0;
    }
    else
        pf = 0;

    if (!pf)
        return(0); /* nothing to do */
    /* Start with the residual */
    clusters_to_process =  pf->end_location - start_contig + 1;
    /* Add in the rest */
    if (pf->pnext)
      clusters_to_process += pc_fraglist_count_clusters(pf->pnext,0);


    /* Now process KEEP */
     if (!(allocation_policy & PCE_KEEP_PREALLOC))
     { /* Bump up the filesize */
     dword total_link_needed;
     dword total_link_queued;
        total_link_queued =  start_offset + clusters_to_process;
         /* How many clusters are linked */
#if (INCLUDE_EXFATORFAT64)
		if (ISEXFATORFAT64(pefinode->my_drive))
			total_link_needed = pc_byte2clmod64(pefinode->my_drive ,pefinode->fsizeu.fsize64>>32,(dword)(pefinode->fsizeu.fsize64)&0xffffffff);
		else
#endif
			total_link_needed = pc_byte2clmod(pefinode->my_drive , pefinode->fsizeu.fsize);

        if (total_link_queued > total_link_needed)
        {
        dword ltemp;
            ltemp = total_link_queued - total_link_needed;
            if (ltemp > clusters_to_process)
                clusters_to_process = 0;
            else
                clusters_to_process -= ltemp;
        }
     }
     return(clusters_to_process);
}

/* Finode will not always be pefile->pobj->finode so they are passed seperately */
BOOLEAN _pc_efinode_queue_cluster_links(FINODE *pefinode, dword allocation_policy)
{
dword start_contig,start_offset;
REGION_FRAGMENT *pf;


    /* Process fragment chain if just starting and it is queued for deletion */
    if (pefinode->e.x->ptofree_fragment)
    {
        if (!pefinode->e.x->last_deleted_cluster)
            pefinode->e.x->clusters_to_delete =
                pc_fraglist_count_clusters(pefinode->e.x->ptofree_fragment, 0);
    }
    else
        pefinode->e.x->clusters_to_delete = 0;

    start_contig = start_offset = 0;
    pefinode->e.x->clusters_to_process = 0;
    if (pefinode->e.x->last_processed_cluster)
    {
        if (pefinode->e.x->plast_fragment &&
            pefinode->e.x->plast_fragment->end_location ==
            pefinode->e.x->last_processed_cluster)
                return(TRUE);
        /* Get offset to last linked and number left. Also works if last_processed_cluster is zero */
        pf = pc_fraglist_find_next_cluster(pefinode->e.x->pfirst_fragment,0,pefinode->e.x->last_processed_cluster,&start_contig,&start_offset);
    }
    else if (pefinode->e.x->pfirst_fragment)/* Get offset to first cluster. This will set up the return code correctly */
    {
        pf = pefinode->e.x->pfirst_fragment;
        start_contig = pefinode->e.x->pfirst_fragment->start_location;
        start_offset = 0;
    }
    else
        pf = 0;

    if (!pf)
    {
#if (INCLUDE_EXFATORFAT64)
		if (pefinode->e.x->last_processed_cluster || (!ISEXFATORFAT64(pefinode->my_drive)&&pefinode->e.x->alloced_size_bytes.val32) || (ISEXFATORFAT64(pefinode->my_drive)&&pefinode->e.x->alloced_size_bytes.val64))
#else
		if (pefinode->e.x->last_processed_cluster || pefinode->e.x->alloced_size_bytes.val32)
#endif
        { /* Something is wrong */
            rtfs_set_errno(PEINVALIDCLUSTER, __FILE__, __LINE__);
            return(FALSE);
        }
        return(TRUE); /* nothing to do */
    }
    /* Start with the residual */
    pefinode->e.x->clusters_to_process =  pf->end_location - start_contig + 1;

    /* Add in the rest */
    if (pf->pnext)
      pefinode->e.x->clusters_to_process += pc_fraglist_count_clusters(pf->pnext,0);

    /* Now process KEEP */
     if (allocation_policy & PCE_KEEP_PREALLOC)
     { /* Bump up the filesize */
#if (INCLUDE_EXFATORFAT64)
		if (ISEXFATORFAT64(pefinode->my_drive))
			pefinode->fsizeu.fsize64 = pefinode->e.x->alloced_size_bytes.val64;
		else
#endif
       		pefinode->fsizeu.fsize = pefinode->e.x->alloced_size_bytes.val32;
        /* Cluster to link et al stay the same */
     }
     else
     {
     dword total_link_needed;
     dword total_link_queued;
        /* Get the number of clusters that still need to be linked */
        total_link_queued =  start_offset + pefinode->e.x->clusters_to_process;
        /* How many clusters are linked */
#if (INCLUDE_EXFATORFAT64)
		if (ISEXFATORFAT64(pefinode->my_drive))
			total_link_needed = pc_byte2clmod64(pefinode->my_drive ,(dword)(pefinode->fsizeu.fsize64>>32),(dword)(pefinode->fsizeu.fsize64&0xffffffff));
		else
#endif
			total_link_needed = pc_byte2clmod(pefinode->my_drive , pefinode->fsizeu.fsize);

        if (total_link_queued > total_link_needed)
        {  /* Subtract count of clusters beyond the end of the sized file */
        dword ltemp;
            ltemp = total_link_queued - total_link_needed;
            if (ltemp > pefinode->e.x->clusters_to_process)
                pefinode->e.x->clusters_to_process = 0;
            else
                pefinode->e.x->clusters_to_process -= ltemp;
        }
     }
     /* Help the routines that follow */
     /* We are done..            */
     /* ->pasy_fragment has the frag to begin linking from */
     /* ->clusters_to_process is updated */
     /* ->last_processed_cluster is current */
     /* fsize is updated if KEEP was requested */

     return(TRUE);
}



/* A file is closing. If its fragment list representation is
   larger than it's FAT representation, release the unused
   clusters to the region manager to be reallocated again
   Modified to reterminate the list, set allocated size
   to actual size and discard excess fragments
   this change allows us to clip pre-allocated clusters
   from files at run-time */
void pc_free_excess_clusters(FINODE *pefinode)
{
    dword  valid_size_clusters;
    DDRIVE *pdr;
    int is_error;

    pdr = pefinode->my_drive;
#if (INCLUDE_EXFATORFAT64)
	if (ISEXFATORFAT64(pefinode->my_drive))
		valid_size_clusters = pc_byte2clmod64(pefinode->my_drive , (dword)(pefinode->fsizeu.fsize64>>32),(dword)(pefinode->fsizeu.fsize64&0xffffffff));
	else
#endif
		valid_size_clusters = pc_byte2clmod(pdr, pefinode->fsizeu.fsize);

    if (pefinode->e.x->alloced_size_clusters > valid_size_clusters)
    {
    REGION_FRAGMENT *pexcess;
        pexcess = pc_fraglist_split(pdr, pefinode->e.x->pfirst_fragment,valid_size_clusters,&is_error);
        if (pexcess)
        {
            pc_fraglist_add_free_region(pdr, pexcess);
            pc_fraglist_free_list(pexcess);
        }
        pefinode->e.x->alloced_size_clusters = valid_size_clusters;
     }
}



/* Link clusters represented by the finode's fragment list into a chain and
   update the fat teble. */
BOOLEAN pc_efinode_link_or_delete_cluster_chain(FINODE *pefinode, dword flags, dword max_clusters_per_pass)
{
REGION_FRAGMENT *pf;
DDRIVE *pdr;
dword  clusters_to_process,start_contig, start_offset,last_processed_cluster;

    if (pefinode->e.x->clusters_to_process)
    {
        pdr = pefinode->my_drive;
        pf = pefinode->e.x->pfirst_fragment;
        start_contig = 0;
        if (pefinode->e.x->last_processed_cluster) /* Find the starting point if not zero */
        {
            pf = pc_fraglist_find_next_cluster(pf,0,pefinode->e.x->last_processed_cluster,&start_contig,&start_offset);
        }
        else if (pf) /* Start with the whole first fragment */
        {
            start_contig = pf->start_location;
        }
        if (!pf)
        {
            rtfs_set_errno(PEINVALIDCLUSTER, __FILE__, __LINE__);
            return(FALSE);
        }

        /* Now link or delete the chains */
        if (!max_clusters_per_pass)
            clusters_to_process = pefinode->e.x->clusters_to_process;
        else if (pefinode->e.x->clusters_to_process > max_clusters_per_pass)
            clusters_to_process = max_clusters_per_pass;
        else
            clusters_to_process = pefinode->e.x->clusters_to_process;
        last_processed_cluster =
                pc_frag_chain_delete_or_link(pdr,
                flags,
                pf,
                pefinode->e.x->last_processed_cluster,start_contig,clusters_to_process);
        if (!last_processed_cluster)
            return(FALSE);
        else
        {
            pefinode->e.x->last_processed_cluster = last_processed_cluster;
            pefinode->e.x->clusters_to_process -= clusters_to_process;
            return(TRUE);
        }
    }
    return(TRUE);
}

/* Link clusters represented by the finode's fragment list into a chain and
   update the fat teble. */
BOOLEAN pc_efinode_delete_cluster_chain(FINODE *pefinode, dword max_clusters_per_pass)
{
REGION_FRAGMENT *pf;
DDRIVE *pdr;
dword  clusters_to_delete,start_contig, start_offset,last_deleted_cluster;

    if (pefinode->e.x->clusters_to_delete)
    {
        pdr = pefinode->my_drive;
        pf = pefinode->e.x->ptofree_fragment;
        start_contig = 0;
        if (pefinode->e.x->last_deleted_cluster) /* Find the starting point if not zero */
        {
            pf = pc_fraglist_find_next_cluster(pf,0,pefinode->e.x->last_deleted_cluster,&start_contig,&start_offset);
        }
        else if (pf) /* Start with the whole first fragment */
        {
            start_contig = pf->start_location;
        }
        if (!pf)
        {
            rtfs_set_errno(PEINVALIDCLUSTER, __FILE__, __LINE__);
            return(FALSE);
        }

        /* Now link or delete the chains */
        if (!max_clusters_per_pass)
            clusters_to_delete = pefinode->e.x->clusters_to_delete;
        else if (pefinode->e.x->clusters_to_delete > max_clusters_per_pass)
            clusters_to_delete = max_clusters_per_pass;
        else
            clusters_to_delete = pefinode->e.x->clusters_to_delete;
        /* Delete the cluster and tell the region manager */
        last_deleted_cluster =
                pc_frag_chain_delete_or_link(pdr,
                FOP_RMTELL,
                pf,
                pefinode->e.x->last_deleted_cluster,start_contig,clusters_to_delete);
        if (!last_deleted_cluster)
            return(FALSE);
        else
        {
            pefinode->e.x->last_deleted_cluster = last_deleted_cluster;
            pefinode->e.x->clusters_to_delete -= clusters_to_delete;
            return(TRUE);
        }
    }
    return(TRUE);
}




BOOLEAN _pc_efinode_truncate_finode(FINODE *pfi)
{
    if (pfi)
    {
#if (INCLUDE_EXFATORFAT64)
		if ((!ISEXFATORFAT64(pfi->my_drive)&&pfi->fsizeu.fsize)||(ISEXFATORFAT64(pfi->my_drive)&&pfi->fsizeu.fsize64))
#else
		if (pfi->fsizeu.fsize)
#endif
        {
            if (pfi->e.x->pfirst_fragment)
            {
                if (!pc_fraglist_fat_free_list(pfi->my_drive, pfi->e.x->pfirst_fragment))
                    return(FALSE);
                pc_fraglist_free_list(pfi->e.x->pfirst_fragment);
                pfi->e.x->pfirst_fragment = 0;
                pfi->e.x->plast_fragment = 0;
            }
            pc_pfinode_cluster(pfi->my_drive,pfi,0);
            pfi->fsizeu.fsize = 0L;
#if (INCLUDE_EXFATORFAT64)
			pfi->e.x->alloced_size_bytes.val64 = 0;
#endif
			pfi->e.x->alloced_size_bytes.val32 = 0;
            pfi->e.x->alloced_size_clusters = 0;
            pfi->e.x->clusters_to_process = 0;
            pfi->e.x->last_processed_cluster = 0;
            pfi->operating_flags |= FIOP_NEEDS_FLUSH;
         }
    }
    return(TRUE);
}

void _pc_efinode_set_file_size(FINODE *pefinode, dword length_hi, dword length_lo)
{
#if (INCLUDE_EXFATORFAT64)
	if (ISEXFATORFAT64(pefinode->my_drive))
		pefinode->fsizeu.fsize64 = ((ddword)length_hi)<<32|(ddword)(length_lo&0xffffffff);
    else
#endif
		pefinode->fsizeu.fsize = length_lo;
    pefinode->operating_flags |= FIOP_NEEDS_FLUSH;
}

/* A file is being truncated. Queue unused clusters for releasing from the FAT
   and the region manager if they are already linked on fat. Otherwize
   just free them. Also queues the file for relinking the FAT chains */
BOOLEAN  _pc_efinode_chsize(FINODE *pefinode,dword newsize_hi, dword newsize_lo)
{
    dword  new_size_clusters;
    DATESTR crdate;
    REGION_FRAGMENT *pfreefirst_fragment;
    int is_error;
    DDRIVE *pdr;
	llword new_size;

    pdr = pefinode->my_drive;

#if (INCLUDE_EXFATORFAT64)
	if (ISEXFATORFAT64(pefinode->my_drive))
	{
		new_size.val64 = ((ddword)newsize_hi)<<32|(ddword)newsize_lo;
		if (pefinode->fsizeu.fsize64 <= new_size.val64)
		    return(TRUE);
		new_size_clusters = pc_byte2clmod64(pdr, (dword)(new_size.val64>>32) , (dword) (new_size.val64&0xffffffff));
	}
	else
#endif
	{
		new_size.val32 = newsize_lo;
		if (pefinode->fsizeu.fsize <= new_size.val32)
		    return(TRUE);
		new_size_clusters = pc_byte2clmod(pdr, new_size.val32);
	}




    if (pefinode->e.x->alloced_size_clusters > new_size_clusters)
    {
        if (!new_size_clusters)
        {
            pfreefirst_fragment = pefinode->e.x->pfirst_fragment;
            pefinode->e.x->pfirst_fragment = pefinode->e.x->plast_fragment = 0;
        }
        else
        {
            /* Split the chain, put clusters to free in a seperate list */
            pfreefirst_fragment = pc_fraglist_split(pdr, pefinode->e.x->pfirst_fragment,
                new_size_clusters, &is_error);
            if (is_error) /* Oops out of fragments */
                return(FALSE);
            /* Get the new last fragment value */
            pefinode->e.x->plast_fragment =
                pc_end_fragment_chain(pefinode->e.x->pfirst_fragment);
        }
        if (pefinode->e.x->ptofree_fragment)
            pc_end_fragment_chain(pefinode->e.x->ptofree_fragment)->pnext = pfreefirst_fragment;
        else
            pefinode->e.x->ptofree_fragment = pfreefirst_fragment;
        /* Relink the whole chain when we flush */
        pefinode->e.x->last_processed_cluster = 0;
        pefinode->e.x->alloced_size_clusters = new_size_clusters;
#if (INCLUDE_EXFATORFAT64)
		if (ISEXFATORFAT64(pefinode->my_drive))
			pefinode->e.x->alloced_size_bytes.val64 =  pc_alloced_bytes_from_clusters_64(pdr, new_size_clusters);
		else
#endif
			pefinode->e.x->alloced_size_bytes.val32 =  pc_alloced_bytes_from_clusters(pdr, new_size_clusters);

    }
    /* Set the new size */
#if (INCLUDE_EXFATORFAT64)
	if (ISEXFATORFAT64(pefinode->my_drive))
	{
		pefinode->fsizeu.fsize64 = new_size.val64;
		if (!pefinode->fsizeu.fsize64)        /* Zero the start point if we have to */
			pc_pfinode_cluster(pdr, pefinode,0);
	}
	else
#endif
	{
		pefinode->fsizeu.fsize = new_size.val32;
		if (!pefinode->fsizeu.fsize)        /* Zero the start point if we have to */
			pc_pfinode_cluster(pdr, pefinode,0);
	}
    pefinode->operating_flags |= FIOP_NEEDS_FLUSH;
    pefinode->fattribute |= ARCHIVE;
    pc_getsysdate(&crdate);
    pefinode->ftime = crdate.time;
    pefinode->fdate = crdate.date;
    return(TRUE);
}

/* Link "clusters_to_process" clusters in a fragment list starting
   at "start_contig" into a chain in the FAT table. If "start_cluster"
   is specified continue the chain terminated at "start_cluster" by linking
   it to "start_contig". "start_contig" must reside withing the fragment
   at pf */

dword pc_frag_chain_delete_or_link(DDRIVE *pdr,
                    dword flags,
                    REGION_FRAGMENT *pf,
                    dword prev_end_chain,
                    dword start_contig,
                    dword clusters_to_process)
{
dword  n_left, n_contig, last_processed_cluster, next_frag;
dword  user_buffer_size;
byte   *pubuff;

    /* Deferred link don't alert region manager */
    if (prev_end_chain && (flags & FOP_LINK))
    {
        if (!fatop_pfaxx(pdr, prev_end_chain, start_contig))
            return(0);
    }

    /* Now link the chains */
    pubuff = pc_claim_user_buffer(pdr, &user_buffer_size, 0); /* released at cleanup */
    last_processed_cluster = 0;
    n_left = clusters_to_process;
    n_contig = pf->end_location - start_contig + 1;
    while (n_left)
    {
		BOOLEAN ret_val;
        next_frag = 0;
        if (n_contig > n_left)  /*  Truncate chain if needed */
            n_contig = n_left;
        else if (pf->pnext)
        {
            next_frag = pf->pnext->start_location;
        }
        /* link: flags will instruct to alert region manager */
        ret_val=fatop_link_frag(pdr, pubuff, user_buffer_size, flags, next_frag, start_contig, n_contig);

		if (!ret_val)
        {   /* Error, break out and return 0 */
            last_processed_cluster = 0;
            break;
        }
        last_processed_cluster = start_contig + n_contig-1;
        n_left -= n_contig;
        if (n_left)
        {
            pf = pf->pnext;
            if (pf)
            {
                start_contig = pf->start_location;
                n_contig = PC_FRAGMENT_SIZE_CLUSTERS(pf);
            }
            else
            {   /* Error, break out and return 0 */
                last_processed_cluster = 0;
                break;
            }
        }
    }
    if (pubuff)
        pc_release_user_buffer(pdr, pubuff);
    return (last_processed_cluster);
}

/* Looks for "chain_size" free clusters in the default allocation region
   for the file, implement PCE_FIRST_FIT,PCE_FORCE_FIRST, and
   PCE_FORCE_CONTIGUOUS policies */


static REGION_FRAGMENT *pc_find_free_chain_in_fat_region(
                DDRIVE *pdr,
                dword region_start,
                dword region_end,
                dword start_hint,
                dword min_segment_size,
                dword chain_size,
                int allocation_scheme,
                int *p_is_error);

REGION_FRAGMENT *pc_region_file_find_free_chain(
                DDRIVE *pdrive,
                dword allocation_policy,
                dword alloc_start_hint,
                dword chain_size,
                int *p_is_error)
{
    dword region_start, region_end;
    dword min_segment_size;
    REGION_FRAGMENT *pnew_frags;
    int allocation_scheme;

    *p_is_error = 0;
    pnew_frags = 0;

    region_start = 2;
    region_end   = pdrive->drive_info.maxfindex;

    while (!pnew_frags)
    {
        if (allocation_policy & PCE_FORCE_FIRST)
        {
           min_segment_size = 1; /* prefer fragmenting */
           allocation_scheme = ALLOC_CLUSTERS_UNALIGNED;    /* For flash this also turns on garbage collection, allocating clusters from partial erase blocks */
        }
        else
        {
        	if ((pdrive->du.drive_operating_policy & DRVPOL_NAND_SPACE_OPTIMIZE) == 0)
			{
				min_segment_size = chain_size; /* No fragmenting */
				allocation_scheme = ALLOC_CLUSTERS_ALIGNED;
			}
			else
			{
				min_segment_size = 1; /* prefer fragmenting */
				allocation_scheme = ALLOC_CLUSTERS_PACKED;
			}
        }
        pnew_frags = pc_find_free_chain_in_fat_region(pdrive, region_start, region_end, alloc_start_hint, min_segment_size, chain_size,allocation_scheme,p_is_error);
        if (*p_is_error)
            return(0);
        if (!pnew_frags && min_segment_size > 1)
        {
            if (!(allocation_policy & PCE_FORCE_CONTIGUOUS))
            {
                /* Try the same region but don't require contiguous (min segment = 1) */
                pnew_frags = pc_find_free_chain_in_fat_region(pdrive, region_start, region_end, alloc_start_hint, 1, chain_size,allocation_scheme,p_is_error);
                if (*p_is_error)
                    return(0);
            }
        }
        if (!pnew_frags)
        {
            if (alloc_start_hint)       /* If we started at a hint and didn't get anything, try from the start of the current region */
                alloc_start_hint = 0;
            else
            {
                /* Break out if we started from the beginning */
                if (region_start == 2)
                    break;
                /* We check from the start hint, then the file region base,
                   now try from the beginning of the FAT */
                region_start = 2;
                alloc_start_hint = 0;
            }
        }
    }
    return(pnew_frags);
}


static REGION_FRAGMENT *pc_find_free_chain_in_fat_region(
                DDRIVE *pdr,
                dword region_start,
                dword region_end,
                dword start_hint,
                dword min_segment_size,
                dword chain_size,
                int allocation_scheme,
                int *p_is_error)
{
REGION_FRAGMENT *pf, *pfcurr, *pfstart;
dword start_contig, n_contig;


    *p_is_error = 0;
    n_contig = 0;

    if (start_hint < region_start)
        start_hint = region_start;
    if (start_hint > region_end)
        return(0);
    /* Find chain_size clusters using min_segment_size
       between start_hint and region_end */
    pfstart = pfcurr = 0;

    while (chain_size)
    {
        if ((region_end - start_hint + 1) < min_segment_size)
            break;      /* Can't find */
        else
           start_contig = fatop_find_contiguous_free_clusters(pdr,
                        start_hint,
                        region_end,
                        min_segment_size,
                        chain_size, &n_contig, p_is_error, allocation_scheme);
        if (!start_contig || *p_is_error)
            break;

        if (pfcurr && pfcurr->end_location+1 == start_contig)
        { /* Merge with previous fragment if they are contiguous */
            pfcurr->end_location += n_contig;
        }
        else
        {
            /* Note that we have a new section */
            /* Allocate a fragment. If none available recycle freelists. If we fail we we are out of luck */
            pf = pc_fraglist_frag_alloc(pdr, start_contig, start_contig+n_contig-1, 0);
            if (!pf)
            {
                *p_is_error = 1;
                break;
            }
            if (!pfstart)
                pfstart = pf;
            else
                pfcurr->pnext = pf;
            pfcurr = pf;
        }
        chain_size -= n_contig;
        start_hint = start_contig + n_contig;
    }
    if (*p_is_error || chain_size)
    {
        /* we didn't find them all */
        if (pfstart)
            pc_fraglist_free_list(pfstart);
        pfstart = 0;
    }
    return(pfstart);
}
#endif /* Exclude from build if read only */
