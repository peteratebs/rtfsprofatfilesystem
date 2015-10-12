/*
<TEST>  Test File:   rtfscommon/source/apiregress.c
<TEST>
<TEST>   Procedure:BOOLEAN pc_regression_test()
<TEST>   Description:  Rtfs baseline feature set regression test suite.
<TEST>   This subroutine calls all or most of the API routines while stress testing the system for driver bugs, memory leaks and freespace leaks.
<TEST>   The routine may be inoked by typing REGRESS D: from the command shell
<TEST>
*/

#include "rtfs.h"


#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */

#define RGE_FLTDRIVE       3
#define RGE_FREEERROR      4
#define RGE_LEAKERROR      5
#define RGE_MKDIR          6
#define RGE_SCWD           7
#define RGE_MKDIRERR       9
#define RGE_PWD           10
#define RGE_RMDIR         11
#define RGE_DSKCLOSE      12
#define RGE_OPEN          13
#define RGE_SEEK          14
#define RGE_WRITE         15
#define RGE_READ          16
#define RGE_TRUNC         17
#define RGE_FLUSH         18
#define RGE_CLOSE         19
#define RGE_UNLINK        20
#define RGE_MV            21
#define RGE_CHSIZE        22
#define RGE_DELTREE       23
#define RGE_ERRNO         24
#define RGE_LONGFILETEST  25
#define RGE_FILETEST      26
#define RGE_GFIRST        27
#define RGE_GREAD         28
#define RGE_LARGEFILETEST 29
#define RGE_CLUSTERCONVERSIONTEST 30
#define RGE_APPENDFILETEST 31

/* Porting issues */
#define VERBOSE   1           /* Set to zero for quiet operation */
byte    test_drive[8];        /* The drive where the test will occur */
#define FIVE12      512       /* Not sector size dependant */
#define NLONGS      FIVE12    /* Longs to write in file write test */
#define SUBDIRDEPTH  10       /* Depth of subdirectories */
#define NSUBDIRS     8        /* <= 26 Number of subdirs at below RTFSTEST */

dword test_rtfs_buf[NLONGS];      /* Used in the file write test */


#if (INCLUDE_VFAT && INCLUDE_CS_UNICODE)  /* Small piece of compile time VFAT vs's NONVFAT code  */
/* Make names a little shorter in Unicode, otherwise path's get too long */
KS_CONSTANT byte * _utest_dir = (byte *) L"Directory";       /* Test will occur in this Directory */
KS_CONSTANT byte * _utest_file_name = (byte *) L"Long File Name";
KS_CONSTANT byte * _utest_newfile_name = (byte *) L"New Long File Name";
KS_CONSTANT byte * _utest_subdir_name =  (byte *) L"SUBDIR";
#endif

#if (INCLUDE_VFAT)  /* Small piece of compile time VFAT vs's NONVFAT code  */
KS_CONSTANT byte _test_dir[] = "RTFS_Test_Directory";       /* Test will occur in this Directory */
KS_CONSTANT byte _test_file_name[] = "Long File Name";
KS_CONSTANT byte _test_newfile_name[] = "New Long File Name";
KS_CONSTANT byte _test_subdir_name[] =  "SUBDIR";
#else
KS_CONSTANT byte _test_dir[] = "RTFSTEST";      /* Test will occur in this Directory */
KS_CONSTANT byte _test_file_name[] = "FILE";
KS_CONSTANT byte _test_newfile_name[] = "NEWFILE";
KS_CONSTANT byte _test_subdir_name[] =  "SUBDIR";
#endif
/* Above strings are copied into the following buffers in native cha set */
byte test_dir[40];
byte test_file_name[40];
byte test_newfile_name[40];
byte test_subdir_name[40];


#define INNERLOOP  2               /* Number of times we run the tes suite
                                      between open and close. */
#define OUTERLOOP  2               /* Number of times we open the drive
                                      run the inner loop and close the drive */

static BOOLEAN do_test(int loop_count, BOOLEAN do_clean, int use_charset);
static BOOLEAN do_file_test(int loop_count, BOOLEAN do_clean);
static BOOLEAN do_append_test(void);
static BOOLEAN do_po_lseek_test(void);
#if (INCLUDE_VFAT)
static BOOLEAN do_long_file_test(BOOLEAN do_clean,int use_charset);
static BOOLEAN do_more_long_file_tests(BOOLEAN do_clean,int use_charset);
#endif

#if (INCLUDE_MATH64)
static BOOLEAN do_comprehensive_filio_test(void);
#endif


static BOOLEAN do_rm(byte *buffer, int level, int use_charset);
static BOOLEAN check_errno(int expected_error);
static BOOLEAN do_gread_file_test(void);
static BOOLEAN do_buffered_file_test(BOOLEAN do_clean);
static BOOLEAN do_chkdsk_test(void);
#if (INCLUDE_RTFS_PROPLUS)
static BOOLEAN do_cluster_conversion_test(BOOLEAN raw);
#endif
static BOOLEAN check_if_exfat(void);

#define regress_error(E) _regress_error(E, __LINE__)
static void _regress_error(int error, long linenumber)
{
    RTFS_PRINT_STRING_1((byte *)"", PRFLG_NL);
    RTFS_PRINT_STRING_1((byte *)" regress_error was called line number: (", 0); /* "regress_error was called with error" */
    RTFS_PRINT_LONG_1((dword) linenumber, 0);
    RTFS_PRINT_STRING_1((byte *)") error: ", 0); /* "regress_error was called with error" */
    RTFS_PRINT_LONG_1((dword) error, PRFLG_NL);
	ERTFS_ASSERT_TEST(rtfs_debug_zero())
    return;
}

/* Copy strings to native character sets */
static void setup_regress_strings(int use_charset)
{
#if (INCLUDE_VFAT && INCLUDE_CS_UNICODE)  /* Small piece of compile time VFAT vs's NONVFAT code  */
    if (use_charset == CS_CHARSET_UNICODE)
    {
        rtfs_cs_strcpy(test_dir, (byte *) _utest_dir, use_charset);
        rtfs_cs_strcpy(test_file_name, (byte *) _utest_file_name, use_charset);
        rtfs_cs_strcpy(test_newfile_name, (byte *) _utest_newfile_name, use_charset);
        rtfs_cs_strcpy(test_subdir_name, (byte *) _utest_subdir_name, use_charset);
        return;
    }
#endif
    /* Set up strings in native character set */
    rtfs_cs_strcpy(test_dir, (byte *) _test_dir, use_charset);
    rtfs_cs_strcpy(test_file_name, (byte *) _test_file_name, use_charset);
    rtfs_cs_strcpy(test_newfile_name, (byte *) _test_newfile_name, use_charset);
    rtfs_cs_strcpy(test_subdir_name, (byte *) _test_subdir_name, use_charset);
}


#define PATHSIZE 256

static BOOLEAN do_comprehensive_filio_seek(void);


//===
static DDRIVE *path_to_drive_struct(byte *path);
extern byte cmdshell_check_disk_scratch_memory[];
extern dword cmdshell_size_check_disk_scratch_memory;

void _debug_chkdsk(void)
{
CHKDISK_STATS chkstat;
CHKDSK_CONTEXT chkcontext;
/*dword current_lost_cluster,current_crossed_points,current_bad_lfns; */
DDRIVE *test_drive_structure;

	test_drive_structure = path_to_drive_struct(test_drive);
	if (!test_drive_structure)
		return;

	/* Scan the disk first */
    if (!pc_check_disk_ex(test_drive, &chkstat, TRUE, &chkcontext, (void *)&cmdshell_check_disk_scratch_memory[0], cmdshell_size_check_disk_scratch_memory)
		|| chkstat.has_errors)
    {
       rtfs_print_one_string((byte *)"Checkdisk failed", PRFLG_NL);
    }
}
//===
BOOLEAN pc_regression_test(byte *driveid, BOOLEAN do_clean) /* __api__ */
{
int inner_loop;
int outer_loop;
//int use_charset = CS_CHARSET_UNICODE;
int use_charset = CS_CHARSET_NOT_UNICODE; /* not tested with CS_CHARSET_UNICODE */
dword blocks_total, blocks_free, blocks_free_too;

    rtfs_cs_strcpy(test_drive, driveid, CS_CHARSET_NOT_UNICODE);
    setup_regress_strings(use_charset);
    for (outer_loop = 0; outer_loop < OUTERLOOP; outer_loop++)
    {
#if (VERBOSE)
    RTFS_PRINT_LONG_1((dword) outer_loop, PRFLG_NL);
#endif

        if (!pc_set_default_drive(test_drive))
            { regress_error(RGE_FLTDRIVE); return(FALSE);}
#if (INCLUDE_RTFS_PROPLUS)
        /* September 2007.. Add call */
        if (!do_cluster_conversion_test(TRUE))  /* Raw */
            return(FALSE);
        if (!do_cluster_conversion_test(FALSE))
            return(FALSE);
#endif
        /* If test_dir is not there, deltree will just fail */
        pc_deltree(test_dir);
        /* Test buffered IO support. Loop a few times to check for leaks */
        for (inner_loop = 0; inner_loop < INNERLOOP; inner_loop++)
        {
            RTFS_PRINT_STRING_1((byte *)"Performing buffered file io test", PRFLG_NL); /* "Performing buffered file io test" */
            if (!do_buffered_file_test(do_clean))
              return(FALSE);
            RTFS_PRINT_STRING_1((byte *)" Performing pc_gread test", PRFLG_NL);
           if (!do_gread_file_test())
                return(FALSE);
        }
        for (inner_loop = 0; inner_loop < INNERLOOP; inner_loop++)
        {

            /* Check freespace */
            if (!pc_blocks_free(test_drive, &blocks_total, &blocks_free))
            {
                regress_error(RGE_FREEERROR);
                return(FALSE);
            }
            if (!do_test(inner_loop, do_clean,use_charset))  /* Call the main test routine */
                return(FALSE);
            /* Check freespace again. They should match */
            if (!do_clean)   /* If not cleaning up don't recheck freespace */
                return(TRUE);/* And return */
            if (!pc_blocks_free(test_drive, &blocks_total, &blocks_free_too))
            {
                regress_error(RGE_FREEERROR);
                return(FALSE);
            }
            if (blocks_free_too != blocks_free)
            {
            	RTFS_PRINT_STRING_1((byte *)"Leak detected: blocks free : ", PRFLG_NL);
            	RTFS_PRINT_LONG_1((dword) blocks_free_too, 0);
            	RTFS_PRINT_STRING_1((byte *)") Should be: ", 0); /* "regress_error was called with error" */
            	RTFS_PRINT_LONG_1((dword) blocks_free, PRFLG_NL);
            	RTFS_PRINT_STRING_1((byte *)"Inner Loop  : ", PRFLG_NL);
            	RTFS_PRINT_LONG_1((dword) inner_loop, 0);
            	RTFS_PRINT_STRING_1((byte *)") Outer loop: ", 0); /* "regress_error was called with error" */
            	RTFS_PRINT_LONG_1((dword) outer_loop, PRFLG_NL);
/*                regress_error(RGE_LEAKERROR); */
                return(FALSE);
            }
        }
		if (check_if_exfat())
		{
			RTFS_PRINT_STRING_1((byte *)"!!!!!!! NOT Performing check disk test", PRFLG_NL);
		}
		else
		{
			if (!do_chkdsk_test())
        		return(FALSE);
		}

    }
	RTFS_PRINT_STRING_1((byte *)"!!!!!!! Regression test complete", PRFLG_NL);
	return(TRUE);
}

/*
<TEST>    Procedure: Basic File Regression Test
<TEST>    Check Disk Freespace
<TEST>    Make the test directory
<TEST>    loop
<TEST>        Step into the test directory
<TEST>        Make another subdiretory
<TEST>            loop
<TEST>                Make N deep subdirectories
<TEST>                    Change into each
<TEST>                    compare what we know is the directory with
<TEST>                    what pc_get_cwd returns.
<TEST>            End loop
<TEST>            In the lowest level directory perform file tests (explained below)
<TEST>                    test long file names
<TEST>                    test large files (4 Gig)
<TEST>                loop
<TEST>                    create a file
<TEST>                    open it with multiple file descriptors
<TEST>                    loop
<TEST>                        write to it on multiple FDs
<TEST>                    loop
<TEST>                        seek and read  on multiple FDs. Testing values
<TEST>                    flush
<TEST>                    truncate
<TEST>                    close
<TEST>                end loop
<TEST>                loop
<TEST>                    rename file
<TEST>                    delete file
<TEST>                end loop
<TEST>    either
<TEST>      loop N times
<TEST>             change to parent directory
<TEST>              delete subdirectory
<TEST>      end loop
<TEST>    or
<TEST>      deltree the test directory
<TEST>    Check Disk Freespace again and compare with original
*/
static void build_subdir_name(byte *p, int index, int use_charset)
{
byte c;
/* Create SubDirA, or SubDirB.. SubDir('A'+index) */
    rtfs_cs_strcpy(p,test_subdir_name, use_charset);
    p = CS_OP_GOTO_EOS(p,use_charset);
    /* Put A, or B, C, D.. at the end of the string */
    c = 'A';
    c = (byte) (c + index);
    CS_OP_ASSIGN_ASCII(p,c,use_charset);
    CS_OP_INC_PTR(p,use_charset);
    CS_OP_TERM_STRING(p,use_charset);
}

static BOOLEAN do_test(int loop_count, BOOLEAN do_clean, int use_charset)
{
    int i;
    int j;
    byte *buffer;
    byte *buffer3;
    byte *buffer4;
    byte *home;
    byte *p;
    BLKBUFF *scratch_buffer;
    BLKBUFF *scratch_buffer3;
    BLKBUFF *scratch_buffer4;
    BLKBUFF *scratch_home;

    scratch_buffer = pc_scratch_blk();
    scratch_buffer3 = pc_scratch_blk();
    scratch_buffer4 = pc_scratch_blk();
    scratch_home = pc_scratch_blk();


    if (!(scratch_buffer && scratch_buffer3 && scratch_buffer4 && scratch_home))
    {
        if (scratch_buffer)
            pc_free_scratch_blk(scratch_buffer);
        if (scratch_buffer3)
            pc_free_scratch_blk(scratch_buffer3);
        if (scratch_buffer4)
            pc_free_scratch_blk(scratch_buffer4);
        if (scratch_home)
            pc_free_scratch_blk(scratch_home);
        return(FALSE);
    }
    buffer = (byte *)scratch_buffer->data;
    buffer3 = (byte *)scratch_buffer3->data;
    buffer4 = (byte *)scratch_buffer4->data;
    home = (byte *)scratch_home->data;

#if (VERBOSE)
    RTFS_PRINT_STRING_2((byte *)"Creating Subdirectory:", test_dir,PRFLG_NL); /* "Creating Subdirectory:" */
#endif
    /* Delete the test dir if it exists */
    if (pc_isdir(test_dir))
    {
        if (!pc_deltree(test_dir))
        { regress_error(RGE_DELTREE); goto return_false;}
    }
    else
       { if (!check_errno(PENOENT)) goto return_false;}

    /* Create the test dir if it exists */
    if (!pc_mkdir(test_dir))
        { regress_error(RGE_MKDIR); goto return_false;}
    if (!check_errno(0)) goto return_false;

    if (!pc_set_cwd(test_dir))
        {regress_error(RGE_SCWD); goto return_false;}

    if (!check_errno(0)) goto return_false;
    /* Save the location of the test directory for later */
    if (!pc_get_cwd(test_drive, home))
        goto return_false;

    if (!check_errno(0)) goto return_false;


    for (i = 0; i < NSUBDIRS; i++)
    {
        if (!pc_set_cwd(home))
            { regress_error(RGE_SCWD); goto return_false;}
        if (!check_errno(0)) goto return_false;
        /* Make the top level subdirs */
        build_subdir_name(buffer, i, use_charset);
#if (VERBOSE)
        RTFS_PRINT_STRING_2((byte *)"Creating Subdirectory  ", buffer,PRFLG_NL); /* "Creating Subdirectory  " */
#endif

        if (!pc_mkdir(buffer))
            { regress_error(RGE_MKDIR); goto return_false;}
        if (!check_errno(0)) goto return_false;
        for (j = 0; j < SUBDIRDEPTH; j++)
        {
            if (!pc_set_cwd(home))
                { regress_error(RGE_SCWD); goto return_false;}
            if (!check_errno(0)) goto return_false;

            /* Now make SubdirA\\Subdir in native char set */
            p = buffer;
            p = CS_OP_GOTO_EOS(p, use_charset);
            CS_OP_ASSIGN_ASCII(p,'\\', use_charset);
            CS_OP_INC_PTR(p, use_charset);
            CS_OP_TERM_STRING(p, use_charset);
            rtfs_cs_strcat(buffer,test_subdir_name,use_charset);
#if (VERBOSE)
            RTFS_PRINT_STRING_2((byte *)"Creating Subdirectory   ", buffer,PRFLG_NL); /* "Creating Subdirectory   " */
#endif

            /* Save D:\RTFS_Test_Directory\SubdirA\Subdir in native char set */
            /* For later comparison with get_pwd results */
            rtfs_cs_strcpy(buffer4, home, use_charset);
            p = buffer4;
            p = CS_OP_GOTO_EOS(p, use_charset);
            CS_OP_ASSIGN_ASCII(p,'\\', use_charset);
            CS_OP_INC_PTR(p, use_charset);
            CS_OP_TERM_STRING(p, use_charset);
            rtfs_cs_strcat(buffer4, buffer,use_charset);
            /* Create SubdirA\\Subdir under the test directory  */
            if (!pc_mkdir(buffer))
                { regress_error(RGE_MKDIR); goto return_false;}
            if (!check_errno(0)) goto return_false;
            /* Create a dir. We know this will fail. Force error recovery */
            if (pc_mkdir(buffer))
                { regress_error(RGE_MKDIRERR); goto return_false;}
            if (!check_errno(PEEXIST)) goto return_false;
            /* Go into the new directory */
            if (!pc_set_cwd(buffer))
                { regress_error(RGE_SCWD); goto return_false;}
            if (!check_errno(0)) goto return_false;
            /* Get the dir string */
            /* Should be:D:\RTFS_Test_Directory\SubdirA\\Subdir */
            if (!pc_get_cwd(test_drive, buffer3))
                { regress_error(RGE_PWD); goto return_false;}
            if (!check_errno(0)) goto return_false;
            /* Compare with saved vesrion */
            if (rtfs_cs_strcmp(buffer4, buffer3,use_charset) != 0)
                { regress_error(RGE_PWD); goto return_false;}
        }
    }
#if (INCLUDE_VFAT)
    /* Do the long file test */
    RTFS_PRINT_STRING_1((byte *)"Performing long file name test", PRFLG_NL); /* "Performing long file name test" */
    if (!do_long_file_test(do_clean, use_charset))
    {
        regress_error(RGE_LONGFILETEST);
        goto return_false;
    }
//    RTFS_PRINT_STRING_1((byte *)"!!!!!!! Skipping everal tests", PRFLG_NL);
// goto skip_to_file_test;
    RTFS_PRINT_STRING_1((byte *)"Performing more long file name tests", PRFLG_NL); /* "Performing long file name test" */
    if (!do_more_long_file_tests(do_clean, use_charset))
    {
        regress_error(RGE_LONGFILETEST);
        goto return_false;
    }
#endif
    /* Do the test of append mode */
    if (!do_append_test())
        goto return_false;
    /* Do the test of po_lseek (signed seek) */
    if (!do_po_lseek_test())
        goto return_false;
    /* Do the file test */
    if (!do_file_test(loop_count, do_clean))
    {
        regress_error(RGE_FILETEST);
        goto return_false;
    }
    /* DELETE the  subdirs */
    if (!pc_set_cwd(home))
        { regress_error(RGE_SCWD); goto return_false;}
    if (!check_errno(0)) goto return_false;
    if (do_clean && (loop_count & 1))
    {
        /* Manually remove subdirs on odd loops */
        for (i = 0; i < NSUBDIRS; i++)
        {
            /* Delete sub directories SubdirectoryA\SUBDIR\SUBDIR ... */
            for (j = SUBDIRDEPTH; j > 0; j--)
            {
                build_subdir_name(buffer, i, use_charset);
                if (!do_rm(buffer, j, use_charset))
                    goto return_false;
            }
            /* Delete sub directories SUB_? */
            build_subdir_name(buffer, i, use_charset);
            if (!pc_rmdir(buffer))
                { regress_error(RGE_RMDIR); goto return_false;}
            if (!check_errno(0)) goto return_false;
        }
    }

    /* Delete the test dir */
    if (!pc_set_cwd(home))
        { regress_error(RGE_SCWD); goto return_false;}
    /* Now make .. in native char set */
    p = buffer;
    CS_OP_ASSIGN_ASCII(p,'.', use_charset);
    CS_OP_INC_PTR(p, use_charset);
    CS_OP_ASSIGN_ASCII(p,'.', use_charset);
    CS_OP_INC_PTR(p, use_charset);
    CS_OP_TERM_STRING(p, use_charset);
    if (!pc_set_cwd(buffer))
        { regress_error(RGE_SCWD); goto return_false;}
    if (!check_errno(0)) goto return_false;
    if (do_clean && (loop_count & 1))
    {
        if (!pc_rmdir(test_dir))
            { regress_error(RGE_RMDIR); goto return_false;}
        if (!check_errno(0)) goto return_false;
    }
    else if (do_clean)
    {
        if (!pc_deltree(test_dir))
            { regress_error(RGE_DELTREE); goto return_false;}
        if (!check_errno(0)) goto return_false;
    }
    pc_free_scratch_blk(scratch_buffer);
    pc_free_scratch_blk(scratch_buffer3);
    pc_free_scratch_blk(scratch_buffer4);
    pc_free_scratch_blk(scratch_home);
    return(TRUE);
return_false:
    pc_free_scratch_blk(scratch_buffer);
    pc_free_scratch_blk(scratch_buffer3);
    pc_free_scratch_blk(scratch_buffer4);
    pc_free_scratch_blk(scratch_home);
    return(FALSE);
}

