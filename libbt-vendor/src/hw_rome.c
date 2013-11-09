/*
 *
 *  Copyright (c) 2013, The Linux Foundation. All rights reserved.
 *  Not a Contribution.
 *
 *  Copyright 2012 The Android Open Source Project
 *
 *  Licensed under the Apache License, Version 2.0 (the "License"); you
 *  may not use this file except in compliance with the License. You may
 *  obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 *  implied. See the License for the specific language governing
 *  permissions and limitations under the License.
 *
 */

/******************************************************************************
 *
 *  Filename:      hw_rome.c
 *
 *  Description:   Contains controller-specific functions, like
 *                      firmware patch download
 *                      low power mode operations
 *
 ******************************************************************************/
#ifdef __cplusplus
extern "C" {
#endif

#define LOG_TAG "bt_vendor"

#include <sys/socket.h>
#include <utils/Log.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <ctype.h>
#include <cutils/properties.h>
#include <stdlib.h>
#include <termios.h>

#include "bt_hci_bdroid.h"
#include "bt_vendor_qcom.h"
#include "hci_uart.h"
#include "hw_rome.h"

#ifdef __cplusplus
}
#endif

/******************************************************************************
**  Variables
******************************************************************************/
FILE *file;
unsigned char *phdr_buffer;
unsigned char *pdata_buffer = NULL;
patch_info rampatch_patch_info;
unsigned short rome_ver = ROME_VER_UNKNOWN;
unsigned char gTlv_type;


/******************************************************************************
**  Extern variables
******************************************************************************/
extern uint8_t vnd_local_bd_addr[6];

/*****************************************************************************
**   Functions
*****************************************************************************/
/*
 * Read an VS HCI event from the given file descriptor.
 */
int read_vs_hci_event(int fd, unsigned char* buf, int size)
{
    int remain, r;
    int count = 0, i;

    if (size <= 0) {
        ALOGE("Invalid size arguement!");
        return -1;
    }

    ALOGI("%s: Wait for HCI-Vendor Specfic Event from SOC", __FUNCTION__);

    /* The first byte identifies the packet type. For HCI event packets, it
     * should be 0x04, so we read until we get to the 0x04. */
    /* It will keep reading until find 0x04 byte */
    while (1) {
            r = read(fd, buf, 1);
            if (r <= 0)
                    return -1;
            if (buf[0] == 0x04)
                    break;
    }
    count++;

    /* The next two bytes are the event code and parameter total length. */
    while (count < 3) {
            r = read(fd, buf + count, 3 - count);
            if ((r <= 0) || (buf[1] != 0xFF )) {
                ALOGE("It is not VS event !!");
                return -1;
            }
            count += r;
    }

    /* Now we read the parameters. */
    if (buf[2] < (size - 3))
            remain = buf[2];
    else
            remain = size - 3;

    while ((count - 3) < remain) {
            r = read(fd, buf + count, remain - (count - 3));
            if (r <= 0)
                    return -1;
            count += r;
    }

     /* Check if the set patch command is successful or not */
    if(get_vs_hci_event(buf) != HCI_CMD_SUCCESS)
        return -1;

    return count;
}


int hci_send_vs_cmd(int fd, unsigned char *cmd, unsigned char *rsp, int size)
{
    int ret = 0;

    /* Send the HCI command packet to UART for transmission */
    ret = write(fd, cmd, size);
    if (ret != size) {
        ALOGE("%s: Send failed with ret value: %d", __FUNCTION__, ret);
        goto failed;
    }

    /* Check for response from the Controller */
    if (read_vs_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE) < 0) {
        ret = -ETIMEDOUT;
        ALOGI("%s: Failed to get HCI-VS Event from SOC", __FUNCTION__);
        goto failed;
    }

    ALOGI("%s: Received HCI-Vendor Specific Event from SOC", __FUNCTION__);
failed:
    return ret;
}

int get_vs_hci_event(unsigned char *rsp)
{
    int err = 0, i;

    if( (rsp[EVENTCODE_OFFSET] == VSEVENT_CODE) || (rsp[EVENTCODE_OFFSET] == EVT_CMD_COMPLETE))
        ALOGI("%s: Received HCI-Vendor Specific event", __FUNCTION__);
    else {
        ALOGI("%s: Failed to receive HCI-Vendor Specific event", __FUNCTION__);
        err = -EIO;
        goto failed;
    }

    ALOGI("%s: Command response: 0x%x", __FUNCTION__, rsp[CMD_RSP_OFFSET]);
    ALOGI("%s: Response type   : 0x%x", __FUNCTION__, rsp[RSP_TYPE_OFFSET]);

    /* Check the status of the operation */
    switch ( rsp[CMD_RSP_OFFSET] )
    {
        case EDL_CMD_REQ_RES_EVT:
        ALOGI("%s: Command Request Response", __FUNCTION__);
        switch(rsp[RSP_TYPE_OFFSET])
        {
            case EDL_PATCH_VER_RES_EVT:
            case EDL_APP_VER_RES_EVT:
                ALOGI("\t Product ID: 0x%x",
                    (unsigned int)(rsp[PATCH_PROD_ID_OFFSET +3] << 24 |
                                        rsp[PATCH_PROD_ID_OFFSET+2] << 16 |
                                        rsp[PATCH_PROD_ID_OFFSET+1] << 8 |
                                        rsp[PATCH_PROD_ID_OFFSET]  ));
                ALOGI("\t Current FW Version: 0x%x",
                    (unsigned short)(rsp[PATCH_CURR_FW_VER_OFFSET + 1] << 8 |
                                            rsp[PATCH_CURR_FW_VER_OFFSET] ));
                ALOGI("\t Chipset Version: 0x%x",  rome_ver =
                    (unsigned short)(rsp[PATCH_CHIPSET_VER_OFFSET + 1] << 8 |
                                            rsp[PATCH_CHIPSET_VER_OFFSET] ));
                break;
            case EDL_TVL_DNLD_RES_EVT:
            case EDL_CMD_EXE_STATUS_EVT:
                switch (err = rsp[CMD_STATUS_OFFSET])
                    {
                    case HCI_CMD_SUCCESS:
                        ALOGI("%s: Download Packet successfully!", __FUNCTION__);
                        break;
                    case PATCH_LEN_ERROR:
                        ALOGI("%s: Invalid patch length argument passed for EDL PATCH "
                        "SET REQ cmd", __FUNCTION__);
                        break;
                    case PATCH_VER_ERROR:
                        ALOGI("%s: Invalid patch version argument passed for EDL PATCH "
                        "SET REQ cmd", __FUNCTION__);
                        break;
                    case PATCH_CRC_ERROR:
                        ALOGI("%s: CRC check of patch failed!!!", __FUNCTION__);
                        break;
                    case PATCH_NOT_FOUND:
                        ALOGI("%s: Invalid patch data!!!", __FUNCTION__);
                        break;
                    case TLV_TYPE_ERROR:
                        ALOGI("%s: TLV Type Error !!!", __FUNCTION__);
                        break;
                    default:
                        ALOGI("%s: Undefined error (0x%x)", __FUNCTION__, err);
                        break;
                    }
            break;
        }
        break;

        case NVM_ACCESS_CODE:
            ALOGI("%s: NVM Access Code!!!", __FUNCTION__);
            err = HCI_CMD_SUCCESS;
            break;
        case EDL_SET_BAUDRATE_RSP_EVT:
            /* Rome 1.1 has bug with the response, so it should ignore it. */
            if (rsp[BAUDRATE_RSP_STATUS_OFFSET] != BAUDRATE_CHANGE_SUCCESS)
            {
                ALOGE("%s: Set Baudrate request failed - 0x%x", __FUNCTION__,
                    rsp[CMD_STATUS_OFFSET]);
                err = -1;
            }
            break;
        default:
            ALOGE("%s: Not a valid status!!!", __FUNCTION__);
            err = -1;
            break;
    }

failed:
    return err;
}

