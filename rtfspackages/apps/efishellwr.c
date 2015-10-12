/*
<TEST>  Test File:   rtfspackages/apps/efishellwr.c
<TEST>
<TEST>   Procedure: efio_shell()
<TEST>   Description: Interactive command commands that call Rtfs ProPlus extended API subroutines.
<TEST>
*/
/* efishell.c -
*/

#include "rtfs.h"
#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */
#include "protests.h"
#include "rtfs.h"


/* Set USE_TARGET_THREADS to 1 if you wish to use background mode
   asynchronous completion of asynchronous operations from the
   shell.
   Thread support for the Microsoft windows and linux are provided
   For other environments modify the routines in this file:
        ContinueProc()
        spawn_async_continue()
*/
void show_status(char *prompt, dword val, int flags);

#define EFISHELL_USAGE(X) rtfs_print_one_string((byte *)X,PRFLG_NL)
#define EFISHELL_WARN(X) rtfs_print_one_string((byte *)X,PRFLG_NL)
#define EFISHELL_SHOW(X) rtfs_print_one_string((byte *)X,0)
#define EFISHELL_SHOWNL(X) rtfs_print_one_string((byte *)X,PRFLG_NL)
#define EFISHELL_SHOWINT(PROMPT, VAL) show_status(PROMPT, VAL, 0)
dword parse_extended_option(byte *input);
int register_open_file(int fd,byte *name, EFILEOPTIONS *poptions);

#define _rtfs_cs_strcmp(A,B,C) rtfs_cs_strcmp((byte *) (A) , (byte *) (B), C)
#define _rtfs_cs_strcpy(A,B,C) rtfs_cs_strcpy((byte *) (A) , (byte *) (B), C)

#define PRECISION_64 12 /* Number of places to show */


/* Structure to simplify keeping track of open files */
typedef struct openfile {
    int fd;
    byte filename[255];
    EFILEOPTIONS options;
    byte *transaction_buffer;
} OPENFILE;
extern OPENFILE open_files[]; /* Commands access this table by index */

extern int do_async;
BOOLEAN _do_async_complete(int driveno, BOOLEAN manual);
int efio_shell_restore_default_config(void);

/* See apputil.c */
int parse_args(int agc, byte **agv, char *input_template);
int rtfs_args_arg_count(void);
dword rtfs_args_val_hi(int this_arg);
dword rtfs_args_val_lo(int this_arg);
byte *rtfs_args_val_text(int this_arg);
void use_args(int agc, byte **agv);

/*
<TEST>   Procedure:  do_efile_setallocation_hint()
<TEST>   Description: Calls pc_efilio_setalloc() to assign specific clusters to an open file
<TEST>   Invoke by typing "sethint" in the extended command shell
*/

int do_efile_setallocation_hint(int agc, byte **agv)
{
    int index;
    dword cluster, n_clusters;

     /* fileindex allocation hint */
     if (!parse_args(agc, agv,"III"))
     {
         EFISHELL_USAGE("usage: sethint fileindex clusternumber nclusters");
         EFISHELL_USAGE("usage: clusternumber is the next cluster to allocate");
         EFISHELL_USAGE("usage: if nclusters > 0, allocate this many clusters now");
         EFISHELL_USAGE("usage: otherwise advise RTFS to allocate from clusternumber");
         EFISHELL_USAGE("usage: when the file is extended");
         return(-1);
    }

    index = rtfs_args_val_lo(0);
    cluster = rtfs_args_val_lo(1);
    if (agc == 3)
    {
        n_clusters = rtfs_args_val_lo(2);
    }
    else
    {
        n_clusters = 0;
    }

    if (!pc_efilio_setalloc(open_files[index].fd, cluster,n_clusters))
        EFISHELL_WARN("    Could not set allocation hint ");
    return(0);
}

