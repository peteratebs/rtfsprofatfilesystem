/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* RTFSBASICRD.C - Internal source code provided to implement po_xxx() IO routines without utilizing
   extended IO library. These routines are smaller and slightly slower then the emulation provided
   by the extended IO library.

   The routines are compiled when INCLUDE_BASIC_POSIX_EMULATION is enabled.

   They are called by the the posix style IO routines in prbasicemurd.c


   This is the default and only configuration possible when INCLUDE_RTFS_PROPLUS is not-enabled.

   INCLUDE_BASIC_POSIX_EMULATION is by default disabled when INCLUDE_RTFS_PROPLUS is enabled, but
   it may enabled.

   These routines implement file IO with a small footprint but reasonable performance. They are the
   standard, and only file IO routines provided with the Rtfs Basic and Rtfs Pro packages, which do
   not supply extended file IO. They are included with RtfsProPlus and they may be used with RtfsProPlus
   by enabling INCLUDE_BASIC_POSIX_EMULATION, which is, by default, disabled in ProPlus.


   Like the extended IO library we manage a fragment list of file extents when the file is opened.

   Un-like the extended IO library we do not maintain current fragment and current block information. We
   hold on;y a 32 bit file pointer. Every time read, write or seek is performed the cluster and
   block locations of the file pointer are calculated from the fragment chain. This is a minor performance
   penalty that reduces the complexity significantly compared to the extended IO library.

   Un-like the extended IO library we do not provide cluster allocation strategy options. All clusters
   are allocated by the routine named pc_bfilio_alloc_chain, which utilizes fatop_alloc_chain.

*/

#include "rtfs.h"
#if (INCLUDE_BASIC_POSIX_EMULATION||INCLUDE_EXFATORFAT64)
dword bfilio_truncate_32_count(dword file_pointer, dword count); /* There is another copy around.. change later */
static BOOLEAN pc_bfilio_load_fragments_until_by_cluster(FINODE *pefinode, dword clusters_required);
#endif

#if (INCLUDE_BASIC_POSIX_EMULATION)


/* Imported from rtfsbasicwr.c */
#if (!RTFS_CFG_READONLY)
BOOLEAN _pc_bfilio_reduce_size(PC_FILE *pefile, dword new_size);
BOOLEAN _pc_bfilio_flush(PC_FILE *pefile);
#endif

/* Export to  rtfsbasicwr.c */
dword bfilio_truncate_32_count(dword file_pointer, dword count); /* There is another copy around.. change later */
BOOLEAN _pc_bfilio_io(PC_FILE *pefile, byte *pdata, dword n_bytes, BOOLEAN reading, BOOLEAN appending);
BOOLEAN pc_bfilio_load_fragments_until(FINODE *pefinode, dword clusters_required);


static BOOLEAN _pc_efiliocom_move_fp(dword data_start, dword file_size, dword file_pointer, dword offset, int origin, dword *pnewoffset);

/* Open a file using the pc_bfilio method, called by po_open() */
int pc_bfilio_open_cs(byte *name, word flag, word mode, int use_charset)
{
int fd, driveno;
BOOLEAN created;

    rtfs_clear_errno();
     /* Get the drive and make sure it is mounted   */
    driveno = check_drive_name_mount(name, use_charset);
    if (driveno < 0)   /*  errno was set by check_drive */
        return(-1);
    fd =  _pc_file_open(pc_drno2dr(driveno), name,flag, mode, 0, &created, use_charset);
    if (fd >= 0)
    {
    PC_FILE *pefile;
      	pefile = prtfs_cfg->mem_file_pool+fd;
      	pefile->fc.basic.file_pointer = 0;
#if (INCLUDE_EXFATORFAT64)
   		pefile->fc.basic.file_pointer64 = M64SET32(0, 0);
#endif

#if (!RTFS_CFG_READONLY)    /* Read only file system, don't truncate files */
      if (flag & PO_TRUNC)
      {
#if (INCLUDE_EXFATORFAT64)	 /* Checking is 64 bit file */
      	if (ISEXFATORFAT64(pefile->pobj->pdrive))
		{
			ddword newsize;
			newsize = M64SET32(0, 0);
			if (!pcexfat_bfilio_chsize(pefile, newsize))
			{
            	pc_freefile(pefile);
            	fd = -1;
			}
		}
		else
#endif
		{
        	if (!_pc_bfilio_reduce_size(pefile, 0))
        	{
            	pc_freefile(pefile);
            	fd = -1;
        	}
		}
      }
#endif
    }
#if (!RTFS_CFG_READONLY)    /* Read only file system, don't truncate files */
      if (!release_drive_mount_write(driveno))/* Release lock, unmount if aborted */
        fd = -1;
#else
      release_drive_mount(driveno);
#endif
    return(fd);
}

