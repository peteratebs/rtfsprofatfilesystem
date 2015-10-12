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


int _pc_efilio32_open(DDRIVE *pdr,byte *name, word flag, word mode, EFILEOPTIONS *poptions, int use_charset)  /*__apifn__*/
{
int fd;
PC_FILE *pefile;
FINODE *pefinode;
dword extended_options,allocation_policy;

    extended_options = PCE_EXTENDED_FILE;
    if (poptions)
        allocation_policy = poptions->allocation_policy;
    else
        allocation_policy = 0;

    if (allocation_policy & PCE_TRANSACTION_FILE)
        extended_options |= PCE_TRANSACTION_FILE;


    /* Check and correct incompatible options */
    if (allocation_policy & PCE_LOAD_AS_NEEDED)
    {
        if ((flag & PO_TRUNC) || (allocation_policy & (PCE_TRANSACTION_FILE|PCE_CIRCULAR_FILE|PCE_CIRCULAR_BUFFER|PCE_ASYNC_OPEN)))
            allocation_policy &= ~PCE_LOAD_AS_NEEDED;
    }

    fd = _pc_file_open(pdr, name, flag,  mode, extended_options, 0, use_charset);
    if (fd < 0)
        return(-1);
    /* Now the driver is locked */
    pefile = prtfs_cfg->mem_file_pool+fd;
    pdr = pefile->pobj->pdrive;

    /* Default to minimum allocation of one cluster */
    pefile->fc.plus.min_alloc_bytes = (dword) pdr->drive_info.bytespcluster;
    pefile->fc.plus.allocation_policy = allocation_policy;

    if (poptions)
    {

        /* Remember if the user suggested a place to allocate from */
        pefile->fc.plus.allocation_hint = poptions->allocation_hint;
        /* create a mask for rounding up to the minimum allocation size */
        if (poptions->min_clusters_per_allocation)
        {
            pefile->fc.plus.min_alloc_bytes =
                poptions->min_clusters_per_allocation * pdr->drive_info.bytespcluster;
        }
    }

    pefinode =  pefile->pobj->finode;
    pefile->fc.plus.ffinode =  pefile->pobj->finode;
    if (pefinode->opencount != 1)
    {
        if (pefinode->operating_flags & FIOP_ASYNC_ALL_OPS)
        { /* Already opened but in an async operation */
            rtfs_set_errno(PEEINPROGRESS, __FILE__, __LINE__);
            goto errex;
        }
    }
    else
    {
       /* If it is a temp file it must be zero sized (can't have an existing
          disk image */
		if (allocation_policy&PCE_TEMP_FILE && !FINODESIZEISZERO(pefinode))
        {
            rtfs_set_errno(PEEXIST, __FILE__, __LINE__);
            goto errex;
        }
        /* Initialize extended io portion of the finode alloc zeroes the structure */
        pefinode->e.x = pc_memory_finode_ex(0);
        if (!pefinode->e.x)                       /* Errno is set */
            goto errex;

#if (INCLUDE_ASYNCRONOUS_API)
        if (allocation_policy & PCE_ASYNC_OPEN)
        { /* Queue for asynchronous completion */
            _set_asy_operating_flags(pefile,
                        pefile->pobj->finode->operating_flags|FIOP_ASYNC_OPEN,1);

            return(fd);
        }
#endif
        {
        BOOLEAN load_result;
        /* Read the first fragment chain and call pc_efilio32_open_complete to set
               file pointers, need a new allocation policy for incremental open
               and for partial open of the finode
               */
           if (allocation_policy & PCE_LOAD_AS_NEEDED)
           {
                /* Initialize as needed loading and read the first fragment chain, pc_efilio32_open_complete will set filepointer */
				llword zero;
				zero.val32=0;
#if (INCLUDE_EXFATORFAT64)
				zero.val64=0;
#endif
                pefile->pobj->finode->operating_flags |= FIOP_LOAD_AS_NEEDED;
                load_result = load_efinode_fragments_until(pefinode, zero);
           }
           else
                load_result = load_efinode_fragment_list(pefinode);

            if (load_result)
            {
                if (!pc_efilio32_open_complete(pefile))
                {
                    pc_efilio32_open_error(pefile);
                    return(-1);
                }
            }
            else
            {
                pc_efilio32_open_error(pefile);
                return(-1);
            }
        }
    }
    /* pc_file_open does not truncate extended files because we can do it better -
       do it now if we need to */
#if (!RTFS_CFG_READONLY)   /* Do not truncate extended files if read only file system */
    if (flag & PO_TRUNC) /* Truncate loaded fragments now */
    {
        if (!(_pc_efinode_truncate_finode(pefile->pobj->finode) &&
             pc_update_inode(pefile->pobj, TRUE, DATESETUPDATE)) )
        {
            pc_efilio32_open_error(pefile);
            return(-1); /* open complete freed the file */
        }
        /* Resync after truncate */
        if (!pc_efilio32_open_complete(pefile))
        {
            pc_efilio32_open_error(pefile);
            return(-1); /* open complete freed the file */
        }
    }
#endif
    return(fd);
errex:
   pc_freefile(pefile);
   return(-1);
}

