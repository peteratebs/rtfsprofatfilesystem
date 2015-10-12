/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 2002
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* UNICODE.C - Contains UNCICODE string manipulation and character conversion routines */
#include "rtfs.h"

#if (INCLUDE_CS_UNICODE)
#if (KS_LITTLE_ENDIAN)
#define BYTE_LOW  1 /* Numeric low byte of a character */
#define BYTE_HIGH 0 /* Numeric high byte of a character */
#else
#define BYTE_LOW  0 /* Numeric low byte of a character */
#define BYTE_HIGH 1 /* Numeric high byte of a character */
#endif
static void pc_unicode_byte2upper(byte *to, byte *from);
int unicode_compare(byte *p1, byte *p2);
byte *unicode_goto_eos(byte *p);


int unicode_cs_strcmp(byte * s1, byte * s2)
{
    word w1, w2;

    while (CS_OP_IS_NOT_EOS(s1, CS_CHARSET_UNICODE) && CS_OP_IS_NOT_EOS(s2,CS_CHARSET_UNICODE) && unicode_compare(s1, s2))
    {
        s1 += 2;
        s2 += 2;
    }

    w1 = (word)*(s1+BYTE_HIGH); w1 = (word) w1 << 8; w1 = (word)w1 + (word) *(s1+BYTE_LOW);
    w2 = (word)*(s2+BYTE_HIGH); w2 = (word) w2 << 8; w2 = (word)w2 + (word) *(s2+BYTE_LOW);

    if (w1 == w2)            return 0;
    else if (w1 < w2)        return -1;
    else                     return 1;
}

byte * unicode_cs_strcat(byte * targ, byte * src)    /*__fn__*/
{
byte *p;
    p = unicode_goto_eos(targ);
    rtfs_cs_strcpy(p, src, CS_CHARSET_UNICODE);
    return targ;
}

int unicode_cs_strcpy(byte * targ, byte * src)
{
int loop_count = 0;
    while (CS_OP_IS_NOT_EOS(src,CS_CHARSET_UNICODE))
    {
        *targ++ = *src++;
        *targ++ = *src++;
        loop_count++;
    }
    *targ++ = 0;
    *targ = 0;
    return (loop_count);
}

/* return number of unicode chars in a string */
int unicode_cs_strlen(byte * string)   /*__fn__*/
{
int len;
    len = 0;
    while (CS_OP_IS_NOT_EOS(string, CS_CHARSET_UNICODE))
    {
        len += 1;
        string += 2;
    }
    return (len);
}

/* Macros here */
byte *unicode_goto_eos(byte *p)
{
    while ((*p) || *(p+1)) p+=2;
    return(p);
}

int unicode_ascii_index(byte *p, byte base)
{
byte c[2];
int index;

    pc_unicode_byte2upper(c, p);
    if (c[BYTE_LOW]==0)
    {
        index = (int) (c[BYTE_HIGH] - base);
    }
    else
    {
        index = c[BYTE_HIGH];
        index <<= 8;
        index += c[BYTE_LOW];
    }
    return(index);
}

int unicode_compare_nc(byte *p1, byte *p2)
{
byte cp1[2];
byte cp2[2];
    pc_unicode_byte2upper(cp1,p1);
    pc_unicode_byte2upper(cp2,p2);
    if (unicode_compare(cp1, cp2))
        return(1);
    else
        return(0);
}


int unicode_compare(byte *p1, byte *p2)
{
    if (*p1 == *p2 && *(p1+1) == *(p2+1))
        return(1);
    else
        return(0);
}

int unicode_cmp_to_ascii_char(byte *p, byte c)
{
    if (*(p+BYTE_LOW) == 0)
        return ( c ==*(p+BYTE_HIGH) );
    return(0);
}

void unicode_assign_ascii_char(byte *p, byte c)
{
    *(p+BYTE_LOW) = 0;
    *(p+BYTE_HIGH) = c;
}
#if (INCLUDE_CS_JIS)
void jis_to_unicode(byte *to, byte *p);
#endif

