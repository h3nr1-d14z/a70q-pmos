// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2025 Linaro Ltd
//
// Samsung S5K4HA driver (Galaxy A70 ultra wide camera), adapted from the S5K3M5 driver
// (SMIA-style registers); settings from the device's camera module data.

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

#define S5K4HA_LINK_FREQ	(334000ULL * HZ_PER_KHZ)	/* OP PLL 24/6*167 = 668 Mbps/lane */
#define S5K4HA_MCLK_FREQ_24MHZ		(24 * HZ_PER_MHZ)
#define S5K4HA_DATA_LANES		4

/* Register map is similar to MIPI CCS compliant camera sensors */
#define S5K4HA_REG_CHIP_ID		CCI_REG16(0x0000)
#define S5K4HA_CHIP_ID			0x48ab

/* Both streaming and flipping settings are controlled at the same time */
#define S5K4HA_REG_CTRL_MODE		CCI_REG16(0x0100)
#define S5K4HA_MODE_STREAMING		BIT(8)
#define S5K4HA_VFLIP			BIT(1)
#define S5K4HA_HFLIP			BIT(0)

#define S5K4HA_REG_EXPOSURE		CCI_REG16(0x0202)
#define S5K4HA_EXPOSURE_MIN		8
#define S5K4HA_EXPOSURE_STEP		1
#define S5K4HA_EXPOSURE_MARGIN		4

#define S5K4HA_REG_AGAIN		CCI_REG16(0x0204)
/* register code, gain = code / 32 (1x .. 16x) */
#define S5K4HA_AGAIN_MIN		32
#define S5K4HA_AGAIN_MAX		512
#define S5K4HA_AGAIN_STEP		1
#define S5K4HA_AGAIN_DEFAULT		32

#define S5K4HA_REG_VTS			CCI_REG16(0x0340)
#define S5K4HA_VTS_MAX			0xffff

#define S5K4HA_REG_HTS			CCI_REG16(0x0342)
#define S5K4HA_REG_X_ADDR_START		CCI_REG16(0x0344)
#define S5K4HA_REG_Y_ADDR_START		CCI_REG16(0x0346)
#define S5K4HA_REG_X_ADDR_END		CCI_REG16(0x0348)
#define S5K4HA_REG_Y_ADDR_END		CCI_REG16(0x034a)
#define S5K4HA_REG_X_OUTPUT_SIZE	CCI_REG16(0x034c)
#define S5K4HA_REG_Y_OUTPUT_SIZE	CCI_REG16(0x034e)

#define S5K4HA_REG_TEST_PATTERN		CCI_REG16(0x0600)

#define to_s5k4ha(_sd)			container_of(_sd, struct s5k4ha, sd)

static const s64 s5k4ha_link_freq_menu[] = {
	S5K4HA_LINK_FREQ,
};

/* List of supported formats to cover horizontal and vertical flip controls */
static const u32 s5k4ha_mbus_formats[] = {
	MEDIA_BUS_FMT_SGRBG10_1X10,	MEDIA_BUS_FMT_SRGGB10_1X10,
	MEDIA_BUS_FMT_SBGGR10_1X10,	MEDIA_BUS_FMT_SGBRG10_1X10,
};

struct s5k4ha_reg_list {
	const struct cci_reg_sequence *regs;
	unsigned int num_regs;
};

struct s5k4ha_mode {
	u32 width;			/* Frame width in pixels */
	u32 height;			/* Frame height in pixels */
	u32 hts;			/* Horizontal timing size */
	u32 vts;			/* Default vertical timing size */
	u32 exposure;			/* Default exposure value */

	const struct s5k4ha_reg_list reg_list;	/* Sensor register setting */
};

static const char * const s5k4ha_test_pattern_menu[] = {
	"Disabled",
	"Solid colour",
	"Colour bars",
	"Fade to grey colour bars",
	"PN9",
};

static const char * const s5k4ha_supply_names[] = {
	"afvdd",	/* Autofocus power */
	"vdda",		/* Analog power */
	"vddd",		/* Digital core power */
	"vddio",	/* Digital I/O power */
};

#define S5K4HA_NUM_SUPPLIES	ARRAY_SIZE(s5k4ha_supply_names)