/* Delete a subdir at level */
static BOOLEAN do_rm(byte *buffer, int level, int use_charset)                              /*__fn__*/
{
int i;
byte *p;
    for (i = 0; i < level; i++)
    {
        p = buffer;
        p = CS_OP_GOTO_EOS(p, use_charset);
        CS_OP_ASSIGN_ASCII(p,'\\', use_charset);
        CS_OP_INC_PTR(p, use_charset);
        CS_OP_TERM_STRING(p, use_charset);
        rtfs_cs_strcat(buffer,test_subdir_name,use_charset);
    }
#if (VERBOSE)
    RTFS_PRINT_STRING_2((byte *)"Removing Directory  ", buffer,PRFLG_NL); /* "Removing Directory  " */
#endif
    if (!pc_rmdir(buffer))
    {
        { regress_error(RGE_RMDIR); return(FALSE);}
    }
    if (!check_errno(0)) return(FALSE);
    return(TRUE);
}

#if (INCLUDE_VFAT)
byte *plong_name;
static void create_long_name(int len, byte c, int use_charset);
static int write_long_name(int len, byte c, int use_charset);
static int reopen_long_name(int len, byte c, int use_charset);
static int remove_long_name(int len, byte c, int use_charset);
/*
<TEST>     Procedure: Long file name test
<TEST>     Verify proper operation of file create, reopen and delete on files with
<TEST>     name lengths that vary between 32 and 255 characters.
<TEST>     Verify the attempting to create filename with a length greater that 255 characters fails
*/

static BOOLEAN do_long_file_test(BOOLEAN do_clean,int use_charset)
{
   /* Use this buffer since under unicode we need more than FIVE12 bytes to test */
    plong_name = (byte *) &test_rtfs_buf[0];


    if (write_long_name(32, (byte) 'X', use_charset)) goto big_error;
    if (write_long_name(64, (byte) 'X', use_charset)) goto big_error;
    if (write_long_name(63, (byte) 'X', use_charset)) goto big_error;

	if (write_long_name(127,(byte) 'X', use_charset)) goto big_error;
    if (write_long_name(34, (byte) 'X', use_charset)) goto big_error;
    if (write_long_name(66, (byte) 'X', use_charset)) goto big_error;
    if (write_long_name(68, (byte) 'X', use_charset)) goto big_error;
    if (write_long_name(124,(byte) 'X', use_charset)) goto big_error;
    if (write_long_name(221,(byte) 'X', use_charset)) goto big_error;
    if (write_long_name(255,(byte) 'X', use_charset)) goto big_error;
    /* This one should fail */
    if (!write_long_name(256,(byte) 'X', use_charset)) goto big_error;
    if (write_long_name(66, (byte) 'Y', use_charset)) goto big_error;
    if (write_long_name(68, (byte) 'Y', use_charset)) goto big_error;
    if (write_long_name(124,(byte) 'Y', use_charset)) goto big_error;
    if (write_long_name(221,(byte) 'Y', use_charset)) goto big_error;
    if (write_long_name(255,(byte) 'Y', use_charset)) goto big_error;
    if (write_long_name(32, (byte) 'Y', use_charset)) goto big_error;
    if (write_long_name(64, (byte) 'Y', use_charset)) goto big_error;
    if (write_long_name(63, (byte) 'Y', use_charset)) goto big_error;
    if (write_long_name(127,(byte) 'Y', use_charset)) goto big_error;
    if (write_long_name(34, (byte) 'Y', use_charset)) goto big_error;
    if (reopen_long_name(32, (byte) 'X', use_charset)) goto big_error;
    if (reopen_long_name(64, (byte) 'X', use_charset)) goto big_error;
    if (reopen_long_name(63, (byte) 'X', use_charset)) goto big_error;
    if (reopen_long_name(127,(byte) 'X', use_charset)) goto big_error;
    if (reopen_long_name(34, (byte) 'X', use_charset)) goto big_error;
    if (reopen_long_name(66, (byte) 'X', use_charset)) goto big_error;
    if (reopen_long_name(68, (byte) 'X', use_charset)) goto big_error;
    if (reopen_long_name(124,(byte) 'X', use_charset)) goto big_error;
    if (reopen_long_name(221,(byte) 'X', use_charset)) goto big_error;
    if (reopen_long_name(255,(byte) 'X', use_charset)) goto big_error;
    if (reopen_long_name(66, (byte) 'Y', use_charset)) goto big_error;
    if (reopen_long_name(68, (byte) 'Y', use_charset)) goto big_error;
    if (reopen_long_name(124,(byte) 'Y', use_charset)) goto big_error;
    if (reopen_long_name(221,(byte) 'Y', use_charset)) goto big_error;
    if (reopen_long_name(255,(byte) 'Y', use_charset)) goto big_error;
    if (reopen_long_name(32, (byte) 'Y', use_charset)) goto big_error;
    if (reopen_long_name(64, (byte) 'Y', use_charset)) goto big_error;
    if (reopen_long_name(63, (byte) 'Y', use_charset)) goto big_error;
    if (reopen_long_name(127,(byte) 'Y', use_charset)) goto big_error;
    if (reopen_long_name(34, (byte) 'Y', use_charset)) goto big_error;
/* Return here if you want to see the results */
/*    pc_free_scratch_blk(scratch); */
/* return(1);  */
    if (do_clean)
    {
    if (remove_long_name(32, (byte) 'X', use_charset)) goto big_error;
    if (remove_long_name(64, (byte) 'X', use_charset)) goto big_error;
    if (remove_long_name(63, (byte) 'X', use_charset)) goto big_error;
    if (remove_long_name(127,(byte) 'X', use_charset)) goto big_error;
    if (remove_long_name(34, (byte) 'X', use_charset)) goto big_error;
    if (remove_long_name(66, (byte) 'X', use_charset)) goto big_error;
    if (remove_long_name(68, (byte) 'X', use_charset)) goto big_error;
    if (remove_long_name(124,(byte) 'X', use_charset)) goto big_error;
    if (remove_long_name(221,(byte) 'X', use_charset)) goto big_error;
    if (remove_long_name(255,(byte) 'X', use_charset)) goto big_error;
    if (remove_long_name(66, (byte) 'Y', use_charset)) goto big_error;
    if (remove_long_name(68, (byte) 'Y', use_charset)) goto big_error;
    if (remove_long_name(124,(byte) 'Y', use_charset)) goto big_error;
    if (remove_long_name(221,(byte) 'Y', use_charset)) goto big_error;
    if (remove_long_name(255,(byte) 'Y', use_charset)) goto big_error;
    if (remove_long_name(32, (byte) 'Y', use_charset)) goto big_error;
    if (remove_long_name(64, (byte) 'Y', use_charset)) goto big_error;
    if (remove_long_name(63, (byte) 'Y', use_charset)) goto big_error;
    if (remove_long_name(127,(byte) 'Y', use_charset)) goto big_error;
    if (remove_long_name(34, (byte) 'Y', use_charset)) goto big_error;
    }
    return(TRUE);
big_error:
    return(FALSE);
}

static void create_long_name(int len, byte c, int use_charset)
{
    int i;
    byte *p;

    p = plong_name;
    for (i = 0; i < len; i++)
    {
        CS_OP_ASSIGN_ASCII(p,c, use_charset);
        CS_OP_INC_PTR(p, use_charset);
    };
    CS_OP_TERM_STRING(p, use_charset);
}

static int write_long_name(int len, byte c, int use_charset)
{
    int fd, ret_val;
    create_long_name(len, c, use_charset);

    if ((fd = po_open(plong_name,(word)(PO_BINARY|PO_WRONLY|PO_CREAT|PO_TRUNC),(word)(PS_IWRITE | PS_IREAD))) >= 0)
    {
       po_close(fd);
       ret_val = 0;
    }
    else
    {
       ret_val = -1;
    }
    return(ret_val);



}
static int reopen_long_name(int len, byte c, int use_charset)
{
    int fd;
    create_long_name(len, c, use_charset);

    if ((fd = po_open(plong_name,(word)(PO_BINARY|PO_WRONLY),(word)(PS_IWRITE | PS_IREAD))) >= 0)
    {
       po_close(fd);
       return(0);
    }
    else
    {
       return(-1);
    }
}
static int remove_long_name(int len, byte c, int use_charset)
{
    create_long_name(len, c, use_charset);
    if (!pc_unlink(plong_name))
    {
       return(-1);
    }
    else
       return(0);
}


/*
<TEST>     Procedure: More long file name tests
<TEST>     The test repeats once creating the file once using unicode and once using long file name
<TEST>     The test repeats to test pc_unlink() once using alias, once using unicode and once using long file name.
<TEST>     Verify proper alias name creation.
<TEST>     Verify proper handling of file names with illegal characters.
<TEST>     The test contains a table of long file names and the alias name that should result when the file is created.
<TEST>     The alias name in the table is set zero when the long name contains illegal characters.
<TEST>     The file is created
<TEST>     If it succeeds and the alias is NULL then illegal character handling is incorrect.
<TEST>     Test all illegal characters and reserved names.
<TEST>     If it fails and the alias is not null then some other error has occured.
<TEST>     The alias name and the long name are written to the file.
<TEST>     The the file is re-opened by it's alias name and unicode and long file name and alias names are read in and compared to the table.
<TEST>     The the file is found with pc_gfirst() using unicode and long file name and alias names are read in and compared to the table.
*/

struct lfnaliaspairs {
        char *lfn;
        char *alias;
        byte *unicode_lfn;
        };

struct lfnaliaspairs namepairs[] = {
#if (0)
                                                   /* Test bad alias characters that are okay in the long file name " ,;=+[]"; */
    {",", "_~1", (byte *)L","},
    {";", "_~2", (byte *)L";"},
    {"=", "_~3", (byte *)L"="},
    {"+", "_~4", (byte *)L"+"},
    {"[", "_~5", (byte *)L"["},
    {"]", "_~6", (byte *)L"]"}, // remove
#endif

#if (INCLUDE_CS_JIS)
    /* Test JIS support */
    {"\345TEST.XXX", "\345TEST.XXX", 0},              /* File starting with e5 */
    {"T\345EST.XXX", "T\345EST.XXX", 0},              /* File with e5 in the middle */
    {"\345TESTlongfile.XXX", "\345TESTL~1.XXX", 0},   /* Long file starting with e5 */
    {"TE\345STlongfile.XXX", "TE\345STL~1.XXX", 0},    /* Long file with e5 in the middle */
    {"TEST567\201\100.XXX", "TEST56~1.XXX", 0},        /* File with JIS 2 byte character (\201\100) as 8th character in file name */
    {"TEST56\201\100.XXX", "TEST56\201\100.XXX", 0},   /* File with JIS 2 byte character (\201\100) as 7th character in file name */
    {"\201\100TEST.XXX", "\201\100TEST.XXX", 0},       /* File with JIS 2 byte character (\201\100) as 1st character in file name */
    {"TEST567.YY\201\100", "TEST56~1.YY", 0},          /* File with JIS 2 byte character (\201\100) as 3d character in file ext */
    {"TEST.Y\201\100", "TEST.Y\201\100", 0},           /* File with JIS 2 byte character (\201\100) as 2nd character in file ext */
    {"TEST.\201\100Y", "TEST.\201\100Y", 0},           /* File with JIS 2 byte character (\201\100) as 1st character in file ext */

    {"TEST567\242.XXX", "TEST567\242.XXX", 0},         /* File with JIS 1 byte character (\242) as 8th character in file name */
    {"TEST56\242.XXX", "TEST56\242.XXX", 0},           /* File with JIS 1 byte character (\242) as 7th character in file name */
    {"\242TEST.XXX", "\242TEST.XXX", 0},               /* File with JIS 1 byte character (\242) as 1st character in file name */
    {"TEST567.YY\242", "TEST567.YY\242", 0},           /* File with JIS 1 byte character (\242) as 3d character in file ext */
    {"TEST.Y\242", "TEST.Y\242", 0},                   /* File with JIS 1 byte character (\242) as 2nd character in file ext */
    {"TEST.\242Y", "TEST.\242Y", 0},                   /* File with JIS 1 byte character (\242) as 1st character in file ext */

    {"\201\076EST.XXX", 0, 0},                         /* File illegal JIS character in start file name */
    {"TES\201\076T.ABC", 0, 0},                        /* File illegal JIS character in mid file name */
    {"TEST567\201\076.ABC", 0, 0},                     /* File illegal JIS character in end file name */
    {"TTEST.\201\076XX", 0, 0},                        /* File illegal JIS character in start file ext */
    {"TTEST.X\201\076X", 0, 0},                        /* File illegal JIS character in mid file ext */
    {"TTEST.XX\201\076", 0, 0},                        /* File illegal JIS character in end file ext */
    {"Thisisalongfile\201\076name.xx", 0, 0},         /* File illegal JIS character in lfn */
#endif

    /* Test all bad characters */
//    {"\\TEST.XXX", 0, 0},                              /* Bad characters in file name or extension */
    {"T\\ST.XXX" , 0, (byte *)L"T\\ST.XXX" },
    {"TEST\\.XXX", 0, (byte *)L"TEST\\.XXX"},
    {"TESTLONGNAME\\XXX.XX", 0, (byte *)L"TESTLONGNAME\\XXX.XX"},
    {"TEST.\\XX" , 0, (byte *)L"TEST.\\XX"},
    {"TEST.X\\"  , 0, (byte *)L"TEST.X\\" },
    {"TEST.XX\\" , 0, (byte *)L"TEST.XX\\"},
    {"/TEST.XXX" , 0, (byte *)L"/TEST.XXX"},
    {"T/ST.XXX"  , 0, (byte *)L"T/ST.XXX" },
    {"TEST/.XXX" , 0, (byte *)L"TEST/.XXX"},
    {"TESTLONGNAME/XXX.XX", 0, (byte *)L"TESTLONGNAME/XXX.XX"},
    {"TEST./XX"  , 0, (byte *)L"TEST./XX" },
    {"TEST.X/"   , 0, (byte *)L"TEST.X/"  },
    {"TEST.XX/"  , 0, (byte *)L"TEST.XX/" },
    {":TEST.XXX" , 0, (byte *)L":TEST.XXX"},
    {"T:ST.XXX"  , 0, (byte *)L"T:ST.XXX" },
    {"TEST:.XXX" , 0, (byte *)L"TEST:.XXX"},
    {"TESTLONGNAME:XXX.XX", 0, (byte *)L"TESTLONGNAME:XXX.XX"},
    {"TEST.:XX"  , 0, (byte *)L"TEST.:XX" },
    {"TEST.X:"   , 0, (byte *)L"TEST.X:"  },
    {"TEST.XX:"  , 0, (byte *)L"TEST.XX:" },
    {"*TEST.XXX" , 0, (byte *)L"*TEST.XXX"},
    {"T*ST.XXX"  , 0, (byte *)L"T*ST.XXX" },
    {"TEST*.XXX" , 0, (byte *)L"TEST*.XXX"},
    {"TESTLONGNAME*XXX.XX", 0, (byte *)L"TESTLONGNAME*XXX.XX"},
    {"TEST.*XX"  , 0, (byte *)L"TEST.*XX"  },
    {"TEST.X*"   , 0, (byte *)L"TEST.X*"   },
    {"TEST.XX*"  , 0, (byte *)L"TEST.XX*"  },
    {"?TEST.XXX" , 0, (byte *)L"?TEST.XXX" },
    {"T?ST.XXX"  , 0, (byte *)L"T?ST.XXX"  },
    {"TEST?.XXX" , 0, (byte *)L"TEST?.XXX" },
    {"TESTLONGNAME?XXX.XX", 0, (byte *)L"TESTLONGNAME?XXX.XX"},
    {"TEST.?XX"  , 0, (byte *)L"TEST.?XX"  },
    {"TEST.X?"   , 0, (byte *)L"TEST.X?"   },
    {"TEST.XX?"  , 0, (byte *)L"TEST.XX?"  },
    {"\"TEST.XXX", 0, (byte *)L"\"TEST.XXX"},
    {"T\"ST.XXX" , 0, (byte *)L"T\"ST.XXX" },
    {"TEST\".XXX", 0, (byte *)L"TEST\".XXX"},
    {"TESTLONGNAME\"XXX.XX", 0, (byte *)L"TESTLONGNAME\"XXX.XX"},
    {"TEST.\"XX" , 0, (byte *)L"TEST.\"XX" },
    {"TEST.X\""  , 0, (byte *)L"TEST.X\""  },
    {"TEST.XX\"" , 0, (byte *)L"TEST.XX\"" },
    {"<TEST.XXX" , 0, (byte *)L"<TEST.XXX" },
    {"T<ST.XXX"  , 0, (byte *)L"T<ST.XXX"  },
    {"TEST<.XXX" , 0, (byte *)L"TEST<.XXX" },
    {"TESTLONGNAME<XXX.XX", 0, (byte *)L"TESTLONGNAME<XXX.XX"},
    {"TEST.<XX"  , 0, (byte *)L"TEST.<XX" },
    {"TEST.X<"   , 0, (byte *)L"TEST.X<"  },
    {"TEST.XX<"  , 0, (byte *)L"TEST.XX<" },
    {">TEST.XXX" , 0, (byte *)L">TEST.XXX"},
    {"T>ST.XXX"  , 0, (byte *)L"T>ST.XXX" },
    {"TEST>.XXX" , 0, (byte *)L"TEST>.XXX"},
    {"TESTLONGNAME>XXX.XX", 0, (byte *)L"TESTLONGNAME>XXX.XX"},
    {"TEST.>XX"  , 0, (byte *)L"TEST.>XX" },
    {"TEST.X>"   , 0, (byte *)L"TEST.X>"  },
    {"TEST.XX>"  , 0, (byte *)L"TEST.XX>" },
    {"|TEST.XXX" , 0, (byte *)L"|TEST.XXX"},
    {"T|ST.XXX"  , 0, (byte *)L"T|ST.XXX" },
    {"TEST|.XXX" , 0, (byte *)L"TEST|.XXX"},
    {"TESTLONGNAME|XXX.XX", 0, (byte *)L"TESTLONGNAME|XXX.XX"},
    {"TEST.|XX"  , 0, (byte *)L"TEST.|XX"},
    {"TEST.X|"   , 0, (byte *)L"TEST.X|" },
    {"TEST.XX|"  , 0, (byte *)L"TEST.XX|"},

                                                    /* Test bad alias characters that are okay in the long file name " ,;=+[]"; */
    {",", "_~1", (byte *)L","},
    {";", "_~2", (byte *)L";"},
    {"=", "_~3", (byte *)L"="},
    {"+", "_~4", (byte *)L"+"},
    {"[", "_~5", (byte *)L"["},
    {"]", "_~6", (byte *)L"]"},

