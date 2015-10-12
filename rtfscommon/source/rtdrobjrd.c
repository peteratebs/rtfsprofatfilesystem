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


/***************************************************************************
    PC_GET_CWD -  Get the current working directory for a drive,

 Description
    Return the current directory inode for the drive represented by ddrive.

***************************************************************************/

/*  Get the current working directory and copy it into pobj   */
DROBJ *_pc_get_user_cwd(DDRIVE *pdrive)                                   /*__fn__*/
{
    DROBJ *pcwd;
    DROBJ *pobj;
	RTFS_SYSTEM_USER *pu;

	pu = rtfs_get_system_user();
	pcwd = rtfs_get_user_pwd(pu, pdrive->driveno,FALSE);	  /* Get cwd for driveno, don't clear it */

    /* If no current working dir set it to the root   */

	if (!pcwd)
    {
        pcwd = pc_get_root(pdrive);
		rtfs_set_user_pwd(pu, pcwd);  /* Set cwd to pobj */
    }

    if (pcwd)
    {
        pobj = pc_allocobj();
        if (!pobj)
        {
            return (0);
        }
        /* Free the inode that comes with allocobj   */
        pc_freei(pobj->finode);
        OS_CLAIM_FSCRITICAL()
        copybuff(pobj, pcwd, sizeof(DROBJ));
        pobj->finode->opencount += 1;
        OS_RELEASE_FSCRITICAL()
        return (pobj);
    }
    else    /* If no cwd is set error */
    {
        rtfs_set_errno(PEINTERNAL, __FILE__, __LINE__);
        return(0);
    }
}


 /**************************************************************************
    PC_FNDNODE -  Find a file or directory on disk and return a DROBJ.

 Description
    Take a full path name and traverse the path until we get to the file
    or subdir at the end of the path spec. When found allocate and init-
    ialize (OPEN) a DROBJ.

 Returns
    Returns a pointer to a DROBJ if the file was found, otherwise 0.

***************************************************************************/


