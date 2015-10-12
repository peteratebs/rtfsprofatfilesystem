/*****************************************************************************
*Filename: APIINIT.C - RTFS Inititialization and device attach
*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS inc, 1993 - 2006
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/

#include "rtfs.h"
#include "portconf.h"   /* For included devices */
#include "v10wrapper.h"   /* For included devices */

/* Initialize ERTFS and optionally attach default device drivers.
   This routine must be called before other ERTFS API routines are used.
   In the default environment this routine is called by rtfs_run(),
   (aiprun.c) the top level entry point for ERTFS provided applications.
*/

#define DRIVEIDA (byte *)"A:"
#define DRIVEIDB (byte *)"B:"
#define DRIVEIDC (byte *)"C:"
#define DRIVEIDD (byte *)"D:"
#define DRIVEIDE (byte *)"E:"
#define DRIVEIDF (byte *)"F:"
#define DRIVEIDG (byte *)"G:"
#define DRIVEIDH (byte *)"H:"
#define DRIVEIDI (byte *)"I:"
#define DRIVEIDJ (byte *)"J:"
#define DRIVEIDK (byte *)"K:"
#define DRIVEIDL (byte *)"L:"
#define DRIVEIDM (byte *)"M:"

static BOOLEAN attach_ertfs_devices();

BOOLEAN pc_v1_0_ertfs_init(void)
{
    /* Call the user supplied configuration function */
//    prtfs_cfg = 0;
//    if (!pc_ertfs_config())
//        return(FALSE);
    /* Call the memory and RTOS resource initiatilization function */
//    if (!pc_memory_init())
//        return(FALSE);
//    RTFS_DEBUG_UNQUIET_ERRNO(PENOENT) /* In debug mode start with PENOENT and all other errno reporting enabled */

    /* If you want to attach device handlers at a later time return here
    return(TRUE);
    */
    return(attach_ertfs_devices());
}


/* Device attach subroutines - These routines are all included
   and always called, whether the device is included at compile time,
   If the device is not included the attach routine does nothing */
static BOOLEAN attach_ide_devices(void);
static BOOLEAN attach_flashdisk_devices(void);
static BOOLEAN attach_romdisk_devices(void);
static BOOLEAN attach_ramdisk_devices(void);
static BOOLEAN attach_mmc_devices(void);
static BOOLEAN attach_smartmedia_devices(void);
static BOOLEAN attach_floppy_devices(void);
static BOOLEAN attach_hostdisk_devices(void);
static BOOLEAN attach_dynamic_devices(void);
static BOOLEAN attach_windev_devices(void);
static BOOLEAN attach_usb_disk_devices(void);

extern DDRIVE *v1_0_drno_to_dr_map[26]; /* MAPS DRIVE structure to DRIVE: */
/* Attach all devices */
static BOOLEAN attach_ertfs_devices()
{

    rtfs_memset(&v1_0_drno_to_dr_map[0], 0, sizeof(v1_0_drno_to_dr_map));
    if (!attach_hostdisk_devices()) /* Windows file emulating disk */
        return(FALSE);
    if (!attach_dynamic_devices())  /* Windows file emulating nand */
        return(FALSE);
    if (!attach_windev_devices())   /* Direct access to Windows device */
        return(FALSE);
    if (!attach_ide_devices())      /* ATA and Compact flash */
        return(FALSE);
    if (!attach_flashdisk_devices())/* NOR flash */
        return(FALSE);
    if (!attach_romdisk_devices())  /* ROM based disk image */
        return(FALSE);
    if (!attach_ramdisk_devices())  /* Temporary RAM disk */
        return(FALSE);
    if (!attach_usb_disk_devices()) /* USB disks */
        return(FALSE);
    if (!attach_mmc_devices())      /* MMC (Multi Media Card) */
        return(FALSE);
    if (!attach_smartmedia_devices())/* Smart Media (NAND flash) */
        return(FALSE);
    if (!attach_floppy_devices())    /* Floppy disk */
        return(FALSE);
    if (!v1_0_assign_media_structures())
		return(FALSE);

    /* Now scan the attached drive letter and set the default drive to the
       lowest valid drive id */
//    prtfs_cfg->default_drive_id = 27; /* greater than maximum legal value */
//    for (j = 0; j < 26; j++)
//    {
//        if (prtfs_cfg->drno_to_dr_map[j] &&
//        (prtfs_cfg->drno_to_dr_map[j]->driveno < prtfs_cfg->default_drive_id))
//            prtfs_cfg->default_drive_id =
//                prtfs_cfg->drno_to_dr_map[j]->driveno;
//    }
//    ERTFS_ASSERT(prtfs_cfg->default_drive_id < 27) /* No devices attached ? */
//    if (prtfs_cfg->default_drive_id == 27)
//        return(FALSE);
//    else
//        return(TRUE);
        return(TRUE);
}


