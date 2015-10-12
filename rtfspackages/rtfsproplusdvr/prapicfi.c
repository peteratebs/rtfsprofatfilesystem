/*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* PRAPIC64.C - Contains user api level 32/64 bit enhanced circular file IO
                source code.

pc_cfilio_open -  Open a file for circular file IO operations
pc_cfilio_close - Close an open circular file
pc_cfilio_read -  Read from a circular file
pc_cfilio_write -  Write to a circular file
*/
#include "rtfs.h"
#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */

#if (INCLUDE_CIRCULAR_FILES)

static BOOLEAN _pc_cfilio_check_read_wrap(PC_FILE *pefile, BOOLEAN after_read);
dword _pc_cfilio_get_max_read_count(PC_FILE *pefile, dword count);
dword _pc_cfilio_get_max_write_count(PC_FILE *pwriterefile, dword count);
ddword pc_cfilio_get_fp_ddw(PC_FILE *pefile);
static BOOLEAN pc_check_cfile_open_mode(PC_FILE *pefile);


/****************************************************************************
pc_cfilio_open -  Open a file for circular file IO operations

    Summary:
    int pc_cfilio_open(name, flag, poptions)
        byte *name  - File name
        word flag   - Flag values. Same as for po_open
        EFILEOPTIONS *poptions - Extended options

  Description

  Open the file for access as a circular file with opteions specified in
  in flag, with additional options specified in the poptions structure.

    Flag values are

      PO_BINARY   - Ignored. All file access is binary
      PO_TEXT     - Ignored
      PO_RDONLY   - Open for read only
      PO_RDWR     - Read/write access allowed.
      PO_WRONLY   - Open for write only
      PO_CREAT    - Create the file if it does not exist. Use mode to
      specify the permission on the file.
      PO_EXCL     - If flag contains (PO_CREAT | PO_EXCL) and the file already
      exists fail and set errno() to EEXIST
      PO_TRUNC    - Truncate the file if it already exists
      PO_NOSHAREANY- Fail if the file is already open. If the open succeeds
      no other opens will succeed until it is closed.
      PO_NOSHAREWRITE-  Fail if the file is already open for write. If the open
      succeeds no other opens for write will succeed until it is closed.
      PO_AFLUSH   - Flush the file after each write
      PO_APPEND   - Always seek to end of file before writing.
      PO_BUFFERED - Use persistent buffers to improve performance of non
      block alligned reads and writes.

      Extended options -

      If the poptions argument is zero no extend options are used otherwise
      the options structure must be inititialized appropriately and passed
      to pc_efilio64_open.

      The options structure is:

        typedef struct efileoptions {
            dword allocation_policy;
            dword min_clusters_per_allocation;
            dword circular_file_size_hi;
            dword circular_file_size_lo;
            int   n_remap_records;
            REMAP_RECORD *remap_records;
        } EFILEOPTIONS;


        allocation_policy - This field contains bit flags that may be set
        by the user to modifio the behavior of the extended file IO routines.


        The following options may be used:

        PCE_CIRCULAR_FILE  - Select this option to force cfilio_write calls
        to truncate the write operation rather than allow the write file
        pointer to overtake the read read file pointer.

        PCE_CIRCULAR_BUFFER   - Select this option to allow cfilio_write calls
        to proceed even when the write file pointer overtakes the read
        file pointer. (if neither PCE_CIRCULAR_FILE or PCE_CIRCULAR_BUFFER
        are selected this is the default behavior)

        PCE_FIRST_FIT   - Select this option to give precedence to allocating
        file extent from the beginning of the file area. Otherwise the
        default behavior allocates space near the file's currently allocated
        extents.

        PCE_FORCE_FIRST - Select this option to give precedence to
        allocating the first free clusters in the range that fullfill the
        request. If PCE_FORCE_FIRST is not enabled precedence goes to
        allocating the contiguous group free clusters in the range that
        can fullfill the request. If that fails then the same algorith as
        PCE_FORCE_FIRST is used.

        Note: If you set both PCE_FIRST_FIT and PCE_FORCE_FIRST then
        free clusters are allocated sequentially from the beginning of the
        FAT. Using this option will help reduce disk fragmentation
        if it is used on trassient files, small files and other files
        for which a higher likelyhood of fragmentation is acceptable.


        PCE_FORCE_CONTIGUOUS - Select this option to force calls write
        calls to fail if the whole request can not be fullfilled in
        a contiguous extent.

        PCE_KEEP_PREALLOC   - Select this option to force excess preallocated
        clusters (see: min_clusters_per_allocation, below) to be
        incorporated into the file when it is closed. If this option is
        not selected, then excess preallocated clusters are returned to
        freespace when the file is closed.

        PCE_64BIT_META_FILE  - Open as a 64 bit metafile

        min_clusters_per_allocation - Set this value to a value greater than
        one to force pc_efilio64_write to allocate a minimum number of clusters
        each time it needs to extend the file. When the file is closed any
        clusters that were pre-allocated but not used are returned to the
        disk's free space. (This behavior may be overridden by using the
        option PCE_KEEP_PREALLOC).

        Cluster pre-allocation is useful for minimizing disk fragmentation
        and for creating files that are contiguous.
        For example: A high speed video capture and playback application
        requires file extents to be contiguous in order to play back the
        video in real time. If the worst case file size is 1000 clusters
        then set min_clusters_per_allocation to 1000. The first byte that
        is written will cause the file to be extended by 1000 clusters (
        if PCE_FORCE_CONTIGOUS is set then these clusters will all be
        contiguous). Then up to 1000 clusters of data may be written to the
        file without incurring additional overhead. When the file is
        closed those clusters that were not consumed are returned to free
        space.

        circular_file_size_hi - Set this value to the high 32 bits of
        the 64 bit file offset that defines the circular file wrap point.
        If the wrap point is less than 4 gigabytes set this to zero.
        Note: - If circular_file_size_hi is non zero the circular file
        operations require underlying 64 bit metafiles support. So
        pc_cfilio_open sets the PCE_64BIT_META_FILE option bit automatically.

        circular_file_size_lo - Set this value to the low 32 bits of
        the 64 bit file offset that defines the circular file wrap point.

        Two fields in the EFILEOPTIONS structure are used to provide
        ERTFS with the additional storage required to support extract files.

        If the application will call pc_cfilio_extract() to extract linear
        sections from the circular file then these two fields must
        provide storage to contain the file's remap records. One remap
        record is required per active extract file. (an active extract
        file is  a file descriptor that was used as an argument to
        pc_cfilio_extract() but has not yet been closed or overtaken by the
        write pointer. (see the ERTFS-Pro Plus application notes for
        a description and tutorial on using extract files).

        n_remap_records - The number of remap records being provided in the
        remap_records filed.

        remap_records - Pointer to a user supplied buffer or array of
        structures of type REMAP_RECORD large enough to contain
        n_remap_records structures. The REMAP_RECORD is a small structure
        (approximately 32 bytes).


      Returns a non-negative integer to be used as a file descriptor for
      calling other pc_cfilio_xxx routines.

      If an error occurs it returns -1.

      errno is set to one of the following
      0                 - No error
      PENOENT           - Not creating a file and file not found
      PEINVALIDPATH     - Invalid pathname
      PEINVALIDPARMS    - Bad arguments
      PENOSPC           - No space left on disk to create the file
      PEACCES           - Is a directory or opening a read only file for write
      PESHARE           - Sharing violation on file opened in exclusive mode
      PEEFIOILLEGALFD   - The file is already open in basic IO mode.
      PEEXIST           - Opening for exclusive create but file already exists
      PERESOURCEREGION  - Ran out of region structures while performing operation
      An ERTFS system error
****************************************************************************/

