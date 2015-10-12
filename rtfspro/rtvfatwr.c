/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* RTVFAT.C - Contains routines specific to the vfat implementation */

#include "rtfs.h"

#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */

#if (INCLUDE_VFAT)

#define FIRST_NAMESEG 0x40
#define NAMESEG_ORDER 0x3F


/* LFNINODE - This is an image of lfn extended names in a subdirectory */
/* Note: lfn file names are null terminated unicode 00, the lfninode is
   padded mod 13 with ffff */
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

void pc_addtoseglist(SEGDESC *s, dword my_block, int my_index);
void pc_zeroseglist(SEGDESC *s);
byte *pc_data_in_ubuff(DDRIVE *pdr, dword blockno, byte *puser_buffer, dword user_buffer_first_block,dword user_buffer_n_blocks);

static void pcdel2lfi(LFNINODE *lfi, int nsegs);
BOOLEAN pc_deleteseglist(DDRIVE *pdrive, SEGDESC *s);
static byte *text2lfi(byte *lfn, LFNINODE *lfi, int nsegs, byte ncksum, byte order, int use_charset);
static void pc_reduceseglist(SEGDESC *s, int inodes_perblock);
static BOOLEAN pc_malias(byte *fname, byte *fext, byte *input_file, int *segs_required, DROBJ *pobj, int nsegs, int use_charset);
static int pc_multi_get_entry_number(byte *alias_image, byte *alias_base, DOSINODE *pi);
static void pc_complete_alias_name(byte *palias_base,byte *fext, byte *plast_tilde, int alias_number);
static BOOLEAN pc_seglist2disk(DROBJ *pobj, SEGDESC *s, byte *lfn,int use_charset);
static int _pc_multi_alias_scan(DROBJ *pobj, int n_segs, byte *fext, byte *alias_image, byte *alias_base);


/***************************************************************************
    PC_INSERT_INODE - Insert a new inode into an existing directory inode.

Description
    Take mom , a fully defined DROBJ, and pobj, a DROBJ with a finode
    containing name, ext, etc, but not yet stitched into the inode buffer
    pool, and fill in pobj and its inode, write it to disk and make the inode
    visible in the inode buffer pool. (see also pc_mknode() )


Returns
    Returns TRUE if all went well, FALSE on a write error, disk full error or
    root directory full.

**************************************************************************/
/* Note: the parent directory is locked before this routine is called   */
BOOLEAN pc_insert_inode(DROBJ *pobj , DROBJ *pmom, byte attr, dword initcluster, byte *filename, byte *fileext, int use_charset)
{
    DIRBLK *pd;
    dword cluster,prev_end_cluster;
    DDRIVE *pdrive;
    DATESTR crdate;
    byte vffilename[9],vffileext[4];
    byte cksum;
    int n_segs;
    SEGDESC saved_segdesc;
    int segs_required;

    cluster = 0;
    prev_end_cluster = 0;
    RTFS_ARGSUSED_PVOID((void *) fileext);

    /* Set up pobj      */
    pdrive = pobj->pdrive = pmom->pdrive;
    pobj->isroot = FALSE;
    pd = &pobj->blkinfo;
    pd->my_block = pd->my_frstblock = pc_firstblock(pmom);
    if (!pd->my_block)
    {
        rtfs_set_errno(PEINTERNAL, __FILE__, __LINE__);
        return (FALSE);
    }
    else
        pd->my_index = 0;


    /* How many segments do we need   */
    n_segs = (rtfs_cs_strlen(filename, use_charset) + 12 )/13;
    if (!n_segs)
        return(FALSE);  /* Won't happen */
    n_segs += 1; /* Add one for the alias */

    segs_required = n_segs; /* Set segs_required, pc_malias() will reset it to one if filename is a valid sfn */
    /* Get the alias filename and fileextension and find freespace */
    if (!pc_malias(vffilename,vffileext, filename, &segs_required, pobj, n_segs, use_charset)) /*__fn__*/
        return (FALSE); /* pc_malias set errno */

    n_segs = segs_required;  /* If we only need one, pc_malias() only got us one otherwise it is what we started with */
                             /* If filename is a valid sfn except for case we will create an lfn, but pc_malias() will have
                                failed if the upper-case alias has already been found */

    /* See if pc_malias() got the free segments we needed */
    while (pobj->finode->s.nsegs < n_segs)
    { /* No.. grow the cluster chain */
        dword blockno;
        int index;
        cluster = pc_grow_dir(pobj->pdrive, pmom, &prev_end_cluster);
        if (!cluster)
            return (FALSE);
         /* Do not forget where the new item is   */
         pd->my_block = blockno = pc_cl2sector(pobj->pdrive , cluster);

         pd->my_index = 0;
         /* Zero out the cluster    */
         if (!pc_clzero( pobj->pdrive , cluster ) )
             goto clean_and_fail;
         /*  add segments to the freelist until we have enough */
         index = 0;
         {
             dword n_sectors = 0;
             while (pobj->finode->s.nsegs != n_segs)
             {  /* pc_addtoseglist() always increments pobj->finode->s.nsegs, no endless loop possible */
                    pc_addtoseglist(&pobj->finode->s, blockno, index);
                    index++;
                    if (index == pobj->pdrive->drive_info.inopblock)
                    {
                        n_sectors += 1;
                        if (n_sectors >= pobj->pdrive->drive_info.secpalloc)
                            break; /* Go for another cluster */
                        else
                        {
                            index = 0;
                            blockno++;
                        }
                    }
             }
         }
    }
    /* Save and restore the sgment list since init_inode vlobbers it */
    saved_segdesc = pobj->finode->s;
    pc_init_inode( pobj->finode, vffilename, vffileext,
                    attr, initcluster, /*size*/ 0L ,pc_getsysdate(&crdate) );
    pobj->finode->s = saved_segdesc;

    /* If the alias contains Kanji E5 this goes on disk as 05 so we have to
       checksum it with 05 */
    if (pobj->finode->fname[0] == 0xe5)
    {
        pobj->finode->fname[0] = 0x05;
        cksum = pc_cksum((byte*)pobj->finode);
        pobj->finode->fname[0] = 0xe5;
    }
    else
         cksum = pc_cksum((byte*)pobj->finode);
    pobj->finode->s.ncksum = cksum;

    /* Write the dosinode and long names to disk. also sets blockinfo to point at DOSINODE */
    if (pc_seglist2disk(pobj, &pobj->finode->s, filename, use_charset))
    {   /* The seglist now holds the lfn and DOSINOE info, we want only lfn,
           so remove the last segment from the segment list (the DOSINODE) */
        pc_reduceseglist(&pobj->finode->s, pobj->pdrive->drive_info.inopblock);
        pc_marki(pobj->finode , pobj->pdrive , pd->my_block, pd->my_index);
        return(TRUE);
    }

clean_and_fail:
    fatop_truncate_dir(pdrive, pmom, cluster,prev_end_cluster);
    return (FALSE);
}


