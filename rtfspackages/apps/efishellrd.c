/*
<TEST>  Test File:   rtfspackages/apps/efishellrd.c
<TEST>
<TEST>   Procedure: efio_shell()
<TEST>   Description: Interactive command shell with commands that call Rtfs ProPlus extended API subroutines.
<TEST>
*/
/* efishell.c -
   Interactive shell program to demonstrate the features of
   ERTFS Pro Plus's 32/64 bit IO package with circular file support

   help            config          clear           async
   remount         open            copen           close
   flush           diskflush       read            write
   seek            fstat           dstat           delete
   shell           dir             setbuff         test
   extract         quit
*/
/*
  This program requires the following library routines:
       pro_test_malloc()
       pro_test_free()
   If you do not have these routines implemented in protest.h, you must
   perform minor editing of the file to work around that fact.

   This program will execute in any environment but it can perform
   additional operations if certain TARGET resources are provided, including
   threads, implemented in this module, and high speed timers implemented in
   portkern.c

   To build in a default environment set the following to zero:

   USE_TARGET_THREADS

   Other wise search the code (this file only) for the USE_TARGET_THREADS
   and make changes as needed to support your target.

*/

#define SYS_SUPPORTS_SPRINTF 1
#include "rtfs.h"

#if (SYS_SUPPORTS_SPRINTF)
#include <stdio.h>  /* For sprintf */
#define TERM_SPRINTF sprintf
#endif
#include "protests.h"


/* Set USE_TARGET_THREADS to 1 if you wish to use background mode
   asynchronous completion of asynchronous operations from the
   shell.
   Thread support for the Microsoft windows and linux are provided
   For other environments modify the routines in this file:
        ContinueProc()
        spawn_async_continue()
*/
#define USE_TARGET_THREADS 0
#ifdef RTFS_WINDOWS
#undef USE_TARGET_THREADS
#define USE_TARGET_THREADS 1
#endif
#ifdef RTFS_LINUX
#undef USE_TARGET_THREADS
#define USE_TARGET_THREADS 1
#endif

#define EFISHELL_USAGE(X) rtfs_print_one_string((byte *)X,PRFLG_NL)
#define EFISHELL_WARN(X) rtfs_print_one_string((byte *)X,PRFLG_NL)
#define EFISHELL_SHOW(X) rtfs_print_one_string((byte *)X,0)
#define EFISHELL_SHOWNL(X) rtfs_print_one_string((byte *)X,PRFLG_NL)
#define EFISHELL_SHOWINT(PROMPT, VAL) show_status(PROMPT, VAL, 0)
#define EFISHELL_SHOWINTNL(PROMPT, VAL) show_status(PROMPT, VAL, PRFLG_NL)
#define EFISHELL_SHOW64INT(PROMPT, VALHI, VALLO) show_status64(PROMPT, VALHI,VALLO,0)
#define EFISHELL_SHOW64INTNL(PROMPT,VALHI, VALLO) show_status64(PROMPT, VALHI,VALLO,PRFLG_NL)

static void show_status64(char *prompt, dword val_hi,dword val_lo, int flags);
byte *pc_ltoa(dword num, byte *dest, int number_base);



#define SHELLMAXFILES 10
#define FIVE12 512 /* Not sector size dependant */

static BOOLEAN do_efio_command(byte *command_string);
void _tst_shell(byte *initial_command);
static void call_tst_shell(byte *raw_input);


int efio_shell_restore_default_config(void);

/* Addresses of things we may allocate on a per user basis
   If non-null and we are allocating again, then zero */
byte *gl_temp_user_buffers[26];
                          /* Toggle with "async" shell command */
#define ASY_INLINE 0
#define ASY_MANUAL 1
#define ASY_SPAWN  2
int do_async_method = ASY_INLINE;

#define _rtfs_cs_strcmp(A,B, C) rtfs_cs_strcmp((byte *) (A) , (byte *) (B), C)
#define _rtfs_cs_strcpy(A,B, C) rtfs_cs_strcpy((byte *) (A) , (byte *) (B), C)

#define PRECISION_64 12 /* Number of places to show */

void  _tst_shell_init(void);

/* Structure to simplify keeping track of open files */
typedef struct openfile {
    int fd;
    byte filename[255];
    EFILEOPTIONS options;
    byte *transaction_buffer;
} OPENFILE;
OPENFILE open_files[SHELLMAXFILES]; /* Commands access this table by index */
int register_open_file(int fd,byte *name, EFILEOPTIONS *poptions);
static void un_register_open_file(int index);


/* utility functions */
int do_async = 0; /* If TRUE use asynchs versions of API calls */
BOOLEAN _do_async_complete(int driveno, BOOLEAN manual);


/**********************************************************************/
void  efio_shell(void)
{
int default_drive;
byte drive_name[8];

    /* Save the default drive number */
    default_drive = pc_get_default_drive(0);

    /* Zero out per drive context blocks */
    /* Clear tables */
    do_async = 0;
    rtfs_memset(&open_files[0], 0, sizeof(open_files));
    /* Set global variables test_driveid and test_drivenumber to the default drive */
    pc_drno_to_drname(default_drive, drive_name, CS_CHARSET_NOT_UNICODE);
    set_test_drive(drive_name);
    _tst_shell_init();
    do_efio_command((byte *) "help");    /* display help */
    while (do_efio_command((byte *) 0))
        ;
}

#if (!RTFS_CFG_READONLY)                    /* Exclude write commands and dvr command if read only */
/* Command table from efishellwr */
#if (INCLUDE_CIRCULAR_FILES)
int do_cfile_open(int agc, byte **agv);
int do_cfile_extract(int agc, byte **agv);
#endif
int do_efile_setallocation_hint(int agc, byte **agv);
int do_efile_flush(int agc, byte **agv);
int do_efile_settime(int agc, byte **agv);
int do_efile_setsize(int agc, byte **agv);
int do_efile_setcluster(int agc, byte **agv);
int do_efile_chsize(int agc, byte **agv);
int do_efile_writefile(int agc, byte **agv);
int do_efile_delete(int agc, byte **agv);
int do_efile_diskflush(int agc, byte **agv);
int do_efile_tests(int agc, byte **agv);
#endif
#if (INCLUDE_FAILSAFE_CODE)
int do_efile_failsafe(int agc, byte **agv);
#endif

/* Command table */
int donull(int agc, byte **agv);
static int demohelp(int agc, byte **agv);
static int do_efile_open(int agc, byte **agv);
static int do_efile_close(int agc, byte **agv);
static int do_efile_file_stat(int agc, byte **agv);
static int do_efile_read(int agc, byte **agv);
static int do_efile_seek(int agc, byte **agv);
static int do_efile_set_drive(int agc, byte **agv);
static int do_efile_clear_stats(int agc, byte **agv);
static int do_efile_remount(int agc, byte **agv);
static int do_efile_async(int agc, byte **agv);
static int do_efile_async_complete(int agc, byte **agv);


typedef struct dispatcher_text {
    byte *cmd;
    int  (*proc)( int argc, byte **argv);
    byte *helpstr;
} DISPATCHER_TEXT;
DISPATCHER_TEXT democmds[] =
    {
    { (byte *) "-", demohelp, (byte *) "---- Drive and System Operations ------" },
    { (byte*)"clear", do_efile_clear_stats, (byte*)"clear           " },
    { (byte*)"remount", do_efile_remount,   (byte*)"remount         " },
#if (INCLUDE_ASYNCRONOUS_API)
    { (byte*)"async", do_efile_async,       (byte*)"async           " },
    { (byte*)"complete", do_efile_async_complete, (byte*)"complete        " },
#endif
    { (byte*)"setdrive", do_efile_set_drive,(byte*)"setdrive        " },
    { (byte*)"diskflush", do_efile_diskflush,(byte*)"diskflush       " },
    { (byte*)"test", do_efile_tests,         (byte*)"test            " },
#if (INCLUDE_FAILSAFE_CODE)
    { (byte *) "-", donull, (byte *) "---- Failsafe Operations ------" },
    { (byte*)"fs", do_efile_failsafe,       (byte*)"fs command      " },
#endif
    { (byte *) "-", donull, (byte *) "---- File Operations     ------" },
    { (byte*)"open", do_efile_open,         (byte*)"open            " },
    { (byte*)"close", do_efile_close,       (byte*)"close           " },
#if (!RTFS_CFG_READONLY)                    /* Exclude write commands and dvr command if read only */
    { (byte*)"flush", do_efile_flush,              (byte*)"flush           " },
    { (byte*)"sethint", do_efile_setallocation_hint, (byte*)"sethint         " },
    { (byte*)"write", do_efile_writefile,    (byte*)"write          " },
#endif
    { (byte*)"read", do_efile_read,         (byte*)"read            " },
    { (byte*)"seek", do_efile_seek,         (byte*)"seek            " },
    { (byte*)"fstat", do_efile_file_stat,   (byte*)"fstat           " },
#if (INCLUDE_CIRCULAR_FILES)
    { (byte *) "-", donull, (byte *) "---- Circular File Operations     ------" },
    { (byte*)"copen", do_cfile_open,        (byte*)"copen           " },
    { (byte*)"extract", do_cfile_extract,   (byte*)"extract         " },
#endif
#if (!RTFS_CFG_READONLY)                    /* Exclude write commands and dvr command if read only */
    { (byte *) "-", donull, (byte *) "---- Miscelaneous Operations     ------" },
    { (byte*)"chsize", do_efile_chsize,     (byte*)"chsize          " },
    { (byte*)"delete", do_efile_delete,     (byte*)"delete          " },
    { (byte*)"settime", do_efile_settime,   (byte*)"settime         " },
    { (byte*)"setsize", do_efile_setsize,   (byte*)"setsize         " },
    { (byte*)"setcluster", do_efile_setcluster, (byte*)"setcluster      " },
#endif
    { (byte*)"quit",  0, (byte*)"quit" },
    { 0 }
    };
