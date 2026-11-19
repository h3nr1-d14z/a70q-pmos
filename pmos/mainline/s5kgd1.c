// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2025 Linaro Ltd
//
// Samsung S5KGD1 driver (Galaxy A70 rear camera), adapted from the S5K3M5 driver: same
// Samsung sensor family/register model; settings from the device's camera module data.

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

#define S5KGD1_LINK_FREQ	(513000ULL * HZ_PER_KHZ)	/* OP PLL 24/4*342/4 MHz */
#define S5KGD1_MCLK_FREQ_24MHZ		(24 * HZ_PER_MHZ)
#define S5KGD1_DATA_LANES		4

/* Register map is similar to MIPI CCS compliant camera sensors */
#define S5KGD1_REG_CHIP_ID		CCI_REG16(0x0000)
#define S5KGD1_CHIP_ID			0x0841

/* Both streaming and flipping settings are controlled at the same time */
#define S5KGD1_REG_CTRL_MODE		CCI_REG16(0x0100)
#define S5KGD1_MODE_STREAMING		BIT(8)
#define S5KGD1_VFLIP			BIT(1)
#define S5KGD1_HFLIP			BIT(0)

#define S5KGD1_REG_EXPOSURE		CCI_REG16(0x0202)
#define S5KGD1_EXPOSURE_MIN		8
#define S5KGD1_EXPOSURE_STEP		1
#define S5KGD1_EXPOSURE_MARGIN		4

#define S5KGD1_REG_AGAIN		CCI_REG16(0x0204)
/* register code, gain = code / 32 (1x .. 16x) */
#define S5KGD1_AGAIN_MIN		32
#define S5KGD1_AGAIN_MAX		512
#define S5KGD1_AGAIN_STEP		1
#define S5KGD1_AGAIN_DEFAULT		32

#define S5KGD1_REG_VTS			CCI_REG16(0x0340)
#define S5KGD1_VTS_MAX			0xffff

#define S5KGD1_REG_HTS			CCI_REG16(0x0342)
#define S5KGD1_REG_X_ADDR_START		CCI_REG16(0x0344)
#define S5KGD1_REG_Y_ADDR_START		CCI_REG16(0x0346)
#define S5KGD1_REG_X_ADDR_END		CCI_REG16(0x0348)
#define S5KGD1_REG_Y_ADDR_END		CCI_REG16(0x034a)
#define S5KGD1_REG_X_OUTPUT_SIZE	CCI_REG16(0x034c)
#define S5KGD1_REG_Y_OUTPUT_SIZE	CCI_REG16(0x034e)

#define S5KGD1_REG_TEST_PATTERN		CCI_REG16(0x0600)

#define to_s5kgd1(_sd)			container_of(_sd, struct s5kgd1, sd)

static const s64 s5kgd1_link_freq_menu[] = {
	S5KGD1_LINK_FREQ,
};

/* List of supported formats to cover horizontal and vertical flip controls */
static const u32 s5kgd1_mbus_formats[] = {
	MEDIA_BUS_FMT_SGRBG10_1X10,	MEDIA_BUS_FMT_SRGGB10_1X10,
	MEDIA_BUS_FMT_SBGGR10_1X10,	MEDIA_BUS_FMT_SGBRG10_1X10,
};

struct s5kgd1_reg_list {
	const struct cci_reg_sequence *regs;
	unsigned int num_regs;
};

struct s5kgd1_mode {
	u32 width;			/* Frame width in pixels */
	u32 height;			/* Frame height in pixels */
	u32 hts;			/* Horizontal timing size */
	u32 vts;			/* Default vertical timing size */
	u32 exposure;			/* Default exposure value */

	const struct s5kgd1_reg_list reg_list;	/* Sensor register setting */
};

static const char * const s5kgd1_test_pattern_menu[] = {
	"Disabled",
	"Solid colour",
	"Colour bars",
	"Fade to grey colour bars",
	"PN9",
};

static const char * const s5kgd1_supply_names[] = {
	"afvdd",	/* Autofocus power */
	"vdda",		/* Analog power */
	"vddd",		/* Digital core power */
	"vddio",	/* Digital I/O power */
};

#define S5KGD1_NUM_SUPPLIES	ARRAY_SIZE(s5kgd1_supply_names)

struct s5kgd1 {
	struct device *dev;
	struct regmap *regmap;
	struct clk *mclk;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *mipi_sel_gpio;	/* shared MIPI lane switch (optional) */
	u32 mipi_sel_value;
	struct regulator_bulk_data supplies[S5KGD1_NUM_SUPPLIES];

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

	const struct s5kgd1_mode *mode;
};

/*
 * Register settings of the Galaxy A70 rear camera module, 3264x2448 (2x2 binned), 4 lanes,
 * from the vendor sensor module (com.samsung.sensormodule.0_lsi_s5kgd1, regSetting block 1).
 */
static const struct cci_reg_sequence init_array_setting[] = {
	{ CCI_REG16(0xfcfc), 0x4000 },
	{ CCI_REG16(0x6214), 0xe9f0 },
	{ CCI_REG16(0x6218), 0xe150 },
	{ CCI_REG16(0x6242), 0x0e00 },
};

