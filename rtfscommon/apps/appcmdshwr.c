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
/*
<TEST>  Test File:   rtfscommon/apps/appcmdshwr.c
*/

#include "rtfs.h"


#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */


#define INCLUDE_MKROM 0
#define INCLUDE_MKHOSTDISK 0
#define DISPLAY_ERRNO(ROUTINE)
/* #define DISPLAY_ERRNO(ROUTINE) printf("API Call Failed %s: errno == %d\n", ROUTINE, get_errno()); */

void show_status(char *prompt, dword val, int flags);
#define EFISHELL_USAGE(X) rtfs_print_one_string((byte *)X,PRFLG_NL)
#define EFISHELL_WARN(X) rtfs_print_one_string((byte *)X,PRFLG_NL)
#define EFISHELL_SHOW(X) rtfs_print_one_string((byte *)X,0)
#define EFISHELL_SHOWNL(X) rtfs_print_one_string((byte *)X,PRFLG_NL)
#define EFISHELL_SHOWINT(PROMPT, VAL) show_status(PROMPT, VAL, 0)
#define EFISHELL_SHOWINTNL(PROMPT, VAL) show_status(PROMPT, VAL, PRFLG_NL)


BOOLEAN tstsh_is_yes(byte *p);
BOOLEAN tstsh_is_no(byte *p);
byte *pc_ltoa(dword num, byte *dest, int number_base);


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
#if (INCLUDE_CS_UNICODE)
byte *rtfs_args_val_utext(int this_arg, int which_uarg);
#endif
void use_args(int agc, byte **agv);
void rtfs_print_prompt_user(byte *prompt, byte *buf);


extern byte shell_buf[1024];
extern byte working_buf[512];      /* Not sector size dependant used by lex: must be global */
void pause(void);

#define RNDFUFFSIZE 512 /* Not sector size dependant */
typedef struct rndfile {
    int fd;
    int reclen;
    byte name[90];
    byte buff[RNDFUFFSIZE];
} RNDFILE;

RNDFILE *fndrnd( int fd);

extern BOOLEAN unicode_enabled;


/* WRITE fd "data" */
int dowrite(int agc, byte **agv)                                /*__fn__*/
{
    int fd;
    RNDFILE *rndf;

    if (parse_args(agc, agv,"IT"))
    {
        fd = (int)rtfs_args_val_lo(0);
        rndf = fndrnd(  fd );
        if (!rndf)
            {RTFS_PRINT_STRING_1((byte *)"Cant find file",PRFLG_NL);} /* "Cant find file" */
        else
        {
            pc_cppad((byte*)rndf->buff,rtfs_args_val_text(1),(int) rndf->reclen);
            rndf->buff[rndf->reclen] = 0;
            if ( po_write(fd,(byte*)rndf->buff,(word)rndf->reclen) != rndf->reclen)
            {
                DISPLAY_ERRNO("po_write")
                RTFS_PRINT_STRING_1((byte *)"Write operation failed ", PRFLG_NL); /* "Write operation failed " */
            }
            else
            {
				po_flush(fd);
                return(0);
            }
        }
    }
    RTFS_PRINT_STRING_1((byte *)"Usage: WRITE fd data ",PRFLG_NL); /* "Usage: WRITE fd data " */
    return(-1);
}

/*
<TEST>  Procedure:   domkdir(() - Call pc_mkdir() to create a directory
<TEST>   Invoke by typing "MKDIR" in the command shell
<TEST>
*/

/* MKDIR PATH */
int domkdir(int agc, byte **agv)                            /*__fn__*/
{
    if (parse_args(agc, agv,"T"))
    {
#if (INCLUDE_CS_UNICODE)
        if (unicode_enabled)
        {
            if (pc_mkdir_uc(rtfs_args_val_utext(0, 0)))
                return(0);
        }
#endif
        if (!unicode_enabled)
            if (pc_mkdir(rtfs_args_val_text(0)))
                return(0);

        /* Did not work */
        DISPLAY_ERRNO("pc_mkdir")
        return(-1);
    }
    RTFS_PRINT_STRING_1((byte *)"Usage: MKDIR D:PATH",PRFLG_NL); /* "Usage: MKDIR D:PATH" */
    return (-1);
}

/* RMDIR PATH */
/*
<TEST>  Procedure:   dormdir(() - Call pc_rmdir() to delete a directory
<TEST>   Invoke by typing "RMDIR" in the command shell
<TEST>
*/
int dormdir(int agc, byte **agv)                                    /*__fn__*/
{
    if (parse_args(agc, agv,"T"))
    {
#if (INCLUDE_CS_UNICODE)
        if (unicode_enabled)
        {
            if (pc_rmdir_uc(rtfs_args_val_utext(0, 0)))
                return(0);
        }
#endif
        if (!unicode_enabled)
            if (pc_rmdir(rtfs_args_val_text(0)))
                return(0);
       DISPLAY_ERRNO("pc_rmdir")
    }
    RTFS_PRINT_STRING_1((byte *)"Usage: RMDIR D:PATH",PRFLG_NL); /* "Usage: RMDIR D:PATH\n" */
    return (-1);
}

/* DELTREE PATH */
/*
<TEST>  Procedure:   dodeltree(() - Call pc_dodeltree() to recursively delete a directory
<TEST>   Invoke by typing "DELTREE" in the command shell
<TEST>
*/
int dodeltree(int agc, byte **agv)                                    /*__fn__*/
{
    if (parse_args(agc, agv,"T"))
    {
#if (INCLUDE_CS_UNICODE)
        if (unicode_enabled)
        {
            if (pc_deltree_uc(rtfs_args_val_utext(0, 0)))
                return(0);
        }
#endif
        if (!unicode_enabled)
            if (pc_deltree(rtfs_args_val_text(0)))
                return(0);
        DISPLAY_ERRNO("pc_deltree")
        return(-1);
    }
    RTFS_PRINT_STRING_1((byte *)"Usage: DELTREE D:PATH",PRFLG_NL); /* "Usage: DELTREE D:PATH" */
    return (-1);
}