static void pcdel2lfi(LFNINODE *lfi, int nsegs)    /* __fn__ */
{
    for (;nsegs;nsegs--, lfi--)
        lfi->lfnorder = PCDELETE;
}


BOOLEAN pc_deleteseglist(DDRIVE *pdrive, SEGDESC *s)  /* __fn__ */
{
BLKBUFF *rbuf;
LFNINODE *lfi;
int ntodo_0,ntodo_1,ntodo_2;

    if (!s->nsegs)
        return(TRUE);
    /* Read segblock[0] and copy text   */
    rbuf = pc_read_blk(pdrive, s->segblock[0]);
    if (!rbuf)
        return(FALSE);
    lfi = (LFNINODE *) rbuf->data;
    lfi += s->segindex;
    /* If the lfn segments span two or more blocks then segblock[0] contains the
       last block, segblock[1] contains next to last, segblock[3] contains
        the last if there are three. We delete all of the segments up to
        and including segindex in last block value stored in segblock[0]. */
    if (s->nsegs > s->segindex+1)
        ntodo_0 = s->segindex+1;
    else
        ntodo_0 = s->nsegs;

    if (ntodo_0 > 0) /* Test just in case should never be <= 0 */
        pcdel2lfi(lfi, ntodo_0);
    if ( !pc_write_blk(rbuf) )
    {
        pc_discard_buf(rbuf);
        return (FALSE);
    }
    else
    {
        pc_release_buf(rbuf);
    }
    if (s->segblock[1])
    {
           rbuf = pc_read_blk(pdrive, s->segblock[1]);
           if (!rbuf)
               return (FALSE);
        lfi = (LFNINODE *) rbuf->data;
        lfi += (pdrive->drive_info.inopblock-1);  /* The last index */

        /* Delete the next N segments. Clip it at 16 since there are only
           16 segments per block */
        ntodo_1 = s->nsegs - ntodo_0;
        if (ntodo_1 > pdrive->drive_info.inopblock)
            ntodo_1 = pdrive->drive_info.inopblock;

        if (ntodo_1 > 0) /* Test just in case should never be <= 0 */
            pcdel2lfi(lfi, ntodo_1);
        if ( !pc_write_blk ( rbuf ) )
        {
            pc_discard_buf(rbuf);
            return (FALSE);
        }
        else
            pc_release_buf(rbuf);
           if (s->segblock[2])
        {
               rbuf = pc_read_blk(pdrive, s->segblock[2]);
               if (!rbuf)
                   return (FALSE);
            lfi = (LFNINODE *) rbuf->data;
            lfi += (pdrive->drive_info.inopblock-1);  /* The last index */

            ntodo_2 = s->nsegs - (ntodo_1 + ntodo_0);
            /* Delete the next N segments. Clip it at 16 since there are only
            16 segments per block - this should not happen but just to be safe*/
            if (ntodo_2 > pdrive->drive_info.inopblock)
                ntodo_2 = pdrive->drive_info.inopblock;

            if (ntodo_2 > 0) /* Test just in case should never be <= 0 */
                pcdel2lfi(lfi, ntodo_2);
            if ( !pc_write_blk ( rbuf ) )
            {
                pc_discard_buf(rbuf);
                return (FALSE);
            }
            else
                pc_release_buf(rbuf);
        }
    }
    return(TRUE);
}

