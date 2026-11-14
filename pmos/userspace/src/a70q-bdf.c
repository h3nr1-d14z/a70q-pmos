/*
 * a70q-bdf: download the WLAN board data file (bdwlan.bin) to WCN3990 firmware
 * over QMI WLFW (service 0x45, msg 0x25 BDF_DOWNLOAD), like Android's cnss-daemon
 * or mainline ath10k_snoc do. The downstream icnss driver never sends it, so the
 * firmware never reports FW_READY and wlan0 never appears.
 *
 *   a70q-bdf <node> <port> <bdwlan.bin> [file_id]
 * (node/port of service 69 from qrtr-lookup)
 * Build: gcc -static -O2 -o a70q-bdf a70q-bdf.c
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <linux/qrtr.h>

#ifndef AF_QIPCRTR
#define AF_QIPCRTR 42
#endif
#define WLFW_BDF_DOWNLOAD 0x0025
#define WLFW_CAL_REPORT 0x0026
#define SEG_MAX 6144

static uint8_t buf[SEG_MAX + 128];

static size_t tlv(uint8_t *p, uint8_t type, const void *val, uint16_t len)
{
	p[0] = type; p[1] = len & 0xff; p[2] = len >> 8;
	memcpy(p + 3, val, len);
	return 3 + len;
}

int main(int argc, char **argv)
{
	if (argc < 4) {
		fprintf(stderr, "usage: %s <node> <port> <bdf> [file_id]\n", argv[0]);
		return 2;
	}
	struct sockaddr_qrtr dst = { .sq_family = AF_QIPCRTR,
		.sq_node = strtoul(argv[1], 0, 0), .sq_port = strtoul(argv[2], 0, 0) };
	int32_t file_id = argc > 4 ? atoi(argv[4]) : 0;
	FILE *f = fopen(argv[3], "rb");
	if (!f) { perror(argv[3]); return 1; }
	fseek(f, 0, SEEK_END); uint32_t total = ftell(f); fseek(f, 0, SEEK_SET);

	int fd = socket(AF_QIPCRTR, SOCK_DGRAM, 0);
	if (fd < 0) { perror("socket"); return 1; }
	struct timeval tv = { .tv_sec = 5 };
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	uint32_t seg = 0, sent = 0;
	uint16_t txn = 1;
	printf("BDF %s: %u bytes -> %u:%u file_id %d\n", argv[3], total,
	       dst.sq_node, dst.sq_port, file_id);
	while (sent < total || (total == 0 && seg == 0)) {
		uint8_t data[SEG_MAX];
		uint16_t n = fread(data, 1, SEG_MAX, f);
		uint8_t end = (sent + n >= total);
		uint8_t one = 1, *p = buf + 7;
		uint8_t dtlv[2 + SEG_MAX];

		p += tlv(p, 0x01, &one, 1);			/* valid */
		p += tlv(p, 0x10, &file_id, 4);			/* file_id */
		p += tlv(p, 0x11, &total, 4);			/* total_size */
		p += tlv(p, 0x12, &seg, 4);			/* seg_id */
		dtlv[0] = n & 0xff; dtlv[1] = n >> 8; memcpy(dtlv + 2, data, n);
		p += tlv(p, 0x13, dtlv, 2 + n);			/* data_len + data */
		p += tlv(p, 0x14, &end, 1);			/* end */
		uint16_t mlen = p - (buf + 7);
		buf[0] = 0;					/* request */
		buf[1] = txn & 0xff; buf[2] = txn >> 8;
		buf[3] = WLFW_BDF_DOWNLOAD & 0xff; buf[4] = WLFW_BDF_DOWNLOAD >> 8;
		buf[5] = mlen & 0xff; buf[6] = mlen >> 8;
		if (sendto(fd, buf, 7 + mlen, 0, (void *)&dst, sizeof(dst)) < 0) {
			perror("sendto"); return 1;
		}
		uint8_t r[256];
		ssize_t rl = recv(fd, r, sizeof(r), 0);
		if (rl < 7) { fprintf(stderr, "seg %u: no response (%s)\n", seg, strerror(errno)); return 1; }
		uint16_t rtxn = r[1] | r[2] << 8, rmsg = r[3] | r[4] << 8;
		/* resp TLV 0x02: result(u16) error(u16) */
		uint16_t res = 0xffff, err = 0xffff;
		for (ssize_t i = 7; i + 3 <= rl; ) {
			uint16_t l = r[i + 1] | r[i + 2] << 8;
			if (r[i] == 0x02 && l >= 4) { res = r[i+3] | r[i+4] << 8; err = r[i+5] | r[i+6] << 8; }
			i += 3 + l;
		}
		printf("seg %u len %u end %u -> type %u txn %u msg 0x%x result %u error %u\n",
		       seg, n, end, r[0], rtxn, rmsg, res, err);
		if (res != 0) return 1;
		sent += n; seg++; txn++;
		if (end) break;
	}
	printf("BDF download done (%u segments)\n", seg);

	/* CAL_REPORT with no host calibration files (what ath10k_snoc sends);
	 * firmware sends FW_READY_IND to its registered client (icnss) after this */
	{
		uint8_t *p = buf + 7, zero = 0;
		p += tlv(p, 0x01, &zero, 1);			/* meta_data_len = 0 */
		uint16_t mlen = p - (buf + 7);
		buf[0] = 0; buf[1] = txn & 0xff; buf[2] = txn >> 8;
		buf[3] = WLFW_CAL_REPORT & 0xff; buf[4] = WLFW_CAL_REPORT >> 8;
		buf[5] = mlen & 0xff; buf[6] = mlen >> 8;
		if (sendto(fd, buf, 7 + mlen, 0, (void *)&dst, sizeof(dst)) < 0) {
			perror("sendto cal"); return 1;
		}
		uint8_t r[256];
		ssize_t rl = recv(fd, r, sizeof(r), 0);
		if (rl < 7) { fprintf(stderr, "cal report: no response (%s)\n", strerror(errno)); return 1; }
		uint16_t res = 0xffff, err = 0xffff;
		for (ssize_t i = 7; i + 3 <= rl; ) {
			uint16_t l = r[i + 1] | r[i + 2] << 8;
			if (r[i] == 0x02 && l >= 4) { res = r[i+3] | r[i+4] << 8; err = r[i+5] | r[i+6] << 8; }
			i += 3 + l;
		}
		printf("CAL_REPORT -> msg 0x%x result %u error %u\n", r[3] | r[4] << 8, res, err);
		if (res != 0) return 1;
	}
	return 0;
}
