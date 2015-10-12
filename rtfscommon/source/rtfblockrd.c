/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* RTBLOCK.C - ERTFS-PRO and ProPlus merged fat sector buffering routines

This file contains FAT buffering code common to RTFS Pro and RTFS ProPlus
This code is used by Pro for all FAT accesses and by ProPlus for most
FAT accesses. In ProPlus extended file operations use a differnt method.

This version of FAT buffering (May 2007) contains several improvements including:

    Uses the sector buffering algorithm from ProPlus, which is simplified
    without reducing performance
    Paging - Supports multi sector FAT buffering and media transfers.
    Note: The paging is not yet a runtime configuration and must be
    configured with the FAT_PAGE_SIZE_BLOCKS, compile constant in this
    file (see below).

    Note: This release includes a new structure of type PCMBUFF. This is a
    multisector buffer container with support for marking individual bits
    dirty. To test all possible combinations of dirty bits a test named
    pc_test_mbuff_dirty() was devised. This code is included in the file
    but it is conditionally excluded for the compilation


The FAT buffering scheme works as follows:

    Buffer structures - The FATBUFF structure an rtfsmbuff structure and some additional
    fields that are used to link it in the "committed" list and a cache list.
    The committed list contains FAT buffers that are mapped to sector on the media. Stored
    in ascending order.

    Caching -

    The cache lists are list of buffers who share the same hash value. This hashing method
    provides faster lookup. The hashing function simply masks off the upper bytes of the
    fat sector number.

    The secondary cache is an array of list linked lists of FAT buffers. The sector numbers of
    all buffers in the list all having the same hahsing value.

    A primary cache contains two arrays containing the last sector number accessed in a hash group
    and a pointer to the buffer. This primary hash eliminates secondary cache lookups when sequential
    cluster accesses are being performed.

    Hashing performance is a more significant factor for Rtfs Pro ProPlus and is configurable under
    Pro but uses a fixed size under ProPlus.

    Paging -

    Pages are read from the media into the PCMBUFF data structure contained in the FAT buffer using
    multi-sector reads.

    When pages are modified, they are written to the FAT writing only the individual changed sectors.
    If a continguous sectors are marked dirty they are written with multi-sector writes.
    The dirty sector management is performed by the following functions using data structures in the
    PCMBUFF structure:
        void pc_set_mbuff_dirty(PCMBUFF *pcmb, dword sector_offset, int sector_count)
        void pc_zero_mbuff_dirty(PCMBUFF *pcmb)
        int pc_get_clear_mbuff_dirty(PCMBUFF *pcmb, int *pfirst_dirty)

    Block buffering algorithm -

    The FAT driver code in prfatdiver.c, rtfatdiver.c, and proplusfatdriver.c calls pc_map_fat_sector()
    to map in a sector of the FAT and return a pointer to it.

    pc_map_fat_sector() calculates the page number that the sector resides in. It then calulates the
    hash index of the first sector in the page and checks the primary cache and if necessary,
    the secondary cache for the page. If the page is found a pointer to the data within the
    page is returned.

    If a page is not found, it is read into a free or re-allocated PCMBUFF buffer and the page is
    inserted into the secondary cache and into the comitted list in sector order. If there are no free
    fat buffers to read the data into, a fat buffer is reallocated by calling pc_realloc_fat_blk().
    This routine recycles the least recently used "non dirty" page. If all pages are dirty it writes
    and the recycles the least recently used dirty page.

    The last page accessed is always placed in the primary cache.

    If the access is for "write", pc_set_mbuff_dirty() is called to mark the individual sector dirty
    in the PCMBUFF memebr of the FAT buffer. A bit map is kept of the individual dirty sectors within
    the page. This bitmap is consulted when the page is later flushed.

    Failsafe Interaction -

    If Failsafe is enabled FAT page reads and writes are performed by the Failsafe module instead of
    by simple calls to the devio layer to access the media.

    If Failsafe is enabled the routines fat_devio_write() and fat_devio_read() are provided by
    Failsafe. These routines redirect writes to the journal file and for reads the journal is
    consulted first. If the required sector is not in the journal it is then read from the
    media.


*/

#include "rtfs.h"

#if (RTFS_CFG_LEAN)
#define INCLUDE_MULTI_BLOCKS_PERBUFFER 0
#define INCLUDE_FATSTREAM_DRIVER 0
#else
#define INCLUDE_MULTI_BLOCKS_PERBUFFER 1
#define INCLUDE_FATSTREAM_DRIVER 1
#endif

/* Configure the page size. Uncomment the 2 lines associated with the page size
   to select that page size

   Note: Page size may be 1,2,4,8,16 or 32.

   The optimal page size to select is dependant on the media and driver.
   Fat sectors are read from the media in "page size" chunks.

   If performing multi sector reads are significantly faster than multiple single
   sector reads then increasing page size should improve performance. On ATA devices
   performance of many functions doubles as the page size doubles.
*/

