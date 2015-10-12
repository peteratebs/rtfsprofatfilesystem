/*
* rtleakcheck.c - Check for leaks
*
* ERTFS portable process management and other functions.
* This file is portable but it requires interaction with the
* porting layer functions in portrtfs.c.
*
*   Copyright EBS Inc. 1987-2003
*   All rights reserved.
*   This code may not be redistributed in source or linkable object form
*   without the consent of its author.
*
*/

#include "rtfs.h"

#if (INCLUDE_DEBUG_LEAK_CHECKING)

static void pc_lk_print_check(struct mem_report *preport);

#define ARGTYPE_DROBJ       1
#define ARGTYPE_FINODE      2
#define ARGTYPE_FILEBUFFER  3
#define ARGTYPE_REGION      4
#define ARGTYPE_FINODE_UEX   5

static BOOLEAN pc_lk_drive_in_freelist(DDRIVE *pdr);
static BOOLEAN pc_lk_drive_in_map(DDRIVE *pdr);
static BOOLEAN pc_lk_drobj_in_freelist(DROBJ *pobj);
static BOOLEAN pc_lk_drobj_in_file(DROBJ *pobj);
static BOOLEAN pc_lk_drobj_in_user(DROBJ *pobj);
static BOOLEAN pc_lk_finode_in_list(FINODE *plist, FINODE *p);
static BOOLEAN pc_lk_finode_in_freelist(FINODE *p);
static BOOLEAN pc_lk_finode_in_pool(FINODE *p);
static BOOLEAN pc_lk_finode_in_finode(FINODE *finode, void *p, int argtype);
static int pc_lk_finode_in_drobj(void *p, int argtype);
#if (INCLUDE_RTFS_PROPLUS)  /* config structure: ProPlus specific element*/
static BOOLEAN pc_lk_finode_uex_in_freelist(FINODE_EXTENSION_MEMORY *p);
#endif
static BOOLEAN pc_lk_blkbuff_inlist(BLKBUFF *plist, BLKBUFF *pbuff);
static BOOLEAN pc_lk_blkbuff_in_freelist(BLKBUFFCNTXT *pbuffcntxt, BLKBUFF *pbuff);
static BOOLEAN pc_lk_blkbuff_in_filebuffer(BLKBUFF *pbuff);
static BOOLEAN pc_lk_blkbuff_in_dirbuffer(BLKBUFFCNTXT *pbuffcntxt, BLKBUFF *pbuff);
static BOOLEAN pc_lk_blkbuff_in_scratchbuffer(BLKBUFFCNTXT *pbuffcntxt, BLKBUFF *pbuff);
static BOOLEAN pc_lk_rgnbuff_in_list(struct region_fragment *plist, struct region_fragment *prgnbuff);
static BOOLEAN pc_lk_rgnbuff_in_freelist(struct region_fragment *prgnbuff);
static BOOLEAN pc_lk_rgnbuff_in_file(struct region_fragment *prgnbuff);
static BOOLEAN pc_lk_rgnbuff_in_freemap(struct region_fragment *prgnbuff);
static BOOLEAN pc_lk_rgnbuff_in_failsafe(struct region_fragment *prgnbuff);
static void pc_lk_fatbuff_check(DDRIVE *pdrive,struct mem_report *preport);
void pc_leak_test(struct mem_report * preport);


