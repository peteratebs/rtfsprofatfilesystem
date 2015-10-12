/*
<TEST>  Test File:   rtfscommon/source/apinandregress.c
<TEST>
<TEST>   Procedure:BOOLEAN pc_nand_regression_test()
<TEST>   Description:  Rtfs nand feature set regression test suite.
<TEST>   This subroutine tests operation of Rtfs on NAND flash media
<TEST>   The routine may be inoked by typing TESTNAND D: from the command shell
<TEST>
*/


#include "rtfs.h"

#if (!RTFS_CFG_READONLY  && INCLUDE_NAND_DRIVER) /* Excluded from build if read only or dynamic driver support disabled */


#define nand_regress_error() _nand_regress_error(__LINE__)
static BOOLEAN _nand_regress_error(long linenumber)
{
    RTFS_PRINT_STRING_1((byte *)"", PRFLG_NL);
    RTFS_PRINT_STRING_1((byte *)" nand_regress_error was called line number: ", 0); /* "regress_error was called with error" */
    RTFS_PRINT_LONG_1((dword) linenumber, PRFLG_NL);
	return(FALSE);
}

static DDRIVE   *Ntu_get_drive_structure(byte *driveid);
static BOOLEAN   Ntu_clear_mbr(byte *driveid);
static BOOLEAN   Ntu_check_eb_aligned(DDRIVE *pdr, dword sector_number);
static BOOLEAN   Ntu_check_eb_mod(DDRIVE *pdr, dword sector_count);
static BOOLEAN   Ntu_check_format_aligned(byte *driveid, BOOLEAN check_cluster_size);
static void 	 Ntu_slow_test(void);
static int   Ntu_check_eb_contents(DDRIVE *pdrive, dword sector_number, dword val, dword increment);
static int   Ntu_check_if_eb_erased(DDRIVE *pdrive, dword sector_number);
static BOOLEAN   Ntu_format_for_testing(byte *driveid, dword n_erase_blocks, dword clusterspereraseblock, unsigned char bits_per_cluster, unsigned char numfats);
static REGION_FRAGMENT *Ntu_get_fragment_list(int fd);
static PC_FILE *Ntu_get_file_structure(int fd);
static BLKBUFF *Ntu_get_file_buffer(int fd);
static byte *Ntu_get_sector_buffer(DDRIVE *pdrive);


static BOOLEAN   Ntt_format_test_1(byte *driveid);
static BOOLEAN   Ntt_format_test_2(byte *driveid);
static BOOLEAN   Ntt_cluster_placement_test(byte *driveid);
static BOOLEAN   Ntt_block_erase_test(byte *driveid);
static BOOLEAN   Ntt_file_buffering_test(byte *driveid);




BOOLEAN pc_nand_regression_test(byte *driveid) /* __api__ */
{
DDRIVE *pdrive;
RTFS_DEVI_MEDIA_PARMS lmedia_info;

    RTFS_PRINT_STRING_1((byte *)"Performing nand regression tests", PRFLG_NL);

	/* Save a local copy of media parameters */
    pdrive = Ntu_get_drive_structure(driveid);
	if (!pdrive)
	{
    	RTFS_PRINT_STRING_1((byte *)"Not a valid drive", PRFLG_NL);
		return(FALSE);
	}
	lmedia_info = *pdrive->pmedia_info;
	if (!lmedia_info.eraseblock_size_sectors)
	{
    	RTFS_PRINT_STRING_1((byte *)"Not a nand device", PRFLG_NL);
		return(FALSE);
	}
	if (!Ntu_get_sector_buffer(pdrive))
	{
   		RTFS_PRINT_STRING_1((byte *)"Need an erase block sized buffer to run the tests", PRFLG_NL);
		return(FALSE);
	}

goto current_debug_test;
current_debug_test:
	/* Test-  Check default format routine for erase block alignment */
   	RTFS_PRINT_STRING_1((byte *)"Verifying formatting routines", PRFLG_NL);
   	RTFS_PRINT_STRING_1((byte *)"    Verify format routine for erase block alignment", PRFLG_NL);
   	if (!Ntt_format_test_1(driveid))
		return(FALSE);
   	RTFS_PRINT_STRING_1((byte *)"Format erase block alignment test succeeded", PRFLG_NL);

	/* Test-  Check extended format erase option */
   	RTFS_PRINT_STRING_1((byte *)"    Verify extended format erase option", PRFLG_NL);
   	if (!Ntt_format_test_2(driveid))
		return(FALSE);
   	RTFS_PRINT_STRING_1((byte *)"Format erase option test succeeded", PRFLG_NL);
	/* Test-  Check FAT32 format for erase block alignment  */
	/* Test-  Check FAT16 format for erase block alignment  */
	/* Test-  Check FAT12 format for erase block alignment  */
   	RTFS_PRINT_STRING_1((byte *)"Verify cluster allocation erase block alignment", PRFLG_NL);
	/* Test-  Check cluster allocation behavior */
	if (!Ntt_cluster_placement_test(driveid))
		return(FALSE);
	/* Test-  Check block erase behavior during file and directory deletes */
	if (!Ntt_block_erase_test(driveid))
		return(FALSE);
	/* Test-  Check buffered file IO behavior */
   	RTFS_PRINT_STRING_1((byte *)"Verifying file buffering algorithm", PRFLG_NL);
	if (!Ntt_file_buffering_test(driveid))
		return(FALSE);

	/* Test-  Check un-buffered file IO behavior */
	/* Test-  Check block erase behavior during file delete */
	/* Test-  Check block erase behavior during directory delete */
	/* Test-  Check for FAT table erase block aligned accesses */
    return(TRUE);
}

static BOOLEAN   Ntt_format_test_1(byte *driveid)
{
	/* Test-  Check default format routine for erase block alignment */
	if (!Ntu_clear_mbr(driveid))
		return(FALSE);
	if (!pc_format_volume(driveid))
		return(FALSE);
	return(Ntu_check_format_aligned(driveid, TRUE));
}

static BOOLEAN   Ntt_format_test_2(byte *driveid)
{
RTFSFMTPARMSEX parms_ex;
DDRIVE *pdrive;
dword sector_number,first_sector_number;

	Ntu_slow_test();
	/* Check extended format erase option */
    rtfs_memset(&parms_ex, 0, sizeof(parms_ex));
	parms_ex.scrub_volume = TRUE;

	if (!Ntu_clear_mbr(driveid))
		return(FALSE);
	if (!pc_format_volume_ex(driveid, &parms_ex))
		return(FALSE);
	if (!Ntu_check_format_aligned(driveid, TRUE))
		return(FALSE);

	pdrive = Ntu_get_drive_structure(driveid);
	if (!pdrive)
		return(FALSE);

	/* Scan all erase blocks in the cluster area, verify they are erased. Skip the first erase block if FAT32 */
	first_sector_number = pdrive->drive_info.firstclblock;
	if (pdrive->drive_info.fasize == 8)
		first_sector_number += pdrive->pmedia_info->eraseblock_size_sectors;
	for (sector_number = first_sector_number; sector_number < pdrive->drive_info.numsecs; sector_number +=  pdrive->pmedia_info->eraseblock_size_sectors)
	{
		if (Ntu_check_if_eb_erased(pdrive, sector_number) != 1)
			return(FALSE);
	}
	return(TRUE);
}
#define TESTNAME0 (byte *) "TEST0.DAT"
#define FILL32DIRNAME (byte *) "FILL32.DIR"
#define TESTDIRNAME0 (byte *) "TEST0.DIR"
#define TESTDIRNAME1 (byte *) "TEST1.DIR"
#define TESTDIRNAME2 (byte *) "TEST2.DIR"
#define TESTDIRNAME3 (byte *) "TEST3.DIR"
#define TESTDIRNAME4 (byte *) "TEST4.DIR"