/* Mark primary cache empty with hi 4 bit clear and low 28 bits set.
   needed to be changed because the sector at the page boundary can be now be zero,
   so we select a non zero value that can not match a real sector number and set
   the hi 4 flag bits to clear. The previous value of zero is not usable because
   the paging algorithm comes up with sector numbers of zero */
#define FAT_PRIMARY_CACHE_EMPTY 0x0ffffffful

#define DEBUG_FAT_BUFFERING 0
#if (DEBUG_FAT_BUFFERING)
void assert_not_on_queue(DDRIVE *pdrive, FATBUFF *pblk);
void assert_on_queue(DDRIVE *pdrive, FATBUFF *pblk);
#define ASSERT_ON_QUEUE(DRIVE, BLOCK) assert_on_queue(DRIVE, BLOCK);
#define ASSERT_NOT_ON_QUEUE(DRIVE, BLOCK) assert_not_on_queue(DRIVE, BLOCK);
#else
#define ASSERT_ON_QUEUE(DRIVE, BLOCK)
#define ASSERT_NOT_ON_QUEUE(DRIVE, BLOCK)
#endif


FATBUFF *pc_find_fat_blk_primary(FATBUFFCNTXT *pfatbuffcntxt, dword fat_sector_offset); // DONE
void pc_set_fat_blk_primary(FATBUFFCNTXT *pfatbuffcntxt, FATBUFF *pblk);		// DONE
void pc_clear_fat_blk_primary(FATBUFFCNTXT *pfatbuffcntxt, FATBUFF *pblk); // DONE
FATBUFF *pc_find_fat_blk_secondary (FATBUFFCNTXT *pfatbuffcntxt, dword fat_sector_offset); // DONE
void pc_clear_fat_blk_secondary(FATBUFFCNTXT *pfatbuffcntxt, FATBUFF *pblk); // DONE
void pc_set_fat_blk_secondary (FATBUFFCNTXT *pfatbuffcntxt, FATBUFF *pblk); // DONE
void pc_uncommit_fat_blk_queue(FATBUFFCNTXT *pfatbuffcntxt, FATBUFF *pblk); // DONE
void pc_commit_fat_blk_queue(FATBUFFCNTXT *pfatbuffcntxt, FATBUFF *pblk); // DONE
void pc_free_fat_blk(FATBUFFCNTXT *pfatbuffcntxt, FATBUFF *pblk); // DONE
FATBUFF *pc_realloc_fat_blk(DDRIVE *pdrive, FATBUFFCNTXT *pfatbuffcntxt, dword fat_sector_offset);	// DONE
#if (INCLUDE_MULTI_BLOCKS_PERBUFFER)
int fat_sector_clip(DDRIVE *pdrive, FATBUFFCNTXT *pfatbuffcntxt, dword fat_sector_offset); // DONE
#endif

#if (INCLUDE_NAND_DRIVER)   /* Implement pc_set_mbuff_all_dirty() if any FAT sector is dirty in an erase blocks */
/* Sets the whole mbuff dirty. Called when Rtfs is configure to flush all sectors in a page buffer with a single write */
void pc_set_mbuff_all_dirty(PCMBUFF *pcmb, dword block_count)
{
    pcmb->block_count = (int) block_count;
    pcmb->dirty_count = pcmb->block_count;
    pcmb->dirty_block_offset = 0;
    pcmb->dirty_bitmap = 0xffffffff;
}
#endif

/* Read sector fromthe FAT, sectorno is offset from start of volume */
BOOLEAN fat_devio_read(DDRIVE *pdrive, dword sectorno,dword nsectors, byte *fat_data)
{
BOOLEAN ret_val;
#if (INCLUDE_FAILSAFE_RUNTIME)
	if (prtfs_cfg->pfailsafe)
		return(prtfs_cfg->pfailsafe->fat_devio_read(pdrive, sectorno, nsectors, fat_data));
#endif
    UPDATE_RUNTIME_STATS(pdrive, fat_reads, 1)
    UPDATE_RUNTIME_STATS(pdrive, fat_blocks_read, nsectors)
    ret_val = raw_devio_xfer(pdrive, sectorno, fat_data, nsectors, FALSE, TRUE);
    return(ret_val);
}


/* Read from the fat into a byte buffer first_sector is offset from volume start not FAT start */
int pc_read_fat_blocks (DDRIVE* pdrive, byte* buffer, dword first_sector, dword num_sectors)
{
    if (!fat_devio_read(pdrive, first_sector, num_sectors, buffer))
    {
        rtfs_set_errno(PEIOERRORREADFAT, __FILE__, __LINE__);
        return -1;
    }
    return 0;
}


/* Create the FAT buffer free list from a contiguos block of fat buffer control structures
   that are provided in a memory area pointed to by pblk, containing space for user_num_fat_buffers control structures.

   if "provided_buffers" is non zero then it points

   If provided_buffers is non zero, it points to a contiguous section of memory that is
   (fat_buffer_page_size_bytes * user_num_fat_buffers) bytes wide.
    Split the data into user_num_fat_buffers seperate buffers of fat_buffer_page_size_bytes bytes each and assign the
    addresses in the control block.

   If provided_buffers is zero, the device driver already assigned the addresses in the control block, so preserve
   the address pointers.
   */