/* Write sequential blocks to a file.

write fileindex resetfp xferdata totalsize writesize

     Write bytes to a file.

        fileindex - The file index printed by the "open" or "copen" command
        resetfp   - 'y' or 'Y' to seek to the beginning of the file before
                    writing.
                    'n' or 'N' to append from the current file pointer
        xferdata  - 'y' or 'Y' to transfer data to the file (the contents of
                    the data is just random data returned from a malloc().
                    'n' or 'N' to pass a NULL pointer to the write call. When
                    a NULL pointer is passed the routine behaves exactly the
                    same except that data is not written to the data blocks
                    of the file.
       totalsize  - Total number of bytes to write to the file

                    The argument is in (GB,KB,B) format, see below for an
                    explanation

       writesize  - Number of bytes to write per write call. The write
                    command will call pc_efilio_write or pc_cfilio_write
                    as many times as needed until "totalsize" bytes are
                    written.

                    The argument is in (GB,KB,B) format, see below for an
                    explanation

                    Note: if xferdata is Y or y then the write command
                    call malloc() to allocate a buffer "writesize" bytes
                    long to use for data (the buffer is freed when the
                    command finishes). So very large writesize values will
                    result in very large malloc()s.

                    If xferdata is N or n then no buffer is allocated so
                    gigabyte values and more are acceptable.


*/
dword _do_efile_fd_write(int fd, dword options,byte *fill_buffer,
                        dword file_size_hi, dword file_size_lo,
                        dword buffer_size,dword *pn_write_calls);


/*
<TEST>   Procedure:  do_efile_writefile()
<TEST>   Description: Write to a file
<TEST>   Invoke by typing "write" in the extended command shell with the following options
<TEST>        fileindex - The file index printed by the "open" or "copen" command
<TEST>        resetfp   - 'y' or 'Y' to seek to the beginning of the file before
<TEST>        xferdata  - 'y' or 'Y' to transfer from an uninitialized data buffer or 'n' to skip data transfer portion
<TEST>        totalsize  - Total number of bytes to read from the file, can be a 64 bit value
<TEST>        writesize  -  Number of bytes to write per write call.
*/

int do_efile_writefile(int agc, byte **agv)
{
    int index,do_xfer,do_reset;
    int this_arg=0;
    byte *fill_buffer;
    dword ret_hi, ret_lo, n_write_calls,
    file_size_hi, file_size_lo, buffer_size;

     /* fileindex:resetfp:xferdata:totalsize:writesize */
     if (!parse_args(agc, agv,"IBBII"))
     {
         EFISHELL_USAGE("usage: write fileindex resetfp(Y/N) xferdata(Y/N) totalsize(GB,KB,B) writesize(GB,KB,B)");
         return(-1);
    }
    index = rtfs_args_val_lo(this_arg++);/* "Select file to fill" */
    do_reset = rtfs_args_val_lo(this_arg++); /* "Reset file pointer ? " */
    do_xfer = rtfs_args_val_lo(this_arg++); /* "Perform data transfers ?" */
    file_size_hi  = rtfs_args_val_hi(this_arg); /* total bytes to write */
    file_size_lo  = rtfs_args_val_lo(this_arg++);
    buffer_size     = rtfs_args_val_lo(this_arg++); /* bytes per write */

    if (do_reset)
    {
#if (INCLUDE_CIRCULAR_FILES)
       if (open_files[index].options.allocation_policy & PCE_CIRCULAR_BUFFER)
            pc_cfilio_lseek(open_files[index].fd, CFWRITE_POINTER,
                            0, 0, PSEEK_SET, &ret_hi, &ret_lo);
       else
#endif
            pc_efilio_lseek(open_files[index].fd, 0, 0, PSEEK_SET,
                            &ret_hi, &ret_lo);
    }

    if (do_xfer)
    {
        fill_buffer = (byte *)pro_test_malloc(buffer_size);
        if (!fill_buffer)
        {
            EFISHELL_WARN("    Could not allocate buffer ");
            return(-1);
        }
    }
    else
        fill_buffer = 0;


    if ( _do_efile_fd_write(open_files[index].fd,
                         open_files[index].options.allocation_policy,
                         (byte*)fill_buffer,
                         file_size_hi, file_size_lo, buffer_size,
                         &n_write_calls))
    if (fill_buffer)
        pro_test_free(fill_buffer);
    return(0);
}

