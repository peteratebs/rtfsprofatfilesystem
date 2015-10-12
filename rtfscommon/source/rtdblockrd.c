/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* RTDBLOCK.C - ERTFS-PRO and ProPlus Directory and scrath block buffering routines */

#include "rtfs.h"

static void pc_add_blk(BLKBUFFCNTXT *pbuffcntxt, BLKBUFF *pinblk);
static void pc_release_blk(BLKBUFFCNTXT *pbuffcntxt, BLKBUFF *pinblk);


/* Debugging tools to be removed in he final product */
#define DEBUG_BLOCK_CODE    0
void debug_check_blocks(BLKBUFFCNTXT *pbuffcntxt, int numblocks,  char *where, dword line);
void debug_check_fat(FATBUFFCNTXT *pfatbuffcntxt, char *where);
void debug_break(char *where, dword line, char *message);
#if (DEBUG_BLOCK_CODE)
#define DEBUG_CHECK_BLOCKS(X,Y,Z) debug_check_blocks(X,Y,Z,0);
#else
#define DEBUG_CHECK_BLOCKS(X,Y,Z)
#endif


BOOLEAN block_devio_read(DDRIVE *pdrive, dword blockno, byte * buf)
{
#if (INCLUDE_FAILSAFE_RUNTIME)
	if (prtfs_cfg->pfailsafe)
		return(prtfs_cfg->pfailsafe->block_devio_read(pdrive, blockno, buf));
#endif
    UPDATE_RUNTIME_STATS(pdrive, dir_buff_reads, 1)
    return(raw_devio_xfer(pdrive,  blockno, buf, 1, FALSE, TRUE));
}

/* Multi block transfer to the block or fat area */
BOOLEAN block_devio_xfer(DDRIVE *pdrive, dword blockno, byte * buf, dword n_to_xfer, BOOLEAN reading)
{
#if (INCLUDE_FAILSAFE_RUNTIME)
	if (prtfs_cfg->pfailsafe)
		return(prtfs_cfg->pfailsafe->block_devio_xfer(pdrive, blockno, buf, n_to_xfer, reading));
#endif
#if (INCLUDE_DEBUG_RUNTIME_STATS)
    if (reading)
    {
        UPDATE_RUNTIME_STATS(pdrive, dir_direct_reads, 1)
        UPDATE_RUNTIME_STATS(pdrive, dir_direct_blocks_read, n_to_xfer)
    }
    else
    {
        UPDATE_RUNTIME_STATS(pdrive, dir_direct_writes, 1)
        UPDATE_RUNTIME_STATS(pdrive, dir_direct_blocks_written, n_to_xfer)
    }
#endif
    return(raw_devio_xfer(pdrive, blockno, buf, n_to_xfer, FALSE, reading));
}

/*****************************************************************************
    pc_release_buf - Unlock a block buffer.

Description
    Give back a buffer to the system buffer pool so that it may
    be re-used. If was_err is TRUE this means that the data in the
    buffer is invalid so discard the buffer from the buffer pool.

 Returns
    Nothing

***************************************************************************/

void pc_release_buf(BLKBUFF *pblk)
{
    if (!pblk)
        return;
    DEBUG_CHECK_BLOCKS(pblk->pdrive->pbuffcntxt, pblk->pdrive->pbuffcntxt->num_blocks, "Release")
    ERTFS_ASSERT(chk_mount_valid(pblk->pdrive))
    ERTFS_ASSERT(pblk->block_state == DIRBLOCK_UNCOMMITTED)

    if (pblk->block_state != DIRBLOCK_UNCOMMITTED)
        return;
    OS_CLAIM_FSCRITICAL()
    if (pblk->use_count)
    {
        pblk->use_count -= 1;
      /* 03-07-07 Changed. No longer increment num_free if usecount goes to zero */
    }
    OS_RELEASE_FSCRITICAL()
}

/*****************************************************************************
    pc_discard_buf - Put a buffer back on the free list.

Description
    Check if a buffer is in the buffer pool, unlink it from the
    buffer pool if it is.
    Put the buffer on the free list.

 Returns
    Nothing

***************************************************************************/

