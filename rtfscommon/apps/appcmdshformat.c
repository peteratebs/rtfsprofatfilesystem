/**************************************************************************
    APPTSTSH.C   - Command driven test shell for embedded file manager.

Summary
    TSTSH

 Description
    Interactive shell program designed to allow testing of the file manager.
Returns

Example:
*****************************************************************************
*/
/*
<TEST>  Test File:   rtfscommon/apps/appcmdshformat.c
*/

#include "rtfs.h"

#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */

#define DISPLAY_ERRNO(ROUTINE)
/* See apputil.c */
long rtfs_atol(byte * s);
int rtfs_atoi(byte * s);

void use_args(int agc, byte **agv);
static int _ask_user_for_partitions(byte *working_buffer, dword *psizeincylindersarray, byte *ppartitiontypes,  dword total_cylinders, dword sectors_per_cylinder);
static DDRIVE *_choose_drive(char *prompt, byte *path);
BOOLEAN tstsh_is_yes(byte *p);
byte *pc_ltoa(dword num, byte *dest, int number_base);
void rtfs_print_prompt_user(byte *prompt, byte *buf);

/* FORMAT*/
/*
<TEST>  Procedure:   doformat() - Format volumes.
<TEST>   Invoke by typing "FORMAT" in the command shell
<TEST>
*/

#if (INCLUDE_EXFATORFAT64)
void probe_exfat_format_parms(byte *path);
int doprobeexfatformat(int agc, byte **agv)
{
DDRIVE *pdr;
byte path[10];

    use_args(agc, agv);

    /* Select the device, check media and clear change conditions */
    pdr = _choose_drive("Enter the drive to probe as A:, B: etc ", path);
    if (!pdr)
        return(-1);

#ifdef RTFS_WINDOWS
		probe_exfat_format_parms(path);
#endif
	return(0);
}
int doexfatformat(int agc, byte **agv)
{
DDRIVE *pdr;
byte path[10];

    use_args(agc, agv);

    /* Select the device, check media and clear change conditions */
    pdr = _choose_drive("Enter the drive to format as A:, B: etc ", path);
    if (!pdr)
        return(-1);

	if (pcexfat_format_volume(path))
	{
		RTFS_PRINT_STRING_1((byte *)"ExFat Format Succeeded.", PRFLG_NL);
		return(0);
	}
	else
	{
		RTFS_PRINT_STRING_1((byte *)"ExFat Format Failed.", PRFLG_NL);
       	return(-1);
	}
}
#endif

int doformat(int agc, byte **agv)
{
byte buf[80];
DDRIVE *pdr;
byte path[10];
BOOLEAN use_extended_format = FALSE;
RTFSFMTPARMSEX parms_ex;
BOOLEAN status;

    use_args(agc, agv);

    rtfs_memset(&parms_ex, 0, sizeof(parms_ex));

    /* Select the device, check media and clear change conditions */
    pdr = _choose_drive("Enter the drive to format as A:, B: etc ", path);
    if (!pdr)
        return(-1);

    rtfs_print_prompt_user((byte *)"Zero Volume Blocks ? (Y/N) ", buf); /* "Show directories(Y/N) " */
    if (tstsh_is_yes(buf))
	{
		parms_ex.scrub_volume = TRUE;
		use_extended_format = TRUE;
	}

    rtfs_print_prompt_user((byte *)"Specify FAT size, Cluster size, etc ? (Y/N) ", buf); /* "Show directories(Y/N) " */
    if (tstsh_is_yes(buf))
	{
		use_extended_format = TRUE;
		RTFS_PRINT_STRING_1((byte *)"All Values must be provided ", PRFLG_NL);
		do {
		rtfs_print_prompt_user((byte *)"Cluster size: 12, 16, 32  ", buf); /* "Show directories(Y/N) " */
		parms_ex.bits_per_cluster = (unsigned char) rtfs_atoi(buf);
		} while (parms_ex.bits_per_cluster != 12 && parms_ex.bits_per_cluster != 16 && parms_ex.bits_per_cluster != 32);
		rtfs_print_prompt_user((byte *)"# of root dir entries (normally 512 for FAT12, FAT16, must be 0 for FAT32 ", buf);
		parms_ex.numroot = (unsigned short) rtfs_atoi(buf);
		do {
		rtfs_print_prompt_user((byte *)"Number of FATS on the disk, 1 or 2 Must be 2 if using Failsafe ", buf);
		parms_ex.numfats = (unsigned char) rtfs_atoi(buf);
		} while (parms_ex.numfats != 1 && parms_ex.numfats != 2);
		do {
		rtfs_print_prompt_user((byte *)"Reserved sectors must be >= 1 for FAT16, >= 32 For FAT32 ", buf);
		parms_ex.secreserved = (unsigned char) rtfs_atoi(buf);
		} while (parms_ex.secreserved < 1);
		do {
		rtfs_print_prompt_user((byte *)"Sectors per cluster (1, 2, 4, 8, 16, 32, 64 or 128) ", buf);
		parms_ex.secpalloc = (unsigned char) rtfs_atoi(buf);
		} while (parms_ex.secpalloc != 1 && parms_ex.secpalloc != 2 &&
				 parms_ex.secpalloc != 4 && parms_ex.secpalloc != 8	&&
				 parms_ex.secpalloc != 16 && parms_ex.secpalloc != 32 &&
				 parms_ex.secpalloc != 64 && parms_ex.secpalloc != 128);
	}

    RTFS_PRINT_STRING_1((byte *)"Calling pc_format_volume()", PRFLG_NL);
    if (use_extended_format)
	{
    	RTFS_PRINT_STRING_1((byte *)"Calling pc_format_volume_ex()", PRFLG_NL);
		status = pc_format_volume_ex(path, &parms_ex);
	}
	else
	{
    	RTFS_PRINT_STRING_1((byte *)"Calling pc_format_volume()", PRFLG_NL);
		status = pc_format_volume(path);
	}

    if (!status)
    {
        rtfs_print_prompt_user((byte *)"Format: Format volume failed. Press return", buf);
        return(-1);
    }
    return (0);
}


