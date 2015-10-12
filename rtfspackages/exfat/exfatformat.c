/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 2000
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
#include "rtfs.h"
#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */

byte flashparms_gui_id[16] = {0x46,0x7e,0x0c,0x0a,0x99,0x33,0x21,0x40,0x90,0xc8,0xfa,0x6d,0x38,0x9c,0x4b,0xa2};

ddword m64_native_set32(dword a, dword b);

static BOOLEAN _scrub_volume(DDRIVE *pdr, RTFSEXFATFMTPARMS *pappfmt, byte *pbuffer, dword buffer_size_sectors, dword buffer_size_bytes);
static BOOLEAN __pcexfat_mkfs(DDRIVE *pdr, RTFSEXFATFMTPARMS *pappfmt, byte *pbuffer, dword buffer_size_bytes);
extern dword _eb_align(dword v, dword ebsize);
static dword _pcexfat_format_good_cluster_range(RTFSEXFATFMTPARMS *pappfmt,dword start, dword requested_range);
static byte pcexfat_dword_toshifter(dword n);
static dword _pcexfat_get_boundary_unit_size(ddword MediaLengthBytes);
static dword _pcexfat_get_cluster_size(ddword MediaLengthBytes);
static byte _pcexfat_get_hcn_heads(ddword MediaLengthBytes);
static byte _pcexfat_get_hcn_secptrack(ddword MediaLengthBytes);
static dword efxat_format_cl2sector(RTFSEXFATFMTPARMS *pappfmt, dword Cluster);
static void _lba_to_hcntuple(dword sector, dword heads, dword secptrack, byte *hcntuple);

/* Format values to force in order to verify exactness of the format compared to SD card formats */
/*	*/

/* should be b717ae5d is 5dae17b7 */
// #define FORCED_SERIAL_NUMBER 0x5dae17b7 // produced for 48 GB card
// #define FORCED_USECOUNT_ZERO


/* Leave defined to force residual bytes in BPB to F4. What some cards do. Still investigating */
#define FORCE_BOOTCODE_ALL_F4S


static BOOLEAN _pcexfat_format_volume(byte *path, RTFSEXFATFMTPARMS *pappfmt);

BOOLEAN pcexfat_format_volume(byte *path)
{
RTFSEXFATFMTPARMS appfmt;
DEV_GEOMETRY geometry;

	if (!pc_get_media_parms(path, &geometry))
		return(FALSE);
	rtfs_memset(&appfmt, 0, sizeof(appfmt));

	appfmt.MediaLengthSectors = M64SET32(0,geometry.dev_geometry_lbas);
	if (geometry.bytespsector)
		appfmt.BytesPerSector = geometry.bytespsector;
	else
		appfmt.BytesPerSector = 512;

	if (!_pcexfat_format_volume(path, &appfmt))
		return(FALSE);
	else
		return(TRUE);
}

/***************************************************************************
    PCEXFAT_FORMAT_VOLUME -  Format a volume

Description
    This routine formats the volume referred to by drive letter.

Returns
    Returns TRUE if it was able to perform the operation otherwise
    it returns FALSE.

    errno is set to one of the following
    0               - No error
    PEINVALIDDRIVEID- Drive component is invalid
    PEIOERRORREADMBR- Partitioned device. IO error reading
    PEINVALIDMBR    - Partitioned device has no master boot record
    PEINVALIDMBROFFSET - Requested partition has no entry in master boot record
    PEINVALIDPARMS  - Inconsistent or missing parameters
    PEIOERRORWRITE  - Error writing during format
    An ERTFS system error
****************************************************************************/

static BOOLEAN _pcexfat_set_format_parms(RTFSEXFATFMTPARMS *pappfmt);
static BOOLEAN _pcexfat_mkfs(DDRIVE *pdr,  RTFSEXFATFMTPARMS *pappfmt);