    {",.,", "_~1._", (byte *)L",.,"},
    {";.;", "_~2._", (byte *)L";.;"},
    {"=.=", "_~3._", (byte *)L"=.="},
    {"+.+", "_~4._", (byte *)L"+.+"},
    {"[.[", "_~5._", (byte *)L"[.["},
    {"].]", "_~6._", (byte *)L"].]"},


/*  {" TEST.XXX", "_TEST~1.XXX", 0}, == should be */
    /* April 2012 changed test to verify that leading spaces are accepted
    {" TEST.XXX", "TEST.XXX"   , (byte *)L" TEST.XXX"},
    */
    {" TEST.XXX", "TEST~1.XXX"   , (byte *)L" TEST.XXX"},
    /* April 2012 added test to verify that trailing spaces are not accepted*/
    {"TSET.XXX   ", "TSET.XXX"   , (byte *)L"TSET.XXX"},
    /* July 2012 changed fro ~1 to ~2 because  ~1 already used */
    {"T EST.XXX", "TEST~2.XXX" , (byte *)L"T EST.XXX"},
    {"TES .XXX" , "TES~1.XXX"  , (byte *)L"TES .XXX" },
    {"TEST. XX" , "TEST~1.XX"  , (byte *)L"TEST. XX" },
    {"TEST.X X" , "TEST~2.XX"  , (byte *)L"TEST.X X" },
    {"TEST.XX " , "TEST.XX"    , (byte *)L"TEST.XX " },

    {",TEST.ZZZ", "_TEST~1.ZZZ", (byte *)L",TEST.ZZZ"},
    {"T,EST.ZZZ", "T_EST~1.ZZZ", (byte *)L"T,EST.ZZZ"},
    {"TES,.ZZZ" , "TES_~1.ZZZ" , (byte *)L"TES,.ZZZ" },
    {"TEST.,ZZ" , "TEST~1._ZZ" , (byte *)L"TEST.,ZZ" },
    {"TEST.Z,Z" , "TEST~1.Z_Z" , (byte *)L"TEST.Z,Z" },
    {"TEST.ZZ," , "TEST~1.ZZ_" , (byte *)L"TEST.ZZ," },

    {";TEST.AAA", "_TEST~1.AAA", (byte *)L";TEST.AAA"},
    {"T;EST.AAA", "T_EST~1.AAA", (byte *)L"T;EST.AAA"},
    {"TES;.AAA" , "TES_~1.AAA" , (byte *)L"TES;.AAA" },
    {"TEST.;AA" , "TEST~1._AA" , (byte *)L"TEST.;AA" },
    {"TEST.A;A" , "TEST~1.A_A" , (byte *)L"TEST.A;A" },
    {"TEST.AA;" , "TEST~1.AA_" , (byte *)L"TEST.AA;" },

    {"=TEST.BBB", "_TEST~1.BBB", (byte *)L"=TEST.BBB"},
    {"T=EST.BBB", "T_EST~1.BBB", (byte *)L"T=EST.BBB"},
    {"TES=.BBB" , "TES_~1.BBB" , (byte *)L"TES=.BBB" },
    {"TEST.=BB" , "TEST~1._BB" , (byte *)L"TEST.=BB" },
    {"TEST.B=B" , "TEST~1.B_B" , (byte *)L"TEST.B=B" },
    {"TEST.BB=" , "TEST~1.BB_" , (byte *)L"TEST.BB=" },

    {"+TEST.CCC", "_TEST~1.CCC", (byte *)L"+TEST.CCC"},
    {"T+EST.CCC", "T_EST~1.CCC", (byte *)L"T+EST.CCC"},
    {"TES+.CCC" , "TES_~1.CCC" , (byte *)L"TES+.CCC" },
    {"TEST.+CC" , "TEST~1._CC" , (byte *)L"TEST.+CC" },
    {"TEST.C+C" , "TEST~1.C_C" , (byte *)L"TEST.C+C" },
    {"TEST.CC+" , "TEST~1.CC_" , (byte *)L"TEST.CC+" },

    {"[TEST.DDD", "_TEST~1.DDD", (byte *)L"[TEST.DDD"},
    {"T[EST.DDD", "T_EST~1.DDD", (byte *)L"T[EST.DDD"},
    {"TES[.DDD" , "TES_~1.DDD" , (byte *)L"TES[.DDD" },
    {"TEST.[DD" , "TEST~1._DD" , (byte *)L"TEST.[DD" },
    {"TEST.D[D" , "TEST~1.D_D" , (byte *)L"TEST.D[D" },
    {"TEST.DD[" , "TEST~1.DD_" , (byte *)L"TEST.DD[" },

    {"]TEST.EEE", "_TEST~1.EEE", (byte *)L"]TEST.EEE"},
    {"T]EST.EEE", "T_EST~1.EEE", (byte *)L"T]EST.EEE"},
    {"TES].EEE" , "TES_~1.EEE" , (byte *)L"TES].EEE" },
    {"TEST.]EE" , "TEST~1._EE" , (byte *)L"TEST.]EE" },
    {"TEST.E]E" , "TEST~1.E_E" , (byte *)L"TEST.E]E" },
    {"TEST.EE]" , "TEST~1.EE_" , (byte *)L"TEST.EE]" },
                                                                /* Test Reserved names */
    {"CON" , 0, (byte *)L"CON" },
    {"PRN" , 0, (byte *)L"PRN" },
    {"NUL" , 0, (byte *)L"NUL" },
    {"AUX" , 0, (byte *)L"AUX" },
    {"LPT1", 0, (byte *)L"LPT1"},
    {"LPT2", 0, (byte *)L"LPT2"},
    {"LPT3", 0, (byte *)L"LPT3"},
    {"LPT4", 0, (byte *)L"LPT4"},
    {"COM1", 0, (byte *)L"COM1"},
    {"COM2", 0, (byte *)L"COM2"},
    {"COM3", 0, (byte *)L"COM3"},
    {"COM4", 0, (byte *)L"COM4"},
    {"con" , 0, (byte *)L"con" },
    {"prn" , 0, (byte *)L"prn" },
    {"nul" , 0, (byte *)L"nul" },
    {"aux" , 0, (byte *)L"aux" },
    {"lpt1", 0, (byte *)L"lpt1"},
    {"lpt2", 0, (byte *)L"lpt2"},
    {"lpt3", 0, (byte *)L"lpt3"},
    {"lpt4", 0, (byte *)L"lpt4"},
    {"com1", 0, (byte *)L"com1"},
    {"com2", 0, (byte *)L"com2"},
    {"com3", 0, (byte *)L"com3"},
    {"com4", 0, (byte *)L"com4"},

                                                                /* Test Reserved names with extensions */

    {"CON.XXX" , 0, (byte *)L"CON.XXX" },
    {"PRN.XXX" , 0, (byte *)L"PRN.XXX" },
    {"NUL.XXX" , 0, (byte *)L"NUL.XXX" },
    {"AUX.XXX" , 0, (byte *)L"AUX.XXX" },
    {"LPT1.XXX", 0, (byte *)L"LPT1.XXX"},
    {"LPT2.XXX", 0, (byte *)L"LPT2.XXX"},
    {"LPT3.XXX", 0, (byte *)L"LPT3.XXX"},
    {"LPT4.XXX", 0, (byte *)L"LPT4.XXX"},
    {"COM1.XXX", 0, (byte *)L"COM1.XXX"},
    {"COM2.XXX", 0, (byte *)L"COM2.XXX"},
    {"COM3.XXX", 0, (byte *)L"COM3.XXX"},
    {"COM4.XXX", 0, (byte *)L"COM4.XXX"},
    {"con.XXX" , 0, (byte *)L"con.XXX" },
    {"prn.XXX" , 0, (byte *)L"prn.XXX" },
    {"nul.XXX" , 0, (byte *)L"nul.XXX" },
    {"aux.XXX" , 0, (byte *)L"aux.XXX" },
    {"lpt1.XXX", 0, (byte *)L"lpt1.XXX"},
    {"lpt2.XXX", 0, (byte *)L"lpt2.XXX"},
    {"lpt3.XXX", 0, (byte *)L"lpt3.XXX"},
    {"lpt4.XXX", 0, (byte *)L"lpt4.XXX"},
    {"com1.XXX", 0, (byte *)L"com1.XXX"},
    {"com2.XXX", 0, (byte *)L"com2.XXX"},
    {"com3.XXX", 0, (byte *)L"com3.XXX"},
    {"com4.XXX", 0, (byte *)L"com4.XXX"},

                                                    /* More tests */
    {"TEST_LFN1.TXT" , "TEST_L~1.TXT", (byte *)L"TEST_LFN1.TXT" },          /* File name > 8 ext == 3*/
    {"TEST_LFN2.TxT" , "TEST_L~2.TXT", (byte *)L"TEST_LFN2.TxT" },
    {"TeST_LFN3.TXT" , "TEST_L~3.TXT", (byte *)L"TeST_LFN3.TXT" },
    {"TEST_LFN1.TXTA", "TEST_L~4.TXT", (byte *)L"TEST_LFN1.TXTA"},             /* File name > 8 ext == 4*/
    {"TEST_LFN2.TxTB", "TEST_L~5.TXT", (byte *)L"TEST_LFN2.TxTB"},
    {"TeST_LFN3.TXTC", "TEST_L~6.TXT", (byte *)L"TeST_LFN3.TXTC"},
    {"TEST_lFN1.Tx"  , "TEST_L~1.TX",  (byte *)L"TEST_lFN1.Tx"  },                /* File name > 8 ext == 2 */
    {"TEST_LfN2.tX"  , "TEST_L~2.TX",  (byte *)L"TEST_LfN2.tX"  },
    {"TEST_LFn3.TX"  , "TEST_L~3.TX",  (byte *)L"TEST_LFn3.TX"  },
    {"TEST_lFN1.T"   , "TEST_L~1.T",   (byte *)L"TEST_lFN1.T"   },              /* File name > 8 ext == 1 */
    {"TEST_LfN2.t"   , "TEST_L~2.T",   (byte *)L"TEST_LfN2.t"   },
    {"TEST_LFn3.T"   , "TEST_L~3.T",   (byte *)L"TEST_LFn3.T"   },
    {"T2ST_LFN1.TXT" , "T2ST_L~1.TXT", (byte *)L"T2ST_LFN1.TXT" },          /* File name >8 ext == 3*/
    {"T2ST_LFN2.TxT" , "T2ST_L~2.TXT", (byte *)L"T2ST_LFN2.TxT" },
    {"T2ST_LFN3.TXT" , "T2ST_L~3.TXT", (byte *)L"T2ST_LFN3.TXT" },
    {"T2ST_lFN1.Tx"  , "T2ST_L~1.TX",  (byte *)L"T2ST_lFN1.Tx"  },               /* File name == 8 ext == 2 */
    {"T2ST_LfN2.tX"  , "T2ST_L~2.TX",  (byte *)L"T2ST_LfN2.tX"  },
    {"T2ST_LFn3.TX"  , "T2ST_L~3.TX",  (byte *)L"T2ST_LFn3.TX"  },
    {"T2ST_lFN1.TxTA", "T2ST_L~4.TXT", (byte *)L"T2ST_lFN1.TxTA"},                /* File name == 8 ext == 4 */
    {"T2ST_LfN2.tXTB", "T2ST_L~5.TXT", (byte *)L"T2ST_LfN2.tXTB"},
    {"T2ST_LFn3.TXTC", "T2ST_L~6.TXT", (byte *)L"T2ST_LFn3.TXTC"},
    {"T2ST_lFN.T"    , "T2ST_LFN.T",   (byte *)L"T2ST_lFN.T"    },                /* File name == 8 ext == 1 */
    {"T2ST_LfN.t",    0, 0},                           /* Should fail */
    {"TEST.T",     "TEST.T", (byte *)L"TEST.T"},                       /* File name < 8 ext == 1,2,3,4 */
    {"TeST.T",     0, 0},                              /* Should fail */
    {"SfN.T",     "SFN.T", (byte *)L"SfN.T"},                         /* Legal SFN except case */
    {"sfN.T",     0, 0},                               /* Same Legal SFN except case Should fail */
    {"TEST.TX"  , "TEST.TX"   ,(byte *)L"TEST.TX"},
    {"TEST.TXT" , "TEST.TXT"  ,(byte *)L"TEST.TXT"   },
    {"TEST.TXTA", "TEST~1.TXT",(byte *)L"TEST.TXTA"  },
    {"TEST.TXTB", "TEST~2.TXT",(byte *)L"TEST.TXTB" },
    {"TEST"     , "TEST" , (byte *)L"TEST"},                               /* File name < 8 No extension */
    {"TeST1"    , "TEST1", (byte *)L"TEST1"},                               /* No extension */
    {"TEsT1"    ,       0, 0},                                /* Same Legal SFN except Should fail */
    {"TESt56789"     ,"TEST56~1"     ,(byte *)L"TESt56789"      },                    /* File name > 8 No extension */
    {"TeST5678A"     ,"TEST56~2"     ,(byte *)L"TeST5678A"      },
    {"TESt5678.123"  ,"TEST5678.123" ,(byte *)L"TESt5678.123"   },                           /* File name == 8 extension == 3 */
    {"TESt5678.1234" ,"TEST56~1.123" ,(byte *)L"TESt5678.1234"  },                          /* File name == 8 extension >  3 */
    {"TESt5678.12345","TEST56~2.123" ,(byte *)L"TESt5678.12345" },              /* File name == 8 extension  > 3  */
    {"a", "A"   , (byte *)L"a"},                                     /* File name == 1 extension == 0 */
    {"A", 0, 0},                              /* Should fail */
    {"A.B"     , "A.B"    , (byte *)L"A.B"      },                        /* File name == 1 extension == 1 */
    {"A.ABC"   , "A.ABC"  , (byte *)L"A.ABC"   },                    /* File name == 1 extension == 3 */
    {"a.ABCd"  , "A~1.ABC", (byte *)L"a.ABCd"  },                 /* File name == 1 extension == 4 */
    {"a.ABCde" , "A~2.ABC", (byte *)L"a.ABCde" },                /* File name == 1 extension == 5 */
    {".yyy"    , "YYY~1"  , (byte *)L".yyy"    },                     /* File name with leading '.' no extension */
    {".y.yy"   , "Y~1.YY" , (byte *)L".y.yy"   },                  /* File name with leading '.' and extension */
    {".y.yy.xx", "YYY~1.XX",(byte *)L".y.yy.xx"},             /* File name with leading '.' and extension */
    {0,0, 0}
};

static BOOLEAN do_one_long_file_test(int i, int use_unicode);

static BOOLEAN do_more_long_file_tests(BOOLEAN do_clean,int use_charset)
{
int i;
byte *p;
BOOLEAN isExFat;

    isExFat = check_if_exfat();

    /* Create an empty subdirectory named RTFS_Test_Directory\\RTFS_Test_Directory */
    if (pc_isdir(test_dir))
    {
        if (!pc_deltree(test_dir))
        { regress_error(RGE_DELTREE); goto return_false;}
    }
    else
       { if (!check_errno(PENOENT)) goto return_false;}
    if (!pc_mkdir(test_dir))
        { regress_error(RGE_MKDIR); goto return_false;}
    if (!check_errno(0)) goto return_false;
    if (!pc_set_cwd(test_dir))
        {regress_error(RGE_SCWD); goto return_false;}
    if (!check_errno(0)) goto return_false;

    /* Create using lfn */
    RTFS_PRINT_STRING_1((byte *)"Performing long file name tests", PRFLG_NL); /* "Performing buffered file io test" */
    for (i = 0; namepairs[i].lfn; i++)
    {
        if (!namepairs[i].lfn)
            break;
        if (!do_one_long_file_test(i, FALSE))
            goto return_false;

		if (namepairs[i].alias)
        {
			if (isExFat)
			{
			    /* delete using lfn since there is no alias */
				if (!pc_unlink((byte *)namepairs[i].lfn))
				{ regress_error(RGE_UNLINK); goto return_false; }
			}
			else
			{
				/* Test delete using alias */
				if (!pc_unlink((byte *)namepairs[i].alias))
				{ regress_error(RGE_UNLINK); goto return_false; }
			}

            /* Now do it again */
            if (!do_one_long_file_test(i, FALSE))
                goto return_false;

            /* Test delete using lfn */
            if (!pc_unlink((byte *)namepairs[i].lfn))
            { regress_error(RGE_UNLINK); goto return_false; }
            /* Now do it again */
            if (!do_one_long_file_test(i, FALSE))
                goto return_false;
        }
#if (INCLUDE_CS_UNICODE)
        if (namepairs[i].unicode_lfn)
        {
            if (namepairs[i].alias)
            {
                /* Test delete using unicode */
                if (!pc_unlink_uc((byte *)namepairs[i].unicode_lfn))
                { regress_error(RGE_UNLINK); goto return_false; }
            }
            /* Test File create using unicode */
            if (!do_one_long_file_test(i, TRUE))
                goto return_false;
        }
#endif
    }
    /* Now make .. in native char set */
    p = (byte *) &test_rtfs_buf[0];
    CS_OP_ASSIGN_ASCII(p,'.', use_charset);
    CS_OP_INC_PTR(p, use_charset);
    CS_OP_ASSIGN_ASCII(p,'.', use_charset);
    CS_OP_INC_PTR(p, use_charset);
    CS_OP_TERM_STRING(p, use_charset);
    if (!pc_set_cwd((byte *) &test_rtfs_buf[0]))
        { regress_error(RGE_SCWD); goto return_false;}
    if (!check_errno(0)) goto return_false;

    if (do_clean)
    {
        if (!pc_deltree(test_dir))
        { regress_error(RGE_DELTREE); goto return_false;}
        if (!check_errno(0)) goto return_false;
    }

    RTFS_PRINT_STRING_1((byte *)" ", PRFLG_NL); /* "Performing buffered file io test" */
    return(TRUE);
return_false:
    RTFS_PRINT_STRING_1((byte *)" ", PRFLG_NL); /* "Performing buffered file io test" */
    return(FALSE);
}

static BOOLEAN do_one_long_file_test(int i, int use_unicode)
{
int j;
int fd;
byte *plfn;
DRIVE_INFO drive_info_struture;
BOOLEAN isExFat;

#if (INCLUDE_CS_UNICODE)
    if (use_unicode)
    {
        RTFS_PRINT_STRING_1((byte *)"== Unicode: ", 0);
    }
#else
    RTFS_ARGSUSED_INT((int) use_unicode);
#endif

    if (namepairs[i].alias)
    	RTFS_PRINT_STRING_1((byte *)"Testing legal file name  :\"", 0);
	else
    	RTFS_PRINT_STRING_1((byte *)"Testing illegal file name:\"", 0);
    RTFS_PRINT_STRING_1((byte *)namepairs[i].lfn, 0);
   	RTFS_PRINT_STRING_1((byte *)"\"", PRFLG_NL);

    if (!pc_diskio_info(test_drive, &drive_info_struture, TRUE) || drive_info_struture.sector_size > sizeof(test_rtfs_buf))
    {
        RTFS_PRINT_STRING_1((byte *)"Buffer too small for long file test ", PRFLG_NL);
        return(FALSE);
    }
    isExFat = (BOOLEAN) drive_info_struture.is_exfat;

#if (INCLUDE_CS_UNICODE)
    if (use_unicode)
        fd = po_open_uc((byte *)namepairs[i].unicode_lfn,(word)(PO_BINARY|PO_WRONLY|PO_CREAT|PO_EXCL),(word)(PS_IWRITE | PS_IREAD));
    else
#endif
        fd = po_open((byte *)namepairs[i].lfn, (word)(PO_BINARY|PO_WRONLY|PO_CREAT|PO_EXCL),(word)(PS_IWRITE | PS_IREAD));
    if (fd < 0)
    {
        if (namepairs[i].alias) /* If there is an alias, it should have worked */
        {
            regress_error(RGE_OPEN);
            return(FALSE);
        }
    }
    else
    {
    byte *b;
         if (!namepairs[i].alias) /* If there is no alias, it should have failed */
        {
            regress_error(RGE_OPEN);
            return(FALSE);
        }

        b = (byte *) &test_rtfs_buf[0];
        rtfs_memset(b, 0, 256);
        rtfs_cs_strcpy(b, (byte *)namepairs[i].lfn, CS_CHARSET_NOT_UNICODE);
        po_write(fd, b, 256);
        b = (byte *) &test_rtfs_buf[0];
        rtfs_memset(b, 0, 256);
        rtfs_cs_strcpy(b, (byte *)namepairs[i].alias, CS_CHARSET_NOT_UNICODE);
        po_write(fd, b, 256);
        po_close(fd);


        /* reopen the file using the lfn (unicode and non-unicode) and check contents */
        for (j = 0; j < 6; j++)
        {
            byte *b2;
            b = (byte *) &test_rtfs_buf[0];

            b2 = b + 256;
            rtfs_memset(b, 0, FIVE12);
            /*
                j == 0 -> po_open lfn
                j == 1 -> pc_gfirst lfn
                j == 2 -> po_open sfn
                j == 3 -> pc_gfirst sfn
                j == 4 -> Use po_open_uc
                j == 5 -> pc_gfirst_uc
            */
            if (j < 2)      /* Use lfn and non-unicode interface */
                plfn = (byte *) namepairs[i].lfn;
            else if (j < 4) /* Use alias and non-unicode interface */
			{
				if (isExFat)	/* No aliases under exFat, skip to the next */
					continue;
                plfn = (byte *) namepairs[i].alias;
			}
            else            /* Use unicode interface, break if unicode disabled or no unicode in the table */
            {
#if (INCLUDE_CS_UNICODE)
                plfn = (byte *) namepairs[i].unicode_lfn;
#else
                plfn = 0;
#endif
            }
            if (!plfn)
                break;
            if (j == 0 || j == 2 || j == 4) /* 0, 2 and 4 use open */
            {
#if (INCLUDE_CS_UNICODE)
                if (j == 4)
                {
                    fd = po_open_uc((byte *)plfn,(word)(PO_BINARY|PO_RDONLY),(word)(PS_IWRITE | PS_IREAD));
                }
                else
#endif
                {
                    fd = po_open((byte *)plfn,(word)(PO_BINARY|PO_RDONLY),(word)(PS_IWRITE | PS_IREAD));
                }
                if (fd < 0)
                {
                    regress_error(RGE_OPEN);
                    return(FALSE);
                }
                /* read lfn */
                po_read(fd, b, 256);
                /* read alias */
                po_read(fd, b2, 256);
                po_close(fd);
            }
            else        /* 1, 3 and 5 use gfirst */
            {
                DSTAT statobj;
                BOOLEAN gfirst_res;
                int blocks_read;

#if (INCLUDE_CS_UNICODE)
                if (j == 5)
                    gfirst_res = pc_gfirst_uc(&statobj, (byte *)plfn);
                else
#endif
                    gfirst_res = pc_gfirst(&statobj, (byte *)plfn);

                 if (!gfirst_res)
                    { regress_error(RGE_GFIRST); return(FALSE);}

                 if (!pc_gread(&statobj, 1, (byte *)b, &blocks_read) || blocks_read != 1)
                 { regress_error(RGE_GREAD); return(FALSE);}
                 pc_gdone(&statobj);
            }

            if (rtfs_cs_strcmp(b, (byte *)namepairs[i].lfn, CS_CHARSET_NOT_UNICODE) != 0)
            {
                regress_error(RGE_READ);
                return(FALSE);
            }
            if (rtfs_cs_strcmp(b2, (byte *)namepairs[i].alias, CS_CHARSET_NOT_UNICODE) != 0)
            {
                regress_error(RGE_READ);
                return(FALSE);
            }
        }
    }
    return(TRUE);
}



