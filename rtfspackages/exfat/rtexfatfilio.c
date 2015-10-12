/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* RTEXFATFILIO.C - Functions to extend basic file IO for exFat 64 bit.

    Routines Exported from this file include:

*/

#include "rtfs.h"
static BOOLEAN _pc_efiliocom_move_fp64(ddword file_size, ddword file_pointer, ddword offset, int origin, ddword *pnewoffset);
dword pc_bfilio_alloc_chain(DDRIVE *pdrive, dword alloc_start_hint, dword chain_size, dword *pstart_cluster, int *p_is_error);
BOOLEAN pcexfat_fraglist_fat_free_list(FINODE *pfi, REGION_FRAGMENT *pf);
BOOLEAN _pcexfat_bfilio_reduce_size(PC_FILE *pefile, ddword new_size);
static BOOLEAN _pcexfat_bfilio_increase_size(PC_FILE *pefile, ddword new_size);
dword pcexfat_finode_length_clusters(FINODE *pefinode);
ddword pc_byte2ddwclmodbytes(DDRIVE *pdr, ddword nbytes64);
static BOOLEAN _pc_bfilio_io64(PC_FILE *pefile, byte *pdata, ddword n_bytes, BOOLEAN reading, BOOLEAN appending);
static BOOLEAN pc_bfilio_load_fragments_until64(FINODE *pefinode, ddword offset);
static ddword pc_alloced_bytes_from_clusters64(DDRIVE *pdr, dword total_alloced_clusters);

#if (!INCLUDE_RTFS_PROPLUS)
/* read from a file using the pc_bfilio method, called by po_read() */
int pcexfat_bfilio_read(PC_FILE *pefile, byte *in_buff, int count)
{
int ret_val = -1;

    {
    ddword n_left, n_left_file, n_bytes;

     ret_val = 0;
    /* Turn count into ddword */
	n_bytes = M64SET32(0, (dword) count);
    n_left = n_bytes;

    if (M64LT(pefile->fc.basic.file_pointer64, pefile->pobj->finode->fsizeu.fsize64))
        n_left_file = M64MINUS(pefile->pobj->finode->fsizeu.fsize64, pefile->fc.basic.file_pointer64);
    else
        n_left_file = M64SET32(0, 0);
    if (M64GT(n_left , n_left_file))
        n_left = n_left_file;
    if (M64NOTZERO(n_left))
    {   /* pc_bfilio_io reads the data and updates the file pointer */
           if (_pc_bfilio_io64(pefile, in_buff, n_left, TRUE, FALSE))
		   {
                ret_val = (int) M64LOWDW(n_left);
                pc_update_finode_datestamp(pefile->pobj->finode, FALSE, DATESETACCESS);
		   }
            else
                ret_val = -1;
        }
    }
    return(ret_val);
}


BOOLEAN pcexfat_bpefile_ulseek(PC_FILE *pefile, ddword offset, ddword *pnew_offset, int origin)
{
BOOLEAN ret_val = FALSE;
ddword newoffset;

    /* Move and query the file pointer */
    ret_val = _pc_efiliocom_move_fp64(pefile->pobj->finode->fsizeu.fsize64, pefile->fc.basic.file_pointer64, offset, origin, &newoffset);
    if (ret_val)
    {
        if (M64NOTZERO(newoffset))  /* Now check the chain */
        {
            if (M64EQ(newoffset, pefile->pobj->finode->fsizeu.fsize64))
			{
				ddword newoffsetminus = M64MINUS32(newoffset, 1);
                ret_val = pc_bfilio_load_fragments_until64(pefile->pobj->finode, newoffsetminus);
			}
            else
                ret_val = pc_bfilio_load_fragments_until64(pefile->pobj->finode, newoffset);
        }
        if (ret_val)
        {
            *pnew_offset = newoffset;
            pefile->fc.basic.file_pointer64 = newoffset;
        }
    }
    return(ret_val);
}


/* Move the file pointer using seek rules between data_start (0) and the file size
   does not worry about cluster chains, that is done at a higher level */
