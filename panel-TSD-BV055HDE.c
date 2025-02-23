// SPDX-License-Identifier: GPL-2.0
/*
 * TSD BV055HDE 5.5" MIPI-DSI panel driver
 *
 * Copyright (C) 420nm 2020
 */

#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/media-bus-format.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>

#include <video/display_timing.h>
#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>
#include <linux/of.h>
#include <linux/backlight.h>

#include <linux/sysfs.h> 
#include <linux/kobject.h> 

#define DRV_NAME "panel-TSD-BV055HDE"

#define ST7703_CMD_SETRSO	0xB2
#define ST7703_CMD_SETRGB	0xB3
#define ST7703_CMD_SETCYC	0xB4
#define ST7703_CMD_SETBGP	0xB5
#define ST7703_CMD_SETVCOM	0xB6
#define ST7703_CMD_SETECP	0xB8
#define ST7703_CMD_SETEXTC	0xB9
#define ST7703_CMD_SETMIPI	0xBA
#define ST7703_CMD_SETVDC	0xBC
#define ST7703_CMD_SETPCR	0xBF
#define ST7703_CMD_SETSCR	0xC0
#define ST7703_CMD_SETPOWER	0xC1
#define ST7703_CMD_SETPANEL	0xCC
#define ST7703_CMD_SETGAMMA	0xE0
#define ST7703_CMD_SETEQ	0xE3
#define ST7703_CMD_SETGIP1	0xE9
#define ST7703_CMD_SETGIP2	0xEA
#define ST7703_CMD_SLEEPOUT	0x11
#define ST7703_CMD_DISPON	0x29

#define ST7703_CMD_ALL_PIXEL_ON	 0x23
/*
#define BV055HDE_Width 720
#define BV055HDE_Height 1280

#define BV055HDE_VFP 16
#define BV055HDE_VBP 14
#define BV055HDE_VSA 4  

#define BV055HDE_HFP 40
#define BV055HDE_HBP 40
#define BV055HDE_HSA 20
*/

#define BV055HDE_Width 720
#define BV055HDE_Height 1280

#define BV055HDE_VFP 3
#define BV055HDE_VBP 10
#define BV055HDE_VSA 24

#define BV055HDE_HFP 48
#define BV055HDE_HBP 32
#define BV055HDE_HSA 80


int BV055HDE_loaded_state = 0;

EXPORT_SYMBOL(BV055HDE_loaded_state);


#define BV055HDE_DEBUG

#ifdef BV055HDE_DEBUG
#define BVSTACKTRACE \
	do { \
		printk(KERN_WARNING "BV055HDE Stacktrace: %s:%d\n", __FUNCTION__, __LINE__); \
	} while(0)
#define BVSTACKTRACE1I(I) \
    do { \
        printk(KERN_WARNING "BV055HDE Stacktrace: %s:%d, I:%d\n", __FUNCTION__, __LINE__, (int)I); \
    } while(0)
#else
#define BVSTACKTRACE do { ; } while(0)
#define BVSTACKTRACE1I(I) do { (void)I; } while(0)
#endif

struct BV055HDE {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *ldo_gpio;
	//struct backlight_device *backlight;
	int prepared;

	struct kobject *panel_dev_kob;
};

static int BV055HDE_disable(struct drm_panel *panel);
static int BV055HDE_unprepare(struct drm_panel *panel);

static int BV055HDE_prepare(struct drm_panel *panel);
static int BV055HDE_enable(struct drm_panel *panel);

static struct BV055HDE *staticctx = 0;

volatile int BV055HDE_onstate = 0;

static ssize_t bv_sysfs_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	BVSTACKTRACE;
	return sprintf(buf, "%d", BV055HDE_onstate);
}

static ssize_t bv_sysfs_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	BVSTACKTRACE;
	sscanf(buf, "%d", &BV055HDE_onstate);
	if (BV055HDE_onstate == 0)
	{
		BV055HDE_disable(&staticctx->panel);
		BV055HDE_unprepare(&staticctx->panel);
		BV055HDE_onstate = 0;
	} else {
		BV055HDE_prepare(&staticctx->panel);
		BV055HDE_enable(&staticctx->panel);
		BV055HDE_onstate = 1;
	}
	return count;
}

