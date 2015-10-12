/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* RTEXFATBOOT.C - Low level boot functions for exFat.

    Routines Exported from this file include:

    rtexfat_gblk0       -   Called by pc_gblk0 Read block zero and load exFat parms into the drive structure.
    rtexfat_i_dskopen   -   Called by pc_i_dskopen. Completes initializatioj of drive structure, read bam, upcase table etc.
    preboot_pcclnext	-   Called by preboot code to travers cluster chains when the drive is not yet mounted.
*/

#include "rtfs.h"



BOOLEAN exfatop_check_freespace(DDRIVE *pdr);
int bam_size_sectors(DDRIVE *pdr);
static BOOLEAN  rtexfat_i_rootscan(DDRIVE *pdr);
static BOOLEAN process_directory_block(DDRIVE *pdr, dword sector, byte *b);
static BOOLEAN pc_allocate_exfat_buffers(DDRIVE *pdr);



/****************************************************************************
    rtexfat_gblk0 -

 Description
    Given a valid drive number, read block zero and convert
    its contents from intel to native byte order.

 Returns
    Returns TRUE if all went well.

****************************************************************************/



BOOLEAN  rtexfat_gblk0(DDRIVE *pdr, struct pcblk0 *_pbl0, byte *b)
{
	BOOLEAN ret_val;
	int bp;

	SETISEXFAT(pdr);

	for(bp = 11; bp < 63; bp++)
	{
		if (b[bp] != 0)
		{
#if (DEBUG_EXFAT_VERBOSE)
   		rtfs_print_one_string((byte *)"found non zero value in must be zero range", PRFLG_NL);
#endif
/*			return (FALSE); */
			break;
		}
	}

	_pbl0->jump = b[0];									/* Setting this here so the compiler doesn't complian about
														   not referencing _pbl0 */

    copybuff(PDRTOEXFSTR(pdr)->JumpBoot, &b[0], 3); 				/* (BP 0 to 2) */
    copybuff(PDRTOEXFSTR(pdr)->FileSystemName, &b[3], 8); 			/* (BP 3 to 10) */
    to_DDWORD(&PDRTOEXFSTR(pdr)->PartitionOffset, &b[64]);			/* (BP 64 to 71) */
    to_DDWORD(&PDRTOEXFSTR(pdr)->VolumeLength, &b[72]);				/* (BP 72 to 79) */
    PDRTOEXFSTR(pdr)->FatOffset 					= to_DWORD(&b[80]); /* (BP 80 to 83) */
    PDRTOEXFSTR(pdr)->FatLength 					= to_DWORD(&b[84]); /* (BP 84 to 87) */
    PDRTOEXFSTR(pdr)->ClusterHeapOffset 			= to_DWORD(&b[88]); /* (BP 88 to 91) */
    PDRTOEXFSTR(pdr)->ClusterCount 					= to_DWORD(&b[92]); /* (BP 92 to 95) */
    PDRTOEXFSTR(pdr)->FirstClusterOfRootDirectory 	= to_DWORD(&b[96]); /* (BP 96 to 99) */
    PDRTOEXFSTR(pdr)->VolumeSerialNumber 			= to_DWORD(&b[100]); /* (BP 100 to 103) */
    PDRTOEXFSTR(pdr)->FileSystemRevision 			= to_WORD(&b[104]); /* (BP 104 and 105) */
    PDRTOEXFSTR(pdr)->VolumeFlags 					= to_WORD(&b[106]); /* (BP 106 and 107) */
    PDRTOEXFSTR(pdr)->BytesPerSectorShift 			= b[108]; 			/* (BP 108) */
    PDRTOEXFSTR(pdr)->SectorsPerClusterShift 		= b[109]; 			/* (BP 109) */
    PDRTOEXFSTR(pdr)->NumberOfFats 					= b[110]; 			/* (BP 110) */
    PDRTOEXFSTR(pdr)->DriveSelect 					= b[111]; 			/* (BP 111) */
    PDRTOEXFSTR(pdr)->PercentInUse 					= b[112]; 			/* (BP 112) */
    copybuff(PDRTOEXFSTR(pdr)->BootSignature, &b[510], 2); /* (BP 510 and 511) */

    ret_val = TRUE;
    return(ret_val);
}


