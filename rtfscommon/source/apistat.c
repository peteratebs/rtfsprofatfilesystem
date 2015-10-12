/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* APISTAT.C - Contains user api level source code.

    The following routines are included:

    pc_fstat        -  Obtain statistics on an open file
    pc_stat         -  Obtain statistics on a path.

*/

#include "rtfs.h"


/****************************************************************************
    PC_STAT  -  Obtain statistics on a path.

 Description
    This routine searches for the file or directory provided in the first
    argument. If found it fills in the stat structure as described here.

    st_dev  -   The entry s drive number
    st_mode;
        S_IFMT  type of file mask
        S_IFCHR character special (unused)
        S_IFDIR directory
        S_IFBLK block special   (unused)
        S_IFREG regular         (a file)
        S_IWRITE    Write permitted
        S_IREAD Read permitted.
    st_rdev -   The entry s drive number
    st_size -   file size
    st_atime    -   creation date in DATESTR format
    st_mtime    -   creation date in DATESTR format
    st_ctime    -   creation date in DATESTR format
    t_blksize   -   optimal blocksize for I/O (cluster size)
    t_blocks    -   blocks allocated for file
    fattributes -   The DOS attributes. This is non-standard but supplied
                    if you want to look at them


 Returns
    Returns 0 if all went well otherwise it returns -1.

    errno is set to one of the following
    0               - No error
    PEINVALIDDRIVEID- Drive component is invalid
    PENOENT         - File or directory not found
    An ERTFS system error
****************************************************************************/

#if (INCLUDE_CS_UNICODE)
int pc_stat_cs(byte *name, ERTFS_STAT *pstat, int use_charset)
#else
int pc_stat(byte *name, ERTFS_STAT *pstat)
#endif
{
    DROBJ *pobj;
    int driveno;
    int ret_val;
    CHECK_MEM(int, -1)  /* Make sure memory is initted */

    rtfs_clear_errno();  /* pc_stat: clear error status */
    /* Get the drive and make sure it is mounted   */
    driveno = check_drive_name_mount(name, CS_CHARSET_ARGS);
    if (driveno < 0)
    {   /* errno was set by check_drive */
        return(-1);
    }

    /* pc_fndnode will set errno */
    pobj = pc_fndnode(name, CS_CHARSET_ARGS);
    if (pobj)
    {
        if (pobj->isroot)
        {
            pstat->st_rdev  =
            pstat->st_dev   = pobj->finode->my_drive->driveno;
            pstat->st_ino   = 0;
            pstat->fattribute = ADIRENT;
            pstat->st_mode = S_IFDIR;
            pstat->st_nlink  = 1;            /* (always 1) */
            pstat->st_size  = 0;     /* file size, in bytes */
            pstat->st_atime.date  =  pstat->st_atime.time  = 0;
            pstat->st_mtime  = pstat->st_atime;         /* last modification */
            pstat->st_ctime  = pstat->st_atime;         /* last status change */
            pstat->st_blksize = (dword) pobj->finode->my_drive->drive_info.bytespcluster;
            pstat->st_blocks  =  0;
        }
        else
        {
            /* cal pc_finode_stat() to update the stat structure   */
            pc_finode_stat(pobj->finode, pstat);
        }
        ret_val = 0;
    }
    else
        ret_val = -1;
    if (pobj)
        pc_freeobj(pobj);
    release_drive_mount(driveno);/* Release lock, unmount if aborted */

    return(ret_val);
}

/****************************************************************************
    PC_FINODE_STAT - Convert finode information to stat info for stat and fstat

 Description
    Given a pointer to a FINODE and a ERTFS_STAT structure
    load ERTFS_STAT with filesize, date of modification et al. Interpret
    the fattributes field of the finode to fill in the st_mode field of the
    the stat structure.

 Returns
    Nothing


****************************************************************************/

void pc_finode_stat(FINODE *pi, ERTFS_STAT *pstat)                /*__fn__*/
{
    rtfs_memset(pstat, 0 , sizeof(*pstat));
    pstat->st_dev   = pi->my_drive->driveno;    /* (drive number, rtfs) */
    pstat->st_ino   = 0;                        /* inode number (0) */
    pstat->st_mode  = 0;                        /* (see S_xxxx below) */

    /* Store away the DOS file attributes in case someone needs them   */
    pstat->fattribute = pi->fattribute;
    pstat->st_mode |= S_IREAD;
    if(!(pstat->fattribute & ARDONLY))
        pstat->st_mode |= S_IWRITE;
    if (pstat->fattribute & ADIRENT)
        pstat->st_mode |= S_IFDIR;
    if (!(pstat->fattribute & (AVOLUME|ADIRENT)))
        pstat->st_mode |= S_IFREG;

    pstat->st_nlink  = 1;                       /* (always 1) */
    pstat->st_rdev  = pstat->st_dev;            /* (drive number, rtfs) */
   	pstat->st_size_hi  	= 0;

    /* optimal buffering size. is a cluster   */
    pstat->st_blksize = (dword) pi->my_drive->drive_info.bytespcluster;

#if (INCLUDE_EXFATORFAT64)
	if (ISEXFATORFAT64(pi->my_drive))
	{
    	pstat->st_size  	= M64LOWDW(pi->fsizeu.fsize64);
    	pstat->st_size_hi  	= M64HIGHDW(pi->fsizeu.fsize64);

    	/* blocks (sectors) is file size / byts per sector. with round up   */
    	{
    		ddword fsize_ddw,blocks_ddw;
    		/* 64 bit version of (dword) ((pi->fsize + 511)>>9); */
    		fsize_ddw = M64SET32(pstat->st_size_hi, pstat->st_size);
    		fsize_ddw = M64PLUS32(fsize_ddw, (dword) pi->my_drive->drive_info.bytespsector-1);
    		blocks_ddw = M64RSHIFT(fsize_ddw, (dword) pi->my_drive->drive_info.log2_bytespsec);
    		pstat->st_blocks  =  (dword) M64LOWDW(blocks_ddw);
       }
	}
	else
#endif
	{
    	pstat->st_size  = pi->fsizeu.fsize;                /* file size, in bytes */
    	/* blocks (sectors) is file size / byts per sector. with round up   */
    	pstat->st_blocks  =  (dword) ((pstat->st_size + (pi->my_drive->drive_info.bytespsector-1))>>pi->my_drive->drive_info.log2_bytespsec);
	}
   	pstat->st_atime.date  = pi->adate;          /* last access  */
   	pstat->st_atime.time  = pi->atime;
   	pstat->st_ctime.date  = pi->cdate;          /* Created  */
   	pstat->st_ctime.time  = pi->ctime;
   	pstat->st_mtime.date  = pi->fdate;          /* Modified  */
   	pstat->st_mtime.time  = pi->ftime;

}
