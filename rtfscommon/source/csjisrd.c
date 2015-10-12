/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 2002
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* JIS.C - Contains Japanese string manipulation and character conversion routines */
/*         See also jistab.c */


#include "rtfs.h"

#if (INCLUDE_CS_JIS)
static byte * rtfs_jis_strcat(byte * targ, byte * src);
static int rtfs_jis_strcpy(byte * targ, byte * src);
static void pc_jis_byte2upper(byte *to, byte *from);
int jis_compare(byte *p1, byte *p2);
byte *jis_increment(byte *p);
int jis_char_length(byte *p);


/* compares 2 strings; returns 0 if they match */
int jis_cs_strcmp(byte * s1, byte * s2)        /*__fn__*/
{
    while (*s1 && *s2)
    {
        if (!jis_compare(s1, s2))
            return(1);
        s1 = jis_increment(s1);
        s2 = jis_increment(s2);
    }
    if (!*s1 && !*s2)
        return(0);
    else
        return(1);
}

byte * jis_cs_strcat(byte * targ, byte * src)    /*__fn__*/
{
    /* Call the byte oriented function */
    return(rtfs_jis_strcat(targ, src));
}

/* This works because JIS has no ZERO in hi byte of 16 bit jis chars */
int jis_cs_strcpy(byte * targ, byte * src)
{
    /* Call the byte oriented function */
    return (rtfs_jis_strcpy(targ, src));
}


/* return number of jis chars in a string */
int jis_cs_strlen(byte * string)   /*__fn__*/
{
int len=0;
   while (*string)
   {
    string = jis_increment(string);
    len++;
   }
   return len;
}

/* Macros here */
byte *jis_goto_eos(byte *p)
{
    while (*p)
        p = jis_increment(p);
    return(p);
}

int jis_ascii_index(byte *p, byte base)
{
byte c;
    pc_jis_byte2upper(&c, p);
    return((int) (c - base));
}

int jis_compare_nc(byte *p1, byte *p2)
{
byte c,d;
    if (jis_compare(p1, p2))
        return(1);
    else if (jis_char_length(p1)==1 && jis_char_length(p2)==1)
    {
        pc_jis_byte2upper(&c, p1);
        pc_jis_byte2upper(&d, p2);
        return(c == d);
    }
    else
        return(0);
}

void pc_jis_strn2upper(byte *to, byte *from, int n)  /* __fn__*/
{
    int i;
    byte c;
    for (i = 0; i < n; i++)
    {
            if (jis_char_length(from) == 2)
            {
                *to++ = *from++;
                *to++ = *from++;
            }
            else
            {
                c = *from++;
                if  ((c >= 'a') && (c <= 'z'))
                        c = (byte) ('A' + c - 'a');
                *to++ = c;
            }
    }
}

/* Return the length of a JIS character, 1 or 2 */
int jis_char_length(byte *p)
{
    if ((*p >= 0x81 && *p <= 0x9f) || (*p >= 0xe0 && *p <= 0xfc))
        return(2);
    else
        return(1);
}

/* Copy JIS character, 1 or 2 bytes */
int jis_char_copy(byte *to, byte *from)
{
int len;
    len = jis_char_length(from);
    *to++ = *from++;
    if (len == 2)
        *to++ = *from++;
    return(len);
}

/* Advance a pointer to the next JIS character in a string */
byte *jis_increment(byte *p)
{
    return(p + jis_char_length(p));
}

/* return number of jis chars in a string */
/* Return 1 if p1 and p2 are the same */
int jis_compare(byte *p1, byte *p2)
{
    /* If 1st char same and (len is 1 or 2nd char the same */
    return ( (*p1 == *p2) && (jis_char_length(p1)==1 || (*(p1+1) == *(p2+1))) );
}



void pc_jis_str2upper(byte *to, byte *from)  /* __fn__*/
{
        byte c;
        while(*from)
        {
            if (jis_char_length(from) == 2)
            {
                *to++ = *from++;
                *to++ = *from++;
            }
            else
            {
                c = *from++;
                if  ((c >= 'a') && (c <= 'z'))
                        c = (byte) ('A' + c - 'a');
                *to++ = c;
            }
        }
        *to = '\0';
}