static BOOLEAN _pc_efiliocom_move_fp64(ddword file_size, ddword file_pointer, ddword offset, int origin, ddword *pnewoffset)
{
ddword start_pointer, new_file_pointer;

    *pnewoffset = file_pointer;
    if (origin == PSEEK_SET)        /*  offset from begining of data */
		start_pointer = M64SET32(0, 0);
    else if (origin == PSEEK_CUR)   /* offset from current file pointer */
        start_pointer = file_pointer;
    else if (origin == PSEEK_CUR_NEG)  /* negative offset from current file pointer */
        start_pointer = file_pointer;
    else if (origin == PSEEK_END)   /*  offset from end of file */
    {
       start_pointer = file_size;
    }
    else
    {
        rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
        return(FALSE);
    }

    if (origin == PSEEK_CUR_NEG || origin == PSEEK_END)
    {
        if (M64GT(start_pointer, offset))
        	new_file_pointer = M64MINUS(start_pointer,offset);
        else
           	new_file_pointer =  M64SET32(0, 0);
    }
    else
    {
        new_file_pointer = M64PLUS(start_pointer,offset);
        if (M64GT(new_file_pointer, file_size))
            new_file_pointer = file_size; /* truncate to end */
    }
    *pnewoffset = new_file_pointer;
    return(TRUE);
}






/*
   Read or write n_bytes at the current file pointer
   The the size has already been verified and the cluster
   chain should contain at least n_bytes at the file pointer.
   If not it is an error
*/
static BOOLEAN _pc_bfilio_io64(PC_FILE *pefile, byte *pdata, ddword n_bytes, BOOLEAN reading, BOOLEAN appending)
{
ddword n_left, n_todo, file_pointer,region_byte_next,file_region_byte_base, file_region_block_base;
int at_eor;
REGION_FRAGMENT *file_pcurrent_fragment;
DDRIVE *pdrive;
FINODE *pefinode;


    if (M64ISZERO(n_bytes))
        return(TRUE);
    /* use local copies of file info so if we fail the pointers will not advance */
    file_pointer  = pefile->fc.basic.file_pointer64;
    pefinode = pefile->pobj->finode;
    pdrive = pefinode->my_drive;

    n_left = n_bytes;
    /* load cluster chain to include all bytes (up to filepointer+(nbytes-1)) we will read or write, they should all be there */
    {
        ddword new_offset;
        new_offset = M64PLUS(file_pointer, n_left);
        new_offset = M64MINUS32(new_offset, 1);
        if (!pc_bfilio_load_fragments_until64(pefinode, new_offset))
		{
            return(FALSE); /* load_efinode_fragments_until has set errno */
		}
    }
    file_pcurrent_fragment      = pefinode->pbasic_fragment;
    file_region_byte_base       = M64SET32(0,0);
    /* Get the block base */
    while (file_pcurrent_fragment && M64LT(file_region_byte_base,file_pointer))
    {
        region_byte_next = M64PLUS32(file_region_byte_base, pc_fragment_size_32(pdrive, file_pcurrent_fragment));
        if (M64GT(region_byte_next,file_pointer))
            break;
        file_region_byte_base  = region_byte_next;
        file_pcurrent_fragment = file_pcurrent_fragment->pnext;
    }
    if (!file_pcurrent_fragment)
    {  /* File pointer not in the chain */
        rtfs_set_errno(PEINVALIDCLUSTER, __FILE__, __LINE__);
        return(FALSE);
    }
    file_region_block_base  = M64SET32(0,pc_cl2sector(pdrive, file_pcurrent_fragment->start_location));
    region_byte_next        = M64PLUS32(file_region_byte_base, pc_fragment_size_32(pdrive, file_pcurrent_fragment));
    while (M64NOTZERO(n_left))
    {
        at_eor = 0;
        if (M64LOWDW(file_pointer) & pdrive->drive_info.bytemasksec)
        {   /* fp not on a sector boundary */
            n_todo = M64SET32(0, pdrive->drive_info.bytespsector - (M64LOWDW(file_pointer) & pdrive->drive_info.bytemasksec));
            if (M64GT(n_todo,n_left))
                n_todo = n_left;
        }
        else
        {
            /* mask off end if not on a sector boundary */
        	n_todo = M64SET32(M64HIGHDW(n_left), (M64LOWDW(n_left) & ~pdrive->drive_info.bytemasksec));
            if (M64ISZERO(n_todo))
                n_todo = n_left;
        }
        {
        ddword new_file_pointer;
            new_file_pointer = M64PLUS(file_pointer,n_todo);
            if (M64GTEQ(new_file_pointer,region_byte_next))
            {
                new_file_pointer = region_byte_next;
                n_todo = M64MINUS(new_file_pointer,file_pointer);
                at_eor = 1;
            }
        }
        if (pdata)
        {
            /* call pc_buffered_fileio() which buffers the data if necessary  */
            dword start_byte_offset, start_sector;
			ddword offsetinregion,start_sectorddw;
            /* Calculate the starting sector and byte offset into that sector of the file pointer  */
            /* start_sector = file_region_block_base + ((file_pointer - file_region_byte_base)>>pdrive->drive_info.log2_bytespsec); */
            offsetinregion = M64MINUS(file_pointer, file_region_byte_base);
            offsetinregion = M64RSHIFT(offsetinregion, pdrive->drive_info.log2_bytespsec);
            start_sectorddw = M64PLUS(file_region_block_base,offsetinregion);

            start_sector = M64LOWDW(start_sectorddw);
            start_byte_offset = (M64LOWDW(file_pointer) & pdrive->drive_info.bytemasksec);

            if (!pc_buffered_fileio(pefinode, start_sector, start_byte_offset, M64LOWDW(n_todo), pdata,  reading, appending))
			{
				return(FALSE);
			}
            pdata += M64LOWDW(n_todo);
        }
        file_pointer = M64PLUS(file_pointer,n_todo);
        n_left = M64MINUS(n_left, n_todo);
        if (M64NOTZERO(n_left))
        {
            if (at_eor)
            { /* Should clusters in more fragments otherwise we are just advancing to a block boundary */
                if (file_pcurrent_fragment->pnext)
                {
                    file_pcurrent_fragment      = file_pcurrent_fragment->pnext;
                    file_region_byte_base       = region_byte_next;
                    region_byte_next            = M64PLUS32(file_region_byte_base, pc_fragment_size_32(pdrive, file_pcurrent_fragment));
                    file_region_block_base      = M64SET32(0,pc_cl2sector(pdrive, file_pcurrent_fragment->start_location));
                }
                else
                {
                    rtfs_set_errno(PEINVALIDCLUSTER, __FILE__, __LINE__);
                    return(FALSE);
                }
            }
        }
    }
    /* everything is good so update the file structure */
    pefile->fc.basic.file_pointer64      =    file_pointer;
    return(TRUE);
}


