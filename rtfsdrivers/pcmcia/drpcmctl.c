/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*
*/
/*
 * drpcmctl.c - pd67xx.c chip driver for the cirrus logic 6710, 6720 and
 * intel 82365 compatible pcmcia bus controllers
 */

#include "rtfs.h"

#if (INCLUDE_82365_PCMCTRL)
/* Provided by the porting layer */
void hook_82365_pcmcia_interrupt(int irq);
void phys82365_to_virtual(byte ** virt, dword phys);
void write_82365_index_register(byte value) ;
void write_82365_data_register(byte value);
byte read_82365_data_register(void);

BOOLEAN pcmctrl_card_is_installed(int socket, BOOLEAN apply_power);
void pcmctrl_card_down(int socket);
BOOLEAN pcmctrl_card_power_up(int socket);



/* Constants used by pcmctrl.c */

#define NSOCKET 2

/* Memory regions which are used to map the CIS onto the host memory
   space. The first value is the 24 Bit linear physical address. The
   second value is this address a represented as a pointer in hosts
   logical address space. These values are peculiar to the cirrus logic
   6710, 6720 and intel 82365 compatible pcmcia bus controllers.
*/

/* WINDOW_0 - Used to access the CIS of slot 0 */

#define ISA_MEM_WINDOW_0   0xD0000L     /* ISA 24 bit physical address */
/*#define ISA_MEM_WINDOW_0   0xC8000L */

/* WINDOW_1 - Used to access the CIS of slot 1 */
#define ISA_MEM_WINDOW_1   0xCC000L     /* ISA 24 bit physical address */

/* Which interrupt do we use for the pcmcia card event register. This
   value is peculiar to the cirrus logic 6710, 6720 and intel 82365
   compatible pcmcia bus controllers. */
#define MGMT_INTERRUPT 15



#define PD_MAP_COMMON 0 /* routine is never used */

/* Addresses particular to this implementation */
#define ADDR_INDEX       0x3E0   /* PD67XX Index register in io space. */
#define ADDR_DATA        0x3E1   /* PD67XX Data register in io space. */
#define PCMCIA_CIS_SPACE   0x0L  /* Address of CIS in PCMCIA cards */
#define CIS_WINDOW           4   /* memory window we use for the CIS */
#define IO_WINDOW_0          0   /* Io windows we use */
#define IO_WINDOW_1          1

/* Constants used by the 82365/67xx pcmcia control module */

/* Tables of offsets to PD67XX registers */
KS_CONSTANT byte mem_start_address_low[5]  = {0x10, 0x18, 0x20, 0x28, 0x30};
KS_CONSTANT byte mem_start_address_high[5] = {0x11, 0x19, 0x21, 0x29, 0x31};
KS_CONSTANT byte mem_end_address_low[5]  = {0x12, 0x1a, 0x22, 0x2a, 0x32};
KS_CONSTANT byte mem_end_address_high[5]    = {0x13, 0x1b, 0x23, 0x2b, 0x33};
KS_CONSTANT byte mem_offset_low[5]  = {0x14, 0x1c, 0x24, 0x2c, 0x34};
KS_CONSTANT byte mem_offset_high[5] = {0x15, 0x1d, 0x25, 0x2d, 0x35};
KS_CONSTANT byte mem_page[5] = {0x40, 0x41, 0x42, 0x43, 0x44};
KS_CONSTANT byte io_start_address_low[2]  = {0x08, 0x0c};
KS_CONSTANT byte io_start_address_high[2] = {0x09, 0x0d};
KS_CONSTANT byte io_end_address_low[2]    = {0x0a, 0x0e};
KS_CONSTANT byte io_end_address_high[2]   = {0x0b, 0x0f};
KS_CONSTANT byte io_offset_low[2]         = {0x36, 0x38};
KS_CONSTANT byte io_offset_high[2]        = {0x37, 0x39};

/* Table mapping logical socket number to chip/socket.
   There should be one pair per logical socket number */
KS_CONSTANT byte chip_mask[2] = {0x0, 0x80};
KS_CONSTANT byte socket_mask[2] = {0x0, 0x40};


