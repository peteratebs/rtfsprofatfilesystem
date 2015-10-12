/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* RTLOWL.C - Low level functions that don't directly manipulate the fat.

    Routines in this file include:

    pc_i_dskopen        -   Mount a drive if you can
    pc_read_partition_table
                        -  Load a drive structure with partition info
    pc_gblk0            -   Read block zero and set up internal structures.
    pc_drno2dr          -   Convert a drive number to a drive structure.
    pc_dskfree          -   Free resources associated with a drive.
    pc_sec2cluster      -   Convert a sector number to a cluster value.
    pc_sec2index        -   Convert a sector number to a cluster offset.
    pc_cl2sector        -   Convert a cluster value to a sector number.
    pc_pfinode_cluster  -   Assign a cluster value to a finode
    pc_finode_cluster   -   Get the cluster value from a finode
*/

#include "rtfs.h"


/******************************************************************************
    PC_I_DSKOPEN -  Open a disk for business.

 Description
    Called by lower level code in chkmedia to open the disk


 Returns
    Returns TRUE if the disk was successfully initialized.
****************************************************************************/

int pc_log_base_2(word n)                                   /*__fn__*/
{
int log;

    log = 0;
    if (n <= 1)
        return(log);

    while(n)
    {
        log += 1;
        n >>= 1;
    }
    return((int)(log-1));
}

BOOLEAN  pc_auto_dskopen(DDRIVE *pdr)
{
    rtfs_clear_errno();  /* Clear errno to be safe */
    if (pc_i_dskopen(pdr->driveno,FALSE))
        return(TRUE);
    else
    {
        return(FALSE);
    }
}

/*
* Note: This routine is called with the drive already locked so
*   in several cases there is no need for critical section code handling
*   This is a helper function for pc_i_dskopen()
*/

void pc_drive_scaleto_blocksize(DDRIVE *pdr, int bytspsector) /* :LB: Adjust driveinfo block scale factors */
{
    pdr->drive_info.bytespsector = bytspsector; /* bytes/sector */
    pdr->drive_info.log2_bytespsec = (int)pc_log_base_2((word)pdr->drive_info.bytespsector);
    pdr->drive_info.bytemasksec = (dword) pdr->drive_info.bytespsector; /* And to get byte offset in a sector */
    pdr->drive_info.bytemasksec &= 0xffff;
    pdr->drive_info.bytemasksec -= 1;
    pdr->drive_info.inopblock   = pdr->drive_info.bytespsector/32;
    pdr->drive_info.blockspsec  = pdr->drive_info.bytespsector/512; /* Not sector dependant. How many 512 byte blocks in a sector */
    pdr->drive_info.clpfblock32 = pdr->drive_info.bytespsector/4;
    pdr->drive_info.cl32maskblock = (dword) pdr->drive_info.clpfblock32; /* And to get cluster offset in a sector */
    pdr->drive_info.cl32maskblock &= 0xffff;
    pdr->drive_info.cl32maskblock -= 1;
    pdr->drive_info.clpfblock16 = pdr->drive_info.bytespsector/2;
    pdr->drive_info.cl16maskblock = (dword) pdr->drive_info.clpfblock16; /* And to get cluster offset in a sector */
    pdr->drive_info.cl16maskblock &= 0xffff;
    pdr->drive_info.cl16maskblock -= 1;

    /* save away log of sectors per alloc   */
    pdr->drive_info.log2_secpalloc = (int)pc_log_base_2((word)pdr->drive_info.secpalloc);
    pdr->drive_info.log2_bytespalloc = pdr->drive_info.log2_secpalloc+pdr->drive_info.log2_bytespsec;
    pdr->drive_info.bytespcluster = (int) (pdr->drive_info.bytespsector * pdr->drive_info.secpalloc);
    pdr->drive_info.byte_into_cl_mask = (dword) pdr->drive_info.bytespcluster;
    pdr->drive_info.byte_into_cl_mask -= 1L;
}

