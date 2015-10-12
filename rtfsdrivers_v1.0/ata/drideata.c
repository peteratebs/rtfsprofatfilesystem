/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*
*
*/
/* drideata.c - Portable IDE/ATA/Compact flash device driver.
*
*
*/

#include "rtfs.h"
#include "portconf.h"   /* For included devices */

#if (INCLUDE_IDE)
/* Defines for ide_drv.c: To get to where the device driver code begins
please go to the function ide_in_words(word nwords) */

/* Don't allow more then 128 blocks (64K) for fear of a segment wrap. */
#define MAX_BLOCKS  128

/* Function prototypes */
 /* Timeout values - These indicate how long we will wait until deciding
    that an operation has failed. Set these high if you are not worried about
    bus latches. Values are in ticks */
#define TIMEOUT_RDWR  (4000)      /*  4 second for a multiblock read or write
                                                    transfer to take. This is from start to finish.
                                                    We can't diddle the timer on each interrupt of
                                                    a multiblock transfer */
#define TIMEOUT_DIAG    (12000)   /* 12 seconds maximum to complete diagnostics */
#define TIMEOUT_RESET   (12000)   /* 12 seconds maximum to complete reset */
#define TIMEOUT_TYPICAL (12000)   /* 12 seconds maximum for other commands not listed*/
/*  #define TIMEOUT_ATAPI_SPINUP (24000) */  /* 24 seconds maximum for other commands not listed*/
/* Reduce the ATAPI Timeout. Test UNIT ready always fails initially,
   twice. With a 24 second timeout it takes forever. A 2 second timeout
   works and it boots faster. */
#define TIMEOUT_ATAPI_SPINUP (2000)   /* 2 seconds maximum for other commands not listed*/


/* Error codes. One  will be in c_s.error_code if an ide function fails. */
#define IDE_ERC_DIAG        1   /* Drive diagnostic failed in initialize */
#define IDE_ERC_ARGS        2   /* User supplied invalid arguments */
#define IDE_ERC_BUS         3   /* DRQ should be asserted but it isn't
                                    or driver and controller are out of phase*/
#define IDE_ERC_TIMEOUT     4   /* Timeout during some operation */
#define IDE_ERC_STATUS      5   /* Controller reported an error look in the error register */
#define IDE_ERC_BAD_CMD     6   /* Drive does't support command */

/* Register file address offsets */
/* offsets for read ops and for write ops (where write==read) */
#define IDE_OFF_DATA                0
#define IDE_OFF_ERROR               1
#define IDE_OFF_FEATURE             1
#define IDE_OFF_SECTOR_COUNT        2
#define IDE_OFF_SECTOR_NUMBER       3
#define IDE_OFF_CYL_LOW             4
#define IDE_OFF_CYL_HIGH            5
#define IDE_OFF_DRIVE_HEAD          6
#define IDE_OFF_STATUS              7
#if (USE_CONTIG_IO)
#define IDE_OFF_ALT_STATUS          0xe
#define IDE_OFF_DRIVE_ADDRESS       0xf
#else
#define IDE_OFF_ALT_STATUS          0x206
#define IDE_OFF_DRIVE_ADDRESS       0x207
#endif
/* offsets for write ops when register usage differs from read. */
#define IDE_OFF_PRECOMP     IDE_OFF_ERROR
#define IDE_OFF_DIG_OUTPUT  IDE_OFF_ALT_STATUS
#define IDE_OFF_COMMAND     IDE_OFF_STATUS

/* Commands: These are placed in the command register to start an operation.
            The values put into the other registers are context dependent */

#define IDE_LONG_MASK                   0x02    /* Mask in to read/write ECC on
                                                CMD_IDE_READS, CMD_IDE_WRITES */
#define IDE_RETRY_MASK                  0x01    /* Mask in to enable drive retries
                                                CMD_IDE_READS, CMD_IDE_WRITES
                                                CMD_IDE_READV */
#define IDE_CMD_READS                   0x20    /* Read sector(s) */
#define IDE_CMD_READS_EXT               0x24    /* Read sector(s) 48-bit */
#define IDE_CMD_DMA_READ_EXT            0x25    /* DMA Read (Ultra DMA) 48-bit */
#define IDE_CMD_READM_EXT               0x29    /* Read multiple sectors/interrupt 48-bit */
#define IDE_CMD_WRITES                  0x30    /* Write sector(s) */
#define IDE_CMD_WRITES_EXT              0x34    /* Write sector(s) 48-bit */
#define IDE_CMD_DMA_WRITE_EXT           0x35    /* DMA Write (Ultra DMA) 48-bit */
#define IDE_CMD_WRITES_NE               0x38    /* Write sector(s) No pre-erase */
#define IDE_CMD_WRITEM_EXT              0x39    /* Write multiple sectors/interrupt 48-bit */
#define IDE_CMD_ERASES                  0xC0    /* Erase sector(s) */
#define IDE_CMD_DIAG                    0x90    /* Initiate drive diagnostics */
#define IDE_CMD_INITP                   0x91    /* Initialize drive parameters */
#define IDE_CMD_READM                   0xC4    /* Read multiple sectors/interrupt */
#define IDE_CMD_WRITEM                  0xC5    /* Write multiple sectors/interrupt */
#define IDE_CMD_WRITEM_NE               0xCD    /* Write multiple No pre-erase */
#define IDE_CMD_SETM                    0xC6    /* Set multiple mode */
#define IDE_CMD_IDENT                   0xEC    /* Return drive parameters */
#define IDE_CMD_SETF                    0xEF    /* Set Features */
#define IDE_CMD_GET_MEDIA_STATUS        0xDA    /* GET THE MEDIA STATUS */
#define IDE_CMD_SOFT_RESET              0x08    /* Soft Reset */
#define IDE_CMD_DMA_READ                0xC8    /* DMA Read (Ultra DMA) */
#define IDE_CMD_DMA_WRITE               0xCA    /* DMA Write (Ultra DMA) */

#define ATAPI_CMD_IDENT                 0xA1            /* Return drive parameters */
#define ATAPI_CMD_PKT                   0xA0            /* ATAPI Packet command */
#define ATAPI_CMD_GETMS                 0xDA            /* ATAPI GET MEDIA STATUS */
#define ATAPI_CMD_SERVICE               0xA2            /* ATAPI SERVICE */

#define ATAPI_PKT_CMD_READ              0x28    /* ATAPI PACKET READ COMMAND */
#define ATAPI_PKT_CMD_WRITE             0x2A    /* ATAPI PACKET WRITE COMMAND */
#define ATAPI_PKT_CMD_MODE_SENSE        0x5A    /* ATAPI MODE SENSE */
#define ATAPI_PKT_CMD_TEST_UNIT_READY   0x00    /* ATAPI TEST UNIT READY */
#define ATAPI_PKT_CMD_FORMAT_UNIT       0x04    /* ATAPI FORMAT UNIT */
#define ATAPI_PKT_CMD_OLD_FORMAT_UNIT   0x24    /* ATAPI FORMAT UNIT */
#define ATAPI_PKT_CMD_REQUEST_SENSE     0x03    /* ATAPI REQUEST SENSE */
#define ATAPI_PKT_CMD_START_STOP_UNIT   0x1B    /* Start Stop eject */
#define ATAPI_PKT_CMD_RDVTOC            0x43    /* Rd Audio VTOC */
#define ATAPI_PKT_CMD_RDAUDIO           0xBE    /* Rd Audio track */


#define IDE_FEATURE_SETPERF             0x9A     /* Set Perfomance Option */
#define IDE_FEATURE_WCACHE_ON           0x02     /* Enable the write cache */
#define IDE_FEATURE_DEFECT_ON           0x04     /* Enable the defect re-assignment */
#define IDE_FEATURE_RLOOK_ON            0xAA     /* Enable read look-ahead */

#define IDE_FEATURE_SETPERF_VALUE       0xff     /* Default. Max performance (current) */

#define IDE_FEATURE_WCACHE_OFF          0x82     /* Disable the write cache */
#define IDE_FEATURE_RLOOK_OFF           0x55     /* Disable read look-ahead */
#define IDE_FEATURE_SET_TRANSFER_MODE   0x03     /* Ultra DMA */


/*                          ============                                */

/* Error bits: These bits are returned in the error register. They are valid
                only if the error bit in the status register is set. */
#define IDE_ERB_AMNF        0x01            /* Address mark not found */
#define IDE_ERB_RECAL       0x02            /* Seek failed during recal */
#define IDE_ERB_ABORT       0x04            /* Drive error (write fault etc)
                                                or invalid command code */
#define IDE_ERB_MCR         0x08            /* Media change request */
#define IDE_ERB_IDNF        0x10            /* Read/Write or Seek failed
                                                requested sector not found */
#define IDE_ERB_MC          0x20            /* Media change requested */
#define IDE_ERB_UNC         0x40            /* Uncorrectable data error */
#define IDE_ERB_BADBLK      0x80            /* Requested Block Is Bad */
/*                          ============                                */

/* Status bits: These bits are returned in the status register. If the
                busy bit is set no other bits are busy              */
#define IDE_STB_ERROR       0x01            /* Error occured: consult error reg */
#define IDE_STB_INDEX       0x02            /* Pulses as the disk rotates */
#define IDE_STB_CORRECTED   0x04            /* The drive had to use ECC on the data */
#define IDE_STB_DRQ     0x08            /* Drive will send or accept data */
#define IDE_STB_ONTRACK 0x10            /* Set when the head is on track */
#define IDE_STB_WFAULT      0x20            /* Drive write fault occured */
#define IDE_STB_READY       0x40            /* Drive is up and spinning if 1 */
#define IDE_STB_BUSY        0x80            /* Drive side cpu is busy. It owns
                                                the register file now. All register
                                                File accesses from the host side
                                                will return status register
                                                contents */
#define IDE_IRR_IO          0x02
#define IDE_IRR_CoD         0x01

/* Digital output register: Bitwise command latch */
#define IDE_OUT_IEN         0x02            /* Enable AT bus's IRQ14 line */
#define IDE_OUT_RESET       0x04            /* Hold high for 10 uSecs to
                                                reset drives */

#define HIMAWARI_FLEXIBLE_DISK_PAGE_CODE    0x05

typedef struct transfer_length
    {
    byte    msb;
    byte    lsb;
    }   TRANSFER_LENGTH;

typedef struct  himawari_flexible_disk_page
    {
    byte    page_code;                      /* should be x000 0101 */
    byte    page_length;                    /* should be 0001 1110 */
    byte    transfer_rate_msb;
    byte    transfer_rate_lsb;
    byte    number_of_heads;
    byte    sectors_per_track;
    byte    bytes_per_cylinder_msb;
    byte    bytes_per_cylinder_lsb;
    byte    number_of_cylinders_msb;
    byte    number_of_cylinders_lsb;
    byte    reserved1[10];
    byte    motor_off_delay;
    byte    swp_swpp;
    byte    reserved2[6];
    byte    medium_rotation_rate_msb;
    byte    medium_rotation_rate_lsb;
    byte    reserved3[2];
    } HIMAWARI_FLEXIBLE_DISK_PAGE;

typedef struct  Himawari_defect_list_header
    {
    byte    reserved;
    byte    control_byte;
    byte    length_msb;
    byte    length_lsb;
    }   HIMAWARI_DEFECT_LIST_HEADER;

typedef struct  Himawari_formattable_capacity_descriptor
    {
    byte    number_of_blocks[4];                            /*Big Endian format*/
    byte    reserved;
    byte    block_length[3];                                /*Big Endian format*/
    }   HIMAWARI_FORMATTABLE_CAPACITY_DESCRIPTOR;

typedef struct  Himawari_format_descriptor
    {
    HIMAWARI_DEFECT_LIST_HEADER                 dlheader;
    HIMAWARI_FORMATTABLE_CAPACITY_DESCRIPTOR    capacity_descriptor;
    }   HIMAWARI_FORMAT_DESCRIPTOR;

typedef struct  atapi_mode_sense_packet_desc
    {
    byte            op_code;
    byte            lun;
    byte            pc_code_page;
    byte            reserved1;
    byte            reserved2;
    byte            reserved3;
    byte            reserved4;
    TRANSFER_LENGTH transfer_length;                        /*Big Endian format*/
    byte            reserved5;
    byte            reserved6;
    byte            reserved7;
    }   ATAPI_MODE_SENSE_PACKET_DESC;

typedef struct  atapi_format_unit_packet_desc
    {
    byte            op_code;
    byte            defect_list_format;
    byte            track_number;
    byte            interleave[2];
    byte            reserved[7];
    }   ATAPI_FORMAT_UNIT_PACKET_DESC;

typedef struct  atapi_start_stop_packet_desc
    {
    byte            op_code;
    byte            lun;
    byte            reserved1;
    byte            reserved2;
    byte            eject;              /* Bit 1 is eject, 0 is start */
    byte            reserved[7];
    }   ATAPI_START_STOP_UNIT_PACKET_DESC;

typedef struct  atapi_old_format_unit_packet_desc
    {
    byte            op_code;
    byte            reserved1;
    byte            medium;
    byte            reserved2[3];
    byte            control_byte;
    byte            track_number;
    byte            reserved3[4];
    }   ATAPI_OLD_FORMAT_UNIT_PACKET_DESC;

typedef struct  atapi_packet_desc
    {
    byte            op_code;
    byte            lun;
    dword           lba;
    byte            reserved1;
    TRANSFER_LENGTH transfer_length;                        /*Big Endian format*/
    byte            reserved2;
    byte            reserved3;
    byte            reserved4;
    }   ATAPI_PACKET_DESC;

typedef struct  atapi_general_packet_desc
{
   byte            bytes[12];
}   ATAPI_GENERAL_PACKET_DESC;

typedef union   {
                ATAPI_PACKET_DESC                   generic;
                ATAPI_GENERAL_PACKET_DESC           general;
                ATAPI_MODE_SENSE_PACKET_DESC        mode_sense;
                ATAPI_FORMAT_UNIT_PACKET_DESC       format;
                ATAPI_OLD_FORMAT_UNIT_PACKET_DESC   old_format;
                ATAPI_START_STOP_UNIT_PACKET_DESC start_stop;
                }   ATAPI_PACKET;


/* ide_control structure - We use this abstraction to manage the IDE
    driver.
        Structure contents:
            .  drive description. logical drive structure (heads/secptrak/secpcyl)
            .  virtual registers: These aren't (can't be) mapped onto the
            the controller but we use virtual representation on the
            register file to drive the system.
            .  virtual output register file (to drive) - We load the registers
            and then ask lower level code to send the command block to the
            controller.
            .  virtual input register file (from drive) - We offload the register
            file from the drive into these fields after a command complete.
            Note: We only offload the whole register file in certain cases.
                    The status register is always offloaded.
*/

/* The control structure contains one of these per drive [2]. We set this
    up at init time after issueing SET_PARAMS. The values are used to convert
    block number to track:head:sector */