#if (INCLUDE_CS_UNICODE)
int pc_cfilio_open_cs(byte *name, word flag, EFILEOPTIONS *poptions,int use_charset)
#else
int pc_cfilio_open(byte *name, word flag, EFILEOPTIONS *poptions)
#endif
{
int fd,newfd,driveno;
PC_FILE *pwriterefile;
PC_FILE *preaderefile;
DDRIVE *pdr;
ddword ltemp_ddw;

    pwriterefile = preaderefile = 0;
    if (!poptions)
    {
invalid_options:
        rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
        return(-1);
    }
    /* If neither option is selected default to circular buffer */
    if ( (poptions->allocation_policy & (PCE_CIRCULAR_FILE|PCE_CIRCULAR_BUFFER)) == 0)
        poptions->allocation_policy |= PCE_CIRCULAR_BUFFER;


    if (!(poptions->circular_file_size_hi||poptions->circular_file_size_lo))
         goto invalid_options;

    /* Mask in legal flags */
    /* Note buffering is not allowed for circular files */
    flag &= (PO_AFLUSH|PO_CREAT|PO_TRUNC|PO_EXCL);
    flag |= PO_WRONLY;  /* Open write file descriptor for write only*/

    /* Get the drive and make sure it is mounted   */
    driveno = check_drive_name_mount(name, CS_CHARSET_ARGS);
    if (driveno < 0)   /*  errno was set by check_drive */
        return(-1);
    fd = _pc_efilio_open(pc_drno2dr(driveno), name, flag, (PS_IREAD|PS_IWRITE), poptions, CS_CHARSET_ARGS);
    if (fd < 0)
        goto errex;

    pwriterefile = prtfs_cfg->mem_file_pool+fd;
    pdr = pwriterefile->pobj->pdrive;

    /* Default to minimum allocation of one cluster */
    pwriterefile->fc.plus.min_alloc_bytes = (dword) pdr->drive_info.bytespcluster;
    pwriterefile->fc.plus.allocation_policy = poptions->allocation_policy;
   /* create a mask for rounding up to the minimum allocation size */
    if (poptions->min_clusters_per_allocation)
    {
        pwriterefile->fc.plus.min_alloc_bytes =
                poptions->min_clusters_per_allocation * pdr->drive_info.bytespcluster;
    }

    /* Put the user's wrap point on a cluster boundary */
    ltemp_ddw = M64SET32(poptions->circular_file_size_hi,poptions->circular_file_size_lo);
    pwriterefile->fc.plus.circular_file_size_ddw = pc_byte2ddwclmodbytes(pdr, ltemp_ddw);

    /* Now open the read file descriptor */
    { /* Clear invalid flags for this call */
    word f1, f2;
        f1 = (PO_WRONLY|PO_RDWR|PO_TRUNC|PO_EXCL|PO_CREAT);
        f2 = (word)(~f1);
        flag &= f2;
    }
    flag |= PO_RDONLY;  /* Open reead file descriptor for read only*/
    newfd = _pc_efilio_open(pdr, name, flag, (PS_IREAD|PS_IWRITE), poptions, CS_CHARSET_ARGS);
    if (newfd < 0)
        goto errex;
    preaderefile = prtfs_cfg->mem_file_pool+newfd;
    preaderefile->fc.plus.psibling = pwriterefile;
    preaderefile->fc.plus.sibling_fd = fd;
    pwriterefile->fc.plus.psibling = preaderefile;
    pwriterefile->fc.plus.sibling_fd = newfd;
    preaderefile->fc.plus.circular_file_size_ddw = pwriterefile->fc.plus.circular_file_size_ddw;

    if (poptions->n_remap_records)       /* User supplied space for remapping circular */
    {
        pc_cfilio_remap_region_init(preaderefile, poptions->remap_records,poptions->n_remap_records);
    }

    release_drive_mount_write(driveno);/* Release lock, unmount if aborted */
    return(fd);
errex:
    if (pwriterefile)
        _pc_efilio_close(pwriterefile);
    if (preaderefile)
        _pc_efilio_close(preaderefile);
    release_drive_mount_write(driveno);/* Release lock, unmount if aborted */
    return(-1);
}


