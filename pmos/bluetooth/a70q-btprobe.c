// a70q Bluetooth bring-up probe: power the WCN399x BT core through the downstream btpower
// driver and talk raw HCI (H4) over the GENI UART, before any kernel HCI driver is involved.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <poll.h>
#include <sys/ioctl.h>

#define BT_CMD_PWR_CTRL 0xbfad

static int rd(int fd, unsigned char *b, int max, int ms) {
	int n = 0;
	for (;;) {
		struct pollfd p = { fd, POLLIN, 0 };
		if (poll(&p, 1, ms) <= 0) break;
		int r = read(fd, b + n, max - n);
		if (r <= 0) break;
		n += r; ms = 100;
	}
	return n;
}
static void dump(const char *t, unsigned char *b, int n) {
	printf("%s (%d):", t, n);
	for (int i = 0; i < n; i++) printf(" %02x", b[i]);
	printf("\n");
}
static void cmd(int fd, const char *name, const unsigned char *c, int len) {
	unsigned char b[512];
	tcflush(fd, TCIFLUSH);
	if (write(fd, c, len) != len) perror("write");
	int q = -1; ioctl(fd, TIOCOUTQ, &q);
	if (q) printf("  (%d bytes still queued after write)\n", q);
	int n = rd(fd, b, sizeof b, 1000);
	dump(name, b, n);
}

int main(int argc, char **argv) {
	const char *tty = argc > 1 ? argv[1] : "/dev/ttyHS0";
	int pw = open("/dev/btpower", O_RDWR);
	if (pw < 0) { perror("/dev/btpower"); return 1; }
	if (ioctl(pw, BT_CMD_PWR_CTRL, 1) < 0) perror("BT_CMD_PWR_CTRL 1");
	usleep(100000);
	int fd = open(tty, O_RDWR | O_NOCTTY);
	if (fd < 0) { perror(tty); return 1; }
	// downstream msm_geni_serial (hs) only clocks the port after a userspace vote
	// (Android's BT vendor HAL does this): TIOCPMGET = vote on, TIOCPMPUT = vote off
	if (ioctl(fd, 0x544D, 0) < 0) perror("TIOCPMGET");
	int flow = getenv("NOFLOW") ? 0 : 1;
	struct termios t; tcgetattr(fd, &t); cfmakeraw(&t);
	cfsetspeed(&t, B115200); t.c_cflag |= CLOCAL | CREAD; t.c_cflag &= ~CSTOPB;
	if (flow) t.c_cflag |= CRTSCTS; else t.c_cflag &= ~CRTSCTS;
	tcsetattr(fd, TCSANOW, &t);
	int m = 0; ioctl(fd, TIOCMGET, &m);
	printf("flow=%d modem lines: CTS=%d DSR=%d CD=%d RTS=%d DTR=%d\n", flow,
		!!(m & TIOCM_CTS), !!(m & TIOCM_DSR), !!(m & TIOCM_CD), !!(m & TIOCM_RTS), !!(m & TIOCM_DTR));
	unsigned char junk[256]; dump("unsolicited", junk, rd(fd, junk, sizeof junk, 300));
	const unsigned char reset[] = { 0x01, 0x03, 0x0c, 0x00 };
	cmd(fd, "HCI_Reset", reset, sizeof reset);
	// QCA EDL vendor command 0xFC00: EDL_PATCH_VER_REQ_CMD (0x19)
	const unsigned char ver[] = { 0x01, 0x00, 0xfc, 0x01, 0x19 };
	cmd(fd, "EDL version", ver, sizeof ver);
	const unsigned char lver[] = { 0x01, 0x01, 0x10, 0x00 }; // HCI_Read_Local_Version
	cmd(fd, "Read_Local_Version", lver, sizeof lver);
	ioctl(fd, 0x544E, 0);
	close(fd);
	if (argc < 3) ioctl(pw, BT_CMD_PWR_CTRL, 0);
	return 0;
}
