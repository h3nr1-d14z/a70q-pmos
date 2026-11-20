// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2025 Linaro Ltd
//
// GalaxyCore GC5035 driver (Galaxy A70 depth camera), built on the S5K3M5 driver skeleton;
// register semantics as the ChromeOS gc5035 driver, settings from the device's camera module data.

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/units.h>
#include <media/v4l2-cci.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>

#define GC5035_LINK_FREQ	(438000ULL * HZ_PER_KHZ)
#define GC5035_MCLK_FREQ_24MHZ		(24 * HZ_PER_MHZ)
#define GC5035_DATA_LANES		2

/* Register map is similar to MIPI CCS compliant camera sensors */
#define GC5035_REG_CHIP_ID		CCI_REG16(0x0000)
#define GC5035_CHIP_ID			0x5035

/* Both streaming and flipping settings are controlled at the same time */
#define GC5035_REG_CTRL_MODE		CCI_REG16(0x0100)
#define GC5035_MODE_STREAMING		BIT(8)
#define GC5035_VFLIP			BIT(1)
#define GC5035_HFLIP			BIT(0)

#define GC5035_REG_EXPOSURE		CCI_REG16(0x0202)
#define GC5035_EXPOSURE_MIN		8
#define GC5035_EXPOSURE_STEP		1
#define GC5035_EXPOSURE_MARGIN		16

#define GC5035_REG_AGAIN		CCI_REG8(0xb6)
/* gain table index (page 0, 0xb6) */
#define GC5035_AGAIN_MIN		0
#define GC5035_AGAIN_MAX		31
#define GC5035_AGAIN_STEP		1
#define GC5035_AGAIN_DEFAULT		0

#define GC5035_REG_VTS			CCI_REG16(0x0340)
#define GC5035_VTS_MAX			0xffff

#define GC5035_REG_HTS			CCI_REG16(0x0342)
#define GC5035_REG_X_ADDR_START		CCI_REG16(0x0344)
#define GC5035_REG_Y_ADDR_START		CCI_REG16(0x0346)
#define GC5035_REG_X_ADDR_END		CCI_REG16(0x0348)
#define GC5035_REG_Y_ADDR_END		CCI_REG16(0x034a)
#define GC5035_REG_X_OUTPUT_SIZE	CCI_REG16(0x034c)
#define GC5035_REG_Y_OUTPUT_SIZE	CCI_REG16(0x034e)

#define GC5035_REG_TEST_PATTERN		CCI_REG16(0x0600)

#define to_gc5035(_sd)			container_of(_sd, struct gc5035, sd)

static const s64 gc5035_link_freq_menu[] = {
	GC5035_LINK_FREQ,
};

/* List of supported formats to cover horizontal and vertical flip controls */
static const u32 gc5035_mbus_formats[] = {
	MEDIA_BUS_FMT_SRGGB10_1X10,	MEDIA_BUS_FMT_SRGGB10_1X10,
	MEDIA_BUS_FMT_SRGGB10_1X10,	MEDIA_BUS_FMT_SRGGB10_1X10,
};

struct gc5035_reg_list {
	const struct cci_reg_sequence *regs;
	unsigned int num_regs;
};

struct gc5035_mode {
	u32 width;			/* Frame width in pixels */
	u32 height;			/* Frame height in pixels */
	u32 hts;			/* Horizontal timing size */
	u32 vts;			/* Default vertical timing size */
	u32 exposure;			/* Default exposure value */

	const struct gc5035_reg_list reg_list;	/* Sensor register setting */
};

static const char * const gc5035_test_pattern_menu[] = {
	"Disabled",
	"Solid colour",
	"Colour bars",
	"Fade to grey colour bars",
	"PN9",
};

static const char * const gc5035_supply_names[] = {
	"afvdd",	/* Autofocus power */
	"vdda",		/* Analog power */
	"vddd",		/* Digital core power */
	"vddio",	/* Digital I/O power */
};

#define GC5035_NUM_SUPPLIES	ARRAY_SIZE(gc5035_supply_names)

struct gc5035 {
	struct device *dev;
	struct regmap *regmap;
	struct clk *mclk;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *mipi_sel_gpio;	/* shared MIPI lane switch (optional) */
	u32 mipi_sel_value;
	struct regulator_bulk_data supplies[GC5035_NUM_SUPPLIES];