static DDRIVE *attach_device_driver(
        byte *driveid, byte *device_name,
        dword drive_flags,
        BOOLEAN (*io_routine)(int driveno, dword sector,
                void  *buffer, word count, BOOLEAN reading),
        int (*ioctl_routine)(int driveno, int opcode, void * arg),
        BOOLEAN do_start);
static BOOLEAN start_device_driver(DDRIVE *pdr, byte *device_name);

void print_device_names(void);


#if (INCLUDE_FLASH_FTL)
BOOLEAN flashdisk_io(int d, dword b, void *v, word c, BOOLEAN r);
BOOLEAN flashdisk_perform_device_ioctl(int d, int c, void *p);
static BOOLEAN attach_flashdisk_devices(void)
{
    /* Select drive ID G: or better and start the device */
    if (attach_device_driver(DRIVEIDG, (byte *)"Flash Disk", 0, flashdisk_io,
                             flashdisk_perform_device_ioctl, TRUE))
        return(TRUE);
    else
        return(FALSE); /* Return failure because out of resources */
}
#endif

#if (INCLUDE_ROMDISK)
BOOLEAN romdisk_io(int d, dword b, void *v, word c, BOOLEAN r);
int romdisk_perform_device_ioctl(int d, int c, void *p);
static BOOLEAN attach_romdisk_devices(void)
{
    /* Select drive ID G: or better and start the device */
    if (attach_device_driver(DRIVEIDG, (byte *)"Rom Disk", 0, romdisk_io,
                             romdisk_perform_device_ioctl, TRUE))
        return(TRUE);
    else
        return(FALSE);

}
#endif

#if (INCLUDE_RAMDISK)
BOOLEAN ramdisk_io(int d, dword b, void *v, word c, BOOLEAN r);
int ramdisk_perform_device_ioctl(int d, int c, void *p);
static BOOLEAN attach_ramdisk_devices(void)
{
    /* Select drive ID G: or better and start the device */
    if (attach_device_driver(DRIVEIDG, (byte *)"Ram Disk", 0, ramdisk_io,
                             ramdisk_perform_device_ioctl, TRUE))
    {
        if (pc_format_media(DRIVEIDG))
        {
            if (pc_format_volume(DRIVEIDG))
        return(TRUE);
        }
    }
        return(FALSE);
}
#endif

#if (INCLUDE_MMCCARD)
BOOLEAN mmc_io(int d, dword b, void *v, word c, BOOLEAN r);
int mmc_perform_device_ioctl(int d, int c, void *p);
static BOOLEAN attach_mmc_devices(void)
{
    /* Select drive ID M: or better and start the device */
    if (attach_device_driver(DRIVEIDM, (byte *)"mmc Disk", DRIVE_FLAGS_PARTITIONED,
                        mmc_io, mmc_perform_device_ioctl, TRUE))
        return(TRUE);
    else
        return(FALSE);
}
#endif
#if (INCLUDE_SMARTMEDIA)
BOOLEAN smartmedia_io(int d, dword b, void *v, word c, BOOLEAN r);
int smartmedia_perform_device_ioctl(int d, int c, void *p);
static BOOLEAN attach_smartmedia_devices(void)
{
    /* Select drive ID M: or better and start the device */
    if (attach_device_driver(DRIVEIDM, (byte *)"smartmedia Disk", DRIVE_FLAGS_PARTITIONED,
                    smartmedia_io, smartmedia_perform_device_ioctl, TRUE))
        return(TRUE);
    else
        return(FALSE);
}
#endif
#if (INCLUDE_FLOPPY)
BOOLEAN floppy_io(int d, dword b, void *v, word c, BOOLEAN r);
int floppy_perform_device_ioctl(int d, int c, void *p);
static BOOLEAN attach_floppy_devices(void)
{
    /* Select drive ID A: or better and start the device */
    if (attach_device_driver(DRIVEIDA, (byte *)"floppy Disk", 0, floppy_io,
                             floppy_perform_device_ioctl, TRUE))
        return(TRUE);
    else
        return(FALSE);
}
#endif

