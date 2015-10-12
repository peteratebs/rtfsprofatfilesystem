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
#include "sd.h"
#include "oswrp.h"
#endif

#if (INCLUDE_SDCARD)

#include "rtfssdmmc.h"

extern int MMCSDReadWriteData(struct mmc_card *card, byte *ext_csd, int rw, int sector);

static const unsigned int tran_exp[] =
{
	10000,		100000,		1000000,	10000000,
	0,		0,		0,		0
};

static const unsigned char tran_mant[] =
{
	0,	10,	12,	13,	15,	20,	25,	30,
	35,	40,	45,	50,	55,	60,	70,	80,
};

static const unsigned int tacc_exp[] =
{
	1,	10,	100,	1000,	10000,	100000,	1000000, 10000000,
};

static const unsigned int tacc_mant[] =
{
	0,	10,	12,	13,	15,	20,	25,	30,
	35,	40,	45,	50,	55,	60,	70,	80,
};


/*!
 * SDDecodeCID - decode the raw CID to our CID structure.
 */
static void SDDecodeCID(struct mmc_card *card)
{
	dword *resp = card->raw_cid;

	rtfs_memset(&card->cid, 0, sizeof(struct mmc_cid));

	card->cid.manfid		= UNSTUFF_BITS(resp, 120, 8);
	card->cid.oemid			= UNSTUFF_BITS(resp, 104, 16);
	card->cid.prod_name[0]	= UNSTUFF_BITS(resp, 96, 8);
	card->cid.prod_name[1]	= UNSTUFF_BITS(resp, 88, 8);
	card->cid.prod_name[2]	= UNSTUFF_BITS(resp, 80, 8);
	card->cid.prod_name[3]	= UNSTUFF_BITS(resp, 72, 8);
	card->cid.prod_name[4]	= UNSTUFF_BITS(resp, 64, 8);
	card->cid.hwrev			= UNSTUFF_BITS(resp, 60, 4);
	card->cid.fwrev			= UNSTUFF_BITS(resp, 56, 4);
	card->cid.serial		= UNSTUFF_BITS(resp, 24, 32);
	card->cid.year			= UNSTUFF_BITS(resp, 12, 8);
	card->cid.month			= UNSTUFF_BITS(resp, 8, 4);

	card->cid.year += 2000;

    MMCTraceM(("cid                     %08x%08x%08x%08x\n", card->raw_cid[0], card->raw_cid[1], card->raw_cid[2], card->raw_cid[3]));
    MMCTraceM(("csd                     %08x%08x%08x%08x\n", card->raw_csd[0], card->raw_csd[1], card->raw_csd[2], card->raw_csd[3]));
    MMCTraceM(("scr                     %08x%08x\n", card->raw_scr[0], card->raw_scr[1]));
    MMCTraceM(("date                    %02d/%04d\n", card->cid.month, card->cid.year));
    MMCTraceM(("fwrev                   0x%x\n", card->cid.fwrev));
    MMCTraceM(("hwrev                   0x%x\n", card->cid.hwrev));
    MMCTraceM(("manfid                  0x%06x\n", card->cid.manfid));
    MMCTraceM(("name                    %s\n", card->cid.prod_name));
    MMCTraceM(("oemid                   0x%04x\n", card->cid.oemid));
    MMCTraceM(("serial                  0x%08x\n", card->cid.serial));
}

/*!
 * SDDecodeCSD - decode the raw CSD to our card CSD structure.
 */
