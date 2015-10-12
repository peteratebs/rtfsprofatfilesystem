/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* PRBASICEMU.C - Emulate RTFS Basic API Routines in RtfsProPlus */

#include "rtfs.h"


#if (INCLUDE_BASIC_POSIX_EMULATION)
int pc_bfilio_fstat(int fd, ERTFS_STAT *pstat);
int pc_bfilio_close(int fd);
BOOLEAN pc_bfilio_ulseek(int fd, dword offset, dword *pnew_offset, int origin);
int pc_bfilio_read(int fd,    byte *in_buff, int count);
int pc_bfilio_open_cs(byte *name, word flag, word mode, int use_charset);
int pc_bfilio_fstat(int fd, ERTFS_STAT *pstat);
#endif


#if (INCLUDE_CS_UNICODE)
int po_open_cs(byte *name, word flag, word mode, int use_charset)
#else
int po_open(byte *name, word flag, word mode)
#endif
{
#if (INCLUDE_BASIC_POSIX_EMULATION)
#if (INCLUDE_CS_UNICODE)
    return(pc_bfilio_open_cs(name, flag, mode, CS_CHARSET_ARGS));
#else
    return(pc_bfilio_open_cs(name, flag, mode, CS_CHARSET_NOT_UNICODE));
#endif
#else
EFILEOPTIONS basic_options;
    rtfs_memset(&basic_options, 0, sizeof(basic_options));
    basic_options.allocation_policy = PCE_LOAD_AS_NEEDED;
#if (INCLUDE_CS_UNICODE)
    return(pc_efilio_open_cs(name, flag, mode, &basic_options, CS_CHARSET_ARGS));
#else
    return(pc_efilio_open(name, flag, mode, &basic_options));
#endif
#endif
}

int po_read(int fd,    byte *in_buff, int count)    /*__apifn__*/
{
    if (!count)
    {
        rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
        return(-1);
    }
#if (INCLUDE_BASIC_POSIX_EMULATION)
    return (pc_bfilio_read(fd, in_buff, count));
#else
    {
    dword nread, count_dw;
    count_dw = (dword) count;
    if (pc_efilio_read(fd, in_buff, count_dw, &nread))
    {
        return((int)nread);
    }
    else
        return(-1);
    }
#endif
}

long po_lseek(int fd, long offset, int origin)       /*__apifn__*/
{
dword offset_dw, newoffset_lo;
#if (!INCLUDE_BASIC_POSIX_EMULATION)
dword newoffset_hi;
#endif

    if (origin == PSEEK_SET)  /* offset from beginning of file */
    {
        if (offset < 0)
        {
            /* Negative seek from beginning is an error */
            rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__); /* Set it to illegal, it should be cleared */
            return(-1L);
        }
    }
    else if (origin == PSEEK_CUR)   /* offset from current file pointer */
    {
        if (offset < 0)
        {
            origin = PSEEK_CUR_NEG;
            offset *= -1;
        }
    }
    else if (origin == PSEEK_END)   /*  offset from end of file */
    {
        if (offset <= 0)
        {
            offset *= -1;
        }
        else
        {
            /* Positive seek from end is an error */
            rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__); /* Set it to illegal, it should be cleared */
            return(-1L);
        }
    }
    offset_dw = (dword) offset;
#if (INCLUDE_BASIC_POSIX_EMULATION)
    if (!pc_bfilio_ulseek(fd, offset_dw, &newoffset_lo, origin))
#else
    if (!pc_efilio_lseek(fd, 0, offset_dw, origin, &newoffset_hi, &newoffset_lo))
#endif
        return(-1);
    else
        return((long) newoffset_lo);
}

#if (INCLUDE_MATH64)
ddword pc_bfilio_lseek64(int fd, ddword offset, int origin);

ddword po_lseek64(int fd, ddword offset, int origin)
{
#if (INCLUDE_BASIC_POSIX_EMULATION)
    return(pc_bfilio_lseek64(fd, offset, origin));
#else
    return pc_efilio_lseek64(fd, offset, origin);
#endif
}
#endif
BOOLEAN po_ulseek(int fd, dword offset, dword *pnew_offset, int origin)       /*__apifn__*/
{
#if (INCLUDE_BASIC_POSIX_EMULATION)
    return(pc_bfilio_ulseek(fd, offset, pnew_offset, origin));
#else
dword newoffset_hi;
    return(pc_efilio_lseek(fd, 0, offset, origin, &newoffset_hi, pnew_offset));
#endif
}


int po_close(int fd)  /*__apifn__*/
{
#if (INCLUDE_BASIC_POSIX_EMULATION)
    return(pc_bfilio_close(fd));
#else
    if (pc_efilio_close(fd))
        return(0);
    else
        return(-1);
#endif
}

int pc_fstat(int fd, ERTFS_STAT *pstat)                              /*__apifn__*/
{
#if (INCLUDE_BASIC_POSIX_EMULATION)
    return(pc_bfilio_fstat(fd, pstat));
#else
ERTFS_EFILIO_STAT estat;
    if (!pc_efilio_fstat(fd, &estat))
        return(-1);
    else
    {
        *pstat = estat.stat_struct;
        return(0);
    }
#endif
}
