// SPDX-License-Identifier: GPL-2.0-only
/*
 * Galaxy A70 bring-up helper: power a camera sensor (supplies, MCLK, reset) at probe and keep it
 * powered, so its I2C address / chip ID can be found over CCI from userspace before a real
 * sensor driver exists. Debug only.
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>

static const char * const campower_supplies[] = { "vio", "vana", "vdig", "vaf" };

static int campower_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct regulator_bulk_data *supplies;
	struct gpio_desc *reset;
	struct clk *mclk;
	u32 rate = 24000000;
	int i, ret;

	supplies = devm_kcalloc(dev, ARRAY_SIZE(campower_supplies), sizeof(*supplies), GFP_KERNEL);
	if (!supplies)
		return -ENOMEM;
	for (i = 0; i < ARRAY_SIZE(campower_supplies); i++)
		supplies[i].supply = campower_supplies[i];
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(campower_supplies), supplies);
	if (ret)
		return dev_err_probe(dev, ret, "supplies\n");

	reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);	/* asserted */
	if (IS_ERR(reset))
		return dev_err_probe(dev, PTR_ERR(reset), "reset gpio\n");

	mclk = devm_clk_get(dev, NULL);
	if (IS_ERR(mclk))
		return dev_err_probe(dev, PTR_ERR(mclk), "mclk\n");
	of_property_read_u32(dev->of_node, "clock-frequency", &rate);

	/* downstream order: vio, vana, vdig (vaf), then mclk, then reset release */
	ret = regulator_bulk_enable(ARRAY_SIZE(campower_supplies), supplies);
	if (ret)
		return dev_err_probe(dev, ret, "enable supplies\n");
	usleep_range(1000, 2000);

	ret = clk_set_rate(mclk, rate);
	if (!ret)
		ret = clk_prepare_enable(mclk);
	if (ret) {
		regulator_bulk_disable(ARRAY_SIZE(campower_supplies), supplies);
		return dev_err_probe(dev, ret, "mclk enable\n");
	}
	usleep_range(1000, 2000);

	gpiod_set_value_cansleep(reset, 0);
	msleep(10);

	dev_info(dev, "camera powered: mclk %lu Hz\n", clk_get_rate(mclk));
	return 0;
}

static const struct of_device_id campower_of_match[] = {
	{ .compatible = "samsung,a70q-campower" },
	{ }
};
MODULE_DEVICE_TABLE(of, campower_of_match);

static struct platform_driver campower_driver = {
	.driver = {
		.name = "a70q-campower",
		.of_match_table = campower_of_match,
	},
	.probe = campower_probe,
};
module_platform_driver(campower_driver);

MODULE_DESCRIPTION("Galaxy A70 camera power bring-up helper");
MODULE_LICENSE("GPL");
