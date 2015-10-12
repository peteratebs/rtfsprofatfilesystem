/*
 */

#include "rtfs.h"


#if (0)
#include <linux/err.h>
#include <linux/scatterlist.h>
#include "card.h"
#include "host.h"
#include "core.h"
#include "mmc.h"
#include "oswrp.h"
#endif

#if (INCLUDE_SDCARD)

#include "rtfssdmmc.h"

extern int MMCSDReadWriteData(struct mmc_card *card, byte *ext_csd, int rw, int sector);

static const unsigned int tran_exp[] =
{
	10000,  100000, 1000000,    10000000,
	0,		0,		0,		    0
};

static const unsigned char tran_mant[] =
{
	0,	10,	12,	13,	15,	20,	25,	30,
	35,	40,	45,	50,	55,	60,	70,	80,
};

static const unsigned int tacc_exp[] =
{
	1, 10, 100, 1000, 10000, 100000, 1000000, 10000000,
};

static const unsigned int tacc_mant[] =
{
	0,	10,	12,	13,	15,	20,	25,	30,
	35,	40,	45,	50,	55,	60,	70,	80,
};

/*!
 * MMCDecodeCID - decode the raw CID to our CID structure.
 */
static int MMCDecodeCID(struct mmc_card *card)
{
	dword *resp = card->raw_cid;

	switch (card->csd.mmca_vsn)
    {
    	case 0: /* MMC v1.0 - v1.2 */
    	case 1: /* MMC v1.4 */
    		card->cid.manfid	    = UNSTUFF_BITS(resp, 104, 24);
    		card->cid.prod_name[0]	= UNSTUFF_BITS(resp, 96, 8);
    		card->cid.prod_name[1]	= UNSTUFF_BITS(resp, 88, 8);
    		card->cid.prod_name[2]	= UNSTUFF_BITS(resp, 80, 8);
    		card->cid.prod_name[3]	= UNSTUFF_BITS(resp, 72, 8);
    		card->cid.prod_name[4]	= UNSTUFF_BITS(resp, 64, 8);
    		card->cid.prod_name[5]	= UNSTUFF_BITS(resp, 56, 8);
    		card->cid.prod_name[6]	= UNSTUFF_BITS(resp, 48, 8);
    		card->cid.hwrev		    = UNSTUFF_BITS(resp, 44, 4);
    		card->cid.fwrev		    = UNSTUFF_BITS(resp, 40, 4);
    		card->cid.serial	    = UNSTUFF_BITS(resp, 16, 24);
    		card->cid.month		    = UNSTUFF_BITS(resp, 12, 4);
    		card->cid.year		    = UNSTUFF_BITS(resp, 8, 4) + 1997;
    		break;

    	case 2: /* MMC v2.0 - v2.2 */
    	case 3: /* MMC v3.1 - v3.3 */
    	case 4: /* MMC v4 */
    		card->cid.manfid	    = UNSTUFF_BITS(resp, 120, 8);
    		card->cid.oemid		    = UNSTUFF_BITS(resp, 104, 16);
    		card->cid.prod_name[0]	= UNSTUFF_BITS(resp, 96, 8);
    		card->cid.prod_name[1]	= UNSTUFF_BITS(resp, 88, 8);
    		card->cid.prod_name[2]	= UNSTUFF_BITS(resp, 80, 8);
    		card->cid.prod_name[3]	= UNSTUFF_BITS(resp, 72, 8);
    		card->cid.prod_name[4]	= UNSTUFF_BITS(resp, 64, 8);
    		card->cid.prod_name[5]	= UNSTUFF_BITS(resp, 56, 8);
    		card->cid.serial	    = UNSTUFF_BITS(resp, 16, 32);
    		card->cid.month		    = UNSTUFF_BITS(resp, 12, 4);
    		card->cid.year		    = UNSTUFF_BITS(resp, 8, 4) + 1997;
    		break;

    	default:
    		MMCTraceM(("    >>> card has unknown MMCA version %d\n", card->csd.mmca_vsn));
    		return -EINVAL;
	}

    MMCTraceM(("scr     %08x%08x\n", card->raw_scr[0], card->raw_scr[1]));
    MMCTraceM(("date    %02d/%04d\n", card->cid.month, card->cid.year));
    MMCTraceM(("fwrev   0x%x\n", card->cid.fwrev));
    MMCTraceM(("hwrev   0x%x\n", card->cid.hwrev));
    MMCTraceM(("manfid  0x%06x\n", card->cid.manfid));
    MMCTraceM(("name    %s\n", card->cid.prod_name));
    MMCTraceM(("oemid   0x%04x\n", card->cid.oemid));
    MMCTraceM(("serial  0x%08x\n", card->cid.serial));

	return 0;
}