/* DELETE PATH */
byte rmfil[EMAXPATH_BYTES];
byte rmpath[EMAXPATH_BYTES];
byte tfile[EMAXPATH_BYTES];
/*
<TEST>  Procedure:   dorm() - Call pc_rm() to delete a file. Use enumeration to delete multiple files using wildcard patterns.
<TEST>   Invoke by typing "DELETE" in the command shell
<TEST>
*/
int dorm(int agc, byte **agv)                                       /*__fn__*/
{
    DSTAT statobj;

    if (parse_args(agc, agv,"T"))
    {
        /* Get the path */
        rtfs_cs_strcpy(&rmfil[0],rtfs_args_val_text(0),CS_CHARSET_NOT_UNICODE);

        if (pc_gfirst(&statobj, &rmfil[0]))
        {
            do
            {
                /* Construct the path to the current entry ASCII 8.3 */
                if (statobj.lfname[0])
					pc_mpath((byte*)&rmpath[0],(byte*) &statobj.path[0],(byte*)&statobj.lfname[0], CS_CHARSET_NOT_UNICODE);
				else
				{
					pc_cs_mfile((byte*) &tfile[0],(byte*) &statobj.fname[0], (byte*) &statobj.fext[0], CS_CHARSET_NOT_UNICODE);
					pc_mpath((byte*)&rmpath[0],(byte*) &statobj.path[0],(byte*)&tfile[0], CS_CHARSET_NOT_UNICODE);
				}
                /* Delete if it's  a regular file */
                if (!pc_isdir(rmpath) && !pc_isvol(rmpath))
                {
                    RTFS_PRINT_STRING_2((byte *)"deleting  --> ",rmpath,PRFLG_NL); /* "deleting  --> " */
                    if (!pc_unlink( rmpath ) )
                    {
                        DISPLAY_ERRNO("pc_unlink")
                        RTFS_PRINT_STRING_2((byte *)"Can not delete: ",rmpath,PRFLG_NL); /* "Can not delete: " */
                    }
                }
            }  while(pc_gnext(&statobj));
            DISPLAY_ERRNO("pc_gnext: PENOENT IS OKAY")
            pc_gdone(&statobj);
        }
        else
        {
            DISPLAY_ERRNO("pc_gfirst")
        }
        return(0);
    }
    RTFS_PRINT_STRING_1((byte *)"Usage: DELETE D:PATH",PRFLG_NL); /* "Usage: DELETE D:PATH" */
    return(-1);
}

/* RENAME PATH NEWNAME */
/*
<TEST>  Procedure:   domv() - Call pc_mv() to move or rename a file
<TEST>   Invoke by typing "RENAME" in the command shell
<TEST>
*/
int domv(int agc, byte **agv)                                      /*__fn__*/
{
    if (parse_args(agc, agv,"TT"))
    {
#if (INCLUDE_CS_UNICODE)
        if (unicode_enabled)
        {
            if (pc_mv_uc(rtfs_args_val_utext(0,0) , rtfs_args_val_utext(1,1)))
                return(0);
        }
#endif
        if (!unicode_enabled)
            if (pc_mv(rtfs_args_val_text(0) , rtfs_args_val_text(1)))
                return(0);
        DISPLAY_ERRNO("pc_mv")
        return(-1);
    }

    RTFS_PRINT_STRING_1((byte *)"Usage:  RENAME PATH NEWNAME",PRFLG_NL); /* "Usage:  RENAME PATH NEWNAME" */
    return(-1);
}

int doregress(int agc, byte **agv)
{
    if (parse_args(agc, agv,"T"))
    {
        pc_regression_test(rtfs_args_val_text(0), TRUE);
        return(0);
    }
    else
    {
        RTFS_PRINT_STRING_1((byte *)"Usage: REGRESSTEST D:",PRFLG_NL); /*  "Usage: REGRESSTEST D:" */
        return(-1);
    }
}

#if (INCLUDE_NAND_DRIVER)
BOOLEAN pc_nand_regression_test(byte *driveid);

int donandregress(int agc, byte **agv)
{
    if (parse_args(agc, agv,"T"))
    {
        if (pc_nand_regression_test(rtfs_args_val_text(0)))
		{
        	RTFS_PRINT_STRING_1((byte *)"NAND Test passed",PRFLG_NL);
		}
		else
		{
        	RTFS_PRINT_STRING_1((byte *)"NAND Test failed",PRFLG_NL);
		}
        return(0);
    }
    else
    {
        RTFS_PRINT_STRING_1((byte *)"Usage: TESTNAND D:",PRFLG_NL); /*  "Usage: REGRESSTEST D:" */
        return(-1);
    }
}
#endif


#if (INCLUDE_CHKDSK)
/*  */
/*
<TEST>  Procedure:   dochkdsk() - Call pc_check_disk() to check a disk's format
<TEST>   Invoke by typing "CHKDSK" in the command shell
<TEST>
*/

#define CHKDSK_NUM_SRATCH_REGION_BUFFERS 2000
#define CHKDSK_SRATCH_MEMORYSIZE (12 * CHKDSK_NUM_SRATCH_REGION_BUFFERS)
/* These are declared here because they are also used by the regression test */
EXRAM byte cmdshell_check_disk_scratch_memory[CHKDSK_SRATCH_MEMORYSIZE];
dword cmdshell_size_check_disk_scratch_memory = CHKDSK_SRATCH_MEMORYSIZE;

static void print_chkdsk_statistics(CHKDISK_STATS *pstats);


int dochkdsk(int agc, byte **agv)                                  /*__fn__*/
{
int write_chains;
CHKDISK_STATS chkstat;
CHKDSK_CONTEXT chkcontext;
dword chkdsk_options;
byte buf[32];
    if (!parse_args(agc, agv,"TI"))
    {
        RTFS_PRINT_STRING_1((byte *) "Usage: CHKDSK DRIVE: FIXPROBLEMS",PRFLG_NL); /* "Usage: CHKDSK DRIVE: FIXPROBLEMS" */
        RTFS_PRINT_STRING_1((byte *) "Example:CHKDSK A: 1 or CHKDSK A: 0" ,PRFLG_NL); /* "Example:CHKDSK A: 1 or CHKDSK A: 0" */
        return(0);
    }

	chkdsk_options = CHKDSK_VERBOSE;
    write_chains = (int) rtfs_args_val_lo(1);

	if (write_chains)
	{
		chkdsk_options |= CHKDSK_FIXPROBLEMS;
		rtfs_print_prompt_user((byte *)"Press Y to free all files with any errors ", buf); /* "Press Y to free all files with any errors " */
		if (tstsh_is_yes(buf))
			chkdsk_options |= CHKDSK_FREEFILESWITHERRORS;
		rtfs_print_prompt_user((byte *) "Press Y to free all subdirectories with any errors ", buf); /* "Press Y to free all subdirectories with any errors " */
		if (tstsh_is_yes(buf))
			chkdsk_options |= CHKDSK_FREESUBDIRSWITHERRORS;
		rtfs_print_prompt_user((byte *) "Press Y to free lost cluster chains, N to create CHK files", buf); /* "Press Y to free lost cluster chains, N to create CHK files" */
		if (tstsh_is_yes(buf))
			chkdsk_options |= CHKDSK_FREELOSTCLUSTERS;
	}
    if (pc_check_disk_ex(rtfs_args_val_text(0), &chkstat, chkdsk_options, &chkcontext, (void *)&cmdshell_check_disk_scratch_memory[0], cmdshell_size_check_disk_scratch_memory))
    	print_chkdsk_statistics(&chkstat);
    return(0);
}
#ifdef RTFS_MAJOR_VERSION
/* Version 6 strings, version 6 does not use a string table */
#define    USTRING_CHKDSK_09 (byte *)"      Crossed Chains Were Found"
#define    USTRING_CHKDSK_11 (byte *)"       user files in this many directories "
#define    USTRING_CHKDSK_13 (byte *)"     Kbytes total disk space"
#define    USTRING_CHKDSK_15 (byte *)" Kbytes in "
#define    USTRING_CHKDSK_16 (byte *)" hidden files"
#define    USTRING_CHKDSK_18 (byte *)" KBytes in "
#define    USTRING_CHKDSK_19 (byte *)" directories"
#define    USTRING_CHKDSK_21 (byte *)" KBytes in "
#define    USTRING_CHKDSK_22 (byte *)" user files"
#define    USTRING_CHKDSK_24 (byte *)" KBytes in bad sectors"
#define    USTRING_CHKDSK_26 (byte *)" Free sectors available on disk"
#define    USTRING_CHKDSK_29 (byte *)" Bytes Per Allocation Unit"
#define    USTRING_CHKDSK_30 (byte *)" "
#define    USTRING_CHKDSK_32 (byte *)" Total Allocation Units On Disk"
#define    USTRING_CHKDSK_35 (byte *)" lost clusters found in "
#define    USTRING_CHKDSK_36 (byte *)" lost chains"
#define    USTRING_CHKDSK_38 (byte *)"  bad long file name chains found"
#define    USTRING_CHKDSK_39 (byte *)" and deleted"
#define    USTRING_CHKDSK_40 (byte *)" that were not deleted"
#define    USTRING_SYS_TAB	(byte *)"       "
#define    USTRING_SYS_NULL  (byte *) ""
#define    USTRING_CHKDSK_53 (byte *)"  Unterminated chains found."
#define    USTRING_CHKDSK_54 (byte *)"  Chains containing bad values found."
#define    USTRING_CHKDSK_55 (byte *)"  Directory entries with invalid first cluster value or whose first cluster is free."
#define    USTRING_CHKDSK_56 (byte *)"  Subdirectories were removed."
#define    USTRING_CHKDSK_57 (byte *)"  Files were removed."
#define    USTRING_CHKDSK_58 (byte *)"  Clusters were freed from lost or corrupted cluster chains."
#define    USTRING_CHKDSK_59 (byte *)"  .CHK were created to preserve lost cluster chains."

