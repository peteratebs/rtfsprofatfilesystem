/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* RTCOMMONGLUE.C - ProPlus Pro miscelaneous common functions */

#include "rtfs.h"



dword pc_finode_cluster(DDRIVE *pdr, FINODE *finode)  /* __fatfn__ */
{
    if (pdr->drive_info.fasize == 8)
        return ( (dword)finode->fcluster | ((dword)finode->fclusterhi << 16) );
    else
        return ( (dword)finode->fcluster );
}
void pc_pfinode_cluster(DDRIVE *pdr, FINODE *finode, dword value) /*__fatfn__ */
{
    finode->fcluster = (word)value;
    if (pdr->drive_info.fasize == 8)
        finode->fclusterhi = (word)(value >> 16);
}

dword pc_get_parent_cluster(DDRIVE *pdrive, DROBJ *pobj) /* __fatfn__ */
{
        if ((pdrive->drive_info.fasize == 8) &&
            (pobj->blkinfo.my_frstblock == pdrive->drive_info.rootblock))
            return(0);
        else
            return(pc_sec2cluster(pdrive,pobj->blkinfo.my_frstblock));
}

/****************************************************************************
int _pc_file_open(DDRIVE *pdrive, byte *name, word flag, word mode, dword extended, BOOLEAN *created, int use_charset)
 -  Open a file.

  This routine is the original po_open() API call with minor changes to accomodate
  RtfsProPlus

  Description
  Open the file for access as specified in flag. If creating use mode to
  set the access permissions on the file.

    Flag values are

      PO_BINARY   - Ignored. All file access is binary
      PO_TEXT     - Ignored
      PO_RDONLY   - Open for read only
      PO_RDWR     - Read/write access allowed.
      PO_WRONLY   - Open for write only
      PO_CREAT    - Create the file if it does not exist. Use mode to
      specify the permission on the file.
      PO_EXCL     - If flag contains (PO_CREAT | PO_EXCL) and the file already
      exists fail and set xn_getlasterror() to EEXIST
      PO_TRUNC    - Truncate the file if it already exists
      PO_NOSHAREANY   - Fail if the file is already open. If the open succeeds
      no other opens will succeed until it is closed.
      PO_NOSHAREWRITE-  Fail if the file is already open for write. If the open
      succeeds no other opens for write will succeed until it
      is closed.

      Mode values are

      PS_IWRITE   - Write permitted
      PS_IREAD    - Read permitted. (Always true anyway)

      Returns
      Returns a non-negative integer to be used as a file descriptor for
      calling read/write/seek/close otherwise it returns -1.

      errno is set to one of the following
      0                 - No error
      PENOENT           - Not creating a file and file not found
      PEINVALIDPATH    - Invalid pathname
      PENOSPC          - No space left on disk to create the file
      PEACCES           - Is a directory or opening a read only file for write
      PESHARE          - Sharing violation on file opened in exclusive mode
      PEEXIST           - Opening for exclusive create but file already exists
      PEEXIST           - Opening for exclusive create but file already exists
      An ERTFS system error
****************************************************************************/

BOOLEAN _synch_file_ptrs(PC_FILE *pfile);

