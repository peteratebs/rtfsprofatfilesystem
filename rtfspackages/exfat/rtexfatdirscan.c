/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* RTEXFATDIRSCAN.C - Directory level read functions for exFat.

    Routines Exported from this file include:

    pcexfat_checksum_util			- Used by exfat modules to calculate checksums.
	pcexfat_filenameobj_init		- Used by exfat modules to parse filenames and store Unicode and Upcased names.
    pcexfat_filenameobj_destroy 	- Used by exfat modules to release stored Unicode and Upcased names.
	pcexfat_findin					- Called by pc_findin if media is exFat formated.
	pcexfat_findinbyfilenameobj     - Used by exfat modules to match on file names and to find free directoiry slots.
	pcexfat_seglist2text			- Called by pc_get_lfn_filename (upstat)
*/

#include "rtfs.h"


/***************************************************************************
    PCEXFAT_FINDIN -  Find a filename in the same directory as the argument.

 Description
    Look for the next match of filename or pattern filename:ext in the
    subdirectory containing pobj. If found update pobj to contain the
    new information  (essentially getnext.) Called by pc_get_inode().

    Note: Filename and ext must be right filled with spaces to 8 and 3 bytes
            respectively. Null termination does not matter.

    Note the use of the action variable. This is used so we do not have to
    use match patterns in the core code like *.* and ..
    GET_INODE_MATCH   Must match the pattern exactly
    GET_INODE_WILD    Pattern may contain wild cards
    GET_INODE_STAR    Like he passed *.* (pattern will be null)
    GET_INODE_DOTDOT  Like he past .. (pattern will be null

 Returns
    Returns TRUE if found or FALSE.

****************************************************************************/
/* Find filename in the directory containing pobj. If found, load the inode
section of pobj. If the inode is already in the inode buffers we free the current inode
and stitch the existing one in, bumping its open count */


static word pcexfat_calculate_hash(word *filename);
static word pc_exfatNameHash(word * FileName, byte NameLength);
static byte *exfatnameseg2text(byte *lfn, int *current_lfn_length, byte *pfi, int use_charset);

/* 	Routines to encapsulate search terms in one objet
byte 			*pInputFile;              -  input file plus storage objects and
BLKBUFF 		*pUpCasedFileNameBuffer;  -  pointers to upcased and unicode copies of the file name
BLKBUFF 		*pUnicodeFileNameBuffer;
word 			*upCasedLfn;
word	 		*UnicodeLfn;
int				use_charset;       - Input character set
int   			NameSegments;	   - Number of segments needed for the name
byte  			NameLen;           - Length bytes
word 			NameHash;		   - Hash value for name
int 			segmentsRequired;  - If an insert, find this many free segments while searching for the name
SEGDESC			Freesegments;	   - Found free segments returned here
*/

void pcexfat_filenameobj_destroy(EXFATFILEPARSEOBJ *pfilenameobj)
{
	if (pfilenameobj->pUpCasedFileNameBuffer)
        pc_free_scratch_blk(pfilenameobj->pUpCasedFileNameBuffer);
	if (pfilenameobj->pUnicodeFileNameBuffer)
        pc_free_scratch_blk(pfilenameobj->pUnicodeFileNameBuffer);
	rtfs_memset(pfilenameobj,0, sizeof(*pfilenameobj));
}
BOOLEAN pcexfat_filenameobj_init(DROBJ *pobj, byte *filename, EXFATFILEPARSEOBJ *pfilenameobj, int use_charset)          /*__fn__*/
{
    BOOLEAN retval = FALSE;
	rtfs_memset(pfilenameobj,0, sizeof(*pfilenameobj));
   	pfilenameobj->pUpCasedFileNameBuffer =  pc_scratch_blk();
	if (!pfilenameobj->pUpCasedFileNameBuffer)
       	goto cleanup_and_return;
   	pfilenameobj->pUnicodeFileNameBuffer =  pc_scratch_blk();
	if (!pfilenameobj->pUnicodeFileNameBuffer)
       	goto cleanup_and_return;
	pfilenameobj->use_charset = use_charset;
	pfilenameobj->pInputFile  = filename;

   	pfilenameobj->upCasedLfn = (word *)pfilenameobj->pUpCasedFileNameBuffer->data;
   	pfilenameobj->UnicodeLfn = (word *)pfilenameobj->pUnicodeFileNameBuffer->data;
	rtfs_memset((byte *) pfilenameobj->upCasedLfn, 0, 512);	/* Zero these 512 byte name buffers. Allocation could be longer */
	rtfs_memset((byte *) pfilenameobj->UnicodeLfn, 0, 512);	/* But we only care about 512 */
	pfilenameobj->NameLen = (byte)rtfs_cs_strlen(filename, use_charset);
	pfilenameobj->NameSegments = (int) pfilenameobj->NameLen;
	pfilenameobj->NameSegments = (pfilenameobj->NameSegments + EXFATCHARSPERFILENAMEDIRENT-1)/EXFATCHARSPERFILENAMEDIRENT;
    if (use_charset != CS_CHARSET_UNICODE)
    	map_jis_ascii_to_unicode((byte *)pfilenameobj->UnicodeLfn, filename);
	else
         rtfs_cs_strcpy((byte *)pfilenameobj->UnicodeLfn, filename, CS_CHARSET_UNICODE);
	pcexfat_upcase_unicode_string(pobj->pdrive,pfilenameobj->upCasedLfn, pfilenameobj->UnicodeLfn,EMAXPATH_CHARS);
	pfilenameobj->NameHash = pcexfat_calculate_hash(pfilenameobj->upCasedLfn);
	retval = TRUE;
cleanup_and_return:
	if (!retval)
		pcexfat_filenameobj_destroy(pfilenameobj);
	return(retval);
}
/* Zero a free segment list */
static void pc_exfatzeroseglist(SEGDESC *s)  /* __fn__ */
{
    s->nsegs = 0;
    s->segblock[0] =
    s->segblock[1] =
    s->segblock[2] =
    s->segindex = 0;
}
/* Add the block number and segment index (there are 16 segments in a 512 byte sector) to the list of contuous Zero a free segment list */
static void pc_exfatpushseglist(SEGDESC *s, dword my_block, int my_index) /*__fn__*/
{
    if (!s->nsegs)				/* They are contguous so we only need to save the first */
        s->segindex = my_index;
    s->nsegs += 1;

    /* The block list is a FIFO insert the block into the first empty entry if it is not already stored */
    if (!s->segblock[0])
        s->segblock[0] = my_block;
    else if ( s->segblock[0] == my_block)
    	;
    else if (!s->segblock[1])
        s->segblock[1] = my_block;
    else if ( s->segblock[1] == my_block)
    	;
    else if (!s->segblock[2])
        s->segblock[2] = my_block;

}

