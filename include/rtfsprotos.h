/*****************************************************************************
*Filename: RTFSPROTOS.H - RTFS common function prototypes
*
*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS, 2007
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*
*
*
* Description:
*
*
*
*
****************************************************************************/

#ifndef __RTFSPROTOS__
#define __RTFSPROTOS__ 1

/* ===========================  */
/* Rtfs internal prototypes     */
/* ===========================  */
#ifdef __cplusplus
extern "C" {
#endif

/* Application callbacks */
#define RTFS_CBS_INIT						0
#define RTFS_CBS_PUTS					 	1
#define RTFS_CBS_GETS					 	2
#define RTFS_CBS_GETDATE				 	3
#define RTFS_CBS_POLL_DEVICE_READY			4
/* Exfat buffering and operating parameters are via callback not from the device layer */
#define RTFS_CBS_GETEXFATBUFFERS		 	5
#define RTFS_CBS_RELEASEEXFATBUFFERS	 	6
/* Exfat date extensions  via callback not from the device layer */
#define RTFS_CBS_UTCOFFSET					7
#define RTFS_CBS_10MSINCREMENT				8





#define RTFS_CBA_INFO_MOUNT_STARTED			0
#define RTFS_CBA_INFO_MOUNT_FAILED			1
#define RTFS_CBA_INFO_MOUNT_COMPLETE 		2
#define RTFS_CBA_ASYNC_MOUNT_CHECK		 	3
#define RTFS_CBA_ASYNC_START		 		4
#define RTFS_CBA_ASYNC_DRIVE_COMPLETE		5
#define RTFS_CBA_ASYNC_FILE_COMPLETE		6
#define RTFS_CBA_DVR_EXTRACT_RELEASE		7



#define RTFS_CBD_ASSERT						 0
#define RTFS_CBD_ASSERT_TEST				 1
#define RTFS_CBD_SETERRNO					 2
#define RTFS_CBD_IOERROR					 3


#define RTFS_CB_FS_RETRIEVE_FIXED_JOURNAL_LOCATION			200
#define RTFS_CB_FS_FAIL_ON_JOURNAL_RESIZE					201
#define RTFS_CB_FS_FAIL_ON_JOURNAL_FULL 					202
#define RTFS_CB_FS_RETRIEVE_JOURNAL_SIZE					203
#define RTFS_CB_FS_RETRIEVE_RESTORE_STRATEGY        		204
#define RTFS_CB_FS_FAIL_ON_JOURNAL_CHANGED					205
#define RTFS_CB_FS_CHECK_JOURNAL_BEGIN_NOT					206
#define RTFS_CB_FS_RETRIEVE_FLUSH_STRATEGY					207

int rtfs_sys_callback(int cb_code, void *pvargs);
int rtfs_app_callback(int cb_code, int iarg0, int iargs1, int iargs2, void *pvargs);
void rtfs_diag_callback(int cb_code, int iarg0);
int rtfs_failsafe_callback(int cb_code, int driveno, int iarg0, void *pvargs0, void *pvargs1);


#if (INCLUDE_FAILSAFE_RUNTIME)
BOOLEAN pc_failsafe_init(void);
#endif


#if (INCLUDE_CS_UNICODE)
/* Prototypes of unicode / Non-Unicode selectable APIS */
BOOLEAN pc_get_volume_cs(byte *driveid, byte  *volume_label,int use_charset);
BOOLEAN pc_set_volume_cs(byte *driveid, byte  *volume_label,int use_charset);
BOOLEAN pc_mv_cs(byte *old_name, byte *new_name,int use_charset);
BOOLEAN pc_get_cwd_cs(byte *drive, byte *path, int use_charset);
BOOLEAN pc_pwd_cs(byte *drive, byte *path, int use_charset);
BOOLEAN pc_gfirst_cs(DSTAT *statobj, byte *name, int use_charset);
BOOLEAN pc_glast_cs(DSTAT *statobj, byte *name, int use_charset);
BOOLEAN pc_gnext_cs(DSTAT *statobj, int use_charset);
BOOLEAN pc_gprev_cs(DSTAT *statobj, int use_charset);
BOOLEAN pc_isdir_cs(byte *path, int use_charset);
BOOLEAN pc_isvol_cs(byte *path, int use_charset);
BOOLEAN pc_get_attributes_cs(byte *path, byte *p_return, int use_charset);
BOOLEAN pc_mkdir_cs(byte  *name, int use_charset);
BOOLEAN pc_rmdir_cs(byte  *name, int use_charset);
BOOLEAN pc_set_attributes_cs(byte *path, byte attributes, int use_charset);
BOOLEAN pc_set_cwd_cs(byte *name, int use_charset);
int pc_stat_cs(byte *name, ERTFS_STAT *pstat, int use_charset);
BOOLEAN pc_unlink_cs(byte *name, int use_charset);
int po_open_cs(byte *name, word flag, word mode, int use_charset);
int pc_get_default_drive_cs(byte *drive_name, int use_charset);
BOOLEAN pc_deltree_cs(byte  *name, int use_charset);
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
                 int use_charset);
BOOLEAN pc_get_dirent_info_cs(byte *path, DIRENT_INFO *pinfo, int use_charset);
BOOLEAN pc_set_dirent_info_cs(byte *path, DIRENT_INFO *pinfo, int use_charset);
BOOLEAN pc_get_media_parms_cs(byte *path, PDEV_GEOMETRY pgeometry, int use_charset);
BOOLEAN pc_format_media_cs(byte *path,  int use_charset);
BOOLEAN pc_format_volume_cs(byte *path, int use_charset);
BOOLEAN pc_format_volume_ex_cs(byte *path, RTFSFMTPARMSEX *pappfmt, int use_charset);
BOOLEAN pc_partition_media_cs(byte *path, struct mbr_specification *pmbrspec, int use_charset);

#else
/* Prototypes of Non-Unicode APIS */
BOOLEAN pc_get_volume(byte *driveid, byte  *volume_label);
BOOLEAN pc_set_volume(byte *driveid, byte  *volume_label);
BOOLEAN pc_mv(byte *old_name, byte *new_name);
BOOLEAN pc_get_cwd(byte *drive, byte *path);
BOOLEAN pc_pwd(byte *drive, byte *path);
BOOLEAN pc_gfirst(DSTAT *statobj, byte *name);
BOOLEAN pc_glast(DSTAT *statobj, byte *name);
BOOLEAN pc_gnext(DSTAT *statobj);
BOOLEAN pc_gprev(DSTAT *statobj);
BOOLEAN pc_isdir(byte *path);
BOOLEAN pc_isvol(byte *path);
BOOLEAN pc_get_attributes(byte *path, byte *p_return);
BOOLEAN pc_rmdir(byte  *name);
BOOLEAN pc_mkdir(byte  *name);
BOOLEAN pc_set_attributes(byte *path, byte attributes);
BOOLEAN pc_set_cwd(byte *name);
int pc_stat(byte *name, ERTFS_STAT *pstat);
BOOLEAN pc_unlink(byte *name);

int po_open(byte *name, word flag, word mode);
int pc_get_default_drive(byte *drive_name);
BOOLEAN pc_deltree(byte  *name);
int pc_enumerate( /* __apifn__ */
                 byte    * from_path_buffer,
                 byte    * from_pattern_buffer,
                 byte    * spath_buffer,
                 byte    * dpath_buffer,
                 byte    * root_search,
                 word    match_flags,
                 byte    * match_pattern,
                 int     maxdepth,
                 PENUMCALLBACK pcallback);
BOOLEAN pc_get_dirent_info(byte *path, DIRENT_INFO *pinfo);
BOOLEAN pc_set_dirent_info(byte *path, DIRENT_INFO *pinfo);
BOOLEAN pc_get_media_parms(byte *path, PDEV_GEOMETRY pgeometry);
BOOLEAN pc_format_media(byte *path);
BOOLEAN pc_format_volume(byte *path);
BOOLEAN pc_format_volume_ex(byte *path, RTFSFMTPARMSEX *pappfmt);
BOOLEAN pc_partition_media(byte *path, struct mbr_specification *pmbrspec);
#endif
/* Versions of these entry points are provided by both Pro and ProPlus */
int rtfs_app_entry(void);   /* Pro style See apirun.c */
int pc_ertfs_run(void);     /* ProPlus style See apirun.c */
int _pc_ertfs_run(void);
void pc_ertfs_shutdown(void);

