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

*/

#include "rtfs.h"


/*
    ERTFS Extended IO Functions

    Introduction
        The ERTFS-Pro Extended IO (efilio) package uses the ERTFS-Pro
        region manager package to provide enhanced file read, file write and
        file seek performance. The determinism and increased raw performance
        of these functions are major improvements over earlier versions of
        ERTFS and other available file systems.

        Some of the advancements in the efilio package are:

        . Zero disk latency cluster allocation and linking - During write
          operations all cluster management is done on in-memory region
          caches. This allows write operations to operate in near real
          time. Disk operations pertaining to cluster management are
          deferred until the file is closed or flushed.
        . Zero disk latency seek operations - A seek may be performed from
          any location in a file to any location in a file without
          ever accessing the disk. This makes file seek operations
          essentially real time operations.
        . Full four gigabyte file support - All file pointer and transfer
          size arguments are unsigned long values and provide seemless
          operation of files from zero bytes to four gigabytes.
        . Contiguous file support using cluster pre-allocation - The user
          may open the file and instruct ERTFS-Pro to preallocate some
          minimum number of contiguous clusters when the file is extended.
          This way, the file can grow into that preallocated region of
          clusters as it is written to. If the preallocation value is
          set at an application determined maximum practical limit then the
          file can be guaranteed to be contiguous. Unused clusters in the
          preallocated region are automatically released when the file is
          closed.
          The following functions are include in the enhance file IO pacakge:
            pc_efilio_open()
            pc_efilio_lseek()
            pc_efilio_read()
            pc_efilio_write()
            pc_efilio_close()
            pc_efilio_stat()

*/

