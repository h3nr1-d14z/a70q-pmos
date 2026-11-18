// SPDX-License-Identifier: GPL-2.0
/*
 * Samsung Galaxy A70 (a70q) battery gauge ("QG-lite"), adapted from the a70q-battery driver of the
 * violet-sm6150-mainline project (Redmi Note 7 Pro, same SM6150/PM6150): only the battery data
 * differs (downstream "a70q_4400mAh" profile: FCC 4238 mAh at 25C, pc-temp-v1-lut 25C column).
 *
 * Mainline has no driver for the pm6150 QPNP QG fuel gauge or the SMB5
 * charger.  The QG hardware still measures pack voltage and current on its own,
 * so this driver reads those samples straight from the PMIC and does the
 * state-of-charge bookkeeping itself:
 *
 *  - current: QG_LAST_BURST_AVG_I (signed, 152.588 uA/LSB on the 5 A part),
 *    voltage: QG_LAST_ADC_V (194.637 uV/LSB), same formulas as the downstream
 *    qpnp-qg driver;
 *  - SoC is coulomb-counted from the current, starting from the last value the
 *    downstream driver left in its SDAM (so a downstream -> mainline hop continues
 *    seamlessly) or, when that is not trustworthy, from the IR-compensated
 *    open-circuit voltage and the vendor battery profile's 25C OCV table;
 *  - while the pack rests (|I| small) the SoC is pulled towards the OCV value,
 *    and it is pinned to 100 % when the charger reports termination;
 *  - the SoC is written back to the SDAM so it survives a reboot;
 *  - sign convention: ibat > 0 = charging (measured against the SMB5 registers,
 *    see the note in a70q_batt_sample()).
 */
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/workqueue.h>
#include <linux/ktime.h>

#define QG_BASE			0x4800
#define QG_SUBTYPE		(QG_BASE + 0x05)
#define QG_STATUS1		(QG_BASE + 0x08)
#define QG_DATA_CTL2		(QG_BASE + 0x42)
#define  BURST_AVG_HOLD	BIT(0)
#define QG_LAST_ADC_V		(QG_BASE + 0xc0)
#define QG_LAST_BURST_AVG_I	(QG_BASE + 0xc6)
#define QG_ADC_IBAT_5A		0x3

#define SDAM_BASE		0xb600
#define SDAM_VALID		(SDAM_BASE + 0x46)
#define SDAM_SOC		(SDAM_BASE + 0x47)
#define SDAM_RBAT		(SDAM_BASE + 0x4a)	/* u16 mOhm */

#define SMB5_CHGR_STATUS1	0x1006	/* [2:0] charge state, 5 = terminate */
#define SMB5_USBIN_RT_STS	0x1310	/* bit 4 USBIN plugged */
#define SMB5_CHARGING_ENABLE_CMD	0x1042	/* bit 0: charging enabled */
#define SMB5_FCC_CFG		0x1061	/* fast charge current, 50 mA steps, max 3 A */
#define SMB5_FV_CFG		0x1070	/* float voltage, 3.6 V + 10 mV steps */
#define SMB5_USBIN_ICL_CFG	0x1370	/* USB input current limit, 50 mA steps, max 3 A */
#define A70Q_FV_MAX_UV		4350000	/* qcom,max-voltage-uv of the battery profile */

#define FCC_UAH			4238000	/* a70q_4400mAh profile, fcc1-temp-lut 25C */
#define POLL_MS			5000
#define REST_UA			80000	/* |I| below this counts as resting */
#define REST_SECS		120

/* mV*10 -> percent, a70q_4400mAh profile (pc-temp-v1-lut), 25C, descending */
static const struct { u16 pct; u16 v; } ocv[] = {
	{100, 42839}, {98, 42614}, {96, 42393}, {94, 42178}, {92, 41975}, {90, 41781},
	{88, 41590}, {86, 41392}, {84, 41201}, {82, 41036}, {80, 40896}, {78, 40734},
	{76, 40462}, {74, 40137}, {72, 39938}, {70, 39838}, {68, 39751}, {66, 39642},
	{64, 39522}, {62, 39372}, {60, 39143}, {58, 38900}, {56, 38754}, {54, 38658},
	{52, 38575}, {50, 38493}, {48, 38417}, {46, 38349}, {44, 38287}, {42, 38230},
	{40, 38181}, {38, 38136}, {36, 38097}, {34, 38065}, {32, 38036}, {30, 38004},
	{28, 37973}, {26, 37934}, {24, 37872}, {22, 37787}, {20, 37692}, {18, 37582},
	{16, 37453}, {14, 37300}, {12, 37170}, {10, 37089}, {9, 37067}, {8, 37051},
	{7, 37032}, {6, 36987}, {5, 36767}, {4, 36332}, {3, 35775}, {2, 35023},
	{1, 33847}, {0, 30000},
};

