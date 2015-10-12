/* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/*
<TEST>  Test File:   rtfspackages/apps/prfstest.c
<TEST>  Procedure: pc_failsafe_test(void)
<TEST>   Description: Test suite performs tests to verify the correct operation of features provided with RTFS Pro Plus Failsafe.
<TEST>    The following basic tests ares performed by source code in the file named prfstest.c
<TEST>    Tests that journaling works correctly when a journal file wrap occurs. When volume synchronize and journal
<TEST>    flush steps are done seperately, the journal file will wrap and the session start location will be offset
<TEST>    when the current frame record reaches the end of the journal and the frame record at the beginning of the
<TEST>    file has been synchronized. This is an unusual event so this test verifies that the wrap algorithm is
<TEST>    working correctly.
<TEST>    Tests the use of fs_api_cb_journal_fixed places the journal at a fixed place and size. This test creates a contiguous
<TEST>    file on a volume and then sets the fixed journal placement callback coordinates to the file's extents. It then starts journaling
<TEST>    and verifies the correct size and location of the journal.
<TEST>    Tests correct behavior on disk full conditions - Two seperate tests verify that when Failsafe is Journaling to free
<TEST>    space it properly resizes the journal file when the disk becomes full. These tests
<TEST>        Verify that the initial jounal size is reduced when journaling is started on a nearly full disk.
<TEST>        Verify that the active jounal size is reduced when a disk becomes nearly full while journalin
<TEST>        The test are perfromed with and without using the memory based free manager
<TEST>    Tests Journal full condition. Verifies correct operation and proper error handling when all records in
<TEST>    the journal file are consumed.
<TEST>    Tests Journal file error conditions. Correct operation and proper error handling are tested for several
<TEST>    simulated error conditions.
<TEST>        Out of date - Volume is changed with journaling disabled and flushed records in the file.
<TEST>        Bad master record - Fields in the master record are manually corrupted and error handling is verified.
<TEST>        Bad frame record - Fields in frame records are manually corrupted and error handling is verified.
<TEST>    Tests other operating conditions
<TEST>        No flush    - Verifies that changes that are not flushed are lost
<TEST>        Restore from disk - Verifies that flushed but unsynchronized volume changes can be restored
<TEST>        Synchronize from active session - Verifies that flushed volume changes can be synchronized
<TEST>        Restore after aborted synchronize - Verifies that aborted synchronizes may be completed by restore
<TEST>        Simultaneous journal and syncronize - Verifies that volume changes can be journaled to one segment while another
<TEST>        segment is being synchronized.
<TEST>
<TEST>    The following tests are performed by source code in the file named prasytest.c
<TEST>
<TEST>        Test that interrupting after JOURNAL flush leaves the disk undamaged and unchanged
<TEST>       and that the Journaled changes are be restored to the volume by a restore.
<TEST>    Simulate a write IO error during asynchronous the first journal flush of s session.
<TEST>       Then check Journal file status. The journal file should be not valid or restore should
<TEST>       be not required or recommend.
<TEST>   Simulate write IO error during asynchronous journal commit. Then check disk
<TEST>   and look for lost chains or some other error. Check
<TEST>   Journal status and verify restore is required. Now restore disk from Journal
<TEST>   and verfiy lost chains or other errors are clear.
<TEST>     or invoke the "fs test" command from the RTFS Pro Plus command shell.
*/

#include "rtfs.h"


#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */
dword fs_test_journal_size = 0; /* Test code may use to set the journal file size */
#if (!(INCLUDE_DEBUG_TEST_CODE && INCLUDE_FAILSAFE_CODE))

void pc_fstest_main(byte *pdriveid)
{
    RTFS_ARGSUSED_PVOID((void *)pdriveid);
    RTFS_PRINT_STRING_1((byte *)"Build with INCLUDE_DEBUG_TEST_CODE and INCLUDE_FAILSAFE_CODE to run test", PRFLG_NL);
}
#else
#include "protests.h"

extern DRIVE_INFO test_drive_infostruct;


#define CHECK_OUT_OF_DATE   1
#define CHECK_BAD_MASTER    2
#define CHECK_BAD_FRAME     3

#define TEST_NOJFLUSH               1
#define TEST_DISK_RESTORE           2
#define TEST_RAM_RESTORE            3
#define TEST_ABORTED_RAM_RESTORE    4
#define TEST_SIMULTANEOUS_RESTORE   5



#define DEFAULT_INDEX_BUFFER_SIZE  66

/* Used to access failsafe values after journaling is shut down */
static REGION_FRAGMENT copy_of_nv_reserved_fragment;
static FAILSAFECONTEXT copy_of_context;
static FAILSAFECONTEXT *pcopy_of_context;

static void fstest_journal_full(void);
static void fstest_journal_errors(int which_test);
static void fstest_journaling(int which_test);
static void save_fs_context(void);
static void restore_fs_context(void);
static void reserve_fs_reserved_clusters(void);
static void release_fs_reserved_clusters(void);
static void save_fs_file_handle(void);
static void restore_fs_file_handle(void);
static void fs_consume_frames(byte * fname, dword nframes);

static FAILSAFECONTEXT *fstest_failsafe_context();
static void fstest_journal_wrap(void);
static void fstest_disk_full_1(BOOLEAN use_freespace_manager);
static void fstest_disk_full_2(BOOLEAN use_freespace_manager);
static void fs_test_fixed_journal(dword size_in_sectors);

/*
<TEST>   Procedure: Perform random_test_asynchronous_api().
<TEST>   Description: This test verifies that Failsafe behave properly when IO errors are simulated on every block that is read or written during a sequence of operations.
<TEST>   It  uses block access statistics to capture all block IO activity and set of becnchmarks that log the state of the volume
<TEST>   It calibrates itself by creating subdirectories and populating files, and issuing Journal flush and synchronize requests
<TEST>   while gathering block access statistics and capturing a set of volume state bechmarks. It then performs the same set of operations in a
<TEST>   repetative loop and simulates an IO error for every block read and write request. For each simulated error it verifies
<TEST>   that Failsafe performs correctly, either leaving the volume unchanged if no flushes occured before the error, or restoring the volume to state recorded
<TEST>   after the last succesful synchronize operation.
<TEST>
*/

static void fstest_simple_drive_config(
    dword drive_operating_policy,
    dword blockmap_size,
    dword index_buffer_size,
    dword restore_buffer_size,
    dword journal_size);

void pc_fstest_main(byte *pdriveid)
{
struct save_mount_context saved_mount_parms;
DDRIVE *ptest_drive;

	pcopy_of_context = 0; /* Points to shadow context, cleare once, will be referenced if non zero */

    if (!set_test_drive(pdriveid))
        return;
	/* Get drive information, this should force a mount if needed */
	if (!pc_diskio_info(test_driveid, &test_drive_infostruct, FALSE))
    {
        ERTFS_ASSERT_TEST(rtfs_debug_zero())
		return;
    }

	/* Save the current volume configuration */
   	ptest_drive = test_drive_structure();

	pro_test_save_mount_context(ptest_drive,&saved_mount_parms);
	/* Disable shared buffer mode for user buffer and failsafe restore buffer
	   just clearing the semaphore is enough the shared buffers won't be touched. */
	prtfs_cfg->rtfs_exclusive_semaphore	= 0;

	/* Now provide an initial configuration so we can access the drive - Will be chnaged by the tests as needed */
    fstest_simple_drive_config(DRVPOL_DISABLE_AUTOFAILSAFE,  DEFAULT_BLOCKMAPSIZE, DEFAULT_INDEX_BUFFER_SIZE ,DEFAULT_RESTORE_BUFFER_SIZE, 0);

   	fs_api_disable(test_driveid, TRUE);

    data_buffer = 0;
    pro_test_alloc_data_buffer();

    PRO_TEST_ANNOUNCE("Testing journal file wrap");
    fstest_journal_wrap();

    PRO_TEST_ANNOUNCE("Testing fixed journal placement with 128 sectors");
    fs_test_fixed_journal(128);


	PRO_TEST_ANNOUNCE("Test Disk full condition with memory based free manager");
	fstest_disk_full_1(TRUE);
	fstest_disk_full_2(TRUE);
#if (INCLUDE_EXFATORFAT64)
	if (ISEXFATORFAT64(ptest_drive))
	{
		/* Seeing the journal file fragment   one cluster to large after resize */
		PRO_TEST_ANNOUNCE("****** Bypass Disk full condition test with no free manager, not working under exFAT ");
	}
	else
#endif
	{
		/* Test error handling for journal full */
		PRO_TEST_ANNOUNCE("Test Disk full condition with-out memory based free manager");
		fstest_disk_full_1(FALSE);
		fstest_disk_full_2(FALSE);
	}

    PRO_TEST_ANNOUNCE("Test Journal full condition");
    fstest_journal_full();

    /* Abort mount .. note.. if we don't crashes the config portion - because we free the structures */
    fs_api_disable(test_driveid, TRUE);

	{
		PRO_TEST_ANNOUNCE("Test Journal CHECK_OUT_OF_DATE");
		fstest_journal_errors(CHECK_OUT_OF_DATE);
		fs_api_disable(test_driveid, TRUE);
	}
    PRO_TEST_ANNOUNCE("Test Journal CHECK_BAD_MASTER");
    fstest_journal_errors(CHECK_BAD_MASTER);
    fs_api_disable(test_driveid, TRUE);
    PRO_TEST_ANNOUNCE("Test Journal CHECK_BAD_FRAME");
    fstest_journal_errors(CHECK_BAD_FRAME);
    fs_api_disable(test_driveid, TRUE);

    PRO_TEST_ANNOUNCE("Test Journal no flush");
    fstest_journaling(TEST_NOJFLUSH);
    PRO_TEST_ANNOUNCE("Test restore from disk after shutdown");
    fstest_journaling(TEST_DISK_RESTORE);
    PRO_TEST_ANNOUNCE("Test restore (synchronize) from active session");
    fstest_journaling(TEST_RAM_RESTORE);
	{
	/* Note: July 2012 - the underlying test was changed to accomodate wxFAT */
    PRO_TEST_ANNOUNCE("Test disk restore after aborted synchronize from active session");
    fstest_journaling(TEST_ABORTED_RAM_RESTORE);
	}
    PRO_TEST_ANNOUNCE("Test simultaneous journal and syncronize");
    fstest_journaling(TEST_SIMULTANEOUS_RESTORE);
    /* Abort the current mount and journaling session (if journaling and or mounted */
    fs_api_disable(test_driveid, TRUE);
    /* Free the current configuration if we changed it */
    pro_test_release_mount_parameters(ptest_drive);
    /* Restore the original configuration */
    pro_test_restore_mount_context(ptest_drive,&saved_mount_parms);
    pro_test_free_data_buffer();
}