static BOOLEAN _pcexfat_format_volume(byte *path, RTFSEXFATFMTPARMS *pappfmt)
{
DDRIVE *pdr;
int driveno;

    CHECK_MEM(BOOLEAN, 0)
    rtfs_clear_errno(); /* pc_format_volume: clear error status */
	if (!path || !pappfmt)
	{
        rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
        return(FALSE);
    }
    /* Make sure it s a valid drive number */
    driveno = pc_parse_raw_drive(path, CS_CHARSET_NOT_UNICODE);
    if (driveno < 0)
    {
        rtfs_set_errno(PEINVALIDDRIVEID, __FILE__, __LINE__);
        return(FALSE);
    }
    pdr = pc_drno_to_drive_struct(driveno);

    if (!pdr)
    {
        rtfs_set_errno(PEINVALIDDRIVEID, __FILE__, __LINE__); /* pc_format_volume: bad arguments */
        return(FALSE);
    }

    if (!_pcexfat_set_format_parms(pappfmt))
	{
        rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
        return(FALSE);
	}
	{
	void *devhandle;
	BOOLEAN retval;
    	devhandle = pdr->pmedia_info->devhandle;
    	retval = _pcexfat_mkfs(pdr, pappfmt);
    	if (devhandle) /* re-mount the media, re-read partition tables */
			pc_rtfs_media_remount(devhandle);
		return(retval);
	}
}


/* Look at users input and update input structure with caclulated falues.
   If any field is zero defaults are calculated.

   Required Fields are:
   		ddword MediaLengthSectors
		dword  BytesPerSector;

*/

static BOOLEAN _pcexfat_set_format_parms(RTFSEXFATFMTPARMS *pappfmt)
{
	if (M64ISZERO(pappfmt->MediaLengthSectors) || pappfmt->BytesPerSector == 0)
	{
       	rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
		return(FALSE);
	}

	pappfmt->BytesPerSectorShift = pcexfat_dword_toshifter(pappfmt->BytesPerSector);

	pappfmt->MediaLengthBytes = M64LSHIFT(pappfmt->MediaLengthSectors,pappfmt->BytesPerSectorShift);

	{
	ddword TS;
	dword BU,SC;

		BU = _pcexfat_get_boundary_unit_size(pappfmt->MediaLengthBytes);
		if (!pappfmt->SectorsPerCluster)
		{
			SC = _pcexfat_get_cluster_size(pappfmt->MediaLengthBytes);
			pappfmt->SectorsPerCluster = SC;

		}
		else
		 	SC = pappfmt->SectorsPerCluster;
		pappfmt->BoundaryUnit = BU;
		pappfmt->SectorsPerClusterShift = pcexfat_dword_toshifter(SC);

		TS = pappfmt->MediaLengthSectors;
		pappfmt->MediaLengthBytes = M64LSHIFT(pappfmt->MediaLengthSectors,pappfmt->BytesPerSectorShift);

		pappfmt->PartitionOffset = M64SET32(0,BU);
		pappfmt->VolumeLength = M64MINUS32(TS,BU);
		pappfmt->FatOffset = BU / 2;
		pappfmt->FatLength = BU / 2;


		pappfmt->ClusterHeapOffset = BU;


/* D0 this in 64 bit		pappfmt->ClusterCount = (TS - BU * 2) / SC; */
		{
		ddword dwClusterCount;

		dwClusterCount = M64MINUS32(TS,BU);
		dwClusterCount = M64MINUS32(dwClusterCount,BU);
		dwClusterCount = M64RSHIFT(dwClusterCount, pappfmt->SectorsPerClusterShift);
		pappfmt->ClusterCount = M64LOWDW(dwClusterCount);
		}
	}

	pappfmt->JumpBoot[0] = 0xeb;
	pappfmt->JumpBoot[1] = 0x76;
	pappfmt->JumpBoot[2] = 0x90;

	copybuff(&pappfmt->FileSystemName[0], "EXFAT   ", 8);

	pappfmt->NumberOfFats = 1;

	pappfmt->FileSystemRevision[0] = 0x00;
	pappfmt->FileSystemRevision[1] = 0x01;

	{
	DATESTR crdate;
	dword *pdw;
		pc_getsysdate(&crdate);
		pdw = (dword *) &crdate;
		pappfmt->VolumeSerialNumber = *pdw;
#ifdef FORCED_SERIAL_NUMBER
   		rtfs_print_one_string((byte *)"ifdef FORCED_SERIAL_NUMBER enabled", PRFLG_NL);
		pappfmt->VolumeSerialNumber = FORCED_SERIAL_NUMBER;
#endif
	}
	pappfmt->DriveSelect = 0x80;

#ifdef FORCED_USECOUNT_ZERO
	pappfmt->PercentInUse = 0x0;
#else
	pappfmt->PercentInUse = 0xff;
#endif


	pappfmt->BootSignature[0] = 0x55;
	pappfmt->BootSignature[1] = 0xaa;


   	{
	dword UpCaseSizeClusters,BamSizeClusters,bytes_per_cluster;

	bytes_per_cluster = pappfmt->BytesPerSector * pappfmt->SectorsPerCluster;

   	UpCaseSizeClusters = STANDARD_UCTABLE_SIZE+bytes_per_cluster-1;
   	UpCaseSizeClusters >>= pappfmt->BytesPerSectorShift;
   	UpCaseSizeClusters >>= pappfmt->SectorsPerClusterShift;
	pappfmt->UpCaseSizeClusters = UpCaseSizeClusters;


	{
        /*	BamSizeClusters   = (pappfmt->ClusterCount + entries_per_cluster -1)/entries_per_cluster; */
        /*	pappfmt->BamSizeClusters = BamSizeClusters; */

        /* July 2012 - Fixed error, was undercalculating bam size clusters */
        BamSizeClusters = pappfmt->ClusterCount+7;							  // Round bit to nearest byte */
        BamSizeClusters	  >>= 3;											  // Divide by bits per byte
        BamSizeClusters   += bytes_per_cluster-1;							  // Round up to nearest cluster boundary
        BamSizeClusters   >>= pappfmt->BytesPerSectorShift;					  // Divide by bytes per sector
        BamSizeClusters   >>= pappfmt->SectorsPerClusterShift;				  // Divide by sectors per cluster
        pappfmt->BamSizeClusters = BamSizeClusters;
	}

	{
		dword BamSizeBytes;
		BamSizeBytes = (pappfmt->ClusterCount+7)/8;
		pappfmt->BamSizeBytes = M64SET32(0,BamSizeBytes);
	}
   	pappfmt->FirstClusterOfBam 				= _pcexfat_format_good_cluster_range(pappfmt,2,BamSizeClusters+UpCaseSizeClusters);
   	pappfmt->FirstClusterOfUpCaseTable		= pappfmt->FirstClusterOfBam+BamSizeClusters;
   	pappfmt->FirstClusterOfRootDirectory	= pappfmt->FirstClusterOfUpCaseTable + pappfmt->UpCaseSizeClusters;

   	}
	return(TRUE);
}

