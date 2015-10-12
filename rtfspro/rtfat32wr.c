/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* RTFAT32.C - FAT32 specific managment routines  */

#include "rtfs.h"
#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */

#if (INCLUDE_FAT32)

/* Update a fat32 voulme s info sector   */
BOOLEAN fat_flushinfo(DDRIVE *pdr)                                      /*__fn__*/
{
    byte  *pf;
    BLKBUFF *buf;

#if (INCLUDE_EXFATORFAT64)
   	if (ISEXFATORFAT64(pdr) )
   	{
		return(TRUE);
   	}
#endif


    if (pdr->drive_info.fasize == 8)
    {
        /* Use read_blk, to take advantage of the failsafe cache */
        buf = pc_read_blk(pdr, (dword) pdr->drive_info.infosec);
        if(!buf)
        {
            rtfs_set_errno(PEIOERRORREADINFO32, __FILE__, __LINE__); /* fat_flushinfo: failed writing info block */
            goto info_error;
        }
        /* Merge in the new values   */
        pf = buf->data;      /* Now we do not have to use the stack */
        /* 3-07-02 - Remove scan to find INFOSIG. Access at offset 484. */
        pf += 484;
		/* 10-29-08 - Update the signature field too, this self corrects corrupted media */
        fr_DWORD((byte *) (&((struct fat32_info *)pf)->fs_sig), FSINFOSIG);
        fr_DWORD((byte *) (&((struct fat32_info *)pf)->free_alloc), pdr->drive_info.known_free_clusters);
        /* 2-10-2007 - put  free_contig_base in allocation hint field. This forces cluster
           allocations to initially scan from the base of the FAT for free clusters rather
           than from the previous "most likely" location */
        fr_DWORD((byte *) (&((struct fat32_info *)pf)->next_alloc), pdr->drive_info.free_contig_base );
        /* Use write_blk, to take advantage of the failsafe cache */
        /* Only the first copy of the info sector is updated, the second copy is initialized by
           format but the freespace fileds are not updated continuously */
        if (!pc_write_blk(buf))
        {
            rtfs_set_errno(PEIOERRORWRITEINFO32, __FILE__, __LINE__); /* fat_flushinfo: failed writing info block */
            pc_discard_buf(buf);
info_error:
            return(FALSE);
        }
        pc_release_buf(buf); /* Leave it cached */
    }
    return (TRUE);
}
#endif /* INCLUDE_FAT32 */
#endif /* Exclude from build if read only */
