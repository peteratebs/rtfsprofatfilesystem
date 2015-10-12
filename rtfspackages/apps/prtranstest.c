/* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/*
<TEST>  Test File:   prtranstest.c
<TEST>  Procedure:   test_efilio_transactions()
<TEST>  Description: Regression test for RTFS transaction file operations on 32 bit and 64 bit files.
<TEST>
*/

#include "rtfs.h"
#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */
#if (!(INCLUDE_DEBUG_TEST_CODE && INCLUDE_FAILSAFE_CODE && INCLUDE_TRANSACTION_FILES))
void test_efilio_transactions(byte *pdriveid)
{
    RTFS_ARGSUSED_PVOID((void *)pdriveid);
    RTFS_PRINT_STRING_1((byte *)"Build with INCLUDE_DEBUG_TEST_CODE and INCLUDE_TRANSACTION_FILES to run test", PRFLG_NL);
}
#else
#include "protests.h"


/*
void test_efilio_transactions(void)
   This routine tests file opened with the
   PCE_TRANSACTION_FILE policy
*/



int _do_efile_apply_config(void);

/* Transaction file support tests */
static byte *transaction_buffer;
/* Test argument handling for pc_efilio_open with transaction support */
static void do_test_open_transaction_file(dword is_meta_64);
/* Test reading, writing and overwriting of transaction data. Simulate
   power interrupts by aborting disk mount sessions and verify that
   file overwrites are completed properly during system restore, */
static void do_test_single_transactions(void);
byte *tu_filename;
dword tu_allocation_policy;
dword tu_bytes_per_cluster;
static void do_test_random_transactions(void);
static void do_init_overwrite_test(dword segment, dword fill_count);
static void display_transaction_write_parms(dword fill_count, dword offset, dword write_length, dword segment);
static void do_one_overwrite_test(dword fill_count, dword offset, dword write_length, dword segment);

#define TRANSACTION_FILE (byte *) "transaction_file"

/* Transaction file support test entry point */

/*
<TEST>
<TEST>   Test reading, writing and overwriting of file data with the file opened in PCE_TRANSACTION_FILE mode.
<TEST>   Simulate power interrupts by aborting disk mount sessions.
<TEST>   Verify that aborted transaction file overwrites are completed properly during system restore.
<TEST>
<TEST>   Perform the test using random file pointer offsets and random file write sizes
<TEST>
<TEST>   Test transaction overwrites at various boundary conditions including
<TEST>   Beginning of file
<TEST>   End of file
<TEST>   End of file
*/

void test_efilio_transactions(byte *pdriveid)
{
    if (!set_test_drive(pdriveid))
        return;
    data_buffer = 0;
    pro_test_alloc_data_buffer();

    /* Make sure we are mounted */
    pc_set_default_drive(test_driveid);
    pc_set_cwd((byte *)"\\");

    /* Save off bytes per cluster while we know the drive is mounted */
    tu_bytes_per_cluster = pro_test_bytes_per_cluster();
    tu_filename = TRANSACTION_FILE;
    tu_allocation_policy = 0;
    /* Allocate a data buffer for transaction overwrites */
    transaction_buffer = (byte *) pro_test_malloc(tu_bytes_per_cluster);
    ERTFS_ASSERT_TEST(transaction_buffer!=0)

    pc_unlink(TRANSACTION_FILE); /* Make sure it's not there to start  */

    /* Test file open option 32 bit */
    do_test_open_transaction_file(0);
    /* Test file open option 64 bit */
    /* Test transaction overwrites of random sizes at random offsets */
    do_test_random_transactions();
    /* Test transaction overwrites at various boundary conditions */
    do_test_single_transactions();
    pro_test_free_data_buffer();
    pro_test_free(transaction_buffer);
}

static void _do_random_overwrite_tests(dword allocation_policy,dword segment);

static void do_test_random_transactions(void)
{
   /* 32 bit file no segment offset into the file */
    _do_random_overwrite_tests(0,0);
}

/* Perform file overwrites of various sizes and varying offsets */
static void _do_single_overwrite_tests(dword allocation_policy,dword segment);

static void do_test_single_transactions(void)
{
    /* 32 bit file no segment offset into the file */
    _do_single_overwrite_tests(0,0);
}

