/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* RTFATDRV.C - Low level FAT management functions, shared by Pro and Pro Plus.
*/

#include "rtfs.h"


static BOOLEAN fatop_buff_check_freespace(DDRIVE *pdr);
dword _fatop_get_frag(DDRIVE *pdr, byte *palt_buffer, dword alt_buffer_size, dword startpt, dword *pnext_cluster, dword n_clusters, int *end_of_chain, BOOLEAN is_async);
static dword fatop_buff_get_frag(DDRIVE *pdr, dword start_cluster, dword *pnext_cluster, dword n_clusters, int *end_of_chain);

#if (RTFS_CFG_LEAN)
#define INCLUDE_FATPAGE_DRIVER 0
#else
#define INCLUDE_FATPAGE_DRIVER 1
#endif
#if (INCLUDE_FATPAGE_DRIVER) /* Declare friend functions to rtfatdrv.c provided by ProPlus */
static dword fatop_page_get_frag(DDRIVE *pdr, byte *palt_buffer, dword alt_buffer_size, dword startpt, dword *pnext_cluster, dword n_clusters, int *end_of_chain, BOOLEAN is_async);
static BOOLEAN fatop_page_check_freespace(DDRIVE *pdr);
#endif

#if (INCLUDE_FAT32)
static BOOLEAN fatxx_pfgdword(DDRIVE *pdr, dword index, dword *value);
#endif



/******************************************************************************
    fatop_check_freespace - Load fat free space at mount time
 Description
        Scan the FAT and determine the number of free clusters.
 Returns
    FALSE if an io error occurred during fat swapping, else TRUE
*****************************************************************************/

static BOOLEAN fatop_buff_check_freespace(DDRIVE *pdr);
BOOLEAN fatop_check_freespace(DDRIVE *pdr)
{
     pdr->drive_info.known_free_clusters = 0;
#if (INCLUDE_FATPAGE_DRIVER)
    if (pdr->drive_info.fasize != 3)
        return(fatop_page_check_freespace(pdr));
#endif
    return(fatop_buff_check_freespace(pdr));
}

static BOOLEAN fatop_buff_check_freespace(DDRIVE *pdr)
{
    dword i;
    dword start,nxt;
    dword freecount = 0;

    start = 0;
    for (i = 2 ; i <= pdr->drive_info.maxfindex; i++)
    {
        if (!fatop_get_cluster(pdr, i, &nxt)) /* Any (ifree) */
            return(FALSE);
        if (nxt == 0)
        {
            if (!start) {start = i; freecount = 0; }
            freecount++;
        }
        else
        {
            if (start)
            {
                if (!fatop_add_free_region(pdr, start, freecount, FALSE))
                    return(FALSE);
                start = 0;
            }
        }
    }
    if (start)
       if (!fatop_add_free_region(pdr, start, freecount, FALSE))
           return(FALSE);
    return(TRUE);
}


/* Called when the driver open count goes to zero. NO-OP Under rtfspro.. */
void fatop_close_driver(DDRIVE *pdr)
{
#if (INCLUDE_RTFS_FREEMANAGER)  /* fatop_close_driver() release free regions */
    free_manager_close(pdr);   /* Claim semaphoire before releaseing */
#else
    RTFS_ARGSUSED_PVOID((void *) pdr); /* Not used by Pro */
#endif
}

