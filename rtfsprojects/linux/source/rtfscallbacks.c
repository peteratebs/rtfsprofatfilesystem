/****************************************************************************
*Filename: rtfscallbacks.c - RTFS Application callback funtions
*
* Callback functions that may be used to customize Rtfs for you application and target and to change default
* behaviors and configuration while Rtfs is running.
*
*
*  Read the user manual for more information.
*
*   Copyright EBS Inc , 1993-2008
*   All rights reserved.
*   This code may not be redistributed in source or linkable object form
*   without the consent of its author.
*
*   Generic unpopulated kernel porting layer
*
*
*/
#ifdef _MSC_VER
#include <windows.h>
#include <conio.h>
#include <stdio.h>
#include <malloc.h>
#endif

#include "rtfs.h"

#ifdef RTFS_LINUX
#include <time.h>
#endif

static void pc_get_system_date(DATESTR * pd);


/* int rtfs_sys_callback(int cb_code, void *pvargs)

   System service callback function. This callback provides the functionality previously provided by
   functions in the porting file, portkern.c. They were moved to the application callback layer so
   they can be adjusted at run time without the need for rebuilding the Rtfs library. */
int rtfs_sys_callback(int cb_code, void *pvargs)
{
	switch (cb_code) {

		case RTFS_CBS_INIT:
			/* This callback is made by pc_ertfs_init before any other callbacks are made or any operating system porting layer functions are called.
			   System initializations like opening the terminal window may be performed by the handler. */
		break;
		case RTFS_CBS_PUTS:
			/* use printf to print a line to the console, cputs() or a low level line output driver will work as well */
			printf("%s",(char *)pvargs);
		break;
		case RTFS_CBS_GETS:
			/* use gets() to retrieve a line from the console, low level line input driver will work as well */
			gets((char *)pvargs);
		break;
		case RTFS_CBS_GETDATE:
			/* Call a static funtion (see below) to populate the file timestamp function.
			   Modify pc_get_system_date() for your systems calendar support function */
			pc_get_system_date((DATESTR *) pvargs);
		break;
		case RTFS_CBS_10MSINCREMENT:
			*((byte *)pvargs) = 0;
		break;
		case RTFS_CBS_UTCOFFSET:
			*((byte *)pvargs) = 0xf0; /* Eastern time zone US */
		break;
		case RTFS_CBS_POLL_DEVICE_READY:
			/* Poll the device driver. If the system does not have interrupt driven inser / remove events polling may be done here */
		break;
#if (INCLUDE_EXFAT)	  /* Exfat buffer allocation */
		case RTFS_CBS_GETEXFATBUFFERS:
		{
		EXFATMOUNTPARMS *pexfm = (EXFATMOUNTPARMS *) pvargs;
		BOOLEAN good = TRUE;
			pexfm->BitMapBufferCore = (void *) 0;
			pexfm->BitMapBufferControlCore = (void *) 0;
			pexfm->UpCaseBufferCore = (void *) 0;
			pexfm->BitMapBufferPageSizeSectors = 1;
			pexfm->BitMapBufferSizeSectors = pexfm->BitMapSizeSectors;
			if (pexfm->BitMapBufferSizeSectors > 64)
			{
				pexfm->BitMapBufferSizeSectors = 64;
			}
			pexfm->BitMapBufferCore = (void *) rtfs_port_malloc(pexfm->SectorSizeBytes*pexfm->BitMapBufferSizeSectors);
			if (!pexfm->BitMapBufferCore)
				good = FALSE;
			if (good)
			{
				pexfm->BitMapBufferControlCore = (void *) rtfs_port_malloc(sizeof(FATBUFF)*pexfm->BitMapSizeSectors/pexfm->BitMapBufferPageSizeSectors);
				if (!pexfm->BitMapBufferControlCore)
					good = FALSE;
			}
			if (good && pexfm->UpcaseSizeBytes)
			{
				pexfm->UpCaseBufferCore = (void *) rtfs_port_malloc(pexfm->UpcaseSizeBytes);
				if (!pexfm->UpCaseBufferCore)
					good = FALSE;
			}
			if (!good)
			{
				if (pexfm->BitMapBufferCore)
					rtfs_port_free(pexfm->BitMapBufferCore);
				if (pexfm->BitMapBufferControlCore)
					rtfs_port_free(pexfm->BitMapBufferControlCore);
				if (pexfm->UpCaseBufferCore)
					rtfs_port_free(pexfm->UpCaseBufferCore);
				pexfm->BitMapBufferCore = (void *) 0;
				pexfm->BitMapBufferControlCore = (void *) 0;
				pexfm->UpCaseBufferCore = (void *) 0;
			}
		}
		break;
		case RTFS_CBS_RELEASEEXFATBUFFERS:
		{
		EXFATMOUNTPARMS *pexfm = (EXFATMOUNTPARMS *) pvargs;
			if (pexfm->BitMapBufferCore)
				rtfs_port_free(pexfm->BitMapBufferCore);
			if (pexfm->BitMapBufferControlCore)
				rtfs_port_free(pexfm->BitMapBufferControlCore);
			if (pexfm->UpCaseBufferCore)
				rtfs_port_free(pexfm->UpCaseBufferCore);
			pexfm->BitMapBufferCore = (void *) 0;
			pexfm->BitMapBufferControlCore = (void *) 0;
			pexfm->UpCaseBufferCore = (void *) 0;
		}
		break;
#endif
	}
	return(0);
}