/***************************************************************************
        PC_MFILE  - Build a file spec (xxx.yyy) from a file name and extension

 Description
        Fill in to with a concatenation of file and ext. File and ext are
        not assumed to be null terminated but must be blank filled to [8,3]
        chars respectively. 'to' will be a null terminated string file.ext.

        ASCII character function only. Unicode not required

 Returns
        A pointer to 'to'.

****************************************************************************/
byte *jis_cs_mfile(byte *to, byte *filename, byte *ext, byte ntflags)
{
        byte *p;
        int i,l;
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
                l = jis_char_length(p);
                if (*p == ' ')
                        break;
                else
                {
                    byte c;
                    if (l == 2)
                    {
                        if (i>6)
                            break;
                        *to++ = *p++;
                        c=*p++;
                    }
                    else
                    {
                        c=*p++;
                        c=CS_APPLY_NT_CASEMASK(namecasemask,c); /* Convert to lower case if NT flags in DOS inode specified. */
                    }
                    *to++ = c;
                    i += l;
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
                    l = jis_char_length(p);
                    if (*p == ' ')
                        break;
                    else
                    {
                        byte c;
                        if (l == 2)
                        {
                            if (i>1)
                                break;
                            *to++ = *p++;
                            c=*p++;
                        }
                        else
                        {
                            c=*p++;
                            c=CS_APPLY_NT_CASEMASK(extcasemask,c); /* Convert to lower case if NT flags in DOS inode specified. */
                        }
                        *to++ = c;
                        i += l;
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
    /* Take a string xxx[.yy] and put it into filename and fileext */
    /* Note: add a check legal later */
BOOLEAN pc_jis_fileparse(byte *filename, byte *fileext, byte *p)                  /* __fn__*/
{
    int i;
    int l;

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
        l = jis_char_length(p);
        if (*p == '.')
        {
            p++;
            break;
        }
        else
        {
            if (i + l <= 8)
            {
                jis_char_copy(filename, p);
                filename += l;
                i += l;
            }
            p += l;
        }
    }

    i = 0;
    while (*p)
    {
        l = jis_char_length(p);
        if (i + l <= 3)
        {
            jis_char_copy(fileext, p);
            fileext += l;
            i += l;
        }
        p += l;
    }
    return (TRUE);
}

static int rtfs_jis_strcpy(byte * targ, byte * src)   /*__fn__*/
{
    int loop_cnt=0;
    do
    {
        targ[loop_cnt] = src[loop_cnt];
    } while(src[loop_cnt++]);
    return loop_cnt;
}
/* This works because JIS has no ZERO in hi byte of 16 bit jis chars */
static byte * rtfs_jis_strcat(byte * targ, byte * src)   /*__fn__*/
{
byte *pappend;
    pappend = jis_goto_eos(targ);
    rtfs_jis_strcpy(pappend, src);
    return targ;
}
static void pc_jis_byte2upper(byte *to, byte *from)
{
byte c;
    c = *from;
    if  ((c >= 'a') && (c <= 'z'))
        c = (byte) ('A' + c - 'a');
    *to = c;
}

#if (!INCLUDE_VFAT) /* Small piece of compile time INCLUDE_VFAT vs's NONVFAT code  */

BOOLEAN pc_jis_patcmp_8(byte *p, byte *pattern, BOOLEAN dowildcard)  /* __fn__*/
{
int size = 8;
byte save_char;
byte *save_p;
BOOLEAN ret_val;

    /* never match a deleted file */
    if (*p == PCDELETE)
      return (FALSE);
    save_p = p;
    save_char = *p;
    if(save_char == 0x05)
        *p = 0xe5;          /* JIS KANJI char */

    ret_val = TRUE;
    while (size > 0)
    {
        if(dowildcard)
        {
            if (*pattern == '*')    /* '*' matches the rest of the name */
                goto ret;
            if (*pattern != '?' && !jis_compare_nc(pattern,p))
            {
                ret_val = FALSE;
                goto ret;
            }
        }
        else
        {
            if (!jis_compare_nc(pattern,p))
            {
                ret_val = FALSE;
                goto ret;
            }
        }
        size -= jis_char_length(p);
        p = jis_increment(p);
        pattern = jis_increment(pattern);
    }
ret:
    *save_p = save_char;
    return(ret_val);
}

BOOLEAN pc_jis_patcmp_3(byte *p, byte *pattern, BOOLEAN dowildcard)  /* __fn__*/
{
int size = 3;
BOOLEAN ret_val;

    ret_val = TRUE;
    while (size > 0)
    {
        if(dowildcard)
        {
            if (*pattern == '*')    /* '*' matches the rest of the name */
                goto ret;
            if (*pattern != '?' && !jis_compare_nc(pattern,p))
            {
                ret_val = FALSE;
                goto ret;
            }
        }
        else
        {
            if (!jis_compare_nc(pattern,p))
            {
                ret_val = FALSE;
                goto ret;
            }
        }
        size -= jis_char_length(p);
        p = jis_increment(p);
        pattern = jis_increment(pattern);
    }
ret:
    return(ret_val);
}

#endif

#endif
