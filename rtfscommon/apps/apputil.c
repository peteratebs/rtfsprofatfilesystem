/**************************************************************************
    APPUTIL.C   - Parse and other utility functions for Pro and ProPlus command shells
                  Takes parsed argc, argv arguments and a parse template
                  and saves "typed" versions of arguments
                  The can be
*
* rtfs_print_prompt_user() -
*
*   This routine is called when the ERTFS demo programs and critical error handlr routine
*  requires console input from the user. It takes as input a prompt id (this is a numeric
*  handle to the prompt strings in the prompts string table prompt_table[]
*   in portstr.c and the address of a buffer where to place the console
*   input.
*
*   This routine displays the prompt by calling rtfs_print_one_string() and then
*   calls the target specific routine tm_gets_rtfs() to recieve the console
*   input.
*
*   For example a sequence of strings might fill.
*
*   FILLFILE TEST.DAT MYPATTERN 100
*   CAT TEST.DAT
*   DELETE TEST.BAT
*
*   If you do not wish to use the interactive test programs you need
*   not implement this function.
*****************************************************************************
*/

#include "rtfs.h"


long rtfs_atol(byte * s);
void rtfs_print_prompt_user(byte *prompt, byte *buf);

typedef struct dispatcher_text {
    byte *cmd;
    int  (*proc)( int argc, byte **argv);
    byte *helpstr;
} DISPATCHER_TEXT;

extern byte working_buf[512];      /* Not sector size dependant used by lex: must be global */

/* Miscelaneous functions */
/* PRFLG_NL means skip a line */

void show_status(char *prompt, dword val, int flags)
{
    rtfs_print_one_string((byte *)prompt,0);
    rtfs_print_long_1(val,flags);
}

static byte *gnext(byte *p,byte termat);
/* ******************************************************************** */
/* get next command; process history log */
void *lex(void *_pcmds, int *agc, byte **agv,byte *initial_cmd,byte *raw_input)
{
    byte *cmd,*p;
    DISPATCHER_TEXT *pcmds_start,*pcmds;
    *agc = 0;
    /* "CMD>" */
    if (initial_cmd)
        rtfs_cs_strcpy(working_buf, initial_cmd, CS_CHARSET_NOT_UNICODE);
    else
        rtfs_print_prompt_user((byte *)"CMD> ", working_buf);
    if (raw_input)
        rtfs_cs_strcpy(raw_input,working_buf, CS_CHARSET_NOT_UNICODE);

    pcmds = (DISPATCHER_TEXT *) _pcmds;
    pcmds_start = pcmds;

    p = cmd = &working_buf[0];

    p = gnext(p, ' ');
    /* Keep grabbing tokens until there are none left */
    while (p)
    {
       if (CS_OP_CMP_ASCII(p,'"', CS_CHARSET_NOT_UNICODE))
       { /* Quoted string.. find the end quote and terminate */
            CS_OP_TERM_STRING(p, CS_CHARSET_NOT_UNICODE);
            CS_OP_INC_PTR(p, CS_CHARSET_NOT_UNICODE);
            *agv++ = p;
            *agc += 1;
            p = gnext(p,'"');
       }
       else
       {
           *agv++ = p;
           *agc += 1;
           p = gnext(p,' ');
       }
    }

    {
    DISPATCHER_TEXT *pcmds_txt;
        pcmds_txt = (DISPATCHER_TEXT *) pcmds;
        while (pcmds_txt->cmd)
        {
            if (rtfs_cs_strcmp(cmd,pcmds_txt->cmd, CS_CHARSET_NOT_UNICODE) == 0)
                return ((void *)pcmds_txt);
            pcmds_txt++;
        }
    }
    /* No match return ??? */

    return ((void *)pcmds_start);
 }

