/*
 */

#include "rtfs.h"
#if (0)

#include <linux/err.h>
#include <linux/scatterlist.h>
#include <linux/device.h>
#include <linux/idr.h>
#include <linux/pagemap.h>

#include "core.h"
#include "card.h"
#include "host.h"
#include "mmc.h"
#include "sd.h"
#include "oswrp.h"
#include "lpc2478_mci.h"

#endif

#if (INCLUDE_SDCARD)

#include "rtfssdmmc.h"

extern int gCardInserted;   // 1 - card present, 0 - card not present
static struct workqueue_struct *workqueue;

/*!
 * Read/Write data SD/MMC function. 1 block only.
 */
int MMCSDReadWriteData(struct mmc_card *card, char *ext_buf, int rw, int sector)
{
	struct mmc_request mrq;
	struct mmc_command cmd;
	struct mmc_data data;
	struct scatterlist sg;
    struct MCI_data mci_data;

    MMCTraceM(("    >> MMCSDReadWriteData\n"));

	memset(&mrq, 0, sizeof(struct mmc_request));
	memset(&cmd, 0, sizeof(struct mmc_command));
	memset(&data, 0, sizeof(struct mmc_data));
    memset(&mci_data, 0, sizeof(struct MCI_data));

    if (rw == 0)        // Read 1 block
    {
        cmd.opcode = MMCSD_READ_SINGLE_BLOCK;
        cmd.arg = sector;
        cmd.flags = MMC_RSP_R1 | MMC_CMD_ADTC;

        data.blksz = 512;
        data.blocks = 1;
        data.flags |= MMC_DATA_READ;
    } else if (rw == 1) // Write 1 block
    {
        cmd.opcode = MMCSD_WRITE_BLOCK;
        cmd.arg = sector;
        cmd.flags = MMC_RSP_R1 | MMC_CMD_ADTC;

        data.blksz = 512;
        data.blocks = 1;
        data.flags |= MMC_DATA_WRITE;
    }

	mrq.cmd = &cmd;
	mrq.data = &data;

    mci_data.data = ext_buf;
    mci_data.len = data.blksz;  //512

	data.sg = &sg;
	data.sg_len = 1;

    sg_init_one(&sg, ext_buf, 512);

    MMCSDSetDataTimeout(&data, card, rw);

	MMCSDWaitForRequest(card->host, &mrq, &mci_data);

	if (cmd.error)
    {
        PRINTK(("cmd.error = %d\n", cmd.error));
        return cmd.error;
    }
	if (data.error)
    {
        PRINTK(("data.error = %d\n", data.error));
        return data.error;
    }

	return 0;
}

/*!
 *	MMCSDRequestDone - finish processing an MMCSD request.
 */
void MMCSDRequestDone(struct mmc_host *host, struct mmc_request *mrq)
{
	struct mmc_command *cmd = mrq->cmd;
	int err = cmd->error;

	if (err && cmd->retries)
    {
        MMCTraceM(("    >> req failed (CMD%u): %d, retrying...\n", cmd->opcode, err));

		cmd->retries--;
		cmd->error = 0;
        MCIRequest(host, mrq, NULL);
	} else
    {
        MMCTraceM(("    >> req done (CMD%u): %d: %08x %08x %08x %08x\n",
			cmd->opcode, err, cmd->resp[0], cmd->resp[1],	cmd->resp[2], cmd->resp[3]));

		if (mrq->data)
        {
            MMCTraceM(("    >> %d bytes transferred: %d\n",
                mrq->data->bytes_xfered, mrq->data->error));
		}

		if (mrq->stop)
        {
            MMCTraceM(("    >> (CMD%u): %d: %08x %08x %08x %08x\n",
				mrq->stop->opcode, mrq->stop->error,
				mrq->stop->resp[0], mrq->stop->resp[1], mrq->stop->resp[2], mrq->stop->resp[3]));
		}

		if (mrq->done)
        {
            mrq->done(mrq);
        }
	}
}

/*!
 *	MMCSDSetDataTimeout - set the timeout for a data command
 */
void MMCSDSetDataTimeout(struct mmc_data *data, const struct mmc_card *card, int write)
{
	unsigned int mult;

    MMCTraceM(("    >> MMCSDSetDataTimeout\n"));

	mult = (card->type == MMC_TYPE_SD) ? 100 : 10;

	if (write)
    {
        mult <<= card->csd.r2w_factor;
    }

	data->timeout_ns = card->csd.tacc_ns * mult;
	data->timeout_clks = card->csd.tacc_clks * mult;

	if (card->type == MMC_TYPE_SD)
    {
		unsigned int timeout_us, limit_us;

		timeout_us = data->timeout_ns / 1000;
		timeout_us += data->timeout_clks * 1000 / (card->host->ios.clock / 1000);

		if (write)
        {
            limit_us = 250000;
        } else
        {
            limit_us = 100000;
        }

		if (timeout_us > limit_us || (card->state & MMC_STATE_BLOCKADDR))
        {
			data->timeout_ns = limit_us * 1000;
			data->timeout_clks = 0;
		}
	}
}

