/*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* PRAPIEXT.C - Contains user api level circular file extract function
    pc_cfilio_extract()  -  Extract data from the circular buffer to a file
*/
#include "rtfs.h"
#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */

#if (INCLUDE_CIRCULAR_FILES)

/*
. Allocate a new file dataoffset + extract_len bytes long
. seek soure file to nearest cluster boundary
. Create copy/extract list (by cluster)
. make sure both source and dest region lists are cluster bounded
. do copies
. purge remap region
. do linking
. set remap region
*/
#define DEBUG_EXTRACT 0
/*DBEXTRACT*/ void DEBUG_dump32_chain(PC_FILE *pefile);

#if (DEBUG_EXTRACT)
#include <stdio.h>
void DEBUG_disp_file_chain(char *prompt, PC_FILE *pefile);
void DEBUG_disp_remaps(char *prompt,PC_FILE *preaderefile);
/*DBEXTRACT*/#define DEBUG_PRINTF_64(A,B) printf("%s %d:%d", A, M64HIGHDW(B),M64LOWDW(B))
/*DBEXTRACT*/#define DEBUG_PRINTF printf
/*DBEXTRACT*/ void DEBUG_log_progress(PC_FILE *pefile, dword caller, dword op, dword length_segment);
#define DEBUG_LOG_PROGRESS(A,B,C,D) DEBUG_log_progress(A,B,C,D);
#define DEBUG_SHOW_PROGRESS() DEBUG_show_progress();


/*DBEXTRACT*/ void DEBUG_show_progress(void);
#else
#define DEBUG_LOG_PROGRESS(A,B,C,D)
#define DEBUG_SHOW_PROGRESS()
#endif


static BOOLEAN _pc_fill_extract_file(PC_FILE *plinearefile, ddword length_ddw);
static BOOLEAN _pc_cfilio_extract(PC_FILE *pwriterefile, PC_FILE *preaderefile, PC_FILE *plinearefile, byte *copy_buffer, dword copy_buffer_size, ddword length_ddw,byte *header_buffer, int header_size);
static BOOLEAN _pc_cfilio_is_mapped(PC_FILE *preaderefile,ddword file_pointer_ddw, dword length_section);
static void _pc_cfilio_extract_do_swap_section(PC_FILE *pefile1, PC_FILE *pefile2);
static BOOLEAN _pc_copy_bytes_from_file(PC_FILE *pefileto, PC_FILE *pcfilefrom, ddword nbytes, byte *copy_buffer, dword copy_buffer_size, BOOLEAN is_circ);

static REMAP_RECORD *pc_cfilio_remap_region_find(PC_FILE *preaderefile, ddword data_start_ddw, ddword data_length_ddw);
static BOOLEAN pc_cfilio_remap_region_assign(PC_FILE *preaderefile,
                        PC_FILE *premapfile,
                        ddword linear_file_offset_ddw,
                        ddword remap_fpoff_start_ddw,
                        ddword remap_fpoff_length_ddw);
static void pc_cfilio_remap_region_unmap(PC_FILE *preaderefile, REMAP_RECORD *premap);
static REMAP_RECORD *pc_cfilio_remap_region_alloc(PC_FILE *preaderefile);
static REMAP_RECORD *pc_cfilio_remap_region_find(PC_FILE *preaderefile, ddword data_start_ddw, ddword data_length_ddw);
static void pc_cfilio_remap_region_free(PC_FILE *preaderefile,REMAP_RECORD *premap);
static REMAP_RECORD *pc_cfilio_unlink_remap_record(REMAP_RECORD *premove,REMAP_RECORD *plist);
static REMAP_RECORD *pc_cfilio_remap_find_region_by_file(PC_FILE *preaderefile, PC_FILE *premapfile);

static BOOLEAN _reader_ftell(PC_FILE *pwriterefile, ddword *psaved_read_pointer_ddw);
static BOOLEAN _writer_ftell(PC_FILE *pwriterefile, ddword *psaved_write_pointer_ddw);
static BOOLEAN _reader_frestore(PC_FILE *pwriterefile, ddword saved_read_pointer_ddw);
static BOOLEAN _writer_frestore(PC_FILE *pwriterefile, ddword saved_read_pointer_ddw);
static BOOLEAN _pc_advance_reader(PC_FILE *preaderefile, ddword length_ddw);
static BOOLEAN _pc_advance_file(PC_FILE *pefile, ddword length_ddw);
static BOOLEAN _pc_retard_file(PC_FILE *pefile, ddword length_ddw);
static BOOLEAN _pc_efilio_split_fraglist_at_fp(PC_FILE *pefile);

#define REGION_ENDLESS 0x10000000 /* Endless loop guard */


#if (INCLUDE_EXFATORFAT64)
dword pc_byte2clmod64(DDRIVE *pdr, dword nbytes_hi, dword nbytes_lo);
dword pc_byte2cloff64(DDRIVE *pdr, ddword nbytes);
ddword pc_byte2cloffbytes64(DDRIVE *pdr, ddword nbytes);
#endif
/****************************************************************************
pc_cfilio_extract -  Extract a region from a circular file to a linear
                     extract file.

  Summary
    BOOLEAN pc_cfilio_extract(circ_fd, linear_fd, length_hi, length_lo)

        int circ_fd - A file descriptor that was returned from a succesful
        call to pc_Cfilio_open.

        int linear_fd - A file descriptor that was returned from a succesful
        call to pc_efilio_open. The file must have been opened with the
        PCE_REMAP_FILE allocation attribute.

        dword length_hi
        dword length_lo - length_hi:length_lo is the 64 bit length of
        data to extract from the logical read pointer foreward in the
        circular buffer.

    Note: In addition to the supplied arguments pc_cfilio_extract()
    requires the device to have user buffer space assigned to it to be
    used as a scratch buffer. If pc_cfilio_extract() is forced to copy
    blocks it double buffers them through the user buffer space. The larger
    the user buffer space is the fewer indidual seek/read and seek/write
    operations are required.

    If no user buffer space has been provided,pc_cfilio_extract() will fail,
    returning FALSE and setting errno to PEINVALIDPARMS.

    See the documentation for pc_efilio_setbuff() for instructions on
    providing ERTFS with additional buffering.


  Description

    This function unlinks the cluster chains in the range bounded by the
    current read stream pointer and the current read stream pointer plus
    lenght_hi:length_lo. It moves these clusters to the linear file and
    allocates new clusters from freespace and links these replacement
    clusters into the circular file where the extracted file was removed
    from.

    If the extracted region does not begin on a cluster boundary the
    extracted file will be assigned a start offset equal to the offset
    into the first cluster and the file size will be this offset plus the
    size of the extracted data. The data start extended attribute is


    The size of the extract file will be the size of the extracted data plus

    Under most circumstances clusters are linked and not copied. But
    sometimes clusters must be copied.

        If the beginning of the extract region does not lie on a
        cluster boundary that cluster must be copied from the circular
        file to the extract file.

        If the end of the extract region does not lie on a cluster boundary
        that cluster must be copied from the circular file to the extract
        file.

        If the extract region spans previously extracted and remapped
        regions then these overlapping blocks are copied and not
        relinked.

    After pc_cfilio_extract() completes the extracted region is remapped
    in the circular file. What this means is that future reads from the
    circular file in this region will return bytes from the extract file.
    Future writes to this region will be written, not to the extract file,
    but to replacement clusters in the circular file. If an area of the
    remap region is overwritten the remap region is split or shrunk so
    future reads from that area will be from the circular file and not
    the remap file.

    When the linear extract file is closed the remap region is removed
    from the circular file. In this event the contents of the region
    may be read but the data will be uninitialized until the region is
    overwritten.

    Note the following preconditions for using pc_cfilio_extract().

    . The linear extract file  The file must have been opened with
    pc_efilio_open usng the PCE_REMAP_FILE allocation attribute.
    It must be an empty file.

    . pc_cfilio_extract() requires at least one block of user supplied
    buffering for copying clusters that must be copied instead of
    linked. The amount of buffering must be at least one block but
    it is a good idea for the user buffer size to be large enough to
    hold at least one cluster. If larger regions are being copied
    because of overlapping extract regions then even larger buffer
    will improve performance by reducing seeks.


    . When the circular file was opened pc_cfilio_open() must have been
    provided with enough remap structures to hold this remap extract
    region, and all other active remap regions.

    The number of remap regions present depends on the number of
    remap regions that have been created by calls to pc_cfilio_extract()
    and have not yet been purged. Remap regions are purged when the
    of the write stream pointer overwrites the region when a new
    remap region covers all of the current remap region or when the file
    descriptor of the extract file is with pc_efilio_close().

    A call to pc_cfilio_extract() will consume one remap record if it
    either does not overlap an existing region or if it intersects or
    overlaps an existing region on one side. If the new region resides
    within completely inside of another remap region then the existing
    region is split and two instead of one remap structure is consumed.
    If one the other hand the new remap region completely overlaps
    one or more existing reamap regions then those regions are released
    resulting in one or more remap records being made available again
    for reuse.

    Remap records are relatively small (around 32 bytes) so make sure
    enough of them are provided.

    See the description of pc_cfilio_open() for a description of
    the configuration values:
        int   n_remap_records;
          REMAP_RECORD *remap_records;


  Returns:
      TRUE if no errors were encountered. FALSE otherwise.

      *nread is set to the number of bytes successfully read.

  errno is set to one of the following
    0               - No error
    PEBADF          - Invalid file descriptor
    PECLOSED        - File is no longer available. Call pc_cfilio_close().
    PEINVALIDPARMS  - Bad or missing argument
    PEEFIOILLEGALFD - The file not open in circular IO mode.
    PEIOERRORREAD   - Read error
    An ERTFS system error
*****************************************************************************/

