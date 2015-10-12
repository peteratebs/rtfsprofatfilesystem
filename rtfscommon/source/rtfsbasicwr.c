/*
* EBS - RTFS (Real Time File Manager)
*
`* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/

/* RTFSBASICWR.C - Internal source code provided to implement po_xxx() IO routines without utilizing
   extended IO library. These routines are smaller and slightly slower then the emulation provided
   by the extended IO library.

   The routines are compiled when INCLUDE_BASIC_POSIX_EMULATION is enabled.

   They are called by the the posix style IO routines in prbasicemuwr.c

   This is the default and only configuration possible when INCLUDE_RTFS_PROPLUS is disabled.

   INCLUDE_BASIC_POSIX_EMULATION is by default disabled when INCLUDE_RTFS_PROPLUS is enabled, but
   it may enabled.

*/
#include "rtfs.h"


#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */

#if (INCLUDE_BASIC_POSIX_EMULATION)

dword pc_bfilio_alloc_chain(DDRIVE *pdrive, dword alloc_start_hint, dword chain_size, dword *pstart_cluster, int *p_is_error);
static BOOLEAN _pc_bfilio_increase_size(PC_FILE *pefile, dword new_size);
static BOOLEAN _pc_bfilio_write(PC_FILE *pefile, byte *in_buff, dword n_bytes, dword *pnwritten);

/* Imported from rtfsbasicrd.c */
BOOLEAN _pc_bfilio_io(PC_FILE *pefile, byte *pdata, dword n_bytes, BOOLEAN reading, BOOLEAN appending);
BOOLEAN pc_bfilio_load_fragments_until(FINODE *pefinode, dword byte_offset);
dword bfilio_truncate_32_count(dword file_pointer, dword count); /* There is another copy around.. change later */

/* Export to rtfsbasicrd.c */
BOOLEAN _pc_bfilio_reduce_size(PC_FILE *pefile, dword new_size);
BOOLEAN _pc_bfilio_flush(PC_FILE *pefile);



/* Write to a file using the pc_bfilio method, called by po_write() */
int pc_bfilio_write(int fd, byte *in_buff, int count)
{
PC_FILE *pefile;
int ret_val = -1;
    rtfs_clear_errno();

    /* Get the FILE. must be open for write   */
    /* Get the file structure and semaphore lock the drive */
    pefile = pc_fd2file(fd, PO_WRONLY|PO_RDWR);
    if (pefile)
    {
    dword n_bytes, n_written;
        /* Turn count into dword */
        n_bytes = 0; n_bytes += count;
#if (INCLUDE_EXFATORFAT64)
		if (ISEXFATORFAT64(pefile->pobj->pdrive))
		{
			if (pcexfat_bfilio_write(pefile, in_buff, n_bytes, &n_written))
            	ret_val = (int) n_written;
            if (!release_drive_mount_write(pefile->pobj->pdrive->driveno))
            	ret_val = -1;
			return(ret_val);
		}
#endif

        if (_pc_bfilio_write(pefile, in_buff, n_bytes, &n_written))
            ret_val = (int) n_written;
        if (!release_drive_mount_write(pefile->pobj->pdrive->driveno))
            ret_val = -1;
    }
    return(ret_val);
}

/* Flush a file using the pc_bfilio method, called by po_flush() */
BOOLEAN pc_bfilio_flush(int fd)
{
PC_FILE *pefile;
BOOLEAN ret_val = FALSE;

    rtfs_clear_errno();
    pefile = pc_fd2file(fd, 0);
    if (pefile)
    {
        ret_val = _pc_bfilio_flush(pefile);
        if (!release_drive_mount_write(pefile->pobj->pdrive->driveno))
            ret_val = FALSE;
    }
    return(ret_val);
}

