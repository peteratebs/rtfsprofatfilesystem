/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* APIDIRENT.C - Contains directory access functions and change functions */

#include "rtfs.h"
#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */

/*****************************************************************************
    pc_get_dirent_info - Retrieve low level directory entry information

 Description

    Given a file or directory name and a dirent_info buffer fill the buffer with low level
    directory entry information. This structure can be examined and it can be modified
    and passed to pc_set_dirent_info to change the entry.

    The dirent_info structure is:

typedef struct dirent_info {
    byte     fattribute;
    dword    fcluster;
    word     ftime;
    word     fdate;
    dword    fsize;
    dword    my_block;
    int      my_index;
} DIRENT_INFO;


 Returns
    Returns TRUE if successful otherwise it returns FALSE.

****************************************************************************/



#if (INCLUDE_CS_UNICODE)
BOOLEAN pc_get_dirent_info_cs(byte *path, DIRENT_INFO *pinfo, int use_charset)
#else
BOOLEAN pc_get_dirent_info(byte *path, DIRENT_INFO *pinfo)
#endif
{
    DROBJ  *pobj;
    BOOLEAN ret_val;
    int driveno;
    CHECK_MEM(BOOLEAN, 0)   /* Make sure memory is initted */

    rtfs_clear_errno();  /* pc_set_attributes: clear error status */
    if (!path || !pinfo)
    {
        rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
        /*rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__); */
        return(FALSE);
    }
    driveno = check_drive_name_mount(path, CS_CHARSET_ARGS);
    if (driveno < 0)
    {  /* errno was set by check_drive */
        return (FALSE);
    }
    ret_val = FALSE;
    /* pc_fndnode will set errno */
    pobj = pc_fndnode(path, CS_CHARSET_ARGS);
    if (pobj)
    {
        pinfo->fattribute = pobj->finode->fattribute;
        pinfo->fcluster = pc_finode_cluster(pobj->pdrive, pobj->finode);
        pinfo->ftime = pobj->finode->ftime;
        pinfo->fdate = pobj->finode->fdate;
#if (INCLUDE_EXFATORFAT64)
		if (ISEXFATORFAT64(pobj->pdrive))
             pinfo->fsize64 = pobj->finode->fsizeu.fsize64;
		else
#endif
			pinfo->fsize = pobj->finode->fsizeu.fsize;
        pinfo->my_block = pobj->finode->my_block;
        pinfo->my_index = pobj->finode->my_index;
        ret_val = TRUE;
        pc_freeobj(pobj);
    }
    if (!release_drive_mount_write(driveno))/* Release lock, unmount if aborted */
        ret_val = FALSE;
    return (ret_val);
}

/*****************************************************************************
    pc_set_dirent_info - Change low level directory entry information

 Description

    Given a file or directory name and a dirent_info buffer, update the on disk directory
    with the values in dirent_info structure is.

    To use this function you should first call pc_get_dirent_info, to retrieve low level directory entry information,
    then change the fields you wish to modify and then call the function to apply the changes.


    For example, to move the contents from "Fila A" to "File B" you could perform the following.

    DIRENT_INFO fileainfo, filebinfo;

        pc_get_dirent_info("File A", &fileainfo);
        pc_get_dirent_info("File B", &filebinfo);
        fileainfo.fcluster =  filebinfo.fcluster;
        fileainfo.fsize    =  filebinfo.fcfsize;
        fileainfo.fcluster =  0;
        fileainfo.fsize    =  0;
        pc_set_dirent_info("File A", &fileainfo);
        pc_set_dirent_info("File B", &filebinfo);

    The entries in the dirent_info structure that may be changed by pc_set_dirent_info are:
        fattribute,fcluster,ftime and fdate;

    Note: This is a very low level function that could cause serious problems if used incorrectly.

 Returns
    Returns TRUE if successful otherwise it returns FALSE.

****************************************************************************/


#if (INCLUDE_CS_UNICODE)
BOOLEAN pc_set_dirent_info_cs(byte *path, DIRENT_INFO *pinfo, int use_charset)
#else
BOOLEAN pc_set_dirent_info(byte *path, DIRENT_INFO *pinfo)
#endif
{
    DROBJ  *pobj;
    BOOLEAN ret_val;
    int driveno;
    CHECK_MEM(BOOLEAN, 0)   /* Make sure memory is initted */

    rtfs_clear_errno();  /* pc_set_attributes: clear error status */
    if (!path || !pinfo)
    {
        rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
        /*rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__); */
        return(FALSE);
    }
    driveno = check_drive_name_mount(path, CS_CHARSET_ARGS);
    if (driveno < 0)
    {  /* errno was set by check_drive */
        return (FALSE);
    }
    ret_val = FALSE;
    /* pc_fndnode will set errno */
    pobj = pc_fndnode(path, CS_CHARSET_ARGS);
    if (pobj)
    {
     /* Be sure it exists and is a normal directory or file and is not open */
        if (pc_isroot(pobj) || (pobj->finode->opencount > 1))
        {
            rtfs_set_errno(PEACCES, __FILE__, __LINE__);
        }
        else
        {
            pobj->finode->fattribute = pinfo->fattribute;
            pc_pfinode_cluster(pobj->pdrive, pobj->finode, pinfo->fcluster);
            pobj->finode->ftime = pinfo->ftime;
            pobj->finode->fdate = pinfo->fdate;

#if (INCLUDE_EXFATORFAT64)
		    if (ISEXFATORFAT64(pobj->pdrive))
                pobj->finode->fsizeu.fsize64 = pinfo->fsize64;
		    else
#endif
                pobj->finode->fsizeu.fsize = pinfo->fsize;
            /* Now flush the changed directory entry */
            if (pc_update_inode(pobj, FALSE, 0))
                ret_val = TRUE;
        }
        pc_freeobj(pobj);
    }
    if (!release_drive_mount_write(driveno))/* Release lock, unmount if aborted */
        ret_val = FALSE;
    return (ret_val);
}

#endif /* Exclude from build if read only */