BOOLEAN pc_cfilio_extract(int circ_fd, int linear_fd, dword length_hi, dword length_lo,byte *header_buffer, int header_size)
{
PC_FILE *pwriterefile,*plinearefile,*preaderefile;
byte *copy_buffer,*copy_buffer_base;
dword copy_buffer_size;
BOOLEAN ret_val;
DDRIVE *pdr;

    CHECK_MEM(BOOLEAN,0) /* Make sure memory is initted */
    rtfs_clear_errno();    /* clear errno */
    /* return False if bad arguments   */
    if (!(length_hi || length_lo))
    {
        rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
        return(FALSE);
    }
    ret_val = FALSE;
    /* Prepare the cirecular file */
    pwriterefile = pc_cfilio_fd2file(circ_fd, TRUE); /* and unlock */
    if (!pwriterefile)
        return(FALSE); /* pc_fd2file set errno */

    preaderefile = pwriterefile->fc.plus.psibling;
    plinearefile = pc_fd2file(linear_fd, 0);
    if (!plinearefile)
        return(FALSE); /* pc_fd2file set errno */
    /* From now on return through return_locked */
    copy_buffer = copy_buffer_base = 0;
    pdr = preaderefile->pobj->pdrive;

    /* requires a user buffer assigned */
    copy_buffer_base = pc_claim_user_buffer(pdr, &copy_buffer_size, 0); /* released at cleanup */
    if (!copy_buffer_base)
        goto return_locked;

	{
		/* make sure we have enough buffering, the user buffer must be large enough for one file buffer plus at least one sector */
		dword file_buffer_needed;
		if (pdr->pmedia_info->eraseblock_size_sectors)
			file_buffer_needed = pdr->pmedia_info->eraseblock_size_sectors;
		else
			file_buffer_needed = 1;
        if (copy_buffer_size <= file_buffer_needed)
		{
        	rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
        	goto return_locked;
		}
		copy_buffer = 	copy_buffer_base + (file_buffer_needed * pdr->drive_info.bytespsector);
		copy_buffer_size -= file_buffer_needed;
	}
    if (!preaderefile ||
        plinearefile->fc.plus.ffinode->openflags & OF_BUFFERED ||
        !(plinearefile->fc.plus.allocation_policy & PCE_REMAP_FILE) )
    {
        rtfs_set_errno(PEEFIOILLEGALFD, __FILE__, __LINE__);
        goto return_locked;
    }
    /* Make sure the file contains enough data */
    if (M64GT(M64SET32(length_hi,length_lo), pc_cfilio_get_file_size_ddw(pwriterefile)))
    {
        rtfs_set_errno(PETOOLARGE, __FILE__, __LINE__);
        goto return_locked;
    }
    /* If the two files aren't on the same volume it's no good */
    if (pwriterefile->pobj->pdrive != plinearefile->pobj->pdrive)
    {
        rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
        goto return_locked;
    }
    /* Now do the extract */
    ret_val = _pc_cfilio_extract(pwriterefile, preaderefile, plinearefile, copy_buffer, copy_buffer_size, M64SET32(length_hi,length_lo),header_buffer, header_size);
return_locked:
    if (copy_buffer_base)
        pc_release_user_buffer(pwriterefile->pobj->pdrive, copy_buffer_base);
    if (!release_drive_mount_write(pwriterefile->pobj->pdrive->driveno))/* Release lock, unmount if aborted */
        return(FALSE);
    return(ret_val);
}