typedef struct ide_drive_desc
    {
    word                open_count;
    byte                num_heads;
    byte                sec_p_track;
    word                sec_p_cyl;
    word                num_cylinders;
    byte                max_multiple;
    BOOLEAN             supports_lba;
    BOOLEAN             supports_48bits;
    dword               total_lba;
    byte                protocol;
    ATAPI_PACKET        atapi_packet;
    word                atapi_packet_size;
    byte                media_descriptor;
    byte                medium_type_code;
    byte                allocation_unit;
    BOOLEAN             UltraDmaModeCapable;
    byte                CMD_DRQ_type;
    }   IDE_DRIVE_DESC;

typedef  struct physical_region_descriptor /* Ultra Dma */
   {
   dword base_address;
   word  byte_count;
   byte  reserved;
   byte  eot;
   } PHYSICAL_REGION_DESCRIPTOR, *PHYSICAL_REGION_DESCRIPTOR_PTR;

typedef struct ide_control
{
#define CONTROLLER_INITIALIZED     0x12344321
    dword  init_signature;               /* set to CONTROLLER_INITIALIZED in WARMSTART */
    dword register_file_address;        /* Address of the controller */
    dword ide_signal;                   /* Signal for IO completion */
#if (INCLUDE_UDMA)
    dword  bus_master_address;           /* Address of the bus master controller */
    PHYSICAL_REGION_DESCRIPTOR_PTR  dma_descriptor_base; /* Dma descriptors */
    dword  dma_descriptor_bus_address;   /* Bus address of above */
# endif /*#if   (INCLUDE_UDMA) */
    int interrupt_number;                   /* -1 if not being used */
    int controller_number;
    int contiguous_io_mode;         /* If 1 use contig IO */
    BOOLEAN command_complete;       /* Set by ISR when operation is complete */
    word error_code;            /* Set if an error occured - This is the error
                                    reported to the user */
    word timer;             /* Command fails if this much time elapsed */
    byte  block_size;           /* Block size for read/write multiple cmds */
    word sectors_remaining; /* Sectors remaining for read/write mult */
    byte * user_address;    /* C pointer address of the user's buffer */
    dword  atapi_words_to_xfer; /* Words expected in an atapi command */
    /* Geometry of the drives */
    IDE_DRIVE_DESC drive[2];

    /* Virtual output register file - we load these and then send them to
        the IDE drive. */
    byte vo_write_precomp;  /* Used to turn on read ahead on certain drives */
    word vo_sector_count;   /* Sector transfer request */
    byte vo_sector_number;  /* Sector number request    */
    byte vo_cyl_low;        /* Low byte of cylinder */
    byte vo_cyl_high;       /* High byte of cylinder    */
    byte vo_high_lba;       /* High byte of LBA */
    byte vo_drive_head; /* Drive & Head register
                                Bit 5 == 1
                                Bit 4 = Drive 0/1 == C/D,
                                bit 0-3 == head */
    byte vo_feature;        /* Feature register */
    byte vo_command;        /* command register see command patterns above (W) */
    byte vo_dig_out;        /*  Digital output register. Bits enable interrupts
                                and software reset See BIT patterns above */

    /* Virtual input register file - We read these from the drive during our
        command completion code */
    byte vi_error;          /* Error register. See IDE_ERB_XXXX above */
    byte vi_sector_count;   /* # Sectors left in request */
    byte vi_sector_number;  /* Current sector number in request  */
    byte vi_cyl_low;        /* Low byte of cylinder in request  */
    byte vi_cyl_high;       /* High byte of cylinder in request  */
    byte vi_drive_head; /* Drive & Head register in request  */
                            /* Byte 5 == 1
                                Byte 4 = Drive 1/0 == C/D,
                                bytes 0-3 == head */
    byte vi_status;     /* Status register see IDE_STB_XXXX above. Reading
                                this register sends an interrupt acknowledge
                                to the drive.  */
    byte vi_alt_status; /* Same as status register but does not clear
                                interrupt state. */
    byte vi_drive_addr; /* Bit 0 & 1 are a mask of which drive is selected
                                bits 2-5 are the head number in ones complement.
                                ~(BITS2-5) == head number */
    } IDE_CONTROLLER;

typedef IDE_CONTROLLER * PIDE_CONTROLLER;

/* Functions supplied by the porting layer */
void hook_ide_interrupt(int irq, int controller_number);
byte ide_rd_status(dword register_file_address);
word ide_rd_data(dword register_file_address);
byte ide_rd_sector_count(dword register_file_address);
byte ide_rd_alt_status(dword register_file_address,int contiguous_io_mode);
byte ide_rd_error(dword register_file_address);
byte ide_rd_sector_number(dword register_file_address);
byte ide_rd_cyl_low(dword register_file_address);
byte ide_rd_cyl_high(dword register_file_address);
byte ide_rd_drive_head(dword register_file_address);
byte ide_rd_drive_address(dword register_file_address, int contiguous_io_mode);
void ide_wr_dig_out(dword register_file_address, int contiguous_io_mode, byte value);
void ide_wr_data(dword register_file_address, word value);
void ide_wr_sector_count(dword register_file_address, byte value);
void ide_wr_sector_number(dword register_file_address, byte value);
void ide_wr_cyl_low(dword register_file_address, byte value);
void ide_wr_cyl_high(dword register_file_address, byte value);
void ide_wr_drive_head(dword register_file_address, byte value);
void ide_wr_command(dword register_file_address, byte value);
void ide_wr_feature(dword register_file_address, byte value);
byte ide_rd_udma_status(dword bus_master_address);
void ide_wr_udma_status(dword bus_master_address, byte value);
byte ide_rd_udma_command(dword bus_master_address);
void ide_wr_udma_command(dword bus_master_address, byte value);
void ide_wr_udma_address(dword bus_master_address, dword bus_address);
void ide_insw(dword register_file_address, unsigned short *p, int nwords);
void ide_outsw(dword register_file_address, unsigned short *p, int nwords);
dword rtfs_port_ide_bus_master_address(int controller_number);
BOOLEAN ide_detect_80_cable(int controller_number);
dword rtfs_port_bus_address(void * p);

/* Functions supplied by the pcmcia driver */
BOOLEAN pcmctrl_init(void);
BOOLEAN pcmcia_card_is_ata(int socket, dword register_file_address,int interrupt_number, byte pcmcia_cfg_opt_value);
BOOLEAN pcmctrl_card_installed(int socket);
BOOLEAN pcmctrl_card_changed(int socket);
void pcmctrl_card_down(int socket);

/* Function prototypes */
BOOLEAN ide_io(int driveno, dword sector, void *buffer, word count, BOOLEAN reading);
int ide_perform_device_ioctl(int driveno, int opcode, void * pargs);
void ide_interrupt(int controller_no);
BOOLEAN ide_command_identify(PIDE_CONTROLLER pc, byte * address, int phys_drive);
void ide_interrupt(int controller_no);
BOOLEAN ide_wait_ready(PIDE_CONTROLLER pc,int ticks);
BOOLEAN ide_wait_not_busy(PIDE_CONTROLLER pc,int ticks);
BOOLEAN ide_wait_drq(PIDE_CONTROLLER pc, int ticks);

BOOLEAN simple_atapi_command(byte command, PIDE_CONTROLLER pc, int phys_drive);
BOOLEAN atapi_ls120_format(int driveno);

#define N_ATA_CONTROLLERS       4  /* If INCLUDE_IDE is 1 this is the number of controllers */

#define IDE_USE_SET_FEATURES    0
/* if one use the following set features call when the drive is opened:
        IDE_FEATURE_SETPERF     Set Perfomance Option
        IDE_FEATURE_WCACHE_ON   Enable the write cache
        IDE_FEATURE_DEFECT_ON   Enable the defect re-assignment
        IDE_FEATURE_RLOOK_ON    Enable read look-ahead
        IDE_FEATURE_WCACHE_OFF  Disable write cache
        IDE_FEATURE_RLOOK_OFF   Disable read look-ahead
*/
#define IDE_USE_ATAPI           0  /* if one enable ATAPI support (for CD, LS-120, etc) */


#define ZERO 0
/* Set USE_SETPARMS to 1 to force this driver to set the drive to use its
   true geometry (cyls, hds, sec/track) when this driver initializes.
   This is only necessary if the IDE drive had already been set to use
   a translated geometry during BIOS initialization at boot. It may also
   be needed for ancient IDE drives. It should not cause any harm on
   newer devices -- it just adds a little unnecessary code. */

#define USE_SETPARMS 0


#if (IDE_USE_SET_FEATURES || IDE_USE_ATAPI || INCLUDE_UDMA)

static BOOLEAN ide_set_features(PIDE_CONTROLLER pc, int phys_drive, byte feature, byte config);
#endif  /*#if (IDE_USE_SET_FEATURES || IDE_USE_ATAPI || INCLUDE_UDMA) */
static BOOLEAN ide_command(byte command, PIDE_CONTROLLER pc, int phys_drive, dword blockno, word nblocks);

#if (IDE_USE_ATAPI)
static BOOLEAN ide_do_atapi_command(PIDE_CONTROLLER pc);
BOOLEAN _atapi_drive_open(DDRIVE *pdr);
#endif

/*static BOOLEAN ide_reset(PIDE_CONTROLLER pc);*/
static BOOLEAN ide_command_diags(PIDE_CONTROLLER pc);
static BOOLEAN ide_command_read_multiple(PIDE_CONTROLLER pc, int phys_drive, dword blockno, word nblocks);
static BOOLEAN ide_command_write_multiple(PIDE_CONTROLLER pc, int phys_drive, dword blockno, word nblocks);
static BOOLEAN ide_rdwr_setup(PIDE_CONTROLLER pc, int phys_drive, dword blockno, word nblocks);
static BOOLEAN ide_do_command(PIDE_CONTROLLER pc);
#if (USE_SETPARMS)
static BOOLEAN ide_command_setparms(PIDE_CONTROLLER pc, int phys_drive, byte heads, byte sectors);
#endif
static void ide_clear_voregs(PIDE_CONTROLLER pc);
static void ide_read_register_file(PIDE_CONTROLLER pc);

BOOLEAN ide_in_words(PIDE_CONTROLLER pc, word nwords);
BOOLEAN ide_out_words(PIDE_CONTROLLER pc, word nwords);

#if (INCLUDE_UDMA)
BOOLEAN ide_set_dma_mode(DDRIVE *pdr, word *pbuff);
BOOLEAN ide_dma_io(DDRIVE *pdr, PIDE_CONTROLLER pc, dword sector, void *buffer, word count, BOOLEAN reading);
static BOOLEAN ide_do_dma_command(PIDE_CONTROLLER pc);
#endif


IDE_CONTROLLER controller_s[N_ATA_CONTROLLERS];

BOOLEAN trueide_card_changed(DDRIVE *pdr);
BOOLEAN trueide_card_installed(DDRIVE *pdr);
void trueide_report_card_removed(int driveno);

/* void ide_in_words(word nwords)
*
*   This routine reads nwords 16 bit words from the IDE data register
*   and places it in the buffer at user_address. It must increment the
*   user_address pointer so future calls will work correctly. In the IBM
*   PC port we do an equivalent operation in assembler and update
*   user_offset and user_segment.
*
*   This has to be FAST !!!!
*
*/


BOOLEAN ide_in_words(PIDE_CONTROLLER pc, word nwords)       /* __fn__ */
{
word  *p;
word buf[256];
dword l;

    pc->vi_status = ide_rd_status(pc->register_file_address);
    if (!(pc->vi_status & IDE_STB_DRQ))     /* Drive should have data ready */
    {
        /* Serious problem. Bus state incorrect   */
        pc->error_code = IDE_ERC_BUS;
        return(FALSE);
    }
    /* Note: the word wide data port is at offset 0 in the register file   */
    /* Casting pointer to a dword to test if it is odd. compiler warnings may result */
    p = (word  *) pc->user_address;
    l = (dword) p;
    if (l&1)
    { /* If the destination address is odd, double buffer */
    dword _nwords;
        for (_nwords = 0; _nwords < nwords; _nwords += 256,p += 256)
        {
            ide_insw(pc->register_file_address + IDE_OFF_DATA, buf, 256);
            copybuff((void *) p, (void *) buf, 512);

        }
    }
    else
    ide_insw(pc->register_file_address + IDE_OFF_DATA, p, nwords);
    pc->user_address += (nwords*2);
    return(TRUE);
}

/* extern void ide_out_words(word nwords)
*
*   This routine writes nwords 16 bit words from the buffer at
*   user_address to the IDE data register.
*   It must increment the user_address pointer so future calls will work
*   correctly. In the IBM PC port we do an equivalent operation in assembler
*   and update user_offset and user_segment.
*
*   This is just a sample. This has to be FAST !!!!
*
*/


BOOLEAN ide_out_words(PIDE_CONTROLLER pc, word nwords)      /* __fn__ */
{
word  *p;
word buf[256];
dword l;

    pc->vi_status = ide_rd_status(pc->register_file_address);

    if (!(pc->vi_status & IDE_STB_DRQ))     /* Drive should be requesting */
    {
        pc->error_code = IDE_ERC_BUS;
        return(FALSE);
    }

    /* Casting pointer to a dword to test if it is odd. compiler warnings may result */
    p = (word  *) pc->user_address;
    l = (dword) p;
    if (l&1)
    { /* If the source address is odd, double buffer */
    dword _nwords;
        for (_nwords = 0; _nwords < nwords; _nwords += 256,p += 256)
        {
            copybuff((void *) buf, (void *) p, 512);
            ide_outsw((dword)(pc->register_file_address + IDE_OFF_DATA), buf, 256);

        }
    }
    else
    ide_outsw((dword)(pc->register_file_address + IDE_OFF_DATA), p, nwords);
    pc->user_address += (nwords*2);
    return(TRUE);
}


/* ide_isr() - IDE ISR routine.
*
*   This routine is a portable interrupt service routine.
*
*/

void   ide_isr(int controller_number)                           /*__fn__*/
{
PIDE_CONTROLLER pc;

    pc = &controller_s[controller_number];
    /* Read the status register. to clear the interrupt status   */
    pc->vi_status = ide_rd_status(pc->register_file_address);
    /* Signal that we got an interrupt   */
    rtfs_port_set_signal(pc->ide_signal);
}

/* Open function. This is pointed to by the bdevsw[] table */
/*
* Note: This routine is called with the drive already locked so
*       in several cases there is no need for critical section code handling
*/


