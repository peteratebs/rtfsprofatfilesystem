/***********************************************************************
 *   File        : rtsdcard.c
 *   Date        :
 *   Author      :
 *   Description :
 *        Contains file system independent entry points for MMC/SD card driver.
 *   Revision    : 0.0
 ***********************************************************************/


void rtsdcardReportRemovalEvent(int unit);
void rtsdcardReportInsertionEvent(int unit);
typedef void (* INSERTCBFN)(int unit);
typedef void (* REMOVECBFN)(int unit);


void rtsdcard_driver_attach(INSERTCBFN pInsertFn, REMOVECBFN pReMoveFn);
int rtsdcard_device_media_parms(int unit_number, unsigned long *nSectors, unsigned long *BytesPerSector, int *isReadOnly);
int rtsdcard_device_read(int unit_number, unsigned long Sector, unsigned long nSectors, unsigned char *pData);
int rtsdcard_device_write(int unit_number, unsigned long Sector, unsigned long nSectors, unsigned char *pData);

unsigned long RtSdcard_Write_Block(int unit_number, unsigned long blockNum, unsigned long blockCount, unsigned char *pbuffer);
unsigned long RtSdcard_Read_Block(int unit_number, unsigned long blockNum, unsigned long blockCount, unsigned char *pbuffer);
int RtSdcard_device_media_parms(int unit_number, unsigned long *nSectors, unsigned long *BytesPerSector, int *isReadOnly);

#define SECTORSIZE(U) 512

static INSERTCBFN pFsInsertFn;
static REMOVECBFN pFsReMoveFn;

void rtsdcardReportRemovalEvent(int unit)
{
    if (pFsReMoveFn)
        pFsReMoveFn(unit);
}
void rtsdcardReportInsertionEvent(int unit)
{
    if (pFsInsertFn)
        pFsInsertFn(unit);
}
void rtsdcard_driver_attach(INSERTCBFN pInsertFn, REMOVECBFN pReMoveFn)
{
    pFsInsertFn = pInsertFn;
    pFsReMoveFn = pReMoveFn;
}

int rtsdcard_device_media_parms(int unit_number, unsigned long *nSectors, unsigned long *BytesPerSector, int *isReadOnly)
{
    return RtSdcard_device_media_parms(unit_number, nSectors, BytesPerSector, isReadOnly);
}

int rtsdcard_device_open(int unit_number)
{
    if (RtSdcard_init(unit_number)==0)
    {
        rtp_printf("Force card insert \n");
        pFsInsertFn(unit_number);
        return 0;
    }
    else
        return -1;
}

int rtsdcard_device_read(int unit_number, unsigned long Sector, unsigned long nSectors, unsigned char *pData)
{
    while (nSectors)
    {
    unsigned long nread;
       nread=RtSdcard_Read_Block(unit_number, Sector,nSectors, pData);
       if (nread < 1)
          return -1;
       Sector += nread;
       pData += nread*SECTORSIZE(unit_number);
       nSectors -= nread;
    }
    return 0;
}


int rtsdcard_device_write(int unit_number, unsigned long Sector, unsigned long nSectors, unsigned char *pData)
{
    while (nSectors)
    {
        unsigned long nWritten;
        nWritten=RtSdcard_Write_Block(unit_number, Sector, nSectors, pData);
        if (nWritten < 1)
            return -1;
        Sector += nWritten;
        pData += nWritten*SECTORSIZE(unit_number);
        nSectors -= nWritten;
    }
    return 0;
}