/* Write to a file using the pc_bfilio method, implementation */
BOOLEAN pcexfat_bfilio_write(PC_FILE *pefile, byte *in_buff, dword n_bytes, dword *pnwritten)
{
BOOLEAN ret_val = FALSE;
BOOLEAN appending;
dword n_left,file_chain_length;
ddword required_size_bytes, file_chain_length_bytes;
FINODE *pefinode;
DDRIVE *pdrive;

    *pnwritten = 0;
    if (!n_bytes)
        return(TRUE);

    pefinode = pefile->pobj->finode;
    pdrive = pefile->pobj->pdrive;


    /* If append is requested, call internal seek routine to seek to the end */
    if (pefile->flag & PO_APPEND)
    {
        ddword zeroddw, newoffset;

		zeroddw = M64SET32(0,0);
        if (!pcexfat_bpefile_ulseek(pefile, zeroddw, &newoffset, PSEEK_END))
            return(FALSE);
    }
    n_left = n_bytes;
    /* make sure the cluster chain is loaded and test if we are appending to the file */
    appending = FALSE;
    if (M64NOTZERO(pefinode->fsizeu.fsize64))
    {
    ddword new_size,last_byte_offset;
        new_size = M64PLUS32(pefile->fc.basic.file_pointer64,n_left);
        if (M64GT(new_size, pefinode->fsizeu.fsize64))
        {
            appending = TRUE;
            last_byte_offset = M64MINUS32(pefinode->fsizeu.fsize64,1);
        }
        else
            last_byte_offset = M64MINUS32(new_size,1);

        if (!pc_bfilio_load_fragments_until64(pefinode, last_byte_offset))
		{
			return(FALSE);
		}
    }

    required_size_bytes =  M64PLUS32(pefile->fc.basic.file_pointer64,n_left);
	file_chain_length = pcexfat_finode_length_clusters(pefinode);
	file_chain_length_bytes = pc_alloced_bytes_from_clusters64(pefinode->my_drive, file_chain_length);
    while (M64GT(required_size_bytes, file_chain_length_bytes))
    { /* Append clusters to the file */
    	dword alloc_hint, new_clusters_required, n_alloced, start_cluster;
    	ddword new_bytes_required;
    	REGION_FRAGMENT *pend;
    	int is_error;

        new_bytes_required = M64MINUS(required_size_bytes, file_chain_length_bytes);
        new_clusters_required = pc_byte2clmod(pdrive, M64LOWDW(new_bytes_required));

        if (file_chain_length)
		{
            pend = pc_end_fragment_chain(pefinode->pbasic_fragment);
            alloc_hint = pend->end_location;
		}
        else
		{
			pend = 0;
            alloc_hint = 0;
		}
        is_error = 0;
        n_alloced = pc_bfilio_alloc_chain(pdrive, alloc_hint, new_clusters_required , &start_cluster, &is_error);
        if (is_error)
		{
            return(FALSE);
		}
        if (!n_alloced)
			break;
#if (EXFAT_FAVOR_CONTIGUOUS_FILES) /* pcexfat_bfilio_write update free_contig_pointer to allocate file data from if necessary */
        if (start_cluster >= pdrive->drive_info.free_contig_pointer)
        	pdrive->drive_info.free_contig_pointer = start_cluster + n_alloced;
        if (pdrive->drive_info.free_contig_pointer >= pdrive->drive_info.maxfindex)
        	pdrive->drive_info.free_contig_pointer = pdrive->drive_info.free_contig_base;
#endif
		/* Release the clusters frpom the free manager/bit map */
   		if (!exfatop_remove_free_region(pefinode->my_drive, start_cluster, n_alloced))
			return(FALSE);
        if (!file_chain_length) /* Set the start cluster in the entry */
		{
            pc_pfinode_cluster(pdrive, pefinode, start_cluster);
		}
		else
		{
			if (alloc_hint+1 != start_cluster)
			{
				if (pefinode->exfatinode.GeneralSecondaryFlags & EXFATNOFATCHAIN)
				{	/* Switch to non-contiguous file, clear the flag and link the current segment list into a cluster chain */
					pefinode->exfatinode.GeneralSecondaryFlags &= ~EXFATNOFATCHAIN;
		            pc_set_file_dirty(pefile, TRUE);
					if (!pcexfat_link_file_chain(pdrive, pefinode->pbasic_fragment))
					{
						return(FALSE);
					}
				}
			}
		}
		/* If the file still contiguous count the latest segment and keep going */
        if (pefinode->exfatinode.GeneralSecondaryFlags & EXFATNOFATCHAIN)
		{
			ddword savedSize64;
			/* temporarily increase the size and expand the chain, then restore the size  */
			savedSize64 = pefinode->fsizeu.fsize64;
			pefinode->fsizeu.fsize64 = pc_alloced_bytes_from_clusters64(pefinode->my_drive, file_chain_length + n_alloced);
			if (!pcexfat_expand_nochain(pefinode))
			{
				pefinode->fsizeu.fsize64 = savedSize64;
				return(FALSE);
			}

			pefinode->fsizeu.fsize64 = savedSize64;
		}
		else
		{
			REGION_FRAGMENT *pf;/* Not contiguous so link the new fragments into the chain */
			BOOLEAN r;
			/* Link us into a chain using a temporary fragment */
			pf = pc_fraglist_frag_alloc(pdrive, start_cluster, start_cluster+n_alloced-1, 0);
			if (!pf)
			{
				return(FALSE);
			}
			/* Link the new fragment together in the fat */
			r = pcexfat_link_file_chain(pdrive, pf);
			pc_fraglist_frag_free(pf);
			if (!r)
			{
				return(FALSE);
			}
			/* Link the previous end in the fat chain to the the new chain */
			if (pend)
			{
	            if (!fatop_pfaxx(pdrive,  pend->end_location, start_cluster))
				{
					return(FALSE);
				}
			}
			/* Now grow the fragment list in the finode */
			if (!pc_grow_basic_fragment(pefinode, start_cluster, n_alloced))
			{
				return(FALSE);
			}
		}
		file_chain_length += n_alloced;
		file_chain_length_bytes = pc_alloced_bytes_from_clusters64(pefinode->my_drive, file_chain_length);
	}
    /* See if we got some clusters but not all .. (short write) */
    if (M64LT(file_chain_length_bytes, required_size_bytes))
	{
		ddword ltempddw;
        ltempddw = M64MINUS(required_size_bytes, file_chain_length_bytes);
        n_left -= M64LOWDW(ltempddw);
    }
    if (n_left)
    {
    ddword savedSize64;
		savedSize64 = pefinode->fsizeu.fsize64;
		pefinode->fsizeu.fsize64 = file_chain_length_bytes;
		/* pc_bfilio_io writes the data and updates the file pointer */
        if (_pc_bfilio_io64(pefile, in_buff, M64SET32(0,n_left), FALSE, appending))
        {
            *pnwritten = n_left;
			pefinode->fsizeu.fsize64 = savedSize64;
            if (M64GT(pefile->fc.basic.file_pointer64, pefinode->fsizeu.fsize64))
            {
                pefinode->fsizeu.fsize64 = pefile->fc.basic.file_pointer64;
            }
            pc_set_file_dirty(pefile, TRUE);
            ret_val = TRUE;
        }
        else
		{
			pefinode->fsizeu.fsize64 = savedSize64;
            ret_val = FALSE;
		}
    }
    else
        ret_val = TRUE; /* No space left, wrote zero byes, but not an error */
    return(ret_val);
}

