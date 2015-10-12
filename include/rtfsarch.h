/*****************************************************************************
*Filename: RTFSARCH.H - RTFS CPU and Host configuration
*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc, 2006
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*
* Description:
*   This file contains CPU and Host configuration constants and types
*   It is included by rtfsconf.h
*
****************************************************************************/

#ifndef __RTFSARCH__
#define __RTFSARCH__ 1

/* Define RTFS_LINUX or RTFS_WINDOWS if we are running in an emulation environment
   select between linux and windows
*/
#ifdef __GNUC__
#define RTFS_LINUX
#endif
#ifdef _MSC_VER
#define RTFS_WINDOWS
#define _CRT_SECURE_NO_WARNINGS 1 /* Disabe warnings about using insecure string functions */
#endif
/*  If TMS320C6X we are not running in emulation mode */

/* CPU Configuration section */
#define KS_LITTLE_ENDIAN 1          /* See reference guide for explanation */
#define KS_LITTLE_ODD_PTR_OK 0      /* See reference guide for explanation */

#define INCLUDE_NATIVE_64_TYPE  1    /* ProPlus only, ignore for Pro
                                       See reference guide for explanation */

/* Asserts for unexpected conditions are compiled into Rtfs using the macros
   ERTFS_ASSERT(X) and ERTFS_ASSERT_TEST(X) see See rtfsarch.h.
   if INCLUDE_DEBUG_TRUE_ASSERT is enabled then these asserts use the compiler's
   assert((X)); call otherwise they result in callbacks rtfs_diag_callback() with
   arguments RTFS_CBD_ASSERT and RTFS_CBD_ASSERT_TEST respectively. */
#define INCLUDE_DEBUG_TRUE_ASSERT     1

/********************************************************************
 TYPES
********************************************************************/
#if (!defined(TRUE))
#define TRUE  1                 /* Don't change */
#endif
#if (!defined(FALSE))
#define FALSE 0                 /* Don't change */
#endif
typedef unsigned char byte;     /* Don't change */
typedef unsigned short word;    /* Don't change */
#ifdef _TMS320C6X
/*  If TMS320C6X we are not running in emulation mode */
typedef unsigned int dword;    /* Don't change */
#define INCLUDE_THREAD_SETENV_SUPPORT   1 /* Use rtfs_port_set_task_env() and rtfs_port_get_task_env() for binding thread to user structure */
#define INCLUDE_THREAD_EXIT_CALLBACK    0 /* Thread exits are supported but configured in project files */
#undef INCLUDE_NATIVE_64_TYPE
#define INCLUDE_NATIVE_64_TYPE  0
#undef  KS_LITTLE_ODD_PTR_OK
#define KS_LITTLE_ODD_PTR_OK 0
#define RTFS_CACHE_LINE_SIZE_IN_BYTES 128u
#define RTFS_CACHE_ALIGN_POINTER(POINTER)(void*)(((dword)((POINTER) + ((RTFS_CACHE_LINE_SIZE_IN_BYTES) - (1U)))) & (~((RTFS_CACHE_LINE_SIZE_IN_BYTES) - (1U))))
#else
#define INCLUDE_THREAD_SETENV_SUPPORT   0  /* Use task id for binding thread to user structure */
#define INCLUDE_THREAD_EXIT_CALLBACK    0 /*  Set to one if you can support thread exit handling */
typedef unsigned long dword;    /* Don't change */
#define RTFS_CACHE_LINE_SIZE_IN_BYTES 0
#define RTFS_CACHE_ALIGN_POINTER(POINTER) (void*)(POINTER)
#endif
#define BOOLEAN int             /* Don't change */
#define KS_CONSTANT const



#if (INCLUDE_MATH64)          /* ddword type, ProPlus only */
#if (INCLUDE_NATIVE_64_TYPE)
#define ddword unsigned long long
#else
typedef struct _ddword
{
    dword hi;
    dword lo;
} ddword;       /* ddword data type works with the M64XXXX macro package */
#endif
#endif


#if (INCLUDE_DEBUG_TRUE_ASSERT)
#include <assert.h>
#define ERTFS_ASSERT_TEST(X) assert((X));
#define ERTFS_ASSERT(X) assert((X));
#else
#define ERTFS_ASSERT(X) if (!(X)) {rtfs_assert_break();}
#define ERTFS_ASSERT_TEST(X) if (!(X)) {rtfs_assert_test();}
#endif


#endif /* __RTFSARCH__ */
/*
 *  @(#) ti.rtfs.config; 1, 0, 0, 0,17; 1-20-2009 17:04:20; /db/vtree/library/trees/rtfs/rtfs-a18x/src/
 */