static int SDDecodeCSD(struct mmc_card *card)
{
	struct mmc_csd *csd = &card->csd;
	unsigned int e, m, csd_struct;
	dword *resp = card->raw_csd;

	csd_struct = UNSTUFF_BITS(resp, 126, 2);

	switch (csd_struct)
    {
    	case 0:
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
    		break;

    	case 1:
       		card->state |= MMC_STATE_BLOCKADDR;

    		csd->tacc_ns	 = 0;
    		csd->tacc_clks	 = 0;

    		m = UNSTUFF_BITS(resp, 99, 4);
    		e = UNSTUFF_BITS(resp, 96, 3);
    		csd->max_dtr	  = tran_exp[e] * tran_mant[m];
    		csd->cmdclass	  = UNSTUFF_BITS(resp, 84, 12);

    		m = UNSTUFF_BITS(resp, 48, 22);
    		csd->capacity     = (1 + m) << 10;

    		csd->read_blkbits = 9;
    		csd->read_partial = 0;
    		csd->write_misalign = 0;
    		csd->read_misalign = 0;
    		csd->r2w_factor = 4;
    		csd->write_blkbits = 9;
    		csd->write_partial = 0;
    		break;

    	default:
    		MMCTraceM(("    >>> unrecognised CSD structure version %d\n", csd_struct));
    		return -EINVAL;
	}

    if(csd_struct == 0)
    {
        MMCTraceM(("MAX read current        0x%X\n", UNSTUFF_BITS(resp, 56, 3)));
        MMCTraceM(("MIN read current        0x%X\n", UNSTUFF_BITS(resp, 59, 3)));
        MMCTraceM(("MAX write current       0x%X\n", UNSTUFF_BITS(resp, 50, 3)));
        MMCTraceM(("MIN wrute current       0x%X\n", UNSTUFF_BITS(resp, 53, 3)));
        MMCTraceM(("W protect group size    0x%X\n", UNSTUFF_BITS(resp, 39, 7)));
        MMCTraceM(("W protect group enable  0x%X\n", UNSTUFF_BITS(resp, 46, 1)));
        MMCTraceM(("TMP write protect       0x%X\n", UNSTUFF_BITS(resp, 1, 1)));
    }

    MMCTraceM(("csd_struct              0x%X\n", csd_struct));
    MMCTraceM(("capacity                0x%X\n", csd->capacity));
    MMCTraceM(("max_dtr                 0x%X\n", csd->max_dtr));
    MMCTraceM(("read_blkbits            0x%X\n", csd->read_blkbits));
    MMCTraceM(("read_partial            0x%X\n", csd->read_partial));
    MMCTraceM(("write_blkbits           0x%X\n", csd->write_blkbits));
    MMCTraceM(("write_partial           0x%X\n", csd->write_partial));
    MMCTraceM(("cmdclass                0x%X\n", csd->cmdclass));

	return 0;
}

/*!
 * SDDecodeCSR - decode to our card SCR structure.
 */
static int SDDecodeCSR(struct mmc_card *card)
{
	struct sd_scr *scr = &card->scr;
	unsigned int scr_struct;
	dword resp[4];

	resp[3] = card->raw_scr[1];
	resp[2] = card->raw_scr[0];

	scr_struct = UNSTUFF_BITS(resp, 60, 4);
	if (scr_struct != 0)
    {
		MMCTraceM(("    >>> unrecognised SCR structure version %d\n", scr_struct));
		return -EINVAL;
	}

	scr->sda_vsn = UNSTUFF_BITS(resp, 56, 4);
	scr->bus_widths = UNSTUFF_BITS(resp, 48, 4);

    MMCTraceM(("scr struct     = %x\n", scr_struct));
    MMCTraceM(("scr sda_vsn    = %x\n", scr->sda_vsn));
    MMCTraceM(("scr bus_widths = %x\n", scr->bus_widths));

	return 0;
}

/*!
 * SDReadSwitch - fetches and decodes switch information.
 */
