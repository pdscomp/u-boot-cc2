// SPDX-License-Identifier: GPL-2.0+
/*
 * 'efex' command - reboot into the Allwinner FEL (USB boot ROM) mode.
 *
 * Set the FEL flag in the RTC data registers and reset the board.
 * The SPL checks the flag early during the next boot and jumps back
 * into the boot ROM FEL routine (see arch/arm/mach-sunxi/board.c).
 *
 * Ported from the Allwinner BSP U-Boot:
 * (C) Copyright 2018-2020
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * wangwei <wangwei@allwinnertech.com>
 */

#include <command.h>
#include <asm/arch/efex.h>

static int do_efex(struct cmd_tbl *cmdtp, int flag, int argc,
		   char *const argv[])
{
	printf("## jump to efex ...\n");

	sunxi_set_fel_flag();

	do_reset(NULL, 0, 0, NULL);

	return 0;
}

U_BOOT_CMD(
	efex,	1,	0,	do_efex,
	"reboot into FEL (USB boot ROM) mode",
	""
);
