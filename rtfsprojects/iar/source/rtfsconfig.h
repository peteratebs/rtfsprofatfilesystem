/* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS inc,   2008
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*******************************************************************************/

/* ================================== rtfsconfig.h ================================================   */


/*================================== defines to control general behavior of configuration section ========================   */
#define RTFS_CFG_SINGLE_THREADED                            1 /* If 1, share user buffer and failsafe buffers */
#define RTFS_CFG_INIT_DYNAMIC_ALLOCATION 					1  /* If 1 dynamically allocate system wide resources */


/*================================== defines to control system wide configuration  ========================   */
#if (1)
#define RTFS_CFG_MAX_DRIVES                                   6 /* Maximum number of mounted volumes */
#define RTFS_CFG_MAX_FILES                                   20 /* Maximum number of open files */
#define RTFS_CFG_MAX_SCRATCH_BUFFERS                         32 /* Minimum four see application notes */
#define RTFS_CFG_MAX_SCRATCH_DIRS                             8 /* Minimum one see application notes */
#define RTFS_CFG_MAX_USER_CONTEXTS                            3 /* Minimum 1 see application notes */
#define RTFS_CFG_MAX_REGION_BUFFERS                        1000 /* See application notes */
#define RTFS_CFG_SINGLE_THREADED_USER_BUFFER_SIZE         64*2048 //32768 /* Only used if SAMPLE_APP_SINGLE_THREADED == 1*/
#define RTFS_CFG_SINGLE_THREADED_FAILSAFE_BUFFER_SIZE     32768 /* Only used if SAMPLE_APP_SINGLE_THREADED == 1*/
#define RTFS_CFG_DIRS_PER_DRIVE         					 16 /* Extra directory objects required for internal processing */
#define RTFS_CFG_DIRS_PER_USER_CONTEXT   					  4 /* Do not reduce unless application is fixed and can be verified */
#else
#define RTFS_CFG_MAX_DRIVES                                   1 /* Maximum number of mounted volumes */
#define RTFS_CFG_MAX_FILES                                    2 /* Maximum number of open files */
#define RTFS_CFG_MAX_SCRATCH_BUFFERS                          4 /* Minimum four see application notes */
#define RTFS_CFG_MAX_SCRATCH_DIRS                             1 /* Minimum one see application notes */
#define RTFS_CFG_MAX_USER_CONTEXTS                            1 /* Minimum 1 see application notes */
#define RTFS_CFG_MAX_REGION_BUFFERS                         100 /* See application notes */
#define RTFS_CFG_SINGLE_THREADED_USER_BUFFER_SIZE           512 //32768 /* Only used if SAMPLE_APP_SINGLE_THREADED == 1*/
#define RTFS_CFG_SINGLE_THREADED_FAILSAFE_BUFFER_SIZE         0 /* Only used if SAMPLE_APP_SINGLE_THREADED == 1*/
#define RTFS_CFG_DIRS_PER_DRIVE         					 16 /* Extra directory objects required for internal processing */
#define RTFS_CFG_DIRS_PER_USER_CONTEXT   					  4 /* Do not reduce unless application is fixed and can be verified */
#endif



/*================================== end rtfsconfig.h ================================================   */
