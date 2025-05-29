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
#include <linux/cleanup.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/device/driver.h>
#include <linux/dmi.h>
#include <linux/errno.h>
#include <linux/fixp-arith.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kstrtox.h>
#include <linux/leds.h>
#include <linux/led-class-multicolor.h>
#include <linux/limits.h>
#include <linux/list.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/pm.h>
#include <linux/printk.h>
#include <linux/regmap.h>
#include <linux/string.h>
#include <linux/string_choices.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include <linux/unaligned.h>
#include <linux/units.h>
#include <linux/wmi.h>

#include <acpi/battery.h>

#include "uniwill-wmi.h"

#define EC_ADDR_BAT_POWER_UNIT_1	0x0400

#define EC_ADDR_BAT_POWER_UNIT_2	0x0401

#define EC_ADDR_BAT_DESIGN_CAPACITY_1	0x0402

#define EC_ADDR_BAT_DESIGN_CAPACITY_2	0x0403

#define EC_ADDR_BAT_FULL_CAPACITY_1	0x0404

#define EC_ADDR_BAT_FULL_CAPACITY_2	0x0405

#define EC_ADDR_BAT_DESIGN_VOLTAGE_1	0x0408

#define EC_ADDR_BAT_DESIGN_VOLTAGE_2	0x0409

#define EC_ADDR_BAT_STATUS_1		0x0432
#define BAT_DISCHARGING			BIT(0)

#define EC_ADDR_BAT_STATUS_2		0x0433

#define EC_ADDR_BAT_CURRENT_1		0x0434

#define EC_ADDR_BAT_CURRENT_2		0x0435

#define EC_ADDR_BAT_REMAIN_CAPACITY_1	0x0436

#define EC_ADDR_BAT_REMAIN_CAPACITY_2	0x0437

#define EC_ADDR_BAT_VOLTAGE_1		0x0438

#define EC_ADDR_BAT_VOLTAGE_2		0x0439

#define EC_ADDR_CPU_TEMP		0x043E

#define EC_ADDR_GPU_TEMP		0x044F

#define EC_ADDR_MAIN_FAN_RPM_1		0x0464

#define EC_ADDR_MAIN_FAN_RPM_2		0x0465

#define EC_ADDR_SECOND_FAN_RPM_1	0x046C

#define EC_ADDR_SECOND_FAN_RPM_2	0x046D

#define EC_ADDR_DEVICE_STATUS		0x047B
#define WIFI_STATUS_ON			BIT(7)
/* BIT(5) is also unset depending on the rfkill state (bluetooth?) */

#define EC_ADDR_BAT_ALERT		0x0494

#define EC_ADDR_BAT_CYCLE_COUNT_1	0x04A6

#define EC_ADDR_BAT_CYCLE_COUNT_2	0x04A7

#define EC_ADDR_PROJECT_ID		0x0740

#define EC_ADDR_AP_OEM			0x0741
#define	ENABLE_MANUAL_CTRL		BIT(0)
#define ITE_KBD_EFFECT_REACTIVE		BIT(3)
#define FAN_ABNORMAL			BIT(5)

#define EC_ADDR_SUPPORT_5		0x0742
#define FAN_TURBO_SUPPORTED		BIT(4)
#define FAN_SUPPORT			BIT(5)

#define EC_ADDR_CTGP_DB_CTRL		0x0743
#define CTGP_DB_GENERAL_ENABLE		BIT(0)
#define CTGP_DB_DB_ENABLE		BIT(1)
#define CTGP_DB_CTGP_ENABLE		BIT(2)

#define EC_ADDR_CTGP_OFFSET		0x0744

#define EC_ADDR_TPP_OFFSET		0x0745

#define EC_ADDR_MAX_TGP			0x0746

#define EC_ADDR_LIGHTBAR_AC_CTRL	0x0748
#define LIGHTBAR_APP_EXISTS		BIT(0)
#define LIGHTBAR_POWER_SAVE		BIT(1)
#define LIGHTBAR_S0_OFF			BIT(2)
#define LIGHTBAR_S3_OFF			BIT(3)	// Breathing animation when suspended
#define LIGHTBAR_WELCOME		BIT(7)	// Rainbow animation

#define EC_ADDR_LIGHTBAR_AC_RED		0x0749