#define NUM_RANDOM_OVERWRITES 1000
#define RANDOM_TEST_FILLCOUNT 100000

static void _do_random_overwrite_tests(dword allocation_policy,dword segment)
{
dword loop_count, offset, write_length,ltemp2, blocks_total,blocks_free;

    /* Make sure the test file is erased from the volume
       enable failsafe and save free space value */
    fs_api_disable(test_driveid, TRUE);
    pc_unlink(tu_filename); /* Make sure it's deleted */
    fs_api_enable(test_driveid, TRUE);

    /* Cheat use a global variable */
    tu_allocation_policy = allocation_policy;
    /* Create a file containing segment+fill_count_dwords. write a pattern
       containing fill_count dword starting at offset "segment" in bytes */
    do_init_overwrite_test(segment, RANDOM_TEST_FILLCOUNT);
    /* Remember what freespace should be */
    pc_blocks_free(test_driveid, &blocks_total, &blocks_free);
    for (loop_count = 0; loop_count < NUM_RANDOM_OVERWRITES; loop_count++)
    {
        /* randomize offset and write length between 0 and fill_count */
        do {
            offset = pro_test_dwrand() % RANDOM_TEST_FILLCOUNT;
            write_length = pro_test_dwrand() % (RANDOM_TEST_FILLCOUNT - offset);
        } while (!offset || !write_length);
        RTFS_PRINT_STRING_1((byte *)"Trans random write loop, offset, write_count - ", 0);
        RTFS_PRINT_LONG_1(loop_count,0);
        RTFS_PRINT_STRING_1((byte *)",", 0);
        RTFS_PRINT_LONG_1(offset,0);
        RTFS_PRINT_STRING_1((byte *)",", 0);
        RTFS_PRINT_LONG_1(write_length,PRFLG_NL);
        /* Now verify an overwrite of write_length dwords at offset "offset" */
         do_one_overwrite_test(RANDOM_TEST_FILLCOUNT, offset, write_length, segment);
        pc_blocks_free(test_driveid, &blocks_total, &ltemp2);
        ERTFS_ASSERT_TEST(ltemp2==blocks_free)
    }

    pc_unlink(tu_filename); /* Make sure it's deleted */
}


/* Sequence through a pattern of sizes and offsets performing overwrite
   thests */
static void do_one_single_overwrite_test(dword fill_count, dword offset, dword write_length, dword segment);
static void _do_single_overwrite_tests(dword allocation_policy,dword segment)
{
dword cluster_offset;
dword block_offset;
dword quarter_block_offset;
dword cluster_length;
dword block_length;
dword quarter_block_length;
dword offset;
dword fill_count;
dword write_length;


    tu_filename = TRANSACTION_FILE;
    tu_allocation_policy = allocation_policy;

    for (cluster_offset = 0; cluster_offset < 2;cluster_offset++)
    for (block_offset = 0; block_offset < 2;block_offset++)
    for (quarter_block_offset = 0; quarter_block_offset < 2; quarter_block_offset++)
    {
    /* Offset in quads */
    offset =    cluster_offset * (tu_bytes_per_cluster/4) +              // :LB:
                block_offset * 128 +
                quarter_block_offset * 32;
    for (cluster_length = 0; cluster_length < 2;cluster_length++)
    for (block_length = 0; block_length < 2;block_length++)
    for (quarter_block_length = 0; quarter_block_length < 2; quarter_block_length++)
    { /* Length in quads */
        write_length =    cluster_length * (tu_bytes_per_cluster/4) +
                    block_length * 128 +
                    quarter_block_length * 32;
        if (!write_length)
            continue; /* only happens once when all lengths are zero */
        fill_count = 0;
        do_one_single_overwrite_test(fill_count, offset, write_length, segment);
        fill_count = offset + write_length;
        do_one_single_overwrite_test(fill_count, offset, write_length, segment);
        fill_count = offset + 2*write_length;
        do_one_single_overwrite_test(fill_count, offset, write_length, segment);
        fill_count = offset + write_length/2;
        do_one_single_overwrite_test(fill_count, offset, write_length, segment);
    }
    }
}

