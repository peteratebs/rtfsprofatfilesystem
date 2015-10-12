/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* RTVFAT.C - Contains routines specific to the vfat implementation */

#include "rtfs.h"

#if (INCLUDE_VFAT)

#define SEARCH_LFN_IN_ALIAS_SCAN 0


#define FIRST_NAMESEG 0x40
#define NAMESEG_ORDER 0x3F
/* LFNINODE - This is an image of lfn extended names in a subdirectory */
/* Note: lfn file names are null terminated unicode 00, the lfninode is
   padded mod 13 with ffff */
typedef struct lfninode {
                              /* The first LNF has 0x40 + N left */
        byte    lfnorder;     /* 0x45, 0x04, 0x03, 0x02, 0x01 they are stored in
                               reverse order */
        byte    lfname1[10];
        byte    lfnattribute; /* always 0x0F */
        byte    lfnres;       /* reserved */
        byte    lfncksum;   /* All lfninode in one dirent have the same chksum */
        byte    lfname2[12];
        word    lfncluster; /* always 0x0000 */
        byte    lfname3[4];
        } LFNINODE;


void pc_addtoseglist(SEGDESC *s, dword my_block, int my_index);
void pc_zeroseglist(SEGDESC *s);
byte *pc_data_in_ubuff(DDRIVE *pdr, dword blockno, byte *puser_buffer, dword user_buffer_first_block,dword user_buffer_n_blocks);


static BOOLEAN pc_patcmp_vfat_8_3(byte *pat, byte *name, BOOLEAN dowildcard, int use_charset);
static BOOLEAN pc_allspace(byte *p, int i);
static byte *lfi2text(byte *lfn, int *current_lfn_length, LFNINODE *lfi, int nsegs, int use_charset);
static byte *pc_seglist2text(DDRIVE * pdrive, SEGDESC *s, byte *lfn, int use_charset);
static byte *pc_multiseglist2text(DDRIVE *pdrive, SEGDESC *s, byte *lfn, byte *pscan_data, dword user_buffer_first_block,dword user_buffer_n_blocks, int use_charset);

/***************************************************************************
    PC_FINDIN -  Find a filename in the same directory as the argument.

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

static BOOLEAN pc_vfatfindin( DROBJ *pobj, byte *filename, byte *fileext, int action, int use_charset);

BOOLEAN pc_findin( DROBJ *pobj, byte *filename, byte *fileext, int action, int use_charset)          /*__fn__*/
{
#if (INCLUDE_EXFATORFAT64)
	if (ISEXFATORFAT64(pobj->pdrive))
	{
	BOOLEAN retval;
		retval = pcexfat_findin(pobj, filename, action,  FALSE, use_charset);
		return(retval);
	}
	else
#endif
		return(pc_vfatfindin(pobj, filename, fileext, action, use_charset));
}

