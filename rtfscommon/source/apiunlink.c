/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2007
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* APIUNLINK.C - Contains user api level file IO source code.

    PC_UNLINK - Delete a file.
*/

#include "rtfs.h"
#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */

/****************************************************************************
    PC_UNLINK - Delete a file.

 Description
    Delete the file in name. Fail if not a simple file,if it is open,
    does not exist or is read only.

 Returns
    Returns TRUE if it successfully deleted the file.

    errno is set to one of the following
    0               - No error
    PEINVALIDDRIVEID- Drive component is invalid
    PEINVALIDPATH   - Path specified badly formed.
    PENOENT         - Can't find file to delete
    PEACCESS        - File in use, is read only or is not a simple file.
    An ERTFS system error
***************************************************************************/

/* Delete a file   */
#if (INCLUDE_CS_UNICODE)
BOOLEAN pc_unlink_cs(byte *name, int use_charset)
#else
BOOLEAN pc_unlink(byte *name)
#endif
{
    DROBJ *pobj;
    DROBJ *parent_obj;
    BOOLEAN ret_val;
    DDRIVE *pdrive;
    byte  *path;
    byte  *filename;
    byte  fileext[4];
    int driveno;
    int p_errno;
    CHECK_MEM(BOOLEAN, 0)   /* Make sure memory is initted */

    ret_val = FALSE;
    parent_obj  = 0;
    pobj        = 0;
    p_errno = 0;
    rtfs_clear_errno();  /* pc_unlink: clear error status */

    /* Get the drive and make sure it is mounted   */
    driveno = check_drive_name_mount(name, CS_CHARSET_ARGS);
    if (driveno < 0)
    {
        /* errno was set by check_drive */
        return(FALSE);
    }

    pdrive = pc_drno2dr(driveno);
    /* Allocate scratch buffers in the DRIVE structure. */
    if (!pc_alloc_path_buffers(pdrive))
        goto errex;
    path = pdrive->pathname_buffer;
    filename = pdrive->filename_buffer;

    /* Get out the filename and d:parent   */
    if (!pc_parsepath(path, filename,fileext,name, CS_CHARSET_ARGS))
    {
        p_errno = PEINVALIDPATH;
        goto errex;
    }

    /* Find the parent and make sure it is a directory    */
    parent_obj = pc_fndnode(path, CS_CHARSET_ARGS);
    if (!parent_obj)
        goto errex;     /* pc_fndnode set errno */
    if (!pc_isadir(parent_obj) ||  pc_isavol(parent_obj))
    {
        p_errno = PEACCES;
        goto errex;
    }

    /* Find the file   */
    pobj = pc_get_inode(0, parent_obj, filename, (byte*)fileext, GET_INODE_MATCH, CS_CHARSET_ARGS);
    /* if pc_get_inode() fails it sets errno to PENOENT or to an internal or IO error status */
    if (pobj)
    {

        if (pc_isroot(pobj) || (pobj->finode->opencount > 1) ||
            (pobj->finode->fattribute&(ARDONLY|AVOLUME)))
        {
access_error:
            p_errno = PEACCES;
            ret_val = FALSE;
            goto errex;
        }
        if (pobj->finode->fattribute&ADIRENT)
        {
            goto access_error;
        }
        else
        {   /* pc_rmnode sets errno */
            ret_val = pc_rmnode(pobj);
        }
    }

errex:
    pc_release_path_buffers(pdrive);
    if (pobj)
        pc_freeobj(pobj);
    if (parent_obj)
    {
        pc_freeobj(parent_obj);
    }
    /* Set errno if we have one and not set by lower level already */
    if (ret_val)
        rtfs_clear_errno();
    else if ((p_errno) && !get_errno())
        rtfs_set_errno(p_errno, __FILE__, __LINE__);
    if (!release_drive_mount_write(driveno))/* Release lock, unmount if aborted */
        ret_val = FALSE;
    return(ret_val);
}
#endif /* Exclude from build if read only */
