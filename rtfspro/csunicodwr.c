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
#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */
#if (INCLUDE_CS_UNICODE)

BOOLEAN _illegal_alias_char(byte ch);
#if (KS_LITTLE_ENDIAN)
#define BYTE_LOW  1 /* Numeric low byte of a character */
#define BYTE_HIGH 0 /* Numeric high byte of a character */
#else
#define BYTE_LOW  0 /* Numeric low byte of a character */
#define BYTE_HIGH 1 /* Numeric high byte of a character */
#endif


BOOLEAN pc_unicode_validate_filename(byte * name, byte * ext)
{
    int len;
    byte *pa, *pu, uni_buffer[16], ascii_buffer[8];

    RTFS_ARGSUSED_PVOID((void *) ext);

    /* Check short filenames to see if they are reserved names, The test
       is done in ascii because we use this for aliases */
    len = 0;
    pu = uni_buffer; pa = name;
    /* Make a unicode string up to DOT or Zero */
    while (CS_OP_IS_NOT_EOS(pa,CS_CHARSET_UNICODE))
    {
        if (CS_OP_CMP_ASCII(pa,'.',CS_CHARSET_UNICODE))
            break;
        CS_OP_CP_CHR(pu, pa,CS_CHARSET_UNICODE);
        CS_OP_INC_PTR(pu,CS_CHARSET_UNICODE);
        CS_OP_INC_PTR(pa,CS_CHARSET_UNICODE);
        CS_OP_TERM_STRING(pu,CS_CHARSET_UNICODE);
        len += 1;
        if (len > 4)
            break;
    }
    if (len && len <= 4)
    {
        map_unicode_to_jis_ascii(ascii_buffer, uni_buffer);
        if(name_is_reserved(ascii_buffer)) return(FALSE);
    }

 	/* July 2012 - Removed test that returned error for a leading space in a Unicode file name */

    len = 0; /* Fixed earlier bug which did not test the first character */
    while (CS_OP_IS_NOT_EOS(name,CS_CHARSET_UNICODE))
    {
        if (*(name+BYTE_LOW)==0)
            if (_illegal_lfn_char(*(name+BYTE_HIGH)))
                return(FALSE);
        name += 2;
        len += 1;
    }
    if(len > 255) return(FALSE);
    return(TRUE);
}

/* UNICODE Version

   Input file is a unicode file name
   Alias is populated with the ascii or JIS alias
*/

BOOLEAN pc_unicode_malias(byte *alias, byte *input_file, int which_try) /*__fn__*/
{
    BLKBUFF *scratch;
    byte *ascii_input_file;
    BOOLEAN ret_val = FALSE;

    scratch = 0;

    scratch = pc_scratch_blk();
    if (!scratch)
        goto ex_it;
    ascii_input_file = scratch->data;

    /* Map the unicode to ascii and create an ascii alias
      If the unicode contains non-ascii characters and it's the first
      try return false.. otherwise if everyhting else was legal we'd create an alias with the
      unicode replaced */
    if (map_unicode_to_jis_ascii(ascii_input_file, input_file) && (which_try == -1))
        goto ex_it;

    if (!pc_cs_malias(alias, ascii_input_file, which_try,CS_CHARSET_NOT_UNICODE))
        goto ex_it;
    ret_val = TRUE;
ex_it:
    if (scratch)
        pc_free_scratch_blk(scratch);
    return(ret_val);
}

#endif /* (INCLUDE_CS_UNICODE) */
#endif /* Exclude from build if read only */