static BOOLEAN pc_vfatfindin( DROBJ *pobj, byte *filename, byte *fileext, int action, int use_charset)          /*__fn__*/
{
    dword user_buffer_first_block,user_buffer_n_blocks,blocks_in_buffer,entries_processed;
    DIRBLK *pd;
    DOSINODE *pi;
    FINODE *pfi;
    BOOLEAN matchfound;
    BOOLEAN dowildcard, still_searching;
    BLKBUFF *scratch;
    byte *lfn;
    byte sfn[26]; /* Leave room in case unicode */
    LFNINODE *lfn_node;
    SEGDESC s;
    byte lastsegorder;
    BLKBUFF *scan_buff, **pscan_buff; /* If using block buffers for scan */
    byte *p,*pscan_data;
    dword ubuff_size;
    byte *puser_buffer;
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

    RTFS_ARGSUSED_PVOID((void *) fileext);

    rtfs_clear_errno();  /* Clear it here just in case */
    scratch = pc_scratch_blk();
    if (!scratch)
        goto cleanup_and_fail;
    lfn = (byte *)scratch->data;

    pc_zeroseglist(&s);

    if (action == GET_INODE_WILD)
        dowildcard = TRUE;
    else
    {
        dowildcard = FALSE;
    }
    /* For convenience. We want to get at block info here   */
    pd = &pobj->blkinfo;

    /* Read the data   */
    still_searching = TRUE;
    if (!pc_multi_dir_get(pobj, pscan_buff, &pscan_data, puser_buffer, &blocks_in_buffer, FALSE))
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
        pi = (DOSINODE *) pc_data_in_ubuff(pobj->pdrive, user_buffer_first_block, pscan_data, user_buffer_first_block,user_buffer_n_blocks);
    }

    lastsegorder = 0;
    while (still_searching)
    {
        /* Look at the current inode   */
        pi += pd->my_index;

        /* And look for a match   */
        while ( pd->my_index < pobj->pdrive->drive_info.inopblock )
        {
            matchfound = FALSE;
            /* End of dir if name is 0  or 8 x (0xff)  */
            if (pc_check_dir_end(pi->fname))
            {
                rtfs_set_errno(PENOENT, __FILE__, __LINE__);
                goto cleanup_and_fail;
            }

            if (pi->fname[0] == PCDELETE)
            {
                pc_zeroseglist(&s);
            }
            else
            {
                /* Support long file names   */
                if (pi->fattribute == CHICAGO_EXT)
                {
                    lfn_node = (LFNINODE *) pi;
                    if (lfn_node->lfnorder & FIRST_NAMESEG)
                    {
                        pc_addtoseglist(&s, pd->my_block, pd->my_index);
                        /*  .. we could build up the name here too    */
                        lastsegorder = lfn_node->lfnorder;
                        s.ncksum = lfn_node->lfncksum;
                    }
                    else
                    {
                        if (s.nsegs)/* if a chain already exists */
                        {
                            if ( ((lfn_node->lfnorder & NAMESEG_ORDER) == (lastsegorder & NAMESEG_ORDER) - 1) &&
                                  (lfn_node->lfncksum == s.ncksum) )
                            {
                                /* Add new segment to lfn chain   */
                                lastsegorder = lfn_node->lfnorder;
                                pc_addtoseglist(&s, pd->my_block, pd->my_index);
                            }
                            else
                            {
                               /* disconnect chain... segments do not match   */
                                lastsegorder = 0;
                                pc_zeroseglist(&s);
                            }
                        }
                    }
                }
                else
                {
               /* Note: Patcmp wo not match on deleted   */
                    if (s.nsegs)
                    {
                        if (action == GET_INODE_STAR)
                            matchfound = TRUE;
                        else if (action == GET_INODE_DOTDOT)
                            matchfound = FALSE; /* 8.3, not lfn */
                        else
                        {
                            p = (byte*)pc_multiseglist2text(pobj->pdrive, &s,lfn, pscan_data, user_buffer_first_block,user_buffer_n_blocks, use_charset);
                            if (!p)
                                matchfound = FALSE; /* 8.3, not lfn */
                            else
                                matchfound = pc_patcmp_vfat(filename, p ,dowildcard, use_charset);
                        }
                    }
                    else
                        matchfound = FALSE; /* 8.3, not lfn */

                    if (matchfound)
                        matchfound = (BOOLEAN)(pc_cksum( (byte*) pi ) == s.ncksum);
                    else
                    {
                        if (action == GET_INODE_STAR)
                            matchfound = TRUE;
                        else if (action == GET_INODE_DOTDOT)
                        {
                            if (pi->fname[0] == (byte)'.' && pi->fname[1] == (byte)'.')
                                matchfound = TRUE; /* 8.3, not lfn */
                        }
                        else
                        { /* Make native charset name from the short file and try for a match.
                             Done in native charset because no LFN entry is created if the
                             name is valid 8.3, even with Unicode */
                            pc_cs_mfile(sfn,pi->fname,pi->fext, use_charset);
                            matchfound = pc_patcmp_vfat_8_3(filename, sfn, dowildcard, use_charset);
                        }
                    }
                    if (matchfound && pi->fattribute & AVOLUME &&
                        action != GET_INODE_STAR && action != GET_INODE_WILD)
                    {
                        /* Don't match volume labels if we are finding a specific match. */
                        matchfound = FALSE;
                    }
                    if (matchfound)
                    {
                        /* We found it   */
                        /* See if it already exists in the inode list.
                        If so.. we use the copy from the inode list */
                        pfi = pc_scani(pobj->pdrive, pd->my_block, pd->my_index);

                        if (pfi)
                        {
                            pc_freei(pobj->finode);
                            pobj->finode = pfi;
                        }
                        else    /* No inode in the inode list. Copy the data over
                                and mark where it came from */
                        {
                            pfi = pc_alloci();
                            if (pfi)
                            {
                                pc_freei(pobj->finode); /* Release the current */
                                pobj->finode = pfi;
                                pc_dos2inode(pobj->finode , pi );
                                pc_marki(pobj->finode , pobj->pdrive , pd->my_block,
                                        pd->my_index );
                                if (s.nsegs)
                                {
                                    if (pc_cksum( (byte*) pi ) != s.ncksum)
                                    {
                                        pc_zeroseglist(&s);
                                        lastsegorder = 0;
                                    }
                                    pobj->finode->s = s;
                                }
                                else
                                    pc_zeroseglist(&pobj->finode->s);
                            }
                            else
                            {
                                goto cleanup_and_fail;
                            }
                        }
                        /* Free, no error   */
                         if (scan_buff)
                            pc_release_buf(scan_buff);
                        if (puser_buffer)
                            pc_release_user_buffer(pobj->pdrive, puser_buffer);
                         pc_free_scratch_blk(scratch);
                        return (TRUE);
                    }                   /* if (match) */
                    else /* disconnect chain... segments do not match */
                    {
                        pc_zeroseglist(&s);
                    }
                }               /* else (CHICAGO_EXT) */
            }                   /* if (!PCDELETE) */
            pd->my_index++;
            pi++;
        }
        entries_processed += pobj->pdrive->drive_info.inopblock;
        if (entries_processed >= RTFS_CFG_MAX_DIRENTS)
        {
            rtfs_set_errno(PEINVALIDDIR, __FILE__, __LINE__);
            goto cleanup_and_fail;
        }

        pd->my_index = 0;
        blocks_in_buffer -= 1;
        if(blocks_in_buffer)
            pobj->blkinfo.my_block += 1;
        else
        {
            if (!pc_multi_dir_get(pobj, pscan_buff, &pscan_data, puser_buffer, &blocks_in_buffer, TRUE))
                goto cleanup_and_fail;
            if (!blocks_in_buffer)
                still_searching = FALSE;
            else
            {
                user_buffer_first_block = pobj->blkinfo.my_block;
                user_buffer_n_blocks = blocks_in_buffer;
                pi = (DOSINODE *) pc_data_in_ubuff(pobj->pdrive, user_buffer_first_block, pscan_data, user_buffer_first_block,user_buffer_n_blocks);
            }
        }
    }
    if (!get_errno())
        rtfs_set_errno(PENOENT, __FILE__, __LINE__);
cleanup_and_fail:
    if (puser_buffer)
        pc_release_user_buffer(pobj->pdrive, puser_buffer);
    if (scan_buff)
        pc_release_buf(scan_buff);
    if (scratch)
        pc_free_scratch_blk(scratch);
    return (FALSE);
}

/****************************************************************************
PC_NIBBLEPARSE -  Nibble off the left most part of a pathspec

  Description
  Take a pathspec (no leading D:). and parse the left most element into
  filename and files ext. (SPACE right filled.).

    Returns
    Returns a pointer to the rest of the path specifier beyond file.ext
    ****************************************************************************/
    /* Parse a path. Return NULL if problems or a pointer to the next */
byte *pc_nibbleparse(byte *filename, byte *fileext, byte *path, int use_charset)     /* __fn__*/
{
    byte *p;
    byte *t = filename;

    RTFS_ARGSUSED_PVOID((void *)fileext);


    p = path;

    if (!p)  /* Path must exist */
        return (0);
    while (CS_OP_IS_NOT_EOS(p, use_charset))
    {
        if (CS_OP_CMP_ASCII(p,'\\', use_charset))
        {
            CS_OP_INC_PTR(p, use_charset);
			/* if there are any double backslashes in the path then following line will catch the error */
			if(CS_OP_IS_EOS(p, use_charset))
				return (0);
            break;
        }
        else
        {
            CS_OP_CP_CHR(t, p, use_charset);
            CS_OP_INC_PTR(t, use_charset);
            CS_OP_INC_PTR(p, use_charset);

            /* Added november 2009. Guard against the argument being larger then will fit into the filename.
              The file name uses a sector buffer for storage so limit the length to RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES */
            if(t >= filename + RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES)
            	return (0);
        }
    }
    CS_OP_TERM_STRING(t, use_charset);

    return (p);
}


/*****************************************************************************
PC_CKSUM - Compute checksum on string

  Description
  Computes a single UTINY checksum based on the first 11 characters in
  the input string (test).  This value is used to link LFNINODE's on the
  disk directory table.
  Returns
  The value of the checksum
*****************************************************************************/

