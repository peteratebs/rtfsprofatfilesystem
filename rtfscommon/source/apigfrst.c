/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* APIGFRST.C - Contains user api level source code.

    The following routines are included:

    pc_gfirst       - Get stats on the first file to match a pattern.
    pc_gnext        - Get stats on the next file to match a pattern.
    pc_gdone        - Free resources used by pc_gfirst/pc_gnext.
    pc_upstat       -   Copy directory entry info to a user s stat buffer


*/

#include "rtfs.h"


#if (INCLUDE_CS_UNICODE)
static BOOLEAN _pc_gfirst_cs(DSTAT *statobj, byte *name, int use_charset);
#else
static BOOLEAN _pc_gfirst(DSTAT *statobj, byte *name);
#endif
#if (INCLUDE_REVERSEDIR)
#if (INCLUDE_CS_UNICODE)
BOOLEAN pc_glast_cs(DSTAT *statobj, byte *name, int use_charset)
#else
BOOLEAN pc_glast(DSTAT *statobj, byte *name)      /*__apifn__*/
#endif
{
	statobj->search_backwards_if_magic = SEARCH_BACKWARDS_MAGIC_NUMBER;
#if (INCLUDE_CS_UNICODE)
	return (_pc_gfirst_cs(statobj, name, use_charset));
#else
	return(_pc_gfirst(statobj, name));
#endif
}
#endif


/***************************************************************************
    PC_GFIRST - Get first entry in a directory to match a pattern.

 Description
    Given a pattern which contains both a path specifier and a search pattern
    fill in the structure at statobj with information about the file and set
    up internal parts of statobj to supply appropriate information for calls
    to pc_gnext.

    Examples of patterns are:
        D:\USR\RELEASE\NETWORK\*.C
        D:\USR\BIN\UU*.*
        D:MEMO_?.*
        D:*.*

 Returns
    Returns TRUE if a match was found otherwise FALSE. (see also the pcls.c
    utility.)

    errno is set to one of the following
    0               - No error
    PEINVALIDDRIVEID- Drive component is invalid
    PEINVALIDPATH   - Path specified badly formed.
    PENOENT         - Not found, no match
    An ERTFS system error

****************************************************************************/

static void pc_upstat(DSTAT *statobj, int use_charset);

#if (INCLUDE_CS_UNICODE)
BOOLEAN pc_gfirst_cs(DSTAT *statobj, byte *name, int use_charset)
#else
BOOLEAN pc_gfirst(DSTAT *statobj, byte *name)      /*__apifn__*/
#endif
{
#if (INCLUDE_EXFATORFAT64)
	if (statobj->search_backwards_if_magic!=SYS_SCAN_MAGIC_NUMBER) /* system scan overrides default behavior */
#endif
		statobj->search_backwards_if_magic = 0;
#if (INCLUDE_CS_UNICODE)
	return (_pc_gfirst_cs(statobj, name, use_charset));
#else
	return(_pc_gfirst(statobj, name));
#endif
}

