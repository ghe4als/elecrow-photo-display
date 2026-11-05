#!/bin/zsh
set -e

# Ensure we are running from the repo root
cd "$(dirname "$0")/.."

# Create the output directory if it doesn't exist
mkdir -p src/crowpanel/fonts

# Generate the clock font
# Range 0x30-0x3A: digits 0-9 and colon (:)
npx --yes lv_font_conv \
  --font .pio/libdeps/crowpanel-5/lvgl/scripts/built_in_font/Montserrat-Medium.ttf \
  --range 0x30-0x3A \
  --size 80 \
  --bpp 4 \
  --format lvgl \
  --no-compress \
  --lv-font-name lv_font_clock_80 \
  --lv-include lvgl.h \
  -o src/crowpanel/fonts/lv_font_clock_80.c