BOOLEAN pcexfat_bfilio_chsize(PC_FILE *pefile, ddword new_size)
{
BOOLEAN ret_val = FALSE;

    if (pefile)
    {
        ddword old_size;
        old_size = pefile->pobj->finode->fsizeu.fsize64;
        /* Flush the file first */
        if (_pcexfat_bfilio_flush(pefile))
        {
            if (M64GT(old_size,new_size))
            {   /* Truncate: Can only truncate a file that you hold exclusively   */
                if (pefile->pobj->finode->opencount > 1)
                {
                    rtfs_set_errno(PEACCES, __FILE__, __LINE__);
                    ret_val = FALSE;
                }
                else
				{
                   	ret_val = _pcexfat_bfilio_reduce_size(pefile, new_size);
				}
            }
            else if (M64LT(old_size,new_size))
            {   /* Expand: _pc_bfilio_increase_size uses _pc_bfilio_write() to extend the file */
                ret_val = _pcexfat_bfilio_increase_size(pefile, new_size);
                if (!ret_val)
                { /* revert to original size if we ran out of space */
                    if (get_errno() == PENOSPC || get_errno() == PENOEMPTYERASEBLOCKS)
                    {
                     	/* Added november 2009. return FALSE and do not clear errno */
                        ret_val = FALSE;
                        _pcexfat_bfilio_reduce_size(pefile, old_size);
                    }
                }
            }
            else
                ret_val = TRUE; /* Unchanged */
        }
        /* Flush any changes */
        if (!_pcexfat_bfilio_flush(pefile))
            ret_val = FALSE;
    }
    return(ret_val);
}