/* Retrieve a value from the fat   */
BOOLEAN fatop_get_cluster(DDRIVE *pdr, dword clno, dword *pvalue)             /*__fatfn__*/
{
    dword  nibble,index, offset, result;
    byte    c;
    union align1 {
        byte    wrdbuf[4];          /* Temp storage area */
        word  fill[2];
    } u;
    union align2 {
    byte    wrdbuf2[4];         /* Temp storage area */
    word  fill[2];
    } u2;
    if ((clno < 2) || (clno > pdr->drive_info.maxfindex) )
    {
        rtfs_set_errno(PEINVALIDCLUSTER, __FILE__, __LINE__);   /* fatop_get_cluster: bad cluster value internal error */
        return (FALSE);
    }
    result = 0;
    if (pdr->drive_info.fasize == 3)       /* 3 nibble ? */
    {
        nibble = (word)(clno * 3);
        index  = (word)(nibble >> 2);
        offset = (word)(clno & 0x03);
        /* Read the first word   */
        if (!fatxx_fword( pdr, index, (word *) &u.wrdbuf[0], FALSE ))
            return(FALSE);
/*
        |   W0      |   W1      |   W2  |
        A1 A0 B0 A2 B2 B1 C1 C0 D0 C2 D2 D1
        xx xx   xx
*/
        if (offset == 0) /* (A2 << 8) | A1 A2 */
        {
            /* BYTE 0 == s Low byte, byte 1 low nibble == s high byte   */
            u.wrdbuf[1] &= 0x0f;
            result = u.wrdbuf[1];
            result <<= 8;
            result |= u.wrdbuf[0];
        }
/*
        |   W0      |   W1      |   W2  |
        A1 A0 B0 A2 B2 B1 C1 C0 D0 C2 D2 D1
                xx  xx xx
*/

        else if (offset == 1) /* (B1 B2 << 4) | B0 */
        {
            /* BYTE 2 == High byte Byte 1 high nibb == low nib   */
            if (!fatxx_fword( pdr, (word)(index+1), (word *) &u2.wrdbuf2[0], FALSE ))
                return(FALSE);
            c = (byte) (u.wrdbuf[1] >> 4);
            result = u2.wrdbuf2[0];
            result <<= 4;
            result |= c;
        }
/*
        |   W0      |   W1      |   W2  |
        A1 A0 B0 A2 B2 B1 C1 C0 D0 C2 D2 D1
                            xx xx   xx
*/
        else if (offset == 2) /*(C2 << 8) | C1 C2 */
        {
            if (!fatxx_fword( pdr, (word)(index+1), (word *) &u2.wrdbuf2[0], FALSE ))
                return(FALSE);
            /* BYTE 1 == s Low byte, byte 2 low nibble == s high byte   */
            result = (word) (u2.wrdbuf2[0] & 0x0f);
            result <<= 8;
            result |= u.wrdbuf[1];
        }
/*
        |   W0      |   W1      |   W2  |
        A1 A0 B0 A2 B2 B1 C1 C0 D0 C2 D2 D1
                                xx  xx xx
*/

        else if (offset == 3) /* (D2 D1) << 4 | D0 */
        {
            result = u.wrdbuf[1];
            result <<= 4;
            c = u.wrdbuf[0];
            c >>= 4;
            result |= c;
        }
    }
    else if (pdr->drive_info.fasize == 8) /* 32 BIT fat. ret the value at 4 * clno */
    {
#if (INCLUDE_FAT32)
#if KS_LITTLE_ENDIAN
            if (!fatxx_pfgdword( pdr, clno, (dword *) &result ))
                return (FALSE);
#else
            if ( fatxx_pfgdword( pdr, clno, (dword *) &u.wrdbuf[0] ))
                result = (dword) to_DWORD(&u.wrdbuf[0]);
            else
                return (FALSE);
#endif
#else
        return (FALSE);
#endif
    }
    else    /* 16 BIT fat. ret the value at 2 * clno */
    {
            if ( fatxx_fword( pdr, clno, (word *) &u.wrdbuf[0], FALSE ))
                result = (dword) to_WORD(&u.wrdbuf[0]); /*X*/ /* And use the product as index */
            else
                return (FALSE);
    }
#if (INCLUDE_EXFAT) /* FAT64 does not use cluster chains */
   if (!ISEXFAT(pdr) )
#endif
   {
		result &= 0x0fffffff;
   }
    *pvalue = result;
    return (TRUE);
}


dword fatop_next_cluster(DDRIVE *pdr, dword cluster)
{
dword nxt;
    if (!fatop_get_cluster(pdr, cluster, &nxt))
       return(0);
#if (INCLUDE_EXFAT) /* FAT64 does not use cluster chains */
   if ( ISEXFAT(pdr) )
   {
   		if (nxt >=  0xfffffff7)
        	nxt = FAT_EOF_RVAL;                            /* end of chain */
   }
   else
#endif
   {
     if ( (pdr->drive_info.fasize == 3 && nxt >=  0xff8) ||
        (pdr->drive_info.fasize == 4 && nxt >=  0xfff8) ||
        (pdr->drive_info.fasize == 8 && nxt >=  0x0ffffff8))
        nxt = FAT_EOF_RVAL;                            /* end of chain */
   }

    if (nxt != FAT_EOF_RVAL && (nxt < 2 || nxt > pdr->drive_info.maxfindex) )
    {
        rtfs_set_errno(PEINVALIDCLUSTER, __FILE__, __LINE__);
        return (0);
    }
    return(nxt);
}




