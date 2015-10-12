/*---------------------------------------------------------------------------*/
#ifndef smilH
#define smilH
/*---------------------------------------------------------------------------*/
/***************************************************************************
 ***************************************************************************/
#define K_BYTE   1024    /* Kilo Byte */
#define SECTSIZE  512    /* Sector buffer size */
#define REDTSIZE   16    /* Redundant buffer size */
/***************************************************************************/
#define DUMMY_DATA 0xFF  /* Non Data */
/***************************************************************************
 Max Zone/Block/Sectors Data Definition
 ***************************************************************************/
#define MAX_ZONENUM  0x01    /* Max Zone Numbers in a SmartMedia */
#define MAX_BLOCKNUM 0x0400  /* Max Block Numbers in a Zone */
#define MAX_SECTNUM  0x20    /* Max Sector Numbers in a Block */
#define MAX_LOGBLOCK 1000    /* Max Logical Block Numbers in a Zone */
/***************************************************************************/
#define CIS_SEARCH_SECT 0x08 /* Max CIS Search Sector Number */
/***************************************************************************
 Logical to Physical Block Table Data Definition
 ***************************************************************************/
#define NO_ASSIGN 0xFFFF     /* No Logical Block Address Assigned */
/***************************************************************************
 'SectCopyMode' Data
 ***************************************************************************/
#define COMPLETED 0  /* Sector Copy Completed */
#define REQ_ERASE 1  /* Request Read Block Erase */
#define REQ_FAIL  2  /* Request Read Block Failed */
/***************************************************************************
 Retry Counter Definition
 ***************************************************************************/
#define RDERR_REASSIGN 1 /* Reassign with Read Error */
#define L2P_ERR_ERASE  1 /* BlockErase for Contradicted L2P Table */
/***************************************************************************
 SmartMedia Command & Status Definition
 ***************************************************************************/
/* SmartMedia Command */
#define WRDATA    0x80
#define READ      0x00
#define READ_REDT 0x50
#define READ1     0x00
#define READ2     0x01
#define READ3     0x50
#define RST_CHIP  0xFF
#define WRITE     0x10
#define ERASE1    0x60
#define ERASE2    0xD0
#define RDSTATUS  0x70
#define READ_ID   0x90
/* SmartMedia Status */
#define WR_FAIL   0x01 /* 0:Pass, 1:Fail */
#define SUSPENDED 0x20 /* 0:Not Suspended, 1:Suspended */
#define READY     0x40 /* 0:Busy, 1:Ready */
#define WR_PRTCT  0x80 /* 0:Protect, 1:Not Protect */
/***************************************************************************
 Redundant Area Data
 The redundant area data buffer is used when writing or reading a SmartCard.
 This area is composed of 16 bytes. Bytes 0 to 3 are reserved for future use.
 The bytes currently used are shown below:
 ***************************************************************************/
#define REDT_DATA   0x04
#define REDT_BLOCK  0x05
#define REDT_ADDR1H 0x06
#define REDT_ADDR1L 0x07
#define REDT_ADDR2H 0x0B
#define REDT_ADDR2L 0x0C
#define REDT_ECC10  0x0D
#define REDT_ECC11  0x0E
#define REDT_ECC12  0x0F
#define REDT_ECC20  0x08
#define REDT_ECC21  0x09
#define REDT_ECC22  0x0A
/***************************************************************************
 SmartMedia Model & Attribute
 ***************************************************************************/