/****************************************************************************
    pc_cfilio_close - Close an open circular file

  Summary:
    BOOLEAN pc_cfilio_close(int fd)

     int fd - A file descriptor that was returned from a succesful call
     to pc_cfilio_open.

  Description:
    Flush the directory and fat to disk, process any deferred cluster
    maintenance operations and free all core associated with the file
    descriptor.

Returns
    Returns TRUE if all went well otherwise it returns FALSE.

errno is set to one of the following
0               - No error
PEBADF          - Invalid file descriptor
PEEFIOILLEGALFD - The file not open in extended IO mode.
An ERTFS system error
****************************************************************************/



BOOLEAN pc_cfilio_close(int fd)
{
    PC_FILE *pefile, *psibling;
    BOOLEAN ret_val;
    int driveno;
    CHECK_MEM(int, 0)  /* Make sure memory is initted */

    rtfs_clear_errno();
    ret_val = TRUE;
    /* Get the file structure and semaphore lock the drive */
    pefile = pc_cfilio_fd2file(fd, FALSE); /* do notunlock */
    if (!pefile)
    {
        /* Add a check here to see if fd2file failed because it was
           closed by pc_dskfree. If so we mark the file free here and
           return success */
        if (get_errno() == PECLOSED)
        {
            OS_CLAIM_FSCRITICAL()
            pefile = prtfs_cfg->mem_file_pool+fd;
            psibling = pefile->fc.plus.psibling;
            OS_RELEASE_FSCRITICAL()
            if (psibling)
                pc_cfilio_release_all_remap_files(psibling, 1);
            OS_CLAIM_FSCRITICAL()
            pefile->is_free = TRUE;
            if (psibling)
                psibling->is_free = TRUE;
            OS_RELEASE_FSCRITICAL()
            rtfs_clear_errno();
            return (TRUE);
        }
        /* all other errors. fd2file set errno */
        return(FALSE);
    }
    else
    {
        driveno = pefile->pobj->pdrive->driveno;
        psibling = pefile->fc.plus.psibling;
        if (!pc_cfilio_release_all_remap_files(psibling, 0))
           ret_val = FALSE;
        pefile->fc.plus.psibling = 0;
        if (!_pc_efilio_flush(pefile))
            ret_val = FALSE;
        /* Closing a file. If in write mode clear the write exclusive flag if set */
        pc_cfilio_clear_open_flags(pefile,OF_WRITEEXCLUSIVE);
        /* Release the FD and its core   */
        pc_freefile(pefile);
        if (psibling) /* Should always be non-zero but check */
        {
            pc_freefile(psibling);
        }
    }
    if (!release_drive_mount_write(driveno))/* Release lock, unmount if aborted */
        return (FALSE);
    return(ret_val);
}