static BOOLEAN _pc_cfilio_extract(PC_FILE *pwriterefile,PC_FILE *preaderefile, PC_FILE *plinearefile, byte *copy_buffer, dword copy_buffer_size, ddword length_ddw,byte *header_buffer, int header_size)
{
DDRIVE *pdr;
dword  dataoffsetincluster,lin_metadata_bytes,extra_cluster_bytes;
dword  copyback_lead_clusters,cluster_size,ltemp, ltemp_hi;
dword   seekfp_hi,seekfp_lo,clboundfp_hi,clboundfp_lo,bytes_reader_retarded_by;
ddword  clboundfp_ddw,nleft_ddw,cluster_size_ddw,saved_logical_read_pointer_ddw,saved_logical_write_pointer_ddw,reader_linfp_ddw;
BOOLEAN swap_extra_cluster,has_extra_cluster,swap_first_cluster;

   if (!_reader_ftell(pwriterefile, &saved_logical_read_pointer_ddw))
        return(FALSE);
   if (!_writer_ftell(pwriterefile, &saved_logical_write_pointer_ddw))
        return(FALSE);

	/* Seek reader linear file to the nearest lower cluster boundary */
	if (!_pc_efilio_lseek(preaderefile,  0, 0, PSEEK_CUR, &seekfp_hi, &seekfp_lo))
		return FALSE;

	nleft_ddw=length_ddw;
	extra_cluster_bytes=0;
	has_extra_cluster = FALSE;
	swap_extra_cluster=FALSE;
	swap_first_cluster=FALSE;
	bytes_reader_retarded_by=0;
	/* Try to allocate new clusters as close to the circular file as possible */
    plinearefile->fc.plus.allocation_hint = _pc_efilio_first_cluster(preaderefile);

#if (DEBUG_EXTRACT)
/*DBEXTRACT*/    DEBUG_PRINTF_64("extract length ==  ",length_ddw);
/*DBEXTRACT*/    DEBUG_PRINTF_64("saved read  fp ", saved_logical_read_pointer_ddw);
/*DBEXTRACT*/    DEBUG_PRINTF_64("saved write fp ", saved_logical_write_pointer_ddw);
#endif

    pdr = preaderefile->fc.plus.ffinode->my_drive;
#if (INCLUDE_EXFATORFAT64)
	if (ISEXFATORFAT64(pdr))
	{
		dataoffsetincluster = (dword) (preaderefile->fc.plus.file_pointer.val64 - pc_byte2cloffbytes64(pdr, preaderefile->fc.plus.file_pointer.val64));
		reader_linfp_ddw=preaderefile->fc.plus.file_pointer.val64;
	}
	else
#endif
    {
        dataoffsetincluster = preaderefile->fc.plus.file_pointer.val32 - pc_byte2cloffbytes(pdr, preaderefile->fc.plus.file_pointer.val32);
		reader_linfp_ddw=M64SET32(0,preaderefile->fc.plus.file_pointer.val32);
    }
    cluster_size = (dword)pdr->drive_info.bytespcluster;
	cluster_size_ddw = M64SET32(0,cluster_size);

/* Calculate the total number of additional bytes to prepend for meta data
Update:
	lin_metadata_bytes = number of bytes required for metadata
	swap_first_cluster = true if we should swap the first data cluster
	has_extra_cluster  = true if we need an extra cluster to contain metadata
	swap_extra_cluster = true if we should swap in the extra cluster
	extra_cluster_bytes= cluster_size if has_extra_cluster is set
*/

	/* Calculate the seek pointer at the cluster boundary in the circular file*/
	clboundfp_ddw=M64MINUS32(pc_efilio_get_fp_ddw(preaderefile),dataoffsetincluster);
	clboundfp_lo=M64LOWDW(clboundfp_ddw);
	clboundfp_hi=M64HIGHDW(clboundfp_ddw);
	copyback_lead_clusters=0;

    if (header_size||dataoffsetincluster)
	{
        lin_metadata_bytes = header_size+RTFS_EXTATTRIBUTE_RECORD_SIZE;

		/* If there is not enough room before the data start to hold all metadata
		   Then expand the extract file by an additional cluster.
		   Decide if we can expand by swapping clusters */
		if (dataoffsetincluster<lin_metadata_bytes)
		{
			extra_cluster_bytes = cluster_size;
			has_extra_cluster=TRUE;
			/*
				Swap two leading clusters and copy  2 cluster back to the reader file if:
				We need an extra cluster for metadata
			    and there is a cluster preceeding the current cluster in the reader file
			    and the data from the start of the extract to the first cluster boundary is not mapped */
			if (M64GTEQ(clboundfp_ddw,cluster_size_ddw))
			{
				if (!_pc_cfilio_is_mapped(preaderefile,saved_logical_read_pointer_ddw,cluster_size-dataoffsetincluster))
				{
					swap_extra_cluster=TRUE;
					swap_first_cluster=TRUE;
					copyback_lead_clusters=2;
				}
			}
		}
		else
		{
			/* No extra clusters - Swap the first cluster if the data from the start of the extract to the first cluster boundary is not mapped */
			if (!_pc_cfilio_is_mapped(preaderefile,saved_logical_read_pointer_ddw,cluster_size-dataoffsetincluster))
			{
				copyback_lead_clusters=1;
				swap_first_cluster=TRUE;
			}
		}


	}
    else
        lin_metadata_bytes = 0;

/* Create a linear file with clusters to hold metadata and data */
    plinearefile->fc.plus.allocation_policy &= ~PCE_REMAP_FILE; /* Temporarilly clear the remap file attribute because we are growing the file */
    if (!_pc_fill_extract_file(plinearefile, pc_byte2ddwclmodbytes(pdr, M64PLUS32(length_ddw,dataoffsetincluster+extra_cluster_bytes))))
    {
        plinearefile->fc.plus.allocation_policy |= PCE_REMAP_FILE;
        return(FALSE);
    }
    plinearefile->fc.plus.allocation_policy |= PCE_REMAP_FILE;


/*
	nleft_ddw has the number of data bytes to extract

	if we are swapping the first cluster
		Add the data offset to the range
		Reduce the reader filepointer by data offset
	if we are padding
		if we are swapping
			Add a  cluster to the range
			Reduce the filepointer by a cluster
		otherwise
			start the linear file on a one cluster boundary
	Loop through the fragment chains until all bytes in the extract region are consumed.
		Copy bytes if the current file pointer is mapped to an extract file or if the read pointer is not cluster bound.
		Swap clusters if the current file pointer is not mapped to an extract file and is cluster bound.
		Copy the the first clusters and the last cluster back to the circular file if while swapping clusters we reached
		boundaries in the reader file and swapped anyway.
*/

	if (!_pc_efilio_lseek(plinearefile,  0, 0, PSEEK_SET, &ltemp_hi, &ltemp))
			return FALSE;

	nleft_ddw = length_ddw;
	if (dataoffsetincluster)
	{
		if (swap_first_cluster)
		{
			nleft_ddw = M64PLUS32(nleft_ddw, dataoffsetincluster);
			bytes_reader_retarded_by=dataoffsetincluster;
			if (!_pc_retard_file(preaderefile, M64SET32(0,dataoffsetincluster)))
				return FALSE;
		}
		else
		{
			if (!_pc_efilio_lseek(plinearefile,  0, dataoffsetincluster, PSEEK_CUR, &ltemp_hi, &ltemp))
				return FALSE;
		}
	}
	if (has_extra_cluster) /* We are padding */
	{
		if (swap_extra_cluster)
		{
			nleft_ddw = M64PLUS32(nleft_ddw, cluster_size);
			bytes_reader_retarded_by+=cluster_size;
			if (!_pc_retard_file(preaderefile, M64SET32(0,cluster_size)))
				return FALSE;
		}
		else
		{
			if (!_pc_efilio_lseek(plinearefile,  0, cluster_size, PSEEK_CUR, &ltemp_hi, &ltemp))
				return FALSE;
		}
	}

	while(M64NOTZERO(nleft_ddw))
	{
	ddword copy_bytes_ddw,bytes_to_region_ddw,byte_offset_in_region_ddw,bytes_in_region_ddw,read_file_pointer_ddw;

		/* Start by assuming all data is unmapped and can be swapped */
		bytes_to_region_ddw=nleft_ddw;
        byte_offset_in_region_ddw=M64SET32(0,0);
        bytes_in_region_ddw=M64SET32(0,0);
		if (!_reader_ftell(pwriterefile, &read_file_pointer_ddw))
			return FALSE;

		/* Check if the circular file pointer is in the cirular file clusters or if it is in a remap region */
		if (preaderefile->fc.plus.remapped_regions)
		{
			REMAP_RECORD *remap_record=0; /* returned from pc_cfilio_check_remap_read, not used here */
			pc_cfilio_check_remap_read(preaderefile,
                                read_file_pointer_ddw,
                                nleft_ddw,
                                &bytes_to_region_ddw,
                                &byte_offset_in_region_ddw,
                                &bytes_in_region_ddw,
                                &remap_record);
		}
		copy_bytes_ddw=M64SET32(0,0);
		if (!swap_first_cluster)
		{ /* If swap_first_cluster is true unaligned copies are managed through copybacks and swap has already been decided upon so don't calculate */
			if (M64ISZERO(bytes_to_region_ddw))
			{ /* We are inside a map region so we must perform a copy, bytes_in_region_ddw is already clipped by nleft_ddw */
				if (M64ISZERO(bytes_in_region_ddw))	/* Should not happen */
					{ERTFS_ASSERT(0);return FALSE;}
				copy_bytes_ddw=bytes_in_region_ddw;
			}
			else
			{
				/*	Check if we must perfrom a copy. */
				/* We are in the reader file's cluster chain.  We must copy if we are either not cluster bound or if bytes_in_region_ddw < cluster_size. */
				ltemp = M64LOWDW(read_file_pointer_ddw) & pdr->drive_info.byte_into_cl_mask;
				if (ltemp)
					copy_bytes_ddw=M64SET32(0,cluster_size-ltemp);
				else
				{ /* If we are less than a cluster from a remapped area we have to copy */
					if (M64GT(cluster_size_ddw,bytes_to_region_ddw))
						copy_bytes_ddw=bytes_to_region_ddw;
					//
					// HEREHERE We are copying if < cluster is left
					// we need to swap
//					if (M64GT(cluster_size_ddw,nleft_ddw))
//						copy_bytes_ddw=nleft_ddw;
				}
			}
		}
		if (M64NOTZERO(copy_bytes_ddw))
		{ /* Copy bytes from the reader file to the extract file */
			if (M64GT(copy_bytes_ddw,nleft_ddw))
				copy_bytes_ddw=nleft_ddw;

			if (!_pc_copy_bytes_from_file(plinearefile, preaderefile, copy_bytes_ddw, copy_buffer, copy_buffer_size,TRUE))
				return FALSE;
			nleft_ddw=M64MINUS(nleft_ddw,copy_bytes_ddw);
		}
		else
		{ /* Trim the fragments to be the same length, choosing the shorter length if they are not the same, and swap the fragments */
			dword circ_fraglen, lin_fraglen,lin_hi,lin_lo,circ_hi,circ_lo,i;
			dword did_copy=0;
			_pc_efilio_lseek(plinearefile,  0, 0, PSEEK_CUR, &lin_hi, &lin_lo);
			_pc_efilio_lseek(preaderefile,  0, 0, PSEEK_CUR, &circ_hi, &circ_lo);

			if (!_pc_efilio_split_fraglist_at_fp(plinearefile))
				return FALSE;
			if (!_pc_efilio_split_fraglist_at_fp(preaderefile))
				return FALSE;
#if (INCLUDE_EXFATORFAT64)
		if (ISEXFATORFAT64(pdr))
		{
			ERTFS_ASSERT(preaderefile->fc.plus.region_byte_base.val64==preaderefile->fc.plus.file_pointer.val64)
			;
		}
		else
#endif
		{
			ERTFS_ASSERT(preaderefile->fc.plus.region_byte_base.val32==preaderefile->fc.plus.file_pointer.val32)
			;
		}

			circ_fraglen = PC_FRAGMENT_SIZE_CLUSTERS(preaderefile->fc.plus.pcurrent_fragment);
			lin_fraglen  = PC_FRAGMENT_SIZE_CLUSTERS(plinearefile->fc.plus.pcurrent_fragment);

			/* The file pointer should be aligned with the base of the current fragment */

			/* Split fragments as needed so we can swap them */
			if (circ_fraglen>lin_fraglen)
			{
				/* Resize the current fragment of the circular file to the lesser lin_fraglen */
				if (!_pc_efiliocom_resize_current_fragment(preaderefile, preaderefile->fc.plus.ffinode, lin_fraglen))
					return(FALSE);
			}
			else if (lin_fraglen>circ_fraglen)
			{
				/* Resize the current fragment of the linear file to the lesser circ_fraglen */
				if (!_pc_efiliocom_resize_current_fragment(plinearefile, plinearefile->fc.plus.ffinode, circ_fraglen))
					return(FALSE);
			}

			/* The fragment lengths are the same, swap them */
			_pc_cfilio_extract_do_swap_section(plinearefile, preaderefile);

			 /* Re-seek after manipulating clusters */
			_pc_efilio_reset_seek(plinearefile);
			_pc_efilio_reset_seek(preaderefile);
			_pc_efilio_lseek(plinearefile,  lin_hi, lin_lo, PSEEK_SET, &ltemp_hi, &ltemp);
			_pc_efilio_lseek(preaderefile,  circ_hi, circ_lo, PSEEK_SET, &ltemp_hi, &ltemp);

			/*	Check if we have to copy bytes back from the extract file. */
			/* Check if copying the first and or second cluster required because we swapped but there is leading metadata */
			if (copyback_lead_clusters)
			{
			dword n = copyback_lead_clusters;
				if (n>PC_FRAGMENT_SIZE_CLUSTERS(plinearefile->fc.plus.pcurrent_fragment))
					n=PC_FRAGMENT_SIZE_CLUSTERS(plinearefile->fc.plus.pcurrent_fragment);
				if (!_pc_copy_bytes_from_file(preaderefile, plinearefile, n*cluster_size, copy_buffer, copy_buffer_size, FALSE))
					return FALSE;
				copyback_lead_clusters -= n;
				_pc_efilio_lseek(plinearefile,  lin_hi, lin_lo, PSEEK_SET, &ltemp_hi, &ltemp);
				_pc_efilio_lseek(preaderefile,  circ_hi, circ_lo, PSEEK_SET, &ltemp_hi, &ltemp);
				did_copy=n;
			}

			/*	If the length of the fragment we swapped is greater than bytes left, we are done but we have to copy the last cluster back to the circular file. */
			/* Use GTEQ and always copy back becuase we rounded up..
			   Copy back is not necessary on a boundary but do it always anyway
			*/
			if (M64GTEQ(pc_alloced_bytes_from_clusters_64(pdr, PC_FRAGMENT_SIZE_CLUSTERS(plinearefile->fc.plus.pcurrent_fragment)),nleft_ddw))
			{

				if (did_copy == PC_FRAGMENT_SIZE_CLUSTERS(plinearefile->fc.plus.pcurrent_fragment))
					; /* Leading and trailing clusters overlap */
				else
				{
					/* Seek Fragment_size-1 clusters in each file and copy from linear back to reader */
					for (i=1; i < PC_FRAGMENT_SIZE_CLUSTERS(plinearefile->fc.plus.pcurrent_fragment);i++)
					{
						if (!_pc_efilio_read(preaderefile,  0, cluster_size, &ltemp) || ltemp != cluster_size)
						{
							ERTFS_ASSERT(0)
							return FALSE;
						}
						if (!_pc_efilio_read(plinearefile,  0, cluster_size, &ltemp) || ltemp != cluster_size)
						{
							ERTFS_ASSERT(0)
							return FALSE;
						}
					}
					/* Now copy from the linear file back to the circular file  */
					if (!_pc_copy_bytes_from_file(preaderefile, plinearefile, cluster_size_ddw, copy_buffer, copy_buffer_size,FALSE))
						return FALSE;
				}
				/* We are past the end, set nleft_ddw to 0.. */
				nleft_ddw=M64SET32(0,0);
			}
			else
			{ /* We swapped a segment but there are more segments to come, advance file pointers and decrease nleft_ddw */
			ddword n_bytes;	/* The bytecount of the clusters we swapped */
				n_bytes = pc_alloced_bytes_from_clusters_64(pdr, PC_FRAGMENT_SIZE_CLUSTERS(plinearefile->fc.plus.pcurrent_fragment));
				/* Restore seekpointers and then advance by the amount swapped  */
				_pc_efilio_lseek(plinearefile,  lin_hi, lin_lo, PSEEK_SET, &ltemp_hi, &ltemp);
				if (!_pc_advance_file(plinearefile, n_bytes))
					return FALSE;
				/* We increased nbytes to include the whols section so decrease it now */
				nleft_ddw=M64MINUS(nleft_ddw,n_bytes);
				/* Put the reader file's liner file pointer back to it's origin */
				_pc_efilio_lseek(preaderefile,  circ_hi, circ_lo, PSEEK_SET, &ltemp_hi, &ltemp);
				/* Adjust if we moved the read pointer back to a cluster boundary before we swapped  */
				if (bytes_reader_retarded_by)
				{ /* Advance the linear fp by the amount we moved */
					if (!_pc_advance_file(preaderefile, bytes_reader_retarded_by))
						return FALSE;
					/* Reduce bytecount by the amount we moved */
					n_bytes=M64MINUS32(n_bytes,bytes_reader_retarded_by);
					bytes_reader_retarded_by=0;
				}
				swap_first_cluster=FALSE; /* Clear command to unilaterally swap the first clusters */
				if (!_pc_advance_reader(preaderefile, n_bytes))
					return FALSE;
			}
		}
	}
	/* The extraction is complete. Restore the circular file pointer */
    /* Now purge and remap length_ddw bytes at saved_logical_read_pointer_ddw in the circular file to offset datastart in the linear file */
    if (!pc_cfilio_remap_region_purge(preaderefile, saved_logical_read_pointer_ddw, length_ddw))
        return(FALSE);
    if (!pc_cfilio_remap_region_assign(preaderefile,
                        plinearefile,
                        M64SET32(0,dataoffsetincluster+extra_cluster_bytes),
                        saved_logical_read_pointer_ddw,
                        length_ddw))
        return(FALSE);

	{
	ddword lin_length_ddw;
		/* Set the data start and the file size. If dataoffsetincluster+extra_cluster_bytes is non zero it is written into the RTFS_EXTATTRIBUTE_RECORD in the metadata cluster */
		lin_length_ddw=M64PLUS32(length_ddw,dataoffsetincluster+extra_cluster_bytes);
		_pc_cfilio_set_file_size(plinearefile, dataoffsetincluster+extra_cluster_bytes, M64HIGHDW(lin_length_ddw),M64LOWDW(lin_length_ddw));
	}
    /* If a header was provided write it into the reserved area at the beginning of the file, just past the data offset record */
	if (header_buffer&&header_size)
	{
	dword ltemp;
		if (!_pc_efilio_lseek(plinearefile, 0, RTFS_EXTATTRIBUTE_RECORD_SIZE, PSEEK_SET_RAW, &ltemp_hi, &ltemp)||(ltemp!=RTFS_EXTATTRIBUTE_RECORD_SIZE))
			return(FALSE);
		if (!_pc_efilio_write(plinearefile, header_buffer, (dword)header_size, &ltemp) || ltemp != (dword)header_size)
			return(FALSE);
	}

   /* make sure the files get flushed and chains get linked */
   pc_set_file_dirty(pwriterefile, TRUE);
   pc_set_file_dirty(plinearefile, TRUE);
    /* Since we may have modified the fragment lists we rewind the file pointers
       coalesce chains and the restore */
   _pc_efilio_reset_seek(pwriterefile);
   _pc_efilio_coalesce_fragments(pwriterefile);
   if (!_writer_frestore(pwriterefile, saved_logical_write_pointer_ddw))
        return(FALSE);
   _pc_efilio_reset_seek(preaderefile);
   _pc_efilio_coalesce_fragments(preaderefile);
   if (!_reader_frestore(pwriterefile, saved_logical_read_pointer_ddw))
        return(FALSE);

   	_pc_efilio_reset_seek(plinearefile);
	_pc_efilio_coalesce_fragments(plinearefile);
	if (!_pc_efilio_lseek(plinearefile, 0, 0, PSEEK_SET, &ltemp_hi, &ltemp))
        return(FALSE);

    return(TRUE);
}