static byte *text2lfi(byte *lfn, LFNINODE *lfi, int nsegs, byte ncksum, byte order, int use_charset) /* __fn__ */
{
    int n;
    BOOLEAN end_of_lfn = FALSE;
    byte *pfi;

    for (;nsegs && !end_of_lfn;nsegs--, lfi--, order++)
    {
        pfi = lfi->lfname1;
        for(n=0; n<10; n += 2, pfi += 2)
        {
            if (end_of_lfn)
                *pfi = *(pfi+1) = 0xff;
            else
            {
                CS_OP_TO_LFN(pfi, lfn ,use_charset);
                CS_OP_INC_PTR(lfn,use_charset);
                if ((*pfi== 0) && (*(pfi+1) == 0))
                    end_of_lfn = TRUE;
            }
        }
        pfi = lfi->lfname2;
        for(n=0; n<12; n += 2, pfi += 2)
        {
            if (end_of_lfn)
                *pfi = *(pfi+1) = 0xff;
            else
            {
                CS_OP_TO_LFN(pfi, lfn,use_charset);
                CS_OP_INC_PTR(lfn,use_charset);
                if ((*pfi== 0) && (*(pfi+1) == 0))
                    end_of_lfn = TRUE;
            }
        }
        pfi = lfi->lfname3;
        for(n=0; n<4; n += 2, pfi += 2)
        {
            if (end_of_lfn)
                *pfi = *(pfi+1) = 0xff;
            else
            {
                CS_OP_TO_LFN(pfi, lfn,use_charset);
                CS_OP_INC_PTR(lfn,use_charset);
                if ((*pfi== 0) && (*(pfi+1) == 0))
                    end_of_lfn = TRUE;
            }
        }
        if (CS_OP_IS_EOS(lfn,use_charset))
        {
            end_of_lfn = TRUE;
        }
        if (end_of_lfn)
            order |= FIRST_NAMESEG;
        lfi->lfnorder = order;
        lfi->lfnattribute = 0x0F;
        lfi->lfnres =  0;
        lfi->lfncksum = ncksum;
        lfi->lfncluster = 0x0000;
    }
    return(lfn);
}


/* This function is used by pc_insert_inode(). That function builds a seglist
   that is 1 segment longer then the lfn. The extra segment is where the
   dosinode will be placed. Before we write the the lfn to disk we call this
   function to reduce the segment list by one. */

static void pc_reduceseglist(SEGDESC *s, int inodes_perblock)
{
    if (s->nsegs)                   /* This should always be true */
    {
        s->nsegs -= 1;
        if (s->segblock[2] && s->segindex == 0)
        {
            s->segblock[0] = s->segblock[1];
            s->segblock[1] = s->segblock[2];
            s->segblock[2] = 0;
            s->segindex = inodes_perblock-1;
        }
        else if (s->segblock[1] && s->segindex == 0)
        {
            s->segblock[0] = s->segblock[1];
            s->segblock[1] = s->segblock[2] = 0;
            s->segindex = inodes_perblock-1;
        }
        else
        {
            if (s->segindex)        /* This should always be true */
                s->segindex -= 1;
        }
    }
}


/*****************************************************************************
PC_MALIAS - Create a unique alias for input_file

  Description
  Fills fname and fext with a valid short file name alias that is unique
  in the destination directory (dest).  Not to be confused with pc_galias,
  which finds the currently used short file name alias for an existing
  file.
  Returns
  TRUE if a unique alias could be found, FALSE otherwise
*****************************************************************************/

#if (INCLUDE_CS_JIS)
/* int jis_char_length(byte *p) */
int jis_char_length(byte *p);
#define alias_char_length(p) jis_char_length(p)
#else
#define alias_char_length(p) 1
#endif


