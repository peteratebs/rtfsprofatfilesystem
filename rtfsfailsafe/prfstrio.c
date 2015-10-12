/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* PRFSTRIO32.C - Contains internal transaction file overwrite and
                 other transaction file routines.
    BOOLEAN _pc_efilio32_overwrite(PC_FILE *pefile, byte *pdata, dword n_bytes)
    BOOLEAN _pc_load_transaction_buffer(PC_FILE *pefile, dword clusterno)
    BOOLEAN _pc_check_transaction_buffer(PC_FILE *pefile, dword clusterno)
    void _pc_clear_transaction_buffer(PC_FILE *pefile)

*/

#include "rtfs.h"

#if (INCLUDE_FAILSAFE_CODE)
#if (INCLUDE_TRANSACTION_FILES)

static BOOLEAN _pc_load_transaction_buffer(PC_FILE *pefile, DDRIVE *pdr, dword clusterno);
BOOLEAN _pc_check_transaction_buffer(PC_FILE *pefile, dword clusterno);
static void _pc_clear_transaction_buffer(PC_FILE *pefile);

#if (INCLUDE_DEBUG_TEST_CODE)
extern dword debug_num_transaction_buffer_loads;
#endif


/*
   Perform a transactional overwrite of a section of a file

   pdata is always non zero otherwise pc_efilio32 exectues.

   This routine is called by _pc_efilio32_io() when the file is
   is opened in transaction mode and a section of a file that was
   already allocated is being overwritten.

   Overall structure:

   1. Allocate a new cluster chain to hold the new data bytes to be written
      and if the data is not cluster alligned additional data bytes in
      the non aligned clusters at the left and/or right boundaries of the
      overwrite region.
   2. Edit the file's cluster chain to unlink the current cluster chain from
      the overwrite region and replace it with the new chain.
   3. Queue the replaced cluster chain to be released next time the FAT is
      synchronized.
   4. Copy residual bytes in left side cluster if not aligned.
   5. Write users data to the region
   6. Copy residual bytes in right side cluster if not aligned.

*/