/* Test-  Check cluster allocation behavior */
static BOOLEAN   Ntt_cluster_placement_test(byte *driveid)
{
DDRIVE *pdrive;
int fd,bytes_in_eraseblock,bytes_in_cluster,current_errno;
dword i,file_clusters_available,clusterspereraseblock,file_eblocks_available,save_drive_operating_policy,ltemp;
BOOLEAN isfat32;

   	RTFS_PRINT_STRING_1((byte *)"Verifying default operating mode", PRFLG_NL);
   	RTFS_PRINT_STRING_1((byte *)"    Verify cluster placement", PRFLG_NL);
	/* Create a FAT 12 volume from 1000 erase blocks, put 2 clusters per erase block, 2 FATs */
	clusterspereraseblock = 2;
	isfat32 = FALSE;  /* Use later on if we test FAT32 */
	if (!Ntu_format_for_testing(driveid, 1000, clusterspereraseblock, 12, 2))
	{
		nand_regress_error();
		return(FALSE);
	}

	pdrive = Ntu_get_drive_structure(driveid);
	if (!pdrive)
		return(FALSE);
	/* Make sure default operating policy is configured */
	save_drive_operating_policy = pdrive->du.drive_operating_policy;
	ltemp = DRVPOL_NAND_SPACE_RECOVER | DRVPOL_NAND_SPACE_OPTIMIZE;
	pdrive->du.drive_operating_policy &= ~ltemp;


	/* How many clusters and erase blocks are available ?? */
	file_clusters_available = pdrive->drive_info.maxfindex - 1;
	if (isfat32)
		file_clusters_available -= 1;	/* The root uses one cluster */
	file_eblocks_available	= file_clusters_available/clusterspereraseblock;

	/* How many bytes in a cluster and erase block */
	bytes_in_cluster = (int) (pdrive->drive_info.secpalloc * pdrive->pmedia_info->sector_size_bytes);
	bytes_in_eraseblock = pdrive->pmedia_info->eraseblock_size_sectors * pdrive->pmedia_info->sector_size_bytes;

	/* First verify that we can write all sectors writing a full erase block at a time */
   	RTFS_PRINT_STRING_1((byte *)"    Verify all sectors are available if writing full erase blocks", PRFLG_NL);
    if ((fd = po_open(TESTNAME0,(word)(PO_BINARY|PO_RDWR|PO_CREAT),(word)(PS_IWRITE | PS_IREAD))) < 0)
		goto unexpected;
	for (i = 0; i < file_eblocks_available; i++)
	{ /* Note passing a null pointer so no actual data is transfered */
    	if (po_write(fd, 0, bytes_in_eraseblock) != bytes_in_eraseblock)
    	{
			nand_regress_error();
			return(FALSE);
    	}
	}

 	/* One more write should fail */
	if (po_write(fd, 0, 1) != 0)
	{
		nand_regress_error();
		return(FALSE);
	}
   	RTFS_PRINT_STRING_1((byte *)"    Verify errno settings for full condition", PRFLG_NL);
    current_errno = get_errno();
	if (current_errno != PENOEMPTYERASEBLOCKS)
	{
		nand_regress_error();
		return(FALSE);
	}
	if (po_close(fd) != 0)
		goto unexpected;
	if (!pc_unlink(TESTNAME0))
		goto unexpected;

	/* Now write the file in smaller chunks and verify proper behavior */
	/* Next verify that in the default allocation mode, writing a cluster at a time retuires one erase block per write */
   	RTFS_PRINT_STRING_1((byte *)"    Verify not all sectors available writing partial erase blocks", PRFLG_NL);
    if ((fd = po_open(TESTNAME0,(word)(PO_BINARY|PO_RDWR|PO_CREAT),(word)(PS_IWRITE | PS_IREAD))) < 0)
		goto unexpected;
	/* Verify that we can only write half the sectors writing a cluster at a time when cluster == one half erase block  */
	for (i = 0; i < file_eblocks_available; i++)
	{ /* Note passing a null pointer so no actual data is transfered */
    	if (po_write(fd, 0, bytes_in_cluster) != bytes_in_cluster)
    	{
			nand_regress_error();
			return(FALSE);
    	}
	}

	/* Check that the clusters are all on erase block boundaries */
   	RTFS_PRINT_STRING_1((byte *)"    Verify all clusters allocated on erase block boundaries", PRFLG_NL);
	{
		REGION_FRAGMENT *pf = Ntu_get_fragment_list(fd);
		dword expected_cluster = 2;
		if (isfat32)
			expected_cluster += clusterspereraseblock;
		for (i = 0; i < file_eblocks_available; i++)
		{
			if (!pf || expected_cluster != pf->start_location)
			{
				nand_regress_error();
				return(FALSE);
			}
			pf = pf->pnext;
			expected_cluster += clusterspereraseblock;
		}
	}

 	/* One more write should fail */
	if (po_write(fd, 0, 1) != 0)
	{
		nand_regress_error();
		return(FALSE);
	}
    current_errno = get_errno();
	if (current_errno != PENOEMPTYERASEBLOCKS)
	{
		nand_regress_error();
		return(FALSE);
	}

	/* The previous writes in default mode should have left one recoverable cluster per erase block */
   	RTFS_PRINT_STRING_1((byte *)"Verify DRVPOL_NAND_SPACE_RECOVER operating mode", PRFLG_NL);
	{
		dword partial_eblocks_available = file_eblocks_available;
		if (isfat32)
			partial_eblocks_available += 1;	/* The root uses one cluster, leaving one additional free cluster */

    	/* Verify that we can write the rest of the the sectors a cluster at a time in DRVPOL_NAND_SPACE_RECOVER mode when cluster == one half erase block  */
		pdrive->du.drive_operating_policy |= DRVPOL_NAND_SPACE_RECOVER;

    	for (i = 0; i < partial_eblocks_available; i++)
    	{ /* Note passing a null pointer so no actual data is transfered */
        	if (po_write(fd, 0, bytes_in_cluster) != bytes_in_cluster)
        	{
    			nand_regress_error();
    			return(FALSE);
        	}
    	}
     	/* One more write should fail and errno should be PENOSPC */
    	if (po_write(fd, 0, 1) != 0)
    	{
    		nand_regress_error();
    		return(FALSE);
    	}
    	RTFS_PRINT_STRING_1((byte *)"    Verify errno settings for full condition", PRFLG_NL);
        current_errno = get_errno();
    	if (current_errno != PENOSPC)
    	{
    		nand_regress_error();
    		return(FALSE);
    	}
		pdrive->du.drive_operating_policy = save_drive_operating_policy;
	}

	if (po_close(fd) != 0)
		goto unexpected;
	if (!pc_unlink(TESTNAME0))
		goto unexpected;

   	RTFS_PRINT_STRING_1((byte *)"Verify DRVPOL_NAND_SPACE_OPTIMIZE operating mode", PRFLG_NL);
   	RTFS_PRINT_STRING_1((byte *)"    Verify that all clusters are allocated sequentially", PRFLG_NL);
	{
	REGION_FRAGMENT *pf;
	dword expected_cluster = 2;

    	if ((fd = po_open(TESTNAME0,(word)(PO_BINARY|PO_RDWR|PO_CREAT),(word)(PS_IWRITE | PS_IREAD))) < 0)
			goto unexpected;

		if (isfat32)
			expected_cluster += 1; /* FAT32 root uses one cluster */

    	/* Verify that we can write the rest of the the sectors a cluster at a time in DRVPOL_NAND_SPACE_OPTIMIZE mode when cluster == one half erase block  */
		pdrive->du.drive_operating_policy |= DRVPOL_NAND_SPACE_OPTIMIZE;

    	for (i = 0; i < file_clusters_available; i++)
    	{ /* Note passing a null pointer so no actual data is transfered */
        	if (po_write(fd, 0, bytes_in_cluster) != bytes_in_cluster)
        	{
    			nand_regress_error();
    			return(FALSE);
        	}
        	pf = Ntu_get_fragment_list(fd);
			/* The file should contain one contiguous cluster chain starting from the beginning of the drive */
			if (!pf || pf->pnext || expected_cluster != pf->start_location || pf->end_location != (expected_cluster + i))
			{
				nand_regress_error();
				return(FALSE);
			}
    	}
     	/* One more write should fail but errno should be PENOSPC */
    	if (po_write(fd, 0, 1) != 0)
    	{
    		nand_regress_error();
    		return(FALSE);
    	}
    	RTFS_PRINT_STRING_1((byte *)"    Verify errno settings for full condition", PRFLG_NL);
        current_errno = get_errno();
    	if (current_errno != PENOSPC)
    	{
    		nand_regress_error();
    		return(FALSE);
    	}
		pdrive->du.drive_operating_policy = save_drive_operating_policy;
	}

	if (po_close(fd) != 0)
		goto unexpected;
	if (!pc_unlink(TESTNAME0))
		goto unexpected;
/* HEREHERE - Try proplus garbage collect mode */

/* HEREHERE - Note to self.. use a second partition, verify unaffected */
/* HEREHERE - Note to self.. add to the above tests validation the sectors are erase block bound */
/* HEREHERE - Try subdirectory allocations */

   	RTFS_PRINT_STRING_1((byte *)"Verify subdirectory cluster allocation algorithm", PRFLG_NL);
	{
		DIRENT_INFO direntinfo;
		dword expected_cluster = 2;


		RTFS_PRINT_STRING_1((byte *)"    Verify that all clusters are allocated sequentially", PRFLG_NL);
		if (isfat32)
		{ /* Fat32 consumes one cluster (half erase block) for the root. So normalize by creating a directory to fill the first erase block */
			if (!pc_mkdir(FILL32DIRNAME))
				goto unexpected;
			expected_cluster += clusterspereraseblock;
		}
		/* Verify subdirectory cluster is allocated on first available basis */
		if (!pc_mkdir(TESTDIRNAME0))
			goto unexpected;
		if (!pc_get_dirent_info(TESTDIRNAME0, &direntinfo))
			goto unexpected;
    	if (direntinfo.fcluster != expected_cluster)
    	{
    		nand_regress_error();
    		return(FALSE);
    	}
		/* Open a file in default mode and write one cluster, should be allocated at next erase block boundary */
    	if ((fd = po_open(TESTNAME0,(word)(PO_BINARY|PO_RDWR|PO_CREAT),(word)(PS_IWRITE | PS_IREAD))) < 0)
			goto unexpected;
       	if (po_write(fd, 0, bytes_in_cluster) != bytes_in_cluster)
			goto unexpected;
		/* Verify that the next subdirectory cluster is allocated from the rest of the first erase block (before the file cluster) */
		expected_cluster += 1;
		if (!pc_mkdir(TESTDIRNAME1))
			goto unexpected;
		if (!pc_get_dirent_info(TESTDIRNAME1, &direntinfo))
			goto unexpected;
    	if (direntinfo.fcluster != expected_cluster)
    	{
    		nand_regress_error();
    		return(FALSE);
    	}
		/* Verify that the next subdirectory cluster is allocated two clusters hence (after the file cluster, sharing its erase block) */
		expected_cluster += clusterspereraseblock;
		if (!pc_mkdir(TESTDIRNAME2))
			goto unexpected;
		if (!pc_get_dirent_info(TESTDIRNAME2, &direntinfo))
			goto unexpected;
    	if (direntinfo.fcluster != expected_cluster)
    	{
    		nand_regress_error();
    		return(FALSE);
    	}

		/* Here fill the rest of the disk, make sure we can allocate last cluster to a directory, then make sure we get proper errno */
		expected_cluster += 1;
		{
			dword free_clusters_left,free_eblocks_left;
			free_clusters_left = file_clusters_available - (expected_cluster - 2);
			free_eblocks_left = free_clusters_left/clusterspereraseblock;
			/* Fill all but the last erase block */
			for (i = 0; i < free_eblocks_left-1; i++)
			{ /* Note passing a null pointer so no actual data is transfered */
    			if (po_write(fd, 0, bytes_in_eraseblock) != bytes_in_eraseblock)
    			{
					nand_regress_error();
					return(FALSE);
    			}
			}
			/* Fill half of the last erase block */
			if (po_write(fd, 0, bytes_in_cluster) != bytes_in_cluster)
        	{
    			nand_regress_error();
    			return(FALSE);
        	}
        	RTFS_PRINT_STRING_1((byte *)"    Verify that last cluster may be allocated", PRFLG_NL);
			/* should be able to create one more directory */
        	if (!pc_mkdir(TESTDIRNAME3))
        	{
    			nand_regress_error();
    			return(FALSE);
        	}
			/* Should occupy the last cluster on the volume */
        	if (!pc_get_dirent_info(TESTDIRNAME3, &direntinfo))
				goto unexpected;
        	if (direntinfo.fcluster != pdrive->drive_info.maxfindex)
        	{
    			nand_regress_error();
    			return(FALSE);
        	}
        	RTFS_PRINT_STRING_1((byte *)"    Verify errno settings for full condition", PRFLG_NL);
			/* should get a disk full error if we try to create one more directory */
        	if (pc_mkdir(TESTDIRNAME4))
        	{
    			nand_regress_error();
    			return(FALSE);
        	}
        	current_errno = get_errno();
        	if (current_errno != PENOSPC)
        	{
    			nand_regress_error();
    			return(FALSE);
        	}
		}

	   	if (po_close(fd) != 0)
			goto unexpected;
		if (!(pc_unlink(TESTNAME0) && pc_rmdir(TESTDIRNAME0) && pc_rmdir(TESTDIRNAME1) && pc_rmdir(TESTDIRNAME2) && pc_rmdir(TESTDIRNAME3)))
			goto unexpected;
	}


   	RTFS_PRINT_STRING_1((byte *)"Placement test succeeded", PRFLG_NL);
	return(TRUE);

unexpected:
   	RTFS_PRINT_STRING_1((byte *)"Placement test failed with unexpected error", PRFLG_NL);
	nand_regress_error();
    return(FALSE);
}

