/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2010
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* RTEXFATDIRWRITE.C - Directory level write functions for exFat.

    Routines Exported from this file include:
			pcexfat_grow_directory	 - Called by pc_mknode  (Note: Can do more localizing of scope here).
			pcexfat_insert_inode	 - Called by pc_mknode
			pcexfat_update_by_finode - Called by pc_update_by_finode
			pcexfat_rmnode			 - Called by pc_rmnode.
			pcexfat_mvnode			 - Called by pc_mv.

*/

#include "rtfs.h"


static int pcexfat_segoffsettosegoffsetandindex(DDRIVE *pdr, int offset, SEGDESC *pseg, int *index_return)
{
int segindex,segblock;
	segindex = offset + pseg->segindex;
	segblock = segindex/pdr->drive_info.inopblock;
	segindex -= segblock * pdr->drive_info.inopblock;
	*index_return = segindex;
	return(segblock);
}

static dword pcexfat_datestrtodword(DATESTR d)
{
	dword l;
	l = d.date;
	l <<= 16;
	l |= (dword)d.time;
	return(l);
}

BOOLEAN _pc_bfilio_flush(PC_FILE *pefile);


BOOLEAN pcexfat_grow_directory(DROBJ *pobj)
{
dword new_cluster, n_contig;
int is_error;
DDRIVE *pdr;
BOOLEAN ret_val = FALSE;
REGION_FRAGMENT *pf = 0;

	pdr = pobj->pdrive;
    if ( pc_isroot(pobj))
 		pf = pcexfat_load_root_fragments(pdr);
	else
	{
		if (M64ISZERO(pobj->finode->fsizeu.fsize64) || !pc_finode_cluster(pobj->pdrive, pobj->finode))
		{
			rtfs_set_errno(PEINTERNAL, __FILE__, __LINE__);
			return(FALSE);
		}
		/* Load the fragment chain USNG EXISTING CODE THAT WORKS WITH pbasic_fragment */
		if (!pcexfat_bfilio_load_all_fragments(pobj->finode) || !pobj->finode->pbasic_fragment)
			pf = 0;
		else
			pf = pobj->finode->pbasic_fragment;
		pobj->finode->pbasic_fragment = 0;			/* Don't leave it hanging around to cause trouble */

    }
	if (pf)
	{
		/* Allocate a cluster */
		new_cluster = exfatop_find_contiguous_free_clusters(pdr, 0, pdr->drive_info.maxfindex, 1, 1, &n_contig, &is_error);
		if (!new_cluster)
    		goto cleanup_and_exit;
		/* Zero out the cluster    */
		if (!pc_clzero( pdr , new_cluster) )
        	goto cleanup_and_exit;
    	if (!exfatop_remove_free_region(pdr, new_cluster, 1))
    		goto cleanup_and_exit;
    	/* Link it to the end of the chain */
    	if (!fatop_pfaxxterm(pobj->pdrive, new_cluster))
    		goto cleanup_and_exit;
    	if (!fatop_pfaxx(pobj->pdrive, pc_end_fragment_chain(pf)->end_location, new_cluster))
    		goto cleanup_and_exit;
#if (EXFAT_FAVOR_CONTIGUOUS_FILES)	 /* pcexfat_grow_directory update free_contig_pointer to allocate file data from if necessary */
		if (new_cluster >= pobj->pdrive->drive_info.free_contig_pointer)
   			pobj->pdrive->drive_info.free_contig_pointer = new_cluster + 1;
   		if (pobj->pdrive->drive_info.free_contig_pointer >= pobj->pdrive->drive_info.maxfindex)
   			pobj->pdrive->drive_info.free_contig_pointer = pobj->pdrive->drive_info.free_contig_base;
#endif

    	/* Update the size */
    	if (pc_isroot(pobj))
			ret_val = TRUE;
		else
    	{
			pobj->finode->fsizeu.fsize64 = M64PLUS32(pobj->finode->fsizeu.fsize64,pdr->drive_info.bytespcluster);
			/* Update Secondary Flags because we have a chain */
			pobj->finode->exfatinode.GeneralSecondaryFlags &= ~EXFATNOFATCHAIN;
			/* Update the inode  */
			ret_val = pc_update_inode(pobj, TRUE, DATESETUPDATE);
    	}
	}

cleanup_and_exit:
	/* Free the fragment list */
    if (pf)
        pc_fraglist_free_list(pf);
   return(ret_val);
}



DATESTR dwordToDateStr(dword indword)
{
DATESTR x;
#if (KS_LITTLE_ENDIAN)
	x.date = (word) ((indword >> 16) & 0xffff);
	x.time = (word) ((indword) & 0xffff);
#else
	x.date = (word) ((indword) & 0xffff);
	x.time = (word) ((indword >> 16) & 0xffff);
#endif
	return(x);
}



static DATESTR datetimeToDateStr(word date , word time)
{
DATESTR x;
	x.date = date;
	x.time = time;
	return(x);
}


