/*
<TEST>  Test File:   rtfscommon/apps/appcmdshrd.c
<TEST>
<TEST>   Procedure: _tst_shell()
<TEST>   Description: Interactive command commands that call common Rtfs subroutines.
<TEST>
*/


/**************************************************************************
    APPTSTSH.C   - Command driven test shell for embedded file manager.

Summary
    TSTSH

 Description
    Interactive shell program designed to allow testing of the file manager.
Returns

Example:
*****************************************************************************
*/

/* If sprintf is not available to you set SYS_SUPPORTS_SPRINTF to zero.
***************************************************************************** */


#define SYS_SUPPORTS_SPRINTF 1
#include "rtfs.h"


#if (SYS_SUPPORTS_SPRINTF)
#if (defined(__IAR_SYSTEMS_ICC__))
#include "rtpprint.h"  /* For rtp_sprintf */
#define TERM_SPRINTF rtp_sprintf
#else
#include <stdio.h>  /* For sprintf */
#define TERM_SPRINTF sprintf
#endif
#endif


#define INCLUDE_MKROM 0
#define INCLUDE_MKHOSTDISK 0
#define INCLUDE_CHKDSK 1


#ifdef _MSC_VER
/* In pc simulation environment use statics so we can leak check the system on reset */
#define LEAKCHECK_ON_RESET 1
BOOLEAN check_for_leaks();
#else
/* leak check can be implemented on other systems if desired */
#define LEAKCHECK_ON_RESET 0
#endif

#define DISPLAY_ERRNO(ROUTINE)
/* #define DISPLAY_ERRNO(ROUTINE) printf("API Call Failed %s: errno == %d\n", ROUTINE, get_errno()); */
/* Set to one to compile in check disk.  */

#ifdef RTFS_WINDOWS
#if (INCLUDE_V_1_0_CONFIG==0)
static int dosavesectors(int agc, byte **agv);
static int dorestoresectors(int agc, byte **agv);
#endif
#endif

#if (!RTFS_CFG_READONLY)            /* Exclude write commands if built for read only */
int dowrite(int agc, byte **agv);
int domkdir(int agc, byte **agv);
int dormdir(int agc, byte **agv);
int dorm(int agc, byte **agv);
int dodeltree(int agc, byte **agv);
int domv(int agc, byte **agv);
int dochsize(int agc, byte **agv);
int dodiskflush(int agc, byte **agv);
#if (INCLUDE_CHKDSK)
int dochkdsk(int agc, byte **agv);
#endif
int docopy(int agc, byte **agv);
int dofillfile(int agc, byte **agv);
int dosetattr(int agc, byte **agv);
int dosetvol(int agc, byte **agv);
int doformat(int agc, byte **agv);
int dodeviceformat(int agc, byte **agv);
int dodumpmbr(int agc, byte **agv);
int dohackwin7(int agc, byte **agv);
int dodumpbpb(int agc, byte **agv);
int dofdisk(int agc, byte **agv);
int doregress(int agc, byte **agv);
int donandregress(int agc, byte **agv);
int dotestopenspeed(int agc, byte **agv);
#ifdef _MSC_VER
#if (INCLUDE_HOSTDISK)
int dowindir(int agc, byte **agv);
int dowinimport(int agc, byte **agv);
#endif
#endif
#endif
#if (INCLUDE_FAILSAFE_CODE)
int do_efile_failsafe(int agc, byte **agv);
#endif
void  efio_shell(void);

int doexfatformat(int agc, byte **agv);
int doprobeexfatformat(int agc, byte **agv);
#if (INCLUDE_MATH64)
void format_64(char *buffer, ddword ddw, int precision);
int dofillhugefile(int agc, byte **agv);
int doreadhugefile(int agc, byte **agv);
#endif

static void rtfs_print_format_dir(byte *display_buffer, DSTAT *statobj);
static void rtfs_print_format_stat(byte *display_buffer, ERTFS_STAT *st);

#if (INCLUDE_MKROM)
int domkrom(int agc, byte **agv);
#endif
#if (INCLUDE_MKHOSTDISK)
int domkhostdisk(int agc, byte **agv);
#endif

/* 10-24-2000 added LBA formatting. Submit to main tree */
#define INCLUDE_CHKDSK 1

/* See apputil.c */
long rtfs_atol(byte * s);
int rtfs_atoi(byte * s);
int parse_args(int agc, byte **agv, char *input_template);
int rtfs_args_arg_count(void);
dword rtfs_args_val_hi(int this_arg);
dword rtfs_args_val_lo(int this_arg);
byte *rtfs_args_val_text(int this_arg);
void use_args(int agc, byte **agv);
void use_args(int agc, byte **agv);
#if (INCLUDE_CS_UNICODE)
byte *rtfs_args_val_utext(int this_arg, int which_uarg);
#endif
void rtfs_print_prompt_user(byte *prompt, byte *buf);

byte shell_buf[1024];
byte working_buf[512];      /* Not sector size dependant used by lex: must be global */

BOOLEAN unicode_enabled = FALSE;

typedef struct dispatcher_text {
    byte *cmd;
    int  (*proc)( int argc, byte **argv);
    byte *helpstr;
} DISPATCHER_TEXT;


#define RNDFUFFSIZE 512 /* Not sector size dependant */
typedef struct rndfile {
    int fd;
    int reclen;
    byte name[90];
    byte buff[RNDFUFFSIZE];
} RNDFILE;

#define MAXRND 10

RNDFILE rnds[MAXRND];
int dohelp_txt(DISPATCHER_TEXT *pcmds,int agc, byte **agv);
void *lex(void *_pcmds, int *agc, byte **agv,byte *initial_cmd,byte *raw_input);
int doquit(int agc, byte **agv);
RNDFILE *fndrnd( int fd);
int doeject(int agc, byte **agv);
int dobreak(int agc, byte **agv);
int donull(int agc, byte **agv);
int dohelp(int agc, byte **agv);
int doverbose(int agc, byte **agv);
int doselect(int agc, byte **agv);
int rndop(int agc, byte **agv);
int doclose(int agc, byte **agv);
int doseek(int agc, byte **agv);
int doread(int agc, byte **agv);
int dolstop(int agc, byte **agv);
int docwd(int agc, byte **agv);
int dols(int agc, byte **agv);
int dorls(int agc, byte **agv);
int doenum(int agc, byte **agv);
static int pc_seedir(byte *path, BOOLEAN backwards);
int docat(int agc, byte **agv);
int dodevinfo(int agc, byte **agv);
int dodiff(int agc, byte **agv);
int dostat(int agc, byte **agv);
int dogetattr(int agc, byte **agv);
int dogetvol(int agc, byte **agv);
#if (INCLUDE_RTFS_PROPLUS)
static int doeshell(int agc, byte **agv);
#endif
static int doreset(int agc, byte **agv);
static int dodiskclose(int agc, byte **agv);

#if (INCLUDE_RTFS_PROPLUS)
static int do_show_file_blocks(int agc, byte **agv);
int do_show_disk_free(int agc, byte **agv);
#endif

static int do_show_dirent_extents(int agc, byte **agv);
static int do_show_disk_stats(int agc, byte **agv);
#if (INCLUDE_CS_UNICODE)
static int dounicode(int agc, byte **agv);
#endif
/* From rtfsinit.c */
void print_device_names(void);
#if (INCLUDE_FAILSAFE_CODE)
static FAILSAFE_RUNTIME_STATS running_failsafe_stats;  /* So we can measure accesses in a call without clearing stats */
#endif
#if (INCLUDE_DEBUG_LEAK_CHECKING)
static int doleaktest(int agc, byte **agv);
#endif
static int doshowerrno(int agc, byte **agv);
static int doloopfile(int agc, byte **agv);
static int doloopdir(int agc, byte **agv);
static int doloopfix(int agc, byte **agv);

static DRIVE_RUNTIME_STATS running_disk_stats;  /* So we can measure accesses in a call without clearing stats */

DISPATCHER_TEXT cmds[] =
    {
    { (byte *) "-", dohelp, (byte *) "---- Random Access File Operations ------" },
    { (byte *) "RNDOP", rndop, (byte *) "RNDOP" },
    { (byte *) "READ",  doread, (byte *) "READ" },
    { (byte *) "SEEK",  doseek, (byte *) "SEEK" },
#if (!RTFS_CFG_READONLY)            /* Exclude write commands if built for read only */
    { (byte *) "WRITE", dowrite, (byte *) "WRITE" },
#endif
    { (byte *) "CLOSE",  doclose, (byte *) "CLOSE" },
    { (byte *) "LSTOPEN", dolstop, (byte *) "LSTOPEN" },
#if (!RTFS_CFG_READONLY)            /* Exclude write commands if built for read only */
    { (byte *) "-", donull, (byte *) "---- Test and Miscelaneous Operations ------" },
    { (byte *) "OPENSPEED", dotestopenspeed, (byte *) "OPENSPEED" },
    { (byte *) "REGRESSTEST", doregress, (byte *) "REGRESSTEST D:" },
#if (INCLUDE_NAND_DRIVER)
    { (byte *) "TESTNAND", donandregress, (byte *) "TESTNAND D:" },
#endif
#if (INCLUDE_RTFS_PROPLUS)
    { (byte *) "ESHELL", doeshell, (byte *) "ESHELL" },
#endif
    { (byte *) "QUIT", 0, (byte *) "QUIT" },
    { (byte *) "VERBOSE", doverbose, (byte *) "VERBOSE Y/N" },
#if (INCLUDE_CS_UNICODE)
    { (byte *) "UNICODE", dounicode, (byte *) "UNICODE Y/N" },
#endif
#if (INCLUDE_MKHOSTDISK)
    { (byte *) "MKHOSTDISK", domkhostdisk, (byte *) "MKHOSTDISK win9xpath" },
#endif
#if (INCLUDE_MKROM)
    { (byte *) "MKROM", domkrom, (byte *) "MKROM" },
#endif
#endif
#if (INCLUDE_FAILSAFE_CODE)
    { (byte *) "-", donull, (byte *) "---- Failsafe Operations ------" },
    { (byte*)"FS", do_efile_failsafe,       (byte*)"FS command  (control failsafe)" },
#endif
    { (byte *) "-", donull, (byte *) "---- Drive and System Operations ------" },
    { (byte *) "DSKSEL", doselect, (byte *) "DSKSEL" },
    { (byte *) "DSKCLOSE", dodiskclose, (byte *) "DSKCLOSE" },
#if (!RTFS_CFG_READONLY)             /* Exclude write commands if built for read only */
    { (byte *) "DSKFLUSH", dodiskflush, (byte *) "DSKFLUSH" },
#endif
    { (byte *) "DEVINFO", dodevinfo, (byte *) "DEVINFO" },
    { (byte *) "SHOWDISKSTATS", do_show_disk_stats, (byte *) "SHOWDISKSTATS" },
#if (INCLUDE_RTFS_PROPLUS)
#if (!RTFS_CFG_READONLY)             /* Exclude write commands if built for read only */
    { (byte *) "SHOWDISKFREE", do_show_disk_free, (byte *) "SHOWDISKFREE" },
#endif
#endif
#if (!RTFS_CFG_READONLY)            /* Exclude write commands if built for read only */
#if (INCLUDE_EXFATORFAT64)
    { (byte *) "EXFATFORMAT",        doexfatformat, (byte *)"EXFATFORMAT" },
    { (byte *) "EXFATPROBE",         doprobeexfatformat, (byte *)"EXFATPROBE" },
#endif
    { (byte *) "FORMAT",        doformat, (byte *)"FORMAT" },
    { (byte *) "FDISK",         dofdisk,  (byte *)"FDISK" },
    { (byte *) "DEVICEFORMAT",  dodeviceformat, (byte *)"DEVICEFORMAT" },
#ifdef _MSC_VER
#if (INCLUDE_HOSTDISK)
#if (INCLUDE_V_1_0_CONFIG==0)
    { (byte *) "WINSCAN",  dowindir, (byte *)"WINSCAN path (scan windows subdir)" },
    { (byte *) "WINIMPORT",  dowinimport, (byte *) "WINIMPORT path D:" },
#endif
#endif
#endif

    { (byte *) "DUMPMBR",  dodumpmbr, (byte *)"DUMPMBR" },
    { (byte *) "DUMPBPB",   dodumpbpb, (byte *)"DUMPBPB" },
#if (INCLUDE_WINDEV)
    { (byte *) "HACKWIN7", dohackwin7, (byte *)"HACKWIN7 Toggle Clobber/restore MBR" },
#endif
#ifdef RTFS_WINDOWS
#if (INCLUDE_V_1_0_CONFIG==0)
    { (byte *) "SAVESECTORS", dosavesectors, (byte *)"SAVESECTORS filename path start end" },
    { (byte *) "RESTORESECTORS", dorestoresectors, (byte *)"RESTORESECTORS filename path start end" },
#endif
#endif
#if (INCLUDE_CHKDSK)
    { (byte *) "CHKDSK", dochkdsk, (byte *) "CHKDSK" },
#endif
#endif
    { (byte *) "EJECT", doeject, (byte *) "EJECT (simulate a removal event)" },
    { (byte *) "RESET", doreset, (byte *) "RESET (reinitialize RTFS)" },
    { (byte *) "-", donull, (byte *) "---- Utility Operations ------" },
    { (byte *) "CD", docwd, (byte *) "CD PATH or CD to display PWD " },
    { (byte *) "DIR", dols, (byte *) "DIR" },
#if (INCLUDE_REVERSEDIR)
    { (byte *) "RDIR", dorls, (byte *) "RDIR (reverse dir)" },
#endif
    { (byte *) "ENUMDIR", doenum, (byte *) "ENUMDIR" },
    { (byte *) "STAT", dostat, (byte *) "STAT" },
    { (byte *) "GETATTR", dogetattr, (byte *)"GETATTR" },
    { (byte *) "GETVOL", dogetvol, (byte *)"GETVOL" },
#if (!RTFS_CFG_READONLY)            /* Exclude write commands if built for read only */
    { (byte *) "SETATTR", dosetattr, (byte *) "SETATTR" },
    { (byte *) "SETVOL", dosetvol, (byte *) "SETVOL D: VOLUME (use XXXXXXXX.YYY form)" },
    { (byte *) "MKDIR", domkdir, (byte *) "MKDIR" },
    { (byte *) "RMDIR", dormdir, (byte *) "RMDIR" },
    { (byte *) "DELTREE", dodeltree, (byte *) "DELTREE" },
    { (byte *) "DELETE",dorm, (byte *) "DELETE" },
    { (byte *) "RENAME",domv, (byte *) "RENAME" },
    { (byte *) "CHSIZE", dochsize, (byte *) "CHSIZE" },
#endif
#if (!RTFS_CFG_READONLY)             /* Exclude write commands if built for read only */
    { (byte *) "FILLFILE", dofillfile, (byte *)"FILLFILE" },
    { (byte *) "COPY", docopy, (byte *)"COPY" },
#if (INCLUDE_MATH64)
    { (byte *) "FILLHUGEFILE", dofillhugefile, (byte *)"FILLHUGEFILE ? for help" },
    { (byte *) "READHUGEFILE", doreadhugefile, (byte *)"READHUGEFILE ? for help" },
#endif
#endif
    { (byte *) "DIFF", dodiff, (byte *)"DIFF" },
    { (byte *) "CAT", docat, (byte *)"CAT" },
#if (INCLUDE_RTFS_PROPLUS)
    { (byte *) "SHOWFILEEXTENTS", do_show_file_blocks, (byte *) "SHOWFILEEXTENTS" },
#endif
#if (INCLUDE_DEBUG_LEAK_CHECKING)
    { (byte *) "LEAKCHECK", doleaktest, (byte *) "LEAKCHECK" },
#endif
    { (byte *) "SHOWEXTENTS", do_show_dirent_extents, (byte *) "SHOWEXTENTS" },
    { (byte *) "LOOPFILE", doloopfile, (byte *) "LOOPFILE" },
    { (byte *) "LOOPDIR", doloopdir, (byte *) "LOOPDIR" },
    { (byte *) "LOOPFIX", doloopfix, (byte *) "LOOPFIX" },
    { (byte *) "ERRNO", doshowerrno, (byte *) "ERRNO" },
    { (byte *) "BREAK", dobreak, (byte *) "BREAK (to debugger)" },
    { 0 }
    };

