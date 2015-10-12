/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* APISETVOL.C - Contains user api level source code.

    The following routines are included:

    pc_set_volume         - Set Volume label.
    pc_get_volume         - Retrieve Volume label.

*/

#include "rtfs.h"


static DROBJ *pc_find_volume(DROBJ *proot_obj);

#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */

/***************************************************************************
    pc_set_volume         - Set Volume label.

Description
    BOOLEAN pc_set_volume(byte *drive_id, byte  *volume_label)

    Create a volume label or rename the current volume label.

    The volume label is a directory entry in the root directory of the volume.


Returns
    Returns TRUE if it was able to create or chenge the volume label.

    errno is set to one of the following
    0               - No error
    PEINVALIDDRIVEID- Drive component is invalid
    PEINVALIDPARMS   - The label must contain only valid short file name characters. Lower case characters are converted to upper case.
    An ERTFS system error
****************************************************************************/
static void pc_cp_volume_to8dot3(byte *to, byte *from);

#if (INCLUDE_CS_UNICODE)
BOOLEAN pc_set_volume_cs(byte *driveid, byte  *volume_label,int use_charset)
#else
BOOLEAN pc_set_volume(byte *driveid, byte  *volume_label)
#endif
{
    DROBJ *pobj, *root_obj;
    byte  *path;
    byte  *filename;
    byte  fileext[4];
    BOOLEAN  ret_val;
    int driveno;
    DDRIVE *pdrive;
    int p_set_errno;
    CHECK_MEM(BOOLEAN, 0)   /* Make sure memory is initted */

    root_obj = 0;
    pobj = 0;
    p_set_errno = 0;
    ret_val = FALSE;
    rtfs_clear_errno();  /* pc_mkdir: clear error status */

	if (!volume_label)
	{
        rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
		return(FALSE);
	}

    /* Get the drive and make sure it is mounted   */
    driveno = check_drive_name_mount(driveid, CS_CHARSET_ARGS);
    if (driveno < 0)
    {
        /* errno was set by check_drive */
        return(FALSE);
    }

    pdrive = pc_drno2dr(driveno);

#if (INCLUDE_EXFATORFAT64)
	if (ISEXFATORFAT64(pdrive))
	{
    	ret_val = pcexfat_set_volume(pdrive, volume_label,CS_CHARSET_ARGS);
    	release_drive_mount(driveno);/* Release lock, unmount if aborted */
    	return(ret_val);
	}
#endif

    /* Allocate scratch buffers in the DRIVE structure. */
    if (!pc_alloc_path_buffers(pdrive))
        goto errex;
    path = pdrive->pathname_buffer;
    filename = pdrive->filename_buffer;


	/* Take the volume label string and make it into a short file name and extension */
    pc_cp_volume_to8dot3(path, volume_label);

    if (!pc_cs_valid_sfn(path, FALSE)) /* Verify the result is a valid file name, not case sensitive */
    {
bad_path:
        p_set_errno = PEINVALIDPARMS;
        goto errex;
    }
	/* Get the file and extension padded to 8 3 with spaces */
    if (!rtfs_cs_ascii_fileparse(filename, &fileext[0], path))
		goto bad_path;

	/* Get the root, if an error, errno is set below */
    root_obj = pc_get_root(pdrive);
	if (!root_obj)
        goto errex;

	/* Search the root for a volume label */
	pobj =	pc_find_volume(root_obj);
	/* We found it, just change the name and update the directory entry */
	if (pobj)
	{
    	copybuff( pobj->finode->fname, filename, 8);
    	copybuff( pobj->finode->fext, &fileext[0], 3);
   		if (pc_update_inode(pobj, TRUE, DATESETUPDATE))
   			ret_val = TRUE;
	}
    else
    {
        if (get_errno() != PENOENT) /* If pobj is NULL we abort on abnormal errors */
            goto errex;
        rtfs_clear_errno();
#if (!INCLUDE_VFAT)
        pobj = pc_mknode( root_obj,filename, fileext, AVOLUME, 0, CS_CHARSET_NOT_UNICODE);
#else
        pobj = pc_mknode( root_obj, path, fileext, AVOLUME, 0, CS_CHARSET_NOT_UNICODE);
#endif

        if (pobj)
        {
            p_set_errno = 0;
            ret_val = TRUE;
        }
        else
        {
            /* pc_mknode has set errno */
            goto errex;
        }
    }

errex:
    if (pobj)
        pc_freeobj(pobj);
    if (root_obj)
        pc_freeobj(root_obj);
    pc_release_path_buffers(pdrive);
    if (p_set_errno)
        rtfs_set_errno(p_set_errno, __FILE__, __LINE__);
    if (!release_drive_mount_write(driveno))/* Release lock, unmount if aborted */
        ret_val = FALSE;
    return(ret_val);
}