BOOLEAN pcexfat_insert_inode(DROBJ *pobj , DROBJ *pmom, byte _attr, FINODE *infinode, dword initcluster, byte *filename, byte secondaryflags, dword sizehi, dword sizelow, int use_charset)
{
BOOLEAN retval;
EXFATFILEPARSEOBJ filenameobj;
EXFATFILEENTRY fileentry;
EXFATSTREAMEXTENSIONENTRY streamextensionentry;
BLKBUFF *rbuf[3];
word *pUnicodeName;
int segmentindex;
byte attr;
dword my_block = 0;
int my_index= 0;

	if (infinode)
    	initcluster = pc_finode_cluster(pmom->pdrive, infinode);

    rbuf[0] = rbuf[1] = rbuf[2] = 0;

	attr = _attr | ARCHIVE;
	{
	DIRBLK *pd;

		/* Set up pobj      */
		pobj->pdrive = pmom->pdrive;
		pobj->isroot = FALSE;
		pd = &pobj->blkinfo;
		pd->my_block = pd->my_frstblock = pc_firstblock(pmom);
		pd->my_exNOFATCHAINfirstcluster = pcexfat_getexNOFATCHAINfirstcluster(pmom);
		pd->my_exNOFATCHAINlastcluster = pcexfat_getexNOFATCHAINlastcluster(pmom);

		if (!pd->my_block)
		{
			rtfs_set_errno(PEINTERNAL, __FILE__, __LINE__);
			return (FALSE);
		}
		else
			pd->my_index = 0;
	}

	/* Get unicode filename, upcased file name, checksum, hashcode */
	if (!pcexfat_filenameobj_init(pobj, filename, &filenameobj, use_charset))
        return(FALSE);
	/* Files and directories add 2 segments for the file segment and stream segment */
	if ((_attr & AVOLUME))
		filenameobj.segmentsRequired = 1;
	else
	{
		filenameobj.segmentsRequired = filenameobj.NameSegments + 2;
	}

	retval = pcexfat_findinbyfilenameobj(pobj, &filenameobj, FALSE, GET_INODE_MATCH);
	if (retval)
	{
        rtfs_set_errno(PEEXIST, __FILE__, __LINE__);
		goto cleanup_and_exit;
	}
	else if (get_errno() != PENOENT)
		goto cleanup_and_exit;

	/* Make sure we have enough space */
	if (filenameobj.segmentsRequired > filenameobj.Freesegments.nsegs)
	{
		/* Report an error. The caller will try to grow the directory and try again */
        rtfs_set_errno(PENOSPC, __FILE__, __LINE__);
		goto cleanup_and_exit;
	}
    rtfs_memset(&fileentry, 0, sizeof(fileentry));
    rtfs_memset(&streamextensionentry, 0, sizeof(streamextensionentry));

	if ((_attr & AVOLUME) == 0)	/* Build file name and stream entries */
	{
	    fileentry.EntryType					= 0x85;
	    fileentry.SecondaryCount			= (byte) (filenameobj.segmentsRequired-1 & 0xff);
	    fileentry.SetChecksum				= 0;
	    fileentry.FileAttributes			= (word) attr;
/* 	    fileentry.Reserved1[2];   */

	    streamextensionentry.EntryType		= 0xC0;
//	    streamextensionentry.Reserved1;
	    streamextensionentry.NameLen		= filenameobj.NameLen;
	    streamextensionentry.NameHash       = filenameobj.NameHash;

		if (infinode)
		{ /* If renaming use the timestamps and sizes from the original */

	    	fileentry.CreateUtcOffset					=	infinode->exfatinode.CreateUtcOffset;
	    	fileentry.Create10msIncrement				=	infinode->exfatinode.Create10msIncrement;

	    	//fileentry.LastModifiedTimeStamp				=	dwordToDateStr(infinode->exfatinode.LastModifiedTimeStamp);
	    	//fileentry.CreateTimeStamp           		=	dwordToDateStr(infinode->exfatinode.CreateTimeStamp);
	    	//fileentry.LastAccessedTimeStamp				=	dwordToDateStr(infinode->exfatinode.LastAccessedTimeStamp);
	    	fileentry.CreateTimeStamp           		=	datetimeToDateStr(infinode->cdate,infinode->ctime);
	    	fileentry.LastModifiedTimeStamp		    	=	datetimeToDateStr(infinode->fdate,infinode->ftime);
	    	fileentry.LastAccessedTimeStamp		    	=	datetimeToDateStr(infinode->adate,infinode->atime);

	    	fileentry.LastModified10msIncrement 		=	infinode->exfatinode.LastModified10msIncrement;
	    	fileentry.LastModifiedUtcOffset     		=	infinode->exfatinode.LastModifiedUtcOffset;
	    	fileentry.LastAccessedUtcOffset				=	infinode->exfatinode.LastAccessedUtcOffset;
	    	streamextensionentry.ValidDataLength		=	infinode->fsizeu.fsize64;
	    	streamextensionentry.FirstCluster   		=	pc_finode_cluster(infinode->my_drive, infinode);
	    	streamextensionentry.DataLength     		=	infinode->fsizeu.fsize64;
	    	streamextensionentry.GeneralSecondaryFlags  =	infinode->exfatinode.GeneralSecondaryFlags;
		}
		else
		{
	    	pcexfat_getsysdate( &fileentry.CreateTimeStamp, &fileentry.CreateUtcOffset, &fileentry.Create10msIncrement);
	    	fileentry.LastModifiedTimeStamp		= fileentry.CreateTimeStamp;
	    	fileentry.LastAccessedTimeStamp		= fileentry.CreateTimeStamp;
	    	fileentry.LastModified10msIncrement = fileentry.Create10msIncrement;;
	    	fileentry.LastModifiedUtcOffset     = fileentry.CreateUtcOffset;
	    	fileentry.LastAccessedUtcOffset		= fileentry.CreateUtcOffset;
	    	streamextensionentry.ValidDataLength=M64SET32(sizehi, sizelow);
	    	streamextensionentry.FirstCluster   =initcluster;
	    	streamextensionentry.DataLength     =M64SET32(sizehi, sizelow);
	    	/* Calculate Attributes (has cluster chain) */
	    	streamextensionentry.GeneralSecondaryFlags = secondaryflags;
		}
	}
	/* Read and or initialize buffers */


    /* read the sector buffers we need three are possible with 512 byte sectors */
    if (filenameobj.Freesegments.segindex == 0 && filenameobj.segmentsRequired >= pobj->pdrive->drive_info.inopblock)
        rbuf[0] = pc_init_blk(pobj->pdrive, filenameobj.Freesegments.segblock[0]);
    else /* otherwise read it because we are not filling it all in */
    	rbuf[0] = pc_read_blk(pobj->pdrive, filenameobj.Freesegments.segblock[0]);
    if (!rbuf[0])
		goto cleanup_and_exit;
    if (filenameobj.Freesegments.segblock[1])
	{
		if (filenameobj.segmentsRequired - (pobj->pdrive->drive_info.inopblock - filenameobj.Freesegments.segindex) >= pobj->pdrive->drive_info.inopblock)
        	rbuf[1] = pc_init_blk(pobj->pdrive, filenameobj.Freesegments.segblock[1]);
        else /* otherwise read it because we are not filling it all in */
        	rbuf[1] = pc_read_blk(pobj->pdrive, filenameobj.Freesegments.segblock[1]);
		if (!rbuf[1])
			goto cleanup_and_exit;
	}
    if (filenameobj.Freesegments.segblock[2])
	{
		if (filenameobj.segmentsRequired - (pobj->pdrive->drive_info.inopblock - filenameobj.Freesegments.segindex) >= (pobj->pdrive->drive_info.inopblock*2))
        	rbuf[2] = pc_init_blk(pobj->pdrive, filenameobj.Freesegments.segblock[2]);
        else /* otherwise just intialize the buffer because we'll fill it all */
        	rbuf[2] = pc_read_blk(pobj->pdrive, filenameobj.Freesegments.segblock[2]);
		if (!rbuf[2])
			goto cleanup_and_exit;
	}

	if (_attr & AVOLUME)	/* Build file name and stream entries */
	{
		int buffer_offset,segment_offset_in_buffer;
		byte *p;
		buffer_offset = pcexfat_segoffsettosegoffsetandindex(pobj->pdrive, 0, &filenameobj.Freesegments, &segment_offset_in_buffer);

		my_block = rbuf[buffer_offset]->blockno; /* Use this to mark the finode */
		my_index= segment_offset_in_buffer;

		p = (byte *) rbuf[buffer_offset]->data;
		p += (segment_offset_in_buffer * 32);
		rtfs_memset(p, 0, 32);
		if (_attr & AVOLUME)	/* Build file name and stream entries */
		{
		byte i,len = 0;
			*(p +0) = 	EXFAT_DIRENTTYPE_VOLUME_LABEL;
			pUnicodeName = (word *) filenameobj.UnicodeLfn;
			/* Copy 15 characters */
			for (i = 2; i < 22; i += 2)
			{
				if (*pUnicodeName == 0)
					break;
				fr_WORD((p + i), *pUnicodeName++);
				len += 1;
			}
			*(p +1) = 	len;
		}
	}
	else /* if ((_attr & AVOLUME) == 0)	 Populate file entry in the buffer */
	{
		int buffer_offset,segment_offset_in_buffer;
		byte *p;
		buffer_offset = pcexfat_segoffsettosegoffsetandindex(pobj->pdrive, 0, &filenameobj.Freesegments, &segment_offset_in_buffer);

		my_block = rbuf[buffer_offset]->blockno; /* Use this to mark the finode */
		my_index= segment_offset_in_buffer;

		p = (byte *) rbuf[buffer_offset]->data;
		p += (segment_offset_in_buffer * 32);
		rtfs_memset(p, 0, 32);
		*(p +0) = 	fileentry.EntryType;
		*(p +1) =	fileentry.SecondaryCount;
		/* Clear the checksum field, it is exclude from the calculation */
		fileentry.SetChecksum = 0;
		fr_WORD(p + 2, fileentry.SetChecksum);
		fr_WORD((p+ 4), fileentry.FileAttributes);
		/* 	fileentry.Reserved1[2];   */
		fr_DWORD((p + 8 ), pcexfat_datestrtodword(fileentry.CreateTimeStamp));
		fr_DWORD((p + 12), pcexfat_datestrtodword(fileentry.LastModifiedTimeStamp));
		fr_DWORD((p + 16), pcexfat_datestrtodword(fileentry.LastAccessedTimeStamp));
		*(p + 20) =	fileentry.Create10msIncrement;
		*(p + 21) =	fileentry.LastModified10msIncrement;
		*(p + 22) =   fileentry.CreateUtcOffset;
		*(p + 23) =	fileentry.LastModifiedUtcOffset;
		*(p + 24) =	fileentry.LastAccessedUtcOffset;

		/* Now calculate checksum */
		fileentry.SetChecksum = 0;
		fileentry.SetChecksum = pcexfat_checksum_util(fileentry.SetChecksum, TRUE, p);

	}
	if ((_attr & AVOLUME) == 0)	/* Populate file entry (offset 1) in the buffer */
	{
		int buffer_offset,segment_offset_in_buffer;
		byte *p;
		buffer_offset = pcexfat_segoffsettosegoffsetandindex(pobj->pdrive, 1, &filenameobj.Freesegments, &segment_offset_in_buffer);

		p = (byte *) rbuf[buffer_offset]->data;
		p += (segment_offset_in_buffer * 32);
		rtfs_memset(p, 0, 32);


		*(p +0) =streamextensionentry.EntryType;
		*(p +1) =streamextensionentry.GeneralSecondaryFlags;
		*(p +3) =streamextensionentry.NameLen;
		fr_WORD((p + 4), streamextensionentry.NameHash);
		fr_DDWORD((p + 8),  streamextensionentry.ValidDataLength);
		fr_DWORD((p + 20),  streamextensionentry.FirstCluster);
		fr_DDWORD((p + 24), streamextensionentry.DataLength);

		/* Now calculate checksum */
		fileentry.SetChecksum = pcexfat_checksum_util(fileentry.SetChecksum, FALSE, p);
	}
	if ((_attr & AVOLUME) == 0)	/* Populate file entry (offset 1) in the buffer */
	{
		/* Now poulate Name segments */
		pUnicodeName = (word *) filenameobj.UnicodeLfn;
		for(segmentindex = 2;segmentindex < filenameobj.segmentsRequired;segmentindex++)
		{
			int i,buffer_offset,segment_offset_in_buffer;
			byte *p;
			buffer_offset = pcexfat_segoffsettosegoffsetandindex(pobj->pdrive, segmentindex, &filenameobj.Freesegments, &segment_offset_in_buffer);

			p = (byte *) rbuf[buffer_offset]->data;
			p += (segment_offset_in_buffer * 32);
			rtfs_memset(p, 0, 32);

			*(p +0) = 0xC1;
			*(p +1) = 0;
			/* Copy 15 characters */
			for (i = 2; i < 32; i += 2)
			{
				fr_WORD((p + i), *pUnicodeName++);
			}
			/* Now calculate checksum */
			fileentry.SetChecksum = pcexfat_checksum_util(fileentry.SetChecksum, FALSE, p);
		}

		/* Now update the checksum in the first segment */
		{
			int buffer_offset,segment_offset_in_buffer;
			byte *p;
			buffer_offset = pcexfat_segoffsettosegoffsetandindex(pobj->pdrive, 0, &filenameobj.Freesegments, &segment_offset_in_buffer);
			p = (byte *) rbuf[buffer_offset]->data;
			p += (segment_offset_in_buffer * 32);
			/* Clear the checksum field, it is exclude from the calculation */
			fr_WORD(p + 2, fileentry.SetChecksum);
		}
	}
	/* Now write the sectors out */
	{
		int i;
		for (i = 0; i < 3; i++)
		{
    		if (!rbuf[i])
				break;
			if (!pc_write_blk(rbuf[i]))
				goto cleanup_and_exit;
		}
	}
	/* Copy the found segment list into the finode */
	pobj->finode->s = filenameobj.Freesegments;
	if (_attr & AVOLUME)
	{	/* None of this stuff matters for a volume entry, we'll free it imediately and don't need an entry in the finode pool */
		retval = TRUE;
		goto cleanup_and_exit;
	}

	pobj->finode->exfatinode.SecondaryCount 				=   fileentry.SecondaryCount;
	pobj->finode->exfatinode.SetChecksum					=	fileentry.SetChecksum;
	pobj->finode->exfatinode.FileAttributes					=   fileentry.FileAttributes;


	pobj->finode->ctime			= 	fileentry.CreateTimeStamp.time;
	pobj->finode->cdate			= 	fileentry.CreateTimeStamp.date;
	pobj->finode->ftime			= 	fileentry.LastModifiedTimeStamp.time;
	pobj->finode->fdate  		= 	fileentry.LastModifiedTimeStamp.date;
	pobj->finode->atime			= 	fileentry.LastAccessedTimeStamp.time;
	pobj->finode->adate			= 	fileentry.LastAccessedTimeStamp.date;

//	pobj->finode->exfatinode.CreateTimeStamp				= 	pcexfat_datestrtodword(fileentry.CreateTimeStamp);
//	pobj->finode->exfatinode.LastModifiedTimeStamp			= 	pcexfat_datestrtodword(fileentry.LastModifiedTimeStamp);
//	pobj->finode->exfatinode.LastAccessedTimeStamp			= 	pcexfat_datestrtodword(fileentry.LastAccessedTimeStamp);
	pobj->finode->exfatinode.Create10msIncrement			=	fileentry.Create10msIncrement;
	pobj->finode->exfatinode.LastModified10msIncrement		=	fileentry.LastModified10msIncrement;
	pobj->finode->exfatinode.CreateUtcOffset				=	fileentry.CreateUtcOffset;
	pobj->finode->exfatinode.LastModifiedUtcOffset			=	fileentry.LastModifiedUtcOffset;
	pobj->finode->exfatinode.LastAccessedUtcOffset			= 	fileentry.LastAccessedUtcOffset;

	pobj->finode->exfatinode.GeneralSecondaryFlags			=   streamextensionentry.GeneralSecondaryFlags;
	pobj->finode->exfatinode.NameLen						=   streamextensionentry.NameLen;
	pobj->finode->exfatinode.NameHash						=	streamextensionentry.NameHash;
	/* Update the rtfs finode structuire */
	pc_pfinode_cluster(pobj->pdrive, pobj->finode, streamextensionentry.FirstCluster);
	pobj->finode->fattribute = attr;       /* File attributes */

    ERTFS_ASSERT(pobj->finode->pbasic_fragment==0)
    if (pobj->finode->pbasic_fragment)
	{
    	pobj->finode->pbasic_fragment = 0;
	}

//	pobj->finode->ftime       = fileentry.CreateTimeStamp.time;
//	pobj->finode->fdate       = fileentry.CreateTimeStamp.date;
	pobj->finode->fsizeu.fsize64	   = streamextensionentry.ValidDataLength;

	/* Mark it b setting drive block and index info */
	/* Note: The index on the scan points to the end of the finode, not the beginning like in VFAT so we sue the beginning index */
	/* pc_marki(pobj->finode , pobj->pdrive , pobj->blkinfo.my_block, pobj->blkinfo.my_index); */
	pc_marki(pobj->finode , pobj->pdrive , my_block, my_index);
	retval = TRUE;

cleanup_and_exit:
	{
		int i;
		for (i = 0; i < 3; i++)
		{
    		if (!rbuf[i])
				break;
       		pc_release_buf(rbuf[i]);
		}
	}
	pcexfat_filenameobj_destroy(&filenameobj);
	if (retval)
    	rtfs_clear_errno();  /* Clear errno in case we generated interim values */
	return retval;
}


