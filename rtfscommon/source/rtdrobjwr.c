/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* RTDROBJ.C - Directory object manipulation routines */

#include "rtfs.h"
#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */

/**************************************************************************
    PC_MKNODE -  Create an empty subdirectory or file.

 Description
    Creates a file or subdirectory inode depending on the flag values in
    attributes. A pointer to an inode is returned for further processing. See
    po_open(),po_close(), pc_mkdir() et al for examples.

    Note: After processing, the DROBJ must be released by calling pc_freeobj.

 Returns
    Returns a pointer to a DROBJ structure for further use, or NULL if the
    inode name already exists or path not found.
**************************************************************************/

/* Make a node from path and attribs create and fill in pobj             */
/* Note: the parent directory is locked before this routine is called    */
void pc_init_directory_cluster(DROBJ *pobj, byte *pbuffer);

#if (INCLUDE_EXFATORFAT64)
/* Note: This is a new way of doing things for all. Leave conditional until testing is completer */
DROBJ *pc_mknode(DROBJ *pmom ,byte *filename, byte *fileext, byte attributes, FINODE *infinode,int use_charset) /*__fn__*/
#else
DROBJ *pc_mknode(DROBJ *pmom ,byte *filename, byte *fileext, byte attributes, dword incluster,int use_charset) /*__fn__*/
#endif
{
    DROBJ *pobj;
    BOOLEAN ret_val, use_ubuff,initialize_fat_dir_cluster;
    dword new_cluster,cluster;
    dword user_buffer_size;
    byte *puser_buffer;
    BLKBUFF *pbuff;
    DDRIVE *pdrive;
    byte attr;
#if (INCLUDE_EXFATORFAT64)
    dword incluster = 0;
#endif

    ret_val = TRUE;
    new_cluster = 0;
    pobj = 0;
    pbuff = 0;
    puser_buffer = 0;
    initialize_fat_dir_cluster = FALSE;

    if (!pmom || !pmom->pdrive)
    {
        rtfs_set_errno(PEINTERNAL, __FILE__, __LINE__); /* pc_mknode: Internal error*/
        return (0);
    }
    /* Make sure the file/directory name is legal */
    if (!pc_cs_validate_filename(filename, fileext, use_charset))
    {
        rtfs_set_errno(PEINVALIDPATH, __FILE__, __LINE__);
        return (0);
    }
    pdrive = pmom->pdrive;

#if (INCLUDE_EXFATORFAT64)
	if (infinode)
    	incluster = pc_finode_cluster(pmom->pdrive, infinode);
#endif

    use_ubuff = FALSE;
    cluster = incluster;
    if (attributes & ADIRENT)
    {
        /*Unless renaming a directory we grab a cluster for a new dir */
        if (!incluster)
        {
            /* pc_alloc_dir will set errno */
            cluster = pc_alloc_dir(pdrive,pmom);
            if (!cluster)
                goto clean_and_fail;
            else
            {
                new_cluster = cluster;  /* Remeber so we can release if we fail */
            }
#if (INCLUDE_EXFATORFAT64)			/* Should be a wrapper function */
   			if ( ISEXFATORFAT64(pdrive) )
			{
 				pcexfat_set_volume_dirty(pdrive);
 #if (EXFAT_FAVOR_CONTIGUOUS_FILES)	 /* Update free_contig_pointer for exfat if enabled */
				/* Update location to allocate file data from if necessary */
				if (new_cluster >= pdrive->drive_info.free_contig_pointer)
        			pdrive->drive_info.free_contig_pointer = new_cluster + 1;
        		if (pdrive->drive_info.free_contig_pointer >= pdrive->drive_info.maxfindex)
        			pdrive->drive_info.free_contig_pointer = pdrive->drive_info.free_contig_base;
#endif

			}
#endif
            /* Use the user buffer if we are creating a directory and it large enough to hold a whole cluster */
            puser_buffer = pc_claim_user_buffer(pdrive, &user_buffer_size,(dword) pdrive->drive_info.secpalloc); /* released on cleanup */
            if (puser_buffer)
                use_ubuff = TRUE;
        }
    }
    /* For a subdirectory. First make it a simple file. We will change the
        attribute after all is clean */
    attr = attributes;
    if (attr & ADIRENT)
        attr = ANORMAL;

    /* Allocate an empty DROBJ and FINODE to hold the new file   */
    pobj = pc_allocobj();
    if (!pobj)
    {
        goto clean_and_fail;
    }
    /* Set up the drive link */
    pobj->pdrive = pmom->pdrive;

    /* Load the inode copy name,ext,attr,cluster, size,datetime  */
    /* Convert pobj to native and stitch it in to mom   */
#if (INCLUDE_EXFATORFAT64)
   	if ( ISEXFATORFAT64(pdrive) )
   	{
   	byte secondaryflags;
	dword size_hi, size_lo;

		size_hi = size_lo = 0;
		if (attributes & ADIRENT)
			size_lo = (dword) pdrive->drive_info.bytespcluster;

		secondaryflags = EXFATALLOCATIONPOSSIBLE;
		secondaryflags |= EXFATNOFATCHAIN;


		*pobj->finode = *pmom->finode;
    	if (!pcexfat_insert_inode(pobj , pmom, attributes, infinode, cluster, filename, secondaryflags, size_hi, size_lo, use_charset))
		{	/* If it failed due to PENOSPC try to grow the directory and try again */
			if (get_errno() != PENOSPC)
        		goto clean_and_fail;
			rtfs_clear_errno();
			if (!pcexfat_grow_directory(pmom))
				goto clean_and_fail;
			if (!pcexfat_insert_inode(pobj , pmom, attributes, infinode, cluster, filename, secondaryflags, size_hi, size_lo, use_charset))
				goto clean_and_fail;
		}
       	pcexfat_clear_volume_dirty(pdrive);
   	}
   	else
#endif
	{
    	if (!pc_insert_inode(pobj , pmom, attr, cluster, filename, fileext,use_charset))
        	goto clean_and_fail;

	}
	if ( attributes & ADIRENT)
		initialize_fat_dir_cluster = TRUE;
    new_cluster = 0; /* Will not release now since part of tree */

    /* Now if we are creating subdirectory we have to make the DOT and DOT DOT
        inodes and then change pobj s attribute to ADIRENT
        The DOT and DOTDOT are not buffered inodes. We are simply putting
        the to disk  */
    if ( initialize_fat_dir_cluster)
    {
        if (use_ubuff)
        { /* A create, not a rename and user buffer holds the whole cluster zero the cluster and initialize . and .. */
            dword n_blocks, blockno;
            n_blocks = (dword) pdrive->drive_info.secpalloc;
            blockno  =  pc_cl2sector(pdrive , cluster);
            rtfs_memset(puser_buffer, (byte) 0,n_blocks<<pdrive->drive_info.log2_bytespsec);
#if (INCLUDE_EXFATORFAT64)
			if (!ISEXFATORFAT64(pdrive))
            	pc_init_directory_cluster(pobj, puser_buffer);
#else
           	pc_init_directory_cluster(pobj, puser_buffer);
#endif
            if (!block_devio_xfer(pdrive,blockno, puser_buffer, n_blocks, FALSE))
                goto clean_and_fail;
        }
        else
        {   /* Use the block buffer pool.. if user buffer is too small or we are moving a subdirectory */
            if (incluster)
                pbuff = pc_read_blk( pdrive , pc_cl2sector(pdrive , incluster));
            else
            {
                /* If no user buffer use the block buffer system to zero the first cluster */
                if (!pc_clzero( pdrive , cluster ) )
                    goto clean_and_fail;
#if (INCLUDE_EXFATORFAT64)
   				if (ISEXFATORFAT64(pdrive))
   					goto first_cluster_initialized;
#endif
                /* If now get a block to place . and .. */
                pbuff = pc_init_blk( pdrive , pc_cl2sector(pdrive , cluster));
            }
            if (!pbuff)
                goto clean_and_fail;
            /* Update . and .. overides existing if moving a directory */
            pc_init_directory_cluster(pobj, pbuff->data);
            /* Write the cluster out   */
            if ( !pc_write_blk ( pbuff ) )
                goto clean_and_fail;
            pc_release_buf(pbuff);
#if (INCLUDE_EXFATORFAT64)
first_cluster_initialized:
#endif
            pbuff = 0;
        }
#if (INCLUDE_EXFATORFAT64)
   		if ( ISEXFATORFAT64(pdrive) )
			;	/* No need to change the attribute type on exFat directories because there is no back link (..) */
		else
#endif
		{
        	/* And write the node out with the original attributes   */
        	pobj->finode->fattribute = (byte)(attributes|ARCHIVE);
        	/* Convert to native. Overwrite the existing inode.Set archive/date  */
        	if (!pc_update_inode(pobj, TRUE, DATESETCREATE|DATESETUPDATE|DATESETACCESS))
        	{
            	goto clean_and_fail;
        	}
		}
	}
    if (puser_buffer)
        pc_release_user_buffer(pdrive, puser_buffer);
   	ret_val = fatop_flushfat(pdrive);
    if (ret_val)
    {
        return (pobj);
    }
    else
    {
        pc_freeobj(pobj);
        return (0);
    }
clean_and_fail:
    if (pbuff)
        pc_discard_buf(pbuff);
    if (puser_buffer)
        pc_release_user_buffer(pdrive, puser_buffer);
    if (pobj)
        pc_freeobj(pobj);
    if (new_cluster)
    {
        fatop_clrelease_dir(pdrive , new_cluster);
    }
    return (0);
}