static BOOLEAN _pcexfat_mkfs(DDRIVE *pdr,  RTFSEXFATFMTPARMS *pappfmt)
{
int driveno;
byte *pubuff;
dword buffer_size_sectors = 0;
BOOLEAN ret_val = FALSE;

	driveno = pdr->driveno;

    pc_dskfree(driveno);   /* Make sure all is flushed */

    pubuff = pc_claim_user_buffer(pdr, &buffer_size_sectors, 0); /* released on cleanup */
    if (!pubuff)
        return(FALSE);
    /* Call the common format routine */
    ret_val = __pcexfat_mkfs(pdr, pappfmt, pubuff, buffer_size_sectors * RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES);

    pc_release_user_buffer(pdr, pubuff);
    return(ret_val);
}


static BOOLEAN devio_write_format(DDRIVE *pdr, dword blockno, byte * buf, dword n_to_write);
dword pcexfat_bootchecksum_util(dword Checksum, int index, int ByteCount, byte *string);


/* Perform format operation - this is called after the format parameter block has been initialized  */
static BOOLEAN __pcexfat_mkfs(DDRIVE *pdr, RTFSEXFATFMTPARMS *pappfmt, byte *pbuffer, dword buffer_size_bytes)
{
    byte  *b_;
	int i;
    dword  di, buffer_size_sectors,CheckSum,dwPartitionOffset;
    int bytes_per_sector;
    BOOLEAN ret_val;

    ret_val = FALSE;

    dwPartitionOffset =  M64LOWDW(pappfmt->PartitionOffset);

    /* Calculate things from bytes per sector */
    buffer_size_sectors   = buffer_size_bytes>>pappfmt->BytesPerSectorShift;
    bytes_per_sector      = 1;
    bytes_per_sector      <<= pappfmt->BytesPerSectorShift;

	/* Scrub the volume clean if requested */
	if (pappfmt->ScrubVolume && !_scrub_volume(pdr, pappfmt, pbuffer, buffer_size_sectors, buffer_size_bytes))
		return(FALSE);

    b_ = pbuffer;
	/* Format the partition table */
	{
    dword partition_start;
    dword partition_size;

		rtfs_memset(b_, 0, buffer_size_bytes);
		partition_start = M64LOWDW(pappfmt->PartitionOffset);
		partition_size  = M64LOWDW(pappfmt->VolumeLength);
		{
			dword heads;
			dword secptrack;
            heads		= _pcexfat_get_hcn_heads(pappfmt->MediaLengthBytes);
			secptrack	= _pcexfat_get_hcn_secptrack(pappfmt->MediaLengthBytes);
			b_[446] = 0x00; /* Not bootable */
			/* Calculate hcn parms for start. */
			if (partition_start > 16450560)
			{ /* Too big for HCN */
				b_[447] = 0xfe;	b_[448] = 0xff;	b_[449] = 0xff;
			}
			else
			{
				_lba_to_hcntuple(partition_start, heads, secptrack, &b_[447]);
			}
			/* Hardwire hcn parms for end to fe ff ff for SDXC */
			b_[450] = 0x7;
			b_[451] = 0xfe;
			b_[452] = 0xff;
			b_[453] = 0xff;
			fr_DWORD(&b_[454],partition_start);
			fr_DWORD(&b_[458],partition_size);
		}

		b_[511] = 0xaa;
		b_[510] = 0x55;
#if (INCLUDE_WINDEV)
		if (pdr->driveno==('P'-'A')) /* P: is the host drive */
		{
   			rtfs_print_one_string((byte *)"INCLUDE_WINDEV is enabled", PRFLG_NL);
			rtfs_print_one_string((byte*)"Use private boot signature, call hackwin7 to make the volume accessable on windows", PRFLG_NL);
			b_[510] = 0x66;
		}
#endif
		/* Write the partition table */
		if (!devio_write_format(pdr,  0, b_, 1) )
   			goto errex;
	}

	{
    /* Build up main boot sector   */
    rtfs_memset(b_, 0, buffer_size_bytes);
    copybuff(&b_[0],&pappfmt->JumpBoot[0], 3);
    copybuff(&b_[3],&pappfmt->FileSystemName[0], 8);
	fr_DDWORD(&b_[64], pappfmt->PartitionOffset);
	fr_DDWORD(&b_[72], pappfmt->VolumeLength);
	fr_DWORD(&b_[80], pappfmt->FatOffset);
	fr_DWORD(&b_[84], pappfmt->FatLength);
	fr_DWORD(&b_[88], pappfmt->ClusterHeapOffset);
	fr_DWORD(&b_[92], pappfmt->ClusterCount);
	fr_DWORD(&b_[96], pappfmt->FirstClusterOfRootDirectory);
	fr_DWORD(&b_[100], pappfmt->VolumeSerialNumber);
	copybuff(&b_[104], pappfmt->FileSystemRevision,2);
	fr_WORD(&b_[106], pappfmt->VolumeFlags);
	b_[108] = pappfmt->BytesPerSectorShift;
	b_[109] = pappfmt->SectorsPerClusterShift;
	b_[110] = pappfmt->NumberOfFats;
	b_[111] = pappfmt->DriveSelect;
	b_[112] = pappfmt->PercentInUse;
#ifdef FORCE_BOOTCODE_ALL_F4S
	/* Fill the bootcode region with all 4fs. Don't know why but several preformated cards do this */
	rtfs_memset(&b_[120], 0xf4, 390);
#else
	copybuff(&b_[120], pappfmt->BootCode, 390);
#endif
	copybuff(&b_[510], pappfmt->BootSignature, 2);

	/* Do phase 1 of checksum calculation */
	CheckSum = pcexfat_bootchecksum_util(0, 0, bytes_per_sector, b_);
	/* Write the mbr and the second copy of the mbr */
    if (!devio_write_format(pdr,  dwPartitionOffset+0, b_, 1) )
    	goto errex;
    if (!devio_write_format(pdr,  dwPartitionOffset+12, b_, 1) )
        goto errex;
	}

	/* Now write extended boot sectors */
    rtfs_memset(b_, 0, buffer_size_bytes);
	b_[510] = 0x55;
	b_[511] = 0xaa;

	for (i = 0; i < 8; i++)
	{
	byte *pbs;
		if (pappfmt->ExtendedBootCodeCountSectors && i < pappfmt->ExtendedBootCodeCountSectors)
			pbs = pappfmt->ExtendedBootCodeData + (i * bytes_per_sector);
		else
			pbs = b_;
		/* Do phase 2 of checksum */
    	CheckSum = pcexfat_bootchecksum_util(CheckSum, (i+1)*bytes_per_sector, bytes_per_sector, pbs);
    	if (!devio_write_format(pdr,  dwPartitionOffset+1+i, pbs, 1) )
    		goto errex;
        if (!devio_write_format(pdr,  dwPartitionOffset+13+i, pbs, 1) )
        	goto errex;
	}
	/* Now write OEM Parameters (all are null except flash parms */
    rtfs_memset(b_, 0, buffer_size_bytes);
    if (pappfmt->BoundaryUnit)
	{
		dword HalfBoundaryInBytes = (pappfmt->BoundaryUnit << pappfmt->BytesPerSectorShift)/2;
		copybuff(&b_[0],flashparms_gui_id , 16);
		fr_DWORD(&b_[16], HalfBoundaryInBytes);
	}
	/* checksum */
   	CheckSum = pcexfat_bootchecksum_util(CheckSum, 9*bytes_per_sector, bytes_per_sector, b_);
   	if (!devio_write_format(pdr,  dwPartitionOffset+9, b_, 1) )
   		goto errex;
    if (!devio_write_format(pdr,  dwPartitionOffset+21, b_, 1) )
      	goto errex;

	/* Reserved sector must be set to zero */
    rtfs_memset(b_, 0, buffer_size_bytes);
	/* checksum */
   	CheckSum = pcexfat_bootchecksum_util(CheckSum, 10*bytes_per_sector, bytes_per_sector, b_);
   	if (!devio_write_format(pdr,  dwPartitionOffset+10, b_, 1) )
   		goto errex;
    if (!devio_write_format(pdr,  dwPartitionOffset+22, b_, 1) )
      	goto errex;

	/* Write Repeating check sum to sector 11 */
    rtfs_memset(b_, 0, bytes_per_sector);
	for (i = 0; i < bytes_per_sector; i += 4)
	{
		fr_DWORD(&b_[i], CheckSum);
	}
   	if (!devio_write_format(pdr,  dwPartitionOffset+11, b_, 1) )
   		goto errex;
    if (!devio_write_format(pdr,  dwPartitionOffset+23, b_, 1) )
      	goto errex;

	/* Initialize the FAT table */
	{
		dword start_bam = pappfmt->FirstClusterOfBam;
		dword end_bam = start_bam + pappfmt->BamSizeClusters-1;
		dword start_upcase= pappfmt->FirstClusterOfUpCaseTable;
		dword end_upcase = start_upcase + pappfmt->UpCaseSizeClusters-1;
		dword first_index, end_index;
		dword entries_per_sector = pappfmt->BytesPerSector/4;

		rtfs_memset(b_, 0, bytes_per_sector);
		b_[0] = 0xf8;
		rtfs_memset(&b_[1], 0xff, 7);
		first_index = 2;
		end_index = entries_per_sector-1;
		for (di = 0; di < pappfmt->FatLength; di++)
		{
			if (first_index <= pappfmt->FirstClusterOfRootDirectory /* end_bam */)
			{
				if (first_index <= end_bam && first_index <= end_index)
				{
					while (first_index < end_bam && first_index <= end_index)
					{
						fr_DWORD(&b_[first_index*4],first_index);
						first_index++;
					}
					if (first_index == end_bam)
					{
						fr_DWORD(&b_[first_index*4],EXFATEOFCLUSTER);
						first_index++;
					}
				}
				if (first_index <= end_upcase && first_index <= end_index)
				{
					while (first_index < end_upcase && first_index <= end_index)
					{
						fr_DWORD(&b_[first_index*4],first_index);
						first_index++;
					}
					if (first_index == end_upcase)
					{
						fr_DWORD(&b_[first_index*4],EXFATEOFCLUSTER);
						first_index++;
					}
				}
				if (first_index == pappfmt->FirstClusterOfRootDirectory)
				{
					fr_DWORD(&b_[first_index*4],EXFATEOFCLUSTER);
					first_index++;
				}
			}
    		if (!devio_write_format(pdr,  dwPartitionOffset+pappfmt->FatOffset+di, b_, 1) )
      			goto errex;
    		rtfs_memset(b_, 0, bytes_per_sector);
    		end_index += entries_per_sector;
		}
	}
	/* Zero the root directory and initialize with
		BAM
		UpCaseTable
	*/
	rtfs_memset(b_, 0, bytes_per_sector);
	{
		byte *p = &b_[0];
		ddword ddw;
		p[0] = 3;		/* Fudge an empty volume label entry */
		p = &b_[32];
		p[0] = EXFAT_DIRENTTYPE_ALLOCATION_BITMAP;
		fr_DWORD(&p[20], pappfmt->FirstClusterOfBam);
		fr_DDWORD(&p[24], pappfmt->BamSizeBytes);
		p = &b_[64];
		p[0] = EXFAT_DIRENTTYPE_UPCASE_TABLE;
		fr_DWORD(&p[4], STANDARD_UCTABLE_CHECKSUM);
		fr_DWORD(&p[20], pappfmt->FirstClusterOfUpCaseTable);
		ddw = M64SET32(0, STANDARD_UCTABLE_SIZE);
		fr_DDWORD(&p[24], ddw);
	}
	{
	dword FirstSectorOfRootDirectory = efxat_format_cl2sector(pappfmt, pappfmt->FirstClusterOfRootDirectory);
	for (di = 0; di < pappfmt->SectorsPerCluster; di++)
	{
    	if (!devio_write_format(pdr,  dwPartitionOffset+FirstSectorOfRootDirectory+di, b_, 1) )
      		goto errex;
    	rtfs_memset(b_, 0, bytes_per_sector);
	}
	}
	/* Now write the BAM */
	{
	dword BamsizeSectors;
	dword FirstSectorOfBam = efxat_format_cl2sector(pappfmt, pappfmt->FirstClusterOfBam);
		dword filled_byte_count, filled_bit_count, last_filled_sector_number;

	filled_byte_count = (pappfmt->FirstClusterOfRootDirectory-2)/8;
	filled_bit_count  = (pappfmt->FirstClusterOfRootDirectory-1)%8;
	last_filled_sector_number = filled_byte_count / pappfmt->BytesPerSector;
	filled_byte_count = filled_byte_count % pappfmt->BytesPerSector;

	BamsizeSectors = M64LOWDW(pappfmt->BamSizeBytes);
	BamsizeSectors += pappfmt->BytesPerSector-1;
	BamsizeSectors >>= pappfmt->BytesPerSectorShift;
	for (di = 0; di < BamsizeSectors; di++)
	{
		if (di < last_filled_sector_number)
		{
    		rtfs_memset(b_, 0xff, bytes_per_sector);
		}
		else if (di == last_filled_sector_number)
		{
			if (filled_byte_count)
    			rtfs_memset(b_, 0xff, filled_byte_count);
			if (filled_bit_count)
			{
			byte bits = 0;
				while (filled_bit_count--)
				{
					bits <<= 1;
					bits |= 1;
					b_[filled_byte_count] = bits;
				}
			}

		}
		else
    		rtfs_memset(b_, 0, bytes_per_sector);

    	if (!devio_write_format(pdr, dwPartitionOffset+FirstSectorOfBam+di, b_, 1) )
      		goto errex;
    	rtfs_memset(b_, 0, bytes_per_sector);
	}
	}

	/* Now write the UpCaseTable */
	{
	dword FirstSectorOfUpCase = efxat_format_cl2sector(pappfmt, pappfmt->FirstClusterOfUpCaseTable);
	dword UpCaseSizeSectors;
	dword bytes_per_cluster;
	byte *pUpcase;
	int ntocopy;

	bytes_per_cluster = pappfmt->BytesPerSector * pappfmt->SectorsPerCluster;
	UpCaseSizeSectors = STANDARD_UCTABLE_SIZE;
	UpCaseSizeSectors += bytes_per_cluster-1;
	UpCaseSizeSectors /= bytes_per_cluster;
	UpCaseSizeSectors <<= pappfmt->SectorsPerClusterShift;

	ntocopy = STANDARD_UCTABLE_SIZE;
	pUpcase = (byte *) &cStandarducTableCompressed[0];
	for (di = 0; di < UpCaseSizeSectors; di++)
	{
		rtfs_memset(b_, 0, bytes_per_sector);
		if (ntocopy)
		{
		int n;
			if (ntocopy > bytes_per_sector)
				n = bytes_per_sector;
			else
				n = ntocopy;
			copybuff(&b_[0], pUpcase, n);
			pUpcase += n;
			ntocopy -= n;
		}
    	if (!devio_write_format(pdr, dwPartitionOffset+FirstSectorOfUpCase+di, b_, 1) )
      		goto errex;
	}
	}


    ret_val = TRUE;
errex:      /* Not only errors return through here. Everything does. */
    return(ret_val);
}

