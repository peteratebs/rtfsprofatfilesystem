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
/* Initialize ERTFS and optionally attach default device drivers.
   This routine must be called before other ERTFS API routines are used.
   In the default environment this routine is called by rtfs_run(),
   (aiprun.c) the top level entry point for ERTFS provided applications.
*/

BOOLEAN pc_ertfs_init(void)
{
struct rtfs_init_resource_reply system_configuration;
    /* Call the user configuration manager */
	/* Let the callback layer initialize itself if it needs to */
	rtfs_sys_callback(RTFS_CBS_INIT,0);
	rtfs_memset(&system_configuration, 0, sizeof(system_configuration));
	rtfs_init_configuration(&system_configuration);

    /* Set up the rtfs configuration structure from the information in the the user configuration block
       dynamically allocate memory if user requested it  */
	if (!rtfs_dynamic_init_configuration(&system_configuration))
		return(FALSE);

    /* Call the memory and RTOS resource initiatilization function */
    if (!pc_memory_init())
        return(FALSE);

#if (INCLUDE_FAILSAFE_RUNTIME)
    return(pc_failsafe_init());
#else
    return(TRUE);
#endif
}




void print_device_names(void)
{

    RTFS_PRINT_STRING_1((byte *)"ERTFS Device List",PRFLG_NL); /* "ERTFS Device List" */
    RTFS_PRINT_STRING_1((byte *)"=================",PRFLG_NL); /* "=================" */
    RTFS_PRINT_STRING_1((byte *)"Name Logging Disabled",PRFLG_NL); /* "Name Logging Disabled" */
}
