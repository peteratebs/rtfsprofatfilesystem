/* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* PROTESTS.C - RtfsProPLus Tests, common routines


*/


#include "rtfs.h"
#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */
#include "protests.h"

extern byte test_driveid_buffer[8];
extern byte *test_driveid;
extern int  test_drivenumber;

extern byte *data_buffer;
extern dword  data_buffer_size_dw;
/* extended IO routines set these variables in debug mode
   when allocating clusters, the regression test uses them to
   verify that clusters where allocated as expected */
dword debug_first_efile_cluster_allocated;
dword debug_last_efile_cluster_allocated;
dword debug_num_transaction_buffer_loads;



BOOLEAN pro_test_free_manager_atached(void)
{
    DRIVE_INFO disk_info;
    if (!pc_diskio_info(test_driveid, &disk_info, FALSE))
        return(FALSE);
    return(disk_info.free_manager_enabled);
}

/* Moved pro_test_bytes_per_sector and pro_test_bytes_per_cluster(void) to protestrd.c */


dword pro_test_dwrand(void)
{
dword x,y,a;
         x = (dword) rand();
         y = (dword) rand();
         x <<= 16;
         a = ((x+y)+3) & ~3L;   /* round up to four byte boundary */
         return(a);
}


void pro_test_print_dword(char *prompt, dword val, int flag)
{
    RTFS_PRINT_STRING_1((byte *)prompt, 0);
    RTFS_PRINT_LONG_1(val,flag);
}

void pro_test_print_two_dwords(char *prompt_1, dword val_1,
                                  char *prompt_2, dword val_2,int flag)
{
    RTFS_PRINT_STRING_1((byte *)prompt_1, 0);
    RTFS_PRINT_LONG_1(val_1,0);
    RTFS_PRINT_STRING_1((byte *)prompt_2, 0);
    RTFS_PRINT_LONG_1(val_2,flag);
}


/* Moved pro_test_compare_checkpoints() */


/* Moved pro_test_mark_checkpoint(int drive_no, PRO_TEST_CHECKPOINT *pcheck) */



dword pro_test_check_buffer_dwords(dword *dw, dword value, dword count)
{
dword l;
    for (l = 0; l < count; l+=1)
    {
       if (*dw != value)
       {
           return(l);
       }
       dw++;
       value++;
    }
    return(count);
}
void pro_test_set_buffer_dwords(dword *dw, dword value, dword count)
{
    while(count--)
        *dw++ = value;
}

void pro_test_fill_buffer_dwords(dword *dw, dword value, dword count)
{
    while(count--)
        *dw++ = value++;
}


/* Moved pro_test_alloc_data_buffer(void) and pro_test_free_data_buffer(void) to protestrd.c */

int pro_test_efile_create(byte *filename, dword options,dword min_clusters_per_allocation)
{
    int fd;
    EFILEOPTIONS my_options;
    pc_unlink(filename); /* Make sure it's not there */

    rtfs_memset(&my_options, 0, sizeof(my_options));
    my_options.allocation_policy = options;
    my_options.min_clusters_per_allocation = min_clusters_per_allocation;

    fd = pc_efilio_open(filename,(word)(PO_BINARY|PO_RDWR|PO_EXCL|PO_CREAT),(word)(PS_IWRITE | PS_IREAD)
                         ,&my_options);
    ERTFS_ASSERT_TEST(fd>=0)
    return(fd);
}

int pro_test_efile_frag_fill(byte *filename,dword options, dword fragment_size_dw, dword test_file_size_dw,BOOLEAN do_io, BOOLEAN do_close)
{
    int fd,fdfrag;
    dword  ntransfered_dw, nleft_dw, value;

    fdfrag = pro_test_efile_create((byte *)"FRAGFILE",0 ,0);

    /* Fill a file */
    fd = pro_test_efile_create(filename,options,0);
    ERTFS_ASSERT_TEST(fd>=0)

    nleft_dw = test_file_size_dw;
    value = 0;

    while (nleft_dw)
    {
    dword ntodo_dw;

        ntodo_dw = fragment_size_dw;
        if (ntodo_dw > nleft_dw)
            ntodo_dw = nleft_dw;
        if (do_io)
            pro_test_write_n_dwords(fd, value, ntodo_dw, &ntransfered_dw, TRUE, data_buffer);
        else
            pro_test_write_n_dwords(fd, 0, ntodo_dw, &ntransfered_dw,  TRUE, 0);


        ERTFS_ASSERT_TEST(ntodo_dw == ntransfered_dw)
        /* Write a fragment */
        pro_test_write_n_dwords(fdfrag, 0, pro_test_bytes_per_cluster()/4, &ntransfered_dw,  TRUE, 0);
        value += ntodo_dw;
        nleft_dw -= ntodo_dw;
    }
    pc_efilio_close(fdfrag);
    if (do_close)
        pc_efilio_close(fd);
    pc_unlink((byte *)"FRAGFILE"); /* kill the fragment file */
    return(fd);
}


//dword pro_test_bytes_per_cluster(void)