void *lex(void *_pcmds,int *agc, byte **agv,byte *initial_cmd,byte *raw_input);  /* See appcmdsh.c */

DISPATCHER_TEXT *current_command;

void  pc_shell_show_stats(dword start_time);

/* Process one command. If command_string is NULL, lex() will
   prompt for input, otherwise execute the provided command */
static BOOLEAN do_efio_command(byte *command_string)
{
    DISPATCHER_TEXT *pcmd;
    byte raw_input[256];
    dword start_time;
    int  agc = 0;
    byte *agv[20];

    raw_input[0] = 0;
    pcmd = (DISPATCHER_TEXT *)lex((void *)&democmds[0], &agc, &agv[0],command_string,raw_input);
    if (!pcmd || !pcmd->proc)
    {
        return(FALSE);
    }
    else
    {
        current_command = pcmd;
        if (current_command == &democmds[0]) /* help */
        {
            if (raw_input[0] &&
                _rtfs_cs_strcmp(raw_input,"help", CS_CHARSET_NOT_UNICODE) != 0)
            { /* if its not "help" call the standard shell */
                call_tst_shell(raw_input);
                return(TRUE);
            }
        }
        start_time = rtfs_port_elapsed_zero();
        pcmd->proc(agc, &agv[0]);
        /* Show stats if it is not HELP */
        if (pcmd != &democmds[0])
            pc_shell_show_stats(start_time);
        return(TRUE);
    }
}

/* External functions */
/* See apputil.c */
void show_status(char *prompt, dword val, int flags);
long rtfs_atol(byte * s);
int rtfs_atoi(byte * s);
int parse_args(int agc, byte **agv, char *input_template);
int rtfs_args_arg_count(void);
dword rtfs_args_val_hi(int this_arg);
dword rtfs_args_val_lo(int this_arg);
byte *rtfs_args_val_text(int this_arg);
void use_args(int agc, byte **agv);


int   async_continues_completed;
int   async_continue_delay;
BOOLEAN async_task_spawned = FALSE;
static void  spawn_async_continue(void);

/* Shell Procedure section - BEGIN */

/* Linear file procedures BEGIN */

/* Open a 32 or 64 bit file. prompt for file open options

open filename [preallocsize (clusters)] [option option ..]

    open or re-open a file. If asynchronous mode is enable (see async y/n)
    then loop and complete the open asynchronously.

    filename      - File name to open
    preallocsize  - Optional parameter, if specified write calls allocate
                    this many clusters when they need to expand the file.
                    helps reduce fragmentation. When the file is closed
                    unused pre-allocated clusters are freed.

    [options]     - One or more of the following options may be used.

    FIRST_FIT     - Always allocate new clusters from the begining of
                    the FAT region instead of the default, which is to
                    allocate from the next cluster after the last cluster in
                    this file.
    FORCE_FIRST   - Force cluster allocation to allocate the first free
                    cluster, even if that means clusters that span multiple
                    clusters are broken up into multiple writes with seeks.
                    Otherwise by default for writes that span multiple
                    clusters, they are allocated in a contiguos group if
                    possible, and if that is not possible, then they are
                    broken broken up into multiple writes with seeks.
    CONTIGUOUS    - For writes that span multiple clusters, allocate clusters
                    in a contiguos group. If that is not possible, then fail.
    KEEP_PREALLOC - If preallocsize is specified, when the file is closed,
                    instead of releasing preallated clusters, make them part
                    of the file and set the file size to include them.

    REMAP_FILE   - The file being opened will be used as an argument to
                   pc_cfilio_extract().. Can not be wriiten to with the
                   write command.
    TEMP_FILE    - Tell ERTFS to delete the file when it is closed, this
                   also has the effect of never actually writing the
                   cluster chains to the FAT so the close happens much
                   faster than a normal close
    TRUNCATE     - If it is a file reopen truncate the file.
                       Note: this option is invalid if in asynchronous
                       mode.


     When open succeeds it prints an fileindex number that may be used
     as an argument to the other command that operate on open files

*/
//static void demonstrate_dma_with_cfile(byte *filename, BOOLEAN use_cfile_mode);
//static void demonstrate_dma_to_efile(byte *filename);
//static void demonstrate_dma_from_efile(byte *filename);

/*
<TEST>   Procedure:  do_efile_open()
<TEST>   Description: open or re-open a file using pc_efilio_open().
<TEST>   All extended options are supported
<TEST>   Invoke by typing "open" in the extended command shell
*/
dword parse_extended_option(byte *input);
static int do_efile_open(int agc, byte **agv)
{
    int fd;
    byte filename[255];
    EFILEOPTIONS my_options;
    BOOLEAN do_truncate=FALSE;
    BOOLEAN do_buffered=FALSE;
    word flags;
    int index, this_arg;
    this_arg = 0;
//demonstrate_dma_with_cfile((byte *)"DMAFILE", TRUE);
//demonstrate_dma_with_cfile((byte *)"DMAFILE", FALSE);
//demonstrate_dma_to_efile((byte *) "DMAFILE");
//demonstrate_dma_from_efile((byte *) "DMAFILE");

    if (!parse_args(agc, agv,0))
    {
usage:
        EFISHELL_USAGE("usage: open filename [preallocsize (clusters)] [option option ..]");
        EFISHELL_USAGE("[options] == ");
        EFISHELL_USAGE("FIRST_FIT FORCE_FIRST CONTIGUOUS KEEP_PREALLOC FILE_64 LOAD_AS_NEEDED");
        EFISHELL_USAGE("REMAP_FILE TEMP_FILE TRANSACTION_FILE TRUNCATE BUFFERED");
        return(-1);
    }
    /* Parse command line */
    rtfs_memset(&my_options, 0, sizeof(my_options));
    my_options.min_clusters_per_allocation = 0;

    if (rtfs_args_val_text(this_arg))
        _rtfs_cs_strcpy(filename, rtfs_args_val_text(this_arg++), CS_CHARSET_NOT_UNICODE);
    else
        goto usage;
    if (this_arg < rtfs_args_arg_count())
    {    /* Get preallocate value if supplied */
       if (!rtfs_args_val_text(this_arg))
            my_options.min_clusters_per_allocation
               = rtfs_args_val_lo(this_arg++);
       /* Get EFIO OPEN options */
       my_options.allocation_policy = 0;
       while (this_arg < rtfs_args_arg_count())
       {
           dword option;
           if (!rtfs_args_val_text(this_arg))
               goto usage;
           option = parse_extended_option(rtfs_args_val_text(this_arg++));
           if (option == 0xffffffff)
               goto usage;
           if (option == 0xffff0000)
               do_truncate=TRUE;
           else if (option == 0xfffe0000)
               do_buffered=TRUE;
           else
               my_options.allocation_policy |= option;
       }
    }
    if (my_options.allocation_policy & PCE_REMAP_FILE)
        flags = (word)(PO_BINARY|PO_RDWR|PO_TRUNC|PO_CREAT);
    else
    {
        flags = (word)(PO_BINARY|PO_RDWR|PO_CREAT);
        if (do_truncate)
            flags |= PO_TRUNC;
        if (do_buffered)
            flags |= PO_BUFFERED;
    }
    if (do_async)
        my_options.allocation_policy |= PCE_ASYNC_OPEN;

    if (my_options.allocation_policy & PCE_TRANSACTION_FILE)
    {
#if (INCLUDE_TRANSACTION_FILES)
        /* Allocate a transaction buffer. 32 K is larges possible cluster
           size so it will work for all.
           We will free it when we unregister the drive
        */
        my_options.transaction_buffer = (byte *) pro_test_malloc(0x8000);
        my_options.transaction_buffer_size  = 0x8000;
#else
        EFISHELL_USAGE("Transaction files disabled ");
        goto usage;
#endif
    }
    /* Open the file */
    fd = pc_efilio_open((byte *)filename,flags,(word)(PS_IWRITE | PS_IREAD)
                         ,&my_options);
    /* Register the file index for use by other routines */
    if (fd>=0)
    {
        index = register_open_file(fd, filename, &my_options);
#if (INCLUDE_ASYNCRONOUS_API)
        /* Complete asynchronously */
        if (do_async)
        {
            if (!_do_async_complete(pc_fd_to_driveno(fd,0,CS_CHARSET_NOT_UNICODE),FALSE))
            {
                un_register_open_file(index);
                fd = -1;
            }
        }
#endif
            /* ("use handle %d to access <%s>\n", index, filename); */
        EFISHELL_SHOWINT("use handle ", index);
        EFISHELL_SHOW(" to access ");
        EFISHELL_SHOWNL(filename);
        return(0);
    }
    else
    {
        EFISHELL_WARN("    file open failed ");
        return(-1);
    }

}
/* Parse command line options for open command */
dword parse_extended_option(byte *input)
{
#define PARSEOPTION(NAME, INPUT, VALUE)    \
    if (_rtfs_cs_strcmp(NAME,INPUT, CS_CHARSET_NOT_UNICODE) == 0) return(VALUE)

    PARSEOPTION("FIRST_FIT", input, PCE_FIRST_FIT);
    PARSEOPTION("FORCE_FIRST", input, PCE_FORCE_FIRST);
    PARSEOPTION("CONTIGUOUS", input, PCE_FORCE_CONTIGUOUS);
    PARSEOPTION("KEEP_PREALLOC", input, PCE_KEEP_PREALLOC);
    PARSEOPTION("LOAD_AS_NEEDED", input, PCE_LOAD_AS_NEEDED);
    PARSEOPTION("REMAP_FILE", input, PCE_REMAP_FILE);
    PARSEOPTION("TEMP_FILE", input, PCE_TEMP_FILE);
    PARSEOPTION("TRANSACTION_FILE", input, PCE_TRANSACTION_FILE);
    PARSEOPTION("TRUNCATE", input, 0xffff0000);
    PARSEOPTION("BUFFERED", input, 0xfffe0000);
    return(0xffffffff); /* Invalid */
}