/* int rtfs_app_callback (int cb_code, int iarg0, int iargs1, int iargs2, void *pvargs)

   Application callback function. This calllback provides the functionality previously provided
   by multiple individual callback functions and some adds some extra informational messages.
   Handlers that require a response can return zero to use default behavior */
int rtfs_app_callback(int cb_code, int iarg0, int iargs1, int iargs2, void *pvargs)
{

    RTFS_ARGSUSED_PVOID(pvargs);
    RTFS_ARGSUSED_INT(cb_code);
    RTFS_ARGSUSED_INT(iarg0);
    RTFS_ARGSUSED_INT(iargs1);
    RTFS_ARGSUSED_INT(iargs2);

	switch (cb_code) {
		case RTFS_CBA_INFO_MOUNT_STARTED:   /* iarg0 == drive number. Mount starting */
		break;
		case RTFS_CBA_INFO_MOUNT_FAILED:    /* iarg0 == drive number. Mount failed */
		break;
		case RTFS_CBA_INFO_MOUNT_COMPLETE:  /* iarg0 == drive number. Mount succeeded */
		break;
		case RTFS_CBA_ASYNC_MOUNT_CHECK:	/* iarg0 == drive number. Return 1 to request asynchronous mounting */
		break;
		case RTFS_CBA_ASYNC_START:    		/* iarg0 == cycle the statemachine */
		break;
        case RTFS_CBA_ASYNC_DRIVE_COMPLETE: /* iarg0 == drive number. iarg1 == operation iarg2 == status */
		break;
        case RTFS_CBA_ASYNC_FILE_COMPLETE:  /* iarg0 == file number. iarg1 == status */
		break;
        case RTFS_CBA_DVR_EXTRACT_RELEASE:  /* iarg0 == file number. iarg1 == abort if 1, overwritten if 0 */
		break;
	}
	return(0);
}

