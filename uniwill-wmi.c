// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Linux hotkey driver for Uniwill notebooks.
 *
 * Copyright (C) 2024 Armin Wolf <W_Armin@gmx.de>
 */

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/input.h>
#include <linux/input/sparse-keymap.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/wmi.h>

#include "uniwill-wmi.h"

#define DRIVER_NAME		"uniwill-wmi"
#define UNIWILL_EVENT_GUID	"ABBC0F72-8EA1-11D1-00A0-C90629100000"

struct uniwill_wmi_data {
	struct mutex input_lock;	/* Protects input sequence during notify */
	struct input_dev *input_device;
};

static BLOCKING_NOTIFIER_HEAD(uniwill_wmi_chain_head);

static const struct key_entry uniwill_wmi_keymap[] = {
	/* Reported via keyboard controller */
	{ KE_IGNORE,	UNIWILL_OSD_CAPSLOCK,			{ KEY_CAPSLOCK }},
	{ KE_IGNORE,	UNIWILL_OSD_NUMLOCK,			{ KEY_NUMLOCK }},

	/* Reported when the user locks/unlocks the super key */
	{ KE_IGNORE,	UNIWILL_OSD_SUPER_KEY_LOCK_ENABLE,	{ KEY_UNKNOWN }},
	{ KE_IGNORE,	UNIWILL_OSD_SUPER_KEY_LOCK_DISABLE,	{ KEY_UNKNOWN }},

	/* Reported in manual mode when toggling the airplane mode status */
	{ KE_KEY,	UNIWILL_OSD_RFKILL,			{ KEY_RFKILL }},

	/* Reported when user wants to cycle the platform profile */
	{ KE_IGNORE,	UNIWILL_OSD_PERFORMANCE_MODE_TOGGLE,	{ KEY_UNKNOWN }},

	/* Reported when the user wants to toggle the microphone mute status */
	{ KE_KEY,	UNIWILL_OSD_MIC_MUTE,			{ KEY_MICMUTE }},

	/* Reported when the user locks/unlocks the Fn key */
	{ KE_IGNORE,	UNIWILL_OSD_FN_LOCK,			{ KEY_FN_ESC }},

	/* Reported when the user wants to toggle the brightness of the keyboard */
	{ KE_KEY,	UNIWILL_OSD_KBDILLUMTOGGLE,		{ KEY_KBDILLUMTOGGLE }},

	/* FIXME: find out the exact meaning of those events */
	{ KE_IGNORE,	UNIWILL_OSD_BAT_CHARGE_FULL_24_H,	{ KEY_UNKNOWN }},
	{ KE_IGNORE,	UNIWILL_OSD_BAT_ERM_UPDATE,		{ KEY_UNKNOWN }},

	/* Reported when the user wants to toggle the benchmark mode status */
	{ KE_IGNORE,	UNIWILL_OSD_BENCHMARK_MODE_TOGGLE,	{ KEY_UNKNOWN }},

	{ KE_END }
};

int uniwill_wmi_register_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&uniwill_wmi_chain_head, nb);
}
EXPORT_SYMBOL_NS_GPL(uniwill_wmi_register_notifier, "UNIWILL");

int uniwill_wmi_unregister_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&uniwill_wmi_chain_head, nb);
}
EXPORT_SYMBOL_NS_GPL(uniwill_wmi_unregister_notifier, "UNIWILL");

static void devm_uniwill_wmi_unregister_notifier(void *data)
{
	struct notifier_block *nb = data;

	uniwill_wmi_unregister_notifier(nb);
}

int devm_uniwill_wmi_register_notifier(struct device *dev, struct notifier_block *nb)
{
	int ret;

	ret = uniwill_wmi_register_notifier(nb);
	if (ret < 0)
		return ret;

	return devm_add_action_or_reset(dev, devm_uniwill_wmi_unregister_notifier, nb);
}
EXPORT_SYMBOL_NS_GPL(devm_uniwill_wmi_register_notifier, "UNIWILL");

static void uniwill_wmi_notify(struct wmi_device *wdev, union acpi_object *obj)
{
	struct uniwill_wmi_data *data = dev_get_drvdata(&wdev->dev);
	u32 value;
	int ret;

	if (obj->type != ACPI_TYPE_INTEGER)
		return;

	value = obj->integer.value;

	dev_dbg(&wdev->dev, "Received WMI event %u\n", value);

	ret = blocking_notifier_call_chain(&uniwill_wmi_chain_head, value, NULL);
	if (ret == NOTIFY_BAD)
		return;

	mutex_lock(&data->input_lock);
	sparse_keymap_report_event(data->input_device, value, 1, true);
	mutex_unlock(&data->input_lock);
}

static int uniwill_wmi_probe(struct wmi_device *wdev, const void *context)
{
	struct uniwill_wmi_data *data;
	int ret;

	data = devm_kzalloc(&wdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	ret = devm_mutex_init(&wdev->dev, &data->input_lock);
	if (ret < 0)
		return ret;

	dev_set_drvdata(&wdev->dev, data);

	data->input_device = devm_input_allocate_device(&wdev->dev);
	if (!data->input_device)
		return -ENOMEM;

	ret = sparse_keymap_setup(data->input_device, uniwill_wmi_keymap, NULL);
	if (ret < 0)
		return ret;

	data->input_device->name = "Uniwill WMI hotkeys";
	data->input_device->phys = "wmi/input0";
	data->input_device->id.bustype = BUS_HOST;

	return input_register_device(data->input_device);
}

/*
 * We cannot fully trust this GUID since Uniwill just copied the WMI GUID
 * from the Windows driver example, and others probably did the same.
 *
 * Because of this we cannot use this WMI GUID for autoloading. The uniwill-laptop
 * driver will instead load this module as a dependency.
 */
static const struct wmi_device_id uniwill_wmi_id_table[] = {
	{ UNIWILL_EVENT_GUID, NULL },
	{ }
};

static struct wmi_driver uniwill_wmi_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.id_table = uniwill_wmi_id_table,
	.probe = uniwill_wmi_probe,
	.notify = uniwill_wmi_notify,
	.no_singleton = true,
};
module_wmi_driver(uniwill_wmi_driver);

MODULE_AUTHOR("Armin Wolf <W_Armin@gmx.de>");
MODULE_DESCRIPTION("Uniwill notebook hotkey driver");
MODULE_LICENSE("GPL");