/* Check drive structures should either be on free list or in drive map */
void pc_leak_test(struct mem_report * preport)
{
    rtfs_memset((byte *)preport, 0, sizeof(struct mem_report));

    /* Check drive structures */
    {
        int i;
        DDRIVE *pdr;
        pdr = prtfs_cfg->mem_drive_pool;
        preport->ndrives_configured = prtfs_cfg->cfg_NDRIVES;
        for (i = 0; i < prtfs_cfg->cfg_NDRIVES; i++, pdr++)
        {
            if (pc_lk_drive_in_freelist(pdr))
                preport->ndrives_free += 1;
            else if (pc_lk_drive_in_map(pdr))
                preport->ndrives_mapped += 1;
            else
                preport->ndrives_lost += 1;
        }
    }
    /* Check drobj structures, should be in file pool, free list or in current working directories */
    {
        int i;
        DROBJ *pobj;
        pobj = prtfs_cfg->mem_drobj_pool;
        preport->ndrobjs_configured = prtfs_cfg->cfg_NDROBJS;
        for (i = 0; i < prtfs_cfg->cfg_NDROBJS; i++, pobj++)
        {
            if (pc_lk_drobj_in_freelist(pobj))
                preport->ndrobjs_free +=1;
            else if (pc_lk_drobj_in_file(pobj))
                preport->ndrobjs_in_files +=1;
            else if (pc_lk_drobj_in_user(pobj))
                preport->ndrobjs_in_users +=1;
            else
                preport->ndrobjs_lost += 1;
        }
    }
    /* Check finodes structures, DROBJ structures, on free list or in the inode pool. If in the INODE pool and not
       In a DROBJ it should have a zero open count */
    {
        int i,finode_reference_count;
        FINODE *pfinode;
        pfinode = prtfs_cfg->mem_finode_pool;
        preport->nfinodes_configured = prtfs_cfg->cfg_NFINODES;
        for (i = 0; i < prtfs_cfg->cfg_NFINODES; i++, pfinode++)
        {
            if (pc_lk_finode_in_freelist(pfinode))
                preport->nfinodes_free     += 1;
            else if (pc_lk_finode_in_pool(pfinode))
            {
                preport->nfinodes_in_pool  += 1;
                finode_reference_count = pc_lk_finode_in_drobj((void *)pfinode,ARGTYPE_FINODE);
                if (finode_reference_count)
                    preport->nfinodes_in_drobj += 1;
                if (pfinode->opencount != finode_reference_count)
                    preport->nfinodes_reference_errors += 1;
            }
            else
                preport->nfinodes_lost     += 1;

        }
    }
#if (INCLUDE_RTFS_PROPLUS)  /* config structure: ProPlus specific element*/
    /* Check extended finodes structures */
    {
        int i;
        FINODE_EXTENSION_MEMORY *pfiuex;
        pfiuex = prtfs_cfg->mem_finode_uex_pool;
        preport->nfinodeex64_configured = 0;
        preport->nfinodeex_configured = prtfs_cfg->cfg_NFINODES_UEX;
        for (i = 0; i < prtfs_cfg->cfg_NFINODES_UEX; i++, pfiuex++)
        {
            if (pc_lk_finode_uex_in_freelist(pfiuex))
                preport->nfinodeex_free += 1;
            else if (pc_lk_finode_in_drobj((void *)pfiuex,ARGTYPE_FINODE_UEX) == 0)
                preport->nfinodeex_lost += 1;
        }
    }
#endif /* #if (INCLUDE_RTFS_PROPLUS)  */

    /* Check shared block buffer pool */
    {
        int i;
        BLKBUFF  *pbuff;
        pbuff = prtfs_cfg->mem_block_pool;
        preport->nglblkbuff_configured = prtfs_cfg->cfg_NBLKBUFFS;
        for (i = 0; i < prtfs_cfg->cfg_NBLKBUFFS; i++, pbuff++)
        {
            if (pc_lk_blkbuff_in_freelist(&prtfs_cfg->buffcntxt, pbuff))
                preport->nglblkbuff_free += 1;
            else if (pc_lk_blkbuff_in_filebuffer(pbuff)) /* Must be called before scratch */
                preport->nglblkbuff_in_filebuff += 1;
            else if (pc_lk_blkbuff_in_dirbuffer(&prtfs_cfg->buffcntxt, pbuff))
                preport->nglblkbuff_in_dirbuff += 1;
            else if (pc_lk_blkbuff_in_scratchbuffer(&prtfs_cfg->buffcntxt, pbuff))
                preport->nglblkbuff_in_scratchbuff += 1;
            else
                preport->nglblkbuff_lost += 1;
        }
    }


    /* Check region structures */
    {
        int i;
        struct region_fragment *prgnbuff;
        prgnbuff = prtfs_cfg->mem_region_pool;
        preport->nrgnbuff_configured = prtfs_cfg->cfg_NREGIONS;

        for (i = 0; i < prtfs_cfg->cfg_NREGIONS; i++, prgnbuff++)
        {
            if (pc_lk_rgnbuff_in_freelist(prgnbuff))
                preport->nrgnbuff_free += 1;
            else if (pc_lk_rgnbuff_in_file(prgnbuff))
                preport->nrgnbuff_in_files += 1;
            else if (pc_lk_rgnbuff_in_freemap(prgnbuff))
                preport->nrgnbuff_in_freemap += 1;
            else if (pc_lk_rgnbuff_in_failsafe(prgnbuff))
                preport->nrgnbuff_in_failsafe += 1;
            else
                preport->nrgnbuff_lost += 1;
        }
    }

    /* Check user structures */
    {
        int i;
        preport->nusers_configured = prtfs_cfg->cfg_NUM_USERS;
        for (i = 0; i < prtfs_cfg->cfg_NUM_USERS; i++)
        {
            if (prtfs_cfg->rtfs_user_table[i].task_handle==0)
                preport->nusers_free += 1;
        }
    }

    /* Check files structures */
    {
        int i;
        preport->nfiles_configured = prtfs_cfg->cfg_NUSERFILES;
        for (i = 0; i < prtfs_cfg->cfg_NUSERFILES; i++)
        {
            if (prtfs_cfg->mem_file_pool[i].is_free)
                preport->nfiles_free += 1;
        }
    }
    /* Check fat buffer structures */
    {
        int i;
        DDRIVE *pdr;
        pdr = prtfs_cfg->mem_drive_pool;
        for (i = 0; i < prtfs_cfg->cfg_NDRIVES; i++, pdr++)
        {
            if (pc_lk_drive_in_map(pdr))
                pc_lk_fatbuff_check(pdr, preport);
        }
    }

    /* Add up all lost fields, will be zero if none lost */
    preport->lost_count =
    preport->ndrives_lost +
    preport->ndrobjs_lost +
    preport->nfinodes_lost +
    preport->nfinodeex_lost +
    preport->nfinodeex64_lost +
    preport->nglblkbuff_lost +
    preport->nfatbuff_lost +
    preport->nrgnbuff_lost;

    pc_lk_print_check(preport);

}