BOOLEAN _pc_cfilio_read(PC_FILE *preaderefile, byte *buff, dword count, dword *nread)
{
dword read_count,ltemp;
REMAP_RECORD *remap_record; /* returned from pc_cfilio_check_remap_read */
ddword file_pointer_ddw;
ddword bytes_to_region_ddw,byte_offset_in_region_ddw,bytes_in_region_ddw;

    remap_record = 0;
    *nread =  0;
    while (count)
    {
         /* Truncate to count or data to eof.. whichever is less */
        read_count = _pc_cfilio_get_max_read_count(preaderefile, count);
        if (!read_count)
            break;
        file_pointer_ddw = pc_cfilio_get_fp_ddw(preaderefile);
        if (preaderefile->fc.plus.remapped_regions)
        {
        ddword ltemp_ddw;
            ltemp_ddw = M64SET32(0,read_count),
            pc_cfilio_check_remap_read(preaderefile,
                                file_pointer_ddw,
                                ltemp_ddw,
                                &bytes_to_region_ddw,
                                &byte_offset_in_region_ddw,
                                &bytes_in_region_ddw,
                                &remap_record);
            if (M64NOTZERO(bytes_to_region_ddw))
            {/* Note: we know that bytes_to_region_ddw fits in a dword */
                bytes_in_region_ddw = M64SET32(0,0);
                read_count = M64LOWDW(bytes_to_region_ddw);
            }
        }
        else
        {
            bytes_in_region_ddw = M64SET32( 0,0);
        }

        if (M64NOTZERO(bytes_in_region_ddw))
        { /* Read remapped byes, then read null to advance circ file pointer */

            read_count = M64LOWDW(bytes_in_region_ddw);
            if (!pc_cfilio_remap_read(preaderefile,file_pointer_ddw,buff,read_count,remap_record))
                return(FALSE);
            /* Advance the read file pointer, wrap at the wrap pointer */
            if (!_pc_efilio_read(preaderefile, 0, read_count, &ltemp) || ltemp != read_count)
                return(FALSE);
        }
        else
        { /* Read bytes from the circular file */

            if (!_pc_efilio_read(preaderefile, buff, read_count, &ltemp) || ltemp != read_count)
                return(FALSE);
        }
        *nread += read_count;
        count -= read_count;
        if (buff)
            buff += read_count;
        /* Move the read pointer to the beginning if we are at the end */
        if (!_pc_cfilio_check_read_wrap(preaderefile,TRUE))
            return(FALSE);
    }
    return(TRUE);
}