void frame_hci_cmd_pkt(
    unsigned char *cmd,
    int edl_cmd, unsigned int p_base_addr,
    int segtNo, int size
    )
{
    int offset = 0;
    hci_command_hdr *cmd_hdr;

    memset(cmd, 0x0, HCI_MAX_CMD_SIZE);

    cmd_hdr = (void *) (cmd + 1);

    cmd[0]      = HCI_COMMAND_PKT;
    cmd_hdr->opcode = cmd_opcode_pack(HCI_VENDOR_CMD_OGF, HCI_PATCH_CMD_OCF);
    cmd_hdr->plen   = size;
    cmd[4]      = edl_cmd;

    switch (edl_cmd)
    {
        case EDL_PATCH_SET_REQ_CMD:
            /* Copy the patch header info as CMD params */
            memcpy(&cmd[5], phdr_buffer, PATCH_HDR_LEN);
            ALOGD("%s: Sending EDL_PATCH_SET_REQ_CMD", __FUNCTION__);
            ALOGD("HCI-CMD %d:\t0x%x \t0x%x \t0x%x \t0x%x \t0x%x",
                segtNo, cmd[0], cmd[1], cmd[2], cmd[3], cmd[4]);
            break;
        case EDL_PATCH_DLD_REQ_CMD:
            offset = ((segtNo - 1) * MAX_DATA_PER_SEGMENT);
            p_base_addr += offset;
            cmd_hdr->plen   = (size + 6);
            cmd[5]  = (size + 4);
            cmd[6]  = EXTRACT_BYTE(p_base_addr, 0);
            cmd[7]  = EXTRACT_BYTE(p_base_addr, 1);
            cmd[8]  = EXTRACT_BYTE(p_base_addr, 2);
            cmd[9]  = EXTRACT_BYTE(p_base_addr, 3);
            memcpy(&cmd[10], (pdata_buffer + offset), size);

            ALOGD("%s: Sending EDL_PATCH_DLD_REQ_CMD: size: %d bytes",
                __FUNCTION__, size);
            ALOGD("HCI-CMD %d:\t0x%x\t0x%x\t0x%x\t0x%x\t0x%x\t0x%x\t0x%x\t"
                "0x%x\t0x%x\t0x%x\t\n", segtNo, cmd[0], cmd[1], cmd[2],
                cmd[3], cmd[4], cmd[5], cmd[6], cmd[7], cmd[8], cmd[9]);
            break;
        case EDL_PATCH_ATCH_REQ_CMD:
            ALOGD("%s: Sending EDL_PATCH_ATTACH_REQ_CMD", __FUNCTION__);
            ALOGD("HCI-CMD %d:\t0x%x \t0x%x \t0x%x \t0x%x \t0x%x",
            segtNo, cmd[0], cmd[1], cmd[2], cmd[3], cmd[4]);
            break;
        case EDL_PATCH_RST_REQ_CMD:
            ALOGD("%s: Sending EDL_PATCH_RESET_REQ_CMD", __FUNCTION__);
            ALOGD("HCI-CMD %d:\t0x%x \t0x%x \t0x%x \t0x%x \t0x%x",
            segtNo, cmd[0], cmd[1], cmd[2], cmd[3], cmd[4]);
            break;
        case EDL_PATCH_VER_REQ_CMD:
            ALOGD("%s: Sending EDL_PATCH_VER_REQ_CMD", __FUNCTION__);
            ALOGD("HCI-CMD %d:\t0x%x \t0x%x \t0x%x \t0x%x \t0x%x",
            segtNo, cmd[0], cmd[1], cmd[2], cmd[3], cmd[4]);
            break;
        case EDL_PATCH_TLV_REQ_CMD:
            ALOGD("%s: Sending EDL_PATCH_TLV_REQ_CMD", __FUNCTION__);
            /* Parameter Total Length */
            cmd[3] = size +2;

            /* TLV Segment Length */
            cmd[5] = size;
            ALOGD("HCI-CMD %d:\t0x%x \t0x%x \t0x%x \t0x%x \t0x%x \t0x%x",
            segtNo, cmd[0], cmd[1], cmd[2], cmd[3], cmd[4], cmd[5]);
            offset = (segtNo * MAX_SIZE_PER_TLV_SEGMENT);
            memcpy(&cmd[6], (pdata_buffer + offset), size);
            break;
        default:
            ALOGE("%s: Unknown EDL CMD !!!", __FUNCTION__);
    }
}

void rome_extract_patch_header_info(unsigned char *buf)
{
    int index;

    /* Extract patch id */
    for (index = 0; index < 4; index++)
        rampatch_patch_info.patch_id |=
            (LSH(buf[index + P_ID_OFFSET], (index * 8)));

    /* Extract (ROM and BUILD) version information */
    for (index = 0; index < 2; index++)
        rampatch_patch_info.patch_ver.rom_version |=
            (LSH(buf[index + P_ROME_VER_OFFSET], (index * 8)));

    for (index = 0; index < 2; index++)
        rampatch_patch_info.patch_ver.build_version |=
            (LSH(buf[index + P_BUILD_VER_OFFSET], (index * 8)));

    /* Extract patch base and entry addresses */
    for (index = 0; index < 4; index++)
        rampatch_patch_info.patch_base_addr |=
            (LSH(buf[index + P_BASE_ADDR_OFFSET], (index * 8)));

    /* Patch BASE & ENTRY addresses are same */
    rampatch_patch_info.patch_entry_addr = rampatch_patch_info.patch_base_addr;

    /* Extract total length of the patch payload */
    for (index = 0; index < 4; index++)
        rampatch_patch_info.patch_length |=
            (LSH(buf[index + P_LEN_OFFSET], (index * 8)));

    /* Extract the CRC checksum of the patch payload */
    for (index = 0; index < 4; index++)
        rampatch_patch_info.patch_crc |=
            (LSH(buf[index + P_CRC_OFFSET], (index * 8)));

    /* Extract patch control value */
    for (index = 0; index < 4; index++)
        rampatch_patch_info.patch_ctrl |=
            (LSH(buf[index + P_CONTROL_OFFSET], (index * 8)));

    ALOGI("PATCH_ID\t : 0x%x", rampatch_patch_info.patch_id);
    ALOGI("ROM_VERSION\t : 0x%x", rampatch_patch_info.patch_ver.rom_version);
    ALOGI("BUILD_VERSION\t : 0x%x", rampatch_patch_info.patch_ver.build_version);
    ALOGI("PATCH_LENGTH\t : 0x%x", rampatch_patch_info.patch_length);
    ALOGI("PATCH_CRC\t : 0x%x", rampatch_patch_info.patch_crc);
    ALOGI("PATCH_CONTROL\t : 0x%x\n", rampatch_patch_info.patch_ctrl);
    ALOGI("PATCH_BASE_ADDR\t : 0x%x\n", rampatch_patch_info.patch_base_addr);

}