static BOOLEAN pc_lk_drive_in_freelist(DDRIVE *pdr)
{
DDRIVE *lpdr;
int i = 0;
    lpdr = prtfs_cfg->mem_drive_freelist;
    while (lpdr && i++ <= prtfs_cfg->cfg_NDRIVES)
    {
        if (lpdr == pdr)
            return(TRUE);
        lpdr = lpdr->pnext_free;
    }
    return(FALSE);
}
static BOOLEAN pc_lk_drive_in_map(DDRIVE *pdr)
{
    int i;
    for (i = 0; i < 26; i++)
        if (pdr == prtfs_cfg->drno_to_dr_map[i])
            return(TRUE);
    return(FALSE);
}


static BOOLEAN pc_lk_drobj_in_freelist(DROBJ *pobj)
{
    DROBJ *scanobj;
    int i = 0;
    scanobj = prtfs_cfg->mem_drobj_freelist;
    while (scanobj)
    {
        if (scanobj == pobj)
            return(TRUE);
        scanobj = (DROBJ *) scanobj->pdrive; /* Acts as pnext on the free list */
        if (i++ > prtfs_cfg->cfg_NDROBJS)
            return(FALSE);
    }
    return(FALSE);
}
static BOOLEAN pc_lk_drobj_in_file(DROBJ *pobj)
{
    int i;
    for (i = 0; i < prtfs_cfg->cfg_NUSERFILES; i++)
    {
        if (!prtfs_cfg->mem_file_pool[i].is_free)
        {
            if (prtfs_cfg->mem_file_pool[i].pobj == pobj)
                return(TRUE);
        }
    }
    return(FALSE);
}
static BOOLEAN pc_lk_drobj_in_user(DROBJ *pobj)
{
    int i,j;
        for (i = 0; i < prtfs_cfg->cfg_NUM_USERS; i++)
        {
            if (prtfs_cfg->rtfs_user_table[i].task_handle)
            {
                for (j = 0; j < 26; j++)
                {
                    if (pobj == rtfs_get_user_pwd(&prtfs_cfg->rtfs_user_table[i], j, FALSE))
                        return(TRUE);
                }
            }
        }
        return(FALSE);
}




