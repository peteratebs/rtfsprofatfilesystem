/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* RTEXFATUTIL.C - Utility functions for exFat.

    Routines Exported from this file include:

    pcexfat_pdrtoexfat			   - Temp solution, called via macro.
    pc_release_exfat_buffers	   - Called when mount fails, needs to be called from unmount still.
    exFatfatop_get_frag			   - More needed for ProPlus.. called from pc_multi_dir_get, pc_gread, and pc_bfilio_load_fragments_until.
    pcexfat_flush				   - Called from fatop_flushfat.
    pcexfat_link_file_chain		   - Called from pc_bfilio_flush. More needed for ProPlus..
    pcexfat_load_root_fragments	   - Called from pcexfat_grow_directory.

*/

#include "rtfs.h"

int pc_async_flush_bam_blocks(DDRIVE *pdrive,dword max_flushes_per_pass);
ddword pc_byte2ddwclmodbytes(DDRIVE *pdr, ddword nbytes64);

/* Return a drobj finode pair that represent the root of an exFat volume. */
DROBJ *pcexfat_get_root( DDRIVE *pdrive)                                 /*__fn__*/
{
    DIRBLK *pd;
    DROBJ *pobj;
    FINODE *pfi;

    pobj = pc_allocobj();
    if (!pobj)
        return (0);
    pobj->pdrive = pdrive;
   	pfi = pc_scani(pdrive, pdrive->drive_info.rootblock, 0);
	if (pfi)
	{
       	pc_freei(pobj->finode);
       	pobj->finode = pfi;
	}
	else    /* No inode in the inode list where it came from */
    {
		rtfs_memset(pobj->finode, 0, sizeof(*pobj->finode));
		pobj->finode->exfatinode.FileAttributes				=   ADIRENT;
		pc_pfinode_cluster(pdrive, pobj->finode, PDRTOEXFSTR(pdrive)->FirstClusterOfRootDirectory);
		pobj->finode->fattribute		= ADIRENT;
		pobj->finode->my_drive		= pdrive;
		pobj->finode->my_block		= pdrive->drive_info.rootblock;
		pobj->finode->my_index		= 0;
        pc_marki(pobj->finode , pdrive , pdrive->drive_info.rootblock, 0);
    }
    pobj->pdrive = pdrive;
    /* Set up block information for scanning the root */
    pd = &pobj->blkinfo;
    pd->my_frstblock = pdrive->drive_info.rootblock;
    pd->my_block = pdrive->drive_info.rootblock;
    pd->my_index = 0;
    pobj->isroot = TRUE;
	pd->my_exNOFATCHAINfirstcluster = 0;	/* Root always has a chain */
	pd->my_exNOFATCHAINlastcluster = 0;
    return (pobj);
}



/* Return the finode's length from the length field rounded up to the nearest cluster */
dword pcexfat_finode_length_clusters(FINODE *pefinode)
{
ddword file_chain_length_bytes;
dword file_chain_length;
	ddword ltempddw;
    file_chain_length_bytes = pc_byte2ddwclmodbytes(pefinode->my_drive, pefinode->fsizeu.fsize64);
	ltempddw = M64RSHIFT(file_chain_length_bytes, pefinode->my_drive->drive_info.log2_bytespalloc);
	file_chain_length = M64LOWDW(ltempddw);
	return(file_chain_length);
}

/* Return the exFat substructure of the drive structure */
EXFATDDRIVE *pcexfat_pdrtoexfat(DDRIVE *pdr)
{
	return (&pdr->exfatextension);
}