/* sensor firmware patch (vendor initSettings: burst writes to 0x2001:9200 and 0x2001:ca00) */
static const struct cci_reg_sequence tnp_array_setting[] = {
	{ CCI_REG16(0xfcfc), 0x4000 },
	{ CCI_REG16(0x6004), 0x0001 },
	{ CCI_REG16(0x6028), 0x2001 },
	{ CCI_REG16(0x602a), 0x9200 },
	{ CCI_REG16(0x6f12), 0x0e49 },
	{ CCI_REG16(0x6f12), 0x10b5 },
	{ CCI_REG16(0x6f12), 0x0c48 },
	{ CCI_REG16(0x6f12), 0x0e4a },
	{ CCI_REG16(0x6f12), 0xc1f8 },
	{ CCI_REG16(0x6f12), 0xd006 },
	{ CCI_REG16(0x6f12), 0x101a },
	{ CCI_REG16(0x6f12), 0xa1f8 },
	{ CCI_REG16(0x6f12), 0xd406 },
	{ CCI_REG16(0x6f12), 0x0022 },
	{ CCI_REG16(0x6f12), 0x0b49 },
	{ CCI_REG16(0x6f12), 0x0c48 },
	{ CCI_REG16(0x6f12), 0x03f0 },
	{ CCI_REG16(0x6f12), 0xf2fb },
	{ CCI_REG16(0x6f12), 0x0b49 },
	{ CCI_REG16(0x6f12), 0x0022 },
	{ CCI_REG16(0x6f12), 0x4861 },
	{ CCI_REG16(0x6f12), 0x0c49 },
	{ CCI_REG16(0x6f12), 0x0a48 },
	{ CCI_REG16(0x6f12), 0xc1f8 },
	{ CCI_REG16(0x6f12), 0x4c02 },
	{ CCI_REG16(0x6f12), 0xbde8 },
	{ CCI_REG16(0x6f12), 0x1040 },
	{ CCI_REG16(0x6f12), 0x0a49 },
	{ CCI_REG16(0x6f12), 0x0a48 },
	{ CCI_REG16(0x6f12), 0x00f0 },
	{ CCI_REG16(0x6f12), 0xbbb9 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x6f12), 0x2001 },
	{ CCI_REG16(0x6f12), 0x6884 },
	{ CCI_REG16(0x6f12), 0x2000 },
	{ CCI_REG16(0x6f12), 0x68c0 },
	{ CCI_REG16(0x6f12), 0x2001 },
	{ CCI_REG16(0x6f12), 0x9200 },
	{ CCI_REG16(0x6f12), 0x2001 },
	{ CCI_REG16(0x6f12), 0x9559 },
	{ CCI_REG16(0x6f12), 0x0002 },
	{ CCI_REG16(0x6f12), 0x0f85 },
	{ CCI_REG16(0x6f12), 0x2001 },
	{ CCI_REG16(0x6f12), 0xce00 },
	{ CCI_REG16(0x6f12), 0x2001 },
	{ CCI_REG16(0x6f12), 0x9273 },
	{ CCI_REG16(0x6f12), 0x2001 },
	{ CCI_REG16(0x6f12), 0xf000 },
	{ CCI_REG16(0x6f12), 0x2001 },
	{ CCI_REG16(0x6f12), 0x9261 },
	{ CCI_REG16(0x6f12), 0x0001 },
	{ CCI_REG16(0x6f12), 0x7e15 },
	{ CCI_REG16(0x6f12), 0x70b5 },
	{ CCI_REG16(0x6f12), 0xca4c },
	{ CCI_REG16(0x6f12), 0x0120 },
	{ CCI_REG16(0x6f12), 0x2578 },
	{ CCI_REG16(0x6f12), 0x2070 },
	{ CCI_REG16(0x6f12), 0xfcf7 },
	{ CCI_REG16(0x6f12), 0x35ff },
	{ CCI_REG16(0x6f12), 0x2570 },
	{ CCI_REG16(0x6f12), 0x70bd },
	{ CCI_REG16(0x6f12), 0x2de9 },
	{ CCI_REG16(0x6f12), 0xf041 },
	{ CCI_REG16(0x6f12), 0x0c46 },
	{ CCI_REG16(0x6f12), 0x00f0 },
	{ CCI_REG16(0x6f12), 0x9df9 },
	{ CCI_REG16(0x6f12), 0x002c },
	{ CCI_REG16(0x6f12), 0x46d1 },
	{ CCI_REG16(0x6f12), 0xc348 },
	{ CCI_REG16(0x6f12), 0x0078 },
	{ CCI_REG16(0x6f12), 0x0028 },
	{ CCI_REG16(0x6f12), 0x42d0 },
	{ CCI_REG16(0x6f12), 0xc048 },
	{ CCI_REG16(0x6f12), 0x3030 },
	{ CCI_REG16(0x6f12), 0x8288 },
	{ CCI_REG16(0x6f12), 0xc188 },
	{ CCI_REG16(0x6f12), 0x8a42 },
	{ CCI_REG16(0x6f12), 0x01d9 },
	{ CCI_REG16(0x6f12), 0x1346 },
	{ CCI_REG16(0x6f12), 0x00e0 },
	{ CCI_REG16(0x6f12), 0x0b46 },
	{ CCI_REG16(0x6f12), 0x4088 },
	{ CCI_REG16(0x6f12), 0x8342 },
	{ CCI_REG16(0x6f12), 0x01d2 },
	{ CCI_REG16(0x6f12), 0x0246 },
	{ CCI_REG16(0x6f12), 0x02e0 },
	{ CCI_REG16(0x6f12), 0x8a42 },
	{ CCI_REG16(0x6f12), 0x00d8 },
	{ CCI_REG16(0x6f12), 0x0a46 },
	{ CCI_REG16(0x6f12), 0x4ff4 },
	{ CCI_REG16(0x6f12), 0x8064 },
	{ CCI_REG16(0x6f12), 0xa242 },
	{ CCI_REG16(0x6f12), 0x05d9 },
	{ CCI_REG16(0x6f12), 0xa104 },
	{ CCI_REG16(0x6f12), 0x01eb },
	{ CCI_REG16(0x6f12), 0x5201 },
	{ CCI_REG16(0x6f12), 0xb1fb },
	{ CCI_REG16(0x6f12), 0xf2f1 },
	{ CCI_REG16(0x6f12), 0x0c0a },
	{ CCI_REG16(0x6f12), 0xb54d },
	{ CCI_REG16(0x6f12), 0x2146 },
	{ CCI_REG16(0x6f12), 0x00f0 },
	{ CCI_REG16(0x6f12), 0x7df9 },
	{ CCI_REG16(0x6f12), 0x87b2 },
	{ CCI_REG16(0x6f12), 0xb5f8 },
	{ CCI_REG16(0x6f12), 0xa009 },
	{ CCI_REG16(0x6f12), 0x2146 },
	{ CCI_REG16(0x6f12), 0x00f0 },
	{ CCI_REG16(0x6f12), 0x77f9 },
	{ CCI_REG16(0x6f12), 0x86b2 },
	{ CCI_REG16(0x6f12), 0xb5f8 },
	{ CCI_REG16(0x6f12), 0xa209 },
	{ CCI_REG16(0x6f12), 0x2146 },
	{ CCI_REG16(0x6f12), 0x00f0 },
	{ CCI_REG16(0x6f12), 0x71f9 },
	{ CCI_REG16(0x6f12), 0x4ff0 },
	{ CCI_REG16(0x6f12), 0x8041 },
	{ CCI_REG16(0x6f12), 0xb1fb },
	{ CCI_REG16(0x6f12), 0xf7f2 },
	{ CCI_REG16(0x6f12), 0x930a },
	{ CCI_REG16(0x6f12), 0xb1fb },
	{ CCI_REG16(0x6f12), 0xf6f2 },
	{ CCI_REG16(0x6f12), 0x80b2 },
	{ CCI_REG16(0x6f12), 0xb1fb },
	{ CCI_REG16(0x6f12), 0xf0f1 },
	{ CCI_REG16(0x6f12), 0xa94c },
	{ CCI_REG16(0x6f12), 0x920a },
	{ CCI_REG16(0x6f12), 0x890a },
	{ CCI_REG16(0x6f12), 0x2780 },
	{ CCI_REG16(0x6f12), 0xa41c },
	{ CCI_REG16(0x6f12), 0x2680 },
	{ CCI_REG16(0x6f12), 0xa41c },
	{ CCI_REG16(0x6f12), 0x2080 },
	{ CCI_REG16(0x6f12), 0xa01c },
	{ CCI_REG16(0x6f12), 0x0380 },
	{ CCI_REG16(0x6f12), 0x801c },
	{ CCI_REG16(0x6f12), 0x0280 },
	{ CCI_REG16(0x6f12), 0x801c },
	{ CCI_REG16(0x6f12), 0x0180 },
	{ CCI_REG16(0x6f12), 0xbde8 },
	{ CCI_REG16(0x6f12), 0xf081 },
	{ CCI_REG16(0x6f12), 0x2de9 },
	{ CCI_REG16(0x6f12), 0xf84f },
	{ CCI_REG16(0x6f12), 0x8146 },
	{ CCI_REG16(0x6f12), 0xdde9 },
	{ CCI_REG16(0x6f12), 0x0a5b },
	{ CCI_REG16(0x6f12), 0x0020 },
	{ CCI_REG16(0x6f12), 0x8df8 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x6f12), 0x9e48 },
	{ CCI_REG16(0x6f12), 0x0f46 },
	{ CCI_REG16(0x6f12), 0x1e46 },
	{ CCI_REG16(0x6f12), 0xb0f8 },
	{ CCI_REG16(0x6f12), 0x5610 },
	{ CCI_REG16(0x6f12), 0x0120 },
	{ CCI_REG16(0x6f12), 0xb840 },
	{ CCI_REG16(0x6f12), 0xa0f1 },
	{ CCI_REG16(0x6f12), 0x010a },
	{ CCI_REG16(0x6f12), 0x1068 },
	{ CCI_REG16(0x6f12), 0x1446 },
	{ CCI_REG16(0x6f12), 0xc7f1 },
	{ CCI_REG16(0x6f12), 0x2008 },
	{ CCI_REG16(0x6f12), 0x0129 },
	{ CCI_REG16(0x6f12), 0x20d0 },
	{ CCI_REG16(0x6f12), 0x56e0 },
	{ CCI_REG16(0x6f12), 0x9748 },
	{ CCI_REG16(0x6f12), 0xb0f8 },
	{ CCI_REG16(0x6f12), 0x3e12 },
	{ CCI_REG16(0x6f12), 0xa1f5 },
	{ CCI_REG16(0x6f12), 0x9050 },
	{ CCI_REG16(0x6f12), 0x3168 },
	{ CCI_REG16(0x6f12), 0x3d38 },
	{ CCI_REG16(0x6f12), 0x06d0 },
	{ CCI_REG16(0x6f12), 0x01eb },
	{ CCI_REG16(0x6f12), 0x0900 },
	{ CCI_REG16(0x6f12), 0x0122 },
	{ CCI_REG16(0x6f12), 0x6946 },
	{ CCI_REG16(0x6f12), 0xfdf7 },
	{ CCI_REG16(0x6f12), 0xbbf9 },
	{ CCI_REG16(0x6f12), 0x03e0 },
	{ CCI_REG16(0x6f12), 0x9048 },
	{ CCI_REG16(0x6f12), 0x405c },
	{ CCI_REG16(0x6f12), 0x8df8 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x6f12), 0xe278 },
	{ CCI_REG16(0x6f12), 0x9df8 },
	{ CCI_REG16(0x6f12), 0x0010 },
	{ CCI_REG16(0x6f12), 0x2868 },
	{ CCI_REG16(0x6f12), 0x9140 },
	{ CCI_REG16(0x6f12), 0x0843 },
	{ CCI_REG16(0x6f12), 0x2860 },
	{ CCI_REG16(0x6f12), 0x3068 },
	{ CCI_REG16(0x6f12), 0x401c },
	{ CCI_REG16(0x6f12), 0x3060 },
	{ CCI_REG16(0x6f12), 0x2068 },
	{ CCI_REG16(0x6f12), 0x0830 },
	{ CCI_REG16(0x6f12), 0x2060 },
	{ CCI_REG16(0x6f12), 0xb842 },
	{ CCI_REG16(0x6f12), 0xdddb },
	{ CCI_REG16(0x6f12), 0x2868 },
	{ CCI_REG16(0x6f12), 0x0122 },
	{ CCI_REG16(0x6f12), 0x00ea },
	{ CCI_REG16(0x6f12), 0x0a00 },
	{ CCI_REG16(0x6f12), 0xbbf1 },
	{ CCI_REG16(0x6f12), 0x000f },
	{ CCI_REG16(0x6f12), 0x03d0 },
	{ CCI_REG16(0x6f12), 0x00fa },
	{ CCI_REG16(0x6f12), 0x08f0 },
	{ CCI_REG16(0x6f12), 0x40fa },
	{ CCI_REG16(0x6f12), 0x08f0 },
	{ CCI_REG16(0x6f12), 0x2168 },
	{ CCI_REG16(0x6f12), 0xc91b },
	{ CCI_REG16(0x6f12), 0x2160 },
	{ CCI_REG16(0x6f12), 0x8a40 },
	{ CCI_REG16(0x6f12), 0x2b68 },
	{ CCI_REG16(0x6f12), 0x521e },
	{ CCI_REG16(0x6f12), 0x1340 },
	{ CCI_REG16(0x6f12), 0x2b60 },
	{ CCI_REG16(0x6f12), 0xbde8 },
	{ CCI_REG16(0x6f12), 0xf88f },
	{ CCI_REG16(0x6f12), 0x0002 },
	{ CCI_REG16(0x6f12), 0x2860 },
	{ CCI_REG16(0x6f12), 0x7a48 },
	{ CCI_REG16(0x6f12), 0xb0f8 },
	{ CCI_REG16(0x6f12), 0x3e12 },
	{ CCI_REG16(0x6f12), 0xa1f5 },
	{ CCI_REG16(0x6f12), 0x9050 },
	{ CCI_REG16(0x6f12), 0x3168 },
	{ CCI_REG16(0x6f12), 0x3d38 },
	{ CCI_REG16(0x6f12), 0x06d0 },
	{ CCI_REG16(0x6f12), 0x01eb },
	{ CCI_REG16(0x6f12), 0x0900 },
	{ CCI_REG16(0x6f12), 0x0122 },
	{ CCI_REG16(0x6f12), 0x6946 },
	{ CCI_REG16(0x6f12), 0xfdf7 },
	{ CCI_REG16(0x6f12), 0x82f9 },
	{ CCI_REG16(0x6f12), 0x03e0 },
	{ CCI_REG16(0x6f12), 0x7448 },
	{ CCI_REG16(0x6f12), 0x405c },
	{ CCI_REG16(0x6f12), 0x8df8 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x6f12), 0x9df8 },
	{ CCI_REG16(0x6f12), 0x0010 },
	{ CCI_REG16(0x6f12), 0x2868 },
	{ CCI_REG16(0x6f12), 0x0843 },
	{ CCI_REG16(0x6f12), 0x2860 },
	{ CCI_REG16(0x6f12), 0x3068 },
	{ CCI_REG16(0x6f12), 0x401c },
	{ CCI_REG16(0x6f12), 0x3060 },
	{ CCI_REG16(0x6f12), 0x2068 },
	{ CCI_REG16(0x6f12), 0x0830 },
	{ CCI_REG16(0x6f12), 0x2060 },
	{ CCI_REG16(0x6f12), 0xb842 },
	{ CCI_REG16(0x6f12), 0x2868 },
	{ CCI_REG16(0x6f12), 0xdcdb },
	{ CCI_REG16(0x6f12), 0x2168 },
	{ CCI_REG16(0x6f12), 0xc91b },
	{ CCI_REG16(0x6f12), 0xc840 },
	{ CCI_REG16(0x6f12), 0x00ea },
	{ CCI_REG16(0x6f12), 0x0a00 },
	{ CCI_REG16(0x6f12), 0xbbf1 },
	{ CCI_REG16(0x6f12), 0x000f },
	{ CCI_REG16(0x6f12), 0x03d0 },
	{ CCI_REG16(0x6f12), 0x00fa },
	{ CCI_REG16(0x6f12), 0x08f0 },
	{ CCI_REG16(0x6f12), 0x40fa },
	{ CCI_REG16(0x6f12), 0x08f0 },
	{ CCI_REG16(0x6f12), 0x2160 },
	{ CCI_REG16(0x6f12), 0xcce7 },
	{ CCI_REG16(0x6f12), 0x2de9 },
	{ CCI_REG16(0x6f12), 0xf04f },
	{ CCI_REG16(0x6f12), 0x87b0 },
	{ CCI_REG16(0x6f12), 0x0026 },
	{ CCI_REG16(0x6f12), 0x634f },
	{ CCI_REG16(0x6f12), 0x8346 },
	{ CCI_REG16(0x6f12), 0x0296 },
	{ CCI_REG16(0x6f12), 0x0396 },
	{ CCI_REG16(0x6f12), 0x0496 },
	{ CCI_REG16(0x6f12), 0x388a },
	{ CCI_REG16(0x6f12), 0x0590 },
	{ CCI_REG16(0x6f12), 0x5c48 },
	{ CCI_REG16(0x6f12), 0x4088 },
	{ CCI_REG16(0x6f12), 0x18b1 },
	{ CCI_REG16(0x6f12), 0xa0f5 },
	{ CCI_REG16(0x6f12), 0x7f41 },
	{ CCI_REG16(0x6f12), 0xff39 },
	{ CCI_REG16(0x6f12), 0x06d1 },
	{ CCI_REG16(0x6f12), 0x5a48 },
	{ CCI_REG16(0x6f12), 0xb0f8 },
	{ CCI_REG16(0x6f12), 0x3e02 },
	{ CCI_REG16(0x6f12), 0xa0f5 },
	{ CCI_REG16(0x6f12), 0x9051 },
	{ CCI_REG16(0x6f12), 0x3d39 },
	{ CCI_REG16(0x6f12), 0x7cd1 },
	{ CCI_REG16(0x6f12), 0x4ff4 },
	{ CCI_REG16(0x6f12), 0xdc50 },
	{ CCI_REG16(0x6f12), 0x00f0 },
	{ CCI_REG16(0x6f12), 0xbff8 },
	{ CCI_REG16(0x6f12), 0x0400 },
	{ CCI_REG16(0x6f12), 0x76d0 },
	{ CCI_REG16(0x6f12), 0xb846 },
	{ CCI_REG16(0x6f12), 0x0025 },
	{ CCI_REG16(0x6f12), 0x04af },
	{ CCI_REG16(0x6f12), 0xcde9 },
	{ CCI_REG16(0x6f12), 0x0076 },
	{ CCI_REG16(0x6f12), 0x02ab },
	{ CCI_REG16(0x6f12), 0x03aa },
	{ CCI_REG16(0x6f12), 0x1021 },
	{ CCI_REG16(0x6f12), 0x0598 },
	{ CCI_REG16(0x6f12), 0xfff7 },
	{ CCI_REG16(0x6f12), 0x56ff },
	{ CCI_REG16(0x6f12), 0x08eb },
	{ CCI_REG16(0x6f12), 0x4501 },
	{ CCI_REG16(0x6f12), 0x6d1c },
	{ CCI_REG16(0x6f12), 0x4886 },
	{ CCI_REG16(0x6f12), 0x042d },
	{ CCI_REG16(0x6f12), 0xf1db },
	{ CCI_REG16(0x6f12), 0x0025 },
	{ CCI_REG16(0x6f12), 0xb046 },
	{ CCI_REG16(0x6f12), 0xb946 },
	{ CCI_REG16(0x6f12), 0x0026 },
	{ CCI_REG16(0x6f12), 0x04eb },
	{ CCI_REG16(0x6f12), 0x4517 },
	{ CCI_REG16(0x6f12), 0xcde9 },
	{ CCI_REG16(0x6f12), 0x0098 },
	{ CCI_REG16(0x6f12), 0x02ab },
	{ CCI_REG16(0x6f12), 0x03aa },
	{ CCI_REG16(0x6f12), 0x1021 },
	{ CCI_REG16(0x6f12), 0x0598 },
	{ CCI_REG16(0x6f12), 0xfff7 },
	{ CCI_REG16(0x6f12), 0x42ff },
	{ CCI_REG16(0x6f12), 0x27f8 },
	{ CCI_REG16(0x6f12), 0x1600 },
	{ CCI_REG16(0x6f12), 0xcde9 },
	{ CCI_REG16(0x6f12), 0x0098 },
	{ CCI_REG16(0x6f12), 0x02ab },
	{ CCI_REG16(0x6f12), 0x03aa },
	{ CCI_REG16(0x6f12), 0x1021 },
	{ CCI_REG16(0x6f12), 0x0598 },
	{ CCI_REG16(0x6f12), 0xfff7 },
	{ CCI_REG16(0x6f12), 0x38ff },
	{ CCI_REG16(0x6f12), 0x07eb },
	{ CCI_REG16(0x6f12), 0x4601 },
	{ CCI_REG16(0x6f12), 0x761c },
	{ CCI_REG16(0x6f12), 0x0881 },
	{ CCI_REG16(0x6f12), 0x042e },
	{ CCI_REG16(0x6f12), 0xe7db },
	{ CCI_REG16(0x6f12), 0x6d1c },
	{ CCI_REG16(0x6f12), 0x042d },
	{ CCI_REG16(0x6f12), 0xe1db },
	{ CCI_REG16(0x6f12), 0x0026 },
	{ CCI_REG16(0x6f12), 0x0025 },
	{ CCI_REG16(0x6f12), 0x04eb },
	{ CCI_REG16(0x6f12), 0x461a },
	{ CCI_REG16(0x6f12), 0xcde9 },
	{ CCI_REG16(0x6f12), 0x0098 },
	{ CCI_REG16(0x6f12), 0x02ab },
	{ CCI_REG16(0x6f12), 0x03aa },
	{ CCI_REG16(0x6f12), 0x1021 },
	{ CCI_REG16(0x6f12), 0x0598 },
	{ CCI_REG16(0x6f12), 0xfff7 },
	{ CCI_REG16(0x6f12), 0x23ff },
	{ CCI_REG16(0x6f12), 0x0aeb },
	{ CCI_REG16(0x6f12), 0x4507 },
	{ CCI_REG16(0x6f12), 0x02ab },
	{ CCI_REG16(0x6f12), 0x3882 },
	{ CCI_REG16(0x6f12), 0xcde9 },
	{ CCI_REG16(0x6f12), 0x0098 },
	{ CCI_REG16(0x6f12), 0x03aa },
	{ CCI_REG16(0x6f12), 0x1021 },
	{ CCI_REG16(0x6f12), 0x0598 },
	{ CCI_REG16(0x6f12), 0xfff7 },
	{ CCI_REG16(0x6f12), 0x18ff },
	{ CCI_REG16(0x6f12), 0x6d1c },
	{ CCI_REG16(0x6f12), 0x3883 },
	{ CCI_REG16(0x6f12), 0x042d },
	{ CCI_REG16(0x6f12), 0xe8db },
	{ CCI_REG16(0x6f12), 0x761c },
	{ CCI_REG16(0x6f12), 0x042e },
	{ CCI_REG16(0x6f12), 0xe2db },
	{ CCI_REG16(0x6f12), 0xc246 },
	{ CCI_REG16(0x6f12), 0x0027 },
	{ CCI_REG16(0x6f12), 0xc846 },
	{ CCI_REG16(0x6f12), 0x0026 },
	{ CCI_REG16(0x6f12), 0xd146 },
	{ CCI_REG16(0x6f12), 0x18e0 },
	{ CCI_REG16(0x6f12), 0x0025 },
	{ CCI_REG16(0x6f12), 0xcde9 },
	{ CCI_REG16(0x6f12), 0x0089 },
	{ CCI_REG16(0x6f12), 0x02ab },
	{ CCI_REG16(0x6f12), 0x03aa },
	{ CCI_REG16(0x6f12), 0x0821 },
	{ CCI_REG16(0x6f12), 0x0598 },
	{ CCI_REG16(0x6f12), 0xfff7 },
	{ CCI_REG16(0x6f12), 0x02ff },
	{ CCI_REG16(0x6f12), 0x0146 },
	{ CCI_REG16(0x6f12), 0x05eb },
	{ CCI_REG16(0x6f12), 0x8700 },
	{ CCI_REG16(0x6f12), 0x00eb },
	{ CCI_REG16(0x6f12), 0xc000 },
	{ CCI_REG16(0x6f12), 0x00eb },
	{ CCI_REG16(0x6f12), 0x4000 },
	{ CCI_REG16(0x6f12), 0x04eb },
	{ CCI_REG16(0x6f12), 0x0010 },
	{ CCI_REG16(0x6f12), 0x3044 },
	{ CCI_REG16(0x6f12), 0x6d1c },
	{ CCI_REG16(0x6f12), 0x80f8 },
	{ CCI_REG16(0x6f12), 0x8010 },
	{ CCI_REG16(0x6f12), 0x042d },
	{ CCI_REG16(0x6f12), 0xe8db },
	{ CCI_REG16(0x6f12), 0x761c },
	{ CCI_REG16(0x6f12), 0x5846 },
	{ CCI_REG16(0x6f12), 0x5e45 },
	{ CCI_REG16(0x6f12), 0xe3db },
	{ CCI_REG16(0x6f12), 0x7f1c },
	{ CCI_REG16(0x6f12), 0x042f },
	{ CCI_REG16(0x6f12), 0xdddb },
	{ CCI_REG16(0x6f12), 0x0246 },
	{ CCI_REG16(0x6f12), 0x00e0 },
	{ CCI_REG16(0x6f12), 0x08e0 },
	{ CCI_REG16(0x6f12), 0x04f1 },
	{ CCI_REG16(0x6f12), 0x8001 },
	{ CCI_REG16(0x6f12), 0x2046 },
	{ CCI_REG16(0x6f12), 0x00f0 },
	{ CCI_REG16(0x6f12), 0x45f8 },
	{ CCI_REG16(0x6f12), 0x4ff4 },
	{ CCI_REG16(0x6f12), 0xdc50 },
	{ CCI_REG16(0x6f12), 0x00f0 },
	{ CCI_REG16(0x6f12), 0x46f8 },
	{ CCI_REG16(0x6f12), 0x07b0 },
	{ CCI_REG16(0x6f12), 0xbde8 },
	{ CCI_REG16(0x6f12), 0xf08f },
	{ CCI_REG16(0x6f12), 0x1048 },
	{ CCI_REG16(0x6f12), 0x10b5 },
	{ CCI_REG16(0x6f12), 0x0088 },
	{ CCI_REG16(0x6f12), 0x18b1 },
	{ CCI_REG16(0x6f12), 0x1149 },
	{ CCI_REG16(0x6f12), 0x0120 },
	{ CCI_REG16(0x6f12), 0x8887 },
	{ CCI_REG16(0x6f12), 0x10bd },
	{ CCI_REG16(0x6f12), 0x0d48 },
	{ CCI_REG16(0x6f12), 0xb0f8 },
	{ CCI_REG16(0x6f12), 0x4002 },
	{ CCI_REG16(0x6f12), 0x0128 },
	{ CCI_REG16(0x6f12), 0x09d0 },
	{ CCI_REG16(0x6f12), 0x4ff4 },
	{ CCI_REG16(0x6f12), 0x8c70 },
	{ CCI_REG16(0x6f12), 0xfff7 },
	{ CCI_REG16(0x6f12), 0x4cff },
	{ CCI_REG16(0x6f12), 0x00f0 },
	{ CCI_REG16(0x6f12), 0x35f8 },
	{ CCI_REG16(0x6f12), 0xbde8 },
	{ CCI_REG16(0x6f12), 0x1040 },
	{ CCI_REG16(0x6f12), 0x00f0 },
	{ CCI_REG16(0x6f12), 0x36b8 },
	{ CCI_REG16(0x6f12), 0x4ff4 },
	{ CCI_REG16(0x6f12), 0xd870 },
	{ CCI_REG16(0x6f12), 0xf4e7 },
	{ CCI_REG16(0x6f12), 0x2000 },
	{ CCI_REG16(0x6f12), 0xc36c },
	{ CCI_REG16(0x6f12), 0x2000 },
	{ CCI_REG16(0x6f12), 0xed60 },
	{ CCI_REG16(0x6f12), 0x2000 },
	{ CCI_REG16(0x6f12), 0xba00 },
	{ CCI_REG16(0x6f12), 0x4000 },
	{ CCI_REG16(0x6f12), 0x9f5e },
	{ CCI_REG16(0x6f12), 0x2000 },
	{ CCI_REG16(0x6f12), 0x3020 },
	{ CCI_REG16(0x6f12), 0x2001 },
	{ CCI_REG16(0x6f12), 0xf000 },
	{ CCI_REG16(0x6f12), 0x2001 },
	{ CCI_REG16(0x6f12), 0xd000 },
	{ CCI_REG16(0x6f12), 0x2001 },
	{ CCI_REG16(0x6f12), 0x4550 },
	{ CCI_REG16(0x6f12), 0x4bf6 },
	{ CCI_REG16(0x6f12), 0x152c },
	{ CCI_REG16(0x6f12), 0xc0f2 },
	{ CCI_REG16(0x6f12), 0x000c },
	{ CCI_REG16(0x6f12), 0x6047 },
	{ CCI_REG16(0x6f12), 0x42f6 },
	{ CCI_REG16(0x6f12), 0x236c },
	{ CCI_REG16(0x6f12), 0xc0f2 },
	{ CCI_REG16(0x6f12), 0x010c },
	{ CCI_REG16(0x6f12), 0x6047 },
	{ CCI_REG16(0x6f12), 0x49f6 },
	{ CCI_REG16(0x6f12), 0x713c },
	{ CCI_REG16(0x6f12), 0xc0f2 },
	{ CCI_REG16(0x6f12), 0x010c },
	{ CCI_REG16(0x6f12), 0x6047 },
	{ CCI_REG16(0x6f12), 0x40f6 },
	{ CCI_REG16(0x6f12), 0xbd7c },
	{ CCI_REG16(0x6f12), 0xc0f2 },
	{ CCI_REG16(0x6f12), 0x000c },
	{ CCI_REG16(0x6f12), 0x6047 },
	{ CCI_REG16(0x6f12), 0x40f6 },
	{ CCI_REG16(0x6f12), 0x1d0c },
	{ CCI_REG16(0x6f12), 0xc0f2 },
	{ CCI_REG16(0x6f12), 0x020c },
	{ CCI_REG16(0x6f12), 0x6047 },
	{ CCI_REG16(0x6f12), 0x40f6 },
	{ CCI_REG16(0x6f12), 0xf97c },
	{ CCI_REG16(0x6f12), 0xc0f2 },
	{ CCI_REG16(0x6f12), 0x000c },
	{ CCI_REG16(0x6f12), 0x6047 },
	{ CCI_REG16(0x6f12), 0x40f2 },
	{ CCI_REG16(0x6f12), 0xe77c },
	{ CCI_REG16(0x6f12), 0xc0f2 },
	{ CCI_REG16(0x6f12), 0x020c },
	{ CCI_REG16(0x6f12), 0x6047 },
	{ CCI_REG16(0x6f12), 0x40f6 },
	{ CCI_REG16(0x6f12), 0xa32c },
	{ CCI_REG16(0x6f12), 0xc0f2 },
	{ CCI_REG16(0x6f12), 0x020c },
	{ CCI_REG16(0x6f12), 0x6047 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x6f12), 0x0841 },
	{ CCI_REG16(0x6f12), 0x03ea },
	{ CCI_REG16(0x6f12), 0x839f },
	{ CCI_REG16(0x6f12), 0x7ab7 },
	{ CCI_REG16(0x602a), 0xca00 },
	{ CCI_REG16(0x6f12), 0x4bf6 },
	{ CCI_REG16(0x6f12), 0x1523 },
	{ CCI_REG16(0x6f12), 0x1847 },
	{ CCI_REG16(0x6f12), 0x10b5 },
	{ CCI_REG16(0x6f12), 0x0022 },
	{ CCI_REG16(0x6f12), 0x2b49 },
	{ CCI_REG16(0x6f12), 0x2b48 },
	{ CCI_REG16(0x6f12), 0xfff7 },
	{ CCI_REG16(0x6f12), 0xf7ff },
	{ CCI_REG16(0x6f12), 0x2b4c },
	{ CCI_REG16(0x6f12), 0x0022 },
	{ CCI_REG16(0x6f12), 0x2b49 },
	{ CCI_REG16(0x6f12), 0x2060 },
	{ CCI_REG16(0x6f12), 0x2b48 },
	{ CCI_REG16(0x6f12), 0xfff7 },
	{ CCI_REG16(0x6f12), 0xf0ff },
	{ CCI_REG16(0x6f12), 0x6060 },
	{ CCI_REG16(0x6f12), 0x0022 },
	{ CCI_REG16(0x6f12), 0x2949 },
	{ CCI_REG16(0x6f12), 0x2a48 },
	{ CCI_REG16(0x6f12), 0xfff7 },
	{ CCI_REG16(0x6f12), 0xeaff },
	{ CCI_REG16(0x6f12), 0xa060 },
	{ CCI_REG16(0x6f12), 0x0022 },
	{ CCI_REG16(0x6f12), 0x2849 },
	{ CCI_REG16(0x6f12), 0x2948 },
	{ CCI_REG16(0x6f12), 0xfff7 },
	{ CCI_REG16(0x6f12), 0xe4ff },
	{ CCI_REG16(0x6f12), 0xe060 },
	{ CCI_REG16(0x6f12), 0x0022 },
	{ CCI_REG16(0x6f12), 0x2749 },
	{ CCI_REG16(0x6f12), 0x2848 },
	{ CCI_REG16(0x6f12), 0xfff7 },
	{ CCI_REG16(0x6f12), 0xdeff },
	{ CCI_REG16(0x6f12), 0x2061 },
	{ CCI_REG16(0x6f12), 0x0022 },
	{ CCI_REG16(0x6f12), 0x2649 },
	{ CCI_REG16(0x6f12), 0x2748 },
	{ CCI_REG16(0x6f12), 0xfff7 },
	{ CCI_REG16(0x6f12), 0xd8ff },
	{ CCI_REG16(0x6f12), 0xa062 },
	{ CCI_REG16(0x6f12), 0x0122 },
	{ CCI_REG16(0x6f12), 0x2549 },
	{ CCI_REG16(0x6f12), 0x2648 },
	{ CCI_REG16(0x6f12), 0xfff7 },
	{ CCI_REG16(0x6f12), 0xd2ff },
	{ CCI_REG16(0x6f12), 0xe062 },
	{ CCI_REG16(0x6f12), 0x0022 },
	{ CCI_REG16(0x6f12), 0x2449 },
	{ CCI_REG16(0x6f12), 0x2548 },
	{ CCI_REG16(0x6f12), 0xfcf7 },
	{ CCI_REG16(0x6f12), 0xa2fd },
	{ CCI_REG16(0x6f12), 0x2549 },
	{ CCI_REG16(0x6f12), 0x2448 },
	{ CCI_REG16(0x6f12), 0x0022 },
	{ CCI_REG16(0x6f12), 0x8864 },
	{ CCI_REG16(0x6f12), 0x2449 },
	{ CCI_REG16(0x6f12), 0x2548 },
	{ CCI_REG16(0x6f12), 0xfcf7 },
	{ CCI_REG16(0x6f12), 0x9afd },
	{ CCI_REG16(0x6f12), 0x0122 },
	{ CCI_REG16(0x6f12), 0x2449 },
	{ CCI_REG16(0x6f12), 0x2448 },
	{ CCI_REG16(0x6f12), 0xfff7 },
	{ CCI_REG16(0x6f12), 0xbfff },
	{ CCI_REG16(0x6f12), 0x2063 },
	{ CCI_REG16(0x6f12), 0x2348 },
	{ CCI_REG16(0x6f12), 0x244c },
	{ CCI_REG16(0x6f12), 0x0122 },
	{ CCI_REG16(0x6f12), 0xb0f8 },
	{ CCI_REG16(0x6f12), 0xcc03 },
	{ CCI_REG16(0x6f12), 0xa080 },
	{ CCI_REG16(0x6f12), 0x2249 },
	{ CCI_REG16(0x6f12), 0x2348 },
	{ CCI_REG16(0x6f12), 0xfcf7 },
	{ CCI_REG16(0x6f12), 0x8afd },
	{ CCI_REG16(0x6f12), 0x0022 },
	{ CCI_REG16(0x6f12), 0x2249 },
	{ CCI_REG16(0x6f12), 0x2248 },
	{ CCI_REG16(0x6f12), 0xfcf7 },
	{ CCI_REG16(0x6f12), 0x85fd },
	{ CCI_REG16(0x6f12), 0x0020 },
	{ CCI_REG16(0x6f12), 0xa081 },
	{ CCI_REG16(0x6f12), 0x6081 },
	{ CCI_REG16(0x6f12), 0x2081 },
	{ CCI_REG16(0x6f12), 0xa071 },
	{ CCI_REG16(0x6f12), 0xf9f7 },
	{ CCI_REG16(0x6f12), 0x95fd },
	{ CCI_REG16(0x6f12), 0xbde8 },
	{ CCI_REG16(0x6f12), 0x1040 },
	{ CCI_REG16(0x6f12), 0xfcf7 },
	{ CCI_REG16(0x6f12), 0xa4bb },
	{ CCI_REG16(0x6f12), 0x2001 },
	{ CCI_REG16(0x6f12), 0x51a1 },
	{ CCI_REG16(0x6f12), 0x0001 },
	{ CCI_REG16(0x6f12), 0x7547 },
	{ CCI_REG16(0x6f12), 0x2001 },
	{ CCI_REG16(0x6f12), 0xce00 },
	{ CCI_REG16(0x6f12), 0x2001 },
	{ CCI_REG16(0x6f12), 0x5301 },
	{ CCI_REG16(0x6f12), 0x0001 },
	{ CCI_REG16(0x6f12), 0x7361 },
	{ CCI_REG16(0x6f12), 0x2001 },
	{ CCI_REG16(0x6f12), 0x54c1 },
	{ CCI_REG16(0x6f12), 0x0001 },
	{ CCI_REG16(0x6f12), 0x276f },
	{ CCI_REG16(0x6f12), 0x2001 },
	{ CCI_REG16(0x6f12), 0x5615 },
	{ CCI_REG16(0x6f12), 0x0001 },
	{ CCI_REG16(0x6f12), 0xe573 },
	{ CCI_REG16(0x6f12), 0x2001 },
	{ CCI_REG16(0x6f12), 0x5697 },
	{ CCI_REG16(0x6f12), 0x0001 },
	{ CCI_REG16(0x6f12), 0xeeb3 },
	{ CCI_REG16(0x6f12), 0x2001 },
	{ CCI_REG16(0x6f12), 0x5fdb },
	{ CCI_REG16(0x6f12), 0x0001 },
	{ CCI_REG16(0x6f12), 0x574d },
	{ CCI_REG16(0x6f12), 0x2001 },
	{ CCI_REG16(0x6f12), 0x6039 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x6f12), 0x243b },
	{ CCI_REG16(0x6f12), 0x2001 },
	{ CCI_REG16(0x6f12), 0x6177 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x6f12), 0x6d83 },
	{ CCI_REG16(0x6f12), 0x2001 },
	{ CCI_REG16(0x6f12), 0x618b },
	{ CCI_REG16(0x6f12), 0x2000 },
	{ CCI_REG16(0x6f12), 0x5d90 },
	{ CCI_REG16(0x6f12), 0x2001 },
	{ CCI_REG16(0x6f12), 0x62bb },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x6f12), 0x0acb },
	{ CCI_REG16(0x6f12), 0x2001 },
	{ CCI_REG16(0x6f12), 0x6349 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x6f12), 0xa89b },
	{ CCI_REG16(0x6f12), 0x2000 },
	{ CCI_REG16(0x6f12), 0x9420 },
	{ CCI_REG16(0x6f12), 0x2001 },
	{ CCI_REG16(0x6f12), 0xf270 },
	{ CCI_REG16(0x6f12), 0x2001 },
	{ CCI_REG16(0x6f12), 0x59b3 },
	{ CCI_REG16(0x6f12), 0x0001 },
	{ CCI_REG16(0x6f12), 0x3325 },
	{ CCI_REG16(0x6f12), 0x2001 },
	{ CCI_REG16(0x6f12), 0x5f3f },
	{ CCI_REG16(0x6f12), 0x0001 },
	{ CCI_REG16(0x6f12), 0xf9cd },
	{ CCI_REG16(0x6004), 0x0000 },
};