/*
<TEST>  Procedure:   dodeviceformat() - Perform device level format
<TEST>   Invoke by typing "DEVICEFORMAT" in the command shell
<TEST>
*/

int dodeviceformat(int agc, byte **agv)
{
byte working_buffer[100];
DDRIVE *pdr;
byte path[10];


    use_args(agc, agv);

    /* Select the device, check media and clear change conditions */
    pdr = _choose_drive("Enter the drive to perfrom device level format on A:, B: etc ", path);
    if (!pdr)
        return(-1);
    /* Call the low level media format. ? */
    {
        RTFS_PRINT_STRING_1((byte *)"Calling media format", PRFLG_NL); /* "Calling media format" */
        if (!pc_format_media(path))
        {
           DISPLAY_ERRNO("pc_format_media")
           rtfs_print_prompt_user((byte *)"Format: Media format failed. Press return", working_buffer);  /* "Format: Media format failed. Press return" */
           return(-1);
        }
    }
    return(0);
}

static BOOLEAN  _process_partition_list(byte *path, int num_partitions, dword sectors_per_cylinder, dword sectors_per_track, dword *psizeincylinders, byte *ppartitiontypes);
/*
<TEST>  Procedure:   dofdisk() - Partion a device
<TEST>   Invoke by typing "FDISK" in the command shell
<TEST>
*/

int dofdisk(int agc, byte **agv)                                    /*__fn__*/
{
byte working_buffer[100];
DDRIVE *pdr;
byte path[10];
DEV_GEOMETRY geometry;

    use_args(agc, agv);
    /* Select the device, check media and clear change conditions */
    pdr = _choose_drive("Enter the drive to partition A:, B: etc ", path);
    if (!pdr)
        return(-1);

    /* Partition the drive */
    {
        dword total_cylinders, sectors_per_cylinder;
        int    num_partitions;
        dword  sizeincylinders[16];
        byte   partitiontypes[16];

        /* Get geometry information and then call _ask_user_for_partitions() to populate two arrays:
           dword sizeincylinders[] (size in cylinders of each partition)
           byte   partitiontypes[] (type of each each partition) */
        rtfs_memset(&geometry, 0, sizeof(geometry));
        if (!pc_get_media_parms(path, &geometry))
        {
            rtfs_print_prompt_user((byte *)"Format: get media geometry failed. Press return", working_buffer);
            return(-1);
        }
        /* Cylinder align */
        sectors_per_cylinder =  (dword) (geometry.dev_geometry_heads & 0xff);
        sectors_per_cylinder *= geometry.dev_geometry_secptrack;
        total_cylinders = geometry.dev_geometry_lbas/sectors_per_cylinder;
        /* Ask the user for the sizes in cylinders and types of partitions  */
        num_partitions = _ask_user_for_partitions(&working_buffer[0], &sizeincylinders[0], &partitiontypes[0], total_cylinders, sectors_per_cylinder);
        /* Convert the input values to partition specifications and call pc_partition_media() */
        if (!_process_partition_list(path, num_partitions, sectors_per_cylinder, geometry.dev_geometry_secptrack, &sizeincylinders[0], &partitiontypes[0]))
        {
            RTFS_PRINT_STRING_1((byte *)"pc_partition_media() failed.", PRFLG_NL);
            return(-1);
        }
    }
    return (0);
}

