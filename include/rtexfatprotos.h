#ifndef __RTFSEXFATPROTOS__
#define __RTFSEXFATPROTOS__ 1






#if (DEBUG_EXFAT_PROBE_ROOT)
void probePrintDirectoryBlock(DDRIVE *pdr, byte *b, dword cluster, dword sector, int nsectors);
void probePrintBootRegion(DDRIVE *pdr);
#endif

void pcexfat_filenameobj_destroy(EXFATFILEPARSEOBJ *pfilenameobj);
BOOLEAN pcexfat_filenameobj_init(DROBJ *pobj, byte *filename, EXFATFILEPARSEOBJ *pfilenameobj, int use_charset);
BOOLEAN pcexfat_findinbyfilenameobj(DROBJ *pobj, EXFATFILEPARSEOBJ *pfilenameobj, BOOLEAN oneshot, int action);
BOOLEAN exfatop_remove_free_region(DDRIVE *pdr, dword cluster, dword ncontig);
BOOLEAN exfatop_add_free_region(DDRIVE *pdr, dword cluster, dword ncontig, BOOLEAN do_erase);

word pcexfat_checksum_util(word Checksum, BOOLEAN isprimary, byte *string);
BOOLEAN pcexfat_link_file_chain(DDRIVE *pdr, REGION_FRAGMENT *pf);
BOOLEAN pcexfat_grow_directory(DROBJ *pobj);
REGION_FRAGMENT *pcexfat_load_root_fragments(DDRIVE *pdrive);
BOOLEAN _pcexfat_bfilio_reduce_size(PC_FILE *pefile, ddword new_size);

DROBJ *pcexfat_fndnode(DDRIVE *pdrive, byte *path, int use_charset);
void pcexfat_initialize_root_finode(DDRIVE *pdrive, FINODE *pfi);


BOOLEAN pcexfat_multi_dir_get(DROBJ *pobj, BLKBUFF **pscan_buff, byte **pscan_data, byte *puser_buffer, dword *n_blocks, BOOLEAN do_increment);

FATBUFF *pc_find_fat_blk_primary(FATBUFFCNTXT *pfatbuffcntxt, dword fat_sector_offset);
void pc_set_fat_blk_primary(FATBUFFCNTXT *pfatbuffcntxt, FATBUFF *pblk);
void pc_clear_fat_blk_primary(FATBUFFCNTXT *pfatbuffcntxt, FATBUFF *pblk);
FATBUFF *pc_find_fat_blk_secondary (FATBUFFCNTXT *pfatbuffcntxt, dword fat_sector_offset);
void pc_clear_fat_blk_secondary(FATBUFFCNTXT *pfatbuffcntxt, FATBUFF *pblk);
void pc_set_fat_blk_secondary (FATBUFFCNTXT *pfatbuffcntxt, FATBUFF *pblk);
void pc_uncommit_fat_blk_queue(FATBUFFCNTXT *pfatbuffcntxt, FATBUFF *pblk);
void pc_commit_fat_blk_queue(FATBUFFCNTXT *pfatbuffcntxt, FATBUFF *pblk);
void pc_free_fat_blk(FATBUFFCNTXT *pfatbuffcntxt, FATBUFF *pblk);
FATBUFF *pc_realloc_fat_blk(DDRIVE *pdrive, FATBUFFCNTXT *pfatbuffcntxt, dword fat_sector_offset);

BOOLEAN pc_write_bam_block_buffer_page(DDRIVE *pdrive, FATBUFF *pblk);
void set_bam_dirty(DDRIVE *pdr);
void clear_bam_dirty(DDRIVE *pdr);
void pc_free_all_bam_buffers(DDRIVE *pdr);

BOOLEAN pcexfat_insert_inode(DROBJ *pobj , DROBJ *pmom, byte _attr, FINODE *infinode, dword initcluster, byte *filename, byte secondaryflags, dword sizehi, dword sizelow, int use_charset);

BOOLEAN pcexfat_checkerased(byte *pi);
void pcexfat_addtoseglist(SEGDESC *s, dword my_block, int my_index);

dword preboot_pcclnext(DDRIVE *pdr, dword cluster, int *error);
void pcexfat_upcase_unicode_string(DDRIVE *pdr, word *to, word *from,int maxcount);