/* Null term the current token and return a pointer to the next
   termat is '"' for a quoted string, otherwise it is ' '
*/
static byte *gnext(byte *p,byte termat)                                           /*__fn__*/
{
    /* GET RID OF LEADING SPACES */
    while (CS_OP_CMP_ASCII(p,' ', CS_CHARSET_NOT_UNICODE))
        CS_OP_INC_PTR(p, CS_CHARSET_NOT_UNICODE);

    while (CS_OP_IS_NOT_EOS(p, CS_CHARSET_NOT_UNICODE))
    {
        if (CS_OP_CMP_ASCII(p,termat, CS_CHARSET_NOT_UNICODE))
        {
            CS_OP_TERM_STRING(p, CS_CHARSET_NOT_UNICODE);    /* Null it and look at next */
            CS_OP_INC_PTR(p, CS_CHARSET_NOT_UNICODE);
            break;
        }
        CS_OP_INC_PTR(p, CS_CHARSET_NOT_UNICODE);
    }

    /* GET RID OF TRAILING SPACES */
    while (CS_OP_CMP_ASCII(p,' ', CS_CHARSET_NOT_UNICODE))
        CS_OP_INC_PTR(p, CS_CHARSET_NOT_UNICODE);
    if (CS_OP_IS_EOS(p, CS_CHARSET_NOT_UNICODE))                /* All done */
        return(0);

    return (p);
}


/* Argument parsing structures */
#define MAX_ARGS 20
typedef struct one_arg
{
    dword val_hi;
    dword val_lo;
    byte  *val_text;
} ONE_ARG;
typedef struct parsed_args
{
    int argc;
    ONE_ARG argv[MAX_ARGS];
} PARSED_ARGS;
PARSED_ARGS args;
static BOOLEAN parse_numeric_input(byte *input_buffer,dword *val_hi, dword *val_lo);
static byte *strip_if_quoted_input(byte *input_buffer);

static byte *strip_if_quoted_input(byte *input_buffer)
{
byte *p_in,*p_out,*ret_val;

   ret_val = p_out = p_in = input_buffer;
    if (*p_in == '\"')
    {
        p_in++;
        ret_val++;
        do
        {
            if (*p_in == '\"')
            {
                *p_out = 0;
                break;
            }
            else
                *p_out++ = *p_in;
        } while (*p_in++);
    }
    return(ret_val);
}


static BOOLEAN parse_numeric_input(byte *input_buffer,dword *val_hi, dword *val_lo)
{
    dword gigs, kilos, ones;
    int  n_commas;
    byte parse_buf[3][20];
    byte *p_out,*p_in;

    n_commas = 0;
    parse_buf[0][0] = 0; parse_buf[1][0] = 0;  parse_buf[2][0] = 0;
    p_in = input_buffer;
    p_out = &parse_buf[n_commas][0];

    *val_lo = *val_hi = 0;
    p_in = input_buffer;
    if (*p_in == ',' || ( (*p_in >= '0') &&  (*p_in <= '9')) )
        ;
    else
        return(FALSE);

    while (*p_in)
    {
        if (*p_in == ',')
        {
            n_commas += 1;
            if (n_commas == 3)
            {
                n_commas = 2;
                break;
            }
            p_out = &parse_buf[n_commas][0];
        }
        else
        {
            *p_out++ = *p_in;
            *p_out = 0;
        }
        p_in++;
    }
    if (n_commas >= 2)
    {
        gigs   = rtfs_atol(&parse_buf[0][0]);
        kilos  = rtfs_atol(&parse_buf[1][0]);
        ones   = rtfs_atol(&parse_buf[2][0]);
    }
    else if (n_commas >= 1)
    {
        gigs   = 0;
        kilos  = rtfs_atol(&parse_buf[0][0]);
        ones   = rtfs_atol(&parse_buf[1][0]);
    }
    else
    {
        gigs   = 0;
        kilos  = 0;
        ones   = rtfs_atol(&parse_buf[0][0]);
    }
{
#if (INCLUDE_RTFS_PROPLUS)  /* Proplus only Parse 64 bit file offset inputs */
    ddword ltemp_ddw;
    ltemp_ddw = M64SET32(0,gigs);
    ltemp_ddw =  M64LSHIFT(ltemp_ddw,30);
    kilos *= 1024;
    kilos += ones;
    ltemp_ddw =  M64PLUS32(ltemp_ddw,kilos);
    *val_lo = M64LOWDW(ltemp_ddw);
    if (val_hi)
        *val_hi = M64HIGHDW(ltemp_ddw);
#else
    dword ltemp;
    ltemp = gigs << 30;
    kilos *= 1024;
    kilos += ones;
    ltemp +=  kilos;
    *val_lo = ltemp;
    if (val_hi)
        *val_hi = 0;
#endif
}
     return(TRUE);
}


