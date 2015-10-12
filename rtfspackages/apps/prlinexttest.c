/*
* Copyright EBS Inc. 1987-2007
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* PREFTEST.C - Test enhanced file IO routines source code.
<TEST>  Test File:   rtfspackages/apps/prlinexttest.c
<TEST>
<TEST>   Procedure: test_efilio_extract(void)
<TEST>   Description: Tests pc_efilio_swap(), pc_efilio_extract(), and pc_efilio_remove() file cluster manipulation routines.
<TEST>   Test suite performs tests to verify the correct operation of pc_efilio_swap(), pc_efilio_extract(), and pc_efilio_remove() on 32 and 64 bit files
<TEST>   Files are created and filled with data that identifies each file's cluster.
<TEST>   Segments of files are moved and removed using the pc_efilio_swap(), pc_efilio_extract(), and pc_efilio_remove() API calls
<TEST>   File chains and data are check to verify proper operation
<TEST>
*/

#include "rtfs.h"

#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */
#include "protests.h"


/* See prapilinext.c */
dword pc_byte2clmod64(DDRIVE *pdr, dword nbytes_hi, dword nbytes_lo);

#define LTNAME1 "FILE1"
#define LTNAME2 "FILE2"
#define TEST_EXTRACT 1
#define TEST_SWAP    2
#define TEST_REMOVE  3


#define INCLUDE_LINEXT_DEBUG 0
#if (INCLUDE_LINEXT_DEBUG)
void DEBUG_dumpfrag_chain(char *which_frag, REGION_FRAGMENT *pf);
void DEBUG_file_dumpfrag_chain(char *which_frag, int fd1)
{
ERTFS_EFILIO_STAT file_stats;
 if (!pc_efilio_fstat(fd1, &file_stats))
    return;
 DEBUG_dumpfrag_chain(which_frag, file_stats.pfirst_fragment[0]);
}
#endif

typedef struct extract_test_struct {
byte *fname1;
dword sizecl_1;        /* Original file size */
dword fpcl_1;          /* File pointer */
byte *fname2;
dword sizecl_2;        /* Original file size */
dword fpcl_2;          /* File pointer */
dword n_clusters;
} EXTRACT_TEST_STRUCT;

static int open_test_file(byte *filename, BOOLEAN is_create);
static BOOLEAN create_test_file(byte *filename, dword n_clusters);
static void test_extract_combinations(void);
static void test_one_extract_combination(EXTRACT_TEST_STRUCT *plt);
static void _test_one_combination(EXTRACT_TEST_STRUCT *plt);
static void _test_one_operation(EXTRACT_TEST_STRUCT *plt, int which_test);
static void test_linext_unlink(byte *file_name);

void  test_efilio_extract(byte *pdriveid)
{
    if (!set_test_drive(pdriveid))
    {ERTFS_ASSERT_TEST(rtfs_debug_zero())}

    test_extract_combinations();
}


static void test_extract_combinations(void)
{
EXTRACT_TEST_STRUCT lt;

    /* Test SPLIT operation */
    lt.fname1 = (byte *) LTNAME1;
    lt.fname2 = (byte *) LTNAME2;

#if (INCLUDE_EXFATORFAT64)
	if (ISEXFATORFAT64(test_drive_structure()))
	{
		/* Move 1000 clusters of a 512K cluster file (should be a 64 bit file) to an empty file */
		lt.sizecl_1     =   (131072*4);
		lt.fpcl_1       =     0;
		lt.sizecl_2     =     0;
		lt.fpcl_2       =     0;
		lt.n_clusters   =   1000;
		{
			DDRIVE *pdr;
			pdr = test_drive_structure();
			/* Both files exist at the same time in the delete test so we can only take up total-size2 clusters for file 1 */
			if (lt.sizecl_1 > pdr->drive_info.known_free_clusters-lt.n_clusters)
				lt.sizecl_1 = pdr->drive_info.known_free_clusters-lt.n_clusters;
		}
		test_one_extract_combination(&lt);
	}
#endif
    /* Test some combinations of 32 bit files */
    /* Move 100 clusters of a 100 cluster file to an empty file */
    lt.sizecl_1     =   100;
    lt.fpcl_1       =     0;
    lt.sizecl_2     =     0;
    lt.fpcl_2       =     0;
    lt.n_clusters   =   100;
    test_one_extract_combination(&lt);


    /*<TEST>     Insert clusters 0-99 of a 100 cluster file in the front of a 10 cluster file */
    lt.sizecl_2     =     10;
    lt.n_clusters   =   100;
    lt.fpcl_2       =      0;
    test_one_extract_combination(&lt);
    /*<TEST>     Insert clusters 0-99 of a 100 cluster file in the front of a 10 cluster file */
    lt.fpcl_2       =      5;
    test_one_extract_combination(&lt);
    /*<TEST>     Insert clusters 0-99 of a 100 cluster file at the end of a 10 cluster file */
    lt.fpcl_2       =      10;
    test_one_extract_combination(&lt);

    /*<TEST>     Insert clusters 0-9 of a 100 cluster file in the front of a 10 cluster file */
    lt.n_clusters   =     10;
    lt.fpcl_2       =      0;
    test_one_extract_combination(&lt);
    /*<TEST>     Insert clusters 1-10 of a 100 cluster file in the middle of a 10 cluster file */
    lt.n_clusters   =   10;
    lt.fpcl_1       =    1;
    lt.fpcl_2       =    5;
    test_one_extract_combination(&lt);
    /*<TEST>     Insert clusters 1-10 of a 100 cluster file at the end of a 10 cluster file */
    lt.n_clusters   =   10;
    lt.fpcl_1       =    1;
    lt.fpcl_2       =    10;
    test_one_extract_combination(&lt);
}

