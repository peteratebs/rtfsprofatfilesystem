/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS 1993 - 2008
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/****************************************************************************
CHKDSK.C   - Check Files System Integrity

  Summary

  	BOOLEAN pc_check_disk_ex(byte *drive_id, CHKDISK_STATS *pstat, int verbose, int fix_problems, int write_chains, CHKDSK_CONTEXT *pgl, void *scratch_memory, dword scratch_memory_size)

****************************************************************************/

#include "rtfs.h"


#define CHKDSK_FAT_EOF_VAL   0xffffffff
#define CHKDSK_FAT_ERROR_VAL 0xfffffffe
#define CHKDSK_TERMINATOR	 	0x80000000
#define CHKDSK_NOT_TERMINATOR	0x7FFFFFFF

#define CHKDSK_LEAN_RECURSION_METHOD	1

#if (CHKDSK_LEAN_RECURSION_METHOD)
#define MAX_RECURSION_DEPTH 128 /* 8 */
#else
#define MAX_RECURSION_DEPTH 16 /* 8 */
#endif

#ifdef RTFS_MAJOR_VERSION

/* Version 6 does not use a string table */
#define STRTABLE_TYPE byte *
#define USTRING_CHKDSK_03 (byte *)"Failed Scanning Disk Files"
#define USTRING_CHKDSK_04 (byte *)"Failed Scanning Fat"
#define USTRING_CHKDSK_06 (byte *)"     Failed Creating .CHK Files"
#define USTRING_CHKDSK_41 (byte *)"Write Failed"
#define USTRING_CHKDSK_42 (byte *)"Entry has crossed chain : "

#define USTRING_CHKDSK_52 (byte *)"Failed Writing This Adusted File:    "
#define USTRING_CHKDSK_45 (byte *)"Path too deep , directory == "
#define USTRING_CHKDSK_46 (byte *)"Failed Scanning This Directory on LFN Pass  -"
#define USTRING_CHKDSK_47 (byte *)"Failed Scanning This Directory -"

#define USTRING_SYS_NULL  (byte *)""
#define USTRING_CHKDSK_49 (byte *)"    "
#define USTRING_CHKDSK_50 (byte *)"Failed Scanning This File "
#define USTRING_CHKDSK_51 (byte *)"Failed Scanning This Directory -"
#define USTRING_CHKDSK_63 (byte *)"Chkdsk found looped cluster chain and stopped, correct with fix option"
#define USTRING_CHKDSK_52 (byte *)"Failed Writing This Adusted File:    "
#define USTRING_CHKDSK_62 (byte *)"Chkdsk gives up, unexplained endless loop"

/* Version 6 drive info access */
#define DRIVE_INFO(D,F) D->drive_info.F

#define USTRING_DOTDOT (byte *) ".."

/* Version 4 get current directory object */
#define GET_CURRENT_DIROBJ(DR) _pc_get_user_cwd(DR)

#else
/* Rtfs 4 uses VFAT instead of INCLUDE_VFAT */
#define INCLUDE_VFAT VFAT
/* Rtfs 4 emulations of Rtfs 6 functions and structures */
typedef struct region_fragment {
        dword start_location;
        dword end_location;
        struct region_fragment *pnext;
        } REGION_FRAGMENT;
#define fatop_flushfat(D) FATOP(D)->fatop_flushfat(D->driveno)
#define fatop_get_cluster(D, CLUSTER, NEXT) FATOP(D)->fatop_faxx(D, CLUSTER, NEXT)
#define fatop_pfaxxterm(D, CLUSTER) FATOP(D)->fatop_pfaxxterm(D, CLUSTER)
#define fatop_pfaxx(D, CLUSTER, VALUE) FATOP(D)->fatop_pfaxx(D, CLUSTER, VALUE)
#define fatop_freechain(D, CLUSTER, MAX_CLUSTERS) FATOP(D)->fatop_freechain(D, CLUSTER, 0, MAX_CLUSTERS)
#define pc_set_file_dirty(F,V) F->needs_flush = V
/* Version 4 drive info access */
#define DRIVE_INFO(D,F) D->F
#define STRTABLE_TYPE int
/* Version 4 get current directory object */
#define GET_CURRENT_DIROBJ(DR) pc_get_cwd(DR)

#if (INCLUDE_CS_UNICODE)
#define USTRING_DOTDOT (byte *) L".."
#else
#define USTRING_DOTDOT (byte *) ".."
#endif
#endif

void chkdsk_print_string_1(CHKDSK_CONTEXT *pgl,STRTABLE_TYPE str, int control_code)
{
	if (pgl->chkdsk_opmode & CHKDSK_VERBOSE)
	{
		RTFS_PRINT_STRING_1(str,control_code);
	}

}
void chkdsk_print_string_2(CHKDSK_CONTEXT *pgl,STRTABLE_TYPE str1, byte *str2, int control_code)
{
    if (pgl->chkdsk_opmode & CHKDSK_VERBOSE)
	{
		RTFS_PRINT_STRING_2(str1, str2, control_code);
	}
}
#define CHKDSK_DOFIXPROBLEMS(P) (P->chkdsk_opmode&CHKDSK_FIXPROBLEMS)
#define CHKDSK_MAKECHECKFILES(P) ((P->chkdsk_opmode&CHKDSK_FREELOSTCLUSTERS) == 0)

#define CHKDSK_FREEBROKENFILES(P) ((P->chkdsk_opmode&CHKDSK_FIXPROBLEMS) && (P->chkdsk_opmode&CHKDSK_FREEFILESWITHERRORS))
#define CHKDSK_FREEBROKENDIRECTORIES(P) ((P->chkdsk_opmode&CHKDSK_FIXPROBLEMS) && (P->chkdsk_opmode&CHKDSK_FREESUBDIRSWITHERRORS))


/* Function prototypes                                                  */
static BOOLEAN chkdsk_remount(CHKDSK_CONTEXT *pgl, byte *drive_id, BOOLEAN starting_checkdisk, void *failsafehandle, int *drive_number, void **retfailsafehandle);
static BOOLEAN chkdsk_process_crossed_chains(CHKDSK_CONTEXT *pgl);
static BOOLEAN scan_all_files(CHKDSK_CONTEXT *pgl, byte *dir_name);
static BOOLEAN process_find_crossed_chains(CHKDSK_CONTEXT *pgl, byte *dirent_name, DROBJ *pobj);
static BOOLEAN process_directory_entry(CHKDSK_CONTEXT *pgl, DROBJ *pobj, byte *filename);
static BOOLEAN check_lost_clusters(CHKDSK_CONTEXT *pgl, DDRIVE  *pdr);
static dword chkdsk_chain_size_clusters(CHKDSK_CONTEXT *pgl, dword cluster);
static dword chkdsk_chain_size_bytes(CHKDSK_CONTEXT *pgl, dword cluster);
static dword chkdsk_clusterstobytes(DDRIVE *pdr,dword n_clusters);
static BOOLEAN chkdsk_free_chain(CHKDSK_CONTEXT *pgl, dword cluster);
static BOOLEAN chkdsk_init_region_heap(CHKDSK_CONTEXT *pgl, void *scratch_memory, dword scratch_memory_size);
static BOOLEAN chkdsk_set_cluster_used(CHKDSK_CONTEXT *pgl, dword cluster, REGION_FRAGMENT *pf_to_use);
static BOOLEAN chkdsk_check_cluster_used(CHKDSK_CONTEXT *pgl, dword cluster);
static dword chkdsk_midpoint_usedmap(CHKDSK_CONTEXT *pgl);
static BOOLEAN chkdsk_add_lost_cluster(CHKDSK_CONTEXT *pgl, dword cluster, dword value, BOOLEAN is_terminator);
static BOOLEAN chkdsk_make_check_dir(void);
static int chkdsk_write_check_files(CHKDSK_CONTEXT *pgl);
static BOOLEAN chkdsk_build_chk_file(CHKDSK_CONTEXT *pgl, dword chain_start_cluster, dword current_file_no, dword *ret_file_no);
static void free_lost_chain_list(CHKDSK_CONTEXT *pgl);
static void free_mapped_chain_list(CHKDSK_CONTEXT *pgl);
static REGION_FRAGMENT *chkdsk_allocate_fragment(CHKDSK_CONTEXT *pgl);
static void chkdsk_free_fragment(CHKDSK_CONTEXT *pgl,REGION_FRAGMENT *pf);

/* chkdsk_next_cluster() is externed so the regression test can access it */
dword chkdsk_next_cluster(DDRIVE *pdr, dword cluster);

/* VFAT specific code provided in this file */
#if (INCLUDE_VFAT)
static dword scan_for_bad_lfns(DROBJ *pmom, int delete_bad_lfn);
#endif
static dword chkdsk_get_dir_loop_guard(DDRIVE *pdr);
static dword chkdsk_get_chain_loop_guard(DDRIVE *pdr);