static BOOLEAN tstsh_check_for_exfatorfat64(byte *path);

BOOLEAN tstsh_is_(byte *p, byte c)
{
int  index;
    index = CS_OP_ASCII_INDEX(p, 'A', CS_CHARSET_NOT_UNICODE);
    if (index == (int) (c - 'A'))
        return(TRUE);
    else
        return(FALSE);
}
BOOLEAN tstsh_is_yes(byte *p)
{
    return(tstsh_is_(p, 'Y'));
}
BOOLEAN tstsh_is_no(byte *p)
{
    return(tstsh_is_(p, 'N'));
}

/* ******************************************************************** */
/* THIS IS THE MAIN PROGRAM FOR THE TEST SHELL */
/* ******************************************************************** */
/* Entry point for interactive shell */
void  _tst_shell(byte *initial_command);
void  _tst_shell_init(void);
void  tst_shell(void)
{
    _tst_shell(0);
}
/* Entry point callable shell to execute one command  */
void  pc_shell_show_stats(dword start_time);

void  _tst_shell_init(void)
{
int i;
    rtfs_memset((byte *)rnds, 0, sizeof(RNDFILE)*MAXRND);
    rtfs_memset((byte *)&running_disk_stats, 0, sizeof(running_disk_stats));
    for (i = 0 ; i < MAXRND; i++)
        rnds[i].fd = -1;
}


void  _tst_shell(byte *initial_command)
{
    DISPATCHER_TEXT *pcmd;
    int  agc = 0;
    byte *agv[20];
    int i;
    dword start_time;

    if (!initial_command)
    {
        _tst_shell_init();
        rtfs_print_prompt_user((byte *)"Press Return ", working_buf);
        dohelp(agc, agv);
        pcmd = (DISPATCHER_TEXT *)lex((void *)cmds, &agc, &agv[0],0,0);
    }
    else
	{
		working_buf[0]=0;
        pcmd = (DISPATCHER_TEXT *)lex((void *)cmds, &agc, &agv[0],initial_command,0);
	}
    while (pcmd)
    {
        if (!pcmd->proc)
            return;
        start_time = rtfs_port_elapsed_zero();
		i = pcmd->proc(agc, &agv[0]);
		if (i < 0)
			RTFS_PRINT_STRING_1((byte *)"Command failed or did not execute", PRFLG_NL);
		/* Show stats if it is not HELP */
		if (i >= 0 && pcmd != &cmds[0])
			pc_shell_show_stats(start_time);
		if (initial_command)
			return;
        pcmd = (DISPATCHER_TEXT *)lex((void *)cmds, &agc, &agv[0],0,0);
   }
}


static BOOLEAN display_io_stats = FALSE;
int doverbose(int agc, byte **agv)
{
    if (parse_args(agc, agv,"T"))
    {
        if (rtfs_args_arg_count() == 1)
        {
            if (*rtfs_args_val_text(0) == 'y' || *rtfs_args_val_text(0) == 'Y')
                display_io_stats = TRUE;
            else
                display_io_stats = FALSE;
        }
    }
    else
    {
        RTFS_PRINT_STRING_1((byte *)"Usage: VERBOSE (Y/N)", PRFLG_NL);
    }


    return(0);
}

/*
<TEST>  Procedure:   pc_shell_show_stats()
*/

void  pc_shell_show_stats(dword start_time)
{
    DRIVE_RUNTIME_STATS disk_stats;
    DRIVE_RUNTIME_STATS show_disk_stats;
#if (INCLUDE_FAILSAFE_CODE)
    FAILSAFE_RUNTIME_STATS failsafe_stats;
    FAILSAFE_RUNTIME_STATS show_failsafe_stats;
#endif
    if (!display_io_stats)
        return;
    rtfs_memset(&show_disk_stats, 0, sizeof(show_disk_stats));
    rtfs_memset(&disk_stats, 0, sizeof(disk_stats));
    if (pc_diskio_runtime_stats((byte *)"", &disk_stats, FALSE))
    {
    show_disk_stats.fat_reads = disk_stats.fat_reads - running_disk_stats.fat_reads;
    show_disk_stats.fat_blocks_read = disk_stats.fat_blocks_read - running_disk_stats.fat_blocks_read;
    show_disk_stats.fat_writes = disk_stats.fat_writes - running_disk_stats.fat_writes;
    show_disk_stats.fat_blocks_written = disk_stats.fat_blocks_written - running_disk_stats.fat_blocks_written;
    show_disk_stats.dir_buff_hits = disk_stats.dir_buff_hits - running_disk_stats.dir_buff_hits;
    show_disk_stats.dir_buff_reads = disk_stats.dir_buff_reads - running_disk_stats.dir_buff_reads;
    show_disk_stats.dir_buff_writes = disk_stats.dir_buff_writes - running_disk_stats.dir_buff_writes;
    show_disk_stats.dir_direct_reads = disk_stats.dir_direct_reads - running_disk_stats.dir_direct_reads;
    show_disk_stats.dir_direct_blocks_read = disk_stats.dir_direct_blocks_read - running_disk_stats.dir_direct_blocks_read;
    show_disk_stats.dir_direct_writes = disk_stats.dir_direct_writes - running_disk_stats.dir_direct_writes;
    show_disk_stats.dir_direct_blocks_written = disk_stats.dir_direct_blocks_written - running_disk_stats.dir_direct_blocks_written;
    show_disk_stats.async_steps = disk_stats.async_steps - running_disk_stats.async_steps;

    show_disk_stats.file_buff_hits = disk_stats.file_buff_hits - running_disk_stats.file_buff_hits;
    show_disk_stats.file_buff_reads = disk_stats.file_buff_reads - running_disk_stats.file_buff_reads;
    show_disk_stats.file_buff_writes = disk_stats.file_buff_writes - running_disk_stats.file_buff_writes;
    show_disk_stats.file_direct_reads = disk_stats.file_direct_reads - running_disk_stats.file_direct_reads;
    show_disk_stats.file_direct_blocks_read = disk_stats.file_direct_blocks_read - running_disk_stats.file_direct_blocks_read;
    show_disk_stats.file_direct_writes = disk_stats.file_direct_writes - running_disk_stats.file_direct_writes;
    show_disk_stats.file_direct_blocks_written = disk_stats.file_direct_blocks_written - running_disk_stats.file_direct_blocks_written;
    }
    running_disk_stats = disk_stats;

#if (INCLUDE_FAILSAFE_CODE)
    rtfs_memset(&show_failsafe_stats, 0, sizeof(show_failsafe_stats));
    rtfs_memset(&failsafe_stats, 0, sizeof(failsafe_stats));
    if (pc_diskio_failsafe_stats((byte *)"", &failsafe_stats))
    {
    show_failsafe_stats.restore_data_reads = failsafe_stats.restore_data_reads - running_failsafe_stats.restore_data_reads;
    show_failsafe_stats.restore_data_blocks_read = failsafe_stats.restore_data_blocks_read - running_failsafe_stats.restore_data_blocks_read;
    show_failsafe_stats.journal_data_reads = failsafe_stats.journal_data_reads - running_failsafe_stats.journal_data_reads;
    show_failsafe_stats.journal_data_blocks_read = failsafe_stats.journal_data_blocks_read - running_failsafe_stats.journal_data_blocks_read;
    show_failsafe_stats.journal_data_writes = failsafe_stats.journal_data_writes - running_failsafe_stats.journal_data_writes;
    show_failsafe_stats.journal_data_blocks_written = failsafe_stats.journal_data_blocks_written - running_failsafe_stats.journal_data_blocks_written;
    show_failsafe_stats.journal_index_writes = failsafe_stats.journal_index_writes - running_failsafe_stats.journal_index_writes;
    show_failsafe_stats.journal_index_reads = failsafe_stats.journal_index_reads - running_failsafe_stats.journal_index_reads;
    show_failsafe_stats.fat_synchronize_writes = failsafe_stats.fat_synchronize_writes - running_failsafe_stats.fat_synchronize_writes;
    show_failsafe_stats.fat_synchronize_blocks_written = failsafe_stats.fat_synchronize_blocks_written - running_failsafe_stats.fat_synchronize_blocks_written;
    show_failsafe_stats.dir_synchronize_writes = failsafe_stats.dir_synchronize_writes - running_failsafe_stats.dir_synchronize_writes;
    show_failsafe_stats.dir_synchronize_blocks_written = failsafe_stats.dir_synchronize_blocks_written - running_failsafe_stats.dir_synchronize_blocks_written;
    running_failsafe_stats = failsafe_stats;
    }
#endif
    rtfs_print_one_string((byte *)"  IO statistics for previous command ", PRFLG_NL);
    rtfs_print_one_string((byte *)"  ================================== ", PRFLG_NL);

#if (INCLUDE_DEBUG_RUNTIME_STATS)
    if (show_disk_stats.async_steps)
    {
        rtfs_print_one_string((byte *)"Async steps        : ", 0); rtfs_print_long_1(show_disk_stats.async_steps, PRFLG_NL);
    }
    if (show_disk_stats.fat_reads || show_disk_stats.fat_writes)
    {
        rtfs_print_one_string((byte *)"fat reads          : ", 0); rtfs_print_long_1(show_disk_stats.fat_reads, 0);
        rtfs_print_one_string((byte *)"  blocks read      : ", 0); rtfs_print_long_1(show_disk_stats.fat_blocks_read, PRFLG_NL);
        rtfs_print_one_string((byte *)"fat writes         : ", 0); rtfs_print_long_1(show_disk_stats.fat_writes, 0);
        rtfs_print_one_string((byte *)"  blocks written   : ", 0); rtfs_print_long_1(show_disk_stats.fat_blocks_written, PRFLG_NL);
    }
    if (show_disk_stats.dir_buff_hits || show_disk_stats.dir_buff_reads || show_disk_stats.dir_buff_writes)
    {
        rtfs_print_one_string((byte *)"dir buff_hits      : ", 0); rtfs_print_long_1(show_disk_stats.dir_buff_hits, 0);
        rtfs_print_one_string((byte *)"    buff_reads     : ", 0); rtfs_print_long_1(show_disk_stats.dir_buff_reads, 0);
        rtfs_print_one_string((byte *)"    buff writes    : ", 0); rtfs_print_long_1(show_disk_stats.dir_buff_writes, PRFLG_NL);
    }
    if (show_disk_stats.dir_direct_reads)
    {
        rtfs_print_one_string((byte *)"dir  direct reads  : ", 0); rtfs_print_long_1(show_disk_stats.dir_direct_reads, 0);
        rtfs_print_one_string((byte *)"     blocks read   : ", 0); rtfs_print_long_1(show_disk_stats.dir_direct_blocks_read, PRFLG_NL);
    }
    if (show_disk_stats.dir_direct_writes)
    {
        rtfs_print_one_string((byte *)"dir direct writes  : ", 0); rtfs_print_long_1(show_disk_stats.dir_direct_writes, 0);
        rtfs_print_one_string((byte *)"     blocks written: ", 0); rtfs_print_long_1(show_disk_stats.dir_direct_blocks_written, PRFLG_NL);
    }
    if (show_disk_stats.file_buff_hits || show_disk_stats.file_buff_reads || show_disk_stats.file_buff_writes)
    {
        rtfs_print_one_string((byte *)"file buff hits     : ", 0); rtfs_print_long_1(show_disk_stats.file_buff_hits, 0);
        rtfs_print_one_string((byte *)"     buff reads    : ", 0); rtfs_print_long_1(show_disk_stats.file_buff_reads, 0);
        rtfs_print_one_string((byte *)"     buff writes   : ", 0); rtfs_print_long_1(show_disk_stats.file_buff_writes, PRFLG_NL);
    }
    if (show_disk_stats.file_direct_reads)
    {
        rtfs_print_one_string((byte *)"file direct reads   : ", 0); rtfs_print_long_1(show_disk_stats.file_direct_reads, 0);
        rtfs_print_one_string((byte *)"     blocks read    : ", 0); rtfs_print_long_1(show_disk_stats.file_direct_blocks_read, PRFLG_NL);
    }
    if (show_disk_stats.file_direct_writes)
    {
        rtfs_print_one_string((byte *)"file direct writes  : ", 0); rtfs_print_long_1(show_disk_stats.file_direct_writes, 0);
        rtfs_print_one_string((byte *)"     blocks written : ", 0); rtfs_print_long_1(show_disk_stats.file_direct_blocks_written, PRFLG_NL);
    }
#if (INCLUDE_FAILSAFE_CODE)
    if (show_failsafe_stats.restore_data_reads)
    {
        rtfs_print_one_string((byte *)"restore data reads  : ", 0); rtfs_print_long_1(show_failsafe_stats.restore_data_reads, 0);
        rtfs_print_one_string((byte *)"     blocks read    : ", 0); rtfs_print_long_1(show_failsafe_stats.restore_data_blocks_read, PRFLG_NL);
    }
    if (show_failsafe_stats.fat_synchronize_writes)
    {
        rtfs_print_one_string((byte *)"FS FAT sync writes  : ", 0); rtfs_print_long_1(show_failsafe_stats.fat_synchronize_writes, 0);
        rtfs_print_one_string((byte *)"    blocks written  : ", 0); rtfs_print_long_1(show_failsafe_stats.fat_synchronize_blocks_written, PRFLG_NL);
    };
    if (show_failsafe_stats.dir_synchronize_writes)
    {
        rtfs_print_one_string((byte *)"FS BLOCK sync writes: ", 0); rtfs_print_long_1(show_failsafe_stats.dir_synchronize_writes, 0);
        rtfs_print_one_string((byte *)"    blocks written  : ", 0); rtfs_print_long_1(show_failsafe_stats.dir_synchronize_blocks_written, PRFLG_NL);
    };
    if (show_failsafe_stats.journal_data_reads)
    {
        rtfs_print_one_string((byte *)"journal data reads : ", 0); rtfs_print_long_1(show_failsafe_stats.journal_data_reads, 0);
        rtfs_print_one_string((byte *)"     blocks read   : ", 0); rtfs_print_long_1(show_failsafe_stats.journal_data_blocks_read, PRFLG_NL);
    }
    if (show_failsafe_stats.journal_data_writes)
    {
        rtfs_print_one_string((byte *)"journal data writes : ", 0); rtfs_print_long_1(show_failsafe_stats.journal_data_writes, 0);
        rtfs_print_one_string((byte *)"     blocks written : ", 0); rtfs_print_long_1(show_failsafe_stats.journal_data_blocks_written, PRFLG_NL);
    }
    if (show_failsafe_stats.journal_index_writes || show_failsafe_stats.journal_index_reads)
    {
        rtfs_print_one_string((byte *)"journal index writes : ", 0); rtfs_print_long_1(show_failsafe_stats.journal_index_writes, 0);
        rtfs_print_one_string((byte *)"journal index reads  : ", 0); rtfs_print_long_1(show_failsafe_stats.journal_index_reads, PRFLG_NL);
    }
#endif
#endif
    rtfs_print_one_string((byte *)"Elapsed time       : ", 0); rtfs_print_long_1(rtfs_port_elapsed_zero()-start_time, 0);
    rtfs_print_one_string((byte *)" Milliseconds", PRFLG_NL);
    start_time = rtfs_port_elapsed_zero();
    running_disk_stats = disk_stats;
}