BOOLEAN  rtexfat_i_dskopen(DDRIVE *pdr)                      /*__fn__*/
{
#if (DEBUG_EXFAT_VERBOSE)
dword scan_start_time;
#endif
#if (INCLUDE_WINDEV)
    if ( (PDRTOEXFSTR(pdr)->JumpBoot[0] == (byte) 0xE8) )
	{
#if (DEBUG_EXFAT_VERBOSE)
       	rtfs_print_one_string((byte *)"Using Hack E8 BPB signature so volume it is writeable", PRFLG_NL);
#endif
    	PDRTOEXFSTR(pdr)->JumpBoot[0]= 0xEB;
	}
#endif
    /* Verify that we have a good exFat formatted disk   */
    if ( PDRTOEXFSTR(pdr)->JumpBoot[0] != (byte) 0xEB || PDRTOEXFSTR(pdr)->JumpBoot[1] != (byte) 0x76 || PDRTOEXFSTR(pdr)->JumpBoot[2] != (byte) 0x90)
    {
        rtfs_set_errno(PEINVALIDBPB, __FILE__, __LINE__);  /* pc_i_dskopen Unkown values in Bios Parameter block */
        goto return_error;
    }

    /* set up the drive structure from block 0   */
    pdr->drive_info.secpalloc = 1;
    pdr->drive_info.log2_secpalloc = PDRTOEXFSTR(pdr)->SectorsPerClusterShift;
    pdr->drive_info.secpalloc <<= PDRTOEXFSTR(pdr)->SectorsPerClusterShift;

    pdr->drive_info.log2_bytespsec = PDRTOEXFSTR(pdr)->BytesPerSectorShift;
    pdr->drive_info.bytespsector = 1;
    pdr->drive_info.bytespsector <<= PDRTOEXFSTR(pdr)->BytesPerSectorShift;

    pdr->drive_info.numroot = 0;

	/* July 2012 - bug bux, add PDRTOEXFSTR(pdr)->ClusterHeapOffset to numsec */
    /* pdr->drive_info.numsecs = M64LOWDW(PDRTOEXFSTR(pdr)->VolumeLength);  Double check this, plus truncated to 4 bytes from 8 */
    pdr->drive_info.numsecs      = PDRTOEXFSTR(pdr)->ClusterCount << PDRTOEXFSTR(pdr)->SectorsPerClusterShift;
	pdr->drive_info.numsecs		+= PDRTOEXFSTR(pdr)->ClusterHeapOffset;
    pdr->drive_info.bytespcluster = pdr->drive_info.bytespsector << PDRTOEXFSTR(pdr)->SectorsPerClusterShift;
    pdr->drive_info.byte_into_cl_mask = (dword) pdr->drive_info.bytespcluster;
    pdr->drive_info.byte_into_cl_mask -= 1L;

    pdr->drive_info.fasize = 8;             /* Nibbles per fat entry. (3,4 or 8) */
    pdr->drive_info.rootblock = 0;          /* First block of root dir */

    pdr->drive_info.firstclblock = PDRTOEXFSTR(pdr)->ClusterHeapOffset;
    pdr->drive_info.maxfindex    = PDRTOEXFSTR(pdr)->ClusterCount + 1; /* Index starts at 2 so maxfindex count + 1 */

    pdr->drive_info.fatblock     = PDRTOEXFSTR(pdr)->FatOffset;

    pdr->drive_info.secproot = 0;           /* blocks in root dir */

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

    pdr->drive_info.log2_bytespalloc = pdr->drive_info.log2_secpalloc + pdr->drive_info.log2_bytespsec;
    pdr->drive_info.bytespcluster = (int) (pdr->drive_info.bytespsector * pdr->drive_info.secpalloc);
    pdr->drive_info.byte_into_cl_mask = (dword) pdr->drive_info.bytespcluster;
    pdr->drive_info.byte_into_cl_mask -= 1L;

    pdr->drive_info.secreserved = (word) PDRTOEXFSTR(pdr)->FatOffset;    /* Reserved sectors before the FAT */
    pdr->drive_info.numfats      = PDRTOEXFSTR(pdr)->NumberOfFats;

	/* So we don't divide by zero */
    if ( (pdr->drive_info.numfats != 1) && (pdr->drive_info.numfats != 2) )
   		pdr->drive_info.numfats = 1;

    pdr->drive_info.secpfat      =		PDRTOEXFSTR(pdr)->FatLength/pdr->drive_info.numfats;
    /* Check bytes per sector.. make sure <= MAX_BLOCK_SIZE and Non-zero */
    {
    int sector_size_bytes;
        sector_size_bytes = (int) pc_get_media_sector_size(pdr);

        if(!pdr->drive_info.secpalloc || PDRTOEXFSTR(pdr)->SectorsPerClusterShift > 16 || !pdr->drive_info.bytespsector || (int) pdr->drive_info.bytespsector != sector_size_bytes) // <NAND>
        {
#if (DEBUG_EXFAT_VERBOSE)
			rtfs_print_one_string((byte *)"Invalid exFat BPB encountered", PRFLG_NL);
#endif
           rtfs_set_errno(PEINVALIDBPB, __FILE__, __LINE__);  /* pc_i_dskopen Unkown values in Bios Parameter block */
            goto return_error;
        }
    }
	/* Set the root sector value from the first cluster value */
    pdr->drive_info.rootblock = pc_cl2sector(pdr, PDRTOEXFSTR(pdr)->FirstClusterOfRootDirectory);
    SETISEXFAT(pdr);

    /* use a private buffer pool per drive */
    pdr->drive_state.pbuffcntxt = &pdr->_buffctxt;

    /* Initialize the fat block buffer pool */
    pc_free_all_fat_buffers(pdr);

#if (INCLUDE_RTFS_FREEMANAGER)
    /* Attach region manager functions if enabled */
    if (!free_manager_attach(pdr))
	{
#if (DEBUG_EXFAT_VERBOSE)
		rtfs_print_one_string((byte *)"Free manager attach failed", PRFLG_NL);
#endif
        goto return_error;
	}
#endif
#if (INCLUDE_RTFS_PROPLUS)
	/* Tell the driver to initialize the cache for this volume if it has one */
    pdr->pmedia_info->device_ioctl(pdr->pmedia_info->devhandle, (void *) pdr, RTFS_IOCTL_INITCACHE, 0 , 0);
#endif

    /* Scan the root directory for the locations of the bam, volume label, ucase table, etc */
    if (!rtexfat_i_rootscan(pdr))
	{
#if (DEBUG_EXFAT_VERBOSE)
		rtfs_print_one_string((byte *)"rtexfat_i_rootscan.", PRFLG_NL);
#endif
        goto return_error;
	}


	/* If exFat buffering is not yet allocated call the user for buffers
	   Note: this will be called each time but not sure where to glue in the call to release so being defensive */
	if (!PDRTOEXFSTR(pdr)->BitMapBufferCore)
	{
		if (!pc_allocate_exfat_buffers(pdr))
		{
#if (DEBUG_EXFAT_VERBOSE)
			rtfs_print_one_string((byte *)"pc_allocate_exfat_buffers failed.", PRFLG_NL);
#endif
			goto return_error;
		}
	}
    /* Initialize the exfat bit allocation map (bam) block buffer pool */
    pc_free_all_bam_buffers(pdr);
    /* Preload the bam cache and scan for free clusters, push information to the free manager */

	 /* Set values for exFat free_contig_pointer is used if EXFAT_FAVOR_CONTIGUOUS_FILES is true. */
    pdr->drive_info.free_contig_pointer = pdr->drive_info.free_contig_base = 2;
    pdr->drive_info.infosec = 0;

#if (DEBUG_EXFAT_VERBOSE)
	scan_start_time =  rtfs_port_elapsed_zero();
#endif
    if (!exfatop_check_freespace(pdr))
	{
#if (DEBUG_EXFAT_VERBOSE)
		rtfs_print_one_string((byte *)"exfatop_check_freespace failed.", PRFLG_NL);
#endif
		goto return_error;
	}
#if (DEBUG_EXFAT_VERBOSE)
		rtfs_print_one_string((byte *)"exfatop_check_freespace duration: ", 0);
		rtfs_print_long_1(rtfs_port_elapsed_zero() - scan_start_time, PRFLG_NL);
#endif
    /* Load the exFat upCase table */
    if (!exfatop_read_upCaseTable(pdr))
	{
#if (DEBUG_EXFAT_VERBOSE)
		rtfs_print_one_string((byte *)"exfatop_read_upCaseTable failed.", PRFLG_NL);
#endif
		goto return_error;
    }
    pc_i_dskopen_complete(pdr); /* Set the drive open */

#if (DEBUG_EXFAT_PROBE_ROOT)
	probePrintBootRegion(pdr);
#endif
#if (INCLUDE_FAILSAFE_RUNTIME) /* Call failsafe autorestore and open routines */
    if (prtfs_cfg->pfailsafe && !prtfs_cfg->pfailsafe->fs_failsafe_dskopen(pdr) )
        goto return_error;
#endif
    rtfs_app_callback(RTFS_CBA_INFO_MOUNT_COMPLETE, pdr->driveno, 0, 0, 0);
    return(TRUE);
return_error:
	pc_release_exfat_buffers(pdr);
    return(FALSE);
}


