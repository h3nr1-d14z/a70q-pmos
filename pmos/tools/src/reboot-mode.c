/* reboot-mode: reboot(2) with a mode string, e.g. "download" (Samsung Download Mode),
 * "recovery", "bootloader". For initramfs/rootfs shells without Android's reboot tool.
 * Build static: cc -static -Os -s -o reboot-mode reboot-mode.c */
#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/reboot.h>
int main(int argc, char **argv) {
	const char *mode = argc > 1 ? argv[1] : "download";
	sync();
	long r = syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
			 LINUX_REBOOT_CMD_RESTART2, mode);
	perror("reboot");
	return (int)r;
}
