/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* APIINFO.C - Contains user api level source code.

    The following routines are included:

    pc_set_default_drive - Set the default drive number.
    pc_free         - Calculate and return the free space on a disk.
    pc_isdir        -   Determine if a path is a directory.
    pc_isvol        -   Determine if a path is a volume
    pc_get_attributes - Get File Attributes
    pc_getdfltdrvno - Get the default drive number.
*/

#include "rtfs.h"

/***************************************************************************
    PC_SET_DEFAULT_DRIVE - Set the current default drive.

 Description
    Use this function to set the current default drive that will be used
    when a path specifier does not contain a drive specifier.
    Note: The default default is zero (drive A:)


 Returns
    Return FALSE if the drive is out of range.

    errno is set to one of the following
     0                - No error
     PEINVALIDDRIVEID - Driveno is incorrect
****************************************************************************/

/* Set the currently stored default drive       */
BOOLEAN pc_set_default_drive(byte *drive)        /*__apifn__*/
{
int drive_no;

    rtfs_clear_errno();  /* pc_set_default_drive: clear error status */
    /* get drive no   */
    drive_no = pc_parse_raw_drive(drive, CS_CHARSET_NOT_UNICODE);
    if ( ( drive_no < 0) || !pc_validate_driveno(drive_no))
    {
        rtfs_set_errno(PEINVALIDDRIVEID, __FILE__, __LINE__);/* pc_set_default_drive: invalid argument */
        return(FALSE);
    }
    else
    {
        rtfs_get_system_user()->dfltdrv = drive_no+1;
        return(TRUE);
    }
}

/****************************************************************************
    PC_FREE - Count the number of free bytes remaining on a disk

 Description
    Given a path containing a valid drive specifier count the number
    of free bytes on the drive. The function also takes two additional
    argument that point to location that will be loaded with the
    total number of blocks on the drive and the total number of
    free clusters


 Returns
    The number of free bytes or zero if the drive is full, -1 if not open,
    or out of range.

    dword *blocks_total - Contains the total block count
    dword *blocks_free  - Contain the total count of free blocks.

    errno is set to one of the following
    0                - No error
    PEINVALIDDRIVEID - Driveno is incorrect
    An ERTFS system error
*****************************************************************************/


/* RTFS Pro version of pc_free returns long.. not a good solution */
long pc_free(byte *path, dword *blocks_total, dword *blocks_free)  /*__apifn__*/
{
    if (!pc_blocks_free(path, blocks_total, blocks_free))
        return(-1);
    else
        return((long)(*blocks_free * pc_sector_size(path)));
}


/* Return # blocks free on a drive   */
BOOLEAN pc_blocks_free(byte *path, dword *blocks_total, dword *blocks_free)  /*__apifn__*/
{
    int driveno;
    DDRIVE  *pdr;
    BOOLEAN ret_val;
    CHECK_MEM(BOOLEAN, 0)  /* Make sure memory is initted */


    rtfs_clear_errno();  /* po_free: clear error status */

    /* assume failure to start   */
    ret_val = FALSE;
    /* Get the drive and make sure it is mounted   */
    driveno = check_drive_name_mount(path, CS_CHARSET_NOT_UNICODE);
    /* if error check_drive errno was set by check_drive */
    if (driveno >= 0)
    {
        pdr = pc_drno2dr(driveno);
        if (!pdr)
        {
            rtfs_set_errno(PEINVALIDDRIVEID, __FILE__, __LINE__); /* pc_free: no valid drive present */
        }
        else
        {
            *blocks_free = pdr->drive_info.known_free_clusters;
            *blocks_free *= pdr->drive_info.secpalloc;          /* Size of each fat */
            *blocks_total = pdr->drive_info.maxfindex - 1;      /* Number of entries in the fat */
            *blocks_total *= pdr->drive_info.secpalloc;        /* Size of each fat */
            /* Return number of free sectors */
            ret_val = TRUE;
        }
        release_drive_mount(driveno);/* Release lock, unmount if aborted */
    }
    return(ret_val);
}

/*****************************************************************************
    PC_ISDIR - Test if a path name is a directory

 Description
    Test to see if a path specification ends at a subdirectory or a
    file.
    Note: \ is a directory.

 Returns
    Returns TRUE if it is a directory.
****************************************************************************/


#define ISDIR 1
#define ISVOL 2