// #if (INCLUDE_DYNAMIC_DRIVER)
// BOOLEAN drdynamic_io(int driveno, dword sector, void  *buffer, word count, BOOLEAN reading);
// int drdynamic_perform_device_ioctl(int driveno, int opcode, void * pargs);
// static BOOLEAN attach_dynamic_devices(void)
// {
// DDRIVE *pdr;
//     /* Get and partially init a drive give it the next ID C: or better */
//     pdr = attach_device_driver(DRIVEIDC, (byte *)"Simulated Nand", DRIVE_FLAGS_PARTITIONED,
//                        drdynamic_io, drdynamic_perform_device_ioctl, FALSE);
//     if (!pdr)
//         return(FALSE); /* Return failure because out of resources */
//     pdr->partition_number = 0;
//     prtfs_cfg->drno_to_dr_map[pdr->driveno] = pdr; /* MAPS DRIVE structure to DRIVE: */
//
//     if (!start_device_driver(pdr,(byte *)"Simulated Nand"))
//     { /* No contoller there for this configuration */
//         prtfs_cfg->drno_to_dr_map[pdr->driveno] = 0;
//         pc_memory_ddrive(pdr);
//         return(FALSE); /* Return failure because out of resources */
//     }
//     return(TRUE);
// }
// #endif


#if (INCLUDE_HOSTDISK)
BOOLEAN hostdisk_io(int d, dword b, void *v, word c, BOOLEAN r);
int hostdisk_perform_device_ioctl(int d, int c, void *p);
/* Select the name of the host disk file, to pass to the host disk driver */
#ifdef RTFS_WINDOWS
#define HOSTDISK_FILENAME "Hostdisk"
#endif
#ifdef RTFS_LINUX
#define HOSTDISK_FILENAME "/tmp/RTFS"
#endif
#if (!defined(RTFS_WINDOWS) && !defined(RTFS_LINUX))
/* If neither linux or windows are defined, use a file named "Hostdisk" in the current directory.
   if this doesn't work it must be changed */
#define HOSTDISK_FILENAME "Hostdisk"
#endif

static BOOLEAN attach_hostdisk_devices(void)
{
DDRIVE *pdr;
    /* Get and partially init a drive give it the next ID C: or better */
    pdr = attach_device_driver(DRIVEIDC, (byte *)HOSTDISK_FILENAME, DRIVE_FLAGS_PARTITIONED,
                       hostdisk_io,hostdisk_perform_device_ioctl, FALSE);
    if (!pdr)
        return(FALSE); /* Return failure because out of resources */
    pdr->partition_number = 0;
    v1_0_drno_to_dr_map[pdr->driveno] = pdr; /* MAPS DRIVE structure to DRIVE: */

    if (!start_device_driver(pdr,(byte *)HOSTDISK_FILENAME))
    { /* No controller there for this configuration */
        v1_0_drno_to_dr_map[pdr->driveno] = 0;
        pc_memory_ddrive(pdr);
    }
    /* Second partition now */
#define NUM_EXTRA_HOSTDISK_PARTITIONS 2
#if (NUM_EXTRA_HOSTDISK_PARTITIONS)
{
    int partition_number;
    for (partition_number = 1; partition_number <= NUM_EXTRA_HOSTDISK_PARTITIONS;partition_number++)
    {
        /* Get and partially init a drive give it the next ID C: or better */
        pdr = attach_device_driver(DRIVEIDC, (byte *)HOSTDISK_FILENAME, DRIVE_FLAGS_PARTITIONED,
                        hostdisk_io,hostdisk_perform_device_ioctl, FALSE);
        if (!pdr)
            return(FALSE); /* Return failure because out of resources */
        pdr->partition_number = partition_number;
        pdr->drive_flags |= DRIVE_FLAGS_VALID;
        v1_0_drno_to_dr_map[pdr->driveno] = pdr; /* MAPS DRIVE structure to DRIVE: */
    }
}
#endif
    return(TRUE);
}
#endif