void pcexfat_update_finode_datestamp(FINODE *pfi, BOOLEAN set_archive, int set_date_mask) /*__fn__*/
{
DATESTR TimeStamp;
byte UtcOffset,_10msIncrement;

    OS_CLAIM_FSCRITICAL()
    if (set_archive)
        pfi->fattribute |= ARCHIVE;
	if (set_date_mask)
	{
		pcexfat_getsysdate( &TimeStamp, &UtcOffset, &_10msIncrement);
		if (set_date_mask & DATESETCREATE)
		{
			pfi->ctime									= TimeStamp.time;
			pfi->cdate									= TimeStamp.date;
			pfi->exfatinode.CreateUtcOffset				= UtcOffset;
			pfi->exfatinode.Create10msIncrement			= _10msIncrement;
		}
		if (set_date_mask & DATESETUPDATE)
		{
			pfi->ftime									= TimeStamp.time;
			pfi->fdate									= TimeStamp.date;
			pfi->exfatinode.LastModifiedUtcOffset		= UtcOffset;
			pfi->exfatinode.LastModified10msIncrement	= _10msIncrement;
		}
		if (set_date_mask & DATESETACCESS)
		{
			pfi->atime									= TimeStamp.time;
			pfi->adate									= TimeStamp.date;
			pfi->exfatinode.LastAccessedUtcOffset		= UtcOffset;
		}
	}
    OS_RELEASE_FSCRITICAL()
}