/*!
 *	MMCSDStartRequest - start processing an MMCSD request.
 */
static void MMCSDStartRequest(struct mmc_host *host, struct mmc_request *mrq, struct MCI_data *mci_data)
{
    MMCTraceM(("    >> starting CMD%u arg %08x flags %08x\n", mrq->cmd->opcode, mrq->cmd->arg, mrq->cmd->flags));

	if (mrq->data)
    {
        MMCTraceM(("    >> blksz %d blocks %d flags %08x tsac %d ms nsac %d\n",
			mrq->data->blksz, mrq->data->blocks, mrq->data->flags,
			mrq->data->timeout_ns / 1000000, mrq->data->timeout_clks));
	}

	if (mrq->stop)
    {
        MMCTraceM(("    >> CMD%u arg %08x flags %08x\n", mrq->stop->opcode, mrq->stop->arg, mrq->stop->flags));
	}

	mrq->cmd->error = 0;
	mrq->cmd->mrq = mrq;
	if (mrq->data)
    {
		mrq->cmd->data = mrq->data;
		mrq->data->error = 0;
		mrq->data->mrq = mrq;
		if (mrq->stop)
        {
			mrq->data->stop = mrq->stop;
			mrq->stop->error = 0;
			mrq->stop->mrq = mrq;
		}
	}

    MCIRequest(host, mrq, mci_data);
}

/*!
 *	MMCSDWaitDone - request completion.
 */
static void MMCSDWaitDone(struct mmc_request *mrq)
{
	complete(mrq->done_data);
}

/*!
 *	MMCSDWaitForRequest - start a request and wait for completion.
 */
void MMCSDWaitForRequest(struct mmc_host *host, struct mmc_request *mrq, struct MCI_data *mci_data)
{
	DECLARE_COMPLETION_ONSTACK(complete);

	mrq->done_data = &complete;
	mrq->done = MMCSDWaitDone;

	MMCSDStartRequest(host, mrq, mci_data);

	wait_for_completion(&complete);
}

/*!
 *	MMCSDWaitForCmd - start a command and wait for completion.
 */
int MMCSDWaitForCmd(struct mmc_host *host, struct mmc_command *cmd, int retries)
{
	struct mmc_request mrq;

    MMCTraceM(("    >> MMCSDWaitForCmd\n"));

	memset(&mrq, 0, sizeof(struct mmc_request));

	memset(cmd->resp, 0, sizeof(cmd->resp));
	cmd->retries = retries;

	mrq.cmd = cmd;
	cmd->data = NULL;

	MMCSDWaitForRequest(host, &mrq, NULL);

	return cmd->error;
}



/*!
 * MMCSDSetIOS - ios call to the host driver.
 */
static inline void MMCSDSetIOS(struct mmc_host *host)
{
	struct mmc_ios *ios = &host->ios;

    MMCTraceM(("    >> clock %uHz powermode %u Vdd %u width %u\n",
		 ios->clock, ios->power_mode, ios->vdd, ios->bus_width));

    MCISetCLKPWR(host, ios);

}

/*!
 * MMCSDSetClock - sets the host clock.
 */
void MMCSDSetClock(struct mmc_host *host, unsigned int hz)
{
    MMCTraceM(("    >> MMCSDSetClock\n"));

	if (hz > host->f_max)
    {
        hz = host->f_max;
    }

	host->ios.clock = hz;
	MMCSDSetIOS(host);
}


/*!
 * MMCSDSetBusWidth - change data bus width of a host.
 */
void MMCSDSetBusWidth(struct mmc_host *host, unsigned int width)
{
    MMCTraceM(("    >> MMCSDSetBusWidth\n"));
	host->ios.bus_width = width;
	MMCSDSetIOS(host);
}

/*!
 * MMCSDSelectVoltage -  select need voltage.
 */
dword MMCSDSelectVoltage(struct mmc_host *host, dword ocr)
{
	int bit;

    MMCTraceM(("    >> MMCSDSelectVoltage\n"));

	ocr &= host->ocr_avail;

	bit = ffs(ocr);

	if (bit)
    {
		bit -= 1;
		ocr &= 3 << bit;
		host->ios.vdd = bit;
		MMCSDSetIOS(host);
	} else
    {
		ocr = 0;
	}

	return ocr;
}

/*!
 * MMCSDPowerUP - power up MMC/SD card.
 */
