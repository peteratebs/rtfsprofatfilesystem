/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2008
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* RTFILEBUFFER.C - Common buffered file read write function

Exports the following functions:

void pc_release_file_buffer(BLKBUFF *pblk) - Called to release the finode's file buffering when the finode is freed.
                                             (when the file is closed)

BOOLEAN pc_buffered_fileio() - Called by Rtfs to perform all file IO data transfers to or from the media.

    If the data is properly aligned the IO request is passed directly to the device driver
    If some or all of the data is not properly aligned then the function transfers unaligned
    data through intermediate buffers and aligned data directly

    By default the file buffer size equals the the default sector size.

    A different file buffer size may be used if the underlying device driver provides the allocation.

    For FLASH it is recomendeded that the file buffer size be the same size as the erase block size.



*/

#include "rtfs.h"


static void pc_discard_file_buffer(FINODE *pfinode);
static BOOLEAN pc_sync_file_buffer(FINODE *pfinode, dword io_start_sector, dword n_sectors, BOOLEAN reading);
static BOOLEAN pc_check_unbuffered(DDRIVE *pdr, dword buffered_start_sector, dword buffer_size_bytes, dword buffer_size_sectors, dword start_sector, dword start_byte_offset, dword n_bytes, dword *psector_count, BOOLEAN reading);
static BOOLEAN pc_perform_buffered_fileio(FINODE *pfinode, dword start_sector, dword start_byte_offset, dword n_bytes, byte *pdata, dword *n_bytes_transfered, dword *n_sectors_transfered, BOOLEAN reading, BOOLEAN appending);
static BOOLEAN pc_file_buffer_size(DDRIVE *pdr, BLKBUFF *pfile_buffer, dword *psize_sectors, dword *psize_bytes);
static BLKBUFF *pc_alloc_file_buffer(FINODE *pfinode);


