/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* rtfreemanager.c - ERTFS memory bease freespace management routines

 free_manager_attach   - Region manager attach function.
     This routine is called each time a device is opened. If the drive
     structure contains an initialized free_region_context field,
     this routine overrides several FAT driver functions to use
     the region manager to mange free space on the disk

 Fat driver functions - These routines take over some tasks from the FAT
     driver after being linked into the fat driver structure by
     free_manager_manager_attach.

 free_manager_add_freelist - put contiguous clusters on the list of free
     regions at steady state corresponds to zero values in the FAT
 free_manager_remove_freelist - remove contiguous clusters from the list of free
     regions. The clusters will ultimately be linked into
     cluster chains in the fat.
 free_manager_find_contiguous_free_clusters - Search between startpt, and endpt,
    Return at the first cluster where at least min_clusters contiguous free
    clusters are available. Report the number of contiguous free available
    clusters. in *p_contig. Truncate the return value to max_clusters
    Returns zero and set errno to PENOSPACE if no space in this range
    Return zero and set *is_error to one if a real error occured.
 free_manager_close_free_region - Release resources consumed by the region
    manager.
free_manager_release_clusters - Release clusters to the free cluster cache so they may be reallocated
free_manager_claim_clusters(DDRIVE *pdr, dword cluster, dword ncontig) - Remove clusters from the free cluster cache so they may not be allocated

*/

#include "rtfs.h"
#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */

#if (INCLUDE_RTFS_FREEMANAGER)

#define DEBUG_FREE_MANAGER 0
#if (DEBUG_FREE_MANAGER)
void DEBUG_check_freeregions(DDRIVE *pdr);
#define DEBUG_CHECK_FREEREGIONS(D) DEBUG_check_freeregions(D);
#else
#define DEBUG_CHECK_FREEREGIONS(D)
#endif


/* External interface - If needed attach the region manager to the drive and
   initialize the cache */
BOOLEAN free_manager_attach(DDRIVE *pdr)
{
    if (pdr->du.drive_operating_policy & DRVPOL_DISABLE_FREEMANAGER)
    {
        pdr->drive_state.free_ctxt_cluster_shifter = 0;
        return(TRUE);
    }
    /* Initialize the free list cache */
    {
    dword range,cluster_range;

        range = pdr->drive_info.maxfindex;
        /* How many equal sized ranges fit in our hash table ? */
        /* round the fat size up to nearest power of 2 */
        cluster_range = 1;  while(cluster_range < range){cluster_range <<= 1;}
        pdr->drive_state.free_ctxt_slot_size = cluster_range/RTFS_FREE_MANAGER_HASHSIZE;
    }
    {
    dword ltemp, shift_val;
        /* Calculate the shifter needed to divide cluster_range by nslots */
        shift_val = 0;
        ltemp = 1;
        while (ltemp < pdr->drive_state.free_ctxt_slot_size)
        {
            ltemp <<= 1;
            shift_val++;
        }
        /* The shifter also acts as an open flag if free_ctxt_cluster_shifter is zero the free manager is disabled */
        pdr->drive_state.free_ctxt_cluster_shifter = shift_val;
    }
     rtfs_memset((byte *)&(pdr->drive_state.free_ctxt_hash_tbl[0]), 0, sizeof(pdr->drive_state.free_ctxt_hash_tbl));
    return(TRUE);
}

/* Fat driver function free_manager_close_free_region() */
void free_manager_close(DDRIVE *pdr)
{
dword i;
    /* The shifter acts as an open flag so close only when needed and set it to zero */
    if (CHECK_FREEMG_OPEN(pdr))
    {
        for (i = 0; i < RTFS_FREE_MANAGER_HASHSIZE; i++)
        {
            pc_fraglist_free_list(pdr->drive_state.free_ctxt_hash_tbl[i]);
            pdr->drive_state.free_ctxt_hash_tbl[i] = 0;
        }
        pdr->drive_state.free_ctxt_cluster_shifter = 0;
    }
}

/* Fallback routine for if we run out of fragments. shut down the free manager and continue */
void free_manager_revert(DDRIVE *pdr)
{
    /* The shifter acts as an open flag so close only when needed and set it to zero */
    if (CHECK_FREEMG_OPEN(pdr))
    {
        free_manager_close(pdr);       /* Release all of our region structures */
        pc_release_all_prealloc(pdr);   /* Release all pre-allocated clusters because they aren't pre-allocated anymore */
        pc_flush_all_fil(pdr);          /* Flush all files so the FAT and the free counts are in sync with FAT tables */
    }
}


