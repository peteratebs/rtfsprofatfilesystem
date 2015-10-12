/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* RTFATDRV.C - Low level FAT management functions, shared by Pro and Pro Plus.
*/

#include "rtfs.h"
#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */

#if (RTFS_CFG_LEAN)
#define INCLUDE_FATPAGE_DRIVER 0
#else
#define INCLUDE_FATPAGE_DRIVER 1
#endif


static dword fatop_buff_find_contiguous_free_clusters(DDRIVE *pdr,
                                            dword startpt,
                                            dword endpt,
                                            dword min_clusters,
                                            dword max_clusters,
                                            dword *p_contig,
                                            int *is_error);
static BOOLEAN fatop_buff_link_frag(DDRIVE *pdr, dword flags, dword cluster, dword startpt, dword n_contig);

#if (INCLUDE_FATPAGE_DRIVER) /* Declare friend functions to rtfatdrv.c provided by ProPlus */
static BOOLEAN fatop_page_link_frag(DDRIVE *pdr, byte *palt_buffer, dword alt_buffer_size, dword flags, dword link_cluster, dword startpt, dword n_contig);
static dword fatop_page_find_contiguous_free_clusters(DDRIVE *pdr,
                                            dword startpt,
                                            dword endpt,
                                            dword min_clusters,
                                            dword max_clusters,
                                            dword *p_contig,
                                            int *is_error);
#endif

#if (INCLUDE_FAT32)
static BOOLEAN fatxx_pfpdword(DDRIVE *pdr, dword index, dword *pvalue);
#endif


/******************************************************************************
    fatop_alloc_chain  -  Allocate as many contiguous clusters as possible.

 Description
        Reserve up to n_clusters contiguous clusters from the FAT and
        return the number of contiguous clusters reserved.
        If previous_end points to a valid cluster  then link the new chain to it.

 Returns
    Returns the number of contiguous clusters found. Or zero on an error.
    pstart_cluster contains the address of the start of the chain on
    return.

*****************************************************************************/


dword fatop_alloc_chain(DDRIVE *pdr, BOOLEAN is_file, dword hint_cluster, dword previous_end, dword *pfirst_new_cluster, dword n_clusters, dword min_clusters) /*__fatfn__*/
{
    dword last_scanned_cluster;
    dword first_new_cluster;
    dword n_contig;
    int is_error, allocation_scheme;

    if (is_file && (pdr->du.drive_operating_policy & DRVPOL_NAND_SPACE_OPTIMIZE) == 0)    /* For flash, default to allocating file data from empty erase blocks if possible */
        allocation_scheme = ALLOC_CLUSTERS_ALIGNED;
    else
        allocation_scheme = ALLOC_CLUSTERS_PACKED;

    is_error = 0;
    if (previous_end &&
        ( (previous_end < 2) || (previous_end > pdr->drive_info.maxfindex) ) )
    {
        rtfs_set_errno(PEINVALIDCLUSTER, __FILE__, __LINE__);   /* fatop_alloc_file_chain: bad cluster value internal error */
        return (0);
    }
    /* If the user provided a cluster we find the next cluster beyond that
        one. Otherwise we look at the disk structure and find the next
        free cluster in the free cluster region after the current best guess
        of the region. If that fails we look to the beginning of the region
        and if that fails we look in the non-contiguous region. */
    last_scanned_cluster = pdr->drive_info.maxfindex;  /* We haven't scanned any yet */
    first_new_cluster = 0;
    n_contig = 0;
    if ( (hint_cluster<2) || (hint_cluster > pdr->drive_info.maxfindex) )
        hint_cluster = 0;
    if (hint_cluster)
    {
        /* search from the start_cluster hint to the end of the fat   */
        first_new_cluster = fatop_find_contiguous_free_clusters(pdr, hint_cluster, pdr->drive_info.maxfindex, min_clusters, n_clusters, &n_contig, &is_error, allocation_scheme);
        if (is_error)   /* Error reading fat */
            return(0);
        /* If we search again search only to the start_cluster */
        last_scanned_cluster = hint_cluster;
    }
    if (is_file)
    { /* Try allocating files from file locations , directory fall through and allocate from 2 on up */
        dword start_scan;
        start_scan = pdr->drive_info.free_contig_pointer;	/* Allocate file clusters starting at free_contig_pointer */

		ERTFS_ASSERT(start_scan >= 2)
		if (start_scan < 2)
			start_scan = 2;

        if (!first_new_cluster)
        {
            /* Search from free_contig_pointer to the last unsearched region of the fat */
            if (last_scanned_cluster >= start_scan)
            {
                first_new_cluster = fatop_find_contiguous_free_clusters(pdr, start_scan , last_scanned_cluster, min_clusters, n_clusters, &n_contig, &is_error, allocation_scheme);
                if (is_error)   /* Error reading fat */
                    return(0);
                /* If we search again search only to the free_contig_pointer since we've searched above it */
                last_scanned_cluster  = start_scan;
            }
        }
        if (!first_new_cluster)
        {
            dword start_scan;
            start_scan = pdr->drive_info.free_contig_base;
            /* Search from free_contig_base to the last unsearched region of the fat */
            if (last_scanned_cluster >= start_scan)
            {
                first_new_cluster = fatop_find_contiguous_free_clusters(pdr, start_scan, last_scanned_cluster, min_clusters, n_clusters, &n_contig, &is_error,allocation_scheme);
                if (is_error)   /* Error reading fat */
                    return(0);
                last_scanned_cluster = start_scan;
            }
        }
    }
    /* None found yet ? then check the beginning of the disk */
    if (!first_new_cluster)
    {
        /* Search from 2 to the last unsearched region of the fat */
        first_new_cluster = fatop_find_contiguous_free_clusters(pdr, 2, last_scanned_cluster, min_clusters, n_clusters, &n_contig, &is_error,allocation_scheme);
        if (is_error)   /* Error reading fat */
            return(0);
    }
    if (!first_new_cluster)
    {
       rtfs_set_errno(PENOSPC, __FILE__, __LINE__);    /* fatop_alloc_file_chain: No free clusters */
       return(0);
    }

    /* 0, 0 for alt buffer and size means use FAT table */
	/* exFAT will not link the chain because FOP_EXF_CHAIN is not true */
   if (!fatop_link_frag(pdr, 0, 0, FOP_RMTELL|FOP_LINK_PREV|FOP_LINK, previous_end, first_new_cluster, n_contig))
   	return(0);

    *pfirst_new_cluster = first_new_cluster;
    rtfs_clear_errno(); /* clear error status */
    return(n_contig);
}