/****************************************************************************
pc_efilio_open -  Open a file for extended IO operations

    Summary:
    int pc_efilio_open(name, flag, mode, poptions)
        byte *name  - File name
        word flag   - Flag values.
        word mode   - Mode values.
        EFILEOPTIONS *poptions - Extended options

  Description

  Open the file for access as specified in flag with additional
  options specified in the poptions structure.

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


      Mode values are

      PS_IWRITE   - Write permitted
      PS_IREAD    - Read permitted. (Always true anyway)

      Extended options -

      If the poptions argument is zero no extended options are used, otherwise
      the options structure must be be zeroed and it's fields must be
      inititialized properly before they are passed.


      The options structure is:


        typedef struct efileoptions {
            dword allocation_policy;
            dword min_clusters_per_allocation;
            byte  *transaction_buffer;
            dword transaction_buffer_size;
            dword circular_file_size_hi;
            dword circular_file_size_lo;
            int   n_remap_records;
            REMAP_RECORD *remap_records;
        } EFILEOPTIONS;


        allocation_policy - This field contains bit flags that may be set
        by the user to modifio the behavior of the extended file IO routines.

        The following options may be used:

        PCE_TRANSACTION_FILE - Open the file in transaction mode. In
        transaction mode, when RTFS returns from pc_efilio_write(), it
        guarantees that the data is written to the volume and will survive
        power loss. If power is interrupted before pc_efilio_write() returns
        then it is guaranteed that the file is unchanged.

        If the write operation overwrites an existing region, RTFS
        insures that the overwrite may be rewound if a power outage occurs
        before it completes. If the data is cluster alligned this is done
        with no copying, and RTFS achieves similar overwrite performance in
        transactional mode as it does in normal mode.  When the data is not
        cluster aligned, RTFS still performs similarly by doing minimal copying
        (one cluster or less) and using a special buffering scheme.

        In transaction mode, RTFS automatically flushes Failsafe buffers
        to disk before pc_efilio_write() returns. This additional step adds
        typically one or two additional block writes per write call. This
        reduces performance somewhat over non transactional files but
        still provides reasonably high performance.

        Notes:
            Failsafe must be enabled to use this option.
            To use this option the fields in the
            options structure named transaction_buffer and
            transaction_buffer_size must also be intialized.
            (see below for a description of)

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

        PCE_ASYNC_OPEN       - Perform an asynchronous open of the file.
        If PCE_ASYNC_OPEN is enabled pc_efilio_open() returns quickly
        after it has determined that the arguments are valid and it has
        created the directory entry on a file create or for a file re-open
        , after it has loaded the directory entry contents. If this option
        is not enabled then pc_efilio_open() will make the necessary disk
        accesses required to load the file's FAT based extent maps. On small
        files, even file up to several megabytes in size this will not be
        noticable, but on very large files this may introduce a perceptible
        delay.
        The application must call pc_efilio_async_continue() to complete the
        open operation so the file descriptor may used by the API.


        PCE_FORCE_CONTIGUOUS - Select this option to force calls write
        calls to fail if the whole request can not be fullfilled in
        a contiguous extent.

        PCE_KEEP_PREALLOC   - Select this option to force excess preallocated
        clusters (see: min_clusters_per_allocation, below) to be
        incorporated into the file when it is closed. If this option is
        not selected, then excess preallocated clusters are returned to
        freespace when the file is closed.

        PCE_TEMP_FILE        - Select this option to force ERTFS Pro Plus
        to consider this to be a termporary file and release the directory
        entry and free all clusters when the file is closed. If the file
        already exists the open will fail and errno will be set to PEEXIST.

        Note: Since the cluster chains of files opened with the
        PCE_TEMP_FILE option are never actually commited to the disk based
        FAT table,  opening with the PCE_TEMP_FILE option is more efficient
        than creating a normal file and deleting it after it is closed.


        PCE_REMAP_FILE       - Select this option if the file will be
        used as an extract region and argument to pc_cfilio_extract.
        If this option is used the file descriptor may be read bu it
        may not be written. It can only be populated by calling
        pc_cfilio_extract.


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

        The following fields must be initialized if the
        PCE_TRANSACTION_FILE option is selected.

        transaction_buffer - This field must contain the address
        of a memory buffer that is large enough to hold one cluster
        of data.


        transaction_buffer_size - This field must contain the size of the
        transaction_buffer in blocks. It must be greater than or equal to the
        volume's cluster size.

        For example: If cluster the cluster size it 16 K (32 blocks),
        then transaction_buffer_size should be set to 32, and
        transaction_buffer should point to a 16 K memory buffer.

        The following options are reserved for calls to pc_cfilio_open()
        and must be set to zero.

        circular_file_size_hi  - Set this value to zero
        circular_file_size_lo  - Set this value to zero
        n_remap_records        - Set this value to zero
        remap_records          - Set this value to zero

      Returns a non-negative integer to be used as a file descriptor for
      calling pc_efilio64_read,pc_efilio64_write,pc_efilio64_lseek,
      pc_efilio64_close, pc_efilio64_stat and pc_cfilio_extract.
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
int _pc_efilio_open(DDRIVE *pdrive, byte *name, word flag, word mode, EFILEOPTIONS *poptions, int use_charset)
{
    int fd;

    if ( (flag & PO_TRUNC) &&
         (poptions && poptions->allocation_policy & PCE_ASYNC_OPEN) )
    {
#if (INCLUDE_FAILSAFE_CODE)
#if (INCLUDE_TRANSACTION_FILES)
bad_parms:
#endif
#endif
         /* Truncate not supported on ASYNC open */
        rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
        return(-1);
    }
#if (INCLUDE_FAILSAFE_CODE)
#if (INCLUDE_TRANSACTION_FILES)
    /* Transaction files must have a cluster sized buffer */
    /* Can't be buffered and must be open for write */
    if (poptions && (poptions->allocation_policy & PCE_TRANSACTION_FILE))
    {
        if (pdrive->drive_state.drive_async_state != DRV_ASYNC_IDLE)
        {
            /* Can't process transaction while async operation in progress */
            rtfs_set_errno(PEEINPROGRESS, __FILE__, __LINE__);
            return(-1);
        }
        /* Failsafe must be enabled */
        if (!pdrive->drive_state.failsafe_context)
            goto bad_parms;
        if ( poptions->transaction_buffer_size < (dword)pdrive->drive_info.secpalloc ||
             !poptions->transaction_buffer)
            goto bad_parms;
        if (flag & (PO_BUFFERED|PO_AFLUSH))
            goto bad_parms;
        if (poptions->allocation_policy &
            (PCE_KEEP_PREALLOC|PCE_CIRCULAR_FILE|
             PCE_CIRCULAR_BUFFER|PCE_REMAP_FILE|PCE_TEMP_FILE))
            goto bad_parms;
    }