/****************************************************************************
pc_cfilio_read -  Read from a circular file

  Summary
    BOOLEAN pc_cfilio_read(fd, buf, count, nread)
        int fd - A file descriptor that was returned from a succesful call
         to pc_Cfilio_open.

        dword count; - The length of the read request, (0 to 0xffffffff)
        byte *buf - Buffer where data is to be placed. If buf is a null
            pointer pc_cfilio_read() will proceed as as usual but it
            will not transfer bytes to the buffer.
        dword *nread - Returns the number of bytes read.

  Description

    Read foreward from the current pointer count bytes,or until the
    last valid byte in the circular file is encountered, whichever is less.

    If the region of the circular file to be read contains remapped sections
    the data is transfered from the remap file rather than the blocks of the
    circular file itself. Pc_cfilio_read utilizes an underlying.

    If the the logical read pointer crosses (laps) the file wrap point
    during the read operation then underlying physical linear file pointer
    as reported by pc_cfilio_lseek() is reset to zero. The logical stream
    pointer as reported by pc_cstreamio_lseek() continues to increase.


    pc_efilio_read uses underlying extended file read functions so it has
    the same zero disk latency file extent tracking behavior.

    Note: If buf is 0 (the null pointer) then the operation is performed
          identically to a normal read except no data transfers are
          performed. This may be used to quicky advance the file pointer.

  Returns:
      TRUE if no errors were encountered. FALSE otherwise.

      *nread is set to the number of bytes successfully read.

  errno is set to one of the following
    0               - No error
    PEBADF          - Invalid file descriptor
    PECLOSED        - File is no longer available. Call pc_cfilio_close().
    PEINVALIDPARMS  - Bad or missing argument
    PEEFIOILLEGALFD - The file not open in circular IO mode.
    PEIOERRORREAD   - Read error
    An ERTFS system error
*****************************************************************************/



BOOLEAN pc_cfilio_read(int fd, byte *buff, dword count, dword *nread)
{
BOOLEAN ret_val;
PC_FILE *pwriterefile, *preaderefile;

    CHECK_MEM(BOOLEAN, 0) /* Make sure memory is initted */

    rtfs_clear_errno();    /* clear     errno */

    ret_val = FALSE;
    /* return 0 bytes read if bad arguments   */
    if (!nread || !count)
    {
        rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
        return(FALSE);
    }
    pwriterefile = pc_cfilio_fd2file(fd, FALSE); /* do not unlock */
    if (!pwriterefile)
        return(FALSE);              /* pc_fd2file() set errno */
    /* The logical drive is now locked */
    preaderefile = pwriterefile->fc.plus.psibling;
    ret_val = _pc_cfilio_read(preaderefile, buff, count, nread);
    release_drive_mount(pwriterefile->pobj->pdrive->driveno);/* Release lock, unmount if aborted */
    return(ret_val);
}


/****************************************************************************
pc_cfilio_write -  Write to a circular file

  Summary
    BOOLEAN pc_cfilio_write(fd, buf, count, nwrite)
        int fd - A file descriptor that was returned from a succesful call
         to pc_cfilio_open.

        dword count; - The length of the write request, (0 to 0xffffffff)

        byte *buf - Buffer where data is to be placed. If buf is a null
            pointer pc_cfilio_write() will proceed as as usual but it
            will not transfer bytes to the buffer.

        dword count - Number of bytes to attempt to write.
        dword *nwrite - Returns the number of bytes write.

  Description

    Write foreward from the current write pointer, count bytes, if the
    circular file is not yet filled to the wrap point extend the underlying
    linear file. Using the allocation rules provided to pc_cfilio_open().
    If the circular file is filled to the wrap point and the write file
    pointer reaches the wrap point

    If the region of the circular file to be written contains remapped
    sections, reduce the remap section by the amount to be written before
    the write takes place so the newly written data is contained in the
    extents owned by the circular file , and not the remap file. Subsequant
    reads from this section of the circular file will return the bytes
    just written and not byets in the remap region.

    If the circulator file was opened with the PCE_CIRCULAR_FILE allocation
    strategy rather than the PCE_CIRCULAR_BUFFER a write call will write
    less bytes than requested if the the write pointer would otherwise
    catch (lap) the read pointer, a short write return value may be used
    to signal to your application that the buffer offload function is not
    keeping up with the buffer load functions.

    If the the logical write pointer crosses (laps) the file wrap point
    during the write operation then underlying physical linear file pointer
    as reported by pc_cfilio_lseek() is reset to zero. The logical stream
    pointer as reported by pc_cstreamio_lseek() continues to increase.

    pc_efilio_write uses underlying extended file write functions so it has
    the same zero disk latency file extent tracking behavior and file
    extend behavior.

    Note: If buf is 0 (the null pointer) then the operation is performed
          identically to a normal write except no data transfers are
          performed. This may be used to quicky advance the file pointer.

  Returns:
      TRUE if no errors were encountered. FALSE otherwise.

      *nwritten is set to the number of bytes successfully write.

  errno is set to one of the following
    0               - No error
    PEBADF          - Invalid file descriptor
    PECLOSED        - File is no longer available. Call pc_cfilio_close().
    PEINVALIDPARMS  - Bad or missing argument
    PEEFIOILLEGALFD - The file not open in circular IO mode.
    PEIOERRORREAD   - Read error
    An ERTFS system error
*****************************************************************************/

