// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Samsung S2MPB03 camera PMIC (7 LDOs over I2C), as on the Galaxy A70.
 * Register layout from Samsung's downstream driver (drivers/regulator/s2mpb03.c,
 * Copyright (c) 2017 Samsung Electronics Co., Ltd, GPL-2.0-or-later):
 *   0x00 PMIC_ID, 0x01 STATUS, 0x02 CTRL, 0x03..0x09 LDO1..LDO7 CTRL
 *   LDOn CTRL: [7] enable, [5:0] voltage select
 *   LDO1/2/4: 0.7 V + 12.5 mV steps, LDO3: 0.7 V + 25 mV, LDO5/6/7: 1.8 V + 25 mV
 */
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>

#define S2MPB03_REG_PMIC_ID	0x00
#define S2MPB03_REG_LDO1_CTRL	0x03
#define S2MPB03_REG_LDO_SLEW2	0x0b
#define S2MPB03_LDO_VSEL_MASK	0x3f
#define S2MPB03_LDO_ENABLE	BIT(7)

static const struct regulator_ops s2mpb03_ldo_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
};

#define S2MPB03_LDO(_n, _min, _step) {					\
	.name		= "ldo" #_n,					\
	.of_match	= of_match_ptr("ldo" #_n),			\
	.regulators_node = of_match_ptr("regulators"),			\
	.id		= (_n) - 1,					\
	.ops		= &s2mpb03_ldo_ops,				\
	.type		= REGULATOR_VOLTAGE,				\
	.owner		= THIS_MODULE,					\
	.min_uV		= (_min),					\
	.uV_step	= (_step),					\
	.n_voltages	= S2MPB03_LDO_VSEL_MASK + 1,			\
	.vsel_reg	= S2MPB03_REG_LDO1_CTRL + (_n) - 1,		\
	.vsel_mask	= S2MPB03_LDO_VSEL_MASK,			\
	.enable_reg	= S2MPB03_REG_LDO1_CTRL + (_n) - 1,		\
	.enable_mask	= S2MPB03_LDO_ENABLE,				\
	.enable_time	= 150,						\
}

static const struct regulator_desc s2mpb03_regulators[] = {
	S2MPB03_LDO(1, 700000, 12500),
	S2MPB03_LDO(2, 700000, 12500),
	S2MPB03_LDO(3, 700000, 25000),
	S2MPB03_LDO(4, 700000, 12500),
	S2MPB03_LDO(5, 1800000, 25000),
	S2MPB03_LDO(6, 1800000, 25000),
	S2MPB03_LDO(7, 1800000, 25000),
};

static const struct regmap_config s2mpb03_regmap = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.max_register	= S2MPB03_REG_LDO_SLEW2,
};

static int s2mpb03_probe(struct i2c_client *i2c)
{
	struct regulator_config config = { .dev = &i2c->dev };
	struct regmap *regmap;
	unsigned int id;
	int i, ret;

	regmap = devm_regmap_init_i2c(i2c, &s2mpb03_regmap);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	ret = regmap_read(regmap, S2MPB03_REG_PMIC_ID, &id);
	if (ret)
		return dev_err_probe(&i2c->dev, ret, "failed to read PMIC ID\n");
	dev_info(&i2c->dev, "S2MPB03 PMIC ID 0x%02x\n", id);

	config.regmap = regmap;
	for (i = 0; i < ARRAY_SIZE(s2mpb03_regulators); i++) {
		struct regulator_dev *rdev;

		rdev = devm_regulator_register(&i2c->dev, &s2mpb03_regulators[i], &config);
		if (IS_ERR(rdev))
			return dev_err_probe(&i2c->dev, PTR_ERR(rdev), "failed to register %s\n",
					     s2mpb03_regulators[i].name);
	}

	return 0;
}

static const struct of_device_id s2mpb03_of_match[] = {
	{ .compatible = "samsung,s2mpb03" },
	{ }
};
MODULE_DEVICE_TABLE(of, s2mpb03_of_match);

static struct i2c_driver s2mpb03_driver = {
	.driver = {
		.name = "s2mpb03",
		.of_match_table = s2mpb03_of_match,
	},
	.probe = s2mpb03_probe,
};
module_i2c_driver(s2mpb03_driver);

MODULE_DESCRIPTION("Samsung S2MPB03 camera PMIC regulator driver");
MODULE_LICENSE("GPL");