byte pc_cksum(byte *test)   /*__fn__*/
{
    byte  sum,i;

    for (sum = i = 0; i < 11; i++)
    {
        sum = (byte)((((sum & 0x01)<<7)|((sum & 0xFE)>>1)) + test[i]);
    }

    return(sum);
}

static byte *lfi2text(byte *lfn, int *current_lfn_length, LFNINODE *lfi, int nsegs, int use_charset) /* __fn__ */
{
    int n;
    byte *pfi;

    for (;nsegs;nsegs--, lfi--)
    {
        pfi = (byte *) lfi->lfname1;
        for(n=0; n<10; n += 2,pfi+=2)
        {
            if((*pfi==0x00)&&(*(pfi+1)==0x00))
                goto  lfi2text_eos;
            if (*current_lfn_length == FILENAMESIZE_CHARS)
                 return(0);
            CS_OP_LFI_TO_TXT(lfn, pfi, use_charset);
            CS_OP_INC_PTR(lfn, use_charset);
            *current_lfn_length += 1;
        }
        pfi = (byte *) lfi->lfname2;
        for(n=0; n<12; n += 2,pfi+=2)
        {
            if((*pfi==0x00)&&(*(pfi+1)==0x00))
                goto  lfi2text_eos;
            if (*current_lfn_length == FILENAMESIZE_CHARS)
                 return(0);
            CS_OP_LFI_TO_TXT(lfn, pfi, use_charset);
            CS_OP_INC_PTR(lfn, use_charset);
            *current_lfn_length += 1;
        }
        pfi = (byte *) lfi->lfname3;
        for(n=0; n<4; n += 2,pfi+=2)
        {
            if((*pfi==0x00)&&(*(pfi+1)==0x00))
                goto  lfi2text_eos;
            if (*current_lfn_length == FILENAMESIZE_CHARS)
                 return(0);
            CS_OP_LFI_TO_TXT(lfn, pfi, use_charset);
            CS_OP_INC_PTR(lfn, use_charset);
            *current_lfn_length += 1;
        }
        CS_OP_TERM_STRING(lfn, use_charset);
    }
    return(lfn);
lfi2text_eos:
    CS_OP_TERM_STRING(lfn, use_charset);
    return(lfn);
}

static byte *pc_seglist2text(DDRIVE * pdrive, SEGDESC *s, byte *lfn, int use_charset) /* __fn__ */
{
BLKBUFF *rbuf;
LFNINODE *lfi;
byte *p;
int ntodo_0,ntodo_1,ntodo_2;
int current_lfn_length;

    CS_OP_TERM_STRING(lfn, use_charset);
    p = lfn;
    if (!s->nsegs)
        goto sl2_done;
    /* Read segblock[0] and copy text   */
    rbuf = pc_read_blk(pdrive, s->segblock[0]);
    if (!rbuf)
        goto sl2_done;
    lfi = (LFNINODE *) rbuf->data;
    lfi += s->segindex;
    /* If the lfn segments span two or blocks then segblock[0] contains the
       last block and segblock[1] contains next to last and if their are
       three segblock[2] contains the first. */
    if (s->nsegs > s->segindex+1)
        ntodo_0 = s->segindex+1;
    else
        ntodo_0 = s->nsegs;
    current_lfn_length = 0;
    p = lfi2text(p, &current_lfn_length, lfi, ntodo_0, use_charset);
    pc_release_buf(rbuf);
    if (!p)
        return(0);
    if (s->segblock[1])
    {
        rbuf = pc_read_blk(pdrive, s->segblock[1]);
        if (!rbuf)
            goto sl2_done;
        lfi = (LFNINODE *) rbuf->data;
        lfi += (pdrive->drive_info.inopblock-1);  /* The last index */
        /* Read the next N segments. Clip it at 16 since there are only
           16 segments per block */
        ntodo_1 = s->nsegs - ntodo_0;
        if (ntodo_1 > pdrive->drive_info.inopblock)
            ntodo_1 = pdrive->drive_info.inopblock;
        if (ntodo_1)
            p = lfi2text(p, &current_lfn_length, lfi, ntodo_1, use_charset);
        pc_release_buf(rbuf);
        if (!p)
            return(0);

        if (s->segblock[2])
        {
            rbuf = pc_read_blk(pdrive, s->segblock[2]);
            if (!rbuf)
                goto sl2_done;
            lfi = (LFNINODE *) rbuf->data;
            lfi += (pdrive->drive_info.inopblock-1);  /* The last index */
        /* Read the next N segments. Clip it at 16 since there are only
           16 segments per block */
            ntodo_2 = s->nsegs - (ntodo_1 + ntodo_0);
            if (ntodo_2 > pdrive->drive_info.inopblock)
                ntodo_2 = pdrive->drive_info.inopblock;
            if (ntodo_2)
                p = lfi2text(p, &current_lfn_length, lfi, ntodo_2, use_charset);
            pc_release_buf(rbuf);
            if (!p)
                return(0);
        }
    }
sl2_done:
    return(lfn);
}

void pc_zeroseglist(SEGDESC *s)  /* __fn__ */
{
/* Note: we do not zero the checksum field here   */
    s->nsegs = 0;
    s->segblock[0] =
    s->segblock[1] =
    s->segblock[2] =
    s->segindex = 0;
}
void pc_addtoseglist(SEGDESC *s, dword my_block, int my_index) /*__fn__*/
{
    s->nsegs += 1;
    /* The block list is a LIFO stack so if it's empty start it
       otherwise ripple copy in */
    if (!s->segblock[0])
    {
        s->segblock[0] = my_block;
    }
    else if ( s->segblock[0] != my_block &&
              s->segblock[1] != my_block &&
              s->segblock[2] != my_block)
    {
        s->segblock[2] = s->segblock[1];
        s->segblock[1] = s->segblock[0];
        s->segblock[0] = my_block;
    }
    s->segindex = my_index;
}


/****************************************************************************
PC_PARSEPATH -  Parse a path specifier into path,file,ext

  Description
  Take a path specifier in path and break it into three null terminated
  strings topath,filename and file ext.
  The result pointers must contain enough storage to hold the results.
  Filename and fileext are BLANK filled to [8,3] spaces.

    Rules:

      SPEC                                        PATH            FILE                EXT
      B:JOE                                       B:              'JOE        '   '   '
      B:\JOE                                  B:\             'JOE        '   '   '
      B:\DIR\JOE                          B:\DIR      'JOE        '   '   '
      B:DIR\JOE                               B:DIR           'JOE        '   '   '
      Returns
      Returns TRUE.


 ****************************************************************************/
