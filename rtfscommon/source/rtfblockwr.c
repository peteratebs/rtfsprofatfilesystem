/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* RTBLOCK.C - ERTFS-PRO and ProPlus merged fat block buffering routines

This file contains FAT buffering code common to RTFS Pro and RTFS ProPlus
This code is used by Pro for all FAT accesses and by ProPlus for most
FAT accesses. In ProPlus extended file operations use a differnt method.

*/

#include "rtfs.h"

#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */
#if (RTFS_CFG_LEAN)
#define INCLUDE_MULTI_BLOCKS_PERBUFFER 0
#else
#define INCLUDE_MULTI_BLOCKS_PERBUFFER 1
#endif


/* Configure the page size. Uncomment the 2 lines associated with the page size
   to select that page size

   Note: Page size may be 1,2,4,8,16 or 32.

   The optimal page size to select is dependant on the media and driver.
   Fat blocks are read from the media in "page size" chunks.

   If performing multi block reads are significantly faster than multiple single
   block reads then increasing page size should improve performance. On ATA devices
   performance of many functions doubles as the page size doubles.
*/


/* Mark primary cache empty with hi 4 bit clear and low 28 bits set.
   needed to be changed because the block at the page boundary can be now be zero,
   so we select a non zero value that can not match a real block number and set
   the hi 4 flag bits to clear. The previous value of zero is not usable because
   the paging algorithm comes up with block numbers of zero */
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


#if (INCLUDE_MULTI_BLOCKS_PERBUFFER)
int pc_get_clear_mbuff_dirty(PCMBUFF *pcmb, int *pfirst_dirty);
#endif

/* Write block to the FAT, blockno is offset from start of volume */
BOOLEAN fat_devio_write(DDRIVE *pdrive, dword fat_blockno, dword nblocks, byte *fat_data, int fatnumber)
{
dword blockno;
#if (INCLUDE_FAILSAFE_RUNTIME)
        if (prtfs_cfg->pfailsafe)
            return(prtfs_cfg->pfailsafe->fat_devio_write(pdrive, fat_blockno, nblocks, fat_data, fatnumber));
#endif

        if (fatnumber)
            blockno = fat_blockno+pdrive->drive_info.secpfat;
        else
        {
            blockno = fat_blockno;
        }
        UPDATE_RUNTIME_STATS(pdrive, fat_writes, 1)
        UPDATE_RUNTIME_STATS(pdrive, fat_blocks_written, nblocks)
        return(raw_devio_xfer(pdrive, blockno, fat_data, nblocks, FALSE, FALSE));
}

BOOLEAN pc_flush_fat_blocks(DDRIVE *pdrive)
{
    if (pc_async_flush_fat_blocks(pdrive,0) == PC_ASYNC_COMPLETE)
        return(TRUE);
    else
        return(FALSE);
}

/* Write a fat buffer to disk, first clip the blocks if necessary so the write does not go
   beyond the FAT */
BOOLEAN pc_write_fat_block_buffer_page(DDRIVE *pdrive, FATBUFF *pblk)
{
#if (INCLUDE_MULTI_BLOCKS_PERBUFFER)
byte *pclipped_data;
dword volume_blockno;
int n_dirty_blocks,dirty_block_offset;

    /* Check for dirty blocks in the buffer, get count and clear status */
    n_dirty_blocks = pc_get_clear_mbuff_dirty(&pblk->fat_data_buff, &dirty_block_offset);
    while (n_dirty_blocks)
    {
        volume_blockno = pblk->fat_data_buff.first_blockno+pdrive->drive_info.fatblock;
        pclipped_data = pblk->fat_data_buff.pdata;
        if (dirty_block_offset)
        {
            volume_blockno += dirty_block_offset;
            pclipped_data += (dirty_block_offset<<pdrive->drive_info.log2_bytespsec);
        }
        if (!pc_write_fat_blocks(pdrive, volume_blockno, n_dirty_blocks, pclipped_data,0x3))
            return(FALSE);
        n_dirty_blocks = pc_get_clear_mbuff_dirty(&pblk->fat_data_buff, &dirty_block_offset);
    }
#else
        if (pblk->fat_data_buff.dirty_count)
        {
            if (!pc_write_fat_blocks(pdrive, pblk->fat_data_buff.first_blockno+pdrive->drive_info.fatblock, 1, pblk->fat_data_buff.pdata,0x3))
                return(FALSE);
            pc_zero_mbuff_dirty(&pblk->fat_data_buff);
        }
#endif
    return(TRUE);
}

