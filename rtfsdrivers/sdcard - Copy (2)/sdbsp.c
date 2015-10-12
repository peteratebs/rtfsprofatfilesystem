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


#define LPC24XXMCI 1
#define MICROSPERLOOP 10


#define SD_CMD_CRC_FAIL         (1<<0)
#define SD_DATA_CRC_FAIL	(1<<1)
#define SD_FIFO_CMD_ TIMEOUT    (1<<2)
#define SD_FIFO_DATA_TIMEOUT	(1<<3)
#define SD_START_BIT_ERR	(1<<9)
#define SD_FIFO_DATA_OVERRUN    (1<<5)
#define SD_FIFO_HALF_FULL       (1<<15)
#define SD_FIFO_FULL            (1<<17)
#define SD_FIFO_EMPTY           (1<<19)
#define SD_RX_DATA_AVAILABLE    (1<<21)
#define TX_DATA_AVAILABLE       (1 << 21)
#define TX_FIFO_EMPTY           (1 << 18)
#define TX_FIFO_FULL            (1 << 16)
#define TXFIFOHALFEMPTY         (1 << 14)
#define MCI_TXACTIVE            (1 << 12)
#define SD_RCV_ERRORS (SD_DATA_CRC_FAIL|SD_FIFO_DATA_TIMEOUT|SD_START_BIT_ERR|SD_FIFO_DATA_OVERRUN)

unsigned long maxwloops = 100000000;
volatile int MCI_Block_End_Flag = 0;
static int Card_Detect_Events =0;
static int Card_Detect_Events_Reported =0;
static int Card_Detect_Inserted=0;
#define DATA_TIMER_VALUE        0x10000    /* Check if I need to scale this down, or if it can be determined programatically from the CSD  */
#define MA_DATA_TIMER_VALUE     0x80000000 //Check if I need to scale this down, or if it can be determined programatically from the CSD


#if (ENABLE_DMA)
//#include "dma.h"
#endif

#if(CONFIGURE_SDMMC_INTERRUPTS)
#include "irq.h"
#endif  //CONFIGURE_SDMMC_INTERRUPTS

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

static void RtSdBSP_Dma_Setup(SD_CARD_INFO * mmc_sd_card,unsigned char *pBbuffer, int Reading);
static int RtSdBSP_Dma_Complete(SD_CARD_INFO * mmc_sd_card, int Reading);

static void MCI_TXEnable( void );
static void MCI_RXEnable( void );
void RtSdBSP_Set_Clock(int Clock_rate );


void RtSdBSP_Delay_micros(unsigned long delay)
{
int i;
    for (i = 0; i < delay * MICROSPERLOOP; i++)
        ;
}

/* Experimentation yields these working sets

MCLKDIV_SLOW        23 = 391,304Hz -> @18Mhz/(2*60) works on commands to get started but reads bad data if we run this way.
CLKDIV_NORMAL       (0x6-1)  1.5Mhz -> @18Mhz/(2*6) Works on data
can't reducing CLKDIV_NORMAL any lower or it fails

Things to try -
	1.
	CMD23 set block count to eliminate stop


*/

#define MCLKDIV_SLOW        0x17-1 /* 23 = 391,304Hz -> @18Mhz/(2*60) */
#define MCLKDIV_NORMAL        (0x6-1)  /* 5 = 3Mhz -> @18Mhz/(2*4) */          //TODO:might need To figure out the max freq depending on the SD Card Class, conditional to the CSD Register description??
//#define MCLKDIV_NORMAL       (0x6-1)  /* 3 = 6Mhz -> @18Mhz/(2*4) */            //TODO:Experimental, to remove after the optimization of the read loop
// FC== CLK/4 FD == CLK, FE == CLK/2 FF=CLK/8
#define PCLK_MCICLK_MASK  0xF0FFFFFF
//#define PCLK_MCICLK_DIV_4 0x0C000000
//#define PCLK_MCICLK_DIV_2 0x0E000000
//#define PCLK_MCICLK_DIV_1 0x0D000000
#define PCLK_MCICLK_DIV_4 0xFCFFFFFF
#define PCLK_MCICLK_DIV_2 0xFEFFFFFF
#define PCLK_MCICLK_DIV_1 0xFDFFFFFF

#define PCCLK_MCICLK_DIV PCLK_MCICLK_DIV_4