static BOOLEAN   Ntbe_write_file(int fd, byte *buffer, int nwrites, int bytes_in_buffer, int nwrites_2, int bytes_in_buffer_2)
{
int i;
	for (i = 0; i < nwrites; i++)
	{
		if (po_write(fd, buffer, bytes_in_buffer) != bytes_in_buffer)
			return(FALSE);
	}
	for (i = 0; i < nwrites_2; i++)
	{
		if (po_write(fd, buffer, bytes_in_buffer_2) != bytes_in_buffer_2)
			return(FALSE);
	}
	return(TRUE);
}

/* Test-  Check block erase behavior during file and directory deletes */
static BOOLEAN   Ntt_block_erase_test(byte *driveid)
{
DDRIVE *pdrive;
int fd,bytes_in_eraseblock,bytes_in_cluster;
dword clusterspereraseblock,save_drive_operating_policy,ltemp,expected_cluster,sector_number;
BOOLEAN isfat32;

   	RTFS_PRINT_STRING_1((byte *)"Verifying block erasure on file and subdirectory deletion", PRFLG_NL);
	/* Create a FAT 12 volume from 1000 erase blocks, put 2 clusters per erase block, 2 FATs */
	clusterspereraseblock = 2;
	expected_cluster = 2;
	if (!Ntu_format_for_testing(driveid, 1000, clusterspereraseblock, 12, 2))
	{
		nand_regress_error();
		return(FALSE);
	}
	isfat32 = FALSE;  /* Use later on if we test FAT32 */

	if (isfat32)
	{ /* Fat32 consumes one cluster (half erase block) for the root. So normalize by creating a directory to fill the first erase block */
		if (!pc_mkdir(FILL32DIRNAME))
			goto unexpected;
		expected_cluster += clusterspereraseblock;
	}

	pdrive = Ntu_get_drive_structure(driveid);
	if (!pdrive)
		return(FALSE);
	/* Make sure default operating policy is configured */
	save_drive_operating_policy = pdrive->du.drive_operating_policy;
	ltemp = DRVPOL_NAND_SPACE_RECOVER | DRVPOL_NAND_SPACE_OPTIMIZE;
	pdrive->du.drive_operating_policy &= ~ltemp;

	/* How many bytes in a cluster and erase block */
	bytes_in_cluster = (int) (pdrive->drive_info.secpalloc * pdrive->pmedia_info->sector_size_bytes);
	bytes_in_eraseblock = pdrive->pmedia_info->eraseblock_size_sectors * pdrive->pmedia_info->sector_size_bytes;

  	RTFS_PRINT_STRING_1((byte *)"    Verify erasure when file spans an erase block", PRFLG_NL);
   	if ((fd = po_open(TESTNAME0,(word)(PO_BINARY|PO_RDWR|PO_CREAT),(word)(PS_IWRITE | PS_IREAD))) < 0)
		goto unexpected;
	if (!Ntbe_write_file(fd, Ntu_get_sector_buffer(pdrive), 1, bytes_in_eraseblock, 0, 0))
		goto unexpected;
   	if (po_close(fd) != 0)
		goto unexpected;

	/* Verify the sector in not erased */
	sector_number = pc_cl2sector(pdrive,expected_cluster);
	if (Ntu_check_if_eb_erased(pdrive, sector_number) != 0)
	{
		nand_regress_error();
		return(FALSE);
	}
	/* Now delete it and verify the block is erased */
	if (!pc_unlink(TESTNAME0))
		goto unexpected;
	if (Ntu_check_if_eb_erased(pdrive, sector_number) != 1)
	{
		nand_regress_error();
		return(FALSE);
	}
  	RTFS_PRINT_STRING_1((byte *)"    Verify erasure when file spans a block plus partial block that is not empty", PRFLG_NL);
   	if ((fd = po_open(TESTNAME0,(word)(PO_BINARY|PO_RDWR|PO_CREAT),(word)(PS_IWRITE | PS_IREAD))) < 0)
		goto unexpected;
	if (!Ntbe_write_file(fd, Ntu_get_sector_buffer(pdrive), 1, bytes_in_eraseblock, 1, bytes_in_cluster))
		goto unexpected;
   	if (po_close(fd) != 0)
		goto unexpected;
	/* Create a directory to occupy second half of second erase block */
	if (!pc_mkdir(TESTDIRNAME0))
		goto unexpected;
	/* Verify the sectors are not erased */
	sector_number = pc_cl2sector(pdrive,expected_cluster);
	if (Ntu_check_if_eb_erased(pdrive, sector_number) != 0)
	{
		nand_regress_error();
		return(FALSE);
	}
	sector_number = pc_cl2sector(pdrive,expected_cluster+1);
	if (Ntu_check_if_eb_erased(pdrive, sector_number) != 0)
	{
		nand_regress_error();
		return(FALSE);
	}
	/* Now delete it and verify the first block is erased but the second block is not */
	if (!pc_unlink(TESTNAME0))
		goto unexpected;
	sector_number = pc_cl2sector(pdrive,expected_cluster);
	if (Ntu_check_if_eb_erased(pdrive, sector_number) != 1)
	{
		nand_regress_error();
		return(FALSE);
	}
	sector_number = pc_cl2sector(pdrive,expected_cluster+1);
	if (Ntu_check_if_eb_erased(pdrive, sector_number) != 0)
	{
		nand_regress_error();
		return(FALSE);
	}
	/* Now delete the directory and verify that the secod block is erased */
	if (!pc_rmdir(TESTDIRNAME0))
		goto unexpected;

	sector_number = pc_cl2sector(pdrive,expected_cluster+1);
	if (Ntu_check_if_eb_erased(pdrive, sector_number) != 1)
	{
		nand_regress_error();
		return(FALSE);
	}


  	RTFS_PRINT_STRING_1((byte *)"    Verify erasure when file spans a block plus partial block that is mpty", PRFLG_NL);
   	if ((fd = po_open(TESTNAME0,(word)(PO_BINARY|PO_RDWR|PO_CREAT),(word)(PS_IWRITE | PS_IREAD))) < 0)
		goto unexpected;
	if (!Ntbe_write_file(fd, Ntu_get_sector_buffer(pdrive), 1, bytes_in_eraseblock, 1, bytes_in_cluster))
		goto unexpected;
   	if (po_close(fd) != 0)
		goto unexpected;

	/* Verify the sectors are not erased */
	sector_number = pc_cl2sector(pdrive,expected_cluster);
	if (Ntu_check_if_eb_erased(pdrive, sector_number) != 0)
	{
		nand_regress_error();
		return(FALSE);
	}
	sector_number = pc_cl2sector(pdrive,expected_cluster+1);
	if (Ntu_check_if_eb_erased(pdrive, sector_number) != 0)
	{
		nand_regress_error();
		return(FALSE);
	}
	/* Now delete it and verify the blocks are erased */
	if (!pc_unlink(TESTNAME0))
		goto unexpected;
	sector_number = pc_cl2sector(pdrive,expected_cluster);
	if (Ntu_check_if_eb_erased(pdrive, sector_number) != 1)
	{
		nand_regress_error();
		return(FALSE);
	}
	sector_number = pc_cl2sector(pdrive,expected_cluster+1);
	if (Ntu_check_if_eb_erased(pdrive, sector_number) != 1)
	{
		nand_regress_error();
		return(FALSE);
	}


  	RTFS_PRINT_STRING_1((byte *)"    Verify erasure when a subdirectory occupies a block that is otherwise empty", PRFLG_NL);
	if (!pc_mkdir(TESTDIRNAME0))
		goto unexpected;
	sector_number = pc_cl2sector(pdrive,expected_cluster);
	if (Ntu_check_if_eb_erased(pdrive, sector_number) != 0)
	{
		nand_regress_error();
		return(FALSE);
	}
  	if (!pc_rmdir(TESTDIRNAME0))
		goto unexpected;
	if (Ntu_check_if_eb_erased(pdrive, sector_number) != 1)
	{
		nand_regress_error();
		return(FALSE);
	}

  	RTFS_PRINT_STRING_1((byte *)"    Verify erasure algorithm for a subdirectory occupying un-aligned cluster in a block", PRFLG_NL);
	if (!pc_mkdir(TESTDIRNAME0))
		goto unexpected;
	if (!pc_mkdir(TESTDIRNAME1))
		goto unexpected;
	/* Remove the directory claiming the first half of the erase block, this should not cause an erase */
  	if (!pc_rmdir(TESTDIRNAME0))
		goto unexpected;
	sector_number = pc_cl2sector(pdrive,expected_cluster);
	if (Ntu_check_if_eb_erased(pdrive, sector_number) != 0)
	{
		nand_regress_error();
		return(FALSE);
	}
	/* Remove the directory claiming the second half of the erase block, this should cause an erase */
  	if (!pc_rmdir(TESTDIRNAME1))
		goto unexpected;
	if (Ntu_check_if_eb_erased(pdrive, sector_number) != 1)
	{
		nand_regress_error();
		return(FALSE);
	}

	RTFS_PRINT_STRING_1((byte *)"    Verify erasure algorithm for a subdirectory occupying aligned cluster in a block", PRFLG_NL);
	if (!pc_mkdir(TESTDIRNAME0))
		goto unexpected;
	if (!pc_mkdir(TESTDIRNAME1))
		goto unexpected;
	sector_number = pc_cl2sector(pdrive,expected_cluster);
	if (Ntu_check_if_eb_erased(pdrive, sector_number) != 0)
	{
		nand_regress_error();
		return(FALSE);
	}
	/* Remove the directory claiming the second half of the erase block, this should not cause an erase */
  	if (!pc_rmdir(TESTDIRNAME1))
		goto unexpected;
	if (Ntu_check_if_eb_erased(pdrive, sector_number) != 0)
	{
		nand_regress_error();
		return(FALSE);
	}
	/* Remove the directory claiming the first half of the erase block, this should cause an erase */
  	if (!pc_rmdir(TESTDIRNAME0))
		goto unexpected;
	if (Ntu_check_if_eb_erased(pdrive, sector_number) != 1)
	{
		nand_regress_error();
		return(FALSE);
	}

	if (isfat32)
	{ /* If Fat32 delete the fill directory and verify the first cluster was not erased */
		if (!pc_rmdir(FILL32DIRNAME))
			goto unexpected;
		sector_number = pc_cl2sector(pdrive,2);
		if (Ntu_check_if_eb_erased(pdrive, sector_number) != 0)
		{
			nand_regress_error();
			return(FALSE);
		}
	}

   	RTFS_PRINT_STRING_1((byte *)"Block erase test succeeded", PRFLG_NL);
	pdrive->du.drive_operating_policy = save_drive_operating_policy;
	return(TRUE);
unexpected:
   	RTFS_PRINT_STRING_1((byte *)"Block erase test failed with unexpected error", PRFLG_NL);
	nand_regress_error();
    return(FALSE);

}