/* read from a file using the pc_bfilio method, called by po_read() */
int pc_bfilio_read(int fd, byte *in_buff, int count)
{
PC_FILE *pefile;
int ret_val = -1;

    rtfs_clear_errno();
    pefile = pc_fd2file(fd, 0);
    if (pefile)
    {
    dword n_left,n_left_file, n_bytes;

     ret_val = 0;
#if (INCLUDE_EXFAT)
	if (ISEXFAT(pefile->pobj->pdrive))
	{
		ret_val = pcexfat_bfilio_read(pefile, in_buff, count);
		release_drive_mount(pefile->pobj->pdrive->driveno);
		return(ret_val);
	}
#endif
        /* Turn count into dword */
        n_bytes = 0; n_bytes += count;
        n_left = bfilio_truncate_32_count(pefile->fc.basic.file_pointer,n_bytes);
        if (pefile->fc.basic.file_pointer < pefile->pobj->finode->fsizeu.fsize)
            n_left_file = pefile->pobj->finode->fsizeu.fsize - pefile->fc.basic.file_pointer;
        else
            n_left_file = 0;
        if (n_left > n_left_file)
            n_left = n_left_file;
        if (n_left)
        {   /* pc_bfilio_io reads the data and updates the file pointer */
            if (_pc_bfilio_io(pefile, in_buff, n_left, TRUE, FALSE))
			{
				pc_update_finode_datestamp(pefile->pobj->finode, FALSE, DATESETACCESS);
                ret_val = (int) n_left;
			}
            else
                ret_val = -1;
			 /* Update the accessed date stamp. Note: The date change will only be
			    flushed to disk if a write operation occurs */
        }
        release_drive_mount(pefile->pobj->pdrive->driveno);
    }
    return(ret_val);
}

/* close a file using the pc_bfilio method, called by po_close() */
int pc_bfilio_close(int fd)
{
PC_FILE *pefile;
int ret_val = -1;

    rtfs_clear_errno();
    if ( (pefile = pc_fd2file(fd, 0)) == 0)
    {
        if (get_errno() == PECLOSED)
        {
            pefile = prtfs_cfg->mem_file_pool+fd;
            /* fd2file failed because it was closed by pc_dskfree. */
            /* mark the file free here and set is_aborted status */
            OS_CLAIM_FSCRITICAL()
            pefile = prtfs_cfg->mem_file_pool+fd;
            pefile->is_free = TRUE;
            OS_RELEASE_FSCRITICAL()
            rtfs_clear_errno();    /* clear errno */
            ret_val = 0;
        }
    }
    else
    {
    int driveno;
        ret_val = 0;
        driveno = pefile->pobj->pdrive->driveno;
#if (!RTFS_CFG_READONLY)    /* Read only file system, don't flush files */
        if (!_pc_bfilio_flush(pefile))
            ret_val = -1;
#endif
        pc_freefile(pefile);
#if (RTFS_CFG_READONLY)    /* Read only file system, don't flush files */
        release_drive_mount(driveno);
#else
        if (!release_drive_mount_write(driveno))
            ret_val = -1;
#endif
    }
    return(ret_val);
}