/* void rtfs_diag_callback(int cb_code, int iarg0)

   Diagnostic callback function. This callback provides an interface for fielding Rtfs asserts and for monitoring
   Rtfs errnos and to detect when device IO errors occur.
   The RTFS_CBD_ASSERT handler may be used to monitor for when Rtfs detects an unexpected internal state.
   The RTFS_CBD_ASSERT_TEST handler may be used to monitor for when an Rtfs regression test fails
   The RTFS_CBD_SETERRNO handler may be used to inspect Rtfs errno values and monitor for system errors.
   The RTFS_CBD_IOERROR hander may be used to monitor for IO errors.

*/
void rtfs_diag_callback(int cb_code, int iarg0)
{
int error_number;
long line_number = 0;
	switch (cb_code)
	{
		case RTFS_CBD_ASSERT:
			printf("Assert called \n");
			for (;;);
		break;
        case RTFS_CBD_ASSERT_TEST:
			printf("Assert Test called\n");
			for (;;);
		break;
	    case RTFS_CBD_IOERROR:   /* iarg0 contains the drive number */
			break;
	    case RTFS_CBD_SETERRNO:  /* iarg0 contains the error value */
				 error_number = iarg0;
	    	/* Monitor iarg0 to detect serious errors like resource allocation failures. For example if
	    	   iarg0 is PERESOURCEFILES then the application has exhausted all file structures, which may
	    	   indicate that it is not always closing files */
			switch (error_number) {
                /* Normal application errors*/
                case PEACCES:                 /* deleting an in-use object or witing to read only object */
                case PEBADF:                  /* Invalid file descriptor*/
                case PEEXIST:                 /* Creating an object that already exists */
                case PENOENT:                 /* File or directory not found */
                case PENOSPC:                 /* Out of space to perform the operation */
                case PESHARE:                 /* Sharing violation */
                case PEINVALIDPARMS:          /* Missing or invalid parameters */
 /*               case PEINVAL:                 Invalid api argument same as PEINVALIDPARMS */
                case PEINVALIDPATH:           /* Invalid path name used as an argument */
                case PEINVALIDDRIVEID:        /* Invalid drive specified in an argument */
                case PECLOSED:                /* Media failure closed the volume. close the file */
                case PETOOLARGE:              /* File size exceeds 0xFFFFFFFF */
					break;

                /* Other application errors*/
                case PENOEMPTYERASEBLOCKS:     /* Out of empty erase blocks and DRVPOL_NAND_SPACE_OPTIMIZE operating mode is not specified */
                case PEEINPROGRESS: 		   /* Cant perform operation because ASYNC operation in progress */
                case PENOTMOUNTED:             /* PROPLUS Only Automount disabled, drive must be mounted*/
                case PEEFIOILLEGALFD:          /* PROPLUS Only Api call not compatible file descriptor open method */
					break;

                /* Device level failures */
                case PEDEVICEFAILURE:          /* Driver reports that the device is not working */
                case PEDEVICENOMEDIA:          /* Driver reports that the device is empty */
                case PEDEVICEUNKNOWNMEDIA:     /* Driver reports that the device is not recognized */
                case PEDEVICEWRITEPROTECTED:   /* Driver reports that IO failed because the device is write protected */
                case PEDEVICEADDRESSERROR:     /* Driver reports that IO failed because the sector number or count were wrong */
                case PEINVALIDBPB:             /* No signature found in BPB (please format) */
                case PEIOERRORREAD:            /* ProOnly Read error performing the API's function */
                case PEIOERRORWRITE:          /* ProOnly Write error performing the API's function */
                case PEIOERRORREADMBR:         /* IO error reading MBR (note: MBR is first to be read on a new insert) */
                case PEIOERRORREADBPB:         /* IO error reading BPB (block 0) */
                case PEIOERRORREADINFO32:      /* IO error reading fat32 INFO struc (BPB extension) */
                case PEIOERRORREADBLOCK:       /* Error reading a directory block  */
                case PEIOERRORREADFAT:         /* Error reading a fat block  */
                case PEIOERRORWRITEBLOCK:      /* Error writing a directory block  */
                case PEIOERRORWRITEFAT:        /* Error writing a fat block  */
                case PEIOERRORWRITEINFO32:     /* Error writing FAT32 info block */
					break;

                /* Errors associated with
                   corrupted formats */
                case PEINVALIDCLUSTER:         /* Unexpected cluster suspect volume corruption */
                case PEINVALIDDIR:             /* Unexpected directory content suspect volume corruption */
                case PEINTERNAL:               /* Unexpected condition */
					break;

                /* Errors associated with
                   running out of resource */
                case PERESOURCEBLOCK:          /* Out of directory buffers  */
                case PERESOURCEFATBLOCK:       /* Out of fat buffers */
                case PERESOURCEREGION:         /* PROPLUS Only Out of region structures */
                case PERESOURCEFINODE:         /* Out of finode structures */
                case PERESOURCEDROBJ:          /* Out of drobj structures */
                case PERESOURCEDRIVE:          /* PROPLUS Only Out of drive structures */
                case PERESOURCEFINODEEX:       /* PROPLUS Only Out of extended32 finode structures */
                case PERESOURCESCRATCHBLOCK:   /* Out of scratch buffers */
                case PERESOURCEFILES:          /* PROPLUS Only Out of File Structure */
                case PECFIONOMAPREGIONS:       /* PROPLUS Only Map region buffer too small for pc_cfilio_extract to execute */
                case PERESOURCEHEAP:           /* PROPLUS Only Out of File Structure */
                case PERESOURCESEMAPHORE:      /* PROPLUS Only Out of File Structure */
                case PENOINIT:                 /*Module not initialized */
                case PEDYNAMIC:                /*Error with device driver dynamic drive initialization */
					break;
				default:
					break;
			}
			break;
		default:
			break;
	}
}