void pc_discard_buf(BLKBUFF *pblk)
{
BLKBUFFCNTXT *pbuffcntxt;

    if (!pblk)
        return;
    /*note:pblk->pdrive must not be 0. if so, use pc_free_scratch_buffer() */
    ERTFS_ASSERT(pblk->block_state != DIRBLOCK_FREE)
    if (pblk->block_state == DIRBLOCK_FREE)
        return;

    OS_CLAIM_FSCRITICAL()
    pbuffcntxt = pblk->pdrive->drive_state.pbuffcntxt;
    DEBUG_CHECK_BLOCKS(pbuffcntxt, pbuffcntxt->num_blocks, "Discard 1")
    if (pblk->block_state == DIRBLOCK_UNCOMMITTED)
    {
        /* Release it from the buffer pool */
        pc_release_blk(pbuffcntxt, pblk);
        /* If there's a next make sure we are the prev */
        ERTFS_ASSERT(!pblk->pnext || (pblk->pnext->pprev == pblk))
        /* If there's a prev make sure we're the next */
        ERTFS_ASSERT(!pblk->pprev || (pblk->pprev->pnext == pblk))
        /* Unlink it from the populated pool double check link integrity */
        if (pblk->pnext && pblk->pnext->pprev == pblk)
            pblk->pnext->pprev = pblk->pprev;
        if (pblk->pprev && pblk->pprev->pnext == pblk)
            pblk->pprev->pnext = pblk->pnext;
        if (pbuffcntxt->ppopulated_blocks == pblk)
            pbuffcntxt->ppopulated_blocks = pblk->pnext;
    }
    pblk->block_state = DIRBLOCK_FREE;
    pblk->pnext = pbuffcntxt->pfree_blocks;
    pbuffcntxt->num_free += 1;
    pbuffcntxt->pfree_blocks = pblk;
    OS_RELEASE_FSCRITICAL()
    DEBUG_CHECK_BLOCKS(pbuffcntxt, pbuffcntxt->num_blocks, "Discard 2")
}

/****************************************************************************
    PC_READ_BLK - Allocate and read a BLKBUFF, or get it from the buffer pool.

Description
    Use pdrive and blockno to determine what block to read. Read the block
    or get it from the buffer pool and return the buffer.

    Note: After reading, you own the buffer. You must release it by
    calling pc_release_buff() or pc_discard_buff() before it may be
    used for other blocks.

 Returns
    Returns a valid pointer or NULL if block not found and not readable.

*****************************************************************************/



BLKBUFF *pc_read_blk(DDRIVE *pdrive, dword blockno)                /*__fn__*/
{
BLKBUFF *pblk;
BLKBUFFCNTXT *pbuffcntxt;

    if ( !pdrive || (blockno >= pdrive->drive_info.numsecs) )
        return(0);
    OS_CLAIM_FSCRITICAL()
    DEBUG_CHECK_BLOCKS(pdrive->pbuffcntxt, pdrive->pbuffcntxt->num_blocks, "Read 1")
    /* Try to find it in the buffer pool */
    pblk = pc_find_blk(pdrive, blockno);
    if (pblk)
    {
        UPDATE_RUNTIME_STATS(pdrive, dir_buff_hits, 1)
        pdrive->drive_state.pbuffcntxt->stat_cache_hits += 1;
        DEBUG_CHECK_BLOCKS(pdrive->drive_state.pbuffcntxt, pdrive->pbuffcntxt->num_blocks, "Read 2")
        pblk->use_count += 1;
        OS_RELEASE_FSCRITICAL()
    }
    else
    {
        pdrive->drive_state.pbuffcntxt->stat_cache_misses += 1;
        /* Allocate, read, stitch into the populated list, and the buffer pool */
        pblk = pc_allocate_blk(pdrive, pdrive->drive_state.pbuffcntxt);
        OS_RELEASE_FSCRITICAL()
        if (pblk)
        {
            if (block_devio_read(pdrive, blockno, pblk->data))
            {
                pbuffcntxt = pdrive->drive_state.pbuffcntxt;
                pblk->use_count = 1;
                pblk->block_state = DIRBLOCK_UNCOMMITTED;
                pblk->blockno = blockno;
                pblk->pdrive = pdrive;
                /* Put in front of populated list (it's new) */
                OS_CLAIM_FSCRITICAL()
                pblk->pprev = 0;
                pblk->pnext = pbuffcntxt->ppopulated_blocks;
                if (pbuffcntxt->ppopulated_blocks)
                    pbuffcntxt->ppopulated_blocks->pprev = pblk;
                pbuffcntxt->ppopulated_blocks = pblk;
                pc_add_blk(pbuffcntxt, pblk); /* Add it to the buffer pool */
                DEBUG_CHECK_BLOCKS(pdrive->pbuffcntxt, pdrive->pbuffcntxt->num_blocks, "Read 3")
                OS_RELEASE_FSCRITICAL()
            }
            else
            {
                /* set errno to IO error unless devio set PEDEVICE */
                if (!get_errno())
                    rtfs_set_errno(PEIOERRORREADBLOCK, __FILE__, __LINE__); /* pc_read_blk device read error */
                pc_discard_buf(pblk);
                DEBUG_CHECK_BLOCKS(pdrive->pbuffcntxt, pdrive->pbuffcntxt->num_blocks, "Read 4")
                pblk = 0;
            }
        }
    }
    DEBUG_CHECK_BLOCKS(pdrive->pbuffcntxt, pdrive->pbuffcntxt->num_blocks, "Read 5")
    return(pblk);
}


