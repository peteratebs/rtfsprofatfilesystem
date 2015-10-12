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

int pc_efinode_async_load_continue(FINODE *pefinode)
{
DDRIVE *pdr;
REGION_FRAGMENT *pf;
dword  n_contig, this_cluster, next_cluster, user_buffer_size, max_clusters_per_pass;
byte *pubuff;
int    end_of_chain, ret_val;

    ret_val = PC_ASYNC_ERROR;

    pdr = pefinode->my_drive;
    pubuff = pc_claim_user_buffer(pdr, &user_buffer_size, 0); /* released at cleanup */

    if (pefinode->e.x->last_processed_cluster)
    { /* continue */
        if (!pefinode->e.x->clusters_to_process)
        {
            ret_val = PC_ASYNC_COMPLETE;
            goto release_and_exit;
        }
        this_cluster = pefinode->e.x->last_processed_cluster;
    }
    else
    { /* start */
#if (INCLUDE_EXFATORFAT64)
        pefinode->e.x->alloced_size_bytes.val64= 0;
#endif
        pefinode->e.x->alloced_size_bytes.val32= 0;
        pefinode->e.x->alloced_size_clusters = 0;
        pefinode->e.x->plast_fragment        = 0;
        pefinode->e.x->pfirst_fragment       = 0;
		if (!FINODESIZEISZERO(pefinode))
        {
            this_cluster = pc_finode_cluster(pdr, (FINODE *)pefinode);
            if (!this_cluster)
            {
                rtfs_set_errno(PEINVALIDCLUSTER, __FILE__, __LINE__);
                goto release_and_exit;
            }
#if (INCLUDE_EXFATORFAT64)
			if (ISEXFATORFAT64(pefinode->my_drive))
				pefinode->e.x->clusters_to_process =  pc_byte2clmod64(pdr, (dword)((pefinode->fsizeu.fsize64>>32)&0xffffffff), (dword)(pefinode->fsizeu.fsize64&0xffffffff));
			else
#endif
				pefinode->e.x->clusters_to_process =  pc_byte2clmod(pdr, pefinode->fsizeu.fsize);

        }
        else
        {
            ret_val = PC_ASYNC_COMPLETE;
            goto release_and_exit;
        }
    }

    end_of_chain = 0;
    /* Limit max to what is left - note:  fatop_get_frag_async() will make one read maximum */
    max_clusters_per_pass = pefinode->e.x->clusters_to_process;
#if (INCLUDE_EXFATORFAT64)
	/* exFatfatop_getfile_frag handles both FAT and EXFAT */
    n_contig=exFatfatop_getfile_frag(pefinode, this_cluster, &next_cluster, max_clusters_per_pass, &end_of_chain);
#else
    n_contig = fatop_get_frag_async(pdr, pubuff, user_buffer_size, this_cluster, &next_cluster, max_clusters_per_pass, &end_of_chain);
#endif
    if (!n_contig || (n_contig > pefinode->e.x->clusters_to_process))
    {
        ERTFS_ASSERT(rtfs_debug_zero())
        rtfs_set_errno(PEINVALIDCLUSTER, __FILE__, __LINE__);
        goto release_and_exit;
    }
    /* Keep alloced_size_clusters and alloced_size_bytes updated as we worlk because we could be doing
       a partial load of the cluster chain */
    pefinode->e.x->alloced_size_clusters += n_contig;
#if (INCLUDE_EXFATORFAT64)
	if (ISEXFATORFAT64(pefinode->my_drive))
		pefinode->e.x->alloced_size_bytes.val64 =  pc_alloced_bytes_from_clusters_64(pdr, pefinode->e.x->alloced_size_clusters);
	else
#endif
		pefinode->e.x->alloced_size_bytes.val32 =  pc_alloced_bytes_from_clusters(pdr, pefinode->e.x->alloced_size_clusters);

    pf = pefinode->e.x->plast_fragment;
    if (pf && (pf->end_location+1 == this_cluster))
    { /* contiguous so expand the current fragment */
        pf->end_location += n_contig;
    }
    else
    { /* not contiguous so grab a new fragment */
        /* Allocate a fragment. If none available recycle freelists. If we fail we we are out of luck */
        pf = pc_fraglist_frag_alloc(pdr, this_cluster, this_cluster+n_contig-1, 0);
        ERTFS_ASSERT(pf)
        if (!pf)
        {
            goto release_and_exit;
        }
        if (pefinode->e.x->plast_fragment)
            pefinode->e.x->plast_fragment->pnext = pf;
        else
            pefinode->e.x->pfirst_fragment = pf;
        pefinode->e.x->plast_fragment = pf;
    }
    ERTFS_ASSERT(n_contig <=  pefinode->e.x->clusters_to_process)
    if (n_contig >  pefinode->e.x->clusters_to_process)
    { /* Chain larger than file size  */
        rtfs_set_errno(PEINVALIDCLUSTER, __FILE__, __LINE__);
        goto release_and_exit;
    }
    else if (end_of_chain)
    {
        pefinode->e.x->last_processed_cluster = pf->end_location;
        pefinode->e.x->clusters_to_process -= n_contig;
        ERTFS_ASSERT(pefinode->e.x->clusters_to_process==0)
        if (pefinode->e.x->clusters_to_process)
        { /* End of chain but should be more */
            rtfs_set_errno(PEINVALIDCLUSTER, __FILE__, __LINE__);
            goto release_and_exit;
        }
        ret_val = PC_ASYNC_COMPLETE;
        goto release_and_exit;
    }
    else
    {
        pefinode->e.x->clusters_to_process -= n_contig;
        pefinode->e.x->last_processed_cluster = next_cluster;

        ERTFS_ASSERT(pefinode->e.x->clusters_to_process!=0)
        if (!pefinode->e.x->clusters_to_process)
        { /* No end of chain but should be done */
            rtfs_set_errno(PEINVALIDCLUSTER, __FILE__, __LINE__);
            goto release_and_exit;
        }
        ret_val = PC_ASYNC_CONTINUE;
        goto release_and_exit;
    }
release_and_exit:
    if (pubuff)
        pc_release_user_buffer(pdr, pubuff);

    if (ret_val == PC_ASYNC_ERROR)
    {
        if (pefinode->e.x->pfirst_fragment)
            pc_fraglist_free_list(pefinode->e.x->pfirst_fragment);
        pefinode->e.x->pfirst_fragment = 0;
    }

    return(ret_val);
}

