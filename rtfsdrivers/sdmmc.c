/*
 * SDMMC.c
 *
 *  Created on: Mar 29, 2011
 *      Author: Peter
 */
#include "sdmmc.h"
#include "lpc214x.h"
#include "rtpstr.h"
#include "rtpprint.h"

#if (ENABLE_DMA)
#include "dma.h"
#endif

#if(CONFIGURE_SDMMC_INTERRUPTS)
#include "irq.h"
#endif  //CONFIGURE_SDMMC_INTERRUPTS


#define DRIVE_SD_MAX_FREQ 		0
#define DEBUG_SDCARD_DRIVER		0
#define DEBUG_DUMP_DATA			0
#define SET_4_BIT				0
#define SDMMC_SUPPORT_STREAMING	0


#define MAXSDCARDS 1
#define LPC24XXMCI 1
#define MICROSPERLOOP 10

#define DWSWAP(X) X
#define DATA_TIMER_VALUE		0x10000  /* Check if I need to scale this down, or if it can be determined programatically from the CSD  */
#define SD_FIFO_HALF_FULL 		(1<<15)  //Need to rename to SD_RX_FIFO_HALF_FULL	 , or RX
#define SD_FIFO_FULL 			(1<<17)	 //Need to rename to SD_RX_FIFO_FULL		 , or RX
#define SD_FIFO_DATA_TIMEOUT	(1<<5)	 //Need to rename to SD_RX_FIFO_DATA_TIMEOUT   , or RX
#define SD_RX_DATA_AVAILABLE 	(1<<21)	 //Need to rename to SD_RX_DATA_AVAILABLE , or RX
#define SD_FIFO_EMPTY 			(1<<19)	 //Need to rename to SD_RX_FIFO_EMPTY	  , or RX
#define MA_DATA_TIMER_VALUE		0x80000000  //Check if I need to scale this down, or if it can be determined programatically from the CSD
#define TX_DATA_AVAILABLE		(1 << 21)
#define TX_FIFO_EMPTY 			(1 << 18)
#define TX_FIFO_FULL			(1 << 16)
#define TXFIFOHALFEMPTY			(1 << 14)
#define MCI_TXACTIVE			(1 << 12)


int WriteGotTo= 0;
static int RtSdcard_Driver_Initialized;
static SD_CARD_INFO mmc_sd_cards[MAXSDCARDS];
volatile int MCI_Block_End_Flag = 0;
unsigned long maxwloops = 100000000;
unsigned long mmcstatuses[10000];
int numstatuses;


static int RtSdcard_Controller_Init(void);
static int RtSdcard_Media_Init(SD_CARD_INFO *pmmc_sd_card);
static int RtSdcard_Stack_Init(void);
static void RtSdcard_Set_Clock(int Clock_rate );
static int RtSdcard_Check_CID( SD_CARD_INFO * mmc_sd_card);
static int RtSdcard_Send_CSD( SD_CARD_INFO * mmc_sd_card);
static int RtSdcard_Set_Address(SD_CARD_INFO * mmc_sd_card);
static int RtSdcard_Select_Card(SD_CARD_INFO * mmc_sd_card, int is_specific_card);
static void RtSdcard_Delay_micros(unsigned long delay);
static void RtSdcard_Display_Media_Type(SD_CARD_INFO *pmmc_sd_card);
static int RtSdcard_CardInit( SD_CARD_INFO * mmc_sd_card );
static int RtSdcard_Go_Idle_State( SD_CARD_INFO *pmmc_sd_card );
static int RtSdcard_Send_OP_Cond( SD_CARD_INFO *pmmc_sd_card );
static int RtSdcard_Send_ACMD_OP_Cond( SD_CARD_INFO *pmmc_sd_card );
static int RtSdcard_Send_ACMD(  SD_CARD_INFO *pmmc_sd_card );
static  int RtSdcard_Send_If_Cond( SD_CARD_INFO * mmc_sd_card );
static void parse_CID(SD_CARD_INFO * mmc_sd_card, unsigned long *respValue);
static void parse_CSD(SD_CARD_INFO * mmc_sd_card, unsigned long *respValue);
static void MCI_TXEnable( void );
static int RtSdcard_Send_Write_Block( SD_CARD_INFO * mmc_sd_card, unsigned long blockNum );
static void MCI_RXEnable( void );
static int RtSdcard_Switch_Voltage(SD_CARD_INFO *pmmc_sd_card);
static int RtSdcard_Send_ACMD_SCR(SD_CARD_INFO *pmmc_sd_card );
static int PolledWriteXfer(unsigned long *pBbuffer);
static int PolledReadXfer(unsigned char *pBbuffer);
unsigned long PVOPVO_T_splx(unsigned long level);
unsigned long PVOPVO_T_splhigh();

#if(SET_4_BIT)
static int RtSdcard_Send_ACMD_Bus_Width(SD_CARD_INFO *pmmc_sd_card, int bus_width);
static int RtSdcard_Set_BusWidth(SD_CARD_INFO *pmmc_sd_card, int buswidthflag); //MA
#endif

#if(SDMMC_SUPPORT_STREAMING)
static int RtSdcard_Send_Write_Multiple_Block( SD_CARD_INFO * mmc_sd_card, unsigned long blockNum); //MA
static int RtSdcard_Send_Read_Multiple_Block( SD_CARD_INFO * mmc_sd_card , unsigned long blockNum ); //MA
#endif

#if(CONFIGURE_SDMMC_INTERRUPTS)
int configure_sdmmcIsr(void);
void MCI_DataErrorProcess(void);
void MCI_DATA_END_InterruptService(void);

void MCI_TXDisable( void );

static int SD_CARD_UNIT_NUM=0;
#endif


#if (DEBUG_SDCARD_DRIVER&&DEBUG_DUMP_DATA)
void DumpSector(unsigned long sectornumber, char *message, unsigned char *Buffer);
#endif


int RtSdcard_init(int unitnumber)							/* Top level interface */
{
	if (unitnumber>=MAXSDCARDS)
		return -1;

	if (!RtSdcard_Driver_Initialized)
	{
		int i;
		rtp_memset(&mmc_sd_cards[0], 0, sizeof(mmc_sd_cards));
		for (i = 0; i < MAXSDCARDS; i++)
		{
			mmc_sd_cards[i].card_inserted= 0;
			mmc_sd_cards[i].card_type = CARD_UNKNOWN_TYPE;
			mmc_sd_cards[i].state = INACTIVE_STATE;
		}
		if (RtSdcard_Controller_Init()!=0) /* Target specific initialization (pinconfig, clocks etc) */
			return -1;
		if (RtSdcard_Stack_Init()!=0)  /* Standard device independent initialization */
			return -1;
		RtSdcard_Driver_Initialized = 1;
	}

#if(CONFIGURE_SDMMC_INTERRUPTS)
	configure_sdmmcIsr();
#endif
	if (RtSdcard_Media_Init(&mmc_sd_cards[unitnumber])!=0)  /* Standard device independent initialization */
		return -1;

	return 0;


}

int RtSdcard_device_media_parms(int unitnumber, unsigned long *nSectors, unsigned long *BytesPerSector, int *isReadOnly)
{
	if (unitnumber>=MAXSDCARDS)
		return -1;
	*isReadOnly = 0;
	*nSectors =	mmc_sd_cards[unitnumber].no_blocks;
	*BytesPerSector = mmc_sd_cards[unitnumber].bytes_per_block;
	return 0;
}


static int RtSdcard_Media_Init(SD_CARD_INFO *pmmc_sd_card)
{
	pmmc_sd_card->card_type = CARD_UNKNOWN_TYPE;
	pmmc_sd_card->card_inserted= 0;
	pmmc_sd_card->state = INACTIVE_STATE;

	if (RtSdcard_CardInit( pmmc_sd_card ) != 0)
		return -1;

	RtSdcard_Display_Media_Type(pmmc_sd_card);

	if( pmmc_sd_card->card_type == CARD_UNKNOWN_TYPE)
		return -1; //for now return, no card is inserted, or we don't support that card
	pmmc_sd_card->card_inserted =1;

	RtSdcard_Switch_Voltage(pmmc_sd_card); 		/* Switch to 1.8 volts if supported by the controller */

	/*We do this for each card we have, on the Olemix board we only have one SD card supported, if we have 
	 * more (i.e. have more than one CMD line for the SD interface, then we loop throught these, and issue 
	 * CMD2 followed by CMD3 for each card) */
	
	/* Send CMD2 */

	if (RtSdcard_Check_CID( pmmc_sd_card) != 0)		 /* Check Card information description, manufacturer et al. */
		goto CARD_INITIALIZATION_FAILED;

	if (RtSdcard_Set_Address(pmmc_sd_card) != 0)     /* Set card address for this device */
		goto CARD_INITIALIZATION_FAILED;

	if (RtSdcard_Send_CSD(pmmc_sd_card) != 0)       /* Check card specific data (operating range, capacity etc.) */
		goto CARD_INITIALIZATION_FAILED;

	if (RtSdcard_Select_Card(pmmc_sd_card, 1) != 0)  /* Switch card to xfer state */
		goto CARD_INITIALIZATION_FAILED;

	/* send SCR , can easily removed*/
	if (RtSdcard_Send_ACMD_SCR(pmmc_sd_card ) != 0)
		goto CARD_INITIALIZATION_FAILED;

	if (( pmmc_sd_card->card_type == SD_CARD_TYPE ) || ( pmmc_sd_card->card_type == SDHC_CARD_TYPE ) || ( pmmc_sd_card->card_type == SDXC_CARD_TYPE ))
	{
#if(DRIVE_SD_MAX_FREQ)
	  	RtSdcard_Set_Clock(NORMAL_RATE);
#endif //DRIVE_SD_MAX_FREQ

#if(SET_4_BIT)
		if (RtSdcard_Set_BusWidth( pmmc_sd_card, SD_4_BIT ) != 0)
			goto CARD_INITIALIZATION_FAILED; 		/* fatal error */
#endif //SET_4_BIT
	}

#if(CONFIGURE_SDMMC_INTERRUPTS)
//#if(0)
	if(_rtp_sig_semaphore_alloc (pmmc_sd_card->sdmmc_rw_sm, "sdmmc_rw_sm") <0)
	{
		printf("creating SDMMC semaphore failed");
		goto CARD_INITIALIZATION_FAILED; 		/* fatal error */
	}


#endif


	return 0;
CARD_INITIALIZATION_FAILED:
	pmmc_sd_card->card_type = CARD_UNKNOWN_TYPE;
	pmmc_sd_card->card_inserted= 0;
	pmmc_sd_card->state = INACTIVE_STATE;
	return -1;
}


static void RtSdcard_Delay_micros(unsigned long delay)
{
int i;
	for (i = 0; i < delay * MICROSPERLOOP; i++)
		;
}

static int RtSdcard_Stack_Init(void)
{
	return 0;
}

static int RtSdcard_Controller_Init(void)
{
#if (LPC24XXMCI)
	/* Power the PCONP register */
	/* On Reset, the SD/MMC is disabled PCMCI=0 in PCONP */

	PCONP |= 0x10000000; 		/* (1<<28) */

	PCLKSEL1 &= 0xFCFFFFFF ;	/* With this setting the PCLK_MCI = 72000000/4 == 18000000 , less than the 20~25 MHz requirement
								 * well, we will stay here, until I find a Cdivider/mulitplier,   and see the req.
								 * Bits 25:24 Control the Peripheral Clock Selection for MCI?? should be something like 20~25 MH
								 * (20 MHz max for MMC, and up to 25 MHz for SD), for * now set it to PCLK_PCMCI=CCLK/4 ,
								 * where bits 25:24 == 00 */
	 
	PINSEL2 |= 0x20 ;  			/* Set GPIO Port 1.2 to MCICLK   */
	PINSEL2 |= 0x80 ;  			/* Set GPIO Port 1.3 to MCICMD   */
	PINSEL2 |= 0x800;  			/* Set GPIO Port 1.5 to MCIPWR   */
	PINSEL2 |= 0x2000;  		/* Set GPIO Port 1.6 to MCIDAT0  */
	PINSEL2 |= 0x8000;  		/* Set GPIO Port 1.7 to MCIDAT1  */
	PINSEL2 |= 0x800000;		/* Set GPIO Port 1.11 to MCIDAT2 */
	PINSEL2 |= 0x2000000;		/* Set GPIO Port 1.12 to MCIDAT3 */

	PINMODE2 |= 0x280A8A0; 		/* MCI/SD Pins have neither pull-up nor pull down resistor, the value is
								 * 00000010100000001010100010100000 */

	SCS |= 0x08; 				/* SCS register bit 3 corresponds to MCIPWR 0 the MCIPWR pin is low. 1 The MCIPWR pin is high */

	if(MCIClock & 0x100)
		MCIClock &= ~(1 << 8);

	if(MCIPower & 0x2)
		MCIPower =0x00;

	MCIPower |= 0x02;
	while ( !(MCIPower & 0x02) );		 /* PVOPVO - Endless */
	RtSdcard_Delay_micros(10000); 		 /* When the external  power supply is switched on, the software first enters the power-up phase,
										  * and wait until the supply output is stable before moving to the power-on phase. */

	MCIPower |= 0x01;					/* bit 1 is set already, from power up to power on */

	RtSdcard_Delay_micros(100000); 		/* When the external  power supply is switched on, the software first enters the power-up phase,
										 * and wait until the supply output is stable before moving to the power-on phase. */

	MCIMask0 = 0x00 ; 					/* Mask the interrupts Disable all interrupts for now */
	MCIClear = 0x7FF; 					/* Write 0B11111111111 to the MCI Clear register to clear the status flags */

	/* Pins are selected and power is applied, interrupts are disabled */

	/* Set the initial clock rate, we'll raise it later */
	RtSdcard_Set_Clock(SLOW_RATE);

	return 0;
#else
#error
#endif
}

