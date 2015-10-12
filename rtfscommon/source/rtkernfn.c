/*
* rtkernfn.c - Miscelaneous portable functions
*
* ERTFS portable process management and other functions.
* This file is portable but it requires interaction with the
* porting layer functions in portrtfs.c.
*
*   Copyright EBS Inc. 1987-2003
*   All rights reserved.
*   This code may not be redistributed in source or linkable object form
*   without the consent of its author.
*
*/

#include "rtfs.h"

#if (RTFS_CFG_LEAN)
#define INCLUDE_MULTIPLE_USERS 0
#else
#define INCLUDE_MULTIPLE_USERS 0
#endif

#if (INCLUDE_RTFS_PROPLUS) /* ProPlus specific memory initialization */
void pc_memory_init_proplus(void);
#endif

static void clear_user_entry(RTFS_SYSTEM_USER *pu, void *plcwd)
{
	rtfs_memset((byte *)pu, 0, sizeof(RTFS_SYSTEM_USER));
	pu->plcwd = plcwd;
}
static void clear_user_table(void)
{
int i;
void **pplcwd;

	pplcwd = (void **) prtfs_cfg->rtfs_user_cwd_pointers;
	rtfs_memset(pplcwd, 0, prtfs_cfg->cfg_NUM_USERS * prtfs_cfg->cfg_NDRIVES * sizeof(void *));
    for (i = 0; i < prtfs_cfg->cfg_NUM_USERS; i++, pplcwd += prtfs_cfg->cfg_NDRIVES)
    	clear_user_entry(&prtfs_cfg->rtfs_user_table[i], (void *) pplcwd);
}

BOOLEAN rtfs_resource_init(void)   /*__fn__*/
{

    prtfs_cfg->userlist_semaphore = pc_rtfs_alloc_mutex("Rtfs_userlist");
    if (!prtfs_cfg->userlist_semaphore)
        return(FALSE);
    prtfs_cfg->critical_semaphore = pc_rtfs_alloc_mutex("Rtfs_critical");
    if (!prtfs_cfg->critical_semaphore)
        return(FALSE);
    prtfs_cfg->mountlist_semaphore = pc_rtfs_alloc_mutex("Rtfs_mountlist");
    if (!prtfs_cfg->mountlist_semaphore)
        return(FALSE);
    return(TRUE);
}



DROBJ *rtfs_get_user_pwd(RTFS_SYSTEM_USER *pu, int driveno, BOOLEAN doclear)
{
	int i;
    ERTFS_ASSERT((pu && pu->plcwd))
	if (pu && pu->plcwd)
	{
		DROBJ **ppobj;
		ppobj = (DROBJ **) pu->plcwd;
		for (i = 0; i < prtfs_cfg->cfg_NDRIVES; i++, ppobj++)
		{
		DROBJ *pobj;
			pobj = *ppobj;
			if (pobj && pobj->pdrive && pobj->pdrive->driveno == driveno)
			{
				if (doclear)
					*ppobj = 0;
   				return(pobj);
			}
		}
	}
	return(0);
}

void rtfs_set_user_pwd(RTFS_SYSTEM_USER *pu, DROBJ *pobj)
{
	int i;
    ERTFS_ASSERT((pu && pu->plcwd))
	if (pu && pu->plcwd)
	{
		DROBJ **ppobj;
		ppobj = (DROBJ **) pu->plcwd;
		for (i = 0; i < prtfs_cfg->cfg_NDRIVES; i++, ppobj++)
		{
			if (!*ppobj)
			{
   				*ppobj = pobj;
				break;
			}
		}
	}
}

#if (INCLUDE_THREAD_EXIT_CALLBACK)
void rtfs_port_set_task_exit_handler(void);
#endif

