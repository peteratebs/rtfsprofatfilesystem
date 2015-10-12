/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* RTEXFATPROBE.C - Low level pre-production diagnostic functions for exFat.

    Routines Exported from this file include:

    probePrintDirectoryBlock - Called from rtexfat_i_rootscan

*/

#include "rtfs.h"


dword pcexfat_bootchecksum_util(dword Checksum, int index, int ByteCount, byte *string);
int bam_get_bits(DDRIVE *pdr, dword which_cluster, dword *ncontig);

static byte bigbuffer[11*512];
void probe_exfat_format_parms(byte *path)
{
int driveno;
DDRIVE *pdr;
BLKBUFF *buf;
byte *pbuf;
PTABLE *ppart;
dword  BigCheckSum, CheckSum ,i, copy_offset,partition_start = 0;
    BLKBUFF bbuf_scratch;   /* Used by pc_sys_sector build a buffer structure with core from the driver level */
    /* Make sure it s a valid drive number */
    driveno = pc_parse_raw_drive(path, CS_CHARSET_NOT_UNICODE);
    if (driveno < 0)
        return;
    pdr = pc_drno_to_drive_struct(driveno);
    buf = pc_sys_sector(pdr, &bbuf_scratch);
    if (!buf)
		return;

    if (!raw_devio_xfer(pdr, 0, buf->data , 1, TRUE, TRUE))
		return;

    /* Copy the table to a word alligned buffer   */
    pbuf = buf->data;
 //   jump = buf->data[0];
    pbuf += 0x1be;          /* The info starts at buf[1be] */
    /* Don't use sizeof here since the structure does not pack to exact size */
    copybuff(buf->data, pbuf, 0x42);
    ppart = (PTABLE *) buf->data;


    rtfs_print_one_string((byte *)"rsec    == ",PRFLG_NL);    rtfs_print_long_1(to_DWORD ((byte *) &ppart->ents[0].r_sec),PRFLG_NL);
    rtfs_print_one_string((byte *)"p_size  == ",PRFLG_NL);    rtfs_print_long_1(to_DWORD ((byte *) &ppart->ents[0].p_size),PRFLG_NL);
    rtfs_print_one_string((byte *)"p_type  == ",PRFLG_NL);    rtfs_print_long_1(ppart->ents[0].p_typ,PRFLG_NL);
    rtfs_print_one_string((byte *)"boot    == ",PRFLG_NL);    rtfs_print_long_1(ppart->ents[0].boot,0);   /* 0x80 for bootable ,PRFLG_NL*/
    rtfs_print_one_string((byte *)"s_head  == ",PRFLG_NL);    rtfs_print_long_1(ppart->ents[0].s_head,PRFLG_NL);
    rtfs_print_one_string((byte *)"s_cyl   == ",PRFLG_NL);    rtfs_print_long_1( to_WORD((byte *)&ppart->ents[0].s_cyl),PRFLG_NL);
    rtfs_print_one_string((byte *)"e_head  == ",PRFLG_NL);    rtfs_print_long_1(ppart->ents[0].e_head,PRFLG_NL);
    rtfs_print_one_string((byte *)"e_cyl   == ",PRFLG_NL);    rtfs_print_long_1(to_WORD((byte *)&ppart->ents[0].e_cyl),PRFLG_NL);
    partition_start = to_DWORD ((byte *) &ppart->ents[0].r_sec);
    copy_offset = 0;

	for (copy_offset = 0; copy_offset < 24; copy_offset += 12)
	{
	/// Calulate checksum in one read
  	if (!raw_devio_xfer(pdr, partition_start+copy_offset+0, bigbuffer , 11, TRUE, TRUE))
		return;
	BigCheckSum = pcexfat_bootchecksum_util(0, 0, 11*512, bigbuffer);
	rtfs_print_one_string((byte *)" Checksum of 12 block section in one pass = ",0);
    rtfs_print_long_1(BigCheckSum,PRFLG_NL);

	/* Read exFat BPB */
  	if (!raw_devio_xfer(pdr, partition_start+copy_offset +0, buf->data , 1, TRUE, TRUE))
		return;
	CheckSum = pcexfat_bootchecksum_util(0, 0, 512, buf->data);
	rtfs_print_one_string((byte *)" Checksum of bpb =  ",0);
    rtfs_print_long_1(CheckSum,PRFLG_NL);


	/* Read Extended Boot Sectors */
	for (i = 1; i < 9; i++)
	{
    	if (!raw_devio_xfer(pdr, partition_start+copy_offset+i, buf->data , 1, TRUE, TRUE))
			return;
    	CheckSum = pcexfat_bootchecksum_util(CheckSum, i*512, 512, buf->data);
	}
	rtfs_print_one_string((byte *)" Checksum of extended boot sectors =  ",0);
    rtfs_print_long_1(CheckSum,PRFLG_NL);
	/* Read parameter table */
    if (!raw_devio_xfer(pdr, partition_start+copy_offset+9, buf->data , 1, TRUE, TRUE))
		return;
   	CheckSum = pcexfat_bootchecksum_util(CheckSum, 9*512, 512, buf->data);
	rtfs_print_one_string((byte *)" Checksum of extended parm sectors =  ",0);
    rtfs_print_long_1(CheckSum,PRFLG_NL);

	/* Read reserved area */
    if (!raw_devio_xfer(pdr, partition_start+copy_offset+10, buf->data , 1, TRUE, TRUE))
		return;
   	CheckSum = pcexfat_bootchecksum_util(CheckSum, 10*512, 512, buf->data);
	rtfs_print_one_string((byte *)" Checksum of reserved sectors =  ",0);
    rtfs_print_long_1(CheckSum,PRFLG_NL);

	/* Read Checksum */
    if (!raw_devio_xfer(pdr, partition_start+copy_offset+11, buf->data , 1, TRUE, TRUE))
		return;
	{
	to_DWORD ((byte *) buf->data);
	rtfs_print_one_string((byte *)" Stored Checksum =  ",0);
    rtfs_print_long_1(to_DWORD ((byte *) buf->data),PRFLG_NL);
	rtfs_print_one_string((byte *)" Calculated Checksum =  ",0);
    rtfs_print_long_1(CheckSum,PRFLG_NL);
	}

	if (copy_offset!=0)
	{
	dword index;
	dword dw;
	dword frag_start, frag_len;
	dword mymax = 0;

	frag_start = 0; frag_len = 0;
	printf("Dispaying used regions of the FAT \n");
	printf("=============================== \n");
	for(index = 2;index < pdr->drive_info.maxfindex;)
	{
		byte *b;
		b = fatxx_pfswap(pdr, index, FALSE);
		if (index == 2)
		{
			b+=8;
			mymax = 128;
		}
		else mymax += 128;
		for(;index < mymax;index++)
		{
			dw =  to_DWORD(b);
			b += 4;
			if (dw == 0)
			{
				if (frag_start)
				{
					printf("(%d - %d)", frag_start,frag_start+frag_len-1);
					frag_start = 0;
					frag_len = 0;
				}
			}
			else
			{
				if (!frag_start)
					frag_start = index;
				frag_len += 1;
			}
		}
	}
	if (frag_start)
	{
		printf("(%d - %d)", frag_start,frag_start+frag_len-1);
	}
	printf("\n =============================== \n");
	printf("Dispaying used regions of the BAM \n");
	printf("=============================== \n");
	for(index = 2;index < pdr->drive_info.maxfindex;)
	{
	dword ncontig;
	int v;
		v = bam_get_bits(pdr, index, &ncontig);
       	if (!ncontig)	/* Shouldn't happen but don't get stuck in a loop */
			break;
   		if (v == 1)		/* Found ncontig in use clusters starting at index */
		{
			printf("(%d-%d)", index, index+ncontig-1);
		}
		index += ncontig;
	}
	}}
}

