/*
 * ata_id - reads product/serial number from ATA drives
 *
 * Copyright (C) 2005-2008 Kay Sievers <kay@vrfy.org>
 * Copyright (C) 2009 Lennart Poettering <lennart@poettering.net>
 * Copyright (C) 2009-2010 David Zeuthen <zeuthen@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <scsi/scsi.h>
#include <scsi/sg.h>
#include <scsi/scsi_ioctl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/hdreg.h>
#include <linux/bsg.h>

//#include "libudev.h"
//#include "libudev-private.h"
//#include "udev-util.h"
//#include "log.h"

#define COMMAND_TIMEOUT_MSEC (30 * 1000)

static int disk_scsi_inquiry_command(int      fd,
                                     void    *buf,
                                     size_t   buf_len)
{
        uint8_t cdb[6] = {
                /*
                 * INQUIRY, see SPC-4 section 6.4
                 */
                [0] = 0x12,                /* OPERATION CODE: INQUIRY */
                [3] = (buf_len >> 8),      /* ALLOCATION LENGTH */
                [4] = (buf_len & 0xff),
        };
        uint8_t sense[32] = {};
        struct sg_io_v4 io_v4 = {
                .guard = 'Q',
                .protocol = BSG_PROTOCOL_SCSI,
                .subprotocol = BSG_SUB_PROTOCOL_SCSI_CMD,
                .request_len = sizeof(cdb),
                .request = (uintptr_t) cdb,
                .max_response_len = sizeof(sense),
                .response = (uintptr_t) sense,
                .din_xfer_len = buf_len,
                .din_xferp = (uintptr_t) buf,
                .timeout = COMMAND_TIMEOUT_MSEC,
        };
        int ret;

        ret = ioctl(fd, SG_IO, &io_v4);
        if (ret != 0) {
                /* could be that the driver doesn't do version 4, try version 3 */
                if (errno == EINVAL) {
                        struct sg_io_hdr io_hdr = {
                                .interface_id = 'S',
                                .cmdp = (unsigned char*) cdb,
                                .cmd_len = sizeof (cdb),
                                .dxferp = buf,
                                .dxfer_len = buf_len,
                                .sbp = sense,
                                .mx_sb_len = sizeof(sense),
                                .dxfer_direction = SG_DXFER_FROM_DEV,
                                .timeout = COMMAND_TIMEOUT_MSEC,
                        };

                        ret = ioctl(fd, SG_IO, &io_hdr);
                        if (ret != 0)
                                return ret;

                        /* even if the ioctl succeeds, we need to check the return value */
                        if (!(io_hdr.status == 0 &&
                              io_hdr.host_status == 0 &&
                              io_hdr.driver_status == 0)) {
                                errno = EIO;
                                return -1;
                        }
                } else
                        return ret;
        }

        /* even if the ioctl succeeds, we need to check the return value */
        if (!(io_v4.device_status == 0 &&
              io_v4.transport_status == 0 &&
              io_v4.driver_status == 0)) {
                errno = EIO;
                return -1;
        }

        return 0;
}

static int disk_identify_command(int          fd,
                                 void         *buf,
                                 size_t          buf_len)
{
        uint8_t cdb[12] = {
                /*
                 * ATA Pass-Through 12 byte command, as described in
                 *
                 *  T10 04-262r8 ATA Command Pass-Through
                 *
                 * from http://www.t10.org/ftp/t10/document.04/04-262r8.pdf
                 */
                [0] = 0xa1,     /* OPERATION CODE: 12 byte pass through */
                [1] = 4 << 1,   /* PROTOCOL: PIO Data-in */
                [2] = 0x2e,     /* OFF_LINE=0, CK_COND=1, T_DIR=1, BYT_BLOK=1, T_LENGTH=2 */
                [3] = 0,        /* FEATURES */
                [4] = 1,        /* SECTORS */
                [5] = 0,        /* LBA LOW */
                [6] = 0,        /* LBA MID */
                [7] = 0,        /* LBA HIGH */
                [8] = 0 & 0x4F, /* SELECT */
                [9] = 0xEC,     /* Command: ATA IDENTIFY DEVICE */
        };
        uint8_t sense[32] = {};
        uint8_t *desc = sense + 8;
        struct sg_io_v4 io_v4 = {
                .guard = 'Q',
                .protocol = BSG_PROTOCOL_SCSI,
                .subprotocol = BSG_SUB_PROTOCOL_SCSI_CMD,
                .request_len = sizeof(cdb),
                .request = (uintptr_t) cdb,
                .max_response_len = sizeof(sense),
                .response = (uintptr_t) sense,
                .din_xfer_len = buf_len,
                .din_xferp = (uintptr_t) buf,
                .timeout = COMMAND_TIMEOUT_MSEC,
        };
        int ret;

        ret = ioctl(fd, SG_IO, &io_v4);
        if (ret != 0) {
                /* could be that the driver doesn't do version 4, try version 3 */
                if (errno == EINVAL) {
                        struct sg_io_hdr io_hdr = {
                                .interface_id = 'S',
                                .cmdp = (unsigned char*) cdb,
                                .cmd_len = sizeof (cdb),
                                .dxferp = buf,
                                .dxfer_len = buf_len,
                                .sbp = sense,
                                .mx_sb_len = sizeof (sense),
                                .dxfer_direction = SG_DXFER_FROM_DEV,
                                .timeout = COMMAND_TIMEOUT_MSEC,
                        };

                        ret = ioctl(fd, SG_IO, &io_hdr);
                        if (ret != 0)
                                return ret;
                } else
                        return ret;
        }

        if (!(sense[0] == 0x72 && desc[0] == 0x9 && desc[1] == 0x0c)) {
                errno = EIO;
                return -1;
        }

        return 0;
}

