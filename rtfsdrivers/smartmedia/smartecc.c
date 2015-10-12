/******************************************************************
   File Name:        SmartEcc.c
      This file contains the ECC routines for the SmartMedia
      interface.

 ******************************************************************/
#include <rtfs.h>

#if (INCLUDE_SMARTMEDIA)

#include "smartecc.h"


/******************** ECC Related Definitions ********************************/
#define BIT7        0x80
#define BIT6        0x40
#define BIT5        0x20
#define BIT4        0x10
#define BIT3        0x08
#define BIT2        0x04
#define BIT1        0x02
#define BIT0        0x01
#define BIT1BIT0    0x03
#define BIT23       0x00800000L
#define MASK_CPS    0x3f
#define CORRECTABLE 0x00555554L

/******************************************************************************
    ecctable      CP0-CP5 code table of ECC
******************************************************************************/
const unsigned char ecctable[256] =
{
     0x00,0x55,0x56,0x03,0x59,0x0C,0x0F,0x5A,0x5A,0x0F,0x0C,0x59,0x03,0x56,0x55,0x00,
     0x65,0x30,0x33,0x66,0x3C,0x69,0x6A,0x3F,0x3F,0x6A,0x69,0x3C,0x66,0x33,0x30,0x65,
     0x66,0x33,0x30,0x65,0x3F,0x6A,0x69,0x3C,0x3C,0x69,0x6A,0x3F,0x65,0x30,0x33,0x66,
     0x03,0x56,0x55,0x00,0x5A,0x0F,0x0C,0x59,0x59,0x0C,0x0F,0x5A,0x00,0x55,0x56,0x03,
     0x69,0x3C,0x3F,0x6A,0x30,0x65,0x66,0x33,0x33,0x66,0x65,0x30,0x6A,0x3F,0x3C,0x69,
     0x0C,0x59,0x5A,0x0F,0x55,0x00,0x03,0x56,0x56,0x03,0x00,0x55,0x0F,0x5A,0x59,0x0C,
     0x0F,0x5A,0x59,0x0C,0x56,0x03,0x00,0x55,0x55,0x00,0x03,0x56,0x0C,0x59,0x5A,0x0F,
     0x6A,0x3F,0x3C,0x69,0x33,0x66,0x65,0x30,0x30,0x65,0x66,0x33,0x69,0x3C,0x3F,0x6A,
     0x6A,0x3F,0x3C,0x69,0x33,0x66,0x65,0x30,0x30,0x65,0x66,0x33,0x69,0x3C,0x3F,0x6A,
     0x0F,0x5A,0x59,0x0C,0x56,0x03,0x00,0x55,0x55,0x00,0x03,0x56,0x0C,0x59,0x5A,0x0F,
     0x0C,0x59,0x5A,0x0F,0x55,0x00,0x03,0x56,0x56,0x03,0x00,0x55,0x0F,0x5A,0x59,0x0C,
     0x69,0x3C,0x3F,0x6A,0x30,0x65,0x66,0x33,0x33,0x66,0x65,0x30,0x6A,0x3F,0x3C,0x69,
     0x03,0x56,0x55,0x00,0x5A,0x0F,0x0C,0x59,0x59,0x0C,0x0F,0x5A,0x00,0x55,0x56,0x03,
     0x66,0x33,0x30,0x65,0x3F,0x6A,0x69,0x3C,0x3C,0x69,0x6A,0x3F,0x65,0x30,0x33,0x66,
     0x65,0x30,0x33,0x66,0x3C,0x69,0x6A,0x3F,0x3F,0x6A,0x69,0x3C,0x66,0x33,0x30,0x65,
     0x00,0x55,0x56,0x03,0x59,0x0C,0x0F,0x5A,0x5A,0x0F,0x0C,0x59,0x03,0x56,0x55,0x00
};


