/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2005
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*
* This file is automatically included in rtfs.h if INCLUDE_FAILSAFE_CODE is enabled
* this file is note intended for inclusion by user code.
*/
/* RTFSFAILSAFE.H - ERTFS-PRO Directory FailSafe routines */

/* Configuration values placed into user_configuration filed */
#define FS_CONFIG_JOURNAL_TO_RESERVED 0x00000001


#define FS_OP_ASYNCRESTORE           0x00000100
#define FS_OP_CLEAR_ASYNC            (~FS_OP_ASYNCRESTORE)


/* block map structure. One structure is used for each range of blocks
   that are redirectected to the journal.

   Two blockmaps may be active at a time.
   The first is the list of block replacements whose replacement maps in the
   journal file have already been flushed.
   The second is the list of block replacements whose replacement maps in the
   journal file have not yet been flushed.
*/

typedef struct fsblockmap {
        struct fsblockmap *pnext;
        dword volume_blockno;
        dword journal_blockno;
        dword n_replacement_blocks;
        } FSBLOCKMAP;



#define FS_MASTER_SIGNATURE_1          0x4641494C /* 'F''A''I''L' */
#define FS_MASTER_SIGNATURE_2          0x53414645 /* 'S''A''F''E' */
/* April-2004 version 2. - substantial rework  */
/* December 2005 version 3. - substantial rework  */
/* December 2005 version 31. - substantial rework  */
#define FS_CURRENT_VERSION              0x00000031

/* Internal field offsets in failsafe master record */
#define  FS_MASTER_OFFSET_SIGNATURE_1                0
#define  FS_MASTER_OFFSET_SIGNATURE_2                1
#define  FS_MASTER_OFFSET_VERSION                    2
#define  FS_MASTER_OFFSET_FSIZE                      3
#define  FS_MASTER_OFFSET_SESSION                    4
#define  FS_MASTER_OFFSET_START_RECORD               5


#define FS_FRAME_TYPE_OPEN          1 /* Must be first */
#define FS_FRAME_TYPE_NULL          2
#define FS_FRAME_TYPE_CLOSED        3
#define FS_FRAME_TYPE_FLUSHED       4
#define FS_FRAME_TYPE_RESTORING     5
#define FS_FRAME_TYPE_RESTORED      6

/* Internal field offsets in failsafe frame record */
#define  FS_FRAME_OFFSET_TYPE               0
#define  FS_FRAME_OFFSET_FRAME_SEQUENCE     1
#define  FS_FRAME_OFFSET_SEGMENT_CHECKSUM   2
#define  FS_FRAME_OFFSET_FRAME_CHECKSUM     3
#define  FS_FRAME_OFFSET_FRAME_RECORDS      4
#define  FS_FRAME_OFFSET_FAT_FREESPACE      5
#define  FS_FRAME_OFFSET_SESSION_ID         6
#define  FS_FRAME_HEADER_SIZE               7

#define FS_FIRST_INDEX_IN_FRAME FS_FRAME_HEADER_SIZE

/* FSRESTORE Structure part of the failsafe context, but private */


typedef struct fsrestore_buffer {
    /* Transfer buffer contains data read from the journal and written to the volume */
    byte *transfer_buffer;
    dword transfer_buffer_size;
    dword first_block_in_buffer;
    dword last_block_in_buffer;
    dword num_blocks_in_buffer;
} FSRESTORE_BUFFER;

typedef struct fsreplacement_queue {
    /* Transfer buffer contains data read from the journal and written to
       the volume */
    dword       file_current_index_block;
    dword       file_current_index_offset;
    struct fsblockmap *mem_current_blockmap;

    BOOLEAN     used_all_replacement_records;
    BOOLEAN     all_replacements_queued;
    dword       total_replacement_records; /* Diagnostic */
    dword       replacement_records_free; /* Diagnostic */
    FSBLOCKMAP *preplacement_freelist;
    FSBLOCKMAP *preplacement_extras;    /* Used by write to volume processing */
    FSBLOCKMAP *preplacements_queued;   /* Queued from file index headers or from blockmap */
    FSBLOCKMAP *preplacements_buffered; /* Replacements currently in the current buffer */
    FSBLOCKMAP *preplacement_fat;       /* For looping back */
    int num_fats_restored;
} FSREPLACEMENT_QUEUE;

