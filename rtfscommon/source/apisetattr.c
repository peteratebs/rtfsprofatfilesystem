/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* APISETATTR.C - Contains pc_set_attributes function */

#include "rtfs.h"

#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */

/*****************************************************************************
    pc_set_attributes - Set File Attributes

 Description
    Given a file or directory name return set directory entry attributes
    associated with the entry.

    The following values may be set:

    BIT Nemonic
    0       ARDONLY
    1       AHIDDEN
    2       ASYSTEM
    5       ARCHIVE

    Note: bits 3 & 4 (AVOLUME,ADIRENT) may not be changed.


 Returns
    Returns TRUE if successful otherwise it returns FALSE.

    errno is set to one of the following
    0               - No error
    PEINVALIDDRIVEID- Drive component is invalid
    PEINVALIDPATH   - Path specified badly formed.
    PENOENT         - Path not found
    PEACCESS        - Object is read only
    PEINVALIDPARMS  - attribute argument is invalid
    An ERTFS system error
****************************************************************************/


#if (INCLUDE_CS_UNICODE)
BOOLEAN pc_set_attributes_cs(byte *path, byte attributes, int use_charset)
#else
BOOLEAN pc_set_attributes(byte *path, byte attributes)
#endif
{
    DROBJ  *pobj;
    BOOLEAN ret_val;
    int driveno;
    CHECK_MEM(BOOLEAN, 0)   /* Make sure memory is initted */

    rtfs_clear_errno();  /* pc_set_attributes: clear error status */
    if ((attributes&(0x40|0x80)) !=0 ) /* Illegal */
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
        /* Change the attributes if legal   */
        if (
              (attributes & (AVOLUME|ADIRENT)) ==   /* Still same type */
              (pobj->finode->fattribute & (AVOLUME|ADIRENT)))
        {
            pobj->finode->fattribute = attributes;
            /* Overwrite the existing inode. Do not set archive/date  */
            /* pc_update_inode() will set errno */
            ret_val = pc_update_inode(pobj, FALSE, 0);
        }
        else
            rtfs_set_errno(PEACCES, __FILE__, __LINE__);
        /*rtfs_set_errno(PEACCES, __FILE__, __LINE__); */

        pc_freeobj(pobj);
    }
    if (!release_drive_mount_write(driveno))/* Release lock, unmount if aborted */
        ret_val = FALSE;
    return (ret_val);
}
#endif /* Exclude from build if read only */