#if (DEBUG_EXFAT_PROBE_ROOT)

static void probePrintMBR(byte *b);
static void probePrintBPB(DDRIVE *pdr, byte *b);
static void probePrintExtendedBootSectors(byte *b, dword sector);
static void probePrintOemParameters(byte *b);
static void probePrintReserved(byte *b);
static void probePrintCheckSum(byte *b);
static void probePrintUpCase(DDRIVE *pdr, byte *b);
static void probePrintBAM(DDRIVE *pdr, byte *b);
static void probePrintFAT(DDRIVE *pdr, byte *b);
static void probePrintROOTDIRECTORY(DDRIVE *pdr, byte *b);
static void probeDumpSector(byte *b);
static void probePrintClusterChain(DDRIVE *pdr, byte *b, char *prompt, dword cluster);
static void probeDumpSectorData(byte *b);
static int probeTestSectorData(byte *b);

static dword sector_size;

static char myhexbuff[80];
static char * probeFormatHexString(byte *b, int nbytes)
{
	int i;
	for (i = 0; i < nbytes; i++)
	{
		sprintf(&myhexbuff[i*2],"%2.2x", b[i]);
	}
	return(&myhexbuff[0]);
}

dword preboot_pcclnext(DDRIVE *pdr, dword cluster, int *error);

void probePrintBootRegion(DDRIVE *pdr)
{
dword sector;
byte b[2048];
byte b2[2048];

    sector_size = 512;

    if (pdr->drive_info.partition_base)
	{
		printf("Partition base at %d (%X) \n",pdr->drive_info.partition_base,pdr->drive_info.partition_base);
		sector = 0;
		if (!raw_devio_xfer(pdr, sector, b,1, TRUE, TRUE))
			goto error_out;
		probePrintMBR(b);
	}
	else
		printf(" No partitions found. see bpb dump for raw contents of first sector\n");

    sector = 0;
    if (!raw_devio_xfer(pdr, sector, b,1, FALSE, TRUE))
            goto error_out;
    probePrintBPB(pdr, b);
//    printf("=====Extended Boot Sectors==========\n");
    for (sector = 1; sector < 9; sector++)
    {
        if (!raw_devio_xfer(pdr, sector, b,1, FALSE, TRUE))
            goto error_out;
        probePrintExtendedBootSectors(b, sector);
    }
//    printf("=====OEM Parameters ==========\n");
    sector = 9;
    if (!raw_devio_xfer(pdr, sector, b,1, FALSE, TRUE))
        goto error_out;
    probePrintOemParameters(b);


    sector = 10;
    if (!raw_devio_xfer(pdr, sector, b,1, FALSE, TRUE))
        goto error_out;
    probePrintReserved(b);

    sector = 11;
    if (!raw_devio_xfer(pdr, sector, b,1, FALSE, TRUE))
        goto error_out;
    probePrintCheckSum(b);

    printf("=====Comparing Primary and Backup ==========\n");
	{
	dword primary, secondary;

		for (secondary = 12, primary = 0; primary < 12; primary++, secondary++)
		{
		dword i;
		int dump = 0;
			if (!raw_devio_xfer(pdr, primary, b,1, FALSE, TRUE))
				goto error_out;
			if (!raw_devio_xfer(pdr, secondary, b2,1, FALSE, TRUE))
				goto error_out;
			for(i = 0; i < sector_size; i++)
			{
				if (b[i] != b2[i])
				{
					dump = 1;
					break;
				}
			}
			if (dump)
			{
				printf("Primary %d and secondary %d are different\n", primary, secondary);
			}
			}
	}
//    printf("=========================\n");
//    printf("===== Testing FAT Table    ==========\n");
    probePrintFAT(pdr, b);
//    printf("===== Testing BAM Table    ==========\n");
    probePrintBAM(pdr, b);
//   printf("===== Testing UpCase Table ==========\n");
    probePrintUpCase(pdr, b);
//    printf("===== Testing ROOT Directory  ==========\n");
    probePrintROOTDIRECTORY(pdr, b);

    return;
error_out:
    printf("ExfatProbe Failed\n");
}


static void probePrintMBR(byte *b)
{
    PTABLE _part, *ppart;
    byte *pbuf;
    int j;
    int dump_sector = 0;


    /* Copy the table to a word alligned buffer   */
    pbuf = b;

    for (j = 0; j < 0x1be; j++)
    {
        if (*pbuf)
        {
            dump_sector = 1;
        }
    }
    /* The info starts at buf[1be] */
    /* Don't use sizeof here since the structure does not pack to exact size */
    printf("MBR Partition Area = %s\n", probeFormatHexString((b+446), 16));
	printf("MBR Partition Area = %s\n", probeFormatHexString((b+462), 16));
	printf("MBR Partition Area = %s\n", probeFormatHexString((b+478), 16));
	printf("MBR Partition Area = %s\n", probeFormatHexString((b+494), 16));
	printf("MBR Partition Area = %s\n", probeFormatHexString((b+510), 2));

    copybuff(&_part, (b+0x1be), 0x42);
    ppart = (PTABLE *) &_part;
    printf("Partition Signature == %X \n", to_WORD ((byte *) &ppart->signature));
    for (j = 0; j < 4; j++)
    {
        if (ppart->ents[j].boot ||
            ppart->ents[j].s_head ||
            ppart->ents[j].s_cyl ||
            ppart->ents[j].p_typ ||
            ppart->ents[j].e_head ||
            ppart->ents[j].e_cyl ||
            ppart->ents[j].r_sec ||
            ppart->ents[j].p_size)
        {
            printf("Partition Number ===%X\n", j);
			printf("  boot                             = %X\n", b+((j*16) + 0));
            printf("  s_head                           = %X\n", b+((j*16) + 1));
			{
				byte sector;
				word s_cyl;
				sector = b[(j*16) + 2] & 0x3f;
		        printf("  s_sector                           = %X\n", sector);
				s_cyl = b[(j*16) + 2] & 0xc0;
				s_cyl <<= 8;
				s_cyl |= b[(j*16) + 3];
		        printf("  s_cyl                           = %X\n", s_cyl);
			}
            printf("  boot                             = %X\n",  ppart->ents[j].boot);
            printf("  s_head                           = %X\n",  ppart->ents[j].s_head);
            printf("  s_cyl                            = %X\n",  to_WORD ((byte *) &ppart->ents[j].s_cyl));
            printf("  p_typ                            = %X\n",  ppart->ents[j].p_typ);
            printf("  e_head                           = %X\n",  ppart->ents[j].e_head);
            printf("  e_cyl                            = %X\n",  to_WORD ((byte *) &ppart->ents[j].e_cyl));
            printf("  r_sec                            = %X\n",  to_DWORD ((byte *) &ppart->ents[j].r_sec));
            printf("  p_size                           = %X\n",  to_DWORD ((byte *) &ppart->ents[j].p_size));
        }
    }
    if (dump_sector)
    {
        printf("MBR Contains addition data \n");
        probeDumpSector(b);
    }
    printf("=========================\n");
}