/* Find path and create a DROBJ structure if found   */
DROBJ *pc_fndnode(byte *path, int use_charset)                                   /*__fn__*/
{
    DROBJ *pobj;
    DROBJ *pmom;
    DROBJ *pchild;
    int  driveno;
    DDRIVE *pdrive;
    byte *filename;
    byte fileext[4];
    byte *pf0,*pf1;
    BLKBUFF *scratch;
    BLKBUFF *exfatscratch = 0;
#if (INCLUDE_EXFATORFAT64)
    byte *exFatPathbuff = 0;
#endif
    /* Get past D: plust get drive number if there   */
    path = pc_parsedrive( &driveno, path, use_charset);
    if (!path)
    {
        rtfs_set_errno(PEINVALIDDRIVEID, __FILE__, __LINE__); /* pc_fndnode: parsedrive failed */
        return (0);
    }
    /* Find the drive   */
    pdrive = pc_drno2dr(driveno);
    if (!pdrive)
    {
        rtfs_set_errno(PEINVALIDDRIVEID, __FILE__, __LINE__); /* pc_fndnode: pc_drno2dr failed */
        return (0);
    }
#if (INCLUDE_EXFATORFAT64)
	if (ISEXFATORFAT64(pdrive))
	{
    	exfatscratch = pc_scratch_blk();
    	if (!exfatscratch)
		{
        	return(0);
		}
    	exFatPathbuff = (byte *)exfatscratch->data;

    	if (!pcexfat_parse_path(pdrive, exFatPathbuff, path, use_charset))
    	{
    		pc_free_scratch_blk(exfatscratch);
        	return(0);
    	}
    	path = exFatPathbuff;
        pobj = pc_get_root(pdrive);
	    if (CS_OP_IS_NOT_EOS(path, use_charset))
		{
			CS_OP_INC_PTR(path, use_charset);
		}
	}
	else
#endif
	{
		/* Get the top of the current path   */
		if (CS_OP_CMP_ASCII(path, '\\', use_charset))
		{
        	CS_OP_INC_PTR(path, use_charset);
        	pobj = pc_get_root(pdrive);
		}
		else
		{
        	pobj = _pc_get_user_cwd(pdrive);
		}
	}
    if (!pobj)
        return (0);
    scratch = pc_scratch_blk();
    if (!scratch)
        return(0);
    filename = (byte *)scratch->data;


    /* Search through the path til exausted   */
    while (CS_OP_IS_NOT_EOS(path, use_charset))
    {
        path = pc_nibbleparse(filename,fileext, path, use_charset);
        if (!path)
        {
        	if (exfatscratch)
        		pc_free_scratch_blk(exfatscratch);
            pc_free_scratch_blk(scratch);
            rtfs_set_errno(PENOENT, __FILE__, __LINE__); /* pc_fndnode: no match */
            pc_freeobj(pobj);
            return (0);
        }
        pf1 = pf0 = filename;
        CS_OP_INC_PTR(pf1, use_charset);
#if (INCLUDE_VFAT)
        if (CS_OP_CMP_ASCII(pf0,'.', use_charset) && CS_OP_IS_EOS(pf1, use_charset)) /* DOT */
            ;
#else
        /* Tomo */
        if (CS_OP_CMP_ASCII(pf0,'.', use_charset) && CS_OP_CMP_ASCII(pf1,' ', use_charset)) /* DOT-in NON-VFAT it is space filled  */
            ;
#endif
        else
        {
            /* Find Filename in pobj. and initialize lpobj with result   */
            pchild = pc_get_inode(0, pobj, filename, fileext, GET_INODE_MATCH, use_charset);
            if (!pchild)
            { /* get_inode set errno */
        		if (exfatscratch)
        			pc_free_scratch_blk(exfatscratch);
                pc_free_scratch_blk(scratch);
                pc_freeobj(pobj);
                return (0);
            }
            /* We found it. We have one special case. if DOTDOT we need
                to shift up a level so we are not the child of mom
                but of grand mom. */
            pf1 = pf0 = filename;
            CS_OP_INC_PTR(pf1, use_charset);
            if (CS_OP_CMP_ASCII(pf0,'.', use_charset) && CS_OP_CMP_ASCII(pf1,'.', use_charset))
            {
                /* Find pobj s parent. By looking back from DOTDOT   */
                pmom = pc_get_mom(pchild);
                /* We are done with pobj for now   */
                pc_freeobj(pobj);

                if (!pmom)
                { /* Get mom set errno */
                	if (exfatscratch)
                		pc_free_scratch_blk(exfatscratch);
                    pc_free_scratch_blk(scratch);
                    pc_freeobj(pchild);
                    return (0);
                }
                else
                {
                    /* We found the parent now free the child   */
                    pobj = pmom;
                    pc_freeobj(pchild);
                }
            }
            else
            {
                /* We are done with pobj for now   */
                pc_freeobj(pobj);
                /* Make sure pobj points at the next inode   */
                pobj = pchild;
#if (INCLUDE_EXFATORFAT64)	 /* Found a directory, update drobj */
				if (ISEXFATORFAT64(pdrive) && pc_isadir(pobj))
				{
				dword fcluster,sectorno;
					fcluster = pc_finode_cluster(pobj->pdrive,pobj->finode);
					sectorno = pc_cl2sector(pobj->pdrive,fcluster);
					pobj->blkinfo.my_frstblock =  sectorno;
					pobj->blkinfo.my_block  =  sectorno;
					pobj->blkinfo.my_index  =  0;
#if (INCLUDE_EXFAT) /* FAT64 does not require exNOFATCHAIN */
					pobj->blkinfo.my_exNOFATCHAINfirstcluster = pcexfat_getexNOFATCHAINfirstcluster(pobj);
					pobj->blkinfo.my_exNOFATCHAINlastcluster = pcexfat_getexNOFATCHAINlastcluster(pobj);
#endif
				}
#endif
            }
        }
    }
    if (exfatscratch)
    	pc_free_scratch_blk(exfatscratch);
    pc_free_scratch_blk(scratch);
    return (pobj);
}

/***************************************************************************
    PC_GET_INODE -  Find a filename within a subdirectory

 Description
    Search the directory pmom for the pattern or name in filename:ext and
    return the an initialized object. If pobj is NULL start the search at
    the top of pmom (getfirst) and allocate pobj before returning it.
    Otherwise start the search at pobj (getnext). (see also pc_gfirst,
    pc_gnext)


    Note: Filename and ext must be right filled with spaces to 8 and 3 bytes
            respectively. Null termination does not matter.

    Note the use of the action variable. This is used so we do not have to
    use match patterns in the core code like *.* and ..
    GET_INODE_MATCH   Must match the pattern exactly
    GET_INODE_WILD    Pattern may contain wild cards
    GET_INODE_STAR    Like he passed *.* (pattern will be null)
    GET_INODE_DOTDOT  Like he past .. (pattern will be null


 Returns
    Returns a drobj pointer or NULL if file not found.
***************************************************************************/

