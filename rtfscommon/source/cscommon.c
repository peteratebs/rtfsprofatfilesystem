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

#if (INCLUDE_CS_UNICODE)
/* csunicodrd.c */
int unicode_cs_strcmp(byte * s1, byte * s2);
byte * unicode_cs_strcat(byte * targ, byte * src);
int unicode_cs_strcpy(byte * targ, byte * src);
int unicode_cs_strlen(byte * string);
byte *unicode_goto_eos(byte *p);
int unicode_ascii_index(byte *p, byte base);
int unicode_compare_nc(byte *p1, byte *p2);
int unicode_compare(byte *p1, byte *p2);
int unicode_cmp_to_ascii_char(byte *p, byte c);
void unicode_assign_ascii_char(byte *p, byte c);
void lfn_chr_to_unicode(byte *to, byte *fr);
void unicode_chr_to_lfn(byte *to, byte *fr);
byte *unicode_cs_mfile(byte *to, byte *filename, byte *ext, byte ntflags);
/* csunicodwr.c */
BOOLEAN pc_unicode_validate_filename(byte * name, byte * ext);
BOOLEAN pc_unicode_malias(byte *alias, byte *input_file, int which_try);
#endif
#if (INCLUDE_CS_JIS)
/* csjisrd.c */
int jis_cs_strcmp(byte * s1, byte * s2);
byte * jis_cs_strcat(byte * targ, byte * src);
int jis_cs_strcpy(byte * targ, byte * src);
int jis_cs_strlen(byte * string);
byte *jis_goto_eos(byte *p);
int jis_ascii_index(byte *p, byte base);
int jis_compare_nc(byte *p1, byte *p2);
void pc_jis_strn2upper(byte *to, byte *from, int n);
int jis_char_length(byte *p);
int jis_char_copy(byte *to, byte *from);
byte *jis_increment(byte *p);
int jis_compare(byte *p1, byte *p2);
void pc_jis_str2upper(byte *to, byte *from);
byte *jis_cs_mfile(byte *to, byte *filename, byte *extint, byte ntflags);
BOOLEAN pc_jis_fileparse(byte *filename, byte *fileext, byte *p);
BOOLEAN pc_jis_patcmp_8(byte *p, byte *pattern, BOOLEAN dowildcard);
BOOLEAN pc_jis_patcmp_3(byte *p, byte *pattern, BOOLEAN dowildcard);
/* csjiswr.c */
BOOLEAN jis_valid_sfn(byte *filename, BOOLEAN case_sensitive);
BOOLEAN pc_jis_validate_filename(byte * filename, byte * ext);
BOOLEAN pc_jis_malias(byte *alias, byte *input_file, int which_try);
BOOLEAN pc_jis_validate_8_3_name(byte * name,int len);
/* csjistap.c */
void jis_to_unicode(byte *to, byte *p);
int unicode_to_jis(byte *pjis, byte *punicode, BOOLEAN *is_default);
#else
/* csasciird.c */
byte * ascii_cs_strcat(byte * targ, byte * src);
int ascii_cs_strcmp(byte * s1, byte * s2);
int ascii_cs_strcpy(byte * targ, byte * src);
int ascii_cs_strlen(byte * string);
byte *ascii_goto_eos(byte *p);
int ascii_ascii_index(byte *p, byte base);
int ascii_compare_nc(byte *p1, byte *p2);
byte *ascii_cs_mfile(byte *to, byte *filename, byte *ext, byte ntflags);
BOOLEAN pc_ascii_fileparse(byte *filename, byte *fileext, byte *p);
BOOLEAN pc_ascii_patcmp_8(byte *p, byte *pattern, BOOLEAN dowildcard);
BOOLEAN pc_ascii_patcmp_3(byte *p, byte *pattern, BOOLEAN dowildcard);
void pc_ascii_str2upper(byte *to, byte *from);
void pc_ascii_strn2upper(byte *to, byte *from, int n);
/* csasciiwr.c */
BOOLEAN ascii_valid_sfn(byte *filename,BOOLEAN case_sensitive);
BOOLEAN pc_ascii_validate_filename(byte * filename, byte * ext);
BOOLEAN pc_ascii_malias(byte *alias, byte *input_file, int which_try);
BOOLEAN pc_ascii_validate_8_3_name(byte * name,int len);
#endif

