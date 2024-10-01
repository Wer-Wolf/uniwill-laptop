// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Linux driver for Uniwill notebooks.
 *
 * Copyright (C) 2024 Armin Wolf <W_Armin@gmx.de>
 */

#define pr_format(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/container_of.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/device/driver.h>
#include <linux/errno.h>
#include <linux/fixp-arith.h>
#include <linux/hwmon.h>
#include <linux/kernel.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_profile.h>
#include <linux/pm.h>
#include <linux/printk.h>
#include <linux/regmap.h>
#include <linux/types.h>
#include <linux/wmi.h>

#include <asm/unaligned.h>

#include "uniwill-wmi.h"

#define EC_ADDR_BAT_STATUS	0x0432
#define BAT_DISCHARGING		BIT(0)

#define EC_ADDR_CPU_TEMP	0x043E

#define EC_ADDR_GPU_TEMP	0x044F

#define EC_ADDR_MAIN_FAN_RPM_1	0x0464

#define EC_ADDR_MAIN_FAN_RPM_2	0x0465

#define EC_ADDR_SECOND_FAN_RPM_1	0x046C

#define EC_ADDR_SECOND_FAN_RPM_2	0x046D

#define EC_ADDR_BAT_ALLERT	0x0494

#define EC_ADDR_PROJECT_ID	0x0740

#define EC_ADDR_AP_OEM		0x0741
#define	ENABLE_MANUAL_CTRL	BIT(0)
#define ITE_KBD_EFFECT_REACTIVE	BIT(3)
#define FAN_ABNORMAL		BIT(5)

#define EC_ADDR_SUPPORT_5	0x0742
#define FAN_TURBO_SUPPORTED	BIT(4)
#define FAN_SUPPORT		BIT(5)
#define CHARGIN_PRIO_SUPPORTED	BIT(5)	// TODO Conflict!

#define EC_ADDR_CTGP_DB_CTRL	0x0743
#define CTGP_DB_GENERAL_ENABLE	BIT(0)
#define CTGP_DB_DB_ENABLE	BIT(1)
#define CTGP_DB_CTGP_ENABLE	BIT(2)

#define EC_ADDR_CTGP_OFFSET	0x0744

#define EC_ADDR_TPP_OFFSET	0x0745

#define EC_ADDR_MAX_TGP		0x0746

#define EC_ADDR_LIGHTBAR_CTRL	0x0748
#define LIGHTBAR_POWER_SAFE	BIT(1)
#define LIGHTBAR_S0_OFF		BIT(2)
#define LIGHTBAR_S3_OFF		BIT(3)
#define LIGHTBAR_RAINBOW	BIT(7)

#define EC_ADDR_LIGHTBAR_RED	0x0749

#define EC_ADDR_LIGHTBAR_GREEN	0x074A

#define EC_ADDR_LIGHTBAR_BLUE	0x074B

#define EC_ADDR_BIOS_OEM	0x074E
#define FN_LOCK_STATUS		BIT(4)

#define EC_ADDR_MANUAL_FAN_CTRL	0x0751
#define FAN_LEVEL_MASK		GENMASK(2, 0)
#define FAN_MODE_TURBO		BIT(4)
#define FAN_MODE_HIGH		BIT(5)
#define FAN_MODE_BOOST		BIT(6)
#define FAN_MODE_USER		BIT(7)

#define EC_ADDR_SUPPORT_1	0x0765
#define AIRPLANE_MODE		BIT(0)
#define GPS_SWITCH		BIT(1)
#define OVERCLOCK		BIT(2)
#define MACRO_KEY		BIT(3)
#define SHORTCUT_KEY		BIT(4)
#define SUPER_KEY_LOCK		BIT(5)
#define LIGHTBAR		BIT(6)
#define FAN_BOOST		BIT(7)	/* Seems to be unrelated to manual fan control */

#define EC_ADDR_SUPPORT_2	0x0766
#define SILENT_MODE		BIT(0)
#define USB_CHARGING		BIT(1)
#define SINGLE_ZONE_KBD		BIT(2)
#define CHINA_MODE		BIT(5)
#define MY_BATTERY		BIT(6)