/* Convert input values to partition specifications and call pc_partition_media() */
static BOOLEAN  _process_partition_list(byte *path, int num_partitions, dword sectors_per_cylinder, dword sectors_per_track, dword *psizeincylinders, byte *ppartitiontypes)
{
int which_partition, which_mbr_index;
dword current_start_cylinder;
struct mbr_specification mbrspecs[16];

    /* Clear the specifications */
    rtfs_memset(&mbrspecs[0], 0, sizeof(mbrspecs));

    mbrspecs[0].mbr_sector_location = 0;    /* The mbr is always at sector zero */
    if (num_partitions)
        mbrspecs[0].device_mbr_count    = 1;    /* There will be at least one master mbr */
    else
        mbrspecs[0].device_mbr_count    = 0;    /* Clear the partition table */

    which_mbr_index = 0;
    current_start_cylinder = 0;
    for (which_partition = 0; which_partition < num_partitions; which_partition++)
    {
#if (SUPPORT_EXTENDED_PARTITIONS)
        if (which_partition == 3 && num_partitions > 4)
        {    /* Extended partition required initialize the extended partition entry at offset 3 */
             int i; dword total_cylinders;
             mbrspecs[0].entry_specifications[3].partition_start = (current_start_cylinder * sectors_per_cylinder);
             mbrspecs[0].entry_specifications[3].partition_type  = 0x05;
             mbrspecs[0].entry_specifications[3].partition_boot  = 0x00;
             /* Count all the cylinders in the extended partition and update the size field */
             total_cylinders = 0;
             for (i = 4; i <= num_partitions; i++)
                total_cylinders += *(psizeincylinders+i);
             mbrspecs[0].entry_specifications[3].partition_size  = total_cylinders*sectors_per_cylinder;
             /* Done filling the primary partition - start with extended partitions */
             which_mbr_index = 1;
        }
#endif
        if (which_mbr_index == 0)
        { /* Inside the mbr - start the volume at head 1 sector 0 */
            mbrspecs[which_mbr_index].entry_specifications[which_partition].partition_start =  (current_start_cylinder * sectors_per_cylinder)+sectors_per_track;
            mbrspecs[which_mbr_index].entry_specifications[which_partition].partition_size  =  (*(psizeincylinders+which_partition) * sectors_per_cylinder)-sectors_per_track+1;
            mbrspecs[which_mbr_index].entry_specifications[which_partition].partition_type  =   *(ppartitiontypes+which_partition);
            mbrspecs[which_mbr_index].entry_specifications[which_partition].partition_boot  =   0x80;
        }
#if (SUPPORT_EXTENDED_PARTITIONS)
        else
        {
            mbrspecs[0].device_mbr_count += 1;  /* Update mbr count in the first record (mbrspecs[0].device_mbr_count-1) =='s number of extended partitions */
            mbrspecs[which_mbr_index].device_mbr_count = 0; /* not used */
            /* Where to write this extended boot record */
            mbrspecs[which_mbr_index].mbr_sector_location = (current_start_cylinder * sectors_per_cylinder);
            /* Entry zero of the ebr contains the relative offset to the BPB and the number of usable sectors beyond that */
            mbrspecs[which_mbr_index].entry_specifications[0].partition_start  = sectors_per_track;
            mbrspecs[which_mbr_index].entry_specifications[0].partition_size   = (*(psizeincylinders+which_partition) * sectors_per_cylinder)
                                                                                            - sectors_per_track + 1;
            /* Entry one of the ebr contains the address of the next ebr minus the first sector in the extended partition */
            if ((which_partition + 1)  ==  num_partitions)
            { /* If this is the last partition set the start location and size of the next partition to zero */
                mbrspecs[which_mbr_index].entry_specifications[1].partition_start = 0;
                mbrspecs[which_mbr_index].entry_specifications[1].partition_size =  0;
            }
            else
            {   /* The next ebr resides at the current cylinder plus the size of the current partition */
                mbrspecs[which_mbr_index].entry_specifications[1].partition_start =
                    (current_start_cylinder + *(psizeincylinders+which_partition)) * sectors_per_cylinder;
                /* Subtract the first sector of the first extended partition so it is a relative offset */
                mbrspecs[which_mbr_index].entry_specifications[1].partition_start -= mbrspecs[1].mbr_sector_location;
                /* Entry one of the ebr contains the size of the next partition in the size field */
                mbrspecs[which_mbr_index].entry_specifications[1].partition_size =
                                                    (*(psizeincylinders+which_partition+1) * sectors_per_cylinder);
            }
            which_mbr_index += 1;   /* Each partition has its own ebr */
        }
#endif
        current_start_cylinder = current_start_cylinder + *(psizeincylinders+which_partition);
    }
    return (pc_partition_media(path, &mbrspecs[0]));

}

