/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Linux hotkey driver for Uniwill notebooks.
 *
 * Copyright (C) 2024 Armin Wolf <W_Armin@gmx.de>
 */

#ifndef UNIWILL_WMI_H
#define UNIWILL_WMI_H

#define UNIWILL_KEY_CAPSLOCK			0x01
#define UNIWILL_KEY_NUMLOCK			0x02
#define UNIWILL_KEY_SCROLLLOCK			0x03

#define UNIWILL_KEY_TOUCHPAD_ON			0x04
#define UNIWILL_KEY_TOUCHPAD_OFF		0x05

#define UNIWILL_KEY_BRIGHTNESSUP		0x14
#define UNIWILL_KEY_BRIGHTNESSDOWN		0x15

#define UNIWILL_OSD_RADIOON			0x1A
#define UNIWILL_OSD_RADIOOFF			0x1B

#define UNIWILL_KEY_MUTE			0x35
#define UNIWILL_KEY_VOLUMEDOWN			0x36
#define UNIWILL_KEY_VOLUMEUP			0x37

#define UNIWILL_OSD_LIGHTBAR_ON			0x39
#define UNIWILL_OSD_LIGHTBAR_OFF		0x3A

#define UNIWILL_OSD_KB_LED_LEVEL0		0x3B
#define UNIWILL_OSD_KB_LED_LEVEL1		0x3C
#define UNIWILL_OSD_KB_LED_LEVEL2		0x3D
#define UNIWILL_OSD_KB_LED_LEVEL3		0x3E
#define UNIWILL_OSD_KB_LED_LEVEL4		0x3F

#define UNIWILL_OSD_SUPER_KEY_LOCK_ENABLE	0x40
#define UNIWILL_OSD_SUPER_KEY_LOCK_DISABLE	0x41

#define UNIWILL_KEY_RFKILL			0xA4

#define UNIWILL_OSD_SUPER_KEY_LOCK_TOGGLE	0xA5

#define UNIWILL_OSD_LIGHTBAR_STATE_CHANGED	0xA6

#define UNIWILL_OSD_FAN_BOOST_STATE_CHANGED	0xA7

#define UNIWILL_OSD_DC_ADAPTER_CHANGED		0xAB

#define UNIWILL_OSD_PERF_MODE_CHANGED		0xB0

#define UNIWILL_KEY_KBDILLUMDOWN		0xB1
#define UNIWILL_KEY_KBDILLUMUP			0xB2

#define UNIWILL_KEY_FN_LOCK			0xB8
#define UNIWILL_KEY_KBDILLUMTOGGLE		0xB9

#define UNIWILL_OSD_KBD_BACKLIGHT_CHANGED	0xF0

struct notifier_block;

int uniwill_wmi_register_notifier(struct notifier_block *nb);
int uniwill_wmi_unregister_notifier(struct notifier_block *nb);
int devm_uniwill_wmi_register_notifier(struct device *dev, struct notifier_block *nb);

#endif /* UNIWILL_WMI_H */
