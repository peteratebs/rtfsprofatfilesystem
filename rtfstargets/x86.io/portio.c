/*
* portio.c
*
* ERTFS Porting layer peripheral input output functions. The user must port
* some or all of the routines in this file to his CPU environment according
* to the rules set forth in the ERTFS porting specification.
*
*   Copyright EBS Inc , 1993-2003
*   All rights reserved.
*   This code may not be redistributed in source or linkable object form
*   without the consent of its author.
*
*   Implementation for the x86 POLLED mode porting layer
*
*/

#include "rtfs.h"

/* This section is specific to the x86 polled support */

#define POLLED_DOS 0
#define POLLED_WINDOWS  1
#define POLLED_RTTARGET 0

#if (POLLED_RTTARGET)
/* Using On-Times Rttarget for PCI, CLOCK and Interrupt Management */
#include <rttarget.h>
#include <rttbios.h>
/* See below */
dword  get_bus_master_address_from_bios(int controller_number);
#endif

#if (defined(__BORLANDC__) || defined(_MSC_VER))
#define IS_X86  1
#include <conio.h>
#include <dos.h>
#if (defined(__BORLANDC__))
#define _outp outp
#define _inp inp
#define _outpw outpw
#define _inpw inpw
#define _FP_SEG(X) FP_SEG(X)
#define _FP_OFF(X) FP_OFF(X)
#endif
#if (defined(__WIN32__) || (defined(_MSC_VER) && (_MSC_VER >= 800) && (_M_IX86 >= 300)) )
#define IS_FLAT 1
#else
#define IS_FLAT 0
#endif
#define RTFS_OUTBYTE(ADDR,VALUE) _outp((ADDR), (VALUE))
#define RTFS_INBYTE(ADDR) (byte) _inp((ADDR))
#define RTFS_OUTWORD(ADDR,VALUE) _outpw((ADDR), (VALUE))
#define RTFS_INWORD(ADDR) (word) _inpw((ADDR))
#if (IS_FLAT)
#define RTFS_OUTDWORD(ADDR, VAL)    _outpd((ADDR), (VAL))
#else
#if (INCLUDE_UDMA)
#error UDMA only supported FLAT addressing mode
#endif
#endif
#endif

#if (!defined(IS_X86))
#define IS_FLAT 1
#endif

/* End this section is specific to the x86 polled support */
/* The code below must be implemented for all targets */

/*rtfs_port_disable() and rtfs_port_enable()
  Used by a few device drivers, floppy, flash chip and pcmctrl,
  you may ignore it if not being called, but first check to see
  if floppy, flash chip and pcmctrl are still the only drivers
  using the functions */

/*This function must disable interrupts and return. */
void rtfs_port_disable(void)
{
#if (defined(IS_X86))
    __asm cli
#endif
}

/*This function must re-disable interrupts that were disabled via a call to
rtfs_port_disable(). */

void rtfs_port_enable(void)
{
#if (defined(IS_X86))
__asm sti
#endif
}

#if (INCLUDE_82365_PCMCTRL)
/* These routines are required only if the 82365 pcmcia controller driver
   is included (pcmctrl.c)
 phys82365_to_virtual(byte * * virt, dword phys)
 write_82365_index_register(byte value)
 write_82365_data_register(byte value)
 read_82365_data_register(void)
*/
/* This routine must take a physical linear 32 bit bus address passed in
the "phys" argument and convert it to an address adressable in the logical
space of the CPU, returning that value in "virt". Two sample methods are
provided, a flat version where the virtual address is simply the "phys"
address cast to a pointer and a second segmented version for X86
segmented applications.
*/
void phys82365_to_virtual(byte * * virt, dword phys)
{
#if (IS_FLAT)
    *virt = (byte *) phys; /* example for flat unmapped address space */
#else
    /* real mode x86 version */
    /* Take the address of a pointer, (virt) and a physical address
       (0xd0000) and map in 64 k  */
    /* Real mode version */
    phys = phys << 12;      /* C0000 ==> C000 0000 */
    *virt = (byte *) phys;
#endif
}
/*
These routines write and read the 82365's index and data registers which,
in a standard PC environment, are located in IO space at address 0x3E0 and
0x3E1. Non PC architectures typically map these as memory mapped locations
somewhere high in memory such as 0xB10003E0 and 0xB10003E1.
*/
void write_82365_index_register(byte value)
{
    RTFS_OUTBYTE(0x3e0,value); /* Defined for X86. Replace with your own */
}