static void MMCSDPowerUP(struct mmc_host *host)
{
	int bit = fls(host->ocr_avail) - 1;

    MMCTraceM(("    >> MMCSDPowerUP\n"));

	host->ios.vdd = bit;
	host->ios.power_mode = MMC_POWER_UP;
	host->ios.bus_width = MMC_BUS_WIDTH_1;
	MMCSDSetIOS(host);

    msleep(1);

	host->ios.clock = host->f_min;
	host->ios.power_mode = MMC_POWER_ON;
	MMCSDSetIOS(host);

    msleep(2);
}

/*!
 * MMCSDPowerOFF - power off MMC/SD card.
 */
static void MMCSDPowerOFF(struct mmc_host *host)
{
    MMCTraceM(("    >> MMCSDPowerOFF\n"));

	host->ios.clock = 0;
	host->ios.vdd = 0;
	host->ios.power_mode = MMC_POWER_OFF;
	host->ios.bus_width = MMC_BUS_WIDTH_1;
	MMCSDSetIOS(host);
}

/*!
 * MMCSDBusGet - increase reference count of bus operator.
 */
static inline void MMCSDBusGet(struct mmc_host *host)
{
	unsigned long flags;

	spin_lock_irqsave(&host->lock, flags);
	host->bus_refs++;
	spin_unlock_irqrestore(&host->lock, flags);
}

/*!
 * MMCSDBusPut - decrease reference count of bus operator.
 */
static inline void MMCSDBusPut(struct mmc_host *host)
{
	unsigned long flags;

	spin_lock_irqsave(&host->lock, flags);
	host->bus_refs--;
	if ((host->bus_refs == 0) && host->bus_ops)
    {
        host->bus_ops = NULL;
    }
	spin_unlock_irqrestore(&host->lock, flags);
}

/*!
 * MMCSDAttachBus - assign a mmc bus handler to a host.
 */
void MMCSDAttachBus(struct mmc_host *host, const struct mmc_bus_ops *ops)
{
	unsigned long flags;

	spin_lock_irqsave(&host->lock, flags);

	host->bus_ops = ops;
	host->bus_refs = 1;
	host->bus_dead = 0;

	spin_unlock_irqrestore(&host->lock, flags);
}

/*!
 * MMCDetachBus - remove the current bus handler from a host.
 */
void MMCDetachBus(struct mmc_host *host)
{
	unsigned long flags;

	spin_lock_irqsave(&host->lock, flags);

	host->bus_dead = 1;

	spin_unlock_irqrestore(&host->lock, flags);

	MMCSDPowerOFF(host);

	MMCSDBusPut(host);
}

/*!
 *	MMCSDDetectChange - process change of state on a MMCSD.
 */
void MMCSDDetectChange(struct mmc_host *host, unsigned long delay)
{
    MMCTraceM(("    >> MMCSDDetectChange\n"));

    queue_delayed_work(workqueue, &host->detect, delay);
}

/*!
 *	MMCSDRescan - rescan of state on a MMCSD.
 */
void MMCSDRescan(struct work_struct *work)
{
	struct mmc_host *host =	container_of(work, struct mmc_host, detect.work);
	dword ocr;
	int err;

    MMCTraceM(("    >> MMCSDRescan\n"));

	MMCSDBusGet(host);

	if (host->bus_ops == NULL)
    {
		MMCSDBusPut(host);
		MMCSDClaimHost(host);
		MMCSDPowerUP(host);
		MMCGoIdle(host);
		SDSendIfCond(host, host->ocr_avail);

		/*
		 * First we search for SD
		 */
		err = SDSendOpCond(host, 0, &ocr);
		if (!err)
        {
			if (SDAttach(host, ocr))
            {
                MMCSDPowerOFF(host);
            }
			return;
		}

		/*
		 * search for MMC.
		 */
		err = MMCSendOPCond(host, 0, &ocr);
		if (!err)
        {
			if (MMCAttach(host, ocr))
            {
                MMCSDPowerOFF(host);
            }
			return;
		}

		MMCSDReleaseHost(host);
		MMCSDPowerOFF(host);
	} else
    {
		if (host->bus_ops->detect && !host->bus_dead)
        {
            host->bus_ops->detect(host);
        }

		MMCSDBusPut(host);
	}
}

static void MMCSDHostClassdevRelease(struct device *dev)
{
    struct mmc_host *host = container_of(dev, struct mmc_host, class_dev);
	kfree(host);
}

//static struct class mmc_host_class =
//{
//	.name		    = "mmc_host",
//	.dev_release	= MMCSDHostClassdevRelease,
//};

//static DEFINE_IDR(mmc_host_idr);
//static DEFINE_SPINLOCK(mmc_host_lock);

/*!
 *	MMCSDAllocHost - initialise the per-host structure.
 */
struct mmc_host *MMCSDAllocHost(int extra, struct device *dev)
{
	struct mmc_host *host;

	host = (struct mmc_host *) kmalloc(sizeof(struct mmc_host) + extra, GFP_KERNEL);
	if (!host)
    {
        return NULL;
    }