static const struct cci_reg_sequence s5kgd1_3264x2448_mode[] = {
	{ CCI_REG16(0x6028), 0x2000 },
	{ CCI_REG16(0x602a), 0x10bc },
	{ CCI_REG16(0x6f12), 0xf9f0 },
	{ CCI_REG16(0x6028), 0x2001 },
	{ CCI_REG16(0x602a), 0xf000 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x6f12), 0x0004 },
	{ CCI_REG16(0x6f12), 0x0001 },
	{ CCI_REG16(0x602a), 0x4b86 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x602a), 0x4de0 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x602a), 0xf1ea },
	{ CCI_REG16(0x6f12), 0x0008 },
	{ CCI_REG16(0x6f12), 0x13c0 },
	{ CCI_REG16(0x6f12), 0x00fd },
	{ CCI_REG16(0x602a), 0xf008 },
	{ CCI_REG16(0x6f12), 0x00e4 },
	{ CCI_REG16(0x602a), 0xf1f0 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x602a), 0xf230 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x602a), 0xf23a },
	{ CCI_REG16(0x6f12), 0x0001 },
	{ CCI_REG16(0x6f12), 0x2000 },
	{ CCI_REG16(0x602a), 0xf1f4 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x602a), 0xf23e },
	{ CCI_REG16(0x6f12), 0x123d },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x6f12), 0x0400 },
	{ CCI_REG16(0x6f12), 0x0400 },
	{ CCI_REG16(0x602a), 0xf228 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x602a), 0xf21c },
	{ CCI_REG16(0x6f12), 0x0001 },
	{ CCI_REG16(0x6f12), 0x0226 },
	{ CCI_REG16(0x6f12), 0x00c8 },
	{ CCI_REG16(0x6f12), 0x00e6 },
	{ CCI_REG16(0x602a), 0xf2a4 },
	{ CCI_REG16(0x6f12), 0x0300 },
	{ CCI_REG16(0x602a), 0xf2a8 },
	{ CCI_REG16(0x6f12), 0x1000 },
	{ CCI_REG16(0x602a), 0xf2ac },
	{ CCI_REG16(0x6f12), 0x0001 },
	{ CCI_REG16(0x6f12), 0x0001 },
	{ CCI_REG16(0x6f12), 0x0400 },
	{ CCI_REG16(0x6f12), 0x0400 },
	{ CCI_REG16(0x6f12), 0x0320 },
	{ CCI_REG16(0x6f12), 0x011b },
	{ CCI_REG16(0x6f12), 0x0064 },
	{ CCI_REG16(0x6f12), 0x0100 },
	{ CCI_REG16(0x6f12), 0x0200 },
	{ CCI_REG16(0x6f12), 0x03d3 },
	{ CCI_REG16(0x6f12), 0x05a7 },
	{ CCI_REG16(0x6f12), 0x0ad3 },
	{ CCI_REG16(0x6f12), 0x0320 },
	{ CCI_REG16(0x6f12), 0x0320 },
	{ CCI_REG16(0x6f12), 0x011b },
	{ CCI_REG16(0x6f12), 0x0064 },
	{ CCI_REG16(0x6f12), 0x0064 },
	{ CCI_REG16(0x6f12), 0x0100 },
	{ CCI_REG16(0x6f12), 0x0200 },
	{ CCI_REG16(0x6f12), 0x1000 },
	{ CCI_REG16(0x6f12), 0x0800 },
	{ CCI_REG16(0x6f12), 0x1000 },
	{ CCI_REG16(0x6f12), 0x1000 },
	{ CCI_REG16(0x602a), 0xce80 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x6f12), 0x1111 },
	{ CCI_REG16(0x6f12), 0x0100 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x6f12), 0x0400 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x6f12), 0x0008 },
	{ CCI_REG16(0x6f12), 0x0008 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x602a), 0xcea4 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x602a), 0xcea2 },
	{ CCI_REG16(0x6f12), 0x0001 },
	{ CCI_REG16(0x602a), 0xcea6 },
	{ CCI_REG16(0x6f12), 0x0001 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x6f12), 0x0400 },
	{ CCI_REG16(0x6f12), 0x0400 },
	{ CCI_REG16(0x6f12), 0x0200 },
	{ CCI_REG16(0x6f12), 0x0100 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x6f12), 0xf800 },
	{ CCI_REG16(0x6028), 0x2000 },
	{ CCI_REG16(0x0344), 0x0018 },
	{ CCI_REG16(0x0348), 0x1997 },
	{ CCI_REG16(0x0346), 0x0010 },
	{ CCI_REG16(0x034a), 0x133f },
	{ CCI_REG16(0x034c), 0x0cc0 },
	{ CCI_REG16(0x034e), 0x0990 },
	{ CCI_REG16(0x0352), 0x0004 },
	{ CCI_REG16(0x602a), 0x12f2 },
	{ CCI_REG16(0x6f12), 0x0d10 },
	{ CCI_REG16(0x6f12), 0x0a18 },
	{ CCI_REG16(0x6f12), 0x19b0 },
	{ CCI_REG16(0x6f12), 0x1350 },
	{ CCI_REG16(0x0bc6), 0x0000 },
	{ CCI_REG16(0x0342), 0x38c0 },
	{ CCI_REG16(0x0340), 0x0a40 },
	{ CCI_REG16(0x0702), 0x0000 },
	{ CCI_REG16(0x0900), 0x0112 },
	{ CCI_REG16(0x0380), 0x0001 },
	{ CCI_REG16(0x0382), 0x0001 },
	{ CCI_REG16(0x0384), 0x0002 },
	{ CCI_REG16(0x0386), 0x0002 },
	{ CCI_REG16(0x602a), 0x20de },
	{ CCI_REG16(0x6f12), 0x0003 },
	{ CCI_REG16(0x6f12), 0x0011 },
	{ CCI_REG16(0x6f12), 0x0022 },
	{ CCI_REG16(0x6f12), 0x0011 },
	{ CCI_REG16(0x6f12), 0x0022 },
	{ CCI_REG16(0x6f12), 0x0011 },
	{ CCI_REG16(0x6f12), 0x0022 },
	{ CCI_REG16(0x6f12), 0x0011 },
	{ CCI_REG16(0x6f12), 0x0022 },
	{ CCI_REG16(0x6f12), 0x0011 },
	{ CCI_REG16(0x6f12), 0x0022 },
	{ CCI_REG16(0x602a), 0x2108 },
	{ CCI_REG16(0x6f12), 0x0022 },
	{ CCI_REG16(0x6f12), 0x0011 },
	{ CCI_REG16(0x6f12), 0x0022 },
	{ CCI_REG16(0x6f12), 0x0011 },
	{ CCI_REG16(0x6f12), 0x0022 },
	{ CCI_REG16(0x6f12), 0x0011 },
	{ CCI_REG16(0x6f12), 0x0022 },
	{ CCI_REG16(0x6f12), 0x0011 },
	{ CCI_REG16(0x6f12), 0x0022 },
	{ CCI_REG16(0x6f12), 0x0011 },
	{ CCI_REG16(0x0402), 0x1010 },
	{ CCI_REG16(0x0404), 0x1000 },
	{ CCI_REG16(0x0400), 0x2010 },
	{ CCI_REG16(0x021e), 0x0000 },
	{ CCI_REG16(0x0202), 0x0100 },
	{ CCI_REG16(0x022c), 0x0100 },
	{ CCI_REG16(0x0226), 0x0100 },
	{ CCI_REG16(0x0bc0), 0x0040 },
	{ CCI_REG16(0x602a), 0x2500 },
	{ CCI_REG16(0x6f12), 0x0080 },
	{ CCI_REG16(0x0204), 0x0080 },
	{ CCI_REG16(0x0136), 0x1800 },
	{ CCI_REG16(0x0304), 0x0004 },
	{ CCI_REG16(0x030c), 0x0000 },
	{ CCI_REG16(0x0306), 0x011e },
	{ CCI_REG16(0x0302), 0x0003 },
	{ CCI_REG16(0x0300), 0x0002 },
	{ CCI_REG16(0x030e), 0x0004 },
	{ CCI_REG16(0x0312), 0x0002 },
	{ CCI_REG16(0x0310), 0x0156 },
	{ CCI_REG16(0x030a), 0x0002 },
	{ CCI_REG16(0x0308), 0x0008 },
	{ CCI_REG16(0x602a), 0x10b8 },
	{ CCI_REG16(0x6f12), 0x0020 },
	{ CCI_REG16(0x602a), 0x1ee0 },
	{ CCI_REG16(0x6f12), 0x0078 },
	{ CCI_REG16(0x602a), 0x1eb6 },
	{ CCI_REG16(0x6f12), 0x0206 },
	{ CCI_REG16(0x602a), 0x2870 },
	{ CCI_REG16(0x6f12), 0x0100 },
	{ CCI_REG16(0x602a), 0x23a0 },
	{ CCI_REG16(0x6f12), 0x0001 },
	{ CCI_REG16(0x0fe8), 0x49c1 },
	{ CCI_REG16(0x602a), 0x3022 },
	{ CCI_REG16(0x6f12), 0x1281 },
	{ CCI_REG16(0x0fea), 0x0040 },
	{ CCI_REG16(0x602a), 0x32e8 },
	{ CCI_REG16(0x6f12), 0x0100 },
	{ CCI_REG16(0x0bc8), 0x0001 },
	{ CCI_REG16(0x0b06), 0x0101 },
	{ CCI_REG16(0x602a), 0x3302 },
	{ CCI_REG16(0x6f12), 0x0100 },
	{ CCI_REG16(0x602a), 0x3304 },
	{ CCI_REG16(0x6f12), 0x0100 },
	{ CCI_REG16(0x0b08), 0x0000 },
	{ CCI_REG16(0x6f12), 0x0001 },
	{ CCI_REG16(0x602a), 0x54a2 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x602a), 0x120e },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x0b00), 0x0080 },
	{ CCI_REG16(0x602a), 0x1212 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x602a), 0x3770 },
	{ CCI_REG16(0x6f12), 0x0100 },
	{ CCI_REG16(0x602a), 0x2860 },
	{ CCI_REG16(0x6f12), 0x0001 },
	{ CCI_REG16(0x602a), 0x3220 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x602a), 0x1226 },
	{ CCI_REG16(0x6f12), 0x0301 },
	{ CCI_REG16(0x0b0a), 0x0101 },
	{ CCI_REG16(0x602a), 0x29c8 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x602a), 0x32ec },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x0fe0), 0x0000 },
	{ CCI_REG16(0x602a), 0x31e4 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x0116), 0x3000 },
	{ CCI_REG16(0x0110), 0x1002 },
	{ CCI_REG16(0x602a), 0x1eb8 },
	{ CCI_REG16(0x6f12), 0x0301 },
	{ CCI_REG16(0x602a), 0x131e },
	{ CCI_REG16(0x6f12), 0x0100 },
	{ CCI_REG16(0x602a), 0x2824 },
	{ CCI_REG16(0x6f12), 0x1fff },
	{ CCI_REG16(0x602a), 0x12be },
	{ CCI_REG16(0x6f12), 0x0101 },
	{ CCI_REG16(0x602a), 0x29c8 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x602a), 0x1146 },
	{ CCI_REG16(0x6f12), 0x25e4 },
	{ CCI_REG16(0x602a), 0x3dea },
	{ CCI_REG16(0x6f12), 0x0081 },
	{ CCI_REG16(0x602a), 0x3034 },
	{ CCI_REG16(0x6f12), 0x049b },
	{ CCI_REG16(0x0902), 0x0000 },
	{ CCI_REG16(0x602a), 0x138e },
	{ CCI_REG16(0x6f12), 0x13c0 },
	{ CCI_REG16(0x602a), 0x1392 },
	{ CCI_REG16(0x6f12), 0x0038 },
	{ CCI_REG16(0x602a), 0x21b6 },
	{ CCI_REG16(0x6f12), 0x0002 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x0d00), 0x0001 },
	{ CCI_REG16(0x0d02), 0x0101 },
	{ CCI_REG16(0x0114), 0x0301 },
	{ CCI_REG16(0x602a), 0x3698 },
	{ CCI_REG16(0x6f12), 0x0104 },
	{ CCI_REG16(0x602a), 0x5e20 },
	{ CCI_REG16(0x6f12), 0x0001 },
	{ CCI_REG16(0x602a), 0x5e24 },
	{ CCI_REG16(0x6f12), 0x1100 },
	{ CCI_REG16(0x602a), 0x5e22 },
	{ CCI_REG16(0x6f12), 0x0100 },
	{ CCI_REG16(0x0d04), 0x0101 },
	{ CCI_REG16(0x602a), 0x36ea },
	{ CCI_REG16(0x6f12), 0x0101 },
	{ CCI_REG16(0x602a), 0x36fa },
	{ CCI_REG16(0x6f12), 0x3311 },
	{ CCI_REG16(0x602a), 0x3702 },
	{ CCI_REG16(0x6f12), 0x2200 },
	{ CCI_REG16(0x602a), 0x370a },
	{ CCI_REG16(0x6f12), 0x6677 },
	{ CCI_REG16(0x602a), 0x3712 },
	{ CCI_REG16(0x6f12), 0x4455 },
	{ CCI_REG16(0x602a), 0x36ac },
	{ CCI_REG16(0x6f12), 0x0b05 },
	{ CCI_REG16(0x602a), 0x36ae },
	{ CCI_REG16(0x6f12), 0x0305 },
	{ CCI_REG16(0x602a), 0x36b4 },
	{ CCI_REG16(0x6f12), 0x0b0d },
	{ CCI_REG16(0x602a), 0x36b6 },
	{ CCI_REG16(0x6f12), 0x030d },
	{ CCI_REG16(0x602a), 0x369e },
	{ CCI_REG16(0x6f12), 0x0204 },
	{ CCI_REG16(0x602a), 0x36a0 },
	{ CCI_REG16(0x6f12), 0x0a0c },
	{ CCI_REG16(0x602a), 0x36a6 },
	{ CCI_REG16(0x6f12), 0x020c },
	{ CCI_REG16(0x602a), 0x36a8 },
	{ CCI_REG16(0x6f12), 0x0a04 },
	{ CCI_REG16(0x6028), 0x2001 },
	{ CCI_REG16(0x602a), 0x5178 },
	{ CCI_REG16(0x6f12), 0x0a0a },
	{ CCI_REG16(0x602a), 0x5180 },
	{ CCI_REG16(0x6f12), 0x0703 },
	{ CCI_REG16(0x602a), 0x5178 },
	{ CCI_REG16(0x6f12), 0x0a0a },
	{ CCI_REG16(0x602a), 0x5180 },
	{ CCI_REG16(0x6f12), 0x0703 },
	{ CCI_REG16(0x602a), 0x517a },
	{ CCI_REG16(0x6f12), 0x0202 },
	{ CCI_REG16(0x602a), 0x5182 },
	{ CCI_REG16(0x6f12), 0x0703 },
	{ CCI_REG16(0x602a), 0x517a },
	{ CCI_REG16(0x6f12), 0x0202 },
	{ CCI_REG16(0x602a), 0x5182 },
	{ CCI_REG16(0x6f12), 0x0703 },
	{ CCI_REG16(0x602a), 0x517c },
	{ CCI_REG16(0x6f12), 0x0404 },
	{ CCI_REG16(0x602a), 0x5184 },
	{ CCI_REG16(0x6f12), 0x0703 },
	{ CCI_REG16(0x602a), 0x517c },
	{ CCI_REG16(0x6f12), 0x0404 },
	{ CCI_REG16(0x602a), 0x5184 },
	{ CCI_REG16(0x6f12), 0x0703 },
	{ CCI_REG16(0x602a), 0x517e },
	{ CCI_REG16(0x6f12), 0x0c0c },
	{ CCI_REG16(0x602a), 0x5186 },
	{ CCI_REG16(0x6f12), 0x0703 },
	{ CCI_REG16(0x602a), 0x517e },
	{ CCI_REG16(0x6f12), 0x0c0c },
	{ CCI_REG16(0x602a), 0x5186 },
	{ CCI_REG16(0x6f12), 0x0703 },
	{ CCI_REG16(0x6028), 0x2000 },
	{ CCI_REG16(0x602a), 0x3732 },
	{ CCI_REG16(0x6f12), 0x0100 },
	{ CCI_REG16(0x602a), 0x375c },
	{ CCI_REG16(0x6f12), 0x0080 },
	{ CCI_REG16(0x6f12), 0x0080 },
	{ CCI_REG16(0x6f12), 0x07c0 },
	{ CCI_REG16(0x6f12), 0x07c0 },
	{ CCI_REG16(0x6f12), 0x0449 },
	{ CCI_REG16(0x6f12), 0x0449 },
	{ CCI_REG16(0x602a), 0x376a },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0xf488), 0x01c6 },
	{ CCI_REG16(0xf48a), 0x01d6 },
	{ CCI_REG16(0xf48c), 0x01e6 },
	{ CCI_REG16(0xf48e), 0x01f6 },
	{ CCI_REG16(0xf446), 0x000c },
	{ CCI_REG16(0xf448), 0x0018 },
	{ CCI_REG16(0xf450), 0x0010 },
	{ CCI_REG16(0xf44e), 0x0000 },
	{ CCI_REG16(0x602a), 0x250a },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0xf468), 0xe000 },
	{ CCI_REG16(0x602a), 0x21be },
	{ CCI_REG16(0x6f12), 0x04d2 },
	{ CCI_REG16(0x6f12), 0x41a6 },
	{ CCI_REG16(0x602a), 0x1230 },
	{ CCI_REG16(0x6f12), 0x0101 },
	{ CCI_REG16(0x602a), 0x1232 },
	{ CCI_REG16(0x6f12), 0x00f0 },
	{ CCI_REG16(0x602a), 0x1236 },
	{ CCI_REG16(0x6f12), 0x01ff },
	{ CCI_REG16(0x602a), 0x123a },
	{ CCI_REG16(0x6f12), 0x0004 },
	{ CCI_REG16(0x6f12), 0x0009 },
	{ CCI_REG16(0x6f12), 0xf45a },
	{ CCI_REG16(0xf466), 0x0e0d },
	{ CCI_REG16(0x602a), 0x1ee2 },
	{ CCI_REG16(0x6f12), 0x19cd },
	{ CCI_REG16(0x602a), 0x1ee6 },
	{ CCI_REG16(0x6f12), 0x030f },
	{ CCI_REG16(0x602a), 0x115e },
	{ CCI_REG16(0x6f12), 0x0048 },
	{ CCI_REG16(0x602a), 0x11a6 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x602a), 0x11a8 },
	{ CCI_REG16(0x6f12), 0x0004 },
	{ CCI_REG16(0x602a), 0x11ae },
	{ CCI_REG16(0x6f12), 0x0003 },
	{ CCI_REG16(0x602a), 0x131c },
	{ CCI_REG16(0x6f12), 0x2400 },
	{ CCI_REG16(0x602a), 0x2872 },
	{ CCI_REG16(0x6f12), 0x0001 },
	{ CCI_REG16(0x602a), 0x13fc },
	{ CCI_REG16(0x6f12), 0x0044 },
	{ CCI_REG16(0x6f12), 0x0064 },
	{ CCI_REG16(0x602a), 0x1edc },
	{ CCI_REG16(0x6f12), 0x5008 },
	{ CCI_REG16(0x602a), 0x145e },
	{ CCI_REG16(0x6f12), 0x0049 },
	{ CCI_REG16(0x602a), 0x1410 },
	{ CCI_REG16(0x6f12), 0x0001 },
	{ CCI_REG16(0x602a), 0x1ec8 },
	{ CCI_REG16(0x6f12), 0x0503 },
	{ CCI_REG16(0x602a), 0x1eca },
	{ CCI_REG16(0x6f12), 0x0504 },
	{ CCI_REG16(0x602a), 0x1ed0 },
	{ CCI_REG16(0x6f12), 0x040f },
	{ CCI_REG16(0x602a), 0x145c },
	{ CCI_REG16(0x6f12), 0x0035 },
	{ CCI_REG16(0x602a), 0x140e },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x602a), 0x2550 },
	{ CCI_REG16(0x6f12), 0x193c },
	{ CCI_REG16(0x6028), 0x2000 },
	{ CCI_REG16(0x602a), 0x303a },
	{ CCI_REG16(0x6f12), 0x003a },
	{ CCI_REG16(0x6f12), 0x0033 },
	{ CCI_REG16(0x602a), 0x3042 },
	{ CCI_REG16(0x6f12), 0x0014 },
	{ CCI_REG16(0x6f12), 0x000e },
	{ CCI_REG16(0x602a), 0x304a },
	{ CCI_REG16(0x6f12), 0x0110 },
	{ CCI_REG16(0x6f12), 0x011c },
	{ CCI_REG16(0x6f12), 0x012c },
	{ CCI_REG16(0x6f12), 0x013c },
	{ CCI_REG16(0x6f12), 0x0150 },
	{ CCI_REG16(0x6f12), 0x0168 },
	{ CCI_REG16(0x6f12), 0x0180 },
	{ CCI_REG16(0x6f12), 0x019c },
	{ CCI_REG16(0x6f12), 0x01bc },
	{ CCI_REG16(0x6f12), 0x01e4 },
	{ CCI_REG16(0x602a), 0x3062 },
	{ CCI_REG16(0x6f12), 0x012c },
	{ CCI_REG16(0x6f12), 0x0144 },
	{ CCI_REG16(0x6f12), 0x0160 },
	{ CCI_REG16(0x6f12), 0x0180 },
	{ CCI_REG16(0x6f12), 0x01a8 },
	{ CCI_REG16(0x6f12), 0x01d8 },
	{ CCI_REG16(0x6f12), 0x0214 },
	{ CCI_REG16(0x602a), 0x3014 },
	{ CCI_REG16(0x6f12), 0x123d },
	{ CCI_REG16(0x602a), 0x2eaa },
	{ CCI_REG16(0x6f12), 0x0200 },
	{ CCI_REG16(0x6028), 0x2001 },
	{ CCI_REG16(0x602a), 0x4b84 },
	{ CCI_REG16(0x6f12), 0x0200 },
	{ CCI_REG16(0x6028), 0x2000 },
	{ CCI_REG16(0x602a), 0x3778 },
	{ CCI_REG16(0x6f12), 0x0101 },
	{ CCI_REG16(0x602a), 0x37fc },
	{ CCI_REG16(0x6f12), 0x0001 },
	{ CCI_REG16(0x602a), 0x4bfc },
	{ CCI_REG16(0x6f12), 0x9696 },
	{ CCI_REG16(0x6f12), 0x9696 },
	{ CCI_REG16(0x6f12), 0x9696 },
	{ CCI_REG16(0x602a), 0x465c },
	{ CCI_REG16(0x6f12), 0x0505 },
	{ CCI_REG16(0x6f12), 0x0505 },
	{ CCI_REG16(0x6f12), 0x0505 },
	{ CCI_REG16(0x602a), 0x4652 },
	{ CCI_REG16(0x6f12), 0x1032 },
	{ CCI_REG16(0x6f12), 0x3232 },
	{ CCI_REG16(0x6f12), 0x3232 },
	{ CCI_REG16(0x6f12), 0x3200 },
	{ CCI_REG16(0x602a), 0x466e },
	{ CCI_REG16(0x6f12), 0x0404 },
	{ CCI_REG16(0x6f12), 0x0404 },
	{ CCI_REG16(0x6f12), 0x0404 },
	{ CCI_REG16(0x602a), 0x469a },
	{ CCI_REG16(0x6f12), 0x1032 },
	{ CCI_REG16(0x6f12), 0x3232 },
	{ CCI_REG16(0x6f12), 0x3232 },
	{ CCI_REG16(0x6f12), 0x3200 },
	{ CCI_REG16(0x602a), 0x46ac },
	{ CCI_REG16(0x6f12), 0x1027 },
	{ CCI_REG16(0x6f12), 0x2727 },
	{ CCI_REG16(0x6f12), 0x2727 },
	{ CCI_REG16(0x6f12), 0x2700 },
	{ CCI_REG16(0x602a), 0x4676 },
	{ CCI_REG16(0x6f12), 0x1014 },
	{ CCI_REG16(0x6f12), 0x1414 },
	{ CCI_REG16(0x6f12), 0x1414 },
	{ CCI_REG16(0x6f12), 0x1400 },
	{ CCI_REG16(0x602a), 0x4688 },
	{ CCI_REG16(0x6f12), 0x1027 },
	{ CCI_REG16(0x6f12), 0x2727 },
	{ CCI_REG16(0x6f12), 0x2727 },
	{ CCI_REG16(0x6f12), 0x2700 },
	{ CCI_REG16(0x602a), 0x4c0e },
	{ CCI_REG16(0x6f12), 0x3232 },
	{ CCI_REG16(0x6f12), 0x3232 },
	{ CCI_REG16(0x6f12), 0x3232 },
	{ CCI_REG16(0x602a), 0x3b1e },
	{ CCI_REG16(0x6f12), 0x0078 },
	{ CCI_REG16(0x602a), 0x4c20 },
	{ CCI_REG16(0x6f12), 0x8080 },
	{ CCI_REG16(0x6f12), 0x8080 },
	{ CCI_REG16(0x6f12), 0x8080 },
	{ CCI_REG16(0x602a), 0x3b12 },
	{ CCI_REG16(0x6f12), 0x0005 },
	{ CCI_REG16(0x602a), 0x3af2 },
	{ CCI_REG16(0x6f12), 0x0007 },
	{ CCI_REG16(0x602a), 0x3af6 },
	{ CCI_REG16(0x6f12), 0x000a },
	{ CCI_REG16(0x602a), 0x3afa },
	{ CCI_REG16(0x6f12), 0x000c },
	{ CCI_REG16(0x602a), 0x3afe },
	{ CCI_REG16(0x6f12), 0x0032 },
	{ CCI_REG16(0x602a), 0x3b02 },
	{ CCI_REG16(0x6f12), 0x0039 },
	{ CCI_REG16(0x602a), 0x3b06 },
	{ CCI_REG16(0x6f12), 0x0037 },
	{ CCI_REG16(0x602a), 0x46be },
	{ CCI_REG16(0x6f12), 0x107d },
	{ CCI_REG16(0x6f12), 0x7d7d },
	{ CCI_REG16(0x6f12), 0x7d7d },
	{ CCI_REG16(0x6f12), 0x7d00 },
	{ CCI_REG16(0x602a), 0x46c8 },
	{ CCI_REG16(0x6f12), 0xafaf },
	{ CCI_REG16(0x6f12), 0xafaf },
	{ CCI_REG16(0x6f12), 0xafaf },
	{ CCI_REG16(0x602a), 0x3b2e },
	{ CCI_REG16(0x6f12), 0x0005 },
	{ CCI_REG16(0x602a), 0x3b32 },
	{ CCI_REG16(0x6f12), 0x0074 },
	{ CCI_REG16(0x602a), 0x4c28 },
	{ CCI_REG16(0x6f12), 0x1080 },
	{ CCI_REG16(0x6f12), 0x8080 },
	{ CCI_REG16(0x6f12), 0x8080 },
	{ CCI_REG16(0x6f12), 0x8000 },
	{ CCI_REG16(0x602a), 0x4c32 },
	{ CCI_REG16(0x6f12), 0x8080 },
	{ CCI_REG16(0x6f12), 0x8080 },
	{ CCI_REG16(0x6f12), 0x8080 },
	{ CCI_REG16(0x602a), 0x4c3a },
	{ CCI_REG16(0x6f12), 0x1009 },
	{ CCI_REG16(0x6f12), 0x0909 },
	{ CCI_REG16(0x6f12), 0x0909 },
	{ CCI_REG16(0x6f12), 0x0900 },
	{ CCI_REG16(0x602a), 0x4c44 },
	{ CCI_REG16(0x6f12), 0x0909 },
	{ CCI_REG16(0x6f12), 0x0909 },
	{ CCI_REG16(0x6f12), 0x0909 },
	{ CCI_REG16(0x602a), 0x4c4c },
	{ CCI_REG16(0x6f12), 0x1004 },
	{ CCI_REG16(0x6f12), 0x0404 },
	{ CCI_REG16(0x6f12), 0x0404 },
	{ CCI_REG16(0x6f12), 0x0400 },
	{ CCI_REG16(0x602a), 0x4c56 },
	{ CCI_REG16(0x6f12), 0x0404 },
	{ CCI_REG16(0x6f12), 0x0404 },
	{ CCI_REG16(0x6f12), 0x0404 },
	{ CCI_REG16(0x602a), 0x3e06 },
	{ CCI_REG16(0x6f12), 0x000a },
	{ CCI_REG16(0x602a), 0x3e0a },
	{ CCI_REG16(0x6f12), 0x000a },
	{ CCI_REG16(0x602a), 0x3e2e },
	{ CCI_REG16(0x6f12), 0x000a },
	{ CCI_REG16(0x602a), 0x37fe },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x602a), 0x3e0e },
	{ CCI_REG16(0x6f12), 0x00af },
	{ CCI_REG16(0x602a), 0x3e12 },
	{ CCI_REG16(0x6f12), 0x00af },
	{ CCI_REG16(0x602a), 0x3e16 },
	{ CCI_REG16(0x6f12), 0x0004 },
	{ CCI_REG16(0x602a), 0x3e1a },
	{ CCI_REG16(0x6f12), 0x0004 },
	{ CCI_REG16(0x602a), 0x3e1e },
	{ CCI_REG16(0x6f12), 0x0004 },
	{ CCI_REG16(0x602a), 0x3e22 },
	{ CCI_REG16(0x6f12), 0x0004 },
	{ CCI_REG16(0x602a), 0x3e26 },
	{ CCI_REG16(0x6f12), 0x0004 },
	{ CCI_REG16(0x602a), 0x3e2a },
	{ CCI_REG16(0x6f12), 0x0004 },
	{ CCI_REG16(0x602a), 0x3cb2 },
	{ CCI_REG16(0x6f12), 0x0001 },
	{ CCI_REG16(0x602a), 0x3ba2 },
	{ CCI_REG16(0x6f12), 0x0001 },
	{ CCI_REG16(0x602a), 0x4c5e },
	{ CCI_REG16(0x6f12), 0x4035 },
	{ CCI_REG16(0x6f12), 0x3535 },
	{ CCI_REG16(0x6f12), 0x3535 },
	{ CCI_REG16(0x6f12), 0x3501 },
	{ CCI_REG16(0x602a), 0x4c68 },
	{ CCI_REG16(0x6f12), 0x3535 },
	{ CCI_REG16(0x6f12), 0x3535 },
	{ CCI_REG16(0x6f12), 0x3535 },
	{ CCI_REG16(0x602a), 0x4c70 },
	{ CCI_REG16(0x6f12), 0x40c8 },
	{ CCI_REG16(0x6f12), 0xc8c8 },
	{ CCI_REG16(0x6f12), 0xc8c8 },
	{ CCI_REG16(0x6f12), 0xc801 },
	{ CCI_REG16(0x602a), 0x4c7a },
	{ CCI_REG16(0x6f12), 0xc8c8 },
	{ CCI_REG16(0x6f12), 0xc8c8 },
	{ CCI_REG16(0x602a), 0x4c82 },
	{ CCI_REG16(0x6f12), 0x4085 },
	{ CCI_REG16(0x6f12), 0x8585 },
	{ CCI_REG16(0x6f12), 0x8585 },
	{ CCI_REG16(0x6f12), 0x8501 },
	{ CCI_REG16(0x602a), 0x4c8c },
	{ CCI_REG16(0x6f12), 0x8585 },
	{ CCI_REG16(0x6f12), 0x8585 },
	{ CCI_REG16(0x6f12), 0x8585 },
	{ CCI_REG16(0x602a), 0x4c94 },
	{ CCI_REG16(0x6f12), 0x4085 },
	{ CCI_REG16(0x6f12), 0x8585 },
	{ CCI_REG16(0x6f12), 0x8585 },
	{ CCI_REG16(0x6f12), 0x8501 },
	{ CCI_REG16(0x602a), 0x4c9e },
	{ CCI_REG16(0x6f12), 0x3232 },
	{ CCI_REG16(0x6f12), 0x3232 },
	{ CCI_REG16(0x6f12), 0x3232 },
	{ CCI_REG16(0x602a), 0x4ca6 },
	{ CCI_REG16(0x6f12), 0x4032 },
	{ CCI_REG16(0x6f12), 0x3232 },
	{ CCI_REG16(0x6f12), 0x3232 },
	{ CCI_REG16(0x6f12), 0x3201 },
	{ CCI_REG16(0x602a), 0x4cb0 },
	{ CCI_REG16(0x6f12), 0x2121 },
	{ CCI_REG16(0x6f12), 0x2121 },
	{ CCI_REG16(0x6f12), 0x2121 },
	{ CCI_REG16(0x602a), 0x4cb8 },
	{ CCI_REG16(0x6f12), 0x4021 },
	{ CCI_REG16(0x6f12), 0x2121 },
	{ CCI_REG16(0x6f12), 0x2121 },
	{ CCI_REG16(0x6f12), 0x2101 },
	{ CCI_REG16(0x602a), 0x4cc2 },
	{ CCI_REG16(0x6f12), 0x2121 },
	{ CCI_REG16(0x6f12), 0x2121 },
	{ CCI_REG16(0x6f12), 0x2121 },
	{ CCI_REG16(0x602a), 0x3da6 },
	{ CCI_REG16(0x6f12), 0x0064 },
	{ CCI_REG16(0x602a), 0x3daa },
	{ CCI_REG16(0x6f12), 0x0032 },
	{ CCI_REG16(0x602a), 0x3db0 },
	{ CCI_REG16(0x6f12), 0x01ab },
	{ CCI_REG16(0x6f12), 0x0000 },
	{ CCI_REG16(0x6f12), 0x01ac },
	{ CCI_REG16(0x6f12), 0x0050 },
	{ CCI_REG16(0x6f12), 0x01ad },
	{ CCI_REG16(0x6f12), 0x0064 },
	{ CCI_REG16(0x6f12), 0x01ae },
	{ CCI_REG16(0x6f12), 0x0064 },
	{ CCI_REG16(0x6f12), 0x01af },
	{ CCI_REG16(0x6f12), 0x00c8 },
	{ CCI_REG16(0x6f12), 0x01b0 },
	{ CCI_REG16(0x6f12), 0x00c8 },
	{ CCI_REG16(0x602a), 0x3dd4 },
	{ CCI_REG16(0x6f12), 0x01b4 },
	{ CCI_REG16(0x6f12), 0x0032 },
	{ CCI_REG16(0x6f12), 0x01b5 },
	{ CCI_REG16(0x6f12), 0x0050 },
	{ CCI_REG16(0x6f12), 0x01b6 },
	{ CCI_REG16(0x6f12), 0x0050 },
	{ CCI_REG16(0x6f12), 0x01b7 },
	{ CCI_REG16(0x6f12), 0x00c8 },
	{ CCI_REG16(0x6f12), 0x01b8 },
	{ CCI_REG16(0x6f12), 0x00c8 },
	{ CCI_REG16(0x6f12), 0x01b9 },
	{ CCI_REG16(0x6f12), 0x0081 },
	{ CCI_REG16(0xf452), 0x0018 },
	{ CCI_REG16(0x602a), 0x3b22 },
	{ CCI_REG16(0x6f12), 0x0001 },
	{ CCI_REG16(0x602a), 0x27d2 },
	{ CCI_REG16(0x6f12), 0x0203 },
};