#define EC_ADDR_LIGHTBAR_AC_GREEN	0x074A

#define EC_ADDR_LIGHTBAR_AC_BLUE	0x074B

#define EC_ADDR_BIOS_OEM		0x074E
#define FN_LOCK_STATUS			BIT(4)

#define EC_ADDR_MANUAL_FAN_CTRL		0x0751
#define FAN_LEVEL_MASK			GENMASK(2, 0)
#define FAN_MODE_TURBO			BIT(4)
#define FAN_MODE_HIGH			BIT(5)
#define FAN_MODE_BOOST			BIT(6)
#define FAN_MODE_USER			BIT(7)

#define EC_ADDR_PWM_1			0x075B

#define EC_ADDR_PWM_2			0x075C

/* Unreliable */
#define EC_ADDR_SUPPORT_1		0x0765
#define AIRPLANE_MODE			BIT(0)
#define GPS_SWITCH			BIT(1)
#define OVERCLOCK			BIT(2)
#define MACRO_KEY			BIT(3)
#define SHORTCUT_KEY			BIT(4)
#define SUPER_KEY_LOCK			BIT(5)
#define LIGHTBAR			BIT(6)
#define FAN_BOOST			BIT(7)

#define EC_ADDR_SUPPORT_2		0x0766
#define SILENT_MODE			BIT(0)
#define USB_CHARGING			BIT(1)
#define RGB_KEYBOARD			BIT(2)
#define CHINA_MODE			BIT(5)
#define MY_BATTERY			BIT(6)

#define EC_ADDR_TRIGGER			0x0767
#define TRIGGER_SUPER_KEY_LOCK		BIT(0)
#define TRIGGER_LIGHTBAR		BIT(1)
#define TRIGGER_FAN_BOOST		BIT(2)
#define TRIGGER_SILENT_MODE		BIT(3)
#define TRIGGER_USB_CHARGING		BIT(4)
#define RGB_APPLY_COLOR			BIT(5)
#define RGB_LOGO_EFFECT			BIT(6)
#define RGB_RAINBOW_EFFECT		BIT(7)

#define EC_ADDR_SWITCH_STATUS		0x0768
#define SUPER_KEY_LOCK_STATUS		BIT(0)
#define LIGHTBAR_STATUS			BIT(1)
#define FAN_BOOST_STATUS		BIT(2)
#define MACRO_KEY_STATUS		BIT(3)
#define MY_BAT_POWER_BAT_STATUS		BIT(4)

#define EC_ADDR_RGB_RED			0x0769

#define EC_ADDR_RGB_GREEN		0x076A

#define EC_ADDR_RGB_BLUE		0x076B

#define EC_ADDR_ROMID_START		0x0770
#define ROMID_LENGTH			14

#define EC_ADDR_ROMID_EXTRA_1		0x077E

#define EC_ADDR_ROMID_EXTRA_2		0x077F

#define EC_ADDR_BIOS_OEM_2		0x0782
#define FAN_V2_NEW			BIT(0)
#define FAN_QKEY			BIT(1)
#define FAN_TABLE_OFFICE_MODE		BIT(2)
#define FAN_V3				BIT(3)
#define DEFAULT_MODE			BIT(4)

#define EC_ADDR_PL1_SETTING		0x0783

#define EC_ADDR_PL2_SETTING		0x0784

#define EC_ADDR_PL4_SETTING		0x0785

#define EC_ADDR_FAN_DEFAULT		0x0786
#define FAN_CURVE_LENGTH		5

#define EC_ADDR_KBD_STATUS		0x078C
#define KBD_WHITE_ONLY			BIT(0)	// ~single color
#define KBD_SINGLE_COLOR_OFF		BIT(1)
#define KBD_TURBO_LEVEL_MASK		GENMASK(3, 2)
#define KBD_APPLY			BIT(4)
#define KBD_BRIGHTNESS			GENMASK(7, 5)

#define EC_ADDR_FAN_CTRL		0x078E
#define FAN3P5				BIT(1)
#define CHARGING_PROFILE		BIT(3)
#define UNIVERSAL_FAN_CTRL		BIT(6)

#define EC_ADDR_BIOS_OEM_3		0x07A3
#define FAN_REDUCED_DURY_CYCLE		BIT(5)
#define FAN_ALWAYS_ON			BIT(6)