/* Workhorse filio opne/create routine used by RtfsPro and ProPlus */
int _pc_file_open(DDRIVE *pdrive, byte *name, word flag, word mode, dword extended, BOOLEAN *created, int use_charset)
{
    int fd;
    PC_FILE *pfile;
    DROBJ *parent_obj;
    DROBJ *pobj;
    byte  *path;
    byte  *filename;
    byte  fileext[4];
    BOOLEAN open_for_write;
    BOOLEAN sharing_error;
    int p_set_errno;

    sharing_error = FALSE;
    parent_obj = 0;

    open_for_write = FALSE;
    p_set_errno = 0;

#if (!INCLUDE_RTFS_PROPLUS)
    RTFS_ARGSUSED_DWORD(extended);
#endif
#if (!RTFS_CFG_READONLY)    /* Read only file system, don't open files for writing */
    /* We will need to know this in a few places.   */
    if(flag & (PO_WRONLY|PO_RDWR))
        open_for_write = TRUE;
#endif
    fd = -1;
    pfile = pc_allocfile();
    if (pfile)
        fd = pfile->my_fd;
    else
    {
        rtfs_set_errno(PERESOURCEFILES, __FILE__, __LINE__);
        return(fd);
    }

    /* Allocate scratch buffers in the DRIVE structure. */
    if (!pc_alloc_path_buffers(pdrive))
        goto errex;
    path = pdrive->pathname_buffer;
    filename = pdrive->filename_buffer;

    /* Get out the filename and d:parent   */
    if (!pc_parsepath(path,filename,fileext,name, use_charset))
    {
        p_set_errno = PEINVALIDPATH;
        /* rtfs_set_errno(PEINVALIDPATH, __FILE__, __LINE__); */
        goto errex;
    }

    /* Find the parent   */
    /* pc_fndnode will set errno */
    parent_obj = pc_fndnode(path, use_charset);
    if (!parent_obj)
        goto errex;

    if (!pc_isadir(parent_obj) ||  pc_isavol(parent_obj))
    {
        p_set_errno = PENOENT;      /* Path is not a directory */
        goto errex;
    }

    pobj =  pc_get_inode(0, parent_obj,filename,(byte*)fileext, GET_INODE_MATCH, use_charset);
    if(pobj && (pc_isadir(pobj) || pc_isavol(pobj)) )
    {
        pc_freeobj(pobj);
        p_set_errno = PEACCES;      /* is a directory */
        goto errex;
    }
    if (pobj)
    {
        /* If we goto exit: we want them linked so we can clean up   */
        pfile->pobj = pobj;         /* Link the file to the object */
#if (INCLUDE_RTFS_PROPLUS)    /* Use ProPlus ffinode field */
        pfile->fc.plus.ffinode = pobj->finode;
#endif
        /* check the sharing conditions   */
        sharing_error = FALSE;
        if (pobj->finode->opencount != 1)
        {
        /* The file is already open by someone. Lets see if we are
            compatible */
            /* 1. We do not want to share with anyone   */
            if (flag & PO_NOSHAREANY)
                sharing_error = TRUE;
            /* 2. Someone else does not want to share   */
            if (pobj->finode->openflags & OF_EXCLUSIVE)
                sharing_error = TRUE;
            /* 3. We want exclusive write but already open for write   */
            if ( open_for_write && (flag & PO_NOSHAREWRITE) &&
                (pobj->finode->openflags & OF_WRITE))
                sharing_error = TRUE;
                /* 4. We want open for write but it is already open for
            exclusive */
            if ( (open_for_write) &&
                (pobj->finode->openflags & OF_WRITEEXCLUSIVE))
                sharing_error = TRUE;
            /* 5. Open for trunc when already open   */
            if (flag & PO_TRUNC)
                sharing_error = TRUE;
#if (INCLUDE_RTFS_PROPLUS)  /* Test for ProPlus sharing errors */
            /* 4. We want open for write but it is already open for
            transaction */
            if ( (open_for_write) &&
                (pobj->finode->openflags & OF_TRANSACTION))
                sharing_error = TRUE;
         /* Opened in write already and now opening for transaction */
             if ((extended & PCE_TRANSACTION_FILE) &&
                (pobj->finode->openflags & OF_WRITE))
                sharing_error = TRUE;
#endif
        }
        if (sharing_error)
        {
            p_set_errno = PESHARE;
            goto errex;
        }
        if ( (flag & (PO_EXCL|PO_CREAT)) == (PO_EXCL|PO_CREAT) )
        {
            p_set_errno = PEEXIST;      /* Exclusive fail */
            goto errex;
        }
        if(open_for_write && (pobj->finode->fattribute & ARDONLY) )
        {
            p_set_errno = PEACCES;      /* read only file */
            goto errex;
        }
        /* Removed file truncation code. basic file io and extended file IO have their own methods of thruncating.
           The common routine does not need to support it */
        if (created)
            *created = FALSE;
    }
#if (RTFS_CFG_READONLY)    /* Read only file system, don't create files */
    else    /* File not found */
    {
        if (get_errno() != PENOENT)
            goto errex;
        rtfs_clear_errno();      /* Clear PENOENT */
        p_set_errno = PEACCES;      /* read only file */
        RTFS_ARGSUSED_INT((int) mode);
        goto errex;
     }
#else
    else    /* File not found */
    {
        if (get_errno() != PENOENT)
            goto errex;
        if (!(flag & PO_CREAT))
        {
            p_set_errno = PENOENT;   /* Set PENOENT again, this time in debug mode to print the error */
            goto errex; /* File does not exist */
        }
        rtfs_clear_errno();      /* Clear PENOENT */
        /* Do not allow create if write bits not set   */
        if(!open_for_write)
        {
            p_set_errno = PEACCES;      /* read only file */
            goto errex;
        }
        /* Create for read only if write perm not allowed               */
        pobj = pc_mknode( parent_obj, filename, fileext, (byte) ((mode == PS_IREAD) ? ARDONLY : 0), 0, use_charset);
        if (!pobj)
        {
            /* pc_mknode has set errno  */
            goto errex;
        }

        pfile->pobj = pobj;         /* Link the file to the object */
#if (INCLUDE_RTFS_PROPLUS)    /* Use ProPlus ffinode field */
        pfile->fc.plus.ffinode = pobj->finode;
#endif
        if (created)
            *created = TRUE;
    }
#endif /* !RTFS_CFG_READONLY  (end do not create because read-only) */
    /* Set the file sharing flags in the shared finode structure   */
    /* clear flags if we just opened it .                          */
    if (pobj->finode->opencount == 1)
        pobj->finode->openflags = 0;

    if (flag & PO_BUFFERED)
	{
	/* use pc_load_file_buffer(pfinode, 1, FALSE) to allocate a file buffer to use with this file until it is closed
	   use sector 1 which is never a valid file location */
	   if (!pc_load_file_buffer(pobj->finode, 1, FALSE))
            goto errex;
        pobj->finode->openflags |= OF_BUFFERED;
	}

    if (open_for_write)
    {
        pobj->finode->openflags |= OF_WRITE;
        if (flag & PO_NOSHAREWRITE)
            pobj->finode->openflags |= OF_WRITEEXCLUSIVE;
    }
    if (flag & PO_NOSHAREANY)
        pobj->finode->openflags |= OF_EXCLUSIVE;
    pfile->flag = flag;         /* Access flags */
#if (INCLUDE_RTFS_PROPLUS)  /* ProPlus transaction files */
    if (extended & PCE_TRANSACTION_FILE)
        pobj->finode->openflags |= OF_TRANSACTION;
#endif

    p_set_errno = 0;
    if (parent_obj)
        pc_freeobj(parent_obj);
    pc_release_path_buffers(pdrive);
    return(fd);
errex:
    pc_release_path_buffers(pdrive);
    pc_freefile(pfile);
    if (parent_obj)
        pc_freeobj(parent_obj);
    if (p_set_errno)
        rtfs_set_errno(p_set_errno, __FILE__, __LINE__);
    return(-1);
}