void write_82365_data_register(byte value)
{
    RTFS_OUTBYTE(0x3e1,value); /* Defined for X86. Replace with your own */
}

byte read_82365_data_register(void)
{
byte v;
    v = RTFS_INBYTE(0x3e1); /* Defined for X86. Replace with your own */
    return (v);
}
#endif /* (INCLUDE_82365_PCMCTRL) */

/*
These routines are only required if INCLUDE_IDE is turned on (ide_drv.c)
byte ide_rd_status(dword register_file_address)
byte ide_rd_sector_count(dword register_file_address)
byte ide_rd_alt_status(dword register_file_address,int contiguous_io_mode)
byte ide_rd_error(dword register_file_address)
byte ide_rd_sector_number(dword register_file_address)
byte ide_rd_cyl_low(dword register_file_address)
byte ide_rd_cyl_high(dword register_file_address)
byte ide_rd_drive_head(dword register_file_address)
byte ide_rd_drive_address(dword register_file_address,int contiguous_io_mode)
void ide_wr_dig_out(dword register_file_address, int contiguous_io_mode, byte value)
void ide_wr_sector_count(dword register_file_address, byte value)
void ide_wr_sector_number(dword register_file_address, byte value)
void ide_wr_cyl_low(dword register_file_address, byte value)
void ide_wr_cyl_high(dword register_file_address, byte value)
void ide_wr_drive_head(dword register_file_address, byte value)
void ide_wr_command(dword register_file_address, byte value)
void ide_wr_feature(dword register_file_address, byte value)
dword rtfs_port_bus_address(void * p)
void ide_insw(dword register_file_address, unsigned short *p, int nwords)
void ide_outsw(dword register_file_address, unsigned short *p, int nwords)

These routines are only required if INCLUDE_IDE
and INCLUDE_UDMA are turned on

dword rtfs_port_ide_bus_master_address(int controller_number)
byte ide_rd_udma_status(dword bus_master_address)
void ide_wr_udma_status(dword bus_master_address, byte value)
byte ide_rd_udma_command(dword bus_master_address)
void ide_wr_udma_command(dword bus_master_address, byte value)
void ide_wr_udma_address(dword bus_master_address, dword bus_address)
*/

#if (INCLUDE_IDE)
/* These routines are required only if using the IDE device driver */
/*  This function must return the byte in location 7 (IDE_OFF_STATUS)
    of the ide register file at register_file_address */
byte ide_rd_status(dword register_file_address)
{
    return(RTFS_INBYTE((word) (register_file_address + 7)));
}

/* This function must return the word in location 0 (IDE_OFF_DATA)
    of the ide register file at register_file_address */
word ide_rd_data(dword register_file_address)
{
    return(RTFS_INWORD((word)register_file_address));
}


/* This function must return the byte in location 2 (IDE_OFF_SECTOR_COUNT)
    of the ide register file at register_file_address */
byte ide_rd_sector_count(dword register_file_address)
{
    return(RTFS_INBYTE((word) (register_file_address + 2)));
}

/*  This function must return the byte in location 0x206 (IDE_OFF_ALT_STATUS)
    of the ide register file at register_file_address If the value of the
    argument contiguous_io_mode is 1 then the register must be 14 rather
    than 0x206.
*/

byte ide_rd_alt_status(dword register_file_address, int contiguous_io_mode)
{
    if (contiguous_io_mode)
        return(RTFS_INBYTE((word) (register_file_address + 14)));
    else
        return(RTFS_INBYTE((word) (register_file_address + 0x206)));
}
/* This function must return the byte in location 1 (IDE_OFF_ERROR)
    of the ide register file at register_file_address.
*/
byte ide_rd_error(dword register_file_address)
{
    return(RTFS_INBYTE((word) (register_file_address + 1)));
}