struct s5k4ha {
	struct device *dev;
	struct regmap *regmap;
	struct clk *mclk;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *mipi_sel_gpio;	/* shared MIPI lane switch (optional) */
	u32 mipi_sel_value;
	struct regulator_bulk_data supplies[S5K4HA_NUM_SUPPLIES];

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

	const struct s5k4ha_mode *mode;
};

/*
 * Register settings of the Galaxy A70 ultra wide camera module (S5K4HA, 8-bit registers),
 * 3264x2448, 4 lanes, from the vendor sensor module (com.samsung.sensormodule.2_lsi_s5k4ha).
 */
static const struct cci_reg_sequence init_array_setting[] = {
	{ CCI_REG8(0x0a02), 0x7f },
	{ CCI_REG8(0x3b45), 0x01 },
	{ CCI_REG8(0x3264), 0x01 },
	{ CCI_REG8(0x3290), 0x10 },
	{ CCI_REG8(0x0b05), 0x01 },
	{ CCI_REG8(0x3069), 0xc7 },
	{ CCI_REG8(0x3074), 0x06 },
	{ CCI_REG8(0x3075), 0x32 },
	{ CCI_REG8(0x3068), 0xf7 },
	{ CCI_REG8(0x30c6), 0x01 },
	{ CCI_REG8(0x301f), 0x20 },
	{ CCI_REG8(0x306b), 0x9a },
	{ CCI_REG8(0x3091), 0x1f },
	{ CCI_REG8(0x306e), 0x71 },
	{ CCI_REG8(0x306f), 0x28 },
	{ CCI_REG8(0x306d), 0x08 },
	{ CCI_REG8(0x3084), 0x16 },
	{ CCI_REG8(0x3070), 0x0f },
	{ CCI_REG8(0x306a), 0x79 },
	{ CCI_REG8(0x30b0), 0xff },
	{ CCI_REG8(0x30c2), 0x05 },
	{ CCI_REG8(0x30c4), 0x06 },
	{ CCI_REG8(0x3081), 0x07 },
	{ CCI_REG8(0x307b), 0x85 },
	{ CCI_REG8(0x307a), 0x0a },
	{ CCI_REG8(0x3079), 0x0a },
	{ CCI_REG8(0x308a), 0x20 },
	{ CCI_REG8(0x308b), 0x08 },
	{ CCI_REG8(0x308c), 0x0b },
	{ CCI_REG8(0x392f), 0x01 },
	{ CCI_REG8(0x3930), 0x00 },
	{ CCI_REG8(0x3924), 0x7f },
	{ CCI_REG8(0x3925), 0xfd },
	{ CCI_REG8(0x3c08), 0xff },
	{ CCI_REG8(0x3c09), 0xff },
	{ CCI_REG8(0x3c31), 0xff },
	{ CCI_REG8(0x3c32), 0xff },
};

