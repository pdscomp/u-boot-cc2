/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Allwinner TCON-TOP register definitions for T113/R528 (NCAT2).
 *
 * Linux reference: drivers/gpu/drm/sun4i/sun8i_tcon_top.h
 */

#ifndef _SUNXI_TCON_H
#define _SUNXI_TCON_H

#define TCON_TOP_PORT_SEL_REG		0x1c
#define TCON_TOP_PORT_DE0_MSK		0x3
#define TCON_TOP_PORT_DE0_SHIFT		0
#define TCON_TOP_PORT_DE1_MSK		0x3
#define TCON_TOP_PORT_DE1_SHIFT		4

#define TCON_TOP_GATE_SRC_REG		0x20
#define TCON_TOP_TCON_DSI_GATE		BIT(16)
#define TCON_TOP_TCON_TV0_GATE		BIT(20)
#define TCON_TOP_TCON_TV1_GATE		BIT(24)
#define TCON_TOP_HDMI_SRC_MSK		(0x3 << 28)

/* TCON-LCD0 interface selection for DSI */
#define SUN4I_TCON0_CPU_IF_REG		0x60
#define SUN4I_TCON0_CPU_IF_MODE_DSI	(1 << 28)
#define SUN4I_TCON0_CPU_IF_TRI_FIFO_FLUSH (1 << 16)
#define SUN4I_TCON0_CPU_IF_TRI_FIFO_EN	(1 << 2)
#define SUN4I_TCON0_CPU_IF_TRI_EN	(1 << 0)

#define SUN4I_TCON0_CTL_IF_MASK		GENMASK(25, 24)
#define SUN4I_TCON0_CTL_IF_8080		(1 << 24)

#define SUN4I_TCON_ECC_FIFO_REG		0xf8
#define SUN4I_TCON_ECC_FIFO_EN		BIT(3)

#define SUN4I_TCON0_CPU_TRI0_REG	0x160
#define SUN4I_TCON0_CPU_TRI0_BLOCK_SPACE(space)	((((space) - 1) & 0xfff) << 16)
#define SUN4I_TCON0_CPU_TRI0_BLOCK_SIZE(size)	(((size) - 1) & 0xfff)

#define SUN4I_TCON0_CPU_TRI1_REG	0x164
#define SUN4I_TCON0_CPU_TRI1_BLOCK_NUM(num)	(((num) - 1) & 0xffff)

#define SUN4I_TCON0_CPU_TRI2_REG	0x168
#define SUN4I_TCON0_CPU_TRI2_START_DELAY(delay)	(((delay) & 0xffff) << 16)
#define SUN4I_TCON0_CPU_TRI2_TRANS_START_SET(set)	((set) & 0xfff)

#define SUN4I_TCON_SAFE_PERIOD_REG	0x1f0
#define SUN4I_TCON_SAFE_PERIOD_NUM(num)	(((num) & 0xfff) << 16)
#define SUN4I_TCON_SAFE_PERIOD_MODE(mode)	((mode) & 0x3)

#define SUN6I_DSI_TCON_DIV		4	/* matches Linux sun6i; used by TCON and DSI host */

#endif /* _SUNXI_TCON_H_ */