/* Note is_async is not used for Pro */
BOOLEAN pc_i_dskopen(int driveno, BOOLEAN is_async)                      /*__fn__*/
{
    DDRIVE *pdr;
    struct pcblk0 bl0;

    RTFS_ARGSUSED_INT((int) is_async); /* Not used for Pro */

    if (!prtfs_cfg)
    {   /* Failed: pc_ertfs__init() was not called, don't set errno not have been called   */
        return (FALSE);
    }

    /* Check drive number   */
    if (!pc_validate_driveno(driveno))
    {
invalid_drive:
        rtfs_set_errno(PEINVALIDDRIVEID, __FILE__, __LINE__);
        return(FALSE);
    }
    pdr = pc_drno_to_drive_struct(driveno);
    if (!pdr)
    	goto invalid_drive;

    /* If the device driver does not configure the drive structure
       then verify that the drive was configured by the user, keying off fat buffer array */
#if (INCLUDE_RTFS_PROPLUS) /* ProPlus Can't start a mount while in an async operation Pro always returns idle */
    if (pdr->drive_state.drive_async_state != DRV_ASYNC_IDLE)
    {
        rtfs_set_errno(PEEINPROGRESS, __FILE__, __LINE__);
        return(FALSE);
    }
#endif
    /* Do not do anything on reopens   */
    if (chk_mount_valid(pdr))
    {
        return(TRUE);
    }
    else
    {
        if (!RTFS_DEVI_process_mount_parms(pdr))
            return(FALSE);
        /* Zero working variables, leave configuration values unchanged */
        OS_CLAIM_FSCRITICAL()
        {
			dword saved_operating_flags;
			/* Preserve a few bits of the operating flags these are used by Failsafe fs_api_enable() fs_api_disable() to pass parameters to the remount.	*/
			saved_operating_flags = pdr->drive_info.drive_operating_flags & (DRVPOL_DISABLE_AUTOFAILSAFE|DRVOP_FS_CLEAR_ON_MOUNT|DRVOP_FS_START_ON_MOUNT|DRVOP_FS_DISABLE_ON_MOUNT);
            rtfs_memset(&pdr->drive_info, (byte) 0,  sizeof(pdr->drive_info));
			pdr->drive_info.drive_operating_flags |= saved_operating_flags;
            rtfs_memset(&pdr->drive_state, (byte) 0, sizeof(pdr->drive_state));
            /*  Note: not clearing pdr->drive_rtstats.. they can be cleared explicitly */
            pdr->driveno = (word)driveno;
        }
        OS_RELEASE_FSCRITICAL()
    }

    set_mount_valid(pdr);
    /* The partition table was read when the device was inserted */
    /* We saved the base and size when the device was mounted. */
    pdr->drive_info.partition_base = pdr->dyn_partition_base;
    pdr->drive_info.partition_size = pdr->dyn_partition_size;
	pdr->drive_info.partition_type = pdr->dyn_partition_type;

    rtfs_app_callback(RTFS_CBA_INFO_MOUNT_STARTED, pdr->driveno, 0, 0, 0);
    /* Read block 0 (BPB)  */

    if (!pc_gblk0(pdr, &bl0 ))
    {
        /* pc_gblk0 set errno */
 return_error:
    	rtfs_app_callback(RTFS_CBA_INFO_MOUNT_FAILED, pdr->driveno, 0, 0, 0);
        clear_mount_valid(pdr);
		/* Clear temporary remount parms because the mount failed */
        pdr->drive_info.drive_operating_flags &= ~(DRVOP_FS_START_ON_MOUNT|DRVOP_FS_DISABLE_ON_MOUNT|DRVPOL_DISABLE_AUTOFAILSAFE|DRVOP_FS_CLEAR_ON_MOUNT);
        return(FALSE);
    }

#if (INCLUDE_EXFATORFAT64)
	/* If gblk0 detected an EXFAT volume, open it */
	if (ISEXFATORFAT64(pdr))
	{
    	if (!rtexfat_i_dskopen(pdr))
    		goto return_error;
    	return(TRUE);
	}
#endif
    /* Verify that we have a good dos formatted disk   */
#if (INCLUDE_WINDEV)
    if ( (bl0.jump == (byte) 0xE8) )
	{
       	rtfs_print_one_string((byte *)"Using Hack E8 BPB signature so volume it is writeable", PRFLG_NL);
    	bl0.jump = 0xE9;
	}
#endif
    if ( (bl0.jump != (byte) 0xE9) && (bl0.jump !=(byte) 0xEB) )
    {
        rtfs_set_errno(PEINVALIDBPB, __FILE__, __LINE__);  /* pc_i_dskopen Unkown values in Bios Parameter block */
        goto return_error;
    }

    /* set up the drive structure from block 0   */
    pdr->drive_info.secpalloc = bl0.secpalloc; /* sectors / cluster */
    pdr->drive_info.numroot = bl0.numroot; /* Maximum number of root entries */
    pdr->drive_info.numsecs = (dword) bl0.numsecs;    /* Total sectors on the disk */
    pdr->drive_info.mediadesc =    bl0.mediadesc;  /* Media descriptor byte */
    pdr->drive_info.secreserved = bl0.secreserved; /* sectors reserved */
    pdr->drive_info.secptrk    = bl0.secptrk;  /* sectors per track */
    pdr->drive_info.numhead    = bl0.numhead;  /* number of heads */
    {
    dword ltemp;
        pdr->drive_info.numhide    =bl0.numhide;   /* # hidden sectors */
        ltemp = bl0.numhide2;
        ltemp <<= 16;
        pdr->drive_info.numhide    |= ltemp;
    }

    /* Check secpalloc field, zero would be a disaster and we know that > 64 is invalid */
    /* Check bytes per sector.. make sure <= MAX_BLOCK_SIZE and Non-zero */
    {
    int sector_size_bytes;
        sector_size_bytes = (int) pc_get_media_sector_size(pdr);
        if(!pdr->drive_info.secpalloc || pdr->drive_info.secpalloc > 64 || !bl0.bytspsector || (int) bl0.bytspsector != sector_size_bytes) // <NAND>
        {
            rtfs_set_errno(PEINVALIDBPB, __FILE__, __LINE__);  /* pc_i_dskopen Unkown values in Bios Parameter block */
            goto return_error;
        }
    }
    /* Set up sector size dependent values */
    pc_drive_scaleto_blocksize(pdr, (int) bl0.bytspsector);

    /* use a private buffer pool per drive */
    pdr->drive_state.pbuffcntxt = &pdr->_buffctxt;

    copybuff(pdr->drive_info.volume_label, &bl0.vollabel[0], 11);
    pdr->drive_info.volume_label[11] = 0;
    pdr->drive_info.volume_serialno = bl0.volid;

/* Check if running on a DOS (4.0) huge partition                           */
    /* If traditional total # sectors is zero, use value in extended BPB    */
    if (pdr->drive_info.numsecs == 0L)
        pdr->drive_info.numsecs = bl0.numsecs2;                            /* (4.0) */

    {
        BOOLEAN ret_val;
        ret_val = FALSE;

        if (pdr->drive_info.numroot==0) /* Drive is FAT32 */
        {
#if (INCLUDE_FAT32)
            ret_val = pc_init_drv_fat_info32(pdr, &bl0);
#endif
        }
        else
            ret_val = pc_init_drv_fat_info16(pdr, &bl0);
        if (!ret_val)
            goto return_error;
    }
    /* Initialize the fat block buffer pool */
    pc_free_all_fat_buffers(pdr);

#if (INCLUDE_RTFS_FREEMANAGER)
    /* Attach region manager functions if enabled */
    if (!free_manager_attach(pdr))
        goto return_error;
#endif
#if (INCLUDE_RTFS_PROPLUS)
	/* Tell the driver to initialize the cache for this volume if it has one */
    pdr->pmedia_info->device_ioctl(pdr->pmedia_info->devhandle, (void *) pdr, RTFS_IOCTL_INITCACHE, 0 , 0);
#endif

#if (INCLUDE_ASYNCRONOUS_API) /* ProPlus check if async mount was requested */
    if (pdr->drive_info.fasize == 3)
        is_async = FALSE; /* Async mount not supported for FAT12 */
    if (is_async)
    {
        pdr->drive_info.known_free_clusters = 0;
        fatop_page_start_check_freespace(pdr);
        set_mount_valid(pdr);       /* Set bits valid to alow async operations to complete */
        return(TRUE);
    }
#endif
    if (!fatop_check_freespace(pdr))
        goto return_error;

    pc_i_dskopen_complete(pdr); /* Set the drive open */
#if (INCLUDE_FAILSAFE_RUNTIME) /* Call failsafe autorestore and open routines */
    if (prtfs_cfg->pfailsafe && !prtfs_cfg->pfailsafe->fs_failsafe_dskopen(pdr) )
        goto return_error;
#endif
    rtfs_app_callback(RTFS_CBA_INFO_MOUNT_COMPLETE, pdr->driveno, 0, 0, 0);
    return(TRUE);
}