/*  Init DOT and DOT DOT directory entries in the buffer */
void pc_init_directory_cluster(DROBJ *pobj, byte *pbuffer)
{
    DDRIVE *pdrive;
    DOSINODE *pdinodes;
    FINODE lfinode;
    dword cluster, cltemp;
    DATESTR crdate;
    byte dot_str[4]; /* Make DOT and DOTDOT strings */
    byte null_str[4];

    pdrive = pobj->pdrive;
    cluster = pc_finode_cluster(pdrive, pobj->finode);

    /* Make the DOT and DOT DOT inodes */
    pdinodes = (DOSINODE *) pbuffer;
    /* Load DOT and DOTDOT in native form                     */
    /* DOT first. It points to the begining of this sector    */
    dot_str[0] = (byte) '.';dot_str[1]=0;
    null_str[0] = 0;

    /* Load the time and date stamp to be used for "." and ".."
       from the subdirectory that we are creating. In this way
       the three directory entries will all have the same timestamp
       value */
    crdate.time = pobj->finode->ftime;
    crdate.date = pobj->finode->fdate;
    pc_init_inode( &lfinode, dot_str,null_str, ADIRENT|ARCHIVE,
                        cluster, /*size*/ 0L , &crdate);
    /* And to the buffer in intel form   */
    pc_ino2dos (pdinodes, &lfinode);
    /* Now DOTDOT points to mom s cluster   */
    cltemp = pc_get_parent_cluster(pdrive, pobj);
    dot_str[0] = (byte)'.';dot_str[1] = (byte)'.';
    dot_str[2] = 0;
    null_str[0] = 0;
    pc_init_inode( &lfinode, dot_str, null_str, ADIRENT|ARCHIVE, cltemp,
                /*size*/ 0L , &crdate);
    /* And to the buffer in intel form   */
    pc_ino2dos (++pdinodes, &lfinode );
}