/*  Ask the user for the drive id to device format, format or partition */
static DDRIVE *_choose_drive(char *prompt, byte *path)
{
byte buf[10];
DDRIVE *pdr;
int driveno;


    rtfs_print_prompt_user((byte *)prompt, path);
    pdr = 0;
    driveno = pc_parse_raw_drive(path, CS_CHARSET_NOT_UNICODE);
    if (driveno != -1)
        pdr = pc_drno_to_drive_struct(driveno);
    if (!pdr)
    {
        rtfs_print_prompt_user((byte *)"Invalid drive selection, press return", buf);
        return(0);
    }
    return(pdr);
}

/*  Ask the user for partition sizes and partition types . */
static int _ask_user_for_partitions(byte *working_buffer, dword *psizeincylindersarray, byte *ppartitiontypes,  dword total_cylinders, dword sectors_per_cylinder)
{
    dword cylinders_remaining,requested_cylinders_this_partition;
    int partition_count = 0;

    cylinders_remaining = total_cylinders;

    RTFS_PRINT_STRING_1((byte *)"      Please provide partitioning instructions.. ", PRFLG_NL);
    RTFS_PRINT_STRING_1((byte *)"      ========================================== ", PRFLG_NL);
    RTFS_PRINT_STRING_1((byte *)"    ", PRFLG_NL);
    RTFS_PRINT_STRING_1((byte *)"      Note: press X to exit or C to eliminate the partition table.. ", PRFLG_NL);
    RTFS_PRINT_STRING_1((byte *)"      ============================================================= ", PRFLG_NL);

    while (cylinders_remaining)
    {

        RTFS_PRINT_STRING_1((byte *)"Defining partition number : ", 0);
        RTFS_PRINT_LONG_1((dword) partition_count, PRFLG_NL);

        RTFS_PRINT_STRING_1((byte *)"This many cylinders remain: ", 0);
        RTFS_PRINT_LONG_1((dword) cylinders_remaining, PRFLG_NL);
        RTFS_PRINT_STRING_1((byte *)"This many sectors remain: ", 0);
        RTFS_PRINT_LONG_1((dword) cylinders_remaining*sectors_per_cylinder, PRFLG_NL);

#if (SUPPORT_EXTENDED_PARTITIONS)
        if (partition_count == 3)
            RTFS_PRINT_STRING_1((byte *)"An extended partition will be created if you do not select all..", PRFLG_NL);
#else
        if (partition_count == 3)
            RTFS_PRINT_STRING_1((byte *)"Last available partition space will be wasted if you do not select all..", PRFLG_NL);
#endif
        RTFS_PRINT_STRING_1((byte *)"Type X to exit or C to eliminate the partition table.. ", PRFLG_NL);
        rtfs_print_prompt_user((byte *)"Select the number of cylinders for this partition or return for all:", working_buffer);

        if (working_buffer[0] == 'X' || working_buffer[0] == 'x')
            break;  /* psizeincylindersarray, and ppartitiontypesreturn are complete for "partiton count" entries. break and return */

        if (working_buffer[0] == 'C' || working_buffer[0] == 'c')
            return (0);  /* Will clear the partition */

        if (!working_buffer[0])
            requested_cylinders_this_partition = cylinders_remaining;
        else
            requested_cylinders_this_partition = (dword)rtfs_atol(working_buffer);

        if (requested_cylinders_this_partition == 0 || requested_cylinders_this_partition >  cylinders_remaining)
            requested_cylinders_this_partition =  cylinders_remaining;
        *(psizeincylindersarray+partition_count) = requested_cylinders_this_partition;

        /* Now ask the usr for the type.
            if > 0xffff sectors it can be 0x0c or 0x06
            otherwise it can be 0x04 or 0x01 */
        {
            dword logical_sectors_this_partition;
            byte  this_entry_type;

            this_entry_type = 0;
            logical_sectors_this_partition = (requested_cylinders_this_partition * sectors_per_cylinder);
            if (logical_sectors_this_partition > 0xffff)
            { /* Select partition type */
                while (this_entry_type != 0x0c && this_entry_type != 0x06)
                {
                    rtfs_print_prompt_user((byte *)"Select the partition type 0.) FAT32(0x0c), 1.) FAT16(0x06): ", working_buffer);
                    if (working_buffer[0] == '0') this_entry_type = 0x0c;
                    else if (working_buffer[0] == '1') this_entry_type = 0x06;
                }
            }
            else
            {
                while (this_entry_type != 0x04 && this_entry_type != 0x01)
                {
                    rtfs_print_prompt_user((byte *)"Select the partition type 0.) FAT16(0x04), 1.) FAT12(0x01): ", working_buffer);
                    if (working_buffer[0] == '0') this_entry_type = 0x04;
                    else if (working_buffer[0] == '1') this_entry_type = 0x01;
                }
            }
            *(ppartitiontypes+partition_count) = this_entry_type;
        }
        partition_count += 1;
        cylinders_remaining -= requested_cylinders_this_partition;
    }
    return(partition_count);
}

