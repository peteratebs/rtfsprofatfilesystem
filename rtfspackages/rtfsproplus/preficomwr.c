/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* PREFICOM.C - Contains internal 32 bit enhanced file IO source code
   shared by 32 and 64 bit apis
*/

#include "rtfs.h"
#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */


#if (INCLUDE_DEBUG_TEST_CODE)
extern dword debug_first_efile_cluster_allocated;
extern dword debug_last_efile_cluster_allocated;
#endif


static dword truncate_alloc_request_32(dword max_file_size, dword file_pointer, dword min_alloc_bytes, dword count)
{
dword max_count,requested_bytes;

    max_count = max_file_size - file_pointer;

    /* Check for a wrap of request */
    if ((count + min_alloc_bytes) <= count)
    {
        if (count > min_alloc_bytes)
            requested_bytes = count;
        else
            requested_bytes = min_alloc_bytes;
    }
    else
        requested_bytes =  ((count + min_alloc_bytes-1)/min_alloc_bytes) * min_alloc_bytes;
    /* Check for a file pointer wrap */
    if (requested_bytes > max_count)
        requested_bytes = max_count;
    return(requested_bytes);
}

#if (INCLUDE_EXFATORFAT64)
static dword truncate_alloc_request_64(dword min_alloc_bytes, dword count)
{
ddword requested_bytes;
	requested_bytes =  (((ddword)count + (ddword)min_alloc_bytes-1)/(ddword)min_alloc_bytes) * (ddword)min_alloc_bytes;
	if (requested_bytes&0xffffffff00000000)
		return (0xffffffff);
    return((dword)(requested_bytes&0xffffffff));
}
#endif
static REGION_FRAGMENT *_pc_efiliocom_find_free_clusters (DDRIVE *pdr, PC_FILE *pefile, dword alloc_count, BOOLEAN is_prealloc, int *is_error);
void _pc_efiliocom_append_clusters (DDRIVE *pdr, PC_FILE *pefile, FINODE *peffinode, REGION_FRAGMENT *pnew_frags, BOOLEAN is_prealloc, dword new_clusters, dword new_bytes);
static void _pc_efiliocom_queue_flush(PC_FILE *pefile,FINODE *peffinode);


