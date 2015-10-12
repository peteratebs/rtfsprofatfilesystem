/*
|  MAIN.C -
|
|  EBS -
|
|   $Author: vmalaiya $
|   $Date: 2006/07/17 19:12:25 $
|   $Name:  $
|   $Revision: 1.3 $
|
|  Copyright EBS Inc. , 2005
|  All rights reserved.
|  This code may not be redistributed in source or linkable object form
|  without the consent of its author.
*/
/*****************************************************************************/
/* Header files
 *****************************************************************************/

/*****************************************************************************/
/* Macros
 *****************************************************************************/

/*****************************************************************************/
/* Types
 *****************************************************************************/

/*****************************************************************************/
/* Function Prototypes
/*****************************************************************************/

/*****************************************************************************/
/* Data
 *****************************************************************************/

/*****************************************************************************/
/* Function Definitions
 *****************************************************************************/

/*---------------------------------------------------------------------------*/

int pc_ertfs_run(void);
/*---------------------------------------------------------------------------*/

char *default_device_name = "Hostdisk_SEGMENT_0.HDK";
char *linux_device_name;

int main (int* argc, char* argv[])
{
    if (argc!=2)
    {
        linux_device_name = default_device_name;
        printf("\n\n");
        printf("Usage: %s fspath\n", argv[0]);
        printf("   <fspath> is not specified.\n");
        printf("   <fspath> may be a device file like (%s /dev/sd0) for example.\n", argv[0]);
        printf("   Rtfs will try to mount the host disk file. %s\n", linux_device_name);
        printf("   If you meant to do this you should be sure to at least once select\n");
        printf("   Install the host disk emulator at C:\n");
        printf("   to format a host disk.\n");
    }
    else
        linux_device_name = argv[1];
    printf("\n\n\n\n\n");
	return pc_ertfs_run();
}

/*---------------------------------------------------------------------------*/