#define EC_ADDR_BIOS_BYTE		0x07A4
#define FN_LOCK_SWITCH			BIT(3)

#define EC_ADDR_OEM_3			0x07A5
#define POWER_LED_MASK			GENMASK(1, 0)
#define POWER_LED_LEFT			0x00
#define POWER_LED_BOTH			0x01
#define POWER_LED_NONE			0x02
#define FAN_QUIET			BIT(2)
#define OVERBOOST			BIT(4)
#define HIGH_POWER			BIT(7)

#define EC_ADDR_OEM_4			0x07A6
#define OVERBOOST_DYN_TEMP_OFF		BIT(1)
#define TOUCHPAD_TOGGLE_OFF		BIT(6)

#define EC_ADDR_CHARGE_CTRL		0x07B9
#define CHARGE_CTRL_MASK		GENMASK(6, 0)
#define CHARGE_CTRL_REACHED		BIT(7)

#define EC_ADDR_UNIVERSAL_FAN_CTRL	0x07C5
#define SPLIT_TABLES			BIT(7)

#define EC_ADDR_AP_OEM_6		0x07C6
#define ENABLE_UNIVERSAL_FAN_CTRL	BIT(2)
#define BATTERY_CHARGE_FULL_OVER_24H	BIT(3)
#define BATTERY_ERM_STATUS_REACHED	BIT(4)

#define EC_ADDR_CHARGE_PRIO		0x07CC
#define CHARGING_PERFORMANCE		BIT(7)

/* Same bits as EC_ADDR_LIGHTBAR_AC_CTRL except LIGHTBAR_S3_OFF */
#define EC_ADDR_LIGHTBAR_BAT_CTRL	0x07E2

#define EC_ADDR_LIGHTBAR_BAT_RED	0x07E3

#define EC_ADDR_LIGHTBAR_BAT_GREEN	0x07E4

#define EC_ADDR_LIGHTBAR_BAT_BLUE	0x07E5

#define EC_ADDR_CPU_TEMP_END_TABLE	0x0F00

#define EC_ADDR_CPU_TEMP_START_TABLE	0x0F10

#define EC_ADDR_CPU_FAN_SPEED_TABLE	0x0F20

#define EC_ADDR_GPU_TEMP_END_TABLE	0x0F30

#define EC_ADDR_GPU_TEMP_START_TABLE	0x0F40

#define EC_ADDR_GPU_FAN_SPEED_TABLE	0x0F50

/*
 * Those two registers technically allow for manual fan control,
 * but are unstable on some models and are likely not meant to
 * be used by applications.
 */
#define EC_ADDR_PWM_1_WRITEABLE		0x1804

#define EC_ADDR_PWM_2_WRITEABLE		0x1809

#define DRIVER_NAME	"uniwill"
#define UNIWILL_GUID	"ABBC0F6F-8EA1-11D1-00A0-C90629100000"

#define PWM_MAX			200
#define FAN_TABLE_LENGTH	16

#define LED_CHANNELS	3

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
	struct acpi_battery_hook hook;
	unsigned int last_charge_limit;
	struct mutex battery_lock;	/* Protects the list of currently registered batteries */
	unsigned int last_switch_status;
	struct mutex super_key_lock;	/* Protects the toggling of the super key lock state */
	struct list_head batteries;
	struct led_classdev_mc led_mc_cdev;
	struct mc_subled led_mc_subled_info[LED_CHANNELS];
	struct notifier_block nb;
};

struct uniwill_battery_entry {
	struct list_head head;
	struct power_supply *battery;
};

static bool force;
module_param_unsafe(force, bool, 0);
MODULE_PARM_DESC(force, "Force loading without checking for supported devices\n");

/*
 * "disable" is placed on index 0 so that the return value of sysfs_match_string()
 * directly translates into a boolean value.
 */