/* Flush a file using the pc_bfilio method, implementation called by pc_bfilio_flush and pc_bfilio_close */
BOOLEAN _pcexfat_bfilio_flush(PC_FILE *pefile)
{
    if (!pc_flush_file_buffer(pefile->pobj->finode))
        return(FALSE);
    if (pc_check_file_dirty(pefile))
    {
    	pcexfat_set_volume_dirty(pefile->pobj->pdrive);
    	/* Overwrite the existing inode.Set archive/date, flush the FAT  */
    	if (fatop_flushfat(pefile->pobj->pdrive))
    	{
       		if (pc_update_inode(pefile->pobj, FALSE, 0)) /* Flush the finode, don't force date field updates, they were done when dirty was set */
       		{
               	pc_set_file_dirty(pefile, FALSE);
               	pcexfat_clear_volume_dirty(pefile->pobj->pdrive);
               	return(TRUE);
           }
        }
        return(FALSE);
    }
    return(TRUE);
}




/* Truncate a file using the pc_bfilio method, implementation called by pc_bfilio_open and pc_bfilio_chsize */
BOOLEAN _pcexfat_bfilio_reduce_size(PC_FILE *pefile, ddword new_size)
{
FINODE *pefinode;
dword first_cluster_to_delete,new_end_cluster,residual_discard_start, residual_discard_end;
REGION_FRAGMENT *pfdiscard = 0;

    pefinode = pefile->pobj->finode;
    new_end_cluster = first_cluster_to_delete = 0;
	residual_discard_start = residual_discard_end = 0;
	/* Load up the fragment chain */
	if (M64NOTZERO(pefinode->fsizeu.fsize64) && pc_finode_cluster(pefinode->my_drive, pefinode))
	{
		if (!pcexfat_bfilio_load_all_fragments(pefinode))
		{
			return(FALSE);
		}
	}

    if (M64ISZERO(new_size))
    {
        /* new_end_cluster is zero, first_cluster_to_delete is the first cluster in the file */
		pfdiscard = pefinode->pbasic_fragment;
		pefinode->pbasic_fragment = 0;
        pc_pfinode_cluster(pefile->pobj->pdrive, pefinode, 0);
    }
    else
    {
        REGION_FRAGMENT *file_pcurrent_fragment,*file_prev_fragment;
        dword new_size_clusters;
        dword chain_size_clusters;
        /* Find the the new end cluster and the first cluster in the rest of the chain to delete */
		file_prev_fragment = 0;
        file_pcurrent_fragment = pefinode->pbasic_fragment;
        chain_size_clusters = 0;
		/* Calculate the new truncated size in clusters */
		{
		ddword new_size_clusters_ddw;
		new_size_clusters_ddw =  pc_byte2ddwclmodbytes(pefile->pobj->pdrive, new_size);
		new_size_clusters_ddw = M64RSHIFT(new_size_clusters_ddw, pefile->pobj->pdrive->drive_info.log2_bytespalloc);

		new_size_clusters = M64LOWDW(new_size_clusters_ddw);
		}
        while (file_pcurrent_fragment)
        {
            dword new_chain_size_clusters;
            new_chain_size_clusters = chain_size_clusters + PC_FRAGMENT_SIZE_CLUSTERS(file_pcurrent_fragment);
			/*
				new_size_clusters is between chain_size_clusters (the length at the previous entry) and new_chain_size_clusters
				the (the length at the end of this entry) so subtract (new_size_clusters - chain_size_clusters) and add it to the start
				of the current segment to get the new length
			    | chain_size_clusters  |
			    x----------------------x----------x
			    | new_size_clusters        |
			    | new_chain_size_clusters         |
			*/
            if (new_chain_size_clusters > new_size_clusters)
            { /* delete all or part of the current fragment */
                first_cluster_to_delete = file_pcurrent_fragment->start_location +
                                            (new_size_clusters - chain_size_clusters);
                /* Remember the new end of file if it is inside this fragment
                   We saved the end of the previous fragment in case we are deleting this
                   whole fragment */
                if (first_cluster_to_delete == file_pcurrent_fragment->start_location)
				{
                    if (file_prev_fragment)
                    	file_prev_fragment->pnext = 0;
                    pfdiscard = file_pcurrent_fragment;
					residual_discard_start = residual_discard_end = 0;

				}
                else
				{
                    new_end_cluster = first_cluster_to_delete - 1;
					residual_discard_start = first_cluster_to_delete;
					residual_discard_end   = file_pcurrent_fragment->end_location;
					file_pcurrent_fragment->end_location = new_end_cluster;
                    pfdiscard = file_pcurrent_fragment->pnext;
                    file_pcurrent_fragment->pnext = 0;
				}
                break;
            }
            chain_size_clusters = new_chain_size_clusters;
            /* Save the end location of the current fragment. It will be the new end of file if
               chain_size_clusters == new_size_clusters. (first_cluster_to_delete will be the
               start of the next fragment) */
            new_end_cluster = file_pcurrent_fragment->end_location;
            file_prev_fragment = file_pcurrent_fragment;
            file_pcurrent_fragment = file_pcurrent_fragment->pnext;
        }
    }
    pc_set_file_dirty(pefile, TRUE);
    /* if the fp is out of bound put it at the end of file */
    if (M64GT(pefile->fc.basic.file_pointer64, new_size))
        pefile->fc.basic.file_pointer64 = new_size;

	if (residual_discard_start)
	{ /* Discard residual clusters in the last fragment */
		REGION_FRAGMENT *pfrag;
		pfrag = pc_fraglist_frag_alloc(pefile->pobj->pdrive, residual_discard_start,residual_discard_end, 0);
		if (!pcexfat_fraglist_fat_free_list(pefile->pobj->finode, pfrag))
		{
        	if (pfdiscard)
				pc_fraglist_free_list(pfdiscard);
        	pc_fraglist_free_list(pfrag);
			return(FALSE);
		}
        pc_fraglist_free_list(pfrag);
	}
    if (pfdiscard)
	{ /* Discard clusters in the chains after the last fragment */
		if (!pcexfat_fraglist_fat_free_list(pefile->pobj->finode, pfdiscard))
		{
        	pc_fraglist_free_list(pfdiscard);
			return(FALSE);
		}
        pc_fraglist_free_list(pfdiscard);
	}
    /* Update the file size */
    pefinode->fsizeu.fsize64 = new_size;
	/* Set it dirty and flush the file. This will update the Fat values and do the right thing with the chain */
	pc_set_file_dirty(pefile, TRUE);
	if (!_pcexfat_bfilio_flush(pefile))
		return(FALSE);
	/* If we have a new end and we are not contiguous then terminate the FAT chain */
    if (new_end_cluster && (pefinode->exfatinode.GeneralSecondaryFlags & EXFATNOFATCHAIN) == 0)
	{
   		if (!fatop_pfaxxterm(pefinode->my_drive, new_end_cluster))
   			return(FALSE);
	}
	return(TRUE);
}

