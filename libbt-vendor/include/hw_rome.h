/*
 * Copyright 2012 The Android Open Source Project
 * Copyright (c) 2013, The Linux Foundation. All rights reserved.
 * Not a Contribution.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */
#ifndef HW_ROME_H
#define HW_ROME_H

/******************************************************************************
**  Constants & Macros
******************************************************************************/
#define HCI_MAX_CMD_SIZE        260
#define HCI_MAX_EVENT_SIZE     260
#define PRINT_BUF_SIZE              ((HCI_MAX_CMD_SIZE * 3) + 2)

#define HCI_CHG_BAUD_CMD_OCF        0x0C
#define HCI_VENDOR_CMD_OGF             0x3F
#define WRITE_BDADDR_CMD_LEN        14
#define WRITE_BAUD_CMD_LEN             6
#define MAX_CMD_LEN                    WRITE_BDADDR_CMD_LEN
#define GET_VERSION_OCF            0x1E

#define PS_HDR_LEN                         4
#define HCI_VENDOR_CMD_OGF      0x3F
#define HCI_PS_CMD_OCF                0x0B

#define HCI_COMMAND_HDR_SIZE        3
#define EVT_CMD_COMPLETE_SIZE       3
#define EVT_CMD_STATUS                     0x0F
#define EVT_CMD_STATUS_SIZE           4
#define HCI_EVENT_HDR_SIZE              2
#define HCI_EV_SUCCESS                      0x00
/* HCI Socket options */
#define HCI_DATA_DIR            1
#define HCI_FILTER                  2
#define HCI_TIME_STAMP        3

#define P_ID_OFFSET                                     (0)
#define HCI_CMD_IND                                   (1)
#define EVENTCODE_OFFSET                         (1)
#define PLEN                                                   (2)
#define CMD_RSP_OFFSET                             (3)
#define RSP_TYPE_OFFSET                            (4)
#define BAUDRATE_RSP_STATUS_OFFSET    (4)
#define CMD_STATUS_OFFSET                      (5)
#define P_ROME_VER_OFFSET                       (4)
#define P_BUILD_VER_OFFSET                      (6)
#define P_BASE_ADDR_OFFSET                     (8)
#define P_ENTRY_ADDR_OFFSET                   (12)
#define P_LEN_OFFSET                                   (16)
#define P_CRC_OFFSET                                  (20)
#define P_CONTROL_OFFSET                          (24)
#define PATCH_HDR_LEN                               (28)
#define MAX_DATA_PER_SEGMENT                (239)
#define VSEVENT_CODE                                 (0xFF)
#define HC_VS_MAX_CMD_EVENT                 (0xFF)
#define PATCH_PROD_ID_OFFSET                (5)
#define PATCH_CURR_FW_VER_OFFSET       (9)
#define PATCH_CHIPSET_VER_OFFSET        (11)
#define MAX_SIZE_PER_TLV_SEGMENT        (243)

/* VS Opcode */
#define HCI_PATCH_CMD_OCF                       (0)
#define EDL_SET_BAUDRATE_CMD_OCF        (0x48)

/* VS Commands */
#define VSC_SET_BAUDRATE_REQ_LEN        (1)
#define EDL_PATCH_CMD_LEN	                       (1)
#define EDL_PATCH_CMD_REQ_LEN               (1)
#define EDL_PATCH_DLD_REQ_CMD               (0x01)
#define EDL_PATCH_RST_REQ_CMD               (0x05)
#define EDL_PATCH_SET_REQ_CMD               (0x16)
#define EDL_PATCH_ATCH_REQ_CMD            (0x17)
#define EDL_PATCH_VER_REQ_CMD               (0x19)
#define EDL_PATCH_TLV_REQ_CMD               (0x1E)

/* VS Event */
#define EDL_CMD_REQ_RES_EVT                 (0x00)
#define EDL_CMD_EXE_STATUS_EVT           (0x00)
#define EDL_SET_BAUDRATE_RSP_EVT       (0x92)
#define EDL_PATCH_VER_RES_EVT             (0x19)
#define EDL_TVL_DNLD_RES_EVT                (0x04)
#define EDL_APP_VER_RES_EVT                  (0x02)


