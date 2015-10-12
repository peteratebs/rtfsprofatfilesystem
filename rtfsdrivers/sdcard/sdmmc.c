/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc 1987-2012
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* sdmc.c - Core device and file system independent functionality */

/*



*/

#define MAXSDCARDS 1 /* Do not change */

#include "sdmmc.h"
#include "rtpstr.h"
#include "rtpprint.h"

#define DEBUG_SDCARD_DRIVER       0
#if (ENABLE_DMA)
#define SD_FORCE_MAX_BLOCK_COUNT_WRITE  8
#define SD_FORCE_MAX_BLOCK_COUNT_READ   8
#else
#define SD_FORCE_MAX_BLOCK_COUNT_WRITE 0
#define SD_FORCE_MAX_BLOCK_COUNT_READ  0
#endif
#define DWSWAP(X) X

struct SdTransaction {
unsigned long  Index;
unsigned long  Argument;
#define SDFL_RESPONSE_0          1   /* No response expected */
#define SDFL_RESPONSE_48         2   /* 48  Bit response expected */
#define SDFL_RESPONSE_136        4   /* 136 bit response expected */
#define SDFL_COMMAND_INTERRUPT   8   /* Don't begin timeout processing until first response from card */
#define SDFL_SEND_ACMD          0x10    /* Application specific send ACCMD before command */
#define SDFL_COMMAND_PEND       0x20   /* Wait for CmdPend before sending */
unsigned long  Flags;
int MaxRetries;
unsigned long ResponseMask;            /* Success if (Response[0] & ResponseMask) == ResponseExpected) */
unsigned long ResponseExpected;
unsigned long  Response[5];          /* 5 long (160 bits) holds maximum response of 136 bits */
};

unsigned long respValue[4];

int RtSdBSP_SdTransaction( struct SdTransaction *cmd );

static int RtSdcard_Driver_Initialized;
SD_CARD_INFO mmc_sd_cards[MAXSDCARDS];

/* Device/controller dependent routines that must be supplied (see sdbsp.c) */
extern int RtSdBSP_Controller_Attach(void);
extern void RtSdBSP_Delay_micros(unsigned long delay);
extern void RtSdBSP_Set_Clock(int rate );
extern int RtSdBSP_Set_BusWidth(SD_CARD_INFO *pmmc_sd_card, int width );
extern void RtSdBSP_CardInit( SD_CARD_INFO * pmmc_sd_card );
extern int RtSdBSP_SendCmd( unsigned long  CmdIndex, unsigned long  Argument, int  ExpectResp, int  AllowTimeout );
extern int RtSdBSP_GetCmdResp(int ExpectCmdData, int ExpectResp, unsigned long * CmdResp );
extern int RtSdBSP_Read_SCR(SD_CARD_INFO *mmc_sd_card);
extern void RtSdBSP_CardPushPullMode( SD_CARD_INFO * pmmc_sd_card );
extern int RtSdBSP_Read_BlockSetup(SD_CARD_INFO * mmc_sd_card,unsigned long blockNum);
extern int RtSdBSP_Read_BlockTransfer(SD_CARD_INFO * mmc_sd_card);
extern void RtSdBSP_Read_BlockShutdown(SD_CARD_INFO * mmc_sd_card);
extern int RtSdBSP_Write_BlockSetup(SD_CARD_INFO * mmc_sd_card,unsigned long blockNum);
extern void RtSdBSP_Write_BlockShutdown(SD_CARD_INFO * mmc_sd_card);
extern int RtSdBSP_Write_BlockTransfer(SD_CARD_INFO * mmc_sd_card);
extern int RtSdBSP_Card_IsInstalled(int unit);
extern int RtSdBSP_Read_SCRSetup(SD_CARD_INFO * mmc_sd_card);
extern int RtSdBSP_Read_SCRTransfer(SD_CARD_INFO * mmc_sd_card,unsigned char *buf);
extern void RtSdBSP_Read_SCRShutdown(SD_CARD_INFO * mmc_sd_card);


static int RtSdcard_Media_Init(SD_CARD_INFO *pmmc_sd_card);
static int RtSdcard_Check_CID( SD_CARD_INFO * mmc_sd_card);
static int RtSdcard_Send_CSD( SD_CARD_INFO * mmc_sd_card);
static int RtSdcard_Set_Address(SD_CARD_INFO * mmc_sd_card);
static int RtSdcard_Select_Card(SD_CARD_INFO * mmc_sd_card, int is_specific_card);
static int RtSdcard_CardInit( SD_CARD_INFO * mmc_sd_card );
static int RtSdcard_Go_Idle_State( SD_CARD_INFO *pmmc_sd_card );
static int RtSdcard_Send_OP_Cond( SD_CARD_INFO *pmmc_sd_card );
static int RtSdcard_Send_ACMD_OP_Cond( SD_CARD_INFO *pmmc_sd_card );
int RtSdcard_Send_ACMD(  SD_CARD_INFO *pmmc_sd_card );
static  int RtSdcard_Send_If_Cond( SD_CARD_INFO * mmc_sd_card );
static void parse_CID(SD_CARD_INFO * mmc_sd_card, unsigned long *respValue);
static void parse_CSD(SD_CARD_INFO * mmc_sd_card, unsigned long *respValue);
static void RtSdcard_Switch_Voltage(SD_CARD_INFO *pmmc_sd_card);
static int RtSdcard_Send_ACMD_Disconnect(SD_CARD_INFO *pmmc_sd_card);
int RtSdcard_Send_ACMD_SCR(SD_CARD_INFO *pmmc_sd_card );
static int RtSdcard_Send_ACMD_Bus_Width(SD_CARD_INFO *pmmc_sd_card, int bus_width);
static int RtSdcard_Set_BusWidth(SD_CARD_INFO *pmmc_sd_card, int buswidthflag); //MA
static int RtSdcard_Read_SCR( SD_CARD_INFO * mmc_sd_card);

/* Called from rtsdcard_driver_attach() to initialize the controller */
int RtSdcard_attach(void)
{
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
        if (RtSdBSP_Controller_Attach()!=0) /* Target specific initialization (pinconfig, clocks etc) */
            return -1;
        RtSdcard_Driver_Initialized = 1;
    }
    /* Check if a card is installed and trigger an insertion event */
    if (RtSdBSP_Card_IsInstalled(0))
        rtsdcardReportInsertionEvent(0);
    return 0;
}

/* Called from rtsdcard_device_open(int unit) initialize a card */
int RtSdcard_init(int unitnumber)
{
    if (unitnumber>=MAXSDCARDS)
        return -1;
    if (RtSdcard_Media_Init(&mmc_sd_cards[unitnumber])!=0)  /* Standard device independent initialization */
        return -1;
    return 0;
}