static void test_one_extract_combination(EXTRACT_TEST_STRUCT *plt)
{
dword max_f32_clusters,two_gig_in_clusters;
BOOLEAN f1_mustbe_64,f2_mustbe_64;

    /* Calculate the minimum number of clusters to hold 2 Gigabytes */
    if (!pc_bytes_to_clusters(pc_get_default_drive(0), 0, 0x80000000, &two_gig_in_clusters))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    /* Calculate clusters in 4 Gigabytes */
    max_f32_clusters = 2 * two_gig_in_clusters;

    f1_mustbe_64 = f2_mustbe_64 = FALSE;
    if (plt->sizecl_1  > max_f32_clusters)
        f1_mustbe_64 = TRUE;
    if (plt->sizecl_2 + plt->n_clusters > max_f32_clusters)
        f2_mustbe_64 = TRUE;
	_test_one_combination(plt);

}



static BOOLEAN pc_seek_clusters(int fd, dword n_clusters)
{
    dword fplo, fphi, ltemp_hi, ltemp_lo;
    if (!pc_clusters_to_bytes(pc_get_default_drive(0), n_clusters, &fphi, &fplo))
        return(FALSE);

    /* Seek to the file pointers */
    if (!pc_efilio_lseek(fd, fphi, fplo, PSEEK_SET, &ltemp_hi, &ltemp_lo))
        return(FALSE);
    if (fphi != ltemp_hi)
        return(FALSE);
    if (fplo != ltemp_lo)
        return(FALSE);
    return(TRUE);
}

#define MOVED_1_DATA_START  0x11111111
#define MOVED_1_DATA_END    0x22222222
#define MOVED_2_DATA_START  0x33333333
#define MOVED_2_DATA_END    0x44444444

static void _test_one_combination(EXTRACT_TEST_STRUCT *plt)
{

    /* Always test EXTRACT and REMOVE */
    PRO_TEST_ANNOUNCE("        Testing pc_efilio_extract()");
    _test_one_operation(plt, TEST_EXTRACT);

    PRO_TEST_ANNOUNCE("        Testing pc_efilio_remove()");
    _test_one_operation(plt, TEST_REMOVE);

    /* Test swap if conditions are correct */
    if (plt->n_clusters <= (plt->sizecl_2  - plt->fpcl_2))
    {
        PRO_TEST_ANNOUNCE("        Testing pc_efilio_swap()");
        _test_one_operation(plt, TEST_SWAP);
    }
    test_linext_unlink(plt->fname1);
    test_linext_unlink(plt->fname2);
}