/* Give a directory mom. And a file name and extension.
    Find find the file or dir and initialize pobj.
    If pobj is NULL. We allocate and initialize the object otherwise we get the
    next item in the chain of dirents.
*/
DROBJ *pc_get_inode( DROBJ *pobj, DROBJ *pmom, byte *filename, byte *fileext, int action, int use_charset) /*__fn__*/
{
    BOOLEAN  starting = FALSE;
    /* Create the child if just starting   */
    if (!pobj)
    {
        starting = TRUE;
        pobj = pc_mkchild(pmom);    /* If failure sets errno */
        if (!pobj)
            return(0);
    }
    else    /* If doing a gnext do not get stuck in and endless loop */
    {
        if ( ++(pobj->blkinfo.my_index) >= pobj->pdrive->drive_info.inopblock)
        {
            rtfs_clear_errno();  /* Clear errno to be safe */
            if (!pc_next_block(pobj)) /* Will set errno illegal block id encountered */
            {
                if (!get_errno())
                    rtfs_set_errno(PENOENT, __FILE__, __LINE__); /* pc_get_inode: end of dir */
                if (starting)
                    pc_freeobj(pobj);
                return(0);
            }
            else
                pobj->blkinfo.my_index = 0;
        }
    }
    if (pc_findin(pobj, filename, fileext,action, use_charset))
    {
DROBJ *proot;
        if (pobj->isroot)
		{
			proot = pc_get_root(pobj->pdrive);
			if (!proot || proot->finode != pobj->finode)
			{
				pobj->isroot = FALSE;
			}
			if (proot)
				pc_freeobj(proot);
		}
        return (pobj);
    }
    else
    {
        /* pc_findin set errno */
        if (starting)
            pc_freeobj(pobj);
        return (0);
    }
}

#if (INCLUDE_REVERSEDIR)
/* Give a directory mom. And a file name and extension.
    Search backwards and find the file or dir and initialize pobj.
    If pobj is NULL. We allocate and initialize the object otherwise we get the
    next item in the chain of dirents.
*/
DROBJ *pc_rget_inode( DROBJ *pobj, DROBJ *pmom, byte *filename, byte *fileext, int action, int use_charset) /*__fn__*/
{
#if (INCLUDE_VFAT)
    BOOLEAN  starting = FALSE;
	RTFS_ARGSUSED_PVOID((void *) fileext);
    /* Create the child if just starting   */
    if (!pobj)
    {
        starting = TRUE;
        pobj = pc_mkchild(pmom);    /* If failure sets errno */
        if (!pobj)
            return(0);
    }
    if (pc_rfindin(pobj, filename, action, use_charset, starting))
    {
DROBJ *proot;
        if (pobj->isroot)
		{
			proot = pc_get_root(pobj->pdrive);
			if (!proot || proot->finode != pobj->finode)
			{
				pobj->isroot = FALSE;
			}
			if (proot)
				pc_freeobj(proot);
		}
        return (pobj);
    }
    else
    {
        /* pc_findin set errno */
        if (starting)
            pc_freeobj(pobj);
        return (0);
    }
#else
    /* reverse directory not supported for 8.3 only */
    return 0;
#endif
}
#endif
/**************************************************************************
    PC_GET_MOM -  Find the parent inode of a subdirectory.

 Description
    Given a DROBJ initialized with the contents of a subdirectory s DOTDOT
    entry, initialize a DROBJ which is the parent of the current directory.

 Returns
    Returns a DROBJ pointer or NULL if could something went wrong.


****************************************************************************/
/*
* Get mom:
*   if (!dotodot->cluster)  Mom is root.
*       getroot()
*   else                    cluster points to mom.
*       find .. in mom
*       then search through the directory pointed to by moms .. until
*       you find mom. This will be current block startblock etc for mom.
*/