void _pc_link_buffer_and_freelist(FATBUFF *pblk, int user_num_fat_buffers, byte *provided_buffers, dword fat_buffer_page_size_bytes)
{
int i;
byte *save_pdata;
    for (i = 0; i < (user_num_fat_buffers-1); i++, pblk++)
    {
        save_pdata = pblk->fat_data_buff.pdata;
        rtfs_memset(pblk,(byte) 0, sizeof(FATBUFF));
        if (provided_buffers)
        {
            pblk->fat_data_buff.pdata = provided_buffers;
            provided_buffers += fat_buffer_page_size_bytes;
        }
        else
            pblk->fat_data_buff.pdata = save_pdata;
        pblk->pnext = pblk+1;
    }
    save_pdata = pblk->fat_data_buff.pdata;
    rtfs_memset(pblk,(byte) 0, sizeof(FATBUFF));
    if (provided_buffers)
        pblk->fat_data_buff.pdata = provided_buffers;
    else
        pblk->fat_data_buff.pdata = save_pdata;
    pblk->pnext = 0;
}


/* Reset the fat buffer cache  */
void    pc_free_all_fat_buffers(DDRIVE *pdrive)
{
FATBUFFCNTXT *pfatbuffcntxt;

    pfatbuffcntxt = &pdrive->fatcontext;
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

    /* Initialize secondary FAT cache (these are linked lists of FATBUFS) the number of lists
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
        /* needed to be changed because the sector at the page boundary can be now be zero,
           and we can no longer use zero as an empty indicator.. */
        pdw = (dword *) pfatbuffcntxt->primary_mapped_sectors;
        for (i = 0; i < pfatbuffcntxt->hash_size; i++, pdw++)
        {
            *pdw = FAT_PRIMARY_CACHE_EMPTY;
        }
    }
    rtfs_memset(pfatbuffcntxt->primary_mapped_buffers,(byte) 0, sizeof(FATBUFF *)*pfatbuffcntxt->hash_size);

    pfatbuffcntxt->pcommitted_buffers = 0;

    /* Create a freelist from pdrive->du.user_num_fat_buffers control structures provided in
       pdrive->du.fat_buffer_structures.
       If the drive was configured by pc_diskio_configure() and not by the device driver, then
       link the data buffer space provided in pdrive->du.user_fat_buffer_data to the control structures
       If the drive was the device driver the data fields are already established */
#if (!INCLUDE_MULTI_BLOCKS_PERBUFFER)
    /* Make sure page size is 1 if multi sector fat buffering is not enabled */
    pdrive->du.fat_buffer_pagesize = 1;
#endif
    pfatbuffcntxt->fat_buffer_page_size_sectors = pdrive->du.fat_buffer_pagesize;
    pfatbuffcntxt->fat_buffer_page_mask = 0xffffffff - (pfatbuffcntxt->fat_buffer_page_size_sectors-1);

    {    /* Call _pc_link_buffer_and_freelist() with raw buffer space and page size zero because buffer
           control blocks were set up by the device driver */
        pfatbuffcntxt->pfree_buffers = (struct fatbuff *) pdrive->mount_parms.fatbuff_memory;
        _pc_link_buffer_and_freelist(pfatbuffcntxt->pfree_buffers, (int) pdrive->mount_parms.n_fat_buffers, 0, 0);

    }
}
/* This is called when disk freespace is scanned. The fat buffer is not being used so we use it
   as a large transfer buffer to maximize disk read sizes */
byte *pc_get_raw_fat_buffer(DDRIVE *pdrive, dword *pbuffer_size_sectors)
{
    *pbuffer_size_sectors = 0;
    if (pdrive->du.user_fat_buffer_data)
    {
        *pbuffer_size_sectors = pdrive->du.user_num_fat_buffers;
        return (pdrive->du.user_fat_buffer_data);
    }
    return (0);
}

/* Find the first sector between first_sector and last_sector that is currently mapped
   This is used by FAT stream access funtion to detect if any sectors in the stream are buffered
   and should be read from the fat buffer */
#if (INCLUDE_FATSTREAM_DRIVER)
static dword pc_find_first_fat_blk(DDRIVE *pdrive, dword first_sector_offset, dword last_sector_offset)
{
FATBUFF *pblk_scan;
int page_size, loop_guard;

    page_size = pdrive->fatcontext.fat_buffer_page_size_sectors;
    pblk_scan = pdrive->fatcontext.pcommitted_buffers;
    loop_guard = pdrive->du.user_num_fat_buffers;
    /* Scan up to where our last interesting sector is >= the the first sector in the buffer */

    while (pblk_scan && loop_guard--)
    {
        if (last_sector_offset < pblk_scan->fat_data_buff.first_blockno)        /* buffer before range */
            pblk_scan = pblk_scan->pnext;
        else if (pblk_scan->fat_data_buff.first_blockno > last_sector_offset)   /* buffer beyond the range */
            return(0);
        else
        {
            if (first_sector_offset <= pblk_scan->fat_data_buff.first_blockno) /* range straddles buffer start */
                return (pblk_scan->fat_data_buff.first_blockno);
            else
            {
                dword last_in_buffer;
                last_in_buffer = pblk_scan->fat_data_buff.first_blockno + page_size-1;
                if (first_sector_offset <= last_in_buffer)                      /* range intersects buffer middle */
                    return (first_sector_offset);
                else
                    pblk_scan = pblk_scan->pnext;
            }
        }
    }
    return(0);
}

