/*
 * SDMMC.c
 *
 *  Created on: Mar 29, 2011
 *      Author: Peter
 */
#include "sdmmc.h"
//#include "lpc214x.h"
#include "rtpstr.h"
#include "rtpprint.h"

#define DEBUG_SDCARD_DRIVER       0
#define SD_FORCE_MAX_BLOCK_COUNT_WRITE 1
#define SD_FORCE_MAX_BLOCK_COUNT_READ 1
#define MAXSDCARDS 1

#define DWSWAP(X) X


static int RtSdcard_Driver_Initialized;
static SD_CARD_INFO mmc_sd_cards[MAXSDCARDS];

int RtSdBSP_Controller_Init(void);
void RtSdBSP_Delay_micros(unsigned long delay);
void RtSdBSP_Set_Clock(int rate );
int RtSdBSP_Set_BusWidth(SD_CARD_INFO *pmmc_sd_card, int width );
void RtSdBSP_CardInit( SD_CARD_INFO * pmmc_sd_card );
int RtSdBSP_SendCmd( unsigned long  CmdIndex, unsigned long  Argument, int  ExpectResp, int  AllowTimeout );
int RtSdBSP_GetCmdResp( int ExpectCmdData, int ExpectResp, unsigned long * CmdResp );
void RtSdBSP_ClearStatus(SD_CARD_INFO * mmc_sd_card,unsigned long clear_mask);
int RtSdBSP_Write_Block(SD_CARD_INFO * mmc_sd_card, unsigned long blockNum);
int RtSdBSP_Read_Block(SD_CARD_INFO * mmc_sd_card,unsigned long blockNum);
int RtSdBSP_Read_SCR(SD_CARD_INFO * mmc_sd_card);
void RtSdBSP_CardPushPullMode( SD_CARD_INFO * pmmc_sd_card );
int RtSdBSP_Read_BlockSetup(SD_CARD_INFO * mmc_sd_card,unsigned long blockNum);
int RtSdBSP_Read_BlockTransfer(SD_CARD_INFO * mmc_sd_card);
void RtSdBSP_Read_BlockShutdown(SD_CARD_INFO * mmc_sd_card);
int RtSdBSP_Write_BlockSetup(SD_CARD_INFO * mmc_sd_card,unsigned long blockNum);
void RtSdBSP_Write_BlockShutdown(SD_CARD_INFO * mmc_sd_card);
int RtSdBSP_Write_BlockTransfer(SD_CARD_INFO * mmc_sd_card);



static int RtSdcard_Media_Init(SD_CARD_INFO *pmmc_sd_card);
static int RtSdcard_Check_CID( SD_CARD_INFO * mmc_sd_card);
static int RtSdcard_Send_CSD( SD_CARD_INFO * mmc_sd_card);
static int RtSdcard_Set_Address(SD_CARD_INFO * mmc_sd_card);
static int RtSdcard_Select_Card(SD_CARD_INFO * mmc_sd_card, int is_specific_card);
static void RtSdcard_Display_Media_Type(SD_CARD_INFO *pmmc_sd_card);
static int RtSdcard_CardInit( SD_CARD_INFO * mmc_sd_card );
static int RtSdcard_Go_Idle_State( SD_CARD_INFO *pmmc_sd_card );
static int RtSdcard_Send_OP_Cond( SD_CARD_INFO *pmmc_sd_card );
static int RtSdcard_Send_ACMD_OP_Cond( SD_CARD_INFO *pmmc_sd_card );
static int RtSdcard_Send_ACMD(  SD_CARD_INFO *pmmc_sd_card );
static  int RtSdcard_Send_If_Cond( SD_CARD_INFO * mmc_sd_card );
static void parse_CID(SD_CARD_INFO * mmc_sd_card, unsigned long *respValue);
static void parse_CSD(SD_CARD_INFO * mmc_sd_card, unsigned long *respValue);
static void RtSdcard_Switch_Voltage(SD_CARD_INFO *pmmc_sd_card);
static int RtSdcard_Send_ACMD_Disconnect(SD_CARD_INFO *pmmc_sd_card);
int RtSdcard_Send_ACMD_SCR(SD_CARD_INFO *pmmc_sd_card );
static int RtSdcard_Send_ACMD_Bus_Width(SD_CARD_INFO *pmmc_sd_card, int bus_width);
static int RtSdcard_Set_BusWidth(SD_CARD_INFO *pmmc_sd_card, int buswidthflag); //MA


int RtSdcard_init(int unitnumber)                            /* Top level interface */
{
    if (unitnumber>=MAXSDCARDS)
        return -1;

    if (!RtSdcard_Driver_Initialized)
    {
        int i;
        rtp_memset(&mmc_sd_cards[0], 0, sizeof(mmc_sd_cards));
        for (i = 0; i < MAXSDCARDS; i++)
        {
            mmc_sd_cards[i].card_operating_flags= 0;
            mmc_sd_cards[i].card_type = CARD_UNKNOWN_TYPE;
            mmc_sd_cards[i].state = INACTIVE_STATE;
        }
        if (RtSdBSP_Controller_Init()!=0) /* Target specific initialization (pinconfig, clocks etc) */
            return -1;
        RtSdcard_Driver_Initialized = 1;
    }
    if (RtSdcard_Media_Init(&mmc_sd_cards[unitnumber])!=0)  /* Standard device independent initialization */
        return -1;

    return 0;


}

int RtSdcard_device_media_parms(int unitnumber, unsigned long *nSectors, unsigned long *BytesPerSector, int *isReadOnly)
{
    if (unitnumber>=MAXSDCARDS)
        return -1;
    *isReadOnly = 0;
    *nSectors =    mmc_sd_cards[unitnumber].no_blocks;
    *BytesPerSector = mmc_sd_cards[unitnumber].bytes_per_block;
    return 0;
}