BOOLEAN fatop_add_free_region(DDRIVE *pdr, dword cluster, dword ncontig, BOOLEAN do_erase)
{
    if (cluster > pdr->drive_info.maxfindex || (cluster+ncontig-1) > pdr->drive_info.maxfindex)
    {
        ERTFS_ASSERT(rtfs_debug_zero())
        return(TRUE);
    }
#if (INCLUDE_RTFS_FREEMANAGER)
    free_manager_release_clusters(pdr, cluster, ncontig, do_erase);
#else
#if (INCLUDE_NAND_DRIVER)  /* calling eraseop_erase_blocks() in dynamic mode */
    /* If the free manager is included it erased blocks if necessary
       If not we have to do it here */
    if (do_erase)
        eraseop_erase_blocks(pdr, cluster, ncontig);
#else
    RTFS_ARGSUSED_INT((int)do_erase);
#endif
#endif
    pdr->drive_info.known_free_clusters = pdr->drive_info.known_free_clusters + ncontig;
    ERTFS_ASSERT(pdr->drive_info.known_free_clusters < pdr->drive_info.maxfindex)
#if (INCLUDE_EXFATORFAT64)
    if (!ISEXFATORFAT64(pdr) ) /* Update free_contig_pointer if not exfat */
#endif
    {
    	/* Update Pro style free pointer hints. Exfat updates this field differently */
    	if (cluster >= pdr->drive_info.free_contig_base &&  cluster <= pdr->drive_info.free_contig_pointer)
        	pdr->drive_info.free_contig_pointer = cluster;
	}
    return(TRUE);

}


/****************************************************************************
    fatop_get_frag  -  Return as many contiguous clusters as possible.
 Description
        Starting at start_cluster return the number of contiguous clusters
        allocated in the chain containing start_cluster or n_clusters,
        whichever is less.
 Returns
    Returns the number of contiguous clusters found. Or zero on an error.
    This function should always return at least one. (start_cluster). Unless
    an error occurs.
    The dword at *pnext_cluster is filled with on of the following:
        . If we went beyond a contiguous section it contains
        the first cluster in the next segment of the chain.
        . If we are still in a section it contains
        the next cluster in the current segment of the chain.
        . If we are at the end of the chain it contains the last cluster
        in the chain.
****************************************************************************/

dword _fatop_get_frag(DDRIVE *pdr, byte *palt_buffer, dword alt_buffer_size, dword startpt, dword *pnext_cluster, dword n_clusters, int *end_of_chain, BOOLEAN is_async);

dword fatop_get_frag_async(DDRIVE *pdr, byte *palt_buffer, dword alt_buffer_size, dword startpt, dword *pnext_cluster, dword n_clusters, int *end_of_chain)
{
    return(_fatop_get_frag(pdr, palt_buffer, alt_buffer_size, startpt, pnext_cluster, n_clusters, end_of_chain, TRUE));
}
dword fatop_get_frag(DDRIVE *pdr, byte *palt_buffer, dword alt_buffer_size, dword startpt, dword *pnext_cluster, dword n_clusters, int *end_of_chain)
{
    return(_fatop_get_frag(pdr, palt_buffer, alt_buffer_size, startpt, pnext_cluster, n_clusters, end_of_chain, FALSE));
}