BOOLEAN _pc_cfilio_write(PC_FILE *pwriterefile, byte *buff, dword count, dword *nwritten)
{
PC_FILE *preaderefile;
DDRIVE *pdr;
BOOLEAN ret_val,truncate_count;
dword  write_count,ltemp;
ddword cfile_pointer_ddw;

    ret_val = FALSE;
    pdr = pwriterefile->pobj->pdrive;
    /* The logical drive is now locked */
    preaderefile = pwriterefile->fc.plus.psibling;

    *nwritten = 0;
    /* If count is zero just force cluster pre-allocation */
    if (!count)
    {
        ddword cfile_size_ddw;
        /* Check the file size. If it is greater than or equal to the
           wrap point we know that clusters are fully allocated */
        cfile_size_ddw = pc_cfilio_get_file_size_ddw(pwriterefile);
        if (M64GTEQ(cfile_size_ddw,pwriterefile->fc.plus.circular_file_size_ddw))
            return(TRUE);
        else
        {/* Haven't wrapped yet so make sure clusters are pre-allocated at
            the current write pointer */
            return(_pc_efilio_write(pwriterefile, 0, 0, &ltemp));
        }
    }
    /* Get file pointers */
    /* Get the current stream pointer, that's the linear file pointer plus
       the stream offset at the beginning of the file */
    cfile_pointer_ddw = pc_cfilio_get_fp_ddw(pwriterefile);
    truncate_count = FALSE;
    while (count)
    {
        ddword  write_count_ddw;
        write_count = _pc_cfilio_get_max_write_count(pwriterefile, count);
        if (!write_count)
            break;
        write_count_ddw = M64SET32(0,write_count);
        if (!_pc_efilio_write(pwriterefile, buff, write_count, &ltemp) || ltemp != write_count)
           goto return_here;
        if (buff)
            buff += write_count;
        *nwritten += write_count;
        count -= write_count;
        /* purge or shrink any remapped regions in the range we wrote */
        if (preaderefile->fc.plus.remapped_regions)
        {
            if (!pc_cfilio_remap_region_purge(preaderefile, cfile_pointer_ddw,write_count_ddw))
                goto return_here;
        }
        /* Update max write if we hit it */
        cfile_pointer_ddw = M64PLUS(cfile_pointer_ddw, write_count_ddw);
        if (M64GT(cfile_pointer_ddw,pwriterefile->fc.plus.circular_max_offset_ddw))
            pwriterefile->fc.plus.circular_max_offset_ddw = cfile_pointer_ddw;
    }/* while (count) */
    ret_val = TRUE;
return_here:
    return(ret_val);
}

/****************************************************************************
    pc_cfilio_setalloc - Specify the next cluster to allocate for the file

  Summary
    BOOLEAN pc_efilio_setalloc(int fd, dword cluster)
        int fd - A file descriptor that was returned from a succesful call
         to pc_efilio_open.
        dword cluster - Hint for the next cluster to allocate.

  Description
    pc_cfilio_setalloc allows the programmer to specify a hint where the next cluster
    should be allocated when the file is next expanded.

    Freespace will be searched from the hint to the end of volume and if no
    clusters are found in that range, the FAT is searched again, starting from
    the beginning.

    This function may be called prior to file write operations to assign specific
    clusters to a file.

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

BOOLEAN _pc_efilio_setalloc(PC_FILE *pefile, dword cluster, dword reserve_count);

BOOLEAN pc_cfilio_setalloc(int fd, dword cluster, dword reserve_count)
{
PC_FILE *pwriterfile;
BOOLEAN ret_val;
    CHECK_MEM(BOOLEAN,0) /* Make sure memory is initted */
    rtfs_clear_errno();    /* clear errno */
    ret_val = FALSE;
    pwriterfile = pc_cfilio_fd2file(fd, FALSE); /* do not unlock */
    if (pwriterfile)
    {
        ret_val = _pc_efilio_setalloc(pwriterfile, cluster,reserve_count);
        release_drive_mount(pwriterfile->pobj->pdrive->driveno);/* Release lock, unmount if aborted */
    }
    return(ret_val);
}