BOOLEAN pcexfat_findin( DROBJ *pobj, byte *filename, int action, BOOLEAN oneshot, int use_charset)          /*__fn__*/
{
BOOLEAN retval;
EXFATFILEPARSEOBJ filenameobj;
	/* Get unicode filename, upcased file name, checksum, hashcode */
	if (action == GET_INODE_STAR)	/* If a null value was passed just process a space so we dont have problems. */
		filename = (byte *) " ";
	if (!pcexfat_filenameobj_init(pobj, filename, &filenameobj, use_charset))
        return(FALSE);

	retval = pcexfat_findinbyfilenameobj(pobj, &filenameobj, oneshot, action);
	pcexfat_filenameobj_destroy(&filenameobj);
	return retval;

}

BOOLEAN pcexfat_findinbyfilenameobj(DROBJ *pobj, EXFATFILEPARSEOBJ *pfilenameobj, BOOLEAN oneshot, int action)          /*__fn__*/
{
    dword user_buffer_first_block,user_buffer_n_blocks,blocks_in_buffer,entries_processed,hash_value,ubuff_size;
    byte *pi,*pscan_data,*puser_buffer;
    word *scannedLfn;
    BOOLEAN still_searching, ret_val;
    BOOLEAN processing_file_group = FALSE;
    BOOLEAN end_of_directory_was_reached = FALSE;
    BLKBUFF *scratch2,*scan_buff, **pscan_buff; /* If using block buffers for scan */
    DIRBLK *pd;
	EXFATDIRSCAN current_primary_entry;
	word Checksum = 0;

	rtfs_memset(&current_primary_entry.control, 0, sizeof(current_primary_entry.control));
	ret_val = FALSE;


    entries_processed = 0;
    scan_buff = 0;
    pscan_buff = &scan_buff;        /* Assume using buffer pool */

    /* Use unbuffered mode if the userbuffer holds a whole cluster and when looking for a specific entry.
       This bypasses the block buffer pool and makes single entry searches faster. Otherwise the data is rippled
       through the buffer pool and will be there for future calls  */
    puser_buffer = pc_claim_user_buffer(pobj->pdrive, &ubuff_size,(dword) pobj->pdrive->drive_info.secpalloc);  /* released on cleanup */
    if (puser_buffer && action == GET_INODE_MATCH) /* Unbuffered mode specidied. bypass block buffer pool */
    {
        pscan_buff = 0;             /* override default and use user buffer */
    }

    rtfs_clear_errno();  /* Clear it here just in case */

   	scratch2 = pc_scratch_blk();
   	if (!scratch2)
       	goto cleanup_and_return;

   	scannedLfn = (word *)scratch2->data;

	/* Use hashing and prepare for comparisons */
   	if (action == GET_INODE_MATCH || action == GET_INODE_WILD)
		hash_value = pfilenameobj->NameHash;
	else /* if (action == GET_INODE_STAR) */
        hash_value = 0;

    /* For convenience. We want to get at block info here   */
    pd = &pobj->blkinfo;

    /* Read the data   */
    still_searching = TRUE;
    if (!pcexfat_multi_dir_get(pobj, pscan_buff, &pscan_data, puser_buffer, &blocks_in_buffer, FALSE))
        goto cleanup_and_return;
    user_buffer_first_block = pd->my_block;
    user_buffer_n_blocks = blocks_in_buffer;
    if (!blocks_in_buffer)
    {
        still_searching = FALSE;
        pi = 0;
    }
    else
    {
        pi = (byte *) pc_data_in_ubuff(pobj->pdrive, user_buffer_first_block, pscan_data, user_buffer_first_block,user_buffer_n_blocks);
    }
    while (still_searching)
    {
        /* Look at the current inode   */
        pi += (pd->my_index*EXFATDIRENTSIZE);
		/* And look for a match   */
        while (pd->my_index < pobj->pdrive->drive_info.inopblock )
        {
			byte EntryType;
            /* If we are at the end, act like it's an EOF character, this way we continue to accumulate free segments
			   this only happens when we are looking for free sectors */
			if (end_of_directory_was_reached)
				EntryType = EXFAT_DIRENTTYPE_EOF;
			else
				EntryType = *pi;

			/* Clear state if we just completed the scan of a file set */
			if (processing_file_group && current_primary_entry.control.secondary_entries_found == current_primary_entry.control.secondary_entries_expected)
				processing_file_group = FALSE;

#ifdef EXFAT_EXTEND_DIRECTORIES_FROM_END_ONLY
/* This is not the default */
/* Extend only from end like microsoft does.. need to analyze */
			if (end_of_directory_was_reached && (EntryType & 0x80) == 0)   /* If we are allocating and it's a free segment see if we can use it */
#else
			if ((EntryType & 0x80) == 0)   /* If we are allocating and it's a free segment see if we can use it */
#endif
			{
				if (pfilenameobj->segmentsRequired && pfilenameobj->segmentsRequired > pfilenameobj->Freesegments.nsegs)
				{
					pc_exfatpushseglist(&pfilenameobj->Freesegments, pd->my_block, pd->my_index);
				}
			}
			else
			{
				if (pfilenameobj->segmentsRequired && pfilenameobj->segmentsRequired > pfilenameobj->Freesegments.nsegs)
					pc_exfatzeroseglist(&pfilenameobj->Freesegments);
			}

			switch (EntryType)  {
				default:
					processing_file_group = FALSE;
					break;
				case EXFAT_DIRENTTYPE_ALLOCATION_BITMAP :
				case EXFAT_DIRENTTYPE_UPCASE_TABLE 		:
				case EXFAT_DIRENTTYPE_VOLUME_LABEL 		:
				case EXFAT_DIRENTTYPE_VENDOR_EXTENSION	:
				case EXFAT_DIRENTTYPE_VENDOR_ALLOCATION :
				case EXFAT_DIRENTTYPE_GUID 				:
					processing_file_group = FALSE;
					break;
				case EXFAT_DIRENTTYPE_FILE 				:
					rtfs_memset(&current_primary_entry.control, 0, sizeof(current_primary_entry.control));
					copybuff(&current_primary_entry.rawfileentry[0], pi, 32);
					current_primary_entry.control.expected_entry_type = EXFAT_DIRENTTYPE_STREAM_EXTENSION;
					current_primary_entry.control.secondary_entries_expected = current_primary_entry.rawfileentry[1]; /* Offset 1 */
					current_primary_entry.control.secondary_entries_found = 0;
					current_primary_entry.control.spans_sectors[0] = pd->my_block;
					current_primary_entry.control.first_index = pd->my_index;
					/* Should be at least 2 entries and at most 18 - otherwise lets assume it's junk */
					if (current_primary_entry.control.secondary_entries_expected > 1 && current_primary_entry.control.secondary_entries_expected < 19)
						processing_file_group = TRUE;
					break;
				case EXFAT_DIRENTTYPE_STREAM_EXTENSION	:
				{
					if (processing_file_group && current_primary_entry.control.expected_entry_type == EXFAT_DIRENTTYPE_STREAM_EXTENSION)
					{
						if (action == GET_INODE_MATCH)
						{
							if (hash_value != to_WORD(pi+4)) 							/* NameHash Field */
							{
								current_primary_entry.control.expected_entry_type = 0;	/* Skip the rest */
							}
						}
					}
					if (current_primary_entry.control.expected_entry_type == EXFAT_DIRENTTYPE_STREAM_EXTENSION)
					{
						current_primary_entry.control.NameLen = *(pi+3);
						current_primary_entry.control.NameLenProcessed = 0;
						copybuff(&current_primary_entry.rawstreamextensionentry[0], pi, 32);
						current_primary_entry.control.expected_entry_type = EXFAT_DIRENTTYPE_FILE_NAME_ENTRY;
					}
					if (processing_file_group)
					{	/* Keep track of sectors making up the directory */
						if (current_primary_entry.control.spans_sectors[0] != pd->my_block)
						{
							if (!current_primary_entry.control.spans_sectors[1])
								current_primary_entry.control.spans_sectors[1] = pd->my_block;
							else if (current_primary_entry.control.spans_sectors[1] != pd->my_block)
								current_primary_entry.control.spans_sectors[2] = pd->my_block;
						}
						current_primary_entry.control.secondary_entries_found += 1;

						/* If we are in a file group and we are taking all entries or we passed the hash test.. we probably match so checksum */

						Checksum = 0;
						Checksum = pcexfat_checksum_util(Checksum, TRUE, &current_primary_entry.rawfileentry[0]);
						Checksum = pcexfat_checksum_util(Checksum, FALSE, &current_primary_entry.rawstreamextensionentry[0]);
					}
				}
				break;
				case EXFAT_DIRENTTYPE_FILE_NAME_ENTRY	:
				{
					if (processing_file_group && current_primary_entry.control.expected_entry_type == EXFAT_DIRENTTYPE_FILE_NAME_ENTRY)
					{
						if (current_primary_entry.control.secondary_entries_found >= current_primary_entry.control.secondary_entries_expected)
							processing_file_group = FALSE;	/* We are lost */
						else
						{
							int rawfileoffset = 30*(current_primary_entry.control.secondary_entries_found-1);

							/* Add the checksum for the name segment */
							Checksum = pcexfat_checksum_util(Checksum, FALSE, pi);
							{
#if (KS_LITTLE_ENDIAN)
								copybuff(&current_primary_entry.rawfilenamedata[rawfileoffset], pi+2, 30);
#else
								int i;
								byte *from;
								word *to;
								to = (word *) &current_primary_entry.rawfilenamedata[rawfileoffset];
								from = pi+2;
								for (i = 0; i < 15; i++)
								{
									*to++ = to_WORD(from);
									from += 2;
								}
#endif
							}

							current_primary_entry.control.secondary_entries_found += 1;
							/* Keep track of sectors making up the directory */
							if (current_primary_entry.control.spans_sectors[0] != pd->my_block)
							{
								if (!current_primary_entry.control.spans_sectors[1])
									current_primary_entry.control.spans_sectors[1] = pd->my_block;
								else if (current_primary_entry.control.spans_sectors[1] != pd->my_block)
									current_primary_entry.control.spans_sectors[2] = pd->my_block;
							}
							if (current_primary_entry.control.secondary_entries_found == current_primary_entry.control.secondary_entries_expected)
							{
							BOOLEAN matched;

								matched = FALSE;
								if (action == GET_INODE_STAR)
									matched = TRUE;
								else
								{
								BOOLEAN dowild;
									if (action == GET_INODE_MATCH)
										dowild = FALSE;
									else
										dowild = TRUE;
									{
									int	ntoprocess;
										ntoprocess = (int) current_primary_entry.control.NameLen;
										ntoprocess = ntoprocess - current_primary_entry.control.NameLenProcessed;
										if (ntoprocess > 255)
											ntoprocess = 255;
										if (ntoprocess)
											pcexfat_upcase_unicode_string(pobj->pdrive,scannedLfn, (word *) &current_primary_entry.rawfilenamedata[0],ntoprocess);
										current_primary_entry.control.NameLenProcessed = ntoprocess;
									}
									matched = pc_patcmp_vfat((byte *)pfilenameobj->upCasedLfn, (byte *)scannedLfn, dowild, CS_CHARSET_UNICODE);
								}
								/* Now verify the checksum */
								if (matched)
								{
									if (Checksum != to_WORD(&current_primary_entry.rawfileentry[2]))
									{
										matched = FALSE;
#if (DEBUG_EXFAT_VERBOSE)
   										rtfs_print_one_string((byte *)"Stored checksum does not match", PRFLG_NL);
#endif
							    	}
							    }
								if (matched)
								{
									FINODE *pfi;
									/* We found it   */
									/* See if it already exists in the inode list, if so.. we use the copy from the inode list */
									pfi = pc_scani(pobj->pdrive, current_primary_entry.control.spans_sectors[0], current_primary_entry.control.first_index);
									if (pfi)
									{
										pc_freei(pobj->finode);
										pobj->finode = pfi;
								    	ret_val = TRUE;									/* Succes */
								    	goto cleanup_and_return;
									}
									else    /* No inode in the inode list. Copy the data over
												and mark where it came from */
									{
									    pfi = pc_alloci();
									    if (pfi)
									    {
										dword FirstCluster,	CreateTimeStamp,LastModifiedTimeStamp,LastAccessedTimeStamp;

											if (pobj->finode)
											{
									    		*pfi = *pobj->finode;
									    		pc_freei(pobj->finode); /* Release the current */
											}
									    	pobj->finode = pfi;
									    	pc_marki(pobj->finode , pobj->pdrive , current_primary_entry.control.spans_sectors[0], current_primary_entry.control.first_index);

									    	/* Initialize finode structure from exFat */
									    	/* Store segments in the set including file */
                                            pfi->s.nsegs 		= current_primary_entry.control.secondary_entries_found+1;
									    	/* Starting index of file and sectors spanned by the set. Sector is 0 if not used */
                                            pfi->s.segindex 	= current_primary_entry.control.first_index;
                                            pfi->s.segblock[0] 	= current_primary_entry.control.spans_sectors[0];
                                            pfi->s.segblock[1] 	= current_primary_entry.control.spans_sectors[1];
                                            pfi->s.segblock[2] 	= current_primary_entry.control.spans_sectors[2];
									    	/* Store exfat info from the file set into exfat finode structure */
									    	pfi->exfatinode.SecondaryCount 				=   current_primary_entry.rawfileentry[1];
									    	pfi->exfatinode.SetChecksum					=	to_WORD(&current_primary_entry.rawfileentry[2]);
									    	pfi->exfatinode.FileAttributes				=   to_WORD(&current_primary_entry.rawfileentry[4]);
									    	//pfi->exfatinode.CreateTimeStamp				= 	to_DWORD(&current_primary_entry.rawfileentry[8]);
									    	//pfi->exfatinode.LastModifiedTimeStamp		= 	to_DWORD(&current_primary_entry.rawfileentry[12]);
									    	//pfi->exfatinode.LastAccessedTimeStamp		= 	to_DWORD(&current_primary_entry.rawfileentry[16]);
									    	CreateTimeStamp				= 	to_DWORD(&current_primary_entry.rawfileentry[8]);
									    	LastModifiedTimeStamp		= 	to_DWORD(&current_primary_entry.rawfileentry[12]);
									    	LastAccessedTimeStamp		= 	to_DWORD(&current_primary_entry.rawfileentry[16]);
									    	pfi->exfatinode.Create10msIncrement			=	current_primary_entry.rawfileentry[20];
									    	pfi->exfatinode.LastModified10msIncrement	=	current_primary_entry.rawfileentry[21];
									    	pfi->exfatinode.CreateUtcOffset				=	current_primary_entry.rawfileentry[22];
									    	pfi->exfatinode.LastModifiedUtcOffset		=	current_primary_entry.rawfileentry[23];
									    	pfi->exfatinode.LastAccessedUtcOffset		= 	current_primary_entry.rawfileentry[24];

									    	pfi->exfatinode.GeneralSecondaryFlags		=   current_primary_entry.rawstreamextensionentry[1];
									    	pfi->exfatinode.NameLen						=   current_primary_entry.rawstreamextensionentry[3];
									    	pfi->exfatinode.NameHash					=	to_WORD(&current_primary_entry.rawstreamextensionentry[4]);
									    	to_DDWORD(&pfi->fsizeu.fsize64, &current_primary_entry.rawstreamextensionentry[8]);
									    	/* to_DDWORD(&pfi->exfatinode.ValidDataLength, &current_primary_entry.rawstreamextensionentry[8]); */
									    	/* to_DDWORD(&pfi->exfatinode.DataLength, &current_primary_entry.rawstreamextensionentry[24]); */
									    	ret_val = TRUE;									/* Succes */
									    	/* Set some valid values here for the basic finode so basic dir listings etc will work   */
									    	rtfs_memset(pfi->fname, ' ', 8);
									    	rtfs_memset(pfi->fext,  ' ', 3);
             						    	pfi->fattribute		= (byte)(pfi->exfatinode.FileAttributes & 0xff);
             						    	pfi->ftime 			= (word) (LastModifiedTimeStamp & 0xffff);
             						    	pfi->fdate 			= (word) ((LastModifiedTimeStamp>>16) & 0xffff);
             						    	pfi->ctime 			= (word) (CreateTimeStamp & 0xffff);
             						    	pfi->cdate 			= (word) ((CreateTimeStamp>>16) & 0xffff);
             						    	pfi->atime 			= (word) (LastAccessedTimeStamp & 0xffff);
             						    	pfi->adate 			= (word)((LastAccessedTimeStamp>>16) & 0xffff);
									    	FirstCluster				= 	to_DWORD(&current_primary_entry.rawstreamextensionentry[20]) ;
             						    	pc_pfinode_cluster(pfi->my_drive, pfi, FirstCluster);
									    	goto cleanup_and_return;
									    }
									    else
									    	goto cleanup_and_return;
									}
								}
								processing_file_group = FALSE;
							}

						}
					}
					else if (processing_file_group)
						current_primary_entry.control.secondary_entries_found += 1;
				}
				break;
				case EXFAT_DIRENTTYPE_EOF:
				{
					if (pfilenameobj->segmentsRequired && pfilenameobj->segmentsRequired > pfilenameobj->Freesegments.nsegs)
					{ /* All entries after EOF fall through here, so log only the first */
						if (!end_of_directory_was_reached )
						{
#ifdef EXFAT_EXTEND_DIRECTORIES_FROM_END_ONLY
							pc_exfatpushseglist(&pfilenameobj->Freesegments, pd->my_block, pd->my_index);
#endif
							end_of_directory_was_reached = TRUE;
						}
					}
					else
					{
                		rtfs_set_errno(PENOENT, __FILE__, __LINE__);
                		goto cleanup_and_return;
					}
				}
				break;
			}
			if (oneshot && !processing_file_group)
				break;
            pd->my_index++;
            pi += EXFATDIRENTSIZE;
        }
        entries_processed += pobj->pdrive->drive_info.inopblock;
        if (entries_processed >= RTFS_CFG_MAX_DIRENTS)
        {
            rtfs_set_errno(PEINVALIDDIR, __FILE__, __LINE__);
            goto cleanup_and_return;
        }
		if (oneshot && !processing_file_group)
        {
            rtfs_set_errno(PENOENT, __FILE__, __LINE__);
            goto cleanup_and_return;
        }

        pd->my_index = 0;
        blocks_in_buffer -= 1;
        if(blocks_in_buffer)
            pd->my_block += 1;
        else
        {
            if (!pcexfat_multi_dir_get(pobj, pscan_buff, &pscan_data, puser_buffer, &blocks_in_buffer, TRUE))
                goto cleanup_and_return;
            if (!blocks_in_buffer)
                still_searching = FALSE;
            else
            {
                user_buffer_first_block = pobj->blkinfo.my_block;
                user_buffer_n_blocks = blocks_in_buffer;
                pi = (byte *) pc_data_in_ubuff(pobj->pdrive, user_buffer_first_block, pscan_data, user_buffer_first_block,user_buffer_n_blocks);
            }
        }
    }
    if (!get_errno())
        rtfs_set_errno(PENOENT, __FILE__, __LINE__);
cleanup_and_return:
    if (puser_buffer)
        pc_release_user_buffer(pobj->pdrive, puser_buffer);
    if (scan_buff)
        pc_release_buf(scan_buff);
    if (scratch2)
        pc_free_scratch_blk(scratch2);
	if (ret_val)
    	rtfs_clear_errno();  /* We succeeded so be sure errno is clear */
    return (ret_val);
}