BOOLEAN ide_drive_open(DDRIVE *pdr)                         /*__fn__*/
{

	byte heads;
    byte sectors;
    dword ltemp;
    word buf[256];
    PIDE_CONTROLLER pc;

    pc = &controller_s[pdr->controller_number];

    if (pc->drive[pdr->logical_unit_number].open_count)
        return(TRUE);

    /*ide_controller_reset(pc) */
    ide_clear_voregs(pc);               /* Clear virtual output registers */
   /* Clear the atapi flag to start */
    pc->drive[pdr->logical_unit_number].protocol = 0;
    if (!ide_wait_ready(pc, (int)TIMEOUT_RESET))
        return(FALSE);
    /* First issue a reset to the controller */
    ide_wr_dig_out(pc->register_file_address, pc->contiguous_io_mode, IDE_OUT_RESET);
    /* sleep 2 millis can be as little as 12 microseconds) */
    rtfs_port_sleep(2);
    ide_wr_dig_out(pc->register_file_address, pc->contiguous_io_mode, 0);
    /* Wait for busy to drop */
    if (!ide_wait_not_busy(pc, (int)TIMEOUT_RESET))
        return(FALSE);
#if (IDE_USE_ATAPI)
    if (pdr->drive_flags & DRIVE_FLAGS_CDFS)
        return(_atapi_drive_open(pdr));
#endif
    /* Now perform drive diagnostics   */
    if (!ide_command_diags(pc))
        return(FALSE);

   /* Set the power value in case we are on a lowl power system */
   /* Note: change the default value */
/* SET FEATURES needs a proper API call */
#if (IDE_USE_SET_FEATURES)
	if (0)
	{
    ide_set_features(pc, pdr->logical_unit_number, IDE_FEATURE_SETPERF, IDE_FEATURE_SETPERF_VALUE);
    ide_set_features(pc, pdr->logical_unit_number, IDE_FEATURE_WCACHE_ON, 0x00);
    ide_set_features(pc, pdr->logical_unit_number, IDE_FEATURE_DEFECT_ON, 0x00);
    ide_set_features(pc, pdr->logical_unit_number, IDE_FEATURE_RLOOK_ON, 0x00);
	}
	else
	{
	ide_set_features(pc, pdr->logical_unit_number, IDE_FEATURE_WCACHE_OFF, 0x00);
    ide_set_features(pc, pdr->logical_unit_number, IDE_FEATURE_RLOOK_OFF, 0x00);
	}
#endif

    /* Clear the atapi flag to start */
    pc->drive[pdr->logical_unit_number].protocol = 0;
    /*  Use the ATA identify command. word 3 == # heads */
    /*  word 6 == sectors per track */
    /*  word 47 bits 0-7 ==s max sectors per read/write multiple */
    /*  word 49 bit  9  == if 1 supports logocal block addressing */
    /*  word 60-61      == number of user addressable LBA s in LBA mode */
    pc->user_address = (byte *) &buf[0];
    if (!ide_command(IDE_CMD_IDENT, pc, pdr->logical_unit_number, 0, 0))
    {
#if (IDE_USE_ATAPI)
        return(_atapi_drive_open(pdr));
#else
            return(FALSE);
#endif /* (IDE_USE_ATAPI) */
    }
    else
    {
        pc->drive[pdr->logical_unit_number].media_descriptor=0xf8; /* assume hard disk media */
#if (INCLUDE_UDMA)
        pc->drive[pdr->logical_unit_number].UltraDmaModeCapable =
                ide_set_dma_mode(pdr, buf);
#endif /* INCLUDE_UDMA */
    }
    pc->drive[pdr->logical_unit_number].num_cylinders = to_WORD((byte *) &buf[1]);
    heads   = (byte) (to_WORD((byte *) &buf[3]));
    sectors  = (byte)(to_WORD((byte *) &buf[6]));
    pc->drive[pdr->logical_unit_number].max_multiple = (byte) (to_WORD((byte *) &buf[47]) & 0xff);

    pc->drive[pdr->logical_unit_number].supports_48bits = FALSE;
    if (!(to_WORD((byte *) &buf[83]) & 0x8000) && /* Bit 15 must be zero and */
         (to_WORD((byte *) &buf[83]) & 0x4000) && /* bit 14 must be one for word 83 to be valid */
         (to_WORD((byte *) &buf[83]) & 0x0400)) /* 48-bit support? */
        pc->drive[pdr->logical_unit_number].supports_48bits = TRUE;

    if (to_WORD((byte *) &buf[49]) & 0x200)
    {
        pc->drive[pdr->logical_unit_number].supports_lba = TRUE;

        if (pc->drive[pdr->logical_unit_number].supports_48bits) /* 48-bit addressing */
        {
            /* Check if total lba is higher than 32 bits, which is all we handle right now */
            if (to_WORD((byte *) &buf[103]) || to_WORD((byte *) &buf[102]))
                ltemp = 0xffffffff;
            else
            {
                /* Get highword low word of highest lba */
                ltemp = to_WORD((byte *) &buf[101]);
                ltemp <<= 16;
                ltemp += to_WORD((byte *) &buf[100]);
            }
        }
        else /* 28-bit addressing */
        {
            /* Get highword low word of highest lba */
            ltemp = to_WORD((byte *) &buf[61]);
            ltemp <<= 16;
            ltemp += to_WORD((byte *) &buf[60]);
        }

        pc->drive[pdr->logical_unit_number].total_lba = ltemp;
    }
    else
    {
        pc->drive[pdr->logical_unit_number].supports_lba = FALSE;
        pc->drive[pdr->logical_unit_number].total_lba = 0;
    }

    /* Our local view of the drive for block to track::sector::head xlations */
    pc->drive[pdr->logical_unit_number].num_heads   =   heads;
    pc->drive[pdr->logical_unit_number].sec_p_track = sectors;
    pc->drive[pdr->logical_unit_number].sec_p_cyl   =  (word) heads;
    pc->drive[pdr->logical_unit_number].sec_p_cyl   =  (word) (pc->drive[pdr->logical_unit_number].sec_p_cyl  * sectors);

#if (USE_SETPARMS)
    if (!ide_command_setparms(pc, pdr->logical_unit_number, heads, sectors))
    {
        return(FALSE);
    }
#endif

    /* 11-10-2000 - New code. Only do SETM on open. We had been doing it
       on every read & write which caused a cache flush and slowed down io
       Thanks to Paul Swan. */
    if (pc->drive[pdr->logical_unit_number].max_multiple > 1)
    {
        if (!ide_command(IDE_CMD_SETM, pc, pdr->logical_unit_number, 0, 0))
        {
            return(FALSE);
        }
    }

    pc->drive[pdr->logical_unit_number].open_count++;
    return(TRUE);
}

/* Read/write function: */

BOOLEAN _ide_io(int driveno, dword sector, void *buffer, word count, BOOLEAN reading)    /*__fn__*/
{
BOOLEAN ret_val;
PIDE_CONTROLLER pc;
DDRIVE *pdr;

    pdr = pc_drno_to_drive_struct(driveno);
    if (!pdr)
        return(FALSE);
    pc = &controller_s[pdr->controller_number];
    if (!count || !pc)                      /* Must have a count */
        return(FALSE);

    ret_val = FALSE;
    rtfs_port_claim_mutex(prtfs_cfg->ide_semaphore);

    /* Set up a counter for data transfer (See ide_isr) */
    pc->sectors_remaining = count;
#if (INCLUDE_UDMA)
    /* Try doing ultra-dma if it fails fall trough and try pio */
    if(pc->drive[pdr->logical_unit_number].UltraDmaModeCapable)
        if (ide_dma_io(pdr, pc, sector, buffer, count, reading))
        {
            ret_val = TRUE;
            goto ide_io_done;
        }
    /* If it works jump to done, otherwise fall trough and try pio */
#endif
    if (reading)
    {
        pc->user_address = (byte *) buffer;
        /* Use read multiple command */
        if ((count > 1) && (pc->drive[pdr->logical_unit_number].max_multiple > 1))
        {
            /* Call set multiple */
            ret_val = ide_command_read_multiple(pc, pdr->logical_unit_number, sector, count);
            if (!ret_val)
            {
                /* Set up a counter for data transfer (See ide_isr) */
                pc->sectors_remaining = count;
                goto non_multiple_read;
            }
        }
        else
        {
non_multiple_read:
            do
            {
                pc->block_size = 1;
                if(pc->drive[pdr->logical_unit_number].supports_48bits && (sector > 0xfffffff || count > 0xff))
                    ret_val = ide_command(IDE_CMD_READS_EXT, pc, pdr->logical_unit_number, sector, count);
                else
                    ret_val = ide_command(IDE_CMD_READS, pc, pdr->logical_unit_number, sector, count);
                if (!ret_val)
                {
                        break;  /* Break out of do loop */
                }
            } while(!ret_val);
        }
    }
    else /* WRITING */
    {
        pc->user_address = (byte *) buffer;
        if ( (count > 1) && (pc->drive[pdr->logical_unit_number].max_multiple > 1))
        {
            /* Try write multiple. */
            ret_val = ide_command_write_multiple(pc, pdr->logical_unit_number, sector, count);
            if (!ret_val)
            {
                pc->sectors_remaining = count;
                goto non_multiple_write;
            }
        }
        else
        {
non_multiple_write:
            pc->block_size = 1;
            if(pc->drive[pdr->logical_unit_number].supports_48bits && (sector > 0xfffffff || count > 0xff))
                ret_val = ide_command(IDE_CMD_WRITES_EXT, pc, pdr->logical_unit_number, sector, count);
            else
                ret_val = ide_command(IDE_CMD_WRITES, pc, pdr->logical_unit_number, sector, count);
        }
    }
#if (INCLUDE_UDMA)
ide_io_done:
#endif
    rtfs_port_release_mutex(prtfs_cfg->ide_semaphore);
    return(ret_val);
}

BOOLEAN ide_io(int driveno, dword sector, void *buffer, word count, BOOLEAN
reading)
{
    byte *b;
    DDRIVE *pdr;
    b = (byte *) buffer;

    pdr = pc_drno_to_drive_struct(driveno);
    if (!pdr)
        return(FALSE);
    if (controller_s[pdr->controller_number].drive[pdr->logical_unit_number].supports_48bits)
    {
        if (! _ide_io(driveno, sector, b, count, reading))
            return(FALSE);
        return(TRUE);
    }

    while (count > MAX_BLOCKS)
    {
           if (! _ide_io(driveno, sector, b, MAX_BLOCKS, reading))
                return(FALSE);
            sector += MAX_BLOCKS;
            b += (MAX_BLOCKS*512);
            count -= MAX_BLOCKS;
    }
    if (count)
    {
         if (! _ide_io(driveno, sector, b, count, reading))
                return(FALSE);
    }
    return(TRUE);
}

static BOOLEAN ide_command(byte command, PIDE_CONTROLLER pc, int logical_unit_number, dword blockno, word nblocks)
{
    ide_clear_voregs(pc);               /* Clear virtual output registers */
     /* Call ide_rdwr_setup to set transfer addresses, sector_count, sector_number,
        cylinder_low, cylinder_hi and drive_head */
    if ((command == IDE_CMD_ERASES) ||
        (command == IDE_CMD_DMA_READ) ||  /* Ultra dma */
        (command == IDE_CMD_DMA_WRITE) || /* Ultra dma */
        (command == IDE_CMD_DMA_READ_EXT) ||  /* Ultra dma */
        (command == IDE_CMD_DMA_WRITE_EXT) || /* Ultra dma */
        (command == IDE_CMD_READS) ||
        (command == IDE_CMD_READS_EXT) ||
        (command == IDE_CMD_READM) ||
        (command == IDE_CMD_READM_EXT) ||
        (command == IDE_CMD_WRITEM_NE) ||
        (command == IDE_CMD_WRITEM) ||
        (command == IDE_CMD_WRITEM_EXT) ||
        (command == IDE_CMD_WRITES_NE) ||
        (command == IDE_CMD_WRITES_EXT) ||
        (command == IDE_CMD_WRITES) )
    {
        if (!ide_rdwr_setup(pc, logical_unit_number, blockno, nblocks))
            return(FALSE);
        pc->timer = (word)TIMEOUT_RDWR;
    }
    else
    {
        pc->timer = (word)TIMEOUT_TYPICAL;
    }
    pc->vo_drive_head  |= 0x20;                 /* Bit five is always one */
    if (logical_unit_number)                                /* Bit four is drive (1 or 0) */
        pc->vo_drive_head |= 0x10;              /* select device 1 (slave) */
    if (command == IDE_CMD_ERASES)              /* Erase sets bit 7 */
        pc->vo_drive_head |= 0x80;
    if (command == IDE_CMD_SETM)
    {
        pc->block_size = pc->drive[logical_unit_number].max_multiple;
        pc->vo_sector_count = pc->block_size;
    }
    /* 11-10-2000 - New code. Since we do not call SETM on each WRITEM or
       READM we must set block size now on the READM, WRITEM calls */
    if ((command == IDE_CMD_READM)||
        (command == IDE_CMD_READM_EXT)||
        (command == IDE_CMD_WRITEM_EXT)||
        (command == IDE_CMD_WRITEM))
    {
        pc->block_size = pc->drive[logical_unit_number].max_multiple;
    }

    pc->vo_command = command;

    /* Call the processing routine */
    return(ide_do_command(pc));
}

#if (IDE_USE_ATAPI)