static void _test_one_operation(EXTRACT_TEST_STRUCT *plt, int which_test)
{
int fd1;
int fd2;
dword ltemp;
ERTFS_EFILIO_STAT file_stats;
dword cluster_1_new_length, cluster_2_new_length;
dword nwritten, n_moved_clusters, cloff_1_ext_start, cloff_1_ext_end, cloff_2_ext_start, cloff_2_ext_end;
dword expect_len_hi,expect_len_lo,extract_len_hi,extract_len_lo,size_1_hi,size_1_lo,size_2_hi,size_2_lo;
byte *test_buffer_raw = 0;
dword *test_buffer;
dword sector_size_bytes;
    fd1 = fd2 = 0;
    cluster_1_new_length = 0;

    sector_size_bytes = pro_test_bytes_per_sector();
    if (!sector_size_bytes)
    {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    test_buffer_raw = (byte *) pro_test_malloc(sector_size_bytes);
    test_buffer = (dword *) test_buffer_raw;

    cloff_1_ext_start = plt->fpcl_1;
    cloff_2_ext_start = cloff_2_ext_end = 0;
    test_linext_unlink(plt->fname1);
    if (!create_test_file(plt->fname1, plt->sizecl_1)) /* Create file 1 */
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    fd1 = open_test_file(plt->fname1, FALSE);  /* reopen file 1 */
    if (fd1 < 0)
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    /* Mark the start and end of the region to remove, swap or extract */
    if (!pc_seek_clusters(fd1, cloff_1_ext_start))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    *test_buffer = MOVED_1_DATA_START;
    if (!pc_efilio_write(fd1, (byte *) test_buffer, sector_size_bytes, &nwritten) ||  sector_size_bytes != nwritten )
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    /* Mark the end of the region */
    if (plt->n_clusters)
        n_moved_clusters = plt->n_clusters;
    else
        n_moved_clusters = plt->sizecl_1-plt->fpcl_1;
    cloff_1_ext_end = cloff_1_ext_start + n_moved_clusters -1;

    if (cloff_1_ext_end != cloff_1_ext_start)
    {
        if (!pc_seek_clusters(fd1, cloff_1_ext_end))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        *test_buffer = MOVED_1_DATA_END;
        if (!pc_efilio_write(fd1, (byte *) test_buffer, sector_size_bytes, &nwritten) ||  sector_size_bytes != nwritten )
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    }
    /* Seek to the file pointer  */
    if (!pc_seek_clusters(fd1, plt->fpcl_1))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}

    if (which_test == TEST_EXTRACT || which_test == TEST_SWAP)
    {
        test_linext_unlink(plt->fname2);
        if (!create_test_file(plt->fname2, plt->sizecl_2)) /* Create file 2 */
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        fd2 = open_test_file(plt->fname2, FALSE); /* reopen file 2 */
        if (fd2 < 0)
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        if (!pc_seek_clusters(fd2, plt->fpcl_2))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    }

    if (which_test == TEST_SWAP || which_test == TEST_EXTRACT)
    {
        cloff_2_ext_start = plt->fpcl_2;
        cloff_2_ext_end   = cloff_2_ext_start + n_moved_clusters - 1;
    }
    if (which_test == TEST_EXTRACT)
    {
        /* Perform the extract */
        if (!pc_efilio_extract(fd1, fd2, plt->n_clusters))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    }
    else if (which_test == TEST_SWAP)
    {
        /* Mark the start and end of the region in fd2 to swap */
        if (!pc_seek_clusters(fd2, cloff_2_ext_start))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        *test_buffer = MOVED_2_DATA_START;
        if (!pc_efilio_write(fd2, (byte *) test_buffer, sector_size_bytes, &nwritten) ||  sector_size_bytes != nwritten )
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        /* Mark the end of the region */
        if (cloff_2_ext_end != cloff_2_ext_start)
        {
            if (!pc_seek_clusters(fd2, cloff_2_ext_end))
                {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
            *test_buffer = MOVED_2_DATA_END;
            if (!pc_efilio_write(fd2, (byte *) test_buffer, sector_size_bytes, &nwritten) ||  sector_size_bytes != nwritten )
                {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        }
        /* Seek to the file pointer  */
        if (!pc_seek_clusters(fd2, plt->fpcl_2))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        /* Perform the swap */
        if (!pc_efilio_swap(fd1, fd2, plt->n_clusters))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    }
    else if (which_test == TEST_REMOVE)
    {
        /* Perform the remove */
        if (!pc_efilio_remove(fd1, plt->n_clusters))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    }

    /* Check the files now */

    if (!pc_clusters_to_bytes(pc_get_default_drive(0), plt->n_clusters, &extract_len_hi, &extract_len_lo))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (!pc_clusters_to_bytes(pc_get_default_drive(0), plt->sizecl_1, &size_1_hi, &size_1_lo))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (!pc_clusters_to_bytes(pc_get_default_drive(0), plt->sizecl_2, &size_2_hi, &size_2_lo))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}

    if (which_test == TEST_EXTRACT || which_test == TEST_REMOVE)
    {
        dword expect_len_hi, expect_len_lo;
        /* Check the new size of file 1 */
        if (plt->n_clusters)
            cluster_1_new_length = plt->sizecl_1 - plt->n_clusters;
        else
            cluster_1_new_length = plt->fpcl_1;

        if (!pc_efilio_fstat(fd1, &file_stats))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        if (!pc_bytes_to_clusters(pc_get_default_drive(0), file_stats.file_size_hi, file_stats.file_size_lo, &ltemp))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        /* Check cluster length */
        if (ltemp != cluster_1_new_length)
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        /* Check byte length */
        pc_subtract_64(size_1_hi, size_1_lo, extract_len_hi, extract_len_lo, &expect_len_hi, &expect_len_lo);
        if (expect_len_hi != file_stats.file_size_hi ||  expect_len_lo != file_stats.file_size_lo)
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        /* Close file, reopen and check again */
        if (!pc_efilio_close(fd1))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        fd1 = open_test_file(plt->fname1, FALSE);
        if (fd1 < 0)
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        /* Check the new size of file 1 */
        if (!pc_efilio_fstat(fd1, &file_stats))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        if (!pc_bytes_to_clusters(pc_get_default_drive(0), file_stats.file_size_hi, file_stats.file_size_lo, &ltemp))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        if (ltemp != cluster_1_new_length)
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        /* Check byte length */
        pc_subtract_64(size_1_hi, size_1_lo, extract_len_hi, extract_len_lo, &expect_len_hi, &expect_len_lo);
        if (expect_len_hi != file_stats.file_size_hi ||  expect_len_lo != file_stats.file_size_lo)
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    }
    if (which_test == TEST_SWAP)
    {
        /* Check size of fd1 and fd2 */
        if (!pc_efilio_fstat(fd1, &file_stats))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        if (file_stats.file_size_hi != size_1_hi || file_stats.file_size_lo != size_1_lo)
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        if (!pc_efilio_fstat(fd2, &file_stats))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        if (file_stats.file_size_hi != size_2_hi || file_stats.file_size_lo != size_2_lo)
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        /* Check contents of fd1 */
        if (!pc_seek_clusters(fd1, cloff_1_ext_start))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        if (!pc_efilio_read(fd1, (byte *) test_buffer, sector_size_bytes, &nwritten) ||  sector_size_bytes != nwritten )
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        if (*test_buffer != MOVED_2_DATA_START)
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        if (cloff_1_ext_end != cloff_1_ext_start)
        {
            if (!pc_seek_clusters(fd1, cloff_1_ext_end))
                {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
            if (!pc_efilio_read(fd1, (byte *) test_buffer, sector_size_bytes, &nwritten) ||  sector_size_bytes != nwritten )
                {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
            if (*test_buffer != MOVED_2_DATA_END)
                {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        }
    }
    if (!pc_efilio_close(fd1))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}

    if (which_test == TEST_EXTRACT)
    {
        /* Check the new size of file 2 */
        cluster_2_new_length = plt->sizecl_2 + (plt->sizecl_1 - cluster_1_new_length);
        if (!pc_efilio_fstat(fd2, &file_stats))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        if (!pc_bytes_to_clusters(pc_get_default_drive(0), file_stats.file_size_hi, file_stats.file_size_lo, &ltemp))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        if (ltemp != cluster_2_new_length)
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        /* Close file, reopen and check again */
        if (!pc_efilio_close(fd2))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        fd2 = open_test_file(plt->fname2, FALSE);
        if (fd2 < 0)
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        if (!pc_efilio_fstat(fd2, &file_stats))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        if (!pc_bytes_to_clusters(pc_get_default_drive(0), file_stats.file_size_hi, file_stats.file_size_lo, &ltemp))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        if (ltemp != cluster_2_new_length)
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        /* Test byte length */
        pc_add_64(size_2_hi, size_2_lo, extract_len_hi, extract_len_lo, &expect_len_hi, &expect_len_lo);
        if (expect_len_hi != file_stats.file_size_hi ||  expect_len_lo != file_stats.file_size_lo)
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}

    }
    if (which_test == TEST_EXTRACT || which_test == TEST_SWAP)
    {
        if (!pc_seek_clusters(fd2, cloff_2_ext_start))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        if (!pc_efilio_read(fd2, (byte *) test_buffer, sector_size_bytes, &nwritten) ||  sector_size_bytes != nwritten )
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        if (*test_buffer != MOVED_1_DATA_START)
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        if (cloff_2_ext_end != cloff_2_ext_start)
        {
            if (!pc_seek_clusters(fd2, cloff_2_ext_end))
                {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
            if (!pc_efilio_read(fd2, (byte *) test_buffer, sector_size_bytes, &nwritten) ||  sector_size_bytes != nwritten )
                {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
            if (*test_buffer != MOVED_1_DATA_END)
            {
                {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
            }
        }
        if (!pc_efilio_close(fd2))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    }
    if (test_buffer_raw)
        pro_test_free(test_buffer_raw);
}

/* Open a file to be the target of an extract */
static int open_test_file(byte *filename, BOOLEAN is_create)
{
    EFILEOPTIONS options;
    word open_flags;
    rtfs_memset(&options, 0, sizeof(options));
    options.min_clusters_per_allocation = 0;
    open_flags = (word)(PO_BINARY|PO_RDWR);
    if (is_create)
        open_flags |=  (word)(PO_TRUNC|PO_CREAT);
    return(pc_efilio_open((byte *)filename, open_flags, (word)(PS_IWRITE | PS_IREAD),&options));
}

/* Create a file and fill it to size_hi:size_lo bytes wide. (data is not actually written, file is just expanded
   by passing a null buffer */
static BOOLEAN create_test_file(byte *filename, dword n_clusters)
{
    int fd;
    dword nwritten;
    dword size_hi, size_lo;

    if (!pc_clusters_to_bytes(pc_get_default_drive(0), n_clusters, &size_hi, &size_lo))
        return(FALSE);
    fd = open_test_file(filename, TRUE);
    if (fd < 0)
    {
        return(FALSE);
    }
    /* Write from the first 4 gigabyte region */
    if (size_lo)
    {
        if (!pc_efilio_write(fd, (byte*)0, size_lo, &nwritten) || size_lo != nwritten )
        {
            pc_efilio_close(fd);
            return(FALSE);
        }
    }
    /* Write from the the next 4 gigabyte regions if there are any  */
    if (size_hi)
    {
    ddword gig_ddw,nleft_ddw;
        gig_ddw  =  M64SET32(0,0x40000000);   /* 1 gigabyte as ddword */
        nleft_ddw = M64SET32(size_hi,0);

        while (M64NOTZERO(nleft_ddw))
        {
            if (M64GT(nleft_ddw, gig_ddw))
            {  /* If more than 1 gig left write just 1 gigabyte */
                if (!pc_efilio_write(fd, (byte*)0, 0x40000000, &nwritten) || 0x40000000 != nwritten )
                {
                    pc_efilio_close(fd);
                    return(FALSE);
                }
                nleft_ddw = M64MINUS(nleft_ddw,gig_ddw);
            }
            else
            {  /* If less or equal to 1 gig left write what is left */
                dword nleft_dw;
                nleft_dw = M64LOWDW(nleft_ddw);
                if (!pc_efilio_write(fd, (byte*)0, nleft_dw, &nwritten) || nleft_dw != nwritten )
                {
                    pc_efilio_close(fd);
                    return(FALSE);
                }
                nleft_ddw = M64SET32(0,0);
            }
        }
    }
    pc_efilio_close(fd);
    return(TRUE);
}

/* Unlink. But use fast version if available */
static void test_linext_unlink(byte *file_name)
{
#if (INCLUDE_ASYNCRONOUS_API)
int ret_val;
int fd;
    ret_val = PC_ASYNC_ERROR;
    fd = pc_efilio_async_unlink_start(file_name);
    if (fd >= 0)
    {
        /* Step until we exit FILE state */
        ret_val = pc_async_continue(pc_fd_to_driveno(fd,0,0), DRV_ASYNC_IDLE, 0);
    }
    if (ret_val == PC_ASYNC_COMPLETE)
        return;
    /* Fall through if we failed.. pc_unlink is less fragile */
#endif
    pc_unlink(file_name);
}


#endif /* Exclude from build if read only */