static BOOLEAN  rtexfat_i_rootscan(DDRIVE *pdr)
{
	dword cluster;
    BLKBUFF *buf;
    BLKBUFF bbuf_scratch;   /* Used by pc_sys_sector build a buffer structure with core from the driver level */
    byte *b;
	int error;
	BOOLEAN ret_val,processing;

    /* Grab a buffer to play with use pc_sys_sector() because the volume is not mounted  */
    buf = pc_sys_sector(pdr, &bbuf_scratch);
    if (!buf)
    {
        rtfs_set_errno(PERESOURCESCRATCHBLOCK, __FILE__, __LINE__); /* pc_gblk0 couldn't allocate a buffer */
        return(FALSE);
    }
	ret_val = FALSE;
    b = buf->data;      /* Now we do not have to use the stack */
	processing = TRUE;

	cluster = PDRTOEXFSTR(pdr)->FirstClusterOfRootDirectory;
    while (cluster && processing)
	{
		dword s, sector;
		sector = pc_cl2sector(pdr, cluster);

		for (s = 0; s < pdr->drive_info.secpalloc; s++)
		{
			if (!raw_devio_xfer(pdr, sector+s, b,1, FALSE, TRUE))
			{
#if (DEBUG_EXFAT_VERBOSE)
				rtfs_print_one_string((byte *)"exfat error reading root directory.", PRFLG_NL);
#endif
        		rtfs_set_errno(PEIOERRORREAD, __FILE__, __LINE__);
        		goto done;
			}
#if (DEBUG_EXFAT_PROBE_ROOT)
			probePrintDirectoryBlock(pdr, b, cluster, s, 1);
#endif
			if (!process_directory_block(pdr, sector+s, b))
			{
				processing = FALSE;
				break;
			}
		}
		error = 0;
		cluster = preboot_pcclnext(pdr, cluster, &error);
		if (error)
		{
       		rtfs_set_errno(PEIOERRORREAD, __FILE__, __LINE__);
        		ret_val = FALSE;
       		goto done;
		}

	}
	/* Validate that we got the right info from root sector */
	if (PDRTOEXFSTR(pdr)->FirstClusterOfBitMap[0] && PDRTOEXFSTR(pdr)->SizeOfBitMap[0] &&
		PDRTOEXFSTR(pdr)->FirstClusterOfUpCase	&& PDRTOEXFSTR(pdr)->SizeOfUpcase)
	{
		ret_val = TRUE;
	}
	else
	{	/* Set errno to invalid BPB (bad format) */
#if (DEBUG_EXFAT_VERBOSE)
	if (!PDRTOEXFSTR(pdr)->FirstClusterOfBitMap[0] || !PDRTOEXFSTR(pdr)->SizeOfBitMap[0])
		rtfs_print_one_string((byte *)"Missing bitmap directory entry.", PRFLG_NL);
	if (!PDRTOEXFSTR(pdr)->FirstClusterOfUpCase	&& !PDRTOEXFSTR(pdr)->SizeOfUpcase)
		rtfs_print_one_string((byte *)"Missing UpCase Table directoryy entry.", PRFLG_NL);
#endif
		ret_val = FALSE;
		rtfs_set_errno(PEINVALIDBPB, __FILE__, __LINE__);

	}
done:
    pc_free_sys_sector(buf);
    return(ret_val);
}