static void fstest_drive_config(
    dword drive_operating_policy,
    dword blockmap_size,
    dword index_buffer_size,
    dword restore_buffer_size,
    dword journal_size,
    dword user_buffer_size_sectors,
    dword num_fat_buffers);

static int create_file_for_fstest(byte *filename);
static BOOLEAN write_for_fstest(int fd,dword ntowrite);
static dword cluster_to_sector_for_fstest(int drivenumber, dword cluster);

static BOOLEAN close_file_for_fstest(int fd);
static void fstest_create_file_journaled(byte *file_name, dword n_frames);

#if (INCLUDE_RTFS_PROPLUS)  /* Uses ProPLus features, not supported under Pro */
static FREELISTINFO *allocate_clusters_until(dword leave_n_free, int *pn_fragments)
{
FREELISTINFO *plist, *plist_iterator;
FREELISTINFO info_element;
int i, n_fragments;
dword free_clusters;

    /* How many fragments are there ? */
    n_fragments = pc_diskio_free_list(test_driveid, 1, &info_element, 0, 0, 1);
    if (n_fragments  < 0)
    { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    plist = (FREELISTINFO *) pro_test_malloc(sizeof(*plist) * n_fragments);
    if (!plist)
    { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    /* Now get the fragments */
    n_fragments = pc_diskio_free_list(test_driveid, n_fragments, plist, 0, 0, 1);
    if (n_fragments  < 0)
    { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }

    free_clusters = 0;
    plist_iterator = plist;
    for (i = 0; i < n_fragments; i++)
    {
        free_clusters += plist_iterator->nclusters;
        plist_iterator++;
    }
    plist_iterator = plist;
    for (i = 0; i < n_fragments; i++)
    {
        dword n_to_claim,first_new_cluster;
        if (free_clusters <= leave_n_free)
            break;
        n_to_claim = plist_iterator->nclusters;
        if (free_clusters - n_to_claim < leave_n_free)
            n_to_claim = free_clusters - leave_n_free;

       if ( (n_to_claim != fatop_alloc_chain(test_drive_structure(), FALSE, plist_iterator->cluster, 0, &first_new_cluster, n_to_claim, n_to_claim))
            || first_new_cluster !=  plist_iterator->cluster )
            { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        free_clusters -= n_to_claim;
        plist_iterator++;
    }
    if (!pc_diskflush(test_driveid))
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }

    *pn_fragments = n_fragments;
    return(plist);
}
/* Release all clusters allocated from allocate_clusters_until() and release the list */
/* Release all clusters allocated from allocate_clusters_until() and release the list */
static void release_allocate_clusters_until(dword leave_n_free, int n_fragments, FREELISTINFO *plist)
{
FREELISTINFO *plist_iterator;
int i;
dword free_clusters;

    free_clusters = 0;
    plist_iterator = plist;
    for (i = 0; i < n_fragments; i++)
    {
        free_clusters += plist_iterator->nclusters;
        plist_iterator++;
    }
    plist_iterator = plist;
    for (i = 0; i < n_fragments; i++)
    {
        dword n_to_release;
        if (free_clusters <= leave_n_free)
            break;
        n_to_release = plist_iterator->nclusters;
        if (free_clusters - n_to_release < leave_n_free)
            n_to_release = free_clusters - leave_n_free;
        if (!fatop_free_frag(test_drive_structure(),FOP_RMTELL,0, plist_iterator->cluster, n_to_release))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero()) }

        free_clusters -= n_to_release;
        plist_iterator++;
    }
    if (!pc_diskflush(test_driveid))
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    pro_test_free(plist);
}
#else
static dword allocate_clusters_until(dword leave_n_free, int *pn_fragments)
{
dword n_this_fragment, hint_cluster,total_to_claim,first_in_chain,previous_end,first_new_cluster;
DRIVE_INFO drive_info_structure;

    RTFS_ARGSUSED_PVOID((void *)pn_fragments);

    if (!pc_diskio_info(test_driveid, &drive_info_structure, TRUE))
    {    ERTFS_ASSERT_TEST(rtfs_debug_zero()) }

	if (leave_n_free > drive_info_structure.free_clusters)
    {    ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
	total_to_claim = drive_info_structure.free_clusters - leave_n_free;
	first_in_chain = 0;
	previous_end = 0;
	hint_cluster = 2;
	while (total_to_claim)
	{
		/* fatop_alloc_chain(DDRIVE *pdr, BOOLEAN is_file, dword hint_cluster, dword previous_end, dword *pfirst_new_cluster, dword n_clusters, dword min_clusters) */
		n_this_fragment = fatop_alloc_chain(test_drive_structure(), FALSE, hint_cluster, previous_end, &first_new_cluster, total_to_claim, 1);
		if (n_this_fragment < 1)
			break;
		previous_end = first_new_cluster+n_this_fragment-1;
		hint_cluster = previous_end;
		total_to_claim -= n_this_fragment;
		if (first_in_chain == 0)
			first_in_chain = first_new_cluster;
	}
    if (!pc_diskflush(test_driveid))
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
	return(first_in_chain);
}

/* Release all clusters allocated from allocate_clusters_until() and release the list */
static void release_allocate_clusters_until(dword leave_n_free, int n_fragments, dword start_cluster)
{
    RTFS_ARGSUSED_DWORD(leave_n_free);
    RTFS_ARGSUSED_INT(n_fragments);

	if (!fatop_freechain(test_drive_structure(), start_cluster, LARGEST_DWORD))
	{ ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    if (!pc_diskflush(test_driveid))
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
}
#endif /*  (INCLUDE_RTFS_PROPLUS)  Uses ProPLus features, not supported under Pro */

/*
<TEST>   Test 1.
<TEST>   Test shrink of journal file due to disk filling
<TEST>   Enable failsafe
<TEST>   Create a directory
<TEST>   Check the size of failsafe reserved clusters
<TEST>   Allocate all free blocks from the memory manager
<TEST>   Check that failsafe reserved clusters are still reported as available
<TEST>   Create another directory
<TEST>   Make sure Failsafe file size was reduced by half and that free cluster counts are correct
<TEST>   Flush the journal and abort
<TEST>   Verify that the first directory is on the volume, because of synch that occured during resize
<TEST>   Restor
<TEST>   Verify that the second directory is on the volume, because of restore
<TEST>   Verify that free cluster counts are correct in all steps
<TEST>   Release blocks from the memory manager
<TEST>   Test with free manager enabled and with it disabled
*/
#if (1 || INCLUDE_RTFS_PROPLUS)  /* Uses ProPLus features, not supported under Pro */

static dword _fill_using_file(byte *file_name, dword cluster_size_bytes, dword n_clusters, int expected_errno)
{
    int fd;
    dword total;

    if ((fd = po_open(file_name, (word) (PO_BINARY|PO_WRONLY|PO_CREAT),(word) (PS_IWRITE | PS_IREAD) ) ) < 0)
    	return(0);

    for (total = 0; total < n_clusters; total++)
    {
        if (po_write(fd, 0, (int) cluster_size_bytes) != (int) cluster_size_bytes)
        {
            ERTFS_ASSERT_TEST(get_errno() == expected_errno)
            break;
        }
    }
    po_close(fd);
    return(total);

}

static BOOLEAN check_enabled(void)
{
    FAILSAFE_RUNTIME_STATS failsafe_stats;

    if (!pc_diskio_failsafe_stats(test_driveid, &failsafe_stats))
    { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    if (failsafe_stats.journaling_active)
        return(TRUE);
    else
        return(FALSE);

}

static void fstest_disk_full_1(BOOLEAN use_freespace_manager)
{
DRIVE_INFO drive_info_structure;
dword ltemp, sec_p_cluster,bytes_per_cluster;
dword free_clusters_zero, default_drive_policy, test_size_clusters,minimum_journal_size;
FAILSAFECONTEXT *pfstest_failsafe_context;
DDRIVE *pdr;

	pdr = test_drive_structure();

    if (use_freespace_manager)
        default_drive_policy = DRVPOL_DISABLE_AUTOFAILSAFE;
    else
        default_drive_policy = DRVPOL_DISABLE_AUTOFAILSAFE|DRVPOL_DISABLE_FREEMANAGER;


    PRO_TEST_ANNOUNCE("Test automatic shrinking of the failsafe file on disk full");
    PRO_TEST_ANNOUNCE("The test will take some time because it fills the volume");
    fs_api_disable(test_driveid, TRUE);
    /* Make sure test directories do not exist */
    pc_rmdir((byte *)"DISK_FULL_TEST_ONE_DIRECTORY_NAME");
    pc_rmdir((byte *)"DISK_FULL_TEST_TWO_DIRECTORY_NAME");
    pc_rmdir((byte *)"DISK_FULL_TEST_THREE_DIRECTORY_NAME");
    pc_rmdir((byte *)"DISK_FULL_TEST_FOUR_DIRECTORY_NAME");
    pc_unlink((byte *)"DISK_FULL_TEST_ONE_FILE_NAME");
    pc_unlink((byte *)"DISK_FULL_TEST_TWO_FILE_NAME");
    pc_unlink((byte *)"DISK_FULL_TEST_THREE_FILE_NAME");

    if (!pc_diskio_info(test_driveid, &drive_info_structure, TRUE))
    { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    free_clusters_zero = drive_info_structure.free_clusters;
    sec_p_cluster = drive_info_structure.cluster_size;
    bytes_per_cluster = sec_p_cluster * drive_info_structure.sector_size;

    minimum_journal_size = 1;
    test_size_clusters = minimum_journal_size * 8;
    {
        int n_fragments,n_fragments2;
#if (INCLUDE_RTFS_PROPLUS)
        FREELISTINFO *plist,*plist2;
#else
        dword plist,plist2;
#endif
        ERTFS_STAT st;
        /* Configure the drive to use test_size_clusters clusters for the journal */
        fstest_simple_drive_config(default_drive_policy, DEFAULT_BLOCKMAPSIZE, DEFAULT_INDEX_BUFFER_SIZE , DEFAULT_RESTORE_BUFFER_SIZE, test_size_clusters * sec_p_cluster);

        /* Force a mount and allocate all but test_size_clusters *2 from the disk */
        /* We do this with failsafe disabled so we don't flood the journal file later when we allocate all space */
        pc_blocks_free(test_driveid, &ltemp, &ltemp);
        plist = allocate_clusters_until(test_size_clusters*2, &n_fragments);

        /* Now enable failsafe */
        fs_api_disable(test_driveid, TRUE);

        if (!fs_api_enable(test_driveid, TRUE))
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        /* Force a mount */
        pc_blocks_free(test_driveid, &ltemp, &ltemp);

        /* Check that the journal is test_size_clusters long */
		pfstest_failsafe_context = fstest_failsafe_context();
        if ((pfstest_failsafe_context->nv_reserved_fragment.end_location - pfstest_failsafe_context->nv_reserved_fragment.start_location+1) != test_size_clusters)
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        /* Now make a directory */
        if (!pc_mkdir((byte *)"DISK_FULL_TEST_ONE_DIRECTORY_NAME"))
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }

        if (!pc_diskio_info(test_driveid, &drive_info_structure, TRUE))
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }

        /* Now consume all un-reserved free clusters */
        plist2 = allocate_clusters_until(0, &n_fragments2);


        /* And there should be test_size_clusters clusters free because reserved clusters appear as free */
        if (!pc_diskio_info(test_driveid, &drive_info_structure, TRUE))
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        if (drive_info_structure.free_clusters != test_size_clusters)
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }

        /* Now make another directory - this should force a flush and resize the Journal file, */
        if (!pc_mkdir((byte *)"DISK_FULL_TEST_TWO_DIRECTORY_NAME"))
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        /* The journal should be one cluster less now */
		pfstest_failsafe_context = fstest_failsafe_context();
        if ((pfstest_failsafe_context->nv_reserved_fragment.end_location - pfstest_failsafe_context->nv_reserved_fragment.start_location+1) != (test_size_clusters-1))
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }

        /* And there should be test_size_clusters-1 clusters free */
        if (!pc_diskio_info(test_driveid, &drive_info_structure, TRUE))
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        if (drive_info_structure.free_clusters != test_size_clusters-1)
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        /* Now commit the journal file but don't update the volume */
        /* And abort the mount.. and test for the first directory */
        if (!fs_api_commit(test_driveid, FALSE))
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }

        if (!fs_api_disable(test_driveid,TRUE))
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        /* the first directory should be there */
        if (pc_stat((byte *)"DISK_FULL_TEST_ONE_DIRECTORY_NAME", &st) != 0)
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        /* the second directory should not there */
        if (pc_stat((byte *)"DISK_FULL_TEST_TWO_DIRECTORY_NAME", &st) == 0)
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        /* And there should be test_size_clusters clusters free */
        if (!pc_diskio_info(test_driveid, &drive_info_structure, TRUE))
		{
			ERTFS_ASSERT_TEST(rtfs_debug_zero())
		}

        if (drive_info_structure.free_clusters != test_size_clusters)
        {
        FREELISTINFO *p;
        	p = allocate_clusters_until(drive_info_structure.free_clusters, &n_fragments);
			ERTFS_ASSERT_TEST(rtfs_debug_zero())
		}
        /* Now restore and then both should be there */
        if (!fs_api_restore(test_driveid))
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        /* the first directory should be there */
        if (pc_stat((byte *)"DISK_FULL_TEST_ONE_DIRECTORY_NAME", &st) != 0)
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        /* the second directory should be there */
        if (pc_stat((byte *)"DISK_FULL_TEST_TWO_DIRECTORY_NAME", &st) != 0)
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        /* And there should be test_size_clusters-1 clusters free */
        if (!pc_diskio_info(test_driveid, &drive_info_structure, TRUE))
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        if (drive_info_structure.free_clusters != test_size_clusters-1)
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }

        /* Now check that remounting forces to test_size_clusters - 1 */
        /* Configure the drive to use test_size_clusters clusters for the journal */
        fstest_simple_drive_config(default_drive_policy, DEFAULT_BLOCKMAPSIZE, DEFAULT_INDEX_BUFFER_SIZE ,DEFAULT_RESTORE_BUFFER_SIZE, test_size_clusters * sec_p_cluster);

		if (!fs_api_enable(test_driveid, TRUE))
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        /* Force a mount */

        pc_blocks_free(test_driveid, &ltemp, &ltemp);
        /* The journal should be test_size_clusters-1 clusters now */
		pfstest_failsafe_context = fstest_failsafe_context();
        if ((pfstest_failsafe_context->nv_reserved_fragment.end_location - pfstest_failsafe_context->nv_reserved_fragment.start_location+1) != (test_size_clusters-1))
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }

        /* Now consume one cluster for a file */
        if (_fill_using_file((byte *)"DISK_FULL_TEST_ONE_FILE_NAME", bytes_per_cluster, 1, 0) != 1)
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        if (!pc_diskio_info(test_driveid, &drive_info_structure, TRUE))
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        /* And there should be test_size_clusters-2 clusters free */
        if (drive_info_structure.free_clusters != test_size_clusters-2)
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }

        /* The journal should be test_size_clusters-2 clusters now */
		pfstest_failsafe_context = fstest_failsafe_context();
        if ((pfstest_failsafe_context->nv_reserved_fragment.end_location - pfstest_failsafe_context->nv_reserved_fragment.start_location+1) != (test_size_clusters-2))
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }

        /* Now consume blocks until the file is reduce to minimum_journal_size blocks */
		/* The journal size is now N = (test_size_clusters-2) - We should burn all N - 1 (test_size_clusters-3) clusters. (one sector is needed for the master record) */
        ltemp = test_size_clusters-3;

        if (_fill_using_file((byte *)"DISK_FULL_TEST_TWO_FILE_NAME", bytes_per_cluster, ltemp,0 ) != ltemp)
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }

        /* The journal should be 1 cluster now */
        if ((pfstest_failsafe_context->nv_reserved_fragment.end_location - pfstest_failsafe_context->nv_reserved_fragment.start_location+1) != 1)
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        if (!pc_diskio_info(test_driveid, &drive_info_structure, TRUE))
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }

        /* Now consume one more cluster, the journal file can not be reduced */
        {
            /* The allocation should not be granted but journaling should still be enables */
            if (_fill_using_file((byte *)"DISK_FULL_TEST_THREE_FILE_NAME", bytes_per_cluster, 1, PENOSPC) != 0)
            { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
            if (!check_enabled())
            { ERTFS_ASSERT_TEST(rtfs_debug_zero())}
            /* Commit the journal and shut down Journalling */
            if (!fs_api_commit(test_driveid, TRUE))
            { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
            fs_api_disable(test_driveid, TRUE);
        }

        /* Clean up */
        pc_rmdir((byte *)"DISK_FULL_TEST_ONE_DIRECTORY_NAME");
        pc_rmdir((byte *)"DISK_FULL_TEST_TWO_DIRECTORY_NAME");
        pc_rmdir((byte *)"DISK_FULL_TEST_THREE_DIRECTORY_NAME");
        pc_rmdir((byte *)"DISK_FULL_TEST_FOUR_DIRECTORY_NAME");
        pc_unlink((byte *)"DISK_FULL_TEST_ONE_FILE_NAME");
        pc_unlink((byte *)"DISK_FULL_TEST_TWO_FILE_NAME");
        pc_unlink((byte *)"DISK_FULL_TEST_THREE_FILE_NAME");

        /* Now that it is restored release all fill clusters */
        release_allocate_clusters_until(0, n_fragments2, plist2);
        release_allocate_clusters_until(test_size_clusters*2, n_fragments, plist);

        /* Check that all fill clusters where returned */
        if (!pc_diskio_info(test_driveid, &drive_info_structure, TRUE))
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        if (drive_info_structure.free_clusters != free_clusters_zero)
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    }
}
/*
<TEST>   Test 2.
<TEST>   Test initial reduction of Journal file due to disk filling
<TEST>   Fill the disk with all but a few cluster
<TEST>   Start Journaling and verify that a reduced journal file is produced
<TEST>   Test with free manager enabled and with it disabled

*/
static void fstest_disk_full_2(BOOLEAN use_freespace_manager)
{
DRIVE_INFO drive_info_structure;
dword ltemp, sec_p_cluster,test_size_clusters,minimum_journal_size;
int n_fragments;
#if (INCLUDE_RTFS_PROPLUS)
FREELISTINFO *plist;
#else
dword plist;
#endif
dword default_drive_policy;
FAILSAFECONTEXT *pfstest_failsafe_context;

    if (use_freespace_manager)
        default_drive_policy = DRVPOL_DISABLE_AUTOFAILSAFE;
    else
        default_drive_policy = DRVPOL_DISABLE_AUTOFAILSAFE|DRVPOL_DISABLE_FREEMANAGER;

    PRO_TEST_ANNOUNCE("Test automatic shrinking of the failsafe during start");
    PRO_TEST_ANNOUNCE("The test will take some time because it fills the volume");

    fs_api_disable(test_driveid, TRUE);

    if (!pc_diskio_info(test_driveid, &drive_info_structure, TRUE))
    { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }

    minimum_journal_size = 1;

    sec_p_cluster = drive_info_structure.cluster_size;

    test_size_clusters = minimum_journal_size * 4;
    /* Configure the drive to use test_size_clusters clusters for the journal */
    fstest_simple_drive_config(default_drive_policy, DEFAULT_BLOCKMAPSIZE,  DEFAULT_INDEX_BUFFER_SIZE ,DEFAULT_RESTORE_BUFFER_SIZE, test_size_clusters * sec_p_cluster);
    if (!fs_api_enable(test_driveid, TRUE))
    { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    /* Force a mount */
    pc_blocks_free(test_driveid, &ltemp, &ltemp);
    /* Check that the journal is test_size_clusters clusters long */
	pfstest_failsafe_context = fstest_failsafe_context();
    if (pfstest_failsafe_context->nv_reserved_fragment.end_location - pfstest_failsafe_context->nv_reserved_fragment.start_location != (test_size_clusters-1))
   { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    fs_api_disable(test_driveid, TRUE);

    /* Now consume all clusters except test_size_clusters/2 */
    plist = allocate_clusters_until(test_size_clusters/2, &n_fragments);

    /* Configure the drive to use test_size_clusters clusters for the journal */
    fstest_simple_drive_config(DRVPOL_DISABLE_AUTOFAILSAFE, DEFAULT_BLOCKMAPSIZE,  DEFAULT_INDEX_BUFFER_SIZE ,DEFAULT_RESTORE_BUFFER_SIZE, test_size_clusters * sec_p_cluster);
    if (!fs_api_enable(test_driveid, TRUE))
    { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    /* Force a mount */
    pc_blocks_free(test_driveid, &ltemp, &ltemp);
    /* Check that the journal is test_size_clusters/2 clusters long */
	pfstest_failsafe_context = fstest_failsafe_context();
    if (pfstest_failsafe_context->nv_reserved_fragment.end_location - pfstest_failsafe_context->nv_reserved_fragment.start_location != ((test_size_clusters/2)-1))
   { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    fs_api_disable(test_driveid, TRUE);


    /* Force a mount */
    pc_blocks_free(test_driveid, &ltemp, &ltemp);
    /* Now this it is restored release all fill clusters */
    release_allocate_clusters_until(test_size_clusters/2, n_fragments, plist);

    /* Now consume all clusters except minimum_journal_size -1 */
    plist = allocate_clusters_until(minimum_journal_size-1, &n_fragments);
    /* Configure the drive to use test_size_clusters clusters for the journal */
    fstest_simple_drive_config(DRVPOL_DISABLE_AUTOFAILSAFE, DEFAULT_BLOCKMAPSIZE,  DEFAULT_INDEX_BUFFER_SIZE ,DEFAULT_RESTORE_BUFFER_SIZE, test_size_clusters * sec_p_cluster);
    if (!fs_api_enable(test_driveid, TRUE))
    { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }

    if (fs_api_cb_disable_on_full(test_drive_structure()))
    {
        /* Force a mount */
        if (!pc_blocks_free(test_driveid, &ltemp, &ltemp))
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        if (check_enabled())
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    }
    else
    {
        /* Force a mount - should Fail.  */
        if (pc_blocks_free(test_driveid, &ltemp, &ltemp))
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        /* Errno should be set to PEFSCREATE */
        ERTFS_ASSERT_TEST(get_errno() == PEFSCREATE)
        /* Now disable Failsafe so we can run */
        fs_api_disable(test_driveid, TRUE);
        /* Force a mount */
        if (!pc_blocks_free(test_driveid, &ltemp, &ltemp))
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    }
    /* Now this it is restored release all fill clusters */
    release_allocate_clusters_until(minimum_journal_size-1, n_fragments, plist);

    /* Now consume all clusters except 1 */
    plist = allocate_clusters_until(1, &n_fragments);
    /* Configure the drive to use test_size_clusters clusters for the journal */
    fstest_simple_drive_config(DRVPOL_DISABLE_AUTOFAILSAFE, DEFAULT_BLOCKMAPSIZE,  DEFAULT_INDEX_BUFFER_SIZE ,DEFAULT_RESTORE_BUFFER_SIZE, test_size_clusters * sec_p_cluster);
    if (!fs_api_enable(test_driveid, TRUE))
    { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    /* Force a mount */
    pc_blocks_free(test_driveid, &ltemp, &ltemp);

    /* The journal file is only one cluster wide, almost full.. If the cluster size is < 4 we won't have enough
       space and journaling will be disabled */
    if (check_enabled())
    {
        /* Check that the journal is 1 clusters long */
		pfstest_failsafe_context = fstest_failsafe_context();
        if (pfstest_failsafe_context->nv_reserved_fragment.end_location - pfstest_failsafe_context->nv_reserved_fragment.start_location != 0)
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        fs_api_disable(test_driveid, TRUE);
    }
    /* Force a mount */
    pc_blocks_free(test_driveid, &ltemp, &ltemp);
    /* Now this it is restored release all fill clusters */
    release_allocate_clusters_until(1, n_fragments, plist);
}
#endif /* (INCLUDE_RTFS_PROPLUS) */

extern dword failsafe_fixed_start;
extern dword failsafe_fixed_size;

static void fs_test_place_fixed(dword size_in_sectors, dword *pfixed_start_sector, dword *pfixed_start_cluster, dword *psize_clusters)
{
DRIVE_INFO drive_info_structure;
DDRIVE *pdr;

    if (!pc_diskio_info(test_driveid, &drive_info_structure, TRUE))
    {    ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    *psize_clusters = (size_in_sectors + drive_info_structure.cluster_size - 1)/drive_info_structure.cluster_size;

	pdr = test_drive_structure();
	if (!pdr)
	{ ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
	/* fatop_alloc_chain(DDRIVE *pdr, BOOLEAN is_file, dword hint_cluster, dword previous_end, dword *pfirst_new_cluster, dword n_clusters, dword min_clusters) */
	if (fatop_alloc_chain(pdr, FALSE, 2, 0, pfixed_start_cluster, *psize_clusters, *psize_clusters) != *psize_clusters)
	{ ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    if (!pc_diskflush(test_driveid))
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }

	*pfixed_start_sector = pc_cl2sector(pdr , *pfixed_start_cluster);
	*pfixed_start_sector += pdr->drive_info.partition_base;
}
static void fs_test_remove_fixed_journal(dword fixed_start_cluster, dword size_in_clusters)
{
DDRIVE *pdr;
	pdr = test_drive_structure();
	if (!pdr)
	{ ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
#if (INCLUDE_EXFATORFAT64)
	if (ISEXFATORFAT64(pdr))
	{
      	if (!exfatop_add_free_region(pdr, fixed_start_cluster, size_in_clusters, TRUE))
		{ ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
	}
	else
#endif
	if (!fatop_freechain(pdr, fixed_start_cluster, size_in_clusters))
		{ ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    if (!pc_diskflush(test_driveid))
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
}
static void fs_test_fixed_journal(dword size_in_sectors)
{
dword ltemp;
ERTFS_STAT st;
FAILSAFECONTEXT *pfstest_failsafe_context;
dword fixed_start_cluster, size_clusters;
    PRO_TEST_ANNOUNCE("Test fixed journal file placement ");
    fs_api_disable(test_driveid, TRUE);
    /* Configure Failsafe for a 128 block file, but we will override this
       use local configuration because we will inspect the context block */
    fstest_simple_drive_config(DRVPOL_DISABLE_AUTOFAILSAFE, DEFAULT_BLOCKMAPSIZE,  DEFAULT_INDEX_BUFFER_SIZE ,DEFAULT_RESTORE_BUFFER_SIZE, 128);
    pc_rmdir((byte *)"TEST_DIR");
    failsafe_fixed_size =  size_in_sectors;
    /* failsafe_fixed_size and failsafe_fixed_start are global, used by the failsafe internals */
    fs_test_place_fixed(failsafe_fixed_size, &failsafe_fixed_start, &fixed_start_cluster, &size_clusters);


     fs_api_disable(test_driveid, TRUE);
    /* Now enable failsafe and remount */
    if (!fs_api_enable(test_driveid, TRUE))
    { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    /* Force a mount */
    pc_blocks_free(test_driveid, &ltemp, &ltemp);

    /* Now check the journal context.
        Should be raw IO mode, to blocks in failsafe_fixed_start to failsafe_fixed_size
        no reserved clusters or cluster handle */
	pfstest_failsafe_context = fstest_failsafe_context();
    if (
        !pfstest_failsafe_context->nv_raw_mode ||
        pfstest_failsafe_context->nv_buffer_handle != failsafe_fixed_start ||
        pfstest_failsafe_context->journal_file_size != size_in_sectors ||
        pfstest_failsafe_context->nv_reserved_fragment.start_location != 0 ||
        pfstest_failsafe_context->nv_cluster_handle != 0 )

    {
        ERTFS_ASSERT_TEST(rtfs_debug_zero())
    }
    /* Now make a directory with journaling to a fixed range */
    if (!pc_mkdir((byte *)"TEST_DIR"))
    { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }

    /* Now commit the journal file but don't update the volume */
    /* And abort the mount.. and test for the first directory */
    if (!fs_api_commit(test_driveid, FALSE))
    { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    if (!fs_api_disable(test_driveid,TRUE))
    { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    /* the directory should not be there */
    if (pc_stat((byte *)"TEST_DIR", &st) == 0)
    { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    /* Now restore */
    if (!fs_api_restore(test_driveid))
    { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    /* the directory should be there */
    if (pc_stat((byte *)"TEST_DIR", &st) != 0)
    { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }

    fs_test_remove_fixed_journal(fixed_start_cluster, size_clusters);
    pc_rmdir((byte *)"TEST_DIR");
    /* Now restore callback values for fs_api_cb_journal_fixed() */
    failsafe_fixed_start = 0;
    failsafe_fixed_size  = 0;
}

static void fstest_journal_full(void)
{
int fd;
byte attr;
ERTFS_STAT st;
FAILSAFECONTEXT *pfstest_failsafe_context;

    /* Create a small Journal file (128) blocks. Failsafe will round up to a FAT page boundary */
    fstest_simple_drive_config(DRVPOL_DISABLE_AUTOFAILSAFE, DEFAULT_BLOCKMAPSIZE,  DEFAULT_INDEX_BUFFER_SIZE ,DEFAULT_RESTORE_BUFFER_SIZE, 128);
    /* Create a file we will use to fill the journal */
    pc_unlink((byte *)"FSTEST_1");
    pc_unlink((byte *)"FSTEST_2");
    fd = create_file_for_fstest((byte *)"FSTEST_1");
    ERTFS_ASSERT_TEST(fd >= 0)
    if (!close_file_for_fstest(fd))
   { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    if (!pc_get_attributes((byte *)"FSTEST_1", &attr))
    { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }

    /* Turn on failsafe */
    fs_api_disable(test_driveid, TRUE);
    if (!fs_api_enable(test_driveid, TRUE))
    {
        ERTFS_ASSERT_TEST(rtfs_debug_zero())
    }

    /* Create a file that will only reside in the journal */
    fd = create_file_for_fstest((byte *)"FSTEST_2");
    ERTFS_ASSERT_TEST(fd >= 0)
    if (!close_file_for_fstest(fd))
   { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }

    /* Use up most of the frames */
	pfstest_failsafe_context = fstest_failsafe_context();
    while (pfstest_failsafe_context->fs_journal.frames_free > 2)
        fs_consume_frames((byte *)"FSTEST_1", 2);

    /* Use up one more frame - will always succeed unless the frame resides at the end of file */
   if (!pc_set_attributes((byte *)"FSTEST_1", attr))
   {  ERTFS_ASSERT_TEST(get_errno() == PEFSJOURNALFULL) }
   else
   {
        /* Now consume the frame by flushing the journal file */
        if (!fs_api_commit(test_driveid, FALSE))
        {     ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
   }
   /* Try to journal something.. it should fail with a full error */
   if (pc_set_attributes((byte *)"FSTEST_1", attr))
   {
        ERTFS_ASSERT_TEST(rtfs_debug_zero()) /* Should have failed */
   }
   else
   {
        ERTFS_ASSERT_TEST(get_errno() == PEFSJOURNALFULL)
   }
   /* Now abort */
   fs_api_disable(test_driveid, TRUE);

    /* the new file should not be there */
    if (pc_stat((byte *)"FSTEST_2", &st) == 0)
    { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    /* Now restore */
   if (!fs_api_restore(test_driveid))
   { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    /* the new file should be there */
    if (pc_stat((byte *)"FSTEST_2", &st) != 0)
    { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
   pc_unlink((byte *)"FSTEST_1");
   pc_unlink((byte *)"FSTEST_2");

}


static void fs_consume_frames(byte * fname, dword nframes)
{
dword frames_left = nframes;
byte attr;

    if (!pc_get_attributes(fname, &attr))
    { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }

    while (frames_left >= 2)
    {
        if (!pc_set_attributes(fname, attr))
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
       /* Now consume 2 frame by flushing the journal file with a changed index and directory block */
       if (!fs_api_commit(test_driveid, FALSE))
       { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
       frames_left -= 2;
    }


}

#if (!INCLUDE_RTFS_PROPLUS)
int fs_restore_from_session_start(FAILSAFECONTEXT *pfscntxt);
int fs_restore_from_session_continue(FAILSAFECONTEXT *pfscntxt);
#endif

static void fstest_journal_wrap(void)
{
int fd, status;
dword leading_replacement_records;
FSINFO fs_info_struct;
ERTFS_STAT st;
FAILSAFECONTEXT *pfstest_failsafe_context;

    /* Create a small Journal file  */
    fstest_simple_drive_config(DRVPOL_DISABLE_AUTOFAILSAFE, DEFAULT_BLOCKMAPSIZE,  DEFAULT_INDEX_BUFFER_SIZE ,DEFAULT_RESTORE_BUFFER_SIZE, 128);
    /* don't use < 2 in this loop because fs_consume_frames() requires >= 2 */
    for (leading_replacement_records = 2; leading_replacement_records < 125; leading_replacement_records += 1)
    {
        RTFS_PRINT_STRING_1((byte *)"Testing file wrap by : ", 0);
        RTFS_PRINT_LONG_1(leading_replacement_records, PRFLG_NL);

        fs_api_disable(test_driveid, TRUE);
        pc_unlink((byte *)"FSTEST_1");
        pc_unlink((byte *)"FSTEST_2");

        /* create an empty 32 bit file */
        fd = create_file_for_fstest((byte *)"FSTEST_1");
        ERTFS_ASSERT_TEST(fd >= 0)
        if (!close_file_for_fstest(fd))
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }

        /* Turn on failsafe */
        fs_api_disable(test_driveid, TRUE);
        if (!fs_api_enable(test_driveid, TRUE))
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }

        /* use up "leading_replacement_records" in the journal */
        fs_consume_frames((byte *)"FSTEST_1", leading_replacement_records);

#if (INCLUDE_RTFS_PROPLUS)
        /* Now do an async commit up to the restore point so a new frame is started */
        if (fs_api_async_commit_start(test_driveid) != PC_ASYNC_CONTINUE)
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        status = pc_async_continue(pc_path_to_driveno(test_driveid, CS_CHARSET_NOT_UNICODE),DRV_ASYNC_DONE_JOURNALFLUSH, 0);
        /* Step twice to write the restore start and end block */
        status = pc_async_continue(pc_path_to_driveno(test_driveid, CS_CHARSET_NOT_UNICODE),DRV_ASYNC_IDLE, 2);
#else
		pfstest_failsafe_context = fstest_failsafe_context();
		if (fs_restore_from_session_start(pfstest_failsafe_context )!= PC_ASYNC_CONTINUE)
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        /* Step twice to write the restore start and end block */

        if (fs_restore_from_session_continue(pfstest_failsafe_context)!= PC_ASYNC_CONTINUE)
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        if (fs_restore_from_session_continue(pfstest_failsafe_context)!= PC_ASYNC_CONTINUE)
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
#endif
        /* Now consume  2 frames to start a frame */
        fs_consume_frames((byte *)"FSTEST_1", 2);
        /* Now complete the restore of the blocks blocks at the beginning of the file */
#if (INCLUDE_RTFS_PROPLUS)
        status = pc_async_continue(pc_path_to_driveno(test_driveid, CS_CHARSET_NOT_UNICODE),DRV_ASYNC_DONE_RESTORE, 0);
        if (status !=  PC_ASYNC_COMPLETE)
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
#else
		pfstest_failsafe_context = fstest_failsafe_context();
		do {
			status = fs_restore_from_session_continue(pfstest_failsafe_context);
			} while (status == PC_ASYNC_CONTINUE);
		if (status != PC_ASYNC_COMPLETE)
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
#endif
        /* Now consume all but 2 frames to forces a wrap */
		pfstest_failsafe_context = fstest_failsafe_context();
        fs_consume_frames((byte *)"FSTEST_1", pfstest_failsafe_context->fs_journal.frames_free - 2);

        /* No create another empty 32 bit file */
        fd = create_file_for_fstest((byte *)"FSTEST_2");
        ERTFS_ASSERT_TEST(fd >= 0)
        if (!close_file_for_fstest(fd))
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }

        /* Now commit the journal file but don't update the volume */
        if (!fs_api_commit(test_driveid, FALSE))
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }

        /* Now abort, the start record should be offset */
        fs_api_disable(test_driveid, TRUE);

        if (!fs_api_info(test_driveid,&fs_info_struct))
        {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        /* Should be valid, recomended */
        if (!(fs_info_struct.journal_file_valid && fs_info_struct.restore_recommended))
        {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        /* start session frame will be offset  */
        if (fs_info_struct._start_session_frame == 1)
        {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }

        /* the new file should not be there */
        if (pc_stat((byte *)"FSTEST_2", &st) == 0)
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }

        if (!fs_api_restore(test_driveid))
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }

        /* the new file should be there */
        if (pc_stat((byte *)"FSTEST_2", &st) != 0)
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    }
    pc_unlink((byte *)"FSTEST_1");
    pc_unlink((byte *)"FSTEST_2");
}

static void fstest_master_errors(int which_field);
static void fstest_frame_errors(int which_field);
static void restore_fs_file_location(dword saved_fs_location);
static dword get_fs_file_location(void);

static void fstest_journal_errors(int which_test)
{
FSINFO fs_info_struct;
dword saved_fs_location;
    /* Create a normal journal file */
    fstest_simple_drive_config(DRVPOL_DISABLE_AUTOFAILSAFE,  DEFAULT_BLOCKMAPSIZE, DEFAULT_INDEX_BUFFER_SIZE ,DEFAULT_RESTORE_BUFFER_SIZE, 0);
    /* Turn on failsafe */
    if (!fs_api_enable(test_driveid, TRUE))
    {
        ERTFS_ASSERT_TEST(rtfs_debug_zero())
    }
    /* Now create a 32 bit file, make it large enough to consume 3 frames  */
    fstest_create_file_journaled((byte *)"FSTEST_1", 3);
    /* Commit but don't sync fats */
    fs_api_commit(test_driveid,FALSE);
    /* Now abort and leave a file */
    /* Save off a copy of the context because we are disabling and reserved fragments here and the location of the journal file */
    save_fs_context();
    saved_fs_location = get_fs_file_location();
    fs_api_disable(test_driveid, TRUE);

    /* Now get stats */
    if (!fs_api_info(test_driveid,&fs_info_struct))
    {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    /* Should be valid, recomended */
    if (!(fs_info_struct.journal_file_valid && fs_info_struct.restore_recommended))
    {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    switch (which_test)
    {
    case CHECK_OUT_OF_DATE:
        /* Now make a directory - with failsafe disabled */
    /* Reserve the clusters that were used by the journal so we detect freesapce change. If we do not
       reserve them the journal file sectors will probably be allocated and overwritten because they are actually
       free. This wuould result in fs_info_struct.journal_file_valid is FALSE, which is correct but not what
       we are testing for */
       reserve_fs_reserved_clusters();
       if (!pc_mkdir((byte *)"FSTEST_DIR_1"))
       {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    /* Reserve the clusters that were used by the journal so we detect freesapce change. If we do not
       reserve them the journal file sectors will probably be allocated and overwritten because they are actually
       free. This wuould result in fs_info_struct.journal_file_valid is FALSE, which is correct but not what
       we are testing for */
       release_fs_reserved_clusters();

        /* Now get stats */
        if (!fs_api_info(test_driveid,&fs_info_struct))
        {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        /* May not be valid because FAT copy 2 was updated */
        if (!fs_info_struct.journal_file_valid)
        {
            /* Now restore the location in FAT 2 and it should be able to find the file */
            restore_fs_file_location(saved_fs_location);
        }
        /* Now get stats */
        if (!fs_api_info(test_driveid,&fs_info_struct))
        {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        /* Should be valid, out of date */
        if (!fs_info_struct.journal_file_valid ||
            !fs_info_struct.out_of_date)
            {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
            /* Now remove the directory - with failsafe disabled - will restore
            freespace */
            if (!pc_rmdir((byte *)"FSTEST_DIR_1"))
            {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
            /* Now restore the location in FAT 2 so we can find the file */
            restore_fs_file_location(saved_fs_location);
            /* Now get stats */
            if (!fs_api_info(test_driveid,&fs_info_struct))
            {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
            /* Should be valid, recomended */
            if (!(fs_info_struct.journal_file_valid && fs_info_struct.restore_recommended))
            {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    break;
    case CHECK_BAD_MASTER:
        fstest_master_errors(FS_MASTER_OFFSET_SIGNATURE_1);
        fstest_master_errors(FS_MASTER_OFFSET_SIGNATURE_2);
        fstest_master_errors(FS_MASTER_OFFSET_VERSION);
/*       fstest_master_errors(FS_MASTER_OFFSET_FSIZE); */
/*        fstest_master_errors(FS_MASTER_OFFSET_SESSION); */
    break;
    /* check errors in frame */
    case CHECK_BAD_FRAME:
        fstest_frame_errors(FS_FRAME_OFFSET_TYPE);
        fstest_frame_errors(FS_FRAME_OFFSET_SEGMENT_CHECKSUM);
        fstest_frame_errors(FS_FRAME_OFFSET_FRAME_CHECKSUM);
        fstest_frame_errors(FS_FRAME_OFFSET_FRAME_SEQUENCE);
        fstest_frame_errors(FS_FRAME_OFFSET_FRAME_RECORDS);
        fstest_frame_errors(FS_FRAME_OFFSET_FAT_FREESPACE);
        fstest_frame_errors(FS_FRAME_OFFSET_SESSION_ID);
        fstest_frame_errors(FS_FRAME_HEADER_SIZE);
    break;
    };
    restore_fs_context();
}

static FAILSAFECONTEXT *fstest_failsafe_context(void)
{
DDRIVE *ptest_drive;
	FAILSAFECONTEXT *pcontext = 0;
	/* If failsafe is closed but we saved a copy, use that */
    if (pcopy_of_context)
    	return(pcopy_of_context);
	ptest_drive = test_drive_structure();
	if (ptest_drive)
		pcontext = (FAILSAFECONTEXT *) ptest_drive->drive_state.failsafe_context;
	if (!pcontext)
    { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
	return(pcontext);
}

/* Restore the handle to the journal in the second FAT so our tests will proceed. If we
   don't do this we will get no journal errors instead of out of date errors */
static void restore_fs_file_location(dword saved_fs_location)
{
	/* This is done offline so we use a fake context structure so we can cll the failsafe nvio function, we initialize the fileds we need */
    if (saved_fs_location)
    {
    BLKBUFF *buf;
    BLKBUFF bbuf_scratch;   /* Used by pc_sys_sector build a buffer structure with core from the driver level */
    FAILSAFECONTEXT *pfakecntxt;
    byte *pbuffer;
    	pfakecntxt = (FAILSAFECONTEXT *) pro_test_malloc(sizeof(FAILSAFECONTEXT));
    	pfakecntxt->nv_cluster_handle =  saved_fs_location;
    	pfakecntxt->pdrive = test_drive_structure();

    	buf = pc_sys_sector(pfakecntxt->pdrive, &bbuf_scratch);
    	if (!buf)
    	{ ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    	pbuffer = buf->data;
    	if (!failsafe_nv_buffer_mark(pfakecntxt, pbuffer))
    	{ ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
		pro_test_free(pfakecntxt);
    	pc_free_sys_sector(buf);
	}
}

static dword get_fs_file_location(void)
{
FAILSAFECONTEXT *pfstest_failsafe_context;
	pfstest_failsafe_context = fstest_failsafe_context();
    return(pfstest_failsafe_context->nv_cluster_handle);
}


static void fstest_frame_errors(int which_field)
{
FSINFO fs_info_struct;
dword  *my_dw_buffer;
dword saved_val;
dword frame_number;
FAILSAFECONTEXT *pfstest_failsafe_context;

        if (!pro_test_bytes_per_sector())
        {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }

        my_dw_buffer = (dword *) pro_test_malloc(pro_test_bytes_per_sector());
        if (!my_dw_buffer)
        {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }

        if (!fs_api_info(test_driveid,&fs_info_struct))
        {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }

        /* 0 = master, 1 == first frame, 1 + pfstest_failsafe_context->fs_frame_max_records + 1 == 2nd frame */
        pfstest_failsafe_context = fstest_failsafe_context();
        frame_number = 1 + pfstest_failsafe_context->fs_frame_max_records + 1;
        /* Read the frame */
        save_fs_file_handle();  /* Save file info */
        if (!failsafe_nv_buffer_io(pfstest_failsafe_context,
            frame_number, 1, (byte *) my_dw_buffer,TRUE))
        {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        /* Test Bad value */
        saved_val = *(my_dw_buffer+which_field);
        *(my_dw_buffer+which_field) = ~saved_val;
        if (!failsafe_nv_buffer_io(pfstest_failsafe_context,
            frame_number, 1, (byte *) my_dw_buffer,FALSE))
        {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        if (!fs_api_info(test_driveid,&fs_info_struct))
        {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        /* Restore journal file handle so we can write. It was cleared
           by fs_api_info, because it is not valid */
        restore_fs_file_handle();

/*
        fs_info_struct.journal_file_valid
        fs_info_struct.restore_recommended);
        fs_info_struct.check_sum_fails);
        if (fs_info_struct.journal_file_valid)
        {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
Note: Need to fix these tests
*/
        switch (which_field) {
            case FS_FRAME_OFFSET_TYPE:
            break;
            case FS_FRAME_OFFSET_SEGMENT_CHECKSUM:
            break;
            case FS_FRAME_OFFSET_FRAME_CHECKSUM:
            break;
            case FS_FRAME_OFFSET_FRAME_SEQUENCE:
            break;
            case FS_FRAME_OFFSET_FRAME_RECORDS:
            break;
            case FS_FRAME_OFFSET_FAT_FREESPACE:
            break;
            case FS_FRAME_OFFSET_SESSION_ID:
            break;
            case FS_FRAME_HEADER_SIZE:
            break;
        }
        /* Restore master */
        *(my_dw_buffer+which_field) = saved_val;
        if (!failsafe_nv_buffer_io(pfstest_failsafe_context,
            frame_number, 1, (byte *) my_dw_buffer,FALSE))
        {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        if (!fs_api_info(test_driveid,&fs_info_struct))
        {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        /* Should be valid */
        if (!fs_info_struct.journal_file_valid)
        {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }

        pro_test_free((void *)my_dw_buffer);
}

static void fstest_master_errors(int which_field)
{
FSINFO fs_info_struct;
dword  *my_dw_buffer;
dword saved_val;
FAILSAFECONTEXT *pfstest_failsafe_context;

        if (!pro_test_bytes_per_sector())
        {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        my_dw_buffer = (dword *) pro_test_malloc(pro_test_bytes_per_sector());
        if (!my_dw_buffer)
        {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        save_fs_file_handle();  /* Save file hadnle */
        /* Read record zero */
        pfstest_failsafe_context = fstest_failsafe_context();
        if (!failsafe_nv_buffer_io(pfstest_failsafe_context,
            0, 1, (byte *) my_dw_buffer,TRUE))
        {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        /* Test Bad signature */
        saved_val = *(my_dw_buffer+which_field);
        *(my_dw_buffer+which_field) = ~saved_val;
        if (!failsafe_nv_buffer_io(pfstest_failsafe_context,
            0, 1, (byte *) my_dw_buffer,FALSE))
        {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        if (!fs_api_info(test_driveid,&fs_info_struct))
        {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        /* Should be in-valid */
        if (fs_info_struct.journal_file_valid)
        {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        /* Restore master */
        /* Restore journal file handle so we can write. It was cleared
           by fs_api_info, because it is not valid */
        restore_fs_file_handle();  /* Save file hadnle */
        *(my_dw_buffer+which_field) = saved_val;
        if (!failsafe_nv_buffer_io(pfstest_failsafe_context,
            0, 1, (byte *) my_dw_buffer,FALSE))
        {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        if (!fs_api_info(test_driveid,&fs_info_struct))
        {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        /* Should be valid */
        if (!fs_info_struct.journal_file_valid)
        {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        pro_test_free((void *)my_dw_buffer);
}


#define TSTOP_CHKPOINT_A        0
#define TSTOP_CHKPOINT_B        1
#define TSTOP_CHKPOINT_C        2
#define TSTOP_CMPCHKPOINT_A     3
#define TSTOP_CMPCHKPOINT_B     4
#define TSTOP_CMPCHKPOINT_C     5
#define TSTOP_CREATE_1          6
#define TSTOP_CREATE_2          7
#define TSTOP_CREATE_3          8
#define TSTOP_DELETE_1          9
#define TSTOP_DELETE_2         10
#define TSTOP_DELETE_3         11
#define TSTOP_FSENABLE         12
#define TSTOP_FSABORT          13
#define TSTOP_FSFLUSH          14
#define TSTOP_RESTORE          15
#define TSTOP_FSSYNC           16
#define TSTOP_FSSYNC_PARTIAL   17
#define TSTOP_NOTCHKPOINT_B    18
#define TSTOP_FSSYNC_START     19
#define TSTOP_FSSYNC_DONE      20
#define TSTOP_FORCE_JCREATE    21


static void do_op(int test_op);

static void fstest_journaling(int which_test)
{
    /* Create a normal journal file - enable ASYNC flush */
#if (INCLUDE_RTFS_PROPLUS)
    fstest_simple_drive_config(DRVPOL_DISABLE_AUTOFAILSAFE|DRVPOL_ASYNC_FATFLUSH|DRVPOL_ASYNC_JOURNALFLUSH|DRVPOL_ASYNC_RESTORE,
        DEFAULT_BLOCKMAPSIZE,  DEFAULT_INDEX_BUFFER_SIZE ,DEFAULT_RESTORE_BUFFER_SIZE,0);
#else
    fstest_simple_drive_config(DRVPOL_DISABLE_AUTOFAILSAFE,
        DEFAULT_BLOCKMAPSIZE,  DEFAULT_INDEX_BUFFER_SIZE ,DEFAULT_RESTORE_BUFFER_SIZE,0);
#endif
    switch (which_test) {
    case TEST_NOJFLUSH:         /* Journal don't flush                */
    do_op(TSTOP_CHKPOINT_A);
    do_op(TSTOP_CMPCHKPOINT_A); /* Verify that the volume is unchanged*/
    do_op(TSTOP_FSENABLE);
    do_op(TSTOP_CREATE_1);      /* Create a file with Journaling      */
    do_op(TSTOP_FSABORT);       /* Abort the journaling seesion       */
    do_op(TSTOP_CMPCHKPOINT_A); /* Verify that the volume is unchanged*/
    break;

    case TEST_DISK_RESTORE:       /* Journal,flush,restore            */
    do_op(TSTOP_FORCE_JCREATE);    /* Enable and then abort and save copy of the context structure so we know we have a journal */
    do_op(TSTOP_CHKPOINT_A);      /* Save a check point               */
    /* Reserve the clusters that were used by the journal so we detect freesapce change. If we do not
       reserve them the journal file sectors will probably be allocated and overwritten because they are actually
       free. This wuould result in fs_info_struct.journal_file_valid is FALSE, which is correct but not what
       we are testing for */
       reserve_fs_reserved_clusters();
    do_op(TSTOP_CREATE_1);        /* Create a file with no Journaling */
    do_op(TSTOP_CHKPOINT_B);      /* Save a check point               */
    do_op(TSTOP_DELETE_1);        /* Delete the file                  */
    do_op(TSTOP_CMPCHKPOINT_A);   /* Verify unchanged                 */
    release_fs_reserved_clusters();
    restore_fs_context();		  /* Clear pcopy_of_context so we access the real context structure */
    do_op(TSTOP_FSENABLE);        /* Enable Journaling                */
    do_op(TSTOP_CREATE_1);        /* Create the file with Journaling  */
    do_op(TSTOP_FSFLUSH);         /* Flush the Journal                */
    do_op(TSTOP_FSABORT);         /* Abort, don't synchronize         */
    do_op(TSTOP_CMPCHKPOINT_A);   /* Verify volume unchanged          */
    do_op(TSTOP_RESTORE);         /* Restore the Volume               */
    do_op(TSTOP_CMPCHKPOINT_B);   /* Verify on volume after restore   */
    do_op(TSTOP_DELETE_1);
    break;

    case TEST_RAM_RESTORE:         /* Journal,flush,synchronize        */
    do_op(TSTOP_FSENABLE);
    save_fs_context();
    do_op(TSTOP_FSABORT);          /* Turn off journal                 */
    do_op(TSTOP_CHKPOINT_A);       /* Save a check point               */
    /* Reserve the clusters that are used by the journal. If we do not
       reserve them the journal file sectors will probably be allocated and overwritten because they are actually
       free. This would result in different check points */
    reserve_fs_reserved_clusters();
    do_op(TSTOP_CREATE_1);         /* Create a file with no Journaling */
    do_op(TSTOP_CHKPOINT_B);       /* Save a check point               */
    do_op(TSTOP_DELETE_1);         /* Delete the file                  */
    do_op(TSTOP_CMPCHKPOINT_A);    /* Verify unchanged                 */
    release_fs_reserved_clusters();
    restore_fs_context();
    do_op(TSTOP_FSENABLE);         /* Enable Journaling                */
    do_op(TSTOP_CREATE_1);         /* Create the file with Journaling  */
    do_op(TSTOP_FSFLUSH);          /* Flush the Journal                */
    do_op(TSTOP_FSSYNC);           /* Synchronize                      */
    do_op(TSTOP_CMPCHKPOINT_B);    /* Verify that results are the same */
    do_op(TSTOP_FSABORT);          /* Turn off journal                 */
    do_op(TSTOP_DELETE_1);         /* Delete the file                  */
    break;

    case TEST_ABORTED_RAM_RESTORE:/* Journal,flush,abort-sync, restore */
    do_op(TSTOP_FSENABLE);
    save_fs_context();
    do_op(TSTOP_FSABORT);          /* Turn off journal                 */
    do_op(TSTOP_CHKPOINT_A);     /* Save a check point                 */
    /* Reserve the clusters that are used by the journal. If we do not
       reserve them the journal file sectors will probably be allocated and overwritten because they are actually
       free. This would result in different check points */
    reserve_fs_reserved_clusters();
    do_op(TSTOP_CREATE_1);       /* Create a file with no Journaling   */
    do_op(TSTOP_CHKPOINT_B);     /* Save a check point                 */
    do_op(TSTOP_DELETE_1);       /* Delete the file                    */
    do_op(TSTOP_CMPCHKPOINT_A);  /* Verify unchanged                   */
    release_fs_reserved_clusters();
    restore_fs_context();
    do_op(TSTOP_FSENABLE);       /* Enable Journaling                  */
    do_op(TSTOP_CREATE_1);       /* Create the file with Journaling    */
    do_op(TSTOP_FSFLUSH);        /* Flush the Journal                  */
#if (INCLUDE_ASYNCRONOUS_API)
    /* In syncronous mode we cant do a controlleed partial syncronize
       (we can force an error in TEST mode, but we are not for now      */
    do_op(TSTOP_FSSYNC_PARTIAL); /* Abort Partial Synchronize          */
#endif
    do_op(TSTOP_FSABORT);        /* Turn off journal                 */
    do_op(TSTOP_NOTCHKPOINT_B);  /* Verify Volume has changed          */

    do_op(TSTOP_RESTORE);        /* Restore the Volume                 */
    do_op(TSTOP_CMPCHKPOINT_B);  /* Verify the results are the same    */
    do_op(TSTOP_DELETE_1);
    break;
    case TEST_SIMULTANEOUS_RESTORE:/*Journal,flush,journal,sync,flush,sync*/
    do_op(TSTOP_FSENABLE);
    save_fs_context();
    do_op(TSTOP_FSABORT);          /* Turn off journal                 */
    do_op(TSTOP_CHKPOINT_A);
    /* Reserve the clusters that are used by the journal. If we do not
       reserve them the journal file sectors will probably be allocated and overwritten because they are actually
       free. This would result in different check points */
    reserve_fs_reserved_clusters();
    do_op(TSTOP_CREATE_1);      /* Create file 1 with no Journaling    */
    do_op(TSTOP_CHKPOINT_B);    /* Save a check point                  */
    do_op(TSTOP_CREATE_2);      /* Create file 2 with no Journaling    */
    do_op(TSTOP_CREATE_3);      /* Create file 3 with no Journaling    */
    do_op(TSTOP_CHKPOINT_C);    /* Save a check point                  */
    do_op(TSTOP_DELETE_1);      /* Delete file1                        */
    do_op(TSTOP_DELETE_2);      /* Delete file2                        */
    do_op(TSTOP_DELETE_3);      /* Delete file3                        */
    do_op(TSTOP_CMPCHKPOINT_A); /* Verify back to A                    */
    release_fs_reserved_clusters();
    restore_fs_context();
    do_op(TSTOP_FSENABLE);      /* Enable Journaling                   */
    do_op(TSTOP_CREATE_1);      /* Create file 1 with Journaling       */
    do_op(TSTOP_FSFLUSH);       /* Flush the Journal                   */
#if (INCLUDE_ASYNCRONOUS_API)
    do_op(TSTOP_FSSYNC_START);  /* Start Sync File 1 to volume         */
    do_op(TSTOP_CREATE_2);      /* Create file 2 with Journaling       */
    do_op(TSTOP_FSSYNC_DONE);   /* Finish sync File 1 to volume        */
#else
    do_op(TSTOP_FSSYNC);        /* Sync file 1 on volume)  */
    do_op(TSTOP_CREATE_2);      /* Create file 2 with Journaling       */
#endif
    do_op(TSTOP_CMPCHKPOINT_B); /* Verify file 1 on volume             */
    do_op(TSTOP_CREATE_3);      /* Create file 3 with Journaling       */
    do_op(TSTOP_FSFLUSH);       /* Flush the Journal                   */
    do_op(TSTOP_FSSYNC);        /* Sync the Journal (2 & 3 on volume)  */
    do_op(TSTOP_CMPCHKPOINT_C); /* Verify file 1, 2 & 3 on volume      */
    do_op(TSTOP_FSABORT);
    do_op(TSTOP_DELETE_1);      /*  delete the files                   */
    do_op(TSTOP_DELETE_2);
    do_op(TSTOP_DELETE_3);
    break;
    }

    fs_api_disable(test_driveid, TRUE);

}

PRO_TEST_CHECKPOINT check_point_a,check_point_b,check_point_c,check_point_temp;
byte *fstest_file_names[] = {(byte *) "NOTUSED",(byte *) "FSTEST_1",(byte *) "FSTEST_2",(byte *) "FSTEST_3"};

static void fstest_jflush(void);
static void fstest_jrestore(void);
#if (INCLUDE_ASYNCRONOUS_API)
static void fstest_jsync_start(void);
static void fstest_jsync_complete(void);
static void fstest_jsync_partial(void);
#endif
static void fstest_delete_file(byte *file_name);

static void do_op(int test_op)
{
    switch (test_op)
    {
    case TSTOP_CMPCHKPOINT_A:
    case TSTOP_CMPCHKPOINT_B:
    case TSTOP_NOTCHKPOINT_B:
    case TSTOP_CMPCHKPOINT_C:
        pro_test_mark_checkpoint(test_drivenumber, &check_point_temp);
    break;
    }

    switch (test_op)
    {
    case TSTOP_CHKPOINT_A:
        pro_test_mark_checkpoint(test_drivenumber, &check_point_a);
    break;
    case TSTOP_CHKPOINT_B:
        pro_test_mark_checkpoint(test_drivenumber, &check_point_b);
    break;
    case TSTOP_CHKPOINT_C:
        pro_test_mark_checkpoint(test_drivenumber, &check_point_c);
    break;
    case TSTOP_CMPCHKPOINT_A:
        if (!pro_test_compare_checkpoints(&check_point_temp, &check_point_a))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    break;
    case TSTOP_CMPCHKPOINT_B:
        if (!pro_test_compare_checkpoints(&check_point_temp, &check_point_b))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    break;
    case TSTOP_NOTCHKPOINT_B:
        if (pro_test_compare_checkpoints(&check_point_temp, &check_point_b))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    break;
    case TSTOP_CMPCHKPOINT_C:
        if (!pro_test_compare_checkpoints(&check_point_temp, &check_point_c))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    break;
    case TSTOP_CREATE_1:
        fstest_create_file_journaled(fstest_file_names[1], 3);
    break;
    case TSTOP_CREATE_2:
        fstest_create_file_journaled(fstest_file_names[2], 3);
    break;
    case TSTOP_CREATE_3:
        fstest_create_file_journaled(fstest_file_names[3], 3);
    break;
    case TSTOP_DELETE_1:
        fstest_delete_file(fstest_file_names[1]);
    break;
    case TSTOP_DELETE_2:
        fstest_delete_file(fstest_file_names[2]);
    break;
    case TSTOP_DELETE_3:
        fstest_delete_file(fstest_file_names[3]);
    break;
    case TSTOP_FSENABLE:
        fs_api_disable(test_driveid, TRUE);
        if (!fs_api_enable(test_driveid, TRUE))
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        /* Get drive information, this should force a mount if needed */
        if (!pc_diskio_info(test_driveid, &test_drive_infostruct, FALSE))
        {
        	ERTFS_ASSERT_TEST(rtfs_debug_zero())
        	return;
        }
    break;
    case TSTOP_FSABORT:
        fs_api_disable(test_driveid, TRUE);
    break;
   case TSTOP_FORCE_JCREATE:    /* Enable and then abort so we know we have a journal */
   { dword ltemp;
        fs_api_disable(test_driveid, TRUE);
        if (!fs_api_enable(test_driveid, TRUE))
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        /* Force a mount and then abort */
        pc_blocks_free(test_driveid, &ltemp, &ltemp);
        /* Save off reserved fragments here */
        save_fs_context();
        fs_api_disable(test_driveid, TRUE);
   }
   break;
    case TSTOP_FSFLUSH:
        fstest_jflush();
    break;
    case TSTOP_RESTORE:
        fstest_jrestore();
    break;
#if (INCLUDE_ASYNCRONOUS_API)
    case TSTOP_FSSYNC_START:
        fstest_jsync_start();
    break;
    case TSTOP_FSSYNC_DONE:
        fstest_jsync_complete();
    break;
    case TSTOP_FSSYNC:
        fstest_jsync_start();
        fstest_jsync_complete();
    break;
    case TSTOP_FSSYNC_PARTIAL:
        fstest_jsync_partial();
    break;
#else
    case TSTOP_FSSYNC:
    {
        BOOLEAN ret_val;
        ret_val = fs_api_commit(test_driveid,TRUE);
        ERTFS_ASSERT_TEST(ret_val == TRUE)
    }
    break;
#endif
    }
}
static void fstest_jflush(void)
{
#if (INCLUDE_ASYNCRONOUS_API)
int ret_val;
    /* Step until we exit JOURNALFLUSH state */
    ret_val = pc_async_continue(test_drivenumber, DRV_ASYNC_DONE_JOURNALFLUSH, 0);
    ERTFS_ASSERT_TEST(ret_val == PC_ASYNC_COMPLETE)
#else
    BOOLEAN ret_val;
    ret_val = fs_api_commit(test_driveid,FALSE);
    ERTFS_ASSERT_TEST(ret_val == TRUE)
#endif
}

static void fstest_jrestore(void)
{
    if (!fs_api_restore(test_driveid))
    {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
}
#define RS_WRITE_REPLACEMENTS 8 /* Cheating.. this is private in prfsrestore.h  */

#if (INCLUDE_ASYNCRONOUS_API)

static void fstest_jsync_start(void)
{
int ret_val;
FAILSAFECONTEXT *pfstest_failsafe_context;
    fstest_jflush();    /* Make sure we're flushed */
    /* get to the write volume state */
    pfstest_failsafe_context = fstest_failsafe_context();
    while (pfstest_failsafe_context->fs_restore.restore_state != RS_WRITE_REPLACEMENTS)
    {
        ret_val = pc_async_continue(test_drivenumber, DRV_ASYNC_DONE_RESTORE, 1);
        ERTFS_ASSERT_TEST(ret_val == PC_ASYNC_CONTINUE)
    }
}
static void fstest_jsync_complete(void)
{
int ret_val;
    /* Step until we exit JOURNAL RESTORE state */
    ret_val = pc_async_continue(test_drivenumber, DRV_ASYNC_DONE_RESTORE, 0);
    ERTFS_ASSERT_TEST(ret_val == PC_ASYNC_COMPLETE)
}

static void fstest_jsync_partial(void)
{
int ret_val;
    /* get to the write volume state */
    fstest_jsync_start();
    /* 2 more passes should leave us still restoring but incomplete */
    ret_val = pc_async_continue(test_drivenumber, DRV_ASYNC_DONE_RESTORE, 1);
    ERTFS_ASSERT_TEST(ret_val == PC_ASYNC_CONTINUE)
	// Do only one.. Under exFAt two passes complete everything
    //ret_val = pc_async_continue(test_drivenumber, DRV_ASYNC_DONE_RESTORE, 1);
    //ERTFS_ASSERT_TEST(ret_val == PC_ASYNC_CONTINUE)
}
#endif

static void fstest_delete_file(byte *file_name)
{
#if (INCLUDE_ASYNCRONOUS_API)
int ret_val;
int fd;
    fd = pc_efilio_async_unlink_start(file_name);
    ERTFS_ASSERT_TEST(fd >= 0)
    /* Step until we exit FILE state */
    ret_val = pc_async_continue(test_drivenumber, DRV_ASYNC_DONE_FILES, 0);
    ERTFS_ASSERT_TEST(ret_val == PC_ASYNC_COMPLETE)
#else
BOOLEAN ret_val;
    ret_val = pc_unlink(file_name);
    ERTFS_ASSERT_TEST(ret_val == TRUE)
#endif
}


static void fstest_simple_drive_config(
    dword drive_operating_policy,
    dword blockmap_size,
    dword index_buffer_size,
    dword restore_buffer_size,
    dword journal_size)
{
dword ltemp;
   	fs_api_disable(test_driveid, TRUE);

    fstest_drive_config(
    drive_operating_policy,
    blockmap_size,
    index_buffer_size,
    restore_buffer_size,
    journal_size,
    DEFAULT_USER_BUFFER_SIZE_BLOCKS,
    DEFAULT_NUM_FAT_BUFFERS);
	if (!fs_api_disable(test_driveid, TRUE))
    { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        /* Force a mount */
     pc_blocks_free(test_driveid, &ltemp, &ltemp);
     if (!fs_api_disable(test_driveid, TRUE))
     { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
}


static void fstest_drive_config(
    dword drive_operating_policy,
    dword blockmap_size,
    dword index_buffer_size,
    dword restore_buffer_size,
    dword journal_size,
    dword user_buffer_size_sectors,
    dword num_fat_buffers)
{
	DDRIVE *ptest_drive;
	struct rtfs_volume_resource_reply volume_config;
   	ptest_drive = test_drive_structure();

    rtfs_memset(&volume_config, 0, sizeof(volume_config));
    /* Global vraible. If zero uses defaults */
    fs_test_journal_size = journal_size;

	pro_test_set_mount_parameters(ptest_drive, &volume_config,
		drive_operating_policy,
		test_drive_infostruct.sector_size,
		user_buffer_size_sectors,
		32, /* dword n_sector_buffers, */
		num_fat_buffers,
		DEFAULT_FAT_PAGESIZE,
		blockmap_size,
		index_buffer_size,
		restore_buffer_size);
}

static BOOLEAN close_file_for_fstest(int fd)
{
    if (po_close(fd) == 0)
		return(TRUE);
	else
		return(FALSE);
}
static int create_file_for_fstest(byte *filename)
{
int fd;
    fd = po_open(filename,(word)(PO_BINARY|PO_RDWR|PO_EXCL|PO_CREAT),(word)(PS_IWRITE | PS_IREAD));
    return(fd);
}
static BOOLEAN write_for_fstest(int fd,dword ntowrite)
{
	if (po_write(fd, (byte*)0, (int)ntowrite) != (int)ntowrite)
		return(FALSE);
	return(TRUE);
}
static void fstest_create_file_journaled(byte *file_name, dword n_frames)
{
int fd;
dword total_clusters;
FAILSAFECONTEXT *pfstest_failsafe_context;

    /* Now create a 32 bit file, make it large enough to consume n_frames  */
    fd = create_file_for_fstest(file_name);
    ERTFS_ASSERT_TEST(fd >= 0)

    /* Calculate total number of clusters needed to fill a few frames */
    pfstest_failsafe_context = fstest_failsafe_context();
    total_clusters = n_frames*pfstest_failsafe_context->fs_frame_max_records;
    while (total_clusters--)
    {
    dword ntowrite;
        ntowrite = (dword) test_drive_structure()->drive_info.bytespcluster;
        if (!write_for_fstest(fd,ntowrite))
       { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    }
    if (!close_file_for_fstest(fd)) /* Should succeed */
    {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
}

static void save_fs_context(void)
{
FAILSAFECONTEXT *pfstest_failsafe_context;
    pfstest_failsafe_context = fstest_failsafe_context();
    copy_of_context = *pfstest_failsafe_context;
    pcopy_of_context = &copy_of_context;
    copy_of_nv_reserved_fragment = pfstest_failsafe_context->nv_reserved_fragment;
}
static void restore_fs_context(void)
{
    pcopy_of_context = 0;
}
static void reserve_fs_reserved_clusters(void)
{
   free_manager_claim_clusters(test_drive_structure(), copy_of_nv_reserved_fragment.start_location,
                          copy_of_nv_reserved_fragment.end_location - copy_of_nv_reserved_fragment.start_location + 1);
}
static void release_fs_reserved_clusters(void)
{
       free_manager_release_clusters(test_drive_structure(), copy_of_nv_reserved_fragment.start_location,
                            copy_of_nv_reserved_fragment.end_location - copy_of_nv_reserved_fragment.start_location + 1, FALSE);
}

static dword saved_nv_buffer_handle, saved_journal_file_size;
static void save_fs_file_handle(void)
{
FAILSAFECONTEXT *pfstest_failsafe_context;
	pfstest_failsafe_context = fstest_failsafe_context();
    saved_nv_buffer_handle  = pfstest_failsafe_context->nv_buffer_handle;
    saved_journal_file_size = pfstest_failsafe_context->journal_file_size;
}
static void restore_fs_file_handle(void)
{
FAILSAFECONTEXT *pfstest_failsafe_context;
		pfstest_failsafe_context = fstest_failsafe_context();
        /* Restore journal file handle so we can write. It was cleared
           by fs_api_info, because it is not valid */
        pfstest_failsafe_context->nv_buffer_handle = saved_nv_buffer_handle;
        pfstest_failsafe_context->journal_file_size = saved_journal_file_size;
}

#endif /* (!INCLUDE_DEBUG_TEST_CODE && FAILSAFE) else.. */
#endif /* Exclude from build if read only */