static BOOLEAN pc_is(int op, byte *path, int use_charset)                                   /*__fn__*/
{
    DROBJ  *pobj;
    BOOLEAN ret_val = FALSE;
    int driveno;
    CHECK_MEM(BOOLEAN, 0)   /* Make sure memory is initted */

    rtfs_clear_errno(); /* pc_isdir/pc_isvol: clear errno */

    /* Get the drive and make sure it is mounted   */
    driveno = check_drive_name_mount(path, use_charset);
    /* if check_drive failed errno was set by check_drive */
    if (driveno >= 0)
    {
        pobj = pc_fndnode(path, use_charset);
        /* pc_isdir/pc_isvol: if pc_fndnode fails it will set errno */
        if (pobj)
        {
            if (op == ISDIR)
                ret_val = pc_isadir(pobj);
            else if (op == ISVOL)
                ret_val = pc_isavol(pobj);
            pc_freeobj(pobj);
        }
        release_drive_mount(driveno);/* Release lock, unmount if aborted */
    }
    return (ret_val);
}


/*****************************************************************************
    PC_ISDIR - Test if a path name is a directory

 Description
    Test to see if a path specification ends at a subdirectory.

 Returns
    Returns TRUE if it is a directory.

    errno is set to one of the following
    0                - No error
    PENOENT          - Path not found
    An ERTFS system error
****************************************************************************/

#if (INCLUDE_CS_UNICODE)
BOOLEAN pc_isdir_cs(byte *path, int use_charset) /*__apifn__*/
{
    return(pc_is(ISDIR, path,CS_CHARSET_ARGS));
}
#else
BOOLEAN pc_isdir(byte *path) /*__apifn__*/
{
    return(pc_is(ISDIR, path,CS_CHARSET_ARGS));
}
#endif

/*****************************************************************************
    PC_ISVOL - Test if a path name is a volume entry

 Description
    Test to see if a path specification ends at a volume label

 Returns
    Returns TRUE if it is a volume

    errno is set to one of the following
    0                - No error
    PENOENT          - Path not found
    An ERTFS system error
****************************************************************************/


#if (INCLUDE_CS_UNICODE)
BOOLEAN pc_isvol_cs(byte *path, int use_charset)  /*__apifn__*/
{
    return(pc_is(ISVOL, path,CS_CHARSET_ARGS));
}
#else
BOOLEAN pc_isvol(byte *path)  /*__apifn__*/
{
    return(pc_is(ISVOL, path,CS_CHARSET_ARGS));
}
#endif

/*****************************************************************************
    pc_get_attributes - Get File Attributes

 Description
    Given a file or directory name return the directory entry attributes
    associated with the entry.

    The following values are returned:

    BIT Nemonic
    0       ARDONLY
    1       AHIDDEN
    2       ASYSTEM
    3       AVOLUME
    4       ADIRENT
    5       ARCHIVE
    6-7 Reserved

 Returns
    Returns TRUE if successful otherwise it returns FALSE

    errno is set to one of the following
    0                - No error
    PENOENT          - Path not found
    An ERTFS system error
****************************************************************************/


#if (INCLUDE_CS_UNICODE)
BOOLEAN pc_get_attributes_cs(byte *path, byte *p_return, int use_charset)           /*__apifn__*/
#else
BOOLEAN pc_get_attributes(byte *path, byte *p_return)           /*__apifn__*/
#endif
{
    DROBJ  *pobj;
    BOOLEAN ret_val = FALSE;
    int driveno;
    CHECK_MEM(BOOLEAN, 0)   /* Make sure memory is initted */

    rtfs_clear_errno(); /* pc_get_attributes: clear errno */

    /* Get the drive and make sure it is mounted   */
    driveno = check_drive_name_mount(path, CS_CHARSET_ARGS);
    if (driveno < 0)
    {
        /* pc_get_attributes: if check_drive failed errno was set by check_drive */
        return (FALSE);
    }
    pobj = pc_fndnode(path, CS_CHARSET_ARGS);
    /* if pc_fndnode fails it will set errno */
    if (pobj)
    {
    	if ( pc_isroot(pobj))
			*p_return = ADIRENT;
		else
        	*p_return = pobj->finode->fattribute;
        pc_freeobj(pobj);
        ret_val = TRUE;
    }
    release_drive_mount(driveno);/* Release lock, unmount if aborted */
    return (ret_val);
}