dword pcexfat_bootchecksum_util(dword Checksum, int index, int ByteCount, byte *string)
{
int i;

	for (i = 0; i < ByteCount; i++,index++)
	{
		if (index == 106 || index == 107 || index == 112)
			continue;
		Checksum =
		 ((Checksum & 1) ? 0x80000000 : 0) + (Checksum>>1) + (dword) string[i];
	}
	return(Checksum);
}


/* These are special versions of devio_write and read that are used by the format utility
   They are the same as their counterparts except they do not automount the
   drive or load the partition table or add partition offsets.
*/

static BOOLEAN devio_write_format(DDRIVE *pdr, dword blockno, byte * buf, dword n_to_write)
{
    if (RTFS_DEVI_io(pdr->driveno, blockno, buf, (word) n_to_write, FALSE))
        return(TRUE);
    else
    {
        rtfs_set_errno(PEIOERRORWRITE, __FILE__, __LINE__);/* devio_write_format: write failed */
        return(FALSE);
    }
}


static BOOLEAN _scrub_volume(DDRIVE *pdr, RTFSEXFATFMTPARMS *pappfmt, byte *pbuffer, dword buffer_size_sectors, dword buffer_size_bytes)
{
	RTFS_ARGSUSED_PVOID((void *) pdr);
	RTFS_ARGSUSED_PVOID((void *) pappfmt);
	RTFS_ARGSUSED_PVOID((void *) pbuffer);
	RTFS_ARGSUSED_DWORD(buffer_size_sectors);
	RTFS_ARGSUSED_DWORD(buffer_size_bytes);

#if (0)



#if (INCLUDE_NAND_DRIVER)   /* Use device erase call if it is availabe */
	if (pdr->pmedia_info->device_erase)
	{
// Fix me...
//		if (pdr->pmedia_info->device_erase(pdr->pmedia_info->devhandle, (void *) pdr, papfmt->ClusterHeapOffset, total_sectors))
//			return(TRUE);
//		else
//			return(FALSE);
	}
#endif
    rtfs_memset(pbuffer, 0, buffer_size_bytes);
    blockno = pfmt->numhide;
    n_left  = pfmt->total_sectors;
    /* zero fill the volume */
    while(n_left)
    {
        if (buffer_size_sectors > n_left)
            n_to_write = n_left;
        else
            n_to_write = buffer_size_sectors;
        if (!devio_write_format(pdr, blockno, pbuffer, n_to_write) )
			return(FALSE);
        n_left  -= n_to_write;
        blockno += n_to_write;
    }
#endif
    return(TRUE);
}

