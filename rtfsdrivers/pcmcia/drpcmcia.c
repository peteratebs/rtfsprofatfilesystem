/*****************************************************************************
*Filename: DRPCMCIA.C - Portable pcmcia access routines
*
*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*
*
* Description:
*
*   This code provides a logical access layer to the PCMCIA
*   card information structure. It is hardware independent.
*
*
*
****************************************************************************/

#include "rtfs.h"

#if (INCLUDE_PCMCIA)
/* Imports from the pcmctrl library for ERTFS or RTIP */
void pcmctrl_put_cis_byte(int socket, dword offset, byte c);
byte pcmctrl_get_cis_byte(int socket, dword offset);
BOOLEAN pcmctrl_init(void);
BOOLEAN pcmctrl_card_installed(int socket);

/* Imports from the pcmctrl library for RTIP */
void pcmctrl_map_ata_regs(int socket, dword ioaddr, int interrupt_number);
void pcmctrl_map_3com(int socket, int irq, int iobase);
void pcmctrl_map_linksys_regs(int socket, int irq, int iobase);
void pcmctrl_device_power_up(int socket);
void pcmctrl_card_down(int socket);


#define PCMCIA_NO_DEVICE 0
#define PCMCIA_ATA_DEVICE 1
#define PCMCIA_LINKSYS_DEVICE 2
#define PCMCIA_3C589_DEVICE 3
#define PCMCIA_SMC_DEVICE 4
#define PCMCIA_SRAM_DEVICE 5





void pcmcia_dummy(void) /* force pcmcia to link in under some linkers */
{
}
/* BSS area used by the pcmcia system */
extern int card_device_type[2];



/* CIS tuples */
#define CISTPL_NULL                 0x00
#define CISTPL_DEVICE               0x01
#define CISTPL_FUNCE_LAN_NODE_ID    0x04
#define CISTPL_LONG_LINK_A          0x11
#define CISTPL_LONG_LINK_C          0x12
#define CISTPL_LINKTARGET           0x13
#define CISTPL_NO_LINK              0x14
#define CISTPL_VERS_1               0x15
#define CISTPL_CONFIG               0x1A
#define CISTPL_MANFID               0x20
#define CISTPL_FUNCE                0x22
#define CISTPL_END                  0xff

typedef struct tuple_args {
    int  socket;            /* Logical socket number  */
    word  attributes;       /* If bit 0 is 1 return link information */
    byte    tuple_desired;      /* Which tupple is wanted 0xFF is wild */
    byte    tuple_offset;       /* For get_tuple_data */
    word  flags;                /* INTERNAL USE. Following values are defined */
#define TUFLG_NO_LNK    0x01    /* Found NOLINK tupple. long links not allowed */
#define TUFLG_L_LNK_C   0x02    /* link_offset is valid for common memory */
#define TUFLG_L_LNK_A   0x04    /* link_offset is valid for attribut memory */
#define TUFLG_COMMON    0x08    /* cis_offset points to common memory */
#define TUFLG_FIRST 0x10    /* This is a get first */
    dword   link_offset;        /* If there is a link this is its address */
    dword   cis_offset;     /* Offset into the CIS for this tupple */
    byte    tuple_code;     /* Current tuple */
    byte    tuple_link;     /* Next tuple */
} TUPLE_ARGS;
typedef TUPLE_ARGS  * PTUPLE_ARGS;

static BOOLEAN tuple_get(PTUPLE_ARGS pt);
static BOOLEAN tuple_next_tuple(int socket, PTUPLE_ARGS pt, byte *ptuple, byte *plink);
static BOOLEAN tuple_map_link(int socket, PTUPLE_ARGS pt, dword  *plink);
static BOOLEAN tuple_next_chain(int socket, PTUPLE_ARGS pt, byte *ptuple, byte *plink);
static int tuple_copy_data(int socket, PTUPLE_ARGS pt, byte  *pdata, word offset , int count);
static void tuple_map_tuple(int socket, PTUPLE_ARGS pt, byte *ptuple, byte *plink);
static int card_GetTupleData(PTUPLE_ARGS pt, byte  *pdata, int count);
static BOOLEAN card_GetFirstTuple(int socket, PTUPLE_ARGS pt);
static BOOLEAN card_config_addr(int socket, int register_no, dword *offset);
static BOOLEAN card_write_config_regs(int socket, int register_no, byte c);


