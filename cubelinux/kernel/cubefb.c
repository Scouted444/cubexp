// SPDX-License-Identifier: GPL-2.0
/*
 * cubefb - GameCube TV framebuffer for CubeLinux
 *
 * The CubeLinux boot loader programs the Video Interface (VI) with libogc and
 * leaves it scanning out an external framebuffer (XFB) at a fixed physical
 * address. The XFB is YUY2 (4:2:2), which ordinary Linux software can't draw
 * into, so this driver exposes a normal 32-bit XRGB8888 framebuffer in system
 * RAM and converts the parts that change into the XFB:
 *   - fbcon / fb_write damage is tracked as a dirty row range
 *   - mmap() writes are tracked by fbdev deferred I/O (dirty pages)
 * A worker converts the dirty rows ~50 times a second.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fb.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>

struct cubefb_par {
	struct fb_info *info;
	void __iomem *xfb;		/* YUY2 scan-out buffer (uncached) */
	u32 width, height, xfb_stride;
	spinlock_t lock;
	int dirty_y0, dirty_y1;		/* inclusive row range, y0 > y1 = clean */
	struct delayed_work work;
	u32 pseudo_palette[16];
};

/* ---- colour conversion ---- */
static inline u32 yuy2_pair(u32 p0, u32 p1)
{
	int r0 = (p0 >> 16) & 255, g0 = (p0 >> 8) & 255, b0 = p0 & 255;
	int r1 = (p1 >> 16) & 255, g1 = (p1 >> 8) & 255, b1 = p1 & 255;
	int y0 = ((66 * r0 + 129 * g0 + 25 * b0 + 128) >> 8) + 16;
	int y1 = ((66 * r1 + 129 * g1 + 25 * b1 + 128) >> 8) + 16;
	int r = (r0 + r1) >> 1, g = (g0 + g1) >> 1, b = (b0 + b1) >> 1;
	int cb = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
	int cr = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;

	return ((u32)y0 << 24) | ((u32)cb << 16) | ((u32)y1 << 8) | (u32)cr;
}

static void cubefb_convert_rows(struct cubefb_par *par, int y0, int y1)
{
	const u32 *src = (const u32 *)par->info->screen_buffer;
	u32 stride_px = par->info->fix.line_length / 4;
	int y, x;

	if (y0 < 0)
		y0 = 0;
	if (y1 >= (int)par->height)
		y1 = par->height - 1;
	for (y = y0; y <= y1; y++) {
		const u32 *s = src + y * stride_px;
		u32 __iomem *d = (u32 __iomem *)((u8 __iomem *)par->xfb + y * par->xfb_stride);

		for (x = 0; x < (int)par->width; x += 2)
			__raw_writel(yuy2_pair(s[x], s[x + 1]), d + x / 2);
	}
	wmb();
}

static void cubefb_mark(struct cubefb_par *par, int y0, int y1)
{
	unsigned long flags;

	spin_lock_irqsave(&par->lock, flags);
	if (par->dirty_y0 > par->dirty_y1) {
		par->dirty_y0 = y0;
		par->dirty_y1 = y1;
	} else {
		if (y0 < par->dirty_y0)
			par->dirty_y0 = y0;
		if (y1 > par->dirty_y1)
			par->dirty_y1 = y1;
	}
	spin_unlock_irqrestore(&par->lock, flags);
	schedule_delayed_work(&par->work, HZ / 50);
}

static void cubefb_work(struct work_struct *w)
{
	struct cubefb_par *par = container_of(to_delayed_work(w), struct cubefb_par, work);
	unsigned long flags;
	int y0, y1;

	spin_lock_irqsave(&par->lock, flags);
	y0 = par->dirty_y0;
	y1 = par->dirty_y1;
	par->dirty_y0 = INT_MAX;
	par->dirty_y1 = -1;
	spin_unlock_irqrestore(&par->lock, flags);
	if (y0 <= y1)
		cubefb_convert_rows(par, y0, y1);
}

/* ---- fbdev callbacks ---- */
static void cubefb_damage_range(struct fb_info *info, off_t off, size_t len)
{
	struct cubefb_par *par = info->par;
	u32 ll = info->fix.line_length;

	if (!len)
		return;
	cubefb_mark(par, off / ll, (off + len - 1) / ll);
}

static void cubefb_damage_area(struct fb_info *info, u32 x, u32 y, u32 width, u32 height)
{
	if (!height)
		return;
	cubefb_mark(info->par, y, y + height - 1);
}

FB_GEN_DEFAULT_DEFERRED_SYSMEM_OPS(cubefb, cubefb_damage_range, cubefb_damage_area)

static int cubefb_setcolreg(unsigned regno, unsigned red, unsigned green,
			    unsigned blue, unsigned transp, struct fb_info *info)
{
	struct cubefb_par *par = info->par;

	if (regno >= 16)
		return -EINVAL;
	par->pseudo_palette[regno] = ((red >> 8) << 16) | ((green >> 8) << 8) | (blue >> 8);
	return 0;
}

static int cubefb_blank(int blank, struct fb_info *info)
{
	return 0;	/* the TV signal stays on */
}

static const struct fb_ops cubefb_ops = {
	.owner		= THIS_MODULE,
	FB_DEFAULT_DEFERRED_OPS(cubefb),
	.fb_setcolreg	= cubefb_setcolreg,
	.fb_blank	= cubefb_blank,
};