/****************************************************************************
    fatop_clgrow - Extend a cluster chain and return the next free cluster

 Description
    Given a DDRIVE and a cluster, extend the chain containing the cluster
    by allocating a new cluster and linking clno to it. If clno is zero
    assume it is the start of a new file and allocate a new cluster.

    Note: The chain is traversed to the end before linking in the new
            cluster. The new cluster terminates the chain.
 Returns
    Return a new cluster number or 0 if the disk is full.

****************************************************************************/

/* dword fatop_grow_dir(DDRIVE *pdrive, dword previous_end)

    Allocate a directory cluster, starting at previous_end and link the new
    cluster to previous_end

    Return 0 if none found or an error

    Note: fatop_grow_dir() will supercede __pc_clgrow() when newvfat completed
*/

dword fatop_grow_dir(DDRIVE *pdr, dword previous_end)  /* __fatfn__ */
{
    dword first_new_cluster;
    /* alloc a directory cluster, start looking for free clusters at previous_end and  link
       previous_end to the new cluster and returns the new cluster value in first_new_cluster */
    if (fatop_alloc_chain(pdr, FALSE, previous_end, previous_end, &first_new_cluster, 1, 1))
        return(first_new_cluster);
#if (INCLUDE_FAILSAFE_RUNTIME)
    /* If we ran out of space, call Failsafe and tell it to shrink the journal file
       by at least one cluster so we can try again.. If Failsafe is not running or
       if the journal file size, can't be reduced no clustser will be released
       and the our retry attempt will fail. */
    if (prtfs_cfg->pfailsafe && get_errno() == PENOSPC)
    {
        rtfs_clear_errno(); /* clear error status */
        if (prtfs_cfg->pfailsafe->fs_recover_free_clusters(pdr, 1))
        {
            first_new_cluster = 0;
            if (fatop_alloc_chain(pdr, FALSE, previous_end, previous_end, &first_new_cluster, 1, 1))
                return(first_new_cluster);
        }
    }
#endif
    return(0);
}


/* fatop_alloc_dir(DDRIVE *pdrive, dword clbase)

    Allocate a directory cluster, starting at clbase

    Return 0 if none found

    Note: fatop_alloc_dir() will supercede _pc_alloc_dir when newvfat completed
*/

dword fatop_alloc_dir(DDRIVE *pdr, dword clhint)
{
    dword first_new_cluster;

    if (clhint < 2 || clhint >= pdr->drive_info.free_contig_base)
        clhint = 2;
    /* alloc a directory cluster, start looking for free clusters at previous_end and  link
       previous_end to the new cluster and returns the new cluster value in first_new_cluster */
    if (fatop_alloc_chain(pdr, FALSE, clhint, 0, &first_new_cluster, 1, 1))
        return(first_new_cluster);
#if (INCLUDE_FAILSAFE_RUNTIME)
    if (prtfs_cfg->pfailsafe && get_errno() == PENOSPC)
    {
    /* If we ran out of space, call Failsafe and tell it to shrink the journal file
       by at least one cluster so we can try again.. If Failsafe is not running or
       if the journal file size, can't be reduced no clustser will be released
       and the our retry attempt will fail. */
        rtfs_clear_errno(); /* clear error status */
        first_new_cluster = 0;
        if (prtfs_cfg->pfailsafe->fs_recover_free_clusters(pdr, 1))
        {
            if (fatop_alloc_chain(pdr, FALSE, clhint, 0, &first_new_cluster, 1, 1))
                return(first_new_cluster);
        }
    }
#endif
    return(0);
}


/* Note: The caller locks the fat before calling this routine   */
dword  fatop_clgrow(DDRIVE *pdr, dword  clno, dword *previous_end)
{
    dword nextcluster;
    long range_check;

    *previous_end = 0;
    /* Check the incoming argument. Should be a valid cluster */
    if ((clno < 2) || (clno > pdr->drive_info.maxfindex) )
    {
        rtfs_set_errno(PEINVALIDCLUSTER, __FILE__, __LINE__);
        return (0);
    }
    /* Make sure we are at the end of chain   */
    range_check = 0;
    nextcluster = fatop_next_cluster(pdr , clno);
    while (nextcluster != FAT_EOF_RVAL && ++range_check < MAX_CLUSTERS_PER_DIR)
    {
        if (!nextcluster) /* fatop_next_cluster - set errno */
            return (0);
        clno = nextcluster;
        nextcluster = fatop_next_cluster(pdr , clno);
    }
    if (get_errno())
        return (0);
    if (range_check == MAX_CLUSTERS_PER_DIR)
    {
        rtfs_set_errno(PEINVALIDCLUSTER, __FILE__, __LINE__);
        return (0);
    }
    *previous_end = clno;
    /* Get a new cluster and link it to clno */
    return(fatop_grow_dir(pdr, clno));
}


/****************************************************************************
    void fatop_truncate_dir(DDRIVE *pdr, DROBJ *pobj, dword cluster, dword previous_end)

 Description
    Free the cluster chain at cluster
    If previous_end is non zero, overwrite (terinate at that cluster)
    The FAT is not flushed.

 Returns
****************************************************************************/
void fatop_truncate_dir(DDRIVE *pdr, DROBJ *pobj, dword cluster, dword previous_end) /* __fatfn__ */
{
    int current_errno;
    RTFS_ARGSUSED_PVOID((void *) pobj); /* Not used by Pro */
    if ((cluster < 2) || (cluster > pdr->drive_info.maxfindex) )
        return ;
    /* This is a cleanup routine, an earlier event is the interesting errno
       to the application, so we restore errno if we fail */
    current_errno = get_errno();
    if (fatop_freechain(pdr, cluster, MAX_CLUSTERS_PER_DIR))
    {
        if (previous_end)    /* Terminate the chain */
            fatop_pfaxxterm(pdr, previous_end);
    }
    rtfs_set_errno(current_errno, __FILE__, __LINE__);
}