static BOOLEAN _pc_copy_bytes_from_file(PC_FILE *pefileto, PC_FILE *pcfilefrom, ddword nbytes, byte *copy_buffer, dword copy_buffer_size, BOOLEAN is_circ)
{
ddword nleft_ddw,buffer_size_ddw;
dword ltemp,buffer_size;

	buffer_size = copy_buffer_size * pefileto->pobj->pdrive->drive_info.bytespsector;
	buffer_size_ddw=M64SET32(0,buffer_size);
	nleft_ddw=nbytes;
	while(M64NOTZERO(nleft_ddw))
	{
		dword xfer_size;
		if (M64GT(nleft_ddw,buffer_size_ddw))
			xfer_size = buffer_size;
		else
			xfer_size = M64LOWDW(nleft_ddw);

		if (is_circ)
		{
			if (!_pc_cfilio_read(pcfilefrom, copy_buffer, xfer_size, &ltemp) || ltemp != xfer_size)
				return(FALSE);
		}
		else
		{
			if (!_pc_efilio_read(pcfilefrom, copy_buffer, xfer_size, &ltemp) || ltemp != xfer_size)
				return(FALSE);
		}
		/* Writing to the reader file using the linear file handle */
		nleft_ddw=M64MINUS32(nleft_ddw,xfer_size);
		if (!_pc_efilio_write(pefileto, copy_buffer, xfer_size, &ltemp) || ltemp != xfer_size)
			return(FALSE);
	}
	return TRUE;
}

/* Make sure the fragment list is split at the current file pointer */
static BOOLEAN _pc_efilio_split_fraglist_at_fp(PC_FILE *pefile)
{
ddword byteoffset_in_frag_ddw;
#if (INCLUDE_EXFATORFAT64)
	if (ISEXFATORFAT64(pefile->pobj->pdrive))
		byteoffset_in_frag_ddw=M64MINUS(pefile->fc.plus.file_pointer.val64,pefile->fc.plus.region_byte_base.val64);
	else
#endif
	byteoffset_in_frag_ddw=M64SET32(0,pefile->fc.plus.file_pointer.val32-pefile->fc.plus.region_byte_base.val32);

	if (M64NOTZERO(byteoffset_in_frag_ddw))
	{
		dword new_frag_size,new_sizebytes_hi,new_sizebytes_lo;
		dword save_hi,save_lo,ltemp_hi,ltemp_lo;

		/* The file pointer shoould be cluster alligned */
		ERTFS_ASSERT((M64LOWDW(byteoffset_in_frag_ddw)&pefile->pobj->pdrive->drive_info.byte_into_cl_mask)==0)

		if (!_pc_efilio_lseek(pefile,  0, 0, PSEEK_CUR, &save_hi, &save_lo))
			return FALSE;
		new_sizebytes_hi=M64HIGHDW(byteoffset_in_frag_ddw);
		new_sizebytes_lo= M64LOWDW(byteoffset_in_frag_ddw);
		if (!_pc_efilio_lseek(pefile,  new_sizebytes_hi, new_sizebytes_lo, PSEEK_CUR_NEG, &ltemp_hi, &ltemp_lo))
			return FALSE;
#if (INCLUDE_EXFATORFAT64)
	if (ISEXFATORFAT64(pefile->pobj->pdrive))
		new_frag_size=pc_byte2clmod64(pefile->pobj->pdrive, new_sizebytes_hi, new_sizebytes_lo);
	else
#endif
		new_frag_size=pc_byte2clmod(pefile->pobj->pdrive, new_sizebytes_lo);

		if (!_pc_efiliocom_resize_current_fragment(pefile, pefile->fc.plus.ffinode, new_frag_size))
			return(FALSE);
 		_pc_efilio_reset_seek(pefile);
		if (!_pc_efilio_lseek(pefile,  save_hi, save_lo, PSEEK_SET, &ltemp_hi, &ltemp_lo))
			return FALSE;
	}
	return TRUE;

}
static BOOLEAN _pc_advance_file(PC_FILE *pefile, ddword length_ddw)
{
    ddword ltemp_ddw;
	dword start_hi, start_lo,new_hi, new_lo,ltemp_hi,ltemp_lo;

	/* Get the current pointer and add length_ddw */
	if (!_pc_efilio_lseek(pefile,  0, 0, PSEEK_CUR, &start_hi, &start_lo))
		return FALSE;
	ltemp_ddw=M64SET32(start_hi, start_lo);
	ltemp_ddw= M64PLUS(ltemp_ddw,length_ddw);
	new_hi= M64HIGHDW(ltemp_ddw);
	new_lo= M64LOWDW(ltemp_ddw);
	/* Seek to the new location */
	if (!_pc_efilio_lseek(pefile,  new_hi, new_lo, PSEEK_SET_RAW, &ltemp_hi, &ltemp_lo) || (new_hi!=ltemp_hi) || (new_lo!=ltemp_lo))
		return FALSE;
     return TRUE;
}

