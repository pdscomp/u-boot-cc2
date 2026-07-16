// SPDX-License-Identifier: GPL-2.0+
/*
 * Allwinner LCD driver
 *
 * (C) Copyright 2017 Vasily Khoruzhick <anarsoul@gmail.com>
 */

#include <display.h>
#include <log.h>
#include <panel.h>
#include <video_bridge.h>
#include <video.h>
#include <dm.h>
#include <dm/lists.h>
#include <dm/ofnode.h>
#include <dm/ofnode_graph.h>
#include <dm/root.h>
#include <edid.h>
#include <asm/io.h>
#include <clk.h>
#include <asm/arch/clock.h>
#include <asm/arch/lcdc.h>
#include <asm/global_data.h>
#include <asm/gpio.h>
#include <sunxi_gpio.h>

#ifdef CONFIG_VIDEO_SUNXI_MIPI_DSI
#include "sunxi_tcon.h"
#endif

struct sunxi_lcd_priv {
	struct display_timing timing;
	int panel_bpp;
	bool rb_swap;
#ifdef CONFIG_VIDEO_SUNXI_MIPI_DSI
	bool has_dsi;
	ofnode dsi_panel;
	struct udevice *panel;
#endif
};

#ifdef CONFIG_VIDEO_SUNXI_MIPI_DSI
static int sunxi_lcd_get_dsi_host(struct sunxi_lcd_priv *priv,
				  struct udevice **dsi_host)
{
	ofnode endpoint, dsi_node;
	ofnode parent_node;
	struct udevice *parent;
	int ret;

	if (!ofnode_valid(priv->dsi_panel))
		return -ENODEV;

	endpoint = ofnode_graph_get_endpoint_by_regs(priv->dsi_panel, -1, -1);
	if (!ofnode_valid(endpoint))
		return -ENODEV;

	dsi_node = ofnode_graph_get_remote_port_parent(endpoint);
	if (!ofnode_valid(dsi_node))
		return -ENODEV;

	ret = uclass_get_device_by_ofnode(UCLASS_DSI_HOST, dsi_node, dsi_host);
	if (!ret)
		goto bind_panel;
	if (ret != -ENODEV)
		return ret;

	parent_node = ofnode_get_parent(dsi_node);
	if (device_find_global_by_ofnode(parent_node, &parent))
		parent = dm_root();

	ret = lists_bind_fdt(parent, dsi_node, NULL, NULL, false);
	if (ret)
		return ret;

	ret = uclass_get_device_by_ofnode(UCLASS_DSI_HOST, dsi_node, dsi_host);
	if (!ret)
		goto bind_panel;
	if (ret != -ENODEV)
		return ret;

	ret = device_bind_driver_to_node(parent, "sunxi-mipi-dsi",
					 ofnode_get_name(dsi_node), dsi_node,
					 NULL);
	if (ret && ret != -EEXIST)
		return ret;

	ret = uclass_get_device_by_ofnode(UCLASS_DSI_HOST, dsi_node, dsi_host);
	if (ret)
		return ret;

bind_panel:
	if (!ofnode_valid(priv->dsi_panel))
		return 0;

	ret = lists_bind_fdt(*dsi_host, priv->dsi_panel, &priv->panel, NULL,
			     false);
	if (ret && ret != -EEXIST)
		return ret;

	return uclass_get_device_by_ofnode(UCLASS_PANEL, priv->dsi_panel,
					   &priv->panel);
}

#endif

static void sunxi_lcdc_config_pinmux(void)
{
#if defined(CONFIG_MACH_SUN50I)
	int pin;

	for (pin = SUNXI_GPD(0); pin <= SUNXI_GPD(21); pin++) {
		sunxi_gpio_set_cfgpin(pin, SUNXI_GPD_LCD0);
		sunxi_gpio_set_drv(pin, 3);
	}

#elif defined(CONFIG_MACH_SUN8I_R528)
	int pin;

	for (pin = SUNXI_GPD(0); pin <= SUNXI_GPD(21); pin++) {
		sunxi_gpio_set_cfgpin(pin, SUNXI_GPD_LCD0);
		sunxi_gpio_set_drv(pin, 3);
	}
	for (pin = SUNXI_GPB(2); pin <= SUNXI_GPB(7); pin++) {
		sunxi_gpio_set_cfgpin(pin, 2);
		sunxi_gpio_set_drv(pin, 3);
	}
#endif
}