static const struct cci_reg_sequence s5k4ha_3264x2448_mode[] = {
	{ CCI_REG8(0x0136), 0x18 },
	{ CCI_REG8(0x0137), 0x00 },
	{ CCI_REG8(0x0301), 0x04 },
	{ CCI_REG8(0x0305), 0x06 },
	{ CCI_REG8(0x0306), 0x00 },
	{ CCI_REG8(0x0307), 0x8c },
	{ CCI_REG8(0x3c1f), 0x00 },
	{ CCI_REG8(0x030d), 0x06 },
	{ CCI_REG8(0x030e), 0x00 },
	{ CCI_REG8(0x030f), 0xa7 },
	{ CCI_REG8(0x3c17), 0x00 },
	{ CCI_REG8(0x0340), 0x09 },
	{ CCI_REG8(0x0341), 0xe2 },
	{ CCI_REG8(0x0342), 0x0e },
	{ CCI_REG8(0x0343), 0x68 },
	{ CCI_REG8(0x0344), 0x00 },
	{ CCI_REG8(0x0345), 0x08 },
	{ CCI_REG8(0x0346), 0x00 },
	{ CCI_REG8(0x0347), 0x08 },
	{ CCI_REG8(0x0348), 0x0c },
	{ CCI_REG8(0x0349), 0xc7 },
	{ CCI_REG8(0x034a), 0x09 },
	{ CCI_REG8(0x034b), 0x97 },
	{ CCI_REG8(0x034c), 0x0c },
	{ CCI_REG8(0x034d), 0xc0 },
	{ CCI_REG8(0x034e), 0x09 },
	{ CCI_REG8(0x034f), 0x90 },
	{ CCI_REG8(0x0381), 0x01 },
	{ CCI_REG8(0x0383), 0x01 },
	{ CCI_REG8(0x0385), 0x01 },
	{ CCI_REG8(0x0387), 0x01 },
	{ CCI_REG8(0x0900), 0x00 },
	{ CCI_REG8(0x0901), 0x00 },
	{ CCI_REG8(0x0204), 0x00 },
	{ CCI_REG8(0x0205), 0x20 },
	{ CCI_REG8(0x0200), 0x0d },
	{ CCI_REG8(0x0201), 0xd8 },
	{ CCI_REG8(0x0202), 0x00 },
	{ CCI_REG8(0x0203), 0x02 },
	{ CCI_REG8(0x0820), 0x02 },
	{ CCI_REG8(0x0821), 0x9c },
	{ CCI_REG8(0x0822), 0x00 },
	{ CCI_REG8(0x0823), 0x00 },
	{ CCI_REG8(0x0112), 0x0a },
	{ CCI_REG8(0x0113), 0x0a },
	{ CCI_REG8(0x0114), 0x03 },
	{ CCI_REG8(0x3929), 0x3f },
	{ CCI_REG8(0x0101), 0x00 },
	{ CCI_REG8(0x3941), 0x0e },
	{ CCI_REG8(0x3942), 0x11 },
};

static const struct s5k4ha_mode s5k4ha_supported_modes[] = {
	{
		.width = 3264,
		.height = 2448,
		.hts = 0x0e68,
		.vts = 0x09e2,
		.exposure = 256,
		.reg_list = {
			.regs = s5k4ha_3264x2448_mode,
			.num_regs = ARRAY_SIZE(s5k4ha_3264x2448_mode),
		},
	},
};

