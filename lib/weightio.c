//  MODULE: weightio.c

//
//
//
//      Copyright (c) Stratus Technologies Bermuda Ltd., 2008.
//      All Rights Reserved. Unpublished rights reserved
//      under the copyright laws of the United States.
//
//      This program is free software; you can redistribute it and/or modify
//      it under the terms of the GNU Lesser General Public License as published
//      by the Free Software Foundation; version 2.1 only. with the special
//      exception on linking described in file LICENSE.
//
//      This program is distributed in the hope that it will be useful,
//      but WITHOUT ANY WARRANTY; without even the implied warranty of
//      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//      GNU Lesser General Public License for more details.
//
//
//  DESCRIPTION:
//
//      Xen HA host weight control file io utility.
//
//  AUTHORS:
//
//      Shinji Matsumoto
//
//  CREATION DATE: 
//
//      Nov 13, 2008
//
//   


//
//
//  O P E R A T I N G   S Y S T E M   I N C L U D E   F I L E S
//
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <fcntl.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <errno.h>
#include <assert.h>


//
//
//  M A R A T H O N   I N C L U D E   F I L E S
//
//

#include "mtctypes.h"
#include "mtcerrno.h"
#include "log.h"
#include "hostweight.h"

//
//
//  S T A T I C   F U N C T I O N   P R O T O T Y P E S
//
//


//
//
//  L O C A L   D E F I N I T I O N S
//
//

#define MAX_HOST_WEIGHT_FILE_LINELEN (MAX_HOST_WEIGHT_CLASSNAME_LEN + BUFSIZ) // +BUFSIZ means white spaces, decimal weight value and +alpha
#define MAX_HOST_WEIGHT_FILE_SIZE (MAX_HOST_WEIGHT_CLASS_NUM * MAX_HOST_WEIGHT_FILE_LINELEN)


//
//
//  F U N C T I O N   D E F I N I T I O N S
//
//



//
//
//  NAME:
//
//      open_hostweidht_file
//
//  DESCRIPTION:
//
//      open and lock the weight file
//
//  FORMAL PARAMETERS:
//
//      fd:     file descriptor of hostweight file
//      err_no: err_no
//          
//  RETURN VALUE:
//
//      MTC_SUCCESS
//      MTC_ERROR_WEIGHT_OPEN
//      MTC_ERROR_WEIGHT_LOCK
//
//  ENVIRONMENT:
//
//      none
//
//

MTC_STATUS
open_hostweight_file(int *fd, int *err_no)
{
    if ((*fd = open(HA_HOST_WEIGHT_FILE, O_RDWR|O_CREAT, 00400)) < 0)
    {
        *err_no = errno;
        return MTC_ERROR_WEIGHT_OPEN;
    }
    if (lockf(*fd, F_LOCK, 0) < 0)
    {
        *err_no = errno;
        close(*fd);
        return MTC_ERROR_WEIGHT_LOCK;
    }
    return MTC_SUCCESS;
}


//
//
//  NAME:
//
//      read_hostweight_file
//
//  DESCRIPTION:
//
//      read the weight file into weight table
//
//  FORMAL PARAMETERS:
//
//      fd: file descriptor of the weight file
//      err_no: errno
//      wtable: pointer for weight table
//      size: size of the  weight table
//          
//  RETURN VALUE:
//
//      MTC_SUCCESS
//      MTC_ERROR_WEIGHT_IO_ERROR
//
//  ENVIRONMENT:
//
//      none
//
//