BOOLEAN pc_alloc_path_buffers(DDRIVE *pdrive)
{
    pdrive->pathname_blkbuff  = pc_scratch_blk();
    pdrive->filename_blkbuff  = pc_scratch_blk();
    if (!pdrive->pathname_blkbuff || !pdrive->filename_blkbuff)
    {
        pc_release_path_buffers(pdrive);
        return(FALSE);
    }
    pdrive->pathname_buffer = pdrive->pathname_blkbuff->data;
    pdrive->filename_buffer = pdrive->filename_blkbuff->data;
    return(TRUE);

}

void pc_release_path_buffers(DDRIVE *pdrive)
{
    if (pdrive->pathname_blkbuff)
        pc_free_scratch_blk(pdrive->pathname_blkbuff);
    if (pdrive->filename_blkbuff)
        pc_free_scratch_blk(pdrive->filename_blkbuff);
    pdrive->pathname_blkbuff = pdrive->filename_blkbuff = 0;
}
/****************************************************************************
Miscelaneous File and file descriptor management functions

These functions are private functions used by the po_ file io routines.

    pc_fd2file -
    Map a file descriptor to a file structure. Return null if the file is
    not open. If an error has occured on the file return NULL unless
    allow_err is true.

    pc_allocfile -
    Allocate a file structure an return its handle. Return -1 if no more
    handles are available.

        pc_freefile -
        Free all core associated with a file descriptor and make the descriptor
        available for future calls to allocfile.

        pc_free_all_fil -
*****************************************************************************/