PRTFS_SYSTEM_USER rtfs_get_system_user(void)
{
#if (!INCLUDE_MULTIPLE_USERS)
    prtfs_cfg->rtfs_user_table[0].task_handle = 1;
    return(&prtfs_cfg->rtfs_user_table[0]);
#else
int i,driveno;
dword t;

    t = rtfs_port_get_taskid();
#if (INCLUDE_THREAD_SETENV_SUPPORT)
{
	RTFS_SYSTEM_USER *puser;

	puser = (RTFS_SYSTEM_USER *) rtfs_port_get_task_env(t);
	if (puser)
		return(puser);
	// else fall through and assign one
}
#else
    if (prtfs_cfg->cfg_NUM_USERS == 1)
    {
        prtfs_cfg->rtfs_user_table[0].task_handle = t;
        return(&prtfs_cfg->rtfs_user_table[0]);
    }
	/* Generic solutions scans user tables for task_handle == task_id */
    for (i = 0; i < prtfs_cfg->cfg_NUM_USERS; i++)
    {
        if (t == prtfs_cfg->rtfs_user_table[i].task_handle)
            return(&prtfs_cfg->rtfs_user_table[i]);
    }
	/* else fall through and assign one */
#endif
    /* Did not find one.. so assign one from between 1 and n */
	rtfs_port_claim_mutex(prtfs_cfg->userlist_semaphore);
    for (i = 1; i < prtfs_cfg->cfg_NUM_USERS; i++)
    {
        if (!prtfs_cfg->rtfs_user_table[i].task_handle)
        {
return_it:
			clear_user_entry(&prtfs_cfg->rtfs_user_table[i], prtfs_cfg->rtfs_user_table[i].plcwd);
            prtfs_cfg->rtfs_user_table[i].task_handle = t;
			rtfs_port_release_mutex(prtfs_cfg->userlist_semaphore);
			// Set Rtfsuser structure environment variable for the thread if possible
#if (INCLUDE_THREAD_SETENV_SUPPORT)
			rtfs_port_set_task_env((void *)&prtfs_cfg->rtfs_user_table[i]);
#endif

#if (INCLUDE_THREAD_EXIT_CALLBACK)
			/* If the OS supports it, schedule pc_free_user to run when the task exits */
			rtfs_port_set_task_exit_handler();
#endif
            return(&prtfs_cfg->rtfs_user_table[i]);
        }
    }
    /* We are out of user structures so use element 0 */
    i = 0;
    /*  Bug fix 02-01-2007 - If we are using the default user (0), make sure the
        current working directory objects are freed and the finode access counts
        are reduced */
    for(driveno = 0; driveno < 26; driveno++)
    {
		DROBJ *pobj;
		pobj = rtfs_get_user_pwd(&prtfs_cfg->rtfs_user_table[i], driveno, TRUE); /* Get cwd for driveno and clear it */
        if(pobj)
        {
            pc_freeobj(pobj);
        }
    }

    goto return_it;
#endif
}


void  pc_free_user(void)                            /*__fn__*/
{
	pc_free_other_user(rtfs_port_get_taskid());
}

void  pc_free_other_user(dword taskId)                            /*__fn__*/
{
int driveno;
RTFS_SYSTEM_USER *pu;
DROBJ *pobj;

    RTFS_ARGSUSED_DWORD(taskId);
#if (INCLUDE_THREAD_SETENV_SUPPORT)
	pu = (RTFS_SYSTEM_USER *) rtfs_port_get_task_env(taskId);
#else
    pu = rtfs_get_system_user();
#endif
    if (pu)
    {
        for (driveno = 0; driveno < 26; driveno++)
        {
        	pobj = rtfs_get_user_pwd(pu, driveno, TRUE); /* Get cwd for driveno and clear it */
            if (pobj)
            {
                pc_freeobj(pobj);
            }
        }
		clear_user_entry(pu, pu->plcwd);
    }
}

