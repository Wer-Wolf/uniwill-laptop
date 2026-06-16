// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * DMI matching logic.
 *
 * Special thanks go to Pőcze Barnabás, Christoffer Sandberg and Werner Sembach
 * for supporting the development of this driver either through prior work or
 * by answering questions regarding the underlying ACPI and WMI interfaces.
 *
 * Copyright (C) 2025 Armin Wolf <W_Armin@gmx.de>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/dmi.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/regmap.h>
#include <linux/types.h>

#include "internal.h"
#include "registers.h"

static bool force;
module_param_unsafe(force, bool, 0);
MODULE_PARM_DESC(force, "Force loading without checking for supported devices\n");

static struct uniwill_device_descriptor machenike_l16p_descriptor __initdata = {
	.config = {
		.features = UNIWILL_FEATURE_FN_LOCK |
			    UNIWILL_FEATURE_SUPER_KEY |
			    UNIWILL_FEATURE_CPU_TEMP |
			    UNIWILL_FEATURE_GPU_TEMP |
			    UNIWILL_FEATURE_PRIMARY_FAN |
			    UNIWILL_FEATURE_SECONDARY_FAN |
			    UNIWILL_FEATURE_NVIDIA_CTGP_CONTROL |
			    UNIWILL_FEATURE_KEYBOARD_BACKLIGHT |
			    UNIWILL_FEATURE_AC_AUTO_BOOT |
			    UNIWILL_FEATURE_USB_POWERSHARE,
		.kbd_led_max_brightness = 4,
	},
};

static struct uniwill_device_descriptor lapqc71a_lapqc71b_descriptor __initdata = {
	.config = {
		.features = UNIWILL_FEATURE_SUPER_KEY |
			    UNIWILL_FEATURE_LIGHTBAR |
			    UNIWILL_FEATURE_BATTERY_CHARGE_LIMIT |
			    UNIWILL_FEATURE_CPU_TEMP |
			    UNIWILL_FEATURE_GPU_TEMP |
			    UNIWILL_FEATURE_PRIMARY_FAN |
			    UNIWILL_FEATURE_SECONDARY_FAN,
		.lightbar_max_brightness = 36,
	},
};

static struct uniwill_device_descriptor lapac71h_descriptor __initdata = {
	.config = {
		.features = UNIWILL_FEATURE_FN_LOCK |
			    UNIWILL_FEATURE_SUPER_KEY |
			    UNIWILL_FEATURE_TOUCHPAD_TOGGLE |
			    UNIWILL_FEATURE_BATTERY_CHARGE_LIMIT |
			    UNIWILL_FEATURE_CPU_TEMP |
			    UNIWILL_FEATURE_GPU_TEMP |
			    UNIWILL_FEATURE_PRIMARY_FAN |
			    UNIWILL_FEATURE_SECONDARY_FAN,
	},
};

static struct uniwill_device_descriptor lapkc71f_descriptor __initdata = {
	.config = {
		.features = UNIWILL_FEATURE_FN_LOCK |
			    UNIWILL_FEATURE_SUPER_KEY |
			    UNIWILL_FEATURE_TOUCHPAD_TOGGLE |
			    UNIWILL_FEATURE_LIGHTBAR |
			    UNIWILL_FEATURE_BATTERY_CHARGE_LIMIT |
			    UNIWILL_FEATURE_CPU_TEMP |
			    UNIWILL_FEATURE_GPU_TEMP |
			    UNIWILL_FEATURE_PRIMARY_FAN |
			    UNIWILL_FEATURE_SECONDARY_FAN,
		.lightbar_max_brightness = 200,
	}
};

/*
 * The featuresets below reflect somewhat chronological changes:
 * 1 -> 2: UNIWILL_FEATURE_NVIDIA_CTGP_CONTROL is added to the EC firmware.
 * 2 -> 3: UNIWILL_FEATURE_USB_C_POWER_PRIORITY is removed from the EC firmware.
 * Some devices might divert from this timeline.
 */