	struct v4l2_subdev sd;
	struct media_pad pad;

	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *vflip;
	struct v4l2_ctrl *hflip;

	const struct gc5035_mode *mode;
};

/*
 * Register settings of the Galaxy A70 depth camera module (GalaxyCore GC5035, 8-bit address and
 * data, page register 0xfe), 2576x1932, 2 lanes, from the vendor sensor module
 * (com.samsung.sensormodule.3_lsi_gc5035, regSetting block 0).
 */
static const struct cci_reg_sequence init_array_setting[] = {
	{ CCI_REG8(0xfc), 0x01 },
	{ CCI_REG8(0xf4), 0x40 },
	{ CCI_REG8(0xf5), 0xe9 },
};

static const struct cci_reg_sequence gc5035_2576x1932_mode[] = {
	{ CCI_REG8(0xf6), 0x14 },
	{ CCI_REG8(0xf8), 0x49 },
	{ CCI_REG8(0xf9), 0x82 },
	{ CCI_REG8(0xfa), 0x00 },
	{ CCI_REG8(0xfc), 0x81 },
	{ CCI_REG8(0xfe), 0x00 },
	{ CCI_REG8(0x36), 0x01 },
	{ CCI_REG8(0xd3), 0x87 },
	{ CCI_REG8(0x36), 0x00 },
	{ CCI_REG8(0x33), 0x00 },
	{ CCI_REG8(0xfe), 0x03 },
	{ CCI_REG8(0x01), 0xe7 },
	{ CCI_REG8(0xf7), 0x01 },
	{ CCI_REG8(0xfc), 0x8f },
	{ CCI_REG8(0xfc), 0x8f },
	{ CCI_REG8(0xfc), 0x8e },
	{ CCI_REG8(0xfe), 0x00 },
	{ CCI_REG8(0xee), 0x30 },
	{ CCI_REG8(0x87), 0x18 },
	{ CCI_REG8(0xfe), 0x01 },
	{ CCI_REG8(0x8c), 0x90 },
	{ CCI_REG8(0xfe), 0x00 },
	{ CCI_REG8(0x05), 0x02 },
	{ CCI_REG8(0x06), 0xd8 },
	{ CCI_REG8(0x9d), 0x14 },
	{ CCI_REG8(0x09), 0x00 },
	{ CCI_REG8(0x0a), 0x04 },
	{ CCI_REG8(0x0b), 0x00 },
	{ CCI_REG8(0x0c), 0x03 },
	{ CCI_REG8(0x0d), 0x07 },
	{ CCI_REG8(0x0e), 0xa8 },
	{ CCI_REG8(0x0f), 0x0a },
	{ CCI_REG8(0x10), 0x30 },
	{ CCI_REG8(0x91), 0x80 },
	{ CCI_REG8(0x92), 0x28 },
	{ CCI_REG8(0x93), 0x20 },
	{ CCI_REG8(0x95), 0xa0 },
	{ CCI_REG8(0x96), 0xe0 },
	{ CCI_REG8(0xd5), 0xfc },
	{ CCI_REG8(0x97), 0x28 },
	{ CCI_REG8(0x1f), 0x11 },
	{ CCI_REG8(0xd0), 0xb2 },
	{ CCI_REG8(0xfc), 0x88 },
	{ CCI_REG8(0xfe), 0x10 },
	{ CCI_REG8(0xfe), 0x00 },
	{ CCI_REG8(0xfc), 0x8e },
	{ CCI_REG8(0xfe), 0x00 },
	{ CCI_REG8(0xfe), 0x00 },
	{ CCI_REG8(0xfe), 0x00 },
	{ CCI_REG8(0xfc), 0x88 },
	{ CCI_REG8(0xfe), 0x10 },
	{ CCI_REG8(0xfe), 0x00 },
	{ CCI_REG8(0xfc), 0x8e },
	{ CCI_REG8(0xfe), 0x00 },
	{ CCI_REG8(0x41), 0x07 },
	{ CCI_REG8(0x42), 0xd4 },
	{ CCI_REG8(0x03), 0x07 },
	{ CCI_REG8(0x04), 0x08 },
	{ CCI_REG8(0xfe), 0x01 },
	{ CCI_REG8(0x42), 0x21 },
	{ CCI_REG8(0x49), 0x03 },
	{ CCI_REG8(0x4a), 0xff },
	{ CCI_REG8(0x4b), 0xc0 },
	{ CCI_REG8(0x55), 0x00 },
	{ CCI_REG8(0x41), 0x28 },
	{ CCI_REG8(0x4c), 0x00 },
	{ CCI_REG8(0x4d), 0x00 },
	{ CCI_REG8(0x4e), 0x3c },
	{ CCI_REG8(0x44), 0x08 },
	{ CCI_REG8(0x48), 0x02 },
	{ CCI_REG8(0x91), 0x00 },
	{ CCI_REG8(0x92), 0x0e },
	{ CCI_REG8(0x93), 0x00 },
	{ CCI_REG8(0x94), 0x10 },
	{ CCI_REG8(0x95), 0x07 },
	{ CCI_REG8(0x96), 0x8c },
	{ CCI_REG8(0x97), 0x0a },
	{ CCI_REG8(0x98), 0x10 },
	{ CCI_REG8(0xfe), 0x03 },
	{ CCI_REG8(0x02), 0x57 },
	{ CCI_REG8(0x03), 0xb7 },
	{ CCI_REG8(0x15), 0x14 },
	{ CCI_REG8(0x18), 0x0f },
	{ CCI_REG8(0x21), 0x22 },
	{ CCI_REG8(0x22), 0x06 },
	{ CCI_REG8(0x23), 0x48 },
	{ CCI_REG8(0x24), 0x12 },
	{ CCI_REG8(0x25), 0x28 },
	{ CCI_REG8(0x26), 0x08 },
	{ CCI_REG8(0x29), 0x06 },
	{ CCI_REG8(0x2a), 0x58 },
	{ CCI_REG8(0x2b), 0x08 },
	{ CCI_REG8(0xfe), 0x01 },
	{ CCI_REG8(0x8c), 0x10 },
	{ CCI_REG8(0xfe), 0x00 },
	{ CCI_REG8(0x3e), 0x91 },
};