#endif
/*
<TEST>     Verify proper operation of files opened with buffering
<TEST>
<TEST>     Open the same file twice simultaneously in buffered mode.
<TEST>     verify that the file buffer remains coherent when reads, writes and seeks are are performed on the two file descriptors
<TEST>     po_lseek
*/

static BOOLEAN do_buffered_file_test(BOOLEAN do_clean)
{
int fd1, fd2;
word i,j, *wbuff;

    wbuff = (word *) &test_rtfs_buf[0];

    /* Write 1024 bytes, close to flush, re-open and read */
    fd1 = po_open(test_file_name, PO_RDWR|PO_CREAT|PO_TRUNC|PO_BUFFERED, PS_IWRITE|PS_IREAD);
    if (fd1 < 0)
        { regress_error(RGE_OPEN); return(FALSE);}
    for (i = 0; i < FIVE12; i++)
    {
        if (po_write(fd1, (byte *)&i, 2) != 2)
            { regress_error(RGE_WRITE); return(FALSE);}
    }
    po_close(fd1);
    fd1 = po_open(test_file_name, PO_RDWR|PO_BUFFERED, 0);
    if (fd1 < 0)
        { regress_error(RGE_OPEN); return(FALSE);}
    for (i = 0; i < FIVE12; i++)
    {
        if ( (po_read(fd1, (byte *)&j, 2) != 2) || j != i)
        {
            { po_close(fd1); regress_error(RGE_READ); return(FALSE);}
        }
    }
    po_close(fd1);

    /* Read two buffered viewports of the same file.
       Note: Buffer thrashing occurs here */
    fd1 = po_open(test_file_name, PO_RDWR|PO_BUFFERED, PS_IWRITE|PS_IREAD);
    if (fd1 < 0)
        { regress_error(RGE_OPEN); return(FALSE);}
    fd2 = po_open(test_file_name, PO_RDWR|PO_BUFFERED, PS_IWRITE|PS_IREAD);
    if (fd2 < 0)
        { regress_error(RGE_OPEN); po_close(fd1); return(FALSE);}
    if (po_lseek(fd2, FIVE12, PSEEK_SET) != FIVE12)
        { regress_error(RGE_SEEK); po_close(fd1); po_close(fd2); return(FALSE);}

    for (i = 0; i < 256; i++)
    {
        int r1, r2;
        word v1, v2;
        r1 = r2 = 0;
        v1 = v2 = 0xfff;

        r1 = po_read(fd1, (byte *)&v1, 2);
        r2 = po_read(fd2, (byte *)&v2, 2);
        if (r1 != 2 || r2 != 2 || v1 != i || v2 != i+256)
        { regress_error(RGE_READ); po_close(fd1); po_close(fd2);return(FALSE);}
    }
    /* Write buffered, read unbuffered */
    if ( (po_lseek(fd1, 0, PSEEK_SET) != 0) || (po_lseek(fd2, 0, PSEEK_SET) != 0))
        { regress_error(RGE_SEEK); po_close(fd1); po_close(fd2);return(FALSE);}

	/* Do this in two steps for testing */
	for (j = 99, i = 0; i < FIVE12/2; i++)
        if (po_write(fd1, (byte *)&j, 2) != 2)
            { regress_error(RGE_WRITE); po_close(fd1); po_close(fd2);return(FALSE);}

    for (j = 99, i = 0; i < FIVE12/2; i++)
        if (po_write(fd1, (byte *)&j, 2) != 2)
            { regress_error(RGE_WRITE); po_close(fd1); po_close(fd2);return(FALSE);}
    if (po_read(fd2, (byte *)wbuff, 1024) != 1024)
            { regress_error(RGE_READ); po_close(fd1); po_close(fd2);return(FALSE);}
    for (j = 99, i = 0; i < FIVE12; i++)
	{
        if (wbuff[i] != j)
        {
				regress_error(RGE_READ);
				po_close(fd1);
				po_close(fd2);
				return(FALSE);
		}
	}
    /* Seek test */
    if ( (po_lseek(fd1, 520, PSEEK_SET) != 520) )
        { regress_error(RGE_SEEK); po_close(fd1); po_close(fd2);return(FALSE);}
    j = 520; po_write(fd1, (byte *)&j, 2);
    if ( (po_lseek(fd1, 20, PSEEK_SET) != 20) )
        { regress_error(RGE_SEEK); po_close(fd1); po_close(fd2);return(FALSE);}
    j = 20; po_write(fd1, (byte *)&j, 2);
    if ( (po_lseek(fd1, 520, PSEEK_SET) != 520) )
        { regress_error(RGE_SEEK); po_close(fd1); po_close(fd2);return(FALSE);}
    j = 0; po_read(fd1, (byte *)&j, 2);
    if (j != 520)
        { regress_error(RGE_READ); po_close(fd1); po_close(fd2);return(FALSE);}
    if ( (po_lseek(fd1, 20, PSEEK_SET) != 20) )
        { regress_error(RGE_SEEK); po_close(fd1); po_close(fd2);return(FALSE);}
    j = 0; po_read(fd1,(byte *) &j, 2);
    if (j != 20)
        { regress_error(RGE_READ); po_close(fd1); po_close(fd2);return(FALSE);}

    /* Write buffered and write unbuffered */
    if ( (po_lseek(fd1, 0, PSEEK_SET) != 0) || (po_lseek(fd2, 0, PSEEK_SET) != 0))
        { regress_error(RGE_SEEK); po_close(fd1); po_close(fd2);return(FALSE);}
    for (i = 0; i < FIVE12; i++)
        wbuff[i] = (word) (FIVE12-i);
    for (i = 0; i < FIVE12; i++)
        if (po_write(fd1, (byte *)&i, 2) != 2)
            { regress_error(RGE_WRITE); po_close(fd1); po_close(fd2);return(FALSE);}
    if (po_write(fd2, (byte *)wbuff, 1024) != 1024)
            { regress_error(RGE_WRITE); po_close(fd1); po_close(fd2);return(FALSE);}
    po_close(fd1);
    if ( po_lseek(fd2, 0, PSEEK_SET) != 0 )
        { regress_error(RGE_SEEK); po_close(fd2);return(FALSE);}
    for (i = 0; i < FIVE12; i++)
        if ( (po_read(fd2, (byte *)&j, 2) != 2) || j != FIVE12-i)
            { regress_error(RGE_READ); po_close(fd2); return(FALSE);}
    po_close(fd2);
    if (do_clean)
        pc_unlink(test_file_name);
    return(TRUE);
}

/*
<TEST>     Procedure:Verify proper operation of the pc_gread() API function.
<TEST>     Write data to a file and verify that the data can be accessed during a directory
<TEST>     enumeration using the pc_gread API call.
*/

static BOOLEAN do_gread_file_test(void)
{
int fd,i, blocks_read,blocks_to_read,sector_size_dwords;

dword value;
DSTAT statobj;

    /* Write 1024 bytes, close to flush, re-open and read */
    fd = po_open(test_file_name, PO_RDWR|PO_CREAT|PO_TRUNC|PO_BUFFERED, PS_IWRITE|PS_IREAD);
    if (fd< 0)
        { regress_error(RGE_OPEN); return(FALSE);}
    value = 0;
    for (i = 0; i < NLONGS; i++)
        test_rtfs_buf[i] = value++;
    if (po_write(fd, (byte *)&test_rtfs_buf[0], (NLONGS * 4)) != (NLONGS * 4))
    {
		regress_error(RGE_WRITE);
		return(FALSE);}
    po_close(fd);


    for (i = 0; i < NLONGS; i++)
        test_rtfs_buf[i] = 0;
    if (!pc_gfirst(&statobj, test_file_name))
    { regress_error(RGE_GFIRST); return(FALSE);}

    sector_size_dwords = pc_sector_size(test_drive)/4;
    if (sector_size_dwords > NLONGS)
        blocks_to_read = 1;
    else
        blocks_to_read = ((NLONGS+(sector_size_dwords-1))/sector_size_dwords);
    if (!pc_gread(&statobj, blocks_to_read, (byte *)&test_rtfs_buf[0], &blocks_read) || blocks_read != blocks_to_read)
    {
		regress_error(RGE_GREAD); return(FALSE);}
    pc_gdone(&statobj);

    value = 0;
    for (i = 0; i < (NLONGS/128)*128; i++)
    {
        if (test_rtfs_buf[i] != value++)
        { regress_error(RGE_GFIRST); return(FALSE);}
    }
    pc_unlink(test_file_name);
    return(TRUE);
}




#define FOUR_GIG_BLOCKS 0x80000
#define ONE_MEG (dword) 0x100000
#define ONE_MEG_SHY (0x100000-FIVE12)
#define FILL_TEST_HIT_WRAP 0
#define DO_LARGE_FILE_TEST 1

#ifndef RTFS_MAX_FILE_SIZE /* BUGFIX */
/* Must be ProPlus because it doesn't use these compile time constants. So set them to
   correct values for this test under ProPlus */
#define RTFS_MAX_FILE_SIZE      0xffffffff
#define RTFS_TRUNCATE_WRITE_TO_MAX    1
#endif
/*
<TEST>     Procedure:Test correct behavior with large (4 Gig) files
<TEST>     Verify proper operation of a 32 bit file when it is almost full (4 Gigabytes)
<TEST>     Verify that file write and file extends behave appropriately when the maximum file size is reached.
<TEST>     Verify that file read behaves appropriately when the maximum file size is reached.
<TEST>     Verify that file seek behaves appropriately over the range including The maximum file size is reached.
*/

#if (DO_LARGE_FILE_TEST)

static BOOLEAN do_large_file_test(void)
{
int residual,fd;
dword i, max_filesize_megs,ltemp,ltemp2,target_value,blocks_total,blocks_free;
ERTFS_STAT stat_buff;

    max_filesize_megs = RTFS_MAX_FILE_SIZE/ONE_MEG;
    ltemp = max_filesize_megs * ONE_MEG;
    ltemp2 =  (RTFS_MAX_FILE_SIZE-ltemp);
    residual = (int)ltemp2;
    /* Won't work on a 16 bit machine */
    if ((dword) residual != ltemp2)
        return(FALSE);

    if (residual == 0)
    {
        max_filesize_megs -= 1;
        residual = ONE_MEG;
    }

    pc_blocks_free(test_drive, &blocks_total, &blocks_free);

    if (blocks_free/2048 <=  max_filesize_megs) /* Not enough space to do long test */
        return(TRUE);
#if (VERBOSE)
    RTFS_PRINT_STRING_1((byte *)"Performing Large 4GIG File io test",PRFLG_NL); /* "Performing Large 4GIG File io test" */
#endif

    /* Test read, write, ulseek, chsize, extend file */
    fd = po_open(test_file_name, PO_RDWR|PO_CREAT|PO_EXCL, PS_IWRITE|PS_IREAD);

    if (fd < 0)
        { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    if (!check_errno(0)) return(FALSE);
    test_rtfs_buf[0] = 0;

    /* Write the file - 1 meg at a time */
    for (i = 0 ; i < max_filesize_megs; i++)
    {
         if (po_write(fd, (byte *) test_rtfs_buf, FIVE12) != FIVE12)
        { regress_error(RGE_LARGEFILETEST); return(FALSE);}
        if (po_write(fd, (byte *) 0, ONE_MEG_SHY) != ONE_MEG_SHY)
        { regress_error(RGE_LARGEFILETEST); return(FALSE);}
        test_rtfs_buf[0] += ONE_MEG;
    }
	if (check_if_exfat())
	{
    	if ((dword) residual != 0)
    		if (po_write(fd, (byte *) 0, residual) != residual)
    		{ regress_error(RGE_LARGEFILETEST); return(FALSE);}
	}
	else
	{
    /* The last meg will be truncated */
#if(FILL_TEST_HIT_WRAP)
#if (RTFS_TRUNCATE_WRITE_TO_MAX)
    if (po_write(fd, (byte *) 0, residual+1) != residual)
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    if (po_write(fd, (byte *) 0, 1) != 0)
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}
#else
    if (po_write(fd, (byte *) 0, residual+1) != -1)
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    if (po_write(fd, (byte *) 0, residual) != residual)
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}
#endif
#else
    /* The last meg will be truncated */
    if (po_write(fd, (byte *) test_rtfs_buf, FIVE12) != FIVE12)
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}
#if (RTFS_TRUNCATE_WRITE_TO_MAX)
{
    if (po_write(fd, (byte *) 0, ONE_MEG) != (residual-FIVE12))
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}
}
#else
    if (po_write(fd, (byte *) 0, ONE_MEG) != -1)
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    if (po_write(fd, (byte *) 0, (residual-FIVE12)) != (residual-FIVE12))
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}
#endif
#endif
	}
    /* Read it back - 1 gig at a time */
    if (!po_ulseek(fd, 0, &ltemp, PSEEK_SET))
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    /* Read the file - 1 gig at a time */
    target_value = 0;
    for (i = 0 ; i < max_filesize_megs; i++)
    {
        if ( (po_read(fd, (byte *) test_rtfs_buf, FIVE12) != FIVE12) ||
            (test_rtfs_buf[0] !=  target_value) )
        { regress_error(RGE_LARGEFILETEST); return(FALSE);}
        target_value += ONE_MEG;
        if (po_read(fd, (byte *) 0, ONE_MEG_SHY) != ONE_MEG_SHY)
        { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    }
	if (check_if_exfat())
	{
     	if (po_read(fd, (byte *) 0, residual) != residual)
     	{ regress_error(RGE_LARGEFILETEST); return(FALSE);}
	}
	else
	{
    /* The last meg will be truncated */
#if (FILL_TEST_HIT_WRAP)
     if (po_read(fd, (byte *) 0, ONE_MEG) != residual)
     { regress_error(RGE_LARGEFILETEST); return(FALSE);}
     if (po_read(fd, (byte *) 0, ONE_MEG) != 0)
     { regress_error(RGE_LARGEFILETEST); return(FALSE);}
#else
    if ( (po_read(fd, (byte *) test_rtfs_buf, FIVE12) != FIVE12) ||
         (test_rtfs_buf[0] !=  target_value))
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    if (po_read(fd, (byte *) 0, ONE_MEG) != residual-FIVE12)
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}
#endif
#if (FILL_TEST_HIT_WRAP)
    po_close(fd);
    goto unlink_it;
#endif
	}

	if (check_if_exfat())	  	/* Thorough seek test already performed and these test assume a maximum size */
		goto around_seek_test;
    /* Test seek set */


    /* Test po_ulseek */
    /* Test seek end */
    if (!po_ulseek(fd, 0, &ltemp, PSEEK_END) || ltemp != RTFS_MAX_FILE_SIZE)
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    /* Test seek curr neg */
    target_value = (dword) (max_filesize_megs * ONE_MEG);
    if (!po_ulseek(fd, residual, &ltemp, PSEEK_CUR_NEG) || ltemp != target_value)
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    if (!po_ulseek(fd, 0, &ltemp, PSEEK_CUR_NEG) ||
        ltemp != target_value)
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    if ( (po_read(fd, (byte *) test_rtfs_buf, FIVE12) != FIVE12) ||
        (test_rtfs_buf[0] !=  target_value))
    { 	regress_error(RGE_LARGEFILETEST); return(FALSE);}
    if (!po_ulseek(fd, FIVE12, &ltemp, PSEEK_CUR_NEG) ||
        ltemp != target_value)
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    target_value -= (10 * ONE_MEG);
    if (!po_ulseek(fd, 10 * ONE_MEG, &ltemp, PSEEK_CUR_NEG) || ltemp != target_value)
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    if ( (po_read(fd, (byte *) test_rtfs_buf, FIVE12) != FIVE12) ||
        (test_rtfs_buf[0] !=  target_value))
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    if (!po_ulseek(fd, FIVE12, &ltemp, PSEEK_CUR_NEG) ||
        ltemp != target_value)
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    /* Test seek curr positive */
    target_value += (4 * ONE_MEG);
    if (!po_ulseek(fd, 4 * ONE_MEG, &ltemp, PSEEK_CUR) || ltemp != target_value)
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    if ( (po_read(fd, (byte *) test_rtfs_buf, FIVE12) != FIVE12) ||
        (test_rtfs_buf[0] !=  target_value))
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    if (!po_ulseek(fd, FIVE12, &ltemp, PSEEK_CUR_NEG) ||
        ltemp != target_value)
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    if (!po_ulseek(fd, 0, &ltemp, PSEEK_CUR) ||
        ltemp != target_value)
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}

    /* Test seek set */
    target_value = (4 * ONE_MEG);
    if (!po_ulseek(fd, target_value, &ltemp, PSEEK_SET) || ltemp != target_value)
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    if ( (po_read(fd, (byte *) test_rtfs_buf, FIVE12) != FIVE12) ||
        (test_rtfs_buf[0] !=  target_value))
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    target_value = ((max_filesize_megs - 1000) * ONE_MEG);
    if (!po_ulseek(fd, target_value, &ltemp, PSEEK_SET) || ltemp != target_value)
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    if ( (po_read(fd, (byte *) test_rtfs_buf, FIVE12) != FIVE12) ||
        (test_rtfs_buf[0] !=  target_value))
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    /* Test fstat set */
    if (pc_fstat(fd, &stat_buff) != 0 || stat_buff.st_size != RTFS_MAX_FILE_SIZE)
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    if (po_close(fd) != 0)
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    /* Test stat set */
    if (pc_stat(test_file_name, &stat_buff) != 0 || stat_buff.st_size != RTFS_MAX_FILE_SIZE)
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    /* Test reopen */
    fd = po_open(test_file_name, PO_RDWR, PS_IWRITE|PS_IREAD);
    if (fd < 0)
        { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    if (!po_ulseek(fd, 0, &ltemp, PSEEK_END) || ltemp != RTFS_MAX_FILE_SIZE)
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    target_value = ((max_filesize_megs - 1000) * ONE_MEG);
    if (!po_ulseek(fd, target_value, &ltemp, PSEEK_SET) || ltemp != target_value)
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    if ( (po_read(fd, (byte *) test_rtfs_buf, FIVE12) != FIVE12) ||
        (test_rtfs_buf[0] !=  target_value))
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    /* Test fstat */
    if (pc_fstat(fd, &stat_buff) != 0 || stat_buff.st_size != RTFS_MAX_FILE_SIZE)
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}