/***************************************************************************
    PC_SYS_SECTOR - Return a buffer large enough to hold a sector for scratch purposes.

Description
    Use the device driver to allocate a sector buffer. If the driver does not
    have allocator support then allocate a scratch buffer.
    When done call void pc_free_sys_sector(pblk) to clean up

 Returns
    Returns a blkbuf if one is available or NULL
****************************************************************************/
/* Allocate a scratch sector buffer */

BLKBUFF *pc_sys_sector(DDRIVE *pdr, BLKBUFF *pscratch_buff)                                   /*__fn__*/
{
dword buffer_size_sectors;

    if (!pdr)
        return(0);
    /* Zero the scratch buffer to be sure we can't possibly get a match */
    rtfs_memset(pscratch_buff,(byte) 0, sizeof(*pscratch_buff));
    /* dynamic drivers use the user buffer which will contain at least one sector */
    pscratch_buff->data = pc_claim_user_buffer(pdr, &buffer_size_sectors, 1);
    if (pscratch_buff->data)
    {
        pscratch_buff->pdrive = pdr;
        pscratch_buff->pdrive_owner = pdr;
        pscratch_buff->data_size_bytes = buffer_size_sectors * pdr->pmedia_info->sector_size_bytes;
        return(pscratch_buff);
    }
    else
        return(0);
}

/* Free a scratch sector, which may have been allocated by the driver or it may be a shared scratch block */
void pc_free_sys_sector(BLKBUFF *pscratch_buff)
{
    /* pc_sys_sector() uses the user buffer so release it here */
    pc_release_user_buffer(pscratch_buff->pdrive_owner, pscratch_buff->data);
}


/***************************************************************************
    PC_SCRATCH_BLK - Return a block for scratch purposes.
Description
    Use the block buffer pool as a heap of default sector sized memory locations
    When done call void pc_free_scratch_buf(pblk) to clean up

 Returns
    Returns a blkbuf if one is available or NULL
****************************************************************************/
/* Allocate a scratch buffer from the shared (amongst drives) buffer pool */
BLKBUFF *pc_scratch_blk(void)                                   /*__fn__*/
{
BLKBUFF *pblk;
    OS_CLAIM_FSCRITICAL()
    pblk = pc_allocate_blk(0, &prtfs_cfg->buffcntxt);
    if (pblk)
    {
        pblk->pdrive_owner = 0;
        prtfs_cfg->buffcntxt.scratch_alloc_count += 1;
#if (INCLUDE_DEBUG_LEAK_CHECKING)
        {
BLKBUFFCNTXT *pbuffcntxt;
        pbuffcntxt = &prtfs_cfg->buffcntxt;
            pblk->pnext = pbuffcntxt->pscratch_blocks;
            pbuffcntxt->pscratch_blocks = pblk;
        }
#endif
    }
    OS_RELEASE_FSCRITICAL()
    /* Remember the source for when we release it */
    pblk->pdrive_owner = 0;
    return (pblk);
}