/*
* BOOLEAN pc_buffered_fileio(FINODE *pfinode, dword start_sector, dword start_byte_offset, dword n_todo, byte *pdata,  BOOLEAN reading)
*
* Exported function, all file IO operations call this funtion to transfer data to or from the media.
*
*
*/
BOOLEAN pc_buffered_fileio(FINODE *pfinode, dword start_sector, dword start_byte_offset, dword n_todo, byte *pdata,  BOOLEAN reading, BOOLEAN appending)
{
DDRIVE *pdr;

    if (!(pfinode->openflags & OF_BUFFERED))    /* There shouldn't be a buffer but clear just in case */
        pc_discard_file_buffer(pfinode);

	pdr = pfinode->my_drive;

    /* The loop will complete in one pass unless processing unaligned data.*/
    while (n_todo)
    {
    dword sector_count, n_bytes_transfered, n_sectors_transfered;
	dword buffered_start_sector, buffer_size_bytes, buffer_size_sectors;
	BOOLEAN release_file_buffer;
    BLKBUFF *pscratch_buff;
    BLKBUFF bbuf_scratch;   /* Used by pc_sys_sector build a buffer structure with core from the driver level */

		pscratch_buff = 0;
		release_file_buffer = FALSE;
        sector_count = n_bytes_transfered = n_sectors_transfered = 0;

        if ((pfinode->openflags & OF_BUFFERED) == 0)
        {
			buffered_start_sector = 0;
            /* In unbuffered mode create a temporary file buffer using the user buffer for data transfer
               do this because even if file buffer size is < eraseblock size we can double buffer using the larger sys sector buffer in un buffered mode */
            pscratch_buff = pc_sys_sector(pdr, &bbuf_scratch);
            if (!pscratch_buff)
            	return(FALSE);

			/* Set the buffer size to a sector size or erase block size if the media has erase blocks and the buffer is large enough */
			if (pdr->pmedia_info->eraseblock_size_sectors)
			{
			dword erase_block_size_bytes;
				erase_block_size_bytes = pdr->pmedia_info->eraseblock_size_sectors * pdr->pmedia_info->sector_size_bytes;
				if (pscratch_buff->data_size_bytes > erase_block_size_bytes)
					pscratch_buff->data_size_bytes = erase_block_size_bytes;
			}
			else
				pscratch_buff->data_size_bytes = pdr->pmedia_info->sector_size_bytes;

			/* Calculate number of sectors in the scratch buffer from number of bytes in the buffer and sector size */
			pc_file_buffer_size(pdr, pscratch_buff, &buffer_size_sectors, &buffer_size_bytes);
        }
		else
		{
			/* Buffered mode, we either already have a buffer allocated or we may need one, so
			   make sure we have a file buffer allocated, we need to do this to determine the size of the buffer
			   for dynamic devices.. The size of the file buffer can change because the underlying system
			   ran out of erase block sized buffers and switched to sector buffers. We want to have the same
			   buffer trhoughout the call so use pc_load_file_buffer(pfinode, 1, FALSE) to load a bogus buffer
			   can not use pc_load_file_buffer(pfinode, 0, FALSE) because 0 tells the underlying service to release
			   the buffer */
        	if (pfinode->pfile_buffer)
				buffered_start_sector = pfinode->pfile_buffer->blockno;
        	else
        	{
				buffered_start_sector = 0;
             	if (!pc_load_file_buffer(pfinode, 1, FALSE))
                	return(FALSE); /* pc_alloc_file_buffer() set errno */
            	release_file_buffer = TRUE; /* Release the buffer if we determine that it is unbuffered IO */
			}
			/* Calculate number of sectors in the file buffer from number of bytes in the buffer and sector size */
            pc_file_buffer_size(pdr, pfinode->pfile_buffer, &buffer_size_sectors, &buffer_size_bytes);
		}

        /* Test if we should perform un-buffered IO. uses the buffer size of pfinode->pfile_buffer */
        if (pc_check_unbuffered(pdr, buffered_start_sector, buffer_size_bytes, buffer_size_sectors, start_sector, start_byte_offset, n_todo, &sector_count, reading))
        {  /* sector_count was be updated with the number of sectors to process */
        dword save_drive_filio;
            /* Unbuffered IO, release the buffer we used for sizing */
            if (release_file_buffer)
                pc_discard_file_buffer(pfinode);
			else if (pscratch_buff)
			{
				pc_free_sys_sector(pscratch_buff);
				pscratch_buff = 0;
			}

            if (!sector_count) /* Defensive: sector count should be at least one for unbuffered IO*/
            {
                rtfs_set_errno(PEINTERNAL, __FILE__, __LINE__);
                return(FALSE);
            }
            save_drive_filio = pfinode->my_drive->drive_flags & DRIVE_FLAGS_FILEIO;
            pfinode->my_drive->drive_flags |= DRIVE_FLAGS_FILEIO;
            if (reading)
            {   /* Perform un-buffered read */
                /* First flush the file buffer if it contains any sectors in our range */
                if (!pc_sync_file_buffer(pfinode, start_sector, sector_count, TRUE))
                    goto read_error;
				/* Discard the file buffer if there is one */
                pc_discard_file_buffer(pfinode);
                UPDATE_RUNTIME_STATS(pfinode->my_drive, file_direct_reads, 1)
                UPDATE_RUNTIME_STATS(pfinode->my_drive, file_direct_blocks_read, sector_count)
                if (!raw_devio_xfer(pfinode->my_drive, start_sector, pdata, sector_count, FALSE, TRUE))
                {
read_error:       /* set errno to IO error unless devio set PEDEVICE */
                    if (!save_drive_filio)
                        pfinode->my_drive->drive_flags &= ~DRIVE_FLAGS_FILEIO;
                    if (!get_errno()) rtfs_set_errno(PEIOERRORREAD, __FILE__, __LINE__);
                    return(FALSE);
                }
            }
            else
            {   /* Perform un-buffered write */
                /* If we are writing and our sector range overlaps the whole buffered sectors then
                      Purge the file block buffer if we will overwrite all sectors it contains
                      Flush the file block buffer if we will overwrite a portion of the sectors it contains
                */
                pc_sync_file_buffer(pfinode, start_sector, sector_count, FALSE);
                UPDATE_RUNTIME_STATS(pfinode->my_drive, file_direct_writes, 1)
                UPDATE_RUNTIME_STATS(pfinode->my_drive, file_direct_blocks_written, sector_count)
                pc_discard_file_buffer(pfinode);
                if (!raw_devio_xfer(pfinode->my_drive, start_sector, pdata, sector_count, FALSE, FALSE))
                {
                    if (!save_drive_filio)
                        pfinode->my_drive->drive_flags &= ~DRIVE_FLAGS_FILEIO;
                    if (!get_errno()) /* set errno to IO error unless devio set PEDEVICE */
                        rtfs_set_errno(PEIOERRORWRITE, __FILE__, __LINE__);
                    return(FALSE);
                }
            }
            if (!save_drive_filio)
                pfinode->my_drive->drive_flags &= ~DRIVE_FLAGS_FILEIO;
            /* Set n_bytes_transfered and n_sectors_transfered for advancing our pointers */
            n_bytes_transfered = sector_count<<pfinode->my_drive->drive_info.log2_bytespsec;
            n_sectors_transfered = sector_count;
        }
        else
        {  	/* Perform buffered IO, either because opened in buffered mode or because not sector or erase block aligned */
        	if (pfinode->openflags & OF_BUFFERED)
        	{
        		if (!pc_perform_buffered_fileio(pfinode, start_sector, start_byte_offset, n_todo, pdata, &n_bytes_transfered, &n_sectors_transfered, reading, appending))
				{
                	pc_discard_file_buffer(pfinode);
                	return(FALSE);
				}
			}
			else
			{
				BOOLEAN ret_val;
				/* If we allocated a scratch buffer for the transfer use it */
				if (pscratch_buff)
           			pfinode->pfile_buffer = pscratch_buff;
                if (!pc_perform_buffered_fileio(pfinode, start_sector, start_byte_offset, n_todo, pdata, &n_bytes_transfered, &n_sectors_transfered, reading, appending))
                	ret_val = FALSE;
                else if (!pc_flush_file_buffer(pfinode)) /* write it if it changed */
                    ret_val = FALSE;
				else
					ret_val = TRUE;
				if (pscratch_buff)
				{
					pc_set_file_buffer_dirty(pfinode, FALSE);       /* Defensive */
           			pfinode->pfile_buffer = 0;
           			pc_free_sys_sector(pscratch_buff);
           			pscratch_buff = 0;
				}
				else
               		pc_discard_file_buffer(pfinode);
				if (!ret_val)
					return(FALSE);
			}
        }
        /* Update pointers */
        pdata += n_bytes_transfered;
        start_sector += n_sectors_transfered;


        /* defensive: break out if no bytes or too many bytes have been transferred */
        if (!n_bytes_transfered || n_bytes_transfered > n_todo)
        {
            n_todo = 0;
            /* This should not happen, raise an assert if in diagnostic mode */
            ERTFS_ASSERT(0)
        }
        else
            n_todo -= n_bytes_transfered;

        /* Since we have processed at the first sector make sure byte offset is zero */
        start_byte_offset = 0;
    } /* while (n_todo) */
    return(TRUE);
}


