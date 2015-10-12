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


#define START_RAMDISK_DRIVER  0
#define START_HOSTDISK_DRIVER 1
#define START_NANDSIM_DRIVER  0
#define START_ROMDISK_DRIVER  0
#define START_FLASHDISK_DRIVER 0
#define START_WINDEV_DRIVER 1

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

static void init_example_device_drivers(void)
{
char buf[32];
int start_hostdisk=0;
int start_hostdev=0;

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

#if (INCLUDE_IDE)
    /* Start a virtual windows disk disk   */
	BLK_DEV_IDE_Idedisk_Mount();
#endif
#if (INCLUDE_FLOPPY)
	BLK_DEV_FLOPPY_Floppydisk_Mount();
#endif
}