BOOLEAN _pc_efiliocom_write(PC_FILE *pefile,FINODE *peffinode, byte *buff, dword count, dword *nwritten)
{
DDRIVE *pdr;
BOOLEAN ret_val, do_preallocates;
dword  saved_alloc_hint, alloc_cluster_count;
llword orginal_file_size, max_file_size, max_file_pointer;
dword prealloc_cluster_count,residual_cluster_count,write_count,bytes_required,bytes_allocated;
dword prealloc_byte_count, residual_byte_count;
BOOLEAN appending;
int is_error, allocation_scheme;
#if (INCLUDE_DEBUG_TEST_CODE)
dword  first_cluster_allocated =0;
#endif

    pdr = peffinode->my_drive;

    /* For flash memory use the PCE_FIRST_FIT option to also force garbage collection from partially used erase blocks */
    if (pefile->fc.plus.allocation_policy & PCE_FIRST_FIT)
        allocation_scheme = ALLOC_CLUSTERS_UNALIGNED;
    else if ((pdr->du.drive_operating_policy & DRVPOL_NAND_SPACE_OPTIMIZE) == 0)
        allocation_scheme = ALLOC_CLUSTERS_ALIGNED;
	else
        allocation_scheme = ALLOC_CLUSTERS_PACKED;

    /* Don't worry about preallocation if not enabled */

    if (pefile->fc.plus.min_alloc_bytes == (dword) pdr->drive_info.bytespcluster)
        do_preallocates = FALSE;
    else
        do_preallocates = TRUE;
    appending = FALSE;
    prealloc_byte_count = residual_byte_count = 0;
    prealloc_cluster_count = residual_cluster_count = 0;

    ret_val = FALSE;
    *nwritten = 0;

    saved_alloc_hint = pefile->fc.plus.allocation_hint;

     _pc_efiliocom_sync_current_fragment(pefile,peffinode);
#if (INCLUDE_EXFATORFAT64)
		if (ISEXFATORFAT64(pdr))
		{
			max_file_pointer.val64 = 0;
	        max_file_size.val64 = 0xffffffffffffffff;
			orginal_file_size.val64 = peffinode->fsizeu.fsize64;
		}
		else
#endif
		{
			max_file_pointer.val32 = 0;
			max_file_size.val32 = LARGEST_DWORD;
			orginal_file_size.val32 = peffinode->fsizeu.fsize;
		}

    /* Truncate write counts to fit inside 4 Gig size limit */
    if (count)
	{
#if (INCLUDE_EXFATORFAT64)
		if (!ISEXFATORFAT64(pdr))
#endif
			count = truncate_32_count(pefile->fc.plus.file_pointer.val32,count);
	}
    if (peffinode->operating_flags & FIOP_LOAD_AS_NEEDED)
    { /* If load as needed make sure the range we are writing to is loaded
         if it all doesn't fit we will load until the end */
        llword  new_file_pointer;
        if (count == 0)
		{
#if (INCLUDE_EXFATORFAT64)
			if (ISEXFATORFAT64(pdr))
				new_file_pointer.val64 = peffinode->fsizeu.fsize64;
			else
#endif
				new_file_pointer.val32 = peffinode->fsizeu.fsize;
		}
        else
		{
#if (INCLUDE_EXFATORFAT64)
			if (ISEXFATORFAT64(pdr))
				new_file_pointer.val64 = peffinode->fsizeu.fsize64 + (ddword)(count - 1);
			else
#endif
				new_file_pointer.val32 = pefile->fc.plus.file_pointer.val32 + (count - 1);
		}
        if (!load_efinode_fragments_until(peffinode, new_file_pointer))
            goto return_locked; /* load_efinode_fragments_until has set errno */
    }
#if (INCLUDE_EXFATORFAT64)
		if (ISEXFATORFAT64(pdr))
		{
			ddword l;
				l = peffinode->e.x->alloced_size_bytes.val64 - pefile->fc.plus.file_pointer.val64;
				if (l > (ddword) count)
					bytes_allocated=count;
				else
					bytes_allocated=(dword)l;
		}
		else
#endif
			bytes_allocated = peffinode->e.x->alloced_size_bytes.val32 - pefile->fc.plus.file_pointer.val32;

    if (count == 0)
    {   /* Preallocating. If some bytes already allocated return success imediately */
        bytes_required = alloc_cluster_count = 0;
        if (bytes_allocated)
        {
            ret_val = TRUE;
            goto return_locked;
        }
        else
        {
            do_preallocates = TRUE;
#if (INCLUDE_EXFATORFAT64)
			if (ISEXFATORFAT64(pdr))
				prealloc_byte_count = truncate_alloc_request_64(pefile->fc.plus.min_alloc_bytes, pefile->fc.plus.min_alloc_bytes);
			else
#endif
				prealloc_byte_count = truncate_alloc_request_32(max_file_size.val32, pefile->fc.plus.file_pointer.val32, pefile->fc.plus.min_alloc_bytes, pefile->fc.plus.min_alloc_bytes);
            prealloc_cluster_count = pc_byte2clmod(pdr, prealloc_byte_count);
        }
    }
    else
    { /* Calculate allocation requirements */
        /* Bytes required is count - available, clipped to max file size */
        if (count > bytes_allocated)
        {
            bytes_required = count - bytes_allocated;   /* Count minus bytes from file pointer to alloced size */
#if (INCLUDE_EXFATORFAT64)
			if (ISEXFATORFAT64(pdr))
				bytes_required = truncate_alloc_request_64(bytes_required, bytes_required);
			else
#endif
            	bytes_required = truncate_alloc_request_32(max_file_size.val32, pefile->fc.plus.file_pointer.val32, bytes_required, bytes_required);
            alloc_cluster_count = pc_byte2clmod(pdr, bytes_required);
        }
        else
        {
            bytes_required = alloc_cluster_count = 0;
        }
        if (do_preallocates)
        {
            /* Calculate request with pre-allocation */
#if (INCLUDE_EXFATORFAT64)
			if (ISEXFATORFAT64(pdr))
				prealloc_byte_count = truncate_alloc_request_64(pefile->fc.plus.min_alloc_bytes, bytes_required);
			else
#endif
				prealloc_byte_count = truncate_alloc_request_32(max_file_size.val32, pefile->fc.plus.file_pointer.val32, pefile->fc.plus.min_alloc_bytes, bytes_required);
            prealloc_cluster_count = pc_byte2clmod(pdr, prealloc_byte_count);   /* We will subtract alloc_cluster count to see if we need any */
        }
    }
    /* Calculate residual fragment requirements - prealloc_cluster_count also contains alloc_cluster_count */
    if (do_preallocates)
    {   /* If Force contiguous - make sure we can contiguously allocate what we need now plus preallocated clusters */
        if (pefile->fc.plus.allocation_policy & PCE_FORCE_CONTIGUOUS)
        {
            dword n_contig, alloc_start_hint, first_free_cluster ;
            int is_error;

                if (saved_alloc_hint) /* User suggested a space */
                    alloc_start_hint = saved_alloc_hint;
                else if (pefile->fc.plus.allocation_policy & PCE_FIRST_FIT)
                    alloc_start_hint = 0;  /* Start at beginning of fat */
                else
                    alloc_start_hint = _pc_efilio_last_cluster(pefile);


            if (!alloc_start_hint)
                alloc_start_hint = 2;  /* Start at beginning of fat */
            first_free_cluster =
                fatop_find_contiguous_free_clusters(pdr,
                    alloc_start_hint, pdr->drive_info.maxfindex,
                prealloc_cluster_count, prealloc_cluster_count, &n_contig, &is_error, allocation_scheme);
            if (n_contig != prealloc_cluster_count) /* Defensive.. should be true */
                first_free_cluster = 0;
            if (!first_free_cluster && !is_error && alloc_start_hint != 2)
            {
                first_free_cluster =
                    fatop_find_contiguous_free_clusters(pdr,
                    2, pdr->drive_info.maxfindex, prealloc_cluster_count, prealloc_cluster_count, &n_contig, &is_error, allocation_scheme);
            }
            if (is_error)
                goto return_locked;
            if (n_contig != prealloc_cluster_count) /* Defensive.. should be true */
                first_free_cluster = 0;
            if (!first_free_cluster)
            {
                rtfs_set_errno(PENOSPC, __FILE__, __LINE__);
                goto return_locked;
            }
            /* Set the allocation hint so we grab these contighues clusters */
            pefile->fc.plus.allocation_hint = first_free_cluster;
        }
        /* Now subtract alloc_cluster_count amount from total including pre-alloc and see if we need prealloc */
        prealloc_cluster_count -= alloc_cluster_count;
        prealloc_byte_count = pc_alloced_bytes_from_clusters(pdr, prealloc_cluster_count);
    }
    /* Now perform writing and allocating to write "count" bytes */
    write_count = count;    /* Will be zero for a pure pre-allocate */
    while (write_count)
    {
#if (INCLUDE_EXFATORFAT64)
		if (ISEXFATORFAT64(pdr))
		{
			ERTFS_ASSERT(peffinode->e.x->alloced_size_bytes.val64 >= pefile->fc.plus.file_pointer.val64)
				;
		}
		else
#endif
		{
			ERTFS_ASSERT(peffinode->e.x->alloced_size_bytes.val32 >= pefile->fc.plus.file_pointer.val32)
				;
		}

        if (!bytes_allocated)
        {  /* Allocate additional space (file_pointer==alloced_size_bytes) */
        dword new_clusters;
        REGION_FRAGMENT *pnew_frags;

            if (pefile->fc.plus.allocation_policy & PCE_REMAP_FILE)
            { /* You can overwrite a remap file but you can't extend it */
                rtfs_set_errno(PESHARE, __FILE__, __LINE__);
                goto return_locked;
            }
            /* Allocate new fragments, pre-alloc is false so release frags from free manager if we have to */
            pnew_frags = _pc_efiliocom_find_free_clusters(pdr, pefile, alloc_cluster_count, FALSE, &is_error);

#if (INCLUDE_FAILSAFE_CODE)
            if (!pnew_frags && !is_error && get_errno() == PENOSPC) /* out of space. If Journaling try again after shrinking failsafe file */
            {
                rtfs_clear_errno(); /* clear error status */
                if (prtfs_cfg->pfailsafe && prtfs_cfg->pfailsafe->fs_recover_free_clusters(pdr, alloc_cluster_count))
                {
                    pnew_frags = _pc_efiliocom_find_free_clusters(pdr, pefile, alloc_cluster_count, FALSE, &is_error);
                    if (!pnew_frags && !is_error)
					{
                        rtfs_set_errno(PENOSPC, __FILE__, __LINE__);
                    }
                }
            }
#endif
            if (!pnew_frags) /* Errno was set, either a problem or out of space */
                goto return_locked;
            new_clusters = pc_fraglist_count_clusters(pnew_frags,0);
            bytes_allocated = pc_alloced_bytes_from_clusters(pdr, new_clusters);
#if (INCLUDE_DEBUG_TEST_CODE)
            if (!first_cluster_allocated)  /* For regression test */
                first_cluster_allocated = debug_first_efile_cluster_allocated = pnew_frags->start_location;
           debug_last_efile_cluster_allocated =  pc_end_fragment_chain(pnew_frags)->end_location;
#endif
            /* Attach the new cluster to the file, and update allocation information
               is_prealloc is FALSE */
            _pc_efiliocom_append_clusters(pdr, pefile,peffinode, pnew_frags, FALSE, new_clusters, bytes_allocated);
            appending = TRUE;
        }
        /* Save allocation size because we will change it before we call the io
           routine so it doesn't accidentally think it is in an overwrite if in
           transaction mode */
        /* Now fill all allocated space */
        if ( bytes_allocated)
        {  /* First fill all allocated space */
        dword n_to_write;
            if (bytes_allocated <= write_count )
                n_to_write = bytes_allocated;
            else
                n_to_write = write_count;
            /* Set the allocation size temporilly to the original size,
               this way if we are in transaction mode we won't try to
               overwrite space we just allocated */
            if (!_pc_efiliocom_io(pefile,peffinode, buff, n_to_write, FALSE, appending))
            {
                goto return_locked;
            }
#if (INCLUDE_EXFATORFAT64)
		if (ISEXFATORFAT64(pdr))
		{
			if (pefile->fc.plus.file_pointer.val64 > max_file_pointer.val64)
           {
                max_file_pointer = pefile->fc.plus.file_pointer;
				if (max_file_pointer.val64 > peffinode->fsizeu.fsize64)
                {
					peffinode->fsizeu.fsize64 = max_file_pointer.val64;
                }
            }
		}
		else
#endif
		{
			if (pefile->fc.plus.file_pointer.val32 > max_file_pointer.val32)
           {
                max_file_pointer = pefile->fc.plus.file_pointer;
				if (max_file_pointer.val32 > peffinode->fsizeu.fsize)
                {
					peffinode->fsizeu.fsize = max_file_pointer.val32;
                }
            }
		}
            *nwritten += n_to_write;
            bytes_allocated -= n_to_write;
            write_count -= n_to_write;
            if (buff)
                buff += n_to_write;
        }
    }
    /* Allocating and writing are done, now see about preallocating residual clusters for the next segment */
    if (prealloc_cluster_count)
    {
        dword new_clusters;
		dword bytes_pre_allocated;
        REGION_FRAGMENT *pnew_frags;

        /* pre-allocate new fragments */
        /* pre-alloc is true so don't pre allocate if free manager is disabled */
        pnew_frags = _pc_efiliocom_find_free_clusters(pdr, pefile, prealloc_cluster_count, TRUE, &is_error);
        if (is_error) /* Errno was set */
            goto return_locked;
        if (pnew_frags)
        {
            new_clusters = pc_fraglist_count_clusters(pnew_frags,0);
            bytes_pre_allocated = pc_alloced_bytes_from_clusters(pdr, new_clusters);
#if (INCLUDE_DEBUG_TEST_CODE)
            if (!first_cluster_allocated)  /* For regression test */
                first_cluster_allocated = debug_first_efile_cluster_allocated = pnew_frags->start_location;
            debug_last_efile_cluster_allocated =  pc_end_fragment_chain(pnew_frags)->end_location;
#endif
            /* Attach the new cluster to the file, and update allocation information
               is_prealloc is TRUE */
            _pc_efiliocom_append_clusters(pdr, pefile,peffinode, pnew_frags, TRUE, new_clusters, bytes_pre_allocated);
        }
    }
    /* Allocating and pre-allocating are done now see about preallocating for the nex segment in a 64 bit file  */
    /* Now what ? */
#if (INCLUDE_EXFATORFAT64)
		if ( (!ISEXFATORFAT64(pdr)&&(max_file_pointer.val32 > orginal_file_size.val32))||(ISEXFATORFAT64(pdr)&&(max_file_pointer.val64 > orginal_file_size.val64))||buff)
#else
		if ((max_file_pointer.val32 > orginal_file_size.val32) || buff)
#endif

    {
        _pc_efiliocom_queue_flush(pefile,peffinode);
    }
    ret_val = TRUE;
#if (INCLUDE_EXFATORFAT64)
	if (CHECK_FREEMG_CLOSED(pefile->pobj->pdrive) && ((!ISEXFATORFAT64(pdr)&&max_file_pointer.val32 > orginal_file_size.val32)||(ISEXFATORFAT64(pdr)&&max_file_pointer.val64 > orginal_file_size.val64) )&& ret_val)
#else
	if (CHECK_FREEMG_CLOSED(pefile->pobj->pdrive) && (max_file_pointer.val32 > orginal_file_size.val32) && ret_val)
#endif
    {
        /* If we allocated and no free manager is present then flush the file
           we don't require a flush if it was just an overwrite */
        ret_val = _pc_efilio_flush(pefile);
    }

return_locked:
    pefile->fc.plus.allocation_hint = saved_alloc_hint;
    return(ret_val);
}