/* CS_OP_CP_CHR(TO,FR,CS) rtfs_cs_char_copy((TO),(FR), CS) */
void rtfs_cs_char_copy(byte *to, byte *from, int use_charset)
{
#if (INCLUDE_CS_UNICODE)
    if (use_charset == CS_CHARSET_UNICODE)
    {
        {*(to)=*(from);*((to)+1)=*((from)+1);}
        return;
    }
#else
    RTFS_ARGSUSED_INT(use_charset);
#endif
#if (INCLUDE_CS_JIS)
    jis_char_copy(to,from);
#else
    *to = *from;
#endif

}
/* #define CS_OP_INC_PTR(P,CS) rtfs_cs_increment((P), CS) */
byte *rtfs_cs_increment(byte *p, int use_charset)
{
#if (INCLUDE_CS_UNICODE)
    if (use_charset == CS_CHARSET_UNICODE)
        return(p+2);
#else
    RTFS_ARGSUSED_INT(use_charset);
#endif
#if (INCLUDE_CS_JIS)
    return(jis_increment(p));
#else
    return(p+1);
#endif
}
/* #define CS_OP_CMP_CHAR(P1, P2,CS) rtfs_cs_compare((P1), (P2), CS) */
int rtfs_cs_compare(byte *p1, byte *p2, int use_charset)
{
#if (INCLUDE_CS_UNICODE)
    if (use_charset == CS_CHARSET_UNICODE)
       return(unicode_compare(p1,p2));
#else
    RTFS_ARGSUSED_INT(use_charset);
#endif
#if (INCLUDE_CS_JIS)
    return(jis_compare(p1,p2));
#else
    return ((int)(*p1==*p2));
#endif
}

/* #define CS_OP_CMP_CHAR_NC(P1, P2,CS) ascii_compare_nc((P1), (P2)) */
int rtfs_cs_compare_nc(byte *p1, byte *p2, int use_charset)
{
#if (INCLUDE_CS_UNICODE)
    if (use_charset == CS_CHARSET_UNICODE)
       return(unicode_compare_nc(p1,p2));
#else
    RTFS_ARGSUSED_INT(use_charset);
#endif
#if (INCLUDE_CS_JIS)
    return(jis_compare_nc(p1,p2));
#else
    return(ascii_compare_nc(p1,p2));
#endif
}
/* #define CS_OP_ASCII_INDEX(P,C,CS) rtfs_cs_ascii_index(P,C, CS) */
int rtfs_cs_ascii_index(byte *p, byte base, int use_charset)
{
#if (INCLUDE_CS_UNICODE)
    if (use_charset == CS_CHARSET_UNICODE)
        return(unicode_ascii_index(p, base));
#else
    RTFS_ARGSUSED_INT(use_charset);
#endif
#if (INCLUDE_CS_JIS)
    return(jis_ascii_index(p, base));
#else
    return(ascii_ascii_index(p, base));
#endif

}
/* #define CS_OP_TO_LFN(TO, FROM,CS) rtfs_cs_to_unicode(TO, FROM, CS) */
void rtfs_cs_to_unicode(byte *to, byte *p, int use_charset)
{
#if (INCLUDE_CS_UNICODE)
    if (use_charset == CS_CHARSET_UNICODE)
    {
        unicode_chr_to_lfn(to, p);
        return;
    }
#else
    RTFS_ARGSUSED_INT(use_charset);
#endif
#if (INCLUDE_CS_JIS)
    jis_to_unicode(to, p);
#else
    {*to = *p; *(to+1) = 0;}
#endif
}

/* #define CS_OP_LFI_TO_TXT(TO, FROM,CS) rtfs_cs_unicode_to_cs(TO, FROM, CS) */
void rtfs_cs_unicode_to_cs(byte *to, byte *punicode, int use_charset)
{
#if (INCLUDE_CS_UNICODE)
    if (use_charset == CS_CHARSET_UNICODE)
    {
        lfn_chr_to_unicode(to, punicode);
        return;
    }
#else
    RTFS_ARGSUSED_INT(use_charset);
#endif
#if (INCLUDE_CS_JIS)
    {
        BOOLEAN btemp;
        unicode_to_jis(to, punicode, &btemp);
    }
#else
    *to = *punicode;
#endif
}
/* #define CS_OP_IS_EOS(P,CS) rtfs_cs_is_eos(P, CS) */
BOOLEAN rtfs_cs_is_eos(byte *p, int use_charset)
{
#if (INCLUDE_CS_UNICODE)
    if (use_charset == CS_CHARSET_UNICODE)
        return((BOOLEAN)(*p == 0 && *(p +1) == 0));
#else
    RTFS_ARGSUSED_INT(use_charset);
#endif
    return((BOOLEAN)(*p == 0));

}
/* #define CS_OP_IS_NOT_EOS(P,CS) rtfs_cs_is_not_eos(P, CS) */
BOOLEAN rtfs_cs_is_not_eos(byte *p, int use_charset)
{
    return(!rtfs_cs_is_eos(p,use_charset));
}