static const struct gc5035_mode gc5035_supported_modes[] = {
	{
		.width = 2576,
		.height = 1932,
		.hts = 2912,
		.vts = 2004,
		.exposure = 256,
		.reg_list = {
			.regs = gc5035_2576x1932_mode,
			.num_regs = ARRAY_SIZE(gc5035_2576x1932_mode),
		},
	},
};

static int gc5035_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct gc5035 *gc5035 = container_of(ctrl->handler, struct gc5035,
					     ctrl_handler);
	const struct gc5035_mode *mode = gc5035->mode;
	s64 exposure_max;
	int ret;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_HFLIP:
	case V4L2_CID_VFLIP:
		/* The orientation settings are applied along with streaming */
		return 0;
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		exposure_max = mode->height + ctrl->val - GC5035_EXPOSURE_MARGIN;
		__v4l2_ctrl_modify_range(gc5035->exposure,
					 gc5035->exposure->minimum,
					 exposure_max,
					 gc5035->exposure->step,
					 gc5035->exposure->default_value);
		break;
	}

	/* V4L2 controls are applied, when sensor is powered up for streaming */
	if (!pm_runtime_get_if_active(gc5035->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		ret = cci_write(gc5035->regmap, CCI_REG8(0xfe), 0, NULL);
		cci_write(gc5035->regmap, GC5035_REG_AGAIN, ctrl->val, &ret);
		break;
	case V4L2_CID_EXPOSURE:
		ret = cci_write(gc5035->regmap, CCI_REG8(0xfe), 0, NULL);
		cci_write(gc5035->regmap, CCI_REG8(0x03), (ctrl->val >> 8) & 0x3f, &ret);
		cci_write(gc5035->regmap, CCI_REG8(0x04), ctrl->val & 0xff, &ret);
		break;
	case V4L2_CID_VBLANK:
		ret = cci_write(gc5035->regmap, CCI_REG8(0xfe), 0, NULL);
		cci_write(gc5035->regmap, CCI_REG8(0x41), ((ctrl->val + mode->height) >> 8) & 0x3f, &ret);
		cci_write(gc5035->regmap, CCI_REG8(0x42), (ctrl->val + mode->height) & 0xff, &ret);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = cci_write(gc5035->regmap, CCI_REG8(0xfe), 1, NULL);
		cci_write(gc5035->regmap, CCI_REG8(0x8c), ctrl->val ? 0x11 : 0x10, &ret);
		cci_write(gc5035->regmap, CCI_REG8(0xfe), 0, &ret);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	pm_runtime_put(gc5035->dev);

	return ret;
}

static const struct v4l2_ctrl_ops gc5035_ctrl_ops = {
	.s_ctrl = gc5035_set_ctrl,
};

static inline u64 gc5035_freq_to_pixel_rate(const u64 freq)
{
	return div_u64(freq * 2 * GC5035_DATA_LANES, 10);
}

static int gc5035_init_controls(struct gc5035 *gc5035)
{
	struct v4l2_ctrl_handler *ctrl_hdlr = &gc5035->ctrl_handler;
	const struct gc5035_mode *mode = gc5035->mode;
	s64 pixel_rate, hblank, vblank, exposure_max;
	struct v4l2_fwnode_device_properties props;
	int ret;

	v4l2_ctrl_handler_init(ctrl_hdlr, 9);

	gc5035->link_freq = v4l2_ctrl_new_int_menu(ctrl_hdlr, &gc5035_ctrl_ops,
					V4L2_CID_LINK_FREQ,
					ARRAY_SIZE(gc5035_link_freq_menu) - 1,
					0, gc5035_link_freq_menu);
	if (gc5035->link_freq)
		gc5035->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	pixel_rate = gc5035_freq_to_pixel_rate(gc5035_link_freq_menu[0]);
	gc5035->pixel_rate = v4l2_ctrl_new_std(ctrl_hdlr, &gc5035_ctrl_ops,
					       V4L2_CID_PIXEL_RATE,
					       0, pixel_rate, 1, pixel_rate);

	hblank = mode->hts - mode->width;
	gc5035->hblank = v4l2_ctrl_new_std(ctrl_hdlr, &gc5035_ctrl_ops,
					   V4L2_CID_HBLANK, hblank,
					   hblank, 1, hblank);
	if (gc5035->hblank)
		gc5035->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank = mode->vts - mode->height;
	gc5035->vblank = v4l2_ctrl_new_std(ctrl_hdlr, &gc5035_ctrl_ops,
					   V4L2_CID_VBLANK, vblank,
					   GC5035_VTS_MAX - mode->height, 1,
					   vblank);

	v4l2_ctrl_new_std(ctrl_hdlr, &gc5035_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  GC5035_AGAIN_MIN, GC5035_AGAIN_MAX,
			  GC5035_AGAIN_STEP, GC5035_AGAIN_DEFAULT);

	exposure_max = mode->vts - GC5035_EXPOSURE_MARGIN;
	gc5035->exposure = v4l2_ctrl_new_std(ctrl_hdlr, &gc5035_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     GC5035_EXPOSURE_MIN,
					     exposure_max,
					     GC5035_EXPOSURE_STEP,
					     mode->exposure);

	v4l2_ctrl_new_std_menu_items(ctrl_hdlr, &gc5035_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(gc5035_test_pattern_menu) - 1,
				     0, 0, gc5035_test_pattern_menu);

	gc5035->hflip = v4l2_ctrl_new_std(ctrl_hdlr, &gc5035_ctrl_ops,
					  V4L2_CID_HFLIP, 0, 1, 1, 0);
	if (gc5035->hflip)
		gc5035->hflip->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;

	gc5035->vflip = v4l2_ctrl_new_std(ctrl_hdlr, &gc5035_ctrl_ops,
					  V4L2_CID_VFLIP, 0, 1, 1, 0);
	if (gc5035->vflip)
		gc5035->vflip->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;

	ret = v4l2_fwnode_device_parse(gc5035->dev, &props);
	if (ret)
		goto error_free_hdlr;

	ret = v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &gc5035_ctrl_ops,
					      &props);
	if (ret)
		goto error_free_hdlr;

	gc5035->sd.ctrl_handler = ctrl_hdlr;

	return 0;

error_free_hdlr:
	v4l2_ctrl_handler_free(ctrl_hdlr);

	return ret;
}