/* Return a single cluster to the freelist */
void fatop_clrelease_dir(DDRIVE *pdr, dword  clno)
{
    fatop_free_frag(pdr,FOP_RMTELL,0, clno, 1);
 }


/****************************************************************************
    fatop_freechain - Free a cluster chain associated with an inode.

 Description
    Trace the cluster chain starting at cluster and return all the clusters to
    the free state for re-use. The FAT is not flushed.

 Returns
    Nothing.

****************************************************************************/
/* Note: The caller locks the fat before calling this routine   */

BOOLEAN fatop_freechain(DDRIVE *pdr, dword cluster, dword max_clusters_to_free)
{
    dword next_cluster,n_contig;
    dword clusters_freed;
    int end_of_chain;

	ERTFS_ASSERT(!ISEXFATORFAT64(pdr)) /* exFAT/FAT64 should not call this  */

    if (max_clusters_to_free==0)
        return(TRUE);

    /* Add endless loop protection */
    if (max_clusters_to_free > pdr->drive_info.maxfindex)
        max_clusters_to_free = pdr->drive_info.maxfindex;

    if ((cluster < 2) || (cluster > pdr->drive_info.maxfindex) )
    {
        rtfs_set_errno(PEINVALIDCLUSTER, __FILE__, __LINE__);
        return (FALSE);
    }
    clusters_freed = 0;
    end_of_chain = 0;

    while (clusters_freed < max_clusters_to_free && !end_of_chain)
    {
        n_contig = fatop_get_frag(pdr, 0, 0, cluster, &next_cluster,max_clusters_to_free-clusters_freed, &end_of_chain);
        if (!n_contig)
            return (FALSE); /* will only happen on an error condition, fatop_get_frag set errno */
        /* Truncate the number to release if beyond the requested maximum */
        if (clusters_freed + n_contig > max_clusters_to_free)
            n_contig = max_clusters_to_free-clusters_freed;

        /* Zero from cluster to cluster + n_contig and tell */
        /* The free region manager that these clusters are free */
        if (!fatop_free_frag(pdr,FOP_RMTELL,0, cluster, n_contig))
           return(FALSE);
        clusters_freed += n_contig;
        cluster = next_cluster;
    }
    return(TRUE);
}


BOOLEAN fatop_free_frag(DDRIVE *pdr, dword flags, dword prev_cluster, dword startpt, dword n_contig)
{
    dword endpt;
    endpt = startpt + n_contig-1;
    if ((startpt < 2) || (endpt > pdr->drive_info.maxfindex) )
    {
        rtfs_set_errno(PEINVALIDCLUSTER, __FILE__, __LINE__);
        return (FALSE);
    }
    return(fatop_link_frag(pdr, 0, 0, flags|FOP_TERM_PREV, prev_cluster, startpt, n_contig));
}


/* Fat driver function region_remove_freelist() */
BOOLEAN fatop_remove_free_region(DDRIVE *pdr, dword cluster, dword ncontig)
{
dword last_cluster;

    last_cluster = cluster+ncontig-1;
    if (cluster < 2 || last_cluster > pdr->drive_info.maxfindex)
    {
        ERTFS_ASSERT(rtfs_debug_zero())
        return(TRUE);
    }
#if (INCLUDE_RTFS_FREEMANAGER)
    free_manager_claim_clusters(pdr, cluster, ncontig);
#endif
    ERTFS_ASSERT(pdr->drive_info.known_free_clusters >= ncontig)
    pdr->drive_info.known_free_clusters = (pdr->drive_info.known_free_clusters - ncontig);
#if (INCLUDE_EXFATORFAT64)
    if (!ISEXFATORFAT64(pdr) ) /* Update free_contig_pointer if not exfat */
#endif
	{
    	/* Update Pro style hint of most likely place to find a free cluster
    	if the start of the region is the old hint, change the hint */
    	if (cluster == pdr->drive_info.free_contig_pointer)
        	pdr->drive_info.free_contig_pointer = last_cluster+1;
    	/* Wrap to the start if we allocated maxfindex */
    	if (pdr->drive_info.free_contig_pointer >= pdr->drive_info.maxfindex)
        	pdr->drive_info.free_contig_pointer = pdr->drive_info.free_contig_base;
	}
    return(TRUE);
}


/****************************************************************************
    fatop_flushfat -  Write any dirty FAT blocks to disk

 Description
    Given a valid drive number. Write any fat blocks to disk that
    have been modified. Updates all copies of the fat.

 Returns
    Returns FALSE if driveno is not an open drive. Or a write failed.

****************************************************************************/

BOOLEAN fatop_flushfat(DDRIVE *pdr)
{
    if (!pdr)
        return(FALSE);

#if (INCLUDE_EXFATORFAT64)
	if (ISEXFATORFAT64(pdr))
	{
		/* What do we do if AUTOFLUSH is disabled ?? */
		return(pcexfat_flush(pdr));
	}
#endif
#if (INCLUDE_RTFS_PROPLUS) /* ProPlus Fake a succesful return if auto flush is disabled */
    if (pdr->du.drive_operating_policy & DRVPOL_DISABLE_AUTOFLUSH)
        return(TRUE);
#endif

    /* Don't check for dirty flags or call fat_flushinfo(). That is all done inside pc_flush_fat_blocks() */
    return (pc_flush_fat_blocks(pdr));
}


