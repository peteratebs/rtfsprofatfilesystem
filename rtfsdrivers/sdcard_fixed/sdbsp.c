/*
 * SDMMC.c
 *
 *  Created on: Mar 29, 2011
 *      Author: Peter
 */

 /*
        RtSdBSP_Controller_Init -
            Called once at start-up.
            Make sure the controller is initialized
            Make sure card detect interrupts are hooked
                card detect interrupts should call one of these events when card insert status cahnges.
                    rtsdcardReportRemovalEvent - A card has been removed.
                    rtsdcardReportInsertionEvent - A card has been inserted.
            Make sure card IO event interrupts are hooked
                The card IO event handlers coordinate with other routines that are private to the sdbsp file
                    see: RtSdBSP_Read_BlockSetup, RtSdBSP_Read_BlockTransfer, RtSdBSP_Read_BlockShutdown
                    see: RtSdBSP_Write_BlockSetup, RtSdBSP_Write_BlockTransfer, RtSdBSP_Write_BlockShutdown
        RtSdBSP_CardInit -
            Called when a card is installed
            Must set Open Drain output control for MMC
        RtSdBSP_CardPushPullMode
            Called when a card exits initialization mode
            Must set Open Drain output control for MMC
        RtSdBSP_Set_Clock -
            If passed zero set the SD bus clock to its low value (must be less than  400 KHZ;
            If passed one set the SD bus clock to its high value (18 to 25 mhz)
        RtSdBSP_Set_BusWidth
            Set the SD bus width to the width requested (1 or 4)
        SendCmd -
            Send an SD card command
        GetCmdResp
            Receive an SD card response
        RtSdBSP_Read_BlockSetup
            Prepare for a read transfer.
                If using DMA initialize the DMA controller.
                If using interrupts clear the interrupt signal.
                If using interrupts mask on proper interrupts.
                If using PIO transfer with polling disable interrupts.
        RtSdBSP_Read_BlockTransfer
                If using interrupts wait for signal from interrupt complete.
                If using PIO transfer without interrupts perform polled transfer.
                return 0 if successful
                return -1 if an error is detected.
        RtSdBSP_Read_BlockShutdown
                If using interrupts mask off proper interrupts.
                If using PIO transfer with polling re-enable interrupts.
        RtSdBSP_Write_BlockSetup
            Prepare for a read transfer.
                If using DMA initialize the DMA controller.
                If using interrupts clear the interrupt signal.
                If using interrupts mask on proper interrupts.
                If using PIO transfer with polling disable interrupts.
        RtSdBSP_Write_BlockTransfer
                If using interrupts wait for signal from interrupt complete.
                If using PIO transfer without interrupts perform polled transfer.
                return 0 if successful
                return -1 if an error is detected.
        RtSdBSP_Write_BlockShutdown
                If using interrupts mask off proper interrupts.
                If using PIO transfer with polling re-enable interrupts.
        RtSdBSP_Delay_micros -
            Perform a polling loop to delay the number of microseconds requested

        int RtSdBSP_Read_SCR(SD_CARD_INFO * mmc_sd_card);

*/

#include "sdmmc.h"

extern SD_CARD_INFO mmc_sd_cards[];

/* Relatively device independent example BSP routenes */

/* Delay loop assuming 10 loops per microsecond */
void RtSdBSP_Delay_micros(unsigned long delay)
{
int i;
#define MICROSPERLOOP 10
    for (i = 0; i < delay * MICROSPERLOOP; i++)
        ;
}

/* Simple minded signalling mechanism for interrupts */
static int sig;
static void sd_sig_clear(SD_CARD_INFO * mmc_sd_card)
{
    sig=0;
}
static void sd_sig_set(SD_CARD_INFO * mmc_sd_card)
{
    sig=1;
}
#define TIMEOUTVAL 100000000
static int sd_sig_wait(SD_CARD_INFO * mmc_sd_card,unsigned long timeout)
{
  while (sig==0)
  {
    if (timeout-- == 0)
      return -1;
  }
  return 0;

}




#include "lpc214x.h"
#include "rtpstr.h"
#include "rtpprint.h"

#define CONFIGURE_SDMMC_INTERRUPTS 1
#define ENABLE_DMA 0



#define SD_CARD_UNIT_NUM 0

volatile int MCI_Block_End_Flag = 0;
static int Card_Detect_Events =0;
static int Card_Detect_Events_Reported =0;
static int Card_Detect_Inserted=0;
#define DATA_TIMER_VALUE        0x10000    /* Check if I need to scale this down, or if it can be determined programatically from the CSD  */
#define MA_DATA_TIMER_VALUE     0x80000000 //Check if I need to scale this down, or if it can be determined programatically from the CSD




extern unsigned long PVOPVO_T_splx(unsigned long level);
extern unsigned long PVOPVO_T_splhigh();
extern int RtSdcard_CheckStatus( SD_CARD_INFO * mmc_sd_card );
extern int RtSdcard_Send_Stop( SD_CARD_INFO * mmc_sd_card );
extern int RtSdcard_Send_BlockCount( SD_CARD_INFO * mmc_sd_card, unsigned long block_count );
extern int RtSdcard_Send_Write_Block( SD_CARD_INFO * mmc_sd_card, unsigned long blockNum);
extern int RtSdcard_Send_Read_Block( SD_CARD_INFO * mmc_sd_card , unsigned long blockNum, unsigned long blk_len);
extern int RtSdcard_Send_ACMD_SCR(SD_CARD_INFO *pmmc_sd_card);
extern int RtSdcard_Send_Write_Multiple_Block( SD_CARD_INFO * mmc_sd_card, unsigned long blockNum); //MA
extern void rtsdcardReportRemovalEvent(int unit);
extern void rtsdcardReportInsertionEvent(int unit);
#if (ENABLE_DMA)
static void RtSdBSP_Dma_Setup(SD_CARD_INFO * mmc_sd_card,unsigned char *pBbuffer, int Reading);
static int RtSdBSP_Dma_Complete(SD_CARD_INFO * mmc_sd_card, int Reading);
#endif

static int RtSdBSP_PolledReadXfer(unsigned char *pBbuffer,unsigned long count_of_data);
static int RtSdBSP_PolledWriteXfer(unsigned long *pcBbuffer,unsigned long remain);

static void RtSdBSP_Enable_Card_Isr(void);
void RtSdBSP_Set_Clock(int Clock_rate );


/*



*/

extern unsigned long install_irq(unsigned long IntNumber, void *HandlerAddr, unsigned long Priority);