#if (INCLUDE_EXFATORFAT64)
#define FINODESIZEISZERO(P) ((!ISEXFATORFAT64(P->my_drive)&&P->fsizeu.fsize== 0)||(ISEXFATORFAT64(P->my_drive)&&M64ISZERO(P->fsizeu.fsize64)))
BOOLEAN pcexfat_format_volume(byte *path);
#else
#define FINODESIZEISZERO(P) (P->fsizeu.fsize == 0)
#endif

/* rtfs shell entry point */
void tst_shell(void);

void pc_free_disk_configuration(int drive_number);

int po_read(int fd,    byte *in_buff, int count);
int po_write(int fd, byte *buf, int count);
BOOLEAN po_truncate(int fd, dword offset);
int po_chsize(int fd, dword offset);
BOOLEAN po_flush(int fd);
long po_lseek(int fd, long offset, int origin);
BOOLEAN po_ulseek(int fd, dword offset, dword *pnew_offset, int origin);
int po_close(int fd);
int pc_fstat(int fd, ERTFS_STAT *pstat);
BOOLEAN  pc_check_automount(DDRIVE *pdr);
BOOLEAN _pc_diskflush(DDRIVE *pdrive);

long pc_free(byte *path, dword *blocks_total, dword *blocks_free);

BOOLEAN pc_raw_read(int driveno,  byte *buf, dword blockno, dword nblocks, BOOLEAN raw);
BOOLEAN pc_raw_write(int driveno,  byte *buf, dword blockno, dword nblocks, BOOLEAN raw);


/* apickdsk.c */
BOOLEAN pc_check_disk_ex(byte *drive_id, CHKDISK_STATS *pstat, dword chkdsk_opmode, CHKDSK_CONTEXT *pgl, void *scratch_memory, dword scratch_memory_size);
BOOLEAN pc_diskflush(byte *path);                           /* apidiskflush.c */

BOOLEAN pc_diskclose(byte *driveid, BOOLEAN clear_init);
BOOLEAN pc_diskio_free_list(byte *drivename, int listsize, FREELISTINFO *plist, dword startcluster, dword endcluster, dword threshhold);
BOOLEAN pc_diskio_info(byte *drive_name, DRIVE_INFO *pinfo, BOOLEAN extended);
BOOLEAN pc_diskio_runtime_stats(byte *drive_name, DRIVE_RUNTIME_STATS *pstats, BOOLEAN clear);
int pc_get_file_extents(int fd, int infolistsize, FILESEGINFO *plist, BOOLEAN report_clusters, BOOLEAN raw);
void pc_calculate_chs(dword total, dword *cylinders, int *heads, int *secptrack);/* apifrmat.c */
void pc_gdone(DSTAT *statobj);
BOOLEAN pc_gread(DSTAT *statobj, int blocks_to_read, byte *buffer, int *blocks_read);
BOOLEAN pc_set_default_drive(byte *drive);                  /* apiinfo.c */
BOOLEAN pc_blocks_free(byte *path, dword *blocks_total, dword *blocks_free);
int pc_cluster_size(byte *drive);
int pc_sector_size(byte *drive);

void pc_drno_to_drname(int driveno, byte *pdrive_name, int use_charset);
int pc_drname_to_drno(byte *drive_name, int use_charset);
int pc_fd_to_driveno(int fd,byte *pdrive_name, int use_charset);
BOOLEAN pc_ertfs_init(void);                                /* apiinit.c */
BOOLEAN pc_regression_test(byte *driveid, BOOLEAN do_clean);/* apiregress.c */
void pc_finode_stat(FINODE *pi, ERTFS_STAT *pstat);
void rtfs_print_string_1(byte *pstring ,int flags);           /* rttermin.c */
void rtfs_print_string_2(byte *pstring,byte *pstr2, int flags);
void rtfs_print_long_1(dword l,int flags);
void rtfs_print_one_string(byte *pstr,int flags);
byte *pc_ltoa(dword num, byte *dest, int number_base);


#if (INCLUDE_CS_UNICODE)
void map_jis_ascii_to_unicode(byte *unicode_to, byte *ascii_from);
BOOLEAN map_unicode_to_jis_ascii(byte *to, byte *from);
#endif

void rtfs_cs_char_copy(byte *to, byte *from, int use_charset);
byte *rtfs_cs_increment(byte *p, int use_charset);
int rtfs_cs_compare(byte *p1, byte *p2, int use_charset);
int rtfs_cs_compare_nc(byte *p1, byte *p2, int use_charset);
int rtfs_cs_ascii_index(byte *p, byte base, int use_charset);
void rtfs_cs_to_unicode(byte *to, byte *p, int use_charset);
void rtfs_cs_unicode_to_cs(byte *to, byte *punicode, int use_charset);
BOOLEAN rtfs_cs_is_eos(byte *p, int use_charset);
BOOLEAN rtfs_cs_is_not_eos(byte *p, int use_charset);
void rtfs_cs_term_string(byte *p, int use_charset);
int rtfs_cs_cmp_to_ascii_char(byte *p, byte c, int use_charset);
void rtfs_cs_assign_ascii_char(byte *p, byte c, int use_charset);
byte *rtfs_cs_goto_eos(byte *p, int use_charset);
BOOLEAN rtfs_cs_ascii_fileparse(byte *filename, byte *fileext, byte *p);
byte *pc_cs_mfile(byte *to, byte *filename, byte *ext, int use_charset);
byte *pc_cs_mfileNT(byte *to, byte *filename, byte *ext, int use_charset, byte ntflags);
byte *rtfs_cs_strcat(byte * targ, byte * src, int use_charset);
int rtfs_cs_strcmp(byte * s1, byte * s2, int use_charset);
int rtfs_cs_strcpy(byte * targ, byte * src, int use_charset);
int rtfs_cs_strlen(byte * string, int use_charset);
void pc_cs_ascii_str2upper(byte *to, byte *from);
void pc_cs_ascii_strn2upper(byte *to, byte *from, int n);
BOOLEAN pc_cs_malias(byte *alias, byte *input_file, int which_try, int use_charset);
BOOLEAN pc_cs_valid_sfn(byte *filename, BOOLEAN case_sensitive);
BOOLEAN pc_cs_validate_filename(byte * name, byte * ext, int use_charset);
BOOLEAN pc_cs_validate_8_3_name(byte * name,int len);
BOOLEAN rtfs_cs_patcmp_8(byte *p, byte *pattern, BOOLEAN dowildcard);


dword pc_finode_cluster(DDRIVE *pdr, FINODE *finode);       /* rtcomonglue.c */
void pc_pfinode_cluster(DDRIVE *pdr, FINODE *finode, dword value);
dword pc_get_parent_cluster(DDRIVE *pdrive, DROBJ *pobj);
dword pc_alloc_dir(DDRIVE *pdrive, DROBJ *pmom);
dword pc_grow_dir(DDRIVE *pdrive, DROBJ *pobj, dword *previous_end);
int _pc_file_open(DDRIVE *pdrive, byte *name, word flag, word mode, dword extended, BOOLEAN *created, int use_charset);
BOOLEAN pc_alloc_path_buffers(DDRIVE *pdrive);
void pc_release_path_buffers(DDRIVE *pdrive);
PC_FILE *pc_fd2file(int fd,int flags);
PC_FILE *pc_allocfile(void);
void pc_freefile(PC_FILE *pfile);

BOOLEAN pc_load_file_buffer(FINODE *pfinode, dword new_blockno, BOOLEAN read_buffer);
void pc_release_file_buffer(BLKBUFF *pblk);
BOOLEAN pc_buffered_fileio(FINODE *pfinode, dword start_sector, dword start_byte_offset, dword n_todo, byte *pdata,  BOOLEAN reading, BOOLEAN appending);
BOOLEAN pc_flush_file_buffer(FINODE *pfinode);
dword pc_get_media_sector_size(DDRIVE *pdr);

