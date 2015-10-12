/*              (proplus)
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* PRAPIEFI.C - Contains user api level enhanced file IO source code.

  The following public API routines are provided by this module:
    pc_efilio_flush  -
    pc_efilio_chsize -
    pc_efilio_write -
    pc_efilio_setalloc -
    pc_efilio_settime -

*/

#include "rtfs.h"
#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */

void _pc_efiliocom_append_clusters (DDRIVE *pdr, PC_FILE *pefile, FINODE *peffinode, REGION_FRAGMENT *pnew_frags, BOOLEAN is_prealloc, dword new_clusters, dword new_bytes);


/****************************************************************************
    pc_efilio_flush  -  Flush an extended 32 file file or 64 bit metafile

Summary
    BOOLEAN pc_efilio_flush(fd)
       int fd - A file descriptor that was returned from a succesful call
        to pc_efilio_open.

 Description
    If there are any defered cluster maintanance operations to do on
    the file do them now. If the directory entry is modified
    or the fat is dirty flush them to disk.

 Returns
    Returns TRUE if all went well otherwise it returns FALSE.

    errno is set to one of the following
    0               - No error
    PEBADF          - Invalid file descriptor
    PEACCES         - File is read only
    PECLOSED        - File is no longer available.  Call pc_efilio_close().
    PEEFIOILLEGALFD - The file not open in extended IO mode
    An ERTFS system error
****************************************************************************/

BOOLEAN pc_efilio_flush(int fd)                                /*__apifn__*/
{
    PC_FILE *pefile;
    BOOLEAN ret_val;
    int driveno;
    CHECK_MEM(BOOLEAN, 0)   /* Make sure memory is initted */

    rtfs_clear_errno();  /* po_flush: clear error status */

    /* Get the FILE. must be open for write   */
    /* Get the file structure and semaphore lock the drive */
    pefile = pc_fd2file(fd, PO_WRONLY|PO_RDWR);
    if (!pefile)
        return(FALSE); /* fd2file set errno */

    driveno = pefile->pobj->pdrive->driveno;
    if (_pc_check_efile_open_mode(pefile))
        ret_val = _pc_efilio_flush(pefile);
    else
        ret_val = FALSE;
    if (!release_drive_mount_write(driveno))/* Release lock, unmount if aborted */
        ret_val = FALSE;
    return(ret_val);
}

BOOLEAN _pc_efilio_flush_file_buffer(PC_FILE *pefile)
{
    return(pc_flush_file_buffer(pefile->pobj->finode));
}

BOOLEAN _pc_efilio_flush(PC_FILE *pefile)
{
     if (!_pc_efilio_flush_file_buffer(pefile))
        return(FALSE);
     return(_pc_efilio32_flush(pefile,0)); /* _pc_efilio32_flush() will set errno */
}

void _pc_efilio_free_excess_clusters(PC_FILE *pefile)
{
    pc_free_excess_clusters(pefile->fc.plus.ffinode);
}


