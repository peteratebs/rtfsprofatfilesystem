/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* APIRAWWR.C - Contains user api level source code.

    The following routines are included:

    pc_raw_write - Write blocks directly to a disk

*/

#include "rtfs.h"
#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */


/****************************************************************************
Name:
    pc_raw_write - Write blocks directly to a disk

Summary:

    #include "rtfs.h"

    BOOLEAN pc_raw_write(int driveno,  byte *buf, dword blockno, dword nblocks, BOOLEAN raw_io)

Description:
    Write nblocks blocks starting at blockno. If raw is TRUE
    then blockno is the offset from the beginning of the disk itself. If
    raw is FALSE then blockno is the offset from the beginning of the
    partition.

    Note: It is possible to write any range of blocks in the disk.

 Returns
    Returns TRUE if the write succeeded or FALSE on error.

    errno is set to one of the following
    0                - No error
    PEINVALIDDRIVEID - Driveno is incorrect
    PEINVALIDPARMS   - Invalid or inconsistent arguments
    PEIOERRORWRITE   - The read operation failed
    An ERTFS system error
*****************************************************************************/

BOOLEAN pc_raw_write(int driveno,  byte *buf, dword blockno, dword nblocks, BOOLEAN raw)
{
DDRIVE *pdr;
BOOLEAN ret_val;
    CHECK_MEM(int, FALSE) /* Make sure memory is initted */

    rtfs_clear_errno(); /* pc_raw_write: clear error status. */

    /* return -1 if bad arguments   */
    if (!nblocks || !buf)
    {
        rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
        return(FALSE);
    }
	/* Claim access to the drive, if the drive is inserted the partitions will be set up already */
    pdr = check_drive_by_number(driveno, FALSE);
    if (!pdr)
    	return (FALSE);
    if (raw_devio_xfer(pdr, blockno, buf, nblocks, raw, FALSE))
        ret_val = TRUE;
    else
    {
        rtfs_set_errno(PEIOERRORWRITE, __FILE__, __LINE__);
        ret_val = FALSE;
    }
    rtfs_release_media_and_buffers(driveno);
    return(ret_val);
}
#endif /* Exclude from build if read only */