around_seek_test:

    /* Test chsize (truncate) to zero */
    target_value = 0;
    if (po_chsize(fd, target_value) != 0)
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    if (pc_fstat(fd, &stat_buff) != 0 || stat_buff.st_size != target_value)
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    if (po_close(fd) != 0)
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    if (pc_stat(test_file_name, &stat_buff) != 0 || stat_buff.st_size != target_value)
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}

    /* Test chsize (expand) to RTFS_MAX_FILE_SIZE */
    fd = po_open(test_file_name, PO_RDWR, PS_IWRITE|PS_IREAD);
    if (fd < 0)
        { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    target_value = RTFS_MAX_FILE_SIZE;
    if (po_chsize(fd, target_value) != 0)
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    if (pc_fstat(fd, &stat_buff) != 0 || stat_buff.st_size != target_value)
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    if (po_close(fd) != 0)
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    if (pc_stat(test_file_name, &stat_buff) != 0 || stat_buff.st_size != target_value)
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}


    /* Test chsize (expand) to RTFS_MAX_FILE_SIZE */
    fd = po_open(test_file_name, PO_RDWR, PS_IWRITE|PS_IREAD);
    if (fd < 0)
        { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    target_value = RTFS_MAX_FILE_SIZE;
    if (po_chsize(fd, target_value) != 0)
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    if (pc_fstat(fd, &stat_buff) != 0 || stat_buff.st_size != target_value)
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    if (po_close(fd) != 0)
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    if (pc_stat(test_file_name, &stat_buff) != 0 || stat_buff.st_size != target_value)
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}

    /* Test chsize (truncate) to 4000 meg */
    fd = po_open(test_file_name, PO_RDWR, PS_IWRITE|PS_IREAD);
    if (fd < 0)
        { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    target_value = ((max_filesize_megs - 1000) * ONE_MEG);
    if (po_chsize(fd, target_value) != 0)
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    if (pc_fstat(fd, &stat_buff) != 0 || stat_buff.st_size != target_value)
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    if (po_close(fd) != 0)
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    if (pc_stat(test_file_name, &stat_buff) != 0 || stat_buff.st_size != target_value)
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}

    /* Test chsize (truncate) to 1000 meg */
    fd = po_open(test_file_name, PO_RDWR, PS_IWRITE|PS_IREAD);
    if (fd < 0)
        { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    target_value = (1000 * ONE_MEG);
    if (po_chsize(fd, target_value) != 0)
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}

    if (pc_fstat(fd, &stat_buff) != 0 || stat_buff.st_size != target_value)
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    if (po_close(fd) != 0)
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    if (pc_stat(test_file_name, &stat_buff) != 0 || stat_buff.st_size != target_value)
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}

    /* Test chsize (expand) to ffffffff0  */
    fd = po_open(test_file_name, PO_RDWR, PS_IWRITE|PS_IREAD);
    if (fd < 0)
        { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    target_value = RTFS_MAX_FILE_SIZE-16;
    if (po_chsize(fd, target_value) != 0)
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    if (pc_fstat(fd, &stat_buff) != 0 || stat_buff.st_size != target_value)
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    if (po_close(fd) != 0)
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}
    if (pc_stat(test_file_name, &stat_buff) != 0 || stat_buff.st_size != target_value)
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}

#if (FILL_TEST_HIT_WRAP)
unlink_it:
#endif


    if (!pc_unlink(test_file_name))
    { regress_error(RGE_LARGEFILETEST); return(FALSE);}


    return(TRUE);
}
//=====
#endif /* (DO_LARGE_FILE_TEST) */

/*
<TEST>     Procedure: Test file behavior when handles are opened multiple times
<TEST>     Test file create, reopen, close, flush and delete operations files
<TEST>     Verify proper operation of exclusive and shared open modes
<TEST>     Verify proper operation when a file is opened multiple times simulteously
<TEST>     Verify proper operation of functions (delete, chsize) that require exclusive access to a file
*/

static int fdarray[250];
/* Test file manipulation routines */
static BOOLEAN do_file_test(int loop_count, BOOLEAN do_clean)
{
int i;
int j;
dword index;
dword di;
int ntestfiles = prtfs_cfg->cfg_NUSERFILES;

    if (ntestfiles >= 250)
        ntestfiles = 250;
#if (DO_LARGE_FILE_TEST)
    if (!loop_count)
	{
        if (!do_large_file_test())
            return(FALSE);
#if (INCLUDE_MATH64)
		if (!do_comprehensive_filio_test())
            return(FALSE);
#endif
	}
#endif

#if (VERBOSE)
    RTFS_PRINT_STRING_1((byte *)"Performing File io test",PRFLG_NL); /* "Performing File io test" */
#endif

    fdarray[0] = po_open(test_file_name, PO_RDWR|PO_CREAT|PO_EXCL, PS_IWRITE|PS_IREAD);
    if (fdarray[0] < 0)
        { regress_error(RGE_OPEN); return(FALSE);}
    if (!check_errno(0)) return(FALSE);
    for (i = 1; i < ntestfiles;i++)
    {
        /* This should fail */
        fdarray[i] = po_open(test_file_name, PO_RDWR|PO_CREAT|PO_EXCL, PS_IWRITE|PS_IREAD);
        if (fdarray[i] >= 0)
            { regress_error(RGE_OPEN); return(FALSE);}
        if (!check_errno(PEEXIST)) return(FALSE);

        /* This should work */
        fdarray[i] = po_open(test_file_name, PO_RDWR, PS_IWRITE|PS_IREAD);
        if (fdarray[i] < 0)
            { regress_error(RGE_OPEN); return(FALSE);}
        if (!check_errno(0)) return(FALSE);
    }

    /* Write into the file using all file descriptors */
    index = 0;
    for (i = 0; i < ntestfiles;i++)
    {
        if (po_lseek(fdarray[i], 0L, PSEEK_END) == -1L)
            { regress_error(RGE_SEEK); return(FALSE);}
        if (!check_errno(0)) return(FALSE);
        for (j = 0; j < NLONGS; j++)
            test_rtfs_buf[j] = index++;
        if (po_write(fdarray[i], (byte *) test_rtfs_buf, (NLONGS*4)) != (NLONGS*4))
            { regress_error(RGE_WRITE); return(FALSE);}
        if (!check_errno(0)) return(FALSE);
    }

    /* Read file using all fds */
    index = 0;
    for (i = 0; i < ntestfiles;i++)
    {
        if (po_lseek(fdarray[i], (dword) (index*4), PSEEK_SET) != (long) (index*4))
            { regress_error(RGE_SEEK); return(FALSE);}
        if (!check_errno(0)) return(FALSE);
        if (po_read(fdarray[i], (byte *) test_rtfs_buf, (NLONGS*4)) != (NLONGS*4))
            { regress_error(RGE_READ); return(FALSE);}
        if (!check_errno(0)) return(FALSE);
        for (j = 0; j < NLONGS; j++)
        {
            if (test_rtfs_buf[j] != index++)
                { regress_error(RGE_READ); return(FALSE);}
        }
    }
    /* This should fail if more than one file is open */
    if (ntestfiles > 1)
    {
        if (po_truncate(fdarray[0], 256))
            { regress_error(RGE_TRUNC); return(FALSE);}

        if (!check_errno(PEACCES)) return(FALSE);
    }
    if (!po_flush(fdarray[0]))
        { regress_error(RGE_FLUSH); return(FALSE);}
    if (!check_errno(0)) return(FALSE);

    /* Close all secondary files */
    for (i = 1; i < ntestfiles;i++)
    {
        if (po_close(fdarray[i]) != 0)
            { regress_error(RGE_CLOSE); return(FALSE);}
        if (!check_errno(0)) return(FALSE);
    }
    /* This should work */
    if (!po_truncate(fdarray[0], 256))
        { regress_error(RGE_TRUNC); return(FALSE);}
    if (!check_errno(0)) return(FALSE);

    /* This should work */
    for (di = 0; di < 64 * 1024; di += 1024)
    {
        if (po_chsize(fdarray[0], di) != 0)
        { regress_error(RGE_CHSIZE); return(FALSE);}
        if (!check_errno(0)) return(FALSE);
    }
    /* This should work */
    for (di = 63 * 1024; di; di -= 1024)
    {
        if (po_chsize(fdarray[0], di) != 0)
        { regress_error(RGE_CHSIZE); return(FALSE);}
        if (!check_errno(0)) return(FALSE);
    }
    /* This should fail */
    if (pc_unlink(test_file_name))
        { regress_error(RGE_UNLINK); return(FALSE);}
    if (!check_errno(PEACCES)) return(FALSE);

    if (po_close(fdarray[0]) != 0)
        { regress_error(RGE_CLOSE); return(FALSE);}
    if (!check_errno(0)) return(FALSE);

    if (!pc_mv(test_file_name, test_newfile_name))
        { regress_error(RGE_MV); return(FALSE);}
    if (!check_errno(0)) return(FALSE);
    /* This should work */
    /* Manually delete on odd loops, even loops use deltree */
    if (do_clean && (loop_count & 1))
    {
        if (!pc_unlink(test_newfile_name))
            { regress_error(RGE_UNLINK); return(FALSE);}
        if (!check_errno(0)) return(FALSE);
    }
    return(TRUE);
}

/*
<TEST>     Procedure:Test correct operation of append
<TEST>
<TEST>     Verify that po_open with append mode correctly appends to the end of the file
<TEST>     after a file is reopened or after the file pointer has been moved.
*/

/* Test file manipulation routines */
static BOOLEAN do_append_test(void)
{
int i,j,fd, residual;
dword index = 0;
dword expected_file_size = 0;
byte *pb;
ERTFS_STAT stat_buff;

#if (VERBOSE)
    RTFS_PRINT_STRING_1((byte *)"Performing File append test",PRFLG_NL);
#endif
    /* Delete the test file if it exists */
    pc_unlink(test_file_name);
    residual = (NLONGS*4) - 1;
    index = 0;
    /* write to the file in append mode and be sure everything works correctly */
    for (i = 0; i < 128;i++) /* Make 256K file using append (NLONGS == FIVE12) */
    {
        for (j = 0; j < NLONGS; j++)
            test_rtfs_buf[j] = index++;
        fd = po_open(test_file_name, PO_RDWR|PO_CREAT|PO_APPEND, PS_IWRITE|PS_IREAD);
        if (fd < 0)
            { regress_error(RGE_OPEN); return(FALSE);}
        if (!check_errno(0)) return(FALSE);
        if (pc_fstat(fd, &stat_buff) != 0 || stat_buff.st_size != expected_file_size)
            { regress_error(RGE_APPENDFILETEST); return(FALSE);}
        pb = (byte *) test_rtfs_buf;
        /* Write one byte, then seek to beginning and write the rest */
        if (po_write(fd, (byte *) pb, 1) != 1)
            { regress_error(RGE_WRITE); return(FALSE);}
        pb += 1;
        if (po_lseek(fd, 0L, PSEEK_SET) != 0)
            { regress_error(RGE_SEEK); return(FALSE);}
        if (po_write(fd, (byte *) pb, residual) != residual) /* Should append */
            { regress_error(RGE_WRITE); return(FALSE);}
        expected_file_size += (NLONGS*4);
        if (pc_fstat(fd, &stat_buff) != 0 || stat_buff.st_size != expected_file_size)
            { regress_error(RGE_APPENDFILETEST); return(FALSE);}
        po_close(fd);
    }

    /* Read the file and verify contents */
    index = 0;
    fd = po_open(test_file_name, PO_RDONLY, PS_IWRITE|PS_IREAD);
    if (fd < 0)
        { regress_error(RGE_OPEN); return(FALSE);}
    for (i = 0; i < 128;i++)
    {
        if (po_read(fd, (byte *) test_rtfs_buf, (NLONGS*4)) != (NLONGS*4))
            { regress_error(RGE_READ); return(FALSE);}
        if (!check_errno(0)) return(FALSE);
        for (j = 0; j < NLONGS; j++)
        {
            if (test_rtfs_buf[j] != index++)
                { regress_error(RGE_READ); return(FALSE);}
        }
    }
    po_close(fd);

    pc_unlink(test_file_name);
    return(TRUE);
}

/*
<TEST>     Procedure:Test correct operation of po_lseek()
<TEST>
<TEST>     verify that po_lseek() works correctly
<TEST>     po_lseek() with negative offsets is performed by changing the sign if
<TEST>     and using an unsigned file pointer move.
<TEST>     This test creates a 16 byte file and verifies that the following arguments result in the the following results.
<TEST>	     origin    offset  returns  errno
<TEST>      PSEEK_SET	0       0	    0
<TEST>            	    -1	    -1	    PEINVALIDPARMS
<TEST>      	        8	    8	    0
<TEST>      	        16	    16	    0
<TEST>      	        17	    16	    0 ??
<TEST>      PSEK_CUR	0	    8	0   (from current postion=8)
<TEST>            	    -9	    0	    0
<TEST>      	        -8	    0	    0
<TEST>      	        8	    16	    0  ?
<TEST>      	        9	    16	    0
<TEST>      PSEEK_END	0	    16	    0
<TEST>      	        1	    -1	    PEINVALIDPARMS
<TEST>      	        -9	    9	    0
<TEST>      	        -16	    0	    0
<TEST>      	        -17	    0	    0  ??
*/

/* Test po_lseek() (signed seek) */

static BOOLEAN do_one_po_lseek_test(int fd, int origin, int offset, int expected_return, int expected_errno)
{
    if (origin == PSEEK_CUR)
    { /* Seek curr test starts from origin 8 */
        if (po_lseek(fd, 8, PSEEK_SET) != 8)
        {
            regress_error(RGE_SEEK);
            return(FALSE);
        }
    }
    if (po_lseek(fd, offset, origin) == expected_return)
    {
        return(check_errno(expected_errno));
    }
    regress_error(RGE_SEEK);
    return(FALSE);
}


static BOOLEAN do_po_lseek_test(void)
{
int fd;

#if (VERBOSE)
    RTFS_PRINT_STRING_1((byte *)"Performing po_lseek (signed seek) test",PRFLG_NL);
#endif
    /* Delete the test file if it exists */
    pc_unlink(test_file_name);
    fd = po_open(test_file_name, PO_RDWR|PO_CREAT, PS_IWRITE|PS_IREAD);
    if (fd < 0)
    { regress_error(RGE_OPEN); return(FALSE);}
    if (po_write(fd, (byte *) &test_rtfs_buf[0], 16) != 16)
    { regress_error(RGE_WRITE); return(FALSE);}
    po_close(fd);
    fd = po_open(test_file_name, PO_RDWR, PS_IWRITE|PS_IREAD);
    if (fd < 0)
    { regress_error(RGE_OPEN); return(FALSE);}
/*    PSEEK_SET	0       0	    0
           	    -1	    16	    0
      	        16	    16	    0
      	        17	    16	    0 ?? */
    if (!do_one_po_lseek_test(fd, PSEEK_SET, 0, 0, 0)) return(FALSE);
    if (!do_one_po_lseek_test(fd, PSEEK_SET, -1, -1, PEINVALIDPARMS)) return(FALSE);
    if (!do_one_po_lseek_test(fd, PSEEK_SET, 8, 8, 0)) return(FALSE);
    if (!do_one_po_lseek_test(fd, PSEEK_SET, 16, 16, 0)) return(FALSE);
    if (!do_one_po_lseek_test(fd, PSEEK_SET, 17, 16, 0)) return(FALSE);
/*     PSEK_CUR	0	    8	0   (from current postion=8)
           	    -9	    0	    0
      	        -8	    0	    0
      	        8	    16	    0  ?
      	        9	    16	    0 */
    if (!do_one_po_lseek_test(fd, PSEEK_CUR, 0, 8, 0)) return(FALSE);
    if (!do_one_po_lseek_test(fd, PSEEK_CUR, -9, 0, 0)) return(FALSE);
    if (!do_one_po_lseek_test(fd, PSEEK_CUR, -8, 0, 0)) return(FALSE);
    if (!do_one_po_lseek_test(fd, PSEEK_CUR, 8, 16, 0)) return(FALSE);
    if (!do_one_po_lseek_test(fd, PSEEK_CUR, 9, 16, 0)) return(FALSE);
/*     PSEEK_END	0	    16	    0
      	        1	    15	    0
      	        -16	    0	    0
      	        -17	    0	    0  ?? */
    if (!do_one_po_lseek_test(fd, PSEEK_END, 0, 16, 0)) return(FALSE);
    if (!do_one_po_lseek_test(fd, PSEEK_END, 1, -1, PEINVALIDPARMS)) return(FALSE);
    if (!do_one_po_lseek_test(fd, PSEEK_END, -9, 7, 0)) return(FALSE);
    if (!do_one_po_lseek_test(fd, PSEEK_END, -16, 0, 0)) return(FALSE);
    if (!do_one_po_lseek_test(fd, PSEEK_END, -17, 0, 0)) return(FALSE);

    po_close(fd);
    pc_unlink(test_file_name);
    return(TRUE);
}

/*
<TEST>     Procedure:Test correct errno setting
<TEST>
<TEST>     Every API call that is made by the basic regression test is followed by a check
<TEST>     of the errno setting.
<TEST>     Verify that errno is cleaerd when there is no error
<TEST>     Verify that errno contains the correct value when API calls return an error status
*/

static BOOLEAN check_errno(int expected_error)
{
int current_errno;

    current_errno = get_errno();
    if (current_errno != expected_error)
    {
        regress_error(RGE_ERRNO);
        RTFS_PRINT_LONG_1((dword) get_errno(), PRFLG_NL);
        return(FALSE);
    }
    else
    {
        rtfs_set_errno(PEILLEGALERRNO, __FILE__, __LINE__); /* Set it to illegal, it should be cleared */
        return(TRUE);
    }
}

static BOOLEAN check_if_exfat(void)
{
DRIVE_INFO drive_info_struture;

    if (!pc_diskio_info(test_drive, &drive_info_struture, TRUE))
    {
        RTFS_PRINT_STRING_1((byte *)"Drive info failed ", PRFLG_NL);
        return(FALSE);
    }
    return(drive_info_struture.is_exfat);
}

/*
<TEST>     Procedure:Test correct operation of the check disk utility.
<TEST>
<TEST>     Create lost cluster chains and verify that checkdisk can find and remove
<TEST>     the lost chains.
*/

/*
<TEST>     Procedure:Test correct operation of the check disk utility.
<TEST>
<TEST>     Create lost cluster chains and verify that checkdisk can find and remove
<TEST>     the lost chains.
*/
/* See appcmdsh.c */
extern byte cmdshell_check_disk_scratch_memory[];
extern dword cmdshell_size_check_disk_scratch_memory;

/* Test operation of checkdisk */
static BOOLEAN create_lost_chains(int n_chains,dword clusters_per_chain);
void rtfs_print_one_string(byte *pstr,int flags);

#define REGRESS_CHKDSK_VERBOSE 0 /* or CHKDSK_VERBOSE */

#ifdef RTFS_MAJOR_VERSION
/* Version 6 drive info access */
#define DRIVE_INFO(D,F) D->drive_info.F
#define heapfragfilename (byte *) "BIGFRAG"
#define fragfilename (byte *) "FRAG"
#define brokenfile1 (byte *) "BROKEN1"
#define brokenfile2 (byte *) "BROKEN2"
#define root_dir (byte *) "\\"
#else
#define INCLUDE_VFAT VFAT
#define DRIVE_INFO(D,F) D->F
#define fatop_pfaxx(D, CLUSTER, VALUE) FATOP(D)->fatop_pfaxx(D, CLUSTER, VALUE)
#define fatop_flushfat(D) FATOP(D)->fatop_flushfat(D->driveno)
/* Rtfs 4 emulations of Rtfs 6 functions and structures */
typedef struct region_fragment {
        dword start_location;
        dword end_location;
        struct region_fragment *pnext;
        } REGION_FRAGMENT;
#if (INCLUDE_CS_UNICODE)
#define heapfragfilename (byte *) L"BIGFRAG"
#define fragfilename (byte *) L"FRAG"
#define brokenfile1 (byte *) L"BROKEN1"
#define brokenfile2 (byte *) L"BROKEN2"
#define root_dir (byte *) L"\\"
#else
#define heapfragfilename (byte *)"BIGFRAG"
#define fragfilename (byte *) "FRAG"
#define brokenfile1 (byte *) "BROKEN1"
#define brokenfile2 (byte *) "BROKEN2"
#define root_dir (byte *) "\\"
#endif
#endif