static BOOLEAN pc_lk_finode_in_list(FINODE *plist, FINODE *p)
{
    int i = 0;
    FINODE *scannode;
    scannode = plist;
    while (scannode)
    {
        if (scannode == p)
            return(TRUE);
        scannode = scannode->pnext;
        if (i++ > prtfs_cfg->cfg_NFINODES)
            return(FALSE);
    }
    return(FALSE);
}
static BOOLEAN pc_lk_finode_in_freelist(FINODE *p)
{
    return(pc_lk_finode_in_list(prtfs_cfg->mem_finode_freelist, p));
}

static BOOLEAN pc_lk_finode_in_pool(FINODE *p)
{
    return(pc_lk_finode_in_list(prtfs_cfg->inoroot, p));
}

static BOOLEAN pc_lk_rgnbuff_in_list(struct region_fragment *plist, struct region_fragment *prgnbuff);


static BOOLEAN pc_lk_finode_in_finode(FINODE *finode, void *p, int argtype)
{
    if (argtype == ARGTYPE_FINODE && finode == (FINODE *) p)
        return(TRUE);
#if (INCLUDE_RTFS_PROPLUS)
    if (argtype == ARGTYPE_FINODE_UEX && finode->e.x == (FINODE_EXTENDED *) p)
        return(TRUE);
    if (argtype == ARGTYPE_REGION && finode->e.x)
    {
        if (pc_lk_rgnbuff_in_list(finode->e.x->pfirst_fragment, (struct region_fragment *)p))
            return(TRUE);
        if (pc_lk_rgnbuff_in_list(finode->e.x->ptofree_fragment, (struct region_fragment *)p))
            return(TRUE);
    }
#endif
    return(FALSE);
}
static int pc_lk_finode_in_drobj(void *p, int argtype)
{
int i, hit_count;
    DROBJ *pobj;
    pobj = prtfs_cfg->mem_drobj_pool;

	hit_count = 0;
    for (i = 0; i < prtfs_cfg->cfg_NDROBJS; i++, pobj++)
    {
        if (!pc_lk_drobj_in_freelist(pobj))
        {
            if (argtype == ARGTYPE_FINODE && pobj->finode == (FINODE *)p) /* See if it is the finode linked to a drobj for a directory entry or file */
                hit_count += 1;
            else if (pc_lk_finode_in_finode(pobj->finode, p, argtype))    /* See if it is a child of the finode */
                hit_count += 1;
        }
    }
    return(hit_count);
}

#if (INCLUDE_RTFS_PROPLUS)  /* config structure: ProPlus specific element*/
static BOOLEAN pc_lk_finode_uex_in_freelist(FINODE_EXTENSION_MEMORY *p)
{
    int i = 0;
    FINODE_EXTENSION_MEMORY *scannode;
    scannode = prtfs_cfg->mem_finode_uex_freelist;

    while (scannode)
    {
        if (scannode == p)
            return(TRUE);
        scannode = scannode->pnext_freelist;
        if (i++ > prtfs_cfg->cfg_NFINODES_UEX)
            return(FALSE);
    }
    return(FALSE);
}
#endif /* INCLUDE_RTFS_PROPLUS */

static BOOLEAN pc_lk_blkbuff_inlist(BLKBUFF *plist, BLKBUFF *pbuff)
{
    while (plist)
    {
        if (plist == pbuff)
            return(TRUE);
        plist = plist->pnext;
    }
    return(FALSE);
}


static BOOLEAN pc_lk_blkbuff_in_freelist(BLKBUFFCNTXT *pbuffcntxt, BLKBUFF *pbuff)
{
    return(pc_lk_blkbuff_inlist(pbuffcntxt->pfree_blocks , pbuff));
}