/* This function must return the byte in location 3 (IDE_OFF_SECTOR_NUMBER)
    of the ide register file at register_file_address
*/
byte ide_rd_sector_number(dword register_file_address)
{
    return(RTFS_INBYTE((word) (register_file_address + 3)));
}
/* This function must return the byte in location 4 (IDE_OFF_CYL_LOW)
    of the ide register file. register_file_address
*/
byte ide_rd_cyl_low(dword register_file_address)
{
    return(RTFS_INBYTE((word) (register_file_address + 4)));
}
/*  This function must return the byte in location 5 (IDE_OFF_CYL_HIGH)
    of the ide register file. register_file_address
*/
byte ide_rd_cyl_high(dword register_file_address)
{
    return(RTFS_INBYTE((word) (register_file_address + 5)));
}

/*  This function must return the byte in location 6 (IDE_OFF_DRIVE_HEAD)
    of the ide register file. register_file_address
*/
byte ide_rd_drive_head(dword register_file_address)
{
    return(RTFS_INBYTE((word) (register_file_address + 6)));
}

/*  This function must return the byte in location 0x207 (IDE_OFF_DRIVE_ADDRESS)
    of the ide register file at register_file_address.
    If the value of the argument contiguous_io_mode is 1 then the register
    must be 15 rather than 0x207.
*/

byte ide_rd_drive_address(dword register_file_address,int contiguous_io_mode)
{
    if (contiguous_io_mode)
        return(RTFS_INBYTE((word) (register_file_address + 15)));
    else
        return(RTFS_INBYTE((word) (register_file_address + 0x207)));
}

/* This function must place the byte in value at location
   0x206 (IDE_OFF_ALT_STATUS) of the ide register file at
   register_file_address. If the value of the argument contiguous_io_mode
   is 1 then the register must be 14 rather than 0x206.
*/


void ide_wr_dig_out(dword register_file_address, int contiguous_io_mode, byte value)
{
    if (contiguous_io_mode)
        RTFS_OUTBYTE((word) (register_file_address + 14), value);
    else
        RTFS_OUTBYTE((word) (register_file_address + 0x206), value);
}

/* This function must place the word in location 0 (IDE_OFF_DATA)
    of the ide register file at register_file_address */
void ide_wr_data(dword register_file_address, word value)
{
    RTFS_OUTWORD((word)register_file_address, value);
}

/*  This function must place the byte in value at location
    2 (IDE_OFF_SECTOR_COUNT) of the ide register file at
    register_file_address
*/
void ide_wr_sector_count(dword register_file_address, byte value)
{
    RTFS_OUTBYTE((word) (register_file_address + 2), value);
}

/*  This function must place the byte in value at location
    3 (IDE_OFF_SECTOR_NUMBER) of the ide register file at
    register_file_address
*/
void ide_wr_sector_number(dword register_file_address, byte value)
{
    RTFS_OUTBYTE((word) (register_file_address + 3), value);
}

/* This function must place the byte in value at location
    4 (IDE_OFF_CYL_LOW) of the ide register file at
    register_file_address
*/
void ide_wr_cyl_low(dword register_file_address, byte value)
{
    RTFS_OUTBYTE((word) (register_file_address + 4), value);
}
/* This function must place the byte in value at location
   5 (IDE_OFF_CYL_HIGH) of the ide register file at register_file_address
*/
void ide_wr_cyl_high(dword register_file_address, byte value)
{
    RTFS_OUTBYTE((word) (register_file_address + 5), value);
}

/* This function must place the byte in value at location
   6 (IDE_OFF_DRIVE_HEAD) of the ide register file at register_file_address
*/
void ide_wr_drive_head(dword register_file_address, byte value)
{
    RTFS_OUTBYTE((word) (register_file_address + 6), value);
}
/* This function must place the byte in value at location
    7 (IDE_OFF_COMMAND) of the ide register file at register_file_address */
void ide_wr_command(dword register_file_address, byte value)
{
    RTFS_OUTBYTE((word) (register_file_address + 7), value);
}