#if (INCLUDE_WINDEV)
BOOLEAN windev_io(int d, dword b, void *v, word c, BOOLEAN r);
int windev_perform_device_ioctl(int d, int c, void *p);
#if (0)
static BOOLEAN attach_windev_devices(void)
{

    /* Select drive ID D: or better and start the device */
    /*  Uncomment mask to turn on Failsafe if desired */
    if(attach_device_driver(DRIVEIDD, (byte *)"Device", DRIVE_FLAGS_PARTITIONED,
                            windev_io,windev_perform_device_ioctl, TRUE))
        return(TRUE);
    else
        return(FALSE);
}
#endif

static BOOLEAN attach_windev_devices(void)
{
DDRIVE *pdr;
int partition_number;

    /* Get and partially init a drive give it the next ID C: or better */
    pdr = attach_device_driver(DRIVEIDC, (byte *)"Device", DRIVE_FLAGS_PARTITIONED,
                            windev_io,windev_perform_device_ioctl, FALSE);
    if (!pdr)
        return(FALSE); /* Return failure because out of resources */
    pdr->partition_number = 0;
    v1_0_drno_to_dr_map[pdr->driveno] = pdr; /* MAPS DRIVE structure to DRIVE: */

    if (!start_device_driver(pdr,(byte *)"Device"))
    { /* No contoller there for this configuration */
        v1_0_drno_to_dr_map[pdr->driveno] = 0;
        pc_memory_ddrive(pdr);
    }
    /* Attach entries for up to 6 partitions  */
    for (partition_number = 1;  partition_number < 6;partition_number++)
    {
        /* Get and partially init a drive give it the next ID C: or better */
        pdr = attach_device_driver(DRIVEIDC, (byte *)"Device", DRIVE_FLAGS_PARTITIONED,
                                windev_io,windev_perform_device_ioctl, FALSE);
        if (!pdr)
            return(FALSE); /* Return failure because out of resources */
        pdr->partition_number = partition_number;
        pdr->drive_flags |= DRIVE_FLAGS_VALID;
        v1_0_drno_to_dr_map[pdr->driveno] = pdr; /* MAPS DRIVE structure to DRIVE: */
    }

    return(TRUE);
}

#endif

#if (INCLUDE_USB_DISK)
BOOLEAN usb_disk_io(int d, dword b, void *v, word c, BOOLEAN r);
int usb_disk_perform_device_ioctl(int d, int c, void *p);
static BOOLEAN attach_usb_disk_devices(void)
{
    /* Select drive ID A: or better and start the device */
    if (attach_device_driver(DRIVEIDA, (byte *)"USBDisk", DRIVE_FLAGS_PARTITIONED,
        usbdisk_io, usbdisk_perform_device_ioctl, TRUE))
        return(TRUE);
    else
        return(FALSE);
}
#endif

#if (INCLUDE_IDE)
BOOLEAN ide_io(int d, dword b, void *v, word c, BOOLEAN r);
int ide_perform_device_ioctl(int d, int c, void *p);
static BOOLEAN attach_one_ide_device(
    byte *driveid,
    byte *device_name,
    dword drive_flags,
    int   controller_number,
    int   unit_number,
    int   partition_number,
    dword register_file_address,
    int   interrupt_number,
    int   pcmcia_slot_number,
    int   pcmcia_controller_number,
    byte  pcmcia_cfg_opt_value);