typedef struct fsrestore {
    int restore_state;
	dword restore_loop_guard;
    BOOLEAN restore_from_file;
    dword restoring_start_record;
    dword restoring_terminal_record;
    dword restoring_last_block;
    FSRESTORE_BUFFER restore_buffer;
    FSREPLACEMENT_QUEUE replacement_queue;
} FSRESTORE;

/* FJOURNAL Structure part of the failsafe context, but private */
typedef struct fsjournal {
    dword session_current_checksum;
    dword session_current_freespace;
    dword session_start_record;
    dword frames_free;
    dword next_free_frame;
    dword wrap_counter;
    dword frame_sequence;

    dword open_start_record;
    dword open_current_frame;
    dword open_current_index;
    dword open_current_free;
    dword open_current_used;
    dword open_current_checksum;

    dword *journal_buffer_start;
    dword *open_index_buffer;
	dword buffer_sector_start;
	dword replacement_sectors_dirty;
    dword open_remapped_blocks;
    dword flushed_remapped_blocks;
    dword restoring_remapped_blocks;

    dword flushed_start_record;
    dword flushed_terminal_record;
    dword flushed_last_block;

    BOOLEAN block_cache_full;
    dword   num_blockmaps_used;
    dword   max_blockmaps_used;
    struct fsblockmap *blockmap_freelist;
    struct fsblockmap *open_blockmap_cache;
    struct fsblockmap *flushed_blockmap_cache;
    struct fsblockmap *restoring_blockmap_cache;

    /* Fragments to be released to the freelist manager after restore */
    REGION_FRAGMENT   *open_free_fragments;
    REGION_FRAGMENT   *flushed_free_fragments;
    REGION_FRAGMENT   *restoring_free_fragments;
} FSJOURNAL;
#define CONTEXT_TO_JOURNAL(CONTEXT) &(CONTEXT->fs_journal)

#define CONTEXT_TO_RESTORE(CONTEXT) &(CONTEXT->fs_restore)

#define FS_DEBUG 0

#if (FS_DEBUG)
void show_status(char *prompt, dword val, int flags);
#define FS_DEBUG_SHOW(X) rtfs_print_one_string((byte *)X,0);
#define FS_DEBUG_SHOWNL(X) rtfs_print_one_string((byte *)X,PRFLG_NL);
#define FS_DEBUG_SHOWINT(PROMPT, VAL) show_status(PROMPT, VAL, 0);
#define FS_DEBUG_SHOWINTNL(PROMPT, VAL) show_status(PROMPT, VAL, PRFLG_NL);
#else
#define FS_DEBUG_SHOW(X)
#define FS_DEBUG_SHOWNL(X)
#define FS_DEBUG_SHOWINT(PROMPT, VAL)
#define FS_DEBUG_SHOWINTNL(PROMPT, VAL)
#endif



typedef struct failsafe_runtime_stats {
		dword    journaling_active;
		dword    sync_in_progress;
		dword    journal_file_size;
		dword    journal_file_used;
		dword    journal_max_used;
		dword    restore_buffer_size;
		dword    num_blockmaps;
		dword    num_blockmaps_used;
		dword    max_blockmaps_used;
		dword    cluster_frees_pending;
		dword    reserved_free_clusters;
		dword    current_frame;
		dword    current_index;
		dword    flushed_blocks;
		dword    open_blocks;

		dword    restore_pass_count;
		dword    restoring_blocks;
		dword    restored_blocks;
		dword    current_restoring_block;
        /* The following field is updated by pc_failsafe_runtime_stats when extended is specified and Failsafe is configured */
		dword    fat_synchronize_writes;
		dword    fat_synchronize_blocks_written;
		dword    dir_synchronize_writes;
		dword    dir_synchronize_blocks_written;
		dword    frames_closed;
		dword    frames_flushed;

		dword    frames_restoring;
		dword    restore_data_reads;
		dword    restore_data_blocks_read;
		dword    restore_write_calls;
		dword    restore_blocks_written;
        dword    volume_block_writes;
        dword    volume_blocks_written;

        dword    journal_data_reads;
		dword    journal_data_blocks_read;
		dword    journal_data_writes;
		dword    journal_data_blocks_written;
		dword    journal_index_writes;
		dword    journal_index_reads;

        dword    transaction_buff_hits;
		dword    transaction_buff_reads;
		dword    transaction_buff_writes;

} FAILSAFE_RUNTIME_STATS;
BOOLEAN pc_diskio_failsafe_stats(byte *drive_name, FAILSAFE_RUNTIME_STATS *pstats);

