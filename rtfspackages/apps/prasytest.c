/* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* PRASYTEST.C - Test asynchronous features of RTFS Pro Plus */
/*
<TEST>  Test File:   rtfspackages/apps/prasyest.c
<TEST>
<TEST>   Procedure:
<TEST>   Description: RTFS Pro Plus asynchronous feature set regression test suite.
<TEST>      Test suite performs tests to verify the correct operation of asynchronous features.
<TEST>      Test suite performs tests to verify the correct operation Failsafe features.
<TEST>
*/
#include "rtfs.h"
#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */

#if (!(INCLUDE_DEBUG_TEST_CODE && INCLUDE_ASYNCRONOUS_API))
void test_asynchronous_api(byte *pdriveid)
{
    RTFS_ARGSUSED_PVOID((void *)pdriveid);
    RTFS_PRINT_STRING_1((byte *)"Build with INCLUDE_DEBUG_TEST_CODE and INCLUDE_ASYNCRONOUS_API to run tests", PRFLG_NL);
}
#else
#include "protests.h"


#define VERBOSE_MODE 0

#if (INCLUDE_FAILSAFE_CODE)
static void   asytest_drive_async_complete_cb(int driveno, int operation,int success);
static void failsafe_test_asynchronous_api(void);
#endif

static dword asytest_drive_operating_policy;
static BOOLEAN asytest_failsafe_enabled;
static void test_asynchronous_api_calls(void);
static void _init_drive_for_asynchronous_test(dword drive_operating_policy, BOOLEAN use_failsafe,BOOLEAN do_mount);

static dword gl_global_restores = 0;
extern DRIVE_INFO test_drive_infostruct;

static void random_test_asynchronous_api(void);


/*
<TEST>     Procedure: test_asynchronous api
<TEST>      Verifies proper operation of asynchronous API
*/


void test_asynchronous_api(byte *pdriveid)
{
struct save_mount_context saved_mount_parms;
DDRIVE *ptest_drive;


    if (!set_test_drive(pdriveid))
        return;

    data_buffer = 0;
    pro_test_alloc_data_buffer();

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


    /* Configure drive operating conditions. This configuration is applied by:
            init_drive_for_asynchronous_test()
       Which is called multiple times with differing operating policies*/
    asytest_drive_operating_policy = 0; /* Defaults */
#if (INCLUDE_FAILSAFE_CODE)
	prtfs_cfg->test_drive_async_complete_cb = asytest_drive_async_complete_cb;
#endif
    /* Test specific async calls */
    test_asynchronous_api_calls();
#if (INCLUDE_FAILSAFE_CODE)
    failsafe_test_asynchronous_api();
#endif

    pc_dskfree(test_drivenumber);
    pro_test_free_data_buffer();

    /* Free the current configuration if we changed it */
    pro_test_release_mount_parameters(ptest_drive);
    /* Restore the original configuration */
    pro_test_restore_mount_context(ptest_drive,&saved_mount_parms);
#if (INCLUDE_FAILSAFE_CODE)
	prtfs_cfg->test_drive_async_complete_cb = 0;
#endif

}


static int test_async_reopen_file(byte *filename, dword options);
static void test_async_verify_drive_in_progress(void);
static void test_async_verify_file_in_progress(int fd);
static void test_async_file_flush(byte *filename, dword options,dword test_file_size_dw);
static void test_async_file_reopen(byte *filename, dword test_file_size_dw);
static void test_async_file_delete(byte *filename);
static void test_async_disk_flush(void);
static void mount_drive_for_asynchronous_test(byte *drivename);

