/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* RTEXFATBAM.C - Bit allocation map (BAM) functions for exFat.

    Routines exported from this file include:

    pc_free_all_bam_buffers 	- Called from mount code to reset the fat buffer cache
    bam_size_sectors 			- Called from mount code to get the size of the bam
    exfatop_check_freespace		- Called from mount code to scan the bam for freespace
    exfatop_remove_free_region	- Called from fatop_link_frag and pcexfat_grow_directory
    exfatop_add_free_region		- Called from fatop_link_frag when it frees a fragment
    exfatop_find_contiguous_free_clusters - Called from fatop_find_contiguous_free_clusters and pcexfat_grow_directory.
    pc_async_flush_bam_blocks   - Called from pcexfat_flush.
*/

#include "rtfs.h"
#if (RTFS_CFG_LEAN)
#define INCLUDE_MULTI_BLOCKS_PERBUFFER 0
#else
#define INCLUDE_MULTI_BLOCKS_PERBUFFER 1
#endif


int bam_size_sectors(DDRIVE *pdr);

dword preboot_pcclnext(DDRIVE *pdr, dword cluster, int *error);
int bam_get_bits(DDRIVE *pdr, dword which_cluster, dword *ncontig);
static BOOLEAN bam_set_bits(DDRIVE *pdr, dword which_cluster, dword ncontig, int v);
static BOOLEAN pc_write_bam_blocks(DDRIVE *pdrive,dword bam_blockno, dword nblocks, byte *bam_data, byte which_copy);


static BOOLEAN chk_bam_dirty(DDRIVE *pdr);

#define FAT_PRIMARY_CACHE_EMPTY 0x0ffffffful

void _pc_link_buffer_and_freelist(FATBUFF *pblk, int user_num_fat_buffers, byte *provided_buffers, dword fat_buffer_page_size_bytes);

void pc_free_all_bam_buffers(DDRIVE *pdr)
{
FATBUFFCNTXT *pfatbuffcntxt;

    pfatbuffcntxt = &(PDRTOEXFSTR(pdr)->bambuffcntxt);
    /* Clear stats */
    pfatbuffcntxt->stat_primary_cache_hits =
    pfatbuffcntxt->stat_secondary_cache_hits =
    pfatbuffcntxt->stat_secondary_cache_loads =
    pfatbuffcntxt->stat_secondary_cache_swaps = 0;

    pfatbuffcntxt->lru_counter = 0;

    /* Clear hash tables */
    /* Primary hash table size is a compile time option in ProPlus */
    pfatbuffcntxt->fat_blk_hash_tbl = &pfatbuffcntxt->fat_blk_hash_tbl_core[0];
    pfatbuffcntxt->primary_mapped_sectors = &pfatbuffcntxt->primary_sectormap_core[0];
    pfatbuffcntxt->primary_mapped_buffers = &pfatbuffcntxt->primary_buffmap_core[0];
    pfatbuffcntxt->hash_size = FAT_HASH_SIZE;
    pfatbuffcntxt->hash_mask = (dword)(pfatbuffcntxt->hash_size-1);

    /* Initialize secondary cache (these are linked lists of FATBUFS) the number of lists
       is divided by the cache size to reduce traversales per lookup */
    rtfs_memset(pfatbuffcntxt->fat_blk_hash_tbl,0, sizeof(FATBUFF *)*pfatbuffcntxt->hash_size);
    /* Initialize primary FAT cache - This speeds up sequential cluster accesss from the same fat sector
       by eliminating scanning the secondary hashed lists.. */

    /* The last sector number accessed in a hash group is stored in pfatbuffcntxt->primary_mapped_sectors[hash_index]..
       If no sector is in the primary cache at an index pfatbuffcntxt->primary_mapped_buffers[hash_index] will be
       NULL and pfatbuffcntxt->primary_mapped_sectors[hash_index]; will contain FAT_PRIMARY_CACHE_EMPTY  */
    {
    dword *pdw;
    int i;
        pdw = (dword *) pfatbuffcntxt->primary_mapped_sectors;
        for (i = 0; i < pfatbuffcntxt->hash_size; i++, pdw++)
        {
            *pdw = FAT_PRIMARY_CACHE_EMPTY;
        }
    }
    rtfs_memset(pfatbuffcntxt->primary_mapped_buffers,(byte) 0, sizeof(FATBUFF *)*pfatbuffcntxt->hash_size);

    pfatbuffcntxt->pcommitted_buffers = 0;
#if (!INCLUDE_MULTI_BLOCKS_PERBUFFER)
    /* Make sure page size is 1 if multi sector fat buffering is not enabled */
    pdrive->du.fat_buffer_pagesize = 1;
#endif
    pfatbuffcntxt->fat_buffer_page_size_sectors = PDRTOEXFSTR(pdr)->BitMapBufferPageSizeSectors;
    pfatbuffcntxt->fat_buffer_page_mask = 0xffffffff - (pfatbuffcntxt->fat_buffer_page_size_sectors-1);
    pfatbuffcntxt->pfree_buffers = (FATBUFF *) PDRTOEXFSTR(pdr)->BitMapBufferControlCore;
	/* _Create a buffer pool for the bit allocation map, use the page size and buffer size returned by the user callback mechanism */
    _pc_link_buffer_and_freelist(
    	pfatbuffcntxt->pfree_buffers, 																/* Control elements, one per page */
    	PDRTOEXFSTR(pdr)->BitMapBufferSizeSectors/pfatbuffcntxt->fat_buffer_page_size_sectors,  	/* Number of control elements */
    	(byte *) PDRTOEXFSTR(pdr)->BitMapBufferCore,
    	pdr->drive_info.bytespsector*pfatbuffcntxt->fat_buffer_page_size_sectors					/* Page size */
    );
}

