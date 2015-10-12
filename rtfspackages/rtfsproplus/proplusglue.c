/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* RTFSGLUE.C - Routines needed by RTFSProPlus to share a common code base with RTfsPro */
/*

    Routines in this file include:
*/

#include "rtfs.h"


BOOLEAN pc_bytes_to_clusters(int driveno, dword bytes_hi, dword bytes_lo, dword *pcluster_mod)
{
DDRIVE *pdrive;

CHECK_MEM(int, 0)   /* Make sure memory is initted */

    rtfs_clear_errno();  /* pc_cluster_size: clear error status */
    *pcluster_mod = 0;
    pdrive = check_drive_by_number(driveno, TRUE);
    if (!pdrive)
        return(FALSE);

#if (INCLUDE_EXFATORFAT64)
	if (ISEXFATORFAT64(pdrive))
    	*pcluster_mod = pc_byte2clmod64(pdrive, bytes_hi, bytes_lo);
	else
#endif
	{
    	RTFS_ARGSUSED_DWORD(bytes_hi); /* Not used */
    	*pcluster_mod = pc_byte2clmod(pdrive, bytes_lo);
	}

    release_drive_mount(driveno);/* Release lock, unmount if aborted */
    return(TRUE);
}

/* Helper functions used by ProPlus and Pro */
BOOLEAN pc_clusters_to_bytes(int driveno, dword n_clusters, dword *pbytes_hi, dword *pbytes_lo)
{
DDRIVE *pdr;

    CHECK_MEM(int, 0)   /* Make sure memory is initted */
    rtfs_clear_errno();  /* pc_cluster_size: clear error status */
    /* Get the drive and make sure it is mounted   */
    pdr = check_drive_by_number(driveno, TRUE);
    if (!pdr)
        return(FALSE);
    pc_clusters2bytes64(pdr, n_clusters, pbytes_hi, pbytes_lo);
    release_drive_mount(driveno);/* Release lock, unmount if aborted */
    return(TRUE);
}

void pc_subtract_64(dword hi_1, dword lo_1, dword hi_0, dword lo_0, dword *presult_hi, dword *presult_lo)
{
#if (INCLUDE_EXFATORFAT64)
ddword ddw_1, ddw_0, ddw_result;
    ddw_1 = M64SET32(hi_1, lo_1);
    ddw_0 = M64SET32(hi_0, lo_0);
    ddw_result = M64MINUS(ddw_1, ddw_0);
    *presult_hi = M64HIGHDW(ddw_result);
    *presult_lo = M64LOWDW(ddw_result);
#else
    RTFS_ARGSUSED_DWORD(hi_1); /* Not used */
    RTFS_ARGSUSED_DWORD(hi_0); /* Not used */
   *presult_hi = 0;
   *presult_lo = lo_1-lo_0;
#endif
}

void pc_add_64(dword hi_1, dword lo_1, dword hi_0, dword lo_0, dword *presult_hi, dword *presult_lo)
{
#if (INCLUDE_EXFATORFAT64)
ddword ddw_1, ddw_0, ddw_result;
    ddw_1 = M64SET32(hi_1, lo_1);
    ddw_0 = M64SET32(hi_0, lo_0);
    ddw_result = M64PLUS(ddw_1, ddw_0);
    *presult_hi = M64HIGHDW(ddw_result);
    *presult_lo = M64LOWDW(ddw_result);
#else
    RTFS_ARGSUSED_DWORD(hi_1); /* Not used */
    RTFS_ARGSUSED_DWORD(hi_0); /* Not used */
   *presult_hi = 0;
   *presult_lo = lo_1+lo_0;
#endif
}