/* byte *pc_map_fat_stream()

    Internal function, synchronizes user buffer FAT accesses with the FAt buffer pool
    and returns the buffer addresses and ranges for cluster access

    Determine the buffer address of a cluster.

    If the FAT buffer pool is used and it is a write operation sets appropriate dirty bits for flushing

    If an alternate buffer is supplied decides decides if the alternate user supplied buffer should be
    used or if the FAT buffer pool should be used. The decision is based on the boundaries and size of
    of the cluster range and what is already buffered.

        The FAT buffer pool is used if:
            No alternate buffer is supplied.
            The sector at first_sector_offset is already buffered
            byte_offset is non-zero (not sector aligned)
            The byte range is < 1 sector

            If the FAT buffer pool is used the returned sector count may be up to the width of a
            FAT buffer page.
        The Alternate buffer is used if:
            An alternate buffer is supplied.
            The sectors are aligned so they overlap one or more full FAT sectors and they occupy
            one or more full FAT buffer pages.
            If Alternate buffer is used the returned sector count may be up to alt_buffer_size
            , smaller if the range fits in a smaller sector range or if sectors withing the range are
            already buffered. These buffered sectors should then be retrieved by calling the function
            again after processing the user buffer

    If the alternate buffer is selected the return pointer is always alt_buffer..
    If alt_buffer is returned and the operation is a read, the caller must read.
    If alt_buffer is returned and the operation is a write, the caller is not required to first read
    the sector because the operation is guaranteed to overwrite the whole region.

    byte *pc_map_fat_stream(
        DDRIVE *pdrive,
        dword *return_sector_count,
        dword first_sector_offset,
        dword byte_offset,
        dword byte_count,
        byte *alt_buffer,
        dword alt_buffer_size,
        BOOLEAN is_a_read)

*/
byte *pc_map_fat_stream(DDRIVE *pdrive, dword *return_sector_count, dword first_sector_offset, dword byte_offset, dword byte_count, byte *alt_buffer, dword alt_buffer_size, BOOLEAN is_a_read)
{
BOOLEAN use_fat_pool;
dword last_sector_offset, n_sectors, first_mapped_sector;
dword first_page_boundary,sector_offset_in_page;
byte *return_pointer;


    /* pdrive->drive_info.bytespsector was set before this was called */
    ERTFS_ASSERT(pdrive->drive_info.bytespsector != 0)


    /* Calculate sector counts and offsets */
    n_sectors  = (byte_offset + byte_count + pdrive->drive_info.bytespsector-1)/pdrive->drive_info.bytespsector;
    last_sector_offset = first_sector_offset+n_sectors-1;
    first_page_boundary = first_sector_offset & pdrive->fatcontext.fat_buffer_page_mask;
    sector_offset_in_page = first_sector_offset - first_page_boundary;


    /* Clip the range if any sectors are mapped */
    first_mapped_sector = pc_find_first_fat_blk(pdrive, first_sector_offset, last_sector_offset);
    if (first_mapped_sector)
        n_sectors = first_mapped_sector - first_sector_offset + 1;

    /* See if we need to use the buffer pool, and if so do we have to read it */
    use_fat_pool = FALSE;
    /* If the drive is configured for page write mode and we are accessing the fat area
       to perform a write, then select use_fat_pool. For NAND this means it disables writing
       multiple contiguous sectors using the potentially larger user buffer and always ripples
       fat table writes through the FAT buffer pool, which should be configured by the driver to be
       erase block aligned */
#if (INCLUDE_NAND_DRIVER)    /* Force use of FAT buffer pool in dynamic mode with erase blocks */
    if (pdrive->pmedia_info->eraseblock_size_sectors)
    {
        use_fat_pool = TRUE;
    }
    else
#endif
    {
        if (alt_buffer == 0)        /* Use fat buffers if no buffer provided */
            use_fat_pool = TRUE;
        else if (byte_offset)      /* Use fat buffers if byte offset into a sector */
            use_fat_pool = TRUE;
        else if ((sector_offset_in_page+n_sectors) <= pdrive->fatcontext.fat_buffer_page_size_sectors)
            /* Or if the whole request fits in one buffer */
            use_fat_pool = TRUE;
    }

    if (use_fat_pool)
    {   /* Using the buffer pool, set read flags and dirty flags  */
        dword usage_flags;
        /* clip the sector count to the sectors to the end of the page
           or the requested sector count, whichever is less */
       *return_sector_count = pdrive->fatcontext.fat_buffer_page_size_sectors - sector_offset_in_page;
       if (*return_sector_count > n_sectors)
          *return_sector_count = n_sectors;
       usage_flags = 0;
       if (!is_a_read)                      /* Set write flag if requested */
           usage_flags = FATPAGEWRITE;
       /* Add in read flag if we need to */
       if (is_a_read)                              /* read because it is a read request */
           usage_flags |= FATPAGEREAD;
       else if (byte_offset || sector_offset_in_page) /* read because not overwriting the start of buffer */
           usage_flags |= FATPAGEREAD;
       /* We are starting at the first byte in the buffer, read if we are not overwriting every
          byte in the page */
       else if (byte_count < (pdrive->fatcontext.fat_buffer_page_size_sectors<<pdrive->drive_info.log2_bytespsec))
           usage_flags |= FATPAGEREAD;              /* read because not overwriting the end of buffer */
       /* The 3rd argument is the number of sectors in the buffer to set dirty if it is a write */
       return_pointer = pc_map_fat_sector(pdrive, &pdrive->fatcontext, first_sector_offset, (int) *return_sector_count, usage_flags);
    }
    else
    { /* Use the input buffer, clip the number of sectors if in range already mapped
         or if the sector count is more than the buffer size */
         /* We are bursting, but we can only do complete sectors. We know the left cluster is alligned,
            and n_sectors > 1, check the right side and reduce the sector count if we need to
            the next iteration will finish. */
    dword ltemp;
         ltemp = byte_offset + byte_count;
/*         if (ltemp & 0xfff)  :LB: - Seems like this was a bug.. should have been 0x1ff */
         if (ltemp & pdrive->drive_info.bytemasksec)
            n_sectors -= 1;
        if (first_mapped_sector)
            *return_sector_count = first_mapped_sector - first_sector_offset; /* will never be > n_sectors */
        else
            *return_sector_count = n_sectors;
        if (*return_sector_count > alt_buffer_size)
            *return_sector_count = alt_buffer_size;
        return_pointer = alt_buffer;
    }
    return(return_pointer);
}
#else
byte *pc_map_fat_stream(DDRIVE *pdrive, dword *return_sector_count, dword first_sector_offset, dword byte_offset, dword byte_count, byte *alt_buffer, dword alt_buffer_size, BOOLEAN is_a_read)
{   /* Using the buffer pool, set read flags and dirty flags  */
    dword usage_flags;
    *return_sector_count = 1;
    usage_flags |= FATPAGEREAD;              /* read because not overwriting the end of buffer */
    if (!is_a_read)
        usage_flags = FATPAGEWRITE;
    if (!return_sector_count) /* Won't execute */
    {
        RTFS_ARGSUSED_PVOID((void *) alt_buffer);
        RTFS_ARGSUSED_DWORD(byte_offset);
        RTFS_ARGSUSED_DWORD(byte_count);
        RTFS_ARGSUSED_DWORD(alt_buffer_size);
    }
    return(pc_map_fat_sector(pdrive, &pdrive->fatcontext, first_sector_offset, (int) *return_sector_count, usage_flags));
}
#endif /* INCLUDE_FATSTREAM_DRIVER */