BOOLEAN pcmctrl_initted = FALSE;





/* This structure is used to pass mapping information between subroutines. */
typedef struct pcmcia_map {
    byte address_low;
    byte address_high;
    byte offset_low;
    byte offset_high;
} PCMCIA_MAP;

/* bits for controlling the IO window characteristics. ORed together  */
/* when calling pcmcia_map_io() */
#define IO_CTL_AUTO     0x02
#define IO_CTL_8BIT     0x00
#define IO_CTL_16BIT    0x01
#define IO_CTL_TR0      0x00
#define IO_CTL_TR1      0x40

/* bits for controlling the management interrupt ORed together  */
/* when calling card_set_mgmt_int() */
#define MGMT_BDEAD      0x01        /* Battery dead */
#define MGMT_BWARN      0x02        /* Battery low warning */
#define MGMT_RCHANGE    0x04        /* Change in ready line */
#define MGMT_CCHANGE    0x08        /* Card removed or inserted */


/* See bss.c */
BOOLEAN card_is_powered[NSOCKET];
int card_device_type[NSOCKET];


static void pd67xx_nand(int socket, int index, byte value);
static void pd67xx_or(int socket, int index, byte value);
static byte pd67xx_read(int socket, int index);
static void pd67xx_write(int socket, int index, byte value);
static void pd67xx_index(int socket, int index);
static void pd67xx_unmap_io(int socket , int window);
static void pd67xx_map_io(int socket , int window, int size, word isa_address, byte w_cntrl);
static void pd67xx_calc_io(struct pcmcia_map *pmap, word isa_address, word pcmcia_address);
static void pd67xx_unmap_memory(int socket, int window);
byte *  pd67xx_map_cis(int socket , dword offset);
static void pd67xx_calc_memory(struct pcmcia_map *pmap, dword isa_address, dword pcmcia_address);
static void pcmctrl_card_power_down(int socket);
void pcmctrl_device_power_up(int socket);
static void pcmctrl_enable_io_mode(int socket);
#if (PD_MAP_COMMON)
static void pd67xx_map_common(int socket, int window, KS_CONSTANT dword address, dword offset, int data_size);
#endif


/* Macro used to manipulte the second argument to calc io.
   Calc ios second argument is the pcmcia address that corresponds to
   the host IO address. In cases where the pcmcia address is the same as the
   host IO address this value should be the same as the hosts start of window.
   For example: A window at 0x170 yields
   offset = 0x170-0x170 = 0 so a host access to 0x170 yields
   0x170 + 0 = 0x170.
   In cases where the pcmcia address is not the same this should pass a value
   such that host_address + (this_value - host_address) == desired pcmia addres
   In a case where there is a pure offset to pcmcia then this value should
   contain the 0.
   For example if Host 0x1000 ==s PCMCIA 0 then pass in 0.
   to yield an offset of 0x0 - 0x1000 == (-0x1000)
   so that for example host 0x1170 yields
   0x1170 + (-0x1000) == 0x170.
*/
#define ISA_IO_TO_PCMCIA_ADDRESS(X) X




void  mgmt_isr(void)                          /*__fn__*/
{
int i;
byte card_status;

    for (i = 0; i < NSOCKET; i++)
    {
        card_status = pd67xx_read(i, 0x04);
        /* Write the status register to clear */
        pd67xx_write(i, 0x04, 0xff);
        if (card_status)
        {
            if (!pcmctrl_card_is_installed(i, FALSE))
            {
                pcmctrl_card_down(i);
                {
                int j;
                DDRIVE *pdr;

                    for (j = 0; j < prtfs_cfg->cfg_NDRIVES; j++)
                    {

                        pdr = pc_drno_to_drive_struct(j);
                        if (pdr &&
                           pdr->drive_flags & DRIVE_FLAGS_PCMCIA &&
                           pdr->drive_flags & DRIVE_FLAGS_VALID  &&
                           pdr->pcmcia_slot_number == i )
                                pdr->dev_table_perform_device_ioctl(
                                    pdr->driveno,
                                    DEVCTL_REPORT_REMOVE, (void *) 0);
                    }
                }
            }
        }
    }
}