/* Change file size using the pc_bfilio method, called by po_chsize() */
BOOLEAN pc_bfilio_chsize(int fd, dword new_size)
{
PC_FILE *pefile;
BOOLEAN ret_val = FALSE;

    rtfs_clear_errno();
    /* Get the FILE. must be open for write   */
    /* Get the file structure and semaphore lock the drive */
    pefile = pc_fd2file(fd, PO_WRONLY|PO_RDWR);
    if (pefile)
    {
        dword old_size;
#if (INCLUDE_EXFATORFAT64)
		if (ISEXFATORFAT64(pefile->pobj->pdrive))
		{
		ddword dwnew_size;
			dwnew_size = M64SET32(0,new_size);
			ret_val = pcexfat_bfilio_chsize(pefile, dwnew_size);
			if (!release_drive_mount_write(pefile->pobj->pdrive->driveno))
				ret_val = FALSE;
			return (ret_val);
		}
#endif
        old_size = pefile->pobj->finode->fsizeu.fsize;
        /* Flush the file first */
        if (_pc_bfilio_flush(pefile))
        {
            if (old_size > new_size)
            {   /* Truncate: Can only truncate a file that you hold exclusively   */
                if (pefile->pobj->finode->opencount > 1)
                {
                    rtfs_set_errno(PEACCES, __FILE__, __LINE__);
                    ret_val = FALSE;
                }
                else
				{
                   	ret_val = _pc_bfilio_reduce_size(pefile, new_size);
				}
            }
            else if (old_size < new_size)
            {   /* Expand: _pc_bfilio_increase_size uses _pc_bfilio_write() to extend the file */
                ret_val = _pc_bfilio_increase_size(pefile, new_size);
                if (!ret_val)
                { /* revert to original size if we ran out of space */
                    if (get_errno() == PENOSPC || get_errno() == PENOEMPTYERASEBLOCKS)
                    {
                     	/* Added november 2009. return FALSE and do not clear errno */
                        ret_val = FALSE;
                        _pc_bfilio_reduce_size(pefile, old_size);
                    }
                }
            }
            else
                ret_val = TRUE; /* Unchanged */
        }
        /* Flush any changes */
        if (!_pc_bfilio_flush(pefile))
            ret_val = FALSE;
        if (!release_drive_mount_write(pefile->pobj->pdrive->driveno))
            ret_val = FALSE;
    }
    return(ret_val);
}

/* Flush a file using the pc_bfilio method, implementation called by pc_bfilio_flush and pc_bfilio_close */
BOOLEAN _pc_bfilio_flush(PC_FILE *pefile)
{
#if (INCLUDE_EXFATORFAT64)	 /* Need callback to _pcexfat_bfilio_flush */
	if (ISEXFATORFAT64(pefile->pobj->pdrive))
		return(_pcexfat_bfilio_flush(pefile));
#endif
    if (!pc_flush_file_buffer(pefile->pobj->finode))
        return(FALSE);
    if (pc_check_file_dirty(pefile))
    {
        /* Overwrite the finode, don't force date field updates, they were done when dirty was set */
  		if (pc_update_inode(pefile->pobj, FALSE, 0))
        {
           if (fatop_flushfat(pefile->pobj->pdrive))
           {
                pc_set_file_dirty(pefile, FALSE);
                return(TRUE);
           }
        }
        return(FALSE);
    }
    return(TRUE);
}

