/* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/*
<TEST>  Test File:   rtfspackages/apps/preftest.c
<TEST>
<TEST>   Procedure: pc_efilio_test(void)
<TEST>   Description: RTFS Pro Plus Extended file regression test suite.
<TEST>   Test suite performs tests to verify the correct operation of features provided with RTFS Pro Plus.
<TEST>
*/

#include "rtfs.h"
#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */
#if (!INCLUDE_DEBUG_TEST_CODE)
void pc_efilio_test(byte *pdriveid)
{
    RTFS_ARGSUSED_PVOID((void *)pdriveid);
    RTFS_PRINT_STRING_1((byte *)"Build with INCLUDE_DEBUG_TEST_CODE to run tests", PRFLG_NL);
}
#else
#include "protests.h"


/* Individual tests */
static BOOLEAN test_region_alloc_recover(void);
static BOOLEAN test_efile_placement(void);
static BOOLEAN test_efile_as_needed(void);
static BOOLEAN test_efile_force_first(void);
static BOOLEAN test_efile_first_fit(void);
static BOOLEAN test_efile_force_contiguous(void);
static BOOLEAN test_efile_force_prealloc(void);
static BOOLEAN test_efile_open_remap(void);
static BOOLEAN test_efile_open_fat64(void);
static BOOLEAN test_efile_chsize(void);

static void test_efile_open_options(void);
static void test_efile_extent_tracking(void);
static void test_efile_data_tracking(void);
static void test_efile_drive_options(void);
static void test_efile_api_usage(void);

static void test_alloc_fragment(dword start_contig, dword n_contig);
static void test_free_fragment(dword start_contig, dword n_contig);
static void _test_efile_data_basic(byte *filename,dword options,dword test_file_size_dw,BOOLEAN do_io);
void test_set_default_configuration(void);
static int test_efile_reopen_file(byte *filename, dword options, dword min_clusters_per_allocation);

#define RANDOM_SEEK_TEST_PASSES 10
/* #define RANDOM_SEEK_TEST_PASSES 1000 */


static BOOLEAN append_n_clusters(int fd, int clusters, dword expected_start,dword expected_end);
static BOOLEAN seek_n_dwords(int fd, dword _offset_in_dwords,dword test_file_size_dw);

/* Stand alone efilio test called fron efishell */
/*
<TEST>  Procedure: pc_efilio_test - Extended IO regression test entry point
<TEST>
<TEST>   This routine calls subroutines that test RTFS Pro Plus subsytems
<TEST>   for proper operations.
<TEST>
<TEST>   The following subrotuines execute to test subsystems:
<TEST>
<TEST>      test_efile_open_options - Test extended file open options.
<TEST>      test_efile_data_tracking- Test extended file data operations.
<TEST>      test_asynchronous_api   - Test asynchronous operations
<TEST>                                API.
<TEST>
*/


void pc_efilio_test(byte *pdriveid)
{
dword operating_flags;
#if (INCLUDE_FAILSAFE_CODE)
void *pfailsafe;
#endif
    if (!set_test_drive(pdriveid))
        return;
    operating_flags = pro_test_operating_policy();
	if (operating_flags != 0)
	{
    	PRO_TEST_ANNOUNCE("pc_efilio_test can not run, default operating flags required");
		return;
	}

#if (INCLUDE_FAILSAFE_CODE)
	pfailsafe = (void *) test_drive_structure()->drive_state.failsafe_context;	/* Will be zero if disabled already */
	if (pfailsafe)
	{
   		if (!fs_api_disable(pdriveid, FALSE))  /* Disables Failsafe if it was previously enabled */
   		{
   			ERTFS_ASSERT_TEST(rtfs_debug_zero())
			return;
		}
	}
#else
	pc_diskclose(pdriveid, FALSE);
#endif
    data_buffer = 0;
    pro_test_alloc_data_buffer();

    pc_set_default_drive(test_driveid);
    pc_set_cwd((byte *)"\\");

    test_efile_open_options();
    test_region_alloc_recover();
    test_efile_placement();
    test_efile_as_needed();
    test_efile_chsize();
    test_efile_open_options();
    test_efile_extent_tracking();
    test_efile_data_tracking();
    test_efile_api_usage();     /* Not done */
    test_efile_drive_options(); /* Not done */
	pc_diskclose(pdriveid, FALSE);
#if (INCLUDE_FAILSAFE_CODE)
	if (pfailsafe)
	{
   		if (!fs_api_enable(pdriveid, TRUE))
   		{
   			ERTFS_ASSERT_TEST(rtfs_debug_zero())
			return;
		}
	}
#endif
    pro_test_free_data_buffer();
}

/*
*
* 1. Fill a file
* 2. Reopen with as needed
* 3. Read to end
* 4. Close and Reopen with as needed
* 5. Seek half,quarter full and read
* 6. Close and Reopen with as needed
* 7. Do some test writing
*
*/


int pro_test_efile_frag_fill(byte *filename,dword options, dword fragment_size_dw, dword test_file_size_dw,BOOLEAN do_io, BOOLEAN do_close);