BOOLEAN pcmctrl_init(void)  /* __fn__ */
{
int i;
byte mask;

    if (pcmctrl_initted)
        return(TRUE);


    /* Make sure power is down on all sockets */
     rtfs_port_disable();
    /* put the general control register in a known state */
    for (i = 0; i < NSOCKET; i++)
        pd67xx_write(i, 0x03, 0x40);

    for (i = 0; i < NSOCKET; i++)
        pcmctrl_card_down(i);
    hook_82365_pcmcia_interrupt(MGMT_INTERRUPT);
    /* write management interrupt configuration register
        interrupt 15, mask in card detect change */
    mask = (MGMT_INTERRUPT << 4);
    for (i = 0; i < NSOCKET; i++)
    {
        pd67xx_read(i, 0x04);           /* read the status register to clear */
        pd67xx_write(i, 0x05, (byte)(mask|8));
        pd67xx_read(i, 0x04);
    }
    rtfs_port_enable();
    pcmctrl_initted = TRUE;
    return(TRUE);
}

void pcmctrl_close(void)        /* __fn__ */
{

}

static void pcmctrl_enable_interrupt(int socket, int irq, BOOLEAN on_off)
{
byte c;
    /* Modify the interrupt and general control. */
    c = pd67xx_read(socket, 0x03);
    c &= 0xf0;          /* 0 card interrupt fields */
    if (on_off)
        c |= (byte) irq;
    pd67xx_write(socket, 0x03, c);
}

#if (INCLUDE_PCMCIA)

void pcmctrl_map_3com(int socket, int irq, int iobase)  /* __fn__ */
{
    pcmctrl_enable_io_mode(socket);
    /* Set up io windows */
    /* chip 0, Socket 0, window 0, size 16, isa_add = iobase pcmcia addr = iobase */
    /* Allow bus lines (CS16) to determine transfer size */
    pd67xx_map_io(socket, IO_WINDOW_0, 16, (word)iobase,
                  (word)(IO_CTL_AUTO|IO_CTL_16BIT|IO_CTL_TR1));
    pcmctrl_enable_interrupt(socket, irq, TRUE);

}



void pcmctrl_map_linksys_regs(int socket, int irq, int iobase)  /*__fn__*/
{
    /* enable IO mode. */
    pcmctrl_enable_io_mode(socket);

    /* Set up io windows */
    /* chip 0, Socket 0, window 0, size 16, isa_add = 0x170 pcmcia addr = 0x170 */
    /* Allow bus lines (CS16) to determine transfer size */
    pd67xx_map_io(socket, IO_WINDOW_0, 32, (word)iobase, (byte)(IO_CTL_AUTO|IO_CTL_16BIT|IO_CTL_TR1));
    /* Turn on interrupts */
    pcmctrl_enable_interrupt(socket, irq, TRUE);
    /* Apply Vpp power (does nothing since we use auto Vpp) */
    pcmctrl_device_power_up(socket);

}
#endif


void pcmctrl_map_ata_regs(int socket, dword ioaddr, int interrupt_number)
{
    /* enable IO mode. */
    pcmctrl_enable_io_mode(socket);

    /* Set up io windows */
    /* chip 0, Socket 0, window 0, size 16, isa_add = 0x170 pcmcia addr = 0x170 */
    /* Allow bus lines (CS16) to determine transfer size */
    pd67xx_map_io(socket, IO_WINDOW_0, 16, (word)ioaddr,IO_CTL_AUTO);
    /* chip 0, Socket 0, window 1, size 2. isa_add = 0x376 pcmcia addr = 0x376 */
    /* transfer size is always 8 bit */
#if (!USE_CONTIG_IO)
    pd67xx_map_io(socket, IO_WINDOW_1, 2,
                  (word)(ioaddr+0x206), IO_CTL_8BIT);
#endif
    if (interrupt_number >= 0)
        pcmctrl_enable_interrupt(socket, interrupt_number, TRUE);

    /* Apply Vpp power (does nothing since we use auto Vpp) */
    pcmctrl_device_power_up(socket);

}