dword _fatop_get_frag(DDRIVE *pdr, byte *palt_buffer, dword alt_buffer_size, dword startpt, dword *pnext_cluster, dword n_clusters, int *end_of_chain, BOOLEAN is_async)
{
    /* Truncate the end point to the end of the fat. This allows us to
       signify scan to end of fat by passing maxfindex as nclusters */
    if (n_clusters > pdr->drive_info.maxfindex)
        n_clusters = pdr->drive_info.maxfindex-startpt + 1;
    else if ((startpt + n_clusters-1) > pdr->drive_info.maxfindex)
        n_clusters = pdr->drive_info.maxfindex-startpt + 1;

    if ((startpt < 2) || startpt > pdr->drive_info.maxfindex)
    {
        rtfs_set_errno(PEINVALIDCLUSTER, __FILE__, __LINE__);
        return (0);
    }
#if (INCLUDE_FATPAGE_DRIVER)
    if (pdr->drive_info.fasize != 3)
        return(fatop_page_get_frag(pdr, palt_buffer, alt_buffer_size, startpt, pnext_cluster, n_clusters,end_of_chain, is_async));
#else
    RTFS_ARGSUSED_PVOID((void *) palt_buffer);
    RTFS_ARGSUSED_DWORD(alt_buffer_size);
    RTFS_ARGSUSED_INT((int)is_async);
#endif
    return(fatop_buff_get_frag(pdr, startpt, pnext_cluster, n_clusters,end_of_chain));
}

/****************************************************************************
    fatop_buff_get_frag  -  Return as many contiguous clusters as possible.
 Description
        Starting at start_cluster return the number of contiguous clusters
        allocated in the chain containing start_cluster or n_clusters,
        whichever is less.
 Returns
    Returns the number of contiguous clusters found. Or zero on an error.
    This function should always return at least one. (start_cluster). Unless
    an error occurs.
    The word at *pnext_cluster is filled with on of the following:
        . If we went beyond a contiguous section it contains
        the first cluster in the next segment of the chain.
        . If we are still in a section it contains
        the next cluster in the current segment of the chain.
        . If we are at the end of the chain it contains the last cluster
        in the chain.
****************************************************************************/

static dword fatop_buff_get_frag(DDRIVE *pdr, dword start_cluster, dword *pnext_cluster, dword n_clusters, int *end_of_chain)
{
    dword clno, next_cluster;
    dword n_contig;
    dword value;

    value = 0;
    clno = start_cluster;
    n_contig = 1;
    *pnext_cluster = 0;

    /* Get each FAT entry. If its value points to the next contiguous entry
        continue. Otherwise we have reached the end of the contiguous chain.
        At which point we return the number of contig s found and by reference
        the address of the FAT entry beginning the next chain segment.
    */
    *end_of_chain = 0;
    for (;;)
    {
        next_cluster = fatop_next_cluster(pdr, clno);
        if (!next_cluster) /* fatop_next_cluster detected an error */
            return(0);
        /* check for end markers set next cluster to the last
           cluster in the chain if we are at the end */
        if (next_cluster == FAT_EOF_RVAL) /* fatop_next_cluster detected end */
        {
            *end_of_chain = 1;
            value = clno;
            break;
        }
        else if (next_cluster == ++clno)
        {
            value = next_cluster;
            if (n_contig >= n_clusters)
                break;
            n_contig++;
        }
        else /* (next_cluster != ++clno) and we know it is not an error condition */
        {
            value = next_cluster;
            break;
        }
    }
    *pnext_cluster = value;
    return (n_contig);
}

/* Put or get a WORD value into the fat at index                */
BOOLEAN fatxx_fword(DDRIVE *pdr, dword index, word *pvalue, BOOLEAN putting)         /*__fatfn__*/
{
    word *ppage;
    word offset;
    /* Make sure we have access to the page. Mark it for writing (if a put)   */
    ppage = (word *)fatxx_pfswap(pdr,index,putting);

    if (!ppage)
        return(FALSE);
    else
    {
        /* there are 256 * sectorsize entries per page   */
        offset = (word) (index & pdr->drive_info.cl16maskblock);
        if (putting)
            ppage[offset] = *pvalue;
        else
            *pvalue = ppage[offset];
    }
    return(TRUE);
}


#if (INCLUDE_FAT32)

/* Get a DWORD value from the fat at index                */
static BOOLEAN fatxx_pfgdword(DDRIVE *pdr, dword index, dword *value)          /*__fatfn__*/
{
    dword  *ppage;
    dword offset;
    /* Make sure we have access to the page. Do not Mark it for writing   */
    ppage = (dword  *)fatxx_pfswap(pdr,index,FALSE);

    if (!ppage)
        return(FALSE);
    else
    {
        /* there are 128 entries per page   */
        offset = (dword) (index & pdr->drive_info.cl32maskblock);
        *value = ppage[(int)offset];
    }
    return(TRUE);
}
#endif