#endif
#endif

    {
        fd = _pc_efilio32_open(pdrive, name, flag, mode, poptions, use_charset);
    }
#if (INCLUDE_FAILSAFE_CODE)
#if (INCLUDE_TRANSACTION_FILES)
    if (fd >= 0 && poptions && (poptions->allocation_policy & PCE_TRANSACTION_FILE))
    {
    PC_FILE *pefile;

         pefile = prtfs_cfg->mem_file_pool+fd;

/* NOTE - Have to move transaction stuff lower into finodes so they can be shared.*/
        pefile->fc.plus.transaction_buffer = poptions->transaction_buffer;
        if (!(_pc_efilio_flush(pefile) &&
              fs_flush_transaction(pdrive)) )
        {
           pc_freefile(pefile);
           fd = -1;
        }
    }
#endif
#endif
    return(fd);
}

#if (INCLUDE_CS_UNICODE)
int pc_efilio_open_cs(byte *name, word flag, word mode, EFILEOPTIONS *poptions, int use_charset)
#else
int pc_efilio_open(byte *name, word flag, word mode, EFILEOPTIONS *poptions)
#endif
{
    int fd, driveno;

    rtfs_clear_errno();

    if (poptions && (poptions->allocation_policy & (PCE_CIRCULAR_FILE|PCE_CIRCULAR_BUFFER)))
    { /* circular files must use pc_cfilio_open */
        rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
        return(-1);
    }
     /* Get the drive and make sure it is mounted   */
    driveno = check_drive_name_mount(name, CS_CHARSET_ARGS);
    if (driveno < 0)   /*  errno was set by check_drive */
        return(-1);

    fd = _pc_efilio_open(pc_drno2dr(driveno), name, flag, mode, poptions, CS_CHARSET_ARGS);
    if (!release_drive_mount_write(driveno))/* Release lock, unmount if aborted */
        return(-1);
    return(fd);
}

/****************************************************************************
    pc_efilio_close - Close an extended 32 bit file or 64 bit metafile

  Summary:
    BOOLEAN pc_efilio_close(int fd)
     int fd - A file descriptor that was returned from a succesful call
     to pc_efilio_open.

  Description
    Flush the directory entry and flush the fat chaan to disk. Process any
    deferred cluster chain linking and free all core associated with the file
    descriptor.

Returns
    Returns TRUE if all went well otherwise it returns FALSE.

    errno is set to one of the following

    0               - No error
    PEBADF          - Invalid file descriptor
    PEEFIOILLEGALFD - The file not open in extended IO mode.
    An ERTFS system error
****************************************************************************/


BOOLEAN pc_efilio_close(int fd)
{
    PC_FILE *pefile;
    BOOLEAN ret_val,is_aborted;
    CHECK_MEM(BOOLEAN, FALSE)  /* Make sure memory is initted */

    /* Get the file structure. If found semaphore lock the drive */
    is_aborted = FALSE;
    pefile = pc_efilio_start_close(fd, &is_aborted);

    if (!pefile)
        ret_val = is_aborted; /* TRUE (OK) if aborted, else an error */
    else
    {
    int driveno;
        driveno = pefile->pobj->pdrive->driveno;
        if (!_pc_efilio_flush_file_buffer(pefile))
            ret_val = FALSE;
        else
            ret_val = _pc_efilio_close(pefile);
        if (!release_drive_mount_write(driveno))/* Release lock, unmount if aborted */
            ret_val = FALSE;
    }
    return(ret_val);
}