/* Called when a mount succeeds, seperate function because can be the result of async
   completion. */
void pc_i_dskopen_complete(DDRIVE *pdr)
{
    set_mount_valid(pdr);
    /* Save Unique id for this mount */
    OS_CLAIM_FSCRITICAL()
    prtfs_cfg->drive_opencounter += 1;
    OS_RELEASE_FSCRITICAL()
    pdr->drive_opencounter = prtfs_cfg->drive_opencounter;

}

/******************************************************************************
    pc_read_partition_table() -  Load a drive structure with partition info

 Description
    Read the partition table from a disk. If one is found then check the
    entry in the table that is specified by pdr->partition_number. If the
    entry is valid then load the fields
        pdr->partition_base,pdr->partition_size and pdr->partition_type;

 Returns
    The following values.

    >= 0                      FAT partitions found
    READ_PARTITION_ERR        Internal error (could not allocate buffers ?)
    READ_PARTITION_NO_TABLE   No partition table found
    READ_PARTITION_IOERROR    Device IO error

****************************************************************************/
#if (INCLUDE_WINDEV)
void win_dev_write_enable(void);
#endif

int pc_read_partition_table(DDRIVE *pdr, struct mbr_entry_specification *pspec)
{
    PTABLE *ppart;
    BLKBUFF *buf;
    BLKBUFF bbuf_scratch;   /* Used by pc_sys_sector build a buffer structure with core from the driver level */
    byte *pbuf;
    int j,partition_number,partition_count,last_valid_index_in_mbr,extended_index_in_mbr,loop_count;
    BOOLEAN in_extended;
    int ret_val;
    dword mbr_sector_location;
    struct mbr_entry_specification _lspecs[RTFS_MAX_PARTITIONS];

    /* Grab some working space pass pdr to get a sector width  */
    buf = pc_sys_sector(pdr, &bbuf_scratch);
    if (!buf)
        return(READ_PARTITION_ERR);

    mbr_sector_location = 0;
    partition_count = 0;
    in_extended = FALSE;
    extended_index_in_mbr = last_valid_index_in_mbr = 0;
    ret_val = 0;
    for(loop_count = 0;loop_count < RTFS_MAX_PARTITIONS;loop_count++)
    {
        /* Read block zero   */
        if (!raw_devio_xfer(pdr, mbr_sector_location, buf->data , 1, TRUE, TRUE))
        {
            /* Failed reading Master boot record */
            rtfs_set_errno(PEIOERRORREADMBR, __FILE__, __LINE__);
            ret_val = READ_PARTITION_IOERROR;
            goto done;
        }
        /* Copy the table to a word alligned buffer   */
        pbuf = buf->data;
        pbuf += 0x1be;          /* The info starts at buf[1be] */
        /* Don't use sizeof here since the structure does not pack to exact size */
        copybuff(buf->data, pbuf, 0x42);
        ppart = (PTABLE *) buf->data;
/*
        July 2012 - revove this test, it is now done inside the device driver
#if (INCLUDE_WINDEV)
        if (to_WORD((byte *) &ppart->signature)  ==  0xAA66)
		{
        	rtfs_print_one_string((byte *)"Using Hack AA66 signature so volume it is writeable", PRFLG_NL);
        	fr_WORD((byte *) &ppart->signature, 0xAA55);
		}
#endif
*/
        if (to_WORD((byte *) &ppart->signature)  !=  0xAA55)
        {
            if (!in_extended)
            {
                ret_val = READ_PARTITION_NO_TABLE;
                goto done;
            }
            break;
        }
        if (!in_extended)
        {
            for (j = 0; j < 4; j++)
            {

				if (pc_validate_partition_type(ppart->ents[j].p_typ))
                {
                    /* Get the relative start and size   */
                    _lspecs[partition_count].partition_start = to_DWORD ((byte *) &ppart->ents[j].r_sec);
                    _lspecs[partition_count].partition_size  = to_DWORD ((byte *) &ppart->ents[j].p_size);
                    _lspecs[partition_count].partition_type  = ppart->ents[j].p_typ;
                    _lspecs[partition_count].partition_boot  = ppart->ents[j].boot;   /* 0x80 for bootable */
                    last_valid_index_in_mbr = j;
                    if (partition_count < RTFS_MAX_PARTITIONS-1) partition_count += 1;
                }
#if (SUPPORT_EXTENDED_PARTITIONS)
                else if (ppart->ents[j].p_typ == 0x5 || ppart->ents[j].p_typ == 0xF) /* extended partition */
                {
                    in_extended = TRUE;
                    extended_index_in_mbr = j;
                    mbr_sector_location = to_DWORD ((byte *) &ppart->ents[j].r_sec);
                }
#endif
            }
        }
#if (SUPPORT_EXTENDED_PARTITIONS)
        else
        {
            /* the partition is inside an extended partition */
            if (pc_validate_partition_type(ppart->ents[0].p_typ))
            {
                _lspecs[partition_count].partition_start = to_DWORD ((byte *) &ppart->ents[0].r_sec);
                _lspecs[partition_count].partition_size  = to_DWORD ((byte *) &ppart->ents[0].p_size);
                _lspecs[partition_count].partition_type  = ppart->ents[0].p_typ;
                _lspecs[partition_count].partition_boot  = ppart->ents[0].boot;   /* 0x80 for bootable */
                if (partition_count < RTFS_MAX_PARTITIONS-1) partition_count += 1;
            }
            /* Check if there are more extended partitions */
            if (ppart->ents[1].p_typ == 0x5 || ppart->ents[1].p_typ == 0xF)
            {
                mbr_sector_location = mbr_sector_location + to_DWORD ((byte *) &ppart->ents[1].r_sec);
            }
            else
                break;
        }
#endif
        if (!in_extended)
            break;
    }
    partition_number = 0;
    for (j = 0; j < partition_count; j++)
    {
        /* If we processed extended partitions and there are partitions in the mbr beyond them
           the extended partitons are next numerically but the records start after the primary records */
#if (SUPPORT_EXTENDED_PARTITIONS)
        if (in_extended && last_valid_index_in_mbr > extended_index_in_mbr)
        {
            if (partition_number == extended_index_in_mbr)   /* Start of extended, skip primaries */
                partition_number += last_valid_index_in_mbr - extended_index_in_mbr;
            else if (partition_number == partition_count-1)  /* End of extended, skip bak to primaries */
                partition_number =  extended_index_in_mbr;
        }
#endif
        pspec->partition_start =   _lspecs[partition_number].partition_start;
        pspec->partition_size  =   _lspecs[partition_number].partition_size;
        pspec->partition_type  =   _lspecs[partition_number].partition_type;
        pspec->partition_boot  =   _lspecs[partition_number].partition_boot;
        partition_number++;
        pspec++;
    }
    ret_val = partition_count;
    /* Retain legacy behavior of setting the partiton values if the drive matched */
    if (partition_count >= pdr->partition_number)
    {   /* Get the relative start and size   */
        pdr->drive_info.partition_base =  _lspecs[pdr->partition_number].partition_start;
        pdr->drive_info.partition_size =  _lspecs[pdr->partition_number].partition_size;
        pdr->drive_info.partition_type =  _lspecs[pdr->partition_number].partition_type;
    }
done:
    pc_free_sys_sector(buf);
    return(ret_val);
}