/* pc_free_all_users() - Run down the user list releasing drive resources
 *
 * This routine is called by RTFS when it closes a drive.
 * The routine must release the current directory object for that drive
 * for each user. If a user does not have a CWD for the drive it should
 * not call pc_freeobj.
 *
 * In the reference port we cycle through our array of user structures
 * to provide the enumeration. Other implementations are equally valid.
 */

void  pc_free_all_users(int driveno)                            /*__fn__*/
{
DROBJ *pobj;
RTFS_SYSTEM_USER *pu;

    if (prtfs_cfg->cfg_NUM_USERS == 1)
    {
		pu = &prtfs_cfg->rtfs_user_table[0];
        pobj = rtfs_get_user_pwd(pu, driveno,TRUE); /* Get cwd for driveno and clear it */

        if (pobj)
        {
            pc_freeobj(pobj);
        }
    }
#if (INCLUDE_MULTIPLE_USERS)
    else
    {
        int i;
        for (i = 0; i < prtfs_cfg->cfg_NUM_USERS; i++)
        {
			pu = &prtfs_cfg->rtfs_user_table[i];
			if (pu->task_handle)
				pobj = rtfs_get_user_pwd(pu, driveno, TRUE);   /* Get cwd for driveno and clear it */
			else
				pobj = 0;

            if (pobj)
            {
                pc_freeobj(pobj);
            }
        }
    }
#endif
}


/* int rtfs_set_driver_errno() - set device driver errno for the calling task

   Saves driver errno for the calling task in array based on callers taskid.

   Note: This routine must not be called from the interrupt service layer

   Returns nothing
*/
void rtfs_set_driver_errno(dword error)    /*__fn__*/
{
    	prtfs_cfg->rtfs_driver_errno = error;
}


/* ********************************************************************

dword rtfs_get_driver_errno() - get device driver errno for the calling task

  Returns device driver errno for the calling task in array based on
  callers taskid.
*/

dword rtfs_get_driver_errno(void)    /*__fn__*/
{
    return(prtfs_cfg->rtfs_driver_errno);
}


/* int rtfs_set_errno() - set errno for the calling task

   Saves errno for the calling task in array based on callers taskid.

   Returns -1
*/
void rtfs_clear_errno(void)    /*__fn__*/
{
	if (prtfs_cfg)
    	rtfs_get_system_user()->rtfs_errno = 0;
}



static BOOLEAN _error_propogation_guard_enabled(RTFS_SYSTEM_USER *pu)    /*__fn__*/
{
	/* If the following errnos are already set leave them active until it is cleared. This way they wont be overridden by higher levels */
	switch (pu->rtfs_errno) {

    	case PEDEVICEFAILURE      	:
    	case PEDEVICENOMEDIA      	:
    	case PEDEVICEUNKNOWNMEDIA 	:
    	case PEDEVICEWRITEPROTECTED	:
    	case PEDEVICEADDRESSERROR 	:
    	case PENOEMPTYERASEBLOCKS 	:
    	case PEDYNAMIC				:
		case PEFSRESTORENEEDED		:
		case PEFSRESTOREERROR		:
			return(TRUE);
		default:
			return(FALSE);
	}
}

void _debug_rtfs_set_errno(int error, char *filename, long linenumber)
{
RTFS_SYSTEM_USER *pu;
	/* Pass error value to application callback error, it can monitor for resource errors */
	rtfs_diag_callback(RTFS_CBD_SETERRNO, error);
	if (prtfs_cfg)
	{
    	pu = rtfs_get_system_user();
    	/* Don't set errno if we have already set a value thhat must be returned to the application layer */
    	if (_error_propogation_guard_enabled(pu))
    		return;
    	pu->rtfs_errno = error;
	}
}