/* Read sequential blocks from a file.

read fileindex resetfp xferdata totalsize readsize

     Read bytes from a file.

        fileindex - The file index printed by the "open" or "copen" command
        resetfp   - 'y' or 'Y' to seek to the beginning of the file before
                    reading.
                    'n' or 'N' to read from the current file pointer
        xferdata  - 'y' or 'Y' to transfer data from the file
                    'n' or 'N' to pass a NULL pointer to the read call. When
                    a NULL pointer is passed the routine behaves exactly the
                    same except that data is not read from the data blocks
                    of the file.
       totalsize  - Total number of bytes to read from the file

                    The argument is in (GB,KB,B) format, see below for an
                    explanation

       readsize  -  Number of bytes to read per read call. The read
                    command will call pc_efilio_read or pc_cfilio_read
                    as many times as needed until "totalsize" bytes are
                    read.

                    The argument is in (GB,KB,B) format, see below for an
                    explanation

                    Note: if xferdata is Y or y then the read command
                    call malloc() to allocate a buffer "readsize" bytes
                    long to use for data (the buffer is freed when the
                    command finishes). So very large readsize values will
                    result in very large malloc()s.

                    If xferdata is N or n then no buffer is allocated so
                    gigabyte values and more are acceptable.

*/

/*
<TEST>   Procedure:  do_efile_read()
<TEST>   Description: Read from a file
<TEST>   Invoke by typing "read" in the extended command shell with the following options
<TEST>        fileindex - The file index printed by the "open" or "copen" command
<TEST>        resetfp   - 'y' or 'Y' to seek to the beginning of the file before
<TEST>        xferdata  - 'y' or 'Y' to transfer data or 'n' to skip data transfer portion
<TEST>        totalsize  - Total number of bytes to read from the file, can be a 64 bit value
<TEST>       readsize  -  Number of bytes to read per read call.
*/

static int do_efile_read(int agc, byte **agv)
{
    int index,do_reset, do_xfer;
    byte *read_buffer;
    dword ret_hi, ret_lo, n_read_calls,nread,file_size_hi, file_size_lo,
          buffer_size,pattern_value;
    ddword nread_ddw,n_left_ddw,file_size_ddw,buffer_size_ddw;
     int this_arg=0;
    /* read fileindex:resetfp:xferdata:totalsize:readsize */
    /* IBBII == integer, boolean, boolean ... */
    if (!parse_args(agc, agv,"IBBII"))
    {
       EFISHELL_USAGE("usage: read fileindex resetfp(Y/N) xferdata(Y/N) totalsize(GB,KB,B) readsize(GB,KB,B)");
       return(-1);
    }

    index = rtfs_args_val_lo(this_arg++);
    do_reset = rtfs_args_val_lo(this_arg++); /* "Reset file pointer ? " */
    do_xfer = rtfs_args_val_lo(this_arg++); /* "Perform data transfers ?" */
    file_size_hi = rtfs_args_val_hi(this_arg);
    file_size_lo =  rtfs_args_val_lo(this_arg++);
    buffer_size = rtfs_args_val_lo(this_arg++);
	pattern_value=0;

    if (do_reset)
    {
#if (INCLUDE_CIRCULAR_FILES)
        if (open_files[index].options.allocation_policy & PCE_CIRCULAR_BUFFER)
            pc_cfilio_lseek(open_files[index].fd, CFREAD_POINTER,
                            0, 0, PSEEK_SET, &ret_hi, &ret_lo);
        else
#endif
            pc_efilio_lseek(open_files[index].fd, 0, 0, PSEEK_SET,
                            &ret_hi, &ret_lo);
    }
    if (do_xfer)
    {
        read_buffer = (byte *) pro_test_malloc(buffer_size);
        if (!read_buffer)
        {
            EFISHELL_WARN("    Could not allocate buffer ");
            return(-1);
        }
		pc_efilio_lseek(open_files[index].fd, 0, 0, PSEEK_CUR, &ret_hi, &ret_lo);
		pattern_value= (dword) M64RSHIFT((M64SET32(ret_hi, ret_lo)),2); /* ret/4 */
    }
    else
        read_buffer = 0;


    /* total bytes to read */
    file_size_ddw = M64SET32(file_size_hi, file_size_lo);
    /* bytes per read call */
    buffer_size_ddw = M64SET32(0,buffer_size);
    nread_ddw = M64SET32(0,0);
    n_left_ddw = file_size_ddw;
    n_read_calls = 0;

    while(!M64ISZERO(n_left_ddw))
    {
        n_read_calls += 1;
#if (INCLUDE_CIRCULAR_FILES)
        if (open_files[index].options.allocation_policy & PCE_CIRCULAR_BUFFER)
        {
            if (!pc_cfilio_read(open_files[index].fd, (byte*)read_buffer,
                                buffer_size, &nread))
            {
                EFISHELL_WARN("    pc_cfilio_read failed");
                if (read_buffer)
                    pro_test_free(read_buffer);
                return(FALSE);
            }
        }
        else
#endif
        {
             if (!pc_efilio_read(open_files[index].fd, (byte*)read_buffer,
                buffer_size, &nread))
            {
                EFISHELL_WARN("    pc_efilio_read failed");
                if (read_buffer)
                    pro_test_free(read_buffer);
                return(FALSE);
            }
       }
		if (read_buffer)
		{
		dword i;
		dword *p=(dword *) read_buffer;

			if (n_read_calls==1)
			{
				pattern_value=*p;
				rtfs_print_one_string((byte *)"Testing for contiguous starting at : ", 0); rtfs_print_long_1(pattern_value, PRFLG_NL);
			}
			for (i=0;i<nread/4;i++)
			{
				if (*p!=pattern_value)
				{
					rtfs_print_one_string((byte *)"Compare failed at dword offset  : ", 0); rtfs_print_long_1(pattern_value, PRFLG_NL);
					break;
				}
				p++;
				pattern_value++;
			}
		}
		nread_ddw = M64PLUS32(nread_ddw, nread);
        n_left_ddw = M64MINUS(n_left_ddw,buffer_size_ddw);
        if (buffer_size != nread)
            break;
    }

    if (read_buffer)
        pro_test_free(read_buffer);
    return(0);
}