#define EC_ADDR_TRIGGER		0x0767
#define TRIGGER_SUPER_KEY_LOCK	BIT(0)
#define TRIGGER_LIGHTBAR	BIT(1)
#define TRIGGER_FAN_BOOST	BIT(2)
#define TRIGGER_SILENT_MODE	BIT(3)
#define TRIGGER_USB_CHARGING	BIT(4)
#define RGB_APPLY_COLOR		BIT(5)
#define RGB_RAINBOW_EFFECT	BIT(7)

#define EC_ADDR_SWITCH_STATUS	0x0768
#define SUPER_KEY_LOCK_STATUS	BIT(0)
#define LIGHTBAR_STATUS		BIT(1)
#define FAN_BOOST_STATUS	BIT(2)

#define EC_ADDR_RGB_RED		0x0769

#define EC_ADDR_RGB_GREEN	0x076A

#define EC_ADDR_RGB_BLUE	0x076B

#define EC_ADDR_ROMID_START	0x0770
#define ROMID_LENGTH		14

#define EC_ADDR_ROMID_EXTRA_1	0x077E

#define EC_ADDR_ROMID_EXTRA_2	0x077F

#define EC_ADDR_BIOS_OEM_2	0x0782
#define FAN_V2_NEW		BIT(0)
#define FAN_QKEY		BIT(1)
#define FAN_TABLE_OFFICE_MODE	BIT(2)
#define FAN_V3			BIT(3)
#define DEFAULT_MODE		BIT(4)

#define EC_ADDR_PL1_SETTING	0x0783

#define EC_ADDR_PL2_SETTING	0x0784

#define EC_ADDR_PL4_SETTING	0x0785

#define EC_ADDR_FAN_DEFAULT	0x0786
#define FAN_CURVE_LENGTH	5

#define EC_ADDR_KBD_STATUS	0x078C
#define KBD_WHITE_ONLY		BIT(0)	// ~single color
#define KBD_SINGLE_COLOR_OFF	BIT(1)
#define KBD_TURBO_LEVEL_MASK	GENMASK(3, 2)
#define KBD_APPLY		BIT(4)
#define KBD_BRIGHTNESS		GENMASK(7, 5)

#define EC_ADDR_FAN_CTRL	0x078E
#define FAN3P5			BIT(1)
#define CHARGING_PROFILE	BIT(3)
#define UNIVERSAL_FAN_CTRL	BIT(6)

#define EC_ADDR_BIOS_OEM_3	0x07A3
#define FAN_REDUCED_DURY_CYCLE	BIT(5)
#define FAN_ALWAYS_ON		BIT(6)

#define EC_ADDR_BIOS_BYTE	0x07A4
#define FN_LOCK_SWITCH		BIT(3)

#define EC_ADDR_OEM_3		0x07A5
#define POWER_LED_MASK		GENMASK(1, 0)
#define POWER_LED_LEFT		0x00
#define POWER_LED_BOTH		0x01
#define POWER_LED_NONE		0x02
#define FAN_QUIET		BIT(2)
#define OVERBOOST		BIT(4)
#define HIGH_POWER		BIT(7)

#define EC_ADDR_OEM_4		0x07A6
#define OVERBOOST_DYN_TEMP_OFF	BIT(1)
#define TOUCHPAD_TOGGLE_OFF	BIT(6)
// TODO

#define EC_ADDR_CHARGE_CTRL	0x07B9
#define CHARGE_CTRL_MASK	GEMASK(6, 0)
#define CHARGE_CTRL_REACHED	BIT(7)

#define EC_ADDR_CHARGE_PRIO	0x07CC
#define CHARGING_PERFORMANCE	BIT(7)

#define EC_ADDR_PWM_1		0x1804

#define EC_ADDR_PWM_2		0x1809

#define DRIVER_NAME	"uniwill"
#define UNIWILL_GUID	"ABBC0F6F-8EA1-11D1-00A0-C90629100000"

#define PWM_MAX			200

enum uniwill_method {
	UNIWILL_GET_ULONG	= 0x01,
	UNIWILL_SET_ULONG	= 0x02,
	UNIWILL_FIRE_ULONG	= 0x03,
	UNIWILL_GET_SET_ULONG	= 0x04,
	UNIWILL_GET_BUTTON	= 0x05,
};

