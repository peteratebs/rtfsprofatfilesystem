/*
 * SDMMC.h
 *
 *  Created on: Mar 29, 2011
 *      Author: Peter
 */

#ifndef SDMMC_H_
#define SDMMC_H_


#define CONFIGURE_SDMMC_INTERRUPTS 0
#define ENABLE_DMA 1

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
    unsigned char CSD_STRUCTURE;        //2 bits
    unsigned char TAAC;                    //8 bits
    unsigned char NSAC;                    //8 bits
    unsigned char TRAN_SPEED;             //8 bits
    unsigned short int CCC;              //12 bits
    unsigned char READ_BL_LEN;            //4 bits
    unsigned char READ_BL_PARTIAL;         //1 bit
    unsigned char WRITE_BLK_MISALIGN;      //1 bit
    unsigned char READ_BLK_MISALIGN;      //1 bit
    unsigned char DSR_IMP;              //1 bit
    unsigned short int C_SIZE;             //12 bits
    unsigned char VDD_R_CURR_MIN;          //3 bits
    unsigned char VDD_R_CURR_MAX;          //3 bits
    unsigned char VDD_W_CURR_MIN;          //3 bits
    unsigned char VDD_W_CURR_MAX;          //3 bits
    unsigned char C_SIZE_MULT;          //3 bits
    unsigned char ERASE_BLK_EN;          //1 bit
    unsigned char SECTOR_SIZE;          //7 bits
    unsigned char WP_GRP_SIZE;          //7 bits
    unsigned char WP_GRP_ENABLE;          //1 bit
    unsigned char R2W_FACTOR;              //3 bits
    unsigned char WRITE_BL_LEN;              //4 bits
    unsigned char WRITE_BL_PARTIAL;          //1 bit
    unsigned char FILE_FORMAT_GRP;         //1 bit
    unsigned char COPY;                      //1 bit
    unsigned char PERM_WRITE_PROTECT;    //1 bit
    unsigned char TMP_WRITE_PROTECT;    //1 bit
    unsigned char FILE_FORMAT;              //2 bits
    unsigned char CRC;                      //7 bits
    unsigned char NOT_USED_ALWAYS_1;    //1 bit
} ;



struct s_CSD_REG_V2{
    unsigned char CSD_STRUCTURE;        //2 bits
    unsigned char TAAC;                    //8 bits
    unsigned char NSAC;                    //8 bits
    unsigned char TRAN_SPEED;             //8 bits
    unsigned short int CCC;              //12 bits
    unsigned char READ_BL_LEN;            //4 bits
    unsigned char READ_BL_PARTIAL;         //1 bit
    unsigned char WRITE_BLK_MISALIGN;      //1 bit
    unsigned char READ_BLK_MISALIGN;      //1 bit
    unsigned char DSR_IMP;              //1 bit
    unsigned int C_SIZE;                 //22 bits
    unsigned char ERASE_BLK_EN;          //1 bit
    unsigned char SECTOR_SIZE;          //7 bits
    unsigned char WP_GRP_SIZE;          //7 bits
    unsigned char WP_GRP_ENABLE;          //1 bit
    unsigned char R2W_FACTOR;              //3 bits
    unsigned char WRITE_BL_LEN;              //4 bits
    unsigned char WRITE_BL_PARTIAL;          //1 bit
    unsigned char FILE_FORMAT_GRP;         //1 bit
    unsigned char COPY;                      //1 bit
    unsigned char PERM_WRITE_PROTECT;    //1 bit
    unsigned char TMP_WRITE_PROTECT;    //1 bit
    unsigned char FILE_FORMAT;              //2 bits
    unsigned char CRC;                      //7 bits
    unsigned char NOT_USED_ALWAYS_1;    //1 bit
} ;

struct s_CID_REG{
    unsigned char MID;            //8 bits    Manufacturer ID
    unsigned short int OID;        //16 bits    OEM/Application ID
    union
    {
      char PNMASCI[5];
      unsigned long long PNM;        //40 bits    Product Name
    } PNM;
    unsigned char PRV;             //8 bits    Produce Revision
    unsigned long int PSN;        //32 bits    Product Serial Number
    unsigned short int MDT;     //12 bits    Manufacturing Date
    unsigned short int MDT_year;     //12 bits    Manufacturing Date
    unsigned short int MDT_month;     //12 bits    Manufacturing Date
    unsigned char WRITE_BLK_MISALIGN;      //1 bit
    unsigned char CRC;                      //7 bits
} ;

struct s_SCR_REG
{
    unsigned long scrbits;
    unsigned long reserved;
    /* TODO:to be implemented*/
};

typedef struct s_CSD_REG_V1 CSD_REG_V1;
typedef struct s_CSD_REG_V2    CSD_REG_V2;
typedef struct s_SCR_REG SCR_REG;
typedef struct s_CID_REG CID_REG;
typedef enum   e_SD_CARD_STATE SD_CARD_STATES;
typedef enum   e_SD_CARD_OPERATION_MODE SD_CARD_OPERATION_MODE;