static struct uniwill_device_descriptor tux_featureset_1_descriptor __initdata = {
	.config = {
		.features = UNIWILL_FEATURE_FN_LOCK |
			    UNIWILL_FEATURE_SUPER_KEY |
			    UNIWILL_FEATURE_BATTERY_CHARGE_MODES |
			    UNIWILL_FEATURE_CPU_TEMP |
			    UNIWILL_FEATURE_PRIMARY_FAN |
			    UNIWILL_FEATURE_SECONDARY_FAN |
			    UNIWILL_FEATURE_USB_C_POWER_PRIORITY,
	},
};

static struct uniwill_device_descriptor tux_featureset_1_nvidia_descriptor __initdata = {
	.config = {
		.features = UNIWILL_FEATURE_FN_LOCK |
			    UNIWILL_FEATURE_SUPER_KEY |
			    UNIWILL_FEATURE_BATTERY_CHARGE_MODES |
			    UNIWILL_FEATURE_CPU_TEMP |
			    UNIWILL_FEATURE_GPU_TEMP |
			    UNIWILL_FEATURE_PRIMARY_FAN |
			    UNIWILL_FEATURE_SECONDARY_FAN |
			    UNIWILL_FEATURE_USB_C_POWER_PRIORITY,
	},
};

static struct uniwill_device_descriptor tux_featureset_2_nvidia_descriptor __initdata = {
	.config = {
		.features = UNIWILL_FEATURE_FN_LOCK |
			    UNIWILL_FEATURE_SUPER_KEY |
			    UNIWILL_FEATURE_BATTERY_CHARGE_MODES |
			    UNIWILL_FEATURE_CPU_TEMP |
			    UNIWILL_FEATURE_GPU_TEMP |
			    UNIWILL_FEATURE_PRIMARY_FAN |
			    UNIWILL_FEATURE_SECONDARY_FAN |
			    UNIWILL_FEATURE_NVIDIA_CTGP_CONTROL |
			    UNIWILL_FEATURE_USB_C_POWER_PRIORITY,
	},
};

static struct uniwill_device_descriptor tux_featureset_3_descriptor __initdata = {
	.config = {
		.features = UNIWILL_FEATURE_FN_LOCK |
			    UNIWILL_FEATURE_SUPER_KEY |
			    UNIWILL_FEATURE_BATTERY_CHARGE_MODES |
			    UNIWILL_FEATURE_CPU_TEMP |
			    UNIWILL_FEATURE_PRIMARY_FAN |
			    UNIWILL_FEATURE_SECONDARY_FAN,
	},
};

static struct uniwill_device_descriptor tux_featureset_3_nvidia_descriptor __initdata = {
	.config = {
		.features = UNIWILL_FEATURE_FN_LOCK |
			    UNIWILL_FEATURE_SUPER_KEY |
			    UNIWILL_FEATURE_BATTERY_CHARGE_MODES |
			    UNIWILL_FEATURE_CPU_TEMP |
			    UNIWILL_FEATURE_GPU_TEMP |
			    UNIWILL_FEATURE_PRIMARY_FAN |
			    UNIWILL_FEATURE_SECONDARY_FAN |
			    UNIWILL_FEATURE_NVIDIA_CTGP_CONTROL,
	},
};

static int phxtxx1_probe(struct regmap *regmap, u8 project_id, struct uniwill_device_config *config)
{
	if (project_id == PROJECT_ID_PH4TRX1 || project_id == PROJECT_ID_PH6TRX1)
		config->features |= UNIWILL_FEATURE_SECONDARY_FAN;

	return 0;
};

static struct uniwill_device_descriptor phxtxx1_descriptor __initdata = {
	.config = {
		.features = UNIWILL_FEATURE_FN_LOCK |
			    UNIWILL_FEATURE_SUPER_KEY |
			    UNIWILL_FEATURE_BATTERY_CHARGE_MODES |
			    UNIWILL_FEATURE_CPU_TEMP |
			    UNIWILL_FEATURE_PRIMARY_FAN |
			    UNIWILL_FEATURE_USB_C_POWER_PRIORITY,
	},
	.probe = phxtxx1_probe,
};