static void Card_Check_Insert(void)
{
unsigned long l;
    l=FIO2PIN;
  /* If the pin reads zero the card is inserted.
       The signal should be de-bounced because several edges may occur per removal */
    if (l & (1<<11))
    {
      Card_Detect_Inserted=0;
      rtsdcardReportRemovalEvent(0);
    }
    else
    {
      Card_Detect_Inserted=1;
      rtsdcardReportInsertionEvent(0);
    }
}
static void Card_Detect_Isr (void)
{
    IO2INTCLR |= (1 << 11);
    VICSoftIntClr = 0xffffffff;
    VICAddress=0; // Ack the interrupt
    Card_Detect_Events+=1;
    Card_Check_Insert();
}
static void RtSdBSP_Enable_Card_Detect_Isr(void)
{
  IO2INTCLR |= (1 << 11);
  IO2INTENR |= (1 << 11);
  IO2INTENF |= (1 << 11);

  install_irq(VIC_EINT3, (void *)Card_Detect_Isr, EINT3_PRIORITY);

  IO2INTCLR |= (1 << 11);
  VICIntEnable |= (1 << VIC_EINT3);
}


static void RtSdBSP_PowerOff(void)
{
    SCS |= 0x08;                    /* SCS register bit 3 corresponds to MCIPWR 0 the MCIPWR pin is low. 1 The MCIPWR pin is high */
    MCIPower =0x00;                 /* Shut down bus power */
    MCIMask0 = 0x00 ;               /* Disable all interrupts for now */

    RtSdBSP_Delay_micros(1);

    // SD_CMD_MODE = 2;
    MCICommand = 0;
    RtSdBSP_Delay_micros(1);
    MCIDataCtrl = 0;
    RtSdBSP_Delay_micros(1);
    MCIClear = 0x7FF;               /* clear all interrupts */

    MCIClock &=  ~(1 << 11)|(0x16); /* disabled, 1 bit bus, clock to slow mode < 400 khz */
    RtSdBSP_Delay_micros(1);
}

int RtSdBSP_Controller_Init(void)
{
    /* Power the PCONP register */
    /* On Reset, the SD/MMC is disabled PCMCI=0 in PCONP */
// Linux
//    PINSEL1  &= ~(0x3<<12);

    /* P2.11 - EINT1 */
    PINSEL4  &= ~(0x3<<22);
    PINSEL4   |= (0x0<<22); /* GPIO */

    PINMODE4 &= ~(0x3<<22);
    PINMODE4  |= (0x0<<22);

    /* PCONP - set power for SD block PCSDC bit */
//    PCONP |= (1<<28);


    PCONP |= 0x10000000;         /* (1<<28) */

#if(ENABLE_DMA)
    PCONP |= (1<<29);

  // enable DMA , little endian
    DMACConfiguration = 1;
    DMACSync = 0;           // DMA sync enable
    DMACIntErrClr = 3;
    DMACIntTCClear = 3;
    DMACC0Configuration = 0;
    DMACC1Configuration = 0;

#endif
// FC== CLK/4 FD == CLK, FE == CLK/2 FF=CLK/8
    PCLKSEL1 &= 0xFCFFFFFF; // PCCLK_MCICLK_DIV;

    PINSEL2 |= 0x20 ;              /* Set GPIO Port 1.2 to MCICLK   */
    PINSEL2 |= 0x80 ;              /* Set GPIO Port 1.3 to MCICMD   */
    PINSEL2 |= 0x800;              /* Set GPIO Port 1.5 to MCIPWR   */
    PINSEL2 |= 0x2000;          /* Set GPIO Port 1.6 to MCIDAT0  */
    PINSEL2 |= 0x8000;          /* Set GPIO Port 1.7 to MCIDAT1  */
    PINSEL2 |= 0x800000;        /* Set GPIO Port 1.11 to MCIDAT2 */
    PINSEL2 |= 0x2000000;        /* Set GPIO Port 1.12 to MCIDAT3 */

    PINMODE2 |= 0x280A8A0;         /* MCI/SD Pins have neither pull-up nor pull down resistor, the value is
                                 * 00000010100000001010100010100000 */

    SCS |= 0x08;                 /* SCS register bit 3 corresponds to MCIPWR 0 the MCIPWR pin is low. 1 The MCIPWR pin is high */

    RtSdBSP_PowerOff();

    if(MCIClock & 0x100)
        MCIClock &= ~(1 << 8);

    if(MCIPower & 0x2)
        MCIPower =0x00;

    MCIPower |= 0x02;
    while ( !(MCIPower & 0x02) );         /* PVOPVO - Endless */
    RtSdBSP_Delay_micros(10000);          /* When the external  power supply is switched on, the software first enters the power-up phase,
                                          * and wait until the supply output is stable before moving to the power-on phase. */

    MCIPower |= 0x01;                    /* bit 1 is set already, from power up to power on */

    RtSdBSP_Delay_micros(100000);         /* When the external  power supply is switched on, the software first enters the power-up phase,
                                         * and wait until the supply output is stable before moving to the power-on phase. */

    MCIMask0 = 0x00 ;                     /* Mask the interrupts Disable all interrupts for now */
    MCIClear = 0x7FF;                     /* Write 0B11111111111 to the MCI Clear register to clear the status flags */

    /* Pins are selected and power is applied, interrupts are disabled */

    /* Set the initial clock rate, we'll raise it later */
    RtSdBSP_Set_Clock(0);

    RtSdBSP_Enable_Card_Detect_Isr();
#if(CONFIGURE_SDMMC_INTERRUPTS)
    RtSdBSP_Enable_Card_Isr();
#endif  //CONFIGURE_SDMMC_INTERRUPTS

    return 0;
}

/* Experimentation yields these working sets
MCLKDIV_SLOW        23 = 391,304Hz -> @18Mhz/(2*60) works on commands to get started but reads bad data if we run this way.
CLKDIV_NORMAL       (0x6-1)  1.5Mhz -> @18Mhz/(2*6) Works on data
CLKDIV_NORMAL       (0x3-1)  3Mhz -> @18Mhz/(2*3) Works on data with interrupts
USE_BYPASS          Set this to one to use 18 MHZ clock undivided, some luck with DMA
*/

#define MCLKDIV_SLOW        0x17-1 /* 23 = 391,304Hz -> @18Mhz/(2*60) */
#if(CONFIGURE_SDMMC_INTERRUPTS)
#define MCLKDIV_NORMAL        (0x5-1)  /* 9 = 3Mhz -> @18Mhz/(2*4) */
#else
#define MCLKDIV_NORMAL        (0x6-1)  /* 5 = 15Mhz -> @18Mhz/(2*4) */
#endif
#define USE_BYPASS 0        /* Set this to one to use 18 MHZ clock undivided. */


unsigned long SYS_GetFpclk(unsigned long clock_offset);