static int gc5035_enable_streams(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state, u32 pad,
				 u64 streams_mask)
{
	struct gc5035 *gc5035 = to_gc5035(sd);
	const struct gc5035_reg_list *reg_list = &gc5035->mode->reg_list;
	int ret;

	ret = pm_runtime_resume_and_get(gc5035->dev);
	if (ret)
		return ret;

	/*
	 * Route a shared CSI PHY to this sensor when it starts streaming (Galaxy A70: front and
	 * ultra wide cameras share CSIPHY1 through a switch); both sensors may be powered.
	 */
	if (gc5035->mipi_sel_gpio)
		gpiod_direction_output_raw(gc5035->mipi_sel_gpio, gc5035->mipi_sel_value);

	/* clock/analog setup, then wait as the vendor sequence does */
	cci_multi_reg_write(gc5035->regmap, init_array_setting,
			    ARRAY_SIZE(init_array_setting), &ret);
	if (ret)
		goto error;

	usleep_range(4 * USEC_PER_MSEC, 5 * USEC_PER_MSEC);

	cci_multi_reg_write(gc5035->regmap, reg_list->regs,
			    reg_list->num_regs, &ret);
	if (ret)
		goto error;

	ret = __v4l2_ctrl_handler_setup(gc5035->sd.ctrl_handler);

	cci_write(gc5035->regmap, CCI_REG8(0xfe), 0, &ret);
	cci_write(gc5035->regmap, CCI_REG8(0x3e), 0x91, &ret);
	if (ret)
		goto error;

	return 0;

error:
	dev_err(gc5035->dev, "failed to start streaming: %d\n", ret);
	pm_runtime_put_autosuspend(gc5035->dev);

	return ret;
}