/*  If the finode contains a file buffer
        flush it to disk if it is dirty
        free the buffer
*/
static void pc_discard_file_buffer(FINODE *pfinode)
{
    if (pfinode->pfile_buffer)
    {
        pc_release_file_buffer(pfinode->pfile_buffer);
        pfinode->pfile_buffer = 0;
    }
    pc_set_file_buffer_dirty(pfinode, FALSE);       /* Defensive */

}

/* pc_load_file_buffer(FINODE *pfinode, dword new_blockno)
*
*  If there already is a buffer, flush it to disk if it is dirty and release the buffer
*
*  Allocate a new buffer.
*      If the underlying device driver provides buffer allocation, the allocate and size the buffer using the
*      underlying device driver's IOCTL mechanism
*  If read_buffer is TRUE then read the sector(s) from disk into the buffer
*  If read_buffer is FALSE then zero fill (initialize) the buffer
*
*  Note: new_blockno will be aligned by buffer width when the sector size is > 1
*        for example, if the buffer width is 8 sectors then new_blockno will be
*        0,8,16,24 etc but never 1,2,3.. 17,89, 19.. etc.
*/

BOOLEAN pc_load_file_buffer(FINODE *pfinode, dword new_blockno, BOOLEAN read_buffer)
{
BLKBUFF *pfile_buffer;

    pfile_buffer = pfinode->pfile_buffer;
    if (pfile_buffer)
    {
        if (pfile_buffer->blockno == new_blockno)
        {  /* We already have it */

            UPDATE_RUNTIME_STATS(pfile_buffer->pdrive, file_buff_hits, 1)
            return(TRUE);
        }
        if (!pc_flush_file_buffer(pfinode)) /* Flush the current buffer if it changed */
            return(FALSE);
        /* Interface says discard the buffer if new_blockno is zero */
        if (!new_blockno)
            pc_discard_file_buffer(pfinode);
    }
    else
    {
        if (!new_blockno)   /* Nothing to do */
            return(TRUE);
        /* Allocate a buffer if we don't already have one, otherwise we will use the one we have */
        pfile_buffer = pc_alloc_file_buffer(pfinode);
        if (!pfile_buffer)
            return(FALSE); /* pc_alloc_file_buffer() set errno */
    }
    if (new_blockno)
    {
    dword buffer_size_bytes,buffer_size_sectors;
        /* Get the buffer size */
        buffer_size_bytes = buffer_size_sectors = 0;
        if (!pc_file_buffer_size(pfinode->my_drive, pfile_buffer, &buffer_size_sectors, &buffer_size_bytes))
            return(FALSE);
        pfile_buffer->blockno = new_blockno;
        pfile_buffer->pdrive =  pfinode->my_drive;
        if (read_buffer)
        {
        dword save_drive_filio;
            save_drive_filio = pfile_buffer->pdrive->drive_flags & DRIVE_FLAGS_FILEIO;
            pfile_buffer->pdrive->drive_flags |= DRIVE_FLAGS_FILEIO;
            if (!(raw_devio_xfer(pfile_buffer->pdrive,pfile_buffer->blockno,
                pfile_buffer->data, buffer_size_sectors, FALSE, TRUE) ))
            {
                if (!save_drive_filio)
                    pfile_buffer->pdrive->drive_flags &= ~DRIVE_FLAGS_FILEIO;
                /* set errno to IO error unless devio set PEDEVICE */
                if (!get_errno())
                    rtfs_set_errno(PEIOERRORREAD, __FILE__, __LINE__); /* device read error */
                pc_discard_file_buffer(pfinode);
                return(FALSE);
            }
            if (!save_drive_filio)
                pfile_buffer->pdrive->drive_flags &= ~DRIVE_FLAGS_FILEIO;
            UPDATE_RUNTIME_STATS(pfile_buffer->pdrive, file_buff_reads, 1)
        }
        else
        {
            rtfs_memset(pfile_buffer->data, 0, buffer_size_bytes);
        }
        pfinode->pfile_buffer = pfile_buffer;
    }
    return(TRUE);
}