/****************************************************************************
    PC_GBLK0 -  Read block 0 and load values into a a structure

 Description
    Given a valid drive number, read block zero and convert
    its contents from intel to native byte order.

 Returns
    Returns TRUE if all went well.

****************************************************************************/


/* read block zero   */
BOOLEAN pc_gblk0(DDRIVE *pdr, struct pcblk0 *pbl0)                 /*__fn__*/
{
    BLKBUFF *buf;
    BLKBUFF bbuf_scratch;   /* Used by pc_sys_sector build a buffer structure with core from the driver level */
    byte *b;
	BOOLEAN ret_val;

	    /* Zero fill pbl0 so we do not get any surprises */
    rtfs_memset(pbl0, (byte) 0,sizeof(struct pcblk0));
    /* Grab a buffer to play with use pc_sys_sector() because the volume is not mounted  */
    buf = pc_sys_sector(pdr, &bbuf_scratch);
    if (!buf)
    {
        rtfs_set_errno(PERESOURCESCRATCHBLOCK, __FILE__, __LINE__); /* pc_gblk0 couldn't allocate a buffer */
        return(FALSE);
    }
	ret_val = FALSE;
    b = buf->data;      /* Now we do not have to use the stack */

    /* get 1 block starting at 0 from driveno   */
    /* READ                                     */
    if (!raw_devio_xfer(pdr, 0L ,b,1, FALSE, TRUE))
    {
        rtfs_set_errno(PEIOERRORREADBPB, __FILE__, __LINE__); /* pc_gblk0 failed reading Bios Parameter block */
		goto done;
    }

    /* Now load the structure from the buffer   */

    /* The valid bpb test passes for NTFS. So identify NTFS and force jump to zero so the bpb test fails. */
    /* Check for EXFAT and NTFS */
    if (*(b + 0x3) == (byte)'E' && *(b + 0x4) == (byte)'X' && *(b + 0x5) == (byte)'F' && *(b + 0x6) == (byte)'A' && *(b + 0x7) == (byte)'T')
	{
#if (INCLUDE_EXFATORFAT64)
		ret_val = rtexfat_gblk0(pdr, pbl0,b);
#else
		{
			pbl0->jump = 0; /* Set jmp to zero so FAT bpb test will fail */
			ret_val = TRUE;
		}
#endif
		goto done;
	}
    else if (*(b + 0x3) == (byte)'N' && *(b + 0x4) == (byte)'T' && *(b + 0x5) == (byte)'F' && *(b + 0x6) == (byte)'S')
		pbl0->jump = 0; /* Set jmp to zero so FAT bpb test will fail */
	else
    	pbl0->jump = b[0];
    if (pbl0->jump == 0)
	{
		ret_val = TRUE; /* It is not an error, just not a FAT bpb */
		goto done;
	}
    copybuff( &pbl0->oemname[0],b+3,8);
    pbl0->oemname[8] = 0;
    pbl0->secpalloc = b[0xd];
    pbl0->numfats = b[0x10];
    pbl0->mediadesc = b[0x15];
    pbl0->physdrv = b[0x24];            /* Physical Drive No. (4.0) */
    pbl0->xtbootsig = b[0x26];      /* Extended signt 29H if 4.0 stuf valid */
    /* BUG FIX 12-1-99 - Add KS_LITTLE_ODD_PTR_OK flag to split between
       big endian and little endian system. The top section works on little
       endian systems that do not require even alligned word accesses like
       the x86 but for example on Little endian ARM systems these assignments
       derefrencing a pointer to word at an odd address which gives bad data .*/
    pbl0->bytspsector = to_WORD(b+0xb); /*X*/
    pbl0->secreserved = to_WORD(b+0xe); /*X*/
    pbl0->numroot   = to_WORD(b+0x11); /*X*/
    pbl0->numsecs   = to_WORD(b+0x13); /*X*/
    pbl0->secpfat   = to_WORD(b+0x16); /*X*/
    pbl0->secptrk   = to_WORD(b+0x18); /*X*/
    pbl0->numhead   = to_WORD(b+0x1a); /*X*/
    pbl0->numhide   = to_WORD(b+0x1c); /*X*/
    pbl0->numhide2  = to_WORD(b+0x1e); /*X*/
    pbl0->numsecs2  = to_DWORD(b+0x20);/*X*/ /* # secs if > 32M (4.0) */
    pbl0->volid     = to_DWORD(b+0x27);/*X*/ /* Unique number per volume (4.0) */
    copybuff( &pbl0->vollabel[0],b+0x2b,11); /* Volume label (4.0) */

    if (pbl0->numroot == 0)
        pbl0->fasize = 8;
	else
	{ /* Check the bpb file sys type field. If it was initialized by format use that to determine FAT type */
		if (*(b + 0x36) == (byte)'F' && *(b + 0x37) == (byte)'A' && *(b + 0x38) == (byte)'T' && *(b + 0x39) == (byte)'1')
		{
			if (*(b + 0x3A) == (byte)'2')
				pbl0->fasize = 3;
			else if (*(b + 0x3A) == (byte)'6')
				pbl0->fasize = 4;
		}
	}

    if (pbl0->numroot == 0 && !pc_gblk0_32(pdr, pbl0, b))
    {
        rtfs_set_errno(PEIOERRORREADINFO32, __FILE__, __LINE__); /* pc_gblk0_32 failed reading Bios Parameter block */
		goto done;
    }
	ret_val = TRUE;
done:
    pc_free_sys_sector(buf);
    return(ret_val);
}