int rome_edl_set_patch_request(int fd)
{
    int size, err;
    unsigned char cmd[HCI_MAX_CMD_SIZE];
    unsigned char rsp[HCI_MAX_EVENT_SIZE];

    /* Frame the HCI CMD to be sent to the Controller */
    frame_hci_cmd_pkt(cmd, EDL_PATCH_SET_REQ_CMD, 0,
        -1, PATCH_HDR_LEN + 1);

    /* Total length of the packet to be sent to the Controller */
    size = (HCI_CMD_IND	+ HCI_COMMAND_HDR_SIZE + cmd[PLEN]);

    /* Send HCI Command packet to Controller */
    err = hci_send_vs_cmd(fd, (unsigned char *)cmd, rsp, size);
    if ( err != size) {
        ALOGE("Failed to set the patch info to the Controller!");
        goto error;
    }

    err = read_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE);
    if ( err < 0) {
        ALOGE("%s: Failed to set patch info on Controller", __FUNCTION__);
        goto error;
    }
    ALOGI("%s: Successfully set patch info on the Controller", __FUNCTION__);
error:
    return err;
}

int rome_edl_patch_download_request(int fd)
{
    int no_of_patch_segment;
    int index = 1, err = 0, size = 0;
    unsigned int p_base_addr;
    unsigned char cmd[HCI_MAX_CMD_SIZE];
    unsigned char rsp[HCI_MAX_EVENT_SIZE];

    no_of_patch_segment = (rampatch_patch_info.patch_length /
        MAX_DATA_PER_SEGMENT);
    ALOGI("%s: %d patch segments to be d'loaded from patch base addr: 0x%x",
        __FUNCTION__, no_of_patch_segment,
    rampatch_patch_info.patch_base_addr);

    /* Initialize the patch base address from the one read from bin file */
    p_base_addr = rampatch_patch_info.patch_base_addr;

    /*
    * Depending upon size of the patch payload, download the patches in
    * segments with a max. size of 239 bytes
    */
    for (index = 1; index <= no_of_patch_segment; index++) {

        ALOGI("%s: Downloading patch segment: %d", __FUNCTION__, index);

        /* Frame the HCI CMD PKT to be sent to Controller*/
        frame_hci_cmd_pkt(cmd, EDL_PATCH_DLD_REQ_CMD, p_base_addr,
        index, MAX_DATA_PER_SEGMENT);

        /* Total length of the packet to be sent to the Controller */
        size = (HCI_CMD_IND	+ HCI_COMMAND_HDR_SIZE + cmd[PLEN]);

        /* Initialize the RSP packet everytime to 0 */
        memset(rsp, 0x0, HCI_MAX_EVENT_SIZE);

        /* Send HCI Command packet to Controller */
        err = hci_send_vs_cmd(fd, (unsigned char *)cmd, rsp, size);
        if ( err != size) {
            ALOGE("Failed to send the patch payload to the Controller!");
            goto error;
        }

        /* Read Command Complete Event */
        err = read_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE);
        if ( err < 0) {
            ALOGE("%s: Failed to downlaod patch segment: %d!",
            __FUNCTION__, index);
            goto error;
        }
        ALOGI("%s: Successfully downloaded patch segment: %d",
        __FUNCTION__, index);
    }

    /* Check if any pending patch data to be sent */
    size = (rampatch_patch_info.patch_length < MAX_DATA_PER_SEGMENT) ?
        rampatch_patch_info.patch_length :
        (rampatch_patch_info.patch_length  % MAX_DATA_PER_SEGMENT);

    if (size)
    {
        /* Frame the HCI CMD PKT to be sent to Controller*/
        frame_hci_cmd_pkt(cmd, EDL_PATCH_DLD_REQ_CMD, p_base_addr, index, size);

        /* Initialize the RSP packet everytime to 0 */
        memset(rsp, 0x0, HCI_MAX_EVENT_SIZE);

        /* Total length of the packet to be sent to the Controller */
        size = (HCI_CMD_IND	+ HCI_COMMAND_HDR_SIZE + cmd[PLEN]);

        /* Send HCI Command packet to Controller */
        err = hci_send_vs_cmd(fd, (unsigned char *)cmd, rsp, size);
        if ( err != size) {
            ALOGE("Failed to send the patch payload to the Controller!");
            goto error;
        }

        /* Read Command Complete Event */
        err = read_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE);
        if ( err < 0) {
            ALOGE("%s: Failed to downlaod patch segment: %d!",
                __FUNCTION__, index);
            goto error;
        }

        ALOGI("%s: Successfully downloaded patch segment: %d",
        __FUNCTION__, index);
    }

error:
    return err;
}

static int rome_download_rampatch(int fd)
{
    int c, tmp, size, index, ret = -1;

    ALOGI("%s: ", __FUNCTION__);

    /* Get handle to the RAMPATCH binary file */
    ALOGI("%s: Getting handle to the RAMPATCH binary file from %s", __FUNCTION__, ROME_FW_PATH);
    file = fopen(ROME_FW_PATH, "r");
    if (file == NULL) {
        ALOGE("%s: Failed to get handle to the RAMPATCH bin file!",
        __FUNCTION__);
        return -ENFILE;
    }

    /* Allocate memory for the patch headder info */
    ALOGI("%s: Allocating memory for the patch header", __FUNCTION__);
    phdr_buffer = (unsigned char *) malloc(PATCH_HDR_LEN + 1);
    if (phdr_buffer == NULL) {
        ALOGE("%s: Failed to allocate memory for patch header",
        __FUNCTION__);
        goto phdr_alloc_failed;
    }
    for (index = 0; index < PATCH_HDR_LEN + 1; index++)
        phdr_buffer[index] = 0x0;

    /* Read 28 bytes of patch header information */
    ALOGI("%s: Reading patch header info", __FUNCTION__);
    index = 0;
    do {
        c = fgetc (file);
        phdr_buffer[index++] = (unsigned char)c;
    } while (index != PATCH_HDR_LEN);

    /* Save the patch header info into local structure */
    ALOGI("%s: Saving patch hdr. info", __FUNCTION__);
    rome_extract_patch_header_info((unsigned char *)phdr_buffer);

    /* Set the patch header info onto the Controller */
    ret = rome_edl_set_patch_request(fd);
    if (ret < 0) {
        ALOGE("%s: Error setting the patchheader info!", __FUNCTION__);
        goto pdata_alloc_failed;
    }

    /* Allocate memory for the patch payload */
    ALOGI("%s: Allocating memory for patch payload", __FUNCTION__);
    size = rampatch_patch_info.patch_length;
    pdata_buffer = (unsigned char *) malloc(size+1);
    if (pdata_buffer == NULL) {
        ALOGE("%s: Failed to allocate memory for patch payload",
            __FUNCTION__);
        goto pdata_alloc_failed;
    }
    for (index = 0; index < size+1; index++)
        pdata_buffer[index] = 0x0;

    /* Read the patch data from Rampatch binary image */
    ALOGI("%s: Reading patch payload from RAMPATCH file", __FUNCTION__);
    index = 0;
    do {
        c = fgetc (file);
        pdata_buffer[index++] = (unsigned char)c;
    } while (c != EOF);
    fclose(file);

    /* Downloading patches in segments to controller */
    ret = rome_edl_patch_download_request(fd);
    if (ret < 0) {
        ALOGE("%s: Error downloading patch segments!", __FUNCTION__);
        goto cleanup;
    }
cleanup:
    free(pdata_buffer);
pdata_alloc_failed:
    free(phdr_buffer);
phdr_alloc_failed:
    fclose(file);
error:
    return ret;
}