static const struct s5kgd1_mode s5kgd1_supported_modes[] = {
	{
		.width = 3264,
		.height = 2448,
		.hts = 0x38c0,
		.vts = 0x0a40,
		.exposure = 256,
		.reg_list = {
			.regs = s5kgd1_3264x2448_mode,
			.num_regs = ARRAY_SIZE(s5kgd1_3264x2448_mode),
		},
	},
};

static int s5kgd1_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct s5kgd1 *s5kgd1 = container_of(ctrl->handler, struct s5kgd1,
					     ctrl_handler);
	const struct s5kgd1_mode *mode = s5kgd1->mode;
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
		exposure_max = mode->height + ctrl->val - S5KGD1_EXPOSURE_MARGIN;
		__v4l2_ctrl_modify_range(s5kgd1->exposure,
					 s5kgd1->exposure->minimum,
					 exposure_max,
					 s5kgd1->exposure->step,
					 s5kgd1->exposure->default_value);
		break;
	}

	/* V4L2 controls are applied, when sensor is powered up for streaming */
	if (!pm_runtime_get_if_active(s5kgd1->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		ret = cci_write(s5kgd1->regmap, S5KGD1_REG_AGAIN,
				ctrl->val, NULL);
		break;
	case V4L2_CID_EXPOSURE:
		ret = cci_write(s5kgd1->regmap, S5KGD1_REG_EXPOSURE,
				ctrl->val, NULL);
		break;
	case V4L2_CID_VBLANK:
		ret = cci_write(s5kgd1->regmap, S5KGD1_REG_VTS,
				ctrl->val + mode->height, NULL);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = cci_write(s5kgd1->regmap, S5KGD1_REG_TEST_PATTERN,
				ctrl->val, NULL);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	pm_runtime_put(s5kgd1->dev);

	return ret;
}

static const struct v4l2_ctrl_ops s5kgd1_ctrl_ops = {
	.s_ctrl = s5kgd1_set_ctrl,
};

static inline u64 s5kgd1_freq_to_pixel_rate(const u64 freq)
{
	return div_u64(freq * 2 * S5KGD1_DATA_LANES, 10);
}

static int s5kgd1_init_controls(struct s5kgd1 *s5kgd1)
{
	struct v4l2_ctrl_handler *ctrl_hdlr = &s5kgd1->ctrl_handler;
	const struct s5kgd1_mode *mode = s5kgd1->mode;
	s64 pixel_rate, hblank, vblank, exposure_max;
	struct v4l2_fwnode_device_properties props;
	int ret;

	v4l2_ctrl_handler_init(ctrl_hdlr, 9);

	s5kgd1->link_freq = v4l2_ctrl_new_int_menu(ctrl_hdlr, &s5kgd1_ctrl_ops,
					V4L2_CID_LINK_FREQ,
					ARRAY_SIZE(s5kgd1_link_freq_menu) - 1,
					0, s5kgd1_link_freq_menu);
	if (s5kgd1->link_freq)
		s5kgd1->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	pixel_rate = s5kgd1_freq_to_pixel_rate(s5kgd1_link_freq_menu[0]);
	s5kgd1->pixel_rate = v4l2_ctrl_new_std(ctrl_hdlr, &s5kgd1_ctrl_ops,
					       V4L2_CID_PIXEL_RATE,
					       0, pixel_rate, 1, pixel_rate);

	hblank = mode->hts - mode->width;
	s5kgd1->hblank = v4l2_ctrl_new_std(ctrl_hdlr, &s5kgd1_ctrl_ops,
					   V4L2_CID_HBLANK, hblank,
					   hblank, 1, hblank);
	if (s5kgd1->hblank)
		s5kgd1->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank = mode->vts - mode->height;
	s5kgd1->vblank = v4l2_ctrl_new_std(ctrl_hdlr, &s5kgd1_ctrl_ops,
					   V4L2_CID_VBLANK, vblank,
					   S5KGD1_VTS_MAX - mode->height, 1,
					   vblank);

	v4l2_ctrl_new_std(ctrl_hdlr, &s5kgd1_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  S5KGD1_AGAIN_MIN, S5KGD1_AGAIN_MAX,
			  S5KGD1_AGAIN_STEP, S5KGD1_AGAIN_DEFAULT);

	exposure_max = mode->vts - S5KGD1_EXPOSURE_MARGIN;
	s5kgd1->exposure = v4l2_ctrl_new_std(ctrl_hdlr, &s5kgd1_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     S5KGD1_EXPOSURE_MIN,
					     exposure_max,
					     S5KGD1_EXPOSURE_STEP,
					     mode->exposure);

	v4l2_ctrl_new_std_menu_items(ctrl_hdlr, &s5kgd1_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(s5kgd1_test_pattern_menu) - 1,
				     0, 0, s5kgd1_test_pattern_menu);

	s5kgd1->hflip = v4l2_ctrl_new_std(ctrl_hdlr, &s5kgd1_ctrl_ops,
					  V4L2_CID_HFLIP, 0, 1, 1, 0);
	if (s5kgd1->hflip)
		s5kgd1->hflip->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;

	s5kgd1->vflip = v4l2_ctrl_new_std(ctrl_hdlr, &s5kgd1_ctrl_ops,
					  V4L2_CID_VFLIP, 0, 1, 1, 0);
	if (s5kgd1->vflip)
		s5kgd1->vflip->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;

	ret = v4l2_fwnode_device_parse(s5kgd1->dev, &props);
	if (ret)
		goto error_free_hdlr;

	ret = v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &s5kgd1_ctrl_ops,
					      &props);
	if (ret)
		goto error_free_hdlr;

	s5kgd1->sd.ctrl_handler = ctrl_hdlr;

	return 0;

error_free_hdlr:
	v4l2_ctrl_handler_free(ctrl_hdlr);

	return ret;
}

