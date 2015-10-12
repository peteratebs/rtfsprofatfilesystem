/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* RTFRAGMT.C - ERTFS basic fragment list management routines
   These basic fragment list operators are used by extensively by ProPlus
   and if certain fetures are enabled, by Pro */
#include "rtfs.h"

/* Count clusters in the list from pf to pfend. If pfend is null
   count till end, if pf is 0 return 0 */
dword pc_fraglist_count_clusters(REGION_FRAGMENT *pf,REGION_FRAGMENT *pfend)
{
    dword l;
    l = 0;
    while(pf)
    {
        l += PC_FRAGMENT_SIZE_CLUSTERS(pf);
        if (pf == pfend)
            break;
        pf= pf->pnext;
    }
    return(l);
}

/* Initialize the global freelist of fragment structures */
void pc_fraglist_init_freelist(void)
{
    int i;
    REGION_FRAGMENT *pf;

    pf = prtfs_cfg->mem_region_freelist = prtfs_cfg->mem_region_pool;
    if (!pf)
        return;
    for (i = 0; i < prtfs_cfg->cfg_NREGIONS-1; i++)
    {
        pf->pnext = pf+1;
        pf = pf + 1;
    }
    pf->pnext = 0;
    prtfs_cfg->region_buffers_free =
    prtfs_cfg->region_buffers_low_water = prtfs_cfg->cfg_NREGIONS;
}


/* Allocate a fragment from the freelist and initialize start, end an pnext fields */
REGION_FRAGMENT *pc_fraglist_frag_alloc(DDRIVE *pdr, dword frag_start,
                                   dword frag_end,
                                   REGION_FRAGMENT *pnext)
{
REGION_FRAGMENT *pf;
    OS_CLAIM_FSCRITICAL()
    pf = prtfs_cfg->mem_region_freelist;
    if (!pf && pdr)
    { /* If none available and pdr was provided, release freesapce and try again */
        /* Recycle region structures from freelists */
        OS_RELEASE_FSCRITICAL()
        free_manager_revert(pdr);
        OS_CLAIM_FSCRITICAL()
        pf = prtfs_cfg->mem_region_freelist;
    }
    if (pf)
    {
        prtfs_cfg->mem_region_freelist = pf->pnext;
        prtfs_cfg->region_buffers_free -= 1;
        if (prtfs_cfg->region_buffers_free < prtfs_cfg->region_buffers_low_water)
            prtfs_cfg->region_buffers_low_water = prtfs_cfg->region_buffers_free;
        OS_RELEASE_FSCRITICAL()
        pf->start_location = frag_start;
        pf->end_location = frag_end;
        pf->pnext = pnext;
    }
    else
    {
        OS_RELEASE_FSCRITICAL()
        rtfs_set_errno(PERESOURCEREGION, __FILE__, __LINE__);
    }
    return(pf);
}

/* Return a single fragment structure to the free list */
void pc_fraglist_frag_free(REGION_FRAGMENT *pf)
{
    if (pf)
    {
        OS_CLAIM_FSCRITICAL()
        pf->pnext = prtfs_cfg->mem_region_freelist;
        prtfs_cfg->mem_region_freelist = pf;
        prtfs_cfg->region_buffers_free += 1;
        OS_RELEASE_FSCRITICAL()
    }
}

/* Return all fragments in a list to fragment free pool */
void pc_fraglist_free_list(REGION_FRAGMENT *pf)
{
    REGION_FRAGMENT *pfirst;
    if (pf)
    {
        OS_CLAIM_FSCRITICAL()
        pfirst = pf;
        prtfs_cfg->region_buffers_free += 1;
        while (pf->pnext)
        {
            pf  = pf->pnext;
            prtfs_cfg->region_buffers_free += 1;
        }
        pf->pnext = prtfs_cfg->mem_region_freelist;
        prtfs_cfg->mem_region_freelist = pfirst;
        OS_RELEASE_FSCRITICAL()
    }
}