/* Fail configuration and context structure */
typedef struct failsafecontext {
    dword   blockmap_size;
    struct fsblockmap *blockmap_core;
    byte *user_restore_transfer_buffer;
    dword user_restore_transfer_buffer_size;
    /* Devices must provide a buffer for the index page. If the buffer is > 1 sector the buffer determines the number of sectors to buffer before flushing the frame  */
    dword user_index_buffer_size_sectors;
    byte *user_open_index_buffer;
    byte *assigned_restore_transfer_buffer;
    dword assigned_restore_transfer_buffer_size;
    dword user_configuration;
    dword session_id;

    dword  operating_flags;
    int             error_val;
/* Internal elements, not intended to be accessed from user code */
    dword   journal_file_size;/* In blocks */
    dword   min_free_blocks;  /* Low water of allocated blocks */
    DDRIVE  *pdrive;
    dword    fs_frame_size;         /* Number of dwords that fit in a disk block, 128 for 512 byte blocks, 256 for 1024 etc. */
    dword   fs_frame_max_records;  /*  Number of indeces that fit in a disk block after the header */
    dword   nv_buffer_handle;
    dword   nv_cluster_handle;
    BOOLEAN nv_raw_mode;           /* Set to TRUE if nv_buffer_handle is a raw block number from the beginning of the device */
    REGION_FRAGMENT nv_reserved_fragment;
    BOOLEAN nv_disable_failsafe; /* nvio layer sets if journal creation failed but mount should proceed without Journaling */
    FSRESTORE fs_restore;       /* Current restore session */
    FSJOURNAL fs_journal;       /* Current journal session */

#if (INCLUDE_DEBUG_RUNTIME_STATS)
    FAILSAFE_RUNTIME_STATS stats;
#endif
} FAILSAFECONTEXT;

#if (INCLUDE_DEBUG_RUNTIME_STATS)
#define UPDATE_FSRUNTIME_STATS(CTX, FIELD, COUNT) CTX->stats.FIELD += COUNT;
#else
#define UPDATE_FSRUNTIME_STATS(DRIVE, FIELD, COUNT)
#endif


/* Failsafe Header File Status */
#define FS_STATUS_JOURNALING        1
#define FS_STATUS_RESTORING         2
#define FS_STATUS_NEEDS_RESTORE     3
#define FS_STATUS_RESTORED          4

/*
journaling          - TRUE if a Failsafe jounaling session is currently active.
journal_file_valid  - TRUE if the on disk Journal File is valid
needsflush          - TRUE If Failsafe is active and flush is required.

  The following BOOLEAN status fields are valid only when Failsafe is
   not active (journaling == FALSE) and the Journal file is valid.
   (journal_file_valid == TRUE)
out_of_date         - TRUE if the freespace on the volume has changed since
                      the Journal was last flushed.
check_sum_fails     - TRUE if the Journal file index block have been
                      corrupted or tambered with outside of the Failsafe
                      environment.
restore_required    - TRUE if the Failsafe file state is FS_STATUS_RESTORING.
                      This indictates that a Journal file restore operation,
                      or the FAT synchronization operation during a Failsafe
                      commit process was interrupted.

                      This condition indicates that the volume structure
                      is currently corrupted and must be restored.

restore_recommended - This condition indicates that the Journal file has
                      been flushed but the FAT synchronization operation
                      has not been done yet.
                      The volume structure is not currently corrupted and all
                      actions performed during the previous Failsafe session
                      exist only in the Journal file and not on the volume.

                      To synchronize the FAT Volume run restore or..

                      To discard the previous session and start a new
                      Journalling session just enable Failsafe.

                      Use ERTFS with Failsafe disabled.

version             - Failsafe version number (currently 3)
status              - Current Status
                        FS_STATUS_JOURNALING        - if (journaling == TRUE)
                            The Journal file is currently opened, needsflush
                            is set to TRUE.
                            if (journaling == FALSE) this status indicates
                            the the last Failsafe session was interrupted
                            before the Journal file was flushed. There is
                            no volume corruption the operations perfromed
                            since the last succesful flush are lost.
                        FS_STATUS_RESTORING         - See restore_required
                        FS_STATUS_NEEDS_RESTORE     - See restore_reccomended
                        FS_STATUS_RESTORED          - Journal and FAT are in
                                                      synch


numindexblocks      - Number of blocks in the journal file reserved for
                      indexing.
totalremapblocks    - Number of blocks in the journal file reserved for
                      Journalling.
numblocksremapped   - Number of FAT and directory blocks currently journalled
journaledfreespace  - Volume freespace when Journalling session was opened
currentfreespace    - Current volume freespace

journal_block_number- Disk location of journal file if using default NVIO
                      routines
filesize            - The size of the journal file size in 512 byte blocks.
*/
typedef struct fsinfo {
        BOOLEAN journaling;
        BOOLEAN journal_file_valid;
        dword version;
        dword numblocksremapped;
        dword journaledfreespace;
        dword currentfreespace;
        dword journal_block_number;
        dword filesize;
        BOOLEAN needsflush;
        BOOLEAN out_of_date;
        BOOLEAN check_sum_fails;
        BOOLEAN restore_required;
        BOOLEAN restore_recommended;
        /* Internal values */
        dword _start_session_frame;    /* First record in the session */
        dword _start_restored_frame;   /* Records already restored */
        dword _last_restored_frame;
        dword _first_restoring_frame;  /* Records being restored but interrupted */
        dword _last_restoring_frame;
        dword _first_flushed_frame;    /* Records flushed but not restored */
        dword _last_flushed_frame;
        dword _records_to_restore;
 } FSINFO;