/* Version 6 drive info access */
#define DRIVE_INFO(D,F) D->drive_info.F
#else
/* Version 4 drive info access */
#define DRIVE_INFO(D,F) D->F
#endif

static dword chkdsk_sectorstokbytes(CHKDISK_STATS *pstat, dword n_sectors)
{
	dword kbytes;
	if (pstat->n_bytespersector >= 1024)
		kbytes = n_sectors * (pstat->n_bytespersector/1024);
	else
		kbytes = n_sectors/2; /* 512 bytes == 1 half k */
	return(kbytes);
}


static void print_chkdsk_statistics(CHKDISK_STATS *pstats)
{
    dword ltemp;

    RTFS_PRINT_STRING_1(USTRING_SYS_TAB, 0); /* "       " */
    RTFS_PRINT_LONG_1  ((dword)pstats->n_user_files, 0);
    RTFS_PRINT_STRING_1(USTRING_CHKDSK_11,0); /* "       user files in this many directories " */
	ltemp = (dword)pstats->n_user_directories;
    RTFS_PRINT_LONG_1  (ltemp, PRFLG_NL);

	ltemp = pstats->n_clusters_total;
    ltemp *= (dword) pstats->n_sectorspercluster;
    RTFS_PRINT_STRING_1(USTRING_SYS_TAB, 0); /* "       " */
    RTFS_PRINT_LONG_1  (chkdsk_sectorstokbytes(pstats,ltemp), 0);
    RTFS_PRINT_STRING_1(USTRING_CHKDSK_13, PRFLG_NL); /* "     KBytes total disk space" */

    ltemp =  (dword) pstats->n_hidden_clusters;
    ltemp *= (dword) pstats->n_sectorspercluster;
    RTFS_PRINT_STRING_1(USTRING_SYS_TAB, 0); /* "       " */
    RTFS_PRINT_LONG_1  (chkdsk_sectorstokbytes(pstats,ltemp), 0);
    RTFS_PRINT_STRING_1(USTRING_CHKDSK_15, 0); /* " KBytes in " */
    RTFS_PRINT_LONG_1  ((dword)pstats->n_hidden_files, 0);
    RTFS_PRINT_STRING_1(USTRING_CHKDSK_16, PRFLG_NL); /* " hidden files" */

    ltemp =  (dword) pstats->n_dir_clusters;
    ltemp *= pstats->n_sectorspercluster;
    ltemp += pstats->n_reservedrootsectors;
    RTFS_PRINT_STRING_1(USTRING_SYS_TAB, 0); /* "       " */
    RTFS_PRINT_LONG_1  (chkdsk_sectorstokbytes(pstats,ltemp), 0);
    RTFS_PRINT_STRING_1(USTRING_CHKDSK_18, 0); /* " KBytes in " */
    RTFS_PRINT_LONG_1  ((dword)pstats->n_user_directories, 0);
    RTFS_PRINT_STRING_1(USTRING_CHKDSK_19, PRFLG_NL); /* " directories" */

    ltemp =  (dword) pstats->n_file_clusters;
    ltemp *= pstats->n_sectorspercluster;
    RTFS_PRINT_STRING_1(USTRING_SYS_TAB, 0); /* "       " */
    RTFS_PRINT_LONG_1  (chkdsk_sectorstokbytes(pstats,ltemp), 0);
    RTFS_PRINT_STRING_1(USTRING_CHKDSK_21, 0); /* " KBytes in " */
    RTFS_PRINT_LONG_1  ((dword)pstats->n_user_files, 0);
    RTFS_PRINT_STRING_1(USTRING_CHKDSK_22, PRFLG_NL); /* " user files" */

    ltemp =  (dword) pstats->n_bad_clusters;
    ltemp *= pstats->n_sectorspercluster;
    RTFS_PRINT_STRING_1(USTRING_SYS_TAB, 0); /* "       " */
    RTFS_PRINT_LONG_1  (chkdsk_sectorstokbytes(pstats,ltemp), 0);
    RTFS_PRINT_STRING_1(USTRING_CHKDSK_24, PRFLG_NL); /* " KBytes in bad sectors" */

    ltemp =  (dword) pstats->n_free_clusters;
    ltemp *= pstats->n_sectorspercluster;
    RTFS_PRINT_STRING_1(USTRING_SYS_TAB, 0); /* "       " */
    RTFS_PRINT_LONG_1  ((dword)(ltemp), 0);
    RTFS_PRINT_STRING_1(USTRING_CHKDSK_26, PRFLG_NL); /* " Free sectors available on disk" */

    ltemp = pstats->n_sectorspercluster * pstats->n_bytespersector;
    RTFS_PRINT_STRING_1(USTRING_SYS_TAB, PRFLG_NL); /* "       " */
    RTFS_PRINT_STRING_1(USTRING_SYS_TAB, 0); /* "       " */
    RTFS_PRINT_LONG_1  ((dword)ltemp, 0);
    RTFS_PRINT_STRING_1(USTRING_CHKDSK_29, PRFLG_NL); /* " Bytes Per Allocation Unit" */

    ltemp = pstats->n_clusters_total;
    RTFS_PRINT_STRING_1(USTRING_CHKDSK_30, PRFLG_NL); /* "" */
    RTFS_PRINT_STRING_1(USTRING_SYS_TAB, 0); /* "       " */
    RTFS_PRINT_LONG_1  ((dword)ltemp, 0);
    RTFS_PRINT_STRING_1(USTRING_CHKDSK_32, PRFLG_NL); /* " Total Allocation Units On Disk" */

    RTFS_PRINT_STRING_1(USTRING_SYS_NULL, PRFLG_NL); /* "" */

/* ===

pstats->n_lost_chains=
pstats->n_bad_lfns=
pstats->n_crossed_chains=
pstats->n_unterminated_chains=
pstats->n_badcluster_values=
pstats->n_bad_dirents=
pstats->n_directories_removed=
pstats->n_files_removed=
pstats->n_clusters_freed=
pstats->n_checkfiles_created=999;

*/

    if (pstats->n_lost_chains)
    {
        RTFS_PRINT_STRING_1(USTRING_SYS_TAB, 0); /* "       " */
        RTFS_PRINT_LONG_1((dword)pstats->n_lost_clusters, 0);
        RTFS_PRINT_STRING_1(USTRING_CHKDSK_35, 0); /* " lost clusters found in " */
        RTFS_PRINT_LONG_1((dword)pstats->n_lost_chains, 0);
        RTFS_PRINT_STRING_1(USTRING_CHKDSK_36, PRFLG_NL); /* " lost chains" */
    }

    if (pstats->n_bad_lfns)
    {
        RTFS_PRINT_STRING_1(USTRING_SYS_TAB, 0); /* "       " */
        RTFS_PRINT_LONG_1((dword)pstats->n_bad_lfns, 0);
        RTFS_PRINT_STRING_1(USTRING_CHKDSK_38,PRFLG_NL); /* "  bad long file name chains found" */
    }

    if (pstats->n_crossed_chains)
    {
        RTFS_PRINT_STRING_1(USTRING_SYS_TAB, 0); /* "       " */
        RTFS_PRINT_LONG_1((dword)pstats->n_crossed_chains, 0);
    	RTFS_PRINT_STRING_1(USTRING_CHKDSK_09,PRFLG_NL); /* "   Crossed Chains Were Found" */
    }
    if (pstats->n_unterminated_chains)
    {
        RTFS_PRINT_STRING_1(USTRING_SYS_TAB, 0); /* "       " */
        RTFS_PRINT_LONG_1((dword)pstats->n_unterminated_chains, 0);
        RTFS_PRINT_STRING_1(USTRING_CHKDSK_53,PRFLG_NL); /* "  Unterminated_chains found" */
    }

	if (pstats->n_badcluster_values)
    {
        RTFS_PRINT_STRING_1(USTRING_SYS_TAB, 0); /* "       " */
        RTFS_PRINT_LONG_1((dword)pstats->n_badcluster_values, 0);
        RTFS_PRINT_STRING_1(USTRING_CHKDSK_54, PRFLG_NL); /* "  chains containing bad values found" */
    }

	if (pstats->n_bad_dirents)
    {
        RTFS_PRINT_STRING_1(USTRING_SYS_TAB, 0); /* "       " */
        RTFS_PRINT_LONG_1((dword)pstats->n_bad_dirents, 0);
        RTFS_PRINT_STRING_1(USTRING_CHKDSK_55,PRFLG_NL); /* "  Directory entries with invalid first cluster value or whose first cluster is free" */
    }

	if (pstats->n_directories_removed)
    {
        RTFS_PRINT_STRING_1(USTRING_SYS_TAB, 0); /* "       " */
        RTFS_PRINT_LONG_1((dword)pstats->n_directories_removed, 0);
        RTFS_PRINT_STRING_1(USTRING_CHKDSK_56,PRFLG_NL); /* "  Subdirectories were removed" */
    }
	if (pstats->n_files_removed)
    {
        RTFS_PRINT_STRING_1(USTRING_SYS_TAB, 0); /* "       " */
        RTFS_PRINT_LONG_1((dword)pstats->n_files_removed, 0);
        RTFS_PRINT_STRING_1(USTRING_CHKDSK_57,PRFLG_NL); /* "  Files were removed" */
    }

	if (pstats->n_clusters_freed)
    {
        RTFS_PRINT_STRING_1(USTRING_SYS_TAB, 0); /* "       " */
        RTFS_PRINT_LONG_1((dword)pstats->n_clusters_freed, 0);
        RTFS_PRINT_STRING_1(USTRING_CHKDSK_58,PRFLG_NL); /* "  Clusters were freed from lost or corrupted cluster chains" */
    }

	if (pstats->n_checkfiles_created)
    {
        RTFS_PRINT_STRING_1(USTRING_SYS_TAB, 0); /* "       " */
        RTFS_PRINT_LONG_1((dword)pstats->n_checkfiles_created, 0);
        RTFS_PRINT_STRING_1(USTRING_CHKDSK_59,PRFLG_NL); /* "  .CHK were created to preserve lost cluster chains." */
    }

}