static BOOLEAN _pc_retard_file(PC_FILE *pefile, ddword length_ddw)
{
    ddword ltemp_ddw;
	dword start_hi, start_lo,new_hi, new_lo,ltemp_hi,ltemp_lo;

	/* Get the current pointer and add length_ddw */
	if (!_pc_efilio_lseek(pefile,  0, 0, PSEEK_CUR, &start_hi, &start_lo))
		return FALSE;
	ltemp_ddw=M64SET32(start_hi, start_lo);
	ltemp_ddw= M64MINUS(ltemp_ddw,length_ddw);
	new_hi= M64HIGHDW(ltemp_ddw);
	new_lo= M64LOWDW(ltemp_ddw);
	/* Seek to the new location */
	if (!_pc_efilio_lseek(pefile,  new_hi, new_lo, PSEEK_SET_RAW, &ltemp_hi, &ltemp_lo) || (new_hi!=ltemp_hi) || (new_lo!=ltemp_lo))
		return FALSE;
     return TRUE;
}

static BOOLEAN _pc_advance_reader(PC_FILE *preaderefile, ddword length_ddw)
{
    ddword ltemp_ddw;
     return(_pc_cstreamio_lseek(preaderefile->fc.plus.psibling,  CFREAD_POINTER, length_ddw, PSEEK_CUR, &ltemp_ddw));
}

static BOOLEAN _pc_fill_extract_file(PC_FILE *plinearefile, ddword length_ddw)
{
    ddword nleft_ddw;
    dword  ntowrite, ltemp_hi,ltemp;

    nleft_ddw = length_ddw;

    while (M64NOTZERO(nleft_ddw))
    {
        if (M64HIGHDW(nleft_ddw))  /* Split the operation into 2 gig segments */
            ntowrite = 0x80000000;
        else
            ntowrite = M64LOWDW(nleft_ddw);
        if (!_pc_efilio_write(plinearefile, 0, ntowrite, &ltemp) || ltemp != ntowrite)
            return(FALSE);
        nleft_ddw = M64MINUS32(nleft_ddw, ntowrite);
    }
    if (!_pc_efilio_lseek(plinearefile, 0,0, PSEEK_SET, &ltemp_hi, &ltemp))
        return(FALSE);
    return(TRUE);
}






static void _pc_cfilio_extract_do_swap_section(PC_FILE *pefile1, PC_FILE *pefile2)
{
REGION_FRAGMENT swap_frag;

    swap_frag = *(pefile1->fc.plus.pcurrent_fragment);
    pefile1->fc.plus.pcurrent_fragment->start_location =
        pefile2->fc.plus.pcurrent_fragment->start_location;
    pefile1->fc.plus.pcurrent_fragment->end_location =
        pefile2->fc.plus.pcurrent_fragment->end_location;
    pefile2->fc.plus.pcurrent_fragment->start_location =
        swap_frag.start_location;
    pefile2->fc.plus.pcurrent_fragment->end_location =
        swap_frag.end_location;
    /* Check the finodes */
    {
    FINODE *pefinode;
        pefinode = pefile1->fc.plus.ffinode;
        pefinode->e.x->last_processed_cluster = 0; /* We swapped need to relink chains */
        pefinode->operating_flags |= FIOP_NEEDS_FLUSH;
        if (pefinode->e.x->pfirst_fragment == pefile1->fc.plus.pcurrent_fragment)
            pc_pfinode_cluster(pefile1->pobj->pdrive, pefinode, pefinode->e.x->pfirst_fragment->start_location);
        pefinode = pefile2->fc.plus.ffinode;
        pefinode->e.x->last_processed_cluster = 0; /* We swapped need to relink chains */
        pefinode->operating_flags |= FIOP_NEEDS_FLUSH;
        if (pefinode->e.x->pfirst_fragment == pefile2->fc.plus.pcurrent_fragment)
            pc_pfinode_cluster(pefile1->pobj->pdrive, pefinode, pefinode->e.x->pfirst_fragment->start_location);
    }
}


/* Check if the current fragment is mapped */
static BOOLEAN _pc_cfilio_is_mapped(PC_FILE *preaderefile,ddword file_pointer_ddw, dword length_section)
{
REMAP_RECORD *remap_record; /* returned from pc_cfilio_check_remap_read */
ddword length_section_ddw,bytes_to_region_ddw,byte_offset_in_region_ddw,bytes_in_region_ddw;

    remap_record = 0;
    if (!preaderefile->fc.plus.remapped_regions)
        return(FALSE);
    length_section_ddw = M64SET32(0,length_section),

    pc_cfilio_check_remap_read(preaderefile,
                                file_pointer_ddw,
                                length_section_ddw,
                                &bytes_to_region_ddw,
                                &byte_offset_in_region_ddw,
                                &bytes_in_region_ddw,
                                &remap_record);
    if (M64EQ(bytes_to_region_ddw,length_section_ddw))
        return(FALSE);
    else
        return(TRUE);
}


/* Create a free list of remap records */
void pc_cfilio_remap_region_init(
                        PC_FILE *preaderefile,
                        REMAP_RECORD *premap_records,
                        int   num_records)
{
int i;
    preaderefile->fc.plus.remap_freelist = preaderefile->fc.plus.remapped_regions = 0;
    if (!num_records)
        return;
    preaderefile->fc.plus.remap_freelist = premap_records;
    for (i = 1; i < num_records; i++)
    {
        premap_records->pnext = premap_records+1;
        premap_records += 1;
        premap_records->pnext = 0;
    }
}

BOOLEAN pc_cfilio_release_all_remap_files(PC_FILE *preaderefile, int abort)
{
REMAP_RECORD *premap_record;
PC_FILE *pefile;
DDRIVE *pdrive;
dword region_count = 0;

    pdrive = preaderefile->pobj->pdrive;
    premap_record = preaderefile->fc.plus.remapped_regions;
    while (premap_record)
    {
        pefile = premap_record->premapfile;
        if (pefile)
        {
            pefile->fc.plus.psibling = 0;
            pefile->fc.plus.allocation_policy &= ~PCE_REMAP_FILE;
            rtfs_app_callback(RTFS_CBA_DVR_EXTRACT_RELEASE, pefile->my_fd, abort, 0, 0);
        }
        if (region_count++ > REGION_ENDLESS)
            return(FALSE);
        premap_record = premap_record->pnext;
    }
    return(TRUE);
}

/* Purge a region of a circular file of remap records */
/*  while (bytes_to_purge)
        if not in a remap region
             advance and shrink purge widow
        else
           if coterminous on the left and purge ends inside region
                set region left side to end of purge region
                done
           if coterminous on the left and coterminous on the right
                remove current region
                done
           if coterminous on the left and purge extends beyond region
                remove current region
                advance and shrink purge widow
           if purge region begins inside region and ends inside region
                truncate the region to end at the purge region
                create region that starts after purge region and ends at
                    existing region end
                done
           if purge region begins inside region and coterminous on the right
                truncate the region to end at the purge region
                done
           if purge region begins inside region and extends beyond the right
                truncate the region to end at the purge region
*/