/* Return values from fs_api_cb_restore() */
#define FS_CB_RESTORE  0 /* Do not change, must be zero. Tell Failsafe to proceded and restore the volume */
#define FS_CB_ABORT    2 /* Do not change, must be 1 Tell Failsafe to terminate the mount, causing the
                            API call to fail with errno set to PEFSRESTORENEEDED. */

/* Return values from  fs_api_cb_flush() */
#define FS_CB_SYNC     0 /* Do not change, must be 0 Default: Tell Failsafe to flush the journal file and synchronize the FAT volume */
#define FS_CB_FLUSH    2 /* Do not change, must be 2. Tell Failsafe to flush the journal file but not synchronize the FAT volume*/

#define FS_CB_CONTINUE 1 /* Do not change, must be 1 Tell Failsafe to proceded and not restore the volume. */

/* ===========================  */
/* Failsafe api prototypes  */
/* ===========================  */
#ifdef __cplusplus
extern "C" {
#endif

/* rtfsproplusfailsafe\prfsapi.c */
BOOLEAN fs_api_info(byte *drive_name,FSINFO *pinfo);

/* rtfsproplusfailsafe\prfsjournal.c */
BOOLEAN fs_api_enable(byte *drivename, BOOLEAN clear_journal);
BOOLEAN fs_api_disable(byte *drive_name, BOOLEAN abort);
BOOLEAN fs_api_commit(byte *path,BOOLEAN synch_fats);
int fs_api_async_commit_start(byte *path);
void fs_failsafe_disable(DDRIVE *pdrive, int error);

/* rtfsproplusfailsafe\prfsrestore.c */
BOOLEAN fs_api_restore(byte *drive_name);

/* rtfsproplusfailsafe\prfscb.c */

int fs_api_cb_journal_fixed(DDRIVE *pdrive, dword *raw_start_sector, dword *file_size_sectors);
BOOLEAN  fs_api_cb_check_fail_on_journal_resize(int driveno);
BOOLEAN fs_api_cb_disable_on_full(DDRIVE *pdrive);
dword fs_api_cb_journal_size(DDRIVE *pdrive);
int fs_api_cb_restore(int driveno);
BOOLEAN fs_api_cb_error_restore_continue(int driveno);
BOOLEAN fs_api_cb_enable(int driveno);
int fs_api_cb_flush(int driveno);


/* ===========================  */
/* End Failsafe api prototypes  */
/* ===========================  */


/* ===========================  */
/* Failsafe internal prototypes  */
/* ===========================  */
/* rtfsproplusfailsafe\prfsjournal.c */

dword _fs_transfer_buffer_size_sectors(FAILSAFECONTEXT *pfscntxt);
BOOLEAN fs_sessioninfo_internal(FAILSAFECONTEXT *pfscntxt, FSINFO *pinfo);
void fs_clear_session_vars(FAILSAFECONTEXT *pfscntxt);
BOOLEAN fs_failsafe_autocommit(DDRIVE *pdrive);
int _fs_api_async_flush_journal(DDRIVE *pdrive);
int _fs_api_async_commit_continue(DDRIVE *pdrive);
BOOLEAN fs_flush_transaction(DDRIVE *pdrive);
BOOLEAN fs_dynamic_config_check(DDRIVE *pdr);
void fs_free_blockmap(FSJOURNAL *pjournal,FSBLOCKMAP *pbm);
void fs_show_journal_session(FAILSAFECONTEXT *pfscntxt);
/* rtfsproplusfailsafe\prfsnvio.c */
BOOLEAN failsafe_reopen_nv_buffer(FAILSAFECONTEXT *pfscntxt, dword raw_start_sector, dword file_size_sectors);
BOOLEAN failsafe_create_nv_buffer(FAILSAFECONTEXT *pfscntxt, dword raw_start_sector, dword file_size_sectors);
BOOLEAN  failsafe_nv_buffer_io(FAILSAFECONTEXT *pfscntxt, dword block_no, dword nblocks, byte *pblock,BOOLEAN reading);
BOOLEAN failsafe_nv_buffer_mark(FAILSAFECONTEXT *pfscntxt, byte *pbuffer);
BOOLEAN failsafe_nv_buffer_clear(FAILSAFECONTEXT *pfscntxt, byte *pbuffer);
/* rtfsproplusfailsafe\prfsrestore.c */
BOOLEAN fs_failsafe_autorestore(FAILSAFECONTEXT *pfscntxt);
int fs_restore_from_session_start(FAILSAFECONTEXT *pfscntxt);
int fs_restore_from_session_continue(FAILSAFECONTEXT *pfscntxt);
BOOLEAN fs_fileinfo_internal(FAILSAFECONTEXT *pfscntxt, FSINFO *pinfo);
/* rtfsproplusfailsafe\prfstrio.c */
BOOLEAN _pc_efiliocom_overwrite(PC_FILE *pefile, FINODE *pefinode, byte *pdata, dword n_bytes);
BOOLEAN _pc_check_transaction_buffer(PC_FILE *pefile, dword clusterno);
BOOLEAN fs_failsafe_autocommit(DDRIVE *pdrive);
BOOLEAN fs_flush_transaction(DDRIVE *pdrive);
#ifdef __cplusplus
}
#endif