static int RtSdcard_Media_Init(SD_CARD_INFO *pmmc_sd_card)
{
    pmmc_sd_card->card_type = CARD_UNKNOWN_TYPE;
    pmmc_sd_card->card_operating_flags= 0;
    pmmc_sd_card->state = INACTIVE_STATE;

    if (RtSdcard_CardInit( pmmc_sd_card ) != 0)
        return -1;

    RtSdcard_Display_Media_Type(pmmc_sd_card);

    if( pmmc_sd_card->card_type == CARD_UNKNOWN_TYPE)
        return -1; //for now return, no card is inserted, or we don't support that card
    pmmc_sd_card->card_operating_flags |= SDFLGINSERTED;


    RtSdcard_Switch_Voltage(pmmc_sd_card);         /* Switch to 1.8 volts if supported by the controller */

    /*We do this for each card we have, on the Olemix board we only have one SD card supported, if we have
     * more (i.e. have more than one CMD line for the SD interface, then we loop throught these, and issue
     * CMD2 followed by CMD3 for each card) */

    /* Send CMD2 */

    if (RtSdcard_Check_CID( pmmc_sd_card) != 0)         /* Check Card information description, manufacturer et al. */
        goto CARD_INITIALIZATION_FAILED;

    if (RtSdcard_Set_Address(pmmc_sd_card) != 0)     /* Set card address for this device */
        goto CARD_INITIALIZATION_FAILED;


    if (RtSdcard_Send_CSD(pmmc_sd_card) != 0)       /* Check card specific data (operating range, capacity etc.) */
        goto CARD_INITIALIZATION_FAILED;

    if (RtSdcard_Select_Card(pmmc_sd_card, 1) != 0)  /* Switch card to xfer state */
        goto CARD_INITIALIZATION_FAILED;

    RtSdBSP_CardPushPullMode(pmmc_sd_card );		/* Transfer the rest in push pull mode */


    if (RtSdcard_Send_ACMD_Disconnect(pmmc_sd_card) != 0)  /* Disconnect pullup resistor so it can enter hd mode */
        goto CARD_INITIALIZATION_FAILED;
    /* send SCR , can easily removed*/
    //if (RtSdcard_Send_ACMD_SCR(pmmc_sd_card ) != 0)
    //    goto CARD_INITIALIZATION_FAILED;

	/* Sets up
		SDFLGMULTIBLOCK
		SDFLGBLOCKCOUNT
		SDFLGHIGHSPEED
		SDFLG4BITMODE
	*/
    if (RtSdBSP_Read_SCR(pmmc_sd_card)!=0)
        goto CARD_INITIALIZATION_FAILED;

    if (( pmmc_sd_card->card_type == SD_CARD_TYPE ) || ( pmmc_sd_card->card_type == SDHC_CARD_TYPE ) || ( pmmc_sd_card->card_type == SDXC_CARD_TYPE ))
    {
    	if (pmmc_sd_card->card_operating_flags&SDFLGHIGHSPEED)
          	RtSdBSP_Set_Clock(1);
        else
          	RtSdBSP_Set_Clock(0);       // TBD do we have to do this a second time ?

        if (pmmc_sd_card->card_operating_flags&SDFLG4BITMODE)
        {
      		if (RtSdcard_Set_BusWidth( pmmc_sd_card, SD_4_BIT ) != 0)
            	goto CARD_INITIALIZATION_FAILED;         /* fatal error */
        }
    }

    return 0;
CARD_INITIALIZATION_FAILED:
    rtp_printf("Card initialization failed\n");
    pmmc_sd_card->card_type = CARD_UNKNOWN_TYPE;
    pmmc_sd_card->card_operating_flags = 0;
    pmmc_sd_card->state = INACTIVE_STATE;
    return -1;
}


static void RtSdcard_Display_Media_Type(SD_CARD_INFO *pmmc_sd_card)
{
    if( pmmc_sd_card->card_type == CARD_UNKNOWN_TYPE)
    {
        rtp_printf("Card Type is Unknown, or no card is inserted... need to check how to check for inserted card \n \r");
    }
    else if(pmmc_sd_card->card_type == MMC_CARD_TYPE )
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
}


/******************************************************************************
** Function name:        RtSdcard_Set_BusWidth
**
** Descriptions:        1-bit bus or 4-bit bus.
**
** parameters:            bus width
** Returned value:        TRUE or FALSE
**
******************************************************************************/
static int RtSdcard_Set_BusWidth(SD_CARD_INFO *pmmc_sd_card, int width )
{

    RtSdBSP_Set_BusWidth(pmmc_sd_card,width);
    if ( width == SD_4_BIT )
    {
        return RtSdcard_Send_ACMD_Bus_Width(pmmc_sd_card, 0x2 );
    }
    return 0;
}


/******************************************************************************
** Function name:        RtSdcard_CardInit
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
  RtSdBSP_CardInit( pmmc_sd_card );
  /* Try CMD1 first for MMC, if it's timeout, try CMD55 and CMD41 for SD, if both failed, initialization faliure, bailout. */

  /*MCI_SendCmd( SEND_OP_COND, OCR_INDEX, EXPECT_SHORT_RESP, 0 ); */
  if(RtSdcard_Send_If_Cond(pmmc_sd_card) == 0 )////SDXC condition
  {
      RtSdBSP_Delay_micros(30000);
      pmmc_sd_card->card_type = SDHC_CARD_TYPE;
      return RtSdcard_Send_ACMD_OP_Cond( pmmc_sd_card);
  }
  if (RtSdcard_Go_Idle_State(pmmc_sd_card) == -1)
      return -1;
  if ( RtSdcard_Send_OP_Cond(pmmc_sd_card) == 0)
  {
      pmmc_sd_card->card_type = MMC_CARD_TYPE;
      return 0;    /* Found the card, it's a MMC */
  }
  else
  {
      if ( RtSdcard_Send_ACMD_OP_Cond(pmmc_sd_card) == 0)
      {
          pmmc_sd_card->card_type = SD_CARD_TYPE;
          return 0;    /* Found the card, it's a SD */
      }
  }
  return -1;
}



