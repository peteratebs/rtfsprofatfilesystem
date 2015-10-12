/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* PRASYINT.C - Contains asynchronous state machine .

  The following public API routines are provided by this module:

*/

#include "rtfs.h"
#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */

#if (INCLUDE_ASYNCRONOUS_API)

dword _get_operating_flags(PC_FILE *pefile);



/* #define ASYNC_ANNOUNCE(A) RTFS_PRINT_STRING_1((byte *)A, PRFLG_NL); */
#define ASYNC_ANNOUNCE(A)


int pc_efilio_async_flush_continue(PC_FILE *pefile)
{
    int ret_val;
    dword operating_flags, max_clusters_per_pass;
    DDRIVE *pdr;

    operating_flags = _get_operating_flags(pefile);
    if (!(operating_flags & FIOP_NEEDS_FLUSH))
    {
        ret_val = PC_ASYNC_COMPLETE;
        goto ex_it;
    }
    pdr = pefile->pobj->pdrive;

    {
        byte *pubuff;
        max_clusters_per_pass = 1; /* Get the size of the user buffer in sectors */
        pubuff = pc_claim_user_buffer(pdr, &max_clusters_per_pass, 0); /* released at cleanup */
        if (!pubuff)
        {
            ret_val = PC_ASYNC_ERROR;
            goto ex_it;
        }
        pc_release_user_buffer(pdr, pubuff);
    }
    if (pdr->drive_info.fasize == 8)
        max_clusters_per_pass *= pefile->pobj->pdrive->drive_info.clpfblock32;
    else
        max_clusters_per_pass *= pefile->pobj->pdrive->drive_info.clpfblock16;
    {
        if (!_pc_efilio32_flush(pefile,max_clusters_per_pass))
        {
            ret_val = PC_ASYNC_ERROR;
            goto ex_it;
        }
    }
    operating_flags = _get_operating_flags(pefile);
    if (operating_flags & FIOP_NEEDS_FLUSH)
        ret_val = PC_ASYNC_CONTINUE;
    else
        ret_val = PC_ASYNC_COMPLETE;
ex_it:
    return(ret_val);
}


/* Transition from file reopen to file unlink
   Call truncate to truncate the file to zero sized
   this will queu up a flush. When that completes we will finish the
   process by calling rmode */

int pc_queue_file_unlink(PC_FILE *pefile)
{
BOOLEAN rval;

    rval = _pc_efinode_chsize(pefile->pobj->finode, 0, 0);
    if (rval)
        return(PC_ASYNC_CONTINUE);
    else
        return(PC_ASYNC_ERROR);
}


#endif /* (INCLUDE_ASYNCRONOUS_API) */
#endif /* Exclude from build if read only */