byte *pc_map_fat_sector(DDRIVE *pdrive, FATBUFFCNTXT *pfatbuffcntxt, dword sector_offset_in_fat, int n_dirty, dword usage_flags)
{
FATBUFF *pblk;
dword new_sector_offset_in_page;
dword page_offset_boundary;

    /* Using paging method to reduce the count of individual read and writes */
    /* Convert sector number to the sector number at the page boundary,
       remember the offset to sector that we want in the page */
    page_offset_boundary = sector_offset_in_fat & pfatbuffcntxt->fat_buffer_page_mask;
    new_sector_offset_in_page = sector_offset_in_fat - page_offset_boundary;
    sector_offset_in_fat = page_offset_boundary;

    /* Check the cache for the sector */
    /* Check the primary */
    pblk = pc_find_fat_blk_primary(pfatbuffcntxt, page_offset_boundary);
    if (!pblk)
    {   /* Check the secondary */
        pblk = pc_find_fat_blk_secondary(pfatbuffcntxt, page_offset_boundary);
    }
    if (!pblk)
    {
        pblk = pc_realloc_fat_blk(pdrive, pfatbuffcntxt, page_offset_boundary);
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
#if (INCLUDE_MULTI_BLOCKS_PERBUFFER)
                  clipped_sector_count = fat_sector_clip(pdrive, pfatbuffcntxt, page_offset_boundary);
                  if (!clipped_sector_count)       /* This will not happen */
                    return(0);
#else
                  clipped_sector_count = 1;
#endif
                  if (!fat_devio_read(pdrive, pdrive->drive_info.fatblock+page_offset_boundary, clipped_sector_count, pblk->fat_data_buff.pdata))
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
            set_fat_dirty(pdrive);
        }
        /* Mark the sector offest dirty in the buffer */
#if (INCLUDE_NAND_DRIVER)   /* Call pc_set_mbuff_all_dirty() if any FAT sector is dirty in an erase blocks */
        if (pdrive->pmedia_info->eraseblock_size_sectors)
        { /* In this mode we want to write all sectors when we flush the page. So set all dirty */
            pc_set_mbuff_all_dirty(&pblk->fat_data_buff, pfatbuffcntxt->fat_buffer_page_size_sectors);
        }
        else
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
         pfatdata += (new_sector_offset_in_page<<pdrive->drive_info.log2_bytespsec);
         return(pfatdata);
   }
}