BOOLEAN fatop_link_frag(DDRIVE *pdr, byte *palt_buffer, dword alt_buffer_size,  dword flags, dword cluster, dword startpt, dword n_contig)
{
    dword endpt = startpt + n_contig-1;
    if ((startpt < 2) || (endpt > pdr->drive_info.maxfindex) )
    {
        rtfs_set_errno(PEINVALIDCLUSTER, __FILE__, __LINE__);
        return (FALSE);
    }
    if (flags & FOP_RMTELL)
    {
        if (flags & FOP_LINK)
        {
#if (INCLUDE_EXFATORFAT64)
			if (ISEXFATORFAT64(pdr))
			{ /* Remove reange of clusters from the free region manager and clears the BAM bits */
				if (!exfatop_remove_free_region(pdr, startpt, n_contig))
					return FALSE;
			}
			else
#endif
			{ /* This removes from the free region manager. The chain is linked below */
				if (!fatop_remove_free_region(pdr, startpt, n_contig))
					return FALSE;
			}
        }
        else
        {
#if (INCLUDE_FAILSAFE_RUNTIME)      /* queue free clusters in Failsafe */
        /* If failsafe is included call fs_add_free_region()..
           this routine will call fatop_add_free_region() if
           failsafe is not enebled. If Failsafe is enabled it
           will queue the clusters to be put back in free space
           as soon as the FAT is synchronized with the journal file.
           This is necessary because we do not want clusters that
           are free in the Journalled view of the meta-structure but are
           still part of the on disk meta structures to be re-used and
           overwritten. Once the on-disk FAT structure is synchronized
           failsafe will dequeueu these records and call
           fatop_add_free_region() */
    		if (prtfs_cfg->pfailsafe)
    		{
                if (!prtfs_cfg->pfailsafe->fs_add_free_region(pdr, startpt, n_contig))
                    return(FALSE);
    		}
    		else
#endif
    		{
#if (INCLUDE_EXFATORFAT64)
    			if (ISEXFATORFAT64(pdr))
    			{
    				if (!exfatop_add_free_region(pdr, startpt, n_contig, TRUE))
    					return FALSE;
    			}
    			else
#endif
				{
    				if (!fatop_add_free_region(pdr, startpt, n_contig, TRUE))
    					return(FALSE);
				}
    		}
        }
    }
#if (INCLUDE_EXFATORFAT64)
	if (ISEXFATORFAT64(pdr))
	{ /* Exfat only links the chain when commanded from above. FAT64 never creates cluster chains */
		if (!(flags & FOP_EXF_CHAIN))
			return TRUE;
	}
#endif

#if (INCLUDE_FATPAGE_DRIVER)
    if (pdr->drive_info.fasize != 3)
        return(fatop_page_link_frag(pdr, palt_buffer, alt_buffer_size,  flags, cluster, startpt, n_contig));
#else
    RTFS_ARGSUSED_PVOID((void *) palt_buffer);
    RTFS_ARGSUSED_DWORD(alt_buffer_size);
#endif
     return(fatop_buff_link_frag(pdr, flags, cluster, startpt, n_contig));
}


/***************************************************************************
    fatop_buff_link_frag()  -  Link or zero a contiguous group of clusters

    Description
        Starting at startpt link the contiguous clusters into
        a terminated chain.

        If do_zero is TRUE Zero the clusters instead of linking them
        If do_zero is TRUE and prev_cluster is non zero
            terminate the previous list
        If do_zero is FALSE and prev_cluster is non zero
            link the previous list to start_point
        If do_zero is FALSE and next_cluster is non zero
            link this chain to next_cluster
 Returns
    Returns TRUE on success, false on an IO error.
****************************************************************************/

static BOOLEAN fatop_buff_link_frag(DDRIVE *pdr, dword flags, dword cluster,
                            dword startpt, dword n_contig)
{
dword value, clno;

    if (!n_contig || !startpt)
        return(TRUE);
    if (flags & FOP_LINK)
    {
        if (cluster && (flags & FOP_LINK_PREV))
        {   /* Link the chain */
           if (!startpt)
           {
               if (!fatop_pfaxxterm(pdr, cluster))
                    return (FALSE); /* fat12_XXXX set errno */
           }
           else
                if (!fatop_pfaxx(pdr, cluster, startpt))
                    return (FALSE); /* fat12_XXXX set errno */
        }
        n_contig -= 1;
        value = clno = startpt;
        while (n_contig--)
        {
            value += 1;
            if (!fatop_pfaxx(pdr, clno, value))
                return (FALSE); /* fat12_XXXX set errno */
            clno  += 1;
        }
        if (cluster && (flags & FOP_LINK_NEXT))
        {   /* Link the chain */
            if (!fatop_pfaxx(pdr, clno, cluster))
                return (FALSE); /* fat12_XXXX set errno */
        }
        else    /* Terminate the chain */
            if (!fatop_pfaxxterm(pdr, clno))
                return (FALSE); /* fat12_XXXX set errno */
        return(TRUE);
    }
    else /* Zeroe */
    {
        if (cluster && (flags & FOP_TERM_PREV))
        {
            if (!fatop_pfaxxterm(pdr, cluster))
                return (FALSE); /* fat12_XXXX set errno */
        }
        while (n_contig--)
        {
            if (!fatop_pfaxx(pdr, startpt, 0x0000))
                return (FALSE); /* fat12_XXXX set errno */
            startpt += 1;
        }
        return(TRUE);
    }
}


/* Find contiguous free clusters
   Inputs: startpt , endpt - cluster range to search
           min_clusters - minimum number of contiguous clusters or fail
           max_clusters - maximum number of free clusters to allocate
   Outputs -
            returns the first free cluster in the range
            *p_contig - the number in the range
            *is_error = 1 if an IO or some other system error occured
*/