struct kobj_attribute BV055HDE_onstate_attr = __ATTR(BV055HDE_onstate, 0660, bv_sysfs_show, bv_sysfs_store);

static int BV055HDE_init_sysfs(struct BV055HDE *info)
{
	info->panel_dev_kob = kobject_create_and_add("420-DSI", kernel_kobj);

	if (sysfs_create_file(info->panel_dev_kob, &BV055HDE_onstate_attr.attr)) {
		printk(KERN_WARNING "BV055HDE: Failed to create sysfs file");
	}

	return 0;
}

static inline struct BV055HDE *panel_to_BV055HDE(struct drm_panel *panel)
{
	return container_of(panel, struct BV055HDE, panel);
}

static const struct drm_display_mode BV055HDE_default_mode = {
	.vrefresh    = 60,
	.clock	     = 69500,
	.hdisplay    = BV055HDE_Width,
	.hsync_start = BV055HDE_Width + BV055HDE_HFP,
	.hsync_end   = BV055HDE_Width + BV055HDE_HFP + BV055HDE_HBP,
	.htotal	     = BV055HDE_Width + BV055HDE_HFP + BV055HDE_HBP + BV055HDE_HSA,
	.vdisplay    = BV055HDE_Height,
	.vsync_start = BV055HDE_Height + BV055HDE_VFP,
	.vsync_end   = BV055HDE_Height + BV055HDE_VFP + BV055HDE_VBP,
	.vtotal	     = BV055HDE_Height + BV055HDE_VFP + BV055HDE_VBP + BV055HDE_VSA,
	.flags	     = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC,
	.width_mm	= 68,
	.height_mm   = 121,
};

#define DSI_WRITE(dsidev, sequence, seqlength, retval) \
	do { \
		retval = mipi_dsi_dcs_write_buffer( dsidev , sequence , seqlength ); \
		if (retval < 0) { \
			printk(KERN_CRIT "DSI TX (%s) ERROR (r:%d) (%s:%d)\n", \
				#sequence, \
				retval, \
				__FUNCTION__, \
				__LINE__); \
			return retval; \
		} \
	} while(0)