BOOLEAN pcexfat_multi_dir_get(DROBJ *pobj, BLKBUFF **pscan_buff, byte **pscan_data, byte *puser_buffer, dword *n_blocks, BOOLEAN do_increment)
{
    if (pobj->blkinfo.my_block < pobj->pdrive->drive_info.firstclblock)
    {	/* In exFAT all directories are in cluster space */
    	rtfs_set_errno(PEINTERNAL, __FILE__, __LINE__);
        return (FALSE);
	}
	else
		return(pc_multi_dir_get(pobj, pscan_buff, pscan_data, puser_buffer, n_blocks, do_increment));
}

/* Calculate hash value of an upcased file name */
static word pcexfat_calculate_hash(word *filename)
{
byte NameLength;
	NameLength = (byte) rtfs_cs_strlen((byte *)filename, CS_CHARSET_UNICODE);
	return(pc_exfatNameHash(filename, NameLength));
}

static word pc_exfatNameHash(word * FileName, byte NameLength)
{
byte * Buffer = (byte *)FileName;
byte b;
word Hash = 0;
int Index;
int NumberOfBytes;

 	NumberOfBytes = (int)NameLength;
 	NumberOfBytes *= 2;
	for (Index = 0; Index < NumberOfBytes; Index++)
	{
		b = *Buffer++;
		Hash = ((Hash&1) ? 0x8000 : 0) + (Hash>>1) + (word)b;
	}
	return Hash;
}