/* Seek to last region in a fragment chain */
REGION_FRAGMENT *pc_end_fragment_chain(REGION_FRAGMENT *pf)
{
    if (pf) { while(pf->pnext) pf = pf->pnext;}
    return(pf);
}


dword pc_fragment_size_32(DDRIVE *pdr, REGION_FRAGMENT *pf)
{
    dword clusters_in_fragment;
    clusters_in_fragment = pf->end_location - pf->start_location + 1;
    return(pc_alloced_bytes_from_clusters(pdr, clusters_in_fragment));
}


/* Allocate a fragment. If it is >= 4 gigabytes (max_cluster_per_fragment) allocate more fragments so none of them is larger than 4 gigabytes */
REGION_FRAGMENT *pc_fraglist_alloc_frag_clipped(DDRIVE *pdrive, dword first_new_cluster, dword nclusters)
{
dword currentcluster, nleft,max_cluster_per_fragment;
REGION_FRAGMENT *pf,*pnew_fragment,*pfprev;

	max_cluster_per_fragment = 1;
    max_cluster_per_fragment <<= (32-pdrive->drive_info.log2_bytespalloc);
	max_cluster_per_fragment -= 1;

	nleft = nclusters;
	currentcluster = first_new_cluster;
	pfprev = pf = pnew_fragment = 0;
	while (nleft)
	{
		dword ntoalloc;
		ntoalloc = nleft;
		if (ntoalloc > max_cluster_per_fragment)
			ntoalloc = max_cluster_per_fragment;
		pf = pc_fraglist_frag_alloc(pdrive, currentcluster,currentcluster+ntoalloc-1, 0);
		if (!pf)
		{
			if (pnew_fragment)
				pc_fraglist_free_list(pnew_fragment);
			return 0;
		}
		if (!pnew_fragment)
			pnew_fragment = pf;
		if (pfprev)
			pfprev->pnext = pf;
		pfprev = pf;
		nleft -= ntoalloc;
		currentcluster += ntoalloc;
	}
	return (pnew_fragment);
}

/* Append n_clusters to the pbasic_fragment chain making sure no fragment exceeds 4 gigabytes */
BOOLEAN pc_grow_basic_fragment(FINODE *pefinode, dword first_new_cluster, dword nclusters)
{
REGION_FRAGMENT *pf;

	/* If the chain is empty just add a new fragment */
	if (!pefinode->pbasic_fragment)
	{
		pefinode->pbasic_fragment = pc_fraglist_alloc_frag_clipped(pefinode->my_drive, first_new_cluster, nclusters);
		if (pefinode->pbasic_fragment) return(TRUE);else return(FALSE);
	}

	/* If the clusters are not contiguous with the chain, append a new fragment */
    pf = pc_end_fragment_chain(pefinode->pbasic_fragment);
    if (pf->end_location+1 != first_new_cluster)
	{
		pf->pnext = pc_fraglist_alloc_frag_clipped(pefinode->my_drive, first_new_cluster, nclusters);
		if (pf->pnext) return(TRUE);else return(FALSE);
	}

	/* The clusters are contiguous. Fill out the last segment and append more if needed */
	{
	dword ncontig,newlengthclusters,max_cluster_per_fragment;
		max_cluster_per_fragment = 1;
		max_cluster_per_fragment <<= (32-pefinode->my_drive->drive_info.log2_bytespalloc);
		max_cluster_per_fragment -= 1;

		ncontig = PC_FRAGMENT_SIZE_CLUSTERS(pf);
		newlengthclusters = ncontig + nclusters;
		if (newlengthclusters <= max_cluster_per_fragment)
		{
			pf->end_location += nclusters;
		}
		else
		{
		dword new_fragment_length;
			new_fragment_length = newlengthclusters - max_cluster_per_fragment;
			pf->end_location = pf->start_location + max_cluster_per_fragment - 1;
			pf->pnext = pc_fraglist_alloc_frag_clipped(pefinode->my_drive,pf->end_location+1, new_fragment_length);
			if (!pf->pnext)
				return (FALSE);
		}
	}
	return(TRUE);
}