static void RtSdcard_Set_Clock(int Clock_rate )
{
#if (LPC24XXMCI)
	unsigned long ClkValue = 0;

	if(Clock_rate == SLOW_RATE)
		ClkValue |= MCLKDIV_SLOW; /* Slow Clock */
	else if(Clock_rate == NORMAL_RATE)
		ClkValue |= MCLKDIV_NORMAL;

	MCIClock &=~(0xFF); 				/* Clear the clock divider */
	MCIClock |= (1<<8) |ClkValue;
	RtSdcard_Delay_micros(1000);  		/* Delay 3MCLK + 2PCLK before next write */
#else
#error
#endif
}

static void RtSdcard_Display_Media_Type(SD_CARD_INFO *pmmc_sd_card)
{
#if(DEBUG_SDCARD_DRIVER)
	if( pmmc_sd_card->card_type == CARD_UNKNOWN_TYPE)
	{
		rtp_printf("Card Type is Unknown, or no card is inserted... need to check how to check for inserted card \n \r");
		return ; 						/* for now return, no card is inserted, or we don't support that card */
	}
	pmmc_sd_card->card_inserted =1;
	
	if(pmmc_sd_card->card_type == MMC_CARD_TYPE )
	{
		rtp_printf("An MMC card is inserted \n \r");
	}
	else
	{
		if(pmmc_sd_card->card_type  == SD_CARD_TYPE )
			rtp_printf("An Standard SD card is inserted \n \r");
		else
		{
			if(( pmmc_sd_card->card_type  == SDHC_CARD_TYPE )||( pmmc_sd_card->card_type == SDXC_CARD_TYPE ))
			{
				rtp_printf("An Standard SDHC || SDXC card is inserted \n \r");
			}
		}
	}
#endif
}

#if(SET_4_BIT)
/******************************************************************************
** Function name:		RtSdcard_Set_BusWidth
**
** Descriptions:		1-bit bus or 4-bit bus.
**
** parameters:			bus width
** Returned value:		TRUE or FALSE
**
******************************************************************************/
static int RtSdcard_Set_BusWidth(SD_CARD_INFO *pmmc_sd_card, int width )
{
	int width_bus =0;
#if (LPC24XXMCI)
	RtSdcard_Delay_micros(100);
	if ( width == SD_1_BIT )
	{
		MCIClock &=  ~(1 << 11);	/* 1 bit bus */
	}
	else if ( width == SD_4_BIT )
	{
		MCIClock |=  (1 << 11);		/* 4 bit bus */
		width_bus = 2; 				/* binary 10*/
		return RtSdcard_Send_ACMD_Bus_Width(pmmc_sd_card, width_bus );
	}
#else
#error
#endif
	return 0;
}
#endif


/******************************************************************************
** Function name:		RtSdcard_CardInit
**
** Descriptions:
**
** parameters:
** Returned value:
**
******************************************************************************/

static int RtSdcard_CardInit( SD_CARD_INFO * pmmc_sd_card )
{
  if (RtSdcard_Go_Idle_State(pmmc_sd_card) != 0)
	return -1;
#if (LPC24XXMCI)
  MCIPower |= (1 << 6 );		/* Set Open Drain output control for MMC */
  RtSdcard_Delay_micros(3000);
#else
#error
#endif

  /* Try CMD1 first for MMC, if it's timeout, try CMD55 and CMD41 for SD, if both failed, initialization faliure, bailout. */

#if(SUPPORT_SDHC_SDXC)
  /*MCI_SendCmd( SEND_OP_COND, OCR_INDEX, EXPECT_SHORT_RESP, 0 ); */
  if(RtSdcard_Send_If_Cond(pmmc_sd_card) == 0 )////SDXC condition
  {
	  RtSdcard_Delay_micros(30000);
	  pmmc_sd_card->card_type = SDHC_CARD_TYPE;
	  return RtSdcard_Send_ACMD_OP_Cond( pmmc_sd_card);
  }
  if (RtSdcard_Go_Idle_State(pmmc_sd_card) == -1)
	  return -1;
#endif /* SUPPORT_SDHC_SDXC */
  if ( RtSdcard_Send_OP_Cond(pmmc_sd_card) == 0)
  {
	  pmmc_sd_card->card_type = MMC_CARD_TYPE;
	  return 0;	/* Found the card, it's a MMC */
  }
  else
  {
	  if ( RtSdcard_Send_ACMD_OP_Cond(pmmc_sd_card) == 0)
	  {
	  	pmmc_sd_card->card_type = SD_CARD_TYPE;
	  	return 0;	/* Found the card, it's a SD */
	  }
  }
  return -1;
}

/******************************************************************************
** Function name:		RtSdcard_SendCmd
**
** Descriptions:
**
** parameters:
** Returned value:
**
******************************************************************************/

static int RtSdcard_SendCmd( unsigned long  CmdIndex, unsigned long  Argument, int  ExpectResp, int  AllowTimeout )
{
#if (LPC24XXMCI)
unsigned long  CmdData = 0;
unsigned long  CmdStatus;

  /* the command engine must be disabled when we modify the argument
  or the peripheral resends */

  while ( (CmdStatus = MCIStatus) & MCI_CMD_ACTIVE )	/* Command in progress. */
  {
	MCICommand = 0;										/* PVOPVO endless */
	MCIClear = CmdStatus | MCI_CMD_ACTIVE;
  }
  RtSdcard_Delay_micros(100);

  /*set the command details, the CmdIndex should 0 through 0x3F only */
  CmdData |= (CmdIndex & 0x3F);							/* bit 0 through 5 only */
  if ( ExpectResp == EXPECT_NO_RESP )					/* no response */
  {
	CmdData &= ~((1 << 6) | (1 << 7));					/* Clear long response bit as well */
  }
  else if ( ExpectResp == EXPECT_SHORT_RESP )			/* expect short response */
  {
	CmdData |= (1 << 6);
  }
  else if ( ExpectResp == EXPECT_LONG_RESP )			/* expect long response */
  {
	CmdData |= (1 << 6) | (1 << 7);
  }

  if ( AllowTimeout )									/* allow timeout or not */
  {
	CmdData |= (1 << 8);
  }
  else
  {
	CmdData &= ~(1 << 8);
  }

  /*send the command*/
  CmdData |= (1 << 10);									/* This bit needs to be set last. */
  MCIArgument = Argument;								/* Set the argument first, finally command */
  MCICommand = CmdData;

  return 0;
#else
#error
#endif
}

static int RtSdcard_GetCmdResp( int ExpectCmdData, int ExpectResp, unsigned long * CmdResp )
{
int CmdRespStatus = 0;
int LastCmdIndex;

  if ( ExpectResp == EXPECT_NO_RESP )
	return ( 0 );
#if (LPC24XXMCI)
  while ( 1 )
  {	// PVOPVO endless
	CmdRespStatus = MCIStatus;
	if ( CmdRespStatus & MCI_CMD_TIMEOUT)
	{
		MCIClear = CmdRespStatus | MCI_CMD_TIMEOUT;
		MCICommand = 0;
		MCIArgument = 0xFFFFFFFF;
	    return -1;
	}
	if (CmdRespStatus & MCI_CMD_CRC_FAIL )
	{
	   MCIClear = CmdRespStatus | MCI_CMD_CRC_FAIL;
	   LastCmdIndex = MCICommand & 0x003F;
	   if ( (LastCmdIndex == SEND_OP_COND) || (LastCmdIndex == SEND_APP_OP_COND) || (LastCmdIndex == STOP_TRANSMISSION) )
	   {
		  MCICommand = 0;
		  MCIArgument = 0xFFFFFFFF;
		  break;							/* ignore CRC error if it's a resp for SEND_OP_COND or STOP_TRANSMISSION. */
	  }
	  else
	  {
		return -1;
	  }
	}
	else if ( CmdRespStatus & MCI_CMD_RESP_END )
	{
		MCIClear = CmdRespStatus | MCI_CMD_RESP_END;
	    break;								/* cmd response is received, expecting response */
	}
  }

  if ( (MCIRespCmd & 0x3F) != ExpectCmdData )
  {
	/* If the response is not R1, in the response field, the Expected Cmd data
	won't be the same as the CMD data in SendCmd(). Below four cmds have
	R2 or R3 response. We don't need to check if MCI_RESP_CMD is the same
	as the Expected or not. */
	if ( (ExpectCmdData != SEND_OP_COND) && (ExpectCmdData != SEND_APP_OP_COND)	&& (ExpectCmdData != ALL_SEND_CID) && (ExpectCmdData != SEND_CSD) )
	{
		/* PVOPVO - Analyze  */
	  return -1;
	}
  }

  if ( ExpectResp == EXPECT_SHORT_RESP )
  {
	*CmdResp = MCIResponse0;
  }
  else if ( ExpectResp == EXPECT_LONG_RESP )
  {
	*CmdResp = MCIResponse0;
	*(CmdResp+1) = MCIResponse1;
	*(CmdResp+2) = MCIResponse2;
	*(CmdResp+3) = MCIResponse3;
  }
  return ( 0 );					/* Read MCI_RESP0 register assuming it's not long response. */
#else
#error
#endif
}


static int RtSdcard_Go_Idle_State( SD_CARD_INFO *pmmc_sd_card  )
{
int retryCount;
unsigned long respStatus;
unsigned long  respValue[4];

  retryCount = 0x20;
  while ( retryCount > 0 )
  {
	/* Send CMD0 command repeatedly until the response is back correctly */
    RtSdcard_SendCmd( GO_IDLE_STATE, 0x00000000, EXPECT_NO_RESP, 0 );
	respStatus = RtSdcard_GetCmdResp(GO_IDLE_STATE, EXPECT_NO_RESP, &respValue[0] );
	if (respStatus == 0 )
	{
		pmmc_sd_card->state = IDLE_STATE;
		return 0;
	}
	retryCount--;
  }
  return -1;
}


static int RtSdcard_Send_OP_Cond( SD_CARD_INFO *pmmc_sd_card )
{
int retryCount;
int respStatus;
unsigned long respValue[4];

  retryCount = 0x200;			/* reset retry counter */
  while ( retryCount > 0 )
  {
	/* Send CMD1 command repeatedly until the response is back correctly */
	RtSdcard_SendCmd( SEND_OP_COND, OCR_INDEX, EXPECT_SHORT_RESP, 0 );
	respStatus = RtSdcard_GetCmdResp( SEND_OP_COND, EXPECT_SHORT_RESP, &respValue[0] );
	/* bit 0 and bit 2 must be zero, or it's timeout or CRC error */
	if ( (respStatus == 0) && (respValue[0] & 0x80000000) )
	  return  0;	/* response is back and correct. */
	RtSdcard_Delay_micros(100);
	retryCount--;
  }
  return -1;
}


static int RtSdcard_Send_ACMD_OP_Cond( SD_CARD_INFO *pmmc_sd_card )
{
int retryCount;
int respStatus;
unsigned long respValue[4];

  /* timeout on SEND_OP_COND command on MMC, now, try SEND_APP_OP_COND
  command to SD */
  retryCount = 0x200;			/* reset retry counter */
  while ( retryCount > 0 )
  {
	RtSdcard_Delay_micros(3000);

	if ( RtSdcard_Send_ACMD(pmmc_sd_card ) == -1)//??
	{
		retryCount--;
	    continue;
  	}

	/* Send ACMD41 command repeatedly until the response is back correctly */
	RtSdcard_SendCmd( SEND_APP_OP_COND, OCR_INDEX_SDHC_SDHX, EXPECT_SHORT_RESP, 0 );

	respStatus = RtSdcard_GetCmdResp( SEND_APP_OP_COND, EXPECT_SHORT_RESP, &respValue[0] );

	if ( (respStatus == 0) && (respValue[0] & 0x80000000) ) /* TODO:Check the response of SD, SDHC and SHXC and compare against the return value for each, SDHC and SDXC will have the CCS bit set, 
															   as well as the bit 31 */
	{
		pmmc_sd_card->state = READY_STATE;
		return 0;	/* response is back and correct. */
	}
	RtSdcard_Delay_micros(200);
	retryCount--;
  }
  return -1;
}

/******************************************************************************
** Function name:		RtSdcard_Send_ACMD
**
** Descriptions:
**
** parameters:
** Returned value:
**
******************************************************************************/