/****************************************************************************
    PC_DRNO2DR -  Convert a drive number to a pointer to DDRIVE

 Description
    Given a drive number look up the DDRIVE structure associated with it.

 Returns
    Returns NULL if driveno is not an open drive.

****************************************************************************/

DDRIVE *pc_drno_to_drive_struct(int driveno)                                    /*__fn__*/
{
DDRIVE  *pdr;

    pdr = 0;
    /* Check drive number   */
    if (pc_validate_driveno(driveno))
    {
        pdr = prtfs_cfg->drno_to_dr_map[driveno];
    }
    return(pdr);
}

DDRIVE  *pc_drno2dr(int driveno)                                    /*__fn__*/
{
DDRIVE  *pdr;
DDRIVE  *pretval;

    pdr = pc_drno_to_drive_struct(driveno);
    pretval = 0;

    OS_CLAIM_FSCRITICAL()
    /* Check drive number   */
    if (pdr)
    {
        if (chk_mount_valid(pdr))
        {
            pretval = pdr;
        }
    }
    OS_RELEASE_FSCRITICAL()
    return(pretval);
}

/***************************************************************************
    PC_DSKFREE -  Deallocate all core associated with a disk structure

 Description
    Given a valid drive number. If the drive open count goes to zero, free the
    file allocation table and the block zero information associated with the
    drive. If unconditional is true, ignore the open count and release the
    drive.
    If open count reaches zero or unconditional, all future accesses to
    driveno will fail until re-opened.

 Returns
    Returns FALSE if driveno is not an open drive.

****************************************************************************/