static const char * const uniwill_enable_disable_strings[] = {
	[0] = "disable",
	[1] = "enable",
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
	case EC_ADDR_LIGHTBAR_AC_CTRL:
	case EC_ADDR_LIGHTBAR_AC_RED:
	case EC_ADDR_LIGHTBAR_AC_GREEN:
	case EC_ADDR_LIGHTBAR_AC_BLUE:
	case EC_ADDR_BIOS_OEM:
	case EC_ADDR_TRIGGER:
	case EC_ADDR_OEM_4:
	case EC_ADDR_CHARGE_CTRL:
	case EC_ADDR_LIGHTBAR_BAT_CTRL:
	case EC_ADDR_LIGHTBAR_BAT_RED:
	case EC_ADDR_LIGHTBAR_BAT_GREEN:
	case EC_ADDR_LIGHTBAR_BAT_BLUE:
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
	case EC_ADDR_BAT_ALERT:
	case EC_ADDR_PROJECT_ID:
	case EC_ADDR_AP_OEM:
	case EC_ADDR_LIGHTBAR_AC_CTRL:
	case EC_ADDR_LIGHTBAR_AC_RED:
	case EC_ADDR_LIGHTBAR_AC_GREEN:
	case EC_ADDR_LIGHTBAR_AC_BLUE:
	case EC_ADDR_BIOS_OEM:
	case EC_ADDR_PWM_1:
	case EC_ADDR_PWM_2:
	case EC_ADDR_TRIGGER:
	case EC_ADDR_SWITCH_STATUS:
	case EC_ADDR_OEM_4:
	case EC_ADDR_CHARGE_CTRL:
	case EC_ADDR_LIGHTBAR_BAT_CTRL:
	case EC_ADDR_LIGHTBAR_BAT_RED:
	case EC_ADDR_LIGHTBAR_BAT_GREEN:
	case EC_ADDR_LIGHTBAR_BAT_BLUE:
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
	case EC_ADDR_BAT_ALERT:
	case EC_ADDR_PWM_1:
	case EC_ADDR_PWM_2:
	case EC_ADDR_TRIGGER:
	case EC_ADDR_SWITCH_STATUS:
	case EC_ADDR_CHARGE_CTRL:
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
	.max_register = 0xFFFF,
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
		return 0444;
	default:
		return 0;
	}
}

static ssize_t fn_lock_store(struct device *dev, struct device_attribute *attr, const char *buf,
			     size_t count)
{
	struct uniwill_data *data = dev_get_drvdata(dev);
	unsigned int value;
	int ret;

	ret = sysfs_match_string(uniwill_enable_disable_strings, buf);
	if (ret < 0)
		return ret;

	if (ret)
		value = FN_LOCK_STATUS;
	else
		value = 0;

	ret = regmap_update_bits(data->regmap, EC_ADDR_BIOS_OEM, FN_LOCK_STATUS, value);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t fn_lock_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct uniwill_data *data = dev_get_drvdata(dev);
	unsigned int value;
	int ret;

	ret = regmap_read(data->regmap, EC_ADDR_BIOS_OEM, &value);
	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%s\n", str_enable_disable(value & FN_LOCK_STATUS));
}

static DEVICE_ATTR_RW(fn_lock);

static ssize_t super_key_lock_store(struct device *dev, struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct uniwill_data *data = dev_get_drvdata(dev);
	unsigned int value;
	int ret;

	ret = sysfs_match_string(uniwill_enable_disable_strings, buf);
	if (ret < 0)
		return ret;

	guard(mutex)(&data->super_key_lock);

	ret = regmap_read(data->regmap, EC_ADDR_SWITCH_STATUS, &value);
	if (ret < 0)
		return ret;

	/*
	 * We can only toggle the super key lock, so we return early if the setting
	 * is already in the correct state.
	 */
	if (ret == !(value & SUPER_KEY_LOCK_STATUS))
		return count;

	ret = regmap_write_bits(data->regmap, EC_ADDR_TRIGGER, TRIGGER_SUPER_KEY_LOCK,
				TRIGGER_SUPER_KEY_LOCK);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t super_key_lock_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct uniwill_data *data = dev_get_drvdata(dev);
	unsigned int value;
	int ret;

	ret = regmap_read(data->regmap, EC_ADDR_SWITCH_STATUS, &value);
	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%s\n", str_enable_disable(!(value & SUPER_KEY_LOCK_STATUS)));
}

static DEVICE_ATTR_RW(super_key_lock);

