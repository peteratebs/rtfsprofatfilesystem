/* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
#include "rtfs.h"

#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */

#if (INCLUDE_CIRCULAR_FILES)

ddword pc_cfilio_get_fp_ddw(PC_FILE *pefile);
/*
  The following public API routines are provided by this module:

pc_cfilio_lseek    -  Move linear read or write file pointer of a circular
                      file.
pc_cstreamio_lseek -  Move read or write stream pointer of a circular file.

*/

/**************************************************************************
pc_cfilio_lseek  - Move linear read or write file pointer of a circular file

Summary:
  BOOLEAN pc_cfilio_lseek(fd, which_pointer, off_hi, off_lo, origin, *poff_hi, *poff_lo)

        int fd - A file descriptor that was returned from a succesful
                 call to pc_cfilio_open.
        int which_pointer - Which file pointer read or write
            CFREAD_POINTER  - Move the read file pointer
            CFWRITE_POINTER - Move the write file pointer
        dword offset_hi     - High 32 bit word of the 64 bit offset from the
                              beginning of the file. For 32 bit files must
                              be zero.
        dword offset_lo     - Low 32 bit word of the 64 bit offset from the
                              beginning of the file.
        int origin          - Origin and direction of the request (see below)


        dword *poffset_hi -  The high dword of the new 64 bit offset from the
        beginning of the underlying linear file is returned in *poffset_lo.

        dword *poffset_lo -  The low dword of the new 64 bit offset from the
        beginning of the underlying linear file is returned  in *poffset_hi.

  Description
    pc_cfilio_lseek moves the linear read or write file pointer of the
    linear file underlying the circular file. This function selects the
    appropriate read or write file structure and then calls the
    underlying pc_efilio_lseek() to move the file pointer according to the
    rules of pc_efilio_lseek().

    The file pointer is not the same as the stream pointer that is used
    by pc_cstreamio_lseek(). The file pointer may not exceed the current
    file length and at steady state this maximum will be the circular file
    wrap point that was provided when the file was opened. The stream
    pointer is not bounded like the file pointer, it begins at zero
    and extends to the 64 bit value 0xffffffffffffffff. At a given time
    valid values for the stream pointer are contained in a sliding
    window that extends backwards from the last logical offset
    written to, to the last logical offset written to minus the size of
    the circular file. If the file has not yet reached the wrap point the
    window extends from zero to the wrap point.


    The file pointer is set according to the following rules.

    PSEEK_SET           offset from begining of file
    PSEEK_CUR           positive offset from current file pointer
    PSEEK_CUR_NEG       negative offset from current file pointer
    PSEEK_END           0 or negative offset from end of file


  If a PSEEK_CUR operation attempts to move the file pointer beyond the end
  of file, the pointer is moved to the end of file.

  If a PSEEK_CUR_NEG or PSEEK_END operation tries to place the file pointer
  before zero the file pointer is placed at zeor.

    To query the current file pointer call:
        pc_cfilio_lseek(fd, 0, 0, PSEEK_CUR, &offset_hi, &offset_lo)




    Returns
       Returns TRUE on succes, FALSE on error.

       If succesful *poffset_hi:*poffset_lo contains the new file pointer

     errno is set to one of the following
        0               - No error
        PEBADF          - Invalid file descriptor
        PEINVALIDPARMS  - Bad or missing argument
        PECLOSED        - File is no longer available.  Call pc_cfilio_close().
        PEEFIOILLEGALFD - The file is not a valid circular file descriptor
        An ERTFS system error
*****************************************************************************/