static BOOLEAN card_write_config_regs(int socket, int register_no, byte c)          /*__fn__*/
{
dword offset;

    if (card_config_addr(socket, register_no, &offset))
    {
        pcmctrl_put_cis_byte(socket, offset, c);
        return(TRUE);
    }
    else
        return(FALSE);
}


static BOOLEAN card_config_addr(int socket, int register_no, dword *offset)  /*__fn__*/
{
TUPLE_ARGS t;
byte  buf[8];
int base_address_size;
dword base_address;
int mask_size;
int n;
word mask;
word offset_bit;

    t.tuple_offset = 0x00; t.tuple_desired = CISTPL_CONFIG;
    if (!card_GetFirstTuple(socket, &t))
        return(FALSE);

    /* Read the first 8 bytes from the CONIFG tuple. It is a variable length */
    /* structure (this is insane). But it must be at least 4 bytes. */
    if (card_GetTupleData((PTUPLE_ARGS)&t, (byte  *) buf, 8) < 4)
        return(FALSE);
    base_address_size = (int) (buf[0] & 0x03) + 1;  /* Address size is bits 10 */
    mask_size = (int) (buf[0] & 0x3C);              /* Mask size is bits 5432 */

    /* mask_size >= 2; */
    mask_size += 1;

    /* The base address is in  low byte to hibyte buf[2], buf[3] buf[4], buf[5].  */
    /* base address size determines which ones are significant */
    for (base_address = 0,n = base_address_size; n ; n--)
    { base_address <<= 8; base_address |= buf[n+1]; }

    mask = 0;       /* Only supporting two mask bytes for now */
    if (mask_size > 1) mask = (word) buf[2+base_address_size+1];
    mask |= (word) buf[2+base_address_size];

    /* Test if the register is correct   */
    offset_bit = 0;
    if (register_no < 16) offset_bit =1; offset_bit <<= register_no;
    if (!(offset_bit&mask))
        return(FALSE);


    /* divide the base address by two and add the offset.
        Do this because CIS memory is accessed on an every other
        byte basis so this value when passed to map_cis will work
    */
    base_address = base_address / 2;
    base_address = base_address + register_no;
    *offset = base_address;
    return(TRUE);

}



/* card_GetFirstTuple - Get the first tuple from the CIS which matches selection */
/* criterria. */
/* */
/* Inputs: */
/*  PTUPLE_ARGS pt  Pointer to a tuple arg structure (see below) */
/*      These values must be supplied by the caller */
/*  pt->attributes  - If bit 0 is 1 link information is returned */
/*  pt->tuple_desired  - Which tupple. 0 - 0xFF. If any tuple desired */
/*                          set to 0xFF. Note 0xff does not alter the meaning  */
/*                          of attributes bit. */
/* */
/* Outputs: */
/*  PTUPLE_ARGS pt  Pointer to a tuple arg structure (see below) */
/*      If TRUE is returned the following fields will be valid */
/*      pt->tuple_code  - Tuple code found */
/*      pt->tuple_link  - Link to next tuple. Also ==s the size of the  */
/*                          data field for this tuple. */
/* Returns: */
/*  TRUE        - The query was succesful, pt is filled in */
/*  FALSE           - The query failed  */
/* */
/*  Note: */
/*  If Get first returns success the pt structure may be passed to */
/*  card_GetNextTuple to get the next tuple and or card_GetTupleData     */
/*  to read the data field of the tuple. */
/* */

static BOOLEAN card_GetFirstTuple(int socket, PTUPLE_ARGS pt) /* __fn__ */
{
    pt->socket = socket;
    pt->flags = TUFLG_FIRST;
    return((BOOLEAN)tuple_get(pt));
}

/* card_GetNextTuple - Get the next tuple from the CIS which matches selection */
/* criteria in pt->tuple_desired */
/* */
/* Inputs: */
/*  PTUPLE_ARGS pt  - tuple arg structure returned from card_GetFirstTuple */
/*      pt->tuple_offset - Set this to zero */
/* Outputs: */
/*  PTUPLE_ARGS pt  - tuple arg structure (see card_GetFirstTuple) */
/* Returns: */
/* */
/*  TRUE        - The query was succesful, pt is filled in */
/*  FALSE           - The query failed  */
/* */
/*  Note: */
/*  card_GetFirstTuple must have been called first. Call card_GetTupleData   */
/*  to read the data field of the tuple. To scan all tuples in the CIS, first */
/*  call card_GetFirstTuple() with tuple_desired set to 0xff. If link fields */
/*  are of interest set pt->attributes to 0x0001. */

