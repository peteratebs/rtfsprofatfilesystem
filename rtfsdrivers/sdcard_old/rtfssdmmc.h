/*
 */

#ifndef RTFSSDMMC_H
#define RTFSSDMMC_H

#define MMCTraceM(X) 0
#define PRINTK(X) 0
#define GFP_KERNEL 0
#define EINVAL  1
#define EIO		2
#define ENOMEM	3
#define EOPNOTSUPP 4
#define ETIMEDOUT 5
#define ENOENT 6

#define kmalloc(A, B) rtfs_port_malloc(A)
#define kfree(A) rtfs_port_free(A)

#define msleep(X)
#define complete(X)
void wait_for_completion(int *);
#define atomic_t dword
int fls(dword a);
int ffs(dword a);
#define queue_delayed_work(A, B, C)
#define container_of(A,B,C) 0


#define spin_lock_irqsave(A, B)
#define spin_unlock_irqrestore(A, b)

int MMCSDClaimHost(struct mmc_host *host);
void MMCSDReleaseHost(struct mmc_host *host);
void MCISetCLKPWR(struct mmc_host *host, struct mmc_ios *ios);


#define DECLARE_COMPLETION_ONSTACK(A) int A
#define DECLARE_WAITQUEUE(A, B) int A,B

void MCIRequest(struct mmc_host *mmc, struct mmc_request *mrq, struct MCI_data *mci_data);


#define memcmp(A,B,C) 0
#define memcpy(a,b,c) copybuff((a),(b),(c));
#define memset(a,b,c) rtfs_memset((a),(b),(c));

#define sg_init_one(A,B,C)
#define IS_ERR(A) 0
#define PTR_ERR(A) 0

struct device

{
	int xxx;
};
struct delayed_work
{
	int xxx;
};
struct scatterlist {
	int xxx;
};
#define spinlock_t int
typedef struct wait_queue_head_s
{
	int xxx;
} wait_queue_head_t;

typedef struct MMCSDParams_s
{
    dword    SectorSize;
    dword    SectorNumber;
    byte     MMCSDVendorInfo[16];
    byte     RMBInfo;
} MMCSDParams_t;
#include "card.h"
#include "host.h"
#include "core.h"
#include "mmc.h"
#include "sd.h"


BOOLEAN MMCSDLLDriverOpen(void);
BOOLEAN  MMCSDLLDriverGetInc(MMCSDParams_t *Params);
BOOLEAN  MMCSDLLDriverGetStatus(dword *pstatus);
BOOLEAN  MMCSDLLDriverIO(dword curBlock, byte *buffer, dword sectornum, dword numsectors, BOOLEAN reading);



#define UNSTUFF_BITS(A,B,C) (dword) 0xffffffff

#endif  /* RTFSSDMMC_H */