static int RtSdcard_Send_ACMD(  SD_CARD_INFO *pmmc_sd_card )
{
int retryCount;
unsigned long CmdArgument;
int respStatus;
unsigned long respValue[4];

  if ( (pmmc_sd_card->card_type ==  SD_CARD_TYPE) || (pmmc_sd_card->card_type == SDHC_CARD_TYPE) || (pmmc_sd_card->card_type == SDXC_CARD_TYPE) )
  {
	CmdArgument = pmmc_sd_card->RCA;	/* Use the address from SET_RELATIVE_ADDR cmd */
  }
  else			/* if MMC or unknown card type, use 0x0. */
  {
	CmdArgument = 0x00000000;
  }

  retryCount = 20;
  while ( retryCount > 0 )
  {
	/* Send CMD55 command followed by an ACMD */
	RtSdcard_SendCmd( APP_CMD, CmdArgument, EXPECT_SHORT_RESP, 0 );
	respStatus = RtSdcard_GetCmdResp( APP_CMD, EXPECT_SHORT_RESP, &respValue[0] );
	if ( respStatus==0 && (respValue[0] & CARD_STATUS_ACMD_ENABLE) )	/* Check if APP_CMD enabled */
	  return 0;
	RtSdcard_Delay_micros(200);
	retryCount--;
  }
  return -1;
}

#if(SET_4_BIT)
/******************************************************************************
** Function name:		RtSdcard_Send_ACMD_Bus_Width
**
** Descriptions:		ACMD6, SET_BUS_WIDTH, if it's SD card, we can
**						use the 4-bit bus instead of 1-bit. This cmd
**						can only be called during TRANS state.
**						Since it's a ACMD, CMD55 APP_CMD needs to be
**						sent out first.
**
** parameters:			Bus width value, 1-bit is 0, 4-bit is 10
** Returned value:		true or false, true if the card is still in the
**						TRANS state after the cmd.
**
******************************************************************************/

static int RtSdcard_Send_ACMD_Bus_Width(SD_CARD_INFO *pmmc_sd_card, int buswidth )
{
int retryCount;
unsigned long respStatus;
unsigned long  respValue[4];

	retryCount = 0x20;			/* reset retry counter */
	while ( retryCount > 0 )
	{
		if ( RtSdcard_Send_ACMD(pmmc_sd_card) == -1 )
			continue;
		/* Send ACMD6 command to set the bus width */
		RtSdcard_SendCmd( SET_ACMD_BUS_WIDTH, buswidth, EXPECT_SHORT_RESP, 0 );
		respStatus = RtSdcard_GetCmdResp( SET_ACMD_BUS_WIDTH, EXPECT_SHORT_RESP, &respValue[0] );
		if ( respStatus==0 && ((respValue[0] & (0x0F << 8)) == 0x0900) )
			return 0;	/* response is back and correct. */
		RtSdcard_Delay_micros(200);
		retryCount--;
	}
	return -1;
}
#endif


/*****************************************************************************
** Function name:		RtSdcard_Send_Status
**
** Descriptions:		CMD13, SEND_STATUS, the most important cmd to
**						debug the state machine of the card.
**
** parameters:			None
** Returned value:		Response value(card status), true if the ready bit
**						is set in the card status register, if timeout, return
**						INVALID_RESPONSE 0xFFFFFFFF.
**
******************************************************************************/
/* CMD13 */
static unsigned long RtSdcard_Send_Status( SD_CARD_INFO * mmc_sd_card )
{
int retryCount;
int respStatus;
unsigned long respValue[4];
unsigned long CmdArgument;

	if (( mmc_sd_card->card_type == SD_CARD_TYPE ) || ( mmc_sd_card->card_type == SDHC_CARD_TYPE ) || ( mmc_sd_card->card_type == SDXC_CARD_TYPE ))
		CmdArgument = mmc_sd_card->RCA;
	else			/* if MMC or unknown card type, use default RCA addr. */
		CmdArgument = 0x00010000;

  /* Note that, since it's called after the block write and read, this timeout
  is important based on the clock you set for the data communication. */
  retryCount = 0x2000;
  while ( retryCount > 0 )
  {
#if (LPC24XXMCI)
	/* Send SELECT_CARD command before read and write */
	 MCIClear |= (MCI_CMD_TIMEOUT | MCI_CMD_CRC_FAIL | MCI_CMD_RESP_END);
#else
#error
#endif
	RtSdcard_SendCmd( SEND_STATUS, CmdArgument, EXPECT_SHORT_RESP, 0 );
	respStatus = RtSdcard_GetCmdResp( SEND_STATUS, EXPECT_SHORT_RESP, &respValue[0] );
	if ( respStatus==0 && (respValue[0] & (1 << 8)) )
	{ /* The ready bit should be set, it should be in either TRAN or RCV state now */
	  return ( respValue[0] );
	}
	RtSdcard_Delay_micros(200);
	retryCount--;
  }
  return ( INVALID_RESPONSE );
}


/*****************************************************************************
** Function name:		RtSdcard_Check_CID
**
** Descriptions:
**
** parameters:
** Returned value:
**
******************************************************************************/


static int RtSdcard_Check_CID( SD_CARD_INFO * mmc_sd_card)
{
int retryCount;
int respStatus;
unsigned long respValue[4];

  /* This command is normally after CMD1(MMC) or ACMD41(SD) or CMD11 if board supports 1.8V signaling and SDHC/SDXC */
  retryCount = 0x20;			/* reset retry counter */
  while ( retryCount > 0 )
  {
	/* Send CMD2 command repeatedly until the response is back correctly */
	RtSdcard_SendCmd( ALL_SEND_CID, 0, EXPECT_LONG_RESP, 0 );
	respStatus = RtSdcard_GetCmdResp( ALL_SEND_CID, EXPECT_LONG_RESP, &respValue[0] );
	/* bit 0 and bit 2 must be zero, or it's timeout or CRC error */
	if ( respStatus == 0)
	{
	  parse_CID(mmc_sd_card, &respValue[0]);
	  mmc_sd_card->state = IDENTIFICATION_STATE;
	  return 0;	/* response is back and correct. */
	}
	RtSdcard_Delay_micros(200);
	retryCount--;
  }
  return -1;
}

/******************************************************************************
** Function name:		RtSdcard_Set_Address
**
** Descriptions:		Send CMD3, STE_RELATIVE_ADDR, should after CMD2
**
** parameters:
** Returned value:		TRUE if response is back before timeout.
**
******************************************************************************/
static int RtSdcard_Set_Address( SD_CARD_INFO * mmc_sd_card )
{
int retryCount;
int respStatus;
unsigned long respValue[4];
unsigned long CmdArgument;

  /* If it's a SD card, SET_RELATIVE_ADDR is to get the address
  from the card and use this value in RCA, if it's a MMC, set default
  RCA addr. 0x00010000. */
	if (( mmc_sd_card->card_type == SD_CARD_TYPE ) || ( mmc_sd_card->card_type == SDHC_CARD_TYPE ) || ( mmc_sd_card->card_type == SDXC_CARD_TYPE ))
	{
		//CmdArgument = 0;
		CmdArgument = mmc_sd_card->RCA;
	}
	else			/* If it's unknown or MMC_CARD, fix the RCA address */
	{
		CmdArgument = 0x00010000;
	}
	retryCount = 0x20;			/* reset retry counter */
	while ( retryCount > 0 )
	{
		/* Send CMD3 command repeatedly until the response is back correctly */
		RtSdcard_SendCmd( SET_RELATIVE_ADDR, CmdArgument, EXPECT_SHORT_RESP, 0 );
		respStatus = RtSdcard_GetCmdResp( SET_RELATIVE_ADDR, EXPECT_SHORT_RESP, &respValue[0] );
		/* bit 0 and bit 2 must be zero, or it's timeout or CRC error */
		/* It should go to IDEN state and bit 8 should be 1 */
		/* The second condition ((respValue[0] & (0x0F << 8)) == 0x0700) ) is added if we alreay issued CMD3 and then reissued it to the same card, 
		 * it should be ok, the only difference the first time we issue CMD3 we were in iden state and we go to stdby, second case we are in stdby 
		 * and stay @ stdby */
		if ( respStatus ==0 && (((respValue[0] & (0x0F << 8)) == 0x0500) || ((respValue[0] & (0x0F << 8)) == 0x0700) ) ) 
		{
			mmc_sd_card->RCA = respValue[0] & 0xffff0000;
#if(DEBUG_SDCARD_DRIVER)
			rtp_printf("RCA == %X\n", mmc_sd_card->RCA);
#endif
			if(mmc_sd_card->state == IDENTIFICATION_STATE || (mmc_sd_card->state == STANDBY_STATE) )
				mmc_sd_card->state = STANDBY_STATE;
			return 0;	/* response is back and correct. */
		}
		RtSdcard_Delay_micros(200);
		retryCount--;
	}
	return -1;
}


/* CMD9 */

/******************************************************************************
** Function name:		RtSdcard_Send_CSD
**
** Descriptions:
**
** parameters:
** Returned value:
**
******************************************************************************/

static int RtSdcard_Send_CSD( SD_CARD_INFO * mmc_sd_card )
{
int retryCount;
int respStatus;
unsigned long respValue[4];
unsigned long CmdArgument;

	if (( mmc_sd_card->card_type == SD_CARD_TYPE ) || ( mmc_sd_card->card_type == SDHC_CARD_TYPE ) || ( mmc_sd_card->card_type == SDXC_CARD_TYPE ))
		CmdArgument = mmc_sd_card->RCA;
	else			/* if MMC or unknown card type, use default RCA addr. */
		CmdArgument = 0x00010000;

	retryCount = 0x20;
	while ( retryCount > 0 )
	{
		/* Send SET_BLOCK_LEN command before read and write */
#if (LPC24XXMCI)
	/* Send SELECT_CARD command before read and write */
	MCIClear |= (MCI_CMD_TIMEOUT | MCI_CMD_CRC_FAIL | MCI_CMD_RESP_END);
#else
#error
#endif
		RtSdcard_SendCmd( SEND_CSD, CmdArgument, EXPECT_LONG_RESP, 0 );
		respStatus = RtSdcard_GetCmdResp( SEND_CSD, EXPECT_LONG_RESP, &respValue[0] );
		if ( !respStatus )
		{
			parse_CSD(mmc_sd_card, &respValue[0]);
			return 0;
		}
		RtSdcard_Delay_micros(200);
		retryCount--;
	}
	return -1;
}



/* CMD7 */
/******************************************************************************
** Function name:		RtSdcard_Select_Card
**
** Descriptions:
**
** parameters:
** Returned value:
**
******************************************************************************/

static int RtSdcard_Select_Card( SD_CARD_INFO * mmc_sd_card , int is_specific_card)
{
int retryCount;
int respStatus;
unsigned long respValue[4];
int CmdArgument;

	if(is_specific_card)
	{
		if ( ( mmc_sd_card->card_type == SD_CARD_TYPE ) || ( mmc_sd_card->card_type == SDHC_CARD_TYPE ) || ( mmc_sd_card->card_type == SDXC_CARD_TYPE ) )
		{
			CmdArgument = mmc_sd_card->RCA;
		}
		else			/* if MMC or unknown card type, use default RCA addr. */
		{
			CmdArgument = 0x00010000;
		}
	}
	else
		CmdArgument =0;

  retryCount = 0x20;
  while ( retryCount > 0 )
  {
	/* Send SELECT_CARD command before read and write */
#if (LPC24XXMCI)
	/* Send SELECT_CARD command before read and write */
	MCIClear |= (MCI_CMD_TIMEOUT | MCI_CMD_CRC_FAIL | MCI_CMD_RESP_END);
#else
#error
#endif
	  RtSdcard_SendCmd( SELECT_CARD, CmdArgument, EXPECT_SHORT_RESP, 0 );
	  respStatus = RtSdcard_GetCmdResp( SELECT_CARD, EXPECT_SHORT_RESP, &respValue[0] );
	  if ( (respStatus==0) && ((respValue[0] & (0x0F << 8)) == 0x0700 ))
  	  {						/* Should be in STANDBY state now and ready */
		  if(is_specific_card)
		  {
			  	 if(mmc_sd_card->state == STANDBY_STATE)
			  		mmc_sd_card->state = TRANSFER_STATE;
			  	 else{
			  		 if(mmc_sd_card->state == DISCONNECT_STATE)
			  			 mmc_sd_card->state = PROGRAMMING_STATE;
			  	 }
		  }
		  else //!is_specific_card
		  {
			  if(mmc_sd_card->state == PROGRAMMING_STATE)
				  mmc_sd_card->state = DISCONNECT_STATE;
		  }
		  return 0;
  	  }
	  RtSdcard_Delay_micros(200);
	  retryCount--;
  	 }
  return -1;
}

/******************************************************************************
** Function name:		RtSdcard_Send_Stop
**
** Descriptions:		CMD12, STOP_TRANSMISSION. if that happens, the card is
**						maybe in a unknown state that need a warm reset.
**
** parameters:
** Returned value:		true or false, true if, at least, the card status
**						shows ready bit is set.
**
******************************************************************************/
/* CMD12 MCI SEND_STOP */
static int RtSdcard_Send_Stop( SD_CARD_INFO * mmc_sd_card )
{
int retryCount;
unsigned long respStatus;
unsigned long respValue[4];

	retryCount = 0x20;
	while ( retryCount > 0 )
	{
	  MCIClear = 0x7FF;
	  RtSdcard_SendCmd( STOP_TRANSMISSION, 0x00000000, EXPECT_SHORT_RESP, 0 );
	  respStatus = RtSdcard_GetCmdResp( STOP_TRANSMISSION, EXPECT_SHORT_RESP, &respValue[0] );
	  /* ready bit, bit 8, should be set in the card status register */
	  if ( !respStatus && (respValue[0] & (1 << 8)) )
		  return 0;
	  RtSdcard_Delay_micros(200);
	  retryCount--;
	}
	return -1;
}