/* pc_sync_file_buffer(FINODE *pfinode, dword io_start_sector, dword n_sectors, BOOLEAN reading)
*
*   If the finode structure that the file points to contains a file buffer
*       And the sectors in the buffer overlap the sectors to be transfered
*           If reading then:
*               Flush the buffer if it is dirty
*           If writing then:
*               Flush the buffer if it is dirty unless the tranfer will overwrite the whole sector range
*
*/

static BOOLEAN pc_sync_file_buffer(FINODE *pfinode, dword io_start_sector, dword n_sectors, BOOLEAN reading)
{
BLKBUFF *pfile_buffer;

    pfile_buffer = pfinode->pfile_buffer;
    if (pfile_buffer)
    {
        dword buffer_start_sector, buffer_end_sector, io_end_sector;
        dword buffer_size_bytes,buffer_size_sectors;
        /* Get the buffer size */
        buffer_size_bytes = buffer_size_sectors = 0;
        if (!pc_file_buffer_size(pfinode->my_drive, pfile_buffer, &buffer_size_sectors, &buffer_size_bytes))
            return(FALSE);
        buffer_start_sector = pfile_buffer->blockno;
        buffer_end_sector = buffer_start_sector + buffer_size_sectors-1;
        io_end_sector = io_start_sector + n_sectors-1;
/* Bug fix August 2010. Comment these lines out, they were causing data that should have been flushed to not be flushed. */
    		/* Quick check to test if no overlap */
/*        if (buffer_start_sector > io_end_sector ||  buffer_end_sector < io_start_sector) */
/*            return(TRUE);																   */
        /* If we are writing and our sector range overlaps the whole buffer then clear
           the dirty bit so it is not flushed and then completely overwritten again */
        if (!reading)
        {
            if (io_start_sector <= buffer_start_sector && io_end_sector >=  buffer_end_sector)
                pc_set_file_buffer_dirty(pfinode, FALSE);
        }
        /* flush the current buffer if it is marked dirty */
        return(pc_flush_file_buffer(pfinode));
    }
    return(TRUE);
}

/*
    File IO operations call this routine to see if they should use buffered IO.
    Given a start sector, sector offset byte count, and IO direction,
    decide if the operation should be buffered or not.
    If the IO is not to be buffered then return the number of sectors to transfer in the
    variable pointed to by transfer  dword *psector_count
*/