/* Given a fd. return a RNDFILE record with matching fd */
RNDFILE *fndrnd( int fd)                                       /*__fn__*/
{
int i;

    for (i = 0 ; i < MAXRND; i++)
    {
        if (fd == rnds[i].fd)
            return (&rnds[i]);
    }
    return (0);
}

#define LINES_PER_SCREEN 22
int dohelp_txt(DISPATCHER_TEXT *pcmds,int agc, byte **agv)                             /*__fn__*/
{
byte buf[10];
byte display_buf[120];
int i, do_print, do_seperator, helpstr_len, nprinted, buffpos,first_column_length;

    use_args(agc, agv);

    rtfs_memset(display_buf, ' ', 80);
    display_buf[80] = 0;
    for (i = 0; i < LINES_PER_SCREEN; i++)
    {
        RTFS_PRINT_STRING_1(display_buf, PRFLG_NL);
    }
    first_column_length=do_print = buffpos = nprinted = 0;
	while (pcmds->cmd)
    {

        do_seperator = do_print = 0;
        helpstr_len = rtfs_cs_strlen(pcmds->helpstr, CS_CHARSET_NOT_UNICODE);
        if (*pcmds->cmd == '-')
        {
            do_seperator = 1;
            if (buffpos != 0)
                do_print = 1;
        }
        else if (buffpos == 0)
        {
            rtfs_memset(display_buf, ' ', 80);
            rtfs_cs_strcpy(&display_buf[0], pcmds->helpstr, CS_CHARSET_NOT_UNICODE);
            if (helpstr_len < 40)
            {
                first_column_length = helpstr_len;
                buffpos = 40;
            }
            else
                do_print = 1;
            pcmds++;
        }
        else
        {
            do_print = 1;
            if (helpstr_len < 40)
            {
                display_buf[first_column_length] = ' ';
                display_buf[38] = '|';
                rtfs_cs_strcpy(&display_buf[40], pcmds->helpstr, CS_CHARSET_NOT_UNICODE);
                pcmds++;
            }
            buffpos = 0;
            first_column_length = 0;
        }
        if (do_print)
        {
            RTFS_PRINT_STRING_1(display_buf, PRFLG_NL);
            if (nprinted++ > LINES_PER_SCREEN)
            {
                rtfs_print_prompt_user((byte *)"Press return", buf);
                nprinted = 0;
            }
            do_print = 0;
        }
        if (do_seperator)
        {
            if (nprinted++ > (LINES_PER_SCREEN-2))
            {
                rtfs_print_prompt_user((byte *)"Press return", buf);
                nprinted = 0;
            }
            RTFS_PRINT_STRING_1(pcmds->helpstr, PRFLG_NL);
            do_seperator = 0;
            buffpos = 0;
            pcmds++;
        }
    }
    if (buffpos || do_print)
        RTFS_PRINT_STRING_1(display_buf, PRFLG_NL);
    return(0);
}

int donull(int agc, byte **agv)                             /*__fn__*/
{

    use_args(agc, agv);
    return(0);
}

int dohelp(int agc, byte **agv)                             /*__fn__*/
{

    use_args(agc, agv);
    return(dohelp_txt(&cmds[0], agc, agv));
}

/* EJECT D: */
RTFS_DEVI_MEDIA_PARMS *saved_media_parms[26];
#if (LEAKCHECK_ON_RESET)
/* In pc simulation environment use statics so we can leak check the system on reset */
RTFS_DEVI_MEDIA_PARMS _saved_media_parms[26];
#endif
/* Implements EJECT command for BLK_DEV devices */
static int eject_driveno(int driveno)
{
    DDRIVE *pdr;
	/* Save a copy of the media parameters and simulate a device driver eject alert. This will release the drive structure and
	   media parameter structucure. uneject_driveno(int driveno) see below, will use the saved  media parameter block to
	   renerate an rtfs_media_insert_args structure and call pc_rtfs_media_insert(&rtfs_insert_parms) to simulate a media insert
	   call from the devie driver */

    pdr = pc_drno_to_drive_struct(driveno);

    if (pdr)
    {
#if (LEAKCHECK_ON_RESET)
	/* In pc simulation environment use statics so we can leak check the system on reset */
		saved_media_parms[driveno] = &_saved_media_parms[driveno];
#else
		saved_media_parms[driveno] = (RTFS_DEVI_MEDIA_PARMS *) rtfs_port_malloc(sizeof(RTFS_DEVI_MEDIA_PARMS));
#endif
		if (saved_media_parms[driveno])
		{
			*saved_media_parms[driveno] = *pdr->pmedia_info;
			pc_rtfs_media_alert(pdr->pmedia_info->devhandle, RTFS_ALERT_EJECT, 0);
			return(1);
		}
    }
	return(0);
}

static void uneject_driveno(int driveno)
{
	struct rtfs_media_insert_args rtfs_insert_parms;
	if (saved_media_parms[driveno])
	{
		pc_rtfs_regen_insert_parms(&rtfs_insert_parms, saved_media_parms[driveno]);
#if (LEAKCHECK_ON_RESET)
/* In pc simulation environment use statics so we can leak check the system on reset */
#else
		rtfs_port_free(saved_media_parms[driveno]);
#endif
		saved_media_parms[driveno] = 0;
   		if (pc_rtfs_media_insert(&rtfs_insert_parms) < 0)
			rtfs_print_one_string((byte *)"  Device Insert simulation failed ", PRFLG_NL);
		else
			rtfs_print_one_string((byte *)"  Device Insert simulation succeeded", PRFLG_NL);
	}
}


int dobreak(int agc, byte **agv)
{
    use_args(agc, agv);
    RTFS_PRINT_STRING_1((byte *)"Sleeping 8 seconds operate the debugger now",PRFLG_NL);
    rtfs_port_sleep(8000);
    return(0);
}

void eject_drivename(byte *drivename)
{
    int driveno;
    driveno = pc_parse_raw_drive(drivename, CS_CHARSET_NOT_UNICODE);
    if (driveno != -1)
        if (eject_driveno(driveno))
			rtfs_print_one_string((byte *)"  Device Ejected Call eject again to re-insert ", PRFLG_NL);
		else
			rtfs_print_one_string((byte *)"  Device Ejected did not succeed ", PRFLG_NL);
}


/*
<TEST>  Procedure:   doeject() - Call a device driver to report device removal.
<TEST>   Invoke by typing "EJECT" in the extended command shell
*/
int doeject(int agc, byte **agv)                                   /*__fn__*/
{
    if (parse_args(agc, agv,"T"))
    {
    	{
    		int driveno;
    		driveno = pc_path_to_driveno(rtfs_args_val_text(0), CS_CHARSET_NOT_UNICODE);
    		if (driveno >= 0 && saved_media_parms[driveno])
    		{
            	uneject_driveno(driveno);
    			return(0);
    		}
    	}
        if (!pc_set_default_drive(rtfs_args_val_text(0)))
        {
            RTFS_PRINT_STRING_1((byte *)"Set Default Drive Failed",PRFLG_NL);
            return(-1);
        }
        else
        {
            eject_drivename(rtfs_args_val_text(0));
        }
    }
    else
    {
        RTFS_PRINT_STRING_1((byte *)"Usage: EJECT D: ", PRFLG_NL);
    }
    return(0);
}

/* DSKSEL PATH */
int doselect(int agc, byte **agv)                                   /*__fn__*/
{
    if (parse_args(agc, agv,"T"))
    {
        /* Set default statistics for measuring info */
        pc_diskio_runtime_stats((byte *)rtfs_args_val_text(0), &running_disk_stats, FALSE);
        if (!pc_set_default_drive(rtfs_args_val_text(0)))
        {
            DISPLAY_ERRNO("pc_set_default_drive")
            RTFS_PRINT_STRING_1((byte *)"Set Default Drive Failed",PRFLG_NL);
            return(-1);
        }

        return(0);
    }
    else
    {
         RTFS_PRINT_STRING_1((byte *)"Usage: DSKSELECT D: ", PRFLG_NL);
         return(-1);
    }
}

#if (INCLUDE_CS_UNICODE)
/*
<TEST>  Procedure:   dounicode() - Force the command shell to convert names to Unicode and call unicode versions of functions
<TEST>   Invoke by typing "UNICODE" in the extended command shell
*/
static int dounicode(int agc, byte **agv)
{
    if (parse_args(agc, agv,"B"))
    {
        unicode_enabled = (BOOLEAN) rtfs_args_val_lo(0);
    }
    if (unicode_enabled)
        {RTFS_PRINT_STRING_1((byte *)"Unicode enabled ", PRFLG_NL);}
    else
        {RTFS_PRINT_STRING_1((byte *)"Unicode disabled ", PRFLG_NL);}
    return(0);
}
#endif

/* RNDOP PATH RECLEN */
int rndop(int agc, byte **agv)                                  /*__fn__*/
{
    RNDFILE *rndf;

    if (parse_args(agc, agv,"TI"))
    {
        rndf = fndrnd(-1);
        if (!rndf)
            {RTFS_PRINT_STRING_1((byte *)"No more random file slots ", PRFLG_NL);}
        else
        {
            rtfs_cs_strcpy(rndf->name, rtfs_args_val_text(0), CS_CHARSET_NOT_UNICODE);
            rndf->reclen  = (int)rtfs_args_val_lo(1);
            if (rndf->reclen <= 0 || rndf->reclen > RNDFUFFSIZE)
                goto usage;
            if ((rndf->fd = po_open(rndf->name, (PO_BINARY|PO_RDWR|PO_CREAT),
                           (PS_IWRITE | PS_IREAD) ) ) < 0)
            {
                DISPLAY_ERRNO("pc_open(PO_BINARY|PO_RDWR|PO_CREAT)")
                 RTFS_PRINT_STRING_2((byte *)"Cant open : ",rndf->name,PRFLG_NL);
                 /* Note: rndf->fd is still -1 on error */
            }
            else
            {
                dolstop(0,0);   /* Print */
                return (0);
            }
       }
  }
usage:
  RTFS_PRINT_STRING_1((byte *)"Usage: RNDOP D:PATH RECLEN (0 < reclen <= RNDFUFFSIZE)",PRFLG_NL);
  return (-1);
}