/*  This function must place the byte in value at location
    1 (IDE_OFF_FEATURE) of the ide register file at register_file_address
*/
void ide_wr_feature(dword register_file_address, byte value)
{
    RTFS_OUTBYTE((word) (register_file_address + 1), value);
}


/*  This function must read nwords 16 bit values from the data
    register at offset 0 of the the ide register file and place
    them in succesive memory locations starting at p. Since large
    blocks of data are transferred from the drive in this way this
    routine should be optimized. On x86 based systems the repinsw
    instruction should be used, on non x86 platforms the loop should
    be as tight as possible.
*/

void ide_insw(dword register_file_address, unsigned short *p, int nwords)
{
#if (!defined(IS_X86))
    while (nwords--)
        *p++ = (word)INWORD(register_file_address);
#else /* is an 808x */
word ioaddr;
#if (IS_FLAT)
ioaddr = (word) register_file_address;
__asm {
    pushf
    push        edi
    mov     dx, ioaddr
    mov     edi, p
    mov     ecx, nwords
    cld
    rep     insw
    pop     edi
    popf
}
#else
static word near s,o,_n, _i;
ioaddr = (word) register_file_address;
__asm {
pushf
cli
}
_n = (word)nwords;
s = (word)_FP_SEG(p);
o = (word)_FP_OFF(p);
_i = ioaddr;
__asm {
    pop ax              /* get flags back into ax */
    push    es
    push    di
    push    cx
    push    dx
    mov     dx, ds:_i
    mov     es, ds:s
    mov     di, ds:o
    mov     cx, ds:_n
    push ax             /* put flags back */
    popf
    cld
    rep     insw
    pop     dx
    pop     cx
    pop     di
    pop     es
}
#endif
#endif /* else is an 808x */
}
/*
    This function must write nwords 16 bit values to  the data
    register at offset 0 of the the ide register file. The data is
    taken from succesive memory locations starting at p. Since large
    blocks of data are transferred from the drive in this way this
    routine should be optimized. On x86 based systems the repoutsw
    instruction should be used, on non x86 platforms the loop should
    be as tight as possible.
*/
void ide_outsw(dword register_file_address, unsigned short *p, int nwords)
{
#if (!defined(IS_X86))
    while (nwords--)
        OUTWORD(register_file_address, *p++);
#else /* is an 808x */
word ioaddr;
ioaddr = (word) register_file_address;
#if (IS_FLAT)
__asm {
    pushf
    push    esi
    mov     dx, ioaddr
    mov     ecx, nwords
    mov     esi, p
    cld
    rep     outsw
    pop     esi
    popf
}
#else
static word near s,o,_n,_i;
__asm {
pushf
cli
}
_n = (word)nwords;
_i = ioaddr;

s = (word)_FP_SEG(p);
o = (word)_FP_OFF(p);

__asm {
    pop ax              /* get flags back into ax */
    push    es
    push    cx
    push    dx
    push    ds
    push    si
    mov     dx, ds:_i
    mov     cx, ds:_n
    mov     si, ds:o
    mov     ds, ds:s
    push ax             /* put flags back */
    popf
    cld
    rep     outsw
    pop     si
    pop     ds
    pop     dx
    pop     cx
    pop     es
}
#endif
#endif

}

#if (INCLUDE_UDMA)
/* These routines are required only if using an ultra-dma controller. */
/*  This function must determine if the specified controller is a PCI bus
    mastering IDE controller and if so return the location of the
    control and status region for that controller. If it is not a bus
    bus master controller it should return zero.
*/
dword rtfs_port_ide_bus_master_address(int controller_number)
{
#if (POLLED_RTTARGET)
    /* See below */
    return(get_bus_master_address_from_bios(controller_number));
#else
    RTFS_ARGSUSED_INT(controller_number);
    return (0); /* Must be implemented. 0 return val disables UDMA */
#endif
}

/*  This function must determine if the ATA cable is 80 wires or 40 wires.
    This can be done by sampling pin #34; if #34 is grounded, the 80-wire
    cable is likely used.
*/
BOOLEAN ide_detect_80_cable(int controller_number)
{
    RTFS_ARGSUSED_INT(controller_number);
    return(FALSE);
}