static int gc5035_disable_streams(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state, u32 pad,
				  u64 streams_mask)
{
	struct gc5035 *gc5035 = to_gc5035(sd);
	int ret;

	ret = cci_write(gc5035->regmap, CCI_REG8(0xfe), 0, NULL);
	cci_write(gc5035->regmap, CCI_REG8(0x3e), 0x00, &ret);
	if (ret)
		dev_err(gc5035->dev, "failed to stop streaming: %d\n", ret);

	pm_runtime_put_autosuspend(gc5035->dev);

	return ret;
}

static u32 gc5035_get_format_code(struct gc5035 *gc5035)
{
	unsigned int i;

	i = (gc5035->vflip->val ? 2 : 0) | (gc5035->hflip->val ? 1 : 0);

	return gc5035_mbus_formats[i];
}

static void gc5035_update_pad_format(struct gc5035 *gc5035,
				     const struct gc5035_mode *mode,
				     struct v4l2_mbus_framefmt *fmt)
{
	fmt->code = gc5035_get_format_code(gc5035);
	fmt->width = mode->width;
	fmt->height = mode->height;
	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = V4L2_COLORSPACE_SRGB;
	fmt->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	fmt->quantization = V4L2_QUANTIZATION_FULL_RANGE;
	fmt->xfer_func = V4L2_XFER_FUNC_NONE;
}

static int gc5035_set_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state,
				 struct v4l2_subdev_format *fmt)
{
	struct gc5035 *gc5035 = to_gc5035(sd);
	s64 hblank, vblank, exposure_max;
	const struct gc5035_mode *mode;

	mode = v4l2_find_nearest_size(gc5035_supported_modes,
				      ARRAY_SIZE(gc5035_supported_modes),
				      width, height,
				      fmt->format.width, fmt->format.height);

	gc5035_update_pad_format(gc5035, mode, &fmt->format);

	/* Format code could be updated with respect to flip controls */
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY || gc5035->mode == mode)
		goto set_format;

	/* Update limits and set FPS and exposure to default values */
	hblank = mode->hts - mode->width;
	__v4l2_ctrl_modify_range(gc5035->hblank, hblank, hblank, 1, hblank);

	vblank = mode->vts - mode->height;
	__v4l2_ctrl_modify_range(gc5035->vblank, vblank,
				 GC5035_VTS_MAX - mode->height, 1, vblank);
	__v4l2_ctrl_s_ctrl(gc5035->vblank, vblank);

	exposure_max = mode->vts - GC5035_EXPOSURE_MARGIN;
	__v4l2_ctrl_modify_range(gc5035->exposure, GC5035_EXPOSURE_MIN,
				 exposure_max, GC5035_EXPOSURE_STEP,
				 mode->exposure);
	__v4l2_ctrl_s_ctrl(gc5035->exposure, mode->exposure);

	if (gc5035->sd.ctrl_handler->error)
		return gc5035->sd.ctrl_handler->error;

	gc5035->mode = mode;