int pc_enum_file(DDRIVE *pdrive, int chore);
void pc_free_all_fil(DDRIVE *pdrive);
BOOLEAN pc_flush_all_fil(DDRIVE *pdrive);
void pc_release_all_prealloc(DDRIVE *pdrive);
int pc_test_all_fil(DDRIVE *pdrive);
BOOLEAN pc_init_drv_fat_info16(DDRIVE *pdr, struct pcblk0 *pbl0);
dword pc_byte2clmod(DDRIVE *pdr, dword nbytes);
dword pc_alloced_bytes_from_clusters(DDRIVE *pdr, dword total_alloced_clusters);
void pc_set_file_dirty(PC_FILE * pfile, BOOLEAN isdirty);                /* rtconditionalglue.c */
byte *pc_claim_user_buffer(DDRIVE *pdr, dword *pbuffer_size_blocks, dword minimimum_size_sectors);
void pc_release_user_buffer(DDRIVE *pdr, byte *pbuffer);
BOOLEAN pc_check_file_dirty(PC_FILE * pfile);
void pc_set_file_buffer_dirty(FINODE *pfinode, BOOLEAN isdirty);
BOOLEAN pc_check_file_buffer_dirty(FINODE *pfinode);
void set_fat_dirty(DDRIVE *pdr);
void clear_fat_dirty(DDRIVE *pdr);
BOOLEAN chk_fat_dirty(DDRIVE *pdr);
void set_mount_abort_status(DDRIVE *pdr);
void clear_mount_valid(DDRIVE *pdr);
void clear_mount_abort(DDRIVE *pdr);
void set_mount_valid(DDRIVE *pdr);
BOOLEAN chk_mount_abort(DDRIVE *pdr);
BOOLEAN chk_mount_valid(DDRIVE *pdr);
BOOLEAN pc_update_inode(DROBJ *pobj, BOOLEAN set_archive, int set_date_mask);
FINODE *pc_file2_finode(PC_FILE * pfile);
BOOLEAN pc_force_file_flush(PC_FILE * pfile);
BOOLEAN block_devio_read(DDRIVE *pdrive, dword blockno, byte * buf);  /* rtdblock.c */
BOOLEAN block_devio_write(BLKBUFF *pblk);
BOOLEAN block_devio_xfer(DDRIVE *pdrive, dword blockno, byte * buf, dword n_to_xfer, BOOLEAN reading);
BLKBUFF *pc_allocate_blk(DDRIVE *pdrive, BLKBUFFCNTXT *pbuffcntxt);
void pc_release_buf(BLKBUFF *pblk);
void pc_discard_buf(BLKBUFF *pblk);
BLKBUFF *pc_read_blk(DDRIVE *pdrive, dword blockno);
BLKBUFF *pc_sys_sector(DDRIVE *pdr, BLKBUFF *pscratch_buff);
BLKBUFF *pc_scratch_blk(void);
void pc_free_sys_sector(BLKBUFF *pblk);
void pc_free_scratch_blk(BLKBUFF *pblk);
BLKBUFF *pc_init_blk(DDRIVE *pdrive, dword blockno);
void pc_free_all_blk(DDRIVE *pdrive);
BOOLEAN pc_write_blk(BLKBUFF *pblk);
BLKBUFF *pc_find_blk(DDRIVE *pdrive, dword blockno);
void pc_flush_chain_blk(DDRIVE *pdrive, dword cluster);
void pc_initialize_block_pool(BLKBUFFCNTXT *pbuffcntxt, int nblkbuffs, BLKBUFF *pmem_block_pool, byte *raw_buffer_space, dword data_size_bytes);
void debug_check_blocks(BLKBUFFCNTXT *pbuffcntxt, int numblocks,  char *where, dword line);
void display_free_lists(char *in_where);
void check_blocks(DDRIVE *pdrive, char *prompt, dword line);

int check_drive_name_mount(byte *name, int use_charset);                             /* rtdevio.c */
BOOLEAN check_drive_number_mount(int driveno);
DDRIVE *check_drive_by_name(byte *name, int use_charset);
DDRIVE *check_drive_by_number(int driveno, BOOLEAN check_mount);
void release_drive_mount(int driveno);
BOOLEAN release_drive_mount_write(int driveno);
BOOLEAN raw_devio_xfer(DDRIVE *pdr, dword blockno, byte * buf, dword n_to_xfer, BOOLEAN raw, BOOLEAN reading);

DROBJ *_pc_get_user_cwd(DDRIVE *pdrive);                            /* rtdrobj.c */
DROBJ *pc_fndnode(byte *path, int use_charset);
DROBJ *pc_get_inode( DROBJ *pobj, DROBJ *pmom, byte *filename, byte *fileext, int action, int use_charset);
DROBJ *pc_rget_inode( DROBJ *pobj, DROBJ *pmom, byte *filename, byte *fileext, int action, int use_charset);
DROBJ *pc_get_mom(DROBJ *pdotdot);
DROBJ *pc_mkchild( DROBJ *pmom);

#if (INCLUDE_EXFATORFAT64)
/* Note: This is a new way of doing things for all. Leave conditional until testing is completer */
DROBJ *pc_mknode(DROBJ *pmom ,byte *filename, byte *fileext, byte attributes, FINODE *infinode,int use_charset);
#else
DROBJ *pc_mknode(DROBJ *pmom ,byte *filename, byte *fileext, byte attributes, dword incluster,int use_charset);
#endif

void pc_init_directory_cluster(DROBJ *pobj, byte *pbuffer);
BOOLEAN pc_rmnode( DROBJ *pobj);
void pc_update_finode_datestamp(FINODE *pfinode, BOOLEAN set_archive, int set_date_mask);
BOOLEAN pc_update_by_finode(FINODE *pfinode, int entry_index, BOOLEAN set_archive, int set_date_mask);
void pc_init_inode(FINODE *pdir, KS_CONSTANT byte *filename,
            KS_CONSTANT byte *fileext, byte attr,
            dword cluster, dword size, DATESTR *crdate);
void pc_ino2dos (DOSINODE *pbuff, FINODE *pdir);
DROBJ *pc_get_root( DDRIVE *pdrive);
dword pc_firstblock( DROBJ *pobj);
BOOLEAN pc_next_block( DROBJ *pobj);
dword pc_l_next_block(DDRIVE *pdrive, dword curblock);
void pc_marki( FINODE *pfi, DDRIVE *pdrive, dword sectorno, int  index);
FINODE *pc_scani( DDRIVE *pdrive, dword sectorno, int index);
DROBJ *pc_allocobj(void);
FINODE *pc_alloci(void);
void pc_free_all_drobj( DDRIVE *pdrive);
void pc_free_all_i( DDRIVE *pdrive);
void pc_freei(FINODE *pfi);
void pc_freeobj( DROBJ *pobj);
void pc_dos2inode (FINODE *pdir, DOSINODE *pbuff);
BOOLEAN pc_isavol( DROBJ *pobj);
BOOLEAN pc_isadir( DROBJ *pobj);
BOOLEAN pc_isroot( DROBJ *pobj);

BOOLEAN pc_init_drv_fat_info32(DDRIVE *pdr, struct pcblk0 *pbl0);           /* rtfat32.c */
BOOLEAN pc_mkfs32(int driveno, FMTPARMS *pfmt, BOOLEAN use_raw);
BOOLEAN pc_gblk0_32(DDRIVE *pdr, struct pcblk0 *pbl0, byte *b);
BOOLEAN pc_validate_partition_type(byte p_type);
BOOLEAN fat_flushinfo(DDRIVE *pdr);

/* rtfatdrvrd.c */
BOOLEAN fatop_check_freespace(DDRIVE *pdr);
void fatop_close_driver(DDRIVE *pdr);
BOOLEAN fatop_get_cluster(DDRIVE *pdr, dword clno, dword *pvalue);
dword fatop_next_cluster(DDRIVE *pdr, dword cluster);
BOOLEAN fatop_add_free_region(DDRIVE *pdr, dword cluster, dword ncontig, BOOLEAN do_erase);
dword fatop_get_frag_async(DDRIVE *pdr, byte *palt_buffer, dword alt_buffer_size, dword startpt, dword *pnext_cluster, dword n_clusters, int *end_of_chain);
dword fatop_get_frag(DDRIVE *pdr, byte *palt_buffer, dword alt_buffer_size, dword startpt, dword *pnext_cluster, dword n_clusters, int *end_of_chain);
int fatop_page_continue_check_freespace(DDRIVE *pdr);
void fatop_page_start_check_freespace(DDRIVE *pdr);
byte * fatxx_pfswap(DDRIVE *pdr, dword index, BOOLEAN for_write);
void fatop_get_page_masks(DDRIVE *pdr, dword *mask_offset, dword *mask_cl_page,  dword *cl_per_block, dword *cl_to_page_shift, dword * bytes_per_entry);
BOOLEAN fatxx_fword(DDRIVE *pdr, dword index, word *pvalue, BOOLEAN putting);