static int BV055HDE_init_sequence(struct BV055HDE *ctx)
{
	/*
	 * Init sequence was supplied by the panel vendor TSD.
	 * Panel is BV055HDE, IC driver ST7703.
	 */
	int ret;

	static const u8 SETEXTC[4] = {ST7703_CMD_SETEXTC,
		0xF1, 0x12, 0x83};

	static const u8 SETMIPI[28] = {ST7703_CMD_SETMIPI, 
		0x33, 0x81, 0x05, 0xF9, 0x0E, 0x0E, 0x20, 0x00, 0x00, 0x00,	0x00, 0x00, 
		0x00, 0x00, 0x44, 0x25, 0x00, 0x91, 0x0A, 0x00, 0x00, 0x02, 0x4F, 0xD1, 
		0x00, 0x00, 0x37};

	static const u8 SETECP[5] = {ST7703_CMD_SETECP,
		0x23, 0x22, 0x20, 0x03};
	/* byte1: Power Mode 0x23:ICMode 0x73:3Power */

	static const u8 SETPCR[4] = {ST7703_CMD_SETPCR,
		0x02, 0x11, 0x00};

	static const u8 SETRGB[11] = {ST7703_CMD_SETRGB,
		0x0C, 0x10, 0x0A, 0x50, 0x03, 0xFF, 0x00, 0x00, 0x00, 0x00};

	static const u8 SETSCR[10] = {ST7703_CMD_SETSCR,
		0x73, 0x73, 0x50, 0x50, 0x00, 0x00, 0x08, 0x70, 0x00};

	static const u8 SETVDC[2] = {ST7703_CMD_SETVDC,
		0x46};

	static const u8 SETPANEL[2] = {ST7703_CMD_SETPANEL,
		0x0B};

	static const u8 SETCYC[2] = {ST7703_CMD_SETCYC,
		0x80};

	static const u8 SETRSO[4] = {ST7703_CMD_SETRSO,
		0xC8, 0x02, 0x30};

	static const u8 SETEQ[15] = {ST7703_CMD_SETEQ,
		0x07, 0x07, 0x0B, 0x0B, 0x03, 0x0B, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x80,
		0xC0, 0x10};

	static const u8 SETPOWER[13] = {ST7703_CMD_SETPOWER,
		0x25, 0x00, 0x1E, 0x1E, 0x77, 0xF1, 0xFF, 0xFF, 0xCC, 0xCC, 0x77, 0x77};

	static const u8 SETBGP[3] = {ST7703_CMD_SETBGP,
		0x0A, 0x0A};

	static const u8 SETVCOM[3] = {ST7703_CMD_SETVCOM,
		0x50, 0x50};

	static const u8 SETGIP1[64] = {ST7703_CMD_SETGIP1,
		0xC4, 0x10, 0x0F, 0x00, 0x00, 0xB2, 0xB8, 0x12, 0x31, 0x23, 0x48, 0x8B,
		0xB2, 0xB8, 0x47, 0x20, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x30, 0x00, 0x00, 0x00, 0x02, 0x46, 0x02, 0x88, 0x88, 0x88, 0x88, 0x88,
		0x88, 0x88, 0xF8, 0x13, 0x57, 0x13, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88,
		0x88, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00};

	static const u8 SETGIP2[62] = {ST7703_CMD_SETGIP2,
		0x00, 0x1A, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x75, 0x31, 0x31, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x8F, 0x64,
		0x20, 0x20, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x8F, 0x23, 0x10,
		0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00};

	static const u8 SETGAMMA[35] = {ST7703_CMD_SETGAMMA,
		0x00, 0x07, 0x0C, 0x2A, 0x38, 0x3F, 0x3B, 0x34, 0x05, 0x0C, 0x10, 0x13,
		0x14, 0x14, 0x15, 0x0D, 0x0B, 0x00, 0x07, 0x0C, 0x2A, 0x38, 0x3F, 0x3B,
		0x34, 0x05, 0x0C, 0x10, 0x13, 0x14, 0x14, 0x15, 0x0D, 0x0B};

	static const u8 SLEEPOUT[1] = {0x11};

	static const u8 DISPON[1] = {0x29};

	static const u8 ALLPIXON[1] = {ST7703_CMD_ALL_PIXEL_ON};

	BVSTACKTRACE;

	/* #01 */ DSI_WRITE(ctx->dsi, SETEXTC, 4, ret);
	/* #02 */ DSI_WRITE(ctx->dsi, SETMIPI, 28, ret);
	/* #03 */ DSI_WRITE(ctx->dsi, SETECP, 5, ret);
	/* #04 */ DSI_WRITE(ctx->dsi, SETPCR, 4, ret);
	/* #05 */ DSI_WRITE(ctx->dsi, SETRGB, 11, ret);
	/* #06 */ DSI_WRITE(ctx->dsi, SETSCR, 10, ret);
	/* #07 */ DSI_WRITE(ctx->dsi, SETVDC, 2, ret);
	/* #08 */ DSI_WRITE(ctx->dsi, SETPANEL, 2, ret);
	/* #09 */ DSI_WRITE(ctx->dsi, SETCYC, 2, ret);
	/* #10 */ DSI_WRITE(ctx->dsi, SETRSO, 4, ret);
	/* #11 */ DSI_WRITE(ctx->dsi, SETEQ, 15, ret);
	/* #12 */ DSI_WRITE(ctx->dsi, SETPOWER, 13, ret);
	/* #13 */ DSI_WRITE(ctx->dsi, SETBGP, 3, ret);
	/* #14 */ DSI_WRITE(ctx->dsi, SETVCOM, 3, ret);
	/* #15 */ DSI_WRITE(ctx->dsi, SETGIP1, 64, ret);
	/* #16 */ DSI_WRITE(ctx->dsi, SETGIP2, 62, ret);
	/* #17 */ DSI_WRITE(ctx->dsi, SETGAMMA, 35, ret);

	msleep(250);

	/* #18 */ DSI_WRITE(ctx->dsi, DISPON, 1, ret);

	msleep(50);
	
	/* #19 */ DSI_WRITE(ctx->dsi, SLEEPOUT, 1, ret);

	msleep(100);

	/* #20 */ DSI_WRITE(ctx->dsi, DISPON, 1, ret);

	msleep(200);

	//printk(KERN_WARNING "SUCCESS: %s:%d\n", __FUNCTION__, __LINE__);
	return 0;
}