BOOLEAN pc_check_disk_ex(byte *drive_id, CHKDISK_STATS *pstat, dword chkdsk_opmode, CHKDSK_CONTEXT *pgl, void *scratch_memory, dword scratch_memory_size) /* __apifn__*/
{
    int drive_number;
    byte str_slash[8];
    BOOLEAN ret_val;
    byte *p;
	void *failsafehandle;
    int loop_guard = 1000;
    /* Initialize filesystem memory */
    CHECK_MEM(BOOLEAN, 0)    /* Make sure memory is initted */

	/* Check arguments */
	if (!pgl || !pstat)
		return(FALSE);
#ifndef RTFS_MAJOR_VERSION
	/* Version 4 requires scratch memory, for version 6 it is optional  */
	if (!scratch_memory_size || !scratch_memory)
		return(FALSE);
#endif

    ret_val = FALSE;
	/* Clear the return structure */
    rtfs_memset((byte *)pstat, 0, sizeof(*pstat));

    rtfs_memset((byte *)pgl, 0, sizeof(*pgl));
    pgl->chkdsk_opmode = chkdsk_opmode;
	pgl->pstats = pstat;

    /* Now make \\ in native char set */
    p = &str_slash[0];
#ifdef RTFS_MAJOR_VERSION
    CS_OP_ASSIGN_ASCII(p,'\\',CS_CHARSET_NOT_UNICODE);
    CS_OP_INC_PTR(p, CS_CHARSET_NOT_UNICODE);
    CS_OP_TERM_STRING(p, CS_CHARSET_NOT_UNICODE);
#else
    CS_OP_ASSIGN_ASCII(p,'\\');
	CS_OP_INC_PTR(p);
    CS_OP_TERM_STRING(p);
#endif


    pc_diskflush(drive_id);
     /* Remount the drive, disable Failsafe and free manager, remember previous failsafe and free manager settings. set pgl->drive_structure. */
	if (!chkdsk_remount(pgl, drive_id, TRUE, 0, &drive_number, &failsafehandle))
		return(FALSE);
    if (!pc_set_default_drive(drive_id))
		return(FALSE);

	/* If scratch memory was provided, link it into a free list. Version 6 may default to using memory from the free manager. Rtfs 4 must provide scratch memory */
	if (!chkdsk_init_region_heap(pgl, scratch_memory, scratch_memory_size))
		return(FALSE);
	/* Copy useful information from the internal drive staruture to the return structure */
    pstat->n_sectors_total = DRIVE_INFO(pgl->drive_structure, numsecs);
    pstat->n_sectorspercluster = 1;
    pstat->n_sectorspercluster <<= (dword) DRIVE_INFO(pgl->drive_structure,log2_secpalloc);
    pstat->n_reservedrootsectors = (dword) DRIVE_INFO(pgl->drive_structure,secproot);
    pstat->n_clusters_total = (dword) DRIVE_INFO(pgl->drive_structure,maxfindex) -1;
#ifdef RTFS_MAJOR_VERSION
    pstat->n_bytespersector = (dword) DRIVE_INFO(pgl->drive_structure,bytespsector);
#else
    pstat->n_bytespersector = 512;
#endif

#if (INCLUDE_EXFATORFAT64)
	{
		if (ISEXFATORFAT64(pgl->drive_structure))
		{
 			ret_val=pc_exfat_check_disk(drive_id,pgl->drive_structure,pstat);
			/* Now remount the disk, if failsafehandle is non zero re-enable Failsafe */
			if (!chkdsk_remount(pgl, drive_id, FALSE, failsafehandle, &drive_number, &failsafehandle))
				ret_val= FALSE;
			return ret_val;
		}
	}
#endif
    pgl->recursion_depth = 0;

	/* On pass 1:
	     Check all files and directories for unterminated cluster chains, incorrect file sizes and looping cluster chains.
		 If problems are found and the fix option is requested
		 For subdirectory entries:
                If the directory entry contains an invalid cluster number the entry is removed.
                If the directory entry points to a cluster chain that is not terminated properly the cluster chain is terminated.
                If the directory entry points to a cluster chain that loops on itself, the chain is clipped to include only the first cluster, the
				rest of the clusters are discared.
		 For file entries:
                If the directory entry contains a non zero file size but no start cluster value, the directory entry is removed.
                If the directory entry points to a cluster chain that is not terminated properly the cluster chain is terminated.
                If the directory entry points to a cluster chain that loops on itself, the directory entry is removed and the
				rest of the clusters are discared.
	*/
    pgl->on_which_pass = 1;
    pgl->cl_start = 2;
    pgl->cl_end = DRIVE_INFO(pgl->drive_structure,maxfindex);

    while (pgl->cl_start < DRIVE_INFO(pgl->drive_structure,maxfindex))
    {
		pgl->scan_state = SC_STATE_PERFORM_SCAN;
		do
		{
        	pgl->pstats->n_user_files = 0;		 /* Clear these, they are recalculated on each pass */
        	pgl->pstats->n_hidden_files = 0;
        	pgl->pstats->n_user_directories = 0;

        	free_mapped_chain_list(pgl);					/* Clear the used cluster table */

			/* scan_all_files() runs in 2 or more passes
			   On pass 1, scan_all_files() identifies unterminated chains, directory entries that point to unallocated clusters,
			   and chains that loop ion themselves. If fix_problems is TRUE these problems are corrected by:
			    Delete files and directories whose first cluster fields points to an anallocated cluster
			    Terminate chains that are not terminated.
			    Delete files that contain cluster chains that loop on themselves
			    Truncate directories that contain cluster chains that loop on themselves
			   On pass 2 scan_all_files() identifies orphaned VFAT file name extensions and clears the if fix_problems is TRUE.
			   	If a crossed cluster chain is found and fix_problems is TRUE.
			   		The current directory entry and the crossed cluster are saved.
			   		The directory tree is rescanned to find the directory entry that crosses this entry.
					The cluster chains are terminated so the chains are no longer crossed.
					If a subdirectory crosses a file, the clusters after the cross point are cleaved from the file and left as \
					part of the subdirectory
			   	If lost clusters are found and fix_problems is TRUE.
			   		If write_chains if FALSE, the lost clusters are released.
			   		If write_chains if TRUE, lost cluster chains are accumulated on a list and are written to .CHK files after the scan completes.

					Note: pc_check_disk_ex() requires additional scans if it runs out of scratch memory needed to store a run length encoded map of
					all allocated clusters and lost file fragments.
			   */
#if (CHKDSK_LEAN_RECURSION_METHOD)
        	if (!pc_set_cwd(str_slash))
			{
           		chkdsk_print_string_1(pgl, USTRING_CHKDSK_03,PRFLG_NL); /* "Failed Scanning Disk Files" */
            	goto ex_it;
			}
#endif
        	pgl->pstats->n_directory_scans += 1;

        	if (!scan_all_files(pgl, str_slash))        	/* Build a used map and identify crossed files */
        	{
        		if (pgl->on_which_pass==1 && pgl->pstats->has_endless_loop && !CHKDSK_DOFIXPROBLEMS(pgl))
        		{ /* If an endless loop was detected and not repaired set endless loop status and exit */
        			ret_val = TRUE;
				}
				else
				{
            		chkdsk_print_string_1(pgl, USTRING_CHKDSK_03,PRFLG_NL); /* "Failed Scanning Disk Files" */
				}
#if (CHKDSK_LEAN_RECURSION_METHOD)
				pc_set_cwd(str_slash);
#endif
            	goto ex_it;
			}
#if (CHKDSK_LEAN_RECURSION_METHOD)
        	if (!pc_set_cwd(str_slash))
			{
           		chkdsk_print_string_1(pgl, USTRING_CHKDSK_03,PRFLG_NL); /* "Failed Scanning Disk Files" */
            	goto ex_it;
			}
#endif
			/* Note: pgl->scan_state will never be set in pass 1, only in subsequant passes */
			if (pgl->scan_state == SC_STATE_REQUEST_RESCAN_FOR_CROSSED_CHAIN)
			{
				/* Scan detected a crossed chain, restart the scan searching for the entry that crosses it */
				pgl->scan_state = SC_STATE_PERFORM_RESCAN_FOR_CROSSED_CHAIN;
			}
			else if (pgl->scan_state == SC_STATE_FOUND_RESCAN_CROSSED_CHAIN)
			{
				/* Scan searched for a crossed chain and found it. Perform the fix here	 */
				if (!chkdsk_process_crossed_chains(pgl))
					return(FALSE);
				/* Now restart the scan, if no crossed chains are found the scan will complete */
				pgl->crossed_cluster = 0;
				pgl->scan_state = SC_STATE_PERFORM_SCAN;
			}
			else if (pgl->scan_state == SC_STATE_PERFORM_RESCAN_FOR_CROSSED_CHAIN)
			{
				/* Scan searched for a crossed chain and did not find it. This should not happen, release
				   pgl->scan_crossed_drobj and return	FALSE;
				*/
				pc_freeobj(pgl->scan_crossed_drobj);
				goto ex_it;
			}
			else /* if (pgl->scan_state == SC_STATE_PERFORM_SCAN) */
				break;

		} while(--loop_guard);
		if (loop_guard==0)
		{
			chkdsk_print_string_1(pgl, USTRING_CHKDSK_62, PRFLG_NL); /* "Chkdsk gives up, unexplained endless loop" */
			return(FALSE);
		}
		/* The first pass only inspected cluster chains, it did not create a map of use clusters, process the cluster map
		   and advance the FAT scan window on subsequant passes */
        if (pgl->on_which_pass > 1)
		{
			dword cl_end;
			BOOLEAN rescan_required = FALSE;

			/* Save the end pont if check_lost clusters changes it that means it ran out of structures and we must rescan */
			cl_end = pgl->cl_end;
           	/* Now check if any allocated clusters are unaccounted for */
           	if (!check_lost_clusters(pgl, pgl->drive_structure))
           	{
           		chkdsk_print_string_1(pgl, USTRING_CHKDSK_04,PRFLG_NL); /* "Failed Scanning Fat" */
           		goto ex_it;
           	}
			if (cl_end != pgl->cl_end)
			{
				rescan_required = TRUE;
				if (pgl->cl_start > pgl->cl_end)
				{
					chkdsk_print_string_1(pgl, USTRING_CHKDSK_62, PRFLG_NL); /* "Chkdsk gives up, unexplained endless loop" */
					goto ex_it;
				}
			}
			else
			{
				/* If fixing problems and lost chains accumulated write them */
				if (CHKDSK_DOFIXPROBLEMS(pgl) && CHKDSK_MAKECHECKFILES(pgl) && pgl->lost_chain_list)
				{
					if (!chkdsk_make_check_dir() || chkdsk_write_check_files(pgl) < 1)
					{
           				chkdsk_print_string_1(pgl, USTRING_CHKDSK_06,PRFLG_NL); /* "     Failed Creating .CHK Files" */
           				goto ex_it;
					}
				}
			}

			if (!rescan_required)
			{
            	/* Break out if the end cluster was not reduced (this means all clusters fit in our map) */
            	if (pgl->cl_end == DRIVE_INFO(pgl->drive_structure,maxfindex))
            	{
					break;
            	}
            	else
            	{	/* Now scan from the end of this region to the end of the volume */
            		pgl->cl_start = pgl->cl_end+1;
            		pgl->cl_end = DRIVE_INFO(pgl->drive_structure,maxfindex);
            	}
			}
		}

        pgl->on_which_pass += 1;
		/* Release any structures tracking lost chains, and mapped chains the lists will be generated if there is another pass */
        free_lost_chain_list(pgl);
        free_mapped_chain_list(pgl);
    }

    ret_val = TRUE;
ex_it:
	/* Release any structures tracking lost chains */
	free_lost_chain_list(pgl);
	/* Release any structures tracking mapped chains */
	free_mapped_chain_list(pgl);
	/* Now remount the disk, if failsafehandle is non zero re-enable Failsafe */
	if (!chkdsk_remount(pgl, drive_id, FALSE, failsafehandle, &drive_number, &failsafehandle))
		return(FALSE);

    if (pstat->has_endless_loop || pstat->n_crossed_chains || pstat->n_lost_chains || pstat->n_lost_clusters ||
    	pstat->n_unterminated_chains || pstat->n_badcluster_values || pstat->n_bad_dirents || pstat->n_bad_lfns)
    	pstat->has_errors = TRUE;
    return (ret_val);
}

static BOOLEAN chkdsk_remount(CHKDSK_CONTEXT *pgl, byte *drive_id, BOOLEAN starting_checkdisk, void *failsafehandle, int *drive_number, void **retfailsafehandle)
{
	*retfailsafehandle = 0;
    /* Check that the disk is mounted or force a mount */
#ifdef RTFS_MAJOR_VERSION
    *drive_number = (int) check_drive_name_mount(drive_id,CS_CHARSET_NOT_UNICODE);
#else
    *drive_number = (int) check_drive_name_mount(drive_id);
#endif
    if (*drive_number < 0)
        return(FALSE);
    /* Release the lock. chkdsk is not atomic */
    release_drive_mount(*drive_number); /* Release lock, unmount if aborted */
    pgl->drive_structure = pc_drno2dr(*drive_number);

    if (starting_checkdisk)
	{
#if (INCLUDE_FAILSAFE_CODE)
#ifdef RTFS_MAJOR_VERSION
    	*retfailsafehandle = (void *) pgl->drive_structure->drive_state.failsafe_context;	/* Will be zero if disabled already */
    	if (!fs_api_disable(drive_id, FALSE))  /* Disables Failsafe if it was previously enabled */
			return(FALSE);
#else
		/* Save off the failsafe context and close the drive, disable failsafe */
		*retfailsafehandle = pgl->drive_structure->pfscntxt;
		if (*retfailsafehandle)
		{
    		if (!pro_failsafe_shutdown(drive_id, FALSE))
        		return(FALSE);
		}
		else
		{
   			pc_dskfree(*drive_number);
		}
#endif
#else
		pc_dskfree(*drive_number);
#endif
#ifdef RTFS_MAJOR_VERSION
    	if (check_drive_name_mount(drive_id,CS_CHARSET_NOT_UNICODE) < 0)
#else
    	if (check_drive_name_mount(drive_id) < 0)
#endif
        	return(FALSE);

#ifdef RTFS_MAJOR_VERSION
#if (INCLUDE_RTFS_FREEMANAGER)  /* fatop_close_driver() release free regions */
		free_manager_close(pgl->drive_structure); /* Close the free manager, releases fragment structures that may be used by check disk */
#endif
#endif
   		release_drive_mount(*drive_number); /* Release lock */
    }
	else /* Remounting after check disk */
	{
		pc_dskfree(*drive_number);
#ifdef RTFS_MAJOR_VERSION
    	if (check_drive_name_mount(drive_id,CS_CHARSET_NOT_UNICODE) < 0)
#else
    	if (check_drive_name_mount(drive_id) < 0)
#endif
        	return(FALSE);
		/* Release the lock. chkdsk is not atomic */
        release_drive_mount(*drive_number); /* Release lock, unmount if aborted */

	/* If failsafe was disabled to run check disk, re-enable it here. */
    	if (failsafehandle)
    	{
#if (INCLUDE_FAILSAFE_CODE)
#ifdef RTFS_MAJOR_VERSION
			fs_api_enable(drive_id, TRUE);
#else
    		pgl->drive_structure->pfscntxt = failsafehandle;
#endif
#endif
        	return(TRUE);
		}
	}
    return(TRUE);
}

