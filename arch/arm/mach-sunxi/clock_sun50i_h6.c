// SPDX-License-Identifier: GPL-2.0+

#include <asm/io.h>
#include <asm/arch/cpu.h>
#include <asm/arch/clock.h>
#include <asm/arch/prcm.h>
#include <linux/delay.h>

#ifndef SUNXI_CPU_PLL_CFG_BASE
#define SUNXI_CPU_PLL_CFG_BASE 0
#endif

#ifdef CONFIG_XPL_BUILD
void clock_init_safe(void)
{
	void *const ccm = (void *)SUNXI_CCM_BASE;
	void *const prcm = (void *)SUNXI_PRCM_BASE;

	if (IS_ENABLED(CONFIG_MACH_SUN50I_H616))
		setbits_le32(prcm + CCU_PRCM_SYS_PWROFF_GATING, 0x10);
	if (IS_ENABLED(CONFIG_MACH_SUN55I_A523))
		setbits_le32(prcm + CCU_PRCM_SYS_PWROFF_GATING, 0x200);
	udelay(1);

	if (IS_ENABLED(CONFIG_MACH_SUN50I_H616) ||
	    IS_ENABLED(CONFIG_MACH_SUN55I_A523))
		setbits_le32(prcm + CCU_PRCM_RES_CAL_CTRL, 2);
	udelay(1);

	if (IS_ENABLED(CONFIG_MACH_SUN50I_H616) ||
	    IS_ENABLED(CONFIG_MACH_SUN50I_H6) ||
	    IS_ENABLED(CONFIG_MACH_SUN55I_A523)) {
		clrbits_le32(prcm + CCU_PRCM_RES_CAL_CTRL, 1);
		udelay(1);
		setbits_le32(prcm + CCU_PRCM_RES_CAL_CTRL, 1);
	}

	if (IS_ENABLED(CONFIG_MACH_SUN50I_H6)) {
		/* set key field for ldo enable */
		setbits_le32(prcm + CCU_PRCM_PLL_LDO_CFG, 0xA7000000);
		/* set PLL VDD LDO output to 1.14 V */
		setbits_le32(prcm + CCU_PRCM_PLL_LDO_CFG, 0x60000);
	}

	clock_set_pll1(408000000);

	writel(CCM_PLL6_DEFAULT, ccm + CCU_H6_PLL6_CFG);
	while (!(readl(ccm + CCU_H6_PLL6_CFG) & CCM_PLL_LOCK))
		;

	if (!IS_ENABLED(CONFIG_MACH_SUN55I_A523))
		clrsetbits_le32(ccm + CCU_H6_CPU_AXI_CFG,
				CCM_CPU_AXI_APB_MASK | CCM_CPU_AXI_AXI_MASK,
				CCM_CPU_AXI_DEFAULT_FACTORS);

	writel(CCM_PSI_AHB1_AHB2_DEFAULT, ccm + CCU_H6_PSI_AHB1_AHB2_CFG);
#ifdef CCM_AHB3_DEFAULT
	writel(CCM_AHB3_DEFAULT, ccm + CCU_H6_AHB3_CFG);
#endif
	writel(CCM_APB1_DEFAULT, ccm + CCU_H6_APB1_CFG);

	/*
	 * The mux and factor are set, but the clock will be enabled in
	 * DRAM initialization code.
	 */
	if (IS_ENABLED(CONFIG_MACH_SUN55I_A523)) {
		writel(MBUS_RESET, ccm + CCU_H6_MBUS_CFG);
		udelay(1);
		writel(MBUS_UPDATE | MBUS_CLK_SRC_OSCM24 | MBUS_CLK_M(4),
		       ccm + CCU_H6_MBUS_CFG);
	} else {
		writel(MBUS_CLK_SRC_PLL6X2 | MBUS_CLK_M(3),
		       ccm + CCU_H6_MBUS_CFG);
	}
}