/***************************************************************************
    PC_FATSW - Map in a page of the FAT
****************************************************************************/

/* Swap in the page containing index                           */
/* Note: The caller locks the fat before calling this routine  */
byte * fatxx_pfswap(DDRIVE *pdr, dword index, BOOLEAN for_write)         /*__fatfn__*/
{
    dword  sector_offset_in_fat;
    dword flags;

    if (pdr->drive_info.fasize == 8)
        sector_offset_in_fat = (dword)(index >> (pdr->drive_info.log2_bytespsec-2)); /* Divide by (bytes per sector/4) */
    else
        sector_offset_in_fat = (word) (index >> (pdr->drive_info.log2_bytespsec-1));/* Divide by (bytes per sector/2) */

    if (sector_offset_in_fat >= pdr->drive_info.secpfat) /* Check range */
        return (0);


    if (for_write)
        flags = FATPAGEWRITE|FATPAGEREAD;
    else
        flags = FATPAGEREAD;
    /* The 4th argument is the number of sectors in the buffer to set dirty on a write */
    return (pc_map_fat_sector(pdr, &pdr->fatcontext, sector_offset_in_fat, 1, flags));
}


#if (INCLUDE_FATPAGE_DRIVER) /* Page oriented FAT16 or FAT 32 routines */

/* Declare friend functions to rtfatdrv.c provided by ProPlus */



void fatop_get_page_masks(DDRIVE *pdr, dword *mask_offset, dword *mask_cl_page,  dword *cl_per_sector, dword *cl_to_page_shift, dword * bytes_per_entry)
{
    if (pdr->drive_info.fasize == 8)
    {
        *mask_offset = pdr->drive_info.cl32maskblock;
        *mask_cl_page = ~(*mask_offset);
        *cl_per_sector = pdr->drive_info.clpfblock32;
        *cl_to_page_shift = pdr->drive_info.log2_bytespsec - 2;
        *bytes_per_entry = 4;
    }
    else
    {
        *mask_offset = pdr->drive_info.cl16maskblock;
        *mask_cl_page = ~(*mask_offset);
        *cl_per_sector = pdr->drive_info.clpfblock16;
        *cl_to_page_shift = pdr->drive_info.log2_bytespsec - 1;
        *bytes_per_entry = 2;
    }
}