void RtSdBSP_Set_Clock(int rate )
{
    unsigned long ClkValue = 0;
#define MCI_PCLK_OFFSET     56 // See board.h
    rtp_printf("MCI Periph clock freq == %d \n",SYS_GetFpclk(MCI_PCLK_OFFSET));
    if(rate == 0)
    {
        ClkValue |= MCLKDIV_SLOW; /* Slow Clock */
    }
// 25 MHZ maximum 72MHZ/18 ==> ClkDiv=17
    else
    {
       ClkValue |= MCLKDIV_NORMAL; /* 0x06-1 */
#if (USE_BYPASS)
// This will work with DMA, getting errors when using fifo
        rtp_printf("MCI Bypass Bus clock freq == 18MHZ \n");
        MCIClock &=~(0xFF);                 /* Clear the clock divider */
        MCIClock |= (1<<10); // turn on bypass
        RtSdBSP_Delay_micros(1000);          /* Delay 3MCLK + 2PCLK before next write */
        return;
#endif
    }
    rtp_printf("MCI Bus clock freq == %d \n" ,SYS_GetFpclk(MCI_PCLK_OFFSET)/(2*(ClkValue+1)));
    MCIClock &=~(0xFF);                 /* Clear the clock divider */
    MCIClock |= (1<<8) |ClkValue;
    RtSdBSP_Delay_micros(1000);          /* Delay 3MCLK + 2PCLK before next write */
}


int RtSdBSP_Set_BusWidth(SD_CARD_INFO *pmmc_sd_card, int width )
{
    RtSdBSP_Delay_micros(100);
    if ( width == 1 )
    {
        MCIClock &=  ~(1 << 11);    /* 1 bit bus */
    }
    else if ( width == 4 )
    {
        MCIClock |=  (1 << 11);        /* 4 bit bus */
    }
    return 0;
}

void RtSdBSP_CardInit( SD_CARD_INFO * pmmc_sd_card )
{
  MCIPower |= (1 << 6 );        /* Set Open Drain output control for MMC */
  RtSdBSP_Delay_micros(3000);
}

void RtSdBSP_CardPushPullMode( SD_CARD_INFO * pmmc_sd_card )
{
  MCIPower &= ~(1 << 6 );        /* Clear Open Drain output control for MMC */
  RtSdBSP_Delay_micros(3000);
}

int RtSdBSP_SendCmd( unsigned long  CmdIndex, unsigned long  Argument, int  ExpectResp, int  AllowTimeout )
{
unsigned long  CmdData = 0;
unsigned long  CmdStatus;

    /* Clear appropriate status bits before sending the command */
    switch(CmdIndex)
    {
        case SEND_STATUS:
        case SEND_CSD:
//        case VOLTAGE_SWITCH:
        case SELECT_CARD:
            MCIClear |= (MCI_CMD_TIMEOUT | MCI_CMD_CRC_FAIL | MCI_CMD_RESP_END);
            break;
        case SET_BLOCK_COUNT:
        case STOP_TRANSMISSION:
        case READ_SINGLE_BLOCK:
        case WRITE_BLOCK:
        case READ_MULTIPLE_BLOCK:
        case WRITE_MULTIPLE_BLOCK:
            MCIClear |= MCI_CLEAR_ALL;
            break;
        // HEREHERE - We don't clear for any of these ? why
        case GO_IDLE_STATE:
        case SEND_OP_COND:
        case SEND_APP_OP_COND:
        case APP_CMD:
        case SET_ACMD_BUS_WIDTH:
        case ALL_SEND_CID:
        case SET_RELATIVE_ADDR:
        case SEND_IF_COND:
        case SEND_APP_SEND_SCR:
        case SEND_APP_OP_DISCONNECT:
          // HEREHERE try this
            MCIClear |= (MCI_CMD_TIMEOUT | MCI_CMD_CRC_FAIL | MCI_CMD_RESP_END);
            break;
    }

    /* the command engine must be disabled when we modify the argument or the peripheral resends */
    while ( (CmdStatus = MCIStatus) & MCI_CMD_ACTIVE )    /* Command in progress. */
    {
        MCICommand = 0;                                        /* PVOPVO endless */
        RtSdBSP_Delay_micros(10);
        MCIClear = CmdStatus | MCI_CMD_ACTIVE;
    }
    RtSdBSP_Delay_micros(100);

    /*set the command details, the CmdIndex should 0 through 0x3F only */
    CmdData |= (CmdIndex & 0x3F);
    if ( ExpectResp == EXPECT_NO_RESP )                    /* no response */
    {
        CmdData &= ~((1 << 6) | (1 << 7));                 /* Clear long response bit as well */
    }
    else if ( ExpectResp == EXPECT_SHORT_RESP )            /* expect short response */
    {
        CmdData |= (1 << 6);
    }
    else if ( ExpectResp == EXPECT_LONG_RESP )            /* expect long response */
    {
        CmdData |= (1 << 6) | (1 << 7);
    }

    if ( AllowTimeout )                                    /* allow timeout or not */
    {
        CmdData |= (1 << 8);
    }
    else
    {
        CmdData &= ~(1 << 8);
    }
    /*send the command*/
    CmdData |= (1 << 10);
    MCIArgument = Argument;                                /* Set the argument first, finally command */
    MCICommand = CmdData;
    return 0;
}

int RtSdBSP_GetCmdResp( int ExpectCmdData, int ExpectResp, unsigned long * CmdResp )
{
int CmdRespStatus = 0;
int LastCmdIndex;
unsigned long MCIRespCmdValue;
  if ( ExpectResp == EXPECT_NO_RESP )
    return ( 0 );
  while ( 1 )
  {    // PVOPVO endless
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
       if ( (LastCmdIndex == SET_BLOCK_COUNT)||(LastCmdIndex == SEND_OP_COND) || (LastCmdIndex == SEND_APP_OP_COND) || (LastCmdIndex == STOP_TRANSMISSION) )
       {
          MCICommand = 0;
          MCIArgument = 0xFFFFFFFF;
          break;                            /* ignore CRC error if it's a resp for SEND_OP_COND or STOP_TRANSMISSION. */
      }
      else
      {
        return -1;
      }
    }
    else if ( CmdRespStatus & MCI_CMD_RESP_END )
    {
        MCIClear = CmdRespStatus | MCI_CMD_RESP_END;
        break;                                /* cmd response is received, expecting response */
    }
  }
  MCIRespCmdValue=MCIRespCmd;
  if ( (MCIRespCmdValue & 0x3F) != ExpectCmdData )
  {
    /* If the response is not R1, in the response field, the Expected Cmd data
    won't be the same as the CMD data in SendCmd(). Below four cmds have
    R2 or R3 response. We don't need to check if MCI_RESP_CMD is the same
    as the Expected or not. */
    if ( (ExpectCmdData != SEND_OP_COND) && (ExpectCmdData != SEND_APP_OP_COND)    && (ExpectCmdData != ALL_SEND_CID) && (ExpectCmdData != SEND_CSD) )
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
  return ( 0 );                    /* Read MCI_RESP0 register assuming it's not long response. */
}