/* Free a scratch buffer to the shared (amongst drives) buffer pool */
void pc_free_scratch_blk(BLKBUFF *pblk)
{
    BLKBUFFCNTXT *pbuffcntxt;
    OS_CLAIM_FSCRITICAL()
    pbuffcntxt = &prtfs_cfg->buffcntxt;
#if (INCLUDE_DEBUG_LEAK_CHECKING)
    if (pblk == pbuffcntxt->pscratch_blocks)
        pbuffcntxt->pscratch_blocks = pblk->pnext;
    else
    {
        BLKBUFF *pblk_scan;
        pblk_scan = pbuffcntxt->pscratch_blocks;
        while (pblk_scan)
        {
            if (pblk_scan->pnext == pblk)
            {
                pblk_scan->pnext = pblk->pnext;
                break;
            }
            pblk_scan = pblk_scan->pnext;
        }
    }
#endif
    pblk->pnext = pbuffcntxt->pfree_blocks;
    pbuffcntxt->pfree_blocks = pblk;
    pblk->block_state = DIRBLOCK_FREE;
    pbuffcntxt->num_free += 1;
    pbuffcntxt->scratch_alloc_count -= 1;
    OS_RELEASE_FSCRITICAL()
}


/***************************************************************************
    PC_INIT_BLK - Zero a BLKBUFF and add it to the buffer pool
Description
    Allocate and zero a BLKBUFF and add it to the to the buffer pool.

    Note: After initializing you own the buffer. You must release it by
    calling pc_release_buff() or pc_discard_buf() before it may be used
    for other blocks.

 Returns
    Returns a valid pointer or NULL if no core.

****************************************************************************/

BLKBUFF *pc_init_blk(DDRIVE *pdrive, dword blockno)                /*__fn__*/
{
BLKBUFF *pblk;
BLKBUFFCNTXT *pbuffcntxt;

    if ( !pdrive || (blockno >= pdrive->drive_info.numsecs) )
        return(0);
    /* Find it in the buffer pool */
    OS_CLAIM_FSCRITICAL()
    DEBUG_CHECK_BLOCKS(pdrive->pbuffcntxt, pdrive->pbuffcntxt->num_blocks, "Init 1")
    pblk = pc_find_blk(pdrive, blockno);
    if (pblk)
    {
        pblk->use_count += 1;
        OS_RELEASE_FSCRITICAL()
    }
    else
    {
        /* Allocate, read, stitch into the populated list, and the buffer pool */
        DEBUG_CHECK_BLOCKS(pdrive->pbuffcntxt, pdrive->pbuffcntxt->num_blocks, "Init 2")
        pblk = pc_allocate_blk(pdrive, pdrive->drive_state.pbuffcntxt);

        OS_RELEASE_FSCRITICAL()
        if (pblk)
        {
            DEBUG_CHECK_BLOCKS(pdrive->pbuffcntxt, pdrive->pbuffcntxt->num_blocks-1, "Init 2_a")
            pbuffcntxt = pdrive->drive_state.pbuffcntxt;
            pblk->use_count = 1;
            pblk->block_state = DIRBLOCK_UNCOMMITTED;
            pblk->blockno = blockno;
            pblk->pdrive = pdrive;
            /* Put in front of populated list (it's new) */
            OS_CLAIM_FSCRITICAL()
            pblk->pprev = 0;
            pblk->pnext = pbuffcntxt->ppopulated_blocks;
            if (pbuffcntxt->ppopulated_blocks)
                pbuffcntxt->ppopulated_blocks->pprev = pblk;
            pbuffcntxt->ppopulated_blocks = pblk;
            pc_add_blk(pbuffcntxt, pblk); /* Add it to the buffer pool */
            OS_RELEASE_FSCRITICAL()
            DEBUG_CHECK_BLOCKS(pdrive->pbuffcntxt, pdrive->pbuffcntxt->num_blocks, "Init 3")
        }
    }
    if (pblk)
        rtfs_memset(pblk->data, (byte) 0, pblk->data_size_bytes);
    return(pblk);
}

