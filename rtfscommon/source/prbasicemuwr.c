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
#define INCLUDE_BASIC_IO 1

#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */

#if (INCLUDE_BASIC_POSIX_EMULATION)
    int pc_bfilio_write(int fd, byte *buf, int count);
    BOOLEAN pc_bfilio_flush(int fd);
    BOOLEAN pc_bfilio_chsize(int fd, dword offset);
#endif


int po_write(int fd, byte *buf, int count)                   /*__apifn__*/
{
#if (INCLUDE_BASIC_POSIX_EMULATION)
    return(pc_bfilio_write(fd, buf, count));
#else
dword nwritten, count_dw;;
    count_dw = (dword) count;
    if (pc_efilio_write(fd, buf, count_dw, &nwritten))
    {
        return((int)nwritten);
    }
    else
        return(-1);
#endif
}


BOOLEAN po_flush(int fd)                                           /*__apifn__*/
{
#if (INCLUDE_BASIC_POSIX_EMULATION)
    return(pc_bfilio_flush(fd));
#else
    return(pc_efilio_flush(fd));
#endif
}


BOOLEAN po_truncate(int fd, dword offset)
{
#if (INCLUDE_BASIC_POSIX_EMULATION)
    return(pc_bfilio_chsize(fd, offset));
#else
    return(pc_efilio_chsize(fd, 0, offset));
#endif
}


int po_chsize(int fd, dword offset)                               /*__apifn__*/
{
#if (INCLUDE_BASIC_POSIX_EMULATION)
    if (!pc_bfilio_chsize(fd, offset))
        return(-1);
    else
        return(0);
#else
    if (!pc_efilio_chsize(fd, 0, offset))
        return(-1);
    else
        return(0);
#endif
}

#endif /* Exclude from build if read only */