void clock_init_uart(void)
{
	void *const ccm = (void *)SUNXI_CCM_BASE;

	/* uart clock source is apb2 */
	writel(APB2_CLK_SRC_OSC24M|
	       APB2_CLK_RATE_N_1|
	       APB2_CLK_RATE_M(1),
	       ccm + CCU_H6_APB2_CFG);

	/* open the clock for uart */
	setbits_le32(ccm + CCU_H6_UART_GATE_RESET,
		     1 << (CONFIG_CONS_INDEX - 1));

	/* deassert uart reset */
	setbits_le32(ccm + CCU_H6_UART_GATE_RESET,
		     1 << (RESET_SHIFT + CONFIG_CONS_INDEX - 1));
}

static bool has_pll_output_gate(void)
{
	return (IS_ENABLED(CONFIG_SUNXI_GEN_NCAT2) ||
		IS_ENABLED(CONFIG_MACH_SUN50I_H616) ||
		IS_ENABLED(CONFIG_MACH_SUN50I_A133));
}

/* A shared routine to program the CPU PLLs for H6, H616, T113, A523 */
static void clock_set_pll(u32 *reg, unsigned int n)
{
	u32 val = readl(reg);

	/* clear the lock enable bit */
	val &= ~CCM_PLL_LOCK_EN;
	writel(val, reg);

	/* gate the output on the newer SoCs */
	if (has_pll_output_gate()) {
		val &= ~CCM_PLL_OUT_EN;
		writel(val, reg);
	}

	val &= ~(CCM_PLL1_CTRL_N_MASK | GENMASK(3, 0) | GENMASK(21, 16));
	val |= CCM_PLL1_CTRL_N(n);
	writel(val, reg);			/* program parameter */

	val |= CCM_PLL_CTRL_EN;
	if (IS_ENABLED(CONFIG_SUNXI_GEN_NCAT2))
		val |= CCM_PLL_LDO_EN;
	writel(val, reg);			/* enable PLL */

	val |= CCM_PLL_LOCK_EN;
	if (IS_ENABLED(CONFIG_MACH_SUN55I_A523))
		val |= CCM_PLL1_UPDATE;
	writel(val, reg);			/* start locking process */

	while (!(readl(reg) & CCM_PLL_LOCK)) {	/* wait for lock bit */
	}
	udelay(20);				/* wait as per manual */

	/* un-gate the output on the newer SoCs */
	if (has_pll_output_gate()) {
		val |= CCM_PLL_OUT_EN;
		writel(val, reg);
	}
}

/* Program the PLLs for both clusters plus the DSU. */
static void clock_a523_set_cpu_plls(unsigned int n_factor)
{
	void *const cpc = (void *)SUNXI_CPU_PLL_CFG_BASE;
	u32 val;

	val = CPU_CLK_SRC_HOSC | CPU_CLK_CTRL_P(0) |
	       CPU_CLK_APB_DIV(4) | CPU_CLK_PERI_DIV(2) |
	       CPU_CLK_AXI_DIV(2);

	/* Switch CPU clock source to 24MHz HOSC while changing the PLL */
	writel(val, cpc + CPC_CPUA_CLK_REG);
	writel(val, cpc + CPC_CPUB_CLK_REG);
	udelay(20);
	writel(CPU_CLK_SRC_HOSC | CPU_CLK_CTRL_P(0),
	       cpc + CPC_DSU_CLK_REG);
	udelay(20);

	clock_set_pll(cpc + CPC_CPUA_PLL_CTRL, n_factor);
	clock_set_pll(cpc + CPC_CPUB_PLL_CTRL, n_factor);
	clock_set_pll(cpc + CPC_DSU_PLL_CTRL, n_factor);

	/* Switch CPU clock source to the CPU PLL */
	clrsetbits_le32(cpc + CPC_CPUA_CLK_REG, CPU_CLK_SRC_HOSC,
			CPU_CLK_SRC_CPUPLL);
	clrsetbits_le32(cpc + CPC_CPUB_CLK_REG, CPU_CLK_SRC_HOSC,
			CPU_CLK_SRC_CPUPLL);
	clrsetbits_le32(cpc + CPC_DSU_CLK_REG, CPU_CLK_SRC_HOSC,
			CPU_CLK_SRC_CPUPLL);
}

