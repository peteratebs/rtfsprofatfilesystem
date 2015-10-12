/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* RTNVFAT.C - Contains routines specific to the non-vfat implementation */

#include "rtfs.h"

#if (!INCLUDE_VFAT)

BOOLEAN rtfs_cs_patcmp_8(byte *p, byte *pattern, BOOLEAN dowildcard);
BOOLEAN rtfs_cs_patcmp_3(byte *p, byte *pattern, BOOLEAN dowildcard);

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
BOOLEAN pc_findin( DROBJ *pobj, byte *filename, byte *fileext, int action, int use_charset)          /*__fn__*/
{
    BLKBUFF *rbuf;
    DIRBLK *pd;
    DOSINODE *pi;
    FINODE *pfi;
    BOOLEAN matchfound;
    BOOLEAN dowildcard;
    dword entries_processed;

    entries_processed = 0;

    if (action == GET_INODE_WILD)
        dowildcard = TRUE;
    else
    {
        dowildcard = FALSE;
    }

    rtfs_clear_errno();  /* Clear it here just in case */
    /* For convenience. We want to get at block info here   */
    pd = &pobj->blkinfo;

    /* Read the data   */
    pobj->pblkbuff = rbuf = pc_read_blk(pobj->pdrive, pobj->blkinfo.my_block);

    while (rbuf)
    {
        pi = (DOSINODE *) rbuf->data;

        /* Look at the current inode   */
        pi += pd->my_index;

        /* And look for a match   */
        while ( pd->my_index < pobj->pdrive->drive_info.inopblock)
        {
            matchfound = FALSE;
            /* End of dir if name is 0  or 8 x (0xff)  */
            if (pc_check_dir_end(pi->fname))
            {
                rtfs_set_errno(PENOENT, __FILE__, __LINE__); /* pc_findin: file or dir not found */
                pc_release_buf(rbuf);
                return(FALSE);
            }
            if (pi->fattribute != CHICAGO_EXT  && pi->fname[0] != PCDELETE)
            {
                if (action == GET_INODE_STAR)
                    matchfound = TRUE;
                else if (action == GET_INODE_DOTDOT)
                {
                    if (pi->fname[0] == (byte)'.' && pi->fname[1] == (byte)'.')
                        matchfound = TRUE; /* 8.3, not lfn */
                }
                else
                {
                    matchfound =
                        ( rtfs_cs_patcmp_8(pi->fname, (byte*) filename, dowildcard) &&
                            rtfs_cs_patcmp_3(pi->fext,  (byte*) fileext, dowildcard ) );
                }
                if (matchfound && pi->fattribute & AVOLUME &&
                    action != GET_INODE_STAR && action != GET_INODE_WILD)
                {
                    /* Don't match volume labels if we are finding a specific match. */
                    matchfound = FALSE;
                }
            }
            if (matchfound)
            {
                /* We found it   */
                /* See if it already exists in the inode list.
                If so.. we use the copy from the inode list */
                pfi = pc_scani(pobj->pdrive, rbuf->blockno, pd->my_index);

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
                    }
                    else
                    {
                        pc_release_buf(rbuf);
                        return (FALSE);
                    }
                }
                /* Free, no error   */
                pc_release_buf(rbuf);
                return (TRUE);
            }                   /* if (match) */
            pd->my_index++;
            pi++;
        }
        /* Not in that block. Try again   */
        pc_release_buf(rbuf);
        entries_processed += pobj->pdrive->drive_info.inopblock;
        /* Check for endless loop */
        if (entries_processed >= RTFS_CFG_MAX_DIRENTS)
        {
            rtfs_set_errno(PEINVALIDDIR, __FILE__, __LINE__);
            break;
        }
        /* Update the objects block pointer   */
        if (!pc_next_block(pobj))
            break;
        pd->my_index = 0;
        pobj->pblkbuff = rbuf = pc_read_blk(pobj->pdrive, pobj->blkinfo.my_block);
    }
    RTFS_ARGSUSED_INT(use_charset);
    if (!get_errno())
        rtfs_set_errno(PENOENT, __FILE__, __LINE__); /* pc_findin: file or dir not found */
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
    BLKBUFF *scratch;
    byte *tbuf;
    byte *t;

    p = path;

    if (!p)  /* Path must exist */
        return (0);
    scratch = pc_scratch_blk();
    if (!scratch)
        return(0);
    tbuf = scratch->data;
    t = tbuf;

    while (CS_OP_IS_NOT_EOS(p , use_charset))
    {
        if (CS_OP_CMP_ASCII(p,'\\', use_charset))
        {
            CS_OP_INC_PTR(p, use_charset);
            break;
        }
        else
        {
            CS_OP_CP_CHR(t, p, use_charset);
            CS_OP_INC_PTR(t, use_charset);
            CS_OP_INC_PTR(p, use_charset);
            /* Added november 2009. Guard against the argument being larger then will fit into the filename.
              The file name uses a sector buffer for storage so limit the length to RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES */
            if(t >= tbuf + RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES)
			{
				pc_free_scratch_blk(scratch);
            	return (0);
			}
        }
    }
    CS_OP_TERM_STRING(t, use_charset);

    if (rtfs_cs_ascii_fileparse(filename, fileext, tbuf))
    {
        pc_free_scratch_blk(scratch);
        return (p);
    }
    else
    {
        pc_free_scratch_blk(scratch);
        return (0);
    }
}

