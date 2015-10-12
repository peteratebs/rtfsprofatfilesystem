/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright Peter Van Oudenaren , 1993
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*
*
*   Implementation for the POLLED mode porting layer
*/
/*
*****************************************************************************
    Portmain.c   - Main entry point RTOS specific

    This file contains the main entry point for the executable. It is
    required only if building a stand alone application like our demo.
    Several examples of entry point are provided for different operating
    systems. The only requirement of the startup module is to call
    rtfs_app_entry() from within the contents of a thread. (or of the
    main thread for OS's like DOS or UNIX. (see rtfsdemo.c for rtfs_app_entry)

    Note: All that is required to use ERTFS is to call pc_rtfs_init() before
    using the API This file is required only if building the RTFS DEMO
    program as a stand alone application.
    If you wish to use the RTFS DEMO program from your existing framework
    from your existing simply call rtfs_app_entry().
    If you wish to use the RTFS library from your existing framework
    from your existing simply call pc_rtfs_init() and then make calls
    to the ERTFS API.

*****************************************************************************/

#include "rtfs.h"

/********************************************************************
 THIS TASK IS THE MAIN PROGRAM
********************************************************************/

#ifdef USEWIN32
void display_introduction(void);
#endif


void main()

{
#ifdef USEWIN32
    /* Display product information on WIN9X Demo systems */
    display_introduction();
#endif
    rtfs_app_entry();
}