static int s5kgd1_enable_streams(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state, u32 pad,
				 u64 streams_mask)
{
	struct s5kgd1 *s5kgd1 = to_s5kgd1(sd);
	const struct s5kgd1_reg_list *reg_list = &s5kgd1->mode->reg_list;
	int ret;

	ret = pm_runtime_resume_and_get(s5kgd1->dev);
	if (ret)
		return ret;

	/*
	 * Route a shared CSI PHY to this sensor when it starts streaming (Galaxy A70: front and
	 * ultra wide cameras share CSIPHY1 through a switch); both sensors may be powered.
	 */
	if (s5kgd1->mipi_sel_gpio)
		gpiod_direction_output_raw(s5kgd1->mipi_sel_gpio, s5kgd1->mipi_sel_value);

	/* clock/analog setup, then wait as the vendor sequence does */
	cci_multi_reg_write(s5kgd1->regmap, init_array_setting,
			    ARRAY_SIZE(init_array_setting), &ret);
	if (ret)
		goto error;

	usleep_range(4 * USEC_PER_MSEC, 5 * USEC_PER_MSEC);

	cci_multi_reg_write(s5kgd1->regmap, tnp_array_setting,
			    ARRAY_SIZE(tnp_array_setting), &ret);
	cci_multi_reg_write(s5kgd1->regmap, reg_list->regs,
			    reg_list->num_regs, &ret);
	if (ret)
		goto error;

	ret = __v4l2_ctrl_handler_setup(s5kgd1->sd.ctrl_handler);

	cci_write(s5kgd1->regmap, S5KGD1_REG_CTRL_MODE,
		  S5KGD1_MODE_STREAMING |
		  (s5kgd1->vflip->val ? S5KGD1_VFLIP : 0) |
		  (s5kgd1->hflip->val ? S5KGD1_HFLIP : 0), &ret);
	if (ret)
		goto error;

	return 0;

error:
	dev_err(s5kgd1->dev, "failed to start streaming: %d\n", ret);
	pm_runtime_put_autosuspend(s5kgd1->dev);

	return ret;
}