static ssize_t touchpad_toggle_store(struct device *dev, struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct uniwill_data *data = dev_get_drvdata(dev);
	unsigned int value;
	int ret;

	ret = sysfs_match_string(uniwill_enable_disable_strings, buf);
	if (ret < 0)
		return ret;

	if (ret)
		value = 0;
	else
		value = TOUCHPAD_TOGGLE_OFF;

	ret = regmap_update_bits(data->regmap, EC_ADDR_OEM_4, TOUCHPAD_TOGGLE_OFF, value);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t touchpad_toggle_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct uniwill_data *data = dev_get_drvdata(dev);
	unsigned int value;
	int ret;

	ret = regmap_read(data->regmap, EC_ADDR_OEM_4, &value);
	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%s\n", str_enable_disable(!(value & TOUCHPAD_TOGGLE_OFF)));
}

static DEVICE_ATTR_RW(touchpad_toggle);

static struct attribute *uniwill_attrs[] = {
	&dev_attr_fn_lock.attr,
	&dev_attr_super_key_lock.attr,
	&dev_attr_touchpad_toggle.attr,
	NULL
};
ATTRIBUTE_GROUPS(uniwill);

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

static const struct hwmon_ops uniwill_ops = {
	.is_visible = uniwill_is_visible,
	.read = uniwill_read,
	.read_string = uniwill_read_string,
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
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT),
	NULL
};

static const struct hwmon_chip_info uniwill_chip_info = {
	.ops = &uniwill_ops,
	.info = uniwill_info,
};

static int uniwill_hwmon_init(struct uniwill_data *data)
{
	struct device *hdev;

	hdev = devm_hwmon_device_register_with_info(&data->wdev->dev, "uniwill", data,
						    &uniwill_chip_info, NULL);

	return PTR_ERR_OR_ZERO(hdev);
}

static const unsigned int uniwill_led_channel_to_bat_reg[LED_CHANNELS] = {
	EC_ADDR_LIGHTBAR_BAT_RED,
	EC_ADDR_LIGHTBAR_BAT_GREEN,
	EC_ADDR_LIGHTBAR_BAT_BLUE,
};

static const unsigned int uniwill_led_channel_to_ac_reg[LED_CHANNELS] = {
	EC_ADDR_LIGHTBAR_AC_RED,
	EC_ADDR_LIGHTBAR_AC_GREEN,
	EC_ADDR_LIGHTBAR_AC_BLUE,
};

static int uniwill_led_brightness_set(struct led_classdev *led_cdev, enum led_brightness brightness)
{
	struct led_classdev_mc *led_mc_cdev = lcdev_to_mccdev(led_cdev);
	struct uniwill_data *data = container_of(led_mc_cdev, struct uniwill_data, led_mc_cdev);
	unsigned int value;
	int ret;

	ret = led_mc_calc_color_components(led_mc_cdev, brightness);
	if (ret < 0)
		return ret;

	for (int i = 0; i < LED_CHANNELS; i++) {
		/* Prevent the brightness values from overflowing */
		value = min(U8_MAX, data->led_mc_subled_info[i].brightness);
		ret = regmap_write(data->regmap, uniwill_led_channel_to_ac_reg[i], value);
		if (ret < 0)
			return ret;

		ret = regmap_write(data->regmap, uniwill_led_channel_to_bat_reg[i], value);
		if (ret < 0)
			return ret;
	}

	if (brightness)
		value = 0;
	else
		value = LIGHTBAR_S0_OFF;

	ret = regmap_update_bits(data->regmap, EC_ADDR_LIGHTBAR_AC_CTRL, LIGHTBAR_S0_OFF, value);
	if (ret < 0)
		return ret;

	return regmap_update_bits(data->regmap, EC_ADDR_LIGHTBAR_BAT_CTRL, LIGHTBAR_S0_OFF, value);
}

#define LIGHTBAR_MASK	(LIGHTBAR_APP_EXISTS | LIGHTBAR_S0_OFF | LIGHTBAR_S3_OFF | LIGHTBAR_WELCOME)