set_format:
	*v4l2_subdev_state_get_format(state, 0) = fmt->format;

	return 0;
}

static int gc5035_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct gc5035 *gc5035 = to_gc5035(sd);

	/* Media bus code index is constant, but code formats are not */
	if (code->index > 0)
		return -EINVAL;

	code->code = gc5035_get_format_code(gc5035);

	return 0;
}

static int gc5035_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	struct gc5035 *gc5035 = to_gc5035(sd);

	if (fse->index >= ARRAY_SIZE(gc5035_supported_modes))
		return -EINVAL;

	if (fse->code != gc5035_get_format_code(gc5035))
		return -EINVAL;

	fse->min_width = gc5035_supported_modes[fse->index].width;
	fse->max_width = fse->min_width;
	fse->min_height = gc5035_supported_modes[fse->index].height;
	fse->max_height = fse->min_height;

	return 0;
}

static int gc5035_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_selection *sel)
{
	struct gc5035 *gc5035 = to_gc5035(sd);

	if (sel->which != V4L2_SUBDEV_FORMAT_ACTIVE)
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = gc5035->mode->width;
		sel->r.height = gc5035->mode->width;	/* as upstream s5k3m5; the correct height breaks the libcamera pipeline here */
		return 0;
	default:
		return -EINVAL;
	}

	return 0;
}

static int gc5035_init_state(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *state)
{
	struct gc5035 *gc5035 = to_gc5035(sd);
	struct v4l2_subdev_format fmt = {
		.which = V4L2_SUBDEV_FORMAT_TRY,
		.pad = 0,
		.format = {
			/* Media bus code depends on current flip controls */
			.width = gc5035->mode->width,
			.height = gc5035->mode->height,
		},
	};

	gc5035_set_pad_format(sd, state, &fmt);

	return 0;
}

static const struct v4l2_subdev_video_ops gc5035_video_ops = {
	.s_stream = v4l2_subdev_s_stream_helper,
};

static const struct v4l2_subdev_pad_ops gc5035_pad_ops = {
	.set_fmt = gc5035_set_pad_format,
	.get_fmt = v4l2_subdev_get_fmt,
	.get_selection = gc5035_get_selection,
	.enum_mbus_code = gc5035_enum_mbus_code,
	.enum_frame_size = gc5035_enum_frame_size,
	.enable_streams = gc5035_enable_streams,
	.disable_streams = gc5035_disable_streams,
};

static const struct v4l2_subdev_ops gc5035_subdev_ops = {
	.video = &gc5035_video_ops,
	.pad = &gc5035_pad_ops,
};

static const struct v4l2_subdev_internal_ops gc5035_internal_ops = {
	.init_state = gc5035_init_state,
};

static const struct media_entity_operations gc5035_subdev_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static int gc5035_identify_sensor(struct gc5035 *gc5035)
{
	u64 val;
	int ret;

	u64 lo;

	ret = cci_write(gc5035->regmap, CCI_REG8(0xfe), 0, NULL);
	cci_read(gc5035->regmap, CCI_REG8(0xf0), &val, &ret);
	cci_read(gc5035->regmap, CCI_REG8(0xf1), &lo, &ret);
	val = (val << 8) | lo;
	if (ret) {
		dev_err(gc5035->dev, "failed to read chip id: %d\n", ret);
		return ret;
	}

	if (val != GC5035_CHIP_ID) {
		dev_err(gc5035->dev, "chip id mismatch: %x!=%llx\n",
			GC5035_CHIP_ID, val);
		return -ENODEV;
	}

	return 0;
}