BOOLEAN pc_parsepath(byte *topath, byte *filename, byte *fileext, byte *path, int use_charset) /*__fn__*/
{
    int i,keep_slash;
    byte *pfile,*pslash,*pcolon,*p,*pto,*pfilespace;
    RTFS_ARGSUSED_PVOID((void *)fileext);

    /* Check the path length, compare it EMAXPATH_CHARS (255) the
       maximum filename length for VFAT */
    if (rtfs_cs_strlen(path, use_charset) > EMAXPATH_CHARS)
        return(FALSE);

    pslash = pfile = 0;
    p = path;
    pcolon = 0;
    keep_slash = 0;
    /* if A:\ or \ only keep slash */
    i = 0;
    while (CS_OP_IS_NOT_EOS(p, use_charset))
    {
        if (CS_OP_CMP_ASCII(p,'\\', use_charset))
            pslash = p;
        else if (CS_OP_CMP_ASCII(p,':', use_charset))
        {
            if (i != 1)     /* A: B: C: .. x: y: z: only */
                return (FALSE);
            pcolon = p;
        }
        CS_OP_INC_PTR(p, use_charset);
        i++;
    }
    if (pslash == path)
        keep_slash = 1;
    else if (pcolon && pslash)
    {
        CS_OP_INC_PTR(pcolon, use_charset);
        if (pslash == pcolon)
            keep_slash = 1;
    }


    p = path;
    /* Find the file section, after the colon or last backslash */
    while (CS_OP_IS_NOT_EOS(p, use_charset))
    {
        if (CS_OP_CMP_ASCII(p,'\\', use_charset) ||  CS_OP_CMP_ASCII(p,':', use_charset) )
        {
            CS_OP_INC_PTR(p, use_charset);
            pfile = p;
        }
        else
        {
            CS_OP_INC_PTR(p, use_charset);
        }
    }

    /* Now copy the path. Up to the file or NULL if no file */
    pto = topath;
    if (pfile)
    {
        p = path;
        while (p < pfile)
        {
            /* Don't put slash on the end if more than one */
            if (p == pslash && !keep_slash)
                break;
            CS_OP_CP_CHR(pto, p, use_charset);
            CS_OP_INC_PTR(pto, use_charset);
            CS_OP_INC_PTR(p, use_charset);
        }
    }
    CS_OP_TERM_STRING(pto, use_charset);
    /* Now copy the file portion or the whole path to file if no path portion */
    pto = filename;
    if (pfile)
        p = pfile;
    else
        p = path;

   /* April 2012 Fixed bug. Removed source code that stripped leading white space */
    /* Check the file length */
    if (rtfs_cs_strlen(p, use_charset) > FILENAMESIZE_CHARS)
        return(FALSE);

    pfilespace = 0;
    while (CS_OP_IS_NOT_EOS(p, use_charset))
    {
        CS_OP_CP_CHR(pto, p, use_charset);
        CS_OP_INC_PTR(p, use_charset);
        if (CS_OP_CMP_ASCII(pto,' ', use_charset))
        {
            if (!pfilespace)
                pfilespace = pto;
        }
        else
            pfilespace = 0;
        CS_OP_INC_PTR(pto, use_charset);
    }
    /* If the trailing character is a space NULL terminate */
    if (pfilespace)
    {
        {CS_OP_TERM_STRING(pfilespace, use_charset);}
    }
    else
        CS_OP_TERM_STRING(pto, use_charset);
    return(TRUE);
}
/******************************************************************************
PC_PATCMP  - Compare a pattern with a string

  Description
  Compare size bytes of p against pattern. Applying the following rules.
  If size == 8.
  (To handle the way dos handles deleted files)
  if p[0] = DELETED, never match
  if pattern[0] == DELETED, match with 0x5

    '?' in pattern always matches the current char in p.
    '*' in pattern always matches the rest of p.
    Returns
    Returns TRUE if they match

****************************************************************************/
BOOLEAN pc_patcmp_vfat(byte *in_pat, byte *name, BOOLEAN dowildcard, int use_charset)    /*__fn__*/
{
    byte *pat, *p, *pp, *pn, *pp2, *pn2;
    byte star[4];
    BOOLEAN res = FALSE;

    /* Convert *.* to just * */
    p = pat = in_pat;
    if (dowildcard && CS_OP_CMP_ASCII(p,'*', use_charset))
    {
        CS_OP_INC_PTR(p, use_charset);
        if (CS_OP_CMP_ASCII(p,'.', use_charset))
        {
            CS_OP_INC_PTR(p, use_charset);
            if (CS_OP_CMP_ASCII(p,'*', use_charset))
            {
                CS_OP_INC_PTR(p, use_charset);
                if (CS_OP_IS_EOS(p, use_charset))
                {
                    /* Change *.* to * but since the argument may have been
                       const we do it in a private buffer */
                    p = pat = star;
                    CS_OP_ASSIGN_ASCII(p,'*', use_charset);
                    CS_OP_INC_PTR(p, use_charset);
                    CS_OP_TERM_STRING(p, use_charset);
                }
            }
        }
    }
    /* * matches everything */
    p = pat;
    if (dowildcard && CS_OP_CMP_ASCII(p,'*', use_charset))
    {
        CS_OP_INC_PTR(p, use_charset);
        if (CS_OP_IS_EOS(p, use_charset))
            return(TRUE);
    }

    for (pp=pat,pn=name;CS_OP_IS_NOT_EOS(pp, use_charset); CS_OP_INC_PTR(pn, use_charset),CS_OP_INC_PTR(pp, use_charset))
    {
        if(CS_OP_CMP_ASCII(pp,'*', use_charset) && dowildcard)
        {
            pp2 = pp;
            CS_OP_INC_PTR(pp2, use_charset);
            if (CS_OP_IS_EOS(pp2, use_charset))
                return(TRUE); /* '*' at end */
            pn2 = pn;
            /* We hit star. Now go past it and see if there is another
            exact match. IE: a*YYY matches abcdefgYYY but not abcdefgXXX */
            for (;!res && CS_OP_IS_NOT_EOS(pn2, use_charset); CS_OP_INC_PTR(pn2, use_charset))
            {
                res = res || pc_patcmp_vfat(pp2,pn2,TRUE,use_charset);
            }
            return(res);
        }

        else if (CS_OP_CMP_CHAR(pp, pn, use_charset))
            ;
        else if (CS_OP_CMP_ASCII(pp,'?', use_charset) && dowildcard)
            ;
        else if (CS_OP_CMP_CHAR_NC(pp, pn, use_charset))
            ;
        else
            return(FALSE);
    }
    if(CS_OP_IS_EOS(pn, use_charset))
        return(TRUE);
    else
        return(FALSE);
}