void _rtfs_set_errno(int error)    /*__fn__*/
{
RTFS_SYSTEM_USER *pu;

	/* Pass error value to application callback error, it can monitor for resource errors */
	rtfs_diag_callback(RTFS_CBD_SETERRNO, error);
	if (prtfs_cfg)
	{
    	pu = rtfs_get_system_user();
    	/* Don't set errno if we have already set a value thhat must be returned to the application layer */
    	if (_error_propogation_guard_enabled(pu))
    		return;
    	pu->rtfs_errno = error;
	}
}


/* ********************************************************************

int get_errno() - get errno for the calling task

  Returns errno for the calling task in array based on callers taskid.
*/

int get_errno(void)    /*__fn__*/
{
    return(rtfs_get_system_user()->rtfs_errno);
}


/* Miscelaneous functions */


/**************************************************************************
    PC_NUM_DRIVES -  Return total number of drives in the system

 Description
    This routine returns the number of drives in the system

 Returns
    The number
*****************************************************************************/
int pc_num_drives(void)                                 /* __fn__ */
{
    return(prtfs_cfg->cfg_NDRIVES);
}

/**************************************************************************
    PC_NUSERFILES -  Return total number of uses allowed in the system

 Description
    This routine returns the number of user in the system

 Returns
    The number
*****************************************************************************/
int pc_num_users(void)                                  /* __fn__ */
{
    return(prtfs_cfg->cfg_NUM_USERS);
}

/**************************************************************************
    PC_NUSERFILES -  Return total number of userfiles alloed in the system

 Description
    This routine returns the number of user files in the system

 Returns
    The number
*****************************************************************************/

int pc_nuserfiles(void)                                 /* __fn__ */
{
    return(prtfs_cfg->cfg_NUSERFILES);
}

/**************************************************************************
    PC_VALIDATE_DRIVENO -  Verify that a drive number is <= prtfs_cfg->cfg_NDRIVES

 Description
    This routine is called when a routine is handed a drive number and
    needs to know if it is within the number of drives set during
    the congiguration.

 Returns
    TRUE if the drive number is valid or FALSE.
*****************************************************************************/

BOOLEAN pc_validate_driveno(int driveno)                            /* __fn__ */
{
    if ((driveno < 0) || (driveno > 25))
        return(FALSE);
    else
        return(TRUE);
}

/**************************************************************************
    PC_MEMORY_INIT -  Initialize and allocate File system structures.

    THIS ROUTINE MUST BE CALLED BEFORE ANY FILE SYSTEM ROUTINES !!!!!!
    IT IS CALLED BY THE PC_ERTFS_INIT() Function

 Description
    This routine must be called before any file system routines. Its job
    is to allocate tables needed by the file system. We chose to implement
    memory management this way to provide maximum flexibility for embedded
    system developers. In the reference port we use malloc to allocate the
    various chunks of memory we need, but we could just have easily comiled
    the tables into the BSS section of the program.

    Use whatever method makes sense in you system.

    Note the total number of bytes allocated by this routine is:
        (sizeof(DDRIVE) * prtfs_cfg->cfg_NDRIVES) + (sizeof(PC_FILE)*NUSERFILES) +
        (sizeof(BLKBUFF)*NBLKBUFFS)+ (sizeof(DROBJ)*NDROBJS) +
        (sizeof(FINODE)*NFINODES)


 Returns
    TRUE on success or no ON Failure.
*****************************************************************************/

static BOOLEAN pc_memory_init_drive_structures(void);

