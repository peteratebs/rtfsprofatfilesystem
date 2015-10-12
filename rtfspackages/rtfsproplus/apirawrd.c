/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* APIRAWIO.C - Contains user api level source code.

    The following routines are included:

    pc_raw_read - Read blocks directly from a disk

*/

#include "rtfs.h"


/****************************************************************************
Name:
    pc_raw_read - Read blocks directly from a disk

Summary:

    #include "rtfs.h"

    BOOLEAN pc_raw_read(int driveno,  byte *buf, dword blockno, dword nblocks, BOOLEAN raw)

Description:
    Read nblocks blocks starting at blockno.
    If raw_is TRUE then blockno is the offset from the beginning of the
    physical device.
    If raw_is FALSE then blockno is the offset from the beginning of the
    partition.
    If raw_is FALSE and the drive volume is not currently mounted RTFS will
    attempt to mount the device.
    If raw_is TRUE the device may be accessed even if the volume is not currently mounted.

 Returns:

    Returns TRUE if the read succeeded or FALSE on error.

    errno is set to one of the following
    0                - No error
    PEINVALIDDRIVEID - Driveno is incorrect
    PEINVALIDPARMS   - Invalid or inconsistent arguments
    PEIOERRORREAD    - The read operation failed
    An ERTFS system error
*****************************************************************************/

BOOLEAN pc_raw_read(int driveno,  byte *buf, dword blockno, dword nblocks, BOOLEAN raw)
{
DDRIVE *pdr;
BOOLEAN ret_val;
    CHECK_MEM(int, FALSE) /* Make sure memory is initted */

    rtfs_clear_errno(); /* pc_raw_read: clear error status. */

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

    ret_val = FALSE;
    /* READ   */
    if (!raw_devio_xfer(pdr, blockno, buf, nblocks, raw, TRUE))
    {
        rtfs_set_errno(PEIOERRORREAD, __FILE__, __LINE__);
        ret_val = FALSE;
    }
    else
        ret_val = TRUE;

    rtfs_release_media_and_buffers(pdr->driveno);
    return(ret_val);
}
