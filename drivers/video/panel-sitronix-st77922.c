#include <backlight.h>
#include <dm.h>
#include <dm/device_compat.h>
#include <fdtdec.h>
#include <log.h>
#include <mipi_dsi.h>
#include <panel.h>
#include <asm/gpio.h>
#include <dsi_host.h>
#include <linux/delay.h>
#include <linux/string.h>
#include "sunxi/sunxi_mipi_dsi.h"

/* ST77922 panel driver for Elegoo Centauri Carbon 2 */
#define ST77922_PAGE_CMD1	0xF0
#define ST77922_PAGE_CMD2	0xF1
#define ST77922_PAGE_CMD3	0xF2

struct st77922_cmd {
	u8 cmd;
	u8 len;
	u8 data[16];
};

struct st77922_panel_priv {
	struct udevice *backlight;
	struct gpio_desc reset;
	struct udevice *host;
	struct mipi_dsi_device device;
};

static const struct display_timing st77922_default_timing = {
	.pixelclock = {	20000000, 20000000, 20000000 },
	.hactive = { 532, 532, 532 },
	.hfront_porch = { 94, 94, 94 },
	.hback_porch = { 84, 84, 84 },
	.hsync_len = { 4, 4, 4 },
	.vactive = { 300, 300, 300 },
	.vfront_porch = { 134, 134, 134 },
	.vback_porch = { 24, 24, 24 },
	.vsync_len = { 4, 4, 4 },
	.flags = DISPLAY_FLAGS_HSYNC_LOW | DISPLAY_FLAGS_VSYNC_LOW |
		 DISPLAY_FLAGS_PIXDATA_NEGEDGE,
};

static int st77922_panel_get_display_timing(struct udevice *dev,
					    struct display_timing *timings);