/* Called from mount code to get the size of the bam */
int bam_size_sectors(DDRIVE *pdr)
{
int fatno = 0;
int sectorsize;
    sectorsize = (PDRTOEXFSTR(pdr)->SizeOfBitMap[fatno]+ pdr->drive_info.bytespsector-1)>>PDRTOEXFSTR(pdr)->BytesPerSectorShift;
	return(sectorsize);
}

/* Called from mount code check freespace */
BOOLEAN exfatop_check_freespace(DDRIVE *pdr)
{
BOOLEAN ret_val;
dword start,cluster,ncontig;
dword freecount = 0;
int v;
dword largest_region_size = 0;
dword largest_region_start = 2;

	ret_val = TRUE;

    cluster = 2;
    start   = 0;

	do
	{	/* */
    	v = bam_get_bits(pdr, cluster, &ncontig);
		if (v == 0)
		{
            if (!start) {start = cluster; freecount = 0; }
            freecount += ncontig;
		}
		else
		{
            if (start)
            {
                if (!fatop_add_free_region(pdr, start, freecount, FALSE))
				{
                	ret_val = FALSE;
					break;
				}
					if (freecount > largest_region_size)
					{
						largest_region_size = freecount;
						largest_region_start = start;
					}
            }
            start = 0;
		}
		cluster += ncontig;
		if (!ncontig) /* Should not happen */
			break;
	} while (cluster <= pdr->drive_info.maxfindex);

    if (ret_val && start)
    {
    	if (!fatop_add_free_region(pdr, start, freecount, FALSE))
    		ret_val = FALSE;
		if (freecount > largest_region_size)
		{
			largest_region_size = freecount;
			largest_region_start = start;
		}
    }
/* If EXFAT_FAVOR_CONTIGUOUS_FILES is enabled remember the largest contiguous region of free space so we can allocate files in contiguous files as much as possible */
#if (EXFAT_FAVOR_CONTIGUOUS_FILES)
	if (ret_val)
	{
		pdr->drive_info.free_contig_pointer = largest_region_start;  /* set free_contig_pointer	if EXFAT_FAVOR_CONTIGUOUS_FILES */
	}
#endif
    return(ret_val);
}