#if (INCLUDE_NAND_DRIVER)   /* If the device has erase blocks use the erase block optimize algorithm */
dword get_clusters_per_erase_block(DDRIVE *pdr);  /* Change July 2010 */
#endif
dword fatop_find_contiguous_free_clusters(DDRIVE *pdr, dword startpt, dword endpt, dword min_clusters, dword max_clusters, dword *p_contig, int *is_error, int allocation_scheme)
{
dword range_size;

    *is_error = 0;
    if (endpt < startpt || max_clusters < min_clusters)
        return(0);
    range_size = endpt - startpt + 1;
    if (range_size < max_clusters)
        max_clusters = range_size;
    if (max_clusters < min_clusters)
        return(0);

    if ((startpt < 2) || (startpt > pdr->drive_info.maxfindex) || (endpt < 2) || (endpt > pdr->drive_info.maxfindex) )
    {
        rtfs_set_errno(PEINVALIDCLUSTER, __FILE__, __LINE__);
        *is_error = 1;
        return (0);
    }
#if (INCLUDE_NAND_DRIVER)   /* If the device has erase blocks use the erase block optimize algorithm */
    if (pdr->pmedia_info->eraseblock_size_sectors && get_clusters_per_erase_block(pdr) > 1)	/* Change July 2010. Don't call EB aware allocator if */
    {
    dword ret_val = 0;
        ret_val = eraseop_find_free_clusters(pdr, startpt, endpt, min_clusters, max_clusters, allocation_scheme, p_contig, is_error);

        if (!ret_val && !*is_error)
        {   /* If we are not already using ALLOC_CLUSTERS_PACKED, retry using the packed method to get it any way we can */
            if (allocation_scheme == ALLOC_CLUSTERS_ALIGNED)
			{
				if ((pdr->du.drive_operating_policy & DRVPOL_NAND_SPACE_RECOVER) == 0)
				{
					rtfs_set_errno(PENOEMPTYERASEBLOCKS, __FILE__, __LINE__);
					return(0);
				}
			}
			if (allocation_scheme != ALLOC_CLUSTERS_PACKED)
            {
                rtfs_clear_errno(); /* clear error status */
                ret_val = eraseop_find_free_clusters(pdr, startpt, endpt, min_clusters, max_clusters, ALLOC_CLUSTERS_PACKED, p_contig, is_error);
            }
        }
        return(ret_val);
    }
#else
    RTFS_ARGSUSED_INT(allocation_scheme);
#endif

#if (INCLUDE_RTFS_FREEMANAGER)
    /* Search ram based free manager for clusters to allocate.. if the ram based manager is shut down
    free_manager_find_contiguous_free_clusters() will perform FAT scans for free clusters but exclude
    free clusters that are reserved by Failsafe */
    return(free_manager_find_contiguous_free_clusters(pdr, startpt, endpt, min_clusters, max_clusters, p_contig, is_error));
#else
    /* Call FAT scan routine */
    return(_fatop_find_contiguous_free_clusters(pdr, startpt, endpt, min_clusters, max_clusters, p_contig, is_error));
#endif
}


/*
    Find contiguous free clusters internal version
        This is called by fatop_find_contiguous_free_clusters() when no memory based free manager is included
        at compile time or by the free manager when it runs out of REGION structures and must consult the FAT tables
        to find free clusters, the input argument are valid.  */
dword _fatop_find_contiguous_free_clusters(DDRIVE *pdr, dword startpt, dword endpt, dword min_clusters, dword max_clusters, dword *p_contig, int *is_error)
{
dword first_free;
    *is_error = 0;
#if (INCLUDE_EXFATORFAT64)
	if (ISEXFATORFAT64(pdr))
	{
    	first_free = exfatop_find_contiguous_free_clusters(pdr, startpt, endpt, min_clusters, max_clusters, p_contig, is_error);
	}
	else
#endif
    {
#if (INCLUDE_FATPAGE_DRIVER)
        if (pdr->drive_info.fasize != 3)
            first_free = fatop_page_find_contiguous_free_clusters(pdr, startpt, endpt, min_clusters, max_clusters, p_contig, is_error);
        else
#endif
            first_free = fatop_buff_find_contiguous_free_clusters(pdr, startpt, endpt, min_clusters, max_clusters, p_contig, is_error);
        if (*is_error)
            first_free = 0;
        else if (!first_free) /* november 2009, set PENOSPC if normal return and no space was found */
            rtfs_set_errno(PENOSPC, __FILE__, __LINE__);
    }
    return (first_free);
}

/***************************************************************************
    fatop_buff_find_contiguous_free_clusters()  -

 Description
 Find the first zero valued cluster in a range, then count the number of contiguous free
 clusters from there to the end of the range

 Returns
        Returns # free up to range size.
        The count of the number of contiguous zero valued clusters is returned through *p_contig.
        If an error occurs *is_error is set to 1 and ERRNO is set.
****************************************************************************/

/* Called by fatop_plus_find_contiguous_free_clusters args already checked */
static dword fatop_buff_find_contiguous_free_clusters(DDRIVE *pdr,
                                            dword startpt,
                                            dword endpt,
                                            dword min_clusters,
                                            dword max_clusters,
                                            dword *p_contig,
                                            int *is_error)
{
dword clno, value, start_contig, n_contig, range_size;

    *is_error = 0;

    if (endpt < startpt)
        return(0);
    range_size = endpt - startpt + 1;
    if (range_size < max_clusters)
        max_clusters = range_size;
    if (max_clusters < min_clusters)
        return(0);

    clno = startpt;
    start_contig = 0;
    n_contig = 0;

    while (range_size--)
    {
        if (!fatop_get_cluster(pdr, clno, &value))
        {
            *is_error = 1;
            return(0);
        }
        if (value == 0)
        {
            if (!n_contig)
                start_contig = clno;
            n_contig += 1;
            if (n_contig == max_clusters)
                break;
        }
        else
        {
            if (n_contig >= min_clusters)
                break;
            n_contig = 0;
        }
        clno += 1;
    }
    if (n_contig < min_clusters)
        return(0);
    *p_contig = n_contig;
    return(start_contig);
}

/******************************************************************************
    fatop_pfaxxterm - Write a terminating value to the FAT at clno.

 Description
    Given a DDRIVE,cluster number and value. Write the value 0xffffffff or
    0xffff in the fat at clusterno. Handle 32, 16 and 12 bit fats correctly.

 Returns
    FALSE if an io error occurred during fat swapping, else TRUE

*****************************************************************************/

/* Given a clno & fatval Put the value in the table at the index (clno)    */
/* Note: The caller locks the fat before calling this routine              */
BOOLEAN fatop_pfaxxterm(DDRIVE   *pdr, dword  clno)             /*__fatfn__*/
{
#if (INCLUDE_FAT32)
            if (pdr->drive_info.fasize == 8)
			{
			dword term32;
#if (INCLUDE_EXFAT) /* FAT64 does not use cluster chains */
				if (ISEXFAT(pdr))
					term32 = (dword)0xffffffff;
				else
#endif
					term32 = (dword)0x0fffffff;
                return(fatop_pfaxx(pdr, clno, term32));
			}
            else
#endif
                return(fatop_pfaxx(pdr, clno, 0xffff));

}
/******************************************************************************
    fatop_pfaxx - Write a value to the FAT at clno.

 Description
    Given a DDRIVE,cluster number and value. Write the value in the fat
    at clusterno. Handle 16 and 12 bit fats correctly.

 Returns
    No if an io error occurred during fat swapping, else TRUE.

*****************************************************************************/