static int disk_identify_packet_device_command(int          fd,
                                               void         *buf,
                                               size_t          buf_len)
{
        uint8_t cdb[16] = {
                /*
                 * ATA Pass-Through 16 byte command, as described in
                 *
                 *  T10 04-262r8 ATA Command Pass-Through
                 *
                 * from http://www.t10.org/ftp/t10/document.04/04-262r8.pdf
                 */
                [0] = 0x85,   /* OPERATION CODE: 16 byte pass through */
                [1] = 4 << 1, /* PROTOCOL: PIO Data-in */
                [2] = 0x2e,   /* OFF_LINE=0, CK_COND=1, T_DIR=1, BYT_BLOK=1, T_LENGTH=2 */
                [3] = 0,      /* FEATURES */
                [4] = 0,      /* FEATURES */
                [5] = 0,      /* SECTORS */
                [6] = 1,      /* SECTORS */
                [7] = 0,      /* LBA LOW */
                [8] = 0,      /* LBA LOW */
                [9] = 0,      /* LBA MID */
                [10] = 0,     /* LBA MID */
                [11] = 0,     /* LBA HIGH */
                [12] = 0,     /* LBA HIGH */
                [13] = 0,     /* DEVICE */
                [14] = 0xA1,  /* Command: ATA IDENTIFY PACKET DEVICE */
                [15] = 0,     /* CONTROL */
        };
        uint8_t sense[32] = {};
        uint8_t *desc = sense + 8;
        struct sg_io_v4 io_v4 = {
                .guard = 'Q',
                .protocol = BSG_PROTOCOL_SCSI,
                .subprotocol = BSG_SUB_PROTOCOL_SCSI_CMD,
                .request_len = sizeof (cdb),
                .request = (uintptr_t) cdb,
                .max_response_len = sizeof (sense),
                .response = (uintptr_t) sense,
                .din_xfer_len = buf_len,
                .din_xferp = (uintptr_t) buf,
                .timeout = COMMAND_TIMEOUT_MSEC,
        };
        int ret;

        ret = ioctl(fd, SG_IO, &io_v4);
        if (ret != 0) {
                /* could be that the driver doesn't do version 4, try version 3 */
                if (errno == EINVAL) {
                        struct sg_io_hdr io_hdr = {
                                .interface_id = 'S',
                                .cmdp = (unsigned char*) cdb,
                                .cmd_len = sizeof (cdb),
                                .dxferp = buf,
                                .dxfer_len = buf_len,
                                .sbp = sense,
                                .mx_sb_len = sizeof (sense),
                                .dxfer_direction = SG_DXFER_FROM_DEV,
                                .timeout = COMMAND_TIMEOUT_MSEC,
                        };

                        ret = ioctl(fd, SG_IO, &io_hdr);
                        if (ret != 0)
                                return ret;
                } else
                        return ret;
        }

        if (!(sense[0] == 0x72 && desc[0] == 0x9 && desc[1] == 0x0c)) {
                errno = EIO;
                return -1;
        }

        return 0;
}

