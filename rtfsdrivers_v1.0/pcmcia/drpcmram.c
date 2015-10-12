/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* drpcmram.c - PCMCIA SRam disk device driver.

Summary

 Description
    Provides a configurable pcmcia SRAM drive capability.

    Change the constant SRAM_CARD_SIZE in this file to use a different
    size ram card.

    Note: Currently limitted to 1024 bocks maximum do to the
    statement
        gc.dev_geometry_cylinders   = SRAM_CARD_SIZE;

    Note: Does not autosize ram cards.

*/
#include "rtfs.h"
#include "portconf.h"   /* For included devices */

#if (INCLUDE_PCMCIA_SRAM)
BOOLEAN pcmctrl_init(void);
BOOLEAN pcmctrl_card_installed(int socket);

byte *  pd67xx_map_sram(int socket , dword offset);

/* Set up for a 256K ram disk pcmcia card */
#define SRAM_CARD_SIZE 512

byte  *pcmsram_block(int socket_no, word block)                                       /*__fn__*/
{
byte  *p;
dword offset;

    offset = block;
    offset <<= 8;   /* Map cis multiplies the address by 2 so here we
                       multiply # blocks by 256 intead of 512 to get the
                       byte offset */
    offset += 256;  /* The SRAM starts at 512 */
    p = pd67xx_map_sram(socket_no , offset);
    return(p);
}


/* BOOLEAN sram_io(dword block, void *buffer, word count, BOOLEAN reading)
*
*   Perform io to and from the ramdisk.
*
*   If the reading flag is true copy data from the ramdisk (read).
*   else copy to the ramdisk. (write). called by pc_gblock and pc_pblock
*
*   This routine is called by pc_rdblock.
*
*/
BOOLEAN pcmsram_io(word driveno, dword block, void  *buffer, word count, BOOLEAN reading) /*__fn__*/
{
    byte  *p;
    int i;
    byte  *pbuffer;
    DDRIVE *pdr;

    pdr = pc_drno_to_drive_struct(driveno);
    if (!pdr)
        return (FALSE);



    pbuffer = (byte  *)buffer;

    while (count)
    {

        p = pcmsram_block(pdr->pcmcia_slot_number, (word) block);

        if (!p)
            return(FALSE);

        if (reading)
        {
            for (i = 0; i < 512; i++)
                *pbuffer++ = *p++;
        }
        else
        {
            for (i = 0; i < 512; i++)
            {
                *p++ = *pbuffer++;
            }
        }
        count--;
        block++;
    }
    return(TRUE);
}



int pcmsram_perform_device_ioctl(int driveno, int opcode, void * pargs)
{
DDRIVE *pdr;
DEV_GEOMETRY gc;        /* used by DEVCTL_GET_GEOMETRY */

    pdr = pc_drno_to_drive_struct(driveno);
    if (!pdr)
        return (-1);

    switch (opcode)
    {
    case DEVCTL_GET_GEOMETRY:
        rtfs_memset(&gc, (byte) 0, sizeof(gc));
        /* Now set the geometry section */
        gc.dev_geometry_heads       = 1;
        gc.dev_geometry_cylinders   = SRAM_CARD_SIZE;
        gc.dev_geometry_secptrack   = 1;

        copybuff(pargs, &gc, sizeof(gc));
        return (0);

    case DEVCTL_FORMAT:
        {
            byte  *p;
            dword block;
            int i;
            for (block = 0; block < SRAM_CARD_SIZE; block++)
            {
                p = pcmsram_block(pdr->pcmcia_slot_number, (word)block);
                if (!p)
                    return(-1);
                for (i = 0 ; i < 512; i++)
                    *p++ = 0;
            }
        }
        return(0);

    case DEVCTL_REPORT_REMOVE:
        pdr->drive_flags &= ~DRIVE_FLAGS_INSERTED;
        return(0);

    case DEVCTL_CHECKSTATUS:

        if (pdr->drive_flags & DRIVE_FLAGS_INSERTED)
            return(DEVTEST_NOCHANGE);
        else
        {
            if (pcmctrl_card_installed(pdr->pcmcia_slot_number))
            {
                pdr->drive_flags |= DRIVE_FLAGS_INSERTED;
                return(DEVTEST_CHANGED);
            }
            else
                return(DEVTEST_NOMEDIA);
        }
        /* never gets here */

    case DEVCTL_WARMSTART:
/*  Note: should check pcmcia controller status */
        pcmctrl_init();
        pdr->drive_flags |= (DRIVE_FLAGS_VALID|DRIVE_FLAGS_REMOVABLE);
        return(0);

    case DEVCTL_POWER_RESTORE:
        /* Fall through */
    case DEVCTL_POWER_LOSS:
        /* Fall through */
    default:
        break;
    }
    return(0);

}
#endif /* (INCLUDE_PCMCIA_SRAM) */