/* Seek multiple times random lengths


seek fileindex nseeks doread(Y/N) doxfer(Y/N)

     Perform a specified number of seeks of random lengths on a file.

        fileindex - The file index printed by the "open" or "copen" command
        nseeks    - Number of seeks to perform on the file

                    The argument is in (GB,KB,B) format, see below for an
                    explanation

        doread    - 'y' or 'Y' To perform a one block read operation after
                    every seek.
                    'n' or 'N' To just perform the seeks
        doxfer    - If doread is true then this instructs the command whether
                    it should perform data transfers during the read.
                    'n' or 'N' to pass a NULL pointer to the read call. When
                    a NULL pointer is passed the routine behaves exactly the
                    same except that data is not read from the data blocks
                    of the file.

        Note: If doread is 'y' and doxfer is 'y' then for each seek performed
              by this command the underlying device is forced to read a
              single block, which forces a seek on the device itself.


        Example:
         Perform 100000 seeks on a file

                seek 0 1000000 y n

         Perform 100000 seeks on a file and do call read but do not force a
         call to the device driver.

                seek 0 1000000 y n

         Perform 1000 seeks on a file and do call read and do force a
         call to the device driver, )a head seek)

                seek 0 1000 y y

*/

/*
<TEST>   Procedure:  do_efile_seek()
<TEST>   Description: Seek within a file
<TEST>   Invoke by typing "seek" in the extended command shell with the following options
<TEST>        fileindex - The file index printed by the "open" or "copen" command
<TEST>        nseeks    - Number of seeks to perform on the file (for viewing perfromance)
<TEST>        doread    - 'y' To perform a one block read operation after every seek (forces a file read).
<TEST>        doxfer    - If 'Y' and doread is true instructs the command read a block and force a disk seek.
*/

static int _do_efile_seek(int index, dword n_seeks, BOOLEAN do_read, byte *read_buffer);
static int do_efile_seek(int agc, byte **agv)
{
    int index, do_read, do_xfer, this_arg;
    dword n_seeks;
    byte read_buffer[FIVE12],*pread_buffer;
    this_arg = 0;
     /* seek fileindex nseeks doread(Y/N) doxfer(Y/N) */
     if (!parse_args(agc, agv,"IIBB"))
     {
        EFISHELL_USAGE("usage: seek fileindex nseeks doread(Y/N) doxfer(Y/N)");
        return(-1);
     }

     index = rtfs_args_val_lo(this_arg++);
     n_seeks = rtfs_args_val_lo(this_arg++);
     do_read = rtfs_args_val_lo(this_arg++);
     do_xfer = rtfs_args_val_lo(this_arg++);
     if (do_xfer)
         pread_buffer = read_buffer;
     else
          pread_buffer = 0;

     return(_do_efile_seek(index, n_seeks, do_read, pread_buffer));
}
static int _do_efile_seek(int index, dword n_seeks,
                    BOOLEAN do_read, byte *read_buffer)
{
    dword ret_hi, ret_lo,nread,buffer_size,n_seek_calls,filesize_in_16s,
          offset_in_16s;
    ddword file_size_ddw,ltemp_ddw,total_bytes_seeked;

    buffer_size = FIVE12;
    pc_efilio_lseek(open_files[index].fd, 0, 0, PSEEK_END, &ret_hi, &ret_lo);
    if (ret_hi == 0 && ret_lo == 0)
        return(-1);

    file_size_ddw = M64SET32(ret_hi,ret_lo);
    ltemp_ddw = M64RSHIFT(file_size_ddw,4); /* Divide file size by sixteen*/
    filesize_in_16s = M64LOWDW(ltemp_ddw);  /* so we can use 32 bit math */
    if ( filesize_in_16s < 1024)
    {
        EFISHELL_WARN("File to small for seek test parameters");
        return(-1);
    }
    total_bytes_seeked = M64SET32(0,0);
    n_seek_calls = 0;
    while(n_seeks--)
    {
        n_seek_calls += 1;
        offset_in_16s   = filesize_in_16s - 1024;
        ltemp_ddw = M64SET32(0, offset_in_16s);
        ltemp_ddw = M64LSHIFT(ltemp_ddw,4);
        /* Fail on error return or if new offsets unexpected */
        if (!pc_efilio_lseek(open_files[index].fd, M64HIGHDW(ltemp_ddw),
                    M64LOWDW(ltemp_ddw), PSEEK_SET, &ret_hi, &ret_lo) ||
        (M64HIGHDW(ltemp_ddw) != ret_hi) ||(M64LOWDW(ltemp_ddw) != ret_lo))
        {
            EFISHELL_WARN("Seek error");
            return(-1);
        }
        total_bytes_seeked = M64PLUS(total_bytes_seeked,ltemp_ddw);
        /* If do_read, read one block.
           If read_buffer is non zero read will access the device driver
           and cause a head seek */
        if (do_read)
        {
            if (!pc_efilio_read(open_files[index].fd, (byte*)read_buffer,
                buffer_size, &nread))
            {
                EFISHELL_WARN("pc_efilio_read failed");
                if (read_buffer)
                    pro_test_free(read_buffer);
                return(FALSE);
            }
        }
        /* Seek back to origin */
        pc_efilio_lseek(open_files[index].fd, 0,0, PSEEK_SET, &ret_hi, &ret_lo);
        /* Read again to move the head if doing so */
        if (do_read)
        {
            if (!pc_efilio_read(open_files[index].fd, (byte*)read_buffer,
                buffer_size, &nread))
            {
                EFISHELL_WARN("pc_efilio_read failed");
                if (read_buffer)
                    pro_test_free(read_buffer);
                return(FALSE);
            }
        }
    }
    return(0);
}

/* Close a file

close fileindex

        Flush a file's directory entry and cluster chain to disk. And
        release the file.
        If asynchronous mode is enabled (see async y/n) then loop and
        complete the flush asynchronously.

        fileindex - The file index printed by the "open" or "copen" command

*/
/*
<TEST>   Procedure:  do_efile_close()
<TEST>   Description: Close a file
<TEST>   Invoke by typing "close" in the extended command shell with the following options
<TEST>        fileindex - The file index printed by the "open" or "copen" command
<TEST>   Completes asynchronously if the command shell is in asynchronous completion mode.
*/
static int _do_efile_async_close(int index);
static int do_efile_close(int agc, byte **agv)
{
    int index, this_arg;
    BOOLEAN do_unregister = FALSE;

    this_arg = 0;
    /* close fileindex */
    if (!parse_args(agc, agv,"I"))
    {
        EFISHELL_USAGE("usage: close fileindex");
         return(-1);
    }

    index = rtfs_args_val_lo(this_arg++);
#if (INCLUDE_CIRCULAR_FILES)
    if (open_files[index].options.allocation_policy & PCE_CIRCULAR_BUFFER)
    {
        if (!pc_cfilio_close(open_files[index].fd))
            EFISHELL_WARN("    Cannot Close circular file");
        else
            do_unregister = TRUE;
    }
    else
#endif
    {
        if (!do_async)
        {
            if (!pc_efilio_close(open_files[index].fd))
            {
                EFISHELL_WARN("    Cannot Close file");
                return(0);
            }
            do_unregister = TRUE;
        }
#if (INCLUDE_ASYNCRONOUS_API)
        else
        {
            EFISHELL_WARN("forcing async close");
            if (!_do_efile_async_close(index))
            {
                EFISHELL_WARN("    Cannot Close file");
                return(0);
            }
            do_unregister = TRUE;
        }
#endif
    }
    if (do_unregister)
       un_register_open_file(index);
    return(0);
}

#if (INCLUDE_ASYNCRONOUS_API)
/* Asynchronous version of close */
static int _do_efile_async_close(int index)
{
    int status;
    /* Call Asynchronous close start */
    status = pc_efilio_async_close_start(open_files[index].fd);
    if (status == PC_ASYNC_ERROR)
    {
        EFISHELL_WARN("    Cannot Close file");
        return(0);
    }
    else if (status == PC_ASYNC_CONTINUE)
    {
        if (!_do_async_complete(pc_fd_to_driveno(open_files[index].fd,0,CS_CHARSET_NOT_UNICODE),FALSE))
        {
            EFISHELL_WARN("    Cannot Close file");
            return(0);
        }
    }
    return(1);
}
#endif