/*<BCI>*****************************************************************
Name:       int _Correct_SwECC(byte *buf,
                               byte *eccdata,
                               byte *calculate_ecc )

Parameters: buf points to a 512 byte page of data
            eccdata points to ECC in redundant buffer.
            calculate_ecc
Returns:    correct ecc is in calculate_ecc
            return value is 0 for no error
                           -1 for nonrecoverable error
Description:
            Calculates ECC in software and recover from error if necessary.

Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
int _Correct_SwECC(byte *buf, byte *eccdata, byte *calculate_ecc )
{
    dword l;    /* Working to check d */
    dword d;    /* Result of comparison */
    word i;             /* For counting */
    byte d1,d2,d3;      /* Result of comparison */
    byte a;             /* Working for add */
    byte add;           /* Byte address of cor. DATA */
    byte b;             /* Working for bit */
    byte bit;           /* Bit address of cor. DATA */
    byte err;
    byte ecc1;          /* LP15,LP14,LP13,... */
    byte ecc2;          /* LP07,LP06,LP05,... */
    byte ecc3;          /* CP5,CP4,CP3,...,"1","1" */

    ecc1 = calculate_ecc[1];                            /* load local copies */
    ecc2 = calculate_ecc[0];
    ecc3 = calculate_ecc[2];

    d1=(byte)(ecc1^eccdata[1]); d2=(byte)(ecc2^eccdata[0]);             /* Compare LP's */
    d3=(byte)(ecc3^eccdata[2]);                                 /* Comapre CP's */
    d=((dword)d1<<16)                           /* Result of comparison */
        +((dword)d2<<8)
        +(dword)d3;
    if (d==0)
        err = 0;                                        /* If No error, return */
    else
    {
        if (((d^(d>>1))&CORRECTABLE)==CORRECTABLE)      /* If correctable */
        {
            l=BIT23;
            add=0;                                      /* Clear parameter */
            a=BIT7;
            for(i=0; i<8; ++i)                          /* Checking 8 bit */
            {
                if ((d&l)!=0) add|=a;                   /* Make byte address from LP's */
                l>>=2; a>>=1;                           /* Right Shift */
            }
            bit=0;                                      /* Clear parameter */
            b=BIT2;
            for(i=0; i<3; ++i)                          /* Checking 3 bit */
            {
                if ((d&l)!=0) bit|=b;                   /* Make bit address from CP's */
                l>>=2; b>>=1;                           /* Right shift */
            }
            b=BIT0;
            buf[add]^=(b<<bit);                         /* Put corrected data */
            calculate_ecc[0] = eccdata[0];
            calculate_ecc[1] = eccdata[1];
            calculate_ecc[2] = eccdata[2];
            err = 1;                                    /* corrected data */
        }
        else
        {
            i=0;                                        /* Clear count */
            d&=0x00ffffffL;                             /* Masking */
            while(d)                                    /* If d=0 finish counting */
            {
                if (d&BIT0) ++i;                        /* Count number of 1 bit */
                d>>=1;                                  /* Right shift */
            }
            if (i==1)                                   /* If ECC error */
            {
                eccdata[1]=ecc1; eccdata[0]=ecc2;       /* Put right ECC code */
                eccdata[2]=ecc3;
                err = 2;                                /* corrected ecc */
            }
            else
                err = 3;                                /* Uncorrectable error */
        }
    }

    if(err==3)
        return(-1);                                     /* exit with error */
    return(0);                                          /* no error */
}



/*<BCI>*****************************************************************
Name:       void _Calculate_SwECC(byte *buf, byte *ecc)

Parameters: buf points to data and ecc points to 3 bytes of allocated memory.
Returns:    ecc is changed to ECC for data in buf

Description:
            Calculating ECC data[0-255] -> ecc1,ecc2,ecc3 using CP0-CP5 code table[0-255]

Called By:
Calls:      none

Globals:    none

History:
 11.01.00   DAU   Created from PC based version
**<BCI>*****************************************************************/
void _Calculate_SwECC(unsigned char *buf, unsigned char *ecc)
{
    byte *ecc1;         /* LP15,LP14,LP13,... */
    byte *ecc2;         /* LP07,LP06,LP05,... */
    byte *ecc3;         /* CP5,CP4,CP3,...,"1","1" */
    byte i,j;           /* For counting */
    byte a;             /* Working for table or reg2, reg3 */
    byte b;             /* Working for ecc1,ecc2 */
    byte reg1;          /* D-all,CP5,CP4,CP3,... */
    byte reg2;          /* LP14,LP12,L10,... */
    byte reg3;          /* LP15,LP13,L11,... */

    ecc1 = ecc+1;                                   /* local copies */
    ecc2 = ecc;
    ecc3 = ecc+2;

    reg1=reg2=reg3=0;                               /* Clear parameter */
    for(i=j=0; j==0; ++i)
    {
        a=ecctable[buf[i]];                         /* Get CP0-CP5 code from table */
        reg1^=(a&MASK_CPS);                         /* XOR with a */
        if ((a&BIT6)!=0)                            /* If D_all(all bit XOR) = 1 */
        {
            reg3^=i;                                /* XOR with counter */
            reg2^=~(i);                             /* XOR with inv. of counter */
        }
        if(i == 255)
            j++;
    }

    /* Trans LP14,12,10,... & LP15,13,11,... -> LP15,14,13,... & LP7,6,5,.. */
    a=BIT7; b=BIT7;                                 /* 80h=10000000b */
    *ecc1=*ecc2=0;                                  /* Clear ecc1,ecc2 */
    for(i=0; i<4; ++i)
    {
        if ((reg3&a)!=0) *ecc1|=b;                  /* LP15,13,11,9 -> ecc1 */
        b=(byte)(b>>1);                                     /* Right shift */
        if ((reg2&a)!=0) *ecc1|=b;                  /* LP14,12,10,8 -> ecc1 */
        b=(byte)(b>>1);                                     /* Right shift */
        a=(byte)(a>>1);                                     /* Right shift */
    }
    b=BIT7;                                         /* 80h=10000000b */
    for(i=0; i<4; ++i)
    {
        if ((reg3&a)!=0) *ecc2|=b;                  /* LP7,5,3,1 -> ecc2 */
        b=(byte)(b>>1);                                     /* Right shift */
        if ((reg2&a)!=0) *ecc2|=b;                  /* LP6,4,2,0 -> ecc2 */
        b=(byte)(b>>1);                                     /* Right shift */
        a=(byte)(a>>1);                                     /* Right shift */
    }
    *ecc1=(byte)~(*ecc1); *ecc2=(byte)~(*ecc2);                 /* Inv. ecc2 & ecc3 */
    *ecc3=(byte)((byte)((~reg1)<<2)|BIT1BIT0);                    /* Make TEL format */
}
#endif /* (INCLUDE_SMARTMEDIA) */