BOOLEAN exfatop_remove_free_region(DDRIVE *pdr, dword cluster, dword ncontig)
{
dword last_cluster;

    last_cluster = cluster+ncontig-1;
    if (cluster < 2 || last_cluster > pdr->drive_info.maxfindex)
    {
        ERTFS_ASSERT(rtfs_debug_zero())
        return(TRUE);
    }
#if (INCLUDE_RTFS_FREEMANAGER)
    free_manager_claim_clusters(pdr, cluster, ncontig);
#endif
    ERTFS_ASSERT(pdr->drive_info.known_free_clusters >= ncontig)
    pdr->drive_info.known_free_clusters = (pdr->drive_info.known_free_clusters - ncontig);
	return(bam_set_bits(pdr, cluster, ncontig, 1));
}
BOOLEAN exfatop_add_free_region(DDRIVE *pdr, dword cluster, dword ncontig, BOOLEAN do_erase)
{
    pdr->drive_info.known_free_clusters = pdr->drive_info.known_free_clusters + ncontig;
    ERTFS_ASSERT(pdr->drive_info.known_free_clusters < pdr->drive_info.maxfindex)
    /* July 2012 - Removed redundant call to setbits */
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

	return (bam_set_bits(pdr, cluster, ncontig, 0));

}



dword exfatop_find_contiguous_free_clusters(DDRIVE *pdr, dword startpt, dword endpt, dword min_clusters, dword max_clusters, dword *p_contig, int *is_error)
{
dword currentcluster,ncontig;
dword freecount = 0;
dword firstfreecluster = 0;
int v;

	*p_contig = 0;
	*is_error = 0;
	if (startpt < 2)
		startpt = 2;
#if (INCLUDE_RTFS_FREEMANAGER)
    if (!CHECK_FREEMG_CLOSED(pdr))
	{
	    /* Search ram based free manager for clusters to allocate.. if the ram based manager is shut down
		   it returns and we fall through and use the bam */
		firstfreecluster = free_manager_find_contiguous_free_clusters(pdr, startpt, endpt, min_clusters, max_clusters, p_contig, is_error);
		if (firstfreecluster)
			return firstfreecluster;
		/* July 2012 - Don't fall through if the free manager is open */
		if (!CHECK_FREEMG_CLOSED(pdr))
			return 0;
	}
#endif
	currentcluster = startpt;

	while (currentcluster <= endpt)
	{
		ncontig = 0;
   		v = bam_get_bits(pdr, currentcluster, &ncontig);
       	if (!ncontig)	/* Shouldn't happen but don't get stuck in a loop */
			break;
   		if (v == 0)		/* Found ncontig free clusters starting at currentcluster */
   		{
        	if (firstfreecluster && (firstfreecluster + freecount) == currentcluster)
        		freecount += ncontig;
        	else
        	{
        		firstfreecluster = currentcluster;
        		freecount = ncontig;
        	}
        	if (freecount > min_clusters)
        	{
				if (freecount > max_clusters)					 /* Truncate if more than requested */
					freecount = max_clusters;
				if (firstfreecluster + freecount -1 > endpt)	 /* Truncate if outside the range */
				{
					if (firstfreecluster > endpt)
						firstfreecluster = 0;
					else
						freecount = endpt - firstfreecluster + 1;
				}
				break;
        	}
   		}
   		else			/* Found ncontig used clusters starting at currentcluster */
   		{
        	firstfreecluster = 0;
        	freecount = 0;
   		}
       	currentcluster += ncontig;
	}
	*p_contig = freecount;
	return(firstfreecluster);

}

int pc_async_flush_bam_blocks(DDRIVE *pdrive,dword max_flushes_per_pass)
{
FATBUFF *pblk;
dword num_flushed;
FATBUFFCNTXT *pfatbuffcntxt;

    max_flushes_per_pass = 0;
    if (!chk_bam_dirty(pdrive))
        return(PC_ASYNC_COMPLETE);
    pfatbuffcntxt = &(PDRTOEXFSTR(pdrive)->bambuffcntxt);
    ERTFS_ASSERT(pfatbuffcntxt->num_dirty > 0)

    pblk = pfatbuffcntxt->pcommitted_buffers;
    ERTFS_ASSERT(pblk)
    num_flushed = 0;
    while (pblk)
    {
        if (pblk->fat_data_buff.dirty_count)
        {
            if (max_flushes_per_pass && (num_flushed >= max_flushes_per_pass))
                   return(PC_ASYNC_CONTINUE);
            if (!pc_write_bam_block_buffer_page(pdrive,pblk))
                return(PC_ASYNC_ERROR);
            pc_zero_mbuff_dirty(&pblk->fat_data_buff);
            pfatbuffcntxt->num_dirty -= 1;
            num_flushed += 1;
        }
        pblk = pblk->pnext;
    }
    if (!pblk)
    {
        clear_bam_dirty(pdrive);
        return(PC_ASYNC_COMPLETE);
    }
    else if (pfatbuffcntxt->num_dirty)
        return(PC_ASYNC_CONTINUE);
    else
	{
        return(PC_ASYNC_COMPLETE);
	}
}


