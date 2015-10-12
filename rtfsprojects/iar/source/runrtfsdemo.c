// This is the main project file for VC++ application project
// generated using an Application Wizard.

#include <rtfs.h>

#if (INCLUDE_V_1_0_DEVICES == 0)


static void init_example_device_drivers(void);

int pc_ertfs_run(void)
{
    /* Run rtfs */
    /* Initialize ertfs  */
	if (!pc_ertfs_init())
    {
      printf("pc_ertfs_init failed\r");
	  return (0);
    }

	/* Initialize example device drivers here */
    init_example_device_drivers();

    printf("Calling test shell \n\r");

	tst_shell();
	printf("Returned from Rtfs shell..\n\r");

	return 0;
}


#define START_RAMDISK_DRIVER  0 // INCLUDE_RAMDISK
#define START_HOSTDISK_DRIVER INCLUDE_HOSTDISK // for NAND testing, default == INCLUDE_HOSTDISK
#define START_NANDSIM_DRIVER  0
#define START_ROMDISK_DRIVER  INCLUDE_ROMDISK
#define START_FLASHDISK_DRIVER 0
#define START_WINDEV_DRIVER INCLUDE_WINDEV
#define START_SDCARD_DRIVER INCLUDE_SDCARD

#if (START_SDCARD_DRIVER)
BOOLEAN BLK_DEV_SDCARD_Mount(void);
#endif


#if (START_RAMDISK_DRIVER)
BOOLEAN BLK_DEV_RD_Ramdisk_Mount(BOOLEAN doformat);
#endif
#if (START_HOSTDISK_DRIVER)
BOOLEAN BLK_DEV_hostdisk_Mount(void);
#endif
#if (START_NANDSIM_DRIVER)
BOOLEAN BLK_DEV_nandsim_Mount(void);
#endif
#if (START_ROMDISK_DRIVER)
BOOLEAN BLK_DEV_RD_Romdisk_Mount(void);
#endif
#if (START_FLASHDISK_DRIVER)
BOOLEAN BLK_DEV_RD_fls_Mount(void);
#endif
#if (START_WINDEV_DRIVER)
BOOLEAN BLK_DEV_windev_Mount(void);
#endif

/* bring in from the shell */
BOOLEAN tstsh_is_yes(byte *p);
void rtfs_print_prompt_user(byte *prompt, byte *buf);

#if (INCLUDE_FAILSAFE_CODE)
/* In evaluation mode this flag instructs host disk and host dev device drivers whether to
   autoenable falisafe */
BOOLEAN auto_failsafe_mode;
#endif
static void init_example_device_drivers(void)
{
int start_hostdisk=1;
int start_hostdev=1;

byte buf[80];
#if (INCLUDE_HOSTDISK&&INCLUDE_WINDEV)

printf("%s",
"=========================================================\n\
..The host disk driver simulates a disk using files.\n\
..When it is first executed it asks how large a disk you want.\n");

printf("%s",
"..After a simulated disk is initialized the first time you must\n\
..Run the commands FDISK C: and FORMAT (or EXFATFORMAT)\n\
..to format the volume before using it\n");

   rtfs_print_prompt_user((byte *)"Install the host disk emulator at C: ? (Y/N) ", buf);
    if (tstsh_is_yes(buf))
		start_hostdisk=1;
	else
		start_hostdisk=0;

printf("%s",
"=========================================================\n\
.. The host raw device driver directly accesses raw sectors on devices.\n\
.. On first accessed (DIR P:) it asks to select which raw device to \n\
.. mount with Rtfs. It will only list FAT and exFAT volumes.\n");

printf("%s",
".. The driver will not allow access to the primary device\n\
 .. Vista and Windows 7 users must have elevated prileges.\n\
 .. Use a usb drive ,SD card ,or secondary SATA device.\n");

printf("%s",
".. By default Vista and Windows 7 won\'t allow Rtfs to write to the device\n\
 .. but a command is provided in the Rtfs command shell, \"HACKWIN7\" to enable\n\
 .. Use caution when using the raw device driver with XP.\n\
 .. visit http://www.ebsembeddedsoftware.com/rtfsproplusdownload.html for complete instructions.\n");

    rtfs_print_prompt_user((byte *)"Install the host raw device driver a P: ? (Y/N) ", buf);
    if (tstsh_is_yes(buf))
		start_hostdev=1;
	else
		start_hostdev=0;

#if (INCLUDE_FAILSAFE_CODE)
printf("%s",
"=========================================================\n\
.. Failsafe journaling is available. Enable AUTO mode to automatically flush  \n\
.. and synchronize the journal after each operation. Disable AUTO mode to disable \n\
.. journaling by default and control it from the shell\n\n");

printf("%s",
".. AUTO mode is a lower performance option that does not utilize features of Failsafe\n\
.. it also automatically restores the volume when it is mounted, so you should \n\
.. disable it if you want to experiment with \"FS restore D:\" or \"FS info D:\" \n\
.. commands from the shell.\n\n");

   rtfs_print_prompt_user((byte *)"Enable Failsafe AUTO mode ? (Y/N) ", buf);
    if (tstsh_is_yes(buf))
		auto_failsafe_mode=TRUE;
	else
		auto_failsafe_mode=FALSE;
#endif
#endif

#if (START_HOSTDISK_DRIVER)
    /* Start a host disk at C: */
	if (start_hostdisk)
	{
		if (BLK_DEV_hostdisk_Mount())
    		printf("Host disk media mount succeeded you must format or fdisk+format the disk if created by mount\n\n");
		else
    		printf("Host disk media mount failed\n\n");
	}
#endif
#if (START_WINDEV_DRIVER)
	if (start_hostdev)
	{
		/* Start a driver with raw access to a disk  */
		if (!BLK_DEV_windev_Mount())
		{
			printf("FAIL FLASHDISK MOUNT\n\n");
		}
	}
#endif
#if (START_NANDSIM_DRIVER)
    /* Start a host disk at C: */
    if (BLK_DEV_nandsim_Mount())
    	printf("Simulated nand mount succeeded you must format or fdisk+format the disk if created by mount\n\n");
	else
    	printf("Simulated nand media mount failed\n\n");
#endif

#if (START_SDCARD_DRIVER)
    if (!BLK_DEV_SDCARD_Mount())
    {
         printf("FAIL SDCARD MOUNT\n\n");
    }

#endif
#if (START_RAMDISK_DRIVER)
    /* Start a ram disk at M: and Format (zero fill it(  */
    if (BLK_DEV_RD_Ramdisk_Mount(TRUE))
    {
        if (!pc_format_volume((byte *) "M:"))
            printf("Ramdisk create failed\n");
    }
#endif

#if (START_ROMDISK_DRIVER)
    /* Start a ram disk at N: and Format (zero fill it(  */
    if (!BLK_DEV_RD_Romdisk_Mount())
    {
        printf("FAIL ROMDISK MOUNT\n\n");
    }
#endif

#if (START_FLASHDISK_DRIVER)
    /* Start a flash disk   */
    if (!BLK_DEV_RD_fls_Mount())
    {
        printf("FAIL FLASHDISK MOUNT\n\n");
    }
#endif


}

#endif /* #if (INCLUDE_V_1_0_DEVICES == 0) */