BOOLEAN pc_cfilio_write(int fd, byte *buff, dword count, dword *nwritten)
{
BOOLEAN ret_val;
PC_FILE *pwriterefile;

    CHECK_MEM(BOOLEAN, 0) /* Make sure memory is initted */

    rtfs_clear_errno();    /* clear     errno */

    ret_val = FALSE;
    /* return 0 bytes read if bad arguments   */
    if (!nwritten)
    {
        rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
        return(FALSE);
    }
    pwriterefile = pc_cfilio_fd2file(fd, FALSE); /* do not unlock */
    if (!pwriterefile)
        return(FALSE);              /* pc_fd2file() set errno */
    /* The logical drive is now locked */
    ret_val = _pc_cfilio_write(pwriterefile, buff, count, nwritten);
    if (!release_drive_mount_write(pwriterefile->pobj->pdrive->driveno))
        ret_val = FALSE;
    return(ret_val);
}

dword _pc_cfilio_get_max_write_count(PC_FILE *pwriterefile, dword count)
{
ddword write_count_ddw,nleft_to_end_ddw,wfile_pointer_ddw;

    write_count_ddw = M64SET32(0,count);
    /* Truncate to eof */
    wfile_pointer_ddw = pc_efilio_get_fp_ddw(pwriterefile);
    nleft_to_end_ddw = M64MINUS(pwriterefile->fc.plus.circular_file_size_ddw,wfile_pointer_ddw);
    if (M64ISZERO(nleft_to_end_ddw))
    {
    dword ltemp, ltemp2;
        if (!_pc_efilio_lseek(pwriterefile, 0, 0, PSEEK_SET, &ltemp, &ltemp2))
            return(FALSE);
        nleft_to_end_ddw = pwriterefile->fc.plus.circular_file_size_ddw;
        pwriterefile->fc.plus.circular_file_base_ddw =
             M64PLUS(pwriterefile->fc.plus.circular_file_base_ddw,pwriterefile->fc.plus.circular_file_size_ddw);
    }
    /* ========== */
    if (pwriterefile->fc.plus.allocation_policy & PCE_CIRCULAR_FILE)
    { /* Truncate if we hit the read pointer */
ddword nleft_ddw, ltemp_ddw, rfile_pointer_ddw;

        wfile_pointer_ddw = pc_cfilio_get_fp_ddw(pwriterefile);
        rfile_pointer_ddw = pc_cfilio_get_fp_ddw(pwriterefile->fc.plus.psibling);

        /* (write pointer - read pointer) is the number read but not written */
        ltemp_ddw = M64MINUS(wfile_pointer_ddw,rfile_pointer_ddw);
        /* size minus (write pointer - read pointer) is the number that can be written */
        nleft_ddw = M64MINUS(pwriterefile->fc.plus.circular_file_size_ddw,ltemp_ddw);
        if (M64GT(nleft_ddw,nleft_to_end_ddw))
            nleft_ddw = nleft_to_end_ddw;

        /* Take the smaller of count and nleft */
        if (M64GT(write_count_ddw,nleft_ddw))
        {
            write_count_ddw = nleft_ddw;
        }
    }
    else
    {
        if (M64GT(write_count_ddw,nleft_to_end_ddw))
            write_count_ddw = nleft_to_end_ddw;
    }
    count = M64LOWDW(write_count_ddw);
    return(count);
}

static BOOLEAN _pc_cfilio_check_read_wrap(PC_FILE *pefile, BOOLEAN after_read)
{
ddword file_pointer_ddw;

    file_pointer_ddw = pc_efilio_get_fp_ddw(pefile);
    if ( M64GTEQ(file_pointer_ddw, pefile->fc.plus.circular_file_size_ddw) )
    {
        dword ltemp, ltemp2;
        if (!_pc_efilio_lseek(pefile, 0, 0, PSEEK_SET, &ltemp, &ltemp2))
            return(FALSE);
        if (after_read)
        { /* Update the window base if we  read to the wrap point */
            pefile->fc.plus.circular_file_base_ddw =
                 M64PLUS(pefile->fc.plus.circular_file_base_ddw,pefile->fc.plus.circular_file_size_ddw);
        }
    }
    return(TRUE);
}

