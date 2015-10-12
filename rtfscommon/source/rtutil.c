/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* RTUTIL.C - String manipulation and byte order conversion routines */


#include "rtfs.h"

/******************************************************************************
PC_PARSEDRIVE -  Get a drive number from a path specifier

  Description
  Take a path specifier in path and extract the drive number from it.
  If the second character in path is ':' then the first char is assumed
  to be a drive specifier and 'A' is subtracted from it to give the
  drive number. If the drive number is valid, driveno is updated and
  a pointer to the text just beyond ':' is returned. Otherwise null
  is returned.
  If the second character in path is not ':' then the default drive number
  is put in driveno and path is returned.

    Unicode character function. Unicode is required

      Returns
      Returns NULL on a bad drive number otherwise a pointer to the first
      character in the rest of the path specifier.
      ***************************************************************************/

      /* Get the drive number form the path. Make sure one is provided */
      /* and that it is valid. */
      /* returns -1 if not valid otherwise return the driveno */

      /* UNICODE/ASCI/JIS Version */
int pc_path_to_driveno(byte  *path, int use_charset)                           /* __fn__*/
{
    byte *p;
    int drivenumber;
    p = path;
    if (!p || CS_OP_IS_EOS(p, use_charset))
        return(-2);
    CS_OP_INC_PTR(p, use_charset);
    if (CS_OP_CMP_ASCII(p,':', use_charset))
    {
        drivenumber = CS_OP_ASCII_INDEX(path,'A', use_charset);
        if (drivenumber < 0 || drivenumber > 25)
        {
            rtfs_set_errno(PEINVALIDDRIVEID, __FILE__, __LINE__); /* pc_path_to_driveno: invalid drive number */
            drivenumber = -1;
        }
    }
    else
        drivenumber = -2;
    return(drivenumber);
}

/* Parse drive part from a string - return -2 if no 'A:' -1 if A:
and not valid drive id */
int pc_parse_raw_drive(byte  *path, int use_charset)                             /* __fn__*/
{
    int dno;

    /* Allow device drivers to poll on-line status and register or deregister themselves if their status changes */
    pc_rtfs_poll_devices_ready();

    dno = pc_path_to_driveno(path, use_charset);
    if (dno < 0 || !pc_validate_driveno(dno))
        return (-1);
    else
        return(dno);
}

/* Extract drive no from D: or use defualt. return the rest of the string
or NULL if a bad drive no is requested */
byte *pc_parsedrive(int *driveno, byte  *path, int use_charset)                              /* __fn__*/
{
    byte *p = path;
    int dno;

    /* get drive no */
    dno = pc_path_to_driveno(path, use_charset);
    if (dno == -2)  /* D: Not specified. Use current default  */
    {
        dno = pc_get_default_drive(0);
        /* Make sure default drive number is valid */
        if (!pc_validate_driveno(dno))
            return(0);
    }
    else if (dno == -1) /* D: specified but bogus */
        return(0);
    else  /* D: Specified. validate */
    {
        if (!pc_validate_driveno(dno))
            return(0);
        /* SKIP D and : */
        CS_OP_INC_PTR(p, use_charset);
        CS_OP_INC_PTR(p, use_charset);
    }

    *driveno = dno;
    return (p);
}