/* Expand a file using the pc_bfilio method, implementation called by pc_bfilio_chsize */
static BOOLEAN _pcexfat_bfilio_increase_size(PC_FILE *pefile, ddword new_size)
{
FINODE *pefinode;
BOOLEAN ret_val;
ddword n_left, saved_file_pointer;

    pefinode = pefile->pobj->finode;
    saved_file_pointer = pefile->fc.basic.file_pointer64;
    pefile->fc.basic.file_pointer64 = pefinode->fsizeu.fsize64;

    n_left = M64MINUS(new_size,pefinode->fsizeu.fsize64);
    ret_val = TRUE;
    while (M64NOTZERO(n_left) && ret_val)
    {
		dword n_left32,n_written;
		if (M64HIGHDW(n_left))
			n_left32 = 0x80000000;
		else
			n_left32 = M64LOWDW(n_left);

        if (!pcexfat_bfilio_write(pefile, 0, n_left32, &n_written))
        {
            ret_val = FALSE;
        }
        if (n_left32 != n_written)
        {
            ret_val = FALSE;
        }
		n_left = M64MINUS32(n_left,n_left32);
    }
    pefile->fc.basic.file_pointer64 = saved_file_pointer;
	/* Set it dirty and flush the file. This will update the Fat values and do the right thing with the chain */
    if (ret_val)
	{
		pc_set_file_dirty(pefile, TRUE);
		ret_val = _pcexfat_bfilio_flush(pefile);
	}
	return(ret_val);
}