#if (INCLUDE_CS_UNICODE)
static BOOLEAN _pc_gfirst_cs(DSTAT *statobj, byte *name, int use_charset)
#else
static BOOLEAN _pc_gfirst(DSTAT *statobj, byte *name)      /*__apifn__*/
#endif
{
    byte  *mompath;
    byte  *filename;
    byte  fileext[4];
    int driveno;
    DDRIVE *pdrive;
    BOOLEAN ret_val;



    CHECK_MEM(BOOLEAN, 0)   /* Make sure memory is initted */

    rtfs_clear_errno();  /* po_gfirst: clear error status */

	{
		dword save_magic = statobj->search_backwards_if_magic;
		rtfs_memset((byte *) statobj,0,sizeof(*statobj));
		/*    statobj->pobj = 0; */
		/*    statobj->pmom = 0; */
		statobj->search_backwards_if_magic = save_magic;
	}


    /* Get the drive and make sure it is mounted   */
    driveno = check_drive_name_mount(name, CS_CHARSET_ARGS);
    if (driveno < 0)
    {
        /* errno was set by check_drive */
        return(FALSE);
    }
    pdrive = pc_drno2dr(driveno);
    ret_val = FALSE;
    /* Allocate scratch buffers in the DRIVE structure. */
    if (!pc_alloc_path_buffers(pdrive))
        goto errex;
    mompath = pdrive->pathname_buffer;
    filename = pdrive->filename_buffer;
    rtfs_memset((byte *)&fileext[0],0,sizeof(fileext)); /* Zero fill fileext because we will not use it */
    /* Get out the filename and d:parent   */
    if (!pc_parsepath(mompath,filename,fileext,name, CS_CHARSET_ARGS))
    {
        rtfs_set_errno(PEINVALIDPATH, __FILE__, __LINE__);
        goto errex;
    }

    /* Save the pattern. we will need it in pc_gnext   */
    copybuff(statobj->pname, filename, FILENAMESIZE_BYTES);
    copybuff(statobj->pext, fileext, 4);
    /* Copy over the path. we will need it later   */
    copybuff(statobj->path, mompath, EMAXPATH_BYTES);
    /* Find the file and init the structure   */
    statobj->pmom = (void *) pc_fndnode(mompath, CS_CHARSET_ARGS);
    /* pc_fndnode will set errno */
    if (statobj->pmom)
    /* Found it. Check access permissions   */
    {
        if(pc_isadir((DROBJ *)(statobj->pmom)))
        {
#if (INCLUDE_REVERSEDIR)
            /* Now find pattern in the directory   */
        	if (statobj->search_backwards_if_magic == SEARCH_BACKWARDS_MAGIC_NUMBER)
            	statobj->pobj = (void *) pc_rget_inode(0, (DROBJ *)(statobj->pmom), filename, (byte*) fileext, GET_INODE_WILD, CS_CHARSET_ARGS);
			else
#endif
            	statobj->pobj = (void *) pc_get_inode(0, (DROBJ *)(statobj->pmom), filename, (byte*) fileext, GET_INODE_WILD, CS_CHARSET_ARGS);

            if (statobj->pobj)
            {
                /* And update the stat structure   */
                pc_upstat(statobj, CS_CHARSET_ARGS);
                /* remember the drive number. used by gnext et al.   */
                statobj->driveno = driveno;
#if (INCLUDE_EXFATORFAT64)
				if (statobj->search_backwards_if_magic!=SYS_SCAN_MAGIC_NUMBER) /* EXFAT system scan  uses and then frees the finode at a higher layer  */
#endif
				{
					pc_freei(((DROBJ *)(statobj->pobj))->finode); /* Release the current */
				     ((DROBJ *)(statobj->pobj))->finode = 0;
				}
                statobj->drive_opencounter = pdrive->drive_opencounter;
                ret_val = TRUE;
                goto errex;
            }
            else
            {
            /* pc_gfirst: if statobj->pobj is 0 pc_get_inode() has set errno to PENOENT or
               to an internal or IO error status
               if PENOENT set we will clear errno */
                if (get_errno() == PENOENT)
                    rtfs_clear_errno(); /* pc_gfirst: file not found in directory
                                      set errno to zero and return FALSE */
            }
        }
        else
            rtfs_set_errno(PENOENT, __FILE__, __LINE__); /* pc_gfirst: Path not a directory, report not found */
    }
    /* If it gets here we had a problem  ret_val is false  */
    if (statobj->pmom)
        pc_freeobj((DROBJ *)statobj->pmom);
    rtfs_memset((byte *) statobj,0,sizeof(*statobj));
errex:
    pc_release_path_buffers(pdrive);
    release_drive_mount(driveno);/* Release lock, unmount if aborted */
    return(ret_val);
}