static const struct st77922_cmd st77922_init_cmds[] = {
	{ 0xD0, 1, { 0x80 } },
	{ 0xF1, 1, { 0x00 } },
	{ 0x60, 3, { 0x00, 0x00, 0x00 } },
	{ 0x65, 1, { 0x00 } },
	{ 0x66, 2, { 0x00, 0x3F } },
	{ 0xBE, 3, { 0x1E, 0x01, 0x06 } },
	{ 0x70, 12, { 0x02, 0x7D, 0x12, 0x14, 0x30, 0x00, 0x07, 0x52, 0x01, 0x00, 0x00, 0x1A } },
	{ 0x71, 1, { 0xD0 } },
	{ 0x7B, 3, { 0x00, 0x08, 0x08 } },
	{ 0x80, 11, { 0x55, 0x62, 0x2F, 0x17, 0xF0, 0x52, 0x70, 0xD2, 0x52, 0x62, 0xEA } },
	{ 0x81, 4, { 0x26, 0x52, 0x72, 0x27 } },
	{ 0x84, 2, { 0x92, 0x25 } },
	{ 0x86, 14, { 0xC6, 0x04, 0xB1, 0x02, 0x58, 0x12, 0x58, 0x10, 0x13, 0x01, 0xAA, 0x00, 0xAA, 0xAA } },
	{ 0x87, 6, { 0x10, 0x10, 0x58, 0x00, 0x02, 0x3A } },
	{ 0x88, 15, { 0x00, 0x00, 0x2C, 0x10, 0x04, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x06 } },
	{ 0x89, 3, { 0x00, 0x00, 0x00 } },
	{ 0x8A, 11, { 0x13, 0x00, 0x2C, 0x00, 0x00, 0x2C, 0x10, 0x10, 0x00, 0x3E, 0x19 } },
	{ 0x8B, 9, { 0x15, 0xB1, 0xB1, 0x44, 0x96, 0x2C, 0x10, 0x97, 0x8E } },
	{ 0x8C, 13, { 0x1D, 0xB1, 0xB1, 0x44, 0x96, 0x2C, 0x10, 0x50, 0x0F, 0x01, 0xC5, 0x12, 0x09 } },
	{ 0x8D, 1, { 0x0C } },
	{ 0x8E, 6, { 0x33, 0x01, 0x0C, 0x13, 0x01, 0x01 } },
	{ 0x90, 9, { 0x00, 0x44, 0x33, 0x36, 0x00, 0x79, 0x40, 0xB6, 0xB6 } },
	{ 0x91, 9, { 0x00, 0x44, 0x33, 0x37, 0x00, 0x78, 0x40, 0xB6, 0xB6 } },
	{ 0x92, 10, { 0x02, 0x44, 0x55, 0x82, 0x86, 0x2F, 0x00, 0x04, 0x73, 0xB6 } },
	{ 0x93, 10, { 0x0C, 0x00, 0x11, 0x81, 0x87, 0x3F, 0x00, 0x00, 0x73, 0x73 } },
	{ 0x94, 6, { 0x08, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ 0x95, 5, { 0x1A, 0x1A, 0x00, 0x00, 0xFF } },
	{ 0x96, 12, { 0x44, 0x35, 0x07, 0x16, 0x20, 0x21, 0x07, 0x06, 0xB6, 0xB6, 0x00, 0x40 } },
	{ 0x97, 12, { 0x44, 0x35, 0x25, 0x34, 0x22, 0x23, 0x05, 0x04, 0xB6, 0xB6, 0x00, 0x40 } },
	{ 0xBA, 5, { 0x55, 0xB6, 0xB6, 0xB6, 0xB6 } },
	{ 0x9A, 7, { 0x40, 0x0C, 0x56, 0x01, 0x08, 0xB6, 0xB6 } },
	{ 0x9B, 7, { 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00 } },
	{ 0x9C, 13, { 0x00, 0x12, 0x00, 0x00, 0x00, 0x12, 0x00, 0x00, 0x00, 0x12, 0x00, 0x00, 0x00 } },
	{ 0x9D, 8, { 0x80, 0x15, 0x00, 0x07, 0x01, 0x80, 0x73, 0x73 } },
	{ 0x9E, 7, { 0x00, 0x00, 0x00, 0x00, 0x80, 0x1E, 0x01 } },
	{ 0x9F, 4, { 0xA0, 0x09, 0x00, 0x57 } },
	{ 0xB3, 7, { 0x00, 0x30, 0x0F, 0x00, 0x00, 0x00, 0x00 } },
	{ 0xB4, 12, { 0x10, 0x1C, 0x19, 0x14, 0x18, 0x01, 0x1D, 0x03, 0x12, 0x0A, 0x11, 0x08 } },
	{ 0xB5, 12, { 0x1C, 0x1C, 0x19, 0x14, 0x18, 0x00, 0x1D, 0x02, 0x10, 0x0B, 0x13, 0x09 } },
	{ 0xB6, 7, { 0xFF, 0xFF, 0x00, 0x0F, 0xFE, 0x0F, 0xFE } },
	{ 0xB7, 16, { 0x00, 0x09, 0x10, 0x0B, 0x0A, 0x06, 0x38, 0x04, 0x04, 0x4F, 0x09, 0x15, 0x15, 0x30, 0x37, 0x0F } },
	{ 0xB8, 16, { 0x00, 0x09, 0x0F, 0x0A, 0x09, 0x05, 0x37, 0x03, 0x03, 0x4F, 0x09, 0x15, 0x15, 0x31, 0x36, 0x0F } },
	{ 0xB9, 2, { 0x23, 0x23 } },
	{ 0xBF, 6, { 0x0F, 0x13, 0x13, 0x09, 0x09, 0x09 } },
	{ 0xF2, 1, { 0x00 } },
	{ 0x73, 5, { 0x04, 0xBA, 0x12, 0x58, 0x5B } },
	{ 0x77, 5, { 0x6B, 0x5B, 0xFD, 0xC3, 0xC5 } },
	{ 0x7A, 2, { 0x15, 0x27 } },
	{ 0x7B, 2, { 0x04, 0x57 } },
	{ 0x7E, 2, { 0x01, 0x0E } },
	{ 0xBF, 1, { 0x36 } },
	{ 0xE3, 2, { 0x43, 0x43 } },
	{ 0xF0, 1, { 0x00 } },
	{ 0x21, 0, { 0 } },
};

static int st77922_send_init_sequence(struct mipi_dsi_device *dsi)
{
	int i, ret;
	u8 buf[17];

	for (i = 0; i < ARRAY_SIZE(st77922_init_cmds); i++) {
		const struct st77922_cmd *c = &st77922_init_cmds[i];

		buf[0] = c->cmd;
		memcpy(&buf[1], c->data, c->len);

		ret = mipi_dsi_dcs_write_buffer(dsi, buf, c->len + 1);
		if (ret < 0) {
			dev_err(dsi->dev, "st77922 init cmd 0x%02x failed: %d\n",
				c->cmd, ret);
			return ret;
		}
	}

	return 0;
}

struct mipi_dsi_host *sunxi_mipi_dsi_host(struct udevice *dev);

static int st77922_panel_enable(struct udevice *dev)
{
	struct st77922_panel_priv *priv = dev_get_priv(dev);
	struct mipi_dsi_panel_plat *plat = dev_get_plat(dev);
	struct mipi_dsi_device *dsi = &priv->device;
	int ret;

	printf("st77922: enable_backlight entry\n");
	ret = uclass_get_device(UCLASS_DSI_HOST, 0, &priv->host);
	if (ret) {
		printf("st77922: no dsi host: %d\n", ret);
		return ret;
	}

	dsi->dev = dev;
	dsi->host = sunxi_mipi_dsi_host(priv->host);
	if (!dsi->host) {
		printf("st77922: no mipi_dsi_host\n");
		return -ENODEV;
	}
	strlcpy(dsi->name, "st77922", sizeof(dsi->name));
	dsi->channel = 0;
	dsi->lanes = plat->lanes;
	dsi->format = plat->format;
	dsi->mode_flags = plat->mode_flags;
	plat->device = dsi;

	printf("st77922: attach\n");
	ret = mipi_dsi_attach(dsi);
	if (ret < 0)
		return ret;

	{
		struct display_timing timings;

		ret = st77922_panel_get_display_timing(dev, &timings);
		if (ret)
			return ret;

		printf("st77922: host_init\n");
		ret = dsi_host_init(priv->host, dsi, &timings, 1, NULL);
		if (ret < 0) {
			printf("st77922: dsi_host_init failed: %d\n", ret);
			return ret;
		}
	}

	/*
	 * Put the DSI host into command (LPDT) mode before we talk to the
	 * panel. This first dsi_host_enable() call configures clocks and D-PHY;
	 * a second call after panel init will start HS video mode.
	 */
	printf("st77922: host_enable 1\n");
	ret = dsi_host_enable(priv->host);
	if (ret < 0)
		return ret;
	printf("st77922: host_enable 1 ret=%d\n", ret);

	/* Active-low reset: assert, hold, release. */
	printf("st77922: panel reset pulse\n");
	dm_gpio_set_value(&priv->reset, true);
	mdelay(20);
	dm_gpio_set_value(&priv->reset, false);
	mdelay(120);

	printf("st77922: sending init sequence\n");
	ret = st77922_send_init_sequence(dsi);
	if (ret)
		return ret;

	ret = mipi_dsi_dcs_set_pixel_format(dsi,
					    (MIPI_DCS_PIXEL_FMT_24BIT << 4) |
					    MIPI_DCS_PIXEL_FMT_24BIT);
	if (ret < 0)
		return ret;

	{
		u8 addr_mode[] = { 0x36, 0x00 };
		ret = mipi_dsi_dcs_write_buffer(dsi, addr_mode, 2);
		if (ret < 0)
			return ret;
	}

	ret = mipi_dsi_dcs_set_column_address(dsi, 0, 531);
	if (ret < 0)
		return ret;

	ret = mipi_dsi_dcs_set_page_address(dsi, 0, 299);
	if (ret < 0)
		return ret;

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0)
		return ret;
	mdelay(120);

	printf("st77922: display on\n");
	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0)
		return ret;

	ret = mipi_dsi_dcs_set_tear_on(dsi, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	if (ret < 0)
		return ret;
	mdelay(100);

	/*
	 * The DSI host was left in command mode by the first dsi_host_enable()
	 * call. Now that the panel is initialized, start HS video mode.
	 */
	printf("st77922: enabling HS video\n");
	ret = dsi_host_enable(priv->host);
	if (ret < 0)
		return ret;

	if (priv->backlight) {
		printf("st77922: enabling backlight\n");
		ret = backlight_enable(priv->backlight);
		if (ret && ret != -ENOSYS && ret != -ENOENT)
			return ret;
	}

	return 0;
}