dword _do_efile_fd_write(int fd, dword options, byte *fill_buffer,
                        dword file_size_hi, dword file_size_lo,
                        dword buffer_size,dword *pn_write_calls)
{
    dword nwritten,pattern_value;
    ddword nwritten_ddw,n_left_ddw,file_size_ddw,buffer_size_ddw;
	pattern_value=0;
    *pn_write_calls = 0;
    buffer_size_ddw = M64SET32(0, buffer_size);
    nwritten_ddw = M64SET32(0, 0);
    file_size_ddw = M64SET32(file_size_hi, file_size_lo);
    n_left_ddw = file_size_ddw;
    while(!M64ISZERO(n_left_ddw))
    {
        RTFS_ARGSUSED_DWORD(options);
		if (fill_buffer)
		{
		dword i;
		dword *p=(dword *) fill_buffer;
			for (i=0;i<buffer_size/4;i++)
				*p++=pattern_value++;
		}
#if (INCLUDE_CIRCULAR_FILES)
        if (options & PCE_CIRCULAR_BUFFER)
        {
            if (!pc_cfilio_write(fd, (byte*)fill_buffer,
                buffer_size, &nwritten))
            {
                EFISHELL_WARN("    pc_cfilio_write failed");
                return(0);
            }
        }
        else
#endif
        {
            if (!pc_efilio_write(fd, (byte*)fill_buffer,
                buffer_size, &nwritten))
            {
                EFISHELL_WARN("    pc_efilio_write failed");
                return(0);
            }
        }
        nwritten_ddw = M64PLUS32(nwritten_ddw, nwritten);
        *pn_write_calls += 1;
        if (M64GT(n_left_ddw,buffer_size_ddw))
            n_left_ddw = M64MINUS(n_left_ddw,buffer_size_ddw);
        else
            n_left_ddw = M64SET32(0,0);
        if (buffer_size != nwritten)
            break;
    }
    return(1);
}

/* Change a file size

    chsize fileindex newsize

     Expand or truncate a file

*/
/*
<TEST>   Procedure:  do_efile_chsize()
<TEST>   Description: Calls pc_efilio_chsize() to shrink or grow a 32 or 64 bit file
<TEST>   Invoke by typing "chsize" in the extended command shell
*/
int do_efile_chsize(int agc, byte **agv)
{
    int this_arg,index;
    dword file_size_hi, file_size_lo;

    this_arg = 0;
     /* fileindex:newsize */
     if (!parse_args(agc, agv,"II"))
     {
         EFISHELL_USAGE("usage: chsize fileindex newsize(GB,KB,B)");
         return(-1);
    }
    index = rtfs_args_val_lo(this_arg++);
    file_size_hi  = rtfs_args_val_hi(this_arg);
    file_size_lo  = rtfs_args_val_lo(this_arg++);

    if (!pc_efilio_chsize(open_files[index].fd, file_size_hi, file_size_lo))
    {
        EFISHELL_WARN("pc_efilio_chsize failed");
        return(-1);
     }
     return(0);
}


word pc_encode_date( word  year,     /* relative to 1980 */
                     word  month,    /* 1 - 12 */
                     word  day)      /* 1 - 31 */
{
word new_date;
    new_date = (word) ( (year << 9) | (month << 5) | day);
    return(new_date);
}

word pc_encode_time( word  hour,
                     word  minute,
                     word  second)      /* Note: seconds are 2 per. ie 3 == 6 seconds */
{
word new_time;
    new_time = (word) ( (hour << 11) | (minute << 5) | second);
    return(new_time);
}
/*
<TEST>   Procedure:  do_efile_settime()
<TEST>   Description: Calls pc_get_dirent_info() pc_set_dirent_info() to change the time field of a directory entry
<TEST>   Invoke by typing "settime" in the extended command shell
*/

int do_efile_settime(int agc, byte **agv)
{
    DIRENT_INFO dinfo;

     if (!parse_args(agc, agv,"T"))
    {
        EFISHELL_USAGE("usage: settime filename");
        return(-1);
    }
    if (!pc_get_dirent_info(rtfs_args_val_text(0), &dinfo))
    {
        EFISHELL_WARN("    Cannot access entry");
        return(-1);
    }

    EFISHELL_WARN("    Setting date to: April, 1, 2007. 11:32:42 AM \n");
    dinfo.ftime = pc_encode_time( (word) 11, (word) 32, (word) 21); /* 11:32:42 AM */
    dinfo.fdate = pc_encode_date( (word) 27, (word) 4, (word) 1);   /* 2007, April, 1 */
    if (!pc_set_dirent_info(rtfs_args_val_text(0), &dinfo))
    {
        EFISHELL_WARN("    Cannot change entry");
        return(-1);
    }
    return(0);
}

/*
<TEST>   Procedure:  do_efile_setsize()
<TEST>   Description: Calls pc_get_dirent_info() pc_set_dirent_info() to change the size field of a directory entry
<TEST>   Invoke by typing "setsize" in the extended command shell
*/