/* Test-  Check buffered file IO behavior */
static BOOLEAN   Ntt_verify_buffered_read_range(int fd, dword start_value, dword end_value, dword max_value);

static BOOLEAN   Ntt_file_buffering_test(byte *driveid)
{
DDRIVE *pdrive;
int fd,fd2, bytes_in_eraseblock;
dword i,save_drive_operating_policy,ltemp,sector_number,test_number;
ERTFS_STAT stat_buff,stat_buff2;

	if (!Ntu_clear_mbr(driveid))
		goto unexpected;
	{
		RTFSFMTPARMSEX parms_ex;
    	rtfs_memset(&parms_ex, 0, sizeof(parms_ex));
    	parms_ex.scrub_volume = TRUE;
    	RTFS_PRINT_STRING_1((byte *)"    Formating and erasing the volume", PRFLG_NL);
    	Ntu_slow_test();
		if (!pc_format_volume_ex(driveid, &parms_ex))
			goto unexpected;
	}

	pdrive = Ntu_get_drive_structure(driveid);
	if (!pdrive)
		return(FALSE);
	/* Make sure default operating policy is configured */
	save_drive_operating_policy = pdrive->du.drive_operating_policy;
	ltemp = DRVPOL_NAND_SPACE_RECOVER | DRVPOL_NAND_SPACE_OPTIMIZE;
	pdrive->du.drive_operating_policy &= ~ltemp;

	/* How many bytes in a cluster and erase block */
	bytes_in_eraseblock = pdrive->pmedia_info->eraseblock_size_sectors * pdrive->pmedia_info->sector_size_bytes;

	RTFS_PRINT_STRING_1((byte *)"    Verify file buffer is erase block sized", PRFLG_NL);
	{
    	BLKBUFF *pblk;
        if ((fd = po_open(TESTNAME0,(word)(PO_BINARY|PO_RDWR|PO_CREAT|PO_BUFFERED),(word)(PS_IWRITE | PS_IREAD))) < 0)
    		goto unexpected;
        pblk = Ntu_get_file_buffer(fd);
    	if (!pblk)
    		goto unexpected;
    	if ((int)pblk->data_size_bytes != bytes_in_eraseblock)
    	{
    		nand_regress_error();
    		return(FALSE);
    	}
       	if (po_close(fd) != 0 || !pc_unlink(TESTNAME0))
    		goto unexpected;
	}
/* PC_FILE *pfile;
	    pfile = Ntu_get_file_structure(fd);
*/
/* test_number
0 - verify sizes, traccking etc, the verify that flush flushes the buffer
1 - verify that close flushes the buffer
2 - that seek flushes the buffer
*/

	for (test_number = 0; test_number < 4; test_number++)
	{
    	sector_number = 0;
		if (test_number == 0)
		{
			RTFS_PRINT_STRING_1((byte *)"    Verify writes are buffered on erase block boundaries", PRFLG_NL);
			RTFS_PRINT_STRING_1((byte *)"    Verify file size tracks buffer size", PRFLG_NL);
			RTFS_PRINT_STRING_1((byte *)"    Verify multiple files share the buffer", PRFLG_NL);
		}
		if (test_number == 2)
		{	/* Pre size the file so we can validate seek forces a flush */
        	if ((fd = po_open(TESTNAME0,(word)(PO_BINARY|PO_RDWR|PO_CREAT),(word)(PS_IWRITE | PS_IREAD))) < 0)
       			goto unexpected;
    		if (po_write(fd, 0, bytes_in_eraseblock*2) != bytes_in_eraseblock*2)
    			goto unexpected;
			if (po_close(fd) != 0)
				goto unexpected;
		}
        if ((fd = po_open(TESTNAME0,(word)(PO_BINARY|PO_RDWR|PO_CREAT|PO_BUFFERED),(word)(PS_IWRITE | PS_IREAD))) < 0)
       		goto unexpected;
        if ((fd2 = po_open(TESTNAME0,(word)(PO_BINARY|PO_RDONLY|PO_BUFFERED),(word)(PS_IWRITE | PS_IREAD))) < 0)
    		goto unexpected;

    	for (i = 0; i < (dword)bytes_in_eraseblock/4; i++)
    	{
    		if (po_write(fd, (byte *)&i, 4) != 4)
    			goto unexpected;
    		if (i == 0)	/* rember the sector */
			{
    			sector_number = Ntu_get_file_buffer(fd)->blockno;
			}
    		if (po_read(fd2, (byte *)&ltemp, 4) != 4 || ltemp != i)
    		{
    			nand_regress_error();
    			return(FALSE);
    		}
			if (test_number != 2) /* test 2 preallocates */
			{
    		if ( pc_fstat(fd, &stat_buff) != 0 || stat_buff.st_size != (i+1)*4 || pc_fstat(fd2, &stat_buff2) != 0 || stat_buff2.st_size != (i+1)*4)
    		{
    			nand_regress_error();
    			return(FALSE);
    		}
			}
    		/* The erase block should still be erased */
    		if (Ntu_check_if_eb_erased(pdrive, sector_number) != 1)
    		{
    			nand_regress_error();
    			return(FALSE);
    		}
// HEREHERE
    	}
		if (test_number == 0)
		{
			RTFS_PRINT_STRING_1((byte *)"    Verify file flush flushes erase block sized file buffer", PRFLG_NL);

			if (!po_flush(fd))
			{
    			nand_regress_error();
    			return(FALSE);
    		}
    		if (Ntu_check_eb_contents(pdrive, sector_number, 0, 1) != 1)
			{
    			nand_regress_error();
    			return(FALSE);
    		}
		}
		if (test_number == 2)
		{
			RTFS_PRINT_STRING_1((byte *)"    Verify seek past current block flushes erase block sized file buffer", PRFLG_NL);
			po_lseek(fd,  bytes_in_eraseblock, PSEEK_SET);
    		if (po_read(fd, (byte *)&ltemp, 4) != 4 || ltemp != 0xffffffff)
    		{
    			nand_regress_error();
    			return(FALSE);
    		}
    		if (Ntu_check_eb_contents(pdrive, sector_number, 0, 1) != 1)
			{
    			nand_regress_error();
    			return(FALSE);
    		}
			RTFS_PRINT_STRING_1((byte *)"    Verify seek to previos block flushes erase block sized file buffer", PRFLG_NL);
			po_lseek(fd,  bytes_in_eraseblock, PSEEK_SET);
        	for (i = 0; i < (dword)bytes_in_eraseblock/4; i++)
        	{
    			ltemp = i + bytes_in_eraseblock/4;
        		if (po_write(fd, (byte *)&ltemp, 4) != 4)
        			goto unexpected;
        		if (i == 0)	/* remember the sector */
        			sector_number = Ntu_get_file_buffer(fd)->blockno;
        		/* The erase block should still be erased */
        		if (Ntu_check_if_eb_erased(pdrive, sector_number) != 1)
        		{
        			nand_regress_error();
        			return(FALSE);
        		}
        	}
			po_lseek(fd,  0, PSEEK_SET);
    		if (po_read(fd, (byte *)&ltemp, 4) != 4 || ltemp != 0)
    		{
    			nand_regress_error();
    			return(FALSE);
    		}
			if (Ntu_check_eb_contents(pdrive, sector_number, (dword)bytes_in_eraseblock/4, 1) != 1)
			{
    			nand_regress_error();
    			return(FALSE);
    		}
		}
		if (test_number == 3)
		{
			RTFS_PRINT_STRING_1((byte *)"    Verify write past block flushes erase block sized file buffer", PRFLG_NL);
			/* One more write should advance the pointer and cause a flush */
			if (po_write(fd, (byte *)&i, 4) != 4)
   				goto unexpected;
    		if (Ntu_check_eb_contents(pdrive, sector_number, 0, 1) != 1)
			{
    			nand_regress_error();
    			return(FALSE);
    		}
		}

    	if (po_close(fd) != 0)
			goto unexpected;
		if (test_number == 1)
		{
			RTFS_PRINT_STRING_1((byte *)"    Verify file close flushes erase block sized file buffer", PRFLG_NL);
    		if (Ntu_check_eb_contents(pdrive, sector_number, 0, 1) != 1)
			{
    			nand_regress_error();
    			return(FALSE);
    		}
		}

		if (test_number == 3)
		{
			RTFS_PRINT_STRING_1((byte *)"    Verify write past block flushes erase block sized file buffer", PRFLG_NL);
    		if (Ntu_check_eb_contents(pdrive, sector_number, 0, 1) != 1)
			{
    			nand_regress_error();
    			return(FALSE);
    		}
		}

		if (po_close(fd2) != 0 || !pc_unlink(TESTNAME0))
    			goto unexpected;
	}
	RTFS_PRINT_STRING_1((byte *)"    Verify buffer defaults to a sector buffer when file buffers exhausted", PRFLG_NL);
	{
	BLKBUFF *save_pblk;
   	BLKBUFF *pblk;
    	save_pblk = pdrive->file_buffer_freelist;
    	pdrive->file_buffer_freelist = 0;
        if ((fd = po_open(TESTNAME0,(word)(PO_BINARY|PO_RDWR|PO_CREAT|PO_BUFFERED),(word)(PS_IWRITE | PS_IREAD))) < 0)
		{
        	pdrive->file_buffer_freelist = save_pblk;
    		goto unexpected;
		}
        pdrive->file_buffer_freelist = save_pblk;
        pblk = Ntu_get_file_buffer(fd);
    	if (!pblk)
    		goto unexpected;
    	if (!pblk || (int)pblk->data_size_bytes != pdrive->pmedia_info->sector_size_bytes)
    	{
    		nand_regress_error();
    		return(FALSE);
    	}

    	RTFS_PRINT_STRING_1((byte *)"    Verify buffer honors sector boundaries when file buffers exhausted", PRFLG_NL);
		sector_number = 0;
    	for (i = 0; i < (dword)pdrive->pmedia_info->sector_size_bytes/4; i++)
    	{
    		if (po_write(fd, (byte *)&i, 4) != 4)
    			goto unexpected;
    		if (i == 0)	/* remember the sector */
    			sector_number = Ntu_get_file_buffer(fd)->blockno;
    		/* The erase block should still be erased */
    		if (Ntu_check_if_eb_erased(pdrive, sector_number) != 1)
    		{
    			nand_regress_error();
    			return(FALSE);
    		}
    	}
		/* One more write should advance the pointer and cause a flush */
   		if (po_write(fd, (byte *)&i, 4) != 4)
   			goto unexpected;
   		/* The erase block should not still be erased */
   		if (Ntu_check_if_eb_erased(pdrive, sector_number) != 0)
   		{
   			nand_regress_error();
   			return(FALSE);
   		}

       	if (po_close(fd) != 0 || !pc_unlink(TESTNAME0))
    		goto unexpected;
	}
	RTFS_PRINT_STRING_1((byte *)"    Verify unaligned reads and writes when opened in unbuffered mode", PRFLG_NL);
	{
   	BLKBUFF *pblk;
	byte *pb;
	dword *pdw;
	dword eb_baseval;
	int   j;

   		/* First fill a file with a pattern use 3 erase blocks */
        if ((fd = po_open(TESTNAME0,(word)(PO_BINARY|PO_RDWR|PO_CREAT),(word)(PS_IWRITE | PS_IREAD))) < 0)
    		goto unexpected;
		eb_baseval = 0;
		for (j = 0; j < 3; j++)
		{
			pb = Ntu_get_sector_buffer(pdrive);
			pdw = (dword *) pb;
			for (i = 0; i < (dword)bytes_in_eraseblock/4; i++, pdw++)
				*pdw = eb_baseval+i;
   			if (po_write(fd, pb, bytes_in_eraseblock) != bytes_in_eraseblock)
   				goto unexpected;
   			eb_baseval += (dword) bytes_in_eraseblock/4;
		}
       	if (po_close(fd)!=0)
    		goto unexpected;
		/* Reopen the file in buffered mode and retrieve the sector number */
        if ((fd = po_open(TESTNAME0,(word)(PO_BINARY|PO_RDONLY|PO_BUFFERED),(word)(PS_IWRITE | PS_IREAD))) < 0)
    		goto unexpected;
        pblk = Ntu_get_file_buffer(fd);
    	if (!pblk)
    		goto unexpected;
   		if (po_read(fd, (byte *)&ltemp, 4) != 4)
   			goto unexpected;
   		/* remember the sector */
   		sector_number = Ntu_get_file_buffer(fd)->blockno;
       	if (po_close(fd)!=0)
    		goto unexpected;
		RTFS_PRINT_STRING_1((byte *)"    Verify unaligned writes when opened in unbuffered mode", PRFLG_NL);
		/* Now overwrite the file a byte at a time and make sure the buffer is reloaded on erase block boundaries */
        if ((fd = po_open(TESTNAME0,(word)(PO_BINARY|PO_RDWR),(word)(PS_IWRITE | PS_IREAD))) < 0)
    		goto unexpected;
		eb_baseval = 0;
		for (j = 0; j < 3; j++)
		{
			for (i = 0; i < (dword)bytes_in_eraseblock/4; i++)
			{
				ltemp = eb_baseval+i;
				pb = Ntu_get_sector_buffer(pdrive); /* This also zeroes the buffer */
				if (po_write(fd, (byte *) &ltemp, 4) != 4)
   					goto unexpected;
				/* Now check the sector buffer, if the erase block was not read in first it will have zeroes and not match */
				{
					dword z,eraseblock_size_dwords;
					dword val,*pdw;
					val = eb_baseval;
					pdw = (dword *) pb;
					eraseblock_size_dwords = (pdrive->pmedia_info->eraseblock_size_sectors * pdrive->pmedia_info->sector_size_bytes)/4;
					for ( z = 0; z < eraseblock_size_dwords; z++, pdw++,val += 1)
					{
						if (*pdw != val)
						{
							nand_regress_error();
							return(FALSE);
						}
					}
				}
			}
   			eb_baseval += (dword) bytes_in_eraseblock/4;
		}
       	if (po_close(fd)!=0)
    		goto unexpected;
		RTFS_PRINT_STRING_1((byte *)"    Verify unaligned reads when opened in unbuffered mode", PRFLG_NL);
		/* Now read the file a byte at a time and make sure the buffer is reloaded on sector block boundaries */
        if ((fd = po_open(TESTNAME0,(word)(PO_BINARY|PO_RDWR),(word)(PS_IWRITE | PS_IREAD))) < 0)
    		goto unexpected;
		eb_baseval = 0;
		for (j = 0; j < 3; j++)
		{
			dword sector_size_dwords,first_expected_value, last_expected_value;

			for (i = 0; i < (dword)bytes_in_eraseblock/4; i++)
			{
				ltemp = eb_baseval+i;
				pb = Ntu_get_sector_buffer(pdrive); /* This also zeroes the buffer */
				if (po_read(fd, (byte *) &ltemp, 4) != 4 || ltemp != eb_baseval+i)
   					goto unexpected;
				/* Now check the sector buffer, only the current sector should be populated all others should be zero */
				sector_size_dwords = pdrive->pmedia_info->sector_size_bytes/4;
				first_expected_value = eb_baseval + (i / sector_size_dwords) * sector_size_dwords;
				last_expected_value = first_expected_value + sector_size_dwords - 1;
				{
					dword z,eraseblock_size_dwords;
					dword *pdw;
					pdw = (dword *) pb;
					eraseblock_size_dwords = (pdrive->pmedia_info->eraseblock_size_sectors * pdrive->pmedia_info->sector_size_bytes)/4;
					for ( z = 0; z < eraseblock_size_dwords; z++, pdw++)
					{
						if (*pdw != 0 && (*pdw < first_expected_value || *pdw > last_expected_value))
						{
							nand_regress_error();
							return(FALSE);
						}
					}
				}
			}
   			eb_baseval += (dword) bytes_in_eraseblock/4;
		}

		RTFS_PRINT_STRING_1((byte *)"    Extensive verify unaligned reads when opened in unbuffered mode", PRFLG_NL);
		Ntu_slow_test();
		{
			dword max_value, start_value, end_value;

			max_value = ((3 * bytes_in_eraseblock)/4)-1;

			for (start_value = 0; start_value < max_value; start_value++)
			{
				for (end_value = start_value; end_value < max_value; end_value++)
				{
					if (!Ntt_verify_buffered_read_range(fd, start_value, end_value, max_value))
					{
						nand_regress_error();
						return(FALSE);
					}
				}
				if (start_value % 32 == 0)
					RTFS_PRINT_STRING_1((byte *)"    Still in extensive verify unaligned reads when opened in unbuffered mode", PRFLG_NL);
			}
		}



       	if (po_close(fd)!=0)
    		goto unexpected;
       	if (!pc_unlink(TESTNAME0))
    		goto unexpected;
	}

   	RTFS_PRINT_STRING_1((byte *)"File buffering test succeeded", PRFLG_NL);
	pdrive->du.drive_operating_policy = save_drive_operating_policy;
	return(TRUE);
unexpected:
   	RTFS_PRINT_STRING_1((byte *)"File buffering test failed with unexpected error", PRFLG_NL);
	nand_regress_error();
    return(FALSE);

}