struct a70q_batt {
	struct device *dev;
	struct regmap *map;
	struct power_supply *psy;
	struct delayed_work work;
	bool five_amp;
	int rbat_mohm;
	int vbat_uv, ibat_ua;		/* ibat: >0 discharging */
	s64 charge_uah;			/* remaining charge */
	int soc;			/* percent, 0..100 */
	int sdam_soc;
	bool plugged, term;
	ktime_t last, rest_since;
	bool resting;
};

/* returns percent * 100 */
static int ocv_to_soc_x100(int uv)
{
	int v = uv / 100, i;

	if (v >= ocv[0].v)
		return 10000;
	for (i = 1; i < ARRAY_SIZE(ocv); i++) {
		if (v >= ocv[i].v) {
			int span = ocv[i - 1].v - ocv[i].v;
			int dp = (ocv[i - 1].pct - ocv[i].pct) * 100;

			return ocv[i].pct * 100 + (v - ocv[i].v) * dp / span;
		}
	}
	return 0;
}

static int a70q_batt_sample(struct a70q_batt *b)
{
	unsigned int lo, hi, raw;
	u8 buf[2];
	int ret;

	ret = regmap_bulk_read(b->map, QG_LAST_ADC_V, buf, 2);
	if (ret)
		return ret;
	raw = buf[0] | (buf[1] << 8);
	b->vbat_uv = raw * 194637ULL / 1000;

	regmap_update_bits(b->map, QG_DATA_CTL2, BURST_AVG_HOLD, BURST_AVG_HOLD);
	ret = regmap_bulk_read(b->map, QG_LAST_BURST_AVG_I, buf, 2);
	regmap_update_bits(b->map, QG_DATA_CTL2, BURST_AVG_HOLD, 0);
	if (ret)
		return ret;
	raw = buf[0] | (buf[1] << 8);
	/*
	 * Sign, measured 2026-10-29 against the charger registers: with CHGR_STATUS1
	 * in an active state, CHARGER_INHIBIT clear, CMD_IL not suspended and ICL ~3 A,
	 * this negation yields a POSITIVE value while the pack charges. Hence
	 * ibat_ua > 0 = charging, ibat_ua < 0 = discharging.
	 */
	b->ibat_ua = -(s16)raw * (b->five_amp ? 152588LL : 305176LL) / 1000;

	regmap_read(b->map, SMB5_USBIN_RT_STS, &hi);
	b->plugged = hi & BIT(4);
	regmap_read(b->map, SMB5_CHGR_STATUS1, &lo);
	b->term = (lo & 7) == 5;
	return 0;
}

static void a70q_batt_poll(struct work_struct *w)
{
	struct a70q_batt *b = container_of(w, struct a70q_batt, work.work);
	ktime_t now = ktime_get_boottime();
	s64 dt_ms = ktime_ms_delta(now, b->last);
	int soc, ocv_uv, soc_ocv_x100;

	if (a70q_batt_sample(b))
		goto out;
	b->last = now;

	/* coulomb counting, ibat > 0 = charging */
	if (dt_ms > 0 && dt_ms < 60000)
		b->charge_uah += (s64)b->ibat_ua * dt_ms / 3600000;

	/* IR-compensated open circuit voltage */
	ocv_uv = b->vbat_uv - (s64)b->ibat_ua * b->rbat_mohm / 1000;
	soc_ocv_x100 = ocv_to_soc_x100(ocv_uv);

	if (abs(b->ibat_ua) < REST_UA) {
		if (!b->resting) {
			b->resting = true;
			b->rest_since = now;
		}
		/* after a quiet spell trust the OCV: nudge the counter to it */
		if (ktime_ms_delta(now, b->rest_since) > REST_SECS * 1000) {
			s64 target = (s64)soc_ocv_x100 * FCC_UAH / 10000;

			b->charge_uah += (target - b->charge_uah) / 20;
		}
	} else {
		b->resting = false;
	}

	/* the charger says the pack is full */
	if (b->plugged && b->term && b->vbat_uv > 4300000 && b->ibat_ua < 150000)
		b->charge_uah = FCC_UAH;

	b->charge_uah = clamp_t(s64, b->charge_uah, 0, FCC_UAH);
	soc = DIV_ROUND_CLOSEST_ULL(b->charge_uah * 100, FCC_UAH);

	/* only publish moves that agree with the current's direction */
	if (soc != b->soc) {
		if ((soc < b->soc && b->ibat_ua < 20000) ||
		    (soc > b->soc && b->ibat_ua > -20000))
			b->soc = soc;
	}
	if (b->soc != b->sdam_soc) {
		regmap_write(b->map, SDAM_SOC, b->soc);
		b->sdam_soc = b->soc;
	}
	dev_dbg(b->dev, "v=%d i=%d ocv=%d soc_ocv=%d.%02d q=%lld soc=%d plug=%d term=%d\n",
		b->vbat_uv, b->ibat_ua, ocv_uv, soc_ocv_x100 / 100, soc_ocv_x100 % 100,
		b->charge_uah, b->soc, b->plugged, b->term);
	power_supply_changed(b->psy);
out:
	schedule_delayed_work(&b->work, msecs_to_jiffies(POLL_MS));
}