/*!
 * MMCDecodeCSD - decode the raw CSD to our card CSD structure.
 */
static int MMCDecodeCSD(struct mmc_card *card)
{
	struct mmc_csd *csd = &card->csd;
	unsigned int e, m, csd_struct;
	dword *resp = card->raw_csd;

	csd_struct = UNSTUFF_BITS(resp, 126, 2);
	if (csd_struct != 1 && csd_struct != 2)
    {
		MMCTraceM(("    >>> unrecognised CSD structure version %d\n", csd_struct));
		return -EINVAL;
	}

	csd->mmca_vsn	 = UNSTUFF_BITS(resp, 122, 4);
	m = UNSTUFF_BITS(resp, 115, 4);
	e = UNSTUFF_BITS(resp, 112, 3);
	csd->tacc_ns	 = (tacc_exp[e] * tacc_mant[m] + 9) / 10;
	csd->tacc_clks	 = UNSTUFF_BITS(resp, 104, 8) * 100;

	m = UNSTUFF_BITS(resp, 99, 4);
	e = UNSTUFF_BITS(resp, 96, 3);
	csd->max_dtr	  = tran_exp[e] * tran_mant[m];
	csd->cmdclass	  = UNSTUFF_BITS(resp, 84, 12);

	e = UNSTUFF_BITS(resp, 47, 3);
	m = UNSTUFF_BITS(resp, 62, 12);
	csd->capacity	  = (1 + m) << (e + 2);

	csd->read_blkbits = UNSTUFF_BITS(resp, 80, 4);
	csd->read_partial = UNSTUFF_BITS(resp, 79, 1);
	csd->write_misalign = UNSTUFF_BITS(resp, 78, 1);
	csd->read_misalign = UNSTUFF_BITS(resp, 77, 1);
	csd->r2w_factor = UNSTUFF_BITS(resp, 26, 3);
	csd->write_blkbits = UNSTUFF_BITS(resp, 22, 4);
	csd->write_partial = UNSTUFF_BITS(resp, 21, 1);

    MMCTraceM(("capacity        0x%X\n", csd->capacity));
    MMCTraceM(("max_dtr         0x%X\n", csd->max_dtr));
    MMCTraceM(("read_blkbits    0x%X\n", csd->read_blkbits));
    MMCTraceM(("read_partial    0x%X\n", csd->read_partial));
    MMCTraceM(("write_blkbits   0x%X\n", csd->write_blkbits));
    MMCTraceM(("write_partial   0x%X\n", csd->write_partial));

	return 0;
}

/*!
 * MMCReadExtCSD - read and decode extended CSD.
 */
static int MMCReadExtCSD(struct mmc_card *card)
{
	int err;
	byte *ext_csd;

    MMCTraceM(("    >>> MMCReadExtCSD\n"));

	err = -EIO;

	if (card->csd.mmca_vsn < CSD_SPEC_VER_4)
		return 0;

	ext_csd = (byte *)kmalloc(512, GFP_KERNEL);
	if (!ext_csd)
    {
		MMCTraceM(("    >>> could not allocate a buffer to receive the ext_csd\n"));
		return -ENOMEM;
	}

	err = MMCSendExtCSD(card, ext_csd);
	if (err)
    {
		if (err != -EINVAL)
			goto out;

		if (card->csd.capacity == (4096 * 512))
        {
			MMCTraceM(("    >>> unable to read EXT_CSD "
				"on a possible high capacity card. "
				"Card will be ignored\n"));
		} else
        {
			MMCTraceM(("    >>> unable to read EXT_CSD, performance might suffer\n"));
			err = 0;
		}

		goto out;
	}

	card->ext_csd.sectors =
		ext_csd[EXT_CSD_SEC_CNT + 0] << 0 |
		ext_csd[EXT_CSD_SEC_CNT + 1] << 8 |
		ext_csd[EXT_CSD_SEC_CNT + 2] << 16 |
		ext_csd[EXT_CSD_SEC_CNT + 3] << 24;
	if (card->ext_csd.sectors)
		card->state |= MMC_STATE_BLOCKADDR;

	switch (ext_csd[EXT_CSD_CARD_TYPE])
    {
    	case EXT_CSD_CARD_TYPE_52 | EXT_CSD_CARD_TYPE_26:
    		card->ext_csd.hs_max_dtr = 52000000;
    		break;
    	case EXT_CSD_CARD_TYPE_26:
    		card->ext_csd.hs_max_dtr = 26000000;
    		break;
    	default:
    		MMCTraceM(("    >>> card is mmc v4 but doesn't "
    			"support any high-speed modes.\n"));
    		goto out;
	}

out:
	kfree(ext_csd);

	return err;
}

