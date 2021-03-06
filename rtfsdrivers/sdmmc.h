/*
 * SDMMC.h
 *
 *  Created on: Mar 29, 2011
 *      Author: Peter
 */

#ifndef SDMMC_H_
#define SDMMC_H_


#define OLIMEX_BOARD 1
#define CONFIGURE_SDMMC_INTERRUPTS 0

#define ENABLE_DMA 0
#define MCI_DMA_ENABLED		ENABLE_DMA
#define POLING_MODE 1

#if(CONFIGURE_SDMMC_INTERRUPTS)
#include "rtp.h"
#include "rtpsignl.h"
#endif


enum e_SD_CARD_STATE
{
	//UNKNOWN_STATE=-1,
	INACTIVE_STATE=0,
	IDLE_STATE,
	READY_STATE,
	IDENTIFICATION_STATE,
	STANDBY_STATE,
	TRANSFER_STATE,
	SENDING_DATA_STATE,
	RECEIVE_DATA_STATE,
	PROGRAMMING_STATE,
	DISCONNECT_STATE
};
//SD_CARD_STATES;

enum e_SD_CARD_OPERATION_MODE
{
	UNKNOWN_MODE=-1,
	INACTIVE_MODE=0,
	CARD_IDENTIFICATION_MODE,
	DATA_TRANSFER_MODE,
};



//need to provide function to parse this from the register value we acquire from the card.
struct s_CSD_REG_V1{
	unsigned char CSD_STRUCTURE;		//2 bits
	unsigned char TAAC;					//8 bits
	unsigned char NSAC;					//8 bits
	unsigned char TRAN_SPEED; 			//8 bits
	unsigned short int CCC;  			//12 bits
	unsigned char READ_BL_LEN;			//4 bits
	unsigned char READ_BL_PARTIAL; 		//1 bit
	unsigned char WRITE_BLK_MISALIGN;  	//1 bit
	unsigned char READ_BLK_MISALIGN;  	//1 bit
	unsigned char DSR_IMP;  			//1 bit
	unsigned short int C_SIZE; 			//12 bits
	unsigned char VDD_R_CURR_MIN;  		//3 bits
	unsigned char VDD_R_CURR_MAX;  		//3 bits
	unsigned char VDD_W_CURR_MIN;  		//3 bits
	unsigned char VDD_W_CURR_MAX;  		//3 bits
	unsigned char C_SIZE_MULT;  		//3 bits
	unsigned char ERASE_BLK_EN;  		//1 bit
	unsigned char SECTOR_SIZE;  		//7 bits
	unsigned char WP_GRP_SIZE;  		//7 bits
	unsigned char WP_GRP_ENABLE;  		//1 bit
	unsigned char R2W_FACTOR;	  		//3 bits
	unsigned char WRITE_BL_LEN;	  		//4 bits
	unsigned char WRITE_BL_PARTIAL;	  	//1 bit
	unsigned char FILE_FORMAT_GRP; 		//1 bit
	unsigned char COPY;			  		//1 bit
	unsigned char PERM_WRITE_PROTECT;	//1 bit
	unsigned char TMP_WRITE_PROTECT;	//1 bit
	unsigned char FILE_FORMAT;	  		//2 bits
	unsigned char CRC;	  				//7 bits
	unsigned char NOT_USED_ALWAYS_1;	//1 bit
} ;



struct s_CSD_REG_V2{
	unsigned char CSD_STRUCTURE;		//2 bits
	unsigned char TAAC;					//8 bits
	unsigned char NSAC;					//8 bits
	unsigned char TRAN_SPEED; 			//8 bits
	unsigned short int CCC;  			//12 bits
	unsigned char READ_BL_LEN;			//4 bits
	unsigned char READ_BL_PARTIAL; 		//1 bit
	unsigned char WRITE_BLK_MISALIGN;  	//1 bit
	unsigned char READ_BLK_MISALIGN;  	//1 bit
	unsigned char DSR_IMP;  			//1 bit
	unsigned int C_SIZE; 				//22 bits
	unsigned char ERASE_BLK_EN;  		//1 bit
	unsigned char SECTOR_SIZE;  		//7 bits
	unsigned char WP_GRP_SIZE;  		//7 bits
	unsigned char WP_GRP_ENABLE;  		//1 bit
	unsigned char R2W_FACTOR;	  		//3 bits
	unsigned char WRITE_BL_LEN;	  		//4 bits
	unsigned char WRITE_BL_PARTIAL;	  	//1 bit
	unsigned char FILE_FORMAT_GRP; 		//1 bit
	unsigned char COPY;			  		//1 bit
	unsigned char PERM_WRITE_PROTECT;	//1 bit
	unsigned char TMP_WRITE_PROTECT;	//1 bit
	unsigned char FILE_FORMAT;	  		//2 bits
	unsigned char CRC;	  				//7 bits
	unsigned char NOT_USED_ALWAYS_1;	//1 bit
} ;