/*  pcmctrl_card_is_installed(int socket, BOOLEAN apply_power) - Check if a socket has a card installed */
/* */
/*  Check the pcmcia interface at logical socket socket for the presence of  */
/*  a card. */
/* */
/*  Inputs: */
/*      Logical socket number */
/*  */
/*  Returns: */
/*      TRUE        - if a card is installed */
/*      FALSE           - if the socket is empty */

BOOLEAN pcmctrl_card_is_installed(int socket, BOOLEAN apply_power)          /*__fn__*/
{
byte c;

    /* Now read the interface status. Look for a card */
    c = pd67xx_read(socket, 1);
    if ( (c & 0x0c) == 0x0c)
    {
        if (apply_power && !card_is_powered[socket])
        {
            if (pcmctrl_card_power_up(socket))
                return(TRUE);
            else
            {
                pcmctrl_card_down(socket);
                return(FALSE);
            }

        }
        else
            return(TRUE);
    }
    return(FALSE);
}

/*  pcmctrl_card_installed(int socket) - Check if a socket has a card */
/*  and apply power installed */
/* */
/*  Check the pcmcia interface at logical socket socket for the presence of  */
/*  a card. If found apply power and return TRUE. Otherwise return FALSE. */
/*  If the card is found and powered this routine must set the vriable */
/*  BOOLEAN card_is_powered[socket] to TRUE. */
/*  */
/* */
/*  Inputs: */
/*      Logical socket number */
/*  */
/*  Returns: */
/*      TRUE        - if a card is installed */
/*      FALSE           - if the socket is empty */

BOOLEAN pcmctrl_card_installed(int socket)          /*__fn__*/
{
    return(pcmctrl_card_is_installed(socket, TRUE));
}


/*  pcmctrl_card_changed(int socket) - Check if a change event has
*   occured.
*
*  Note: If the pcmcia controller supports card removal interrupts then
*  this routine is not needed and may simply return FALSE always.
*  If card removal interrupts are not supported then this routine must
*  be implemented if hot swapping is needed.
*
*  If this routine is required, it must check the pcmcia interface at
*  the logical socket for the presence of a latched media change condition.
*  If a change has occured then it must clear the latched condition and
*  return TRUE. Otherwise it must return FALSE.
*
*  The routine also unmaps the card in pcmcia space and shuts down power
*  to the card. This is done to assure that the card powers up
*  appropriately when it is reopened.
*
*  Inputs:
*      Logical socket number
*
*  Returns:
*      TRUE         - a card has been inserted or removed since last called
*      FALSE        - no card has been inserted or removed since last called
*/

BOOLEAN pcmctrl_card_changed(int socket)          /*__fn__*/
{
byte card_status;
    card_status = pd67xx_read(socket, 0x04);
    if (card_status)
    {
        /* Write the status register to clear */
         pd67xx_write(socket, 0x04, 0xff);
         pcmctrl_card_down(socket);
         return(TRUE);
    }
    else
         return(FALSE);
}


/*  pcmctrl_power_up(int socket) - Enable power to a card. */
/* */
/*  This function clears the reset bit and enables power to a card. */
/*   */
/* Notes:  */
/*  1. The card is assumed to be in the slot (see card_is_installed(int socket)) */
/*  2. The power setting is 5.0 volts applied automatically when the card is */
/*  inserted. Other power options should be added as required. */
/*  Inputs: */
/*      Logical socket number */
/*  */
/*  Returns: */

BOOLEAN pcmctrl_card_power_up(int socket)                       /*__fn__*/
{
byte c;
word i;

    /* Set the *reset bit so the device comes up out of reset */
    c = pd67xx_read(socket, 0x03);
    c |= 0x40;
    pd67xx_write(socket, 0x03, c);
    /* Card not enabled no power  */
    pd67xx_write(socket, 0x02, 0x0);
    /* Sleep 50 msecs */
    rtfs_port_sleep(50);

    /* Card enabled, autopower, vcc power   */
    pd67xx_write(socket, 0x02, 0xB0);

    /* Sleep 300 msecs */
    rtfs_port_sleep(50);

    /* Toggle reset */
    c &= ~0x40;
    pd67xx_write(socket, 0x03, c);

    /* Sleep 10 msecs */
    rtfs_port_sleep(50);

    c |= 0x40;
    pd67xx_write(socket, 0x03, c);

    /* Sleep 20 msecs */
    rtfs_port_sleep(20);

    /* Wait for pcmcia ready. wait up to 1 seconds. */
    for (i = 0; i < 10; i++)
    {
        c = pd67xx_read(socket, 0x1);
        if (c & 0x20)
            break;
        rtfs_port_sleep(100);    /* Sleep 100 milliseconds */
    }
    if (!(c & 0x20))
        return(FALSE);
    card_device_type[socket] = 0;
    card_is_powered[socket] = TRUE;
    return(TRUE);
}