static void do_one_single_overwrite_test(dword fill_count, dword offset, dword write_length, dword segment)
{
dword blocks_free, blocks_total,ltemp2;

    display_transaction_write_parms(fill_count, offset, write_length, segment);
    /* Make sure the test file is erased from the volume
       enable failsafe and save free space value */
    fs_api_disable(test_driveid, TRUE);
    pc_unlink(tu_filename); /* Make sure it's deleted */
    fs_api_enable(test_driveid, TRUE);
    pc_blocks_free(test_driveid, &blocks_total, &blocks_free);

    /* Create a file containing segment+fill_count_dwords. write a pattern
       containing fill_count dword starting at offset "segment" in bytes */
    do_init_overwrite_test(segment, fill_count);
    /* Now verify an overwrite of write_length dwords at offset "offset" */
    do_one_overwrite_test(fill_count, offset, write_length, segment);

    pc_unlink(tu_filename); /* Make sure it's deleted */
    pc_blocks_free(test_driveid, &blocks_total, &ltemp2);
    ERTFS_ASSERT_TEST(ltemp2==blocks_free)
}

static int open_transaction_file(byte *filename,
            byte *transaction_buffer,
            dword transaction_buffer_size,
            word flags,
            dword allocation_policy)
{
int fd;
EFILEOPTIONS my_options;
    rtfs_memset(&my_options, 0, sizeof(my_options));
    my_options.transaction_buffer = transaction_buffer;
    my_options.transaction_buffer_size = transaction_buffer_size;
    my_options.allocation_policy = allocation_policy;
    my_options.min_clusters_per_allocation = 1;

    fd = pc_efilio_open(filename,flags ,(word)(PS_IWRITE | PS_IREAD),&my_options);
    return(fd);
}


/*
 Test using transactions

    Open transaction
    close file queue close file test
    write to beginning and queue write file test
    do this test:
        write 0 cluster (plus block offset and byte offsets)
        write 1 cluster (plus block offset and byte offsets)
        write n cluster (plus block offset and byte offsets)
        overwrite 0 cluster (plus block offset and byte offsets)
        overwrite 1 cluster (plus block offset and byte offsets)
        overwrite n cluster (plus block offset and byte offsets)
        overwrite n cluster (plus block offset and byte offsets)
     at offset 0 (plus block offset and byte offsets)
     at offset 1 (plus block offset and byte offsets)
     at offset n1 (plus block offset and byte offsets)

*/


static int do_reopen_test_use(
    byte *filename,
    dword allocation_policy,

    dword segment)
{
int fd;
dword nread;
    fd = open_transaction_file(filename,
                        transaction_buffer,
                        tu_bytes_per_cluster,
                        (word)(PO_BINARY|PO_RDWR),
                        allocation_policy);
    ERTFS_ASSERT_TEST(fd >= 0)
    if (segment)
    {
        if (!pc_efilio_read(fd, 0,segment,&nread))
        {
            ERTFS_ASSERT_TEST(rtfs_debug_zero());
        }
        ERTFS_ASSERT_TEST(segment==nread)
    }
    return(fd);
}

static void display_transaction_write_parms(dword fill_count, dword offset, dword write_length, dword segment)
{
static dword loop_count = 0;
    RTFS_PRINT_STRING_1((byte *)"Loop, Fill, offset, write_count, segment:", 0);
    RTFS_PRINT_LONG_1(++loop_count,0);
    RTFS_PRINT_STRING_1((byte *)",", 0);
    RTFS_PRINT_LONG_1(fill_count,0);
    RTFS_PRINT_STRING_1((byte *)",", 0);
    RTFS_PRINT_LONG_1(offset,0);
    RTFS_PRINT_STRING_1((byte *)",", 0);
    RTFS_PRINT_LONG_1(write_length,0);
    RTFS_PRINT_STRING_1((byte *)",", 0);
    RTFS_PRINT_LONG_1(segment,PRFLG_NL);
}


/* Test reading, writing and overwriting of transaction data. Simulate
   power interrupts by aborting disk mount sessions and verify that
   file overwrites are completed properly during system restore, */
