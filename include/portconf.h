/*****************************************************************************
*Filename: Portconf.h - Dummy file included only by Rtfilesv1.0 drivers
*
*
* EBS - RTFS (Real Time File Manager)
*
* Copyright Peter Van Oudenaren , 1993
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
/* portconf.h

This file is only included when supporting device drivers which supported the earlier
device driver model. The latest drivers where released in from "release_candidate60_f_2"
which was formally released as Rtfsilesex1.0.
*/


#define INCLUDE_IDE             0 /* - Include the IDE driver */
#define INCLUDE_PCMCIA          0 /* - Include the pcmcia driver */
#define INCLUDE_PCMCIA_SRAM     0 /* - Include the pcmcia static ram card driver */
#define INCLUDE_COMPACT_FLASH   0 /* - Support compact flash (requires IDE and PCMCIA) */
#define INCLUDE_FLASH_FTL       0 /* - Include the linear flash driver */
#define INCLUDE_ROMDISK         0 /* - Include the rom disk driver */
#define INCLUDE_RAMDISK         0 /* - Include the rom disk driver */
#define INCLUDE_MMCCARD         0 /* - Include the multi media flash card driver */
#define INCLUDE_SMARTMEDIA      0 /* - Include the smart media flash card driver */
#define INCLUDE_FLOPPY          0 /* - Include the floppy disk driver */
#define INCLUDE_HOSTDISK        1 /* - Include the host disk disk simulator */
#define INCLUDE_WINDEV          0 /* - Include windows direct device access */
#define INCLUDE_UDMA            0 /* - Include ultra dma support for the ide driver */
#define INCLUDE_82365_PCMCTRL   0 /* - Include the 82365 pcmcia controller driver */


/* These opcodes must be supported by the device driver's
   perform_device_ioctl() routine.
*/

#define DEVCTL_CHECKSTATUS 0
#define DEVCTL_WARMSTART        1
#define DEVCTL_POWER_RESTORE    2
#define DEVCTL_POWER_LOSS       3
#define DEVCTL_FORMAT           4
#define DEVCTL_GET_GEOMETRY     5
#define DEVCTL_REPORT_REMOVE    6
#define DEVCTL_SHUTDOWN         7



/* These codes are to be returned from the device driver's
   perform_device_ioctl() function when presented with
   DEVCTL_CHECKSTATUS as an opcode.
*/

#define DEVTEST_NOCHANGE        0 /* Status is 'UP' */
#define DEVTEST_NOMEDIA         1 /*  The device is empty */
#define DEVTEST_UNKMEDIA        2 /*  Contains unknown media */
#define DEVTEST_CHANGED         3 /*  Controller recognized and cleared a */
                                  /*  change condition */
#define BIN_VOL_LABEL 0x12345678L

void v1_0_poll_devices(void);