static byte *exfatnameseg2text(byte *lfn, int *current_lfn_length, byte *pfi, int use_charset);

byte *pcexfat_seglist2text(DDRIVE * pdrive, SEGDESC *s, byte *lfn, int use_charset) /* __fn__ */
{
BLKBUFF *rbuff;
byte *p,*pfrom;
int current_lfn_length;
int namesegindex, sectorindex, namesegments, segmentnumber;

	current_lfn_length = 0;
    CS_OP_TERM_STRING(lfn, use_charset);
	sectorindex  = 0;
    if (s->nsegs > 2)
		namesegments = s->nsegs - 2;
	else
        return(0);	/* Should not happen */
	namesegindex = s->segindex+2;	 /* Skip file entry and stream entry to get to the file names */
	p = lfn;

	rbuff = 0;
	for (segmentnumber = 0; segmentnumber < namesegments; segmentnumber++)
	{
		if (namesegindex >= pdrive->drive_info.inopblock)
		{
			namesegindex -= pdrive->drive_info.inopblock;
			sectorindex  += 1;
			if (sectorindex > 2)  	/* Maximum sectors. Can't happen */
				goto return_error;
		}
		if (!rbuff)
		{
		 	if (!s->segblock[sectorindex])
				goto return_error;
    		rbuff = pc_read_blk(pdrive, s->segblock[sectorindex]);
    		if (!rbuff)
				goto return_error;
		}
		pfrom = (byte *) rbuff->data;
		pfrom += (namesegindex * 32)+2;	/* Skip preceeding segments and two bytes preceeding name information */

		p = exfatnameseg2text(p, &current_lfn_length, pfrom, use_charset);
		if (!p)	/* Something wrong, too long */
		{
        	pc_release_buf(rbuff);
			goto return_error;
		}
		pfrom += 32;
		namesegindex += 1;
		if (namesegindex >= pdrive->drive_info.inopblock)
		{
        	pc_release_buf(rbuff);
        	rbuff = 0;
		}
	}
	if (rbuff)
       	pc_release_buf(rbuff);
    return(p);

return_error:
	if (rbuff)
       	pc_release_buf(rbuff);
    return(0);

}