static dword debug_dump_mbr(byte *pdata, BOOLEAN is_primary);

int dodumpmbr(int agc, byte **agv)
{
DDRIVE *pdr;
byte path[10];
BLKBUFF * scratch;
BLKBUFF bbuf_scratch;   /* Used by pc_sys_sector build a buffer structure with core from the driver level */
dword sector_number;

    use_args(agc, agv);

    /* Select the device, check media and clear change conditions */
    pdr = _choose_drive("Enter the drive to read A:, B: etc ", path);
    if (!pdr)
        return(-1);

    scratch = pc_sys_sector(pdr, &bbuf_scratch);

    /* Read location 0  */
    sector_number = 0;
    if (!raw_devio_xfer(pdr, sector_number, scratch->data, 1, TRUE, TRUE))
    {
        rtfs_print_one_string((byte *)"Read failed", PRFLG_NL);
        pc_free_sys_sector(scratch);
        return(-1);
    }

    /* Dump the primary, not printing extended paartitions yet  */
    sector_number = debug_dump_mbr(scratch->data, TRUE);

    pc_free_sys_sector(scratch);

    return (0);
}

static void debug_dump_bpb(DDRIVE *pdr, dword sector, byte *p, int depth);

int dodumpbpb(int agc, byte **agv)
{
DDRIVE *pdr;
byte path[10];
BLKBUFF * scratch;
BLKBUFF bbuf_scratch;   /* Used by pc_sys_sector build a buffer structure with core from the driver level */
dword sector_number;
int partition_status;
struct mbr_entry_specification mbr_specs[RTFS_MAX_PARTITIONS];
    use_args(agc, agv);

    /* Select the device, check media and clear change conditions */
    pdr = _choose_drive("Enter the drive to read A:, B: etc ", path);
    if (!pdr)
        return(-1);

    /* Read the partition table. If one is found the following fields will be set in the drive structure:
        pdr->drive_info.partition_base, pdr->drive_info.partition_size, pdr->drive_info.partition_type */
    partition_status = pc_read_partition_table(pdr, &mbr_specs[0]);

    if (partition_status >= pdr->partition_number)
        sector_number = pdr->drive_info.partition_base;
    else
        sector_number = 0;

    scratch = pc_sys_sector(pdr, &bbuf_scratch);

    if (!raw_devio_xfer(pdr, sector_number, scratch->data, 1, TRUE, TRUE))
    {
        rtfs_print_one_string((byte *)"Read failed", PRFLG_NL);
        pc_free_sys_sector(scratch);
        return(-1);
    }

    debug_dump_bpb(pdr, sector_number, scratch->data, 0);

    pc_free_sys_sector(scratch);

    return (0);
}

