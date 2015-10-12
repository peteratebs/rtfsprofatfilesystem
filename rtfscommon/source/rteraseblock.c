/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2008
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* RTERASEBLOCK.C - Erase block aware functions, shared by Pro and Pro Plus.
*/

#include "rtfs.h"
#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */

#if (INCLUDE_NAND_DRIVER)  /* rteraseblock.c source file is needed only for dynamic mode */

dword get_clusters_per_erase_block(DDRIVE *pdr);
static BOOLEAN erase_block_if_free(DDRIVE *pdr, dword eb_start_cluster,dword clusters_per_erase_block, dword erase_block_size_sectors);
static void get_erase_block_boundaries(DDRIVE *pdr, dword cluster, dword *pstart, dword *pend);
static BOOLEAN erase_logical_sectors(DDRIVE *pdr, dword start_sector, dword n_sectors);

/* BOOLEAN eraseop_erase_blocks(DDRIVE *pdr, BOOLEAN is_file, dword start_cluster, dword n_clusters)

   Inputs: dword cluster, dword ncontig - cluster range to erase
   Outputs -
            Returns TRUE if no errors

   Erase the erase blocks containing the range start_cluster to (start_cluster + n_clusters - 1).
   If the start or end clusters are not erase block bound, only erase the block if all other clusters
   in the erase block are also free.

   If the drive does no have erase blocks then simply return success

*/

BOOLEAN eraseop_erase_blocks(DDRIVE *pdr, dword cluster, dword ncontig)
{
    if (pdr && pdr->pmedia_info->eraseblock_size_sectors)
    {
       dword erase_block_size_sectors, cluster_size_sectors, first_eb_cluster, last_eb_cluster;

       cluster_size_sectors = (dword) (pdr->drive_info.secpalloc); /* byte to dword */

       erase_block_size_sectors = pdr->pmedia_info->eraseblock_size_sectors;

       if (!cluster_size_sectors || !erase_block_size_sectors) /* Defensive won't happen */
           return(TRUE);   /* Something is wrong, but return TRUE, if driver is down it will be caught again */
       else if (cluster_size_sectors >= erase_block_size_sectors)
       { /* Clusters >= erase block size. They must be erase block bound */
           if (!erase_logical_sectors(pdr, pc_cl2sector(pdr, cluster), ncontig << pdr->drive_info.log2_secpalloc) )
               goto return_error;
       }
       else
       { /* erase block contains multiple clusters */
       dword n_left, clusters_per_erase_block;

           clusters_per_erase_block = get_clusters_per_erase_block(pdr);
           if (!clusters_per_erase_block)
               goto return_error;        /* Defensive, won't happen */

           n_left = ncontig;
           while (n_left)
           {
               /* Get the boundaries of the current erase block */
               get_erase_block_boundaries(pdr, cluster, &first_eb_cluster, &last_eb_cluster);
               if (cluster != first_eb_cluster || n_left < clusters_per_erase_block)
               {   /* If not spanning a whole erase block, call partial erase block routine to erase it only the whole block is free */
                   dword n_used;
                   if (!erase_block_if_free(pdr, first_eb_cluster, clusters_per_erase_block, erase_block_size_sectors))
                       goto return_error;
                   /* Subtract the number of cluster to the end of the erase block from n_left, truncate if we overshoot */
                   n_used = last_eb_cluster - cluster + 1;
                   if (n_left > n_used)
                   {
                       n_left -= n_used;
                       cluster = last_eb_cluster + 1;
                   }
                   else
                       n_left = 0;
               }
               else
               { /* If spanning a whole erase block, call full erase block routine to erase it */
               		if (!erase_logical_sectors(pdr, pc_cl2sector(pdr, first_eb_cluster), erase_block_size_sectors))
                       goto return_error;
                   /* Subtract clusters_per_erase_block from n_left, will not overshoot */
                   n_left -= clusters_per_erase_block;
                   cluster = first_eb_cluster + clusters_per_erase_block;
               }
           }
       }
    }
    return(TRUE);
return_error:
    return(FALSE);
}

#define EB_PARTIAL           1
#define EB_EMPTY             2
#define EB_PARTIALOREMPTY    3
#define EB_FULL              4
#define EB_COMPLETE          5