/* Write blocks to the FAT, blockno is offset from start of volume */
BOOLEAN pc_write_fat_blocks(DDRIVE *pdrive,dword fat_blockno, dword nblocks, byte *fat_data,dword which_copy)
{
    if (which_copy & 0x1)
    {
        if (!fat_devio_write(pdrive,fat_blockno, nblocks, fat_data,0))
        {
            /* set errno to PEDEVICE unless devio set errno */
io_error:
            if (!get_errno())
                rtfs_set_errno(PEIOERRORWRITEFAT, __FILE__, __LINE__); /* flush_fat device write error */
            return(FALSE);
        }

    }
    if (which_copy & 0x2 && pdrive->drive_info.numfats >= 2)
    {   /* Write the second copy */
       if (!fat_devio_write(pdrive,fat_blockno, nblocks, fat_data,1))
           goto io_error;
    }
    return(TRUE);
}


int pc_async_flush_fat_blocks(DDRIVE *pdrive,dword max_flushes_per_pass)
{
FATBUFF *pblk;
dword num_flushed;

    if (!chk_fat_dirty(pdrive))
        return(PC_ASYNC_COMPLETE);
    ERTFS_ASSERT(pdrive->fatcontext.num_dirty > 0)
    if (pdrive->drive_info.fasize == 3) /* For FAT12 convert to synchronous */
        max_flushes_per_pass = 0;
    pblk = pdrive->fatcontext.pcommitted_buffers;
    ERTFS_ASSERT(pblk)
    num_flushed = 0;
    while (pblk)
    {
        if (pblk->fat_data_buff.dirty_count)
        {
            if (max_flushes_per_pass && (num_flushed >= max_flushes_per_pass))
                   return(PC_ASYNC_CONTINUE);
            if (!pc_write_fat_block_buffer_page(pdrive,pblk))
                return(PC_ASYNC_ERROR);
            pc_zero_mbuff_dirty(&pblk->fat_data_buff);
            pdrive->fatcontext.num_dirty -= 1;
            num_flushed += 1;
        }
        pblk = pblk->pnext;
    }
    if (!pblk)
    {
        clear_fat_dirty(pdrive);
        if (!fat_flushinfo(pdrive))
            return(PC_ASYNC_ERROR);
        return(PC_ASYNC_COMPLETE);
    }
    else if (pdrive->fatcontext.num_dirty)
        return(PC_ASYNC_CONTINUE);
    else
	{
        return(PC_ASYNC_COMPLETE);
	}
}

/* RTFS mbuff support package.. Each FAT block buffer pool entry (FATBUFF)
   contains a pcmbuff structure, which supports buffering up to 32 blocks
   per FATBUFF structure. Blocks are read into the the mbuff structures
   in mutiblock reads. Writes can be multiblock but a dirty count and
   dirty bitmap are provided to allow writing only the blocks in the
   page that have been modified.

   The following routines are provided:

        void pc_set_mbuff_dirty() - Mark one or more blocks in the mbuff dirty
        void pc_zero_mbuff_dirty() - Clear dirty status for all blocks
        pc_get_clear_mbuff_dirty() - Retrieve the next contiguous dirty blocks
                                     in the mbuff and clear the dirty status.
*/


#define DEBUG_MBUFFS 0
#if (DEBUG_MBUFFS)
int countbits(dword d, int *first_bit, int *last_bit)
{
dword dirty_bit;
int i, dirty_count;

    dirty_bit = 1;
    dirty_count = 0;
    *first_bit   = 0;
    *last_bit    = 0;
    for (i = 0; i < 32; i++)
    {
        if (d & dirty_bit)
        {
            if (!dirty_count)
                *first_bit = i;
            dirty_count += 1;
            *last_bit    = i;
        }
        dirty_bit <<= 1;
    }
    return(dirty_count);
}

void check_mbuff(PCMBUFF *pcmb)
{
int dirty_count, first_bit, last_bit;
    /* Check if marked "all dirty" first */
    if (pcmb->block_count == pcmb->dirty_count && pcmb->dirty_block_offset == 0 && pcmb->dirty_bitmap == 0xffffffff)
        return;
    dirty_count = countbits(pcmb->dirty_bitmap, &first_bit, &last_bit);
    ERTFS_ASSERT(dirty_count != pcmb->dirty_count)
    if (dirty_count)
    {
        ERTFS_ASSERT(first_bit == pcmb->dirty_block_offset)
    }
}

#define DEBUG_CHECK_MBUFF(X)   check_mbuff(X);
#else
#define DEBUG_CHECK_MBUFF(X)
#endif