int do_efile_setsize(int agc, byte **agv)
{
    DIRENT_INFO dinfo;

    if (!parse_args(agc, agv,"TI"))
    {
        EFISHELL_USAGE("usage: setsize filename newsize");
        return(-1);
    }
    if (!pc_get_dirent_info(rtfs_args_val_text(0), &dinfo))
    {
        EFISHELL_WARN("    Cannot access entry");
        return(-1);
    }

    EFISHELL_WARN("    Setting size will cause size mismatch\n");
	dinfo.fsize=rtfs_args_val_lo(1);
#if (INCLUDE_EXFATORFAT64)
	dinfo.fsize64=((ddword)rtfs_args_val_hi(1))<<32|rtfs_args_val_lo(1);
#endif

    if (!pc_set_dirent_info(rtfs_args_val_text(0), &dinfo))
    {
        EFISHELL_WARN("    Cannot change entry");
        return(-1);
    }
    return(0);
}

/*
<TEST>   Procedure:  do_efile_setcluster()
<TEST>   Description: Calls pc_get_dirent_info() pc_set_dirent_info() to change the cluster field of a directory entry
<TEST>   Invoke by typing "setcluster" in the extended command shell
*/

int do_efile_setcluster(int agc, byte **agv)
{
    DIRENT_INFO dinfo;

    if (!parse_args(agc, agv,"TI"))
    {
        EFISHELL_USAGE("usage: setcluster filename newcluster");
        return(-1);
    }
    if (!pc_get_dirent_info(rtfs_args_val_text(0), &dinfo))
    {
        EFISHELL_WARN("    Cannot access entry");
        return(-1);
    }

    EFISHELL_WARN("    Setting cluster will cause lost or crossed chains\n");
    dinfo.fcluster = rtfs_args_val_lo(1);
    if (!pc_set_dirent_info(rtfs_args_val_text(0), &dinfo))
    {
        EFISHELL_WARN("    Cannot change entry");
        return(-1);
    }
    return(0);
}

/* Flush a file */
int _do_efile_async_flush(int index);

/*
<TEST>   Procedure:  do_efile_flush()
<TEST>   Description: Flush a file
<TEST>   Invoke by typing "flush" in the extended command shell with the following options
<TEST>        fileindex - The file index printed by the "open" or "copen" command
<TEST>   Completes asynchronously if the command shell is in asynchronous completion mode.
*/

int do_efile_flush(int agc, byte **agv)
{
    int index, this_arg;

    this_arg = 0;
    /* flushe fileindex */
    if (!parse_args(agc, agv,"I"))
    {
        EFISHELL_USAGE("usage: flush fileindex");
        return(-1);
    }

    index = rtfs_args_val_lo(this_arg++);
    if (do_async)
        EFISHELL_WARN("forcing async flush ");

    if (open_files[index].options.allocation_policy & PCE_CIRCULAR_BUFFER)
    {
        EFISHELL_WARN("    Cannot Flush circular file");
    }
    else if (!do_async)
    {
        if (!pc_efilio_flush(open_files[index].fd))
        {
            EFISHELL_WARN("    Cannot Flush file");
            return(0);
        }
    }
#if (INCLUDE_ASYNCRONOUS_API)
    else
    {

        if (!_do_efile_async_flush(index))
        {
            EFISHELL_WARN("    Cannot Flush file");
            return(0);
        }
    }
#endif
    return(0);
}
#if (INCLUDE_ASYNCRONOUS_API)
/* Asynchronous version of flush */
int _do_efile_async_flush(int index)
{
    int status;
    /* Call Asynchronous flush start */
    status = pc_efilio_async_flush_start(open_files[index].fd);
    if (status != PC_ASYNC_CONTINUE)
    {
        EFISHELL_WARN("    Cannot Flush file");
        return(0);
    }
    /* Call Asynchronous file operation continue until completed */
    if (!_do_async_complete(pc_fd_to_driveno(open_files[index].fd,0,CS_CHARSET_NOT_UNICODE),FALSE))
    {
        EFISHELL_WARN("    Cannot Flush file");
        return(0);
    }
    return(1);
}
#endif


/* Delete a 62 or 32 bit file

delete filename

        Delete a file from the disk.
        If asynchronous mode is enabled (see async y/n) then loop and
        complete the delete asynchronously.

        filename - The name of the file to delete
*/
/*
<TEST>   Procedure:  do_efile_delete()
<TEST>   Description: Delete a 32 bit or 64 bit file
<TEST>   Invoke by typing "delete" in the extended command shell
<TEST>   Completes asynchronously if the command shell is in asynchronous completion mode.
<TEST>
*/