static void probePrintBPB(DDRIVE *pdr, byte *b)
{
int bp;

    printf("  BPB Signature                     = %s\n", probeFormatHexString(&b[0], 3));
    printf("  FSNAME (EXFAT   )4558464154202020 = %s\n", probeFormatHexString(&b[3], 8));
    printf("exFat BPB Data area at offset 72 \n");
    {
    ddword VolumeLength,PartitionOffset;
    to_DDWORD(&VolumeLength, &b[72]);                /* (BP 72 to 79) */
    to_DDWORD(&PartitionOffset, &b[64]);            /* (BP 64 to 71) */
    printf("  PartitionOffset                  = %s %d\n", probeFormatHexString(&b[64], 8), (dword) PartitionOffset);
    printf("  VolumeLength                     = %s %d\n", probeFormatHexString(&b[72], 8), (dword) VolumeLength);
    }
    printf("  FatOffset                        = %s %d\n", probeFormatHexString(&b[80], 4), to_DWORD(&b[80])); /* (BP 80 to 83) */
    printf("  FatLength                        = %s %d\n", probeFormatHexString(&b[84], 4),to_DWORD(&b[84])); /* (BP 84 to 87) */
    printf("  ClusterHeapOffset                = %s %d\n", probeFormatHexString(&b[88], 4),to_DWORD(&b[88])); /* (BP 88 to 91) */
    printf("  ClusterCount                     = %s %d\n", probeFormatHexString(&b[92], 4),to_DWORD(&b[92])); /* (BP 92 to 95) */
    printf("  FirstClusterOfRootDirectory      = %s %d\n", probeFormatHexString(&b[96], 4),to_DWORD(&b[96])); /* (BP 96 to 99) */

    printf("  FirstClusterOfBitMap             = %d\n", PDRTOEXFSTR(pdr)->FirstClusterOfBitMap[0]);
    printf("  FirstClusterOfUpCase             = %d\n", PDRTOEXFSTR(pdr)->FirstClusterOfUpCase);

    printf("  VolumeSerialNumber               = %s %d\n", probeFormatHexString(&b[100], 4),to_DWORD(&b[100])); /* (BP 100 to 103) */
    printf("  FileSystemRevision               = %s %d\n", probeFormatHexString(&b[104], 2),to_WORD(&b[104])); /* (BP 104 and 105) */
    printf("  VolumeFlags                      = %s %d\n", probeFormatHexString(&b[106], 2),to_WORD(&b[106])); /* (BP 106 and 107) */
    printf("  BytesPerSectorShift              = %s %d\n", probeFormatHexString(&b[108], 1),b[108]);             /* (BP 108) */
    printf("  SectorsPerClusterShift           = %s %d\n", probeFormatHexString(&b[109], 1),b[109]);             /* (BP 109) */
    printf("  NumberOfFats                     = %s %d\n", probeFormatHexString(&b[110], 1),b[110]);             /* (BP 110) */
    printf("  DriveSelect                      = %s %d\n", probeFormatHexString(&b[111], 1),b[111]);             /* (BP 111) */
    printf("  PercentInUse                     = %s %d\n", probeFormatHexString(&b[112], 1),b[112]);             /* (BP 112) */
    printf("  Boot Sig                         = %x %x\n", b[510],b[511]);             /* (BP 112) */



    printf("Raw BPB Data\n");
    probeDumpSector(b);
    printf("=========================\n");

}


static void probePrintExtendedBootSectors(byte *b, dword sector)
{
int bp;
int dump = 0;

    if (b[510] != 0x55 || b[511] != 0xaa)
    {
        printf("Extended Sector Unknown signature %d %X %X \n", sector, b[510], b[511]);
        dump = 1;
    }

    for(bp = 0; bp < 510; bp++)
    {
        if (b[bp] != 0)
        {
            printf("Extended Sector (%d) contains data\n", sector);
            dump = 1;
            break;
        }
    }

    if (dump)
    {
        probeDumpSector(b);
        printf("=========================\n");
    }

}
static void probePrintOemParameters(byte *b)
{
int bp;
int dump = 0;


    for(bp = 0; bp < 512; bp++)
    {
        if (b[bp] != 0)
        {
            printf("OEM Parameters contain data\n");
            dump = 1;
            break;
        }
    }

    if (dump)
    {
        probeDumpSector(b);
        printf("=========================\n");
    }
    else
        printf("OEM Parameters are empty\n");
}

static void probeDumpSector(byte *b)
{
    int row, column;
    for (row = 0; row < (int)sector_size/32; row++)
    {
        printf("(% 3.3d) ", row*32);
        for (column= 0; column < 32; column++)
            printf("%2.2x", b[row*32+column]);
        printf("\n");
    }
};

static int probeTestSectorData(byte *b)
{
    int i;
    int highest_non_zero = -1;
    for (i = 0; i < (int)sector_size; i++)
	{
    	if (b[i])
    		highest_non_zero = i;
    }

    return(highest_non_zero);
}

static void probeDumpSectorData(byte *b)
{
    int row, column;
    int highest_non_zero = probeTestSectorData(b);


    if (highest_non_zero == -1)
	{
		return;
	}
    for (row = 0; row < (int)sector_size/32; row++)
    {
        if (row*32 > highest_non_zero)
			break;
        printf("(% 3.3d) ", row*32);
        for (column= 0; column < 32; column++)
            printf("%2.2x", b[row*32+column]);
        printf("\n");
    }
};