BOOLEAN pc_memory_init(void)                                                /*__fn__*/
{
    int i,j;
    DROBJ *pobj;
    FINODE *pfi;

/* Note: cfg_NDRIVES semaphores are allocated and assigned to the individual
   drive structure within routine pc_ertfs_init() */
    /* Initialize a user table */
#if (!INCLUDE_MULTIPLE_USERS)
    prtfs_cfg->cfg_NUM_USERS = 1;
#endif
	clear_user_table();

    /* Call the kernel level initialization code */
    if (!rtfs_resource_init())
        return(FALSE);

    /* Initialize the region manager structures */
    pc_fraglist_init_freelist();

    /* Initialize the default directory sector and scratch buffer array   */
    pc_initialize_block_pool(&prtfs_cfg->buffcntxt, prtfs_cfg->cfg_NBLKBUFFS,
            prtfs_cfg->mem_block_pool, prtfs_cfg->mem_block_data, RTFS_CFG_DEFAULT_SECTOR_SIZE_BYTES);

    /* Initialize drive structures */
    if (!pc_memory_init_drive_structures())
        return(FALSE);

    /* make a NULL terminated freelist of the DROBJ pool using
        pdrive as the link. This linked freelist structure is used by the
        DROBJ memory allocator routine. */
    pobj = prtfs_cfg->mem_drobj_freelist = prtfs_cfg->mem_drobj_pool;
    pobj->is_free = TRUE;
    for (i = 0,j = 1; i < prtfs_cfg->cfg_NDROBJS-1; i++, j++)
    {
        pobj = prtfs_cfg->mem_drobj_freelist + j;
        pobj->is_free = TRUE;
        prtfs_cfg->mem_drobj_freelist[i].pdrive = (DDRIVE *) pobj;
    }
    prtfs_cfg->mem_drobj_freelist[prtfs_cfg->cfg_NDROBJS-1].pdrive = 0;

    /* Make a NULL terminated FINODE freelist using
        pnext as the link. This linked freelist is used by the FINODE
        memory allocator routine */
    pfi = prtfs_cfg->mem_finode_freelist = prtfs_cfg->mem_finode_pool;
    for (i = 0; i < prtfs_cfg->cfg_NFINODES-1; i++)
    {
        pfi->is_free = TRUE;
        pfi++;
        prtfs_cfg->mem_finode_freelist->pnext = pfi;
        prtfs_cfg->mem_finode_freelist++;
        prtfs_cfg->mem_finode_freelist->pnext = 0;
    }


#if (INCLUDE_RTFS_PROPLUS) /* ProPlus specific memory initialization */
    pc_memory_init_proplus();
#endif
    /* Mark all user files free */
    for (i = 0; i < prtfs_cfg->cfg_NUSERFILES; i++)
        prtfs_cfg->mem_file_pool[i].is_free = TRUE;

    prtfs_cfg->mem_finode_freelist = prtfs_cfg->mem_finode_pool;
    return(TRUE);
}

static BOOLEAN pc_memory_init_drive_structures(void)
{
DDRIVE *pdrive;
RTFS_DEVI_MEDIA_PARMS *pmediaparms;
int i;
byte muxName[16];

    rtfs_cs_strcpy(muxName, (byte *) "Rtfs_access_A", CS_CHARSET_NOT_UNICODE);

    for (pdrive=prtfs_cfg->mem_drive_pool,i = 0;
        i < prtfs_cfg->cfg_NDRIVES; i++, pdrive++)
    {
        rtfs_memset(pdrive, (byte) 0, sizeof(*pdrive));
    }
    /* Make a NULL terminated DDRIVE freelist using
        pnext_free as the link. This linked freelist is used by the DDRIVE
        memory allocator routine */
    prtfs_cfg->mem_drive_freelist = prtfs_cfg->mem_drive_pool;
    pdrive = (DDRIVE *)prtfs_cfg->mem_drive_freelist;
    for (i = 0; i < prtfs_cfg->cfg_NDRIVES-1; i++)
    {
        pdrive++;
        prtfs_cfg->mem_drive_freelist->pnext_free = pdrive;
        prtfs_cfg->mem_drive_freelist++;
        prtfs_cfg->mem_drive_freelist->pnext_free = 0;
    }
    prtfs_cfg->mem_drive_freelist = prtfs_cfg->mem_drive_pool;
    for (pmediaparms=prtfs_cfg->mem_mediaparms_pool,i = 0;
        i < prtfs_cfg->cfg_NDRIVES; i++, pmediaparms++)
    {
        rtfs_memset(pmediaparms, (byte) 0, sizeof(*pmediaparms));
        pmediaparms->access_semaphore = pc_rtfs_alloc_mutex((char*)muxName);
	    muxName[12] += 1;
		/* Allocate one access semaphore per media type */
		if (!pmediaparms->access_semaphore)
			return(FALSE);

    }
    return(TRUE);
}