#if (INCLUDE_RTFS_DVR_OPTION||INCLUDE_FAILSAFE_CODE)
#if (INCLUDE_EXFATORFAT64)
dword pc_byte2cloff64(DDRIVE *pdr, ddword nbytes);
dword pc_byte2cloffbytes64(DDRIVE *pdr, ddword nbytes);
#endif
/* Resize the fragment containing the current file pointer to be
   new_size clusters long.  */
BOOLEAN _pc_efiliocom_resize_current_fragment(PC_FILE *pefile,FINODE *peffinode,dword new_size)
{
REGION_FRAGMENT *pf,*pcurr;
dword cl_offset_in_region;
DDRIVE *pdr;

    ERTFS_ASSERT(new_size > 0)

    pdr = peffinode->my_drive;
    pcurr = pefile->fc.plus.pcurrent_fragment;
    if (PC_FRAGMENT_SIZE_CLUSTERS(pcurr) == new_size)
        return(TRUE);
    /* Allocate a fragment. If none available recycle freelists. If we fail we we are out of luck */
    pf = pc_fraglist_frag_alloc(pdr, pcurr->start_location+new_size, pcurr->end_location, pcurr->pnext);
    if (!pf)
        return(FALSE);
    /* Find the cluster offset in the old fragment that contained the file pointer
       if this is greater than the new size, reset block offset and byte offset
       pointers */
#if (INCLUDE_EXFATORFAT64)
	if (ISEXFATORFAT64(pdr))
	{
		cl_offset_in_region = pc_byte2cloff64(pdr, pefile->fc.plus.file_pointer.val64 - pefile->fc.plus.region_byte_base.val64);
	}
	else
#endif
		cl_offset_in_region = pc_byte2cloff(pdr, (dword) (pefile->fc.plus.file_pointer.val32 - pefile->fc.plus.region_byte_base.val32));
    pcurr->end_location = pf->start_location-1;
    pcurr->pnext = pf;

    if (pcurr == peffinode->e.x->plast_fragment)
        peffinode->e.x->plast_fragment = pf;

    if (new_size <= cl_offset_in_region)
    {
        llword new_size_bytes;
#if (INCLUDE_EXFATORFAT64)
	if (ISEXFATORFAT64(pdr))
	{
		new_size_bytes.val64 = pc_alloced_bytes_from_clusters_64(pdr, new_size);
		pefile->fc.plus.region_byte_base.val64+=new_size_bytes.val64;
	}
	else
#endif
	{
        new_size_bytes.val32 = pc_alloced_bytes_from_clusters(pdr, new_size);
		pefile->fc.plus.region_byte_base.val32+=new_size_bytes.val32;
	}
        pefile->fc.plus.pcurrent_fragment = pf;
        pefile->fc.plus.region_block_base = pc_cl2sector(pdr, pf->start_location);
    }
    return(TRUE);
}
#endif
static REGION_FRAGMENT *_pc_efiliocom_find_free_clusters (DDRIVE *pdr, PC_FILE *pefile, dword alloc_count, BOOLEAN is_prealloc, int *is_error)
{
    REGION_FRAGMENT *pnew_frags = 0;
    dword alloc_start_hint;

    *is_error = 0;
    /* Don't preallocate if the memory manager is out of resources */
    if (is_prealloc && CHECK_FREEMG_CLOSED(pdr))
        return(0);

    if (!pdr->drive_info.known_free_clusters)
    {
        /* Don't return ot of space if it is a prealloc */
        if (is_prealloc)
            return(0);
        rtfs_set_errno(PENOSPC, __FILE__, __LINE__);
    }
    else
    {
        if (pefile->fc.plus.allocation_hint) /* User suggested a space */
            alloc_start_hint = pefile->fc.plus.allocation_hint;
        else if (pefile->fc.plus.allocation_policy & PCE_FIRST_FIT)
            alloc_start_hint = 0;  /* Start at beginning of file region */
        else
            alloc_start_hint = _pc_efilio_last_cluster(pefile);
        pnew_frags = pc_region_file_find_free_chain(pdr, pefile->fc.plus.allocation_policy,
                        alloc_start_hint, alloc_count,is_error);
    }
    /* If it is a preallocate and the memory manager ran out of resource while we where allocating,
       don't keep it */
    if (is_prealloc && pnew_frags && CHECK_FREEMG_CLOSED(pdr))
    {
        pc_fraglist_free_list(pnew_frags);
        pnew_frags = 0;
    }
#if (INCLUDE_RTFS_FREEMANAGER == 0)
    if ((!is_prealloc) && (!pnew_frags)) {
        rtfs_set_errno(PENOSPC, __FILE__, __LINE__);
    }
#endif  /* INCLUDE_RTFS_FREEMANAGER == 0 */
    return(pnew_frags);
}