BOOLEAN pc_cfilio_remap_region_purge(PC_FILE *preaderefile,ddword purge_start_ddw, ddword purge_length_ddw)
{
dword region_count;
ddword   current_purge_start_ddw,
        current_purge_end_ddw,
        current_purge_length_ddw,
        this_region_start_ddw,
        this_region_end_ddw,
        this_region_length_ddw,
        bytes_to_region_ddw,byte_offset_in_region_ddw,bytes_in_region_ddw;
REMAP_RECORD *pregion;

    current_purge_start_ddw = purge_start_ddw;
    current_purge_length_ddw = purge_length_ddw;

    region_count = 0;

    while (M64NOTZERO(current_purge_length_ddw))
    {
        if (region_count++ > REGION_ENDLESS)
            return(FALSE);

        current_purge_end_ddw =  M64PLUS(current_purge_start_ddw, current_purge_length_ddw);
        current_purge_end_ddw = M64MINUS32(current_purge_end_ddw, 1);
        pc_cfilio_check_remap_read(preaderefile,
                                current_purge_start_ddw,
                                current_purge_length_ddw,
                                &bytes_to_region_ddw,
                                &byte_offset_in_region_ddw,
                                &bytes_in_region_ddw,
                                &pregion);

        if (M64NOTZERO(bytes_to_region_ddw))
        {
        /*        if not in a remap region
                       advance and shrink purge widow
        */
            current_purge_start_ddw = M64PLUS(current_purge_start_ddw,bytes_to_region_ddw);
            if (M64GTEQ(bytes_to_region_ddw, current_purge_length_ddw))
                current_purge_length_ddw = M64SET32(0,0);
            else
                current_purge_length_ddw = M64MINUS(current_purge_length_ddw, bytes_to_region_ddw);
            continue;
        }
        if (!M64NOTZERO(bytes_in_region_ddw))
        {
#if (DEBUG_EXTRACT)
/*DBEXTRACT*/    DEBUG_PRINTF("Purge: Internal error \n");
#endif
            rtfs_set_errno(PEINTERNAL, __FILE__, __LINE__);
            return(FALSE); /* Shouldn't happen */
        }
        /* We are in a purge region */
        this_region_start_ddw = pregion->remap_fpoff_start_ddw;
        this_region_end_ddw =  pregion->remap_fpoff_end_ddw;
        this_region_length_ddw = M64MINUS(this_region_end_ddw, this_region_start_ddw);
        this_region_length_ddw = M64PLUS32(this_region_length_ddw, 1);

        /* Does the purge land on an existing region's left boundary */

        if (M64EQ(this_region_start_ddw,current_purge_start_ddw))
        {
           /* if coterminous on the left and purge ends inside region */
            if (M64GT(this_region_end_ddw,current_purge_end_ddw))
            {
            ddword ltemp_ddw;
                /* set region left side to end of purge region and done */
                pregion->remap_fpoff_start_ddw = M64PLUS32(current_purge_end_ddw,1);
                /* Add the amount we shifted to the linear offset */
                ltemp_ddw = M64MINUS(pregion->remap_fpoff_start_ddw,this_region_start_ddw);
                pregion->remap_linear_offset_ddw = M64PLUS(pregion->remap_linear_offset_ddw, ltemp_ddw);
                current_purge_length_ddw = M64SET32( 0, 0); /* used it up */
                continue;
            }
           /* if coterminous on the left and coterminous on the right */
            else if (M64EQ(this_region_end_ddw, current_purge_end_ddw))
            {
               /*   remove current region and done */
                pc_cfilio_remap_region_unmap(preaderefile,pregion);
                current_purge_length_ddw = M64SET32( 0, 0);/* Used it up */
                continue;
            }
           /* if coterminous on the left and purge extends beyond region */
            else /* if (this_region_end < current_purge_end) */
            {
                /* remove current region
                  advance and shrink purge widow */
                pc_cfilio_remap_region_unmap(preaderefile,pregion);
                current_purge_start_ddw = M64PLUS(current_purge_start_ddw,this_region_length_ddw);
                current_purge_length_ddw = M64MINUS(current_purge_length_ddw, this_region_length_ddw);
                continue;
            }

        }  /* End - purge landing on existing region's left boundary */
        else /* if (this_region_start < current_purge_start) */
        {
            /* if purge region begins inside region and ends inside region */
            if (M64GT(this_region_end_ddw,current_purge_end_ddw))
            {
                /*
                truncate the region to end at the purge region
                create region starts after purge region and ends region end
                done
                */
            ddword this_saved_end_ddw,new_region_start_ddw,
                  new_region_length_ddw,new_region_linear_offset_ddw,ltemp_ddw;

                this_saved_end_ddw = pregion->remap_fpoff_end_ddw;

                /* Truncate the existing region  */
                pregion->remap_fpoff_end_ddw = M64MINUS32(current_purge_start_ddw, 1);

                 /* Now create one region that starts at the end of
                    the purge region and ends at the current end */
                new_region_start_ddw = M64PLUS32(current_purge_end_ddw, 1);
                /* and ends at the old right boundary */
                new_region_length_ddw =
                    M64MINUS(this_region_end_ddw,new_region_start_ddw);
                new_region_length_ddw = M64PLUS32(new_region_length_ddw, 1);
                /* The linear offset of the new region is the linear
                   offset immediately after the purge region */
                ltemp_ddw = M64MINUS(new_region_start_ddw,this_region_start_ddw);
                new_region_linear_offset_ddw = M64PLUS(pregion->remap_linear_offset_ddw, ltemp_ddw);
#if (DEBUG_EXTRACT_PURGE)
/*DBEXTRACT*/                DEBUG_PRINTF("Purge: Inserting remap region into (%d-%d) \n",
                new_region_start,new_region_start+new_region_start-1);
#endif
                if (!pc_cfilio_remap_region_assign(preaderefile,
                    pregion->premapfile,
                    new_region_linear_offset_ddw,
                    new_region_start_ddw,
                    new_region_length_ddw))
                {
                    pregion->remap_fpoff_end_ddw = this_saved_end_ddw;
#if (DEBUG_EXTRACT)
/*DBEXTRACT*/                    DEBUG_PRINTF("Purge: out of regions \n");
#endif
                    return(FALSE);
                }
                current_purge_length_ddw = M64SET32(0,0);
                continue;
               /* END if purge region begins inside region and ends inside
                  region */
            }

           /* if purge region begins inside region and coterminous on
              the right */
            else if (M64EQ(this_region_end_ddw, current_purge_end_ddw))
            {
                /* truncate the region to end at the purge region and done */
                pregion->remap_fpoff_end_ddw = M64MINUS32(current_purge_start_ddw, 1);
                current_purge_length_ddw = M64SET32(0,0);
                continue;
            }
           /* if purge region begins inside region and extends beyond
              the right */
            else /* if (this_region_end < current_purge_end) */
            {
            /*  truncate the region to end at the purge region
                do not advance and shrink purge window  */
                pregion->remap_fpoff_end_ddw = M64MINUS32(current_purge_start_ddw, 1);
                continue;
            }
        }  /* End - Purge starting to the right of an existing
                  region's left boundary */
    } /* while(current_purge_length) */
    return(TRUE);
}



/* Read a remapped region of a circular file.
   The read_count will always fit inside the region.
   read_file_pointer is the offset in the circular file. */

BOOLEAN pc_cfilio_remap_read(PC_FILE *preaderefile,
                              ddword reader_file_pointer_ddw,
                              byte *buf,
                              dword read_count,
                              REMAP_RECORD *remap_record)
{
ddword  offset_file_ddw, saved_file_pointer_ddw,ltemp_ddw;
dword   saved_file_pointer_hi, saved_file_pointer_lo;
BOOLEAN ret_val;

    RTFS_ARGSUSED_PVOID((void *)preaderefile);

    ret_val = FALSE;

    /* get the offset in the region for the read pointer */
    ltemp_ddw = M64MINUS(reader_file_pointer_ddw,remap_record->remap_fpoff_start_ddw);
    /* get the offset in the file for the read pointer   */
    offset_file_ddw = M64PLUS(ltemp_ddw, remap_record->remap_linear_offset_ddw);

	/* Cooked mode file pointer */
    saved_file_pointer_ddw = pc_efilio_get_fp_ddw(remap_record->premapfile);
    saved_file_pointer_hi = M64HIGHDW(saved_file_pointer_ddw);
    saved_file_pointer_lo = M64LOWDW(saved_file_pointer_ddw);

#if (DEBUG_EXTRACT_REMAP_READ)
/*DBEXTRACT*/    DEBUG_PRINTF("Remap read: file_pointer = %d, read count = %d, linear offset = %d\n",
                        reader_file_pointer, read_count, offset_file);
#endif
    {
        dword ltemp_lo, ltemp_hi;
        if (_pc_efilio_lseek(remap_record->premapfile,
                              M64HIGHDW(offset_file_ddw),
                              M64LOWDW(offset_file_ddw),
                              PSEEK_SET_RAW, &ltemp_hi, &ltemp_lo) &&
                              ltemp_hi == M64HIGHDW(offset_file_ddw) &&
                              ltemp_lo == M64LOWDW(offset_file_ddw))
        {
        dword ltemp;
            if (_pc_efilio_read(remap_record->premapfile, buf, read_count, &ltemp) && ltemp == read_count)
                   ret_val = TRUE;

        }
      _pc_efilio_lseek(remap_record->premapfile,
                              saved_file_pointer_hi,
                              saved_file_pointer_lo,
                              PSEEK_SET, &ltemp_hi, &ltemp_lo);
    }
    return(ret_val);
}


/*
void pc_cfilio_check_remap_read(
PC_FILE *preaderefile
    dword data_start
    dword data_length
    dword *bytes_to_region
    dword *byte_offset_in_region
    dword *bytes_in_region,
    REMAP_RECORD **preturn

    Return a region structure if there is a remapped region between
    data_start and data_start plus data_len
    Returns:
    *bytes_to_region - number of bytes up to the next region or data_length
    whichever is less
    byte_offset_in_region - If a region is found offset into the range where
    the region starts.
    bytes_in_region - If a region is found bytes from offset to the end of
    region or data_length, whichever is less.
    preturn - pointer to the region

*/

void pc_cfilio_check_remap_read(PC_FILE *preaderefile,
ddword data_start_ddw,
ddword data_length_ddw,
ddword *bytes_to_region_ddw,
ddword *byte_offset_in_region_ddw,
ddword *bytes_in_region_ddw,
REMAP_RECORD **preturn)
{
REMAP_RECORD *pr;

    *byte_offset_in_region_ddw = M64SET32(0,0);
    *bytes_to_region_ddw = M64SET32(0,0);
    *bytes_in_region_ddw = M64SET32(0,0);;

    pr = pc_cfilio_remap_region_find(preaderefile, data_start_ddw, data_length_ddw);
    *preturn = pr;
    if (!pr)
    {
        *bytes_to_region_ddw = data_length_ddw;
    }
    else if (M64LT(data_start_ddw, pr->remap_fpoff_start_ddw))
    {
    ddword ltemp_ddw;
        ltemp_ddw = M64MINUS(pr->remap_fpoff_start_ddw,data_start_ddw);
        if M64LT(ltemp_ddw,data_length_ddw)
        {
            *bytes_to_region_ddw = ltemp_ddw;
            data_length_ddw = M64MINUS(data_length_ddw,ltemp_ddw);
        }
        ltemp_ddw = M64MINUS(pr->remap_fpoff_end_ddw,pr->remap_fpoff_start_ddw);
        ltemp_ddw = M64PLUS32(ltemp_ddw, 1);
        if (M64LT(ltemp_ddw, data_length_ddw))
            *bytes_in_region_ddw = ltemp_ddw;
        else
           *bytes_in_region_ddw = data_length_ddw;
    }
    else
    {
    ddword ltemp_ddw;
        *bytes_to_region_ddw = M64SET32(0,0);
        *byte_offset_in_region_ddw = M64MINUS(data_start_ddw, pr->remap_fpoff_start_ddw);
        ltemp_ddw = M64MINUS(pr->remap_fpoff_end_ddw, data_start_ddw);
        ltemp_ddw = M64PLUS32(ltemp_ddw, 1);
        if (M64LT(ltemp_ddw , data_length_ddw))
           *bytes_in_region_ddw = ltemp_ddw;
        else
           *bytes_in_region_ddw =  data_length_ddw;
    }
    return;
}

static REMAP_RECORD *pc_cfilio_remap_region_find(PC_FILE *preaderefile, ddword data_start_ddw, ddword data_length_ddw)
{
REMAP_RECORD *premap;
ddword data_end_ddw;

    premap = preaderefile->fc.plus.remapped_regions;
    if (!premap)
        return(0);
    data_end_ddw = M64PLUS(data_start_ddw,data_length_ddw);
    data_end_ddw = M64MINUS32(data_end_ddw,1);
    while (premap)
    {
        if (M64LTEQ(data_start_ddw,premap->remap_fpoff_end_ddw) &&
           M64GTEQ(data_end_ddw,premap->remap_fpoff_start_ddw))
            break;
        premap = premap->pnext;
    }
    return(premap);
}