static int SDReadSwitch(struct mmc_card *card)
{
	int err;
	byte *status;

	if (card->scr.sda_vsn < SCR_SPEC_VER_1)
		return 0;

	if (!(card->csd.cmdclass & (1<<10)))
    {
		MMCTraceM(("    >>> card lacks mandatory switch function, performance might suffer\n"));
		return 0;
	}

	err = -EIO;

	status = (byte *)kmalloc(64, GFP_KERNEL);
	if (!status)
    {
		MMCTraceM(("    >>> could not allocate a buffer for switch capabilities\n"));
		return -ENOMEM;
	}

	err = SDSwitch(card, 0, 0, 1, status);
	if (err)
    {
		if (err != -EINVAL)
			goto out;

		MMCTraceM(("    >>> problem reading switch capabilities, performance might suffer\n"));
		err = 0;

		goto out;
	}

	if (status[13] & 0x02)
        card->hs_max_dtr = 50000000;

out:
	kfree(status);

	return err;
}

/*!
 * SDSwitchHS - test if the card supports high-speed mode and switch to it.
 */
static int SDSwitchHS(struct mmc_card *card)
{
	int err;
	byte *status;

	if (card->scr.sda_vsn < SCR_SPEC_VER_1)
		return 0;

	if (!(card->csd.cmdclass & (1<<10)))
		return 0;

	if (!(card->host->caps & MMC_CAP_SD_HIGHSPEED))
		return 0;

    if (card->hs_max_dtr == 0)
		return 0;

	err = -EIO;

	status = (byte *)kmalloc(64, GFP_KERNEL);
	if (!status)
    {
		MMCTraceM(("    >>> could not allocate a buffer for switch capabilities\n"));
		return -ENOMEM;
	}

	err = SDSwitch(card, 1, 0, 1, status);
	if (err)
		goto out;

	if ((status[16] & 0xF) != 1)
    {
		MMCTraceM(("    >>> problem switching card into high-speed mode!\n"));
	} else
    {
		card->state |= MMC_STATE_HIGHSPEED;
	}

out:
	kfree(status);

	return err;
}

/*!
 * SDAppCmd - send command: app cmd.
 */
static int SDAppCmd(struct mmc_host *host, struct mmc_card *card)
{
	int err;
	struct mmc_command cmd;

	cmd.opcode = MMCSD_APP_CMD;

	if (card)
    {
		cmd.arg = card->rca << 16;
		cmd.flags = MMC_RSP_R1 | MMC_CMD_AC;
	} else
    {
		cmd.arg = 0;
		cmd.flags = MMC_RSP_R1 | MMC_CMD_BCR;
	}

	err = MMCSDWaitForCmd(host, &cmd, 0);
	if (err)
		return err;

	if (!(cmd.resp[0] & R1_APP_CMD))
		return -EOPNOTSUPP;

	return 0;
}

/*!
 *  SDWaitForAppCmd - start an application command and wait for completion.
 */
int SDWaitForAppCmd(struct mmc_host *host, struct mmc_card *card, struct mmc_command *cmd, int retries)
{
	struct mmc_request mrq;

	int i, err;

    MMCTraceM(("    >>> SDWaitForAppCmd\n"));

	err = -EIO;

	for (i = 0;i <= retries;i++)
    {
		rtfs_memset(&mrq, 0, sizeof(struct mmc_request));

		err = SDAppCmd(host, card);
		if (err)
			continue;

		rtfs_memset(&mrq, 0, sizeof(struct mmc_request));

		rtfs_memset(cmd->resp, 0, sizeof(cmd->resp));
		cmd->retries = 0;

		mrq.cmd = cmd;
		cmd->data = NULL;

		MMCSDWaitForRequest(host, &mrq, NULL);

		err = cmd->error;
		if (!cmd->error)
			break;
	}

	return err;
}

/*!
 *  SDAppSetBusWidth - send command: set bus width.
 */