struct uniwill_method_buffer {
	__le16 address;
	__le16 data;
	__le16 operation;
	__le16 reserved;
} __packed;

struct uniwill_data {
	struct wmi_device *wdev;
	struct regmap *regmap;
	struct platform_profile_handler profile_handler;
	struct notifier_block notifier;
};

static const char * const uniwill_temp_labels[] = {
	"CPU",
	"GPU",
};

static const char * const uniwill_fan_labels[] = {
	"Main",
	"Secondary",
};

static int uniwill_get_set_ulong(struct wmi_device *wdev, struct uniwill_method_buffer *input,
				 u32 *output)
{
	struct acpi_buffer out = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_buffer in = {
		.length = sizeof(*input),
		.pointer = input,
	};
	union acpi_object *obj;
	acpi_status status;
	int ret = 0;

	status = wmidev_evaluate_method(wdev, 0x0, UNIWILL_GET_SET_ULONG, &in, &out);
	if (ACPI_FAILURE(status))
		return -EIO;

	obj = out.pointer;
	if (!obj)
		return -ENODATA;

	if (obj->type != ACPI_TYPE_BUFFER) {
		ret = -ENOMSG;
		goto free_obj;
	}

	if (obj->buffer.length < sizeof(*output)) {
		ret = -EPROTO;
		goto free_obj;
	}

	*output = get_unaligned_le32(obj->buffer.pointer);

free_obj:
	kfree(obj);

	return ret;
}

static int uniwill_ec_reg_write(void *context, unsigned int reg, unsigned int val)
{
	struct uniwill_method_buffer input = {
		.address = cpu_to_le16(reg),
		.data = cpu_to_le16(val & U8_MAX),
		.operation = 0x0000,
	};
	struct uniwill_data *data = context;
	u32 output;
	int ret;

	ret = uniwill_get_set_ulong(data->wdev, &input, &output);
	if (ret < 0)
		return ret;

	if (output == 0xFEFEFEFE)
		return -ENXIO;

	return 0;
}

static int uniwill_ec_reg_read(void *context, unsigned int reg, unsigned int *val)
{
	struct uniwill_method_buffer input = {
		.address = cpu_to_le16(reg),
		.data = 0x0000,
		.operation = cpu_to_le16(0x0100),
	};
	struct uniwill_data *data = context;
	u32 output;
	int ret;

	ret = uniwill_get_set_ulong(data->wdev, &input, &output);
	if (ret < 0)
		return ret;

	if (output == 0xFEFEFEFE)
		return -ENXIO;

	*val = (u8)output;

	return 0;
}

static const struct regmap_bus uniwill_ec_bus = {
	.reg_write = uniwill_ec_reg_write,
	.reg_read = uniwill_ec_reg_read,
	.reg_format_endian_default = REGMAP_ENDIAN_LITTLE,
	.val_format_endian_default = REGMAP_ENDIAN_LITTLE,
};

static bool uniwill_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case EC_ADDR_AP_OEM:
	case EC_ADDR_MANUAL_FAN_CTRL:
	case EC_ADDR_PWM_1:
	case EC_ADDR_PWM_2:
		return true;
	default:
		return false;
	}
}

static bool uniwill_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case EC_ADDR_CPU_TEMP:
	case EC_ADDR_GPU_TEMP:
	case EC_ADDR_MAIN_FAN_RPM_1:
	case EC_ADDR_MAIN_FAN_RPM_2:
	case EC_ADDR_SECOND_FAN_RPM_1:
	case EC_ADDR_SECOND_FAN_RPM_2:
	case EC_ADDR_PROJECT_ID:
	case EC_ADDR_AP_OEM:
	case EC_ADDR_MANUAL_FAN_CTRL:
	case EC_ADDR_SUPPORT_1:
	case EC_ADDR_PWM_1:
	case EC_ADDR_PWM_2:
		return true;
	default:
		return false;
	}
}