/* Take the volume label string and make it into a short file name and extesion */
static void pc_cp_volume_to8dot3(byte *to, byte *from)
{
int n_file_chars,n_ext_chars,i;
byte *p;
BOOLEAN dofile = TRUE;

	*to = 0;
	/* Calculate how many characters before and after a seperating period are needed for valid 8.3
	   this method also accounts for 2 byte JIS codes */
	n_file_chars = n_ext_chars = 0;
	p = from;
	while (*p)
	{
	byte *next;
	int  n_chars;

		next = p + 1;
		CS_OP_INC_PTR(p, CS_CHARSET_NOT_UNICODE);
		if (next == p)
			n_chars = 1;
		else
			n_chars = 2;

		if (dofile)
		{
			if ((n_file_chars + n_chars) <= 8)
			{
				n_file_chars += n_chars;
			}
			else
				dofile = FALSE;	/* FALL Through and do extension */
		}
		if (!dofile)
		{
			if (n_ext_chars + n_chars <= 3)
			{
				n_ext_chars += n_chars;
			}
			else
				break;
		}
	}

	/* Now block copy based on character counts */
	for (i = 0; i < n_file_chars; i++)
	{
		*to++ = *from++;
		*to = 0;
	}
	/* add .ext if needed */
	if (n_ext_chars)
	{
		*to++ = (byte) '.';
		*to = 0;
		for (i = 0; i < n_ext_chars; i++)
		{
			*to++ = *from++;
			*to = 0;
		}
	}
}


#endif /* Exclude from build if read only */
/***************************************************************************
    pc_get_volume         - Get Volume label.

Description
    BOOLEAN pc_get_volume(byte *drive_id, byte  *volume_label_buffer)

    Retrieve the volume label and place it in the volume_label_buffer.

    The volume label is a directory entry in the root directory of the volume.


Returns
    Returns TRUE if it was able to retrieve the volume label.

    errno is set to one of the following
    0               - No error
    PEINVALIDDRIVEID- Drive component is invalid
    PEINVALIDPARMS   - volume_lable buffer not provided.
    An ERTFS system error
****************************************************************************/

#if (INCLUDE_CS_UNICODE)
BOOLEAN pc_get_volume_cs(byte *driveid, byte  *volume_label,int use_charset)
#else
BOOLEAN pc_get_volume(byte *driveid, byte  *volume_label)
#endif
{

    DROBJ *pobj, *root_obj;
    BOOLEAN  ret_val;
    int i,driveno;
    DDRIVE *pdrive;
    CHECK_MEM(BOOLEAN, 0)   /* Make sure memory is initted */

	if (!volume_label)
	{
        rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
		return(FALSE);
	}

    rtfs_clear_errno();

    /* Get the drive and make sure it is mounted   */
    driveno = check_drive_name_mount(driveid, CS_CHARSET_ARGS);
    if (driveno < 0)
        return(FALSE); /* errno was set by check_drive */

    root_obj = 0;
    pobj = 0;
    ret_val = FALSE;

    pdrive = pc_drno2dr(driveno);
	if (!pdrive)	/* Won't happen */
        goto errex;

#if (INCLUDE_EXFATORFAT64)
	if (ISEXFATORFAT64(pdrive))
	{
    	ret_val = pcexfat_get_volume(pdrive, volume_label, CS_CHARSET_ARGS);
    	release_drive_mount(driveno);/* Release lock, unmount if aborted */
    	return(ret_val);
	}
#endif

	/* Get the root, if an error, errno is set below */
    root_obj = pc_get_root(pdrive);
	if (!root_obj)
        goto errex;
	/* Search the root for a volume label */
	pobj =	pc_find_volume(root_obj);
	/* We found it copy the results to volume_label */
	if (pobj)
	{
		byte filename[10],fileext[4];

    	copybuff( &filename[0], pobj->finode->fname, 8);
    	copybuff( &fileext[0],pobj->finode->fext, 3);

		/* Replace trailing spaces with null terminate and then concatonate the name and extension
		   Works for both JIS and ASCII */
		filename[8] = fileext[3] = 0;
		for(i = 7; i > 0; i--)
		{
			if (filename[i] == (byte) ' ')
				filename[i] = 0;
			else
				break;
		}
		for(i = 3; i > 0; i--)
		{
			if (fileext[i] == (byte) ' ')
				fileext[i] = 0;
			else
				break;
		}
        rtfs_cs_strcpy(volume_label, &filename[0], CS_CHARSET_NOT_UNICODE);
		rtfs_cs_strcat(volume_label, &fileext[0], CS_CHARSET_NOT_UNICODE);

        ret_val = TRUE;
	}
errex:
    if (pobj)
        pc_freeobj(pobj);
    if (root_obj)
        pc_freeobj(root_obj);
    release_drive_mount(driveno); /* Release lock, unmount if aborted */
    return(ret_val);
}


/* Search the root for the volume label, if found return an initialized drobj. If not found and no system errors errno is set to PENOENT */
static DROBJ *pc_find_volume(DROBJ *proot_obj)
{
	DROBJ *pobj;
	byte filename[10],fileext[4];
    pobj = pc_get_inode(0, proot_obj, &filename[0], &fileext[0], GET_INODE_STAR, CS_CHARSET_NOT_UNICODE);
    while (pobj)
    {
        if (pc_isavol(pobj))
        	break;
        pobj = pc_get_inode(pobj, proot_obj, &filename[0], &fileext[0], GET_INODE_STAR, CS_CHARSET_NOT_UNICODE);
    }
	return(pobj);
}