void pc_efilio32_open_error(PC_FILE *pefile)
{
FINODE *pefinode;
    pefinode = pefile->pobj->finode;
    if (pefinode->e.x->pfirst_fragment)
        pc_fraglist_free_list(pefinode->e.x->pfirst_fragment);
	pefinode->e.x->alloced_size_bytes.val32=0;
#if (INCLUDE_EXFATORFAT64)
	pefinode->e.x->alloced_size_bytes.val64=0;
#endif
    pefinode->e.x->alloced_size_clusters = 0;
    pefinode->e.x->plast_fragment        = 0;
    pefinode->e.x->pfirst_fragment       = 0;
    pc_freefile(pefile);
}

#if (INCLUDE_EXTENDED_ATTRIBUTES)

/* Checks the first cluster in a file for an extended attribute record value */
dword pc_efilio32_get_dstart(PC_FILE *pefile)
{
FINODE *pefinode;
dword retval=0;
    pefinode = pefile->pobj->finode;

    if (!FINODESIZEISZERO(pefinode))
	{
	dword first_cluster,sector;

		first_cluster=pc_finode_cluster(pefile->pobj->pdrive, pefinode);
		if (first_cluster)
		{
		BLKBUFF *pblk;
		byte *b;

  			sector=pc_cl2sector(pefile->pobj->pdrive,first_cluster);
			pblk=pc_read_blk(pefile->pobj->pdrive, sector);
			if (!pblk)
				return 0;
			if (rtfs_bytecomp((byte *)pblk->data, (byte *) RTFS_EXTATTRIBUTE_SIGNATURE, RTFS_EXTATTRIBUTE_SIGNATURE_SIZE))
			{
				b=pblk->data;
				b+=RTFS_EXTATTRIBUTE_SIGNATURE_SIZE;
				retval=to_DWORD(b);
			}
			pc_discard_buf(pblk);
		}
	}
	return retval;
}

/* Checks the first cluster in a file for an extended attribute record value */
BOOLEAN pc_efilio32_set_dstart(PC_FILE *pefile, dword dstart)
{
FINODE *pefinode;
BOOLEAN retval=FALSE;
    pefinode = pefile->pobj->finode;


	if (!FINODESIZEISZERO(pefinode))
	{
	dword first_cluster,sector;

		first_cluster=pc_finode_cluster(pefile->pobj->pdrive, pefinode);
		if (first_cluster)
		{
		BLKBUFF *pblk;
		byte *b;

  			sector=pc_cl2sector(pefile->pobj->pdrive,first_cluster);
			pblk=pc_read_blk(pefile->pobj->pdrive, sector);
			if (!pblk)
				return 0;
			copybuff(pblk->data, (void *)RTFS_EXTATTRIBUTE_SIGNATURE, RTFS_EXTATTRIBUTE_SIGNATURE_SIZE);
			b=pblk->data;
			b+=RTFS_EXTATTRIBUTE_SIGNATURE_SIZE;
			fr_DWORD(b,dstart);
			retval=pc_write_blk(pblk);
			pc_discard_buf(pblk);
			pefinode->extended_attribute_dstart=dstart;
		}
	}
	return retval;
}
#endif

