/*
 */

#ifndef MMCSD_CORE_H
#define MMCSD_CORE_H


#define MMC_CMD_RETRIES        3

struct mmc_card;
struct device_attribute;
struct mmc_host;
struct mmc_data;
struct mmc_request;
struct mmc_command;

struct mmc_bus_ops
{
	void (*remove)(struct mmc_host *);
	void (*detect)(struct mmc_host *);
};

struct MCI_data
{
    char *data;
    int len;
};

void MMCSDSetDataTimeout(struct mmc_data *, const struct mmc_card *, int);
void MMCSDAttachBus(struct mmc_host *, const struct mmc_bus_ops *);
void MMCDetachBus(struct mmc_host *);
void MMCSDSetClock(struct mmc_host *, unsigned int);
void MMCSDSetBusWidth(struct mmc_host *, unsigned int);
dword MMCSDSelectVoltage(struct mmc_host *, dword);
void MMCSDWaitForRequest(struct mmc_host *, struct mmc_request *, struct MCI_data *);
int MMCSDWaitForCmd(struct mmc_host *, struct mmc_command *, int);
int MMCSDClaimHost(struct mmc_host *);
void MMCSDReleaseHost(struct mmc_host *);
struct mmc_card *MMCSDAllocCard(struct mmc_host *);
int MMCSDAddCard(struct mmc_card *);
void MMCSDRemoveCard(struct mmc_card *);
void MMCSDRequestDone(struct mmc_host *, struct mmc_request *);
struct mmc_host *MMCSDAllocHost(int, struct device *);
int MMCSDAddHost(struct mmc_host *);
void MMCSDRemoveHost(struct mmc_host *);
void MMCSDDetectChange(struct mmc_host *, unsigned long);

#endif  /* MMCSD_CORE_H */
