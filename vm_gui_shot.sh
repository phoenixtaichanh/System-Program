#!/bin/bash
PROJ="${PROJ:-/home/tuananh/fs_explorer}"
cd "$PROJ" || exit 1
export DISPLAY=:99
export GSETTINGS_BACKEND=memory

pkill -f "Xvfb :99" 2>/dev/null
pkill -f fs_explorer 2>/dev/null
sleep 1
Xvfb :99 -screen 0 1400x900x24 >/tmp/xvfb.log 2>&1 &
sleep 3

rm -rf /tmp/fse_gui
mkdir -p /tmp/fse_gui/docs /tmp/fse_gui/images
echo "hello world" > /tmp/fse_gui/readme.txt
echo "abcdefghij"  > /tmp/fse_gui/docs/manual.pdf
dd if=/dev/zero of=/tmp/fse_gui/images/big.bin bs=1K count=64 2>/dev/null
ln -s /tmp/fse_gui/readme.txt /tmp/fse_gui/link.txt

./build/fs_explorer --path /tmp/fse_gui >/tmp/gui.out 2>&1 &
GUI_PID=$!
sleep 6

# Select the "link.txt" row so the symlink metadata is visible.
xdotool mousemove 500 178 click 1
sleep 2

# Capture the whole screen first.
import -window root /tmp/gui_full.png

# Crop the interesting half of the window (list + metadata panel) and
# convert it to a colour PNG.
convert /tmp/gui_full.png -crop 1150x750+0+0 +repage -colorspace sRGB /tmp/gui_crop.png 2>/dev/null || \
  import -window root /tmp/gui_crop.png

# Also crop just the metadata panel (bottom of the window).
convert /tmp/gui_crop.png -crop 1150x340+0+410 +repage /tmp/gui_meta.png 2>/dev/null

ls -la /tmp/gui_full.png /tmp/gui_crop.png /tmp/gui_meta.png 2>&1
kill $GUI_PID 2>/dev/null
wait $GUI_PID 2>/dev/null
echo "### DONE"
