/*
 */

#ifndef MMCSD_MMC_H
#define MMCSD_MMC_H

/* MMC/SD commands */
#define	MMCSD_GO_IDLE_STATE     0
#define MMCSD_SEND_OP_COND      1
#define MMCSD_ALL_SEND_CID      2
#define MMCSD_SET_RELATIVE_ADDR 3
#define MMCSD_SWITCH            6
#define MMCSD_SELECT_CARD       7
#define MMCSD_SEND_EXT_CSD      8
#define MMCSD_SEND_CSD          9
#define MMCSD_SEND_STATUS	    13
#define MMCSD_READ_SINGLE_BLOCK 17
#define MMCSD_WRITE_BLOCK       24
#define MMCSD_APP_CMD           55

#define R1_OUT_OF_RANGE		    (1 << 31)
#define R1_ADDRESS_ERROR	    (1 << 30)
#define R1_BLOCK_LEN_ERROR	    (1 << 29)
#define R1_ERASE_SEQ_ERROR      (1 << 28)
#define R1_ERASE_PARAM		    (1 << 27)
#define R1_WP_VIOLATION		    (1 << 26)
#define R1_CARD_IS_LOCKED	    (1 << 25)
#define R1_LOCK_UNLOCK_FAILED	(1 << 24)
#define R1_COM_CRC_ERROR	    (1 << 23)
#define R1_ILLEGAL_COMMAND	    (1 << 22)
#define R1_CARD_ECC_FAILED	    (1 << 21)
#define R1_CC_ERROR		        (1 << 20)
#define R1_ERROR		        (1 << 19)
#define R1_UNDERRUN		        (1 << 18)
#define R1_OVERRUN		        (1 << 17)
#define R1_CID_CSD_OVERWRITE	(1 << 16)
#define R1_WP_ERASE_SKIP	    (1 << 15)
#define R1_CARD_ECC_DISABLED    (1 << 14)
#define R1_ERASE_RESET		    (1 << 13)
#define R1_STATUS(x)            (x & 0xFFFFE000)
#define R1_CURRENT_STATE(x)    	((x & 0x00001E00) >> 9)
#define R1_READY_FOR_DATA	    (1 << 8)
#define R1_APP_CMD		        (1 << 5)

#define MMC_CARD_BUSY	0x80000000	/* Card Power up status bit */

/*
 * CSD field definitions
 */
#define CSD_STRUCT_VER_1_0  0
#define CSD_STRUCT_VER_1_1  1
#define CSD_STRUCT_VER_1_2  2
#define CSD_STRUCT_EXT_CSD  3

#define CSD_SPEC_VER_0      0
#define CSD_SPEC_VER_1      1
#define CSD_SPEC_VER_2      2
#define CSD_SPEC_VER_3      3
#define CSD_SPEC_VER_4      4

/*
 * EXT_CSD fields
 */
#define EXT_CSD_BUS_WIDTH	183
#define EXT_CSD_HS_TIMING	185
#define EXT_CSD_CARD_TYPE	196
#define EXT_CSD_SEC_CNT		212

/*
 * EXT_CSD field definitions
 */
#define EXT_CSD_CMD_SET_NORMAL		(1<<0)
#define EXT_CSD_CMD_SET_SECURE		(1<<1)
#define EXT_CSD_CMD_SET_CPSECURE	(1<<2)

#define EXT_CSD_CARD_TYPE_26	(1<<0)
#define EXT_CSD_CARD_TYPE_52	(1<<1)

#define EXT_CSD_BUS_WIDTH_1	0
#define EXT_CSD_BUS_WIDTH_4	1

/*
 * MMC_SWITCH access modes
 */
#define MMC_SWITCH_MODE_CMD_SET		0x00
#define MMC_SWITCH_MODE_SET_BITS	0x01
#define MMC_SWITCH_MODE_CLEAR_BITS	0x02
#define MMC_SWITCH_MODE_WRITE_BYTE	0x03

extern struct mmc_host *mmc_glob;

int MMCSelectCard(struct mmc_host *, struct mmc_card *);
int MMCGoIdle(struct mmc_host *);
int MMCSendOPCond(struct mmc_host *, dword, dword *);
int MMCSendCID(struct mmc_host *, dword *);
int MMCSendCSD(struct mmc_card *, dword *);
int MMCSendExtCSD(struct mmc_card *, byte *);
int MMCSwitch(struct mmc_card *, byte, byte, byte);
int MMCSendStatus(struct mmc_card *, dword *);
int MMCAttach(struct mmc_host *, dword);

#endif  /* MMCSD_MMC_H */