static void do_one_overwrite_test(dword fill_count, dword offset, dword write_length, dword segment)
{
int fd;
dword nwritten,nread,n_to_read;

    fd = do_reopen_test_use(tu_filename, tu_allocation_policy, segment);
    if (fill_count)
    { /* Check that the file is unchanged from the aborted transaction write */
        if (!pro_test_read_n_dwords(fd, 0, fill_count, &nread, TRUE))
        {
            ERTFS_ASSERT_TEST(rtfs_debug_zero());
        }
    }
    pc_efilio_close(fd);
    fs_api_enable(test_driveid, TRUE);

	{	/* Make sure it's mounted */
    	dword blocks_total, blocks_free;
    	pc_blocks_free(test_driveid, &blocks_total, &blocks_free);
	}

	/* Temporarilly disable automatic flush operations */
    test_drive_structure()->du.drive_operating_policy |= DRVPOL_DISABLE_AUTOFAILSAFE;

    /* Reopen the file and skip "segment" bytes */
    fd = do_reopen_test_use(tu_filename,
                                tu_allocation_policy|PCE_TRANSACTION_FILE,

                                segment);
    ERTFS_ASSERT_TEST(fd >= 0)
    /* Now complete the test */
    /* Advance the file pointer by offset bytes and then
       overwrite a section of the file with an offset pattern */
    if (offset)
    {
        /* Seek foreward, don't write data */
        if (!pro_test_write_n_dwords(fd, 0, offset, &nwritten, FALSE, 0)
                        || nwritten != offset)
        {
            ERTFS_ASSERT_TEST(rtfs_debug_zero());
        }
    }
    /* Now fill the section with offset+100,offset+101 ... */
    if (!pro_test_write_n_dwords(fd, offset+100, write_length, &nwritten, TRUE, data_buffer)
                        || nwritten != write_length)
    {
        ERTFS_ASSERT_TEST(rtfs_debug_zero());
    }
    /* Now abort failsafe, abort the mount and verify that the
       overwritten area is unchanged */
    test_drive_structure()->du.drive_operating_policy &= ~DRVPOL_DISABLE_AUTOFAILSAFE;
    fs_api_disable(test_driveid, TRUE);
    pc_efilio_close(fd); /* Release the File structure, won't flush
                            since mount was aborted */

    /* reopen the file and if there is a leading segment skip */
    fd = do_reopen_test_use(tu_filename, tu_allocation_policy, segment);
    if (fill_count)
    { /* Check that the file is unchanged from the aborted transaction write */
        if (!pro_test_read_n_dwords(fd, 0, fill_count, &nread, TRUE))
        {
            ERTFS_ASSERT_TEST(rtfs_debug_zero());
        }
    }
    else
    { /* The file should be zero sized because we started empty and never updated the volume */
        /* Single byte read should fail */
        if (!pc_efilio_read(fd, (byte*)0, 1, &nread) || (nread != 0))
        {
            ERTFS_ASSERT_TEST(rtfs_debug_zero());
        }
    }
    pc_efilio_close(fd);
    /* Now FAILSAFE restore and reopen file, verify the region is changed */
    if (!fs_api_restore(test_driveid))
    {
        ERTFS_ASSERT_TEST(rtfs_debug_zero());
    }
    /* reopen the file not in transaction mode and if there are leading
       segment and offset, skip */
    fd = do_reopen_test_use(tu_filename, tu_allocation_policy, segment);
    if (offset)
    {
    dword n_to_skip;
        if (fill_count > offset)
        { /* If offset should have a pattern verify it */
            n_to_read = offset;
            n_to_skip = 0;
        }
        else
        { /* read as much pattern as is present */
            n_to_read = fill_count;
            n_to_skip = fill_count - offset; /* Skip the rest */
        }
        if (n_to_read)
            if (!pro_test_read_n_dwords(fd, 0, n_to_read, &nread, TRUE))
            {
                ERTFS_ASSERT_TEST(rtfs_debug_zero());
            }
          if (n_to_skip)
            if (!pro_test_read_n_dwords(fd, 0, n_to_skip, &nread, FALSE))
            {
                ERTFS_ASSERT_TEST(rtfs_debug_zero());
            }
    }
    /* Now verify that our overwrite worked */
    if (!pro_test_read_n_dwords(fd, offset+100, write_length, &nread, TRUE))
    {
        ERTFS_ASSERT_TEST(rtfs_debug_zero());
    }
    /* Now if there was data beyond our overwrite verify it is what it should be */
    if (fill_count < (offset+write_length))
      n_to_read = 0;
    else
      n_to_read = fill_count - (offset+write_length);
    if (n_to_read)
    {
        if (!pro_test_read_n_dwords(fd, (offset+write_length), n_to_read, &nread, TRUE))
        {
            ERTFS_ASSERT_TEST(rtfs_debug_zero());
        }
    }
    pc_efilio_close(fd);
    /* Now put the file pattern back to what it was so we can continue
       testing on the file if we wish to */
    fd = do_reopen_test_use(tu_filename,
                                tu_allocation_policy,
                                segment);
    ERTFS_ASSERT_TEST(fd >= 0)
    /* Advance the file pointer by offset bytes and then
       overwrite a section of the file with the original pattern */
    if (offset)
    {
        /* Seek foreward, don't write data */
        if (!pro_test_write_n_dwords(fd, 0, offset, &nwritten, FALSE, 0)
                        || nwritten != offset)
        {
            ERTFS_ASSERT_TEST(rtfs_debug_zero());
        }
    }
    /* Now fill the section with offset,offset+1 ... */
    if (!pro_test_write_n_dwords(fd, offset, write_length, &nwritten, TRUE, data_buffer)
                        || nwritten != write_length)
    {
        ERTFS_ASSERT_TEST(rtfs_debug_zero());
    }
    pc_efilio_close(fd);
}