/* Called by: fatop_get_frag() */
static dword fatop_page_get_frag(DDRIVE *pdr, byte *palt_buffer, dword alt_buffer_size, dword startpt, dword *pnext_cluster, dword n_clusters, int *end_of_chain, BOOLEAN is_async)
{
dword mask_offset, mask_cl_page, cl_per_sector, cl_to_page_shift,bytes_per_entry;
dword i,n_clusters_left;
dword mapped_sector_count,cluster_offset_in_page, byte_offset_in_page, first_sector_offset_to_map;
byte *mapped_data,*pb;
dword current_cluster, num_this_page,n_contig;
dword term32;
#if (INCLUDE_EXFAT)	/* FAT64 does not use cluster chains */
   if (ISEXFAT(pdr))
   		term32 = (dword)0xfffffff8;
   else
#endif
		term32 = (dword)0xffffff8;

   /* Get masks based on cluster size */
    fatop_get_page_masks(pdr, &mask_offset, &mask_cl_page,  &cl_per_sector, &cl_to_page_shift, &bytes_per_entry);
    /* Get starting cluster, offsets, blocks etc */
    cluster_offset_in_page = startpt&mask_offset;
    byte_offset_in_page = cluster_offset_in_page*bytes_per_entry;
    first_sector_offset_to_map  = startpt>>cl_to_page_shift;
    current_cluster = startpt;
    n_contig = 1;
    n_clusters_left = n_clusters;

    while (n_clusters_left)
    {
        /* get a pointer to first_sector_offset_to_map and the number of blocks in the buffer
           Only atteempts to map up to n_clusters_left*bytes_per_entry.
           will return the optional buffer passed in in palt_buffer
           if the clusters are aligned for efficient streaming and the clusters are not already
           cached. Otherwise uses the fat buffer pool */
        mapped_data = pc_map_fat_stream(pdr, &mapped_sector_count, first_sector_offset_to_map, byte_offset_in_page,
                               n_clusters_left*bytes_per_entry, palt_buffer, alt_buffer_size, TRUE);
        if (!mapped_data)   /* Error accessing the fat buffer (io error during read ?) errno was set below.. */
            goto map_error;
        if (mapped_data == palt_buffer)
        {    /* pc_map_fat_stream() returned palt_buffer, meaning that the next "mapped_sector_count" blocks
                are unmapped and we should read them. If mapped_data != palt_buffer then we know that
                "mapped_sector_count" blocks at mapped data are cached in the fat buffer pool */
            if (pc_read_fat_blocks (pdr, mapped_data, pdr->drive_info.fatblock+first_sector_offset_to_map, mapped_sector_count) < 0)
                goto map_error;
        }

        first_sector_offset_to_map += mapped_sector_count;   /* the next block we will want */
        pb = mapped_data;   /* set up pb for pointer access */
        /* test as many as we can starting from cluster_offset_in_page (always zero after first iteration) */
        num_this_page = mapped_sector_count<<cl_to_page_shift;
        if (cluster_offset_in_page)
        {
            num_this_page -= cluster_offset_in_page;
            pb += byte_offset_in_page;   /* set up pb for pointer access */
            byte_offset_in_page = 0;        /* only first iteration may not be on a boundary */
            cluster_offset_in_page  = 0;
        }
        if (num_this_page > n_clusters_left)
          num_this_page = n_clusters_left;
        /* If it is async process at most one page.. */
        if (is_async)
            n_clusters = num_this_page;

        if (pdr->drive_info.fasize == 8)
        {
        dword nxt_dw;
        dword *pdw = (dword *) pb;
            for (i = 0; i < num_this_page; i++)
            {
                /* July 2012.. this is a bug under exFat
				nxt_dw = to_DWORD((byte *)pdw) & 0x0fffffff;
				*/

				nxt_dw = to_DWORD((byte *)pdw) & (term32|0xf);
                current_cluster += 1;
                if (nxt_dw != current_cluster)
                {
                    current_cluster -= 1;
done_32:
                    if (nxt_dw >= term32)
                    {
                        *end_of_chain = 1;
                        *pnext_cluster = current_cluster;
                    }
                    else if (nxt_dw < 2 || nxt_dw > pdr->drive_info.maxfindex)
                        goto link_error; /* Handles 0xffffff6 and 0xffffff7 (reserved and bad */
                    else
                    {
                        *end_of_chain = 0;
                        *pnext_cluster = nxt_dw;
                    }
                    return(n_contig);
                }
                if (n_contig >= n_clusters)
                    goto done_32;
                n_contig++;
                pdw += 1;
            }
        }
        else /* if (pdr->fasize == 4) */
        {
        word *pw = (word *) pb;
        word clno_w, nxt_w;
            clno_w = (word) (current_cluster&0xffff);
            for (i = 0; i < num_this_page; i++)
            {
                nxt_w = to_WORD((byte *)pw);
                clno_w += 1;
                if (nxt_w != clno_w)
                {
                    clno_w -= 1;
done_16:
                    if (nxt_w >= 0xfff8)
                    {
                        *end_of_chain = 1;
                        *pnext_cluster = (dword) clno_w;
                    }
                    else if (nxt_w < 2 || nxt_w > pdr->drive_info.maxfindex)
                        goto link_error; /* Handles 0xfff6 and 0xfff7 (reserved and bad */
                    else
                    {
                        *end_of_chain = 0;
                        *pnext_cluster = (dword) nxt_w;
                    }
                    return(n_contig);
                }
                if (n_contig >= n_clusters)
                    goto done_16;
                n_contig++;
                pw += 1;
            }
            current_cluster = (dword) clno_w;
        }
        n_clusters_left -= num_this_page;
    }
link_error:
    rtfs_set_errno(PEINVALIDCLUSTER, __FILE__, __LINE__);
    return (0);
map_error:
    return (0);
}

