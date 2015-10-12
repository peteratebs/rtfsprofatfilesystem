/*
 */

#ifndef MMCSD_HOST_H
#define MMCSD_HOST_H



/* Power mode */
#define MMC_POWER_OFF		0
#define MMC_POWER_UP		1
#define MMC_POWER_ON		2

/* Bus width */
#define MMC_BUS_WIDTH_1		0
#define MMC_BUS_WIDTH_4		2

/* Available Vdd */
#define MMC_VDD_165_195		0x00000080	/* VDD voltage 1.65 - 1.95 */
#define MMC_VDD_20_21		0x00000100	/* VDD voltage 2.0 ~ 2.1 */
#define MMC_VDD_21_22		0x00000200	/* VDD voltage 2.1 ~ 2.2 */
#define MMC_VDD_22_23		0x00000400	/* VDD voltage 2.2 ~ 2.3 */
#define MMC_VDD_23_24		0x00000800	/* VDD voltage 2.3 ~ 2.4 */
#define MMC_VDD_24_25		0x00001000	/* VDD voltage 2.4 ~ 2.5 */
#define MMC_VDD_25_26		0x00002000	/* VDD voltage 2.5 ~ 2.6 */
#define MMC_VDD_26_27		0x00004000	/* VDD voltage 2.6 ~ 2.7 */
#define MMC_VDD_27_28		0x00008000	/* VDD voltage 2.7 ~ 2.8 */
#define MMC_VDD_28_29		0x00010000	/* VDD voltage 2.8 ~ 2.9 */
#define MMC_VDD_29_30		0x00020000	/* VDD voltage 2.9 ~ 3.0 */
#define MMC_VDD_30_31		0x00040000	/* VDD voltage 3.0 ~ 3.1 */
#define MMC_VDD_31_32		0x00080000	/* VDD voltage 3.1 ~ 3.2 */
#define MMC_VDD_32_33		0x00100000	/* VDD voltage 3.2 ~ 3.3 */
#define MMC_VDD_33_34		0x00200000	/* VDD voltage 3.3 ~ 3.4 */
#define MMC_VDD_34_35		0x00400000	/* VDD voltage 3.4 ~ 3.5 */
#define MMC_VDD_35_36		0x00800000	/* VDD voltage 3.5 ~ 3.6 */

/* Caps */
#define MMC_CAP_4_BIT_DATA	    (1 << 0)
#define MMC_CAP_MULTIWRITE	    (1 << 1)
#define MMC_CAP_BYTEBLOCK	    (1 << 2)
#define MMC_CAP_MMC_HIGHSPEED	(1 << 3)
#define MMC_CAP_SD_HIGHSPEED	(1 << 4)

struct mmc_ios
{
	unsigned int	    clock;
	unsigned short	    vdd;
	unsigned char	    power_mode;
	unsigned char	    bus_width;
};

struct mmc_host
{
	struct device		*parent;
	struct device		class_dev;
	int			        index;
	unsigned int		f_min;
	unsigned int		f_max;
	dword			        ocr_avail;
	unsigned long		caps;
	spinlock_t		    lock;
	struct mmc_ios		ios;
	dword			        ocr;
	struct mmc_card		*card;
	wait_queue_head_t	wq;
	unsigned int		claimed:1;
	struct delayed_work	detect;
	const struct mmc_bus_ops *bus_ops;
	unsigned int		bus_refs;
	unsigned int		bus_dead:1;
//	unsigned long		private[0] ____cacheline_aligned;

	unsigned long		what_private[1];

};

#endif  /* MMCSD_HOST_H */