int RtSdBSP_Read_BlockSetup(SD_CARD_INFO * mmc_sd_card,unsigned long blockNum)
{
    MCIClear |= MCI_CLEAR_ALL;
    RtSdBSP_Delay_micros(100);

   if (mmc_sd_card->card_operating_flags & SDFLGMULTIBLOCK)
         mmc_sd_card->bytes_left_to_transfer = BLOCK_LENGTH*mmc_sd_card->blocks_to_transfer;
    else
         mmc_sd_card->bytes_left_to_transfer = BLOCK_LENGTH;
    mmc_sd_card->byte_buffer =(unsigned char *)mmc_sd_card->blk_buffer;

#if (ENABLE_DMA==0)
#if(CONFIGURE_SDMMC_INTERRUPTS)
    sd_sig_clear(mmc_sd_card);
    MCIClear |=   ((FIFO_RX_INT_MASK)|(DATA_END_INT_MASK)|(ERR_RX_INT_MASK));    /* FIFO RX interrupts only */
    MCIMask0 |= ((FIFO_RX_INT_MASK)|(DATA_END_INT_MASK)|(ERR_RX_INT_MASK));    /* FIFO RX interrupts only */
#else
    mmc_sd_card->cpu_level=PVOPVO_T_splhigh();
#endif
#else
    RtSdBSP_Dma_Setup(mmc_sd_card,(unsigned char *)mmc_sd_card->blk_buffer,1/* Reading*/);
#endif
    MCIDataTimer = MA_DATA_TIMER_VALUE; //MA: Increased the time out, huge now, optimize later
    MCIDataLength = mmc_sd_card->bytes_left_to_transfer;
    return 0;
}

void RtSdBSP_Read_BlockShutdown(SD_CARD_INFO * mmc_sd_card)
{
#if (ENABLE_DMA==0)
#if(CONFIGURE_SDMMC_INTERRUPTS)
    sd_sig_clear(mmc_sd_card);
    MCIMask0 &= ~((FIFO_RX_INT_MASK)|(DATA_END_INT_MASK)|(ERR_RX_INT_MASK));    /* FIFO RX interrupts only */
#else
    PVOPVO_T_splx(mmc_sd_card->cpu_level);
#endif
#endif
}

int RtSdBSP_Read_BlockTransfer(SD_CARD_INFO * mmc_sd_card)
{
unsigned long DataCtrl;
int ret_val=0;
#if (ENABLE_DMA)
      DataCtrl = ((1 << 0) | (1 << 1) | (1 << 3) | (LOG_BLOCK_LENGTH << 4));
#else
      /* Read, enable, block transfer, and data length */
      DataCtrl = ((1 << 0) | (1 << 1) | (LOG_BLOCK_LENGTH << 4));
#endif

  /* No break points after this or we'll get a fifo overrun */
      MCIDataCtrl = DataCtrl;
//      RtSdBSP_Delay_micros(1); //MA
#if (ENABLE_DMA==0)
#if(CONFIGURE_SDMMC_INTERRUPTS)
    ret_val = sd_sig_wait(mmc_sd_card,TIMEOUTVAL);
    if (ret_val==-1)
    {
        ;   /* Check ISR status */
    }
    else
    {
      if ( mmc_sd_card->bytes_left_to_transfer==0)
        mmc_sd_card->blocks_transfered =  mmc_sd_card->blocks_to_transfer;
    }
#else
    if (mmc_sd_card->card_operating_flags & SDFLGMULTIBLOCK)
    {
        while (mmc_sd_card->blocks_transfered < mmc_sd_card->blocks_to_transfer&&ret_val==0)
        {
            ret_val = RtSdBSP_PolledReadXfer((unsigned char *)mmc_sd_card->blk_buffer,mmc_sd_card->blocks_to_transfer*512);
            mmc_sd_card->blocks_transfered+=mmc_sd_card->blocks_to_transfer;
        }
    }
    else
    {
      ret_val = RtSdBSP_PolledReadXfer((unsigned char *)mmc_sd_card->blk_buffer,512);
      mmc_sd_card->blocks_transfered+=1;
    }
#endif
#else
      ret_val=RtSdBSP_Dma_Complete(mmc_sd_card,1/* Reading*/);
      if (ret_val==0)
        mmc_sd_card->blocks_transfered = mmc_sd_card->blocks_to_transfer;
#endif
//  mmc_sd_card->blocks_to_transfer=blockCount;
  return ret_val;
}




int RtSdBSP_Write_BlockSetup(SD_CARD_INFO * mmc_sd_card,unsigned long blockNum)
{
    MCIClear |= MCI_CLEAR_ALL;
    MCIDataCtrl = 0;
    RtSdBSP_Delay_micros(100);

    if (mmc_sd_card->card_operating_flags & SDFLGMULTIBLOCK)
         mmc_sd_card->bytes_left_to_transfer = BLOCK_LENGTH*mmc_sd_card->blocks_to_transfer;
    else
         mmc_sd_card->bytes_left_to_transfer = BLOCK_LENGTH;
    mmc_sd_card->byte_buffer =(unsigned char *)mmc_sd_card->blk_buffer;

#if (ENABLE_DMA==0)
    /* pre-load the fifo with the first 64 bytes of data */
    if(0)
    {
    unsigned long i,*fifo_ptr,*plBbuffer;
        fifo_ptr = (unsigned long *) 0xE008C080;
        plBbuffer = (unsigned long *)mmc_sd_card->byte_buffer;
        for (i =0;i < 16; i++)
        {
            *fifo_ptr        =     *plBbuffer++;
        }
        mmc_sd_card->bytes_left_to_transfer -= 64;
        mmc_sd_card->byte_buffer += 64;
    }
#if(CONFIGURE_SDMMC_INTERRUPTS)
    sd_sig_clear(mmc_sd_card);
    MCIClear |= ((FIFO_TX_INT_MASK)|(DATA_END_INT_MASK)|(ERR_TX_INT_MASK));
    MCIMask0 |= ((FIFO_TX_INT_MASK)|(DATA_END_INT_MASK)|(ERR_TX_INT_MASK));
#else
    mmc_sd_card->cpu_level=PVOPVO_T_splhigh();
#endif
#else
    RtSdBSP_Dma_Setup(mmc_sd_card,(unsigned char *)mmc_sd_card->blk_buffer,0/* writing */);
#endif
    MCIDataTimer = MA_DATA_TIMER_VALUE; //MA: Increased the time out, huge now, optimize later
    MCIDataLength =  mmc_sd_card->bytes_left_to_transfer;

    return 0;
}