/*
<TEST>   Procedure:  do_efile_stat()
<TEST>   Description: Performs a call to pc_efilio_fstat() and display information about the file
*/

/* Print statistics on an open file
fstat fileindex

        Print stat information on an open file

        fileindex - The file index printed by the "open" or "copen" command

*/

static int do_efile_file_stat(int agc, byte **agv)
{
    int index;
    ERTFS_EFILIO_STAT estat;
    int  this_arg = 0;

    /* stat fileindex */
    if (!parse_args(agc, agv,"I"))
    {
        EFISHELL_USAGE("usage: stat fileindex");
        return(-1);
    }

    index = rtfs_args_val_lo(this_arg++);

    if (!pc_efilio_fstat(open_files[index].fd,&estat))
    {
        EFISHELL_WARN("    Cannot stat file");
    }
    else
    {
        EFISHELL_SHOWINTNL("    minimum allocation :               ",
                estat.minimum_allocation_size);
        EFISHELL_SHOWINTNL("    allocation_policy  :               ",
                estat.allocation_policy);
        EFISHELL_SHOWINTNL("    fragments_in_file  :               ",
                estat.fragments_in_file);
        EFISHELL_SHOW64INTNL("    file size          :", estat.file_size_hi,estat.file_size_lo);
        EFISHELL_SHOW64INTNL("    allocated size     :", estat.allocated_size_hi,estat.allocated_size_lo);
        EFISHELL_SHOWINTNL("    First cluster      :               ",
                    estat.pfirst_fragment[0]->start_location);
    }
    return(0);
}


#if (INCLUDE_ASYNCRONOUS_API)
#if (USE_TARGET_THREADS)
/*
<TEST>   Procedure:  async_continue_proc()
<TEST>   Description: A background thread that completes asynchronous process by calling pc_async_continue()
<TEST>   Used when the command shell is in asynchronous completion mode.
*/
static void async_continue_proc(void)
{
int status = PC_ASYNC_COMPLETE;
int prev_status  = PC_ASYNC_CONTINUE;
int drive_number;
DDRIVE  *pdr;

     async_continues_completed = 0;
    drive_number = 0;
    for(;;)
    {
        rtfs_port_sleep(async_continue_delay);
        prev_status = status;
        pdr = 0;
        for(;drive_number<26;drive_number++)
        {
            pdr = pc_drno_to_drive_struct(drive_number);
            if (pdr)
            {
                if (pdr->drive_info.drive_operating_flags & DRVOP_MOUNT_VALID)
                    break;
                if (pdr->drive_state.drive_async_state == DRV_ASYNC_MOUNT)
                    break;
            }
            pdr = 0;
        }
        if (!pdr)
            continue;
        /* Do one step */
        status = pc_async_continue(drive_number,DRV_ASYNC_IDLE, 1);
        if (status == PC_ASYNC_COMPLETE)
        {
            if (prev_status != PC_ASYNC_COMPLETE)
            {
                async_continues_completed += 1;
            }
            if (do_async_method != ASY_SPAWN)
            {
                EFISHELL_WARN("Async continue canceled, terminating thread");
                async_task_spawned = FALSE;
                return;
            }
        }
        prev_status = status;
        if (status == PC_ASYNC_ERROR)
        {
            EFISHELL_WARN("    Async continue error, terminating thread");
            async_task_spawned = FALSE;
            return;
        }
    }
}
#endif /* (INCLUDE_ASYNCRONOUS_API) */
#endif /* (USE_TARGET_THREADS)      */


#if (INCLUDE_ASYNCRONOUS_API)

BOOLEAN _do_async_complete(int driveno, BOOLEAN manual)
{
int status;
    if (do_async_method == ASY_SPAWN)
    {
        EFISHELL_WARN(" Wait for background to complete async operattion");
        return(TRUE);
    }
    if (!manual && do_async_method == ASY_MANUAL)
    {
        EFISHELL_WARN(" Call asycomplete to complete async operattion");
        return(TRUE);
    }
    do
    {
        status = pc_async_continue(driveno,DRV_ASYNC_IDLE, 1);
    }  while (status == PC_ASYNC_CONTINUE);
    if (status != PC_ASYNC_COMPLETE)
    {
        EFISHELL_WARN("    can not complete async operattion");
        return(FALSE);
    }
    else
        return(TRUE);
}
#endif
/*
clear DRIVEID:

        Clear the read and write counters for the FAT regions. This routine
        is useful for clearing the counters before executing a command
        so it is more clear how many accesses actually occured to
        complete the command.

         DRIVEID: - The drive id A:. B: etc
*/
static int do_efile_clear_stats(int agc, byte **agv)
{
DRIVE_RUNTIME_STATS drive_rtstats;
    /* remount DRIVEID: */
    if (!parse_args(agc, agv,"T"))
    {
        EFISHELL_USAGE("usage: clear DRIVEID:");
        return(-1);
    }
    if (!pc_diskio_runtime_stats((byte *)rtfs_args_val_text(0), &drive_rtstats, TRUE))
        return(-1);
    return(0);
}

/*
remount DRIVEID:

        Flush the drive, close it and then re-open it and rescan the FAT.

        DRIVEID:   - The drive id A:. B: etc

        If asynchronous mode is enabled (see async y/n) then loop and
        complete the delete asynchronously.

*/
/*
<TEST>   Procedure:  do_efile_remount()
<TEST>   Description: Flushes, closes and remounts a volume.
<TEST>   Completes asynchronously if the command shell is in asynchronous completion mode.
<TEST>   Invoke by typing "remount" in the extended command shell
*/
static int do_efile_remount(int agc, byte **agv)
{
    int driveno, status;
    /* remount DRIVEID: */
    if (!parse_args(agc, agv,"T"))
    {
        EFISHELL_USAGE("usage: remount DRIVEID:");
        return(-1);
    }
#if (RTFS_CFG_READONLY)                    /* Exclude write commands and dvr command if read only */
    status = PC_ASYNC_COMPLETE;
#else
    /* first flush and close the drive so remount occurs */
    status = pc_diskflush(rtfs_args_val_text(0));
    if (status != PC_ASYNC_COMPLETE)
    {
        EFISHELL_WARN("disk flush failed ");
        return(-1);
    }
#endif
    /* close the drive, calling an internal functio, need an API call */

    driveno = pc_parse_raw_drive(rtfs_args_val_text(0), CS_CHARSET_NOT_UNICODE);

    pc_dskfree(driveno);
#if (INCLUDE_ASYNCRONOUS_API)
    if (do_async)
    {
        driveno = pc_diskio_async_mount_start(rtfs_args_val_text(0));
        if (driveno < 0)
        {
            EFISHELL_WARN("    pc_diskio_async_mount_start() failed");
            return(0);
        }
        if (!_do_async_complete(driveno,FALSE))
        {
            EFISHELL_WARN("async disk mount failed failed");
            return(0);
        }
    }
    else
#endif
    {
        if (check_drive_name_mount(rtfs_args_val_text(0), CS_CHARSET_NOT_UNICODE) < 0)
        {
            EFISHELL_WARN("remount failed ");
            return(-1);
        }
        /* release from check mount */
        rtfs_release_media_and_buffers(pc_parse_raw_drive(rtfs_args_val_text(0), CS_CHARSET_NOT_UNICODE));
    }
    return(0);
}

static int do_efile_set_drive(int agc, byte **agv)
{
    if (!parse_args(agc, agv,"T"))
    {
        EFISHELL_USAGE("usage: setdrive D:");
        return(-1);
    }
    /* Set global variables test_driveid and test_drivenumber */
    if (!set_test_drive(rtfs_args_val_text(0)))
        return(-1);
    return(0);
}

/* Disk oriented procedures END */


/* Configration oriented procedures BEGIN */


/*
shell

        Enter the old rtfs interactive test shell. When you are done
        type "QUIT" you will be returned to efishell.

        Note: The commands in the old rtfs test shell are all in upper case
        , not in lower case like they are in efishell.

        Note: If you know the command that you wish to execute in the old
        rtfs test shell, it is not necessary to type "shell" first and then
        type the command. If you type a command that efishell does not
        recognize it automatically passes it to the old shell to see if
        if knows how to execute it, if it does understand the command
        it executes it and returns to efishell.

*/


static void call_tst_shell(byte *raw_input)
{
    _tst_shell(raw_input);
}