/* Sets sepcific sectors in the mbuff dirty called when Rtfs is configure to flush only dirty sectors in a page buffer with multiple writes */
void pc_set_mbuff_dirty(PCMBUFF *pcmb, dword block_offset, int block_count)
{
#if (INCLUDE_MULTI_BLOCKS_PERBUFFER)
dword dirty_bit;
int  block_offset_int;
    block_offset_int = (int) block_offset;

    DEBUG_CHECK_MBUFF(pcmb)

    /* Remember the numerically first dirty block, (speeds flush) */
    if (pcmb->dirty_bitmap == 0 || block_offset_int < pcmb->dirty_block_offset)
        pcmb->dirty_block_offset = block_offset;

    /* Set dirty bits */
    dirty_bit = 1;
    dirty_bit <<= block_offset_int;

    while(block_count--)
    {
        /* Defensive - test dirty bit for zero so we dont bump dirty count if args are wrong */
        if (dirty_bit && !(dirty_bit & pcmb->dirty_bitmap))
        { /* Up the dirty count and set dirty if not done already */
            pcmb->dirty_count += 1;
            pcmb->dirty_bitmap |= dirty_bit;
        }
        dirty_bit <<= 1;
    }
    DEBUG_CHECK_MBUFF(pcmb)
#else
    RTFS_ARGSUSED_INT(block_count);
    RTFS_ARGSUSED_DWORD(block_offset);
    pcmb->dirty_count = 1;
#endif
}
void pc_zero_mbuff_dirty(PCMBUFF *pcmb)
{
    pcmb->dirty_count = 0;
    pcmb->dirty_block_offset = 0;
    pcmb->dirty_bitmap = 0;
}

#if (INCLUDE_MULTI_BLOCKS_PERBUFFER)
int pc_get_clear_mbuff_dirty(PCMBUFF *pcmb, int *pfirst_dirty)
{
    DEBUG_CHECK_MBUFF(pcmb)
    if (!pcmb->dirty_count)
        return(0);

    *pfirst_dirty = pcmb->dirty_block_offset; /* Should be correct */
    if (pcmb->dirty_count == 1)
    { /* Special case, one block dirty */
        pc_zero_mbuff_dirty(pcmb);
        return(1);
    }
    else if ((pcmb->dirty_block_offset + pcmb->dirty_count) == pcmb->block_count)
    { /* Special case, all blocks dirty to the end */
        int ret_dirty_count;
        ret_dirty_count =pcmb->dirty_count;
        pc_zero_mbuff_dirty(pcmb);
        return(ret_dirty_count);
    }
    else
    { /* Return the next group of dirty blocks from the bitmap, there will be more to follow.  */
    dword dirty_bit_test,dirty_bit_clear;
    int dirty_count;
        /* Test and clear dirty bits */
        dirty_bit_test = 1;
        dirty_bit_test <<= pcmb->dirty_block_offset;
        dirty_bit_clear = ~dirty_bit_test;

        /* Defensive programming first_dirty_block should be dirty. should not happen */
        ERTFS_ASSERT((dirty_bit_test & pcmb->dirty_bitmap) != 0)
        if (!(dirty_bit_test & pcmb->dirty_bitmap))
        {
            pc_zero_mbuff_dirty(pcmb);
            return(0);
        }

        /* Count contiguous dirty bits starting from pcmb->dirty_block_offset..
           and clear the bits as we count them */
        dirty_count = 0;
        while (dirty_bit_test & pcmb->dirty_bitmap)
        {
            dirty_count += 1;
            pcmb->dirty_bitmap &= dirty_bit_clear;
            dirty_bit_test  <<= 1;
            dirty_bit_clear <<= 1;
        }
        /* Defensive programming dirty_count should not be zero. should not happen */
        ERTFS_ASSERT((dirty_count != 0))
        if (!dirty_count)
        {
            pc_zero_mbuff_dirty(pcmb);
            return(0);
        }
        pcmb->dirty_count -= dirty_count;
        /* Should never go < 0 */
        ERTFS_ASSERT(pcmb->dirty_count >= 0)
        if (pcmb->dirty_count < 0)
        {
            ERTFS_ASSERT(pcmb->dirty_bitmap == 0)
            pc_zero_mbuff_dirty(pcmb);
            return(0);
        }
        else
        {
        int clean_count;
            /* Find the next dirty block for next time */
            pcmb->dirty_block_offset += dirty_count;
            clean_count = 0;
            while (dirty_bit_test && ((dirty_bit_test & pcmb->dirty_bitmap) == 0))
            {
                clean_count += 1;
                dirty_bit_test  <<= 1;
            }
            pcmb->dirty_block_offset += clean_count; /* now contains the first dirty block */
            /* Defensive programming intervening clean bits should not be zero. should not happen */
            ERTFS_ASSERT((clean_count != 0))
            if (!clean_count)
            {
                pc_zero_mbuff_dirty(pcmb);
            }
        }
        DEBUG_CHECK_MBUFF(pcmb)
        return(dirty_count);
    }
}
#endif /* #if (INCLUDE_MULTI_BLOCKS_PERBUFFER) */


#endif /* Exclude from build if read only */
