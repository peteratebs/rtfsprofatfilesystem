/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* PREFICOM.C - Contains internal 32 bit enhanced file IO source code
   shared by 32 and 64 bit apis
*/

#include "rtfs.h"


dword truncate_32_count(dword file_pointer, dword count);
dword truncate_32_sum(dword val1, dword val2);

#if (INCLUDE_FAILSAFE_CODE)
#if (INCLUDE_TRANSACTION_FILES)
/* Prototypes of internal function implemented in prfstrio.c */
BOOLEAN _pc_efiliocom_overwrite(PC_FILE *pefile, FINODE *peffinode, byte *pdata, dword n_bytes);
BOOLEAN _pc_check_transaction_buffer(PC_FILE *pefile, dword clusterno);
#endif
#endif

void _pc_efiliocom_sync_current_fragment(PC_FILE *pefile,FINODE *peffinode)
{
    /* syncronize the file pointer with the finode */
    if (!peffinode)
    {
        pefile->fc.plus.pcurrent_fragment = 0;
        pefile->fc.plus.region_block_base = 0;
#if (INCLUDE_EXFATORFAT64)
		pefile->fc.plus.region_byte_base.val64 = 0;
#endif
		pefile->fc.plus.region_byte_base.val32 = 0;

    }
    else if (!pefile->fc.plus.pcurrent_fragment && peffinode->e.x->pfirst_fragment)
    {
        pefile->fc.plus.pcurrent_fragment = peffinode->e.x->pfirst_fragment;
        pefile->fc.plus.region_block_base = pc_cl2sector(pefile->pobj->pdrive, peffinode->e.x->pfirst_fragment->start_location);
#if (INCLUDE_EXFATORFAT64)
		pefile->fc.plus.region_byte_base.val64 = 0;
#endif
		pefile->fc.plus.region_byte_base.val32 = 0;

    }
}



void _pc_efiliocom_reset_seek(PC_FILE *pefile,FINODE *peffinode)
{
    pefile->fc.plus.pcurrent_fragment = peffinode->e.x->pfirst_fragment;
    peffinode->e.x->plast_fragment = pc_end_fragment_chain(peffinode->e.x->pfirst_fragment);
    if (pefile->fc.plus.pcurrent_fragment)
        pefile->fc.plus.region_block_base = pc_cl2sector(peffinode->my_drive, pefile->fc.plus.pcurrent_fragment->start_location);
    else
        pefile->fc.plus.region_block_base = 0;
#if (INCLUDE_EXFATORFAT64)
	pefile->fc.plus.region_byte_base.val64 = 0;
	pefile->fc.plus.file_pointer.val64 = 0;
#endif
	pefile->fc.plus.region_byte_base.val32 = 0;
    pefile->fc.plus.file_pointer.val32 = 0;

}


