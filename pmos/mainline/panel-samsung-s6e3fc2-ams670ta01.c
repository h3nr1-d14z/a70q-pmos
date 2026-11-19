// SPDX-License-Identifier: GPL-2.0-only
/*
 * Samsung S6E3FC2 DDIC with the AMS670TA01 6.7" 1080x2400 AMOLED panel (Samsung Galaxy A70).
 *
 * Command mode, 4 lanes, TE on the panel's vsync line. The power-on and power-off sequences are
 * the ones of the downstream "ss_dsi_panel_S6E3FC2_AMS670TA01_FHD" panel description
 * (qcom,mdss-dsi-on-command / qcom,mdss-dsi-off-command); brightness is DCS 0x51 with 10 bits.
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

struct ams670ta01 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator *vddio;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *enable_gpio;
};

static inline struct ams670ta01 *to_ams670ta01(struct drm_panel *panel)
{
	return container_of(panel, struct ams670ta01, panel);
}

#define key1_on(c)	mipi_dsi_dcs_write_seq_multi(c, 0xf0, 0x5a, 0x5a)
#define key1_off(c)	mipi_dsi_dcs_write_seq_multi(c, 0xf0, 0xa5, 0xa5)
#define key2_on(c)	mipi_dsi_dcs_write_seq_multi(c, 0xfc, 0x5a, 0x5a)
#define key2_off(c)	mipi_dsi_dcs_write_seq_multi(c, 0xfc, 0xa5, 0xa5)

static void ams670ta01_reset(struct ams670ta01 *ctx)
{
	/* qcom,mdss-dsi-reset-sequence = <0 10 1 10> (physical levels, reset is active low) */
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	msleep(10);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(10);
}

static int ams670ta01_on(struct ams670ta01 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	key1_on(&dsi_ctx);
	mipi_dsi_dcs_set_tear_on_multi(&dsi_ctx, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	key1_off(&dsi_ctx);

	mipi_dsi_dcs_set_column_address_multi(&dsi_ctx, 0, 1080 - 1);
	mipi_dsi_dcs_set_page_address_multi(&dsi_ctx, 0, 2400 - 1);

	key1_on(&dsi_ctx);
	key2_on(&dsi_ctx);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xed, 0x00, 0x4c);
	key1_off(&dsi_ctx);
	key2_off(&dsi_ctx);

	key1_on(&dsi_ctx);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xeb, 0x1b, 0x41, 0xb0, 0x0e, 0x00, 0x1e,
				     0x5a, 0x1e, 0x1e);
	key1_off(&dsi_ctx);

	key1_on(&dsi_ctx);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x20);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb7, 0x00, 0x01, 0x53, 0x28, 0x4d, 0x00,
					 0x90, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_DISPLAY_BRIGHTNESS, 0x03, 0xff);
	key1_off(&dsi_ctx);

	return dsi_ctx.accum_err;
}

static int ams670ta01_off(struct ams670ta01 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);

	key1_on(&dsi_ctx);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf4, 0x01);
	key1_off(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	return dsi_ctx.accum_err;
}

