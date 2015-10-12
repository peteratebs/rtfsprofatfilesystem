/******************************************************************
   File Name:        SmtMedia.c

      This file contains the BIOS level routines for the SmartMedia
      card.  These are the only hooks between the SmartMedia system
      and the EBS Real Time File Manage.

 ******************************************************************/
#include <rtfs.h>
#include <portconf.h>

#if (INCLUDE_SMARTMEDIA)
#include "smartmed.h"
#include "smarthw.h"


/********************* External Function Prototypes ************************/
char SmartMedia_Initialize(void);
char SmartMedia_Check_Parameter(word* cylinder, byte* head, byte* sector);
char SmartMedia_ReadSector(dword  start, word count, byte* destination);
char SmartMedia_WriteSector(dword start, word count, byte* source);
char SmartMedia_EraseAll(void);


int smartmedia_driveno = -1;


/*<BCI>*****************************************************************
Name:   BOOLEAN smartmedia_io(int driveno,
                                dword block,
                                    void *buffer,
                                        word count,
                                            BOOLEAN reading)

Parameters: driveno - Selected drive
            block   - logical sector number
            buffer  - buffer to read to or write from
            count   - number of bytes to read\write
            reading - TRUE if reading, FALSE if writing
Returns:    Error Status
Description: Perform io to and from the smartmedia.
             If the reading flag is true copy data from the smartmedia (read).
             else copy to the smartmedia. (write). called by pc_gblock and pc_pblock

Called By:  This routine is called by pc_rdblock.
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
BOOLEAN smartmedia_io(int driveno, dword block, void *buffer, word count, BOOLEAN reading)
{
    byte *buf;
    int ErrorCode;
    buf = (byte*)buffer;
    driveno++;
    if(reading)
        ErrorCode = SmartMedia_ReadSector(block+Ssfdc.BootSector, count, buf);
    else
        ErrorCode = SmartMedia_WriteSector(block+Ssfdc.BootSector, count, buf);
    if( ErrorCode == SmartMediaError_NoError)
        return TRUE;
    else
        return FALSE;
}



/*<BCI>*****************************************************************
Name:   int smartmedia_perform_device_ioctl(int driveno, int opcode, PFVOID pargs)
Parameters: driveno - Selected drive
            opcode  -
            pargs   -
Returns:    -1 = Failed, 0 = Success
Description: Respond and reply to the following low level commands...
                DEVCTL_GET_GEOMETRY:  Get geometry parameters
                DEVCTL_WARMSTART:     Initialize the card (generate Log2Phy table)
                DEVCTL_FORMAT:        Low Level Format (erase the entire card)
                DEVCTL_CHECKSTATUS:   Check if SmartMedia is installed
                DEVCTL_POWER_RESTORE: Not used
                DEVCTL_POWER_LOSS:    Not used
Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
int smartmedia_perform_device_ioctl(int driveno, int opcode, void *pargs)
{
int ErrorCode;
DDRIVE *pdr;
DEV_GEOMETRY gc;        // used by DEVCTL_GET_GEOMETRY
word cylinders;
byte  heads;
byte  sectors;

    pdr = pc_drno_to_drive_struct(driveno);
    if (!pdr)
        return (-1);

    switch (opcode)
    {
        case DEVCTL_GET_GEOMETRY:
            ErrorCode = SmartMedia_Check_Parameter(&cylinders, &heads, &sectors);
            if( ErrorCode != SmartMediaError_NoError)
                return (-1);

       		/* Prepare to return an initialized format parameter structure */
            rtfs_memset(&gc, 0, sizeof(gc));

            gc.dev_geometry_heads       = heads;
            gc.dev_geometry_cylinders   = cylinders;
            gc.dev_geometry_secptrack   = sectors;

			switch(Ssfdc.Model)
		    {
			case SSFDC1MB:
			case SSFDC2MB:
			case SSFDC4MB:
			case SSFDC8MB:
				/* Now fill in specific logical format parameters
				   so we adhere to recomended values */
	    	    /* Force format routine to use format params that we specify */
    	    	gc.fmt_parms_valid = TRUE;
      	        rtfs_cs_strcpy(gc.fmt.text_volume_label,(byte *)pustring_sys_volume_label, CS_CHARSET_NOT_UNICODE);
    	    	gc.fmt.physical_drive_no =   0;
        		gc.fmt.binary_volume_label = BIN_VOL_LABEL;
				switch(Ssfdc.Model)
			    {
				case SSFDC1MB:
		            gc.fmt.secpalloc =      (byte)  8;
		            gc.fmt.secreserved =    (word)  1;
	    	        gc.fmt.numfats     =    (byte)  2;
	        	    gc.fmt.secpfat     =    (word)  1;
	            	gc.fmt.numroot     =    (word)  0x100;
		            gc.fmt.mediadesc =      (byte)  0xF8;
	    	        gc.fmt.secptrk     =    (word)  4;
	        	    gc.fmt.numhead     =    (word)  4;
	        	    gc.fmt.nibs_per_entry = 3;
	            	gc.fmt.numcyl     =     (word)  125;
					break;
				case SSFDC2MB:
		            gc.fmt.secpalloc =      (byte)  8;
		            gc.fmt.secreserved =    (word)  1;
	    	        gc.fmt.numfats     =    (byte)  2;
	        	    gc.fmt.secpfat     =    (word)  2;
	            	gc.fmt.numroot     =    (word)  0x100;
		            gc.fmt.mediadesc =      (byte)  0xF8;
	    	        gc.fmt.secptrk     =    (word)  8;
	        	    gc.fmt.numhead     =    (word)  4;
	        	    gc.fmt.nibs_per_entry = 4;
	            	gc.fmt.numcyl     =     (word)  125;
					break;
				case SSFDC4MB:
		            gc.fmt.secpalloc =      (byte)  16;
		            gc.fmt.secreserved =    (word)  1;
	    	        gc.fmt.numfats     =    (byte)  2;
	        	    gc.fmt.secpfat     =    (word)  2;
	            	gc.fmt.numroot     =    (word)  0x100;
		            gc.fmt.mediadesc =      (byte)  0xF8;
	    	        gc.fmt.secptrk     =    (word)  8;
	        	    gc.fmt.numhead     =    (word)  4;
	        	    gc.fmt.nibs_per_entry = 4;
	            	gc.fmt.numcyl     =     (word)  250;
					break;
				case SSFDC8MB:
		            gc.fmt.secpalloc =      (byte)  16;
		            gc.fmt.secreserved =    (word)  1;
	    	        gc.fmt.numfats     =    (byte)  2;
	        	    gc.fmt.secpfat     =    (word)  3;
	            	gc.fmt.numroot     =    (word)  0x100;
		            gc.fmt.mediadesc =      (byte)  0xF8;
	    	        gc.fmt.secptrk     =    (word)  16;
	        	    gc.fmt.numhead     =    (word)  4;
	        	    gc.fmt.nibs_per_entry = 4;
	            	gc.fmt.numcyl     =     (word)  125;
					break;
				}
			default:
				/* For other then 1,2,4,8 MB use RTFS defaults */
				break;
			}
			copybuff(pargs, &gc, sizeof(gc));
    	    return (0);
			break;

        case DEVCTL_FORMAT:
            ErrorCode = SmartMedia_EraseAll();
            if( ErrorCode == SmartMediaError_NoError)
                return (0);
            else
                return (-1);

        case DEVCTL_REPORT_REMOVE:
            pdr->drive_flags &= ~DRIVE_FLAGS_INSERTED;
            return(0);

        case DEVCTL_CHECKSTATUS:
            if (!(pdr->drive_flags & DRIVE_FLAGS_REMOVABLE))
                return(DEVTEST_NOCHANGE);
            if (pdr->drive_flags & DRIVE_FLAGS_INSERTED)
                return(DEVTEST_NOCHANGE);
            else if (!_Hw_ChkCardIn())
                return(DEVTEST_NOMEDIA);
            else if (SmartMedia_Initialize() != SmartMediaError_NoError)
                return(DEVTEST_UNKMEDIA);
            else
            {
                pdr->drive_flags |= DRIVE_FLAGS_INSERTED;
                return(DEVTEST_CHANGED);
            }

        case DEVCTL_WARMSTART:

            /* Removability is supported, but is not complete.  There needs to be a way
               for the adapter for the smartmedia to indicate removal.  When a removal
               event occurs, smartmedia_perform_device_ioctl() needs to be called with
               DEVCTL_REPORT_REMOVE as the opcode.  Without doing so, the current code
               will not notice media removals. */

            pdr->drive_flags |= DRIVE_FLAGS_VALID | DRIVE_FLAGS_REMOVABLE;
            smartmedia_driveno = driveno;

#if (SMARTMEDIA_EMULATION)
            if (sm_need_to_format())
            {
                pdr->drive_flags |= DRIVE_FLAGS_FORMAT;
            }
#endif

            if (SmartMedia_Initialize() == SmartMediaError_NoError)
            {
                pdr->drive_flags |= DRIVE_FLAGS_INSERTED;
            }

            return (0);

        case DEVCTL_POWER_RESTORE:
            /* Fall through */
        case DEVCTL_POWER_LOSS:
            /* Fall through */
        default:
            break;
    }
    return(0);
}


/* Call this function to indicate to the driver that the card has been removed. */
/* Could also call mmc_perform_device_ioctl() yourself, but this may be more convenient. */
void smartmedia_report_remove(void)
{
    if (smartmedia_driveno >= 0)
    {
        smartmedia_perform_device_ioctl(smartmedia_driveno, DEVCTL_REPORT_REMOVE, 0);
    }
}


#endif /* (INCLUDE_SMARTMEDIA) */