/*  Map a file descriptor to a file structure. Return null if the file is
not open or the flags do not match (test for write access if needed).
Get the file structure and semaphore lock the drive
*/


PC_FILE *pc_fd2file(int fd,int flags)                 /*__fn__*/
{
    PC_FILE *pfile;

    /* Get the file and associated drive structure with the crit sem locked */
    if (0 <= fd && fd < pc_nuserfiles())
    {
        pfile = prtfs_cfg->mem_file_pool+fd;
        OS_CLAIM_FSCRITICAL()
        if (!pfile->is_free)
        {
            if (!pfile->pobj)
            {
                /* An event (probably card removal or failure)
                   closed the file. Set errno and return. The
                   user must call po_close to clear this condition */
                rtfs_set_errno(PECLOSED, __FILE__, __LINE__);
            }
        /* If flags == 0. Any access allowed. Otherwise at least one
            bit in the file open flags must match the flags sent in */
            else if (!flags || (pfile->flag&flags))
            {
            	DDRIVE *pdrive;
				int driveno;
                /* dereference pobj while critical semaphore is still locked */
                driveno = pfile->pobj->pdrive->driveno;
                OS_RELEASE_FSCRITICAL()
                /* Claim the drive, double check that the file is still
                    good (was not closed out by a pc_dskfree() call) */
                pdrive = rtfs_claim_media_and_buffers(driveno);
                if (!pdrive)
				{
					rtfs_clear_errno();      /* Clear errno set by claim, we want PECLOSED */
					rtfs_set_errno(PECLOSED, __FILE__, __LINE__);
					return(0);
				}

                if (pfile->pobj)
                {
                    return(pfile);
                }
                else
                {
                     /* An event (probably card removal or failure)
                        closed the file. Set errno and return. The
                        user must call po_close to clear this condition */
                        rtfs_release_media_and_buffers(driveno);  /* pc_fd2file - clear Drive on error */
                        rtfs_clear_errno();      /* Make sure we set errno to PECLOSED */
                        rtfs_set_errno(PECLOSED, __FILE__, __LINE__);
                        return(0);
                }
            }
            else
                rtfs_set_errno(PEACCES, __FILE__, __LINE__);
        }
        else
            rtfs_set_errno(PEBADF, __FILE__, __LINE__);
        OS_RELEASE_FSCRITICAL()
    }
    else
        rtfs_set_errno(PEBADF, __FILE__, __LINE__);
    return(0);
}

/* Assign zeroed out file structure to an FD and return the structure. Return
0 on error. pfile->my_fd contains the file descriptor */
PC_FILE *pc_allocfile(void)                                  /*__fn__*/
{
    PC_FILE *pfile;
    int i;

    OS_CLAIM_FSCRITICAL()
    pfile = prtfs_cfg->mem_file_pool;
    for (i=0;i<pc_nuserfiles();i++, pfile++)
    {
        if (pfile->is_free)
        {
            rtfs_memset(pfile, 0, sizeof(PC_FILE));
            OS_RELEASE_FSCRITICAL()
            pfile->my_fd = i;
            return(pfile);
        }
    }
    OS_RELEASE_FSCRITICAL()
    return (0);
}

/* Free core associated with a file descriptor. Release the FD for later use   */
void pc_freefile(PC_FILE *pfile)
{
DROBJ *pobj;
    OS_CLAIM_FSCRITICAL()
    pobj = pfile->pobj;
    pfile->is_free = TRUE;
    OS_RELEASE_FSCRITICAL()
    if (pobj)
       pc_freeobj(pobj);
}

/* Note: all file buffering code was moved to rtfilebuffer.c */


#define ENUM_FLUSH 1
#define ENUM_TEST  2
#define ENUM_FREE  3
#define ENUM_RELEASE_PREALLOC 4

/* Release all file descriptors associated with a drive and free up all core
associated with the files called by pc_free_all_fil, pc_flush_all_fil,
pc_test_all_fil. The drive semaphore must be locked before this call is
entered.
*/
/* basic file flush if PROPLUS is not enabled */
BOOLEAN _pc_bfilio_flush(PC_FILE *pefile);