static int ams670ta01_prepare(struct drm_panel *panel)
{
	struct ams670ta01 *ctx = to_ams670ta01(panel);
	int ret;

	ret = regulator_enable(ctx->vddio);
	if (ret)
		return ret;
	msleep(20);	/* qcom,supply-post-on-sleep */
	gpiod_set_value_cansleep(ctx->enable_gpio, 1);

	/*
	 * Now and then (warm boot from the downstream kernel) the first DCS commands time out
	 * (-ETIMEDOUT) and the panel stays dark, after which the vblank timeouts flood the log and
	 * slow everything down. Reset the panel and try again a couple of times.
	 */
	for (int tries = 0; tries < 3; tries++) {
		if (tries) {
			dev_warn(&ctx->dsi->dev, "panel init failed (%d), retry %d\n", ret, tries);
			gpiod_set_value_cansleep(ctx->reset_gpio, 1);
			msleep(50);
		}
		ams670ta01_reset(ctx);
		ret = ams670ta01_on(ctx);
		if (ret >= 0)
			break;
	}
	if (ret < 0) {
		dev_err(&ctx->dsi->dev, "panel init failed: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		gpiod_set_value_cansleep(ctx->enable_gpio, 0);
		regulator_disable(ctx->vddio);
		return ret;
	}

	return 0;
}

static int ams670ta01_enable(struct drm_panel *panel)
{
	struct ams670ta01 *ctx = to_ams670ta01(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);

	return dsi_ctx.accum_err;
}

static int ams670ta01_disable(struct drm_panel *panel)
{
	struct ams670ta01 *ctx = to_ams670ta01(panel);
	int ret;

	ret = ams670ta01_off(ctx);
	if (ret < 0)
		dev_err(&ctx->dsi->dev, "panel off failed: %d\n", ret);

	return 0;
}

static int ams670ta01_unprepare(struct drm_panel *panel)
{
	struct ams670ta01 *ctx = to_ams670ta01(panel);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	gpiod_set_value_cansleep(ctx->enable_gpio, 0);
	regulator_disable(ctx->vddio);

	return 0;
}

/* timings of the downstream "fhd" mode: h 120/121/122, v 16/2/2, 60 Hz */
static const struct drm_display_mode ams670ta01_mode = {
	.clock = (1080 + 120 + 121 + 122) * (2400 + 16 + 2 + 2) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 120,
	.hsync_end = 1080 + 120 + 121,
	.htotal = 1080 + 120 + 121 + 122,
	.vdisplay = 2400,
	.vsync_start = 2400 + 16,
	.vsync_end = 2400 + 16 + 2,
	.vtotal = 2400 + 16 + 2 + 2,
	.width_mm = 70,
	.height_mm = 155,
	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static int ams670ta01_get_modes(struct drm_panel *panel, struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &ams670ta01_mode);
}

static const struct drm_panel_funcs ams670ta01_panel_funcs = {
	.prepare = ams670ta01_prepare,
	.enable = ams670ta01_enable,
	.disable = ams670ta01_disable,
	.unprepare = ams670ta01_unprepare,
	.get_modes = ams670ta01_get_modes,
};

static int ams670ta01_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);

	return mipi_dsi_dcs_set_display_brightness_large(dsi, brightness);
}

static const struct backlight_ops ams670ta01_bl_ops = {
	.update_status = ams670ta01_bl_update_status,
};

/* read back the panel state (DCS get_power_mode, brightness, DSI error count) */
static ssize_t panel_state_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(dev);
	u8 mode = 0, errs = 0;
	u8 bl[2] = { 0 };
	int r1, r2, r3;

	r1 = mipi_dsi_dcs_get_power_mode(dsi, &mode);
	r2 = mipi_dsi_dcs_read(dsi, MIPI_DCS_GET_DISPLAY_BRIGHTNESS, bl, sizeof(bl));
	r3 = mipi_dsi_dcs_read(dsi, 0x05, &errs, 1);

	return sysfs_emit(buf, "power_mode %02x (%d) brightness %02x%02x (%d) dsi_errors %02x (%d)\n",
			  mode, r1, bl[0], bl[1], r2, errs, r3);
}
static DEVICE_ATTR_RO(panel_state);

static int ams670ta01_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 511,
		.max_brightness = 1023,
	};
	struct ams670ta01 *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct ams670ta01, panel, &ams670ta01_panel_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ctx->vddio = devm_regulator_get(dev, "vddio");
	if (IS_ERR(ctx->vddio))
		return dev_err_probe(dev, PTR_ERR(ctx->vddio), "vddio supply\n");

	/* the bootloader left the panel on: keep it so until the first modeset */
	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio), "reset gpio\n");
	ctx->enable_gpio = devm_gpiod_get_optional(dev, "enable", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->enable_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->enable_gpio), "enable gpio\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS;

	ctx->panel.prepare_prev_first = true;

	ctx->panel.backlight = devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
							      &ams670ta01_bl_ops, &props);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight), "backlight\n");

	drm_panel_add(&ctx->panel);
	device_create_file(dev, &dev_attr_panel_state);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "failed to attach to DSI host: %d\n", ret);
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	return 0;
}

static void ams670ta01_remove(struct mipi_dsi_device *dsi)
{
	struct ams670ta01 *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id ams670ta01_of_match[] = {
	{ .compatible = "samsung,s6e3fc2-ams670ta01" },
	{ }
};
MODULE_DEVICE_TABLE(of, ams670ta01_of_match);

static struct mipi_dsi_driver ams670ta01_driver = {
	.probe = ams670ta01_probe,
	.remove = ams670ta01_remove,
	.driver = {
		.name = "panel-samsung-s6e3fc2-ams670ta01",
		.of_match_table = ams670ta01_of_match,
	},
};
module_mipi_dsi_driver(ams670ta01_driver);

MODULE_DESCRIPTION("DRM driver for the Samsung S6E3FC2 AMS670TA01 AMOLED panel");
MODULE_LICENSE("GPL");