/***************************************************************************
        PC_MPATH  - Build a path sppec from a filename and pathname

 Description
        Fill in to with a concatenation of path and filename. If path
        does not end with a path separator, one will be placed between
        path and filename.

        TO will be null terminated.

 Returns
        A pointer to 'to'.

*****************************************************************************/
byte *pc_mpath(byte *to, byte *path, byte *filename, int use_charset)                                /* __fn__*/
{
        byte *retval = to;
        byte *p;
        byte c[2];
        /* April 2012 Added code to protect against overruning the to buffer.
           The callers buffer is always at least EMAXPATH_BYTES long so use that as a guard. */
        int bytescopied;
        int maxbytes;

        bytescopied=0;
        if (use_charset == CS_CHARSET_UNICODE)
            maxbytes=EMAXPATH_BYTES-2;
        else
            maxbytes=EMAXPATH_BYTES-1;

        c[0] = c[1] = 0;
        p = path;
        while (bytescopied < maxbytes && CS_OP_IS_NOT_EOS(p, use_charset))
        {
            /* April 2012 Fixed bug that broke out when the first space occured */
            if (CS_OP_CMP_ASCII(p,' ', use_charset))
            {
            byte *checktoend=p;
                /* Check if there are all space until the end */
                while (CS_OP_IS_NOT_EOS(checktoend, use_charset))
				{
                    if (!CS_OP_CMP_ASCII(checktoend,' ', use_charset))
                        break;
					/* July 2012 bug fix, advance pointer */
					CS_OP_INC_PTR(checktoend, use_charset);
				}
                if (CS_OP_IS_EOS(checktoend, use_charset))/* If at end all spaces */
                    break;
            }
            CS_OP_CP_CHR(c, p, use_charset);
            CS_OP_CP_CHR(to, p, use_charset);
            CS_OP_INC_PTR(p, use_charset);
            CS_OP_INC_PTR(to, use_charset);
            bytescopied = (int)(to-retval);
        }
        /* Put \\ on the end f not there already, but not if path was null */
        if (bytescopied < maxbytes && p != path && !CS_OP_CMP_ASCII(c,'\\', use_charset))
        {
            CS_OP_ASSIGN_ASCII(to,'\\', use_charset);
            CS_OP_INC_PTR(to, use_charset);
            bytescopied = (int)(to-retval);
        }

        p = filename;
        while (bytescopied < maxbytes && CS_OP_IS_NOT_EOS(p, use_charset))
        {
            CS_OP_CP_CHR(to, p, use_charset);
            CS_OP_INC_PTR(p, use_charset);
            CS_OP_INC_PTR(to, use_charset);
            bytescopied = (int)(to-retval);
        }
        CS_OP_TERM_STRING(to, use_charset);
        return (retval);
}

/* Byte oriented - search comma separated list in set for filename */
BOOLEAN pc_search_csl(byte *set, byte *string)
{
byte *p;
    if (!set || !string)
        return(FALSE);
    while(*set)
    {
        p = string;
        while (*set && *p && (*set == *p))
        {
            set++;
            p++;
        }
        if ((*set == 0 || *set == ',') && *p == 0)
            return(TRUE);
        while (*set && *set != ',') set++;
        while (*set == ',') set++;
        if (!(*set))
            break;
    }
    return(FALSE);
}

BOOLEAN name_is_reserved(byte *filename)    /* __fn__*/
{
    return (
    pc_search_csl((byte *)pustring_sys_ucreserved_names, filename) ||
    pc_search_csl((byte *)pustring_sys_lcreserved_names, filename)
    );
}

/* ******************************************************************** */
/* KEEP COMPILER HAPPY ROUTINES */
/* ******************************************************************** */
/* Used to keep the compiler happy  */
void RTFS_ARGSUSED_PVOID(void * p)  /*__fn__*/
{
    p = p;
}

/* Used to keep the compiler happy  */
void RTFS_ARGSUSED_INT(int i)       /*__fn__*/
{
    i = i;
}

/* Used to keep the compiler happy  */
void RTFS_ARGSUSED_DWORD(dword l)       /*__fn__*/
{
    l = l;
}

/*****************************************************************************
PC_CNVRT -  Convert intel byte order to native byte order.

  Summary
  #include "rtfs.h"

    dword to_DWORD (from)  Convert intel style 32 bit to native 32 bit
    byte *from;

      word to_WORD (from)  Convert intel style 16 bit to native 16 bit
      byte *from;

        void fr_WORD (to,from) Convert native 16 bit to 16 bit intel
        byte *to;
        word from;

          void fr_DWORD (to,from) Convert native 32 bit to 32 bit intel
          byte *to;
          dword from;

            Description
            This code is known to work on 68K and 808x machines. It has been left
            as generic as possible. You may wish to hardwire it for your CPU/Code
            generator to shave off a few bytes and microseconds, be careful though
            the addresses are not guaranteed to be word aligned in fact to_WORD AND
            fr_WORD's arguments are definately NOT word alligned when working on odd
            number indeces in 12 bit fats. (see pc_faxx and pc_pfaxx().

              Note: Optimize at your own peril, and after everything else is debugged.

                Bit shift operators are used to convert intel ordered storage
                to native. The host byte ordering should not matter.

                  Returns

                    Example:
                    See other sources.

                      *****************************************************************************
*/