int pc_enum_file(DDRIVE *pdrive, int chore)             /*__fn__*/
{
    PC_FILE *pfile;
    DROBJ *pobj;
    int i;
    int dirty_count;

    dirty_count = 0;
    for (i=0; i < pc_nuserfiles(); i++)
    {
        OS_CLAIM_FSCRITICAL()
        pfile = prtfs_cfg->mem_file_pool+i;
        if (pfile && !pfile->is_free && pfile->pobj && pfile->pobj->pdrive == pdrive)
        {
            OS_RELEASE_FSCRITICAL()
            if (chore == ENUM_FLUSH)
            {
#if (INCLUDE_RTFS_PROPLUS)  /* ProPlus file flush */
                if (!_pc_efilio_flush(pfile))
                    return(-1);
#else
                if (!_pc_bfilio_flush(pfile))
                    return(-1);
#endif
            }
#if (INCLUDE_RTFS_PROPLUS)  /* ProPlus file purge preallocated clusters */
            else if (chore == ENUM_RELEASE_PREALLOC)
            {
                _pc_efilio_free_excess_clusters(pfile);
            }
#endif
            else if (chore == ENUM_TEST)
            {
                if (pc_check_file_dirty(pfile))
                    dirty_count += 1;
            }
            if (chore == ENUM_FREE)
            {
                /* Mark the file closed here. po_close must release it */
                OS_CLAIM_FSCRITICAL()
                pobj = pfile->pobj;
                pfile->pobj = 0;
                OS_RELEASE_FSCRITICAL()
                if (pobj)
                    pc_freeobj(pobj);
            }
        }
        else
        {
            OS_RELEASE_FSCRITICAL()
        }
    }
    return(dirty_count);
}

/* Return the user buffer and it calculates how many sectors can fit in it.
   Sectors are usually 512 bytes but could be larger.
   If no user buffers are assigned use a scratch block
   If the user buffer can't hold mimimum sector blocks then
   */

byte *pc_claim_user_buffer(DDRIVE *pdr, dword *pbuffer_size_sectors, dword minimimum_size_sectors)
{
byte *ret_val;
dword ret_size;

    ret_size = 0;
    /* If we return NULL it may be an error but it may also mean that the
       user buffer or scratch buffers are not large enough for the request. */
    /* use the device-wide shared user buffer (all volumes on the device) if one was provided  */
    if (prtfs_cfg->rtfs_exclusive_semaphore)  /* In single threaded mode use the system-wide shared user buffer */
    {
        ret_val                = prtfs_cfg->shared_user_buffer;
        ret_size               = prtfs_cfg->shared_user_buffer_size/pdr->pmedia_info->sector_size_bytes;
    }
	else
	{
    	ret_val                = (byte *) pdr->pmedia_info->device_sector_buffer;
        ret_size               = pdr->pmedia_info->device_sector_buffer_size/pdr->pmedia_info->sector_size_bytes;
	}
    if (!ret_val || ret_size < minimimum_size_sectors)
    {
        ret_val                = 0;
        ret_size               = 0;
    }

    *pbuffer_size_sectors = ret_size;
    return(ret_val);
}
/* Release the user buffer, releases scratch buffer if that's what we are using for user buffers */
void pc_release_user_buffer(DDRIVE *pdr, byte *pbuffer)
{
    RTFS_ARGSUSED_PVOID((void *) pdr);
    RTFS_ARGSUSED_PVOID((void *) pbuffer);
}




/* Release all file descriptors associated with a drive and free up all core
associated with the files called by dsk_close */
void pc_free_all_fil(DDRIVE *pdrive)                                /*__fn__*/
{
    pc_enum_file(pdrive, ENUM_FREE);
}

/* Release all preallocated clusters from all files */
void pc_release_all_prealloc(DDRIVE *pdrive)                                /*__fn__*/
{
    pc_enum_file(pdrive, ENUM_RELEASE_PREALLOC);
}

/* Flush all files on a drive   */
BOOLEAN pc_flush_all_fil(DDRIVE *pdrive)                                /*__fn__*/
{
    if (pc_enum_file(pdrive, ENUM_FLUSH) == 0)
        return(TRUE);
    else
        return(FALSE);
}