/* rtfatdrvwr.c */
dword fatop_alloc_chain(DDRIVE *pdr, BOOLEAN is_file, dword hint_cluster, dword previous_end, dword *pfirst_new_cluster, dword n_clusters, dword min_clusters);
dword fatop_grow_dir(DDRIVE *pdr, dword previous_end);
dword fatop_alloc_dir(DDRIVE *pdr, dword clhint);
dword  fatop_clgrow(DDRIVE *pdr, dword  clno, dword *previous_end);
void fatop_truncate_dir(DDRIVE *pdr, DROBJ *pobj, dword cluster, dword previous_end);
void fatop_clrelease_dir(DDRIVE *pdr, dword  clno);
BOOLEAN fatop_freechain(DDRIVE *pdr, dword cluster, dword max_clusters_to_free);
BOOLEAN fatop_free_frag(DDRIVE *pdr, dword flags, dword prev_cluster, dword startpt, dword n_contig);
BOOLEAN fatop_remove_free_region(DDRIVE *pdr, dword cluster, dword ncontig);
BOOLEAN fatop_flushfat(DDRIVE *pdr);
BOOLEAN fatop_link_frag(DDRIVE *pdr, byte *palt_buffer, dword alt_buffer_size,  dword flags, dword cluster, dword startpt, dword n_contig);
dword fatop_find_contiguous_free_clusters(DDRIVE *pdr, dword startpt, dword endpt, dword min_clusters, dword max_clusters, dword *p_contig, int *is_error, int allocation_scheme);
dword _fatop_find_contiguous_free_clusters(DDRIVE *pdr, dword startpt, dword endpt, dword min_clusters, dword max_clusters, dword *p_contig, int *is_error);
BOOLEAN fatop_pfaxxterm(DDRIVE   *pdr, dword  clno);
BOOLEAN fatop_pfaxx(DDRIVE *pdr, dword  clno, dword  value);

#if (INCLUDE_NAND_DRIVER)
/* rteraseblock.c */
BOOLEAN eraseop_erase_blocks(DDRIVE *pdr, dword cluster, dword ncontig);
dword eraseop_find_free_clusters(DDRIVE *pdr, dword startpt, dword endpt, dword min_clusters, dword max_clusters, int allocation_scheme, dword *p_contig, int *is_error);
#endif
/* drdynamic.c */
void pc_rtfs_poll_devices_ready(void);
BOOLEAN pc_rtfs_media_remount(void *devicehandle);
void pc_rtfs_regen_insert_parms(struct rtfs_media_insert_args *prtfs_insert_parms, RTFS_DEVI_MEDIA_PARMS *pmedia_info);
byte *pc_rtfs_iomalloc(dword alloc_size, void **paligned_data);
BOOLEAN rtfs_dynamic_init_configuration(struct rtfs_init_resource_reply *preply);
BOOLEAN RTFS_DEVI_process_mount_parms(DDRIVE *pdr);
BLKBUFF *RTFS_DEVI_alloc_filebuffer(DDRIVE *pdr);
void RTFS_DEVI_release_filebuffer(BLKBUFF *pblk);

BOOLEAN fat_devio_write(DDRIVE *pdrive, dword fat_blockno, dword nblocks, byte *fat_data, int fatnumber);   /* rtfblock.c */
BOOLEAN fat_devio_read(DDRIVE *pdrive, dword blockno,dword nblocks, byte *fat_data);
BOOLEAN pc_flush_fat_blocks(DDRIVE *pdrive);
BOOLEAN pc_write_fat_blocks(DDRIVE *pdrive,dword fat_blockno, dword nblocks, byte *fat_data,dword which_copy);
BOOLEAN pc_write_fat_block_buffer_page(DDRIVE *pdrive, FATBUFF *pblk);
int pc_read_fat_blocks (DDRIVE* pdrive, byte* buffer, dword first_block, dword num_blocks);
BOOLEAN pc_initialize_fat_block_pool(FATBUFFCNTXT *pfatbuffcntxt,
            int fat_buffer_size, FATBUFF *pfat_buffers,
            int fat_hashtbl_size,   FATBUFF **pfat_hash_table,
            byte **pfat_primary_cache, dword *pfat_primary_index);
void pc_free_all_fat_buffers(DDRIVE *pdrive);
byte *pc_map_fat_stream(DDRIVE *pdrive, dword *return_block_count, dword first_block, dword byte_offset, dword byte_count, byte *alt_buffer, dword alt_buffer_size, BOOLEAN is_a_read);
byte *pc_map_fat_sector(DDRIVE *pdrive, FATBUFFCNTXT *pfatbuffcntxt,dword blockno, int n_dirty, dword usage_flags);
int pc_async_flush_fat_blocks(DDRIVE *pdrive,dword max_flushes_per_pass);

void pc_set_mbuff_dirty(PCMBUFF *pcmb, dword block_offset, int block_count);
void pc_zero_mbuff_dirty(PCMBUFF *pcmb);

BOOLEAN rtfs_resource_init(void);                                                   /* rtkernfn.c */
void pc_rtfs_register_poll_devices_ready_handler(RTFS_DEVI_POLL_REQUEST_VECTOR *poll_device_vector, void (*poll_device_ready)(void));
DDRIVE *rtfs_claim_media_and_buffers(int driveno);
void rtfs_release_media_and_buffers(int driveno);

DROBJ *rtfs_get_user_pwd(RTFS_SYSTEM_USER *pu,int driveno, BOOLEAN doclear);
void  rtfs_set_user_pwd(RTFS_SYSTEM_USER *pu, DROBJ *pobj);
PRTFS_SYSTEM_USER rtfs_get_system_user(void);
void  pc_free_user(void);
void  pc_free_other_user(dword taskId);
void  pc_free_all_users(int driveno);
void rtfs_set_driver_errno(dword error);
dword rtfs_get_driver_errno(void);
void rtfs_clear_errno(void);
void _rtfs_set_errno(int error);
void _debug_rtfs_set_errno(int error, char *filename, long linenumber);

int get_errno(void);
int pc_num_drives(void);
int pc_num_users(void);
int pc_nuserfiles(void);
BOOLEAN pc_validate_driveno(int driveno);
BOOLEAN pc_memory_init(void);
DDRIVE *pc_memory_ddrive(DDRIVE *pdrive);
DROBJ *pc_memory_drobj(DROBJ *pobj);
FINODE *pc_memory_finode(FINODE *pinode);
void rtfs_kern_gets(byte *buffer);
void rtfs_kern_puts(byte *buffer);
int rtfs_debug_zero(void);
void rtfs_assert_break(void);
void rtfs_assert_test(void);
BOOLEAN pc_i_dskopen(int driveno, BOOLEAN is_async);                                /* rtlowl.c */
void pc_drive_scaleto_blocksize(DDRIVE *pdr, int bytspsector);
BOOLEAN  pc_auto_dskopen(DDRIVE *pdr);
void pc_i_dskopen_complete(DDRIVE *pdr);
int pc_read_partition_table(DDRIVE *pdr, struct mbr_entry_specification *pspec);
BOOLEAN pc_gblk0(DDRIVE *pdr, struct pcblk0 *pbl0);
BOOLEAN pc_clzero(DDRIVE *pdrive, dword cluster);
DDRIVE *pc_drno_to_drive_struct(int driveno);
DDRIVE  *pc_drno2dr(int driveno);
BOOLEAN pc_dskfree(int driveno);
dword pc_sec2cluster(DDRIVE *pdrive, dword blockno);
dword pc_sec2index(DDRIVE *pdrive, dword blockno);
dword pc_cl2sector(DDRIVE *pdrive, dword cluster);