/*
*  Start close processing for synchronous or asynchronous close.
*
*  Test if the FD is valid
*  If not valid:
*    Test if the file was closed by a drive removal event.
*       if so:
*           Detach file from circular buffer if it is an extract file
*           set is_aborted to TRUE,
*    return NULL with the drive not claimed.
*  If valid:
*    Detach file from circular buffer if it is an extract file
*    set is_aborted to FALSE
*    return file structure with the drive claimed
*
*
*/

PC_FILE *pc_efilio_start_close(int fd, BOOLEAN *is_aborted)
{
    PC_FILE *pefile;

    *is_aborted = FALSE;
    rtfs_clear_errno();
    /* Get the file structure and semaphore lock the drive */
    if ( (pefile = pc_fd2file(fd, 0)) == 0)
    {
        if (get_errno() == PECLOSED)
        {
            pefile = prtfs_cfg->mem_file_pool+fd;
#if (INCLUDE_CIRCULAR_FILES)
            if (pefile->fc.plus.allocation_policy & PCE_REMAP_FILE)
                pc_cfilio_release_remap_file(pefile, 1);
#endif
            /* fd2file failed because it was closed by pc_dskfree. */
            /* mark the file free here and set is_aborted status */
            OS_CLAIM_FSCRITICAL()
            pefile = prtfs_cfg->mem_file_pool+fd;
            pefile->is_free = TRUE;
            OS_RELEASE_FSCRITICAL()
            rtfs_clear_errno();    /* clear errno */
            *is_aborted = TRUE;
            pefile = 0;
        }
    }
    else
    {
    int driveno;
        driveno = pefile->pobj->pdrive->driveno;
        if (!_pc_check_efile_open_mode(pefile))
        {
            release_drive_mount_write(driveno);
            pefile = 0;
        }
#if (INCLUDE_CIRCULAR_FILES)
        else
        {
            if (pefile->fc.plus.allocation_policy & PCE_REMAP_FILE)
                pc_cfilio_release_remap_file(pefile,0);
        }
#endif
    }
    return(pefile);
}

BOOLEAN _pc_efilio_close(PC_FILE *pefile)
{
     return(_pc_efilio32_close(pefile));
}

/*
pc_efilio_lseek  - Move the file pointer of an extended 32 bit file or 64
                   bit metafile

Summary:
  BOOLEAN pc_efilio_lseek(fd, offset_hi, offset_lo, origin,
                            *poffset_hi, *poffset_lo)

        int fd - A file descriptor that was returned from a succesful
                 call to pc_cfilio_open.
        dword offset_hi     - High 32 bit word of the 64 bit offset from the
                              beginning of the file. For 32 bit files this
                              argument must be zero.
        dword offset_lo     - Low 32 bit word of the 64 bit offset from the
                              beginning of the file.
        int origin          - Origin and direction of the request (see below)

        dword *poffset_hi -  The high dword of the new 64 bit offset from the
        beginning of the file is returned in *poffset_lo. For 32 bit files
        this argument is ignored.

        dword *poffset_lo -  The low dword of the offset from the
        beginning of the linear file is returned  in *poffset_lo.


  Description
    pc_efilio_lseek takes advantage of the extended filio subsystem to
    perform file seeks with zero disk latency. A seek may be performed
    from anywhere to anywhere in a file almost instantaneously.
    The offset field is an unsigned long 64 bit value so a single seek may
    move the file pointer up to 0xffffffffffffffff bytes if the file is a
    64 bit meat file or 0xffffffff bytes for a 32 bit file.

    If a negative offset from the current file pointer is required the
    origin value PSEEK_CUR_NEG may be used. Seeks from PSEEK_END are
    always made in the negative direction from the end of file.


    The file pointer is set according to the following rules.

    Origin              Rule
    PSEEK_SET           offset from begining of file
    PSEEK_CUR           positive offset from current file pointer
    PSEEK_CUR_NEG       negative offset from current file pointer
    PSEEK_END           0 or negative offset from end of file

  If a PSEEK_CUR operation attempts to move the file pointer beyond the end
  of file, the pointer is moved to the end of file.

  If a PSEEK_CUR_NEG or PSEEK_END operation tries to place the file pointer
  before zero the file pointer is placed at zero.

    To query the current file pointer call:
        pc_efilio_lseek(fd, 0, 0, PSEEK_CUR, &offset_hi, &offset_lo)

    To report the file size in end_hi:end_lo without moving the file pointer:
        pc_efilio_lseek(fd, 0, 0, PSEEK_CUR, &temp_hi, &temp_lo);
        pc_efilio_lseek(fd, 0, 0, PSEEK_END, &end_hi, &end_lo);
        pc_efilio_lseek(fd, temp_hi, temp_lo, PSEEK_SET, &temp_hi, &temp_lo);


    Returns
       Returns TRUE on succes, FALSE on error.

       If succesful *pnewoffset_hi:*poffest_lo contains the new file pointer

     errno is set to one of the following
        0               - No error
        PEBADF          - Invalid file descriptor
        PEINVALIDPARMS  - Bad or missing argument
        PECLOSED        - File is no longer available.  Call pc_efilio_close().
        PEEFIOILLEGALFD - The file not open in extended IO mode.
        An ERTFS system error
*****************************************************************************/