int rome_attach_rampatch(int fd)
{
    int size, err;
    unsigned char cmd[HCI_MAX_CMD_SIZE];
    unsigned char rsp[HCI_MAX_EVENT_SIZE];

    /* Frame the HCI CMD to be sent to the Controller */
    frame_hci_cmd_pkt(cmd, EDL_PATCH_ATCH_REQ_CMD, 0,
        -1, EDL_PATCH_CMD_LEN);

    /* Total length of the packet to be sent to the Controller */
    size = (HCI_CMD_IND	+ HCI_COMMAND_HDR_SIZE + cmd[PLEN]);

    /* Send HCI Command packet to Controller */
    err = hci_send_vs_cmd(fd, (unsigned char *)cmd, rsp, size);
    if ( err != size) {
        ALOGE("Failed to attach the patch payload to the Controller!");
        goto error;
    }

    /* Read Command Complete Event */
    err = read_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE);
    if ( err < 0) {
        ALOGE("%s: Failed to attach the patch segment(s)", __FUNCTION__);
        goto error;
    }
error:
    return err;
}

int rome_rampatch_reset(int fd)
{
    int size, err = 0, flags;
    unsigned char cmd[HCI_MAX_CMD_SIZE];
    struct timespec tm = { 0, 100*1000*1000 }; /* 100 ms */

    /* Frame the HCI CMD to be sent to the Controller */
    frame_hci_cmd_pkt(cmd, EDL_PATCH_RST_REQ_CMD, 0,
                                        -1, EDL_PATCH_CMD_LEN);

    /* Total length of the packet to be sent to the Controller */
    size = (HCI_CMD_IND	+ HCI_COMMAND_HDR_SIZE + EDL_PATCH_CMD_LEN);

    /* Send HCI Command packet to Controller */
    err = write(fd, cmd, size);
    if (err != size) {
        ALOGE("%s: Send failed with ret value: %d", __FUNCTION__, err);
        goto error;
    }

    /*
    * Controller doesn't sends any response for the patch reset
    * command. HOST has to wait for 100ms before proceeding.
    */
    nanosleep(&tm, NULL);

error:
    return err;
}

int rome_get_tlv_file(char *file_path)
{
    FILE * pFile;
    long fileSize;
    int readSize, err = 0, total_segment, remain_size, nvm_length, nvm_index, i;
    unsigned short nvm_tag_len;
    tlv_patch_info *ptlv_header;
    tlv_nvm_hdr *nvm_ptr;
    unsigned char data_buf[PRINT_BUF_SIZE]={0,};
    unsigned char *nvm_byte_ptr;

    ALOGI("File Open (%s)", file_path);
    pFile = fopen ( file_path , "r" );
    if (pFile==NULL) {;
        ALOGE("%s File Open Fail", file_path);
        return -1;
    }

    /* Get File Size */
    fseek (pFile , 0 , SEEK_END);
    fileSize = ftell (pFile);
    rewind (pFile);

    pdata_buffer = (unsigned char*) malloc (sizeof(char)*fileSize);
    if (pdata_buffer == NULL) {
        ALOGE("Allocated Memory failed");
        return -1;
    }

    /* Copy file into allocated buffer */
    readSize = fread (pdata_buffer,1,fileSize,pFile);

    /* File Close */
    fclose (pFile);

    if (readSize != fileSize) {
        ALOGE("Read file size(%d) not matched with actual file size (%d bytes)",readSize,fileSize);
        return -1;
    }

    ptlv_header = (tlv_patch_info *) pdata_buffer;

    /* To handle different event between rampatch and NVM */
    gTlv_type = ptlv_header->tlv_type;

    if(ptlv_header->tlv_type == TLV_TYPE_PATCH){
        ALOGI("====================================================");
        ALOGI("TLV Type\t\t\t : 0x%x", ptlv_header->tlv_type);
        ALOGI("Length\t\t\t : %d bytes", (ptlv_header->tlv_length1) |
                                                    (ptlv_header->tlv_length2 << 8) |
                                                    (ptlv_header->tlv_length3 << 16));
        ALOGI("Signing Format Version\t : 0x%x", ptlv_header->tlv.patch.sign_ver);
        ALOGI("Signature Algorithm\t\t : 0x%x", ptlv_header->tlv.patch.sign_algorithm);
        ALOGI("Reserved\t\t\t : 0x%x", ptlv_header->tlv.patch.reserved1);
        ALOGI("Product ID\t\t\t : 0x%x\n", ptlv_header->tlv.patch.prod_id);
        ALOGI("Rom Build Version\t\t : 0x%x\n", ptlv_header->tlv.patch.build_ver);
        ALOGI("Patch Version\t\t : 0x%x\n", ptlv_header->tlv.patch.patch_ver);
        ALOGI("Reserved\t\t\t : 0x%x\n", ptlv_header->tlv.patch.reserved2);
        ALOGI("Patch Entry Address\t\t : 0x%x\n", (ptlv_header->tlv.patch.patch_entry_addr));
        ALOGI("====================================================");

    } else if(ptlv_header->tlv_type == TLV_TYPE_NVM) {
        ALOGI("====================================================");
        ALOGI("TLV Type\t\t\t : 0x%x", ptlv_header->tlv_type);
        ALOGI("Length\t\t\t : %d bytes",  nvm_length = (ptlv_header->tlv_length1) |
                                                    (ptlv_header->tlv_length2 << 8) |
                                                    (ptlv_header->tlv_length3 << 16));

        if(nvm_length <= 0)
            return readSize;

       for(nvm_byte_ptr=(unsigned char *)(nvm_ptr = &(ptlv_header->tlv.nvm)), nvm_index=0;
             nvm_index < nvm_length ; nvm_ptr = (tlv_nvm_hdr *) nvm_byte_ptr)
       {
            ALOGI("TAG ID\t\t\t : %d", nvm_ptr->tag_id);
            ALOGI("TAG Length\t\t\t : %d", nvm_tag_len = nvm_ptr->tag_len);
            ALOGI("TAG Pointer\t\t\t : %d", nvm_ptr->tag_ptr);
            ALOGI("TAG Extended Flag\t\t : %d", nvm_ptr->tag_ex_flag);

            /* Increase nvm_index to NVM data */
            nvm_index+=sizeof(tlv_nvm_hdr);
            nvm_byte_ptr+=sizeof(tlv_nvm_hdr);

            /* Write BD Address */
            if(nvm_ptr->tag_id == TAG_NUM_2){
                memcpy(nvm_byte_ptr, vnd_local_bd_addr, 6);
                ALOGI("BD Address: %.02x:%.02x:%.02x:%.02x:%.02x:%.02x",
                    *nvm_byte_ptr, *(nvm_byte_ptr+1), *(nvm_byte_ptr+2),
                    *(nvm_byte_ptr+3), *(nvm_byte_ptr+4), *(nvm_byte_ptr+5));
            }

            for(i =0;(i<nvm_ptr->tag_len && (i*3 + 2) <PRINT_BUF_SIZE);i++)
                snprintf(data_buf, PRINT_BUF_SIZE, "%s%.02x ", data_buf, *(nvm_byte_ptr + i));

            ALOGI("TAG Data\t\t\t : %s", data_buf);

            /* Clear buffer */
            memset(data_buf, 0x0, PRINT_BUF_SIZE);

            /* increased by tag_len */
            nvm_index+=nvm_ptr->tag_len;
            nvm_byte_ptr +=nvm_ptr->tag_len;
        }

        ALOGI("====================================================");

    } else {
        ALOGI("TLV Header type is unknown (%d) ", ptlv_header->tlv_type);
    }

    return readSize;
}