BOOLEAN _atapi_drive_open(DDRIVE *pdr)
{
int i;
word buf[256];
byte cbuff[6];
PIDE_CONTROLLER pc;

    if (!pdr)
        return(FALSE);

    pc = &controller_s[pdr->controller_number];

#if (INCLUDE_UDMA)
        pc->drive[pdr->logical_unit_number].UltraDmaModeCapable =
                ide_set_dma_mode(pdr, buf);
#endif /* INCLUDE_UDMA */

    if (pc->drive[pdr->logical_unit_number].open_count)
        return(TRUE);

    pc->user_address = (byte *) &buf[0];
    if (!ide_command(ATAPI_CMD_IDENT, pc, pdr->logical_unit_number, 0, 0))
    {
    if (!ide_command(ATAPI_CMD_IDENT, pc, pdr->logical_unit_number, 0, 0))
    {
        return(FALSE);
    }
    }
    /* It is atapi */
    pc->drive[pdr->logical_unit_number].protocol = 2;
    /* PVO 11-26-99 just send reset. do not treat as a command */
    ide_wr_command(pc->register_file_address, IDE_CMD_SOFT_RESET);
    for(i=0;i<6;i++)
    {
        if(simple_atapi_command(ATAPI_PKT_CMD_TEST_UNIT_READY, pc, pdr->logical_unit_number))
            break;
        rtfs_port_sleep(2000);
    }
    /* prepare for Himawari test */
    copybuff(cbuff, ((byte *)buf)+54, 6);
    if((to_WORD((byte *) &buf[0])&0xdf03)==0x8500)
    {
        /* It is a CD-ROM */
        pc->drive[pdr->logical_unit_number].media_descriptor=0xef;
        pc->drive[pdr->logical_unit_number].CMD_DRQ_type=(byte)(to_WORD((byte *) &buf[0])&0x60);
#if (0)
        pio_set = 0;
        if (to_WORD((byte *) &buf[64]) & 0x01)
            pio_set = 0x0b; /* 1011  MODE 3 */
        if (to_WORD((byte *) &buf[64]) & 0x02)
            pio_set = 0x0c; /* 1100  MODE 4 */
        if (pio_set)
        {
            if (ide_set_features(pc, pdr->logical_unit_number, 0x03, pio_set))
                RTFS_PRINT_STRING_1((byte *)"PIO Mode set succeed",PRFLG_NL); /* "PIO Mode set succeed" */
            else
                RTFS_PRINT_STRING_1((byte *)"PIO Mode set failed",PRFLG_NL); /* "PIO Mode set failed" */
        }
#endif
    }
    else if (rtfs_cs_strcmp(cbuff,(byte *)"SL1-02", CS_CHARSET_NOT_UNICODE) == 0) /* If a HIMAWARI drive? Jerry*/
    {
        /* LS-120 */
        /* ??? Do first time to make sure MC is clear (needed by some LS-120 drives) */
        ide_command(IDE_CMD_GET_MEDIA_STATUS, pc, pdr->logical_unit_number, 0, 0);
        if (ide_command(IDE_CMD_GET_MEDIA_STATUS, pc, pdr->logical_unit_number, 0, 0))
        {
            if (ide_command(ATAPI_CMD_IDENT, pc, pdr->logical_unit_number, 0, 0))
            {
                /* capture the media descriptor for the format routine */
                switch(pc->drive[pdr->logical_unit_number].medium_type_code=(byte)buf[4]) /* Bytes/Sector*/
                {             /*                        Cyl H  Sector   Track   Capacity    */
                case    0x30: /* Formatted   UHD    963 8    512      32    120Mb */
                case    0x31: /* Unformatted UHD    963 8    512      32    120Mb */
                case    0x20: /* Unformatted HD      80 2    512      18    1.44Mb*/
                case    0x24:   /* Formatted HD      80 2    512      18    1.44Mb*/
                case    0x26:   /* DMF               80 2    512      21    1.7Mb*/
                    pc->drive[pdr->logical_unit_number].media_descriptor=0xf0;
                    pc->drive[pdr->logical_unit_number].allocation_unit=0x01;
                    break;
                case    0x22:   /* Formatted NEC     77 2   1024       8    1.20Mb*/
                case    0x23:   /* Formatted Toshiba 80 2    512      15    1.20Mb*/
                    pc->drive[pdr->logical_unit_number].media_descriptor=0xf9;
                    pc->drive[pdr->logical_unit_number].allocation_unit=0x01;
                    break;
                case    0x27:   /* NEC-DMF           77 2    512       9    720Kb*/
                case    0x10:   /* Unformatted DD    80 2    512       9    720Kb*/
                case    0x11:   /* Formatted DD      80 2    512       9    720Kb*/
                    pc->drive[pdr->logical_unit_number].media_descriptor=0xf9;
                    pc->drive[pdr->logical_unit_number].allocation_unit=0x02;
                    break;
                default:
                    return(FALSE);
                }
#if (ZERO)
                switch(pc->drive[pdr->logical_unit_number].medium_type_code=(byte)buf[4])                                                 /*                         Bytes/  Sectors/             */
                {             /*                    Cyl H  Sector   Track   Capacity    */
                case    0x30: /* Formatted   UHD    963 8    512      32    120Mb */
                case    0x31: /* Unformatted UHD    963 8    512      32    120Mb */
                    pc->drive[pdr->logical_unit_number].enable_mapping = TRUE;
                    break;
                default:
                    pc->drive[pdr->logical_unit_number].enable_mapping = FALSE;
                    break;
                }
#endif
            }
        }
    }
    else  /* NOT CD, Not LS-120 */
       return(FALSE);
    pc->drive[pdr->logical_unit_number].open_count++;
    /* It is an ATAPI IDE drive enable media status notification */
    ide_set_features(pc, pdr->logical_unit_number, 0x95, 0);
    return(TRUE);
}


/* Read/write function: */
BOOLEAN atapi_cd_read(int driveno, dword sector, void *buffer, word count)    /*__fn__*/
{
word w;
dword l;
BOOLEAN ret_val;
PIDE_CONTROLLER pc;
DDRIVE *pdr;

    pdr = pc_drno_to_drive_struct(driveno);
    if (!pdr)
        return(FALSE);
    pc = &controller_s[pdr->controller_number];
    if (!count || !pc)                      /* Must have a count */
        return(FALSE);

    ret_val = FALSE;
    rtfs_port_claim_mutex(prtfs_cfg->ide_semaphore);

    pc->user_address = (byte *) buffer;
    ide_clear_voregs(pc);               /* Clear virtual output registers */
    pc->timer = (word)(TIMEOUT_TYPICAL*4);
    pc->vo_drive_head  |= 0x20;                 /* Bit five is always one */
    if (pdr->logical_unit_number)                             /* Bit four is drive (1 or 0) */
        pc->vo_drive_head |= 0x10;              /* select device 1 (slave) */

    rtfs_memset(&pc->drive[pdr->logical_unit_number].atapi_packet, 0, sizeof(ATAPI_PACKET));
    pc->drive[pdr->logical_unit_number].atapi_packet.generic.op_code=ATAPI_PKT_CMD_READ;
    pc->atapi_words_to_xfer = count*1024; /* 2048 bytes/block */
    /* In lba mode sector no = block (0.7), cyl_low = (8.15), cyl_high = (16,23)
        drive_head = (24, 27) */
    /* Bytes 2 to 5 are sector */
    pc->drive[pdr->logical_unit_number].atapi_packet.general.bytes[2]     = (byte) 0;
    l = (sector >> 16) & 0xff;
    pc->drive[pdr->logical_unit_number].atapi_packet.general.bytes[3]     = (byte) l;
    l = (sector >> 8) & 0xff;
    pc->drive[pdr->logical_unit_number].atapi_packet.general.bytes[4]     = (byte) l;
    l = sector & 0xff;
    pc->drive[pdr->logical_unit_number].atapi_packet.general.bytes[5]     = (byte) l;

    /* Bytes 7 to 8 are n sectors */
    w = (word) ((count >> 8) & 0xff);
    pc->drive[pdr->logical_unit_number].atapi_packet.general.bytes[7]     = (byte) w;
    w = (word) (count & 0xff);
    pc->drive[pdr->logical_unit_number].atapi_packet.general.bytes[8]     = (byte) w;

    /* Call the processing routine */
    if (!ide_do_atapi_command(pc))
    {
        rtfs_port_release_mutex(prtfs_cfg->ide_semaphore);
        return(FALSE);
    }
    rtfs_port_release_mutex(prtfs_cfg->ide_semaphore);
    return(TRUE);
}



#endif  /*  #if (IDE_USE_ATAPI) */

/* ........................................................................
    .... End Device driver external interface routines.             .......
    .......................................................................*/
#if (IDE_USE_SET_FEATURES || IDE_USE_ATAPI || INCLUDE_UDMA)
static BOOLEAN ide_set_features(PIDE_CONTROLLER pc, int logical_unit_number, byte feature, byte config) /* __fn__ */
{
    ide_clear_voregs(pc);               /* Clear virtual output registers */
    if (logical_unit_number)                            /* Bit four is drive (1 or 0) */
        pc->vo_drive_head |= 0x10;          /* select device 1 (slave) */
    pc->vo_feature      = feature;
    pc->vo_sector_count = config;
    pc->vo_command = IDE_CMD_SETF;
    pc->timer = (word)TIMEOUT_TYPICAL;
 /* Call the processing routine */
    return(ide_do_command(pc));
}
#endif  /*#if (IDE_USE_SET_FEATURES || IDE_USE_ATAPI || INCLUDE_UDMA) */


/* Command execution routines. */

/*  ide_command_diags - Execute drive diagnostics
*
*
*
* Returns:
*   TRUE if the drive diagnostic succeeded else FALSE.
*
*   This routine execute a diagnostic request. If the diagnostic succeeds
*   it returns TRUE otherwise it returns FALSE.
*
* called by: ide_drive_init
*
* This routine is portable
*
*/

static BOOLEAN ide_command_diags(PIDE_CONTROLLER pc) /* __fn__ */
{
    ide_clear_voregs(pc);               /* Clear virtual output registers */
    pc->vo_command = IDE_CMD_DIAG;      /* Command */
    pc->timer = (word)TIMEOUT_DIAG;
    return(ide_do_command(pc));
}
/*DM: commented out conditional to re-enable */

/*#endif CDFS_ONLY */



/*  ide_command_setparms - Set drive parameters
*
* Inputs:
*   drive       - Drive number (0 or 1)
*   heads      -
*   sectors
*
* Returns:
*   TRUE on success else FALSE. If FALSE pc->error_no will contain the error.
*
*   This routine tells the drive how many heads and sectors per track
*   we will be basing our block to sector:trak:head calculations. The
*   drive will map sector:trak:head to its internal geometry.
*
* This routine is portable
*/

#if (USE_SETPARMS)

static BOOLEAN ide_command_setparms(PIDE_CONTROLLER pc, int logical_unit_number, byte heads, byte sectors) /* __fn__ */
{
byte max_head;

    ide_clear_voregs(pc);                 /* Clear virtual output registers */
  /* Sectors per track go in sector count register */
    pc->vo_sector_count  = sectors;
  /* number of heads - 1 goes in the drive head register */
    max_head = (byte) (heads - 1);
    pc->vo_drive_head    = (byte) (max_head & 0x0f);   /* bit 0-3 is head */
    pc->vo_drive_head    |= 0x20;                   /* Bit five is always one */
    if (logical_unit_number)                                     /* Bit four is drive (1 or 0) */
        pc->vo_drive_head |= 0x10;
    pc->vo_command = IDE_CMD_INITP;
    pc->timer = (word)TIMEOUT_TYPICAL;
 /* Call the processing routine */
    return(ide_do_command(pc));
}

#endif


/*  ide_command_read_multiple - Execute read multiple commands
*
* Inputs:
*   address - Destination address for the data
*   drive       - Drive number (0 or 1)
*   blockno - Block number to read
*   nblocks - Number of blocks (legal range: 1-256)
*
* Returns:
*   TRUE on success else FALSE. If FALSE pc->error_no will contain the error.
*
*   This is the read worker function for the device driver. It performs a one
*   or many block read operation. It calls ide_rdwr_setup() and ide_do_command
*   to do most of the work.
*
*   Called by ide_io
*
* This routine is portable
*/

static BOOLEAN ide_command_read_multiple(PIDE_CONTROLLER pc, int logical_unit_number, dword blockno, word nblocks) /* __fn__ */
{
    /* Call set multiple */
/* 11-10=2000 - Do not call this, we did it in the open
    if (!ide_command(IDE_CMD_SETM, pc, logical_unit_number, 0, 0))
        return(FALSE);
*/
    if(pc->drive[logical_unit_number].supports_48bits && (blockno > 0xfffffff || nblocks > 0xff))
        return(ide_command(IDE_CMD_READM_EXT, pc, logical_unit_number, blockno, nblocks));
    else
        return(ide_command(IDE_CMD_READM, pc, logical_unit_number, blockno, nblocks));
}

/*  ide_command_write_multiple - Execute write_multiple(s) command
*
* Inputs:
*   address - Source address for the data
*   drive       - Drive number (0 or 1)
*   blockno - Block number to read
*   nblocks - Number of blocks (legal range: 1-256)
*
* Returns:
*   TRUE on success else FALSE. If FALSE pc->error_no will contain the error.
*
*   This routine performs a one or many block write operation. It calls
*   ide_rdwr_setup() and ide_do_command() to do most of the work.
*
*   This is the write worker function for the device driver. It performs a one
*   or many block write operation. It calls ide_rdwr_setup() and ide_do_command
*   to do most of the work.
*
*   Called by ide_do_block
*
* This routine is portable
*/

static BOOLEAN ide_command_write_multiple(PIDE_CONTROLLER pc, int logical_unit_number, dword blockno, word nblocks) /* __fn__ */
{
    /* Call set multiple */
/* 11-10=2000 - Do not call this, we did it in the open
    if (!ide_command(IDE_CMD_SETM, pc, logical_unit_number, 0, 0))
        return(FALSE);
*/
    if(pc->drive[logical_unit_number].supports_48bits && (blockno > 0xfffffff || nblocks > 0xff))
        return(ide_command(IDE_CMD_WRITEM_EXT, pc, logical_unit_number, blockno, nblocks));
    else
        return(ide_command(IDE_CMD_WRITEM, pc, logical_unit_number, blockno, nblocks));
}

/* ide_rdwr_setup - Setup routine for read  for read and write operations
* Inputs:
*   address - Destination address for the data
*   drive       - Drive number (0 or 1)
*   blockno - Block number to read/write
*   nblocks - Number of blocks (legal range: 1-256)
*
* Outputs:
*
*   Sets the following register file registers:
*   vo_sector_count,vo_sector_number,vo_cyl_low,vo_cyl_high,vo_drive_head
*
*
* Returns:
*   TRUE if inputs are valid else FALSE
*
*   This routine performs setup operations common to read and write. The
*   registers listed above are set up and the user transfer address is
*   set up.
*
* This routine is portable
*
*/

static  byte l_to_byte(dword l, int bit)
{
    if (bit)
        l >>= bit;
    l &= 0xff;
    return( (byte) l);
}




static BOOLEAN ide_rdwr_setup(PIDE_CONTROLLER pc, int logical_unit_number, dword blockno, word nblocks) /* __fn__ */
{
word cylinder;
word head;
word sector;
word blk_offs_in_cyl;

    /* Check against max_blocks. this is 256 for IDE. In real mode 80xx we limit
        it to 128 to eliminate segment wrap. */
    if ((!pc->drive[logical_unit_number].supports_48bits && nblocks > MAX_BLOCKS) ||
        (logical_unit_number && logical_unit_number != 1)  ||               /* Can only be 0 or 1 */
        (!pc->drive[logical_unit_number].sec_p_cyl&&(pc->drive[logical_unit_number].media_descriptor!=0xef)) )  /* Will be non zeroe if the */
    {                                               /* Drive is initted */
        pc->error_code      = IDE_ERC_ARGS;
        return(FALSE);                          /* Drive is initted */
    }

    if (pc->drive[logical_unit_number].supports_lba)
    {
        pc->vo_sector_number = (byte) l_to_byte(blockno, 0);
        pc->vo_cyl_low      = (byte) l_to_byte(blockno, 8);
        pc->vo_cyl_high     = (byte) l_to_byte(blockno, 16);
        pc->vo_drive_head   = (byte) (l_to_byte(blockno, 24) & 0x0f);
        pc->vo_high_lba     = (byte) l_to_byte(blockno, 24);
        pc->vo_drive_head   |= 0x40;  /* Select lba mode */
    }
    else
    {
    /* Calculate block address */
        cylinder = (word) (blockno/pc->drive[logical_unit_number].sec_p_cyl);
        blk_offs_in_cyl = (word) (blockno%pc->drive[logical_unit_number].sec_p_cyl);
        head = (word) (blk_offs_in_cyl/pc->drive[logical_unit_number].sec_p_track);
        sector = (word) (blk_offs_in_cyl%pc->drive[logical_unit_number].sec_p_track);
/* Load registers */
        pc->vo_sector_number = (byte) (sector + 1); /* Note in 1-N order at the controller */
        pc->vo_cyl_low      = (byte) (cylinder & 0xff);
        pc->vo_cyl_high     = (byte) (cylinder >> 8);
        pc->vo_drive_head   = (byte) (head & 0x0f); /* bit 0-3 is head */
    }
    pc->vo_sector_count  = nblocks;
    return(TRUE);
}