/* Given a clno & fatval Put the value in the table at the index (clno)    */
/* Note: The caller locks the fat before calling this routine              */
BOOLEAN fatop_pfaxx(DDRIVE *pdr, dword  clno, dword  value)            /*__fatfn__*/
{
    union align1 {
        byte    wrdbuf[4];          /* Temp storage area */
        word  fill[2];
    } u;
    dword nibble,index,offset, t;

    if ((clno < 2) || (clno > pdr->drive_info.maxfindex) )
    {
        rtfs_set_errno(PEINVALIDCLUSTER, __FILE__, __LINE__);   /* fatop_pfaxx: bad cluster value internal error */
        return (FALSE);
    }
    set_fat_dirty(pdr);
    if (pdr->drive_info.fasize == 3)       /* 3 nibble ? */
    {
        nibble = (dword)(clno * 3);
        index  = (dword)(nibble >> 2);
        offset = (dword)(clno & 0x03);
        /* Read the first word   */
        if (!fatxx_fword( pdr, index, (word *) &u.wrdbuf[0], FALSE ))
            return(FALSE);
/*
        |   W0      |   W1      |   W2  |
        A1 A0 B0 A2 B2 B1 C1 C0 D0 C2 D2 D1
        xx xx   xx
*/
        if (offset == 0) /* (A2 << 8) | A1 A2 */
        {
            /* Low nibble of b[1] is hi nibble of value   */
            u.wrdbuf[1] &= 0xf0;
            t = (dword)((value >> 8) & 0x0f);
            u.wrdbuf[1] |= (byte) t;
            /* b[0] is lo byte of value   */
            t = (dword)(value & 0x00ff);
            u.wrdbuf[0] = (byte) t;
            if (!fatxx_fword( pdr, index, (word *) &u.wrdbuf[0], TRUE ))
                return (FALSE);
        }
/*
        |   W0      |   W1      |   W2  |
        A1 A0 B0 A2 B2 B1 C1 C0 D0 C2 D2 D1
                xx  xx xx
*/

        else if (offset == 1) /* (B1 B2 << 4) | B0 */
        {
            /* Hi nibble of b[1] is lo nibble of value   */
            u.wrdbuf[1] &= 0x0f;
            t = (dword)((value << 4) & 0x00f0);
            u.wrdbuf[1] |= (byte)t;
            if (!fatxx_fword( pdr, index, (word *) &u.wrdbuf[0], TRUE ))
                return (FALSE);
            /*  b[0] is hi byte of value   */
            if (!fatxx_fword( pdr, (word)(index+1), (word *) &u.wrdbuf[0], FALSE ))
                return(FALSE);
            t = (dword)((value >> 4) & 0x00ff);
            u.wrdbuf[0] = (byte) t;
            if (!fatxx_fword( pdr, (word)(index+1), (word *) &u.wrdbuf[0], TRUE ))
                return (FALSE);
        }
/*
        |   W0      |   W1  |   W2  |
        A1 A0 B0 A2 B2 B1 C1 C0 D0 C2 D2 D1
                            xx xx   xx
*/

        else if (offset == 2) /*(C2 << 8) | C1 C2 */
        {
            /* b[1] = low byte of value   */
            t = (dword)(value & 0x00ff);
            u.wrdbuf[1] = (byte) t;
            if (!fatxx_fword( pdr, index, (word *) &u.wrdbuf[0], TRUE ))
                return (FALSE);
            /* lo nibble of b[0] == hi nibble of value   */
            if (!fatxx_fword( pdr, (word)(index+1), (word *) &u.wrdbuf[0], FALSE ))
                return(FALSE);
            u.wrdbuf[0] &= 0xf0;
            t = (dword)((value >> 8) & 0x0f);
            u.wrdbuf[0] |= (byte) t;
            if (!fatxx_fword( pdr, (word)(index+1), (word *) &u.wrdbuf[0], TRUE ))
                return (FALSE);
        }
/*
        |   W0      |   W1  |   W2  |
        A1 A0 B0 A2 B2 B1 C1 C0 D0 C2 D2 D1
                                xx  xx xx
*/

        else if (offset == 3) /* (D2 D1) << 4 | D0 */
        {
            /* Hi nibble b[0] == low nible of value    */
            u.wrdbuf[0] &= 0x0f;
            t = (dword)((value << 4) & 0x00f0);
            u.wrdbuf[0] |= (byte) t;
            t = (dword)((value >> 4) & 0x00ff);
            u.wrdbuf[1] = (byte) t;
            if (!fatxx_fword( pdr, index, (word *) &u.wrdbuf[0], TRUE ))
                return (FALSE);
        }
    }
#if (INCLUDE_FAT32) /* FAT32 only supported if FAT swapping is enabled */
    else if (pdr->drive_info.fasize == 8)
    {
        fr_DWORD(&u.wrdbuf[0],value);          /*X*/
     /* Now put the values back into the FAT   */
        if (!fatxx_pfpdword( pdr, clno, (dword *) &u.wrdbuf[0] ))
        {
            return (FALSE);
        }
    }
#endif /* INCLUDE_FAT32 */
    else        /* 16 BIT entries */
    {
        fr_WORD((byte *) &u.wrdbuf[0],(word)value);         /*X*/
        /* Now put the values back into the FAT   */
        if (!fatxx_fword( pdr, clno, (word *) &u.wrdbuf[0], TRUE ))
        {
            return (FALSE);
        }
    }
    return (TRUE);
}




#if (INCLUDE_FAT32)