static int uniwill_led_init(struct uniwill_data *data)
{
	struct led_init_data init_data = {
		.devicename = DRIVER_NAME,
		.default_label = "multicolor:" LED_FUNCTION_STATUS,
		.devname_mandatory = true,
	};
	unsigned int color_indices[3] = {
		LED_COLOR_ID_RED,
		LED_COLOR_ID_GREEN,
		LED_COLOR_ID_BLUE,
	};
	unsigned int value;
	int ret;

	/*
	 * The EC has separate lightbar settings for AC and battery mode,
	 * so we have to ensure that both settings are the same.
	 */
	ret = regmap_read(data->regmap, EC_ADDR_LIGHTBAR_AC_CTRL, &value);
	if (ret < 0)
		return ret;

	/*
	 * We currently do not support the two animation modes, so we need to
	 * disable both here.
	 */
	value |= LIGHTBAR_APP_EXISTS | LIGHTBAR_S3_OFF;
	value &= ~LIGHTBAR_WELCOME;
	ret = regmap_write(data->regmap, EC_ADDR_LIGHTBAR_AC_CTRL, value);
	if (ret < 0)
		return ret;

	ret = regmap_update_bits(data->regmap, EC_ADDR_LIGHTBAR_BAT_CTRL, LIGHTBAR_MASK, value);
	if (ret < 0)
		return ret;

	data->led_mc_cdev.led_cdev.color = LED_COLOR_ID_MULTI;
	data->led_mc_cdev.led_cdev.max_brightness = U8_MAX;
	data->led_mc_cdev.led_cdev.flags = LED_REJECT_NAME_CONFLICT;
	data->led_mc_cdev.led_cdev.brightness_set_blocking = uniwill_led_brightness_set;

	if (value & LIGHTBAR_S0_OFF)
		data->led_mc_cdev.led_cdev.brightness = 0;
	else
		data->led_mc_cdev.led_cdev.brightness = U8_MAX;

	for (int i = 0; i < LED_CHANNELS; i++) {
		data->led_mc_subled_info[i].color_index = color_indices[i];

		ret = regmap_read(data->regmap, uniwill_led_channel_to_ac_reg[i], &value);
		if (ret < 0)
			return ret;

		ret = regmap_write(data->regmap, uniwill_led_channel_to_bat_reg[i], value);
		if (ret < 0)
			return ret;

		data->led_mc_subled_info[i].intensity = value;
		data->led_mc_subled_info[i].channel = i;
	}

	data->led_mc_cdev.subled_info = data->led_mc_subled_info;
	data->led_mc_cdev.num_colors = LED_CHANNELS;

	return devm_led_classdev_multicolor_register_ext(&data->wdev->dev, &data->led_mc_cdev,
							 &init_data);
}

static int uniwill_get_property(struct power_supply *psy, const struct power_supply_ext *ext,
				void *drvdata, enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct uniwill_data *data = drvdata;
	union power_supply_propval prop;
	unsigned int regval;
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_HEALTH:
		ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_PRESENT, &prop);
		if (ret < 0)
			return ret;

		if (!prop.intval) {
			val->intval = POWER_SUPPLY_HEALTH_NO_BATTERY;
			return 0;
		}

		ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_STATUS, &prop);
		if (ret < 0)
			return ret;

		if (prop.intval == POWER_SUPPLY_STATUS_UNKNOWN) {
			val->intval = POWER_SUPPLY_HEALTH_UNKNOWN;
			return 0;
		}

		ret = regmap_read(data->regmap, EC_ADDR_BAT_ALERT, &regval);
		if (ret < 0)
			return ret;

		if (regval) {
			/* Charging issue */
			val->intval = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
			return 0;
		}

		val->intval = POWER_SUPPLY_HEALTH_GOOD;
		return 0;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_END_THRESHOLD:
		ret = regmap_read(data->regmap, EC_ADDR_CHARGE_CTRL, &regval);
		if (ret < 0)
			return ret;

		val->intval = clamp_val(FIELD_GET(CHARGE_CTRL_MASK, regval), 0, 100);
		return 0;
	default:
		return -EINVAL;
	}
}

static int uniwill_set_property(struct power_supply *psy, const struct power_supply_ext *ext,
				void *drvdata, enum power_supply_property psp,
				const union power_supply_propval *val)
{
	struct uniwill_data *data = drvdata;

	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_END_THRESHOLD:
		if (val->intval < 1 || val->intval > 100)
			return -EINVAL;

		return regmap_update_bits(data->regmap, EC_ADDR_CHARGE_CTRL, CHARGE_CTRL_MASK,
					  val->intval);
	default:
		return -EINVAL;
	}
}