static  BOOLEAN ide_error(PIDE_CONTROLLER pc, word error_code)
{
    pc->error_code = error_code;
    pc->command_complete = TRUE;            /* Shesssaaa hung */
    return(FALSE);
}

/*  ide_do_command- Execute an ide cmd and poll or block until complete
* Inputs:
*   uses values in cs (the control structure)
* Outputs:
*   pc->command_complete and pc->error_code are updated. As well as all the
*   vi_xxx (virtual register file in) are read in.
* Returns:
*   TRUE if command succeeded else look in error_code.
*
*   This routine is called by all ide_command_xxx routines to begin
*   command executiion. It copies the vo_xxxxx register file from the control
*   structure to the drive and then polls or blocks until completion. If
*   pc->vo_dig_out & IDE_OUT_IEN is true it polls waiting for completion,
*   otherwise it blocks waiting for the ISR to wake it. Each command has
*   a time out value in pc->timer.
*
*/

static BOOLEAN ide_do_command(PIDE_CONTROLLER pc) /* __fn__ */
{
    word        utemp;

    pc->error_code = 0;
    pc->command_complete = FALSE;

 /* The drive may be busy. If so, wait for it to complete */

    if (!ide_wait_not_busy(pc, pc->timer))
        return(ide_error(pc,IDE_ERC_BUS));                  /* bus is hung */

    /* There should be no pending interrupt complete signals
        so call os_ide_clear_signal(); to be sure we are not
        signalled until after an int occurs. */

    if (!(pc->vo_dig_out & IDE_OUT_IEN))
    {
        rtfs_port_clear_signal(pc->ide_signal);
    }
    {
        /* If we are doing a 48-bit operation, we need to double-write the registers */
        if((pc->vo_command == IDE_CMD_DMA_READ_EXT) || (pc->vo_command == IDE_CMD_DMA_WRITE_EXT) ||
           (pc->vo_command == IDE_CMD_READM_EXT) || (pc->vo_command == IDE_CMD_WRITEM_EXT) ||
           (pc->vo_command == IDE_CMD_READS_EXT) || (pc->vo_command == IDE_CMD_WRITES_EXT))
        {
            /* Load the upper bits first */
            ide_wr_sector_count(pc->register_file_address, (byte) (pc->vo_sector_count >> 8));
            ide_wr_sector_number(pc->register_file_address, pc->vo_high_lba);
            ide_wr_cyl_low(pc->register_file_address, 0);
            ide_wr_cyl_high(pc->register_file_address, 0);
            pc->vo_drive_head = pc->vo_drive_head & 0xf0; /* lower 4 bits are reserved */
        }
     /* Now load the rest of the register file. (send command last) */
        ide_wr_sector_count(pc->register_file_address, (byte) pc->vo_sector_count);
        ide_wr_sector_number(pc->register_file_address, pc->vo_sector_number);
        ide_wr_cyl_low(pc->register_file_address,  pc->vo_cyl_low);
        ide_wr_cyl_high(pc->register_file_address, pc->vo_cyl_high);
        ide_wr_drive_head(pc->register_file_address, pc->vo_drive_head);
        ide_wr_feature(pc->register_file_address, pc->vo_feature);
     /* Write the digital output register (interrupts on/off) */
        ide_wr_dig_out(pc->register_file_address, pc->contiguous_io_mode, pc->vo_dig_out);
     /* The register file is loaded.Now load the command into the command register. */
#if (INCLUDE_UDMA)
      if((pc->vo_command == IDE_CMD_DMA_READ) || (pc->vo_command == IDE_CMD_DMA_WRITE) ||
         (pc->vo_command == IDE_CMD_DMA_READ_EXT) || (pc->vo_command == IDE_CMD_DMA_WRITE_EXT))
        return(ide_do_dma_command(pc));
      else
#endif
        ide_wr_command(pc->register_file_address, pc->vo_command);

        if ((pc->vo_command == IDE_CMD_WRITES) ||
            (pc->vo_command == IDE_CMD_WRITES_EXT) ||
            (pc->vo_command == IDE_CMD_WRITES_NE) ||
            (pc->vo_command == IDE_CMD_WRITEM) ||
            (pc->vo_command == IDE_CMD_WRITEM_EXT) ||
            (pc->vo_command == IDE_CMD_WRITEM_NE) )
        {
            /* We must wait at least 400 nSec for busy to be valid.
               4 reads of the alternate status register will provide
               at least this */
           ide_rd_alt_status(pc->register_file_address, pc->contiguous_io_mode);   /* Give time for BUSY to assert */
           ide_rd_alt_status(pc->register_file_address, pc->contiguous_io_mode);   /* Give time for BUSY to assert */
           ide_rd_alt_status(pc->register_file_address, pc->contiguous_io_mode);   /* Give time for BUSY to assert */

           ide_rd_alt_status(pc->register_file_address, pc->contiguous_io_mode);   /* Give time for BUSY to assert */

            /* Busy should deassert. DRQ should assert */
            if (!(ide_wait_not_busy(pc, pc->timer) && ide_wait_drq(pc, pc->timer)))
            {
                pc->error_code = IDE_ERC_BUS;
                pc->command_complete = TRUE;            /* Shesssaaa hung */
                return(FALSE);
            }
            if (pc->sectors_remaining >= (word) pc->block_size)
                utemp = (word) pc->block_size;
            else
                utemp = pc->sectors_remaining;

            ide_out_words(pc, (word) (utemp << 8));/* 256 times xfer size */
            pc->sectors_remaining = (word) (pc->sectors_remaining - utemp);
        }

    /* The NEW SMART WAY OF DOING IT. DOING THE DATA TRANSFER AT THE APP LAYER */
        while (!pc->command_complete)
        {
            if (!(pc->vo_dig_out & IDE_OUT_IEN))    /* Interupt mode */
            {
                /* Wait for a signal of completion from ide_interrupt. Pass in
                   pc->timer (# millis to wait before detecting a timeout */
                if (rtfs_port_test_signal(pc->ide_signal, pc->timer) != 0)
                    goto bail;
            }
            else
            {
                ide_rd_alt_status(pc->register_file_address, pc->contiguous_io_mode);   /* Give time for BUSY to assert */
                ide_rd_alt_status(pc->register_file_address, pc->contiguous_io_mode);   /* Give time for BUSY to assert */
                ide_rd_alt_status(pc->register_file_address, pc->contiguous_io_mode);   /* Give time for BUSY to assert */
                ide_rd_alt_status(pc->register_file_address, pc->contiguous_io_mode);   /* Give time for BUSY to assert */
                ide_rd_alt_status(pc->register_file_address, pc->contiguous_io_mode);   /* Give time for BUSY to assert */
                ide_rd_alt_status(pc->register_file_address, pc->contiguous_io_mode);   /* Give time for BUSY to assert */
                ide_rd_alt_status(pc->register_file_address, pc->contiguous_io_mode);   /* Give time for BUSY to assert */
                ide_rd_alt_status(pc->register_file_address, pc->contiguous_io_mode);   /* Give time for BUSY to assert */
                if (!ide_wait_not_busy(pc, pc->timer)) /* Polled mode */
                {
    bail:
                    pc->error_code      = IDE_ERC_TIMEOUT;
                    pc->command_complete = TRUE;            /* Shesssaaa hung */
                }
            }
            if (!pc->command_complete)
                ide_interrupt(pc->controller_number);
        }
    }
    if (pc->error_code)
        return(FALSE);
    else
        return(TRUE);
}

/*  ide_do_atapi_command- Execute an ide cmd and poll or block until complete
* Inputs:
*   uses values in cs (the control structure)
* Outputs:
*   pc->command_complete and pc->error_code are updated. As well as all the
*   vi_xxx (virtual register file in) are read in.
* Returns:
*   TRUE if command succeeded else look in error_code.
*
*   This routine is called by all ide_command_xxx routines to begin
*   command executiion. It copies the vo_xxxxx register file from the control
*   structure to the drive and then polls or blocks until completion. If
*   pc->vo_dig_out & IDE_OUT_IEN is true it polls waiting for completion,
*   otherwise it blocks waiting for the ISR to wake it. Each command has
*   a time out value in pc->timer.
*
*/
#if (IDE_USE_ATAPI)

static BOOLEAN ide_do_atapi_command(PIDE_CONTROLLER pc) /* __fn__ */
{
dword       words_remaining;
dword       words_to_transfer;
int         phys_drive;
byte *      save_useraddress;
int         loop_count=8;
word        utemp;
word     dummyWord;
dword    i;

    pc->error_code = 0;
    pc->command_complete = FALSE;


     /* The drive may be busy. If so, wait for it to complete */
    if (!ide_wait_not_busy(pc, pc->timer))
        return(ide_error(pc,IDE_ERC_BUS));                  /* bus is hung */

    /* There should be no pending interrupt complete signals
        so call os_ide_clear_signal(); to be sure we are not
        signalled until after an int occurs. */
    if (!(pc->vo_dig_out & IDE_OUT_IEN))
    {
        rtfs_port_clear_signal(pc->ide_signal);
    }

    phys_drive=(pc->vo_drive_head & 0x10)>>4;               /* GET PHYSICAL DRIVE */

    /* Now load the rest of the register file. (send command last) */
    if(pc->drive[phys_drive].media_descriptor==0xef)
    {
        ide_wr_drive_head(pc->register_file_address, (byte) ((pc->vo_drive_head|0xa0)&0xb0) );
    }
    else
        ide_wr_drive_head(pc->register_file_address, (byte) (pc->vo_drive_head|0xa0));                          /* 176 Atapi Drive Select */
     while((pc->vi_status = ide_rd_status(pc->register_file_address))&(IDE_STB_BUSY|IDE_STB_DRQ))   /* wait till busy off and drq off*/
     {
        pc->vi_sector_number = ide_rd_sector_number(pc->register_file_address);
        if(pc->vi_sector_number&IDE_IRR_CoD)
        {
           if(pc->vi_sector_number&IDE_IRR_IO)
               ide_rd_data(pc->register_file_address);               /* read a word  */
           else
               ide_wr_data(pc->register_file_address, 0x0000);      /* write a word */
         }
         else
         {
             if(loop_count)
                 loop_count--;
             else
             {
                 ide_wr_command(pc->register_file_address, IDE_CMD_SOFT_RESET);
                 loop_count=8;
              }
          }
     }
     if (pc->atapi_words_to_xfer > 65534)
        utemp = 65534;
     else
        utemp = (word) pc->atapi_words_to_xfer;


    ide_wr_cyl_low(pc->register_file_address,  (byte)utemp);        /* 174 Atapi Byte Count Register (bits 0-7) */
    ide_wr_cyl_high(pc->register_file_address, (byte) (utemp>>8));     /* 175 Atapi Byte Count Register (bits 8-15) */
    ide_wr_feature(pc->register_file_address, pc->vo_feature);/* 171 Atapi Features */

    /* Write the digital output register (interrupts on/off) */
     ide_wr_dig_out(pc->register_file_address, pc->contiguous_io_mode, (byte) (pc->vo_dig_out | 0x08));

    /* The register file is loaded.Now load the command into the command register. */
     ide_wr_command(pc->register_file_address, ATAPI_CMD_PKT);
     if(pc->drive[phys_drive].CMD_DRQ_type == 0x01)
     {
       /* DRQ type is == 1. drive interrupts when ready for packet */
        if (!(pc->vo_dig_out & IDE_OUT_IEN))    /* Interupt mode */
        {
            if (rtfs_port_test_signal(pc->ide_signal, pc->timer) != 0)
            {
                return(ide_error(pc,IDE_ERC_BUS));
            }
        }
        else
        {
            /* DRQ type is == 1. but we are not using interrupt */
            /* This is not guaranteed to always work */
            if (!ide_wait_not_busy(pc, pc->timer)) /* Polled mode */
                return(ide_error(pc,IDE_ERC_BUS));
         }
    }
    else
    {
        /* DRQ type is != 1. drive drops busy and asserts drq    */
        if (!(ide_wait_not_busy(pc, TIMEOUT_TYPICAL) && ide_wait_drq(pc, TIMEOUT_TYPICAL)))
        {
           return(ide_error(pc,IDE_ERC_BUS));
        }
    }

    /* DRQ should be set error should be clear */
    if(!(ide_rd_status(pc->register_file_address)&IDE_STB_DRQ)||(ide_rd_status(pc->register_file_address)&IDE_STB_ERROR))
    {
        return(ide_error(pc,IDE_ERC_BUS));
    }
    save_useraddress=pc->user_address;
    pc->user_address=(byte *)&pc->drive[phys_drive].atapi_packet;
/*      if((ide_rd_sector_count(pc)&0x03)!=0x01) */
/*          _asm int 3 */
    if(!ide_out_words(pc, sizeof(ATAPI_PACKET_DESC)>>1))            /* Packet size /2 */
        return(ide_error(pc,IDE_ERC_BUS));

    pc->user_address=save_useraddress;
    if(pc->drive[phys_drive].atapi_packet.generic.op_code==ATAPI_PKT_CMD_FORMAT_UNIT)
    {
        if(!ide_out_words(pc, sizeof(HIMAWARI_FORMAT_DESCRIPTOR)>>1))           /* Packet size /2 */
        {
            return(ide_error(pc,IDE_ERC_BUS));
        }
    }
    if (!(pc->vo_dig_out & IDE_OUT_IEN))    /* Interupt mode */
    {
        if (rtfs_port_test_signal(pc->ide_signal, pc->timer) != 0)
        {
            return(ide_error(pc,IDE_ERC_BUS));
        }
    }
    else
    {
        if (!ide_wait_not_busy(pc, pc->timer)) /* Polled mode */
            return(ide_error(pc,IDE_ERC_BUS));
    }
    if (ide_rd_status(pc->register_file_address)&IDE_STB_ERROR)    /* if error */
    {
        if (pc->drive[phys_drive].atapi_packet.generic.op_code!=ATAPI_PKT_CMD_TEST_UNIT_READY)
            return(ide_error(pc,IDE_ERC_STATUS));
    }
    words_remaining = pc->atapi_words_to_xfer;
    while(ide_rd_status(pc->register_file_address)&IDE_STB_DRQ)                        /* if DRQ then data to transfer */
    {
        words_to_transfer =(dword)(((ide_rd_cyl_low(pc->register_file_address))+(ide_rd_cyl_high(pc->register_file_address)<<8))>>1); /* number of words to transfer */
        if(words_remaining)
        {
            if(words_remaining<words_to_transfer) words_to_transfer=words_remaining;
            switch(ide_rd_sector_count(pc->register_file_address)&0x03)
            {
               case    0x00:                                           /* write to device */
                   ide_out_words(pc, (word)words_to_transfer);
                   break;
               case    0x02:                                           /* read from device */
                   ide_in_words(pc, (word)words_to_transfer);
                   break;
               case    0x01:
               case    0x03:
                   return(ide_error(pc,IDE_ERC_BUS));
            }
        }
        else
        {
            switch(ide_rd_sector_count(pc->register_file_address)&0x03)
            {
                case    0x00:
                    dummyWord = 0;
                    /* write to device */
                    for (i=0; i < words_to_transfer; i++)
                        ide_wr_data(pc->register_file_address,dummyWord);
                    break;
                case    0x02:                                           /* read from device */
                    for (i=0; i < words_to_transfer; i++)
                        dummyWord = ide_rd_data(pc->register_file_address);
                     break;
                 case    0x01:
                 case    0x03:
                    return(ide_error(pc,IDE_ERC_BUS));
             }
         }

         words_remaining -= words_to_transfer;
         if(words_remaining)
         {
             if (!(pc->vo_dig_out & IDE_OUT_IEN))    /* Interupt mode */
             {
                    if (rtfs_port_test_signal(pc->ide_signal, pc->timer)!=0)
                         return(ide_error(pc,IDE_ERC_BUS));
              }
              else
              {
                  if (!ide_wait_not_busy(pc, pc->timer)) /* Polled mode */
                         return(ide_error(pc,IDE_ERC_BUS));
              }
        }
    }
    if (ide_rd_status(pc->register_file_address)&IDE_STB_ERROR)    /* if error */
    {
        return(ide_error(pc,IDE_ERC_STATUS));
    }
    if (ide_rd_status(pc->register_file_address)&IDE_STB_DRQ)     /* if drq */
    {
         return(ide_error(pc,IDE_ERC_BUS));
    }

    return(TRUE);
}
#endif /* USE_ATAPI */