BOOLEAN pc_parsepath(byte *topath, byte *filename, byte *fileext, byte *path, int use_charset) /*__fn__*/
{
    byte *pfile, *pto, *pfr, *p, *pslash, *pcolon;
    int i;

    /* Check the path length - compare the string length in bytes
       against the max path in chars. This is correct since in
       ASCII charlen==bytelen, in JIS charlen does not equal bytelen
       but we don't want the string to be > the char len */
    if (rtfs_cs_strlen(path, use_charset) > EMAXPATH_CHARS)
        return(FALSE);

    pslash = pcolon = 0;
    pfr = path;
    pto = topath;
    /* Copy input path to output keep note colon and backslash positions */

    for (i = 0; CS_OP_IS_NOT_EOS(pfr, use_charset); i++,CS_OP_INC_PTR(pfr, use_charset),CS_OP_INC_PTR(pto, use_charset))
    {
        CS_OP_CP_CHR(pto, pfr, use_charset);
        if (CS_OP_CMP_ASCII(pto,'\\', use_charset))
            pslash = pto;
        else if (CS_OP_CMP_ASCII(pto,':', use_charset))
        {
            if (i != 1)     /* A: B: C: .. x: y: z: only */
                return (FALSE);
            pcolon = pto;
            CS_OP_INC_PTR(pcolon, use_charset); /* Look one past */
        }
    }
    CS_OP_TERM_STRING(pto, use_charset);

    if (pslash)
    {
        pfile = pslash;
        CS_OP_INC_PTR(pfile, use_charset);
    }
    else if (pcolon)
        pfile = pcolon;
    else
        pfile = topath;

    if (!rtfs_cs_ascii_fileparse(filename, fileext, pfile))
        return (FALSE);
        /* Terminate path:
        If X:\ or \ leave slash on. Else zero it
    */
    p = topath; /* Default */
    if (!pslash)
    {
        if (pcolon) p = pcolon;
    }
    else /* if slash. and at 0 or right after colon leave else zero it */
    {
        p = pslash;
        /*    \         or  A:\ */
        if (p == topath || p == pcolon)
        {
            CS_OP_INC_PTR(p, use_charset); /* (leave it in path) */
        }
    }
    CS_OP_TERM_STRING(p, use_charset);
    return(TRUE);
}

static BOOLEAN pc_allspace(byte *p, int i)                                                             /* __fn__*/
{while (i--) if (*p++ != ' ') return (FALSE);   return (TRUE); }
BOOLEAN pc_isdot(byte *fname, byte *fext)                                               /* __fn__*/
{
    return (BOOLEAN)((*fname == '.') &&
        pc_allspace(fname+1,7) && pc_allspace(fext,3) );
}
BOOLEAN pc_isdotdot(byte *fname, byte *fext)                                            /* __fn__*/
{
    return (BOOLEAN)( (*fname == '.') && (*(fname+1) == '.') &&
        pc_allspace(fname+2,6) && pc_allspace(fext,3) );
}

BOOLEAN pc_get_lfn_filename(DROBJ *pobj, byte *path, int use_charset)
{
    RTFS_ARGSUSED_INT(use_charset);
    RTFS_ARGSUSED_PVOID((void *) pobj);
    RTFS_ARGSUSED_PVOID((void *) path);
    return(FALSE);
}

#endif /* #if (!INCLUDE_VFAT) */
