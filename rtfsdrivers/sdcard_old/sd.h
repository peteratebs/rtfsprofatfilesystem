/*
 */

#ifndef MMCSD_SD_H
#define MMCSD_SD_H

/* SD commands */
#define SD_SEND_RELATIVE_ADDR     3   /* bcr                     R6  */
#define SD_SEND_IF_COND           8   /* bcr  [11:0] See below   R7  */

/* Application commands */
#define SD_APP_SET_BUS_WIDTH      6   /* ac   [1:0] bus width    R1  */
#define SD_APP_SEND_NUM_WR_BLKS  22   /* adtc                    R1  */
#define SD_APP_OP_COND           41   /* bcr  [31:0] OCR         R3  */
#define SD_APP_SEND_SCR          51   /* adtc                    R1  */

/*
 * SCR field definitions
 */
#define SCR_SPEC_VER_0		0	/* Implements system specification 1.0 - 1.01 */
#define SCR_SPEC_VER_1		1	/* Implements system specification 1.10 */
#define SCR_SPEC_VER_2		2	/* Implements system specification 2.00 */

/*
 * SD bus widths
 */
#define SD_BUS_WIDTH_1		0
#define SD_BUS_WIDTH_4		2

extern struct mmc_host *mmc_glob;

int SDSendOpCond(struct mmc_host *, dword, dword *);
int SDSendIfCond(struct mmc_host *, dword);
int SDSwitch(struct mmc_card *, int, int, byte, byte *);
int SDAttach(struct mmc_host *, dword);

#endif  /* MMCSD_SD_H */