/******************************************************************************
** Function name:		RtSdcard_Send_Read_Block
**
** Descriptions:		CMD17, READ_SINGLE_BLOCK, send this cmd in the TRANS
**						state to read a block of data from the card.
**
** parameters:			,block number
** Returned value:		Response value
**
******************************************************************************/
/* CMD17 READ_SINGLE_BLOCK */
static int RtSdcard_Send_Read_Block( SD_CARD_INFO * mmc_sd_card , unsigned long blockNum, unsigned long blk_len )
{
int retryCount;
int respStatus;
unsigned long respValue[4];
unsigned long block_arg;
	/* HC and XC cards use sector orietend addressing, early versions use bytes */
	if ( ( mmc_sd_card->card_type == SDHC_CARD_TYPE ) ||( mmc_sd_card->card_type == SDXC_CARD_TYPE ) )
			block_arg = blockNum;
	else /* if ( mmc_sd_card->card_type == SD_CARD_TYPE ) */
		block_arg = blockNum * BLOCK_LENGTH;
	retryCount = 0x20;
	while ( retryCount > 0 )
	{
		MCIClear = 0x7FF;
		RtSdcard_SendCmd( READ_SINGLE_BLOCK, block_arg, EXPECT_SHORT_RESP, 0 );
		respStatus = RtSdcard_GetCmdResp( READ_SINGLE_BLOCK, EXPECT_SHORT_RESP, &respValue[0] );
		/* it should be in the transfer state, bit 9~12 is 0x0100 and bit 8 is 1 */
		if ( respStatus==0 && ((respValue[0] & (0x0F << 8)) == 0x0900) )
		{
			return 0;			/* ready and in TRAN state */
			// PVOPVO - Do state transition
		}
		RtSdcard_Delay_micros(200);
		retryCount--;
	}
	return -1;					/* Fatal error */
}

/* CMD 24 */
static int RtSdcard_Send_Write_Block( SD_CARD_INFO * mmc_sd_card, unsigned long blockNum)
{
int retryCount;
int respStatus;
unsigned long respValue[4];
unsigned long block_arg;
	/* HC and XC cards use sector orietend addressing, early versions use bytes */
	if ( ( mmc_sd_card->card_type == SDHC_CARD_TYPE ) ||( mmc_sd_card->card_type == SDXC_CARD_TYPE ) )
			block_arg = blockNum;
	else /* if ( mmc_sd_card->card_type == SD_CARD_TYPE ) */
		block_arg = blockNum * BLOCK_LENGTH;
  retryCount = 0x20;
  while ( retryCount > 0 )
  {
	  MCIClear = 0x7FF;
	  RtSdcard_SendCmd( WRITE_BLOCK, block_arg, EXPECT_SHORT_RESP, 0 );
	  respStatus = RtSdcard_GetCmdResp( WRITE_BLOCK, EXPECT_SHORT_RESP, &respValue[0] );
	/* it should be in the transfer state, bit 9~12 is 0x0100 and bit 8 is 1 */
	if ( !respStatus && ((respValue[0] & (0x0F << 8)) == 0x0900) )
	{
	  return 0;			/* ready and in TRAN state */
	  // PVOPVO - Do state transition
	}

	RtSdcard_Delay_micros(200);
	retryCount--;
  }
  return -1;				/* Fatal error */
}



/* SEND_IF_COND */
#if(SUPPORT_SDHC_SDXC)
static int RtSdcard_Send_If_Cond( SD_CARD_INFO * mmc_sd_card ) /* SEND_IF_COND does not take arguments ?? */
{
int retryCount;
int respStatus; 
unsigned long respValue[4]; 

	retryCount = 0x200; /* TODO:should I stick to this retry count */

	while(retryCount > 0)
	{
		/* CMD8 */
		RtSdcard_SendCmd( SEND_IF_COND, SEND_IF_COND_ARG, EXPECT_SHORT_RESP, 0 );
		respStatus = RtSdcard_GetCmdResp( SEND_IF_COND, EXPECT_SHORT_RESP, &respValue[0] );
        if ( (respStatus==0) && (respValue[0] & 0x1AA) )
    	{
        	mmc_sd_card->card_type = SDHC_CARD_TYPE;
        	mmc_sd_card->state = IDLE_STATE;
        	return 0;	/* response is back and correct. */
    	}
		RtSdcard_Delay_micros(200);
    	retryCount--;

	}
	 return -1;
}
#endif //SUPPORT_SDHC_SDXC

#if(SDMMC_SUPPORT_STREAMING)
/******************************************************************************
** Function name:		RtSdcard_Send_Read_Multiple_Block
**
** Descriptions:		CMD18, READ_MULTIPLE_BLOCK, continuously transfers data
** 						blocks from card to host until interrupted by STOP_TRA-
** 						NSMISSION (CMD12) command. block lenght specified the
** 						smae as READ_SINGLE_BLOCK command
**
** parameters:			block number/address
** Returned value:		Response value/R1
**
******************************************************************************/
static int RtSdcard_Send_Read_Multiple_Block( SD_CARD_INFO * mmc_sd_card , unsigned long blockNum )
{
int retryCount;
int respStatus;
unsigned long respValue[4];

	retryCount = 0x20;
	while ( retryCount > 0 )
	{
		MCIClear = 0x7FF;
		RtSdcard_SendCmd( READ_MULTIPLE_BLOCK, blockNum * BLOCK_LENGTH, EXPECT_SHORT_RESP, 0 );
		respStatus = RtSdcard_GetCmdResp( READ_SINGLE_BLOCK, EXPECT_SHORT_RESP, &respValue[0] );
		
		/* it should be in the transfer state, bit 9~12 is 0x0100 and bit 8 is 1 */
		if ( !respStatus && ((respValue[0] & (0x0F << 8)) == 0x0900) )
		{
			return 0;			/* ready and in TRAN state */
		}
		RtSdcard_Delay_micros(200);
		retryCount--;
	}
	return -1;					/* Fatal error */
}


/* CMD 25 */
static int RtSdcard_Send_Write_Multiple_Block( SD_CARD_INFO * mmc_sd_card, unsigned long blockNum)
{
int retryCount;
int respStatus;
unsigned long respValue[4];


  retryCount = 0x20;
  while ( retryCount > 0 )
  {
	  MCIClear = 0x7FF;
	  RtSdcard_SendCmd( WRITE_MULTIPLE_BLOCK, blockNum * BLOCK_LENGTH, EXPECT_SHORT_RESP, 0 );
	  respStatus = RtSdcard_GetCmdResp( WRITE_BLOCK, EXPECT_SHORT_RESP, &respValue[0] );
	/* it should be in the transfer state, bit 9~12 is 0x0100 and bit 8 is 1 */
	if ( !respStatus && ((respValue[0] & (0x0F << 8)) == 0x0900) )
	{
	  return 0;			/* ready and in TRAN state */
	  // PVOPVO - Do state transition
	}

	RtSdcard_Delay_micros(200);
	retryCount--;
  }
  return -1;				/* Fatal error */
}
#endif

static int RtSdcard_CheckStatus( SD_CARD_INFO * mmc_sd_card )  /*check this function */
{
 unsigned long respValue;
  while ( 1 )
  {
	if ( (respValue = RtSdcard_Send_Status(mmc_sd_card)) == INVALID_RESPONSE )
	{
	  break;
	}
	else
	{
	  /* The only valid state is TRANS per MMC and SD state diagram.
	  RCV state may be seen, but, I have found that it happens
	  only when TX_ACTIVE or RX_ACTIVE occurs before the WRITE_BLOCK and
	  READ_BLOCK cmds are being sent, which is not a valid sequence. */
	  if ( (respValue & (0x0F << 8)) == 0x0900 )
	  {
		return 0;
	  }
	}
  }
  return -1;
}


/******************************************************************************
** Function name:		RtSdcard_Write_Block
**
** Descriptions:		Set MMCSD data control register, data length and data
**						timeout, send WRITE_BLOCK cmd, finally, enable
**						interrupt. On completion of WRITE_BLOCK cmd, TX_ACTIVE
**						interrupt will occurs, data can be written continuously
**						into the FIFO until the block data length is reached.
**
** parameters:			block number
** Returned value:		true or false, if cmd times out, return false and no
**						need to continue.
**
******************************************************************************/

static unsigned long config1, config2;
unsigned long check_status;

#if (DEBUG_SDCARD_DRIVER&&DEBUG_DUMP_DATA)
int _RtSdcard_Write_Block(int unitnumber, unsigned long blockNum, unsigned char *Buffer);  // line 1481
void DumpSector(unsigned long sectornumber, char *message, unsigned char *Buffer);

int RtSdcard_Write_Block(int unitnumber, unsigned long blockNum, unsigned char *Buffer)  // line 1481
{
	DumpSector(blockNum, "Write >>>", Buffer);
	int retval = _RtSdcard_Write_Block(unitnumber, blockNum, Buffer);
	if (retval < 0)
		printf("Write failed\n");
	return retval;

}
int _RtSdcard_Write_Block(int unitnumber, unsigned long blockNum, unsigned char *Buffer)
#else
int RtSdcard_Write_Block(int unitnumber, unsigned long blockNum, unsigned char *Buffer)  // line 1481
#endif
{
unsigned long DataCtrl = 0;
SD_CARD_INFO * mmc_sd_card;
int  polled_mode = 0;
int ret_val = -1;
unsigned long xferrbuff[128] ;
unsigned long level=0;
unsigned long *pBuffer;

#if (ENABLE_DMA)
//declare and initialize dma configuration structures
	int result;
	unsigned long data_coun=0;
	DMA_CONFIG dma_config;
	DMA_CHAN_CTRL dma_chan_ctrl;
	RtSdcard_Delay_micros(10000);
#endif

#if(DEBUG_SDCARD_DRIVER)
printf("Writing Block Sector #= %ld \n",blockNum);
#endif

if (unitnumber>=MAXSDCARDS)
		return -1;

	mmc_sd_card = &mmc_sd_cards[unitnumber];
	MCIClear = 0x7FF;
	MCIDataCtrl = 0;
	RtSdcard_Delay_micros(100);

#if(POLING_MODE)
	polled_mode=1;
#endif

	if ((unsigned long)Buffer & 0x3)
	{
		rtp_memcpy(&xferrbuff[0], Buffer, 512);
		pBuffer=(unsigned long *)&xferrbuff[0];
	}
	else
		pBuffer = (unsigned long *)Buffer;


	mmc_sd_card->blk_buffer=(unsigned long *) pBuffer;

#if(CONFIGURE_SDMMC_INTERRUPTS)
	SD_CARD_UNIT_NUM = unitnumber;
	rtp_sig_semaphore_clear( *mmc_sd_card->sdmmc_rw_sm );
#endif

	/*Check Status Before Transfer */
	if (RtSdcard_CheckStatus(mmc_sd_card) != 0 )
	{
		RtSdcard_Send_Stop(mmc_sd_card);
		return -1;
	}

	WriteGotTo=1;

	if (polled_mode)
		level=PVOPVO_T_splhigh();
	else
		MCI_TXEnable();

	WriteGotTo=2;

	MCIDataTimer = MA_DATA_TIMER_VALUE; //MA: Increased the time out, huge now, optimize later
  	MCIDataLength = BLOCK_LENGTH;

  	//TESTTEST
  	MCI_Block_End_Flag = 1; 			/* MA, What is this by the way?? DMA stuff, I wonder */

//	if ( RtSdcard_Send_Write_Block( mmc_sd_card, blockNum) != 0 )
//	{
//		goto write_error_return;
//	}

#if (ENABLE_DMA)
  	//WRITE
#if(1)
	disable_dma_chan(0);

	DMACIntTCClear = 0x03;
	DMACIntErrClr = 0x03;

	dma_config.src_is_mem = 1;
	dma_config.src_peripheral = 0 ;
	dma_config.dst_is_mem = 0;
	dma_config.dst_peripheral = SRC_SDMMC;
	dma_config.flow_control = M2P_P_CNTRL;
	//dma_config.flow_control = M2P_DMA_CNTRL;
	dma_config.intrpt_error_mask = 0  ;
	dma_config.terminal_count_intrpt_mask = 0 ; /* MA */
	dma_config.lock = 1 ;						/* MA */
	dma_config.active = 0 ;
	dma_config.halt = 0;

	dma_chan_ctrl.src_mem_add = mmc_sd_card->blk_buffer; //MA ??
	dma_chan_ctrl.dst_mem_add =  0xE008C080;
	dma_chan_ctrl.Transfer_Size = 512; 			/* Block Size */
	dma_chan_ctrl.Src_Burst_Size = 0x00000004  ; 		/* MA decoded to 8 or 16?? */
	dma_chan_ctrl.Dst_Burst_Size = 0x00000002 ; 		/* MA: More questions, need to investigate ??*/
	dma_chan_ctrl.Src_Transfer_Width = 0x00000002 ; 	/* 32 Bit Bus width MA ??*/
	dma_chan_ctrl.Dst_Transfer_Width = 0x00000002 ; 	/* 32 Bit Bus width MA ??*/
	dma_chan_ctrl.Src_Incr = 0x00000001 ; 			/* MA ??*/
	dma_chan_ctrl.Dst_Incr = 0 ; 				/* MA ??*/
	//dma_chan_ctrl.Protection = 0b100 ; 			/* MA ??*/
	dma_chan_ctrl.Protection = 0 ; 			/* MA ??*/
	dma_chan_ctrl.Terminal_Cnt_Intr_en = 1 ; 	/* MA ??*/

	result = cntrl_dma_chan(0, &dma_chan_ctrl);
		if(result < 0)
			return -1;

	result = configure_dma_chan(0, &dma_config);
		if(result < 0)
			return -1;

//DMACRawIntTCStatus
//DMACRawIntErrorStatus

#else
	DMACIntTCClear = 0x02;
	DMACIntErrClr = 0x02;
	DMACC0SrcAddr = 0xE008C080;
	DMACC0DestAddr = mmc_sd_card->blk_buffer;
	config1= (512 & 0x0FFF) | (0x04 << 12) | (0x02 << 15)
					| (0x02 << 18) | (0x02 << 21) | (1 << 26) | 0x80000000;
	DMACC0Control |= config1;
	DMACConfiguration = 0x01;	/* Enable DMA channels, little endian */
		  while ( !(DMACConfiguration & 0x01) );

	config2 = 0x10001 | (0x00 << 1) | (0x04 << 6) | (0x05 << 11);
	DMACC0Configuration |= config2 ;
#endif
	//DMA_Move( 0, M2P );
	//GPDMA_CH0_CFG |= 0x10001 | (0x00 << 1) | (0x04 << 6) | (0x05 << 11);
	/* Write, block transfer, DMA, and data length */
	DataCtrl |= ((1 << 0) | (1 << 3) | (DATA_BLOCK_LEN << 4));
#else
	/* Write, block transfer, and data length */
	DataCtrl |= ((1 << 0) | (DATA_BLOCK_LEN << 4));
#endif
	if ( RtSdcard_Send_Write_Block( mmc_sd_card, blockNum) != 0 )
	{
		goto write_error_return;
	}
	MCIDataCtrl = DataCtrl;
	RtSdcard_Delay_micros(1); //MA

	ret_val = 0;

#if(1)
	if (polled_mode)
  	{
		WriteGotTo=6;
		ret_val = PolledWriteXfer(pBuffer);
  		PVOPVO_T_splx(level);
	}
  	else
  	{
#if(CONFIGURE_SDMMC_INTERRUPTS)
  		rtp_sig_semaphore_wait_timed (*mmc_sd_card->sdmmc_rw_sm,-1);
  		rtp_sig_semaphore_clear( *mmc_sd_card->sdmmc_rw_sm );
#endif //CONFIGURE_SDMMC_INTERRUPTS
  	}
#endif //ENABLE_DMA


  	if (ret_val == -1)
  		return -1;

#if (ENABLE_DMA)
	//data_coun=MCIDataCnt;
	RtSdcard_Delay_micros(10000);  //Wait for the write to finish, have to find another mechanism
	disable_dma_chan(0);
#endif //ENABLE_DMA
  return 0;

write_error_return:
#if(!ENABLE_DMA)
	if (polled_mode)
	{
  		PVOPVO_T_splx(level);
	}
#endif
  	return -1;
}