DROBJ *pc_get_mom(DROBJ *pdotdot)                                   /*__fn__*/
{
    DROBJ *pmom;
    DDRIVE *pdrive = pdotdot->pdrive;
    dword sectorno;
    BLKBUFF *rbuf;
    DIRBLK *pd;
    DOSINODE *pi;
    FINODE *pfi;
    dword clno;
    /* We have to be a subdir   */
    if (!pc_isadir(pdotdot))
    {
        rtfs_set_errno(PEINVALIDDIR, __FILE__, __LINE__);
        return(0);
    }

    /* If ..->cluster is zero then parent is root   */
    if (!pc_finode_cluster(pdrive,pdotdot->finode))
        return(pc_get_root(pdrive));

    /* Otherwise : cluster points to the beginning of our parent.
                    we also need the position of our parent in its parent  */
    pmom = pc_allocobj();
    if (!pmom)
        return (0);

    pmom->pdrive = pdrive;
    /* Find .. in our parent s directory   */
    clno = pc_finode_cluster(pdrive,pdotdot->finode);
    if ((clno < 2) || (clno > pdrive->drive_info.maxfindex) )
    {
        pc_freeobj(pmom);
        rtfs_set_errno(PEINVALIDCLUSTER, __FILE__, __LINE__);
        return (0);
    }
    sectorno = pc_cl2sector(pdrive,clno);
    /* We found .. in our parents dir.   */
    pmom->pdrive = pdrive;
    pmom->blkinfo.my_frstblock =  sectorno;
    pmom->blkinfo.my_block  =  sectorno;
    pmom->blkinfo.my_index  =  0;
    pmom->isroot = FALSE;
    pd = &pmom->blkinfo;

    pmom->pblkbuff = rbuf = pc_read_blk(pdrive, pmom->blkinfo.my_block);
    if (rbuf)
    {
        pi = (DOSINODE *) rbuf->data;
        OS_CLAIM_FSCRITICAL()
        pc_dos2inode(pmom->finode , pi );
        OS_RELEASE_FSCRITICAL()

        pc_release_buf(rbuf);

        /* See if the inode is in the buffers   */
        pfi = pc_scani(pdrive, sectorno, 0);
        if (pfi)
        {
            pc_freei(pmom->finode);
            pmom->finode = pfi;
        }
        else
        {
            pc_marki(pmom->finode , pmom->pdrive , pd->my_block,
                    pd->my_index);
        }
        return (pmom);

    }
    else    /* Error, something did not work */
    {
        pc_freeobj(pmom);
        return (0);
    }
}

/**************************************************************************
    PC_MKCHILD -  Allocate a DROBJ and fill in based on parent object.

 Description
    Allocate an object and fill in as much of the the block pointer section
    as possible based on the parent.

 Returns
    Returns a partially initialized DROBJ if enough core available and
    pmom was a valid subdirectory.

****************************************************************************/

DROBJ *pc_mkchild( DROBJ *pmom)                                     /*__fn__*/
{
    DROBJ *pobj;
    DIRBLK *pd;

    /* Mom must be a directory   */
    if (!pc_isadir(pmom))
    {
        rtfs_set_errno(PEINVALIDDIR, __FILE__, __LINE__); /* pc_mkchild: Internal error, parent dir provided */
        return(0);
    }
    /* init the object -   */
    pobj = pc_allocobj();
    if (!pobj)
        return (0);

    pd = &pobj->blkinfo;

    pobj->isroot = pmom->isroot;    /* Child inherets mom's root status. Changed above us */
    pobj->pdrive =  pmom->pdrive;   /* Child inherets moms drive */

    /* Now initialize the fields storing where the child inode lives   */
    pd->my_index = 0;
    pd->my_block = pd->my_frstblock = pc_firstblock(pmom);
#if (INCLUDE_EXFAT)  /* FAT64 does not require exNOFATCHAIN */
	if (ISEXFAT(pmom->pdrive))
	{
		pd->my_exNOFATCHAINfirstcluster = pcexfat_getexNOFATCHAINfirstcluster(pmom);
		pd->my_exNOFATCHAINlastcluster = pcexfat_getexNOFATCHAINlastcluster(pmom);
	}
#endif
    if (!pd->my_block)
    {
        pc_freeobj(pobj);
        return (0);
    }
    return (pobj);
}

/*************************************************************************
    PC_GET_ROOT -  Create the special ROOT object for a drive.
 Description
    Use the information in pdrive to create a special object for accessing
    files in the root directory.

 Returns
    Returns a pointer to a DROBJ, or NULL if no core available.
****************************************************************************/

/* Initialize the special root object
    Note: We do not read any thing in here we just set up
    the block pointers. */