static int s5k4ha_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct s5k4ha *s5k4ha = container_of(ctrl->handler, struct s5k4ha,
					     ctrl_handler);
	const struct s5k4ha_mode *mode = s5k4ha->mode;
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
		exposure_max = mode->height + ctrl->val - S5K4HA_EXPOSURE_MARGIN;
		__v4l2_ctrl_modify_range(s5k4ha->exposure,
					 s5k4ha->exposure->minimum,
					 exposure_max,
					 s5k4ha->exposure->step,
					 s5k4ha->exposure->default_value);
		break;
	}

	/* V4L2 controls are applied, when sensor is powered up for streaming */
	if (!pm_runtime_get_if_active(s5k4ha->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		ret = cci_write(s5k4ha->regmap, S5K4HA_REG_AGAIN,
				ctrl->val, NULL);
		break;
	case V4L2_CID_EXPOSURE:
		ret = cci_write(s5k4ha->regmap, S5K4HA_REG_EXPOSURE,
				ctrl->val, NULL);
		break;
	case V4L2_CID_VBLANK:
		ret = cci_write(s5k4ha->regmap, S5K4HA_REG_VTS,
				ctrl->val + mode->height, NULL);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = cci_write(s5k4ha->regmap, S5K4HA_REG_TEST_PATTERN,
				ctrl->val, NULL);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	pm_runtime_put(s5k4ha->dev);

	return ret;
}

static const struct v4l2_ctrl_ops s5k4ha_ctrl_ops = {
	.s_ctrl = s5k4ha_set_ctrl,
};

static inline u64 s5k4ha_freq_to_pixel_rate(const u64 freq)
{
	return div_u64(freq * 2 * S5K4HA_DATA_LANES, 10);
}

static int s5k4ha_init_controls(struct s5k4ha *s5k4ha)
{
	struct v4l2_ctrl_handler *ctrl_hdlr = &s5k4ha->ctrl_handler;
	const struct s5k4ha_mode *mode = s5k4ha->mode;
	s64 pixel_rate, hblank, vblank, exposure_max;
	struct v4l2_fwnode_device_properties props;
	int ret;

	v4l2_ctrl_handler_init(ctrl_hdlr, 9);

	s5k4ha->link_freq = v4l2_ctrl_new_int_menu(ctrl_hdlr, &s5k4ha_ctrl_ops,
					V4L2_CID_LINK_FREQ,
					ARRAY_SIZE(s5k4ha_link_freq_menu) - 1,
					0, s5k4ha_link_freq_menu);
	if (s5k4ha->link_freq)
		s5k4ha->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	pixel_rate = s5k4ha_freq_to_pixel_rate(s5k4ha_link_freq_menu[0]);
	s5k4ha->pixel_rate = v4l2_ctrl_new_std(ctrl_hdlr, &s5k4ha_ctrl_ops,
					       V4L2_CID_PIXEL_RATE,
					       0, pixel_rate, 1, pixel_rate);

	hblank = mode->hts - mode->width;
	s5k4ha->hblank = v4l2_ctrl_new_std(ctrl_hdlr, &s5k4ha_ctrl_ops,
					   V4L2_CID_HBLANK, hblank,
					   hblank, 1, hblank);
	if (s5k4ha->hblank)
		s5k4ha->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank = mode->vts - mode->height;
	s5k4ha->vblank = v4l2_ctrl_new_std(ctrl_hdlr, &s5k4ha_ctrl_ops,
					   V4L2_CID_VBLANK, vblank,
					   S5K4HA_VTS_MAX - mode->height, 1,
					   vblank);

	v4l2_ctrl_new_std(ctrl_hdlr, &s5k4ha_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  S5K4HA_AGAIN_MIN, S5K4HA_AGAIN_MAX,
			  S5K4HA_AGAIN_STEP, S5K4HA_AGAIN_DEFAULT);

	exposure_max = mode->vts - S5K4HA_EXPOSURE_MARGIN;
	s5k4ha->exposure = v4l2_ctrl_new_std(ctrl_hdlr, &s5k4ha_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     S5K4HA_EXPOSURE_MIN,
					     exposure_max,
					     S5K4HA_EXPOSURE_STEP,
					     mode->exposure);

	v4l2_ctrl_new_std_menu_items(ctrl_hdlr, &s5k4ha_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(s5k4ha_test_pattern_menu) - 1,
				     0, 0, s5k4ha_test_pattern_menu);

	s5k4ha->hflip = v4l2_ctrl_new_std(ctrl_hdlr, &s5k4ha_ctrl_ops,
					  V4L2_CID_HFLIP, 0, 1, 1, 0);
	if (s5k4ha->hflip)
		s5k4ha->hflip->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;

	s5k4ha->vflip = v4l2_ctrl_new_std(ctrl_hdlr, &s5k4ha_ctrl_ops,
					  V4L2_CID_VFLIP, 0, 1, 1, 0);
	if (s5k4ha->vflip)
		s5k4ha->vflip->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;

	ret = v4l2_fwnode_device_parse(s5k4ha->dev, &props);
	if (ret)
		goto error_free_hdlr;

	ret = v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &s5k4ha_ctrl_ops,
					      &props);
	if (ret)
		goto error_free_hdlr;

	s5k4ha->sd.ctrl_handler = ctrl_hdlr;

	return 0;

error_free_hdlr:
	v4l2_ctrl_handler_free(ctrl_hdlr);

	return ret;
}

