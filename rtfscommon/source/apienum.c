/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* File APIENUM.C */
/* pc_enumerate - List select all directory entries that match rules and
*                  allow a callback  routine to process each on.
*
*  Description - This routine traverses a subdirectory tree. It tests each
*  directory entry to see if it matches user supplied selection criteria.
*  if it does match the criteria a user supplied callback function is
*  called with the full path name of the directory entry and a pointer
*  to a DSTAT structure that contains detailed information about the
*  directory entry. (see the manual page for a detailed description of the
*  DSTAT structure.
*
*  Selection criteria - Two arguments are used to determine the selection
*  criteria. On is a flags word that specifies attributes the other is
*  a pattern that specifies a wild card pattern.
*  The flags argument contains a bitwise or of one or more of the following:
*    MATCH_DIR      - Select directory entries
*    MATCH_VOL      - Select volume labels
*    MATCH_FILES    - Select files
*    MATCH_DOT      - Select the '.'  entry MATH_DIR must be true also
*    MATCH_DOTDOT   - Select the '..' entry MATCH_DIR must be true also
*
*  The selection pattern is a standard wildcard pattern such as '*.*' or
*  *.txt.
*  Note: Patterns don't work the same for VFAT and DOS 8.3. If VFAT is
*  enable the pattern *.* will return any file name that has a '.' in it
*  in 8.3 systems it returns all files.
*
*  Note: pc_enumerate() requires a fair amount of buffer space to function.
*  Instead of allocating the space internally we require that the application
*  pass two buffers of size EMAXPATH_BYTES in to the function. See below.
*
*  Se
*  Summary:
*
* int pc_enumerate(
*   byte * from_pattern_buffer - pointer to a scratch buffer of size EMAXPATH_BYTES
*   byte * spath_buffer        - pointer to a scratch buffer of size EMAXPATH_BYTES
*   byte * dpath_buffer        - pointer to a scratch buffer of size EMAXPATH_BYTES
*   byte * root_search         - Root of the search IE C:\ or C:\USR etc.
*   word match_flags           - Selection flags (see above)
*   byte match_pattern         - Match pattern (see above)
*   int     maxdepth           - Maximum depth of the traversal.
*                                Note: to scan only one level set this to
*                                1. For all levels set it to 99
*   PENUMCALLBACK pcallback    - User callback function. (see below)
*
*  Return value
*   pc_enumerate() returns 0 unless the callback function returns a non-
*   zero value at any point. If the callback returns a non-zero value the
*   scan terminates imediately and returns the returned value to the
*   application.
*
*   errno is set to one of the following
*   pc_enumerate() does not set errno
*
*
*  About the callback.
*
*  The callback function is a function that returns an integer and is passed
*  the fully qualified path to the current directory entry and a DSTAT
*  structure. The callback fuction must return 0 if it wishes the scan to
*  continue or any other integer value to stop the scan and return the
*  callback's return value to the application layer.
*
* Examples
*
* The next two function implement a multilevel directory scan.
* int rdir_callback(byte *path, DSTAT *d) {MY_PRINTF("%s\n", path);return(0);}
*
* rdir(byte *path, byte *pattern)
* {
*   pc_enumerate(from_path,from_pattern,spath,dpath,path,
*   (MATCH_DIR|MATCH_VOL|MATCH_FILES), pattern, 99, rdir_callback);
* }
*
* Poor mans deltree package
* int delfile_callback(byte *path, DSTAT *d) {
*     pc_unlink(path);  return(0);
* }
* int deldir_callback(byte *path, DSTAT *d) {
*     pc_rmdir(path); return(0);
* }
*
*
* deltree(byte *path)
* {
* int i;
*  ==> First delete all of the files
*   pc_enumerate(from_path,from_pattern,spath,dpath,path,
*   (MATCH_FILES), "*",99, delfile_callback);
*   i = 0;
*   ==> Now delete all of the dirs.. deleting path won't  work until the
*   ==> tree is empty
*   while(!pc_rmdir(path) && i++ < 50)
*       pc_enumerate(from_path,from_pattern,spath,dpath,path,
*       (MATCH_DIR), "*", 99, deldir_callback);
* }
*
*/

#include "rtfs.h"

#if (!INCLUDE_VFAT)
BOOLEAN rtfs_cs_patcmp_8(byte *p, byte *pattern, BOOLEAN dowildcard);
BOOLEAN rtfs_cs_patcmp_3(byte *p, byte *pattern, BOOLEAN dowildcard);
#endif

