/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* APIFILEMV.C - Contains user api level source code.

    The following routines are included:

    pc_mv           - Rename a file.
    pc_unlink       - Delete a file.
*/

#include "rtfs.h"
#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */

/***************************************************************************
    PC_MV -  Rename a file.

 Description
    Renames the file in path (name) to newname. Fails if name is invalid,
    newname already exists or path not found.

    01-07-99 - Rewrote to support moving files between subdirectories
               no longer supports renaming subdirectories or volumes
 Returns
    Returns TRUE if the file was renamed. Or no if the name not found.

    errno is set to one of the following
    0               - No error
    PEINVALIDDRIVEID- Drive component is invalid or they are not the same
    PEINVALIDPATH   - Path specified by old_name or new_name is badly formed.
    PEACCESS        - File or directory in use, or old_name is read only
    PEEXIST         - new_name already exists
    An ERTFS system error
***************************************************************************/
/* Rename a file */
#if (INCLUDE_CS_UNICODE)
BOOLEAN pc_mv_cs(byte *old_name, byte *new_name,int use_charset)
#else
BOOLEAN pc_mv(byte *old_name, byte *new_name)
#endif
{
    int old_driveno;
    DROBJ *old_obj;
    DROBJ *old_parent_obj;
    DROBJ *dot_dot_obj;
    byte  *path;
    byte  *filename;
    byte fileext[4];
    int new_driveno;
    DROBJ *new_obj;
    DROBJ *new_parent_obj;
    BOOLEAN ret_val;
    DDRIVE *pdrive;
    int  p_set_errno;
    CHECK_MEM(BOOLEAN, 0)   /* Make sure memory is initted */
    p_set_errno = 0;

    /* Drives must be the same */
    if (    !pc_parsedrive( &old_driveno, old_name, CS_CHARSET_ARGS) ||
            !pc_parsedrive( &new_driveno, new_name, CS_CHARSET_ARGS) ||
            old_driveno != new_driveno)
    {
        rtfs_set_errno(PEINVALIDDRIVEID, __FILE__, __LINE__);
        return(FALSE);
    }

    /* Get the drive and make sure it is mounted   */
    old_driveno = check_drive_name_mount(old_name, CS_CHARSET_ARGS);
    if (old_driveno < 0)
    {
        /* errno was set by check_drive */
        return(FALSE);
    }
    rtfs_clear_errno();  /* pc_mv: clear error status */

    dot_dot_obj     = 0;
    old_obj         = 0;
    old_parent_obj  = 0;
    new_obj         = 0;
    new_parent_obj  = 0;
    ret_val = FALSE;

    pdrive = pc_drno2dr(old_driveno);
    /* Allocate scratch buffers in the DRIVE structure. */
    if (!pc_alloc_path_buffers(pdrive))
        goto errex;
    path = pdrive->pathname_buffer;
    filename = pdrive->filename_buffer;

    /* Get out the filename and d:parent */
    if (!pc_parsepath(path, filename,fileext,old_name, CS_CHARSET_ARGS))
    {
        p_set_errno = PEINVALIDPATH;
        /*rtfs_set_errno(PEINVALIDPATH" __FILE__, __LINE__);*/
        goto errex;
    }
    /* Find the parent and make sure it is a directory  */
    old_parent_obj = pc_fndnode(path, CS_CHARSET_ARGS);
    if (!old_parent_obj)
        goto errex; /* pc_fndinode - set errno */

    if (!pc_isadir(old_parent_obj))
    {
        p_set_errno = PENOENT;
        /*rtfs_set_errno(PENOENT, __FILE__, __LINE__); */
        goto errex;
    }
    /* Find the file */
    old_obj = pc_get_inode(0, old_parent_obj,
        filename, (byte*)fileext, GET_INODE_MATCH, CS_CHARSET_ARGS);
    if (!old_obj)
        goto errex; /* pc_get_inode - set errno */
     /* Be sure it exists and is a normal directory or file and is not open */
    if (pc_isroot(old_obj) || (old_obj->finode->opencount > 1) ||
       (old_obj->finode->fattribute&(ARDONLY|AVOLUME)))
    {
        p_set_errno = PEACCES;
        /*rtfs_set_errno(PEACCES, __FILE__, __LINE__); */
        goto errex;
    }
#if (INCLUDE_EXFATORFAT64)
    if (!ISEXFATORFAT64(pdrive) && old_obj->finode->fattribute & ADIRENT)
#else
    if (old_obj->finode->fattribute & ADIRENT)
#endif
    {
		/* Bug fix November 2009. Previous method failed with unicode because we were passing the ascii ".."
		   and matching on UNICODE. This always failed so you could not rename a directory using the unicode interface.
		   An efficient solution is to pass GET_INODE_DOTDOT for a search command and eliminate string argument preparation and passing. */
        dot_dot_obj = pc_get_inode(0, old_obj, 0, 0, GET_INODE_DOTDOT, CS_CHARSET_ARGS);
        if (!dot_dot_obj)
            goto errex;
    }

    /* At this point old_obj contains the file we are renaming */
    /* See if the new directory entry already exists */
    new_obj = pc_fndnode(new_name, CS_CHARSET_ARGS);
    if (new_obj)
    {
        p_set_errno = PEEXIST;
        /*rtfs_set_errno(PEEXIST, __FILE__, __LINE__); */
        goto errex;
    }
    rtfs_clear_errno();  /* pc_mv - clear errno condition after failed pc_fndnode */

    /* Get out the filename and d:parent */
    if (!pc_parsepath(path,filename,fileext,new_name, CS_CHARSET_ARGS))
    {
        p_set_errno = PEINVALIDPATH;
        /*rtfs_set_errno(PEINVALIDPATH, __FILE__, __LINE__); */
        goto errex;
    }
    /* Find the parent and make sure it is a directory  */
    new_parent_obj = pc_fndnode(path, CS_CHARSET_ARGS);
    if (!new_parent_obj || !pc_isadir(new_parent_obj) ||  pc_isavol(new_parent_obj))
    {
        p_set_errno = PEINVALIDPATH;
        goto errex;
    }

#if (INCLUDE_EXFATORFAT64)
	if (ISEXFATORFAT64(pdrive))
	{	/* Call exfat move routine, it sets errno if an error occurs. */
    	rtfs_clear_errno();
    	p_set_errno = 0;
    	ret_val = pcexfat_mvnode(old_parent_obj,old_obj,new_parent_obj, filename,use_charset);
        goto errex;
	}
    /* Create the new entry and assign cluster to it. If it is a directory
    	.. will be linked correctly */
    new_obj = pc_mknode( new_parent_obj, filename, fileext, old_obj->finode->fattribute, old_obj->finode,CS_CHARSET_ARGS);
#else
	{
	dword cluster;
		/* The cluster value old */
		cluster = pc_finode_cluster(old_obj->pdrive,old_obj->finode);

		/* Create the new entry and assign cluster to it. If it is a directory
    	.. will be linked correctly */
		new_obj = pc_mknode( new_parent_obj, filename, fileext, old_obj->finode->fattribute, cluster,CS_CHARSET_ARGS);
	}
#endif
    if (!new_obj)
        goto errex;

    /* Copy the old directory entry stuf over */
    new_obj->finode->fattribute = old_obj->finode->fattribute;

    new_obj->finode->reservednt = old_obj->finode->reservednt;
    new_obj->finode->create10msincrement = old_obj->finode->create10msincrement;

    new_obj->finode->ftime = old_obj->finode->ftime;
    new_obj->finode->fdate = old_obj->finode->fdate;
    new_obj->finode->ctime = old_obj->finode->ctime;
    new_obj->finode->cdate = old_obj->finode->cdate;
    new_obj->finode->adate = old_obj->finode->adate;
    new_obj->finode->atime = old_obj->finode->atime;
    new_obj->finode->fsizeu.fsize = old_obj->finode->fsizeu.fsize;


    /* Update the new inode. Do not set archive bit or change date */
     if (!pc_update_inode(new_obj, FALSE, 0))
        goto errex;
    if (new_obj->finode->fattribute & ADIRENT)
    {
    dword cltemp;
        /* If we are renaming a directory then update '..' */
        cltemp = pc_get_parent_cluster(pdrive, new_obj);
        dot_dot_obj->finode->fclusterhi = (word)(cltemp >> 16);
        dot_dot_obj->finode->fcluster = (word)cltemp ;
        if (!pc_update_inode(dot_dot_obj, FALSE, 0))
            goto errex;
    }
    /* Set the old cluster value to zero */
    pc_pfinode_cluster(old_obj->pdrive,old_obj->finode,0);
    /* Delete the old but won't delete any clusters */
    if (!pc_rmnode(old_obj))
        goto errex;

    p_set_errno = 0;
    ret_val = TRUE;

    /* Good conditions fall through here, error exits jump to here */
errex:
    if (old_parent_obj)
        pc_freeobj(old_parent_obj);
    if (dot_dot_obj)
        pc_freeobj(dot_dot_obj);
    if (old_obj)
        pc_freeobj(old_obj);
    if (new_parent_obj)
        pc_freeobj(new_parent_obj);
    if (new_obj)
        pc_freeobj(new_obj);
    pc_release_path_buffers(pdrive);
    /* Set errno if we have one and not set by lower level already */
    if ((p_set_errno) && !get_errno())
        rtfs_set_errno(p_set_errno, __FILE__, __LINE__);
    if (!release_drive_mount_write(old_driveno))/* Release lock, unmount if aborted */
        ret_val = FALSE;
    return(ret_val);
}
#endif /* Exclude from build if read only */
