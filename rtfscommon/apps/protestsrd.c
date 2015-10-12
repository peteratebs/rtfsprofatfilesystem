/* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* PROTESTS.C - RtfsProPLus Tests, common routines */

#include "rtfs.h"
#include "protests.h"

byte test_driveid_buffer[8];
byte *test_driveid;
int  test_drivenumber;


byte *data_buffer;
dword  data_buffer_size_dw;

void pro_test_announce(byte *message)
{
    RTFS_PRINT_STRING_1((byte *)message, PRFLG_NL);
}

DDRIVE *test_drive_structure()
{
    return(pc_drno_to_drive_struct(test_drivenumber)); /* BUGFIX */
}

BOOLEAN  set_test_drive(byte *driveid)
{
    if (!pc_set_default_drive(driveid))
    {
        pro_test_announce((byte *) "Invalid drive identifier :");
        pro_test_announce(driveid);
        return(FALSE);
    }
    else
    {
        test_driveid = test_driveid_buffer;
        test_drivenumber = pc_parse_raw_drive(driveid, CS_CHARSET_NOT_UNICODE);
        /* Copy 6 bytes enough for unicode or ascii */
        copybuff(test_driveid_buffer,driveid,6);
        return(TRUE);
    }
}

void *pro_test_malloc(int n_bytes)
{
void *p;
    p = rtfs_port_malloc(n_bytes);
    if (!p) {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    return(p);

}
void  pro_test_free(void *p)
{
    rtfs_port_free(p);
}

BOOLEAN test_if_fat12(void)
{
DRIVE_INFO drive_info_stats;
    if (!pc_diskio_info((byte *)test_driveid, &drive_info_stats, FALSE))
    {   ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    if (drive_info_stats.fat_entry_size == 12)
        return(TRUE);
    return(FALSE);
}

dword pro_test_operating_policy(void)
{
DRIVE_INFO drive_info_stats;
    drive_info_stats.drive_operating_policy = 0;
    if (!pc_diskio_info((byte *)test_driveid, &drive_info_stats, FALSE))
    {   ERTFS_ASSERT_TEST(rtfs_debug_zero()) }
    return(drive_info_stats.drive_operating_policy);
}
void pro_test_alloc_data_buffer(void)
{
    if (!data_buffer)
    {
        data_buffer_size_dw = TEST_BUFFER_SIZE; /* 64 k dwords (256 k) */
        data_buffer = (byte *) pro_test_malloc(data_buffer_size_dw*4);
    }
}
void pro_test_free_data_buffer(void)
{
    if (data_buffer)
        pro_test_free(data_buffer);
    data_buffer = 0;
}


dword pro_test_bytes_per_sector(void)
{
dword byte_per_cluster;
    byte_per_cluster = (dword) test_drive_structure()->drive_info.bytespsector;
    return(byte_per_cluster);
}

dword pro_test_bytes_per_cluster(void)
{
dword byte_per_cluster;
    byte_per_cluster = (dword) (test_drive_structure()->drive_info.secpalloc);
    byte_per_cluster *= test_drive_structure()->drive_info.bytespsector;
    return(byte_per_cluster);
}

BOOLEAN pro_test_compare_checkpoints(PRO_TEST_CHECKPOINT *pcheck1,PRO_TEST_CHECKPOINT *pcheck2)
{
    if
    (
    pcheck1->info_block1_cksum != pcheck2->info_block1_cksum ||
    pcheck1->info_block2_cksum != pcheck2->info_block2_cksum ||
    pcheck1->fat1_cksum != pcheck2->fat1_cksum ||
    pcheck1->fat1_zero_count != pcheck2->fat1_zero_count ||
    pcheck1->fat2_cksum != pcheck2->fat2_cksum ||
    pcheck1->fat2_zero_count != pcheck2->fat2_zero_count ||
    pcheck1->directories_num_clusters != pcheck2->directories_num_clusters ||
    pcheck1->directories_cksum != pcheck2->directories_cksum
    )
        return(FALSE);
    else
        return(TRUE);
}
static dword pro_test_checksum(byte *user_buffer, int n_bytes);
static dword pro_test_count_zeroes(byte *user_buffer, int n_bytes);

static void mount_drive(byte *drivename)
{
#if (INCLUDE_ASYNCRONOUS_API)
int driveno,status;

   driveno = pc_diskio_async_mount_start(drivename);
   ERTFS_ASSERT_TEST(driveno!=-1)
   status = pc_async_continue(driveno,DRV_ASYNC_IDLE, 0);
   ERTFS_ASSERT_TEST(status == PC_ASYNC_COMPLETE)
#else
   if (check_drive_name_mount(drivename, CS_CHARSET_NOT_UNICODE))
   {
        ERTFS_ASSERT_TEST(rtfs_debug_zero());
   }
   /* release from check mount */
   rtfs_release_media_and_buffers(pc_parse_raw_drive(drivename, CS_CHARSET_NOT_UNICODE));
#endif
}

void pro_test_mark_checkpoint(int drive_no, PRO_TEST_CHECKPOINT *pcheck)
{
byte *user_buffer;
dword user_buffer_size;
DDRIVE *pdr;

    pdr = check_drive_by_number(drive_no, TRUE);
    if (!pdr)
    {
        mount_drive(test_driveid);
        pdr = check_drive_by_number(drive_no, TRUE);
        if (!pdr)
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
    }

    user_buffer = pc_claim_user_buffer(pdr, &user_buffer_size, 0); /* released at cleanup */
    if (!user_buffer) {ERTFS_ASSERT_TEST(rtfs_debug_zero())}

    rtfs_memset(pcheck, 0, sizeof(*pcheck));
    /* Check_sum info blocks */
    if (pdr->drive_info.fasize == 8)
    {
         if (!raw_devio_xfer(pdr, (dword) pdr->drive_info.infosec, user_buffer, 1, FALSE, TRUE))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        pcheck->info_block1_cksum = pro_test_checksum(user_buffer, test_drive_structure()->drive_info.bytespsector);


        if (!raw_devio_xfer(pdr, (dword) pdr->drive_info.infosec + 6, user_buffer, 1, FALSE, TRUE))
        {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        pcheck->info_block2_cksum = pro_test_checksum(user_buffer, test_drive_structure()->drive_info.bytespsector);
    }
    /* Check_sum FAT blocks */
    {
    dword i, n_blocks_left, block_no, n_blocks;
        n_blocks = pdr->drive_info.secpfat;
        block_no = pdr->drive_info.fatblock;
        n_blocks_left = n_blocks;
        while (n_blocks_left)
        {
        dword blocks_to_read;
        byte *puser_buff;
            blocks_to_read = n_blocks_left;
            if (user_buffer_size < blocks_to_read)
                blocks_to_read = user_buffer_size;
            if (!raw_devio_xfer(pdr, block_no, user_buffer, blocks_to_read, FALSE, TRUE))
            {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
            puser_buff = user_buffer;
            /* Don't checksum the first 8 bytes of the FAT.. On a 1 FAT format we will write the journal info here */
            pcheck->fat1_zero_count += pro_test_count_zeroes(puser_buff+8, test_drive_structure()->drive_info.bytespsector-8);
            pcheck->fat1_cksum += pro_test_checksum(puser_buff+8, test_drive_structure()->drive_info.bytespsector-8);
            puser_buff += test_drive_structure()->drive_info.bytespsector;
            for (i = 1; i < blocks_to_read; i++, puser_buff += test_drive_structure()->drive_info.bytespsector)
            {
                pcheck->fat1_zero_count += pro_test_count_zeroes(puser_buff, test_drive_structure()->drive_info.bytespsector);
                pcheck->fat1_cksum += pro_test_checksum(puser_buff, test_drive_structure()->drive_info.bytespsector);
            }
            n_blocks_left -=  blocks_to_read;
            block_no += blocks_to_read;
        }
        pcheck->fat2_zero_count = 0;
        pcheck->fat2_cksum = 0;
        /* Checksum the second fat if there is one */
        if (pdr->drive_info.numfats > 1)
        {
            n_blocks = pdr->drive_info.secpfat;
            block_no = pdr->drive_info.fatblock + n_blocks;
            n_blocks_left = n_blocks;

            while (n_blocks_left)
            {
            dword blocks_to_read;
            byte *puser_buff;
                blocks_to_read = n_blocks_left;
                if (user_buffer_size < blocks_to_read)
                    blocks_to_read = user_buffer_size;
                if (!raw_devio_xfer(pdr, block_no, user_buffer, blocks_to_read, FALSE, TRUE))
                {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
                puser_buff = user_buffer;
                /* Don't checksum the first 8 bytes of the FAT.. we will write the journal info here */
                pcheck->fat2_zero_count += pro_test_count_zeroes(puser_buff+8, test_drive_structure()->drive_info.bytespsector-8);
                pcheck->fat2_cksum += pro_test_checksum(puser_buff+8, test_drive_structure()->drive_info.bytespsector-8);
                puser_buff += test_drive_structure()->drive_info.bytespsector;
                for (i = 1; i < blocks_to_read; i++, puser_buff += test_drive_structure()->drive_info.bytespsector)
                {
                    pcheck->fat2_zero_count += pro_test_count_zeroes(puser_buff, test_drive_structure()->drive_info.bytespsector);
                    pcheck->fat2_cksum += pro_test_checksum(puser_buff, test_drive_structure()->drive_info.bytespsector);
                }
                n_blocks_left -=  blocks_to_read;
                block_no += blocks_to_read;
            }
        }
    }
    /* Check_sum directory blocks */
    { /* Not doing yet, need to traverse from root. */
        pcheck->directories_num_clusters = 0;
        pcheck->directories_cksum = 0;
    }
    pc_release_user_buffer(pdr, user_buffer);
    release_drive_mount(drive_no);/* Release lock, unmount if aborted */
}

/* dword checksum on a block. sum up (pos * content), not just pos
   this is a better test for uniqueness */
static dword pro_test_checksum(byte *user_buffer, int n_bytes)
{
dword *pdw,i,cksum, ndwords;
   cksum = 0;
   pdw = (dword *) user_buffer;
   ndwords = (dword)(n_bytes/4);
   for (i = 1; i <  ndwords + 1; i++, pdw++)
        cksum += (i * *pdw);
    return(cksum);
}
static dword pro_test_count_zeroes(byte *user_buffer, int n_bytes)
{
dword *pdw,i,zeroes,ndwords;
   zeroes = 0;
   pdw = (dword *) user_buffer;
   ndwords = (dword)(n_bytes/4);

   for (i = 0; i < ndwords; i++, pdw++)
    if (!*pdw)
            zeroes += 1;
    return(zeroes);
}

DRIVE_INFO test_drive_infostruct;      /* Drive info structure shered by test */
BOOLEAN RTFS_DEVI_copy_volume_resource_reply(DDRIVE *pdr, struct rtfs_volume_resource_reply *preply, dword sector_size_bytes);
void pc_rtfs_free(void *p); /* See drdynamic.c */
static BOOLEAN test_dsk_config_active;
void pro_test_set_mount_parameters(DDRIVE *ptest_drive, struct rtfs_volume_resource_reply *pconfig,
		dword drive_operating_policy,
		dword sector_size_bytes,
		dword device_sector_size_sectors,
		dword n_sector_buffers,
		dword n_fat_buffers,
		dword fat_buffer_page_size_sectors,
		dword fsjournal_n_blockmaps,
		dword fsindex_buffer_size_sectors,
		dword fsrestore_buffer_size_sectors)
{
    /* Configure drive operating conditions. This configuration is applied by:
            init_drive_for_asynchronous_test()
       Which is called multiple times with differing operating policies*/
    rtfs_memset(pconfig, 0, sizeof(*pconfig));


    /* Free the current configuration if we changed it */
    pro_test_release_mount_parameters(ptest_drive);

	/* Set the media info shared buffer to 32 sectors */
	ptest_drive->pmedia_info->device_sector_buffer_size = sector_size_bytes*device_sector_size_sectors;
	ptest_drive->pmedia_info->device_sector_buffer_base = pc_rtfs_iomalloc(ptest_drive->pmedia_info->device_sector_buffer_size, &ptest_drive->pmedia_info->device_sector_buffer);

    pconfig->drive_operating_policy       = drive_operating_policy;
    pconfig->use_dynamic_allocation       = 1;


    pconfig->n_sector_buffers			  = n_sector_buffers;

    pconfig->n_fat_buffers                = n_fat_buffers;
    pconfig->fat_buffer_page_size_sectors = fat_buffer_page_size_sectors;
#if (INCLUDE_FAILSAFE_CODE)
    /* Use larger blockmap than default, this is required if the free manager is disabled and FAT buffers are
    modified during cluster allocations, if the free manager is enabled clusters are allocated from
    the free cluster pool, some tests rely on this, allocating large numbers of clusters during write and
    anticipate a journal full condition during file flush */
    pconfig->fsjournal_n_blockmaps 		= fsjournal_n_blockmaps;
    pconfig->fsrestore_buffer_size_sectors = fsrestore_buffer_size_sectors;
    pconfig->fsindex_buffer_size_sectors = fsindex_buffer_size_sectors;
#endif

    if (!RTFS_DEVI_copy_volume_resource_reply(ptest_drive, pconfig, sector_size_bytes))
    {
        ERTFS_ASSERT_TEST(rtfs_debug_zero())
    }
	test_dsk_config_active = TRUE;

}

void pro_test_release_mount_parameters(DDRIVE *ptest_drive)
{
	if (test_dsk_config_active)
	{
		/* Free the temorarilly allocated media buffer and volume configuration */
		pc_rtfs_free(ptest_drive->pmedia_info->device_sector_buffer_base);
    	pc_free_disk_configuration(ptest_drive->driveno);
		test_dsk_config_active = FALSE;
	}
}


void pro_test_save_mount_context(DDRIVE *ptest_drive,struct save_mount_context *psave_context)
{
	psave_context->saved_mount_parms = ptest_drive->mount_parms;
	/* Disable shared buffer mode for user buffer and failsafe restore buffer
	   just clearing the semaphore is enough the shared buffers won't be touched. */
	psave_context->saved_exclusive_semaphore = prtfs_cfg->rtfs_exclusive_semaphore;
    psave_context->saved_device_sector_buffer_base = ptest_drive->pmedia_info->device_sector_buffer_base;
    psave_context->saved_device_sector_buffer = ptest_drive->pmedia_info->device_sector_buffer;
    psave_context->saved_device_sector_buffer_size = ptest_drive->pmedia_info->device_sector_buffer_size;

}

void pro_test_restore_mount_context(DDRIVE *ptest_drive,struct save_mount_context *psave_context)
{
	ptest_drive->mount_parms = psave_context->saved_mount_parms;
	prtfs_cfg->rtfs_exclusive_semaphore = psave_context->saved_exclusive_semaphore;
    ptest_drive->pmedia_info->device_sector_buffer_base = psave_context->saved_device_sector_buffer_base;
    ptest_drive->pmedia_info->device_sector_buffer = psave_context->saved_device_sector_buffer;
    ptest_drive->pmedia_info->device_sector_buffer_size = psave_context->saved_device_sector_buffer_size;
}