BOOLEAN pc_efilio32_open_complete(PC_FILE *pefile)
{
FINODE *pefinode;
dword ltemp;

    pefinode = pefile->pobj->finode;

    /* When we get here the load completed */
#if (INCLUDE_EXFATORFAT64)
	pefile->fc.plus.file_pointer.val64 = 0;
	pefile->fc.plus.region_byte_base.val64 = 0;
#endif
	pefile->fc.plus.file_pointer.val32 = 0;
	pefile->fc.plus.region_byte_base.val32 = 0;


    if (pefinode->e.x->pfirst_fragment)
    {
        pefile->fc.plus.pcurrent_fragment = pefinode->e.x->pfirst_fragment;
        pefile->fc.plus.region_block_base = pc_cl2sector(pefinode->my_drive, pefinode->e.x->pfirst_fragment->start_location);
    }
    else
    {
        pefile->fc.plus.pcurrent_fragment = 0;
        pefile->fc.plus.region_block_base = 0;
    }


#if(INCLUDE_EXTENDED_ATTRIBUTES)
	pefinode->extended_attribute_dstart=pc_efilio32_get_dstart(pefile);
#else
	pefinode->extended_attribute_dstart=0;
#endif

//===
    /* Seek internally to zero to force file pointer to data start
       (from extended attributes) if it is non zero */
    return(_pc_efiliocom_lseek(pefile,pefinode, 0,0, PSEEK_SET, 0,&ltemp));
}

int pc_efilio_async_open32_continue(PC_FILE *pefile)
{
    if (!pefile || !pefile->pobj || !pefile->pobj->finode)
    { /* Shouldn't happen */
        ERTFS_ASSERT(rtfs_debug_zero())
        return(PC_ASYNC_ERROR);
    }
    return(pc_efinode_async_load_continue(pefile->pobj->finode));
}

BOOLEAN _pc_efilio32_common_fstat(PC_FILE *pefile, ERTFS_EFILIO_STAT *pestat)                              /*__apifn__*/
{
DDRIVE *pdr;
FINODE *pefinode;

    pdr = pefile->pobj->pdrive;
    pefinode =  pefile->pobj->finode;
    /* syncronize the file pointer with the finode */
    _pc_efiliocom_sync_current_fragment(pefile,pefinode);

#if (INCLUDE_EXFATORFAT64)
	if (ISEXFATORFAT64(pefile->pobj->pdrive))
	{
		pestat->file_size_hi       = (pefinode->fsizeu.fsize64>>32)&0xffffffff;
		pestat->file_size_lo       = pefinode->fsizeu.fsize64&0xffffffff;
		pestat->allocated_size_hi  = (pefinode->e.x->alloced_size_bytes.val64>>32)&0xffffffff;
		pestat->allocated_size_lo  = pefinode->e.x->alloced_size_bytes.val64&0xffffffff;
	}
	else
#endif
	{
		pestat->file_size_hi       = 0;
		pestat->file_size_lo       = pefinode->fsizeu.fsize;
		pestat->allocated_size_hi  = 0;
		pestat->allocated_size_lo  = pefinode->e.x->alloced_size_bytes.val32;
	}


    pestat->pfirst_fragment[0] = pefinode->e.x->pfirst_fragment;
    if (pefinode->e.x->pfirst_fragment)
        pestat->fragments_in_file = pc_fraglist_count_list(pefinode->e.x->pfirst_fragment,0);
    else
        pestat->fragments_in_file = 0;
    /* First cluster in the file */
    pestat->first_cluster    =  pc_finode_cluster(pdr, pefinode);
    /* cluster used for data in the file */

#if (INCLUDE_EXFATORFAT64)
	if (ISEXFATORFAT64(pdr))
	{
		pestat->allocated_clusters =  pc_byte2clmod64(pdr, (dword)((pefinode->fsizeu.fsize64>>32)&0xffffffff), (dword)(pefinode->fsizeu.fsize64&0xffffffff));
	}
	else
#endif
		pestat->allocated_clusters =  pc_byte2clmod(pdr, pefinode->fsizeu.fsize);

    /* cluster pre-allocated for data in the file */
    pestat->preallocated_clusters = pefinode->e.x->alloced_size_clusters;
    /* cluster to link */
    pestat->clusters_to_link = _pc_efinode_count_to_link(pefinode,pefile->fc.plus.allocation_policy);
    return(TRUE);
}