int rome_tlv_dnld_segment(int fd, int index, int seg_size, unsigned char wait_cc_evt)
{
    int size=0, err = -1;
    unsigned char cmd[HCI_MAX_CMD_SIZE];
    unsigned char rsp[HCI_MAX_EVENT_SIZE];

    ALOGI("%s: Downloading TLV Patch segment no.%d, size:%d", __FUNCTION__, index, seg_size);

    /* Frame the HCI CMD PKT to be sent to Controller*/
    frame_hci_cmd_pkt(cmd, EDL_PATCH_TLV_REQ_CMD, 0, index, seg_size);

    /* Total length of the packet to be sent to the Controller */
    size = (HCI_CMD_IND + HCI_COMMAND_HDR_SIZE + cmd[PLEN]);

    /* Initialize the RSP packet everytime to 0 */
    memset(rsp, 0x0, HCI_MAX_EVENT_SIZE);

    /* Send HCI Command packet to Controller */
    err = hci_send_vs_cmd(fd, (unsigned char *)cmd, rsp, size);
    if ( err != size) {
        ALOGE("Failed to send the patch payload to the Controller! 0x%x", err);
        return err;
    }

    if(wait_cc_evt) {
        err = read_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE);
        if ( err < 0) {
            ALOGE("%s: Failed to downlaod patch segment: %d!",  __FUNCTION__, index);
            return err;
        }
    }

    ALOGI("%s: Successfully downloaded patch segment: %d", __FUNCTION__, index);
    return err;
}

int rome_tlv_dnld_req(int fd, int tlv_size)
{
    int  total_segment, remain_size, i, err = -1;
    unsigned char wait_cc_evt;

    total_segment = tlv_size/MAX_SIZE_PER_TLV_SEGMENT;
    remain_size = (tlv_size < MAX_SIZE_PER_TLV_SEGMENT)?\
        tlv_size: (tlv_size%MAX_SIZE_PER_TLV_SEGMENT);

    ALOGI("%s: TLV size: %d, Total Seg num: %d, remain size: %d",
        __FUNCTION__,tlv_size, total_segment, remain_size);

    for(i=0;i<total_segment ;i++){
        /* In case of ROME 1.1, last rampatch segment command will not wait for
            command complete event */
        wait_cc_evt = ((rome_ver == ROME_VER_1_1 ) && (gTlv_type == TLV_TYPE_PATCH )
             && !remain_size && ((i+1) == total_segment))? FALSE: TRUE;
        if(err = rome_tlv_dnld_segment(fd, i, MAX_SIZE_PER_TLV_SEGMENT, wait_cc_evt ) < 0)
            goto error;
    }

    /* In case remain data still remain, last rampatch segment command will not wait
        for command complete event here */
    wait_cc_evt = ((rome_ver == ROME_VER_1_1 ) && (gTlv_type == TLV_TYPE_PATCH )
         && remain_size )? FALSE:TRUE;

    if(remain_size) err =rome_tlv_dnld_segment(fd, i, remain_size, wait_cc_evt);

error:
    return err;
}

int rome_download_tlv_file(int fd)
{
    int tlv_size, err = -1;

    /* Rampatch TLV file Downloading */
    pdata_buffer = NULL;
    if((tlv_size = rome_get_tlv_file(ROME_RAMPATCH_TLV_PATH)) < 0)
        goto error;

    if((err =rome_tlv_dnld_req(fd, tlv_size)) <0 )
        goto error;

    if (pdata_buffer != NULL){
        free (pdata_buffer);
        pdata_buffer = NULL;
    }

    /* NVM TLV file Downloading */
    if((tlv_size = rome_get_tlv_file(ROME_NVM_TLV_PATH)) < 0)
        goto error;

    if((err =rome_tlv_dnld_req(fd, tlv_size)) <0 )
        goto error;

error:
    if (pdata_buffer != NULL)
        free (pdata_buffer);

    return err;
}

