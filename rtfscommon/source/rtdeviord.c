/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* rtdevio.c - Media check functions and device io wrapper functions
*
*
*/
#include "rtfs.h"


#if (INCLUDE_RTFS_PROPLUS)    /* pc_check_automount() */
BOOLEAN  pc_check_automount(DDRIVE *pdr);
#endif

/* Check if a drive id is valid and mount the drive. This is an internal
   routine that is called by other api calls
   If it fails, return -1 with the drive not claimed.
   If it succeeds return the drive number with the drive claimed.
   The caller will call release_drive_mount(driveno) to release it
*/

int check_drive_name_mount(byte *name, int use_charset)  /*__fn__*/
{
int driveno;
    /* Get the drive and make sure it is mounted   */
    if (pc_parsedrive( &driveno, name, use_charset))
    {
    	if (check_drive_by_number(driveno, TRUE))
        	return(driveno);
    }
    return(-1);
}


DDRIVE *check_drive_by_name(byte *name, int use_charset)  /*__fn__*/
{
int driveno;
    /* Get the drive and make sure it is installed, don't worry if it's mounted   */
    if (pc_parsedrive( &driveno, name, use_charset))
    {
    	return(check_drive_by_number(driveno, FALSE));
    }
    return(0);
}


/* Check if a drive id is valid and mount the drive. This is an internal
   routine that is called by other api calls
   If it fails, return -1 with the drive not claimed.
   If it succeeds return the drive number with the drive claimed.
   The caller will call release_drive_mount(driveno) to release it
*/

DDRIVE *check_drive_by_number(int driveno, BOOLEAN check_mount)  /*__fn__*/
{
    if (!pc_validate_driveno(driveno))
    {
        rtfs_set_errno(PEINVALIDDRIVEID, __FILE__, __LINE__); /* Check drive called with bad drive name */
    }
    else
    {
		DDRIVE *pdr;
        /* Get the drive and make sure it is mounted   */
        pdr = rtfs_claim_media_and_buffers(driveno);
		if (!pdr)
			return(0);
		/* If just checking vor installed media, we're there */
		if (!check_mount)
			return(pdr);
#if (INCLUDE_RTFS_PROPLUS)    /*  ProPlus check if mount in progress */
        if (pdr->drive_state.drive_async_state == DRV_ASYNC_MOUNT)
        {
            rtfs_set_errno(PEEINPROGRESS, __FILE__, __LINE__);
    		goto release_buffers_and_fail;
        }
#endif
        /* If an abort request is pending, executed it */
        if (chk_mount_valid(pdr) && chk_mount_abort(pdr))
           	pc_dskfree(pdr->driveno);	/* Clears mount valid */

        if (chk_mount_valid(pdr))   /* already mounted */
    		return(pdr);
#if (INCLUDE_RTFS_PROPLUS)    /*  pc_check_automount() ProPlus may want to mount asyncronously */
        if (!pc_check_automount(pdr)) /* Check if automount is disabled (always enabled for Pro) */
    		goto release_buffers_and_fail;
#endif
         /* pc_auto_dskopen() calls dskopen and performs any automatic failsafe operations */
         if (pc_auto_dskopen(pdr))
         	return(pdr);
#if (INCLUDE_RTFS_PROPLUS)
release_buffers_and_fail:
#endif
		rtfs_release_media_and_buffers(driveno); /* check_drive_number_mount release */
	}
    return(0);
}

/* Release a drive that was claimed by a succesful call to
   check_drive_name_mount, or check_drive_number_mount
   If the the operation queued an abort request then
   free the drive an all it's structures
*/



void release_drive_mount(int driveno)
{
DDRIVE *pdr;
    pdr = pc_drno_to_drive_struct(driveno);
    if (pdr && chk_mount_abort(pdr))
    {
        pc_dskfree(driveno);
    }
    rtfs_release_media_and_buffers(driveno); /* release_drive_mount release */
}



/*
*     Note: The the logical drive must be claimed before this routine is
*    called and later released by the caller.
*/

