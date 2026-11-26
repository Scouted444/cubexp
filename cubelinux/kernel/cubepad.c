// SPDX-License-Identifier: GPL-2.0
/*
 * cubepad - GameCube controller ports as Linux input devices (CubeLinux)
 *
 * The CubeLinux boot loader initialises the controllers with libogc, which
 * leaves the Serial Interface (SI) polling every port in hardware and writing
 * the latest state into each channel's input buffer registers. This driver
 * reads those registers ~60 times a second and reports them as gamepads:
 *
 *   SICnINBUFH: [31] error/no controller  [29:16] buttons  [15:8] stick X  [7:0] stick Y
 *   SICnINBUFL: [31:24] C-stick X  [23:16] C-stick Y  [15:8] L analog  [7:0] R analog
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/input.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/slab.h>

#define SI_BASE		0x0c006400
#define SI_SIZE		0x100
#define SI_INBUFH(ch)	(0x04 + (ch) * 12)
#define SI_INBUFL(ch)	(0x08 + (ch) * 12)

/* libogc PAD_BUTTON_* bits */
#define PAD_LEFT	0x0001
#define PAD_RIGHT	0x0002
#define PAD_DOWN	0x0004
#define PAD_UP		0x0008
#define PAD_Z		0x0010
#define PAD_R		0x0020
#define PAD_L		0x0040
#define PAD_A		0x0100
#define PAD_B		0x0200
#define PAD_X		0x0400
#define PAD_Y		0x0800
#define PAD_START	0x1000

static const struct { u16 bit; u16 code; } cubepad_buttons[] = {
	{ PAD_A, BTN_A }, { PAD_B, BTN_B }, { PAD_X, BTN_X }, { PAD_Y, BTN_Y },
	{ PAD_Z, BTN_Z }, { PAD_L, BTN_TL }, { PAD_R, BTN_TR }, { PAD_START, BTN_START },
	{ PAD_UP, BTN_DPAD_UP }, { PAD_DOWN, BTN_DPAD_DOWN },
	{ PAD_LEFT, BTN_DPAD_LEFT }, { PAD_RIGHT, BTN_DPAD_RIGHT },
};

struct cubepad {
	struct input_dev *dev[4];
	void __iomem *si;
};
static struct cubepad *cp;

static void cubepad_poll(struct input_dev *dev)
{
	int ch = (long)input_get_drvdata(dev);
	u32 h = __raw_readl((u8 __iomem *)cp->si + SI_INBUFH(ch));
	u32 l = __raw_readl((u8 __iomem *)cp->si + SI_INBUFL(ch));
	u16 btn;
	int i;

	if ((h & 0x80000000) || (!h && !l)) {	/* no controller in this port */
		h = 0x00008080;
		l = 0x80800000;
	}
	btn = (h >> 16) & 0x3fff;
	for (i = 0; i < ARRAY_SIZE(cubepad_buttons); i++)
		input_report_key(dev, cubepad_buttons[i].code, !!(btn & cubepad_buttons[i].bit));
	input_report_abs(dev, ABS_X, (h >> 8) & 0xff);
	input_report_abs(dev, ABS_Y, 255 - (h & 0xff));		/* Linux: down is positive */
	input_report_abs(dev, ABS_RX, (l >> 24) & 0xff);
	input_report_abs(dev, ABS_RY, 255 - ((l >> 16) & 0xff));
	input_report_abs(dev, ABS_Z, (l >> 8) & 0xff);
	input_report_abs(dev, ABS_RZ, l & 0xff);
	input_report_abs(dev, ABS_HAT0X, (btn & PAD_RIGHT ? 1 : 0) - (btn & PAD_LEFT ? 1 : 0));
	input_report_abs(dev, ABS_HAT0Y, (btn & PAD_DOWN ? 1 : 0) - (btn & PAD_UP ? 1 : 0));
	input_sync(dev);
}

static int __init cubepad_init(void)
{
	int ch, i, ret;

	if (!of_machine_is_compatible("nintendo,gamecube"))
		return -ENODEV;
	cp = kzalloc(sizeof(*cp), GFP_KERNEL);
	if (!cp)
		return -ENOMEM;
	cp->si = ioremap(SI_BASE, SI_SIZE);
	if (!cp->si) {
		kfree(cp);
		return -ENOMEM;
	}
	for (ch = 0; ch < 4; ch++) {
		static const char *names[4] = {
			"GameCube Controller 1", "GameCube Controller 2",
			"GameCube Controller 3", "GameCube Controller 4",
		};
		static const char *phys[4] = { "si/port0", "si/port1", "si/port2", "si/port3" };
		struct input_dev *dev = input_allocate_device();

		if (!dev) {
			ret = -ENOMEM;
			goto fail;
		}
		dev->name = names[ch];
		dev->phys = phys[ch];
		dev->id.bustype = BUS_HOST;
		dev->id.vendor = 0x057e;	/* Nintendo */
		dev->id.product = 0x0337;
		input_set_drvdata(dev, (void *)(long)ch);
		for (i = 0; i < ARRAY_SIZE(cubepad_buttons); i++)
			input_set_capability(dev, EV_KEY, cubepad_buttons[i].code);
		input_set_abs_params(dev, ABS_X, 0, 255, 2, 16);
		input_set_abs_params(dev, ABS_Y, 0, 255, 2, 16);
		input_set_abs_params(dev, ABS_RX, 0, 255, 2, 16);
		input_set_abs_params(dev, ABS_RY, 0, 255, 2, 16);
		input_set_abs_params(dev, ABS_Z, 0, 255, 2, 8);
		input_set_abs_params(dev, ABS_RZ, 0, 255, 2, 8);
		input_set_abs_params(dev, ABS_HAT0X, -1, 1, 0, 0);
		input_set_abs_params(dev, ABS_HAT0Y, -1, 1, 0, 0);
		ret = input_setup_polling(dev, cubepad_poll);
		if (ret) {
			input_free_device(dev);
			goto fail;
		}
		input_set_poll_interval(dev, 16);
		ret = input_register_device(dev);
		if (ret) {
			input_free_device(dev);
			goto fail;
		}
		cp->dev[ch] = dev;
	}
	pr_info("cubepad: GameCube controller ports ready\n");
	return 0;

fail:
	for (ch = 0; ch < 4; ch++)
		if (cp->dev[ch])
			input_unregister_device(cp->dev[ch]);
	iounmap(cp->si);
	kfree(cp);
	cp = NULL;
	return ret;
}

static void __exit cubepad_exit(void)
{
	int ch;

	for (ch = 0; ch < 4; ch++)
		if (cp->dev[ch])
			input_unregister_device(cp->dev[ch]);
	iounmap(cp->si);
	kfree(cp);
}

module_init(cubepad_init);
module_exit(cubepad_exit);

MODULE_DESCRIPTION("GameCube controllers for CubeLinux");
MODULE_LICENSE("GPL");
