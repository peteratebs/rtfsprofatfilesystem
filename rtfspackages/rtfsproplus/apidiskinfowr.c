/*
* EBS - RTFS (Real Time File Manager)
*
* Copyright EBS Inc. 1987-2003
* All rights reserved.
* This code may not be redistributed in source or linkable object form
* without the consent of its author.
*/
/* APIDISK.C - Contains drive oriented api function */

#include "rtfs.h"
#if (!RTFS_CFG_READONLY) /* Excluded from build if read only */

/***************************************************************************
Name:

    pc_diskio_free_list - Query free cluster segments on the drive

Summary:

    #include "rtfs.h"

    BOOLEAN pc_diskio_free_list(byte *drivename, int listsize, FREELISTINFO *plist, dword startcluster, dword endcluster, dword threshhold)

    Where

    drivename - Must contain a valid drive specifier for example C:. An empty string
    selects the current working drive.

    listsize  - Must contain the number of FREELISTINFO elements in the array provided in plist.

    plist - Must contain the address of an array of listsize FREELISTINFO elements

        FREELISTINFO is a structure defined as:

        typedef struct freelistinfo {
            dword cluster;        Cluster where the free region starts
            dword nclusters;      Number of free clusters the free segment
        } FREELISTINFO;

    startcluster - Must contain the start of the cluster range to scan for free clusters.
                   If startcluster is zero the scan starts at the end beginning of the FAT
    endcluster   - Must contain the end of the cluster range to scan for free clusters. If endcluster
                   is zero the scan ends at the end of the FAT
    threshhold   - Selects the minimum sized contiguous free region to report.

                   This argument allows the caller to exclude free chains that are less than
                   a certain number of contiguous clusters.

                   Setting threshhold to one reports every free cluster segment in the range.

                   Setting threshhold to a larger value allows the caller to filter out free fragments
                   that are less than some minimum size.

                   Setting threshhold to a larger value also reduces the number of entries in plist
                   are used to hold return values.

                   The value of threshold must be at least 1.

 Description:
    This routines returns a list of currently free cluster segments and places the results
    in the FREELISTINFO structure array.

    Note:

    If the range contains more free extents than will fit in plist as indicated by listsize then
    the first listsize elements of plist are populated but the return value contains a number
    larger than listsize, the number of individual contiguous segments threshold clusters or
    greater found in the range.

    If a partial list of chains is insufficient and the return count is larger than listsize,
    pc_diskio_free_list() may be called again but using a plist buffer and a listsize
    large enough to contain at least as many elements as the value returned from the initial call.


 Returns
    Returns:

        The number of free extents in the range of length threshold clusters or greater
        -1 on error.

    errno is set to one of the following
    0                - No error
    PEINVALIDDRIVEID - Driveno is incorrect
    PEINVALIDPARMS   - Invalid or inconsistent arguments
    An ERTFS system error
****************************************************************************/

int pc_diskio_free_list(byte *drivename, int listsize, FREELISTINFO *plist, dword startcluster, dword endcluster, dword threshhold)
{
    dword  start_region, region_size, next_clno, first_free_cluster, n_contig;
    int     is_error = 0;
    int     driveno,index;
    BOOLEAN ret_val;
    DDRIVE  *pdr;

    CHECK_MEM(int, 0) /* Make sure memory is initted */


    rtfs_clear_errno();

    if (threshhold < 1)
    {
        rtfs_set_errno(PEINVALIDPARMS, __FILE__, __LINE__);
        return(-1);
    }

    /* Get the drive and make sure it is mounted   */

    driveno = check_drive_name_mount(drivename, CS_CHARSET_NOT_UNICODE);
    /*  if error, errno was set by check_drive */
    if (driveno < 0)
        return(-1);

    ret_val = TRUE; /* Will be set to false if an error occurs */
    pdr = pc_drno2dr(driveno);
    index = 0;
    region_size = 0;
    start_region = 0;

    /* If no start cluster is provided use 2 */
    if (startcluster)
        next_clno = startcluster;
    else
        next_clno = 2;
    /* If no end cluster is provided or the end is > maxfindex, stop at maxfindex */
    if (!endcluster || endcluster > pdr->drive_info.maxfindex)
        endcluster = pdr->drive_info.maxfindex;

    while (next_clno && next_clno <= endcluster)
    {   /* Set min_clusters to threshold and max_clusters argument to maxfindex so it finds at least threshold and as many free as possible */
        first_free_cluster = fatop_find_contiguous_free_clusters(pdr, next_clno, endcluster, threshhold, pdr->drive_info.maxfindex, &n_contig, &is_error, ALLOC_CLUSTERS_PACKED);
        if (is_error)
        {   /* Fatop set errno */
            region_size = 0;
            ret_val = FALSE;
            break;
        }
        if (!first_free_cluster)
            break;
        if (!start_region)
        {
            start_region = first_free_cluster;
            region_size  = n_contig;
        }
        else if (first_free_cluster == (start_region+region_size) )
        {   /* extend the current region */
            region_size += n_contig;
        }
        else
        {   /* Not contiguous , start tracking a new one */
            if (region_size >= threshhold)
            {
                if (index < listsize)
                { /* Remember this region if over the threshold */
                    plist->cluster = start_region;
                    plist->nclusters = region_size;
                    plist++;
                }
                index++;
            }
            region_size =  n_contig;
            start_region = first_free_cluster;
        }
        next_clno = first_free_cluster + n_contig;
    }
    /* Clean up */
    if (ret_val)
    {
        if (start_region && region_size >= threshhold)
        {
            if (index < listsize)
            { /* Remember this region if over the threshold */
                plist->cluster = start_region;
                plist->nclusters = region_size;
                plist++;
            }
            index++;
        }
        ret_val = index;
        rtfs_clear_errno();
    }
    release_drive_mount(driveno);/* Release lock, unmount if aborted */
    return(ret_val);
}
#endif /* Exclude from build if read only */