/* If test mode is enabled include code to force read or write errors if requested */
#if (INCLUDE_DEBUG_TEST_CODE && INCLUDE_RTFS_PROPLUS)
static BOOLEAN _raw_devio_xfer(DDRIVE *pdr, dword blockno, byte * buf, dword n_to_xfer, BOOLEAN raw, BOOLEAN reading) /* __fn__ */
#else
BOOLEAN raw_devio_xfer(DDRIVE *pdr, dword blockno, byte * buf, dword n_to_xfer, BOOLEAN raw, BOOLEAN reading) /* __fn__ */
#endif
{
int driveno;
dword ltemp, nbytes;
word  n_w_to_xfer;

    driveno = pdr->driveno;

/* Bug Fix. 1-28-02 thanks to Kerry Krouse. This code was inside the
   loop, which caused retries to fail
*/
    /* Removed test of if (pdr->drive_flags & DRIVE_FLAGS_PARTITIONED). partition_base will be zero if the device is not partitioned */
    {
        if (!raw)
            blockno += pdr->drive_info.partition_base;
    }
    ltemp = n_to_xfer;

    /* Loop, allowing n blocks to be a long value while driver interace is
       still short */
    while (ltemp)
    {
        if (ltemp > 0xffff)
            n_w_to_xfer = (word) 0xffff;
        else
            n_w_to_xfer = (word) (ltemp & 0xffff);
        if (RTFS_DEVI_io(driveno, blockno, buf, n_w_to_xfer, reading))
        {
            nbytes = (dword) n_w_to_xfer;
            nbytes <<= pdr->drive_info.log2_bytespsec;
            buf += nbytes;
            blockno += n_w_to_xfer;
            /* Calculate loop counter here so we can fall through with counters updated */
            ltemp -= n_w_to_xfer;    /* Will break out when zero */
        }
        else
        {
        	rtfs_diag_callback(RTFS_CBD_IOERROR, driveno);
            return(FALSE);
        }
    }
    return(TRUE);
}


#if (INCLUDE_DEBUG_TEST_CODE && !INCLUDE_RTFS_PROPLUS)
void rtfs_devio_log_io(DDRIVE *pdr, int io_access_type,dword io_block, dword io_count)
{
    RTFS_ARGSUSED_PVOID((void *) pdr);
    RTFS_ARGSUSED_INT((int) io_block);
    RTFS_ARGSUSED_INT((int) io_count);
    RTFS_ARGSUSED_INT((int) io_access_type);
}
#endif

#if (INCLUDE_DEBUG_TEST_CODE && INCLUDE_RTFS_PROPLUS)
/* This is test code that forces a read or write error that was queued up earlier.
   Used for RTFS Pro Plus test modules only */
byte error_data[RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES] = "ERRORERRORERRORERRORERRORERRORERRORERRORERRORERRORERRORERRORERROR";

BOOLEAN raw_devio_xfer(DDRIVE *pdr, dword blockno, byte * buf, dword n_to_xfer, BOOLEAN raw, BOOLEAN reading) /* __fn__ */
{
    /* If run-time tests are enabled see if we need to simulate a read or
       write error - If so, process as many blocks normally as allowed
       and then simulate the error */
    if (pdr->du.iostats.force_io_error_now)
    {
        dword force_error_on;
        force_error_on = pdr->du.iostats.force_io_error_now-1;
        ERTFS_ASSERT(force_error_on <= n_to_xfer)
        pdr->du.iostats.force_io_error_now = 0;
        /* Transfer non error blocks if any */
        if (force_error_on > 1)
        {
            if (!_raw_devio_xfer(pdr, blockno, buf, force_error_on-1, raw, reading))
                return(FALSE);
        }
        /* If it's a write, put down a bad block at the error location */
        if (!reading)
        {
            _raw_devio_xfer(pdr, blockno+force_error_on-1, error_data, 1, raw, FALSE);
        }
        pdr->du.iostats.io_errors_forced += 1;
        return(FALSE);
    }
    else
    {
        return(_raw_devio_xfer(pdr, blockno, buf, n_to_xfer, raw, reading));
    }
}

void rtfs_devio_log_io(DDRIVE *pdr, int io_access_type,dword io_block, dword io_count)
{
dword old_count;

    RTFS_ARGSUSED_INT((int) io_block);
    old_count = pdr->du.iostats.blocks_transferred[io_access_type];
    pdr->du.iostats.blocks_transferred[io_access_type] += io_count;

    /* Set pdr->du.iostats.force_io_error_now to >= 1 to force an IO error when raw_devio_xfer() executes */
    if (io_access_type && pdr->du.iostats.force_io_error_on_type==io_access_type
        && pdr->du.iostats.force_io_error_when
        && (pdr->du.iostats.blocks_transferred[io_access_type] >= pdr->du.iostats.force_io_error_when))
    {
        pdr->du.iostats.force_io_error_now = 1 + pdr->du.iostats.force_io_error_when - old_count;
        pdr->du.iostats.force_io_error_when = 0;
    }
    else
        pdr->du.iostats.force_io_error_now = 0;

}
#endif