void _pc_efiliocom_append_clusters (DDRIVE *pdr, PC_FILE *pefile,FINODE *peffinode, REGION_FRAGMENT *pnew_frags, BOOLEAN is_prealloc, dword new_clusters, dword new_bytes)
{
    REGION_FRAGMENT *pfree_this_frag = 0;    /* If the chains are joined we free one list element */
	llword zero;


    RTFS_ARGSUSED_INT((int) is_prealloc); /* Not using, but nice to know in case we decide to treat pre-allocated cluster
                                             differently in the future. quiet compilers that complain about unused arguments */
#if (INCLUDE_EXFATORFAT64)
	zero.val64=0;
#endif
	zero.val32=0;

    if (!peffinode->e.x->pfirst_fragment)       /* List is empty, initialize */
    {
        peffinode->e.x->pfirst_fragment      =
        peffinode->e.x->plast_fragment       =
        pefile->fc.plus.pcurrent_fragment    = pnew_frags;
        peffinode->e.x->alloced_size_bytes   = zero;
        peffinode->e.x->alloced_size_clusters= 0;
        pefile->fc.plus.region_byte_base     = zero;
        pefile->fc.plus.file_pointer         = zero;
        peffinode->e.x->clusters_to_process  = 0;
        pefile->fc.plus.region_block_base    =  pc_cl2sector(pdr, pnew_frags->start_location);
        pc_pfinode_cluster(pdr, peffinode, pnew_frags->start_location);
    }
    else
    {
        /* Merge fragment list if the new fragment is continuous */
        if ((peffinode->e.x->plast_fragment->end_location+1) == pnew_frags->start_location)
        {
            peffinode->e.x->plast_fragment->end_location = pnew_frags->end_location;
            peffinode->e.x->plast_fragment->pnext = pnew_frags->pnext;
            pfree_this_frag = pnew_frags; /* release the first list element */
            /* pefile->fc.plus.pcurrent_fragment  is unchanged */
            /* pefile->fc.plus.region_byte_base is unchanged */
            /* pefile->fc.plus.region_block_base is unchanged */
        }
        else
        {
            peffinode->e.x->plast_fragment->pnext = pnew_frags;
            if (!is_prealloc)
            {   /* If not a pre-allocation we are at EOF and appending, so set the current pointer */
                pefile->fc.plus.region_byte_base      = pefile->fc.plus.file_pointer;
                pefile->fc.plus.region_block_base     = pc_cl2sector(pdr, pnew_frags->start_location);
                pefile->fc.plus.pcurrent_fragment     = pnew_frags;
            }
        }
    }
#if (INCLUDE_EXFATORFAT64)
	if (ISEXFATORFAT64(pdr))
			peffinode->e.x->alloced_size_bytes.val64 =  peffinode->e.x->alloced_size_bytes.val64+new_bytes;
    else
#endif
		 peffinode->e.x->alloced_size_bytes.val32 = truncate_32_sum(peffinode->e.x->alloced_size_bytes.val32, new_bytes);
    peffinode->e.x->alloced_size_clusters += new_clusters;
    /* go to the end of the chain */
    peffinode->e.x->plast_fragment = pc_end_fragment_chain(peffinode->e.x->plast_fragment);
    /* remove from freespace */
    pc_fraglist_remove_free_region(pdr, pnew_frags);
    if (pfree_this_frag)
    {
        pfree_this_frag->pnext = 0;
        pc_fraglist_frag_free(pfree_this_frag); /* release the first fragment */
    }
}

/* change last modified time and date */
static void _pc_efiliocom_queue_flush(PC_FILE *pefile,FINODE *peffinode)
{
    DATESTR crdate;
    pc_getsysdate(&crdate);
    /* Update fields in the file's directory entry */
    pefile->pobj->finode->fattribute |= ARCHIVE;
    pefile->pobj->finode->ftime = crdate.time;
    pefile->pobj->finode->fdate = crdate.date;
    pc_set_file_dirty(pefile, TRUE);
    /* Update fields in the current segment (only true if a 64 bit file) */
    if (pefile->pobj->finode != peffinode)
    {
       peffinode->fattribute |= ARCHIVE;
       peffinode->ftime = crdate.time;
       peffinode->fdate = crdate.date;
       peffinode->operating_flags |= FIOP_NEEDS_FLUSH;
    }
}
#endif /* Exclude from build if read only */