static void test_asynchronous_api_calls(void)
{
BOOLEAN do_async_mount_test = TRUE;
    /* Close the drive and reopen asyncronously Turn off
       automount, autoflush */
    if (!pc_diskflush((byte *) test_driveid))
    {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    if (test_if_fat12_or_exfat())
		do_async_mount_test=FALSE;

    pc_dskfree(test_drivenumber);
    if (do_async_mount_test)
    {
/*
<TEST>      Verify proper operation of asynchronous volume mount and freespace scan.
<TEST>      Verify proper operation of signaling mechanism that an asynchronous mount is required.
*/
        PRO_TEST_ANNOUNCE("Verfiying DRVPOL_DISABLE_AUTOMOUNT policy ");
        _init_drive_for_asynchronous_test(DRVPOL_DISABLE_AUTOMOUNT, FALSE, FALSE);
        /* Mkdir should fail and report PENOTMOUNTED */
        if (pc_mkdir((byte *) "SHOULD_NOT_WORK") != FALSE)
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        ERTFS_ASSERT_TEST(get_errno() == PENOTMOUNTED)
        /* Now manually complete the mount */
        {
            int status;
            dword num_passes;
            num_passes = 0;

            do {
                status = pc_async_continue(test_drivenumber,DRV_ASYNC_IDLE, 1);

                if (num_passes == 0)
				{
                    PRO_TEST_ANNOUNCE("Testing PEEINPROGRESS error during disk mount");
                    if (status == PC_ASYNC_COMPLETE)
                    {
                    	PRO_TEST_ANNOUNCE("Mount completed in one pass, reduce user buffer size to perform this test");
                    }
                    else
                    { /* test only once per boot */
                    	test_async_verify_drive_in_progress();
                    }
				}
                num_passes += 1;
            }  while (status == PC_ASYNC_CONTINUE);
            ERTFS_ASSERT_TEST(status == PC_ASYNC_COMPLETE)
            pro_test_print_dword("Passes required for disk mount to complete:", num_passes,PRFLG_NL);
            pc_dskfree(test_drivenumber);
        }
    }

    /* Turn off autoflush, failsafe so we can moitor operations */
    /* Disable Failsafe, etc and run checkdisk */
    _init_drive_for_asynchronous_test(DRVPOL_DISABLE_AUTOFLUSH, FALSE, FALSE);
    if (do_async_mount_test)
    {
        PRO_TEST_ANNOUNCE("Testing pc_diskio_async_mount_start Asynchronous disk mount ");
        {
            int driveno, status;
            dword num_passes;

            driveno = pc_diskio_async_mount_start((byte *) test_driveid);
            ERTFS_ASSERT_TEST(driveno >= 0)
            num_passes = 0;
            do {
                status = pc_async_continue(driveno,DRV_ASYNC_IDLE, 1);

                if (num_passes == 0)
				{
                    PRO_TEST_ANNOUNCE("Testing PEEINPROGRESS error during disk mount");
                    if (status == PC_ASYNC_COMPLETE)
                    {
                    	PRO_TEST_ANNOUNCE("Mount completed in one pass, reduce user buffer size to perform this test");
                    }
                    else
                    { /* test only once per boot */
                    	test_async_verify_drive_in_progress();
                    }
				}
                num_passes += 1;
            }  while (status == PC_ASYNC_CONTINUE);
            ERTFS_ASSERT_TEST(status == PC_ASYNC_COMPLETE)
            pro_test_print_dword("Passes required for disk mount to complete:", num_passes,PRFLG_NL);
        }
    }

/* <TEST>      Verify proper operation of asynchronous file open. */
    PRO_TEST_ANNOUNCE("Testing Asynchronous reopen ");

    pro_test_efile_fill(TEST_FILE32, 0,FILESIZE_32, FALSE, TRUE);

    test_async_file_reopen(TEST_FILE32,FILESIZE_32);

/* <TEST>      Verify proper operation of asynchronous file delete. */
    PRO_TEST_ANNOUNCE("Testing Asynchronous delete ");
    test_async_file_delete(TEST_FILE32);

/* <TEST>      Verify proper operation of asynchronous file flush. */
    PRO_TEST_ANNOUNCE("Testing Asynchronous File flush ");
    test_async_file_flush(TEST_FILE32,0,FILESIZE_32);

/* <TEST>      Verify proper operation of asynchronous disk flush. */
    PRO_TEST_ANNOUNCE("Testing Asynchronous Disk flush ");
    test_async_disk_flush();

    ERTFS_ASSERT_TEST(pc_diskflush((byte *) test_driveid))
    /* Restore operating parameters */
    _init_drive_for_asynchronous_test(0, FALSE, FALSE);
   pc_unlink(SMALL_FILE);
   pc_unlink(TEST_FILE32);
}

static void test_async_file_flush(byte *filename, dword options,dword test_file_size_dw)
{
    int fd2,fd,status;
    dword  pass_count,ntransfered_dw;
    EFILEOPTIONS my_options;

    rtfs_memset(&my_options, 0, sizeof(my_options));
    my_options.allocation_policy = options;

    PRO_TEST_ANNOUNCE("Testing Asynch file flush");
    fd = pro_test_efile_create(filename, options, 0);

    ERTFS_ASSERT_TEST(fd >= 0)
    pro_test_write_n_dwords(fd, 0, test_file_size_dw, &ntransfered_dw, TRUE, data_buffer);
    ERTFS_ASSERT_TEST(test_file_size_dw == ntransfered_dw)

    status = pc_efilio_async_flush_start(fd);
    ERTFS_ASSERT_TEST(status == PC_ASYNC_CONTINUE)

    PRO_TEST_ANNOUNCE("Verify fd ops report PEEINPROGRESS during async flush");
    rtfs_memset(&my_options, 0, sizeof(my_options));
    my_options.allocation_policy = options;
    fd2 = pc_efilio_open(filename,(word)(PO_BINARY|PO_RDWR),(word)(PS_IWRITE | PS_IREAD)
           ,&my_options);
    ERTFS_ASSERT_TEST(fd2<0)
    ERTFS_ASSERT_TEST(get_errno() == PEEINPROGRESS)
    /* Verify all fd ops fail with PEEINPROGRESS */
    test_async_verify_file_in_progress(fd);


    pass_count = 0;
    do
    {
        pass_count += 1;
        status = pc_async_continue(test_drivenumber,DRV_ASYNC_IDLE, 1);
    }  while (status == PC_ASYNC_CONTINUE);
    ERTFS_ASSERT_TEST(status == PC_ASYNC_COMPLETE)
    pro_test_print_dword("Passes required for file flush to complete:", pass_count,PRFLG_NL);
    pc_efilio_close(fd);
}

static void test_async_file_reopen(byte *filename, dword test_file_size_dw)
{
    int fd;
    dword  ntransfered_dw;

    fd = test_async_reopen_file(filename,0);
    ERTFS_ASSERT_TEST(fd >= 0)

    /* Read it back */
    pro_test_read_n_dwords(fd, 0, test_file_size_dw, &ntransfered_dw, FALSE);
    ERTFS_ASSERT_TEST(test_file_size_dw == ntransfered_dw)
    pc_efilio_close(fd);
}

static void test_async_file_delete(byte *filename)
{
    dword pass_count;
    int status,fd;

    pass_count = 0;
    fd = pc_efilio_async_unlink_start(filename);
    ERTFS_ASSERT_TEST(fd >= 0)
    do
    {
        pass_count += 1;
        status = pc_async_continue(test_drivenumber,DRV_ASYNC_IDLE, 1);
    }  while (status == PC_ASYNC_CONTINUE);
    ERTFS_ASSERT_TEST(status == PC_ASYNC_COMPLETE)
    pro_test_print_dword("Passes required for file delete to complete:", pass_count,PRFLG_NL);
}

static void test_async_disk_flush(void)
{
    int fd;
    dword ntransfered_dw;

    /* Fill a small file to dirty up the fat cache */
    fd = pro_test_efile_create(SMALL_FILE,0,0);
    ERTFS_ASSERT_TEST(fd>=0)
    pro_test_write_n_dwords(fd, 0, SMALL_FILE_SIZE, &ntransfered_dw, TRUE, data_buffer);
    ERTFS_ASSERT_TEST(SMALL_FILE_SIZE == ntransfered_dw)
    pc_efilio_close(fd);
    /* Now flush the disk */
    {
    dword num_passes;
    int status;
        num_passes = 1;
        status  = pc_diskio_async_flush_start(test_driveid);
        while (status == PC_ASYNC_CONTINUE)
        {
            status = pc_async_continue(test_drivenumber, DRV_ASYNC_IDLE, 1);
            num_passes += 1;
        }
        ERTFS_ASSERT_TEST(status == PC_ASYNC_COMPLETE)
        pro_test_print_dword("Passes required for disk flush to comlete:", num_passes,PRFLG_NL);
    }
    /* Force a remount and read back the file contents */
    pc_dskfree(test_drivenumber);

    _init_drive_for_asynchronous_test(0, FALSE, TRUE);
    /* Now reopen the small file and verify that the contents are intact
      after remount */
    fd = test_async_reopen_file(SMALL_FILE,0);
    ERTFS_ASSERT_TEST(fd>=0)
    /* Read it back */
    pro_test_read_n_dwords(fd, 0, SMALL_FILE_SIZE, &ntransfered_dw, TRUE);
    ERTFS_ASSERT_TEST(SMALL_FILE_SIZE == ntransfered_dw)
    pc_efilio_close(fd);


    PRO_TEST_ANNOUNCE("Verifying flush operation");
    {
    dword num_passes = 1;
    int status;
        status = pc_diskio_async_flush_start(test_driveid);
        while (status == PC_ASYNC_CONTINUE)
        {
            num_passes += 1;
            status = pc_async_continue(test_drivenumber, DRV_ASYNC_IDLE, 1);
        }
        while (status == PC_ASYNC_CONTINUE);
        ERTFS_ASSERT_TEST(status == PC_ASYNC_COMPLETE)
        pro_test_print_dword("Passes required for disk flush to comlete:", num_passes,PRFLG_NL);

    }
    /* Delete the file and flush the disk again */
    pc_unlink(SMALL_FILE);

    PRO_TEST_ANNOUNCE("Verifying flush operation");
    {

    dword num_passes = 1;
    int status;
        status = pc_diskio_async_flush_start(test_driveid);
        while (status == PC_ASYNC_CONTINUE)
        {
            num_passes += 1;
            status = pc_async_continue(test_drivenumber, DRV_ASYNC_IDLE, 1);
        }

        ERTFS_ASSERT_TEST(status == PC_ASYNC_COMPLETE)
        pro_test_print_dword("Passes required for disk flush to comlete:", num_passes,PRFLG_NL);
   }
}


static int test_async_reopen_file(byte *filename, dword options)
{
    int fd;
    EFILEOPTIONS my_options;

    rtfs_memset(&my_options, 0, sizeof(my_options));
    options |= PCE_ASYNC_OPEN;
    my_options.allocation_policy = options;

    fd = pc_efilio_open(filename,(word)(PO_BINARY|PO_RDWR),(word)(PS_IWRITE | PS_IREAD)
                         ,&my_options);
    ERTFS_ASSERT_TEST(fd>=0)
    {
    int pass_count, status;
    int fd2;


        /* Verify that accesses fail */
        /* lseek, read, write, close */
        PRO_TEST_ANNOUNCE("Verify fd ops report PEEINPROGRESS during async re-open");
        fd2 = pc_efilio_open(filename,(word)(PO_BINARY|PO_RDWR),(word)(PS_IWRITE | PS_IREAD)
                         ,&my_options);
        ERTFS_ASSERT_TEST(fd2<0)
        ERTFS_ASSERT_TEST(get_errno() == PEEINPROGRESS)

        /* Verify all fd ops fail with PEEINPROGRESS */
        test_async_verify_file_in_progress(fd);

        pass_count = 0;
        do
        {
            pass_count += 1;
            status = pc_async_continue(test_drivenumber,DRV_ASYNC_IDLE, 1);
        }  while (status == PC_ASYNC_CONTINUE);
        ERTFS_ASSERT_TEST(status == PC_ASYNC_COMPLETE)
        pro_test_print_dword("Passes required for async open to complete", pass_count,PRFLG_NL);
    }
    return(fd);
}

/* <TEST>      Verify proper error handling and errno generation when drive API calls are made while an asynchronous mount is in progress */
static void test_async_verify_drive_in_progress(void)
{
    /* Mkdir should fail and report PEEINPROGRESS */
    if (pc_mkdir((byte *) "SHOULD_NOT_WORK") != FALSE)
    { ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    ERTFS_ASSERT_TEST(get_errno() == PEEINPROGRESS)
}

/* <TEST>      Verify proper error handling and errno generation when file API calls are made while an asynchronous mount is in progress */
static void test_async_verify_file_in_progress(int fd)
{
dword ntransferred,ltemp_hi, ltemp_lo;

   /* Verify that accesses fail */
   /* lseek, read, write, close */
   if (pc_efilio_write(fd, 0, 1, &ntransferred) != FALSE)
   { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
   ERTFS_ASSERT_TEST(get_errno() == PEEINPROGRESS)
   if (pc_efilio_read(fd, 0, 1, &ntransferred) != FALSE)
   { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
   ERTFS_ASSERT_TEST(get_errno() == PEEINPROGRESS)

   if (pc_efilio_lseek(fd, 0, 0, PSEEK_SET, &ltemp_hi, &ltemp_lo) != FALSE)
   { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
   ERTFS_ASSERT_TEST(get_errno() == PEEINPROGRESS)
   if (pc_efilio_close(fd) != FALSE)
   { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
   ERTFS_ASSERT_TEST(get_errno() == PEEINPROGRESS)
   if (pc_efilio_async_flush_start(fd) != PC_ASYNC_ERROR)
   { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
   ERTFS_ASSERT_TEST(get_errno() == PEEINPROGRESS)
   if (pc_efilio_async_close_start(fd) != PC_ASYNC_ERROR)
   { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
   ERTFS_ASSERT_TEST(get_errno() == PEEINPROGRESS)
}



static void _init_drive_for_asynchronous_test(dword drive_operating_policy, BOOLEAN use_failsafe,BOOLEAN do_mount)
{
dword sector_size_bytes;
DDRIVE *ptest_drive;
struct rtfs_volume_resource_reply asytest_dsk_config;

    RTFS_ARGSUSED_INT(use_failsafe);
	/* Get the drive structure */
    ptest_drive = test_drive_structure();

	sector_size_bytes = test_drive_infostruct.sector_size;

	/* Other handy fields
	test_drive_infostruct.erase_block_size;
	test_drive_infostruct.cluster_size;
	test_drive_infostruct.total_clusters;
	test_drive_infostruct.free_clusters;
	test_drive_infostruct.fat_entry_size;
	test_drive_infostruct.drive_operating_policy;

   */
    pc_dskfree(test_drivenumber);
	/* We never want auto failsafe mode, we want to control it */
    drive_operating_policy |= DRVPOL_DISABLE_AUTOFAILSAFE;
    asytest_dsk_config.drive_operating_policy = drive_operating_policy;

    /* Note: Use larger blockmap than default, this is required if the free manager is disabled and FAT buffers are
    modified during cluster allocations, if the free manager is enabled clusters are allocated from
    the free cluster pool, some tests rely on this, allocating large numbers of clusters during write and
    anticipate a journal full condition during file flush */

    pro_test_set_mount_parameters(ptest_drive, &asytest_dsk_config,
		drive_operating_policy, /* dword drive_operating_policy, */
		sector_size_bytes,     /* dword sector_size_bytes */
		32,     /* dword device_sector_size_sectors */
		32, /* dword n_sector_buffers, */
		 2, /* dword n_fat_buffers, */
		 1, /* dword fat_buffer_page_size_sectors, */
		 8192,/* dword fsjournal_n_blockmaps, */
		1, /*  index_buffer_size */
		DEFAULT_RESTORE_BUFFER_SIZE /* dword fsrestore_buffer_size_sectors) */
		);

    if (do_mount)
    {
        mount_drive_for_asynchronous_test((byte *) test_driveid);
    }
#if (INCLUDE_FAILSAFE_CODE)
    if (use_failsafe)
    {
        if (!fs_api_enable((byte *) test_driveid, TRUE))
        {
            ERTFS_ASSERT_TEST(rtfs_debug_zero())
        }
    }
#endif
}
static void mount_drive_for_asynchronous_test(byte *drivename)
{
int driveno,status;
   driveno = pc_diskio_async_mount_start(drivename);
   ERTFS_ASSERT_TEST(driveno!=-1)
   status = pc_async_continue(driveno,DRV_ASYNC_IDLE, 0);
   ERTFS_ASSERT_TEST(status == PC_ASYNC_COMPLETE)
}

#if (INCLUDE_FAILSAFE_CODE)

/* Used by random async test */
#define MAX_CHECKHPOINTS 16
PRO_TEST_CHECKPOINT check_point_watch[MAX_CHECKHPOINTS];
PRO_TEST_CHECKPOINT check_point_current, check_point_initial, check_point_populated;
#define ASYNC_TEST_ROOT_DIR (byte *) "\\async_test_root"
#define ASYNC_TEST_DIR (byte *) "\\async_test_directory"
#define ASYNC_TEST_FULLPATH (byte *) "\\async_test_root\\async_test_directory"

/*
<TEST>
<TEST>    Procedure: Failsafe restore coverage test - This test verifies proper operation of failsafe during every possible block IO operation.
<TEST>    It test the operation of Failsafe using compiled in DEBUG diagnostics that simulate disk read and write errors.
<TEST>    To provide a realistic simulation, simulated write errors overwrite the block they are writing to wich nonsense data.
<TEST>
<TEST>    Enable Failsafe
<TEST>    Perform a series of operations like mkdir, open, write and close to populate a subdirectory of a volume
<TEST>    Uses internal diagnostics to record the block number and directions of all blocks transfered to/from the disk.
<TEST>    Uses internal diagnostics to store watchpoints of the sate of the volume after API calls.
<TEST>    Remove the subdirectory
<TEST>
<TEST>    Perform the same series of operations but simulate read and write errors and use Failsafe to restore the volume
<TEST>        Use internal diagnostics to simulate an IO error when a specific block number, block count, and direction is specified
<TEST>        Verify that when errors are simulated after a journal flush, the volume is restorable
<TEST>        Verify that the restored state matches the proper calibrated watchpoint.
<TEST>        Verify that when errors are simulated before a journal flush, the session is not restorable
<TEST>        Verify that when errors are simulated before a journal flush, the volume is un-change
<TEST>        Repeat this loop for every block number and direction that was recorded during the calibration phase
*/



struct one_failure_mode_test {
    int failure_mode;
    /* See blocks_
        reference_io_stats.blocks_read[failure_mode]
        reference_io_stats.blocks_written[failure_mode]
        for count of block transfers during this phase */
    dword force_next_failure;
    dword forced_failures;
};

struct async_test_control {
    int current_test;
    BOOLEAN test_completed;
//    BOOLEAN flush_journal_frequently;
    int num_watchpoints_executed;
    int num_watchpoints;
    int num_watchpoints_flushed;
    int num_watchpoints_restored;
    struct one_failure_mode_test failure_tests[RTFS_DEBUG_IO_J_RANGES];
    IO_RUNTIME_STATS reference_io_stats;
};
static struct async_test_control async_test_master;


static char *test_failure_mode_names[RTFS_DEBUG_IO_J_RANGES] = {
"RTFS_DEBUG_IO_NOT_USED             ",
"RTFS_DEBUG_IO_J_ROOT_WRITE         ",
"RTFS_DEBUG_IO_J_INDEX_OPEN         ",
"RTFS_DEBUG_IO_J_INDEX_FLUSH        ",
"RTFS_DEBUG_IO_J_INDEX_CLOSE        ",
"RTFS_DEBUG_IO_J_INDEX_CLOSE_EMPTY  ",
"RTFS_DEBUG_IO_J_REMAP_WRITE        ",
"RTFS_DEBUG_IO_J_REMAP_READ         ",
"RTFS_DEBUG_IO_J_ROOT_READ          ",
"RTFS_DEBUG_IO_J_RESTORE_READ       ",
"RTFS_DEBUG_IO_J_RESTORE_START      ",
"RTFS_DEBUG_IO_J_RESTORE_END        ",
"RTFS_DEBUG_IO_J_INDEX_READ         ",
"RTFS_DEBUG_IO_J_RESTORE_INFO_WRITE ",
"RTFS_DEBUG_IO_J_RESTORE_FAT_WRITE  ",
"RTFS_DEBUG_IO_J_RESTORE_BLOCK_WRITE"};


/* Use the journal callback functions to log what operation was last
   completed on behalf of the test loop. We'll use this info to determine
   what should be on the disk after we restore */


static void   asytest_drive_async_complete_cb(int driveno, int operation,int success)
{
    RTFS_ARGSUSED_INT(driveno);
    RTFS_ARGSUSED_INT(operation);
    RTFS_ARGSUSED_INT(success);


    if (operation == DRV_ASYNC_JOURNALFLUSH)
    {
        if (success)
        {
            async_test_master.num_watchpoints_flushed = async_test_master.num_watchpoints_executed;
        }
    }
    else if (operation == DRV_ASYNC_RESTORE)
    {
        if (success)
        {
            async_test_master.num_watchpoints_restored = async_test_master.num_watchpoints_flushed;
        }
    }
#if (VERBOSE_MODE)
    if (operation == DRV_ASYNC_JOURNALFLUSH)
    {
        if (success)
        {
            pro_test_print_dword("DRV_ASYNC_JOURNALFLUSH success set num_watchpoints_flushed to == ", async_test_master.num_watchpoints_executed, PRFLG_NL);
        }
        else
        {
            pro_test_print_dword("DRV_ASYNC_JOURNALFLUSH failed: num_watchpoints_flushed == ", async_test_master.num_watchpoints_flushed, PRFLG_NL);
        }
    }
    else if (operation == DRV_ASYNC_RESTORE)
    {
        if (success)
        {
            pro_test_print_dword("DRV_ASYNC_RESTORE success set num_watchpoints_restored to == ", async_test_master.num_watchpoints_restored, PRFLG_NL);
        }
        else
        {
            pro_test_print_dword("DRV_ASYNC_RESTORE failed: num_watchpoints_restored == ", async_test_master.num_watchpoints_restored, PRFLG_NL);
        }
    }
#endif
}


static void init_async_test_control()
{

int i;
DDRIVE *ptest_drive;

    ptest_drive = test_drive_structure();
    /* Save off statistics from the normal run */
    async_test_master.reference_io_stats = ptest_drive->du.iostats;
    /* Now initialize the test records */
    /* Set the "next error block value to the end for each.
       we will start at the end and loop to the begainning to
       catch boundary errors first */

    for ( i = 0; i < RTFS_DEBUG_IO_J_RANGES; i++)
    {
        async_test_master.failure_tests[i].failure_mode = i;
        async_test_master.failure_tests[i].force_next_failure =
            async_test_master.reference_io_stats.blocks_transferred[i];
    }
    async_test_master.current_test = 0;

}

static int cycle_async_test_control()
{
int loops;
BOOLEAN force_return;
DDRIVE *ptest_drive;

    ptest_drive = test_drive_structure();
    force_return = FALSE;
    loops = 0;
/*
    force_return = TRUE;
    ptest_drive->du.iostats.force_io_error_read = FALSE;
    ptest_drive->du.iostats.force_io_error_type = RTFS_DEBUG_IO_FAT_STREAM;
    ptest_drive->du.iostats.force_io_error_when = 1031;
    ptest_drive->du.iostats.last_force_io_error_request = ptest_drive->du.iostats.force_io_error_when;
    async_test_master.failure_tests[ptest_drive->du.iostats.force_io_error_type].forced_write_failures += 1;
    async_test_master.flush_journal_frequently = TRUE;
*/
    if (force_return)
    {
        PRO_TEST_ANNOUNCE("cycle_async_test_control force fixed");
        return(0);
    }

    for (;;)
    {
        async_test_master.current_test += 1;

        /* An IO failure on opening a segment when no new information is added does not
           cause data loss so don't test that case
        if (async_test_master.current_test == RTFS_DEBUG_IO_J_INDEX_OPEN)
            async_test_master.current_test += 1;
        */

        if (async_test_master.current_test >= RTFS_DEBUG_IO_J_RANGES)
        {
            loops += 1;
            if (loops > 2)
                return(-1);  /* Completed all tests */
            /* Switch from read error test to write error test each time we hit the edge */
            async_test_master.current_test = 0;
        }

        /* Test the next IO, if blocks are tranfered for that type and every block of that type
           wasn't already tested */
        if ( async_test_master.reference_io_stats.blocks_transferred[async_test_master.current_test] &&
             async_test_master.failure_tests[async_test_master.current_test].forced_failures < async_test_master.reference_io_stats.blocks_transferred[async_test_master.current_test])
        {
            ptest_drive->du.iostats.force_io_error_on_type = async_test_master.current_test;
            /* Iterate through all possible blocks and reset to 1 if at end */
            ptest_drive->du.iostats.force_io_error_when =
                async_test_master.failure_tests[async_test_master.current_test].force_next_failure;
            async_test_master.failure_tests[async_test_master.current_test].force_next_failure += 1;
            if (async_test_master.failure_tests[async_test_master.current_test].force_next_failure > async_test_master.reference_io_stats.blocks_transferred[async_test_master.current_test])
                async_test_master.failure_tests[async_test_master.current_test].force_next_failure = 0;



            break;
        }
    }
    RTFS_PRINT_STRING_1((byte *)"Next time: forcing error at :(type, when) ", 0);
    RTFS_PRINT_STRING_1((byte *)test_failure_mode_names[ptest_drive->du.iostats.force_io_error_on_type], 0);
    RTFS_PRINT_LONG_1(ptest_drive->du.iostats.force_io_error_when, PRFLG_NL);
    async_test_master.failure_tests[ptest_drive->du.iostats.force_io_error_on_type].forced_failures += 1;
    return(0);
}


static void print_async_test_summary()
{
int i;
dword total_errors;

    total_errors = 0;
    pro_test_announce((byte *)"Test Summary: compare watchpoints after forced IO errors and restores");
    pro_test_announce((byte *)"Forcing IO error on each possible block");
    pro_test_announce((byte *)"Sequence: deltree(), mkdir(), open, write(), close(), mkdir(), commit()");

    for ( i = 0; i < RTFS_DEBUG_IO_J_RANGES; i++)
    {
        if (async_test_master.reference_io_stats.blocks_transferred[i])
        {
            async_test_master.failure_tests[i].failure_mode = i;
            RTFS_PRINT_STRING_1((byte *)test_failure_mode_names[i], 0);
            pro_test_print_dword("  ", async_test_master.failure_tests[i].forced_failures, 0);
            pro_test_print_dword("  Forced Errors  - out of  ", async_test_master.reference_io_stats.blocks_transferred[i], PRFLG_NL);
            total_errors += async_test_master.failure_tests[i].forced_failures;
        }
    }
    pro_test_print_two_dwords(" Total Forced Errors    - ", total_errors, " Total Restores  - ", gl_global_restores, PRFLG_NL);
}

static int create_file_for_asynchronous_test(byte *filename,dword options)
{
    int fd;
    EFILEOPTIONS my_options;

    rtfs_memset(&my_options, 0, sizeof(my_options));
    my_options.allocation_policy = options;

    fd = pc_efilio_open(filename,(word)(PO_BINARY|PO_RDWR|PO_EXCL|PO_CREAT),(word)(PS_IWRITE | PS_IREAD)
                         ,&my_options);
    return(fd);
}

BOOLEAN _pro_test_write_n_dwords(int fd, dword value, dword size_dw,
                dword *pnwritten, BOOLEAN do_increment, byte *buffer, BOOLEAN abort_on_error);

static dword write_file_for_asynchronous_test(int fd,dword test_file_size_dw,BOOLEAN do_io)
{
dword ntransfered_dw;
    /* Tell write not to abort if it gets an error since we may be causeing it */
    if (do_io)
        _pro_test_write_n_dwords(fd, 0, test_file_size_dw, &ntransfered_dw,
            TRUE, data_buffer, FALSE);
    else
        _pro_test_write_n_dwords(fd, 0, test_file_size_dw, &ntransfered_dw,
            TRUE, 0, FALSE);
    return(ntransfered_dw);
}

static void clean_for_asynchronous_test(byte *path_dir, byte *root_dir)
{
    /* Disable Failsafe, etc and run checkdisk */
    _init_drive_for_asynchronous_test(0, FALSE, TRUE);
    if (!pc_set_cwd((byte *)"\\"))
    { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    pc_deltree((byte *)path_dir);
    if (!pc_set_cwd(root_dir))
    {
        if (!pc_mkdir(root_dir))
         { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        if (!pc_set_cwd(root_dir))
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    }
    if (!pc_diskflush((byte *) test_driveid))
    {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    /* Restore current configuration */
    _init_drive_for_asynchronous_test(asytest_drive_operating_policy, asytest_failsafe_enabled, TRUE);
    if (!pc_set_cwd((byte *)"\\"))
    { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    if (!pc_set_cwd(root_dir))
    {
        { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    }
}




static void one_asynchronous_test(byte *full_path, int end_state)
{
int fd,ret_val;
dword test_file_size_dw, ntransfered_dw;

    if (!pc_mkdir((byte *)full_path))
    {
        ERTFS_ASSERT_TEST(rtfs_debug_zero())
    }
    if (!pc_set_cwd(full_path))
    {
        ERTFS_ASSERT_TEST(rtfs_debug_zero())
    }

    if (!pc_mkdir((byte *)"TEST_D1"))
    {
        ERTFS_ASSERT_TEST(rtfs_debug_zero())
    }
    /* Create a directory, this will dirty the fat and Failsafe Journal */
    if (!pc_mkdir((byte *)"TEST_D2"))
    {
        ERTFS_ASSERT_TEST(rtfs_debug_zero())
    }
    /* Now create and fill a 32 bit file */
    fd = create_file_for_asynchronous_test((byte *)"TEST_F1",PCE_ASYNC_OPEN);
    ERTFS_ASSERT_TEST(fd>=0)
     /* Step once should enter FILE state */
    ret_val = pc_async_continue(test_drivenumber, DRV_ASYNC_FILES, 1);
    ERTFS_ASSERT_TEST(ret_val == PC_ASYNC_COMPLETE)
     /* Step to exit FILE state */
    ret_val = pc_async_continue(test_drivenumber, DRV_ASYNC_DONE_FILES, 0);
    ERTFS_ASSERT_TEST(ret_val == PC_ASYNC_COMPLETE)
    /* Write 256 meg to the file */
/*    test_file_size_dw = 1024*1024*256; */
    test_file_size_dw = FILESIZE_32;
    ntransfered_dw = write_file_for_asynchronous_test(fd,test_file_size_dw,FALSE);
    ERTFS_ASSERT_TEST(test_file_size_dw == ntransfered_dw)

    ret_val = pc_efilio_async_close_start(fd);
    ERTFS_ASSERT_TEST(ret_val == PC_ASYNC_CONTINUE)

     /* Step once should enter FILE state */
    ret_val = pc_async_continue(test_drivenumber, DRV_ASYNC_FILES, 1);
    ERTFS_ASSERT_TEST(ret_val == PC_ASYNC_COMPLETE)
     /* Step to exit FILE state */
    ret_val = pc_async_continue(test_drivenumber, DRV_ASYNC_DONE_FILES, 0);
    ERTFS_ASSERT_TEST(ret_val == PC_ASYNC_COMPLETE)

     /* Step once should enter FATFLUSH state */
    ret_val = pc_async_continue(test_drivenumber, DRV_ASYNC_FATFLUSH, 1);
    ERTFS_ASSERT_TEST(ret_val == PC_ASYNC_COMPLETE)

    /* Step until we exit FATFLUSH state */
    ret_val = pc_async_continue(test_drivenumber, DRV_ASYNC_DONE_FATFLUSH, 0);
    ERTFS_ASSERT_TEST(ret_val == PC_ASYNC_COMPLETE)
    if (!pc_set_cwd((byte *)"\\"))
    {
        ERTFS_ASSERT_TEST(rtfs_debug_zero())
    }

    /* Return if FATFLUSH is the last requested state */
    if (end_state == DRV_ASYNC_FATFLUSH)
        return;

    /* Step once should enter JOURNALFLUSH state */
    ret_val = pc_async_continue(test_drivenumber, DRV_ASYNC_JOURNALFLUSH, 1);
    ERTFS_ASSERT_TEST(ret_val == PC_ASYNC_COMPLETE)
    /* Step until we exit JOURNALFLUSH state */
    ret_val = pc_async_continue(test_drivenumber, DRV_ASYNC_DONE_JOURNALFLUSH, 0);
    ERTFS_ASSERT_TEST(ret_val == PC_ASYNC_COMPLETE)
    if (end_state == DRV_ASYNC_JOURNALFLUSH)
        return;

    /* Step once should enter RESTORE state */
    ret_val = pc_async_continue(test_drivenumber, DRV_ASYNC_RESTORE, 1);
    ERTFS_ASSERT_TEST(ret_val == PC_ASYNC_COMPLETE)
    /* Step until we exit RESTORE state */
    ret_val = pc_async_continue(test_drivenumber, DRV_ASYNC_DONE_RESTORE, 0);
    ERTFS_ASSERT_TEST(ret_val == PC_ASYNC_COMPLETE)
}


/*
<TEST>    Prodedure:Test that interrupting after JOURNAL flush leaves the disk undamaged and unchanged
<TEST>       and that the Journaled changes are be restored to the volume by a restore.
<TEST>    Simulate a write IO error during asynchronous the first journal flush of s session.
<TEST>       Then check Journal file status. The journal file should be not valid or restore should
<TEST>       be not required or recommend.
<TEST>   Simulate write IO error during asynchronous journal commit. Then check disk
<TEST>   and look for lost chains or some other error. Check
<TEST>   Journal status and verify restore is required. Now restore disk from Journal
<TEST>   and verfiy lost chains or other errors are clear.
<TEST>
<TEST>
<TEST>   Perform random_test_asynchronous_api().
<TEST>   This test verifies that Failsafe behave properly when IO errors are simulated on every block that is read or written during a sequence of operations.
<TEST>   It  uses block access statistics to capture all block IO activity and set of becnchmarks that log the state of the volume
<TEST>   It calibrates itself by creating subdirectories and populating files, and issuing Journal flush and synchronize requests
<TEST>   while gathering block access statistics and capturing a set of volume state bechmarks. It then performs the same set of operations in a
<TEST>   repetative loop and simulates an IO error for every block read and write request. For each simulated error it verifies
<TEST>   that Failsafe performs correctly, either leaving the volume unchanged if no flushes occured before the error, or restoring the volume to state recorded
<TEST>   after the last succesful synchronize operation.
<TEST>
*/

static void failsafe_test_asynchronous_api(void)
{
int ret_val;
FSINFO fs_info;
DDRIVE *ptest_drive;

    ptest_drive = test_drive_structure();
    rtfs_memset(&(ptest_drive->du.iostats),0,sizeof(ptest_drive->du.iostats));

    /* Turn off autoflush so we can monitor operations */
    /* Enable automatic flush of FAT, JOURNAL and restore */
    asytest_drive_operating_policy =
                DRVPOL_DISABLE_AUTOFLUSH|DRVPOL_ASYNC_FATFLUSH|DRVPOL_ASYNC_JOURNALFLUSH|DRVPOL_ASYNC_RESTORE;
    /* Initialize drive, enable failsafe, mount */
    asytest_failsafe_enabled = TRUE;
    _init_drive_for_asynchronous_test(asytest_drive_operating_policy, TRUE, TRUE);

    PRO_TEST_ANNOUNCE("Async Test: Initializing");

    /* Start with a clean slate */
    clean_for_asynchronous_test(ASYNC_TEST_FULLPATH, ASYNC_TEST_ROOT_DIR);
    /* Get initial checkpoint */
    pro_test_mark_checkpoint(test_drivenumber, &check_point_initial);

    rtfs_memset(&(ptest_drive->du.iostats),0,sizeof(ptest_drive->du.iostats));
    /* Do one pass all the way to RESTORED to Disk */

    one_asynchronous_test(ASYNC_TEST_FULLPATH, DRV_ASYNC_RESTORE);
    /* Get populated checkpoint */
    pro_test_mark_checkpoint(test_drivenumber, &check_point_populated);

    /* Now delete the test directory and double check disk is unchanged */
    clean_for_asynchronous_test(ASYNC_TEST_FULLPATH,ASYNC_TEST_ROOT_DIR);
    pro_test_mark_checkpoint(test_drivenumber, &check_point_current);
    if (!pro_test_compare_checkpoints(&check_point_current,&check_point_initial))
    {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }

    /*  =========================================================
        Test that interrupting after JOURNAL flush leaves the disk
        undamaged and unchanged and that the Journaled changes can
        be restored to the volume */

    PRO_TEST_ANNOUNCE(" ");
    PRO_TEST_ANNOUNCE("Async Test: Verify abort after asynchronous journal Flush ");

    /* Do one pass all the way to JOURNAL FLUSH STATE */
    one_asynchronous_test(ASYNC_TEST_FULLPATH, DRV_ASYNC_JOURNALFLUSH);
    /* Now check that disk is unchanged from initial */
    pro_test_mark_checkpoint(test_drivenumber, &check_point_current);
    if (!pro_test_compare_checkpoints(&check_point_current,&check_point_initial))
    {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    /* Disable Failsafe, enable automount */
    _init_drive_for_asynchronous_test(0, FALSE, TRUE);
    /* Now check Failsafe status */
    if (!fs_api_info((byte *) test_driveid,&fs_info))
    {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    /* Check Journal status */
    if (!fs_info.journal_file_valid || !fs_info.restore_recommended)
    {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    /* Now restore disk from Journal */
    if (!fs_api_restore((byte *) test_driveid))
    {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    /* Now check disk and verify that the test directory was restored */
     pro_test_mark_checkpoint(test_drivenumber, &check_point_current);
    if (!pro_test_compare_checkpoints(&check_point_current,&check_point_populated))
    {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    /* Now delete the test directory */
    clean_for_asynchronous_test(ASYNC_TEST_FULLPATH,ASYNC_TEST_ROOT_DIR);

    PRO_TEST_ANNOUNCE(" ");
    PRO_TEST_ANNOUNCE("Async Test: Simulate Write IO error during asynchronous journal Flush ");

    /* Do one pass all the way to FAT FLUSH STATE */
    one_asynchronous_test(ASYNC_TEST_FULLPATH, DRV_ASYNC_FATFLUSH);
    /* Now Step into Journal flush state but simulate an error before completion */
    /* Simulate a block write error and drive remove on the flush */
    PRO_TEST_ANNOUNCE("Simulate write error on Journal Flush");
    rtfs_memset(&(ptest_drive->du.iostats),0,sizeof(ptest_drive->du.iostats));
    ptest_drive->du.iostats.force_io_error_on_type = RTFS_DEBUG_IO_J_INDEX_FLUSH;
    ptest_drive->du.iostats.force_io_error_when = 1;

    ret_val = pc_async_continue(test_drivenumber, DRV_ASYNC_DONE_JOURNALFLUSH, 0);
    ERTFS_ASSERT_TEST(ret_val == PC_ASYNC_ERROR)

    /* Now check that disk is unchanged from initial */
    pro_test_mark_checkpoint(test_drivenumber, &check_point_current);
    /* Disk should look the same as initially */
    if (!pro_test_compare_checkpoints(&check_point_current,&check_point_initial))
    {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    /* Disable Failsafe, enable automount */
    _init_drive_for_asynchronous_test(0, FALSE, TRUE);
    /* Now check Failsafe status */
    if (!fs_api_info((byte *) test_driveid,&fs_info))
    {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    /* Check Journal status, should not be valid if journaling to hidden clusters
       Otherwise if ity is valid, restore should be not required or recommend. */
     if (fs_info.journal_file_valid && (fs_info.restore_recommended || fs_info.restore_required))
    {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }

    PRO_TEST_ANNOUNCE(" ");
    PRO_TEST_ANNOUNCE("Async Test: Simulate Write IO error during asynchronous journal commit ");

    _init_drive_for_asynchronous_test(asytest_drive_operating_policy, TRUE, TRUE);
    /* Do one pass all the way throug JOURNAL FLUSH STATE */
    one_asynchronous_test(ASYNC_TEST_FULLPATH, DRV_ASYNC_JOURNALFLUSH);
    /* Complete the JOURNAL RESTORE  */
    /* But simulate a disk write and removal error 10 blocks into the restore */
    rtfs_memset(&(ptest_drive->du.iostats),0,sizeof(ptest_drive->du.iostats));
    ptest_drive->du.iostats.force_io_error_on_type = RTFS_DEBUG_IO_J_RESTORE_FAT_WRITE;
    ptest_drive->du.iostats.force_io_error_when = 1;

    /* Step JOURNALFLUSH state */
    ret_val = pc_async_continue(test_drivenumber, DRV_ASYNC_DONE_RESTORE, 0);
    ERTFS_ASSERT_TEST(ret_val == PC_ASYNC_ERROR)

    /* Now check disk. Should have lost chains or some other error
       but this is not an abolute certainty
       note: check_disk_for_asynchronous_test() aborts the current mount */
    pro_test_mark_checkpoint(test_drivenumber, &check_point_current);
    if (!pro_test_compare_checkpoints(&check_point_current,&check_point_populated))
    {
        PRO_TEST_ANNOUNCE("Async Test: FAT errors observed as expected from aborted commit ");
    }
    else
    {
        PRO_TEST_ANNOUNCE("Async Test: No FAT errors observed as was expected but not guaranteed");
    }
    /* Disable Failsafe, enable automount */
    _init_drive_for_asynchronous_test(0, FALSE, TRUE);
    /* Now check Failsafe status */
    if (!fs_api_info((byte *) test_driveid,&fs_info))
    {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    /* Check Journal status */
    if (!fs_info.journal_file_valid || !fs_info.restore_required)
    {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    /* Now restore disk from Journal */
    if (!fs_api_restore((byte *) test_driveid))
    {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    /* Now check disk and verify that the test directory was restored */
    pro_test_mark_checkpoint(test_drivenumber, &check_point_current);
    if (!pro_test_compare_checkpoints(&check_point_current,&check_point_populated))
    {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }

    /* Now delete the test directory */
    clean_for_asynchronous_test(ASYNC_TEST_FULLPATH,ASYNC_TEST_ROOT_DIR);

    random_test_asynchronous_api();
    /* We'll do more tests like that */
    return;
}

static BOOLEAN process_watchpoint(int calibrate_state,byte *full_path)
{
    if (calibrate_state == 2)
    {
        /* Synch the drive, remember checkdisk */
        pc_async_continue(test_drivenumber, DRV_ASYNC_DONE_RESTORE, 0);
        pro_test_mark_checkpoint(test_drivenumber, &check_point_watch[async_test_master.num_watchpoints]);
        async_test_master.num_watchpoints += 1;
        if (!pc_set_cwd((byte *)"\\"))
        {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        if (full_path && !pc_set_cwd(full_path))
        {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    }
    /* Try to flush the journal if requested. Note this flushes the journal, not the FAT,*/
    else if (calibrate_state == 0) // && async_test_master.flush_journal_frequently)
    {
        async_test_master.num_watchpoints_executed += 1;
        if (pc_async_continue(test_drivenumber, DRV_ASYNC_DONE_JOURNALFLUSH, 0) == PC_ASYNC_ERROR)
        {
           PRO_TEST_ANNOUNCE("IO Error occured during Journal flush request stage");
           async_test_master.num_watchpoints_executed -= 1;
           return(FALSE); /* It failed.. don't record that we reached this far */
        }
    }
    return(TRUE);
}


static void populate_asynchronous_test(byte *full_path, int calibrate_state)
{
int fd,ret_val;
dword test_file_size_dw,ntransfered_dw;
DDRIVE *ptest_drive;


    ptest_drive = test_drive_structure();
    async_test_master.test_completed = FALSE;
    if (calibrate_state == 2)
        async_test_master.num_watchpoints = 0;
    async_test_master.num_watchpoints_executed = 0;
    async_test_master.num_watchpoints_flushed  = 0;
    async_test_master.num_watchpoints_restored = 0;

    ptest_drive->du.iostats.io_errors_forced = 0;


    fd = -1;
    if (!pc_set_cwd((byte *)"\\"))
        goto error_return;

    if (pc_isdir(full_path))
    {
        if (!pc_deltree(full_path))
            goto error_return;
    }
    if (!process_watchpoint(calibrate_state,0))
        goto error_return;

     if (!pc_set_cwd((byte *)"\\"))
        goto error_return;
    if (!pc_mkdir(full_path))
        goto error_return;
    if (!process_watchpoint(calibrate_state,0))
        goto error_return;
     if (!pc_set_cwd(full_path))
        goto error_return;
    if (!pc_mkdir((byte *)"TEST_D1"))
        goto error_return;
    if (!process_watchpoint(calibrate_state,full_path))
        goto error_return;

    /* Now create and fill a 32 bit file */
    fd = create_file_for_asynchronous_test((byte *)"TEST_F1",PCE_ASYNC_OPEN);
    if (fd < 0)
        goto error_return;
    /* Wait for the open to complete by making sure all file ops are completed */
    if (pc_async_continue(test_drivenumber, DRV_ASYNC_DONE_FILES, 0) != PC_ASYNC_COMPLETE)
         goto error_return;

      /* Write 256 meg to the file */
/*    test_file_size_dw = 1024*1024*256; */
    test_file_size_dw = FILESIZE_32;
    ntransfered_dw = write_file_for_asynchronous_test(fd,test_file_size_dw,FALSE);
    if (test_file_size_dw != ntransfered_dw)
        goto error_return;

    /* Create a directory, this will dirty the fat and Failsafe Journal */
    if (!pc_mkdir((byte *)"TEST_D2"))
        goto error_return;

    ret_val = pc_efilio_async_close_start(fd);
    if (ret_val != PC_ASYNC_CONTINUE)
        goto error_return;
    if (!pc_set_cwd((byte *)"\\"))
        goto error_return;
    if (!process_watchpoint(calibrate_state,full_path))
        goto error_return;
   /* If we get here step until we exit RESTORE state or we get aborted */
    if (pc_async_continue(test_drivenumber, DRV_ASYNC_DONE_RESTORE, 0) == PC_ASYNC_COMPLETE)
        async_test_master.test_completed = TRUE;
error_return:
    ptest_drive->du.iostats.force_io_error_on_type = 0;
    /* Changed 1-23-07 - call the driver itself to be certain no change requests
       are outstanding (we simulate errors by triggering change requests */
	ptest_drive->pmedia_info->mount_flags &= ~DEVICE_REMOVE_EVENT;
    /* Abort journalling and disk mount */
    fs_api_disable((byte *) test_driveid, TRUE);
    if (fd >= 0)
    {
        /* Close the file if it is open (will release fd if aborted while open */
        pc_efilio_close(fd);
    }
    return;
}

#define NUM_RANDOM_LOOPS 400

static void random_test_asynchronous_api(void)
{
FSINFO fs_info;
int calibrate_test;
DDRIVE *ptest_drive;
int i;
     gl_global_restores = 0;

    ptest_drive = test_drive_structure();
    clean_for_asynchronous_test(ASYNC_TEST_FULLPATH,ASYNC_TEST_ROOT_DIR);
    /* Get initial checkpoint */
    pro_test_mark_checkpoint(test_drivenumber, &check_point_current);

    /* Turn off autoflush so we can monitor operations */
    /* Enable automatic flush of FAT, JOURNAL and restore */
    asytest_drive_operating_policy =
                DRVPOL_DISABLE_AUTOFLUSH|DRVPOL_ASYNC_FATFLUSH|DRVPOL_ASYNC_JOURNALFLUSH|DRVPOL_ASYNC_RESTORE;
    asytest_failsafe_enabled = TRUE;
    /* Initialize drive, enable failsafe, mount */
    calibrate_test = 2; /* Establish watchpoints */
    rtfs_memset(&async_test_master, 0, sizeof(async_test_master));
    for(i=0;i<NUM_RANDOM_LOOPS;i++)
    {
        rtfs_memset(&(ptest_drive->du.iostats),0,sizeof(ptest_drive->du.iostats));
        asytest_drive_operating_policy =
                DRVPOL_DISABLE_AUTOFLUSH|DRVPOL_ASYNC_FATFLUSH|DRVPOL_ASYNC_JOURNALFLUSH|DRVPOL_ASYNC_RESTORE;
       asytest_failsafe_enabled = TRUE;

        _init_drive_for_asynchronous_test(asytest_drive_operating_policy, TRUE, TRUE);
        rtfs_memset(&(ptest_drive->du.iostats),0,sizeof(ptest_drive->du.iostats));
        if (!calibrate_test)
        {
            print_async_test_summary();
            if (cycle_async_test_control() == -1)   /* Did all combinations */
                break;
        }
        /* Do one pass */
        populate_asynchronous_test(ASYNC_TEST_FULLPATH,calibrate_test);
        asytest_drive_operating_policy = 0;
        asytest_failsafe_enabled = FALSE;

        _init_drive_for_asynchronous_test(asytest_drive_operating_policy, FALSE, TRUE);
        if (!pc_set_cwd((byte *)"\\"))
         {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        if (async_test_master.test_completed)
        {
            if (calibrate_test==2)
                calibrate_test=1; /* Count transfers on the next pass */
            else if (calibrate_test == 1)
            { /* Establish test parameters based on counted transfers */
                init_async_test_control();
                calibrate_test = 0;
            }
            else
            {
                /* Get statistics on the drive - verify no errors */
                PRO_TEST_ANNOUNCE(" ");
                PRO_TEST_ANNOUNCE("Function was completed and committed this pass. No restore needed");

                pro_test_mark_checkpoint(test_drivenumber, &check_point_current);
                if (!pro_test_compare_checkpoints(&check_point_current,&check_point_watch[async_test_master.num_watchpoints-1]))
                {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
           }
        }
        else
        {
        int valid_watchpoint_before_restore, valid_watchpoint_after_restore;
        BOOLEAN do_restore;
           valid_watchpoint_before_restore = valid_watchpoint_after_restore = -1;
           do_restore = FALSE;

           PRO_TEST_ANNOUNCE(" ");
           PRO_TEST_ANNOUNCE("Function was aborted by simulated error this pass. Will restore if flushed");
           /* Check Failsafe status */
           if (!fs_api_info((byte *) test_driveid,&fs_info))
           {
                /* If No FAILSAFE file or there is one and we are not in a restore state
                   then the disk should be in the state at the last syncronize */
                if (async_test_master.num_watchpoints_restored)
                {
                    valid_watchpoint_before_restore = async_test_master.num_watchpoints_restored-1;
                }
                PRO_TEST_ANNOUNCE("fs_api_info failed, but restore needed. Master block write must have aborted..");
                valid_watchpoint_before_restore = -1;
           }
           else
           {
              if (fs_info.journal_file_valid)
              {   /* Now restore disk from Journal */
                 if (fs_info.restore_required || fs_info.restore_recommended)
                 {
                    do_restore = TRUE;
                    if (async_test_master.num_watchpoints_flushed)
                        valid_watchpoint_after_restore = async_test_master.num_watchpoints_flushed-1;
                    else
                    {
                          ERTFS_ASSERT_TEST(rtfs_debug_zero())
                    }
                }
              }
           }
           if (valid_watchpoint_before_restore >= 0)
           {
                /* Now check disk and verify that the whole test directory was restored */
                pro_test_mark_checkpoint(test_drivenumber, &check_point_current);
                if (!pro_test_compare_checkpoints(&check_point_current,&check_point_watch[valid_watchpoint_before_restore]))
                {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
           }
           if (do_restore)
           {
                if (!fs_api_restore((byte *) test_driveid))
                {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
                gl_global_restores += 1;
           }
           if (valid_watchpoint_after_restore >= 0)
           {
                /* Now check disk and verify that the whole test directory was restored */
                pro_test_mark_checkpoint(test_drivenumber, &check_point_current);
                if (!pro_test_compare_checkpoints(&check_point_current,&check_point_watch[valid_watchpoint_after_restore]))
                {
                    int i,hits;
                    hits = 0;
                    RTFS_PRINT_STRING_1((byte *)"Checkpoint compare failed: for test  ", 0);
                    RTFS_PRINT_STRING_1((byte *)test_failure_mode_names[async_test_master.current_test], 0);
                    RTFS_PRINT_STRING_1((byte *)" .. Should compare to Checkpoint # : ", 0);
                    RTFS_PRINT_STRING_1((byte *)"Checkpoint compare failed: for test  ", 0);
                    RTFS_PRINT_LONG_1(valid_watchpoint_after_restore, PRFLG_NL);

                    for (i = 0; i < async_test_master.num_watchpoints ; i++)
                    {
                        if (pro_test_compare_checkpoints(&check_point_current,&check_point_watch[i]))
                        {
                            hits += 1;
                            RTFS_PRINT_STRING_1((byte *)" But (compares to Checkpoint # : ", 0);
                            RTFS_PRINT_LONG_1(i, 0);
                            RTFS_PRINT_STRING_1((byte *)" pass.. limits of the test", PRFLG_NL);
                       }
                    }
                    if (!hits)
                    {
                        RTFS_PRINT_STRING_1((byte *)" fail.. no checkpoints matched", PRFLG_NL);
                        ERTFS_ASSERT_TEST(rtfs_debug_zero())
                    }
                }
           }
        }
        /* If we created a directory delete it and verify disk is pristine */
        if (pc_isdir(ASYNC_TEST_FULLPATH))
        {
            if (!pc_deltree(ASYNC_TEST_FULLPATH))
                {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
            pro_test_mark_checkpoint(test_drivenumber, &check_point_current);
            if (!pro_test_compare_checkpoints(&check_point_current,&check_point_watch[0]))
            {  ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
        }
    }
    /* We'll do more tests like that */
    return;
}

#endif /* (INCLUDE_FAILSAFE_CODE) */

#endif /* (!INCLUDE_DEBUG_TEST_CODE) else.. */
#endif /* Exclude from build if read only */
