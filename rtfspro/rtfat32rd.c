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

#if (INCLUDE_FAT32)

BOOLEAN pc_init_drv_fat_info32(DDRIVE *pdr, struct pcblk0 *pbl0)
{
    pdr->drive_info.secpfat = (dword) pbl0->secpfat;   /* sectors / fat */
    pdr->drive_info.numfats = pbl0->numfats; /* Number of fat copies */
    if (pdr->drive_info.secpfat == 0L)
        pdr->drive_info.secpfat = pbl0->secpfat2;

    if (pbl0->flags & NOFATMIRROR)
    {
        pdr->drive_info.fatblock = (dword) pbl0->secreserved +
                        ((pbl0->flags & ACTIVEFAT) * pdr->drive_info.secpfat);
        pdr->drive_info.numfats = 1;
    }
    else
        pdr->drive_info.fatblock = (dword) pbl0->secreserved;
        /* The first block of the root is just past the fat copies   */
     pdr->drive_info.firstclblock = pdr->drive_info.fatblock + pdr->drive_info.secpfat * pdr->drive_info.numfats;
/* DM: 7-6-99: BUG FIX: */
     pdr->drive_info.rootblock = (pbl0->rootbegin-2) * pdr->drive_info.secpalloc + pdr->drive_info.firstclblock;
/* WRONG: pdr->rootblock = pbl0->rootbegin-2 + pdr->firstclblock; */

    /*  Calculate the largest index in the file allocation table.
        Total # block in the cluster area)/Blockpercluster == s total
        Number of clusters. Entries 0 & 1 are reserved so the highest
        valid fat index is 1 + total # clusters.
    */
    pdr->drive_info.maxfindex = (dword) (1 + ((pdr->drive_info.numsecs - pdr->drive_info.firstclblock)/pdr->drive_info.secpalloc));
    {
        /* Make sure the calculated index doesn't overflow the fat sectors */
        dword max_index;
        /* For FAT32 Each block of the fat holds 128 entries so the maximum index is
           (pdr->secpfat * pdr->drive_info.clpfblock32 (128 for block size 512) )-1; */
        max_index = (dword) pdr->drive_info.secpfat;
        max_index *= pdr->drive_info.clpfblock32;
        max_index -= 1;
        if (pdr->drive_info.maxfindex > max_index)
            pdr->drive_info.maxfindex = max_index;
    }
    /* Create a hint for where we should write file data. We do this
        because directories are allocated in one cluster chunks while
        file may allocate larger chunks. We Try to put directory
        data at the beginning of the disk in a seperate region so we
        do not break the contiguous space further out */
    pdr->drive_info.known_free_clusters = pbl0->free_alloc;
    ERTFS_ASSERT(pdr->drive_info.known_free_clusters == 0xffffffff || pdr->drive_info.known_free_clusters < pdr->drive_info.maxfindex)

    pdr->drive_info.free_contig_base = pbl0->next_alloc;
   /* 2-10-2007 - Added defensive code to check for a valid start hint. If it is out of range set it to
      the first cluster in the FAT */
    if (pdr->drive_info.free_contig_base < 2 || pdr->drive_info.free_contig_base >= pdr->drive_info.maxfindex)
        pdr->drive_info.free_contig_base = 2;

    pdr->drive_info.free_contig_pointer = pdr->drive_info.free_contig_base;
    pdr->drive_info.infosec = pbl0->infosec;
    pdr->drive_info.fasize = 8;
    return(TRUE);
}


BOOLEAN pc_gblk0_32(DDRIVE *pdr, struct pcblk0 *pbl0, byte *b)                 /*__fn__*/
{
    if (pbl0->numroot == 0)
    {
        pbl0->secpfat2    = to_DWORD(b+0x24);
        pbl0->flags       = to_WORD(b+0x28);
        pbl0->fs_version  = to_WORD(b+0x2a);
        pbl0->rootbegin   = to_DWORD(b+0x2c);
        pbl0->infosec     = to_WORD(b+0x30);
        pbl0->backup      = to_WORD(b+0x32);
        copybuff( &pbl0->vollabel[0],b+0x47,11); /* Volume label FAT32 */

        /* Read one block */
        if (!raw_devio_xfer(pdr,(dword)(pbl0->infosec) ,b,1, FALSE, TRUE))
        {
            return(FALSE);
        }
        /* 3-07-02 - Remove scan to find INFOSIG. Access at offset 484. */
        b += 484;
		if (FSINFOSIG == to_DWORD((byte  *)&((struct fat32_info *)b)->fs_sig))
		{
        	pbl0->free_alloc = to_DWORD((byte  *)&((struct fat32_info *)b)->free_alloc);
        	pbl0->next_alloc = to_DWORD((byte  *)&((struct fat32_info *)b)->next_alloc);
		}
		else
		{  /* If the signature is not found default to the beginning of the FAT */

        	pbl0->free_alloc = 0xffffffff;
        	pbl0->next_alloc = 2;

		}
    }
    return(TRUE);
}

BOOLEAN pc_validate_partition_type(byte p_type)
{
    if ( (p_type == 0x01) || (p_type == 0x04) || (p_type == 0x06) ||
         (p_type == 0x0E) ||   /* Windows FAT16 Partition */
         (p_type == 0x0B) ||   /* FAT32 Partition */
         (p_type == 0x0C) ||   /* FAT32 Partition */
         (p_type == 0x07) ||   /* exFat Partition */
         (p_type == 0x55) )    /* FAT32 Partition */
         return(TRUE);
    else
         return(FALSE);
}

#endif /* INCLUDE_FAT32 */