static BOOLEAN pc_malias(byte *fname, byte *fext, byte *input_file, int *segs_required, DROBJ *pobj, int nsegs, int use_charset) /*__fn__*/
{
    int alias_number;
    byte alias_image[26];
    byte alias_base[26];
    byte alias_ext[8];
    byte saved_alias_image_0;

    *segs_required = nsegs;   /* Start assuming we'll need an alias and an lfn. If it is already a valid alias
                                 we will set *segs_required to one */
    /* See if already a valid alias. If so we can use it if it doesn't already exist */
    /* Note: This always fails for unicode use alias base for temp storage */
    if (pc_cs_malias(alias_image, input_file, -1, use_charset)) /*__fn__*/
    {
        /* alias_image[] contains a valid file name. */
        /* Parse into fname.ext */
        rtfs_cs_ascii_fileparse(fname,fext,&alias_image[0]);
        /* If base contains Kanji E5 this goes on disk as 05 so change it to 05 while we search */
        saved_alias_image_0 = alias_image[0];
        if (saved_alias_image_0 == 0xe5)
        {
            *fname = alias_image[0] = 0x05;
        }
        /* call _pc_multi_alias_scan with nsegs == 1
           ignore alias_image and search on
           and fname and fext as base and extension
           alias_image is ignored
           Return -1 on an error.
           Return -1 and sets errno to PEEXIST if the SFN is found
        */
        /* Override segment count and set to one to indicate a short file if the input is not UNICODE
           and input_file is a valid SFN with no lower case characters */
        if (use_charset != CS_CHARSET_UNICODE && pc_cs_valid_sfn(input_file, TRUE))
            *segs_required = 1; /* Indicates a valid alias */
        alias_number = _pc_multi_alias_scan(pobj, *segs_required, fext, alias_image, fname);
        *fname  = alias_image[0] = saved_alias_image_0;
        if (alias_number < 0)
            return(FALSE);
        else
        {
            return(TRUE);
        }
    }
    /* Fall through if not a valid SFN and an alias is required */
    /* Now create alias base */
    {
//    byte alias_scratch[26];
    /* Create base~1.ext */
    pc_cs_malias(alias_image, input_file, 1, use_charset);
    /* Parse into alias_base and alias_ext */
    rtfs_cs_ascii_fileparse(alias_base,alias_ext,&alias_image[0]);
    }
    /* If base contains Kanji E5 this goes on disk as 05 so change it to 05 while we search */
    saved_alias_image_0 = alias_image[0];
    if (saved_alias_image_0 == 0xe5)
    {
        alias_image[0] = alias_base[0] = 0x05;
    }
    /* Get the next available alias number and find free segments */
    /* alias base is xxx~ image is xxx.ext and ext is "ext". return < 0 if not an alias, 0 if LFN==ALIAS or the alias number, 1,2,3 etc */
    alias_number = _pc_multi_alias_scan(pobj, nsegs, alias_ext, alias_image, alias_base);
    alias_base[0] = alias_image[0] = saved_alias_image_0;

    if (alias_number < 0)
    {
        return(FALSE);
    }
    else
    { /* alias_number (1,2,3 etc) needs to go after tilde */
    byte *last_tilde, *p;
    int char_len;
        last_tilde = 0;
        p = &alias_base[0];
        while (*p)
        {
            char_len = alias_char_length(p);
            if ((char_len == 1) && (*p == (byte) '~'))
                last_tilde = p;
            p += char_len;
        }
        if (!last_tilde)   /* won't happen */
            return(FALSE);
        *(last_tilde+1) = 0;
        /* create xx~alias#.ext from xx~ + ext, + pointer to last tilde, plus alius number */
        pc_complete_alias_name(&alias_base[0], &alias_ext[0], last_tilde, alias_number);
        /* Parse the alias into 8.3 */
        rtfs_cs_ascii_fileparse(fname,fext,&alias_base[0]);
        return(TRUE);
    }
}

static int pc_multi_get_entry_number(byte *alias_image, byte *alias_base, DOSINODE *pi)
{
int i,base_file_length,alias_number,num_alias_digits;
byte *ppattern,*psource;
char c;

    /* We know the the extension matches, now see if pi is exactly the sfn for the alias with no tilde */
    if (alias_image[0])
    {
        psource = pi->fname;
        for (i = 0; i < 8; i++,psource++)
        {
        int tilde_count;
            tilde_count = 0;
            if (alias_image[i] == '~')
            {
                int j;
				/* July 2012 Added && alias_image[j] */
                for (j = i; j < 8 && alias_image[j]; j++)
                {
                    if (alias_image[j] == '~')
                        tilde_count += 1;
                }
            }
            /* If the last character and still match, return 0 */
            if ( i == 7 && alias_image[i] == *psource)
                return(0);
            /* If is is the tilde or . or end of file name and at the end of the the file name in pi->fname return 0 */
            if (tilde_count == 1 ||
                alias_image[i] == 0 ||
                alias_image[i] == '.')
            {
                if (*psource == (byte) ' ')
                {
                    return(0);
                }
                else
                    break;    /* Not a match, more in the finode, none in the alias_image */
            }
            else if (alias_image[i] != *psource) /* Not a match for sure */
                break;
        }
    }

    /* See if pi is an alias of alias_base */
    ppattern = alias_base;
    base_file_length = 0;
    psource = pi->fname;
    for (i = 1; i < 9; i++,psource++,ppattern++)
    {
        if (*psource == '~')
            base_file_length = i;
        else
        {
            /* If they don't match and there is no '~' we no there is no alias */
            if (*ppattern != *psource && !base_file_length)
                return(-1);
        }
    }
    /* File name ending in '~' not an alias */
    if (base_file_length == 8)
        return(-1);

    /* A~xxxx8192 */
    /* 9999 is the largest index we support so if base_file_length < 4
       (base_file_length include '~') return no match
       This was wrong and is removed
      if (base_file_length < 4)
        return(-1);
    */

    psource = pi->fname;
    psource += base_file_length;
    /* the first digit can't be zero looking for (1 - 4095) */
    c = (char) *psource;
    if (c < '1'|| c > '9')
        return(-1);
    alias_number = 0;
    num_alias_digits = 0;
    for (i = base_file_length; i < 8; i++,psource++)
    {
        int alias_digit;
        c = (char) *psource;
        if (c == ' ')
            break;
        if (c < '0'|| c > '9')
            return(-1);
        num_alias_digits++;
        alias_digit = (int) c - '0';
        alias_number *= 10;
        alias_number += alias_digit;
        if (alias_number >= 8192)
            return(-1);
    }

    /* Removed the following code because there was no reason for it:
    if ((base_file_length + num_alias_digits) != 8)
        return(-1); */
    return(alias_number);
}