static int s5kgd1_disable_streams(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state, u32 pad,
				  u64 streams_mask)
{
	struct s5kgd1 *s5kgd1 = to_s5kgd1(sd);
	int ret;

	ret = cci_write(s5kgd1->regmap, S5KGD1_REG_CTRL_MODE, 0, NULL);
	if (ret)
		dev_err(s5kgd1->dev, "failed to stop streaming: %d\n", ret);

	pm_runtime_put_autosuspend(s5kgd1->dev);

	return ret;
}

static u32 s5kgd1_get_format_code(struct s5kgd1 *s5kgd1)
{
	unsigned int i;

	i = (s5kgd1->vflip->val ? 2 : 0) | (s5kgd1->hflip->val ? 1 : 0);

	return s5kgd1_mbus_formats[i];
}

static void s5kgd1_update_pad_format(struct s5kgd1 *s5kgd1,
				     const struct s5kgd1_mode *mode,
				     struct v4l2_mbus_framefmt *fmt)
{
	fmt->code = s5kgd1_get_format_code(s5kgd1);
	fmt->width = mode->width;
	fmt->height = mode->height;
	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = V4L2_COLORSPACE_SRGB;
	fmt->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	fmt->quantization = V4L2_QUANTIZATION_FULL_RANGE;
	fmt->xfer_func = V4L2_XFER_FUNC_NONE;
}