static int PolledWriteXfer(unsigned long *plBbuffer)
{
#if(POLING_MODE)
unsigned long *fifo_ptr;
fifo_ptr = (unsigned long *) 0xE008C080;
unsigned long status;
int remain=512;
	do {
		int count;
		int maxcnt ;

		maxcnt = 32;
		if(remain > 32)
				count = 32;
		else
				count = remain;

		status = MCIStatus;

		if (status & TXFIFOHALFEMPTY)
			{
				*fifo_ptr		=	*plBbuffer     ;
				*(fifo_ptr+1 )	=	*(plBbuffer+1 )	 ;
				*(fifo_ptr+2 )	=	*(plBbuffer+2 ) 	 ;
				*(fifo_ptr+3 )	=	*(plBbuffer+3 )	 ;
				*(fifo_ptr+4 )	=	*(plBbuffer+4 )	 ;
				*(fifo_ptr+5 )	=	*(plBbuffer+5 )	 ;
				*(fifo_ptr+6 )	=	*(plBbuffer+6 )	 ;
				*(fifo_ptr+7 )	=	*(plBbuffer+7 )	 ;

				plBbuffer+=8;
				remain -= count;
			}

		if (remain == 0)
		{
			break;
		}

		RtSdcard_Delay_micros(10);
		maxwloops--;
		status = MCIStatus;
	}
	while (status & MCI_TXACTIVE && maxwloops>0);

	if(status & (1<<5) )
	{
#if (DEBUG_SDCARD_DRIVER)
		rtp_printf("DATA TIMEDOUT \n");
#endif //DEBUG_SDCARD_DRIVER
		return -1;  //TIMEOUT ERROR
	}

#endif //POLING_MODE
	return 0;
}


static void MCI_TXEnable( void )  //check this function, don't need interrupt??
{
#if ENABLE_DMA
	MCIMask0 |= ((DATA_END_INT_MASK)|(ERR_TX_INT_MASK));	/* Enable TX interrupts only */
  //MCI_MASK1 |= ((DATA_END_INT_MASK)|(ERR_TX_INT_MASK));	/* Enable TX interrupts only */
#else
	MCIMask0 |= ((FIFO_TX_INT_MASK)|(DATA_END_INT_MASK)|(ERR_TX_INT_MASK));	/* FIFO TX interrupts only */
	//MCIMask0 |= ((FIFO_TX_INT_MASK)|(ERR_TX_INT_MASK));	/* FIFO TX interrupts only */ //removed the DATA END INT MASK??
#endif
  return;
}

void MCI_TXDisable( void )
{
#if (ENABLE_DMA)
	MCIMask0 &= ~((DATA_END_INT_MASK)|(ERR_TX_INT_MASK));	/* Enable TX interrupts only */
	//MCIMask1 &= ~((DATA_END_INT_MASK)|(ERR_TX_INT_MASK));	/* Enable TX interrupts only */
#else
	MCIMask0 &= ~((FIFO_TX_INT_MASK)|(DATA_END_INT_MASK)|(ERR_TX_INT_MASK));	/* FIFO TX interrupts only */
	//MCIMask1 &= ~((FIFO_TX_INT_MASK)|(DATA_END_INT_MASK)|(ERR_TX_INT_MASK));	/* FIFO TX interrupts only */
#endif
  return;
}




#if (DEBUG_SDCARD_DRIVER&&DEBUG_DUMP_DATA)
int _RtSdcard_Read_Block(int unitnumber ,unsigned long blockNum, unsigned char *Buffer);
void DumpSector(unsigned long sectornumber, char *message, unsigned char *Buffer);
int RtSdcard_Read_Block(int unitnumber ,unsigned long blockNum, unsigned char *Buffer)
{
	int retval = _RtSdcard_Read_Block(unitnumber ,blockNum, Buffer);
	if (retval < 0)
	{
		printf("Read failed of sector %d\n", blockNum);
	}
	else
		DumpSector(blockNum, "Read >>>", Buffer);
	return retval;

}
int _RtSdcard_Read_Block(int unitnumber ,unsigned long blockNum, unsigned char *Buffer)
#else
int RtSdcard_Read_Block(int unitnumber ,unsigned long blockNum, unsigned char *Buffer)
#endif
{
	unsigned long DataCtrl = 0;
	SD_CARD_INFO * mmc_sd_card;
	int use_xferrbuff=0;
	unsigned long xferrbuff[128] ;
	unsigned char *pBbuffer=Buffer;
	int  polled_mode = 0;
	unsigned long level=0;
	int ret_val = -1;
#if (ENABLE_DMA)
//declare and initialize dma configuration structures
	int result;
	DMA_CONFIG dma_config;
	DMA_CHAN_CTRL dma_chan_ctrl;
	RtSdcard_Delay_micros(10000);
#endif

#if(POLING_MODE)
	polled_mode=1;
#endif //POLING_MODE

#if(DEBUG_SDCARD_DRIVER)
 printf("Reading Block Sector #= %ld \n",blockNum);
//rtp_printf("Reading Block Sector #= %ld \n",blockNum);
#endif

	if ((unsigned long)pBbuffer & 0x3)
	{
		pBbuffer = (unsigned char *)&xferrbuff[0];
		use_xferrbuff=1;
	}
	if (unitnumber>=MAXSDCARDS)
		return -1;
	mmc_sd_card = &mmc_sd_cards[unitnumber];

	MCIClear = 0x7FF;
	MCIDataCtrl = 0;
	RtSdcard_Delay_micros(100);

	mmc_sd_card->blk_buffer=(unsigned long *) pBbuffer;

#if(CONFIGURE_SDMMC_INTERRUPTS)
	SD_CARD_UNIT_NUM = unitnumber;
	rtp_sig_semaphore_clear( *mmc_sd_card->sdmmc_rw_sm );
#endif

	/* Below status check is redundant, but ensure card is in TRANS state
  	  before writing and reading to from the card. */
	if (RtSdcard_CheckStatus(mmc_sd_card) != 0 )
	{
		RtSdcard_Send_Stop(mmc_sd_card);
		return -1;
	}

	if (polled_mode)
		level=PVOPVO_T_splhigh();
	else
		MCI_RXEnable();

	MCIDataTimer = MA_DATA_TIMER_VALUE; //MA: Increased the time out, huge now, optimize later
	MCIDataLength = BLOCK_LENGTH;
	MCI_Block_End_Flag = 1; 			/* MA, What is this by the way?? DMA stuff, I wonder */
	if (RtSdcard_Send_Read_Block( mmc_sd_card, blockNum, BLOCK_LENGTH ) != 0)
		goto error_return;

#if ENABLE_DMA
#if(1)
	disable_dma_chan(0);

	DMACIntTCClear = 0x02;
	DMACIntErrClr = 0x02;

	//Cofigure GPDMA registers using DMA configuration registers
	dma_config.src_is_mem = 0;
	dma_config.src_peripheral = SRC_SDMMC;
	dma_config.dst_is_mem = 1;
	dma_config.dst_peripheral = 0;
	dma_config.flow_control = P2M_P_CNTRL;
	dma_config.intrpt_error_mask = 0;
	dma_config.terminal_count_intrpt_mask = 0 ; //MA
	dma_config.lock = 0;						//MA??
	dma_config.active = 0;
	dma_config.halt = 0;

	dma_chan_ctrl.src_mem_add = 0xE008C080;
	dma_chan_ctrl.dst_mem_add = mmc_sd_card->blk_buffer;
	dma_chan_ctrl.Transfer_Size = 512; /* Block Size */
	dma_chan_ctrl.Src_Burst_Size = 0x2  ; //MA decoded to 8 or 16??
	dma_chan_ctrl.Dst_Burst_Size = 0x04 ; /*8 MA: More questions, need to investigate ??*/
	dma_chan_ctrl.Src_Transfer_Width = 0x2 ; /* 32 Bit Bus width MA ??*/
	dma_chan_ctrl.Dst_Transfer_Width = 0x2 ; /* 32 Bit Bus width MA ??*/
	dma_chan_ctrl.Src_Incr = 0 ; /* MA ??*/
	dma_chan_ctrl.Dst_Incr = 0x01 ; /* MA ??*/
	dma_chan_ctrl.Protection = 0 ; /* MA ??*/
	dma_chan_ctrl.Terminal_Cnt_Intr_en = 1 ; /* MA ??*/

	result = cntrl_dma_chan(0, &dma_chan_ctrl);
		if(result < 0)
			return -1;


	result = configure_dma_chan(0, &dma_config);
	if(result < 0)
		return -1;


#else
	DMACIntTCClear = 0x02;
	DMACIntErrClr = 0x02;

	DMACC1SrcAddr = 0xE008C080;
	DMACC1DestAddr = mmc_sd_card->blk_buffer;

	config1= (512 & 0x0FFF) | (0x02 << 12) | (0x04 << 15)
					| (0x02 << 18) | (0x02 << 21) | (1 << 27) | 0x80000000;
	DMACC1Control |= config1;
	DMACConfiguration = 0x01;	/* Enable DMA channels, little endian */
		  while ( !(DMACConfiguration & 0x01) );

	config2 = 0x10001 | (0x04 << 1) | (0x00 << 6) | (0x06 << 11);
	DMACC1Configuration |= config2 ;
	//DMA_Move( 1, P2M );
  	//GPDMA_CH1_CFG |= 0x10001 | (0x04 << 1) | (0x00 << 6) | (0x06 << 11);
  	/* Write, block transfer, DMA, and data length */
#endif


  	DataCtrl |= ((1 << 0) | (1 << 1) | (1 << 3) | (DATA_BLOCK_LEN << 4));
#else
  	/* Read, enable, block transfer, and data length */
  	DataCtrl = ((1 << 0) | (1 << 1) | (DATA_BLOCK_LEN << 4));
#endif

  /* No break points after this or we'll get a fifo overrun */
  	MCIDataCtrl = DataCtrl;
  	RtSdcard_Delay_micros(1); //MA

  	ret_val = 0;
  	if (polled_mode)
  	{
  		ret_val = PolledReadXfer(pBbuffer);
  		PVOPVO_T_splx(level);
  	}
  	else
  	{
#if(CONFIGURE_SDMMC_INTERRUPTS)
  		rtp_sig_semaphore_wait_timed (*mmc_sd_card->sdmmc_rw_sm,-1);
  		rtp_sig_semaphore_clear( *mmc_sd_card->sdmmc_rw_sm );
#endif //CONFIGURE_SDMMC_INTERRUPTS
  	}
  	if (ret_val == -1)
  		return -1;
  	if (use_xferrbuff)
  		rtp_memcpy(Buffer, &xferrbuff[0], 512);

  	RtSdcard_Delay_micros(10000);  //Wait for the write to finish, have to find another mechanism
  
#if (ENABLE_DMA)
  	//wait_chan_active(0);
  	disable_dma_chan(0);
#endif
    
  return 0;

error_return:
	if (polled_mode)
	{
  		PVOPVO_T_splx(level);
	}
  	return -1;
}