#endif
/*
<TEST>  Procedure:   dochsize() - Call po_chsize() to truncate or grow a file.
<TEST>   Invoke by typing "CHSIZE" in the command shell
<TEST>
*/

int dochsize(int agc, byte **agv)                                  /*__fn__*/
{
    int fd;
    long newsize;


    if (parse_args(agc, agv,"TI"))
    {
        if ((fd = po_open(rtfs_args_val_text(0), (word)(PO_BINARY|PO_WRONLY),(word) (PS_IWRITE | PS_IREAD) ) ) < 0)
        {
            DISPLAY_ERRNO("po_open")
            RTFS_PRINT_STRING_2((byte *)"Cant open file: ", rtfs_args_val_text(0),PRFLG_NL); /* "Cant open file: " */
            return(-1);
        }
        newsize = rtfs_args_val_lo(1);
        if (po_chsize(fd, newsize) != 0)
        {
            DISPLAY_ERRNO("po_chsize")
            RTFS_PRINT_STRING_1((byte *)"Change size function failed", PRFLG_NL); /* "Change size function failed" */
        }
        if (po_close(fd) != 0)
        {
            DISPLAY_ERRNO("po_close")
        }
        return(0);
    }
    else
    {
        RTFS_PRINT_STRING_1((byte *)"Usage: CHSIZE PATH NEWSIZE",PRFLG_NL); /* "Usage: CHSIZE PATH NEWSIZE" */
        return(-1);
    }
}

/*
<TEST>  Procedure:   docopy() - Copy one named file to another
<TEST>   Invoke by typing "COPY" in the command shell
<TEST>
*/

/* COPY PATH PATH */
#if (0&&(defined(RTFS_WINDOWS) || defined(RTFS_LINUX)))
#define COPYBUFFSIZE 131072
#else
#define COPYBUFFSIZE 512
#endif

