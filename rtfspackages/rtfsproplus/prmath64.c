/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* PRMATH64.C - Contains Internal 64 bit math routines.*/


#include "rtfs.h"

BOOLEAN return_false(void)
{  /* Stub when FAT64 not enabled */
    return(FALSE);
}

#if (INCLUDE_MATH64)

ddword pc_fragment_size_64(DDRIVE *pdr, REGION_FRAGMENT *pf)
{
    dword clusters_in_fragment;
	ddword alloced_size_bytes;
    clusters_in_fragment = pf->end_location - pf->start_location + 1;
	alloced_size_bytes=M64SET32(0,clusters_in_fragment);
	alloced_size_bytes   = M64LSHIFT(alloced_size_bytes, pdr->drive_info.log2_bytespalloc);
    return(alloced_size_bytes);
}

ddword pc_alloced_bytes_from_clusters_64(DDRIVE *pdr, dword total_alloced_clusters)
{
    ddword alloced_size_bytes;
	 alloced_size_bytes    = M64SET32(0,total_alloced_clusters);
    alloced_size_bytes = M64LSHIFT(alloced_size_bytes, pdr->drive_info.log2_bytespalloc);
    return(alloced_size_bytes);
}

ddword pc_byte2ddwclmodbytes(DDRIVE *pdr, ddword nbytes64)
{
    dword lo,hi;
    /* Round nbytes up to its cluster size by adding in clustersize-1
       and masking off the low bits */
    ddword rv = M64PLUS32(nbytes64, pdr->drive_info.byte_into_cl_mask);
    lo = M64LOWDW(rv);
    hi = M64HIGHDW(rv);
    lo &= ~(pdr->drive_info.byte_into_cl_mask);
    rv = M64SET32(hi,lo);
    return rv;
}
#endif

#if (INCLUDE_NATIVE_64_TYPE)
ddword m64_native_set32(dword a, dword b)
{
ddword _c,_a,_b;
    _a = (ddword) a;
    _b = (ddword) b;
    _c = (_a<<32) | _b;
    return(_c);
}
#else
ddword m64_lshift(ddword b, dword c) /* b << c */
{
ddword a;
    a = b;
    while (c--)
    {
        a.hi <<= 1;
        if (a.lo & 0x80000000)
            a.hi |= 1;
        a.lo <<= 1;
    }
    return(a);
}
ddword m64_rshift(ddword b, dword c) /* b >> c */
{
ddword a;
    a = b;
    while(c--)
    {
        a.lo >>= 1;
        if (a.hi & 0x1)
            a.lo |= 0x80000000;
        a.hi >>= 1;
    }
    return(a);
}
ddword m64_minus(ddword b, ddword c) /* b - c */
{
ddword a;
    if (b.lo >= c.lo)
    {
        a.hi = b.hi - c.hi;
    }
    else
    {
        a.hi = b.hi - c.hi - 1;
    }
    a.lo = b.lo - c.lo;
    return(a);
}
ddword m64_minus32(ddword b, dword c) /* b - c */
{
ddword cc;
    cc.hi = 0; cc.lo = c;
    return(m64_minus(b,cc));
}
ddword m64_plus(ddword b, ddword c)   /* b + c */
{
ddword a;
    a.hi = b.hi + c.hi;
    a.lo = b.lo + c.lo;
    if (a.lo < b.lo || a.lo < c.lo)
        a.hi += 1;
    return(a);
}
ddword m64_plus32(ddword b, dword c)  /* b + c */
{
ddword cc;
    cc.hi = 0; cc.lo = c;
    return(m64_plus(b,cc));
}
ddword m64_set32(dword hi, dword lo)
{
ddword a;
    a.hi = hi;
    a.lo = lo;
    return(a);
}
#endif