/* See apichkdsk.c */
#define CHKDSK_FAT_EOF_VAL   0xffffffff
#define CHKDSK_FAT_ERROR_VAL 0xfffffffe

dword chkdsk_next_cluster(DDRIVE *pdr, dword cluster);
void chkdsk_build_chk_name(byte *pretname, dword file_no);

static DDRIVE *path_to_drive_struct(byte *path);
static dword test_drive_cluster_size_bytes(byte *path);
static BOOLEAN chktest_create_one_file(byte *file_name, int n_clusters, int cl_per_write, int interleaf);
static BOOLEAN chktest_getset_fcluster(byte *file_name, BOOLEAN isset, dword sval, dword *rval);
static BOOLEAN chktest_getset_fsize(byte *file_name, BOOLEAN isset, dword sval, dword *rval);
static int chktest_load_cluster_list(byte *file_name, int list_size, REGION_FRAGMENT *pcluster_list);
static BOOLEAN chkdsk_test_lost_clusters(CHKDSK_CONTEXT *pchkcontext, int scratch_memory_size, dword n_test_chains, dword clusters_per_chain,BOOLEAN do_check_files);

static BOOLEAN _do_chkdsk_test(int scratch_memory_size);
static dword chkdisk_cluster_by_offset(dword offset, REGION_FRAGMENT *pregion_array);
static BOOLEAN chktest_remove_check_files(dword n_test_chains);
static BOOLEAN chktest_check_check_files(dword n_test_chains, dword clusters_per_chain,int scratch_memory_size);

static BOOLEAN do_chkdsk_test(void)
{
    if (!pc_set_default_drive(test_drive))
    	return(FALSE);
    if (!pc_set_cwd(root_dir))
    	return(FALSE);


	rtfs_print_one_string((byte *)"Fragmenting freespace to force multiple fat scans", PRFLG_NL);
	/* Create a file with 400 interleaved clusters this will fragment freespace and force check disk to perform multiple scans */
	if (!chktest_create_one_file(heapfragfilename, 400,1,1))
	{
		rtfs_print_one_string((byte *)"Failed fragmenting the disk", PRFLG_NL);
		return (FALSE);
	}
	pc_diskflush(test_drive);
	if (!_do_chkdsk_test(200 * sizeof(REGION_FRAGMENT)))
	{
		pc_unlink(heapfragfilename);
		return(FALSE);
	}
	pc_unlink(heapfragfilename);
	return(TRUE);
}

static BOOLEAN _do_chkdsk_test(int scratch_memory_size)
{
CHKDISK_STATS chkstat;
CHKDSK_CONTEXT chkcontext;
/*dword current_lost_cluster,current_crossed_points,current_bad_lfns; */
DDRIVE *test_drive_structure;
int test_iteration;
dword file_delete_option = 0;

    rtfs_print_one_string((byte *)"Testing check disk", PRFLG_NL);
	test_drive_structure = path_to_drive_struct(test_drive);
	if (!test_drive_structure)
		return(FALSE);

	/* Scan the disk first */
    if (!pc_check_disk_ex(test_drive, &chkstat, REGRESS_CHKDSK_VERBOSE, &chkcontext, (void *)&cmdshell_check_disk_scratch_memory[0], scratch_memory_size))
    {
       rtfs_print_one_string((byte *)"Checkdisk failed", PRFLG_NL);
       return(FALSE);
    }
    while (chkstat.n_lost_chains)
    {
        rtfs_print_one_string((byte *)"Cleaning lost chains to start n_lost == ", 0);
        rtfs_print_long_1(chkstat.n_lost_chains, PRFLG_NL);
        if (!pc_check_disk_ex(test_drive, &chkstat, (REGRESS_CHKDSK_VERBOSE|CHKDSK_FIXPROBLEMS|CHKDSK_FREELOSTCLUSTERS), &chkcontext, (void *)&cmdshell_check_disk_scratch_memory[0], scratch_memory_size))
        {
            rtfs_print_one_string((byte *)"Checkdisk failed", PRFLG_NL);
            return(FALSE);
        }
    }

	if (!chkdsk_test_lost_clusters(&chkcontext, scratch_memory_size, 300, 1, TRUE))	/* Check 300 lost cluster chains one cluster per file, create check files */
		return (FALSE);
	if (!chkdsk_test_lost_clusters(&chkcontext, scratch_memory_size, 20, 1, FALSE))	/* Check 20 lost cluster chains one cluster per file, free lost clusters */
		return (FALSE);
	if (!chkdsk_test_lost_clusters(&chkcontext, scratch_memory_size, 20, 1, TRUE))		/* Check 20 lost cluster chains one cluster per file, create check files */
		return (FALSE);
	if (!chkdsk_test_lost_clusters(&chkcontext, scratch_memory_size, 100, 1, FALSE))	/* Check 100 lost cluster chains one cluster per file, free lost clusters */
		return (FALSE);
	if (!chkdsk_test_lost_clusters(&chkcontext, scratch_memory_size, 100, 1, TRUE))	/* Check 100 lost cluster chains one cluster per file, create check files */
		return (FALSE);
	if (!chkdsk_test_lost_clusters(&chkcontext, scratch_memory_size, 300, 1, FALSE))	/* Check 300 lost cluster chains one cluster per file, free lost clusters */
		return (FALSE);
	if (!chkdsk_test_lost_clusters(&chkcontext, scratch_memory_size, 300, 1, TRUE))	/* Check 300 lost cluster chains one cluster per file, create check files */
		return (FALSE);


	/* Test check file creation */
	/* ======================== */

	/* Test file looping on itself */
    rtfs_print_one_string((byte *)"Testing an endless loop in a file\'s cluster chain", PRFLG_NL);
	{
		REGION_FRAGMENT file_fragments[20];
		int n_fragments;
		/* Create a file with 10 clusters in 5 fragments 2 clusters each */
		if (!chktest_create_one_file(brokenfile1, 10,2,1))
		{
endless_file_test_failed:
			rtfs_print_one_string((byte *)"File endless loop test failed ", PRFLG_NL);
			return (FALSE);
		}
		/* Get the list */
		n_fragments=chktest_load_cluster_list(brokenfile1, 10, &file_fragments[0]);
		if (n_fragments < 5)
			goto endless_file_test_failed;
		/* Loop it, point the 4th fragment at the 3rd */
		if (!fatop_pfaxx(test_drive_structure, file_fragments[3].start_location, file_fragments[2].start_location))
			goto endless_file_test_failed;
		if (!fatop_flushfat(test_drive_structure))
			goto endless_file_test_failed;
		/* Now check again, should be endless loop status */
		if (!pc_check_disk_ex(test_drive, &chkstat, REGRESS_CHKDSK_VERBOSE, &chkcontext, (void *)&cmdshell_check_disk_scratch_memory[0], scratch_memory_size))
			goto endless_file_test_failed;
		if (!chkstat.has_endless_loop)
			goto endless_file_test_failed;
		/* Now clear it */
		if (!pc_check_disk_ex(test_drive, &chkstat, REGRESS_CHKDSK_VERBOSE|CHKDSK_FIXPROBLEMS|CHKDSK_FREELOSTCLUSTERS, &chkcontext, (void *)&cmdshell_check_disk_scratch_memory[0], scratch_memory_size))
			goto endless_file_test_failed;
		if (!chkstat.has_endless_loop) /* Should still report it */
			goto endless_file_test_failed;
		/* Note: the number of freed clusters is wrong */
		/* Now check again, endless loop status should be cleared */
		if (!pc_check_disk_ex(test_drive, &chkstat, REGRESS_CHKDSK_VERBOSE, &chkcontext, (void *)&cmdshell_check_disk_scratch_memory[0], scratch_memory_size))
			goto endless_file_test_failed;
		if (chkstat.has_endless_loop)
			goto endless_file_test_failed;
    }

	/* ======================== */

	/* Test directory looping on itself */
	/* ======================== */

	/* Test crossed files */

	rtfs_print_one_string((byte *)"Testing files with crossed cluster chains", PRFLG_NL);
	for (test_iteration = 0; test_iteration < 2; test_iteration++)
	{
		REGION_FRAGMENT file1_fragments[20];
		REGION_FRAGMENT file2_fragments[20];
		int n_fragments;
		/* Create one file with 10 clusters in 5 fragments 2 clusters each */
		if (!chktest_create_one_file(brokenfile1, 10,2,1))
		{
crossed_file_test_failed:
			rtfs_print_one_string((byte *)"File crossed chain test failed ", PRFLG_NL);
			return (FALSE);
		}
		/* Get the list */
		n_fragments=chktest_load_cluster_list(brokenfile1, 10, &file1_fragments[0]);
		if (n_fragments < 5)
			goto crossed_file_test_failed;
		/* Create one file with 10 clusters in 5 fragments 2 clusters each */
		if (!chktest_create_one_file(brokenfile2, 10,2,1))
			goto crossed_file_test_failed;
		/* Get the list */
		n_fragments=chktest_load_cluster_list(brokenfile2, 10, &file2_fragments[0]);
		if (n_fragments < 5)
			goto crossed_file_test_failed;

		/* Cross the 5th cluster of the second file with the sixth cluster fragment first file */
		/* This leaves the calculated file size the same for both files, otherwise wrong sized files are freed by CHKDSK_FREEFILESWITHERRORS mode */
		if (!(fatop_pfaxx(test_drive_structure, chkdisk_cluster_by_offset(4, &file2_fragments[0]), chkdisk_cluster_by_offset(5, &file1_fragments[0]))))
			goto crossed_file_test_failed;
		if (!fatop_flushfat(test_drive_structure))
			goto crossed_file_test_failed;
		/* Now check again, should be crossed chains status */
		if (!pc_check_disk_ex(test_drive, &chkstat, REGRESS_CHKDSK_VERBOSE, &chkcontext, (void *)&cmdshell_check_disk_scratch_memory[0], scratch_memory_size))
			goto crossed_file_test_failed;
		if (chkstat.n_crossed_chains != 2)
			goto crossed_file_test_failed;

		/* Now clear it */
		if (test_iteration == 0)
		{
			if (!pc_check_disk_ex(test_drive, &chkstat, REGRESS_CHKDSK_VERBOSE|CHKDSK_FIXPROBLEMS|CHKDSK_FREELOSTCLUSTERS, &chkcontext, (void *)&cmdshell_check_disk_scratch_memory[0], scratch_memory_size))
				goto crossed_file_test_failed;
			/* 5 clusters should have been freed */
			if (chkstat.n_clusters_freed != 5)
				goto crossed_file_test_failed;
		}
		else
		{
			if (!pc_check_disk_ex(test_drive, &chkstat, REGRESS_CHKDSK_VERBOSE|CHKDSK_FIXPROBLEMS|CHKDSK_FREELOSTCLUSTERS|CHKDSK_FREEFILESWITHERRORS, &chkcontext, (void *)&cmdshell_check_disk_scratch_memory[0], scratch_memory_size))
				goto crossed_file_test_failed;
			/* 20 clusters should have been freed */
			if (chkstat.n_clusters_freed != 20)
				goto crossed_file_test_failed;
		}
		if (chkstat.n_crossed_chains != 2)
			goto crossed_file_test_failed;
		/* Now check again, should be no crossed chains */
		if (!pc_check_disk_ex(test_drive, &chkstat, REGRESS_CHKDSK_VERBOSE, &chkcontext, (void *)&cmdshell_check_disk_scratch_memory[0], scratch_memory_size))
			goto crossed_file_test_failed;
		if (chkstat.n_crossed_chains != 0)
			goto crossed_file_test_failed;
		if (test_iteration == 0)
		{	/* The size of the two files combined should be exactly 15 clusters */
			dword fsize1, fsize2;
			fsize1 = fsize2 = 0;
			if (!chktest_getset_fsize(brokenfile1, FALSE, 0, &fsize1))
				goto crossed_file_test_failed;
			if (!chktest_getset_fsize(brokenfile2, FALSE, 0, &fsize2))
				goto crossed_file_test_failed;
			if ((fsize1 + fsize2) != (15 * test_drive_cluster_size_bytes(test_drive)))
				goto crossed_file_test_failed;
			if (!pc_check_disk_ex(test_drive, &chkstat, REGRESS_CHKDSK_VERBOSE, &chkcontext, (void *)&cmdshell_check_disk_scratch_memory[0], scratch_memory_size))
				goto crossed_file_test_failed;

		}
		else
		{	/* The files should be gone now */
			dword fsize = 0;
			if (chktest_getset_fsize(brokenfile1, FALSE, 0, &fsize) || chktest_getset_fsize(brokenfile2, FALSE, 0, &fsize))
				goto crossed_file_test_failed;
		}
	}
	/* ======================== */
	/* Test crossed subdirectories */
	/* ======================== */
	/* Test crossed file and subdirectories */
	/* ======================== */

	/* Test bad file size */
	rtfs_print_one_string((byte *)"Testing bad file sizes", PRFLG_NL);
	for (test_iteration = 0; test_iteration < 4; test_iteration++)
	{
		file_delete_option = 0;

		if (test_iteration < 2)
			file_delete_option = CHKDSK_FREEFILESWITHERRORS;

		/* Create one file with 10 clusters in 5 fragments 2 clusters each */
		if (!chktest_create_one_file(brokenfile1, 10,2,1))
		{
bad_file_size_test_failed:
			rtfs_print_one_string((byte *)"Bad file size test failed. ", PRFLG_NL);
			return (FALSE);
		}
		{
			dword fsize, rval;
			/* Try setting the size larger and smaller than the chain */
			if (test_iteration == 0 || test_iteration == 2)
				fsize =  18 * test_drive_cluster_size_bytes(test_drive);
			else
				fsize =  2 * test_drive_cluster_size_bytes(test_drive);

			if (!chktest_getset_fsize(brokenfile1, TRUE, fsize, &rval))
				goto bad_file_size_test_failed;
			if (!chktest_getset_fsize(brokenfile1, FALSE, 0, &rval))
				goto bad_file_size_test_failed;
			if (rval != fsize)
				goto bad_file_size_test_failed;
			/* Now fix it */
			if (!pc_check_disk_ex(test_drive, &chkstat, file_delete_option|REGRESS_CHKDSK_VERBOSE|CHKDSK_FIXPROBLEMS|CHKDSK_FREELOSTCLUSTERS, &chkcontext, (void *)&cmdshell_check_disk_scratch_memory[0], scratch_memory_size))
				goto bad_file_size_test_failed;
			if (file_delete_option)
			{	/* File should be gone */
				if (chktest_getset_fsize(brokenfile1, FALSE, 0, &rval))
					goto bad_file_size_test_failed;
			}
			else
			{
				if (!chktest_getset_fsize(brokenfile1, FALSE, 0, &rval))
					goto bad_file_size_test_failed;
				if (rval != (10 * test_drive_cluster_size_bytes(test_drive)))
					goto bad_file_size_test_failed;
				if (!pc_unlink(brokenfile1))
					goto bad_file_size_test_failed;
			}
		}
	}
	/* ======================== */

	/* Test unterminated file chain */
    rtfs_print_one_string((byte *)"Testing an unterminated file chain", PRFLG_NL);
	for (test_iteration = 0; test_iteration < 2; test_iteration++)
	{
		REGION_FRAGMENT file_fragments[20];
		int n_fragments;
		dword rval;

		file_delete_option = 0;

		if (test_iteration == 1)
			file_delete_option = CHKDSK_FREEFILESWITHERRORS;

		/* Create a file with 10 clusters in 5 fragments 2 clusters each */
		if (!chktest_create_one_file(brokenfile1, 10,2,1))
		{
unterminated_file_test_failed:
			rtfs_print_one_string((byte *)"File endless loop test failed ", PRFLG_NL);
			return (FALSE);
		}
		/* Get the list */
		n_fragments=chktest_load_cluster_list(brokenfile1, 10, &file_fragments[0]);
		if (n_fragments < 5)
			goto unterminated_file_test_failed;
		/* Replce the terminater with zero */
		if (!(fatop_pfaxx(test_drive_structure, chkdisk_cluster_by_offset(9, &file_fragments[0]),0)))
			goto unterminated_file_test_failed;
		if (!fatop_flushfat(test_drive_structure))
			goto unterminated_file_test_failed;
		if (!pc_check_disk_ex(test_drive, &chkstat, REGRESS_CHKDSK_VERBOSE, &chkcontext, (void *)&cmdshell_check_disk_scratch_memory[0], scratch_memory_size))
			goto unterminated_file_test_failed;
		if (chkstat.n_unterminated_chains != 1)
			goto unterminated_file_test_failed;
		/* Now clear it */
		if (!pc_check_disk_ex(test_drive, &chkstat, file_delete_option|REGRESS_CHKDSK_VERBOSE|CHKDSK_FIXPROBLEMS|CHKDSK_FREELOSTCLUSTERS, &chkcontext, (void *)&cmdshell_check_disk_scratch_memory[0], scratch_memory_size))
			goto unterminated_file_test_failed;
		/* Check again */
		if (!pc_check_disk_ex(test_drive, &chkstat, REGRESS_CHKDSK_VERBOSE, &chkcontext, (void *)&cmdshell_check_disk_scratch_memory[0], scratch_memory_size))
			goto unterminated_file_test_failed;
		if (chkstat.n_unterminated_chains != 0)
			goto unterminated_file_test_failed;

		if (file_delete_option)
		{	/* File should be gone */
			if (chktest_getset_fsize(brokenfile1, FALSE, 0, &rval))
				goto unterminated_file_test_failed;
		}
		else
		{
			if (!pc_unlink(brokenfile1))
				goto unterminated_file_test_failed;

		}
    }
	/* ======================== */
	/* Test unterminated directory chain */
	/* ======================== */

	/* Test first cluster unallocated */
	/* Add test first cluster unallocated from a directory */
    rtfs_print_one_string((byte *)"Testing first cluster unallocated from file ", PRFLG_NL);
	for (test_iteration = 0; test_iteration < 2; test_iteration++)
	{
		REGION_FRAGMENT file_fragments[20];
		int n_fragments;
		dword rval;

		/* Create a file with 10 clusters in 5 fragments 2 clusters each */
		if (!chktest_create_one_file(brokenfile1, 10,2,1))
		{
unallocated_cluster_test_failed:
			rtfs_print_one_string((byte *)"First cluster unallocated test failed ", PRFLG_NL);
			return (FALSE);
		}
		/* Get the list */
		n_fragments=chktest_load_cluster_list(brokenfile1, 10, &file_fragments[0]);
		if (n_fragments < 5)
			goto unallocated_cluster_test_failed;
		/* Replace the first cluster with zero */
		if (!fatop_pfaxx(test_drive_structure, chkdisk_cluster_by_offset(0, &file_fragments[0]),0))
			goto unallocated_cluster_test_failed;
		if (!fatop_flushfat(test_drive_structure))
			goto unterminated_file_test_failed;
		if (!pc_check_disk_ex(test_drive, &chkstat, REGRESS_CHKDSK_VERBOSE, &chkcontext, (void *)&cmdshell_check_disk_scratch_memory[0], scratch_memory_size))
			goto unallocated_cluster_test_failed;
		if (chkstat.n_bad_dirents != 1)
			goto unallocated_cluster_test_failed;
		/* Now clear it */
		if (!pc_check_disk_ex(test_drive, &chkstat, REGRESS_CHKDSK_VERBOSE|CHKDSK_FIXPROBLEMS|CHKDSK_FREELOSTCLUSTERS, &chkcontext, (void *)&cmdshell_check_disk_scratch_memory[0], scratch_memory_size))
			goto unallocated_cluster_test_failed;
		/* Check again */
		if (!pc_check_disk_ex(test_drive, &chkstat, REGRESS_CHKDSK_VERBOSE, &chkcontext, (void *)&cmdshell_check_disk_scratch_memory[0], scratch_memory_size))
			goto unallocated_cluster_test_failed;
		if (chkstat.n_bad_dirents != 0)
			goto unallocated_cluster_test_failed;

		/* File should be gone */
		if (chktest_getset_fsize(brokenfile1, FALSE, 0, &rval))
			goto unallocated_cluster_test_failed;
    }
	/* ======================== */
	/* Test invalid value in cluster chain */
	/* Test unterminated file chain */
    rtfs_print_one_string((byte *)"Testing invalid value in a file cluster chain", PRFLG_NL);
	for (test_iteration = 0; test_iteration < 2; test_iteration++)
	{
		REGION_FRAGMENT file_fragments[20];
		int n_fragments;
		dword rval;

		file_delete_option = 0;

		if (test_iteration == 1)
			file_delete_option = CHKDSK_FREEFILESWITHERRORS;

		/* Create a file with 10 clusters in 5 fragments 2 clusters each */
		if (!chktest_create_one_file(brokenfile1, 10,2,1))
		{
invalid_cluster__file_test_failed:
			rtfs_print_one_string((byte *)"Invalid value in a file cluster chain test failed ", PRFLG_NL);
			return (FALSE);
		}
		/* Get the list */
		n_fragments=chktest_load_cluster_list(brokenfile1, 10, &file_fragments[0]);
		if (n_fragments < 5)
			goto invalid_cluster__file_test_failed;
		/* Replace a cluster with 1 an invalid value */
		if (!(fatop_pfaxx(test_drive_structure, chkdisk_cluster_by_offset(6, &file_fragments[0]), 1)))
			goto invalid_cluster__file_test_failed;
		if (!fatop_flushfat(test_drive_structure))
			goto invalid_cluster__file_test_failed;
		if (!pc_check_disk_ex(test_drive, &chkstat, REGRESS_CHKDSK_VERBOSE, &chkcontext, (void *)&cmdshell_check_disk_scratch_memory[0], scratch_memory_size))
			goto invalid_cluster__file_test_failed;
		if (chkstat.n_badcluster_values != 1)
			goto invalid_cluster__file_test_failed;
		/* Now clear it */
		if (!pc_check_disk_ex(test_drive, &chkstat, file_delete_option|REGRESS_CHKDSK_VERBOSE|CHKDSK_FIXPROBLEMS|CHKDSK_FREELOSTCLUSTERS, &chkcontext, (void *)&cmdshell_check_disk_scratch_memory[0], scratch_memory_size))
			goto invalid_cluster__file_test_failed;
		if (file_delete_option)
		{
			/* The whole chain was lost and freed */
			if (chkstat.n_clusters_freed != 10)
				goto invalid_cluster__file_test_failed;
		}
		else
		{
			/* The tail end of the chain was lost and freed */
			if (chkstat.n_clusters_freed != 3)
				goto invalid_cluster__file_test_failed;
		}
		/* Check again */
		if (!pc_check_disk_ex(test_drive, &chkstat, REGRESS_CHKDSK_VERBOSE, &chkcontext, (void *)&cmdshell_check_disk_scratch_memory[0], scratch_memory_size))
			goto invalid_cluster__file_test_failed;
		if (chkstat.n_badcluster_values != 0)
			goto invalid_cluster__file_test_failed;

		if (file_delete_option)
		{	/* File should be gone */
			if (chktest_getset_fsize(brokenfile1, FALSE, 0, &rval))
				goto invalid_cluster__file_test_failed;
		}
		else
		{
			if (!chktest_getset_fsize(brokenfile1, FALSE, 0, &rval))
				goto invalid_cluster__file_test_failed;
			if (rval != (7 * test_drive_cluster_size_bytes(test_drive)))
				goto invalid_cluster__file_test_failed;

			if (!pc_unlink(brokenfile1))
				goto invalid_cluster__file_test_failed;

		}
    }
	/* Test rescan because of too many fragments */
	/* Test rescan because of too many lost files and check creation */
   return (TRUE);
}
static BOOLEAN chkdsk_test_lost_clusters(CHKDSK_CONTEXT *pchkcontext, int scratch_memory_size,  dword n_test_chains, dword clusters_per_chain,BOOLEAN do_check_files)
{
CHKDISK_STATS chkstat;
dword opmode;

	if (do_check_files)
	{
		chktest_remove_check_files(n_test_chains);
		opmode = REGRESS_CHKDSK_VERBOSE|CHKDSK_FIXPROBLEMS;
	}
	else
		opmode = REGRESS_CHKDSK_VERBOSE|CHKDSK_FIXPROBLEMS|CHKDSK_FREELOSTCLUSTERS;

    rtfs_print_one_string((byte *)"Testing check disk lost chain support (", 0);
    rtfs_print_long_1(n_test_chains, 0);
    rtfs_print_one_string((byte *)" chains)", 0);
	if (do_check_files)
    	rtfs_print_one_string((byte *)" with check files", PRFLG_NL);
	else
   		rtfs_print_one_string((byte *)" not creating check files", PRFLG_NL);
    if (!create_lost_chains(n_test_chains,clusters_per_chain))
    {
check_disk_failed:
       rtfs_print_one_string((byte *)"Check disk failed", PRFLG_NL);
       return (FALSE);
    }
    if (!pc_check_disk_ex(test_drive, &chkstat, opmode, pchkcontext, (void *)&cmdshell_check_disk_scratch_memory[0], scratch_memory_size))
    {
       rtfs_print_one_string((byte *)"Checkdisk failed", PRFLG_NL);
       return(FALSE);
    }
    if (chkstat.n_lost_chains != n_test_chains || (!do_check_files && chkstat.n_clusters_freed != (n_test_chains*clusters_per_chain)))
    {
       rtfs_print_one_string((byte *)"Lost chain test failed, returned - ", 0);
       rtfs_print_long_1(chkstat.n_lost_chains, 0);
       rtfs_print_one_string((byte *)" - Should have been - ", 0);
       rtfs_print_long_1((dword)n_test_chains,PRFLG_NL);
       return (FALSE);
    }
	if (do_check_files)
	{
		if (!chktest_check_check_files(n_test_chains,clusters_per_chain,scratch_memory_size))
		{
			rtfs_print_one_string((byte *)"Failed verifying check files", PRFLG_NL);
			return(FALSE);
		}
//		chktest_remove_check_files(n_test_chains);
    }
    /* Now check again, should be no lost chains */
    if (!pc_check_disk_ex(test_drive, &chkstat, REGRESS_CHKDSK_VERBOSE, pchkcontext, (void *)&cmdshell_check_disk_scratch_memory[0], scratch_memory_size))
       goto check_disk_failed;
    if (chkstat.n_lost_chains)
    {
       rtfs_print_one_string((byte *)"Lost chain test failed, returned - ", 0);
       rtfs_print_long_1(chkstat.n_lost_chains, 0);
       rtfs_print_one_string((byte *)" - Should have been - zero", PRFLG_NL);
       return (FALSE);
    }
	return(TRUE);
}