static byte *exfatnameseg2text(byte *lfn, int *current_lfn_length, byte *pfi, int use_charset) /* __fn__ */
{
    int n;
        for(n=0; n<30; n += 2,pfi+=2)
        {
            if((*pfi==0x00)&&(*(pfi+1)==0x00))
			{
				CS_OP_TERM_STRING(lfn, use_charset);
				break;
			}
            if (*current_lfn_length == FILENAMESIZE_CHARS)
                 return(0);
            CS_OP_LFI_TO_TXT(lfn, pfi, use_charset);
            CS_OP_INC_PTR(lfn, use_charset);
            *current_lfn_length += 1;
        }
        CS_OP_TERM_STRING(lfn, use_charset);

    return(lfn);
}

#if (INCLUDE_REVERSEDIR)

static BOOLEAN pc_exfatrfindpreventry( DROBJ *pobj,BLKBUFF **pscan_buff);
static BOOLEAN pc_exfatrfindendentry( DROBJ *pobj,BLKBUFF **pscan_buff);

BOOLEAN pc_exfatrfindin( DROBJ *pobj, byte *filename, int action, int use_charset, BOOLEAN starting)          /*__fn__*/
{
    dword entries_processed;
    DIRBLK *pd;
    BOOLEAN still_searching;
   BLKBUFF *scan_buff, **pscan_buff; /* If using block buffers for scan */
    entries_processed = 0;
    scan_buff = 0;
    pscan_buff = &scan_buff;        /* Assume using buffer pool */

    rtfs_clear_errno();  /* Clear it here just in case */

    /* For convenience. We want to get at block info here   */
    pd = &pobj->blkinfo;

    still_searching = TRUE;
	while (still_searching)
	{
	dword current_block;
	int   current_index;
	BOOLEAN found;
		if (starting)
		{
			/* seek to end and set pd->my_block	pd->my_index */
			if (! pc_exfatrfindendentry(pobj,pscan_buff))
        		goto cleanup_and_fail;
		}
		else
		{
			/* seek to the previous entry */
			if (!pc_exfatrfindpreventry(pobj,pscan_buff))
        		goto cleanup_and_fail;
		}
		starting = FALSE;
       	current_block = pd->my_block;  /* Remember the start point so we can walk backwards from here */
       	current_index = pd->my_index;

       	found = pcexfat_findin(pobj, filename, action, TRUE, use_charset);
		pd->my_block = current_block;  /* Restore the start point so we can walk backwards from here */
		pd->my_index = current_index;
		if (found)
		{
        	if (scan_buff)
            	pc_release_buf(scan_buff);
        	return (TRUE);
        }                   /* if (match) */
        else if (get_errno() != PENOENT)
            goto cleanup_and_fail;

        entries_processed += pobj->pdrive->drive_info.inopblock;
        if (entries_processed >= RTFS_CFG_MAX_DIRENTS)
        {
         	rtfs_set_errno(PEINVALIDDIR, __FILE__, __LINE__);
            goto cleanup_and_fail;
         }

    } /* while (still_searching) */
    if (!get_errno())
        rtfs_set_errno(PENOENT, __FILE__, __LINE__);
cleanup_and_fail:
    if (scan_buff)
        pc_release_buf(scan_buff);
    return (FALSE);
}