int parse_args(int agc, byte **agv, char *input_template)
{
byte *pt;
char *pi;
    rtfs_memset(&args, 0, sizeof(args));
    args.argc = 0;
    pi = input_template;
    while (agc)
    {
        if (parse_numeric_input(*agv,&(args.argv[args.argc].val_hi),
            &(args.argv[args.argc].val_lo)))
            args.argv[args.argc].val_text = 0;
        else
            args.argv[args.argc].val_text = strip_if_quoted_input(*agv);
        if (pi)
        {
            pt = args.argv[args.argc].val_text;
            if (*pi == 'B') /* Must be 'Y' or 'N' */
            {
                if (!pt)
                    return(0);
                if (*pt == 'Y' || *pt == 'y')
                    args.argv[args.argc].val_lo = 1;
                else if (*pt == 'N' || *pt == 'n')
                    args.argv[args.argc].val_lo = 0;
                else
                    return(0);
            }
            else if (*pi == 'I') /* must be a number */
            {
                if (pt)
                    return(0);
            }
            else if (*pi == 'T') /* must be text */
            {
                args.argv[args.argc].val_text = strip_if_quoted_input(*agv);
                if (!args.argv[args.argc].val_text)
                    return(0);
            }
            pi++;
        }
        args.argc += 1;
        agc--; agv++;
    }
    return(args.argc);
}

int rtfs_args_arg_count(void)
{
    return(args.argc);
}

dword rtfs_args_val_hi(int this_arg)
{
    return (args.argv[this_arg].val_hi);
}
dword rtfs_args_val_lo(int this_arg)
{
    return (args.argv[this_arg].val_lo);
}

#if (INCLUDE_CS_UNICODE)
/* Map argument to unicode if needed.
   Which_uarg tells us which unicode sbuffer to use, since we may need a few */
byte unicode_shellbuffs[2][512];
byte *rtfs_args_val_utext(int this_arg, int which_uarg)
{
    rtfs_print_one_string((byte *)"Converting to unicode: ", 0);;
    rtfs_print_one_string((byte *) args.argv[this_arg].val_text, PRFLG_NL);

    map_jis_ascii_to_unicode(&unicode_shellbuffs[which_uarg][0], args.argv[this_arg].val_text);
{
byte *b;
word *p;
       p = (word *)&unicode_shellbuffs[which_uarg][0];
       b = args.argv[this_arg].val_text;
       while (b && *b)
       {
        if (*b == '}')
        {
            *p = 0x1234;
            rtfs_print_one_string((byte *)"Converting } to unicode 0x1234",0);
        }
        b++; p++;
       }
}
    return(&unicode_shellbuffs[which_uarg][0]);
}
#endif

byte *rtfs_args_val_text(int this_arg)
{
    return (args.argv[this_arg].val_text);
}

void use_args(int agc, byte **agv){
    RTFS_ARGSUSED_INT(agc);
    RTFS_ARGSUSED_PVOID((void *)agv);
}

long rtfs_atol(byte * s);

int rtfs_atoi(byte * s)
{
    return((int)rtfs_atol(s));
}

long rtfs_atol(byte * s)
{
long n;
BOOLEAN neg;
int index;

    /* skip over tabs and spaces */
    while ( CS_OP_CMP_ASCII(s,' ', CS_CHARSET_NOT_UNICODE) || CS_OP_CMP_ASCII(s,'\t', CS_CHARSET_NOT_UNICODE) )
        CS_OP_INC_PTR(s, CS_CHARSET_NOT_UNICODE);
    n = 0;
    neg = FALSE;
    if (CS_OP_CMP_ASCII(s,'-', CS_CHARSET_NOT_UNICODE))
    {
        neg = TRUE;
        CS_OP_INC_PTR(s, CS_CHARSET_NOT_UNICODE);
    }
    while (CS_OP_IS_NOT_EOS(s, CS_CHARSET_NOT_UNICODE))
    {
        index = CS_OP_ASCII_INDEX(s, '0', CS_CHARSET_NOT_UNICODE);

        if (index >= 0 && index <= 9)
        {
            n = n * 10;
            n += (long) index;
            CS_OP_INC_PTR(s, CS_CHARSET_NOT_UNICODE);
        }
        else
            break;
    }

    if (neg)
        n = 0 - n;

    return(n);
}

void rtfs_print_prompt_user(byte *prompt, byte *buf)
{
    rtfs_print_one_string(prompt,0);
    *buf = 0;
    rtfs_kern_gets(buf);
}