static BOOLEAN process_directory_block(DDRIVE *pdr, dword sector, byte *b)
{
int i;
byte EntryType;
BOOLEAN retval = TRUE;
ddword ddw;


	for (i = 0; i < pdr->drive_info.inopblock; i++, b += 32)
	{
		EntryType = *b;
		switch (EntryType)  {
        case EXFAT_DIRENTTYPE_EOF:
			break;
        case EXFAT_DIRENTTYPE_ALLOCATION_BITMAP:
		{
		dword FirstCluster,SizeOfBitMap;
		int   fatno;
			/* Save values for later use by the "bam" handler */
			fatno = (int) ((*b+1) & 0x01); /* Bit 1 of second byte indicates if we are the first or second fat */
			FirstCluster = to_DWORD(b+EXFATDIRENTFIRSTCLOFFSET);
			to_DDWORD(&ddw, b+EXFATDIRENTLENGTHOFFSET);
			SizeOfBitMap = M64LOWDW(ddw);

			PDRTOEXFSTR(pdr)->PrimaryFlagsOfBitMap[fatno]  = to_WORD(b+4);
			PDRTOEXFSTR(pdr)->FirstClusterOfBitMap[fatno]	= FirstCluster;
			PDRTOEXFSTR(pdr)->SizeOfBitMap[fatno]		  	= SizeOfBitMap;
			break;
		}
        case EXFAT_DIRENTTYPE_UPCASE_TABLE:

			PDRTOEXFSTR(pdr)->FirstClusterOfUpCase	= to_DWORD(b+EXFATDIRENTFIRSTCLOFFSET);
			to_DDWORD(&ddw, b+EXFATDIRENTLENGTHOFFSET);
			PDRTOEXFSTR(pdr)->SizeOfUpcase = M64LOWDW(ddw);
			PDRTOEXFSTR(pdr)->TableChecksum = to_DWORD(b+4);
			break;
        case EXFAT_DIRENTTYPE_VOLUME_LABEL:
			/* Copy 22 byte unicode VolumeLabel[22] field in pdr and process. pg 72 */

			copybuff(pdr->drive_info.volume_label, b+2, 22);
        	pdr->drive_info.volume_label[22] = 0;
        	pdr->drive_info.volume_label[23] = 0;
			PDRTOEXFSTR(pdr)->volume_label_sector = sector;
			PDRTOEXFSTR(pdr)->volume_label_index  = i;
			break;
        case EXFAT_DIRENTTYPE_FILE:
			break;
        case EXFAT_DIRENTTYPE_GUID:
			break;
        case EXFAT_DIRENTTYPE_STREAM_EXTENSION:
			break;
        case EXFAT_DIRENTTYPE_FILE_NAME_ENTRY:
			break;
        case EXFAT_DIRENTTYPE_VENDOR_EXTENSION:
			break;
        case EXFAT_DIRENTTYPE_VENDOR_ALLOCATION:
			break;
		default:
			break;
		}
		/* Return FALSE so we stop scanning */
		if (EntryType == EXFAT_DIRENTTYPE_EOF)
		{
			retval = FALSE;
			break;
		}

	}
	return (retval);
}
/* Called by rtexfat_i_dskopen() to call back to the user code and allocate exfat
   buffering */