static BOOLEAN chkdsk_delete_dirent(CHKDSK_CONTEXT *pgl,DROBJ *pobj)
{
	pc_delete_lfn_info(pobj); /* Delete long file name info associated with DROBJ */
	pobj->finode->fname[0] = PCDELETEESCAPE;
	if (!pc_update_inode(pobj, FALSE, 0))
	{
		chkdsk_print_string_1(pgl, USTRING_CHKDSK_41,PRFLG_NL); /* "Write Failed" */
		return(FALSE);
	}
	if (pobj->finode->fattribute & ADIRENT)
		pgl->pstats->n_directories_removed += 1;
	else
		pgl->pstats->n_files_removed += 1;

    return(TRUE);
}


static BOOLEAN chkdsk_is_dir(DROBJ *pobj)
{
    if (pc_isroot(pobj) || (pobj->finode->fattribute & ADIRENT))
		return(TRUE);
	else
		return(FALSE);
}
static dword chkdsk_first_cluster(DROBJ *pobj)
{
dword cluster;



	cluster = 0;
    if (pc_isroot(pobj)) /* FAT32 stores the root directory as a cluster chain */
    {
        if (DRIVE_INFO(pobj->pdrive,fasize) == 8) /* FAT32 volume */
            cluster = pc_sec2cluster(pobj->pdrive,DRIVE_INFO(pobj->pdrive,rootblock));
    }
	else
	{
		cluster = pc_finode_cluster(pobj->pdrive,pobj->finode);
	}
	return(cluster);
}


/* Terminate the chain starting with the cluster at "chain_to_terminate" when the next cluster pointed to is intersecting_cluster */
static BOOLEAN chkdsk_terminate_crossed_chain(CHKDSK_CONTEXT *pgl, dword chain_to_terminate, dword intersecting_cluster)
{
dword loop_guard, cluster, next_cluster;
DDRIVE *pdrive;
	pdrive = pgl->drive_structure;
	cluster = chain_to_terminate;

	loop_guard = chkdsk_get_chain_loop_guard(pdrive);
    while (--loop_guard && cluster && cluster != CHKDSK_FAT_ERROR_VAL && cluster != CHKDSK_FAT_EOF_VAL)
	{
		next_cluster = chkdsk_next_cluster(pdrive, cluster);
		if (next_cluster == intersecting_cluster)
		{
			if (!fatop_pfaxxterm(pdrive, cluster))
			{
				chkdsk_print_string_1(pgl, USTRING_CHKDSK_41,PRFLG_NL); /* "Write Failed" */
				return(FALSE);
			}
			break;
		}
		cluster = next_cluster;
	}
	return(TRUE);
}

static BOOLEAN chkdsk_check_chain_size_match(CHKDSK_CONTEXT *pgl, DROBJ *pobj)
{
dword chain_size = chkdsk_chain_size_bytes(pgl, chkdsk_first_cluster(pobj));
	/* If the file size overlaps the last cluster in the chain return TRUE */
    if ( (pobj->finode->fsizeu.fsize >= chain_size) && (pobj->finode->fsizeu.fsize-chain_size) < (dword) DRIVE_INFO(pobj->pdrive,bytespcluster))
		return(TRUE);
	else
		return(FALSE);
}


static BOOLEAN chkdsk_process_crossed_chains(CHKDSK_CONTEXT *pgl)
{
dword scan_cluster, rescan_cluster,crossed_cluster;
BOOLEAN delete_scan, delete_rescan, update_scan, update_rescan;
DDRIVE *pdrive;
DROBJ *scan_crossed_drobj, *rescan_crossed_drobj;

	scan_crossed_drobj = pgl->scan_crossed_drobj;
	rescan_crossed_drobj = pgl->rescan_crossed_drobj;
	crossed_cluster = pgl->crossed_cluster;

	if (!scan_crossed_drobj || !pgl->rescan_crossed_drobj || !pgl->crossed_cluster) /* Won't happen */
		return(FALSE);

	pdrive = scan_crossed_drobj->pdrive;
	update_scan = update_rescan = delete_scan = delete_rescan = FALSE;
	scan_cluster = chkdsk_first_cluster(scan_crossed_drobj);
	rescan_cluster = chkdsk_first_cluster(rescan_crossed_drobj);

	/* If CHKDSK_FREEBROKENDIRECTORIES() and the entry is a directory delete the entry */
	if (chkdsk_is_dir(scan_crossed_drobj) && CHKDSK_FREEBROKENDIRECTORIES(pgl))
		delete_scan = TRUE;
	if (chkdsk_is_dir(rescan_crossed_drobj) && CHKDSK_FREEBROKENDIRECTORIES(pgl))
		delete_rescan = TRUE;
	/* If CHKDSK_FREEBROKENFILES() and the entry is a file delete the entry */
	if (!chkdsk_is_dir(scan_crossed_drobj) && CHKDSK_FREEBROKENFILES(pgl))
		delete_scan = TRUE;
	if (!chkdsk_is_dir(rescan_crossed_drobj) && CHKDSK_FREEBROKENFILES(pgl))
		delete_rescan = TRUE;
	/* If the entry itself  points to the crossed cluster delete the entry */
	if (scan_cluster == crossed_cluster && !pc_isroot(scan_crossed_drobj))
		delete_scan = TRUE;
	if (rescan_cluster == crossed_cluster && !pc_isroot(rescan_crossed_drobj))
		delete_rescan = TRUE;
	if (delete_scan || delete_rescan)
		;
	else if (!chkdsk_is_dir(scan_crossed_drobj) && !chkdsk_is_dir(rescan_crossed_drobj)) /* Both are files */
	{
	DROBJ *resize_drobj;
		if (chkdsk_check_chain_size_match(pgl, rescan_crossed_drobj))
		{ /* If the second file size matches the chain length truncate the first file */
			if (!chkdsk_terminate_crossed_chain(pgl, scan_cluster, crossed_cluster))
				return(FALSE);
			resize_drobj = scan_crossed_drobj;
		}
		else
		{ /* Truncate the second file */
			if (!chkdsk_terminate_crossed_chain(pgl, rescan_cluster, crossed_cluster))
				return(FALSE);
			resize_drobj = rescan_crossed_drobj;

		}
		if (resize_drobj == scan_crossed_drobj)
		{
    		if (!chkdsk_check_chain_size_match(pgl, scan_crossed_drobj))
    		{
    			scan_crossed_drobj->finode->fsizeu.fsize = chkdsk_chain_size_bytes(pgl, scan_cluster);
    			update_scan = TRUE;
    		}
		}
		else
		{
    		if (!chkdsk_check_chain_size_match(pgl, rescan_crossed_drobj))
    		{
    			rescan_crossed_drobj->finode->fsizeu.fsize = chkdsk_chain_size_bytes(pgl, rescan_cluster);
    			update_rescan = TRUE;
    		}
		}
	}
	else if (chkdsk_is_dir(scan_crossed_drobj) && chkdsk_is_dir(rescan_crossed_drobj)) /* Both directories. Terminate one of them */
	{
		dword chain_to_terminate = 0;
		/* If either chain is the root, terminate the other */
		if (pc_isroot(scan_crossed_drobj))
			chain_to_terminate = rescan_cluster;
		else if (pc_isroot(rescan_crossed_drobj))
			chain_to_terminate = scan_cluster;
		else
			chain_to_terminate = scan_cluster;   /* otherwise terminate scan_cluster (just guessing, but this was lower in the tree so it may be later) */

		if (!chkdsk_terminate_crossed_chain(pgl,chain_to_terminate, crossed_cluster))
			return(FALSE);
	}
	else if (chkdsk_is_dir(scan_crossed_drobj)) /* rescan is a file, scan is a dir */
	{
		if (!chkdsk_terminate_crossed_chain(pgl, rescan_cluster, crossed_cluster))
			return(FALSE);
		if (!chkdsk_check_chain_size_match(pgl, rescan_crossed_drobj))
		{
			rescan_crossed_drobj->finode->fsizeu.fsize = chkdsk_chain_size_bytes(pgl, rescan_cluster);
			update_rescan = TRUE;
		}
	}
	else /* if (chkdsk_is_dir(rescan_crossed_drobj)) scan is a file rescan is a dir */
	{
		if (!chkdsk_terminate_crossed_chain(pgl, scan_cluster, crossed_cluster))
			return(FALSE);
		if (!chkdsk_check_chain_size_match(pgl, scan_crossed_drobj))
		{
			scan_crossed_drobj->finode->fsizeu.fsize = chkdsk_chain_size_bytes(pgl, scan_cluster);
			update_scan = TRUE;
		}
	}

	if (delete_scan)
		chkdsk_delete_dirent(pgl, scan_crossed_drobj);
	else if (update_scan && !pc_update_inode(scan_crossed_drobj, TRUE, DATESETCREATE|DATESETUPDATE))
	{
		chkdsk_print_string_1(pgl, USTRING_CHKDSK_41,PRFLG_NL); /* "Write Failed" */
		return(FALSE);
	}
	if (delete_rescan)
		chkdsk_delete_dirent(pgl,rescan_crossed_drobj);
	else if (update_rescan && !pc_update_inode(rescan_crossed_drobj, TRUE, DATESETCREATE|DATESETUPDATE))
	{
		chkdsk_print_string_1(pgl, USTRING_CHKDSK_41,PRFLG_NL); /* "Write Failed" */
		return(FALSE);
	}
	/* Free the objects */
	pc_freeobj(rescan_crossed_drobj);
	pc_freeobj(scan_crossed_drobj);
	/* Flush the FAT buffer, if none are dirty no harm is done */
	return(fatop_flushfat(pdrive));
}


/************************************************************************
*                                                                      *
* File/Directory Scanning                                              *
*                                                                      *
************************************************************************/

/* Scan all files/directories on the drive -
*  Mark all used clusterd in the used bit map.
*  Note any crossed cluster chains
*  Adjust any incorrect file sizes
*/


/* int scan_all_files(byte *dir_name)
*
*  This routine scans all subdirectories and does the following:
*   . calculates pgl->pstats->n_user_directories
*   . calculates pgl->pstats->n_hidden_files
*   . calculates pgl->pstats->n_user_files
*       Then it calls process_find_crossed_chains for each file in the directory and
*       for the directory itself .
*     process_find_crossed_chains does the following:
*   .       calculates pgl->pstats->n_dir_clusters
*   .       calculates pgl->pstats->n_hidden_clusters
*   .       calculates pgl->pstats->n_file_clusters
*   .       notes and if writing adjusts incorrect filesizes
*   .       finds crossed chains
*   . calls scan_for_bad_lfns
*   .
*   . scan_all_files calls itself recursively for each subdirectory it
*   . encounters.
*   .
*   .
*/
#ifdef RTFS_MAJOR_VERSION
#define GET_CURRENT_DIROBJ(DR) _pc_get_user_cwd(DR)
#else
#define GET_CURRENT_DIROBJ(DR) pc_get_cwd(DR)
#endif