/* Status Codes of HCI CMD execution*/
#define HCI_CMD_SUCCESS                     (0x0)
#define PATCH_LEN_ERROR                       (0x1)
#define PATCH_VER_ERROR                       (0x2)
#define PATCH_CRC_ERROR                     (0x3)
#define PATCH_NOT_FOUND                      (0x4)
#define TLV_TYPE_ERROR                         (0x10)
#define NVM_ACCESS_CODE                     (0x0B)
#define BAUDRATE_CHANGE_SUCCESS   (1)

/* TLV_TYPE */
#define TLV_TYPE_PATCH                  (1)
#define TLV_TYPE_NVM                      (2)

/* NVM */
#define MAX_TAG_CMD                 30
#define TAG_END                           0xFF
#define NVM_ACCESS_SET            0x01
#define TAG_NUM_OFFSET             5
#define TAG_NUM_2                       2
#define TAG_BDADDR_OFFSET     7

/* NVM Tags specifically used for ROME 1.0 */
#define ROME_1_0_100022_1       0x101000221
#define ROME_1_0_100019           0x101000190
#define ROME_1_0_6002               0x100600200

/* Default NVM Version setting for ROME 1.0 */
#define NVM_VERSION                  ROME_1_0_100022_1


#define LSH(val, n)     ((uint32_t)(val) << (n))
#define EXTRACT_BYTE(val, pos)      (char) (((val) >> (8 * (pos))) & 0xFF)
#define CALC_SEG_SIZE(len, max)   ((plen) % (max))?((plen/max)+1) : ((plen) / (max))

#define ROME_FW_PATH        "/system/etc/firmware/rampatch.img"
#define ROME_RAMPATCH_TLV_PATH      "/system/etc/firmware/rampatch_tlv.img"
#define ROME_NVM_TLV_PATH         "/system/etc/firmware/nvm_tlv.bin"

/******************************************************************************
**  Local type definitions
******************************************************************************/
typedef struct {
    unsigned short rom_version;
    unsigned short build_version;
} __attribute__ ((packed)) patch_version;

typedef struct {
    unsigned int patch_id;
    patch_version patch_ver;
    unsigned int patch_base_addr;
    unsigned int patch_entry_addr;
    unsigned short patch_length;
    int patch_crc;
    unsigned short patch_ctrl;
} __attribute__ ((packed)) patch_info;

typedef struct {
    unsigned char sign_ver;
    unsigned char sign_algorithm;
    unsigned short reserved1;
    unsigned short prod_id;
    unsigned short build_ver;
    unsigned short patch_ver;
    unsigned short reserved2;
    unsigned int patch_entry_addr;
} __attribute__ ((packed)) tlv_patch_hdr;

typedef struct {
    unsigned short tag_id;
    unsigned short tag_len;
    unsigned int tag_ptr;
    unsigned int tag_ex_flag;
} __attribute__ ((packed)) tlv_nvm_hdr;

typedef struct {
    unsigned char tlv_type;
    unsigned char tlv_length1;
    unsigned char tlv_length2;
    unsigned char tlv_length3;

    union{
        tlv_patch_hdr patch;
        tlv_nvm_hdr nvm;
    }tlv;
} __attribute__ ((packed)) tlv_patch_info;

enum{
    BAUDRATE_115200     = 0x00,
    BAUDRATE_57600       = 0x01,
    BAUDRATE_38400       = 0x02,
    BAUDRATE_19200       = 0x03,
    BAUDRATE_9600         = 0x04,
    BAUDRATE_230400     = 0x05,
    BAUDRATE_250000     = 0x06,
    BAUDRATE_460800     = 0x07,
    BAUDRATE_500000     = 0x08,
    BAUDRATE_720000     = 0x09,
    BAUDRATE_921600     = 0x0A,
    BAUDRATE_1000000   = 0x0B,
    BAUDRATE_1250000   = 0x0C,
    BAUDRATE_2000000   = 0x0D,
    BAUDRATE_3000000   = 0x0E,
    BAUDRATE_4000000   = 0x0F,
    BAUDRATE_1600000   = 0x10,
    BAUDRATE_3200000   = 0x11,
    BAUDRATE_3500000   = 0x12,
    BAUDRATE_AUTO        = 0xFE,
    BAUDRATE_Reserved  = 0xFF
};

enum{
    ROME_VER_UNKNOWN = 0,
    ROME_VER_1_0 = 0x0100,
    ROME_VER_1_1 = 0x0101
};
#endif /* HW_ROME_H */