/*!
 * MMCSelectCard - Send command: select card.
 */
int MMCSelectCard(struct mmc_host *host, struct mmc_card *card)
{
	int err;
	struct mmc_command cmd;

	memset(&cmd, 0, sizeof(struct mmc_command));

	cmd.opcode = MMCSD_SELECT_CARD;

	if (card)
    {
		cmd.arg = card->rca << 16;
		cmd.flags = MMC_RSP_R1 | MMC_CMD_AC;
	} else
    {
		cmd.arg = 0;
		cmd.flags = MMC_RSP_NONE | MMC_CMD_AC;
	}

	err = MMCSDWaitForCmd(host, &cmd, MMC_CMD_RETRIES);
    if (err)
		return err;

	return 0;
}

/*!
 * MMCGoIdle - Send command: go idle.
 */
int MMCGoIdle(struct mmc_host *host)
{
	int err;
	struct mmc_command cmd;

    msleep(1);

	memset(&cmd, 0, sizeof(struct mmc_command));

	cmd.opcode = MMCSD_GO_IDLE_STATE;
	cmd.arg = 0;
	cmd.flags = MMC_RSP_NONE | MMC_CMD_BC;

	err = MMCSDWaitForCmd(host, &cmd, 0);

    msleep(2);

	return err;
}

/*!
 * MMCSendOPCond - Send command: send op cond.
 */
int MMCSendOPCond(struct mmc_host *host, dword ocr, dword *rocr)
{
	struct mmc_command cmd;
	int i, err = 0;

	memset(&cmd, 0, sizeof(struct mmc_command));

	cmd.opcode = MMCSD_SEND_OP_COND;
	cmd.arg = ocr;
	cmd.flags = MMC_RSP_R3 | MMC_CMD_BCR;

	for (i = 100; i; i--)
    {
		err = MMCSDWaitForCmd(host, &cmd, 0);
		if (err)
			break;

		if (cmd.resp[0] & MMC_CARD_BUSY || ocr == 0)
			break;

		err = -ETIMEDOUT;

        msleep(10);
	}

	if (rocr)
		*rocr = cmd.resp[0];

	return err;
}

/*!
 * MMCSendCID - Send command: send CID.
 */
int MMCSendCID(struct mmc_host *host, dword *cid)
{
	int err;
	struct mmc_command cmd;

	memset(&cmd, 0, sizeof(struct mmc_command));

	cmd.opcode = MMCSD_ALL_SEND_CID;
	cmd.arg = 0;
	cmd.flags = MMC_RSP_R2 | MMC_CMD_BCR;

	err = MMCSDWaitForCmd(host, &cmd, MMC_CMD_RETRIES);
	if (err)
		return err;

	memcpy(cid, cmd.resp, sizeof(dword) * 4);

	return 0;
}

/*!
 * MMCSetRelativeAddr - Send command: send relative address.
 */
int MMCSetRelativeAddr(struct mmc_card *card)
{
	int err;
	struct mmc_command cmd;

    memset(&cmd, 0, sizeof(struct mmc_command));

	cmd.opcode = MMCSD_SET_RELATIVE_ADDR;
	cmd.arg = card->rca << 16;
	cmd.flags = MMC_RSP_R1 | MMC_CMD_AC;

	err = MMCSDWaitForCmd(card->host, &cmd, MMC_CMD_RETRIES);
	if (err)
		return err;

	return 0;
}

/*!
 * MMCSendCSD - Send command: send CSD.
 */
int MMCSendCSD(struct mmc_card *card, dword *csd)
{
	int err;
	struct mmc_command cmd;

	memset(&cmd, 0, sizeof(struct mmc_command));

	cmd.opcode = MMCSD_SEND_CSD;
	cmd.arg = card->rca << 16;
	cmd.flags = MMC_RSP_R2 | MMC_CMD_AC;

	err = MMCSDWaitForCmd(card->host, &cmd, MMC_CMD_RETRIES);
	if (err)
		return err;

	memcpy(csd, cmd.resp, sizeof(dword) * 4);

	return 0;
}

/*!
 * MMCSendExtCSD - Send command: send external CSD.
 */
