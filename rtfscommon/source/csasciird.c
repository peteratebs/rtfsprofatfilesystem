/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 2002
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
* UTILASCI.C - Contains ASCII string manipulation and character
*  conversion routines
*/

#include "rtfs.h"

#if (INCLUDE_CS_ASCII)
static void pc_ascii_byte2upper(byte *to, byte *from);
int ascii_cs_strcpy(byte * targ, byte * src);
int ascii_cs_strlen(byte * string);
byte *ascii_goto_eos(byte *p);


byte * ascii_cs_strcat(byte * targ, byte * src)   /*__fn__*/
{
byte *pappend;
    pappend = ascii_goto_eos(targ);
    ascii_cs_strcpy(pappend, src);
    return targ;
}


/* compares 2 strings; returns 0 if they match */
int ascii_cs_strcmp(byte * s1, byte * s2)
{
    int index=0;
    while (s1[index] && s2[index] && s1[index] == s2[index])
    {
        index++;
    }
    if (!s1[index] && !s2[index])        return 0;
    if (s1[index] < s2[index])           return -1;
    else                                 return 1;
}


int ascii_cs_strcpy(byte * targ, byte * src)
{
    int loop_cnt=0;
    do
    {
        targ[loop_cnt] = src[loop_cnt];
    } while(src[loop_cnt++]);
    return loop_cnt;
}

/* return number of ascii chars in a string */
int ascii_cs_strlen(byte * string)   /*__fn__*/
{
    int len=0;
    while (string[len] != 0) len++;
    return len;
}

/* Macros here */

byte *ascii_goto_eos(byte *p)
{
    while (*p) p++;
    return(p);
}

int ascii_ascii_index(byte *p, byte base)
{
byte c;
    pc_ascii_byte2upper(&c, p);
    return((int) (c - base));
}

int ascii_compare_nc(byte *p1, byte *p2)
{
byte c,d;
    if (*p1 == *p2)
        return(1);
    else
    {
        pc_ascii_byte2upper(&c, p1);
        pc_ascii_byte2upper(&d, p2);
        return(c == d);
    }
}

/* Version of MFILE that converts path from byte orientation to
   native char set before returning. pc_mfile and pc_cs_mfile are
   the same for ascii */
byte *ascii_cs_mfile(byte *to, byte *filename, byte *ext, byte ntflags)
{
        byte *p;
        int i;
        byte *retval = to;
        /* ntflags specify if basename and/0r ext should be stired in lower case. Bit 4 (0x10) means lowercase extension and bit 3 (0x8) lowercase basename
           if the lowercase bit is set or in 0x20 to ascii letter to force lower case */
        byte extcasemask,namecasemask;
        extcasemask = (ntflags & 0x10);
        namecasemask= (ntflags & 0x08);
        p = filename;
        i = 0;
        while(*p)
        {
                if (*p == ' ')
                        break;
                else
                {
                    byte c=*p++;
					c=CS_APPLY_NT_CASEMASK(namecasemask,c);
                    *to++ = c;
                    i++;
                }
                if (i == 8)
                        break;
        }
        if (p != filename)
        {
                p = ext;
                if (*p && *p != ' ')
                    *to++ = '.';
                i = 0;
                while(p && *p)
                {
                        if (*p == ' ')
                                break;
                        else
                        {
                            byte c=*p++;
							c=CS_APPLY_NT_CASEMASK(extcasemask,c);
                            *to++ = c;
                            i++;
                        }
                        if (i == 3)
                                break;
                }
        }
        *to = '\0';
        return (retval);
}



/***************************************************************************
PC_FILEPARSE -  Parse a file xxx.yyy into filename/pathname

  Description
  Take a file named XXX.YY and return SPACE padded NULL terminated
  filename [XXX       ] and fileext [YY ] components. If the name or ext are
  less than [8,3] characters the name/ext is space filled and null termed.
  If the name/ext is greater  than [8,3] the name/ext is truncated. '.'
  is used to seperate file from ext, the special cases of . and .. are
  also handled.
  Returns
  Returns TRUE

    ****************************************************************************/
    /* UNICODE - Called by pc_malias not by others if vfat - Okay as ascii */
    /* UNICODE - pc_enum usage is probably incorrect */
    /* Take a string xxx[.yy] and put it into filename and fileext */
    /* Note: add a check legal later */
BOOLEAN pc_ascii_fileparse(byte *filename, byte *fileext, byte *p)                  /* __fn__*/
{
    int i;

    /* Defaults */
    rtfs_memset(filename, ' ', 8);
    filename[8] = '\0';
    rtfs_memset(fileext, ' ', 3);
    fileext[3] = '\0';

    /* Special cases of . and .. */
    if (*p == '.')
    {
        *filename = '.';
        if (*(p+1) == '.')
        {
            *(++filename) = '.';
            return (TRUE);
        }
        else if (*(p + 1) == '\0')
            return (TRUE);
        else
            return (FALSE);
    }

    i = 0;
    while (*p)
    {
        if (*p == '.')
        {
            p++;
            break;
        }
        else
            if (i++ < 8)
                *filename++ = *p;
            p++;
    }

    i = 0;
    while (*p)
    {
        if (i++ < 3)
            *fileext++ = *p;
        p++;
    }
    return (TRUE);
}

#if (!INCLUDE_VFAT) /* Small piece of compile time INCLUDE_VFAT vs's NONVFAT code  */

BOOLEAN pc_ascii_patcmp_8(byte *p, byte *pattern, BOOLEAN dowildcard)  /* __fn__*/
{
int size = 8;
    /* Kludge. never match a deleted file */
    if (*p == PCDELETE)
       return (FALSE);
    else if (*pattern == PCDELETE)  /* But E5 in the Pattern matches 0x5 */
    {
        if (*p == 0x5)
        {
            size -= 1;
            p++;
            pattern++;
        }
        else
           return (FALSE);
   }

    while (size--)
    {
        if(dowildcard)
        {
            if (*pattern == '*')    /* '*' matches the rest of the name */
                return (TRUE);
            if (*pattern != '?' && !ascii_compare_nc(pattern,p))
                  return (FALSE);
        }
        else
        {
            if (!ascii_compare_nc(pattern,p))
                 return (FALSE);
        }
        p++;
        pattern++;
    }
    return (TRUE);
}

BOOLEAN pc_ascii_patcmp_3(byte *p, byte *pattern, BOOLEAN dowildcard)  /* __fn__*/
{
int size = 3;

    while (size--)
    {
        if(dowildcard)
        {
            if (*pattern == '*')    /* '*' matches the rest of the name */
                return (TRUE);
            if (*pattern != '?' && !ascii_compare_nc(pattern,p))
                 return (FALSE);
        }
        else
        {
            if (!ascii_compare_nc(pattern,p))
                 return (FALSE);
        }
        p++;
        pattern++;
    }
    return (TRUE);
}
#endif /* INCLUDE_VFAT */

static void pc_ascii_byte2upper(byte *to, byte *from)  /* __fn__*/
{
byte c;
    c = *from;
    if  ((c >= 'a') && (c <= 'z'))
        c = (byte) ('A' + c - 'a');
    *to = c;
}

void pc_ascii_str2upper(byte *to, byte *from)
{
        while(*from)
            pc_ascii_byte2upper(to++, from++);
        *to = '\0';
}

void pc_ascii_strn2upper(byte *to, byte *from, int n)
{
    int i;
    for (i = 0; i < n; i++,to++, from++)
        pc_ascii_byte2upper(to, from);
}


#endif