static int PolledReadXfer(unsigned char *pBbuffer)
{
unsigned long ShadowStatus;
unsigned long length_of_data =0, count_of_data=0;
unsigned long maxloops = 100000000;
unsigned long fifo_count_start;
unsigned long *plBbuffer = (unsigned long *)pBbuffer;
unsigned long *fifo_ptr;

  fifo_count_start = MCIFifoCnt;
  ShadowStatus=MCIStatus ;

  while((ShadowStatus & SD_RX_DATA_AVAILABLE)==0)
  {
	  ShadowStatus=MCIStatus ;
	  if (--maxloops==0)
	  {
		  return -1;
	  }
  }

  fifo_ptr = (unsigned long *) 0xE008C080;
  count_of_data=512;
  while(count_of_data && maxloops)
  {
  	maxloops--;
		  //READ FIFO LOOP
#if (0)
	  if (count_of_data >=64 && (ShadowStatus & SD_FIFO_FULL))
	  {
		  *plBbuffer = *fifo_ptr;
		  *(plBbuffer+1 ) = *(fifo_ptr+1 );
		  *(plBbuffer+2 ) = *(fifo_ptr+2 );
		  *(plBbuffer+3 ) = *(fifo_ptr+3 );
		  *(plBbuffer+4 ) = *(fifo_ptr+4 );
		  *(plBbuffer+5 ) = *(fifo_ptr+5 );
		  *(plBbuffer+6 ) = *(fifo_ptr+6 );
		  *(plBbuffer+7 ) = *(fifo_ptr+7 );
		  *(plBbuffer+8 ) = *(fifo_ptr+8 );
		  *(plBbuffer+9 ) = *(fifo_ptr+9 );
		  *(plBbuffer+10 ) = *(fifo_ptr+10 );
		  *(plBbuffer+11 ) = *(fifo_ptr+11 );
		  *(plBbuffer+12) = *(fifo_ptr+12);
		  *(plBbuffer+13) = *(fifo_ptr+13);
		  *(plBbuffer+14) = *(fifo_ptr+14);
		  *(plBbuffer+15) = *(fifo_ptr+15);
		  count_of_data -= 64;

	  }
	  else if (count_of_data >=32 && (ShadowStatus & SD_FIFO_HALF_FULL))
	  {
		  *plBbuffer = *fifo_ptr;
		  *(plBbuffer+1 ) = *(fifo_ptr+1 );
		  *(plBbuffer+2 ) = *(fifo_ptr+2 );
		  *(plBbuffer+3 ) = *(fifo_ptr+3 );
		  *(plBbuffer+4 ) = *(fifo_ptr+4 );
		  *(plBbuffer+5 ) = *(fifo_ptr+5 );
		  *(plBbuffer+6 ) = *(fifo_ptr+6 );
		  *(plBbuffer+7 ) = *(fifo_ptr+7 );
		  count_of_data -= 32;
	  }
	  else
#endif
	  if (count_of_data >= 4 && (ShadowStatus & SD_RX_DATA_AVAILABLE))
	  {
		  *plBbuffer++ = *fifo_ptr;
		  count_of_data -= 4;
	  }
	  else if (count_of_data == 0)
	  {
#if (DEBUG_SDCARD_DRIVER)
		  length_of_data =  MCIDataLength;
		  rtp_printf("Too much Length =%d and the count of data to be read =%d, read so far = %d \n",length_of_data, count_of_data, (length_of_data-count_of_data) );
#endif //DEBUG_SDCARD_DRIVER
		  return -1;
	  }
	  ShadowStatus=MCIStatus ; //MA

	  if(ShadowStatus & (1<<5) )
	  {
#if (DEBUG_SDCARD_DRIVER)
		  rtp_printf("DATA TIMEDOUT \n");
#endif //DEBUG_SDCARD_DRIVER
		  return -1;
	  }
  }
  if (maxloops==0)
  {
#if (DEBUG_SDCARD_DRIVER)
		  rtp_printf("Loopmax exceeded\n");
#endif //DEBUG_SDCARD_DRIVER
  	return -1;
  }
  else
  	return 0;

}


static void MCI_RXEnable( void )
  {
  #if ENABLE_DMA
	//not yet done
	MCIMask0  |= ((DATA_END_INT_MASK)|(ERR_RX_INT_MASK));	/* Enable RX interrupts only */
    //MCI_MASK1 |= ((DATA_END_INT_MASK)|(ERR_RX_INT_MASK));	/* Enable RX interrupts only */
  #else
    MCIMask0 |= ((FIFO_RX_INT_MASK)|(DATA_END_INT_MASK)|(ERR_RX_INT_MASK));	/* FIFO RX interrupts only */
    //MCIMask0 |= ((FIFO_RX_INT_MASK)|(ERR_RX_INT_MASK));	/* FIFO RX interrupts only */ //removed DATA_END_INT_MASK??
  #endif
    return;
  }

#if(DEBUG_SDCARD_DRIVER&&DEBUG_DUMP_DATA)
void DumpSector(unsigned long sectornumber, char *message, unsigned char *Buffer)
{
    unsigned short i;
    unsigned char *pW;
	printf("Sector %d: %s\n",sectornumber, message);
    pW = (unsigned char *) Buffer;
    printf("Break here\n");
    for (i=0; i<512; i++)
     {
     	printf("%2x|",*pW++);
     	if ((i&0x1f) ==0)
     		printf("\n");
     }
}
#endif

#if(SUPPORT_1_8_VOLT)
static void RtSdcard_Switch_Voltage(SD_CARD_INFO *pmmc_sd_card)
{
	//send Command 11
	//have to check on the response of ACMD41 for SR18A (Bit 24 of the R3(OCR) )
	if( MCI_VoltageSwitch() == -1)
	{
		rtp_printf("Voltage switch to 1.8V failed?! \n \r");
	}
}

static int RtSdcard_VoltageSwitch( void )
{
	//using MCI_Go_Idle_State( void ) and MCI_Send_Status (void) to prototype
	unsigned long retryCount;
	int respStatus;
	unsigned long respValue[4];
	unsigned long cmd_index=0;
	retryCount = 0x200;
  while ( retryCount > 0 )
  {
	/* Send CMD11 command repeatedly until the response is back correctly */
	  MCI_CLEAR |= (MCI_CMD_TIMEOUT | MCI_CMD_CRC_FAIL | MCI_CMD_RESP_END); //is that all I need to clear?
	  MCI_SendCmd( VOLTAGE_SWITCH, 0x00000000, EXPECT_SHORT_RESP, 0 );  //expected resonse R1
	  respStatus = RtSdcard_GetCmdResp( VOLTAGE_SWITCH/*GO_IDLE_STATE*/, EXPECT_SHORT_RESP, (unsigned long *)&respValue[0] );
	  cmd_index = respValue[0];
	  cmd_index &= 0x3F0000000000;
	  cmd_index = (cmd_index >> 40);
	  if( cmd_index == VOLTAGE_SWITCH )
	  {
		  if ( !respStatus && (respValue[0] & (1 << 9)) )
	  	  {
		  /* The card should be in the ready state due to ACMD41, and the transition is again to the ready state after CMD11 */
		  //return ( respValue[0] );
			  mmc_sd_card->state = READY_STATE;
			  return 0;
	  	  }
	 }
	 retryCount--;
  }

  return -1;
  //TODO: Need review when you get 1.8 V signaling capable board, and maybe complete spec
}
#else
static int RtSdcard_Switch_Voltage(SD_CARD_INFO *pmmc_sd_card)
{
	return 0;
}
#endif //SUPPORT_1_8_VOLT


static int RtSdcard_Send_ACMD_SCR(SD_CARD_INFO *pmmc_sd_card )
{
int retryCount;
unsigned long respStatus;
unsigned long  respValue[4];

	retryCount = 0x20;			/* reset retry counter */
	while ( retryCount > 0 )
	{
		if ( RtSdcard_Send_ACMD(pmmc_sd_card) == -1 )
			continue;
		/* Send ACMD51 command to get the SCR register*/
		RtSdcard_SendCmd( SEND_APP_SEND_SCR, 0x00000000, EXPECT_SHORT_RESP, 0 );
		respStatus = RtSdcard_GetCmdResp( SEND_APP_SEND_SCR, EXPECT_SHORT_RESP, &respValue[0] );
		if ( respStatus==0 && ((respValue[0] & (0x0F << 8)) == 0x0900) )
			return 0;	/* response is back and correct. */
		RtSdcard_Delay_micros(200);
		retryCount--;
	}
	return -1;
}



/*****************************************************************************************************
 *
 * Configuring the Interrupts for SDMMC
 *
 ******************************************************************************************************/
#if(CONFIGURE_SDMMC_INTERRUPTS)

volatile unsigned long MCI_CmdProcess_count = 0;
volatile unsigned long MCI_FIFOInterruptService_count = 0;
volatile unsigned long DataRxFIFOCount = 0;
volatile unsigned long DataTxFIFOCount = 0;

//To Be removed
#define FALSE 0

unsigned long intr_status=0;

void MCI_FIFOInterruptService(SD_CARD_INFO *pmmc_sd_card);

static unsigned long temp_status;
void ISR_sdmmc(void)
{
  unsigned long status;
  //unsigned long temp_status;

  unsigned long intr_mask=0;

  intr_mask = PVOPVO_T_splhigh();

  status = MCIStatus;
  /* handle MCI_STATUS interrupt */
	SD_CARD_INFO * pmmc_sd_card;
	if ( SD_CARD_UNIT_NUM >= MAXSDCARDS )
		printf("ERORR, NO SUCH CARD NUMBER?? \n");  //handel the error?

	pmmc_sd_card = &mmc_sd_cards[SD_CARD_UNIT_NUM];



if ( status & DATA_ERR_INT_MASK )
  {

	/* Not Implemented Yet*/
	MCI_DataErrorProcess();
	//MCI_DataErrorProcess_count++;

	VICAddress=0; 			/* Ack the interrupt */
	printf("DATA ERROR Interrupt \n");
	rtp_printf("DATA ERROR Interrupt \n");
	intr_status = MCIStatus;
	rtp_sig_semaphore_signal(*pmmc_sd_card->sdmmc_rw_sm);
	PVOPVO_T_splx(intr_mask);
	return;
  }
  if ( status & DATA_END_INT_MASK )
  {
	  /* Not Implemented Yet*/
	  MCI_DATA_END_InterruptService();
	  //MCI_DATA_END_InterruptService_count++;
	  VICAddress=0;
	  //MCIClear = 0x500;
	  printf("DATA END Interrupt \n");
	  rtp_sig_semaphore_signal(*pmmc_sd_card->sdmmc_rw_sm);
	  PVOPVO_T_splx(intr_mask);
	  return;
  }
  else if(status & FIFO_INT_MASK)
  {

	  MCI_FIFOInterruptService(pmmc_sd_card);
	  MCI_FIFOInterruptService_count++;
	  VICAddress=0; 			/* Ack the interrupt , we do it this way for USB??*/
	  if(!temp_status )
	  //PVOPVO_T_splx(intr_mask);
	  return;
  }
  else if ( status & CMD_INT_MASK )
  {
	  /* Not Implemented Yet*/
	  //MCI_CmdProcess();
	  //MCI_CmdProcess_count++;

	  /* Ack the interrupt */
	  VICVectAddr0 = 0;
	  printf("Command Process Interrupt \n");
	  rtp_printf("Command Process Interrupt \n");
	  PVOPVO_T_splx(intr_mask);
	  return;
  }

}


int configure_sdmmcIsr(void)
{
/* Install SDMMC interrupt handler */
 printf("Use sys  hook sdmmc \n");
 if (install_irq(VIC_SDMMC, (void *)ISR_sdmmc, SDMMC_PRIORITY) == FALSE)
 {
     return 0;
 }
 else
	 return 1;
}


void MCI_DATA_END_InterruptService(void)
{
unsigned long status = 0;

	status = MCIStatus;
	if(status & MCI_DATA_END)
	{
		MCIClear = MCI_DATA_END;
		return ;
	}

	if(status & MCI_DATA_BLK_END )
	{
		MCIClear = MCI_DATA_BLK_END;

		return ;
	}
}

