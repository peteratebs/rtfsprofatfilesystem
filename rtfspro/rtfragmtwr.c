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
#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */

/*
    Set or clear locations in a fragment list
        pc_fraglist_set_range();
        pc_fraglist_clear_range();
    these routines are used by the freespace manager
*/

/* Set a range of locations in a fragment list, if necessary a new fragment will be allocated
   and linked into the list. If no list was passed, one is created.
   reurns the new head of the list after insertion of the new range
*/
REGION_FRAGMENT  *pc_fraglist_set_range(REGION_FRAGMENT *inpf, dword start_location, dword end_location, int *is_error)
{
REGION_FRAGMENT *outpf,*pf,*pf2,*pfprev;

    *is_error = 0;

    if (!inpf)
    {
        /* Called by the freelist manager only so allocate but don't recycle freelists. If we fail we'll free our own freelist */
        pf = pc_fraglist_frag_alloc(0, start_location, end_location, 0);
        if (!pf)
            *is_error = 1;
        return(pf);
    }
    outpf = pf = inpf;
    pfprev = 0;
    while (pf)
    {
        if ((end_location+1) < pf->start_location)
        { /* fragment is past the region to set. Link a new frag in front */
            pf2 = pc_fraglist_frag_alloc(0, start_location, end_location, pf);
            if (!pf2)
            {
                *is_error = 1;
                return(inpf);
            }
            if (pfprev)
                pfprev->pnext = pf2;
            else
                outpf = pf2;
            break;
        }
        else if ( (end_location+1 >= pf->start_location) &&
             (start_location <= pf->end_location+1) )
        { /* Intersection or overlap */
            if (start_location < pf->start_location)
            {   /* Left overlap move region start_location left */
                pf->start_location = start_location;
            }
            if ( end_location > pf->end_location )
            { /* Right overlap move region end_location right */
                pf->end_location = end_location;
                /* Scan foreward and truncate or remove regions that were
                   absorbed by widening this region */
                pf2 = pf->pnext;
                while (pf2)
                {
                    if (pf2->end_location <= end_location)
                    { /* pf2 is is a member of pf, remove */
                        pf->pnext = pf2->pnext;
                        pc_fraglist_frag_free(pf2);
                        pf2 = pf->pnext;
                    }
                    else if (pf2->start_location <= end_location+1)
                    { /*pf2 joins or intersects pf on the right */
                        pf->end_location = pf2->end_location;
                        pf->pnext = pf2->pnext;
                        pc_fraglist_frag_free(pf2);
                        pf2 = pf->pnext;
                    }
                    else
                        break;
                 }
            }
            break;
        }
        else if (!pf->pnext)
        {
            pf2 = pc_fraglist_frag_alloc(0, start_location, end_location, 0);
            if (!pf2)
            {
                *is_error = 1;
                return(inpf);
            }
            pf->pnext = pf2;
            break;
        }
        else
        {
             pfprev = pf;
             pf = pf->pnext;
        }
    }
    return(outpf);
}

/* Clear a range of locations in a fragment list
    If any holes in the list are created new fragments are allocated
    If any fragment records are eliminated by range, then they are freed.
*/

REGION_FRAGMENT  *pc_fraglist_clear_range(REGION_FRAGMENT *inpf, dword start_location, dword end_location, int *is_error)
{
REGION_FRAGMENT *outpf,*pprev,*pf,*pf2;

    *is_error = 0;

    pprev = 0;
    outpf = pf = inpf;
    while (pf)
    {
        if (pf->start_location > end_location)
            break;
        if (start_location > pf->end_location)
        {
            pprev = pf;
            pf = pf->pnext;
        }
        else if (start_location <= pf->start_location)
        {   /* Region to clear overlaps fragment from the left */
             if (pf->end_location <= end_location)
             { /* Whole fragment is in the region, free it */
                pf2 = pf;
                pf = pf->pnext;
                if (pprev)
                    pprev->pnext = pf;
                else
                    outpf = pf;
                pc_fraglist_frag_free(pf2);
            }
            else
            {   /* release leading locations, shift start of fragment right*/
                pf->start_location = end_location+1;
                pf = pf->pnext;
            }
        }
        else
        {   /* Region to clear starts to the right */
             if (end_location >= pf->end_location)
                pf->end_location = start_location-1; /* Consumes the rest */
             else
             { /* We created a hole between start and end */
                pf2 = pc_fraglist_frag_alloc(0, end_location+1, pf->end_location, pf->pnext);
                if (!pf2)
                {
                    *is_error = 1;
                    return(outpf);
                }
                pf->pnext = pf2;
                pf->end_location = start_location-1;
                pf = pf->pnext;
            }
        }
    }
    return(outpf);
}

#endif /* Exclude from build if read only */