static BOOLEAN pc_lk_blkbuff_in_filebuffer(BLKBUFF *pbuff)
{
    int i;
    for (i = 0; i < prtfs_cfg->cfg_NUSERFILES; i++)
    {
        if (!prtfs_cfg->mem_file_pool[i].is_free)
        {
            if (prtfs_cfg->mem_file_pool[i].pobj &&
                prtfs_cfg->mem_file_pool[i].pobj->finode->pfile_buffer == pbuff)
                return(TRUE);
        }
    }
    return(FALSE);
}
static BOOLEAN pc_lk_blkbuff_in_dirbuffer(BLKBUFFCNTXT *pbuffcntxt, BLKBUFF *pbuff)
{
    return(pc_lk_blkbuff_inlist(pbuffcntxt->ppopulated_blocks , pbuff));
}
static BOOLEAN pc_lk_blkbuff_in_scratchbuffer(BLKBUFFCNTXT *pbuffcntxt, BLKBUFF *pbuff)
{
    return(pc_lk_blkbuff_inlist(pbuffcntxt->pscratch_blocks , pbuff));
}
static BOOLEAN pc_lk_rgnbuff_in_list(struct region_fragment *plist, struct region_fragment *prgnbuff)
{
    struct region_fragment *p;
    int i = 0;
    p = plist;
    while (p)
    {
        if (p == prgnbuff)
            return(TRUE);
        p = p->pnext;
        if (i++ > prtfs_cfg->cfg_NREGIONS)
            return(FALSE);
    }
    return(FALSE);
}

static BOOLEAN pc_lk_rgnbuff_in_freelist(struct region_fragment *prgnbuff)
{
    return(pc_lk_rgnbuff_in_list(prtfs_cfg->mem_region_freelist, prgnbuff));
}

static BOOLEAN pc_lk_rgnbuff_in_file(struct region_fragment *prgnbuff)
{
    if (pc_lk_finode_in_drobj((void *)prgnbuff, ARGTYPE_REGION) == 0)
        return(FALSE);
    else
        return(TRUE);

}
static BOOLEAN pc_lk_rgnbuff_in_freemap(struct region_fragment *prgnbuff)
{
#if (INCLUDE_RTFS_FREEMANAGER)
DDRIVE *pdr;
int i,j;
    pdr = prtfs_cfg->mem_drive_pool;
    for (i = 0; i < prtfs_cfg->cfg_NDRIVES; i++, pdr++)
    {
        if (pc_lk_drive_in_map(pdr))
        {
            if (pdr->drive_state.free_ctxt_cluster_shifter)
            {
                for (j = 0; j < RTFS_FREE_MANAGER_HASHSIZE; j++)
                {
                    if (pc_lk_rgnbuff_in_list(pdr->drive_state.free_ctxt_hash_tbl[j], prgnbuff))
                        return(TRUE);
                }
            }
        }
    }
#endif
    RTFS_ARGSUSED_PVOID((void *) prgnbuff);
    return(FALSE);
}
static BOOLEAN pc_lk_rgnbuff_in_failsafe(struct region_fragment *prgnbuff)
{
#if (INCLUDE_FAILSAFE_CODE)
int i;
DDRIVE *pdr;
FAILSAFECONTEXT *pfscntxt;

    pdr = prtfs_cfg->mem_drive_pool;
    for (i = 0; i < prtfs_cfg->cfg_NDRIVES; i++, pdr++)
    {
        if (pc_lk_drive_in_map(pdr))
        {
            pfscntxt = (FAILSAFECONTEXT *) pdr->drive_state.failsafe_context;
            if (pfscntxt)
            {
                if (pc_lk_rgnbuff_in_list(pfscntxt->fs_journal.open_free_fragments, prgnbuff))
                    return(TRUE);
                if (pc_lk_rgnbuff_in_list(pfscntxt->fs_journal.flushed_free_fragments, prgnbuff))
                    return(TRUE);
                if (pc_lk_rgnbuff_in_list(pfscntxt->fs_journal.restoring_free_fragments, prgnbuff))
                    return(TRUE);
           }
        }
    }
#endif
    RTFS_ARGSUSED_PVOID((void *) prgnbuff);
    return(FALSE);
}

static void pc_lk_fatbuff_check(DDRIVE *pdrive,struct mem_report *preport)
{
    RTFS_ARGSUSED_PVOID((void *) pdrive);
    RTFS_ARGSUSED_PVOID((void *) preport);
    return;
}


