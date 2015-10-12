#include "rtfs.h"

/*  “MSWIN4.1”, is the recommended setting, because it is the setting least likely to cause compatibility problems. */
KS_CONSTANT byte * pustring_sys_oemname            = (byte *) "MSWIN4.1";
KS_CONSTANT byte * pustring_sys_volume_label       = (byte *) "VOLUMELABEL";
KS_CONSTANT byte * pustring_sys_badlfn             = (byte *) "\\/:*?\"<>|";
KS_CONSTANT byte * pustring_sys_badalias           = (byte *) "\\/:*?\"<>| ,;=+[]";
KS_CONSTANT byte * pustring_sys_ucreserved_names   = (byte *) "CON,PRN,NUL,AUX,LPT1,LPT2,LPT3,LPT4,COM1,COM2,COM3,COM4";
KS_CONSTANT byte * pustring_sys_lcreserved_names   = (byte *) "con,prn,nul,aux,lpt1,lpt2,lpt3,lpt4,com1,com2,com3,com4";
