
/* protests.h - This file is required only by the test and shell applications code
   provided in the packages\apps subdirectory. It is included in the common include
   subdirectory to eliminate the need to specify multiple include paths */

#include <stdlib.h>

extern byte *test_driveid;
extern int  test_drivenumber;

#define MAX_USER_BUFFER_SIZE_BLOCKS         2048    /* 1 Meg */
#define DEFAULT_USER_BUFFER_SIZE_BLOCKS     128     /* 64 k */
#define DEFAULT_FAT_PAGESIZE                8
#define DEFAULT_NUM_FAT_BUFFERS             16      /* 16 * DEFAULT_FAT_PAGESIZE */

/* Failsafe constants, ignored if not enabled */
#define MAX_BLOCKMAPSIZE 128
#define DEFAULT_BLOCKMAPSIZE 128
/* number of 512 byte, dword alligned pages */
#define MAX_PAGE_BUFFER_SIZE 10
#define DEFAULT_PAGE_BUFFER_SIZE 10
#define MAX_RESTORE_BUFFER_SIZE 64
#define DEFAULT_RESTORE_BUFFER_SIZE 64

#define TEST_DRIVE_STRUCT() pc_drno2dr(TEST_DRIVE_NO)
#define TEST_FILE_STRUCT(fd) (prtfs_cfg->mem_file_pool+fd)
#define TEST_FILE64 (byte *) "test_64"
#define RENAMED_TEST_FILE64 (byte *) "renamed_test_64"
#define TEST_FILE32 (byte *) "test_32"
#define SMALL_FILE (byte *) "small_file"
#define SMALL_FILE_SIZE     1024      /* dwords in "small file test " */
#define FILESIZE_64 0x80000000 /* 2 gig * 4 == 8 gig */
#define FILESIZE_32 0x100000    
#define TEST_BUFFER_SIZE 0x100000

//#undef FILESIZE_32
//#define FILESIZE_32 0x1000

extern byte *data_buffer;
extern dword  data_buffer_size_dw;

/* extended IO routines set these variables in debug mode
   when allocating clusters, the regression test uses them to
   verify that clusters where allocated as expected */
extern dword debug_first_efile_cluster_allocated;
extern dword debug_last_efile_cluster_allocated;
extern dword debug_num_transaction_buffer_loads;


typedef struct pro_test_checkpoint {
	dword bam_cksum;
    dword info_block1_cksum;
    dword info_block2_cksum;
    dword fat1_cksum;
    dword fat1_zero_count;
    dword fat2_cksum;
    dword fat2_zero_count;
    dword directories_num_clusters;
    dword directories_cksum;
    } PRO_TEST_CHECKPOINT;

#define PRO_TEST_ANNOUNCE(X) pro_test_announce((byte *) X)

/* rtfsproplustests\prlinexttest.c */
void  test_efilio_extract(byte *pdriveid);
/* rtfsproplustests\prtranstest.c */
void test_efilio_transactions(byte *pdriveid);
/* rtfsproplustests\prasytest.c */
void test_asynchronous_api(byte *pdriveid);
/* rtfsproplustests\prcftest.c */
void pc_cfilio_test(byte *pdriveid);
/* rtfsproplustests\preftest.c */
void pc_efilio_test(byte *pdriveid);
/* rtfsproplustests\prfstest.c */
void pc_fstest_main(byte *pdriveid);
void fstest_free_current_config();
/* rtfsproplustests\protests.c */
DDRIVE *test_drive_structure(void);
BOOLEAN pro_test_free_manager_atached(void);
BOOLEAN  set_test_drive(byte *driveid);
dword pro_test_bytes_per_cluster(void);
dword pro_test_dwrand(void);
void pro_test_announce(byte *message);
void pro_test_print_dword(char *prompt, dword val, int flag);
void pro_test_print_two_dwords(char *prompt_1, dword val_1,
                                  char *prompt_2, dword val_2,int flag);
BOOLEAN pro_test_compare_checkpoints(PRO_TEST_CHECKPOINT *pcheck1,PRO_TEST_CHECKPOINT *pcheck2);
void pro_test_mark_checkpoint(int drive_no, PRO_TEST_CHECKPOINT *pcheck);
void *pro_test_malloc(int n_bytes);
void  pro_test_free(void *p);
dword pro_test_check_buffer_dwords(dword *dw, dword value, dword count);
void pro_test_set_buffer_dwords(dword *dw, dword value, dword count);
void pro_test_fill_buffer_dwords(dword *dw, dword value, dword count);
void pro_test_alloc_data_buffer(void);
void pro_test_free_data_buffer(void);
int pro_test_efile_create(byte *filename, dword options,dword min_clusters_per_allocation);
int pro_test_efile_fill(byte *filename,dword options,dword test_file_size_dw,BOOLEAN do_io, BOOLEAN do_close);
BOOLEAN pro_test_read_n_dwords(int fd, dword value, dword size_dw,
        dword *pnread, BOOLEAN check_value);
BOOLEAN pro_test_write_n_dwords(int fd, dword value, dword size_dw,
                dword *pnwritten, BOOLEAN do_increment, byte *buffer);
BOOLEAN test_if_fat12_or_exfat(void);
BOOLEAN test_fat64(void);
dword pro_test_operating_policy(void);
dword pro_test_bytes_per_sector(void);

void pro_test_set_mount_parameters(DDRIVE *ptest_drive, struct rtfs_volume_resource_reply *pconfig,
		dword drive_operating_policy,
		dword sector_size_bytes,
		dword device_sector_size_sectors,
		dword n_sector_buffers,
		dword n_fat_buffers,
		dword fat_buffer_page_size_sectors,
		dword fsjournal_n_blockmaps,
		dword fsindex_buffer_size_sectors,
		dword fsrestore_buffer_size_sectors);

void pro_test_release_mount_parameters(DDRIVE *ptest_drive);


struct save_mount_context {
	RTFS_DEVI_VOLUME_MOUNT_PARMS saved_mount_parms;
	dword saved_exclusive_semaphore;
	void  *saved_device_sector_buffer;
	byte  *saved_device_sector_buffer_base;
	dword saved_device_sector_buffer_size;
};
void pro_test_save_mount_context(DDRIVE *ptest_drive,struct save_mount_context *psave_context);
void pro_test_restore_mount_context(DDRIVE *ptest_drive,struct save_mount_context *psave_context);