/* CLOSE fd */
int doclose(int agc, byte **agv)                                /*__fn__*/
{
    int fd;
    RNDFILE *rndf;

    if (parse_args(agc, agv,"I"))
    {
        fd = (int)rtfs_args_val_lo(0);
        rndf = fndrnd( fd );
        if (!rndf)
            {RTFS_PRINT_STRING_1((byte *)"Cant find file",PRFLG_NL);}
        else
        {
            if (po_close(fd) < 0)
            {
                DISPLAY_ERRNO("po_close")
                RTFS_PRINT_STRING_1((byte *)"Close failed",PRFLG_NL);
            }
            else
             {
                rndf->fd = -1;
                return (0);
             }
        }
    }
    RTFS_PRINT_STRING_1((byte *)"Usage: CLOSE fd",PRFLG_NL);
    return (-1);
}

/* SEEK fd recordno */
int doseek(int agc, byte **agv)                                 /*__fn__*/
{
    int fd;
    RNDFILE *rndf;
    int recno;
    long foff;

    if (parse_args(agc, agv,"II"))
    {
        fd = (int)rtfs_args_val_lo(0);
        rndf = fndrnd(fd);
        if (!rndf)
            {RTFS_PRINT_STRING_1((byte *)"Cant find file",PRFLG_NL);}
        else
        {
            recno =  (int)rtfs_args_val_lo(1);
            foff = (long) recno * rndf->reclen;

            if (foff !=  po_lseek(fd, foff, PSEEK_SET ) )
            {
                 DISPLAY_ERRNO("po_lseek")
                 RTFS_PRINT_STRING_1((byte *)"Seek operation failed ", PRFLG_NL); /* "Seek operation failed " */
            }
            else
                return (0);
        }
    }
    RTFS_PRINT_STRING_1((byte *)"Usage: SEEK fd recordno",PRFLG_NL); /* "Usage: SEEK fd recordno" */
    return (-1);
}

/* READ fd */
int doread(int agc, byte **agv)                                     /*__fn__*/
{
    int fd;
    RNDFILE *rndf;

    if (parse_args(agc, agv,"I"))
    {
        fd = (int)rtfs_args_val_lo(0);
        rndf = fndrnd(  fd );
        if (!rndf)
            {RTFS_PRINT_STRING_1((byte *)"Cant find file",PRFLG_NL);} /* "Cant find file" */
        else
        {
            if ( po_read(fd,(byte*)rndf->buff,(word)rndf->reclen) != rndf->reclen)
            {
                DISPLAY_ERRNO("po_read")
                RTFS_PRINT_STRING_1((byte *)"Read operation failed ", PRFLG_NL); /* "Read operation failed " */
            }
            else
            {
                RTFS_PRINT_STRING_1((byte *)rndf->buff,PRFLG_NL);
                return (0);
            }
        }
    }
    RTFS_PRINT_STRING_1((byte *)"Usage: READ fd",PRFLG_NL); /* "Usage: READ fd" */
    return (-1);
}


/* LSTOPEN */
int dolstop(int agc, byte **agv)                                /*__fn__*/
{
    int i;

    use_args(agc, agv);

    RTFS_PRINT_STRING_1((byte *)"FD     File NAME", PRFLG_NL);
    RTFS_PRINT_STRING_1((byte *)"--     ---------", PRFLG_NL);
    for (i = 0 ; i < MAXRND; i++)
    {
        if (rnds[i].fd != -1)
        {
            RTFS_PRINT_LONG_1((dword)rnds[i].fd, 0);
            RTFS_PRINT_STRING_1((byte *)"       ", 0);
            RTFS_PRINT_STRING_1((byte *)rnds[i].name, PRFLG_NL);
        }
    }
    return (0);
}




/* CD PATH */
int docwd(int agc, byte **agv)                                      /*__fn__*/
{
    byte lbuff[EMAXPATH_BYTES];


    if (parse_args(agc, agv,"T"))
    {
#if (INCLUDE_CS_UNICODE)
        if (unicode_enabled)
        {
            if (pc_set_cwd_uc(rtfs_args_val_utext(0, 0)))
                return (0);
        }
#endif
        if (!unicode_enabled)
            if (pc_set_cwd(rtfs_args_val_text(0)))
                return(0);
        DISPLAY_ERRNO("pc_set_cwd")
        RTFS_PRINT_STRING_1((byte *)"Set cwd failed",PRFLG_NL); /* "Set cwd failed" */
        return(-1);
    }
    else
    {
        byte null_filename[2];
        null_filename[0] = null_filename[1] = 0;
#if (INCLUDE_CS_UNICODE)
        if (unicode_enabled)
        { /* Do unicode then ascii, print ascii version */
            pc_pwd_uc(null_filename,lbuff);
        }
#endif
        if (pc_pwd(null_filename,lbuff))
        {
            RTFS_PRINT_STRING_1((byte *)lbuff,PRFLG_NL);
            return(0);
        }
        else
        {
            DISPLAY_ERRNO("get_cwd")

            RTFS_PRINT_STRING_1((byte *)"Get cwd failed",PRFLG_NL); /* "Get cwd failed" */
            return(-1);
        }
    }
}



/* DIR PATH */
/*
<TEST>  Procedure:   dols() - Perform a directory enumeration and display file information
<TEST>   Invoke by typing "DIR" in the command shell
<TEST>
*/
static int _dols(int agc, byte **agv, BOOLEAN backwards);

int dols(int agc, byte **agv)
{
  return (_dols(agc,agv, FALSE));
}
int dorls(int agc, byte **agv)
{
  return (_dols(agc,agv, TRUE));
}

static int _dols(int agc, byte **agv, BOOLEAN backwards)
{
    int fcount;
    int doit, addwild, use_charset;
    byte *ppath,*p;
    dword blocks_total, blocks_free;
    byte null_str[2];
    BOOLEAN btemp = FALSE;
    ppath = 0;

    addwild = 0;
    use_charset = CS_CHARSET_NOT_UNICODE;
#if (INCLUDE_CS_UNICODE)
     if (unicode_enabled)
        use_charset = CS_CHARSET_UNICODE;
#endif
    if (parse_args(agc, agv,"T"))
    {
        ppath = rtfs_args_val_text(0);
#if (INCLUDE_CS_UNICODE)
        if (unicode_enabled)
            ppath = rtfs_args_val_utext(0, 0);
#endif
        p = ppath;
        /* If the second char is ':' and the third is '\0' then we have
           D: and we will convert to D:*.* */
        CS_OP_INC_PTR(p, use_charset );
        if (CS_OP_CMP_ASCII(p,':', use_charset))
        {
            CS_OP_INC_PTR(p, use_charset );
            if (CS_OP_IS_EOS(p, use_charset ))
                addwild = 1;
        }
    }
    else
    {
        null_str[0] = null_str[1] = 0; /* Works for all char sets */
        /* get the working dir of the default dir */
#if (INCLUDE_CS_UNICODE)
        if (unicode_enabled)
          btemp = pc_pwd_uc(null_str,(byte *)shell_buf);
#endif
        if (!unicode_enabled)
            btemp = pc_pwd(null_str,(byte *)shell_buf);
        if (!btemp)
        {
            RTFS_PRINT_STRING_1((byte *)"PWD Failed ",PRFLG_NL); /* "PWD Failed " */
            return(-1);
        }
        ppath = (byte *)shell_buf;
        p = ppath;

        /* If not the root add a \\ */
        doit = 1; /* Add \\ if true */
        if (CS_OP_CMP_ASCII(p,'\\', use_charset))
        {
            CS_OP_INC_PTR(p, use_charset );
            if (CS_OP_IS_EOS(p, use_charset ))
                doit = 0;
        }
        if (doit)
        {
            p = CS_OP_GOTO_EOS(p, use_charset );
            CS_OP_ASSIGN_ASCII(p,'\\', use_charset);
            CS_OP_INC_PTR(p, use_charset);
            CS_OP_TERM_STRING(p, use_charset);
        }
        addwild = 1;
    }
    if (addwild)
    {
        p = ppath;
        p = CS_OP_GOTO_EOS(p, use_charset);
        /* Now tack on *.* */
        CS_OP_ASSIGN_ASCII(p,'*', use_charset);
        CS_OP_INC_PTR(p, use_charset);
        /* Add .* for non vfat */
        CS_OP_ASSIGN_ASCII(p,'.', use_charset);
        CS_OP_INC_PTR(p, use_charset);
        CS_OP_ASSIGN_ASCII(p,'*', use_charset);
        CS_OP_INC_PTR(p, use_charset);
        CS_OP_TERM_STRING(p, use_charset);
    }


    /* Now do the dir */
    fcount = pc_seedir(ppath, backwards);

    /* And print the bytes remaining */
    if (pc_blocks_free(ppath, &blocks_total, &blocks_free))
    {
        RTFS_PRINT_STRING_1((byte *)"       ", 0); /* "       " */
        RTFS_PRINT_LONG_1((dword) fcount, 0);
        RTFS_PRINT_STRING_1((byte *)" File(s) ", 0); /* " File(s) " */
        RTFS_PRINT_LONG_1((dword) blocks_free, 0);
        RTFS_PRINT_STRING_1((byte *)" Blocks Free ", PRFLG_NL); /* " Blocks Free " */
    }
    return(0);
}

/* List directory, and return number of matching file */
static int pc_seedir(byte *path, BOOLEAN backwards)
{
    int fcount = 0;
    DSTAT statobj;
    byte display_buffer[100];
    BOOLEAN btemp = FALSE;
#if (INCLUDE_REVERSEDIR)
	if (backwards)
	{
    /* Get the first match */
#if (INCLUDE_CS_UNICODE)
    	if (unicode_enabled)
    	{
        	rtfs_print_one_string((byte *)" Can not display Unicode filenames ", PRFLG_NL);
        	btemp = pc_glast_uc(&statobj, path);
    	}
#endif
    	if (!unicode_enabled)
        	btemp = pc_glast(&statobj, path);
	}
 	else
#endif
	{
    /* Get the first match */
#if (INCLUDE_CS_UNICODE)
    	if (unicode_enabled)
    	{
        	rtfs_print_one_string((byte *)" Can not display Unicode filenames ", PRFLG_NL);
        	btemp = pc_gfirst_uc(&statobj, path);
    	}
#endif
    	if (!unicode_enabled)
        	btemp = pc_gfirst(&statobj, path);
	}

    if (btemp)
    {
        for (;;)
        {
            fcount++;
            if (!unicode_enabled)
                rtfs_print_format_dir(display_buffer, &statobj);
#if (INCLUDE_REVERSEDIR)
             /* Get the next */
             if (backwards)
			 {
#if (INCLUDE_CS_UNICODE)
            	if (unicode_enabled)
                	btemp = pc_gprev_uc(&statobj);
#endif
            	if (!unicode_enabled)
                	btemp = pc_gprev(&statobj);

			 }
			 else
#endif
			 {
#if (INCLUDE_CS_UNICODE)
            	if (unicode_enabled)
                	btemp = pc_gnext_uc(&statobj);
#endif
            	if (!unicode_enabled)
                	btemp = pc_gnext(&statobj);
			}
            if (!btemp)
                break;
        }
        DISPLAY_ERRNO("pc_next")
        /* Call gdone to free up internal resources used by statobj */
        pc_gdone(&statobj);
    }
    else
    {
        DISPLAY_ERRNO("pc_gfirst")
    }
    return(fcount);
}


int enum_callback(byte *path, DSTAT *d)
{
    RTFS_ARGSUSED_PVOID((void *)d);
#if (INCLUDE_CS_UNICODE)
    if (unicode_enabled)
    { /* print the alias because that is ascii */
        RTFS_PRINT_STRING_1((byte *)d->filename, PRFLG_NL);
    }
    else
    {
        RTFS_PRINT_STRING_1((byte *)path, PRFLG_NL);
    }
#else
    RTFS_PRINT_STRING_1((byte *)path, PRFLG_NL);
#endif
    return(0);
}

/*
<TEST>  Procedure:   doenum() - Perform a recursive directory enumeration using pc_enumerate() and display file information
<TEST>   Invoke by typing "ENUM" in the command shell
<TEST>
*/
int doenum(int agc, byte **agv)
{
    byte *ppath,*pmatch;
    byte buf[16];
    BLKBUFF *scratch_buffer1;
    BLKBUFF *scratch_buffer2;
    BLKBUFF *scratch_buffer3;
    BLKBUFF *scratch_buffer4;
    word match_flags;

    if (!parse_args(agc, agv,"TT"))
    {
        RTFS_PRINT_STRING_1((byte *)"Usage:  ENUMDIR PATH PATTERN",PRFLG_NL); /* "Usage:  ENUMDIR PATH PATTERN" */
        return(-1);
    }

    ppath = rtfs_args_val_text(0);
    pmatch = rtfs_args_val_text(1);
#if (INCLUDE_CS_UNICODE)
    if (unicode_enabled)
    {
       ppath = rtfs_args_val_utext(0, 0);
       pmatch = rtfs_args_val_utext(1, 1);
    }
#endif

    scratch_buffer1 = pc_scratch_blk();
    scratch_buffer2 = pc_scratch_blk();
    scratch_buffer3 = pc_scratch_blk();
    scratch_buffer4 = pc_scratch_blk();

    if (!(scratch_buffer1 && scratch_buffer2 && scratch_buffer3 && scratch_buffer4))
    {
        RTFS_PRINT_STRING_1((byte *)" ENUM Could not alloc scratch buffers",PRFLG_NL); /* " ENUM Could not alloc scratch buffers" */
        goto done;
    }
    match_flags = 0;
    rtfs_print_prompt_user((byte *)"Show directories(Y/N) ", buf); /* "Show directories(Y/N) " */
    if (tstsh_is_yes(buf))
        match_flags |= MATCH_DIR;
    rtfs_print_prompt_user((byte *)"Show files(Y/N) ", buf); /* "Show files(Y/N) " */
    if (tstsh_is_yes(buf))
        match_flags |= MATCH_FILES;
    rtfs_print_prompt_user((byte *)"Show volume labels (Y/N) ", buf); /* "Show volume labels (Y/N) " */
    if (tstsh_is_yes(buf))
        match_flags |= MATCH_VOL;
    if (match_flags & MATCH_DIR)
    {
        rtfs_print_prompt_user((byte *)"Show .. (Y/N) ", buf); /* "Show .. (Y/N) "*/
        if (tstsh_is_yes(buf))
            match_flags |= MATCH_DOT;
        rtfs_print_prompt_user((byte *)"Show . (Y/N) ", buf);/* "Show . (Y/N) " */
        if (tstsh_is_yes(buf))
            match_flags |= MATCH_DOTDOT;
    }
#if (INCLUDE_CS_UNICODE)
     if (unicode_enabled)
	    pc_enumerate_uc(
		    (byte *)scratch_buffer1->data,
			(byte *)scratch_buffer2->data,
			(byte *)scratch_buffer3->data,
			(byte *)scratch_buffer4->data,
			ppath,
			match_flags,
			pmatch,
			128,
			enum_callback);
#endif
     if (!unicode_enabled)
	    pc_enumerate(
		    (byte *)scratch_buffer1->data,
			(byte *)scratch_buffer2->data,
			(byte *)scratch_buffer3->data,
			(byte *)scratch_buffer4->data,
			ppath,
			match_flags,
			pmatch,
			128,
			enum_callback);
done:
    if (scratch_buffer1)
        pc_free_scratch_blk(scratch_buffer1);
    if (scratch_buffer2)
        pc_free_scratch_blk(scratch_buffer2);
    if (scratch_buffer3)
        pc_free_scratch_blk(scratch_buffer3);
    if (scratch_buffer4)
        pc_free_scratch_blk(scratch_buffer4);
    return(0);

}