/* Scan backward and set pd->my_block:pd->my_index to the beginning of the last segment
   return FALSE and set PENOENT if no more
   return FALSE and set PEINVAL if unexpected behavior */
static BOOLEAN pc_exfatrfindendentry( DROBJ *pobj,BLKBUFF **pscan_buff)          /*__fn__*/
{
dword my_last_block;
int   my_last_index;
BOOLEAN still_searching;
dword user_buffer_first_block,user_buffer_n_blocks, blocks_in_buffer,entries_processed;
byte *pi;
DIRBLK *pd;
byte *pscan_data;

    /* For convenience. We want to get at block info here   */
    pd = &pobj->blkinfo;
	entries_processed = 0;
	my_last_block = pd->my_block;
	my_last_index = pd->my_index;

    /* Read the data   */
    still_searching = TRUE;
    if (!pc_multi_dir_get(pobj, pscan_buff, &pscan_data, 0, &blocks_in_buffer, FALSE))
        goto cleanup_and_fail;
    user_buffer_first_block = pobj->blkinfo.my_block;
    user_buffer_n_blocks = blocks_in_buffer;
    if (!blocks_in_buffer)
    {
        still_searching = FALSE;
        pi = 0;
    }
    else
    {
        pi = (byte *) pc_data_in_ubuff(pobj->pdrive, user_buffer_first_block, pscan_data, user_buffer_first_block,user_buffer_n_blocks);
    }
    while (still_searching)
    {
        /* Look at the current inode   */
       	pi += (pd->my_index*EXFATDIRENTSIZE);
        /* And look for a match   */
        while ( pd->my_index < pobj->pdrive->drive_info.inopblock )
        {
            if (*pi == EXFAT_DIRENTTYPE_EOF)
            {
            	still_searching = FALSE;
                break;
            }
            if (*pi == EXFAT_DIRENTTYPE_FILE) /* It's a file or directory remember it in case it is the end */
			{
               	my_last_block = pd->my_block;
               	my_last_index = pd->my_index;
            }
            pd->my_index++;
            pi += EXFATDIRENTSIZE;
        }
        entries_processed += pobj->pdrive->drive_info.inopblock;
        if (entries_processed >= RTFS_CFG_MAX_DIRENTS)
        {
            rtfs_set_errno(PEINVALIDDIR, __FILE__, __LINE__);
            goto cleanup_and_fail;
        }

        if (still_searching)
		{
        	pd->my_index = 0;
        	blocks_in_buffer -= 1;
        	if(blocks_in_buffer)
            	pobj->blkinfo.my_block += 1;
        	else
        	{
            	if (!pc_multi_dir_get(pobj, pscan_buff, &pscan_data, 0, &blocks_in_buffer, TRUE))
                	goto cleanup_and_fail;
                if (!blocks_in_buffer)
                	still_searching = FALSE;
                else
                {
                	user_buffer_first_block = pobj->blkinfo.my_block;
                	user_buffer_n_blocks = blocks_in_buffer;
                	pi = (byte *) pc_data_in_ubuff(pobj->pdrive, user_buffer_first_block, pscan_data, user_buffer_first_block,user_buffer_n_blocks);
                }
        	}
		}
    }
    pd->my_block = my_last_block;
    pd->my_index = my_last_index;
	return(TRUE);
cleanup_and_fail:
    return (FALSE);
}