BOOLEAN pcexfat_update_by_finode(FINODE *pfi, int entry_index, BOOLEAN set_archive, int set_date_mask, BOOLEAN set_deleted)
{
BLKBUFF *rbuf[3];
EXFATFILEENTRY fileentry;
BOOLEAN retval = FALSE;


	ERTFS_ASSERT(pfi->s.segindex == entry_index) /*  entry_index is unused because it should be the same as  pfi->s.segindex */

    if ( pfi->s.segindex >= pfi->my_drive->drive_info.inopblock )  /* Index into block */
    {
        rtfs_set_errno(PEINTERNAL, __FILE__, __LINE__); /* pc_update_inode: Internal error, illegal inode index */
        return (FALSE);
    }

    /* Read the sector buffers we need three are possible with 512 byte sectors */
    rbuf[0] = rbuf[1] = rbuf[2] = 0;
    rbuf[0] = pc_read_blk(pfi->my_drive, pfi->s.segblock[0]);
    if (!rbuf[0])
		goto cleanup_and_exit;
    if (pfi->s.segblock[1])
	{
       	rbuf[1] = pc_read_blk(pfi->my_drive, pfi->s.segblock[1]);
		if (!rbuf[1])
			goto cleanup_and_exit;
	}
    if (pfi->s.segblock[2])
	{
       	rbuf[2] = pc_read_blk(pfi->my_drive, pfi->s.segblock[2]);
		if (!rbuf[2])
			goto cleanup_and_exit;
	}
	/* Now update the internal date and archive field if instructed */
	pcexfat_update_finode_datestamp(pfi, set_archive, set_date_mask);
	/* Now copy the internal representation into the fileentry structure */
	{
		int buffer_offset,segment_offset_in_buffer;
		byte *p;

		buffer_offset = pcexfat_segoffsettosegoffsetandindex(pfi->my_drive, 0, &pfi->s, &segment_offset_in_buffer);

		p = (byte *) rbuf[buffer_offset]->data;
		p += (segment_offset_in_buffer * 32);
//		*(p +0) = 	fileentry.EntryType;
//		*(p +1) =	fileentry.SecondaryCount;
//		fr_WORD(p + 2, fileentry.SetChecksum);
		if (set_deleted)
			*p  &= 0x7f;

		/* Set the archive bit and the datestamp according to caller's instructions */
//		pc_update_finode_datestamp(pfi, set_archive, set_date_mask);

		if (pfi->fattribute & ARCHIVE)
		{
			word a = to_WORD(p+ 4);
        	a |= ARCHIVE;
			fr_WORD((p+ 4), a);
		}

		fileentry.CreateTimeStamp		= datetimeToDateStr(pfi->cdate,pfi->ctime);
		fileentry.Create10msIncrement   = pfi->exfatinode.Create10msIncrement;
		fileentry.CreateUtcOffset       = pfi->exfatinode.CreateUtcOffset;

		fileentry.LastModifiedTimeStamp		= datetimeToDateStr(pfi->fdate,pfi->ftime);
		fileentry.LastModified10msIncrement =  pfi->exfatinode.LastModified10msIncrement;
		fileentry.LastModifiedUtcOffset     = pfi->exfatinode.LastModifiedUtcOffset;;

		fileentry.LastAccessedTimeStamp		= datetimeToDateStr(pfi->adate,pfi->atime);
		fileentry.LastAccessedUtcOffset		=  pfi->exfatinode.LastAccessedUtcOffset;


       	fr_DWORD((p + 8 ), pcexfat_datestrtodword(fileentry.CreateTimeStamp));
       	fr_DWORD((p + 12), pcexfat_datestrtodword(fileentry.LastModifiedTimeStamp));
       	fr_DWORD((p + 16), pcexfat_datestrtodword(fileentry.LastAccessedTimeStamp));
       	*(p + 21) =	fileentry.LastModified10msIncrement;
      	*(p + 22) = fileentry.CreateUtcOffset;
       	*(p + 23) =	fileentry.LastModifiedUtcOffset;
       	*(p + 24) =	fileentry.LastAccessedUtcOffset;

		/* Now calculate checksum */
		fileentry.SetChecksum = 0;
		if (!set_deleted)
		{
			fileentry.SetChecksum = pcexfat_checksum_util(fileentry.SetChecksum, TRUE, p);
		}
	}
	/* Populate stream extension entry (offset 1) in the buffer */
	{
		int buffer_offset,segment_offset_in_buffer;
		byte *p;
		buffer_offset = pcexfat_segoffsettosegoffsetandindex(pfi->my_drive, 1, &pfi->s, &segment_offset_in_buffer);

		p = (byte *) rbuf[buffer_offset]->data;
		p += (segment_offset_in_buffer * 32);
		if (set_deleted)
			*p  &= 0x7f;
		else
		{
		    /* Update the HASNOFATCHAIN filed based on the fragment situation */
		    *(p +1) = pfi->exfatinode.GeneralSecondaryFlags;
		    fr_DDWORD((p + 8),  pfi->fsizeu.fsize64);
		    fr_DWORD((p + 20), pc_finode_cluster(pfi->my_drive, pfi));
		    fr_DDWORD((p + 24), pfi->fsizeu.fsize64);
		    /* Now calculate checksum */
		    fileentry.SetChecksum = pcexfat_checksum_util(fileentry.SetChecksum, FALSE, p);
		}
	}
	/* Now checksum fields in the file name segments */
	{
		int segmentindex;
		for(segmentindex = 2;segmentindex <= pfi->exfatinode.SecondaryCount;segmentindex++)
		{
			int buffer_offset,segment_offset_in_buffer;
			byte *p;
			buffer_offset = pcexfat_segoffsettosegoffsetandindex(pfi->my_drive, segmentindex, &pfi->s, &segment_offset_in_buffer);


			p = (byte *) rbuf[buffer_offset]->data;
			p += (segment_offset_in_buffer * 32);
			if (set_deleted)
				*p  &= 0x7f;
			else	/* Now calculate checksum */
				fileentry.SetChecksum = pcexfat_checksum_util(fileentry.SetChecksum, FALSE, p);
		}
	}
	/* Now update the checksum in the first segment */
	if (!set_deleted)
	{
		int buffer_offset,segment_offset_in_buffer;
		byte *p;
		buffer_offset = pcexfat_segoffsettosegoffsetandindex(pfi->my_drive, 0, &pfi->s, &segment_offset_in_buffer);
		p = (byte *) rbuf[buffer_offset]->data;
		p += (segment_offset_in_buffer * 32);
		/* Clear the checksum field, it is exclude from the calculation */
		fr_WORD(p + 2, fileentry.SetChecksum);
	}

	/* Now write the sectors out - Note we are writing all, we could distinguish and not write the fikle name segments */
	{
		int i;
		for (i = 0; i < 3; i++)
		{
    		if (!rbuf[i])
				break;
       		if (!pc_write_blk(rbuf[i]))
				goto cleanup_and_exit;
		}
	}
	retval = TRUE;
cleanup_and_exit:
	{
		int i;
		for (i = 0; i < 3; i++)
		{
    		if (!rbuf[i])
				break;
       		pc_release_buf(rbuf[i]);
		}
	}
	return retval;
}