/* Helper functions used by ProPlus and Pro */
static ddword pc_alloced_bytes_from_clusters64(DDRIVE *pdr, dword total_alloced_clusters)
{
    ddword alloced_size_bytes,ltempddw;
	ltempddw = M64SET32(0,total_alloced_clusters);
    alloced_size_bytes    = M64LSHIFT(ltempddw, pdr->drive_info.log2_bytespalloc);
    return(alloced_size_bytes);
}
#endif /* (!INCLUDE_RTFS_PROPLUS) */

static BOOLEAN pc_exfat_load_fragments_until_by_cluster(FINODE *pefinode, dword clusters_required)
{
dword loaded_chain_length, cluster_number, next_cluster;
REGION_FRAGMENT *pend_fragment;
DDRIVE *pdrive;
dword max_cluster_per_fragment;
	rtfs_clear_errno();
    pdrive = pefinode->my_drive;

    if (pefinode->exfatinode.GeneralSecondaryFlags & EXFATNOFATCHAIN)
	{	/* Build a fragment list of segments (each < 4 GIGABYTES) */
		if (!pcexfat_expand_nochain(pefinode))
			return(FALSE);
		loaded_chain_length = pc_fraglist_count_clusters(pefinode->pbasic_fragment,0);
		if (loaded_chain_length < clusters_required)
		{
			rtfs_set_errno(PEINVALIDCLUSTER, __FILE__, __LINE__);
			return(FALSE);
		}
		else
			return(TRUE);
	}
	/* If it is exFat with a cluster chain or it is fat  */
	/* The file contains a cluster chain, exFAT NOFATCHAIN is true entries were processed above */
	max_cluster_per_fragment = 1;
    max_cluster_per_fragment <<= (32-pdrive->drive_info.log2_bytespalloc);
	max_cluster_per_fragment -= 1;

	/* Get the index of the first cluster in our chain that is not in our fragment list */
	loaded_chain_length = 0;
	pend_fragment = 0;
    if (pefinode->pbasic_fragment)
    {
        loaded_chain_length = pc_fraglist_count_clusters(pefinode->pbasic_fragment,0);
        pend_fragment = pc_end_fragment_chain(pefinode->pbasic_fragment);
        if (loaded_chain_length >= clusters_required)
            return(TRUE);
		/* Get the value of the cluster stored at end_location. This points to the next segment */
   		{
		dword n_clusters,_next_cluster;
		int   end_of_chain;
			n_clusters = exFatfatop_getfile_frag(pefinode, pend_fragment->end_location, &_next_cluster, 1, &end_of_chain);
			if (n_clusters == 0)
				goto error_exit;
			else /* if (n_clusters == 1) */
			{
				cluster_number =  _next_cluster;
				if (end_of_chain)
					cluster_number = 0;
			}

   		}
    }
    else
    {
        pend_fragment = 0;
        loaded_chain_length = 0;
        cluster_number = pc_finode_cluster(pdrive, pefinode);
    }
    while (cluster_number && loaded_chain_length < clusters_required)
    {
    dword n_clusters;
    int   end_of_chain;
        end_of_chain = 0;
        	n_clusters = exFatfatop_getfile_frag(pefinode, cluster_number, &next_cluster, pdrive->drive_info.maxfindex, &end_of_chain);
        if (!n_clusters) /* Only happens on an error */
        	goto error_exit;
        if (!pc_grow_basic_fragment(pefinode, cluster_number, n_clusters))
        	goto error_exit;
        loaded_chain_length += n_clusters;
        if (end_of_chain)
            cluster_number = 0;
        else
            cluster_number = next_cluster;
    }
    if (loaded_chain_length < clusters_required)
    {
        rtfs_set_errno(PEINVALIDCLUSTER, __FILE__, __LINE__);
        return(FALSE);
    }
    else
        return(TRUE);

error_exit:
		if (!get_errno())
        	rtfs_set_errno(PEINVALIDCLUSTER, __FILE__, __LINE__);
        return(FALSE);
}
/* Make sure the bfile's fragment list is loaded to include at least byte_offset.
   If it does not it is an error caused by invalid cluster chains */
static BOOLEAN pc_bfilio_load_fragments_until64(FINODE *pefinode, ddword byte_offset)
{
dword clusters_required;
ddword byte_offset_endcluster,clusters_requiredddw;
DDRIVE *pdrive;

    pdrive 	= pefinode->my_drive;
	byte_offset_endcluster =  pc_byte2ddwclmodbytes(pdrive, byte_offset);
	clusters_requiredddw = M64RSHIFT(byte_offset_endcluster, pdrive->drive_info.log2_bytespalloc);

    clusters_required = M64LOWDW(clusters_requiredddw);

    return pc_exfat_load_fragments_until_by_cluster(pefinode, clusters_required);
}


BOOLEAN pcexfat_bfilio_load_all_fragments(FINODE *pefinode)
{
	return pc_bfilio_load_fragments_until64(pefinode, pefinode->fsizeu.fsize64);
}