void RtSdBSP_Write_BlockShutdown(SD_CARD_INFO * mmc_sd_card)
{
#if (ENABLE_DMA==0)
#if(CONFIGURE_SDMMC_INTERRUPTS)
    MCIMask0 &= ~((FIFO_TX_INT_MASK)|(DATA_END_INT_MASK)|(ERR_TX_INT_MASK));
#else
    PVOPVO_T_splx(mmc_sd_card->cpu_level);
#endif
#endif
}

int RtSdBSP_Write_BlockTransfer(SD_CARD_INFO * mmc_sd_card)
{
unsigned long DataCtrl;
int ret_val=0;
#if (ENABLE_DMA)
    DataCtrl = ((1 << 0) | (1 << 3) | (LOG_BLOCK_LENGTH << 4));
#else
    /* Write, block transfer, and data length */
    DataCtrl = ((1 << 0) | (LOG_BLOCK_LENGTH << 4));
#endif

  /* No break points after this or we'll get a fifo overrun */
    MCIDataCtrl = DataCtrl;

    RtSdBSP_Delay_micros(1); //MA

    ret_val = 0;


#if(ENABLE_DMA==0)
#if(CONFIGURE_SDMMC_INTERRUPTS)
    ret_val = sd_sig_wait(mmc_sd_card,TIMEOUTVAL);
    if (ret_val==-1)
    {
        ;   /* Check ISR status */
    }
    else
    {
      if ( mmc_sd_card->bytes_left_to_transfer==0)
        mmc_sd_card->blocks_transfered =  mmc_sd_card->blocks_to_transfer;
    }

#else
    {
        if (mmc_sd_card->card_operating_flags & SDFLGMULTIBLOCK)
        {
            while (mmc_sd_card->blocks_transfered < mmc_sd_card->blocks_to_transfer&&ret_val==0)
            {
                ret_val = RtSdBSP_PolledWriteXfer(mmc_sd_card->blk_buffer,mmc_sd_card->blocks_to_transfer*512);
                mmc_sd_card->blocks_transfered+=mmc_sd_card->blocks_to_transfer;
            }
        }
        else
        {
            ret_val = RtSdBSP_PolledWriteXfer(mmc_sd_card->blk_buffer,512);
            mmc_sd_card->blocks_transfered+=1;
        }
    }
#endif
#else
    {
        ret_val=RtSdBSP_Dma_Complete(mmc_sd_card,0);
        if (ret_val==0)
          mmc_sd_card->blocks_transfered = mmc_sd_card->blocks_to_transfer;

    }
#endif
    if (mmc_sd_card->card_operating_flags & SDFLGMULTIBLOCK)
    {
          /* Stop the transfer */
        if ((mmc_sd_card->card_operating_flags & SDFLGBLOCKCOUNT)==0)
              RtSdcard_Send_Stop(mmc_sd_card);
    }
    return ret_val;
}


extern unsigned long long rtp_GetMicroseconds(void);
static int RtSdBSP_PolledReadXfer(unsigned char *pBbuffer,unsigned long count_of_data)
{
unsigned long ShadowStatus;
unsigned long maxloops = 100000000;
unsigned long *plBbuffer = (unsigned long *)pBbuffer;
unsigned long *fifo_ptr;

  fifo_ptr = (unsigned long *) 0xE008C080;
  ShadowStatus=MCIStatus ;
  while((ShadowStatus & SD_RX_DATA_AVAILABLE)==0)
  {
      if (ShadowStatus & SD_RCV_ERRORS)
      {
          rtp_printf("\nStatus error on Polled read %X\n",ShadowStatus);
          return -1;
      }

      ShadowStatus=MCIStatus ;
      if (--maxloops==0)
      {
          rtp_printf("\npolledXFER timeout 1\n");
          return -1;
      }
  }

  while(count_of_data >= 32 && maxloops)
  {
      if(ShadowStatus & SD_FIFO_DATA_OVERRUN)
      {
          rtp_printf("DATA TIMEDOUT status == %X \n",ShadowStatus);
          return -1;
      }
      while (maxloops && (ShadowStatus & SD_FIFO_HALF_FULL)==0)
      {
          if(ShadowStatus & SD_FIFO_DATA_OVERRUN)
          {
              rtp_printf("DATA TIMEDOUT status == %X \n",ShadowStatus);
              return -1;
          }
          ShadowStatus=MCIStatus; //MA
        maxloops++;
      }
      *plBbuffer++ = *fifo_ptr;
      *plBbuffer++ = *fifo_ptr;
      *plBbuffer++ = *fifo_ptr;
      *plBbuffer++ = *fifo_ptr;
      *plBbuffer++ = *fifo_ptr;
      *plBbuffer++ = *fifo_ptr;
      *plBbuffer++ = *fifo_ptr;
      *plBbuffer++ = *fifo_ptr;
      count_of_data -= 32;
      ShadowStatus=MCIStatus; //MA
  }
  while(count_of_data && maxloops)
  {
      maxloops--;
      //READ FIFO LOOP
      if (count_of_data >= 4 && (ShadowStatus & SD_RX_DATA_AVAILABLE))
      {
          *plBbuffer++ = *fifo_ptr;
          count_of_data -= 4;
          ShadowStatus=MCIStatus; //MA
          if(ShadowStatus & SD_FIFO_DATA_OVERRUN)
              break;
      }
      else if (count_of_data && count_of_data < 4)
      {
//#if (DEBUG_SDCARD_DRIVER)
          unsigned long length_of_data =  MCIDataLength;
          rtp_printf("Too much Length =%d and the count of data to be read =%d, read so far = %d \n",length_of_data, count_of_data, (length_of_data-count_of_data) );
//#endif //DEBUG_SDCARD_DRIVER
          return -1;
      }
      ShadowStatus=MCIStatus ; //MA
      if(ShadowStatus & SD_FIFO_DATA_OVERRUN)
      {
//#if (DEBUG_SDCARD_DRIVER)
          rtp_printf("DATA TIMEDOUT status == %X \n",ShadowStatus);
//#endif //DEBUG_SDCARD_DRIVER
          return -1;
      }
  }
  if (maxloops==0)
  {
//#if (DEBUG_SDCARD_DRIVER)
          rtp_printf("Loopmax exceeded\n");
//#endif //DEBUG_SDCARD_DRIVER
      return -1;
  }
  else
      return 0;

}
/* Note: remain is in bytes not in longs transfered */
static int RtSdBSP_PolledWriteXfer(unsigned long *pcBbuffer,unsigned long remain)
{
unsigned long *fifo_ptr;
unsigned long maxloops = 100000000;
fifo_ptr = (unsigned long *) 0xE008C080;

unsigned long status;
unsigned long *plBbuffer = (unsigned long *)pcBbuffer;
    do {
        int count;

        if(remain > 32)
           count = 32;
        else
           count = (int)remain;

        status = MCIStatus;

        if (status & TXFIFOHALFEMPTY)
            {
                *fifo_ptr        =    *plBbuffer     ;
                *(fifo_ptr+1 )    =    *(plBbuffer+1 )     ;
                *(fifo_ptr+2 )    =    *(plBbuffer+2 )      ;
                *(fifo_ptr+3 )    =    *(plBbuffer+3 )     ;
                *(fifo_ptr+4 )    =    *(plBbuffer+4 )     ;
                *(fifo_ptr+5 )    =    *(plBbuffer+5 )     ;
                *(fifo_ptr+6 )    =    *(plBbuffer+6 )     ;
                *(fifo_ptr+7 )    =    *(plBbuffer+7 )     ;

                plBbuffer+=8;
                remain -= count;
            }

        if (remain == 0)
        {
            break;
        }

        RtSdBSP_Delay_micros(10);
        maxloops--;
        status = MCIStatus;
    }
    while (status & MCI_TXACTIVE && maxloops>0);

    if(status & (1<<5) )
    {
#if (DEBUG_SDCARD_DRIVER)
        rtp_printf("DATA TIMEDOUT \n");
#endif //DEBUG_SDCARD_DRIVER
        return -1;  //TIMEOUT ERROR
    }
    return 0;
}