static int sunxi_lcd_enable(struct udevice *dev, int bpp,
			    const struct display_timing *edid)
{
	struct sunxi_ccm_reg * const ccm =
	       (struct sunxi_ccm_reg *)SUNXI_CCM_BASE;
	struct sunxi_lcdc_reg * const lcdc =
	       (struct sunxi_lcdc_reg *)SUNXI_LCD0_BASE;
	struct sunxi_lcd_priv *priv = dev_get_priv(dev);
#ifdef CONFIG_VIDEO_SUNXI_MIPI_DSI
	struct udevice *dsi_host;
#endif
	int clk_div, clk_double, ret;

	/*
	 * LCD bus gate and reset.  On NCAT2 (R528/T113-S3) the gate/reset
	 * registers follow the H6-era CCM layout (tcon_lcd_gate_reset at
	 * offset 0xb7c).  On older SoCs the sun6i-era ahb_reset1_cfg /
	 * ahb_gate1 fields are used instead.
	 */
#ifdef CONFIG_SUNXI_GEN_NCAT2
	{
		void *const ccm_base = (void *)SUNXI_CCM_BASE;

		setbits_le32(ccm_base + CCU_NCAT2_LCD_GATE_RESET, CCM_BUS_RESET);
		setbits_le32(ccm_base + CCU_NCAT2_LCD_GATE_RESET, CCM_BUS_GATE);
	}
#else
	/* Reset off */
	setbits_le32(&ccm->ahb_reset1_cfg, 1 << AHB_RESET_OFFSET_LCD0);
	/* Clock on */
	setbits_le32(&ccm->ahb_gate1, 1 << AHB_GATE_OFFSET_LCD0);
#endif

	lcdc_init(lcdc);

#ifdef CONFIG_VIDEO_SUNXI_MIPI_DSI
	if (priv->has_dsi) {
		struct udevice *top;

		/*
		 * TCON-TOP handles the DE2 mixer -> TCON-LCD0 mux and the DSI
		 * clock gate. It must probe before the DSI pipeline starts.
		 */
		ret = uclass_get_device_by_driver(UCLASS_SIMPLE_BUS,
						  DM_DRIVER_GET(sunxi_tcon_top),
						  &top);
		if (ret) {
			printf("sunxi_tcon_top not found: %d\n", ret); // ponytail: debug
			return ret;
		}

		ret = sunxi_lcd_get_dsi_host(priv, &dsi_host);
		if (ret) {
			printf("sunxi_mipi_dsi probe failed: %d\n", ret); // ponytail: debug
			return ret;
		}
	} else
#endif
	{
		sunxi_lcdc_config_pinmux();
	}

#ifdef CONFIG_VIDEO_SUNXI_MIPI_DSI
	if (priv->has_dsi) {
		/*
		 * TCON byte clock for DSI: pixel_clock * bpp / lanes / SUN6I_DSI_TCON_DIV.
		 * SUN6I_DSI_TCON_DIV is 4 (Linux sun6i value), so for 1-lane RGB888
		 * the requested dotclock is pixel_clock * 24 / 4 = pixel_clock * 6.
		 * pixelclock.typ is in Hz; lcdc_pll_set expects kHz.
		 */
		printf("sunxi_lcd: set pll\n");
		lcdc_pll_set(ccm, 0,
			     edid->pixelclock.typ / 1000 * priv->panel_bpp / 1 / SUN6I_DSI_TCON_DIV,
			     &clk_div, &clk_double, false);
	} else
#endif
	{
		printf("sunxi_lcd: set pll non-dsi\n");
		lcdc_pll_set(ccm, 0, edid->pixelclock.typ / 1000,
			     &clk_div, &clk_double, false);
	}
	printf("sunxi_lcd: set tcon mode\n");
	lcdc_tcon0_mode_set(lcdc, edid, clk_div, false,
			    priv->panel_bpp, CONFIG_VIDEO_LCD_DCLK_PHASE);
	if (priv->rb_swap)
		setbits_le32(&lcdc->tcon0_ctrl,
			     SUNXI_LCDC_TCON0_CTRL_RB_SWAP);
	else
		clrbits_le32(&lcdc->tcon0_ctrl,
			     SUNXI_LCDC_TCON0_CTRL_RB_SWAP);

#ifdef CONFIG_VIDEO_SUNXI_MIPI_DSI
	if (priv->has_dsi) {
		u32 block_space, htotal, start_delay;
		u8 lanes = 1;
		u8 tcon_scale = priv->panel_bpp / (SUN6I_DSI_TCON_DIV * lanes);
		void __iomem *const lcdc_base = (void __iomem *)lcdc;

		/* TCON-LCD0 CPU/DSI interface setup. */
		clrbits_le32(&lcdc->tcon0_ctrl, SUN4I_TCON0_CTL_IF_MASK);
		setbits_le32(&lcdc->tcon0_ctrl, SUN4I_TCON0_CTL_IF_8080);
		setbits_le32(lcdc_base + SUN4I_TCON_ECC_FIFO_REG,
			     SUN4I_TCON_ECC_FIFO_EN);
		writel(SUN4I_TCON0_CPU_IF_MODE_DSI |
		       SUN4I_TCON0_CPU_IF_TRI_FIFO_FLUSH |
		       SUN4I_TCON0_CPU_IF_TRI_FIFO_EN |
		       SUN4I_TCON0_CPU_IF_TRI_EN,
		       &lcdc->tcon0_cpu_intf);

		/* CPU trigger/block transfer setup for DSI, matching Linux sun4i_tcon. */
		htotal = edid->hactive.typ + edid->hback_porch.typ +
			 edid->hfront_porch.typ + edid->hsync_len.typ;
		block_space = htotal * tcon_scale - edid->hactive.typ - 40;
		if (block_space > 4095)
			block_space = 4095;
		writel(SUN4I_TCON0_CPU_TRI0_BLOCK_SPACE(block_space) |
		       SUN4I_TCON0_CPU_TRI0_BLOCK_SIZE(edid->hactive.typ),
		       lcdc_base + SUN4I_TCON0_CPU_TRI0_REG);
		writel(SUN4I_TCON0_CPU_TRI1_BLOCK_NUM(edid->vactive.typ),
		       lcdc_base + SUN4I_TCON0_CPU_TRI1_REG);
		start_delay = (edid->vactive.typ + edid->vback_porch.typ +
			       edid->vfront_porch.typ + edid->vsync_len.typ -
			       edid->vactive.typ - 10 - 1);
		start_delay = start_delay * htotal * 149;
		start_delay = start_delay / (edid->pixelclock.typ / 1000) / 8;
		if (start_delay > 65535)
			start_delay = 65535;
		writel(SUN4I_TCON0_CPU_TRI2_START_DELAY(start_delay) |
		       SUN4I_TCON0_CPU_TRI2_TRANS_START_SET(10),
		       lcdc_base + SUN4I_TCON0_CPU_TRI2_REG);
		writel(SUN4I_TCON_SAFE_PERIOD_NUM(3000) |
		       SUN4I_TCON_SAFE_PERIOD_MODE(3),
		       lcdc_base + SUN4I_TCON_SAFE_PERIOD_REG);

		/*
		 * Linux programs the DSI CPU interface with these tristate bits
		 * set. lcdc_tcon0_mode_set() clears them for RGB panels, so put
		 * the DSI-specific value back after switching TCON0 to CPU mode.
		 */
		writel(0xe0000000, &lcdc->tcon0_io_tristate);
	}
#endif

#ifdef CONFIG_VIDEO_SUNXI_MIPI_DSI
	/*
	 * The panel's enable_backlight() puts the DSI host into command mode,
	 * sends the DCS init sequence, and then switches it to HS video mode.
	 * The TCON must be configured before this, and must be enabled after
	 * the panel is in HS video mode.
	 */
	if (priv->has_dsi) {
		if (priv->panel) {
			printf("sunxi_lcd: enable panel\n");
			ret = panel_enable_backlight(priv->panel);
			printf("sunxi_lcd: panel enable ret=%d\n", ret);
		}
	}
#endif

	printf("sunxi_lcd: enable lcdc\n");
	lcdc_enable(lcdc, priv->panel_bpp);

#ifdef CONFIG_VIDEO_SUNXI_MIPI_DSI
	/*
	 * The framebuffer may already contain console output written before the
	 * panel path became active. Force a sync now so the first visible frame
	 * is pushed as soon as the DSI/LCDC pipeline is up.
	 */
	if (priv->has_dsi)
		video_sync_all();
#endif

	return 0;
}