BOOLEAN pc_efilio_chsize(int fd, dword newsize_hi, dword newsize_lo)
{
BOOLEAN ret_val;
PC_FILE *pefile;
dword ltemp, orgfp_hi, orgfp_lo, fsize_hi, fsize_lo;
ddword fsize_ddw,newsize_ddw;

    CHECK_MEM(BOOLEAN, 0) /* Make sure memory is initted */
    rtfs_clear_errno();    /* clear errno */

    pefile = pc_fd2file(fd, 0);
    if (!pefile)
        return(FALSE);              /* pc_fd2file() set errno */
    ret_val = FALSE;
    if (_pc_check_efile_open_mode(pefile))
    {
		/* Safe original fp */
        if (!_pc_efilio_lseek(pefile, 0, 0, PSEEK_CUR, &orgfp_hi, &orgfp_lo))
			goto ex_it;
		/* Get size and position fp at the end */
        if (!_pc_efilio_lseek(pefile, 0, 0, PSEEK_END, &fsize_hi, &fsize_lo))
			goto ex_it;
        fsize_ddw = M64SET32(fsize_hi,fsize_lo);
		newsize_ddw = M64SET32(newsize_hi, newsize_lo);
		/* Check for simple case, expanding the file */
		if (M64GTEQ(newsize_ddw, fsize_ddw))
		{
			ddword nleft_ddw;
			nleft_ddw = M64MINUS(newsize_ddw, fsize_ddw);
			while(M64NOTZERO(nleft_ddw))
			{
				BOOLEAN rc;     /* [AIC] test fix RTFiles2009111201 */
				dword n_to_write,n_written;
				if (M64HIGHDW(nleft_ddw))
					n_to_write = 0x80000000; /* 2 gig */
				else
					n_to_write = M64LOWDW(nleft_ddw);
				//if (!_pc_efilio_write(pefile, 0, n_to_write, &n_written))
				//	goto ex_it;
				rc = _pc_efilio_write(pefile, 0, n_to_write, &n_written);
				/* Check for short write */
				if (n_written != n_to_write)
				{
					rtfs_set_errno(PENOSPC, __FILE__, __LINE__);
					//goto ex_it;
					break;
				}
				if (!rc) {
					break;
				}
				nleft_ddw = M64MINUS32(nleft_ddw, n_to_write);
			}
			if (M64NOTZERO(nleft_ddw)) {
				_pc_efinode_chsize(pefile->fc.plus.ffinode,fsize_hi,fsize_lo);
			} else {
			ret_val = TRUE;
			}
			_pc_efilio_lseek(pefile, orgfp_hi, orgfp_lo, PSEEK_SET, &ltemp, &ltemp);
		}
		else
		{
            /* Can only truncate a file that you hold exclusively   */
            if (pefile->pobj->finode->opencount > 1)
            {
                rtfs_set_errno(PEACCES, __FILE__, __LINE__);
                _pc_efilio_lseek(pefile, orgfp_hi, orgfp_lo, PSEEK_SET, &ltemp, &ltemp);
                goto ex_it;
			}
		    ret_val = _pc_efinode_chsize(pefile->fc.plus.ffinode, newsize_hi,newsize_lo);
            _pc_efilio_reset_seek(pefile);
		}
        /* Seek back to where we started. If we truncated before this point seek should
           put the file pointer at eof */
		if (ret_val && !_pc_efilio_lseek(pefile, orgfp_hi, orgfp_lo, PSEEK_SET, &ltemp, &ltemp))
			ret_val = FALSE; /* This is not expected to fail, maybe a removal, just bail */
        /* If no free manager is present flush the file */
        if (ret_val && CHECK_FREEMG_CLOSED(pefile->pobj->pdrive) && !_pc_efilio_flush(pefile))
	        ret_val = FALSE;
    }
ex_it:
    release_drive_mount(pefile->pobj->pdrive->driveno);/* Release lock, unmount if aborted */
    return(ret_val);
}

