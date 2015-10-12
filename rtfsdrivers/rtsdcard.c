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

int RtSdcard_Write_Block(int unit_number, unsigned long blockNum, unsigned char *pbuffer);
int RtSdcard_Read_Block(int unit_number, unsigned long blockNum, unsigned char *pbuffer);
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
//int ii;
	pFsInsertFn = pInsertFn;
	pFsReMoveFn = pReMoveFn;

	/* The driver just attached to us, generate a file system insertion event for all attached drivers */
//	for(ii =0 ;ii < MAX_MASS_STOR_CB; ii++)
//	{
//	IusbMassStorCb_t *pMassStorCb;
//	pMassStorCb = rtusb_unit_to_controlblock(ii);
//    	if(pMassStorCb)
//   			fsReportInsertionEvent(pMassStorCb->ulDevIdx);
//	}
//
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
		if (RtSdcard_Read_Block(unit_number, Sector, pData) < 0)
			return -1;
		Sector += 1;
		pData += SECTORSIZE(unit_number);
		nSectors -= 1;
	}
    return 0;
}



int rtsdcard_device_write(int unit_number, unsigned long Sector, unsigned long nSectors, unsigned char *pData)
{
	while (nSectors)
	{
		if (RtSdcard_Write_Block(unit_number, Sector, pData) < 0)
			return -1;
		Sector += 1;
		pData += SECTORSIZE(unit_number);
		nSectors -= 1;
	}
    return 0;
}