dword _pc_cfilio_get_max_read_count(PC_FILE *pefile, dword count)
{
ddword nleft_ddw,circular_max_offset_ddw,max_count_ddw,count_ddw,file_pointer_ddw;
PC_FILE *pwriterefile;

    count_ddw = M64SET32( 0, count);
    pwriterefile = pefile->fc.plus.psibling;
    /* only allow reading up to the end of data*/

    /* Get the current stream pointer, that's the linear file pointer plus
       the stream offset at the beginning of the file */
    file_pointer_ddw    = pc_cfilio_get_fp_ddw(pefile);
    circular_max_offset_ddw = pwriterefile->fc.plus.circular_max_offset_ddw;
    if (M64GTEQ(file_pointer_ddw,circular_max_offset_ddw))
        return(0);
    max_count_ddw = M64MINUS(circular_max_offset_ddw, file_pointer_ddw);
    /* If > file size means the seek pointer is before the file */
    if (M64GT(max_count_ddw,pefile->fc.plus.circular_file_size_ddw))
        return(0);
    /* only allow reading up to wrap point */
    file_pointer_ddw    = pc_efilio_get_fp_ddw(pefile);
    nleft_ddw = M64MINUS(pefile->fc.plus.circular_file_size_ddw,file_pointer_ddw);

    /* Take the smaller of the two */
    if (M64LT(nleft_ddw,max_count_ddw))
            max_count_ddw = nleft_ddw;

    if (M64GT(count_ddw,max_count_ddw))
        count_ddw = max_count_ddw;
    return(M64LOWDW(count_ddw));
}


PC_FILE *pc_cfilio_fd2file(int fd, BOOLEAN unlock)
{
    PC_FILE *pefile;
    pefile = pc_fd2file(fd, 0);
    if (!pefile)
        return(0); /* pc_fd2file set errno */
    if (pc_check_cfile_open_mode(pefile))
    {
        if (unlock)
            release_drive_mount(pefile->pobj->pdrive->driveno);/* Release lock, unmount if aborted */
        return(pefile);
    }
    else
    {
        release_drive_mount(pefile->pobj->pdrive->driveno);
        return(0);
    }
}

static BOOLEAN pc_check_cfile_open_mode(PC_FILE *pefile)
{
    if (
        !pefile->is_free
        && (pefile->fc.plus.allocation_policy & (PCE_CIRCULAR_FILE|PCE_CIRCULAR_BUFFER))
        && pefile->fc.plus.psibling
        && pefile->fc.plus.psibling->pobj
        && (!pefile->fc.plus.psibling->is_free)
        )
    {
#if (INCLUDE_ASYNCRONOUS_API)
        if (_pc_check_if_async(pefile))
        {
            rtfs_set_errno(PEEINPROGRESS, __FILE__, __LINE__);
            return(FALSE);
        }
#endif
        return(TRUE);
    }
    else
    {
        rtfs_set_errno(PEEFIOILLEGALFD, __FILE__, __LINE__);
        return(FALSE);
    }
}


/* Generic 64 bit file routines routines */
void pc_cfilio_clear_open_flags(PC_FILE *pefile, word flags)
{
    pefile->pobj->finode->openflags &= ~flags;
}
void pc_cfilio_set_open_flags(PC_FILE *pefile, word flags)
{
    pefile->pobj->finode->openflags |= flags;
}

ddword pc_efilio_get_fp_ddw(PC_FILE *pefile)
{
ddword file_pointer_ddw;
#if (INCLUDE_EXFATORFAT64)
	if (ISEXFATORFAT64(pefile->pobj->pdrive))
	    file_pointer_ddw = pefile->fc.plus.file_pointer.val64;
    else
#endif
	    file_pointer_ddw = M64SET32(0, pefile->fc.plus.file_pointer.val32);

    return(file_pointer_ddw);
}

ddword pc_cfilio_get_fp_ddw(PC_FILE *pefile)
{
ddword file_pointer_ddw;
    /* Get the linear file pointer.. */
#if (INCLUDE_EXFATORFAT64)
	if (ISEXFATORFAT64(pefile->pobj->pdrive))
	    file_pointer_ddw = pefile->fc.plus.file_pointer.val64;
    else
#endif
	    file_pointer_ddw = M64SET32(0, pefile->fc.plus.file_pointer.val32);

    /* Add the segment */
    file_pointer_ddw = M64PLUS(file_pointer_ddw,pefile->fc.plus.circular_file_base_ddw);
    return(file_pointer_ddw);
}

ddword pc_cfilio_get_file_size_ddw(PC_FILE *pefile)
{
#if (INCLUDE_EXFATORFAT64)
	if (ISEXFATORFAT64(pefile->pobj->pdrive))
        return(pefile->pobj->finode->fsizeu.fsize64);
	else
#endif
        return(M64SET32(0 , pefile->pobj->finode->fsizeu.fsize));

}

void _pc_cfilio_set_file_size(PC_FILE *pefile, dword dstart, dword length_hi, dword length_lo)
{
		if (dstart)
			pc_efilio32_set_dstart(pefile, dstart);
        _pc_efinode_set_file_size(pefile->pobj->finode, length_hi, length_lo);
}

#endif /* (INCLUDE_CIRCULAR_FILES) */
#endif /* Exclude from build if read only */