/* Scan backwards and set pd->my_block:pd->my_index to the previous directory entry
   return FALSE and set PENOENT if no more
   return FALSE and set PEINVAL if unexpected behavior */
static BOOLEAN pc_exfatprevindex( DROBJ *pobj)          /*__fn__*/
{
DIRBLK *pd;

    pd = &pobj->blkinfo;

    if (pd->my_index != 0)
	{
    	pd->my_index -= 1;
        return (TRUE);
	}
    else
	{
    	dword current_cluster, prev_cluster;
        current_cluster = pc_sec2cluster(pobj->pdrive,pd->my_block);
        prev_cluster = pc_sec2cluster(pobj->pdrive,pd->my_block-1);
		if (prev_cluster == current_cluster)
		{
			pd->my_block -= 1;
			pd->my_index = pobj->pdrive->drive_info.inopblock - 1;
			return(TRUE);

		}
		else
		{
			dword cluster,next_cluster;
			cluster = pc_sec2cluster(pobj->pdrive,pd->my_frstblock);
        	if (current_cluster == cluster)
			{ /* at the beginning already */
                rtfs_set_errno(PENOENT, __FILE__, __LINE__);
                return (FALSE);
			}
			do
			{
				int end_of_chain = 0;
                next_cluster = 0;
                /* Force it to use the FAT buffer pool by passing alt buffer and size as zero */
				if (exFatfatop_getdir_frag(pobj, cluster, &next_cluster, 1, &end_of_chain) == 1 && !end_of_chain)
				{
					if (next_cluster == current_cluster)
					{	/* Last sector in the previous cluster */
						pd->my_block = pc_cl2sector(pobj->pdrive, cluster);
						pd->my_block += pobj->pdrive->drive_info.secpalloc-1;
						pd->my_index = pobj->pdrive->drive_info.inopblock-1;
						return(TRUE);
					}
					cluster = next_cluster;
				}
				else
					break;

			}
			while (cluster >= 2 && cluster != FAT_EOF_RVAL);
		}
        rtfs_set_errno(PEINVALIDDIR, __FILE__, __LINE__);
		return(FALSE);
	}
}
/* Scan backwards and set pd->my_block:pd->my_index to the beginning of the previous segment
   return FALSE and set PENOENT if no more
   return FALSE and set PEINVAL if unexpected behavior */