static int phxarx1_phxaqf1_probe(struct regmap *regmap, u8 project_id,
				 struct uniwill_device_config *config)
{
	unsigned int value;
	int ret;

	ret = regmap_read(regmap, EC_ADDR_SYSTEM_ID, &value);
	if (ret < 0)
		return ret;

	if (value & HAS_GPU)
		config->features |= UNIWILL_FEATURE_GPU_TEMP | UNIWILL_FEATURE_NVIDIA_CTGP_CONTROL;

	return 0;
};

static struct uniwill_device_descriptor phxarx1_phxaqf1_descriptor __initdata = {
	.config = {
		.features = UNIWILL_FEATURE_FN_LOCK |
			    UNIWILL_FEATURE_SUPER_KEY |
			    UNIWILL_FEATURE_BATTERY_CHARGE_MODES |
			    UNIWILL_FEATURE_CPU_TEMP |
			    UNIWILL_FEATURE_PRIMARY_FAN |
			    UNIWILL_FEATURE_SECONDARY_FAN |
			    UNIWILL_FEATURE_USB_C_POWER_PRIORITY,
	},
	.probe = phxarx1_phxaqf1_probe,
};

static struct uniwill_device_descriptor pf5pu1g_descriptor __initdata = {
	.config = {
		.features = UNIWILL_FEATURE_FN_LOCK |
			    UNIWILL_FEATURE_SUPER_KEY |
			    UNIWILL_FEATURE_CPU_TEMP |
			    UNIWILL_FEATURE_PRIMARY_FAN,
	},
};

static struct uniwill_device_descriptor x4sp4nal_descriptor __initdata = {
	.config = {
		.features = UNIWILL_FEATURE_FN_LOCK |
			    UNIWILL_FEATURE_SUPER_KEY |
			    UNIWILL_FEATURE_BATTERY_CHARGE_MODES |
			    UNIWILL_FEATURE_CPU_TEMP |
			    UNIWILL_FEATURE_PRIMARY_FAN |
			    UNIWILL_FEATURE_SECONDARY_FAN |
			    UNIWILL_FEATURE_KEYBOARD_BACKLIGHT |
			    UNIWILL_FEATURE_AC_AUTO_BOOT |
			    UNIWILL_FEATURE_USB_POWERSHARE,
		.kbd_led_max_brightness = 2,
	},
};

