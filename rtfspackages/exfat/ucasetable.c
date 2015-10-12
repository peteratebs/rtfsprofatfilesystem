/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* UCASETABLE.C - Ucase functions for exFat.

    Routines in this file include:
	 pcexfat_upcase_unicode_string		- Called by pcexfat_filenameobj_init and pcexfat_findinbyfilenameobj
	 exfatop_read_upCaseTable			- Called by boot code.

*/
#include "rtfs.h"

static BOOLEAN exfatop_expand_upCaseTable(DDRIVE *pdr);
#define DO_DUMP_UCASE_TABLE 0
#if (DO_DUMP_UCASE_TABLE)
static void 	dump_expanded_upCaseTable(DDRIVE *pdr);
#endif

/* Convert a unicode string to UpCased (note: to and from may point to be same
   location to upCase in place */
void pcexfat_upcase_unicode_string(DDRIVE *pdr, word *to, word *from,int maxcount)
{
word w,UpCaseMaxTranskey;
word *pUpCaseTable;

	pUpCaseTable = (word *) PDRTOEXFSTR(pdr)->UpCaseBufferCore;
    UpCaseMaxTranskey = PDRTOEXFSTR(pdr)->UpCaseMaxTranskey;
	if (UpCaseMaxTranskey == 0xffff)
	{	/* Contains a full table */
		while(*from && maxcount--)
		{
			*to++ = pUpCaseTable[*from++];
		}
	}
	else
	{  /* Mandatory 128 version if it is  */
		while(*from && maxcount--)
		{
			w = *from++;
			if (w & 0x7f)
				*to++ = pUpCaseTable[w];
			else
				*to++ = w;
		}
	}
	*to = 0;
}


BOOLEAN exfatop_read_upCaseTable(DDRIVE *pdr)
{
	BOOLEAN ret_val;
	/* No need to load if we detected and vectored over to the compiled in map */
	if (PDRTOEXFSTR(pdr)->UpCaseBufferCore == &cStandarducTableunCompressed[0])
		return(TRUE);
	ret_val = exfatop_expand_upCaseTable(pdr);
#if (DO_DUMP_UCASE_TABLE)
	if (ret_val)
		dump_expanded_upCaseTable(pdr);
#endif
	return(ret_val);
}


static BOOLEAN exfatop_expand_upCaseTable(DDRIVE *pdr)
{
	dword cluster,words_left,words_in_buffer,sector_offset_in_cluster,words_processed,in_an_identy_count;
    BLKBUFF *buf = 0;
    BLKBUFF bbuf_scratch;   /* Used by pc_sys_sector build a buffer structure with core from the driver level */
    byte *b;
	word *pw,*pwto,*pwend,*pwstart,value_counter;
	BOOLEAN processing_expand,ret_val;

	ret_val = FALSE;

	cluster = PDRTOEXFSTR(pdr)->FirstClusterOfUpCase;
    sector_offset_in_cluster = 0;
    words_in_buffer = 0;
	pw = 0;

	value_counter = 0;
    processing_expand = FALSE;

    words_left = 65536;
    words_processed = 0;
	in_an_identy_count = 0;
	pwstart = pwto = (word *)PDRTOEXFSTR(pdr)->UpCaseBufferCore;

	pwend = pwto + words_left;

    while (words_left)
	{
		/* Read another buffer if we have to */
		if (words_in_buffer == 0)
		{
		dword sector;
			if (!buf)
				buf = pc_sys_sector(pdr, &bbuf_scratch);
			if (!buf)
			{
				rtfs_set_errno(PERESOURCESCRATCHBLOCK, __FILE__, __LINE__);
				goto done;
			}
			b = buf->data;
			pw = (word *) b;
			sector = pc_cl2sector(pdr, cluster)+sector_offset_in_cluster;
			if (!raw_devio_xfer(pdr, sector, b,1, FALSE, TRUE))
			{
        		rtfs_set_errno(PEIOERRORREAD, __FILE__, __LINE__);
        		goto done;
			}
			words_in_buffer = pdr->drive_info.bytespsector/sizeof(word);
		}
		/* Process the next word */
		if (processing_expand) /* Got 0xffff - this is the repeat count, advance input, do not advance output buffer */
		{
			in_an_identy_count = *pw;
			words_processed++;
			words_in_buffer -= 1;
			pw++;
			processing_expand = FALSE;
		}
		else if (in_an_identy_count)
		{	/* Expand into the buffer but do not advance input buffer */
			if (words_left)
			{
				*pwto++ = value_counter++;
				words_left--;
			}
			in_an_identy_count--;
		}
		else if (*pw == 0xffff)
		{	/* Set up to expand into the buffer advance input buffer past  */
			if (words_left==1)
			{
				words_left = 0;
				*pwto++ = 0xffff;
			}
			else
				processing_expand = TRUE;
			words_processed++;
			words_in_buffer -= 1;
			pw++;
		}
		else
		{
			value_counter++;
			words_in_buffer -= 1;
			words_left--;
			words_processed++;
			*pwto++ = *pw++;
		}
		/* Set up for the next sector or cluster */
		if (words_in_buffer == 0 && words_left)
		{
			sector_offset_in_cluster += 1;
			if (sector_offset_in_cluster >= pdr->drive_info.secpalloc)
			{
				sector_offset_in_cluster = 0;
				/* Release the system sector buffer and get the next cluster if we have to */
				pc_free_sys_sector(buf);
				buf = 0;
				/* Advance to the next cluster */
				{
				int error;
				dword next_cluster;
					next_cluster = 0;
					if (cluster < EXFATBADCLUSTER)
					{
						next_cluster = preboot_pcclnext(pdr, cluster, &error);
						if (error)
							next_cluster = 0;
					}
					if (next_cluster == 0)
					{
						rtfs_set_errno(PEINVALIDCLUSTER, __FILE__, __LINE__);
						goto done;
					}
					cluster = next_cluster;
				}
			}
		}
	}
    ret_val = TRUE;
done:
   	if (buf)
   		pc_free_sys_sector(buf);
    return(ret_val);
}

#if (DO_DUMP_UCASE_TABLE)
static void 	dump_expanded_upCaseTable(DDRIVE *pdr)
{
int i;
word *pw,range_baseline;
	pw = (word *) PDRTOEXFSTR(pdr)->UpCaseBufferCore;

	for (range_baseline=0, i = 0; i < 65536; i += 8, range_baseline += 8, pw += 8)
	{
		printf("/*[%4.4x]*/ 0x%4.4x, 0x%4.4x, 0x%4.4x , 0x%4.4x , 0x%4.4x , 0x%4.4x , 0x%4.4x , 0x%4.4x, \n", /* Utility, normally off */
			range_baseline,
			*(pw+ 0),
			*(pw+ 1),
			*(pw+ 2),
			*(pw+ 3),
			*(pw+ 4),
			*(pw+ 5),
			*(pw+ 6),
			*(pw+ 7));
	}
}
#endif