struct s_CID_REG{
	unsigned char MID;			//8 bits	Manufacturer ID
	unsigned short int OID;		//16 bits	OEM/Application ID
	unsigned long long PNM;		//40 bits	Product Name
	unsigned char PRV; 			//8 bits	Produce Revision
	unsigned long int PSN;		//32 bits	Product Serial Number
	unsigned short int MDT; 	//12 bits	Manufacturing Date
	unsigned short int MDT_year; 	//12 bits	Manufacturing Date
	unsigned short int MDT_month; 	//12 bits	Manufacturing Date
	unsigned char WRITE_BLK_MISALIGN;  	//1 bit
	unsigned char CRC;	  				//7 bits
} ;

typedef struct s_SCR_REG
{
	int temp;
	/* TODO:to be implemented*/
};

typedef	struct s_CSD_REG_V1 CSD_REG_V1;
typedef	struct s_CSD_REG_V2	CSD_REG_V2;
typedef struct s_SCR_REG SCR_REG;
typedef struct s_CID_REG CID_REG;
typedef enum e_SD_CARD_STATE SD_CARD_STATES;

typedef enum e_SD_CARD_OPERATION_MODE SD_CARD_OPERATION_MODE;



typedef struct s_SD_CARD
{
	long int		RCA;
	short int 		CSD_Version;
	CSD_REG_V1 		CSD_V1; 		//structure from the card, careful of padding, need to disable padding
	CSD_REG_V2 		CSD_V2;			//structure from the card, careful of padding, need to disable padding
	CID_REG 		CID;		//128 bit, careful with endianess
	SCR_REG 		SCR;			//structure from the card, careful of padding, need to disable padding
	unsigned int 	OCR; 			//32-bit
	short int 		DSR;			//16 bit
	long int SSR_RESP[4];
	//unsigned char 	SSR[16]; 		//512 bit //TODO: make sure it is not a structure
	//implement SSR RESP FIELDS
	unsigned int 	CSR;			//32 bit, Card Status Register
	SD_CARD_STATES state ;			// to be initialized to INACTIVE_STATE
	SD_CARD_OPERATION_MODE mode; 	//TODO: set the card mode in the command codes
	int card_inserted ;
	unsigned long card_type ;
	unsigned long card_capacity_bytes ;
	unsigned long bytes_per_block;
	unsigned long no_blocks;


	unsigned long *blk_buffer;
#if(CONFIGURE_SDMMC_INTERRUPTS)
	RTP_SEMAPHORE *sdmmc_rw_sm;
#endif

} SD_CARD_INFO;


//LPC2478 Support one SD card and up to 4 MMC cards ?? need to check mmc standard to see how to address these card
#define MMC_DEBUG			0
#define SUPPORT_SDHC_SDXC	1
#define SUPPORT_1_8_VOLT	0


#define SLOW_RATE			1
#define NORMAL_RATE			2

#define CARD_UNKNOWN_TYPE	0
#define MMC_CARD_TYPE		1
#define SD_CARD_TYPE		2
#define SDHC_CARD_TYPE		3
#define SDXC_CARD_TYPE		4 //?? how can I identify

#define BUS_WIDTH_1BIT		0
#define BUS_WIDTH_4BITS		10


#define EXPECT_NO_RESP		0
#define EXPECT_SHORT_RESP	1
#define EXPECT_LONG_RESP	2

#define SEND_OP_COND		1		/* SEND_OP_COND(MMC) or ACMD41(SD) */

/* MCIStatus Register bit fields interpretation */
#define MCI_CMD_CRC_FAIL	(1 << 0)
#define MCI_DATA_CRC_FAIL	(1 << 1)
#define MCI_CMD_TIMEOUT		(1 << 2) /* CMD TIMOUT ERROR, MCIStatus Register */
#define MCI_DATA_TIMEOUT	(1 << 3)
#define MCI_TX_UNDERRUN		(1 << 4)
#define MCI_RX_OVERRUN		(1 << 5)