void pc_remap_free_list(PC_FILE *preaderefile, REMAP_RECORD *premap)
{
REMAP_RECORD *pdelremap;
    while (premap)
    {
        pdelremap = premap;
        premap = premap->pnext;
        pc_cfilio_remap_region_free(preaderefile,pdelremap);
    }
}

/* Remap a region of a circular file to a linear file */
static BOOLEAN pc_cfilio_remap_region_assign(PC_FILE *preaderefile,
                        PC_FILE *premapfile,
                        ddword linear_file_offset_ddw,
                        ddword remap_fpoff_start_ddw,
                        ddword remap_fpoff_length_ddw)
{
REMAP_RECORD *prevremap,*premap,*pnewremap;
ddword remap_fpoff_end_ddw;
    /* Remember that this file is in our remap set */

    pnewremap = pc_cfilio_remap_region_alloc(preaderefile);
    if (!pnewremap)
        return(FALSE);
    remap_fpoff_end_ddw = M64PLUS(remap_fpoff_start_ddw,remap_fpoff_length_ddw);
    remap_fpoff_end_ddw = M64MINUS32(remap_fpoff_end_ddw,1);
    premapfile->fc.plus.psibling = preaderefile;  /* Link the remap file to the reader */
    pnewremap->premapfile = premapfile;
    pnewremap->remap_fpoff_start_ddw = remap_fpoff_start_ddw;
    pnewremap->remap_fpoff_end_ddw = remap_fpoff_end_ddw;
    pnewremap->remap_linear_offset_ddw = linear_file_offset_ddw;
    pnewremap->pnext = 0;
    premap = preaderefile->fc.plus.remapped_regions;
    if (!premap)
        preaderefile->fc.plus.remapped_regions = pnewremap;
    else
    {
        prevremap = 0;
        while (M64GT(remap_fpoff_start_ddw,premap->remap_fpoff_end_ddw))
        {
            if (premap->pnext)
            {
                prevremap = premap;
                premap = premap->pnext;
            }
            else
            {
                premap->pnext = pnewremap;
                return(TRUE);
            }
        }
        if (prevremap)
            prevremap->pnext = pnewremap;
        pnewremap->pnext = premap;
        if (premap == preaderefile->fc.plus.remapped_regions)
            preaderefile->fc.plus.remapped_regions = pnewremap;
    }
    return(TRUE);
}

static void pc_cfilio_remap_region_unmap(PC_FILE *preaderefile, REMAP_RECORD *premap)
{
PC_FILE *premapfile;

   premapfile = premap->premapfile;
   preaderefile->fc.plus.remapped_regions =
       pc_cfilio_unlink_remap_record(premap, preaderefile->fc.plus.remapped_regions);
   pc_cfilio_remap_region_free(preaderefile,premap);
   /* Unlink the remap file if no longer in the remap chain */
   if (!pc_cfilio_remap_find_region_by_file(preaderefile,premapfile))
   {
        premapfile->fc.plus.psibling = 0;
        premapfile->fc.plus.allocation_policy &= ~PCE_REMAP_FILE;
        rtfs_app_callback(RTFS_CBA_DVR_EXTRACT_RELEASE, premapfile->my_fd, 0, 0, 0);

    }
}


static REMAP_RECORD *pc_cfilio_remap_region_alloc(PC_FILE *preaderefile)
{
static REMAP_RECORD *premap;

    if (preaderefile->fc.plus.remap_freelist)
    {
        premap = preaderefile->fc.plus.remap_freelist;
        preaderefile->fc.plus.remap_freelist = premap->pnext;
        return(premap);
    }
    else
    {
        rtfs_set_errno(PECFIONOMAPREGIONS, __FILE__, __LINE__);
        return(0);
    }
}

static void pc_cfilio_remap_region_free(PC_FILE *preaderefile,REMAP_RECORD *premap)
{
    premap->pnext = preaderefile->fc.plus.remap_freelist;
    preaderefile->fc.plus.remap_freelist = premap;
}

void pc_cfilio_release_remap_file(PC_FILE *premapfile, int abort)
{
REMAP_RECORD *pr;
PC_FILE *preaderefile;

    if (!premapfile)
        return;
    preaderefile = premapfile->fc.plus.psibling;
    /* Harmless if the file is not mapped */
    if(preaderefile)
    {
        do
        { /* Remove all remap records associated with the linear remap file */
            pr = pc_cfilio_remap_find_region_by_file(preaderefile,premapfile);
            if (pr)
            {
                preaderefile->fc.plus.remapped_regions=
                    pc_cfilio_unlink_remap_record(pr, preaderefile->fc.plus.remapped_regions);
                pc_cfilio_remap_region_free(preaderefile,pr);
            }
        } while(pr);
    }
    /* Unlink the file */
    premapfile->fc.plus.psibling = 0;
    premapfile->fc.plus.allocation_policy &= ~PCE_REMAP_FILE;
    rtfs_app_callback(RTFS_CBA_DVR_EXTRACT_RELEASE, premapfile->my_fd, abort, 0, 0);
}

static REMAP_RECORD *pc_cfilio_unlink_remap_record(REMAP_RECORD *premove,REMAP_RECORD *plist)
{
REMAP_RECORD *listhead,*pr,*pprev;
    pprev = 0;
    listhead = pr = plist;
    while (pr)
    {
        if (pr == premove)
        {
            if (!pprev)
                listhead = pr->pnext;
            else
                pprev->pnext = premove->pnext;
            break;
        }
        pprev = pr;
        pr = pr->pnext;
    }
    return(listhead);
}

static REMAP_RECORD *pc_cfilio_remap_find_region_by_file(PC_FILE *preaderefile, PC_FILE *premapfile)
{
REMAP_RECORD *premap;
    premap = preaderefile->fc.plus.remapped_regions;
    if (!premap)
        return(0);
    while (premap)
    {
        if (premap->premapfile == premapfile)
            break;
        premap = premap->pnext;
    }
    return(premap);
}

static BOOLEAN _reader_ftell(PC_FILE *pwriterefile, ddword *psaved_read_pointer_ddw)
{
    return(_pc_cstreamio_lseek(pwriterefile,  CFREAD_POINTER, M64SET32(0,0), PSEEK_CUR, psaved_read_pointer_ddw));
}
static BOOLEAN _writer_ftell(PC_FILE *pwriterefile, ddword *psaved_write_pointer_ddw)
{
   return(_pc_cstreamio_lseek(pwriterefile,  CFWRITE_POINTER, M64SET32(0,0), PSEEK_CUR, psaved_write_pointer_ddw));
}
static BOOLEAN _reader_frestore(PC_FILE *pwriterefile, ddword saved_read_pointer_ddw)
{
ddword ltemp_ddw;
	_pc_efilio_reset_seek(pwriterefile->fc.plus.psibling);
   return(_pc_cstreamio_lseek(pwriterefile,  CFREAD_POINTER, saved_read_pointer_ddw, PSEEK_SET, &ltemp_ddw));
}
static BOOLEAN _writer_frestore(PC_FILE *pwriterefile, ddword saved_read_pointer_ddw)
{
ddword ltemp_ddw;
	_pc_efilio_reset_seek(pwriterefile);
   return(_pc_cstreamio_lseek(pwriterefile,  CFWRITE_POINTER, saved_read_pointer_ddw, PSEEK_SET, &ltemp_ddw));
}

#if (DEBUG_EXTRACT)