static BOOLEAN scan_all_files(CHKDSK_CONTEXT *pgl, byte *dir_name)                                /*__fn__*/
{
    DROBJ *directory;
    DROBJ *entry;
	BOOLEAN process_val;
#if (CHKDSK_LEAN_RECURSION_METHOD == 0)
    byte ldir_name[EMAXPATH_BYTES];
#endif
    if (pgl->recursion_depth > MAX_RECURSION_DEPTH)
    {
        chkdsk_print_string_2(pgl, USTRING_CHKDSK_45, dir_name,PRFLG_NL); /* "Path too deep , directory == " */
        return(FALSE);
    }
    pgl->recursion_depth += 1;

#if (INCLUDE_VFAT)
    /* Report and optionally mark deleted orphaned VFAT file name extensions. The first pass fixes looped and unterminated
       cluster chains so do this on the second pass. */
    if (pgl->on_which_pass==2)
    {
        /* Find the directory again for scanning the dir for lfn errors */

#if (CHKDSK_LEAN_RECURSION_METHOD)
		directory = GET_CURRENT_DIROBJ(pgl->drive_structure);
#else
        directory = pc_fndnode(dir_name);
#endif
        if (!directory)
        {
            chkdsk_print_string_2(pgl, USTRING_CHKDSK_46, dir_name,PRFLG_NL); /* "Failed Scanning This Directory on LFN Pass  -" */
            return(FALSE);
        }

        /* Scan through the directory looking for bad lfn data */
        pgl->pstats->n_bad_lfns += scan_for_bad_lfns(directory, (int) CHKDSK_DOFIXPROBLEMS(pgl));
        pc_freeobj(directory);
    }
#endif
    /* Find the directory for scanning the dir for files */
#if (CHKDSK_LEAN_RECURSION_METHOD)
	directory = GET_CURRENT_DIROBJ(pgl->drive_structure);
#else
	directory = pc_fndnode(dir_name);
#endif
    if (!directory)
    {
        chkdsk_print_string_2(pgl, USTRING_CHKDSK_47, dir_name,PRFLG_NL); /* "Failed Scanning This Directory -" */
        return(FALSE);
    }
    if (pgl->on_which_pass==1)
    { /* Print the directory name on first pass */
        chkdsk_print_string_2(pgl, USTRING_SYS_NULL,dir_name,PRFLG_NL);
    }
    pgl->pstats->n_user_directories += 1;


    if (pgl->on_which_pass==1)	/* On the first pass find cluster chain problems */
	{
    	process_val = process_directory_entry(pgl, directory, dir_name);
	 	if (!process_val && pgl->pstats->has_endless_loop && !CHKDSK_DOFIXPROBLEMS(pgl))
	 	{
            return(FALSE);
		}
	}
	else						/* Subsequant passes scan for lost chains and crossed chains */
	{
    	process_val = process_find_crossed_chains(pgl, dir_name, directory);
    }

    if (!process_val)
    {
        pc_freeobj(directory);
        chkdsk_print_string_2(pgl, USTRING_CHKDSK_47, dir_name,PRFLG_NL); /* "Failed Scanning This Directory -" */
        return(FALSE);
    }

	if (pgl->scan_state == SC_STATE_REQUEST_RESCAN_FOR_CROSSED_CHAIN)
	{	/* We don't free the object here because we need it for correcting chains */
		pgl->recursion_depth -= 1;
		return(TRUE);
	}
	else if (pgl->scan_state == SC_STATE_FOUND_RESCAN_CROSSED_CHAIN)
	{
		pgl->recursion_depth -= 1;
		return(TRUE);
	}

    /* Scan through the directory looking for all files */
    /* Note:   needs endless loop protection */
#ifdef RTFS_MAJOR_VERSION
    entry = pc_get_inode(0,directory, 0, 0, GET_INODE_STAR,CS_CHARSET_NOT_UNICODE);
#else
    entry = pc_get_inode(0,directory, 0, 0, GET_INODE_STAR);
#endif
    if (entry)
    {
	dword loop_guard;

    	loop_guard = chkdsk_get_dir_loop_guard(directory->pdrive);
        do
        {
            if (!(entry->finode->fattribute & (AVOLUME | ADIRENT) ))
            {

#if (CHKDSK_LEAN_RECURSION_METHOD)
#ifdef RTFS_MAJOR_VERSION
                pc_cs_mfile((byte*)pgl->gl_file_path, (byte*)entry->finode->fname,(byte *) entry->finode->fext,CS_CHARSET_NOT_UNICODE);
#else
                pc_cs_mfile((byte*)pgl->gl_file_path, (byte*)entry->finode->fname,(byte *) entry->finode->fext);
#endif
#else
#ifdef RTFS_MAJOR_VERSION
                pc_cs_mfile((byte*)pgl->gl_file_name, (byte*)entry->finode->fname,(byte *) entry->finode->fext,CS_CHARSET_NOT_UNICODE);
#else
                pc_cs_mfile((byte*)pgl->gl_file_name, (byte*)entry->finode->fname,(byte *) entry->finode->fext);
#endif
                pc_mpath((byte *)pgl->gl_file_path, (byte *)dir_name, (byte *)pgl->gl_file_name);
#endif
                if (pgl->on_which_pass == 1)
                {
                    chkdsk_print_string_2(pgl, USTRING_CHKDSK_49, pgl->gl_file_path,PRFLG_NL); /* "    " */
                }
                if (entry->finode->fattribute & AHIDDEN)
                    pgl->pstats->n_hidden_files += 1;
                else
                    pgl->pstats->n_user_files += 1;
    			if (pgl->on_which_pass==1) /* On the first pass find cluster chain problems */
				{
    				process_val = process_directory_entry(pgl, entry, pgl->gl_file_path);
                }
                else		/* Subsequant passes scan for lost chains and crossed chains */
				{
                	process_val = process_find_crossed_chains(pgl, pgl->gl_file_path, entry);
				}

                if (!process_val)
                {
                    pc_freeobj(entry);
                    pc_freeobj(directory);
                    chkdsk_print_string_2(pgl, USTRING_CHKDSK_50, pgl->gl_file_path,PRFLG_NL); /* "Failed Scanning This File " */
					pgl->recursion_depth -= 1;
                    return(FALSE);
                }
				if (pgl->scan_state == SC_STATE_REQUEST_RESCAN_FOR_CROSSED_CHAIN)
				{
					pc_freeobj(directory);
					pgl->recursion_depth -= 1;
					return(TRUE);
				}
				else if (pgl->scan_state == SC_STATE_FOUND_RESCAN_CROSSED_CHAIN)
				{
					pc_freeobj(directory);
					pgl->recursion_depth -= 1;
					return(TRUE);
				}
            }
#ifdef RTFS_MAJOR_VERSION
        }     while (--loop_guard && pc_get_inode(entry , directory, 0, 0, GET_INODE_STAR,CS_CHARSET_NOT_UNICODE));
#else
        }     while (--loop_guard && pc_get_inode(entry , directory, 0, 0, GET_INODE_STAR));
#endif
        pc_freeobj(entry);
    }
    pc_freeobj(directory);

    /* Now we call scan_all_files() for each subdirectory */
    /* Find the directory for scanning the dir for files */
#if (CHKDSK_LEAN_RECURSION_METHOD)
	directory = GET_CURRENT_DIROBJ(pgl->drive_structure);
#else
    directory = pc_fndnode(dir_name);
#endif
    if (!directory)
    {
        chkdsk_print_string_2(pgl, USTRING_CHKDSK_51, dir_name,PRFLG_NL); /* "Failed Scanning This Directory -" */
        return(FALSE);
    }

    /* Scan through the directory looking for all files */
    /* Note:   needs endless loop protection */
#ifdef RTFS_MAJOR_VERSION
    entry = pc_get_inode(0,directory, 0, 0, GET_INODE_STAR,CS_CHARSET_NOT_UNICODE);
#else
    entry = pc_get_inode(0,directory, 0, 0, GET_INODE_STAR);
#endif

    if (entry)
    {
	dword loop_guard = chkdsk_get_dir_loop_guard(directory->pdrive);
        do
        {
            /* Scan it if it is a directory and not . or .. */
            if (entry->finode->fattribute & ADIRENT)
            {
                if ( !pc_isdot(entry->finode->fname, entry->finode->fext) &&
                    !pc_isdotdot(entry->finode->fname, entry->finode->fext) )
                {
#if (CHKDSK_LEAN_RECURSION_METHOD)
#ifdef RTFS_MAJOR_VERSION
                    pc_cs_mfile((byte *)pgl->gl_file_name, (byte *)entry->finode->fname, (byte *)entry->finode->fext,CS_CHARSET_NOT_UNICODE);
#else
                    pc_cs_mfile((byte *)pgl->gl_file_name, (byte *)entry->finode->fname, (byte *)entry->finode->fext);
#endif
                    if (!pc_set_cwd(pgl->gl_file_name))
						goto bad_scan;
                    if (!scan_all_files(pgl, pgl->gl_file_name))
					{
						pc_set_cwd(USTRING_DOTDOT);
bad_scan:
						chkdsk_print_string_2(pgl, USTRING_CHKDSK_51, pgl->gl_file_name,PRFLG_NL); /* "Failed Scanning This Directory -" */
                        pc_freeobj(directory);
                        pc_freeobj(entry);
                        return(FALSE);
                    }
                   	if (!pc_set_cwd(USTRING_DOTDOT))
						goto bad_scan;
#else
#ifdef RTFS_MAJOR_VERSION
                    pc_cs_mfile((byte *)pgl->gl_file_name, (byte *)entry->finode->fname, (byte *)entry->finode->fext,CS_CHARSET_NOT_UNICODE);
#else
                    pc_cs_mfile((byte *)pgl->gl_file_name, (byte *)entry->finode->fname, (byte *)entry->finode->fext);
#endif
                    pc_mpath((byte *)ldir_name, (byte *)dir_name, (byte *)pgl->gl_file_name);
                    if (!scan_all_files(pgl, ldir_name))
					{
                        pc_freeobj(directory);
                        pc_freeobj(entry);
                        return(FALSE);
                    }
#endif
					if (pgl->scan_state == SC_STATE_REQUEST_RESCAN_FOR_CROSSED_CHAIN || pgl->scan_state == SC_STATE_FOUND_RESCAN_CROSSED_CHAIN)
					{
                        pc_freeobj(directory);
                        pc_freeobj(entry);
						pgl->recursion_depth -= 1;
						return(TRUE);
					}
                }
            }
#ifdef RTFS_MAJOR_VERSION
        } while (--loop_guard && pc_get_inode(entry , directory, 0, 0, GET_INODE_STAR,CS_CHARSET_NOT_UNICODE));
#else
        } while (--loop_guard && pc_get_inode(entry , directory, 0, 0, GET_INODE_STAR));
#endif
        pc_freeobj(entry);
    }
    pc_freeobj(directory);
    pgl->recursion_depth -= 1;

    return (TRUE);
}

/* process_find_crossed_chains(DROBJ *pobj)
*
*  This routine is called for each subdirectory and file in the system.
*  It traverses the chain owned by drobj and does the following with each
*  cluster in the chain:
*    pgl->pstats->n_crossed_chains - Increments if a crossed cluster is found
* If fix_problems is TRUE
*    On the fist pass
*        pgl->crossed_cluster  - Is set to the cluster that is crossed
* 	     pgl->scan_state       - scan_state is set SC_STATE_REQUEST_RESCAN_FOR_CROSSED_CHAIN;
*        pgl->scan_crossed_drobj - Set to the DROBJ where the cross was discovered
*    On the second pass (when  pgl->scan_state == SC_STATE_FOUND_RESCAN_CROSSED_CHAIN)
*        pgl->rescan_crossed_drobj - Set to the drobj in the directory tree that crosses with pgl->scan_crossed_drobj
*/