/*
<TEST>  Procedure:   docat() - Display the contents of a named file
<TEST>   Invoke by typing "CAT" in the command shell
<TEST>
*/
/* CAT PATH */
int docat(int agc, byte **agv)                                  /*__fn__*/
{
    int fd = -1;
    int nread;

    if (parse_args(agc, agv,"T"))
    {
#if (INCLUDE_CS_UNICODE)
        if (unicode_enabled)
            fd = po_open_uc(rtfs_args_val_utext(0, 0), (word)(PO_BINARY|PO_RDONLY|PO_BUFFERED),(word) (PS_IWRITE | PS_IREAD) );
#endif
        if (!unicode_enabled)
            fd = po_open(rtfs_args_val_text(0), (word)(PO_BINARY|PO_RDONLY|PO_BUFFERED),(word) (PS_IWRITE | PS_IREAD) );

        if (fd < 0)
        {
            DISPLAY_ERRNO("po_open")
            RTFS_PRINT_STRING_2((byte *)"Cant open ", rtfs_args_val_text(0),PRFLG_NL); /* "Cant open " */
            return(-1);
        }
        else
        {
            do
            {

                nread = po_read(fd,(byte*)shell_buf,1);
                if (nread > 0)
                {
                    shell_buf[1] = '\0';
                    RTFS_PRINT_STRING_1((byte *)shell_buf,0);
                }
                if (nread < 0)
                {
                    DISPLAY_ERRNO("po_read")
                }
            }while(nread > 0);
            if (po_close(fd) != 0)
            {
                DISPLAY_ERRNO("po_read")
            }
            return(0);
        }
    }
    else
    {
        RTFS_PRINT_STRING_1((byte *)"Usage: CAT PATH",PRFLG_NL); /* "Usage: CAT PATH" */
        return(-1);
    }
}

/*
<TEST>  Procedure:   dodiff() - Compare the contents of a named files
<TEST>   Invoke by typing "DIFF" in the command shell
<TEST>
*/
/* DIFF PATH PATH */
#if (defined(RTFS_WINDOWS) || defined(RTFS_LINUX))
#define DIFFBUFFSIZE 131072
#else
#define DIFFBUFFSIZE 512
#endif
int dodiff(int agc, byte **agv)                                  /*__fn__*/
{
    int in_fd;
    int in_fd1;
    int nread;
    int nread1;
    int i;
	dword nloops = 0;
	#define READ_METADATA_ONLY 0

#if (READ_METADATA_ONLY)
	byte *buff = 0;
	byte *buff1 = 0;

#else
	byte _buff[DIFFBUFFSIZE];
	byte _buff1[DIFFBUFFSIZE];
	byte *buff = _buff;
	byte *buff1 = _buff1;
#endif

    if (parse_args(agc, agv,"TT"))
    {
        if ((in_fd = po_open(rtfs_args_val_text(0), (PO_BINARY|PO_RDONLY), (PS_IWRITE | PS_IREAD) ) ) < 0)
        {
            DISPLAY_ERRNO("po_open")
            RTFS_PRINT_STRING_2((byte *)"Cant open file: ", rtfs_args_val_text(0),PRFLG_NL); /* "Cant open file: " */
            return(-1);
        }
        if ((in_fd1 = po_open(rtfs_args_val_text(1), (PO_BINARY|PO_RDONLY), (PS_IWRITE | PS_IREAD) ) ) < 0)
        {
            DISPLAY_ERRNO("po_open")
            RTFS_PRINT_STRING_2((byte *)"Cant open file", rtfs_args_val_text(1),PRFLG_NL); /* "Cant open file" */
            return(-1);
        }
        for (;;)
        {
            nread = po_read(in_fd,(byte*)buff,DIFFBUFFSIZE);
            if (nread > 0)
            {
                nread1 = po_read(in_fd1,(byte*)buff1,nread);
				nloops += 1;
                if (nread1 != nread)
                {
                    if (nread1 < 0)
                    {
                        DISPLAY_ERRNO("po_read")
                    }
difffail:
                    RTFS_PRINT_STRING_1((byte *)"Files are different after nloops =: " ,0);     /* "Files are different" */
                    RTFS_PRINT_LONG_1(nloops,PRFLG_NL);
                    po_close (in_fd);
                    po_close (in_fd);
                    return(0);
                }
				if (buff)
				{
					for(i = 0; i < nread; i++)
					{
						if (buff[i] != buff1[i])
							goto difffail;
					}
				}
            }
            else
            {
                if (nread < 0)
                {
                    DISPLAY_ERRNO("po_read")
                }
                break;
            }
        }
        nread1 = po_read(in_fd1,(byte*)buff1,(word)nread);
        if (nread1 <= 0)
        {
                if (nread1 < 0)
                {
                    DISPLAY_ERRNO("po_read")
                }
                RTFS_PRINT_STRING_2((byte *)"File: ",rtfs_args_val_text(0),0); /* "File: " */
                RTFS_PRINT_STRING_2((byte *)"And File: ",rtfs_args_val_text(1),0); /* " And File: " */
                RTFS_PRINT_STRING_1((byte *)" are the same",PRFLG_NL); /* " are the same" */
        }
        else
        {
            {
                RTFS_PRINT_STRING_2((byte *)"File: ", rtfs_args_val_text(1), 0); /* "File: " */
                RTFS_PRINT_STRING_2((byte *)" is larger than File: ", rtfs_args_val_text(0), PRFLG_NL); /* " is larger than File: " */
            }
            goto difffail;
        }
        po_close(in_fd);
        po_close(in_fd1);
        return(0);
   }
   else
   {
       RTFS_PRINT_STRING_1((byte *)"Usage: DIFF PATH PATH ", PRFLG_NL); /* "Usage: DIFF PATH PATH " */
   }
    return (0);
}

/*
<TEST>  Procedure:   dostat() - Call pc_stat() and display information about named file
<TEST>   Invoke by typing "STAT" in the command shell
<TEST>
*/
/* STAT PATH */
int dostat(int agc, byte **agv)                                    /*__fn__*/
{
ERTFS_STAT st;
byte display_buffer[256];
int stat_val = 0;
    if (parse_args(agc, agv,"T"))
    {
#if (INCLUDE_CS_UNICODE)
        if (unicode_enabled)
            stat_val = pc_stat_uc(rtfs_args_val_utext(0,0), &st);
#endif
        if (!unicode_enabled)
            stat_val = pc_stat(rtfs_args_val_text(0), &st);
        if (stat_val==0)
        {
            rtfs_print_format_stat(display_buffer, &st);
            RTFS_PRINT_STRING_1((byte *)"MODE BITS :", 0); /* "MODE BITS :" */
            if (st.st_mode&S_IFDIR)
                RTFS_PRINT_STRING_1((byte *)"S_IFDIR|", 0); /* "S_IFDIR|" */
            if (st.st_mode&S_IFREG)
                RTFS_PRINT_STRING_1((byte *)"S_IFREG|", 0); /* "S_IFREG|" */
            if (st.st_mode&S_IWRITE)
                RTFS_PRINT_STRING_1((byte *)"S_IWRITE|", 0); /* "S_IWRITE|" */
            if (st.st_mode&S_IREAD)
                RTFS_PRINT_STRING_1((byte *)"S_IREAD", 0); /* "S_IREAD" */
            RTFS_PRINT_STRING_1((byte *)"", PRFLG_NL); /* "" */
            return (0);
        }
        else
        {
            DISPLAY_ERRNO("pc_stat")
            RTFS_PRINT_STRING_1((byte *)"FSTAT failed", PRFLG_NL); /* "FSTAT failed" */
            return(0);
        }
    }
    RTFS_PRINT_STRING_1((byte *)"Usage: FSTAT D:PATH", PRFLG_NL); /* "Usage: FSTAT D:PATH" */
    return (-1);
}


/* GETATTR PATH*/
int dogetattr(int agc, byte **agv)                                    /*__fn__*/
{
byte attr;

    if (parse_args(agc, agv,"T"))
    {
        if (pc_get_attributes(rtfs_args_val_text(0), &attr))
        {
            RTFS_PRINT_STRING_1((byte *)"Attributes: ",0); /* "Attributes: " */
            if (attr & ARDONLY)
                RTFS_PRINT_STRING_1((byte *)"ARDONLY|",0); /* "ARDONLY|" */
            if (attr & AHIDDEN)
                RTFS_PRINT_STRING_1((byte *)"AHIDDEN|",0); /* "AHIDDEN|" */
            if (attr & ASYSTEM)
                RTFS_PRINT_STRING_1((byte *)"ASYSTEM|",0); /* "ASYSTEM|" */
            if (attr & AVOLUME)

                RTFS_PRINT_STRING_1((byte *)"AVOLUME|",0); /* "AVOLUME|" */
            if (attr & ADIRENT)
                RTFS_PRINT_STRING_1((byte *)"ADIRENT|",0); /* "ADIRENT|" */
            if (attr & ARCHIVE)
                RTFS_PRINT_STRING_1((byte *)"ARCHIVE|",0); /* "ARCHIVE|" */
            if (attr == ANORMAL)
                RTFS_PRINT_STRING_1((byte *)"NORMAL FILE (No bits set)",0); /* "NORMAL FILE (No bits set)" */
            RTFS_PRINT_STRING_1((byte *)"", PRFLG_NL); /* "" */
            return(0);

        }
        else
        {
            DISPLAY_ERRNO("pc_get_attributes")
            RTFS_PRINT_STRING_1((byte *)"get attributes failed", PRFLG_NL); /* "get attributes failed" */
            return(-1);
        }
    }
    RTFS_PRINT_STRING_1((byte *)"Usage: GETATTR D:PATH", PRFLG_NL); /* "Usage: GETATTR D:PATH" */
    return (-1);
}

/* GETVOL PATH*/
int dogetvol(int agc, byte **agv)                                    /*__fn__*/
{
byte volume_label[16];

    if (parse_args(agc, agv,"T"))
	{
		if (pc_get_volume(rtfs_args_val_text(0), &volume_label[0]))
        {
            RTFS_PRINT_STRING_1((byte *)"Volume Label: ",0);
            RTFS_PRINT_STRING_1(&volume_label[0] , PRFLG_NL);
            return(0);

        }
        else
        {
            DISPLAY_ERRNO("pc_get_volume")
            RTFS_PRINT_STRING_1((byte *)"pc_get_volume failed", PRFLG_NL); /* "get attributes failed" */
            return(-1);
        }
    }
    RTFS_PRINT_STRING_1((byte *)"Usage: GETVOL D:", PRFLG_NL);
    return (-1);
}


/* DEVINFO */
int dodevinfo(int agc, byte **agv)                                /*__fn__*/
{
    use_args(agc, agv);

    print_device_names();

    return (0);
}



void pause(void)
{
   rtfs_print_prompt_user((byte *)"Press Return ", working_buf);
}