static dword efxat_format_cl2sector(RTFSEXFATFMTPARMS *pappfmt, dword Cluster)
{
	dword sector;
	sector = Cluster-2;
	sector <<= pappfmt->SectorsPerClusterShift;
	sector += pappfmt->ClusterHeapOffset;
	return sector;
}

static dword _pcexfat_format_good_cluster_range(RTFSEXFATFMTPARMS *pappfmt,dword start, dword requested_range)
{
	RTFS_ARGSUSED_PVOID((void *) pappfmt);
	RTFS_ARGSUSED_DWORD(start);
	RTFS_ARGSUSED_DWORD(requested_range);
	return(start);

#if (0)
	if (!pappfmt->BadSectorCount)
		return(start);
	else
	{

	// HEREHERE Needs work
		dword r,end_range;
		end_range = (start + requested_range);
		for (i = 0; i < pappfmt->BadSectorCount; i++)
		{
			if (pappfmt->BadSectorList[i] < end_range)
			{
				start = end_range;
				end_range = (start + requested_range);
			}

		}
		return(start);
	}
#endif

}

static byte pcexfat_dword_toshifter(dword n)
{
byte log;

    log = 0;
    if (n <= 1)
        return(log);

    while(n)
    {
        log += 1;
        n >>= 1;
    }
    return((log-1));
}