static int sunxi_lcd_read_timing(struct udevice *dev,
				 struct display_timing *timing)
{
	struct sunxi_lcd_priv *priv = dev_get_priv(dev);

	memcpy(timing, &priv->timing, sizeof(struct display_timing));

	return 0;
}

static int sunxi_lcd_read_panel_dt(struct udevice *dev, ofnode panel)
{
	struct sunxi_lcd_priv *priv = dev_get_priv(dev);
	int val;

	if (ofnode_decode_display_timing(panel, 0, &priv->timing)) {
		debug("%s: Failed to decode display timing\n", __func__);
		return -EINVAL;
	}

	val = ofnode_read_s32_default(panel, "bits-per-pixel", -1);
	if (val != -1)
		priv->panel_bpp = val;
	else
		priv->panel_bpp = 24;

	priv->rb_swap = ofnode_read_bool(panel, "rb-swap");
#ifdef CONFIG_VIDEO_SUNXI_MIPI_DSI
	priv->has_dsi = true;
	priv->dsi_panel = panel;
	priv->panel = NULL;
#endif

	return 0;
}

static int sunxi_lcd_probe(struct udevice *dev)
{
	struct udevice *cdev;
	struct sunxi_lcd_priv *priv = dev_get_priv(dev);
	int ret;
	int node, timing_node, val;

	printf("sunxi_lcd probe start for %s\n", dev->name); // ponytail: debug

	priv->panel_bpp = 18;

#ifdef CONFIG_VIDEO_SUNXI_MIPI_DSI
	if (ofnode_device_is_compatible(dev_ofnode(dev),
					"allwinner,sun20i-d1-tcon-lcd")) {
		ofnode panel = ofnode_by_compatible(ofnode_null(),
						    "sitronix,st77922");
		if (ofnode_valid(panel)) {
			printf("found st77922 panel node\n"); // ponytail: debug
			return sunxi_lcd_read_panel_dt(dev, panel);
		}
		printf("st77922 panel node NOT found\n"); // ponytail: debug
	}
#endif

#ifdef CONFIG_VIDEO_BRIDGE
	/* Try to get timings from bridge first */
	ret = uclass_get_device(UCLASS_VIDEO_BRIDGE, 0, &cdev);
	if (!ret) {
		u8 edid[EDID_SIZE];
		int channel_bpp;

		ret = video_bridge_attach(cdev);
		if (ret) {
			debug("video bridge attach failed: %d\n", ret);
			return ret;
		}
		ret = video_bridge_read_edid(cdev, edid, EDID_SIZE);
		if (ret > 0) {
			ret = edid_get_timing(edid, ret,
					      &priv->timing, &channel_bpp);
			priv->panel_bpp = channel_bpp * 3;
			if (!ret)
				return ret;
		}
	}
#endif

	/* Fallback to timings from DT if there's no bridge or
	 * if reading EDID failed
	 */
	ret = uclass_get_device(UCLASS_PANEL, 0, &cdev);
	if (ret) {
		debug("video panel not found: %d\n", ret);
		return ret;
	}

	if (fdtdec_decode_display_timing(gd->fdt_blob, dev_of_offset(cdev),
					 0, &priv->timing)) {
		debug("%s: Failed to decode display timing\n", __func__);
		return -EINVAL;
	}
	timing_node = fdt_subnode_offset(gd->fdt_blob, dev_of_offset(cdev),
					 "display-timings");
	node = fdt_first_subnode(gd->fdt_blob, timing_node);
	val = fdtdec_get_int(gd->fdt_blob, node, "bits-per-pixel", -1);
	if (val != -1)
		priv->panel_bpp = val;

	priv->rb_swap = fdtdec_get_bool(gd->fdt_blob, node, "rb-swap");

	return 0;
}

static const struct dm_display_ops sunxi_lcd_ops = {
	.read_timing = sunxi_lcd_read_timing,
	.enable = sunxi_lcd_enable,
};

static const struct udevice_id sunxi_lcd_ids[] = {
	{ .compatible = "allwinner,sun20i-d1-tcon-lcd" },
	{ }
};

U_BOOT_DRIVER(sunxi_lcd) = {
	.name   = "sunxi_lcd",
	.id     = UCLASS_DISPLAY,
	.of_match = sunxi_lcd_ids,
	.ops    = &sunxi_lcd_ops,
	.probe  = sunxi_lcd_probe,
	.priv_auto	= sizeof(struct sunxi_lcd_priv),
};

/* Only A64 needs the non-DT drvinfo device; R528 has a DT tcon-lcd node. */
#if defined(CONFIG_MACH_SUN50I)
U_BOOT_DRVINFO(sunxi_lcd) = {
	.name = "sunxi_lcd"
};
#endif