#if (INCLUDE_RTFS_PROPLUS)
/*
<TEST>  Procedure:   do_show_file_blocks() - Call pc_get_file_extents() and display the cluster chain of a named named file
<TEST>   Invoke by typing "SHOWFILEEXTENTS" in the command shell
<TEST>
*/
static int do_show_file_blocks(int agc, byte **agv)
{
FILESEGINFO *plist;
int fd,list_size,return_list_size;
BOOLEAN show_clusters;

    if (!parse_args(agc, agv,"TB"))
    {
        RTFS_PRINT_STRING_1((byte *)"Usage: FILEEXTENTS filename show clusters (Y/N)", PRFLG_NL);
        RTFS_PRINT_STRING_1((byte *)"Usage: If show clusters is Y displays clusters, otherwise raw block number)", PRFLG_NL);
        return(-1);
    }
    plist = (FILESEGINFO *) shell_buf;
    list_size = sizeof(shell_buf)/sizeof(*plist);

    if ((fd = po_open(rtfs_args_val_text(0), (word)PO_RDONLY,(word) (PS_IWRITE | PS_IREAD) ) ) < 0)
    {
        DISPLAY_ERRNO("po_open")
        RTFS_PRINT_STRING_2((byte *)"Cant open ", rtfs_args_val_text(0),PRFLG_NL); /* "Cant open " */
        return(-1);
    }
    show_clusters = (BOOLEAN) rtfs_args_val_lo(1);
    return_list_size = pc_get_file_extents(fd, list_size, plist, show_clusters, TRUE);
    if (return_list_size < 0)
    {
        RTFS_PRINT_STRING_1((byte *)"Get file extents failed ",PRFLG_NL);
        po_close(fd);
        return(-1);
    }
    else
    {
        int i,linecount,fragcount;
		dword cluster_count = 0;

        linecount = fragcount = 0;
        for (i = 0; i < return_list_size; i++, plist++)
        {
            rtfs_print_one_string((byte *)"( ",0);
            rtfs_print_long_1(plist->block,0);
            rtfs_print_one_string((byte *)" - ",0);
            rtfs_print_long_1(plist->block+plist->nblocks-1,0);
			cluster_count += plist->nblocks-1;
            if (fragcount++ < 4)
                rtfs_print_one_string((byte *)")",0);
            else
            {
                fragcount = 0;
                rtfs_print_one_string((byte *)")",PRFLG_NL);
                if (linecount++ > 16)
                {
                    pause();
                    linecount = 0;
                }
            }
        }
		cluster_count += plist->nblocks-1;
        rtfs_print_one_string((byte *)" Clusters in file =  ",0);
        rtfs_print_long_1(cluster_count,PRFLG_NL);


        if (show_clusters)
		{
        	rtfs_print_one_string((byte *)" Sector report  ", PRFLG_NL);
			return_list_size = pc_get_file_extents(fd, list_size, plist, 0, TRUE);

			cluster_count = 0;
			linecount = fragcount = 0;
			for (i = 0; i < return_list_size; i++, plist++)
			{
            	rtfs_print_one_string((byte *)"( ",0);
            	rtfs_print_long_1(plist->block,0);
            	rtfs_print_one_string((byte *)" - ",0);
            	rtfs_print_long_1(plist->block+plist->nblocks-1,0);
            	cluster_count += plist->nblocks-1;
            	if (fragcount++ < 4)
                	rtfs_print_one_string((byte *)")",0);
                else
                {
                	fragcount = 0;
                	rtfs_print_one_string((byte *)")",PRFLG_NL);
                	if (linecount++ > 16)
                	{
                    	pause();
                    	linecount = 0;
                	}
                }
        }
			cluster_count += plist->nblocks-1;
			rtfs_print_one_string((byte *)" Sectors in file =  ",0);
			rtfs_print_long_1(cluster_count,PRFLG_NL);
		}

    }
    po_close(fd);
	return(0);
}
#endif

/*
<TEST>  Procedure:   dodiskclose(() - Call pc_diskclose() to abort or a mount
<TEST>   Invoke by typing "DSKCLOSE" in the command shell
<TEST>
*/
static int dodiskclose(int agc, byte **agv)
{
    if (parse_args(agc, agv,"T"))
    {
        rtfs_print_one_string((byte *)"Shutting down Disk",PRFLG_NL);
        rtfs_print_one_string((byte *)"call SHOWDISKSTATS to re-check open counts",PRFLG_NL);
        /* Abort the mount, second argument FALSE means do not release buffers assigned in pc_diskio_configure */
        pc_diskclose((byte *)rtfs_args_val_text(0), FALSE);
        return(0);
    }
    else
    {
         RTFS_PRINT_STRING_1((byte *)"Usage: DSKCLOSE D: ", PRFLG_NL);
         return(-1);
    }
}

#if (INCLUDE_RTFS_PROPLUS)
static int doeshell(int agc, byte **agv)
{
    use_args(agc, agv);
    efio_shell();
    return(0);
}
#endif
/*
<TEST>  Procedure:   doreset() - Call pc_ertfs_shutdown() to shut down and reset Rtfs
<TEST>   Invoke by typing "RESET" in the command shell
<TEST>
*/
static int doreset(int agc, byte **agv)
{
int i;
    use_args(agc, agv);

    /* <PVO - What about shutdown with dynamic drivers */
    rtfs_print_one_string((byte *)"Simulating ejects of all inserted devices",PRFLG_NL);
    for (i = 0; i < 25; i++)
	{
		if (eject_driveno(i))
		{
		byte d[4];
			d[1] = (byte) ':';
			d[2] = 0;
			d[0] = (byte) ('A' + i);
			rtfs_print_one_string((byte *)"Drive ID  = : ",0);
			rtfs_print_one_string(&d[0],PRFLG_NL);
		}
	}

    rtfs_print_one_string((byte *)"Shutting down RTFS",PRFLG_NL);

    pc_ertfs_shutdown();

#if (LEAKCHECK_ON_RESET)
    if (check_for_leaks())
    	rtfs_print_one_string((byte *)"Leak detected: Memory was allocated and not released",PRFLG_NL);
	else
    	rtfs_print_one_string((byte *)"No leaks detected: All memory that was allocated was released",PRFLG_NL);
#else
    rtfs_print_one_string((byte *)"Leak checking not enabled, so can not check",PRFLG_NL);
#endif

    rtfs_print_one_string((byte *)"Restarting RTFS",PRFLG_NL);
	if (!pc_ertfs_init())
        rtfs_print_one_string((byte *)"Restart reported an error",PRFLG_NL);
	else
	{
    rtfs_print_one_string((byte *)"Simulating re-inserts of all devices",PRFLG_NL);
        for (i = 0; i < 25; i++)
    	{
    		if (saved_media_parms[i])
    		{
    		byte d[4];
    			d[1] = (byte) ':';
    			d[2] = 0;
    			d[0] = (byte) ('A' + i);
    			rtfs_print_one_string((byte *)"Drive ID  = : ",0);
    			rtfs_print_one_string(&d[0],PRFLG_NL);
               	uneject_driveno(i);
			}
		}
	}
    return(0);
}

#ifdef RTFS_WINDOWS
#if (INCLUDE_V_1_0_CONFIG==0)
extern int savesectorstofile(byte *filename, byte *path, dword start, dword end);
int restoresectorsfromfile(byte *filename, byte *drive_name, dword start, dword end);
static int dorestoresectors(int agc, byte **agv)
{
    if (parse_args(agc, agv,"T"))
    {
		return(restoresectorsfromfile( (byte *)rtfs_args_val_text(0),(byte *)rtfs_args_val_text(1), rtfs_args_val_lo(2), rtfs_args_val_lo(3)));
    }
    return 0;
}
static int dosavesectors(int agc, byte **agv)
{
    if (parse_args(agc, agv,"T"))
    {
		return(savesectorstofile( (byte *)rtfs_args_val_text(0),(byte *)rtfs_args_val_text(1), rtfs_args_val_lo(2), rtfs_args_val_lo(3)));
    }
    return 0;
}
#endif
#endif

#if (INCLUDE_DEBUG_LEAK_CHECKING)
static int doleaktest(int agc, byte **agv)
{
struct mem_report report;
    use_args(agc, agv);
    pc_leak_test(&report);
    return(0);
}
#endif

static int doshowerrno(int agc, byte **agv)
{
    use_args(agc, agv);
    rtfs_print_one_string((byte *)"Errno = : ",0);
    rtfs_print_long_1((dword) get_errno(), PRFLG_NL);
    return(0);
}

#define EFISHELL_USAGE(X) rtfs_print_one_string((byte *)X,PRFLG_NL)
#define EFISHELL_WARN(X) rtfs_print_one_string((byte *)X,PRFLG_NL)
#define EFISHELL_SHOW(X) rtfs_print_one_string((byte *)X,0)
#define EFISHELL_SHOWNL(X) rtfs_print_one_string((byte *)X,PRFLG_NL)
#define EFISHELL_SHOWINT(PROMPT, VAL) show_status(PROMPT, VAL, 0)
#define EFISHELL_SHOWINTNL(PROMPT, VAL) show_status(PROMPT, VAL, PRFLG_NL)




/*
<TEST>  Procedure:   do_show_dirent_extents() - display the cluster chain of a named named file or directory
<TEST>   Invoke by typing "SHOWEXTENTS" in the command shell
<TEST>
*/

static int _pc_get_direntry_extents(dword cluster, int infolistsize, FILESEGINFO *plist, BOOLEAN report_clusters, BOOLEAN raw);
#if (INCLUDE_EXFATORFAT64)
/* Get the extents of a directory finode.. */
static int _pcexfat_get_direntry_extents(PC_FILE *pfile, int infolistsize, FILESEGINFO *plist, BOOLEAN report_clusters, BOOLEAN raw);
#endif
/* Turn this into an API */
static int pc_get_direntry_extents(byte *path, int infolistsize, FILESEGINFO *plist, BOOLEAN report_clusters, BOOLEAN raw)
{
DIRENT_INFO dinfo;

#if (INCLUDE_EXFATORFAT64)
	if (tstsh_check_for_exfatorfat64(path))
	{
		int list_size,fd;
		list_size=0;
	    fd = po_open(path, (word)(PO_BINARY|PO_RDONLY),0);
        if (fd >= 0)
        {
                ddword offset,new_offset;
                offset = M64SET32(0,0);

				new_offset=po_lseek64(fd, offset, PSEEK_END);
				if (M64LOWDW(new_offset)==0xffffffff && M64HIGHDW(new_offset)==0xffffffff)
                {
                 	po_close(fd);
                	RTFS_PRINT_STRING_1((byte *)"exFat Seek operation failed ", PRFLG_NL); /* "Seek operation failed " */
					return -1;
				}
				{
        			PC_FILE *pefile;
					pefile = pc_fd2file(fd, 0);
					if (pefile)
					{
						list_size = _pcexfat_get_direntry_extents(pefile, infolistsize, plist, report_clusters, raw);
						release_drive_mount(pefile->pobj->pdrive->driveno); /* Release lock, unmount if aborted */
					}
				}
                po_close(fd);
                return(list_size);
        }
	}
#endif

    /* Now get the file start cluster */
    if (!pc_get_dirent_info(path, &dinfo) || dinfo.fcluster == 0)
        return(-1);
    return(_pc_get_direntry_extents(dinfo.fcluster, infolistsize, plist, report_clusters, raw));

}

#if (INCLUDE_EXFATORFAT64)
/* Get the extents of a directory finode.. */
static int _pcexfat_get_direntry_extents(PC_FILE *pefile, int infolistsize, FILESEGINFO *plist, BOOLEAN report_clusters, BOOLEAN raw)
{
int n_segments = 0;
REGION_FRAGMENT *pf = 0;

#if (INCLUDE_RTFS_PROPLUS)
	pf = pefile->pobj->finode->e.x->pfirst_fragment;
#else
	pf = pefile->pobj->finode->pbasic_fragment;
#endif

    while (pf)
    {
        n_segments += 1;
        if (n_segments >= 32000)    /* Endless loop */
            return(-1);
        if (n_segments <= infolistsize)
        {
		dword nclusters;
			nclusters = pf->end_location-pf->start_location+1;
            if (report_clusters)
            {
                plist->block =   pf->start_location;
                plist->nblocks = nclusters;
             }
            else
            {
                plist->block = pc_cl2sector(pefile->pobj->pdrive, pf->start_location);
                if (raw)
                    plist->block += pefile->pobj->pdrive->drive_info.partition_base;
                plist->nblocks = nclusters << pefile->pobj->pdrive->drive_info.log2_secpalloc;
            }
            plist++;
        }
        pf= pf->pnext;
    }
    return(n_segments);
}
#endif


/* Get the extents of a directory finode.. */
static int _pc_get_direntry_extents(dword cluster, int infolistsize, FILESEGINFO *plist, BOOLEAN report_clusters, BOOLEAN raw)
{
int driveno;
int n_segments = 0;
DDRIVE *pdr;
FILESEGINFO *plist_start;

    plist_start = plist;

    driveno = pc_get_default_drive(0);
    if (driveno < 0)
        return(-1);
    pdr = pc_drno_to_drive_struct(driveno);
    if (!pdr)
        return(-1);
    while (cluster != FAT_EOF_RVAL)
    {

        n_segments += 1;
        if (n_segments >= 32000)    /* Endless loop */
            return(-1);
        if (n_segments <= infolistsize)
        {
            if (report_clusters)
            {
                plist->block =   cluster;
                plist->nblocks = 1;
             }
            else
            {
                plist->block = pc_cl2sector(pdr, cluster);
                if (raw)
                    plist->block += pdr->drive_info.partition_base;
                plist->nblocks = 1 << pdr->drive_info.log2_secpalloc;
            }
            plist++;
        }
         /* Consult the fat for the next cluster. */
        cluster = fatop_next_cluster(pdr, cluster);

         /* Check for an endless loop, set nblocks to 0 if found */
        {
            FILESEGINFO *p;
            int i;
            p = plist_start;
            for (i = 0; i < n_segments; i++, p++)
            {
                if (p->block == cluster)
                {
                    plist->block =   cluster;
                    plist->nblocks = 0;
                    return(n_segments+1);
                }
            }
        }

        if (cluster == 0) /* clnext detected error */
        {
            return (-1);
        }
    }
    return(n_segments);
}