static int s5kgd1_set_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state,
				 struct v4l2_subdev_format *fmt)
{
	struct s5kgd1 *s5kgd1 = to_s5kgd1(sd);
	s64 hblank, vblank, exposure_max;
	const struct s5kgd1_mode *mode;

	mode = v4l2_find_nearest_size(s5kgd1_supported_modes,
				      ARRAY_SIZE(s5kgd1_supported_modes),
				      width, height,
				      fmt->format.width, fmt->format.height);

	s5kgd1_update_pad_format(s5kgd1, mode, &fmt->format);

	/* Format code could be updated with respect to flip controls */
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY || s5kgd1->mode == mode)
		goto set_format;

	/* Update limits and set FPS and exposure to default values */
	hblank = mode->hts - mode->width;
	__v4l2_ctrl_modify_range(s5kgd1->hblank, hblank, hblank, 1, hblank);

	vblank = mode->vts - mode->height;
	__v4l2_ctrl_modify_range(s5kgd1->vblank, vblank,
				 S5KGD1_VTS_MAX - mode->height, 1, vblank);
	__v4l2_ctrl_s_ctrl(s5kgd1->vblank, vblank);

	exposure_max = mode->vts - S5KGD1_EXPOSURE_MARGIN;
	__v4l2_ctrl_modify_range(s5kgd1->exposure, S5KGD1_EXPOSURE_MIN,
				 exposure_max, S5KGD1_EXPOSURE_STEP,
				 mode->exposure);
	__v4l2_ctrl_s_ctrl(s5kgd1->exposure, mode->exposure);

	if (s5kgd1->sd.ctrl_handler->error)
		return s5kgd1->sd.ctrl_handler->error;

	s5kgd1->mode = mode;