/* seek in a file using the pc_bfilio method, called by po_ulseek() */
/* Internal version, called by write and seek */
BOOLEAN pc_bpefile_ulseek(PC_FILE *pefile, dword offset, dword *pnew_offset, int origin)
{
BOOLEAN ret_val = FALSE;
dword newoffset;
    *pnew_offset = 0;

    /* Move and query the file pointer */
    ret_val = _pc_efiliocom_move_fp(0, pefile->pobj->finode->fsizeu.fsize, pefile->fc.basic.file_pointer, offset, origin, &newoffset);
    if (ret_val)
    {
        if (newoffset)  /* Now check the chain */
        {
            if (newoffset == pefile->pobj->finode->fsizeu.fsize)
                ret_val = pc_bfilio_load_fragments_until(pefile->pobj->finode, newoffset-1);
            else
                ret_val = pc_bfilio_load_fragments_until(pefile->pobj->finode, newoffset);
        }
        if (ret_val)
        {
            *pnew_offset = newoffset;
            pefile->fc.basic.file_pointer = newoffset;
        }
    }
    return(ret_val);
}

#if (INCLUDE_MATH64)	 /* Checking is 64 bit file */
ddword pc_bfilio_lseek64(int fd, ddword offset, int origin)
{
PC_FILE *pefile;
ddword  new_offset;
    rtfs_clear_errno();
	new_offset = M64SET32(0,0);
    pefile = pc_fd2file(fd, 0);
    if (pefile)
    {
#if (INCLUDE_EXFATORFAT64)
		if (ISEXFATORFAT64(pefile->pobj->pdrive))
		{
			if (!pcexfat_bpefile_ulseek(pefile, offset, &new_offset, origin))
				new_offset = M64SET32(0xffffffff,0xffffffff);
		}
		else
#endif
		{
		dword dwnewoffset,targetoffset;
			targetoffset = M64LOWDW(offset);
       		if (!pc_bpefile_ulseek(pefile, targetoffset, &dwnewoffset, origin))
				new_offset = M64SET32(0xffffffff,0xffffffff);
			else
				new_offset = M64SET32(0,dwnewoffset);

		}
        release_drive_mount(pefile->pobj->pdrive->driveno);
    }
    return(new_offset);
}

#endif

/* external version, called by called by po_ulseek() */
BOOLEAN pc_bfilio_ulseek(int fd, dword offset, dword *pnew_offset, int origin)
{
PC_FILE *pefile;
BOOLEAN ret_val = FALSE;
    rtfs_clear_errno();
    *pnew_offset = 0;
    pefile = pc_fd2file(fd, 0);
    if (pefile)
    {
#if (INCLUDE_EXFATORFAT64)	 /* Checking is 64 bit file */
		if (ISEXFATORFAT64(pefile->pobj->pdrive))
		{
			ddword dwoffset, dwnewoffset;

			dwoffset = M64SET32(0,offset);
			dwnewoffset = M64SET32(0,0);

			ret_val = pcexfat_bpefile_ulseek(pefile, dwoffset, &dwnewoffset, origin);
			*pnew_offset = M64LOWDW(dwnewoffset);
		}
		else
#endif
        {
        	ret_val = pc_bpefile_ulseek(pefile, offset, pnew_offset, origin);
		}
        release_drive_mount(pefile->pobj->pdrive->driveno);
    }
    return(ret_val);
}


/* fstat in a file using the pc_bfilio method, called by pc_fstat() */
int pc_bfilio_fstat(int fd, ERTFS_STAT *pstat)
{
PC_FILE *pefile;
int ret_val = -1;

    rtfs_clear_errno();
    pefile = pc_fd2file(fd, 0);
    if (pefile)
    {
        pc_finode_stat(pefile->pobj->finode, pstat);
        release_drive_mount(pefile->pobj->pdrive->driveno);
        ret_val = 0;
    }
    return(ret_val);
}