static void clock_h6_set_cpu_pll(unsigned int n_factor)
{
	void *const ccm = (void *)SUNXI_CCM_BASE;
	u32 val;

	/* Switch CPU clock source to 24MHz HOSC while changing the PLL */
	val = readl(ccm + CCU_H6_CPU_AXI_CFG);
	val &= ~CCM_CPU_AXI_MUX_MASK;
	val |= CCM_CPU_AXI_MUX_OSC24M;
	writel(val, ccm + CCU_H6_CPU_AXI_CFG);

	clock_set_pll(ccm + CCU_H6_PLL1_CFG, n_factor);

	/* Switch CPU clock source to the CPU PLL */
	val = readl(ccm + CCU_H6_CPU_AXI_CFG);
	val &= ~CCM_CPU_AXI_MUX_MASK;
	val |= CCM_CPU_AXI_MUX_PLL_CPUX;
	writel(val, ccm + CCU_H6_CPU_AXI_CFG);
}

void clock_set_pll1(unsigned int clk)
{
	/* Do not support clocks < 288MHz as they need factor P */
	if (clk < 288000000)
		clk = 288000000;

	clk /= 24000000;

	if (IS_ENABLED(CONFIG_MACH_SUN55I_A523))
		clock_a523_set_cpu_plls(clk);
	else
		clock_h6_set_cpu_pll(clk);
}

int clock_twi_onoff(int port, int state)
{
	void *const ccm = (void *)SUNXI_CCM_BASE;
	void *const prcm = (void *)SUNXI_PRCM_BASE;
	u32 value, *ptr;
	int shift;

	value = BIT(GATE_SHIFT) | BIT (RESET_SHIFT);

	if (port == 5) {
		shift = 0;
		ptr = prcm + CCU_PRCM_I2C_GATE_RESET;
	} else {
		shift = port;
		ptr = ccm + CCU_H6_I2C_GATE_RESET;
	}

	/* set the apb clock gate and reset for twi */
	if (state)
		setbits_le32(ptr, value << shift);
	else
		clrbits_le32(ptr, value << shift);

	return 0;
}
#endif /* CONFIG_XPL_BUILD */

/* PLL_PERIPH0 clock, used by the MMC driver */
unsigned int clock_get_pll6(void)
{
	void *const ccm = (void *)SUNXI_CCM_BASE;
	uint32_t rval = readl(ccm + CCU_H6_PLL6_CFG);
	int n = ((rval & CCM_PLL6_CTRL_N_MASK) >> CCM_PLL6_CTRL_N_SHIFT) + 1;
	int div2 = ((rval & CCM_PLL6_CTRL_DIV2_MASK) >>
		    CCM_PLL6_CTRL_DIV2_SHIFT) + 1;
	int div1, m;

	if (IS_ENABLED(CONFIG_SUNXI_GEN_NCAT2)) {
		div1 = ((rval & CCM_PLL6_CTRL_P0_MASK) >>
			CCM_PLL6_CTRL_P0_SHIFT) + 1;
	} else {
		div1 = ((rval & CCM_PLL6_CTRL_DIV1_MASK) >>
			CCM_PLL6_CTRL_DIV1_SHIFT) + 1;
	}

	/*
	 * The factors encoded in the register describe the doubled clock
	 * frequency, expect for the H6, where it's the quadrupled frequency.
	 * Compensate for that here.
	 */
	if (IS_ENABLED(CONFIG_MACH_SUN50I_H6))
		m = 4;
	else
		m = 2;

	return 24000000U * n / m / div1 / div2;
}