#define EXFATBADCLUSTER 0xfffffff7

static dword bam_sector_to_sector(DDRIVE *pdr, dword sector_offset, dword *sector_count)
{
dword cluster, clustersize, sector;
int fatno = 0;


	clustersize = (dword) pdr->drive_info.secpalloc;
	clustersize &= 0xff;

    cluster = PDRTOEXFSTR(pdr)->FirstClusterOfBitMap[fatno];
    sector = pc_cl2sector(pdr, cluster);

   	sector += sector_offset;
   	*sector_count = (dword) (bam_size_sectors(pdr)-sector_offset);
   	return(sector);

}


static int bam_sector_clip(DDRIVE *pdr, FATBUFFCNTXT *pfatbuffcntxt, dword bam_sector_offset)
{
dword secpbam;
int clipped_sectorcount;

    clipped_sectorcount = pfatbuffcntxt->fat_buffer_page_size_sectors;
    secpbam = (dword) bam_size_sectors(pdr);
    if ( (bam_sector_offset + clipped_sectorcount) > secpbam)
    {
        clipped_sectorcount = secpbam - bam_sector_offset;
    }
    return(clipped_sectorcount);
}

static BOOLEAN bam_devio_read(DDRIVE *pdr, dword sector_offset, dword sector_count, byte *pdata)
{
BOOLEAN ret_val;
dword sectorno,nsectors,max_sector_count;

	sectorno = bam_sector_to_sector(pdr, sector_offset, &max_sector_count);
	if (!sectorno)
		return(FALSE);

//    UPDATE_RUNTIME_STATS(pdr, fat_reads, 1)
//    UPDATE_RUNTIME_STATS(pdr, fat_blocks_read, nsectors)
	nsectors = sector_count;
	if (sector_count > max_sector_count)
		nsectors = max_sector_count;
	else
		nsectors = sector_count;
#if (INCLUDE_FAILSAFE_RUNTIME)
	if (prtfs_cfg->pfailsafe)
		return(prtfs_cfg->pfailsafe->block_devio_xfer(pdr,sectorno, pdata, nsectors, TRUE));
#endif
    ret_val = raw_devio_xfer(pdr, sectorno, pdata, nsectors, FALSE, TRUE);
    return(ret_val);
}


static BOOLEAN bam_devio_write(DDRIVE *pdr, dword sector_offset, dword sector_count, byte *pdata, int whichcopy)
{
BOOLEAN ret_val;
dword sectorno,nsectors,max_sector_count;

	if (whichcopy)			/* HERE - Not doing copy 2 */
		return(TRUE);
	sectorno = bam_sector_to_sector(pdr, sector_offset, &max_sector_count);


	nsectors = sector_count;
	if (sector_count > max_sector_count)
		nsectors = max_sector_count;
	else
		nsectors = sector_count;
#if (INCLUDE_FAILSAFE_RUNTIME)
	if (prtfs_cfg->pfailsafe)
		return(prtfs_cfg->pfailsafe->block_devio_xfer(pdr,sectorno, pdata, nsectors, FALSE));
#endif
    ret_val = raw_devio_xfer(pdr, sectorno, pdata, nsectors, FALSE, FALSE);
    return(ret_val);
}