/* Returns the number of clusters required to hold nbytes_hi:nbytes_lo bytes */
#if (INCLUDE_EXFATORFAT64)
dword pc_byte2clmod64(DDRIVE *pdr, dword nbytes_hi, dword nbytes_lo)
{
    if (!nbytes_hi)
        return(pc_byte2clmod(pdr, nbytes_lo));
    else
    {
        dword nclusters;
        ddword nbytes_ddw,nclusters_ddw;
        /*  64 bit version of:
             nclusters = nbytes >> pdr->drive_info.log2_bytespalloc;
             if (nbytes & pdr->drive_info.byte_into_cl_mask)
                nclusters += 1;
       */
        nbytes_ddw = M64SET32(nbytes_hi, nbytes_lo);
        nclusters_ddw = M64RSHIFT(nbytes_ddw, pdr->drive_info.log2_bytespalloc);
        nclusters =  M64LOWDW(nclusters_ddw);
        if (nbytes_lo & pdr->drive_info.byte_into_cl_mask)
            nclusters += 1;
        return(nclusters);
    }
}
dword pc_byte2cloff64(DDRIVE *pdr, ddword nbytes)
{
dword nclusters;
ddword mask=(ddword)pdr->drive_info.byte_into_cl_mask;
    /* convert nbytes to a cluster offset by masking off the low bits */
    nbytes =  nbytes & ~mask;
    nclusters = (dword) (nbytes >> pdr->drive_info.log2_bytespalloc);
    return(nclusters);
}
ddword pc_byte2cloffbytes64(DDRIVE *pdr, ddword nbytes)
{
ddword mask=(ddword)pdr->drive_info.byte_into_cl_mask;
    /* convert nbytes to a cluster offset by masking off the low bits */
    nbytes =  nbytes & ~mask;
    return(nbytes);
}
#endif

void pc_clusters2bytes64(DDRIVE *pdr, dword n_clusters, dword *pbytes_hi, dword *pbytes_lo)
{
    *pbytes_hi = *pbytes_lo = 0;
#if (INCLUDE_EXFATORFAT64)
{
    ddword nclusters_ddw, nbytes_ddw;
    nclusters_ddw = M64SET32(0, n_clusters);
    nbytes_ddw = M64LSHIFT(nclusters_ddw , pdr->drive_info.log2_bytespalloc);

    *pbytes_hi = M64HIGHDW(nbytes_ddw);
    *pbytes_lo = M64LOWDW(nbytes_ddw);
}
#else
    *pbytes_hi = 0;
    *pbytes_lo = n_clusters << pdr->drive_info.log2_bytespalloc;
#endif
}


static dword _pc_convert_sec_cluster(int driveno, dword cluster,dword sector, BOOLEAN is_raw);
dword pc_cluster_to_sector(int driveno, dword cluster, BOOLEAN raw)
{
dword sector = 0;
    return(_pc_convert_sec_cluster(driveno, cluster, sector, raw));
}
dword pc_sector_to_cluster(int driveno, dword sector, BOOLEAN raw)
{
dword cluster = 0;
    return(_pc_convert_sec_cluster(driveno, cluster, sector, raw));

}
static dword _pc_convert_sec_cluster(int driveno, dword cluster,dword sector, BOOLEAN is_raw)
{
dword ret_val;
DDRIVE *pdr;

    CHECK_MEM(dword, 0) /* Make sure memory is initted */

    rtfs_clear_errno(); /* pc_raw_read: clear error status. */
    pdr = check_drive_by_number(driveno, TRUE);
    if (!pdr)
        return(0);
    if (sector)
    {
        if (is_raw)
            sector -= pdr->drive_info.partition_base;
        ret_val = pc_sec2cluster(pdr, sector);
    }
    else
    {
        ret_val = pc_cl2sector(pdr,cluster);
        if (ret_val && is_raw)
        {
            ret_val += pdr->drive_info.partition_base;
        }
    }
    if (!ret_val)   /* low level calls do not set errno */
        rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
    release_drive_mount(driveno);
    return(ret_val);
}