static int get_parent_path(byte *parent, byte *path, int use_charset);
static int dirscan_isdot(DSTAT *statobj);
static int dirscan_isdotdot(DSTAT *statobj);

#define COMPILED_MAX_DEPTH 128

#if (INCLUDE_CS_UNICODE)
int pc_enumerate_cs( /* __apifn__ */
                 byte    * from_path_buffer,
                 byte    * from_pattern_buffer,
                 byte    * spath_buffer,
                 byte    * dpath_buffer,
                 byte    * root_search,
                 word    match_flags,
                 byte    * match_pattern,
                 int     maxdepth,
                 PENUMCALLBACK pcallback,
                 int use_charset)
#else
int pc_enumerate( /* __apifn__ */
                 byte    * from_path_buffer,
                 byte    * from_pattern_buffer,
                 byte    * spath_buffer,
                 byte    * dpath_buffer,
                 byte    * root_search,
                 word    match_flags,
                 byte    * match_pattern,
                 int     maxdepth,
                 PENUMCALLBACK pcallback)
#endif
{
    int     dir_index[COMPILED_MAX_DEPTH];
    dword   dir_block[COMPILED_MAX_DEPTH];
    int depth;
    DSTAT statobj;
    int process_it;
    int dodone;
    int call_back;
    int ret_val;
    int step_into_dir;
    byte all_files[8];
/* Guard against endless looping */
#define MAX_LOOP_COUNT 327680   /* A lot but we may be enumerating a large tree */
    dword loop_count = 0;
#if (!INCLUDE_VFAT)
    int i;
    /* IN 8.3 systems we parse the match pattern into file and ext parts */
    byte filepat[9];
    byte extpat[9];
#endif


    if (!from_path_buffer ||!from_pattern_buffer || !spath_buffer || !dpath_buffer || !root_search || !match_pattern)
    {
        rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
        return(-1);
    }


#if (!INCLUDE_VFAT)
    rtfs_cs_ascii_fileparse(filepat, extpat, (byte *)match_pattern);
    for(i = 0; i < 8; i++) if (filepat[i]==' ') filepat[i]=0;
    for(i = 0; i < 3; i++) if (extpat[i]==' ') extpat[i]=0;
#endif

    /* Create the "ALL" search pattern */
    {
    byte *p;
    p = &all_files[0];
    CS_OP_ASSIGN_ASCII(p,'*', CS_CHARSET_ARGS); /* * */
    CS_OP_INC_PTR(p, CS_CHARSET_ARGS);
#if (!INCLUDE_VFAT)
    CS_OP_ASSIGN_ASCII(p,'.', CS_CHARSET_ARGS); /* *.* */
    CS_OP_INC_PTR(p, CS_CHARSET_ARGS);
    CS_OP_ASSIGN_ASCII(p,'*', CS_CHARSET_ARGS);
    CS_OP_INC_PTR(p, CS_CHARSET_ARGS);
#endif
    CS_OP_TERM_STRING(p, CS_CHARSET_ARGS);
    }
    ret_val = 0;
    rtfs_cs_strcpy((byte *)from_path_buffer, (byte *) root_search, CS_CHARSET_ARGS);

    depth = 0;

    if (maxdepth > COMPILED_MAX_DEPTH)
        return(0);

    pc_mpath(from_pattern_buffer, from_path_buffer, all_files, CS_CHARSET_ARGS);

    dir_index[0] = 0;
    dir_block[0] = 0;

    do
    {
        if (loop_count++ > MAX_LOOP_COUNT)
            goto endless_loop;

        dodone = 0;
        step_into_dir = 0;
#if (INCLUDE_EXFATORFAT64)
		if (match_flags&MATCH_SYSSCAN)
			statobj.search_backwards_if_magic=SYS_SCAN_MAGIC_NUMBER;
#endif
#if (INCLUDE_CS_UNICODE)
        if (pc_gfirst_cs(&statobj, (byte *)from_pattern_buffer, CS_CHARSET_ARGS))
#else
        if (pc_gfirst(&statobj, (byte *)from_pattern_buffer))
#endif
        {
            dodone = 1;
            process_it = 0;
            do
            {
                if (loop_count++ > MAX_LOOP_COUNT)
                    goto endless_loop;
                step_into_dir = 0;
                if (!dir_block[depth] || ((dir_block[depth] ==
                    ((DROBJ *) (statobj.pobj))->blkinfo.my_block ) &&
                    (dir_index[depth] ==
                    ((DROBJ *) (statobj.pobj))->blkinfo.my_index )))
                process_it = 1;
                if (process_it)
                {
                    call_back = 1;

                    /* Don't report directories if not requested */
                    if ((statobj.fattribute & ADIRENT) &&
                        !(match_flags & MATCH_DIR) )
                        call_back = 0;

                    /* Don't report volumes if not requested */
                    if (call_back && (statobj.fattribute & AVOLUME) &&
                        !(match_flags & MATCH_VOL) )
                        call_back = 0;

                    /* Don't report plain files if not requested */
                    if (call_back && !(statobj.fattribute & (AVOLUME|ADIRENT)) &&
                        !(match_flags & MATCH_FILES) )
                        call_back = 0;

                    /* Don't report DOT if not requested */
                    if (call_back && dirscan_isdot(&statobj) && !(match_flags & MATCH_DOT))
                        call_back = 0;

                    /* Don't report DOTDOT if not requested */
                    if (call_back && dirscan_isdotdot(&statobj) && !(match_flags & MATCH_DOTDOT))
                        call_back = 0;

                    if (call_back)
                    {
                    /* Take it if the pattern match work */
#if (INCLUDE_VFAT)
                        /* Under INCLUDE_VFAT do a pattern match on the long file name */
                        if (statobj.lfname[0])
                            call_back = pc_patcmp_vfat(match_pattern, statobj.lfname, TRUE, CS_CHARSET_ARGS);
                        else
                            call_back = pc_patcmp_vfat(match_pattern, (byte *)(statobj.filename), TRUE, CS_CHARSET_ARGS);
#else
                        /* Non INCLUDE_VFAT uses 8.3 matching conventions */
                        rtfs_cs_ascii_fileparse(filepat, extpat, (byte *)match_pattern);
                        call_back =
                            rtfs_cs_patcmp_8((byte *)&statobj.fname[0], (byte *)&filepat[0] , TRUE);
                        call_back = call_back &&
                            rtfs_cs_patcmp_3((byte *)&statobj.fext[0],  (byte *)&extpat[0]  , TRUE);
#endif
                    }
                    /* Construct the full path */
#if (INCLUDE_VFAT)
                    pc_mpath(dpath_buffer, from_path_buffer, (byte *)statobj.lfname, CS_CHARSET_ARGS);
#else
                    pc_mpath(dpath_buffer, from_path_buffer, (byte *)statobj.filename, CS_CHARSET_ARGS);
#endif
#if (INCLUDE_EXFATORFAT64)
					if (match_flags&MATCH_SYSSCAN)
						call_back=1;	/*A system scan takes all */
#endif
                    if (call_back && pcallback)
                    {
                        /* If the callback returns non zero he says quit */
                        ret_val = pcallback(dpath_buffer, &statobj);
#if (INCLUDE_EXFATORFAT64)
						if ((match_flags&MATCH_SYSSCAN)&&(statobj.pobj&&((DROBJ *)(statobj.pobj))->finode))
						{ /* If inside EXFAT system, gfirst et al did not release the finode, so do it now */
							pc_freei(((DROBJ *)(statobj.pobj))->finode); /* Release the current */
							((DROBJ *)(statobj.pobj))->finode = 0;
						}
#endif
                        if (ret_val)
                        {
                            pc_gdone(&statobj);
                            goto ex_it;
                        }
                    }
                }
                /* If it is a subdirectory and we are still in our depth range
                then process the subdirectory */
                if (process_it && (depth < maxdepth) &&
                    !dirscan_isdot(&statobj) &&
                    !dirscan_isdotdot(&statobj))
                {
                    /* Construct the full path */
#if (INCLUDE_VFAT)
                    pc_mpath(spath_buffer, from_path_buffer, (byte *)statobj.lfname, CS_CHARSET_ARGS);
#else
                    pc_mpath(spath_buffer, from_path_buffer, (byte *)statobj.filename, CS_CHARSET_ARGS);
#endif
                    if (statobj.fattribute & ADIRENT) /* Changed from (AVOLUME | ADIRENT) which was an obscure problem having no side effects except when the volume label is all spaces (something windows allows apparently) */
                    {
#if (INCLUDE_CS_UNICODE)
                        if (pc_gnext_cs(&statobj, CS_CHARSET_ARGS))
#else
                        if (pc_gnext(&statobj))
#endif
                        {
                            /* Mark where we are */
                            dir_block[depth] = ((DROBJ *) (statobj.pobj))->blkinfo.my_block;
                            dir_index[depth] = ((DROBJ *) (statobj.pobj))->blkinfo.my_index;
                            dodone = 0;
                            pc_gdone(&statobj);
                        }
                        else
                        {
                            dodone = 0;
                            pc_gdone(&statobj);
                            dir_index[depth] = -1;
                        }
                        depth += 1;
                        dir_block[depth] = 0;
                        dir_index[depth] = 0;
                        rtfs_cs_strcpy((byte *)from_path_buffer, (byte *)spath_buffer, CS_CHARSET_ARGS);
                        pc_mpath(from_pattern_buffer, from_path_buffer, all_files, CS_CHARSET_ARGS);
                        /* Force it to stay in the loop but not execute
                           gnext.. execute gfirst instead */
                        step_into_dir = 1;
                        break;
                    } /* (statobj.fattribute & (AVOLUME | ADIRENT)) */
                } /* if (process_it && (depth < maxdepth) .... */
                /* pc_gnext will not execute if step_into_dir is non zero */
#if (INCLUDE_CS_UNICODE)
            } while (pc_gnext_cs(&statobj, CS_CHARSET_ARGS));   /* or get the next file in dir */
#else
            } while (pc_gnext(&statobj));   /* or get the next file in dir */
#endif
            if (!step_into_dir)
            {
                /* We broke out so we're done with this level */
                dir_index[depth] = -1;
                /* If we broke out and pc_gdone is needed */
                if (dodone)
                    pc_gdone(&statobj);
            }
        } /* if (pc_gfirst(&statobj, (byte *)from_pattern_buffer)) */
        else
        {
            /* gfirst failed so we're done with this level */
            dir_index[depth] = -1;
        }
        if (!step_into_dir)
        {
            while (depth > 0 && dir_index[depth] == -1)
            {
                if (!get_parent_path(from_path_buffer, from_path_buffer, CS_CHARSET_ARGS))
                    goto endless_loop;
                pc_mpath(from_pattern_buffer, from_path_buffer, all_files, CS_CHARSET_ARGS);
                depth--;
            }
        }
      /* Keep trying unless we are in the root of the search and hit EOF */
    } while (!(depth == 0 && dir_index[0] == -1));