/* Called when a volume is unmounted. Invokes the callback layer to release bitmap buffer, and upcase table memory */
void pc_release_exfat_buffers(DDRIVE *pdr)
{
	EXFATMOUNTPARMS exMountParms;

	rtfs_memset(&exMountParms, 0, sizeof(exMountParms));

	exMountParms.driveID 					= 	pdr->driveno;	/* Make sure this is set already */
	exMountParms.pdr	 					= 	(void *) pdr;
    exMountParms.BitMapBufferCore 			=	PDRTOEXFSTR(pdr)->BitMapBufferCore;
    exMountParms.BitMapBufferControlCore    =   PDRTOEXFSTR(pdr)->BitMapBufferControlCore;

	if (PDRTOEXFSTR(pdr)->UpCaseBufferCore    !=  &cStandarducTableunCompressed[0])
    	exMountParms.UpCaseBufferCore 			=	PDRTOEXFSTR(pdr)->UpCaseBufferCore;

    PDRTOEXFSTR(pdr)->BitMapBufferCore			= 0;
    PDRTOEXFSTR(pdr)->UpCaseBufferCore			= 0;
    PDRTOEXFSTR(pdr)->BitMapBufferControlCore 	= 0;

	rtfs_sys_callback(RTFS_CBS_RELEASEEXFATBUFFERS, (void *) &exMountParms); /* call the application layer to free the buffer core */
}

/* If the directory entry has NOCHAIN attribute return the first cluster in the contiguoous region, else return zero.
   used only for directory objects, files are done differently, see (rtexfatfilio.c). */
dword pcexfat_getexNOFATCHAINfirstcluster(DROBJ *pobj)
{
dword firstcluster	= 0;
	if (ISEXFAT(pobj->pdrive))
	{
		if (!pobj->isroot)	/* The root always has a cluster chain */
		{
			FINODE *pfi = pobj->finode;
			if (pfi && pfi->exfatinode.GeneralSecondaryFlags & EXFATNOFATCHAIN)
			{
				firstcluster = pc_finode_cluster(pfi->my_drive, pfi);
			}
		}
	}
	return(firstcluster);

}
/* If the directory entry has NOCHAIN attribute return the last cluster in the contiguoous region, else return zero.
   used only for directory objects, files are done differently, see (rtexfatfilio.c). */
dword pcexfat_getexNOFATCHAINlastcluster(DROBJ *pobj)
{
dword lastcluster	= 0;
	if (ISEXFAT(pobj->pdrive))
	{
		if (!pobj->isroot)	/* The root always has a cluster chain */
		{
			FINODE *pfi = pobj->finode;
			if (pfi && pfi->exfatinode.GeneralSecondaryFlags & EXFATNOFATCHAIN)
			{
				dword nclustersinfile;
				nclustersinfile = pcexfat_finode_length_clusters(pfi);
				lastcluster = pc_finode_cluster(pfi->my_drive, pfi) + nclustersinfile-1;
			}
		}
	}
	return(lastcluster);

}


void _pc_link_buffer_and_freelist(FATBUFF *pblk, int user_num_fat_buffers, byte *provided_buffers, dword fat_buffer_page_size_bytes);

dword _fatop_get_frag(DDRIVE *pdr, byte *palt_buffer, dword alt_buffer_size, dword startpt, dword *pnext_cluster, dword n_clusters, int *end_of_chain, BOOLEAN is_async);

/* For finode with EXFATNOFATCHAIN attribute set, expand the fragment list to the width of the file.
   fragments are guaranteed to not exceed 4 gigabytes.  */
BOOLEAN pcexfat_expand_nochain(FINODE *pefinode)
{
dword loaded_chain_length,required_length;

	if ((pefinode->exfatinode.GeneralSecondaryFlags & EXFATNOFATCHAIN)==0)
	{
		ERTFS_ASSERT(0)
		return(FALSE);
	}
	/* If the chain is empty just add a new fragment */
	if (pefinode->pbasic_fragment)
	{
		loaded_chain_length = pc_fraglist_count_clusters(pefinode->pbasic_fragment,0);
	}
	else
		loaded_chain_length = 0;

	required_length = pcexfat_finode_length_clusters(pefinode);

	if (required_length > loaded_chain_length)
	{
	dword nclusters,first_new_cluster;
		nclusters				= required_length-loaded_chain_length;
		first_new_cluster 		= pc_finode_cluster(pefinode->my_drive, pefinode) + loaded_chain_length;
		return (pc_grow_basic_fragment(pefinode, first_new_cluster, nclusters));
	}
	else
		return (TRUE);
}

/* Return the number of contiguous clusters,up to n_clusters, in a subdirectory starting at startpt. If the last cluster is end of chain
   set *end_of_chain. Should return at least 1, otherwise startpt is not valid.
   If the volume is exFat handles NOFATCHAIN entries or entries with chains. If it is FAT, we fall through to FAT processing */