int dohackwin7(int agc, byte **agv)
{
DDRIVE *pdr;
byte path[10];
byte buf[10];
BLKBUFF * scratch;
BLKBUFF bbuf_scratch;   /* Used by pc_sys_sector build a buffer structure with core from the driver level */
dword sector_number;
byte *p;

    use_args(agc, agv);

    /* Select the device, check media and clear change conditions */
    pdr = _choose_drive("Enter the drive to read A:, B: etc ", path);
    if (!pdr)
        return(-1);

    scratch = pc_sys_sector(pdr, &bbuf_scratch);

    /* Read location 0  */
    sector_number = 0;
    if (!raw_devio_xfer(pdr, sector_number, scratch->data, 1, TRUE, TRUE))
    {
        rtfs_print_one_string((byte *)"Read failed", PRFLG_NL);
        pc_free_sys_sector(scratch);
        return(-1);
    }
	p = (byte *) scratch->data;
	p += 510;

	if (to_WORD((byte *) p)  ==  0xAA55)
	{
		rtfs_print_prompt_user((byte *)"Do you want to write to the device with Rtfs ? (Y/N) ", buf);
		if (tstsh_is_yes(buf))
		{
			rtfs_print_one_string((byte *)"Invalidating MBR so the volume is writeable", PRFLG_NL);
			fr_WORD(p, 0xAA66);
		}
		else
		{
			rtfs_print_one_string((byte *)"Fixing MBR so volume is readable on windows", PRFLG_NL);
			fr_WORD(p, 0xAA55);
		}
	}
	else
	{
        rtfs_print_one_string((byte *)"No signature on media, not changing it..", PRFLG_NL);
        pc_free_sys_sector(scratch);
        return(-1);
	}

    /* Dump the primary, not printing extended paartitions yet  */
    if (!raw_devio_xfer(pdr, sector_number, scratch->data, 1, TRUE, FALSE))
    {
        rtfs_print_one_string((byte *)"Write failed", PRFLG_NL);
        pc_free_sys_sector(scratch);
        return(-1);
    }

    pc_free_sys_sector(scratch);

    return (0);
 }


static word debug_unpack_sec_field(word cyl);
static word debug_unpack_cyl_field(word cyl);
static void dump_print(char *prompt, dword val, int radix, BOOLEAN newline);

static void debug_dump_info_structure(DDRIVE *pdr, dword sector, byte *p);
/*
static void debug_dump_fats(DDRIVE *pdr, FMTPARMS *pfmt, byte *pbuffer, dword buffer_size_bytes);
static void debug_dump_roots(DDRIVE *pdr, FMTPARMS *pfmt, byte *pbuffer, dword buffer_size_bytes);
*/