static int gc5035_check_hwcfg(struct gc5035 *gc5035)
{
	struct fwnode_handle *fwnode = dev_fwnode(gc5035->dev), *ep;
	struct v4l2_fwnode_endpoint bus_cfg = {
		.bus = {
			.mipi_csi2 = {
				.num_data_lanes = GC5035_DATA_LANES,
			},
		},
		.bus_type = V4L2_MBUS_CSI2_DPHY,
	};
	unsigned long freq_bitmap;
	int ret;

	if (!fwnode)
		return -ENODEV;

	ep = fwnode_graph_get_next_endpoint(fwnode, NULL);
	if (!ep)
		return -EINVAL;

	ret = v4l2_fwnode_endpoint_alloc_parse(ep, &bus_cfg);
	fwnode_handle_put(ep);
	if (ret)
		return ret;

	if (bus_cfg.bus.mipi_csi2.num_data_lanes != GC5035_DATA_LANES) {
		dev_err(gc5035->dev, "Invalid number of data lanes: %u\n",
			bus_cfg.bus.mipi_csi2.num_data_lanes);
		ret = -EINVAL;
		goto endpoint_free;
	}

	ret = v4l2_link_freq_to_bitmap(gc5035->dev, bus_cfg.link_frequencies,
				       bus_cfg.nr_of_link_frequencies,
				       gc5035_link_freq_menu,
				       ARRAY_SIZE(gc5035_link_freq_menu),
				       &freq_bitmap);

endpoint_free:
	v4l2_fwnode_endpoint_free(&bus_cfg);

	return ret;
}

static int gc5035_power_on(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct gc5035 *gc5035 = to_gc5035(sd);
	int ret;

	ret = regulator_bulk_enable(GC5035_NUM_SUPPLIES, gc5035->supplies);
	if (ret)
		return ret;

	ret = clk_prepare_enable(gc5035->mclk);
	if (ret)
		goto disable_regulators;

	gpiod_set_value_cansleep(gc5035->reset_gpio, 0);
	usleep_range(10 * USEC_PER_MSEC, 15 * USEC_PER_MSEC);

	return 0;

disable_regulators:
	regulator_bulk_disable(GC5035_NUM_SUPPLIES, gc5035->supplies);

	return ret;
}

static int gc5035_power_off(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct gc5035 *gc5035 = to_gc5035(sd);

	gpiod_set_value_cansleep(gc5035->reset_gpio, 1);

	clk_disable_unprepare(gc5035->mclk);

	regulator_bulk_disable(GC5035_NUM_SUPPLIES, gc5035->supplies);

	return 0;
}