dword exFatfatop_getdir_frag(DROBJ *pobj, dword startpt, dword *pnext_cluster, dword n_clusters, int *end_of_chain)
{
	DDRIVE *pdr;

	/* Use pobj for control on directory scans */
	pdr = pobj->pdrive;
	if ((startpt < 2) || startpt > pdr->drive_info.maxfindex)
    {
        rtfs_set_errno(PEINVALIDCLUSTER, __FILE__, __LINE__);
        return (0);
    }
	if (ISEXFAT(pdr))
	{
		if (pobj->blkinfo.my_exNOFATCHAINfirstcluster)
		{
			dword nclustersinfile,nclustersleftinfile,lastclusterinfile,first_cluster;
			first_cluster = pobj->blkinfo.my_exNOFATCHAINfirstcluster;
			nclustersinfile = pobj->blkinfo.my_exNOFATCHAINlastcluster - pobj->blkinfo.my_exNOFATCHAINfirstcluster + 1;
			if (!first_cluster || startpt < first_cluster)
				return (0);
			lastclusterinfile = first_cluster + nclustersinfile -1;
			if (startpt > lastclusterinfile)
				return(0);
			nclustersleftinfile = nclustersinfile - (startpt - first_cluster);
			/* We pass max sometimes so limit it to the size of the file or directory */
			if (n_clusters > nclustersleftinfile)
				n_clusters = nclustersleftinfile;
			if (n_clusters >= nclustersleftinfile)
			{
				n_clusters = nclustersleftinfile;
				*end_of_chain  = 1;
			}
			else
				*end_of_chain = 0;
			*pnext_cluster = startpt + n_clusters;
			return(n_clusters);
		}
		/* Note: Fall through to generic FAT chain travers if we are apobj and not a NOFATCHAIN */
	}
    return(_fatop_get_frag(pdr, 0, 0, startpt, pnext_cluster, n_clusters, end_of_chain, FALSE));
}

/* Return the number of contiguous clusters,up to n_clusters, in a file starting at startpt. If the last cluster is end of chain
   set *end_of_chain. Should return at least 1, otherwise startpt is not valid
   If the volume is exFat handles NOFATCHAIN entries or entries with chains. If it is FAT, we fall through to FAT processing */
dword exFatfatop_getfile_frag(FINODE *pfi, dword startpt, dword *pnext_cluster, dword n_clusters, int *end_of_chain)
{
	DDRIVE *pdr;
	pdr = pfi->my_drive;
	if ((startpt < 2) || startpt > pdr->drive_info.maxfindex)
    {
        rtfs_set_errno(PEINVALIDCLUSTER, __FILE__, __LINE__);
        return (0);
    }
	if (ISEXFAT(pdr))
	{
		if (pfi->exfatinode.GeneralSecondaryFlags & EXFATNOFATCHAIN)
		{
		dword nclustersleftinfile,nclustersinfile,lastclusterinfile,first_cluster;
			/* Clusters left between startpt (inclusive) and end of file */
			first_cluster = pc_finode_cluster(pfi->my_drive, pfi);
			nclustersinfile = pcexfat_finode_length_clusters(pfi);
			if (!first_cluster || startpt < first_cluster)
				return (0);
			lastclusterinfile = first_cluster + nclustersinfile -1;
			if (startpt > lastclusterinfile)
				return(0);
			nclustersleftinfile = nclustersinfile - (startpt - first_cluster);
			/* We pass max sometimes so limit it to the size of the file or directory */
			if (n_clusters >= nclustersleftinfile)
			{
				n_clusters = nclustersleftinfile;
				*end_of_chain  = 1;
			}
			else
				*end_of_chain = 0;

			*pnext_cluster = startpt + n_clusters;
			return(n_clusters);
		}
		/* Note: Fall through to generic FAT chain traverse if we are finbode and NOFATCHAIN  attribute is clear */
	}
    return(_fatop_get_frag(pdr, 0, 0, startpt, pnext_cluster, n_clusters, end_of_chain, FALSE));
}