static void pc_lk_print_two(char *p1, int v1, char *p2, int v2)
{
/*     printf("%-30.30s %6d %-30.30s %6d \n", p1, v1, p2, v2); */
    rtfs_print_one_string((byte *)p1, 0);
    rtfs_print_one_string((byte *)" == ", 0);
    rtfs_print_long_1(v1, 0);
    rtfs_print_one_string((byte *)".  ", 0);
    rtfs_print_one_string((byte *)p2, 0);
    rtfs_print_one_string((byte *)" == ", 0);
    rtfs_print_long_1(v2, PRFLG_NL);

}

static void pc_lk_print_one(char *p1, int v1)
{
/*    printf("%-30.30s %6d\n", p1, v1); */
    rtfs_print_one_string((byte *)p1, 0);
    rtfs_print_one_string((byte *)" == ", 0);
    rtfs_print_long_1(v1, PRFLG_NL);
}

static void pc_lk_print_check(struct mem_report *preport)
{
    pc_lk_print_two("nusers_free      ", preport->nusers_free, "nusers_configured", preport->nusers_configured);
    pc_lk_print_two("nfiles_configured", preport->nfiles_configured, "nfiles_free      ", preport->nfiles_free);
    pc_lk_print_two("ndrives_configure", preport->ndrives_configured, "ndrives_free     ", preport->ndrives_free);
    pc_lk_print_two("ndrives_mapped   ", preport->ndrives_mapped, "ndrives_lost     ", preport->ndrives_lost);
    pc_lk_print_two("ndrobjs_configure", preport->ndrobjs_configured, "ndrobjs_free     ", preport->ndrobjs_free);
    pc_lk_print_two("ndrobjs_in_files ", preport->ndrobjs_in_files, "ndrobjs_in_users ", preport->ndrobjs_in_users);
    pc_lk_print_one("ndrobjs_lost     ", preport->ndrobjs_lost);
    pc_lk_print_two("nfinodes_configured", preport->nfinodes_configured, "nfinodes_free    ", preport->nfinodes_free);
    pc_lk_print_two("nfinodes_in_drobj", preport->nfinodes_in_drobj, "nfinodes_in_pool ", preport->nfinodes_in_pool);
    pc_lk_print_two("nfinodes_reference_errors ", preport->nfinodes_reference_errors, "nfinodes_lost    ", preport->nfinodes_lost);
    pc_lk_print_two("nfinodeex_configuredn", preport->nfinodeex_configured, "nfinodeex_free   ", preport->nfinodeex_free);
    pc_lk_print_one("nfinodeex_lost      ", preport->nfinodeex_lost);
    pc_lk_print_one("nfinodeex64_configured ", preport->nfinodeex64_configured);
    pc_lk_print_two("nfinodeex64_free ", preport->nfinodeex64_free, "nfinodeex64_lost    ", preport->nfinodeex64_lost);
    pc_lk_print_two("nblkbuff_configured ", preport->nglblkbuff_configured, "nblkbuff_free       ", preport->nglblkbuff_free);
    pc_lk_print_two("nblkbuff_in_files   ", preport->nglblkbuff_in_filebuff, "nblkbuff_in_dirbuff ", preport->nglblkbuff_in_dirbuff);
    pc_lk_print_two("nblkbuff_in_scratch  ", preport->nglblkbuff_in_scratchbuff, "nblkbuff_lost       ", preport->nglblkbuff_lost);
    pc_lk_print_two("nrgnbuff_configured ", preport->nrgnbuff_configured, "nrgnbuff_free       ", preport->nrgnbuff_free);
    pc_lk_print_two("nrgnbuff_in_files   ", preport->nrgnbuff_in_files, "nrgnbuff_in_freemap ", preport->nrgnbuff_in_freemap);
    pc_lk_print_two("nrgnbuff_in_failsafe", preport->nrgnbuff_in_failsafe, "nrgnbuff_lost       ", preport->nrgnbuff_lost);
    pc_lk_print_two("nfatbuff_free       ", preport->nfatbuff_free, "fatbuff_committed   ", preport->nfatbuff_committed);
    pc_lk_print_two("fatbuff_uncommitted ", preport->nfatbuff_uncommitted, "fatbuff_lost        ", preport->nfatbuff_lost);
    pc_lk_print_one("lost_count", preport->lost_count);
}

#endif