static int s5k4ha_enable_streams(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state, u32 pad,
				 u64 streams_mask)
{
	struct s5k4ha *s5k4ha = to_s5k4ha(sd);
	const struct s5k4ha_reg_list *reg_list = &s5k4ha->mode->reg_list;
	int ret;

	ret = pm_runtime_resume_and_get(s5k4ha->dev);
	if (ret)
		return ret;

	/*
	 * Route a shared CSI PHY to this sensor when it starts streaming (Galaxy A70: front and
	 * ultra wide cameras share CSIPHY1 through a switch); both sensors may be powered.
	 */
	if (s5k4ha->mipi_sel_gpio)
		gpiod_direction_output_raw(s5k4ha->mipi_sel_gpio, s5k4ha->mipi_sel_value);

	/* clock/analog setup, then wait as the vendor sequence does */
	cci_multi_reg_write(s5k4ha->regmap, init_array_setting,
			    ARRAY_SIZE(init_array_setting), &ret);
	if (ret)
		goto error;

	usleep_range(4 * USEC_PER_MSEC, 5 * USEC_PER_MSEC);

	cci_multi_reg_write(s5k4ha->regmap, reg_list->regs,
			    reg_list->num_regs, &ret);
	if (ret)
		goto error;

	ret = __v4l2_ctrl_handler_setup(s5k4ha->sd.ctrl_handler);

	cci_write(s5k4ha->regmap, S5K4HA_REG_CTRL_MODE,
		  S5K4HA_MODE_STREAMING |
		  (s5k4ha->vflip->val ? S5K4HA_VFLIP : 0) |
		  (s5k4ha->hflip->val ? S5K4HA_HFLIP : 0), &ret);
	if (ret)
		goto error;

	return 0;

error:
	dev_err(s5k4ha->dev, "failed to start streaming: %d\n", ret);
	pm_runtime_put_autosuspend(s5k4ha->dev);

	return ret;
}

static int s5k4ha_disable_streams(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state, u32 pad,
				  u64 streams_mask)
{
	struct s5k4ha *s5k4ha = to_s5k4ha(sd);
	int ret;

	ret = cci_write(s5k4ha->regmap, S5K4HA_REG_CTRL_MODE, 0, NULL);
	if (ret)
		dev_err(s5k4ha->dev, "failed to stop streaming: %d\n", ret);

	pm_runtime_put_autosuspend(s5k4ha->dev);

	return ret;
}

static u32 s5k4ha_get_format_code(struct s5k4ha *s5k4ha)
{
	unsigned int i;

	i = (s5k4ha->vflip->val ? 2 : 0) | (s5k4ha->hflip->val ? 1 : 0);

	return s5k4ha_mbus_formats[i];
}

static void s5k4ha_update_pad_format(struct s5k4ha *s5k4ha,
				     const struct s5k4ha_mode *mode,
				     struct v4l2_mbus_framefmt *fmt)
{
	fmt->code = s5k4ha_get_format_code(s5k4ha);
	fmt->width = mode->width;
	fmt->height = mode->height;
	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = V4L2_COLORSPACE_SRGB;
	fmt->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	fmt->quantization = V4L2_QUANTIZATION_FULL_RANGE;
	fmt->xfer_func = V4L2_XFER_FUNC_NONE;
}

static int s5k4ha_set_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state,
				 struct v4l2_subdev_format *fmt)
{
	struct s5k4ha *s5k4ha = to_s5k4ha(sd);
	s64 hblank, vblank, exposure_max;
	const struct s5k4ha_mode *mode;

	mode = v4l2_find_nearest_size(s5k4ha_supported_modes,
				      ARRAY_SIZE(s5k4ha_supported_modes),
				      width, height,
				      fmt->format.width, fmt->format.height);

	s5k4ha_update_pad_format(s5k4ha, mode, &fmt->format);

	/* Format code could be updated with respect to flip controls */
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY || s5k4ha->mode == mode)
		goto set_format;

	/* Update limits and set FPS and exposure to default values */
	hblank = mode->hts - mode->width;
	__v4l2_ctrl_modify_range(s5k4ha->hblank, hblank, hblank, 1, hblank);

	vblank = mode->vts - mode->height;
	__v4l2_ctrl_modify_range(s5k4ha->vblank, vblank,
				 S5K4HA_VTS_MAX - mode->height, 1, vblank);
	__v4l2_ctrl_s_ctrl(s5k4ha->vblank, vblank);

	exposure_max = mode->vts - S5K4HA_EXPOSURE_MARGIN;
	__v4l2_ctrl_modify_range(s5k4ha->exposure, S5K4HA_EXPOSURE_MIN,
				 exposure_max, S5K4HA_EXPOSURE_STEP,
				 mode->exposure);
	__v4l2_ctrl_s_ctrl(s5k4ha->exposure, mode->exposure);

	if (s5k4ha->sd.ctrl_handler->error)
		return s5k4ha->sd.ctrl_handler->error;

	s5k4ha->mode = mode;

set_format:
	*v4l2_subdev_state_get_format(state, 0) = fmt->format;

	return 0;
}