/* SmartMedia Attribute */
#define NOWP    0x00 /* 0... .... No Write Protect */
#define WP      0x80 /* 1... .... Write Protected */
#define MASK    0x00 /* .00. .... NAND MASK ROM Model */
#define FLASH   0x20 /* .01. .... NAND Flash ROM Model */
#define AD3CYC  0x00 /* ...0 .... Address 3-cycle */
#define AD4CYC  0x10 /* ...1 .... Address 4-cycle */
#define BS16    0x00 /* .... 00.. 16page/block */
#define BS32    0x04 /* .... 01.. 32page/block */
#define BS64    0x08 /* .... 10.. 64page/block */
#define PS256   0x00 /* .... ..00 256byte/page */
#define PS512   0x01 /* .... ..01 512byte/page */
#define PS2048  0x02 /* .... ..10 2048byte/page */
#define MWP     0x80 /* WriteProtect mask */
#define MFLASH  0x60 /* Flash Rom mask */
#define MADC    0x10 /* Address Cycle */
#define MBS     0x0C /* BlockSize mask */
#define MPS     0x03 /* PageSize mask */
/* SmartMedia Model */
#define NOSSFDC    0x00 /* SmartMedia Not recognized*/
#define SSFDC1MB   0x01 /*   1MB SmartMedia */
#define SSFDC2MB   0x02 /*   2MB SmartMedia */
#define SSFDC4MB   0x03 /*   4MB SmartMedia */
#define SSFDC8MB   0x04 /*   8MB SmartMedia */
#define SSFDC16MB  0x05 /*  16MB SmartMedia */
#define SSFDC32MB  0x06 /*  32MB SmartMedia */
#define SSFDC64MB  0x07 /*  64MB SmartMedia */
#define SSFDC128MB 0x08 /* 128MB SmartMedia */
#define SSFDC256MB 0x09 /* 256MB SmartMedia */
#define SSFDC512MB 0x0a /* 512MB SmartMedia */
#define SSFDC1GB   0x0b /*   1GB SmartMedia */
#define SSFDC2GB   0x0c /*   2GB SmartMedia */




/***************************************************************************
 Struct Definition
 ***************************************************************************/
struct SSFDCTYPE
{
    unsigned char Model;     /* Smart Media Mode recognized:
                                      * NOSSFDC    == Not recognized here
                                      * SSFDC1MB   ==   1 MB Card
                                      * SSFDC2MB   ==   2 MB Card
                                      * SSFDC4MB   ==   4 MB Card
                                      * SSFDC8MB   ==   8 MB Card
                                      * SSFDC16MB  ==  16 MB Card
                                      * SSFDC32MB  ==  32 MB Card
                                      * SSFDC64MB  ==  64 MB Card
                                      * SSFDC128MB == 128 MB Card
                                      * SSFDC256MB == 256 MB Card
                                      * SSFDC512MB == 512 MB Card
                                      * SSFDC1GB   ==   1 GB Card
                                      * SSFDC2GB   ==   2 GB Card
                                      */
    unsigned char Attribute; /* Bits:
                                      *  7  : 0 => No Write Protect  1=>Write Protected
                                      *  5,6: 00=> Mask ROM,        01=> Flash ROM
                                      *  4  : 0 => 3 Cycle Address   1=> 4 Cycle Address
                                      *  2,3: 00=> 16 pages/block   01=> 32 pages/block
                                      *       10=> 64 pages/block
                                      *  0,1: 00=> 256 bytes/page   01=> 512 bytes/page
                                      *       10=> 2048 bytes/page
                                      */
    unsigned char MaxZones;      /* Number of Zones. If unused, it will be 1 */
    unsigned char MaxSectors;    /* Number of Sectors in a Block or
                                            * number of 512 byte pages */
    unsigned short MaxBlocks;    /* Number of Physical Blocks per Zone */
    unsigned short MaxLogBlocks; /* Number of Logical  Blocks per Zone */
    unsigned char  BootSector;   /* Boot Sector Location */
};


struct ADDRESS              /* Currently accessed address of the SmartCard */
{
    unsigned char Zone;      /* Zone Number */
    unsigned char Sector;    /* Sector (512 byte page) Number on Block */
    unsigned short PhyBlock; /* Physical Block Number on Zone */
    unsigned short LogBlock; /* Logical Block Number of Zone */
};

struct CIS_AREA             /* Currently accessed address where CIS information
                                         is set */
{
    unsigned char  Sector;   /* Sector (512 byte page) Number on Block */
    unsigned short PhyBlock; /* Physical Block Number on Zone 0 */
};


#endif