int docopy(int agc, byte **agv)                                 /*__fn__*/
{
    int in_fd;
    int out_fd;
    int nread;
    BOOLEAN forever = TRUE;         /* use this for while(forever) to quiet anal compilers */
    byte copy_buffer[COPYBUFFSIZE];
	dword nloops = 0;
	int copymetadatonly = 1;		/* set to zero to just read */

    if (parse_args(agc, agv,"TT"))
    {
    in_fd = out_fd = 0;
#if (INCLUDE_CS_UNICODE)
        if (unicode_enabled)
            in_fd = po_open_uc(rtfs_args_val_utext(0,0),(word) (PO_BINARY|PO_RDONLY),(word) (PS_IWRITE | PS_IREAD) );
#endif
        if (!unicode_enabled)
            in_fd = po_open(rtfs_args_val_text(0),(word) (PO_BINARY|PO_RDONLY),(word) (PS_IWRITE | PS_IREAD) );
#if (INCLUDE_CS_UNICODE)
        if (unicode_enabled)
            out_fd = po_open_uc(rtfs_args_val_utext(1,1),(word) (PO_BINARY|PO_WRONLY|PO_CREAT),(word) (PS_IWRITE | PS_IREAD) );
#endif
        if (!unicode_enabled)
            out_fd = po_open(rtfs_args_val_text(1),(word) (PO_BINARY|PO_WRONLY|PO_CREAT),(word) (PS_IWRITE | PS_IREAD) );

        if (in_fd < 0)
        {
            DISPLAY_ERRNO("po_open")
            RTFS_PRINT_STRING_2((byte *)"Cant open file: ", rtfs_args_val_text(0),PRFLG_NL); /* "Cant open file: " */
            return(-1);
        }
        if (out_fd < 0)
        {
            DISPLAY_ERRNO("po_open")
            RTFS_PRINT_STRING_2((byte *)"Cant open file", rtfs_args_val_text(1),PRFLG_NL); /* "Cant open file" */
            return(-1);
        }

        while (forever)
        {
			nloops += 1;
			if (copymetadatonly)
		        nread = po_read(in_fd,(byte*)0, COPYBUFFSIZE);
			else
				nread = po_read(in_fd,(byte*)copy_buffer, COPYBUFFSIZE);
            if (nread < 0)
            {
				RTFS_PRINT_STRING_1((byte *)"Read failure ",PRFLG_NL); /* "Write failure " */
                DISPLAY_ERRNO("po_read")
                break;
            }
            else if (nread > 0)
            {
				if (copymetadatonly)
				{
					if (po_write(out_fd,(byte*)0,nread) != (int)nread)
					{
						RTFS_PRINT_STRING_1((byte *)"Write failure ",PRFLG_NL); /* "Write failure " */
						break;
					}
				}
				else
				{
					if (po_write(out_fd,(byte*)copy_buffer,nread) != (int)nread)
					{
						RTFS_PRINT_STRING_1((byte *)"Write failure ",PRFLG_NL); /* "Write failure " */
						break;
					}
				}

            }
            else
                break;
        }
        po_close(in_fd);
		po_close(out_fd);
        return(0);
    }
    else
    {
        RTFS_PRINT_STRING_1((byte *)"Usage: COPY FROMPATH TOPATH",PRFLG_NL); /* "Usage: COPY FROMPATH TOPATH" */
        return(-1);
    }
}


/* FILLFILE PATH PATTERN NTIMES */
/*
<TEST>  Procedure:   dofillfile() - Create a file and fill it with a pattern. Useful for debugging, the file can later be read, copied, moved deleted etc.
<TEST>   Invoke by typing "FILLFILE" in the command shell
<TEST>
*/


int dofillfile(int agc, byte **agv)                                 /*__fn__*/
{
    int out_fd = -1;
    byte  workbuf[255];
    word bufflen;
    int ncopies;



    if (parse_args(agc, agv,"TTI"))
    {
        ncopies = (int) rtfs_args_val_lo(2);
        if (!ncopies)
            ncopies = 1;

#if (INCLUDE_CS_UNICODE)
        if (unicode_enabled)
            out_fd = po_open_uc(rtfs_args_val_utext(0,0),(word) (PO_TRUNC|PO_BINARY|PO_WRONLY|PO_CREAT),(word) (PS_IWRITE | PS_IREAD) );
#endif
        if (!unicode_enabled)
            out_fd = po_open(rtfs_args_val_text(0),(word) (PO_TRUNC|PO_BINARY|PO_WRONLY|PO_CREAT),(word) (PS_IWRITE | PS_IREAD) );

        if (out_fd < 0)
        {
            DISPLAY_ERRNO("po_open")
            RTFS_PRINT_STRING_2((byte *)"Cant open file", rtfs_args_val_text(0), PRFLG_NL); /* "Cant open file" */
            return(-1);

        }
        rtfs_cs_strcpy(workbuf, rtfs_args_val_text(1), CS_CHARSET_NOT_UNICODE);
        rtfs_cs_strcat(workbuf, (byte *) "\r\n", CS_CHARSET_NOT_UNICODE);
        bufflen = (word) rtfs_cs_strlen(workbuf, CS_CHARSET_NOT_UNICODE);
        while (ncopies--)
        {
            if (po_write(out_fd,(byte*)workbuf,(word)bufflen) != (int)bufflen)
            {
                DISPLAY_ERRNO("po_write")
                RTFS_PRINT_STRING_1((byte *)"Write failure", PRFLG_NL); /* "Write failure" */
                po_close(out_fd);
                return(-1);
            }
        }
        po_close(out_fd);
        return(0);
    }
    else
    {
        RTFS_PRINT_STRING_1((byte *)"Usage: FILLFILE PATH PATTERN NTIMES", PRFLG_NL); /* "Usage: FILLFILE PATH PATTERN NTIMES" */
        return(-1);
    }
}



int dodiskflush(int agc, byte **agv)
{
    if (parse_args(agc, agv,"T"))
    {
        if (!pc_diskflush((byte *)rtfs_args_val_text(0)))
        {
            RTFS_PRINT_STRING_1((byte *)"Flush Failed ", PRFLG_NL);
            return(-1);
        }
        return(0);
    }
    else
    {
         RTFS_PRINT_STRING_1((byte *)"Usage: DSKFLUSH D: ", PRFLG_NL);
         return(-1);
    }
}

#if (INCLUDE_RTFS_PROPLUS)
/*
<TEST>  Procedure:   do_show_disk_free() - Call pc_diskio_free_list() to retrieve and display information about the freespace on current volume
<TEST>   Invoke by typing "SHOWDISKFREE" in the command shell
<TEST>
*/