/*  Called to synchronously load fragment chains of a file */
/*    pefinode->alloced_size_bytes    = 0; */
/*    pefinode->alloced_size_clusters = 0; */
/*    pefinode->e.x->plast_fragment        = 0; */
/*    pefinode->e.x->pfirst_fragment       = 0; */
/*    pefinode->last_processed_cluster   = 0; */


BOOLEAN load_efinode_fragment_list(FINODE *pefinode)
{
int sync_ret_val;
    /* Start the load by setting  pefinode->e.x->last_processed_cluster to zero */
    pefinode->e.x->last_processed_cluster = 0;
    do {
        sync_ret_val = pc_efinode_async_load_continue(pefinode);
        } while (sync_ret_val == PC_ASYNC_CONTINUE);
    if (sync_ret_val == PC_ASYNC_COMPLETE)
        return(TRUE);
    else
        return(FALSE);
}

/* load_efinode_fragments_until(FINODE *pefinode, dword offset)
*
*   Implements as needed cluster chain loads
*
*
*/

BOOLEAN load_efinode_fragments_until(FINODE *pefinode, llword offset)
{
int sync_ret_val;

#if (INCLUDE_EXFATORFAT64)
	if (ISEXFATORFAT64(pefinode->my_drive))
	{
		if (offset.val64 == 0) /* offset == 0 indicates just starting.. set start conditions and load one frag */
		{                /* then fall through and load one frag */
			pefinode->e.x->alloced_size_bytes.val64 = 0;
			pefinode->e.x->last_processed_cluster = 0;
		}
		do {
            sync_ret_val = pc_efinode_async_load_continue(pefinode);
            if (sync_ret_val == PC_ASYNC_ERROR)
                return(FALSE);
		} while (sync_ret_val == PC_ASYNC_CONTINUE && pefinode->e.x->alloced_size_bytes.val64 < offset.val64);
	}
	else
#endif
	{
		if (offset.val32 == 0) /* offset == 0 indicates just starting.. set start conditions and load one frag */
		{                /* then fall through and load one frag */
			pefinode->e.x->alloced_size_bytes.val32 =0;
			pefinode->e.x->last_processed_cluster = 0;
		}
		do {
            sync_ret_val = pc_efinode_async_load_continue(pefinode);
            if (sync_ret_val == PC_ASYNC_ERROR)
                return(FALSE);
		} while (sync_ret_val == PC_ASYNC_CONTINUE && pefinode->e.x->alloced_size_bytes.val32 < offset.val32);
	}
    /* Clear load as needed if fragment chain is completely read in */
    if (sync_ret_val == PC_ASYNC_COMPLETE)
    {
        pefinode->operating_flags &= ~FIOP_LOAD_AS_NEEDED;
    }
    return(TRUE);
}


