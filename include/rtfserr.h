/*****************************************************************************
*Filename: RTFSERR.H - rtfs errno values
*
*
* EBS - RTFSAPI (Real Time File Manager)
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

#ifndef __RTFSERR__
#define __RTFSERR__ 1

/* Errno values */
#define PEACCES                  1 /* deleting an in-use object or witing to read only object */
#define PEBADF                   2 /* Invalid file descriptor*/
#define PEEXIST                  3 /* Creating an object that already exists */
#define PENOENT                  4 /* File or directory not found */
#define PENOSPC                  5 /* Out of space to perform the operation */
#define PESHARE                  6 /* Sharing violation */
#define PEINVALIDPARMS           7 /* Missing or invalid parameters */
#define PEINVAL                  8 /* Invalid api argument same as PEINVALIDPARMS */
#define PEINVALIDPATH            8 /* Invalid path name used as an argument */
#define PEINVALIDDRIVEID         9 /* Invalid drive specified in an argument */
#define PEIOERRORREAD            10 /* ProOnly Read error performing the API's function */
#define PEIOERRORWRITE           11 /* ProOnly Write error performing the API's function */
#define PECLOSED                 12 /* Media failure closed the volume. close the file */
#define PETOOLARGE               13 /* File size exceeds 0xFFFFFFFF */

#define PENOEMPTYERASEBLOCKS     20 /* Out of empty erase blocks and DRVPOL_NAND_SPACE_OPTIMIZE operating mode is not specified */
#define PEEINPROGRESS                    21 /* Cant perform operation because ASYNC operation in progress */
#define PENOTMOUNTED             22 /* PROPLUS Only Automount disabled, drive must be mounted*/
#define PEEFIOILLEGALFD          23 /* PROPLUS Only Api call not compatible file descriptor open method */


#define PEDEVICEFAILURE          51/* Driver reports that the device is not working */
#define PEDEVICENOMEDIA          52/* Driver reports that the device is empty */
#define PEDEVICEUNKNOWNMEDIA     53/* Driver reports that the device is not recognized */
#define PEDEVICEWRITEPROTECTED   54/* Driver reports that IO failed because the device is write protected */
#define PEDEVICEADDRESSERROR     55/* Driver reports that IO failed because the sector number or count were wrong */
#define PEINVALIDBPB             60/* No signature found in BPB (please format) */

#define PEIOERRORREADMBR         63/* IO error reading MBR (note: MBR is first to be read on a new insert) */
#define PEIOERRORREADBPB         64/* IO error reading BPB (block 0) */
#define PEIOERRORREADINFO32      65/* IO error reading fat32 INFO struc (BPB extension) */
#define PEIOERRORREADBLOCK       70/* Error reading a directory block  */
#define PEIOERRORREADFAT         71/* Error reading a fat block  */
#define PEIOERRORWRITEBLOCK      72/* Error writing a directory block  */
#define PEIOERRORWRITEFAT        73/* Error writing a fat block  */
#define PEIOERRORWRITEINFO32     74/* Error writing FAT32 info block */

#define PEINVALIDCLUSTER         100/* Unexpected cluster suspect volume corruption */
#define PEINVALIDDIR             101/* Unexpected directory content suspect volume corruption */
#define PEINTERNAL               102/* Unexpected condition */

#define PERESOURCEBLOCK          111/* Out of directory buffers  */
#define PERESOURCEFATBLOCK       112/* Out of fat buffers */

#define PERESOURCEREGION         113/* PROPLUS Only Out of region structures */
#define PERESOURCEFINODE         114/* Out of finode structures */
#define PERESOURCEDROBJ          115/* Out of drobj structures */
#define PERESOURCEDRIVE          116/* PROPLUS Only Out of drive structures */
#define PERESOURCEFINODEEX       117/* PROPLUS Only Out of extended32 finode structures */
#define PERESOURCESCRATCHBLOCK   119/* Out of scratch buffers */
#define PERESOURCEFILES          120/* PROPLUS Only Out of File Structure */
#define PECFIONOMAPREGIONS       121/* PROPLUS Only Map region buffer too small for pc_cfilio_extract to execute */
#define PERESOURCEHEAP           122/* PROPLUS Only Out of File Structure */
#define PERESOURCESEMAPHORE      123/* PROPLUS Only Out of File Structure */
#define PENOINIT                 124/*Module not initialized */
#define PEDYNAMIC                125/*Error with device driver dynamic drive initialization */
#define PERESOURCEEXFAT		     126/* PROPLUS Only Out of user supplied exFat structures */


#define PEFSCREATE               130/*Failure opening journal failed */
#define PEFSJOURNALFULL          131/*PROPLUS Only Journal file is full */
#define PEFSIOERRORWRITEJOURNAL  132/*PROPLUS Only Failure writing journal file */
#define PEFSRESTOREERROR         133/*PROPLUS Only Failure restoring from journal file */
#define PEFSRESTORENEEDED        134/*PROPLUS Only Restore required and AUTORESTORE disabled */
#define PEFSIOERRORREADJOURNAL   135/*PROPLUS Only Failure reading journal file */
#define PEFSBUSY                 136/*PROPLUS Only Failsafe enable called but already enabled */
#define PEFSMAPFULL              137/*PROPLUS Only Out of MAP buffers */
#define PEFSNOJOURNAL            138/*PROPLUS Only Journal file not found */

#define PEILLEGALERRNO           200 /* Illegal errno used by regression test */

#endif









/*
 *  @(#) ti.rtfs.config; 1, 0, 0, 0,17; 1-20-2009 17:04:20; /db/vtree/library/trees/rtfs/rtfs-a18x/src/
 */