/***************************************************************************
    PC_CLZERO -  Fill a disk cluster with zeroes

 Description
    Write zeroes into the cluster at clusterno on the drive pointed to by
    pdrive. Used to zero out directory and data file clusters to eliminate
    any residual data.

 Returns
    Returns FALSE on a write erro.

****************************************************************************/

/* Write zeros to all blocks in a cluster   */
BOOLEAN pc_clzero(DDRIVE *pdrive, dword cluster)                  /*__fn__*/
{
    BOOLEAN ret_val;
    dword currbl,n_left,n_todo;
    dword user_buffer_size;
    byte *puser_buffer;

    currbl = pc_cl2sector(pdrive , cluster);
    if (!currbl)
        return (FALSE);

    puser_buffer = pc_claim_user_buffer(pdrive, &user_buffer_size, 0); /* released on cleanup */
    if (!puser_buffer)
        return (FALSE);

    /* Fill as much of the cluster as we can per write */
    ret_val = FALSE;
    n_left = pdrive->drive_info.secpalloc;


    while (n_left)
    {
        n_todo = n_left;
        if (n_todo > user_buffer_size)
            n_todo = user_buffer_size;
        rtfs_memset(puser_buffer, (byte) 0,n_todo<<pdrive->drive_info.log2_bytespsec);
        ret_val = block_devio_xfer(pdrive, currbl, puser_buffer, n_todo, FALSE);
        if (!ret_val)
            break;
        n_left -= n_todo;
        currbl += n_todo;
    }
    pc_release_user_buffer(pdrive, puser_buffer);
    return(ret_val);
}

/***************************************************************************
    PC_RMNODE - Delete an inode unconditionally.

 Description
    Delete the inode at pobj and flush the file allocation table. Does not
    check file permissions or if the file is already open. (see also pc_unlink
    and pc_rmdir). The inode is marked deleted on the disk and the cluster
    chain associated with the inode is freed. (Un-delete wo not work)

 Returns
    Returns TRUE if it successfully deleted the inode an flushed the fat.

*****************************************************************************/