static int uniwill_property_is_writeable(struct power_supply *psy,
					 const struct power_supply_ext *ext, void *drvdata,
					 enum power_supply_property psp)
{
	if (psp == POWER_SUPPLY_PROP_CHARGE_CONTROL_END_THRESHOLD)
		return true;

	return false;
}

static const enum power_supply_property uniwill_properties[] = {
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_END_THRESHOLD,
};

static const struct power_supply_ext uniwill_extension = {
	.name = DRIVER_NAME,
	.properties = uniwill_properties,
	.num_properties = ARRAY_SIZE(uniwill_properties),
	.get_property = uniwill_get_property,
	.set_property = uniwill_set_property,
	.property_is_writeable = uniwill_property_is_writeable,
};

static int uniwill_add_battery(struct power_supply *battery, struct acpi_battery_hook *hook)
{
	struct uniwill_data *data = container_of(hook, struct uniwill_data, hook);
	struct uniwill_battery_entry *entry;
	int ret;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	ret = power_supply_register_extension(battery, &uniwill_extension, &data->wdev->dev, data);
	if (ret < 0) {
		kfree(entry);
		return ret;
	}

	scoped_guard(mutex, &data->battery_lock) {
		entry->battery = battery;
		list_add(&entry->head, &data->batteries);
	}

	return 0;
}

static int uniwill_remove_battery(struct power_supply *battery, struct acpi_battery_hook *hook)
{
	struct uniwill_data *data = container_of(hook, struct uniwill_data, hook);
	struct uniwill_battery_entry *entry, *tmp;

	scoped_guard(mutex, &data->battery_lock) {
		list_for_each_entry_safe(entry, tmp, &data->batteries, head) {
			if (entry->battery == battery) {
				list_del(&entry->head);
				kfree(entry);
				break;
			}
		}
	}

	power_supply_unregister_extension(battery, &uniwill_extension);

	return 0;
}

static int uniwill_battery_init(struct uniwill_data *data)
{
	int ret;

	ret = devm_mutex_init(&data->wdev->dev, &data->battery_lock);
	if (ret < 0)
		return ret;

	INIT_LIST_HEAD(&data->batteries);
	data->hook.name = "Uniwill Battery Extension";
	data->hook.add_battery = uniwill_add_battery;
	data->hook.remove_battery = uniwill_remove_battery;

	return devm_battery_hook_register(&data->wdev->dev, &data->hook);
}

static int uniwill_notifier_call(struct notifier_block *nb, unsigned long action, void *dummy)
{
	struct uniwill_data *data = container_of(nb, struct uniwill_data, nb);
	struct uniwill_battery_entry *entry;

	switch (action) {
	case UNIWILL_OSD_BATTERY_ALERT:
		scoped_guard(mutex, &data->battery_lock) {
			list_for_each_entry(entry, &data->batteries, head) {
				power_supply_changed(entry->battery);
			}
		}

		return NOTIFY_OK;
	default:
		return NOTIFY_DONE;
	}
}

static int uniwill_notifier_init(struct uniwill_data *data)
{
	data->nb.notifier_call = uniwill_notifier_call;

	return devm_uniwill_wmi_register_notifier(&data->wdev->dev, &data->nb);
}

static void uniwill_disable_manual_control(void *context)
{
	struct uniwill_data *data = context;

	regmap_clear_bits(data->regmap, EC_ADDR_AP_OEM, ENABLE_MANUAL_CTRL);
}

static int uniwill_ec_init(struct uniwill_data *data)
{
	unsigned int value;
	int ret;

	ret = regmap_read(data->regmap, EC_ADDR_PROJECT_ID, &value);
	if (ret < 0)
		return ret;

	dev_dbg(&data->wdev->dev, "Project ID: %u\n", value);

	ret = regmap_set_bits(data->regmap, EC_ADDR_AP_OEM, ENABLE_MANUAL_CTRL);
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
	ret = devm_mutex_init(&wdev->dev, &data->super_key_lock);
	if (ret < 0)
		return ret;

	ret = uniwill_ec_init(data);
	if (ret < 0)
		return ret;

	ret = uniwill_battery_init(data);
	if (ret < 0)
		return ret;

	ret = uniwill_led_init(data);
	if (ret < 0)
		return ret;

	ret = uniwill_hwmon_init(data);
	if (ret < 0)
		return ret;

	return uniwill_notifier_init(data);
}