struct exfatfmtparmsbysize {
    dword size_mbytes;
    dword boundary_unit_size;
    dword sectors_per_cluster;
	byte hcnheads;
	byte hcnsecptrack;

};

/* Table of media size versus recomended  boundary unit, sectors_per_cluster */

static struct exfatfmtparmsbysize parm_table[] = {
{       1,     32,      32,     2,  16},
{   	2,     32,      32,     2,  16},
{  		8,     32,      32,     8,  32},
{ 	  128,    256,      32,     8,  32},
{ 	 2048,    128,      32,   128, 63},			/* 4 Gb */
{ 	 4096,  16384,      64,   128, 63},			/* 4 Gb */
{ 	 32895, 16384,      64,   255, 63},			/* 32 Gb */
{ 	 131072, 32768,	   256,   255, 63},		/* 128 GB */
{ 	 524288, 65536,    512,   255, 63},		/* .5 Tbytes*/
{ 	 2097152,131072,  1024,  255, 63},		/* 2 Tbytes */
{     0,0, 0, 0, 0},    /* Can't do it */
};




static struct exfatfmtparmsbysize *_pcexfat_get_parm_by_size(ddword MediaLengthBytes)
{
int i;
ddword ddwMediaLengthMbytes;
dword MediaLengthMbytes;

