/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* PRFRAGMT.C - ERTFS fragment list management routines for ProPlus */
#include "rtfs.h"



/* consult the fragment list to find the fragment record and
   the cluster number of the next cluster in the chain after
   this_cluster used by pc_finode_link_cluster_chain(FINODE *pefinode)
   to find the fragments to begin linking */


REGION_FRAGMENT *pc_fraglist_find_next_cluster(REGION_FRAGMENT *pfstart,REGION_FRAGMENT *pfend ,dword this_cluster,dword *pnext_cluster,dword *pstart_offset)
{
REGION_FRAGMENT *pf;

    *pstart_offset = 0;
    pf = pfstart;
    while (pf)
    {
        if (pf->start_location <= this_cluster && pf->end_location >= this_cluster)
            break;
        if (pf == pfend)
            break;
        *pstart_offset += PC_FRAGMENT_SIZE_CLUSTERS(pf);
        pf = pf->pnext;
    }

    if (pf)
    {
        if (pf->end_location == this_cluster)
        { /* Start with the whole next fragment */
            /* Include the fragment we are skipping in the offset */
            *pstart_offset += PC_FRAGMENT_SIZE_CLUSTERS(pf);
            pf = pf->pnext;
            if (pf)
                *pnext_cluster = pf->start_location;
        }
        else /* Start inside this fragment */
        {
            this_cluster += 1;
            *pnext_cluster = this_cluster;
            *pstart_offset += this_cluster - pf->start_location;
        }
    }
    return(pf);
}

/* Seek n_clusters into the fragment chain.
   Return the fragmnet the contains the offset
   Return the offset in clusters from the beginning of the list
   to the start of the current fragment

   Not used so disable
*/
#if (0)
*** REGION_FRAGMENT *pc_fragment_seek_clusters(REGION_FRAGMENT *pf,dword n_clusters, dword *region_base_offset)
*** {
*** dword current_base_offset, next_base_offset;
***
***     current_base_offset = 0;
***     while (pf)
***     {
***         next_base_offset = current_base_offset + PC_FRAGMENT_SIZE_CLUSTERS(pf);
***         if ( (n_clusters >= current_base_offset) &&
***                  (n_clusters < next_base_offset) )
***             break;
***         current_base_offset = next_base_offset;
***         pf = pf->pnext;
***     }
***     /* pf may be null if we didn't hit it */
***     *region_base_offset = current_base_offset;
***     return(pf);
*** }
#endif
/* Split a fragment chain into two chains, the first one containing N clusters,
   the second containing the rest */
REGION_FRAGMENT *pc_fraglist_split(DDRIVE *pdr, REGION_FRAGMENT *pfstart,dword n_clusters,int *is_error)
{
REGION_FRAGMENT *pf;
dword frag_length,length_clusters,n_clusters_left;

    *is_error = 0;
    length_clusters = 0;
    n_clusters_left = n_clusters;
    pf = pfstart;
    while (pf)
    {
        frag_length = PC_FRAGMENT_SIZE_CLUSTERS(pf);
        length_clusters = length_clusters + frag_length;
        if (length_clusters >= n_clusters)
        {
            REGION_FRAGMENT *pend_chain;
            if (length_clusters == n_clusters)
            {/* Intersects at the end of a segment. Split in place */
                pend_chain  = pf->pnext;
                pf->pnext   = 0;
            }
            else
            {/* Intersects  a segment */
                /* Allocate a fragment to hold the residual of this
                   fragment and link it to the rest of the chain */
                /* Allocate a fragment. If none available recycle freelists. If we fail we we are out of luck */
                pend_chain = pc_fraglist_frag_alloc(pdr, pf->start_location+n_clusters_left,
                                                pf->end_location, pf->pnext);
                if (!pend_chain)
                {
                    *is_error = 1;
                    return(0);
                }
                pf->end_location = pend_chain->start_location-1;
                pf->pnext = 0;
            }
            return(pend_chain);
        }
        n_clusters_left -= frag_length;
        pf = pf->pnext;
    }
    return(0); /* oops overshot */
}

/* Find the fragment in a chain that contains "cluster"
   if "cluster" is not in the chain return 0 */
REGION_FRAGMENT *pc_fraglist_find_cluster(REGION_FRAGMENT *pfstart,REGION_FRAGMENT *pfend ,dword cluster)
{
REGION_FRAGMENT *pf;

    pf = pfstart;
    while (pf)
    {
        if (pf->start_location <= cluster && pf->end_location >= cluster)
            return(pf);
        if (pf == pfend)
            break;
        pf = pf->pnext;
     }
     return(0);
}

void pc_fraglist_coalesce(REGION_FRAGMENT *pfstart)
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
            /* Keep pf where it is in case pf->pnext is adjaccent */
        }
        else
            pf = pf->pnext;
    }
}

/* Count the number of fragments in the list from pf to pfend.
   If pfend is null count till end of list if pf is 0 return 0 */
dword pc_fraglist_count_list(REGION_FRAGMENT *pf,REGION_FRAGMENT *pfend)
{
    dword l = 0;
    while(pf) {l += 1; if (pf == pfend) break; else pf= pf->pnext;}
    return(l);
}