static void ShowFifoStats(char *p)
{
  return;
  {
unsigned long fifo_count = MCIFifoCnt;
unsigned long ShadowStatus=MCIStatus;
  rtp_printf("%s %d %x\n", p,  fifo_count, ShadowStatus);
  }
}
#define u32 unsigned long
unsigned long UNSTUFF_BITS(unsigned long resp[],int start, int size)                                   \
         {
                 const int __size = size;
                 const u32 __mask = (__size < 32 ? 1 << __size : 0) - 1;
                 const int __off = 3 - ((start) / 32);
                 const int __shft = (start) & 31;
                 u32 __res;

                 __res = resp[__off] >> __shft;
                 if (__size + __shft > 32)
                         __res |= resp[__off-1] << ((32 - __shft) % 32);
                 __res &= __mask;
                 return __res;
         }
int RtSdBSP_Read_SCR(SD_CARD_INFO * mmc_sd_card)
{
    unsigned long DataCtrl = 0;
    unsigned long level=0;
    int ret_val = -1;
    unsigned char buf[SCR_LENGTH];
    unsigned long i,tmp,scr[2];

    /* Red SCR isn't working but fake values for now to keep going */
    mmc_sd_card->card_operating_flags |= SDFLGMULTIBLOCK;
    // mmc_sd_card->card_operating_flags |= SDFLGBLOCKCOUNT;
    mmc_sd_card->card_operating_flags |= SDFLGHIGHSPEED;
    mmc_sd_card->card_operating_flags |= SDFLG4BITMODE;


   ShowFifoStats("Pre-clear");
    MCIClear |= MCI_CLEAR_ALL;
    RtSdBSP_Delay_micros(100);
   ShowFifoStats("Post-clear");
   /* Below status check is redundant, but ensure card is in TRANS state
        before writing and reading to from the card. */
    if (RtSdcard_CheckStatus(mmc_sd_card) != 0 )
    {
        RtSdcard_Send_Stop(mmc_sd_card);
        return -1;
    }
    level=PVOPVO_T_splhigh();
    MCIDataTimer = MA_DATA_TIMER_VALUE; //MA: Increased the time out, huge now, optimize later
    MCIDataLength = SCR_LENGTH;
    ShowFifoStats("Pre-send ACMS_SCR");
    DataCtrl = ((1 << 0) | (1 << 1) | (LOG_SCR_LENGTH << 4));
    MCIDataCtrl = DataCtrl;
    if (RtSdcard_Send_ACMD_SCR(mmc_sd_card) != 0)
        goto error_return;
    ShowFifoStats("Pos-send ACMS_SCR");
     /* Read, enable,  block transfer, and data length==LOG_SCR_LENGTH(log2(8)0<<4) */
//     DataCtrl = ((1 << 0) | (1 << 1) | (LOG_SCR_LENGTH << 4));
//     MCIDataCtrl = DataCtrl;
     ShowFifoStats("Post data ACMS_SCR");
     ret_val = RtSdBSP_PolledReadXfer(buf,SCR_LENGTH);
     PVOPVO_T_splx(level);
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


{
    unsigned long resp[4];
        int scr_struct,sda_vsn,bus_widths;

    resp[3] = scr[1];
    resp[2] = scr[0];

    scr_struct = UNSTUFF_BITS(resp, 60, 4);

    sda_vsn = UNSTUFF_BITS(resp, 56, 4);
    bus_widths = UNSTUFF_BITS(resp, 48, 4);
for(i=0; i < 8; i++)
  rtp_printf("buf[%d] == %X\n", i, buf[i]);
    rtp_printf("scr struct     = %x %x\n", scr_struct, buf[4]&0xf);
    rtp_printf("scr sda_vsn    = %x %x\n", sda_vsn, buf[5]&0xf);
    rtp_printf("scr bus_widths = %x %x\n", bus_widths, buf[6]&0xf);
}
#if (0)
    rtp_printf("V[0]== %x\n", buffer[0]);
    rtp_printf("Version %d\n", (buffer[0]>>28) & 0x0f);
    rtp_printf("Spec  %d\n", (buffer[0]>>24) & 0x0f);
    rtp_printf("Bus w  %d\n", (buffer[0]>>16) & 0x0f);
    rtp_printf("Spec 3 %d\n", (buffer[0]>>15) & 0x01);
    rtp_printf("CMD23 %d\n", (buffer[0]>>1) & 0x01);
    rtp_printf("CMD20 %d\n", (buffer[0]>>0) & 0x01);

    rtp_printf("V[1]== %x\n", buffer[1]);
    rtp_printf("Version %d\n", (buffer[1]>>28) & 0x0f);
    rtp_printf("Spec  %d\n", (buffer[1]>>24) & 0x0f);
    rtp_printf("Bus w  %d\n", (buffer[1]>>16) & 0x0f);
    rtp_printf("Spec 3 %d\n", (buffer[1]>>15) & 0x01);
    rtp_printf("CMD23 %d\n", (buffer[1]>>1) & 0x01);
    rtp_printf("CMD20 %d\n", (buffer[1]>>0) & 0x01);
#endif


    if (ret_val == -1)
        return -1;
    return 0;
error_return:
    return -1;
}



/*****************************************************************************************************
 *
 * Configuring the Interrupts for SDMMC
 *
 ******************************************************************************************************/