static int do_show_dirent_extents(int agc, byte **agv)
{
FILESEGINFO *plist;
int list_size,return_list_size;
dword cluster_count = 0;

    if (!parse_args(agc, agv,"T"))
    {
        RTFS_PRINT_STRING_1((byte *)"Usage: SHOWEXTENTS pathname", PRFLG_NL);
        RTFS_PRINT_STRING_1((byte *)"Usage: Show clusters in a file or directory)", PRFLG_NL);
        return(-1);
    }
    plist = (FILESEGINFO *) shell_buf;
    list_size = sizeof(shell_buf)/sizeof(*plist);

    return_list_size = pc_get_direntry_extents(rtfs_args_val_text(0), list_size, plist, TRUE, TRUE);

    if (return_list_size < 0)
    {
        RTFS_PRINT_STRING_1((byte *)"Get extents failed ",PRFLG_NL);
        return(-1);
    }
    else
    {
        int i,linecount;

		cluster_count = 0;
        linecount = 0;
        for (i = 0; i < return_list_size; i++, plist++)
        {
            rtfs_print_one_string((byte *)"(",0);
            rtfs_print_long_1(plist->block,0);
            if (plist->nblocks)
			{
                rtfs_print_one_string((byte *)"- ",0);
				rtfs_print_long_1(plist->block+plist->nblocks-1,0);
                rtfs_print_one_string((byte *)")",PRFLG_NL);
            	cluster_count += plist->nblocks-1;
			}
            else
            {
                rtfs_print_one_string((byte *)"<LOOP>)",PRFLG_NL);
                break;
            }

            if (linecount++ > 16)
            {
                pause();
                linecount = 0;
            }
        }
    }
	rtfs_print_one_string((byte *)" Clusters in file =  ",0);
	rtfs_print_long_1(cluster_count,PRFLG_NL);
	{
#if (INCLUDE_MATH64)
		ddword filesizebytes;
		byte buff[32];
		filesizebytes = M64SET32(0,cluster_count);
#ifdef M64MULT32
		filesizebytes = M64MULT32(filesizebytes,pc_cluster_size((byte *)rtfs_args_val_text(0)));
		rtfs_print_one_string((byte *)" Bytes in clusters  =  ",0);
		format_64((char *)buff, filesizebytes, 12);
		rtfs_print_one_string((byte *)buff,PRFLG_NL);
#endif
#else
        dword filesizebytes = cluster_count*pc_cluster_size((byte *)rtfs_args_val_text(0));
    	rtfs_print_one_string((byte *)" Bytes in clusters  =  ",0);
        rtfs_print_long_1(filesizebytes,PRFLG_NL);
#endif
	}
    return_list_size = pc_get_direntry_extents(rtfs_args_val_text(0), list_size, plist, FALSE, TRUE);
	{
        int i,linecount,fragcount;

       	rtfs_print_one_string((byte *)" Sector report  ", PRFLG_NL);

		cluster_count = 0;
		linecount = fragcount = 0;
		for (i = 0; i < return_list_size; i++, plist++)
		{
           	rtfs_print_one_string((byte *)"( ",0);
           	rtfs_print_long_1(plist->block,0);
           	rtfs_print_one_string((byte *)" - ",0);
           	rtfs_print_long_1(plist->block+plist->nblocks-1,0);
           	cluster_count += plist->nblocks-1;
           	if (fragcount++ < 4)
               	rtfs_print_one_string((byte *)")",0);
            else
            {
              	fragcount = 0;
               	rtfs_print_one_string((byte *)")",PRFLG_NL);
               	if (linecount++ > 16)
               	{
                   	pause();
                   	linecount = 0;
               	}
              }
        }
		cluster_count += plist->nblocks-1;
		rtfs_print_one_string((byte *)" Sectors in file =  ",0);
		rtfs_print_long_1(cluster_count,PRFLG_NL);
	}
    return(0);
}

static int doloopfix(int agc, byte **agv)
{
FILESEGINFO *plist;
int driveno, list_size,return_list_size;
DDRIVE *pdr;

    if (!parse_args(agc, agv,"TII"))
        goto usage;

    plist = (FILESEGINFO *) shell_buf;
    list_size = sizeof(shell_buf)/sizeof(*plist);

    return_list_size = pc_get_direntry_extents(rtfs_args_val_text(0), list_size, plist, TRUE, TRUE);
    if (return_list_size < 0)
    {
        EFISHELL_USAGE("usage: Could not access directory entry cluster information");
        goto usage;
    }
    if ((int)rtfs_args_val_lo(1) > return_list_size)
    {
        EFISHELL_USAGE("usage: Invalid cluster offset");
        goto usage;
    }

    driveno = pc_get_default_drive(0);
    if (driveno < 0)
        return(-1);
    pdr = pc_drno_to_drive_struct(driveno);

    if (pdr && fatop_pfaxx(pdr,(plist+rtfs_args_val_lo(1))->block, rtfs_args_val_lo(2)))
    {
        pc_diskflush((byte *)"");
        return(0);
    }
usage:
    EFISHELL_USAGE("usage: LOOPFIX name cloffset cluster");
    EFISHELL_USAGE("usage: Un-Corrupt a file or directory by linking the cluster at offset1 to cluster");
    EFISHELL_USAGE("usage: Example SHOWEXTENTS myfile");
    EFISHELL_USAGE("usage:     Print the extent list and remember the original values.");
    EFISHELL_USAGE("usage: Example LOOPFILE myfile 4 0");
    EFISHELL_USAGE("usage:     Corrupt the file by linking the fifth cluster to the first cluster.");
    EFISHELL_USAGE("usage: Example LOOPFIX myfile 4 1000");
    EFISHELL_USAGE("usage:     Links the fifth cluster to the cluster 100.");
    EFISHELL_USAGE("usage:     (assuming 100 wasthe original value");
    return(-1);
}
static int doloopfile(int agc, byte **agv)
{
FILESEGINFO *plist;
int driveno, list_size,return_list_size;
DDRIVE *pdr;
    if (!parse_args(agc, agv,"TII"))
        goto usage;

    plist = (FILESEGINFO *) shell_buf;
    list_size = sizeof(shell_buf)/sizeof(*plist);

    return_list_size = pc_get_direntry_extents(rtfs_args_val_text(0), list_size, plist, TRUE, TRUE);
    if (return_list_size < 0)
    {
        EFISHELL_USAGE("usage: Could not access directory entry cluster information");
        goto usage;
    }
    if ((int)rtfs_args_val_lo(1) > return_list_size || (int)rtfs_args_val_lo(2) > return_list_size)
    {
        EFISHELL_USAGE("usage: Invalid cluster offset");
        goto usage;
    }

    driveno = pc_get_default_drive(0);
    if (driveno < 0)
        return(-1);
    pdr = pc_drno_to_drive_struct(driveno);

    if (pdr && fatop_pfaxx(pdr,(plist+rtfs_args_val_lo(1))->block, (plist+rtfs_args_val_lo(2))->block))
    {
        pc_diskflush((byte *)"");
        return(0);
    }
usage:
    EFISHELL_USAGE("usage: LOOPFILE name cloffset1 cloffset2");
    EFISHELL_USAGE("usage: Corrupt a file by linking the cluster at offset1 to cloffset2");
    EFISHELL_USAGE("usage: Example LOOPFILE myfile 4 0");
    EFISHELL_USAGE("usage:     Links the fifth cluster to the first cluster in a file.");
    EFISHELL_USAGE("usage: Example LOOPDIR mydir 0 4");
    EFISHELL_USAGE("usage:     Links the first cluster to the fifth cluster in a directory.");
    return(-1);
}

static int doloopdir(int agc, byte **agv)
{
    return(doloopfile(agc, agv));
}



BOOLEAN tstsh_check_for_exfatorfat64(byte *path)
{
DRIVE_INFO drive_info_structure;
BOOLEAN ret_val = FALSE;

	if (path[1] != ':')
    	ret_val = pc_diskio_info((byte *)"", &drive_info_structure, TRUE);
	else
    	ret_val = pc_diskio_info((byte *)path, &drive_info_structure, TRUE);

	ret_val = ret_val && drive_info_structure.is_exfat;
	return(ret_val);
}


/* Print some information about a mounted drive

    dstat DRIVEID:
        DRIVEID:   - The drive id A:. B: etc

*/
void show_status(char *prompt, dword val, int flags);


/*
<TEST>  Procedure:   do_show_disk_stats() - Call pc_diskio_info() to retrieve and display extended information about the current volume
<TEST>   Invoke by typing "SHOWDISKSTATS" in the command shell
<TEST>
*/

static int do_show_disk_stats(int agc, byte **agv)
{
DRIVE_INFO drive_info_structure;
BOOLEAN show_alloc,show_failsafe;
#if (INCLUDE_DEBUG_RUNTIME_STATS)
BOOLEAN show_fat, show_dirs,show_files;
    show_fat = show_dirs = show_files = TRUE;
#endif
	show_alloc = show_failsafe = TRUE;

    RTFS_ARGSUSED_INT(show_failsafe);

    if (!parse_args(agc, agv,"T"))
    {
#if (INCLUDE_DEBUG_RUNTIME_STATS)
usage:
#endif
        EFISHELL_USAGE("usage:   SHOWDISKSTATS DRIVEID:");
#if (INCLUDE_DEBUG_RUNTIME_STATS)
        EFISHELL_USAGE("usage:or SHOWDISKSTATS DRIVEID: frags fats dirs files failsafe");
        EFISHELL_USAGE("Note: fats dirs files not avaiable without INCLUDE_DEBUG_RUNTIME_STATS");
#endif
        return(-1);
    }
#if (INCLUDE_DEBUG_RUNTIME_STATS)
    if (rtfs_args_val_text(1))
    {
        show_alloc = show_fat = show_dirs = show_files = show_failsafe = FALSE;
        if (rtfs_cs_strcmp(rtfs_args_val_text(1), (byte *)"frags", CS_CHARSET_NOT_UNICODE)==0) show_alloc  = TRUE;
        else if (rtfs_cs_strcmp(rtfs_args_val_text(1), (byte *)"fats", CS_CHARSET_NOT_UNICODE)==0)  show_fat    = TRUE;
        else if (rtfs_cs_strcmp(rtfs_args_val_text(1), (byte *)"dirs", CS_CHARSET_NOT_UNICODE)==0)  show_dirs   = TRUE;
        else if (rtfs_cs_strcmp(rtfs_args_val_text(1), (byte *)"files", CS_CHARSET_NOT_UNICODE)==0)  show_files = TRUE;
#if (INCLUDE_FAILSAFE_CODE)
        if (rtfs_cs_strcmp(rtfs_args_val_text(1), (byte *)"failsafe", CS_CHARSET_NOT_UNICODE)==0)show_failsafe = TRUE;
#endif
        else goto usage;
    }
#endif
    if (!pc_diskio_info((byte *)rtfs_args_val_text(0), &drive_info_structure, TRUE))
        return(-1);
   if (show_alloc)
   {
       EFISHELL_SHOWNL("Disk Allocation Information  ");
       EFISHELL_SHOWNL("=============================");
       EFISHELL_SHOWINTNL("Bits per FAT entry          :    ", drive_info_structure.fat_entry_size);
       EFISHELL_SHOWINTNL("Drive open count            :    ", drive_info_structure.drive_opencounter);
       EFISHELL_SHOWINTNL("sector_size                 :    ", drive_info_structure.sector_size);
       EFISHELL_SHOWINTNL("cluster_size                :    ", drive_info_structure.cluster_size);
       EFISHELL_SHOWINTNL("fat_entry_size              :    ", drive_info_structure.fat_entry_size);
       EFISHELL_SHOWINTNL("clusters_total              :    ", drive_info_structure.total_clusters);
       EFISHELL_SHOWINTNL("clusters_free               :    ", drive_info_structure.free_clusters);
       EFISHELL_SHOWINTNL("fragments free              :    ", drive_info_structure.free_fragments);
#if (INCLUDE_RTFS_FREEMANAGER)
       EFISHELL_SHOWINTNL("max region_buffers          :    ", drive_info_structure.region_buffers_total);
       EFISHELL_SHOWINTNL("free region_buffers         :    ", drive_info_structure.region_buffers_free);
       EFISHELL_SHOWINTNL("region_buffers low water    :    ", drive_info_structure.region_buffers_low_water);
#endif
       pause();
    }
#if (INCLUDE_DEBUG_RUNTIME_STATS)
{
DRIVE_RUNTIME_STATS drive_rtstats;
    if (!pc_diskio_runtime_stats((byte *)rtfs_args_val_text(0), &drive_rtstats, FALSE))
        return(-1);
   if (show_fat)
   {
       EFISHELL_SHOWNL("FAT Access Patterns ");
       EFISHELL_SHOWNL("====================");
       EFISHELL_SHOWINTNL("fat_reads                      :    ", drive_rtstats.fat_reads);
       EFISHELL_SHOWINTNL("fat_blocks_read                :    ", drive_rtstats.fat_blocks_read);
       EFISHELL_SHOWINTNL("fat_writes                     :    ", drive_rtstats.fat_writes);
       EFISHELL_SHOWINTNL("fat_blocks_written             :    ", drive_rtstats.fat_blocks_written);
    }
    if (show_dirs)
    {
       EFISHELL_SHOWNL("Directory Buffer Access Patterns ");
       EFISHELL_SHOWNL("=================================");
       EFISHELL_SHOWINTNL("dir_buff_hits               :    ", drive_rtstats.dir_buff_hits);
       EFISHELL_SHOWINTNL("dir_buff_reads              :    ", drive_rtstats.dir_buff_reads);
       EFISHELL_SHOWINTNL("dir_buff_writes             :    ", drive_rtstats.dir_buff_writes);
    }
    if (show_files)
    {
       EFISHELL_SHOWNL("File Data Access Patterns ");
       EFISHELL_SHOWNL("==========================");
       EFISHELL_SHOWINTNL("file_buff_hits              :    ", drive_rtstats.file_buff_hits);
       EFISHELL_SHOWINTNL("file_buff_reads             :    ", drive_rtstats.file_buff_reads);
       EFISHELL_SHOWINTNL("file_buff_writes            :    ", drive_rtstats.file_buff_writes);
       EFISHELL_SHOWINTNL("file_direct_reads           :    ", drive_rtstats.file_direct_reads);
       EFISHELL_SHOWINTNL("file_direct_blocks_read     :    ", drive_rtstats.file_direct_blocks_read);
       EFISHELL_SHOWINTNL("file_direct_writes          :    ", drive_rtstats.file_direct_writes);
       EFISHELL_SHOWINTNL("file_direct_blocks_written  :    ", drive_rtstats.file_direct_blocks_written);
    }
    pause();
}
#endif
#if (INCLUDE_FAILSAFE_CODE)

    if (show_failsafe)
    {
FAILSAFE_RUNTIME_STATS drive_fsstats;
        if (!pc_diskio_failsafe_stats((byte *)rtfs_args_val_text(0), &drive_fsstats))
            return(-1);
        if (!drive_fsstats.journaling_active)
            return(0);
#if (INCLUDE_DEBUG_RUNTIME_STATS)
        EFISHELL_SHOWNL("Journaling Access Patterns ");
        EFISHELL_SHOWNL("===========================");
        EFISHELL_SHOWINTNL("journal_data_reads          :    ", drive_fsstats.journal_data_reads);
        EFISHELL_SHOWINTNL("journal_data_blocks_read    :    ", drive_fsstats.journal_data_blocks_read);
        EFISHELL_SHOWINTNL("journal_data_writes         :    ", drive_fsstats.journal_data_writes);
        EFISHELL_SHOWINTNL("journal_data_blocks_written :    ", drive_fsstats.journal_data_blocks_written);
        EFISHELL_SHOWINTNL("journal_index_writes        :    ", drive_fsstats.journal_index_writes);

        EFISHELL_SHOWNL("Synchronizing Access Patterns  ");
        EFISHELL_SHOWNL("===============================");
        EFISHELL_SHOWINTNL("restore_data_reads          :    ", drive_fsstats.restore_data_reads);
        EFISHELL_SHOWINTNL("restore_data_blocks_read    :    ", drive_fsstats.restore_data_blocks_read);
        EFISHELL_SHOWINTNL("fat_synchronize_writes         :    ", drive_fsstats.fat_synchronize_writes);
        EFISHELL_SHOWINTNL("fat_synchronize_blocks_written :    ", drive_fsstats.fat_synchronize_blocks_written);
        EFISHELL_SHOWINTNL("dir_synchronize_writes         :    ", drive_fsstats.dir_synchronize_writes);
        EFISHELL_SHOWINTNL("dir_synchronize_blocks_written :    ", drive_fsstats.dir_synchronize_blocks_written);

        EFISHELL_SHOWNL("Transaction File Overwrite Access Patterns ");
        EFISHELL_SHOWNL("===========================================");
        EFISHELL_SHOWINTNL("transaction_buff_hits       :    ", drive_fsstats.transaction_buff_hits);
        EFISHELL_SHOWINTNL("transaction_buff_reads      :    ", drive_fsstats.transaction_buff_reads);
        EFISHELL_SHOWINTNL("transaction_buff_writes     :    ", drive_fsstats.transaction_buff_writes);

        pause();
#endif
        EFISHELL_SHOWNL("Failsafe Session Information ");
        EFISHELL_SHOWNL("=============================");
        EFISHELL_SHOWINTNL("journaling_active           :    ", drive_fsstats.journaling_active);
        EFISHELL_SHOWINTNL("sync_in_progress            :    ", drive_fsstats.sync_in_progress);
        EFISHELL_SHOWINTNL("journal_file_size           :    ", drive_fsstats.journal_file_size);
        EFISHELL_SHOWINTNL("journal_file_used           :    ", drive_fsstats.journal_file_used);
        EFISHELL_SHOWINTNL("restore_buffer_size         :    ", drive_fsstats.restore_buffer_size);
        EFISHELL_SHOWINTNL("num_blockmaps               :    ", drive_fsstats.num_blockmaps);
        EFISHELL_SHOWINTNL("num_blockmaps_used          :    ", drive_fsstats.num_blockmaps_used);
        EFISHELL_SHOWINTNL("max_blockmaps_used          :    ", drive_fsstats.max_blockmaps_used);
        EFISHELL_SHOWINTNL("cluster_frees_pending       :    ", drive_fsstats.cluster_frees_pending);
        EFISHELL_SHOWINTNL("current_frame               :    ", drive_fsstats.current_frame);
        EFISHELL_SHOWINTNL("current_index               :    ", drive_fsstats.current_index);
        EFISHELL_SHOWINTNL("flushed_blocks              :    ", drive_fsstats.flushed_blocks);
        EFISHELL_SHOWINTNL("open_blocks                 :    ", drive_fsstats.open_blocks);
        EFISHELL_SHOWINTNL("restoring_blocks            :    ", drive_fsstats.restoring_blocks);
        EFISHELL_SHOWINTNL("restored_blocks             :    ", drive_fsstats.restored_blocks);
        EFISHELL_SHOWINTNL("current_restoring_block     :    ", drive_fsstats.current_restoring_block);
#if (INCLUDE_DEBUG_RUNTIME_STATS)
        EFISHELL_SHOWINTNL("restoring_frames            :    ", drive_fsstats.frames_restoring);
        EFISHELL_SHOWINTNL("closed_frames               :    ", drive_fsstats.frames_closed);
        EFISHELL_SHOWINTNL("flushed_frames              :    ", drive_fsstats.frames_flushed);
        EFISHELL_SHOWINTNL("restore_read_calls          :    ", drive_fsstats.restore_data_reads);
        EFISHELL_SHOWINTNL("restore_read_blocks         :    ", drive_fsstats.restore_data_blocks_read);
        EFISHELL_SHOWINTNL("restore_write_calls         :    ", drive_fsstats.restore_write_calls);
        EFISHELL_SHOWINTNL("restore_write_blocks        :    ", drive_fsstats.restore_blocks_written);
#endif
        pause();
    }
#endif
    return(0);
}