#if (INCLUDE_ASYNCRONOUS_API)
int do_efile_asydelete(int agc, byte **agv);
#endif
int dorm(int agc, byte **agv);
int do_efile_delete(int agc, byte **agv)
{
#if (INCLUDE_ASYNCRONOUS_API)
    if (do_async)
    {
        return(do_efile_asydelete(agc, agv));
    }
#endif
    if (!parse_args(agc, agv,"T"))
    {
        EFISHELL_USAGE("usage: delete filename");
         return(-1);
    }

    /* call the shells delete */
    pc_unlink(rtfs_args_val_text(0));
    return(0);
}
#if (INCLUDE_ASYNCRONOUS_API)
int do_efile_asydelete(int agc, byte **agv)
{
    int fd,driveno;
    int  this_arg = 0;
    /* delete filename */
    if (!parse_args(agc, agv,"T"))
    {
        EFISHELL_USAGE("usage: delete filename");
         return(-1);
    }

    /* Call async file delete start api */
    fd = pc_efilio_async_unlink_start(rtfs_args_val_text(this_arg++));
    driveno = pc_fd_to_driveno(fd,0,CS_CHARSET_NOT_UNICODE);
    if (fd < 0)
    {
        EFISHELL_WARN("pc_efilio_async_unlink_start() failed ");
        return(-1);
    }
    if (!_do_async_complete(driveno,FALSE))
        EFISHELL_WARN("Cannot Unlink file");
    return(0);
}
#endif

/* Call the extended file regression test, see preftest.c
Command:
    efiletest
*/

/*
<TEST>   Procedure:  do_efile_tests()
<TEST>   Description: Invoke an rtfs Pro Plus regression test
<TEST>   Invoke by typing "test testname driveid" in the extended command shell
<TEST>   Testname may be
<TEST>   Completes asynchronously if the command shell is in asynchronous completion mode.
<TEST>
<TEST>    efile       - Test Extendeded File API
<TEST>    extract     - Test pc_efext_extract, swap and remove
<TEST>    cfile       - Test Circular files
<TEST>    async       - Test Asycnhronous operation and Failsafe
<TEST>    transaction - Test transaction files
<TEST>    failsafe    - Test Failsafe
*/
int do_efile_tests(int agc, byte **agv)
{
    byte *pcommandstring;
    byte *pdriveid;

    /* delete filename */
    if (!parse_args(agc, agv,"TT"))
    {
usage:
       EFISHELL_USAGE("       usage:");
       EFISHELL_USAGE("        test testname DRIVEID:");
       EFISHELL_USAGE("        where testname is:");
       EFISHELL_USAGE("        efile       - Test Extendeded File API");
       EFISHELL_USAGE("        extract     - Test pc_efext_extract, swap and remove");
#if (INCLUDE_CIRCULAR_FILES)
       EFISHELL_USAGE("        cfile       - Test Circular files");
#endif
       EFISHELL_USAGE("        async       - Test Asycnhronous operation and Failsafe");
#if (INCLUDE_FAILSAFE_CODE)
#if (INCLUDE_TRANSACTION_FILES)
       EFISHELL_USAGE("        transaction - Test transaction files ");
#endif
       EFISHELL_USAGE("        failsafe    - Test Failsafe III ");
#endif
       return(0);
    }
    pcommandstring   = rtfs_args_val_text(0);
    if (!pcommandstring)
        goto usage;

    pdriveid = rtfs_args_val_text(1);
    if (!pdriveid)
        goto usage;


    if (_rtfs_cs_strcmp(pcommandstring,"efile", CS_CHARSET_NOT_UNICODE) == 0)
    {
        pc_efilio_test(pdriveid);
		return (0);
    }
    else if (_rtfs_cs_strcmp(pcommandstring,"extract", CS_CHARSET_NOT_UNICODE) == 0)
    {
        test_efilio_extract(pdriveid);
    }
    else if (_rtfs_cs_strcmp(pcommandstring,"async", CS_CHARSET_NOT_UNICODE) == 0)
    {
        test_asynchronous_api(pdriveid);
    }
#if (INCLUDE_CIRCULAR_FILES)
    else if (_rtfs_cs_strcmp(pcommandstring,"cfile", CS_CHARSET_NOT_UNICODE) == 0)
    {
        pc_cfilio_test(pdriveid);
    }
#endif
#if (INCLUDE_FAILSAFE_CODE)
#if (INCLUDE_TRANSACTION_FILES)
    else if (_rtfs_cs_strcmp(pcommandstring,"transaction", CS_CHARSET_NOT_UNICODE) == 0)
    {
        test_efilio_transactions(pdriveid);
    }
#endif
    else if (_rtfs_cs_strcmp(pcommandstring,"failsafe", CS_CHARSET_NOT_UNICODE) == 0)
    {
        pc_fstest_main(pdriveid);
    }
#endif
    else
        goto usage;
    return(0);
}