/* Return TRUE if automount is enabled. */
BOOLEAN  pc_check_automount(DDRIVE *pdr)
{
    /* Check if automount is disabled, if so the application perfroms the mount */
    /* The DRVPOL_DISABLE_AUTOMOUNT policy is used for testing. If it is set the automount feature is disabled and the application
       layer performs the mount */
    if ( pdr->mount_parms.drive_operating_policy & DRVPOL_DISABLE_AUTOMOUNT ||
    	rtfs_app_callback(RTFS_CBA_ASYNC_MOUNT_CHECK, pdr->driveno, 0, 0, 0))
    {
       if (pc_i_dskopen(pdr->driveno,TRUE))
       {
           pdr->drive_state.drive_async_state = DRV_ASYNC_MOUNT;
           rtfs_set_errno(PENOTMOUNTED, __FILE__, __LINE__);
           rtfs_app_callback(RTFS_CBA_ASYNC_START, pdr->driveno, 0, 0, 0);
	   }
	   return(FALSE);
    }
    return(TRUE);
}


void pc_memory_init_proplus(void)
{
    /* Make EXTENDED FINODE freelist */
    FINODE_EXTENSION_MEMORY *pfi_uex;
    int i;

    pfi_uex = prtfs_cfg->mem_finode_uex_pool;
    for (i = 0; i < prtfs_cfg->cfg_NFINODES_UEX; i++)
    {
        pc_memory_finode_ex((FINODE_EXTENDED *)pfi_uex);
        pfi_uex++;
    }
}

/**************************************************************************
    pc_memory_finode_ex -  allocate or free an extended finode structure
*****************************************************************************/

FINODE_EXTENDED *pc_memory_finode_ex(FINODE_EXTENDED *pfeinode)
{
FINODE_EXTENSION_MEMORY *pinode;
FINODE_EXTENSION_MEMORY *ret_uval;

    pinode = (FINODE_EXTENSION_MEMORY *) pfeinode;
    OS_CLAIM_FSCRITICAL()
    if (pinode)
    {
        rtfs_memset((byte *)pinode, 0, sizeof(*pinode));
        pinode->pnext_freelist = prtfs_cfg->mem_finode_uex_freelist;
        prtfs_cfg->mem_finode_uex_freelist = pinode;
        ret_uval = 0;

    }
    else
    {
        ret_uval = prtfs_cfg->mem_finode_uex_freelist;
        if (ret_uval)
        {
            prtfs_cfg->mem_finode_uex_freelist = ret_uval->pnext_freelist;
            rtfs_memset((byte *)ret_uval, 0, sizeof(*ret_uval));
        }
    }
    OS_RELEASE_FSCRITICAL()
    if (!pinode && !ret_uval)
        rtfs_set_errno(PERESOURCEFINODEEX, __FILE__, __LINE__); /* pc_memory_finode: out finode of resources */
    if (ret_uval)
        return(&(ret_uval->x));
    else
        return(0);
}

/* Helper functions used by ProPlus but not by Pro */
dword pc_byte2clmodbytes(DDRIVE *pdr, dword nbytes)
{
    /* Round nbytes up to its cluster size by adding in clustersize-1
        and masking off the low bits */
    nbytes =  (nbytes + pdr->drive_info.byte_into_cl_mask) &
                    ~(pdr->drive_info.byte_into_cl_mask);
    return(nbytes);
}

dword pc_byte2cloff(DDRIVE *pdr, dword nbytes)
{
dword nclusters;
    /* convert nbytes to a cluster offset by masking off the low bits */
    nbytes =  nbytes & ~(pdr->drive_info.byte_into_cl_mask);
    nclusters = nbytes >> pdr->drive_info.log2_bytespalloc;
    return(nclusters);
}

dword pc_byte2cloffbytes(DDRIVE *pdr, dword nbytes)
{
    /* convert nbytes to a cluster offset by masking off the low bits */
    nbytes =  nbytes & ~(pdr->drive_info.byte_into_cl_mask);
    return(nbytes);
}

/* Free extended finode portion of a finode that is being discarded.
   No processing is done, structure is just returned to the free list */
void pc_discardi_extended(FINODE *pfi)
{
    /* Free resources used in extended mode */
    if (pfi->e.x)
    {
            pc_memory_finode_ex(pfi->e.x);
    }
}