static BOOLEAN process_find_crossed_chains(CHKDSK_CONTEXT *pgl, byte *dirent_name, DROBJ *pobj)
{
    dword cluster;
	dword loop_guard;

    if (pc_isroot(pobj))
        /* FAT32 stores the root directory as a cluster chain */
    {
        if (DRIVE_INFO(pobj->pdrive,fasize) == 8) /* FAT32 volume */
        {
            cluster = pc_sec2cluster(pobj->pdrive,DRIVE_INFO(pobj->pdrive,rootblock));
        }
        else
            return(TRUE);
    }
    else
    {
        cluster = chkdsk_first_cluster(pobj);
    }

    /* If the incoming value is bad do not traverse */
    if ((cluster < 2) || (cluster > DRIVE_INFO(pobj->pdrive,maxfindex) ))
        cluster = 0;
   /*  is gnext's end marker, 0 is error */
	loop_guard = chkdsk_get_chain_loop_guard(pobj->pdrive);
    while (--loop_guard && cluster && cluster != CHKDSK_FAT_ERROR_VAL && cluster != CHKDSK_FAT_EOF_VAL)
    {
		/* Perform only the intersect test if we are rescanning and looking for a crossed chain */
		if (pgl->scan_state == SC_STATE_PERFORM_RESCAN_FOR_CROSSED_CHAIN)
		{
			if (cluster == pgl->crossed_cluster)
			{
				pgl->rescan_crossed_drobj = pobj;
				pgl->scan_state = SC_STATE_FOUND_RESCAN_CROSSED_CHAIN;
				break;
			}
		}
		else if ( (pgl->cl_start <= cluster) && (cluster <= pgl->cl_end) )
        {
        	/* Mark the cluster in use, chkdsk_set_cluster_used() will return TRUE if a crossed chain is detected */
            if (chkdsk_set_cluster_used(pgl, cluster,0))
            {
				/* Increment the count of crossed chains */
   				pgl->pstats->n_crossed_chains += 2;
				/* If we are fixing problems request a rescan */
				if (CHKDSK_DOFIXPROBLEMS(pgl))
				{
					pgl->scan_state = SC_STATE_REQUEST_RESCAN_FOR_CROSSED_CHAIN;
					pgl->scan_crossed_drobj = pobj;
					pgl->crossed_cluster = cluster;
				}
				break;
            }
        }
        cluster = chkdsk_next_cluster(pobj->pdrive, cluster);  /* Fat */
    }
    if (pgl->scan_state == SC_STATE_FOUND_RESCAN_CROSSED_CHAIN || pgl->scan_state == SC_STATE_REQUEST_RESCAN_FOR_CROSSED_CHAIN)
	{
		chkdsk_print_string_2(pgl, USTRING_CHKDSK_42,dirent_name,PRFLG_NL);/* "Entry has crossed chain : " */
	}

    return(TRUE);
}

// ==================================
#define CKSTAT_OK					0
#define CKSTAT_INVALID_START		2
#define CKSTAT_UNTERMINATED			3
#define CKSTAT_INVALID_CLUSTER		4
#define CKSTAT_ENDLESS_LOOP			5

static BOOLEAN process_directory_entry(CHKDSK_CONTEXT *pgl, DROBJ *pobj, byte *filename)                 /*__fn__*/
{
    dword start_cluster,last_valid_cluster, n_clusters, chain_size_bytes, cluster_size_bytes;
	DDRIVE *pdrive;
    BOOLEAN do_update, is_dir, is_hidden,remove_dirent,release_chain,terminate_chain;
	int cluster_chain_status;

	start_cluster = last_valid_cluster = n_clusters = chain_size_bytes = 0;
	release_chain = do_update = terminate_chain = remove_dirent = is_dir = is_hidden = FALSE;

	pdrive = pobj->pdrive;
	cluster_size_bytes = chkdsk_clusterstobytes(pdrive, 1);

    if (pc_isroot(pobj))
    {
        if (DRIVE_INFO(pdrive,fasize) == 8) /* FAT32 volume */
        {
            start_cluster = pc_sec2cluster(pdrive, DRIVE_INFO(pdrive,rootblock));
            is_dir = TRUE;
        }
        else
            return(TRUE);
    }
    else
    {
        if (pobj->finode->fattribute & AVOLUME)
			return(TRUE);
        else if (pobj->finode->fattribute & ADIRENT)
            is_dir = TRUE;
        else
        {
            if (pobj->finode->fattribute & AHIDDEN)
                is_hidden = TRUE;
        }
        start_cluster = chkdsk_first_cluster(pobj);
    }

    /* If the incoming value is bad do not traverse */
    if ((start_cluster < 2) || (start_cluster > DRIVE_INFO(pobj->pdrive,maxfindex)) )
	{
        cluster_chain_status = CKSTAT_INVALID_START;
        pgl->pstats->n_bad_dirents += 1;

	}
	else
	{
	dword loop_guard, cluster;

		loop_guard = chkdsk_get_chain_loop_guard(pdrive);
		cluster = start_cluster;
		cluster_chain_status = CKSTAT_ENDLESS_LOOP;	/* Default failure mode */
		while (--loop_guard)
		{
			dword new_n_bytes;

			new_n_bytes = chain_size_bytes + cluster_size_bytes;
			if (new_n_bytes < chain_size_bytes)
			{	/* If the chain size overflows the maximum file size, set cluster to zero, to force unterminated chain processing */
				cluster = 0;
			}
			else
			{
				chain_size_bytes = new_n_bytes;
        		n_clusters += 1;
        		last_valid_cluster = cluster;
        		cluster = chkdsk_next_cluster(pdrive, last_valid_cluster);  /* Fat */
			}
        	if (cluster == CHKDSK_FAT_EOF_VAL)
			{
        		cluster_chain_status = CKSTAT_OK;
				break;
			}
        	else if (cluster == 0)
			{
        		cluster_chain_status = CKSTAT_UNTERMINATED;
        		if (last_valid_cluster == start_cluster)
				{
					cluster_chain_status = CKSTAT_INVALID_START;
					pgl->pstats->n_bad_dirents += 1;
				}
				else
        			pgl->pstats->n_unterminated_chains += 1;           /* # of chains that were not terminated */
				break;
			}
			else if (cluster == CHKDSK_FAT_ERROR_VAL)
			{
				cluster_chain_status = CKSTAT_INVALID_CLUSTER;
        		pgl->pstats->n_badcluster_values += 1;           /* # of invalid cluster values found in chains */
        		if (last_valid_cluster == start_cluster)
				{
					cluster_chain_status = CKSTAT_INVALID_START;
					pgl->pstats->n_bad_dirents += 1;
				}
				break;
			}
		}
		/* Check if an endless loop was detected. if CHKDSK_DOFIXPROBLEMS(pgl) is not set, set the has_endless_loop status and return,
		   chkdisk will terminate. Check disk must be restarted with the argument "fix_problems" set to one to clear this
		   condition. */
		if (cluster_chain_status == CKSTAT_ENDLESS_LOOP)
		{
			pgl->pstats->has_endless_loop = TRUE;
			if (!CHKDSK_DOFIXPROBLEMS(pgl))
			{
        		chkdsk_print_string_1(pgl, USTRING_CHKDSK_63,PRFLG_NL); /* Chkdsk found looped cluster chain and stopped, correct with fix option */
        		pgl->pstats->has_endless_loop = TRUE;
        		return(FALSE);
			}
		}
    }
    if (is_dir)
	{
		/* Update count of corrupted directories */
		switch (cluster_chain_status)
		{
			case CKSTAT_INVALID_CLUSTER:
            case CKSTAT_INVALID_START:
            case CKSTAT_UNTERMINATED:
            case CKSTAT_ENDLESS_LOOP:
				break;
		}
		/* Update count of directory clusters */
		switch (cluster_chain_status)
		{
			default:
            case CKSTAT_OK:
			case CKSTAT_INVALID_CLUSTER:
            case CKSTAT_INVALID_START:
            case CKSTAT_UNTERMINATED:
            	pgl->pstats->n_dir_clusters = (dword) (pgl->pstats->n_dir_clusters + n_clusters);
				break;
            case CKSTAT_ENDLESS_LOOP:
            	pgl->pstats->n_dir_clusters = (dword) (pgl->pstats->n_dir_clusters + 1);	/* We will use only the first cluster */
				break;
		}
		/* Note: Should update count of corrupted directory entries */

		if (CHKDSK_DOFIXPROBLEMS(pgl))
		{
    		switch (cluster_chain_status)
    		{
				default:
                case CKSTAT_OK:
    				break;
                case CKSTAT_INVALID_START: /* Sub directories with bad start cluster are removed */
                	if (!pc_isroot(pobj))
					{
                		remove_dirent = TRUE;
					}
    				break;
                case CKSTAT_ENDLESS_LOOP: /* Sub directories circular links are clipped to the first cluster */
				{
				dword next_cluster;
					/* Release all clusters after the first cluster in the chain. */
					next_cluster = chkdsk_next_cluster(pobj->pdrive, start_cluster);
					if (next_cluster && next_cluster != CHKDSK_FAT_ERROR_VAL && next_cluster != CHKDSK_FAT_EOF_VAL)
               			chkdsk_free_chain(pgl,next_cluster);	/* Free all clusters in the file if the cluster chain is looped */
					/* Now fall through to terminate the chain at the first cluster, note this will also fix the case of the first entry looping on itself. */
					last_valid_cluster = start_cluster;		/* Fall through */
					terminate_chain = TRUE;
					break;
				}
    			case CKSTAT_INVALID_CLUSTER: /*	Terminate subdirectory chains at the last valid cluster */
                case CKSTAT_UNTERMINATED:
					terminate_chain = TRUE;
					break;
			}
			/* Test if we want to remove the directory. If terminate_chain is needed we do that first */
    		if (cluster_chain_status != CKSTAT_OK && CHKDSK_FREEBROKENDIRECTORIES(pgl))
    		{
                remove_dirent = TRUE;
    			if (cluster_chain_status != CKSTAT_INVALID_START)
            		release_chain = TRUE;
			}
		}
	 }
	 else /* check results for a file */
	 {
		/* Update count of corrupted files */
		switch (cluster_chain_status)
		{
			case CKSTAT_INVALID_CLUSTER:
            case CKSTAT_INVALID_START:
            case CKSTAT_UNTERMINATED:
            case CKSTAT_ENDLESS_LOOP:
				break;
		}
		switch (cluster_chain_status)
		{
			default:
            case CKSTAT_OK:
			case CKSTAT_INVALID_CLUSTER:
            case CKSTAT_UNTERMINATED:
            	if (is_hidden)
                	pgl->pstats->n_hidden_clusters = (dword) (pgl->pstats->n_hidden_clusters + n_clusters);
                else
                	pgl->pstats->n_file_clusters = (dword) (pgl->pstats->n_file_clusters + n_clusters);
		}
		if (CHKDSK_DOFIXPROBLEMS(pgl))
		{
   			/* Remove entry if endless loop or invalid start cluster. */
       		switch (cluster_chain_status)
       		{
   				default:
                case CKSTAT_OK:
                break;
                case CKSTAT_INVALID_START: /* Files with bad start clusters are removed unless they are legitimate zero sized files */
                	if (!(start_cluster == 0 && pobj->finode->fsizeu.fsize == 0))
        				remove_dirent = TRUE;
                    break;
                case CKSTAT_ENDLESS_LOOP:
            		release_chain = TRUE;
        			remove_dirent = TRUE;
        			break;
    			/* Terminate cluster chain if necessary */
        		case CKSTAT_INVALID_CLUSTER:
                case CKSTAT_UNTERMINATED:
       				terminate_chain = TRUE;
        			break;
       		}
           	/* Now update the file size if necessary */
           	switch (cluster_chain_status)
           	{
                case CKSTAT_OK:
          		case CKSTAT_INVALID_CLUSTER:
                case CKSTAT_UNTERMINATED:
                if (CHKDSK_DOFIXPROBLEMS(pgl))
                {
       				/* If the file size is > the chain or the chain is > file size by more than one cluster then set the file size equal to the chain size */
                   	if ( (pobj->finode->fsizeu.fsize > chain_size_bytes) || ((chain_size_bytes - pobj->finode->fsizeu.fsize) > (dword) DRIVE_INFO(pobj->pdrive,bytespcluster)) )
                   	{
                   		pobj->finode->fsizeu.fsize = chain_size_bytes;
                   		do_update = TRUE;
                   	}
                }
    			default:
      			break;
			}
    		if ((do_update || cluster_chain_status != CKSTAT_OK) && CHKDSK_FREEBROKENFILES(pgl) && CHKDSK_DOFIXPROBLEMS(pgl))
    		{
               	remove_dirent = TRUE;
            	if (cluster_chain_status != CKSTAT_INVALID_START)
            		release_chain = TRUE;
    		}

    	}
   	}
	if (CHKDSK_DOFIXPROBLEMS(pgl))
	{
    	if (terminate_chain)
    	{
    		if (!fatop_pfaxxterm(pdrive, last_valid_cluster))
            {
            	chkdsk_print_string_2(pgl, USTRING_CHKDSK_52, filename,PRFLG_NL); /* "Failed Writing This Adusted File:    " */
            	return(FALSE);
            }
    	}
    	if (release_chain)
    		chkdsk_free_chain(pgl,start_cluster);	/* Free all clusters in the file if the cluster chain is looped */
    	if (remove_dirent)
    	{
			chkdsk_delete_dirent(pgl, pobj);
    	}
    	else if (do_update)
    	{
    		if (!pc_update_inode(pobj, TRUE, DATESETUPDATE))
    		{
    			chkdsk_print_string_2(pgl, USTRING_CHKDSK_52, filename,PRFLG_NL); /* "Failed Writing This Adusted File:    " */
    			return(FALSE);
    	 	}
    	}
    	if (terminate_chain || release_chain)
			return(fatop_flushfat(pdrive));
	}
    return(TRUE);
}