/* Linear file procedures END */

/* Disk oriented procedures BEGIN */

/*
diskflush DRIVEID:

        Flush a drive's directory and small file FAT caches
        If asynchronous mode is enabled (see async y/n) then loop and
        complete the delete asynchronously.

         DRIVEID: - The drive id A:. B: etc

*/

int do_efile_diskflush(int agc, byte **agv)
{
    /* diskflush DRIVEID: */
     if (!parse_args(agc, agv,"T"))
     {
        EFISHELL_USAGE("usage: diskflush DRIVEID:");
        return(-1);
     }

#if (INCLUDE_ASYNCRONOUS_API)
    if (do_async)
    {
int status,driveno;
        status  = pc_diskio_async_flush_start(rtfs_args_val_text(0));
        RTFS_ARGSUSED_INT(status);
        driveno = pc_path_to_driveno(rtfs_args_val_text(0), CS_CHARSET_NOT_UNICODE);
        if (!_do_async_complete(driveno,FALSE))
        {
            EFISHELL_WARN("async disk flush failed");
        }
    }
    else
#endif
    {
        if (!pc_diskflush(rtfs_args_val_text(0)))
        {
           EFISHELL_WARN("async disk flush failed");
        }
    }
    return(0);
}

#if (INCLUDE_CIRCULAR_FILES)

/* Circular file procedures BEGIN */
/*
copen filename wrappoint preallocsize [option option ..]
    open a circular file. re-open of circular files not supported

    filename      - File name to open
    wrappoint     - Required parameter, the byte offset in the file that
                    when the file pointer reads it read and write and
                     operations seek back to the beginning of the file
                    and contimue from there.

         The argument is in (GB,KB,B) format, see below for an
         explanation


    preallocsize  - Optional parameter, if specified write calls allocate
                    this many clusters when they need to expand the file.
                    helps reduce fragmentation. When the file is closed
                    unused pre-allocated clusters are freed.
                        Do not use, not currently supported

    [options]     - One or more of the following options may be used.

    TEMP_FILE    - Tell ERTFS to delete the file when it is closed, this
                   also has the effect of never actually writing the
                   cluster chains to the FAT so the close happens much
                   faster than a normal close

        Do not use other options

     When copen succeeds it prints an fileindex number that may be used
     as an argument to the other command that operate on open files

*/
/*
<TEST>   Procedure:  do_cfile_open()
<TEST>   Description: open or re-open a circular file using pc_cfilio_open().
<TEST>   All extended options are supported
<TEST>   Invoke by typing "copen" in the extended command shell
*/