/*  This function must read the status byte value at location
    2 the bus master control region
*/
byte ide_rd_udma_status(dword bus_master_address)
{
byte value;
    value = RTFS_INBYTE((word) (bus_master_address + 2));
    return (value);
}

/*  This function must write the byte value to location
    2 of the bus master control region
*/
void ide_wr_udma_status(dword bus_master_address, byte value)
{
    RTFS_OUTBYTE((word) (bus_master_address + 2), value);
}

/*  This function must read the command byte value at location
    0 of the bus master control region
*/
byte ide_rd_udma_command(dword bus_master_address)
{
byte value;
    value = RTFS_INBYTE((word) bus_master_address);
    return (value);
}
/*  This function must write the byte value to location
    0 of the bus master control region
*/
void ide_wr_udma_command(dword bus_master_address, byte value)
{
    RTFS_OUTBYTE((word) bus_master_address, value);
}
/*  This function must write the byte dword to location
    4 of the bus master control region
*/
void ide_wr_udma_address(dword bus_master_address, dword bus_address)
{
    RTFS_OUTDWORD((word)(bus_master_address+4), bus_address);
}


/* This function must take a logical pointer and convert it to an
   dword representation of its address on the system bus */
dword rtfs_port_bus_address(void * p)
{
dword laddress;
#if (IS_FLAT)
    laddress = (dword) p;
#else
#error UDMA only supported FLAT addressing mode
#endif
    return(laddress);
}

#if (POLLED_RTTARGET)
/*  get_bus_master_address_from_bios() implemented for On-Time software's
    RTTARGET PCI Bios access API. Other API's will behave similarly */

#define RTPCI_BUS_MASTER_IDE_CONTROLLER 0x010180    /* Class code for a bus mastering controller */
#define RTPCI_BASE_ADDRESS_4            0x20    /* Configuration Space offset for the controller's address */

dword controller_addresses[2];
int num_controller_addresses;

dword  get_bus_master_address_from_bios(int controller_number)
{
   byte bus, devicefunc;
   unsigned int counter=0;
   num_controller_addresses = 0;

   if (RTT_BIOS_Installed() == 0)
       return 0;
   while((counter < 16) && (num_controller_addresses < 2))
   {
      /* Scan the PCI BUS to find a Bus mastering controller with the options in counter */
        bus = devicefunc = 0;
        if (RTT_BIOS_FindClassCode(RTPCI_BUS_MASTER_IDE_CONTROLLER, (int) counter, &bus, &devicefunc) ==    RTT_BIOS_SUCCESSFUL)
        {
            if (RTT_BIOS_ReadConfigData(bus, devicefunc, RTPCI_BASE_ADDRESS_4, 4, &controller_addresses[num_controller_addresses]) != 0) /* Primary Controller IOAddress */
                return(0);
            controller_addresses[num_controller_addresses]&=0xffffc;
            controller_addresses[num_controller_addresses+1]=controller_addresses[num_controller_addresses]+8;
            num_controller_addresses++;
            return (controller_addresses[controller_number]);
        }
        counter += 1;
    }
    return (0);
}
#endif /* (POLLED_RTTARGET) */

#endif /* (INCLUDE_UDMA) */
#endif /*  (INCLUDE_IDE) */


#if (INCLUDE_MMCCARD)
word read_mmc_word(dword bus_master_address)
{
word value;
    value = RTFS_INWORD((word) bus_master_address);
    return (value);
}
void write_mmc_word(dword bus_master_address, word value)
{
    RTFS_OUTWORD((word) bus_master_address, value);
}
#endif /* (INCLUDE_MMCCARD) */


#if (INCLUDE_SMARTMEDIA)
byte read_smartmedia_byte(dword bus_master_address)
{
byte value;
    value = RTFS_INBYTE((word) bus_master_address);
    return (value);
}
void write_smartmedia_byte(dword bus_master_address, byte value)
{
    RTFS_OUTBYTE((word) bus_master_address, value);
}
#endif /* (INCLUDE_SMARTMEDIA) */