BOOLEAN pc_efilio_lseek(int fd, dword offset_hi, dword offset_lo, int origin, dword *pnewoffset_hi, dword *pnewoffset_lo)
{
BOOLEAN ret_val;
PC_FILE *pefile;

    CHECK_MEM(BOOLEAN, 0) /* Make sure memory is initted */
    rtfs_clear_errno();    /* clear errno */
    if (!pnewoffset_lo)
    { /* check that offset pointer is valid */
        rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
        return(FALSE);
    }
    pefile = pc_fd2file(fd, 0);
    if (!pefile)
        return(FALSE);              /* pc_fd2file() set errno */
    ret_val = FALSE;
    if (_pc_check_efile_open_mode(pefile))
    {
	dword dummy;
		if (!pnewoffset_hi)
			pnewoffset_hi=&dummy;
        ret_val = _pc_efilio_lseek(pefile, offset_hi, offset_lo, origin, pnewoffset_hi, pnewoffset_lo);
    }
    release_drive_mount(pefile->pobj->pdrive->driveno);/* Release lock, unmount if aborted */
    return(ret_val);
}


#if (INCLUDE_MATH64)	 /* Checking is 64 bit file */
ddword pc_efilio_lseek64(int fd, ddword offset, int origin)
{
PC_FILE *pefile;
ddword  new_offset;
    rtfs_clear_errno();
	new_offset = M64SET32(0,0);
    pefile = pc_fd2file(fd, 0);
    if (pefile)
    {
		dword dwnewoffset,dwnewoffset_hi,targetoffset,targetoffset_hi;
		targetoffset_hi=0;
		targetoffset = M64LOWDW(offset);
		dwnewoffset_hi=dwnewoffset=0;
#if (INCLUDE_EXFATORFAT64)
		if (ISEXFATORFAT64(pefile->pobj->pdrive))
		{
			targetoffset_hi= M64HIGHDW(offset);
		}
#endif
       	if (!_pc_efilio_lseek(pefile, targetoffset_hi, targetoffset, origin, &dwnewoffset_hi, &dwnewoffset))
			new_offset = M64SET32(0xffffffff,0xffffffff);
       	else
        	new_offset = M64SET32(dwnewoffset_hi,dwnewoffset);
        release_drive_mount(pefile->pobj->pdrive->driveno);
    }
    return(new_offset);
}

#endif


dword _pc_efilio_first_cluster(PC_FILE *pefile)
{
FINODE *pefinode;

    pefinode = pefile->fc.plus.ffinode;
    if (!pefinode || !pefinode->e.x->pfirst_fragment)
        return(0);
    else
        return(pefinode->e.x->pfirst_fragment->start_location);
}
dword _pc_efilio_last_cluster(PC_FILE *pefile)
{
FINODE *pefinode;

    pefinode = pefile->fc.plus.ffinode;
    if (!pefinode || !pefinode->e.x->plast_fragment)
        return(0);
    else
        return(pefinode->e.x->plast_fragment->end_location);
}