static byte *pc_map_bam_sector(DDRIVE *pdr, dword sector_offset_in_bam, int n_dirty, dword usage_flags)
{
FATBUFF *pblk;
dword new_sector_offset_in_page;
dword page_offset_boundary;
FATBUFFCNTXT *pfatbuffcntxt;

    pfatbuffcntxt = &(PDRTOEXFSTR(pdr)->bambuffcntxt);

    /* Using paging method to reduce the count of individual read and writes */
    /* Convert sector number to the sector number at the page boundary,
       remember the offset to sector that we want in the page */
    page_offset_boundary = sector_offset_in_bam & pfatbuffcntxt->fat_buffer_page_mask;
    new_sector_offset_in_page = sector_offset_in_bam - page_offset_boundary;
    sector_offset_in_bam = page_offset_boundary;

    /* Check the cache for the sector */
    /* Check the primary */
    pblk = pc_find_fat_blk_primary(pfatbuffcntxt, page_offset_boundary);

	if (!pblk)
    {   /* Check the secondary */
        pblk = pc_find_fat_blk_secondary(pfatbuffcntxt, page_offset_boundary);
    }
    if (!pblk)
    {
        pblk = pc_realloc_fat_blk(pdr, pfatbuffcntxt, page_offset_boundary);
        if (!pblk)
        {
            /* No sectors available. */
            rtfs_set_errno(PERESOURCEFATBLOCK, __FILE__, __LINE__);
            return(0);
        }
        else
        {
            if (usage_flags & FATPAGEREAD)
            {  /* The sector we are accessing may be offset inside the buffer, because it is
                  not on the page boundary, so move the data pointer to return */
                  int  clipped_sector_count;
                  clipped_sector_count = bam_sector_clip(pdr, pfatbuffcntxt, page_offset_boundary);
                  if (!clipped_sector_count)       /* This will not happen */
                    return(0);
                  if (!bam_devio_read(pdr, page_offset_boundary, clipped_sector_count, pblk->fat_data_buff.pdata))
                  {   /* The read failed. Put on the the free list */
                    rtfs_set_errno(PEIOERRORREADFAT, __FILE__, __LINE__);
                    pc_free_fat_blk(pfatbuffcntxt, pblk);
                    return(0);
                  }
                  pfatbuffcntxt->stat_secondary_cache_loads += 1;
            }
            pc_set_fat_blk_primary(pfatbuffcntxt, pblk);
            pc_set_fat_blk_secondary(pfatbuffcntxt, pblk);
            pc_commit_fat_blk_queue(pfatbuffcntxt, pblk);
        }
    }
    if (usage_flags & FATPAGEWRITE)
    { /* If just switching to dirty, update the count of dirty fat mbuffs */
        if (!(pblk->fat_data_buff.dirty_count))
        {
            pfatbuffcntxt->num_dirty += 1;
            /* Set drive status to "must flush fat" */
            set_bam_dirty(pdr);
        }
        /* Mark the sector offest dirty in the buffer */
#if (INCLUDE_NAND_DRIVER)   /* Call pc_set_mbuff_all_dirty() if any FAT sector is dirty in an erase blocks */
#if (0)
        if (pdr->pmedia_info->eraseblock_size_sectors)
        { /* In this mode we want to write all sectors when we flush the page. So set all dirty */
            pc_set_mbuff_all_dirty(&pblk->fat_data_buff, pfatbuffcntxt->fat_buffer_page_size_sectors);
        }
        else
#endif
#endif
        { /* In this mode we want to write only dirty sectors when we flush the page. So set specific sectors dirty */
            pc_set_mbuff_dirty(&pblk->fat_data_buff, new_sector_offset_in_page, n_dirty);
        }
    }
    pblk->lru_counter = pfatbuffcntxt->lru_counter++;
    /* If we get here we have a sector for sure and it is in the secondary
       cache. Put it in the primary cache. Harmless if already there */
    pc_set_fat_blk_primary(pfatbuffcntxt, pblk);

   /* The sector we are accessing may be offset inside the buffer, because it is
      not on the page boundary, so move the data pointer to return */
   {
         byte *pfatdata;
         pfatdata = pblk->fat_data_buff.pdata;
         pfatdata += (new_sector_offset_in_page<<pdr->drive_info.log2_bytespsec);
         return(pfatdata);
   }
}

/* Set up to count bits starting from start to v */
static byte set_bit_val_n(byte c, int start, int count, int v)
{
int i;
byte b;

	b = 0;
	for (i = 0; i < count; i++)
	{
		b <<= 1;
		b |= 1;
	}
	b <<= start;


	if (v)
		c |= b;
	else
		c &= ~b;
	return(c);
}