BOOLEAN _pc_efiliocom_lseek(PC_FILE *pefile,FINODE *peffinode, dword offset_hi, dword offset_lo, int origin,dword *pnewoffset_hi, dword *pnewoffset)
{
llword zero,offset,start_pointer,dstart,new_file_pointer,region_byte_base,region_byte_next;
dword _temp;
REGION_FRAGMENT *pcurrent_fragment;
DDRIVE *pdr;
#if (INCLUDE_EXFATORFAT64)
	zero.val64=0;
#endif
	zero.val32=0;

	if (!pnewoffset_hi)
		pnewoffset_hi=&_temp;
    pdr = peffinode->my_drive;
#if (INCLUDE_EXFATORFAT64)
	if (ISEXFATORFAT64(pdr))
	{
		offset.val64=(((ddword)offset_hi)<<32)|(ddword)offset_lo;
		*pnewoffset_hi = (dword) ((pefile->fc.plus.file_pointer.val64>>32)&0xffffffff);
		*pnewoffset = (dword) (pefile->fc.plus.file_pointer.val64&0xffffffff);
		dstart.val64=(ddword)peffinode->extended_attribute_dstart;
	}
	else
#endif
	{
		offset.val32=offset_lo;
		*pnewoffset_hi=0;
		*pnewoffset = pefile->fc.plus.file_pointer.val32;
		dstart.val32=peffinode->extended_attribute_dstart;
	}
    /* syncronize the file pointer with the finode */
    _pc_efiliocom_sync_current_fragment(pefile,peffinode);


    if (origin == PSEEK_SET)        /*  offset from begining of data */
	{
        start_pointer=dstart;
	}
    else if (origin == PSEEK_SET_RAW)/*  offset from begining of file */
    {
        start_pointer=zero;
        dstart = zero;
    }
    else if (origin == PSEEK_CUR)   /* offset from current file pointer */
	{
         start_pointer=pefile->fc.plus.file_pointer;
	}
    else if (origin == PSEEK_CUR_NEG)  /* negative offset from current file pointer */
        start_pointer = pefile->fc.plus.file_pointer;
    else if (origin == PSEEK_END)   /*  offset from end of file */
    {
#if (INCLUDE_EXFATORFAT64)
		if (ISEXFATORFAT64(pdr))
		{
			if (peffinode->fsizeu.fsize64)
				start_pointer.val64 = peffinode->fsizeu.fsize64;
			else
			{
				start_pointer=zero;
			}
		}
		else
#endif
		{
			if (peffinode->fsizeu.fsize)
				start_pointer.val32 = peffinode->fsizeu.fsize;
			else
			{
				start_pointer=zero;
			}
		}
    }
    else
    {
        rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
        return(FALSE);
    }

    if (origin == PSEEK_CUR_NEG || origin == PSEEK_END)
    {
#if (INCLUDE_EXFATORFAT64)
		if (ISEXFATORFAT64(pdr))
		{
			new_file_pointer.val64 = start_pointer.val64-offset.val64;
			if (new_file_pointer.val64 > start_pointer.val64 || /* wrapped around */
				new_file_pointer.val64 < dstart.val64)
            new_file_pointer = dstart; /* truncate to start */
		}
		else
#endif
		{
			new_file_pointer.val32 = start_pointer.val32-offset.val32;
			if (new_file_pointer.val32 > start_pointer.val32 || /* wrapped around */
				new_file_pointer.val32 < dstart.val32)
            new_file_pointer = dstart; /* truncate to start */
		}
	}
    else
    {
#if (INCLUDE_EXFATORFAT64)
		if (ISEXFATORFAT64(pdr))
		{
			new_file_pointer.val64 = start_pointer.val64+offset.val64;
			if (new_file_pointer.val64 < start_pointer.val64 || /* wrapped around */
				new_file_pointer.val64 > peffinode->fsizeu.fsize64)
				new_file_pointer.val64 = peffinode->fsizeu.fsize64; /* truncate to end */
		}
		else
#endif
		{
			new_file_pointer.val32 = start_pointer.val32+offset.val32;
			if (new_file_pointer.val32 < start_pointer.val32 || /* wrapped around */
				new_file_pointer.val32 > peffinode->fsizeu.fsize)
				new_file_pointer.val32 = peffinode->fsizeu.fsize; /* truncate to end */
		}
    }

    if (peffinode->operating_flags & FIOP_LOAD_AS_NEEDED)
    { /* Make sure the fragment chains (if there are any) for the region containing the file pointer are loaded.. */
#if (INCLUDE_EXFATORFAT64)
		if ((ISEXFATORFAT64(pdr) && new_file_pointer.val64 )|| (!ISEXFATORFAT64(pdr) && new_file_pointer.val32)) /* Don't pass zero, that resets load which we do not want */
#else
        if (new_file_pointer.val32) /* Don't pass zero, that resets load which we do not want */
#endif
        {
            if (!load_efinode_fragments_until(peffinode, new_file_pointer))
                return(FALSE); /* load_efinode_fragments_until has set errno */
        }
    }

    /* If we seeked to an extent that is not allocated or into
       the extended attribute clusters (if there are any, this it is an error */
#if (INCLUDE_EXFATORFAT64)
	if (ISEXFATORFAT64(pdr))
	{
		if (new_file_pointer.val64 < dstart.val64 ||new_file_pointer.val64 > peffinode->e.x->alloced_size_bytes.val64)
		{ /* Illegal condition size > alloced_size */
			rtfs_set_errno(PEINVALIDCLUSTER, __FILE__, __LINE__);
			return(FALSE);
		}
	}
	else
#endif
	if (new_file_pointer.val32 < dstart.val32 ||new_file_pointer.val32 > peffinode->e.x->alloced_size_bytes.val32)
	{ /* Illegal condition size > alloced_size */
        rtfs_set_errno(PEINVALIDCLUSTER, __FILE__, __LINE__);
        return(FALSE);
    }
#if (INCLUDE_EXFATORFAT64)
	if ((ISEXFATORFAT64(pdr)&&new_file_pointer.val64 < pefile->fc.plus.region_byte_base.val64)||(!ISEXFATORFAT64(pdr)&&new_file_pointer.val32 < pefile->fc.plus.region_byte_base.val32))
#else
	if (new_file_pointer.val32 < pefile->fc.plus.region_byte_base.val32)
#endif
    { /* look from the beginning */
        pcurrent_fragment = peffinode->e.x->pfirst_fragment;
        region_byte_base = zero;
    }
    else
    {
        pcurrent_fragment = pefile->fc.plus.pcurrent_fragment;
        region_byte_base  = pefile->fc.plus.region_byte_base;
    }
    if (!pcurrent_fragment)
    { /* File is empty. zero is the only valid value */
#if (INCLUDE_EXFATORFAT64)
		if ((ISEXFATORFAT64(pdr) && new_file_pointer.val64 )|| (!ISEXFATORFAT64(pdr) && new_file_pointer.val32))
#else
        if (new_file_pointer.val32)
#endif
        {
            rtfs_set_errno(PEINVALIDCLUSTER, __FILE__, __LINE__);
            return(FALSE);
        }
        else
            return(TRUE);
    }
#if (INCLUDE_EXFATORFAT64)
	if (ISEXFATORFAT64(pdr))
		region_byte_next.val64 = region_byte_base.val64+pc_fragment_size_64(pdr, pcurrent_fragment);
    else
#endif
		region_byte_next.val32 = truncate_32_sum(region_byte_base.val32, pc_fragment_size_32(pdr, pcurrent_fragment));
#if (INCLUDE_EXFATORFAT64)
	if ((ISEXFATORFAT64(pdr)&&(new_file_pointer.val64 == peffinode->e.x->alloced_size_bytes.val64)) || (!ISEXFATORFAT64(pdr)&&(new_file_pointer.val32 == peffinode->e.x->alloced_size_bytes.val32)))
#else
	if (new_file_pointer.val32 == peffinode->e.x->alloced_size_bytes.val32)
#endif
    { /* leave it at the end region */
        while (pcurrent_fragment->pnext)
        {
#if (INCLUDE_EXFATORFAT64)
			if (!ISEXFATORFAT64(pdr))
#endif
			{
				if (region_byte_next.val32 == LARGEST_DWORD)
				{ /* if we numerically wrapped and there's more force an error */
					rtfs_set_errno(PEINVALIDCLUSTER, __FILE__, __LINE__);
					return(FALSE);
				}
			}
            pcurrent_fragment = pcurrent_fragment->pnext;
            region_byte_base =  region_byte_next;
#if (INCLUDE_EXFATORFAT64)
	        if (ISEXFATORFAT64(pdr))
				region_byte_next.val64 = region_byte_base.val64+pc_fragment_size_64(pdr, pcurrent_fragment);
            else
#endif
				region_byte_next.val32 = truncate_32_sum(region_byte_base.val32,pc_fragment_size_32(pdr, pcurrent_fragment));
        }
#if (INCLUDE_EXFATORFAT64)
		if ((ISEXFATORFAT64(pdr)&&(region_byte_next.val64 != new_file_pointer.val64)) || (!ISEXFATORFAT64(pdr)&&(region_byte_next.val32 != new_file_pointer.val32)))
#else
        if (region_byte_next.val32 != new_file_pointer.val32)
#endif
        {
            rtfs_set_errno(PEINVALIDCLUSTER, __FILE__, __LINE__);
            return(FALSE);
        }
    }
    else
    { /* find the frag in which we reside */
        while (pcurrent_fragment)
        {
#if (INCLUDE_EXFATORFAT64)
			if (ISEXFATORFAT64(pdr))
			{
				if ((new_file_pointer.val64 >= region_byte_base.val64) && (new_file_pointer.val64 < region_byte_next.val64) )
					break;
			}
			else
#endif
			{
				if ((new_file_pointer.val32 >= region_byte_base.val32) && (new_file_pointer.val32 < region_byte_next.val32) )
					break;
				if (region_byte_next.val32 == LARGEST_DWORD)
					 break;
			}
            pcurrent_fragment = pcurrent_fragment->pnext;
            if (!pcurrent_fragment)
                break;
            region_byte_base =  region_byte_next;
#if (INCLUDE_EXFATORFAT64)
	        if (ISEXFATORFAT64(pdr))
				region_byte_next.val64 = region_byte_base.val64+pc_fragment_size_64(pdr, pcurrent_fragment);
            else
#endif
				region_byte_next.val32 = truncate_32_sum(region_byte_base.val32,
												pc_fragment_size_32(pdr, pcurrent_fragment));
        }
    }
    if (!pcurrent_fragment)
    {
         rtfs_set_errno(PEINVALIDCLUSTER, __FILE__, __LINE__);
         return(FALSE);
    }
    else
    {
        pefile->fc.plus.pcurrent_fragment =  pcurrent_fragment;
        pefile->fc.plus.region_byte_base  =  region_byte_base;
        pefile->fc.plus.region_block_base = pc_cl2sector(pdr, pcurrent_fragment->start_location);
        pefile->fc.plus.file_pointer      = new_file_pointer;
        /* Return the logical offset excluding extended attributes */
#if (INCLUDE_EXFATORFAT64)
	    if (ISEXFATORFAT64(pdr))
		{
			new_file_pointer.val64 = new_file_pointer.val64 - dstart.val64;
			*pnewoffset_hi = (dword) ((new_file_pointer.val64>>32)&0xffffffff);
			*pnewoffset = (dword) (new_file_pointer.val64&0xffffffff);
		}
		else
#endif
		{
			new_file_pointer.val32 = new_file_pointer.val32 - dstart.val32;
			*pnewoffset_hi = 0;
			*pnewoffset = new_file_pointer.val32;
		}
    }

    return(TRUE);
}