/* rtvfatrd.c rtnvfatrd.c */
BOOLEAN pc_findin( DROBJ *pobj, byte *filename, byte *fileext, int action, int use_charset);
BOOLEAN pc_rfindin( DROBJ *pobj, byte *filename, int action, int use_charset, BOOLEAN starting);
byte *pc_nibbleparse(byte *filename, byte *fileext, byte *path, int use_charset);
BOOLEAN pc_parsepath(byte *topath, byte *filename, byte *fileext, byte *path, int use_charset);
BOOLEAN pc_patcmp_vfat(byte *in_pat, byte *name, BOOLEAN dowildcard, int use_charset);
byte pc_cksum(byte *test);
BOOLEAN pc_isdot(byte *fname, byte *fext);
BOOLEAN pc_isdotdot(byte *fname, byte *fext);
BOOLEAN pc_get_lfn_filename(DROBJ *pobj, byte *path, int use_charset);
BOOLEAN pc_multi_dir_get(DROBJ *pobj, BLKBUFF **pscan_buff, byte **pscan_data, byte *puser_buffer, dword *n_blocks, BOOLEAN do_increment);


/* rtvfatwr.c and rtnvfatwr.c */
BOOLEAN pc_insert_inode(DROBJ *pobj , DROBJ *pmom, byte attr, dword initcluster, byte *filename, byte *fileext, int use_charset);
void pc_zero_lfn_info(FINODE *pdir);
BOOLEAN _illegal_lfn_char(byte ch);
BOOLEAN pc_delete_lfn_info(DROBJ *pobj);

/* rtfreemanager.c */
BOOLEAN free_manager_attach(DDRIVE *pdr);
BOOLEAN free_manager_is_attached(DDRIVE *pdr);
dword   free_manager_count_frags(DDRIVE *pdr);
void free_manager_close(DDRIVE *pdr);
void free_manager_revert(DDRIVE *pdr);
dword free_manager_find_contiguous_free_clusters(DDRIVE *pdr, dword startpt, dword endpt, dword min_clusters, dword max_clusters, dword *p_contig, int *is_error);
BOOLEAN free_manager_release_clusters(DDRIVE *pdr, dword cluster, dword ncontig, BOOLEAN do_erase);
void free_manager_claim_clusters(DDRIVE *pdr, dword cluster, dword ncontig);

/* rtfragmt.c */
void pc_fraglist_init_freelist(void);
dword pc_fraglist_count_clusters(REGION_FRAGMENT *pf,REGION_FRAGMENT *pfend);
REGION_FRAGMENT *pc_end_fragment_chain(REGION_FRAGMENT *pf);
REGION_FRAGMENT *pc_fraglist_frag_alloc(DDRIVE *pdr, dword frag_start,
                                   dword frag_end,
                                   REGION_FRAGMENT *pnext);
void pc_fraglist_frag_free(REGION_FRAGMENT *pf);
void pc_fraglist_free_list(REGION_FRAGMENT *pf);
REGION_FRAGMENT  *pc_fraglist_set_range(REGION_FRAGMENT *inpf, dword start_location, dword end_location, int *is_error);
REGION_FRAGMENT  *pc_fraglist_clear_range(REGION_FRAGMENT *inpf, dword start_location, dword end_location, int *is_error);
dword pc_fragment_size_32(DDRIVE *pdr, REGION_FRAGMENT *pf);
BOOLEAN pc_grow_basic_fragment(FINODE *pefinode, dword first_new_cluster, dword nclusters);

/* rtutbyte.c */
BOOLEAN pc_check_dir_end(byte *fname);
byte *pc_strchr(byte *string, byte ch);
BOOLEAN _illegal_alias_char(byte ch);
void copybuff(void *vto, void *vfrom, int size);
void pc_cppad(byte *to, byte *from, int size);
void rtfs_memset(void *pv, byte b, int n);
BOOLEAN rtfs_bytecomp(byte *p1, byte *p2, int n);
int pc_path_to_driveno(byte  *path, int use_charset);                                            /* rtutil.c */
int pc_parse_raw_drive(byte  *path, int use_charset);
byte *pc_parsedrive(int *driveno, byte  *path, int use_charset);
byte *pc_mpath(byte *to, byte *path, byte *filename, int use_charset);
BOOLEAN pc_search_csl(byte *set, byte *string);
BOOLEAN name_is_reserved(byte *filename);
void RTFS_ARGSUSED_PVOID(void * p);
void RTFS_ARGSUSED_INT(int i);
void RTFS_ARGSUSED_DWORD(dword l);
dword to_DWORD( byte *from);
word to_WORD( byte *from);
void fr_WORD( byte *to,  word from);
void fr_DWORD( byte *to,  dword from);

#if (INCLUDE_MATH64)
void to_DDWORD(ddword *to, byte *from);
void fr_DDWORD( byte *to,  ddword from);
ddword m64_native_set32(dword a, dword b);
ddword m64_lshift(ddword b, dword c);
ddword m64_rshift(ddword b, dword c);
ddword m64_minus(ddword b, ddword c);
ddword m64_minus32(ddword b, dword c);
ddword m64_plus(ddword b, ddword c);
ddword m64_plus32(ddword b, dword c);
ddword m64_set32(dword hi, dword lo);
#endif

void *rtfs_port_malloc(int nbytes);       /* portkern.c (target specific) */
void rtfs_port_free(void *pbytes);
dword rtfs_port_alloc_mutex(char *mutex_name);
void rtfs_port_free_mutex(dword handle);
void rtfs_port_claim_mutex(dword handle);
void rtfs_port_release_mutex(dword handle);
dword rtfs_port_alloc_signal(void);
void  rtfs_port_shutdown(void);
void rtfs_port_set_task_env(void *pusercontext);
void *rtfs_port_get_task_env(dword taskId);
void rtfs_port_clear_signal(dword handle);
int rtfs_port_test_signal(dword handle, int timeout);
void rtfs_port_set_signal(dword handle);
void rtfs_port_sleep(int sleeptime);
dword rtfs_port_elapsed_zero(void);
int rtfs_port_elapsed_check(dword zero_val, int timeout);
dword rtfs_port_get_taskid(void);
void rtfs_port_disable(void);
void rtfs_port_enable(void);
void rtfs_port_exit(void);
DATESTR *pc_getsysdate(DATESTR * pd);


/* In prefiocom but should be global for Pro */
dword truncate_32_count(dword file_pointer, dword count);
dword truncate_32_sum(dword val1, dword val2);


#ifdef __cplusplus
}
#endif
/* ===========================  */
/* End Rtfs internal prototypes */
/* ===========================  */

#define rtfs_set_errno(ERROR,CALLER, LINENUMBER) _debug_rtfs_set_errno(ERROR, CALLER, LINENUMBER)

#if (INCLUDE_DEBUG_RUNTIME_STATS)
#define UPDATE_RUNTIME_STATS(DRIVE, FIELD, COUNT) DRIVE->drive_rtstats.FIELD += COUNT;
#else
#define UPDATE_RUNTIME_STATS(DRIVE, FIELD, COUNT)
#endif