/* Called from rtsdcard_device_media_parms() to retrieve device parameter */
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
    memset(pmmc_sd_card,0,sizeof(*pmmc_sd_card));
    pmmc_sd_card->card_type = CARD_UNKNOWN_TYPE;
    pmmc_sd_card->card_operating_flags= 0;
    pmmc_sd_card->state = INACTIVE_STATE;
    pmmc_sd_card->RCA = 0;

    if (RtSdcard_CardInit( pmmc_sd_card ) != 0)
        return -1;

    if( pmmc_sd_card->card_type == CARD_UNKNOWN_TYPE)
        return -1; //for now return, no card is inserted, or we don't support that card
    pmmc_sd_card->card_operating_flags |= SDFLGINSERTED;


    RtSdcard_Switch_Voltage(pmmc_sd_card);         /* Switch to 1.8 volts */


    if (RtSdcard_Check_CID( pmmc_sd_card) != 0)    /* Card information description */
        goto MEDIA_INIT_FAILED;

    if (RtSdcard_Set_Address(pmmc_sd_card) != 0)   /* Set RCA */
        goto MEDIA_INIT_FAILED;

    if (RtSdcard_Send_CSD(pmmc_sd_card) != 0)      /* card specific data */
        goto MEDIA_INIT_FAILED;

    if (RtSdcard_Select_Card(pmmc_sd_card, 1) != 0) /* Switch card to xfer state */
        goto MEDIA_INIT_FAILED;

    RtSdBSP_CardPushPullMode(pmmc_sd_card );        /* Transfer the rest in push pull mode */

    if (RtSdcard_Send_ACMD_Disconnect(pmmc_sd_card) != 0)  /* Disconnect pullup resistor so it can enter hd mode */
        goto MEDIA_INIT_FAILED;

    /* Sets up
        SDFLGMULTIBLOCK
        SDFLGBLOCKCOUNT
        SDFLGHIGHSPEED
        SDFLG4BITMODE
    */
    if (RtSdcard_Read_SCR(pmmc_sd_card)!=0)
        goto MEDIA_INIT_FAILED;

    if (( pmmc_sd_card->card_type == SD_CARD_TYPE ) || ( pmmc_sd_card->card_type == SDHC_CARD_TYPE ) || ( pmmc_sd_card->card_type == SDXC_CARD_TYPE ))
    {
        if (pmmc_sd_card->card_operating_flags&SDFLGHIGHSPEED)
              RtSdBSP_Set_Clock(1);
        else
              RtSdBSP_Set_Clock(0);       // TBD do we have to do this a second time ?

        if (pmmc_sd_card->card_operating_flags&SDFLG4BITMODE)
        {
              if (RtSdcard_Set_BusWidth( pmmc_sd_card, 4 ) != 0)
                goto MEDIA_INIT_FAILED;         /* fatal error */
        }
    }
    return 0;
MEDIA_INIT_FAILED:
    rtp_printf("Card initialization failed\n");
    pmmc_sd_card->card_type = CARD_UNKNOWN_TYPE;
    pmmc_sd_card->card_operating_flags = 0;
    pmmc_sd_card->state = INACTIVE_STATE;
    return -1;
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
    if ( width == 4 )
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
// HEREHERE Change2 move go idle after bsp init
//  if (RtSdcard_Go_Idle_State(pmmc_sd_card) != 0)
//    return -1;
  RtSdBSP_CardInit( pmmc_sd_card );
  if (RtSdcard_Go_Idle_State(pmmc_sd_card) != 0)
    return -1;
  /* Try CMD1 first for MMC, if it's timeout, try CMD55 and CMD41 for SD, if both failed, initialization faliure, bailout. */

  /*MCI_SendCmd( SEND_OP_COND, OCR_INDEX, EXPECT_SHORT_RESP, 0 ); */
  if(RtSdcard_Send_If_Cond(pmmc_sd_card) == 0 )////SDXC condition
  {
      RtSdBSP_Delay_micros(30000);
      pmmc_sd_card->card_type = SDHC_CARD_TYPE;
      return RtSdcard_Send_ACMD_OP_Cond( pmmc_sd_card);
  }
 // rtp_printf("Remove extra idle state\n");
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

static int _RtSdcard_Go_Idle_State( SD_CARD_INFO *pmmc_sd_card  )
{
struct SdTransaction cmd;

    cmd.Index = GO_IDLE_STATE;
    cmd.Argument = 0;
    cmd.Flags = SDFL_RESPONSE_0;
    RtSdBSP_SdTransaction( &cmd );
    pmmc_sd_card->state = IDLE_STATE;
    return 0;
}




static int RtSdcard_Go_Idle_State( SD_CARD_INFO *pmmc_sd_card  )
{
int retries;
unsigned long respStatus;
unsigned long  respValue[4];

    for (retries = 0; retries < 32; retries++)
    {
        /* Send CMD0 command repeatedly until the response is back correctly */
        RtSdBSP_SendCmd( GO_IDLE_STATE, 0x00000000, SD_RESP_R0, 0 );
        respStatus = RtSdBSP_GetCmdResp(GO_IDLE_STATE, SD_RESP_R0, &respValue[0] );
        if (respStatus == 0 )
        {
            pmmc_sd_card->state = IDLE_STATE;
            return 0;
        }
  }
  return -1;
}


static unsigned long RtSdcard_GetRCA(SD_CARD_INFO *pmmc_sd_card)
{
  if ( (pmmc_sd_card->card_type ==  SD_CARD_TYPE) || (pmmc_sd_card->card_type == SDHC_CARD_TYPE) || (pmmc_sd_card->card_type == SDXC_CARD_TYPE) )
    return pmmc_sd_card->RCA;    /* Use the address from SET_RELATIVE_ADDR cmd */
  else
    return 0x00010000;          /* Default for legacy cards */
}
static int RtSdcard_DoTransaction(SD_CARD_INFO *pmmc_sd_card, struct SdTransaction *cmd)
{
int i=0;
  do
  {
    if (cmd->Flags & SDFL_SEND_ACMD)
    {
        if (RtSdcard_Send_ACMD(pmmc_sd_card ) != 0)
            continue;
    }
    if (RtSdBSP_SdTransaction( cmd ))
    {
        if (cmd->Response[0]&cmd->ResponseMask == cmd->ResponseExpected)
        {
            pmmc_sd_card->state = READY_STATE;
            return 0;
        }
    }
    RtSdBSP_Delay_micros(100);
  } while (i++<cmd->MaxRetries);
  return -1;
}

#define OCR_VDD  (0xff80<<8) /* bits 23-8 of OCR - Mask of allowed voltage (ff80== all voltages between 3.6 and 2.7 Volts) */
#define OCR_HCS (1<<30)      /* Support HC or XC */