MTC_STATUS
read_hostweight_file(int fd, int *err_no, PHOST_WEIGHT_TABLE wtable, MTC_U32 size)
{
    char buf[MAX_HOST_WEIGHT_FILE_SIZE + 1];     // + 1 is for '\0'
    char classname[MAX_HOST_WEIGHT_FILE_LINELEN];

    MTC_U32 weight;
    int index;
    int len , readlen;
    char *str, *lf;
    
    memset(wtable, 0, sizeof(HOST_WEIGHT_TABLE) * size);
    lseek(fd, 0, SEEK_SET);
    readlen = 0;
    while ((len = read(fd, buf + readlen, MAX_HOST_WEIGHT_FILE_SIZE - readlen)) > 0) 
    {
        readlen += len;        
        if (readlen >= MAX_HOST_WEIGHT_FILE_SIZE) break;
    }
    if (len < 0)
    {
        // read error
        *err_no = errno;
        return MTC_ERROR_WEIGHT_IO_ERROR;
    }
    if (readlen == 0)
    {
        // no data
        return MTC_SUCCESS;
    }
    buf[readlen] = '\0';

    index = 0;
    str = buf;
    while (1)
    {
        lf = strchr(str, '\n');
        if (lf != NULL) {
            *lf = '\0';
        }
        if (strlen(str) < MAX_HOST_WEIGHT_FILE_LINELEN)
        {
            if (sscanf(str, "%s %d", classname, &weight) == 2 &&
                strlen(classname) < MAX_HOST_WEIGHT_CLASSNAME_LEN)
            {
                strcpy(wtable[index].classname, classname);
                wtable[index].weight = weight;
                index ++;
                if (index >= size)
                {
                    // table full.
                    break;
                }
            }
            else
            {
                // skip format error
            }
        }
        else
        {
            // skip too long line
        }
        if (lf == NULL) {
            // last line
            break;
        }
        str = lf + 1;
    }
    return MTC_SUCCESS;
}


//
//
//  NAME:
//
//      write_hostweight_file
//
//  DESCRIPTION:
//
//      write the weight table into the weight file
//
//  FORMAL PARAMETERS:
//
//      fd: file descriptor of the weight file
//      err_no: errno
//      wtable: pointer for weight table
//      size: size of the  weight table
//          
//  RETURN VALUE:
//
//      MTC_SUCCESS
//      MTC_ERROR_WEIGHT_IO_ERROR
//
//  ENVIRONMENT:
//
//      none
//
//

MTC_STATUS
write_hostweight_file(int fd, int *err_no, PHOST_WEIGHT_TABLE wtable, MTC_U32 size)
{
    char line[MAX_HOST_WEIGHT_FILE_LINELEN];
    int index;
    int len, writelen;

    lseek(fd, 0, SEEK_SET);
    ftruncate(fd, 0);
    for (index = 0; index < size; index++) 
    {
        if (strcmp(wtable[index].classname,""))
        {
            sprintf(line, "%s %d\n", wtable[index].classname, wtable[index].weight);
            writelen = 0;
            while ((len = write(fd, line + writelen, strlen(line) - writelen)) >= 0)
            {
                writelen += len;
                if (writelen >= strlen(line)) break;
            }
            if (len < 0)
            {
                // write error
                *err_no = errno;
                return MTC_ERROR_WEIGHT_IO_ERROR;
            }
        }
    }
    return MTC_SUCCESS;
}

//
//
//  NAME:
//
//      set_hostweight_table
//
//  DESCRIPTION:
//
//      set/remove classname and weight to/from weight table
//         weight != 0: set
//         weight == 0: remove
//
//  FORMAL PARAMETERS:
//
//      wtable: pointer for weight table
//      size: size of the  weight table
//      classname: classname to set/remove
//      weight: weight to set
//          
//  RETURN VALUE:
//
//      MTC_SUCCESS: success
//      
//
//  ENVIRONMENT:
//
//      none
//
//

MTC_STATUS
set_hostweight_table(
    PHOST_WEIGHT_TABLE wtable,
    MTC_U32 size,
    PMTC_S8 classname, 
    MTC_U32 weight)
{
    int index;

    for (index = 0; index < size; index++)
    {
        if (!strcmp(wtable[index].classname, classname))
        {
            // found the classname

            // overwrite the weight
            wtable[index].weight = weight;

            if (weight == 0)
            {
                // remove the entry
                strcpy(wtable[index].classname, "");
            }
            return MTC_SUCCESS;
        }
    }

    // not found the classname in weight table.
    if (weight == 0)
    {
        return MTC_SUCCESS;
    }
    for (index = 0; index < size; index++)
    {
        if (!strcmp(wtable[index].classname, ""))
        {
            strcpy(wtable[index].classname, classname);
            wtable[index].weight = weight;
            return MTC_SUCCESS;
        }
    }
    // table overflow
    return MTC_ERROR_WEIGHT_TABLE_FULL;
}