/* Search for cluster(s) in freespace without regard to  allocation_scheme */
static dword _find_free_clusters(DDRIVE *pdr, dword start_point, dword last_eb_cluster, dword min_clusters, dword max_clusters, dword *p_contig, int *is_error)
{
	*p_contig = 0;		/* Bug Fix July 2010. Clear the n_contig value (upper level was consulting it even with return value indicating none found */
#if (INCLUDE_RTFS_FREEMANAGER)
    /* Search ram based free manager for clusters to allocate.. if the ram based manager is shut down
    free_manager_find_contiguous_free_clusters() will perform FAT scans for free clusters but exclude
    free clusters that are reserved by Failsafe */
    return(free_manager_find_contiguous_free_clusters(pdr, start_point, last_eb_cluster, min_clusters, max_clusters, p_contig, is_error));
#else
    /* Call FAT scan routine */
    return(_fatop_find_contiguous_free_clusters(pdr, start_point, last_eb_cluster, min_clusters, max_clusters, p_contig, is_error));
#endif
}


/* Search for cluster(s) in freespace taking into consideration the allocation_scheme
    if (allocation_scheme == ALLOC_CLUSTERS_ALIGNED) - Allocate for fastest writing
        Take clusters from empty erase blocks first, use partial as a last resort
    if (allocation_scheme == ALLOC_CLUSTERS_UNALIGNED)  - Allocate for garbage collecting partially used erase blocks
        Take clusters from partially empty erase blocks first, use empties as a last resort
    otherwise
       Take the first cluster(s) that fulfill the request (traditional algorithm)

       This routine is only called if the drive has erase blocks
*/