/************************************************************************
*                                                                      *
* Lost cluster fns                                                     *
*                                                                      *
************************************************************************/

/* check_lost_clusters(DDRIVE *pdr)
*
* This routine is called by (app_entry()) after scan_all_files() was called
* to produce the bm_used bitmap of clusters cliamed by files and
* sub-direcories.
* It scans the FILE allocation table. A cluster is lost if it
* is allocated in the FAT but not in the bm_used bitmap we built up while
* scanning the file system (scan_all_files()). We maintain an array of
* chain heads of lost clusters.
*/
static BOOLEAN check_lost_clusters(CHKDSK_CONTEXT *pgl, DDRIVE  *pdr)                             /*__fn__*/
{
    dword cluster,maxfindex;
    dword nxt;
	dword prev_lost_value = 0;
	dword last_logged_cluster = 0;
	BOOLEAN do_flush = FALSE;
	int fasize;

	fasize = DRIVE_INFO(pdr,fasize);
	maxfindex = DRIVE_INFO(pdr,maxfindex);

    for (cluster = pgl->cl_start ; cluster <= pgl->cl_end; cluster++)
    {
        if (!fatop_get_cluster(pdr, cluster, &nxt))    /* Fat */
        {
            return(FALSE);
        }
        if (nxt != 0)
        {
            /* If we did not see the cluster during the directory scan */
        	if (!chkdsk_check_cluster_used(pgl, cluster))
            {
            	/* ff(f)7 marks a bad cluster if it is not already in a chain. */
                if ( ((fasize == 3) && (nxt ==  0xff7)) || ((fasize == 4) && (nxt == 0xfff7)) || ((fasize == 8) && (nxt == 0xffffff7ul)))
                    pgl->pstats->n_bad_clusters += 1;
				else
				{
				BOOLEAN is_terminator = FALSE;

					if ( (fasize == 3 && nxt >=  0xff8) || (fasize == 4 && nxt >=  0xfff8) || (fasize == 8 && nxt >=  0x0ffffff8))
						is_terminator = TRUE;
					/* Increment lost cluster count and estimate cont of lost chains */
					if (!CHKDSK_DOFIXPROBLEMS(pgl))
					{	/* Just increment lost cluster count and estimate count of lost chains */
						pgl->pstats->n_lost_clusters++;
						if (prev_lost_value != cluster)
						{
							pgl->pstats->n_lost_chains++;
						}
						if (is_terminator)
							prev_lost_value = 0;
						else
							prev_lost_value = nxt;
					}
					else
					{
						if (!CHKDSK_MAKECHECKFILES(pgl))
						{	/* If and freeing chains write zeroes to lost clusters */
							do_flush = TRUE;
							/* And increment lost cluster count and estimate count of lost chains */
							pgl->pstats->n_lost_clusters++;
							if (prev_lost_value != cluster)
								pgl->pstats->n_lost_chains++;
							if (is_terminator)
								prev_lost_value = 0;
							else
								prev_lost_value = nxt;
							pgl->pstats->n_clusters_freed += 1;
							if (!fatop_pfaxx(pdr, cluster, 0))    /* Fat */
                        		return(FALSE);
						}
						else
						{   /* Writing .chk files, test if it is a terminator, if it is an illegal value make it a terminator.
						       We will generate check files from a list of cluster chains
							   lost clusters and chain counts are done later  */
							if (!is_terminator && (nxt < 2 || nxt > maxfindex))
							{  /* If it is a bad value replace with a terminator */
								if (!fatop_pfaxxterm(pdr, cluster))    /* Fat */
									return(FALSE);
								do_flush = TRUE;
								is_terminator = TRUE;
							}
							if (chkdsk_add_lost_cluster(pgl, cluster, nxt, is_terminator))
								last_logged_cluster = cluster;
							else
							{	/* Ran out of structures for logging lost cluster chain fragments:
							       Set the scan range to include the last cluster we could log and order a new scan */
								if (last_logged_cluster > pgl->cl_start)
									pgl->cl_end = last_logged_cluster;
								else
								{ /* If we couldn't log any, cut the scan size down to reduce the used map size and try again */
								dword midpoint;
									midpoint = chkdsk_midpoint_usedmap(pgl);
									if (!midpoint)
									{
										chkdsk_print_string_1(pgl, USTRING_CHKDSK_62, PRFLG_NL); /* "Chkdsk gives up, unexplained endless loop" */
										return(FALSE);
									}
									pgl->cl_end = midpoint;
								}
								/* Free the lost chain list so we don't create check files */
								free_lost_chain_list(pgl);
								break;
							}
						}
					}
                }
            }
        }
        else
            pgl->pstats->n_free_clusters += 1;
    }
	if (do_flush)
	{
		return(fatop_flushfat(pdr));
	}
    return (TRUE);
}



/************************************************************************
*                                                                      *
* Utility Functions                                                    *
*                                                                      *
************************************************************************/

/* chain_size (dword cluster)
*
*  Calculate (return) a chain s size in bytes
*
*  Called by: build_chk_file
*/

static dword chkdsk_chain_size_clusters(CHKDSK_CONTEXT *pgl, dword cluster)                           /*__fn__*/
{
    dword n_clusters;
	dword loop_guard;

	loop_guard = chkdsk_get_chain_loop_guard(pgl->drive_structure);

    n_clusters = 0;
    /* If the incoming value is bad do not traverse */
    if ((cluster < 2) || (cluster > DRIVE_INFO(pgl->drive_structure,maxfindex)))
        cluster = 0;
    while (--loop_guard && cluster && cluster != CHKDSK_FAT_ERROR_VAL && cluster != CHKDSK_FAT_EOF_VAL)
    {
        n_clusters += 1;
        cluster = chkdsk_next_cluster(pgl->drive_structure, cluster);  /* Fat */
    }
    return(n_clusters);
}
static dword chkdsk_chain_size_bytes(CHKDSK_CONTEXT *pgl, dword cluster)                           /*__fn__*/
{
dword n_clusters;
	n_clusters = chkdsk_chain_size_clusters(pgl, cluster);
	return(chkdsk_clusterstobytes(pgl->drive_structure,n_clusters));
}

dword chkdsk_next_cluster(DDRIVE *pdr, dword cluster)
{
dword nxt,maxfindex;
int    fasize;             /* Nibbles per fat entry. (2 or 4) */


	fasize = DRIVE_INFO(pdr,fasize);
	maxfindex = DRIVE_INFO(pdr,maxfindex);

    if (fatop_get_cluster(pdr, cluster, &nxt))
	{
		if ( (fasize == 3 && nxt >=  0xff8) ||
        	(fasize == 4 && nxt >=  0xfff8) ||
        	(fasize == 8 && nxt >=  0x0ffffff8))
        		nxt = CHKDSK_FAT_EOF_VAL;                            /* end of chain */
		else if (nxt == 1 || nxt > maxfindex)
			nxt = CHKDSK_FAT_ERROR_VAL;
    }
	else
		nxt = CHKDSK_FAT_ERROR_VAL;
    return(nxt);
}
static BOOLEAN chkdsk_set_cluster_used(CHKDSK_CONTEXT *pgl, dword cluster, REGION_FRAGMENT *pf_to_use)
{
REGION_FRAGMENT *pf,*pf_prev,*pf_free;
	pf_prev = 0;
    pf = (REGION_FRAGMENT *) pgl->used_segment_list;
    /* Find the last fragment containing clusters <= cluster and the first fragment
      containing cluster values greater than cluster. */
    while (pf)
    {
        if (pf->start_location > cluster)
			break;
		pf_prev = pf;
		pf = pf->pnext;
     }
	 /* Check if the first fragment beyond the cluster is adjacent */
     if (pf && (pf->start_location - 1 == cluster))
     {
     	pf->start_location = cluster;
     	return(FALSE);		/* False means we did not detect a crossed chain */
	 }
	 /* Check if the fragment preceeding the cluster value is adjacent */
	 if (pf_prev && pf_prev->end_location+1 == cluster)
	 {
	 	pf_prev->end_location = cluster;
     	return(FALSE);		/* False means we did not detect a crossed chain */
	 }
	 if (pf_prev && pf_prev->start_location <= cluster && cluster <= pf_prev->end_location)
	 { 	/* Check if the last fragment scanned contains cluster, if so it is a crossed chain */
		return(TRUE);
	 }
	 /* Fall through to insert the cluster in a new fragment */
	 if (pf_to_use)
	 	pf_free = pf_to_use;	/* Re-using the old end of chain */
	 else
	 	pf_free = chkdsk_allocate_fragment(pgl);
	 if (pf_free)
	 {
	 	pf_free->start_location = pf_free->end_location = cluster;
	 	pf_free->pnext = pf;
	 	if (pf_prev)
			pf_prev->pnext = pf_free;
	 	else
   			pgl->used_segment_list = (void *) pf_free;
	 	return(FALSE);		/* False means we did not detect a crossed chain */
	 }
	 else
	 { 	/* recycle the last cluster in the list */
	 	pf_prev = 0;
	 	pf = (REGION_FRAGMENT *) pgl->used_segment_list;
	 	while (pf)
	 	{
   			if (pf->pnext)
   			{  /* Keep scanning until we find the last last segment, update pgl_>cl_end because we are shrinking it  */
   				pgl->cl_end = pf->end_location;
			}
			else
			{
				if (pf_prev)
					pf_prev->pnext = 0;
				else
					pgl->used_segment_list = 0;
				return(chkdsk_set_cluster_used(pgl, cluster, pf));
			}
			pf_prev = pf;
			pf = pf->pnext;
		}
		return(FALSE);
	 }
}
/* Check if a cluster is included in our list of mapped cluster regoins */
static BOOLEAN chkdsk_check_cluster_used(CHKDSK_CONTEXT *pgl, dword cluster)
{
REGION_FRAGMENT *pf;
    pf = (REGION_FRAGMENT *)  pgl->used_segment_list;
    /* Find the last fragment containing clusters <= claster and the first fragment
      containing cluster values greater than cluster. */
    while (pf)
    {
	 	if (pf->start_location <= cluster && cluster <= pf->end_location)
			return(TRUE);
		pf = pf->pnext;
     }
	 return(FALSE);
}