void pcmctrl_device_power_up(int socket)                        /*__fn__*/
{
    RTFS_ARGSUSED_INT(socket);
    /* Low Power Dynamic Mode. drive led on IRQ12. Tristat bit 7 on accesses to */
    /* 0x377/0x3f7 */
/* 2-10-98 try removing     pd67xx_write(socket, 0x1E, 0x32); TBD */
}


static void pcmctrl_enable_io_mode(int socket)
{
byte c;
    c = pd67xx_read(socket, 0x03);
    c |= 0x60;                  /* IS io. *RESET is FALSE */
    pd67xx_write(socket, 0x03, c);
}


/*  pcmctrl_card_power_down(int socket) - Turn off power to a card. */
/* */
/*  This function turns off power and clears the reset bit */
/*   */
/*  */
/*  Returns: */

static void pcmctrl_card_power_down(int socket)                     /*__fn__*/
{
byte c;
    /* Clear card enable but leave autopower, Vcc  power    */
    pd67xx_write(socket, 0x02, 0x30);
    /* Set the *reset bit so the device comes up out of reset */
    c = pd67xx_read(socket, 0x03);
    c |= 0x40;
    pd67xx_write(socket, 0x03, c);
    card_is_powered[socket] = FALSE;
    card_device_type[socket] = 0;
}

/*  pcmctrl_card_down(int socket) - Card down */
/* */
/*  The upper layers detected that the disk is not valid or is not  */
/*  installed. This routine is called so the slot is put in a known */
/*  state for re-insertion for pcmcia this is power down, for */
/*  True IDE this should probably assert -OE. */
/*   */
/*  */
/*  Returns: */


void pcmctrl_card_down(int socket)                      /*__fn__*/
{
int i;
    /* Unmap the io regions. */
    pd67xx_unmap_io(socket , 0);
    for (i = 0; i < 2; i++) pd67xx_unmap_io(socket, i);
    /* Unmap the memory regions. */
    for (i = 0; i < 5; i++) pd67xx_unmap_memory(socket, i);
    /* Make sure interrupts are disabled */
    pcmctrl_enable_interrupt(socket, 0, FALSE);
    /* Power it down */
    pcmctrl_card_power_down(socket);
}


byte pcmctrl_get_cis_byte(int socket, dword offset) /* __fn__ */
{
byte  *p;
byte c;
    c = 0;
    p = pd67xx_map_cis(socket , offset);
    if (p)
        c = *p;
    return(c);
}

void pcmctrl_put_cis_byte(int socket, dword offset, byte c) /*__fn__*/
{
byte  *p;
    p = pd67xx_map_cis(socket , offset);
    if (p)
        *p = c;
    pd67xx_unmap_memory(socket, CIS_WINDOW);
}



/* pd67xx_calc_memory - Calculate values for the address and offset values */
/* */
/* This routine take an ISA real memory address and a PCMCIA real memory  */
/* address and returns approriate address and offset register values */
/* to map the PCMCIA location into ISA space at the ISA memory address */

static void pd67xx_calc_memory(struct pcmcia_map *pmap, dword isa_address, dword pcmcia_address) /*__fn__*/
{
dword  uldiff;
    pmap->address_low  = (byte) ( (isa_address >> 12) & 0xff );
    pmap->address_high = (byte) ( (isa_address >> 20) & 0x0f );
    /* Calculate the difference of the two */
    uldiff = (dword) (pcmcia_address - isa_address);
    uldiff &= 0x03ffffffL;          /* 26 bits are significant for the offset */
    pmap->offset_low  = (byte) ( (uldiff >> 12) & 0xff );
    pmap->offset_high = (byte) ( (uldiff >> 20) & 0x3f );
}