/* Test reading, writing and overwriting of transaction data. Simulate
   power interrupts by aborting disk mount sessions and verify that
   file overwrites are completed properly during system restore, */
static void do_init_overwrite_test(dword segment, dword fill_count)

{
int fd;
dword nwritten;

    fs_api_disable(test_driveid, TRUE);
    fd = open_transaction_file(tu_filename,
                  transaction_buffer,
                  tu_bytes_per_cluster,
                  (word)(PO_BINARY|PO_RDWR|PO_CREAT),
                  tu_allocation_policy);
    ERTFS_ASSERT_TEST(fd >= 0)
    /* If requested fill leading bytes of the file so we perform our tests
       at or near segmwent boundaries. */
    if (segment)
    {
        if (!pc_efilio_write(fd, 0,segment,&nwritten) || (segment!=nwritten))
        {
            ERTFS_ASSERT_TEST(rtfs_debug_zero());
        }
    }
     /* If requested write a pattern into the file */
    if (fill_count)
    {
        if (!pro_test_write_n_dwords(fd, 0, fill_count, &nwritten, TRUE, data_buffer)
                        || nwritten != fill_count)
        {
            ERTFS_ASSERT_TEST(rtfs_debug_zero());
        }
    }
    /* Now close the file, enable Failsafe and reopen with the
       transaction atribute, skip "segment" bytes if not zero */
    pc_efilio_close(fd);
}

