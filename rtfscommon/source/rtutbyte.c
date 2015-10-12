/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* rtutbyte.c - Character set independent string manipulation routines */

#include "rtfs.h"

/* Return TRUE if end of dir.. either if fname[0] is 0 or all entries are
   0xff (the case for uninitialized flash blocks)
*/

BOOLEAN pc_check_dir_end(byte *fname)
{
int i;
    if (*fname == 0)
        return(TRUE);
    for (i = 0; i < 8; i++)
    {
        if (*fname++ != 0xff)
            return(FALSE);
    }
    return(TRUE);
}

/* Byte oriented */
byte *pc_strchr(byte *string, byte ch)
{
    for(;string && *string!=0; string++)
    {
        if(ch == *string) return(string);
    }
    return(0);
}
BOOLEAN _illegal_alias_char(byte ch)        /*__fn__*/
{
    if (pc_strchr((byte *)pustring_sys_badalias, ch))
        return(TRUE);
    else
        return(FALSE);
}


/* Byte oriented string functions character set independent */
/****************************************************************************
COPYBUF  - Copy one buffer to another
Description
Essentially strncpy. Copy size BYTES from from to to.
Returns
Nothing
****************************************************************************/
void copybuff(void *vto, void *vfrom, int size)                                 /* __fn__*/
{
    byte *to = (byte *) vto;
    byte *from = (byte *) vfrom;
    while (size--)
        *to++ = *from++;
}

/******************************************************************************
PC_CPPAD  - Copy one buffer to another and right fill with spaces
Description
Copy up to size characters from from to to. If less than size
characters are transferred before reaching \0 fill to with SPACE
characters until its length reaches size.

  Note: to is NOT ! Null terminated.

    ASCII character function only. Unicode not required

      Returns
      Nothing

*****************************************************************************/
/* Byte oriented */
void pc_cppad(byte *to, byte *from, int size)                                       /* __fn__*/
{
    rtfs_memset(to, ' ', size);
    while (size--)
        if (*from)
            *to++ = *from++;
}

/* ******************************************************************** */

/* Byte oriented */
void rtfs_memset(void *pv, byte b, int n)                      /*__fn__*/
{ byte *p; p = (byte *) pv; while(n--) {*p++=b;} }

/* Byte oriented */
BOOLEAN rtfs_bytecomp(byte *p1, byte *p2, int n)
{
int i;
    for (i = 0 ; i < n; i++)
        if (*p1++ != *p2++)
            return(FALSE);
    return(TRUE);
}