/* Delete a file / dir or volume. Do not check for write access et al     */
/* Note: the parent directory is locked before this routine is called    */
BOOLEAN pc_rmnode( DROBJ *pobj)                                         /*__fn__*/
{
    dword cluster;
    DDRIVE *pdrive;
    BOOLEAN is_a_dir;
    /* Do not delete anything that has multiple links   */
    if (pobj->finode->opencount > 1)
    {
        rtfs_set_errno(PEACCES, __FILE__, __LINE__); /* pc_rmnode() - directory entry is in use */
err_ex:
        return (FALSE);
    }

#if (INCLUDE_EXFATORFAT64)
   	if (ISEXFATORFAT64(pobj->pdrive))
   	{
		return(pcexfat_rmnode(pobj));
   	}
#endif
    is_a_dir = pc_isadir(pobj);
    pdrive = pobj->pdrive;


    /* Mark it deleted and unlink the cluster chain   */
    pobj->finode->fname[0] = PCDELETEESCAPE;

    cluster = pc_finode_cluster(pdrive,pobj->finode);

    if (!pc_delete_lfn_info(pobj)) /* Delete lonf file name info associated with DROBJ */
        goto err_ex;

    /* We free up store right away. Do not leave cluster pointer
    hanging around to cause problems. */
    pc_pfinode_cluster(pdrive,pobj->finode,0);
    /* Convert to native. Overwrite the existing inode.Set archive/date  */
    if (pc_update_inode(pobj, FALSE, 0))
    {
        /* If there is no cluster chain to delete don't call freechain */
        if (!cluster)
            return(TRUE);
        /* And clear up the space   */
        /* If it is a directory make sure the blocks contained within the
           cluster chain are flushed from the block buffer pool */
          if (is_a_dir)
                pc_flush_chain_blk(pdrive, cluster);
        /* Set min to 0 and max to 0xffffffff to eliminate range checking on the
           cluster chain and force removal of all clusters */
           if (!fatop_freechain(pdrive, cluster, LARGEST_DWORD))
           {
                /* If freechain failed still flush the fat in INVALID CLUSTER
                   was the failure condition */
                if (get_errno() == PEINVALIDCLUSTER)
                   fatop_flushfat(pobj->pdrive);
                return(FALSE);
           }
           else if ( fatop_flushfat(pobj->pdrive) )
            return (TRUE);
    }
    /* If it gets here we had a problem   */
    return(FALSE);
}

/**************************************************************************
    PC_UPDATE_INODE - Flush an inode to disk

Summary

Description
    Read the disk inode information stored in pobj and write it to
    the block and offset on the disk where it belongs. The disk is
    first read to get the block and then the inode info is merged in
    and the block is written. (see also pc_mknode() )

Returns
    Returns TRUE if all went well, no on a write error.

*****************************************************************************
*/

/* Take a DROBJ that contains correct my_index & my_block. And an inode.
    Load the block. Copy the inode in and write it back out */

/* Update the timestamp fields, called by routines that read/write/modify when the entry is accessed */
void pc_update_finode_datestamp(FINODE *pfinode, BOOLEAN set_archive, int set_date_mask) /*__fn__*/
{
    DATESTR crdate;
#if (INCLUDE_EXFATORFAT64)
	if (ISEXFATORFAT64(pfinode->my_drive))
	{
		pcexfat_update_finode_datestamp(pfinode, set_archive, set_date_mask);
		return;
	}
#endif
    OS_CLAIM_FSCRITICAL()
    /* Set the archive bit and the date   */
    if (set_archive)
        pfinode->fattribute |= ARCHIVE;
    if (set_date_mask)
    {	/* Need to update FAT modified support */

        pc_getsysdate(&crdate);
		if (set_date_mask & DATESETCREATE)
		{
        	pfinode->ctime = crdate.time;
        	pfinode->cdate = crdate.date;
		}
		if (set_date_mask & DATESETUPDATE)
		{
        	pfinode->ftime = crdate.time;
        	pfinode->fdate = crdate.date;
		}
		if (set_date_mask & DATESETACCESS)
		{
        	pfinode->atime = crdate.time;
        	pfinode->adate = crdate.date;
		}
    }
    OS_RELEASE_FSCRITICAL()
}

