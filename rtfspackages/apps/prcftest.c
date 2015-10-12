/* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/*
<TEST>  Test File:   rtfspackages/apps/prcftest.c
<TEST>
<TEST>   Procedure: pc_cfilio_test(void)
<TEST>   Description: RTFS Pro Plus circular file regression test suite. Test suite performs tests to verify the correct operation of circular file features.
<TEST>
<TEST>   Test suite performs tests to verify the correct operation of features provided with RTFS Pro Plus.
<TEST>      Proper operation of circular file API calls.
<TEST>      Proper operation of circular file extract API call.
<TEST>      may be invoked invoke from the "test" command of the RTFS Pro Plus command shell.
<TEST>   The following subrotuines execute to test subsystems:
<TEST>      options_circular_file_efio_test - Verify proper argument handling.
<TEST>      circular_file_test -  Test proper functioning of circular file read, write and seek operations.
<TEST>      circular_file_extract_test -  Test proper functioning of circular file extract
<TEST>
*/
/*
  Notes about running and supporting preftest.c on your target system

    1.   To run pc_cfilio_test() you must first set:

          #define INCLUDE_DEBUG_TEST_CODE      1

          In rtfsconf.h. This is needed because the routine
          shares some global variables with the internal routines, that
          INCLUDE_DEBUG_TEST_CODE

    2. This program requires the following library routines:
        malloc()
        free()
        If you do not have these routines, you must perform
        minor editting of the file to work around that fact.


*/

#include "rtfs.h"
#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */
#if (!(INCLUDE_DEBUG_TEST_CODE && INCLUDE_CIRCULAR_FILES))
void pc_cfilio_test(byte *pdriveid)
{
    RTFS_ARGSUSED_PVOID((void *)pdriveid);
    RTFS_PRINT_STRING_1((byte *)"Build with INCLUDE_DEBUG_TEST_CODE and INCLUDE_CIRCULAR_FILES to run tests", PRFLG_NL);
}
#else
#include "protests.h"

#define MAX_LOOPS 1000 /* For tests that loop limit the number of loops */

static BOOLEAN lin_circ_compare(int fd_circ, int lin_fd, dword length_in_dwords,dword expected_value);


static int options_circular_file_efio_test(char *filename);
static int circular_file_extract_test(char *filename, dword file_size_in_dwords);
static dword dwltohighbytes(dword length_in_dwords);
static dword dwltolowbytes(dword length_in_dwords);
static dword highlowbytestodw(dword hi, dword lo);
static int pc_cfilio_extract_file_open(byte *remap_filename, BOOLEAN is_64);
static void dump_circ_file(PC_FILE *preader);
static void _pc_cfilio_test(void);

/* Stand alone cfilio test called fron efishell */