/***************************************************************************
    PC_GETDFLTDRVNO - Return the current default drive.

 Description
    Use this function to get the current default drive when a path specifier
    does not contain a drive specifier.

    see also pc_setdfltdrvno()

 Returns
    Return the current default drive.

    pc_getdfltdrvno() does not set errno
*****************************************************************************/

/* Return the currently stored default drive    */

#if (INCLUDE_CS_UNICODE)
int pc_get_default_drive_cs(byte *drive_name, int use_charset)        /*__apifn__*/
#else
int pc_get_default_drive(byte *drive_name)        /*__apifn__*/
#endif
{
int drive_number;
    CHECK_MEM(int, 0)   /* Make sure memory is initted */
    if (!rtfs_get_system_user()->dfltdrv)
	{
        drive_number = prtfs_cfg->default_drive_id;
		if (drive_number)	   /* Stored a 1 - 27, zero means not set */
			drive_number -= 1;
	}
    else
        drive_number = rtfs_get_system_user()->dfltdrv-1;
    if (drive_name)
        pc_drno_to_drname(drive_number, drive_name, CS_CHARSET_ARGS);
    return(drive_number);
}

/*  PC_CLUSTER_SIZE  - Return the number of bytes per cluster for a drive

 Description
        This function will return the cluster size mounted device
        named in the argument.


 Returns
    The cluster size or zero if the device is not mounted.

    errno is set to one of the following
    0               - No error
    PEINVALIDDRIVEID- Drive name is invalid
*****************************************************************************/
static BOOLEAN _pc_xxxxx_size(byte *drive, int *pclustersize, int *psectorsize);

int pc_sector_size(byte *drive)                                /*__apifn__*/
{
int sectorsize, clustersize;
    if (!_pc_xxxxx_size(drive, &clustersize, &sectorsize))
        sectorsize = 0;
    return(sectorsize);
}

int pc_cluster_size(byte *drive)                                /*__apifn__*/
{
int clustersize, sectorsize;
    if (!_pc_xxxxx_size(drive, &clustersize, &sectorsize))
        clustersize = 0;
    return(clustersize);
}
static BOOLEAN _pc_xxxxx_size(byte *drive, int *pclustersize, int *psectorsize)
{
int driveno;
DDRIVE *pdrive;
CHECK_MEM(int, 0)   /* Make sure memory is initted */

    rtfs_clear_errno();  /* pc_cluster_size: clear error status */
    /* Get the drive and make sure it is mounted   */
    driveno = check_drive_name_mount(drive, CS_CHARSET_NOT_UNICODE);
    if (driveno < 0)
    {
        /* errno was set by check_drive */
        return(FALSE);
    }
    pdrive = pc_drno2dr(driveno);
    *pclustersize = pdrive->drive_info.bytespcluster;
    *psectorsize  = pdrive->drive_info.bytespsector;

    release_drive_mount(driveno);/* Release lock, unmount if aborted */
    return(TRUE);
}

void pc_drno_to_drname(int driveno, byte *pdrive_name, int use_charset)
{
byte c,*p;
    p = pdrive_name;
    c = (byte) ('A' + driveno);
    CS_OP_ASSIGN_ASCII(p,c, use_charset);
    CS_OP_INC_PTR(p, use_charset);
    CS_OP_ASSIGN_ASCII(p,':', use_charset);
    CS_OP_INC_PTR(p, use_charset);
    CS_OP_TERM_STRING(p, use_charset);
}

int pc_drname_to_drno(byte *drive_name, int use_charset)
{
    return(pc_parse_raw_drive(drive_name, use_charset));
}

int pc_fd_to_driveno(int fd,byte *pdrive_name, int use_charset)
{
PC_FILE *pfile;
int driveno;

    pfile = pc_fd2file(fd, 0);

    if (pfile && pfile->pobj)
    {
        driveno = pfile->pobj->pdrive->driveno;
        rtfs_release_media_and_buffers(driveno);
        if (pdrive_name)
            pc_drno_to_drname(driveno, pdrive_name, use_charset);
    }
    else
    {
        driveno = -1;
        if (pdrive_name)
        {
            *pdrive_name = 0;
            *(pdrive_name+1) = 0;
        }
    }
    return(driveno);
}