/* #define CS_OP_TERM_STRING(P,CS) rtfs_cs_term_string(P, CS) */
void rtfs_cs_term_string(byte *p, int use_charset)
{
    *p=0;
#if (INCLUDE_CS_UNICODE)
    if (use_charset == CS_CHARSET_UNICODE)
    {
        *(p +1)=0;
    }
#else
    RTFS_ARGSUSED_INT(use_charset);
#endif
}
/* #define CS_OP_CMP_ASCII(P,C,CS) rtfs_cs_cmp_to_ascii((P),C,CS) */
int rtfs_cs_cmp_to_ascii_char(byte *p, byte c, int use_charset)
{
#if (INCLUDE_CS_UNICODE)
    if (use_charset == CS_CHARSET_UNICODE)
        return(unicode_cmp_to_ascii_char(p, c));
#else
    RTFS_ARGSUSED_INT(use_charset);
#endif
    return((int)(*p == c));
}

/* #define CS_OP_ASSIGN_ASCII(P,C,CS) rtfs_cs_assign_ascii((P),C,CS) */
void rtfs_cs_assign_ascii_char(byte *p, byte c, int use_charset)
{
#if (INCLUDE_CS_UNICODE)
    if (use_charset == CS_CHARSET_UNICODE)
    {
        unicode_assign_ascii_char(p, c);
        return;
    }
#else
    RTFS_ARGSUSED_INT(use_charset);
#endif
    *p = c;
}
/* #define CS_OP_GOTO_EOS(P,CS) rtfs_cs_goto_eos((P),CS) */
byte *rtfs_cs_goto_eos(byte *p, int use_charset)
{
#if (INCLUDE_CS_UNICODE)
    if (use_charset == CS_CHARSET_UNICODE)
        return(unicode_goto_eos(p));
#else
    RTFS_ARGSUSED_INT(use_charset);
#endif
#if (INCLUDE_CS_JIS)
     return(jis_goto_eos(p));
#else
     return(ascii_goto_eos(p));
#endif
}

BOOLEAN rtfs_cs_ascii_fileparse(byte *filename, byte *fileext, byte *p)
{
#if (INCLUDE_CS_JIS)
    return(pc_jis_fileparse(filename, fileext, p));
#else
    return(pc_ascii_fileparse(filename, fileext, p));
#endif
}

byte *pc_cs_mfile(byte *to, byte *filename, byte *ext, int use_charset)
{
#if (INCLUDE_CS_UNICODE)
    if (use_charset == CS_CHARSET_UNICODE)
        return(unicode_cs_mfile(to, filename, ext,0));
#else
    RTFS_ARGSUSED_INT(use_charset);
#endif
#if (INCLUDE_CS_JIS)
    return(jis_cs_mfile(to, filename, ext,0));
#else
    return(ascii_cs_mfile(to, filename, ext,0));
#endif
}

/* Create a "x.y" string from fname and fext sections of 8.3 directory, being mindful of flags in the reserve field that
   specify forcing lower case for filename, extension or both */
byte *pc_cs_mfileNT(byte *to, byte *filename, byte *ext, int use_charset, byte ntflags)
{
#if (INCLUDE_CS_UNICODE)
    if (use_charset == CS_CHARSET_UNICODE)
        return(unicode_cs_mfile(to, filename, ext,ntflags));
#else
    RTFS_ARGSUSED_INT(use_charset);
#endif
#if (INCLUDE_CS_JIS)
    return(jis_cs_mfile(to, filename, ext,ntflags));
#else
    return(ascii_cs_mfile(to, filename, ext,ntflags));
#endif
}


byte *rtfs_cs_strcat(byte * targ, byte * src, int use_charset)
{
#if (INCLUDE_CS_UNICODE)
    if (use_charset == CS_CHARSET_UNICODE)
        return(unicode_cs_strcat(targ, src));
#else
    RTFS_ARGSUSED_INT(use_charset);
#endif
#if (INCLUDE_CS_JIS)
    return(jis_cs_strcat(targ, src));
#else
    return(ascii_cs_strcat(targ, src));
#endif
}
int rtfs_cs_strcmp(byte * s1, byte * s2, int use_charset)
{
#if (INCLUDE_CS_UNICODE)
    if (use_charset == CS_CHARSET_UNICODE)
        return(unicode_cs_strcmp(s1, s2));
#else
    RTFS_ARGSUSED_INT(use_charset);
#endif
#if (INCLUDE_CS_JIS)
    return(jis_cs_strcmp(s1, s2));
#else
    return(ascii_cs_strcmp(s1, s2));
#endif
}
int rtfs_cs_strcpy(byte * targ, byte * src, int use_charset)
{
#if (INCLUDE_CS_UNICODE)
    if (use_charset == CS_CHARSET_UNICODE)
        return(unicode_cs_strcpy(targ, src));
#else
    RTFS_ARGSUSED_INT(use_charset);
#endif
#if (INCLUDE_CS_JIS)
    return(jis_cs_strcpy(targ, src));
#else
    return(ascii_cs_strcpy(targ, src));
#endif
}
int rtfs_cs_strlen(byte * string, int use_charset)
{
#if (INCLUDE_CS_UNICODE)
    if (use_charset == CS_CHARSET_UNICODE)
        return(unicode_cs_strlen(string));
#else
    RTFS_ARGSUSED_INT(use_charset);
#endif
#if (INCLUDE_CS_JIS)
    return(jis_cs_strlen(string));
#else
    return(ascii_cs_strlen(string));
#endif
}