/* free up all core associated with the drive
    called by close. A drive restart would consist of
    pc_dskfree(driveno, TRUE), pc_dskopen() */
BOOLEAN pc_dskfree(int driveno)                          /*__fn__*/
{
    DDRIVE *pdr;

    /* Note this will fail unless mount_valid is true */
    pdr = pc_drno2dr(driveno);
    if (!pdr)
    {
        return(FALSE);
    }

    if (chk_mount_valid(pdr))
    {
#if (INCLUDE_EXFATORFAT64)
		if (ISEXFATORFAT64(pdr))
			pc_release_exfat_buffers(pdr);
#endif
        /* Free the current working directory for this drive for all users   */
        pc_free_all_users(driveno);
        /* Free all files, finodes & blocks associated with the drive   */
        pc_free_all_fil(pdr);
        pc_free_all_i(pdr);
        /* Free all drobj structures that have not yet been accessed */
        pc_free_all_drobj(pdr);
        pc_free_all_blk(pdr);
        /* No need to free fat blocks because they are local to the drive */
        /* release the free region caches if enabled (no-op under Pro) */
        fatop_close_driver(pdr);
    }
    clear_mount_valid(pdr);
    clear_mount_abort(pdr);
#if (INCLUDE_RTFS_PROPLUS) /* Set drive state idle after mount completion */
    pdr->drive_state.drive_async_state = DRV_ASYNC_IDLE;
#endif

    return (TRUE);
}