/* Convert a 32 bit intel item to a portable 32 bit */
dword to_DWORD ( byte *from)                                                                        /*__fn__*/
{
    dword res;
#if (KS_LITTLE_ENDIAN && KS_LITTLE_ODD_PTR_OK)
    res = ((dword) *((dword *)from));
#else
    dword t;
    t = ((dword) *(from + 3)) & 0xff;
    res = (t << 24);
    t = ((dword) *(from + 2)) & 0xff;
    res |= (t << 16);
    t = ((dword) *(from + 1)) & 0xff;
    res |= (t << 8);
    t = ((dword) *from) & 0xff;
    res |= t;
#endif
    return(res);
}

/* Convert a 16 bit intel item to a portable 16 bit */
word to_WORD ( byte *from)                                                                      /*__fn__*/
{
    word nres;
#if (KS_LITTLE_ENDIAN && KS_LITTLE_ODD_PTR_OK)
    nres = ((word) *((word *)from));
#else
    word t;
    t = (word) (((word) *(from + 1)) & 0xff);
    nres = (word) (t << 8);
    t = (word) (((word) *from) & 0xff);
    nres |= t;
#endif
    return(nres);
}

/* Convert a portable 16 bit to a  16 bit intel item */
void fr_WORD ( byte *to,  word from)                                            /*__fn__*/
{
#if (KS_LITTLE_ENDIAN && KS_LITTLE_ODD_PTR_OK)
    *((word *)to) = from;
#else
    *to             =       (byte) (from & 0x00ff);
    *(to + 1)   =   (byte) ((from >> 8) & 0x00ff);
#endif
}

/* Convert a portable 32 bit to a  32 bit intel item */
void fr_DWORD ( byte *to,  dword from)                                          /*__fn__*/
{
#if (KS_LITTLE_ENDIAN && KS_LITTLE_ODD_PTR_OK)
    *((dword *)to) = from;
#else
    *to = (byte) (from & 0xff);
    *(to + 1)   =  (byte) ((from >> 8) & 0xff);
    *(to + 2)   =  (byte) ((from >> 16) & 0xff);
    *(to + 3)   =  (byte) ((from >> 24) & 0xff);
#endif
}

#if (INCLUDE_MATH64)	   /* 64 bit word byte order conversions */
void to_DDWORD(ddword *to, byte *from)
{
#if (KS_LITTLE_ENDIAN && KS_LITTLE_ODD_PTR_OK)
    *to = *((ddword *)from);
#else
    dword t,lo, hi;

    t = ((dword) *(from + 7)) & 0xff;
    hi = (t << 24);
    t = ((dword) *(from + 6)) & 0xff;
    hi |= (t << 16);
    t = ((dword) *(from + 5)) & 0xff;
    hi |= (t << 8);
    t = ((dword) *(from + 4)) & 0xff;
    hi |= t;

    t = ((dword) *(from + 3)) & 0xff;
    lo = (t << 24);
    t = ((dword) *(from + 2)) & 0xff;
    lo |= (t << 16);
    t = ((dword) *(from + 1)) & 0xff;
    lo |= (t << 8);
    t = ((dword) *from) & 0xff;
    lo |= t;
    *to =  M64SET32(hi, lo);
#endif
}
void fr_DDWORD( byte *to,  ddword from_ddw)
{
#if (KS_LITTLE_ENDIAN && KS_LITTLE_ODD_PTR_OK)
    *((ddword *)to) = from_ddw;
#else
dword from;
	from = M64LOWDW(from_ddw);
    *to = (byte) (from & 0xff);
    *(to + 1)   =  (byte) ((from >> 8) & 0xff);
    *(to + 2)   =  (byte) ((from >> 16) & 0xff);
    *(to + 3)   =  (byte) ((from >> 24) & 0xff);
	from = M64HIGHDW(from_ddw);
    *(to + 4) = (byte) (from & 0xff);
    *(to + 5)   =  (byte) ((from >> 8) & 0xff);
    *(to + 6)   =  (byte) ((from >> 16) & 0xff);
    *(to + 7)   =  (byte) ((from >> 24) & 0xff);
#endif
}
#endif


/* SUm 2 32 bit values to fit inside 4 Gig size limit */
dword truncate_32_sum(dword val1, dword val2)
{
dword result;

    if (!val1 && !val2)
        return(0);
    result = val1 + val2;
    if ((result < val1) || (result < val2))
        return(LARGEST_DWORD);
    else
        return(result);
}