static int st77922_panel_get_display_timing(struct udevice *dev,
					    struct display_timing *timings)
{
	int ret;

	ret = fdtdec_decode_display_timing(gd->fdt_blob, dev_of_offset(dev),
					   0, timings);
	if (ret)
		memcpy(timings, &st77922_default_timing, sizeof(*timings));

	return 0;
}

static int st77922_panel_of_to_plat(struct udevice *dev)
{
	struct mipi_dsi_panel_plat *plat = dev_get_plat(dev);
	struct st77922_panel_priv *priv = dev_get_priv(dev);
	int ret;

	printf("panel of_to_plat start\n"); // ponytail: debug

	plat->lanes = 1;
	plat->format = MIPI_DSI_FMT_RGB888;
	plat->mode_flags = MIPI_DSI_MODE_VIDEO |
			   MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
			   MIPI_DSI_MODE_EOT_PACKET |
			   MIPI_DSI_MODE_LPM;

	ret = gpio_request_by_name(dev, "reset-gpios", 0, &priv->reset,
				   GPIOD_IS_OUT | GPIOD_ACTIVE_LOW);
	if (ret && ret != -ENOENT) {
		printf("reset gpio request failed: %d\n", ret); // ponytail: debug
		return ret;
	}
	printf("panel of_to_plat OK\n"); // ponytail: debug

	return 0;
}

static int st77922_panel_probe(struct udevice *dev)
{
	struct st77922_panel_priv *priv = dev_get_priv(dev);
	int ret;

	ret = uclass_get_device_by_phandle(UCLASS_PANEL_BACKLIGHT, dev,
					   "backlight", &priv->backlight);
	if (ret) {
		dev_err(dev, "Cannot get backlight: %d\n", ret);
		priv->backlight = NULL;
	}

	return 0;
}

static const struct panel_ops st77922_panel_ops = {
	.enable_backlight = st77922_panel_enable,
	.get_display_timing = st77922_panel_get_display_timing,
};

static const struct udevice_id st77922_panel_ids[] = {
	{ .compatible = "sitronix,st77922" },
	{ }
};

U_BOOT_DRIVER(panel_sitronix_st77922) = {
	.name		= "panel-sitronix-st77922",
	.id		= UCLASS_PANEL,
	.of_match	= st77922_panel_ids,
	.ops		= &st77922_panel_ops,
	.of_to_plat	= st77922_panel_of_to_plat,
	.probe		= st77922_panel_probe,
	.priv_auto	= sizeof(struct st77922_panel_priv),
	.plat_auto	= sizeof(struct mipi_dsi_panel_plat),
};