/* static BOOLEAN card_GetNextTuple(PTUPLE_ARGS pt)
{
    return(tuple_get(pt));
}
*/


/* card_GetTupleData - Get the data from the current tuple */
/*  */
/* */
/* Inputs: */
/*  PTUPLE_ARGS pt  - tuple arg structure returned from card_GetFirstTuple */
/*  or GetFirstTuple(). */
/*      You may change the following value:  */
/*      pt->tuple_offset - Set this value to zero to get all data. Or set it  */
/*                      to the offset into the data field to cop first. */
/*  byte  *pdata      - Destination address for the data */
/*  int count           - Number of bytes to copy (0-255). Pass in  */
/*                          pt->tuple_link to get all bytes. */
/* Outputs: */
/*  The tuple s data is copied into the buffer at pdata. */
/* Returns: */
/*  The number of bytes copied. This will equal the input count  */
/*  or pt->tuple_link-pt->tuple_offset whichever is smaller. */
/* */

static int card_GetTupleData(PTUPLE_ARGS pt, byte  *pdata, int count) /* __fn__ */
{
    return(tuple_copy_data(pt->socket, pt, pdata, pt->tuple_offset, count));
}


static void tuple_map_tuple(int socket, PTUPLE_ARGS pt, byte *ptuple, byte *plink) /* __fn__ */
{
    *ptuple = pcmctrl_get_cis_byte(socket, (int)pt->cis_offset);
    *plink  = pcmctrl_get_cis_byte(socket, (int)(pt->cis_offset+1));
}


static int tuple_copy_data(int socket, PTUPLE_ARGS pt, byte  *pdata, word offset , int count) /*__fn__*/
{
byte currtuple,currlink;
int i;
int cis_offset;

    cis_offset = (int)pt->cis_offset+2;     /* Offset of data past tupple and link */
    cis_offset += (int) offset;
    tuple_map_tuple(socket, pt, &currtuple, &currlink);
    if (count > (int) currlink)
        count = (int) currlink;
    for (i = 0; i < count; i++)
        *pdata++ = pcmctrl_get_cis_byte(socket, cis_offset++);
    return(count);
}

static BOOLEAN tuple_next_chain(int socket, PTUPLE_ARGS pt, byte *ptuple, byte *plink)  /*__fn__*/
{
byte currtuple,currlink;
byte buffer[3];

    if (!(pt->flags & (TUFLG_L_LNK_A|TUFLG_L_LNK_C)))
        return(FALSE);      /* No links in the previous chain */
    if (pt->flags & TUFLG_NO_LNK)
        return(FALSE);      /* No links allowed */

    pt->cis_offset = (int)pt->link_offset;  /* Copy the new offset */
    if (pt->flags & TUFLG_L_LNK_C)      /* Note if in common mem */
        pt->flags |= TUFLG_COMMON;
                                        /* Clear the link info */
    pt->flags &= (word) ~(TUFLG_L_LNK_A|TUFLG_L_LNK_C);
    /* map in the new tupple. check if it is a valid link */
    /* target. If so see if the user wants it. If any part */
    /* fails we fall through. */
    tuple_map_tuple(socket, pt, &currtuple, &currlink);
    if (currtuple == CISTPL_LINKTARGET)
    {
        if (tuple_copy_data(socket, pt, buffer, 0, 3) == 3)
        {
            if (buffer[0]=='C'&&buffer[1]=='I'&&buffer[2]=='S')
            {
                *ptuple = currtuple;
                *plink  = currlink;
                return(TRUE);
            }
        }
    }
    return(FALSE);
}

static BOOLEAN tuple_map_link(int socket, PTUPLE_ARGS pt, dword  *plink) /* __fn__ */
{
dword ultemp;
byte buffer[4];
*plink = 0;

    if (tuple_copy_data(socket, pt, buffer, 0, 4) == 4)
    {
        ultemp = 0;
        ultemp |= buffer[3]; ultemp <<= 8;
        ultemp |= buffer[2]; ultemp <<= 8;
        ultemp |= buffer[1]; ultemp <<= 8;
        ultemp |= buffer[0];
        *plink = ultemp;
        return(TRUE);
    }
    return(FALSE);
}