static void pc_complete_alias_name(byte *palias_base,byte *fext, byte *plast_tilde, int alias_number)
{
int i,char_len;
int max_tilde_pos, digit, n_digits, num;
byte *p,*p_alias_digits;

    digit = 0;
    if (alias_number > 999)
        n_digits = 4;
    else if (alias_number > 99)
        n_digits = 3;
    else if (alias_number > 9)
        n_digits = 2;
    else
        n_digits = 1;


    p = palias_base;
    /* Find the location for ~### */
    max_tilde_pos = 8 - (n_digits+1);
    p_alias_digits = 0;
    for (i = 0; i < 8;)
    {
        char_len = alias_char_length(p);
        if (p == plast_tilde)
        {
            p_alias_digits = p;
            break;
        }
        else if ((i + char_len) > max_tilde_pos)
        {
            p_alias_digits = p;
            break;
        }
        i += char_len;
        p += char_len;
    }
    /* Put in the tilde */
    *p_alias_digits = '~';
     /* Null terminate */
     p = p_alias_digits + n_digits;  /* Point at last digit */
     /* Now put in the digits, working backwards */
     num = alias_number;
     i = n_digits;
     while(i--)
     {
         byte c;
         digit = (int) (num % 10);
         c = (byte)(digit + '0');
         *p = c;
         num /= 10;
         p--;
     }
     p = p_alias_digits + 1 + n_digits;  /* Skip ~ and all digits, put in a null */
     *p = 0;
     if (*fext) /* Put on .ext and terminate */
     {
        int i;
        *p++ = (byte) '.';
        *p = 0;
        for (i = 0; i < 3; i++)
        {
            if (!*fext)
                break;
            *p++ = *fext++;
            *p = 0;
        }
     }
}

/* Scans the director at pmom..
    Get the next available alias number for alias_image, alias_base
    Find free segments and update the segment list of pobj to point at available free
    segments.. Clear segment list if enough segments not found.. If end of
    directory contains segments they are kept and added to later whhe the directory is
    expanded
*/

struct alias_map {
        byte *palias_bytemap;
        BLKBUFF *palias_bitmap_pbuff[2];
        byte *palias_bitmap[2];
};


static BOOLEAN pc_init_alias_map(DDRIVE *pdrive, struct alias_map *pamap,dword ubuff_size, byte *puser_buffer);
static void pc_release_alias_map(struct alias_map *pamap);
static void   pc_set_alias_map(struct alias_map *pamap, int alias_index);
static int pc_scan_alias_map(struct alias_map *pamap);