#if (INCLUDE_CS_UNICODE)
/* Unicode API */
#define pc_mv_uc(A,B) pc_mv_cs((A),(B),CS_CHARSET_UNICODE)
#define pc_get_volume_uc(A,B) pc_get_volume_cs((A),(B),CS_CHARSET_UNICODE)
#define pc_set_volume_uc(A,B) pc_set_volume_cs((A),(B),CS_CHARSET_UNICODE)
#define pc_get_cwd_uc(A,B) pc_get_cwd_cs((A),(B),CS_CHARSET_UNICODE)
#define pc_pwd_uc(A,B) pc_pwd_cs((A),(B),CS_CHARSET_UNICODE)
#define pc_gfirst_uc(A,B) pc_gfirst_cs((A),(B),CS_CHARSET_UNICODE)
#define pc_glast_uc(A,B) pc_glast_cs((A),(B),CS_CHARSET_UNICODE)
#define pc_gnext_uc(A) pc_gnext_cs((A),CS_CHARSET_UNICODE)
#define pc_gprev_uc(A) pc_gprev_cs((A),CS_CHARSET_UNICODE)
#define pc_isdir_uc(A) pc_isdir_cs((A),CS_CHARSET_UNICODE)
#define pc_isvol_uc(A) pc_isvol_cs((A),CS_CHARSET_UNICODE)
#define pc_get_attributes_uc(A,B) pc_get_attributes_cs((A),(B),CS_CHARSET_UNICODE)
#define pc_mkdir_uc(A) pc_mkdir_cs((A),CS_CHARSET_UNICODE)
#define pc_rmdir_uc(A) pc_rmdir_cs((A),CS_CHARSET_UNICODE)
#define pc_set_attributes_uc(A,B) pc_set_attributes_cs((A),(B),CS_CHARSET_UNICODE)
#define pc_set_cwd_uc(A) pc_set_cwd_cs((A),CS_CHARSET_UNICODE)
#define pc_stat_uc(A,B) pc_stat_cs((A),(B),CS_CHARSET_UNICODE)
#define pc_unlink_uc(A) pc_unlink_cs((A),CS_CHARSET_UNICODE)
#define po_open_uc(A,B,C) po_open_cs((A),(B),(C), CS_CHARSET_UNICODE)
#define pc_get_default_drive_uc(A) pc_get_default_drive_cs((A), CS_CHARSET_UNICODE)
#define pc_deltree_uc(A) pc_deltree_cs((A),CS_CHARSET_UNICODE)
#define pc_enumerate_uc(A,B,C,D,E,F,G,H,I) pc_enumerate_cs((A),(B),(C),(D),(E),(F),(G),(H),(I),CS_CHARSET_UNICODE)
#define pc_get_dirent_info_uc(A,B) pc_get_dirent_info_cs((A),(B),CS_CHARSET_UNICODE)
#define pc_set_dirent_info_uc(A,B) pc_set_dirent_info_cs((A),(B),CS_CHARSET_UNICODE)
#define pc_get_media_parms_uc(A,B) pc_get_media_parms_cs((A),(B),CS_CHARSET_UNICODE)
#define pc_format_media_uc(A) pc_format_media_cs((A),CS_CHARSET_UNICODE)
#define pc_format_volume_uc(A) pc_format_volume_cs((A),CS_CHARSET_UNICODE)
#define pc_format_volume_ex_uc(A, B) pc_format_volume_ex_cs((A), (B), CS_CHARSET_UNICODE)

#define pc_partition_media_uc(A,B) pc_partition_media_cs((A),(B), CS_CHARSET_UNICODE)

/* Non-Unicode API */
#define pc_mv(A,B) pc_mv_cs((A),(B),CS_CHARSET_NOT_UNICODE)
#define pc_get_volume(A,B) pc_get_volume_cs((A),(B),CS_CHARSET_NOT_UNICODE)
#define pc_set_volume(A,B) pc_set_volume_cs((A),(B),CS_CHARSET_NOT_UNICODE)
#define pc_get_cwd(A,B) pc_get_cwd_cs((A),(B),CS_CHARSET_NOT_UNICODE)
#define pc_pwd(A,B) pc_pwd_cs((A),(B),CS_CHARSET_NOT_UNICODE)
#define pc_gfirst(A,B) pc_gfirst_cs((A),(B),CS_CHARSET_NOT_UNICODE)
#define pc_glast(A,B) pc_glast_cs((A),(B),CS_CHARSET_NOT_UNICODE)
#define pc_gnext(A) pc_gnext_cs((A) ,CS_CHARSET_NOT_UNICODE)
#define pc_gprev(A) pc_gprev_cs((A) ,CS_CHARSET_NOT_UNICODE)
#define pc_isdir(A) pc_isdir_cs((A),CS_CHARSET_NOT_UNICODE)
#define pc_isvol(A) pc_isvol_cs((A),CS_CHARSET_NOT_UNICODE)
#define pc_get_attributes(A,B) pc_get_attributes_cs((A),(B),CS_CHARSET_NOT_UNICODE)
#define pc_mkdir(A) pc_mkdir_cs((A),CS_CHARSET_NOT_UNICODE)
#define pc_rmdir(A) pc_rmdir_cs((A),CS_CHARSET_NOT_UNICODE)
#define pc_set_attributes(A,B) pc_set_attributes_cs((A), (B),CS_CHARSET_NOT_UNICODE)
#define pc_set_cwd(A) pc_set_cwd_cs((A),CS_CHARSET_NOT_UNICODE)
#define pc_stat(A,B) pc_stat_cs((A),(B),CS_CHARSET_NOT_UNICODE)
#define pc_unlink(A) pc_unlink_cs((A),CS_CHARSET_NOT_UNICODE)
#define po_open(A,B,C) po_open_cs((A),(B),(C), CS_CHARSET_NOT_UNICODE)
#define pc_get_default_drive(A) pc_get_default_drive_cs((A), CS_CHARSET_NOT_UNICODE)
#define pc_deltree(A) pc_deltree_cs((A),CS_CHARSET_NOT_UNICODE)
#define pc_enumerate(A,B,C,D,E,F,G,H,I) pc_enumerate_cs((A),(B),(C),(D),(E),(F),(G),(H),(I),CS_CHARSET_NOT_UNICODE)
#define pc_get_dirent_info(A,B) pc_get_dirent_info_cs((A),(B),CS_CHARSET_NOT_UNICODE)
#define pc_set_dirent_info(A,B) pc_set_dirent_info_cs((A),(B),CS_CHARSET_NOT_UNICODE)
#define pc_get_media_parms(A,B) pc_get_media_parms_cs((A),(B),CS_CHARSET_NOT_UNICODE)
#define pc_format_media(A) pc_format_media_cs((A),CS_CHARSET_NOT_UNICODE)
#define pc_format_volume(A) pc_format_volume_cs((A),CS_CHARSET_NOT_UNICODE)
#define pc_format_volume_ex(A, B) pc_format_volume_ex_cs((A), (B), CS_CHARSET_NOT_UNICODE)
#define pc_partition_media(A,B) pc_partition_media_cs((A),(B), CS_CHARSET_NOT_UNICODE)

#endif



#if (INCLUDE_CS_UNICODE)
#define CS_CHARSET_ARGS use_charset /* Extracts character set from passed parameters */
#else
#define CS_CHARSET_ARGS CS_CHARSET_NOT_UNICODE  /* Default character set do not change */
#endif

#if (INCLUDE_RTFS_PROPLUS) /* Include additional ProPlus internal declarations */