#if (INCLUDE_ASYNCRONOUS_API)
/*
<TEST>   Procedure:  do_efile_async()
<TEST>   Description: Enable or disable asynchronous completion modes.
<TEST>   If asynchronous mode is enable then shell commands use asynchronous methds if applicable
<TEST>   Invoke by typing "async" in the extended command shell
<TEST>      Supports "inline" completion in which the shell commands step the async engine until the command is completed.
<TEST>      Supports "manual" completion in which the shell command "complete" must  be called to step the async engine
<TEST>      Supports "background" completion where a thread perfroms completion (requires porting to most targets)
*/
/* Set or clear asynch operations */
static int do_efile_async(int agc, byte **agv)
{
     /* async Y/N */
     if (!parse_args(agc, agv,"BTI"))
     {
usage:
        EFISHELL_USAGE("usage:async Y <bg|inline|manual> delay");
        EFISHELL_USAGE("usage:where delay is delay between sync steps in bg mode");
        EFISHELL_USAGE("usage:or..");
        EFISHELL_USAGE("usage:async N");
        return(-1);
     }
     if (rtfs_args_val_lo(0))
     {
        if (!rtfs_args_val_text(1))
            goto usage;
        if (_rtfs_cs_strcmp(rtfs_args_val_text(1),"bg", CS_CHARSET_NOT_UNICODE) == 0)
        {
            /* Call a routine to spawn a thread to perform async continue */
            /* See async_continue_proc() below */
            async_continue_delay = rtfs_args_val_lo(2);
            if (!async_continue_delay)
            {
                EFISHELL_SHOWNL("No delay specified, default to 100 miliseconds");
                async_continue_delay = 100;
            }
            if (!async_task_spawned)
            {
                spawn_async_continue();
                async_task_spawned = TRUE;
            }
            do_async_method = ASY_SPAWN;
        }
        else if (_rtfs_cs_strcmp(rtfs_args_val_text(1),"inline", CS_CHARSET_NOT_UNICODE) == 0)
            do_async_method = ASY_INLINE;
        else if (_rtfs_cs_strcmp(rtfs_args_val_text(1),"manual", CS_CHARSET_NOT_UNICODE) == 0)
            do_async_method = ASY_MANUAL;
        else
            goto usage;
         do_async = 1;
     }
     else
     {
         EFISHELL_WARN("Async off ");
         do_async = 0;
         return(0);
     }
     return(0);
}
/*
<TEST>   Procedure:  do_efile_async_complete()
<TEST>   Description: step the async engine until all outstanding operations are completed
<TEST>   Invoke by typing "complete" in the extended command shell
<TEST>
*/
static int do_efile_async_complete(int agc, byte **agv)
{
    RTFS_ARGSUSED_PVOID((void *)agv);
    RTFS_ARGSUSED_INT(agc);
    if (do_async_method != ASY_MANUAL)
    {
        EFISHELL_USAGE("usage: Must be in manual mode");
        return(-1);
    }
    if (!_do_async_complete(pc_path_to_driveno(test_driveid, CS_CHARSET_NOT_UNICODE),TRUE))
    {
        EFISHELL_WARN("Async complete failed");
        return(-1);
    }
    return(0);
}
#endif

/* Shell Procedure section - END */


#if (INCLUDE_EXFATORFAT64)
void format_64(char *buffer, ddword ddw, int precision);
#endif

static void show_status64(char *prompt, dword valhi, dword vallo, int flags)
{
#if (INCLUDE_EXFATORFAT64)
char buff[32];
ddword ddw;
    ddw = M64SET32(valhi, vallo);
    rtfs_print_one_string((byte *)prompt,0);
    format_64(buff, ddw, 10);
    rtfs_print_one_string((byte *)buff, flags);
#else
    RTFS_ARGSUSED_DWORD(valhi);
    rtfs_print_one_string((byte *)prompt,0);
    rtfs_print_long_1(vallo,flags);
#endif
}

int register_open_file(int fd,byte *name, EFILEOPTIONS *poptions)
{
dword i;
    /* Get the file and associated drive structure with the crit sem locked */
        for (i = 0; i < SHELLMAXFILES; i++)
        {
            if (!open_files[i].filename[0])
            {
                open_files[i].fd = fd;
                _rtfs_cs_strcpy(open_files[i].filename, name, CS_CHARSET_NOT_UNICODE);
                open_files[i].options = *poptions;
                return(i);
            }
        }
          return(0);
}
static void un_register_open_file(int index)
{
    open_files[index].filename[0] =0;
#if (INCLUDE_TRANSACTION_FILES)
    if (open_files[index].options.transaction_buffer)
        pro_test_free(open_files[index].options.transaction_buffer);
#endif
}

int dohelp_txt(DISPATCHER_TEXT *pcmds,int agc, byte **agv); /* See appcmdsh.c */
static int demohelp(int agc, byte **agv)
{
    dohelp_txt((DISPATCHER_TEXT *)democmds, agc, agv);
    EFISHELL_SHOWNL(" ");
    EFISHELL_SHOW("The Current default drive is :");
    EFISHELL_SHOWNL(test_driveid);
    EFISHELL_SHOW(" ");
    EFISHELL_SHOWNL("Use setdrive to select a new default drive..");
    EFISHELL_SHOWNL(" ");
    return(0);
}

/* User interface section -END */
#if (INCLUDE_ASYNCRONOUS_API)

#if (USE_TARGET_THREADS)
#ifdef RTFS_WINDOWS
#include <process.h>    /* _beginthread, _endthread */

static void __cdecl ContinueProc( void *args )
{
    RTFS_ARGSUSED_PVOID((void *) args);
    async_continue_proc(); /* He'll loop until error and then exit */
}
#endif
#ifdef RTFS_LINUX
#include <pthread.h>
static void ContinueProc( void *args )
{
    RTFS_ARGSUSED_PVOID((void *) args);
    async_continue_proc(); /* He'll loop until error and then exit */
}
#endif
#endif

static void  spawn_async_continue(void)
{
#if (!USE_TARGET_THREADS)
      EFISHELL_USAGE("usage: async mode not supported for this TARGET");
      return;
#endif

#if (USE_TARGET_THREADS)
      if (!async_task_spawned)
      {
#ifdef RTFS_WINDOWS
          _beginthread( ContinueProc, 0, NULL );
#endif
#ifdef RTFS_LINUX
          {
              pthread_t tid;
              pthread_create(&tid, 0, ContinueProc, NULL);
          }
#endif
      }
      async_task_spawned = TRUE;
      do_async_method = ASY_SPAWN;
#endif
}
#endif /* (INCLUDE_ASYNCRONOUS_API) */


#if (0)
// Example 1: Filling a file using DMA operations controlled at the
//            appication layer.
//
// This code fragment demonstrates the basic methods needed
// to use RtfsProPlus to create files that you will populate using
// direct memory access.
//
// For this demonstration we are using ideal conditions that demonstrate
// the principals involved without excess additional glue.
//
// For these ideal conditions we will use a DMA transfer size that is an
// even multiple of the cluster size, and set the minimum preallocation
// size the same as the transfer size. This way there is a one to one to
// one to one correspondance of preallocate requests, get block number
// requests, DMA data to the disk requests, and expand the file, advance
// the file pointer requests.
//
// If these ideal conditions were not met we would be forced to repeat one
// or more of the steps within an internal loop, but the principals are the
// same.
//
// The steps are:
// First open the file with the minimum allocation field set to two clusters
// Then in a repating loop (FIVE12 times)
//  1. Call pc_efilio_write with a null buffer and zero write count. This
//     calling sequence instructs pc_efilio_write to pre-allocate clusters
//     if it is currently at the end of file and more clusters are required.
//     It does not advance the file pointer or change the file size.
//  2. Call pc_efilio_fpos_sector to retrieve the raw sector number at the
//     current file pointer and the number of contiguous sectors.
//  3. Simulate a DMA transfer to the disk with pc_raw_write, which writes
//     contiguous sectors to the disk at the raw sector returned from
//     pc_efilio_fpos_sector.
//  4. Call pc_efilio_write with a null buffer and a write count equal
//     to FIVE12 times the number of sectors we just transfered. This instructs
//     pc_efilio_write to make the previously preallocate clusters a
//     a permanent part of the file and to adjust the file size and advance
//     the file pointer.
//
//   A few things to keep in mind are:
//
//  1. No disk IO takes place during steps one, two and four. So these steps
//     are essentially real time processes.
//  2. Because this is only a simulation we are waiting for step three to
//     complete before we perform step four to update the file structure and
//     then step one and two again to prepare for another "DMA" transfer.
//     This "synchronous" operation is not necessary. The algorithm could
//     be implemented in several other ways.. for example:
//      1. Step three could simply start a DMA transfer and return from the
//         loop. Steps four,one and two could be then be executed asynchronously
//         when the DMA transfer completed.
//      2. Step three could start the DMA transfer and steps four,one and two
//         could be then be executed imediately. This would have the affect
//         of creating a DMA write behind scheme where the file size is up
//         to date, the file pointer is up to date, the DMA circuit is
//         armed to perform the transfer, and the next sector number and
//         sector count are ready so the DMA circuit can be re-armed
//         imediately after completion of the current iteration. This could
//         be repeated as many times as desired to provide a simple method
//         of queueing multiple unattended DMA transfers to populate the
//         file.
//
/*
<TEST>   Procedure:  demonstrate_dma_to_efile()
<TEST>   Description: This code fragment demonstrates the basic methods needed to use RtfsProPlus to write files using direct memory access.
<TEST>
*/

