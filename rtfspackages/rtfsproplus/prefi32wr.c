/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* PREFI32.C - Contains internal 32 bit enhanced file IO source code.
*/

#include "rtfs.h"

#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */



/* Internal version of pc_efilio_flush() called by pc_efilio_flush and pc_efilio_close
   and flush_all_files()  */
BOOLEAN _pc_efilio32_flush(PC_FILE *pefile,dword max_clusters_per_pass)                                       /*__fn__*/
{
    BOOLEAN ret_val;
    FINODE *pefinode;

    pefinode = pefile->pobj->finode;



    /* Convert to native. Overwrite the existing inode.Set archive/date  */
    if (pc_check_file_dirty(pefile))
    { /* flush cluster chains, directory entry and then FAT */
        ret_val = FALSE;
		dword link_flags = FOP_LINK|FOP_LINK_NEXT;
        if (!_pc_efinode_queue_cluster_links(pefile->pobj->finode, pefile->fc.plus.allocation_policy))
            return(FALSE); /* error */
        /* Process fragments that are queued to be freed */

#if (INCLUDE_EXFATORFAT64)
		if (ISEXFATORFAT64(pefile->pobj->pdrive))
		{
#if (INCLUDE_EXFAT)	/* FAT64 Does not have secondary flags */
			if (ISEXFAT(pefile->pobj->pdrive))
			{
				if (!pefinode->e.x->pfirst_fragment || (pefinode->e.x->pfirst_fragment && !pefinode->e.x->pfirst_fragment->pnext))
				{
					pefinode->exfatinode.GeneralSecondaryFlags |= EXFATNOFATCHAIN;
					pefinode->e.x->clusters_to_process=0;
				}
				else
				{	/* Switch to non-contiguous file, allow the FAT close process link into a cluster chain */
					pefinode->exfatinode.GeneralSecondaryFlags &= ~EXFATNOFATCHAIN;
					link_flags |= FOP_EXF_CHAIN;
				}
				 
			}
#endif
		}
#endif
        if (pefinode->e.x->ptofree_fragment)
        {
            if (pefinode->e.x->clusters_to_delete)
            { /* Update the FAT and free region manager */
                if (!pc_efinode_delete_cluster_chain(pefile->pobj->finode,max_clusters_per_pass))
                    return(FALSE);
            }
            if (!pefinode->e.x->clusters_to_delete)
            { /* Done. release the fragment chain and clear variables */
                pc_fraglist_free_list(pefinode->e.x->ptofree_fragment);
                pefinode->e.x->last_deleted_cluster = 0;
                pefinode->e.x->ptofree_fragment = 0;
            }
			/* Return if in async mode otherwise fall through */
			if (max_clusters_per_pass)
				return(TRUE);
        }

        /* Now move on to the link phase Link chain but don't tell region manager */
        if (pc_efinode_link_or_delete_cluster_chain(pefile->pobj->finode,link_flags,max_clusters_per_pass))
        {
            if (pefile->pobj->finode->e.x->clusters_to_process!=0)
                ret_val = TRUE; /* continue */
            else
            {   /* If all clusters are linked flush the rest */
                if (pc_update_inode(pefile->pobj, FALSE, DATESETUPDATE) &&
                    fatop_flushfat(pefile->pobj->pdrive) )
                {
                    ret_val = TRUE;
                    pefile->pobj->finode->operating_flags &= ~FIOP_NEEDS_FLUSH;
                }
            }
        }
    }
    else
    { /* no flush needed */
        ret_val = TRUE;
    }
    return(ret_val);
}

/* Internal version of pc_efilio_settime() called by pc_efilio_settime() */
BOOLEAN _pc_efilio32_settime(PC_FILE *pefile,word new_time, word new_date)
{
    BOOLEAN ret_val;
    FINODE *pefinode;

    pefinode = pefile->pobj->finode;

    pefinode->ftime = new_time;
    pefinode->fdate = new_date;
    ret_val = pc_update_by_finode(pefinode, pefinode->my_index, FALSE, 0);
    return(ret_val);
}
#endif /* Exclude from build if read only */
