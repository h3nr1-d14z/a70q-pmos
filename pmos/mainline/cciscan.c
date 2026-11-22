// SPDX-License-Identifier: GPL-2.0
/* one-shot I2C scanner through the kernel path (read 1 byte at each address), unloads itself */
#include <linux/i2c.h>
#include <linux/module.h>

static int bus = 3;
module_param(bus, int, 0444);

static int __init cciscan_init(void)
{
	struct i2c_adapter *adap = i2c_get_adapter(bus);
	char line[512];
	int addr, n = 0;

	if (!adap)
		return -ENODEV;
	line[0] = 0;
	for (addr = 0x03; addr <= 0x77; addr++) {
		u8 b;
		struct i2c_msg m = { .addr = addr, .flags = I2C_M_RD, .len = 1, .buf = &b };

		if (i2c_transfer(adap, &m, 1) == 1)
			n += scnprintf(line + n, sizeof(line) - n, " %02x", addr);
	}
	pr_info("cciscan bus %d answering:%s\n", bus, n ? line : " none");
	i2c_put_adapter(adap);
	return -EAGAIN;	/* do not stay loaded */
}
module_init(cciscan_init);
MODULE_DESCRIPTION("one-shot I2C bus scan through the kernel I2C path");
MODULE_LICENSE("GPL");