static BOOLEAN chktest_remove_check_files(dword n_test_chains)
{
dword file_no;
byte file_name[60];
	for (file_no = 0; file_no < n_test_chains; file_no++)
	{
		chkdsk_build_chk_name(&file_name[0], file_no);
		if (!pc_unlink(&file_name[0]))
			if (get_errno() != PENOENT)
				return(FALSE);
	}
	return(TRUE);
}
static BOOLEAN chktest_check_check_files(dword n_test_chains, dword clusters_per_chain,int scratch_memory_size)
{
dword file_no, file_size, expected_file_size;
byte file_name[60];

	expected_file_size = test_drive_cluster_size_bytes(test_drive)*clusters_per_chain;

	for (file_no = 0; file_no < n_test_chains; file_no++)
	{
		chkdsk_build_chk_name(&file_name[0], file_no);
		/* Test if the check file exist and test the file size */
		if (!chktest_getset_fsize(file_name, FALSE, 0, &file_size))
			return(FALSE);
		if (file_size != expected_file_size)
			return(FALSE);
		if (chktest_load_cluster_list(file_name, scratch_memory_size/sizeof(REGION_FRAGMENT), (REGION_FRAGMENT *)&cmdshell_check_disk_scratch_memory[0]) < 1)
			return(FALSE);
		/* Test if the cluster chain is exactly clusters_per_chain */
		if (!chkdisk_cluster_by_offset(clusters_per_chain-1,  (REGION_FRAGMENT *)&cmdshell_check_disk_scratch_memory[0]))
			return(FALSE);
		if (chkdisk_cluster_by_offset(clusters_per_chain,  (REGION_FRAGMENT *)&cmdshell_check_disk_scratch_memory[0]))
			return(FALSE);
	}
	return(TRUE);
}





static DDRIVE *path_to_drive_struct(byte *path)
{
	return(pc_drno_to_drive_struct(pc_path_to_driveno(path, CS_CHARSET_NOT_UNICODE)));
}
static dword test_drive_cluster_size_bytes(byte *path)
{
	return ((dword) (dword)DRIVE_INFO(path_to_drive_struct(path),bytespcluster));
}

static int chktest_load_cluster_list(byte *file_name, int list_size, REGION_FRAGMENT *pcluster_list)
{
	dword cluster,next_cluster;
	int n_fragments = 0;
	DDRIVE *pdr;

	pdr = path_to_drive_struct(test_drive);
	if (!chktest_getset_fcluster(file_name, FALSE, 0, &cluster))
		return(0);
	pcluster_list->start_location = cluster;
	pcluster_list->end_location = cluster;
	(pcluster_list+1)->start_location =
	(pcluster_list+1)->end_location = 0;

	n_fragments = 1;
	do {
    	next_cluster = chkdsk_next_cluster(pdr, cluster);
    	if (next_cluster == CHKDSK_FAT_ERROR_VAL)
    		return(0);
    	else if (next_cluster == CHKDSK_FAT_EOF_VAL)
    	{
    		return(n_fragments);
    	}
    	else
    	{
    		if (next_cluster == (cluster+1))
    			pcluster_list->end_location = next_cluster;
    		else
    		{
    			if (list_size-- == 0)
    				return(0);
    			n_fragments += 1;
    			pcluster_list += 1;
    			pcluster_list->start_location = next_cluster;
    			pcluster_list->end_location = next_cluster;
    			(pcluster_list+1)->start_location =
    			(pcluster_list+1)->end_location = 0;
    		}
    		cluster = next_cluster;
    	}
	} while (next_cluster);
	return(n_fragments);
}
#ifndef RTFS_MAJOR_VERSION
static BOOLEAN chktest_readwrite_finode(DDRIVE *pdrive , dword my_block, int my_index, BOOLEAN iswrite, FINODE *pfinode)
{
BLKBUFF *pbuff;
DOSINODE *pi;

    pbuff = pc_read_blk(pdrive, my_block);
    if (pbuff)
    {
        pi = (DOSINODE *) &pbuff->data[0];
        /* Copy it off and write it   */
		if (iswrite)
		{
        	pc_ino2dos( (pi+my_index), pfinode );
        	if (!pc_write_blk(pbuff))
        	{
            	pc_discard_buf(pbuff);
            	return (FALSE);
        	}
		}
		else
			pc_dos2inode(pfinode , (pi+my_index) );
        pc_release_buf(pbuff);
        return (TRUE);
    }
    return (FALSE);
}
#endif

static BOOLEAN chktest_create_one_file(byte *file_name, int n_clusters, int cl_per_write, int interleaf)
{
int i,fd1,fd2;
int cluster_size;
	cluster_size = (int) test_drive_cluster_size_bytes(test_drive);

    /* Open a file and do a fake write to allocate a cluster */
    fd1 = po_open(file_name, PO_RDWR|PO_CREAT|PO_TRUNC, PS_IWRITE|PS_IREAD);
    if (fd1 < 0)
        return(FALSE);
	fd2 = 0;
	if (interleaf)
	{
    	fd2 = po_open(fragfilename, PO_RDWR|PO_CREAT|PO_TRUNC, PS_IWRITE|PS_IREAD);
    	if (fd2 < 0)
        	return(FALSE);
	}
	for (i = 0; i < n_clusters; i += cl_per_write)
	{
	int j;
		for (j = 0; j < cl_per_write; j++)
    		if (po_write(fd1, 0, cluster_size) != cluster_size)
        		return(FALSE);
		if (interleaf)
		{
    		if (po_write(fd2, 0, cluster_size) != cluster_size)
        		return(FALSE);
		}
	}
    if (interleaf)
	{
    	po_close(fd2);
		pc_unlink(fragfilename);
	}
	po_close(fd1);
    return(TRUE);
}

static BOOLEAN chktest_getset_fcluster(byte *file_name, BOOLEAN isset, dword sval, dword *rval)
{
#ifdef RTFS_MAJOR_VERSION
{
DIRENT_INFO dinfo;
    /* Now get/set the file start cluster filed */
    if (pc_get_dirent_info(file_name, &dinfo))
    {
        *rval = dinfo.fcluster;
		if (isset)
		{
        	dinfo.fcluster = sval;
        	if (!pc_set_dirent_info(file_name, &dinfo))
        		return(FALSE);
        }
       	return(TRUE);
    }
	else
    	return(FALSE);
}
#else
{
DSTAT statobj;
DROBJ *pobj;
FINODE myfinode;
    /* Get the first match */
    if (!pc_gfirst(&statobj, file_name))
		return(FALSE);
	/* The statobj.pobj points to a bogus finode, look up the match in the finode pool */
    pobj = (DROBJ *) statobj.pobj;
	/* Read the finode */
    if (!chktest_readwrite_finode(pobj->pdrive , pobj->blkinfo.my_block, pobj->blkinfo.my_index, FALSE, &myfinode))
	{
		pc_gdone(&statobj);
		return(FALSE);
	}
    *rval = pc_finode_cluster(pobj->pdrive, &myfinode);
	if (isset)
	{
		/* Change the fcluster value */
	   pc_pfinode_cluster(pobj->pdrive, &myfinode, sval);
	   /* Write it */
	   if (!chktest_readwrite_finode(pobj->pdrive , pobj->blkinfo.my_block, pobj->blkinfo.my_index, TRUE, &myfinode))
	   {
        	pc_gdone(&statobj);
            return(FALSE);
        }
	}
  	pc_gdone(&statobj);
	return(TRUE);
}
#endif
}

static BOOLEAN chktest_getset_fsize(byte *file_name, BOOLEAN isset, dword sval, dword *rval)
{
#ifdef RTFS_MAJOR_VERSION
{
DIRENT_INFO dinfo;
    /* Now get/set the file start cluster filed */
    if (pc_get_dirent_info(file_name, &dinfo))
    {
        *rval = dinfo.fsize;
		if (isset)
		{
			dinfo.fsize = sval;
        	if (!pc_set_dirent_info(file_name, &dinfo))
        		return(FALSE);
        }
       	return(TRUE);
    }
	else
    	return(FALSE);
}
#else
{
int fd1;
PC_FILE *pfile;
    /* Open a file and do a fake write to allocate a cluster */
    fd1 = po_open(file_name, PO_RDWR, PS_IWRITE|PS_IREAD);
    if (fd1 < 0)
        return(FALSE);
    pfile = prtfs_cfg->mem_file_pool+fd1;
   /* set fcluster to zero */
    *rval = pfile->pobj->finode->fsizeu.fsize;
	if (isset)
	{
    	pfile->pobj->finode->fsizeu.fsize = sval;
	   /* Write it */
       if (!pc_update_inode(pfile->pobj, FALSE, 0))
	   {
	   		po_close(fd1);
            return(FALSE);
        }
	}
	po_close(fd1);
	return(TRUE);
}
#endif
}

static BOOLEAN chktest_create_one_lost_chain(dword clusters_per_chain)
{
	dword rval;
	if (!chktest_create_one_file(test_file_name, (int)clusters_per_chain, 1, 0))
		return(FALSE);
	if (!chktest_getset_fcluster(test_file_name, TRUE, 0, &rval))
		return(FALSE);
    pc_unlink(test_file_name);
	return(TRUE);
}

static BOOLEAN create_lost_chains(int n_chains,dword clusters_per_chain)
{
    int i;
    for (i = 0; i < n_chains; i++)
    {
        if (!chktest_create_one_lost_chain(clusters_per_chain))
            return(FALSE);
    }
    return(TRUE);
}
static dword chkdisk_cluster_by_offset(dword offset, REGION_FRAGMENT *pregion_array)
{
dword start_offset, end_offset;
	start_offset = end_offset = 0;
	while (pregion_array->start_location)
	{
		end_offset += (pregion_array->end_location-pregion_array->start_location+1);
		if (offset >= start_offset && offset < end_offset)
			return(pregion_array->start_location + offset-start_offset);
		start_offset = end_offset;
		pregion_array++;
	}
	return(0);

}

#if (INCLUDE_RTFS_PROPLUS)
/*
<TEST>     Procedure:Cluster conversion test
<TEST>
<TEST>     Verify proper operation of cluster to sector and sector to cluster conversion routines.
<TEST>     pc_diskio_info
<TEST>     pc_cluster_to_sector
<TEST>     pc_sector_to_cluster
*/
static BOOLEAN do_cluster_conversion_test(BOOLEAN raw)
{
int driveno;
dword sector;
DRIVE_INFO drive_info_struture;

    if (raw)
    {  RTFS_PRINT_STRING_1((byte *)"Performing cluster to raw sector conversion test", PRFLG_NL); }
    else
    { RTFS_PRINT_STRING_1((byte *)"Performing cluster to sector conversion test", PRFLG_NL); }
    if (!pc_diskio_info(test_drive, &drive_info_struture, TRUE))
        goto return_false;
    driveno = pc_drname_to_drno(test_drive, CS_CHARSET_NOT_UNICODE);
    sector = pc_cluster_to_sector(driveno, 2, raw);
    if (!sector)
        goto return_false;
    if (pc_sector_to_cluster(driveno, sector, raw) != 2)
        goto return_false;
    if (pc_sector_to_cluster(driveno, sector+drive_info_struture.cluster_size-1, raw) != 2)
        goto return_false;
    if (pc_sector_to_cluster(driveno, sector-1, raw)) /* Out of bounds */
        goto return_false;
    sector = pc_cluster_to_sector(driveno, drive_info_struture.total_clusters+1, raw);
    if (!sector)
        goto return_false;
    /* Note: using drive_info_struture.total_clusters+XX because allocatable clusters start at 2 */
    if (pc_sector_to_cluster(driveno, sector, raw) != drive_info_struture.total_clusters+1)
        goto return_false;
    if (pc_sector_to_cluster(driveno, sector+drive_info_struture.cluster_size-1, raw) != drive_info_struture.total_clusters+1)
        goto return_false;
    if (pc_sector_to_cluster(driveno, sector+drive_info_struture.cluster_size, raw)) /* Out of bounds */
        goto return_false;
    if (pc_sector_to_cluster(driveno, sector-1, raw) != drive_info_struture.total_clusters)
        goto return_false;
    return(TRUE);
return_false:
    regress_error(RGE_CLUSTERCONVERSIONTEST);
    return(FALSE);
}
#endif



// ======

#if (INCLUDE_MATH64)

static BOOLEAN do_comprehensive_filio_fill(void);
static BOOLEAN do_comprehensive_filio_read(void);
static BOOLEAN do_comprehensive_filio_seek(void);


#define EXFATLARGFILEBUFFERSIZE  32768

#define EXFATLARGFILESIZEGIGSEXFAT    8
#define EXFATLARGFILESIZEGIGSFAT    1

#define EXFATLARGFILESIZEBYTES   (32768*10)
static BOOLEAN check_comprehensive_filio_space(void);

static BOOLEAN do_comprehensive_filio_test(void)
{
	if (!check_comprehensive_filio_space())
	{
  		RTFS_PRINT_STRING_1((byte *)"No space to run comprehensive file io",PRFLG_NL);
		return (TRUE);
	}
  	RTFS_PRINT_STRING_1((byte *)"Filling large file",PRFLG_NL);
	if (!do_comprehensive_filio_fill())
		return FALSE;
   	RTFS_PRINT_STRING_1((byte *)"Reading large file",PRFLG_NL);
	if (!do_comprehensive_filio_read())
		return FALSE;
   	RTFS_PRINT_STRING_1((byte *)"Seeking large file",PRFLG_NL);
    if (!do_comprehensive_filio_seek())
		return FALSE;

    pc_unlink(test_file_name);
	return (TRUE);
}

static BOOLEAN check_comprehensive_filio_space(void)
{
	dword gigabytes,bytes,blocks_total,blocks_free,blocks32;
	ddword bytes64;
	if (check_if_exfat())
		gigabytes = EXFATLARGFILESIZEGIGSEXFAT;
	else
		gigabytes = EXFATLARGFILESIZEGIGSFAT;
	bytes =  	EXFATLARGFILESIZEBYTES;
	bytes64 = M64SET32(0,gigabytes);
	bytes64 = M64LSHIFT(bytes64,30);
	bytes64 = M64PLUS32(bytes64,bytes);
	bytes64 = M64RSHIFT(bytes64,9);
	blocks32 = M64LOWDW(bytes64);

	pc_blocks_free(test_drive, &blocks_total, &blocks_free);

	if (blocks_free >  blocks32) /* enough space to do long test ?? */
    	return(TRUE);
	else
    	return(FALSE);
}

