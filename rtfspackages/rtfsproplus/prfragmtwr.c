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
#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */



/* Remove all clusters in the fragment list from the free cluster cache
     note: this does not remove the clusters from the disk itself
     but from the free cache. When the clusters are assigned to
     a file or directory they are remmoved from the disks free space,
     but the ERTFS cluister allocator will not use these clusters
     again a unless pc_fraglist_add_free_region() is clled */
/* These clusters were found in from free region but not claimed
   they will be linked later linked */

BOOLEAN pc_fraglist_remove_free_region(DDRIVE *pdr, REGION_FRAGMENT *pf)
{
#if (INCLUDE_EXFATORFAT64)
	if (ISEXFATORFAT64(pdr))
	{
    	while (pf)
    	{
        	if (!exfatop_remove_free_region(pdr,  pf->start_location,
            	PC_FRAGMENT_SIZE_CLUSTERS(pf)))
            	return(FALSE);
        	pf = pf->pnext;
    	}
    	return(TRUE);
	}
#endif
    while (pf)
    {
        if (!fatop_remove_free_region(pdr,  pf->start_location,
            PC_FRAGMENT_SIZE_CLUSTERS(pf)))
            return(FALSE);
        pf = pf->pnext;
    }
    return(TRUE);
}

/* Return all clusters in the fragment list to the free cluster cache */
/* These clusters were allocated from free region but not linked */
BOOLEAN pc_fraglist_add_free_region(DDRIVE *pdr, REGION_FRAGMENT *pf)
{
    while (pf)
    {
#if (INCLUDE_EXFATORFAT64)
			if (ISEXFATORFAT64(pdr))
			{
				if (!exfatop_add_free_region(pdr,  pf->start_location,
					PC_FRAGMENT_SIZE_CLUSTERS(pf),FALSE))
					return FALSE;
			}
			else
#endif
				if (!fatop_add_free_region(pdr,  pf->start_location,
						PC_FRAGMENT_SIZE_CLUSTERS(pf),FALSE))
					return(FALSE);
        pf = pf->pnext;
    }
    return(TRUE);
}
/* Return all clusters in the fragment list to the pool of free clusters
   and zero the regions in the fat */
BOOLEAN pc_fraglist_fat_free_list(DDRIVE *pdr, REGION_FRAGMENT *pf)
{
dword ncontig;
    while (pf)
    {
        ncontig = PC_FRAGMENT_SIZE_CLUSTERS(pf);
#if (INCLUDE_EXFATORFAT64)
		if (ISEXFATORFAT64(pdr))
		{
			if (!exfatop_add_free_region(pdr,pf->start_location,ncontig,TRUE))
				return FALSE;
		}
		else
#endif
        /* Zero from start to start + n_contig */
        /* And tell the free region manager that these clusters are free */
			if (!fatop_free_frag(pdr,FOP_RMTELL,0, pf->start_location,ncontig))
			return(FALSE);
        pf = pf->pnext;
    }
    return(TRUE);
}
#endif /* Exclude from build if read only */