static void  test_kill_transaction_file(int fd,byte *filename)
{
    if (!pc_efilio_close(fd))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (filename)
    {
        if (!pc_unlink(filename))
           {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    }
}

static int test_open_transaction_file(byte *filename,
            byte *transaction_buffer,
            dword transaction_buffer_size,
            word flags,
            dword allocation_policy,
            int expected_errno,
            BOOLEAN do_close)
{
int i,fd;

    fd = -1;
    if (expected_errno)
    {
        for (i = 0; i < 100; i++)
        {
            fd = open_transaction_file(filename,
                        transaction_buffer, transaction_buffer_size,
                        flags, allocation_policy);
            ERTFS_ASSERT_TEST(fd == -1)
            ERTFS_ASSERT_TEST(get_errno() == expected_errno)

        }
    }
    else
    {
            fd = open_transaction_file(filename,
                        transaction_buffer, transaction_buffer_size,
                        flags, allocation_policy);
            ERTFS_ASSERT_TEST(fd >= 0)
            if (do_close)
            {
                if (!pc_efilio_close(fd))
                    {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
                if (!pc_unlink(filename))
                    {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
            }
    }
    return(fd);
}

static void do_test_open_transaction_file(dword is_meta_64)
{
    dword cluster_size, sector_size_bytes;
    int fd1,fd2;

    sector_size_bytes = (dword) pc_sector_size((byte *)test_driveid);
    if (!sector_size_bytes )
    {
        PRO_TEST_ANNOUNCE("pc_sector_size() failed");
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    }

    cluster_size = tu_bytes_per_cluster/sector_size_bytes;

    /*
<TEST>   Test argument handling for pc_efilio_open with transaction support for 32 bit and 64 bit files.
<TEST>        . With Failsafe disabled
<TEST>        . With no transaction buffer but legal size
<TEST>        . With transaction buffer but illegal size
<TEST>        . Everything legal but buffered
<TEST>        . Test illegal options for transaction files
<TEST>            PCE_KEEP_PREALLOC
<TEST>            PCE_CIRCULAR_FILE
<TEST>            PCE_CIRCULAR_BUFFER
<TEST>            PCE_REMAP_FILE
<TEST>            PCE_TEMP_FILE
<TEST>        . Open twice once transactional once not
<TEST>        . Open twice once not transactional once transactional
    */
    /* We're probably not in failsafe mode now but just make sure */
    fs_api_disable(test_driveid, TRUE);
    /* Do an open with arguments legal except Failsafe is not enabled */
    test_open_transaction_file(
            TRANSACTION_FILE,
            transaction_buffer,
            cluster_size,
            (word)(PO_BINARY|PO_RDWR|PO_CREAT),
            is_meta_64|PCE_TRANSACTION_FILE,
            PEINVALIDPARMS, TRUE);

    /* Now with Failsafe should work */
    fs_api_enable(test_driveid, TRUE);
    test_open_transaction_file(
            TRANSACTION_FILE,
            transaction_buffer,
            cluster_size,
            (word)(PO_BINARY|PO_RDWR|PO_CREAT),
            is_meta_64|PCE_TRANSACTION_FILE,
            0, TRUE);

    /* Now test various combinations that should fail */
    /*    Note: Fail open test retries hundred times to detect leaks */
    /* No transaction buffer */
    test_open_transaction_file(
            TRANSACTION_FILE,
            0,
            cluster_size,
            (word)(PO_BINARY|PO_RDWR|PO_CREAT),
            is_meta_64|PCE_TRANSACTION_FILE,
            PEINVALIDPARMS, TRUE);

    /* Bad transaction buffer size */
    test_open_transaction_file(
            TRANSACTION_FILE,
            transaction_buffer,
            cluster_size/2,
            (word)(PO_BINARY|PO_RDWR|PO_CREAT),
            is_meta_64|PCE_TRANSACTION_FILE,
            PEINVALIDPARMS, TRUE);

    /* Buffered */
    test_open_transaction_file(
            TRANSACTION_FILE,
            transaction_buffer,
            cluster_size,
            (word)(PO_BINARY|PO_RDWR|PO_CREAT|PO_BUFFERED),
            is_meta_64|PCE_TRANSACTION_FILE,
            PEINVALIDPARMS, TRUE);

    /* Autoflush */
    test_open_transaction_file(
            TRANSACTION_FILE,
            transaction_buffer,
            cluster_size,
            (word)(PO_BINARY|PO_RDWR|PO_CREAT|PO_AFLUSH),
            is_meta_64|PCE_TRANSACTION_FILE,
            PEINVALIDPARMS, TRUE);

    /* PCE_KEEP_PREALLOC */
    test_open_transaction_file(
            TRANSACTION_FILE,
            transaction_buffer,
            cluster_size,
            (word)(PO_BINARY|PO_RDWR|PO_CREAT),
            is_meta_64|PCE_TRANSACTION_FILE|PCE_KEEP_PREALLOC,
            PEINVALIDPARMS, TRUE);
    /* PCE_CIRCULAR_FILE */
    test_open_transaction_file(
            TRANSACTION_FILE,
            transaction_buffer,
            cluster_size,
            (word)(PO_BINARY|PO_RDWR|PO_CREAT),
            is_meta_64|PCE_TRANSACTION_FILE|PCE_CIRCULAR_FILE,
            PEINVALIDPARMS, TRUE);
    /* PCE_CIRCULAR_BUFFER */
    test_open_transaction_file(
            TRANSACTION_FILE,
            transaction_buffer,
            cluster_size,
            (word)(PO_BINARY|PO_RDWR|PO_CREAT),
            is_meta_64|PCE_TRANSACTION_FILE|PCE_CIRCULAR_BUFFER,
            PEINVALIDPARMS, TRUE);
    /* PCE_REMAP_FILE */
    test_open_transaction_file(
            TRANSACTION_FILE,
            transaction_buffer,
            cluster_size,
            (word)(PO_BINARY|PO_RDWR|PO_CREAT),
            is_meta_64|PCE_TRANSACTION_FILE|PCE_REMAP_FILE,
            PEINVALIDPARMS, TRUE);
    /* PCE_TEMP_FILE */
    test_open_transaction_file(
            TRANSACTION_FILE,
            transaction_buffer,
            cluster_size,
            (word)(PO_BINARY|PO_RDWR|PO_CREAT),
            is_meta_64|PCE_TRANSACTION_FILE|PCE_TEMP_FILE,
            PEINVALIDPARMS, TRUE);
    /* Open twice transactional */
    fd1 = test_open_transaction_file(
            TRANSACTION_FILE,
            transaction_buffer,
            cluster_size,
            (word)(PO_BINARY|PO_RDWR|PO_CREAT),
            is_meta_64|PCE_TRANSACTION_FILE,
            0, FALSE);
    test_open_transaction_file(
            TRANSACTION_FILE,
            transaction_buffer,
            cluster_size,
            (word)(PO_BINARY|PO_RDWR|PO_CREAT),
            is_meta_64|PCE_TRANSACTION_FILE,
            PESHARE, TRUE);
     test_kill_transaction_file(fd1,TRANSACTION_FILE);
    /* Open twice once transactional once for read */
    fd1 = test_open_transaction_file(
            TRANSACTION_FILE,
            transaction_buffer,
            cluster_size,
            (word)(PO_BINARY|PO_RDWR|PO_CREAT),
            is_meta_64|PCE_TRANSACTION_FILE,
            0, FALSE);
    fd2 = test_open_transaction_file(
            TRANSACTION_FILE,
            transaction_buffer,
            cluster_size,
            (word)(PO_BINARY|PO_RDONLY),
            is_meta_64,
            0, FALSE);
     test_kill_transaction_file(fd1,0);
     test_kill_transaction_file(fd2,TRANSACTION_FILE);

    /* Open twice once transactional once for write  */
    fd1 = test_open_transaction_file(
            TRANSACTION_FILE,
            transaction_buffer,
            cluster_size,
            (word)(PO_BINARY|PO_RDWR|PO_CREAT),
            is_meta_64|PCE_TRANSACTION_FILE,
            0, FALSE);
    test_open_transaction_file(
            TRANSACTION_FILE,
            transaction_buffer,
            cluster_size,
            (word)(PO_BINARY|PO_RDWR),
            is_meta_64,
            PESHARE, FALSE);
     test_kill_transaction_file(fd1,TRANSACTION_FILE);

    /* Open twice once non-transactional for write, transactional */
    fd1 = test_open_transaction_file(
            TRANSACTION_FILE,
            transaction_buffer,
            cluster_size,
            (word)(PO_BINARY|PO_RDWR|PO_CREAT),
            is_meta_64,
            0, FALSE);
    test_open_transaction_file(
            TRANSACTION_FILE,
            transaction_buffer,
            cluster_size,
            (word)(PO_BINARY|PO_RDWR|PO_CREAT),
            is_meta_64|PCE_TRANSACTION_FILE,
            PESHARE, TRUE);
    /* close but don't delete */
     test_kill_transaction_file(fd1,0);
    /* Open twice once non-transactional for read, transactional */
    fd1 = test_open_transaction_file(
            TRANSACTION_FILE,
            transaction_buffer,
            cluster_size,
            (word)(PO_BINARY|PO_RDONLY|PO_CREAT),
            is_meta_64,
            0, FALSE);
    fd2 = test_open_transaction_file(
            TRANSACTION_FILE,
            transaction_buffer,
            cluster_size,
            (word)(PO_BINARY|PO_RDWR|PO_CREAT),
            is_meta_64|PCE_TRANSACTION_FILE,
            0, FALSE);
     test_kill_transaction_file(fd1,0);
     test_kill_transaction_file(fd2,TRANSACTION_FILE);
}

#endif /* (!INCLUDE_DEBUG_TEST_CODE && FAILSAFE) else.. */
#endif /* Exclude from build if read only */