FATBUFF *pc_find_fat_blk_primary(FATBUFFCNTXT *pfatbuffcntxt, dword fat_sector_offset)
{
dword hash_index;
FATBUFF *pblk;
dword b;

    hash_index = fat_sector_offset&pfatbuffcntxt->hash_mask;
    b = *(pfatbuffcntxt->primary_mapped_sectors+hash_index);
    pblk = 0;

    if (b != FAT_PRIMARY_CACHE_EMPTY &&  b == fat_sector_offset)
    {
        pblk = *(pfatbuffcntxt->primary_mapped_buffers+hash_index);
        if (pblk)
        {
            pfatbuffcntxt->stat_primary_cache_hits += 1;
        }
        else  /* Should never happen.. something is wrong but just clear it and return miss */
            *(pfatbuffcntxt->primary_mapped_sectors+hash_index) = FAT_PRIMARY_CACHE_EMPTY;
    }
    return(pblk);
}

void pc_set_fat_blk_primary(FATBUFFCNTXT *pfatbuffcntxt, FATBUFF *pblk)
{
dword hash_index,fat_sector_offset;

    fat_sector_offset = pblk->fat_data_buff.first_blockno;
    hash_index = (fat_sector_offset&pfatbuffcntxt->hash_mask);
    *(pfatbuffcntxt->primary_mapped_sectors+hash_index) = fat_sector_offset;
    *(pfatbuffcntxt->primary_mapped_buffers+hash_index)  = pblk;
}

void pc_clear_fat_blk_primary(FATBUFFCNTXT *pfatbuffcntxt, FATBUFF *pblk)
{
dword hash_index,fat_sector_offset;
;

    fat_sector_offset = pblk->fat_data_buff.first_blockno;
    hash_index = fat_sector_offset&pfatbuffcntxt->hash_mask;
    if (*(pfatbuffcntxt->primary_mapped_sectors+hash_index) == fat_sector_offset)
    {
        *(pfatbuffcntxt->primary_mapped_sectors+hash_index) = FAT_PRIMARY_CACHE_EMPTY;
        *(pfatbuffcntxt->primary_mapped_buffers+hash_index)  = 0;
    }
}


FATBUFF *pc_find_fat_blk_secondary (FATBUFFCNTXT *pfatbuffcntxt, dword fat_sector_offset)
{
dword hash_index;
FATBUFF *pblk;

    hash_index = fat_sector_offset& pfatbuffcntxt->hash_mask;
    pblk = *(pfatbuffcntxt->fat_blk_hash_tbl+hash_index);
    while (pblk)
    {
        if (pblk->fat_data_buff.first_blockno == fat_sector_offset)
        {
            pfatbuffcntxt->stat_secondary_cache_hits += 1;
            return(pblk);
        }
        else
            pblk=pblk->pnext_hash;
    }
    return(0);
}

/* Place on the hash list in sector order */
void pc_clear_fat_blk_secondary(FATBUFFCNTXT *pfatbuffcntxt, FATBUFF *pblk)
{
    ERTFS_ASSERT(pblk->pnext_hash != pblk)
    ERTFS_ASSERT(pblk->pprev_hash != pblk)
    if (pblk->pprev_hash)
        pblk->pprev_hash->pnext_hash = pblk->pnext_hash;
    else
    {
dword hash_index;
        hash_index = pblk->fat_data_buff.first_blockno&pfatbuffcntxt->hash_mask;
        *(pfatbuffcntxt->fat_blk_hash_tbl+hash_index) = pblk->pnext_hash;
    }
    if (pblk->pnext_hash)
        pblk->pnext_hash->pprev_hash = pblk->pprev_hash;
    ERTFS_ASSERT(pblk->pnext_hash != pblk)
    ERTFS_ASSERT(pblk->pprev_hash != pblk)
    pblk->pprev_hash = 0;
    pblk->pnext_hash = 0;
}