static int _pc_multi_alias_scan(DROBJ *pobj, int n_segs, byte *fext, byte *alias_image, byte *alias_base)
{
    dword user_buffer_first_block,user_buffer_n_blocks,blocks_in_buffer,entries_processed;
    DIRBLK *pd;
    DOSINODE *pi;
    BOOLEAN still_searching, end_of_dir, sfn_match;
    BLKBUFF *scan_buff, **pscan_buff; /* If using block buffers for scan */
    byte *pscan_data;
    struct alias_map amap;
    int i;
    int ret_val = -1;
    dword ubuff_size;
    byte *puser_buffer;
    byte ext_padded[4];
    byte name_padded[10];

    entries_processed = 0;
    sfn_match = FALSE;
    /* Make a copy of the extension padded with spaces */
    for (i = 0; i < 3; i++)
       ext_padded[i] = ' ';
    for (i = 0; i < 3; i++)
    {
       if (fext[i])
           ext_padded[i] = fext[i];
       else
           break;
    }
    /* Make a copy of the base padded with spaces if nsegs is 1. Otherwise we don't need it  */
    for (i = 0; i < 8; i++)
        name_padded[i] = ' ';
    if (n_segs == 1)
    {
       for (i = 0; i < 8; i++)
       {
           if (alias_base[i])
               name_padded[i] = alias_base[i];
           else
               break;
       }
    }


    scan_buff = 0;
    pscan_buff = &scan_buff;    /* Assume using buffer pool to start */

    ubuff_size = 0;
    puser_buffer = 0;

    /* Use unbuffered mode if the userbuffer holds a whole cluster and when looking for a specific entry.
       This bypasses the block buffer pool and makes single entry searches faster. Otherwise the data is rippled
       through the buffer pool and will be there for future calls  */
    puser_buffer = pc_claim_user_buffer(pobj->pdrive, &ubuff_size, (dword) pobj->pdrive->drive_info.secpalloc);  /* released on cleanup */
    if (puser_buffer)
    {
        pscan_buff = 0;             /* To use user buffer */
    }
    /* Set up an alias bitmap to track up to 8192 aliases
       This is only used if n_segs > 1, could/should be optimized */
    if (!pc_init_alias_map(pobj->pdrive, &amap,ubuff_size,puser_buffer))
        goto return_failure;
    /* Set the zero entry in the alias map, this way the first return value is 1 */
    pc_set_alias_map(&amap,0);

    pc_zeroseglist(&pobj->finode->s); /* Zero free segment list */

    /* For convenience. We want to get at block info here   */
    pd = &pobj->blkinfo;
    pd->my_block = pd->my_frstblock;
    pd->my_index = 0;

    /* Read the data   */
    still_searching = TRUE;
    end_of_dir = FALSE;


    if (!pc_multi_dir_get(pobj, pscan_buff, &pscan_data, puser_buffer, &blocks_in_buffer, FALSE))
        goto return_failure;
    user_buffer_first_block = pobj->blkinfo.my_block;
    user_buffer_n_blocks = blocks_in_buffer;
    if (!blocks_in_buffer)
    {
        still_searching = FALSE;
        pi = 0;
    }
    else
        pi = (DOSINODE *) pc_data_in_ubuff(pobj->pdrive, user_buffer_first_block, pscan_data, user_buffer_first_block,user_buffer_n_blocks);

    while (still_searching)
    {
        /* Look at the current inode   */
        pi += pd->my_index;
        end_of_dir = FALSE;
        /* And look for a match   */
        while ( pd->my_index < pobj->pdrive->drive_info.inopblock)
        {
            /* End of dir if name is 0  or 8 x (0xff)  */
            if (pc_check_dir_end(pi->fname))
                end_of_dir = TRUE;
            if (pi->fname[0] == PCDELETE || end_of_dir)
            {
                /* Add to the free seg list if we don't already have enough
                   this scheme makes sure that there is room for the DOSINODE
                   immediately after the segment list. We reduce the segment
                   count before we write the segments */
                if (pobj->finode->s.nsegs != n_segs)
                    pc_addtoseglist(&pobj->finode->s, pd->my_block, pd->my_index);
            }
            else if (pi->fattribute == CHICAGO_EXT)
            {
                if (pobj->finode->s.nsegs != n_segs)    /* Not free. zero freesegments */
                    pc_zeroseglist(&pobj->finode->s);
            }
            else  /* if (pi->fattribute != CHICAGO_EXT) */
            { /* Check for alias */
                if (pobj->finode->s.nsegs != n_segs)/* Not free, zero free seg list unless we have space already */
                    pc_zeroseglist(&pobj->finode->s);
                /* Check for a match on the extension */
                if (!(pi->fattribute&AVOLUME) && rtfs_bytecomp(&pi->fext[0], &ext_padded[0], 3))
                {
                    /* Now check if we are building the alias map or failing on an SNF match */
                    if (n_segs != 1)
                    {
					int e;
						e=pc_multi_get_entry_number(alias_image, alias_base, pi);
                        /* The extension is the same, now check if the alias base xxx~ can be an alias for alias_image yyy.ext
                           and if it is, put it in the alias map */
                        pc_set_alias_map(&amap,e);
                    }
                    else
                    {
                        /* NSEGS == 1, fail with PEEXIST is a match is found */
                        if (rtfs_bytecomp(&pi->fname[0], &name_padded[0], 8))
                        {
                            sfn_match = TRUE;
                            goto return_results;
                        }
                    }

                }
            }
            pd->my_index++;
            pi++;
        }

        pd->my_index = 0;
        blocks_in_buffer -= 1;

        /* If we hit a directory terminator and we have already found the segments we will need to allocate then
           we should break out. This eliminates FALSE positive tests for (entries_processed >= RTFS_CFG_MAX_DIRENTS).
           This fixes a problem that is evident when RTFS_CFG_MAX_DIRENTS is set to a very low value.
           If RTFS_CFG_MAX_DIRENTS was less than the number of directory entries that could fit in a cluster then
           this error occured every time a directory entry create operation was attempted.
        */
        if (end_of_dir && pobj->finode->s.nsegs == n_segs)
            break;
        entries_processed += pobj->pdrive->drive_info.inopblock;
        if (entries_processed >= RTFS_CFG_MAX_DIRENTS)
        {
            rtfs_set_errno(PEINVALIDDIR, __FILE__, __LINE__);
            ret_val = -1;
            goto return_failure;
        }

        if(blocks_in_buffer)
            pobj->blkinfo.my_block += 1;
        else
        {
            if (!pc_multi_dir_get(pobj, pscan_buff, &pscan_data, puser_buffer, &blocks_in_buffer, TRUE))
                goto return_failure;
            if (!blocks_in_buffer)
            {
                still_searching = FALSE;
                goto return_results;
            }
            else
            {
                user_buffer_first_block = pobj->blkinfo.my_block;
                user_buffer_n_blocks = blocks_in_buffer;
                pi = (DOSINODE *) pc_data_in_ubuff(pobj->pdrive, user_buffer_first_block, pscan_data, user_buffer_first_block,user_buffer_n_blocks);
            }
        }
    }
return_results:
    if (n_segs == 1) /* Note: If n_segs is 1 set ret_val to -1 and set PEEXIST if we had a match */
    {
        if (sfn_match)
        {
            rtfs_set_errno(PEEXIST, __FILE__, __LINE__);
            ret_val = -1;
        }
        else
            ret_val = 0;
    }
    else
    {
        /* Now scan the alias map for the first free alias and fall through */
        ret_val = pc_scan_alias_map(&amap);
    }
return_failure:
    if (scan_buff)
        pc_release_buf(scan_buff);
    pc_release_alias_map(&amap);
    if (puser_buffer)
        pc_release_user_buffer(pobj->pdrive, puser_buffer);
    return(ret_val);
}
#define FIVE12 512 /* Not sector size dependant */
static BOOLEAN pc_init_alias_map(DDRIVE *pdrive, struct alias_map *pamap,dword ubuff_size, byte *puser_buffer)
{
    dword cluster_size_bytes,map_buffer_size_bytes;
    rtfs_memset(pamap, 0, sizeof(*pamap));
    /* See if we can fit one cluster plus an 8k or 1K alias map in the user buffer.. if not use block buffers */
    map_buffer_size_bytes = ubuff_size << pdrive->drive_info.log2_bytespsec;
    cluster_size_bytes = 1;  cluster_size_bytes <<= pdrive->drive_info.log2_bytespalloc;
    if (map_buffer_size_bytes > cluster_size_bytes)
        map_buffer_size_bytes -= cluster_size_bytes;
    else
        map_buffer_size_bytes = 0;

    if (map_buffer_size_bytes >= 8192)
    {/* We have 8K available for the map after one cluster so use byte operations.. */
        pamap->palias_bytemap = puser_buffer;
        pamap->palias_bytemap += cluster_size_bytes;
        rtfs_memset(pamap->palias_bytemap, 0, 8192);
    }
    else if (map_buffer_size_bytes >= 1024)
    {/* We have 1K in the user buffer after one cluster so use bit operations.. */
    byte *p;
        p = puser_buffer;
        p += cluster_size_bytes;
        rtfs_memset(p, 0, 1024);
        pamap->palias_bitmap[0] = p;
        p += FIVE12;
        pamap->palias_bitmap[1] = p;
    }
    else
    { /* Use a 1 k bitmap from scratch buffers */
        pamap->palias_bitmap_pbuff[0] = pc_scratch_blk();
        if (!pamap->palias_bitmap_pbuff[0])
            return(FALSE);
        pamap->palias_bitmap[0] = (byte *)pamap->palias_bitmap_pbuff[0]->data;
        rtfs_memset(pamap->palias_bitmap[0], 0, pamap->palias_bitmap_pbuff[0]->data_size_bytes);
        if (pamap->palias_bitmap_pbuff[0]->data_size_bytes >= 1024)
        {
            /* Scratch holds >= 1 K */
            pamap->palias_bitmap[1] = pamap->palias_bitmap[0] + FIVE12;
        }
        else
        {
            pamap->palias_bitmap_pbuff[1] = pc_scratch_blk();
            if (!pamap->palias_bitmap_pbuff[1])
            {
                pc_free_scratch_blk(pamap->palias_bitmap_pbuff[0]);
                pamap->palias_bitmap_pbuff[0] = 0;
                return(FALSE);
            }
            pamap->palias_bitmap[1] = (byte *)pamap->palias_bitmap_pbuff[1]->data;
            rtfs_memset(pamap->palias_bitmap[1], 0, pamap->palias_bitmap_pbuff[1]->data_size_bytes);
        }
    }
    return(TRUE);
}