/****************************************************************************
    PC_FREE_ALL_BLK - Release all buffers associated with a drive
Description
    Use pdrive to find all buffers in the buffer pool associated with the
    drive. Mark them as unused, called by dsk_close.
    If any are locked, print a debug message in debug mode to warn the
    programmer.

 Returns
    Nothing
****************************************************************************/
void pc_free_all_blk(DDRIVE *pdrive)                                /*__fn__*/
{
BLKBUFFCNTXT *pbuffcntxt;
BLKBUFF *pblk;
BOOLEAN deleting;

    if (!pdrive)
        return;
    pbuffcntxt = pdrive->drive_state.pbuffcntxt;
    DEBUG_CHECK_BLOCKS(pbuffcntxt, pbuffcntxt->num_blocks, "Free all 1")
    do
    {
        deleting = FALSE;
        OS_CLAIM_FSCRITICAL()
        pblk = pbuffcntxt->ppopulated_blocks;
        while (pblk)
        {
            if (pblk->pdrive == pdrive)
            {
                OS_RELEASE_FSCRITICAL()
                pc_discard_buf(pblk);
                OS_CLAIM_FSCRITICAL()
                deleting = TRUE;
                break;
            }
            else
                pblk = pblk->pnext;
        }
        OS_RELEASE_FSCRITICAL()
    } while (deleting);
    DEBUG_CHECK_BLOCKS(pbuffcntxt, pbuffcntxt->num_blocks, "Free all 2")
}

