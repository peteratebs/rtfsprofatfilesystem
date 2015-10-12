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
#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */

#if (INCLUDE_CS_ASCII)
/***************************************************************************
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
****************************************************************************/
BOOLEAN ascii_valid_sfn(byte *filename, BOOLEAN case_sensitive)
{
    int len,period_count,ext_start;
    byte name_part[9];
    BOOLEAN badchar;
    if(name_is_reserved(filename)) return(FALSE);
    name_part[0] = 0;
    for(len=0,badchar=FALSE,period_count=0,ext_start=0; filename[len]!=0; len++)
    {
        if(_illegal_alias_char(filename[len])) badchar = TRUE;
        if (case_sensitive && (filename[len] >= (byte) 'a' && filename[len] <= (byte) 'z'))
            badchar = TRUE;
        if(filename[len] == '.')
        {
            ext_start = len+1;
            period_count++;
        }
        else
        {
            if (!ext_start && len < 8)
            {
                name_part[len] = filename[len];
                name_part[len+1] = 0;
            }
        }
    }
    /* check if the file name part contains a reserved name */
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

#if (INCLUDE_VFAT) /* Small piece of compile time VFAT vs's NONVFAT code  */
/***************************************************************************
    PC_VALID_LFN - See if filename is a valid long file name

Description
    Determines validity of a long file name based on the following criteria:
        - the file must be between 0 and 256 characters in length
        - it must not be a reserved DOS file name
        - it must not contain any characters that are illegal within lfn's
Returns
    TRUE if filename is a valid lfn, FALSE otherwise
*****************************************************************************/
BOOLEAN pc_ascii_validate_filename(byte * filename, byte * ext)
{
    int len,n;
    byte name_part[9];

    RTFS_ARGSUSED_PVOID((void *) ext);

    name_part[0] = 0;
    for(n=0,len=0; filename[n]!=0; len++,n++)
    {
        if(_illegal_lfn_char(filename[n]))
            return(FALSE);
        else
        {
            if (len < 5)    /* handles lpt1, aux, con etc */
            {
                if(filename[len] == '.')
                {
                    name_part[len] = 0;
                    if(name_is_reserved(name_part)) return(FALSE);
                }
                else
                {
                    name_part[len] = filename[len];
                    if (filename[len+1] == 0)
                    {
                        name_part[len+1] = 0;
                        if(name_is_reserved(name_part)) return(FALSE);
                    }
                }
            }
        }
    }
    if( (len == 0) || (len > 255) ) return(FALSE);

    return(TRUE);
}

BOOLEAN pc_ascii_malias(byte *alias, byte *input_file, int which_try)
{
    int n,i,s;
    byte filename[9],fileext[4];

    /* Fill filename[8] with spaces before we start. */
    rtfs_memset(filename, ' ', 8);

    /* Check if invalid short file name. Ignore case. If contains lower case we fix it later !  */
    if ((which_try == -1) && !pc_cs_valid_sfn((byte *)input_file, FALSE))
        return(FALSE);

    /* Process the ASCII alias name */
    while(*input_file=='.' || *input_file==' ') input_file++;

    /* find extension start */
    for(n=0,i=0; input_file[n]!=0; n++) /* i holds the position right */
    {                                   /* after the last period      */
        if(input_file[n]=='.') i=n+1;
    }

    if(i>0 && input_file[i]!=0){
    /* copy extension to fileext[] */
    for(n=i,s=0; input_file[n]!=0 && s<3; n++)
    {
        if(input_file[n]!=' ')
        {
        if(_illegal_alias_char(input_file[n]))
            {
                fileext[s++] = '_';
            }
            else
                fileext[s++] = input_file[n];
        }
    }
    fileext[s]=0;} else { i=512; fileext[0]=0; } /* null terminate, not sector size dependent */
    /* copy file name to filename[], filtering out spaces, periods and
        replacing characters illegal in alias names with '_' */
    for(n=0,s=0; n<i && input_file[n]!=0 && s<8; n++)
    {
        if(input_file[n]!=' ' && input_file[n]!='.')
        {
            if(_illegal_alias_char(input_file[n]) || input_file[n]>127)
            {
                filename[s++] = '_';
            }
            else
                filename[s++] = input_file[n];
        }
    }
    for(;s<8;s++) /* pad with spaces */
    {
        filename[s] = ' ';
    }
    filename[8]=0; /* null terminate filename[] */

    pc_cs_ascii_str2upper(filename,filename);
    pc_cs_ascii_str2upper(fileext,fileext);

    if (which_try != -1)
    {
        /* append (TEXT[])i to filename[] */
        for(n=7,s=which_try; s>0 && n>0; s/=10,n--)
        {
             filename[n] = (byte)(((byte)s%10)+'0');
        }
        if(n==0 && s>0)
             return(FALSE);
        else
             filename[n]='~';
    }
     /* copy filename[] to alias[], filtering out spaces */
    for(n=0,s=0; s<8; s++)
    {
        if(filename[s]!=' ')
            alias[n++]=filename[s];
    }
    if(fileext[0] != 0)
    {
        alias[n++]='.'; /* insert separating period */
        /* copy fileext[] to alias[] */
        for(s=0; fileext[s]!=0; s++,n++)
        {
            alias[n]=fileext[s];
        }
     }
     alias[n]=0; /* null terminate alias[] */

    return(TRUE);
}
#endif

#if (!INCLUDE_VFAT) /* Small piece of compile time INCLUDE_VFAT vs's NONVFAT code  */
BOOLEAN pc_ascii_validate_8_3_name(byte * name,int len) /*__fn__*/
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
    }
    return(TRUE);
}
#endif /* INCLUDE_VFAT */

#endif
#endif /* Exclude from build if read only */