/*
static int tuple_get_data(PTUPLE_ARGS pt, byte  *pdata, int count)
{
    return(tuple_copy_data(pt->socket, pt, pdata, 0, count));
}
*/

static BOOLEAN tuple_next_tuple(int socket, PTUPLE_ARGS pt, byte *ptuple, byte *plink) /*__fn__*/
{
byte currtuple,currlink;
    tuple_map_tuple(socket, pt, &currtuple, &currlink);
    /* If at the end of a chain see if there is a long link to another chain */
    if ( (currtuple == CISTPL_END) || (currlink == 0xff) )
        return(tuple_next_chain(socket, pt, ptuple, plink));
    else
    {
        pt->cis_offset += 1;        /* Skip past the tuple */
        if (currtuple != CISTPL_NULL)
        {
            pt->cis_offset += 1;            /* Past link */
            pt->cis_offset += currlink; /* Past data */
        }
        tuple_map_tuple(socket, pt, ptuple, plink); /* Get the values */
        return(TRUE);
    }
}

static BOOLEAN tuple_get(PTUPLE_ARGS pt) /* __fn__ */
{
byte tuple;byte link;

    /* Clear return values */
    pt->tuple_code  = pt->tuple_link =  0;
    pt->tuple_offset =  0;
    /* If starting go back to square one */
    if (pt->flags & TUFLG_FIRST)
    {
        pt->flags       = 0;    /* Clear all flags */
        pt->link_offset  = pt->cis_offset = 0L;

        tuple_map_tuple(pt->socket, pt, &tuple, &link);
        /* If just starting make sure we have a CIS */
        if (tuple != 0x01)
        {  /* !!!!!!!!! This needs fixing for special cases */
            return(FALSE);
        }
    }
    else
    {
        if (!tuple_next_tuple(pt->socket, pt, &tuple, &link))
            return(FALSE);
    }

    for (;;)
    {
        /* handle special cases */
        switch(tuple) {
            case CISTPL_NO_LINK:
                pt->flags |= TUFLG_NO_LNK;
                break;
            case CISTPL_LONG_LINK_A:
                if (tuple_map_link(pt->socket, pt, &pt->link_offset)) /* read the link */
                    pt->flags |= TUFLG_L_LNK_A;
                break;
            case CISTPL_LONG_LINK_C:
                if (tuple_map_link(pt->socket, pt, &pt->link_offset))  /* read the link */
                    pt->flags |= TUFLG_L_LNK_C;
                break;
            default:
                break;
        }
        /* Now if we have a match, take it.  */
        if (pt->tuple_desired == 0xff || pt->tuple_desired == tuple)
        {
            switch (tuple) {
                case CISTPL_NO_LINK:    /* Only return link info if requested */
                case CISTPL_LONG_LINK_A:
                case CISTPL_LONG_LINK_C:
                case CISTPL_LINKTARGET:
                    if (!(pt->attributes & 0x01))
                        break;
                    /* Falls through on a hit. */
                default:
                    pt->tuple_code = tuple;
                    pt->tuple_link = link;
                    return(TRUE);
            }
        }
        if (!tuple_next_tuple(pt->socket, pt, &tuple, &link))
            return(FALSE);
    }
}


/* BOOLEAN pcmcia_card_is_sram(int socket) Return TRUE if there is an ATA card in the socket */
/*  Inputs: */
/*      Logical socket number */
/* */
/*  Returns: */
/*  TRUE    If an ATA device is installed. */
/*  FALSE       If an ATA device is not installed. */
/* */

BOOLEAN pcmcia_card_is_sram(int socket)                     /*__fn__*/
{
    RTFS_ARGSUSED_INT(socket);
    return(TRUE);
}


/* BOOLEAN pcmcia_card_is_ata(int socket) Return TRUE if there is an ATA card in the socket */
/*  Inputs: */
/*      Logical socket number */
/* */
/*  Returns: */
/*  TRUE    If an ATA device is installed. */
/*  FALSE       If an ATA device is not installed. */
/* */

