/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* APIDELTR.C - Contains user api level source code.

  The following routines are included:

    pc_deltree      - Delete an entire directory tree.
*/

#include "rtfs.h"
#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */

/****************************************************************************
PC_DELTREE - Delete a directory tree.

  Description

    Delete the directory specified in name, all subdirectories of that
    directory, and all files contained therein. Fail if name is not a
    directory, or is read only.

      Returns
      Returns TRUE if the directory was successfully removed.

        errno will be set to one of these values

          0               - No error
          PEINVALIDDRIVEID- Drive component of path is invalid
          PEINVALIDPATH   - Path specified by name is badly formed.
          PENOENT         - Can't find path specified by name.
          PEACCES         - Directory or one of its subdirectories is read only or
          in use.
          An ERTFS system error

*****************************************************************************/

/* Remove a directory   */

#if (INCLUDE_CS_UNICODE)
BOOLEAN pc_deltree_cs(byte  *name, int use_charset)
#else
BOOLEAN pc_deltree(byte  *name)
#endif
{
    DROBJ *parent_obj;
    DROBJ *pobj;
    DROBJ *pdotdot;
    DROBJ *pchild;
    BOOLEAN is_exfat = FALSE;
    BOOLEAN ret_val;
    DDRIVE *pdrive;
    byte  *path;
    byte  *filename;
    byte  fileext[4];
    int driveno;
    int p_set_errno;
    int dir_depth;
	DROBJ **dirstack = 0;
    BLKBUFF *dirstack_buffer = 0;
    int exfat_max_depth = 0;
    CHECK_MEM(BOOLEAN, 0)   /* Make sure memory is initted */

    parent_obj = 0;
    pchild = 0;
    pobj = 0;
    pdotdot = 0;
    ret_val = FALSE;
    dir_depth = 1;

    p_set_errno = 0;
    rtfs_clear_errno();  /* pc_deltree: clear error status */
    /* Get the drive and make sure it is mounted   */
    driveno = check_drive_name_mount(name, CS_CHARSET_ARGS);
    if (driveno < 0)
    { /* pc_deltree: errno was by check_drive */
        return(FALSE);
    }
    pdrive = pc_drno2dr(driveno);

#if (INCLUDE_EXFATORFAT64)
   	if ( ISEXFATORFAT64(pdrive) )
	{
    	is_exfat = TRUE;
    	dirstack_buffer = pc_scratch_blk();

    	if (!dirstack_buffer)
		{
			ret_val = FALSE;
            goto errex;
		}
		dirstack = (DROBJ **)dirstack_buffer->data;
		rtfs_memset(dirstack,0,512);
		exfat_max_depth = 512/sizeof(DROBJ *);
	}
#endif
    /* Allocate scratch buffers in the DRIVE structure. */
    if (!pc_alloc_path_buffers(pdrive))
        goto errex;
    path = pdrive->pathname_buffer;
    filename = pdrive->filename_buffer;

    /* Get out the filename and d:parent   */
    if (!pc_parsepath(path,filename,fileext,name, CS_CHARSET_ARGS))
    {
        p_set_errno = PEINVALIDPATH;
        /*rtfs_set_errno(PEINVALIDPATH, __FILE__, __LINE__ */
        goto errex;
    }
    /* Find the parent and make sure it is a directory \   */
    parent_obj = pc_fndnode(path, CS_CHARSET_ARGS);
    if (!parent_obj)
        goto errex;     /* pc_fndnode set errno */

    if (!pc_isadir(parent_obj))
    {
        p_set_errno = PENOENT;
        /*rtfs_set_errno(PENOENT, __FILE__, __LINE__ */
        goto errex;
    }
    /* Find the file and init the structure   */
    pobj = pc_get_inode(0, parent_obj, filename, (byte*)fileext, GET_INODE_MATCH, CS_CHARSET_ARGS);
    if (!pobj)
        goto errex; /* pc_get_inode set errno */

    if ( !pc_isadir(pobj) || (pobj->finode->opencount > 1) ||
            (pobj->finode->fattribute & ARDONLY ))
    {
        p_set_errno = PEACCES;
        /*rtfs_set_errno(PEACCES, __FILE__, __LINE__); */
        goto errex;
    }

    /* Search through the directory. look at all files   */
    /* Call pc_get_inode with 0 to give us an obj     */
	if (is_exfat)
		dirstack[dir_depth] = pobj;
    while (dir_depth > 0)
    {
        if (pchild)
            pc_freeobj(pchild);
        pchild = pc_get_inode(0, pobj, 0 , 0, GET_INODE_STAR, CS_CHARSET_ARGS);
        if (pdotdot) {
            pc_freeobj(pdotdot);
            pdotdot = 0;
        }

        if (pchild)
        {
            do
            {
            /* delete all nodes which are not subdirs;
                step into all subdirs and destroy their contents. */
                if (!(pc_isdot(pchild->finode->fname, pchild->finode->fext) ) )
                {
                    if (!(pc_isdotdot(pchild->finode->fname, pchild->finode->fext) ) )
                    {
                        if (pc_isadir(pchild))
                        {
                            if ( (pchild->finode->opencount > 1) ||
                                 (pchild->finode->fattribute&ARDONLY) )
                            {
                                p_set_errno = PEACCES;
                                /*rtfs_set_errno(PEACCES, __FILE__, __LINE__); */
                                ret_val = FALSE;
                                goto errex;
                            }
                           	dir_depth++;
							if (is_exfat)
							{
								if (dir_depth >= exfat_max_depth)
								{
									p_set_errno = PEINVALIDPATH;
									goto errex; /* pc_get_inode set errno */
								}
								dirstack[dir_depth] = pchild;
							}
							else
                            	pc_freeobj(pobj);
                            pobj = pchild; /* enter first subdir */
                            pchild = 0;
                            goto start_over;
                        }
                        else
                        {
                        /* Be sure it is not the root. Since the root is an abstraction
                            we can not delete it plus Check access permissions */
                            if ( pc_isroot(pchild) || (pchild->finode->opencount > 1) ||
                                (pchild->finode->fattribute&(ARDONLY|AVOLUME|ADIRENT)))
                            {
                                p_set_errno = PEACCES;
                                /*rtfs_set_errno(PEACCES, __FILE__, __LINE__); */
                                ret_val = FALSE;
                                goto errex;
                            }
                            else
                            {
                            /* Remove the file */
                            /* calculate max number of clusters to release.
                                Add bytespcluster-1 in case we are not on a cluster boundary */
                                ret_val = pc_rmnode(pchild);
                                if (!ret_val)
                                    goto errex; /* pc_rmnode sets errno */

                                goto start_over;
                            }
                        }
                    }
                    else
                    {
                        if (pdotdot)
                            pc_freeobj(pdotdot);
                        pdotdot = pc_get_mom(pchild);
                    }
                }
            }
            while (pc_get_inode(pchild, pobj, 0 , 0, GET_INODE_STAR, CS_CHARSET_ARGS));
        }

		/* When we get here we just processed a leaf that has no files.
		   Also it is the first entry in it's parent directory, so we pop up one level
		   and remove the first entry.
		*/
        if (get_errno() != PENOENT)
           goto errex; /* pc_get_inode set errno */

        /* dir empty; step out and delete */
        if (pobj) {
            pc_freeobj(pobj);
            pobj = 0;
        }
        if (pchild) {
            pc_freeobj(pchild);
            pchild = 0;
        }
        dir_depth--;
		if (is_exfat)
		{
			pdotdot = dirstack[dir_depth];
		}
        if (dir_depth > 0)
        {
            if (!pdotdot)
            {
                p_set_errno = PEINVALIDCLUSTER;
                /*rtfs_set_errno(PEINVALIDCLUSTER, __FILE__, __LINE__); */
                goto errex;
            }
            pchild = pc_get_inode(0, pdotdot, 0 , 0, GET_INODE_STAR, CS_CHARSET_ARGS);
            if (pchild)
            {
                do
                {
                    if (!(pc_isdot(pchild->finode->fname, pchild->finode->fext) ) )
                    {
                        if (!(pc_isdotdot(pchild->finode->fname, pchild->finode->fext) ) )
                        {
                            ret_val = pc_rmnode(pchild); /* remove the directory */

                            if (!ret_val)
                                goto errex; /* pc_mnode set errno */
							if (is_exfat)
							{
								int ldir_depth;
								for (ldir_depth = 0; ldir_depth < exfat_max_depth; ldir_depth++)
									if (dirstack[ldir_depth] == pchild)
									{
										pc_freeobj(dirstack[ldir_depth]);
										dirstack[ldir_depth] = 0;
										break;
									}
							}
							break;
                        }
                    }
                } while (pc_get_inode(pchild, pdotdot, 0, 0, GET_INODE_STAR, CS_CHARSET_ARGS));
            }
            else
            {
                if (get_errno() != PENOENT)
                    goto errex; /* pc_get_inode set errno */
            }
            pobj = pdotdot;
            pdotdot = 0;
        }
        else
        {

            pobj = pc_get_inode(0, parent_obj, filename, (byte*)fileext, GET_INODE_MATCH, CS_CHARSET_ARGS);
            if (!pobj)
                goto errex; /* pc_mnode set errno */
            ret_val = pc_rmnode(pobj); /* Remove the directory */
            if (!ret_val)
                goto errex; /* pc_mnode set errno */
        }
start_over:
            ;
    }
errex:
    if (dirstack_buffer)
    	pc_free_scratch_blk(dirstack_buffer);
    if (pdotdot)
        pc_freeobj(pdotdot);
    if (pchild)
        pc_freeobj(pchild);
    if (pobj)
        pc_freeobj(pobj);
    if (parent_obj)
        pc_freeobj(parent_obj);
    pc_release_path_buffers(pdrive);

	if (is_exfat)
	{
    	for (dir_depth = 0; dir_depth < exfat_max_depth; dir_depth++)
		{
			if (dirstack[dir_depth])
			{
				pc_freeobj(dirstack[dir_depth]);
				dirstack[dir_depth] = 0;
			}
		}
	}
    if (ret_val)
        rtfs_clear_errno();
    else if (p_set_errno)
        rtfs_set_errno(p_set_errno,__FILE__, __LINE__);
    if (!release_drive_mount_write(driveno))/* Release lock, unmount if aborted */
        ret_val = FALSE;
    return(ret_val);
}
#endif /* Exclude from build if read only */