BOOLEAN _pc_efiliocom_overwrite(PC_FILE *pefile, FINODE *pefinode, byte *pdata, dword n_bytes)
{
DDRIVE *pdr;
dword initial_dstart,initial_file_size,alloced_size_bytes,bytes_to_eof,cluster_copy_from[2], bytes_copy_from[2],copy_one_start;
REGION_FRAGMENT *free_this_frag, *pfirst_replaced_frag, *plast_replaced_frag,
                *pfirst_replacement_frag,*plast_replacement_frag;
dword ltemp,cluster_before_replacement, initial_file_pointer, cl_offset;
dword first_cluster_in_file;
int is_error;
    free_this_frag = 0;

    if (!n_bytes)
        return(TRUE);

    /* We are overwriting preallocated space and may seek past the current
       file size so temporarilly set the file size to the allocated size */
    initial_dstart = pefinode->extended_attribute_dstart;
    pefinode->extended_attribute_dstart = 0;
    initial_file_size = FINODEFILESIZE(pefinode);
    FINODEFILESIZE(pefinode) = pefinode->e.x->alloced_size_bytes;
    alloced_size_bytes = pefinode->e.x->alloced_size_bytes;

    /* We'll be seeking around but over the content, not a logical
       range so clear dstart */
    initial_file_pointer = pefile->fc.plus.file_pointer;
    first_cluster_in_file = _pc_efilio_first_cluster(pefile);
    pdr = pefinode->my_drive;

    /* Left and right side clusters to merge on non-alligned writes */
    cluster_copy_from[0] = cluster_copy_from[1] = 0;
    bytes_copy_from[0] = bytes_copy_from[1] = 0;

    free_this_frag = 0;

    /* Make sure the start and end of region are on fragment boundaries
       and determine if first and or last clusters have to be copied */

    /* make sure the start of the region is on a fragment boundary */
    cl_offset = pc_byte2cloff(pdr, pefile->fc.plus.file_pointer - pefile->fc.plus.region_byte_base);
    if (cl_offset != 0)
    {
        if (!_pc_efiliocom_resize_current_fragment(pefile,pefinode, cl_offset))
            goto return_error;
    }
    if (pefile->fc.plus.file_pointer != pefile->fc.plus.region_byte_base)
    {   /* We know we have to copy the first cluster */
        cluster_copy_from[0] = pefile->fc.plus.pcurrent_fragment->start_location;
        bytes_copy_from[0]   = pefile->fc.plus.file_pointer - pefile->fc.plus.region_byte_base;
    }
    /* We're at the beginning of the region so save off the frags we are replacing */
    pfirst_replaced_frag = pefile->fc.plus.pcurrent_fragment;

    /* make sure the cluster containing the end of the region is on a fragment boundary */
    if (!_pc_efiliocom_lseek(pefile,pefinode, n_bytes-1, PSEEK_CUR, &ltemp))
        goto return_error;
    cl_offset = pc_byte2clmod(pdr, pefile->fc.plus.file_pointer - pefile->fc.plus.region_byte_base);
    if (!_pc_efiliocom_resize_current_fragment(pefile,pefinode, cl_offset))
        goto return_error;
    /* save the fragment pointer to the last fragment in the replacement range */
    plast_replaced_frag = pefile->fc.plus.pcurrent_fragment;

    /* How many residual bytes in the last fragment to write */
    /* Seek foreward one, will skip to the next fragment if we split at the file pointer */
    if (!_pc_efiliocom_lseek(pefile,pefinode, 1, PSEEK_CUR, &ltemp))
        goto return_error;
    /* get the number of bytes to the end of the current fragment */
    ltemp = pc_fragment_size_32(pdr,pefile->fc.plus.pcurrent_fragment);
    /* Double check .. won't happen */
    bytes_to_eof = alloced_size_bytes - pefile->fc.plus.region_byte_base;
    if (ltemp > bytes_to_eof)
        ltemp = bytes_to_eof;

    /* Offset to first byte to write */
    copy_one_start = (pefile->fc.plus.file_pointer - pefile->fc.plus.region_byte_base);
    /* Note: we moved the file pointer by one, so if it entered a new fragment
       we won't be copying any data. Data will only be copied if there are
       residual bytes in the last fragment */
    if (copy_one_start && (ltemp > copy_one_start))
    {
       bytes_copy_from[1] =
                ltemp - copy_one_start;
       cluster_copy_from[1] = pefile->fc.plus.pcurrent_fragment->end_location;

       ltemp = pdr->drive_info.bytespcluster;
       if (ltemp > bytes_to_eof)
           ltemp = bytes_to_eof;
       copy_one_start = ltemp - bytes_copy_from[1];
    }
    else
    {
        cluster_copy_from[1]=bytes_copy_from[1] = 0;
        copy_one_start = 0;
    }

    /* Allocate replacement chain */
    /* Allocate from the beginning of the file on up so we recycle replacement
       fragments from within our own file.  */
    ltemp = pc_byte2clmod(pdr, n_bytes+bytes_copy_from[0]+bytes_copy_from[1]);
    pfirst_replacement_frag = pc_region_file_find_free_chain(pdr, pefile->fc.plus.allocation_policy,
                                                            first_cluster_in_file, ltemp, &is_error);
    /* None found. pc_region_file_find_free_chain handled spillover */
    if (!pfirst_replacement_frag)
    {
        if (!is_error)
            rtfs_set_errno(PENOSPC, __FILE__, __LINE__);
        goto return_error;
    }
    /* Remember to free if we bail */
    free_this_frag = pfirst_replacement_frag;

    /* seek backward into the previous fragment (the last fragment before the splice) */
    if (!_pc_efiliocom_lseek(pefile,pefinode, initial_file_pointer, PSEEK_SET, &ltemp))
        goto return_error;
    if (pefile->fc.plus.region_byte_base)
    { /* There is a fragment preceeding the overwrite region */
        if (!_pc_efiliocom_lseek(pefile,pefinode, pefile->fc.plus.region_byte_base-1, PSEEK_SET, &ltemp))
            goto return_error;
        /* Link this to the next */
        cluster_before_replacement =
            pefile->fc.plus.pcurrent_fragment->end_location;
        pefile->fc.plus.pcurrent_fragment->pnext = pfirst_replacement_frag;
    }
    else
    { /* The is no fragment preceeding the overwrite region */
        pefinode->e.x->pfirst_fragment = pfirst_replacement_frag;
        cluster_before_replacement = 0;
        pc_pfinode_cluster(pdr, pefinode, pfirst_replacement_frag->start_location);
        pefinode->operating_flags |= FIOP_NEEDS_FLUSH;
        pc_set_file_dirty(pefile,TRUE);
    }
    /* Link the tail of the replacement fragments to what we are keeping */
    plast_replacement_frag = pc_end_fragment_chain(pfirst_replacement_frag);
    plast_replacement_frag->pnext = plast_replaced_frag->pnext;
    plast_replaced_frag->pnext = 0;
    free_this_frag = pfirst_replaced_frag;

    /* Now do the link this will write the new chain to the journalled
       view */
    if (!pc_frag_chain_delete_or_link(pdr,
                    FOP_RMTELL|FOP_LINK_NEXT|FOP_LINK,
                    pfirst_replacement_frag,
                    cluster_before_replacement,
                    pfirst_replacement_frag->start_location,
                    pc_fraglist_count_clusters(pfirst_replacement_frag, plast_replacement_frag)))
        goto return_error;
    /* Now zero the replaced clusters and tell the regin manager - this
       will write the zero valued chain to the journalled view and queue
       the clusters to be released when the FAT is synched. */
    if (!pc_frag_chain_delete_or_link(pdr,
                    FOP_RMTELL,
                    pfirst_replaced_frag,
                    0,
                    pfirst_replaced_frag->start_location,
                    pc_fraglist_count_clusters(pfirst_replaced_frag, 0)))
        goto return_error;

    /* We adjusted fragments so reset the seek system */
    _pc_efiliocom_reset_seek(pefile,pefinode);
    /* Since we have already written the linked chain, set last processed
       cluster to the end so we don't accidentally perform a deffered link */
    pefinode->e.x->last_processed_cluster = pefinode->e.x->plast_fragment->end_location;

    /* And seek back to where the merge or overwrite begins */
    if (!_pc_efiliocom_lseek(pefile,pefinode,
        initial_file_pointer - bytes_copy_from[0],PSEEK_SET, &ltemp))
        goto return_error;

    /* Disable TRANSACTION processing because we are calling _pc_efilio32_io() */
    pefile->fc.plus.allocation_policy &= ~PCE_TRANSACTION_FILE;
    /* Now copy leading data that is not fat bounded */
    if (cluster_copy_from[0])
    {
        if (!_pc_load_transaction_buffer(pefile, pdr, cluster_copy_from[0]))
            goto return_error;
        if (!_pc_efiliocom_io(pefile, pefinode, pefile->fc.plus.transaction_buffer, bytes_copy_from[0], FALSE, FALSE))
            goto return_error;
        UPDATE_FSRUNTIME_STATS(((FAILSAFECONTEXT *)pdr->drive_state.failsafe_context),transaction_buff_writes, 1)

        /* Merge the user's datat into the buffer unless we know we're just
           going to kill the buffer anyway. */
        if (cluster_copy_from[0] != cluster_copy_from[1])
        {  /* Merge user data imediatley after the bytes we just wrote */
        dword n_to_copy;

            n_to_copy = pdr->drive_info.bytespcluster - bytes_copy_from[0];
            if (n_to_copy > n_bytes)
                n_to_copy = n_bytes;
            copybuff(pefile->fc.plus.transaction_buffer+bytes_copy_from[0], /* To */
                     pdata, /* from */
                     (int) n_to_copy);
        }
    }
    /* Now write the user's data */
    if (!_pc_efiliocom_io(pefile, pefinode, pdata, n_bytes, FALSE,FALSE))
        goto return_error;
    /* Now copy trailing data that is not fat bounded */
    if (cluster_copy_from[1])
    {
    byte *p;
        if (!_pc_load_transaction_buffer(pefile, pdr, cluster_copy_from[1]))
            goto return_error;
       /* Write the residual */
        p = pefile->fc.plus.transaction_buffer + copy_one_start;
        if (!_pc_efiliocom_io(pefile, pefinode, p, bytes_copy_from[1], FALSE,FALSE))
            goto return_error;
        UPDATE_FSRUNTIME_STATS(((FAILSAFECONTEXT *)pdr->drive_state.failsafe_context),transaction_buff_writes, 1)
        /* Seek back to before the data we just wrote */
        if (!_pc_efiliocom_lseek(pefile,pefinode, bytes_copy_from[1], PSEEK_CUR_NEG, &ltemp))
            goto return_error;
        /* Merge the user's data into the buffer unless we know we already did */
        if (cluster_copy_from[0] == cluster_copy_from[1])
            ; /* Already done */
        else if (cluster_copy_from[1])
        {  /* Merge user data */
           /* We know that the user data extends to the left
              side of the buffer, if it doesn't then
              (cluster_copy_from[0] equals cluster_copy_from[1])
              and we already handled it */
            copybuff(pefile->fc.plus.transaction_buffer,             /* To */
                     pdata + (n_bytes - copy_one_start), /* from */
                     copy_one_start); /* Amount */
        }

    }
    pc_fraglist_free_list(free_this_frag); /* Free fragments if instructed */
    pefile->fc.plus.allocation_policy |= PCE_TRANSACTION_FILE;
    FINODEFILESIZE(pefinode) = initial_file_size; /* restore file size it will be adjusted above */
    pefinode->extended_attribute_dstart = initial_dstart;
    return(TRUE);
return_error:
    /* If an error, reset seek and go back to where we started */
    _pc_clear_transaction_buffer(pefile);
    pefile->fc.plus.allocation_policy |= PCE_TRANSACTION_FILE;
    FINODEFILESIZE(pefinode) = initial_file_size; /* restore file size it will be adjusted above */
    _pc_efiliocom_reset_seek(pefile,pefinode);
    _pc_efiliocom_lseek(pefile, pefinode, initial_file_pointer,PSEEK_SET, &ltemp);
    pefinode->extended_attribute_dstart = initial_dstart;
    pc_fraglist_free_list(free_this_frag); /* Free fragments if instructed */
    return(FALSE);
}