static BOOLEAN   Ntt_verify_buffered_read_range(int fd, dword start_value, dword end_value, dword max_value)
{
byte *pbuffer;
int bytes_to_read, max_to_read, n_read;
dword *pdw, current_value;
long start_point;

	bytes_to_read = (end_value - start_value + 1) * 4;
	max_to_read   = (max_value - start_value + 1) * 4;

	pbuffer = (byte *) rtfs_port_malloc(bytes_to_read);
	if (!pbuffer)
		return(FALSE);

	start_point = (long) (start_value*4);

	if (po_lseek(fd,  start_point, PSEEK_SET) != start_point)
	{
		nand_regress_error();
		goto exit_fail;
	}
	n_read = po_read(fd, pbuffer, bytes_to_read);
	if (n_read != bytes_to_read)
	{
		if (n_read != max_to_read)
		{
			nand_regress_error();
			goto exit_fail;
		}
		else
			end_value = max_value;
	}
	pdw = (dword *) pbuffer;
	for (current_value = start_value; current_value < end_value; current_value++, pdw++)
	{
		if (*pdw != current_value)
		{
			nand_regress_error();
			goto exit_fail;
		}
	}
	rtfs_port_free(pbuffer);
	return(TRUE);
exit_fail:
	rtfs_port_free(pbuffer);
	return(FALSE);
}
static BOOLEAN   Ntu_format_for_testing(byte *driveid, dword n_erase_blocks, dword clusterspereraseblock, unsigned char bits_per_cluster, unsigned char numfats)
{
RTFSFMTPARMSEX parms_ex;
DDRIVE *pdrive;
struct mbr_specification mbrspecs[1];

	/* Create a partition on an erase block boundary containing n_erase_blocks erase blocks   */
	pdrive = Ntu_get_drive_structure(driveid);
	if (!pdrive)
		return(FALSE);

    rtfs_memset(&mbrspecs[0], 0, sizeof(mbrspecs));
    mbrspecs[0].mbr_sector_location = 0;    /* The mbr is always at sector zero */
    mbrspecs[0].device_mbr_count    = 1;    /* There will be at least one master mbr */
    mbrspecs[0].entry_specifications[0].partition_start =   pdrive->pmedia_info->eraseblock_size_sectors;
    mbrspecs[0].entry_specifications[0].partition_size  =   n_erase_blocks * pdrive->pmedia_info->eraseblock_size_sectors;
    mbrspecs[0].entry_specifications[0].partition_type  =   0x06; /* Select FAT16 (not used really) */
    mbrspecs[0].entry_specifications[0].partition_boot  =   0x80;

    if (!pc_partition_media(driveid, &mbrspecs[0]))
		return(FALSE);

	/* Format the partition with the specified characteristics */
    rtfs_memset(&parms_ex, 0, sizeof(parms_ex));
	parms_ex.scrub_volume = TRUE;
	parms_ex.bits_per_cluster = bits_per_cluster; /* 12, 16, 32 */
	if (bits_per_cluster == 32)
		parms_ex.numroot     = (unsigned short) 0;          /* # of root dir entries 0 for FAT32*/
	else													/* # of root dir entries use one erase block */
	{
		dword ltemp;
		ltemp = pdrive->pmedia_info->eraseblock_size_sectors * pdrive->pmedia_info->sector_size_bytes;
		ltemp /= 32;
		parms_ex.numroot     = (unsigned short) ltemp;
	}
	parms_ex.numfats     = numfats;
	/* Set the cluster size so there are clusterspereraseblock clusters per erase block */
	{
		dword ltemp;
		ltemp = pdrive->pmedia_info->eraseblock_size_sectors / clusterspereraseblock;
		parms_ex.secpalloc   = (unsigned char)  ltemp;        /* Sectors per cluster */
	}
	parms_ex.secreserved = (unsigned short) pdrive->pmedia_info->eraseblock_size_sectors;

	if (!pc_format_volume_ex(driveid, &parms_ex))
		return(FALSE);
	if (!Ntu_check_format_aligned(driveid, FALSE))  /* Sanity check */
		return(FALSE);
	return(TRUE);
}