static BOOLEAN pc_exfatrfindpreventry( DROBJ *pobj,BLKBUFF **pscan_buff)          /*__fn__*/
{
dword saved_block, my_last_block,my_last_lfn_block;
int   saved_index, my_last_index,my_last_file_index;
dword user_buffer_first_block,user_buffer_n_blocks, blocks_in_buffer,entries_processed;
byte *pi;
DIRBLK *pd;
byte *pscan_data;

	entries_processed = 0;

    /* For convenience. We want to get at block info here   */
    pd = &pobj->blkinfo;
	saved_block = pd->my_block;
	saved_index = pd->my_index;

	my_last_block = my_last_lfn_block = 0;
	my_last_index = my_last_file_index =  0;

	if (!pc_exfatprevindex(pobj))
		goto error_return;

	for(entries_processed=0;;entries_processed++)
	{
		if (entries_processed >= RTFS_CFG_MAX_DIRENTS)
        {
            rtfs_set_errno(PEINVALIDDIR, __FILE__, __LINE__);
            goto error_return;
        }
    	if (!pc_multi_dir_get(pobj, pscan_buff, &pscan_data, 0, &blocks_in_buffer, FALSE))
        	goto error_return;
    	user_buffer_first_block = pobj->blkinfo.my_block;
    	user_buffer_n_blocks = blocks_in_buffer;
    	if (!blocks_in_buffer)
        	break;

       	pi = (byte *) pc_data_in_ubuff(pobj->pdrive, user_buffer_first_block, pscan_data, user_buffer_first_block,user_buffer_n_blocks);
       	pi += (pd->my_index*EXFATDIRENTSIZE);
		if (*pi == EXFAT_DIRENTTYPE_FILE) /* It's a file or directory (not stream extension etc), take it */
			return(TRUE);
		if (!pc_exfatprevindex(pobj))
			break;
	}
error_return:
	pd->my_block = saved_block;
	pd->my_index = saved_index;
	return(FALSE);
}
#endif /* #if (INCLUDE_REVERSEDIR) */