static int RtSdcard_Go_Idle_State( SD_CARD_INFO *pmmc_sd_card  )
{
int retries;
unsigned long respStatus;
unsigned long  respValue[4];

  for (retries = 0; retries < 32; retries++)
  {
    /* Send CMD0 command repeatedly until the response is back correctly */
    RtSdBSP_SendCmd( GO_IDLE_STATE, 0x00000000, EXPECT_NO_RESP, 0 );
    respStatus = RtSdBSP_GetCmdResp(GO_IDLE_STATE, EXPECT_NO_RESP, &respValue[0] );
    if (respStatus == 0 )
    {
        pmmc_sd_card->state = IDLE_STATE;
        return 0;
    }
  }
  return -1;
}


static int RtSdcard_Send_OP_Cond( SD_CARD_INFO *pmmc_sd_card )
{
int retries;
int respStatus;
unsigned long respValue[4];

  for (retries = 0; retries < 0x200; retries++)
  {
    /* Send CMD1 command repeatedly until the response is back correctly */
    RtSdBSP_SendCmd( SEND_OP_COND, OCR_INDEX, EXPECT_SHORT_RESP, 0 );
    respStatus = RtSdBSP_GetCmdResp( SEND_OP_COND, EXPECT_SHORT_RESP, &respValue[0] );
    /* bit 0 and bit 2 must be zero, or it's timeout or CRC error */
    if ( (respStatus == 0) && (respValue[0] & 0x80000000) )
      return  0;    /* response is back and correct. */
    RtSdBSP_Delay_micros(100);
  }
  return -1;
}


static int RtSdcard_Send_ACMD_OP_Cond( SD_CARD_INFO *pmmc_sd_card )
{
int retries;
int respStatus;
unsigned long respValue[4];

  /* timeout on SEND_OP_COND command on MMC, now, try SEND_APP_OP_COND
  command to SD */
  for (retries = 0; retries < 512; retries++)
  {
    RtSdBSP_Delay_micros(3000);

    if ( RtSdcard_Send_ACMD(pmmc_sd_card ) == -1)//??
    {
        continue;
      }

    /* Send ACMD41 command repeatedly until the response is back correctly */
    RtSdBSP_SendCmd( SEND_APP_OP_COND, OCR_INDEX_SDHC_SDHX, EXPECT_SHORT_RESP, 0 );

    respStatus = RtSdBSP_GetCmdResp( SEND_APP_OP_COND, EXPECT_SHORT_RESP, &respValue[0] );

    if ( (respStatus == 0) && (respValue[0] & 0x80000000) ) /* TODO:Check the response of SD, SDHC and SHXC and compare against the return value for each, SDHC and SDXC will have the CCS bit set,
                                                               as well as the bit 31 */
    {
        pmmc_sd_card->state = READY_STATE;
        return 0;    /* response is back and correct. */
    }
    RtSdBSP_Delay_micros(200);
  }
  return -1;
}


/******************************************************************************
** Function name:        RtSdcard_Send_ACMD
**
** Descriptions:
**
** parameters:
** Returned value:
**
******************************************************************************/

static int RtSdcard_Send_ACMD(  SD_CARD_INFO *pmmc_sd_card )
{
int retries;
unsigned long CmdArgument;
int respStatus;
unsigned long respValue[4];

  if ( (pmmc_sd_card->card_type ==  SD_CARD_TYPE) || (pmmc_sd_card->card_type == SDHC_CARD_TYPE) || (pmmc_sd_card->card_type == SDXC_CARD_TYPE) )
  {
    CmdArgument = pmmc_sd_card->RCA;    /* Use the address from SET_RELATIVE_ADDR cmd */
  }
  else            /* if MMC or unknown card type, use 0x0. */
  {
    CmdArgument = 0x00000000;
  }

  for (retries = 0; retries < 32; retries++)
  {
    /* Send CMD55 command followed by an ACMD */
    RtSdBSP_SendCmd( APP_CMD, CmdArgument, EXPECT_SHORT_RESP, 0 );
    respStatus = RtSdBSP_GetCmdResp( APP_CMD, EXPECT_SHORT_RESP, &respValue[0] );
    if ( respStatus==0 && (respValue[0] & CARD_STATUS_ACMD_ENABLE) )    /* Check if APP_CMD enabled */
      return 0;
    RtSdBSP_Delay_micros(200);
  }
  return -1;
}

/******************************************************************************
** Function name:        RtSdcard_Send_ACMD_Bus_Width
**
** Descriptions:        ACMD6, SET_BUS_WIDTH, if it's SD card, we can
**                        use the 4-bit bus instead of 1-bit. This cmd
**                        can only be called during TRANS state.
**                        Since it's a ACMD, CMD55 APP_CMD needs to be
**                        sent out first.
**
** parameters:            Bus width value, 1-bit is 0, 4-bit is 10
** Returned value:        true or false, true if the card is still in the
**                        TRANS state after the cmd.
**
******************************************************************************/

static int RtSdcard_Send_ACMD_Bus_Width(SD_CARD_INFO *pmmc_sd_card, int buswidth )
{
int retries;
unsigned long respStatus;
unsigned long  respValue[4];

    for (retries = 0; retries < 32; retries++)
    {
        if ( RtSdcard_Send_ACMD(pmmc_sd_card) == -1 )
        	continue;
        /* Send ACMD6 command to set the bus width */
        RtSdBSP_SendCmd( SET_ACMD_BUS_WIDTH, buswidth, EXPECT_SHORT_RESP, 0 );
        respStatus = RtSdBSP_GetCmdResp( SET_ACMD_BUS_WIDTH, EXPECT_SHORT_RESP, &respValue[0] );
        if ( respStatus==0 && ((respValue[0] & (0x0F << 8)) == 0x0900) )
            return 0;    /* response is back and correct. */
        RtSdBSP_Delay_micros(200);
    }
    return -1;
}