void _pc_efilio_reset_seek(PC_FILE *pefile)
{
    _pc_efiliocom_reset_seek(pefile,pefile->fc.plus.ffinode);
}

void _pc_efilio_coalesce_fragments(PC_FILE *pefile)
{
    if (pefile->pobj && pefile->pobj->finode)
        _pc_efinode_coalesce_fragments(pefile->fc.plus.ffinode);
}

BOOLEAN _pc_efilio_lseek(PC_FILE *pefile, dword offset_hi, dword offset_lo, int origin, dword *pnewoffset_hi, dword *pnewoffset_lo)
{
BOOLEAN ret_val;
dword _temp;
    *pnewoffset_lo = 0;
    if (!pnewoffset_hi)
        pnewoffset_hi = &_temp;
    ret_val = _pc_efiliocom_lseek(pefile,pefile->fc.plus.ffinode, offset_hi, offset_lo, origin, pnewoffset_hi, pnewoffset_lo);
    return(ret_val);
}

/****************************************************************************
pc_efilio_read  - Read from an extended 32 bit file or 64 bit metafile

  Summary
    BOOLEAN pc_efilio_read(fd, buf, count, nread)
        int fd - A file descriptor that was returned from a succesful call
         to pc_efilio_open.

        dword count - The length of the read request, (0 to 0xffffffff)
        byte *buf - Buffer where data is to be placed. If buf is a null
            pointer pc_efilio_read() will proceed as as usual but it
            will not transfer bytes to the buffer.
        dword *nread - Returns the number of bytes read.

  Description
    pc_efilio_read takes advantage of the extended filio subsystem to
    perform file reads. There is guaranteed no disk latency required for
    mapping file extents to cluster regions, so reads may be performed at
    very near the bandwidth of the underlying device.

    pc_efilio_read attempts to read count bytes or to the end of file,
    whichever is less, from the current file pointer.
    The vaule of count may be up 0xffffffff. If the read count plus
    the current file pointer exceeds the end of file the read count is
    truncated to the end of file.

    Note: If buf is 0 (the null pointer) then the operation is performed
          identically to a normal read except no data transfers are
          performed. This may be used to quicky advance the file pointer.

  Returns:
      TRUE if no errors were encountered. FALSE otherwise.
      *nread is set to the number of bytes successfully read.

  errno is set to one of the following
    0               - No error
    PEBADF          - Invalid file descriptor
    PECLOSED        - File is no longer available.  Call pc_efilio_close().
    PEINVALIDPARMS  - Bad or missing argument
    PEEFIOILLEGALFD - The file not open in extended IO mode.
    PEIOERRORREAD   - Read error
    An ERTFS system error
*****************************************************************************/


BOOLEAN pc_efilio_read(int fd, byte *buf, dword count, dword *nread)
{
BOOLEAN ret_val;
PC_FILE *pefile;

    CHECK_MEM(BOOLEAN, 0) /* Make sure memory is initted */
    rtfs_clear_errno();    /* clear errno */

    if (!nread || !count)
    {
        rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
        return(FALSE);
    }
    pefile = pc_fd2file(fd, 0);
    if (!pefile)
        return(FALSE); /* pc_fd2file set errno */
    if (_pc_check_efile_open_mode(pefile))
        ret_val = _pc_efilio_read(pefile, buf, count, nread);
    else
        ret_val = FALSE;
    release_drive_mount(pefile->pobj->pdrive->driveno);/* Release lock, unmount if aborted */
    return(ret_val);
}

BOOLEAN _pc_efilio_read(PC_FILE *pefile, byte *buf, dword count, dword *nread)
{
    return(_pc_efilio32_read(pefile, buf, count, nread));
}