static BOOLEAN bam_set_bits(DDRIVE *pdr, dword which_cluster, dword ncontig, int v)
{
byte *p;
dword usage_flags;
dword bytesinsector, byteoffset,bitoffset,byteoffset_in_bam,sectoroffset_in_bam,prevsectoroffset_in_bam;
dword bufferbitoffset;

	if (which_cluster < 2)
	{
        rtfs_set_errno(PEINVALIDCLUSTER, __FILE__, __LINE__); /* flush_bam device write error */
		return(FALSE);
	}
	which_cluster -= 2;
	usage_flags = FATPAGEWRITE|FATPAGEREAD;

	prevsectoroffset_in_bam = 0xffffffff; /* No match */
	p = 0;
	while (ncontig)
	{
		bufferbitoffset = which_cluster;
		bitoffset  = bufferbitoffset&0x7;
		byteoffset_in_bam = bufferbitoffset>>3;
		sectoroffset_in_bam = byteoffset_in_bam >> PDRTOEXFSTR(pdr)->BytesPerSectorShift;
		bytesinsector = (dword) pdr->drive_info.bytespsector;

		/* Map in the page containing the sector we are looping so don't do it if still processing the same sector */
		if (!p || prevsectoroffset_in_bam != sectoroffset_in_bam)
		{
		int n_dirty;
			n_dirty = 1; /* Only doing one sector at a time in this loop so n_dirty is one */
			p = pc_map_bam_sector(pdr, sectoroffset_in_bam, n_dirty, usage_flags);
			if (!p)
				return(FALSE);
			prevsectoroffset_in_bam = sectoroffset_in_bam;
		}
		byteoffset = byteoffset_in_bam % bytesinsector;

		if (bitoffset == 0 && ncontig >= 8)
		{
			byte set;
			if (v) set = 0xff; else set = 0x0;
			while (ncontig >= 8 && byteoffset < bytesinsector)
			{
				p[byteoffset] = set;
				byteoffset++;
				ncontig -= 8;
				which_cluster += 8;
			}
		}
		else
		{
			dword bitstoset = 8 - bitoffset;
			if (bitstoset > ncontig)
				bitstoset = ncontig;
			p[byteoffset] = set_bit_val_n(p[byteoffset], bitoffset, bitstoset,v);
			ncontig -= bitstoset;
			which_cluster += bitstoset;
		}
	}
	return(TRUE);
}


/* Return v and the number in the string of that value */
static int get_bit_val_n(byte c, int start, int *count)
{
int i,v;
byte b;

	b = 1;
	b <<= start;
	if (c & b)
		v = 1;
	else
		v = 0;
	*count = 1;
	b <<= 1;
	start++;
	if (v)
	{
		for (i = start; i < 8; i++, b <<= 1)
			if (c & b)
				*count += 1;
			else
				break;
	}
	else
	{
		for (i = start; i < 8; i++, b <<= 1)
			if ((c & b) == 0)
				*count += 1;
			else
				break;
	}
	return(v);

}

int bam_get_bits(DDRIVE *pdr, dword which_cluster, dword *ncontig)
{
byte *p;
int n_dirty,v,contig_count;
dword usage_flags;
dword bytesinsector, byteoffset,bitoffset,byteoffset_in_bam,sectoroffset_in_bam;
dword bufferbitoffset;

	if (which_cluster < 2)
		return(0);
	which_cluster -= 2;

	*ncontig = 0;

	n_dirty = 0; // ????
	usage_flags = FATPAGEREAD;


	bufferbitoffset = which_cluster;
	bitoffset  = bufferbitoffset&0x7;
	byteoffset_in_bam = bufferbitoffset>>3;
	sectoroffset_in_bam = byteoffset_in_bam >> PDRTOEXFSTR(pdr)->BytesPerSectorShift;
	bytesinsector = (dword) pdr->drive_info.bytespsector;

	/* Map in the page containing the sector */
	p = pc_map_bam_sector(pdr, sectoroffset_in_bam, n_dirty, usage_flags);
	if (!p)				/* should not happen */
		return(0);
	byteoffset = byteoffset_in_bam % bytesinsector;

	contig_count = 0;
	v = get_bit_val_n(p[byteoffset], bitoffset, &contig_count);
	byteoffset++;
	*ncontig += contig_count;
	if ((bitoffset + contig_count) == 8)
	{
		byte match;
		if (v) match = 0xff; else match = 0x0;
		while (byteoffset < bytesinsector)
		{
			if (p[byteoffset] == match)
				*ncontig += 8;
			else
				break;
			byteoffset++;
		}
		if (byteoffset < bytesinsector)
		{
			int this_contig_count = 0;
			int thisv;
			thisv = get_bit_val_n(p[byteoffset], 0, &this_contig_count);
			if (thisv==v)
				*ncontig += this_contig_count;
		}
	}
	/* truncate if we scanned past eof  */
	which_cluster += 2; /* Back into cluster domain */
	if (which_cluster + *ncontig > pdr->drive_info.maxfindex)
		*ncontig = (pdr->drive_info.maxfindex - which_cluster + 1);
	return(v);
}
/* July 2012 updated pc_write_bam_block_buffer_page to be analogous to pc_write_fat_block_buffer_page
   see exFAT bug fix in rtfatdrvrd.c that now flushes the BAM properly */