dword eraseop_find_free_clusters(DDRIVE *pdr, dword startpt, dword endpt, dword min_clusters, dword max_clusters, int allocation_scheme, dword *p_contig, int *is_error)
{
dword clusters_per_erase_block;
dword current_start_point, first_free_eb_cluster, first_eb_cluster, last_eb_cluster, num_free_cluster_in_eb;
dword cur_empty_contiguous_clusters, cur_empty_start_cluster;
dword max_empty_contiguous_clusters, max_empty_start_cluster;
dword cur_partial_contiguous_clusters, cur_partial_start_cluster;
dword max_partial_contiguous_clusters, max_partial_start_cluster;
int do_close_on, do_process_on, do_return_processing;
int search_precedence;
dword ret_val = 0;

    /* Intialize return values and scan values */
    clusters_per_erase_block = get_clusters_per_erase_block(pdr);
    *p_contig = 0;
    cur_empty_contiguous_clusters = cur_empty_start_cluster = 0;
    max_empty_contiguous_clusters = max_empty_start_cluster = 0;
    cur_partial_contiguous_clusters = cur_partial_start_cluster = 0;
    max_partial_contiguous_clusters = max_partial_start_cluster = 0;

    /* Determine the search algorithm */
    if (allocation_scheme == ALLOC_CLUSTERS_ALIGNED)
        search_precedence = EB_EMPTY;  /* Take clusters from empty erase blocks first, use partial as a last resort */
    else if (allocation_scheme == ALLOC_CLUSTERS_UNALIGNED)
        search_precedence = EB_PARTIAL; /* Take clusters from partially empty erase blocks first, use empties as a last resort */
    else
        search_precedence = EB_PARTIALOREMPTY;  /* Take the first cluster(s) that fulfill the request */

    /* Intialize state values */
    do_return_processing =  do_close_on = do_process_on = 0;

    /* Get the first and last clusters in the current erase block */
    get_erase_block_boundaries(pdr, startpt, &first_eb_cluster, &last_eb_cluster);
	/* Start search algorithm at the beginning of the erase block */
    current_start_point = first_eb_cluster;
    /* Start at current_start_point and retrieve the first free group of clusters return up to the number of clusters left in the current erase block.
        retrieve:
          num_free_cluster_in_eb - Number of free clusters found between current_start_point and the end of the erase block.
          first_free_eb_cluster  - First free cluster between current_start_point and the end of the erase block or zero

    */
    /* The way we use the memory manager we report multiple PENOSPC space errors in debug mode so clear them */

    first_free_eb_cluster = _find_free_clusters(pdr, current_start_point, last_eb_cluster, 1, (last_eb_cluster - current_start_point + 1), &num_free_cluster_in_eb, is_error);
    if (!first_free_eb_cluster)
        num_free_cluster_in_eb = 0;
    if (*is_error)
        goto restore_errno_and_exit;
    for(;;)
    {
        dword last_current_start_point;
        last_current_start_point = current_start_point; /* Defensive, test endless loops */
        /* First test if our last scan finished and we have to process close and return */
        do_close_on = 0;
        if (do_process_on == EB_COMPLETE)
        { /* If we finished scan, check close processing and exit */
            if (cur_partial_contiguous_clusters)
                do_close_on = EB_PARTIAL;
            else if (cur_empty_contiguous_clusters)
                do_close_on = EB_EMPTY;
           /* After close processing we will break out */
        }
        /* Decide what to do with free clusters in this erase blocs "num_free_cluster_in_eb" */
        else if (num_free_cluster_in_eb == clusters_per_erase_block)
        { /* current_start_point is on a boundary and all clusters in erase blocks are free */
            if (cur_partial_contiguous_clusters)
                do_close_on = EB_PARTIAL;
            do_process_on = EB_EMPTY;
        }
        else if (num_free_cluster_in_eb == 0)
        { /* All clusters in erase blocks are already allocated  close any free segments and skip to the next */
            if (cur_partial_contiguous_clusters)
                do_close_on = EB_PARTIAL;
            else if (cur_empty_contiguous_clusters)
                do_close_on = EB_EMPTY;
            do_process_on = EB_FULL;
        }
        else
        { /* Only some clusters in erase blocks are free */
            if (cur_empty_contiguous_clusters)
                do_close_on = EB_EMPTY;  /* close segments from empty erase blocks */
            else
            {
                if (cur_partial_contiguous_clusters && first_free_eb_cluster != first_eb_cluster)
                    do_close_on = EB_PARTIAL; /* close segments from partial erase block because we are not adjacent */
            }
            do_process_on = EB_PARTIAL;
        }

        /* Process strings that just closed if there are any */
        if (do_close_on == EB_EMPTY)
        {   /* If we hit a new maximum process it */
            if (cur_empty_contiguous_clusters > max_empty_contiguous_clusters)
            {
                max_empty_contiguous_clusters = cur_empty_contiguous_clusters;
                max_empty_start_cluster = cur_empty_start_cluster;
                /* Return the string if our new max is >= the minimum required and we are looking for empty blocks */
                if (max_empty_contiguous_clusters >= min_clusters)
                {
                    if (search_precedence == EB_EMPTY || search_precedence == EB_PARTIALOREMPTY)
                    {
                        do_return_processing = EB_EMPTY;
                        break;
                    }
                }
            }
            cur_empty_contiguous_clusters = 0;  /* Close so we start a new tally next time we process an empty block */
        }
        else if (do_close_on == EB_PARTIAL)
        {   /* If we hit a new maximum process it */
            if (cur_partial_contiguous_clusters > max_partial_contiguous_clusters)
            {
                max_partial_contiguous_clusters = cur_partial_contiguous_clusters;
                max_partial_start_cluster = cur_partial_start_cluster;
                /* Return the string if our new max is >= the minimum required. and we are looking for partial blocks */
                if (max_partial_contiguous_clusters >= min_clusters)
                {
                    if (search_precedence == EB_PARTIAL || search_precedence == EB_PARTIALOREMPTY)
                    {
                        do_return_processing = EB_PARTIAL;
                        break;
                    }
                }
            }
            cur_partial_contiguous_clusters = 0;    /* Close so we start a new tally next time we process a partial block */
        }

        /* Process the values we placed in first_free_eb_cluster and num_free_cluster_in_eb
           and advance start pointer and if necessary the erase block    */
        if (do_process_on == EB_COMPLETE)
        {
            break;
        }
        else if (do_process_on == EB_EMPTY)
        {
            if (!cur_empty_contiguous_clusters)
                cur_empty_start_cluster = first_free_eb_cluster;
            cur_empty_contiguous_clusters += num_free_cluster_in_eb;
            /* If we hit a new maximum process it if we haven't hit the outer maximum already */
            if (cur_empty_contiguous_clusters > max_empty_contiguous_clusters &&
                max_empty_contiguous_clusters < max_clusters)
            {
                /* If the new maximum is as large as we need, process possible imediate return */
                max_empty_contiguous_clusters = cur_empty_contiguous_clusters;
                max_empty_start_cluster = cur_empty_start_cluster;
                if (max_empty_contiguous_clusters >= max_clusters)
                {
                    if (search_precedence == EB_EMPTY || search_precedence == EB_PARTIALOREMPTY)
                    {
                        do_return_processing = EB_EMPTY;
                        break;
                    }
                }
            }
            /* Advance pointers by a full erase block */
            first_eb_cluster += clusters_per_erase_block;
            last_eb_cluster += clusters_per_erase_block;
            current_start_point += clusters_per_erase_block;
        }
        else if (do_process_on == EB_PARTIAL)
        {
            if (!cur_partial_contiguous_clusters)
                cur_partial_start_cluster = first_free_eb_cluster;
            cur_partial_contiguous_clusters += num_free_cluster_in_eb;
            /* If we hit a new maximum process it if we haven't hit the outer maximum already */
            if (cur_partial_contiguous_clusters > max_partial_contiguous_clusters &&
                max_partial_contiguous_clusters < max_clusters)
            {
                /* If the new maximum is as large as we need, process possible imediate return */
                max_partial_contiguous_clusters = cur_partial_contiguous_clusters;
                max_partial_start_cluster = cur_partial_start_cluster;
                if (max_partial_contiguous_clusters >= max_clusters)
                {
                    if (search_precedence == EB_PARTIAL || search_precedence == EB_PARTIALOREMPTY)
                    {
                        do_return_processing = EB_PARTIAL;
                        break;
                    }
                }
            }
            /* Advance by the number we found (can not be zero)*/
            current_start_point += num_free_cluster_in_eb;
            /* Advance the erase block if we advanced */
            if (current_start_point > last_eb_cluster)
            {
                first_eb_cluster += clusters_per_erase_block;
                last_eb_cluster += clusters_per_erase_block;
            }
        }
        else if (do_process_on == EB_FULL)
        {  /* Erase block was empty, advance pointers by a full erase block */
            first_eb_cluster += clusters_per_erase_block;
            last_eb_cluster += clusters_per_erase_block;
            current_start_point += clusters_per_erase_block;
        }

        /*  get first_free_eb_cluster and num_free_cluster_in_eb from the next erase block */
        if (current_start_point > endpt)
        {   /* Past the end EB_COMPLETE state in next loop will allow close processing and then break out */
            do_process_on = EB_COMPLETE;
        }
        /* Check if this erase block is the last one in our range */
        else if (endpt <= last_eb_cluster)
        {
            /* Only find up to (endpt-current_start_point+1) free clusters */
            first_free_eb_cluster = _find_free_clusters(pdr, current_start_point, last_eb_cluster, 1, (endpt-current_start_point+1), &num_free_cluster_in_eb, is_error);
            if (*is_error)
                goto restore_errno_and_exit;
            do_process_on = 0;
        }
        else
        {
            /* Find up to clusters_per_erase_block free clusters */
            first_free_eb_cluster = _find_free_clusters(pdr, current_start_point, last_eb_cluster, 1, clusters_per_erase_block, &num_free_cluster_in_eb, is_error);
            if (*is_error)
                goto restore_errno_and_exit;
            do_process_on = 0;
        }

        if (current_start_point <= last_current_start_point) /* Defensive, test endless loops */
        {
            *is_error = 1;
            rtfs_set_errno(PEINTERNAL, __FILE__, __LINE__);
            ERTFS_ASSERT(0)
            goto restore_errno_and_exit;
        }
    }

    /* Process returns */
    {
    dword n_contig;
        if (do_return_processing == EB_PARTIAL)
        {
            n_contig = max_partial_contiguous_clusters;
            ret_val = max_partial_start_cluster;
        }
        else if (do_return_processing == EB_EMPTY)
        {
            n_contig = max_empty_contiguous_clusters;
            ret_val = max_empty_start_cluster;
        }
        else
        {
            n_contig = 0;
            ret_val = 0;
        }
       /* Truncate contiguous_clusters if > max_clusters */
        if (max_clusters && n_contig > max_clusters)
            n_contig = max_clusters;
        *p_contig = n_contig;
   }
restore_errno_and_exit:
  if (*is_error == 0 && ret_val)
    rtfs_clear_errno(); /* clear error status */
  return(ret_val);
}