/* Return all clusters in the fragment list to the pool of free clusters
   no need to zero the regions in the fat for exFat */
BOOLEAN pcexfat_fraglist_fat_free_list(FINODE *pfi, REGION_FRAGMENT *pf)
{
    while (pf)
    {
    dword ncontig;
        ncontig = PC_FRAGMENT_SIZE_CLUSTERS(pf);
        /* Tell the free region manager that these clusters are free */
       	if (!exfatop_add_free_region(pfi->my_drive, pf->start_location, ncontig, TRUE))
       		return(FALSE);
        pf = pf->pnext;
    }
    return(TRUE);
}

/* Delete the entry. We know it is ok to unconditionally delete. */
BOOLEAN pcexfat_rmnode(DROBJ *pobj)
{
BOOLEAN free_cluster_chain = FALSE;
	/* 	Load the fat chain.
		Delete the directory entry
		Free the chain
		Flush the Fat and Bam.
	*/

	/* Load up the fragment chain */
	if (M64NOTZERO(pobj->finode->fsizeu.fsize64) && pc_finode_cluster(pobj->pdrive, pobj->finode))
	{
		if (pcexfat_bfilio_load_all_fragments(pobj->finode))
			free_cluster_chain = TRUE;
		else
		{
        	if (get_errno() != PEINVALIDCLUSTER)
				return(FALSE);
			rtfs_clear_errno();
			free_cluster_chain = FALSE;
		}
	}

   	pcexfat_set_volume_dirty(pobj->pdrive); /* This is stubbed out but in the correct place */
	/* Now update the inode, clearing the high bit in the first byte of each entery */
   	if (!pcexfat_update_by_finode(pobj->finode, pobj->finode->my_index, FALSE, 0, TRUE))
		return(FALSE);

	/* Now free the fragment chain */
	if (free_cluster_chain)
	{
		if (!pcexfat_fraglist_fat_free_list(pobj->finode, pobj->finode->pbasic_fragment))
		{
			return(FALSE);
		}
	}
	/* Now zero size and fcluster (defensive: will not be reused again anyway) */
	pobj->finode->fsizeu.fsize64 	= M64SET32(0,0);
	pc_pfinode_cluster(pobj->pdrive, pobj->finode,0);
	/* Now free the fragment list it should not be reused again anyway */
	if (pobj->finode->pbasic_fragment)
        pc_fraglist_free_list(pobj->finode->pbasic_fragment);
    pobj->finode->pbasic_fragment = 0;
	/* Now flush the FAT and BAM. */
	{
		BOOLEAN ret_val;
		ret_val = fatop_flushfat(pobj->pdrive);
		if (ret_val)
		{
    		pcexfat_clear_volume_dirty(pobj->pdrive);
		}
		return(ret_val);
	}
}