static BOOLEAN pc_patcmp_vfat_8_3(byte *pat, byte *name, BOOLEAN dowildcard, int use_charset)    /*__fn__*/
{
    byte save_char;
    BOOLEAN ret_val;
    if (CS_OP_CMP_ASCII(name,PCDELETE, use_charset))
        return (FALSE);
    save_char = *name;
    if (*name == 0x05)
        *name = 0xe5;
    ret_val = pc_patcmp_vfat(pat, name, dowildcard,use_charset);
    *name = save_char;
    return(ret_val);
}

/* Byte oriented */
static BOOLEAN pc_allspace(byte *p, int i)                                                             /* __fn__*/
{while (i--) if (*p++ != ' ') return (FALSE);   return (TRUE); }

BOOLEAN pc_isdot(byte *fname, byte *fext)                                               /* __fn__*/
{
    RTFS_ARGSUSED_PVOID((void *)fext);
    return (BOOLEAN)((*fname == '.') &&
        ((*(fname+1) == '\0') || (pc_allspace((fname+1),10))) );
}
BOOLEAN pc_isdotdot(byte *fname, byte *fext)                                            /* __fn__*/
{
    RTFS_ARGSUSED_PVOID((void *)fext);
    return (BOOLEAN)( (*fname == '.') && (*(fname+1) == '.') &&
        ((*(fname+2) == '\0') || (pc_allspace((fname+2),9)) ) );
}

BOOLEAN pc_get_lfn_filename(DROBJ *pobj, byte *path, int use_charset)
{
#if (INCLUDE_EXFATORFAT64)
 	if (ISEXFATORFAT64(pobj->pdrive))
	{
 		if (pcexfat_seglist2text(pobj->pdrive, &pobj->finode->s, path, use_charset))
			return(TRUE);
		else
			return(FALSE);
	}
#endif
   if (pobj->finode->s.nsegs)
   {
      if (pc_seglist2text(pobj->pdrive, &pobj->finode->s, path, use_charset))
          return(TRUE);
      else
          return(FALSE);
    }
    else
      return(FALSE);
}


BOOLEAN pc_multi_dir_get(DROBJ *pobj, BLKBUFF **pscan_buff, byte **pscan_data, byte *puser_buffer, dword *n_blocks, BOOLEAN do_increment)
{
    dword cluster;
    int end_of_chain;

    *n_blocks = 0;

    /* If the block is in the root area   */
    if (pobj->blkinfo.my_block < pobj->pdrive->drive_info.firstclblock)
    {
        if (do_increment)
        {
            if (pobj->blkinfo.my_block < pobj->pdrive->drive_info.rootblock)
            {
                rtfs_set_errno(PEINTERNAL, __FILE__, __LINE__);
                return (FALSE);
            }
            else if (pobj->blkinfo.my_block+1 < pobj->pdrive->drive_info.firstclblock)
            {
                pobj->blkinfo.my_block += 1;
            }
            else
            {
                *n_blocks = 0;
                return(TRUE);
            }
        }
        /* Don't read more than 1 cluster because the alias map resides just after it in the user buffer */
        *n_blocks = pobj->pdrive->drive_info.firstclblock - pobj->blkinfo.my_block;
        if (*n_blocks > pobj->pdrive->drive_info.secpalloc)
            *n_blocks = pobj->pdrive->drive_info.secpalloc;
    }
    else  /* In cluster space   */
    {
    dword index_offset;
        if (do_increment)
        {
            if (pobj->blkinfo.my_block >= pobj->pdrive->drive_info.numsecs)
            {
                rtfs_set_errno(PEINVALIDCLUSTER, __FILE__, __LINE__);
                return (FALSE);
            }
            /* Get the next block   */
            pobj->blkinfo.my_block += 1;
            /* If the next block is not on a cluster edge then it must be
                in the same cluster as the current. - otherwise we have to
                get the firt block from the next cluster in the chain */
            index_offset = pc_sec2index(pobj->pdrive, pobj->blkinfo.my_block);
            if (index_offset)
                *n_blocks = pobj->pdrive->drive_info.secpalloc - index_offset;
            else
            { /* Start at a new cluster boundary */
                pobj->blkinfo.my_block -= 1;  /* Get the old cluster number */
                cluster = pc_sec2cluster(pobj->pdrive,pobj->blkinfo.my_block);
                if ((cluster < 2) || (cluster > pobj->pdrive->drive_info.maxfindex) )
                {
                    rtfs_set_errno(PEINVALIDCLUSTER, __FILE__, __LINE__);
                    return (FALSE);
                }
                /* Consult the fat for the next cluster. Will return 2
                    (this plus next if any are left */

#if (INCLUDE_EXFATORFAT64)
                /* Force it to use the FAT buffer pool by passing alt buffer and size as zero */
				if (ISEXFATORFAT64(pobj->pdrive))
				{
					if (exFatfatop_getdir_frag(pobj, cluster, &cluster, 1, &end_of_chain) == 1 && !end_of_chain)
					{
						pobj->blkinfo.my_block = pc_cl2sector(pobj->pdrive, cluster);
						*n_blocks = pobj->pdrive->drive_info.secpalloc;
					}
				}
				else
#endif
				{
                	if (fatop_get_frag(pobj->pdrive, 0, 0, cluster, &cluster, 1, &end_of_chain) == 1 && !end_of_chain)
                	{
                    	pobj->blkinfo.my_block = pc_cl2sector(pobj->pdrive, cluster);
                    	*n_blocks = pobj->pdrive->drive_info.secpalloc;
                	}
				}
            }
        }
        else
        { /* Didn't change, just read to end of cluster */
            index_offset = pc_sec2index(pobj->pdrive, pobj->blkinfo.my_block);
            *n_blocks = pobj->pdrive->drive_info.secpalloc - index_offset;
        }
    }
    if (pscan_buff)
    {
        if (*pscan_buff)  /* If we're already processing one, release it */
        {
           pc_release_buf(*pscan_buff);
           *pscan_buff = 0;
        }
        if (*n_blocks)
        {
            *n_blocks = 1;
            *pscan_buff = pc_read_blk(pobj->pdrive, pobj->blkinfo.my_block);
            if (!*pscan_buff)
                return(FALSE);
            *pscan_data = (*pscan_buff)->data;
            return(TRUE);
        }
    }
    else
    {
        *pscan_data = puser_buffer;
        /* *n_blocks will be zero or a value <=  pobj->pdrive->drive_info.secpalloc */
        if (*n_blocks)
		{
            return(block_devio_xfer(pobj->pdrive, pobj->blkinfo.my_block, puser_buffer, *n_blocks, TRUE));
		}
    }
    return(TRUE);
}