int pro_test_efile_fill(byte *filename,dword options, dword test_file_size_dw,BOOLEAN do_io, BOOLEAN do_close)
{
    int fd;
    dword  ntransfered_dw;

    /* Fill a file */
    fd = pro_test_efile_create(filename,options,0);
    ERTFS_ASSERT_TEST(fd>=0)
    if (do_io)
        pro_test_write_n_dwords(fd, 0, test_file_size_dw, &ntransfered_dw,
            TRUE, data_buffer);
    else
        pro_test_write_n_dwords(fd, 0, test_file_size_dw, &ntransfered_dw,
            TRUE, 0);
    ERTFS_ASSERT_TEST(test_file_size_dw == ntransfered_dw)
    if (do_close)
        pc_efilio_close(fd);
    return(fd);
}


BOOLEAN pro_test_read_n_dwords(int fd, dword value, dword size_dw,
        dword *pnread, BOOLEAN check_value)
 {
dword ntoread,rval,nread_dw,n_left_dw, nloops;
BOOLEAN clear_line = FALSE;
    nloops = 0;
    *pnread = 0;

    n_left_dw = size_dw;
    while (n_left_dw)
    {
        if (nloops++ > 16) /* Only print on big transfers */
        {
            pro_test_print_two_dwords("read_dwords completed: ", *pnread," n left: ", n_left_dw,0);
            RTFS_PRINT_STRING_1((byte *)"        ", PRFLG_CR);
            nloops = 0;
            clear_line = TRUE;
        }
        if (n_left_dw >= data_buffer_size_dw)
            ntoread = data_buffer_size_dw*4;
        else
            ntoread = n_left_dw * 4;
        if (check_value)
        {
            if (!pc_efilio_read(fd, (byte*)data_buffer, ntoread, &rval))
                {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
        }
         else
        {
            if (!pc_efilio_read(fd, (byte*)0, ntoread, &rval))
                {ERTFS_ASSERT_TEST(rtfs_debug_zero())}
         }
        if (rval == 0)   /* EOF pnread says how many were read */
            return(TRUE);
        nread_dw = rval/4;
        *pnread += nread_dw;
        if (nread_dw && check_value)
        {
        dword ltemp;
             ltemp = pro_test_check_buffer_dwords((dword *)data_buffer, value,
                                            nread_dw);
            ERTFS_ASSERT_TEST(ltemp == nread_dw)
            value += nread_dw;
        }
        n_left_dw -= nread_dw;
        if (rval != ntoread)
            break;
    }
    if (clear_line)
    {
        RTFS_PRINT_STRING_1((byte *)"",PRFLG_NL);
    }
    return(TRUE);
}
BOOLEAN _pro_test_write_n_dwords(int fd, dword value, dword size_dw,
                dword *pnwritten, BOOLEAN do_increment, byte *buffer, BOOLEAN abort_on_error);

BOOLEAN pro_test_write_n_dwords(int fd, dword value, dword size_dw,
                dword *pnwritten, BOOLEAN do_increment, byte *buffer)
{
    return(_pro_test_write_n_dwords(fd, value, size_dw, pnwritten, do_increment, buffer, TRUE));
}


BOOLEAN _pro_test_write_n_dwords(int fd, dword value, dword size_dw,
                dword *pnwritten, BOOLEAN do_increment, byte *buffer, BOOLEAN abort_on_error)
{
dword ntowrite,nwritten,n_left_dw, nloops;
BOOLEAN clear_line = FALSE;

    nloops = 0;
    *pnwritten = 0;
    n_left_dw = size_dw;
    while (n_left_dw)
    {
        if (nloops++ > 16) /* Only print on big transfers */
        {
            pro_test_print_two_dwords("write_dwords completed: ", *pnwritten," n left: ", n_left_dw,0);
            RTFS_PRINT_STRING_1((byte *)"        ", PRFLG_CR);
            nloops = 0;
            clear_line = TRUE;
        }
        if (n_left_dw >= data_buffer_size_dw)
            ntowrite = data_buffer_size_dw;
        else
            ntowrite = n_left_dw;
        if (buffer)
        {
            if (do_increment)
            {
                pro_test_fill_buffer_dwords((dword *)buffer, value, ntowrite);
                value += ntowrite;
            }
            else
                pro_test_set_buffer_dwords((dword *)buffer, value, ntowrite);
        }
        if (!pc_efilio_write(fd, (byte*)buffer, ntowrite*4, &nwritten))
        {
            if (!abort_on_error)
                return(FALSE);
            ERTFS_ASSERT_TEST(rtfs_debug_zero())
        }
        *pnwritten += nwritten/4;
        if (nwritten != ntowrite*4)
            if (!abort_on_error)
                return(FALSE);
        ERTFS_ASSERT_TEST(nwritten == ntowrite*4)
        n_left_dw -= ntowrite;
    }
    if (clear_line)
    {
        RTFS_PRINT_STRING_1((byte *)"",PRFLG_NL);
    }
    return(TRUE);
}




/* Moved pro_test_release_mount_parameters pro_test_restore_mount_context, void pro_test_save_mount_context to protestrd.c */


#endif /* Exclude from build if read only */