/* Return the cluster at the approximate median of the used cluster map returns zero if the map is empty */
static dword chkdsk_midpoint_usedmap(CHKDSK_CONTEXT *pgl)
{
REGION_FRAGMENT *pf;
dword midpoint = 0;
dword n_records = 0;

	pf = (REGION_FRAGMENT *)  pgl->used_segment_list;
	while (pf)
	{
		n_records += 1;
		pf = pf->pnext;
	}
	if (n_records > 0)
	{
		pf = (REGION_FRAGMENT *)  pgl->used_segment_list;
		n_records /= 2;
     	while (n_records--)
	 	{
			pf = pf->pnext;
		}
	 	midpoint =	pf->start_location + (pf->end_location-pf->start_location)/2;
	 }
	 return(midpoint);
}


static BOOLEAN chkdsk_add_lost_cluster(CHKDSK_CONTEXT *pgl, dword cluster, dword value, BOOLEAN is_terminator)
{
REGION_FRAGMENT *pf_free[2],*pf,*pf_prev;

	/* Allocate two free segments we might need */
 	pf_free[0] = chkdsk_allocate_fragment(pgl);
	if (pf_free[0] == 0)
	{
		return(FALSE);
	}
 	pf_free[1] = chkdsk_allocate_fragment(pgl);
	if (pf_free[1] == 0)
	{
 		chkdsk_free_fragment(pgl, pf_free[0]);
		return(FALSE);
	}
	pf_free[1]->pnext = 0;
	/* Initialize the free records as if it were a chain containing a single lost cluster and a link */
	pf_free[0]->start_location = cluster;
	pf_free[0]->end_location = cluster;
	if (is_terminator)
	{
		pf_free[0]->pnext = 0;
		pf_free[0]->end_location |= CHKDSK_TERMINATOR;
	}
	else
	{
		pf_free[0]->pnext = pf_free[1];
		pf_free[1]->start_location = value;
		pf_free[1]->end_location = 0;
	}

	if (!pgl->lost_chain_list)
	{
		pgl->lost_chain_list = (void *) pf_free[0];
		if (is_terminator)
 			chkdsk_free_fragment(pgl, pf_free[1]);
		return(TRUE);
	}

	pf = (REGION_FRAGMENT *) pgl->lost_chain_list;
	pf_prev = 0;

	while (pf)
	{
		if (pf->start_location == cluster)
		{ /* We are extending a chain */
			if (pf_prev && pf_prev->end_location+1 == cluster)
			{ /* The cluster is contiguous, recycle the link pointer and discard the new structures */
				pf_prev->end_location = cluster;
				chkdsk_free_fragment(pgl, pf_free[0]);
				chkdsk_free_fragment(pgl, pf_free[1]);
				if (is_terminator)
				{ /* If terminating and the cluster was contiguous with pf_prev, then free the link record */
					pf_prev->end_location |= CHKDSK_TERMINATOR;
					pf_prev->pnext = pf->pnext;
					chkdsk_free_fragment(pgl, pf);
				}
				else
				{ /* Not terminating, set the start value to the next cluster we anticipate. */
					pf->start_location = value;
					pf->end_location = 0;
				}
			}
			else
			{ /* The cluster is not contiguous, consume the link pointer */
				pf->end_location = cluster;
				if (is_terminator)
				{ /* If terminating mark it so */
					pf->end_location |= CHKDSK_TERMINATOR;
					chkdsk_free_fragment(pgl, pf_free[0]);
					chkdsk_free_fragment(pgl, pf_free[1]);
				}
				else
				{ /* Not terminating, create a new link pointer with start value to the next cluster we anticipate. */
					pf_free[0]->start_location = value;
					pf_free[0]->end_location = 0;
					pf_free[0]->pnext = pf->pnext;
					pf->pnext = pf_free[0];
					chkdsk_free_fragment(pgl, pf_free[1]);
				}
			}
			/* Break out with pf non zero, means we do not have to append */
			break;
		}
		else
		{
			/* Skip until we find a link pointer with our value or end of the list */
			pf_prev = pf;
			pf = pf->pnext;
		}
	}
	/* If we got to the end of the chain append our new records. if the current record is a terminatore discard the new link pointer */
	if (!pf)
	{
		if (pf_prev)
			pf_prev->pnext = pf_free[0];
		else
			pgl->lost_chain_list = (void *)pf_free[0];
		if (is_terminator)
 			chkdsk_free_fragment(pgl, pf_free[1]);
	}
	return(TRUE);
}

static REGION_FRAGMENT *chkdsk_allocate_fragment(CHKDSK_CONTEXT *pgl)
{
REGION_FRAGMENT *pf;
	pf = 0;
	if (pgl->scratch_memory)
	{
		pf = (REGION_FRAGMENT *) pgl->scratch_segment_heap;
		if (pf)
		{
			pgl->scratch_segment_heap = pf->pnext;
		}
	}
#ifdef RTFS_MAJOR_VERSION
	else
	{
		pf = pc_fraglist_frag_alloc(pgl->drive_structure, 0, 0, 0);
	}
#endif
	return(pf);
}
static void chkdsk_free_fragment(CHKDSK_CONTEXT *pgl,REGION_FRAGMENT *pf)
{
	if (pgl->scratch_memory)
	{
		pf->pnext = (REGION_FRAGMENT *) pgl->scratch_segment_heap;
		pgl->scratch_segment_heap = pf;
	}
#ifdef RTFS_MAJOR_VERSION
	else
	{
		pc_fraglist_frag_free(pf);
	}
#endif
}


static BOOLEAN chkdsk_init_region_heap(CHKDSK_CONTEXT *pgl, void *scratch_memory, dword scratch_memory_size)
{
REGION_FRAGMENT *pf;

    pgl->scratch_memory = scratch_memory;
    pgl->scratch_segment_heap = scratch_memory;

	if (scratch_memory_size)
	{
		dword i, heapsize_frags;
		if (!scratch_memory)
			return(FALSE);
		/* Calculate size required, arbitrarily require 200 structures (at least 2400 bytes) */
		heapsize_frags = scratch_memory_size/sizeof(REGION_FRAGMENT);
		if (heapsize_frags < 200)
			return(FALSE);
		rtfs_memset((byte *)scratch_memory, 0, scratch_memory_size);
		pf = (REGION_FRAGMENT *) scratch_memory;
		for (i = 1; i < heapsize_frags; i++)
		{
			pf->pnext = (pf+1);
			pf = pf->pnext;
			pf->pnext = 0;
		}
	}
	return(TRUE);
}

static void free_lost_chain_list(CHKDSK_CONTEXT *pgl)
{
REGION_FRAGMENT *pf;

	/* Free fragments in */
	pf = (REGION_FRAGMENT *)pgl->lost_chain_list;
	pgl->lost_chain_list = 0;
	while (pf)
	{
		REGION_FRAGMENT *pf_next;
		pf_next = pf->pnext;
 		chkdsk_free_fragment(pgl, pf);
		pf = pf_next;
	}
}

/* Generic list free, returns a fragment list to the fragment structure heap */
static void chkdsk_free_list(CHKDSK_CONTEXT *pgl, REGION_FRAGMENT *pf)
{
	/* Free fragments in */
	while (pf)
	{
		REGION_FRAGMENT *pf_next;
		pf_next = pf->pnext;
 		chkdsk_free_fragment(pgl, pf);
		pf = pf_next;
	}

}
/* Free fragments that are in use tracking mapped clusters */
static void free_mapped_chain_list(CHKDSK_CONTEXT *pgl)
{
    chkdsk_free_list(pgl, (REGION_FRAGMENT *)  pgl->used_segment_list);
    pgl->used_segment_list = 0;
}

static int chkdsk_write_check_files(CHKDSK_CONTEXT *pgl)
{
REGION_FRAGMENT *pf;
int files_written;
dword end_location;

	files_written = 0;
	/* Write check files */
	{
	REGION_FRAGMENT *pf_chain_start;
	dword current_file_no;
    	pf_chain_start = 0;
    	current_file_no = pgl->current_file_no;			/* May occur in several passes so start from where we left off */
    	pf = (REGION_FRAGMENT *) pgl->lost_chain_list;
    	while (pf && files_written < 1000)
    	{
			end_location = pf->end_location & CHKDSK_NOT_TERMINATOR;

			if (end_location) /* If it is not just a place holder for the next value */
			{
    			if (!pf_chain_start)
    				pf_chain_start = pf;

   				if (!pf->pnext || !pf->pnext->end_location) /* Make sure the last file is terminated, it may not be if the scan ran out of memory */
   					pf->end_location |= CHKDSK_TERMINATOR;
				/* Count lost clusters and chains */
    			pgl->pstats->n_lost_clusters += (end_location - pf->start_location + 1);
			}
    		if (pf->end_location & CHKDSK_TERMINATOR)
    		{
				pgl->pstats->n_lost_chains += 1;
				/* Make sure all lost cahins are terminated */
   				if (!fatop_pfaxxterm(pgl->drive_structure, end_location))
    			{
    				files_written = -1;
    				break;
    			}
    			if (!chkdsk_build_chk_file(pgl, pf_chain_start->start_location, current_file_no, &current_file_no))
    			{
    				files_written = -1;
    				break;
    			}
    			files_written += 1;
				pgl->pstats->n_checkfiles_created += 1;
    			pf_chain_start = 0;
			}
    		pf = pf->pnext;
		}
		pgl->current_file_no	= current_file_no;			/* May occur in several passes so start from where we left off */
	}
	if (!fatop_flushfat(pgl->drive_structure))
		return(-1);
	return(files_written);
}

static BOOLEAN chkdsk_make_check_dir(void)
{
    byte dirname[30];
#ifdef RTFS_MAJOR_VERSION
    rtfs_cs_strcpy(dirname, (byte *) ("\\CHKFILES.CHK"), CS_CHARSET_NOT_UNICODE);
#else
    CS_OP_ASCII_TO_CS_STR(dirname, (byte *) ("\\CHKFILES.CHK"));
#endif
	if (!pc_mkdir(dirname))
		if (get_errno() != PEEXIST)
			return(FALSE);
	return(TRUE);
}