	rtfs_memset(host, 0, sizeof(struct mmc_host) + extra);
// PVO removed.
//	host->parent = dev;
//	host->class_dev.parent = dev;
//	host->class_dev.class = &mmc_host_class;
//	device_initialize(&host->class_dev);

//	spin_lock_init(&host->lock);
//	init_waitqueue_head(&host->wq);
//	INIT_DELAYED_WORK(&host->detect, MMCSDRescan);

    return host;
}

/*!
 *	MMCSDAddHost - initialise host hardware.
 */
int MMCSDAddHost(struct mmc_host *host)
{
	int err;

    MMCTraceM(("    >> MMCSDAddHost\n"));
// PVO removed
//	if (!idr_pre_get(&mmc_host_idr, GFP_KERNEL))
//    {
//        return -ENOMEM;
//    }
//
//	spin_lock(&mmc_host_lock);
//	err = idr_get_new(&mmc_host_idr, host, &host->index);
//	spin_unlock(&mmc_host_lock);
//	if (err)
//    {
//        return err;
//    }
//
//	snprintf(host->class_dev.bus_id, BUS_ID_SIZE, "mmc%d", host->index);
//
//	err = device_add(&host->class_dev);
//	if (err)
//    {
//        return err;
//    }

    MMCSDPowerOFF(host);
	MMCSDDetectChange(host, 0);

	return 0;
}

/*!
 *	MMCSDRemoveHost - remove host hardware.
 */
void MMCSDRemoveHost(struct mmc_host *host)     // Call from host driver in release function
{
// PVO removed
//    flush_workqueue(workqueue);
//
//	MMCSDBusGet(host);
//	if (host->bus_ops && !host->bus_dead)
//    {
//		if (host->bus_ops->remove)
//        {
//            host->bus_ops->remove(host);
//        }
//
//		MMCSDClaimHost(host);
//		MMCDetachBus(host);
//		MMCSDReleaseHost(host);
//	}
	MMCSDBusPut(host);

	MMCSDPowerOFF(host);

//	device_del(&host->class_dev);
//
//	spin_lock(&mmc_host_lock);
//	idr_remove(&mmc_host_idr, host->index);
//	spin_unlock(&mmc_host_lock);
}

static void MMCSDReleaseCard(struct device *dev)
{
    struct mmc_card *card = container_of(dev, struct mmc_card, dev);
	kfree(card);
}

/*!
 * MMCSDAllocCard - allocate and initialise a new MMC/SD card structure.
 */
struct mmc_card *MMCSDAllocCard(struct mmc_host *host)
{
	struct mmc_card *card;

	card = (struct mmc_card *) kmalloc(sizeof(struct mmc_card), GFP_KERNEL);
	if (!card)
    {
        return (0);
    }

	memset(card, 0, sizeof(struct mmc_card));

	card->host = host;
// PVO removed
//  device_initialize(&card->dev);

//    card->dev.parent = &host->class_dev;
//    card->dev.bus = NULL;
//	card->dev.release = MMCSDReleaseCard;

	return card;
}

/*!
 * MMCSDAddCard - register a new MMC/SD card with the driver model.
 */
int MMCSDAddCard(struct mmc_card *card)
{
	int ret;
	const char *type;

    MMCTraceM(("    >> MMCSDAddCard\n"));

    gCardInserted = 1;
// PVO removed
// snprintf(card->dev.bus_id, sizeof(card->dev.bus_id), "%s:%04x", card->host->class_dev.bus_id, card->rca);

	switch (card->type)
    {
    	case MMC_TYPE_MMC:
    		type = "MMC";
    		break;
    	case MMC_TYPE_SD:
    		type = "SD";
    		if (card->state & MMC_STATE_BLOCKADDR)
            {
                type = "SDHC";
            }
    		break;
    	default:
    		type = "?";
    		break;
	}

    PRINTK((KERN_INFO "New %s%s card at address %04x\n", (card->state & MMC_STATE_HIGHSPEED) ? "high speed " : "", type, card->rca));
// PVO remove
//	card->dev.uevent_suppress = 1;
//	ret = device_add(&card->dev);
//	if (ret)
//   {
//        return ret;
//    }
//	card->dev.uevent_suppress = 0;

//	kobject_uevent(&card->dev.kobj, KOBJ_ADD);

	card->state |= MMC_STATE_PRESENT;

	return 0;
}

/*!
 * MMCSDRemoveCard - unregister a new MMC/SD card and free it.
 */
void MMCSDRemoveCard(struct mmc_card *card)
{
	if (card->state & MMC_STATE_PRESENT)
    {
		MMCTraceM(("card %04x removed\n", card->rca));
// PVO remove
//		device_del(&card->dev);
	}

// PVO remove
//	put_device(&card->dev);
}


#endif