static int s5k4ha_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct s5k4ha *s5k4ha = to_s5k4ha(sd);

	/* Media bus code index is constant, but code formats are not */
	if (code->index > 0)
		return -EINVAL;

	code->code = s5k4ha_get_format_code(s5k4ha);

	return 0;
}

static int s5k4ha_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	struct s5k4ha *s5k4ha = to_s5k4ha(sd);

	if (fse->index >= ARRAY_SIZE(s5k4ha_supported_modes))
		return -EINVAL;

	if (fse->code != s5k4ha_get_format_code(s5k4ha))
		return -EINVAL;

	fse->min_width = s5k4ha_supported_modes[fse->index].width;
	fse->max_width = fse->min_width;
	fse->min_height = s5k4ha_supported_modes[fse->index].height;
	fse->max_height = fse->min_height;

	return 0;
}

static int s5k4ha_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_selection *sel)
{
	struct s5k4ha *s5k4ha = to_s5k4ha(sd);

	if (sel->which != V4L2_SUBDEV_FORMAT_ACTIVE)
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = s5k4ha->mode->width;
		sel->r.height = s5k4ha->mode->height;
		return 0;
	default:
		return -EINVAL;
	}

	return 0;
}

static int s5k4ha_init_state(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *state)
{
	struct s5k4ha *s5k4ha = to_s5k4ha(sd);
	struct v4l2_subdev_format fmt = {
		.which = V4L2_SUBDEV_FORMAT_TRY,
		.pad = 0,
		.format = {
			/* Media bus code depends on current flip controls */
			.width = s5k4ha->mode->width,
			.height = s5k4ha->mode->height,
		},
	};

	s5k4ha_set_pad_format(sd, state, &fmt);

	return 0;
}

static const struct v4l2_subdev_video_ops s5k4ha_video_ops = {
	.s_stream = v4l2_subdev_s_stream_helper,
};

static const struct v4l2_subdev_pad_ops s5k4ha_pad_ops = {
	.set_fmt = s5k4ha_set_pad_format,
	.get_fmt = v4l2_subdev_get_fmt,
	.get_selection = s5k4ha_get_selection,
	.enum_mbus_code = s5k4ha_enum_mbus_code,
	.enum_frame_size = s5k4ha_enum_frame_size,
	.enable_streams = s5k4ha_enable_streams,
	.disable_streams = s5k4ha_disable_streams,
};

static const struct v4l2_subdev_ops s5k4ha_subdev_ops = {
	.video = &s5k4ha_video_ops,
	.pad = &s5k4ha_pad_ops,
};

static const struct v4l2_subdev_internal_ops s5k4ha_internal_ops = {
	.init_state = s5k4ha_init_state,
};

static const struct media_entity_operations s5k4ha_subdev_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static int s5k4ha_identify_sensor(struct s5k4ha *s5k4ha)
{
	u64 val;
	int ret;

	ret = cci_read(s5k4ha->regmap, S5K4HA_REG_CHIP_ID, &val, NULL);
	if (ret) {
		dev_err(s5k4ha->dev, "failed to read chip id: %d\n", ret);
		return ret;
	}

	if (val != S5K4HA_CHIP_ID) {
		dev_err(s5k4ha->dev, "chip id mismatch: %x!=%llx\n",
			S5K4HA_CHIP_ID, val);
		return -ENODEV;
	}

	return 0;
}