static BOOLEAN _test_efile_as_needed(byte *filename,dword test_file_size_dw)
{
int fd;
dword value, ntransfered_dw, fragment_size_dw, offset_in_dwords, options;

    /* Create and fill the initial file with a pattern, make it fragmented so we can test the as needed feature
       on fragmented files */
    options = 0;

    fragment_size_dw = test_file_size_dw / 256;
    fd = pro_test_efile_frag_fill(filename, options, fragment_size_dw, test_file_size_dw, TRUE, TRUE);
    ERTFS_ASSERT_TEST(fd >= 0)
    fd = test_efile_reopen_file(filename,options|PCE_LOAD_AS_NEEDED,0);
    ERTFS_ASSERT_TEST(fd>=0)
    /* Read it back */
    pro_test_read_n_dwords(fd, 0, test_file_size_dw, &ntransfered_dw, TRUE);
    ERTFS_ASSERT_TEST(test_file_size_dw == ntransfered_dw)
    pc_efilio_close(fd);

    /* Now seek */
    fd = test_efile_reopen_file(filename,options|PCE_LOAD_AS_NEEDED,0);
    ERTFS_ASSERT_TEST(fd>=0)

    offset_in_dwords = fragment_size_dw;
    while (offset_in_dwords < test_file_size_dw)
    {
        if (!seek_n_dwords(fd, offset_in_dwords, test_file_size_dw))
        {
            ERTFS_ASSERT_TEST(fd>=0)
        }
        pro_test_read_n_dwords(fd, offset_in_dwords, 1, &ntransfered_dw, TRUE);
        offset_in_dwords += fragment_size_dw;
    }
    pc_efilio_close(fd);

    /* Now overwrite the file starting with a value of 1000 */
    value = 1000;
    fd = test_efile_reopen_file(filename,options|PCE_LOAD_AS_NEEDED,0);
    ERTFS_ASSERT_TEST(fd>=0)
    pro_test_write_n_dwords(fd, value, test_file_size_dw-100, &ntransfered_dw, TRUE, data_buffer);
    value += ntransfered_dw;
    pro_test_write_n_dwords(fd, value, 10000, &ntransfered_dw, TRUE, data_buffer);
    pc_efilio_close(fd);

    /* Read it back */
    fd = test_efile_reopen_file(filename,options,0);
    ERTFS_ASSERT_TEST(fd>=0)
    pro_test_read_n_dwords(fd, 1000, test_file_size_dw+9900, &ntransfered_dw, TRUE);
    pc_efilio_close(fd);


    return(TRUE);
}

/*
<TEST>    Test correct operation of read write and seek and flush operatins on files opened using PCE_LOAD_AS_NEEDED option
*/
static BOOLEAN test_efile_as_needed(void)
{
    PRO_TEST_ANNOUNCE("Testing PCE_LOAD_AS_NEEDED open option 32 bit file");
    _test_efile_as_needed((byte *) TEST_FILE32, FILESIZE_32);
    return(TRUE);
}


static FREELISTINFO * get_free_clusters(int *plist_size)
{
FREELISTINFO *plist;
FREELISTINFO info_element;
int n_fragments;

    /* How many free fragments are there on the drive ? */
    n_fragments = pc_diskio_free_list((byte *)test_driveid, 1, &info_element, 0, 0, 1);
    ERTFS_ASSERT_TEST(n_fragments > 0)

    /* Now allocate a list and get all free fragmentsw */
    plist = (FREELISTINFO *) pro_test_malloc(sizeof(*plist)*n_fragments);
    /* Now get the fragments */
    n_fragments = pc_diskio_free_list((byte *)test_driveid, n_fragments, plist, 0, 0, 1);
    ERTFS_ASSERT_TEST(n_fragments > 0)

    *plist_size = n_fragments;

    return (plist);
}


static REGION_FRAGMENT *monopolize_region_structures(dword n_to_alloc)
{
REGION_FRAGMENT *pf;
    pf = 0;
    while(n_to_alloc--)
    {
    REGION_FRAGMENT *newpf;
        newpf = pc_fraglist_frag_alloc(0, 0,0,pf);
        if (newpf)
            pf = newpf;
        else
            break;
    }
    return(pf);
}