static void probePrintCheckSum(byte *b)
{
dword i, *pdw0,*pdw1;
int dump = 0;
  pdw1 = pdw0 = (dword *) b;
  for (i = 0; i < sector_size/4; i++)
  {
  	if (*pdw1++ != *pdw0)
	{
		dump = 1;
		break;
	}
  }
    if (dump)
    {
        printf(" CheckSum Sector errors=========\n");
        probeDumpSector(b);
        printf("=========================\n");
    }
    printf("CheckSum OK == %2.2x%2.2x%2.2x%2.2x\n", b[0],b[1],b[2],b[3]);
}
static void probePrintReserved(byte *b)
{
dword i, *pdw0;
int dump = 0;
  pdw0 = (dword *) b;
  for (i = 0; i < sector_size/4; i++)
  {
  	if (*pdw0++ != 0)
	{
		dump = 1;
		break;
	}
  }
    if (dump)
    {
        printf(" Reserved Sector Errors=========\n");
        probeDumpSector(b);
        printf("=========================\n");
    }
    printf(" Reserved Sector Okay =========\n");
}

static void probePrintUpCase(DDRIVE *pdr, byte *b)
{
	dword 	bytenumber, bytenumber_in_sector, sector, cluster, sector_offset_in_cluster,sectors_per_cluster;
	byte    *pStd = (byte *)&cStandarducTableCompressed[0];

	cluster = PDRTOEXFSTR(pdr)->FirstClusterOfUpCase;
	PDRTOEXFSTR(pdr)->FirstClusterOfUpCase;
	bytenumber_in_sector = sector_size + 1;
	sector_offset_in_cluster = 0;
	sectors_per_cluster = (dword) pdr->drive_info.secpalloc;
	sector = 0;
	for (bytenumber = 0; bytenumber < PDRTOEXFSTR(pdr)->SizeOfUpcase; )
	{
		if (bytenumber_in_sector >= sector_size)
		{
			if (sector != 0)
				sector_offset_in_cluster++;
			if (sector_offset_in_cluster >= sectors_per_cluster)
			{
				int error = 0;
				cluster = preboot_pcclnext(pdr, cluster, &error);
				if (error)
				{
       				goto error_out;
				}
				if (!cluster)
				{
					printf("Unexpected values in UpCaseTable cluster chain\n");
       				goto dump_chain;
				}
			}

			sector = pc_cl2sector(pdr, cluster)+sector_offset_in_cluster;
			if (!raw_devio_xfer(pdr, sector, b,1, FALSE, TRUE))
			{
        		goto error_out;
			}
		}
		for (bytenumber_in_sector = 0; bytenumber_in_sector < sector_size; bytenumber_in_sector++)
		{
			if (bytenumber >= PDRTOEXFSTR(pdr)->SizeOfUpcase)
				break;
			if (b[bytenumber_in_sector] != pStd[bytenumber++])
			{
				printf("Unexpected values in UpCaseTable at offset %d \n", bytenumber);
				goto check_size;
			}
		}
	}
check_size:
	if (PDRTOEXFSTR(pdr)->SizeOfUpcase != STANDARD_UCTABLE_SIZE)
	{
			printf("Unexpected UpCaseTable Size %d \n", PDRTOEXFSTR(pdr)->SizeOfUpcase);
	}
dump_chain:
	probePrintClusterChain(pdr,b,"UpCase Cluster Chain :",  PDRTOEXFSTR(pdr)->FirstClusterOfUpCase);
	return;
error_out:
	printf("Error\n");
	return;

}
static void probePrintBAM(DDRIVE *pdr, byte *b)
{
	dword 	bamsizeclusters,cl, cluster, sector, bamsizebytes, sector_offset_in_cluster,sectors_per_cluster;

	cluster = PDRTOEXFSTR(pdr)->FirstClusterOfBitMap[0];
	sector_offset_in_cluster = 0;
	sectors_per_cluster = (dword) pdr->drive_info.secpalloc;
	sector = 0;

	bamsizebytes = PDRTOEXFSTR(pdr)->SizeOfBitMap[0];

	bamsizeclusters = 	(bamsizebytes + (sectors_per_cluster * sector_size) -1)/(sectors_per_cluster * sector_size);
	for (cl = 0; cl < bamsizeclusters; cl++)
	{
		for (sector_offset_in_cluster = 0; sector_offset_in_cluster < (dword) pdr->drive_info.secpalloc; sector_offset_in_cluster++)
		{
			sector = pc_cl2sector(pdr, cluster)+sector_offset_in_cluster;
			if (!raw_devio_xfer(pdr, sector, b,1, FALSE, TRUE))
				goto error_out;
			if (probeTestSectorData(b) != -1)
			{
				printf("Bam sector offset (%d) contains:\n",sector);
				probeDumpSectorData(b);
			}
		}
		if (cl+1 < bamsizeclusters)
		{
			int error = 0;
			cluster = preboot_pcclnext(pdr, cluster, &error);
			if (error)
			{
    			goto error_out;
			}
			if (!cluster)
			{
				printf("Unexpected values in BAM cluster chain\n");
				goto dump_chain;
			}
		}
	}

check_size:
	printf("Printf (BAM Size clusters  : %d \n", bamsizeclusters);
	printf("Printf (BAM Size bytes)    : %d \n", bamsizebytes);
	printf("Printf (BAM Size bits)     : %d \n", 8*bamsizebytes);
	printf("Printf exFAT cluster count : %d \n", PDRTOEXFSTR(pdr)->ClusterCount);
dump_chain:
	probePrintClusterChain(pdr,b,"BAM Cluster Chain :",  PDRTOEXFSTR(pdr)->FirstClusterOfBitMap[0]);
	return;
error_out:
	printf("Error\n");
	return;
}


static void probePrintFAT(DDRIVE *pdr, byte *b)
{
	dword 	sector_offset_in_fat, sector;

	printf("Printf (FAT Offset)        : %d \n", PDRTOEXFSTR(pdr)->FatOffset);
	printf("Printf (FAT Size sectors)  : %d \n", PDRTOEXFSTR(pdr)->FatLength);
	printf("Printf (FAT Size entries)  : %d \n", PDRTOEXFSTR(pdr)->FatLength*(sector_size/4));
	printf("Printf exFAT cluster count : %d \n", PDRTOEXFSTR(pdr)->ClusterCount);


	for (sector_offset_in_fat = 0; sector_offset_in_fat < PDRTOEXFSTR(pdr)->FatLength; sector_offset_in_fat++)
	{
		sector = PDRTOEXFSTR(pdr)->FatOffset+sector_offset_in_fat;
		if (!raw_devio_xfer(pdr, sector, b,1, FALSE, TRUE))
			goto error_out;

		/* Uncomment probeTestSectorData(b) != -1 to dump all sectors.
		   Note: quikformat only clears the first. */
		if (sector_offset_in_fat == 0 /* || probeTestSectorData(b) != -1 */)
		{
			printf("FAT sector offset (%d) contains:\n",sector_offset_in_fat);
			probeDumpSectorData(b);
		}
	}
	printf("=======================\n");
	return;
error_out:
	printf("Error\n");
	return;
}


