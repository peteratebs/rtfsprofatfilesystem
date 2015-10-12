/*****************************************************************************
*Filename: RTFS.H - Defines & structures for RTFS ms-dos utilities
*
*
* EBS - RTFS (Real Time File Manager)
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

#ifndef __RTFS__
#define __RTFS__ 1


#include <stdio.h> // Remove if not experimenting

#include "rtfsversion.h"   /* defines RTFS_MAJOR_VERSION, RTFS_MINOR_VERSION and RTFS_MINOR_REVISION */
#include "rtfsconf.h"   /* Include compile time RTFS configuration, architecture and basic types */
#include "rtfsblkmedia.h"
#include "rtfstypes.h"  /* Include basic rtfs declarations with ProPlus elements included if needed */
#if (INCLUDE_EXFAT)		/* Include rtexfattypes.h */
#include "rtexfattypes.h"
#endif
#include "rtfserr.h"    /* Include errno values */
#include "rtfsprotos.h" /* Include basic rtfs prototypes */
#if (INCLUDE_EXFAT)				/* Include rtexfatprotos.h */
#include "rtexfatprotos.h"
#endif
#if (INCLUDE_FAILSAFE_CODE)
#include "rtfsfailsafe.h"       /* Include rtfs proplus failsafe declarations */
#endif
#if (INCLUDE_FLASH_MANAGER)
#include "flashmgrcfg.h"
#endif
#endif      /* __RTFS__ */