int rome_1_0_nvm_tag_dnld(int fd)
{
    int i, size, err = 0;
    unsigned char cmd[HCI_MAX_CMD_SIZE];
    unsigned char rsp[HCI_MAX_EVENT_SIZE];

#if (NVM_VERSION >= ROME_1_0_100019)
    unsigned char cmds[MAX_TAG_CMD][HCI_MAX_CMD_SIZE] =
    {
        /* Tag 2 */ /* BD Address */
        {  /* Packet Type */HCI_COMMAND_PKT,
            /* Opcode */       0x0b,0xfc,
            /* Total Len */     9,
            /* NVM CMD */    NVM_ACCESS_SET,
            /* Tag Num */     2,
            /* Tag Len */      6,
            /* Tag Value */   0x77,0x78,0x23,0x01,0x56,0x22
         },
        /* Tag 6 */ /* Bluetooth Support Features */
        {  /* Packet Type */HCI_COMMAND_PKT,
            /* Opcode */       0x0b,0xfc,
            /* Total Len */     11,
            /* NVM CMD */    NVM_ACCESS_SET,
            /* Tag Num */     6,
            /* Tag Len */      8,
            /* Tag Value */   0xFF,0xFE,0x8B,0xFE,0xD8,0x3F,0x5B,0x8B
         },
        /* Tag 17 */ /* HCI Transport Layer Setting */
        {  /* Packet Type */HCI_COMMAND_PKT,
            /* Opcode */       0x0b,0xfc,
            /* Total Len */     11,
            /* NVM CMD */    NVM_ACCESS_SET,
            /* Tag Num */     17,
            /* Tag Len */      8,
            /* Tag Value */   0x82,0x01,0x0E,0x08,0x04,0x32,0x0A,0x00
         },
        /* Tag 35 */
        {  /* Packet Type */HCI_COMMAND_PKT,
            /* Opcode */       0x0b,0xfc,
            /* Total Len */     58,
            /* NVM CMD */    NVM_ACCESS_SET,
            /* Tag Num */     35,
            /* Tag Len */      55,
            /* Tag Value */   0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x58, 0x59,
                                      0x0E, 0x0E, 0x16, 0x16, 0x16, 0x1E, 0x26, 0x5F, 0x2F, 0x5F,
                                      0x0E, 0x0E, 0x16, 0x16, 0x16, 0x1E, 0x26, 0x5F, 0x2F, 0x5F,
                                      0x0C, 0x18, 0x14, 0x24, 0x40, 0x4C, 0x70, 0x80, 0x80, 0x80,
                                      0x0C, 0x18, 0x14, 0x24, 0x40, 0x4C, 0x70, 0x80, 0x80, 0x80,
                                      0x1B, 0x14, 0x01, 0x04, 0x48
         },
        /* Tag 36 */
        {  /* Packet Type */HCI_COMMAND_PKT,
            /* Opcode */       0x0b,0xfc,
            /* Total Len */     15,
            /* NVM CMD */    NVM_ACCESS_SET,
            /* Tag Num */     36,
            /* Tag Len */      12,
            /* Tag Value */   0x0F,0x00,0x03,0x03,0x03,0x03,0x00,0x00,0x03,0x03,0x04,0x00
         },
        /* Tag 39 */
        {  /* Packet Type */HCI_COMMAND_PKT,
            /* Opcode */       0x0b,0xfc,
            /* Total Len */     7,
            /* NVM CMD */    NVM_ACCESS_SET,
            /* Tag Num */     39,
            /* Tag Len */      4,
            /* Tag Value */   0x12,0x00,0x00,0x00
         },
        /* Tag 41 */
        {  /* Packet Type */HCI_COMMAND_PKT,
            /* Opcode */       0x0b,0xfc,
            /* Total Len */     91,
            /* NVM CMD */    NVM_ACCESS_SET,
            /* Tag Num */     41,
            /* Tag Len */      88,
            /* Tag Value */   0x15, 0x00, 0x00, 0x00, 0xF6, 0x02, 0x00, 0x00, 0x76, 0x00,
                                      0x1E, 0x00, 0x29, 0x02, 0x1F, 0x00, 0x61, 0x00, 0x1A, 0x00,
                                      0x76, 0x00, 0x1E, 0x00, 0x7D, 0x00, 0x40, 0x00, 0x91, 0x00,
                                      0x06, 0x00, 0x92, 0x00, 0x03, 0x00, 0xA6, 0x01, 0x50, 0x00,
                                      0xAA, 0x01, 0x15, 0x00, 0xAB, 0x01, 0x0A, 0x00, 0xAC, 0x01,
                                      0x00, 0x00, 0xB0, 0x01, 0xC5, 0x00, 0xB3, 0x01, 0x03, 0x00,
                                      0xB4, 0x01, 0x13, 0x00, 0xB5, 0x01, 0x0C, 0x00, 0xC5, 0x01,
                                      0x0D, 0x00, 0xC6, 0x01, 0x10, 0x00, 0xCA, 0x01, 0x2B, 0x00,
                                      0xCB, 0x01, 0x5F, 0x00, 0xCC, 0x01, 0x48, 0x00
         },
        /* Tag 42 */
        {  /* Packet Type */HCI_COMMAND_PKT,
            /* Opcode */       0x0b,0xfc,
            /* Total Len */     63,
            /* NVM CMD */    NVM_ACCESS_SET,
            /* Tag Num */     42,
            /* Tag Len */      60,
            /* Tag Value */   0xD7, 0xC0, 0x00, 0x00, 0x8F, 0x5C, 0x02, 0x00, 0x80, 0x47,
                                      0x60, 0x0C, 0x70, 0x4C, 0x00, 0x00, 0x00, 0x01, 0x1F, 0x01,
                                      0x42, 0x01, 0x69, 0x01, 0x95, 0x01, 0xC7, 0x01, 0xFE, 0x01,
                                      0x3D, 0x02, 0x83, 0x02, 0xD1, 0x02, 0x29, 0x03, 0x00, 0x0A,
                                      0x10, 0x00, 0x1F, 0x00, 0x3F, 0x00, 0x7F, 0x00, 0xFD, 0x00,
                                      0xF9, 0x01, 0xF1, 0x03, 0xDE, 0x07, 0x00, 0x00, 0x9A, 0x01
         },
        /* Tag 84 */
        {  /* Packet Type */HCI_COMMAND_PKT,
            /* Opcode */       0x0b,0xfc,
            /* Total Len */     153,
            /* NVM CMD */    NVM_ACCESS_SET,
            /* Tag Num */     84,
            /* Tag Len */      150,
            /* Tag Value */   0x7C, 0x6A, 0x59, 0x47, 0x19, 0x36, 0x35, 0x25, 0x25, 0x28,
                                      0x2C, 0x2B, 0x2B, 0x28, 0x2C, 0x28, 0x29, 0x28, 0x29, 0x28,
                                      0x29, 0x29, 0x2C, 0x29, 0x2C, 0x29, 0x2C, 0x28, 0x29, 0x28,
                                      0x29, 0x28, 0x29, 0x2A, 0x00, 0x00, 0x2C, 0x2A, 0x2C, 0x18,
                                      0x98, 0x98, 0x98, 0x98, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E,
                                      0x1E, 0x13, 0x1E, 0x1E, 0x1E, 0x1E, 0x13, 0x13, 0x11, 0x13,
                                      0x1E, 0x1E, 0x13, 0x12, 0x12, 0x12, 0x11, 0x12, 0x1F, 0x12,
                                      0x12, 0x12, 0x10, 0x0C, 0x18, 0x0D, 0x01, 0x01, 0x01, 0x01,
                                      0x01, 0x01, 0x01, 0x0C, 0x01, 0x01, 0x01, 0x01, 0x0D, 0x0D,
                                      0x0E, 0x0D, 0x01, 0x01, 0x0D, 0x0D, 0x0D, 0x0D, 0x0F, 0x0D,
                                      0x10, 0x0D, 0x0D, 0x0D, 0x0D, 0x10, 0x05, 0x10, 0x03, 0x00,
                                      0x7E, 0x7B, 0x7B, 0x72, 0x71, 0x50, 0x50, 0x50, 0x00, 0x40,
                                      0x60, 0x60, 0x30, 0x08, 0x02, 0x0F, 0x00, 0x01, 0x00, 0x00,
                                      0x00, 0x00, 0x00, 0x00, 0x08, 0x16, 0x16, 0x08, 0x08, 0x00,
                                      0x00, 0x00, 0x1E, 0x34, 0x2B, 0x1B, 0x23, 0x2B, 0x15, 0x0D
         },
        /* Tag 85 */
        {  /* Packet Type */HCI_COMMAND_PKT,
            /* Opcode */       0x0b,0xfc,
            /* Total Len */     119,
            /* NVM CMD */    NVM_ACCESS_SET,
            /* Tag Num */     85,
            /* Tag Len */      116,
            /* Tag Value */   0x03, 0x00, 0x38, 0x00, 0x45, 0x77, 0x00, 0xE8, 0x00, 0x59,
                                      0x01, 0xCA, 0x01, 0x3B, 0x02, 0xAC, 0x02, 0x1D, 0x03, 0x8E,
                                      0x03, 0x00, 0x89, 0x01, 0x0E, 0x02, 0x5C, 0x02, 0xD7, 0x02,
                                      0xF8, 0x08, 0x01, 0x00, 0x1F, 0x00, 0x0A, 0x02, 0x55, 0x02,
                                      0x00, 0x35, 0x00, 0x00, 0x00, 0x00, 0x2A, 0xD7, 0x00, 0x00,
                                      0x00, 0x1E, 0xDE, 0x00, 0x00, 0x00, 0x14, 0x0F, 0x0A, 0x0F,
                                      0x0A, 0x0C, 0x0C, 0x0C, 0x0C, 0x04, 0x04, 0x04, 0x0C, 0x0C,
                                      0x0C, 0x0C, 0x06, 0x06, 0x00, 0x02, 0x02, 0x02, 0x02, 0x02,
                                      0x01, 0x00, 0x02, 0x02, 0x02, 0x02, 0x01, 0x00, 0x00, 0x00,
                                      0x06, 0x0F, 0x14, 0x05, 0x47, 0xCF, 0x77, 0x00, 0x00, 0x00,
                                      0x00, 0x00, 0x00, 0xAC, 0x7C, 0xFF, 0x40, 0x00, 0x00, 0x00,
                                      0x12, 0x04, 0x04, 0x01, 0x04, 0x03
         },
        {TAG_END}
    };
#elif (NVM_VERSION == ROME_1_0_6002)
    unsigned char cmds[MAX_TAG_CMD][HCI_MAX_CMD_SIZE] =
    {
        /* Tag 2 */
        {  /* Packet Type */HCI_COMMAND_PKT,
            /* Opcode */       0x0b,0xfc,
            /* Total Len */     9,
            /* NVM CMD */    NVM_ACCESS_SET,
            /* Tag Num */     2,
            /* Tag Len */      6,
            /* Tag Value */   0x77,0x78,0x23,0x01,0x56,0x22 /* BD Address */
         },
        /* Tag 6 */
        {  /* Packet Type */HCI_COMMAND_PKT,
            /* Opcode */       0x0b,0xfc,
            /* Total Len */     11,
            /* NVM CMD */    NVM_ACCESS_SET,
            /* Tag Num */     6,
            /* Tag Len */      8,
            /* Tag Value */   0xFF,0xFE,0x8B,0xFE,0xD8,0x3F,0x5B,0x8B
         },
        /* Tag 17 */
        {  /* Packet Type */HCI_COMMAND_PKT,
            /* Opcode */       0x0b,0xfc,
            /* Total Len */     11,
            /* NVM CMD */    NVM_ACCESS_SET,
            /* Tag Num */     17,
            /* Tag Len */      8,
            /* Tag Value */   0x82,0x01,0x0E,0x08,0x04,0x32,0x0A,0x00
         },
        /* Tag 36 */
        {  /* Packet Type */HCI_COMMAND_PKT,
            /* Opcode */       0x0b,0xfc,
            /* Total Len */     15,
            /* NVM CMD */    NVM_ACCESS_SET,
            /* Tag Num */     36,
            /* Tag Len */      12,
            /* Tag Value */   0x0F,0x00,0x03,0x03,0x03,0x03,0x00,0x00,0x03,0x03,0x04,0x00
         },

        /* Tag 39 */
        {  /* Packet Type */HCI_COMMAND_PKT,
            /* Opcode */       0x0b,0xfc,
            /* Total Len */     7,
            /* NVM CMD */    NVM_ACCESS_SET,
            /* Tag Num */     39,
            /* Tag Len */      4,
            /* Tag Value */   0x12,0x00,0x00,0x00
         },

        /* Tag 41 */
        {  /* Packet Type */HCI_COMMAND_PKT,
            /* Opcode */       0x0b,0xfc,
            /* Total Len */     199,
            /* NVM CMD */    NVM_ACCESS_SET,
            /* Tag Num */     41,
            /* Tag Len */      196,
            /* Tag Value */   0x30,0x00,0x00,0x00,0xD5,0x00,0x0E,0x00,0xD6,0x00,0x0E,0x00,
                                      0xD7,0x00,0x16,0x00,0xD8,0x00,0x16,0x00,0xD9,0x00,0x16,0x00,
                                      0xDA,0x00,0x1E,0x00,0xDB,0x00,0x26,0x00,0xDC,0x00,0x5F,0x00,
                                      0xDD,0x00,0x2F,0x00,0xDE,0x00,0x5F,0x00,0xE0,0x00,0x0E,0x00,
                                      0xE1,0x00,0x0E,0x00,0xE2,0x00,0x16,0x00,0xE3,0x00,0x16,0x00,
                                      0xE4,0x00,0x16,0x00,0xE5,0x00,0x1E,0x00,0xE6,0x00,0x26,0x00,
                                      0xE7,0x00,0x5F,0x00,0xE8,0x00,0x2F,0x00,0xE9,0x00,0x5F,0x00,
                                      0xEC,0x00,0x0C,0x00,0xED,0x00,0x08,0x00,0xEE,0x00,0x14,0x00,
                                      0xEF,0x00,0x24,0x00,0xF0,0x00,0x40,0x00,0xF1,0x00,0x4C,0x00,
                                      0xF2,0x00,0x70,0x00,0xF3,0x00,0x80,0x00,0xF4,0x00,0x80,0x00,
                                      0xF5,0x00,0x80,0x00,0xF8,0x00,0x0C,0x00,0xF9,0x00,0x18,0x00,
                                      0xFA,0x00,0x14,0x00,0xFB,0x00,0x24,0x00,0xFC,0x00,0x40,0x00,
                                      0xFD,0x00,0x4C,0x00,0xFE,0x00,0x70,0x00,0xFF,0x00,0x80,0x00,
                                      0x00,0x01,0x80,0x00,0x01,0x01,0x80,0x00,0x04,0x01,0x1B,0x00,
                                      0x05,0x01,0x14,0x00,0x06,0x01,0x01,0x00,0x07,0x01,0x04,0x00,
                                      0x08,0x01,0x00,0x00,0x09,0x01,0x00,0x00,0x0A,0x01,0x03,0x00,
                                      0x0B,0x01,0x03,0x00
         },

        /* Tag 44 */
        {  /* Packet Type */HCI_COMMAND_PKT,
            /* Opcode */       0x0b,0xfc,
            /* Total Len */     44,
            /* NVM CMD */    NVM_ACCESS_SET,
            /* Tag Num */     44,
            /* Tag Len */      41,
            /* Tag Value */   0x6F,0x0A,0x00,0x00,0x00,0x00,0x00,0x50,0xFF,0x10,0x02,0x02,
                                      0x01,0x00,0x14,0x01,0x06,0x28,0xA0,0x62,0x03,0x64,0x01,0x01,
                                      0x0A,0x00,0x00,0x00,0x00,0x00,0x00,0xA0,0xFF,0x10,0x02,0x01,
                                      0x00,0x14,0x01,0x02,0x03
         },
        {TAG_END}
    };
#endif

    ALOGI("%s: Start sending NVM Tags (ver: 0x%x)", __FUNCTION__, NVM_VERSION);

    for (i=0; (i < MAX_TAG_CMD) && (cmds[i][0] != TAG_END); i++)
    {
        /* Write BD Address */
        if(cmds[i][TAG_NUM_OFFSET] == TAG_NUM_2){
            memcpy(&cmds[i][TAG_BDADDR_OFFSET], vnd_local_bd_addr, 6);
            ALOGI("BD Address: %.2x:%.2x:%.2x:%.2x:%.2x:%.2x",
                cmds[i][TAG_BDADDR_OFFSET ], cmds[i][TAG_BDADDR_OFFSET + 1],
                cmds[i][TAG_BDADDR_OFFSET + 2], cmds[i][TAG_BDADDR_OFFSET + 3],
                cmds[i][TAG_BDADDR_OFFSET + 4], cmds[i][TAG_BDADDR_OFFSET + 5]);
        }
        size = cmds[i][3] + HCI_COMMAND_HDR_SIZE + 1;
        /* Send HCI Command packet to Controller */
        err = hci_send_vs_cmd(fd, (unsigned char *)&cmds[i][0], rsp, size);
        if ( err != size) {
            ALOGE("Failed to attach the patch payload to the Controller!");
            goto error;
        }

        /* Read Command Complete Event - This is extra routine for ROME 1.0. From ROM 2.0, it should be removed. */
        err = read_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE);
        if ( err < 0) {
            ALOGE("%s: Failed to get patch version(s)", __FUNCTION__);
            goto error;
        }
    }

