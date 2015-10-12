
/*---------------------------------------------------------------------------*/
#ifndef smarthwH
#define smarthwH

/*
The simplest hardware interface is one input port	SMART_STAT
					     one output port	SMART_CTL
					     one bidir port	SMART_DATA
*/

#define IO_BASE         0x8000
#define SMART_STAT      IO_BASE+0x0001  // SmartMedia Status Port (Input)
#define SMART_CTL       IO_BASE+0x0002  // SmartMedia Control Bits (Output)
#define SMART_DATA      IO_BASE+0x0003  // SmartMedia Card Data Bus (Bidir)

// SMART_STAT Bit Map
#define bSM_R_B         0x01            // bit0 - SmartMedia Busy Flag
#define bSM_DET1        0x02            // bit1 - SmartMedia Det1 Flag
#define bSM_DET2        0x04            // bit2 - SmartMedia Det2 Flag
#define bSM_WRP         0x08            // bit3 - SmartMedia WrtP Flag

// SMART_CTL Bit Map
// Bit3 = ALE
// Bit2 = CLE
// Bit1 = ~WP
// Bit0 = ~CE

/***************************************************************************
SmartMedia Controller Definition
***************************************************************************/

#define SMARTMEDIA_EMULATION 1
#define SMARTMEDIA_EMULATION_ON_DISK 0 /* This is pretty slow */

#if (SMARTMEDIA_EMULATION)
BOOLEAN sm_need_to_format(void);
void sm_insert_or_remove(BOOLEAN inserted); /* To test SM removal */
byte input(int port);
void output(int port, byte value);
#else
/* Provided by the porting layer */
byte read_smartmedia_byte(dword bus_master_address);
void write_smartmedia_byte(dword bus_master_address, byte value);
#define input(ADDR) read_smartmedia_byte(ADDR)
#define output(ADDR,VAL) write_smartmedia_byte(ADDR, VAL)
#endif

#define  _Hw_InData()       input(SMART_DATA)
#define  _Hw_OutData(a)     output(SMART_DATA,a)

// These functions set mode signals WP, CE, CLE, ALE to prepare for various purposes
// Bit3 = ALE, Bit2 = CLE, Bit1 = ~WP, Bit0 = ~CE
#define  _Hw_SetRdData()    output(SMART_CTL, 0x00)
#define  _Hw_SetRdCmd()     output(SMART_CTL, 0x04)
#define  _Hw_SetRdAddr()    output(SMART_CTL, 0x08)
#define  _Hw_SetRdStandby() output(SMART_CTL, 0x01)
#define  _Hw_SetWrData()    output(SMART_CTL, 0x02)
#define  _Hw_SetWrCmd()     output(SMART_CTL, 0x06)
#define  _Hw_SetWrAddr()    output(SMART_CTL, 0x0a)
#define  _Hw_SetWrStandby() output(SMART_CTL, 0x01)

// Check input status of signals from SmartMedia card
// _Hw_ChkCardIn()    0 ==> No card, 1 ==> Card present
// _Hw_ChkStatus()    STATUS means  ~CardIn
// _Hw_ChkWP()        0 ==> WP disabled, 1 ==> WP enabled
// _Hw_ChkBusy()      0 ==> Not busy, 1 ==> Busy
#define  _Hw_ChkCardIn()    (~input(SMART_STAT) & (bSM_DET1 | bSM_DET2))
#define  _Hw_ChkStatus()    ( input(SMART_STAT) & (bSM_DET1 | bSM_DET2))
#define  _Hw_ChkWP()        (input(SMART_STAT) & bSM_WRP)
#define  _Hw_ChkBusy()      (input(SMART_STAT) & bSM_R_B)

void _Hw_InBuf(unsigned char *databuf, int count);
void _Hw_OutBuf(unsigned char *databuf, int count);
/***************************************************************************/
#endif