/* Truncate a file using the pc_bfilio method, implementation called by pc_bfilio_open and pc_bfilio_chsize */
BOOLEAN _pc_bfilio_reduce_size(PC_FILE *pefile, dword new_size)
{
FINODE *pefinode;
dword first_cluster_to_delete,new_end_cluster,start_size;

    pefinode = pefile->pobj->finode;
    new_end_cluster = first_cluster_to_delete = 0;

    start_size = pefinode->fsizeu.fsize;

    if (new_size == 0)
    {
        /* new_end_cluster is zero, first_cluster_to_delete is the first cluster in the file */
        first_cluster_to_delete = pc_finode_cluster(pefile->pobj->pdrive, pefinode);
        pc_pfinode_cluster(pefile->pobj->pdrive, pefinode, 0);
    }
    else
    {
        REGION_FRAGMENT *file_pcurrent_fragment;
        dword new_size_clusters;
        dword chain_size_clusters;

        /* Load cluster chain up to at least new_size bytes */
        if (!pc_bfilio_load_fragments_until(pefinode, new_size-1))
            return(FALSE);
        /* Find the the new end cluster and the first cluster in the rest of the chain to delete */
        file_pcurrent_fragment = pefinode->pbasic_fragment;
        chain_size_clusters = 0;
        new_size_clusters = pc_byte2clmod(pefile->pobj->pdrive, new_size);
        while (file_pcurrent_fragment)
        {
            dword new_chain_size_clusters;
            new_chain_size_clusters = chain_size_clusters + PC_FRAGMENT_SIZE_CLUSTERS(file_pcurrent_fragment);
            if (new_chain_size_clusters > new_size_clusters)
            { /* delete all or part of the current fragment */
                first_cluster_to_delete = file_pcurrent_fragment->start_location +
                                            (new_size_clusters - chain_size_clusters);
                /* Remember the new end of file if it is inside this fragment
                   We saved the end of the previous fragment in case we are deleting this
                   whole fragment */
                if (first_cluster_to_delete != file_pcurrent_fragment->start_location)
                    new_end_cluster = first_cluster_to_delete - 1;
                break;
            }
            chain_size_clusters = new_chain_size_clusters;
            /* Save the end location of the current fragment. It will be the new end of file if
               chain_size_clusters == new_size_clusters. (first_cluster_to_delete will be the
               start of the next fragment) */
            new_end_cluster = file_pcurrent_fragment->end_location;
            file_pcurrent_fragment = file_pcurrent_fragment->pnext;
        }
    }
    pc_set_file_dirty(pefile, TRUE);
    /* if the fp is out of bound put it at the end of file */
    if (pefile->fc.basic.file_pointer > new_size)
        pefile->fc.basic.file_pointer = new_size;
    /* release the fragment list to force a reload */
    if (pefinode->pbasic_fragment)
        pc_fraglist_free_list(pefinode->pbasic_fragment);
    pefinode->pbasic_fragment = 0;
    /* Update the file size */
    pefinode->fsizeu.fsize = new_size;

    if (first_cluster_to_delete)
    {  /* Free the chain.. guard against endless loops by limiting the number of clusters to free to the original size */
        if (!fatop_freechain(pefile->pobj->pdrive, first_cluster_to_delete, pc_byte2clmod(pefile->pobj->pdrive, start_size)))
            return(FALSE);
        /* Terminate the chain if we have to */
        if (new_end_cluster)
        {
            if (!fatop_pfaxxterm(pefile->pobj->pdrive, new_end_cluster))
                return(FALSE);
        }
    }
    return(TRUE);
}

/* Expand a file using the pc_bfilio method, implementation called by pc_bfilio_chsize */
static BOOLEAN _pc_bfilio_increase_size(PC_FILE *pefile, dword new_size)
{
FINODE *pefinode;
BOOLEAN ret_val;
dword n_left,n_written, saved_file_pointer;

    pefinode = pefile->pobj->finode;
    saved_file_pointer = pefile->fc.basic.file_pointer;
    pefile->fc.basic.file_pointer = pefinode->fsizeu.fsize;

    n_left = new_size - pefinode->fsizeu.fsize;
    ret_val = TRUE;
    if (n_left)
    {
        if (!_pc_bfilio_write(pefile, 0, n_left, &n_written))
        {
            ret_val = FALSE;
        }
        if (n_left != n_written)
        {
            ret_val = FALSE;
        }
    }
    pefile->fc.basic.file_pointer = saved_file_pointer;
    return(ret_val);
}

/* Internal seek routine (see rtfsbasicrd) required for append mode */
BOOLEAN pc_bpefile_ulseek(PC_FILE *pefile, dword offset, dword *pnew_offset, int origin);