static int a70q_batt_get(struct power_supply *psy, enum power_supply_property p,
			   union power_supply_propval *val)
{
	struct a70q_batt *b = power_supply_get_drvdata(psy);

	switch (p) {
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 1;
		return 0;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		return 0;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = 4350000;	/* qcom,max-voltage-uv */
		return 0;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		val->intval = 3400000;
		return 0;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		val->intval = FCC_UAH;
		return 0;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		val->intval = b->charge_uah;
		return 0;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = POWER_SUPPLY_HEALTH_GOOD;
		return 0;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = b->vbat_uv;
		return 0;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = b->ibat_ua;	/* kernel ABI: positive = charging */
		return 0;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = b->soc;
		return 0;
	case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
		val->intval = b->soc >= 100 ? POWER_SUPPLY_CAPACITY_LEVEL_FULL :
			      b->soc >= 85 ? POWER_SUPPLY_CAPACITY_LEVEL_HIGH :
			      b->soc >= 20 ? POWER_SUPPLY_CAPACITY_LEVEL_NORMAL :
			      b->soc >= 5 ? POWER_SUPPLY_CAPACITY_LEVEL_LOW :
					    POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
		return 0;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT: {
		unsigned int v = 0;

		regmap_read(b->map, SMB5_FCC_CFG, &v);
		val->intval = v * 50000;
		return 0;
	}
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		val->intval = 3000000;
		return 0;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE: {
		unsigned int v = 0;

		regmap_read(b->map, SMB5_FV_CFG, &v);
		val->intval = 3600000 + v * 10000;
		return 0;
	}
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
		val->intval = A70Q_FV_MAX_UV;
		return 0;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT: {
		unsigned int v = 0;

		regmap_read(b->map, SMB5_USBIN_ICL_CFG, &v);
		val->intval = v * 50000;
		return 0;
	}
	case POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR: {
		unsigned int v = 0;

		regmap_read(b->map, SMB5_CHARGING_ENABLE_CMD, &v);
		val->intval = (v & BIT(0)) ? POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO :
					     POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE;
		return 0;
	}
	case POWER_SUPPLY_PROP_STATUS:
		if (!b->plugged)
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		else if (b->soc >= 100 || (b->term && b->vbat_uv > 4300000))
			val->intval = POWER_SUPPLY_STATUS_FULL;
		else if (b->ibat_ua > 30000)
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		else
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
		return 0;
	default:
		return -EINVAL;
	}
}

/* charger control: SMB5 registers written directly (there is no SMB5 driver) */
static int a70q_batt_set(struct power_supply *psy, enum power_supply_property p,
			 const union power_supply_propval *val)
{
	struct a70q_batt *b = power_supply_get_drvdata(psy);

	switch (p) {
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		return regmap_write(b->map, SMB5_FCC_CFG,
				    clamp(val->intval, 0, 3000000) / 50000);
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		return regmap_write(b->map, SMB5_FV_CFG,
				    (clamp(val->intval, 3600000, A70Q_FV_MAX_UV) - 3600000) / 10000);
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		return regmap_write(b->map, SMB5_USBIN_ICL_CFG,
				    clamp(val->intval, 0, 3000000) / 50000);
	case POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR:
		if (val->intval == POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO)
			return regmap_update_bits(b->map, SMB5_CHARGING_ENABLE_CMD, BIT(0), BIT(0));
		if (val->intval == POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE)
			return regmap_update_bits(b->map, SMB5_CHARGING_ENABLE_CMD, BIT(0), 0);
		return -EINVAL;
	default:
		return -EINVAL;
	}
}