/* pd67xx_map_cis - Set up a isa<->pcmcia memory window. */
/* */
/* This routine sets the memory address and offset registers up to map an */
/* ISA address range to a PCMCIA address range. The registers are configured */
/* and the window is enabled on the ISA bus */
/* */
/* Inputs: */
/* */
/*  socket              - Socket number. 0 or 1 */
/*  offset              - Offset into the CIS we are looking for */
/* */
/*  Outputs - Pointer to the memory window */
/* */


byte *  pd67xx_map(int socket , dword offset, BOOLEAN is_cis)   /*__fn__*/
{
struct pcmcia_map map;
byte c;
byte mask;
dword ultemp;
byte * p;
dword window_offset;
dword pointer_offset;
dword isa_cis_address;

    /* Pick the memory window we will be looking through. - this is
       the 24 bit physical window */
    if (socket == 0)
        isa_cis_address = ISA_MEM_WINDOW_0;
    else
        isa_cis_address = ISA_MEM_WINDOW_1;


#define W(INDEX, VALUE) pd67xx_write((int)socket,(int)(INDEX),(byte)(VALUE))

    mask = 0x01; mask <<= CIS_WINDOW;       /* Memory control reg mask */
    pd67xx_nand(socket, 0x06, mask);/* Make sure the window is disabled. */
    window_offset =  offset;
    if (is_cis) /* CIS memory is every other byte */
        window_offset *= 2;
    pointer_offset = window_offset;

    pointer_offset &= 0xfff;      /* Keep only 12 bits for pointer offset */
    window_offset &= 0xfffff000L; /* Keep hi bits for window */

    /* Calculate appropriate address/offset to see the memory. */
    pd67xx_calc_memory(&map, isa_cis_address, window_offset);

    W(mem_start_address_low[CIS_WINDOW], map.address_low);
    c = map.address_high;
    W(mem_start_address_high[CIS_WINDOW], c);
    W(mem_offset_low[CIS_WINDOW], map.offset_low);
    c = map.offset_high;
    if (is_cis)
        c |= (byte) 0x40;               /* Accessing CIS */
    W(mem_offset_high[CIS_WINDOW], c);

    /* Calculate appropriate end address. in 4K chunks */
    ultemp  = isa_cis_address;
    ultemp += (dword) (0x1000 * (/*size==*/ 1-1));
    /* pcmcia address is ignored */
    pd67xx_calc_memory(&map, ultemp, 0);
    W(mem_end_address_low[CIS_WINDOW], map.address_low);
    c = map.address_high;
    W(mem_end_address_high[CIS_WINDOW], c);
    pd67xx_or(socket, 0x06, mask);          /* Enable window on the ISA BUS */

    phys82365_to_virtual( &p, isa_cis_address);
#undef W
    p = p + (int)pointer_offset;
    return (p);
}

byte *  pd67xx_map_cis(int socket , dword offset)   /*__fn__*/
{
    return(pd67xx_map(socket , offset, TRUE));
}

byte *  pd67xx_map_sram(int socket , dword offset)  /*__fn__*/
{
    return(pd67xx_map(socket , offset, FALSE));
}


#if (PD_MAP_COMMON)
/* pd67xx_map_common - Set up a isa<->pcmcia memory window. */
/* */
/* This routine sets the memory address and offset registers up to map an */
/* ISA address range to a PCMCIA address range. The registers are configured */
/* and the window is enabled on the ISA bus */
/* */
/* Inputs: */
/* */
/*  socket              - Socket number. 0 or 1 */
/*  window              - window number in the chip */
/*  data_size           - 8 or 16  */
/* */
/*  Outputs - None. The memory is mapped to the cis window region */
/* */