void map_jis_ascii_to_unicode(byte *unicode_to, byte *ascii_from)
{
byte *p;

    p = unicode_to;
    while (*ascii_from)
    {
#if (INCLUDE_CS_JIS)
        /* Bug fig 12-16-2013 - Was not properly converting for JIS. Core problem for exFAT,
           for FAT32, is just a problem with shell commands. */
        jis_to_unicode(p, ascii_from);
        CS_OP_INC_PTR(ascii_from,CS_CHARSET_NOT_UNICODE);
#else
        *(p+BYTE_LOW) = 0;
        *(p+BYTE_HIGH) = *ascii_from++;
#endif
        p+= 2;
    }
    *p++ = 0;
    *p++ = 0;
}

#if (INCLUDE_CS_JIS)
int unicode_to_jis(byte *pjis, byte *punicode, BOOLEAN *is_default);
#endif

/* Convert Unicode to ascii - return TRUE if contains non mappable characters .. */
BOOLEAN map_unicode_to_jis_ascii(byte *to, byte *from)
{
BOOLEAN contains_unmapped_characters = FALSE;   /* If unicode to mapping doesn't produce a unique short char */
    while (*from || *(from+1))
    {
#if (INCLUDE_CS_JIS)
    int nbytes;
        nbytes = unicode_to_jis(to, from, &contains_unmapped_characters);
        to += nbytes;
#else
        if (*(from+BYTE_LOW) == 0)
            *to++ = *(from+BYTE_HIGH);
        else
        {
            contains_unmapped_characters = TRUE;
            *to++ = '_';
        }
#endif
        from += 2;
    }
    *to = 0;
    return(contains_unmapped_characters);
}


void lfn_chr_to_unicode(byte *to, byte *fr)
{
#if (KS_LITTLE_ENDIAN)
    *to = *fr++;
    *(to+1)   = *fr;
#else /* make sure UNICODE str is in Intel byte order on disk */
    *(to+1)   = *fr++;
    *to = *fr;
#endif
}
void unicode_chr_to_lfn(byte *to, byte *fr)
{
#if (KS_LITTLE_ENDIAN)
    *to = *fr++;
    *(to+1)   = *fr;
#else /* make sure UNICODE str is in Intel byte order on disk */
    *(to+1)   = *fr++;
    *to = *fr;
#endif
}

static void pc_unicode_byte2upper(byte *to, byte *from)  /* __fn__*/
{
byte c;
    if (*(from+BYTE_LOW)==0)
    {
        c = *(from+BYTE_HIGH);
        if  ((c >= 'a') && (c <= 'z'))
            c = (byte) ('A' + c - 'a');
        *(to+BYTE_LOW)= 0;
        *(to+BYTE_HIGH)= c;
    }
    else
    {
        *to++ = *from++;
        *to = *from;
    }
}


/* Version of MFILE that converts path from byte orientation to
   native char set before returning. pc_mfile and pc_cs_mfile are
   the same for ascii */
byte *unicode_cs_mfile(byte *to, byte *filename, byte *ext, byte ntflags)
{
        byte *p;
        int i;
        byte *retval = to;
        int use_charset = CS_CHARSET_UNICODE;
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
					c=CS_APPLY_NT_CASEMASK(namecasemask,c);  /* Convert to lower case if NT flags in DOS inode specified. */
                    CS_OP_ASSIGN_ASCII(to, c, use_charset);
                    CS_OP_INC_PTR(to, use_charset);
                     i++;
                }
                if (i == 8)
                        break;
        }
        if (p != filename)
        {
                p = ext;
                if (*p && *p != ' ')
                {
                    CS_OP_ASSIGN_ASCII(to,'.', use_charset);
                    CS_OP_INC_PTR(to, use_charset);
                }
                i = 0;
                while(p && *p)
                {
                        if (*p == ' ')
                                break;
                        else
                        {
                            byte c=*p++;
                            c=CS_APPLY_NT_CASEMASK(extcasemask,c);  /* Convert to lower case if NT flags in DOS inode specified. */
                            CS_OP_ASSIGN_ASCII(to, c, use_charset);
                            CS_OP_INC_PTR(to, use_charset);
                            i++;
                        }
                        if (i == 3)
                                break;
                }
        }
        CS_OP_TERM_STRING(to, use_charset);
        return (retval);
}


#endif