static void debug_dump_bpb(DDRIVE *pdr, dword sector, byte *p, int depth)
{
byte buff[32];

    dump_print("   DOS Boot sig (0xe9)  : ",  (dword ) *p, 16, TRUE);

    copybuff(buff, p+3, 8);
    buff[8] = 0;
    rtfs_print_one_string((byte *) "   OEM Name             :", 0);
    rtfs_print_one_string((byte *)buff, PRFLG_NL);
    dump_print("   Bytes per sector     :",    (dword) (to_WORD(p+0xb)), 10, TRUE);
    dump_print("   Sectors per cluster  :", (dword) *(p+0xd), 10, TRUE);
    dump_print("   Reserved sectors     :",    (dword) (to_WORD(p+0xe)), 10, TRUE);
    dump_print("   Number of fats       :",    (dword)*(p+0x10),20, TRUE);
    dump_print("   Num Root             :",    (dword) (to_WORD(p+0x11)), 10, TRUE);
    dump_print("   Total Sector(16)     :",   (dword) (to_WORD(p+0x13)), 10, TRUE);
    dump_print("   Media Description    :",   (dword) *(p+0x15), 16, TRUE);
    dump_print("   Total Sector(32)     :",   (dword) (to_DWORD(p+0x20)), 10, TRUE);

    dump_print("   Sector per track     :",   (dword) (to_WORD(p+0x18)), 10, TRUE);
    dump_print("   Heads                :",   (dword) (to_WORD(p+0x1a)), 10, TRUE);
    dump_print("   NumHide              :",   (dword) (to_DWORD(p+0x1c)), 10, TRUE);

    dump_print("   DOS 4 Ext Sig        :",   (dword) *(p+0x26), 16, TRUE);
    dump_print("   DOS 7 Ext Sig        :",   (dword) *(p+0x42), 16, TRUE);

    if (*(p+0x26) == 0x29)     /* Use MS-DOS 4.0 Extended parameter block for FAT12 and Fat16 */
    {
        rtfs_print_one_string((byte *)"     MSDOS-4.0 EPB detected ", PRFLG_NL);
        dump_print("     Sector per FAT     :",   (dword) (to_WORD(p+0x16)), 10, TRUE);
        dump_print("     Drive              :",   (dword) *(p+0x24), 10, TRUE);
        dump_print("     Binary Volume      :",   to_DWORD(p+0x27), 16, TRUE);
        rtfs_print_one_string((byte *) "   Text Volume          :", 0);
        copybuff(buff, p+0x2b, 11);
        buff[11] = 0;
        rtfs_print_one_string((byte *) &buff[0], PRFLG_NL);
        rtfs_print_one_string((byte *) "   Filesys Type         :", 0);
        copybuff(buff, p+0x36, 8);
        buff[8] = 0;
        rtfs_print_one_string((byte *) &buff[0], PRFLG_NL);
    }
    if (*(p+0x42) == 0x29)  /* Use MS-DOS 7.0 Extended parameter block FAT32 */
    {
        rtfs_print_one_string((byte *)"     MSDOS-7.0 EPB detected ", PRFLG_NL);
        dump_print("   Sector per FAT       :",   to_DWORD(p+0x24), 10, TRUE);
        dump_print("   Flags and Version    :",   to_DWORD(p+0x28), 16, TRUE);
        dump_print("   Root Start Cluster   :",  to_DWORD(p+0x2c), 10, TRUE);
        dump_print("   Info  Sector         :",   to_WORD(p+0x30), 10, TRUE);
        dump_print("   BackupBPB Sector     :",   to_DWORD(p+0x32), 10, TRUE);
        dump_print("   Drive                :",   *(p+0x40), 10, TRUE);
        dump_print("   Binary Volume        :",   to_DWORD(p+0x43), 16, TRUE);
        rtfs_print_one_string((byte *) "   Text Volume          :", 0);
        copybuff(buff, p+0x47, 11);
        buff[11] = 0;
        rtfs_print_one_string((byte *) &buff[0], PRFLG_NL);
        rtfs_print_one_string((byte *) "   Filesys Type         :", 0);
        copybuff(buff, p+0x52, 8);
        buff[8] = 0;
        rtfs_print_one_string((byte *) &buff[0], PRFLG_NL);

    }
    if (*(p+0x26) != 0x29 && *(p+0x42) != 0x29)
    {
        rtfs_print_one_string((byte *)"   No valid EB signature found ", PRFLG_NL);
    }
    dump_print(    "   Signature (aa55 ?)   :",   (dword) (to_WORD(p+0x1fe)), 16, TRUE);

    /* FAT32 */
    if (*(p+0x42) == 0x29)  /* Use MS-DOS 7.0 Extended parameter block FAT32 */
    {
        dword info_sector, backup_info_sector, backup_boot_sector;
        backup_boot_sector = sector + (dword) (to_WORD(p+0x32));
        info_sector = sector + (dword) (to_WORD(p+0x30));
        backup_info_sector = backup_boot_sector + (dword) (to_WORD(p+0x30));

        rtfs_print_one_string((byte *)"   Dumping Backup Boot Sector ", PRFLG_NL);
        if (depth == 0)
        {
            debug_dump_bpb(pdr, backup_boot_sector, p, 1);
            rtfs_print_one_string((byte *)"   Dumping Info Sector ", PRFLG_NL);
            debug_dump_info_structure(pdr, info_sector, p);
            rtfs_print_one_string((byte *)"   Dumping Backup Info Sector ", PRFLG_NL);
            debug_dump_info_structure(pdr, backup_info_sector, p);
        }
    }
}
static void debug_dump_info_structure(DDRIVE *pdr, dword sector, byte *p)
{

    if (!raw_devio_xfer(pdr, sector, p, 1, TRUE, TRUE))
    {
        rtfs_print_one_string((byte *)"Read failed", PRFLG_NL);
    }
    dump_print( "Signature 1 (Should be: 0x41615252):",   to_DWORD(p), 16, TRUE);
    dump_print( "Signature 2                        :",   to_DWORD(p+0x01e4), 16, TRUE);
    dump_print( "Free clusters                      :",   to_DWORD(p+0x01e8), 10, TRUE);
    dump_print( "First Free cluster                 :",   to_DWORD(p+0x01ec), 10, TRUE);
    dump_print( "Signature 3                        :",   to_DWORD(p+0x01fe), 16, TRUE);

}
/*
static void debug_dump_fats(DDRIVE *pdr, FMTPARMS *pfmt, byte *pbuffer)
{
    rtfs_print_one_string((byte *)"Dump FATs, fix me", PRFLG_NL);

}
static void debug_dump_roots(DDRIVE *pdr, FMTPARMS *pfmt, byte *pbuffer)
{
    rtfs_print_one_string((byte *)"Dump Roots, fix me", PRFLG_NL);

}
*/