int do_cfile_open(int agc, byte **agv)
{
    int fd;
     int index,this_arg;
    byte filename[256];
    EFILEOPTIONS my_options;
    word flags;
    dword cfilesize_hi, cfilesize_lo;
    this_arg = 0;
    rtfs_memset(&my_options, 0, sizeof(my_options));

     if (!parse_args(agc, agv,0))
     {
usage:
        EFISHELL_USAGE("usage: copen filename [wrappoint (bytes)] [option option ..]");
        EFISHELL_USAGE("[options] == ");
        EFISHELL_USAGE("FIRST_FIT FORCE_FIRST CONTIGUOUS KEEP_PREALLOC FILE_64");
        EFISHELL_USAGE("CIRCULAR_FILE CIRCULAR_BUFFER TEMP_FILE BUFFERED");
          return(-1);
     }
     if (rtfs_args_val_text(this_arg))
        _rtfs_cs_strcpy(filename, rtfs_args_val_text(this_arg++), CS_CHARSET_NOT_UNICODE);
     else
          goto usage;
     if (this_arg >= rtfs_args_arg_count())
        goto usage;
    /* Get wrap point (required) */
    if (rtfs_args_val_text(this_arg))
        goto usage;
    cfilesize_hi = rtfs_args_val_hi(this_arg);
    cfilesize_lo = rtfs_args_val_lo(this_arg++);

        /* Get CFIO OPEN options */
    flags = (word)(PO_BINARY|PO_RDWR|PO_CREAT);

    my_options.allocation_policy = 0;
#define VALID_OPTIONS PCE_FORCE_CONTIGUOUS|PCE_TEMP_FILE
     while (this_arg < rtfs_args_arg_count())
     {
     dword option;
         if (!rtfs_args_val_text(this_arg))
            goto usage;
         option = parse_extended_option(rtfs_args_val_text(this_arg++));
         if (option == 0xffffffff)
             goto usage;
         else if (option == 0xfffe0000)
            flags |= PO_BUFFERED;

         if (option == 0xffff0000)
             goto usage; /* Truncate not supported */
        else
        {
            if (!((option) & (VALID_OPTIONS)))
                 goto usage;
            my_options.allocation_policy |= option;
        }
    }

    my_options.min_clusters_per_allocation = 0;
    my_options.circular_file_size_hi = cfilesize_hi;
    my_options.circular_file_size_lo = cfilesize_lo;
    /* Providing 32 remap records */
    my_options.n_remap_records = 32;
    my_options.remap_records = (REMAP_RECORD *) pro_test_malloc(32*sizeof(REMAP_RECORD));
    fd = pc_cfilio_open((byte *)filename,flags,&my_options);
    if (fd < 0)
    {
        EFISHELL_WARN("    file open failed ");
        return(-1);
    }

    index = register_open_file(fd, filename, &my_options);
       /* ("use handle %d to access <%s>\n", index, filename); */
    EFISHELL_SHOWINT("use handle ", index);
    EFISHELL_SHOW(" to access ");
    EFISHELL_SHOWNL(filename);
    return(0);
}


/* Circular file procedures END */

/*
extract circfile linfile offset length

        Perform a no-copy extract of bytes from a circular file to a linear
        file.

        circfile - The file index printed by a sucessful "copen" command

        linfile  - The file index printed by the "open" command, note that
                   the REMAP_FILE option must have been selected.

        offset   - Linear "stream offset" to start from. This is the byte
                   number of the first byte to extract, this value never
                   wraps to zero but increases endlessly as the file is
                   written to.

                    The argument is in (GB,KB,B) format, see below for an
                    explanation

        length   - Number of bytes to extract.

                    The argument is in (GB,KB,B) format, see below for an
                    explanation


*/
/*
<TEST>   Procedure:  do_cfile_extract()
<TEST>   Description: Use pc_cfilio_extract() to extract a portion of a circular file into a linear extract file.
<TEST>   Invoke by typing "extract" in the extended command shell
*/
int do_cfile_extract(int agc, byte **agv)
{
int circ_index, lin_index, this_arg;
dword extract_loc_hi,extract_loc_lo,ret_hi,ret_lo;
dword extract_length_hi,extract_length_lo;

    /* extract circfile linfile offset length */
     if (!parse_args(agc, agv,"III"))
     {
        EFISHELL_USAGE("usage: extract circfile linfile offset length");
          return(-1);
     }
    this_arg = 0;
    circ_index = rtfs_args_val_lo(this_arg++);
    lin_index =  rtfs_args_val_lo(this_arg++);
    extract_loc_hi = rtfs_args_val_hi(this_arg);
    extract_loc_lo = rtfs_args_val_lo(this_arg++);
    extract_length_hi = rtfs_args_val_hi(this_arg);
    extract_length_lo = rtfs_args_val_lo(this_arg++);

    if (!pc_cstreamio_lseek(open_files[circ_index].fd, CFREAD_POINTER,
        extract_loc_hi, extract_loc_lo, PSEEK_SET, &ret_hi, &ret_lo) ||
                (extract_loc_hi != ret_hi || extract_loc_lo != ret_lo) )
    {
        EFISHELL_WARN("Seek failed");
        return(-1);
    }
    if (!pc_cfilio_extract(open_files[circ_index].fd,
                            open_files[lin_index].fd,
                            extract_length_hi,extract_length_lo,0,0))
    {
        EFISHELL_WARN("Extract failed");
        return(-1);
    }
    return(0);
}


#endif /* (INCLUDE_CIRCULAR_FILES) */


/* Circular file procedures END */
#endif /* Exclude from build if read only */
