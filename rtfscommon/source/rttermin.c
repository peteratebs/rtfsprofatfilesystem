/*
* rttermin.c - Portable portion of terminal IO routines
*
*   EBS - ERTFS
*
*   Copyright EBS Inc. 1987-2003
*   All rights reserved.
*   This code may not be redistributed in source or linkable object form
*   without the consent of its author.
*
*
*    Module description:
*        This file contains terminal IO routines used by the sample programs
*        and by routines that print diagnostics.
*
*
* The rest of the routines in this module are portable with the possible
* exception of these two routines:
*
* void rtfs_print_format_dir
* void rtfs_print_format_stat
*
* These routines may require porting if sprintf() is not available to you.
* They are called only by the test shell prgram (tstsh.c) and are used to create attractive
* formatted output for the DIR and STAT commands. They rely on sprintf to format the output
*  to provide a system specific console output routine. A define is provided
* in this file named SYS_SUPPORTS_SPRINTF, if this is set to one the routines format the output
* using sprintf, otherwise they print a fixd string. If sprintf is not available to you set
* SYS_SUPPORTS_SPRINTF to zero.
*
*
* The following portable routines are also provided in this file.
*
*   rtfs_print_string_1(int stringid,int flags)
*   rtfs_print_string_2(int stringid,byte *pstr2, int flags)
*
*   These two routines are used to print string values to the console. They are portable,
*   relying on the routine rtfs_kern_puts(() to provide a system specific console output routine.
*  If no output is desired define the macros RTFS_PRINT_STRING_1 and RTFS_PRINT_STRING_2 as
*  no-ops in portterm.h.
*
*   rtfs_print_long_1
*
*   This routine is used to print long integers values to the console. It relies on
*  rtfs_kern_puts(() to provide a system specific console output routine. If no output
*  is desired define the macro RTFS_PRINT_LONG_1 as a no-op in portterm.h.
*
*
*/

#include "rtfs.h"


void rtfs_print_one_string(byte *pstr,int flags);


void rtfs_print_string_1(byte *pstring ,int flags)
{
    rtfs_print_one_string(pstring,flags);
}

void rtfs_print_string_2(byte *pstring,byte *pstr2, int flags)
{
    rtfs_print_string_1(pstring,0);
    rtfs_print_one_string(pstr2,flags);
}

void rtfs_print_long_1(dword l,int flags)
{
byte buffer[16];
    rtfs_print_one_string(pc_ltoa(l, buffer,10),flags);
}



void rtfs_print_one_string(byte *pstr,int flags)
{

    rtfs_kern_puts(pstr);
    if (flags & PRFLG_NL)
        rtfs_kern_puts((byte *)"\n");
    if (flags & PRFLG_CR)
        rtfs_kern_puts((byte *)"\r");
}

/* Portable */
byte *pc_ltoa(dword num, byte *dest, int number_base)      /*__fn__*/
{
byte buffer[33]; /* MAXINT can have 32 digits max, base 2 */
int digit;
byte *olddest = dest;
byte * p;

    p = &(buffer[32]);

    *p = '\0';

    /* Convert num to a string going from dest[31] backwards */
    /* Nasty little ItoA algorithm */
    do
    {
        digit = (int) (num % number_base);

        *(--p) =
          (byte)(digit<10 ? (byte)(digit + '0') : (byte)((digit-10) + 'a'));
        num /= number_base;
    }
    while (num);

    /* Now put the converted string at the beginning of the buffer */
    while((*dest++=*p++)!='\0');
    return (olddest);
}