/* Called by pc_efilio_close() and pc_cfilio_extract() */
BOOLEAN _pc_efilio32_close(PC_FILE *pefile)
{
#if (!RTFS_CFG_READONLY) /* Read only file system, do not process flush or temp file on close */
    if (pefile->flag & ( PO_RDWR | PO_WRONLY ) )
    if (pc_check_file_dirty(pefile))
    {
        if (pefile->fc.plus.allocation_policy & PCE_TEMP_FILE)
        { /* If it's a temp file mark dirent deleted and quue all clusters
             to be freed as excess */
            if (pefile->pobj->finode->opencount == 1)
            {   /* Force all clusters to be freed as excess */
            DROBJ *pobj;
            FINODE *finode;

                pobj = pefile->pobj;
                finode = pobj->finode;
                pc_pfinode_cluster(pobj->pdrive,finode,0);
                /* No need to flush */
                pc_set_file_buffer_dirty(finode, FALSE);
#if (INCLUDE_EXFATORFAT64)
				if (ISEXFATORFAT64(pefile->pobj->pdrive))
				{
					finode->fsizeu.fsize64 = 0;
	   				if (!pcexfat_update_by_finode(pobj->finode, pobj->finode->my_index, FALSE, 0, TRUE))
						return(FALSE);
				}
				else
#endif
				{
					/* Now flush the inode but mark deleted */
					finode->fsizeu.fsize = 0;
					finode->fname[0] = PCDELETEESCAPE;
					if (!pc_delete_lfn_info(pobj)) /* Delete lonf file name info associated with DROBJ */
						return(FALSE);
					/* Convert to native. Overwrite the existing inode.  */
					if (!pc_update_inode(pobj, FALSE, 0))
						return(FALSE);
				}
            }
        }
        else
        {
            if (!_pc_efilio32_flush(pefile,0))
                return(FALSE);  /* _pc_efilio32_flush has set errno */
        }
        /* Closing a file in write mode clear the write exclusive
           flag if it is set */
        pefile->pobj->finode->openflags &= ~OF_WRITEEXCLUSIVE;
        /* If closing a transaction file clear the transaction field in the finode */
        /* Okay because never more than one open at the same time in transaction mode */
        if (pefile->fc.plus.allocation_policy & PCE_TRANSACTION_FILE)
            pefile->pobj->finode->openflags &= ~OF_TRANSACTION;
    }
#endif
    /* Release the FD and its core   */
    pc_freefile(pefile);
    return(TRUE);
}

BOOLEAN _pc_efilio32_read(PC_FILE *pefile, byte *buf, dword count, dword *nread)
{
FINODE *pefinode;

    pefinode =  pefile->pobj->finode;
#if (INCLUDE_EXFATORFAT64)
	if (ISEXFATORFAT64(pefile->pobj->pdrive))
	{
	ddword l;
		l = pefinode->fsizeu.fsize64 - pefile->fc.plus.file_pointer.val64;
		if ((ddword) count > l) /* Truncate the read count if needed */
			count = (dword)l;
	}
	else
#endif
	{
		dword ltemp;
		ltemp = pefinode->fsizeu.fsize - pefile->fc.plus.file_pointer.val32;
		if (count > ltemp) /* Truncate the read count if needed */
			count = ltemp;
	}
    *nread =  0;
    if (count)
    {
        /* syncronize the file pointer with the finode */
        _pc_efiliocom_sync_current_fragment(pefile,pefile->pobj->finode);
        if (!_pc_efiliocom_io(pefile, pefile->pobj->finode, buf, count, TRUE,FALSE)) /* returns true when count is 0 */
            return(FALSE);
        *nread = count;
    }
    return(TRUE);
}