/*  ide_interrupt - Respond to the ide_disk interupt.
* Inputs:
*   uses values in cs (the control structure)
* Outputs:
*   pc->command_complete and pc->error_code are set. If error_code is non zero
*   the command failed.
* Returns:
*   Nothing
*
*   This routine is called by ide_isr or do_command if it is polling. Its job
*   is to
*   determine the cause of the IDE disk interrupt and process it correctly.
*   On multi block reads and writes it performs the data transfer and returns if
*   the multiblock transfer is incomplete. In other cases it checks the status
*   bits for errors. If no errors were found pc->error_code is zero other wise
*   pc->error_code is set to an appropriate value. If the driver is in interrupt
*   mode the task is woken up when command_complete is set.
*
*/

void ide_interrupt(int controller_number)                       /* __fn__ */
{
word utemp;
PIDE_CONTROLLER pc;

    pc = &controller_s[controller_number];

 /* Read the status register. This also clears the interrupt status if pending */
    pc->vi_status = ide_rd_status(pc->register_file_address);

 /* Read the sector count register. We will need it in reads and writes */
    pc->vi_sector_count = ide_rd_sector_count(pc->register_file_address);

  /* Check if the drive reported an error */
    if (pc->vi_status & IDE_STB_ERROR)
    {
    /* For READS and READM commands we need to read the data  */
    /* to put the controller in command mode */

        ide_read_register_file(pc);
        /* Report the error on all but diagnostipc-> The error bits
            are treated differently on this command. */
        if (pc->vo_command != IDE_CMD_DIAG)
        {
            pc->error_code = IDE_ERC_STATUS;
            goto io_done_label;
        }
    }
    /* =================================================================== */
    /* ================= Now process command completes==================== */
    /* =================================================================== */

    /* Reads and Write are treated differently from the rest of the commands.
        They may recieve multiple interrupts and the performance of this
        routine is very important. To pick up some performance we only read
        the register file entries that we need.
    */
/* Command::::: IDE_CMD_READM - read multiple, IDE_CMD_READS - read sector(s) */
    if((pc->vo_command == IDE_CMD_READM)||(pc->vo_command == IDE_CMD_READS)||
       (pc->vo_command == IDE_CMD_READM_EXT)||(pc->vo_command == IDE_CMD_READS_EXT))
    {
        if (pc->sectors_remaining >= (word) pc->block_size)
            utemp = (word) pc->block_size;
        else
            utemp = pc->sectors_remaining;
    /* Note : We could or compare the sector count register here. */

        if (!ide_in_words(pc, (word) (utemp << 8)))/* 256 times xfr size */
        {
            /* Serious problem. Bus state incorrect */
            pc->error_code = IDE_ERC_BUS;
            goto io_done_label;
        }
        pc->sectors_remaining = (word)(pc->sectors_remaining - utemp);

        /* see if we have completed transferring all of our blocks */
        if (pc->sectors_remaining)
            return;             /* NOPE - More to come */
        else
        {
            ide_read_register_file(pc);
            if (pc->vi_status & IDE_STB_ERROR)
                pc->error_code = IDE_ERC_STATUS;
            goto io_done_label;
        }
    }

/* Command::::: IDE_CMD_WRITEM IDE_CMS_WRITES - write multiple, write sectors */
    if ((pc->vo_command == IDE_CMD_WRITES) ||
        (pc->vo_command == IDE_CMD_WRITES_EXT) ||
        (pc->vo_command == IDE_CMD_WRITES_NE) ||
        (pc->vo_command == IDE_CMD_WRITEM) ||
        (pc->vo_command == IDE_CMD_WRITEM_EXT) ||
        (pc->vo_command == IDE_CMD_WRITEM_NE))
    {
        if (!pc->sectors_remaining)
        {
            /* Sector count reg is 0. we are done. We will read the register
                File in case we want to look at the values */
            ide_read_register_file(pc);
            goto io_done_label;
        }
    /* Not Done. We have to send more data */
        /* Note: blocks size is 1 for write sectors */
        if (pc->sectors_remaining >= (word) pc->block_size)
            utemp = (word) pc->block_size;
        else
            utemp = pc->sectors_remaining;
        ide_out_words(pc, (word) (utemp << 8));/* 256 times xfer size */
        pc->sectors_remaining = (word)(pc->sectors_remaining - utemp);
        return;     /* Return from the int with the command still valid. */
    }

    /* all other interrupts are not as performance sensitive so load the
        register file up */
    ide_read_register_file(pc);

    if(pc->vo_command == IDE_CMD_DIAG)
    {
#if (ZERO)
        /* Diagnostics return 01 on no error */
        if (pc->vi_error != 0x01)
                pc->error_code = IDE_ERC_DIAG;
        goto io_done_label;
#else
        /* Diagnostics return 01 on no error */
/*      if (pc->vi_error != 0x01)*/                                                     /* this fails if either drive fails*/
/*              pc->error_code = IDE_ERC_DIAG;*/
/* the following tests to see if the relavent drive failed */
        if(pc->vo_drive_head&0x10)
            {
            if ((pc->vi_error == 0x00)||(pc->vi_error == 0x81))                         /* drive 1 failure */
                pc->error_code = IDE_ERC_DIAG;
            }
        else
            if ((pc->vi_error != 0x01)&&(pc->vi_error != 0x81))                         /* drive 0 failure */
                pc->error_code = IDE_ERC_DIAG;
        goto io_done_label;
#endif
    }
    else
    {
        if ((pc->vo_command == IDE_CMD_IDENT) || (pc->vo_command == ATAPI_CMD_IDENT))   /* :::::- Identify drive completed */
        {
            if (!(pc->vi_status & IDE_STB_DRQ))     /* Drive should be requesting */
            {
                /* Serious problem. Bus state incorrect */
                pc->error_code = IDE_ERC_BUS;
                goto io_done_label;
            }
            ide_in_words(pc, 256); /* 256 times xfr size 1 block */
            ide_read_register_file(pc);
            if (pc->vi_status & IDE_STB_ERROR)
                pc->error_code = IDE_ERC_STATUS;
        }
    }

io_done_label:
 /* If it gets here the current command is complete. If pc->error_code is
    zero we had success */
    pc->command_complete = TRUE;
    return;
}


/* void ide_clear_voregs()
*
*  This routine clears the virtual output register file to a known state.
*  All commands first call this routine. This is done because they all
*  eventually send the vo_regs to the ide drive. We want them to be in a
*  known state when sent.
*
*  Note: We do not clear the digital output register. This is used to enable
*       interrupts on the drive. We do not want to effect this each time.
*
*  This routine is portable
*/

static void ide_clear_voregs(PIDE_CONTROLLER pc) /* __fn__ */
{
    pc->vo_sector_count  =  0;
    pc->vo_write_precomp =
    pc->vo_sector_number =
    pc->vo_cyl_low      =
    pc->vo_cyl_high     =
    pc->vo_drive_head   =
    pc->vo_feature      =
    pc->vo_command      =  0;
}

/* ide_wait_not_busy - Wait for the busy bit to deassert
*
*
*  This routine polls the busy bit in the alternate status register
*  until is clears. It uses a watchdog timer to guard against
*  latchup
*
*   Returns:
*       TRUE if the bit cleared
*       FALSE  if it timed out.
*
*/

BOOLEAN ide_wait_not_busy(PIDE_CONTROLLER pc,int millis) /* __fn__ */
{
byte alt_status;
dword time_zero;

    time_zero = rtfs_port_elapsed_zero();

    alt_status = ide_rd_alt_status(pc->register_file_address, pc->contiguous_io_mode);
    if (!(alt_status & IDE_STB_BUSY))
        return(TRUE);

    /* Set a watchdog to time out in millis seconds */
    while ((alt_status & IDE_STB_BUSY) && !rtfs_port_elapsed_check(time_zero, millis))
    {

        alt_status = ide_rd_alt_status(pc->register_file_address, pc->contiguous_io_mode);
    }
    if(alt_status & IDE_STB_BUSY)
    {
        pc->error_code      = IDE_ERC_TIMEOUT;
        return(FALSE);
    }
    else
        return(TRUE);
}


/* ide_wait_ready - Wait for the ready bit to assert
*
*
*  This routine polls the ready bit in the alternate status register
*  until is asserts. It uses a watchdog timer to guard against
*  latchup
*
*   Returns:
*       TRUE if the bit set

*       FALSE  if it timed out.
*
*/

BOOLEAN ide_wait_ready(PIDE_CONTROLLER pc,int millis) /* __fn__ */
{
byte alt_status;
dword time_zero;



    alt_status = ide_rd_alt_status(pc->register_file_address, pc->contiguous_io_mode);
    if (alt_status & IDE_STB_READY)
        return(TRUE);

    time_zero = rtfs_port_elapsed_zero();
    while(!(alt_status & IDE_STB_READY) && !rtfs_port_elapsed_check(time_zero, millis))
    {
        alt_status = ide_rd_alt_status(pc->register_file_address, pc->contiguous_io_mode);
    }
    if(!(alt_status & IDE_STB_READY))
    {
        pc->error_code      = IDE_ERC_TIMEOUT;
        return(FALSE);
    }
    else
        return(TRUE);
}




/* ide_wait_drq - Wait for the data request bit to assert
*
*
*  This routine polls the drq bit in the alternate status register
*  until it asserts. It uses a watchdog timer to guard against
*  latchup.
*
*   If the bit never sets ERC_BUS is set
*
*   Returns:
*       TRUE if the bit set
*       FALSE  if it timed out.
*
*/

BOOLEAN ide_wait_drq(PIDE_CONTROLLER pc, int millis) /* __fn__ */
{
byte alt_status;
int i;
dword time_zero;

    /* At first stay in a tight loop without a watch dog. This
        is because DRQ should come up fast. We do not want the overhead of
        setting a watchdog */
    alt_status = ide_rd_alt_status(pc->register_file_address, pc->contiguous_io_mode);
    for (i =0; i < 10000; i++)
    {

        if (alt_status & IDE_STB_DRQ)
            return(TRUE);
        alt_status = ide_rd_alt_status(pc->register_file_address, pc->contiguous_io_mode);
    }

    time_zero = rtfs_port_elapsed_zero();

    /* Set a watchdog to time out in millis seconds */
    while (!(alt_status & IDE_STB_DRQ) && !rtfs_port_elapsed_check(time_zero, millis))
    {
        alt_status = ide_rd_alt_status(pc->register_file_address, pc->contiguous_io_mode);
    }

    if(!(alt_status & IDE_STB_DRQ))
    {
        pc->error_code      = IDE_ERC_BUS;
        return(FALSE);

    }
    else
        return(TRUE);
}

/* void ide_read_register_file(pc)
*
*  This routine reads the register file into the vi_xxxx fields
*  in ide control structure. It is called after a drive interrupt
*  has occured and register file data is needed.
*
*/

static void ide_read_register_file(PIDE_CONTROLLER pc) /* __fn__ */
{
    pc->vi_error        = ide_rd_error(pc->register_file_address);
    pc->vi_sector_count  = ide_rd_sector_count(pc->register_file_address);
    pc->vi_sector_number = ide_rd_sector_number(pc->register_file_address);
    pc->vi_cyl_low      = ide_rd_cyl_low(pc->register_file_address);
    pc->vi_cyl_high     = ide_rd_cyl_high(pc->register_file_address);
    pc->vi_drive_head   = ide_rd_drive_head(pc->register_file_address);
    pc->vi_status       = ide_rd_status(pc->register_file_address);
    pc->vi_drive_addr   = ide_rd_drive_address(pc->register_file_address, pc->contiguous_io_mode);
}


#if (IDE_USE_ATAPI)
BOOLEAN ls120_format(DDRIVE *pdr);
#endif