DROBJ *pc_get_root( DDRIVE *pdrive)                                 /*__fn__*/
{
    DIRBLK *pd;
    DROBJ *pobj;
    FINODE *pfi;
#if (INCLUDE_EXFATORFAT64)
	if (ISEXFATORFAT64(pdrive))	/* Treat Exfat root more like normal directories */
		return(pcexfat_get_root(pdrive));
#endif
    pobj = pc_allocobj();
    if (!pobj)
        return (0);
    pobj->pdrive = pdrive;
    pfi = pc_scani(pdrive, 0, 0);

    if (pfi)
    {
        pc_freei(pobj->finode);
        pobj->finode = pfi;
    }
    else    /* No inode in the inode list. Copy the data over
                and mark where it came from */
    {
        pc_marki(pobj->finode , pdrive , 0, 0);
    }


    /* Add a TEST FOR DRIVE INIT Here later   */
    pobj->pdrive = pdrive;
    /* Set up the tree stuf so we know it is the root   */
    pd = &pobj->blkinfo;
    pd->my_frstblock = pdrive->drive_info.rootblock;
    pd->my_block = pdrive->drive_info.rootblock;
    pd->my_index = 0;
    pobj->isroot = TRUE;
    return (pobj);
}


/****************************************************************************
    PC_FIRSTBLOCK -  Return the absolute block number of a directory s
                    contents.
 Description
    Returns the block number of the first inode in the subdirectory. If
    pobj is the root directory the first block of the root will be returned.

 Returns
    Returns 0 if the obj does not point to a directory, otherwise the
    first block in the directory is returned.

*****************************************************************************/

/* Get  the first block of a root or subdir   */
dword pc_firstblock( DROBJ *pobj)                                  /*__fn__*/
{
    dword clno;
    if (!pc_isadir(pobj))
    {
        rtfs_set_errno(PEINTERNAL, __FILE__, __LINE__); /* pc_firstblock: Internal error */
        return (0);
    }
    /* Root dir ?   */
    if (!pobj->isroot)
    {
        clno = pc_finode_cluster(pobj->pdrive, pobj->finode);
        if ((clno < 2) || (clno > pobj->pdrive->drive_info.maxfindex) )
        {
            rtfs_set_errno(PEINVALIDCLUSTER, __FILE__, __LINE__);
            return (0);
        }
        return (pc_cl2sector(pobj->pdrive , clno));
    }
    else
        return (pobj->blkinfo.my_frstblock);
}

/***************************************************************************
    PC_NEXT_BLOCK - Calculate the next block owned by an object.

 Description
    Find the next block owned by an object in either the root or a cluster
    chain and update the blockinfo section of the object.

 Returns
    Returns TRUE or FALSE on end of chain.

*****************************************************************************/


/* Calculate the next block in an object   */
BOOLEAN pc_next_block( DROBJ *pobj)                                 /*__fn__*/
{
    dword nxt;

    nxt = pc_l_next_block(pobj->pdrive, pobj->blkinfo.my_block);

    if (nxt)
    {
        pobj->blkinfo.my_block = nxt;
        return (TRUE);
    }
    else
        return (FALSE);
}

/**************************************************************************
    PC_L_NEXT_BLOCK - Calculate the next block in a chain.

 Description
    Find the next block in either the root or a cluster chain.

 Returns
    Returns 0 on end of root dir or chain.

****************************************************************************/

    /* Return the next block in a chain   */
dword pc_l_next_block(DDRIVE *pdrive, dword curblock)             /*__fn__*/
{
    dword cluster;
    /* If the block is in the root area   */
    if (curblock < pdrive->drive_info.firstclblock)
    {
        if (curblock < pdrive->drive_info.rootblock)
        {
            rtfs_set_errno(PEINTERNAL, __FILE__, __LINE__); /* pc_l_next_block: Internal error */
            return (0);
        }
        else if (++curblock < pdrive->drive_info.firstclblock)
            return (curblock);
        else
            return (0);
    }
    else  /* In cluster space   */
    {
        if (curblock >= pdrive->drive_info.numsecs)
        {
            rtfs_set_errno(PEINVALIDCLUSTER, __FILE__, __LINE__); /* pc_l_next_block: Internal error */
            return (0);
        }
        /* Get the next block   */
        curblock += 1;

        /* If the next block is not on a cluster edge then it must be
            in the same cluster as the current. - otherwise we have to
            get the firt block from the next cluster in the chain */
        if (pc_sec2index(pdrive, curblock))
            return (curblock);
        else
        {
            curblock -= 1;
            /* Get the old cluster number */
            cluster = pc_sec2cluster(pdrive,curblock);
            if ((cluster < 2) || (cluster > pdrive->drive_info.maxfindex) )
            {
                rtfs_set_errno(PEINVALIDCLUSTER, __FILE__, __LINE__); /* pc_l_next_block: Internal error */
                return (0);
            }
             /* Consult the fat for the next cluster. */
            cluster = fatop_next_cluster(pdrive, cluster);
            if (cluster == FAT_EOF_RVAL)
                return (0); /* End of chain */
            else if (cluster == 0) /* clnext detected error */
            {
                return (0);
            }
            else
                return (pc_cl2sector(pdrive, cluster));

        }
    }
}