/* Read or write a section of a file
   If pdata is zero everything is done but transfering the data.
   This is used to conveniently expand a file without actually
   performing any data transfer.
*/
BOOLEAN _pc_efiliocom_io(PC_FILE *pefile,FINODE *peffinode, byte *pdata, dword n_bytes, BOOLEAN reading, BOOLEAN appending)
{
dword file_region_block_base,n_left,n_todo;
llword  file_pointer,region_byte_next,new_file_pointer,file_region_byte_base;
int at_eor;
REGION_FRAGMENT *file_pcurrent_fragment;
DDRIVE *pdrive;

    if (!n_bytes)
        return(TRUE);
    pdrive = peffinode->my_drive;

#if (INCLUDE_FAILSAFE_CODE && INCLUDE_TRANSACTION_FILES)
    if (!reading && !appending && pdata && pefile->fc.plus.allocation_policy & PCE_TRANSACTION_FILE)
    {
        llword ltemp;
        /* If writing, and the file already contains content and there is data
           to write (not just moving the dile pointer, and it's a transcation
           file call the overwrite routine */
        if (pefile->fc.plus.file_pointer < peffinode->e.x->alloced_size_bytes)
        {
            ltemp = peffinode->e.x->alloced_size_bytes - pefile->fc.plus.file_pointer;
            if (ltemp > n_bytes)
                ltemp = n_bytes;
            if (!_pc_efiliocom_overwrite(pefile,peffinode, pdata, ltemp))
                return(FALSE);
            pdata += ltemp;
            n_bytes -= ltemp;
            if (!n_bytes)
                return(TRUE);
        }
    }
#else
    RTFS_ARGSUSED_INT((int) appending); /* quiet compilers that complain about unused argument */
#endif

    /* use local copies of file info so if we fail the pointers will not advance */
    file_pointer = pefile->fc.plus.file_pointer;
    file_pcurrent_fragment = pefile->fc.plus.pcurrent_fragment;
    file_region_byte_base = pefile->fc.plus.region_byte_base;
    file_region_block_base= pefile->fc.plus.region_block_base;
#if (INCLUDE_EXFATORFAT64)
	if (ISEXFATORFAT64(pdrive))
	    n_left = n_bytes;
    else
#endif
		n_left = truncate_32_count(file_pointer.val32,n_bytes);
    if (peffinode->operating_flags & FIOP_LOAD_AS_NEEDED)
    { /* Make sure the fragment chains (if there are any) for the region are loaded..
         this works for both read and write, if appending to the end */
#if (INCLUDE_EXFATORFAT64)
		if (ISEXFATORFAT64(pdrive))
        {
            llword new_offset;
			new_offset.val64 = file_pointer.val64 + n_left -1;
			if (new_offset.val64)
            {
                if (!load_efinode_fragments_until(peffinode, new_offset))
                    return(FALSE); /* load_efinode_fragments_until has set errno */
            }
        }
		else
#endif
        if (n_left)
        {
            llword new_offset;
			new_offset.val32 = file_pointer.val32 + n_left -1;
			if (new_offset.val32)
            {
                if (!load_efinode_fragments_until(peffinode, new_offset))
                    return(FALSE); /* load_efinode_fragments_until has set errno */
            }
        }
    }
#if (INCLUDE_EXFATORFAT64)
	if (ISEXFATORFAT64(pdrive))
		region_byte_next.val64    = file_region_byte_base.val64+pc_fragment_size_64(pdrive, file_pcurrent_fragment);
    else
#endif
		region_byte_next.val32    = truncate_32_sum(file_region_byte_base.val32,
										pc_fragment_size_32(pdrive, file_pcurrent_fragment));
	while (n_left)
	{
        at_eor = 0;
		dword file_pointerlo;
#if (INCLUDE_EXFATORFAT64)
	if (ISEXFATORFAT64(pdrive))
		file_pointerlo=file_pointer.val64&0xffffffff;
    else
#endif
		file_pointerlo=file_pointer.val32;
        if (file_pointerlo & pdrive->drive_info.bytemasksec)
        {
            n_todo = pdrive->drive_info.bytespsector - (file_pointerlo & pdrive->drive_info.bytemasksec);
            if (n_todo > n_left)
                n_todo = n_left;
        }
        else
        {
            n_todo = n_left & ~pdrive->drive_info.bytemasksec;
            if (!n_todo)
                n_todo = n_left;
        }
#if (INCLUDE_EXFATORFAT64)
		if (ISEXFATORFAT64(pdrive))
			new_file_pointer.val64 = file_pointer.val64 + n_todo;
		else
#endif
			new_file_pointer.val32 = file_pointer.val32 + n_todo;
#if (INCLUDE_EXFATORFAT64)
		if (ISEXFATORFAT64(pdrive))
		{
			if (new_file_pointer.val64 >= region_byte_next.val64)
			{
				new_file_pointer = region_byte_next;
				n_todo = (dword)(new_file_pointer.val64 - file_pointer.val64);
				at_eor = 1;
			}
		}
		else
#endif
		{
			if (new_file_pointer.val32 >= region_byte_next.val32)
			{
				new_file_pointer = region_byte_next;
				n_todo = new_file_pointer.val32 - file_pointer.val32;
				if (region_byte_next.val32 != LARGEST_DWORD)
					at_eor = 1;
			}
		}
        if (pdata)
        {
        dword start_sector, start_byte_offset;
        BOOLEAN process_io;
#if (INCLUDE_EXFATORFAT64)
		if (ISEXFATORFAT64(pdrive))
		{
            /* Calculate the starting sector and byte offset into that sector of the file pointer  */
			start_sector = file_region_block_base + (dword) ((file_pointer.val64 - file_region_byte_base.val64)>>pdrive->drive_info.log2_bytespsec);
			start_byte_offset = ((dword)file_pointer.val64 & pdrive->drive_info.bytemasksec);
		}
		else
#endif
		{
            /* Calculate the starting sector and byte offset into that sector of the file pointer  */
			start_sector = file_region_block_base + ((file_pointer.val32-file_region_byte_base.val32)>>pdrive->drive_info.log2_bytespsec);
			start_byte_offset = (file_pointer.val32 & pdrive->drive_info.bytemasksec);
		}
            /* Use a separate test for transaction buffer from the rest of IO */
            /* Process the IO unless processed from the transaction file buffer */
            process_io = TRUE;
#if (INCLUDE_FAILSAFE_CODE)
#if (INCLUDE_TRANSACTION_FILES)
            if (reading)
            {
            dword dwbytespsector;
                dwbytespsector = (dword) pdrive->drive_info.bytespsector;
                if (n_todo < dwbytespsector)
                {
                    /* If reading a transaction file use the transaction buffer
                       if it's already pointed at our cluster */
                    if ((pefile->fc.plus.allocation_policy & PCE_TRANSACTION_FILE) &&
                        _pc_check_transaction_buffer(pefile, pc_sec2cluster(pdrive,start_sector)))
                    { /* Only possble on read beacause writes go through
                         overwrite */
                    dword start_offset;
                        start_offset = (start_sector - pefile->fc.plus.current_transaction_blockno) << pdrive->drive_info.log2_bytespsec;
                        start_offset += start_byte_offset;
                        copybuff(pdata, pefile->fc.plus.transaction_buffer+start_offset, (int)n_todo);
                        process_io = FALSE;
                    }
                }
            }
#endif
#endif
            /* Call pc_buffered_fileio() which buffers the data if necessary  */
            if (process_io)
            {
                if (!pc_buffered_fileio(peffinode, start_sector, start_byte_offset, (dword) n_todo, pdata,  reading, appending))
                    return(FALSE);
            }
            pdata += n_todo;
        } /* if(pdata) */
        file_pointer = new_file_pointer;
        n_left -= n_todo;

        if (at_eor)
        {
        REGION_FRAGMENT *pf;
            pf = file_pcurrent_fragment->pnext;
            if (pf)
            {
                file_pcurrent_fragment      = pf;
                file_region_byte_base       = region_byte_next;
#if (INCLUDE_EXFATORFAT64)
	            if (ISEXFATORFAT64(pdrive))
					region_byte_next.val64            = file_region_byte_base.val64+pc_fragment_size_64(pdrive, pf);
                else
#endif
					region_byte_next.val32            = truncate_32_sum(file_region_byte_base.val32, pc_fragment_size_32(pdrive, pf));
                file_region_block_base      = pc_cl2sector(pdrive, pf->start_location);
            }
            else if (n_left)
            {

                rtfs_set_errno(PEINVALIDCLUSTER, __FILE__, __LINE__);
                return(FALSE);
            }
        }
    }
    /* everything is good so update the file structure */
    pefile->fc.plus.file_pointer      =    file_pointer;
    pefile->fc.plus.pcurrent_fragment =    file_pcurrent_fragment;
    pefile->fc.plus.region_byte_base  =    file_region_byte_base;
    pefile->fc.plus.region_block_base =    file_region_block_base;
    return(TRUE);
}

/* Truncate 32 bit file write counts to fit inside 4 Gig size limit */
dword truncate_32_count(dword file_pointer, dword count)
{
dword max_count;

    max_count = LARGEST_DWORD - file_pointer;
    if (count > max_count)
        count = max_count;
    return(count);
}