/****************************************************************************
pc_efilio_write  - Write to an extended 32 file or 64 bit metafile

  Summary
    BOOLEAN pc_efilio_write(fd, buf, count, nwritten)
        int fd - A file descriptor that was returned from a succesful call
         to pc_efilio_open.

        dword count - The length of the write request, (0 to 0xffffffff)
        byte *buf - Buffer containing data is to be written. If buf is a null
            pointer pc_efilio_write() will proceed as as usual but it
            will not transfer bytes to the buffer.
        dword *nwritten - Returns the number of bytes written

  Description
    pc_efilio_write takes advantage of the extended filio subsystem to
    perform file writes. There is guaranteed no disk latency required for
    mapping file extents to cluster regions, so writes may be performed at
    very near the bandwidth of the underlying device.

    pc_efilio_write attempts to write count bytes to the file at the cuurent
    file pointer.  The value of count may be up 0xffffffff.


    pc_efilio_write's behavior is affected by the following options and
    extended options that were established in the open call.

    PO_APPEND           - Always seek to end of file before writing.
    PCE_FIRST_FIT       - Allocate from beginning of file data area
    PCE_FORCE_FIRST     - Precedence to small free disk fragments over
                          contiguous fragments.
    PCE_FORCE_CONTIGUOUS- Force contiguous allocation or fail.
    PCE_TRANSACTION_FILE- When RTFS returns from pc_efilio_write(), it
                          guarantees that the data is written to the volume
                          and will survive power loss. If power is
                          interrupted before pc_efilio_write() returns
                          then it is guaranteed that the file is unchanged.
                          If the write operation overwrites an existing
                          region, RTFS insures that the overwrite may be
                          rewound if a power outage occurs before it
                          completes.

    If pc_efilio_write needs to allocate clusters during a file extend
    operation and the min_clusters_per_allocation field was established
    when the file was opened the pc_efilio_write pre-allocates
    min_clusters_per_allocation.

    Note: If buf is 0 (the null pointer) then the operation is performed
          identically to a normal write, the file pointer is moved and
          as the file cluster chain is extended if needed but no data is
          transfered. This may be used to quicky expand a file or to move the file
          pointer.


  Returns:
      TRUE if no errors were encountered. FALSE otherwise.
    *nwriiten is set to the number of bytes successfully written.

  errno is set to one of the following
    0               - No error
    PEBADF          - Invalid file descriptor
    PECLOSED        - File is no longer available.  Call pc_efilio_close().
    PEINVALIDPARMS  - Bad or missing argument
    PEACCES         - File is read only
    PEIOERRORWRITE  - Error performing write
    PEIOERRORREADBLOCK- Error reading block for merge and write
    PENOSPC         - Disk to full to allocate file minimmum allocation size.
    PEEFIOILLEGALFD - The file not open in extended IO mode.
    PETOOLARGE      - Attempt to extend the file beyond 4 gigabytes
    PERESOURCEREGION- Ran out of region structures while performing operation

    An ERTFS system error
*****************************************************************************/

BOOLEAN pc_efilio_write(int fd, byte *buff, dword count, dword *nwritten)
{
PC_FILE *pefile;
DDRIVE *pdr;
BOOLEAN ret_val;
dword ltemp, orgfp_hi, orgfp_lo, fsize_hi, fsize_lo;


    CHECK_MEM(BOOLEAN,0) /* Make sure memory is initted */

    rtfs_clear_errno();    /* clear errno */
    if (!nwritten)
    {
        rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
        return(FALSE);
    }
    ret_val = FALSE;
    pefile = pc_fd2file(fd, PO_WRONLY|PO_RDWR);
    if (pefile)
    {
        /* The logical drive is now locked */
        pdr = pefile->pobj->pdrive;
        if (_pc_check_efile_open_mode(pefile)) {
            ret_val = FALSE;
            /* Safe original fp */
            if (!_pc_efilio_lseek(pefile, 0, 0, PSEEK_CUR, &orgfp_hi, &orgfp_lo)) {
                goto ex_it;
            }
            if (!_pc_efilio_lseek(pefile, 0, 0, PSEEK_END, &fsize_hi, &fsize_lo)) {
                goto ex_it;
            }
            if (!_pc_efilio_lseek(pefile, orgfp_hi, orgfp_lo, PSEEK_SET, &ltemp, &ltemp)) {
                goto ex_it;
            }
            ret_val = _pc_efilio_write(pefile, buff, count, nwritten);
            if (!ret_val) {
                _pc_efinode_chsize(pefile->fc.plus.ffinode,fsize_hi,fsize_lo);
                _pc_efilio_lseek(pefile, orgfp_hi, orgfp_lo, PSEEK_SET, &ltemp, &ltemp);
            }
        }
ex_it:
        if (!release_drive_mount_write(pdr->driveno))/* Release lock, unmount if aborted */
            ret_val = FALSE;
    }
    return(ret_val);
}
BOOLEAN _pc_efilio_write(PC_FILE *pefile, byte *buff, dword count, dword *nwritten)
{
DDRIVE *pdr;
BOOLEAN ret_val;
dword ltemp,ltemp1;

    pdr = pefile->pobj->pdrive;
#if (INCLUDE_ASYNCRONOUS_API)
    if (pefile->fc.plus.allocation_policy & PCE_TRANSACTION_FILE)
    {
        if (pdr->drive_state.drive_async_state != DRV_ASYNC_IDLE)
        {
            if (_pc_async_step(pdr, DRV_ASYNC_IDLE, 0) != PC_ASYNC_COMPLETE)
            {
                /* Could not complete async operation in progress and
                   can't process transaction while async operation in progress */
                rtfs_set_errno(PEEINPROGRESS, __FILE__, __LINE__);
                return(FALSE);
            }
        }
    }
#endif
    if (pefile->flag & PO_APPEND)
        if (!_pc_efilio_lseek(pefile, 0, 0, PSEEK_END, &ltemp, &ltemp1))
            return(FALSE);

    ret_val = _pc_efiliocom_write(pefile,pefile->fc.plus.ffinode, buff, count, nwritten);
    /* If the efile is open in auto flush mode flush directory entry */
    if (ret_val)
    {
#if (INCLUDE_FAILSAFE_CODE)
         if (pefile->fc.plus.allocation_policy & PCE_TRANSACTION_FILE)
         {
            if (!(_pc_efilio_flush(pefile) && fs_flush_transaction(pdr)))
                ret_val = FALSE;
        }
        else
        {
            if (pefile->flag & PO_AFLUSH)
            {
                 ret_val = _pc_efilio_flush(pefile);
            }
        }
#endif
    }
    return(ret_val);
}