void fatop_page_start_check_freespace(DDRIVE *pdr)
{
#if (INCLUDE_RTFS_PROPLUS) /* Proplus set up async check_freespace(DDRIVE *pdr) */
    pdr->drive_state.drv_async_current_fatblock = pdr->drive_info.fatblock;
    pdr->drive_state.drv_async_current_cluster  = 2;
    pdr->drive_state.drv_async_region_start     = 0;
    pdr->drive_state.drv_async_region_length    = 0;
#else
    RTFS_ARGSUSED_PVOID((void *) pdr); /* Not used by Pro */
#endif
}

/******************************************************************************
    fatop_page_check_freespace - Load fat free space at mount time
 Description
        Scan the FAT and determine the number of free clusters.
 Returns
    FALSE if an io error occurred during fat swapping, else TRUE
*****************************************************************************/

static BOOLEAN fatop_page_check_freespace(DDRIVE *pdr)
{
int status;
    fatop_page_start_check_freespace(pdr);
    do {
        status =  fatop_page_continue_check_freespace(pdr);
    } while (status == PC_ASYNC_CONTINUE);
    if (status != PC_ASYNC_COMPLETE)
        return(FALSE);
    else
        return(TRUE);
}

byte *pc_get_raw_fat_buffer(DDRIVE *pdrive, dword *pbuffer_size_sectors);