static BOOLEAN pc_allocate_exfat_buffers(DDRIVE *pdr)
{
EXFATMOUNTPARMS exMountParms;
BOOLEAN hasStandardUctable = FALSE;

	rtfs_memset(&exMountParms, 0, sizeof(exMountParms));
	/* Pass hints to callback about buffering required */
	exMountParms.driveID 				= pdr->driveno;
	exMountParms.pdr	 				= (void *) pdr;
	exMountParms.SectorSizeBytes		= pdr->drive_info.bytespsector;
	exMountParms.BitMapSizeSectors		= bam_size_sectors(pdr);

	if (PDRTOEXFSTR(pdr)->TableChecksum == STANDARD_UCTABLE_CHECKSUM && PDRTOEXFSTR(pdr)->SizeOfUpcase == STANDARD_UCTABLE_SIZE)
		hasStandardUctable = TRUE;

	if (hasStandardUctable)
		exMountParms.UpcaseSizeBytes		= 0; /* Don't request a buffer, we will use internal */
	else
		exMountParms.UpcaseSizeBytes		= (65536*2); /* Try for 64 K words to fully decode the packet (not: PDRTOEXFSTR(pdr)->SizeOfUpcase;) */

	/* Call the user routine */
	rtfs_sys_callback(RTFS_CBS_GETEXFATBUFFERS, (void *) &exMountParms); /* call the application layer to get the buffer core */


	if (!exMountParms.BitMapBufferSizeSectors || !exMountParms.BitMapBufferCore || !exMountParms.BitMapBufferControlCore ||
		exMountParms.BitMapBufferPageSizeSectors != 1)
	{
        rtfs_set_errno(PERESOURCEEXFAT, __FILE__, __LINE__);
		return(FALSE);
	}

	if (hasStandardUctable)
	{
		PDRTOEXFSTR(pdr)->UpCaseBufferCore         	 = (void *)&cStandarducTableunCompressed[0];
    	PDRTOEXFSTR(pdr)->UpCaseMaxTranskey        	 = 0xffff;
	}
	else if (!PDRTOEXFSTR(pdr)->UpCaseBufferCore)
	{
		PDRTOEXFSTR(pdr)->UpCaseBufferCore         	 = (void *) &cStandarducTableunCompressed[0];
    	PDRTOEXFSTR(pdr)->UpCaseMaxTranskey        	 = 128;
	}
	else
	{
    	PDRTOEXFSTR(pdr)->UpCaseBufferCore         	 = exMountParms.UpCaseBufferCore;
    	PDRTOEXFSTR(pdr)->UpCaseMaxTranskey        	 = 0xffff;
	}

    PDRTOEXFSTR(pdr)->BitMapBufferSizeSectors  	 = exMountParms.BitMapBufferSizeSectors;
    PDRTOEXFSTR(pdr)->BitMapBufferCore         	 = exMountParms.BitMapBufferCore;
    PDRTOEXFSTR(pdr)->BitMapBufferControlCore  	 = exMountParms.BitMapBufferControlCore;
    PDRTOEXFSTR(pdr)->BitMapBufferPageSizeSectors= exMountParms.BitMapBufferPageSizeSectors;
	return(TRUE);
}