/* Fat driver function free_manager_find_contiguous_free_clusters()
    Seach between startpt, and endpt,
    Return at the first cluster where at least min_clusters contiguous free
    clusters are available.
    Report the number of contiguous free available clusters. in *p_contig
    Truncate the return value to max_clusters
    Returns zero and set errno to PENOSPACE if no space in this range
    Return zero and set *is_error to one if a real error occured.
*/
/* Called by fatop_plus_find_contiguous_free_clusters args already checked */
dword free_manager_find_contiguous_free_clusters(DDRIVE *pdr, dword startpt, dword endpt, dword min_clusters, dword max_clusters, dword *p_contig, int *is_error)
{
REGION_FRAGMENT *pf;
int starting_search;
dword slotno,free_manager_size,ltemp,start_point, end_point, saved_startpoint, saved_unionpoint;

    /* Check if we must perform FAT or bit map table scans because the memory based manager is closed */
    if (CHECK_FREEMG_CLOSED(pdr))
    { /* Closed, out of region structures */
#if (INCLUDE_FAILSAFE_RUNTIME)
    	if (prtfs_cfg->pfailsafe)
        {   /* Execute failsafe aware fallback code if needed */
            if (pdr->drive_state.failsafe_context)
                return(prtfs_cfg->pfailsafe->free_fallback_find_contiguous_free_clusters(pdr, startpt, endpt, min_clusters, max_clusters, p_contig, is_error));
            /* Or fall through */
        }
#endif
#if (INCLUDE_EXFATORFAT64)
		if (ISEXFATORFAT64(pdr))
		{ /* Tell exFAT that it Failed so it can use the BAM */
 			return 0;
		}
#endif
        /* fall through.. failsafe is not active, the FAT tables contain up to date free cluster information. Call the scan */
        return(_fatop_find_contiguous_free_clusters(pdr, startpt, endpt, min_clusters, max_clusters, p_contig, is_error));
    }
    DEBUG_CHECK_FREEREGIONS(pdr)  /* Check consistency if debugging */
    *is_error = 0;

    if (endpt < startpt)
        return(0);
    free_manager_size = endpt - startpt + 1;
    if (free_manager_size < max_clusters)
        max_clusters = free_manager_size;
    *is_error = 0;
    if ((startpt+min_clusters-1) > pdr->drive_info.maxfindex)
        goto no_find;

    ltemp = startpt >> pdr->drive_state.free_ctxt_cluster_shifter;
    starting_search = 1;
    saved_startpoint = 0; /* Save start and end clusters of current contig */
    saved_unionpoint = 0; /* Range since hashing splits fragments into muulti lists */
    for (slotno =  ltemp;slotno < RTFS_FREE_MANAGER_HASHSIZE; slotno++)
    {
        pf = pdr->drive_state.free_ctxt_hash_tbl[slotno];
        if (!pf)
        { /* This section of clusters are all allocated */
            saved_startpoint = 0;
            continue;
        }
        if (starting_search)
        { /* Find the first fragment containing startpt */
            while (pf)
            {
                if (pf->end_location >= startpt)
                {
                    starting_search = 0;
                    break;
                }
                pf = pf->pnext;
            }
        }
        if (!pf)
            continue;
        start_point = pf->start_location;
        if (start_point < startpt)  /* Fragment contains requested start point */
            start_point = startpt;
        if (saved_startpoint)
        {
            if (start_point == saved_unionpoint) /* Contiguous with last fragment */
                start_point = saved_startpoint;
            saved_startpoint = 0;
        }
        while (pf)
        {
            if (pf->end_location < startpt)
                goto no_find;
            if (pf->start_location > endpt)
                goto no_find;
            end_point =  pf->end_location;
            if (end_point > endpt)
                end_point = endpt;
            free_manager_size = end_point - start_point + 1;

            if (free_manager_size >= min_clusters)
            {
                if (free_manager_size <= max_clusters)
                    *p_contig = free_manager_size;
                else
                    *p_contig = max_clusters;
                return(start_point);
            }
            pf = pf->pnext;
            if (!pf)
            { /* End but still contiguous, save current start and end so we can continue */
                saved_startpoint = start_point;
                saved_unionpoint = end_point + 1;
            }
            else
            { /* It is not at end but not contiguous, so it must be a break */
                start_point = pf->start_location; /* Reset the start point */
            }
        } /* while pf */
    }     /* for (slotno) */
no_find:
    rtfs_set_errno(PENOSPC, __FILE__, __LINE__);
    return(0);
}

