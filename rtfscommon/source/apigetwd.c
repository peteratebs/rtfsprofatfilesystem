/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* APIGETWD.C - Contains user api level source code.

    The following routines are included:

    pc_get_cwd          - Get string representation of current working dir.

*/

#include "rtfs.h"

/*****************************************************************************
    PC_get_cwd -  Return a string containing the current working directory for
                a drive.

 Description
    Fill in a string with the full path name of the current working directory.
    Return FALSE if something went wrong.
    If *drive is null or an invalid drive specifier the default drive is used.


 Returns
    Returns the path name in path. The function returns TRUE on success
    no on failure.

    errno is set to one of the following
    0               - No error
    PEINVALIDDRIVEID- Drive component is invalid
    An ERTFS system error
****************************************************************************/
static BOOLEAN pc_l_pwd(byte *path, DROBJ *pobj, int use_charset);
static BOOLEAN pc_gm_name(byte *path, DROBJ *parent_obj, DROBJ *pdotdot, int use_charset);
static BOOLEAN _pc_pwd(byte *drive, byte *path, int use_charset);

/* RtfsProPlus uses pc_get_cwd() while Pro uses pc_pwd() - Provide both */
#if (INCLUDE_CS_UNICODE)
BOOLEAN pc_get_cwd_cs(byte *drive, byte *path, int use_charset)
{
    return(_pc_pwd(drive, path,CS_CHARSET_ARGS));
}
BOOLEAN pc_pwd_cs(byte *drive, byte *path, int use_charset)
{
    return(_pc_pwd(drive, path,CS_CHARSET_ARGS));
}
#else
BOOLEAN pc_get_cwd(byte *drive, byte *path)
{
    return(_pc_pwd(drive, path,CS_CHARSET_ARGS));
}
BOOLEAN pc_pwd(byte *drive, byte *path)
{
    return(_pc_pwd(drive, path,CS_CHARSET_ARGS));
}
#endif

static BOOLEAN _pc_pwd(byte *drive, byte *path, int use_charset)
{
    int  driveno;
    DDRIVE *pdrive;
    DROBJ *pobj;
    BOOLEAN ret_val;
    CHECK_MEM(BOOLEAN, 0)   /* Make sure memory is initted */


    ret_val = FALSE;
    rtfs_clear_errno(); /* pc_pwd: clear error status */
    /* Get the drive and make sure it is mounted   */
    driveno = check_drive_name_mount(drive, use_charset);
    if (driveno < 0)
    {
        /* errno was set by check_drive */
        goto return_error;
    }

    /* Find the drive   */
    pdrive = pc_drno2dr(driveno);

    if (pdrive)
    {
#if (INCLUDE_EXFATORFAT64)
   		if (ISEXFATORFAT64(pdrive) )
   		{
   			ret_val = pcexfat_get_cwd(pdrive, path, use_charset);
   		}
   		else
#endif
		{
       		pobj = _pc_get_user_cwd(pdrive);
       		/* _pc_get_user_cwd sets errno */
       		if (pobj)
       		{
            	ret_val = pc_l_pwd(path, pobj, use_charset);
       		}
		}
	}
    release_drive_mount(driveno);/* Release lock, unmount if aborted */

return_error:   /* Does not have to be an error to get here */
    return(ret_val);
}

static BOOLEAN pc_l_pwd(byte *path, DROBJ *pobj, int use_charset)                         /*__fn__*/
{
#define OBJDEPTH 32
    DROBJ *parentlist[OBJDEPTH]; /* List of drobjs to the top */
    DROBJ *dotdotlist[OBJDEPTH]; /* List of drobjs to the top */
    int   objcnt;
    BOOLEAN ret_val;
    int i;
    DROBJ *tpobj;
    ret_val = FALSE;

    /* If it is the root we are done   */
    if (pc_isroot(pobj))
    {
        CS_OP_ASSIGN_ASCII(path,'\\', use_charset);
        CS_OP_INC_PTR(path, use_charset);
        CS_OP_TERM_STRING(path, use_charset);
        pc_freeobj(pobj);
        return(TRUE);
    }

    /* Build a list of drobjs for directories and a drobj for the .. in
       the directories */
    tpobj = pobj;
    /* Zero the object lists in case we terminate incorrectly */
    for (objcnt = 0; objcnt < OBJDEPTH; objcnt++)
        dotdotlist[objcnt] = parentlist[objcnt] = 0;

    for (objcnt = 0; objcnt < OBJDEPTH; objcnt++)
    {
        /* get dotdot   */
        tpobj = pc_get_inode(0, tpobj, 0, 0, GET_INODE_DOTDOT,use_charset);
        dotdotlist[objcnt] = tpobj;
        if (!tpobj)
            goto clean_and_go;
        /* Get the parent directory   */
        tpobj = pc_get_mom(tpobj);
        parentlist[objcnt] = tpobj;
        if (!tpobj)
            goto clean_and_go;
        if (pc_isroot(tpobj))
            break;
    }

    if (objcnt >= OBJDEPTH)
    {
        objcnt = OBJDEPTH-1;
        goto clean_and_go;
    }

    /* Start at the root and work backwards in the list of parents getting
       the names. (note: the get name algorithm needs the parent of the
       current directory and the .. of the directory */
    for (i = objcnt; i >= 0; i--)
    {
        CS_OP_ASSIGN_ASCII(path,'\\', use_charset);
        CS_OP_INC_PTR(path,use_charset);
        CS_OP_TERM_STRING(path,use_charset);
        if (!pc_gm_name(path , parentlist[i],dotdotlist[i], use_charset))
            goto clean_and_go;
        path = CS_OP_GOTO_EOS(path,use_charset);
    }
    ret_val = TRUE;

clean_and_go:
    for (i = objcnt; i >= 0; i--)
    {
        if (parentlist[i])
            pc_freeobj(parentlist[i]);
        if (dotdotlist[i])
            pc_freeobj(dotdotlist[i]);
    }
    pc_freeobj(pobj);

    return(ret_val);
}

static BOOLEAN pc_gm_name(byte *path, DROBJ *parent_obj, DROBJ *pdotdot, int use_charset)  /*__fn__*/
{
    DROBJ *pchild;
    dword clusterno;
    dword fcluster;
    BOOLEAN ret_val;

    ret_val = FALSE;

    clusterno = pc_sec2cluster(pdotdot->pdrive, pdotdot->blkinfo.my_frstblock);
    pchild = pc_get_inode(0, parent_obj, 0, 0, GET_INODE_STAR,use_charset);
    if (pchild)
    {
        do
        {
            fcluster = pc_finode_cluster(pdotdot->pdrive,pchild->finode);
            if (fcluster == clusterno)
            {
                /* Get a long file name if none revert to the 8.3 name */
                if (!pc_get_lfn_filename(pchild,path, use_charset))
                   pc_cs_mfile (path, pchild->finode->fname, pchild->finode->fext, use_charset);
               ret_val = TRUE;
               break;
            }
        }
        while (pc_get_inode(pchild, parent_obj, 0, 0, GET_INODE_STAR,use_charset));
    }

    if (pchild)
    {
        pc_freeobj(pchild);
    }

    return(ret_val);
}