/* Called by fatop_flushfat() when the volume is exFat. Flushes the FAT and then the BAM */
BOOLEAN pcexfat_flush(DDRIVE *pdrive)
{
    if (pc_async_flush_fat_blocks(pdrive,0) != PC_ASYNC_COMPLETE)
        return(FALSE);
    if (pc_async_flush_bam_blocks(pdrive,0) != PC_ASYNC_COMPLETE)
        return(FALSE);
    return(TRUE);

}

/* Link all clusters in the fragment chain together in the FAT and terminate the chain.
   Called by exfat file write routines when the entry is not contiguous
   don't tell the region manegr, that's been done already */
BOOLEAN pcexfat_link_file_chain(DDRIVE *pdr, REGION_FRAGMENT *pf)
{
dword  prev_cluster, n_contig;

	prev_cluster = 0;
	while (pf)
	{
		n_contig = pf->end_location - pf->start_location + 1;
   		if (!fatop_link_frag(pdr, 0, 0, FOP_EXF_CHAIN|FOP_LINK_PREV|FOP_LINK, prev_cluster, pf->start_location, n_contig))
 		prev_cluster = pf->end_location;
		pf = pf->pnext;
	}
	if (prev_cluster)
    	fatop_pfaxxterm(pdr, prev_cluster);
    return (TRUE);
}

/* Load the root directory into a fragment chain */
REGION_FRAGMENT *pcexfat_load_root_fragments(DDRIVE *pdrive)
{
dword cluster;
REGION_FRAGMENT *pfirst_fragment,*pend_fragment;

	cluster = PDRTOEXFSTR(pdrive)->FirstClusterOfRootDirectory;

    pfirst_fragment = pend_fragment = pc_fraglist_frag_alloc(pdrive, cluster,cluster, 0);
	if (!pfirst_fragment)
		return(0);
    while (cluster)
	{
     	/* Consult the fat for the next cluster. */
     	cluster = fatop_next_cluster(pdrive, cluster);
		if (cluster == 0)
			goto freechain_and_exit;
		if (cluster == FAT_EOF_RVAL)
			cluster = 0;
     	if (cluster)
		{

        	if (pend_fragment->end_location+1 == cluster)
            	pend_fragment->end_location = cluster;
        	else
        	{
        	REGION_FRAGMENT *pnew_fragment;
            	pnew_fragment = pc_fraglist_frag_alloc(pdrive, cluster,cluster, 0);
            	if (!pnew_fragment)
            	{ /* Only happens on an error */
                	goto freechain_and_exit;
            	}
            	pend_fragment->pnext = pnew_fragment;
            	pend_fragment = pnew_fragment;
        	}
		}
	}
	return(pfirst_fragment);

freechain_and_exit:
	if (pfirst_fragment)
		pc_fraglist_free_list(pfirst_fragment);
	return(0);
}

/* Return the start path for the search. If the path starts at the root return an empty string.
   Otherwise, return the current working directory */
static BOOLEAN pcexfat_base_path(DDRIVE *pdrive, byte *outpath, byte *inpath, int use_charset)
{
	/* Start from the root or current working directory */
    if (CS_OP_CMP_ASCII(inpath,'\\', use_charset))
	{ /* We'll tack the slash on later */
		CS_OP_TERM_STRING(outpath, use_charset);
	}
	else
	{
		if (!pcexfat_get_cwd(pdrive, outpath, use_charset))
			return(FALSE);
	}
	return(TRUE);
}

/* Find the parent in outpath and null terminate pass in  \a\b\c will return \a\b \aaaa will return \ */
static byte *pcexfat_parse_dotdot(byte *outpath, BOOLEAN has_backslash, int use_charset)
{
byte *pfout,*lastslash;

	pfout = outpath;
	lastslash = 0;
    while (CS_OP_IS_NOT_EOS(pfout, use_charset))
	{
	byte *p;
		if (CS_OP_CMP_ASCII(pfout,'\\', use_charset))
		{
			p = pfout;
			CS_OP_INC_PTR(p, use_charset);
			if (pfout == outpath || CS_OP_IS_NOT_EOS(p, use_charset))
				lastslash = pfout;
		}
		CS_OP_INC_PTR(pfout, use_charset);
	}
	if (lastslash)
	{/* Leave the root slash there, leave the slash if inpath has a backslash */
		if (has_backslash || lastslash == outpath)
			CS_OP_INC_PTR(lastslash, use_charset);
		CS_OP_TERM_STRING(lastslash, use_charset);
	}
	return(lastslash);
}