/****************************************************************************
    PC_GNEXT - Get next entry in a directory that matches a pattern.

 Description
    Given a pointer to a DSTAT structure that has been set up by a call to
    pc_gfirst(), search for the next match of the original pattern in the
    original path. Return TRUE if found and update statobj for subsequent
    calls to pc_gnext.

 Returns
    Returns TRUE if a match was found otherwise FALSE.

    errno is set to one of the following
    0               - No error
    PEINVALIDPARMS - statobj argument is not valid
    PENOENT        - Not found, no match (normal termination of scan)
    An ERTFS system error
****************************************************************************/
#if (INCLUDE_REVERSEDIR)
#if (INCLUDE_CS_UNICODE)
BOOLEAN pc_gprev_cs(DSTAT *statobj, int use_charset)
#else
BOOLEAN pc_gprev(DSTAT *statobj)
#endif
{
	statobj->search_backwards_if_magic = SEARCH_BACKWARDS_MAGIC_NUMBER;

#if (INCLUDE_CS_UNICODE)
	return pc_gnext_cs(statobj, use_charset);
#else
	return pc_gnext(statobj);
#endif
}
#endif
#if (INCLUDE_CS_UNICODE)
BOOLEAN pc_gnext_cs(DSTAT *statobj, int use_charset)
#else
BOOLEAN pc_gnext(DSTAT *statobj)
#endif
{
    DROBJ *nextobj;
    DDRIVE *pdrive;
    CHECK_MEM(BOOLEAN, 0)   /* Make sure memory is initted */
    /* see if the drive is still mounted. Do not use pmom et al. since they
        may be purged */
    if (!statobj || !statobj->pmom)
    {
        rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__); /* pc_gnext: statobj is not valid */
        return(FALSE);
    }
    pdrive = check_drive_by_number(statobj->driveno, TRUE);
	if (!pdrive)
        return(FALSE);
    if (statobj->drive_opencounter != pdrive->drive_opencounter)
    {
        rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__); /* pc_gnext: statobj is not valid */
        release_drive_mount(statobj->driveno);/* Release lock, unmount if aborted */
        return(FALSE);
    }

    rtfs_clear_errno();  /* po_gnext: clear error status */
#if (INCLUDE_REVERSEDIR)
    /* Now find the next instance of pattern in the directory   */
  	if (statobj->search_backwards_if_magic == SEARCH_BACKWARDS_MAGIC_NUMBER)
        	nextobj = pc_rget_inode((DROBJ *)(statobj->pobj), (DROBJ *)(statobj->pmom), statobj->pname, statobj->pext, GET_INODE_WILD, CS_CHARSET_ARGS);
	else
#endif
    	nextobj = pc_get_inode((DROBJ *)(statobj->pobj), (DROBJ *)(statobj->pmom),
    							statobj->pname, statobj->pext, GET_INODE_WILD, CS_CHARSET_ARGS);
    if (nextobj)
    {
        statobj->pobj = (void *)nextobj;
        /* And update the stat structure   */
        pc_upstat(statobj, CS_CHARSET_ARGS);
#if (INCLUDE_EXFATORFAT64)
		if (statobj->search_backwards_if_magic!=SYS_SCAN_MAGIC_NUMBER) /* EXFAT system scan  uses and then frees the finode at a higher layer  */
#endif
		{
       		pc_freei(((DROBJ *)(statobj->pobj))->finode); /* Release the current */
       		((DROBJ *)(statobj->pobj))->finode = 0;
		}
        release_drive_mount(statobj->driveno);/* Release lock, unmount if aborted */
        return(TRUE);
    }
    else
    {
        if (get_errno() == PENOENT)
            rtfs_clear_errno(); /* get_inode: file not found in directory
                                  set errno to zero and return FALSE */
       /* pc_gnext: nextobj is 0 pc_get_inode() has set errno to PENOENT or to an internal or IO error status */
        release_drive_mount(statobj->driveno);/* Release lock, unmount if aborted */
        return(FALSE);
    }
}