#ifdef CONFIG_VIDEO_DE2
/*
 * Display clock functions for R528/T113-S3 (SUNXI_GEN_NCAT2).
 *
 * PLL3 (VIDEO0, CCM offset 0x040) drives the display pipeline.
 * The DE clock is sourced from PLL_VIDEO0_4X via the DE_CLK_REG mux.
 * The TCON_LCD0 clock is sourced from PLL_VIDEO0 (1x) or PLL_VIDEO0_4X.
 *
 * PLL layout (same for all R528 PLLs):
 *   EN(31), LDO_EN(30), LOCK_EN(29), LOCK(28), OUT_EN(27)
 *   freq_4x = 24MHz * N / (D1+1), freq_1x = freq_4x / 4
 */

#define PLL_LOCK_TIMEOUT_US 1000

void clock_set_pll3(unsigned int clk)
{
	void *const ccm = (void *)SUNXI_CCM_BASE;
	int i;

	if (clk == 0) {
		clrbits_le32(ccm + CCU_NCAT2_PLL_VIDEO0_CFG, CCM_PLL_VIDEO0_CTRL_EN);
		return;
	}

	/*
	 * PLL_VIDEO0 on R528/T113 has: freq_4x = 24MHz * N / (D1+1)
	 * where N is in bits[15:8] (stored as N-1) and D1 is in bit[1].
	 *
	 * The 1x output (pll-video0) is freq_4x / 4.
	 *
	 * For simplicity: if the requested clock is divisible by 24MHz,
	 * use D1=0 (divide-by-1). Otherwise try D1=1 (divide-by-2) so
	 * that the effective multiplier can be a half-integer of 24MHz.
	 *
	 * clk is the desired 1x output frequency in Hz.
	 */
	int n;
	int d1 = 0;

	/* Try D1=0 first: freq_1x = 24*N/4, so N = freq_1x*4/24 */
	n = (int)((unsigned long)clk * 4 / 24000000);
	if (n < 1)
		n = 1;
	if (n > 255)
		n = 255;

	/* Check if D1=1 gives a better match: freq_1x = 24*N/2/4 = 24*N/8 */
	{
		int n2 = (int)((unsigned long)clk * 8 / 24000000);
		unsigned int err0 = abs((int)(24000000UL * n / 4 - clk));
		unsigned int err1 = (n2 >= 1 && n2 <= 255) ?
			abs((int)(24000000UL * n2 / 8 - clk)) : 0xFFFFFFFF;
		if (err1 < err0) {
			n = n2;
			d1 = 1;
		}
	}

	/* Enable PLL with lock detection, but without output yet */
	writel(CCM_PLL_VIDEO0_CTRL_EN | CCM_PLL_VIDEO0_LDO_EN |
	       CCM_PLL_VIDEO0_LOCK_EN |
	       CCM_PLL_VIDEO0_CTRL_N(n) | CCM_PLL_VIDEO0_CTRL_D1(d1),
	       ccm + CCU_NCAT2_PLL_VIDEO0_CFG);

	/* Wait for PLL to lock */
	for (i = 0; i < PLL_LOCK_TIMEOUT_US; i++) {
		if (readl(ccm + CCU_NCAT2_PLL_VIDEO0_CFG) & CCM_PLL_VIDEO0_LOCK)
			break;
		udelay(1);
	}

	/* Enable PLL output */
	setbits_le32(ccm + CCU_NCAT2_PLL_VIDEO0_CFG, CCM_PLL_VIDEO0_OUT_EN);
}

unsigned int clock_get_pll3(void)
{
	void *const ccm = (void *)SUNXI_CCM_BASE;
	uint32_t rval = readl(ccm + CCU_NCAT2_PLL_VIDEO0_CFG);
	int n = ((rval & CCM_PLL_VIDEO0_CTRL_N_MASK) >> CCM_PLL_VIDEO0_CTRL_N_SHIFT) + 1;
	int d1 = ((rval >> CCM_PLL_VIDEO0_CTRL_D1_SHIFT) & 1) + 1;

	/* Returns the 1x output: 24MHz * N / D1 / 4 */
	return 24000000U * n / d1 / 4;
}
#endif /* CONFIG_VIDEO_DE2 */