/* <TEST>    Test recovery modes when ProPlus runs out of regions structures during mount */
static BOOLEAN test_region_alloc_recover(void)
{
#if (INCLUDE_RTFS_FREEMANAGER)
REGION_FRAGMENT *pfmonopolize;
DRIVE_INFO drive_info_stats,drive_info_stats_2;
ERTFS_EFILIO_STAT file_stats;
dword nwritten;
int fd, fd2;
    PRO_TEST_ANNOUNCE("Test recover from out of region structures");

    /* Close the drive, remount it and determine how many region structures are free */
    pc_diskclose((byte *)test_driveid, FALSE);
    if (!pc_diskio_info((byte *)test_driveid, &drive_info_stats, FALSE))
    {   ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    pc_diskclose((byte *)test_driveid, FALSE);
    if (!drive_info_stats.free_manager_enabled) /* Can't run if not enable by default */
        return(TRUE);
/* <TEST>    Test recovery modes when ProPlus runs out of regions structures during mount */
    PRO_TEST_ANNOUNCE("Testing recover from out of region structures during mount");
    /* Now monopolize 1 more region struture than we needed to mount. */
    pfmonopolize = monopolize_region_structures(drive_info_stats.region_buffers_free+1);
    /* Remount the drive and get new stats */
    if (!pc_diskio_info((byte *)test_driveid, &drive_info_stats_2, FALSE))
    {   ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    /* The free manager should be closed because we ran out of strutcures during the mount scan */
    if (drive_info_stats_2.free_manager_enabled)
    {   ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    /* The count of free clusters and fragments should be the same */
    ERTFS_ASSERT_TEST(drive_info_stats_2.free_clusters == drive_info_stats.free_clusters)
    pc_diskclose((byte *)test_driveid, FALSE);
    pc_fraglist_free_list(pfmonopolize);

/* <TEST>    Test recovery modes when ProPlus runs out of regions structures with a mounted volume */
    PRO_TEST_ANNOUNCE("Testing recover from out of region structures after mount");
    pc_unlink(TEST_FILE32); /* Make sure there is no file */
    pc_unlink((byte *) "FRAG1"); /* Make sure there is no file */
    pro_test_efile_fill((byte *)"FRAG1",0, 512, FALSE, TRUE); /* Create a file with some data */
    pc_diskclose((byte *)test_driveid, FALSE);
    /* Create and write one byte to a file ask to pre-allocated 8 clusters */
    fd = pro_test_efile_create(TEST_FILE32, 0, 8);
    if (!pc_efilio_write(fd, 0, 1, &nwritten))
    {   ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    if (!pc_efilio_fstat(fd, &file_stats))
    {   ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    /* Should have preallocated clusters */
    ERTFS_ASSERT_TEST(file_stats.allocated_clusters !=  file_stats.preallocated_clusters)
    /* Get and monopolized all fragments */
    if (!pc_diskio_info((byte *)test_driveid, &drive_info_stats, FALSE))
    {   ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    /* Now exhaust the free pool */
    pfmonopolize = monopolize_region_structures(drive_info_stats.region_buffers_free);
    fd2 = pro_test_efile_create((byte *) "FRAG1", 0, 8); /* Now open a file, should force free manager revert */
    {   ERTFS_ASSERT_TEST(fd2 >= 0) }
    pc_efilio_close(fd2);
    pc_unlink((byte *) "FRAG1"); /* And delete it */

    if (!pc_diskio_info((byte *)test_driveid, &drive_info_stats, FALSE))
    {   ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    /* The manager should be down now */
    if (drive_info_stats.free_manager_enabled)
    {   ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    if (!pc_efilio_fstat(fd, &file_stats))
    {   ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    /* There should be no preallocated clusters */
    ERTFS_ASSERT_TEST(file_stats.allocated_clusters==file_stats.preallocated_clusters)
    pc_efilio_close(fd);
    pc_diskclose((byte *)test_driveid, FALSE);
    pc_fraglist_free_list(pfmonopolize);
#endif
    return(TRUE);
}

static BOOLEAN _test_efile_placement(byte *filename)
{
int fd,i,freelist_size, file_list_size, return_file_list_size;
dword ntransfered_dw, options;
FREELISTINFO *plist;
FILESEGINFO  *pfile_list,*pfile_cluster_list,*expected_pfile_list,*expected_fill_list;
dword cluster_size_bytes,sector_size_bytes;

    /* Create and fill the initial file with a pattern, make it fragmented so we can test the as needed feature
       on fragmented files */
    options = 0;

    fd = pro_test_efile_create(filename,options,0);
    ERTFS_ASSERT_TEST(fd>=0)

    plist = get_free_clusters(&freelist_size);
    cluster_size_bytes = (dword) pc_cluster_size((byte *)test_driveid);
    sector_size_bytes = (dword) pc_sector_size((byte *)test_driveid);
    ERTFS_ASSERT_TEST(sector_size_bytes != 0)
    file_list_size = freelist_size*2;
    pfile_list = (FILESEGINFO *) pro_test_malloc(sizeof(*pfile_list)*file_list_size);
    pfile_cluster_list = (FILESEGINFO *) pro_test_malloc(sizeof(*pfile_list)*file_list_size);
    expected_fill_list = expected_pfile_list = (FILESEGINFO *) pro_test_malloc(sizeof(*pfile_list)*file_list_size);
    ERTFS_ASSERT_TEST(pfile_list)

    /* Now write to the last and first third cluster of each each free fragment */
    for (i = 0; i < freelist_size; i++)
    {
        dword cluster;
        cluster = (plist+i)->cluster;
        if (!pc_efilio_setalloc(fd, cluster,0)) {   ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        expected_fill_list->block = pc_cl2sector(test_drive_structure(),cluster);
        expected_fill_list++;
        pro_test_write_n_dwords(fd, cluster, cluster_size_bytes/4, &ntransfered_dw, TRUE, data_buffer);
        if ((plist+i)->nclusters > 2)
        {
            cluster = (plist+i)->cluster + 2;
            expected_fill_list->block = pc_cl2sector(test_drive_structure(),cluster);
            expected_fill_list++;
            if (!pc_efilio_setalloc(fd, cluster,0)) {   ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
            pro_test_write_n_dwords(fd, cluster, cluster_size_bytes/4, &ntransfered_dw, TRUE, data_buffer);
        }
    }
    /* Now get file extents and make sure they are laid out as expected
       close and re-open the file just for kicks */
    pc_efilio_close(fd);
    fd = test_efile_reopen_file(filename,options,0);

    return_file_list_size = pc_get_file_extents(fd, file_list_size, pfile_list, FALSE, FALSE);
    for (i = 0; i < return_file_list_size; i++)
    {
        if ((expected_pfile_list+i)->block  != (pfile_list+i)->block)
        {
            ERTFS_ASSERT_TEST(rtfs_debug_zero())
        }
    }
    /* Now test raw read and write */
    PRO_TEST_ANNOUNCE("Testing pc_raw_read() and pc_raw_write()");
    pc_get_file_extents(fd, file_list_size, pfile_cluster_list, TRUE, FALSE);

    pc_efilio_close(fd);

    for (i = 0; i < return_file_list_size; i++)
    {
        dword cluster_no, block_no;
        cluster_no = (pfile_cluster_list+i)->block;
        block_no   = (pfile_list+i)->block;
        if (!pc_raw_read(test_drivenumber,  data_buffer, block_no, cluster_size_bytes/sector_size_bytes, FALSE))
        {
            ERTFS_ASSERT_TEST(rtfs_debug_zero())
        }
        if (pro_test_check_buffer_dwords((dword *)data_buffer, cluster_no, cluster_size_bytes/4) != cluster_size_bytes/4)
            ERTFS_ASSERT_TEST(rtfs_debug_zero())

        /* Now write block_no .. as a pattern and read it back to be sure raw_write is okay */
        pro_test_fill_buffer_dwords((dword *)data_buffer, block_no, cluster_size_bytes/4);
        if (!pc_raw_write(test_drivenumber,  data_buffer, block_no, cluster_size_bytes/sector_size_bytes, FALSE))
        {
            ERTFS_ASSERT_TEST(rtfs_debug_zero())
        }
        if (!pc_raw_read(test_drivenumber,  data_buffer, block_no, cluster_size_bytes/sector_size_bytes, FALSE))
        {
            ERTFS_ASSERT_TEST(rtfs_debug_zero())
        }
        if (pro_test_check_buffer_dwords((dword *)data_buffer, block_no, cluster_size_bytes/4) != cluster_size_bytes/4)
            ERTFS_ASSERT_TEST(rtfs_debug_zero())
    }

    /* Now restore original pattern and read it back */
    for (i = 0; i < return_file_list_size; i++)
    {
        dword cluster_no, block_no;
        cluster_no = (pfile_cluster_list+i)->block;
        block_no   = (pfile_list+i)->block;

        pro_test_fill_buffer_dwords((dword *)data_buffer, cluster_no, cluster_size_bytes/4);
        if (!pc_raw_write(test_drivenumber,  data_buffer, block_no, cluster_size_bytes/sector_size_bytes, FALSE))
        {
            ERTFS_ASSERT_TEST(rtfs_debug_zero())
        }
        if (!pc_raw_read(test_drivenumber,  data_buffer, block_no, cluster_size_bytes/sector_size_bytes, FALSE))
        {
            ERTFS_ASSERT_TEST(rtfs_debug_zero())
        }
        if (pro_test_check_buffer_dwords((dword *)data_buffer, cluster_no, cluster_size_bytes/4) != cluster_size_bytes/4)
            ERTFS_ASSERT_TEST(rtfs_debug_zero())
    }
    pro_test_free(plist);
    pro_test_free(expected_pfile_list);
    pro_test_free(pfile_list);
    pro_test_free(pfile_cluster_list);
    return(TRUE);
}

/* <TEST>    Test assigning specific clusters to files using pc_efilio_setalloc and pc_diskio_free_list*/
/* <TEST>    Verify placement using pc_get_file_extents() */
static BOOLEAN test_efile_placement(void)
{
    PRO_TEST_ANNOUNCE("Testing pc_get_file_extents(),pc_efilio_setalloc(),pc_diskio_free_list()");
    PRO_TEST_ANNOUNCE("Testing 32 bit file");
    _test_efile_placement((byte *) TEST_FILE32);
    return(TRUE);
}

static void test_efile_open_options(void)
{
    test_efile_force_prealloc();
    test_efile_force_first();
    test_efile_force_prealloc();
    test_efile_first_fit();
    test_efile_force_prealloc();
    test_efile_force_contiguous();
    test_efile_force_prealloc();
    test_efile_open_remap();

    test_efile_open_fat64();
}


static BOOLEAN _test_efile_force_first(byte *filename, dword options);
static BOOLEAN test_efile_force_first(void)
{
    PRO_TEST_ANNOUNCE("Testing PCE_FORCE_FIRST open option 32 bit file");
    _test_efile_force_first(TEST_FILE32, 0);
    return(TRUE);
}
/*<TEST> Test option PCE_FORCE_FIRST for 32 and 64 bit files*/

static BOOLEAN _test_efile_force_first(byte *filename, dword options)
{
    int fd;
    dword region_start_cluster;
    dword pad_clusters;
    dword hole_cluster , first_free_in_region, contig_found;
    int is_error;

    region_start_cluster = 2;

    fd = pro_test_efile_create(filename,options|PCE_FORCE_FIRST,0);

    pad_clusters = 128; /* some random value */

    /* Write pad cluster to the file, should allocate 1 cluster from first free */
    first_free_in_region = fatop_find_contiguous_free_clusters(test_drive_structure(), region_start_cluster, test_drive_structure()->drive_info.maxfindex, 1, 1, &contig_found, &is_error, ALLOC_CLUSTERS_PACKED);
    ERTFS_ASSERT_TEST(first_free_in_region)
    append_n_clusters(fd, pad_clusters,first_free_in_region,0);

    /* allocate 1 cluster fragment in the middle of the region */
    first_free_in_region = fatop_find_contiguous_free_clusters(test_drive_structure(), region_start_cluster, test_drive_structure()->drive_info.maxfindex, 1, 1, &contig_found, &is_error, ALLOC_CLUSTERS_PACKED);
    ERTFS_ASSERT_TEST(first_free_in_region)
    test_alloc_fragment(first_free_in_region, 1);
    hole_cluster = first_free_in_region;

    /* Write one cluster to the file, should allocate 1 cluster from first free */
    first_free_in_region = fatop_find_contiguous_free_clusters(test_drive_structure(), region_start_cluster, test_drive_structure()->drive_info.maxfindex, 1, 1, &contig_found, &is_error, ALLOC_CLUSTERS_PACKED);
    ERTFS_ASSERT_TEST(first_free_in_region)
    append_n_clusters(fd, 1,first_free_in_region,0);

    /* Get the next free before the hole is freed */
    first_free_in_region = fatop_find_contiguous_free_clusters(test_drive_structure(), region_start_cluster, test_drive_structure()->drive_info.maxfindex, 1, 1, &contig_found, &is_error, ALLOC_CLUSTERS_PACKED);
    /* Release cluster int the middle should not effect it.. will allocate from end of file */
    test_free_fragment(hole_cluster, 1);

    /* Write one cluster to the file, should allocate 1 cluster from first free (beyond eof, ignoring hole)*/
    append_n_clusters(fd, 1,first_free_in_region,0);

    if (!pc_efilio_close(fd))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (!pc_unlink(filename))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    return(TRUE);
}
static BOOLEAN _test_efile_first_fit(byte *filename, dword options);
static BOOLEAN test_efile_first_fit(void)
{
    PRO_TEST_ANNOUNCE("Testing PCE_FORCE_FIRST open option 32 bit file");
    _test_efile_first_fit(TEST_FILE32, 0);
    return(TRUE);
}
/*
  _test_efile_first_fit - Verify first fit, and force first option
     Test option PCE_FIRST_FIT|PCE_FORCE_FIRST, verifies that clusters
    are allocated from the begining of the FAT region, regardless of
    whether all are contiguous or not.
*/

/*<TEST> Test option PCE_FORCE_FIRST|PCE_FORCE_FIRST for 32 and 64 bit files*/

static dword _test_get_fileregion_start()
{
    return(2);
}

static BOOLEAN _test_efile_first_fit(byte *filename, dword options)
{
    int fd, is_error;
    dword region_start_cluster;
    dword pad_clusters, hole_cluster;
    dword contig_found, first_free_in_region;
    region_start_cluster = _test_get_fileregion_start();

    fd = pro_test_efile_create(filename,options|PCE_FIRST_FIT|PCE_FORCE_FIRST,0);

    pad_clusters = 128; /* some random value */

    /* Write pad cluster to the file, should allocate 1 cluster from first free */
    first_free_in_region = fatop_find_contiguous_free_clusters(test_drive_structure(), region_start_cluster, test_drive_structure()->drive_info.maxfindex, 1, 1, &contig_found, &is_error, ALLOC_CLUSTERS_PACKED);
    ERTFS_ASSERT_TEST(first_free_in_region)
    append_n_clusters(fd, pad_clusters,first_free_in_region,0);

    /* Write cluster to the file, should allocate 1 cluster from first free */
    first_free_in_region = fatop_find_contiguous_free_clusters(test_drive_structure(), region_start_cluster, test_drive_structure()->drive_info.maxfindex, 1, 1, &contig_found, &is_error, ALLOC_CLUSTERS_PACKED);
    ERTFS_ASSERT_TEST(first_free_in_region)
    append_n_clusters(fd, 1,first_free_in_region,0);

    /* allocate 1 cluster fragment in the middle of the region */
    first_free_in_region = fatop_find_contiguous_free_clusters(test_drive_structure(), region_start_cluster, test_drive_structure()->drive_info.maxfindex, 1, 1, &contig_found, &is_error, ALLOC_CLUSTERS_PACKED);
    ERTFS_ASSERT_TEST(first_free_in_region)
    test_alloc_fragment(first_free_in_region, 1);
    hole_cluster = first_free_in_region;

    /* Write one cluster to the file, should allocate 1 cluster from first free */
    first_free_in_region = fatop_find_contiguous_free_clusters(test_drive_structure(), region_start_cluster, test_drive_structure()->drive_info.maxfindex, 1, 1, &contig_found, &is_error, ALLOC_CLUSTERS_PACKED);
    ERTFS_ASSERT_TEST(first_free_in_region)
    append_n_clusters(fd, 1,first_free_in_region,0);

    /* Get the next free before the hole is freed */
    first_free_in_region = fatop_find_contiguous_free_clusters(test_drive_structure(), region_start_cluster, test_drive_structure()->drive_info.maxfindex, 1, 1, &contig_found, &is_error, ALLOC_CLUSTERS_PACKED);
    /* Release cluster int the middle should not effect it.. will allocate from end of file */
    test_free_fragment(hole_cluster, 1);

    /* Write one cluster to the file, should reclaim the hole cluster because of FORCE FIRST option */
    append_n_clusters(fd, 1,hole_cluster,0);

    if (!pc_efilio_close(fd))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (!pc_unlink(filename))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    return(TRUE);
}

static BOOLEAN _test_efile_force_contiguous(byte *filename, dword options);
/*<TEST> Test option PCE_FORCE_CONTIGUOUS for 32 and 64 bit files*/
static BOOLEAN test_efile_force_contiguous(void)
{
    PRO_TEST_ANNOUNCE("Testing PCE_FORCE_CONTIGUOUS open option 32 bit file");
    _test_efile_force_contiguous(TEST_FILE32, 0);
    return(TRUE);
}
/*
  _test_efile_force_contiguous - Verify force contiguous option
    Test option PCE_FORCE_CONTIGUOUS, verifies that clusters
    are allocated in contiguous groups skipping non-contiguous
    groups of the FAT region until a contiguous group is found.
*/
static BOOLEAN _test_efile_force_contiguous(byte *filename, dword options)
{
    int fd;
    dword region_start_cluster;
    dword pad_clusters;
    dword padded_region_start_cluster;

        region_start_cluster = _test_get_fileregion_start();

    fd = pro_test_efile_create(filename,options|PCE_FORCE_CONTIGUOUS,0);

    pad_clusters = 128; /* some random value */
    {
        dword contig_needed, contig_found;
        int is_error;
        contig_needed = pad_clusters + 8 + 2;
        contig_found = fatop_find_contiguous_free_clusters(test_drive_structure(), region_start_cluster, test_drive_structure()->drive_info.maxfindex, contig_needed , contig_needed , &contig_found, &is_error, ALLOC_CLUSTERS_PACKED);
        ERTFS_ASSERT_TEST(contig_found)
        if (!contig_found)
        {
            return(FALSE);
        }
        region_start_cluster = contig_found;
/*        pro_test_print_two_dwords("Performing force contiguous fit on : ", contig_needed, " clusters at : ", region_start_cluster,PRFLG_NL); */
    }
    /* Set the allocation hint to location with contig_needed clusters free contiguous clusters.
       So we allocate in the range we found.. otherwise we may pick up the minimum number
       of clusters from a different location since it is less than the range we searched for */
    if (!pc_efilio_setalloc(fd, region_start_cluster, 0))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}


    padded_region_start_cluster = region_start_cluster + pad_clusters;
    /* Write clusters to the file, should allocate from 0 to past skip region  */
    append_n_clusters(fd, pad_clusters,region_start_cluster,padded_region_start_cluster-1);
    /* now make a one cluster hole at region start + 4 */
    test_alloc_fragment(padded_region_start_cluster+4, 1);
    /* allocate 8 clusters. since contig is FORCED. will skip past the
       hole and allocate 8 */
    append_n_clusters(fd, 8,padded_region_start_cluster+5,padded_region_start_cluster+12);
    test_free_fragment(padded_region_start_cluster+4, 1);
    if (!pc_efilio_close(fd))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (!pc_unlink(filename))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    return(TRUE);
}
static BOOLEAN _test_efile_force_prealloc(byte *filename, dword options);


/*<TEST> Test option PCE_KEEP_PREALLOC  for 32 and 64 bit files*/
static BOOLEAN test_efile_force_prealloc(void)
{
    if (!pro_test_free_manager_atached())
    {
        PRO_TEST_ANNOUNCE("Free manager disabled, pre-allocation disabled: Not testing PCE_FORCE_CONTIGUOUS ");
        return(TRUE);
    }
    PRO_TEST_ANNOUNCE("Testing PCE_FORCE_CONTIGUOUS option 32 bit file");
    _test_efile_force_prealloc(TEST_FILE32, PCE_FORCE_CONTIGUOUS);
    PRO_TEST_ANNOUNCE("Testing PCE_FORCE_CONTIGUOUS|PCE_KEEP_PREALLOC option 32 bit file");
    _test_efile_force_prealloc(TEST_FILE32, PCE_FORCE_CONTIGUOUS|PCE_KEEP_PREALLOC);
    return(TRUE);
}

/*
  _test_efile_force_prealloc - Verify cluster pre allocation feature
    min_clusters_per_allocation allocation filed in the file open
    options structure. verifies that this many clusters are pre-allocated
    when the file is extended.
    also tests PCE_KEEP_PREALLOC that assigns excess preallocated
    clusters to the file when it is closed.
*/
static BOOLEAN _test_efile_force_prealloc(byte *filename, dword options)
{
    int fd;
    dword region_start_cluster;
    dword pad_clusters;
    dword padded_region_start_cluster;
    dword reserved_cluster_size,file_cluster_cluster_size;


    region_start_cluster = _test_get_fileregion_start();


    pad_clusters = 128; /* some random value */


    reserved_cluster_size=file_cluster_cluster_size=0;
    fd = pro_test_efile_create(filename,options,pad_clusters);
    /* Testing proper function of adjacent contiguous operations, find a segment that is large
       enough for the test */
    {
        dword contig_needed, contig_found;
        int is_error;
        contig_needed = pad_clusters * 4;
        contig_found = fatop_find_contiguous_free_clusters(test_drive_structure(), region_start_cluster, test_drive_structure()->drive_info.maxfindex, contig_needed , contig_needed , &contig_found, &is_error, ALLOC_CLUSTERS_PACKED);
        ERTFS_ASSERT_TEST(contig_found)
        if (!contig_found)
        {
            return(FALSE);
        }
        region_start_cluster = contig_found;
    }
    /* Set the allocation hint to location with 4 * pad_clusters free contiguous clusters.
       So we allocate in the range we found.. otherwise we may pick up the minimum number
       of clusters from a different location since it is less than the range we searched for */
    if (!pc_efilio_setalloc(fd, region_start_cluster, 0))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}

    padded_region_start_cluster = region_start_cluster + pad_clusters;

    /* Write 1 cluster the file, should pre-allocate from 0 to past skip region  */
    append_n_clusters(fd,
        1,
        region_start_cluster,
        padded_region_start_cluster-1);
    reserved_cluster_size += pad_clusters;
    file_cluster_cluster_size += 1;
    /* Write pad_clusters-1 cluster the file, shouldn't see any new allocations */
    append_n_clusters(fd,
        pad_clusters-1,
        region_start_cluster
        ,padded_region_start_cluster-1);

    file_cluster_cluster_size += pad_clusters-1;

    /* now make a one cluster hole at region start + 4 */
    test_alloc_fragment(padded_region_start_cluster+4, 1);

    /* Write 1 cluster the file, should pre-allocate from
        padded_region_start_cluster+5 to
        padded_region_start_cluster+pad_clusters+4 */
    append_n_clusters(fd, 1,
    padded_region_start_cluster+5,
     padded_region_start_cluster+5+pad_clusters-1);
    reserved_cluster_size += pad_clusters;
    file_cluster_cluster_size += 1;
    /* Write pad_clusters-1 cluster the file, shouldn't see any new allocations */
    append_n_clusters(fd,         pad_clusters-1,
    padded_region_start_cluster+5,
    padded_region_start_cluster+5+pad_clusters-1);
    file_cluster_cluster_size += pad_clusters-1;


   /* Write 1 cluster the file, should pre-allocate from
        padded_region_start_cluster+6 to
        padded_region_start_cluster+pad_clusters+5 */
    append_n_clusters(fd, 1,
    padded_region_start_cluster+5+pad_clusters,
    padded_region_start_cluster+5+pad_clusters+pad_clusters-1);
    reserved_cluster_size += pad_clusters;
    file_cluster_cluster_size += 1;
    /* Release the hole */
    test_free_fragment(padded_region_start_cluster+4, 1);
    if (!pc_efilio_close(fd))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    /* Now check the file size if using keep should be all we alloc, else
       all we used. */
    {
        ERTFS_STAT stat_buff;
        dword blocks_per_cluster,num_blocks;
        blocks_per_cluster = pro_test_bytes_per_cluster()/test_drive_structure()->drive_info.bytespsector;
        if (options & PCE_KEEP_PREALLOC)
            num_blocks = reserved_cluster_size * blocks_per_cluster;
        else
            num_blocks = file_cluster_cluster_size  * blocks_per_cluster;

        if (pc_stat(filename, &stat_buff) < 0)
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        if (stat_buff.st_blocks != num_blocks)
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    }
    if (!pc_unlink(filename))
      {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    return(TRUE);
}

/*
<TEST>  Procedure: test_efile_data_tracking - Verify extended read,write,seek operations on 32 bit and 64 bit files
<TEST>    Fill a file with a pattern and then perform tests to verify that:
<TEST>       The pattern can be read back
<TEST>       File reads stop at end of file
<TEST>       File reads stop at end of file
<TEST>       The pattern can be read back after the file is closed and reopened.
<TEST>       File pattern reads are succesful after seeks to the the following
<TEST>       boundaries:
<TEST>          Beginning of file
<TEST>          End of file
<TEST>       File pattern reads are succesful after random seeks to offsets
<TEST>       within the file.
<TEST>       verify that all seek methods work , method rotates through
<TEST>       PSEEK_SET,PSEEK_CUR,PSEEK_CUR_NEG, and PSEEK_END.
<TEST>       Verify that all seek methods work at boundaries, method rotates through
<TEST>       PSEEK_SET,PSEEK_CUR,PSEEK_CUR_NEG, and PSEEK_END.
*/

static void boundary_seek_test(int fd, dword test_file_size_dw);
static void random_seek_test(int fd, dword test_file_size_dw);


static void test_efile_data_tracking(void)
{
dword test_file_size_dw;

    PRO_TEST_ANNOUNCE("Testing 32 bit File Data Tracking");
    test_file_size_dw = FILESIZE_32;
    _test_efile_data_basic(TEST_FILE32, 0, test_file_size_dw, TRUE);
}

static void _test_efile_data_basic(byte *filename,dword options,dword test_file_size_dw,BOOLEAN do_io)
{
    int fd;
    dword  ntransfered_dw;

    /* Fill a file */
    fd = pro_test_efile_create(filename,options,0);
    ERTFS_ASSERT_TEST(fd>=0)
    if (do_io)
    {
        RTFS_PRINT_STRING_1((byte *)"Writing dword data count dwords == ", 0);
        RTFS_PRINT_LONG_1(test_file_size_dw,PRFLG_NL);
         pro_test_write_n_dwords(fd, 0, test_file_size_dw, &ntransfered_dw,
            TRUE, data_buffer);
    }
    else
    {
        RTFS_PRINT_STRING_1((byte *)"Filling empty data file dwords == ", 0);
        RTFS_PRINT_LONG_1(test_file_size_dw,PRFLG_NL);
        pro_test_write_n_dwords(fd, 0, test_file_size_dw, &ntransfered_dw,
            TRUE, 0);
    }
    ERTFS_ASSERT_TEST(test_file_size_dw == ntransfered_dw)
    if (!do_io)
    {
       pc_efilio_close(fd);
       return;
    }

    PRO_TEST_ANNOUNCE("   Reading data ");
    if (!seek_n_dwords(fd, 0,test_file_size_dw))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    /* Read it back */
    pro_test_read_n_dwords(fd, 0, test_file_size_dw, &ntransfered_dw, TRUE);
    ERTFS_ASSERT_TEST(test_file_size_dw == ntransfered_dw)
    /* should be at the end */
    if (!pro_test_read_n_dwords(fd, 0, test_file_size_dw, &ntransfered_dw, TRUE))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    ERTFS_ASSERT_TEST(ntransfered_dw==0)
    pc_efilio_close(fd);
    fd = test_efile_reopen_file(filename,options,0);
    ERTFS_ASSERT_TEST(fd>=0)
    /* Read it back */
    pro_test_read_n_dwords(fd, 0, test_file_size_dw, &ntransfered_dw, TRUE);
    ERTFS_ASSERT_TEST(test_file_size_dw == ntransfered_dw)
    pc_efilio_close(fd);
    fd = test_efile_reopen_file(filename,options,0);
    PRO_TEST_ANNOUNCE("   Boundary Seek test ");
    boundary_seek_test(fd, test_file_size_dw);
    PRO_TEST_ANNOUNCE("   Random Seek test ");
    random_seek_test(fd, test_file_size_dw);
    pc_efilio_close(fd);
}
static void boundary_seek_test(int fd,dword test_file_size_dw)
{
dword offset_dw;
dword ntransfered_dw,check_range_dw;
dword segment = 0;

    /* Try beginning of file */
    PRO_TEST_ANNOUNCE("Seek Boundary Test begin ");
    if (!seek_n_dwords(fd, 0,test_file_size_dw))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    check_range_dw = 32768;
    if (check_range_dw > test_file_size_dw)
        check_range_dw = 128;

    pro_test_read_n_dwords(fd, 0, check_range_dw, &ntransfered_dw, TRUE);
    ERTFS_ASSERT_TEST(check_range_dw == ntransfered_dw)
    PRO_TEST_ANNOUNCE("Seek Boundary Test End ");
    /* Try end file */
    check_range_dw = 128;
    if (!seek_n_dwords(fd, test_file_size_dw-128,test_file_size_dw))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    pro_test_read_n_dwords(fd, test_file_size_dw-128, check_range_dw,
                &ntransfered_dw, TRUE);
    ERTFS_ASSERT_TEST(check_range_dw == ntransfered_dw)
    /* should be at the end */
    if (!pro_test_read_n_dwords(fd, 0, test_file_size_dw, &ntransfered_dw, TRUE))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    ERTFS_ASSERT_TEST(ntransfered_dw==0)

    /* Now cross segment boudaries */
    offset_dw = DEFAULT_SEGMENT_SIZE/4 - 128;
    while (offset_dw < test_file_size_dw)
    {
        RTFS_PRINT_STRING_1((byte *)"Seek Boundary Segment Overlap ", 0);
        RTFS_PRINT_LONG_1(segment++,PRFLG_NL);
       if (!seek_n_dwords(fd, offset_dw,test_file_size_dw))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
       check_range_dw = 32768;
       if (offset_dw+check_range_dw >= test_file_size_dw)
          check_range_dw = test_file_size_dw-offset_dw;
       pro_test_read_n_dwords(fd, offset_dw, check_range_dw,
                            &ntransfered_dw, TRUE);
       ERTFS_ASSERT_TEST(check_range_dw == ntransfered_dw)
       offset_dw += DEFAULT_SEGMENT_SIZE/4;
    }
}
static void random_seek_test(int fd,dword test_file_size_dw)
{
dword new_offset_dw;
dword ntransfered_dw,check_range_dw;
dword io_pass = 1; /* so we don't print small loops */
    PRO_TEST_ANNOUNCE("Seek Random Test begin ");
    for (io_pass = 1; io_pass < RANDOM_SEEK_TEST_PASSES;io_pass++)
    {
        new_offset_dw = pro_test_dwrand() % test_file_size_dw;
        check_range_dw = test_file_size_dw - new_offset_dw;

        if (check_range_dw > 32768) /* read 128K bytes or be here all day */
            check_range_dw = 32768;
        if (!seek_n_dwords(fd, new_offset_dw,test_file_size_dw))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        /* Read it back */
        pro_test_read_n_dwords(fd, new_offset_dw, check_range_dw,
                            &ntransfered_dw, TRUE);
        ERTFS_ASSERT_TEST(check_range_dw == ntransfered_dw)
    }
    RTFS_PRINT_STRING_1((byte *)"",PRFLG_NL);
}



static BOOLEAN test_efile_open_remap(void)
{
/* Unimplemented */
    return(TRUE);
}

static void test_efile_api_usage(void)
{
/* Unimplemented */
}

static void test_efile_extent_tracking(void)
{
/* Unimplemented */
}
static BOOLEAN test_efile_open_fat64(void)
{
/* Unimplemented */
    return(TRUE);
}
static void test_efile_drive_options(void)
{
/* Unimplemented */
}
static void _test_chsize_simple(BOOLEAN is_64, dword startsize_dw,dword endsize_dw,dword midsize_dw)
{
int fd;
dword ret_hi, ret_lo;
ddword endsize_ddw;
ERTFS_STAT stat;

    RTFS_ARGSUSED_DWORD(midsize_dw); /* Not used */

    /* Create and fill the initial file */
    fd = pro_test_efile_fill(TEST_FILE32, 0,startsize_dw, FALSE, FALSE);
    ERTFS_ASSERT_TEST(fd >= 0)

    /* Now change the size change dword offset to byte offset */
    endsize_ddw =  M64SET32(0, endsize_dw);
    endsize_ddw = M64LSHIFT(endsize_ddw,2);

    if (!pc_efilio_chsize(fd, M64HIGHDW(endsize_ddw), M64LOWDW(endsize_ddw)))
    { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }

    /* Now seek to the end of file, make sure it's the correct size */
    if (!pc_efilio_lseek(fd, 0, 0, PSEEK_END, &ret_hi, &ret_lo))
    {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (ret_hi != M64HIGHDW(endsize_ddw) || ret_lo != M64LOWDW(endsize_ddw))
    {ERTFS_ASSERT_TEST(rtfs_debug_zero())}

    /* Now close the file and check it's size */
    pc_efilio_close(fd);
    if (is_64)
    {
        if (pc_stat(TEST_FILE64, &stat) < 0)
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    }
    else
    {
        if (pc_stat(TEST_FILE32, &stat) < 0)
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    }
    if (stat.st_size_hi != M64HIGHDW(endsize_ddw) || stat.st_size != M64LOWDW(endsize_ddw))
    {ERTFS_ASSERT_TEST(rtfs_debug_zero())}

    /* delete it */
    if (is_64 && !pc_unlink(TEST_FILE64))
    {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (!is_64 && !pc_unlink(TEST_FILE32))
    {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
}
/*<TEST>       Procedure: Test proper operation of pc_efilio_chsize() on 32 and 64 bit files */

static BOOLEAN test_efile_chsize(void)
{
    PRO_TEST_ANNOUNCE("Testing pc_efilio_chsize");
/*  Expand a 32 and 64 bit file from zero */
    _test_chsize_simple(FALSE,0,FILESIZE_32,FILESIZE_32);


/*   Contract a 32 and 64 bit file */
    _test_chsize_simple(FALSE,FILESIZE_32,FILESIZE_32/2,FILESIZE_32/2);

/*   Contract a 32 bit and 64 bit file to zero */
    _test_chsize_simple(FALSE,FILESIZE_32,0,0);

/*   Contract a 32 and 64 bit file several times without flush */
    _test_chsize_simple(FALSE,FILESIZE_32,FILESIZE_32/2,FILESIZE_32/4);
    return(TRUE);
}


static BOOLEAN seek_n_dwords(int fd, dword _offset_in_dwords,dword test_file_size_dw)
{
    dword   offset_in_dwords, offset_hi, offset_lo,ret_hi, ret_lo;
    dword   expect_hi, expect_lo;
    ddword offset_ddw,expect_ddw;
    int new_method;
    /* Variables used to vary the seek method between END, CURR and SET */
    static int current_seek_method = PSEEK_SET;
    dword current_offset_in_dwords;

    /* Get current offset and div by 4 to get offset in dwords */
    if (!pc_efilio_lseek(fd, 0, 0, PSEEK_CUR, &ret_hi, &ret_lo))
    {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    offset_ddw = M64SET32(ret_hi, ret_lo);
    offset_ddw = M64RSHIFT(offset_ddw,2);
    current_offset_in_dwords = M64LOWDW(offset_ddw);


    /* rotate through seek methods. normalize offset to method */
    offset_in_dwords = _offset_in_dwords;
    new_method = current_seek_method;
    if (current_seek_method == PSEEK_SET)
    {
        offset_in_dwords = _offset_in_dwords;
        new_method = PSEEK_CUR;
    }
    else if (current_seek_method == PSEEK_CUR)
    {
        if (_offset_in_dwords >= current_offset_in_dwords)
        {
            offset_in_dwords = _offset_in_dwords -
                                    current_offset_in_dwords;
        }
        else
        {
            offset_in_dwords =  current_offset_in_dwords -
                                    _offset_in_dwords;
            current_seek_method = PSEEK_CUR_NEG;
        }
        new_method = PSEEK_END;
    }
    else if (current_seek_method == PSEEK_END)
    {
        offset_in_dwords = test_file_size_dw - _offset_in_dwords;
        new_method = PSEEK_SET;
    }
    offset_ddw = M64SET32(0, offset_in_dwords);
    offset_ddw = M64LSHIFT(offset_ddw,2);
    offset_lo = M64LOWDW(offset_ddw);
    offset_hi = M64HIGHDW(offset_ddw);
    /* We expect to land at the abloulte coordinates */
    expect_ddw = M64SET32(0, _offset_in_dwords);
    expect_ddw = M64LSHIFT(expect_ddw,2);
    expect_lo = M64LOWDW(expect_ddw);
    expect_hi = M64HIGHDW(expect_ddw);


    if (!pc_efilio_lseek(fd, offset_hi, offset_lo, current_seek_method,
            &ret_hi, &ret_lo) ||
            ret_hi != expect_hi || ret_lo != expect_lo)
    {
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    }
    /* remember where we are */
    current_seek_method = new_method;
    return(TRUE);
}



/* Allocate and free fragments for the fat */
static void test_alloc_fragment(dword start_contig, dword n_contig)
{
    fatop_link_frag(pc_drno2dr(test_drivenumber),0,0,
        FOP_RMTELL|FOP_LINK, 0, start_contig, n_contig);

}
static void test_free_fragment(dword start_contig, dword n_contig)
{
    fatop_link_frag(pc_drno2dr(test_drivenumber),0,0,
        FOP_RMTELL, 0, start_contig, n_contig);
}


static BOOLEAN append_n_clusters(int fd, int clusters, dword expected_start,dword expected_end)
{
dword ntowrite, nwritten;


    ntowrite = clusters * pro_test_bytes_per_cluster();
    if (!pc_efilio_write(fd,0, ntowrite, &nwritten) || ntowrite != nwritten)
    {
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        return(FALSE);
    }
    if (expected_start && expected_start != debug_first_efile_cluster_allocated)
    {
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        return(FALSE);
    }
    if (expected_end && (expected_end != debug_last_efile_cluster_allocated))
    {
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        return(FALSE);
    }
    return(TRUE);
}


static int test_efile_reopen_file(byte *filename, dword options, dword min_clusters_per_allocation)
{
    int fd;
    EFILEOPTIONS my_options;

    rtfs_memset(&my_options, 0, sizeof(my_options));
    my_options.allocation_policy = options;
    my_options.min_clusters_per_allocation = min_clusters_per_allocation;

    fd = pc_efilio_open(filename,(word)(PO_BINARY|PO_RDWR),(word)(PS_IWRITE | PS_IREAD)
                         ,&my_options);
    ERTFS_ASSERT_TEST(fd>=0)

    return(fd);
}

#endif /* (!INCLUDE_DEBUG_TEST_CODE) else.. */
#endif /* Exclude from build if read only */