/****************************************************************************
    pc_efilio_setalloc - Specify the next cluster to allocate for the file

  Summary
    BOOLEAN pc_efilio_setalloc(int fd, dword cluster)
        int fd - A file descriptor that was returned from a succesful call
         to pc_efilio_open.
        dword cluster       - Hint for the next cluster to allocate.
        dword reserve_count - Pre-allocate clusters.

  Description

    This function may be called prior to to assign specific clusters to a file
    or to provide hints for where clusters should be allocated from the next time
    the file is expanded by pc_efilio_write or pc_efilio_chsize.

    If reserve_count is non-zero clusters in the range cluster to cluster + reserve_count -1
    will be appended to the file's reserved cluster list. As the file is expanded these
    clusters will be used. When the file is closed unused reserved clusters
    are released.

    If reserve_count is zero pc_efilio_setalloc specifies a hint where the next cluster
    should be allocated when the file is next expanded. The cluster is not reserved and
    other API calls could allocate this cluster before the next write occurs.
    When the next write does occur,the next write occurs, freespace will be searched
    from the hint to the end of volume and if no clusters are found in that range,
    the FAT is searched again, starting from the beginning.


  Returns:
      TRUE if no errors were encountered. FALSE otherwise.

  errno is set to one of the following
    0               - No error
    PEBADF          - Invalid file descriptor
    PECLOSED        - File is no longer available.  Call pc_efilio_close().
    PEINVALIDPARMS  - Bad or missing argument
    PEACCES         - File is read only
    PEEFIOILLEGALFD - The file not open in extended IO mode.

    An ERTFS system error
*****************************************************************************/