/* Set FAILSAFE_MODE_AUTOMATIC to one to hardwire Failsafe callback functions to perform as follows
    Restore the volume automatically when it is mounted.
    If a restore error occurs, ignore and allow the mount to proceed.
    Enable Journaling automatically when a volume is mounted.
    Flush the journal file and synchronize the volume structure after each API call.
    Disable journaling when the drive becomes too full to store the journal and contents.

    If a different setting is required for any individual callbacks they can be changed in the
    source code while leaving the other default FAILSAFE_MODE_AUTOMATIC settings enabled. (see Failsafe Tech reference guide)
*/
/* int rtfs_failsafe_callback(int cb_code, int driveno, int iarg0, void *pvargs0, void *pvargs1)

   Failsafe run time configuration callback function. This callback provides the functionality previously provided by
   multiple callback functions that were in a file designed to be recompiled along with the Failsafe source code.
   They provide the same functionality as the previous callback interface and default to the same configuration
   as the old method */
#if (INCLUDE_FAILSAFE_CODE)
int fs_flush_behavior = FS_CB_SYNC;			/* Default: Tell Failsafe to flush the journal file and synchronize the FAT volume */

int rtfs_failsafe_callback(int cb_code, int driveno, int iarg0, void *pvargs0, void *pvargs1)
{
    RTFS_ARGSUSED_PVOID(pvargs0);
    RTFS_ARGSUSED_PVOID(pvargs1);
    RTFS_ARGSUSED_INT(driveno);
    RTFS_ARGSUSED_INT(iarg0);

	switch (cb_code) {
		default:
			break;
		/*
		case RTFS_CB_FS_RETRIEVE_FIXED_JOURNAL_LOCATION:

		The default behavior for Failsafe is to use sectors in a region of contiguous unused clusters in the volume being journaled.
		This default behavior can be modified using the RTFS_CB_FS_RETRIEVE_FIXED_JOURNAL_LOCATION callback.
		The RTFS_CB_FS_RETRIEVE_FIXED_JOURNAL_LOCATION callback may be used place the Journal in a fixed region of the media.
		This may be anywhere on the media, in a seperate partition, in reserved sectors of a partition, or in contiguous sectors
		of a file within the partition.

			Note: driveno contains the drive number beign journaled
		*/
        case RTFS_CB_FS_RETRIEVE_FIXED_JOURNAL_LOCATION:
#if (0) /* Customize RTFS_CB_FS_RETRIEVE_FIXED_JOURNAL_LOCATION handler if you wan to use this option */
			*((dword *)pvarg0) = 	; /* set to the raw sector offset on the drive where the journal is kept */
			*((dword *)pvarg1) = 	; /* set the size, in sectors, of the fixed journal location */
			return(1);
#else
			return(0);
#endif
		break;
/* To change failsafe behavior, set FAILSAFE_MODE_AUTOMATIC to 0 and customize the following handlers
   See the failsafe technical reference guide for more information */
#define FAILSAFE_MODE_AUTOMATIC     0
 #if (FAILSAFE_MODE_AUTOMATIC == 0)
	/*
        case RTFS_CB_FS_FAIL_ON_JOURNAL_FULL:

		The default behavior for Failsafe when the journal file fills is to disable Failsafe and continue operating without journaling.

		By returning one, instead of the default zero, RTFS_CB_FS_FAIL_ON_JOURNAL_FULL callback can change this behavior and
		report a disk full condition, even though the disk is not actually full, but the journal file is.

		The RTFS_CB_FS_FAIL_ON_JOURNAL_FULL callback may be also be used to monitor journal file full conditions without changing the default
		behavior.

		Note: driveno contains the drive number beign journaled

		*/
        case RTFS_CB_FS_FAIL_ON_JOURNAL_FULL:
			return(0);	/* Default behavior return zero or fall through to disable journaling */
			/* return(1);	return one to force disk full error condition */
		break;

		/*
        case RTFS_CB_FS_FAIL_ON_JOURNAL_RESIZE:

		Called by Rtfs when there is not enough space to hold a Journal file as large as the size prescribed by the RTFS_CB_FS_RETRIEVE_JOURNAL_SIZE callback.

		This can happen while a mount is occuring or when Rtfs must allocate file or directory clusters and not enough free clusters are
		available.

		The default behavior for Rtfs in this condition if it is possible, is to reduce the journal file until enough space is available to
		fullfill the allocation request.

		By returning one instead of the default zero, the RTFS_CB_FS_FAIL_ON_JOURNAL_RESIZE callback can change this behavior and instruct Rtfs
		to abandon journaling if the free space gets too low. Rtfs will then call rtfs_failsafe_callback(RTFS_CB_FS_FAIL_ON_JOURNAL_FULL..) to
		determine it's next step.


		Note: driveno contains the drive number being journaled

		Note: This callback is only made if a fixed location journal file is not being used (see RTFS_CB_FS_RETRIEVE_FIXED_JOURNAL_LOCATION)

		*/
        case RTFS_CB_FS_FAIL_ON_JOURNAL_RESIZE:
        	return (0); /* Default behavior: Tell Rtfs to proceed with Failsafe disabled, all of the space occupied by the journal file will then be
        	               available proceed with the mount without restoring the volume */
        	/* return (1);  Tell Rtfs to terminate Jouranling */
		break;
		/*
		case RTFS_CB_FS_RETRIEVE_JOURNAL_SIZE:

		The default behavior for Failsafe if RTFS_CB_FS_RETRIEVE_FIXED_JOURNAL_LOCATION was not specified is create a journal file in
		in a region of contiguous unused clusters in the volume being journaled. Rtfs sets the initial size of the journal file created in this way
		to be 1/128th the size of the volume.

		The RTFS_CB_FS_RETRIEVE_JOURNAL_SIZE callback may be used to override the default sizing algorithm.

			Note: driveno contains the drive number beign journaled

		*/
        case RTFS_CB_FS_RETRIEVE_JOURNAL_SIZE:
#if (0) /* Customize to change the journal file size */
		{
		dword volume_size_sectors, journal_size_sectors;
			volume_size_sectors = *((dword *)pvarg0); /* get the size of the volume, you can use this to help decide the size of the journal file */
			journal_size_sectors = volume_size_sectors/256;	/* For example, make the journal size 1/256th the size of the disk */
			*((dword *)pvarg1) = journal_size_sectors; /* set the size, in sectors for the initial journal size, this is also the maximum size because the journal does not grow */
			return(1);
		}
 #endif
		return (0); /* Default behavior: use default values */
		break;

		/*
        case RTFS_CB_FS_RETRIEVE_RESTORE_STRATEGY:

		Called by Rtfs when the volume mount procedure detects a that a restore from journal file procedure is required.

		The default behavior for Rtfs when mounting a volume, if Failsafe is enabled for that volume is to restore the volume from the
		journal file and then continue the mount process.

		By returning FS_CB_CONTINUE or FS_CB_ABORT instead of the default zero (FS_CB_RESTORE), the RTFS_CB_FS_RETRIEVE_RESTORE_STRATEGY callback can
		change this behavior and instruct Rtfs to proceed with the mount without restoring the volume or instruct Rtfs to terminate the mount and set
		errno set to PEFSRESTORENEEDED.

		The RTFS_CB_FS_RETRIEVE_RESTORE_STRATEGY callback may be also be used to monitor when an Rtfs volume mount detects a that a restore from journal file
		procedure is required.

		Note: driveno contains the drive number beign journaled

		*/
        case RTFS_CB_FS_RETRIEVE_RESTORE_STRATEGY:
        	return (FS_CB_RESTORE); /* Default: Tell Rtfs to proceed with the mount after restoring the volume */
        	/* return (FS_CB_CONTINUE);  Tell Rtfs to proceed with the mount without restoring the volume */
        	/* return (FS_CB_ABORT);     Tell Rtfs to terminate the mount and set errno set to PEFSRESTORENEEDED. */
		break;
		/*
        case RTFS_CB_FS_FAIL_ON_JOURNAL_CHANGED:

		Called by Rtfs when the volume mount procedure detects a that a restore from journal file procedure is required but it also detects that
		the volume was modified since the Journal file was created.

		The default behavior for Rtfs in this condition is mount the volume anyway without performing a restore procedure.

		By returning one instead of the default zero, the RTFS_CB_FS_FAIL_ON_JOURNAL_CHANGED callback can
		change this behavior and instruct Rtfs to terminate the mount and set errno to PEFSRESTOREERROR.

		The RTFS_CB_FS_FAIL_ON_JOURNAL_CHANGED callback may also be used to monitor when an Rtfs volume mount detects a that a restore from journal file
		procedure is required but the volume was modified since the Journal file was created..

		Note: driveno contains the drive number beign journaled

		*/
        case RTFS_CB_FS_FAIL_ON_JOURNAL_CHANGED:
        	return (0); /* Default: Tell Rtfs to proceed with the mount without restoring the volume */
        	/* return (1);  Tell Rtfs to terminate the mount and set errno to PEFSRESTOREERROR. */
		break;

		/*
        case RTFS_CB_FS_CHECK_JOURNAL_BEGIN_NOT:

		Called by Rtfs when after a volume is mounted but before any write operations take place.
		procedure detects a that a restore from journal file procedure is required but it also detects that
		the volume was modified since the Journal file was created.


		If a volume was configured for journaling when the media was inserted or started, the default behavior is to automatically start journaling
		immediately after it was mounted. This callback provides a means of overriding that default behaviour an not automatically starting Failsafe.
		Journaling may later be started using the fs_api_enable() API function.

		The RTFS_CB_FS_CHECK_JOURNAL_BEGIN_NOT callback can	change this behavior and instruct Rtfs to proceed without journaling.

		RTFS_CB_FS_CHECK_JOURNAL_BEGIN_NOT 	must return one to instruct Failsafe to not start journaling.
		RTFS_CB_FS_CHECK_JOURNAL_BEGIN_NOT 	must return zero to (default) instruct Failsafe to start journaling.

		The RTFS_CB_FS_CHECK_JOURNAL_BEGIN_NOT callback may also be used to monitor when journaling is active.

		Note: driveno contains the drive number being journaled

		*/
        case RTFS_CB_FS_CHECK_JOURNAL_BEGIN_NOT:
        	return (0); /* Default behavior: Tell Rtfs to enable Failsafe Journaling for the volume that was mounted */
        	/* return (1);  Tell Rtfs not to start journaling */
		break;

		/*
        case RTFS_CB_FS_RETRIEVE_FLUSH_STRATEGY:

		Called by Rtfs after an API call has changed the volume and it is about to return to the user.

		If a volume is configured for journaling the default behavior is to automatically flush the Journal and synchronize the FAT volume with
		the journal before returning from the API call.

		The RTFS_CB_FS_RETRIEVE_FLUSH_STRATEGY callback can	change this default behavior. See the Failsafe manual for more information on choosing
		optimized flush strategies.

		RTFS_CB_FS_RETRIEVE_FLUSH_STRATEGY must return either FS_CB_SYNC, FS_CB_FLUSH or FS_CB_CONTINUE.

		Note: driveno contains the drive number being journaled

		*/
#endif /* (FAILSAFE_MODE_AUTOMATIC == 0) */
        case RTFS_CB_FS_RETRIEVE_FLUSH_STRATEGY:
			return(fs_flush_behavior);			/* Default: Tell Failsafe to flush the journal file and synchronize the FAT volume */

        	/* return(FS_CB_SYNC);     Default: Tell Failsafe to flush the journal file and synchronize the FAT volume */
        	/* return(FS_CB_FLUSH);    Tell Failsafe to flush the journal file but not synchronize the FAT volume*/
        	/* return(FS_CB_CONTIINUE);  Tell Failsafe to not flush the journal file and not syncronize */
		break;
	}

	return(0);	/* Fall through to request default behavior */
}
#endif /* (INCLUDE_FAILSAFE_CODE) */