/*****************************************************************************
** Function name:        RtSdcard_Send_Status
**
** Descriptions:        CMD13, SEND_STATUS, the most important cmd to
**                        debug the state machine of the card.
**
** parameters:            None
** Returned value:        Response value(card status), true if the ready bit
**                        is set in the card status register, if timeout, return
**                        INVALID_RESPONSE 0xFFFFFFFF.
**
******************************************************************************/
/* CMD13 */
static unsigned long RtSdcard_Send_Status( SD_CARD_INFO * mmc_sd_card )
{
int retries;
int respStatus;
unsigned long respValue[4];
unsigned long CmdArgument;

 if (( mmc_sd_card->card_type == SD_CARD_TYPE ) || ( mmc_sd_card->card_type == SDHC_CARD_TYPE ) || ( mmc_sd_card->card_type == SDXC_CARD_TYPE ))
    CmdArgument = mmc_sd_card->RCA;
 else            /* if MMC or unknown card type, use default RCA addr. */
    CmdArgument = 0x00010000;

  /* Note that, since it's called after the block write and read, this timeout
  is important based on the clock you set for the data communication. */
  for (retries = 0; retries < 0x2000; retries++)
  {
    RtSdBSP_ClearStatus( mmc_sd_card,(MCI_CMD_TIMEOUT | MCI_CMD_CRC_FAIL | MCI_CMD_RESP_END));
    /* Send SELECT_CARD command before read and write */
    RtSdBSP_SendCmd( SEND_STATUS, CmdArgument, EXPECT_SHORT_RESP, 0 );
    respStatus = RtSdBSP_GetCmdResp( SEND_STATUS, EXPECT_SHORT_RESP, &respValue[0] );
    if ( respStatus==0 && (respValue[0] & (1 << 8)) )
    { /* The ready bit should be set, it should be in either TRAN or RCV state now */
      return ( respValue[0] );
    }
    RtSdBSP_Delay_micros(200);
  }
  return ( INVALID_RESPONSE );
}


/*****************************************************************************
** Function name:        RtSdcard_Check_CID
**
** Descriptions:
**
** parameters:
** Returned value:
**
******************************************************************************/


static int RtSdcard_Check_CID( SD_CARD_INFO * mmc_sd_card)
{
int retries;
int respStatus;
unsigned long respValue[4];

  /* This command is normally after CMD1(MMC) or ACMD41(SD) or CMD11 if board supports 1.8V signaling and SDHC/SDXC */
  for (retries = 0; retries < 0x20; retries++)
  {
    /* Send CMD2 command repeatedly until the response is back correctly */
    RtSdBSP_SendCmd( ALL_SEND_CID, 0, EXPECT_LONG_RESP, 0 );
    respStatus = RtSdBSP_GetCmdResp( ALL_SEND_CID, EXPECT_LONG_RESP, &respValue[0] );
    /* bit 0 and bit 2 must be zero, or it's timeout or CRC error */
    if ( respStatus == 0)
    {
      parse_CID(mmc_sd_card, &respValue[0]);
      mmc_sd_card->state = IDENTIFICATION_STATE;
      return 0;    /* response is back and correct. */
    }
    RtSdBSP_Delay_micros(200);
  }
  return -1;
}

/******************************************************************************
** Function name:        RtSdcard_Set_Address
**
** Descriptions:        Send CMD3, STE_RELATIVE_ADDR, should after CMD2
**
** parameters:
** Returned value:        TRUE if response is back before timeout.
**
******************************************************************************/
static int RtSdcard_Set_Address( SD_CARD_INFO * mmc_sd_card )
{
int retries;
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
    else            /* If it's unknown or MMC_CARD, fix the RCA address */
    {
        CmdArgument = 0x00010000;
    }
    for (retries = 0; retries < 0x20; retries++)
    {
        /* Send CMD3 command repeatedly until the response is back correctly */
        RtSdBSP_SendCmd( SET_RELATIVE_ADDR, CmdArgument, EXPECT_SHORT_RESP, 0 );
        respStatus = RtSdBSP_GetCmdResp( SET_RELATIVE_ADDR, EXPECT_SHORT_RESP, &respValue[0] );
        /* bit 0 and bit 2 must be zero, or it's timeout or CRC error */
        /* It should go to IDEN state and bit 8 should be 1 */
        /* The second condition ((respValue[0] & (0x0F << 8)) == 0x0700) ) is added if we alreay issued CMD3 and then reissued it to the same card,
         * it should be ok, the only difference the first time we issue CMD3 we were in iden state and we go to stdby, second case we are in stdby
         * and stay @ stdby */
        if ( respStatus ==0 && (((respValue[0] & (0x0F << 8)) == 0x0500) || ((respValue[0] & (0x0F << 8)) == 0x0700) ) )
        {
            mmc_sd_card->RCA = respValue[0] & 0xffff0000;
            if(mmc_sd_card->state == IDENTIFICATION_STATE || (mmc_sd_card->state == STANDBY_STATE) )
                mmc_sd_card->state = STANDBY_STATE;
            return 0;    /* response is back and correct. */
        }
        RtSdBSP_Delay_micros(200);
    }
    return -1;
}


/* CMD9 */

/******************************************************************************
** Function name:        RtSdcard_Send_CSD
**
** Descriptions:
**
** parameters:
** Returned value:
**
******************************************************************************/

static int RtSdcard_Send_CSD( SD_CARD_INFO * mmc_sd_card )
{
int retries;
int respStatus;
unsigned long respValue[4];
unsigned long CmdArgument;

    if (( mmc_sd_card->card_type == SD_CARD_TYPE ) || ( mmc_sd_card->card_type == SDHC_CARD_TYPE ) || ( mmc_sd_card->card_type == SDXC_CARD_TYPE ))
        CmdArgument = mmc_sd_card->RCA;
    else            /* if MMC or unknown card type, use default RCA addr. */
        CmdArgument = 0x00010000;

    for (retries = 0; retries < 0x20; retries++)
    {
        /* Send SET_BLOCK_LEN command before read and write */
        /* Send SELECT_CARD command before read and write */
    	RtSdBSP_ClearStatus( mmc_sd_card,(MCI_CMD_TIMEOUT | MCI_CMD_CRC_FAIL | MCI_CMD_RESP_END));
        RtSdBSP_SendCmd( SEND_CSD, CmdArgument, EXPECT_LONG_RESP, 0 );
        respStatus = RtSdBSP_GetCmdResp( SEND_CSD, EXPECT_LONG_RESP, &respValue[0] );
        if ( !respStatus )
        {
            parse_CSD(mmc_sd_card, &respValue[0]);
            return 0;
        }
        RtSdBSP_Delay_micros(200);
    }
    return -1;
}