static int s5k4ha_check_hwcfg(struct s5k4ha *s5k4ha)
{
	struct fwnode_handle *fwnode = dev_fwnode(s5k4ha->dev), *ep;
	struct v4l2_fwnode_endpoint bus_cfg = {
		.bus = {
			.mipi_csi2 = {
				.num_data_lanes = S5K4HA_DATA_LANES,
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

	if (bus_cfg.bus.mipi_csi2.num_data_lanes != S5K4HA_DATA_LANES) {
		dev_err(s5k4ha->dev, "Invalid number of data lanes: %u\n",
			bus_cfg.bus.mipi_csi2.num_data_lanes);
		ret = -EINVAL;
		goto endpoint_free;
	}

	ret = v4l2_link_freq_to_bitmap(s5k4ha->dev, bus_cfg.link_frequencies,
				       bus_cfg.nr_of_link_frequencies,
				       s5k4ha_link_freq_menu,
				       ARRAY_SIZE(s5k4ha_link_freq_menu),
				       &freq_bitmap);

endpoint_free:
	v4l2_fwnode_endpoint_free(&bus_cfg);

	return ret;
}

static int s5k4ha_power_on(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct s5k4ha *s5k4ha = to_s5k4ha(sd);
	int ret;

	ret = regulator_bulk_enable(S5K4HA_NUM_SUPPLIES, s5k4ha->supplies);
	if (ret)
		return ret;

	ret = clk_prepare_enable(s5k4ha->mclk);
	if (ret)
		goto disable_regulators;

	gpiod_set_value_cansleep(s5k4ha->reset_gpio, 0);
	usleep_range(10 * USEC_PER_MSEC, 15 * USEC_PER_MSEC);

	return 0;

disable_regulators:
	regulator_bulk_disable(S5K4HA_NUM_SUPPLIES, s5k4ha->supplies);

	return ret;
}

static int s5k4ha_power_off(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct s5k4ha *s5k4ha = to_s5k4ha(sd);

	gpiod_set_value_cansleep(s5k4ha->reset_gpio, 1);

	clk_disable_unprepare(s5k4ha->mclk);

	regulator_bulk_disable(S5K4HA_NUM_SUPPLIES, s5k4ha->supplies);

	return 0;
}

static int s5k4ha_probe(struct i2c_client *client)
{
	struct s5k4ha *s5k4ha;
	unsigned long freq;
	unsigned int i;
	int ret;

	s5k4ha = devm_kzalloc(&client->dev, sizeof(*s5k4ha), GFP_KERNEL);
	if (!s5k4ha)
		return -ENOMEM;

	s5k4ha->dev = &client->dev;
	v4l2_i2c_subdev_init(&s5k4ha->sd, client, &s5k4ha_subdev_ops);

	s5k4ha->regmap = devm_cci_regmap_init_i2c(client, 16);
	if (IS_ERR(s5k4ha->regmap))
		return dev_err_probe(s5k4ha->dev, PTR_ERR(s5k4ha->regmap),
				     "failed to init CCI\n");

	s5k4ha->mclk = devm_v4l2_sensor_clk_get(s5k4ha->dev, NULL);
	if (IS_ERR(s5k4ha->mclk))
		return dev_err_probe(s5k4ha->dev, PTR_ERR(s5k4ha->mclk),
				     "failed to get MCLK clock\n");

	freq = clk_get_rate(s5k4ha->mclk);
	if (freq != S5K4HA_MCLK_FREQ_24MHZ)
		return dev_err_probe(s5k4ha->dev, -EINVAL,
				     "MCLK clock frequency %lu is not supported\n",
				     freq);

	ret = s5k4ha_check_hwcfg(s5k4ha);
	if (ret)
		return dev_err_probe(s5k4ha->dev, ret,
				     "failed to check HW configuration\n");

	s5k4ha->reset_gpio = devm_gpiod_get_optional(s5k4ha->dev, "reset",
						     GPIOD_OUT_HIGH);
	if (IS_ERR(s5k4ha->reset_gpio))
		return dev_err_probe(s5k4ha->dev, PTR_ERR(s5k4ha->reset_gpio),
				     "cannot get reset GPIO\n");

	s5k4ha->mipi_sel_gpio = devm_gpiod_get_optional(s5k4ha->dev, "mipi-sel",
						GPIOD_ASIS | GPIOD_FLAGS_BIT_NONEXCLUSIVE);
	if (IS_ERR(s5k4ha->mipi_sel_gpio))
		return dev_err_probe(s5k4ha->dev, PTR_ERR(s5k4ha->mipi_sel_gpio),
				     "cannot get MIPI switch GPIO\n");
	if (s5k4ha->mipi_sel_gpio)
		device_property_read_u32(s5k4ha->dev, "mipi-sel-value", &s5k4ha->mipi_sel_value);

	for (i = 0; i < S5K4HA_NUM_SUPPLIES; i++)
		s5k4ha->supplies[i].supply = s5k4ha_supply_names[i];

	ret = devm_regulator_bulk_get(s5k4ha->dev, S5K4HA_NUM_SUPPLIES,
				      s5k4ha->supplies);
	if (ret)
		return dev_err_probe(s5k4ha->dev, ret,
				     "failed to get supply regulators\n");

	/* The sensor must be powered on to read the CHIP_ID register */
	ret = s5k4ha_power_on(s5k4ha->dev);
	if (ret)
		return ret;

	ret = s5k4ha_identify_sensor(s5k4ha);
	if (ret) {
		dev_err_probe(s5k4ha->dev, ret, "failed to find sensor\n");
		goto power_off;
	}

	s5k4ha->mode = &s5k4ha_supported_modes[0];
	ret = s5k4ha_init_controls(s5k4ha);
	if (ret) {
		dev_err_probe(s5k4ha->dev, ret, "failed to init controls\n");
		goto power_off;
	}

	s5k4ha->sd.state_lock = s5k4ha->ctrl_handler.lock;
	s5k4ha->sd.internal_ops = &s5k4ha_internal_ops;
	s5k4ha->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	s5k4ha->sd.entity.ops = &s5k4ha_subdev_entity_ops;
	s5k4ha->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	s5k4ha->pad.flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&s5k4ha->sd.entity, 1, &s5k4ha->pad);
	if (ret) {
		dev_err_probe(s5k4ha->dev, ret,
			      "failed to init media entity pads\n");
		goto v4l2_ctrl_handler_free;
	}

	ret = v4l2_subdev_init_finalize(&s5k4ha->sd);
	if (ret < 0) {
		dev_err_probe(s5k4ha->dev, ret,
			      "failed to init media entity pads\n");
		goto media_entity_cleanup;
	}

	pm_runtime_set_active(s5k4ha->dev);
	pm_runtime_enable(s5k4ha->dev);

	ret = v4l2_async_register_subdev_sensor(&s5k4ha->sd);
	if (ret < 0) {
		dev_err_probe(s5k4ha->dev, ret,
			      "failed to register V4L2 subdev\n");
		goto subdev_cleanup;
	}

	pm_runtime_set_autosuspend_delay(s5k4ha->dev, 1000);
	pm_runtime_use_autosuspend(s5k4ha->dev);
	pm_runtime_idle(s5k4ha->dev);

	return 0;

subdev_cleanup:
	v4l2_subdev_cleanup(&s5k4ha->sd);
	pm_runtime_disable(s5k4ha->dev);
	pm_runtime_set_suspended(s5k4ha->dev);

media_entity_cleanup:
	media_entity_cleanup(&s5k4ha->sd.entity);

v4l2_ctrl_handler_free:
	v4l2_ctrl_handler_free(s5k4ha->sd.ctrl_handler);

power_off:
	s5k4ha_power_off(s5k4ha->dev);

	return ret;
}

static void s5k4ha_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct s5k4ha *s5k4ha = to_s5k4ha(sd);

	v4l2_async_unregister_subdev(sd);
	v4l2_subdev_cleanup(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(sd->ctrl_handler);
	pm_runtime_disable(s5k4ha->dev);

	if (!pm_runtime_status_suspended(s5k4ha->dev)) {
		s5k4ha_power_off(s5k4ha->dev);
		pm_runtime_set_suspended(s5k4ha->dev);
	}
}

static const struct dev_pm_ops s5k4ha_pm_ops = {
	SET_RUNTIME_PM_OPS(s5k4ha_power_off, s5k4ha_power_on, NULL)
};

static const struct of_device_id s5k4ha_of_match[] = {
	{ .compatible = "samsung,s5k4ha" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, s5k4ha_of_match);

static struct i2c_driver s5k4ha_i2c_driver = {
	.driver = {
		.name = "s5k4ha",
		.pm = &s5k4ha_pm_ops,
		.of_match_table = s5k4ha_of_match,
	},
	.probe = s5k4ha_probe,
	.remove = s5k4ha_remove,
};

module_i2c_driver(s5k4ha_i2c_driver);

MODULE_AUTHOR("Vladimir Zapolskiy <vladimir.zapolskiy@linaro.org>");
MODULE_DESCRIPTION("Samsung S5K4HA image sensor driver");
MODULE_LICENSE("GPL");