static void probePrintROOTDIRECTORY(DDRIVE *pdr, byte *b)
{
	dword 	cl, cluster, sector, sector_offset_in_cluster,sectors_per_cluster;

	cluster = PDRTOEXFSTR(pdr)->FirstClusterOfRootDirectory;
	sector_offset_in_cluster = 0;
	sectors_per_cluster = (dword) pdr->drive_info.secpalloc;
	sector = 0;

	printf("Root directory\n");

	for (cl = 0; cluster != 0; cl++)
	{
		for (sector_offset_in_cluster = 0; sector_offset_in_cluster < (dword) pdr->drive_info.secpalloc; sector_offset_in_cluster++)
		{
			sector = pc_cl2sector(pdr, cluster)+sector_offset_in_cluster;
			if (!raw_devio_xfer(pdr, sector, b,1, FALSE, TRUE))
				goto error_out;
			if (probeTestSectorData(b) != -1)
			{
				printf("Root sector sector offset (%d) contains:\n", (cl*sectors_per_cluster) + sector_offset_in_cluster);
				probeDumpSectorData(b);
			}
		}
		{
			int error = 0;
			cluster = preboot_pcclnext(pdr, cluster, &error);
			if (error)
			{
    			goto error_out;
			}
			if (!cluster)
			{
				goto dump_chain;
			}
		}
	}
dump_chain:
	probePrintClusterChain(pdr,b,"Root Cluster Chain :",  PDRTOEXFSTR(pdr)->FirstClusterOfRootDirectory);
	return;
error_out:
	printf("Error\n");
	return;
}


static void probePrintClusterChain(DDRIVE *pdr, byte *b, char *prompt, dword cluster)
{
	int error = 0, checkme = 0;

	if (!cluster)
		printf("%s: [No cluster chain]\n", prompt);
	else
		printf("%s: %d", prompt, cluster);
	while(cluster)
	{
		cluster = preboot_pcclnext(pdr, cluster, &error);
		if (error)
		{
			printf("\nIO error \n");
       		return;
		}
		if (!cluster)
		{
			printf(",%X\n", cluster);
			break;
		}
		else
			printf(",%d", cluster);
		checkme++;
		if ((checkme % 32) == 0)
			printf("\n");
		if (checkme > 256)
		{
			printf("\n Chain > 250 clusters.. (not expected)\n");
			break;
		}
	}
}


static char *probeFileAttribToName(word Attribute);
static void probeShowEntryDetail(byte *p);
static char *probeEntryTypeToName(byte EntryType);
static char *probesecondaryAttribToName(byte Attribute);



void probePrintDirectoryBlock(DDRIVE *pdr, byte *b, dword cluster, dword sector, int nsectors)
{
int i;
struct exfatdosinode *exi;
char *name;
dword index;
int detail_level = 0;

    /* Works for the first cluster then wraps */
    index = (sector * pdr->drive_info.inopblock);
    for (i = 0; i < pdr->drive_info.inopblock*nsectors; i++, b += 32, index++)
    {
        name = probeEntryTypeToName(*b);
        if (!name)
        {
            name = "Unknown type";
            printf("(%-4d) %-4d:%-4d %s (%d)\n", i, cluster, sector, name, *b);      /* Utility, normally off enabled by DEBUG_EXFAT_PROBE_ROOT */
        }
        else
        {
            printf("(%-4d) %-4d:%-4d %s\n", i, cluster, sector, name);
             probeShowEntryDetail(b);
        }
        if (*b == EXFAT_DIRENTTYPE_EOF)
        {
            break;
        }
        exi = (struct exfatdosinode *) b;
    }
}

static void probeShowEntryDetail(byte *p)
{
byte EntryType = *p;
BOOLEAN retval = TRUE;
        switch (EntryType)  {
        case EXFAT_DIRENTTYPE_ALLOCATION_BITMAP:
        case EXFAT_DIRENTTYPE_UPCASE_TABLE:
            printf("    No details for this type yet\n");
            break;
        case EXFAT_DIRENTTYPE_VOLUME_LABEL:
            printf("    Volume: %11.11S\n", (p+2));
            break;
        case EXFAT_DIRENTTYPE_FILE:
        {
            EXFATFILEENTRY *pf = (EXFATFILEENTRY *) p;
            printf("    Secondary count = %d (%s)\n", pf->SecondaryCount, probeFileAttribToName(pf->FileAttributes) );
            break;
        }
        case EXFAT_DIRENTTYPE_STREAM_EXTENSION:
        {
            EXFATSTREAMEXTENSIONENTRY *pe = (EXFATSTREAMEXTENSIONENTRY *) p;
            printf("    NameLen:%-4d DataLen:%-4d VDataLen:%-4d FirstCl:%-4d (%s)\n", (dword)pe->NameLen, (dword)pe->DataLength, (dword)pe->ValidDataLength, pe->FirstCluster, probesecondaryAttribToName(pe->GeneralSecondaryFlags));
            break;
        }
        case EXFAT_DIRENTTYPE_FILE_NAME_ENTRY:
            printf("    Name: %15.15S\n", (p+2));
            break;
        case EXFAT_DIRENTTYPE_EOF:
        case EXFAT_DIRENTTYPE_GUID:
        case EXFAT_DIRENTTYPE_VENDOR_EXTENSION:
        case EXFAT_DIRENTTYPE_VENDOR_ALLOCATION:
        default:
            printf("    No details for this type\n");
            break;
        }
}