int MMCSendExtCSD(struct mmc_card *card, byte *ext_csd)
{
	struct mmc_request mrq;
	struct mmc_command cmd;
	struct mmc_data data;
	struct scatterlist sg;

    MMCTraceM(("    >>> MMC MMCSendExtCSD\n"));

	memset(&mrq, 0, sizeof(struct mmc_request));
	memset(&cmd, 0, sizeof(struct mmc_command));
	memset(&data, 0, sizeof(struct mmc_data));

	mrq.cmd = &cmd;
	mrq.data = &data;

	cmd.opcode = MMCSD_SEND_EXT_CSD;
	cmd.arg = 0;
	cmd.flags = MMC_RSP_R1 | MMC_CMD_ADTC;

	data.blksz = 512;
	data.blocks = 1;
	data.flags = MMC_DATA_READ;
	data.sg = &sg;
	data.sg_len = 1;

	sg_init_one(&sg, ext_csd, 512);

    MMCSDSetDataTimeout(&data, card, 0);

	MMCSDWaitForRequest(card->host, &mrq, NULL);

	if (cmd.error)
		return cmd.error;
	if (data.error)
		return data.error;

	return 0;
}

/*!
 * MMCSwitch - Send command: switch.
 */
int MMCSwitch(struct mmc_card *card, byte set, byte index, byte value)
{
	int err;
	struct mmc_command cmd;

	memset(&cmd, 0, sizeof(struct mmc_command));

	cmd.opcode = MMCSD_SWITCH;
	cmd.arg = (MMC_SWITCH_MODE_WRITE_BYTE << 24) |
		  (index << 16) |
		  (value << 8) |
		  set;
	cmd.flags = MMC_RSP_R1B | MMC_CMD_AC;

	err = MMCSDWaitForCmd(card->host, &cmd, MMC_CMD_RETRIES);
	if (err)
		return err;

	return 0;
}

/*!
 * MMCSendStatus - Send command: send status.
 */
int MMCSendStatus(struct mmc_card *card, dword *status)
{
	int err;
	struct mmc_command cmd;

	memset(&cmd, 0, sizeof(struct mmc_command));

	cmd.opcode = MMCSD_SEND_STATUS;
	cmd.arg = card->rca << 16;
	cmd.flags = MMC_RSP_R1 | MMC_CMD_AC;

	err = MMCSDWaitForCmd(card->host, &cmd, MMC_CMD_RETRIES);
	if (err)
		return err;

	if (status)
		*status = cmd.resp[0];

	return 0;
}

/*!
 * MMCInitCard - detection and initialisation of a card.
 */
static int MMCInitCard(struct mmc_host *host, dword ocr, struct mmc_card *oldcard)
{
	struct mmc_card *card;
	int err;
	dword cid[4];
	unsigned int max_dtr;

	MMCGoIdle(host);

	err = MMCSendOPCond(host, ocr | (1 << 30), NULL);
	if (err)
		goto err;

	err = MMCSendCID(host, cid);
	if (err)
		goto err;

	if (oldcard)
    {
		if (memcmp(cid, oldcard->raw_cid, sizeof(cid)) != 0)
        {
			err = -ENOENT;
			goto err;
		}

		card = oldcard;
	} else
    {
		card = MMCSDAllocCard(host);
		if (IS_ERR(card))
        {
			err = PTR_ERR(card);
			goto err;
		}

		card->type = MMC_TYPE_MMC;
		card->rca = 1;
		memcpy(card->raw_cid, cid, sizeof(card->raw_cid));
	}

	err = MMCSetRelativeAddr(card);
	if (err)
		goto free_card;

	if (!oldcard)
    {
		err = MMCSendCSD(card, card->raw_csd);
		if (err)
			goto free_card;

		err = MMCDecodeCSD(card);
		if (err)
			goto free_card;
		err = MMCDecodeCID(card);
		if (err)
			goto free_card;
	}

    err = MMCSelectCard(card->host, card);
	if (err)
		goto free_card;

	if (!oldcard)
    {
		err = MMCReadExtCSD(card);
		if (err)
			goto free_card;
	}

	if ((card->ext_csd.hs_max_dtr != 0) && (host->caps & MMC_CAP_MMC_HIGHSPEED))
    {
		err = MMCSwitch(card, EXT_CSD_CMD_SET_NORMAL,
			EXT_CSD_HS_TIMING, 1);
		if (err)
			goto free_card;

		card->state |= MMC_STATE_HIGHSPEED;
	}

	max_dtr = (unsigned int)-1;

	if (card->state & MMC_STATE_HIGHSPEED)
    {
		if (max_dtr > card->ext_csd.hs_max_dtr)
			max_dtr = card->ext_csd.hs_max_dtr;
	} else if (max_dtr > card->csd.max_dtr)
    {
		max_dtr = card->csd.max_dtr;
	}

	MMCSDSetClock(host, max_dtr);

	if ((card->csd.mmca_vsn >= CSD_SPEC_VER_4) && (host->caps & MMC_CAP_4_BIT_DATA))
    {
		err = MMCSwitch(card, EXT_CSD_CMD_SET_NORMAL,
			EXT_CSD_BUS_WIDTH, EXT_CSD_BUS_WIDTH_4);
		if (err)
			goto free_card;

		MMCSDSetBusWidth(card->host, MMC_BUS_WIDTH_4);
	}

	if (!oldcard)
		host->card = card;

	return 0;

free_card:
	if (!oldcard)
		MMCSDRemoveCard(card);
err:

	return err;
}

