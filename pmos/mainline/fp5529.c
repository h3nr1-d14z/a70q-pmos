// SPDX-License-Identifier: GPL-2.0-only
/*
 * FitiPower FP5529 voice coil (focus) driver, as on the Galaxy A70 rear camera.
 * Registers and power-up sequence from the vendor camera module data (actuatorDriver of
 * com.samsung.sensormodule.0_lsi_s5kgd1): 0x02 control, 0x03/0x04 10-bit DAC (MSB bits 9:8,
 * LSB), 0x06/0x07 mode / timing.
 */
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>

#define FP5529_REG_CONTROL	0x02
#define FP5529_REG_DAC_MSB	0x03
#define FP5529_REG_DAC_LSB	0x04
#define FP5529_REG_MODE		0x06
#define FP5529_REG_TIMING	0x07
#define FP5529_MAX_FOCUS	1023

static const char * const fp5529_supply_names[] = { "vdd", "vio" };

struct fp5529 {
	struct i2c_client *client;
	struct regulator_bulk_data supplies[ARRAY_SIZE(fp5529_supply_names)];
	struct v4l2_ctrl_handler ctrls;
	struct v4l2_ctrl *focus;
	struct v4l2_subdev sd;
};

static inline struct fp5529 *to_fp5529(struct v4l2_subdev *sd)
{
	return container_of(sd, struct fp5529, sd);
}

static int fp5529_write(struct fp5529 *fp, u8 reg, u8 val)
{
	return i2c_smbus_write_byte_data(fp->client, reg, val);
}

static int fp5529_set_dac(struct fp5529 *fp, u16 val)
{
	int ret = fp5529_write(fp, FP5529_REG_DAC_MSB, (val >> 8) & 0x03);

	return ret ?: fp5529_write(fp, FP5529_REG_DAC_LSB, val & 0xff);
}

static int fp5529_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct fp5529 *fp = container_of(ctrl->handler, struct fp5529, ctrls);
	int ret;

	if (ctrl->id != V4L2_CID_FOCUS_ABSOLUTE)
		return -EINVAL;
	if (!pm_runtime_get_if_in_use(&fp->client->dev))
		return 0;
	ret = fp5529_set_dac(fp, ctrl->val);
	pm_runtime_put(&fp->client->dev);
	return ret;
}

static const struct v4l2_ctrl_ops fp5529_ctrl_ops = {
	.s_ctrl = fp5529_set_ctrl,
};

static int fp5529_power_on(struct device *dev)
{
	struct fp5529 *fp = to_fp5529(dev_get_drvdata(dev));
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(fp->supplies), fp->supplies);
	if (ret)
		return ret;
	usleep_range(1000, 2000);

	/* vendor sequence: reset, standby, mode/timing, park the lens, active */
	ret = fp5529_write(fp, FP5529_REG_CONTROL, 0x01);
	if (!ret)
		ret = fp5529_write(fp, FP5529_REG_CONTROL, 0x00);
	if (ret)
		goto off;
	usleep_range(5000, 6000);
	ret = fp5529_write(fp, FP5529_REG_MODE, 0x60);
	if (!ret)
		ret = fp5529_write(fp, FP5529_REG_TIMING, 0x05);
	if (!ret)
		ret = fp5529_set_dac(fp, fp->focus->val);
	if (!ret)
		ret = fp5529_write(fp, FP5529_REG_CONTROL, 0x02);
	if (ret)
		goto off;
	return 0;

off:
	regulator_bulk_disable(ARRAY_SIZE(fp->supplies), fp->supplies);
	return ret;
}

static int fp5529_power_off(struct device *dev)
{
	struct fp5529 *fp = to_fp5529(dev_get_drvdata(dev));

	fp5529_write(fp, FP5529_REG_CONTROL, 0x01);	/* standby */
	regulator_bulk_disable(ARRAY_SIZE(fp->supplies), fp->supplies);
	return 0;
}

static int fp5529_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	return pm_runtime_resume_and_get(sd->dev);
}

static int fp5529_close(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	pm_runtime_put_autosuspend(sd->dev);
	return 0;
}

static const struct v4l2_subdev_internal_ops fp5529_internal_ops = {
	.open = fp5529_open,
	.close = fp5529_close,
};

static const struct v4l2_subdev_ops fp5529_ops = { };

static int fp5529_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct fp5529 *fp;
	int i, ret;

	fp = devm_kzalloc(dev, sizeof(*fp), GFP_KERNEL);
	if (!fp)
		return -ENOMEM;
	fp->client = client;

	for (i = 0; i < ARRAY_SIZE(fp5529_supply_names); i++)
		fp->supplies[i].supply = fp5529_supply_names[i];
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(fp->supplies), fp->supplies);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get supplies\n");

	v4l2_i2c_subdev_init(&fp->sd, client, &fp5529_ops);
	fp->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	fp->sd.internal_ops = &fp5529_internal_ops;
	fp->sd.entity.function = MEDIA_ENT_F_LENS;

	v4l2_ctrl_handler_init(&fp->ctrls, 1);
	fp->focus = v4l2_ctrl_new_std(&fp->ctrls, &fp5529_ctrl_ops, V4L2_CID_FOCUS_ABSOLUTE,
				      0, FP5529_MAX_FOCUS, 1, 0);
	if (fp->ctrls.error) {
		ret = fp->ctrls.error;
		goto free_ctrls;
	}
	fp->sd.ctrl_handler = &fp->ctrls;

	ret = media_entity_pads_init(&fp->sd.entity, 0, NULL);
	if (ret)
		goto free_ctrls;

	/* check the device answers */
	ret = fp5529_power_on(dev);
	if (ret) {
		dev_err_probe(dev, ret, "no answer from the actuator\n");
		goto cleanup;
	}
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_set_autosuspend_delay(dev, 1000);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_idle(dev);

	ret = v4l2_async_register_subdev(&fp->sd);
	if (ret)
		goto pm_off;
	return 0;

pm_off:
	pm_runtime_disable(dev);
	fp5529_power_off(dev);
cleanup:
	media_entity_cleanup(&fp->sd.entity);
free_ctrls:
	v4l2_ctrl_handler_free(&fp->ctrls);
	return ret;
}

static void fp5529_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct fp5529 *fp = to_fp5529(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(&fp->ctrls);
	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		fp5529_power_off(&client->dev);
	pm_runtime_set_suspended(&client->dev);
}

static DEFINE_RUNTIME_DEV_PM_OPS(fp5529_pm_ops, fp5529_power_off, fp5529_power_on, NULL);

static const struct of_device_id fp5529_of_match[] = {
	{ .compatible = "fitipower,fp5529" },
	{ }
};
MODULE_DEVICE_TABLE(of, fp5529_of_match);

static struct i2c_driver fp5529_driver = {
	.driver = {
		.name = "fp5529",
		.of_match_table = fp5529_of_match,
		.pm = pm_ptr(&fp5529_pm_ops),
	},
	.probe = fp5529_probe,
	.remove = fp5529_remove,
};
module_i2c_driver(fp5529_driver);

MODULE_DESCRIPTION("FitiPower FP5529 VCM driver");
MODULE_LICENSE("GPL");