/* Put a DWORD value into the fat at index                */
static BOOLEAN fatxx_pfpdword(DDRIVE *pdr, dword index, dword *pvalue)          /*__fatfn__*/
{
    dword  *ppage;
    dword offset;
    /* Make sure we have access to the page. Mark it for writing   */
    ppage = (dword  *)fatxx_pfswap(pdr,index,TRUE);

    if (!ppage)
        return(FALSE);
    else
    {
        /* there are 128 * blocksize entries per page   */
        offset = (dword) (index & pdr->drive_info.cl32maskblock);
        ppage[(int)offset] = *pvalue;
    }
    return(TRUE);
}
#endif


#if (INCLUDE_FATPAGE_DRIVER) /* Page oriented FAT16 or FAT 32 routines */

/***************************************************************************
    fatop_page_link_frag()  -  Link or zero a contiguous group of clusters

    Description
        Starting at startpt link the contiguous clusters into
        a terminated chain.

        If do_zero is TRUE Zero the clusters instead of linking them
        If do_zero is TRUE and prev_cluster is non zero
            terminate the previous list
        If do_zero is FALSE and prev_cluster is non zero
            link the previous list to start_point
        If do_zero is FALSE and next_cluster is non zero
            link this chain to next_cluster
 Returns
    Returns TRUE on success, false on an IO error.
****************************************************************************/

/*
Called by:
    fatop_alloc_file_chain  - when emulating rtfspro po_write
    fatop_link_frag
        called by:
            fatop_free_frag()
            static void test_alloc_fragment(dword start_contig, dword n_contig)
            static void test_free_fragment(dword start_contig, dword n_contig)
            pc_frag_chain_delete_or_link
            pc_region_alloc_frag
*/


static BOOLEAN fatop_page_link_frag(DDRIVE *pdr, byte *palt_buffer, dword alt_buffer_size, dword flags, dword link_cluster, dword startpt, dword n_contig)
{
dword mask_offset, mask_cl_page, cl_per_block, cl_to_page_shift,bytes_per_entry;
dword term_cluster,current_cluster, n_clusters_left;
dword cluster_offset_in_page, byte_offset_in_page, first_block_offset_to_map;
byte *mapped_data;
BOOLEAN do_term;
dword term32;
#if (INCLUDE_EXFAT)/* FAT64 does not use cluster chains */
   if (ISEXFAT(pdr))
      term32 = (dword)0xffffffff;
   else
#endif
      term32 = (dword)0x0fffffff;

// FOP_TERM_PREV is not beign used - should eliminate.. simplify
    /* Link the previous chain end to the new fragment or terminate the previous chain if requested */
    if (link_cluster && (flags & (FOP_LINK_PREV|FOP_TERM_PREV)))
    {
        term_cluster = term32; /* link_cluster is in a previous chain terminate this chain  */
        if (startpt == 0 || flags & FOP_TERM_PREV)
        {
            if (!fatop_pfaxxterm(pdr, link_cluster))
                goto map_error;
        }
        else
        {
            if (!fatop_pfaxx(pdr, link_cluster, startpt))
                goto map_error;
        }
    }
    else
    {
        if (link_cluster && (flags & FOP_LINK_NEXT))
            term_cluster = link_cluster;  /* link_cluster is in a subsequant fragment so link to it */
        else
            term_cluster = term32;
    }
    /* Get masks based on cluster size */
    fatop_get_page_masks(pdr, &mask_offset, &mask_cl_page,  &cl_per_block, &cl_to_page_shift, &bytes_per_entry);
    /* Get starting cluster, offsets, blocks etc */
    cluster_offset_in_page = startpt&mask_offset;
    byte_offset_in_page = cluster_offset_in_page*bytes_per_entry;
    first_block_offset_to_map  = startpt>>cl_to_page_shift;
    current_cluster = startpt;  /* If linking this value is incremented n_contig times and placed in the buffer */

    do_term = FALSE;
    n_clusters_left = n_contig;

    while (n_clusters_left)
    {
        byte *pb;
        dword i, num_to_link, mapped_block_count;

        /* get a pointer to first_block_offset_to_map and the number of blocks in the buffer
           Only attempts to map up to n_clusters_left*bytes_per_entry.
           If palt_buffer is non zeor it will return palt_buffer if the clusters are aligned for
           efficient streaming and the clusters are not already cached. Otherwise not aligned or
           palt_buffer is NULL it uses the fat buffer pool. The last arg tells pc_map_fat_stream()
           not to read the block into the buffer pool if our write will span the whole block */
        mapped_data = pc_map_fat_stream(pdr, &mapped_block_count, first_block_offset_to_map, byte_offset_in_page,
                               n_clusters_left*bytes_per_entry, palt_buffer, alt_buffer_size, FALSE);
        if (!mapped_data)   /* Fat buffer failed reading or swapping, errno set below */
            goto map_error;
        /* If pc_map_fat_stream() returned palt_buffer we know the next "mapped_block_count" blocks
           are uncached and we will fill them all, so no need to read */
        pb = mapped_data;   /* Save mapped_data pointer for block oriented access, set up pb for pointer access */

        /* Link as many as we can starting from cluster_offset_in_page (always zero after first iteration) */
        num_to_link     = mapped_block_count<<cl_to_page_shift;
        if (cluster_offset_in_page)
            num_to_link  -= cluster_offset_in_page;

        if (num_to_link >= n_clusters_left)
        { /* This is the last pass */
            if (flags & FOP_LINK)
            {
                num_to_link = n_clusters_left -1; /* Set up for link next or link */
                do_term = TRUE;
            }
            else
                num_to_link = n_clusters_left;    /* Just zero n_clusters_left */
            n_clusters_left = 0;
        }
        else
        {
            n_clusters_left -= num_to_link;
        }
        /* offset the pointer by the initial offset.. only on first pass, subsequant
           passes are block aligned */
        if (byte_offset_in_page)
        {
            pb += byte_offset_in_page;
            byte_offset_in_page = 0;        /* next time it will be on a boundary */
            cluster_offset_in_page  = 0;
        }
        if (!(flags & FOP_LINK))
        {
            rtfs_memset(pb, 0, num_to_link*bytes_per_entry);
        }
        else if (pdr->drive_info.fasize == 8)
        {
        dword *pdw = (dword *) pb;
            for (i = 0; i < num_to_link; i++)
            {
                current_cluster += 1;
                fr_DWORD((byte *) pdw,(dword)current_cluster);
                pdw += 1;
            }
            if (do_term)
            {
                fr_DWORD((byte *) pdw,(dword)term_cluster);
            }
        }
        else /* if (pdr->fasize == 4) */
        {
        word *pw = (word *) pb;
            for (i = 0; i < num_to_link; i++)
            {
                current_cluster += 1;
                fr_WORD((byte *) pw,(word)current_cluster);
                pw++;
            }
            if (do_term)
            {
                fr_WORD((byte *) pw,(word)term_cluster);
            }
        }
        if (mapped_data == palt_buffer) /* Manual flush if using a local buffer */
        {
            if (!pc_write_fat_blocks(pdr,pdr->drive_info.fatblock+first_block_offset_to_map, mapped_block_count, mapped_data, 0x03))
                goto map_error;
        }
        first_block_offset_to_map += mapped_block_count;   /* next block we want */
    }
    return(TRUE);
map_error:
    return(FALSE);
}


