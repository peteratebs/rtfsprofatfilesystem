/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1993-2005
* All rights reserved.
* This code may not be redistributed in sourceor linkable object form
* without the consent of its author.
*/
/* drwindev.c - Access windows XP block devices directly

Summary

 Description
    Provides a device driver that reads and writes data to devices
    mounted under Windows XP such as the system's hard disk or removable
    devices sdcard, compact flash, a usb drive or floppy disk.





*/


#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <linux/unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "rtfs.h"
#include "portconf.h"   /* For included devices */

#if (INCLUDE_WINDEV)

int linux_fd;
_syscall5(int,  _llseek, uint, fd, ulong, hi, ulong, lo, loff_t *, res, uint, wh)
/*
*
*   Perform io to and from the win dev disk.
*
*   If the reading flag is true copy data from the hostdisk (read).
*   else copy to the hostdisk. (write).
*
*/


BOOLEAN win_dev_seek(int logical_unit_number, dword block);
BOOLEAN win_dev_write(int logical_unit_number, void  *buffer, word count);
BOOLEAN win_dev_read(int logical_unit_number, void  *buffer, word count);
BOOLEAN win_dev_open(DDRIVE *pdr);
static int calculate_hcn(PDEV_GEOMETRY pgeometry);

BOOLEAN windev_io(int driveno, dword block, void  *buffer, word count, BOOLEAN reading) /*__fn__*/
{
    DDRIVE *pdr;
    pdr = pc_drno_to_drive_struct(driveno);
    if (!pdr)
        return(FALSE);
    if (!win_dev_seek(pdr->logical_unit_number, block))
    {
        ERTFS_ASSERT(rtfs_debug_zero())
        return(FALSE);
    }
    if (reading)
        return(win_dev_read(pdr->logical_unit_number, buffer, count));
    else
        return(win_dev_write(pdr->logical_unit_number, buffer, count));
}


int windev_perform_device_ioctl(int driveno, int opcode, void * pargs)
{
DDRIVE *pdr;

    pdr = pc_drno_to_drive_struct(driveno);
    if (!pdr)
        return (-1);

    switch (opcode)
    {
        case DEVCTL_GET_GEOMETRY:
        {
            DEV_GEOMETRY gc;

            rtfs_memset(&gc, 0, sizeof(gc));
            if (!calculate_hcn(&gc))
            {
                ERTFS_ASSERT(rtfs_debug_zero())
				        return(-1);
            }
            copybuff(pargs, &gc, sizeof(gc));
            return (0);
        }
        case DEVCTL_FORMAT:
            break;
        case DEVCTL_REPORT_REMOVE:
            pdr->drive_flags &= ~DRIVE_FLAGS_INSERTED;
            return(0);
        case DEVCTL_CHECKSTATUS:
            return(DEVTEST_NOCHANGE);
         case DEVCTL_WARMSTART:
        {
          if (!win_dev_open(pdr))
              return(-1);
           pdr->drive_flags |= (DRIVE_FLAGS_VALID|DRIVE_FLAGS_INSERTED|DRIVE_FLAGS_REMOVABLE);
           return(0);
         }
            /* Fall through */
        case DEVCTL_POWER_RESTORE:
            /* Fall through */
        case DEVCTL_POWER_LOSS:
            /* Fall through */
        default:
            break;
    }
    return(0);
}

char *device_file_name = "/dev/sdb";

BOOLEAN win_dev_open(DDRIVE *pdr)
{
  linux_fd = open((char*)device_file_name, O_RDWR, S_IREAD | S_IWRITE);
  if (linux_fd>=0)
    return(TRUE);
  return(FALSE);
}

BOOLEAN win_dev_seek(int logical_unit_number, dword block)
{
dword hi, lo;
ddword lhi, llo, result,llbytes;

   llbytes = (ddword) block;
   llbytes *= 512;

   lhi = llbytes >> 32;
   llo = llbytes & 0xffffffff;
   lo = (dword) llo;
   hi = (dword) lhi;

   if (_llseek(linux_fd, hi, lo, &result, SEEK_SET) != 0)
    return(FALSE);

   if (result != llbytes)
       return(FALSE);

    return(TRUE);
 }

BOOLEAN win_dev_write(int logical_unit_number, void  *buffer, word nblocks)
{
dword nbytes, nwritten;
        nbytes = (dword)nblocks;
        nbytes *= 512;

      if ((nwritten = write(linux_fd,buffer,nbytes)) != nbytes)
			{
				return(FALSE);
			}
      else
        return(TRUE);
}


BOOLEAN win_dev_read(int logical_unit_number, void  *buffer, word nblocks)
{
dword nbytes, nread;
        nbytes = (dword)nblocks;
        nbytes *= 512;

      if ((nread = read(linux_fd,buffer,nbytes)) != nbytes)
			{
				return(FALSE);
			}
      else
        return(TRUE);
}

static int calculate_hcn(PDEV_GEOMETRY pgeometry)
{
long cylinders;  /*- Must be < 1024 */
long heads;      /*- Must be < 256 */
long secptrack;  /*- Must be < 64 */
long residual_h;
long residual_s;
dword n_blocks;
ddword llblocks;

    if (_llseek(linux_fd, 0, 0, &llblocks, SEEK_END) != 0)
      return(0);

    llblocks >>= 9; /* divide by 512 */

    n_blocks = (dword) llblocks;

    pgeometry->dev_geometry_lbas = n_blocks;
    secptrack = 1;
    while (n_blocks/secptrack > (1023L*255L))
        secptrack += 1;
    residual_h = (n_blocks+secptrack-1)/secptrack;
    heads = 1;
    while (residual_h/heads > 1023L)
        heads += 1;
    residual_s = (residual_h+heads-1)/heads;
    cylinders = residual_s;
    pgeometry->dev_geometry_cylinders = (dword) cylinders;
    pgeometry->dev_geometry_heads = (int) heads;
    pgeometry->dev_geometry_secptrack = (int) secptrack;
    return(1);

}
#endif /* (INCLUDE_WINDEV) */