void _pc_efinode_coalesce_fragments(FINODE *pefinode)
{
    if (pefinode)
    {
        pc_fraglist_coalesce(pefinode->e.x->pfirst_fragment);
        pefinode->e.x->plast_fragment = pc_end_fragment_chain(pefinode->e.x->pfirst_fragment);
    }
}

/* This routine is called by pc_freei() when the finode's opencount drops
   to zero and it is being returned to the free list.
   - Release clusters that were held in reserve to be used by the finode
     if it was expanded
   - Release all elements used by the extended IO system including:
     fragment lists.
*/
void pc_free_efinode(FINODE *pefinode)
{
    if (pefinode->opencount)
        return;
    if (pefinode->e.x)
    {
        if (pefinode->e.x->pfirst_fragment)
        {
            /* Release excess prealocated clusters */
            pc_free_excess_clusters(pefinode);
            pc_fraglist_free_list(pefinode->e.x->pfirst_fragment);
            pefinode->e.x->pfirst_fragment = 0;
        }
        /* release "to free queue".. normally will already be zeroed */
        if (pefinode->e.x->ptofree_fragment)
        {
            pc_fraglist_free_list(pefinode->e.x->ptofree_fragment);
            pefinode->e.x->ptofree_fragment = 0;
        }
        pc_memory_finode_ex(pefinode->e.x);
        pefinode->e.x = 0;
    }
}


/* Get the file extents of a finode.. this routine is used by pc_get_file_extents */
int _pc_efinode_get_file_extents(FINODE *pefinode,int infolistsize, FILESEGINFO *plist, BOOLEAN report_clusters, BOOLEAN raw)
{
int n_segments = 0;
dword n_clusters;
REGION_FRAGMENT *pf;
    pf = pefinode->e.x->pfirst_fragment;
    if (pf)
    {
        while (pf)
        {
            n_clusters = PC_FRAGMENT_SIZE_CLUSTERS(pf);
            n_segments += 1;
            if (n_segments <= infolistsize)
            {
                if (report_clusters)
                {
                    plist->block = pf->start_location;
                    plist->nblocks = n_clusters;

                }
                else
                {
                    plist->block = pc_cl2sector(pefinode->my_drive,pf->start_location);
                    if (raw)
                        plist->block += pefinode->my_drive->drive_info.partition_base;
                    plist->nblocks = n_clusters << pefinode->my_drive->drive_info.log2_secpalloc;
                }
                plist++;
            }
            pf = pf->pnext;

        }
    }
    return(n_segments);
}