int ide_perform_device_ioctl(int driveno, int opcode, void * pargs)
{
DDRIVE *pdr;
DEV_GEOMETRY gc;        /* used by DEVCTL_GET_GEOMETRY */
PIDE_CONTROLLER pc;


    pdr = pc_drno_to_drive_struct(driveno);
    if (!pdr)
        return(-1);
    pc = &controller_s[pdr->controller_number];

    switch (opcode)
    {
        case DEVCTL_GET_GEOMETRY:

        rtfs_memset(&gc, 0, sizeof(gc));
        gc.dev_geometry_heads       =       pc->drive[pdr->logical_unit_number].num_heads;
        gc.dev_geometry_cylinders   =       pc->drive[pdr->logical_unit_number].num_cylinders;
        gc.dev_geometry_secptrack   =       pc->drive[pdr->logical_unit_number].sec_p_track;
        gc.dev_geometry_lbas        =       pc->drive[pdr->logical_unit_number].total_lba;
        copybuff(pargs, &gc, sizeof(gc));
        return (0);

        case DEVCTL_FORMAT:
#if (IDE_USE_ATAPI)
        if(pc->drive[pdr->logical_unit_number].protocol==2)
        {
            if (!ls120_format(pdr))
                return(-1);
        }
#endif
        return (0);

        case DEVCTL_REPORT_REMOVE:

        pdr->drive_flags &= ~DRIVE_FLAGS_INSERTED;
        /* Close out the drive so we re-open */
        pc->drive[pdr->logical_unit_number].open_count = 0;
        return(0);

        case DEVCTL_CHECKSTATUS:
#if (IDE_USE_ATAPI)
              if (pdr->drive_flags & DRIVE_FLAGS_CDFS)
            {
                if(simple_atapi_command(ATAPI_PKT_CMD_TEST_UNIT_READY, pc, pdr->logical_unit_number))
                {
                    pdr->drive_flags |= DRIVE_FLAGS_INSERTED;
                    return(DEVTEST_NOCHANGE);
                }
                else
                {
                    return(DEVTEST_NOMEDIA);
                }
            }
#endif
           if (!(pdr->drive_flags & DRIVE_FLAGS_REMOVABLE))
                return(DEVTEST_NOCHANGE);
/* Check if the drive is not present or if a swap has occurred. If TRUE
   make sure the drive is shut down (simulate a media removal event)
   Fall through, If the device is physically present and the card is
   not currently mounted, then code farther down will mount it.
   This section of code provides support for removable media even if
   a card removal interrupt is not available */
#if (INCLUDE_COMPACT_FLASH)
            if (pdr->drive_flags & DRIVE_FLAGS_PCMCIA)
            {
                if (!pcmctrl_card_installed(pdr->pcmcia_slot_number) ||
                    pcmctrl_card_changed(pdr->pcmcia_slot_number))
                {
                    pcmctrl_card_down(pdr->pcmcia_slot_number);
                    pdr->drive_flags &= ~DRIVE_FLAGS_INSERTED;
                    pc->drive[pdr->logical_unit_number].open_count = 0;
                }
            }
            else
#endif
            {
               if (!trueide_card_installed(pdr)  ||
                    trueide_card_changed(pdr))
               {
                    pdr->drive_flags &= ~DRIVE_FLAGS_INSERTED;
                    pc->drive[pdr->logical_unit_number].open_count = 0;
               }
            }
            if (pdr->drive_flags & DRIVE_FLAGS_INSERTED)
                return(DEVTEST_NOCHANGE);
            /* If the drive is open but the inserted is clear
               that means another partition accessed the drive
               and succeed so return CHANGED to force a remount
               with no low level drive initialization */
            if (pc->drive[pdr->logical_unit_number].open_count)
            {
                pdr->drive_flags |= DRIVE_FLAGS_INSERTED;
                return(DEVTEST_CHANGED);
            }
#if (IDE_USE_ATAPI)
            /* If its an atapi drive do a get media status and check the media changed
            bit in bit 5 */
            if(pc->drive[pdr->logical_unit_number].protocol==2)
            {
                /* Do a media status call - if it fails try it again.
                needed by some LS-120 drives */
                if (ide_command(IDE_CMD_GET_MEDIA_STATUS, pc, pdr->logical_unit_number, 0, 0))
                    if (ide_command(IDE_CMD_GET_MEDIA_STATUS, pc, pdr->logical_unit_number, 0, 0))
                        return(DEVTEST_NOMEDIA);

                /* If the user accepted a media change.. do an eject */
                if (pc->vi_error & 0x08)
                {
                    simple_atapi_command(ATAPI_PKT_CMD_START_STOP_UNIT, pc, pdr->logical_unit_number);
                }

                /* This is not right. needs minor fixes */
                if (pc->vi_error & 0x20)
                {
                    pdr->drive_flags |= DRIVE_FLAGS_INSERTED;
                    return(DEVTEST_CHANGED);
                }
                else
                    return(DEVTEST_NOMEDIA);
            }
#endif
#if (INCLUDE_COMPACT_FLASH)
            else if (pdr->drive_flags & DRIVE_FLAGS_PCMCIA)
            {
                if (!pcmctrl_card_installed(pdr->pcmcia_slot_number))
                {
                    pdr->drive_flags &= ~DRIVE_FLAGS_INSERTED;
                    return(DEVTEST_NOMEDIA);
                }
                if (pcmcia_card_is_ata(
                    pdr->pcmcia_slot_number,
                    pdr->register_file_address,
                    pdr->interrupt_number,
                    pdr->pcmcia_cfg_opt_value) )

                {
                    if (!ide_drive_open(pdr))
                        return(DEVTEST_UNKMEDIA);
                    else
                    {
                        pdr->drive_flags |= DRIVE_FLAGS_INSERTED;
                        return(DEVTEST_CHANGED);
                    }
                }
                else
                    return(DEVTEST_UNKMEDIA);
            }
#endif /* INCLUDE_COMPACT_FLASH */
            else
            {
                if (!trueide_card_installed(pdr))
                {
                    pdr->drive_flags &= ~DRIVE_FLAGS_INSERTED;
                    return(DEVTEST_NOMEDIA);
                }
                else
                {
                    if (!ide_drive_open(pdr))
                        return(DEVTEST_UNKMEDIA);
                    else
                    {
                        pdr->drive_flags |= DRIVE_FLAGS_INSERTED;
                        return(DEVTEST_CHANGED);
                    }
                }
        }
/*            break; */
        case DEVCTL_WARMSTART:
#ifndef INCLUDE_V_1_0_DEVICES
#define INCLUDE_V_1_0_DEVICES 0
#endif

#if (INCLUDE_V_1_0_DEVICES)
            if (!prtfs_cfg->ide_semaphore)
                prtfs_cfg->ide_semaphore = rtfs_port_alloc_mutex("ide");
#else
            if (!prtfs_cfg->ide_semaphore)
                prtfs_cfg->ide_semaphore = rtfs_port_alloc_mutex();
#endif
            if (!prtfs_cfg->ide_semaphore)
                return(-1);
            if (pc->init_signature != CONTROLLER_INITIALIZED)    /* Only initialize the controller once */
            {
                pc->init_signature = CONTROLLER_INITIALIZED;
                pc->controller_number = pdr->controller_number;
                /* This is the IO address of the register file   */
                pc->register_file_address = pdr->register_file_address;

                pc->interrupt_number = pdr->interrupt_number;

                /* Clear IEN bar if interrupts are requested: Otherwise we poll   */
                if (pc->interrupt_number >= 0)
                    pc->vo_dig_out = 0;
                else
                    pc->vo_dig_out = IDE_OUT_IEN;

                /* Set up OS operating layer and interrupt service routines   */
                if (pc->interrupt_number >= 0)
                {
                    pc->ide_signal = rtfs_port_alloc_signal();                  /* Signal for IO completion */
                    if (!pc->ide_signal)
                        return(-1);

                    hook_ide_interrupt(pc->interrupt_number, pc->controller_number);
                    /* Clear the signal */
                    rtfs_port_clear_signal(pc->ide_signal);
                }
            }
            pc->contiguous_io_mode = 0;
#if (IDE_USE_ATAPI)
            if (pdr->drive_flags & DRIVE_FLAGS_CDFS)
            {
              if (ide_drive_open(pdr))
              {
                  /* Call back to the cdfs porting layer to tell CDFS what
                   drive number to use when calling the ERTFS atapi driver */
                register_cdfs_drivenumber(driveno);
                pdr->drive_flags |= DRIVE_FLAGS_VALID;
                pdr->drive_flags |= DRIVE_FLAGS_REMOVABLE;
                return(0);
              }
              else
                return(-1);
            }
#endif
#if (INCLUDE_COMPACT_FLASH)
            if (pdr->drive_flags & DRIVE_FLAGS_PCMCIA)
            {
                pcmctrl_init();
                /* Note that we are initialized */
                pdr->drive_flags |= DRIVE_FLAGS_VALID;
                pdr->drive_flags |= DRIVE_FLAGS_REMOVABLE;
                /* 40 is memory mapped, 41 is contiguous IO mode */
                if (pdr->pcmcia_cfg_opt_value  == 0x41 || pdr->pcmcia_cfg_opt_value  == 0x40)
                    pc->contiguous_io_mode = 1;
            }
            else
#endif
            {   /* Support TRUEIDE removable devices */
                if (pdr->drive_flags & DRIVE_FLAGS_REMOVABLE)
                {
                    /* Removable drive - set valid but don't open
                      the drive, open on first access  */
                    pdr->drive_flags &= ~DRIVE_FLAGS_INSERTED;
                    pdr->drive_flags |= DRIVE_FLAGS_VALID;
                }
                else
                {
                    /* It is a fixed drive so try to open it */
                    if (!ide_drive_open(pdr))
                        return(-1);
                    else
                        pdr->drive_flags |= DRIVE_FLAGS_VALID;
                }
            }
            return(0);
            /* Fall through */
        case DEVCTL_POWER_RESTORE:
            /* Fall through */
        case DEVCTL_POWER_LOSS:
            /* Fall through */
        default:
            break;
    }
    return(0);
}

/* BOOLEAN trueide_card_changed(DDRIVE *pdr)
*
* Modify this routine to support removable trueide devices
*
* This routine may be used to provide support for hot swapping of removable
* TRUEIDE devices in cases where a removal interrupt source is not
* available.
*
* The TRUEIDE circuit must provide a latch that detects a card removal.
* This routine must report the value of the latch, TRUE if the media has
* changed, FALSE if it has not. It must clear the latch before it returns.
*
*
* By default trueide_card_changeed() returns FALSE, emulating a fixed
* disk or a removable disk with no media changed latch.
*
* Note: The DRIVE_FLAGS_REMOVABLE flag must be set in apiinit.c for
* removable media. To do this or in DRIVE_FLAGS_REMOVABLE to the
* drive flags field.
*   pdr->drive_flags |= DRIVE_FLAGS_REMOVABLE;
*
*
* Example:
*
* BOOLEAN trueide_card_changed(DDRIVE *pdr)
* {
*    if (read_ide_change_latch() == 1)
*    {
*       clear_ide_change_latch();
*       return(TRUE);
*    }
*    else
*       return(FALSE);
* }
*
*/

BOOLEAN trueide_card_changed(DDRIVE *pdr)
{
       RTFS_ARGSUSED_PVOID((void *) pdr);
       return(FALSE);
}

/* BOOLEAN trueide_card_installed(DDRIVE *pdr)
*
* Modify this routine to support removable trueide devices
*
* trueide_card_installed() must return TRUE if IDE compatible media is
* installed, FALSE if it is not.
*
* By default trueide_card_installed() returns TRUE, emulating a fixed
* disk.
*
* Note: The DRIVE_FLAGS_REMOVABLE flag must be set in apiinit.c for
* removable media. To do this or in DRIVE_FLAGS_REMOVABLE to the
* drive flags field.
*   pdr->drive_flags |= DRIVE_FLAGS_REMOVABLE;
*
* To support removable trueide media you must modify this function to
* interface with your trueide media detect circuit. If media is installed
* it must return TRUE, if not it must return FALSE.
*
* Example:
*
* BOOLEAN trueide_card_installed(DDRIVE *pdr)
* {
*    if (read_ide_installed_latch() == 1)
*       return(TRUE);
*    else
*       return(FALSE);
* }
*
*/

BOOLEAN trueide_card_installed(DDRIVE *pdr)
{
    RTFS_ARGSUSED_PVOID((void *) pdr);

    return(TRUE);
}

/* void trueide_report_card_removed(int driveno)
*
* To support removable trueide media you must modify your media change
* interrupt service routine to call this function when a card has been
* removed.
*
*
* Drive number of the card that was removed must be passed in.
*
* The drive number must be the same as the value assigned to the
* pdr->driveno in apiinit.c.
*
* Note: The DRIVE_FLAGS_REMOVABLE flag must be set in apiinit.c for
* removable media. To do this or in DRIVE_FLAGS_REMOVABLE to the
* drive flags field.
*   pdr->drive_flags |= DRIVE_FLAGS_REMOVABLE;
*
* Example:
* #define TRUEIDE_DRIVEID 2 -- C:
*
* void trueide_removal_interrupt(void)
* {
*   trueide_report_card_removed(TRUEIDE_DRIVEID);
* }
*
*/

void trueide_report_card_removed(int driveno)
{
int j;
DDRIVE *pdr;

    for (j = 0; j < prtfs_cfg->cfg_NDRIVES; j++)
    {
        pdr = pc_drno_to_drive_struct(j);
        if (pdr && pdr->driveno == driveno)
        {
            pdr->dev_table_perform_device_ioctl(pdr->driveno,
                                              DEVCTL_REPORT_REMOVE,
                                              (void *) 0);
            break;
        }
    }
}



#if (IDE_USE_ATAPI)