/****************************************************************************
    PC_SEC2CLUSTER - Convert a block number to its cluster representation.

 Description
    Convert blockno to its cluster representation if it is in cluster space.

 Returns
    Returns 0 if the block is not in cluster space, else returns the
    cluster number associated with block.

****************************************************************************/

/* Cluster<->sector conversion routines       */
/* Convert sector to cluster. 0 == s error    */
dword pc_sec2cluster(DDRIVE *pdrive, dword blockno)              /*__fn__*/
{
    dword ltemp;
    dword answer;

    if ((blockno >= pdrive->drive_info.numsecs) || (pdrive->drive_info.firstclblock > blockno))
        return (0);
    else
    {
        /*  (2 + (blockno - pdrive->firstclblock)/pdrive->drive_info.secpalloc)   */
        ltemp = blockno - pdrive->drive_info.firstclblock;
        answer = ltemp;
        answer = (dword) answer >> pdrive->drive_info.log2_secpalloc;
        answer += 2;
        if (answer > pdrive->drive_info.maxfindex)
            answer = 0;
        return ((dword)answer);
    }
}

/****************************************************************************
    PC_SEC2INDEX - Calculate the offset into a cluster for a block.

 Description
    Given a block number offset from the beginning of the drive, calculate
    which block number within a cluster it will be. If the block number
    coincides with a cluster boundary, the return value will be zero. If it
    coincides with a cluster boundary + 1 block, the value will be 1, etc.


 Returns
    0,1,2 upto blockspcluster -1.

***************************************************************************/

/* Convert sector to index into a cluster . No error detection   */
dword pc_sec2index(DDRIVE *pdrive, dword blockno)               /*__fn__*/
{
    dword answer;

    /*  ((blockno - pdrive->firstclblock) % pdrive->drive_info.secpalloc) );   */

    answer = blockno - pdrive->drive_info.firstclblock;
    answer = answer % pdrive->drive_info.secpalloc;

    return (answer);
}


/***************************************************************************
    PC_CL2SECTOR - Convert a cluster number to block number representation.

 Description
    Convert cluster number to a blocknumber.

 Returns
    Returns 0 if the cluster is out of range. else returns the
    block number of the beginning of the cluster.


****************************************************************************/

/* Convert cluster. to sector   */
dword pc_cl2sector(DDRIVE *pdrive, dword cluster)               /*__fn__*/
{
    dword blockno;
    dword t;

    if ((cluster < 2) || (cluster > pdrive->drive_info.maxfindex) )
        return (0);
    else
    {
        t = cluster - 2;
        t = t << pdrive->drive_info.log2_secpalloc;
        blockno = pdrive->drive_info.firstclblock + t;
    }
    if (blockno >= pdrive->drive_info.numsecs)
        return (0);
    else
        return (blockno);
}