unsigned long install_irq(unsigned long IntNumber, void *HandlerAddr, unsigned long Priority);

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

static void RtSdBSP_Enable_Card_Isr(void)
{
  MCIMASK0 = MCI_IRQENABLE;
}

int RtSdBSP_Controller_Init(void)
{
#if (LPC24XXMCI)
    /* Power the PCONP register */
    /* On Reset, the SD/MMC is disabled PCMCI=0 in PCONP */
#if(1)
// Linux
//    PINSEL1  &= ~(0x3<<12);

    /* P2.11 - EINT1 */
    PINSEL4  &= ~(0x3<<22);
    PINSEL4   |= (0x0<<22); /* GPIO */

    PINMODE4 &= ~(0x3<<22);
    PINMODE4  |= (0x0<<22);

    /* PCONP - set power for SD block PCSDC bit */
//    PCONP |= (1<<28);
#endif


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
 //   l = PCLKSEL1;
 //rtp_printf("PCLKSEL1 1 %x \n",l);
 //   l &= PCLK_MCICLK_MASK;
 //   l |= PCCLK_MCICLK_DIV;
 //   PCLKSEL1 = l;
 //   l = PCLKSEL1;
 //rtp_printf("PCLKSEL1 1 %x \n",l);


//    PCLKSEL1 &= PCCLK_MCICLK_DIV; /* Divide 72 MHZ clock by this to source the clock */
// rtp_printf("PCLKSEL1 2 %x \n",PCLKSEL1);
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
    RtSdBSP_Set_Clock(SLOW_RATE);

    RtSdBSP_Enable_Card_Detect_Isr();

    return 0;
#else
#error
#endif
}
#define MCI_PCLK_OFFSET     56 // See board.h