BOOLEAN ls120_format(DDRIVE *pdr)               /* format atapi unit */
{
HIMAWARI_FORMAT_DESCRIPTOR  format_descriptor;
PIDE_CONTROLLER pc;

    pc = &controller_s[pdr->controller_number];

    format_descriptor.dlheader.reserved=0x00;
    format_descriptor.dlheader.control_byte=0x80;
    format_descriptor.dlheader.length_msb=0x00;
    format_descriptor.dlheader.length_lsb=0x08;
    format_descriptor.capacity_descriptor.reserved=0x00;
    format_descriptor.capacity_descriptor.block_length[0]=0x00;     /* set default block length to 512 bytes */
    format_descriptor.capacity_descriptor.block_length[1]=0x02;
    format_descriptor.capacity_descriptor.block_length[2]=0x00;
    switch(pc->drive[pdr->logical_unit_number].medium_type_code)                    /*                         Bytes/  Sectors/             */
    {                                                           /*                  Cyl H  Sector   Track   Capacity    */
        case    0x31:                                               /* Unformatted UHD  963 8    512      32    120Mb */
        case    0x30:                                               /* Formatted UHD    963 8    512      32    120Mb */
            format_descriptor.capacity_descriptor.number_of_blocks[0]=0x00; /* set number of blocks to 246,528 */
            format_descriptor.capacity_descriptor.number_of_blocks[1]=0x03;
            format_descriptor.capacity_descriptor.number_of_blocks[2]=0xc3;
            format_descriptor.capacity_descriptor.number_of_blocks[3]=0x00;
            break;
        case    0x20:                                               /* Unformatted HD    80 2    512      18    1.44Mb*/
        case    0x24:                                               /* Formatted HD      80 2    512      18    1.44Mb*/
            format_descriptor.capacity_descriptor.number_of_blocks[0]=0x00;  /* set number of blocks to  2880 */
            format_descriptor.capacity_descriptor.number_of_blocks[1]=0x00;
            format_descriptor.capacity_descriptor.number_of_blocks[2]=0x0b;
            format_descriptor.capacity_descriptor.number_of_blocks[3]=0x40;
            break;
        case    0x26:                                               /* DMF               80 2    512      21    1.7Mb*/
            format_descriptor.capacity_descriptor.number_of_blocks[0]=0x00; /* set number of blocks to  3360 */
            format_descriptor.capacity_descriptor.number_of_blocks[1]=0x00;
            format_descriptor.capacity_descriptor.number_of_blocks[2]=0x0d;
            format_descriptor.capacity_descriptor.number_of_blocks[3]=0x20;
            break;
        case    0x22:                                               /* Formatted NEC     77 2   1024       8    1.20Mb*/
            format_descriptor.capacity_descriptor.block_length[0]=0x00;     /* set default length to 1024 bytes */
            format_descriptor.capacity_descriptor.block_length[1]=0x04;
            format_descriptor.capacity_descriptor.block_length[2]=0x00;
            format_descriptor.capacity_descriptor.number_of_blocks[0]=0x00; /* set number of blocks to  1232 */
            format_descriptor.capacity_descriptor.number_of_blocks[1]=0x00;
            format_descriptor.capacity_descriptor.number_of_blocks[2]=0x04;
            format_descriptor.capacity_descriptor.number_of_blocks[3]=0xd0;
        case    0x23:                                               /* Formatted Toshiba 80 2    512      15    1.20Mb*/
            break;
        case    0x27:                                               /* NEC-DMF           77 2    512       9    720Kb*/
            format_descriptor.capacity_descriptor.number_of_blocks[0]=0x00; /* set number of blocks to  1386 */
            format_descriptor.capacity_descriptor.number_of_blocks[1]=0x00;
            format_descriptor.capacity_descriptor.number_of_blocks[2]=0x05;
            format_descriptor.capacity_descriptor.number_of_blocks[3]=0x6a;
            break;
        case    0x11:                                               /* Formatted DD      80 2    512       9    720Kb*/
        case    0x10:                                               /* Unformatted DD    80 2    512       9    720Kb*/
            format_descriptor.capacity_descriptor.number_of_blocks[0]=0x00; /* set number of blocks to  1440 */
            format_descriptor.capacity_descriptor.number_of_blocks[1]=0x00;
            format_descriptor.capacity_descriptor.number_of_blocks[2]=0x05;
            format_descriptor.capacity_descriptor.number_of_blocks[3]=0xa0;
            break;
        default:
            return(FALSE);
    }

    pc->user_address=(byte *)&format_descriptor;
         switch(pc->drive[pdr->logical_unit_number].medium_type_code)                  /*                         Bytes/  Sectors/             */
     {                                                           /*                  Cyl H  Sector   Track   Capacity    */
            case    0x22: /* Formatted NEC     77 2   1024       8    1.20Mb*/
            case    0x23: /* Formatted Toshiba 80 2    512      15    1.20Mb*/
            case    0x26: /* DMF               80 2    512      21    1.7Mb*/
            case    0x27:  /* NEC-DMF           77 2    512       9    720Kb*/
            case    0x30:  /* Formatted UHD    963 8    512      32    120Mb */
            case    0x31:  /* Unformatted UHD  963 8    512      32    120Mb */
                pc->drive[pdr->logical_unit_number].atapi_packet.format.op_code=ATAPI_PKT_CMD_FORMAT_UNIT;
                pc->drive[pdr->logical_unit_number].atapi_packet.format.defect_list_format=0x17;
                pc->drive[pdr->logical_unit_number].atapi_packet.format.track_number=0x01;
                break;
            case    0x11:    /* Formatted DD      80 2    512       9    720Kb*/
            case    0x10:    /* Unformatted DD    80 2    512       9    720Kb*/
                pc->drive[pdr->logical_unit_number].atapi_packet.old_format.medium=0x11;
                pc->drive[pdr->logical_unit_number].atapi_packet.old_format.op_code=ATAPI_PKT_CMD_OLD_FORMAT_UNIT;
                pc->drive[pdr->logical_unit_number].atapi_packet.old_format.control_byte=0x21;
                break;
            case    0x20:    /* Unformatted HD    80 2    512      18    1.44Mb*/
            case    0x24:    /* Formatted HD      80 2    512      18    1.44Mb*/
                pc->drive[pdr->logical_unit_number].atapi_packet.old_format.medium=0x24;
                pc->drive[pdr->logical_unit_number].atapi_packet.old_format.op_code=ATAPI_PKT_CMD_OLD_FORMAT_UNIT;
                pc->drive[pdr->logical_unit_number].atapi_packet.old_format.control_byte=0x21;
                break;
            default:
                return(FALSE);

    }
    pc->vo_drive_head  |= 0x20;                 /* Bit five is always one */
    if (pdr->logical_unit_number)                             /* Bit four is drive (1 or 0) */
        pc->vo_drive_head |= 0x10;              /* select device 1 (slave) */

    /* Call the processing routine */
    pc->atapi_words_to_xfer = 0;
    return(ide_do_atapi_command(pc));
}


BOOLEAN simple_atapi_command(byte command, PIDE_CONTROLLER pc, int logical_unit_number)
{

    rtfs_memset(&pc->drive[logical_unit_number].atapi_packet, 0, sizeof(ATAPI_PACKET));
    pc->drive[logical_unit_number].atapi_packet.generic.op_code = command;
    pc->timer = (word)TIMEOUT_ATAPI_SPINUP;
    ide_clear_voregs(pc);               /* Clear virtual output registers */
     /* Call ide_rdwr_setup to set transfer addresses, sector_count, sector_number,
        cylinder_low, cylinder_hi and drive_head */
    switch(command)
    {
        case    ATAPI_PKT_CMD_START_STOP_UNIT:
                /* Eject the disk if possible */
                pc->drive[logical_unit_number].atapi_packet.start_stop.eject = 0x02;
                break;
        case    ATAPI_PKT_CMD_TEST_UNIT_READY:
        case    ATAPI_PKT_CMD_MODE_SENSE:
        case    ATAPI_PKT_CMD_REQUEST_SENSE:
        case    ATAPI_PKT_CMD_RDVTOC:
        case    ATAPI_PKT_CMD_RDAUDIO:
            break;
        default:
            RTFS_PRINT_STRING_1((byte *)"Unknown ATAPI Command Issued", PRFLG_NL); /* "Unknown ATAPI Command Issued" */
            return (FALSE);
    }

    pc->vo_drive_head  |= 0x20;                 /* Bit five is always one */
    if (logical_unit_number)                             /* Bit four is drive (1 or 0) */
        pc->vo_drive_head |= 0x10;              /* select device 1 (slave) */

    /* Call the processing routine */
    pc->atapi_words_to_xfer = 0;
    return(ide_do_atapi_command(pc));
}


#endif /* (IDE_USE_ATAPI) */

#if (INCLUDE_UDMA)

#define DMA_TRANSFER_DESCRIPTORS 16 /* Number of Physical Region Descriptors
                                      allocated per controller. See below
                                      for discussion on setting this. */
/* DMA_TRANSFER_DESCRIPTORS: Divide the maximum desired transfer size by 64K,
   truncate the result and add 1. Example: 256K/64K == 4, then add 1, so set
   to 5. Example: 255K/64K == 3.98. Truncate to 3, then add 1, so set to 4.
   There must be at least 2 DMA_TRANSFER_DESCRIPTORS, which will handle DMA
   transfers up to 128 sectors, (64K). Each additional descriptor will handle
   an additional 128 sectors. There must be at least 2 to allow a transfer to
   straddle a 64K boundry.
*/
byte   dma_descriptors_array[N_ATA_CONTROLLERS][8+DMA_TRANSFER_DESCRIPTORS*sizeof(PHYSICAL_REGION_DESCRIPTOR)];
BOOLEAN ide_set_dma_mode(DDRIVE *pdr, word *pbuff)
{
    word w;
    word *pw;
    PIDE_CONTROLLER pc;
    BOOLEAN dma_mode;
    dword  workPtr;
    byte *p;

    pc = &controller_s[pdr->controller_number];

    /* Initialize Bus Mastering Controller for Ultra DMA */
    /* Returns zero on failure */
    pc->bus_master_address = rtfs_port_ide_bus_master_address(pdr->controller_number);
    if (!pc->bus_master_address)
        return(FALSE);

    /* Compiler may warn because casting a pointer to a dword.
       dma_descriptors_array must be in 32 bit address space
       PHYSICAL_REGION_DESCRIPTOR data type must be packed.
       The expression below:
        pc->dma_descriptor_bus_address =  rtfs_port_bus_address(p);
       converts the logical address to a bus address.
    */
    p = &dma_descriptors_array[pdr->controller_number][0];
    workPtr = (dword) p;
    while(workPtr&0x00000007)
    {
       workPtr++; p++;
    }
    pc->dma_descriptor_base = (PHYSICAL_REGION_DESCRIPTOR_PTR) p;
    pc->dma_descriptor_bus_address =  rtfs_port_bus_address(p);

    dma_mode = FALSE;
    pw = pbuff + 53;
    w = to_WORD((byte *) pw);
    if(w&0x0004)    /* if word 88 is valid */
    {
        pw = pbuff + 88;
        w = to_WORD((byte *) pw);
        if(w&0x0010)
        {
            if(ide_detect_80_cable(pdr->controller_number))
            {
                if(w&0x0040) /* Ultra DMA mode 6, 133 MB/s, is supported */
                {
                    if(ide_set_features(pc, pdr->logical_unit_number, IDE_FEATURE_SET_TRANSFER_MODE, 0x46))
                        dma_mode = TRUE;
                }
                else if(w&0x0020) /* Ultra DMA mode 5, 100 MB/s, is supported */
                {
                    if(ide_set_features(pc, pdr->logical_unit_number, IDE_FEATURE_SET_TRANSFER_MODE, 0x45))
                        dma_mode = TRUE;
                }
                else if(w&0x0010) /* Ultra DMA mode 4, 66 MB/s, is supported */
                {
                    if(ide_set_features(pc, pdr->logical_unit_number, IDE_FEATURE_SET_TRANSFER_MODE, 0x44))
                        dma_mode = TRUE;
                }
                else if(w&0x0008) /* Ultra DMA mode 3, 44 MB/s, is supported */
                {
                    if(ide_set_features(pc, pdr->logical_unit_number, IDE_FEATURE_SET_TRANSFER_MODE, 0x43))
                        dma_mode = TRUE;
                }
            }
            if(!dma_mode && (w&0x0004)) /* Ultra DMA mode 2, 33 MB/s, is supported */
            {
                if(ide_set_features(pc, pdr->logical_unit_number, IDE_FEATURE_SET_TRANSFER_MODE, 0x42))

                    dma_mode = TRUE;
            }
            else if(!dma_mode && (w&0x0002)) /* Ultra DMA mode 1, 25 MB/s, is supported */
            {
                if(ide_set_features(pc, pdr->logical_unit_number, IDE_FEATURE_SET_TRANSFER_MODE, 0x41))
                    dma_mode = TRUE;
            }
            else if(!dma_mode && (w&0x0001)) /* Ultra DMA mode 0, 16 MB/s, is supported */
            {
                if(ide_set_features(pc, pdr->logical_unit_number, IDE_FEATURE_SET_TRANSFER_MODE, 0x40))
                    dma_mode = TRUE;
            }
        }
    }
    return(dma_mode);
}

BOOLEAN ide_dma_io(DDRIVE *pdr, PIDE_CONTROLLER pc, dword sector, void *buffer, word count, BOOLEAN reading)    /*__fn__*/
{
PHYSICAL_REGION_DESCRIPTOR_PTR  prdp;
word  descriptorIndex=0;
dword bytesRemaining;
dword workAddress;
dword countToBoundary;

    /* Set up a counter for data transfer (See ide_isr) */
    pc->sectors_remaining = count;
    bytesRemaining = count<<9;   /* Compute number of Bytes */
    workAddress = rtfs_port_bus_address(buffer);

    /* check for odd buffer pointer */
    if(workAddress & 1)   /* if src/dest buffer address is odd */
        return(FALSE);

    prdp = pc->dma_descriptor_base;
    while(bytesRemaining && (descriptorIndex < DMA_TRANSFER_DESCRIPTORS))
    {
        prdp->base_address = workAddress;
        prdp->reserved = 0x00;
        prdp->eot = 0x00;   /* (field may have 0x80 from last read/write) */
        countToBoundary = ((workAddress&0xffff0000L)+0x00010000L) - workAddress; /*bytes to next 64K boundary */
        if(bytesRemaining <= countToBoundary)
            prdp->byte_count = (word)bytesRemaining;  /* (word typecast takes only lower 16 bits) */
        else
            prdp->byte_count = (word)countToBoundary;
        bytesRemaining -= prdp->byte_count;
        workAddress += prdp->byte_count;
        if(!bytesRemaining)
            prdp->eot = 0x80;
        prdp++;
        descriptorIndex++;
    }
    /* If bytesRemaining == 0 use Ultra DMA. Otherwise not enough */
    /* descriptors, so do io the old fashioned way (down below). */
    if(bytesRemaining)
        return(FALSE);
    if(reading)
    {
        if(pc->drive[pdr->logical_unit_number].supports_48bits &&
           (sector > 0xfffffff || count > 0xff)) /* sector > 28 bits or count > 8 bits */
            return(ide_command(IDE_CMD_DMA_READ_EXT, pc, pdr->logical_unit_number, sector, count));
        else
            return(ide_command(IDE_CMD_DMA_READ, pc, pdr->logical_unit_number, sector, count));
    }
    else
    {
        if(pc->drive[pdr->logical_unit_number].supports_48bits &&
           (sector > 0xfffffff || count > 0xff)) /* sector > 28 bits or count > 8 bits */
            return(ide_command(IDE_CMD_DMA_WRITE_EXT, pc, pdr->logical_unit_number, sector, count));
        else
            return(ide_command(IDE_CMD_DMA_WRITE, pc, pdr->logical_unit_number, sector, count));
    }
}

static BOOLEAN ide_do_dma_command(PIDE_CONTROLLER pc) /* __fn__ */
{
    byte        bus_master_status;

    bus_master_status = ide_rd_udma_status(pc->bus_master_address);
    if(pc->vo_drive_head&0x10)       /* if slave drive */
        bus_master_status |= 0x46;    /* set drive 1 dma capable clear error and interrupt */
    else
        bus_master_status |= 0x26;    /* set drive 0 dma capable clear error and interrupt */

    ide_wr_udma_address(pc->bus_master_address, pc->dma_descriptor_bus_address);

    ide_wr_udma_status(pc->bus_master_address,bus_master_status);
    if(pc->vo_command == IDE_CMD_DMA_WRITE)
        ide_wr_udma_command(pc->bus_master_address, 0x00); /* Prepare for DMA Write */
    else
        ide_wr_udma_command(pc->bus_master_address, 0x08); /* Prepare DMA Read */
    ide_wr_command(pc->register_file_address, pc->vo_command);
    if(pc->vo_command == IDE_CMD_DMA_WRITE)
        ide_wr_udma_command(pc->bus_master_address, 0x01); /* start DMA Write */
    else
        ide_wr_udma_command(pc->bus_master_address, 0x09); /* start DMA Read */
    if (rtfs_port_test_signal(pc->ide_signal, pc->timer)==0)
        pc->command_complete = TRUE;
    ide_wr_udma_command(pc->bus_master_address, (byte) (ide_rd_udma_command(pc->bus_master_address)&~1)); /* stop DMA transfer */
    bus_master_status = ide_rd_udma_status(pc->bus_master_address);
    if (!pc->command_complete || !(bus_master_status&0x04))
    {
        pc->error_code      = IDE_ERC_TIMEOUT;
        pc->command_complete = TRUE;            /* Shesssaaa hung */
        return(FALSE);
    }
    return(TRUE);
}
#endif /* INCLUDE_UDMA */
#endif /* (INCLUDE_IDE) */