BOOLEAN pcmcia_card_is_ata(int socket,
        dword register_file_address,
        int interrupt_number,      /* note -1 is polled for IDE */
        byte pcmcia_cfg_opt_value)  /*__fn__*/
{
TUPLE_ARGS t;
byte c;
word w;
int device_type;
BOOLEAN is_ata;

    if (!pcmctrl_init())
        return(FALSE);

    /* Get Device Information */
    is_ata = FALSE;

    t.tuple_desired = CISTPL_DEVICE;
    if (card_GetFirstTuple(socket, &t))
    {
        /* Get the Device type */
        if (card_GetTupleData((PTUPLE_ARGS)&t, (byte  *) &c, 1)==1)
        {
            device_type = (int)(c>>4);
            if (device_type == 0x0D /* DTYPE_FUNCSPEC */)
            {
                /* Call CISTPL_FUNCE. First 2 bytes should be 0x0101. */
                /* We look at as a word. Since its 0x0101 it is portable */
                /* with respect to byte order */
                t.tuple_offset = 0x00;
                t.tuple_desired = CISTPL_FUNCE;
                if( card_GetFirstTuple(socket, &t) &&
                (card_GetTupleData((PTUPLE_ARGS)&t,(byte  *) &w, 2)==2)
                && (w == (word) 0x0101))
                {
                    /* Put the chip into IO mode, map in the ATA register bank and */
                    /* enable the interrupt (if interrupt_no is -1 the interrupt is  */
                    /* not enabled but everything else is done) */

                    /* Write 0:0 to Socket copy register SOCK_COPY */
                    if (card_write_config_regs(socket, 3 /*SOCK_COPY_REGISTER*/, 0))
                    {
                    /* Set the Configuration option register. */
                        if (card_write_config_regs(socket, 0/* CONFIG_OPTION_REGISTER */,pcmcia_cfg_opt_value))
                        {
                            /* Set up io windows, enable interrupts, power to Vpp */
                            pcmctrl_map_ata_regs(socket, register_file_address, interrupt_number);
                            is_ata = TRUE;
                        }
                    }
                }
            }
        }
    }
    return(is_ata);
}


/* Return true if it is an ATA card */
BOOLEAN check_if_ata(int socket)                              /*__fn__*/
{
TUPLE_ARGS t;
byte c;
word w;
int device_type;
BOOLEAN is_ata;

    /* Get Device Information   */
    is_ata = FALSE;
    t.tuple_desired = CISTPL_DEVICE;
    if (card_GetFirstTuple(socket, &t))
    {
        /* Get the Device type   */
        if (card_GetTupleData((PTUPLE_ARGS)&t, (byte  *) &c, 1)==1)
        {
            device_type = (int)(c>>4);
            if (device_type == 0x0D /* DTYPE_FUNCSPEC */)
            {
                /* Call CISTPL_FUNCE. First 2 bytes should be 0x0101.       */
                /* We look at as a word. Since its 0x0101 it is portable    */
                /* with respect to byte order                               */
                t.tuple_offset = 0x00;
                t.tuple_desired = CISTPL_FUNCE;
                if( card_GetFirstTuple(socket, &t) &&
                (card_GetTupleData((PTUPLE_ARGS)&t,(byte  *) &w, 2)==2)
                && (w == (word) 0x0101))
                {
                    is_ata = TRUE;
                }
            }
        }
    }
    return(is_ata);
}
/*
* int  pcmcia_card_type(int slot)
*
* Check the slot (0 or 1) and return the type ofcard installed.
*
*
* Return values are:
*
* 0 - No card
* 1 - Unkown card
* 2 - ATA Card with normal CIS
* 3 - SMC Ethernet Card with normal CIS
* 4 - Linksys Ethernet Card
* 5 - 3COM Ethernet Card
*/

int  pcmcia_card_type(int socket)                                  /*__fn__*/
{
int card_type;
    /* If the card is already in use. query the device type table.
       we can not touch the CIS now */
    if (card_device_type[socket] != PCMCIA_NO_DEVICE)
    {
        if (card_device_type[socket] == PCMCIA_ATA_DEVICE)
            return(2);
        else
            return(1);  /* Unknown */
    }
    /* Initalize the pcmcia controller if it has not been done already
       return -1 on a controller failure (unlikely) */
    if (!pcmctrl_init())
        return(-1);
    /* Check if a card is installed and powered up   */
    if (!pcmctrl_card_installed(socket))
    {
        pcmctrl_card_down(socket);
        return(0); /* No Card */
    }

    if (check_if_ata(socket))
        card_type = 2; /* Ata card */
    else
        card_type = 1; /* No Card */

    pcmctrl_card_down(socket); /* Leave the card as we left it */
    return(card_type);
}

#endif /* INCLUDE_PCMCIA */