byte *pc_data_in_ubuff(DDRIVE *pdr, dword blockno,byte *puser_buffer, dword user_buffer_first_block,dword user_buffer_n_blocks)
{
dword user_buffer_last_block;
byte *pdata = 0;
    user_buffer_last_block = user_buffer_first_block + user_buffer_n_blocks;
    if (blockno >= user_buffer_first_block && blockno < user_buffer_last_block)
    {
        pdata =  puser_buffer;
        pdata += (blockno - user_buffer_first_block) * pdr->drive_info.bytespsector;

    }
    return(pdata);
}

static byte *pc_multiseglist2text(DDRIVE *pdrive, SEGDESC *s, byte *lfn, byte *pscan_data, dword user_buffer_first_block,dword user_buffer_n_blocks, int use_charset) /* __fn__ */
{
BLKBUFF *rbuf;
LFNINODE *lfi;
byte *p;
int ntodo_0,ntodo_1,ntodo_2;
int current_lfn_length;

    CS_OP_TERM_STRING(lfn, use_charset);
    p = lfn;
    if (!s->nsegs)
        goto sl2_done;

    rbuf = 0;
    lfi = (LFNINODE *) pc_data_in_ubuff(pdrive, s->segblock[0], pscan_data, user_buffer_first_block,user_buffer_n_blocks);
    if (!lfi)
    {
        /* Read segblock[0] and copy text   */
        rbuf = pc_read_blk(pdrive, s->segblock[0]);
        if (!rbuf)
            goto sl2_done;
        lfi = (LFNINODE *) rbuf->data;
    }
    lfi += s->segindex;
    /* If the lfn segments span two or blocks then segblock[0] contains the
       last block and segblock[1] contains next to last and if their are
       three segblock[2] contains the first. */
    if (s->nsegs > s->segindex+1)
        ntodo_0 = s->segindex+1;
    else
        ntodo_0 = s->nsegs;
    current_lfn_length = 0;
    p = lfi2text(p, &current_lfn_length, lfi, ntodo_0, use_charset);
    if (rbuf)
        pc_release_buf(rbuf);
    if (!p)
        return(0);
    if (s->segblock[1])
    {
        rbuf = 0;
        lfi = (LFNINODE *) pc_data_in_ubuff(pdrive, s->segblock[1], pscan_data, user_buffer_first_block,user_buffer_n_blocks);
        if (!lfi)
        {
            rbuf = pc_read_blk(pdrive, s->segblock[1]);
            if (!rbuf)
                goto sl2_done;
            lfi = (LFNINODE *) rbuf->data;
        }
        lfi += (pdrive->drive_info.inopblock-1);  /* The last index */
        /* Read the next N segments. Clip it at 16 since there are only
           16 segments per block */
        ntodo_1 = s->nsegs - ntodo_0;
        if (ntodo_1 > pdrive->drive_info.inopblock)
            ntodo_1 = pdrive->drive_info.inopblock;
        if (ntodo_1)
            p = lfi2text(p, &current_lfn_length, lfi, ntodo_1, use_charset);
        if (rbuf)
            pc_release_buf(rbuf);
        if (!p)
            return(0);

        if (s->segblock[2])
        {
            rbuf = 0;
            lfi = (LFNINODE *) pc_data_in_ubuff(pdrive, s->segblock[2], pscan_data,user_buffer_first_block,user_buffer_n_blocks);
            if (!lfi)
            {
                rbuf = pc_read_blk(pdrive, s->segblock[2]);
                if (!rbuf)
                    goto sl2_done;
                lfi = (LFNINODE *) rbuf->data;
            }
            lfi += (pdrive->drive_info.inopblock-1);  /* The last index */
        /* Read the next N segments. Clip it at 16 since there are only
           16 segments per block */
            ntodo_2 = s->nsegs - (ntodo_1 + ntodo_0);
            if (ntodo_2 > pdrive->drive_info.inopblock)
                ntodo_2 = pdrive->drive_info.inopblock;
            if (ntodo_2)
                p = lfi2text(p, &current_lfn_length, lfi, ntodo_2, use_charset);
            if (rbuf)
                pc_release_buf(rbuf);
            if (!p)
                return(0);
        }
    }
sl2_done:
    return(lfn);
}

#if (INCLUDE_REVERSEDIR)
/*
static BOOLEAN pc_vfatrfind( DROBJ *pobj, byte *filename, byte *fileext, int action, int use_charset)
	If just starting find last segment - returns segment list and segment|index of the last entry in a directory.
	else  - given segment list and segment|index of the last entry, return the segment list and segment|index of the previous entry in a directory.
*/
static BOOLEAN pc_vfatrfindin( DROBJ *pobj, byte *filename, int action, int use_charset, BOOLEAN starting);

BOOLEAN pc_rfindin( DROBJ *pobj, byte *filename, int action, int use_charset, BOOLEAN starting)          /*__fn__*/
{
	BOOLEAN retval;
#if (INCLUDE_EXFATORFAT64)
	if (ISEXFATORFAT64(pobj->pdrive))
		retval= pc_exfatrfindin(pobj, filename, action, use_charset, starting);
	else
#endif
		retval= pc_vfatrfindin(pobj, filename, action, use_charset, starting);
	return(retval);

}

static BOOLEAN pc_vfatrfindpreventry( DROBJ *pobj,BLKBUFF **pscan_buff);
static BOOLEAN pc_vfatrfindendentry( DROBJ *pobj,BLKBUFF **pscan_buff);