	ddwMediaLengthMbytes = M64RSHIFT(MediaLengthBytes, 20);
	MediaLengthMbytes = M64LOWDW(ddwMediaLengthMbytes);
	/* Find parameters from a table lookup */
    for (i = 0; parm_table[i].size_mbytes; i++)
    {
        if (parm_table[i].size_mbytes >= MediaLengthMbytes)
        {
        	return (&parm_table[i]);
		}
	}
	return(0);
}
static dword _pcexfat_get_boundary_unit_size(ddword MediaLengthBytes)
{
struct exfatfmtparmsbysize *p;

	p = _pcexfat_get_parm_by_size(MediaLengthBytes);
	if (p)
		return p->boundary_unit_size;
	else
		return 0;
}
static dword _pcexfat_get_cluster_size(ddword MediaLengthBytes)
{
struct exfatfmtparmsbysize *p;

	p = _pcexfat_get_parm_by_size(MediaLengthBytes);
	if (p)
		return p->sectors_per_cluster;
	else
		return 0;
}

static byte _pcexfat_get_hcn_heads(ddword MediaLengthBytes)
{
struct exfatfmtparmsbysize *p;

	p = _pcexfat_get_parm_by_size(MediaLengthBytes);
	if (p)
		return p->hcnheads;
	else
		return 255;
}