typedef struct s_SD_CARD
{
    long int        RCA;
    short int       CSD_Version;
    CSD_REG_V1      CSD_V1;     // structure from the card, careful of padding, need to disable padding
    CSD_REG_V2      CSD_V2;     // structure from the card, careful of padding, need to disable padding
    CID_REG         CID;        // 128 bit, careful with endianess
    SCR_REG         SCR;        // structure from the card, careful of padding, need to disable padding
    unsigned int    OCR;        // 32-bit
    short int       DSR;        // 16 bit
    long int SSR_RESP[4];
    //unsigned char     SSR[16];         //512 bit //TODO: make sure it is not a structure
    //implement SSR RESP FIELDS
    unsigned int     CSR;        //32 bit, Card Status Register
    SD_CARD_STATES state ;       // to be initialized to INACTIVE_STATE
    SD_CARD_OPERATION_MODE mode; //TODO: set the card mode in the command codes
#define SDFLGINSERTED   1
#define SDFLGMULTIBLOCK 2
#define SDFLGBLOCKCOUNT 4
#define SDFLGHIGHSPEED  8
#define SDFLG4BITMODE  0x10
    unsigned long card_operating_flags;
    unsigned long card_type;
    unsigned long card_capacity_bytes;
    unsigned long bytes_per_block;
    unsigned long no_blocks;


    unsigned long *blk_buffer;
    unsigned long  blocks_transfered;
    unsigned long  blocks_to_transfer;
    /* For use by PIO interrupt transfer methods */
    unsigned long BspErrorStatus;
    unsigned long BspEndInterrupts;
    unsigned long BspFifoInterrupts;
    unsigned long  bytes_left_to_transfer;
    unsigned char *byte_buffer;
    unsigned long cpu_level;    /* used in polled mode to push/pop cpu level */
} SD_CARD_INFO;


//LPC2478 Support one SD card and up to 4 MMC cards ?? need to check mmc standard to see how to address these card
#define MMC_DEBUG            0
#define SUPPORT_1_8_VOLT     0


#define CARD_UNKNOWN_TYPE    0
#define MMC_CARD_TYPE        1
#define SD_CARD_TYPE         2
#define SDHC_CARD_TYPE       3
#define SDXC_CARD_TYPE       4 //?? how can I identify


#define EXPECT_NO_RESP        0
#define EXPECT_SHORT_RESP     1
#define EXPECT_LONG_RESP      2

#define SEND_OP_COND          1        /* SEND_OP_COND(MMC) or ACMD41(SD) */


#define SEND_APP_OP_COND    41        /* ACMD41 for SD card */
#define APP_CMD             55        /* APP_CMD, the following will a ACMD */
#define CARD_STATUS_ACMD_ENABLE        1 << 5
#define STOP_TRANSMISSION   12        /* Stop either READ or WRITE operation */
#define ALL_SEND_CID        2        /* ALL SEND_CID */
#define SEND_CSD            9        /* SEND_CSD */

/* SD COMMANDS */

#define GO_IDLE_STATE           0        /* GO_IDLE_STATE(MMC) or RESET(SD) */
#define SEND_OP_COND            1        /* SEND_OP_COND(MMC) or ACMD41(SD) */
#define ALL_SEND_CID            2        /* ALL SEND_CID */
#define SET_RELATIVE_ADDR       3        /* SET_RELATE_ADDR */
#define SET_ACMD_BUS_WIDTH      6
#define SELECT_CARD             7        /* SELECT/DESELECT_CARD */
#define SEND_CSD                9        /* SEND_CSD */
#define STOP_TRANSMISSION       12        /* Stop either READ or WRITE operation */
#define SEND_STATUS             13        /* SEND_STATUS */
#define SET_BLOCK_LEN           16        /* SET_BLOCK_LEN */
#define READ_SINGLE_BLOCK       17        /* READ_SINGLE_BLOCK */
#define READ_MULTIPLE_BLOCK     18        /* READ_MULTIPLE_BLOCK */
#define SET_BLOCK_COUNT         23        /* SET_BLOCK_COUNT */
#define WRITE_BLOCK             24        /* WRITE_BLOCK */
#define WRITE_MULTIPLE_BLOCK    25        /* WRITE_MULTIPLE BLOCK */
#define SEND_APP_OP_COND        41        /* ACMD41 for SD card */
#define SEND_APP_OP_DISCONNECT  42        /* ACMD42 for SD card */
#define SEND_APP_SEND_SCR       51       /* ACMD51, Send the SCR Register */
#define APP_CMD                 55        /* APP_CMD, the following will a ACMD */

//New Commands, not available in previous SD spec (SPEC v1 ??), from SPEC Ver 2 above
#define SEND_IF_COND          8        /* SELECT/DESELECT_CARD */
#if(SUPPORT_1_8_VOLT)
#define VOLTAGE_SWITCH        11
#endif //SUPPORT_1_8_VOLT
//End of New commands



//Default Arguments
#define OCR_INDEX              0x00FF8000 //argument for command ACMD41, SD SEND OP COND,
                                          // sends host capacity support information (HCS),
                                          // and asks the accessed card to send its operating
                                          // condition register (OCR) content in the response on
                                          // the CMD line, HCS is effective when card receives
                                          // SEND_IF_COND command
#define OCR_INDEX_SDHC_SDHX    0x40FF8000
//#define OCR_INDEX_SDHC_SDHX        0x00000000 //????
                          //111111111000000000000000
#define SEND_IF_COND_ARG        0x000001AA  //argument for command ACMD8, SD IF COND

#define INVALID_RESPONSE         0xFFFFFFFF


#define LOG_BLOCK_LENGTH        9    /* Block size field in DATA_CTRL 2^N */
#define BLOCK_LENGTH        512

#define LOG_SCR_LENGTH      3
#define SCR_LENGTH           8


#endif /* SDMMC_H_ */