static void pc_release_alias_map(struct alias_map *pamap)
{
    if (pamap->palias_bitmap_pbuff[0])
        pc_free_scratch_blk(pamap->palias_bitmap_pbuff[0]);
    if (pamap->palias_bitmap_pbuff[1])
        pc_free_scratch_blk(pamap->palias_bitmap_pbuff[1]);
}

static void   pc_set_alias_map(struct alias_map *pamap, int alias_index)
{
    if (alias_index >= 0 && alias_index < 8192)
    {
        if (pamap->palias_bytemap)
            *(pamap->palias_bytemap+alias_index) = 1;
        else
        {
            int byte_index, bit_index;
            byte byte_set;
            byte *p;
            byte_index = alias_index/8;
            bit_index = alias_index & 0x7;
            byte_set = 1;
            byte_set <<= bit_index;

            if (byte_index < FIVE12)
                p= pamap->palias_bitmap[0];
            else
            {
                p= pamap->palias_bitmap[1];
                byte_index -= FIVE12;
            }
            p += byte_index;
            *p |= byte_set;
        }
    }
}
/* Find the first zero value in the alias map */
static int pc_scan_alias_map(struct alias_map *pamap)
{
    if (pamap->palias_bytemap)
    {
        int i;
        byte *palias_bytemap;
        palias_bytemap = pamap->palias_bytemap;
        for (i = 0; i < 8192; i++,palias_bytemap++)
        {
            if (*palias_bytemap == 0)
			{
                return(i);
			}
       }
    }
    else
    {
        int h, i, j, base;
        for (h = 0, base = 0; h < 2; h++, base += FIVE12)
        {
            byte *p;
            p = pamap->palias_bitmap[h];
            for (i = 0; i < FIVE12; i++, p++)
            {
                if (*p != 0xff)
                {
                    byte b; b = 1;
                    for (j = 0; j < 8; j++, b <<= 1)
                       if (!(*p & b))
                       {
                           /* Changed July 2012 from return(base+i*8+j);
                              Previous version had a logic error at 512 files*/
                           return((base+i)*8+j);
                       }

                }
            }
        }
    }
    return(-1);
}


BOOLEAN _illegal_lfn_char(byte ch)
{
    if (pc_strchr((byte *)pustring_sys_badlfn, ch))
        return(TRUE);
    else
        return(FALSE);
}
BOOLEAN pc_delete_lfn_info(DROBJ *pobj)
{
     return(pc_deleteseglist(pobj->pdrive, &pobj->finode->s));
}
void pc_zero_lfn_info(FINODE *pdir)
{
    pc_zeroseglist(&pdir->s);
}