void MCI_DataErrorProcess(void)
{
unsigned long status;

	status = MCIStatus;

	if(status & MCI_DATA_CRC_FAIL)
		MCIClear = MCI_DATA_CRC_FAIL;

	if(status & MCI_DATA_TIMEOUT)
			MCIClear = MCI_DATA_TIMEOUT;

	if(status & MCI_TX_UNDERRUN)
			MCIClear = MCI_TX_UNDERRUN;

	if(status & MCI_RX_OVERRUN)
			MCIClear = MCI_RX_OVERRUN;

	if(status & MCI_START_BIT_ERR)
			MCIClear = MCI_START_BIT_ERR;


	return;
}



void MCI_FIFOInterruptService(SD_CARD_INFO *pmmc_sd_card)
{
#if (!ENABLE_DMA)
unsigned long count_of_data=0;
unsigned long maxloops = 100000000;
unsigned long fifo_count_start;
unsigned long *fifo_ptr;
unsigned long bytes_count = 0;
unsigned long *plBbuffer;
unsigned long ShadowStatus = MCIStatus ;
unsigned long intrpt_byte_write_count = 0;
unsigned long bytes_to_write = 512;
int remain=512;

fifo_ptr = (unsigned long *) 0xE008C080;

plBbuffer = pmmc_sd_card->blk_buffer;
	if ( (ShadowStatus & (FIFO_TX_INT_MASK) ) && ( ShadowStatus & MMCSD_TX_ACTIVE ) )
	  {
		DataTxFIFOCount++;			 /* if using TX_HALF_EMPTY remove one WriteFifo below */
		int count;

			if (ShadowStatus & MMCSD_TX_FIFO_EMPTY)
				{
					*fifo_ptr		=	*plBbuffer     	 ;
					*(fifo_ptr+1 )	=	*(plBbuffer+1 )	 ;
					*(fifo_ptr+2 )	=	*(plBbuffer+2 )  ;
					*(fifo_ptr+3 )	=	*(plBbuffer+3 )	 ;
					*(fifo_ptr+4 )	=	*(plBbuffer+4 )	 ;
					*(fifo_ptr+5 )	=	*(plBbuffer+5 )	 ;
					*(fifo_ptr+6 )	=	*(plBbuffer+6 )	 ;
					*(fifo_ptr+7 )	=	*(plBbuffer+7 )	 ;
					*(fifo_ptr+8 )	=	*(plBbuffer+8 )	 ;
					*(fifo_ptr+9 )	=	*(plBbuffer+9 )	 ;
					*(fifo_ptr+10 )	=	*(plBbuffer+10 ) ;
					*(fifo_ptr+11 )	=	*(plBbuffer+11 ) ;
					*(fifo_ptr+12 )	=	*(plBbuffer+12 ) ;
					*(fifo_ptr+13 )	=	*(plBbuffer+13 ) ;
					*(fifo_ptr+14 )	=	*(plBbuffer+14 ) ;
					*(fifo_ptr+15 )	=	*(plBbuffer+15 ) ;

					//plBbuffer+=8;
					plBbuffer+=16 ;
					remain -= count;
					intrpt_byte_write_count += 64;
				}//else if data fifo empty
			else if(ShadowStatus & TXFIFOHALFEMPTY)
			{
				*fifo_ptr		=	*plBbuffer     ;
				*(fifo_ptr+1 )	=	*(plBbuffer+1 )	 ;
				*(fifo_ptr+2 )	=	*(plBbuffer+2 ) 	 ;
				*(fifo_ptr+3 )	=	*(plBbuffer+3 )	 ;
				*(fifo_ptr+4 )	=	*(plBbuffer+4 )	 ;
				*(fifo_ptr+5 )	=	*(plBbuffer+5 )	 ;
				*(fifo_ptr+6 )	=	*(plBbuffer+6 )	 ;
				*(fifo_ptr+7 )	=	*(plBbuffer+7 )	 ;

				remain -= count;
				intrpt_byte_write_count += 32;
				plBbuffer+=8;
			}//if data fifo half empty

			RtSdcard_Delay_micros(5);
			maxwloops--;
			ShadowStatus = MCIStatus;
			g_data_counter = MCIDataCnt ;
			pmmc_sd_card->blk_buffer = plBbuffer ;
			return ;
		}
	else if ( ShadowStatus& (FIFO_RX_INT_MASK) )
	  {
		DataRxFIFOCount++;

		  fifo_count_start = MCIFifoCnt;

		  //fifo_ptr = (unsigned long *) 0xE008C080;
		  count_of_data=512;
		  while(count_of_data && maxloops)
		  {
			  maxloops--;
			  if (count_of_data >=64 && (ShadowStatus & SD_FIFO_FULL))
			  {
				  *plBbuffer = *fifo_ptr;
				  *(plBbuffer+bytes_count+1 ) = *(fifo_ptr+1 );
				  *(plBbuffer+bytes_count+2 ) = *(fifo_ptr+2 );
				  *(plBbuffer+bytes_count+3 ) = *(fifo_ptr+3 );
				  *(plBbuffer+bytes_count+4 ) = *(fifo_ptr+4 );
				  *(plBbuffer+bytes_count+5 ) = *(fifo_ptr+5 );
				  *(plBbuffer+bytes_count+6 ) = *(fifo_ptr+6 );
				  *(plBbuffer+bytes_count+7 ) = *(fifo_ptr+7 );
				  *(plBbuffer+bytes_count+8 ) = *(fifo_ptr+8 );
				  *(plBbuffer+bytes_count+9 ) = *(fifo_ptr+9 );
				  *(plBbuffer+bytes_count+10 ) = *(fifo_ptr+10 );
				  *(plBbuffer+bytes_count+11 ) = *(fifo_ptr+11 );
				  *(plBbuffer+bytes_count+12) = *(fifo_ptr+12);
				  *(plBbuffer+bytes_count+13) = *(fifo_ptr+13);
				  *(plBbuffer+bytes_count+14) = *(fifo_ptr+14);
				  *(plBbuffer+bytes_count+15) = *(fifo_ptr+15);
				  bytes_count+=16;
				  count_of_data -= 64;
			  }
			  else if (count_of_data >=32 && (ShadowStatus & SD_FIFO_HALF_FULL))
			  {
				  *plBbuffer = *fifo_ptr;
				  *(plBbuffer+bytes_count+1 ) = *(fifo_ptr+1 );
				  *(plBbuffer+bytes_count+2 ) = *(fifo_ptr+2 );
				  *(plBbuffer+bytes_count+3 ) = *(fifo_ptr+3 );
				  *(plBbuffer+bytes_count+4 ) = *(fifo_ptr+4 );
				  *(plBbuffer+bytes_count+5 ) = *(fifo_ptr+5 );
				  *(plBbuffer+bytes_count+6 ) = *(fifo_ptr+6 );
				  *(plBbuffer+bytes_count+7 ) = *(fifo_ptr+7 );
				  bytes_count+=8;
				  count_of_data -= 32;
			  }
			  else if (count_of_data >= 4 && (ShadowStatus & SD_RX_DATA_AVAILABLE))
			  {
				  *(plBbuffer + bytes_count)= *fifo_ptr;
				  count_of_data -= 4;
				  bytes_count ++;  //to be removed
			  }
			  else if (count_of_data == 0)
			  {
				  return -1;
			  }

			  ShadowStatus=MCIStatus ;

			  if(ShadowStatus & (1<<5) )
			  {
				  return -1;
			  }

			  if (maxloops==0)
			  {
				  return -1;
			  }

		  }

	  }
#endif //ENABLE_DMA
	return;
}


#endif


static void parse_CID(SD_CARD_INFO * mmc_sd_card, unsigned long *respValue)
{
	unsigned char temp_8;
	unsigned long temp_32, temp_32b;
	unsigned short temp_16;
	unsigned long long temp_serial;

	temp_32 = DWSWAP(respValue[0]) & 0xff000000;
	temp_32 = (temp_32 >> 24);
	temp_8 = (unsigned char) (temp_32 & 0xff);
	mmc_sd_card->CID.MID = temp_8;

	temp_32 = DWSWAP(respValue[0]) & 0x00ffff00;
	temp_32 = (temp_32 >> 8);
	temp_16 = temp_32 & 0xFFFF;
	mmc_sd_card->CID.OID = temp_16;

	temp_serial = DWSWAP(respValue[0]) & 0x000000ff;
	temp_serial = (temp_serial << 32);  //bits 40 down  PVOPVO - long long not protable
	temp_32 = DWSWAP(respValue[1]);
	temp_serial = temp_serial | temp_32 ;
	mmc_sd_card->CID.PNM = temp_serial;

	temp_32 = DWSWAP(respValue[2]);
	temp_32 = temp_32 & 0xff000000;
	temp_32 = ( temp_32 >> 24 );
	temp_8 = temp_32 & 0xFF;
	mmc_sd_card->CID.PRV = temp_8;

	temp_32 = DWSWAP(respValue[2]);
	temp_32 = temp_32 & 0x00FFFFFF;
	temp_32 = (temp_32 << 8 );

	temp_32b = DWSWAP(respValue[3]);
	temp_32b = temp_32b & 0xFF000000;
	temp_32b = ( temp_32b >> 24 );
	temp_32 = temp_32 |temp_32b;
	mmc_sd_card->CID.PSN = temp_32;

	temp_32 = DWSWAP(respValue[3]);
	temp_32 = temp_32 & 0x000FFF00;
	temp_32 = ( temp_32 >> 8 );
	temp_16 = temp_32 & 0xfff;
	mmc_sd_card->CID.MDT = temp_16;

	mmc_sd_card->CID.MDT_month = temp_16 & 0x00f;
	temp_16 = temp_16 & 0xff0;
	temp_16 = (temp_16 >> 4);
	mmc_sd_card->CID.MDT_year = 2000 + temp_16;
}