BOOLEAN pc_cfilio_lseek(int fd, int which_pointer, dword offset_hi, dword offset_lo, int origin, dword *poffset_hi, dword *poffset_lo)
{
PC_FILE *pwriterefile;
BOOLEAN ret_val;

    rtfs_clear_errno();
    if ( (which_pointer != CFREAD_POINTER) && (which_pointer != CFWRITE_POINTER))
    {
        rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
        return(FALSE);
    }
    /* Get the write file structure */
    pwriterefile = pc_cfilio_fd2file(fd,FALSE); /* Do not unlock */
    ret_val =  _pc_cfilio_lseek(pwriterefile, which_pointer, offset_hi, offset_lo, origin, poffset_hi, poffset_lo);
    release_drive_mount(pwriterefile->pobj->pdrive->driveno);/* Release lock, unmount if aborted */
    return(ret_val);
}

BOOLEAN _pc_cfilio_lseek(PC_FILE *pwriterefile, int which_pointer, dword offset_hi, dword offset_lo, int origin, dword *poffset_hi, dword *poffset_lo)
{
PC_FILE *pefile;
BOOLEAN ret_val;
    if (which_pointer == CFREAD_POINTER)
        pefile = pwriterefile->fc.plus.psibling;   /* Switch to the read file */
    else
        pefile = pwriterefile;             /* Use the write file */
    /* Call the linear file seek */
    ret_val = _pc_efilio_lseek(pefile, offset_hi, offset_lo, origin, poffset_hi, poffset_lo);
    /* If doing a linear seek on the read pointer set the base to the base of the write
       so we are not out of bounds */
    if (which_pointer == CFREAD_POINTER && ret_val)
        pefile->fc.plus.circular_file_base_ddw = pwriterefile->fc.plus.circular_file_base_ddw;

    return(ret_val);
}


/**************************************************************************
pc_cstreamio_lseek  -  Move read or write stream pointer of a circular file

Summary:
  BOOLEAN pc_cstreamio_lseek(fd, off_hi, off_lo, origin, *poff_hi, *poff_lo)
     int fd - A file descriptor that was returned from a succesful call
              to pc_cfilio_open.
    int which_pointer - Which file pointer read or write
        CFREAD_POINTER  - Move the read file pointer
        CFWRITE_POINTER - Move the write file pointer
     dword offset_hi - hi dword 64 bit offset.
     dword offset_lo - lo dword 64 bit offsetto move the file pointer from
         the specified origin.
         Note: the stream pointer is a 64 bit value regardless of whether
         the underlying circular file is a 32 bit file or a 64 bit meta
         file.

     int origin - Origin and direction of the request (see below)
     dword *poffset_hi - returned hi dword 64 bit offset.
     dword *poffset_lo - returned lo dword 64 bit offset, Contains the new
        file pointer value after the seek.

  Description
    pc_cstreamio_lseek moves the read or write stream pointer of a
    circular file. The stream file pointer is the offset in the data stream
    that has been written to the circular file.

    The stream pointer is not the same as the file pointer that is used
    by pc_cfileio_lseek(). The stream pointer is not bounded like the file
    pointer, it begins at zero and extends to the 64 bit value
    0xffffffffffffffff. At a given time valid values for the stream
    pointer are contained in a sliding window that extends backwards
    from the last logical offset written to, to the last logical offset
    written to minus the size of the circular file. If the file has not
    yet reached the wrap point the window extends from zero to the wrap
    point.


  If a PSEEK_SET operation attempts to move the stream pointer beyond the
  end of the sliding window, or before the sliding window the function
  returns FALSE and errno is set to PEINVALIDPARMS.
  To clear this condition and place the stream pointer at the beginning
  of the sliding window call:
        pc_cstreamio_lseek(fd, 0xffffffff, 0xffffffff, PSEEK_END,
                                 &offset_hi, &offset_lo)

  This will place the stream pointer at the beginning of the sliding window.


  If a PSEEK_CUR operation attempts to move the stream pointer beyond the
  end of the sliding window, the pointer is moved to the end of sliding
  window, (the last byte written).

  If a PSEEK_CUR_NEG or PSEEK_END operation tries to place the stream pointer
  before sliding window the stream pointer is placed at the beginning of
  the sliding window (the oldest data in the stream).

    To query the current stream pointer call:
        pc_cstreamio_lseek(fd, 0, 0, PSEEK_CUR, &offset_hi, &offset_lo)

    The end of the data stream is numerically the furthest offset from the
    origin where a byte has been written.

    It is and error to seek beyond the end of the data stream or before
    the beginning of the sliding window location.

    The behavior of the function with each origins is provided here.

    Origin              Rule

    PSEEK_SET           Seek to the absolute location in the data stream.
                        If the provided location is outside of the sliding
                        window return FALSE andset errno to PEINVALIDPARMS.

    PSEEK_CUR           Seek foreward in the data stream. If the provided
                        offset plus the current stream pointer resides
                        outside of the sliding window place the stream
                        pointer at the end of the data stream.

    PSEEK_CUR_NEG       Seek backward in the data stream. If current stream
                        pointer minus the provided offset preceeds the
                        the sliding window place the stream pointer at
                        beginning of the sliding window.

    PSEEK_END           Seek backward in the data stream. If the
                        current end of the data stream minus the provided
                        offset preceeds the sliding window place the stream
                        pointer at beginning of the sliding window.


    Returns
       Returns TRUE on succes, FALSE on error.

       If succesful *poffset_hi:poffset_lo contains the new stream pointer.


     errno is set to one of the following
        0               - No error
        PEBADF          - Invalid file descriptor
        PEINVALIDPARMS  - Bad or missing argument
        PECLOSED        - File is no longer available.  Call pc_cfilio_close().
        PEEFIOILLEGALFD - The file is not a valid circular file descriptor
        An ERTFS system error
*****************************************************************************/