static BOOLEAN pc_vfatrfindin( DROBJ *pobj, byte *filename, int action, int use_charset, BOOLEAN starting)          /*__fn__*/
{
    dword user_buffer_first_block,user_buffer_n_blocks,blocks_in_buffer,entries_processed;
    DIRBLK *pd;
    DOSINODE *pi;
    FINODE *pfi;
    BOOLEAN matchfound;
    BOOLEAN dowildcard, still_searching,still_searching_segment;
    BLKBUFF *scratch;
    byte *lfn;
    byte sfn[26]; /* Leave room in case unicode */
    LFNINODE *lfn_node;
    SEGDESC s;
    byte lastsegorder;
    BLKBUFF *scan_buff, **pscan_buff; /* If using block buffers for scan */
    byte *p,*pscan_data;
    entries_processed = 0;
    scan_buff = 0;
    pscan_buff = &scan_buff;        /* Assume using buffer pool */

    rtfs_clear_errno();  /* Clear it here just in case */
    scratch = pc_scratch_blk();
    if (!scratch)
        goto cleanup_and_fail;
    lfn = (byte *)scratch->data;

    pc_zeroseglist(&s);

    if (action == GET_INODE_WILD)
        dowildcard = TRUE;
    else
    {
        dowildcard = FALSE;
    }
    /* For convenience. We want to get at block info here   */
    pd = &pobj->blkinfo;

    still_searching = TRUE;
	while (still_searching)
	{
	dword current_block;
	int   current_index;
		if (starting)
		{
			/* seek to end and set pd->my_block	pd->my_index */
			if (! pc_vfatrfindendentry(pobj,pscan_buff))
        		goto cleanup_and_fail;
		}
		else
		{
			/* seek to the previous entry */
			if (! pc_vfatrfindpreventry(pobj,pscan_buff))
        		goto cleanup_and_fail;
		}
		starting = FALSE;

       	current_block = pd->my_block;  /* Remember the start point so we can walk backwards from here */
       	current_index = pd->my_index;
		/* Read the data   */
		if (!pc_multi_dir_get(pobj, pscan_buff, &pscan_data, 0, &blocks_in_buffer, FALSE))
        	goto cleanup_and_fail;
       	user_buffer_first_block = pobj->blkinfo.my_block;
       	user_buffer_n_blocks = blocks_in_buffer;
       	if (!blocks_in_buffer)
       	{
        	still_searching = FALSE;
        	still_searching_segment = FALSE;
        	pi = 0;
       	}
       	else
       	{
        	pi = (DOSINODE *) pc_data_in_ubuff(pobj->pdrive, user_buffer_first_block, pscan_data, user_buffer_first_block,user_buffer_n_blocks);
        	still_searching_segment = TRUE;
       	}

       	lastsegorder = 0;
       	while (still_searching_segment)
       	{
        	/* Look at the current inode   */
        	pi += pd->my_index;
        	/* And look for a match   */
        	while ( still_searching_segment && pd->my_index < pobj->pdrive->drive_info.inopblock )
        	{
            	matchfound = FALSE;
            	/* End of dir if name is 0  or 8 x (0xff)  */
            	if (pc_check_dir_end(pi->fname) || pi->fname[0] == PCDELETE)
            	{
                	rtfs_set_errno(PENOENT, __FILE__, __LINE__);
                	goto cleanup_and_fail;
            	}
               	/* Support long file names   */
               	if (pi->fattribute == CHICAGO_EXT)
               	{
                   	lfn_node = (LFNINODE *) pi;
                   	if (lfn_node->lfnorder & FIRST_NAMESEG)
                   	{
                       	pc_addtoseglist(&s, pd->my_block, pd->my_index);
                       	lastsegorder = lfn_node->lfnorder;
                       	s.ncksum = lfn_node->lfncksum;
                   	}
                   	else
                   	{
                       	if (s.nsegs)/* if a chain already exists */
                       	{
                           	if ( ((lfn_node->lfnorder & NAMESEG_ORDER) == (lastsegorder & NAMESEG_ORDER) - 1) && (lfn_node->lfncksum == s.ncksum) )
                           	{
                               	lastsegorder = lfn_node->lfnorder;
                               	pc_addtoseglist(&s, pd->my_block, pd->my_index);
                           	}
                           	else
                           	{
                           		/* disconnect chain... segments do not match   */
                           		lastsegorder = 0;
                           		pc_zeroseglist(&s);
                           	}
                      	}
                   	}
               	}
                else
                {
                	/* Note: Patcmp wo not match on deleted   */
                	if (s.nsegs)
                	{
                       	if (action == GET_INODE_STAR)
                           	matchfound = TRUE;
                        else
                        {
                          	p = (byte*)pc_multiseglist2text(pobj->pdrive, &s,lfn, pscan_data, user_buffer_first_block,user_buffer_n_blocks, use_charset);
                           	if (!p)
                               	matchfound = FALSE; /* 8.3, not lfn */
                          	else
                          		matchfound = pc_patcmp_vfat(filename, p ,dowildcard, use_charset);
                		}
                	}
                	else
                       	matchfound = FALSE; /* 8.3, not lfn */
                    if (matchfound)
                       	matchfound = (BOOLEAN)(pc_cksum( (byte*) pi ) == s.ncksum);
                    else
                    {
                       	if (action == GET_INODE_STAR)
                           	matchfound = TRUE;
                       	else
                       	{ /* Make native charset name from the short file and try for a match.
                             	Done in native charset because no LFN entry is created if the
                             	name is valid 8.3, even with Unicode */
                           	pc_cs_mfile(sfn,pi->fname,pi->fext, use_charset);
                           	matchfound = pc_patcmp_vfat_8_3(filename, sfn, dowildcard, use_charset);
                       	}
                    }
                    if (matchfound && pi->fattribute & AVOLUME && action != GET_INODE_STAR && action != GET_INODE_WILD)
                    {
                        /* Don't match volume labels if we are finding a specific match. */
                        matchfound = FALSE;
                    }
                    if (matchfound)
                    {
                        /* We found it   */
                        /* See if it already exists in the inode list.
                        If so.. we use the copy from the inode list */
                        pfi = pc_scani(pobj->pdrive, pd->my_block, pd->my_index);

                        if (pfi)
                        {
                            pc_freei(pobj->finode);
                            pobj->finode = pfi;
                        }
                        else    /* No inode in the inode list. Copy the data over  and mark where it came from */
                        {
                            pfi = pc_alloci();
                            if (pfi)
                            {
                                pc_freei(pobj->finode); /* Release the current */
                                pobj->finode = pfi;
                                pc_dos2inode(pobj->finode , pi );
                                pc_marki(pobj->finode , pobj->pdrive , pd->my_block,
                                        pd->my_index );
                                if (s.nsegs)
                                {
                                    if (pc_cksum( (byte*) pi ) != s.ncksum)
                                    {
                                        pc_zeroseglist(&s);
                                        lastsegorder = 0;
                                    }
                                    pobj->finode->s = s;
                                }
                                else
                                    pc_zeroseglist(&pobj->finode->s);
                            }
                            else
                            {
                                goto cleanup_and_fail;
                            }
                        }
                        /* Free, no error   */
                         if (scan_buff)
                            pc_release_buf(scan_buff);
                         pc_free_scratch_blk(scratch);
                        return (TRUE);
                    }                   /* if (match) */
                    else /* disconnect chain... segments do not match */
                    {
                        pc_zeroseglist(&s);
                        pd->my_block = current_block;
                        pd->my_index = current_index;
                        still_searching_segment = FALSE;
                    }
                }  /* else (CHICAGO_EXT) */
				if (still_searching_segment)
				{
					pd->my_index++;
					pi++;
				}
        	}
            entries_processed += pobj->pdrive->drive_info.inopblock;
            if (entries_processed >= RTFS_CFG_MAX_DIRENTS)
            {
            	rtfs_set_errno(PEINVALIDDIR, __FILE__, __LINE__);
                goto cleanup_and_fail;
            }
            if (still_searching_segment)
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
                		pi = (DOSINODE *) pc_data_in_ubuff(pobj->pdrive, user_buffer_first_block, pscan_data, user_buffer_first_block,user_buffer_n_blocks);
                	}
            	}
			}
		}
    } /* while (still_searching) */
    if (!get_errno())
        rtfs_set_errno(PENOENT, __FILE__, __LINE__);