int fatop_page_continue_check_freespace(DDRIVE *pdr)
{
    dword max_sector, num_sectors, entries_per_buffer, next_page, buffer_size_sectors, user_buffer_size_sectors,
           cluster, regionlength, region_start,current_fatsector;

    int ret_val;
    byte *pubuff, *pbuffer;
    BOOLEAN finished;
    dword *pdw;
    word *pw;

    ret_val = PC_ASYNC_ERROR;
    user_buffer_size_sectors  = 0;
#if (INCLUDE_RTFS_PROPLUS) /* ProPlus - re-enter and continue asynchronous check_freespace */
    cluster             = pdr->drive_state.drv_async_current_cluster;
    regionlength        = pdr->drive_state.drv_async_region_length;
    region_start        = pdr->drive_state.drv_async_region_start;
    current_fatsector    = pdr->drive_state.drv_async_current_fatblock;
#else  /* Process synchronously from start to finish */
    cluster             =  2;
    regionlength        =  0;
    region_start        =  0;
    current_fatsector    =  pdr->drive_info.fatblock;
#endif

    /* Get pbuffer and user_buffer_size_sectors
       If the user provided fat buffer space the fat buffer pool is not in use so we can use the whole fat buffer as
       a transfer buffer get the user buffer, if it is smaller than the FAT buffer, release it and use the fat buffer

       If the driver provided fat buffers there is no continuous buffer pool space so we must use the user buffer.
       Note: The driver is required to provide a user buffer. if it support dynam
    */
    /* Get user buffer and user_buffer_size_sectors */
    pubuff = pc_claim_user_buffer(pdr, &user_buffer_size_sectors, 0); /* released on cleanup */
    if (!pubuff)
        user_buffer_size_sectors = 0;
    {
    byte * pfat_buffer;
    dword fat_buffer_size_sectors;
        /* Get fat buffer and fat _buffer_size_sectors */
        pfat_buffer = pc_get_raw_fat_buffer(pdr, &fat_buffer_size_sectors);
        if (user_buffer_size_sectors > fat_buffer_size_sectors)
        {
            pbuffer = pubuff;   /* user buffer larger than fat buffer, buffer_size_sectors was set in pc_claim_user_buffer() */
            buffer_size_sectors = user_buffer_size_sectors;
        }
        else
        { /* fat buffer larger than user buffer */
            pbuffer = pfat_buffer;
            buffer_size_sectors = fat_buffer_size_sectors;
        }
    }

    /* Get number of blocks and number of clusters */
    if (pdr->drive_info.fasize == 8)
    {
        entries_per_buffer = buffer_size_sectors << (pdr->drive_info.log2_bytespsec-2); /* Multiplies the number of sectors in a page times the number of dwords in a sector */
        max_sector  = pdr->drive_info.fatblock + ((pdr->drive_info.maxfindex+pdr->drive_info.clpfblock32-1) >> (pdr->drive_info.log2_bytespsec-2));
    }
    else
    {
        entries_per_buffer = buffer_size_sectors << (pdr->drive_info.log2_bytespsec-1); /* Multiplies the number of sectors in a page times the number of words in a sector */
        max_sector  = pdr->drive_info.fatblock + ((pdr->drive_info.maxfindex+pdr->drive_info.clpfblock16-1) >> (pdr->drive_info.log2_bytespsec-1));
    }
    num_sectors = buffer_size_sectors;
    finished = FALSE;
    while (!finished)   /* This loops until finished for RTFSPro, ProPlus returns an async statrus */
    {
        pdw = (dword *) pbuffer;  pw =  (word *)  pbuffer;

        next_page = cluster + entries_per_buffer;
        if (cluster == 2) {next_page -= 2; pw += 2; pdw += 2;};/*  offset cluster 2 in page appropriately */

        if (next_page > pdr->drive_info.maxfindex)
            next_page = pdr->drive_info.maxfindex+1;
        /* April 2012 Fixed bug: If the number of sectors in the fat are 1 sector larger than
           a multiple of buffer_size_sectors, the last sector in the FAT is ignored.
          if (current_fatsector + buffer_size_sectors >= max_sector)
        * {
        *    finished = TRUE;
        *    num_sectors = max_sector - current_fatsector;
        * }
        */
        if (current_fatsector + buffer_size_sectors > max_sector)
        {
            finished = TRUE;
            num_sectors = max_sector - current_fatsector + 1;
        }

        /* Call pc_read_fat_blocks() */
        if (pc_read_fat_blocks (pdr, pbuffer, current_fatsector, num_sectors) < 0)
        {
           rtfs_set_errno(PEIOERRORREADFAT, __FILE__, __LINE__);
           ret_val = PC_ASYNC_ERROR;
           goto cleanup;
        }
        else
        {
            if (pdr->drive_info.fasize == 8)
            {
                for (;cluster < next_page; cluster++, pdw++)
                {
                    if (*pdw==0)
                    {
                        if (!regionlength)  region_start = cluster;
                        regionlength++;
                    }
                    else if (regionlength)
                    {
                        if (!fatop_add_free_region(pdr,  region_start, regionlength, FALSE))
                        {
                           ret_val = PC_ASYNC_ERROR;
                           goto cleanup;
                        }
                        region_start = 0;
                        regionlength = 0;
                    }
                }
            }
            else /* if (pdr->fasize == 4) */
            {
                for (;cluster < next_page; cluster++, pw++)
                {
                    if (*pw==0)
                    {
                        if (!regionlength)  region_start = cluster;
                        regionlength++;
                    }
                    else if (regionlength)
                    {
                        if (!fatop_add_free_region(pdr,  region_start, regionlength, FALSE))
                        {

                           ret_val = PC_ASYNC_ERROR;
                           goto cleanup;
                        }
                        region_start = 0;
                        regionlength = 0;
                    }
                }
            }
        }

        if (finished)
        {
            if (regionlength)
            {
                /* Add to free cache if enabled */
                if (!fatop_add_free_region(pdr, region_start, regionlength, FALSE))
                {
                    ret_val = PC_ASYNC_ERROR;
                    goto cleanup;
                }
            }
            ret_val = PC_ASYNC_COMPLETE;
            break;
            /* Fall through to cleanup; */
        }
        else
        {
            current_fatsector += num_sectors;
#if (INCLUDE_RTFS_PROPLUS) /* ProPlus - breakout, but first prepare to reenter asynchronous check_freespace */
            pdr->drive_state.drv_async_current_cluster   = cluster;
            pdr->drive_state.drv_async_region_length     = regionlength;
            pdr->drive_state.drv_async_current_fatblock  = current_fatsector;
            pdr->drive_state.drv_async_region_start      = region_start;
            ret_val = PC_ASYNC_CONTINUE;
            break;
#endif
        }
    }
cleanup:
    if (pubuff)
        pc_release_user_buffer(pdr, pubuff);
    return(ret_val);
}

#endif /*  (INCLUDE_FATPAGE_DRIVER) */