#if (CONFIGURE_SDMMC_INTERRUPTS)


static void RtSdBSP_FifoInterruptService(SD_CARD_INFO *pmmc_sd_card);

void Card_Event_Isr(void)
{
  unsigned long intr_mask,ShadowStatus;
  SD_CARD_INFO * pmmc_sd_card = &mmc_sd_cards[SD_CARD_UNIT_NUM];
  unsigned long *fifo_ptr = (unsigned long *) 0xE008C080;
  unsigned long *plBbuffer = (unsigned long *)pmmc_sd_card->byte_buffer;

    intr_mask = PVOPVO_T_splhigh();
    ShadowStatus = MCIStatus;
  /* handle MCI_STATUS interrupt */
    if ( ShadowStatus & (MMCSD_DATA_CRC_FAIL | MMCSD_DATA_TIMEOUT | MMCSD_TX_UNDERRUN | MMCSD_RX_OVERRUN | MMCSD_START_BIT_ERR))
    {
        MCIClear |= MCI_CLEAR_ALL;
        //MCI_DataErrorProcess_count++;
        pmmc_sd_card->BspErrorStatus = ShadowStatus;
        sd_sig_set(pmmc_sd_card);
        VICAddress=0; /* Ack */
        PVOPVO_T_splx(intr_mask);
        return;
    }
    if (ShadowStatus & FIFO_INT_MASK)
    {
 //     MCIClear |= FIFO_INT_MASK; // Done inside
        RtSdBSP_FifoInterruptService(pmmc_sd_card);
        pmmc_sd_card->BspFifoInterrupts += 1;
        VICAddress=0;             /* Ack the interrupt , we do it this way for USB??*/
        PVOPVO_T_splx(intr_mask);
        return;
    }
    if ( ShadowStatus & MMCSD_DATA_END) // |MMCSD_DATA_BLOCK_END))
    {
        MCIClear |= MMCSD_DATA_END;
        sd_sig_set(pmmc_sd_card);
        pmmc_sd_card->BspEndInterrupts += 1;
        VICAddress=0;             /* Ack the interrupt , we do it this way for USB??*/
        PVOPVO_T_splx(intr_mask);
        return;
    }
#if (0)
  else if ( ShadowStatus & CMD_INT_MASK )
  {
      /* Not Implemented Yet*/
      //MCI_CmdProcess();
      /* Ack the interrupt */
      VICVectAddr0 = 0;
//      PVOPVO_T_splx(intr_mask);
      return;
  }
#endif
}

//#define MCI_IRQENABLE	\
//	(MCI_CMDCRCFAILMASK|MCI_DATACRCFAILMASK|MCI_CMDTIMEOUTMASK|	\
//	MCI_DATATIMEOUTMASK|MCI_TXUNDERRUNMASK|MCI_RXOVERRUNMASK|	\
//	MCI_CMDRESPENDMASK|MCI_CMDSENTMASK|MCI_DATABLOCKENDMASK)

#define MCI_IRQENABLE	\
	(MCI_DATACRCFAILMASK|MCI_DATATIMEOUTMASK|MCI_TXUNDERRUNMASK|MCI_RXOVERRUNMASK||MCI_DATABLOCKENDMASK)


static void RtSdBSP_Enable_Card_Isr(void)
{
    install_irq(VIC_MMC, (void *)Card_Event_Isr, SDMMC_PRIORITY);
    VICIntEnable |= (1 << VIC_MMC);
}


static void RtSdBSP_FifoInterruptService(SD_CARD_INFO *pmmc_sd_card)
{
unsigned long *fifo_ptr;
unsigned long *plBbuffer;
unsigned long ShadowStatus = MCIStatus ;
    MCIClear |= FIFO_INT_MASK;
    fifo_ptr = (unsigned long *) 0xE008C080;
    plBbuffer = (unsigned long *)pmmc_sd_card->byte_buffer;
    if ( (ShadowStatus & (FIFO_TX_INT_MASK) ) && ( ShadowStatus & MMCSD_TX_ACTIVE ) )
    {
        int flush_output=0;
            if (ShadowStatus & MMCSD_TX_FIFO_EMPTY)
            {
                if (pmmc_sd_card->bytes_left_to_transfer>=64)
                {
                    *fifo_ptr        =     *plBbuffer          ;
                    *(fifo_ptr+1 )    =    *(plBbuffer+1 )     ;
                    *(fifo_ptr+2 )    =    *(plBbuffer+2 )  ;
                    *(fifo_ptr+3 )    =    *(plBbuffer+3 )     ;
                    *(fifo_ptr+4 )    =    *(plBbuffer+4 )     ;
                    *(fifo_ptr+5 )    =    *(plBbuffer+5 )     ;
                    *(fifo_ptr+6 )    =    *(plBbuffer+6 )     ;
                    *(fifo_ptr+7 )    =    *(plBbuffer+7 )     ;
                    *(fifo_ptr+8 )    =    *(plBbuffer+8 )     ;
                    *(fifo_ptr+9 )    =    *(plBbuffer+9 )     ;
                    *(fifo_ptr+10 )    =    *(plBbuffer+10 ) ;
                    *(fifo_ptr+11 )    =    *(plBbuffer+11 ) ;
                    *(fifo_ptr+12 )    =    *(plBbuffer+12 ) ;
                    *(fifo_ptr+13 )    =    *(plBbuffer+13 ) ;
                    *(fifo_ptr+14 )    =    *(plBbuffer+14 ) ;
                    *(fifo_ptr+15 )    =    *(plBbuffer+15 ) ;
                    plBbuffer+=16;
                    pmmc_sd_card->bytes_left_to_transfer-=64;
                    pmmc_sd_card->byte_buffer+=64;
                }
                else
                  flush_output = 1;
            }//else if data fifo empty
            else if(ShadowStatus & TXFIFOHALFEMPTY)
            {
                if (pmmc_sd_card->bytes_left_to_transfer>=64)
                {
                    *fifo_ptr        =    *plBbuffer     ;
                    *(fifo_ptr+1 )    =    *(plBbuffer+1 )     ;
                    *(fifo_ptr+2 )    =    *(plBbuffer+2 )      ;
                    *(fifo_ptr+3 )    =    *(plBbuffer+3 )     ;
                    *(fifo_ptr+4 )    =    *(plBbuffer+4 )     ;
                    *(fifo_ptr+5 )    =    *(plBbuffer+5 )     ;
                    *(fifo_ptr+6 )    =    *(plBbuffer+6 )     ;
                    *(fifo_ptr+7 )    =    *(plBbuffer+7 )     ;
                    pmmc_sd_card->bytes_left_to_transfer-=32;
                    pmmc_sd_card->byte_buffer+=32;
                    plBbuffer+=8;
                }
                else
                    flush_output = 1;
            }//if data fifo half empty
            if (flush_output)
            {
                int i = pmmc_sd_card->bytes_left_to_transfer;
                pmmc_sd_card->byte_buffer += i;
                pmmc_sd_card->bytes_left_to_transfer = 0;
                while (i > 0)
                {
                    *fifo_ptr = *plBbuffer++;
                    i-=4;
                }
            }
    }
    else if ( (ShadowStatus & (FIFO_RX_INT_MASK) ) && ( ShadowStatus & MMCSD_RX_ACTIVE ) )
    {
    int flush_fifo=0;
        if (ShadowStatus & SD_FIFO_FULL)
        {
            if (pmmc_sd_card->bytes_left_to_transfer>=64)
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
                 plBbuffer+=16;
                 pmmc_sd_card->bytes_left_to_transfer-=64;
                 pmmc_sd_card->byte_buffer+=64;
            }
            else
                flush_fifo = 1;
        }
        else if (ShadowStatus & SD_FIFO_HALF_FULL)
        {
            if (pmmc_sd_card->bytes_left_to_transfer>=32)
            {
                *plBbuffer = *fifo_ptr;
                *(plBbuffer+1 ) = *(fifo_ptr+1 );
                *(plBbuffer+2 ) = *(fifo_ptr+2 );
                *(plBbuffer+3 ) = *(fifo_ptr+3 );
                *(plBbuffer+4 ) = *(fifo_ptr+4 );
                *(plBbuffer+5 ) = *(fifo_ptr+5 );
                *(plBbuffer+6 ) = *(fifo_ptr+6 );
                *(plBbuffer+7 ) = *(fifo_ptr+7 );
                 plBbuffer+=8;
                 pmmc_sd_card->bytes_left_to_transfer-=32;
                 pmmc_sd_card->byte_buffer+=32;
            }
            else
                flush_fifo = 1;
        }
        if (flush_fifo)
        {
            int i = pmmc_sd_card->bytes_left_to_transfer;
            pmmc_sd_card->byte_buffer += i;
            pmmc_sd_card->bytes_left_to_transfer = 0;
            while (i>0)
            {
               *plBbuffer++= *fifo_ptr;
               i-=4;
            }
        }
    }