static int gc5035_probe(struct i2c_client *client)
{
	struct gc5035 *gc5035;
	unsigned long freq;
	unsigned int i;
	int ret;

	gc5035 = devm_kzalloc(&client->dev, sizeof(*gc5035), GFP_KERNEL);
	if (!gc5035)
		return -ENOMEM;

	gc5035->dev = &client->dev;
	v4l2_i2c_subdev_init(&gc5035->sd, client, &gc5035_subdev_ops);

	gc5035->regmap = devm_cci_regmap_init_i2c(client, 8);
	if (IS_ERR(gc5035->regmap))
		return dev_err_probe(gc5035->dev, PTR_ERR(gc5035->regmap),
				     "failed to init CCI\n");

	gc5035->mclk = devm_v4l2_sensor_clk_get(gc5035->dev, NULL);
	if (IS_ERR(gc5035->mclk))
		return dev_err_probe(gc5035->dev, PTR_ERR(gc5035->mclk),
				     "failed to get MCLK clock\n");

	freq = clk_get_rate(gc5035->mclk);
	if (freq != GC5035_MCLK_FREQ_24MHZ)
		return dev_err_probe(gc5035->dev, -EINVAL,
				     "MCLK clock frequency %lu is not supported\n",
				     freq);

	ret = gc5035_check_hwcfg(gc5035);
	if (ret)
		return dev_err_probe(gc5035->dev, ret,
				     "failed to check HW configuration\n");

	gc5035->reset_gpio = devm_gpiod_get_optional(gc5035->dev, "reset",
						     GPIOD_OUT_HIGH);
	if (IS_ERR(gc5035->reset_gpio))
		return dev_err_probe(gc5035->dev, PTR_ERR(gc5035->reset_gpio),
				     "cannot get reset GPIO\n");

	gc5035->mipi_sel_gpio = devm_gpiod_get_optional(gc5035->dev, "mipi-sel",
						GPIOD_ASIS | GPIOD_FLAGS_BIT_NONEXCLUSIVE);
	if (IS_ERR(gc5035->mipi_sel_gpio))
		return dev_err_probe(gc5035->dev, PTR_ERR(gc5035->mipi_sel_gpio),
				     "cannot get MIPI switch GPIO\n");
	if (gc5035->mipi_sel_gpio)
		device_property_read_u32(gc5035->dev, "mipi-sel-value", &gc5035->mipi_sel_value);

	for (i = 0; i < GC5035_NUM_SUPPLIES; i++)
		gc5035->supplies[i].supply = gc5035_supply_names[i];

	ret = devm_regulator_bulk_get(gc5035->dev, GC5035_NUM_SUPPLIES,
				      gc5035->supplies);
	if (ret)
		return dev_err_probe(gc5035->dev, ret,
				     "failed to get supply regulators\n");

	/* The sensor must be powered on to read the CHIP_ID register */
	ret = gc5035_power_on(gc5035->dev);
	if (ret)
		return ret;

	ret = gc5035_identify_sensor(gc5035);
	if (ret) {
		dev_err_probe(gc5035->dev, ret, "failed to find sensor\n");
		goto power_off;
	}

	gc5035->mode = &gc5035_supported_modes[0];
	ret = gc5035_init_controls(gc5035);
	if (ret) {
		dev_err_probe(gc5035->dev, ret, "failed to init controls\n");
		goto power_off;
	}

	gc5035->sd.state_lock = gc5035->ctrl_handler.lock;
	gc5035->sd.internal_ops = &gc5035_internal_ops;
	gc5035->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	gc5035->sd.entity.ops = &gc5035_subdev_entity_ops;
	gc5035->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	gc5035->pad.flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&gc5035->sd.entity, 1, &gc5035->pad);
	if (ret) {
		dev_err_probe(gc5035->dev, ret,
			      "failed to init media entity pads\n");
		goto v4l2_ctrl_handler_free;
	}

	ret = v4l2_subdev_init_finalize(&gc5035->sd);
	if (ret < 0) {
		dev_err_probe(gc5035->dev, ret,
			      "failed to init media entity pads\n");
		goto media_entity_cleanup;
	}

	pm_runtime_set_active(gc5035->dev);
	pm_runtime_enable(gc5035->dev);

	ret = v4l2_async_register_subdev_sensor(&gc5035->sd);
	if (ret < 0) {
		dev_err_probe(gc5035->dev, ret,
			      "failed to register V4L2 subdev\n");
		goto subdev_cleanup;
	}

	pm_runtime_set_autosuspend_delay(gc5035->dev, 1000);
	pm_runtime_use_autosuspend(gc5035->dev);
	pm_runtime_idle(gc5035->dev);

	return 0;

subdev_cleanup:
	v4l2_subdev_cleanup(&gc5035->sd);
	pm_runtime_disable(gc5035->dev);
	pm_runtime_set_suspended(gc5035->dev);

media_entity_cleanup:
	media_entity_cleanup(&gc5035->sd.entity);

v4l2_ctrl_handler_free:
	v4l2_ctrl_handler_free(gc5035->sd.ctrl_handler);

power_off:
	gc5035_power_off(gc5035->dev);

	return ret;
}

static void gc5035_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc5035 *gc5035 = to_gc5035(sd);

	v4l2_async_unregister_subdev(sd);
	v4l2_subdev_cleanup(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(sd->ctrl_handler);
	pm_runtime_disable(gc5035->dev);

	if (!pm_runtime_status_suspended(gc5035->dev)) {
		gc5035_power_off(gc5035->dev);
		pm_runtime_set_suspended(gc5035->dev);
	}
}

static const struct dev_pm_ops gc5035_pm_ops = {
	SET_RUNTIME_PM_OPS(gc5035_power_off, gc5035_power_on, NULL)
};

static const struct of_device_id gc5035_of_match[] = {
	{ .compatible = "galaxycore,gc5035" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, gc5035_of_match);

static struct i2c_driver gc5035_i2c_driver = {
	.driver = {
		.name = "gc5035",
		.pm = &gc5035_pm_ops,
		.of_match_table = gc5035_of_match,
	},
	.probe = gc5035_probe,
	.remove = gc5035_remove,
};

module_i2c_driver(gc5035_i2c_driver);

MODULE_AUTHOR("Vladimir Zapolskiy <vladimir.zapolskiy@linaro.org>");
MODULE_DESCRIPTION("Samsung GC5035 image sensor driver");
MODULE_LICENSE("GPL");