static char *probeEntryTypeToName(byte EntryType)
{
char *v;
BOOLEAN retval = TRUE;
        switch (EntryType)  {
        case EXFAT_DIRENTTYPE_EOF:
            v = "EXFAT_DIRENTTYPE_EOF";
            break;
        case EXFAT_DIRENTTYPE_ALLOCATION_BITMAP:
            v = "EXFAT_DIRENTTYPE_ALLOCATION_BITMAP";
            break;
        case EXFAT_DIRENTTYPE_UPCASE_TABLE:
            v = "EXFAT_DIRENTTYPE_UPCASE_TABLE";
            break;
        case EXFAT_DIRENTTYPE_VOLUME_LABEL:
            v = "EXFAT_DIRENTTYPE_VOLUME_LABEL";
            break;
        case EXFAT_DIRENTTYPE_FILE:
            v = "EXFAT_DIRENTTYPE_FILE";
            break;
        case EXFAT_DIRENTTYPE_GUID:
            v = "EXFAT_DIRENTTYPE_GUID";
            break;
        case EXFAT_DIRENTTYPE_STREAM_EXTENSION:
            v = "EXFAT_DIRENTTYPE_STREAM_EXTENSION";
            break;
        case EXFAT_DIRENTTYPE_FILE_NAME_ENTRY:
            v = "EXFAT_DIRENTTYPE_FILE_NAME_ENTRY";
            break;
        case EXFAT_DIRENTTYPE_VENDOR_EXTENSION:
            v = "EXFAT_DIRENTTYPE_VENDOR_EXTENSION";
            break;
        case EXFAT_DIRENTTYPE_VENDOR_ALLOCATION:
            v = "EXFAT_DIRENTTYPE_VENDOR_ALLOCATION";
            break;
        default:
            v = 0;
            break;
        }
        return(v);
}

static char *probesecondaryAttribToName(byte Attribute)
{
char *v;
        if ((Attribute & 3) == 3)
            v = "AllocPoss|NoFatCh";
        else if ((Attribute & 3) == 2)
            v = "NoFatChain";
        else if ((Attribute & 3) == 1)
            v = "AllocPoss|HasFatCh";
        else
            v = "AllocPoss|HasFatCh";
        return(v);


}
static char *probeFileAttribToName(word Attribute)
{
char *v;
        if (Attribute & 0x10)
            v = "Directory";
        else
            v = "Directory Normal file";
        return(v);
}
#endif /* (DEBUG_EXFAT_PROBE_ROOT) */
struct ex_chk_dirent {
	dword sector;
	int   index;
#define CHK_IS_META_DIRENT    1
#define CHK_IS_ROOT			  2
#define CHK_HAS_CHAIN		  4
#define CHK_IS_DIR			  8
#define CHK_LOOP_CHAIN		 16
#define CHK_SHORT_CHAIN		 32
#define CHK_LONG_CHAIN		 64
#define CHK_CROSSED_CHAIN	 128
#define CHK_UNTERM_CHAIN	 256
#define CHK_CHAIN_HASFREECLUSTERS 512
#define CHK_ERROR_MASK (CHK_LOOP_CHAIN||CHK_SHORT_CHAIN|CHK_LONG_CHAIN|CHK_CROSSED_CHAIN|CHK_UNTERM_CHAIN|CHK_CHAIN_HASFREECLUSTERS)







	dword dirent_check_flags;
	dword dirent_first_cluster;
	dword dirent_length_clusters;
	REGION_FRAGMENT *pFragments;
	struct ex_chk_dirent *pnext;
};
static struct ex_chk_dirent *pCurrentCheck,*pRootCheck;
static DDRIVE *ex_chk_pdr;
static dword chk_count_cluster_range_in_record(struct ex_chk_dirent *Check1,dword start_cluster, dword end_cluster);


/* scan the ex_chk_dirent list
	Check integrety of chain/nochain condition
	Make sure clusters in the ex_chk_dirent list are in the bam
	Make sure clusters in one ex_chk_dirent are not in other ex_chk_dirent entries
*/

/* Allocate a fragment from the freelist and initialize start, end an pnext fields */
static REGION_FRAGMENT *chkdsk_fraglist_frag_alloc(dword frag_start,dword frag_end)
{
REGION_FRAGMENT *pf;
    pf = (REGION_FRAGMENT *) rtfs_port_malloc(sizeof(*pf));

	ERTFS_ASSERT(pf)

    if (pf)
    {
        pf->start_location = frag_start;
        pf->end_location = frag_end;
        pf->pnext = 0;
    }
     return(pf);
}


/* Return all fragments in a list to fragment free pool */
static void chkdsk_fraglist_free_list(REGION_FRAGMENT *pf)
{
REGION_FRAGMENT *pfnext;
	while (pf)
    {
		pfnext  = pf->pnext;
		rtfs_port_free(pf);
		pf=pfnext;
    }
}

static dword chkdsk_getfrag(DDRIVE *pdr, dword start_cluster, dword *pfrag_len, int *error)
{
	dword current_sector,cluster,next_cluster,fat_sector, sector, fat_index,ret_val;
	int clusters_per_sector;
    BLKBUFF *buf;
    BLKBUFF bbuf_scratch;   /* Used by pc_sys_sector build a buffer structure with core from the driver level */
    byte *b;

	*error = 0;
	ret_val = 0;
	*pfrag_len=1;
	cluster=start_cluster;
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
	current_sector = 0xffffffff;
	for (;;)
	{
		fat_sector = cluster/clusters_per_sector;
		fat_index  = cluster%clusters_per_sector;
		sector = fat_sector + PDRTOEXFSTR(pdr)->FatOffset;

		if (current_sector!=sector && !raw_devio_xfer(pdr, sector, b,1, FALSE, TRUE))
		{
#if (DEBUG_EXFAT_VERBOSE)
   			rtfs_print_one_string((byte *)"chkdsk: Error reading sector: ", 0);
   			rtfs_print_long_1(sector,PRFLG_NL);
#endif
			rtfs_set_errno(PEIOERRORREAD, __FILE__, __LINE__);
			*error = 1;
			goto done;
		}
		current_sector=sector;
		next_cluster = to_DWORD(b+(fat_index*4));
		if (next_cluster==cluster+1)
		{
			*pfrag_len +=1;
			cluster += 1;
		}
		else
		{
			ret_val = next_cluster;
			break;
		}

	}
done:
    pc_free_sys_sector(buf);
    return(ret_val);
}

static REGION_FRAGMENT * chkdsk_getfragchain(DDRIVE *pdr, dword start_cluster, dword *perror_returns)
{
	int is_error;
	dword cluster,next_cluster;
	dword frag_len;
	REGION_FRAGMENT *prev_pf,*root_pf;

	*perror_returns=0;
	prev_pf=root_pf=0;
	cluster=start_cluster;
	for(;;)
	{
	REGION_FRAGMENT *pf;

		next_cluster=chkdsk_getfrag(pdr, cluster, &frag_len, &is_error);
		if (is_error)
		{
			//*perror_returns=;
			return 0;
		}
		else
		{
			pf=chkdsk_fraglist_frag_alloc(cluster,cluster+frag_len-1);
			if (!root_pf)
				root_pf=pf;
			if (prev_pf)
				prev_pf->pnext=pf;
			prev_pf=pf;
		 	if (next_cluster == EXFATEOFCLUSTER)
				break;
			if (next_cluster < 2 || next_cluster > pdr->drive_info.maxfindex)
			{
				// *perror_returns= xxx
				break;
			}
			cluster=next_cluster;
		}
	}
	return root_pf;
}