/*DBEXTRACT*/ void DEBUG_dump32_chain(PC_FILE *pefile)
/*DBEXTRACT*/ {
/*DBEXTRACT*/     FINODE *pfi;
/*DBEXTRACT*/     REGION_FRAGMENT *pf;
/*DBEXTRACT*/     dword last_start, last_end, current_length;
/*DBEXTRACT*/
/*DBEXTRACT*/     last_start=last_end=current_length = 0;
/*DBEXTRACT*/
/*DBEXTRACT*/     pfi = pefile->pobj->finode;
/*DBEXTRACT*/     pf = pfi->e.x->pfirst_fragment;
/*DBEXTRACT*/     while (pf)
/*DBEXTRACT*/     {
/*DBEXTRACT*/         if (last_end+1 == pf->start_location)
/*DBEXTRACT*/         {
/*DBEXTRACT*/             last_end = pf->end_location;
/*DBEXTRACT*/             current_length += PC_FRAGMENT_SIZE_CLUSTERS(pf);
/*DBEXTRACT*/         }
/*DBEXTRACT*/         else
/*DBEXTRACT*/         {
/*DBEXTRACT*/             if (current_length)
/*DBEXTRACT*/             {
/*DBEXTRACT*/                 printf("(%d-%d)", last_start, last_end);
/*DBEXTRACT*/                 current_length = 0;
/*DBEXTRACT*/             }
/*DBEXTRACT*/             last_start = pf->start_location;
/*DBEXTRACT*/             last_end = pf->end_location;
/*DBEXTRACT*/             current_length = PC_FRAGMENT_SIZE_CLUSTERS(pf);
/*DBEXTRACT*/         }
/*DBEXTRACT*/         pf = pf->pnext;
/*DBEXTRACT*/     }
/*DBEXTRACT*/     if (current_length)
/*DBEXTRACT*/         printf("(%d-%d)", last_start, last_end);
/*DBEXTRACT*/     printf("<- Clusters \n");
/*DBEXTRACT*/     last_start=last_end=current_length = 0;
/*DBEXTRACT*/	  ddword start_bytes=0;
/*DBEXTRACT*/     pfi = pefile->pobj->finode;
/*DBEXTRACT*/     pf = pfi->e.x->pfirst_fragment;
/*DBEXTRACT*/     while (pf)
/*DBEXTRACT*/     {
/*DBEXTRACT*/         if (last_end+1 == pf->start_location)
/*DBEXTRACT*/         {
/*DBEXTRACT*/             last_end = pf->end_location;
/*DBEXTRACT*/             current_length += PC_FRAGMENT_SIZE_CLUSTERS(pf);
/*DBEXTRACT*/         }
/*DBEXTRACT*/         else
/*DBEXTRACT*/         {
/*DBEXTRACT*/             if (current_length)
/*DBEXTRACT*/             {
/*DBEXTRACT*/             printf("(%I64d-%I64d)",start_bytes, (start_bytes+(ddword)(current_length)*pefile->pobj->pdrive->drive_info.bytespcluster/4)-1);
/*DBEXTRACT*/					  start_bytes = start_bytes + (ddword)(current_length)*pefile->pobj->pdrive->drive_info.bytespcluster/4;
/*DBEXTRACT*/                 current_length = 0;
/*DBEXTRACT*/             }
/*DBEXTRACT*/             last_start = pf->start_location;
/*DBEXTRACT*/             last_end = pf->end_location;
/*DBEXTRACT*/             current_length = PC_FRAGMENT_SIZE_CLUSTERS(pf);
/*DBEXTRACT*/         }
/*DBEXTRACT*/         pf = pf->pnext;
/*DBEXTRACT*/     }
/*DBEXTRACT*/     if (current_length)
/*DBEXTRACT*/			printf("(%I64d-%I64d)",start_bytes, (start_bytes+(ddword)(current_length)*pefile->pobj->pdrive->drive_info.bytespcluster/4)-1);
/*DBEXTRACT*/     printf("<- Dwords \n");
/*DBEXTRACT*/ }
/*DBEXTRACT*/
/*DBEXTRACT*/
/*DBEXTRACT*/ void DEBUG_disp_file_chain(char *prompt, PC_FILE *pefile)
/*DBEXTRACT*/ {
/*DBEXTRACT*/         DEBUG_dump32_chain(pefile);
/*DBEXTRACT*/ }
/*DBEXTRACT*/
/*DBEXTRACT*/ void DEBUG_print_ddword(ddword value_ddw)
/*DBEXTRACT*/ {
/*DBEXTRACT*/     DEBUG_PRINTF("%d%d", M64HIGHDW(value_ddw),M64LOWDW(value_ddw));
/*DBEXTRACT*/ }
/*DBEXTRACT*/
/*DBEXTRACT*/ void DEBUG_disp_remaps(char *prompt,PC_FILE *preaderefile)
/*DBEXTRACT*/ {
/*DBEXTRACT*/ REMAP_RECORD *premap;
/*DBEXTRACT*/     premap = preaderefile->fc.plus.remapped_regions;
/*DBEXTRACT*/     if (!premap)
/*DBEXTRACT*/         return;
/*DBEXTRACT*/     DEBUG_PRINTF("%s\n", prompt);
/*DBEXTRACT*/     while (premap)
/*DBEXTRACT*/     {
/*DBEXTRACT*/         DEBUG_PRINTF("( [%d%d]",
/*DBEXTRACT*/             M64HIGHDW(premap->remap_fpoff_start_ddw),
/*DBEXTRACT*/             M64LOWDW(premap->remap_fpoff_start_ddw));
/*DBEXTRACT*/         DEBUG_PRINTF(" - [%d%d] ",
/*DBEXTRACT*/             M64HIGHDW(premap->remap_fpoff_end_ddw),
/*DBEXTRACT*/             M64LOWDW(premap->remap_fpoff_end_ddw));
/*DBEXTRACT*/         DEBUG_PRINTF(" offset = [%d%d] ",
/*DBEXTRACT*/             M64HIGHDW(premap->remap_linear_offset_ddw),
/*DBEXTRACT*/             M64LOWDW(premap->remap_linear_offset_ddw));
/*DBEXTRACT*/         premap = premap->pnext;
/*DBEXTRACT*/     }
/*DBEXTRACT*/     DEBUG_PRINTF("\n");
/*DBEXTRACT*/ }
/*DBEXTRACT*/
/*DBEXTRACT*/
/*DBEXTRACT*/
/*DBEXTRACT*/
/*DBEXTRACT*/
/*DBEXTRACT*/ ddword copied_array[100];
/*DBEXTRACT*/ dword  copied_length[100];
/*DBEXTRACT*/ ddword swapped_array[100];
/*DBEXTRACT*/ dword  swapped_length[100];
/*DBEXTRACT*/ dword  copy_offset;
/*DBEXTRACT*/ dword swap_offset;
/*DBEXTRACT*/
/*DBEXTRACT*/ /* caller = 0, copy routine, 1 swap routine op = 0=clear, 1=copy or swap, 2=skip,  */
/*DBEXTRACT*/ void DEBUG_log_progress(PC_FILE *pefile, dword caller, dword op, dword length_segment)
/*DBEXTRACT*/ {
/*DBEXTRACT*/ ddword file_pointer;
/*DBEXTRACT*/
/*DBEXTRACT*/     if (op==0)
/*DBEXTRACT*/     {
/*DBEXTRACT*/         copy_offset = swap_offset = 0;
/*DBEXTRACT*/         return;
/*DBEXTRACT*/     }
/*DBEXTRACT*/     if (!_reader_ftell(pefile->fc.plus.psibling, &file_pointer))
/*DBEXTRACT*/        return;
/*DBEXTRACT*/
/*DBEXTRACT*/     if (caller == 0 && op == 1) /* copy routine.. called first */
/*DBEXTRACT*/     {
/*DBEXTRACT*/         copied_array[copy_offset] = file_pointer;
/*DBEXTRACT*/         copied_length[copy_offset++] = length_segment;
/*DBEXTRACT*/ }
/*DBEXTRACT*/     else if (caller == 1 && op == 1) /* swap routine.. called next */
/*DBEXTRACT*/     {
/*DBEXTRACT*/         swapped_array[swap_offset] = file_pointer;
/*DBEXTRACT*/         swapped_length[swap_offset++] = length_segment;
/*DBEXTRACT*/     }
/*DBEXTRACT*/ }
/*DBEXTRACT*/
/*DBEXTRACT*/ void DEBUG_show_progress(void)
/*DBEXTRACT*/ {
/*DBEXTRACT*/ dword current_swap, current_copy;
/*DBEXTRACT*/ ddword current_pos_ddw,next_pos_ddw,current_end_ddw;
/*DBEXTRACT*/ dword current_length,num_ops;
/*DBEXTRACT*/ char *opname;
/*DBEXTRACT*/
/*DBEXTRACT*/     DEBUG_PRINTF("\n<---Summary----->\n");
/*DBEXTRACT*/     num_ops = current_swap = current_copy = 0;
/*DBEXTRACT*/
/*DBEXTRACT*/     next_pos_ddw = M64SET32(0,0);
/*DBEXTRACT*/
/*DBEXTRACT*/     while(current_swap < swap_offset || current_copy < copy_offset)
/*DBEXTRACT*/     {
/*DBEXTRACT*/         if ((current_copy >= copy_offset) ||
/*DBEXTRACT*/             ((current_swap < swap_offset) && M64LT(swapped_array[current_swap],copied_array[current_copy]))
/*DBEXTRACT*/             )
/*DBEXTRACT*/         {
/*DBEXTRACT*/             current_pos_ddw = swapped_array[current_swap];
/*DBEXTRACT*/             current_length =  swapped_length[current_swap++];
/*DBEXTRACT*/             opname = "swap";
/*DBEXTRACT*/         }
/*DBEXTRACT*/         else if ((current_swap >= swap_offset) ||
/*DBEXTRACT*/             ((current_copy < copy_offset) && M64LT(copied_array[current_copy],swapped_array[current_swap]))
/*DBEXTRACT*/             )
/*DBEXTRACT*/         {
/*DBEXTRACT*/             current_pos_ddw = copied_array[current_copy];
/*DBEXTRACT*/             current_length =  copied_length[current_copy++];
/*DBEXTRACT*/             opname = "copy";
/*DBEXTRACT*/         }
/*DBEXTRACT*/         else
/*DBEXTRACT*/         {
/*DBEXTRACT*/                 DEBUG_PRINTF("\n<lost>\n");
/*DBEXTRACT*/                 return;
/*DBEXTRACT*/         }
/*DBEXTRACT*/
/*DBEXTRACT*/         if (num_ops)
/*DBEXTRACT*/         {
/*DBEXTRACT*/             if (M64EQ(next_pos_ddw,current_pos_ddw))
/*DBEXTRACT*/             ; /* okay */
/*DBEXTRACT*/             else
/*DBEXTRACT*/             {
/*DBEXTRACT*/                 DEBUG_PRINTF("\n<contiuity error>\n");
/*DBEXTRACT*/             }
/*DBEXTRACT*/         }
/*DBEXTRACT*/
/*DBEXTRACT*/         num_ops += 1;
/*DBEXTRACT*/         current_end_ddw = M64PLUS32(current_pos_ddw,current_length);
/*DBEXTRACT*/         next_pos_ddw = M64PLUS32(current_pos_ddw,current_length);
/*DBEXTRACT*/         DEBUG_PRINTF("%s %8.8d bytes from(%10.10d:%10.10d) to (%10.10d:%10.10d)\n", opname, current_length, M64HIGHDW(current_pos_ddw),M64LOWDW(current_pos_ddw),
/*DBEXTRACT*/             M64HIGHDW(current_end_ddw),M64LOWDW(current_end_ddw));
/*DBEXTRACT*/     }
/*DBEXTRACT*/     DEBUG_PRINTF("\n<-------->\n");
/*DBEXTRACT*/ }
/*DBEXTRACT*/

#endif /* (DEBUG_EXTRACT) */

#endif /*  (INCLUDE_CIRCULAR_FILES) */
#endif /* Exclude from build if read only */