/* Take inpath. Append to cwd if it is not rooted. remove '.' and '..' */
BOOLEAN pcexfat_parse_path(DDRIVE *pdrive, byte *outpath, byte *inpath, int use_charset)
{
BOOLEAN isdot, isdotdot, is_eos, is_backslash;
byte *pf0, *pf1, *pf2, *pfout;
int outlength = 0;

	/* Check for D: and skip it if there is one. */
	{	byte *pfinpath = inpath;
	if (CS_OP_IS_NOT_EOS(pfinpath, use_charset))
	{
		CS_OP_INC_PTR(pfinpath, use_charset);
		if (CS_OP_IS_NOT_EOS(pfinpath, use_charset))
		{
			if (CS_OP_CMP_ASCII(pfinpath,':', use_charset))
			{
				CS_OP_INC_PTR(pfinpath, use_charset);
				inpath = pfinpath;
			}
		}
	}}
	/* Initialize outpath with the cwd or '\\' if inpath begins at the root */
	if (!pcexfat_base_path(pdrive, outpath, inpath,use_charset))
		return(FALSE);
	/* Now get the base path length and provide a path seprator between the base and the rest */
	pfout = outpath;

    while (CS_OP_IS_NOT_EOS(pfout, use_charset))
	{
		outlength += 1;
		CS_OP_INC_PTR(pfout, use_charset);
	}
	/* Add a backslash to the base if we are not starting from root and the leading character is not '.'
	   If leading character is '.' we will either go backwords or we will append a . right away */
    if (CS_OP_IS_NOT_EOS(inpath, use_charset) && outlength != 1 && !(CS_OP_CMP_ASCII(inpath,'\\', use_charset)) && !(CS_OP_CMP_ASCII(inpath,'.', use_charset)))
	{
		/* Append '\\' followed by our path */
		if (outlength >= EMAXPATH_CHARS)
			goto path_error;
		CS_OP_ASSIGN_ASCII(pfout,'\\', use_charset);
		CS_OP_INC_PTR(pfout, use_charset);
		CS_OP_TERM_STRING(pfout, use_charset);
		outlength += 1;
	}

	pf1 = pf0 = inpath;
	CS_OP_INC_PTR(pf1, use_charset);
	pf2 = pf1;
	CS_OP_INC_PTR(pf2, use_charset);

    while (CS_OP_IS_NOT_EOS(pf0, use_charset))
	{
		/* It is dot or dot dot if it is immediately followed by a separator */
		isdot = isdotdot = is_eos = is_backslash = FALSE;
		if (CS_OP_CMP_ASCII(pf0,'.', use_charset))
			isdot=TRUE;
		if (isdot && CS_OP_CMP_ASCII(pf1,'.', use_charset))
		{
			is_eos = CS_OP_IS_EOS(pf2, use_charset);
			is_backslash = CS_OP_CMP_ASCII(pf2,'\\', use_charset);
			if (is_eos || is_backslash)
			{
				isdot=FALSE;
				isdotdot=TRUE;
				/* ".." is a 2 character sequence so consume another character */
				CS_OP_INC_PTR(pf0, use_charset);
				CS_OP_INC_PTR(pf1, use_charset);
				CS_OP_INC_PTR(pf2, use_charset);
				/* Skip '.' */
				CS_OP_INC_PTR(pf0, use_charset);
				CS_OP_INC_PTR(pf1, use_charset);
				CS_OP_INC_PTR(pf2, use_charset);
				/* Skip '\' */
				if (is_backslash)
				{
					CS_OP_INC_PTR(pf0, use_charset);
					CS_OP_INC_PTR(pf1, use_charset);
					CS_OP_INC_PTR(pf2, use_charset);
				}
			}
		}
		if (isdot)
		{
			is_eos = CS_OP_IS_EOS(pf1, use_charset);
			is_backslash = CS_OP_CMP_ASCII(pf1,'\\', use_charset);
			if (is_eos || is_backslash)
			{
				isdot=TRUE;
				/* Skip '.' */
				CS_OP_INC_PTR(pf0, use_charset);
				CS_OP_INC_PTR(pf1, use_charset);
				CS_OP_INC_PTR(pf2, use_charset);
				/* Skip '\' */
				if (is_backslash)
				{
					CS_OP_INC_PTR(pf0, use_charset);
					CS_OP_INC_PTR(pf1, use_charset);
					CS_OP_INC_PTR(pf2, use_charset);
				}
			}
			else
				isdot=FALSE;
		}
		if (isdot)
		{
			;	/* no-op */
		}
		else if (isdotdot)
		{	/* find the last backslash in outpath and null terminate there, return that location
		       if none found return null */
			pfout = pcexfat_parse_dotdot(outpath, is_backslash, use_charset);
			if (!pfout)
				goto path_error;
			outlength = rtfs_cs_strlen(outpath, use_charset);
		}
		else
		{	/* Append the character to the path */
			if (outlength >= EMAXPATH_CHARS)
				goto path_error;
			CS_OP_CP_CHR(pfout, pf0, use_charset);
			CS_OP_INC_PTR(pfout, use_charset);
			CS_OP_TERM_STRING(pfout, use_charset);
			outlength += 1;
			CS_OP_INC_PTR(pf0, use_charset);
			CS_OP_INC_PTR(pf1, use_charset);
			CS_OP_INC_PTR(pf2, use_charset);
		}
	}
	return(TRUE);

path_error:
	rtfs_set_errno(PEINVALIDPATH, __FILE__, __LINE__);
	return(FALSE);
}
static void	pcexfat_set_user_pwdstring(DDRIVE *pdrive, byte *pathname, int use_charset);
static byte *pcexfat_get_user_pwdstring(DDRIVE *pdrive);