static void uniwill_shutdown(struct wmi_device *wdev)
{
	struct uniwill_data *data = dev_get_drvdata(&wdev->dev);

	regmap_clear_bits(data->regmap, EC_ADDR_AP_OEM, ENABLE_MANUAL_CTRL);
}

static int uniwill_suspend(struct device *dev)
{
	struct uniwill_data *data = dev_get_drvdata(dev);
	unsigned int value;
	int ret;

	/*
	 * The EC_ADDR_SWITCH_STATUS is maked as volatile, so we have to restore it
	 * ourself.
	 */
	ret = regmap_read(data->regmap, EC_ADDR_SWITCH_STATUS, &data->last_switch_status);
	if (ret < 0)
		return ret;

	/*
	 * Save the current charge limit in order to restore it during resume.
	 * We cannot use the regmap code for that since this register needs to
	 * be declared as volatile due to CHARGE_CTRL_REACHED.
	 */
	ret = regmap_read(data->regmap, EC_ADDR_CHARGE_CTRL, &value);
	if (ret < 0)
		return ret;

	data->last_charge_limit = FIELD_GET(CHARGE_CTRL_MASK, value);

	regcache_cache_only(data->regmap, true);
	regcache_mark_dirty(data->regmap);

	return 0;
}

static int uniwill_resume(struct device *dev)
{
	struct uniwill_data *data = dev_get_drvdata(dev);
	unsigned int value;
	int ret;

	regcache_cache_only(data->regmap, false);

	ret = regcache_sync(data->regmap);
	if (ret < 0)
		return ret;

	ret = regmap_update_bits(data->regmap, EC_ADDR_CHARGE_CTRL, CHARGE_CTRL_MASK,
				 data->last_charge_limit);
	if (ret < 0)
		return ret;

	ret = regmap_read(data->regmap, EC_ADDR_SWITCH_STATUS, &value);
	if (ret < 0)
		return ret;

	if ((data->last_switch_status & SUPER_KEY_LOCK_STATUS) == (value & SUPER_KEY_LOCK_STATUS))
		return 0;

	return regmap_write_bits(data->regmap, EC_ADDR_TRIGGER, TRIGGER_SUPER_KEY_LOCK,
				 TRIGGER_SUPER_KEY_LOCK);
}

static DEFINE_SIMPLE_DEV_PM_OPS(uniwill_pm_ops, uniwill_suspend, uniwill_resume);

/*
 * We cannot fully trust this GUID since Uniwill just copied the WMI GUID
 * from the Windows driver example, and others probably did the same.
 *
 * Because of this we cannot use this WMI GUID for autoloading.
 */
static const struct wmi_device_id uniwill_id_table[] = {
	{ UNIWILL_GUID, NULL },
	{ }
};

static struct wmi_driver uniwill_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.dev_groups = uniwill_groups,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.pm = pm_sleep_ptr(&uniwill_pm_ops),
	},
	.id_table = uniwill_id_table,
	.probe = uniwill_probe,
	.shutdown = uniwill_shutdown,
	.no_singleton = true,
};

static const struct dmi_system_id uniwill_dmi_table[] __initconst = {
	{
		.ident = "Intel NUC x15",
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Intel(R) Client Systems"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "LAPAC71H"),
		},
	},
	{
		.ident = "Intel NUC x15",
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Intel(R) Client Systems"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "LAPKC71F"),
		},
	},
	{ }
};
MODULE_DEVICE_TABLE(dmi, uniwill_dmi_table);

static int __init uniwill_init(void)
{
	if (!dmi_first_match(uniwill_dmi_table)) {
		if (!force)
			return -ENODEV;

		pr_warn("Loading on a potentially unsupported device\n");
	}

	return wmi_driver_register(&uniwill_driver);
}
module_init(uniwill_init);

static void __exit uniwill_exit(void)
{
	wmi_driver_unregister(&uniwill_driver);
}
module_exit(uniwill_exit);

MODULE_AUTHOR("Armin Wolf <W_Armin@gmx.de>");
MODULE_DESCRIPTION("Uniwill notebook driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("UNIWILL");