void pc_set_fat_blk_secondary (FATBUFFCNTXT *pfatbuffcntxt, FATBUFF *pblk)
{
FATBUFF *pblk_scan;
dword hash_index;

    ERTFS_ASSERT(pblk->pnext_hash != pblk)
    ERTFS_ASSERT(pblk->pprev_hash != pblk)

    /* Put on the hash list */
    hash_index = pblk->fat_data_buff.first_blockno&pfatbuffcntxt->hash_mask;

    /* Put on the committed list */
    if (!*(pfatbuffcntxt->fat_blk_hash_tbl+hash_index))
    {
        *(pfatbuffcntxt->fat_blk_hash_tbl+hash_index) = pblk;
        pblk->pprev_hash = 0;
        pblk->pnext_hash = 0;
    }
    else
    {
        pblk_scan = *(pfatbuffcntxt->fat_blk_hash_tbl+hash_index);
        while (pblk_scan)
        {
            ERTFS_ASSERT(pblk_scan->pnext_hash != pblk_scan)
            ERTFS_ASSERT(pblk_scan->pprev_hash != pblk_scan)
            if (pblk->fat_data_buff.first_blockno < pblk_scan->fat_data_buff.first_blockno)
            {/* insert before scan */
                pblk->pnext_hash = pblk_scan;
                pblk->pprev_hash = pblk_scan->pprev_hash;
                if (pblk_scan->pprev_hash)
                    pblk_scan->pprev_hash->pnext_hash = pblk;
                else
                    pfatbuffcntxt->fat_blk_hash_tbl[hash_index] = pblk;
                pblk_scan->pprev_hash = pblk;
                break;
            }
            else if (pblk_scan->pnext_hash == 0)
            {/* insert after scan */
                pblk->pprev_hash = pblk_scan;
                pblk->pnext_hash = 0;
                pblk_scan->pnext_hash = pblk;
                break;
            }
            else
                pblk_scan = pblk_scan->pnext_hash;
        }
    }
    ERTFS_ASSERT(pblk->pnext_hash != pblk)
    ERTFS_ASSERT(pblk->pprev_hash != pblk)

}

void pc_uncommit_fat_blk_queue(FATBUFFCNTXT *pfatbuffcntxt, FATBUFF *pblk)
{
    ASSERT_ON_QUEUE(pdrive, pblk)

    if (pblk->pprev)
        pblk->pprev->pnext = pblk->pnext;
    else
    {
        pfatbuffcntxt->pcommitted_buffers = pblk->pnext;
    }
    if (pblk->pnext)
        pblk->pnext->pprev = pblk->pprev;
    pblk->pprev = 0;
    pblk->pnext = 0;
    ASSERT_NOT_ON_QUEUE(pdrive, pblk)
}

/* Place on the fat list in sector order */
void pc_commit_fat_blk_queue(FATBUFFCNTXT *pfatbuffcntxt, FATBUFF *pblk)
{
FATBUFF *pblk_scan;
    ERTFS_ASSERT(pblk->pnext != pblk)
    ERTFS_ASSERT(pblk->pprev != pblk)

    ASSERT_NOT_ON_QUEUE(pfatbuffcntxt, pblk)

    /* Put on the committed list */
    if (!pfatbuffcntxt->pcommitted_buffers)
    {
        pfatbuffcntxt->pcommitted_buffers = pblk;
        pblk->pprev = 0;
        pblk->pnext = 0;
    }
    else
    {
        pblk_scan = pfatbuffcntxt->pcommitted_buffers;
        while (pblk_scan)
        {
            ERTFS_ASSERT(pblk_scan->pnext != pblk_scan)
            ERTFS_ASSERT(pblk_scan->pprev != pblk_scan)
            if (pblk->fat_data_buff.first_blockno < pblk_scan->fat_data_buff.first_blockno)
            {/* insert before scan */
                pblk->pnext = pblk_scan;
                pblk->pprev = pblk_scan->pprev;

                if (pblk_scan->pprev)
                    pblk_scan->pprev->pnext = pblk;
                else
                    pfatbuffcntxt->pcommitted_buffers = pblk;
                pblk_scan->pprev = pblk;
                ASSERT_ON_QUEUE(pfatbuffcntxt, pblk)

                break;
            }
            else if (pblk_scan->pnext == 0)
            {/* insert after scan */
                pblk->pprev = pblk_scan;
                pblk->pnext = 0;
                pblk_scan->pnext = pblk;
                ASSERT_ON_QUEUE(pfatbuffcntxt, pblk)
                break;
            }
            else
                pblk_scan = pblk_scan->pnext;
        }
    }
    ASSERT_ON_QUEUE(pfatbuffcntxt->pcommitted_buffers, pblk)

    ERTFS_ASSERT(pblk->pnext != pblk)
    ERTFS_ASSERT(pblk->pprev != pblk)
}
void pc_free_fat_blk(FATBUFFCNTXT *pfatbuffcntxt, FATBUFF *pblk)
{
    if (pfatbuffcntxt->pfree_buffers)
        pblk->pnext = pfatbuffcntxt->pfree_buffers;
    else
        pblk->pnext = 0;
    pfatbuffcntxt->pfree_buffers = pblk;
}

