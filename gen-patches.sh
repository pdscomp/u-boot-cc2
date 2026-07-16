#!/usr/bin/env bash
# Generate Yocto U-Boot patches from the current cc2-dsi branch.
#
# This script intentionally regenerates only the patch whose content may
# change. Patches that already exist in the cosmos repo and do not need to be
# modified are left as-is, so their From:/Date:/Subject:/Signed-off-by:
# headers stay stable.

set -euo pipefail

UBOOT_DIR="${UBOOT_DIR:-/home/paul/sandbox/u-boot-cc2}"
PATCH_DIR="${PATCH_DIR:-/home/paul/carbon/cosmos/meta-opencentauri/recipes-bsp/u-boot/files}"
BASE="${BASE:-master}"
BRANCH="${BRANCH:-$(git rev-parse --abbrev-ref HEAD)}"

cd "$UBOOT_DIR"

merge_base=$(git merge-base "$BRANCH" "$BASE")

# Shared CC1-safe patch (0001) already exists in the cosmos repo and is copied
# from there, not regenerated, so Timo V's original authorship is preserved.
if [[ -f "$PATCH_DIR/0001-sunxi-r528-add-display-support-with-RB-channel-swap.patch" ]]; then
    echo "leave unchanged: $PATCH_DIR/0001-sunxi-r528-add-display-support-with-RB-channel-swap.patch"
else
    echo "MISSING: $PATCH_DIR/0001-sunxi-r528-add-display-support-with-RB-channel-swap.patch"
    exit 1
fi

# CC2-only DSI patch: everything from the shared baseline to the CC2 tip.
# This includes the MIPI-DSI host, panel driver, TCON-TOP, clock gates, and
# the #ifdef guards in sunxi_lcd.c.
git format-patch -1 --stdout "$merge_base..HEAD" -- \
    arch/arm/include/asm/arch-sunxi/lcdc.h \
    drivers/clk/sunxi/clk_d1.c \
    drivers/video/Kconfig \
    drivers/video/Makefile \
    drivers/video/panel-sitronix-st77922.c \
    drivers/video/sunxi/Makefile \
    drivers/video/sunxi/lcdc.c \
    drivers/video/sunxi/sunxi_de2.c \
    drivers/video/sunxi/sunxi_lcd.c \
    drivers/video/sunxi/sunxi_mipi_dsi.c \
    drivers/video/sunxi/sunxi_mipi_dsi.h \
    drivers/video/sunxi/sunxi_tcon.h \
    drivers/video/sunxi/sunxi_tcon_top.c \
    > "$PATCH_DIR/0002-video-add-st77922-panel-and-allwinner-dsi-host.patch"

# The old 0003-only-DTS patch is no longer referenced by u-boot_%.bbappend; the
# CC2 DTS changes are now folded into 0002. Leave the file in place but do not
# regenerate it.

# Print a summary of the files we just produced.
for f in \
    "$PATCH_DIR/0001-sunxi-r528-add-display-support-with-RB-channel-swap.patch" \
    "$PATCH_DIR/0002-video-add-st77922-panel-and-allwinner-dsi-host.patch"
do
    if [[ -f "$f" ]]; then
        echo "ok: $f"
        md5sum "$f" | awk '{print $1}'
    else
        echo "MISSING: $f"
    fi
done