BOOLEAN exfatop_read_upCaseTable(DDRIVE *pdr);
byte *pc_data_in_ubuff(DDRIVE *pdr, dword blockno,byte *puser_buffer, dword user_buffer_first_block,dword user_buffer_n_blocks);


BOOLEAN pcexfat_rmnode(DROBJ *pobj);
BOOLEAN pcexfat_mvnode(DROBJ *old_parent_obj,DROBJ *old_obj,DROBJ *new_parent_obj, byte *filename,int use_charset);
BOOLEAN pcexfat_set_volume(DDRIVE *pdrive, byte *volume_label,int use_charset);
BOOLEAN pcexfat_get_volume(DDRIVE *pdrive, byte *volume_label,int use_charset);
BOOLEAN pcexfat_get_cwd(DDRIVE *pdrive, byte *path, int use_charset);
BOOLEAN pcexfat_set_cwd(DDRIVE *pdrive, byte *name, int use_charset);
BOOLEAN pcexfat_update_by_finode(FINODE *pfi, int entry_index, BOOLEAN set_archive, int set_date_mask, BOOLEAN do_delete);
void pcexfat_update_finode_datestamp(FINODE *pfi, BOOLEAN set_archive, int set_date_mask);
BOOLEAN pcexfat_flush(DDRIVE *pdrive);
dword exfatop_find_contiguous_free_clusters(DDRIVE *pdr, dword startpt, dword endpt, dword min_clusters, dword max_clusters, dword *p_contig, int *is_error);
BOOLEAN  rtexfat_i_dskopen(DDRIVE *pdr);
void pc_release_exfat_buffers(DDRIVE *pdr);
BOOLEAN pcexfat_findin( DROBJ *pobj, byte *filename, int action, BOOLEAN oneshot, int use_charset);
byte *pcexfat_seglist2text(DDRIVE * pdrive, SEGDESC *s, byte *lfn, int use_charset);
dword exFatfatop_getdir_frag(DROBJ *pobj, dword startpt, dword *pnext_cluster, dword n_clusters, int *end_of_chain);
dword exFatfatop_getfile_frag(FINODE *pfi, dword startpt, dword *pnext_cluster, dword n_clusters, int *end_of_chain);
BOOLEAN pcexfat_gread(DSTAT *statobj, int blocks_to_read, byte *buffer, int *blocks_read);
BOOLEAN pcexfat_parse_path(DDRIVE *pdrive, byte *outpath, byte *inpath, int use_charset);
BOOLEAN  rtexfat_gblk0(DDRIVE *pdr, struct pcblk0 *pbl0b, byte *b);

EXFATDDRIVE *pcexfat_pdrtoexfat(DDRIVE *pdr);
#define PDRTOEXFSTR(X) pcexfat_pdrtoexfat(X)

dword pcexfat_getexNOFATCHAINfirstcluster(DROBJ *pobj);
dword pcexfat_getexNOFATCHAINlastcluster(DROBJ *pobj);
BOOLEAN pcexfat_expand_nochain(FINODE *pefinode);

int pcexfat_bfilio_read(PC_FILE *pefile, byte *in_buff, int count);
BOOLEAN pcexfat_bpefile_ulseek(PC_FILE *pefile, ddword offset, ddword *pnew_offset, int origin);

BOOLEAN pcexfat_bfilio_write(PC_FILE *pefile, byte *in_buff, dword n_bytes, dword *n_written);
BOOLEAN pcexfat_bfilio_chsize(PC_FILE *pefile, ddword dwnew_size);
BOOLEAN _pcexfat_bfilio_flush(PC_FILE *pefile);
BOOLEAN pcexfat_bfilio_load_all_fragments(FINODE *pefinode);
BOOLEAN pc_exfat_check_disk(byte *driveid,DDRIVE *pdr,CHKDISK_STATS *pchkstat);
#define pcexfat_set_volume_dirty(A)   /* Implement to set and clear dirty bytes in bpb */
#define pcexfat_clear_volume_dirty(A)
void pcexfat_getsysdate( DATESTR *pcrdate, byte *putcoffset, byte *pmsincrement);
DATESTR dwordToDateStr(dword indword);

dword pc_byte2clmod64(DDRIVE *pdr, dword nbytes_hi, dword nbytes_lo);
ddword pc_fragment_size_64(DDRIVE *pdr,REGION_FRAGMENT *pfragment);

#endif /*  __RTFSEXFATPROTOS__ */