/***************************************************************************
 fatop_page_find_contiguous_free_clusters()  -
 Description
 Find the first zero valued cluster in a range, then count the number of contiguous free
 clusters from there to the end of the range
 Returns
        Returns # free up to range size.
        The count of the number of contiguous zero valued clusters is returned through *p_contig.
        If an error occurs *is_error is set to 1 and ERRNO is set.
****************************************************************************/

/* fatop_page_find_contiguous_free_clusters() is not necessary if INCLUDE_RTFS_FREEMANAGER
   is included because free clusters are always in memory

   Note: This routine is not used by rtfsproplus and is only used by rtfspro
   when INCLUDE_RTFS_FREEMANAGER is zero. It scans the FAT for free clusters
   using the FAT buffer pool, it is not optimized for userbuffer sized scans
   of the FAT.

   */
static dword fatop_page_find_contiguous_free_clusters(DDRIVE *pdr,
                                            dword startpt,
                                            dword endpt,
                                            dword min_clusters,
                                            dword max_clusters,
                                            dword *p_contig,
                                            int *is_error)
{
dword mask_offset, mask_cl_page, cl_per_block, cl_to_page_shift,bytes_per_entry;
dword i,n_clusters_left;
dword mapped_block_count,cluster_offset_in_page, byte_offset_in_page, first_block_offset_to_map;
byte *mapped_data, *palt_buffer, *pb;
dword current_cluster, num_this_page,alt_buffer_size;
dword start_contig, n_contig;

    if (endpt < startpt)
        return(0);
	ERTFS_ASSERT(!ISEXFATORFAT64(pdr)) /* exFAT/FAT64 should not call this  */

    n_clusters_left = endpt - startpt + 1;
    if (n_clusters_left < max_clusters)
        max_clusters = n_clusters_left;
    if (max_clusters < min_clusters)
        return(0);

    /* Get masks based on cluster size */
    fatop_get_page_masks(pdr, &mask_offset, &mask_cl_page,  &cl_per_block, &cl_to_page_shift, &bytes_per_entry);
    /* Get starting cluster, offsets, blocks etc */
    cluster_offset_in_page = startpt&mask_offset;
    byte_offset_in_page = cluster_offset_in_page*bytes_per_entry;
    first_block_offset_to_map  = startpt>>cl_to_page_shift;
    current_cluster = startpt;

    start_contig = 0;
    n_contig = 0;

    while (n_clusters_left)
    {
        /* get a pointer to first_block_offset_to_map and the number of blocks in the buffer
           Only atteempts to map up to n_clusters_left*bytes_per_entry.
           will return the optional buffer passed in in palt_buffer
           if the clusters are aligned for efficient streaming and the clusters are not already
           cached. Otherwise uses the fat buffer pool

        */
        palt_buffer = 0;
        alt_buffer_size = 0;
        mapped_data = pc_map_fat_stream(pdr, &mapped_block_count, first_block_offset_to_map, byte_offset_in_page,
                               n_clusters_left*bytes_per_entry, palt_buffer, alt_buffer_size, TRUE);
        if (!mapped_data)   /* Error accessing the fat buffer (io error during read ?) errno was set below.. */
            goto map_error;

        first_block_offset_to_map += mapped_block_count;   /* the next block we will want */
        pb = mapped_data;   /* set up pb for pointer access */
        /* test as many as we can starting from cluster_offset_in_page (always zero after first iteration) */
        num_this_page = mapped_block_count<<cl_to_page_shift;
        if (cluster_offset_in_page)
        {
            num_this_page -= cluster_offset_in_page;
            pb += byte_offset_in_page;   /* set up pb for pointer access */
            byte_offset_in_page = 0;        /* only first iteration may not be on a boundary */
            cluster_offset_in_page  = 0;
        }
        if (num_this_page > n_clusters_left)
          num_this_page = n_clusters_left;

        if (pdr->drive_info.fasize == 8)
        {
        dword *pdw;
            pdw = (dword *) pb;
            for (i = 0; i < num_this_page; i++,current_cluster++,pdw++)
            {
                if (*pdw == 0)
                {
                    if (!n_contig)
                        start_contig = current_cluster;
                    n_contig += 1;
                    if (n_contig == max_clusters)
                        goto done;
                }
                else
                {
                    if (n_contig >= min_clusters)
                        goto done;
                    n_contig = 0;
                }
            }
        }
        else
        {
            word *pw;
            pw = (word *) pb;
            for (i = 0; i < num_this_page; i++,current_cluster++,pw++)
            {
                if (*pw == 0)
                {
                    if (!n_contig)
                        start_contig = current_cluster;
                    n_contig += 1;
                    if (n_contig == max_clusters)
                        goto done;
                }
                else
                {
                    if (n_contig >= min_clusters)
                        goto done;
                    n_contig = 0;
                }
            }
        }
        n_clusters_left -= num_this_page;
    }
    if (n_contig < min_clusters)
        return(0);
done:
    *p_contig = n_contig;
    return(start_contig);
map_error:
    *is_error = 1;
    return(0);
}


#endif /*  (INCLUDE_FATPAGE_DRIVER) */
#endif /* Exclude from build if read only */