static BOOLEAN attach_ide_devices(void)
{
    /*              Primary  Secondary Tertiary Quaternary
     *              0x01f0  0x0170     0x01e8   0x0168
     *              0x03f6  0x0376     0x03ee   0x036e
     *              14      15         11       10
     *      Note: -1 for interrupts means used polled
     * Configuration option register values for PCMCIA
     *  0x43 secondary address, 0x42 primary address
     *  0x41 Use contiguos io mode, 0x40 Use memory mapped mode
     */

    /* Install drivers for a fixed IDE drive at the next drive letter C: or
       greater. Install handler for a second partition on the primary drive*/
    if (!attach_one_ide_device(DRIVEIDC, (byte *)"Primary IDE",
       DRIVE_FLAGS_PARTITIONED, 0, 0, 0, 0x1f0, 14, 0,0,0))
        return(FALSE);
    if (!attach_one_ide_device(DRIVEIDC, (byte *)"Primary IDE 2nd Partition",
       DRIVE_FLAGS_PARTITIONED, 0, 0, 1, 0x1f0, 14, 0,0,0))
        return(FALSE);
    /* Attach a second set of handlers on the secondary IDE controller
       select either a fixed ATA device or a compact flash slot */
#if (INCLUDE_COMPACT_FLASH)
#define IDE_DRIVE_FLAGS DRIVE_FLAGS_PARTITIONED|DRIVE_FLAGS_PCMCIA|DRIVE_FLAGS_PCMCIA_ATA
#define CFGOPT 0x43
#else
#define IDE_DRIVE_FLAGS DRIVE_FLAGS_PARTITIONED
#define CFGOPT 0
#endif
    if (!attach_one_ide_device(DRIVEIDC, (byte *)"Secondary IDE",
       IDE_DRIVE_FLAGS, 1, 0, 0, 0x170, 15, 0,0,CFGOPT))
        return(FALSE);
    if (!attach_one_ide_device(DRIVEIDC, (byte *)"Secondary IDE 2nd Partition",
       IDE_DRIVE_FLAGS, 1, 0, 1, 0x170, 15, 0,0,CFGOPT))
        return(FALSE);
    return(TRUE);
}
static BOOLEAN attach_one_ide_device(
    byte *driveid,
    byte *device_name,
    dword drive_flags,
    int   controller_number,
    int   unit_number,
    int   partition_number,
    dword register_file_address,
    int   interrupt_number,
    int   pcmcia_slot_number,
    int   pcmcia_controller_number,
    byte  pcmcia_cfg_opt_value)
{
DDRIVE *pdr;
    /* Get and partially init a drive give it the next ID C: or better */
    pdr = attach_device_driver(driveid, device_name, drive_flags, ide_io,
                                        ide_perform_device_ioctl, FALSE);
    if (!pdr)
        return(FALSE); /* Return failure because out of resources */
    pdr->controller_number = controller_number;
    pdr->partition_number = partition_number;
    pdr->logical_unit_number = unit_number;
    pdr->register_file_address = register_file_address;
    pdr->interrupt_number = interrupt_number;
    /* These are noops except for pcmcia */
    pdr->pcmcia_slot_number = pcmcia_slot_number;
    pdr->pcmcia_controller_number = pcmcia_controller_number;
    pdr->pcmcia_cfg_opt_value = pcmcia_cfg_opt_value;
    v1_0_drno_to_dr_map[pdr->driveno] = pdr; /* MAPS DRIVE structure to DRIVE: */
    if (!start_device_driver(pdr,device_name))
    { /* No contoller there for this configuration */
        v1_0_drno_to_dr_map[pdr->driveno] = 0;
        pc_memory_ddrive(pdr);
    }
    else
    {
        byte drname[16]; /* store "A:" etc */
        pc_drno_to_drname(pdr->driveno, drname, CS_CHARSET_NOT_UNICODE);
        RTFS_PRINT_STRING_2((byte *)"Device name : ", (byte *)device_name,0); /* "Device name : " */
        RTFS_PRINT_STRING_2((byte *)" Is mounted on " , drname,PRFLG_NL);    /* " Is mounted on "  */
    }
    return(TRUE);
}
#endif

