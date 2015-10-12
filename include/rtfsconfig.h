/* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS inc,   2008
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*******************************************************************************/

/* ============================== rtfsconfig.h ============================== */


/* ===== defines to control general behavior of configuration section ======= */
#define RTFS_CFG_SINGLE_THREADED         1 /* If 1, share user and failsafe buffers */

#ifdef DALLOC
/* dynamically allocate system wide resources */
#define RTFS_CFG_INIT_DYNAMIC_ALLOCATION 1
#else
/* do not dynamically allocate system wide resources */
#define RTFS_CFG_INIT_DYNAMIC_ALLOCATION 0
#endif


/* ===== defines to control system wide configuration  ====================== */

#define RTFS_CFG_MAX_DRIVES            8 /* Maximum number of mounted volumes */
#define RTFS_CFG_MAX_FILES            20 /* Maximum number of open files */
#define RTFS_CFG_MAX_SCRATCH_BUFFERS  32 /* Minimum four see application notes */
#define RTFS_CFG_MAX_SCRATCH_DIRS      8 /* Minimum one see application notes */
#define RTFS_CFG_MAX_USER_CONTEXTS     3 /* Minimum 1 see application notes */
#define RTFS_CFG_MAX_REGION_BUFFERS 1000 /* See API guide */
#define RTFS_CFG_SINGLE_THREADED_USER_BUFFER_SIZE         64*2048 //32768 /* Only used if SAMPLE_APP_SINGLE_THREADED == 1*/
#define RTFS_CFG_SINGLE_THREADED_FAILSAFE_BUFFER_SIZE     32768 /* Only used if SAMPLE_APP_SINGLE_THREADED == 1*/
#define RTFS_CFG_DIRS_PER_DRIVE       16 /* Extra directory objects required for internal processing */
#define RTFS_CFG_DIRS_PER_USER_CONTEXT 4 /* Do not reduce unless application is fixed and can be verified */




/* ================ end rtfsconfig.h ================================   */
/*
 *  @(#) ti.rtfs.config; 1, 0, 0, 0,17; 1-20-2009 17:04:20; /db/vtree/library/trees/rtfs/rtfs-a18x/src/
 */