/**************************************************************************
    PC_MARKI -  Set dr:sec:index info and stitch a FINODE into the inode list


Description
    Each inode is uniquely determined by DRIVE, BLOCK and Index into that
    block. This routine takes an inode structure assumed to contain the
    equivalent of a DOS directory entry. And stitches it into the current
    active inode list. Drive block and index are stored for later calls
    to pc_scani and the inode s opencount is set to one.

Returns
    Nothing

***************************************************************************/



/* Take an unlinked inode and link it in to the inode chain. Initialize
    the open count and sector locater info. */
void pc_marki( FINODE *pfi, DDRIVE *pdrive, dword sectorno, int  index)/*__fn__*/
{
    OS_CLAIM_FSCRITICAL()

    pfi->my_drive = pdrive;
    pfi->my_block = sectorno;
    pfi->my_index = index;
    pfi->opencount = 1;

    /* Stitch the inode at the front of the list   */
    if (prtfs_cfg->inoroot)
        prtfs_cfg->inoroot->pprev = pfi;

    pfi->pprev = 0;
    pfi->pnext = prtfs_cfg->inoroot;
    prtfs_cfg->inoroot = pfi;
    OS_RELEASE_FSCRITICAL()
}


/**************************************************************************
    PC_SCANI -  Search for an inode in the internal inode list.

Description
    Each inode is uniquely determined by DRIVE, BLOCK and Index into that
    block. This routine searches the current active inode list to see
    if the inode is in use. If so the opencount is changed and a pointer is
    returned. This guarantees that two processes will work on the same
    information when manipulating the same file or directory.

Returns
    A pointer to the FINODE for pdrive:sector:index or NULL if not found

****************************************************************************/

/* See if the inode for drive,sector , index is in the list. If so..
    bump its open count and return it. Else return NULL */

FINODE *pc_scani( DDRIVE *pdrive, dword sectorno, int index)       /*__fn__*/
{
    FINODE *pfi;
    OS_CLAIM_FSCRITICAL()
    pfi = prtfs_cfg->inoroot;
    while (pfi)
    {
        if (pfi->my_drive == pdrive)
        {
            if ( (pfi->my_block == sectorno) &&
                 (pfi->my_index == index) )
            {
                pfi->opencount += 1;
                OS_RELEASE_FSCRITICAL()
                return (pfi);
            }
        }
        pfi = pfi->pnext;
    }
    OS_RELEASE_FSCRITICAL()
    return (0);
}


/**************************************************************************
    PC_ALLOCOBJ -  Allocate a DROBJ structure
 Description
    Allocates and zeroes the space needed to store a DROBJ structure. Also
    allocates and zeroes a FINODE structure and links the two via the
    finode field in the DROBJ structure.

 Returns
    Returns a valid pointer or NULL if no more core.

*****************************************************************************/

DROBJ *pc_allocobj(void)                                    /*__fn__*/
{
    DROBJ *pobj;

    /* Alloc a DROBJ   */
    pobj = pc_memory_drobj(0);
    if (pobj)
    {
        pobj->finode = pc_alloci();
        if (!pobj->finode)
        {
            /* Free the DROBJ   */
            pc_memory_drobj(pobj);
            pobj = 0;
        }
    }
    return (pobj);
}

/**************************************************************************
    PC_ALLOCI -  Allocate a FINODE structure

 Description
    Allocates and zeroes a FINODE structure.

 Returns
    Returns a valid pointer or NULL if no more core.

****************************************************************************/

FINODE *pc_alloci(void)                                         /*__fn__*/
{
FINODE *p;
    p = pc_memory_finode(0);
    return(p);
}

/**************************************************************************
    PC_FREE_ALL_DROBJ -  Release all drobj buffers associated with a drive.
Description
    For each internally buffered drobj structure associated with pdrive
Returns
    Nothing
****************************************************************************/

void pc_free_all_drobj( DDRIVE *pdrive)                             /*__fn__*/
{
    int i;
    DROBJ *pobj;
    pobj = prtfs_cfg->mem_drobj_pool;
    for (i = 0; i < prtfs_cfg->cfg_NDROBJS; i++,pobj++)
    {
        if (pobj->pdrive == pdrive)
            pc_memory_drobj(pobj);
    }
}