dword get_clusters_per_erase_block(DDRIVE *pdr)
{
dword erase_block_size_sectors, cluster_size_sectors;
    cluster_size_sectors = (dword) (pdr->drive_info.secpalloc); /* byte to dword */
    erase_block_size_sectors = pdr->pmedia_info->eraseblock_size_sectors;
    if (!cluster_size_sectors || !erase_block_size_sectors)      /* Defensive, won't happen */
        return(1);
    return(erase_block_size_sectors / cluster_size_sectors);  /* cluster_size_sectors is not zero */
}

/* erase an erase block if all clusters in it are free if  pdr->pmedia_info->device_erase is unavailable return success*/
static BOOLEAN erase_block_if_free(DDRIVE *pdr, dword eb_start_cluster,dword clusters_per_erase_block, dword erase_block_size_sectors)
{
    dword n_free, found_cluster;
    int is_error;
    is_error = 0;

    if (!pdr->pmedia_info->device_erase)
        return(TRUE);

    n_free = _find_free_clusters(pdr, eb_start_cluster, eb_start_cluster + clusters_per_erase_block-1,
                                        clusters_per_erase_block, clusters_per_erase_block, &found_cluster, &is_error);

    if (is_error)
        return(FALSE);

    if (n_free >= clusters_per_erase_block)
    {
   		if (!erase_logical_sectors(pdr, pc_cl2sector(pdr, eb_start_cluster), erase_block_size_sectors))
            return(FALSE);
    }
    return(TRUE);
}