/****************************************************************************
    pc_efilio_fstat - Report statistics on an extended 32 bit file or 64 bit
    metafile.

 Summary
    BOOLEAN pc_efilio_fstat(int fd, ERTFS_EFILIO_STAT *pestat)
       fd - A file descriptor that was returned from a succesful call
        to pc_efilio_open.

        pestat - The address of an ERTFS_EFILIO_STAT structure that will be
        filled in by this function.

 Description
    Fills in the extended stat structure with information about the open
    open file.

    The extended stat structure contains the following fields.

    dword minimum_allocation_size;
        Minimum number of bytes that will are pre-allocated at one time
        when the file is extended- by default this is the cluster size
        of the volume but it may be effected by the extended file open
        option "min_clusters_per_allocation"

    dword allocation_policy;
        These are the policy bits that were set in the allocation_policy
        field of the extended file open call. Please see the documentation
        for pc_efilio_open and pc_cfilio_open for a description of the
        alocation policy bits.

    dword fragments_in_file;
        The number of seperate disjoint fragments in the file

    dword file_size_hi
    dword file_size_lo
        The current file size in bytes.. file_size_hi and file_size_lo
        are the high and low 32 bit words of the 64 bit file length. If
        the file is a 32 bit file file_size_hi will always be zero. For a
        64 bit metafile file_size_hi will be non zere if the file is
        over four gigabytes in length.

    dword allocated_size_hi
    dword allocated_size_lo
        The number of bytes currently allocated to the file including
        the file contents (current_file_size) and any additional
        blocks that were preallocated due to minimum allocation
        guidelines
        Allocated_size_hi and allocated_size_lo are the high and low 32 bit
        words of the 64 bit allocated file size. If the file is a 32 bit
        file allocated_size_hi will always be zero. For a 64 bit metafile
        allocated_size_hi will be non zerO if the file is over four gigabytes
        in length.

    dword file_pointer_hi;
    dword file_pointer_lo;
        High and low 32 bit words of the current file pointer.

    REGION_FRAGMENT *pfirst_fragment;
        This is the list of cluster fragments that make up the file. It
        is useful to use for test and diagnostic procedures and to
        study file allocation patterns. It should not be changed.

        The region fragment structure is declared as follows:

            typedef struct region_fragment {
                    dword start_location;
                    dword end_location;
                    struct region_fragment *pnext;
                    } REGION_FRAGMENT;


 Returns
    Returns TRUE if all went well otherwise it returns FALSE

    errno is set to one of the following
    0               - No error
    PEBADF          - Invalid file descriptor
    PECLOSED        - File is no longer available.  Call pc_efilio_close().
    PEINVALIDPARMS  - Bad or missing argument. Or trying to stat a
                      circular file
    PEEFIOILLEGALFD - The file not open in extended IO mode.
    An ERTFS system error
****************************************************************************/

BOOLEAN pc_efilio_fstat(int fd, ERTFS_EFILIO_STAT *pestat)                              /*__apifn__*/
{
PC_FILE *pefile;

BOOLEAN ret_val;
CHECK_MEM(int, 0)  /* Make sure memory is initted */

    rtfs_clear_errno();  /* pc_fstat: clear error status */
    if (!pestat)
    {
        rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
        return(FALSE);
    }

    ret_val = FALSE;
    /* Get the file structure and semaphore lock the drive */
    pefile = pc_fd2file(fd, 0);
    if (pefile)
    {
        if (_pc_check_efile_open_mode(pefile))
             ret_val = _pc_efilio_fstat(pefile, pestat);
        release_drive_mount(pefile->pobj->pdrive->driveno);/* Release lock, unmount if aborted */
    }
    return(ret_val);
}