static int a70q_batt_writeable(struct power_supply *psy, enum power_supply_property p)
{
	return p == POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT ||
	       p == POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE ||
	       p == POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT ||
	       p == POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR;
}

static const enum power_supply_property a70q_batt_props[] = {
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT, POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE, POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT, POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR,
	POWER_SUPPLY_PROP_STATUS, POWER_SUPPLY_PROP_PRESENT, POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_HEALTH, POWER_SUPPLY_PROP_VOLTAGE_NOW, POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CAPACITY, POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_CHARGE_NOW, POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN, POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
};

static const struct power_supply_desc a70q_batt_desc = {
	.name = "battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = a70q_batt_props,
	.num_properties = ARRAY_SIZE(a70q_batt_props),
	.get_property = a70q_batt_get,
	.set_property = a70q_batt_set,
	.property_is_writeable = a70q_batt_writeable,
	.charge_behaviours = BIT(POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO) |
			     BIT(POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE),
};

static int a70q_batt_probe(struct platform_device *pdev)
{
	struct power_supply_config cfg = {};
	struct a70q_batt *b;
	unsigned int v, soc = 0, valid = 0, rbat[2] = {};
	int soc_x100;

	b = devm_kzalloc(&pdev->dev, sizeof(*b), GFP_KERNEL);
	if (!b)
		return -ENOMEM;
	b->dev = &pdev->dev;
	b->map = dev_get_regmap(pdev->dev.parent, NULL);
	if (!b->map)
		return -ENODEV;

	regmap_read(b->map, QG_SUBTYPE, &v);
	b->five_amp = v == QG_ADC_IBAT_5A;
	regmap_read(b->map, SDAM_VALID, &valid);
	regmap_read(b->map, SDAM_SOC, &soc);
	regmap_read(b->map, SDAM_RBAT, &rbat[0]);
	regmap_read(b->map, SDAM_RBAT + 1, &rbat[1]);
	b->rbat_mohm = rbat[0] | (rbat[1] << 8);
	if (b->rbat_mohm < 20 || b->rbat_mohm > 400)
		b->rbat_mohm = 120;

	if (a70q_batt_sample(b))
		return dev_err_probe(&pdev->dev, -EIO, "QG not answering\n");
	soc_x100 = ocv_to_soc_x100(b->vbat_uv + (s64)b->ibat_ua * b->rbat_mohm / 1000);
	/*
	 * Start from the OCV estimate when the pack is resting; with a load on it
	 * (or while charging) the downstream driver's saved value is better.
	 */
	if (abs(b->ibat_ua) < REST_UA || !valid || soc > 100 || !soc)
		b->soc = DIV_ROUND_CLOSEST(soc_x100, 100);
	else
		b->soc = soc;
	b->sdam_soc = soc;
	b->charge_uah = (s64)b->soc * FCC_UAH / 100;
	b->last = ktime_get_boottime();
	dev_info(&pdev->dev, "QG %s, rbat %d mOhm, sdam soc %u (valid %u), vbat %d uV ibat %d uA -> start %d%%\n",
		 b->five_amp ? "5A" : "10A", b->rbat_mohm, soc, valid, b->vbat_uv, b->ibat_ua, b->soc);

	cfg.drv_data = b;
	cfg.fwnode = dev_fwnode(&pdev->dev);
	b->psy = devm_power_supply_register(&pdev->dev, &a70q_batt_desc, &cfg);
	if (IS_ERR(b->psy))
		return PTR_ERR(b->psy);
	INIT_DELAYED_WORK(&b->work, a70q_batt_poll);
	platform_set_drvdata(pdev, b);
	schedule_delayed_work(&b->work, msecs_to_jiffies(POLL_MS));
	return 0;
}

static void a70q_batt_remove(struct platform_device *pdev)
{
	cancel_delayed_work_sync(&((struct a70q_batt *)platform_get_drvdata(pdev))->work);
}

static const struct of_device_id a70q_batt_of[] = {
	{ .compatible = "samsung,a70q-battery" }, {}
};
MODULE_DEVICE_TABLE(of, a70q_batt_of);

static struct platform_driver a70q_batt_driver = {
	.probe = a70q_batt_probe,
	.remove = a70q_batt_remove,
	.driver = { .name = "a70q-battery", .of_match_table = a70q_batt_of },
};
module_platform_driver(a70q_batt_driver);
MODULE_DESCRIPTION("Samsung Galaxy A70 battery gauge reading the PM6150 QG hardware");
MODULE_LICENSE("GPL");