/* Add a block to the block buffer pool */
static void pc_add_blk(BLKBUFFCNTXT *pbuffcntxt, BLKBUFF *pinblk)
{
int hash_index;
    hash_index = (int) (pinblk->blockno&BLOCK_HASHMASK);
    pinblk->pnext2 = pbuffcntxt->blk_hash_tbl[hash_index];
    pbuffcntxt->blk_hash_tbl[hash_index] = pinblk;
}
/* Remove a block from the block buffer pool */
static void pc_release_blk(BLKBUFFCNTXT *pbuffcntxt, BLKBUFF *pinblk)
{
int hash_index;
BLKBUFF *pblk;

    hash_index = (int) (pinblk->blockno&BLOCK_HASHMASK);
    pblk = pbuffcntxt->blk_hash_tbl[hash_index];
    if (pblk == pinblk)
        pbuffcntxt->blk_hash_tbl[hash_index] = pinblk->pnext2;
    else
    {
        while (pblk)
        {
            if (pblk->pnext2==pinblk)
            {
                pblk->pnext2 = pinblk->pnext2;
                break;
            }
            pblk = pblk->pnext2;
        }
    }
}
/* Find a block in the block buffer pool */
BLKBUFF *pc_find_blk(DDRIVE *pdrive, dword blockno)
{
BLKBUFFCNTXT *pbuffcntxt;
int hash_index;
BLKBUFF *pblk;

    pbuffcntxt = pdrive->drive_state.pbuffcntxt;
    hash_index = (int) (blockno&BLOCK_HASHMASK);
    pblk = pbuffcntxt->blk_hash_tbl[hash_index];
    while (pblk)
    {
        if (pblk->blockno == blockno && pblk->pdrive == pdrive)
        {
                if (pblk->block_state == DIRBLOCK_UNCOMMITTED)
                { /* Put in front of populated list (it's the last touched) */
                /* Unlink it from the populated pool */
                    if (pbuffcntxt->ppopulated_blocks != pblk)
                    {   /* Since we aren't the root we know pprev is good */
                        pblk->pprev->pnext = pblk->pnext;   /* Unlink. */
                        if (pblk->pnext)
                            pblk->pnext->pprev = pblk->pprev;
                        pblk->pprev = 0;                    /* link in front*/
                        pblk->pnext = pbuffcntxt->ppopulated_blocks;
                        if (pbuffcntxt->ppopulated_blocks)
                            pbuffcntxt->ppopulated_blocks->pprev = pblk;
                        pbuffcntxt->ppopulated_blocks = pblk;
                    }
                }
                return(pblk);
        }
        pblk=pblk->pnext2;
    }
    return(0);
}
/* Allocate a block or re-use an un-committed one */
BLKBUFF *pc_allocate_blk(DDRIVE *pdrive, BLKBUFFCNTXT *pbuffcntxt)
{
BLKBUFF *pfreeblk,*puncommitedblk, *pfoundblk, *pblkscan;
int populated_but_uncommited;

    /* Note: pdrive may be NULL, do not dereference the pointer */
    pfreeblk = pfoundblk = puncommitedblk = 0;
    populated_but_uncommited = 0;

    /* Use blocks that are on the freelist first */
    if (pbuffcntxt->pfree_blocks)
    {
        pfreeblk = pbuffcntxt->pfree_blocks;
        pbuffcntxt->pfree_blocks = pfreeblk->pnext;
        pbuffcntxt->num_free -= 1;
    }

    /* Scan the populated list. Count the number of uncommited blocks to set low water marks
       and, if we haven't already allocated a block from the free list, select a replacement block. */
    if (pbuffcntxt->ppopulated_blocks)
    {
        int loop_guard = 0;
        /* Count UNCOMMITED blocks and find the oldest UNCOMMITED block in the list */
        pblkscan = pbuffcntxt->ppopulated_blocks;
        while (pblkscan)
        {
           if (pblkscan->block_state == DIRBLOCK_UNCOMMITTED && !pblkscan->use_count)
           {
                puncommitedblk = pblkscan;
                populated_but_uncommited += 1;
           }
           pblkscan = pblkscan->pnext;
           /* Guard against endless loop */
           if (loop_guard++ > pbuffcntxt->num_blocks)
           {
                rtfs_set_errno(PEINTERNAL, __FILE__, __LINE__); /* pc_allocate_blk: Internal error*/
                return(0);
           }
        }
        /* If we don't already have a free block we'll reuse the oldest uncommitted block so release it */
        if (!pfreeblk && puncommitedblk)
        {
            pc_release_blk(pbuffcntxt, puncommitedblk); /* Remove it from buffer pool */
            /* Unlink it from the populated pool */
            if (puncommitedblk->pnext)
                puncommitedblk->pnext->pprev = puncommitedblk->pprev;
            if (puncommitedblk->pprev)
                puncommitedblk->pprev->pnext = puncommitedblk->pnext;
            if (pbuffcntxt->ppopulated_blocks == puncommitedblk)
                pbuffcntxt->ppopulated_blocks = puncommitedblk->pnext;
        }
    }
    if (pfreeblk)
        pfoundblk = pfreeblk;
    else
        pfoundblk = puncommitedblk;

    if (pfoundblk)
    {   /* Put in a known state */
        /* 03-07-2007 using a different method to calculate low water mark. Previous method
           undercounted the worst case buffer allocation requirements */
        if (pbuffcntxt->num_free + populated_but_uncommited < pbuffcntxt->low_water)
            pbuffcntxt->low_water = pbuffcntxt->num_free + populated_but_uncommited;
        pfoundblk->use_count = 0;
        pfoundblk->block_state = DIRBLOCK_ALLOCATED;
        pfoundblk->pdrive = pdrive;
    }
    else
    {
        pbuffcntxt->num_alloc_failures += 1;
        rtfs_set_errno(PERESOURCEBLOCK, __FILE__, __LINE__); /* pc_allocate_blk out of resources */
    }
    return(pfoundblk);
}

/* Initialize and populate a block buffer context structure

Called from pc_memory_init() to initialize the shared block buffer pool.
May also be called by device drivers that provide dynamic configuration.

  BOOLEAN pc_initialize_block_pool(
    BLKBUFFCNTXT *pbuffcntxt - Block buffer context, there is 1 globally shared context but device drivers may
                               allocate drive specific contexts.
    int nblkbuffs            - Number of buffers
    BLKBUFF *pmem_block_pool - Pointer to enough space to hold nblkbuffs time the size of the BLKBUFF control structure
    byte *raw_buffer_space   - Optional pointer to enough space to hold nblkbuffs time the size of the BLKBUFF control structure
                                Note: This parameter is optional for device drivers.
                                      If the device drivers populates the pdata field of each individual control structure
                                      before calling it calls pmem_block_pool then it should pass 0 for the
                                      raw_buffer_space parameter.
                                      This method may be used to populate the buffers with memory addresses located
                                      on device specific address boundaries.
    dword data_size_bytes -      Size of each block buffer in bytes.
                                Note: This field is not ioptional.
                                      It must equal the sector size in bytes and always be provided.
*/