/* ===========================  */
/* RtfsProPlus prototypes     */
/* ===========================  */
#ifdef __cplusplus
extern "C" {
#endif

/* ============================  */
/* ProPlus  API Prototypes       */
/* ============================  */

#if (INCLUDE_CS_UNICODE)
/* Prototypes of unicode / Non-Unicode selectable APIS */
int pc_cfilio_open_cs(byte *name, word flag, EFILEOPTIONS *poptions,int use_charset);
int pc_efilio_async_unlink_start_cs(byte *filename,int use_charset);
int pc_efilio_open_cs(byte *name, word flag, word mode, EFILEOPTIONS *poptions, int use_charset);
#else
/* Prototypes of Non-Unicode APIS */
int pc_cfilio_open(byte *name, word flag, EFILEOPTIONS *poptions);
int pc_efilio_async_unlink_start(byte *filename);
int pc_efilio_open(byte *name, word flag, word mode, EFILEOPTIONS *poptions);
#endif

/* rtfsproplustests\efishell.c */
void  efio_shell(void);
/* rtfsproplus\prapiasy.c */
int pc_async_continue(int driveno, int target_state, int steps);
int pc_diskio_async_flush_start(byte *path);
int pc_diskio_async_mount_start(byte *diskid);
int pc_efilio_async_flush_start(int fd);
int pc_efilio_async_close_start(int fd);
/* rtfsproplus\prapiefi.c */
BOOLEAN pc_efilio_settime(int fd,word new_time, word new_date);
BOOLEAN pc_efilio_flush(int fd);
BOOLEAN pc_efilio_lseek(int fd, dword offset_hi, dword offset_lo, int origin, dword *pnewoffset_hi, dword *pnewoffset_lo);
#if (INCLUDE_MATH64)
ddword pc_efilio_lseek64(int fd, ddword offset, int origin);
#endif
BOOLEAN pc_efilio_chsize(int fd, dword newsize_hi, dword newsize_lo);
BOOLEAN pc_efilio_read(int fd, byte *buf, dword count, dword *nread);
BOOLEAN pc_efilio_write(int fd, byte *buff, dword count, dword *nwritten);
BOOLEAN pc_efilio_setalloc(int fd, dword cluster, dword reserve_count);
BOOLEAN pc_efilio_setbuff(byte *path, byte *buffer, dword buffer_size_blocks);
int pc_efilio_get_file_extents(int fd, int infolistsize, FILESEGINFO *plist, BOOLEAN report_clusters, BOOLEAN raw);

/* rtfsproplus\prapilinext.c */
BOOLEAN pc_efilio_extract(int fd1, int fd2, dword n_clusters);
BOOLEAN pc_efilio_swap(int fd1, int fd2, dword n_clusters);
BOOLEAN pc_efilio_remove(int fd1,  dword n_clusters);


/* rtfsproplus\prfastio.c */
BOOLEAN pc_efilio_fpos_sector(int fd, BOOLEAN isreadfp, BOOLEAN raw, dword *psectorno, dword *psectorcount);

/* rtfsproplus\proplusglue.c */
BOOLEAN pc_bytes_to_clusters(int driveno, dword bytes_hi, dword bytes_lo, dword *pcluster_mod);
BOOLEAN pc_clusters_to_bytes(int driveno, dword n_clusters, dword *pbytes_hi, dword *pbytes_lo);
void pc_subtract_64(dword hi_1, dword lo_1, dword hi_0, dword lo_0, dword *presult_hi, dword *presult_lo);
void pc_add_64(dword hi_1, dword lo_1, dword hi_0, dword lo_0, dword *presult_hi, dword *presult_lo);
dword pc_cluster_to_sector(int driveno, dword cluster, BOOLEAN raw);
dword pc_sector_to_cluster(int driveno, dword sector, BOOLEAN raw);
void pc_clusters2bytes64(DDRIVE *pdr, dword n_clusters, dword *pbytes_hi, dword *pbytes_lo);


/* ============================  */
/* End ProPlus  API Prototypes       */
/* ============================  */

/* ============================  */
/* ProPlus  Internal Prototypes  */
/* ============================  */
/* rtfsproplus\prapiasy.c */
BOOLEAN _pc_check_if_async(PC_FILE *pefile);
dword _get_operating_flags(PC_FILE *pefile);
void _set_asy_operating_flags(PC_FILE *pefile, dword new_operating_flags, int success);
/* rtfsproplus\prapicfi.c */
BOOLEAN _pc_cfilio_read(PC_FILE *preaderefile, byte *buff, dword count, dword *nread);
BOOLEAN _pc_cfilio_write(PC_FILE *pwriterefile, byte *buff, dword count, dword *nwritten);
dword _pc_cfilio_get_max_write_count(PC_FILE *pwriterefile, dword count);
dword _pc_cfilio_get_max_read_count(PC_FILE *pefile, dword count);
PC_FILE *pc_cfilio_fd2file(int fd, BOOLEAN unlock);
void pc_cfilio_clear_open_flags(PC_FILE *pefile, word flags);
void pc_cfilio_set_open_flags(PC_FILE *pefile, word flags);
ddword pc_efilio_get_fp_ddw(PC_FILE *pefile);
ddword pc_cfilio_get_fp_ddw(PC_FILE *pefile);
ddword pc_cfilio_get_file_size_ddw(PC_FILE *pefile);
/* rtfsproplus\prapicsk.c */
BOOLEAN _pc_cfilio_lseek(PC_FILE *pwriterefile, int which_pointer, dword offset_hi, dword offset_lo, int origin, dword *poffset_hi, dword *poffset_lo);
BOOLEAN _pc_cstreamio_lseek(PC_FILE *pwriterefile,  int which_pointer, ddword offset_ddw, int origin, ddword *poffset_ddw);
/* rtfsproplus\prapiefi.c */
int _pc_efilio_open(DDRIVE *pdrive, byte *name, word flag, word mode, EFILEOPTIONS *poptions, int use_charset);
BOOLEAN _pc_efilio_flush_file_buffer(PC_FILE *pefile);
BOOLEAN _pc_efilio_flush(PC_FILE *pefile);
void _pc_efilio_free_excess_clusters(PC_FILE *pefile);
BOOLEAN pc_efilio_close(int fd);
PC_FILE *pc_efilio_start_close(int fd, BOOLEAN *is_aborted);
BOOLEAN _pc_efilio_close(PC_FILE *pefile);
void _pc_cfilio_set_file_size(PC_FILE *pefile, dword dstart, dword length_hi, dword length_lo);
dword _pc_efilio_first_cluster(PC_FILE *pefile);
dword _pc_efilio_last_cluster(PC_FILE *pefile);
void _pc_efilio_reset_seek(PC_FILE *pefile);
void _pc_efilio_coalesce_fragments(PC_FILE *pefile);
BOOLEAN _pc_efilio_lseek(PC_FILE *pefile, dword offset_hi, dword offset_lo, int origin, dword *pnewoffset_hi, dword *pnewoffset_lo);
BOOLEAN _pc_efilio_read(PC_FILE *pefile, byte *buf, dword count, dword *nread);
BOOLEAN _pc_efilio_write(PC_FILE *pefile, byte *buff, dword count, dword *nwritten);
BOOLEAN pc_efilio_fstat(int fd, ERTFS_EFILIO_STAT *pestat);
BOOLEAN _pc_efilio_fstat(PC_FILE *pefile, ERTFS_EFILIO_STAT *pestat);
BOOLEAN _pc_check_efile_open_mode(PC_FILE *pefile);

void fatop_page_start_check_freespace(DDRIVE *pdr);

#if (INCLUDE_CIRCULAR_FILES)
/* rtfsproplus\prapicfi.c */
BOOLEAN pc_cfilio_close(int fd);
BOOLEAN pc_cfilio_read(int fd, byte *buff, dword count, dword *nread);
BOOLEAN pc_cfilio_write(int fd, byte *buff, dword count, dword *nwritten);
BOOLEAN pc_cfilio_setalloc(int fd, dword cluster, dword reserve_count);

/* rtfsproplus\prapicsk.c */
BOOLEAN pc_cfilio_lseek(int fd, int which_pointer, dword offset_hi, dword offset_lo, int origin, dword *poffset_hi, dword *poffset_lo);
BOOLEAN pc_cstreamio_lseek(int fd, int which_pointer, dword offset_hi, dword offset_lo, int origin, dword *poffset_hi, dword *poffset_lo);
BOOLEAN pc_cfilio_extract(int circ_fd, int linear_fd, dword length_hi, dword length_lo,byte *header_buffer, int header_size);
/* rtfsproplus\prapiext.c */
BOOLEAN pc_cfilio_release_all_remap_files(PC_FILE *preaderefile, int abort);
BOOLEAN pc_cfilio_remap_region_purge(PC_FILE *preaderefile,ddword purge_start_ddw, ddword purge_length_ddw);
BOOLEAN pc_cfilio_remap_read(PC_FILE *preaderefile,
                              ddword reader_file_pointer_ddw,
                              byte *buf,
                              dword read_count,
                              REMAP_RECORD *remap_record);
void pc_cfilio_check_remap_read(PC_FILE *preaderefile,ddword data_start_ddw,ddword data_length_ddw,
                            ddword *bytes_to_region_ddw,ddword *byte_offset_in_region_ddw,
                            ddword *bytes_in_region_ddw, REMAP_RECORD **preturn);
void pc_remap_free_list(PC_FILE *preaderefile, REMAP_RECORD *premap);
void pc_cfilio_remap_region_init(PC_FILE *preaderefile,REMAP_RECORD *premap_records,int num_records);
void pc_cfilio_release_remap_file(PC_FILE *premapfile, int abort);
#endif /* (INCLUDE_CIRCULAR_FILES) */

/* rtfsproplus\prasyint.c */
int _pc_async_step(DDRIVE *pdrive, int target_state, int steps);
/* rtfsproplus\prefi32.c */
int _pc_efilio32_open(DDRIVE *pdr,byte *name, word flag, word mode, EFILEOPTIONS *poptions, int use_charset);
void pc_efilio32_open_error(PC_FILE *pefile);
BOOLEAN pc_efilio32_open_complete(PC_FILE *pefile);
int pc_efilio_async_open32_continue(PC_FILE *pefile);
BOOLEAN _pc_efilio32_common_fstat(PC_FILE *pefile, ERTFS_EFILIO_STAT *pestat);
BOOLEAN _pc_efilio32_close(PC_FILE *pefile);
BOOLEAN _pc_efilio32_settime(PC_FILE *pefile,word new_time, word new_date);
BOOLEAN _pc_efilio32_flush(PC_FILE *pefile,dword max_clusters_per_pass);
BOOLEAN _pc_efilio32_read(PC_FILE *pefile, byte *buf, dword count, dword *nread);
/* rtfsproplus\preficom.c */

void _pc_efiliocom_sync_current_fragment(PC_FILE *pefile,FINODE *peffinode);
BOOLEAN _pc_efiliocom_write(PC_FILE *pefile,FINODE *peffinode, byte *buff, dword count, dword *nwritten);
void _pc_efiliocom_reset_seek(PC_FILE *pefile,FINODE *peffinode);
BOOLEAN _pc_efiliocom_lseek(PC_FILE *pefile,FINODE *peffinode, dword offset_hi, dword offset, int origin, dword *pnewoffset_hi,dword *pnewoffset);
BOOLEAN _pc_efiliocom_io(PC_FILE *pefile,FINODE *peffinode, byte *pdata, dword n_bytes, BOOLEAN reading, BOOLEAN appending);
BOOLEAN _pc_efiliocom_resize_current_fragment(PC_FILE *pefile,FINODE *peffinode,dword new_size);
/* rtfsproplus\prefinode.c */
int pc_efinode_async_load_continue(FINODE *pefinode);
dword _pc_efinode_count_to_link(FINODE *pefinode,dword allocation_policy);
BOOLEAN _pc_efinode_queue_cluster_links(FINODE *pefinode, dword allocation_policy);
void _pc_efinode_coalesce_fragments(FINODE *pefinode);
BOOLEAN load_efinode_fragment_list(FINODE *pefinode);
BOOLEAN load_efinode_fragments_until(FINODE *pefinode, llword offset);
void pc_free_excess_clusters(FINODE *pefinode);
void pc_free_efinode(FINODE *pefinode);
BOOLEAN pc_efinode_link_or_delete_cluster_chain(FINODE *pefinode, dword flags, dword max_clusters_per_pass);
BOOLEAN pc_efinode_delete_cluster_chain(FINODE *pefinode, dword max_clusters_per_pass);
BOOLEAN _pc_efinode_truncate_finode(FINODE *pfi);
void _pc_efinode_set_file_size(FINODE *pefinode,  dword length_hi, dword length_lo);
BOOLEAN  _pc_efinode_chsize(FINODE *pefinode, dword newsize_hi, dword newsize_lo);
REGION_FRAGMENT *pc_region_file_find_free_chain(
                DDRIVE *pdrive,
                dword allocation_policy,
                dword alloc_start_hint,
                dword chain_size,
                int *p_is_error);
/* rtfsproplus\prfragmt.c */
dword pc_frag_chain_delete_or_link(DDRIVE *pdr,
                    dword flags,
                    REGION_FRAGMENT *pf,
                    dword prev_end_chain,
                    dword start_contig,
                    dword clusters_to_process);
REGION_FRAGMENT *pc_fraglist_find_next_cluster(REGION_FRAGMENT *pfstart,REGION_FRAGMENT *pfend ,dword this_cluster,dword *pnext_cluster,dword *pstart_offset);
REGION_FRAGMENT *pc_fragment_seek_clusters(REGION_FRAGMENT *pf,dword n_clusters, dword *region_base_offset);
REGION_FRAGMENT *pc_fraglist_split(DDRIVE *pdr, REGION_FRAGMENT *pfstart,dword n_clusters,int *is_error);
REGION_FRAGMENT *pc_fraglist_find_cluster(REGION_FRAGMENT *pfstart,REGION_FRAGMENT *pfend ,dword cluster);
void pc_fraglist_coalesce(REGION_FRAGMENT *pfstart);
dword pc_fraglist_count_list(REGION_FRAGMENT *pf,REGION_FRAGMENT *pfend);
BOOLEAN pc_fraglist_remove_free_region(DDRIVE *pdr, REGION_FRAGMENT *pf);
BOOLEAN pc_fraglist_add_free_region(DDRIVE *pdr, REGION_FRAGMENT *pf);
BOOLEAN pc_fraglist_fat_free_list(DDRIVE *pdr, REGION_FRAGMENT *pf);
/* rtfsproplus\prmath64.c */
BOOLEAN return_false(void);
ddword pc_byte2ddwclmodbytes(DDRIVE *pdr, ddword nbytes64);
ddword pc_alloced_bytes_from_clusters_64(DDRIVE *pdr, dword total_alloced_clusters);
FINODE_EXTENDED *pc_memory_finode_ex(FINODE_EXTENDED *pinode);

dword pc_byte2clmodbytes(DDRIVE *pdr, dword nbytes);
dword pc_byte2cloff(DDRIVE *pdr, dword nbytes);
dword pc_byte2cloffbytes(DDRIVE *pdr, dword nbytes);

#ifdef __cplusplus
}
#endif
/* ===========================  */
/* End RtfsPro Plus prototypes */
/* ===========================  */