static dword debug_dump_mbr(byte *pdata, BOOLEAN is_primary)
{
int i;
byte *pentry;
byte *ptable;
dword return_sector,dw;

    RTFS_ARGSUSED_INT(is_primary);

    ptable = pdata + 0x1be;

    /* The table has four 16 bit partition table entries followed by the signature
       Each entry in the table has the followin fields
          byte  Name   width
          0     boot   1
          1     s_head 1
          2     s_cyl  2
          4     p_typ  1
          5     e_head 1
          6     e_cyl; 2
          8     r_sec  4
          12    p_size 4
   */

    return_sector = 0;
    pentry = ptable;
    for(i = 0; i < 4; i++)
    {
        word  w;
        dw = to_DWORD(pentry+12); /* Print the entry if it has a size */
        if (dw)
        {
            dump_print("Partition #  ---->", i, 10, TRUE);
            dump_print("Boot ==  ",  *pentry, 16, FALSE);
            dump_print("Type ==  ",  *(pentry+4), 16, FALSE);

            dw = to_DWORD(pentry+12);
            dump_print("Size == ", dw, 10, FALSE);
            dw = to_DWORD(pentry+8);
            if (*(pentry+4) == 0x5 || *(pentry+4)  == 0xF)
            {
                dump_print("Extended Partition starts at == ", dw, 10, TRUE);
                return_sector = dw;
            }
            else
                dump_print("Start == ", dw, 10, TRUE);


            dump_print("SHead == ",  *(pentry+1), 10, FALSE);
            w = to_WORD(pentry+2);
            dump_print("Packed Cyl =  ", w, 16, FALSE);
            dump_print("Sector   =  ", debug_unpack_sec_field(w), 10, FALSE);
            dump_print("Cylinder =  ", debug_unpack_cyl_field(w), 10, TRUE);
            dump_print("EHead ==    ",  *(pentry+5), 10, FALSE);
            w = to_WORD(pentry+6);
            dump_print("Packed Cyl =  ", w, 16, FALSE);
            dump_print("Sector   = ", debug_unpack_sec_field(w), 10, FALSE);
            dump_print("Cylinder = ", debug_unpack_cyl_field(w), 10, TRUE);
            dw = to_DWORD(pentry+8);
        }
        pentry += 16;
    }
    dw = to_WORD(ptable + 64);
    dump_print("Signature ", dw, 16, TRUE);
    /* Now for the signature   */
    return(return_sector);
}

static word debug_unpack_sec_field(word cyl)
{
    return(cyl & 0x3f);
}
static word debug_unpack_cyl_field(word cyl)
{
word low, hi;
    low = (word)((cyl>>8) & 0xff);      /* Low 8 bit to from hibite */
    hi  = (word)((cyl<<2) & 0x0300);    /* hi 2 bits from bits 6&7 of low */
    low |= hi;
    return(low);
}


static void dump_print(char *prompt, dword val, int radix, BOOLEAN newline)
{
byte buff[32];
    pc_ltoa(val, &buff[0], radix);
    rtfs_print_one_string((byte *)prompt, 0);
    if (newline)
        rtfs_print_one_string((byte *)buff, PRFLG_NL);
    else
    {
        rtfs_print_one_string((byte *)buff, 0);
        rtfs_print_one_string((byte *)" , ",0);
    }
}


#endif /* Exclude from build if read only */