static bool uniwill_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case EC_ADDR_CPU_TEMP:
	case EC_ADDR_GPU_TEMP:
	case EC_ADDR_MAIN_FAN_RPM_1:
	case EC_ADDR_MAIN_FAN_RPM_2:
	case EC_ADDR_SECOND_FAN_RPM_1:
	case EC_ADDR_SECOND_FAN_RPM_2:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config uniwill_ec_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.writeable_reg = uniwill_writeable_reg,
	.readable_reg = uniwill_readable_reg,
	.volatile_reg = uniwill_volatile_reg,
	.can_sleep = true,
	.cache_type = REGCACHE_MAPLE,
	.use_single_read = true,
	.use_single_write = true,
};

static umode_t uniwill_is_visible(const void *drvdata, enum hwmon_sensor_types type, u32 attr,
				  int channel)
{
	switch (type) {
	case hwmon_temp:
		return 0444;
	case hwmon_fan:
		return 0444;
	case hwmon_pwm:
		return 0644;
	default:
		return 0;
	}
}

static int uniwill_read(struct device *dev, enum hwmon_sensor_types type, u32 attr, int channel,
			long *val)
{
	struct uniwill_data *data = dev_get_drvdata(dev);
	unsigned int value;
	__be16 rpm;
	int ret;

	switch (type) {
	case hwmon_temp:
		switch (channel) {
		case 0:
			ret = regmap_read(data->regmap, EC_ADDR_CPU_TEMP, &value);
			break;
		case 1:
			ret = regmap_read(data->regmap, EC_ADDR_GPU_TEMP, &value);
			break;
		default:
			return -EOPNOTSUPP;
		}

		if (ret < 0)
			return ret;

		*val = value * 1000;
		return 0;
	case hwmon_fan:
		switch (channel) {
		case 0:
			ret = regmap_bulk_read(data->regmap, EC_ADDR_MAIN_FAN_RPM_1, &rpm,
					       sizeof(rpm));
			break;
		case 1:
			ret = regmap_bulk_read(data->regmap, EC_ADDR_SECOND_FAN_RPM_1, &rpm,
					       sizeof(rpm));
			break;
		default:
			return -EOPNOTSUPP;
		}

		if (ret < 0)
			return ret;

		*val = be16_to_cpu(rpm);
		return 0;
	case hwmon_pwm:
		switch (attr) {
		case hwmon_pwm_input:
			switch (channel) {
			case 0:
				ret = regmap_read(data->regmap, EC_ADDR_PWM_1, &value);
				break;
			case 1:
				ret = regmap_read(data->regmap, EC_ADDR_PWM_2, &value);
				break;
			default:
				return -EOPNOTSUPP;
			}

			*val = fixp_linear_interpolate(0, 0, PWM_MAX, U8_MAX, value);
			return 0;
		case hwmon_pwm_enable:
			ret = regmap_read(data->regmap, EC_ADDR_MANUAL_FAN_CTRL, &value);
			if (ret < 0)
				return ret;

			if (value & FAN_MODE_BOOST)
				*val = 1;
			else
				*val = 2;

			return 0;
		default:
			return -EOPNOTSUPP;
		}
	default:
		return -EOPNOTSUPP;
	}
}