/* Test the dirty flag for all files   */
int pc_test_all_fil(DDRIVE *pdrive)                             /*__fn__*/
{
    return(pc_enum_file(pdrive, ENUM_TEST));
}

dword pc_get_media_sector_size(DDRIVE *pdr)
{
dword sector_size_bytes;
   /* If the device may have non default sector size we must scale by it */
    sector_size_bytes = pdr->pmedia_info->sector_size_bytes;
    ERTFS_ASSERT(sector_size_bytes)
    if (!sector_size_bytes) /* defensive */
        sector_size_bytes = RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES;
    return(sector_size_bytes);
}

#if (INCLUDE_FAT16)

BOOLEAN pc_init_drv_fat_info16(DDRIVE *pdr, struct pcblk0 *pbl0)
{
    pdr->drive_info.secpfat = (dword) pbl0->secpfat;   /* sectors / fat */
    pdr->drive_info.numfats = pbl0->numfats; /* Number of fat copies */
    pdr->drive_info.fatblock = (dword) pbl0->secreserved;

    pdr->drive_info.rootblock = pdr->drive_info.fatblock + pdr->drive_info.secpfat * pdr->drive_info.numfats;
    pdr->drive_info.secproot =  (int)((pdr->drive_info.numroot + pdr->drive_info.inopblock-1)/pdr->drive_info.inopblock);
    /* The first block of the cluster area is just past the root   */
    /* Round up if we have to                                      */
    pdr->drive_info.firstclblock = pdr->drive_info.rootblock + pdr->drive_info.secproot;
    /*  Calculate the largest index in the file allocation table.
        Total # block in the cluster area)/Blockpercluster == s total
        Number of clusters. Entries 0 & 1 are reserved so the highest
        valid fat index is 1 + total # clusters.
    */
    pdr->drive_info.maxfindex = (dword)(1 + ((pdr->drive_info.numsecs - pdr->drive_info.firstclblock)/pdr->drive_info.secpalloc));
    /* If the file system type was specified by format in the bpb, use that. Otherwise calculate nibbles/fat entry */
	if (pbl0->fasize)
		pdr->drive_info.fasize = pbl0->fasize;
	else /* if < 4087 clusters then 12 bit else 16   */
    	pdr->drive_info.fasize = (int) ((pdr->drive_info.maxfindex < 4087) ? 3 : 4);
    {
    /* Make sure the calculated index doesn't overflow the fat sectors */
    dword max_index;
    /* For FAT16 Each block of the fat holds 256 entries so the maximum index is
        (pdr->secpfat * 256)-1; */
        max_index = (dword) pdr->drive_info.secpfat;
        if (pdr->drive_info.fasize == 3)
        {/* Max clusters in FAT12.. 1024 per 3 blocks plus 341 per residual block */
         /* Modified 8-16-06 to distinguish between FAT12 and FAT16 */
            dword max_index3,div3,ltemp,residual;
            max_index3 = max_index;
            div3 = max_index3/3;
            max_index = div3 * 1024;
            ltemp = div3 * 3;
            residual = max_index3-ltemp;
            max_index += (residual * 341);
        }
        else
        {
            max_index *= 256;  /* Multiplied by blocks per sector later */
        }
        max_index *= pdr->drive_info.blockspsec;
        max_index -= 1;
        if (pdr->drive_info.maxfindex > max_index)
            pdr->drive_info.maxfindex = max_index;
    }
    /* if calculated size > fff0 set it to one less. fff0 to ffff are
        reserved values. */
     if (pdr->drive_info.fasize == 4)
    {
        if (pdr->drive_info.maxfindex >= 0xfff0 && pdr->drive_info.maxfindex <= 0xffff)
            pdr->drive_info.maxfindex = 0xffef;
    }
    else
    {
        if (pdr->drive_info.maxfindex >= 0xff0 && pdr->drive_info.maxfindex <= 0xfff)
            pdr->drive_info.maxfindex = 0xfef;
    }

    /* Create a hint for where we should write file data. */
    /* Previous versions put the file cluster allocation base 1/32 nd into the cluster space. This is no longer the default baehavior. Now
       we allocate file clusters from the beginning of the cluster space */
    pdr->drive_info.free_contig_base = 2;
     /* set the pointer to where to look for free clusters to the contiguous
        area. On the first call to write this will hunt for the real free
        blocks. */
    pdr->drive_info.free_contig_pointer = pdr->drive_info.free_contig_base;

    pdr->drive_info.infosec = 0;
    return(TRUE);
}
#endif
/* Helper functions used by ProPlus and Pro */
dword pc_alloced_bytes_from_clusters(DDRIVE *pdr, dword total_alloced_clusters)
{
    dword ltemp,alloced_size_bytes;
    /* Check if we get an overflow by shifting */
    ltemp = total_alloced_clusters >> (32-pdr->drive_info.log2_bytespalloc);
    if (ltemp)
        alloced_size_bytes    = LARGEST_DWORD;
    else
        alloced_size_bytes    = total_alloced_clusters << pdr->drive_info.log2_bytespalloc;
    return(alloced_size_bytes);
}