/* Set the current working directory for the specified drive. exFat directory trees do not link backwards with .. so we
   save the full path to the root. */
BOOLEAN pcexfat_set_cwd(DDRIVE *pdrive, byte *name, int use_charset)
{
DROBJ *newcwdpobj;
BOOLEAN retval = FALSE;
BLKBUFF *scratch_buffer;

    scratch_buffer = pc_scratch_blk();
	if (!scratch_buffer)
		return(FALSE);

	/* Get the full path to the root of target path. */
	if (pcexfat_parse_path(pdrive, (byte *)scratch_buffer->data, name, use_charset))
	{
		newcwdpobj = pc_fndnode(name, use_charset);
		if (newcwdpobj)
		{
			DROBJ *ptemp;
			RTFS_SYSTEM_USER *pu;
			pu = rtfs_get_system_user();
			ptemp = rtfs_get_user_pwd(pu, pdrive->driveno, TRUE);	  /* Get cwd for driveno and clear it */
			if (ptemp)
        		pc_freeobj(ptemp);							  /* Free it */
			rtfs_set_user_pwd(pu, newcwdpobj);  /* Set cwd to newcwdpobj */
			pcexfat_set_user_pwdstring(pdrive, (byte *)scratch_buffer->data, use_charset);
			retval = TRUE;
		}
	}
    pc_free_scratch_blk(scratch_buffer);
	return(retval);
}


/* Get the current working directory for the specified drive. exFat directory trees do not link backwards with .. so we
   save the full path to the root and return a copy to it. */
BOOLEAN pcexfat_get_cwd(DDRIVE *pdrive, byte *path, int use_charset)
{
byte *p;

   	if (pdrive && ISEXFAT(pdrive) && ((p = pcexfat_get_user_pwdstring(pdrive)) != 0))
   	{
   	word *pw; /* if the first word is unicode NULL assign \\ to the string */
		pw = (word *) p;
		if (pw[0] == 0)
		{
	    	CS_OP_ASSIGN_ASCII(p,'\\',CS_CHARSET_UNICODE);
	    	CS_OP_INC_PTR(p, CS_CHARSET_UNICODE);
	    	CS_OP_TERM_STRING(p, CS_CHARSET_UNICODE);
		}
		/* pwd is stored in unicode so map it to asci jis if needed */
		if (use_charset == CS_CHARSET_UNICODE)
		{
			rtfs_cs_strcpy(path, (byte *) p, use_charset);
		}
		else
		{
    		map_unicode_to_jis_ascii(path, p);
		 }
   	}
	return(TRUE);
}