static void bv_ldoseton(struct BV055HDE *ctx, int ms)
{
	gpiod_set_raw_value(ctx->ldo_gpio, 1);
	printk(KERN_WARNING "bv_ldoseton LDO:3V3  (%d)\n", __LINE__);
	if (ms > 0)
		usleep_range( 900 * ms, 1100 * ms );
}

static void bv_ldosetoff(struct BV055HDE *ctx, int ms)
{
	gpiod_set_raw_value(ctx->ldo_gpio, 0);
	printk(KERN_WARNING "bv_ldosetoff LDO:0V  (%d)\n", __LINE__);
	if (ms > 0)
		usleep_range( 900 * ms, 1100 * ms );
}

static void bv_xres_low(struct BV055HDE *ctx, int ms)
{
	gpiod_set_raw_value(ctx->reset_gpio, 0);
	printk(KERN_WARNING "bv_xres_low XRES:0V (%d)\n", __LINE__);
	if (ms > 0)
		usleep_range( 900 * ms, 1100 * ms );
}

static void bv_xres_highz(struct BV055HDE *ctx, int ms)
{
	gpiod_set_raw_value(ctx->reset_gpio, 1);
	printk(KERN_WARNING "bv_xres_highz XRES:1V8 (%d)\n", __LINE__);
	if (ms > 0)
		usleep_range( 900 * ms, 1100 * ms );
}

static void BV055HDE_power_cycle(struct BV055HDE *ctx)
{
	BVSTACKTRACE;

	////// make sure /reset is pulled low
	bv_xres_low(ctx, 5);

	////// turn off display power LDOs
	bv_ldosetoff(ctx, 10);

	////// turn on display power LDOs, wait 50ms
	bv_ldoseton(ctx, 50);

	////// as per datasheet, release /reset for 10ms
	bv_xres_highz(ctx, 10);

	////// as per datasheet, pull /reset low for 50ms
	bv_xres_low(ctx, 50);

	////// as per datasheet, release /reset and wait 150ms before DSI transfer
	bv_xres_highz(ctx, 150);
}

static void BV055HDE_power_off(struct BV055HDE *ctx)
{
	BVSTACKTRACE;

	////// as per datasheet, pull /reset low for 120ms
	bv_xres_low(ctx, 120);

	////// then turn off power supply LDOs
	bv_ldosetoff(ctx, 0);
}

static int BV055HDE_prepare(struct drm_panel *panel)
{
	struct BV055HDE *ctx = panel_to_BV055HDE(panel);
	//int ret;

	BVSTACKTRACE1I(ctx->prepared);

	staticctx = ctx;

	if (ctx->prepared)
		return 0;

	BV055HDE_power_cycle(ctx);
	
	ctx->prepared = 1;

	return 0;
}

static int BV055HDE_enable(struct drm_panel *panel)
{
	int ret;
	struct BV055HDE *ctx = panel_to_BV055HDE(panel);
	
	BVSTACKTRACE;

	ret = BV055HDE_init_sequence(ctx);
	if (ret < 0)
	{
		printk(KERN_CRIT "ERROR: Panel Init Sequence failed, ret:%d (%s:%d)\n",
			ret, 
			__FUNCTION__, 
			__LINE__);
		return ret;
	}

	BV055HDE_onstate = 1;

	return 0;
}

static int BV055HDE_disable(struct drm_panel *panel)
{
	struct BV055HDE *ctx = panel_to_BV055HDE(panel);

	BVSTACKTRACE;

	return mipi_dsi_dcs_set_display_off(ctx->dsi);
}