static void demonstrate_dma_to_efile(byte *filename)
{
    int fd, i, driveno;
    dword cluster_size_bytes,dma_data_size_blocks,ltemp;
    byte *pdma_buffer, drive_name[8];
    EFILEOPTIONS my_options;

    // We'll simulate ideal conditions for this demonstration by
    // setting dma transfer size and minumum allocation size
    // the same, at two clusters
    driveno = pc_get_default_drive(drive_name);
    cluster_size_bytes = (dword) pc_cluster_size(drive_name);
    if (!cluster_size_bytes)
        return;  // ????

    rtfs_memset(&my_options, 0, sizeof(my_options));
    // Set the minumum allocation size two two clusters
    my_options.min_clusters_per_allocation = 2;
    // And the dma transfer size to two clusters
    dma_data_size_blocks = (2 * cluster_size_bytes)/FIVE12;
    // Open the file
    fd = pc_efilio_open(filename, (word)(PO_BINARY|PO_RDWR|PO_TRUNC|PO_CREAT),
                                  (word)(PS_IWRITE | PS_IREAD),&my_options);
    if (fd<0)
        return;

    // Perform FIVE12 simulated cluster sized dma transfers while
    // extending the file
    pdma_buffer = (byte *) pro_test_malloc(dma_data_size_blocks*FIVE12);
    for(i=0;i<FIVE12;i++)
    {
    dword raw_sector_number,raw_sector_count;
        // force pc_efilio_write to preallocate clusters if neccessary
        // without changing the file size or advancing the file pointer
        if (!pc_efilio_write(fd, 0,0,&ltemp))
            break;
        // Get the raw disk sector associated with the file pointer
        if (!pc_efilio_fpos_sector(fd, FALSE, TRUE, &raw_sector_number,
                                                    &raw_sector_count))
            break;
       // This should never happen because we are cluster aligned
        if (raw_sector_count < dma_data_size_blocks)
            break;
        // raw_sector_number contains the sector at the file pointer
        // raw_sector_count is the count of contiguous sectors there
        // truncate the raw sector count if it is greater than our
        // transfer size. This won't happen in this demonstration
        // because we set min_clusters_per_allocation to 1, but
        // if we had used a larger minimum value raw_sector_count would
        // reflect that we are preallocating larger chunks
        if (raw_sector_count > dma_data_size_blocks)
            raw_sector_count = dma_data_size_blocks;

        // Now use the RtfsProPlus raw block write routine to
        // Simulate a dma transfer
        if (!pc_raw_write(driveno,  pdma_buffer, raw_sector_number,
                                           raw_sector_count, TRUE))
            break;
        // Now call write with a null data buffer to advance the file
        // pointer and update the file size
        if (!pc_efilio_write(fd, 0,raw_sector_count*FIVE12,&ltemp))
            break;
    }
    pc_efilio_close(fd);
    pro_test_free(pdma_buffer);
}


// Example 2: Raeding a file using DMA operations controlled at the
//            appication layer.
//
// This code fragment demonstrates the basic methods needed
// to use RtfsProPlus to read files using direct memory access.
//
// For this demonstration we will use a DMA transfer size that is larger
// than the transfer size used in example one, and it is not an even
// multiple of the cluster size. This size is chosen to demonstrate that
// the transfer size does not have to be cluster aligned and that
// RtfsProPlus will return to us the number of contiguous sectors at
// the file pointer.
//
//
// The steps are:
// First re-open the file (presumably the file created in example one)
// Then in a repeating loop (repeating as many times as required)
//  1. Call pc_efilio_fpos_sector to retrieve the raw sector number at the
//     current file pointer and the number of contiguous sectors.
//  2. Simulate a DMA transfer from the disk with pc_raw_read, which reads
//     contiguous sectors from the disk at the raw sector returned from
//     pc_efilio_fpos_sector.
//  3. Call pc_efilio_read with a null buffer and a read count equal
//     to FIVE12 times the number of sectors we just transfered. This instructs
//     pc_efilio_read to advance the file pointer.
//
//   A few things to keep in mind are:
//
//  1. No disk IO takes place during steps 1, and 3. So these steps are
//     essentially real time processes.
//  2. Because this is only a simulation we are waiting for step 2 to
//     complete before we perform step three and step one again to prepare
//     for another "DMA" transfer.
//     This "synchronous" operation is not necessary. The algorithm could
//     be implemented in several other ways.. for example:
//      1. Step two could simply start a DMA transfer and return from the
//         loop. Steps three and one could be then be executed asynchronously
//         when the DMA transfer completed.
//      2. Step two could start the DMA transfer and steps three and one
//         could be then be executed imediately. This would have the affect
//         of creating a DMA read behind scheme where the DMA circuit is
//         armed to perform the transfer and the next sector number and
//         sector count are ready so the DMA circuit can be re-armed
//         imediately after completion of the current iteration. This can
//         be repeated as many times as desired and provides a simple method
//         of queueing multiple unattended DMA transfers to read the
//         file. The file could even be closed before the transfers complete.
/*
<TEST>   Procedure:  demonstrate_dma_from_efile()
<TEST>   Description: This code fragment demonstrates the basic methods needed to use RtfsProPlus to read files using direct memory access.
<TEST>
*/
static void demonstrate_dma_from_efile(byte *filename)
{
    int fd, driveno;
    dword cluster_size_bytes,dma_data_size_blocks,ltemp;
    byte *pdma_buffer, drive_name[8];
    EFILEOPTIONS my_options;


    // Re-open the file
    rtfs_memset(&my_options, 0, sizeof(my_options));
    fd = pc_efilio_open(filename, (word)(PO_BINARY|PO_RDWR),
                                  (word)(PS_IWRITE | PS_IREAD),&my_options);
    if (fd<0)
        return;

    // We'll Set a dma transfer size of 8 clusters minus
    // one block to demonstrate that the transfers have to
    // be block aligned, but not cluster aligned

    // In this example we'll use pc_fd_to_driveno to get the drive number
    // and drive name from the open file.
    driveno =  pc_fd_to_driveno(fd,drive_name,CS_CHARSET_NOT_UNICODE);
    cluster_size_bytes = (dword) pc_cluster_size(drive_name);
    if (!cluster_size_bytes)
        return;  // ????
    dma_data_size_blocks = ((8 * cluster_size_bytes) - 1)/FIVE12;


    // Perform as many dma transfers as needed to read the whole file
    pdma_buffer = (byte *) pro_test_malloc(dma_data_size_blocks*FIVE12);
    for(;;)
    {
    dword raw_sector_number,raw_sector_count;
        // Get the raw disk sector associated with the file pointer
        // The second argument is TRUE (check for read)
        if (!pc_efilio_fpos_sector(fd, TRUE, TRUE, &raw_sector_number,
                                                    &raw_sector_count))
            break;
       // This will happen when the file pointer is at the end
        if (!raw_sector_count)
            break;
        // raw_sector_number contains the sector at the file pointer
        // raw_sector_count is the count of contiguous sectors there
        // truncate the raw sector count if it is greater than our
        // transfer size. This will happen because the file is more
        // than likely contiguous and raw_sector_count returns the
        // number of contiguous sectors
        if (raw_sector_count > dma_data_size_blocks)
            raw_sector_count = dma_data_size_blocks;

        // Now use the RtfsProPlus raw block write routine to
        // Simulate a dma transfer from the drive
        if (!pc_raw_read(driveno,  pdma_buffer, raw_sector_number,
                                           raw_sector_count, TRUE))
            break;
        // Now call read with a null data buffer to advance the file
        // pointer
        if (!pc_efilio_read(fd, 0,raw_sector_count*FIVE12,&ltemp))
            break;
    }
    pc_efilio_close(fd);
    pro_test_free(pdma_buffer);
}