error:
    return err;
}



int rome_patch_ver_req(int fd)
{
    int size, err = 0;
    unsigned char cmd[HCI_MAX_CMD_SIZE];
    unsigned char rsp[HCI_MAX_EVENT_SIZE];

    /* Frame the HCI CMD to be sent to the Controller */
    frame_hci_cmd_pkt(cmd, EDL_PATCH_VER_REQ_CMD, 0,
    -1, EDL_PATCH_CMD_LEN);

    /* Total length of the packet to be sent to the Controller */
    size = (HCI_CMD_IND + HCI_COMMAND_HDR_SIZE + EDL_PATCH_CMD_LEN);

    /* Send HCI Command packet to Controller */
    err = hci_send_vs_cmd(fd, (unsigned char *)cmd, rsp, size);
    if ( err != size) {
        ALOGE("Failed to attach the patch payload to the Controller!");
        goto error;
    }

    /* Read Command Complete Event - This is extra routine for ROME 1.0. From ROM 2.0, it should be removed. */
    err = read_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE);
    if ( err < 0) {
        ALOGE("%s: Failed to get patch version(s)", __FUNCTION__);
        goto error;
    }
error:
    return err;

}


int rome_set_baudrate_req(int fd)
{
    int size, err = 0;
    unsigned char cmd[HCI_MAX_CMD_SIZE];
    unsigned char rsp[HCI_MAX_EVENT_SIZE];
    hci_command_hdr *cmd_hdr;
    int flags;

    memset(cmd, 0x0, HCI_MAX_CMD_SIZE);

    cmd_hdr = (void *) (cmd + 1);
    cmd[0]  = HCI_COMMAND_PKT;
    cmd_hdr->opcode = cmd_opcode_pack(HCI_VENDOR_CMD_OGF, EDL_SET_BAUDRATE_CMD_OCF);
    cmd_hdr->plen     = VSC_SET_BAUDRATE_REQ_LEN;
    cmd[4]  = BAUDRATE_3000000;

    /* Total length of the packet to be sent to the Controller */
    size = (HCI_CMD_IND + HCI_COMMAND_HDR_SIZE + VSC_SET_BAUDRATE_REQ_LEN);

    /* Flow off during baudrate change */
    if ((err = userial_vendor_ioctl(USERIAL_OP_FLOW_OFF , &flags)) < 0)
    {
      ALOGE("%s: HW Flow-off error: 0x%x\n", __FUNCTION__, err);
      goto error;
    }

    /* Send the HCI command packet to UART for transmission */
    err = write(fd, cmd, size);
    if (err != size) {
        ALOGE("%s: Send failed with ret value: %d", __FUNCTION__, err);
        goto error;
    }

    /* Change Local UART baudrate to high speed UART */
    userial_vendor_set_baud(USERIAL_BAUD_3M);

    /* Flow on after changing local uart baudrate */
    if ((err = userial_vendor_ioctl(USERIAL_OP_FLOW_ON , &flags)) < 0)
    {
        ALOGE("%s: HW Flow-on error: 0x%x \n", __FUNCTION__, err);
        return err;
    }

    /* Check for response from the Controller */
    if ((err =read_vs_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE)) < 0) {
            ALOGE("%s: Failed to get HCI-VS Event from SOC", __FUNCTION__);
            goto error;
    }

    ALOGI("%s: Received HCI-Vendor Specific Event from SOC", __FUNCTION__);

    /* Wait for command complete event */
    err = read_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE);
    if ( err < 0) {
        ALOGE("%s: Failed to set patch info on Controller", __FUNCTION__);
        goto error;
    }

