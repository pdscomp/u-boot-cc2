/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Allwinner FEL (USB boot ROM) flag handling.
 *
 * The stock Allwinner boot0 checks a magic value in an RTC general
 * purpose data register early during boot and jumps back into the
 * boot ROM FEL routine when it is set. U-Boot proper (or Linux) sets
 * the flag and resets the board to request this. The boot ROM itself
 * does not look at the flag, so the SPL has to perform the check.
 *
 * Register index and magic value taken from the Allwinner BSP U-Boot
 * (arch/arm/mach-sunxi/rtc.c) and boot0 (spl-pub/nboot/main/boot0_main.c).
 */

#ifndef _SUNXI_EFEX_H
#define _SUNXI_EFEX_H

#include <asm/arch/cpu.h>
#include <asm/barriers.h>
#include <asm/io.h>

#define SUNXI_RTC_DATA_BASE	(SUNXI_RTC_BASE + 0x100)
#define SUNXI_RTC_FEL_REG	(SUNXI_RTC_DATA_BASE + 2 * 4)
#define SUNXI_EFEX_FLAG		0x5AA5A55A

/* FEL entry point in the boot ROM (H6 and newer generations) */
#define SUNXI_FEL_ENTRY		0x20

static inline void sunxi_set_fel_flag(void)
{
	do {
		writel(SUNXI_EFEX_FLAG, SUNXI_RTC_FEL_REG);
		DSB;
		ISB;
	} while (readl(SUNXI_RTC_FEL_REG) != SUNXI_EFEX_FLAG);
}

static inline void sunxi_clear_fel_flag(void)
{
	do {
		writel(0, SUNXI_RTC_FEL_REG);
		DSB;
		ISB;
	} while (readl(SUNXI_RTC_FEL_REG) != 0);
}

static inline bool sunxi_probe_fel_flag(void)
{
	return readl(SUNXI_RTC_FEL_REG) == SUNXI_EFEX_FLAG;
}

#endif /* _SUNXI_EFEX_H */