static int BV055HDE_unprepare(struct drm_panel *panel)
{
	//int ret;
	struct BV055HDE *ctx = panel_to_BV055HDE(panel);

	BVSTACKTRACE1I(ctx->prepared);

	if (!ctx->prepared)
		return 0;

	mipi_dsi_dcs_enter_sleep_mode(ctx->dsi);
	
	msleep(150);

	BV055HDE_power_off(ctx);

	ctx->prepared = 0;

	BV055HDE_onstate = 0;

	return 0;
}

static int BV055HDE_get_modes(struct drm_panel *panel)
{
	static const u32 bus_format = MEDIA_BUS_FMT_RGB888_1X24;
	struct drm_connector *connector = panel->connector;
	struct drm_display_mode *mode;
	int ret;

	BVSTACKTRACE;

	mode = drm_mode_duplicate(connector->dev, &BV055HDE_default_mode);
	if (!mode)
	{
		printk(KERN_CRIT "ERROR: Failed to add mode: %ux%u@%u (%s:%d)\n",
			BV055HDE_default_mode.hdisplay, 
			BV055HDE_default_mode.vdisplay,
			BV055HDE_default_mode.vrefresh,
			__FUNCTION__, 
			__LINE__);
		return -ENOMEM;
	}


	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_set_name(mode);

	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;

	ret = drm_display_info_set_bus_formats(&panel->connector->display_info,
					       &bus_format, 1);
	if (ret)
	{
		printk(KERN_CRIT "ERROR: Failed to set bus format: %d\n", ret);
		return ret; 
	}

	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs BV055HDE_drm_funcs = {
	.prepare   = BV055HDE_prepare,
	.unprepare = BV055HDE_unprepare,
	.enable	   = BV055HDE_enable,
	.disable   = BV055HDE_disable,
	.get_modes = BV055HDE_get_modes,
};

static int BV055HDE_probe(struct mipi_dsi_device *dsi)
{
	struct device_node *np;
	struct BV055HDE *ctx;
	int ret;

	BVSTACKTRACE;

	ctx = devm_kzalloc(&dsi->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, ctx);
	ctx->dsi = dsi;

	drm_panel_init(&ctx->panel);
	ctx->panel.dev = &dsi->dev;
	ctx->panel.funcs = &BV055HDE_drm_funcs;

	ctx->reset_gpio = devm_gpiod_get(&dsi->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
	{
		DRM_DEV_ERROR(&dsi->dev, "cannot get reset gpio\n");
		return PTR_ERR(ctx->reset_gpio);
	}

	ctx->ldo_gpio = devm_gpiod_get(&dsi->dev, "ldoenable", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->ldo_gpio))
	{
		DRM_DEV_ERROR(&dsi->dev, "cannot get LDO enable gpio\n");
		return PTR_ERR(ctx->ldo_gpio);
	}

	/**************************************************************************
	np = of_parse_phandle(dsi->dev.of_node, "backlight", 0);
	if (np) {
		ctx->backlight = of_find_backlight_by_node(np);
		of_node_put(np);

		if (!ctx->backlight)
			return -EPROBE_DEFER;
	}
	**************************************************************************/

	dev_set_drvdata(&dsi->dev, ctx);

	drm_panel_add(&ctx->panel);
	/*if (ret < 0)
	{
		DRM_DEV_ERROR(&dsi->dev, "drm_panel_add failed (%d).\n", ret);
		return ret;
	}*/

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_LPM;
	/* tested:
		u32 flags = MIPI_DSI_MODE_VIDEO |  MIPI_DSI_MODE_LPM;

		dsi->mode_flags |= 0
			- nothing?

		dsi->mode_flags |= MIPI_DSI_MODE_VIDEO_BURST
			- tearing
		
		dsi->mode_flags |=  MIPI_DSI_MODE_VIDEO_SYNC_PULSE
			- tearing


		dsi->mode_flags |= 0 | MIPI_DSI_MODE_VIDEO_HSE
			- nothing?

		dsi->mode_flags |= MIPI_DSI_MODE_VIDEO_BURST | MIPI_DSI_MODE_VIDEO_HSE
			- tearing
		
		dsi->mode_flags |=  MIPI_DSI_MODE_VIDEO_SYNC_PULSE | MIPI_DSI_MODE_VIDEO_HSE
			- tearing

		dsi->mode_flags |=  MIPI_DSI_MODE_VIDEO_BURST | MIPI_DSI_MODE_VIDEO_HSE
			- tearing

		dsi->mode_flags |= MIPI_DSI_MODE_VIDEO_BURST | MIPI_DSI_MODE_VIDEO_SYNC_PULSE | MIPI_DSI_MODE_VIDEO_HSE
			- tearing

	*/
	//dsi->mode_flags |= MIPI_DSI_MODE_VIDEO_SYNC_PULSE;
	dsi->mode_flags |= MIPI_DSI_MODE_VIDEO_BURST;
	dsi->mode_flags |= MIPI_DSI_MODE_VIDEO_HSE;/*
	dsi->mode_flags |=  MIPI_DSI_MODE_VIDEO_AUTO_VERT;*/
	


	ret = mipi_dsi_attach(dsi);
	if (ret < 0)
	{
		DRM_DEV_ERROR(&dsi->dev,
			      "mipi_dsi_attach failed (%d). Is host ready?\n",
			      ret);
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	DRM_DEV_INFO(&dsi->dev, "%ux%u@%u %ubpp dsi %udl - ready\n",
		     BV055HDE_default_mode.hdisplay, 
		     BV055HDE_default_mode.vdisplay,
		     BV055HDE_default_mode.vrefresh,
		     mipi_dsi_pixel_format_to_bpp(dsi->format), 
		     dsi->lanes);

	BV055HDE_loaded_state = 1;

	BV055HDE_init_sysfs(ctx);

	return ret;
}

static void BV055HDE_shutdown(struct mipi_dsi_device *dsi)
{	
	struct BV055HDE *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	BVSTACKTRACE;

	ret = drm_panel_unprepare(&ctx->panel);
	if (ret < 0)
		DRM_DEV_ERROR(&dsi->dev, "Failed to unprepare panel: %d\n",
			      ret);

	ret = drm_panel_disable(&ctx->panel);
	if (ret < 0)
		DRM_DEV_ERROR(&dsi->dev, "Failed to disable panel: %d\n",
			      ret);
}

static int BV055HDE_remove(struct mipi_dsi_device *dsi)
{
	struct BV055HDE *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	BVSTACKTRACE;

	BV055HDE_shutdown(dsi);

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		DRM_DEV_ERROR(&dsi->dev, 
			"Failed to detach from DSI host: %d\n", 
			ret);

	drm_panel_remove(&ctx->panel);

	sysfs_remove_file(kernel_kobj, &BV055HDE_onstate_attr.attr);

	kobject_put(ctx->panel_dev_kob);

	return ret;
}

static const struct of_device_id BV055HDE_of_match[] = {
	{ .compatible = "TSD,BV055HDE" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, BV055HDE_of_match);

static struct mipi_dsi_driver BV055HDE_driver = {
	.probe	= BV055HDE_probe,
	.remove = BV055HDE_remove,
	.shutdown = BV055HDE_shutdown,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = BV055HDE_of_match,
	},
};

static int __init BV055HDE_driver_init(void)
{
	int ret;

	BVSTACKTRACE;

	ret = mipi_dsi_driver_register_full(&BV055HDE_driver, THIS_MODULE);

	printk(KERN_WARNING "mipi_dsi_driver_register_full (ret:%d)\n", ret);

	return ret;
}
module_init(BV055HDE_driver_init);

static void __exit BV055HDE_driver_exit(void)
{
	BVSTACKTRACE;

	mipi_dsi_driver_unregister(&BV055HDE_driver);

}
module_exit(BV055HDE_driver_exit);

MODULE_AUTHOR("Markus Opheiden <markus.opheiden@420nm.net>");
MODULE_DESCRIPTION("DRM driver for TSD BV055HDE MIPI DSI panel");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("1.0");