static BOOLEAN do_comprehensive_filio_fill(void)
{
    int out_fd = -1;
    {
/*         RTFS_PRINT_STRING_1((byte *)"Usage: FILLHUGEFILE Filename DOMETADATAONLY buffersizekbytes  GIGABYTES BYTES", PRFLG_NL); */
		int buffersizebytes,allocedsizebytes,currentwritepointer;
		dword gigabytes,bytes;
		byte *allocedbuffer,*writebuffer;
		ddword bytes64,byteswritten64,fillvalue,nextfillvalue,buffersizebytesddw,bytesleftddw;
		BOOLEAN refreshbuffer;

		buffersizebytes = EXFATLARGFILEBUFFERSIZE;
		if (check_if_exfat())
			gigabytes = EXFATLARGFILESIZEGIGSEXFAT;
		else
			gigabytes = EXFATLARGFILESIZEGIGSFAT;
		bytes =  	EXFATLARGFILESIZEBYTES;
		buffersizebytesddw = M64SET32(0,buffersizebytes);
		bytes64 = M64SET32(0,gigabytes);
		bytes64 = M64LSHIFT(bytes64,30);
		bytes64 = M64PLUS32(bytes64,bytes);
		if (buffersizebytes >= 1024)
			allocedsizebytes = buffersizebytes;
		else
			allocedsizebytes = 1024;
		allocedbuffer = (byte *)rtfs_port_malloc(allocedsizebytes);
		if (!allocedbuffer)
			return FALSE;
		writebuffer = allocedbuffer;
        out_fd = po_open(test_file_name,(word) (PO_TRUNC|PO_BINARY|PO_WRONLY|PO_CREAT),(word) (PS_IWRITE | PS_IREAD) );

        if (out_fd < 0)
        {
        	RTFS_PRINT_STRING_1((byte *)"exFat large file test file open failed",PRFLG_NL);
			regress_error(RGE_LARGEFILETEST);
            return(FALSE);
        }
		byteswritten64 = M64SET32(0,0);
		fillvalue = M64SET32(0,0);
		nextfillvalue = M64PLUS32(fillvalue,(allocedsizebytes/8));
		refreshbuffer = TRUE;
		currentwritepointer = 0;

		bytesleftddw = bytes64;
        while (M64GT(bytes64,byteswritten64))
        {
			if (writebuffer)
			{ /* Fill here */
				if (refreshbuffer)
				{
					ddword *pdw = (ddword *)writebuffer;
					while(M64GT(nextfillvalue,fillvalue))
					{
						*pdw++ = fillvalue;
						fillvalue = M64PLUS32(fillvalue, 1);
					}
					nextfillvalue = M64PLUS32(nextfillvalue,(allocedsizebytes/8));
					refreshbuffer = FALSE;
					currentwritepointer = 0;
				}
				if (M64GT(buffersizebytesddw, bytesleftddw))
				{
					buffersizebytesddw = bytesleftddw;
					buffersizebytes = M64LOWDW(buffersizebytesddw);
				}

				if (buffersizebytes >= allocedsizebytes)
				{
					if (po_write(out_fd,(byte*)writebuffer,buffersizebytes) != (int)buffersizebytes)
					{
						RTFS_PRINT_STRING_1((byte *)"Write failure", PRFLG_NL); /* "Write failure" */
						goto return_error;
					}
					refreshbuffer = TRUE;
				}
				else
				{
					if (po_write(out_fd,(byte*)writebuffer+currentwritepointer,buffersizebytes) != (int)buffersizebytes)
					{
						RTFS_PRINT_STRING_1((byte *)"Write failure", PRFLG_NL); /* "Write failure" */
						goto return_error;
					}
					currentwritepointer += buffersizebytes;
					if (currentwritepointer >= allocedsizebytes)
						refreshbuffer = TRUE;
				}
            }
			else
			{
				if (po_write(out_fd,0,buffersizebytes) != (int)buffersizebytes)
				{
					RTFS_PRINT_STRING_1((byte *)"Write failure", PRFLG_NL); /* "Write failure" */
					goto return_error;
				}
			}
			byteswritten64 = M64PLUS32(byteswritten64,buffersizebytes);
			bytesleftddw =   M64MINUS32(bytesleftddw,buffersizebytes);

        }
		if (allocedbuffer)
			rtfs_port_free(allocedbuffer);
		RTFS_PRINT_STRING_1((byte *)"File fill succeeded", PRFLG_NL); /* "Write failure" */
        po_close(out_fd);
        return(TRUE);
return_error:
		RTFS_PRINT_STRING_1((byte *)"File fill failed", PRFLG_NL); /* "Write failure" */
		regress_error(RGE_LARGEFILETEST);
        po_close(out_fd);
		if (allocedbuffer)
			rtfs_port_free(allocedbuffer);
        return(FALSE);
    }
}
static BOOLEAN do_comprehensive_filio_read(void)
{
    int in_fd = -1;
		int docompare,buffersizebytes,allocedsizebytes,currentreadpointer;
		byte *allocedbuffer,*readbuffer;
		ddword bytes64,bytesread64,testvalue,nexttestvalue,bytesleftddw,buffersizebytesddw;
		BOOLEAN comparebuffer;

		docompare = 1;
		buffersizebytes = EXFATLARGFILEBUFFERSIZE;
		buffersizebytesddw = M64SET32(0,buffersizebytes);

		if (buffersizebytes >= 1024)
			allocedsizebytes = buffersizebytes;
		else
			allocedsizebytes = 1024;
		allocedbuffer = (byte *)rtfs_port_malloc(allocedsizebytes);
		if (!allocedbuffer)
			return FALSE;
		readbuffer = allocedbuffer;

        in_fd = po_open(test_file_name, (word) (PO_BINARY|PO_RDONLY),0);

        if (in_fd < 0)
        {
            RTFS_PRINT_STRING_2((byte *)"Cant open file", test_file_name, PRFLG_NL); /* "Cant open file" */
            return(FALSE);
        }

		bytes64 = M64SET32(0,0);
		{
		ERTFS_STAT st;
			if (pc_fstat(in_fd, &st) != 0)
			{
				RTFS_PRINT_STRING_2((byte *)"Cant fstat file", test_file_name, PRFLG_NL); /* "Cant open file" */
				return(FALSE);
			}
			bytes64 = M64SET32(st.st_size_hi, st.st_size);
		}

		bytesread64 = M64SET32(0,0);
		testvalue = M64SET32(0,0);
		nexttestvalue = M64PLUS32(testvalue,(allocedsizebytes/8));
		comparebuffer = FALSE;
		currentreadpointer = 0;

		bytesleftddw = bytes64;
        while (M64GT(bytes64,bytesread64))
        {
			if (readbuffer)
			{ /* test here */
				if (comparebuffer)
				{
					ddword *pdw = (ddword *)readbuffer;


					while(docompare && M64GT(nexttestvalue,testvalue))
					{
						if(!M64EQ(*pdw, testvalue))
						{
							RTFS_PRINT_STRING_1((byte *)"Compare failure", PRFLG_NL); /* "Write failure" */
							goto return_error;
						}
						pdw++;
						testvalue = M64PLUS32(testvalue, 1);
					}
					nexttestvalue = M64PLUS32(nexttestvalue,(allocedsizebytes/8));
					comparebuffer = FALSE;
					currentreadpointer = 0;
				}
				if (M64GT(buffersizebytesddw, bytesleftddw))
				{
					buffersizebytesddw = bytesleftddw;
					buffersizebytes = M64LOWDW(buffersizebytesddw);
				}

				if (buffersizebytes >= allocedsizebytes)
				{
					if (po_read(in_fd,(byte*)readbuffer,buffersizebytes) != (int)buffersizebytes)
					{
						RTFS_PRINT_STRING_1((byte *)"Read failure", PRFLG_NL); /* "Write failure" */
						goto return_error;
					}
					comparebuffer = TRUE;
				}
				else
				{
					if (po_read(in_fd,(byte*)readbuffer+currentreadpointer,buffersizebytes) != (int)buffersizebytes)
					{
						RTFS_PRINT_STRING_1((byte *)"Read failure", PRFLG_NL); /* "Write failure" */
						goto return_error;
					}
					currentreadpointer += buffersizebytes;
					if (currentreadpointer >= allocedsizebytes)
					{
						comparebuffer = TRUE;
					}
				}
            }
			else
			{
					if (po_read(in_fd,0,buffersizebytes) != (int)buffersizebytes)
					{
						RTFS_PRINT_STRING_1((byte *)"Read failure", PRFLG_NL); /* "Write failure" */
						goto return_error;
					}
			}
			bytesread64 = M64PLUS32(bytesread64,buffersizebytes);
			bytesleftddw =   M64MINUS32(bytesleftddw,buffersizebytes);
        }
		if (allocedbuffer)
			rtfs_port_free(allocedbuffer);
		RTFS_PRINT_STRING_1((byte *)"File read succeeded", PRFLG_NL); /* "Write failure" */
        po_close(in_fd);
        return(TRUE);
return_error:
		RTFS_PRINT_STRING_1((byte *)"File read failed", PRFLG_NL); /* "Write failure" */
        po_close(in_fd);
		if (allocedbuffer)
			rtfs_port_free(allocedbuffer);
        return(FALSE);
}

#include <stdlib.h>

static dword dwrand(void)
{
dword x,y,a;
         x = (dword) rand();
         y = (dword) rand();
         x <<= 16;
         a = ((x+y)+3) & ~3L;   /* round up to four byte boundary */
         return(a);
}


static BOOLEAN do_exfat_one_large_file_seek(int seektype,ddword targetfileoffet,ddword bytes64,dword bytestoend)
{
byte readbyseekset[8];
byte readbyseektest[8];
dword bytestoread = 8;
int in_fd;

	if (bytestoend < 8)
		bytestoread = 1;
    in_fd = -1;
    {
/*         RTFS_PRINT_STRING_1((byte *)"Usage: FILLHUGEFILE Filename DOMETADATAONLY buffersizekbytes  GIGABYTES BYTES", PRFLG_NL); */
		ddword bytes64minus1;
		ddword testorigin,testoffset,ddw;

		bytes64minus1 = M64MINUS32(bytes64,1);
		/* Divide file size by eight to get size in octets. largest file size is 4 gig octets == 32 gig */

//  		RTFS_PRINT_STRING_1((byte *)"Seek to: ",0);
//   		rtfs_print_long_1(M64HIGHDW(targetfileoffet), 0);
//  		RTFS_PRINT_STRING_1((byte *)",",0);
//        rtfs_print_long_1(M64LOWDW(targetfileoffet), PRFLG_CR);

    	testoffset  = testorigin =	M64SET32(0,0);

		/* Calculate start point and seek offset */
		switch (seektype) {
		    case PSEEK_SET:
		    	testorigin =	M64SET32(0,0);		/* Seek to the offset from zero */
		    	testoffset = 	targetfileoffet;
			break;
		    case PSEEK_END:							/* Seek (filelen)-offset from end */
		    	testorigin =	M64SET32(0,0);
		    	testoffset =	M64MINUS(bytes64,targetfileoffet);
			break;
		    case PSEEK_CUR_NEG:						/* Seek start at target + ((filelen-1)-offset)/2  and seek backward ((filelen-1)-offset)/2*/
		    	testoffset = 	M64MINUS(bytes64minus1,targetfileoffet);
		    	testoffset = 	M64RSHIFT(testoffset,1);
		    	testorigin =	M64PLUS(targetfileoffet,testoffset);
			break;
		    case PSEEK_CUR:						   /* Seek start at offset - offset/2  and seek foreward offset/2 */
		    	testoffset = 	targetfileoffet;
		    	testoffset = 	M64RSHIFT(testoffset,1);
		    	testorigin =	M64MINUS(targetfileoffet,testoffset);
			break;
		}
		/* Use seek set to read 1 or 8 bytes at the actual origin */
        in_fd = po_open(test_file_name, (word) (PO_BINARY|PO_RDWR),0);
        if (in_fd < 0)
        {
       		RTFS_PRINT_STRING_1((byte *)" ", PRFLG_NL);
        	RTFS_PRINT_STRING_1((byte *)"exFat large file seek test file open failed",PRFLG_NL);
return_error:
        	RTFS_PRINT_STRING_1((byte *)"error ",PRFLG_NL);
			regress_error(RGE_LARGEFILETEST);
			po_close(in_fd);
            return(FALSE);
        }
		ddw = po_lseek64(in_fd, targetfileoffet, PSEEK_SET);
		if (!(M64EQ(ddw,targetfileoffet)))
		{
       		RTFS_PRINT_STRING_1((byte *)" ", PRFLG_NL);
       		RTFS_PRINT_STRING_1((byte *)"exFat large file seek PSEEK_SET failed hi: ",0);
        	rtfs_print_long_1(M64HIGHDW(testorigin), 0);
        	RTFS_PRINT_STRING_1((byte *)" , lo: ",0);
        	rtfs_print_long_1(M64LOWDW(testorigin), PRFLG_NL);
			goto return_error;
		}
		if (po_read(in_fd, readbyseekset, bytestoread) != (int)bytestoread)
		{
       		RTFS_PRINT_STRING_1((byte *)" ", PRFLG_NL);
        	RTFS_PRINT_STRING_1((byte *)"exFat large file seek test file read failed",PRFLG_NL);
        	goto return_error;
		}
		po_close(in_fd);

		/* Now seek using the method under test and read into another buffer */
        in_fd = po_open(test_file_name, (word) (PO_BINARY|PO_RDWR),0);
        if (in_fd < 0)
        {
       		RTFS_PRINT_STRING_1((byte *)" ", PRFLG_NL);
        	RTFS_PRINT_STRING_1((byte *)"exFat large file seek test file open failed",PRFLG_NL);
			goto return_error;
        }
    	if (M64NOTZERO(testorigin))
		{
			ddw = po_lseek64(in_fd, testorigin, PSEEK_SET);
			if (!(M64EQ(ddw,testorigin)))
			{
        		RTFS_PRINT_STRING_1((byte *)" ", PRFLG_NL);
        		RTFS_PRINT_STRING_1((byte *)"exFat large file seek PSEEK_SET failed hi: ",0);
        		rtfs_print_long_1(M64HIGHDW(testorigin), 0);
        		RTFS_PRINT_STRING_1((byte *)" , lo: ",0);
        		rtfs_print_long_1(M64LOWDW(testorigin), PRFLG_NL);
				goto return_error;
			}
		}
		/* Now test the seektype method for this offset and compare to seek set */
		ddw = po_lseek64(in_fd, testoffset, seektype);
		if (!M64EQ(ddw,targetfileoffet))
		{
       		RTFS_PRINT_STRING_1((byte *)" ", PRFLG_NL);
       		RTFS_PRINT_STRING_1((byte *)"Seek Test ==  ",0);
       		rtfs_print_long_1(seektype, PRFLG_NL);
       		RTFS_PRINT_STRING_1((byte *)"exFat large file seek test failed failed hi: ",0);
       		rtfs_print_long_1(M64HIGHDW(testorigin), 0);
       		RTFS_PRINT_STRING_1((byte *)" , lo: ",0);
       		rtfs_print_long_1(M64LOWDW(testorigin), PRFLG_NL);
			goto return_error;
		}
		if (po_read(in_fd, readbyseektest, bytestoread) != (int)bytestoread)
		{
       		RTFS_PRINT_STRING_1((byte *)" ", PRFLG_NL);
       		RTFS_PRINT_STRING_1((byte *)"exFat large file seek test file read failed",PRFLG_NL);
			goto return_error;
		}

		/* Be sure that the first byte of the two file read values are the same */
		if (readbyseektest[0] != readbyseekset[0])
		{
       		RTFS_PRINT_STRING_1((byte *)" ", PRFLG_NL);
       		RTFS_PRINT_STRING_1((byte *)"exFat large file seek test file read compare failed",PRFLG_NL);
			goto return_error;
		}
		/* If on an octect boundary the value of the octet should be the offset divided by 3 */
		if (bytestoread == 8 && M64LOWDW(targetfileoffet) % 8 == 0)
		{
		ddword ldw,targval;
			targval = targetfileoffet;
			targval = M64RSHIFT(targval, 3);
			copybuff(&ldw, readbyseektest, 8);
			if (!M64EQ(ldw,targval))
			{
	       		RTFS_PRINT_STRING_1((byte *)" ", PRFLG_NL);
       			RTFS_PRINT_STRING_1((byte *)"exFat large file content val check failed",PRFLG_NL);
       			goto return_error;
			}
		}
		po_close(in_fd);
	}
	return (TRUE);
}

#define SEEKTESTITERATIONS 100  // 00

static BOOLEAN do_comprehensive_filio_seek(void)
{
	ddword bytes64;
	dword octets;
	int in_fd, test_count;

    in_fd = po_open(test_file_name, (word) (PO_BINARY|PO_RDONLY),0);

    if (in_fd < 0)
    {
        RTFS_PRINT_STRING_2((byte *)"Cant open file", test_file_name, PRFLG_NL); /* "Cant open file" */
        return(FALSE);
    }

	bytes64 = M64SET32(0,0);
	{
	ERTFS_STAT st;
		if (pc_fstat(in_fd, &st) != 0)
		{
			RTFS_PRINT_STRING_2((byte *)"Cant fstat file", test_file_name, PRFLG_NL); /* "Cant open file" */
			return(FALSE);
		}
		bytes64 = M64SET32(st.st_size_hi, st.st_size);
	}
    po_close(in_fd);
	/* Divide file size by eight to get size in octets. largest file size is 4 gig octets == 32 gig */
	octets = M64LOWDW(M64RSHIFT(bytes64,3));

	for (test_count=0;test_count < SEEKTESTITERATIONS;test_count++)
	{
		dword targetoctet,byteoffset;
		ddword targetfileoffet;
		/* Seek target in octets */
		targetoctet = dwrand()%octets;
		targetfileoffet = M64SET32(0,targetoctet);
		targetfileoffet = M64LSHIFT(targetfileoffet,3);


		for (byteoffset = 0; byteoffset < 8; byteoffset++)
		{
		ddword ltargetfileoffet, bytestoend64;
		dword bytestoend;

			ltargetfileoffet = M64PLUS32(targetfileoffet,byteoffset);

			if (M64GTEQ(ltargetfileoffet,bytes64))	/* Check if we walked past the end */
				break;

#define VERBOSE_SEEK_TEST 1
#if (VERBOSE_SEEK_TEST)
			RTFS_PRINT_STRING_1((byte *)" ", PRFLG_CR);
			RTFS_PRINT_STRING_1((byte *)"                                                                              ", PRFLG_CR);
			RTFS_PRINT_STRING_1((byte *)" Seek test: ", 0);
			RTFS_PRINT_LONG_1((dword) test_count, 0);
			RTFS_PRINT_STRING_1((byte *)" - of - ", 0);
			RTFS_PRINT_LONG_1((dword) SEEKTESTITERATIONS, 0);
   			RTFS_PRINT_STRING_1((byte *)"  target: ",0);
   			rtfs_print_long_1(M64HIGHDW(targetfileoffet), 0);
   			RTFS_PRINT_STRING_1((byte *)",",0);
   			rtfs_print_long_1(M64LOWDW(targetfileoffet), 0);
#endif


			bytestoend64 = M64MINUS(bytes64,ltargetfileoffet);
			if (M64HIGHDW(bytestoend64))
				bytestoend = 0xffffffff;
			else
				bytestoend = M64LOWDW(bytestoend64);

			if (!do_exfat_one_large_file_seek(PSEEK_SET, ltargetfileoffet, bytes64,bytestoend))
			{
       			RTFS_PRINT_STRING_1((byte *)"exFat seek PSEEK_SET test failed",PRFLG_NL);
       			return(FALSE);
			}
			if (!do_exfat_one_large_file_seek(PSEEK_END, ltargetfileoffet, bytes64,bytestoend))
			{
       			RTFS_PRINT_STRING_1((byte *)"exFat seek PSEEK_END test failed",PRFLG_NL);
       			return(FALSE);
			}
			if (!do_exfat_one_large_file_seek(PSEEK_CUR, ltargetfileoffet, bytes64,bytestoend))
			{
       			RTFS_PRINT_STRING_1((byte *)"exFat seek PSEEK_CUR test failed",PRFLG_NL);
       			return(FALSE);
			}
			if (!do_exfat_one_large_file_seek(PSEEK_CUR_NEG, ltargetfileoffet, bytes64,bytestoend))
			{
       			RTFS_PRINT_STRING_1((byte *)"exFat seek PSEEK_CUR_NEG test failed",PRFLG_NL);
				return(FALSE);
			}
		}
	}
	RTFS_PRINT_STRING_1((byte *)"exFat seek test succeeded",PRFLG_NL);
	return(TRUE);
}

#endif /*if (INCLUDE_MATH64) else.. */




#endif /* Exclude from build if read only */