void pc_cs_ascii_str2upper(byte *to, byte *from)
{
#if (INCLUDE_CS_JIS)
    pc_jis_str2upper(to, from);
#else
    pc_ascii_str2upper(to, from);
#endif
}

void pc_cs_ascii_strn2upper(byte *to, byte *from, int n)
{
#if (INCLUDE_CS_JIS)
    pc_jis_strn2upper(to, from, n);
#else
    pc_ascii_strn2upper(to, from, n);
#endif
}

#if (INCLUDE_VFAT && !RTFS_CFG_READONLY) /* Excluded from build if read only */

BOOLEAN pc_cs_malias(byte *alias, byte *input_file, int which_try, int use_charset)
{
#if (INCLUDE_CS_UNICODE)
    if (use_charset == CS_CHARSET_UNICODE)
        return (pc_unicode_malias(alias, input_file, which_try));
#else
    RTFS_ARGSUSED_INT(use_charset);
#endif
#if (INCLUDE_CS_JIS)
    return (pc_jis_malias(alias, input_file, which_try));
#else
    return (pc_ascii_malias(alias, input_file, which_try));
#endif
}
#endif

#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */

BOOLEAN pc_cs_valid_sfn(byte *filename, BOOLEAN case_sensitive)
{
#if (INCLUDE_CS_JIS)
    return(jis_valid_sfn(filename, case_sensitive));
#else
    return(ascii_valid_sfn(filename, case_sensitive));
#endif
}
#endif


#if (INCLUDE_VFAT) /* Small piece of compile time INCLUDE_VFAT vs's NONVFAT code  */
#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */
BOOLEAN pc_cs_validate_filename(byte * name, byte * ext, int use_charset) /*__fn__*/
{
#if (INCLUDE_CS_UNICODE)
    if (use_charset == CS_CHARSET_UNICODE)
        return(pc_unicode_validate_filename(name, ext));
#else
    RTFS_ARGSUSED_INT(use_charset);
#endif
#if (INCLUDE_CS_JIS)
    return(pc_jis_validate_filename(name, ext));
#else
    return(pc_ascii_validate_filename(name, ext));
#endif
}
#endif
#endif

#if (!INCLUDE_VFAT) /* Small piece of compile time INCLUDE_VFAT vs's NONVFAT code  */

#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */
BOOLEAN pc_cs_validate_8_3_name(byte * name,int len) /*__fn__*/
{
#if (INCLUDE_CS_JIS)
    return(pc_jis_validate_8_3_name(name,len));
#else
    return(pc_ascii_validate_8_3_name(name,len));
#endif
}

BOOLEAN pc_cs_validate_filename(byte * name, byte * ext, int use_charset) /*__fn__*/
{
byte sfn_buffer[14]; /* Only uses 13 but keep declarations even */
   RTFS_ARGSUSED_INT(use_charset);
   if (!(pc_cs_validate_8_3_name(name,8) && pc_cs_validate_8_3_name(ext,3)) )
       return(FALSE);
   pc_cs_mfile(sfn_buffer, name, ext, CS_CHARSET_NOT_UNICODE);
   /* Check for valid file name, ignore case */
   return (pc_cs_valid_sfn(sfn_buffer, FALSE));
}
#endif /* #if (!RTFS_CFG_READONLY) */

BOOLEAN rtfs_cs_patcmp_8(byte *p, byte *pattern, BOOLEAN dowildcard)
{
#if (INCLUDE_CS_JIS)
    return(pc_jis_patcmp_8(p, pattern,dowildcard));
#else
    return(pc_ascii_patcmp_8(p, pattern,dowildcard));
#endif
}
BOOLEAN rtfs_cs_patcmp_3(byte *p, byte *pattern, BOOLEAN dowildcard)
{
#if (INCLUDE_CS_JIS)
    return(pc_jis_patcmp_3(p, pattern,dowildcard));
#else
    return(pc_ascii_patcmp_3(p, pattern,dowildcard));
#endif
}
#endif