/**
 * disk_identify_get_string:
 * @identify: A block of IDENTIFY data
 * @offset_words: Offset of the string to get, in words.
 * @dest: Destination buffer for the string.
 * @dest_len: Length of destination buffer, in bytes.
 *
 * Copies the ATA string from @identify located at @offset_words into @dest.
 */
static void disk_identify_get_string(uint8_t identify[512],
                                     unsigned int offset_words,
                                     char *dest,
                                     size_t dest_len)
{
        unsigned int c1;
        unsigned int c2;

        while (dest_len > 0) {
                c1 = identify[offset_words * 2 + 1];
                c2 = identify[offset_words * 2];
                *dest = c1;
                dest++;
                *dest = c2;
                dest++;
                offset_words++;
                dest_len -= 2;
        }
}

static void disk_identify_fixup_string(uint8_t identify[512],
                                       unsigned int offset_words,
                                       size_t len)
{
        disk_identify_get_string(identify, offset_words,
                                 (char *) identify + offset_words * 2, len);
}

static void disk_identify_fixup_uint16 (uint8_t identify[512], unsigned int offset_words)
{
        uint16_t *p;

        p = (uint16_t *) identify;
        p[offset_words] = le16toh (p[offset_words]);
}

/**
 * disk_identify:
 * @udev: The libudev context.
 * @fd: File descriptor for the block device.
 * @out_identify: Return location for IDENTIFY data.
 * @out_is_packet_device: Return location for whether returned data is from a IDENTIFY PACKET DEVICE.
 *
 * Sends the IDENTIFY DEVICE or IDENTIFY PACKET DEVICE command to the
 * device represented by @fd. If successful, then the result will be
 * copied into @out_identify and @out_is_packet_device.
 *
 * This routine is based on code from libatasmart, Copyright 2008
 * Lennart Poettering, LGPL v2.1.
 *
 * Returns: 0 if the data was successfully obtained, otherwise
 * non-zero with errno set.
 */
static int disk_identify(/*struct udev *udev,*/
                         int fd,
                         uint8_t out_identify[512],
                         int *out_is_packet_device)
{
        int ret;
        uint8_t inquiry_buf[36];
        int peripheral_device_type;
        int all_nul_bytes;
        int n;
        int is_packet_device = 0;

        /* init results */
	for (n=0; n<512; n++) {
	  out_identify[n] = '\0';
	}

        /* If we were to use ATA PASS_THROUGH (12) on an ATAPI device
         * we could accidentally blank media. This is because MMC's BLANK
         * command has the same op-code (0x61).
         *
         * To prevent this from happening we bail out if the device
         * isn't a Direct Access Block Device, e.g. SCSI type 0x00
         * (CD/DVD devices are type 0x05). So we send a SCSI INQUIRY
         * command first... libata is handling this via its SCSI
         * emulation layer.
         *
         * This also ensures that we're actually dealing with a device
         * that understands SCSI commands.
         *
         * (Yes, it is a bit perverse that we're tunneling the ATA
         * command through SCSI and relying on the ATA driver
         * emulating SCSI well-enough...)
         *
         * (See commit 160b069c25690bfb0c785994c7c3710289179107 for
         * the original bug-fix and see http://bugs.debian.org/cgi-bin/bugreport.cgi?bug=556635
         * for the original bug-report.)
         */
        ret = disk_scsi_inquiry_command (fd, inquiry_buf, sizeof (inquiry_buf));
        if (ret != 0)
                goto out;

        /* SPC-4, section 6.4.2: Standard INQUIRY data */
        peripheral_device_type = inquiry_buf[0] & 0x1f;
        if (peripheral_device_type == 0x05)
          {
            is_packet_device = 1;
            ret = disk_identify_packet_device_command(fd, out_identify, 512);
            goto check_nul_bytes;
          }
        if (peripheral_device_type != 0x00) {
                ret = -1;
                errno = EIO;
                goto out;
        }

        /* OK, now issue the IDENTIFY DEVICE command */
        ret = disk_identify_command(fd, out_identify, 512);
        if (ret != 0)
                goto out;

 check_nul_bytes:
         /* Check if IDENTIFY data is all NUL bytes - if so, bail */
        all_nul_bytes = 1;
        for (n = 0; n < 512; n++) {
                if (out_identify[n] != '\0') {
                        all_nul_bytes = 0;
                        break;
                }
        }

        if (all_nul_bytes) {
                ret = -1;
                errno = EIO;
                goto out;
        }

out:
        if (out_is_packet_device != NULL)
                *out_is_packet_device = is_packet_device;
        return ret;
}