/* FailSafe internal restore and synchronize routines */


#define FS_MASTER_TYPE_ERROR            1
#define FS_MASTER_TYPE_VALID            2
#define FS_MASTER_ERROR_NOT_FAILSAFE    1
#define FS_MASTER_ERROR_WRONG_VERSION   2

typedef struct fsmasterinfo {
        int    master_type;
        int    master_error_type;
        dword  master_file_version;
        dword  master_file_size;
        dword  master_file_start;
        dword  master_file_session_id;
        dword  master_start_record;
        } FSMASTERINFO;

#ifdef __cplusplus
extern "C" {
#endif

void fs_failsafe_scaleto_blocksize(FAILSAFECONTEXT *pfscntxt);
void fs_mem_master_info(dword *pdw,FSMASTERINFO *pmaster);

void fs_mem_master_init(FAILSAFECONTEXT *pfscntxt, dword *pdw, dword journal_file_size,dword session_id, dword session_start_record);

int fs_restore_from_file_start(FAILSAFECONTEXT *pfscntxt);
int fs_restore_from_file_continue(FAILSAFECONTEXT *pfscntxt);
int fs_restore_from_session_start(FAILSAFECONTEXT *pfscntxt);
int fs_restore_from_session_continue(FAILSAFECONTEXT *pfscntxt);
BOOLEAN fs_sessioninfo_internal(FAILSAFECONTEXT *pfscntxt, FSINFO *pinfo);
BOOLEAN fs_fileinfo_internal(FAILSAFECONTEXT *pfscntxt, FSINFO *pinfo);
BOOLEAN fs_failsafe_autorestore(FAILSAFECONTEXT *pfscntxt);
void fs_failsafe_disable(DDRIVE *pdrive, int error);
void fs_clear_session_vars(FAILSAFECONTEXT *pfscntxt);
void fs_free_blockmap(FSJOURNAL *pjournal,FSBLOCKMAP *pbm);

#ifdef __cplusplus
}
#endif


/* ===========================  */
/* End Failsafe internal prototypes  */
/* ===========================  */