void RtSdBSP_Set_Clock(int Clock_rate )
{
#if (LPC24XXMCI)
    unsigned long ClkValue = 0;

    rtp_printf("MCI Periph clock freq == %d \n",SYS_GetFpclk(MCI_PCLK_OFFSET));

    if(Clock_rate == SLOW_RATE)
    {
        ClkValue |= MCLKDIV_SLOW; /* Slow Clock */
    }
// 25 MHZ maximum 72MHZ/18 ==> ClkDiv=17
    else if(Clock_rate == NORMAL_RATE)
    {
       ClkValue |= MCLKDIV_NORMAL; /* 0x06-1 */
#define USE_BYPASS 0
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
#else
#error
#endif
}

int RtSdBSP_Set_BusWidth(SD_CARD_INFO *pmmc_sd_card, int width )
{
#if (LPC24XXMCI)
    RtSdBSP_Delay_micros(100);
    if ( width == SD_1_BIT )
    {
        MCIClock &=  ~(1 << 11);    /* 1 bit bus */
    }
    else if ( width == SD_4_BIT )
    {
        MCIClock |=  (1 << 11);        /* 4 bit bus */
    }
#else
#error
#endif
    return 0;
}

void RtSdBSP_CardInit( SD_CARD_INFO * pmmc_sd_card )
{
#if (LPC24XXMCI)
  MCIPower |= (1 << 6 );        /* Set Open Drain output control for MMC */
  RtSdBSP_Delay_micros(3000);
#else
#error
#endif
}
void RtSdBSP_CardPushPullMode( SD_CARD_INFO * pmmc_sd_card )
{
#if (LPC24XXMCI)
  MCIPower &= ~(1 << 6 );        /* Clear Open Drain output control for MMC */
  RtSdBSP_Delay_micros(3000);
#else
#error
#endif
}

/******************************************************************************
** Function name:        RtSdBSP_SendCmd
**
** Descriptions:
**
** parameters:
** Returned value:
**
******************************************************************************/

int RtSdBSP_SendCmd( unsigned long  CmdIndex, unsigned long  Argument, int  ExpectResp, int  AllowTimeout )
{
#if (LPC24XXMCI)
unsigned long  CmdData = 0;
unsigned long  CmdStatus;

  /* the command engine must be disabled when we modify the argument
  or the peripheral resends */
  while ( (CmdStatus = MCIStatus) & MCI_CMD_ACTIVE )    /* Command in progress. */
  {
    MCICommand = 0;                                        /* PVOPVO endless */
    MCIClear = CmdStatus | MCI_CMD_ACTIVE;
  }
  RtSdBSP_Delay_micros(100);

  /*set the command details, the CmdIndex should 0 through 0x3F only */
  CmdData |= (CmdIndex & 0x3F);                            /* bit 0 through 5 only */
  if ( ExpectResp == EXPECT_NO_RESP )                    /* no response */
  {
    CmdData &= ~((1 << 6) | (1 << 7));                    /* Clear long response bit as well */
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
  CmdData |= (1 << 10);                                    /* This bit needs to be set last. */
  MCIArgument = Argument;                                /* Set the argument first, finally command */
  MCICommand = CmdData;

  return 0;
#else
#error
#endif
}

int RtSdBSP_GetCmdResp( int ExpectCmdData, int ExpectResp, unsigned long * CmdResp )
{
int CmdRespStatus = 0;
int LastCmdIndex;
unsigned long MCIRespCmdValue;
  if ( ExpectResp == EXPECT_NO_RESP )
    return ( 0 );
#if (LPC24XXMCI)
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
#else
#error
#endif
}

unsigned long long rtp_GetMicroseconds(void);
#if (0)
static int RtSdBSP_PolledReadXfer(unsigned char *pBbuffer,unsigned long count_of_data)
{
unsigned long ShadowStatus;
unsigned long maxloops = 100000000;
unsigned long fifo_count_start;
unsigned long *plBbuffer = (unsigned long *)pBbuffer;
unsigned long *fifo_ptr;

  fifo_count_start = MCIFifoCnt;
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
  fifo_ptr = (unsigned long *) 0xE008C080;
  while(count_of_data && maxloops)
  {
      maxloops--;
          //READ FIFO LOOP
      if (count_of_data >= 32 && (ShadowStatus & SD_FIFO_HALF_FULL))
      {
          memcpy(plBbuffer,fifo_ptr,32);
          count_of_data -= 32;
          plBbuffer+=8;
      }
      else if (count_of_data >= 4 && (ShadowStatus & SD_RX_DATA_AVAILABLE))
      {
          *plBbuffer++ = *fifo_ptr;
          count_of_data -= 4;
      }
      else if (count_of_data == 0)
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
#else
static int RtSdBSP_PolledReadXfer(unsigned char *pBbuffer,unsigned long count_of_data)
{
unsigned long ShadowStatus;
unsigned long maxloops = 100000000;
unsigned long fifo_count,fifo_count_start;
unsigned long *plBbuffer = (unsigned long *)pBbuffer;
unsigned long *fifo_ptr;

  fifo_ptr = (unsigned long *) 0xE008C080;
  fifo_count_start = MCIFifoCnt;
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
#endif
/* Note: remain is in bytes not in longs transfered */
static int RtSdBSP_PolledWriteXfer(unsigned long *pcBbuffer,unsigned long remain)
{
#if(ENABLE_DMA==0)
unsigned long *fifo_ptr;
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


void RtSdBSP_ClearStatus(SD_CARD_INFO * mmc_sd_card,unsigned long clear_mask)
{
    MCIClear |= clear_mask;
}
//===========================

int RtSdBSP_Write_BlockSetup(SD_CARD_INFO * mmc_sd_card,unsigned long blockNum)
{
    RtSdBSP_ClearStatus( mmc_sd_card,MCI_CLEAR_ALL);
    MCIDataCtrl = 0;
    RtSdBSP_Delay_micros(100);

#if(CONFIGURE_SDMMC_INTERRUPTS)
    SD_CARD_UNIT_NUM = unitnumber;
    rtp_sig_semaphore_clear( *mmc_sd_card->sdmmc_rw_sm );
#endif


#if (ENABLE_DMA==0)
    mmc_sd_card->cpu_level=PVOPVO_T_splhigh();
#else
    RtSdBSP_Dma_Setup(mmc_sd_card,(unsigned char *)mmc_sd_card->blk_buffer,0/* writing */);
#endif
    MCIDataTimer = MA_DATA_TIMER_VALUE; //MA: Increased the time out, huge now, optimize later
	if (mmc_sd_card->card_operating_flags & SDFLGMULTIBLOCK)
    	MCIDataLength = BLOCK_LENGTH*mmc_sd_card->blocks_to_transfer;
	else
    	MCIDataLength = BLOCK_LENGTH;
 
	return 0;
}

void RtSdBSP_Write_BlockShutdown(SD_CARD_INFO * mmc_sd_card)
{
#if (ENABLE_DMA==0)
    PVOPVO_T_splx(mmc_sd_card->cpu_level);
#endif
}

int RtSdBSP_Write_BlockTransfer(SD_CARD_INFO * mmc_sd_card)
{
unsigned long DataCtrl;
int ret_val=0;
#if (ENABLE_DMA)
    DataCtrl = ((1 << 0) | (1 << 3) | (DATA_BLOCK_LEN << 4));
#else
    /* Write, block transfer, and data length */
    DataCtrl = ((1 << 0) | (DATA_BLOCK_LEN << 4));
#endif

  /* No break points after this or we'll get a fifo overrun */
    MCIDataCtrl = DataCtrl;

    RtSdBSP_Delay_micros(1); //MA

    ret_val = 0;

#if(ENABLE_DMA==0)
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
#else
    {
      	ret_val=RtSdBSP_Dma_Complete(mmc_sd_card,0);
        if (ret_val==0)
          mmc_sd_card->blocks_transfered = mmc_sd_card->blocks_to_transfer;

    }
#endif
    if (ret_val == -1)
    	return -1;
    if (mmc_sd_card->card_operating_flags & SDFLGMULTIBLOCK)
    {
      	/* Stop the transfer */
    	if ((mmc_sd_card->card_operating_flags & SDFLGBLOCKCOUNT)==0)
      		RtSdcard_Send_Stop(mmc_sd_card);
	}
  return ret_val;
}

// =====

int RtSdBSP_Write_BlockMulti(SD_CARD_INFO * mmc_sd_card, unsigned long blockNum)
{
unsigned long DataCtrl = 0;
int  polled_mode = 0;
int ret_val = -1;
unsigned long level=0;

//    mmc_sd_card->blk_buffer=(unsigned long *) pBuffer;

    RtSdBSP_ClearStatus( mmc_sd_card,MCI_CLEAR_ALL);
    MCIDataCtrl = 0;
    RtSdBSP_Delay_micros(100);

    /*Check Status Before Transfer */
    if (RtSdcard_CheckStatus(mmc_sd_card) != 0 )
    {
rtp_printf("RtSdBSP_Write_Block: CheckStatus failed\n");
        RtSdcard_Send_Stop(mmc_sd_card);
        return -1;
    }
#if(ENABLE_DMA==0)
    polled_mode=1;
#endif

#if(CONFIGURE_SDMMC_INTERRUPTS)
    SD_CARD_UNIT_NUM = unitnumber;
    rtp_sig_semaphore_clear( *mmc_sd_card->sdmmc_rw_sm );
#endif

    if (polled_mode)
        level=PVOPVO_T_splhigh();
    else
        MCI_TXEnable();

    MCIDataTimer = MA_DATA_TIMER_VALUE; //MA: Increased the time out, huge now, optimize later

      //TESTTEST
    MCI_Block_End_Flag = 1;             /* MA, What is this by the way?? DMA stuff, I wonder */
#if(ENABLE_DMA)
    /* Write, block transfer, DMA, and data length */
    DataCtrl |= ((1 << 0) | (1 << 3) | (DATA_BLOCK_LEN << 4));
#else
    /* Write, block transfer, and data length */
    DataCtrl |= ((1 << 0) | (DATA_BLOCK_LEN << 4));
#endif

	if (mmc_sd_card->card_operating_flags & SDFLGMULTIBLOCK)
	{
    	MCIDataLength = BLOCK_LENGTH*mmc_sd_card->blocks_to_transfer;
    	if (mmc_sd_card->card_operating_flags & SDFLGBLOCKCOUNT)
		{
    		if (RtSdcard_Send_BlockCount( mmc_sd_card, mmc_sd_card->blocks_to_transfer) != 0)
        		goto write_error_return;
		}
    	if (RtSdcard_Send_Write_Multiple_Block(mmc_sd_card, blockNum) != 0)
        	goto write_error_return;
	}
	else
	{
    	MCIDataLength = BLOCK_LENGTH;
    	if ( RtSdcard_Send_Write_Block( mmc_sd_card, blockNum) != 0 )
    	{
        	goto write_error_return;
    	}
	}
    MCIDataCtrl = DataCtrl;
    RtSdBSP_Delay_micros(1); //MA

    ret_val = 0;

    if (polled_mode)
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
    if (mmc_sd_card->card_operating_flags & SDFLGMULTIBLOCK)
    {
      	/* Stop the transfer */
    	if ((mmc_sd_card->card_operating_flags & SDFLGBLOCKCOUNT)==0)
      		RtSdcard_Send_Stop(mmc_sd_card);
	}

    /* Check Status after Transfer to be sure it completed */
    if (RtSdcard_CheckStatus(mmc_sd_card) != 0 )
    {
    	rtp_printf("RtSdBSP_Write_Block: CheckStatus failed after transfer\n");
        RtSdcard_Send_Stop(mmc_sd_card);
        return -1;
    }
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

//===========================

int RtSdBSP_Write_Block(SD_CARD_INFO * mmc_sd_card, unsigned long blockNum)
{
unsigned long DataCtrl = 0;
int  polled_mode = 0;
int ret_val = -1;
unsigned long level=0;

	return RtSdBSP_Write_BlockMulti(mmc_sd_card, blockNum);

//    mmc_sd_card->blk_buffer=(unsigned long *) pBuffer;

    RtSdBSP_ClearStatus( mmc_sd_card,MCI_CLEAR_ALL);
    MCIDataCtrl = 0;
    RtSdBSP_Delay_micros(100);

    /*Check Status Before Transfer */
    if (RtSdcard_CheckStatus(mmc_sd_card) != 0 )
    {
rtp_printf("RtSdBSP_Write_Block: CheckStatus failed\n");
        RtSdcard_Send_Stop(mmc_sd_card);
        return -1;
    }
#if(ENABLE_DMA==0)
    polled_mode=1;
#endif

#if(CONFIGURE_SDMMC_INTERRUPTS)
    SD_CARD_UNIT_NUM = unitnumber;
    rtp_sig_semaphore_clear( *mmc_sd_card->sdmmc_rw_sm );
#endif

    if (polled_mode)
        level=PVOPVO_T_splhigh();
    else
        MCI_TXEnable();

    MCIDataTimer = MA_DATA_TIMER_VALUE; //MA: Increased the time out, huge now, optimize later
    MCIDataLength = BLOCK_LENGTH;

      //TESTTEST
    MCI_Block_End_Flag = 1;             /* MA, What is this by the way?? DMA stuff, I wonder */
#if(ENABLE_DMA)
    /* Write, block transfer, DMA, and data length */
    DataCtrl |= ((1 << 0) | (1 << 3) | (DATA_BLOCK_LEN << 4));
#else
    /* Write, block transfer, and data length */
    DataCtrl |= ((1 << 0) | (DATA_BLOCK_LEN << 4));
#endif
    if ( RtSdcard_Send_Write_Block( mmc_sd_card, blockNum) != 0 )
    {
rtp_printf("RtSdBSP_Write_Block: RtSdcard_Send_Write_Block failed\n");
        goto write_error_return;
    }
    MCIDataCtrl = DataCtrl;
    RtSdBSP_Delay_micros(1); //MA

    ret_val = 0;

    if (polled_mode)
    {
        ret_val = RtSdBSP_PolledWriteXfer(mmc_sd_card->blk_buffer,512);
        PVOPVO_T_splx(level);
if (ret_val<0)
	rtp_printf("RtSdBSP_Write_Block: RtSdBSP_PolledWriteXfer failed\n");
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

    /* Check Status after Transfer to be sure it completed */
    if (RtSdcard_CheckStatus(mmc_sd_card) != 0 )
    {
rtp_printf("RtSdBSP_Write_Block: CheckStatus failed after transfer\n");
        RtSdcard_Send_Stop(mmc_sd_card);
        return -1;
    }
#if (ENABLE_DMA)
    //data_coun=MCIDataCnt;
    RtSdBSP_Delay_micros(10000);  //Wait for the write to finish, have to find another mechanism
    disable_dma_chan(0);
#endif //ENABLE_DMA
  mmc_sd_card->blocks_transfered=1;
//  mmc_sd_card->blocks_to_transfer=blockCount;
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

static void MCI_TXEnable( void )  //check this function, don't need interrupt??
{
#if ENABLE_DMA
    MCIMask0 |= ((DATA_END_INT_MASK)|(ERR_TX_INT_MASK));    /* Enable TX interrupts only */
  //MCI_MASK1 |= ((DATA_END_INT_MASK)|(ERR_TX_INT_MASK));    /* Enable TX interrupts only */
#else
    MCIMask0 |= ((FIFO_TX_INT_MASK)|(DATA_END_INT_MASK)|(ERR_TX_INT_MASK));    /* FIFO TX interrupts only */
    //MCIMask0 |= ((FIFO_TX_INT_MASK)|(ERR_TX_INT_MASK));    /* FIFO TX interrupts only */ //removed the DATA END INT MASK??
#endif
  return;
}

static void MCI_TXDisable( void )
{
#if (ENABLE_DMA)
    MCIMask0 &= ~((DATA_END_INT_MASK)|(ERR_TX_INT_MASK));    /* Enable TX interrupts only */
    //MCIMask1 &= ~((DATA_END_INT_MASK)|(ERR_TX_INT_MASK));    /* Enable TX interrupts only */
#else
    MCIMask0 &= ~((FIFO_TX_INT_MASK)|(DATA_END_INT_MASK)|(ERR_TX_INT_MASK));    /* FIFO TX interrupts only */
    //MCIMask1 &= ~((FIFO_TX_INT_MASK)|(DATA_END_INT_MASK)|(ERR_TX_INT_MASK));    /* FIFO TX interrupts only */
#endif
  return;
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
    RtSdBSP_ClearStatus( mmc_sd_card,MCI_CLEAR_ALL);
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
     DataCtrl = ((1 << 0) | (1 << 1) | (DATA_SCR_LENGTH << 4));
     MCIDataCtrl = DataCtrl;

    /* No break points after this or we'll get a fifo overrun */
    if (RtSdcard_Send_ACMD_SCR(mmc_sd_card) != 0)
    	goto error_return;
    ShowFifoStats("Pos-send ACMS_SCR");
     /* Read, enable,  block transfer, and data length==DATA_SCR_LENGTH(log2(8)0<<4) */
//     DataCtrl = ((1 << 0) | (1 << 1) | (DATA_SCR_LENGTH << 4));
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


//==========


//=====

int RtSdBSP_Read_BlockSetup(SD_CARD_INFO * mmc_sd_card,unsigned long blockNum)
{
    RtSdBSP_ClearStatus( mmc_sd_card,MCI_CLEAR_ALL);
    RtSdBSP_Delay_micros(100);

#if(CONFIGURE_SDMMC_INTERRUPTS)
    SD_CARD_UNIT_NUM = unitnumber;
    rtp_sig_semaphore_clear( *mmc_sd_card->sdmmc_rw_sm );
#endif

#if (ENABLE_DMA==0)
    mmc_sd_card->cpu_level=PVOPVO_T_splhigh();
#else
    RtSdBSP_Dma_Setup(mmc_sd_card,(unsigned char *)mmc_sd_card->blk_buffer,1/* Reading*/);
#endif
    MCIDataTimer = MA_DATA_TIMER_VALUE; //MA: Increased the time out, huge now, optimize later
	if (mmc_sd_card->card_operating_flags & SDFLGMULTIBLOCK)
    	MCIDataLength = BLOCK_LENGTH*mmc_sd_card->blocks_to_transfer;
	else
    	MCIDataLength = BLOCK_LENGTH;
	return 0;
}

void RtSdBSP_Read_BlockShutdown(SD_CARD_INFO * mmc_sd_card)
{
#if (ENABLE_DMA==0)
    PVOPVO_T_splx(mmc_sd_card->cpu_level);
#endif
}

int RtSdBSP_Read_BlockTransfer(SD_CARD_INFO * mmc_sd_card)
{
unsigned long DataCtrl;
int ret_val=0;
#if (ENABLE_DMA)
      DataCtrl = ((1 << 0) | (1 << 1) | (1 << 3) | (DATA_BLOCK_LEN << 4));
#else
      /* Read, enable, block transfer, and data length */
      DataCtrl = ((1 << 0) | (1 << 1) | (DATA_BLOCK_LEN << 4));
#endif

  /* No break points after this or we'll get a fifo overrun */
      MCIDataCtrl = DataCtrl;
//      RtSdBSP_Delay_micros(1); //MA
#if (ENABLE_DMA==0)
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
#else
      ret_val=RtSdBSP_Dma_Complete(mmc_sd_card,1/* Reading*/);
      if (ret_val==0)
        mmc_sd_card->blocks_transfered = mmc_sd_card->blocks_to_transfer;
#endif
//  mmc_sd_card->blocks_to_transfer=blockCount;
  return ret_val;
}

//==========

//#define  OLD_READ_METHOD 1
#ifdef OLD_READ_METHOD
int RtSdcard_Send_Read_Multiple_Block( SD_CARD_INFO * mmc_sd_card , unsigned long blockNum ); //MA
int RtSdBSP_Read_Block(SD_CARD_INFO * mmc_sd_card,unsigned long blockNum)
{
    unsigned long DataCtrl = 0;
    int  polled_mode = 0;
    unsigned long level=0;
    int ret_val = -1;
#if (ENABLE_DMA==0)
    polled_mode=1;
#endif //POLING_MODE

    RtSdBSP_ClearStatus( mmc_sd_card,MCI_CLEAR_ALL);
    RtSdBSP_Delay_micros(100);

//    mmc_sd_card->blk_buffer=(unsigned long *) pBbuffer;

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

    MCI_Block_End_Flag = 1;             /* MA, What is this by the way?? DMA stuff, I wonder */

	if (mmc_sd_card->card_operating_flags & SDFLGMULTIBLOCK)
	{
    	MCIDataLength = BLOCK_LENGTH*mmc_sd_card->blocks_to_transfer;
    	if (mmc_sd_card->card_operating_flags & SDFLGBLOCKCOUNT)
		{
    		if (RtSdcard_Send_BlockCount( mmc_sd_card, mmc_sd_card->blocks_to_transfer) != 0)
        		goto error_return;
		}
    	if (RtSdcard_Send_Read_Multiple_Block(mmc_sd_card, blockNum) != 0)
        	goto error_return;
	}
	else
	{
    	MCIDataLength = BLOCK_LENGTH;
    	if (RtSdcard_Send_Read_Block( mmc_sd_card, blockNum, BLOCK_LENGTH ) != 0)
        	goto error_return;
	}

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
    dma_config.lock = 0;                        //MA??
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
#endif
      DataCtrl |= ((1 << 0) | (1 << 1) | (1 << 3) | (DATA_BLOCK_LEN << 4));
#else
      /* Read, enable, block transfer, and data length */
      DataCtrl = ((1 << 0) | (1 << 1) | (DATA_BLOCK_LEN << 4));
#endif

  /* No break points after this or we'll get a fifo overrun */
      MCIDataCtrl = DataCtrl;
//      RtSdBSP_Delay_micros(1); //MA
      ret_val = 0;
      if (polled_mode)
      {
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
        PVOPVO_T_splx(level);
      }
      else
      {
#if(CONFIGURE_SDMMC_INTERRUPTS)
          rtp_sig_semaphore_wait_timed (*mmc_sd_card->sdmmc_rw_sm,-1);
          rtp_sig_semaphore_clear( *mmc_sd_card->sdmmc_rw_sm );
#endif //CONFIGURE_SDMMC_INTERRUPTS
      }
      if (mmc_sd_card->card_operating_flags & SDFLGMULTIBLOCK)
	  {
      	/* Stop the transfer */
    	if ((mmc_sd_card->card_operating_flags & SDFLGBLOCKCOUNT)==0)
      		RtSdcard_Send_Stop(mmc_sd_card);
	  }

      if (ret_val == -1)
          return -1;

#if (ENABLE_DMA)
      //wait_chan_active(0);
      disable_dma_chan(0);
#endif
//  mmc_sd_card->blocks_to_transfer=blockCount;
  return 0;

error_return:
    if (polled_mode)
    {
          PVOPVO_T_splx(level);
    }
      return -1;
}
#endif

static void MCI_RXEnable( void )
{
#if ENABLE_DMA
    //not yet done
    MCIMask0  |= ((DATA_END_INT_MASK)|(ERR_RX_INT_MASK));    /* Enable RX interrupts only */
    //MCI_MASK1 |= ((DATA_END_INT_MASK)|(ERR_RX_INT_MASK));    /* Enable RX interrupts only */
#else
    MCIMask0 |= ((FIFO_RX_INT_MASK)|(DATA_END_INT_MASK)|(ERR_RX_INT_MASK));    /* FIFO RX interrupts only */
    //MCIMask0 |= ((FIFO_RX_INT_MASK)|(ERR_RX_INT_MASK));    /* FIFO RX interrupts only */ //removed DATA_END_INT_MASK??
#endif
    return;
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
    rtp_printf("ERORR, NO SUCH CARD NUMBER?? \n");  //handel the error?

  pmmc_sd_card = &mmc_sd_cards[SD_CARD_UNIT_NUM];


  if ( status & DATA_ERR_INT_MASK )
  {
    /* Not Implemented Yet*/
    MCI_DataErrorProcess();
    //MCI_DataErrorProcess_count++;
    VICAddress=0;             /* Ack the interrupt */
    rtp_printf("DATA ERROR Interrupt \n");
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
      rtp_printf("DATA END Interrupt \n");
      rtp_sig_semaphore_signal(*pmmc_sd_card->sdmmc_rw_sm);
      PVOPVO_T_splx(intr_mask);
      return;
  }
  else if(status & FIFO_INT_MASK)
  {

      MCI_FIFOInterruptService(pmmc_sd_card);
      MCI_FIFOInterruptService_count++;
      VICAddress=0;             /* Ack the interrupt , we do it this way for USB??*/
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
      rtp_printf("Command Process Interrupt \n");
      rtp_printf("Command Process Interrupt \n");
      PVOPVO_T_splx(intr_mask);
      return;
  }

}


int configure_sdmmcIsr(void)
{
/* Install SDMMC interrupt handler */
 rtp_printf("Use sys  hook sdmmc \n");
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
        return;
    }
    if(status & MCI_DATA_BLK_END )
    {
        MCIClear = MCI_DATA_BLK_END;
        return;
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
        DataTxFIFOCount++;             /* if using TX_HALF_EMPTY remove one WriteFifo below */
        int count;

            if (ShadowStatus & MMCSD_TX_FIFO_EMPTY)
            {
                *fifo_ptr        =    *plBbuffer          ;
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

                //plBbuffer+=8;
                plBbuffer+=16 ;
                remain -= count;
                intrpt_byte_write_count += 64;
            }//else if data fifo empty
            else if(ShadowStatus & TXFIFOHALFEMPTY)
            {
                *fifo_ptr        =    *plBbuffer     ;
                *(fifo_ptr+1 )    =    *(plBbuffer+1 )     ;
                *(fifo_ptr+2 )    =    *(plBbuffer+2 )      ;
                *(fifo_ptr+3 )    =    *(plBbuffer+3 )     ;
                *(fifo_ptr+4 )    =    *(plBbuffer+4 )     ;
                *(fifo_ptr+5 )    =    *(plBbuffer+5 )     ;
                *(fifo_ptr+6 )    =    *(plBbuffer+6 )     ;
                *(fifo_ptr+7 )    =    *(plBbuffer+7 )     ;

                remain -= count;
                intrpt_byte_write_count += 32;
                plBbuffer+=8;
            }//if data fifo half empty

            RtSdBSP_Delay_micros(5);
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


#define TCI 0
#define MEMWIDTH  0
#define FIFOWIDTH  2

#define DBSIZEREAD 	2 // Burst size 8 
#define SBSIZEREAD 	2 //??
#define DBSIZEWRITE	2 //??
#define SBSIZEWRITE	2 //??

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
	DMACC0Configuration	= 0; // DMACC0Configuration&0xfffffffe;

        
	if (Reading)
	{
		DMACC0SrcAddr		= 0xE008C080; // MCI fifo
		DMACC0DestAddr		=  (unsigned long) pBbuffer;
                DMACC0LLI = 0; // No link just a single transaction.           
                l = (TCI<<31)|(DIREAD<<27)|(SIREAD<<26)|(MEMWIDTH<<21)|(FIFOWIDTH<<18)|(DBSIZEREAD<<15)|(SBSIZEREAD<<12)|(TSIZE);
		DMACC0Control	 	= l;     
                l = 1|(MMCPERIPHERAL<<1)|(DMAPERIPHERALTOMEMORY<<11);
		DMACC0Configuration	= l;
        }
	else
	{
		DMACC0SrcAddr		= (unsigned long) pBbuffer;
		DMACC0DestAddr		= 0xE008C080; // MCI fifo
                DMACC0LLI = 0; // No link just a single transaction. 
                 l =  (TCI<<31)|(DIWRITE<<27)|(SIWRITE<<26)|(FIFOWIDTH<<21)|(MEMWIDTH<<18)|(DBSIZEWRITE<<15)|(SBSIZEWRITE<<12)|(TSIZE);
		DMACC0Control		= l;              
                l = 1|(MMCPERIPHERAL<<6)|(DMAMEMORYTOPERIPHERAL<<11);
		DMACC0Configuration	= l;
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