/***************************************************************************
    pc_alloc_drive_id -  Allocate a free drive identifier (A:, B: ... )

Summary:
     BOOLEAN pc_alloc_drive_id(DDRIVE *pdr, byte *driveid)
        pdr     - Drive structure returned by a call to pc_memory_ddrive()
        driveid - First candidate drive letter to assign from

 Description
    Selects an unused drive identifier between the drive identifier passed
    in by the user and "Z:".

 Returns
    Returns TRUE if the mapping was succesful.
    Returns FALSE if all IDs between the requested ID and "Z:" are
    already mapped.

 See also:
    pc_release_drive_id()

***************************************************************************/

BOOLEAN pc_alloc_drive_id(DDRIVE *pdr, byte *driveid)
{
int drive_index;
BOOLEAN ret_val = FALSE;
    OS_CLAIM_FSCRITICAL()
    drive_index = pc_path_to_driveno(driveid, CS_CHARSET_NOT_UNICODE);
    if (drive_index >= 0)
    {
        for (;drive_index < 26; drive_index++)
        {
            if (!v1_0_drno_to_dr_map[drive_index])
            {
                pdr->driveno = drive_index;
                v1_0_drno_to_dr_map[drive_index] = pdr;
                ret_val = TRUE;
                break;
            }
        }
    }
    OS_RELEASE_FSCRITICAL()
    return(ret_val);

}
/***************************************************************************
    pc_release_drive_id - Release a drive identifier (A:-Z:) for re-use.


Summary:
    void pc_release_drive_id(int drive_index)
        drive_index - 0 = A:,1 = B:,.. 25=Z:

 Description
    Returns drive_index to the pool of unused drive identifiers.
    Future calls to pc_alloc_drive_id() will return this drive identifier
    if it fits the selection criteria.
***************************************************************************/

void pc_release_drive_id(int drive_index)
{
    OS_CLAIM_FSCRITICAL()
    if (drive_index >= 0 && drive_index < 26)
        v1_0_drno_to_dr_map[drive_index] = 0;
    OS_RELEASE_FSCRITICAL()
}


static DDRIVE *attach_device_driver(
        byte *driveid,
        byte *device_name,
        dword drive_flags,
        BOOLEAN (*io_routine)(int driveno, dword sector,
            void *vuffer, word count, BOOLEAN reading),
        int (*ioctl_routine)(int driveno, int opcode, void * arg),
        BOOLEAN do_start)
{
DDRIVE *pdr;
// dword  access_semaphore;

//    access_semaphore = rtfs_port_alloc_mutex();
//    if (!access_semaphore)
//        return(0);


    pdr = pc_memory_ddrive(0);
    if (!pdr)
        return(0);
    if (!pc_alloc_drive_id(pdr, (byte *)driveid))
    {
        pc_memory_ddrive(pdr);
        return(0);
    }
#if (STORE_DEVICE_NAMES_IN_DRIVE_STRUCT)
    rtfs_cs_strcpy(pdr->device_name, (byte *)device_name, CS_CHARSET_NOT_UNICODE);
#endif

    pdr->drive_flags = drive_flags;
    pdr->dev_table_drive_io = io_routine;
    pdr->dev_table_perform_device_ioctl = ioctl_routine;
//    pdr->access_semaphore = access_semaphore;


    if (do_start)
    {
        if (!start_device_driver(pdr, device_name))
        { /* No contoller there for this configuration */
            pc_release_drive_id(pdr->driveno);
            pc_memory_ddrive(pdr);
            pdr = 0;
        }
    }
    return(pdr);
}