/* Given a lost chain number and a likely starting point for the
FILE???.CHK file create the file from the lost chain. */
void chkdsk_build_chk_name(byte *pretname, dword file_no)
{
    dword remainder;
    dword temp;
#ifdef RTFS_MAJOR_VERSION
        /* Create a file name */
        rtfs_cs_strcpy(pretname, (byte *) ("\\CHKFILES.CHK\\FILE000.CHK"), CS_CHARSET_NOT_UNICODE);
        temp = (dword) (file_no/100);
        *(pretname + 18) = (byte) ('0' + temp);
        remainder = (dword) (file_no - (temp * 100));
        temp = (dword) (remainder/10);
        *(pretname + 19) = (byte) ('0' + (byte) temp);
        remainder = (dword) (remainder - (temp * 10));
        *(pretname + 20) = (byte) ('0' + remainder);
#else
    byte filename[26];
        rtfs_strcpy((byte *) &filename[0], (byte *) (CS_OP_ASCII("\\CHKFILES.CHK\\FILE000.CHK")));
        temp = (long) (file_no/100);
        filename[18] = (byte) (CS_OP_ASCII('0') + temp);
        remainder = (long) (file_no - (temp * 100));
        temp = (long) (remainder/10);
        filename[19] = (byte) (CS_OP_ASCII('0') + (byte) temp);
        remainder = (long) (remainder - (temp * 10));
        filename[20] = (byte) (CS_OP_ASCII('0') + remainder);
        /* Map to the native character set */
        CS_OP_ASCII_TO_CS_STR(pretname, filename);
#endif
}

static BOOLEAN chkdsk_build_chk_file(CHKDSK_CONTEXT *pgl, dword chain_start_cluster, dword current_file_no, dword *ret_file_no)
{
    int fd;
    PC_FILE *pfile;
    DDRIVE  *pdrive;
    byte filename[60];

    for ( ; current_file_no < 999;  current_file_no++)
    {
    	chkdsk_build_chk_name(&filename[0], current_file_no);
        /* Try to open it exclusive. This will fail if the file exists */
        fd = (int)po_open(filename, (word)PO_CREAT|PO_EXCL|PO_WRONLY, (word)PS_IREAD|PS_IWRITE);
		if (fd < 0)
		{  /* If it failed because it already exists, loop and try then next value, if it failed for another reason return failure */
			if (get_errno() != PEEXIST)
			{
				return(FALSE);
            }
		}
        else
        {
        /* Get underneath the file and set the first cluster to the	beginning of the lost_chain. */
            /* Get the file structure and semaphore lock the drive */
            pfile = pc_fd2file(fd, 0);
            if (pfile)
            {
                FINODE *pfinode;
                pdrive = pfile->pobj->pdrive;
                pfinode = pfile->pobj->finode;
                pc_pfinode_cluster(pdrive,pfinode, chain_start_cluster);
                /* Update the size. */
                pfinode->fsizeu.fsize = chkdsk_chain_size_bytes(pgl, chain_start_cluster);
                pc_set_file_dirty(pfile,TRUE);
                release_drive_mount(pdrive->driveno); /* Release lock, unmount if aborted */
            }
            /* Close the file. This will write everything out. */
            po_close(fd);
            break;
        }
    }
    /* If we get here it did not work */
    if (current_file_no == 999)
        return(FALSE);

    /* Return the next ??? for FILE???.CHK that will probably work */
    *ret_file_no =  current_file_no + 1;
    return(TRUE);
}

/* Must be updated for version 6 */
static dword chkdsk_get_dir_loop_guard(DDRIVE *pdr)
{
dword loop_guard;

	loop_guard = MAX_CLUSTERS_PER_DIR; /* Largest valid directory chain */
	if (loop_guard > DRIVE_INFO(pdr,maxfindex))
		loop_guard = DRIVE_INFO(pdr,maxfindex);

	loop_guard <<= DRIVE_INFO(pdr,log2_secpalloc); /* times sectors per cluster */
#ifdef RTFS_MAJOR_VERSION
	loop_guard *= DRIVE_INFO(pdr,inopblock);
#else
	loop_guard *= INOPBLOCK;
#endif
	return(loop_guard);
}

static dword chkdsk_get_chain_loop_guard(DDRIVE *pdr)
{
dword loop_guard;

	loop_guard = 0x7FFFFF; /* Largest possible chain, a 4 gigabyte file with a 512 byte cluster size */
	loop_guard >>= DRIVE_INFO(pdr,log2_secpalloc); /* divided by sectors per cluster for a 4 gig file with this cluster size */
	if (loop_guard > DRIVE_INFO(pdr,maxfindex))
		loop_guard = DRIVE_INFO(pdr,maxfindex);

	return(loop_guard);
}

static dword chkdsk_clusterstobytes(DDRIVE *pdr,dword n_clusters)
{
    /* don't recompute bytes per cluster - it's already in pdr */
    return (n_clusters*DRIVE_INFO(pdr, bytespcluster));
}

static BOOLEAN chkdsk_free_chain(CHKDSK_CONTEXT *pgl, dword cluster)
{
DDRIVE *pdr;
	/* Call fatop_freechain to release the file chain, fatop_freechain will release normal cluster chains and
	   also safely release cluster chains that are unterminated or looped back on themselves. Ignore the return
	   status */
	pdr = pgl->drive_structure;
	pgl->pstats->n_clusters_freed += chkdsk_chain_size_clusters(pgl, cluster);
	fatop_freechain(pdr, cluster, chkdsk_get_chain_loop_guard(pdr));
    return(fatop_flushfat(pdr));
}


/* scan_for_bad_lfns(DROBJ *pmom)
 *
 *  Scan through a directory and count all Win95 long file name errors.
 *
 *  Errors detected:
 *      Bad lfn checksums
 *      Bad lfn sequence numbers
 *      Stray lfn chains
 *      Incomplete lfn chains
 *
 *  If pgl->delete_bad_lfn is set, free up the directory space used up by
 *  invalid long file name information by deleting invalid chains
 *
 *  Returns: number of invalid lfn chains found
 *
 *  This fn is called by chkdsk. It is based on pc_findin
 */

#if (INCLUDE_VFAT)

#define FIRST_NAMESEG 0x40
#define NAMESEG_ORDER 0x3F

typedef struct lfninode {
                              /* The first LNF has 0x40 + N left */
        byte    lfnorder;     /* 0x45, 0x04, 0x03, 0x02, 0x01 they are stored in
                               reverse order */
        byte    lfname1[10];
        byte    lfnattribute; /* always 0x0F */
        byte    lfnres;       /* reserved */
        byte    lfncksum;   /* All lfninode in one dirent have the same chksum */
        byte    lfname2[12];
        word    lfncluster; /* always 0x0000 */
        byte    lfname3[4];
        } LFNINODE;

void pc_zeroseglist(SEGDESC *s);
void pc_addtoseglist(SEGDESC *s, dword my_block, int my_index);
BOOLEAN pc_deleteseglist(DDRIVE *pdrive, SEGDESC *s);

static dword scan_for_bad_lfns(DROBJ *pmom, int delete_bad_lfn)          /*__fn__*/
{
DROBJ    *pobj;
BLKBUFF  *rbuf;
DIRBLK   *pd;
DOSINODE *pi;
LFNINODE *lfn_node;
SEGDESC  s;
byte     lastsegorder;
dword    entries_processed, loop_guard, bad_lfn_count;
int inopblock;

    pc_zeroseglist(&s);
    lastsegorder = 0;
    entries_processed = 0;

    /* pobj will be used to scan through the directory */
    pobj = pc_mkchild(pmom);
    if (!pobj)
        return(0);

    bad_lfn_count = 0;

	/* Inodes per block is dependent on sector size for Rtfs 6, for Rtfs 4 it is fixed */
#ifdef RTFS_MAJOR_VERSION
	inopblock = pobj->pdrive->drive_info.inopblock;
#else
	inopblock = INOPBLOCK;
#endif

    /* For convenience. We want to get at block info here   */
    pd = &pobj->blkinfo;

    /* Read the data   */
    pobj->pblkbuff = rbuf = pc_read_blk(pobj->pdrive, pobj->blkinfo.my_block);

    loop_guard = chkdsk_get_dir_loop_guard(pobj->pdrive);

    while (rbuf)
    {
#ifdef RTFS_MAJOR_VERSION
        pi = (DOSINODE *) rbuf->data;
#else
        pi = (DOSINODE *) &rbuf->data[0];
#endif

        /* Look at the current inode   */
        pi += pd->my_index;

        while ( pd->my_index < inopblock)
        {
            /* End of dir if name is 0   */
            if (!pi->fname[0])
            {
                pc_release_buf(rbuf);
                pc_freeobj(pobj);
                return(bad_lfn_count);
            }

            if (pi->fname[0] == PCDELETE)
            {
                if (s.nsegs)
                {
                    /* lfn chain interrupted by empty dir entry */
                    bad_lfn_count++;
                    if (delete_bad_lfn)
                        pc_deleteseglist(pobj->pdrive, &s);
                    pc_zeroseglist(&s);
                    lastsegorder = 0;
                }
            }
            else
            {
                if (pi->fattribute == CHICAGO_EXT)
                {
                    /* Found a piece of an lfn */
                    lfn_node = (LFNINODE *) pi;

                    if (lfn_node->lfnorder & FIRST_NAMESEG)
                    {
                        if (s.nsegs) /* if a chain already exists */
                        {
                            /* lfn chain begins in the middle of another one;
                               Delete the one that was interrupted, keep the
                               new one. */
                            bad_lfn_count++;
                            if (delete_bad_lfn)
                                pc_deleteseglist(pobj->pdrive, &s);
                            pc_zeroseglist(&s);
                        }
                        lastsegorder = lfn_node->lfnorder;
                        pc_addtoseglist(&s, pd->my_block, pd->my_index);
                        s.ncksum = lfn_node->lfncksum;
                    }
                    else
                    {
                        /* optimization - the current segment should first be
                           linked onto the chain in each of the branches
                           below */
                        pc_addtoseglist(&s, pd->my_block, pd->my_index);
                        if ((s.nsegs - 1) &&
                            (lfn_node->lfnorder & NAMESEG_ORDER) == (lastsegorder & NAMESEG_ORDER) - 1 &&
                            lfn_node->lfncksum == s.ncksum )
                        {
                            /* Sequence number and checksum match; Add new
                               segment to lfn chain   */
                            lastsegorder = lfn_node->lfnorder;
                        }
                        else
                        {
                            /* New segment has a checksum or sequence number
                               that doesn't match the current chain; Delete
                               the whole chain plus new segment */
                            bad_lfn_count++;
                            if (delete_bad_lfn)
                                pc_deleteseglist(pobj->pdrive, &s);
                            pc_zeroseglist(&s);
                            lastsegorder = 0;
                        }
                    }
                }
                else
             /* if (pi->fattribute != CHICAGO_EXT) */
                {
                    /* Found a file entry - make sure our lfn chain matches
                       (if we have one) */
                    if (s.nsegs) /* if a chain has been built up */
                    {
                        if (pc_cksum((byte*)pi) != s.ncksum ||
                            (lastsegorder & NAMESEG_ORDER) != 1)
                        {
                            /* chain's checksum doesn't match the DOSINODE,
                               or the last sequence number isn't 1; Delete
                               the lfn chain */
                            bad_lfn_count++;
                            if (delete_bad_lfn)
                                pc_deleteseglist(pobj->pdrive, &s);
                        }
                        /* We want to release this chain, whether good or bad */
                        pc_zeroseglist(&s);
                        lastsegorder = 0;
                    }
                }       /* if (!CHICAGO_EXT) */
            }           /* if (!PCDELETE) */
            pd->my_index++;
            pi++;
            entries_processed += inopblock;
            /* Check for endless loop */
            if (entries_processed > loop_guard)
            	break;
        }
        /* Current block is clean; go to next one */
        pc_release_buf(rbuf);
        /* Check for endless loop */
        if (entries_processed > loop_guard)
            break;
        /* Update the objects block pointer */
        if (!pc_next_block(pobj))
            break;
        pd->my_index = 0;
        pobj->pblkbuff = rbuf = pc_read_blk(pobj->pdrive, pobj->blkinfo.my_block);
    }

    pc_freeobj(pobj);
    return (bad_lfn_count);
}
#endif