static BOOLEAN free_manager_edit_freelist(DDRIVE *pdr, dword cluster, dword ncontig, BOOLEAN adding);
/* Release clusters to the free cluster cache so they may be reallocated */
BOOLEAN free_manager_release_clusters(DDRIVE *pdr, dword cluster, dword ncontig, BOOLEAN do_erase)
{
    if (CHECK_FREEMG_OPEN(pdr))      /* If not open just return */
        free_manager_edit_freelist(pdr, cluster, ncontig, TRUE);
    /* Allow the device layer to erase the sectors, without this feature, data written to previously
       freed clusters perfroms more slowly than when the device was first formatted
       The function does nothing and returns TRUE if the device does not support erase blocks
    */
#if (INCLUDE_NAND_DRIVER)/* return(eraseop_erase_blocks(pdr, cluster, ncontig)); */
    if (do_erase)
        return(eraseop_erase_blocks(pdr, cluster, ncontig));
#else
    RTFS_ARGSUSED_INT((int)do_erase);
#endif
    return(TRUE);
}

/* Remove clusters from the free cluster cache so they may not be allocated */
void free_manager_claim_clusters(DDRIVE *pdr, dword cluster, dword ncontig)
{
    if (CHECK_FREEMG_OPEN(pdr))    /* If not open just return */
        free_manager_edit_freelist(pdr, cluster, ncontig, FALSE);
}

/*
   Used by diskstat function to return free clusters
   Count free fragments by finding a minimimum of one clusters and a max of the whole FAT
   this will give us the length of each free fragment, we add the length each time to the
   start to search the next segment */

dword free_manager_count_frags(DDRIVE *pdr)
{
    dword n_free_frags, startpt, endpoint, max_clusters, n_contig, next_contig;
    int is_error;

    n_free_frags = 0;
    startpt  = 2;
    endpoint = pdr->drive_info.maxfindex;
    max_clusters = pdr->drive_info.maxfindex - 1;
    /* Keep track of end points because free manager hashing only returns contiguous cluster up
       to the end of the hash region */
    next_contig = 0;
    while (startpt)
    {
        startpt = fatop_find_contiguous_free_clusters(pdr, startpt, endpoint, 1, max_clusters, &n_contig, &is_error, ALLOC_CLUSTERS_PACKED);
        if (startpt && n_contig)
        {
            if (next_contig != startpt)
                n_free_frags += 1;
            startpt += n_contig;
            next_contig = startpt;
        }
        else /* Defensive, if startpt is non-zero so is n_contig.. but guarantee no endless loops */
            break;
    }
    return(n_free_frags);
}

/* Helper function for fat driver functions free_manager_remove_freelist() and
   free_manager_add_freelist() */
static BOOLEAN free_manager_edit_freelist(DDRIVE *pdr, dword cluster, dword ncontig, BOOLEAN adding)
{
dword clno, slotno, slotbase_next, free_manager_end, end_clno;

    DEBUG_CHECK_FREEREGIONS(pdr)  /* Check consistency if debugging */

    clno = cluster;
    end_clno = cluster+ncontig-1;
    while (clno <= end_clno)
    {
        /* Convert a cluster number to a slot number */
        slotno = clno >> pdr->drive_state.free_ctxt_cluster_shifter;

        /* Get the cluster number at the end of this slot */
        slotbase_next = (slotno+1) << pdr->drive_state.free_ctxt_cluster_shifter;

        if (end_clno >= slotbase_next)
            free_manager_end = slotbase_next-1;
        else
            free_manager_end = end_clno;
        if (adding)
        {
        int is_error;
            pdr->drive_state.free_ctxt_hash_tbl[slotno] =
                    pc_fraglist_set_range(pdr->drive_state.free_ctxt_hash_tbl[slotno], clno, free_manager_end, &is_error);
            if (is_error)   /* Must be out of resources go clean up and we'll resume without a free manager */
                goto error_no_frags;
        }
        else
        {
        int is_error;
            pdr->drive_state.free_ctxt_hash_tbl[slotno] =
                    pc_fraglist_clear_range(pdr->drive_state.free_ctxt_hash_tbl[slotno], clno, free_manager_end, &is_error);

            if (is_error)   /* Must be out of resources go clean up and we'll resume without a free manager */
                goto error_no_frags;
        }
        clno = free_manager_end + 1;
    }
    DEBUG_CHECK_FREEREGIONS(pdr)  /* Check consistency if debugging */
    return(TRUE);
error_no_frags:
    DEBUG_CHECK_FREEREGIONS(pdr)  /* Check consistency if debugging */
    free_manager_revert(pdr);     /* Free up what we have so others can use it */
    return(FALSE);
}