static const struct dmi_system_id uniwill_dmi_table[] __initconst = {
	{
		.ident = "AiStone X4SP4NAL",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "AiStone"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "X4SP4NAL"),
		},
		.driver_data = &x4sp4nal_descriptor,
	},
	{
		.ident = "MACHENIKE L16 Pro",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "MACHENIKE"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "L16P"),
		},
		.driver_data = &machenike_l16p_descriptor,
	},
	{
		.ident = "XMG FUSION 15 (L19)",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "SchenkerTechnologiesGmbH"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "LAPQC71A"),
		},
		.driver_data = &lapqc71a_lapqc71b_descriptor,
	},
	{
		.ident = "XMG FUSION 15 (L19)",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "SchenkerTechnologiesGmbH"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "LAPQC71B"),
		},
		.driver_data = &lapqc71a_lapqc71b_descriptor,
	},
	{
		.ident = "XMG FUSION 15 (L19)",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "LAPQC71A"),
		},
		.driver_data = &lapqc71a_lapqc71b_descriptor,
	},
	{
		.ident = "XMG FUSION 15 (L19)",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "LAPQC71B"),
		},
		.driver_data = &lapqc71a_lapqc71b_descriptor,
	},
	{
		.ident = "Intel NUC x15",
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Intel(R) Client Systems"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "LAPAC71H"),
		},
		.driver_data = &lapac71h_descriptor,
	},
	{
		.ident = "Intel NUC x15",
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Intel(R) Client Systems"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "LAPKC71F"),
		},
		.driver_data = &lapkc71f_descriptor,
	},
	{
		.ident = "TUXEDO InfinityBook Pro 14 Gen6 Intel",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "PHxTxX1"),
		},
		.driver_data = &phxtxx1_descriptor,
	},
	{
		.ident = "TUXEDO InfinityBook Pro 14 Gen6 Intel",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "PHxTQx1"),
		},
		.driver_data = &tux_featureset_2_nvidia_descriptor,
	},
	{
		.ident = "TUXEDO InfinityBook Pro 14/16 Gen7 Intel",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "PHxARX1_PHxAQF1"),
		},
		.driver_data = &phxarx1_phxaqf1_descriptor,
	},
	{
		.ident = "TUXEDO InfinityBook Pro 16 Gen7 Intel/Commodore Omnia-Book Pro Gen 7",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "PH6AG01_PH6AQ71_PH6AQI1"),
		},
		.driver_data = &tux_featureset_2_nvidia_descriptor,
	},
	{
		.ident = "TUXEDO InfinityBook Pro 14/16 Gen8 Intel/Commodore Omnia-Book Pro Gen 8",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "PH4PRX1_PH6PRX1"),
		},
		.driver_data = &tux_featureset_1_descriptor,
	},
	{
		.ident = "TUXEDO InfinityBook Pro 14 Gen8 Intel/Commodore Omnia-Book Pro Gen 8",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "PH4PG31"),
		},
		.driver_data = &tux_featureset_2_nvidia_descriptor,
	},
	{
		.ident = "TUXEDO InfinityBook Pro 16 Gen8 Intel",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "PH6PG01_PH6PG71"),
		},
		.driver_data = &tux_featureset_2_nvidia_descriptor,
	},
	{
		.ident = "TUXEDO InfinityBook Pro 14/15 Gen9 AMD",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "GXxHRXx"),
		},
		.driver_data = &tux_featureset_3_descriptor,
	},
	{
		.ident = "TUXEDO InfinityBook Pro 14/15 Gen9 Intel/Commodore Omnia-Book 15 Gen9",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "GXxMRXx"),
		},
		.driver_data = &tux_featureset_3_descriptor,
	},
	{
		.ident = "TUXEDO InfinityBook Pro 14/15 Gen10 AMD",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "XxHP4NAx"),
		},
		.driver_data = &tux_featureset_3_descriptor,
	},
	{
		.ident = "TUXEDO InfinityBook Pro 14/15 Gen10 AMD",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "XxKK4NAx_XxSP4NAx"),
		},
		.driver_data = &tux_featureset_3_descriptor,
	},
	{
		.ident = "TUXEDO InfinityBook Pro 15 Gen10 Intel",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "XxAR4NAx"),
		},
		.driver_data = &tux_featureset_3_descriptor,
	},
	{
		.ident = "TUXEDO InfinityBook Max 15 Gen10 AMD",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "X5KK45xS_X5SP45xS"),
		},
		.driver_data = &tux_featureset_3_nvidia_descriptor,
	},
	{
		.ident = "TUXEDO InfinityBook Max 16 Gen10 AMD",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "X6HP45xU"),
		},
		.driver_data = &tux_featureset_3_nvidia_descriptor,
	},
	{
		.ident = "TUXEDO InfinityBook Max 16 Gen10 AMD",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "X6KK45xU_X6SP45xU"),
		},
		.driver_data = &tux_featureset_3_nvidia_descriptor,
	},
	{
		.ident = "TUXEDO InfinityBook Max 15 Gen10 Intel",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "X5AR45xS"),
		},
		.driver_data = &tux_featureset_3_nvidia_descriptor,
	},
	{
		.ident = "TUXEDO InfinityBook Max 16 Gen10 Intel",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "X6AR55xU"),
		},
		.driver_data = &tux_featureset_3_nvidia_descriptor,
	},
	{
		.ident = "TUXEDO Polaris 15 Gen1 AMD",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "POLARIS1501A1650TI"),
		},
		.driver_data = &tux_featureset_1_nvidia_descriptor,
	},
	{
		.ident = "TUXEDO Polaris 15 Gen1 AMD",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "POLARIS1501A2060"),
		},
		.driver_data = &tux_featureset_1_nvidia_descriptor,
	},
	{
		.ident = "TUXEDO Polaris 17 Gen1 AMD",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "POLARIS1701A1650TI"),
		},
		.driver_data = &tux_featureset_1_nvidia_descriptor,
	},
	{
		.ident = "TUXEDO Polaris 17 Gen1 AMD",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "POLARIS1701A2060"),
		},
		.driver_data = &tux_featureset_1_nvidia_descriptor,
	},
	{
		.ident = "TUXEDO Polaris 15 Gen1 Intel",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "POLARIS1501I1650TI"),
		},
		.driver_data = &tux_featureset_1_nvidia_descriptor,
	},
	{
		.ident = "TUXEDO Polaris 15 Gen1 Intel",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "POLARIS1501I2060"),
		},
		.driver_data = &tux_featureset_1_nvidia_descriptor,
	},
	{
		.ident = "TUXEDO Polaris 17 Gen1 Intel",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "POLARIS1701I1650TI"),
		},
		.driver_data = &tux_featureset_1_nvidia_descriptor,
	},
	{
		.ident = "TUXEDO Polaris 17 Gen1 Intel",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "POLARIS1701I2060"),
		},
		.driver_data = &tux_featureset_1_nvidia_descriptor,
	},
	{
		.ident = "TUXEDO Trinity 15 Intel Gen1",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "TRINITY1501I"),
		},
		.driver_data = &tux_featureset_1_nvidia_descriptor,
	},
	{
		.ident = "TUXEDO Trinity 17 Intel Gen1",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "TRINITY1701I"),
		},
		.driver_data = &tux_featureset_1_nvidia_descriptor,
	},
	{
		.ident = "TUXEDO Polaris 15/17 Gen2 AMD",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "GMxMGxx"),
		},
		.driver_data = &tux_featureset_2_nvidia_descriptor,
	},
	{
		.ident = "TUXEDO Polaris 15/17 Gen2 Intel",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "GMxNGxx"),
		},
		.driver_data = &tux_featureset_2_nvidia_descriptor,
	},
	{
		.ident = "TUXEDO Stellaris/Polaris 15/17 Gen3 AMD",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "GMxZGxx"),
		},
		.driver_data = &tux_featureset_2_nvidia_descriptor,
	},
	{
		.ident = "TUXEDO Stellaris/Polaris 15/17 Gen3 Intel",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "GMxTGxx"),
		},
		.driver_data = &tux_featureset_2_nvidia_descriptor,
	},
	{
		.ident = "TUXEDO Stellaris/Polaris 15/17 Gen4 AMD",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "GMxRGxx"),
		},
		.driver_data = &tux_featureset_3_nvidia_descriptor,
	},
	{
		.ident = "TUXEDO Stellaris 15 Gen4 Intel",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "GMxAGxx"),
		},
		.driver_data = &tux_featureset_3_nvidia_descriptor,
	},
	{
		.ident = "TUXEDO Polaris 15/17 Gen5 AMD",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "GMxXGxx"),
		},
		.driver_data = &tux_featureset_2_nvidia_descriptor,
	},
	{
		.ident = "TUXEDO Stellaris 16 Gen5 AMD",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "GM6XGxX"),
		},
		.driver_data = &tux_featureset_3_nvidia_descriptor,
	},
	{
		.ident = "TUXEDO Stellaris 16/17 Gen5 Intel/Commodore ORION Gen 5",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "GMxPXxx"),
		},
		.driver_data = &tux_featureset_3_nvidia_descriptor,
	},
	{
		.ident = "TUXEDO Stellaris Slim 15 Gen6 AMD",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "GMxHGxx"),
		},
		.driver_data = &tux_featureset_3_nvidia_descriptor,
	},
	{
		.ident = "TUXEDO Stellaris Slim 15 Gen6 Intel/Commodore ORION Slim 15 Gen6",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "GM5IXxA"),
		},
		.driver_data = &tux_featureset_3_nvidia_descriptor,
	},
	{
		.ident = "TUXEDO Stellaris 16 Gen6 Intel/Commodore ORION 16 Gen6",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "GM6IXxB_MB1"),
		},
		.driver_data = &tux_featureset_3_nvidia_descriptor,
	},
	{
		.ident = "TUXEDO Stellaris 16 Gen6 Intel/Commodore ORION 16 Gen6",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "GM6IXxB_MB2"),
		},
		.driver_data = &tux_featureset_3_nvidia_descriptor,
	},
	{
		.ident = "TUXEDO Stellaris 17 Gen6 Intel/Commodore ORION 17 Gen6",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "GM7IXxN"),
		},
		.driver_data = &tux_featureset_3_nvidia_descriptor,
	},
	{
		.ident = "TUXEDO Stellaris 16 Gen7 AMD",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "X6FR5xxY"),
		},
		.driver_data = &tux_featureset_3_nvidia_descriptor,
	},
	{
		.ident = "TUXEDO Stellaris 16 Gen7 Intel",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "X6AR5xxY"),
		},
		.driver_data = &tux_featureset_3_nvidia_descriptor,
	},
	{
		.ident = "TUXEDO Stellaris 16 Gen7 Intel",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "X6AR5xxY_mLED"),
		},
		.driver_data = &tux_featureset_3_nvidia_descriptor,
	},
	{
		.ident = "TUXEDO Book BA15 Gen10 AMD",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "PF5PU1G"),
		},
		.driver_data = &pf5pu1g_descriptor,
	},
	{
		.ident = "TUXEDO Pulse 14 Gen1 AMD",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "PULSE1401"),
		},
		.driver_data = &tux_featureset_1_descriptor,
	},
	{
		.ident = "TUXEDO Pulse 15 Gen1 AMD",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "PULSE1501"),
		},
		.driver_data = &tux_featureset_1_descriptor,
	},
	{
		.ident = "TUXEDO Pulse 15 Gen2 AMD",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "PF5LUXG"),
		},
		.driver_data = &tux_featureset_1_descriptor,
	},
	{ }
};
MODULE_DEVICE_TABLE(dmi, uniwill_dmi_table);