/***************************************************************************
    PC_GDONE - Free internal resources used by pc_gnext and pc_gfirst.

 Description
    Given a pointer to a DSTAT structure that has been set up by a call to
    pc_gfirst() free internal elements used by the statobj.

    NOTE: You MUST call this function when done searching through a
    directory.

 Returns
    Nothing

    errno is set to one of the following
    0               - No error
    PEINVALIDPARMS - statobj argument is not valid
****************************************************************************/

void pc_gdone(DSTAT *statobj)                                   /*__apifn__*/
{
    DDRIVE *pdrive;
    VOID_CHECK_MEM()    /* Make sure memory is initted */
    /* see if the drive is still mounted. Do not use pmom et al. since they
        may be purged */
    /* see if the drive is still mounted. Do not use pmom et al. since they
        may be purged */
    if (!statobj || !statobj->pmom)
    {
        return;
    }
    pdrive = check_drive_by_number(statobj->driveno, TRUE);
	if (!pdrive)
        return;
    if (statobj->drive_opencounter != pdrive->drive_opencounter)
    {
        release_drive_mount(statobj->driveno);/* Release lock, unmount if aborted */
        return;
    }
    if (statobj->pobj)
    {
        pc_freeobj((DROBJ *)statobj->pobj);
    }
    if (statobj->pmom)
        pc_freeobj((DROBJ *)statobj->pmom);
    release_drive_mount(statobj->driveno);/* Release lock, unmount if aborted */
    rtfs_memset((byte *) statobj,0,sizeof(*statobj));
}

/****************************************************************************
    PC_UPSTAT - Copy private information to public fields for a DSTAT struc.

 Description
    Given a pointer to a DSTAT structure that contains a pointer to an
    initialized DROBJ structure, load the public elements of DSTAT with
    name filesize, date of modification et al. (Called by pc_gfirst &
    pc_gnext)

 Returns
    Nothing


****************************************************************************/

/* Copy internal stuf so the outside world can see it   */
static void pc_upstat(DSTAT *statobj, int use_charset)                                  /*__fn__*/
{
    DROBJ *pobj;
    FINODE *pi;
    pobj = (DROBJ *)(statobj->pobj);

    pi = pobj->finode;

    copybuff( statobj->fname, pi->fname, 8);
    statobj->fname[8] = 0;
    copybuff( statobj->fext, pi->fext, 3);
    statobj->fext[3] = 0;
    /* put null termed 8.3 file.ext into statobj   */
    pc_cs_mfile((byte *)statobj->filename, (byte *)statobj->fname,
             (byte *)statobj->fext, CS_CHARSET_NOT_UNICODE);

    statobj->fattribute = pi->fattribute;
    statobj->ftime = pi->ftime;
    statobj->fdate = pi->fdate;
   	statobj->ctime = pi->ctime;
   	statobj->cdate = pi->cdate;
   	statobj->adate = pi->adate;
   	statobj->atime = pi->atime;

#if (INCLUDE_EXFATORFAT64)
	if (ISEXFATORFAT64(pobj->pdrive))
	{
    	statobj->fsize = M64LOWDW(pi->fsizeu.fsize64);
    	statobj->fsize_hi = M64HIGHDW(pi->fsizeu.fsize64);
    	statobj->fname[0] = 0;
    	statobj->fext[0] = 0;
	}
	else
#endif
	{
    	statobj->fsize = pi->fsizeu.fsize;
    	statobj->fsize_hi = 0;
	}

    statobj->my_block = pi->my_block;
    statobj->my_index = pi->my_index;


    /* Get the lfn value for this object. If none available make
       an ASCII or UNICODE copy of the short name in lfn */
    if (!pc_get_lfn_filename(pobj, (byte *)statobj->lfname, use_charset))
    {
        statobj->lfname[0] = statobj->lfname[1] = 0;
        pc_cs_mfileNT((byte *)statobj->lfname, (byte *)statobj->fname,
             (byte *)statobj->fext, use_charset,pobj->finode->reservednt);
    }
}