/* Porting may be required */
static byte *gotoeos(byte *p) { while(*p) p++; return(p);}
static void rtfs_print_format_dir(byte *display_buffer, DSTAT *statobj)
{
#if (SYS_SUPPORTS_SPRINTF)
    byte *dirstr;
    byte *p;
    int year;
    if (statobj->fattribute & AVOLUME)
         dirstr = (byte *)"<VOL>";
    else if (statobj->fattribute & ADIRENT)
    {

         dirstr = (byte *)"<DIR>";
    }
    else
        dirstr = (byte *)"     ";

    p = display_buffer;
    *p = 0;

    TERM_SPRINTF((char *)p,"%-8s.", (char *)&(statobj->fname[0]));
    TERM_SPRINTF((char *)gotoeos(p),"%-3s ",  (char *)&(statobj->fext[0]));
#if (INCLUDE_MATH64)
	if (statobj->fsize_hi)
    {
    ddword ddw;
    char buff[32];
    ddw = M64SET32(statobj->fsize_hi,statobj->fsize);
    format_64(buff, ddw, 12);
    TERM_SPRINTF((char *)gotoeos(p),"%16s", buff);
    }
	else
	{
   		TERM_SPRINTF((char *)gotoeos(p),"%10u ", statobj->fsize);
	}
#else
    TERM_SPRINTF((char *)gotoeos(p),"%10u ", statobj->fsize);
#endif
    TERM_SPRINTF((char *)gotoeos(p),"%5s", dirstr);
    TERM_SPRINTF((char *)gotoeos(p)," %02d",(statobj->fdate >> 5 ) & 0xf); /* Month */
    TERM_SPRINTF((char *)gotoeos(p),"-%02d",(statobj->fdate & 0x1f));      /* Day */
    year = (80 +(statobj->fdate >> 9)) & 0xff; /* Year */
    if (year >= 100)
        year -= 100;
    TERM_SPRINTF((char *)gotoeos(p),"-%02d", year); /* Year */
    TERM_SPRINTF((char *)gotoeos(p)," %02d",(statobj->ftime >> 11) & 0x1f);    /* Hour */
    TERM_SPRINTF((char *)gotoeos(p),":%02d",(statobj->ftime >> 5) & 0x3f);    /* Minute */

    /* if lfnmae is null we know that the first two bytes are both 0 */
    if (statobj->lfname[0] || statobj->lfname[1])
    {
        /* For vfat systems display the attributes and the long file name
           seperately. This is a trick since the attribute are ASCII and the
            LFN is UNICODE. If we print seperately we will see them both correctly */
        TERM_SPRINTF((char *)gotoeos(p), " -  ");
        rtfs_print_one_string(display_buffer, 0);
        rtfs_print_one_string(statobj->lfname,PRFLG_NL);
    }
    else
        rtfs_print_one_string(display_buffer,PRFLG_NL);
#else /* #if (SYS_SUPPORTS_SPRINTF) */
    rtfs_print_one_string("SPRINTF NOT SUPPORTED",PRFLG_NL);
#endif

}

/* Porting may be required */
static void rtfs_print_format_stat(byte *display_buffer, ERTFS_STAT *st)
{
#if (SYS_SUPPORTS_SPRINTF)
byte *p;
int y;
    p = display_buffer;
    *p = 0;
    TERM_SPRINTF((char *)gotoeos(p),"DRIVENO: %02d", st->st_dev);
#if (INCLUDE_MATH64)
	if (st->st_size_hi)
    {
    ddword ddw;
    char buff[32];
    ddw = M64SET32(st->st_size_hi,st->st_size);
    format_64(buff, ddw, 12);
    TERM_SPRINTF((char *)gotoeos(p)," SIZE %16s", buff);
    }
	else
#endif
   	TERM_SPRINTF((char *)gotoeos(p)," SIZE: %7u", st->st_size);
	rtfs_print_one_string(display_buffer,PRFLG_NL);
	*p = 0;
    TERM_SPRINTF((char *)gotoeos(p)," OPTIMAL BLOCK SIZE: %7u ",st->st_blksize);
	rtfs_print_one_string(display_buffer,PRFLG_NL);
	*p = 0;
    TERM_SPRINTF((char *)gotoeos(p)," FILE size (BLOCKS): %7u",st->st_blocks);
	rtfs_print_one_string(display_buffer,PRFLG_NL);
	*p = 0;
    TERM_SPRINTF((char *)gotoeos(p)," DATE Created :%02d-%02d",(st->st_ctime.date >> 5 ) & 0xf,/* Month */(st->st_ctime.date & 0x1f)/* Day */);
	y = (st->st_ctime.date >> 9);y &= 0x7f;	y += 1980;
    TERM_SPRINTF((char *)gotoeos(p),"-%04d", y); /* Year */

    TERM_SPRINTF((char *)gotoeos(p),"  TIME:%02d:%02d\n",(st->st_ctime.time >> 11) & 0x1f,/* Hour */(st->st_ctime.time >> 5) & 0x3f); /* Minute */
    rtfs_print_one_string(display_buffer,PRFLG_NL);
	*p = 0;
    TERM_SPRINTF((char *)gotoeos(p)," DATE Modified :%02d-%02d",(st->st_mtime.date >> 5 ) & 0xf,/* Month */(st->st_mtime.date & 0x1f)/* Day */);
	y = (st->st_mtime.date >> 9);y &= 0x7f;	y += 1980;
    TERM_SPRINTF((char *)gotoeos(p),"-%04d", y); /* Year */
    TERM_SPRINTF((char *)gotoeos(p),"  TIME:%02d:%02d\n",(st->st_mtime.time >> 11) & 0x1f,/* Hour */(st->st_mtime.time >> 5) & 0x3f); /* Minute */

    rtfs_print_one_string(display_buffer,PRFLG_NL);
	*p = 0;
    TERM_SPRINTF((char *)gotoeos(p)," DATE Accessed :%02d-%02d",(st->st_atime.date >> 5 ) & 0xf,/* Month */(st->st_atime.date & 0x1f)/* Day */);
	y = (st->st_atime.date >> 9);y &= 0x7f;	y += 1980;
    TERM_SPRINTF((char *)gotoeos(p),"-%04d", y); /* Year */
    TERM_SPRINTF((char *)gotoeos(p),"  TIME:%02d:%02d\n",(st->st_atime.time >> 11) & 0x1f,/* Hour */(st->st_atime.time >> 5) & 0x3f); /* Minute */
    rtfs_print_one_string(display_buffer,PRFLG_NL);
#else /* #if (SYS_SUPPORTS_SPRINTF) */
    rtfs_print_one_string("SPRINTF NOT SUPPORTED",PRFLG_NL);
#endif

}
#if (INCLUDE_MATH64)
void format_64(char *buffer, ddword ddw, int precision)
{
#if (!INCLUDE_NATIVE_64_TYPE)
int i;
byte *from,my_buffer_lo[32],my_buffer_hi[32];
dword hi;
   my_buffer_hi[0] = 0;
   hi = M64HIGHDW(ddw);
   if (!hi)
   {
        my_buffer_hi[0] = 0;
        pc_ltoa(M64LOWDW(ddw), my_buffer_lo,10);
   }
   else
   {
        byte *to;
        int c;
        dword lo;
        /* Get the low word and right justify to 8 places lead with 0s */
        lo = M64LOWDW(ddw);
        if (lo)
        {
            pc_ltoa(lo, my_buffer_hi,16);
            from = my_buffer_hi;
            /* How many unsignificant digits in the low word */
            for (c=8,i = 0; i < 8; i++)
            { if (*from++) c--; else break; }
            to = my_buffer_lo;
            for (i = 0; i < c; i++)    { *to++ = '0'; }
            while(*from) {*to++ = *from++;}
            *to = 0;
        }
        else
        {
            for (i = 0; i < 8; i++)    { my_buffer_lo[i] = '0';}
            my_buffer_lo[8] = 0;
        }
        my_buffer_hi[0] = '0';
        my_buffer_hi[1] = 'x';
        pc_ltoa(hi, &my_buffer_hi[2],16);
   }

   rtfs_cs_strcat(my_buffer_hi,my_buffer_lo, CS_CHARSET_NOT_UNICODE);
   from = my_buffer_hi;
   /* Right justify */
   /* Leading spaces in buffer */
   for (i = 0; i < precision; i++)
   { if (*from) from++; else *buffer++ = ' ';  }
    from = my_buffer_hi;
    /* Now into buffer */
   for (i = 0; i < precision; i++)
   { if (*from) *buffer++ = *from++; else break; }
   *buffer = 0;
#else
    RTFS_ARGSUSED_INT(precision);
#ifdef RTFS_WINDOWS
   TERM_SPRINTF(buffer,"%8I64u", ddw);
#else
    TERM_SPRINTF(buffer,"%8llu", ddw);
#endif
#endif
}
#endif