#if (INCLUDE_MULTI_BLOCKS_PERBUFFER)
int pc_get_clear_mbuff_dirty(PCMBUFF *pcmb, int *pfirst_dirty);
#endif
/* Write a bam buffer from a fat buffer pool to disk */
BOOLEAN pc_write_bam_block_buffer_page(DDRIVE *pdrive, FATBUFF *pblk)
{
#if (INCLUDE_MULTI_BLOCKS_PERBUFFER)
byte *pclipped_data;
dword bam_blockno;
int n_dirty_blocks,dirty_block_offset;

    /* Check for dirty blocks in the buffer, get count and clear status */
    n_dirty_blocks = pc_get_clear_mbuff_dirty(&pblk->fat_data_buff, &dirty_block_offset);

    while (n_dirty_blocks)
    {
        bam_blockno = pblk->fat_data_buff.first_blockno;
        pclipped_data = pblk->fat_data_buff.pdata;
        if (dirty_block_offset)
        {
            bam_blockno += dirty_block_offset;
            pclipped_data += (dirty_block_offset<<pdrive->drive_info.log2_bytespsec);
        }
        if (!pc_write_bam_blocks(pdrive, bam_blockno, n_dirty_blocks, pclipped_data,0x3))
            return(FALSE);
        n_dirty_blocks = pc_get_clear_mbuff_dirty(&pblk->fat_data_buff, &dirty_block_offset);
    }
#else
	if (pblk->fat_data_buff.dirty_count)
	{
       if (!pc_write_bam_blocks(pdrive, pblk->fat_data_buff.first_blockno, 1, pblk->fat_data_buff.pdata,0x3))
            return(FALSE);
        pc_zero_mbuff_dirty(&pblk->fat_data_buff);
    }
#endif
    return(TRUE);
}
/* Write blocks to the BAM, blockno is offset from start of volume */
static BOOLEAN pc_write_bam_blocks(DDRIVE *pdrive,dword bam_blockno, dword nblocks, byte *bam_data, byte which_copy)
{
    if (which_copy & 0x1)
    {
        if (!bam_devio_write(pdrive,bam_blockno, nblocks, bam_data,0))
        {
            /* set errno to PEDEVICE unless devio set errno */
/* io_error: */
            if (!get_errno())
                rtfs_set_errno(PEIOERRORWRITEFAT, __FILE__, __LINE__); /* flush_bam device write error */
            return(FALSE);
        }

    }
#if (0)
	/* not processing second copy. Standard formats use only one anyway */
    if (which_copy & 0x2 && pdrive->drive_info.numfats >= 2)
    {   /* Write the second copy */
      if (!bam_devio_write(pdrive,bam_blockno, nblocks, bam_data,1))
          goto io_error;
    }
#endif
    return(TRUE);
}


void set_bam_dirty(DDRIVE *pdr)
{
    pdr->drive_info.drive_operating_flags |= DRVOP_BAM_IS_DIRTY;
}
void clear_bam_dirty(DDRIVE *pdr)
{
    pdr->drive_info.drive_operating_flags &= ~DRVOP_BAM_IS_DIRTY;
}
static BOOLEAN chk_bam_dirty(DDRIVE *pdr)
{
    if (pdr->drive_info.drive_operating_flags & DRVOP_BAM_IS_DIRTY)
        return(TRUE);
    else
        return(FALSE);
}