int do_show_disk_free(int agc, byte **agv)
{
FREELISTINFO *plist;
FREELISTINFO info_element;
int n_fragments, list_size;
BOOLEAN verbose;

    verbose  = FALSE;
    if (!parse_args(agc, agv,"TB"))
    {
        EFISHELL_USAGE("usage:   SHOWDISKFREE DRIVEID: <Y = verbose>");
        return(-1);
    }
    verbose  = rtfs_args_val_lo(1);

    /* How many fragments are there ? */
    n_fragments = pc_diskio_free_list((byte *)rtfs_args_val_text(0), 1, &info_element, 0, 0, 1);
    if (n_fragments  < 0)
    {
        EFISHELL_WARN("pc_diskio_free_list failed ");
        return(-1);
    }
    EFISHELL_SHOWINTNL("Free Fragments on drive   :    ", (dword) n_fragments);

    plist = (FREELISTINFO *) shell_buf;
    list_size = sizeof(shell_buf)/sizeof(*plist);
    if (n_fragments > list_size)
    {
        EFISHELL_SHOWINTNL("Not all fragments will be displayed because free list buffer size is:  ", (dword) list_size);
    }
    /* Now get the fragments */
    n_fragments = pc_diskio_free_list((byte *)rtfs_args_val_text(0), list_size, plist, 0, 0, 1);
    if (n_fragments  < 0)
    {
        EFISHELL_WARN("pc_diskio_free_list failed ");
    }
    else
    {
        int i,linecount,fragcount,sector_size;
        FREELISTINFO *plist_iterator;
        dword largest_fragment, free_clusters, free_blocks,largest_fragment_blocks;
        largest_fragment = 0;
        free_clusters = 0;


        plist_iterator = plist;
        for (i = 0; i < n_fragments; i++)
        {
            free_clusters += plist_iterator->nclusters;
            if (plist_iterator->nclusters > largest_fragment)
                largest_fragment = plist_iterator->nclusters;
            plist_iterator++;
        }


        EFISHELL_SHOWINTNL("Free clusters on drive    :    ", free_clusters);
        EFISHELL_SHOWINTNL("Largest fragment on drive :    ", largest_fragment);
        sector_size = pc_sector_size((byte *)rtfs_args_val_text(0));
        if (sector_size)
        {
            free_blocks = pc_cluster_size((byte *)rtfs_args_val_text(0))/sector_size;
            free_blocks *= free_clusters;
            EFISHELL_SHOWINTNL("Free Blocks   on drive    :    ", free_blocks);
            largest_fragment_blocks = pc_cluster_size((byte *)rtfs_args_val_text(0))/sector_size;
            largest_fragment_blocks *= largest_fragment;
            EFISHELL_SHOWINTNL("Largest fragment on drive :    ", largest_fragment_blocks);
        }
        if (verbose)
        {
            plist_iterator = plist;
            linecount = 4;
            fragcount = 0;
            for (i = 0; i < n_fragments; i++)
            {
                fragcount += 1;
                rtfs_print_one_string((byte *)"( ",0);
                rtfs_print_long_1(plist_iterator->cluster,0);
                rtfs_print_one_string((byte *)" - ",0);
                rtfs_print_long_1(plist_iterator->cluster+plist_iterator->nclusters-1,0);
                if (fragcount < 4)
                    rtfs_print_one_string((byte *)")",0);
                else
                {
                    rtfs_print_one_string((byte *)")",PRFLG_NL);
                    if (linecount++ > 16)
                    {
                        pause();
                        linecount = 0;
                    }
                }
                plist_iterator++;
            }
            rtfs_print_one_string((byte *)" ",PRFLG_NL);
            pause();
        }
    }
    return(0);
}
#endif
/* GETATTR PATH*/
int dosetattr(int agc, byte **agv)                                    /*__fn__*/
{
byte attr;
BOOLEAN ret_val = FALSE;

    attr = 0;

    if (parse_args(agc, agv,"TT"))
    {
#if (INCLUDE_CS_UNICODE)
        if (unicode_enabled)
            ret_val = pc_get_attributes_uc(rtfs_args_val_utext(0,0), &attr);
#endif
        if (!unicode_enabled)
            ret_val = pc_get_attributes(rtfs_args_val_text(0), &attr);
        if (!ret_val)
        {
            DISPLAY_ERRNO("pc_get_attributes")
            RTFS_PRINT_STRING_1((byte *)"Can not get attributes", PRFLG_NL); /* "Can not get attributes" */
            return(-1);
        }
        attr &= ~(ARDONLY|AHIDDEN|ASYSTEM|ARCHIVE);
        if (rtfs_cs_strcmp(rtfs_args_val_text(1), (byte *)"RDONLY", CS_CHARSET_NOT_UNICODE)==0) /* "RDONLY" */
            attr |= ARDONLY;
        else if (rtfs_cs_strcmp(rtfs_args_val_text(1), (byte *)"HIDDEN", CS_CHARSET_NOT_UNICODE)==0) /* "HIDDEN" */
            attr |= AHIDDEN;
        else if (rtfs_cs_strcmp(rtfs_args_val_text(1), (byte *)"SYSTEM", CS_CHARSET_NOT_UNICODE)==0) /* "SYSTEM" */
            attr |= ASYSTEM;
        else if (rtfs_cs_strcmp(rtfs_args_val_text(1),(byte *)"ARCHIVE", CS_CHARSET_NOT_UNICODE)==0) /* "ARCHIVE" */
            attr |= ARCHIVE;
        else if (rtfs_cs_strcmp(rtfs_args_val_text(1),(byte *)"NORMAL", CS_CHARSET_NOT_UNICODE)==0) /* "NORMAL" */
            attr =  ANORMAL;
        else
            goto usage;
        if (pc_set_attributes(rtfs_args_val_text(0), attr))
            return(0);
        else
        {
            DISPLAY_ERRNO("pc_set_attributes")
            RTFS_PRINT_STRING_1((byte *)"Set attributes failed", PRFLG_NL); /* "Set attributes failed" */
            return(-1);
        }
    }
usage:
    RTFS_PRINT_STRING_1((byte *)"Usage: SETATTR D:PATH RDONLY|HIDDEN|SYSTEM|ARCHIVE|NORMAL", PRFLG_NL); /* "Usage: SETATTR D:PATH RDONLY|HIDDEN|SYSTEM|ARCHIVE|NORMAL" */

    return (0);
}

int dosetvol(int agc, byte **agv)                                    /*__fn__*/
{
    if (parse_args(agc, agv,"TT"))
    {
        if (pc_set_volume(rtfs_args_val_text(0), rtfs_args_val_text(1)))
            return(0);
        else
        {
            DISPLAY_ERRNO("pc_set_volume")
            RTFS_PRINT_STRING_1((byte *)"Set volume label failed", PRFLG_NL);
            return(-1);
        }
    }
    RTFS_PRINT_STRING_1((byte *)"Usage: SETVOL D: LABEL", PRFLG_NL);
    return (0);
}