dword pc_byte2clmod(DDRIVE *pdr, dword nbytes)
{
dword nclusters;
    /* Round nbytes up to its cluster size by adding in clustersize-1
        and masking off the low bits */
    nclusters = nbytes >> pdr->drive_info.log2_bytespalloc;
    if (nbytes & pdr->drive_info.byte_into_cl_mask)
        nclusters += 1;
    return(nclusters);
}
/* Routines to test, clear and set file and drive status conditions
   The methods differ between RtfsPro and RtfsProPlus so an abstraction layer is provided */
void pc_set_file_dirty(PC_FILE * pfile, BOOLEAN isdirty)
{
    if (pfile->pobj && pfile->pobj->finode)
    {
        if (isdirty)
		{	/* Update the modified field with the current time and date */
  			pc_update_finode_datestamp(pfile->pobj->finode, TRUE, DATESETUPDATE);
            pfile->pobj->finode->operating_flags |= FIOP_NEEDS_FLUSH;
		}
        else
            pfile->pobj->finode->operating_flags &= ~FIOP_NEEDS_FLUSH;
    }
}
BOOLEAN pc_check_file_dirty(PC_FILE * pfile)
{
    if (pfile->pobj && pfile->pobj->finode  && pfile->pobj->finode->operating_flags & FIOP_NEEDS_FLUSH)
        return(TRUE);
    else
        return(FALSE);
}
void pc_set_file_buffer_dirty(FINODE *pfinode, BOOLEAN isdirty)
{
    if (isdirty)
       pfinode->operating_flags |= FIOP_BUFFER_DIRTY;
    else
       pfinode->operating_flags &= ~FIOP_BUFFER_DIRTY;
}
BOOLEAN pc_check_file_buffer_dirty(FINODE *pfinode)
{
    if (pfinode->operating_flags & FIOP_BUFFER_DIRTY)
        return(TRUE);
    else
        return(FALSE);
}


void set_fat_dirty(DDRIVE *pdr)
{
    pdr->drive_info.drive_operating_flags |= DRVOP_FAT_IS_DIRTY;
}
void clear_fat_dirty(DDRIVE *pdr)
{
    pdr->drive_info.drive_operating_flags &= ~DRVOP_FAT_IS_DIRTY;
}
BOOLEAN chk_fat_dirty(DDRIVE *pdr)
{
    if (pdr->drive_info.drive_operating_flags & DRVOP_FAT_IS_DIRTY)
        return(TRUE);
    else
        return(FALSE);
}
void set_mount_abort_status(DDRIVE *pdr)
{
    pdr->drive_info.drive_operating_flags |= DRVOP_MOUNT_ABORT;
}
void clear_mount_valid(DDRIVE *pdr)
{
    pdr->drive_info.drive_operating_flags &= ~DRVOP_MOUNT_VALID;
}
void clear_mount_abort(DDRIVE *pdr)
{
    pdr->drive_info.drive_operating_flags &= ~DRVOP_MOUNT_ABORT;
}

void set_mount_valid(DDRIVE *pdr)
{
    pdr->drive_info.drive_operating_flags |= DRVOP_MOUNT_VALID;
}
BOOLEAN chk_mount_abort(DDRIVE *pdr)
{
    if (pdr &&  (pdr->drive_info.drive_operating_flags & DRVOP_MOUNT_ABORT))
        return(TRUE);
    return(FALSE);
}
BOOLEAN chk_mount_valid(DDRIVE *pdr)
{
    if (pdr->drive_info.drive_operating_flags & DRVOP_MOUNT_VALID)
        return(TRUE);
    return(FALSE);
}