/* Move the file pointer using seek rules between data_start (0) and the file size
   does not worry about cluster chains, that is done at a higher level */
static BOOLEAN _pc_efiliocom_move_fp(dword data_start, dword file_size, dword file_pointer, dword offset, int origin, dword *pnewoffset)
{
dword start_pointer, new_file_pointer;

    *pnewoffset = file_pointer;
    if (origin == PSEEK_SET)        /*  offset from begining of data */
        start_pointer = data_start;
    else if (origin == PSEEK_CUR)   /* offset from current file pointer */
        start_pointer = file_pointer;
    else if (origin == PSEEK_CUR_NEG)  /* negative offset from current file pointer */
        start_pointer = file_pointer;
    else if (origin == PSEEK_END)   /*  offset from end of file */
    {
        if (file_size)
            start_pointer = file_size;
        else
            start_pointer = 0;
    }
    else
    {
        rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
        return(FALSE);
    }

    if (origin == PSEEK_CUR_NEG || origin == PSEEK_END)
    {
        new_file_pointer = start_pointer-offset;
        if (new_file_pointer > start_pointer || /* wrapped around */
            new_file_pointer < data_start)
            new_file_pointer = data_start; /* truncate to start */
    }
    else
    {
        new_file_pointer = start_pointer+offset;
        if (new_file_pointer < start_pointer || /* wrapped around */
            new_file_pointer > file_size)
            new_file_pointer = file_size; /* truncate to end */
    }
    *pnewoffset = new_file_pointer;
    return(TRUE);
}
#endif


#if (INCLUDE_BASIC_POSIX_EMULATION)

/* Make sure the bfile's fragment list is loaded to include at least byte_offset.
   If it does not it is an error caused by invalid cluster chains */
BOOLEAN pc_bfilio_load_fragments_until(FINODE *pefinode, dword byte_offset)
{
dword clusters_required;
DDRIVE *pdrive;

    pdrive = pefinode->my_drive;
    clusters_required = pc_byte2clmod(pdrive, byte_offset+1);
    return pc_bfilio_load_fragments_until_by_cluster(pefinode, clusters_required);
}



