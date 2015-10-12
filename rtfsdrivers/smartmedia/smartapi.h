/*---------------------------------------------------------------------------*/
#ifndef SmartApiH
#define SmartApiH
/*---------------------------------------------------------------------------*/
#include "smart.h"

#define SUCCESS      0 /* SUCCESS */
#define ERROR       -1 /* ERROR */
#define CORRECTABLE  1 /* CORRECTABLE */

#define SmartMediaError_NoError 0
#define SmartMediaError_NoSmartMedia 0x003A /* Medium Not Present */
#define SmartMediaError_WriteFault   0x0003 /* Peripheral Device Write Fault */
#define SmartMediaError_HwError      0x0004 /* Hardware Error */
#define SmartMediaError_DataStatus   0x0010 /* DataStatus Error */
#define SmartMediaError_EccReadErr   0x0011 /* Unrecovered Read Error */
#define SmartMediaError_CorReadErr   0x0018 /* Recovered Read Data with ECC */
#define SmartMediaError_OutOfLBA     0x0021 /* Illegal Logical Block Address */
#define SmartMediaError_WrtProtect   0x0027 /* Write Protected */
#define SmartMediaError_ChangedMedia 0x0028 /* Medium Changed */
#define SmartMediaError_UnknownMedia 0x0030 /* Incompatible Medium Installed */
#define SmartMediaError_IllegalFmt   0x0031 /* Medium Format Corrupted */

/***************************************************************************/
/* The following routines are exported from smartapi.c */
extern char Bit_Count(unsigned char);
extern char Bit_CountWord(word);
extern void StringCopy(char *, char *, int);
extern char StringCmp(char *, char *, int);

extern char Check_DataBlank(unsigned char *);
extern char Check_FailBlock(unsigned char *);
extern char Check_DataStatus(unsigned char *);
extern char Load_LogBlockAddr(unsigned char *);
extern void Clr_RedundantData(unsigned char *);
extern void Set_LogBlockAddr(unsigned char *);
extern void Set_FailBlock(unsigned char *);
extern void Set_DataStaus(unsigned char *);
extern void Ssfdc_Reset(void);
extern char Ssfdc_ReadCisSect(unsigned char *,unsigned char *);
extern void Ssfdc_WriteRedtMode(void);
extern void Ssfdc_ReadID(unsigned char *);
extern char Ssfdc_ReadSect(unsigned char *,unsigned char *);
extern char Ssfdc_WriteSect(unsigned char *,unsigned char *);
extern char Ssfdc_WriteSectForCopy(unsigned char *,unsigned char *);
extern char Ssfdc_EraseBlock(void);
extern char Ssfdc_ReadRedtData(unsigned char *);
extern char Ssfdc_WriteRedtData(unsigned char *);
extern char Ssfdc_CheckStatus(void);
extern char Set_SsfdcModel(unsigned char);
extern void Cnt_Reset(void);
extern char Check_CntPower(void);
extern char Check_CardExist(void);
extern char Check_CardStsChg(void);
extern char Check_SsfdcWP(void);
/*******************************************************/
extern char Check_ReadError(unsigned char *);
extern char Check_Correct(unsigned char *,unsigned char *);
extern char Check_CISdata(unsigned char *,unsigned char *);
extern void Set_RightECC(unsigned char *);
/***************************************************************************/
/* The following structs are declared in smartapi.c and used elsewhere  */
extern struct SSFDCTYPE Ssfdc;
extern struct ADDRESS Media;
extern struct CIS_AREA CisArea;
/***************************************************************************/
#endif