/* Rename old_obj to new name. We know it is ok to unconditionally rename. */
BOOLEAN pcexfat_mvnode(DROBJ *old_parent_obj,DROBJ *old_obj,DROBJ *new_parent_obj, byte *filename,int use_charset)
{
DROBJ *pNewObj;
BOOLEAN ret_val = FALSE;
	RTFS_ARGSUSED_PVOID((void *) old_parent_obj);
	/* Creat a new directory entry with the old entry's attributes, creation date file size start cluster etc. */
	pNewObj = pc_mknode(new_parent_obj, filename, 0, old_obj->finode->fattribute, old_obj->finode, use_charset);
	if (pNewObj)
	{
		/* Mark the old directory entry deleted */
	 	ret_val = pcexfat_update_by_finode(old_obj->finode, old_obj->finode->s.segindex, FALSE, 0, TRUE);
       	pc_freeobj(pNewObj);
	}
	return(ret_val);
}
BOOLEAN pcexfat_set_volume(DDRIVE *pdrive, byte *volume_label,int use_charset)
{
BOOLEAN ret_val = FALSE;
byte  volume_label_entry[32];

   if ((rtfs_cs_strlen(volume_label, use_charset) > 11))
   {
       	rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
   		return(FALSE);
   }

   rtfs_memset(volume_label_entry,0,32);
   if (use_charset != CS_CHARSET_UNICODE)
   		map_jis_ascii_to_unicode((byte *)&volume_label_entry[2], volume_label);
   else
       	rtfs_cs_strcpy((byte *)&volume_label_entry[2], volume_label, CS_CHARSET_UNICODE);

   if (!pc_cs_validate_filename((byte *)&volume_label_entry[2], 0, use_charset))
   {
   		rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
   		return(FALSE);
	}

	if (!PDRTOEXFSTR(pdrive)->volume_label_sector)
	{
	DROBJ *root_obj;
	DROBJ *pobj;
		/* Get the root, if an error, errno is set below */
		root_obj = pc_get_root(pdrive);
		if (!root_obj)
   			return(FALSE);
		/* Create the entry, mknode will validate that the name is legal  */
        pobj = pc_mknode( root_obj,volume_label, 0, AVOLUME, 0, use_charset);
		if (!pobj)
   			return(FALSE);
		PDRTOEXFSTR(pdrive)->volume_label_sector =	pobj->blkinfo.my_block;
		PDRTOEXFSTR(pdrive)->volume_label_index  =  pobj->blkinfo.my_index;
        pc_freeobj(pobj);
   		/* Fall through and update */
	}

	if (PDRTOEXFSTR(pdrive)->volume_label_sector)
	{
    byte  volume_label_entry[32];
    byte  *p;
	BLKBUFF *rbuf;

		rtfs_memset(volume_label_entry,0,32);
    	if (use_charset != CS_CHARSET_UNICODE)
    		map_jis_ascii_to_unicode((byte *)&volume_label_entry[2], volume_label);
        else
         	rtfs_cs_strcpy((byte *)&volume_label_entry[2], volume_label, CS_CHARSET_UNICODE);

        if (!pc_cs_validate_filename((byte *)&volume_label_entry[2], 0, use_charset))
		{
        	rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
        	return(FALSE);
		}

        volume_label_entry[1] = (byte) rtfs_cs_strlen(&volume_label_entry[2], CS_CHARSET_UNICODE);
        volume_label_entry[0] = EXFAT_DIRENTTYPE_VOLUME_LABEL;

       	rbuf = pc_read_blk(pdrive, PDRTOEXFSTR(pdrive)->volume_label_sector);
		if (rbuf)
		{
			p = rbuf->data;
			p += (PDRTOEXFSTR(pdrive)->volume_label_index * 32);
			copybuff(p, volume_label_entry, 32);
			ret_val = pc_write_blk(rbuf);
			if (ret_val)
			{
				copybuff(&pdrive->drive_info.volume_label[0], &volume_label_entry[2], 22);
				pdrive->drive_info.volume_label[22] = 0;
				pdrive->drive_info.volume_label[23] = 0;
			}
			pc_release_buf(rbuf);
		}
	}
   	if (ret_val)
	{

        rtfs_clear_errno();
	}
    return(ret_val);
}



BOOLEAN pcexfat_get_volume(DDRIVE *pdrive, byte *volume_label,int use_charset)
{
int n;
word *pw;
   	pw = (word *) &pdrive->drive_info.volume_label[0];
   	if (*pw == 0)
	{
		rtfs_set_errno(PENOENT, __FILE__, __LINE__);
		return(FALSE);
	}
	else
	{
    	byte *pb = (byte *) &pdrive->drive_info.volume_label[0];
    	for(n=0; n<11; n += 1,pb+=2)
    	{
    		if((*pb==0x00)&&(*(pb+1)==0x00))
    		{
    			CS_OP_TERM_STRING(volume_label, use_charset);
    			break;
    		}
    		CS_OP_LFI_TO_TXT(volume_label, pb, use_charset);
    		CS_OP_INC_PTR(volume_label, use_charset);
    	}
    	CS_OP_TERM_STRING(volume_label, use_charset);
		return(TRUE);
	}
}
