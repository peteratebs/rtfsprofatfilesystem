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
#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */

#if (INCLUDE_CS_JIS)

static BOOLEAN _illegal_jis_alias_char(byte *p);
#if (INCLUDE_VFAT)
static BOOLEAN _illegal_jis_lfn_char(byte *p);
#endif
byte *jis_increment(byte *p);
int jis_char_length(byte *p);


/* Return TRUE if illegal for an 8 dot 3 file */
static BOOLEAN _illegal_jis_file_char(byte *p, int islfn)
{
byte c;

    /* {[0-80(asci)][81-9f(2byte)][a0{illegal)][a1-df(Katakana)][e0-fc(2byte)][fd-ff{illegal)] */
    if (jis_char_length(p) == 1)
    {
        c = *p;
        /* Test for valid chars. Note: we accept lower case because that
           is patched in pc_ino2dos() at a lower level */
        if ( (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') )
            return(FALSE); /* Valid */
        if (c >= 0xa1 && c <= 0xdf) /* Katakana */
            return(FALSE); /* Valid */
        if (islfn)
            return (_illegal_lfn_char(c)); /* same as ascii */
        return (_illegal_alias_char(c)); /* same as ascii */
    }
    else /* length is 2 */
    {
        p++;
        c = *p;
        if (c >= 0x40 && c <= 0x7e)
            return(FALSE); /* Valid */
        if (c >= 0x80 && c <= 0xfc)
            return(FALSE); /* Valid */
        return(TRUE);      /* Invalid */
    }
}
/* Return TRUE if illegal for an 8 dot 3 file */
static BOOLEAN _illegal_jis_alias_char(byte *p)
{
    return(_illegal_jis_file_char(p, 0));
}

/*****************************************************************************
    PC_VALID_SFN - See if filename is a valid short file name

Description
    Determines validity of a short file name based on the following criteria:
        - the file name must be between 0 and 8 characters
        - the file extension must be between 0 and 3 characters
        - the file name must not begin with a period
        - it must not be a reserved DOS file name
        - it must not contain any characters that are illegal within sfn's
Returns
    TRUE if filename is a valid sfn, FALSE otherwise
*****************************************************************************/

BOOLEAN jis_valid_sfn(byte *filename, BOOLEAN case_sensitive)    /* __fn__ */
{
    int len,period_count,ext_start;
    BOOLEAN badchar;
    byte name_part[9];

    int  char_len;
    byte *pname;
    if(name_is_reserved(filename)) return(FALSE);

    pname = filename;
    len = 0;
    badchar=FALSE;
    period_count=0;
    ext_start=0;
    name_part[0] = 0;
    while(*pname)
    {
        if(_illegal_jis_alias_char(pname)) badchar = TRUE;
        if(*pname == '.')
        {
            ext_start = len+1;
            period_count++;
        }

        char_len = jis_char_length(pname);
        if (!ext_start && len < 8)
        {
            if (char_len == 2)      /* end the test name for reserved */
                name_part[len] = 0;
            else
                name_part[len] = *pname;
            name_part[len+1] = 0;
        }
        if (case_sensitive && char_len == 1 && (*pname >= (byte) 'a' && *pname <= (byte) 'z'))
            badchar = TRUE;
        pname += char_len;
        len += char_len;
    }
    if(name_is_reserved(name_part)) return(FALSE);

    if( (filename[0] == ' ') ||                  /* 1st char is a space */
        (len == 0) ||                            /* no name */
        badchar ||                               /* contains illegal chars */
        (period_count > 1) ||                    /* contains more than one extension */
        ((len-ext_start)>3 && period_count>0) || /* extension is longer than 3 chars */
        (period_count==0 && len > 8) ||          /* name is longer than 8 chars */
        (ext_start > 9) ||                       /* name is longer than 8 chars */
        (ext_start==1) ) return(FALSE);          /* no name; 1st char is a period */

    return(TRUE);
}

#if (INCLUDE_VFAT) /* Small piece of compile time INCLUDE_VFAT vs's NONVFAT code  */

static BOOLEAN _illegal_jis_lfn_char(byte *p)
{
    return(_illegal_jis_file_char(p, 1));
}


/***************************************************************************
    validate_filename - See if filename is a valid long file name

Description
    Determines validity of a long file name based on the following criteria:
        - the file must be between 0 and 256 characters in length
        - it must not be a reserved DOS file name
        - it must not contain any characters that are illegal within lfn's
Returns
    TRUE if filename is a valid lfn, FALSE otherwise
*****************************************************************************/

BOOLEAN pc_jis_validate_filename(byte * filename, byte * ext)
{
    int len;
    byte name_part[9];

    RTFS_ARGSUSED_PVOID((void *) ext);


    for(len=0; *filename; len++)
    {
        if (_illegal_jis_lfn_char(filename))
            return(FALSE);
        if (len < 5)    /* handles lpt1, aux, con etc */
        {
                if(*filename == '.')
                {
                    name_part[len] = 0;
                    if(name_is_reserved(name_part)) return(FALSE);
                }
                else
                {
                    name_part[len] = *filename;
                    if (*(filename+1) == 0)
                    {
                        name_part[len+1] = 0;
                        if(name_is_reserved(name_part)) return(FALSE);
                    }
                }
        }
        filename = jis_increment(filename);
    }

    if( (len == 0) || (len > 255) ) return(FALSE);
    return(TRUE);
}

/* JIS Version */
BOOLEAN pc_jis_malias(byte *alias, byte *input_file, int which_try) /*__fn__*/
{
    int n,s;
    byte filename[10],fileext[4];
    byte *p_in, *p_in_ext, *p_temp, *p_temp_2;
    int char_len, jis_ext_len;

    /* Fill filename[8] with spaces before we start. */
    rtfs_memset(filename, ' ', 8);

    /* Check if invalid short file name. Ignore case. If contains lower case we'll fix it later !  */
    if ((which_try == -1) && !pc_cs_valid_sfn((byte *)input_file, FALSE))
        return(FALSE);

    /* Process the JIS alias name */
    p_in = input_file;
    while(*p_in =='.' || *p_in ==' ') p_in = jis_increment(p_in);

    /* find extension start */
    /* p_in_ext holds the position right */
    /* after the last period (or it is 0 */
    p_in_ext = 0;
    p_temp = p_in;
    while(*p_temp)
    {
        if (*p_temp =='.')
        {
            p_temp = jis_increment(p_temp);
            p_in_ext = p_temp;
        }
        else
            p_temp = jis_increment(p_temp);
    }

    p_temp_2 = &fileext[0];
    jis_ext_len = 0;        /* We'll use this later to append ext to alias */
    if(p_in_ext && *p_in_ext!=0){
        /* copy extension to fileext[] */
        p_temp = p_in_ext;
        for(s=0; *p_temp!=0 && s<3;p_temp+=char_len)
        {
            char_len = jis_char_length(p_temp);
            if(*p_temp!=' ')
            {
                /* finish if 2 bite jis overflows 3 */
                if(s==2&&char_len==2)
                {
                    break;
                }
                /* use '_' if illegal*/
                else if(_illegal_jis_alias_char(p_temp))
                {
                    *p_temp_2++ = '_';
                    s += 1;
                }
                else
                {
                    *p_temp_2++ = *p_temp;
                    if (char_len==2)
                        *p_temp_2++ = *(p_temp+1);
                    s += char_len;
                }
            }
        }
        jis_ext_len = s;    /*  Save for later. bytes in */
    }
    *p_temp_2 = 0;  /* NULL terminate extention */

    /* copy file name to filename[], filtering out spaces, periods and
        replacing characters illegal in alias names with '_' */
    p_temp   = input_file;
    p_temp_2 = filename;

    for(s=0; *p_temp!=0 && s<8; p_temp += char_len)
    {
        if (p_in_ext && p_temp>=p_in_ext)   /* hit extension ? */
            break;
        char_len = jis_char_length(p_temp);
        /* break and use ' ' if 2 bite jis overflows 8 character limit */
        /* Bug fix 2-1-07 , was if(s==5&&char_len==2) */
        if(s==7&&char_len==2)
        {
            filename[7] = ' '; /* shift-jis first bytes clear */
            break;
        }
        else if(*p_temp!=' ' && *p_temp !='.')
        {
            if(_illegal_jis_alias_char(p_temp))
            {
                 *p_temp_2++ = '_';
                 s += 1;
            }
            else
            {
                *p_temp_2++ = *p_temp;
                if (char_len==2)
                    *p_temp_2++ = *(p_temp+1);
                s += char_len;
            }
        }
    }
    /* Null terminate the file at length 8, the alias digits will be right justified in
       file name and the result will be copied, stripping out spaces */
    filename[8]=0;

    pc_cs_ascii_str2upper(filename,filename);
    pc_cs_ascii_str2upper(fileext,fileext);

    /* append (TEXT[])i to filename[] */
    if (which_try != -1)
    {
    /* June 2013 - Change integrated at the suggestion of AI corp. ifed out version failed in certain situations */
#if (0)

        for(n=7,s=which_try; s>0 && n>0; s/=10,n--)
        {
            filename[n] = (byte)(((byte)s%10)+'0');
         }
         if(n==0 && s>0)
             return(FALSE);
         else
             filename[n]='~';
#else
        int l;
        for (l=0,s=which_try; s>0; s/=10,l++);
        if (l > 6) return(FALSE);
        for (n=0; n<8; n++) {
            char_len = jis_char_length(&filename[n]);
            if (char_len == 2) {
                if ((n+1) == (7-l)) {
                    filename[n] = '~';
                    filename[7] = ' ';
                    n = 6;
                    break;
                } else if (n == (7-l)) {
                    filename[n] = '~';
                    n = 7;
                    break;
                }
                n++;
            } else {
                if (n == (7-l)) {
                    filename[n] = '~';
                    n = 7;
                    break;
                }
            }
        }
        for(s=which_try; s>0 && n>0; s/=10,n--)
        {
            filename[n] = (byte)(((byte)s%10)+'0');
        }
#endif

    }

        p_temp_2 = alias;
        p_temp = filename;

        /* copy filename[] to alias[], filtering out spaces */
        s = 0;
        while(*p_temp)
        {
            char_len = jis_char_length(p_temp);

            if (s == 7 && char_len == 2)
                break;
            if(*p_temp!=' ')
            {
                *p_temp_2++=*p_temp++;
                if (char_len == 2)
                    *p_temp_2++=*p_temp++;
                s += char_len;
                if (s == 8)
                    break;

            }
            else
                p_temp++;

        }
        if(jis_ext_len != 0)
        {
            *p_temp_2++='.'; /* insert separating period */

            /* copy fileext[] to alias[] */
            for(s=0; s < jis_ext_len; s++)
            {
                *p_temp_2++ = fileext[s];
            }
        }
    *p_temp_2=0; /* null terminate alias[] */
    return(TRUE);
}
#endif

#if (!INCLUDE_VFAT) /* Small piece of compile time INCLUDE_VFAT vs's NONVFAT code  */
BOOLEAN pc_jis_validate_8_3_name(byte * name,int len) /*__fn__*/
{
    int i;
    int last;

    last = len-1;

    for (i = 0; i < len; i++)
    {
        /* If we hit a space make sure the rest of the string is spaces */
        if (name[i] == ' ')
        {
            for (; i < len; i++)
            {
                if (name[i] != ' ')
                    return(FALSE);
            }
            break;
        }
        else
        {
            if (_illegal_jis_alias_char(&name[i]))
                return(FALSE);
            /* If it is a 2 byte char advance i */
            if (jis_char_length(&name[i])==2)
            {
                if (i == last)
                    return(FALSE);  /* two byte at the end. no good */
                i++;
            }
        }
    }
    return(TRUE);
}

#endif

#endif
#endif /* Exclude from build if read only */