static int chkdsk_callback(byte *path, DSTAT *d)
{
    DROBJ *pD;
	DDRIVE *pdr;
	struct ex_chk_dirent *pCheck;

	pCheck=(struct ex_chk_dirent *)rtfs_port_malloc(sizeof(*pCheck));
	rtfs_memset(pCheck, 0, sizeof(*pCheck));
	if (!pCheck)
		return -1;
	if (pCurrentCheck)
		pCurrentCheck->pnext=pCheck;
	else
	{
		pRootCheck=pCheck;
	}
	pCurrentCheck=pCheck;

	pD=(DROBJ *)d->pobj;
	ERTFS_ASSERT(pD)
	ERTFS_ASSERT(pD->finode)
	pdr=pD->pdrive;
	ex_chk_pdr=pdr;
	if (pD->finode)
	{
		pCheck->dirent_first_cluster = 	((dword)pD->finode->fclusterhi<<16)|pD->finode->fcluster;
		pCheck->dirent_length_clusters=	(dword)((pD->finode->fsizeu.fsize64+pdr->drive_info.bytespcluster-1)>>pdr->drive_info.log2_bytespalloc);

		if ((pD->finode->exfatinode.GeneralSecondaryFlags&EXFATNOFATCHAIN)==0)
		{
			dword error_returns;
			pCheck->dirent_check_flags |= CHK_HAS_CHAIN;
			pCheck->pFragments= chkdsk_getfragchain(pdr, pCheck->dirent_first_cluster,&error_returns);
			pCheck->dirent_check_flags |= error_returns;
		}
		if (pD->finode->fattribute&ADIRENT)
			pCheck->dirent_check_flags |= CHK_IS_DIR;
		pCheck->sector   =	pD->finode->my_block;
		pCheck->index    =	pD->finode->my_index;
	}
    RTFS_PRINT_STRING_1((byte *)path, PRFLG_NL);
    return(0);
}

/*
<TEST>  Procedure:   doenum() - Perform a recursive directory enumeration using pc_enumerate() and display file information
<TEST>   Invoke by typing "ENUM" in the command shell
<TEST>
*/
static BOOLEAN chk_record_clusters_overlap(struct ex_chk_dirent *pInnerCheck,struct ex_chk_dirent *pCheck);
static dword chk_count_cluster_range_in_record(struct ex_chk_dirent *Check1,dword start_cluster, dword end_cluster);
BOOLEAN pc_exfat_check_disk(byte *driveid, DDRIVE *pdr, CHKDISK_STATS *pchkstat)
{
    byte path[4],*pmatch;
    byte *scratch_buffer1;
    byte *scratch_buffer2;
    byte *scratch_buffer3;
    byte *scratch_buffer4;
    word match_flags;

	path[0]=driveid[0];
	path[1]=(byte)':';
	path[2]=(byte)'\\';
	path[3]=0;
    pmatch = (byte *)"*";


    scratch_buffer1 = (byte*)rtfs_port_malloc(512);
    scratch_buffer2 = (byte*)rtfs_port_malloc(512);
    scratch_buffer3 = (byte*)rtfs_port_malloc(512);
    scratch_buffer4 = (byte*)rtfs_port_malloc(512);

    if (!(scratch_buffer1 && scratch_buffer2 && scratch_buffer3 && scratch_buffer4))
    {
         goto done;
    }
    match_flags = 0;
    match_flags |= MATCH_DIR;
    match_flags |= MATCH_FILES;
    match_flags |= MATCH_VOL;
	match_flags |= MATCH_SYSSCAN;

	pRootCheck=pCurrentCheck=0;
	ex_chk_pdr=pdr;
    pc_enumerate(
        scratch_buffer1,
        scratch_buffer2,
        scratch_buffer3,
        scratch_buffer4,
        path,
        match_flags,
        pmatch,
        128,
        chkdsk_callback);
	/* Scan entries make sure all clusters are allocated from the bam  */
	if (pRootCheck)
	{
	 struct ex_chk_dirent *pCheck=pRootCheck;
		while (pCheck)
		{
			int v;
			REGION_FRAGMENT *pf;
			dword start_cluster,required_length,ncontig, n_alloced;
			n_alloced=0;

			pf=pCheck->pFragments;
			if (pf)
				required_length=pc_fraglist_count_clusters(pf,0);
			else
				required_length=pCheck->dirent_length_clusters;
			do
			{
				if (pf)
					start_cluster=pf->start_location;
				else
					start_cluster=pCheck->dirent_first_cluster;
				do
				{
					v = bam_get_bits(ex_chk_pdr, start_cluster, &ncontig);
					ERTFS_ASSERT(ncontig)
					if (!ncontig)
						break; /* shouldn not happen */
					if (v==1)
					{
						n_alloced+=ncontig;
						if (n_alloced>=required_length)
							break;
						start_cluster += ncontig;
					}
				} while (v==1);
				if (pf)
					pf=pf->pnext;
			} while (pf);



			if (n_alloced < required_length)
			{
				pCheck->dirent_check_flags |= CHK_CHAIN_HASFREECLUSTERS;
			}

			pCheck=pCheck->pnext;
		}
	}
	/* Scan Bam and identify regions that are allocate in the bam but not in a know fragment */
	{
		dword start_cluster,cluster,ncontig_this_section;

		/* Scann all clusters, skipping the first cluster of the root, the bam and the upcase table */
		start_cluster = 1+pdr->exfatextension.FirstClusterOfUpCase+(pdr->exfatextension.SizeOfUpcase+pdr->drive_info.bytespcluster-1)/pdr->drive_info.bytespcluster;

		for(cluster = start_cluster;cluster < ex_chk_pdr->drive_info.maxfindex;cluster=cluster+ncontig_this_section)
		{
		dword _ncontig,ncontig,_cluster;
		int v,v_this_string;
			ncontig=0;
			v_this_string=0;


			for(_cluster=cluster;_cluster<ex_chk_pdr->drive_info.maxfindex;_cluster+=_ncontig)
			{
				v = bam_get_bits(ex_chk_pdr, _cluster, &_ncontig);
	       		if (!_ncontig)	/* Shouldn't happen but don't get stuck in a loop */
					break;
				if (!ncontig)
				{
					ncontig=_ncontig;
					v_this_string=v;
				}
				else if (v_this_string==v)
					ncontig+=_ncontig;
				else
					break;
			}
       		if (!ncontig)	/* Shouldn't happen but don't get stuck in a loop */
				break;
			ncontig_this_section=ncontig;
   			if (v_this_string == 1)		/* Found ncontig in-use clusters starting at index */
			{
			struct ex_chk_dirent *pCheck=pRootCheck;
				while (ncontig&&pCheck)
				{
					dword n;
					n=chk_count_cluster_range_in_record(pCheck,cluster,cluster+ncontig_this_section-1);
					if (n>ncontig)
						ncontig=0;
					else
						ncontig-=n;
					pCheck=pCheck->pnext;
				}
				if (ncontig)
				{
					pchkstat->n_lost_clusters += ncontig;
					pchkstat->n_lost_chains += 1;
					pchkstat->has_errors+=1;
					printf("%d clusters not accounted for in region %d to %d\n",ncontig,cluster,cluster+ncontig_this_section-1);
				}
			}
			else
			{
				pchkstat->n_free_clusters += ncontig;
			}
		}
	}

	/* Scan entries make sure no clusters are used by more than one object */
	if (pRootCheck)
	{
	 struct ex_chk_dirent *pCheck=pRootCheck;
		while (pCheck)
		{
			REGION_FRAGMENT *pf;
			struct ex_chk_dirent *pInnerCheck;
			dword start_cluster,required_length;

			pf=pCheck->pFragments;
			if (pf)
			{
				start_cluster=pf->start_location;
				required_length=pc_fraglist_count_clusters(pf,0);
			}
			else
			{
				start_cluster=pCheck->dirent_first_cluster;
				required_length=pCheck->dirent_length_clusters;
			}
			pInnerCheck=pRootCheck;
			while (pInnerCheck)
			{
				if (pInnerCheck!=pCheck)
				{
					if(chk_record_clusters_overlap(pInnerCheck,pCheck))
					{
						pInnerCheck->dirent_check_flags |= CHK_CROSSED_CHAIN;
						pCheck->dirent_check_flags |= CHK_CROSSED_CHAIN;
					}
				}
				pInnerCheck=pInnerCheck->pnext;
			}
			pCheck=pCheck->pnext;
		}
	}
	/* Report ptoblems  */
	if (pRootCheck)
	{
	 struct ex_chk_dirent *pCheck=pRootCheck;
		while (pCheck)
		{
			if (pCheck->dirent_check_flags&CHK_ERROR_MASK)
			{
				pchkstat->has_errors+=1;
				if (pCheck->dirent_check_flags&CHK_CROSSED_CHAIN)
					pchkstat->n_crossed_chains += 1;
				printf("Error found error == %d\n",(pCheck->dirent_check_flags&CHK_ERROR_MASK));
			}
			pCheck=pCheck->pnext;
		}
	}
	/* Get usage statistics */
	if (pRootCheck)
	{
	 struct ex_chk_dirent *pCheck=pRootCheck;
		while (pCheck)
		{
			if (pCheck->dirent_check_flags&CHK_IS_DIR)
			{
				pchkstat->n_user_directories += 1;
				pchkstat->n_dir_clusters += pCheck->dirent_length_clusters;
			}
			else
			{
				pchkstat->n_user_files += 1;
				pchkstat->n_file_clusters +=  pCheck->dirent_length_clusters;
			}
			pCheck=pCheck->pnext;
		}
	}

	/* Free all entries */
	if (pRootCheck)
	{
	 struct ex_chk_dirent *pCheck=pRootCheck;
		while (pCheck)
		{
			struct ex_chk_dirent *pCheck_pnext=pCheck->pnext;
			if (pCheck->pFragments)
				chkdsk_fraglist_free_list(pCheck->pFragments);
			rtfs_port_free(pCheck);
			pCheck=pCheck_pnext;
		}
	}

done:
    if (scratch_buffer1)
        rtfs_port_free(scratch_buffer1);
    if (scratch_buffer2)
        rtfs_port_free(scratch_buffer2);
    if (scratch_buffer3)
        rtfs_port_free(scratch_buffer3);
    if (scratch_buffer4)
        rtfs_port_free(scratch_buffer4);
    return(TRUE);
}