// Example 3: Writing and Reading a circular file using DMA operations
//            controlled at the application layer.
//
// This code fragment demonstrates the basic methods needed
// to use RtfsProPlus to read and write circular files using
// direct memory access.
//
// For this demonstration we will use a DMA transfer size that is larger
// than the transfer size used in example one, and it is not an even
// multiple of the cluster size. This size is chosen to demonstrate that
// the transfer size does not have to be cluster aligned and that
// RtfsProPlus will return to us the number of contiguous sectors at
// the file pointer.
//
//
// The steps are:
// First re-open the file (presumably the file created in example one)
// Then in a repeating loop (repeating as many times as required)
//  1. Call pc_efilio_fpos_sector to retrieve the raw sector number at the
//     current file pointer and the number of contiguous sectors.
//  2. Simulate a DMA transfer from the disk with pc_raw_read, which reads
//     contiguous sectors from the disk at the raw sector returned from
//     pc_efilio_fpos_sector.
//  3. Call pc_efilio_read with a null buffer and a read count equal
//     to FIVE12 times the number of sectors we just transfered. This instructs
//     pc_efilio_read to advance the file pointer.
//
//   A few things to keep in mind are:
//
//  1. No disk IO takes place during steps 1, and 3. So these steps are
//     essentially real time processes.
//  2. Because this is only a simulation we are waiting for step 2 to
//     complete before we perform step three and step one again to prepare
//     for another "DMA" transfer.
//     This "synchronous" operation is not necessary. The algorithm could
//     be implemented in several other ways.. for example:
//      1. Step two could simply start a DMA transfer and return from the
//         loop. Steps three and one could be then be executed asynchronously
//         when the DMA transfer completed.
//      2. Step two could start the DMA transfer and steps three and one
//         could be then be executed imediately. This would have the affect
//         of creating a DMA read behind scheme where the DMA circuit is
//         armed to perform the transfer and the next sector number and
//         sector count are ready so the DMA circuit can be re-armed
//         imediately after completion of the current iteration. This can
//         be repeated as many times as desired and provides a simple method
//         of queueing multiple unattended DMA transfers to read the
//         file. The file could even be closed before the transfers complete.

static void demonstrate_dma_with_cfile(byte *filename, BOOLEAN use_cfile_mode)
{
    int fd, i, driveno;
    dword cluster_size_bytes,ltemp, nwritten;
    dword dma_data_size_blocks;
    byte *dma_buffer;
    byte drive_name[8];
    EFILEOPTIONS my_options;
    dword raw_sector_number,raw_sector_count;

    driveno = pc_get_default_drive(drive_name);
    cluster_size_bytes = (dword) pc_cluster_size(drive_name);
    if (!cluster_size_bytes)
        return;  // ????

    // Open a circular file
    // Set the wrap point at megabyte
    rtfs_memset(&my_options, 0, sizeof(my_options));
    my_options.circular_file_size_lo = (1024*1024);
    // Use a 1 megabyte minimum allocation and force
    // contiguous allocation so it's completely contiguous
    my_options.min_clusters_per_allocation =
        (my_options.circular_file_size_lo+cluster_size_bytes-1)/
        cluster_size_bytes;
    my_options.allocation_policy = PCE_FORCE_CONTIGUOUS;
    // The default is PCE_CIRCULAR_BUFFER mode, where the write
    // pointer can overtake the read pointer. In PCE_CIRCULAR_FILE
    // mode writes are truncated if the write pointer catches the
    // read pointer.
    if (use_cfile_mode)
        my_options.allocation_policy |= PCE_CIRCULAR_FILE;

    fd = pc_cfilio_open((byte *)filename,
                        (word)(PO_BINARY|PO_RDWR|PO_CREAT|PO_TRUNC)
                        ,&my_options);
    if (fd<0)
        return;

    // Allocate a 128 k buffer
    dma_data_size_blocks = 256;
    dma_buffer  = (byte *) pro_test_malloc(dma_data_size_blocks*FIVE12);

    // Start by writing zero bytes to the file. This should force a
    // Pre-alloc
    if (!pc_cfilio_write(fd, 0, 0, &ltemp))
        return;
    // Check the readable raw sectors now there should be 0 blocks
    if (!pc_efilio_fpos_sector(fd, TRUE, TRUE, &raw_sector_number,
                                                &raw_sector_count))
        return;
    if (raw_sector_count != 0)
        return;
    // Check the writable raw sectors now there should be 2048 blocks
    if (!pc_efilio_fpos_sector(fd, FALSE, TRUE, &raw_sector_number,
                                                &raw_sector_count))
        return;
    if (raw_sector_count != 2048)
        return;

    if (!pc_raw_write(driveno,dma_buffer,raw_sector_number,256,TRUE))
       return;
    // Check the readable raw sectors there should still be 0 blocks
    if (!pc_efilio_fpos_sector(fd, TRUE, TRUE, &raw_sector_number,
                                                &raw_sector_count))
        return;
    // Now tell RtfsProPlus that we wrote 256 blocks
    if (!pc_cfilio_write(fd, 0, (256*FIVE12), &nwritten))
        return;
    // Check the readable raw sectors now there should be 256 blocks
    if (!pc_efilio_fpos_sector(fd, TRUE, TRUE, &raw_sector_number,
                                                &raw_sector_count))
        return;
    if (raw_sector_count != 256)
        return;
    // Check the writable raw sectors now there should be 2048-256 blocks
    if (!pc_efilio_fpos_sector(fd, FALSE, TRUE, &raw_sector_number,
                                                &raw_sector_count))
        return;
    if (raw_sector_count != (2048-256))
        return;
    // Now write 6 more chunks to get within 256 blocks of the wrap point
    for (i = 0; i < 6; i++)
    {
        if (!pc_efilio_fpos_sector(fd, FALSE, TRUE, &raw_sector_number,
                                                    &raw_sector_count))
            return;
        if (raw_sector_count < 256)
            return;
        if (!pc_raw_write(driveno,dma_buffer,raw_sector_number,256,TRUE))
            return;
        if (!pc_cfilio_write(fd, 0, (256*FIVE12), &nwritten))
            return;
    }
    // Make sure we are where we think we should be
    if (!pc_efilio_fpos_sector(fd, FALSE, TRUE, &raw_sector_number,
                                                &raw_sector_count))
        return;
    if (raw_sector_count != 256)
        return;
    // Now write 128 blocks
    if (!pc_raw_write(driveno,dma_buffer,raw_sector_number,128,TRUE))
        return;
    if (!pc_cfilio_write(fd, 0, (128*FIVE12), &nwritten))
        return;
    // Ever vigilant, make sure RtfsProPlus indicates that we can only
    // write 128 contiguous blocks
    if (!pc_efilio_fpos_sector(fd, FALSE, TRUE, &raw_sector_number,
                                                &raw_sector_count))
        return;
    if (raw_sector_count != 128)
        return;
    // Now write up to the wrap point
    if (!pc_raw_write(driveno,dma_buffer,raw_sector_number,128,TRUE))
        return;
    if (!pc_cfilio_write(fd, 0, (128*FIVE12), &nwritten))
        return;
    // Check the readable raw sectors now there should be 2048 blocks
    if (!pc_efilio_fpos_sector(fd, TRUE, TRUE, &raw_sector_number,
                                                &raw_sector_count))
        return;
    if (raw_sector_count != 2048)
        return;
    // Check the writable raw sectors since we're back at the beginning
    if (!pc_efilio_fpos_sector(fd, FALSE, TRUE, &raw_sector_number,
                                                &raw_sector_count))
        return;
    if (use_cfile_mode)
    {// PCE_CIRCULAR_FILE mode.. Should be no blcks available tp write
        if (raw_sector_count != 0)
            return;
    }
    // PCE_CIRCULAR_BUFFER mode.. there should be 2048 blocks
    else if (raw_sector_count != 2048)
        return;
    // Now show that the writeable blocks track the read pointer
    // in PCE_CIRCULAR_FILE mode..
    if (use_cfile_mode)
    { // Now consume some blocks an make sure they are freed up for the
      // writer
      for (i = 1; i < 10; i++)
      {
        if (!pc_efilio_fpos_sector(fd, TRUE, TRUE, &raw_sector_number,
                                                &raw_sector_count))
            return;
        if (raw_sector_count < 128)
            return;
        if (!pc_raw_read(driveno,dma_buffer,raw_sector_number,128,TRUE))
            return;
        if (!pc_cfilio_read(fd, 0, (128*FIVE12), &nwritten))
            return;
        // Check the writable raw sectors
        if (!pc_efilio_fpos_sector(fd, FALSE, TRUE, &raw_sector_number,
                                                  &raw_sector_count))
            return;
        if (raw_sector_count != (dword)(i * 128))
            return;
        }
    }

    pc_cfilio_close(fd);
    pro_test_free(dma_buffer);
}
#endif /* (0) */