BOOLEAN pc_update_by_finode(FINODE *pfinode, int entry_index, BOOLEAN set_archive, int set_date_mask) /*__fn__*/
{
    BLKBUFF *pbuff;
    DOSINODE *pi;

#if (INCLUDE_EXFATORFAT64)
   	if ( ISEXFATORFAT64(pfinode->my_drive) )
   	{
   		return (pcexfat_update_by_finode(pfinode, entry_index, set_archive, set_date_mask, FALSE)); /* Delete == FALSE */
   	}
#endif
    if ( entry_index >= pfinode->my_drive->drive_info.inopblock || entry_index < 0 )  /* Index into block */
    {
        rtfs_set_errno(PEINTERNAL, __FILE__, __LINE__); /* pc_update_inode: Internal error, illegal inode index */
        return (FALSE);
    }
    /* Set the archive bit and the datestamp according to caller's instructions */
    pc_update_finode_datestamp(pfinode, set_archive, set_date_mask);
    /* Read the data   */
    pbuff = pc_read_blk(pfinode->my_drive, pfinode->my_block);
    if (pbuff)
    {
        pi = (DOSINODE *) pbuff->data;
        /* Copy it off and write it   */
        pc_ino2dos( (pi+entry_index), pfinode );
        if (!pc_write_blk(pbuff))
        {
            pc_discard_buf(pbuff);
            return (FALSE);
        }
        else
        {
            pc_release_buf(pbuff);
            return (TRUE);
        }
    }
    return (FALSE);
}

    /* Load an in memory inode up with user supplied values  */
void pc_init_inode(FINODE *pdir, KS_CONSTANT byte *filename,
            KS_CONSTANT byte *fileext, byte attr,
            dword cluster, dword size, DATESTR *crdate)
{
    /* Copy the file names and pad with spaces   */
    pc_cppad(pdir->fname,(byte*)filename,8);
    pc_cppad(pdir->fext,(byte*)fileext,3);
    pdir->fattribute = attr;

    pdir->reservednt = 0;
    pdir->create10msincrement = 0;

    pdir->ftime = crdate->time;
    pdir->fdate = crdate->date;
    pdir->ctime	= crdate->time;
    pdir->cdate	= crdate->date;
    pdir->adate = crdate->date;
    pdir->atime	= crdate->time;

    /* Note: fclusterhi is of resarea in fat 16 system and cluster>>16 is 0*/
    pdir->fclusterhi = (word)(cluster >> 16);
    pdir->fcluster = (word) cluster;
    pdir->fsizeu.fsize = size;
    pc_zero_lfn_info(pdir);

}

/***************************************************************************
    PC_INO2DOS - Convert  an in memory inode to a dos disk entry.

 Description
    Take in memory native format inode information and copy it to a
    buffer. Translate the inode to INTEL byte ordering during the transfer.

 Returns
    Nothing

***************************************************************************/
/* Un-Make a disk directory entry         */
/* Convert an inmem inode to dos  form.   */
void pc_ino2dos (DOSINODE *pbuff, FINODE *pdir)                 /*__fn__*/
{
    pc_cs_ascii_strn2upper((byte *)&pbuff->fname[0],(byte *)&pdir->fname[0],8);      /*X*/
    pc_cs_ascii_strn2upper((byte *)&pbuff->fext[0],(byte *)&pdir->fext[0],3);            /*X*/
    pbuff->fattribute = pdir->fattribute;               /*X*/
    /* If the first character is 0xE5 (valid kanji) convert it to 0x5 */
    if (pdir->fname[0] == PCDELETE) /* 0XE5 */
        pbuff->fname[0] = 0x5;

    /* If rmnode wants us to delete the file set it to 0xE5 */
    if (pdir->fname[0] == PCDELETEESCAPE)
        pbuff->fname[0] = PCDELETE;

    pbuff->reservednt = pdir->reservednt;
    pbuff->create10msincrement = pdir->create10msincrement;;

#if (KS_LITTLE_ENDIAN)
    pbuff->ftime = pdir->ftime;
    pbuff->fdate = pdir->fdate;
    pbuff->ctime = pdir->ctime;
    pbuff->cdate = pdir->cdate;
    pbuff->adate = pdir->adate;

    pbuff->fcluster = pdir->fcluster;
    /* Note: fclusterhi is of resarea in fat 16 system */
    pbuff->fclusterhi = pdir->fclusterhi;
    pbuff->fsize = pdir->fsizeu.fsize;
#else
    fr_WORD((byte *) &pbuff->ftime,pdir->ftime);        /*X*/
    fr_WORD((byte *) &pbuff->fdate,pdir->fdate);        /*X*/
    fr_WORD((byte *) &pbuff->ctime,pdir->ctime);
    fr_WORD((byte *) &pbuff->cdate,pdir->cdate);
    fr_WORD((byte *) &pbuff->adate,pdir->adate);

    fr_WORD((byte *) &pbuff->fcluster,pdir->fcluster);  /*X*/
    /* Note: fclusterhi is of resarea in fat 16 system */
    fr_WORD((byte *) &pbuff->fclusterhi,pdir->fclusterhi);  /*X*/
    fr_DWORD((byte *) &pbuff->fsize,pdir->fsizeu.fsize);       /*X*/
#endif
}
#endif /* Exclude from build if read only */