/* CMD7 */
/******************************************************************************
** Function name:        RtSdcard_Select_Card
**
** Descriptions:
**
** parameters:
** Returned value:
**
******************************************************************************/

static int RtSdcard_Select_Card( SD_CARD_INFO * mmc_sd_card , int is_specific_card)
{
int retries;
int respStatus;
unsigned long respValue[4];
int CmdArgument;

    if(is_specific_card)
    {
        if ( ( mmc_sd_card->card_type == SD_CARD_TYPE ) || ( mmc_sd_card->card_type == SDHC_CARD_TYPE ) || ( mmc_sd_card->card_type == SDXC_CARD_TYPE ) )
        {
            CmdArgument = mmc_sd_card->RCA;
        }
        else            /* if MMC or unknown card type, use default RCA addr. */
        {
            CmdArgument = 0x00010000;
        }
    }
    else
        CmdArgument =0;

  for (retries = 0; retries < 0x20; retries++)
  {
    /* Send SELECT_CARD command before read and write */
      RtSdBSP_ClearStatus( mmc_sd_card,(MCI_CMD_TIMEOUT | MCI_CMD_CRC_FAIL | MCI_CMD_RESP_END));
      RtSdBSP_SendCmd( SELECT_CARD, CmdArgument, EXPECT_SHORT_RESP, 0 );
      respStatus = RtSdBSP_GetCmdResp( SELECT_CARD, EXPECT_SHORT_RESP, &respValue[0] );
      if ( (respStatus==0) && ((respValue[0] & (0x0F << 8)) == 0x0700 ))
        {                        /* Should be in STANDBY state now and ready */
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
      RtSdBSP_Delay_micros(200);
       }
  return -1;
}

/******************************************************************************
** Function name:        RtSdcard_Send_BlockCount
**
** Descriptions:        CMD23, SET_BLOCK_COUNT
**
** parameters:
** Returned value:        true or false, true if, at least, the card status
**                        shows ready bit is set.
**
******************************************************************************/
/* CMD23 */
int RtSdcard_Send_BlockCount( SD_CARD_INFO * mmc_sd_card, unsigned long block_count )
{
int retries;
unsigned long respStatus;
unsigned long respValue[4];

    for (retries = 0; retries < 0x20; retries++)
    {
      RtSdBSP_ClearStatus( mmc_sd_card,MCI_CLEAR_ALL);
      RtSdBSP_SendCmd( SET_BLOCK_COUNT, block_count, EXPECT_SHORT_RESP, 1 );
      respStatus = RtSdBSP_GetCmdResp( SET_BLOCK_COUNT, EXPECT_SHORT_RESP, &respValue[0] );
      /* ready bit, bit 8, should be set in the card status register */
      if ( !respStatus && (respValue[0] & (1 << 8)) )
          return 0;
      RtSdBSP_Delay_micros(200);
    }
    return -1;
}

/******************************************************************************
** Function name:        RtSdcard_Send_Stop
**
** Descriptions:        CMD12, STOP_TRANSMISSION. if that happens, the card is
**                        maybe in a unknown state that need a warm reset.
**
** parameters:
** Returned value:        true or false, true if, at least, the card status
**                        shows ready bit is set.
**
******************************************************************************/
/* CMD12 MCI SEND_STOP */
int RtSdcard_Send_Stop( SD_CARD_INFO * mmc_sd_card )
{
int retries;
unsigned long respStatus;
unsigned long respValue[4];

    for (retries = 0; retries < 0x20; retries++)
    {
      RtSdBSP_ClearStatus( mmc_sd_card,MCI_CLEAR_ALL);
      RtSdBSP_SendCmd( STOP_TRANSMISSION, 0x00000000, EXPECT_SHORT_RESP, 0 );
      respStatus = RtSdBSP_GetCmdResp( STOP_TRANSMISSION, EXPECT_SHORT_RESP, &respValue[0] );
      /* ready bit, bit 8, should be set in the card status register */
      if ( !respStatus && (respValue[0] & (1 << 8)) )
          return 0;
      RtSdBSP_Delay_micros(200);
    }
    return -1;
}


/******************************************************************************
** Function name:        RtSdcard_Send_Read_Block
**
** Descriptions:        CMD17, READ_SINGLE_BLOCK, send this cmd in the TRANS
**                        state to read a block of data from the card.
**
** parameters:            ,block number
** Returned value:        Response value
**
******************************************************************************/
/* CMD17 READ_SINGLE_BLOCK */

int RtSdcard_Send_Read_Block( SD_CARD_INFO * mmc_sd_card , unsigned long blockNum, unsigned long blk_len )
{
int retries;
int respStatus;
unsigned long respValue[4];
unsigned long block_arg;
    /* HC and XC cards use sector orietend addressing, early versions use bytes */
    if ( ( mmc_sd_card->card_type == SDHC_CARD_TYPE ) ||( mmc_sd_card->card_type == SDXC_CARD_TYPE ) )
            block_arg = blockNum;
    else /* if ( mmc_sd_card->card_type == SD_CARD_TYPE ) */
        block_arg = blockNum * BLOCK_LENGTH;
    for (retries = 0; retries < 0x20; retries++)
    {
      	RtSdBSP_ClearStatus( mmc_sd_card,MCI_CLEAR_ALL);
        RtSdBSP_SendCmd( READ_SINGLE_BLOCK, block_arg, EXPECT_SHORT_RESP, 0 );
        respStatus = RtSdBSP_GetCmdResp( READ_SINGLE_BLOCK, EXPECT_SHORT_RESP, &respValue[0] );
        /* it should be in the transfer state, bit 9~12 is 0x0100 and bit 8 is 1 */
        if ( respStatus==0 && ((respValue[0] & (0x0F << 8)) == 0x0900) )
        {
            return 0;            /* ready and in TRAN state */
            // PVOPVO - Do state transition
        }
        RtSdBSP_Delay_micros(200);
    }
    return -1;                    /* Fatal error */
}

/* CMD 24 */
int RtSdcard_Send_Write_Block( SD_CARD_INFO * mmc_sd_card, unsigned long blockNum)
{
int retries;
int respStatus;
unsigned long respValue[4];
unsigned long block_arg;
    /* HC and XC cards use sector orietend addressing, early versions use bytes */
    if ( ( mmc_sd_card->card_type == SDHC_CARD_TYPE ) ||( mmc_sd_card->card_type == SDXC_CARD_TYPE ) )
            block_arg = blockNum;
    else /* if ( mmc_sd_card->card_type == SD_CARD_TYPE ) */
        block_arg = blockNum * BLOCK_LENGTH;
  for (retries = 0; retries < 0x20; retries++)
  {
      RtSdBSP_ClearStatus( mmc_sd_card,MCI_CLEAR_ALL);
      RtSdBSP_SendCmd( WRITE_BLOCK, block_arg, EXPECT_SHORT_RESP, 0 );
      respStatus = RtSdBSP_GetCmdResp( WRITE_BLOCK, EXPECT_SHORT_RESP, &respValue[0] );
    /* it should be in the transfer state, bit 9~12 is 0x0100 and bit 8 is 1 */
    if ( !respStatus && ((respValue[0] & (0x0F << 8)) == 0x0900) )
    {
      return 0;            /* ready and in TRAN state */
      // PVOPVO - Do state transition
    }

    RtSdBSP_Delay_micros(200);
  }
  return -1;                /* Fatal error */
}



/* SEND_IF_COND */
static int RtSdcard_Send_If_Cond( SD_CARD_INFO * mmc_sd_card ) /* SEND_IF_COND does not take arguments ?? */
{
int retries;
int respStatus;
unsigned long respValue[4];

    for (retries = 0; retries < 0x200; retries++)
    {
        /* CMD8 */
        RtSdBSP_SendCmd( SEND_IF_COND, SEND_IF_COND_ARG, EXPECT_SHORT_RESP, 0 );
        respStatus = RtSdBSP_GetCmdResp( SEND_IF_COND, EXPECT_SHORT_RESP, &respValue[0] );
        if ( (respStatus==0) && (respValue[0] & 0x1AA) )
        {
            mmc_sd_card->card_type = SDHC_CARD_TYPE;
            mmc_sd_card->state = IDLE_STATE;
            return 0;    /* response is back and correct. */
        }
        RtSdBSP_Delay_micros(200);
    }
     return -1;
}

/******************************************************************************
** Function name:        RtSdcard_Send_Read_Multiple_Block
**
** Descriptions:        CMD18, READ_MULTIPLE_BLOCK, continuously transfers data
**                         blocks from card to host until interrupted by STOP_TRA-
**                         NSMISSION (CMD12) command. block lenght specified the
**                         smae as READ_SINGLE_BLOCK command
**
** parameters:            block number/address
** Returned value:        Response value/R1
**
******************************************************************************/
int RtSdcard_Send_Read_Multiple_Block( SD_CARD_INFO * mmc_sd_card , unsigned long blockNum )
{
int retries;
int respStatus;
unsigned long respValue[4];
unsigned long block_arg;
    /* HC and XC cards use sector orietend addressing, early versions use bytes */
    if ( ( mmc_sd_card->card_type == SDHC_CARD_TYPE ) ||( mmc_sd_card->card_type == SDXC_CARD_TYPE ) )
        block_arg = blockNum;
    else /* if ( mmc_sd_card->card_type == SD_CARD_TYPE ) */
        block_arg = blockNum * BLOCK_LENGTH;
    for (retries = 0; retries < 0x20; retries++)
    {
      	RtSdBSP_ClearStatus( mmc_sd_card,MCI_CLEAR_ALL);
        RtSdBSP_SendCmd( READ_MULTIPLE_BLOCK, block_arg, EXPECT_SHORT_RESP, 0 );
        respStatus = RtSdBSP_GetCmdResp( READ_MULTIPLE_BLOCK, EXPECT_SHORT_RESP, &respValue[0] );

        /* it should be in the transfer state, bit 9~12 is 0x0100 and bit 8 is 1 */
        if ( !respStatus && ((respValue[0] & (0x0F << 8)) == 0x0900) )
        {
            return 0;            /* ready and in TRAN state */
        }
        RtSdBSP_Delay_micros(200);
    }
    return -1;                    /* Fatal error */
}


/* CMD 25 */
int RtSdcard_Send_Write_Multiple_Block( SD_CARD_INFO * mmc_sd_card, unsigned long blockNum)
{
int retries;
int respStatus;
unsigned long respValue[4];
unsigned long block_arg;
    /* HC and XC cards use sector orietend addressing, early versions use bytes */
    if ( ( mmc_sd_card->card_type == SDHC_CARD_TYPE ) ||( mmc_sd_card->card_type == SDXC_CARD_TYPE ) )
            block_arg = blockNum;
    else /* if ( mmc_sd_card->card_type == SD_CARD_TYPE ) */
        block_arg = blockNum * BLOCK_LENGTH;


  for (retries = 0; retries < 0x20; retries++)
  {
      RtSdBSP_ClearStatus( mmc_sd_card,MCI_CLEAR_ALL);
      RtSdBSP_SendCmd( WRITE_MULTIPLE_BLOCK, block_arg, EXPECT_SHORT_RESP, 0 );
      respStatus = RtSdBSP_GetCmdResp( WRITE_MULTIPLE_BLOCK, EXPECT_SHORT_RESP, &respValue[0] );
    /* it should be in the transfer state, bit 9~12 is 0x0100 and bit 8 is 1 */
    if ( !respStatus && ((respValue[0] & (0x0F << 8)) == 0x0900) )
    {
      return 0;            /* ready and in TRAN state */
      // PVOPVO - Do state transition
    }

    RtSdBSP_Delay_micros(200);
  }
  return -1;                /* Fatal error */
}

int RtSdcard_CheckStatus( SD_CARD_INFO * mmc_sd_card )  /*check this function */
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
** Function name:        RtSdcard_Write_Block
**
** Descriptions:        Set MMCSD data control register, data length and data
**                        timeout, send WRITE_BLOCK cmd, finally, enable
**                        interrupt. On completion of WRITE_BLOCK cmd, TX_ACTIVE
**                        interrupt will occurs, data can be written continuously
**                        into the FIFO until the block data length is reached.
**
** parameters:            block number
** Returned value:        true or false, if cmd times out, return false and no
**                        need to continue.
**
******************************************************************************/


static int _RtSdcard_DoWrite_Block(SD_CARD_INFO * mmc_sd_card,unsigned long blockNum);
unsigned long RtSdcard_Write_Block(int unitnumber, unsigned long blockNum, unsigned long blockCount, unsigned char *Buffer)
{
SD_CARD_INFO * mmc_sd_card;
unsigned long xferrbuff[128] ;
unsigned long *pBuffer;
int r;
    if (unitnumber>=MAXSDCARDS)
      return -1;
    mmc_sd_card = &mmc_sd_cards[unitnumber];

#if (SD_FORCE_MAX_BLOCK_COUNT_WRITE)
    blockCount=SD_FORCE_MAX_BLOCK_COUNT_WRITE;
#endif

    if ((unsigned long)Buffer & 0x3)
    {
        rtp_memcpy(&xferrbuff[0], Buffer, 512);
        pBuffer=(unsigned long *)&xferrbuff[0];
        blockCount=1;
    }
    else
        pBuffer = (unsigned long *)Buffer;
    mmc_sd_card->blk_buffer=(unsigned long *) pBuffer;
    mmc_sd_card->blocks_transfered=0;
    mmc_sd_card->blocks_to_transfer=blockCount;

#define  OLD_WRITE_METHOD 0
#if (OLD_WRITE_METHOD)
    r=RtSdBSP_Write_Block(mmc_sd_card,blockNum);
#else
    r=_RtSdcard_DoWrite_Block(mmc_sd_card, blockNum);
#endif
    if (r < 0)
    {
       rtp_printf("RtSdBSP_Write_Block failed\n");
	   return 0;
    }
    return mmc_sd_card->blocks_transfered;
}
static int _RtSdcard_DoWrite_Block(SD_CARD_INFO * mmc_sd_card,unsigned long blockNum)
{
    /* Make sure the card is in TRANS state card. */
    if (RtSdcard_CheckStatus(mmc_sd_card) != 0 )
    {
        RtSdcard_Send_Stop(mmc_sd_card);
        return -1;
    }
    if (RtSdBSP_Write_BlockSetup(mmc_sd_card,blockNum)!=0)
        return -1;
    /* In polled mode interrupts are disabled until RtSdBSP_Read_BlockShutdown */
	if (mmc_sd_card->card_operating_flags & SDFLGMULTIBLOCK)
	{
   		if (mmc_sd_card->card_operating_flags & SDFLGBLOCKCOUNT)
   		{
   			if (RtSdcard_Send_BlockCount( mmc_sd_card, mmc_sd_card->blocks_to_transfer) != 0)
        		goto error_return;
		}
		if (RtSdcard_Send_Write_Multiple_Block(mmc_sd_card, blockNum) != 0)
        	goto error_return;
	}
	else
	{
    	if (RtSdcard_Send_Write_Block( mmc_sd_card, blockNum) != 0)
        	goto error_return;
	}
	if (RtSdBSP_Write_BlockTransfer(mmc_sd_card)!=0)
        goto error_return;
	if (mmc_sd_card->card_operating_flags & SDFLGMULTIBLOCK)
	{
		if ((mmc_sd_card->card_operating_flags & SDFLGBLOCKCOUNT)==0)
		{
      		/* Stop the transfer */
      		RtSdcard_Send_Stop(mmc_sd_card);
		}
	}
	RtSdBSP_Write_BlockShutdown(mmc_sd_card);
    /* Check Status after Transfer to be sure it completed */
    if (RtSdcard_CheckStatus(mmc_sd_card) != 0 )
    {
    	rtp_printf("RtSdBSP_Write_Block: CheckStatus failed after transfer\n");
        RtSdcard_Send_Stop(mmc_sd_card);
        return -1;
    }
    return 0;
error_return:
    RtSdBSP_Write_BlockShutdown(mmc_sd_card);
    return -1;
}


static int _RtSdcard_DoRead_Block(SD_CARD_INFO * mmc_sd_card,unsigned long blockNum);

unsigned long RtSdcard_Read_Block(int unit_number, unsigned long blockNum,unsigned long blockCount, unsigned char *Buffer)
{
    SD_CARD_INFO * mmc_sd_card;
    int use_xferrbuff=0;
    unsigned long xferrbuff[128] ;
    unsigned char *pBbuffer=Buffer;
#if (SD_FORCE_MAX_BLOCK_COUNT_READ)
    blockCount=SD_FORCE_MAX_BLOCK_COUNT_READ;
#endif
    if ((unsigned long)pBbuffer & 0x3)
    {
        pBbuffer = (unsigned char *)&xferrbuff[0];
        use_xferrbuff=1;
        blockCount=1;
    }
    if (unit_number>=MAXSDCARDS)
        return 0;
    mmc_sd_card = &mmc_sd_cards[unit_number];
    mmc_sd_card->blk_buffer=(unsigned long *) pBbuffer;
    mmc_sd_card->blocks_transfered=0;
    mmc_sd_card->blocks_to_transfer=blockCount;
//#define  OLD_READ_METHOD 1
#ifdef OLD_READ_METHOD
    if (RtSdBSP_Read_Block(mmc_sd_card, blockNum)==-1)
    	return 0;
#else
    if (_RtSdcard_DoRead_Block(mmc_sd_card, blockNum)==-1)
    	return 0;
#endif
    if (use_xferrbuff)
    	rtp_memcpy(Buffer, &xferrbuff[0], 512);
   return mmc_sd_card->blocks_transfered;
}

static int _RtSdcard_DoRead_Block(SD_CARD_INFO * mmc_sd_card,unsigned long blockNum)
{
    /* Make sure the card is in TRANS state card. */
    if (RtSdcard_CheckStatus(mmc_sd_card) != 0 )
    {
        RtSdcard_Send_Stop(mmc_sd_card);
        return -1;
    }
    if (RtSdBSP_Read_BlockSetup(mmc_sd_card,blockNum)!=0)
        return -1;
    /* In polled mode interrupts are disabled until RtSdBSP_Read_BlockShutdown */
	if (mmc_sd_card->card_operating_flags & SDFLGMULTIBLOCK)
	{
   		if ((mmc_sd_card->card_operating_flags & SDFLGBLOCKCOUNT))
   		{
   			if (RtSdcard_Send_BlockCount( mmc_sd_card, mmc_sd_card->blocks_to_transfer) != 0)
        		goto error_return;
		}
		if (RtSdcard_Send_Read_Multiple_Block(mmc_sd_card, blockNum) != 0)
        	goto error_return;
	}
	else
	{
    	if (RtSdcard_Send_Read_Block( mmc_sd_card, blockNum, BLOCK_LENGTH ) != 0)
        	goto error_return;
	}
	if (RtSdBSP_Read_BlockTransfer(mmc_sd_card)!=0)
        goto error_return;
	if (mmc_sd_card->card_operating_flags & SDFLGMULTIBLOCK)
	{
		if ((mmc_sd_card->card_operating_flags & SDFLGBLOCKCOUNT)==0)
		{
      		/* Stop the transfer */
      		RtSdcard_Send_Stop(mmc_sd_card);
		}
	}
	RtSdBSP_Read_BlockShutdown(mmc_sd_card);
    return 0;
error_return:
    RtSdBSP_Read_BlockShutdown(mmc_sd_card);
    return -1;
}


static void RtSdcard_Switch_Voltage(SD_CARD_INFO *pmmc_sd_card)
{
    //send Command 11
    //have to check on the response of ACMD41 for SR18A (Bit 24 of the R3(OCR) )
#if(SUPPORT_1_8_VOLT)
    if( MCI_VoltageSwitch() == -1)
    {
        rtp_printf("Voltage switch to 1.8V failed?! \n \r");
    }
#endif
}

#if(SUPPORT_1_8_VOLT)
static int RtSdcard_VoltageSwitch( void )
{
    //using MCI_Go_Idle_State( void ) and MCI_Send_Status (void) to prototype
    int retries;
    int respStatus;
    unsigned long respValue[4];
    unsigned long cmd_index=0;
  for (retries = 0; retries < 0x200; retries++)
  {
    /* Send CMD11 command repeatedly until the response is back correctly */
      RtSdBSP_ClearStatus(mmc_sd_card,(MCI_CMD_TIMEOUT | MCI_CMD_CRC_FAIL | MCI_CMD_RESP_END);
      MCI_SendCmd( VOLTAGE_SWITCH, 0x00000000, EXPECT_SHORT_RESP, 0 );  //expected resonse R1
      respStatus = RtSdBSP_GetCmdResp( VOLTAGE_SWITCH, EXPECT_SHORT_RESP, (unsigned long *)&respValue[0] );
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
  }
  return -1;
}
#endif

int RtSdcard_Send_ACMD_SCR(SD_CARD_INFO *pmmc_sd_card )
{
int retries;
unsigned long respStatus;
unsigned long  respValue[4];

    for (retries = 0; retries < 0x20; retries++)
    {
        if ( RtSdcard_Send_ACMD(pmmc_sd_card) == -1 )
            continue;
        /* Send ACMD51 command to get the SCR register*/
        RtSdBSP_SendCmd( SEND_APP_SEND_SCR, 0x00000000, EXPECT_SHORT_RESP, 0 );
        respStatus = RtSdBSP_GetCmdResp( SEND_APP_SEND_SCR, EXPECT_SHORT_RESP, &respValue[0] );
        if ( respStatus==0 && ((respValue[0] & (0x0F << 8)) == 0x0900) )
            return 0;    /* response is back and correct. */
        RtSdBSP_Delay_micros(200);
    }
    return -1;
}


static int RtSdcard_Send_ACMD_Disconnect(SD_CARD_INFO *pmmc_sd_card)
{
int retries;
unsigned long respStatus;
unsigned long  respValue[4];

    for (retries = 0; retries < 0x20; retries++)
    {
        if ( RtSdcard_Send_ACMD(pmmc_sd_card) == -1 )
            continue;
        /* Send ACMD42 command to set/clear 50 ohm pullup 0 is disconnect */
        RtSdBSP_SendCmd( SEND_APP_OP_DISCONNECT, 0x00000000, EXPECT_SHORT_RESP, 0 );
        respStatus = RtSdBSP_GetCmdResp( SEND_APP_OP_DISCONNECT, EXPECT_SHORT_RESP, &respValue[0] );
        if ( respStatus==0 && ((respValue[0] & (0x0F << 8)) == 0x0900) )
            return 0;    /* response is back and correct. */
        RtSdBSP_Delay_micros(200);
    }
    return -1;
}




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
    mmc_sd_card->CID.PNM.PNM = temp_serial;

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
        {    block_len = 1;  /*pow(2,mmc_sd_card->CSD_V1.READ_BL_LEN) */
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