BOOLEAN _pc_efilio_setalloc(PC_FILE *pefile, dword cluster, dword reserve_count)
{
BOOLEAN ret_val;
dword end_location;

    ret_val = FALSE;
    /* If reserve count is 0 end and start are the same. Otherwise end = cluster + reserv -1 */
    end_location = cluster +  reserve_count;
    if (reserve_count)
        end_location -= 1;
    if (end_location > pefile->pobj->pdrive->drive_info.maxfindex)
    {
        rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
    }
    else
    {
        pefile->fc.plus.allocation_hint = cluster;
        if (!reserve_count)
            ret_val = TRUE;
        else
        {
            REGION_FRAGMENT *pf;
            int is_error;
            dword n_contig,first_free_cluster;
             if (CHECK_FREEMG_CLOSED(pefile->pobj->pdrive))
            {
                rtfs_set_errno(PERESOURCEREGION, __FILE__, __LINE__);
                goto ex_it;
            }
            first_free_cluster =
                fatop_find_contiguous_free_clusters(pefile->pobj->pdrive,
                    cluster, pefile->pobj->pdrive->drive_info.maxfindex,
                    reserve_count, reserve_count, &n_contig, &is_error, ALLOC_CLUSTERS_PACKED);
            if (is_error)
                goto ex_it;
            if (first_free_cluster != cluster || n_contig < reserve_count)
            { /* The clusters aren't free. */
                rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
                goto ex_it;
            }
            /* Allocate a fragment. If none available recycle freelists. If we fail we we are out of luck */
            pf = pc_fraglist_frag_alloc(pefile->pobj->pdrive, cluster, end_location, 0);
            if (!pf)
                goto ex_it;
            else
            {
                FINODE *peffinode;
                dword bytes_allocated;
                peffinode = pefile->fc.plus.ffinode;
                /* Attach the new cluster to the file, and update allocation information. is_prealloc is TRUE */
                bytes_allocated = pc_alloced_bytes_from_clusters(pefile->pobj->pdrive, reserve_count);
                ret_val = TRUE;
                 _pc_efiliocom_append_clusters(pefile->pobj->pdrive, pefile, peffinode, pf, TRUE, reserve_count, bytes_allocated);
            }
        }
    }
ex_it:
    return(ret_val);
}


BOOLEAN pc_efilio_setalloc(int fd, dword cluster, dword reserve_count)
{
PC_FILE *pefile;
BOOLEAN ret_val;
    CHECK_MEM(BOOLEAN,0) /* Make sure memory is initted */
    rtfs_clear_errno();    /* clear errno */
    ret_val = FALSE;
    pefile = pc_fd2file(fd, PO_WRONLY|PO_RDWR);
    if (pefile)
    {
        ret_val = _pc_efilio_setalloc(pefile, cluster,reserve_count);
        release_drive_mount(pefile->pobj->pdrive->driveno);/* Release lock, unmount if aborted */
    }
    return(ret_val);
}


/****************************************************************************
    pc_efilio_settime  -  Set the time stamp of an extended 32 file file or 64
    bit metafile

Summary
    BOOLEAN pc_efilio_settime(fd, new_time, new_date)
       int fd - A file descriptor that was returned from a succesful call
        to pc_efilio_open.
       word new_time -
       word new_date -
 Description
    If a flush is required first flush the file, then update the timestamp
    of the file.

 Returns
    Returns TRUE if all went well otherwise it returns FALSE.

    errno is set to one of the following
    0               - No error
    PEBADF          - Invalid file descriptor
    PEACCES         - File is read only
    PECLOSED        - File is no longer available.  Call pc_efilio_close().
    PEEFIOILLEGALFD - The file not open in extended IO mode
    An ERTFS system error
****************************************************************************/

BOOLEAN pc_efilio_settime(int fd,word new_time, word new_date)                          /*__apifn__*/
{
    PC_FILE *pefile;
    BOOLEAN ret_val;
    int driveno;
    CHECK_MEM(BOOLEAN, 0)   /* Make sure memory is initted */

    rtfs_clear_errno();  /* po_flush: clear error status */


    /* Get the FILE. must be open for write   */
    /* Get the file structure and semaphore lock the drive */
    pefile = pc_fd2file(fd, PO_WRONLY|PO_RDWR);
    if (!pefile)
        return(FALSE); /* fd2file set errno */
    /* Can't set date/time on a temp file */
    if (pefile->fc.plus.allocation_policy & PCE_TEMP_FILE)
    {
        rtfs_set_errno(PEEFIOILLEGALFD, __FILE__, __LINE__);
        return(FALSE);
    }

    driveno = pefile->pobj->pdrive->driveno;
    if (_pc_check_efile_open_mode(pefile))
    {
        ret_val = _pc_efilio_flush(pefile);
        if (ret_val)
        {
        	ret_val = _pc_efilio32_settime(pefile, new_time, new_date);
        }
    }
    else
        ret_val = FALSE;
    if (!release_drive_mount_write(driveno))/* Release lock, unmount if aborted */
        ret_val = FALSE;
    return(ret_val);
}
#endif /* Exclude from build if read only */