static void pd67xx_map_common(int socket, int window, KS_CONSTANT dword address, dword offset, int data_size)   /*__fn__*/
{
struct pcmcia_map map;
byte c;
byte mask;
dword ultemp;

#define W(INDEX, VALUE) pd67xx_write((int)socket,(int)(INDEX),(byte)(VALUE))

    mask = 0x01; mask <<= window;       /* Memory control reg mask */
    pd67xx_nand(socket, 0x06, mask);/* Make sure the window is disabled. */

    /* Calculate appropriate address/offset to see the memory. */
    pd67xx_calc_memory(&map, address, offset);
    W(mem_start_address_low[window], map.address_low);
    c = map.address_high;
    c &= 0x0f;              /* High four bits are unused */
    if (data_size == 16)
        c |= 0x80;
    W(mem_start_address_high[window], c);
    W(mem_offset_low[window], map.offset_low);
    c = map.offset_high;
    /* c |= (byte) 0x40;            NOT !!! Accessing CIS */
    W(mem_offset_high[window], c);

    /* Calculate appropriate end address. in 4K chunks */
    ultemp  = address;
    ultemp += (dword) (0x1000 * (/*size==*/ 1-1));
    /* pcmcia address is ignored */
    pd67xx_calc_memory(&map, ultemp, offset);
    W(mem_end_address_low[window], map.address_low);
    c = map.address_high;
    W(mem_end_address_high[window], c);
#undef W
    pd67xx_or(socket, 0x06, mask);          /* Enable window on the ISA BUS */
}
#endif      /* PD_MAP_COMMON */

/* pd67xx_unmap_memory - Unmap up a isa<->pcmcia memory window. */
/* */
/* This routine disables the specified ISA memory window. */

static void pd67xx_unmap_memory(int socket, int window)  /* __fn__*/
{
byte mask;
    mask = 0x01; mask <<= window;       /* Memory control reg mask */
    pd67xx_nand(socket, 0x06, mask);/* Make sure the window is disabled. */
}

/* pd67xx_calc_io - Calculate values for the address and offset values */
/* */
/* This routine take an ISA real io address and a PCMCIA real io address */
/* and returns approriate address and offset register values */
/* to map the PCMCIA location into ISA space at the ISA memory address */

static void pd67xx_calc_io(struct pcmcia_map *pmap, word isa_address, word pcmcia_address) /* __fn__*/
{
word  ucdiff;
    pmap->address_low  = (byte) (isa_address&0xff);
    pmap->address_high = (byte) ((isa_address >> 8) & 0xff);
    /* Calculate the difference of the two */
    ucdiff = (word) (pcmcia_address - isa_address);
    pmap->offset_low  = (byte) (ucdiff&0xff);
    pmap->offset_high = (byte) ((ucdiff >> 8)&0xff);
}

/* pd67xx_map_io - Set up a isa<->pcmcia io window. */
/* */
/* This routine sets the memory address and offset registers up to map an */
/* ISA address range to a PCMCIA address range. The registers are configured */
/* BUT THE IO MAP IS NOT ENABLED IN THE IO MAP CONTROL REGISTER !!! */
/* Inputs: */
/* */
/*  socket              - Socket number. 0 or 1 */
/*  window              - io window 0 to 1 */
/*  size                - io window size in bytes */
/*  isa_address     - unsigned short physical ISA address for window */
/*  w_cntrl         - Bit map of window cntrl (access size, timing et al) */
/*                      See IO_CTL_xxx in the header */