static BOOLEAN start_device_driver(DDRIVE *pdr, byte *device_name)
{
    /* Call the WARMSTART rouitne, pass device name in case driver interprets it */
    if (v1_0_call_device_ioctl(pdr, DEVCTL_WARMSTART, (void *) device_name) != 0)
    { /* No contoller there for this configuration */
        return(FALSE);
    }
    else
    {
    byte drname[16]; /* store "A:" etc */
        pc_drno_to_drname(pdr->driveno, drname, CS_CHARSET_NOT_UNICODE);
        RTFS_PRINT_STRING_2((byte *)"Device name : ", (byte *)device_name,0); /* "Device name : " */
        RTFS_PRINT_STRING_2((byte *)" Is mounted on " , drname,PRFLG_NL);    /* " Is mounted on "  */
       // if ( (pdr->drive_flags & (DRIVE_FLAGS_INSERTED|DRIVE_FLAGS_REMOVABLE)) == (DRIVE_FLAGS_INSERTED|DRIVE_FLAGS_REMOVABLE))
       //     pdr->drive_flags &= ~DRIVE_FLAGS_INSERTED;
#if (!RTFS_CFG_READONLY)    /* Do not autoformat devices if read only file system */
        if (pdr->drive_flags&DRIVE_FLAGS_VALID && pdr->drive_flags&DRIVE_FLAGS_FORMAT)
        {
            RTFS_PRINT_STRING_2((byte *)"Not: Calling device format for drive Id - ", (byte *)drname,0);
            RTFS_PRINT_STRING_2((byte *)" as Device: ", (byte *)device_name,PRFLG_NL);
            RTFS_PRINT_STRING_1((byte *)"Note: Volume format is required.", PRFLG_NL);
            //if (!pc_format_media(drname))
            //    return(-1);
        }
#endif
    }
    return(TRUE);
}
//void print_device_names(void)
//{
//
//    RTFS_PRINT_STRING_1((byte *)"ERTFS Device List",PRFLG_NL); /* "ERTFS Device List" */
//    RTFS_PRINT_STRING_1((byte *)"=================",PRFLG_NL); /* "=================" */
//#if (!STORE_DEVICE_NAMES_IN_DRIVE_STRUCT)
//    RTFS_PRINT_STRING_1((byte *)"Name Logging Disabled",PRFLG_NL); /* "Name Logging Disabled" */
//#else
//{
//int j;
//DDRIVE *pdr;
//byte drname[8];
//
//    for (j = 0; j < 26; j++)
//    {
//        pdr = prtfs_cfg->drno_to_dr_map[j];
//        if (pdr)
//        {
//            pc_drno_to_drname(pdr->driveno, drname, CS_CHARSET_NOT_UNICODE);
//            RTFS_PRINT_STRING_2((byte *)"Device name : ", pdr->device_name,0); /* "Device name : " */
//            RTFS_PRINT_STRING_2((byte *)" Is mounted on " , drname,PRFLG_NL);    /* " Is mounted on "  */
//        }
//    }
//    RTFS_PRINT_STRING_1((byte *)"       =         ",PRFLG_NL); /* "       =         " */
//}
//#endif
//}
/* Stub functions for non compile time selected devices */
#if (!INCLUDE_FLASH_FTL)
static BOOLEAN attach_flashdisk_devices(void) { return(TRUE);}
#endif
#if (!INCLUDE_ROMDISK)
static BOOLEAN attach_romdisk_devices(void) {return(TRUE);}
#endif
#if (!INCLUDE_RAMDISK)
static BOOLEAN attach_ramdisk_devices(void){ return(TRUE); }
#endif
#if (!INCLUDE_MMCCARD)
static BOOLEAN attach_mmc_devices(void){return(TRUE);}
#endif
#if (!INCLUDE_SMARTMEDIA)
static BOOLEAN attach_smartmedia_devices(void){return(TRUE);}
#endif
#if (!INCLUDE_FLOPPY)
static BOOLEAN attach_floppy_devices(void){return(TRUE);}
#endif
#if (!INCLUDE_DYNAMIC_DRIVER)
static BOOLEAN attach_dynamic_devices(void){return(TRUE);}
#endif
#if (!INCLUDE_HOSTDISK)
static BOOLEAN attach_hostdisk_devices(void){return(TRUE);}
#endif
#if (!INCLUDE_WINDEV)
static BOOLEAN attach_windev_devices(void){return(TRUE);}
#endif
#if (!INCLUDE_USB_DISK)
static BOOLEAN attach_usb_disk_devices(void){return(TRUE);}
#endif
#if (!INCLUDE_IDE)
static BOOLEAN attach_ide_devices(void){return(TRUE);}
#endif