/* Write to a file using the pc_bfilio method, implementation */
static BOOLEAN _pc_bfilio_write(PC_FILE *pefile, byte *in_buff, dword n_bytes, dword *pnwritten)
{
BOOLEAN ret_val = FALSE;
BOOLEAN appending;
dword n_left, required_size_bytes, file_chain_length, file_chain_length_bytes;
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
        dword newoffset;
        if (!pc_bpefile_ulseek(pefile, 0, &newoffset, PSEEK_END))
            return(FALSE);
    }
    n_left = bfilio_truncate_32_count(pefile->fc.basic.file_pointer,n_bytes);
    if (!n_left) /* Not an error just return 0 */
        return(TRUE);
    /* make sure the cluster chain is loaded and test if we are appending to the file */
    appending = FALSE;
    if (pefinode->fsizeu.fsize)
    {
    dword new_size,last_byte_offset;
        new_size = truncate_32_sum(pefile->fc.basic.file_pointer,n_left);
        if (new_size > pefinode->fsizeu.fsize)
        {
            appending = TRUE;
            last_byte_offset = pefinode->fsizeu.fsize-1;
        }
        else
            last_byte_offset = new_size-1;
        if (!pc_bfilio_load_fragments_until(pefinode, last_byte_offset))
            return(FALSE);
    }
    required_size_bytes =  truncate_32_sum(pefile->fc.basic.file_pointer,n_left);
    file_chain_length = pc_byte2clmod(pdrive, pefinode->fsizeu.fsize);
    file_chain_length_bytes = pc_alloced_bytes_from_clusters(pdrive, file_chain_length);
    if (required_size_bytes > file_chain_length_bytes)
    { /* Append clusters to the file */
    dword alloc_hint, new_clusters_required, n_alloced, start_cluster;
    int is_error;

        new_clusters_required = pc_byte2clmod(pdrive, required_size_bytes - file_chain_length_bytes);
        if (file_chain_length)
            alloc_hint = pc_end_fragment_chain(pefinode->pbasic_fragment)->end_location;
        else
            alloc_hint = 0;
        is_error = 0;
        n_alloced = pc_bfilio_alloc_chain(pdrive, alloc_hint, new_clusters_required , &start_cluster, &is_error);
        if (is_error)
            return(FALSE);
        else
        {
            if (n_alloced)
            {
                if (!file_chain_length) /* Set the start cluster in the entry */
                    pc_pfinode_cluster(pdrive, pefinode, start_cluster);
                file_chain_length += n_alloced;
                file_chain_length_bytes = pc_alloced_bytes_from_clusters(pdrive, file_chain_length);
            }
            /* See if we got some clusters but not all .. (short write) */
            if (file_chain_length_bytes < required_size_bytes)
                n_left -= (required_size_bytes - file_chain_length_bytes);
        }
    }

    if (n_left)
    {
		/* pc_bfilio_io writes the data and updates the file pointer */
        if (_pc_bfilio_io(pefile, in_buff, n_left, FALSE, appending))
        {
            *pnwritten = n_left;
            if (pefile->fc.basic.file_pointer > pefinode->fsizeu.fsize)
            {
                pefinode->fsizeu.fsize = pefile->fc.basic.file_pointer;
                pc_set_file_dirty(pefile, TRUE);
            }

            ret_val = TRUE;
        }
        else
		{
            ret_val = FALSE;
		}
    }
    else
        ret_val = TRUE; /* No space left, wrote zero byes, but not an error */
    return(ret_val);
}
#endif

#if (INCLUDE_BASIC_POSIX_EMULATION||INCLUDE_EXFATORFAT64)
/* Cluster allocator function called by_pc_bfilio_write() */
dword pc_bfilio_alloc_chain(DDRIVE *pdrive, dword alloc_start_hint, dword chain_size, dword *pstart_cluster, int *p_is_error)
{
    dword clusters_alocated, hint_cluster,chain_size_left;

    *pstart_cluster = 0;
    *p_is_error = 0;
    clusters_alocated = 0;
    hint_cluster = alloc_start_hint;
    chain_size_left = chain_size;

    while (chain_size_left)
    {
    dword first_new_cluster, n_contig;
        n_contig = fatop_alloc_chain(pdrive, TRUE, hint_cluster, hint_cluster, &first_new_cluster, chain_size_left, 1);
#if (INCLUDE_FAILSAFE_RUNTIME)
    	if (n_contig == 0 && prtfs_cfg->pfailsafe && get_errno() == PENOSPC)
    	{
    		/* If we ran out of space, call Failsafe and tell it to shrink the journal file
    			by at least one cluster so we can try again.. If Failsafe is not running or
    			if the journal file size, can't be reduced no clustser will be released
    			and the our retry attempt will fail. */
    		rtfs_clear_errno(); /* clear error status */
    		first_new_cluster = 0;
    		if (prtfs_cfg->pfailsafe->fs_recover_free_clusters(pdrive, 1))
        		n_contig = fatop_alloc_chain(pdrive, TRUE, hint_cluster, hint_cluster, &first_new_cluster, chain_size_left, 1);
        }
#endif

        if (n_contig == 0)
        {
			if (get_errno() != PENOSPC && get_errno() != PENOEMPTYERASEBLOCKS)
                *p_is_error = 1;
            break;
        }
        else
        {
            if (!*pstart_cluster)
                *pstart_cluster = first_new_cluster;
            clusters_alocated += n_contig;
            chain_size_left -= n_contig;
            hint_cluster = first_new_cluster + (n_contig-1);
        }
    }
    return(clusters_alocated);
}

#endif
#endif /* Exclude from build if read only */