/*!
 * MMCRemove - free up the current card.
 */
static void MMCRemove(struct mmc_host *host)
{
    MMCTraceM(("    >>> MMCRemove\n"));
	MMCSDRemoveCard(host->card);
	host->card = NULL;
}

/*!
 * MMCDetect - card detection callback from host.
 */
static void MMCDetect(struct mmc_host *host)
{
	int err;

    MMCTraceM(("    >>> MMCDetect\n"));

	MMCSDClaimHost(host);

	err = MMCSendStatus(host->card, NULL);

	MMCSDReleaseHost(host);

	if (err)
    {
		MMCRemove(host);
		MMCSDClaimHost(host);
		MMCDetachBus(host);
		MMCSDReleaseHost(host);
	}
}


//static const struct mmc_bus_ops mmc_ops =
//{
//	.remove = MMCRemove,
//	.detect = MMCDetect,
//};

/*!
 * MMC_TEST_RW - test read/write function. Only for debug.
 */
#define INCLUDETESTRDWR 0
#if (INCLUDETESTRDWR)

static int MMC_TEST_RW (struct mmc_host *host)
{
    char* ext_buf;
    int i;

    MMCSDClaimHost(host);

    ext_buf = kmalloc(512, GFP_KERNEL);
	if (!ext_buf)
    {
		MMCTraceM(("    >>> could not allocate a buffer to receive the ext_csd\n"));
		return -1;
	}

    MMCSDReadWriteData(host->card, ext_buf, 0, 0);

    MMCTraceM(("    >>> Read sector 0\n"));
    for (i = 0; i<=511; i++)
    {
        MMCTraceM(("%02X ", ext_buf[i]));
        if (((i+1) % 16) == 0)
        {
            MMCTraceM(("\n"));
        }
    }
    MMCTraceM(("\n"));

    MMCSDReadWriteData(host->card, ext_buf, 0, 512);

    MMCTraceM(("    >>> Read sector 1\n"));
    for (i = 0; i<=511; i++)
    {
        MMCTraceM(("%02X ", ext_buf[i]));
        if (((i+1) % 16) == 0)
        {
            MMCTraceM(("\n"));
        }
    }
    MMCTraceM(("\n"));

    MMCSDReadWriteData(host->card, ext_buf, 0, 1024);

    MMCTraceM(("    >>> Read sector 2\n"));
    for (i = 0; i<=511; i++)
    {
        MMCTraceM(("%02X ", ext_buf[i]));
        if (((i+1) % 16) == 0)
        {
            MMCTraceM(("\n"));
        }
    }
    MMCTraceM(("\n"));

    MMCSDReleaseHost(host);

    kfree(ext_buf);

    return 0;
}
#endif /* (INCLUDETESTRDWR) */

/*!
 * MMCAttach - starting point for MMC card init.
 */
int MMCAttach(struct mmc_host *host, dword ocr)
{
	int err;

//	MMCSDAttachBus(host, &mmc_ops);
    MMCTraceM(("    >>> MMCAttach\n"));

	if (ocr & 0x7F)
    {
		MMCTraceM(("    >>> card claims to support voltages "
		       "below the defined range. These will be ignored\n"));
		ocr &= ~0x7F;
	}

	host->ocr = MMCSDSelectVoltage(host, ocr);

	if (!host->ocr)
    {
		err = -EINVAL;
		goto err;
	}

	err = MMCInitCard(host, host->ocr, NULL);
	if (err)
		goto err;

	MMCSDReleaseHost(host);

	err = MMCSDAddCard(host->card);
	if (err)
		goto remove_card;

    mmc_glob = host;
    //MMC_TEST_RW(host);  // Test read function

	return 0;

remove_card:
	MMCSDRemoveCard(host->card);
	host->card = NULL;
	MMCSDClaimHost(host);
err:
	MMCDetachBus(host);
	MMCSDReleaseHost(host);

	MMCTraceM(("    >>> error %d whilst initialising MMC card\n", err));

	return err;
}

#endif