/*
    When the system needs to date stamp a file it will call this routine
    to get the current time and date. YOU must modify the shipped routine
    to support your hardware's time and date routines.
*/

static void pc_get_system_date(DATESTR * pd)
{
#ifdef RTFS_WINDOWS
	/* Windows runtime provides rotuines specifically for this purpose */
    SYSTEMTIME systemtime;
    FILETIME filetime;

    GetLocalTime(&systemtime);
    SystemTimeToFileTime(&systemtime, &filetime);
    FileTimeToDosDateTime(&filetime, &pd->date, &pd->time);
#else
    word  year;     /* relative to 1980 */
    word  month;    /* 1 - 12 */
    word  day;      /* 1 - 31 */
    word  hour;
    word  minute;
    word  sec;      /* Note: seconds are 2 second/per. ie 3 == 6 seconds */

#ifdef RTFS_LINUX
#define USE_ANSI_TIME 1	  /* Linux supports it */
#else
#define USE_ANSI_TIME 0   /* Enable if your runtime environment supports ansi time functions */
#endif
#if (USE_ANSI_TIME)
	{ /* Use ansi time functions. */
    struct tm *timeptr;
    time_t timer;

    time(&timer);
    timeptr = localtime(&timer);

    hour    =   (word) timeptr->tm_hour;
    minute  =   (word) timeptr->tm_min;
    sec =   (word) (timeptr->tm_sec/2);
    /* Date comes back relative to 1900 (eg 93). The pc wants it relative to
        1980. so subtract 80 */
    year  = (word) (timeptr->tm_year-80);
    month = (word) (timeptr->tm_mon+1);
    day   = (word) timeptr->tm_mday;
	}
#else /* In not windows and not using ansi time functions use hardwired values. */
    /* Modify this code if you have a clock calendar chip and can retrieve the values from that device instead */
    hour = 19;    /* 7:37:28 PM */
    minute = 37;
    sec = 14;
    /* 3-28-2008 */
    year  = 18;       /* relative to 1980 */
    month = 3;      /* 1 - 12 */
    day   = 28;       /* 1 - 31 */
#endif
    pd->time = (word) ( (hour << 11) | (minute << 5) | sec);
    pd->date = (word) ( (year << 9) | (month << 5) | day);
#endif /* #ifdef WINDOWS #else */

}