#if (!INCLUDE_FAT32)

BOOLEAN pc_init_drv_fat_info32(DDRIVE *pdr, struct pcblk0 *pbl0)
{
    RTFS_ARGSUSED_PVOID((void *) pdr);
    RTFS_ARGSUSED_PVOID((void *) pbl0);
    return(FALSE);
}
BOOLEAN pc_gblk0_32(DDRIVE *pdr, struct pcblk0 *pbl0, byte *b)                 /*__fn__*/
{
    RTFS_ARGSUSED_PVOID((void *) pdr);
    RTFS_ARGSUSED_PVOID((void *) pbl0);
    RTFS_ARGSUSED_PVOID((void *) b);
    return(FALSE);
}

BOOLEAN pc_validate_partition_type(byte p_type)
{
    if ( (p_type == 0x01) || (p_type == 0x04) || (p_type == 0x06) || (p_type == 0x0E))
         return(TRUE);
    else
         return(FALSE);
}
#endif /* !(INCLUDE_FAT32) */



#if (!INCLUDE_RTFS_FREEMANAGER)
int free_manager_disabled(int i) /* Quiets down compilers */
{
    return(i);
}
void free_manager_revert(DDRIVE *pdr)
{
    RTFS_ARGSUSED_PVOID((void *) pdr);
}
#endif
#if (RTFS_CFG_READONLY) /* Stubs and overrides of certain functions for read only file system.*/
/* These stubs reduce the number of places where conditional compilation is required without adding significant bloat, */
/* Make release_drive_mount_write() same as release_drive_mount for a read only file system */
BOOLEAN release_drive_mount_write(int driveno)
{
    release_drive_mount(driveno);
    return(TRUE);
}
void pc_set_mbuff_dirty(PCMBUFF *pcmb, dword block_offset, int block_count)
{
    RTFS_ARGSUSED_PVOID((void *) pcmb);
    RTFS_ARGSUSED_DWORD(block_offset);
    RTFS_ARGSUSED_DWORD(block_count);
}
void pc_zero_mbuff_dirty(PCMBUFF *pcmb)
{
    RTFS_ARGSUSED_PVOID((void *) pcmb);
}
int pc_get_clear_mbuff_dirty(PCMBUFF *pcmb, int *pfirst_dirty)
{
    RTFS_ARGSUSED_PVOID((void *) pcmb);
    RTFS_ARGSUSED_PVOID((void *) pfirst_dirty);
    return(0);
}

BOOLEAN pc_flush_file_buffer(FINODE *pfinode)
{
    RTFS_ARGSUSED_PVOID((void *) pfinode);
    return(TRUE);
}
BOOLEAN pc_write_fat_block_buffer_page(DDRIVE *pdrive, FATBUFF *pblk)
{
    RTFS_ARGSUSED_PVOID((void *) pdrive);
    RTFS_ARGSUSED_PVOID((void *) pblk);
    return(TRUE);
}
BOOLEAN _pc_bfilio_flush(PC_FILE *pfile)
{
    RTFS_ARGSUSED_PVOID((void *) pfile);
    return(TRUE);
}
BOOLEAN _pc_efilio_flush(PC_FILE *pfile)
{
    RTFS_ARGSUSED_PVOID((void *) pfile);
    return(TRUE);
}
BOOLEAN _pc_efilio_flush_file_buffer(PC_FILE *pfile)
{
    RTFS_ARGSUSED_PVOID((void *) pfile);
    return(TRUE);
}
void _pc_efilio_free_excess_clusters(PC_FILE *pefile)
{
    RTFS_ARGSUSED_PVOID((void *) pefile);
};

dword _pc_efinode_count_to_link(FINODE *pefinode,dword allocation_policy)
{
    RTFS_ARGSUSED_PVOID((void *) pefinode);
    RTFS_ARGSUSED_DWORD(allocation_policy);
    return(0);
}
void pc_free_excess_clusters(FINODE *pefinode)
{
    RTFS_ARGSUSED_PVOID((void *) pefinode);
};

#endif