BOOLEAN pc_cstreamio_lseek(int fd, int which_pointer, dword offset_hi, dword offset_lo, int origin, dword *poffset_hi, dword *poffset_lo)
{
PC_FILE *pwriterefile;
ddword roffset_ddw;
BOOLEAN ret_val;
    rtfs_clear_errno();
    if ( (which_pointer != CFREAD_POINTER) && (which_pointer != CFWRITE_POINTER))
    {
        rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
        return(FALSE);
    }
    /* Get the write file structure */
    pwriterefile = pc_cfilio_fd2file(fd,FALSE); /* Do not unlock */
    if (!pwriterefile)
        return(FALSE);
    ret_val = _pc_cstreamio_lseek(pwriterefile, which_pointer, M64SET32(offset_hi,offset_lo),
                                origin, &roffset_ddw);
    *poffset_hi = M64HIGHDW(roffset_ddw);
    *poffset_lo = M64LOWDW(roffset_ddw);
    release_drive_mount(pwriterefile->pobj->pdrive->driveno);/* Release lock, unmount if aborted */
    return(ret_val);
}


BOOLEAN _pc_cstreamio_lseek(PC_FILE *pwriterefile,  int which_pointer, ddword offset_ddw, int origin, ddword *poffset_ddw)
{
BOOLEAN ret_val;
dword ltemp_hi, ltemp_lo;
ddword
       circular_window_start_ddw,
       stream_pointer_ddw,
       phys_offset_ddw,
       new_offset_ddw;
PC_FILE *pefile;

    if (which_pointer == CFREAD_POINTER)
    {
        pefile = pwriterefile->fc.plus.psibling;   /* Switch to the read file */
    }
    else
        pefile = pwriterefile;             /* Use the write file */
    /* Get the current stream pointer, that's the linear file pointer plus
       the stream offset at the beginning of the file */
    stream_pointer_ddw = pc_cfilio_get_fp_ddw(pefile);
    *poffset_ddw = stream_pointer_ddw;

    /* the start of the current window */
    if (M64GT(pwriterefile->fc.plus.circular_max_offset_ddw,pwriterefile->fc.plus.circular_file_size_ddw))
        circular_window_start_ddw =  M64MINUS(pwriterefile->fc.plus.circular_max_offset_ddw,pwriterefile->fc.plus.circular_file_size_ddw);
    else
        circular_window_start_ddw =  M64SET32(0,0);


    if (origin == PSEEK_SET)
    {
        new_offset_ddw = offset_ddw;
    }
    else if (origin == PSEEK_CUR)   /* offset from current file pointer */
    {
        if (M64ISZERO(offset_ddw))
            return(TRUE);
        new_offset_ddw = M64PLUS(offset_ddw,stream_pointer_ddw);
        /* If new pointer exceeds end of data set to end of data */
        if (M64GT(new_offset_ddw,pwriterefile->fc.plus.circular_max_offset_ddw))
            new_offset_ddw = pwriterefile->fc.plus.circular_max_offset_ddw;
    }
    else if (origin == PSEEK_CUR_NEG)  /* negative offset from current file pointer */
    {
        if (M64GT(offset_ddw, stream_pointer_ddw))
            new_offset_ddw = circular_window_start_ddw;
        else
        {
            new_offset_ddw = M64MINUS(stream_pointer_ddw,offset_ddw);
            /* Truncate at beginning of data */
            if (M64GT(circular_window_start_ddw,new_offset_ddw))
                new_offset_ddw = circular_window_start_ddw;
        }
    }
    else if (origin == PSEEK_END)   /*  offset from end of file */
    {
        if (M64GT(offset_ddw, pwriterefile->fc.plus.circular_max_offset_ddw))
            new_offset_ddw = circular_window_start_ddw;
        else
        {
            new_offset_ddw = M64MINUS(pwriterefile->fc.plus.circular_max_offset_ddw,offset_ddw);
           /* Truncate at beginning of data */
            if (M64GT(circular_window_start_ddw,new_offset_ddw))
                new_offset_ddw = circular_window_start_ddw;
        }
    }
    else
    {
        new_offset_ddw = M64SET32(0,0);
        rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
        return(FALSE);
    }

    /* Make sure new offset is in the window */
    if ( M64GT(new_offset_ddw, pwriterefile->fc.plus.circular_max_offset_ddw) )
        new_offset_ddw = pwriterefile->fc.plus.circular_max_offset_ddw;
    if (M64GT(circular_window_start_ddw,new_offset_ddw))
        new_offset_ddw = circular_window_start_ddw;

    if (M64LT(new_offset_ddw,pwriterefile->fc.plus.circular_file_base_ddw))
    { /* before the wrap point, back off the base by one window */
        pefile->fc.plus.circular_file_base_ddw =
            M64MINUS(pwriterefile->fc.plus.circular_file_base_ddw ,pwriterefile->fc.plus.circular_file_size_ddw);
    }
    else
    {
        /* Set the window start. Reader and writer are now synched up */
        pefile->fc.plus.circular_file_base_ddw = pwriterefile->fc.plus.circular_file_base_ddw;
    }
    phys_offset_ddw = M64MINUS(new_offset_ddw,pefile->fc.plus.circular_file_base_ddw);

    ltemp_hi  = M64HIGHDW(phys_offset_ddw);
    ltemp_lo  = M64LOWDW(phys_offset_ddw);
    ret_val =  _pc_efilio_lseek(pefile, ltemp_hi, ltemp_lo, PSEEK_SET, &ltemp_hi, &ltemp_lo);
    if (ret_val)
    {
        /* The underlying seek should not have truncated */
        if (!(M64EQ(phys_offset_ddw,M64SET32(ltemp_hi, ltemp_lo))) )
        {
            rtfs_set_errno(PEINTERNAL, __FILE__, __LINE__);
            ret_val = FALSE;
        }
        else
        {
            *poffset_ddw = new_offset_ddw;
        }
    }
    return(ret_val);
}

#endif /* (INCLUDE_CIRCULAR_FILES) */
#endif /* Exclude from build if read only */