int SDAppSetBusWidth(struct mmc_card *card, int width)
{
	int err;
	struct mmc_command cmd;

    rtfs_memset(&cmd, 0, sizeof(struct mmc_command));

	cmd.opcode = SD_APP_SET_BUS_WIDTH;
	cmd.flags = MMC_RSP_R1 | MMC_CMD_AC;

	switch (width)
    {
    	case MMC_BUS_WIDTH_1:
    		cmd.arg = SD_BUS_WIDTH_1;
    		break;
    	case MMC_BUS_WIDTH_4:
    		cmd.arg = SD_BUS_WIDTH_4;
    		break;
    	default:
    		return -EINVAL;
	}

	err = SDWaitForAppCmd(card->host, card, &cmd, MMC_CMD_RETRIES);
	if (err)
		return err;

	return 0;
}

/*!
 *  SDSendOpCond - send command: app op cond.
 */
int SDSendOpCond(struct mmc_host *host, dword ocr, dword *rocr)
{
	struct mmc_command cmd;
	int i, err = 0;

	rtfs_memset(&cmd, 0, sizeof(struct mmc_command));

	cmd.opcode = SD_APP_OP_COND;
	cmd.arg = ocr;
	cmd.flags = MMC_RSP_R3 | MMC_CMD_BCR;

	for (i = 100; i; i--)
    {
		err = SDWaitForAppCmd(host, NULL, &cmd, MMC_CMD_RETRIES);
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
 *  SDSendIfCond - send command: send if cond.
 */
int SDSendIfCond(struct mmc_host *host, dword ocr)
{
	struct mmc_command cmd;
	int err;
	static const byte test_pattern = 0xAA;

	cmd.opcode = SD_SEND_IF_COND;
	cmd.arg = ((ocr & 0xFF8000) != 0) << 8 | test_pattern;
	cmd.flags = MMC_RSP_R7 | MMC_CMD_BCR;

	err = MMCSDWaitForCmd(host, &cmd, 0);
	if (err)
		return err;

	if ((cmd.resp[0] & 0xFF) != test_pattern)
		return -EIO;

	return 0;
}

/*!
 *  SDSendRelativeAddr - send command: send relative address.
 */
int SDSendRelativeAddr(struct mmc_host *host, dword *rca)
{
	int err;
	struct mmc_command cmd;

	rtfs_memset(&cmd, 0, sizeof(struct mmc_command));

	cmd.opcode = SD_SEND_RELATIVE_ADDR;
	cmd.arg = 0;
	cmd.flags = MMC_RSP_R6 | MMC_CMD_BCR;

	err = MMCSDWaitForCmd(host, &cmd, MMC_CMD_RETRIES);
	if (err)
		return err;

	*rca = cmd.resp[0] >> 16;

	return 0;
}

/*!
 *  SDSendSCR - send command: send SCR.
 */
int SDSendSCR(struct mmc_card *card, dword *scr)
{
	int err;
	struct mmc_request mrq;
	struct mmc_command cmd;
	struct mmc_data data;
	struct scatterlist sg;
    struct MCI_data mci_data;
    char buf[8];
    dword tmp;

    MMCTraceM(("    >>> SDSendSCR\n"));

    err = SDAppCmd(card->host, card);
	if (err)
		return err;

	rtfs_memset(&mrq, 0, sizeof(struct mmc_request));
	rtfs_memset(&cmd, 0, sizeof(struct mmc_command));
	rtfs_memset(&data, 0, sizeof(struct mmc_data));
    rtfs_memset(&mci_data, 0, sizeof(struct MCI_data));

	mrq.cmd = &cmd;
	mrq.data = &data;

	cmd.opcode = SD_APP_SEND_SCR;
	cmd.arg = 0;
	cmd.flags = MMC_RSP_R1 | MMC_CMD_ADTC;

	data.blksz = 8;
	data.blocks = 1;
	data.flags = MMC_DATA_READ;
	data.sg = &sg;
	data.sg_len = 1;

	sg_init_one(&sg, scr, 8);

    MMCSDSetDataTimeout(&data, card, 0);

    mci_data.data = buf;
    mci_data.len = 8;

	MMCSDWaitForRequest(card->host, &mrq, &mci_data);

	if (cmd.error)
		return cmd.error;
	if (data.error)
		return data.error;

    tmp = buf[0] << 24;
    scr[0] += tmp & 0xFF000000;
    tmp = buf[1] << 16;
    scr[0] += tmp & 0x00FF0000;
    tmp = buf[2] << 8;
    scr[0] += tmp & 0x0000FF00;
    tmp = buf[3];
    scr[0] += tmp & 0x000000FF;

    tmp = buf[4] << 24;
    scr[1] += tmp & 0xFF000000;
    tmp = buf[5] << 16;
    scr[1] += tmp & 0x00FF0000;
    tmp = buf[6] << 8;
    scr[1] += tmp & 0x0000FF00;
    tmp = buf[7];
    scr[1] += tmp & 0x000000FF;

	return 0;
}

/*!
 *  SDSwitch - send command: switch.
 */
int SDSwitch(struct mmc_card *card, int mode, int group, byte value, byte *resp)
{
	struct mmc_request mrq;
	struct mmc_command cmd;
	struct mmc_data data;
	struct scatterlist sg;

    MMCTraceM(("    >>> SDSwitch\n"));

	mode = !!mode;
	value &= 0xF;

	rtfs_memset(&mrq, 0, sizeof(struct mmc_request));
	rtfs_memset(&cmd, 0, sizeof(struct mmc_command));
	rtfs_memset(&data, 0, sizeof(struct mmc_data));

	mrq.cmd = &cmd;
	mrq.data = &data;

	cmd.opcode = MMCSD_SWITCH;
	cmd.arg = mode << 31 | 0x00FFFFFF;
	cmd.arg &= ~(0xF << (group * 4));
	cmd.arg |= value << (group * 4);
	cmd.flags = MMC_RSP_R1 | MMC_CMD_ADTC;

	data.blksz = 64;
	data.blocks = 1;
	data.flags = MMC_DATA_READ;
	data.sg = &sg;
	data.sg_len = 1;

	sg_init_one(&sg, resp, 64);

    MMCSDSetDataTimeout(&data, card, 0);

	MMCSDWaitForRequest(card->host, &mrq, NULL);

	if (cmd.error)
		return cmd.error;
	if (data.error)
		return data.error;

	return 0;
}

/*!
 * SDInitCard - detection and initialisation of a SD card.
 */
static int SDInitCard(struct mmc_host *host, dword ocr,	struct mmc_card *oldcard)
{
	struct mmc_card *card;
	int err;
	dword cid[4];
	unsigned int max_dtr;

	MMCGoIdle(host);

	err = SDSendIfCond(host, ocr);
	if (!err)
		ocr |= 1 << 30;

	err = SDSendOpCond(host, ocr, NULL);
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

		card->type = MMC_TYPE_SD;
		memcpy(card->raw_cid, cid, sizeof(card->raw_cid));
	}

	err = SDSendRelativeAddr(host, &card->rca);
	if (err)
		goto free_card;

	if (!oldcard)
    {
		err = MMCSendCSD(card, card->raw_csd);
		if (err)
			goto free_card;

		err = SDDecodeCSD(card);
		if (err)
			goto free_card;

		SDDecodeCID(card);
	}

    err = MMCSelectCard(card->host, card);
	if (err)
		goto free_card;

	if (!oldcard)
    {
		err = SDSendSCR(card, card->raw_scr);
		if (err)
			goto free_card;

		err = SDDecodeCSR(card);
		if (err < 0)
			goto free_card;

		err = SDReadSwitch(card);
		if (err)
			goto free_card;
	}

	err = SDSwitchHS(card);
	if (err)
		goto free_card;

	max_dtr = (unsigned int)-1;

	if (card->state & MMC_STATE_HIGHSPEED)
    {
        if (max_dtr > card->hs_max_dtr)
			max_dtr = card->hs_max_dtr;
	} else if (max_dtr > card->csd.max_dtr)
    {
		max_dtr = card->csd.max_dtr;
	}

	MMCSDSetClock(host, max_dtr);

	if ((host->caps & MMC_CAP_4_BIT_DATA) && (card->scr.bus_widths & SD_SCR_BUS_WIDTH_4))
    {
		err = SDAppSetBusWidth(card, MMC_BUS_WIDTH_4);
		if (err)
			goto free_card;

		MMCSDSetBusWidth(host, MMC_BUS_WIDTH_4);
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
 * SDRemove - free up the current card.
 */
static void SDRemove(struct mmc_host *host)
{
    MMCTraceM(("    >>> SDRemove\n"));
	MMCSDRemoveCard(host->card);
	host->card = NULL;
}

/*!
 * SDDetect - card detection callback from host.
 */
static void SDDetect(struct mmc_host *host)
{
	int err;

    MMCTraceM(("    >>> SDDetect\n"));

	MMCSDClaimHost(host);

	err = MMCSendStatus(host->card, NULL);

	MMCSDReleaseHost(host);

	if (err)
    {
		SDRemove(host);

		MMCSDClaimHost(host);
		MMCDetachBus(host);
		MMCSDReleaseHost(host);
	}
}

//static const struct mmc_bus_ops mmc_sd_ops =
//{
//	.remove = SDRemove,
//	.detect = SDDetect,
//};

/*!
 * SD_TEST_RW - test read/write function. Only for debug.
 */
#define INCLUDETESTRDWR 0
#if (INCLUDETESTRDWR)
static int SD_TEST_RW (struct mmc_host *host)
{
    int i;
    char* ext_buf;

    MMCSDClaimHost(host);

    ext_buf = kmalloc(512, GFP_KERNEL);
	if (!ext_buf)
    {
		MMCTraceM(("    >>> could not allocate a buffer to receive the ext_csd.\n"));
        return -1;
	}

    MMCSDReadWriteData(host->card, ext_buf, 0, 0);   // Test read sector 0

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

    for (i = 0; i<=511; i++)
    {
        ext_buf[i]++;
    }
    MMCSDReadWriteData(host->card, ext_buf, 1, 0);   // Test write sector 0

    MMCSDReadWriteData(host->card, ext_buf, 0, 0);   // Test read sector 0

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

    MMCSDReadWriteData(host->card, ext_buf, 0, 5120); // Test read sector 10

    MMCTraceM(("    >>> Read sector 10\n"));
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
#endif /*  (INCLUDETESTRDWR)  */
/*!
 * MMCAttach - starting point for SD card init.
 */
int SDAttach(struct mmc_host *host, dword ocr)
{
	int err;

    MMCTraceM(("    >>> SDAttach\n"));
//	MMCSDAttachBus(host, &mmc_sd_ops);

	if (ocr & 0x7F)
    {
		MMCTraceM(("    >>> card claims to support voltages below the defined range. These will be ignored\n"));
		ocr &= ~0x7F;
	}

	if (ocr & MMC_VDD_165_195)
    {
		MMCTraceM(("    >>> SD card claims to support the incompletely defined 'low voltage range'. This will be ignored\n"));
		ocr &= ~MMC_VDD_165_195;
	}

	host->ocr = MMCSDSelectVoltage(host, ocr);

    if (!host->ocr)
    {
		err = -EINVAL;
		goto err;
	}

	err = SDInitCard(host, host->ocr, NULL);
	if (err)
		goto err;

	MMCSDReleaseHost(host);

	err = MMCSDAddCard(host->card);
	if (err)
		goto remove_card;

    mmc_glob = host;

    //SD_TEST_RW(host);  // Test RW function

    return 0;

remove_card:
	MMCSDRemoveCard(host->card);
	host->card = NULL;
	MMCSDClaimHost(host);
err:
	MMCDetachBus(host);
	MMCSDReleaseHost(host);

	MMCTraceM(("    >>> error %d whilst initialising SD card\n", err));

	return err;
}
#endif