static BOOLEAN pc_check_unbuffered(DDRIVE *pdr, dword buffered_start_sector, dword buffer_size_bytes, dword buffer_size_sectors, dword start_sector, dword start_byte_offset, dword n_bytes, dword *psector_count, BOOLEAN reading)
{
dword sector_size_bytes;

    *psector_count = 0;
    /* If not sector aligned we must perform buffered IO */
    if (start_byte_offset)
        return(FALSE);

    sector_size_bytes = pdr->drive_info.bytespsector;

    if (reading)
    {
        dword buffered_end_sector;
        /* perform buffered IO if we are reading and the start sector is already buffered */
        buffered_end_sector = buffered_start_sector + buffer_size_sectors-1;
        if (buffered_start_sector <= start_sector && buffered_end_sector >= start_sector)
            return(FALSE);
    }
    /* Use the same algorithm if we are reading or if we are writing and the
       read and write buffer boundary requirements are the same */
    if (reading || sector_size_bytes == buffer_size_bytes)
    {
        /* perform buffered IO if < sector size */
        if (n_bytes < sector_size_bytes)
            return(FALSE);
        else
        { /* instruct IO layer to perform un-buffered IO
             calculate the return count of raw blocks to transfer */
            *psector_count = n_bytes>>pdr->drive_info.log2_bytespsec;
            return(TRUE);
        }
    }
    else
    {
    /* Use the same algorithm if we are writing or if we are reading and the the buffer size is > a sector */
        dword buffer_mask;
        /* Create a mask to calculate the start sector of our buffer */
        buffer_mask = buffer_size_sectors - 1;
        /* Use buffered io if the start_sector is not on an eraseblock boundary or the byte count is < buffer size */
        if ((start_sector & buffer_mask) || n_bytes < buffer_size_bytes)
            return(FALSE);
        else
        {   /* Return the number of sectors to write, the returned count will be an even multiple of erase block size */
            *psector_count = n_bytes>>pdr->drive_info.log2_bytespsec;
            return(TRUE);
        }
    }
}