#if (DEBUG_FREE_MANAGER)
/*DEBUG_FREE_MANAGER*/#define DEBUG_FREE_PRINTF printf
/*DEBUG_FREE_MANAGER*/
/*DEBUG_FREE_MANAGER*/void DEBUG_check_freeregions(DDRIVE *pdr)
/*DEBUG_FREE_MANAGER*/{
/*DEBUG_FREE_MANAGER*/REGION_FRAGMENT *pf;
/*DEBUG_FREE_MANAGER*/dword slotno,n_fragments,total_free;
/*DEBUG_FREE_MANAGER*/
/*DEBUG_FREE_MANAGER*/    n_fragments = total_free = 0;
/*DEBUG_FREE_MANAGER*/
/*DEBUG_FREE_MANAGER*/    {
/*DEBUG_FREE_MANAGER*/        REGION_FRAGMENT *pf_array[RTFS_FREE_MANAGER_HASHSIZE];
/*DEBUG_FREE_MANAGER*/        /* So they are easier to see with the debugger */
/*DEBUG_FREE_MANAGER*/        for (slotno =  0;slotno < RTFS_FREE_MANAGER_HASHSIZE;slotno++)
/*DEBUG_FREE_MANAGER*/            pf_array[slotno] = pdr->drive_state.free_ctxt_hash_tbl[slotno];
/*DEBUG_FREE_MANAGER*/
/*DEBUG_FREE_MANAGER*/        for (slotno =  0;slotno < RTFS_FREE_MANAGER_HASHSIZE;slotno++)
/*DEBUG_FREE_MANAGER*/        {
/*DEBUG_FREE_MANAGER*/            pf = pf_array[slotno];
/*DEBUG_FREE_MANAGER*/            while (pf)
/*DEBUG_FREE_MANAGER*/            {
/*DEBUG_FREE_MANAGER*/                n_fragments += 1;
/*DEBUG_FREE_MANAGER*/                if (pf->start_location > pdr->maxfindex ||  pf->end_location > pdr->maxfindex)
/*DEBUG_FREE_MANAGER*/                {
/*DEBUG_FREE_MANAGER*/                    DEBUG_FREE_PRINTF("Freelist: Too big error at slot %d, start, end %d, %d\n",
/*DEBUG_FREE_MANAGER*/                                slotno, pf->start_location,pf->end_location);
/*DEBUG_FREE_MANAGER*/                }
/*DEBUG_FREE_MANAGER*/                else if (pf->start_location < 2 ||  pf->end_location < 2)
/*DEBUG_FREE_MANAGER*/                {
/*DEBUG_FREE_MANAGER*/                    DEBUG_FREE_PRINTF("Freelist: Too small error at slot %d, start, end %d, %d\n",
/*DEBUG_FREE_MANAGER*/                                slotno, pf->start_location,pf->end_location);
/*DEBUG_FREE_MANAGER*/                }
/*DEBUG_FREE_MANAGER*/                else
/*DEBUG_FREE_MANAGER*/                    total_free += (pf->end_location-pf->start_location+1);
/*DEBUG_FREE_MANAGER*/                pf = pf->pnext;
/*DEBUG_FREE_MANAGER*/            }
/*DEBUG_FREE_MANAGER*/        }
/*DEBUG_FREE_MANAGER*/        DEBUG_FREE_PRINTF("Freespace has %d clusters in %d fragments in %d slots\n", total_free, RTFS_FREE_MANAGER_HASHSIZE);
/*DEBUG_FREE_MANAGER*/      }
/*DEBUG_FREE_MANAGER*/}
#endif/*  (DEBUG_FREE_MANAGER) */
#endif  /*(!INCLUDE_RTFS_FREEMANAGER)*/
#endif /* Exclude from build if read only */