static BOOLEAN erase_logical_sectors(DDRIVE *pdr, dword start_sector, dword n_sectors)
{
    /* Removed test of if (pdr->drive_flags & DRIVE_FLAGS_PARTITIONED). partition_base will be zero if the device is not partitioned */
   	start_sector += pdr->drive_info.partition_base;
    if (!pdr->pmedia_info->device_erase ||
    	!pdr->pmedia_info->device_erase(pdr->pmedia_info->devhandle, (void *) pdr, start_sector, n_sectors) )
		return(FALSE);
	return(TRUE);
}


/* return start and end clusters of the erase block containing "cluster" */
static void get_erase_block_boundaries(DDRIVE *pdr, dword cluster, dword *pstart, dword *pend)
{
    dword erase_block_size_sectors, cluster_size_sectors;


    cluster_size_sectors = (dword) (pdr->drive_info.secpalloc); /* byte to dword */
    erase_block_size_sectors = pdr->pmedia_info->eraseblock_size_sectors;

    *pstart = *pend = cluster;
    if (!erase_block_size_sectors)  /* no erase blocks */
        return;
    if (!cluster_size_sectors)      /* Defensive, won't happen */
        return;
    if (cluster_size_sectors >= erase_block_size_sectors)   /* The cluster overlaps erase blocks, so it is on a boundary */
        return;
    else
    { /* The cluster size is less than the erase block size (but the erase block size is a multiple of the cluster size) */
        dword clusters_per_eb, cluster_offsetmask,cluster_basemask;
        clusters_per_eb = get_clusters_per_erase_block(pdr);
        cluster_offsetmask = clusters_per_eb-1;
        cluster_basemask  = ~cluster_offsetmask;
        { /* Normalize the cluster number sinece the first cluster actually has the index 2 */
            dword _cluster;
            /* Normalize the cluster number since the first cluster actually has the index 2 */
            _cluster = cluster - 2;
            /* And with the base mask to get the next lowest cluster on an erase block boundary */
            _cluster = _cluster & cluster_basemask;
            /* Add the 2 back in */
            *pstart = _cluster + 2;
            /* Add in (clusters_per_eb-1) to get the end */
            *pend = *pstart + clusters_per_eb - 1;
        }
    }
}

#endif /* (INCLUDE_NAND_DRIVER) */
#endif /* Exclude from build if read only */