dword preboot_pcclnext(DDRIVE *pdr, dword cluster, int *error)
{
	dword fat_sector, sector, fat_index,ret_val;
	int clusters_per_sector;
    BLKBUFF *buf;
    BLKBUFF bbuf_scratch;   /* Used by pc_sys_sector build a buffer structure with core from the driver level */
    byte *b;

	*error = 0;
	ret_val = 0;

    /* Grab a buffer to play with use pc_sys_sector() because the volume is not mounted  */
    buf = pc_sys_sector(pdr, &bbuf_scratch);
    if (!buf)
    {
#if (DEBUG_EXFAT_VERBOSE)
   		rtfs_print_one_string((byte *)"preboot_pcclnext: Error allocating sys sector", PRFLG_NL);
#endif
        rtfs_set_errno(PERESOURCESCRATCHBLOCK, __FILE__, __LINE__); /* pc_gblk0 couldn't allocate a buffer */
        *error = 1;
        return(0);
    }
    b = buf->data;      /* Now we do not have to use the stack */

    clusters_per_sector = pdr->drive_info.bytespsector >> 2;
	fat_sector = cluster/clusters_per_sector;
	fat_index  = cluster%clusters_per_sector;
	sector = fat_sector + PDRTOEXFSTR(pdr)->FatOffset;

    /* get 1 block starting at 0 from driveno   */
    /* READ                                     */
    if (!raw_devio_xfer(pdr, sector, b,1, FALSE, TRUE))
    {
#if (DEBUG_EXFAT_VERBOSE)
   		rtfs_print_one_string((byte *)"preboot_pcclnext: Error reading sector: ", 0);
   		rtfs_print_long_1(sector,PRFLG_NL);
#endif
        rtfs_set_errno(PEIOERRORREAD, __FILE__, __LINE__);
        *error = 1;
		goto done;
    }

    ret_val = to_DWORD(b+(fat_index*4));
	if (ret_val == EXFATEOFCLUSTER)
		ret_val = 0;
done:
    pc_free_sys_sector(buf);
    return(ret_val);
}