static byte _pcexfat_get_hcn_secptrack(ddword MediaLengthBytes)
{
struct exfatfmtparmsbysize *p;

	p = _pcexfat_get_parm_by_size(MediaLengthBytes);
	if (p)
		return p->hcnsecptrack;
	else
		return 63;
}


static word _lba_to_cylinder(dword lba_val, dword heads, dword secptrack)
{
dword ltemp;

   ltemp = lba_val;
   ltemp /= heads;
   ltemp /= secptrack;
   if (ltemp > 1023)
	ltemp = 1023;
   return((word) (ltemp & 0xffff) );
}

/* Convert an lba value to hcn.
    cyl  is between 1 and 123
    sec  is the same as in the geomerty stucture (1 <= sec <= 63)_
    head is between the same as in the geomerty stucture (0 <= head <= 255)_

    Note: sec and head will always be correct. cyl will be truncated to 1023 if sector is too
    large to represent as (1023 * 63 * 255)
*/
static void _lba_to_hcntuple(dword sector, dword heads, dword secptrack, byte *hcntuple)
{
	word cyl;
	byte head,sec;

    cyl = _lba_to_cylinder(sector, heads,secptrack);
	{
		dword cyl_boundary,rem,dwhead;
		cyl_boundary = heads * secptrack;
		cyl_boundary *= cyl;
		rem = sector - cyl_boundary;
		dwhead = rem/secptrack;
		rem -= dwhead*secptrack;
		sec = (byte) (rem+1 & 0x3f);
		head = (byte) dwhead & 0xff;
	}
	hcntuple[0] = head;
	hcntuple[1] = (byte) ((cyl >> 6) & 0xc0);
	hcntuple[1] |= sec;
	hcntuple[2] = (byte) (cyl & 0xff);

}
#endif /* Exclude from build if read only */