/**************************************************************************
    PC_MEMORY_DDRIVE -  Allocate a DDRIVE structure
 Description
    If called with a null pointer, allocates and zeroes the space needed to
    store a DDRIVE structure. If called with a NON-NULL pointer the DDRIVE
    structure is returned to the heap.

 Returns
    If an ALLOC returns a valid pointer or NULL if no more core. If a free
    the return value is the input.

*****************************************************************************/

DDRIVE *pc_memory_ddrive(DDRIVE *pdrive)                             /*__fn__*/
{
DDRIVE *preturn;
    preturn = 0;
    if (pdrive)
    {
        OS_CLAIM_FSCRITICAL()
        if (!pdrive->pnext_free) /* Defensive, should always be zero */
        {
           /* Free it by putting it at the head of the freelist
                NOTE: pdrive is used to link the freelist */
            pdrive->pnext_free = (DDRIVE *) prtfs_cfg->mem_drive_freelist;
            prtfs_cfg->mem_drive_freelist = pdrive;
        }
        OS_RELEASE_FSCRITICAL()
    }
    else
    {
        /* Alloc: return the first structure from the freelist */
        OS_CLAIM_FSCRITICAL()
        preturn =  prtfs_cfg->mem_drive_freelist;
        if (preturn)
        {
            prtfs_cfg->mem_drive_freelist = (DDRIVE *) preturn->pnext_free;
            preturn->pnext_free = 0; /* Means it is in use */
            OS_RELEASE_FSCRITICAL()
        }
        else
        {
            OS_RELEASE_FSCRITICAL()
            rtfs_set_errno(PERESOURCEDRIVE, __FILE__, __LINE__); /* pc_memory_ddrive: out ddrive of resources */
        }
    }
    return(preturn);
}

/**************************************************************************
    PC_MEMORY_DROBJ -  Allocate a DROBJ structure
 Description
    If called with a null pointer, allocates and zeroes the space needed to
    store a DROBJ structure. If called with a NON-NULL pointer the DROBJ
    structure is returned to the heap.

 Returns
    If an ALLOC returns a valid pointer or NULL if no more core. If a free
    the return value is the input.

*****************************************************************************/

DROBJ *pc_memory_drobj(DROBJ *pobj)                             /*__fn__*/
{
DROBJ *preturn;
    preturn = 0;
    if (pobj)
    {
        OS_CLAIM_FSCRITICAL()
        if (!pobj->is_free)
        {
            pobj->is_free = TRUE;
           /* Free it by putting it at the head of the freelist
                NOTE: pdrive is used to link the freelist */
            pobj->pdrive = (DDRIVE *) prtfs_cfg->mem_drobj_freelist;
            prtfs_cfg->mem_drobj_freelist = pobj;
        }
        OS_RELEASE_FSCRITICAL()
    }
    else
    {
        /* Alloc: return the first structure from the freelist */
        OS_CLAIM_FSCRITICAL()
        preturn =  prtfs_cfg->mem_drobj_freelist;
        if (preturn)
        {
            prtfs_cfg->mem_drobj_freelist = (DROBJ *) preturn->pdrive;
            rtfs_memset(preturn, (byte) 0, sizeof(DROBJ));
            OS_RELEASE_FSCRITICAL()
        }
        else
        {
            OS_RELEASE_FSCRITICAL()
            rtfs_set_errno(PERESOURCEDROBJ, __FILE__, __LINE__);
        }
    }
    return(preturn);
}