static int _RtSdcard_Send_OP_Cond( SD_CARD_INFO *pmmc_sd_card )
{
struct SdTransaction cmd;
    cmd.Index = SEND_OP_COND;
    cmd.Argument = OCR_VDD;
    cmd.Flags = SDFL_RESPONSE_48;
    cmd.MaxRetries =  512;
    cmd.ResponseMask = 0x80000000;
    cmd.ResponseExpected=0x80000000;

    if (RtSdcard_DoTransaction(pmmc_sd_card, &cmd)==0)
    {
        pmmc_sd_card->state = READY_STATE;
        return 0;
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
    /* Send CMD41 command repeatedly until the response is back correctly */
    RtSdBSP_SendCmd( SEND_OP_COND, OCR_VDD, EXPECT_SHORT_RESP, 0 );
    respStatus = RtSdBSP_GetCmdResp( SEND_OP_COND, EXPECT_SHORT_RESP, &respValue[0] );
    /* bit 0 and bit 2 must be zero, or it's timeout or CRC error */
    if ( (respStatus == 0) && (respValue[0] & 0x80000000) )
    {
      return  0;    /* response is back and correct. */
    }
    RtSdBSP_Delay_micros(100);
  }
  return -1;
}


static int _RtSdcard_Send_ACMD_OP_Cond( SD_CARD_INFO *pmmc_sd_card )
{
struct SdTransaction cmd;

  RtSdBSP_Delay_micros(3000);
  cmd.Index       = SEND_APP_OP_COND;
  cmd.Argument    = (OCR_HCS|OCR_VDD);
  cmd.Flags       = SDFL_RESPONSE_48|SDFL_SEND_ACMD;
  cmd.MaxRetries =  512;
  cmd.ResponseMask = 0x80000000;
  cmd.ResponseExpected=0x80000000;

  if (RtSdcard_DoTransaction(pmmc_sd_card, &cmd)==0)
  {
    pmmc_sd_card->state = READY_STATE;
    return 0;
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

    if (RtSdcard_Send_ACMD(pmmc_sd_card ) == -1)//??
    {
        continue;
    }

    /* Send ACMD41 command repeatedly until the response is back correctly */
    RtSdBSP_SendCmd( SEND_APP_OP_COND, (OCR_HCS|OCR_VDD), EXPECT_SHORT_RESP, 0 );

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

int _RtSdcard_Send_ACMD(  SD_CARD_INFO *pmmc_sd_card )
{
struct SdTransaction cmd;

  cmd.Index = APP_CMD;
  cmd.Flags = SDFL_RESPONSE_48;
  cmd.MaxRetries         =  32;
  cmd.ResponseMask       = CARD_STATUS_ACMD_ENABLE;
  cmd.ResponseExpected   = CARD_STATUS_ACMD_ENABLE;
  /* Get relative card address or 0 */
  if ( (pmmc_sd_card->card_type ==  SD_CARD_TYPE) || (pmmc_sd_card->card_type == SDHC_CARD_TYPE) || (pmmc_sd_card->card_type == SDXC_CARD_TYPE) )
  {
    cmd.Argument = pmmc_sd_card->RCA;    /* Use the address from SET_RELATIVE_ADDR cmd */
  }
  else            /* if MMC or unknown card type, use 0x0. */
  {
    cmd.Argument = 0x00000000;
  }
  return RtSdcard_DoTransaction(pmmc_sd_card, &cmd);
}


int RtSdcard_Send_ACMD(  SD_CARD_INFO *pmmc_sd_card )
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

static int _RtSdcard_Send_ACMD_Bus_Width(SD_CARD_INFO *pmmc_sd_card, int Width )
{
struct SdTransaction cmd;

  cmd.Index = SET_ACMD_BUS_WIDTH;
  cmd.Flags = SDFL_RESPONSE_48|SDFL_SEND_ACMD;
  cmd.Argument = Width;
  cmd.MaxRetries         =  32;
  cmd.ResponseMask       = (0x0F << 8);
  cmd.ResponseExpected   = 0x0900;

  return RtSdcard_DoTransaction(pmmc_sd_card, &cmd);

}

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
static unsigned long _RtSdcard_Send_Status( SD_CARD_INFO * mmc_sd_card )
{
struct SdTransaction cmd;

  cmd.Index = SEND_STATUS;
  cmd.Flags = SDFL_RESPONSE_48;
  cmd.Argument           = RtSdcard_GetRCA(mmc_sd_card);  /* Get relative card address */
  cmd.MaxRetries         =  0x2000;     /* Has to be long enough for read and write commands to complete */
  cmd.ResponseMask       =  (1 << 8);   /* Ready*/
  cmd.ResponseExpected   =  (1 << 8);

  if (RtSdcard_DoTransaction(mmc_sd_card, &cmd)==0)
      return ( cmd.Response[0] );
  return ( INVALID_RESPONSE );
}





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

static int _RtSdcard_Check_CID( SD_CARD_INFO * mmc_sd_card)
{
struct SdTransaction cmd;

  cmd.Index = SEND_STATUS;
  cmd.Flags = SDFL_RESPONSE_136;
  cmd.Argument           =  0;
  cmd.MaxRetries         =  32;
  cmd.ResponseMask       =  0xffffffff;   /* expecting zero TBD*/
  cmd.ResponseExpected   =  0;

  if (RtSdcard_DoTransaction(mmc_sd_card, &cmd)==0)
  {
    parse_CID(mmc_sd_card, &respValue[0]);
    mmc_sd_card->state = IDENTIFICATION_STATE;
    return 0;    /* response is back and correct. */
  }
  return -1;
}


static int RtSdcard_Check_CID( SD_CARD_INFO * mmc_sd_card)
{
int retries;
int respStatus;
unsigned long respValue[4];

  /* This command is normally after CMD1(MMC) or ACMD41(SD) or CMD11 if board supports 1.8V signaling and SDHC/SDXC */
  for (retries = 0; retries < 0x20; retries++)
  {
    /* Send CMD2 command repeatedly until the response is back correctly */
    RtSdBSP_SendCmd( ALL_SEND_CID, 0, SD_RESP_R2, 0 );
    respStatus = RtSdBSP_GetCmdResp( ALL_SEND_CID, SD_RESP_R2, &respValue[0] );
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

// HEREHERE
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

static int _RtSdcard_Send_CSD( SD_CARD_INFO * mmc_sd_card )
{
struct SdTransaction cmd;

  cmd.Index = SEND_CSD;
  cmd.Flags = SDFL_RESPONSE_136;
  cmd.Argument           = RtSdcard_GetRCA(mmc_sd_card);  /* Get relative card address */
  cmd.MaxRetries         =  32;
  cmd.ResponseMask       =  0xffffffff;   /* expecting zero TBD*/
  cmd.ResponseExpected   =  0;


  if (RtSdcard_DoTransaction(mmc_sd_card, &cmd)==0)
  {
    parse_CSD(mmc_sd_card, &cmd.Response[0]);
    return 0;    /* response is back and correct. */
  }
  return -1;
}

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
        RtSdBSP_SendCmd( SEND_CSD, CmdArgument, SD_RESP_R2, 0 );
        respStatus = RtSdBSP_GetCmdResp( SEND_CSD, SD_RESP_R2, &respValue[0] );
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



static int _RtSdcard_Select_Card( SD_CARD_INFO * mmc_sd_card , int is_specific_card)
{
struct SdTransaction cmd;

  cmd.Index = SEND_CSD;
  cmd.Flags = SDFL_RESPONSE_48;
  if(is_specific_card)
    cmd.Argument           = RtSdcard_GetRCA(mmc_sd_card);  /* Get relative card address */
  else
    cmd.Argument           = 0;
  cmd.MaxRetries         =  32;
  cmd.ResponseMask       =  (0x0F << 8);
  cmd.ResponseExpected   =  0x0700;


  if (RtSdcard_DoTransaction(mmc_sd_card, &cmd)==0)
  {
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
  return -1;
}

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

int _RtSdcard_Send_BlockCount( SD_CARD_INFO * mmc_sd_card, unsigned long block_count )
{
struct SdTransaction cmd;
  cmd.Index = SET_BLOCK_COUNT;
  cmd.Flags = SDFL_RESPONSE_48|SDFL_COMMAND_INTERRUPT;
  cmd.Argument           = block_count;
  cmd.MaxRetries         =  32;
  cmd.ResponseMask       =  (1 << 8); /* Ready */
  cmd.ResponseExpected   =  (1 << 8);
  return RtSdcard_DoTransaction(mmc_sd_card, &cmd);
}



int RtSdcard_Send_BlockCount( SD_CARD_INFO * mmc_sd_card, unsigned long block_count )
{
int retries;
unsigned long respStatus;
unsigned long respValue[4];

    for (retries = 0; retries < 0x20; retries++)
    {
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
int _RtSdcard_Send_Stop( SD_CARD_INFO * mmc_sd_card )
{
struct SdTransaction cmd;
  cmd.Index = STOP_TRANSMISSION;
  cmd.Flags = SDFL_RESPONSE_48;
  cmd.Argument           =  0;
  cmd.MaxRetries         =  32;
  cmd.ResponseMask       =  (1 << 8); /* Ready */
  cmd.ResponseExpected   =  (1 << 8);
  return RtSdcard_DoTransaction(mmc_sd_card, &cmd);
}


int RtSdcard_Send_Stop( SD_CARD_INFO * mmc_sd_card )
{
int retries;
unsigned long respStatus;
unsigned long respValue[4];

    for (retries = 0; retries < 0x20; retries++)
    {
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

int _RtSdcard_Send_Read_Block( SD_CARD_INFO * mmc_sd_card , unsigned long blockNum, unsigned long blk_len )
{
struct SdTransaction cmd;
  cmd.Index = READ_SINGLE_BLOCK;
  cmd.Flags = SDFL_RESPONSE_48;
  if ( ( mmc_sd_card->card_type == SDHC_CARD_TYPE ) ||( mmc_sd_card->card_type == SDXC_CARD_TYPE ) )
    cmd.Argument = blockNum;
  else /* if ( mmc_sd_card->card_type == SD_CARD_TYPE ) */
    cmd.Argument = blockNum * BLOCK_LENGTH;
  cmd.MaxRetries         =  32;
  cmd.ResponseMask       =  (0x0F << 8);
  cmd.ResponseExpected   =  0x0900;
  return RtSdcard_DoTransaction(mmc_sd_card, &cmd);
}

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

int _RtSdcard_Send_Write_Block( SD_CARD_INFO * mmc_sd_card, unsigned long blockNum)
{
struct SdTransaction cmd;
  cmd.Index = WRITE_BLOCK;
  cmd.Flags = SDFL_RESPONSE_48;
  if ( ( mmc_sd_card->card_type == SDHC_CARD_TYPE ) ||( mmc_sd_card->card_type == SDXC_CARD_TYPE ) )
    cmd.Argument = blockNum;
  else /* if ( mmc_sd_card->card_type == SD_CARD_TYPE ) */
    cmd.Argument = blockNum * BLOCK_LENGTH;
  cmd.MaxRetries         =  32;
  cmd.ResponseMask       =  (0x0F << 8);
  cmd.ResponseExpected   =  0x0900;
  return RtSdcard_DoTransaction(mmc_sd_card, &cmd);
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
static int _RtSdcard_Send_If_Cond( SD_CARD_INFO * mmc_sd_card ) /* SEND_IF_COND does not take arguments ?? */
{
struct SdTransaction cmd;
  cmd.Index = WRITE_BLOCK;
  cmd.Flags = SDFL_RESPONSE_48;
   /* Send 0x1AA Bit 8 = 1 (2.7 - 3.6V) Check pattern = 10101010 (0xaa) */
  cmd.Argument = 0x1AA;
  cmd.MaxRetries         =  512;
  cmd.ResponseMask       =  0x1AA;  /* Expect it echoed back */
  cmd.ResponseExpected   =  0x1AA;
  return RtSdcard_DoTransaction(mmc_sd_card, &cmd);
}

/* SEND_IF_COND */
static int RtSdcard_Send_If_Cond( SD_CARD_INFO * mmc_sd_card ) /* SEND_IF_COND does not take arguments ?? */
{
int retries;
int respStatus;
unsigned long respValue[4];

    /* Bit 8 = 1 (27 - 36V) Check pattern = 10101010 (0xaa) */
    for (retries = 0; retries < 0x200; retries++)
    {
        /* CMD8 */
        RtSdBSP_SendCmd( SEND_IF_COND, 0x1AA, EXPECT_SHORT_RESP, 0 );
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
int _RtSdcard_Send_Read_Multiple_Block( SD_CARD_INFO * mmc_sd_card , unsigned long blockNum )
{
struct SdTransaction cmd;
  cmd.Index = READ_MULTIPLE_BLOCK;
  cmd.Flags = SDFL_RESPONSE_48;
  if ( ( mmc_sd_card->card_type == SDHC_CARD_TYPE ) ||( mmc_sd_card->card_type == SDXC_CARD_TYPE ) )
    cmd.Argument = blockNum;
  else /* if ( mmc_sd_card->card_type == SD_CARD_TYPE ) */
    cmd.Argument = blockNum * BLOCK_LENGTH;
  cmd.MaxRetries         =  32;
  cmd.ResponseMask       =  (0x0F << 8);
  cmd.ResponseExpected   =  0x0900;
  return RtSdcard_DoTransaction(mmc_sd_card, &cmd);
}


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
int _RtSdcard_Send_Write_Multiple_Block( SD_CARD_INFO * mmc_sd_card, unsigned long blockNum)
{
struct SdTransaction cmd;
  cmd.Index = WRITE_MULTIPLE_BLOCK;
  cmd.Flags = SDFL_RESPONSE_48;
  if ( ( mmc_sd_card->card_type == SDHC_CARD_TYPE ) ||( mmc_sd_card->card_type == SDXC_CARD_TYPE ) )
    cmd.Argument = blockNum;
  else /* if ( mmc_sd_card->card_type == SD_CARD_TYPE ) */
    cmd.Argument = blockNum * BLOCK_LENGTH;
  cmd.MaxRetries         =  32;
  cmd.ResponseMask       =  (0x0F << 8);
  cmd.ResponseExpected   =  0x0900;
  return RtSdcard_DoTransaction(mmc_sd_card, &cmd);
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
    if (blockCount > SD_FORCE_MAX_BLOCK_COUNT_WRITE)
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

    r=_RtSdcard_DoWrite_Block(mmc_sd_card, blockNum);
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

unsigned long RtSdcard_Read_Block(int unit, unsigned long blockNum,unsigned long blockCount, unsigned char *Buffer)
{
    SD_CARD_INFO * mmc_sd_card;
    int use_xferrbuff=0;
    unsigned long xferrbuff[128] ;
    unsigned char *pBbuffer=Buffer;
#if (SD_FORCE_MAX_BLOCK_COUNT_READ)
    if (blockCount>SD_FORCE_MAX_BLOCK_COUNT_READ)
        blockCount=SD_FORCE_MAX_BLOCK_COUNT_READ;
#endif
    if ((unsigned long)pBbuffer & 0x3)
    {
        pBbuffer = (unsigned char *)&xferrbuff[0];
        use_xferrbuff=1;
        blockCount=1;
    }
    if (unit>=MAXSDCARDS)
        return 0;
    mmc_sd_card = &mmc_sd_cards[unit];
    mmc_sd_card->blk_buffer=(unsigned long *) pBbuffer;
    mmc_sd_card->blocks_transfered=0;
    mmc_sd_card->blocks_to_transfer=blockCount;
    if (_RtSdcard_DoRead_Block(mmc_sd_card, blockNum)==-1)
        return 0;
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

static int RtSdcard_Read_SCR( SD_CARD_INFO * mmc_sd_card)
{
unsigned char buf[SCR_LENGTH];

    /* Read SCR isn't working but fake values for now to keep going */
    mmc_sd_card->card_operating_flags |= SDFLGMULTIBLOCK;
    //mmc_sd_card->card_operating_flags |= SDFLGBLOCKCOUNT;
    mmc_sd_card->card_operating_flags |= SDFLGHIGHSPEED;
    mmc_sd_card->card_operating_flags |= SDFLG4BITMODE;
    rtp_printf("fall through to read but use fake scr values\n");
//    return 0;
 //   MCIClear |= MCI_CLEAR_ALL;
 //   RtSdBSP_Delay_micros(100);


    /* Make sure the card is in TRANS state card. */
    if (RtSdcard_CheckStatus(mmc_sd_card) != 0 )
    {
        RtSdcard_Send_Stop(mmc_sd_card);
        return -1;
    }
    if (RtSdBSP_Read_SCRSetup(mmc_sd_card)!=0)
        return -1;

    if (RtSdcard_Send_ACMD_SCR(mmc_sd_card) != 0)
        goto error_return;

    if (RtSdBSP_Read_SCRTransfer(mmc_sd_card,buf)!=0)
        goto error_return;

    RtSdBSP_Read_SCRShutdown(mmc_sd_card);

    {
    unsigned long tmp,scr[2];
    int i;
 //    mmc_sd_card->SCR.scrbits=buffer[1];
        scr[0]=scr[1]=0;
        tmp = (unsigned long)buf[0] << 24;
        scr[0] = tmp & 0xFF000000;
        tmp = (unsigned long)buf[1] << 16;
        scr[0] += (tmp & 0x00FF0000);
        tmp = (unsigned long)buf[2] << 8;
        scr[0] += (tmp & 0x0000FF00);
        tmp = (unsigned long)buf[3];
        scr[0] += (tmp & 0x000000FF);

        tmp = (unsigned long)buf[4] << 24;
        scr[1] = tmp & 0xFF000000;
        tmp = (unsigned long)buf[5] << 16;
        scr[1] += tmp & 0x00FF0000;
        tmp = (unsigned long)buf[6] << 8;
        scr[1] += tmp & 0x0000FF00;
        tmp = (unsigned long)buf[7];
        scr[1] += tmp & 0x000000FF;

        for(i=0; i < 8; i++)
            rtp_printf("buf[%d] == %X\n", i, buf[i]);
    }
     return 0;
error_return:

 //   return 0;

    RtSdBSP_Read_SCRShutdown(mmc_sd_card);
    rtp_printf("Ignoring read scr failure,  \n");
//    return -1;
    return 0;
}


int _RtSdcard_Send_ACMD_SCR(SD_CARD_INFO *pmmc_sd_card )
{
struct SdTransaction cmd;
  cmd.Index = SEND_APP_SEND_SCR;
  cmd.Flags = SDFL_RESPONSE_48|SDFL_SEND_ACMD;
  cmd.Argument = 0;
  cmd.MaxRetries         =  32;
  cmd.ResponseMask       =  (0x0F << 8);
  cmd.ResponseExpected   =  0x0900;
  return RtSdcard_DoTransaction(pmmc_sd_card, &cmd);
}

int RtSdcard_Send_ACMD_SCR(SD_CARD_INFO *pmmc_sd_card )
{
int retries;
unsigned long respStatus;
unsigned long  respValue[4];
unsigned long  CmdArgument=0;

    for (retries = 0; retries < 0x20; retries++)
    {
        if ( RtSdcard_Send_ACMD(pmmc_sd_card) == -1 )
            continue;

        /* Send ACMD51 command to get the SCR register*/
        RtSdBSP_SendCmd( SEND_APP_SEND_SCR, CmdArgument, EXPECT_SHORT_RESP, 0 );
        respStatus = RtSdBSP_GetCmdResp( SEND_APP_SEND_SCR, EXPECT_SHORT_RESP, &respValue[0] );
        if ( respStatus==0 && ((respValue[0] & (0x0F << 8)) == 0x0900) )
            return 0;    /* response is back and correct. */
        RtSdBSP_Delay_micros(200);
    }
    return -1;
}


static int _RtSdcard_Send_ACMD_Disconnect(SD_CARD_INFO *pmmc_sd_card)
{
struct SdTransaction cmd;

  cmd.Index = SEND_APP_OP_DISCONNECT;
  cmd.Flags = SDFL_RESPONSE_48|SDFL_SEND_ACMD;
  cmd.Argument = 0;
  cmd.MaxRetries         =  32;
  cmd.ResponseMask       =  (0x0F << 8);
  cmd.ResponseExpected   =  0x0900;
  return RtSdcard_DoTransaction(pmmc_sd_card, &cmd);
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



static void unpack_bits(unsigned char *dest, unsigned long *_source, int bit_start, int bit_count)
{
int i,j;
unsigned long bit_offset,source[4];
unsigned long mask=0xff;
int bits;

//        Product revision PRV 8 [63:56]              = (DWSWAP(respValue[2]) & 0xff000000)>>24;

    for (i =0; i < 4;i++,_source++)
        source[i]=DWSWAP(*_source);
    for (i =0; i < (bit_count+7)/8;i++)
        dest[i] = 0;

    for (i=((bit_count+7)/8)-1,bit_offset=bit_start;i>=0;i--,bit_count-=bits,bit_offset+=bits)
    {
        unsigned long bit_pos_string =  (127-bit_offset);
        unsigned long dword_pos = bit_pos_string/32;
        unsigned long byte_pos_dword = 3-(bit_pos_string%32)/8;
        unsigned long bit_pos_dword = 31-(bit_pos_string%32);
        dest[i] = (unsigned char)(source[dword_pos]>>(byte_pos_dword*8));
        dest[i] = (unsigned char)(source[dword_pos]>>bit_pos_dword);
        bits=8;         /* Start by assuming we take the whole byte */
        if (bit_offset&0x7)
        {
            /*
           mask by (8-boundary) bits
           bits_used = (8-boundary)
            */
            mask=0;
            for(j = 0; j < 8-(bit_offset&0x7);j++)
            {
                mask <<=1; mask |= 1;
            }
            dest[i] &= mask;
            bits=8-(bit_offset&0x7);
        }
        if (bit_count<8)
        {
            /* bit count is < 8
            mask by bit_count bits
            bits_used = bit_count
            */

            mask=0;
            for(j = 0; j < bit_count;j++)
            {
                mask <<=1; mask |= 1;
            }
            dest[i] &= mask;
            bits = bit_count;
        }
     }

}


static void parse_CID(SD_CARD_INFO * mmc_sd_card, unsigned long *respValue)
{
    unsigned char temp_8;
    unsigned long temp_32, temp_32b;
    unsigned short temp_16;
    unsigned long long temp_serial;

    unsigned char ManufacturerID[1];
    unsigned char OEM_ApplicationID[2];
    unsigned char ProductName[5];
    unsigned char ProductRevision[1];
    unsigned char ProductSerialNumber[4];
    // reserved -- 4 [23:20]
    unsigned char ManufacturingMonth[1];
    unsigned char ManufacturingYear[1];

    /*
        Manufacturer ID MID 8 [127:120]             = (DWSWAP(respValue[0]) & 0xff000000)>>24;
        OEM/Application ID OID 16 [119:104]         = (DWSWAP(respValue[0]) & 0x00ffff00)>>16;
        Product name PNM 40 [103:64]
        Product revision PRV 8 [63:56]              = (DWSWAP(respValue[2]) & 0xff000000)>>24;
        Product serial number PSN 32 [55:24]        = (DWSWAP(respValue[2]) & 0x00ffffff)<<8|(DWSWAP(respValue[3]) & 0xff000000)>>24;
        reserved -- 4 [23:20]
        Manufacturing date MDT 12    [19:8]               = (DWSWAP(respValue[3])&0x000FFF00)>>8
    */


    temp_32 = DWSWAP(respValue[0]) & 0xff000000;
    temp_32 = (temp_32 >> 24);
    temp_8 = (unsigned char) (temp_32 & 0xff);
    mmc_sd_card->CID.MID = temp_8;
    unpack_bits(ManufacturerID,&respValue[0],120,8);

    temp_32 = DWSWAP(respValue[0]) & 0x00ffff00;
    temp_32 = (temp_32 >> 8);
    temp_16 = temp_32 & 0xFFFF;
    mmc_sd_card->CID.OID = temp_16;
    unpack_bits((char *) &temp_16,&respValue[0],104,16);
    unpack_bits(OEM_ApplicationID,&respValue[0],104,16);

    temp_serial = DWSWAP(respValue[0]) & 0x000000ff;
    temp_serial = (temp_serial << 32);  //bits 40 down  PVOPVO - long long not protable
    temp_32 = DWSWAP(respValue[1]);
    temp_serial = temp_serial | temp_32 ;
    mmc_sd_card->CID.PNM.PNM = temp_serial;
    unpack_bits(ProductName, &respValue[0], 64, 40);


    temp_32 = DWSWAP(respValue[2]);
    temp_32 = temp_32 & 0xff000000;
    temp_32 = ( temp_32 >> 24 );
    temp_8 = temp_32 & 0xFF;
    mmc_sd_card->CID.PRV = temp_8;
    unpack_bits(ProductRevision, &respValue[0], 56, 8);

    temp_32 = DWSWAP(respValue[2]);
    temp_32 = temp_32 & 0x00FFFFFF;
    temp_32 = (temp_32 << 8 );

    temp_32b = DWSWAP(respValue[3]);
    temp_32b = temp_32b & 0xFF000000;
    temp_32b = ( temp_32b >> 24 );
    temp_32 = temp_32 |temp_32b;
    mmc_sd_card->CID.PSN = temp_32;
    unpack_bits(ProductSerialNumber, &respValue[0], 24, 32);

    temp_32 = DWSWAP(respValue[3]);
    temp_32 = temp_32 & 0x000FFF00;
    temp_32 = ( temp_32 >> 8 );
    temp_16 = temp_32 & 0xfff;
    mmc_sd_card->CID.MDT = temp_16;
    unpack_bits((unsigned char *)&ManufacturingMonth, &respValue[0], 8, 4);
    unpack_bits((unsigned char *)&ManufacturingMonth, &respValue[0], 8, 4);
    unpack_bits((unsigned char *)&ManufacturingMonth, &respValue[0], 8, 4);
    unpack_bits((unsigned char *)&ManufacturingMonth, &respValue[0], 8, 4);
    unpack_bits((unsigned char *)&ManufacturingMonth, &respValue[0], 8, 4);

    unpack_bits((unsigned char *)&ManufacturingYear, &respValue[0], 12, 8);
   unpack_bits((unsigned char *)&ManufacturingYear, &respValue[0], 12, 8);
   unpack_bits((unsigned char *)&ManufacturingYear, &respValue[0], 12, 8);
   unpack_bits((unsigned char *)&ManufacturingYear, &respValue[0], 12, 8);
   unpack_bits((unsigned char *)&ManufacturingYear, &respValue[0], 12, 8);

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
    unsigned char temp_8[4];
    unsigned short temp_16;


    temp_32 = DWSWAP(respValue[0]);
    temp_32 = temp_32 & 0xC0000000 ;
    temp_32 = (temp_32 >> 30);
    csd_structure  = temp_32 & 0x00000003;
    unpack_bits((unsigned char *)&csd_structure, &respValue[0], 126, 2);

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
            unpack_bits((unsigned char *)&mmc_sd_card->CSD_V2.TAAC, &respValue[0], 112, 8);

            temp_32 = DWSWAP(respValue[0]);
            temp_32 = temp_32 & 0x0000ff00 ;
            temp_32 = (temp_32 >> 8);
            mmc_sd_card->CSD_V2.NSAC = temp_32 & 0xff;
            unpack_bits((unsigned char *)&mmc_sd_card->CSD_V2.NSAC, &respValue[0], 104, 8);
            temp_32 = DWSWAP(respValue[0]);
            temp_32 = temp_32 & 0x000000ff ;
            mmc_sd_card->CSD_V2.TRAN_SPEED = temp_32 & 0xff;
            unpack_bits((unsigned char *)&mmc_sd_card->CSD_V2.TRAN_SPEED , &respValue[0], 96, 8);
            temp_32 = DWSWAP(respValue[1]);
            temp_32 = temp_32 & 0xfff00000 ;
            temp_32 = (temp_32 >> 20);
            mmc_sd_card->CSD_V2.CCC = temp_32 & 0xfff;
            /* Get a 12 bit word at offset 84 by getting a 16 bit word from offset 80 and shifting */

            unpack_bits(&temp_8[0], &respValue[0], 84, 4);
            unpack_bits(&temp_8[1], &respValue[0], 88, 8);
            temp_16= (((unsigned short)temp_8[0])<<8)|temp_8[1];
            mmc_sd_card->CSD_V2.CCC = temp_16;

            temp_32 = DWSWAP(respValue[1]);
            temp_32 = temp_32 & 0x000f0000 ;
            temp_32 = (temp_32 >> 16);
            mmc_sd_card->CSD_V2.READ_BL_LEN = temp_32 & 0xf;
            unpack_bits((unsigned char *)&mmc_sd_card->CSD_V2.READ_BL_LEN , &respValue[0], 80, 4);

            temp_32 = DWSWAP(respValue[1]);
            temp_32 = temp_32 & 0x0000f000 ;
            temp_32 = (temp_32 >> 15);
            mmc_sd_card->CSD_V2.READ_BL_PARTIAL= temp_32 & 0x1;
            unpack_bits((unsigned char *)&mmc_sd_card->CSD_V2.READ_BL_PARTIAL , &respValue[0], 79, 1);

            temp_32 = DWSWAP(respValue[1]);
            temp_32 = temp_32 & 0x0000f000 ;
            temp_32 = (temp_32 >> 14);
            mmc_sd_card->CSD_V2.WRITE_BLK_MISALIGN= temp_32 & 0x1;
            unpack_bits((unsigned char *)&mmc_sd_card->CSD_V2.WRITE_BLK_MISALIGN , &respValue[0], 78, 1);

            temp_32 = DWSWAP(respValue[1]);
            temp_32 = temp_32 & 0x0000f000 ;
            temp_32 = (temp_32 >> 13);
            mmc_sd_card->CSD_V2.READ_BLK_MISALIGN= temp_32 & 0x1;
            unpack_bits((unsigned char *)&mmc_sd_card->CSD_V2.READ_BLK_MISALIGN , &respValue[0], 77, 1);

            temp_32 = DWSWAP(respValue[1]);
            temp_32 = temp_32 & 0x0000f000 ;
            temp_32 = (temp_32 >> 12);
            mmc_sd_card->CSD_V2.DSR_IMP= temp_32 & 0x1;
            unpack_bits((unsigned char *)&mmc_sd_card->CSD_V2.DSR_IMP , &respValue[0], 76, 1);

            temp_32 = DWSWAP(respValue[1]);
            temp_32 = temp_32 & 0x0000003f ;
            temp_32 = (temp_32 << 16);
            mmc_sd_card->CSD_V2.C_SIZE= temp_32 & 0x3F0000;

            temp_32 = DWSWAP(respValue[2]);
            temp_32 = temp_32 & 0xffff0000 ;
            temp_32 = (temp_32 >> 16);
            mmc_sd_card->CSD_V2.C_SIZE = mmc_sd_card->CSD_V2.C_SIZE | (temp_32 & 0xffff);

            if(csd_structure == 0)
            {
                temp_32 = DWSWAP(respValue[1]);
                temp_32 = temp_32 & 0x000003ff ;
                temp_32 = ( temp_32 << 2 );
                mmc_sd_card->CSD_V1.C_SIZE = temp_32 & 0xff8;
                temp_32 = DWSWAP(respValue[2]);
                temp_32 = temp_32 & 0xe0000000 ;
                temp_32 = ( temp_32 >> 30 );
                mmc_sd_card->CSD_V2.C_SIZE = mmc_sd_card->CSD_V2.C_SIZE | (temp_32 & 0x03);


                unpack_bits(&temp_8[0], &respValue[0], 62, 2);
                unpack_bits(&temp_8[1], &respValue[0], 64, 8);
                unpack_bits(&temp_8[2], &respValue[0], 72, 2);
                temp_32 = ((unsigned long)temp_8[0]<<10)|( (unsigned long)temp_8[1]<<2)|(unsigned short)temp_8[2];
                mmc_sd_card->CSD_V2.C_SIZE =temp_32;

                temp_32 = DWSWAP(respValue[2]);
                temp_32 = temp_32 & 0x38000000 ;
                temp_32 = ( temp_32 >> 27 );
                mmc_sd_card->CSD_V1.VDD_R_CURR_MIN= temp_32 & 0x07;
                unpack_bits(&mmc_sd_card->CSD_V1.VDD_R_CURR_MIN, &respValue[0], 59, 3); // V1 starts at 62:73 on V1 HEREHERE----

                temp_32 = DWSWAP(respValue[2]);
                temp_32 = temp_32 & 0x07000000 ;
                temp_32 = ( temp_32 >> 24 );
                mmc_sd_card->CSD_V1.VDD_R_CURR_MAX = temp_32 & 0x07;
                unpack_bits(&mmc_sd_card->CSD_V1.VDD_R_CURR_MAX,  &respValue[0], 56, 3); // V1 starts at 62:73 on V1 HEREHERE----

                temp_32 = DWSWAP(respValue[2]);
                temp_32 = temp_32 & 0x00e00000 ;
                temp_32 = ( temp_32 >> 21 );
                mmc_sd_card->CSD_V1.VDD_W_CURR_MIN = temp_32 & 0x07;
                unpack_bits(&mmc_sd_card->CSD_V1.VDD_W_CURR_MIN,  &respValue[0], 53, 3); // V1 starts at 62:73 on V1 HEREHERE----

                temp_32 = DWSWAP(respValue[2]);
                temp_32 = temp_32 & 0x001c0000 ;
                temp_32 = ( temp_32 >> 18 );
                mmc_sd_card->CSD_V1.VDD_W_CURR_MAX = temp_32 & 0x07;
                unpack_bits(&mmc_sd_card->CSD_V1.VDD_W_CURR_MAX,  &respValue[0], 50, 3); // V1 starts at 62:73 on V1 HEREHERE----

                temp_32 = DWSWAP(respValue[2]);
                temp_32 = temp_32 & 0x00038000 ;
                temp_32 = ( temp_32 >> 15 );
                mmc_sd_card->CSD_V1.C_SIZE_MULT = temp_32 & 0x07;
                unpack_bits(&mmc_sd_card->CSD_V1.C_SIZE_MULT,  &respValue[0], 47, 3); // V1 starts at 62:73 on V1 HEREHERE----

/*
V1 - Here
device size C_SIZE 12 xxxh R [73:62]
max. read current @VDD min VDD_R_CURR_MIN 3 xxxb R [61:59]
max. read current @VDD max VDD_R_CURR_MAX 3 xxxb R [58:56]
max. write current @VDD min VDD_W_CURR_MIN 3 xxxb R [55:53]
max. write current @VDD max VDD_W_CURR_MAX 3 xxxb R [52:50]
device size multiplier C_SIZE_MULT 3 xxxb R [49:47]
*/
            }
            else
            {
                unpack_bits(&temp_8[0], &respValue[0], 48, 8); // V1 starts at 62:73 on V1 HEREHERE----
                unpack_bits(&temp_8[1], &respValue[0], 56, 8);
                unpack_bits(&temp_8[2], &respValue[0], 64, 6);
                temp_32= ((unsigned long)temp_8[2]<<16)|((unsigned long)temp_8[1]<<8)|temp_8[0];
                mmc_sd_card->CSD_V2.C_SIZE =temp_32;
            }


            temp_32 = DWSWAP(respValue[2]);
            temp_32 = temp_32 & 0x00004000 ; //100000000000000
            temp_32 = (temp_32 >> 14);
            mmc_sd_card->CSD_V2.ERASE_BLK_EN = (temp_32 & 0x1);
            unpack_bits((unsigned char *)&mmc_sd_card->CSD_V2.ERASE_BLK_EN, &respValue[0], 46, 1);

            temp_32 = DWSWAP(respValue[2]);
            temp_32 = temp_32 & 0x00003f80 ; //011111110000000
            temp_32 = (temp_32 >> 7);
            mmc_sd_card->CSD_V2.SECTOR_SIZE= (temp_32 & 0x7f);
            unpack_bits((unsigned char *)&mmc_sd_card->CSD_V2.SECTOR_SIZE, &respValue[0], 39, 7);

            temp_32 = DWSWAP(respValue[2]);
            temp_32 = temp_32 & 0x0000003f ; //011111110000000
            mmc_sd_card->CSD_V2.WP_GRP_SIZE= (temp_32 & 0x3f);
            unpack_bits((unsigned char *)&mmc_sd_card->CSD_V2.WP_GRP_SIZE, &respValue[0], 32, 6);

            temp_32 = DWSWAP(respValue[3]);
            temp_32 = temp_32 & 0x80000000 ;
            temp_32 = (temp_32 >> 31);
            mmc_sd_card->CSD_V2.WP_GRP_ENABLE = temp_32 & 0x01;
            unpack_bits((unsigned char *)&mmc_sd_card->CSD_V2.WP_GRP_ENABLE, &respValue[0], 31, 1);

            temp_32 = DWSWAP(respValue[3]);
            temp_32 = temp_32 & 0x1c000000 ;
            temp_32 = (temp_32 >> 26);
            mmc_sd_card->CSD_V2.R2W_FACTOR= temp_32 & 0x07;
            unpack_bits((unsigned char *)&mmc_sd_card->CSD_V2.R2W_FACTOR, &respValue[0], 26, 3);

            temp_32 = DWSWAP(respValue[3]);
            temp_32 = temp_32 & 0x03c00000 ;
            temp_32 = (temp_32 >> 22);
            mmc_sd_card->CSD_V2.WRITE_BL_LEN= temp_32 & 0x0f;
            unpack_bits((unsigned char *)&mmc_sd_card->CSD_V2.WRITE_BL_LEN, &respValue[0], 22, 4);

            temp_32 = DWSWAP(respValue[3]);
            temp_32 = temp_32 & 0x00200000 ;
            temp_32 = (temp_32 >> 21);
            mmc_sd_card->CSD_V2.WRITE_BL_PARTIAL= temp_32 & 0x01;
            unpack_bits((unsigned char *)&mmc_sd_card->CSD_V2.WRITE_BL_PARTIAL, &respValue[0], 21, 1);

            temp_32 = DWSWAP(respValue[3]);
            temp_32 = temp_32 & 0x00008000 ;
            temp_32 = (temp_32 >> 15);
            mmc_sd_card->CSD_V2.FILE_FORMAT_GRP= temp_32 & 0x01;
            unpack_bits((unsigned char *)&mmc_sd_card->CSD_V2.FILE_FORMAT_GRP, &respValue[0], 15, 1);

            temp_32 = DWSWAP(respValue[3]);
            temp_32 = temp_32 & 0x00004000 ;
            temp_32 = (temp_32 >> 14);
            mmc_sd_card->CSD_V2.COPY= temp_32 & 0x01;
            unpack_bits((unsigned char *)&mmc_sd_card->CSD_V2.COPY, &respValue[0], 14, 1);

            temp_32 = DWSWAP(respValue[3]);
            temp_32 = temp_32 & 0x00002000 ;
            temp_32 = (temp_32 >> 13);
            mmc_sd_card->CSD_V2.PERM_WRITE_PROTECT= temp_32 & 0x01;
            unpack_bits((unsigned char *)&mmc_sd_card->CSD_V2.PERM_WRITE_PROTECT, &respValue[0], 13, 1);


            temp_32 = DWSWAP(respValue[3]);
            temp_32 = temp_32 & 0x00001000 ;
            temp_32 = (temp_32 >> 12);
            mmc_sd_card->CSD_V2.TMP_WRITE_PROTECT= temp_32 & 0x01;
            unpack_bits((unsigned char *)&mmc_sd_card->CSD_V2.TMP_WRITE_PROTECT, &respValue[0], 12, 1);

            temp_32 = DWSWAP(respValue[3]);
            temp_32 = temp_32 & 0x00000c00 ;
            temp_32 = (temp_32 >> 10);
            mmc_sd_card->CSD_V2.FILE_FORMAT= temp_32 & 0x03;
            unpack_bits((unsigned char *)&mmc_sd_card->CSD_V2.FILE_FORMAT, &respValue[0], 10, 2);

            temp_32 = DWSWAP(respValue[3]);
            temp_32 = temp_32 & 0x000000fe ;
            temp_32 = (temp_32 >> 1);
            mmc_sd_card->CSD_V2.CRC= temp_32 & 0x7f;
            unpack_bits((unsigned char *)&mmc_sd_card->CSD_V2.CRC, &respValue[0], 1, 7);
            if(csd_structure == 0)
            {
                if( mmc_sd_card->CSD_V2.READ_BL_LEN < 12 )
                {    block_len = 1;  /*pow(2,mmc_sd_card->CSD_V1.READ_BL_LEN) */
                    for(i=1 ; i <= mmc_sd_card->CSD_V2.READ_BL_LEN ; i++)
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
                card_memory_capacity = (mmc_sd_card->CSD_V2.C_SIZE+1) *512 ;
                mmc_sd_card->card_capacity_bytes = card_memory_capacity * 1024;
                mmc_sd_card->no_blocks = (mmc_sd_card->CSD_V2.C_SIZE+1) * 1024;  /* ?? no of blocks in SDSC card */
                mmc_sd_card->bytes_per_block = 512;
            }
        }
    }
}