static BOOLEAN   Ntu_check_format_aligned(byte *driveid, BOOLEAN check_cluster_size)
{
DDRIVE *pdrive;

	pdrive = Ntu_get_drive_structure(driveid);
	if (!pdrive)
		return(FALSE);
	/* Check start of FAT table for erase block alignment */
	if (!Ntu_check_eb_aligned(pdrive, (dword) pdrive->drive_info.secreserved))
		goto fail_not_aligned;
	/* Check start of second FAT table for erase block alignment */
	if (pdrive->drive_info.numfats > 1 && !Ntu_check_eb_aligned(pdrive, (dword) pdrive->drive_info.secreserved+pdrive->drive_info.secpfat))
		goto fail_not_aligned;
	/* Check start of root directory and root directory size for erase block alignment */
	if (pdrive->drive_info.fasize != 8)
	{
		if (!Ntu_check_eb_mod(pdrive, (dword) pdrive->drive_info.secproot))
			goto fail_not_aligned;
	}
	/* Check start of cluster area for erase block alignment */
	if (!Ntu_check_eb_aligned(pdrive, pdrive->drive_info.firstclblock))
		goto fail_not_aligned;
	/* Check size of cluster for erase block alignment
	   We can disable this check to allow clustersize < erase block */
	if (check_cluster_size)
	{
		dword ltemp;
		ltemp = (dword) pdrive->drive_info.secpalloc;
		ltemp &= 0xff;
		if (!Ntu_check_eb_mod(pdrive, ltemp))
			goto fail_not_aligned;
	}
	return(TRUE);
fail_not_aligned:
	nand_regress_error();
	return(FALSE);
}