/**************************************************************************
    PC_FREE_ALL_I -  Release all inode buffers associated with a drive.
Description
    For each internally buffered finode (dirent) check if it exists on
    pdrive. If so delete it. In debug mode print a message since all
    finodes should be freed before pc_dskclose is called.
Returns
    Nothing
****************************************************************************/

static void pc_discardi(FINODE *pfi);

void pc_free_all_i( DDRIVE *pdrive)                             /*__fn__*/
{
    FINODE *pfi;
    OS_CLAIM_FSCRITICAL()
    pfi = prtfs_cfg->inoroot;
    OS_RELEASE_FSCRITICAL()
    while (pfi)
    {
        if (pfi->my_drive == pdrive)
        {
            pc_discardi(pfi);
            /* Since we changed the list go back to the top   */
            OS_CLAIM_FSCRITICAL()
            pfi = prtfs_cfg->inoroot;
            OS_RELEASE_FSCRITICAL()
        }
        else
        {
            OS_CLAIM_FSCRITICAL()
            pfi = pfi->pnext;
            OS_RELEASE_FSCRITICAL()
        }
    }
}


#if (INCLUDE_RTFS_PROPLUS)  /* ProPlus finode extensions */
void pc_discardi_extended(FINODE *pfi);
void pc_freei_extended(FINODE *pfi);
#endif

/* return finode memory to the free list without processing */
static void pc_discardi(FINODE *pfi)
{
    OS_CLAIM_FSCRITICAL()
    {   /* Unlink from the shared finode list */
        if (pfi->pprev) /* Pont the guy behind us at the guy in front*/
            pfi->pprev->pnext = pfi->pnext;
        else
             prtfs_cfg->inoroot = pfi->pnext; /* No prev, we were at the front so
                                    make the next guy the front */
        if (pfi->pnext)         /* Make the next guy point behind */
            pfi->pnext->pprev = pfi->pprev;
    }
    OS_RELEASE_FSCRITICAL()
#if (INCLUDE_RTFS_PROPLUS)  /* ProPlus finode extensions */
    /* Free resources used in extended mode */
    pc_discardi_extended(pfi);
#endif
    pc_memory_finode(pfi);
}

/*****************************************************************************
    PC_FREEI -  Release an inode from service

Description
    If the FINODE structure is only being used by one file or DROBJ, unlink it
    from the internal active inode list and return it to the heap; otherwise
    reduce its open count.

 Returns
    Nothing

****************************************************************************/


void pc_freei(FINODE *pfi)                                     /*__fn__*/
{
    if (!pfi)
        return;
    OS_CLAIM_FSCRITICAL()
    if (pfi->opencount)
    {
        if (--pfi->opencount) /* Decrement opencount and return if non zero */
        {
            OS_RELEASE_FSCRITICAL()
#if (INCLUDE_RTFS_PROPLUS)  /* ProPlus finode extensions */
            pc_freei_extended(pfi); /* If ProPlus, reduce extension opencounts */
#endif
            return;
        }
        else
        {   /* Unlink from the shared finode list */

            if (pfi->pprev) /* Pont the guy behind us at the guy in front*/
            {
                pfi->pprev->pnext = pfi->pnext;
            }
            else
            {

                prtfs_cfg->inoroot = pfi->pnext; /* No prev, we were at the front so
                                        make the next guy the front */
            }

            if (pfi->pnext)         /* Make the next guy point behind */
            {
                pfi->pnext->pprev = pfi->pprev;
            }
        }
    }
    OS_RELEASE_FSCRITICAL()
    /* If the finode contains a fragment list for use in basic file io then release it */
    if (pfi->pbasic_fragment)
        pc_fraglist_free_list(pfi->pbasic_fragment);
    pfi->pbasic_fragment = 0;

#if (INCLUDE_RTFS_PROPLUS)  /* ProPlus finode extensions */
    /* pc_freei_entended() tests if the finode is an ProPlus extended Inode.
       If so it release the extensions */
    pc_freei_extended(pfi);
#endif
    pc_memory_finode(pfi);

}

/***************************************************************************
    PC_FREEOBJ -  Free a DROBJ structure

 Description
    Return a drobj structure to the heap. Calls pc_freei to reduce the
    open count of the finode structure it points to and return it to the
    heap if appropriate.

 Returns
    Nothing


****************************************************************************/

void pc_freeobj( DROBJ *pobj)                                   /*__fn__*/
{
    if (pobj)
    {
        pc_freei(pobj->finode);
        /* Release the core   */
        pc_memory_drobj(pobj);
    }
}