static BOOLEAN pc_bfilio_load_fragments_until_by_cluster(FINODE *pefinode, dword clusters_required)
{
dword loaded_chain_length, cluster_number, next_cluster;
REGION_FRAGMENT *pend_fragment;
DDRIVE *pdrive;
dword max_cluster_per_fragment;
	rtfs_clear_errno();
    pdrive = pefinode->my_drive;

    ERTFS_ASSERT(!ISEXFATORFAT64(pdrive))

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
		{ /* Fat version */
      	cluster_number = fatop_next_cluster(pdrive, pend_fragment->end_location);
       	if (cluster_number == FAT_EOF_RVAL)
       		cluster_number = 0;
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
       	n_clusters = fatop_get_frag(pdrive, 0, 0, cluster_number, &next_cluster, pdrive->drive_info.maxfindex, &end_of_chain);
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

/*
   Read or write n_bytes at the current file pointer
   The the size has already been verified and the cluster
   chain should contain at least n_bytes at the file pointer.
   If not it is an error
*/
BOOLEAN _pc_bfilio_io(PC_FILE *pefile, byte *pdata, dword n_bytes, BOOLEAN reading, BOOLEAN appending)
{
dword n_left, n_todo, file_pointer,region_byte_next,file_region_byte_base, file_region_block_base;
int at_eor;
REGION_FRAGMENT *file_pcurrent_fragment;
DDRIVE *pdrive;
FINODE *pefinode;


    if (!n_bytes)
        return(TRUE);
    /* use local copies of file info so if we fail the pointers will not advance */
    file_pointer  = pefile->fc.basic.file_pointer;
    pefinode = pefile->pobj->finode;
    pdrive = pefinode->my_drive;

    n_left = bfilio_truncate_32_count(file_pointer,n_bytes);
    if (!n_left)
        return(TRUE);
    /* load cluster chain to include all bytes we will read or write, they should all be there */
    {
        dword new_offset;
        new_offset = file_pointer + n_left -1;
        if (!pc_bfilio_load_fragments_until(pefinode, new_offset))
        {
            return(FALSE); /* load_efinode_fragments_until has set errno */
        }
    }
    file_pcurrent_fragment      = pefinode->pbasic_fragment;
    file_region_byte_base       = 0;
    /* Get the block base */
    while (file_pcurrent_fragment && file_region_byte_base < file_pointer)
    {
        region_byte_next = truncate_32_sum(file_region_byte_base, pc_fragment_size_32(pdrive, file_pcurrent_fragment));
        if (region_byte_next > file_pointer)
            break;
        file_region_byte_base  = region_byte_next;
        file_pcurrent_fragment = file_pcurrent_fragment->pnext;
    }
    if (!file_pcurrent_fragment)
    {  /* File pointer not in the chain */
        rtfs_set_errno(PEINVALIDCLUSTER, __FILE__, __LINE__);
        return(FALSE);
    }
    file_region_block_base  = pc_cl2sector(pdrive, file_pcurrent_fragment->start_location);
    region_byte_next        = truncate_32_sum(file_region_byte_base, pc_fragment_size_32(pdrive, file_pcurrent_fragment));
    while (n_left)
    {
        at_eor = 0;
        if (file_pointer & pdrive->drive_info.bytemasksec)
        {   /* fp not on a sector boundary */
            n_todo = pdrive->drive_info.bytespsector - (file_pointer & pdrive->drive_info.bytemasksec);
            if (n_todo > n_left)
                n_todo = n_left;
        }
        else
        {
            n_todo = n_left & ~pdrive->drive_info.bytemasksec; /* mask off end if not on a sector boundary */
            if (!n_todo)
                n_todo = n_left;
        }
        {
        dword new_file_pointer;
            new_file_pointer = file_pointer + n_todo;
            if (new_file_pointer >= region_byte_next)
            {
                new_file_pointer = region_byte_next;
                n_todo = new_file_pointer - file_pointer;
                if (region_byte_next != LARGEST_DWORD)
                    at_eor = 1;
            }
        }
        if (pdata)
        {
            /* call pc_buffered_fileio() which buffers the data if necessary  */
            dword start_byte_offset, start_sector;
            /* Calculate the starting sector and byte offset into that sector of the file pointer  */
            start_sector = file_region_block_base + ((file_pointer - file_region_byte_base)>>pdrive->drive_info.log2_bytespsec);
            start_byte_offset = (file_pointer & pdrive->drive_info.bytemasksec);

            if (!pc_buffered_fileio(pefinode, start_sector, start_byte_offset, n_todo, pdata,  reading, appending))
                return(FALSE);
            pdata += n_todo;
        }
        file_pointer += n_todo;
        n_left -= n_todo;
        if (n_left)
        {
            if (at_eor)
            { /* Should clusters in more fragments otherwise we are just advancing to a block boundary */
                if (file_pcurrent_fragment->pnext)
                {
                    file_pcurrent_fragment      = file_pcurrent_fragment->pnext;
                    file_region_byte_base       = region_byte_next;
                    region_byte_next            =  truncate_32_sum(file_region_byte_base, pc_fragment_size_32(pdrive, file_pcurrent_fragment));
                    file_region_block_base      = pc_cl2sector(pdrive, file_pcurrent_fragment->start_location);
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
    pefile->fc.basic.file_pointer      =    file_pointer;
    return(TRUE);
}

/* Truncate 32 bit file write counts to fit inside 4 Gig size limit */
dword bfilio_truncate_32_count(dword file_pointer, dword count)
{
dword max_count;

    max_count = LARGEST_DWORD - file_pointer;
    if (count > max_count)
        count = max_count;
    return(count);
}




#endif