static void parse_CSD(SD_CARD_INFO * mmc_sd_card, unsigned long *respValue)
{
	unsigned long temp_32;
	unsigned long card_memory_capacity;
	int csd_structure;
	unsigned long block_len;
	unsigned long mult;
	unsigned long blocknr;
	int i;

	temp_32 = DWSWAP(respValue[0]);
	temp_32 = temp_32 & 0xC0000000 ;
	temp_32 = (temp_32 >> 30);
	csd_structure  = temp_32 & 0x00000003;

	if(csd_structure == 0)
	{
		/* CSD strucure version 1 */
		mmc_sd_card->CSD_Version=0;
		mmc_sd_card->CSD_V1.CSD_STRUCTURE = csd_structure ;

		temp_32 = DWSWAP(respValue[0]);
		temp_32 = temp_32 & 0x00ff0000 ;
		temp_32 = (temp_32 >> 16);
		mmc_sd_card->CSD_V1.TAAC = temp_32 & 0xff;

		temp_32 = DWSWAP(respValue[0]);
		temp_32 = temp_32 & 0x0000ff00 ;
		temp_32 = ( temp_32 >> 8 );
		mmc_sd_card->CSD_V1.NSAC = temp_32 & 0xff;

		temp_32 = DWSWAP(respValue[0]);
		temp_32 = temp_32 & 0x000000ff ;
		mmc_sd_card->CSD_V1.TRAN_SPEED = temp_32 & 0xff;

		temp_32 = DWSWAP(respValue[1]);
		temp_32 = temp_32 & 0xfff00000 ;
		temp_32 = ( temp_32 >> 20 );
		mmc_sd_card->CSD_V1.CCC = temp_32 & 0xfff; /* ?? does not match with the document default value (default value is 01x110110101 getting 100110101) */

		temp_32 = DWSWAP(respValue[1]);
		temp_32 = temp_32 & 0x000f0000 ;
		temp_32 = ( temp_32 >> 16 );
		mmc_sd_card->CSD_V1.READ_BL_LEN = temp_32 & 0x0f;

		temp_32 = DWSWAP(respValue[1]);
		temp_32 = temp_32 & 0x00008000 ;
		temp_32 = ( temp_32 >> 15 );
		mmc_sd_card->CSD_V1.READ_BL_PARTIAL= temp_32 & 0x01;

		temp_32 = DWSWAP(respValue[1]);
		temp_32 = temp_32 & 0x00004000 ;
		temp_32 = ( temp_32 >> 14 );
		mmc_sd_card->CSD_V1.WRITE_BLK_MISALIGN = temp_32 & 0x01;

		temp_32 = DWSWAP(respValue[1]);
		temp_32 = temp_32 & 0x00002000 ;
		temp_32 = ( temp_32 >> 13 );
		mmc_sd_card->CSD_V1.READ_BLK_MISALIGN = temp_32 & 0x01;

		temp_32 = DWSWAP(respValue[1]);
		temp_32 = temp_32 & 0x00001000 ;
		temp_32 = ( temp_32 >> 12 );
		mmc_sd_card->CSD_V1.DSR_IMP = temp_32 & 0x01;

		temp_32 = DWSWAP(respValue[1]);
		temp_32 = temp_32 & 0x000003ff ;
		temp_32 = ( temp_32 << 2 );
		mmc_sd_card->CSD_V1.C_SIZE = temp_32 & 0xff8;

		temp_32 = DWSWAP(respValue[2]);
		temp_32 = temp_32 & 0xe0000000 ;
		temp_32 = ( temp_32 >> 30 );
		mmc_sd_card->CSD_V1.C_SIZE = mmc_sd_card->CSD_V1.C_SIZE | (temp_32 & 0x03);

		temp_32 = DWSWAP(respValue[2]);
		temp_32 = temp_32 & 0x38000000 ;
		temp_32 = ( temp_32 >> 27 );
		mmc_sd_card->CSD_V1.VDD_R_CURR_MIN= temp_32 & 0x07;

		temp_32 = DWSWAP(respValue[2]);
		temp_32 = temp_32 & 0x07000000 ;
		temp_32 = ( temp_32 >> 24 );
		mmc_sd_card->CSD_V1.VDD_R_CURR_MAX = temp_32 & 0x07;

		temp_32 = DWSWAP(respValue[2]);
		temp_32 = temp_32 & 0x00e00000 ;
		temp_32 = ( temp_32 >> 21 );
		mmc_sd_card->CSD_V1.VDD_W_CURR_MIN = temp_32 & 0x07;

		temp_32 = DWSWAP(respValue[2]);
		temp_32 = temp_32 & 0x001c0000 ;
		temp_32 = ( temp_32 >> 18 );
		mmc_sd_card->CSD_V1.VDD_W_CURR_MAX = temp_32 & 0x07;

		temp_32 = DWSWAP(respValue[2]);
		temp_32 = temp_32 & 0x00038000 ;
		temp_32 = ( temp_32 >> 15 );
		mmc_sd_card->CSD_V1.C_SIZE_MULT = temp_32 & 0x07;

		temp_32 = DWSWAP(respValue[2]);
		temp_32 = temp_32 & 0x00004000 ;
		temp_32 = ( temp_32 >> 14 );
		mmc_sd_card->CSD_V1.ERASE_BLK_EN= temp_32 & 0x01;

		temp_32 = DWSWAP(respValue[2]);
		temp_32 = temp_32 & 0x00003f80 ;
		temp_32 = ( temp_32 >> 7 );
		mmc_sd_card->CSD_V1.SECTOR_SIZE = temp_32 & 0x7f;

		temp_32 = DWSWAP(respValue[2]);
		temp_32 = temp_32 & 0x0000007f ;
		mmc_sd_card->CSD_V1.WP_GRP_SIZE = temp_32 & 0x7f;

		temp_32 = DWSWAP(respValue[3]);
		temp_32 = temp_32 & 0x80000000 ;
		temp_32 = ( temp_32 >> 31 );
		mmc_sd_card->CSD_V1.WP_GRP_ENABLE = temp_32 & 0x01;

		temp_32 = DWSWAP(respValue[3]);
		temp_32 = temp_32 & 0x1c000000 ;
		temp_32 = ( temp_32 >> 26 );
		mmc_sd_card->CSD_V1.R2W_FACTOR= temp_32 & 0x07;

		temp_32 = DWSWAP(respValue[3]);
		temp_32 = temp_32 & 0x03c00000 ;
		temp_32 = ( temp_32 >> 22 );
		mmc_sd_card->CSD_V1.WRITE_BL_LEN = temp_32 & 0x0f;

		temp_32 = DWSWAP(respValue[3]);
		temp_32 = temp_32 & 0x00200000 ;
		temp_32 = ( temp_32 >> 21 );
		mmc_sd_card->CSD_V1.WRITE_BL_PARTIAL = temp_32 & 0x01;

		temp_32 = DWSWAP(respValue[3]);
		temp_32 = temp_32 & 0x00008000 ;
		temp_32 = ( temp_32 >> 15 );
		mmc_sd_card->CSD_V1.FILE_FORMAT_GRP = temp_32 & 0x01;

		temp_32 = DWSWAP(respValue[3]);
		temp_32 = temp_32 & 0x00004000 ;
		temp_32 = ( temp_32 >> 14 );
		mmc_sd_card->CSD_V1.COPY = temp_32 & 0x01;

		temp_32 = DWSWAP(respValue[3]);
		temp_32 = temp_32 & 0x00002000 ;
		temp_32 = ( temp_32 >> 13 );
		mmc_sd_card->CSD_V1.PERM_WRITE_PROTECT= temp_32 & 0x01;

		temp_32 = DWSWAP(respValue[3]);
		temp_32 = temp_32 & 0x00001000 ;
		temp_32 = ( temp_32 >> 12 );
		mmc_sd_card->CSD_V1.TMP_WRITE_PROTECT= temp_32 & 0x01;

		temp_32 = DWSWAP(respValue[3]);
		temp_32 = temp_32 & 0x00000c00 ;
		temp_32 = ( temp_32 >> 10 );
		mmc_sd_card->CSD_V1.FILE_FORMAT= temp_32 & 0x03;

		//to be removed later.....
		temp_32 = DWSWAP(respValue[3]);
		temp_32 = temp_32 & 0x000000fe ;
		temp_32 = ( temp_32 >> 1 );
		mmc_sd_card->CSD_V1.CRC = temp_32 & 0x7f;


		if( mmc_sd_card->CSD_V1.READ_BL_LEN < 12 )
		{	block_len = 1;  /*pow(2,mmc_sd_card->CSD_V1.READ_BL_LEN) */
			for(i=1 ; i <= mmc_sd_card->CSD_V1.READ_BL_LEN ; i++)
				block_len *= 2 ; /* pow(2,mmc_sd_card->CSD_V1.READ_BL_LEN)*/
		}
		else
		{
			rtp_printf("Error?! mmc_sd_card->CSD_V1.READ_BL_LEN >= 12 \n\r");
	}

		if( mmc_sd_card->CSD_V1.C_SIZE_MULT < 8 )
		{
			mult =1;
			for(i=1 ; i <= mmc_sd_card->CSD_V1.C_SIZE_MULT+2 ; i++)
				mult *= 2 ; /* pow(2,(mmc_sd_card->CSD_V1.C_SIZE_MULT+2)) */
		}
		blocknr = ( mmc_sd_card->CSD_V1.C_SIZE +1) * mult;
		mmc_sd_card->no_blocks = blocknr ;  /* no of blocks in SDSC card */
		mmc_sd_card->bytes_per_block = block_len;
		card_memory_capacity = blocknr * block_len ;
		mmc_sd_card->card_capacity_bytes = card_memory_capacity ;


	}
	else
	{
		/* CSD strucure version 2 */
		if(csd_structure == 1)
		{
			mmc_sd_card->CSD_Version=1;
			mmc_sd_card->CSD_V2.CSD_STRUCTURE = csd_structure ;

			temp_32 = DWSWAP(respValue[0]);
			temp_32 = temp_32 & 0x00ff0000 ;
			temp_32 = (temp_32 >> 16);
			mmc_sd_card->CSD_V2.TAAC = temp_32 & 0xff;

			temp_32 = DWSWAP(respValue[0]);
			temp_32 = temp_32 & 0x0000ff00 ;
			temp_32 = (temp_32 >> 8);
			mmc_sd_card->CSD_V2.NSAC = temp_32 & 0xff;

			temp_32 = DWSWAP(respValue[0]);
			temp_32 = temp_32 & 0x000000ff ;
			mmc_sd_card->CSD_V2.TRAN_SPEED = temp_32 & 0xff;

			temp_32 = DWSWAP(respValue[1]);
			temp_32 = temp_32 & 0xfff00000 ;
			temp_32 = (temp_32 >> 20);
			mmc_sd_card->CSD_V2.CCC = temp_32 & 0xfff;

			temp_32 = DWSWAP(respValue[1]);
			temp_32 = temp_32 & 0x000f0000 ;
			temp_32 = (temp_32 >> 16);
			mmc_sd_card->CSD_V2.READ_BL_LEN = temp_32 & 0xf;

			temp_32 = DWSWAP(respValue[1]);
			temp_32 = temp_32 & 0x0000f000 ;
			temp_32 = (temp_32 >> 15);
			mmc_sd_card->CSD_V2.READ_BL_PARTIAL= temp_32 & 0x1;

			temp_32 = DWSWAP(respValue[1]);
			temp_32 = temp_32 & 0x0000f000 ;
			temp_32 = (temp_32 >> 14);
			mmc_sd_card->CSD_V2.WRITE_BLK_MISALIGN= temp_32 & 0x1;

			temp_32 = DWSWAP(respValue[1]);
			temp_32 = temp_32 & 0x0000f000 ;
			temp_32 = (temp_32 >> 13);
			mmc_sd_card->CSD_V2.READ_BLK_MISALIGN= temp_32 & 0x1;

			temp_32 = DWSWAP(respValue[1]);
			temp_32 = temp_32 & 0x0000f000 ;
			temp_32 = (temp_32 >> 12);
			mmc_sd_card->CSD_V2.DSR_IMP= temp_32 & 0x1;

			temp_32 = DWSWAP(respValue[1]);
			temp_32 = temp_32 & 0x0000003f ;
			temp_32 = (temp_32 << 16);
			mmc_sd_card->CSD_V2.C_SIZE= temp_32 & 0x3F0000;

			temp_32 = DWSWAP(respValue[2]);
			temp_32 = temp_32 & 0xffff0000 ;
			temp_32 = (temp_32 >> 16);
			mmc_sd_card->CSD_V2.C_SIZE = mmc_sd_card->CSD_V2.C_SIZE | (temp_32 & 0xffff);

			temp_32 = DWSWAP(respValue[2]);
			temp_32 = temp_32 & 0x00004000 ; //100000000000000
			temp_32 = (temp_32 >> 14);
			mmc_sd_card->CSD_V2.ERASE_BLK_EN = (temp_32 & 0x1);

			temp_32 = DWSWAP(respValue[2]);
			temp_32 = temp_32 & 0x00003f80 ; //011111110000000
			temp_32 = (temp_32 >> 7);
			mmc_sd_card->CSD_V2.SECTOR_SIZE= (temp_32 & 0x7f);

			temp_32 = DWSWAP(respValue[2]);
			temp_32 = temp_32 & 0x0000003f ; //011111110000000
			mmc_sd_card->CSD_V2.WP_GRP_SIZE= (temp_32 & 0x3f);

			temp_32 = DWSWAP(respValue[3]);
			temp_32 = temp_32 & 0x80000000 ;
			temp_32 = (temp_32 >> 31);
			mmc_sd_card->CSD_V2.WP_GRP_ENABLE = temp_32 & 0x01;

			temp_32 = DWSWAP(respValue[3]);
			temp_32 = temp_32 & 0x1c000000 ;
			temp_32 = (temp_32 >> 26);
			mmc_sd_card->CSD_V2.R2W_FACTOR= temp_32 & 0x07;

			temp_32 = DWSWAP(respValue[3]);
			temp_32 = temp_32 & 0x03c00000 ;
			temp_32 = (temp_32 >> 22);
			mmc_sd_card->CSD_V2.WRITE_BL_LEN= temp_32 & 0x0f;

			temp_32 = DWSWAP(respValue[3]);
			temp_32 = temp_32 & 0x00200000 ;
			temp_32 = (temp_32 >> 21);
			mmc_sd_card->CSD_V2.WRITE_BL_PARTIAL= temp_32 & 0x01;

			temp_32 = DWSWAP(respValue[3]);
			temp_32 = temp_32 & 0x00008000 ;
			temp_32 = (temp_32 >> 15);
			mmc_sd_card->CSD_V2.FILE_FORMAT_GRP= temp_32 & 0x01;

			temp_32 = DWSWAP(respValue[3]);
			temp_32 = temp_32 & 0x00004000 ;
			temp_32 = (temp_32 >> 14);
			mmc_sd_card->CSD_V2.COPY= temp_32 & 0x01;

			temp_32 = DWSWAP(respValue[3]);
			temp_32 = temp_32 & 0x00002000 ;
			temp_32 = (temp_32 >> 13);
			mmc_sd_card->CSD_V2.PERM_WRITE_PROTECT= temp_32 & 0x01;


			temp_32 = DWSWAP(respValue[3]);
			temp_32 = temp_32 & 0x00001000 ;
			temp_32 = (temp_32 >> 12);
			mmc_sd_card->CSD_V2.TMP_WRITE_PROTECT= temp_32 & 0x01;

			temp_32 = DWSWAP(respValue[3]);
			temp_32 = temp_32 & 0x00000c00 ;
			temp_32 = (temp_32 >> 10);
			mmc_sd_card->CSD_V2.FILE_FORMAT= temp_32 & 0x03;

			temp_32 = DWSWAP(respValue[3]);
			temp_32 = temp_32 & 0x000000fe ;
			temp_32 = (temp_32 >> 1);
			mmc_sd_card->CSD_V2.CRC= temp_32 & 0x7f;

			card_memory_capacity = (mmc_sd_card->CSD_V2.C_SIZE+1) *512 ;
			mmc_sd_card->card_capacity_bytes = card_memory_capacity * 1024;
			mmc_sd_card->no_blocks = (mmc_sd_card->CSD_V2.C_SIZE+1) * 1024;  /* ?? no of blocks in SDSC card */
			mmc_sd_card->bytes_per_block = 512;
		}
	}
}