static const struct uniwill_device_descriptor default_descriptor __initconst = {
	.config = {
		/* Assume that the device supports all features except the charge limit */
		.features = UINT_MAX & ~UNIWILL_FEATURE_BATTERY_CHARGE_LIMIT,
		/* Some models only support 3 brightness levels */
		.kbd_led_max_brightness = 4,
		/* Some models only support 36 brightness levels per color component */
		.lightbar_max_brightness = 200,
	},
};

int __init uniwill_dmi_match(struct uniwill_device_descriptor *descriptor)
{
	const struct uniwill_device_descriptor *desc;
	const struct dmi_system_id *id;
	int ret;

	id = dmi_first_match(uniwill_dmi_table);
	if (!id) {
		if (!force)
			return -ENODEV;

		pr_warn("Loading on a potentially unsupported device\n");
	} else {
		/*
		 * Some devices might support additional features depending on
		 * the BIOS version/date, so we call this callback to let them
		 * modify their device descriptor accordingly.
		 */
		if (id->callback) {
			ret = id->callback(id);
			if (ret < 0)
				return ret;
		}

		desc = id->driver_data;
		*descriptor = *descriptor;
	}

	if (force) {
		*descriptor = default_descriptor;
		pr_warn("Enabling potentially unsupported features\n");
	}

	return 0;
}