BOOLEAN _pc_check_transaction_buffer(PC_FILE *pefile, dword clusterno)
{
    if (pefile->fc.plus.current_transaction_cluster == clusterno)
    {
        UPDATE_FSRUNTIME_STATS(((FAILSAFECONTEXT *)pefile->pobj->pdrive->drive_state.failsafe_context), transaction_buff_hits, 1)
        return(TRUE);
    }
    else
        return(FALSE);
}

static BOOLEAN _pc_load_transaction_buffer(PC_FILE *pefile, DDRIVE *pdr, dword clusterno)
{
    /* If already buffered... good */
    if (pefile->fc.plus.current_transaction_cluster == clusterno)
    {
        UPDATE_FSRUNTIME_STATS(((FAILSAFECONTEXT *)pdr->drive_state.failsafe_context), transaction_buff_hits, 1)
        return(TRUE);
    }

    /* Otherwise store the address and read it */
    pefile->fc.plus.current_transaction_cluster = clusterno;
    pefile->fc.plus.current_transaction_blockno = pc_cl2sector(pdr, clusterno);
    if (raw_devio_xfer(pdr,
            pefile->fc.plus.current_transaction_blockno,
            pefile->fc.plus.transaction_buffer, pdr->drive_info.secpalloc, FALSE, TRUE) )
    {
#if (INCLUDE_DEBUG_TEST_CODE)
        debug_num_transaction_buffer_loads += 1;
#endif
        UPDATE_FSRUNTIME_STATS(((FAILSAFECONTEXT *)pdr->drive_state.failsafe_context), transaction_buff_reads, 1)
        return(TRUE);
    }
    else
        return(FALSE);
}

static void _pc_clear_transaction_buffer(PC_FILE *pefile)
{
    pefile->fc.plus.current_transaction_cluster = 0;
}
#endif /* (INCLUDE_TRANSACTION_FILES) */
#endif /* (INCLUDE_FAILSAFE_CODE) */