int main(int argc, char *argv[])
{
  /*        _cleanup_udev_unref_ struct udev *udev = NULL;*/
        struct hd_driveid id;
        union {
                uint8_t  byte[512];
                uint16_t wyde[256];
                uint64_t octa[64];
        } identify;
        char model[41];
        char model_enc[256];
        char serial[21];
        char revision[9];
        const char *node = NULL;
        int export = 0;
        /*_cleanup_close_*/ int fd = -1;
        uint16_t word;
        int is_packet_device = 0;
        static const struct option options[] = {
                { "export", no_argument, NULL, 'x' },
                { "help", no_argument, NULL, 'h' },
                {}
        };

	/*
        log_parse_environment();
        log_open();
	*/
	/*
        udev = udev_new();
        if (udev == NULL)
                return 0;
	*/
        while (1) {
                int option;

                option = getopt_long(argc, argv, "xh", options, NULL);
                if (option == -1)
                        break;

                switch (option) {
                case 'x':
                        export = 1;
                        break;
                case 'h':
                        printf("Usage: ata_id [--export] [--help] <device>\n"
                               "  -x,--export    print values as environment keys\n"
                               "  -h,--help      print this help text\n\n");
                        return 0;
                }
        }

        node = argv[optind];
        if (node == NULL) {
                printf("no node specified\n");
                return 1;
        }

        fd = open(node, O_RDONLY|O_NONBLOCK|O_CLOEXEC);
        if (fd < 0) {
                printf("unable to open '%s'\n", node);
                return 1;
        }

        if (disk_identify(/*udev,*/ fd, identify.byte, &is_packet_device) == 0) {
                /*
                 * fix up only the fields from the IDENTIFY data that we are going to
                 * use and copy it into the hd_driveid struct for convenience
                 */
                disk_identify_fixup_string(identify.byte,  10, 20); /* serial */
                disk_identify_fixup_string(identify.byte,  23,  8); /* fwrev */
                disk_identify_fixup_string(identify.byte,  27, 40); /* model */
                disk_identify_fixup_uint16(identify.byte,  0);      /* configuration */
                disk_identify_fixup_uint16(identify.byte,  75);     /* queue depth */
                disk_identify_fixup_uint16(identify.byte,  75);     /* SATA capabilities */
                disk_identify_fixup_uint16(identify.byte,  82);     /* command set supported */
                disk_identify_fixup_uint16(identify.byte,  83);     /* command set supported */
                disk_identify_fixup_uint16(identify.byte,  84);     /* command set supported */
                disk_identify_fixup_uint16(identify.byte,  85);     /* command set supported */
                disk_identify_fixup_uint16(identify.byte,  86);     /* command set supported */
                disk_identify_fixup_uint16(identify.byte,  87);     /* command set supported */
                disk_identify_fixup_uint16(identify.byte,  89);     /* time required for SECURITY ERASE UNIT */
                disk_identify_fixup_uint16(identify.byte,  90);     /* time required for enhanced SECURITY ERASE UNIT */
                disk_identify_fixup_uint16(identify.byte,  91);     /* current APM values */
                disk_identify_fixup_uint16(identify.byte,  94);     /* current AAM value */
                disk_identify_fixup_uint16(identify.byte, 128);     /* device lock function */
                disk_identify_fixup_uint16(identify.byte, 217);     /* nominal media rotation rate */
                memcpy(&id, identify.byte, sizeof id);
        } else {
                /* If this fails, then try HDIO_GET_IDENTITY */
                if (ioctl(fd, HDIO_GET_IDENTITY, &id) != 0) {
                        printf("HDIO_GET_IDENTITY failed for '%s': %m\n", node);
                        return 2;
                }
        }

        memcpy(model, id.model, 40);
        model[40] = '\0';
	/*        udev_util_encode_string(model, model_enc, sizeof(model_enc));*/
	/*
        util_replace_whitespace((char *) id.model, model, 40);
        util_replace_chars(model, NULL);
        util_replace_whitespace((char *) id.serial_no, serial, 20);
        util_replace_chars(serial, NULL);
        util_replace_whitespace((char *) id.fw_rev, revision, 8);
        util_replace_chars(revision, NULL);
	*/
	if (serial[0] != '\0')
	  printf("%s_%s\n", model, serial);
	else
	  printf("%s\n", model);

        return 0;
}