static void	pcexfat_set_user_pwdstring(DDRIVE *pdrive, byte *pathname, int use_charset)
{
	byte *pcwd = (byte *) pcexfat_get_user_pwdstring(pdrive);
	/* pwd is stored in unicode so map it to asci jis if needed */
	if (use_charset == CS_CHARSET_UNICODE)
		rtfs_cs_strcpy(pcwd, pathname, use_charset);
	else /* if (use_charset != CS_CHARSET_UNICODE) */
	{
   			map_jis_ascii_to_unicode(pcwd, pathname);
	}
}

static byte *pcexfat_get_user_pwdstring(DDRIVE *pdrive)
{
	byte *pcwd = (byte *) &rtfs_get_system_user()->cwd_string[pdrive->driveno][0];
	return (pcwd);
}

/* Read blocks_to_read sectors from the begining of a file. statobj is returned for gfirst, gnext */
BOOLEAN pcexfat_gread(DSTAT *statobj, int blocks_to_read, byte *buffer, int *blocks_read)     /*__apifn__*/
{
    BOOLEAN ret_val;
    DDRIVE *pdrive;
    FINODE *pfi;
    dword cluster_no;
    int blocks_per_cluster,blocks_left;

    ret_val = FALSE;

    if (!statobj->pmom)
        return(FALSE);

    *blocks_read = 0;
	pdrive = ((DROBJ *) statobj->pmom)->pdrive;
    pfi = pc_scani(pdrive, statobj->my_block, statobj->my_index);
    if (!pfi)
    {   /* No inode in the inode list. creat a phoney DROBJ and scan right to the entry on disk  */
	DROBJ  *pobj;
		pobj = pc_mkchild((DROBJ *) statobj->pmom);
		if (pobj)
		{
			pobj->blkinfo.my_block = statobj->my_block;
			pobj->blkinfo.my_index = statobj->my_index;
			if (!pcexfat_findin(pobj, (byte *)" ", GET_INODE_STAR, FALSE, CS_CHARSET_NOT_UNICODE))
			{
				pfi = 0;
			}
			else
				pfi = pobj->finode;
			pobj->finode = 0;
			pc_freeobj(pobj);
		}
	}
    if (pfi)
	{
       	ret_val = TRUE;
    	blocks_per_cluster = pdrive->drive_info.secpalloc;
    	blocks_left = blocks_to_read;

    	cluster_no = pc_finode_cluster(pfi->my_drive, pfi);

    	while (cluster_no && blocks_left)
    	{
        	dword blockno,n_clusters,next_cluster;
        	int n_to_read,end_of_chain;
        	n_to_read = blocks_per_cluster;
        	if (n_to_read > blocks_left)
            	n_to_read = blocks_left;

        	blockno = pc_cl2sector(pdrive,cluster_no);
        	if (!blockno || !block_devio_xfer(pdrive,  blockno, buffer, n_to_read, TRUE))
        	{
            	ret_val = FALSE;
            	break;
        	}
        	*blocks_read += n_to_read;
        	blocks_left -= n_to_read;
			buffer += (n_to_read<<pdrive->drive_info.log2_bytespsec);
        	if (!blocks_left)
            	break;
        	n_clusters = exFatfatop_getfile_frag(pfi, cluster_no, &next_cluster, 1, &end_of_chain);
        	if (n_clusters == 1)
				cluster_no = next_cluster;
			else
				cluster_no = 0;
    	}
    	if (pfi)
    		pc_freei(pfi);
	}
    return(ret_val);
}

/* Calculate the checksum of the file as it will be stored on disk the unicode string is null terminated */
word pcexfat_checksum_util(word Checksum, BOOLEAN isprimary, byte *string)
{
int i;
byte *Entries;

	Entries = (byte *) string;
	for (i = 0; i < 32; i++)
	{
		if (isprimary && (i == 2 || i == 3))
			continue;
		Checksum =
		 ((Checksum & 1) ? 0x8000 : 0) + (Checksum>>1) + (word) Entries[i];
	}
	return(Checksum);
}

