/*
 */

#ifndef MMCSD_CARD_H
#define MMCSD_CARD_H


/* Bus widths */
#define SD_SCR_BUS_WIDTH_1	(1<<0)
#define SD_SCR_BUS_WIDTH_4	(1<<2)

/* Card type */
#define MMC_TYPE_MMC		0		    /* MMC card */
#define MMC_TYPE_SD		    1		    /* SD card */

/* Card state */
#define MMC_STATE_PRESENT	(1<<0)		/* present in sysfs */
#define MMC_STATE_READONLY	(1<<1)		/* card is read-only */
#define MMC_STATE_HIGHSPEED	(1<<2)		/* card is in high speed mode */
#define MMC_STATE_BLOCKADDR	(1<<3)		/* card uses block-addressing */

/* Command flag */
#define MMC_RSP_PRESENT	    (1 << 0)
#define MMC_RSP_136	        (1 << 1)	/* 136 bit response */
#define MMC_RSP_CRC	        (1 << 2)	/* expect valid crc */
#define MMC_RSP_BUSY	    (1 << 3)	/* card may send busy */
#define MMC_RSP_OPCODE	    (1 << 4)	/* response contains opcode */
#define MMC_CMD_MASK	    (3 << 5)	/* command type */
#define MMC_CMD_AC	        (0 << 5)
#define MMC_CMD_ADTC	    (1 << 5)
#define MMC_CMD_BC	        (2 << 5)
#define MMC_CMD_BCR	        (3 << 5)
#define MMC_RSP_NONE	(0)
#define MMC_RSP_R1	    (MMC_RSP_PRESENT|MMC_RSP_CRC|MMC_RSP_OPCODE)
#define MMC_RSP_R1B	    (MMC_RSP_PRESENT|MMC_RSP_CRC|MMC_RSP_OPCODE|MMC_RSP_BUSY)
#define MMC_RSP_R2	    (MMC_RSP_PRESENT|MMC_RSP_136|MMC_RSP_CRC)
#define MMC_RSP_R3	    (MMC_RSP_PRESENT)
#define MMC_RSP_R4	    (MMC_RSP_PRESENT)
#define MMC_RSP_R5	    (MMC_RSP_PRESENT|MMC_RSP_CRC|MMC_RSP_OPCODE)
#define MMC_RSP_R6	    (MMC_RSP_PRESENT|MMC_RSP_CRC|MMC_RSP_OPCODE)
#define MMC_RSP_R7	    (MMC_RSP_PRESENT|MMC_RSP_CRC|MMC_RSP_OPCODE)

/* Data flag */
#define MMC_DATA_WRITE	(1 << 8)
#define MMC_DATA_READ	(1 << 9)
#define MMC_DATA_STREAM	(1 << 10)
#define MMC_DATA_MULTI	(1 << 11)

struct mmc_cid
{
	dword		manfid;
	char		prod_name[8];
	dword		serial;
	word		oemid;
	word		year;
	byte		hwrev;
	byte		fwrev;
	byte		month;
};

struct mmc_csd
{
	byte		mmca_vsn;
	word		cmdclass;
	word		tacc_clks;
	dword		tacc_ns;
	dword		r2w_factor;
	dword		max_dtr;
	dword		read_blkbits;
	dword		write_blkbits;
	dword		capacity;
	dword		read_partial:1;
	dword		read_misalign:1;
	dword		write_partial:1;
	dword		write_misalign:1;
};

struct mmc_ext_csd
{
	dword		hs_max_dtr;
	dword		sectors;
};

struct sd_scr
{
	byte		sda_vsn;
	byte		bus_widths;
};

/*
 * MMC device
 */
struct mmc_card
{
	struct mmc_host		*host;		/* the host this device belongs to */
	struct device		dev;		/* the device */
	dword		rca;		/* relative card address of device */
	dword		type;		/* card type */
	dword		state;		/* (our) card state */
	dword			        raw_cid[4];	/* raw card CID */
	dword			        raw_csd[4];	/* raw card CSD */
	dword			        raw_scr[2];	/* raw card SCR */
	struct mmc_cid		cid;		/* card identification */
	struct mmc_csd		csd;		/* card specific */
	struct mmc_ext_csd	ext_csd;	/* mmc v4 extended card specific */
	struct sd_scr		scr;		/* extra SD information */
    dword        hs_max_dtr;
};

/*
 * These are the command types.
 */
struct mmc_command
{
	dword			        opcode;
	dword			        arg;
	dword			        resp[4];
	dword		flags;		/* expected response type */
	dword		retries;	/* max number of retries */
	dword		error;		/* command error */
	struct mmc_data		*data;		/* data segment associated with cmd */
	struct mmc_request	*mrq;		/* associated request */
};

struct mmc_data
{
	dword		timeout_ns;	/* data timeout (in ns, max 80ms) */
	dword		timeout_clks;	/* data timeout (in clocks) */
	dword		blksz;		/* data block size */
	dword		blocks;		/* number of blocks */
	dword		error;		/* data error */
	dword		flags;
	dword		bytes_xfered;
	struct mmc_command	*stop;		/* stop command */
	struct mmc_request	*mrq;		/* associated request */
	dword		sg_len;		/* size of scatter list */
	struct scatterlist	*sg;		/* I/O scatter list */
};

struct mmc_request
{
	struct mmc_command	*cmd;
	struct mmc_data		*data;
	struct mmc_command	*stop;
	void			    *done_data;	/* completion data */
	void			    (*done)(struct mmc_request *);/* completion function */
};

#endif  /* MMCSD_CARD_H */