ex_it:
    return (ret_val);
endless_loop:
    rtfs_set_errno(PEINVALIDDIR, __FILE__, __LINE__);
    return (-1);
}

static int get_parent_path(byte *parent, byte *path, int use_charset)                                   /*__fn__*/
{
    byte *last_backslash = 0;
    byte *parent_init = parent;
    int  size = 0;

    while (CS_OP_IS_NOT_EOS(path, use_charset))
    {
        size++;
        if (CS_OP_CMP_ASCII(path,'\\', use_charset))
        {
            last_backslash = parent;
        }
        CS_OP_CP_CHR(parent,path, use_charset);
        CS_OP_INC_PTR(parent, use_charset);
        CS_OP_INC_PTR(path, use_charset);
    }
    /* This is to prevent catastrophie if caller ignores failure */
    CS_OP_TERM_STRING(parent, use_charset);

    //if (size < 3)
    //    return (0);
    /* Replace the last backslash with NULL. Unless we are at the top.. we will increment
       first and leave it at the top as \\ so \FRED yields \ */
    if (last_backslash && last_backslash == parent_init)
        CS_OP_INC_PTR(last_backslash, use_charset);
    if (last_backslash)
    {
         CS_OP_TERM_STRING(last_backslash, use_charset);
    }
    return (1);
}


/************************************************************************
*                                                                      *
* File system abstraction layer                                        *
*                                                                      *
************************************************************************/


static int dirscan_isdot(DSTAT *statobj)
{
    char *p;
    p = (char *)statobj->filename;
    if (*p=='.' && *(p+1) == 0)
        return(1);
    else
        return(0);
}

static int dirscan_isdotdot(DSTAT *statobj)
{
    char *p;
    p = (char *)statobj->filename;
    if (*p=='.' && *(p+1) == '.' && *(p+2) == 0)
        return(1);
    else
        return(0);
}