/* Zero fill and return the address of the drive's sector buffer (large enought to hold an erase block) */
static byte *Ntu_get_sector_buffer(DDRIVE *pdrive)
{
	dword buffer_size;
	byte *psectorbuffer;

	if (prtfs_cfg->rtfs_exclusive_semaphore)
	{
		psectorbuffer = (byte *) prtfs_cfg->shared_user_buffer;
		buffer_size = prtfs_cfg->shared_user_buffer_size;
	}
	else
	{
		psectorbuffer = (byte *) (byte *) pdrive->pmedia_info->device_sector_buffer;
		buffer_size = pdrive->pmedia_info->device_sector_buffer_size;
	}
	if (buffer_size < pdrive->pmedia_info->eraseblock_size_sectors * pdrive->pmedia_info->sector_size_bytes)
	{
		nand_regress_error();
		return(0);
	}
	if (!psectorbuffer)
	{
		nand_regress_error();
		return(0);
	}
    rtfs_memset(psectorbuffer, 0, pdrive->pmedia_info->eraseblock_size_sectors * pdrive->pmedia_info->sector_size_bytes);
	return (psectorbuffer);
}

/* Return -1 on error 1 if pattern matches, 0 if not a match */
static int   Ntu_check_eb_contents(DDRIVE *pdrive, dword sector_number, dword val, dword increment)
{
dword i,eraseblock_size_dwords;
byte *pb;
dword *pdw;

	pb = Ntu_get_sector_buffer(pdrive);
	pdw = (dword *) pb;
    if (!raw_devio_xfer(pdrive, sector_number, pb, pdrive->pmedia_info->eraseblock_size_sectors, FALSE, TRUE))
	{
		nand_regress_error();
		return(-1);
	}
	eraseblock_size_dwords = (pdrive->pmedia_info->eraseblock_size_sectors * pdrive->pmedia_info->sector_size_bytes)/4;
	for ( i = 0; i < eraseblock_size_dwords; i++, pdw++,val += increment )
	{
		if (*pdw != val)
		{
			return(0);
		}
	}
	return(1);
}