//    if (!pmmc_sd_card->bytes_left_to_transfer)
//        SIGNAL;

    return;
}

#endif /* #if (CONFIGURE_SDMMC_INTERRUPTS) */


#if (INCLUDE_DMA)

#define TCI 0
#define MEMWIDTH  0
#define FIFOWIDTH  2

#define DBSIZEREAD     2 // Burst size 8
#define SBSIZEREAD     2 //??
#define DBSIZEWRITE    2 //??
#define SBSIZEWRITE    2 //??

#define DIREAD 1
#define DIWRITE 0
#define SIREAD  0
#define SIWRITE 1
#define TSIZE 0

#define MMCPERIPHERAL 0x04
#define DMAPERIPHERALTOMEMORY 0x06
#define DMAMEMORYTOPERIPHERAL 0x05
#define DMAMEMORYTOMEMORY 0x0

static void RtSdBSP_Dma_Setup(SD_CARD_INFO * mmc_sd_card,unsigned char *pBbuffer, int Reading)
{
unsigned long l;

    /* Enable DMA */
    l=DMACConfiguration;
    if (!(l&1))
          DMACConfiguration=1;

    DMACIntTCClear=0x01;
    DMACIntErrClr=0x01;
    /* Disable the channel */
    DMACC0Configuration    = 0; // DMACC0Configuration&0xfffffffe;

    if (Reading)
    {
        DMACC0SrcAddr        = 0xE008C080; // MCI fifo
        DMACC0DestAddr        =  (unsigned long) pBbuffer;
        DMACC0LLI = 0; // No link just a single transaction.
        l = (TCI<<31)|(DIREAD<<27)|(SIREAD<<26)|(MEMWIDTH<<21)|(FIFOWIDTH<<18)|(DBSIZEREAD<<15)|(SBSIZEREAD<<12)|(TSIZE);
        DMACC0Control         = l;
        l = 1|(MMCPERIPHERAL<<1)|(DMAPERIPHERALTOMEMORY<<11);
        DMACC0Configuration    = l;
        }
    else
    {
        DMACC0SrcAddr        = (unsigned long) pBbuffer;
        DMACC0DestAddr        = 0xE008C080; // MCI fifo
        DMACC0LLI = 0; // No link just a single transaction.
        l =  (TCI<<31)|(DIWRITE<<27)|(SIWRITE<<26)|(FIFOWIDTH<<21)|(MEMWIDTH<<18)|(DBSIZEWRITE<<15)|(SBSIZEWRITE<<12)|(TSIZE);
        DMACC0Control        = l;
        l = 1|(MMCPERIPHERAL<<6)|(DMAMEMORYTOPERIPHERAL<<11);
        DMACC0Configuration    = l;
    }

}

static int RtSdBSP_Dma_Complete(SD_CARD_INFO * mmc_sd_card, int Reading)
{
unsigned long mcistatus, dmatcstatus, dmaerrorststatus;
unsigned long maxloops = 100000000;
int ret_val=-1;
    while (maxloops--)
    {
 #if(0)
       dmatcstatus            = DMACRawIntTCStatus;
       if (dmatcstatus)
       {
            rtp_printf("DMA TC \n");
            ret_val = 0;
       }
#endif
         dmaerrorststatus=DMACRawIntErrorStatus;
        if (dmaerrorststatus)
        {
            DMACIntErrClr=dmaerrorststatus;
            rtp_printf("DMA status error\n");
            ret_val = -1;
            break;
        }

        mcistatus = MCIStatus;
        if (mcistatus&(MCI_DATACRCFAILMASK|MCI_DATATIMEOUTMASK|MCI_TXUNDERRUNMASK|MCI_RXOVERRUNMASK))
        {
            MCIClear = mcistatus&(MCI_DATACRCFAILMASK|MCI_DATATIMEOUTMASK|MCI_TXUNDERRUNMASK|MCI_RXOVERRUNMASK|MCI_DATABLOCKENDMASK);
            rtp_printf("Mci status error %d\n", mcistatus);
            ret_val = -1;
        }
        if (mcistatus&(MCI_DATABLOCKENDMASK))
        {
                unsigned long l;
            MCIClear = mcistatus&(MCI_DATACRCFAILMASK|MCI_DATATIMEOUTMASK|MCI_TXUNDERRUNMASK|MCI_RXOVERRUNMASK|MCI_DATABLOCKENDMASK);
            ret_val = 0;
                        break;
        }

    }

    return ret_val;
}
#endif /* (INCLUDE_DMA) */
