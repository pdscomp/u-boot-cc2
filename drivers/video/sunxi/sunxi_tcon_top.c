/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Allwinner TCON-TOP driver for T113/R528 (NCAT2).
 *
 * Sets the DE2 mixer -> TCON-LCD mux and programs the NCAT2 display gate.
 */

#include <clk.h>
#include <dm.h>
#include <dm/device_compat.h>
#include <log.h>
#include <reset.h>
#include <asm/arch/clock.h>
#include <asm/io.h>

#include "sunxi_tcon.h"

struct sunxi_tcon_top_priv {
	void __iomem *regs;
};

static int sunxi_tcon_top_probe(struct udevice *dev)
{
	struct sunxi_tcon_top_priv *priv = dev_get_priv(dev);
	fdt_addr_t addr;
	u32 val;

	printf("tcon-top probe start\n"); // ponytail: debug

	addr = dev_read_addr(dev);
	if (addr == FDT_ADDR_T_NONE) {
		dev_err(dev, "tcon-top no addr\n");
		return -EINVAL;
	}
	priv->regs = (void __iomem *)addr;

#ifdef CONFIG_SUNXI_GEN_NCAT2
	/*
	 * NCAT2 display clocks are programmed directly in the display bring-up
	 * path. Keep TCON-TOP on the same path here instead of relying on a
	 * CCU gate/reset entry that U-Boot does not model.
	 */
	setbits_le32((void __iomem *)SUNXI_CCM_BASE + CCU_NCAT2_DPSS_GATE_RESET,
		     CCM_BUS_RESET);
	setbits_le32((void __iomem *)SUNXI_CCM_BASE + CCU_NCAT2_DPSS_GATE_RESET,
		     CCM_BUS_GATE);
#else
	{
		struct clk clk;
		struct reset_ctl rst;
		int ret;

		ret = clk_get_by_name(dev, "bus", &clk);
		if (ret) {
			dev_err(dev, "failed to get bus clock: %d\n", ret);
			return ret;
		}
		ret = reset_get_by_index(dev, 0, &rst);
		if (ret) {
			printf("tcon-top reset idx0 not found: %d, continuing\n", ret); // ponytail: debug
		} else {
			ret = reset_deassert(&rst);
			if (ret)
				printf("tcon-top reset deassert failed: %d, continuing\n", ret); // ponytail: debug
		}

		ret = clk_enable(&clk);
		if (ret) {
			dev_err(dev, "tcon-top clk enable failed: %d\n", ret);
			return ret;
		}
	}
#endif

	/* Route mixer0 -> TCON-LCD0; clear any stale bits first. */
	val = 0;
	val |= 0 << TCON_TOP_PORT_DE0_SHIFT;
	writel(val, priv->regs + TCON_TOP_PORT_SEL_REG);

	/* Open DSI clock gate; clear other gate/source bits to match Linux. */
	writel(TCON_TOP_TCON_DSI_GATE, priv->regs + TCON_TOP_GATE_SRC_REG);

	printf("tcon-top probe OK\n"); // ponytail: debug
	return 0;
}

static const struct udevice_id sunxi_tcon_top_ids[] = {
	{ .compatible = "allwinner,sun20i-d1-tcon-top" },
	{ }
};

U_BOOT_DRIVER(sunxi_tcon_top) = {
	.name		= "sunxi_tcon_top",
	.id		= UCLASS_SIMPLE_BUS,
	.of_match	= sunxi_tcon_top_ids,
	.probe		= sunxi_tcon_top_probe,
	.priv_auto	= sizeof(struct sunxi_tcon_top_priv),
};