/* Measure create performance with modifications */
static void _test_open_speed(byte *basename, int first, int nfiles)
{
dword time_zero, elapsed_time;
int i,fd;

   if (!nfiles)
        return;
   fd = -1;

    time_zero = rtfs_port_elapsed_zero();
    for (i = 0; i < nfiles; i++)
    {
#if (INCLUDE_CS_UNICODE)
        if (unicode_enabled)
        {
            byte number_part_ascii[32];
            byte number_part_unicode[32];
            pc_ltoa((dword)(i+first), &number_part_ascii[0], 10);
            map_jis_ascii_to_unicode(number_part_unicode, number_part_ascii);
            map_jis_ascii_to_unicode(shell_buf, basename);
            rtfs_cs_strcat(shell_buf, &number_part_unicode[0], CS_CHARSET_UNICODE);
            fd = po_open_uc((byte *) shell_buf, PO_RDWR|PO_CREAT, PS_IWRITE|PS_IREAD);
        }
#endif
        if (!unicode_enabled)
        {
            byte number_part[32];
            rtfs_cs_strcpy(shell_buf, basename, CS_CHARSET_NOT_UNICODE);
            rtfs_cs_strcat(shell_buf, pc_ltoa((dword)(i+first), &number_part[0], 10), CS_CHARSET_NOT_UNICODE);
            fd = po_open((byte *) shell_buf, PO_RDWR|PO_CREAT, PS_IWRITE|PS_IREAD);
        }
        if (fd < 0)
        {
            rtfs_print_one_string((byte *)"  Test failed on filename: ",0);
            rtfs_print_one_string(shell_buf, PRFLG_NL);
            return;
        }
        po_close(fd);
    }
    elapsed_time = rtfs_port_elapsed_zero() - time_zero;

    rtfs_print_one_string((byte *)"  Elapsed Time, miliseconds: ",0);
    rtfs_print_long_1(elapsed_time, PRFLG_NL);
    rtfs_print_one_string((byte *)"  Average Time, miliseconds: ",0);
    rtfs_print_long_1(elapsed_time/nfiles, PRFLG_NL);
}
/*
<TEST>  Procedure:   dotestopenspeed(int agc, byte **agv)
<TEST>  Description: Creates or re-opens up to 1000 files in a subdirectory.
<TEST>  Reports total elapse time and average time per file.
<TEST>  May be invoked by executing the OPENSPEED command from the command shell
*/

int dotestopenspeed(int agc, byte **agv)                                    /*__fn__*/
{

    if (!parse_args(agc, agv,"TII"))
    {
        RTFS_PRINT_STRING_1((byte *)"Usage: OPENSPEED basename startnum numfiles ", PRFLG_NL);
        RTFS_PRINT_STRING_1((byte *)"Usage: Opens or creates numfiles ", PRFLG_NL);
        RTFS_PRINT_STRING_1((byte *)"Usage: files name basnamestartnum, basenamestartnum+1 ..", PRFLG_NL);
        return(-1);
    }
    _test_open_speed(rtfs_args_val_text(0), (int)rtfs_args_val_lo(1),(int)rtfs_args_val_lo(2));
    return(0);
}


#ifdef _MSC_VER
#if (INCLUDE_HOSTDISK)
#if (INCLUDE_V_1_0_CONFIG==0)
void scanwindir(unsigned char *winpath);
void importwindir(unsigned char *winpath,unsigned char *rtfspath);

int dowindir(int agc, byte **agv)                                    /*__fn__*/
{

    if (!parse_args(agc, agv,"T"))
    {
        RTFS_PRINT_STRING_1((byte *)"Usage: WINSCAN pathname", PRFLG_NL);
        RTFS_PRINT_STRING_1((byte *)"Usage: Scans host path and recommends format prameters", PRFLG_NL);
        return(-1);
    }
    scanwindir(rtfs_args_val_text(0));
	return(0);
}
int dowinimport(int agc, byte **agv)                                    /*__fn__*/
{

    if (!parse_args(agc, agv,"TT"))
    {
        RTFS_PRINT_STRING_1((byte *)"Usage: WINIMPORT pathname D:", PRFLG_NL);
        RTFS_PRINT_STRING_1((byte *)"Usage: Scans host path and populates D: from contents", PRFLG_NL);
        return(-1);
    }
    importwindir(rtfs_args_val_text(0),rtfs_args_val_text(1));
	return(0);
}
#endif
#endif
#endif


#if (INCLUDE_MATH64)

#define SPEED_TEST_MODE 1
#if (!(INCLUDE_EXFATORFAT64||INCLUDE_RTFS_PROPLUS))
/* We can still use the speed 64 math if we provide this routin */
ddword m64_native_set32(dword a, dword b)
{
ddword _c,_a,_b;
    _a = (ddword) a;
    _b = (ddword) b;
    _c = (_a<<32) | _b;
    return(_c);
}
#endif
int dofillhugefile(int agc, byte **agv)                                 /*__fn__*/
{
    int out_fd = -1;
	dword time_zero, elapsed_time;
    if (parse_args(agc, agv,"TIIII") && rtfs_args_arg_count() == 6)
    {
/*         RTFS_PRINT_STRING_1((byte *)"Usage: FILLHUGEFILE Filename DOMETADATAONLY dopattern buffersizekbytes  GIGABYTES BYTES", PRFLG_NL); */
		int dofill,dometaonly,buffersizebytes,allocedsizebytes,currentwritepointer;
		dword gigabytes,bytes;
		byte *allocedbuffer,*writebuffer;
		ddword bytes64,byteswritten64,fillvalue,nextfillvalue,buffersizebytesddw,bytesleftddw;
		BOOLEAN refreshbuffer;

		dometaonly = (int) rtfs_args_val_lo(0);
		dofill = (int) rtfs_args_val_lo(1);
		buffersizebytes = (int) rtfs_args_val_lo(3);
		gigabytes = rtfs_args_val_lo(4);
		bytes =  	rtfs_args_val_lo(5);

		buffersizebytesddw = M64SET32(0,buffersizebytes);
		bytes64 = M64SET32(0,gigabytes);
		bytes64 = M64LSHIFT(bytes64,30);
		bytes64 = M64PLUS32(bytes64,bytes);
		if (buffersizebytes >= 1024)
			allocedsizebytes = buffersizebytes;
		else
			allocedsizebytes = 1024;
		if (dometaonly)
		{
			writebuffer = 0;
			allocedbuffer = 0;
		}
		else
		{
			allocedbuffer = (byte *)rtfs_port_malloc(allocedsizebytes);
			if (!allocedbuffer)
				return -1;
			writebuffer = allocedbuffer;
		}
#if (SPEED_TEST_MODE)
		time_zero = rtfs_port_elapsed_zero();
#endif
#if (INCLUDE_CS_UNICODE)
        if (unicode_enabled)
            out_fd = po_open_uc(rtfs_args_val_utext(0,0),(word) (PO_TRUNC|PO_BINARY|PO_WRONLY|PO_CREAT),(word) (PS_IWRITE | PS_IREAD) );
#endif
        if (!unicode_enabled)
            out_fd = po_open(rtfs_args_val_text(0),(word) (PO_TRUNC|PO_BINARY|PO_WRONLY|PO_CREAT),(word) (PS_IWRITE | PS_IREAD) );

        if (out_fd < 0)
        {
            DISPLAY_ERRNO("po_open")
            RTFS_PRINT_STRING_2((byte *)"Cant open file", rtfs_args_val_text(0), PRFLG_NL); /* "Cant open file" */
            return(-1);
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

				refreshbuffer=dofill;

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
					DISPLAY_ERRNO("po_write")
					RTFS_PRINT_STRING_1((byte *)"Write failure", PRFLG_NL); /* "Write failure" */
					goto return_error;
				}
			}
			byteswritten64 = M64PLUS32(byteswritten64,buffersizebytes);
			bytesleftddw =   M64MINUS32(bytesleftddw,buffersizebytes);

        }