#if (INCLUDE_CS_UNICODE)
/* Unicode API */
#define pc_cfilio_open_uc(A,B,C) pc_cfilio_open_cs((A),(B),(C),CS_CHARSET_UNICODE)
#define pc_efilio_async_unlink_start_uc(A) pc_efilio_async_unlink_start_cs((A), CS_CHARSET_UNICODE)
#define pc_efilio_open_uc(A,B,C,D) pc_efilio_open_cs((A),(B),(C),(D),CS_CHARSET_UNICODE)
/* Non-Unicode API */
#define pc_cfilio_open(A,B,C) pc_cfilio_open_cs((A),(B),(C),CS_CHARSET_NOT_UNICODE)
#define pc_efilio_async_unlink_start(A) pc_efilio_async_unlink_start_cs((A), CS_CHARSET_NOT_UNICODE)
#define pc_efilio_open(A,B,C,D) pc_efilio_open_cs((A),(B),(C),(D),CS_CHARSET_NOT_UNICODE)
#endif


#endif


#if (INCLUDE_MATH64)          /* ddword type, ProPlus only */
ddword po_lseek64(int fd, ddword offset, int origin);
ddword m64_native_set32(dword a, dword b);
#endif

#if (INCLUDE_EXTENDED_ATTRIBUTES)
#define RTFS_EXTATTRIBUTE_SIGNATURE "RTFSEXTENTENDEDATTRIBUTE"
#define RTFS_EXTATTRIBUTE_SIGNATURE_SIZE 24
#define RTFS_EXTATTRIBUTE_RECORD_SIZE 28
dword pc_efilio32_get_dstart(PC_FILE *pefile);
BOOLEAN pc_efilio32_set_dstart(PC_FILE *pefile, dword dstart);
#endif

#if (INCLUDE_FAT64 && !INCLUDE_EXFAT)
BOOLEAN exfatop_remove_free_region(DDRIVE *pdr, dword cluster, dword ncontig);
BOOLEAN exfatop_add_free_region(DDRIVE *pdr, dword cluster, dword ncontig, BOOLEAN do_erase);
#define pcexfat_set_volume_dirty(A)   /* Implement to set and clear dirty bytes in bpb */
#define pcexfat_clear_volume_dirty(A)
dword pc_byte2clmod64(DDRIVE *pdr, dword nbytes_hi, dword nbytes_lo);
ddword pc_fragment_size_64(DDRIVE *pdr,REGION_FRAGMENT *pfragment);
#define EXFATALLOCATIONPOSSIBLE 0x1
#define EXFATNOFATCHAIN 		0x2
BOOLEAN pcexfat_insert_inode(DROBJ *pobj , DROBJ *pmom, byte _attr, FINODE *infinode, dword initcluster, byte *filename, byte secondaryflags, dword sizehi, dword sizelow, int use_charset);
BOOLEAN pcexfat_grow_directory(DROBJ *pobj);
void pcexfat_update_finode_datestamp(FINODE *pfi, BOOLEAN set_archive, int set_date_mask);
#endif

#endif     /* _RTFSPROTOS_*/