static void cubefb_deferred_io(struct fb_info *info, struct list_head *pagereflist)
{
	struct cubefb_par *par = info->par;
	struct fb_deferred_io_pageref *pageref;
	u32 ll = info->fix.line_length;
	int y0 = INT_MAX, y1 = -1;

	list_for_each_entry(pageref, pagereflist, list) {
		int a = pageref->offset / ll;
		int b = (pageref->offset + PAGE_SIZE - 1) / ll;

		if (a < y0)
			y0 = a;
		if (b > y1)
			y1 = b;
	}
	if (y0 <= y1)
		cubefb_convert_rows(par, y0, y1);
}

static struct fb_deferred_io cubefb_defio = {
	.delay		= HZ / 30,
	.sort_pagereflist = false,
	.deferred_io	= cubefb_deferred_io,
};

static int cubefb_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fb_info *info;
	struct cubefb_par *par;
	struct resource *res;
	u32 width = 640, height = 480;
	void *vmem;
	size_t vsize;
	int ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	info = framebuffer_alloc(sizeof(struct cubefb_par), dev);
	if (!info)
		return -ENOMEM;
	par = info->par;
	par->info = info;
	par->width = width;
	par->height = height;
	par->xfb_stride = width * 2;
	spin_lock_init(&par->lock);
	par->dirty_y0 = INT_MAX;
	par->dirty_y1 = -1;
	INIT_DELAYED_WORK(&par->work, cubefb_work);

	par->xfb = ioremap(res->start, resource_size(res));
	if (!par->xfb) {
		ret = -ENOMEM;
		goto err_fb;
	}

	vsize = PAGE_ALIGN(width * height * 4);
	vmem = vzalloc(vsize);
	if (!vmem) {
		ret = -ENOMEM;
		goto err_map;
	}

	strscpy(info->fix.id, "cubefb", sizeof(info->fix.id));
	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.visual = FB_VISUAL_TRUECOLOR;
	info->fix.accel = FB_ACCEL_NONE;
	info->fix.line_length = width * 4;
	info->fix.smem_start = (unsigned long)vmem;
	info->fix.smem_len = vsize;

	info->var.xres = info->var.xres_virtual = width;
	info->var.yres = info->var.yres_virtual = height;
	info->var.bits_per_pixel = 32;
	info->var.red.offset = 16;
	info->var.red.length = 8;
	info->var.green.offset = 8;
	info->var.green.length = 8;
	info->var.blue.offset = 0;
	info->var.blue.length = 8;
	info->var.activate = FB_ACTIVATE_NOW;
	info->var.height = -1;
	info->var.width = -1;
	info->var.vmode = FB_VMODE_INTERLACED;

	info->fbops = &cubefb_ops;
	info->screen_buffer = vmem;
	info->screen_size = vsize;
	info->pseudo_palette = par->pseudo_palette;
	info->flags = FBINFO_VIRTFB;
	info->fbdefio = &cubefb_defio;

	fb_deferred_io_init(info);

	/* start from black */
	cubefb_convert_rows(par, 0, height - 1);

	ret = register_framebuffer(info);
	if (ret < 0)
		goto err_defio;

	platform_set_drvdata(pdev, info);
	fb_info(info, "GameCube TV framebuffer %ux%u (XFB at %pa)\n", width, height, &res->start);
	return 0;

err_defio:
	fb_deferred_io_cleanup(info);
	vfree(vmem);
err_map:
	iounmap(par->xfb);
err_fb:
	framebuffer_release(info);
	return ret;
}

static void cubefb_remove(struct platform_device *pdev)
{
	struct fb_info *info = platform_get_drvdata(pdev);
	struct cubefb_par *par = info->par;

	unregister_framebuffer(info);
	cancel_delayed_work_sync(&par->work);
	fb_deferred_io_cleanup(info);
	vfree(info->screen_buffer);
	iounmap(par->xfb);
	framebuffer_release(info);
}

static struct platform_driver cubefb_driver = {
	.probe = cubefb_probe,
	.remove_new = cubefb_remove,
	.driver = {
		.name = "cubefb",
	},
};

/*
 * The XFB lives at a fixed address chosen by the CubeLinux loader: the last
 * megabyte of the GameCube's 24 MiB, which the device tree leaves out of
 * "memory" so Linux never allocates it.
 */
#define CUBEFB_XFB_BASE	0x01700000
#define CUBEFB_XFB_SIZE	0x00100000

static struct platform_device *cubefb_pdev;

static int __init cubefb_init(void)
{
	struct resource res = DEFINE_RES_MEM(CUBEFB_XFB_BASE, CUBEFB_XFB_SIZE);
	int ret;

	if (!of_machine_is_compatible("nintendo,gamecube"))
		return -ENODEV;
	ret = platform_driver_register(&cubefb_driver);
	if (ret)
		return ret;
	cubefb_pdev = platform_device_register_simple("cubefb", -1, &res, 1);
	if (IS_ERR(cubefb_pdev)) {
		platform_driver_unregister(&cubefb_driver);
		return PTR_ERR(cubefb_pdev);
	}
	return 0;
}

static void __exit cubefb_exit(void)
{
	platform_device_unregister(cubefb_pdev);
	platform_driver_unregister(&cubefb_driver);
}

module_init(cubefb_init);
module_exit(cubefb_exit);

MODULE_DESCRIPTION("GameCube XFB framebuffer for CubeLinux");
MODULE_LICENSE("GPL");