#if (SPEED_TEST_MODE)
		elapsed_time = (rtfs_port_elapsed_zero() - time_zero)/1000;

		rtfs_print_one_string((byte *)"  Pre-close Elapsed Time, seconds: ",0);
		rtfs_print_long_1(elapsed_time, PRFLG_NL);
#endif
        po_close(out_fd);
#if (SPEED_TEST_MODE)
		elapsed_time = (rtfs_port_elapsed_zero() - time_zero)/1000;

		rtfs_print_one_string((byte *)"  Total Time, seconds: ",0);
		rtfs_print_long_1(elapsed_time, PRFLG_NL);
#endif
		if (allocedbuffer)
			rtfs_port_free(allocedbuffer);
		RTFS_PRINT_STRING_1((byte *)"File fill succeeded", PRFLG_NL); /* "Write failure" */

        return(0);
return_error:
		RTFS_PRINT_STRING_1((byte *)"File fill failed", PRFLG_NL); /* "Write failure" */
        po_close(out_fd);
		if (allocedbuffer)
			rtfs_port_free(allocedbuffer);
        return(-1);
    }
    else
    {
        RTFS_PRINT_STRING_1((byte *)"    Usage: FILLHUGEFILE Filename FILLPATTERN DOMETADATAONLY buffersizebytes  GIGABYTES BYTES", PRFLG_NL);
        RTFS_PRINT_STRING_1((byte *)"       (file length = (GIGABYTES*1073741824)+bytes", PRFLG_NL);
        RTFS_PRINT_STRING_1((byte *)"    Example: FILLHUGEFILE myfile.dat 1 0 32768 1 0", PRFLG_NL);
        RTFS_PRINT_STRING_1((byte *)"       Write a pattern in 32 K chunks into a file 1073741824 bytes long", PRFLG_NL);
        RTFS_PRINT_STRING_1((byte *)"    Example: FILLHUGEFILE myfile.dat 0 1 131072 1 1000", PRFLG_NL);
        RTFS_PRINT_STRING_1((byte *)"       Extends a file in 128 K increments to 1073742824 bytes long without writing", PRFLG_NL);
        RTFS_PRINT_STRING_1((byte *)"    Example: FILLHUGEFILE myfile.dat 1 0 1 0 1000", PRFLG_NL);
        RTFS_PRINT_STRING_1((byte *)"      Write a pattern 1 bytes at a time into a file 1000 bytes long", PRFLG_NL);
        return(-1);
    }
}
int doreadhugefile(int agc, byte **agv)                                 /*__fn__*/
{
    int in_fd = -1;
	dword time_zero, elapsed_time;
	/*     READHUGEFILE Filename DOMETADATAONLY(1/0) DOCOMPARE(1/0) buffersizebytes */

    if (parse_args(agc, agv,"TIII") && rtfs_args_arg_count() == 4)
    {
		int dometaonly,docompare,buffersizebytes,allocedsizebytes,currentreadpointer;
		byte *allocedbuffer,*readbuffer;
		ddword bytes64,bytesread64,testvalue,nexttestvalue,bytesleftddw,buffersizebytesddw;
		BOOLEAN comparebuffer;

		dometaonly = (int) rtfs_args_val_lo(1);
		docompare = (int) rtfs_args_val_lo(2);
		buffersizebytes = (int) rtfs_args_val_lo(3);
		buffersizebytesddw = M64SET32(0,buffersizebytes);

		if (buffersizebytes >= 1024)
			allocedsizebytes = buffersizebytes;
		else
			allocedsizebytes = 1024;
		if (dometaonly)
		{
			readbuffer = 0;
			allocedbuffer = 0;
		}
		else
		{
			allocedbuffer = (byte *)rtfs_port_malloc(allocedsizebytes);
			if (!allocedbuffer)
				return -1;
			readbuffer = allocedbuffer;
		}
#if (SPEED_TEST_MODE)
		time_zero = rtfs_port_elapsed_zero();
#endif
#if (INCLUDE_CS_UNICODE)
        if (unicode_enabled)
            in_fd = po_open_uc(rtfs_args_val_utext(0,0),(word) (PO_BINARY|PO_RDONLY),0 );
#endif
        if (!unicode_enabled)
            in_fd = po_open(rtfs_args_val_text(0),(word) (PO_BINARY|PO_RDONLY),0);

        if (in_fd < 0)
        {
            DISPLAY_ERRNO("po_open")
            RTFS_PRINT_STRING_2((byte *)"Cant open file", rtfs_args_val_text(0), PRFLG_NL); /* "Cant open file" */
            return(-1);
        }
#if (SPEED_TEST_MODE)
		elapsed_time = (rtfs_port_elapsed_zero() - time_zero);
		rtfs_print_one_string((byte *)"  Open call Elapsed Time, miliseconds: ",0);
		rtfs_print_long_1(elapsed_time, PRFLG_NL);
#endif
		bytes64 = M64SET32(0,0);
		{
		ERTFS_STAT st;
			if (pc_fstat(in_fd, &st) != 0)
			{
				DISPLAY_ERRNO("pc_stat")
				RTFS_PRINT_STRING_2((byte *)"Cant fstat file", rtfs_args_val_text(0), PRFLG_NL); /* "Cant open file" */
				return(-1);
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
						if(!M64EQ(*pdw,testvalue))
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
#if (SPEED_TEST_MODE)
		elapsed_time = (rtfs_port_elapsed_zero() - time_zero)/1000;
		rtfs_print_one_string((byte *)"  Total Time, seconds: ",0);
		rtfs_print_long_1(elapsed_time, PRFLG_NL);
#endif
        return(0);
return_error:
		RTFS_PRINT_STRING_1((byte *)"File read failed", PRFLG_NL); /* "Write failure" */
        po_close(in_fd);
		if (allocedbuffer)
			rtfs_port_free(allocedbuffer);
        return(-1);
    }
    else
    {
        RTFS_PRINT_STRING_1((byte *)"    Usage: READHUGEFILE Filename DOMETADATAONLY(1/0) DOCOMPARE(1/0) buffersizebytes", PRFLG_NL);
        RTFS_PRINT_STRING_1((byte *)"    Example: READHUGEFILE myfile.dat 0 1 32768", PRFLG_NL);
        RTFS_PRINT_STRING_1((byte *)"       Read a pattern in 32 K chunks into a file 1073741824 bytes long, check pattern", PRFLG_NL);
        RTFS_PRINT_STRING_1((byte *)"    Example: READHUGEFILE myfile.dat 1 0 131072", PRFLG_NL);
        RTFS_PRINT_STRING_1((byte *)"       Read a file in 128 K increments without transferring data or comparing", PRFLG_NL);
        RTFS_PRINT_STRING_1((byte *)"    Example: READHUGEFILE myfile.dat 0 1 1", PRFLG_NL);
        RTFS_PRINT_STRING_1((byte *)"      Read a pattern 1 byte at a time into a file and compare to a patern", PRFLG_NL);
        return(-1);
    }
}

#endif /* EXFAT */


#endif /* Exclude from build if read only */