void pc_cfilio_test(byte *pdriveid)
{
#if (INCLUDE_FAILSAFE_CODE)
void *pfailsafe;
#endif

    if (!set_test_drive(pdriveid))
        return;
    data_buffer = 0;
    pro_test_alloc_data_buffer();
    /* Remount the drive by closing it and re-accessing */
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

    pc_set_default_drive(test_driveid);
    pc_set_cwd((byte *)"\\");
    _pc_cfilio_test();
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

static void _pc_cfilio_test(void)
{

    PRO_TEST_ANNOUNCE("Testing PCE_CIRCULAR_FILE option");
    options_circular_file_efio_test("TESTFILE");
    PRO_TEST_ANNOUNCE ("Testing circular file extract api 1024 K 32 bit file");
    circular_file_extract_test("TESTFILE", (1024*1024));
    PRO_TEST_ANNOUNCE ("Testing circular file extract api .5 Gigabyte 32 bit file");
    circular_file_extract_test("TESTFILE", (1024*1024*1024)/4);

    PRO_TEST_ANNOUNCE ("pc_cfilio_test complete");
}

static dword test_sizes[] =
{
1200000, /* 1,200,000 dwords (64 bit) */
1000, /* 1000 dwords */
0
};


static BOOLEAN circular_file_lseek_write_dwords(int fd, int origin,
        dword offset_in_dwords, dword *poffset,BOOLEAN use_stream_seek);
static BOOLEAN circular_file_lseek_read_dwords(int fd, int origin,
        dword offset_in_dwords, dword *poffset,BOOLEAN use_stream_seek);
static void circular_file_test(char *filename, dword cfilesize /* in dwords */,
        BOOLEAN use_stream_seek);
static BOOLEAN circ_write_n_dwords(int fd, dword value, dword count,
        dword *pnwritten, BOOLEAN do_increment);
static BOOLEAN circ_read_n_dwords(int fd, dword value, dword count, dword *pnread);
static int reopen_circular_file(char *filename, dword size_hi,
        dword size_lo,ddword *pfilesize_ddw);

static int options_circular_file_efio_test(char *filename)
{
    int fd;
    EFILEOPTIONS options;
    dword ltemp;

    /*
        options_circular_file_efio_test

<TEST>   Procedure:Test pc_cfilio_open()
<TEST>    Pass invalid arguments (wrap point equals zero),
<TEST>    Verify that the open fails and errno is  PEINVALIDPARMS.
    */
    rtfs_memset(&options, 0, sizeof(options));
    options.allocation_policy = PCE_CIRCULAR_FILE;
    options.min_clusters_per_allocation = 0;
    options.circular_file_size_hi = options.circular_file_size_lo = 0;
    fd = pc_cfilio_open((byte *)filename,
        (word)(PO_BINARY|PO_WRONLY|PO_CREAT|PO_TRUNC|PO_BUFFERED),&options);

    if ((fd >=  0) || (get_errno() != PEINVALIDPARMS))
    {
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    }
    PRO_TEST_ANNOUNCE("Performing circular file test set 1");
    for (ltemp = 0; test_sizes[ltemp] != 0; ltemp++)
    {
        /* Test using stream seek */
        circular_file_test(filename, test_sizes[ltemp],TRUE);
        /* Test using linear seek */
        circular_file_test(filename, test_sizes[ltemp],FALSE);
    }
    return(0);
}

/*
    circular_file_test

<TEST>    Procuedure: Test proper functioning of circular file read, write and seek operations
<TEST>    use both circular file open modes:
<TEST>         PCE_CIRCULAR_BUFFER - Write always succeeds
<TEST>         PCE_CIRCULAR_FILE   - Write truncates if it overtakes the
<TEST>                               read pointer.
<TEST>    and both seek modes:
<TEST>
<TEST>        pc_cstreamio_lseek   - Read and write pointers are 64 bit
<TEST>                               quantities that never reset. Offset
<TEST>                               zero is the first byte written to the
<TEST>                               and the offset of eof is the last
<TEST>                               byte written to the file.
<TEST>                               valid offsets are the range:
<TEST>                               (last byte written - cirular file size)
<TEST>                               to (last byte written)
<TEST>        pc_cfilio_lseek    -   Read and write pointers are 64 bit
<TEST>                               quantities that reset when they reach the
<TEST>                               circular file wrap point.
<TEST>                               Offset zero is offset zero in the file
<TEST>                               The offset of eof is the cirular file size
<TEST>                               to (last byte written)
*/

static void circular_file_test(char *filename, dword cfilesize /* in dwords */,
        BOOLEAN use_stream_seek)
{
    int fd_circ;
    EFILEOPTIONS options;
    dword loop, ltemp,retptr,current_logical_start,
          current_logical_end,current_stream_end,nread, nwritten;
    dword prev_read_pointer,cfilesize_hi, cfilesize_lo;
    ddword ltemp_ddw, cfilesize_ddw;
    /* Calculate the 32 bit size in dwords to g4 bit size in bytes */
    cfilesize_ddw    = M64SET32(0,cfilesize);
    cfilesize_ddw        = M64LSHIFT(cfilesize_ddw,2); /* cfilesize * 4 */
    cfilesize_hi = M64HIGHDW(cfilesize_ddw);
    cfilesize_lo = M64LOWDW(cfilesize_ddw);

    pro_test_print_dword("Performing circ test of file of size ",
                        cfilesize, PRFLG_NL);
    if (use_stream_seek)
        PRO_TEST_ANNOUNCE("Using pc_cstreamio_lseek()");
    else
        PRO_TEST_ANNOUNCE("Using pc_cfilio_lseek()");

    fd_circ = reopen_circular_file(filename, cfilesize_hi, cfilesize_lo,
                                    &cfilesize_ddw);
    ERTFS_ASSERT_TEST(fd_circ >= 0)

    ltemp_ddw        = M64RSHIFT(cfilesize_ddw,2); /* cfilesize/4 */
    cfilesize        = M64LOWDW(ltemp_ddw);
    pro_test_print_dword("size to nearest cluster is ",
            cfilesize, PRFLG_NL);

    PRO_TEST_ANNOUNCE("Testing circular file seek options ");
    /*  Test proper functioning of pc_cstreamio_lseek
        and pc_cfilio_lseek
    */
    current_logical_start=current_logical_end=current_stream_end = 0;
    if (!circ_write_n_dwords(fd_circ, current_stream_end, cfilesize/2,
        &nwritten,TRUE))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}

    current_stream_end += cfilesize/2;
    current_logical_end += cfilesize/2;

#define beyond_current_end (current_logical_end + 1)

    /*
<TEST>    verify that seek past eof stops at eof when eof is not yet at the file wrap point
<TEST>    verify that read past eof stops at eof when eof is not yet at the file wrap point
    */
    /* Seek to 1 */
    if (!circular_file_lseek_write_dwords(fd_circ, PSEEK_SET, 1,
        &retptr, use_stream_seek) || retptr != current_logical_start+1)
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (!circular_file_lseek_write_dwords(fd_circ, PSEEK_SET, 0, &retptr,
            use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (!circular_file_lseek_read_dwords(fd_circ, PSEEK_SET, 0, &retptr,
            use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (!circular_file_lseek_write_dwords(fd_circ, PSEEK_CUR,
            beyond_current_end, &retptr, use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    ERTFS_ASSERT_TEST(retptr == current_logical_end)
    if (!circular_file_lseek_read_dwords(fd_circ, PSEEK_CUR,
        beyond_current_end, &retptr, use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    ERTFS_ASSERT_TEST(retptr == current_logical_end)

    /*
<TEST>        verify that PSEEK_END stops at eof when eof is not yet at the file wrap point
    */
    if (!circular_file_lseek_write_dwords(fd_circ, PSEEK_SET, 0,
        &retptr, use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (!circular_file_lseek_read_dwords(fd_circ, PSEEK_SET, 0, &retptr,
        use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (!circular_file_lseek_write_dwords(fd_circ, PSEEK_END, 0, &retptr,
        use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    ERTFS_ASSERT_TEST(retptr == current_logical_end)
    if (!circular_file_lseek_read_dwords(fd_circ, PSEEK_END, 0, &retptr,
        use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    ERTFS_ASSERT_TEST(retptr == current_logical_end)
    /*
<TEST>        verify that PSEEK_END stops at beginning of file if offset is greater than current length of file, when eof is not yet at the file wrap point
    */
    if (!circular_file_lseek_write_dwords(fd_circ, PSEEK_END, cfilesize-1,
        &retptr, use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    ERTFS_ASSERT_TEST(retptr == current_logical_start)
    if (!circular_file_lseek_read_dwords(fd_circ, PSEEK_END, cfilesize-1,
        &retptr, use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    ERTFS_ASSERT_TEST(retptr ==  current_logical_start)


    /*
<TEST>    verify that seek past eof stops at eof when eof is at the file wrap point
<TEST>    verify that read past eof stops at eof when eof is at the file wrap point
    */
    if (!circular_file_lseek_write_dwords(fd_circ, PSEEK_END, 0, &retptr,
        use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (!circ_write_n_dwords(fd_circ, current_stream_end, cfilesize/2,
        &nwritten,TRUE))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    current_logical_end      += cfilesize/2;
    current_stream_end += cfilesize/2;

    if (!circular_file_lseek_write_dwords(fd_circ, PSEEK_CUR, 0, &ltemp,
        use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (!circular_file_lseek_write_dwords(fd_circ, PSEEK_SET,
        beyond_current_end, &retptr, use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (retptr != current_logical_end)
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (!circular_file_lseek_read_dwords(fd_circ, PSEEK_CUR, 0, &ltemp,
        use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (!circular_file_lseek_read_dwords(fd_circ, PSEEK_SET,
        beyond_current_end, &retptr, use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (retptr != current_logical_end)
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}

    /*
<TEST>    verify that seek past eof using PSEEK_CUR stops at eof when eof
<TEST>    is at the file wrap point
<TEST>    verify that read past eof stops at eof when eof is at
<TEST>    the file wrap point
    */
    if (!circular_file_lseek_write_dwords(fd_circ, PSEEK_SET,
        current_logical_start, &retptr, use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (!circular_file_lseek_read_dwords(fd_circ, PSEEK_SET,
        current_logical_start, &retptr, use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (!circular_file_lseek_write_dwords(fd_circ, PSEEK_CUR, cfilesize*2,
        &retptr, use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (retptr != current_logical_end)
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (!circular_file_lseek_read_dwords(fd_circ, PSEEK_CUR, cfilesize*2,
        &retptr, use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (retptr != current_logical_end)
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}

    /*
<TEST>    verify that PSEEK_END stops at beginning of file if offset is greater than current length of file, when eof is at the file wrap point
    */
    if (!circular_file_lseek_write_dwords(fd_circ, PSEEK_END, 0, &retptr,
        use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (retptr != current_logical_end)
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (!circular_file_lseek_read_dwords(fd_circ, PSEEK_END, 0, &retptr,
        use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (retptr != current_logical_end)
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    /* try to seek back filesize*2 + 1.. should truncate to base (cfilesize) */
    if (!circular_file_lseek_write_dwords(fd_circ, PSEEK_END,
        cfilesize*2 + 1, &retptr, use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (retptr != current_logical_start)
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (!circular_file_lseek_read_dwords(fd_circ, PSEEK_END,
        cfilesize*2 + 1, &retptr, use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (retptr != current_logical_start)
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}

    /* Fill the file to 2 X file size */
    /* seek to end and write filesize/2 */
    if (!circular_file_lseek_write_dwords(fd_circ, PSEEK_END, 0, &retptr,
        use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}


    if (!circ_write_n_dwords(fd_circ, current_stream_end, cfilesize,
        &nwritten,TRUE))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    current_stream_end  += cfilesize;
    if (use_stream_seek)
    {
        current_logical_end += cfilesize;
        current_logical_start = current_logical_end-cfilesize;
    }
    /*
<TEST>    verify that seek past end stops at the correct logical eof when the file has wrapped
<TEST>    verify that read stops at the correct logical eof when the file has wrapped
    */
    if (!circular_file_lseek_write_dwords(fd_circ, PSEEK_SET,
        current_logical_start, &retptr, use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (retptr != current_logical_start)
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (!circular_file_lseek_read_dwords(fd_circ, PSEEK_SET,
        current_logical_start, &retptr, use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (retptr != current_logical_start)
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (!circular_file_lseek_write_dwords(fd_circ, PSEEK_SET,
        current_logical_end+1, &retptr, use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (retptr != current_logical_end)
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (!circular_file_lseek_read_dwords(fd_circ, PSEEK_SET,
        current_logical_end+1, &retptr, use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (retptr != current_logical_end)
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}


    if (!circular_file_lseek_write_dwords(fd_circ, PSEEK_SET,
        current_logical_start+1, &retptr, use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (retptr != current_logical_start+1)
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (!circular_file_lseek_read_dwords(fd_circ, PSEEK_SET,
        current_logical_start+1, &retptr,
        use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (retptr != current_logical_start+1)
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (!circular_file_lseek_write_dwords(fd_circ, PSEEK_CUR, cfilesize,
        &retptr, use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (retptr != current_logical_end)
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (!circular_file_lseek_read_dwords(fd_circ, PSEEK_CUR, cfilesize,
        &retptr, use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (retptr != current_logical_end)
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}

    /*
<TEST>    verify that PSEEK_END starts at the correct logical eof and truncates to the correct start of file when the file has wrapped
    */
     if (!circular_file_lseek_write_dwords(fd_circ, PSEEK_END, 0, &retptr,
        use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (retptr != current_logical_end)
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (!circular_file_lseek_read_dwords(fd_circ, PSEEK_END, 0, &retptr,
        use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (retptr != current_logical_end)
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    /* try to seek back filesize*2 + 1.. should go to zero */
    if (!circular_file_lseek_write_dwords(fd_circ, PSEEK_END, cfilesize*2,
        &retptr, use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (retptr != current_logical_start)
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (!circular_file_lseek_read_dwords(fd_circ, PSEEK_END, cfilesize*2,
        &retptr, use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (retptr != current_logical_start)
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}

    /*
<TEST>    verify that PSEEK_CUR_NEG starts at the correct location and truncates to the correct start of file when the file has wrapped
    */
    if (!circular_file_lseek_write_dwords(fd_circ, PSEEK_END, 0,
        &retptr, use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (!circular_file_lseek_read_dwords(fd_circ, PSEEK_END, 0,
        &retptr, use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (retptr != current_logical_end)
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (!circular_file_lseek_write_dwords(fd_circ, PSEEK_CUR_NEG,
        cfilesize/2, &retptr, use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (retptr != (current_logical_end-cfilesize/2))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (!circular_file_lseek_write_dwords(fd_circ, PSEEK_CUR_NEG,
        cfilesize/2, &retptr, use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (retptr != current_logical_start)
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (!circular_file_lseek_write_dwords(fd_circ, PSEEK_CUR_NEG,
        cfilesize, &retptr, use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (retptr != current_logical_start)
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (!circular_file_lseek_read_dwords(fd_circ, PSEEK_CUR_NEG,
        cfilesize, &retptr, use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (retptr != current_logical_start)
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}

    /*
<TEST>    verify that PSEEK_CUR starts at the correct location and truncates to the correct end of file when the file has wrapped
    */
    if (!circular_file_lseek_write_dwords(fd_circ, PSEEK_SET,
        current_logical_start, &retptr, use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (retptr != current_logical_start)
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (!circular_file_lseek_read_dwords(fd_circ, PSEEK_SET,
        current_logical_start, &retptr, use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (retptr != current_logical_start)
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (!circular_file_lseek_write_dwords(fd_circ, PSEEK_CUR,
        cfilesize/2, &retptr, use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (retptr != (current_logical_start+cfilesize/2))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (!circular_file_lseek_read_dwords(fd_circ, PSEEK_CUR,
        cfilesize/2, &retptr, use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (retptr != (current_logical_start+cfilesize/2))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (!circular_file_lseek_write_dwords(fd_circ, PSEEK_CUR,
        cfilesize, &retptr, use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (retptr != current_logical_end)
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (!circular_file_lseek_read_dwords(fd_circ, PSEEK_CUR, cfilesize,
        &retptr, use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (retptr != current_logical_end)
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}

    /*
<TEST>    verify that stream mode seeks of the read pointer clips to the start of the current data window if the poposed new logical file pointer is to the left the data window
    */
    if (!circ_write_n_dwords(fd_circ, current_stream_end, cfilesize,
        &nwritten,TRUE))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    current_stream_end  += cfilesize;
    if (use_stream_seek)
    { /* Check seek set test */
        current_logical_end += cfilesize;
        current_logical_start = current_logical_end-cfilesize;
        if (!circular_file_lseek_read_dwords(fd_circ, PSEEK_SET, 0,
            &retptr, use_stream_seek))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        if (retptr != current_logical_start)
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    }
    /* save the read pointer for later tests */
    if (!circular_file_lseek_read_dwords(fd_circ, PSEEK_CUR, 0,
        &prev_read_pointer, use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}

    /*
<TEST>    verify that stream mode seeks of the read pointer clips to the end of the current data window if the poposed new logical file pointer is to the right of the data window
    */
    if (!circ_write_n_dwords(fd_circ, current_stream_end, cfilesize,
        &nwritten,TRUE))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    current_stream_end  += cfilesize;
    if (use_stream_seek)
    { /* Check seek curr and seek end */
        current_logical_end += cfilesize;
        current_logical_start = current_logical_end-cfilesize;
        /* Make sure PSEEK_CUR,0 returns the current out of bounds pointer */
       if (!circular_file_lseek_read_dwords(fd_circ, PSEEK_CUR, 0,
            &retptr, use_stream_seek))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        if (retptr != prev_read_pointer)
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        /* Check seek strategies from out of bounds.. they should all
           return start of window or end of window */
        /* out of bounds left */
        if (!circular_file_lseek_read_dwords(fd_circ, PSEEK_SET,
            current_logical_start-1, &retptr, use_stream_seek))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        if (retptr != current_logical_start)
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        /* put the pointer out of bounds again */
        if (!circ_write_n_dwords(fd_circ, current_stream_end, cfilesize,
            &nwritten,TRUE))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        current_stream_end  += cfilesize;
        current_logical_end += cfilesize;
        current_logical_start = current_logical_end-cfilesize;
        /* out of bounds right */
        if (!circular_file_lseek_read_dwords(fd_circ, PSEEK_SET,
            current_logical_end+1, &retptr, use_stream_seek))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        if (retptr != current_logical_end)
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        /* put the pointer out of bounds again */
        if (!circ_write_n_dwords(fd_circ, current_stream_end, cfilesize,
            &nwritten,TRUE))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        current_stream_end  += cfilesize;
        current_logical_end += cfilesize;
        current_logical_start = current_logical_end-cfilesize;
        /* Seek curr landing past end*/
        if (!circular_file_lseek_read_dwords(fd_circ, PSEEK_CUR,
            current_logical_end, &retptr, use_stream_seek))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        if (retptr != current_logical_end)
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        /* put the pointer out of bounds again */
        if (!circular_file_lseek_read_dwords(fd_circ, PSEEK_SET, 0, &retptr,
            use_stream_seek))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        if (retptr != current_logical_start)
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        if (!circ_write_n_dwords(fd_circ, current_stream_end, cfilesize,
            &nwritten,TRUE))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        current_stream_end  += cfilesize;
        current_logical_end += cfilesize;
        current_logical_start = current_logical_end-cfilesize;
        /* Seek curr */
        if (!circular_file_lseek_read_dwords(fd_circ, PSEEK_CUR, 1, &retptr,
            use_stream_seek))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        if (retptr != current_logical_start)
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}

        /* put the pointer out of bounds again */
        if (!circ_write_n_dwords(fd_circ, current_stream_end, cfilesize,
            &nwritten,TRUE))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        current_stream_end  += cfilesize;
        current_logical_end += cfilesize;
        current_logical_start = current_logical_end-cfilesize;
        /* Seek curr neg */
        if (!circular_file_lseek_read_dwords(fd_circ, PSEEK_CUR_NEG, 1,
            &retptr, use_stream_seek))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        if (retptr != current_logical_start)
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        /* put the pointer out of bounds again */
        if (!circ_write_n_dwords(fd_circ, current_stream_end, cfilesize,
            &nwritten,TRUE))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        current_stream_end  += cfilesize;
        current_logical_end += cfilesize;
        current_logical_start = current_logical_end-cfilesize;
        /* Seek end */
        if (!circular_file_lseek_read_dwords(fd_circ, PSEEK_END, 1, &retptr,
            use_stream_seek))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        if (retptr != current_logical_end-1)
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    }
    PRO_TEST_ANNOUNCE("Testing seek data tracking ");
    /* first scan the whole file */
    if (!circular_file_lseek_read_dwords(fd_circ, PSEEK_SET, 0, &retptr,
        use_stream_seek))
         {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (!circ_read_n_dwords(fd_circ, current_stream_end - cfilesize,
        cfilesize, &nread))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}

    /*
<TEST>    verify read write tracking, verify that reads from offsets in the file return the data put there by writes to the same offset
    */

    for (loop = 0; loop < MAX_LOOPS; loop++)
    {
        dword value;

        do { ltemp = pro_test_dwrand() % cfilesize; } while (!ltemp);
        value = current_stream_end-cfilesize+ltemp;
        if (!circular_file_lseek_read_dwords(fd_circ, PSEEK_SET, current_logical_start+ltemp, &retptr, use_stream_seek))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        /* In bounds should have moved */
        if (retptr != current_logical_start+ltemp)
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        if (!circ_read_n_dwords(fd_circ, value, 1, &nread))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}

        /* Now write and read back a zero */
        if (!circular_file_lseek_write_dwords(fd_circ, PSEEK_SET,
            current_logical_start+ltemp, &retptr, use_stream_seek))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        /* In bounds should have moved */
        if (retptr != current_logical_start+ltemp)
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        if (!circ_write_n_dwords(fd_circ, 0, 1, &nwritten,TRUE))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        if (!circular_file_lseek_read_dwords(fd_circ, PSEEK_SET,
            current_logical_start+ltemp, &retptr, use_stream_seek))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        /* In bounds should have moved */
        if (retptr != current_logical_start+ltemp)
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        if (!circ_read_n_dwords(fd_circ, 0, 1, &nread))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}

        if (!circular_file_lseek_write_dwords(fd_circ, PSEEK_SET,
            current_logical_start+ltemp, &retptr, use_stream_seek))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        /* In bounds should have moved */
        if (retptr != current_logical_start+ltemp)
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        if (!circ_write_n_dwords(fd_circ, value, 1, &nwritten,TRUE))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    }
    PRO_TEST_ANNOUNCE("");   /* New line */

    /* rescan the whole file */
    if (!circular_file_lseek_read_dwords(fd_circ, PSEEK_SET, 0, &retptr,
        use_stream_seek))
         {ERTFS_ASSERT_TEST(rtfs_debug_zero())}

    pc_cfilio_close(fd_circ);

    /*
<TEST>    verify behavior when write pointer catches read pointer in PCE_CIRCULAR_BUFFER mode.
<TEST>    verify that the write pointer continue past the read pointer
<TEST>    verify that the after being overtaken, reads return 0 bytes
<TEST>    verify that the after being overtaken, seek of the read pointer places the read pointer at the oldest data in the file.
    */
    PRO_TEST_ANNOUNCE("Testing write catching read in buffer mode ");

    /* Test write and read pointer behavior when write pointer overtakes read
       pointer in PCE_CIRCULAR_BUFFER mode */
    /* reopen and truncate file */
    rtfs_memset(&options, 0, sizeof(options));
    options.allocation_policy = PCE_CIRCULAR_BUFFER;
    options.min_clusters_per_allocation = 0;
    options.circular_file_size_hi = cfilesize_hi;
    options.circular_file_size_lo = cfilesize_lo;
    fd_circ = pc_cfilio_open((byte *)filename,
        (word)(PO_BINARY|PO_RDWR|PO_CREAT|PO_TRUNC|PO_BUFFERED),&options);
    ERTFS_ASSERT_TEST(fd_circ >= 0)

    /* Overtake the read pointer in buffered mode. */
    /* The write pointer should continue past the read pointer */
    if (!circ_write_n_dwords(fd_circ, 0, cfilesize*2, &nwritten,TRUE))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (nwritten != cfilesize*2)
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    /* read from the file - first try should read zero dwrords */
    if (!circ_read_n_dwords(fd_circ, cfilesize, cfilesize, &nread))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (nread != 0)
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    /* Now seek the read pointer to beginning of data */
    if (!circular_file_lseek_read_dwords(fd_circ, PSEEK_SET, 0, &retptr,
        use_stream_seek))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    /* Now read the whole file - data should start at cfilesize since
        we overwrote */
    if (!circ_read_n_dwords(fd_circ, cfilesize, cfilesize, &nread))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (nread != cfilesize)
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}

    pc_cfilio_close(fd_circ);

    PRO_TEST_ANNOUNCE("Testing write catching read in file mode ");
    /*
<TEST>    verify behavior when write pointer catches read pointer in PCE_CIRCULAR_FILE mode.
<TEST>    verify that writes stop when the write pointer stops reaches the read pointer.
<TEST>    verify that after reading some bytes that writes continue and then stop again when the write pointer stops reaches the new read pointer.
    */
    rtfs_memset(&options, 0, sizeof(options));
    options.allocation_policy = PCE_CIRCULAR_FILE;
    options.min_clusters_per_allocation = 0;
    options.circular_file_size_hi = cfilesize_hi;
    options.circular_file_size_lo = cfilesize_lo;
    fd_circ = pc_cfilio_open((byte *)filename,
        (word)(PO_BINARY|PO_RDWR|PO_CREAT|PO_TRUNC|PO_BUFFERED),&options);
    if (fd_circ <0)
    {
        PRO_TEST_ANNOUNCE("circular_file_test: Write open failed");
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    }

    /* Overtake the read pointer in file mode. */
    /* The write pointer should stop at the read pointer */
    if (!circ_write_n_dwords(fd_circ, 0, cfilesize*2, &nwritten,TRUE))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (nwritten != cfilesize)
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    /* read from the file - first try should read cfilesize dwrords */
    if (!circ_read_n_dwords(fd_circ, 0, cfilesize, &nread))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (nread != cfilesize)
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    /* Catch up again should stop at the read pointer */
    if (!circ_write_n_dwords(fd_circ, 0, cfilesize*2, &nwritten,TRUE))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (nwritten != cfilesize)
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    /* read from the file - move fp foreward cfilesize-100 */
    if (!circ_read_n_dwords(fd_circ, 0, cfilesize-100, &nread))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (nread != cfilesize-100)
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    /* Catch up again should stop at the read pointer */
    if (!circ_write_n_dwords(fd_circ, 0, cfilesize*2, &nwritten,TRUE))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (nwritten != cfilesize-100)
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}

    pc_cfilio_close(fd_circ);

    /*
<TEST>    Verify behavior of read and write when the file is wrapped verify that data that was written is read back appropriately
<TEST>    verify the read pointer stops when it reaches the write pointer when the file wrap pointer has been passed multiple times
    */
    /* reopen and truncate file */
    rtfs_memset(&options, 0, sizeof(options));
    options.allocation_policy = PCE_CIRCULAR_BUFFER;
    options.min_clusters_per_allocation = 0;
    options.circular_file_size_hi = cfilesize_hi;
    options.circular_file_size_lo = cfilesize_lo;
    fd_circ = pc_cfilio_open((byte *)filename,
        (word)(PO_BINARY|PO_RDWR|PO_CREAT|PO_TRUNC|PO_BUFFERED),&options);
    if (fd_circ <0)
    {
        PRO_TEST_ANNOUNCE("circular_file_test: Write open failed");
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    }
    PRO_TEST_ANNOUNCE("Test wrapping zero multiple times ");
    {
        dword i,j,writes_per_loop;

        current_logical_start      = 0;
        for (i = 0; i < 16;i++) /* file number */
        {
            /* Use large writes otherwise we'll be here all day */
            writes_per_loop = data_buffer_size_dw;
            if (cfilesize < writes_per_loop)
                writes_per_loop = cfilesize/2;

            for (j = 0; j < cfilesize; j += writes_per_loop)
            {
                if (!circ_write_n_dwords(fd_circ, current_logical_start,
                    writes_per_loop, &nwritten,TRUE))
                    {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
                if (nwritten != writes_per_loop)
                    {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
                /* Read twice what we wrote should always return
                    what we wrote */
                if (!circ_read_n_dwords(fd_circ, current_logical_start,
                    writes_per_loop*2, &nread))
                    {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
                if (nread != writes_per_loop)
                    {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
                current_logical_start      += writes_per_loop;
            }
        }
        PRO_TEST_ANNOUNCE("");   /* New line */
    }
    pc_cfilio_close(fd_circ);
    pc_unlink((byte *)filename);
    return;
}



/*
<TEST>    Procedure: circular_file_extract_test()
<TEST>
<TEST>    Test proper functioning of circular file extract
<TEST>
<TEST>    perform the following tests.
<TEST>
<TEST>    One
<TEST>     Fill a circular file with a known pattern
<TEST>     Select a random offset and extract range within the file
<TEST>     Extract the data to a linear file
<TEST>     Compare the contents of the linear file and the region of the circular file it was extracted from. They should match.
<TEST>
<TEST>    Two
<TEST>     Fill a circular file with a known pattern
<TEST>     Select 10 random offsets and extract ranges within the file
<TEST>     Note: all or portions of the extract regions may overlap.
<TEST>     Extract these extract regions to 10 linear files. Compare the contents of the linear files and the region of the
<TEST>     circular file they were extracted from. They should match.
*/

static BOOLEAN do_extract(int fd_circ, dword offset_in_dwords,
                dword length_in_dwords, int lin_fd,
                dword filesize_in_dwords,dword logical_filesize_in_dwords);
static BOOLEAN do_multi_random_extracts(char *filename,dword filesize_in_dwords,
    int nregions, int nloops);
static BOOLEAN do_random_extracts(char *filename, int fd_circ,
    dword filesize_in_dwords, dword nloops);
static char *ext_filename[10] =
{
"EXTRACT_0","EXTRACT_1","EXTRACT_2","EXTRACT_3","EXTRACT_4","EXTRACT_5",
"EXTRACT_6","EXTRACT_7","EXTRACT_8","EXTRACT_9"};

int ext_fds[10];
BOOLEAN fd_is_64[10];
dword ext_offset_in_dwords[10];
dword ext_range_in_dwords[10];

#define COMPLETE_EXTRACT_TEST_FAST 0
static int circular_file_extract_test(char *filename, dword file_size_in_dwords)
{
int i;
#if (COMPLETE_EXTRACT_TEST_FAST)
    if (!do_multi_random_extracts(filename, file_size_in_dwords, 9, 1))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
#else
    if (!do_multi_random_extracts(filename, file_size_in_dwords, 9, 9))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
#endif
#if (COMPLETE_EXTRACT_TEST_FAST)
    for (i = 0; i < 2; i++)
#else
    for (i = 0; i < 10; i++)
#endif
    {
        if (!do_random_extracts(filename, -1, file_size_in_dwords, 10))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    }
    PRO_TEST_ANNOUNCE(" ");
    PRO_TEST_ANNOUNCE("Circular file io extract test completed ");
    pc_unlink((byte *)filename);
    return(0);
}

static BOOLEAN do_random_extracts(char *filename, int fd_circ,
                            dword filesize_in_dwords, dword nloops)
{
dword start_offset,ltemp, size_hi, size_lo,offset_in_dwords,
        range_in_dwords,loop,logical_filesize_in_dwords;
int fd_lin;
ddword realsize_ddw;

    pro_test_print_dword(
                    "Performing random single extracts from file sized (in dwords) ",
                    filesize_in_dwords,
                    PRFLG_NL);

    for (loop=0; loop < nloops; loop++)
    {
        /* Logical size goes from file size to filesize * 3 */
        ltemp = pro_test_dwrand() % filesize_in_dwords;
        logical_filesize_in_dwords =
                 filesize_in_dwords + (2 * ltemp);
        /* Minimum possible start offset */
        start_offset = logical_filesize_in_dwords - filesize_in_dwords;

        do {
            do {
                range_in_dwords = (pro_test_dwrand() % (filesize_in_dwords-1));
            } while (!range_in_dwords);
            do {
                offset_in_dwords   =
                (pro_test_dwrand() % (logical_filesize_in_dwords-1));
            }  while (offset_in_dwords <= start_offset);
        }  while ((range_in_dwords + offset_in_dwords) >
                    logical_filesize_in_dwords);

        size_hi = dwltohighbytes(filesize_in_dwords);
        size_lo = dwltolowbytes(filesize_in_dwords);
        if (size_hi)
        {
            pro_test_print_two_dwords("Extracting to 64 bit file ", range_in_dwords,
                        " dwords, from offset(in dwords) ", offset_in_dwords,
                        PRFLG_NL);
        }
        else
        {
            pro_test_print_two_dwords("Extracting to 32 bit file ", range_in_dwords,
                        " dwords, from offset(in dwords) ", offset_in_dwords,
                        PRFLG_NL);
        }

        fd_circ = reopen_circular_file(filename, size_hi, size_lo,
            &realsize_ddw); /* Reopen every once in a while to defragment */
        if (fd_circ <0)
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        fd_lin = pc_cfilio_extract_file_open((byte *)"EXTRACT_1",
            (BOOLEAN) (size_hi!=0));
        if (fd_lin < 0)
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}

        if (!do_extract(fd_circ, offset_in_dwords, range_in_dwords,
            fd_lin,filesize_in_dwords,logical_filesize_in_dwords))
        {
            if (get_errno() != PETOOLARGE) /* Double check */
            {
                {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
            }
            pc_efilio_close(fd_lin);
            pc_cfilio_close(fd_circ);
        }
        else
        {
            pc_efilio_close(fd_lin);
            pc_cfilio_close(fd_circ);
        }
        if (!pc_unlink((byte *)"EXTRACT_1"))
            { ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    }
    return(TRUE);
}


static void setup_mulitiple_extracts(int n, dword filesize_in_dwords);
static void close_mulitiple_extracts(int n, BOOLEAN delete_extract_files);

static BOOLEAN do_multi_random_extracts(char *filename,dword filesize_in_dwords,
        int nregions, int nloops)
{
dword new_offset, size_hi, size_lo,nread,ltemp,newoffset_hi,newoffset,
      start_val, range_1_in_dwords;
int i, loop, fd_circ;
byte lbyte;
ddword realsize_ddw;
     size_hi = dwltohighbytes(filesize_in_dwords);
     size_lo = dwltolowbytes(filesize_in_dwords);

    pro_test_print_dword(
          "Performing random multiple extracts from file sized (in dwords) ",
           filesize_in_dwords, PRFLG_NL);

    for (loop = 0; loop < nloops; loop++)
    {
        pro_test_print_two_dwords(
            "Multiple region test loop # ", loop,
            " out of : ", nloops,PRFLG_NL);
        /* Opne nregion extract files and set up nregions extract regions,
           the region sizes and offsets range from zero to filesize_in_dwords
           and may/will overlap */
        setup_mulitiple_extracts(nregions, filesize_in_dwords);
        fd_circ = reopen_circular_file(filename, size_hi, size_lo,
            &realsize_ddw); /* Reopen every once in a while to defragment */
        if (fd_circ <0)
            return(FALSE);
         /* Fill the file with dwords and read it back to be sure */
        if (!circ_write_n_dwords(fd_circ, 0, filesize_in_dwords,
            &ltemp, TRUE))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
/*
   Note: The test should be modified to use stream seek and put the
         0 data element an offset into the file to verify succesful
         extracts wrapping the end of file */
#if (0)
        /* Don't read it back, we know it's ok */
        if (!circular_file_lseek_read_dwords(fd_circ, 0, 0,&new_offset,FALSE))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        if (!circ_read_n_dwords(fd_circ, 0, filesize_in_dwords, &nread)
            || nread != filesize_in_dwords)
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
#endif

        /* Extract each of the regions */
        for (i = 0; i < nregions; i++)
        {
        dword blocks_total, blocks_free;
            /* seek back to offset before extracting */
            if (!pc_free(test_driveid, &blocks_total, &blocks_free))
            {
                {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
            }
            if (ext_range_in_dwords[i]/128 > blocks_free)
            {
                pro_test_print_two_dwords(
                    "No room to extract ", ext_range_in_dwords[i],
                    " dwords from : ", ext_offset_in_dwords[i],PRFLG_NL);
                ext_range_in_dwords[i] = 0;
                continue;
            }
            else
            {
                pro_test_print_two_dwords(
                    "Extracting # ", ext_range_in_dwords[i],
                    " dwords from : ", ext_offset_in_dwords[i],PRFLG_NL);
            }
            if (!circular_file_lseek_read_dwords(fd_circ, 0,
                ext_offset_in_dwords[i],&new_offset,FALSE))
                {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
            if (!pc_cfilio_extract(fd_circ, ext_fds[i],
                dwltohighbytes(ext_range_in_dwords[i]),
                dwltolowbytes(ext_range_in_dwords[i]),0,0))
                    {
                    	pro_test_print_two_dwords(
                    		"Failed extracting # ", ext_range_in_dwords[i],
                    		" dwords from : ", ext_offset_in_dwords[i],PRFLG_NL);
                    	pro_test_print_dword( "Files left open.. Errno == :", get_errno(),PRFLG_NL);
						ERTFS_ASSERT_TEST(rtfs_debug_zero())
					}
			/* Loop back */
#define DO_SLOW_READBACK 1
#if (DO_SLOW_READBACK)
			{
			int j;

				for (j = 0; j <= i; j++)
				{
				   if (!pc_efilio_lseek(ext_fds[j], 0, 0, PSEEK_SET, &newoffset_hi,&newoffset) || newoffset != 0)
							{ERTFS_ASSERT_TEST(rtfs_debug_zero())}
					if (!pro_test_read_n_dwords(ext_fds[j], ext_offset_in_dwords[j], ext_range_in_dwords[j],  &nread, TRUE) || ext_range_in_dwords[j]!= nread)
				    {
						rtfs_print_one_string((byte *)"Failed reading linear test on extract file: ", 0);
						rtfs_print_long_1(i, 0);
						rtfs_print_one_string((byte *)"While extracting file : ", 0);
						rtfs_print_long_1(j, PRFLG_NL);
					}
				}
			}
            if (!circular_file_lseek_read_dwords(fd_circ, 0, 0,
                &new_offset,FALSE))
                {ERTFS_ASSERT_TEST(rtfs_debug_zero())}

            if (!circ_read_n_dwords(fd_circ, 0, filesize_in_dwords, &nread)
                || nread != filesize_in_dwords)
            {
               {ERTFS_ASSERT_TEST(rtfs_debug_zero())};
            }
#endif
        } /*  for (i = 0; i < nregions; i++) */
        pro_test_print_dword("Verify circular after extracts size : ",  filesize_in_dwords, PRFLG_NL);
        /* Now validate the contents of extracted regions */
        /* sanity check read 0 to end of circular file */
        if (!circular_file_lseek_read_dwords(fd_circ, 0, 0,&new_offset,
                 FALSE))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        if (!circ_read_n_dwords(fd_circ, 0, filesize_in_dwords, &nread) || nread != filesize_in_dwords)
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        for (i = 0; i < nregions; i++)
        {
            if (!ext_range_in_dwords[i])
            {
                pro_test_print_dword("Skipping verfification of ", i,PRFLG_NL);
                continue;
            }
            pro_test_print_two_dwords(
                "Verifying Extract of  ", ext_range_in_dwords[i],
                " dwords from : ", ext_offset_in_dwords[i],PRFLG_NL);
            if (!circular_file_lseek_read_dwords(fd_circ, 0,
                ext_offset_in_dwords[i],&new_offset,FALSE))
                {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
            start_val = ext_offset_in_dwords[i];
            range_1_in_dwords = ext_range_in_dwords[i];
            if (!circular_file_lseek_read_dwords(fd_circ, 0,
                ext_offset_in_dwords[i],&new_offset,FALSE))
                {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
			{
			if (!pc_efilio_lseek(ext_fds[i], 0, 0, PSEEK_SET, &newoffset_hi,&newoffset) || newoffset != 0)
				    {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
			if (!pro_test_read_n_dwords(ext_fds[i], ext_offset_in_dwords[i], ext_range_in_dwords[i],  &nread, TRUE) || ext_range_in_dwords[i]!= nread)
                {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
			}
            if (!pc_efilio_lseek(ext_fds[i], 0, 0, PSEEK_SET, &newoffset_hi,
                &newoffset) || newoffset != 0)
                {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
             if (!lin_circ_compare(fd_circ, ext_fds[i], range_1_in_dwords,start_val))
             {
				 /* Reared the linear file */
				{
				if (!pc_efilio_lseek(ext_fds[i], 0, 0, PSEEK_SET, &newoffset_hi,&newoffset) || newoffset != 0)
				    {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
				if (!pro_test_read_n_dwords(ext_fds[i], ext_offset_in_dwords[i], ext_range_in_dwords[i],  &nread, TRUE) || ext_range_in_dwords[i]!= nread)
					{ERTFS_ASSERT_TEST(rtfs_debug_zero())}
				}
				ERTFS_ASSERT_TEST(rtfs_debug_zero())
			 }
            /* make sure linear file is at eof */
            if (!pc_efilio_read(ext_fds[i],(byte*)&lbyte,1,&nread) ||
                 nread != 0)
              {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        } /*  for (i = 0; i < nregions; i++) */
        if (!pc_cfilio_close(fd_circ))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        if (nloops > 1)
            close_mulitiple_extracts(nregions, TRUE);
        else
            close_mulitiple_extracts(nregions, FALSE);
    } /* nloops */
    return(TRUE);
}



static BOOLEAN do_extract(int fd_circ, dword offset_in_dwords,
        dword length_in_dwords, int lin_fd, dword filesize_in_dwords,
        dword logical_filesize_in_dwords)
{
dword new_offset,ltemp,
      newoffset,newoffset_hi,
      nread,file_start_val,start_val;
byte  lbyte;
PC_FILE *pefile, *preaderefile;

    pefile = pc_cfilio_fd2file(fd_circ, TRUE); /* and unlock */
    preaderefile = pefile->fc.plus.psibling;
    ERTFS_ASSERT_TEST(lin_fd >= 0)
    start_val = offset_in_dwords;
    file_start_val = logical_filesize_in_dwords - filesize_in_dwords;

    /* Fill the file with dwords */
    if (!circ_write_n_dwords(fd_circ, 0, logical_filesize_in_dwords,
        &ltemp,TRUE))
    {
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    }
    /* Get to the begining of the data */
    if (!circular_file_lseek_read_dwords(fd_circ, PSEEK_SET,
        logical_filesize_in_dwords-filesize_in_dwords,&new_offset,TRUE))
    {
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    }

    /* sanity check read 0 to end of file make sure all is ok */
    if (!circ_read_n_dwords(fd_circ, file_start_val, filesize_in_dwords,
        &nread) || nread != filesize_in_dwords)
        return(FALSE);
    /* seek back to offset before extracting */
    if (!circular_file_lseek_read_dwords(fd_circ, PSEEK_SET,
        offset_in_dwords,&new_offset,TRUE))
    {
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    }

    if (!pc_cfilio_extract(fd_circ, lin_fd,
        dwltohighbytes(length_in_dwords),dwltolowbytes(length_in_dwords),0,0))
    {
        if (get_errno() == PETOOLARGE)
        {
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
            return(FALSE);
        }
		PRO_TEST_ANNOUNCE("Extract failed, continuing to next combination");
		return(TRUE);
    }

    if (!pc_efilio_lseek(lin_fd, 0, 0, PSEEK_SET, &newoffset_hi, &newoffset)
        || newoffset != 0)
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    if (!pro_test_read_n_dwords(lin_fd, start_val, length_in_dwords, &nread,TRUE))
    {
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    }
    if (!pc_efilio_read(lin_fd,(byte*)&lbyte,1,&nread) || nread != 0)
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    /* sanity check read 0 to end of file make sure all is ok */
    if (!circular_file_lseek_read_dwords(fd_circ, PSEEK_SET,
        logical_filesize_in_dwords-filesize_in_dwords,&new_offset,TRUE))
    {
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    }
    if (!circ_read_n_dwords(fd_circ, file_start_val, filesize_in_dwords,
        &nread) || nread != filesize_in_dwords)
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}

    return(TRUE);
}

static void setup_mulitiple_extracts(int n, dword filesize_in_dwords)
{
int i;

dword offset_in_dwords,range_in_dwords;
BOOLEAN is_64;
    /* Open the extract files */
    for (i = 0; i < n; i++)
    {
        if (filesize_in_dwords >= 1024*1024*1024)
            is_64 = TRUE;
        else
            is_64 = FALSE;
        ext_fds[i] =  pc_cfilio_extract_file_open((byte *)ext_filename[i],
            is_64);
        fd_is_64[i] = is_64;

        if (ext_fds[i] < 0)
        {
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        }
    }
    /* generate extract regions */
#if (1)
// FLUSH_LEFT==1&&FLUSH_RIGHT == 1 works
// FLUSH_LEFT==0&&FLUSH_RIGHT == 1 fails
// FLUSH_LEFT==1&&FLUSH_RIGHT == 0 works
// FLUSH_LEFT==0&&FLUSH_RIGHT == 1 fails With FIX
// FLUSH_LEFT==0&&FLUSH_RIGHT == 1 works if the files are deleted and reloaded each time (no extract regions in the file being extracted)
//
// Now test readding all extract files after each extract to determine when it fails
//
#define FLUSH_LEFT  0
#define FLUSH_RIGHT 0
	{
dword dwordspercluster_mask,dwordspercluster;

	dwordspercluster=pro_test_bytes_per_cluster()/4;

	dwordspercluster_mask = ~(dwordspercluster-1);
    for (i = 0; i < n; i++)
    {
       do {
            do {
				range_in_dwords = (pro_test_dwrand() % (filesize_in_dwords-1));
#if (FLUSH_LEFT&&FLUSH_RIGHT)
				range_in_dwords &= dwordspercluster_mask;
#endif
          } while (!range_in_dwords);
          offset_in_dwords   = (pro_test_dwrand() % (filesize_in_dwords-1));
#if (FLUSH_LEFT)
		  offset_in_dwords &= dwordspercluster_mask;
#endif
#if (!FLUSH_LEFT&&FLUSH_RIGHT)
		  {
			dword bounded_total= (offset_in_dwords + range_in_dwords) & dwordspercluster_mask;
			range_in_dwords=bounded_total-offset_in_dwords;
		  }
#endif
       }  while (range_in_dwords==0 || ((range_in_dwords + offset_in_dwords) > filesize_in_dwords));
       ext_offset_in_dwords[i] = offset_in_dwords;
       ext_range_in_dwords[i] =  range_in_dwords;
    }
	}
#else
   for (i = 0; i < n; i++)
    {
       do {
            do {range_in_dwords = (pro_test_dwrand() % (filesize_in_dwords-1));
          } while (!range_in_dwords);
          offset_in_dwords   = (pro_test_dwrand() % (filesize_in_dwords-1));
       }  while ((range_in_dwords + offset_in_dwords) > filesize_in_dwords);

       ext_offset_in_dwords[i] = offset_in_dwords;
       ext_range_in_dwords[i] =  range_in_dwords;
    }
#endif
}

static void close_mulitiple_extracts(int n, BOOLEAN delete_extract_files)
{
int i;
    for (i = 0; i < n; i++)
    {
        if (fd_is_64[i])
            pro_test_print_two_dwords("Closing and deleting 64 bit file sized: ", ext_range_in_dwords[i],
                        " dwords, from offset(in dwords) ", ext_offset_in_dwords[i],
                        PRFLG_NL);
        else
            pro_test_print_two_dwords("Closing and deleting 64 bit file sized: ", ext_range_in_dwords[i],
                        " dwords, from offset(in dwords) ", ext_offset_in_dwords[i],
                        PRFLG_NL);
        if (!pc_efilio_close(ext_fds[i]))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
            if (delete_extract_files)
            {
                if (!pc_unlink((byte *)ext_filename[i]))
                {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
            }
    }
}

static int pc_cfilio_extract_file_open(byte *remap_filename, BOOLEAN is_64)
{
    EFILEOPTIONS options;
    rtfs_memset(&options, 0, sizeof(options));
    options.allocation_policy = PCE_REMAP_FILE;
    options.min_clusters_per_allocation = 0;

    return(pc_efilio_open((byte *)remap_filename,
        (word)(PO_BINARY|PO_RDWR|PO_TRUNC|PO_CREAT),
        (word)(PS_IWRITE | PS_IREAD),&options));
}


static BOOLEAN lin_circ_compare(int fd_circ, int lin_fd, dword length_in_dwords,dword expected_value)
{
dword l,ltemp,linread, circread, half_buffer_size_dw;
dword *lin, *circ;
dword tlinread, tcircread;
dword linear_offset_hi,linear_offset_lo,ltemp_hi,ltemp_lo;
dword circular_base_hi;
ddword circular_base_lo;
dword circular_offset_hi,circular_offset_lo;
ddword linear_offset_ddw,circular_offset_ddw;

tlinread = tcircread = 0;

    /* Split the data buffer in two */
    half_buffer_size_dw = data_buffer_size_dw/2;
    circ = (dword *)data_buffer;
    lin = circ + half_buffer_size_dw;

	/* Added logic to manually reset seek pointers */
	pc_efilio_lseek(lin_fd, 0, 0, PSEEK_CUR, &linear_offset_hi,&linear_offset_lo);
    pc_cstreamio_lseek(fd_circ, CFREAD_POINTER,0,0,PSEEK_CUR, &circular_offset_hi, &circular_offset_lo);
	linear_offset_ddw=M64SET32(linear_offset_hi,linear_offset_lo);

	circular_offset_ddw=M64SET32(circular_offset_hi,circular_offset_lo);
	circular_base_hi=circular_offset_hi;
	circular_base_lo=circular_offset_lo;
    while (length_in_dwords)
    {
        pro_test_print_two_dwords("lin circ compare ncompared == : ", tlinread/4,
                        " n left == : ", length_in_dwords,PRFLG_CR);
        if (length_in_dwords > half_buffer_size_dw)
            ltemp = half_buffer_size_dw;
        else
            ltemp = length_in_dwords;
        ltemp *= 4;
        circ = (dword *)data_buffer;
        lin = circ + half_buffer_size_dw;
        if (!pc_cfilio_read(fd_circ,(byte*)circ,ltemp,&circread))
           return(FALSE);
		{
		dword lin_saved_hi,lin_saved_lo;
		pc_efilio_lseek(lin_fd, 0, 0, PSEEK_CUR, &lin_saved_hi,&lin_saved_lo);
		pc_efilio_lseek(lin_fd, 0, 0, PSEEK_SET, &ltemp_hi,&ltemp_lo);
 		pc_efilio_lseek(lin_fd, linear_offset_hi, linear_offset_lo, PSEEK_SET, &ltemp_hi,&ltemp_lo);
        if (!pc_efilio_read(lin_fd,(byte *)lin,ltemp,&linread))
           return(FALSE);
		linear_offset_ddw=M64PLUS32(linear_offset_ddw,linread);
		linear_offset_hi=M64HIGHDW(linear_offset_ddw);
		linear_offset_lo=M64LOWDW(linear_offset_ddw);

		}
        tlinread += linread/4;
        tcircread += circread/4;
        for (l = 0; l < ltemp/4; l++, lin++, circ++,expected_value++)
        {
            if (*lin != *circ)
            {
			    PRO_TEST_ANNOUNCE("lin circ compare error");
               pro_test_print_two_dwords(
                    "lin circ compare failed at dword :" , tlinread + l,
                     "*circ=: ", *circ,PRFLG_NL);
               pro_test_print_two_dwords(
				   "*lin=: ", *lin,
                    "expected value ", expected_value,
                     PRFLG_NL);
                 return(FALSE);
            }
			if (*lin != expected_value)
			{
			    PRO_TEST_ANNOUNCE("lin circ unexpected value in both files...");
                pro_test_print_two_dwords(
                    "expected:" , expected_value,
                     "realized: ", *lin,PRFLG_NL);
                return(FALSE);
			}
        }
        if (linread != ltemp)
        {
            PRO_TEST_ANNOUNCE("Short lin read in lin circ compare");
            return(FALSE);
        }
        if (circread != ltemp)
        {
            PRO_TEST_ANNOUNCE("Short circ read in lin circ compare");
            return(FALSE);
        }
        length_in_dwords -= ltemp/4;
    }
    PRO_TEST_ANNOUNCE(" ");
    return(TRUE);
}

#define NUMREMAPS 20
static REMAP_RECORD remap_records[NUMREMAPS];

static int reopen_circular_file(char *filename, dword size_hi,
                         dword size_lo,ddword *pfilesize_ddw)
{
int fd_circ;
EFILEOPTIONS options;
PC_FILE *pwriterefile;

    rtfs_memset(&options, 0, sizeof(options));
    options.allocation_policy = PCE_CIRCULAR_BUFFER;
    options.min_clusters_per_allocation = 0;
    options.circular_file_size_hi = size_hi;
    options.circular_file_size_lo = size_lo;
    options.n_remap_records = NUMREMAPS;
    options.remap_records   = &remap_records[0];
    fd_circ = pc_cfilio_open((byte *)filename,
        (word)(PO_BINARY|PO_RDWR|PO_CREAT|PO_TRUNC),&options);
    if (fd_circ <0)
    {
        PRO_TEST_ANNOUNCE("circular_file_test: Write open failed");
        return(-1);
    }
    pwriterefile = pc_cfilio_fd2file(fd_circ,TRUE);
    if (!pwriterefile)
         return(-1);
    *pfilesize_ddw    =  pwriterefile->fc.plus.circular_file_size_ddw;
    return(fd_circ);
}



static BOOLEAN circ_write_n_dwords(int fd, dword value, dword count,
                            dword *pnwritten, BOOLEAN do_increment)
{
dword ntowrite,nwritten,n_left,nloops;
BOOLEAN clear_line = FALSE;
    *pnwritten = 0;
    n_left = count;
    nloops = 0;
    while (n_left)
    {
       if (nloops++ > 16) /* Only print on big transfers */
       {
            pro_test_print_two_dwords("circ_write completed: ", *pnwritten," n left: ", n_left,0);
            RTFS_PRINT_STRING_1((byte *)"        ", PRFLG_CR);
            nloops = 0;
            clear_line = TRUE;
       }
        if (n_left >= data_buffer_size_dw)
            ntowrite = data_buffer_size_dw;
        else
            ntowrite = n_left;
        if (do_increment)
        {
            pro_test_fill_buffer_dwords((dword *)data_buffer, value, ntowrite);
            value += ntowrite;
        }
        else
            pro_test_set_buffer_dwords((dword *)data_buffer, value, ntowrite);
        if (!pc_cfilio_write(fd, (byte*)data_buffer, ntowrite*4, &nwritten))
		{
			PRO_TEST_ANNOUNCE("pc_cfilio_write failed");
			return(FALSE);
		}
		*pnwritten += nwritten/4;
        /* on EOF pnwritten says how many were read */
        if (nwritten != ntowrite*4)
            return(TRUE);

        n_left -= nwritten/4;
    }
    if (clear_line)
    {
        PRO_TEST_ANNOUNCE(" ");
    }
    return(TRUE);
}

static BOOLEAN circ_read_n_dwords(int fd, dword value, dword count, dword *pnread)
{
dword ntoread,nread,n_left,nloops,first_error,error_count,next_contig_error,first_error_value;
BOOLEAN clear_line = FALSE;
PC_FILE *pfile = prtfs_cfg->mem_file_pool+fd;
PC_FILE *preader= pfile->fc.plus.psibling;

	first_error=0xffffffff;
	first_error_value=next_contig_error=error_count=0;

    *pnread = 0;
    n_left = count;
    nloops = 0;
    while (n_left)
    {
       if (nloops++ > 16) /* Only print on big transfers */
       {
            pro_test_print_two_dwords("circ_read completed: ", *pnread, " n left: ", n_left,0);
            RTFS_PRINT_STRING_1((byte *)"        ", PRFLG_CR);
            nloops = 0;
            clear_line = TRUE;
       }

        if ((n_left*4) >= data_buffer_size_dw)
            ntoread = data_buffer_size_dw;
        else
            ntoread = n_left;
        if (!pc_cfilio_read(fd, (byte*)data_buffer, ntoread*4, &nread))
        {
            PRO_TEST_ANNOUNCE (" ");
            PRO_TEST_ANNOUNCE("pc_cfilio_read failed");
            return(FALSE);
        }
        if (nread == 0)   /* EOF pnread says how many were read */
            return(TRUE);

        *pnread += nread/4;
        if (nread && (value != LARGEST_DWORD))
        {
        dword ltemp;
            ltemp = pro_test_check_buffer_dwords((dword *)data_buffer, value, nread/4);

            if (ltemp != nread/4)
            {
				if (first_error==0xffffffff)
				{

					first_error=value+ltemp;
					next_contig_error=value+(nread/4);
					first_error_value= *((dword *)(data_buffer + ltemp*4));
					rtfs_print_one_string((byte *)"Error. Index == : ", 0);
					rtfs_print_long_1(first_error, 0);
					rtfs_print_one_string((byte *)"Value == : ", 0);
					rtfs_print_long_1(first_error_value, PRFLG_NL);
					rtfs_print_one_string((byte *)"While extracting file : ", 0);
					rtfs_print_one_string((byte *)"first_error offset in clusters (if 32k cluster) == :", 0);
					rtfs_print_long_1(first_error/8192, PRFLG_NL);
					dump_circ_file(preader);

				}
				else
				{
					if (next_contig_error==(value+ltemp))
						next_contig_error+=(nread/4);
					else
					{
						first_error=0xffffffff;
					}
				}
				error_count += 1;

				// return(FALSE);
            }
			else
			{
				first_error=0xffffffff;
				first_error_value= *((dword *)(data_buffer + ltemp*4));
			}
            value += nread/4;
        }
        n_left -= nread/4;
    }

	if (error_count)
	{
		PRO_TEST_ANNOUNCE (" ");
        PRO_TEST_ANNOUNCE ("Check buffer failed");
		return FALSE;
	}
    if (clear_line)
    {
        PRO_TEST_ANNOUNCE (" ");
    }
    return TRUE;
}


static BOOLEAN circular_file_lseek_write_dwords(int fd, int origin,
        dword offset_in_dwords, dword *poffset,BOOLEAN use_stream_seek)
{
dword rhi, rlo;

    if (use_stream_seek)
    {
        if (!pc_cstreamio_lseek(fd, CFWRITE_POINTER,
            dwltohighbytes(offset_in_dwords),
            dwltolowbytes(offset_in_dwords),
            origin, &rhi, &rlo))
            return(FALSE);

    }
    else
    {
        if (!pc_cfilio_lseek(fd, CFWRITE_POINTER,
            dwltohighbytes(offset_in_dwords),
            dwltolowbytes(offset_in_dwords),
            origin, &rhi, &rlo))
            return(FALSE);
    }
    *poffset = highlowbytestodw(rhi, rlo);
    return(TRUE);
}

static BOOLEAN circular_file_lseek_read_dwords(int fd, int origin,
        dword offset_in_dwords, dword *poffset,BOOLEAN use_stream_seek)
{
dword rhi, rlo;
    if (use_stream_seek)
    {
        if (!pc_cstreamio_lseek(fd, CFREAD_POINTER,
            dwltohighbytes(offset_in_dwords),
            dwltolowbytes(offset_in_dwords),
            origin, &rhi, &rlo))
            return(FALSE);
    }
    else
    {
        if (!pc_cfilio_lseek(fd, CFREAD_POINTER,
            dwltohighbytes(offset_in_dwords),
            dwltolowbytes(offset_in_dwords),
            origin, &rhi, &rlo))
            return(FALSE);
    }
    *poffset = highlowbytestodw(rhi, rlo);
    return(TRUE);
}

static dword dwltohighbytes(dword length_in_dwords)
{
ddword length_ddw;
   length_ddw = M64SET32(0,length_in_dwords);
   length_ddw = M64LSHIFT(length_ddw,2);
   return(M64HIGHDW(length_ddw));
}
static dword dwltolowbytes(dword length_in_dwords)
{
ddword length_ddw;
   length_ddw = M64SET32(0,length_in_dwords);
   length_ddw = M64LSHIFT(length_ddw,2);
   return(M64LOWDW(length_ddw));
}
static dword highlowbytestodw(dword hi, dword lo)
{
ddword ddw;
    ddw = M64SET32(hi,lo);
    ddw = M64RSHIFT(ddw,2); /* Divide 4 */
    return(M64LOWDW(ddw));
}

void format_64(char *buffer, ddword ddw, int precision);

static void dump_circ_file(PC_FILE *preader)
{
ddword start_ddw=0;
ddword max_ddw;
REMAP_RECORD *remapped_regions=preader->fc.plus.remapped_regions;

#if (INCLUDE_EXFATORFAT64)
	if (ISEXFATORFAT64(preader->pobj->pdrive))
		max_ddw=preader->fc.plus.ffinode->fsizeu.fsize64;
	else
#endif
		max_ddw=M64SET32(0,preader->fc.plus.ffinode->fsizeu.fsize);

	rtfs_print_one_string((byte *)"Circular file [] == Native >< == mapped ",PRFLG_NL);
	while (start_ddw < max_ddw)
	{
	byte buff[32];
		if (!remapped_regions)
		{
			/* ("[%I64d-%I64d]",start_ddw, max_ddw-1); */
			rtfs_print_one_string((byte *)"[", 0);
			format_64((char*)buff,start_ddw, 12);
			rtfs_print_one_string(buff, 0);
			rtfs_print_one_string((byte *)"-", 0);
			format_64((char*)buff,max_ddw-1, 12);
			rtfs_print_one_string(buff, 0);
			rtfs_print_one_string((byte *)"]", 0);

			break;
		}
		if (remapped_regions->remap_fpoff_start_ddw > start_ddw)
		{
			/* ("[%I64d-%I64d]",start_ddw, remapped_regions->remap_fpoff_start_ddw-1); */
			rtfs_print_one_string((byte *)"[", 0);
			format_64((char*)buff,start_ddw, 12);
			rtfs_print_one_string(buff, 0);
			rtfs_print_one_string((byte *)"-", 0);
			format_64((char*)buff,remapped_regions->remap_fpoff_start_ddw-1, 12);
			rtfs_print_one_string(buff, 0);
			rtfs_print_one_string((byte *)"]", 0);
			start_ddw=remapped_regions->remap_fpoff_start_ddw;
		}
		else
		{
			/* (">%I64d-%I64d<", remapped_regions->remap_fpoff_start_ddw, remapped_regions->remap_fpoff_end_ddw); */
			rtfs_print_one_string((byte *)">", 0);
			format_64((char*)buff, remapped_regions->remap_fpoff_start_ddw, 12);
			rtfs_print_one_string(buff, 0);
			rtfs_print_one_string((byte *)"-", 0);
			format_64((char*)buff,remapped_regions->remap_fpoff_end_ddw,12);
			rtfs_print_one_string(buff, 0);
			rtfs_print_one_string((byte *)"<", 0);
			start_ddw=remapped_regions->remap_fpoff_end_ddw+1;
			remapped_regions=remapped_regions->pnext;
		}
	}
}


#endif /* (!INCLUDE_DEBUG_TEST_CODE) else.. */
#endif /* Exclude from build if read only */