/***************************************************************************
    PC_DOS2INODE - Convert a dos disk entry to an in memory inode.

 Description
    Take the data from pbuff which is a raw disk directory entry and copy
    it to the inode at pdir. The data goes from INTEL byte ordering to
    native during the transfer.

 Returns
    Nothing
****************************************************************************/

/* Convert a dos inode to in mem form.  */
void pc_dos2inode (FINODE *pdir, DOSINODE *pbuff)                   /*__fn__*/
{
    copybuff(&pdir->fname[0],&pbuff->fname[0],8);           /*X*/
    /* If the on disk representation is 0x5, change it to 0xE5, a valid
       kanji character */
    if (pdir->fname[0] == 0x5)
        pdir->fname[0] = PCDELETE;
    copybuff(&pdir->fext[0],&pbuff->fext[0],3);             /*X*/
    pdir->fattribute = pbuff->fattribute;                   /*X*/

    pdir->reservednt = pbuff->reservednt;
    pdir->create10msincrement =  pbuff->create10msincrement;

#if (!KS_LITTLE_ENDIAN)
    pdir->ftime = to_WORD((byte *) &pbuff->ftime);          /*X*/
    pdir->fdate = to_WORD((byte *) &pbuff->fdate);          /*X*/
	pdir->ctime = to_WORD((byte *) &pbuff->ctime);
	pdir->cdate = to_WORD((byte *) &pbuff->cdate);
	pdir->adate	= to_WORD((byte *) &pbuff->adate);
	pdir->atime	= pdir->ftime;			/* Access time not supported for FAT32, set it to last modified */
    /* Note: fclusterhi is of resarea in fat 16 system */
    pdir->fclusterhi = to_WORD((byte *) &pbuff->fclusterhi);
    pdir->fcluster = to_WORD((byte *) &pbuff->fcluster);    /*X*/
    pdir->fsizeu.fsize = to_DWORD((byte *) &pbuff->fsize);     /*X*/
#else
    pdir->ftime = pbuff->ftime;         /*X*/
    pdir->fdate = pbuff->fdate;         /*X*/
	pdir->ctime = pbuff->ctime;
	pdir->cdate = pbuff->cdate;
	pdir->adate	= pbuff->adate;
	pdir->atime	= pdir->ftime;			/* Access time not supported for FAT32, set it to last modified */
    /* Note: fclusterhi is of resarea in fat 16 system */
    pdir->fclusterhi = pbuff->fclusterhi;   /*X*/
    pdir->fcluster = pbuff->fcluster;   /*X*/
    pdir->fsizeu.fsize = pbuff->fsize;     /*X*/
#endif
}

/**************************************************************************
    PC_INIT_INODE -  Load an in memory inode up with user supplied values.

 Description
    Take an uninitialized inode (pdir) and fill in some fields. No other
    processing is done. This routine simply copies the arguments into the
    FINODE structure.

    Note: filename & fileext do not need null termination.

 Returns
    Nothing
****************************************************************************/
/**************************************************************************
    PC_ISAVOL -  Test a DROBJ to see if it is a volume

 Description
    Looks at the appropriate elements in pobj and determines if it is a root
    or subdirectory.

 Returns
    Returns FALSE if the obj does not point to a directory.

****************************************************************************/

BOOLEAN pc_isavol( DROBJ *pobj)                                     /*__fn__*/
{
    if (pobj->finode->fattribute & AVOLUME)
        return(TRUE);
    else
        return(FALSE);
}


/**************************************************************************
    PC_ISADIR -  Test a DROBJ to see if it is a root or subdirectory

 Description
    Looks at the appropriate elements in pobj and determines if it is a root
    or subdirectory.

 Returns
    Returns FALSE if the obj does not point to a directory.

****************************************************************************/

BOOLEAN pc_isadir( DROBJ *pobj)                                     /*__fn__*/
{
    if ( (pobj->isroot) || (pobj->finode->fattribute & ADIRENT)  )
        return(TRUE);
    else
        return(FALSE);
}


/**************************************************************************
    PC_ISROOT -  Test a DROBJ to see if it is the root directory

 Description
    Looks at the appropriate elements in pobj and determines if it is a root
    directory.

 Returns
    Returns NO if the obj does not point to the root directory.

****************************************************************************/

/* Get  the first block of a root or subdir   */
BOOLEAN pc_isroot( DROBJ *pobj)                                        /*__fn__*/
{
    return(pobj->isroot);
}
