/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* APISETCWD.C - Contains user api level source code.

    The following routines are included:

    pc_set_cwd      - Set the current working directory.

*/

#include "rtfs.h"

/***************************************************************************
    PC_SET_CWD -  Set the current working directory for a drive.

 Description
    Find path. If it is a subdirectory make it the current working
    directory for the drive.

 Returns
    Returns TRUE if the current working directory was changed.

    errno is set to one of the following
    0               - No error
    PEINVALIDPATH   - Path specified badly formed.
    PENOENT         - Path not found
    PEINVALIDDIR    - Not a directory
    An ERTFS system error
****************************************************************************/


#if (INCLUDE_CS_UNICODE)
BOOLEAN pc_set_cwd_cs(byte *name, int use_charset)
#else
BOOLEAN pc_set_cwd(byte *name)
#endif
{
    DROBJ *pobj;
    int driveno;
    DDRIVE *pdrive;
    DROBJ *parent_obj;
    byte  fileext[4];
    byte  *path, *pfilename, *pfileext;
    BOOLEAN  is_dot, is_dotdot;
    BOOLEAN  ret_val;
    int p_set_errno;
    CHECK_MEM(BOOLEAN, 0)   /* Make sure memory is initted */

    rtfs_clear_errno();  /* pc_set_cwd: clear error status */


    ret_val = FALSE;
    p_set_errno = 0;
    /* Get the drive and make sure it is mounted   */
    driveno = check_drive_name_mount(name, CS_CHARSET_ARGS);
    if (driveno < 0)
    {   /* errno was set by check_drive */
        return(FALSE);
    }

    pdrive = pc_drno2dr(driveno);


    /* Allocate scratch buffers in the DRIVE structure. */
    if (!pc_alloc_path_buffers(pdrive))
        goto errex;
    path = pdrive->pathname_buffer;
    pfilename = pdrive->filename_buffer;

    pfileext = &fileext[0];

#if (INCLUDE_EXFATORFAT64)
	if (ISEXFATORFAT64(pdrive))
	{
		ret_val = pcexfat_set_cwd(pdrive, name, CS_CHARSET_ARGS);
		goto errex;
	}
#endif

     /* Get out the filename and d:parent   */
    if (!pc_parsepath(path, pfilename,pfileext,name, CS_CHARSET_ARGS))
    {
        p_set_errno = PEINVALIDPATH;
        /*rtfs_set_errno(PEINVALIDPATH, __FILE__, __LINE__); */
        goto errex;
    }


    /* Find the parent and make sure it is a directory    */
    parent_obj = pc_fndnode(path, CS_CHARSET_ARGS);
    if (!parent_obj)
        goto errex; /* pc_fndnode set errno */

    if (!pc_isadir(parent_obj))
    {
        p_set_errno = PEACCES;      /* Path is not a directory */
        /*rtfs_set_errno(PEACCESS, __FILE__, __LINE__); Path is not a directory */
        goto errex;
    }


    is_dot=is_dotdot=FALSE;
    if (CS_OP_CMP_ASCII(pfilename,'.', CS_CHARSET_ARGS))
    {
    byte  *pfilename_plus_1;
        pfilename_plus_1 =  pdrive->filename_buffer;
        CS_OP_INC_PTR(pfilename_plus_1, CS_CHARSET_ARGS);
        if (CS_OP_IS_EOS(pfilename_plus_1, CS_CHARSET_ARGS) || CS_OP_CMP_ASCII(pfilename_plus_1,' ', CS_CHARSET_ARGS))
            is_dot=TRUE;
        else if (CS_OP_CMP_ASCII(pfilename_plus_1,'.', CS_CHARSET_ARGS))
        {
            CS_OP_INC_PTR(pfilename_plus_1, CS_CHARSET_ARGS);
            if (CS_OP_IS_EOS(pfilename_plus_1, CS_CHARSET_ARGS) || CS_OP_CMP_ASCII(pfilename_plus_1,' ', CS_CHARSET_ARGS))
                is_dotdot=TRUE;
        }
    }
    /* Get the directory   */
    /* April 2012 Fixed bug that skipped the directory entry when the first charcter is a space */
    /* Wrong if (CS_OP_CMP_ASCII(pfilename,'\0', CS_CHARSET_ARGS) || CS_OP_CMP_ASCII(pfilename,' ', CS_CHARSET_ARGS))*/
    if (CS_OP_CMP_ASCII(pfilename,'\0', CS_CHARSET_ARGS))
    {
        pobj = parent_obj;
    }
    else if (is_dotdot)
    {
        if (pc_isroot(parent_obj))
            pobj = parent_obj;
        else
        {
            pobj = pc_get_inode(0, parent_obj, 0, 0, GET_INODE_DOTDOT, CS_CHARSET_ARGS);
            /* If the request is cd .. then we just found the .. directory entry
                we have to call get_mom to access the parent. */
            pc_freeobj(parent_obj);
            if (!pobj)  /* pc_get_inode() has set errno to PENOENT or to an internal or IO error status */
                goto errex;
            parent_obj = pobj;
            /* Find parent_objs parent. By looking back from ..   */
            pobj = pc_get_mom(parent_obj);
            pc_freeobj(parent_obj);
            if (!pobj)
            {   /* if pc_get_mom() set errno, use it otherwise set PENOENT */
                if (!get_errno())
                    p_set_errno = PENOENT;      /* Not found */
                /*rtfs_set_errno(PENOENT, __FILE__, __LINE__); */
                goto errex;
            }
        }
    }
    else if (is_dot)
    {
        pobj = parent_obj;
    }
    else
    {
        pobj = pc_get_inode(0, parent_obj, pfilename, pfileext, GET_INODE_MATCH, CS_CHARSET_ARGS);
        pc_freeobj(parent_obj);
    }
    if (!pobj)
    {
        /* pc_get_inode set errno */
        goto errex;
    }
    else if (!pc_isadir(pobj))
    {
        pc_freeobj(pobj);
        p_set_errno = PENOENT;      /* Path is not a directory */
        /*rtfs_set_errno(PENOENT, __FILE__, __LINE__); */
        goto errex;
    }
    driveno = pobj->pdrive->driveno;
	{
		DROBJ *ptemp;
		RTFS_SYSTEM_USER *pu;
		pu = rtfs_get_system_user();
		ptemp = rtfs_get_user_pwd(pu, driveno, TRUE);	  /* Get cwd for driveno and clear it */
		if (ptemp)
		{
        	pc_freeobj(ptemp);							  /* Free it */
		}
		rtfs_set_user_pwd(pu, pobj);  /* Set cwd to pobj */
	}
    ret_val = TRUE;
errex:
    pc_release_path_buffers(pdrive);
    release_drive_mount(driveno);/* Release lock, unmount if aborted */
    if (p_set_errno)
        rtfs_set_errno(p_set_errno, __FILE__, __LINE__);
    return(ret_val);
}