static int uniwill_read_string(struct device *dev, enum hwmon_sensor_types type, u32 attr,
			       int channel, const char **str)
{
	switch (type) {
	case hwmon_temp:
		*str = uniwill_temp_labels[channel];
		return 0;
	case hwmon_fan:
		*str = uniwill_fan_labels[channel];
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int uniwill_write(struct device *dev, enum hwmon_sensor_types type, u32 attr, int channel,
			 long val)
{
	struct uniwill_data *data = dev_get_drvdata(dev);
	unsigned int value;

	switch (type) {
	case hwmon_pwm:
		switch (attr) {
		case hwmon_pwm_input:
			value = fixp_linear_interpolate(0, 0, U8_MAX, PWM_MAX,
							clamp_val(val, 0, U8_MAX));
			switch (channel) {
			case 0:
				return regmap_write(data->regmap, EC_ADDR_PWM_1, value);
			case 1:
				return regmap_write(data->regmap, EC_ADDR_PWM_2, value);
			default:
				return -EOPNOTSUPP;
			}
		case hwmon_pwm_enable:
			switch (val) {
			case 1:
				return regmap_update_bits(data->regmap, EC_ADDR_MANUAL_FAN_CTRL,
							  FAN_MODE_BOOST, FAN_MODE_BOOST);
			case 2:
				return regmap_update_bits(data->regmap, EC_ADDR_MANUAL_FAN_CTRL,
							  FAN_MODE_BOOST, 0);
			default:
				return -EOPNOTSUPP;
			}
		default:
			return -EOPNOTSUPP;
		}
	default:
		return -EOPNOTSUPP;
	}
}

static const struct hwmon_ops uniwill_ops = {
	.is_visible = uniwill_is_visible,
	.read = uniwill_read,
	.read_string = uniwill_read_string,
	.write = uniwill_write,
};

static const struct hwmon_channel_info * const uniwill_info[] = {
	HWMON_CHANNEL_INFO(chip, HWMON_C_REGISTER_TZ),
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL),
	HWMON_CHANNEL_INFO(fan,
			   HWMON_F_INPUT | HWMON_F_LABEL,
			   HWMON_F_INPUT | HWMON_F_LABEL),
	HWMON_CHANNEL_INFO(pwm,
			   HWMON_PWM_INPUT | HWMON_PWM_ENABLE,
			   HWMON_PWM_INPUT | HWMON_PWM_ENABLE),
	NULL
};

static const struct hwmon_chip_info uniwill_chip_info = {
	.ops = &uniwill_ops,
	.info = uniwill_info,
};

static void uniwill_disable_manual_control(void *context)
{
	struct uniwill_data *data = context;

	regmap_update_bits(data->regmap, EC_ADDR_AP_OEM, ENABLE_MANUAL_CTRL, 0);
}

static int uniwill_hwmon_init(struct uniwill_data *data)
{
	struct device *hdev;

	hdev = devm_hwmon_device_register_with_info(&data->wdev->dev, "uniwill", data,
						    &uniwill_chip_info, NULL);

	return PTR_ERR_OR_ZERO(hdev);
}

static int uniwill_platform_profile_get(struct platform_profile_handler *pprof,
					enum platform_profile_option *profile)
{
	struct uniwill_data *data = container_of(pprof, struct uniwill_data, profile_handler);
	unsigned int mask = FAN_MODE_USER | FAN_MODE_HIGH | FAN_MODE_TURBO;
	unsigned int value;
	int ret;

	ret = regmap_read(data->regmap, EC_ADDR_MANUAL_FAN_CTRL, &value);
	if (ret < 0)
		return ret;

	switch (value & mask) {
	case (FAN_MODE_USER | FAN_MODE_HIGH):
		return PLATFORM_PROFILE_BALANCED;
	case 0x00:
		return PLATFORM_PROFILE_BALANCED_PERFORMANCE;
	case FAN_MODE_TURBO:
		return PLATFORM_PROFILE_PERFORMANCE;
	default:
		return -EINVAL;
	}
}

static int uniwill_platform_profile_set(struct platform_profile_handler *pprof,
					enum platform_profile_option profile)
{
	struct uniwill_data *data = container_of(pprof, struct uniwill_data, profile_handler);
	unsigned int mask = FAN_MODE_USER | FAN_MODE_HIGH | FAN_MODE_TURBO;
	unsigned int value;

	switch (profile) {
	case PLATFORM_PROFILE_BALANCED:
		value = FAN_MODE_USER | FAN_MODE_HIGH;
		break;
	case PLATFORM_PROFILE_BALANCED_PERFORMANCE:
		value = 0x00;
		break;
	case PLATFORM_PROFILE_PERFORMANCE:
		value = FAN_MODE_TURBO;
		break;
	default:
		return -EINVAL;
	}

	return regmap_update_bits(data->regmap, EC_ADDR_MANUAL_FAN_CTRL, mask, value);
}

static int uniwill_wmi_notify_call(struct notifier_block *nb, unsigned long action, void *data)
{
	u32 *event = data;

	if (*event != UNIWILL_OSD_PERF_MODE_CHANGED)
		return NOTIFY_DONE;

	platform_profile_cycle();

	return NOTIFY_OK;
}

static void devm_platform_profile_remove(void *data)
{
	platform_profile_remove();
}

static int devm_platform_profile_register(struct device *dev, struct platform_profile_handler *pprof)
{
	 int ret;

	ret = platform_profile_register(pprof);
	if (ret < 0)
		return ret;

	return devm_add_action_or_reset(dev, devm_platform_profile_remove, NULL);
}

static int uniwill_platform_profile_init(struct uniwill_data *data)
{
	int ret;

	set_bit(PLATFORM_PROFILE_BALANCED, data->profile_handler.choices);
	set_bit(PLATFORM_PROFILE_BALANCED_PERFORMANCE, data->profile_handler.choices);
	set_bit(PLATFORM_PROFILE_PERFORMANCE, data->profile_handler.choices);

	data->profile_handler.profile_get = uniwill_platform_profile_get;
	data->profile_handler.profile_set = uniwill_platform_profile_set;

	ret = devm_platform_profile_register(&data->wdev->dev, &data->profile_handler);
	if (ret < 0)
		return ret;

	data->notifier.notifier_call = uniwill_wmi_notify_call;

	return devm_uniwill_wmi_register_notifier(&data->wdev->dev, &data->notifier);
}

static int uniwill_ec_init(struct uniwill_data *data)
{
	unsigned int value;
	int ret;

	ret = regmap_read(data->regmap, EC_ADDR_PROJECT_ID, &value);
	if (ret < 0)
		return ret;

	dev_dbg(&data->wdev->dev, "Project ID: %u\n", value);

	ret = regmap_update_bits(data->regmap, EC_ADDR_AP_OEM, ENABLE_MANUAL_CTRL,
				 ENABLE_MANUAL_CTRL);
	if (ret < 0)
		return ret;

	return devm_add_action_or_reset(&data->wdev->dev, uniwill_disable_manual_control, data);
}

static int uniwill_probe(struct wmi_device *wdev, const void *context)
{
	struct uniwill_data *data;
	struct regmap *regmap;
	int ret;

	data = devm_kzalloc(&wdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->wdev = wdev;
	dev_set_drvdata(&wdev->dev, data);

	regmap = devm_regmap_init(&wdev->dev, &uniwill_ec_bus, data, &uniwill_ec_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	data->regmap = regmap;

	ret = uniwill_ec_init(data);
	if (ret < 0)
		return ret;

	ret = uniwill_platform_profile_init(data);
	if (ret < 0)
		return ret;

	return uniwill_hwmon_init(data);
}

static int uniwill_suspend(struct device *dev)
{
	struct uniwill_data *data = dev_get_drvdata(dev);
	unsigned int value;
	int ret;

	/*
	 * Make sure the EC_ADDR_AP_OEM register in the regmap cache is current
	 * before bypassing it.
	 */
	ret = regmap_read(data->regmap, EC_ADDR_AP_OEM, &value);
	if (ret < 0)
		return ret;

	regcache_cache_bypass(data->regmap, true);
	regmap_update_bits(data->regmap, EC_ADDR_AP_OEM, ENABLE_MANUAL_CTRL, 0);
	regcache_cache_bypass(data->regmap, false);

	regcache_cache_only(data->regmap, true);
	regcache_mark_dirty(data->regmap);

	return 0;
}

static int uniwill_resume(struct device *dev)
{
	struct uniwill_data *data = dev_get_drvdata(dev);

	regcache_cache_only(data->regmap, false);

	return regcache_sync(data->regmap);
}

static DEFINE_SIMPLE_DEV_PM_OPS(uniwill_pm_ops, uniwill_suspend, uniwill_resume);

/*
 * We cannot fully trust this GUID since Uniwill just copied the WMI GUID
 * from the Windows driver example, and others probably did the same.
 *
 * Because of this we are forced to use a DMI table for autoloading.
 */
static const struct wmi_device_id uniwill_id_table[] = {
	{ UNIWILL_GUID, NULL },
	{ }
};

static struct wmi_driver uniwill_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.pm = pm_sleep_ptr(&uniwill_pm_ops),
	},
	.id_table = uniwill_id_table,
	.probe = uniwill_probe,
	.no_singleton = true,
};
module_wmi_driver(uniwill_driver);	// TODO DMI

MODULE_AUTHOR("Armin Wolf <W_Armin@gmx.de>");
MODULE_DESCRIPTION("Uniwill notebook driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(UNIWILL);