/*
    File IO operations call this routine to see if they should use buffered IO.
    Given a start sector, sector offset byte count, and IO direction,
    decide if the operation should be buffered or not.
    If the IO is not to be buffered then return the number of sectors to transfer in the
    variable pointed to by transfer  dword *psector_count
*/
static BOOLEAN pc_perform_buffered_fileio(FINODE *pfinode, dword start_sector, dword start_byte_offset, dword n_bytes, byte *pdata, dword *n_bytes_transfered, dword *n_sectors_transfered, BOOLEAN reading, BOOLEAN appending)
{
dword buffer_size_bytes, buffer_size_sectors;
dword sector_base_offset_bytes, buffer_sector_boundary;
DDRIVE *pdr;

    *n_sectors_transfered = 0;
    *n_bytes_transfered = 0;
    pdr = pfinode->my_drive;

    /* The caller assures us that pfinode->pfile_buffer contains a buffer pointer */
    if (!pc_file_buffer_size(pdr, pfinode->pfile_buffer, &buffer_size_sectors, &buffer_size_bytes))
        return(FALSE);

    /* Set page boundary defaults containing 1 sector */
    buffer_sector_boundary = start_sector;
    sector_base_offset_bytes = 0;

    if (buffer_size_sectors != 1)
    {
    dword ltemp, mask;
    /* Recalculate page boundaries of the buffer if we are not using the default configuration */
        /* Find the nearest sector number divisible by the buffer width, this is the first block in our buffer */
        ltemp = buffer_size_sectors - 1;
        mask = ~ltemp;
        buffer_sector_boundary = start_sector & mask;

        /* calculate the number of bytes offset to the sector we want (will be zero if they are aligned)*/
        ltemp = start_sector - buffer_sector_boundary;
        sector_base_offset_bytes = ltemp<<pdr->drive_info.log2_bytespsec;
    }
    /* flush the current buffer if it is not what we want */
    if (pfinode->pfile_buffer->blockno != buffer_sector_boundary)
    {
        if (!pc_flush_file_buffer(pfinode))
            return(FALSE);
    }
    /* Initialize block number fields in the buffer, read the block into the buffer if needed */
    {
        BOOLEAN do_read;
        /* Don't read the buffer if we are reading in unbuffered mode and have erase blocks. We will read only what we need */
        if (reading && (pfinode->openflags & OF_BUFFERED) == 0 && buffer_size_sectors != 1)
            do_read = FALSE;
        /* initialize the buffer if we are appending to the file and on a buffer boundary
           we do not need the data and it may not be initialized  */
        else if (appending && buffer_sector_boundary == start_sector && start_byte_offset==0)
            do_read = FALSE;
        else  /* not appending read the buffer from the drive */
            do_read = TRUE;
        if (!pc_load_file_buffer(pfinode, buffer_sector_boundary, do_read))
            return(FALSE);
    }
	if (reading && (pfinode->openflags & OF_BUFFERED) == 0 && buffer_size_sectors != 1)
    { /* Read only the sectors we need because the buffer will be discarded */
    dword bytes_to_read,sectors_to_read,sector_offset;

		bytes_to_read = n_bytes + pdr->pmedia_info->sector_size_bytes -1;
		sectors_to_read = bytes_to_read>>pdr->drive_info.log2_bytespsec;
        sector_offset = start_sector - buffer_sector_boundary;
		if (sectors_to_read + sector_offset > buffer_size_sectors)
		{
			sectors_to_read = buffer_size_sectors - sector_offset;
		}
        {
        dword save_drive_filio;
		BLKBUFF *pfile_buffer;
			pfile_buffer =  pfinode->pfile_buffer;
            save_drive_filio = pfile_buffer->pdrive->drive_flags & DRIVE_FLAGS_FILEIO;
            pfile_buffer->pdrive->drive_flags |= DRIVE_FLAGS_FILEIO;
            if (!(raw_devio_xfer(pfinode->my_drive,start_sector,
                pfile_buffer->data+sector_base_offset_bytes, sectors_to_read, FALSE, TRUE) ))
            {
                if (!save_drive_filio)
                    pfile_buffer->pdrive->drive_flags &= ~DRIVE_FLAGS_FILEIO;
                /* set errno to IO error unless devio set PEDEVICE */
                if (!get_errno())
                    rtfs_set_errno(PEIOERRORREAD, __FILE__, __LINE__); /* device read error */
                return(FALSE);
            }
            if (!save_drive_filio)
                pfile_buffer->pdrive->drive_flags &= ~DRIVE_FLAGS_FILEIO;
            UPDATE_RUNTIME_STATS(pfile_buffer->pdrive, file_buff_reads, 1)
        }
	}
    /* Now transfer data in or out of the bufer */
    {
    dword ltemp, byte_offset_in_buffer, bytes_to_copy;
        /* Calculate the offset in the buffer and number of bytes to copy */
        byte_offset_in_buffer = sector_base_offset_bytes +  start_byte_offset;
        /* How many bytes are available */
        bytes_to_copy = buffer_size_bytes - byte_offset_in_buffer;
        if (bytes_to_copy > n_bytes)
            bytes_to_copy = n_bytes;
        if (reading)  /* Copy source data to the local buffer   */
            copybuff(pdata, pfinode->pfile_buffer->data+byte_offset_in_buffer, (int)bytes_to_copy);
        else
        {   /* Merge the data and mark it dirty */
            copybuff(pfinode->pfile_buffer->data+byte_offset_in_buffer, pdata, (int)bytes_to_copy);
            pc_set_file_buffer_dirty(pfinode, TRUE);
        }
        /* Calculate sectors and bytes processed */
        *n_bytes_transfered = bytes_to_copy;

        /* Add offset into first sector + bytes actually copy and divide by sector size
           to calculate the number of sectors processed */
        ltemp = start_byte_offset + bytes_to_copy;
        /* divide by sector size */
        *n_sectors_transfered = ltemp>>pdr->drive_info.log2_bytespsec;

        return(TRUE);
    }
}

/* pc_file_buffer_size(), return size of the buffer in bytes and sector */
static BOOLEAN pc_file_buffer_size(DDRIVE *pdr, BLKBUFF *pfile_buffer, dword *psize_sectors, dword *psize_bytes)
{
    *psize_bytes = pfile_buffer->data_size_bytes;     /* Size of the pointer in data */
    *psize_sectors = *psize_bytes>>pdr->drive_info.log2_bytespsec;
    return(TRUE);
}

/* pc_alloc_file_buffer() allocate a buffer for file IO. */
static BLKBUFF *pc_alloc_file_buffer(FINODE *pfinode)
{
    return(RTFS_DEVI_alloc_filebuffer(pfinode->my_drive));
}
/* pc_release_file_buffer(), release buffers allocated by the pc_alloc_file_buffer() function
   called by file buffering layer and by pc_memory_finode() when it releases a finode that
   owns a file buffer.
  */
void pc_release_file_buffer(BLKBUFF *pblk)
{
    RTFS_DEVI_release_filebuffer(pblk);
}
