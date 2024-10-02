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
	{ KE_IGNORE,	UNIWILL_KEY_CAPSLOCK,			{ KEY_CAPSLOCK }},
	{ KE_IGNORE,	UNIWILL_KEY_NUMLOCK,			{ KEY_NUMLOCK }},
	{ KE_IGNORE,	UNIWILL_KEY_SCROLLLOCK,			{ KEY_SCROLLLOCK }},

	/* TODO */
	{ KE_IGNORE,	UNIWILL_KEY_TOUCHPAD_ON,		{ KEY_TOUCHPAD_ON }},
	{ KE_IGNORE,	UNIWILL_KEY_TOUCHPAD_OFF,		{ KEY_TOUCHPAD_OFF }},

	/* Reported via "video bus" */
	{ KE_IGNORE,	UNIWILL_KEY_BRIGHTNESSUP,		{ KEY_BRIGHTNESSUP }},
	{ KE_IGNORE,	UNIWILL_KEY_BRIGHTNESSDOWN,		{ KEY_BRIGHTNESSDOWN }},

	/*
	 * Reported in automatic mode when rfkill state changes.
	 * We ignore it since the EC is always switched into manual
	 * mode by uniwill-laptop.
	 */
	{ KE_IGNORE,	UNIWILL_OSD_RADIOON,			{.sw = { SW_RFKILL_ALL, 1 }}},
	{ KE_IGNORE,	UNIWILL_OSD_RADIOOFF,			{.sw = { SW_RFKILL_ALL, 0 }}},

	/* Reported via keyboard controller */
	{ KE_IGNORE,	UNIWILL_KEY_MUTE,			{ KEY_MUTE }},
	{ KE_IGNORE,	UNIWILL_KEY_VOLUMEDOWN,			{ KEY_VOLUMEDOWN }},
	{ KE_IGNORE,	UNIWILL_KEY_VOLUMEUP,			{ KEY_VOLUMEUP }},

	/* TODO */
	{ KE_IGNORE,	UNIWILL_OSD_LIGHTBAR_ON,		{ KEY_RESERVED }},
	{ KE_IGNORE,	UNIWILL_OSD_LIGHTBAR_OFF,		{ KEY_RESERVED }},

	/* TODO */
	{ KE_KEY,	UNIWILL_OSD_KB_LED_LEVEL0,		{ KEY_KBDILLUMTOGGLE }},
	{ KE_KEY,	UNIWILL_OSD_KB_LED_LEVEL1,		{ KEY_KBDILLUMTOGGLE }},
	{ KE_KEY,	UNIWILL_OSD_KB_LED_LEVEL2,		{ KEY_KBDILLUMTOGGLE }},
	{ KE_KEY,	UNIWILL_OSD_KB_LED_LEVEL3,		{ KEY_KBDILLUMTOGGLE }},
	{ KE_KEY,	UNIWILL_OSD_KB_LED_LEVEL4,		{ KEY_KBDILLUMTOGGLE }},

	/* TODO */
	{ KE_IGNORE,	UNIWILL_OSD_SUPER_KEY_LOCK_ENABLE,	{ KEY_RESERVED }},
	{ KE_IGNORE,	UNIWILL_OSD_SUPER_KEY_LOCK_DISABLE,	{ KEY_RESERVED }},

	/*
	 * Not reported by other means when in manual mode,
	 * handled automatically when in automatic mode
	 */
	{ KE_KEY,	UNIWILL_KEY_RFKILL,			{ KEY_RFKILL }},

	/* TODO */
	{ KE_IGNORE,	UNIWILL_OSD_SUPER_KEY_LOCK_TOGGLE,	{ KEY_RESERVED }},
	{ KE_IGNORE,	UNIWILL_OSD_LIGHTBAR_STATE_CHANGED,	{ KEY_RESERVED }},
	{ KE_IGNORE,	UNIWILL_OSD_FAN_BOOST_STATE_CHANGED,	{ KEY_RESERVED }},
	{ KE_IGNORE,	UNIWILL_OSD_DC_ADAPTER_CHANGED,		{ KEY_RESERVED }},

	/* TODO */
	{ KE_IGNORE,	UNIWILL_OSD_PERF_MODE_CHANGED,		{ KEY_RESERVED }},

	/*
	 * Not reported by other means when in manual mode,
	 * handled automatically when in automatic mode
	 */
	{ KE_KEY,	UNIWILL_KEY_KBDILLUMDOWN,		{ KEY_KBDILLUMDOWN }},
	{ KE_KEY,	UNIWILL_KEY_KBDILLUMUP,			{ KEY_KBDILLUMUP }},
	{ KE_KEY,	UNIWILL_KEY_FN_LOCK,			{ KEY_FN_ESC }},

	/* TODO */
	{ KE_KEY,	UNIWILL_KEY_KBDILLUMTOGGLE,		{ KEY_KBDILLUMTOGGLE }},

	/* TODO */
	{ KE_IGNORE,	UNIWILL_OSD_KBD_BACKLIGHT_CHANGED,	{ KEY_RESERVED }},

	{ KE_END }
};

int uniwill_wmi_register_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&uniwill_wmi_chain_head, nb);
}
EXPORT_SYMBOL_NS_GPL(uniwill_wmi_register_notifier, UNIWILL);

int uniwill_wmi_unregister_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&uniwill_wmi_chain_head, nb);
}
EXPORT_SYMBOL_NS_GPL(uniwill_wmi_unregister_notifier, UNIWILL);

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
EXPORT_SYMBOL_NS_GPL(devm_uniwill_wmi_register_notifier, UNIWILL);

static void uniwill_wmi_notify(struct wmi_device *wdev, union acpi_object *obj)
{
	struct uniwill_wmi_data *data = dev_get_drvdata(&wdev->dev);
	u32 value;
	int ret;

	if (obj->type != ACPI_TYPE_INTEGER)
		return;

	value = obj->integer.value;

	ret = blocking_notifier_call_chain(&uniwill_wmi_chain_head, 0, &value);
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
 * Because of this we are forced to use a DMI table for autoloading.
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
module_wmi_driver(uniwill_wmi_driver);	// TODO DMI

MODULE_AUTHOR("Armin Wolf <W_Armin@gmx.de>");
MODULE_DESCRIPTION("Uniwill notebook hotkey driver");
MODULE_LICENSE("GPL");