void pc_initialize_block_pool(BLKBUFFCNTXT *pbuffcntxt, int nblkbuffs, BLKBUFF *pmem_block_pool, byte *raw_buffer_space, dword data_size_bytes)
{
int i;
BLKBUFF *pblk;
    rtfs_memset(pbuffcntxt,(byte) 0, sizeof(BLKBUFFCNTXT));
    /* First clear all the buffer control structures, preserve the data field in case it was provided to us */
    pblk = pmem_block_pool;
    for (i = 0; i < nblkbuffs; i++, pblk++)
    {
        byte *saved_buffer_address;
        saved_buffer_address = pblk->data;
        rtfs_memset(pblk,(byte) 0, sizeof(BLKBUFF));
        pblk->data = saved_buffer_address;
        pblk->data_size_bytes = data_size_bytes;
    }

    /* Save the initial value in case we have to delete it */
    pbuffcntxt->assigned_free_buffers = pmem_block_pool;
    /* Make the provided block pool array our free list */
    pbuffcntxt->pfree_blocks = pmem_block_pool;
    /* Link all control structures together into a list a free list */
    pblk = pmem_block_pool;
    for (i = 0; i < (nblkbuffs-1); i++, pblk++)
        pblk->pnext = pblk+1;
    pblk->pnext = 0;

    /* If raw_buffer_space was passed in then set the buffer addresses in the control structures to offsets in
       raw_buffer_space. raw_buffer_space must be at least (nblkbuffs * data_size_bytes) bytes wide.
       If the drive was configured from the application layer raw_buffer_space will point to buffer space.
       If the drive was configured by a device driver ioctl call then raw_buffer_space will not point to
       buffer space and the control structures' pdata filed will be valid already */
    if (raw_buffer_space)
    {
       pblk = pmem_block_pool;
       for (i = 0; i < nblkbuffs; i++, pblk++)
       {
            pblk->data = raw_buffer_space;
            raw_buffer_space += data_size_bytes;
       }
    }
    /* Set up the rest. The hash table was already zeroed when the structure was zeroed, the hash table size
       and mask are now a fixed size */
    pbuffcntxt->num_blocks = nblkbuffs;
    pbuffcntxt->num_free = nblkbuffs;
    pbuffcntxt->low_water = nblkbuffs;
    pbuffcntxt->num_alloc_failures = 0;
}
#if (DEBUG_BLOCK_CODE)
void debug_check_blocks(BLKBUFFCNTXT *pbuffcntxt, int numblocks,  char *where, dword line)
{
BLKBUFF *pblk;
BLKBUFF *pblk_prev;
int nb = 0;
int nfreelist = 0;
int npopulatedlist = 0;

int i;
    pblk = pbuffcntxt->pfree_blocks;
    while (pblk)
    {
        nb += 1;
        /* Check for bad freelist */
        ERTFS_ASSERT(nb <= numblocks)
        pblk = pblk->pnext;
    }
    nfreelist = nb;
    pblk = pbuffcntxt->ppopulated_blocks;
    ERTFS_ASSERT(nb <= numblocks)
    if (pblk && pblk->pprev)
    {
        /* Bad populated root */
        ERTFS_ASSERT(0)
    }
    while (pblk)
    {
        npopulatedlist += 1;
        nb += 1;
        /* Check for Bad populated list */
        ERTFS_ASSERT(nb <= numblocks)
        pblk = pblk->pnext;
    }

    /* Add in outstanding scratch allocates */
    nb += pbuffcntxt->scratch_alloc_count;

    /* Check for leaks */
    ERTFS_ASSERT(nb == numblocks)

    if (pbuffcntxt->ppopulated_blocks)
    {
        pblk_prev = pblk = pbuffcntxt->ppopulated_blocks;
        pblk = pblk->pnext;
        while (pblk)
        {
            /* Check for Bad link in populated list */
            ERTFS_ASSERT(pblk->pprev == pblk_prev)
            pblk_prev = pblk;
            pblk = pblk->pnext;
        }
    }
    for (i = 0; i <  BLOCK_HASHSIZE; i++)
    {
        nb = 0;
        pblk = pbuffcntxt->blk_hash_tbl[i];
        while (pblk)
        {
            /* Check for Block in wrong hash slot */
            ERTFS_ASSERT(i == (int) (pblk->blockno&BLOCK_HASHMASK))

            nb += 1;
            /* Check for loop in hash table */
            ERTFS_ASSERT(nb <= numblocks)
            pblk = pblk->pnext2;
        }
    }
}