#if (!INCLUDE_RTFS_PROPLUS)
/* Copied of source code from ProPlus library
   Find the fragment in a chain that contains "cluster"
   if "cluster" is not in the chain return 0 */
REGION_FRAGMENT *pc_fraglist_find_cluster(REGION_FRAGMENT *pfstart,REGION_FRAGMENT *pfend ,dword cluster)
{
REGION_FRAGMENT *pf;

    pf = pfstart;
    while (pf)
    {
        if (pf->start_location <= cluster && pf->end_location >= cluster)
            return(pf);
        if (pf == pfend)
            break;
        pf = pf->pnext;
     }
     return(0);
}
#endif


static BOOLEAN chk_record_clusters_overlap(struct ex_chk_dirent *Check1,struct ex_chk_dirent *Check2)
{
REGION_FRAGMENT tf1,tf2;
REGION_FRAGMENT *pf1,*pf2;

	if (Check1->pFragments)
		pf1=Check1->pFragments;
	else
	{
		pf1=&tf1;
		pf1->start_location=Check1->dirent_first_cluster;
		pf1->end_location=Check1->dirent_first_cluster+Check1->dirent_length_clusters-1;
		pf1->pnext=0;
	}
	if (Check2->pFragments)
		pf2=Check2->pFragments;
	else
	{
		pf2=&tf2;
		pf2->start_location=Check2->dirent_first_cluster;
		pf2->end_location=Check2->dirent_first_cluster+Check2->dirent_length_clusters-1;
		pf2->pnext=0;
	}

	while (pf1)
	{
		dword cluster;
		for (cluster=pf1->start_location;cluster <= pf1->end_location;cluster++)
		{
			if (pc_fraglist_find_cluster(pf2,0,cluster))
				return TRUE;
		}
		pf1=pf1->pnext;
	}
	return FALSE;
}
static dword chk_count_cluster_range_in_record(struct ex_chk_dirent *Check1,dword start_cluster, dword end_cluster)
{
REGION_FRAGMENT tf1;
REGION_FRAGMENT *pf1;
dword cluster_count = 0;

	if (Check1->pFragments)
		pf1=Check1->pFragments;
	else
	{
		pf1=&tf1;
		pf1->start_location=Check1->dirent_first_cluster;
		pf1->end_location=Check1->dirent_first_cluster+Check1->dirent_length_clusters-1;
		pf1->pnext=0;
	}

	while (pf1)
	{
		dword cluster;
		for (cluster=start_cluster;cluster<=end_cluster;cluster++)
		{
			if (pc_fraglist_find_cluster(pf1,0,cluster))
				cluster_count+=1;
		}
		pf1=pf1->pnext;
	}
	return cluster_count;
}