static void pd67xx_map_io(int socket , int window, int size, word isa_address, byte w_cntrl)    /*__fn__*/
{
struct pcmcia_map map;
word uctemp;
byte mask,c;
#define W(INDEX, VALUE) pd67xx_write(socket,(int)(INDEX),(byte)(VALUE))


    mask = 0x40; mask <<= window;       /* IO control reg mask */
    pd67xx_nand(socket, 0x06, mask);/* Make sure the window is disabled. */

    /* set window control values to high or low nibble of control register */
    c = pd67xx_read(socket,0x07);
    if (window) c &= 0x0f; else c &= 0xf0;          /* Clear the nibble */
    w_cntrl <<= (window*4);
    c |= w_cntrl;
    W(0x07, c);  /* Write the approriate bits in the IO window control reg */

    /* Calculate appropriate address/offset to see the memory. */
    pd67xx_calc_io(&map, isa_address, ISA_IO_TO_PCMCIA_ADDRESS(isa_address));
    W(io_start_address_low[window], map.address_low);
    W(io_start_address_high[window], map.address_high);
    W(io_offset_low[window], map.offset_low);
    W(io_offset_high[window], map.offset_high);
    /* Calculate appropriate end address.  */
    uctemp  = (word)(isa_address + size - 1);
    /* pcmcia address is ignored */
    pd67xx_calc_io(&map, uctemp,  ISA_IO_TO_PCMCIA_ADDRESS(uctemp));
    W(io_end_address_low[window], map.address_low);
    W(io_end_address_high[window], map.address_high);

    pd67xx_or(socket, 0x06, mask);      /* Enable window on the ISA BUS */
#undef W
}
/* pcmcia_unmap_io - Unmap a isa<->pcmcia io window. */
/* */
/* This routine disables the specified ISA IO window. */

static void pd67xx_unmap_io(int socket , int window)  /* __fn__*/
{
byte mask;
    mask = 0x40; mask <<= window;       /* IO control reg mask */
    pd67xx_nand( socket, 0x06, mask);/* Make sure the window is disabled. */
}

/* pd67xx chip management functions. */
/* Write to the 6710 or 6720 chip.   */
/* chip == selects which CL-PD67XX to access 0 ==s first 1 ==s second */
/* socket == selects which socket  0 ==s first 1 ==s second */
/* index  == 0 - 64.  */
/* */
/* These table converts chip and socket id s to mask values  */
static void pd67xx_index(int socket, int index) /* __fn__ */
{
byte c;
    c = (byte) index;
    c |= (chip_mask[0]|socket_mask[socket]);
    write_82365_index_register(c);
}
static void pd67xx_write(int socket, int index, byte value) /* __fn__ */
{
    pd67xx_index(socket,index);     /* Establish the register pointer */
    write_82365_data_register(value);
}
/* Read from the 6710 or 6720 chip.   */
/* chip == selects which CL-PD67XX to access 0 ==s first 1 ==s second */
/* socket == selects which socket  0 ==s first 1 ==s second */
/* index  == 0 - 64.  */
static byte pd67xx_read(int socket, int index)      /* __fn__ */
{
    pd67xx_index(socket,index);     /* Establish the register pointer */
    return((byte)read_82365_data_register());
}

/* Or a value to a 6710 or 6720 register.   */
/* chip == selects which CL-PD67XX to access 0 ==s first 1 ==s second */
/* socket == selects which socket  0 ==s first 1 ==s second */
/* index  == 0 - 64.  */
/* USED TO SET BITS */
/* */
static void pd67xx_or(int socket, int index, byte value) /* __fn__ */
{
byte c;
    c = pd67xx_read(socket, index);
    c |= value;
    pd67xx_write(socket, index, c);
}

/* NAND a value to a 6710 or 6720 register.   */
/* chip == selects which CL-PD67XX to access 0 ==s first 1 ==s second */
/* socket == selects which socket  0 ==s first 1 ==s second */
/* index  == 0 - 64.  */
/* USED TO CLEAR BITS */
/* */
static void pd67xx_nand(int socket, int index, byte value) /* __fn__ */
{
byte c;
    c = pd67xx_read(socket, index);
    c &= (byte) ~(value);
    pd67xx_write(socket, index, c);

}

#ifdef DEBUG_CIS
void dump_cis(int socket)
{
byte  *p;
byte c;
byte buf[42];
int i;
int j;
    c = 0;
    p = pd67xx_map_cis(socket , 0);
    if (p)
    {
        for (j = 0; j < 8; j++)
        {
            for (i = 0; i < 40; i++)
            {
                c = *(p + j * 40 + i);
                if (c > 32 && c < 128)
                    buf[i] = c;
                else
                    buf[i] = '.';
            }
            buf[40] = 0;
            DEBUG_ERROR(">>> ", STR1, buf, 0);
        }
    }
    else
        DEBUG_ERROR("Map CIS Failed", NOVAR, 0, 0);
}
#endif

#endif /* INCLUDE_82365_PCMCTRL */