BOOLEAN _pc_efilio_fstat(PC_FILE *pefile, ERTFS_EFILIO_STAT *pestat)                              /*__apifn__*/
{
BOOLEAN ret_val;

    ret_val = FALSE;
    {
        rtfs_memset(pestat, 0, sizeof(*pestat));
        /* Update the traditional stat structure */
        pc_finode_stat(pefile->pobj->finode, &pestat->stat_struct);
        pestat->minimum_allocation_size = pefile->fc.plus.min_alloc_bytes;
        pestat->allocation_policy       = pefile->fc.plus.allocation_policy;

            /* call pc_efilio_common_stat() to update generic part of stat structure   */
        {
            if (_pc_efilio32_common_fstat(pefile, pestat))
                ret_val = _pc_efiliocom_lseek(pefile,pefile->fc.plus.ffinode, 0,0, PSEEK_CUR,&(pestat->file_pointer_hi), &(pestat->file_pointer_lo));
        }
    }
    if (ret_val)
    { /* Update file size and blocks in file in traditional stat structure */
        ddword fsize_ddw,blocks_ddw;
        pestat->stat_struct.st_size_hi = pestat->file_size_hi;
        pestat->stat_struct.st_size = pestat->file_size_lo;
        fsize_ddw = M64SET32(pestat->file_size_hi,pestat->file_size_lo);
        fsize_ddw = M64PLUS32(fsize_ddw, (pefile->pobj->pdrive->drive_info.bytespsector-1));
        blocks_ddw = M64RSHIFT(fsize_ddw, pefile->pobj->pdrive->drive_info.log2_bytespsec);
        pestat->stat_struct.st_blocks  =  (dword) M64LOWDW(blocks_ddw);
    }
    return(ret_val);
}


int _pc_efinode_get_file_extents(FINODE *pefinode,int infolistsize, FILESEGINFO *plist, BOOLEAN report_clusters, BOOLEAN raw);

int pc_get_file_extents(int fd, int infolistsize, FILESEGINFO *plist, BOOLEAN report_clusters, BOOLEAN raw) /* __apifn__ */
{
    return(pc_efilio_get_file_extents(fd, infolistsize, plist, report_clusters, raw));
}

int pc_efilio_get_file_extents(int fd, int infolistsize, FILESEGINFO *plist, BOOLEAN report_clusters, BOOLEAN raw) /* __apifn__ */
{
PC_FILE *pefile;
int ret_val;
dword orgfp_hi, orgfp_lo, fsize_hi, fsize_lo;

    CHECK_MEM(int, -1) /* Make sure memory is initted */
    rtfs_clear_errno();    /* clear errno */
    pefile = pc_fd2file(fd, 0);
    if (!pefile)
        return(-1); /* pc_fd2file set errno */
    ret_val = -1;

    /* Seek to the end and then restore file pointer to make sure all cluster fragments are read in */
    if (!_pc_efilio_lseek(pefile, 0, 0, PSEEK_CUR, &orgfp_hi, &orgfp_lo))
	    goto ex_it;
    if (!_pc_efilio_lseek(pefile, 0, 0, PSEEK_END, &fsize_hi, &fsize_lo))
	    goto ex_it;
    if (!_pc_efilio_lseek(pefile, 0, 0, PSEEK_SET, &orgfp_hi, &orgfp_lo))
	    goto ex_it;

    ret_val = _pc_efinode_get_file_extents(pefile->pobj->finode, infolistsize, plist, report_clusters, raw);

ex_it:
    release_drive_mount(pefile->pobj->pdrive->driveno);/* Release lock, unmount if aborted */
    return(ret_val);
}


BOOLEAN _pc_check_efile_open_mode(PC_FILE *pefile)
{

    if (pefile->is_free ||
        (pefile->fc.plus.allocation_policy & (PCE_CIRCULAR_FILE|PCE_CIRCULAR_BUFFER)))
    {
        rtfs_set_errno(PEEFIOILLEGALFD, __FILE__, __LINE__);
        return(FALSE);
    }
#if (INCLUDE_ASYNCRONOUS_API)
    if (_pc_check_if_async(pefile))
    {
        rtfs_set_errno(PEEINPROGRESS, __FILE__, __LINE__);
        return(FALSE);
    }
#endif
    return(TRUE);
}