set_format:
	*v4l2_subdev_state_get_format(state, 0) = fmt->format;

	return 0;
}

static int s5kgd1_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct s5kgd1 *s5kgd1 = to_s5kgd1(sd);

	/* Media bus code index is constant, but code formats are not */
	if (code->index > 0)
		return -EINVAL;

	code->code = s5kgd1_get_format_code(s5kgd1);

	return 0;
}

static int s5kgd1_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	struct s5kgd1 *s5kgd1 = to_s5kgd1(sd);

	if (fse->index >= ARRAY_SIZE(s5kgd1_supported_modes))
		return -EINVAL;

	if (fse->code != s5kgd1_get_format_code(s5kgd1))
		return -EINVAL;

	fse->min_width = s5kgd1_supported_modes[fse->index].width;
	fse->max_width = fse->min_width;
	fse->min_height = s5kgd1_supported_modes[fse->index].height;
	fse->max_height = fse->min_height;

	return 0;
}

static int s5kgd1_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_selection *sel)
{
	struct s5kgd1 *s5kgd1 = to_s5kgd1(sd);

	if (sel->which != V4L2_SUBDEV_FORMAT_ACTIVE)
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = s5kgd1->mode->width;
		sel->r.height = s5kgd1->mode->height;
		return 0;
	default:
		return -EINVAL;
	}

	return 0;
}

static int s5kgd1_init_state(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *state)
{
	struct s5kgd1 *s5kgd1 = to_s5kgd1(sd);
	struct v4l2_subdev_format fmt = {
		.which = V4L2_SUBDEV_FORMAT_TRY,
		.pad = 0,
		.format = {
			/* Media bus code depends on current flip controls */
			.width = s5kgd1->mode->width,
			.height = s5kgd1->mode->height,
		},
	};

	s5kgd1_set_pad_format(sd, state, &fmt);

	return 0;
}

static const struct v4l2_subdev_video_ops s5kgd1_video_ops = {
	.s_stream = v4l2_subdev_s_stream_helper,
};

static const struct v4l2_subdev_pad_ops s5kgd1_pad_ops = {
	.set_fmt = s5kgd1_set_pad_format,
	.get_fmt = v4l2_subdev_get_fmt,
	.get_selection = s5kgd1_get_selection,
	.enum_mbus_code = s5kgd1_enum_mbus_code,
	.enum_frame_size = s5kgd1_enum_frame_size,
	.enable_streams = s5kgd1_enable_streams,
	.disable_streams = s5kgd1_disable_streams,
};

static const struct v4l2_subdev_ops s5kgd1_subdev_ops = {
	.video = &s5kgd1_video_ops,
	.pad = &s5kgd1_pad_ops,
};

static const struct v4l2_subdev_internal_ops s5kgd1_internal_ops = {
	.init_state = s5kgd1_init_state,
};

static const struct media_entity_operations s5kgd1_subdev_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static int s5kgd1_identify_sensor(struct s5kgd1 *s5kgd1)
{
	u64 val;
	int ret;

	ret = cci_read(s5kgd1->regmap, S5KGD1_REG_CHIP_ID, &val, NULL);
	if (ret) {
		dev_err(s5kgd1->dev, "failed to read chip id: %d\n", ret);
		return ret;
	}

	if (val != S5KGD1_CHIP_ID) {
		dev_err(s5kgd1->dev, "chip id mismatch: %x!=%llx\n",
			S5KGD1_CHIP_ID, val);
		return -ENODEV;
	}

	return 0;
}

static int s5kgd1_check_hwcfg(struct s5kgd1 *s5kgd1)
{
	struct fwnode_handle *fwnode = dev_fwnode(s5kgd1->dev), *ep;
	struct v4l2_fwnode_endpoint bus_cfg = {
		.bus = {
			.mipi_csi2 = {
				.num_data_lanes = S5KGD1_DATA_LANES,
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

	if (bus_cfg.bus.mipi_csi2.num_data_lanes != S5KGD1_DATA_LANES) {
		dev_err(s5kgd1->dev, "Invalid number of data lanes: %u\n",
			bus_cfg.bus.mipi_csi2.num_data_lanes);
		ret = -EINVAL;
		goto endpoint_free;
	}

	ret = v4l2_link_freq_to_bitmap(s5kgd1->dev, bus_cfg.link_frequencies,
				       bus_cfg.nr_of_link_frequencies,
				       s5kgd1_link_freq_menu,
				       ARRAY_SIZE(s5kgd1_link_freq_menu),
				       &freq_bitmap);

endpoint_free:
	v4l2_fwnode_endpoint_free(&bus_cfg);

	return ret;
}

static int s5kgd1_power_on(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct s5kgd1 *s5kgd1 = to_s5kgd1(sd);
	int ret;

	ret = regulator_bulk_enable(S5KGD1_NUM_SUPPLIES, s5kgd1->supplies);
	if (ret)
		return ret;

	ret = clk_prepare_enable(s5kgd1->mclk);
	if (ret)
		goto disable_regulators;

	gpiod_set_value_cansleep(s5kgd1->reset_gpio, 0);
	usleep_range(10 * USEC_PER_MSEC, 15 * USEC_PER_MSEC);

	return 0;

disable_regulators:
	regulator_bulk_disable(S5KGD1_NUM_SUPPLIES, s5kgd1->supplies);

	return ret;
}

static int s5kgd1_power_off(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct s5kgd1 *s5kgd1 = to_s5kgd1(sd);

	gpiod_set_value_cansleep(s5kgd1->reset_gpio, 1);

	clk_disable_unprepare(s5kgd1->mclk);

	regulator_bulk_disable(S5KGD1_NUM_SUPPLIES, s5kgd1->supplies);

	return 0;
}

static int s5kgd1_probe(struct i2c_client *client)
{
	struct s5kgd1 *s5kgd1;
	unsigned long freq;
	unsigned int i;
	int ret;

	s5kgd1 = devm_kzalloc(&client->dev, sizeof(*s5kgd1), GFP_KERNEL);
	if (!s5kgd1)
		return -ENOMEM;

	s5kgd1->dev = &client->dev;
	v4l2_i2c_subdev_init(&s5kgd1->sd, client, &s5kgd1_subdev_ops);

	s5kgd1->regmap = devm_cci_regmap_init_i2c(client, 16);
	if (IS_ERR(s5kgd1->regmap))
		return dev_err_probe(s5kgd1->dev, PTR_ERR(s5kgd1->regmap),
				     "failed to init CCI\n");

	s5kgd1->mclk = devm_v4l2_sensor_clk_get(s5kgd1->dev, NULL);
	if (IS_ERR(s5kgd1->mclk))
		return dev_err_probe(s5kgd1->dev, PTR_ERR(s5kgd1->mclk),
				     "failed to get MCLK clock\n");

	freq = clk_get_rate(s5kgd1->mclk);
	if (freq != S5KGD1_MCLK_FREQ_24MHZ)
		return dev_err_probe(s5kgd1->dev, -EINVAL,
				     "MCLK clock frequency %lu is not supported\n",
				     freq);

	ret = s5kgd1_check_hwcfg(s5kgd1);
	if (ret)
		return dev_err_probe(s5kgd1->dev, ret,
				     "failed to check HW configuration\n");

	s5kgd1->reset_gpio = devm_gpiod_get_optional(s5kgd1->dev, "reset",
						     GPIOD_OUT_HIGH);
	if (IS_ERR(s5kgd1->reset_gpio))
		return dev_err_probe(s5kgd1->dev, PTR_ERR(s5kgd1->reset_gpio),
				     "cannot get reset GPIO\n");

	s5kgd1->mipi_sel_gpio = devm_gpiod_get_optional(s5kgd1->dev, "mipi-sel",
						GPIOD_ASIS | GPIOD_FLAGS_BIT_NONEXCLUSIVE);
	if (IS_ERR(s5kgd1->mipi_sel_gpio))
		return dev_err_probe(s5kgd1->dev, PTR_ERR(s5kgd1->mipi_sel_gpio),
				     "cannot get MIPI switch GPIO\n");
	if (s5kgd1->mipi_sel_gpio)
		device_property_read_u32(s5kgd1->dev, "mipi-sel-value", &s5kgd1->mipi_sel_value);

	for (i = 0; i < S5KGD1_NUM_SUPPLIES; i++)
		s5kgd1->supplies[i].supply = s5kgd1_supply_names[i];

	ret = devm_regulator_bulk_get(s5kgd1->dev, S5KGD1_NUM_SUPPLIES,
				      s5kgd1->supplies);
	if (ret)
		return dev_err_probe(s5kgd1->dev, ret,
				     "failed to get supply regulators\n");

	/* The sensor must be powered on to read the CHIP_ID register */
	ret = s5kgd1_power_on(s5kgd1->dev);
	if (ret)
		return ret;

	ret = s5kgd1_identify_sensor(s5kgd1);
	if (ret) {
		dev_err_probe(s5kgd1->dev, ret, "failed to find sensor\n");
		goto power_off;
	}

	s5kgd1->mode = &s5kgd1_supported_modes[0];
	ret = s5kgd1_init_controls(s5kgd1);
	if (ret) {
		dev_err_probe(s5kgd1->dev, ret, "failed to init controls\n");
		goto power_off;
	}

	s5kgd1->sd.state_lock = s5kgd1->ctrl_handler.lock;
	s5kgd1->sd.internal_ops = &s5kgd1_internal_ops;
	s5kgd1->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	s5kgd1->sd.entity.ops = &s5kgd1_subdev_entity_ops;
	s5kgd1->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	s5kgd1->pad.flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&s5kgd1->sd.entity, 1, &s5kgd1->pad);
	if (ret) {
		dev_err_probe(s5kgd1->dev, ret,
			      "failed to init media entity pads\n");
		goto v4l2_ctrl_handler_free;
	}

	ret = v4l2_subdev_init_finalize(&s5kgd1->sd);
	if (ret < 0) {
		dev_err_probe(s5kgd1->dev, ret,
			      "failed to init media entity pads\n");
		goto media_entity_cleanup;
	}

	pm_runtime_set_active(s5kgd1->dev);
	pm_runtime_enable(s5kgd1->dev);

	ret = v4l2_async_register_subdev_sensor(&s5kgd1->sd);
	if (ret < 0) {
		dev_err_probe(s5kgd1->dev, ret,
			      "failed to register V4L2 subdev\n");
		goto subdev_cleanup;
	}

	pm_runtime_set_autosuspend_delay(s5kgd1->dev, 1000);
	pm_runtime_use_autosuspend(s5kgd1->dev);
	pm_runtime_idle(s5kgd1->dev);

	return 0;

subdev_cleanup:
	v4l2_subdev_cleanup(&s5kgd1->sd);
	pm_runtime_disable(s5kgd1->dev);
	pm_runtime_set_suspended(s5kgd1->dev);

media_entity_cleanup:
	media_entity_cleanup(&s5kgd1->sd.entity);

v4l2_ctrl_handler_free:
	v4l2_ctrl_handler_free(s5kgd1->sd.ctrl_handler);

power_off:
	s5kgd1_power_off(s5kgd1->dev);

	return ret;
}

static void s5kgd1_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct s5kgd1 *s5kgd1 = to_s5kgd1(sd);

	v4l2_async_unregister_subdev(sd);
	v4l2_subdev_cleanup(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(sd->ctrl_handler);
	pm_runtime_disable(s5kgd1->dev);

	if (!pm_runtime_status_suspended(s5kgd1->dev)) {
		s5kgd1_power_off(s5kgd1->dev);
		pm_runtime_set_suspended(s5kgd1->dev);
	}
}

static const struct dev_pm_ops s5kgd1_pm_ops = {
	SET_RUNTIME_PM_OPS(s5kgd1_power_off, s5kgd1_power_on, NULL)
};

static const struct of_device_id s5kgd1_of_match[] = {
	{ .compatible = "samsung,s5kgd1" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, s5kgd1_of_match);

static struct i2c_driver s5kgd1_i2c_driver = {
	.driver = {
		.name = "s5kgd1",
		.pm = &s5kgd1_pm_ops,
		.of_match_table = s5kgd1_of_match,
	},
	.probe = s5kgd1_probe,
	.remove = s5kgd1_remove,
};

module_i2c_driver(s5kgd1_i2c_driver);

MODULE_AUTHOR("Vladimir Zapolskiy <vladimir.zapolskiy@linaro.org>");
MODULE_DESCRIPTION("Samsung S5KGD1 image sensor driver");
MODULE_LICENSE("GPL");