#define MCI_DATA_END		(1 << 8)
#define MCI_START_BIT_ERR	(1 << 9)
#define MCI_DATA_BLK_END	(1 << 10)

#define SEND_APP_OP_COND	41		/* ACMD41 for SD card */
#define APP_CMD				55		/* APP_CMD, the following will a ACMD */
#define CARD_STATUS_ACMD_ENABLE		1 << 5
#define STOP_TRANSMISSION	12		/* Stop either READ or WRITE operation */
#define MCI_CMD_RESP_END	(1 << 6)
#define ALL_SEND_CID		2		/* ALL SEND_CID */
#define SEND_CSD			9		/* SEND_CSD */

/* SD COMMANDS */
//The commands below are from the nxp examples, from Keil
#define GO_IDLE_STATE			0		/* GO_IDLE_STATE(MMC) or RESET(SD) */
#define SEND_OP_COND			1		/* SEND_OP_COND(MMC) or ACMD41(SD) */
#define ALL_SEND_CID			2		/* ALL SEND_CID */
#define SET_RELATIVE_ADDR		3		/* SET_RELATE_ADDR */
#define SET_ACMD_BUS_WIDTH		6
#define SELECT_CARD				7		/* SELECT/DESELECT_CARD */
#define SEND_CSD				9		/* SEND_CSD */
#define STOP_TRANSMISSION		12		/* Stop either READ or WRITE operation */
#define SEND_STATUS				13		/* SEND_STATUS */
#define SET_BLOCK_LEN			16		/* SET_BLOCK_LEN */
#define READ_SINGLE_BLOCK		17		/* READ_SINGLE_BLOCK */
#define READ_MULTIPLE_BLOCK		18		/* READ_MULTIPLE_BLOCK */
#define WRITE_BLOCK				24		/* WRITE_BLOCK */
#define WRITE_MULTIPLE_BLOCK	25	/* WRITE_MULTIPLE BLOCK */
#define SEND_APP_OP_COND		41		/* ACMD41 for SD card */
#define SEND_APP_SEND_SCR		51		/* ACMD51, Send the SCR Register */
#define APP_CMD					55		/* APP_CMD, the following will a ACMD */

//New Commands, not available in previous SD spec (SPEC v1 ??), from SPEC Ver 2 above
#define SEND_IF_COND		8		/* SELECT/DESELECT_CARD */
#if(SUPPORT_1_8_VOLT)
#define VOLTAGE_SWITCH		11
#endif //SUPPORT_1_8_VOLT
//End of New commands


/* MCI Status Register Flags */
//List status flags
#define MCI_CMD_ACTIVE		(1 << 11)
#define MCI_CMD_SENT		(1 << 7)


//Default Arguments
#define OCR_INDEX				0x00FF8000 //argument for command ACMD41, SD SEND OP COND, sends host capacity support information (HCS), and asks the accessed card to send its operating condition register (OCR) content in the response on the CMD line, HCS is effective when card receives SEND_IF_COND command
#define OCR_INDEX_SDHC_SDHX		0x40FF8000
//#define OCR_INDEX_SDHC_SDHX		0x00000000 //????
						  //111111111000000000000000
#define SEND_IF_COND_ARG	0x000001AA  //argument for command ACMD8, SD IF COND

#define INVALID_RESPONSE	0xFFFFFFFF





#define MCLKDIV_SLOW		0x17-1 /* 23 = 391,304Hz -> @18Mhz/(2*60) */
//#define MCLKDIV_NORMAL		0x4-1  /* 3 = 6Mhz -> @18Mhz/(2*4) */			//TODO:might need To figure out the max freq depending on the SD Card Class, conditional to the CSD Register description??
#define MCLKDIV_NORMAL		0x10-1  /* 3 = 6Mhz -> @18Mhz/(2*4) */			//TODO:Experimental, to remove after the optimization of the read loop
									//The clock frequency can be changed to the maximum card bus frequency when relative card addresses are assigned to all cards. for SD ==25MHz ?? MMC= 20MHz ??

#define SD_1_BIT 			0
#define SD_4_BIT			1