static BOOLEAN pc_seglist2disk(DROBJ *pobj, SEGDESC *s, byte *lfn,int use_charset) /* __fn__*/
{
BLKBUFF *rbuf;
LFNINODE *lfi;
DOSINODE *pi;
int lfn_nsegs, ntodo_0,ntodo_1,ntodo_2;
byte *pmore_lfn;


    if (!s->nsegs)
        return(FALSE);

    /* If not on a block boundary or we don't fill a whole block, read the block first.*/
	/* Bug fix: Nov 2009. Changed if(s->segindex .. to if (s->segindex != pobj->pdrive->drive_info.inopblock-1 ..
		In original code if s->segindex is zero (DOS inode is at the beginning of a sector) and s->nsegs >= 16
		(below), buffer will be initialized and segments of other files may be corrupted. */
    if (s->segindex != pobj->pdrive->drive_info.inopblock-1 || s->nsegs < pobj->pdrive->drive_info.inopblock)
        rbuf = pc_read_blk(pobj->pdrive, s->segblock[0]);
    else /* otherwise just intialize the buffer because we'll fill it all */
        rbuf = pc_init_blk(pobj->pdrive, s->segblock[0]);
    if (!rbuf)
        return(FALSE);
    /* If the lfn segments span two or more blocks then segblock[0]
       contains the last block and segblock[1] contains the next to last
       if there are 3 blocks segblock[2] contains the first
       The DOS inode is at the end of segblock[0]   */

    /* Save the 8.3 DOSINODE as our location and initialze the DOSINODE in the buffer */
    pi =  (DOSINODE *) rbuf->data;
    pi += s->segindex;
    pc_ino2dos( pi, pobj->finode);
    pobj->blkinfo.my_block = s->segblock[0];
    pobj->blkinfo.my_index = s->segindex;

    /* Now for LFNS - if no lfns we just write the short filename */
    ntodo_0 = ntodo_1 = ntodo_2 = 0;
    lfn_nsegs = s->nsegs-1; /* s->nsegs contains segments for lfn plus alias */
    pmore_lfn = lfn;        /* pointer to part of lfn not yet converted by text2lfi() */
    if (lfn_nsegs)          /* Initialize lfn segs if there are any */
    {
        if (s->segindex)
        {   /* alias goes at the end of first block. lfn shares the block */
        int lfn_segindex;
            lfn_segindex = s->segindex-1;
            if (lfn_nsegs > lfn_segindex+1)
                ntodo_0 = lfn_segindex+1;   /* Only up to index */
            else
                ntodo_0 = lfn_nsegs;        /* All fits */
            lfi = (LFNINODE *) rbuf->data;
            lfi += lfn_segindex;
            pmore_lfn = text2lfi(pmore_lfn, lfi, ntodo_0, s->ncksum, 1, use_charset);
        }
        ntodo_1 = lfn_nsegs - ntodo_0;
    }

    /* we'll always write this block becuase of alias */
    if ( !pc_write_blk ( rbuf ) )
    {
        pc_discard_buf(rbuf);
        return (FALSE);
    }
    pc_release_buf(rbuf);
    if (!ntodo_1)
        return(TRUE);

    if (s->segblock[1]) /* defensive s->segblock[1] will be zero if ntodo_1 */
    {
        if (ntodo_1 >= pobj->pdrive->drive_info.inopblock) /* If we span one or more blocks, truncate to a block and init a buffer */
        {
            rbuf = pc_init_blk(pobj->pdrive, s->segblock[1]);
            ntodo_1 = pobj->pdrive->drive_info.inopblock;
        }
        else /* We didn't span, so read the buffer first */
            rbuf = pc_read_blk(pobj->pdrive, s->segblock[1]);
        if (!rbuf)
            return(FALSE);
        lfi = (LFNINODE *) rbuf->data;
        lfi += (pobj->pdrive->drive_info.inopblock-1);  /* The last index */
        /* Do lfn segments in this block */
        pmore_lfn = text2lfi(pmore_lfn, lfi, ntodo_1, s->ncksum, (byte) (ntodo_0+1), use_charset);
        ntodo_2 = lfn_nsegs - (ntodo_1 + ntodo_0);
        if ( !pc_write_blk ( rbuf ) )
        {
            pc_discard_buf(rbuf);
            return (FALSE);
        }
        pc_release_buf(rbuf);
    }

    if (ntodo_2 && s->segblock[2]) /* defensive s->segblock[2] will be zero if ntodo_2 */
    {
        if (ntodo_2 >= pobj->pdrive->drive_info.inopblock) /* If we span one or more blocks, truncate to a block and init a buffer */
        {
            rbuf = pc_init_blk(pobj->pdrive, s->segblock[2]);
            ntodo_2 = pobj->pdrive->drive_info.inopblock;
        }
        else /* We didn't span, so read the buffer first */
            rbuf = pc_read_blk(pobj->pdrive, s->segblock[2]);
        if (!rbuf)
            return(FALSE);
        lfi = (LFNINODE *) rbuf->data;
        lfi += (pobj->pdrive->drive_info.inopblock-1);  /* The last index */
        /* Do 16 more segments (means more blocks to follow) or whatever is
        left */
        if (ntodo_2 > pobj->pdrive->drive_info.inopblock) /* Should always be <= pobj->pdrive->drive_info.inopblock */
            ntodo_2 = pobj->pdrive->drive_info.inopblock;
        pmore_lfn = text2lfi(pmore_lfn, lfi, ntodo_2, s->ncksum, (byte) (ntodo_1+ntodo_0+1), use_charset);
        if ( !pc_write_blk ( rbuf ) )
        {
            pc_discard_buf(rbuf);
            return (FALSE);
        }
        else
            pc_release_buf(rbuf);
    }
    return(TRUE);
}

#endif
#endif /* Exclude from build if read only */
