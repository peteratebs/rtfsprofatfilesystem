/*---------------------------------------------------------------------------*/
#ifndef smartmedH
#define smartmedH
/*---------------------------------------------------------------------------*/
#include "smartapi.h"

/* Routines exported from smartmed.c   */
extern char SmartMedia_Initialize(void);
extern char SmartMedia_WriteSector(dword,word,unsigned char *);
extern char SmartMedia_ReadSector(dword,word,unsigned char *);
extern char SmartMedia_Check_Media(void);
extern char SmartMedia_Check_Parameter(word *,unsigned char *,unsigned char *);
extern char SmartMedia_EraseBlock(dword,word);
extern char SmartMedia_EraseAll(void);
extern char SmartMedia_Logical_Format(void);
extern char Media_OneSectWriteStart(dword,unsigned char *);
extern char Media_OneSectWriteNext(unsigned char *);
extern char Media_OneSectWriteFlush(void);

#endif