error:
    return err;

}


int rome_hci_reset_req(int fd)
{
    int size, err = 0;
    unsigned char cmd[HCI_MAX_CMD_SIZE];
    unsigned char rsp[HCI_MAX_EVENT_SIZE];
    hci_command_hdr *cmd_hdr;
    int flags;

    ALOGI("%s: HCI RESET ", __FUNCTION__);

    memset(cmd, 0x0, HCI_MAX_CMD_SIZE);

    cmd_hdr = (void *) (cmd + 1);
    cmd[0]  = HCI_COMMAND_PKT;
    cmd_hdr->opcode = HCI_RESET;
    cmd_hdr->plen   = 0;

    /* Total length of the packet to be sent to the Controller */
    size = (HCI_CMD_IND + HCI_COMMAND_HDR_SIZE);

    /* Flow off during baudrate change */
    if ((err = userial_vendor_ioctl(USERIAL_OP_FLOW_OFF , &flags)) < 0)
    {
      ALOGE("%s: HW Flow-off error: 0x%x\n", __FUNCTION__, err);
      goto error;
    }

    /* Send the HCI command packet to UART for transmission */
    ALOGI("%s: HCI CMD: 0x%x 0x%x 0x%x 0x%x\n", __FUNCTION__, cmd[0], cmd[1], cmd[2], cmd[3]);
    err = write(fd, cmd, size);
    if (err != size) {
        ALOGE("%s: Send failed with ret value: %d", __FUNCTION__, err);
        goto error;
    }

    /* Change Local UART baudrate to high speed UART */
    userial_vendor_set_baud(USERIAL_BAUD_3M);

    /* Flow on after changing local uart baudrate */
    if ((err = userial_vendor_ioctl(USERIAL_OP_FLOW_ON , &flags)) < 0)
    {
        ALOGE("%s: HW Flow-on error: 0x%x \n", __FUNCTION__, err);
        return err;
    }

    /* Wait for command complete event */
    err = read_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE);
    if ( err < 0) {
        ALOGE("%s: Failed to set patch info on Controller", __FUNCTION__);
        goto error;
    }

error:
    return err;

}

int rome_soc_init(int fd, char *bdaddr)
{
    int err = -1, size = 0;

    ALOGI(" %s ", __FUNCTION__);

    /* Get Rome version information */
    if((err = rome_patch_ver_req(fd)) <0){
        ALOGI("%s: Fail to get Rome Version (0x%x)", __FUNCTION__, err);
        goto error;
    }

    ALOGI("%s: Rome Version (0x%x)", __FUNCTION__, rome_ver);

    switch (rome_ver){
        case ROME_VER_1_0:
            {
                /* Set and Download the RAMPATCH */
                ALOGI("%s: Setting Patch Header & Downloading Patches", __FUNCTION__);
                err = rome_download_rampatch(fd);
                if (err < 0) {
                    ALOGE("%s: DOWNLOAD RAMPATCH failed!", __FUNCTION__);
                    goto error;
                }
                ALOGI("%s: DOWNLOAD RAMPTACH complete", __FUNCTION__);

                /* Attach the RAMPATCH */
                ALOGI("%s: Attaching the patches", __FUNCTION__);
                err = rome_attach_rampatch(fd);
                if (err < 0) {
                    ALOGE("%s: ATTACH RAMPATCH failed!", __FUNCTION__);
                    goto error;
                }
                ALOGI("%s: ATTACH RAMPTACH complete", __FUNCTION__);

                /* Send Reset */
                size = (HCI_CMD_IND + HCI_COMMAND_HDR_SIZE + EDL_PATCH_CMD_LEN);
                err = rome_rampatch_reset(fd);
                if ( err < 0 ) {
                    ALOGE("Failed to RESET after RAMPATCH upgrade!");
                    goto error;
                }

                /* NVM download */
                ALOGI("%s: Downloading NVM", __FUNCTION__);
                err = rome_1_0_nvm_tag_dnld(fd);
                if ( err <0 ) {
                    ALOGE("Downloading NVM Failed !!");
                    goto error;
                }

                /* Change baud rate 115.2 kbps to 3Mbps*/
                err = rome_hci_reset_req(fd);
                if ( err <0 ) {
                    ALOGE("HCI Reset Failed !!");
                    goto error;
                }

                ALOGI("HCI Reset is done\n");
            }
            break;
        case ROME_VER_1_1:
            {
                /* Change baud rate 115.2 kbps to 3Mbps*/
                err = rome_set_baudrate_req(fd);
                if (err < 0) {
                    ALOGE("%s: Baud rate change failed!", __FUNCTION__);
                    goto error;
                }
                ALOGI("%s: Baud rate changed successfully ", __FUNCTION__);

                /* Donwload TLV files (rampatch, NVM) */
                err = rome_download_tlv_file(fd);
                if (err < 0) {
                    ALOGE("%s: Download TLV file failed!", __FUNCTION__);
                    goto error;
                }
                ALOGI("%s: Download TLV file successfully ", __FUNCTION__);

                /* HCI Reset will be implemented by Bluedroid Stack */
            }
            break;
        case ROME_VER_UNKNOWN:
        default:
            ALOGI("%s: Detected unknown ROME version", __FUNCTION__);
            err = -1;
            break;
    }

error:
    return err;
}