/* Free extended finodes, return TRUE if the finode is an extended finode */
void pc_freei_extended(FINODE *pfi)
{
    /* Let the segment finodes reduce their count */
        pc_free_efinode(pfi);
}
#if (INCLUDE_FAT64 && !INCLUDE_EXFAT)
/* Stubs of functions needed for FAT64 file system  */
BOOLEAN exfatop_remove_free_region(DDRIVE *pdr, dword cluster, dword ncontig){return 0;}
BOOLEAN exfatop_add_free_region(DDRIVE *pdr, dword cluster, dword ncontig, BOOLEAN do_erase){return 0;}
BOOLEAN pcexfat_insert_inode(DROBJ *pobj , DROBJ *pmom, byte _attr, FINODE *infinode, dword initcluster, byte *filename, byte secondaryflags, dword sizehi, dword sizelow, int use_charset){return 0;}
BOOLEAN pcexfat_grow_directory(DROBJ *pobj){return 0;}
void pcexfat_update_finode_datestamp(FINODE *pfi, BOOLEAN set_archive, int set_date_mask){}
BOOLEAN pcexfat_rmnode(DROBJ *pobj){return 0;}
BOOLEAN pcexfat_mvnode(DROBJ *old_parent_obj,DROBJ *old_obj,DROBJ *new_parent_obj, byte *filename,int use_charset){return 0;}
BOOLEAN pcexfat_set_volume(DDRIVE *pdrive, byte *volume_label,int use_charset){return 0;}
BOOLEAN pcexfat_get_volume(DDRIVE *pdrive, byte *volume_label,int use_charset){return 0;}
BOOLEAN pcexfat_get_cwd(DDRIVE *pdrive, byte *path, int use_charset){return 0;}
BOOLEAN pcexfat_set_cwd(DDRIVE *pdrive, byte *name, int use_charset){return 0;}
BOOLEAN pcexfat_update_by_finode(FINODE *pfi, int entry_index, BOOLEAN set_archive, int set_date_mask, BOOLEAN do_delete){return 0;}
BOOLEAN pcexfat_flush(DDRIVE *pdrive){return 0;}
dword   exfatop_find_contiguous_free_clusters(DDRIVE *pdr, dword startpt, dword endpt, dword min_clusters, dword max_clusters, dword *p_contig, int *is_error){return 0;}
BOOLEAN rtexfat_i_dskopen(DDRIVE *pdr){return 0;}
void    pc_release_exfat_buffers(DDRIVE *pdr){}
BOOLEAN pcexfat_findin( DROBJ *pobj, byte *filename, int action, BOOLEAN oneshot, int use_charset){return 0;}
byte *  pcexfat_seglist2text(DDRIVE * pdrive, SEGDESC *s, byte *lfn, int use_charset){return 0;}
dword   exFatfatop_getdir_frag(DROBJ *pobj, dword startpt, dword *pnext_cluster, dword n_clusters, int *end_of_chain){return 0;}
dword   exFatfatop_getfile_frag(FINODE *pfi, dword startpt, dword *pnext_cluster, dword n_clusters, int *end_of_chain){return 0;}
BOOLEAN pcexfat_gread(DSTAT *statobj, int blocks_to_read, byte *buffer, int *blocks_read){return 0;}
BOOLEAN pcexfat_parse_path(DDRIVE *pdrive, byte *outpath, byte *inpath, int use_charset){return 0;}
BOOLEAN rtexfat_gblk0(DDRIVE *pdr, struct pcblk0 *pbl0b, byte *b){return 0;}
DROBJ * pcexfat_get_root( DDRIVE *pdrive){return 0;}
BOOLEAN pc_exfatrfindin( DROBJ *pobj, byte *filename, int action, int use_charset, BOOLEAN starting){return 0;}
BOOLEAN pcexfat_format_volume(byte *path){return 0;}
void probe_exfat_format_parms(byte *path) {}
#endif