/* Return -1 on error 1 if erased, 0 if not erased */
static int   Ntu_check_if_eb_erased(DDRIVE *pdrive, dword sector_number)
{
	return(Ntu_check_eb_contents(pdrive, sector_number, 0xffffffff, 0));
}

static BOOLEAN   Ntu_check_eb_aligned(DDRIVE *pdrive, dword sector_number)
{
	if ( (sector_number % pdrive->pmedia_info->eraseblock_size_sectors) != 0)
		return(FALSE);
	else
		return(TRUE);
}
static BOOLEAN   Ntu_check_eb_mod(DDRIVE *pdrive, dword sector_count)
{
	if ( (sector_count % pdrive->pmedia_info->eraseblock_size_sectors) != 0)
		return(FALSE);
	else
		return(TRUE);

}
static DDRIVE *Ntu_get_drive_structure(byte *driveid)
{
	int	driveno;
	if (!pc_set_default_drive(driveid) || !pc_set_cwd((byte *)"\\"))	/* Set default drive id, pc_setcwd forces a mount if not mounted */
	{
		nand_regress_error();
		return(0);
	}
    driveno = pc_drname_to_drno(driveid, CS_CHARSET_NOT_UNICODE);
    return(pc_drno_to_drive_struct(driveno));
}


/* Get a file structure buffer */
static PC_FILE *Ntu_get_file_structure(int fd)
{
PC_FILE *pfile;

	pfile = prtfs_cfg->mem_file_pool+fd;
    return(pfile);
}

/* Get a file's block buffer */
static BLKBUFF *Ntu_get_file_buffer(int fd)
{
PC_FILE *pfile;

	pfile =Ntu_get_file_structure(fd);
    return(pfile->pobj->finode->pfile_buffer);
}

/* Get a file's cluster fragment list */
static REGION_FRAGMENT *Ntu_get_fragment_list(int fd)
{
PC_FILE *pfile;
dword curr_seek_pointer;
	/* Seek to the end of the file to make sure all framgments are loaded */
    curr_seek_pointer = po_lseek(fd, 0L, PSEEK_CUR);
    po_lseek(fd, 0L, PSEEK_END);
    po_lseek(fd,  curr_seek_pointer, PSEEK_SET);

	pfile =Ntu_get_file_structure(fd);
#if (INCLUDE_RTFS_PROPLUS)    /* ProPlus FINODE structure extensions */
    return(pfile->pobj->finode->e.x->pfirst_fragment);
#else
	return(pfile->pobj->finode->pbasic_fragment);
#endif
}

static BOOLEAN  Ntu_clear_mbr(byte *driveid)
{
struct mbr_specification mbrspecs[1];
    /* Clear the specifications */
    rtfs_memset(&mbrspecs[0], 0, sizeof(mbrspecs)); /*  mbrspecs[0].device_mbr_count = 0;  Clears the partition table */
    if (!pc_partition_media(driveid, &mbrspecs[0]))
		return(nand_regress_error());
	return(TRUE);
}

static void Ntu_slow_test(void)
{
    RTFS_PRINT_STRING_1((byte *)"   .. Test may require several minutes to complete ..", PRFLG_NL);
}




#endif /* Exclude from build if read only */