/**************************************************************************
    PC_MEMORY_FINODE -  Allocate a FINODE structure
 Description
    If called with a null pointer, allocates and zeroes the space needed to
    store a FINODE structure. If called with a NON-NULL pointer the FINODE
    structure is returned to the heap.

 Returns
    If an ALLOC returns a valid pointer or NULL if no more core. If a free
    the return value is the input.

*****************************************************************************/

FINODE *pc_memory_finode(FINODE *pinode)                            /*__fn__*/
{
FINODE *preturn;
BLKBUFF *pfile_buffer;

    if (pinode)
    {
        pfile_buffer = 0;
        OS_CLAIM_FSCRITICAL()
        if (!pinode->is_free)
        {
            /* Free it by putting it at the head of the freelist */
            pfile_buffer = pinode->pfile_buffer;
            pinode->pfile_buffer = 0;
            pinode->is_free = TRUE;
            pinode->pnext = prtfs_cfg->mem_finode_freelist;
            prtfs_cfg->mem_finode_freelist = pinode;
        }
        OS_RELEASE_FSCRITICAL()
        if (pfile_buffer)
            pc_release_file_buffer(pfile_buffer);
        preturn = pinode;
    }
    else
    {
        /* Alloc: return the first structure from the freelist */
        OS_CLAIM_FSCRITICAL()
        preturn =  prtfs_cfg->mem_finode_freelist;
        if (preturn)
        {
            prtfs_cfg->mem_finode_freelist = preturn->pnext;
            /* Zero the structure */
            rtfs_memset(preturn, (byte) 0, sizeof(FINODE));
            OS_RELEASE_FSCRITICAL()
        }
        else
        {
            OS_RELEASE_FSCRITICAL()
            rtfs_set_errno(PERESOURCEFINODE, __FILE__, __LINE__);
        }
    }
    return(preturn);
}

void rtfs_kern_gets(byte *buffer)
{
	rtfs_sys_callback(RTFS_CBS_GETS, (void *) buffer); /* call the application layer to retrieve input from the console */
}

void rtfs_kern_puts(byte *buffer)
{
	rtfs_sys_callback(RTFS_CBS_PUTS, (void *) buffer); /* call the application layer to send output to console */
}

DATESTR *pc_getsysdate(DATESTR * pd)
{
	rtfs_sys_callback(RTFS_CBS_GETDATE, (void *) pd); /* call the application layer to get the current date and time */
	return(pd);
}
#if (INCLUDE_EXFATORFAT64)
void pcexfat_getsysdate( DATESTR *pcrdate, byte *putcoffset, byte *pmsincrement)
{
	/* Set the time zone and ten milisecond clock, the callback laer can change these */
	*putcoffset   = 0xf0; /* Eastern time zone US */
	*pmsincrement = 0;
	rtfs_sys_callback(RTFS_CBS_GETDATE, (void *) pcrdate); /* call the application layer to get the current date and time */
	rtfs_sys_callback(RTFS_CBS_UTCOFFSET, (void *) putcoffset); /* call the application layer to get the current date and time */
	rtfs_sys_callback(RTFS_CBS_10MSINCREMENT, (void *) pmsincrement); /* call the application layer to get the current date and time */
}
#endif

/* Shuts the compiler up when we call assert(0) */
int rtfs_debug_zero(void)
{
    return(0);
}

/* If debug code is not included ERTFS_ASSERT(X) is defined as
   ERTFS_ASSERT(X) if (!(X)) {rtfs_assert_break();}
   So when a test fails it enters and endless loop and displays
   assert */
#if (INCLUDE_DEBUG_TRUE_ASSERT == 0)
void rtfs_assert_break(void)
{
	rtfs_diag_callback(RTFS_CBD_ASSERT,0);
}
void rtfs_assert_test(void)
{
	rtfs_diag_callback(RTFS_CBD_ASSERT_TEST,0);
}
#endif