FATBUFF *pc_realloc_fat_blk(DDRIVE *pdrive, FATBUFFCNTXT *pfatbuffcntxt, dword fat_sector_offset)
{
FATBUFF *pblk;
FATBUFF *pblk_lru;


    pblk = pfatbuffcntxt->pfree_buffers;
    if (pblk)
    {
        pfatbuffcntxt->pfree_buffers = pblk->pnext;
    }
    else
    {
FATBUFF *pblk_scan;
        pblk_lru = pblk_scan = pfatbuffcntxt->pcommitted_buffers;
        while (pblk_scan)
        {
            ERTFS_ASSERT(pblk_scan->pnext != pblk_scan)
            ERTFS_ASSERT(pblk_scan->pprev != pblk_scan)
            if (pblk_lru->lru_counter > pblk_scan->lru_counter)
                pblk_lru = pblk_scan;
            if (pblk_scan->fat_data_buff.dirty_count == 0)
            {
                if (!pblk || pblk->lru_counter > pblk_scan->lru_counter)
                    pblk = pblk_scan;
            }
            pblk_scan = pblk_scan->pnext;
        }
        if (!pblk)
        { /* None available swap the oldest */
            if (pblk_lru)
            {
			int r;
#if (INCLUDE_EXFATORFAT64)
				/* July 2012 bug fix. call pc_write_bam_block_buffer_page to update the BAM, was previously overwrting the FAT */
				if (ISEXFATORFAT64(pdrive)&&pfatbuffcntxt==&pdrive->exfatextension.bambuffcntxt)
				{
					r=pc_write_bam_block_buffer_page(pdrive,pblk_lru);
				}
				else
#endif
					r=pc_write_fat_block_buffer_page(pdrive,pblk_lru);
                if (r)
                {
                    pfatbuffcntxt->stat_secondary_cache_swaps += 1;
                    UPDATE_RUNTIME_STATS(pdrive, fat_buffer_swaps, 1)
                    pfatbuffcntxt->num_dirty -= 1;

#if (INCLUDE_EXFATORFAT64)
					/* July 2012 bug fix. update bam flush requirements if we are operating on a bit map */
					if (ISEXFATORFAT64(pdrive)&&pfatbuffcntxt==&pdrive->exfatextension.bambuffcntxt)
					{
						if (pfatbuffcntxt->num_dirty == 0)
                    		clear_bam_dirty(pdrive);
					}
					else
#endif
					{
						/* 04-06-2010 - Clear DRVOP_FAT_IS_DIRTY if dirty count goes to zero,
						was triggering an assert that num_dirty == 0 but the dirty bit it still set */
						if (pfatbuffcntxt->num_dirty == 0)
                    		clear_fat_dirty(pdrive);
					}
					pblk = pblk_lru;
                }
            }
        }
        if (pblk)
        { /* Swapping out, clear caches and remove from the sector number ordered list */
            pc_clear_fat_blk_primary(pfatbuffcntxt, pblk);
            pc_clear_fat_blk_secondary(pfatbuffcntxt, pblk);
            pc_uncommit_fat_blk_queue(pfatbuffcntxt, pblk);
        }
    }
    if (pblk)
    {
        pblk->fat_data_buff.first_blockno = fat_sector_offset;
        pblk->fat_data_buff.block_count = pfatbuffcntxt->fat_buffer_page_size_sectors;
        pc_zero_mbuff_dirty(&pblk->fat_data_buff);
        pblk->pnext=pblk->pprev=pblk->pnext_hash=pblk->pprev_hash = 0;
        ERTFS_ASSERT(pblk->pnext != pblk)
        ERTFS_ASSERT(pblk->pprev != pblk)
    }
    return(pblk);
}


#if (INCLUDE_MULTI_BLOCKS_PERBUFFER)
int fat_sector_clip(DDRIVE *pdrive, FATBUFFCNTXT *pfatbuffcntxt, dword fat_sector_offset)
{
    dword secpfat;
    int clipped_sectorcount;

    clipped_sectorcount = pfatbuffcntxt->fat_buffer_page_size_sectors;
    secpfat = (dword) pdrive->drive_info.secpfat;
    if ( (fat_sector_offset + clipped_sectorcount) > secpfat)
    {
        clipped_sectorcount = secpfat - fat_sector_offset;
    }
    return(clipped_sectorcount);
}
#endif

#if (DEBUG_FAT_BUFFERING)
void assert_not_on_queue(DDRIVE *pdrive, FATBUFF *pblk)
{
FATBUFF *pblk_scan;

    pblk_scan =  pfatbuffcntxt->pcommitted_buffers;
    while(pblk_scan)
    {
        ERTFS_ASSERT(pblk_scan != pblk)
        pblk_scan = pblk_scan->pnext;
    }
}

void assert_on_queue(DDRIVE *pdrive, FATBUFF *pblk)
{
FATBUFF *pblk_scan;
int num_on = 0;
    pblk_scan =  pfatbuffcntxt->pcommitted_buffers;
    while(pblk_scan)
    {
        if (pblk_scan == pblk)
        {
            num_on += 1;
            ERTFS_ASSERT(num_on == 1)
        }
        pblk_scan = pblk_scan->pnext;
    }
    ERTFS_ASSERT(num_on == 1)
}
#endif /* DEBUG_FAT_BUFFERING */