cleanup_and_fail:
    if (scan_buff)
        pc_release_buf(scan_buff);
    if (scratch)
        pc_free_scratch_blk(scratch);
    return (FALSE);
}

/* Scan backward and set pd->my_block:pd->my_index to the beginning of the last segment
   return FALSE and set PENOENT if no more
   return FALSE and set PEINVAL if unexpected behavior */
static BOOLEAN pc_vfatrfindendentry( DROBJ *pobj,BLKBUFF **pscan_buff)          /*__fn__*/
{
dword my_last_block,my_last_lfn_block;
int   my_last_index,my_last_lfn_index;
BOOLEAN still_searching;
dword user_buffer_first_block,user_buffer_n_blocks, blocks_in_buffer,entries_processed;
DOSINODE *pi;
DIRBLK *pd;
byte *pscan_data;

    /* For convenience. We want to get at block info here   */
    pd = &pobj->blkinfo;
	entries_processed = 0;
	my_last_block = pd->my_block;
	my_last_index = pd->my_index;
	my_last_lfn_block = 0;
	my_last_lfn_index = 0;

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
        pi = (DOSINODE *) pc_data_in_ubuff(pobj->pdrive, user_buffer_first_block, pscan_data, user_buffer_first_block,user_buffer_n_blocks);
    }
    while (still_searching)
    {
        /* Look at the current inode   */
        pi += pd->my_index;
        /* And look for a match   */
        while ( pd->my_index < pobj->pdrive->drive_info.inopblock )
        {
            /* End of dir if name is 0  or 8 x (0xff)  */
            if (pc_check_dir_end(pi->fname))
            {
            	still_searching = FALSE;
                break;
            }
            if (pi->fname[0] == PCDELETE)
			{	/* Clear long file name start */
				my_last_lfn_block = 0;
				my_last_lfn_index = 0;
			}
            else  if (pi->fattribute == CHICAGO_EXT)
			{
				if (!my_last_lfn_block)
				{	/* Remember where a long file name starts */
					my_last_lfn_block = pd->my_block;
					my_last_lfn_index = pd->my_index;
				}
			}
            else
            {
				if (my_last_lfn_block)
				{ /* If we have an extension return the start of the extension */
                	my_last_block = my_last_lfn_block;
                	my_last_index = my_last_lfn_index;
                	my_last_lfn_block = 0;
                	my_last_lfn_index = 0;
				}
				else
				{
                	my_last_block = pd->my_block;
                	my_last_index = pd->my_index;
				}
            }
            pd->my_index++;
            pi++;
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
                	pi = (DOSINODE *) pc_data_in_ubuff(pobj->pdrive, user_buffer_first_block, pscan_data, user_buffer_first_block,user_buffer_n_blocks);
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
static BOOLEAN pc_vfatprevindex( DROBJ *pobj)          /*__fn__*/
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
				next_cluster = fatop_next_cluster(pobj->pdrive, cluster);
				if (next_cluster == current_cluster)
				{	/* Last sector in the previous cluster */
					pd->my_block = pc_cl2sector(pobj->pdrive, cluster);
					pd->my_block += pobj->pdrive->drive_info.secpalloc-1;
					pd->my_index = pobj->pdrive->drive_info.inopblock-1;
					return(TRUE);
				}
				cluster = next_cluster;
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
static BOOLEAN pc_vfatrfindpreventry( DROBJ *pobj,BLKBUFF **pscan_buff)          /*__fn__*/
{
dword saved_block, my_last_block,my_last_lfn_block,my_last_file_block;
int   saved_index, my_last_index,my_last_lfn_index,my_last_file_index;
dword user_buffer_first_block,user_buffer_n_blocks, blocks_in_buffer,entries_processed;
DOSINODE *pi;
DIRBLK *pd;
byte *pscan_data;

	entries_processed = 0;

    /* For convenience. We want to get at block info here   */
    pd = &pobj->blkinfo;
	saved_block = pd->my_block;
	saved_index = pd->my_index;

	my_last_block = my_last_file_block = my_last_lfn_block = 0;
	my_last_index = my_last_file_index = my_last_lfn_index = 0;

	if (!pc_vfatprevindex(pobj))
	{
error_return:
		pd->my_block = saved_block;
		pd->my_index = saved_index;
		return(FALSE);
	}

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

       	pi = (DOSINODE *) pc_data_in_ubuff(pobj->pdrive, user_buffer_first_block, pscan_data, user_buffer_first_block,user_buffer_n_blocks);
       	pi += pd->my_index;
    	if (pi->fname[0] == PCDELETE)
		{	/* Clear long file name start */
			my_last_lfn_block = 0;
			my_last_lfn_index = 0;
			my_last_file_block = 0;
			my_last_file_index = 0;
		}
        else if (pi->fattribute == CHICAGO_EXT)
		{
			if (my_last_file_block)
			{	/* Remember where a long file name starts */
				my_last_lfn_block = pd->my_block;
				my_last_lfn_index = pd->my_index;
			}
		}
		else
		{
			if (my_last_file_block == 0)
			{
				my_last_block = my_last_file_block = pd->my_block;
				my_last_index = my_last_file_index = pd->my_index;
				my_last_lfn_block = 0;
				my_last_lfn_index = 0;
			}
			else
			{
				if (my_last_lfn_block)
				{
					my_last_block = my_last_lfn_block;
					my_last_index = my_last_lfn_index;
				}
				else
				{
					my_last_block = my_last_file_block;
					my_last_index = my_last_file_index;
				}
				/* Success */
				break;
			}
		}
		if (!pc_vfatprevindex(pobj))
			break;
	}

	if (my_last_block)
	{
    	pd->my_block = my_last_block;
    	pd->my_index = my_last_index;
		return(TRUE);
	}
	else
		goto error_return;
}

#endif

#endif
