/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Linux driver for Uniwill notebooks.
 *
 * Copyright (C) 2025 Armin Wolf <W_Armin@gmx.de>
 */

#ifndef UNIWILL_INTERNAL_H
#define UNIWILL_INTERNAL_H

#include <linux/bits.h>
#include <linux/init.h>

/* For backwards compatibility */
#define UNIWILL_EC_DRIVER_NAME	"uniwill"

struct device;
struct regmap;
struct regmap_bus;
struct notifier_block;
struct attribute_group;

/* Device configuration */

#define UNIWILL_FEATURE_FN_LOCK			BIT(0)
#define UNIWILL_FEATURE_SUPER_KEY		BIT(1)
#define UNIWILL_FEATURE_TOUCHPAD_TOGGLE		BIT(2)
#define UNIWILL_FEATURE_LIGHTBAR		BIT(3)
#define UNIWILL_FEATURE_BATTERY_CHARGE_LIMIT	BIT(4)
/* Mutually exclusive with the charge limit feature */
#define UNIWILL_FEATURE_BATTERY_CHARGE_MODES	BIT(5)
#define UNIWILL_FEATURE_CPU_TEMP		BIT(6)
#define UNIWILL_FEATURE_GPU_TEMP		BIT(7)
#define UNIWILL_FEATURE_PRIMARY_FAN		BIT(8)
#define UNIWILL_FEATURE_SECONDARY_FAN		BIT(9)
#define UNIWILL_FEATURE_NVIDIA_CTGP_CONTROL	BIT(10)
#define UNIWILL_FEATURE_USB_C_POWER_PRIORITY	BIT(11)
#define UNIWILL_FEATURE_KEYBOARD_BACKLIGHT	BIT(12)
#define UNIWILL_FEATURE_AC_AUTO_BOOT		BIT(13)
#define UNIWILL_FEATURE_USB_POWERSHARE		BIT(14)

struct uniwill_device_config {
	unsigned int features;
	u8 kbd_led_max_brightness;
	u8 lightbar_max_brightness;
};

struct uniwill_device_descriptor {
	struct uniwill_device_config config;
	/* Executed during driver probing */
	int (*probe)(struct regmap *regmap, u8 project_id, struct uniwill_device_config *config);
};

int uniwill_dmi_match(struct uniwill_device_descriptor *descriptor) __init;

/* Generic EC handling */

extern const struct attribute_group *uniwill_groups[];

int uniwill_probe(struct device *dev, const struct regmap_bus *bus, void *bus_context);

int uniwill_suspend(struct device *dev);

int uniwill_resume(struct device *dev);

void uniwill_shutdown(struct device *dev);

/* ACPI backend */

bool uniwill_ec_acpi_interface_available(void) __init;

int uniwill_ec_register_platform_driver(void) __init;

void uniwill_ec_unregister_platform_driver(void);

/* WMI backend */

int uniwill_ec_register_wmi_driver(void) __init;

void uniwill_ec_unregister_wmi_driver(void);

/* Event handling */

int devm_uniwill_wmi_register_notifier(struct device *dev, struct notifier_block *nb);

int uniwill_wmi_register_driver(void) __init;

void uniwill_wmi_unregister_driver(void) __exit;

#endif	/* UNIWILL_INTERNAL_H */