/* Diagnostic to display list list contents for FINODE and DROBJ pools.

    display_free_lists(char *in_where)

    Prints:
        FINODES on FREE list, FINODES on in use list.
        Drobj structures on  freelist,
        drob structure count marked free by scanning the drobj pool sequentially
        BLKBUFF buffer free count, and low water count
        BLKBUFF buffers counted on populated list
        BLKBUFF buffers counted on free list


        If populated count and free list don't add up the remainder will be scratch
        buffers.

        To Do: Add counters for scratch buffer allocation and frees.

    Useful for validating that no leaks are occuring.


*/
#define DEBUGPRINTF printf

void display_free_lists(char *in_where)
{
    FINODE *pfi;
    DROBJ *pobj;
    struct blkbuff *pblk;
    int j, i, objcount, finodecount,populated_block_count,free_list_count;
    objcount = finodecount = i = populated_block_count = free_list_count = 0;

    pfi = prtfs_cfg->mem_finode_freelist;
    while (pfi)
    {
        i++;
        pfi = pfi->pnext;
    }
    finodecount = 0;
    pfi = prtfs_cfg->inoroot;
    while (pfi)
    {
        finodecount++;
        pfi = pfi->pnext;
    }
    DEBUGPRINTF("%-10.10s:INODES  free:%4.4d in-use:%4.4d total:%4.4d \n", in_where, i,finodecount,prtfs_cfg->cfg_NFINODES);
    i = 0;
    pobj = prtfs_cfg->mem_drobj_freelist;
    while (pobj)
    {
        i++;
        pobj = (DROBJ *) pobj->pdrive;
    }
    pobj =  prtfs_cfg->mem_drobj_pool;
    objcount = 0;
    for (j = 0; j < prtfs_cfg->cfg_NDROBJS; j++, pobj++)
    {
        if (!pobj->is_free)
            objcount += 1;
    }
    DEBUGPRINTF("%-10.10s:DROBJS  free:%4.4d in-use:%4.4d total:%4.4d \n", in_where, i,objcount, prtfs_cfg->cfg_NDROBJS);

    pblk = prtfs_cfg->buffcntxt.ppopulated_blocks; /* uses pnext/pprev */
    populated_block_count = 0;
    while (pblk)
    {
        populated_block_count += 1;
        pblk = pblk->pnext;
    }
    DEBUGPRINTF("%-10.10s:BLKBUFS free:%4.4d in-use:%4.4d low w:%4.4d scratch:%4.4d total:%4.4d \n",in_where,
        prtfs_cfg->buffcntxt.num_free,
        populated_block_count,
        prtfs_cfg->buffcntxt.low_water,
        prtfs_cfg->buffcntxt.scratch_alloc_count,
        prtfs_cfg->buffcntxt.num_blocks);

    pblk = prtfs_cfg->buffcntxt.pfree_blocks;
    free_list_count = 0;
    while (pblk)
    {
        free_list_count += 1;
        pblk = pblk->pnext;
    }

    if (free_list_count != prtfs_cfg->buffcntxt.num_free)
    {
        DEBUGPRINTF("%-10.10s:Error num_freelist == %d but %d elements on the freelist\n",in_where, prtfs_cfg->buffcntxt.num_free, free_list_count);
    }
}

/* May be called to detect buffer pool leaks */
void check_blocks(DDRIVE *pdrive, char *prompt, dword line)
{
    debug_check_blocks(pdrive->pbuffcntxt, pdrive->pbuffcntxt->num_blocks, prompt, line);
}

#endif /* (DEBUG_BLOCK_CODE) */