//MCISTATUS flags
#define MMCSD_CMD_CRC_FAIL		    (1 << 0)
#define MMCSD_DATA_CRC_FAIL		    (1 << 1)
#define MMCSD_CMD_TIMEOUT		    (1 << 2)
#define MMCSD_DATA_TIMEOUT		    (1 << 3)
#define MMCSD_TX_UNDERRUN		    (1 << 4)
#define MMCSD_RX_OVERRUN		    (1 << 5)
#define MMCSD_CMD_RESPEND		    (1 << 6)
#define MMCSD_CMD_SENT		        (1 << 7)
#define MMCSD_DATA_END		        (1 << 8)
#define MMCSD_START_BIT_ERR		    (1 << 9)
#define MMCSD_DATA_BLOCK_END    	(1 << 10)
#define MMCSD_CMD_ACTIVE		    (1 << 11)
#define MMCSD_TX_ACTIVE		    	(1 << 12)
#define MMCSD_RX_ACTIVE		    	(1 << 13)
#define MMCSD_TX_FIFO_HALFEMPTY	    (1 << 14)
#define MMCSD_RX_FIFO_HALFFULL	    (1 << 15)
#define MMCSD_TX_FIFOFULL		    (1 << 16)
#define MMCSD_RX_FIFO_FULL		    (1 << 17)
#define MMCSD_TX_FIFO_EMPTY		    (1 << 18)
#define MMCSD_RX_FIFO_EMPTY		    (1 << 19)
#define MMCSD_TX_DATA_AVLBL		    (1 << 20)
#define MMCSD_RX_DATA_AVLBL		    (1 << 21)


#define CMD_INT_MASK      (MCI_CMD_CRC_FAIL | MCI_CMD_TIMEOUT | MCI_CMD_RESP_END \
			             | MCI_CMD_SENT     | MCI_CMD_ACTIVE)

#define DATA_ERR_INT_MASK	(MMCSD_DATA_CRC_FAIL | MMCSD_DATA_TIMEOUT | MMCSD_TX_UNDERRUN \
			               | MMCSD_RX_OVERRUN | MMCSD_START_BIT_ERR	)

#define ACTIVE_INT_MASK ( MCI_TX_ACTIVE | MCI_RX_ACTIVE)

#define FIFO_INT_MASK		(MMCSD_TX_FIFO_HALFEMPTY | MMCSD_RX_FIFO_HALFFULL \
                           | MMCSD_TX_FIFOFULL  | MMCSD_RX_FIFO_FULL \
			               | MMCSD_TX_FIFO_EMPTY | MMCSD_RX_FIFO_EMPTY \
						   | MMCSD_DATA_BLOCK_END )

//#define	FIFO_TX_INT_MASK (MMCSD_TX_FIFO_HALFEMPTY )
//#define	FIFO_TX_INT_MASK (MMCSD_TX_FIFO_EMPTY)
#define	FIFO_TX_INT_MASK (MMCSD_TX_FIFO_HALFEMPTY | MMCSD_TX_FIFO_EMPTY )
#define	FIFO_RX_INT_MASK (MMCSD_RX_FIFO_HALFFULL  )

#define DATA_END_INT_MASK    (MMCSD_DATA_END | MMCSD_DATA_BLOCK_END)

#define ERR_TX_INT_MASK (MMCSD_DATA_CRC_FAIL | MMCSD_DATA_TIMEOUT | MMCSD_TX_UNDERRUN | MMCSD_START_BIT_ERR	)
#define ERR_RX_INT_MASK (MMCSD_DATA_CRC_FAIL | MMCSD_DATA_TIMEOUT | MMCSD_RX_OVERRUN  | MMCSD_START_BIT_ERR	)

/* For the SD card I tested, the minimum block length is 512 */
/* For MMC, the restriction is loose, due to the variety of SD and MMC
card support, ideally, the driver should read CSD register to find the
speed and block length for the card, and set them accordingly. */
/* In this driver example, it will support both MMC and SD cards, it
does read the information by send SEND_CSD to poll the card status,
but, it doesn't configure them accordingly. this is not intended to
support all the SD and MMC card. */

/* DATA_BLOCK_LEN table
	DATA_BLOCK_LEN			Actual Size( BLOCK_LENGTH )
	11						2048
	10						1024
	9						512
	8						256
	7						128
	6						64
	5						32
	4						16
	3						8
	2						4
	1						2
*/

/* To simplify the programming, please note that, BLOCK_LENGTH is a multiple of FIFO_SIZE */
#define DATA_BLOCK_LEN		9	/* Block size field in DATA_CTRL */
#define BLOCK_LENGTH		512
								/* for SD card, 128, the size of the flash */
								/* card is 512 * 128 = 64K */
#define BLOCK_NUM			0x80
#define FIFO_SIZE			16


#endif /* SDMMC_H_ */
