#!/bin/bash
# Interactive GUI test that verifies each dialog really opens.
PROJ="${PROJ:-/home/tuananh/fs_explorer}"
cd "$PROJ" || exit 1
exec > /tmp/gui_check_out.txt 2>&1

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

if ! kill -0 $GUI_PID 2>/dev/null; then
    echo "### GUI EXITED EARLY"; cat /tmp/gui.out; exit 1
fi
echo "### GUI running"

# List every named window on the root.
list_windows() {
    xwininfo -root -tree 2>/dev/null | grep -o '"[^"]*"' | sort -u | tr '\n' ' '
    echo
}

click_btn() {
    # $1 = x, $2 = y
    xdotool mousemove "$1" "$2" click 1
    sleep 3
}

echo ""
echo "=== 0. initial windows ==="
list_windows

echo ""
echo "=== 1. select a row in the file list (metadata panel) ==="
click_btn 500 178
import -window root /tmp/gui_c1_metadata.png
list_windows
echo "(metadata panel screenshot saved)"

echo ""
echo "=== 2. Analyze button ==="
click_btn 480 30
list_windows
import -window root /tmp/gui_c2_analyze.png
xdotool key Escape; sleep 1

echo ""
echo "=== 3. Disk button ==="
click_btn 545 30
list_windows
import -window root /tmp/gui_c3_disk.png
xdotool key Escape; sleep 1

echo ""
echo "=== 4. Find button ==="
click_btn 600 30
list_windows
import -window root /tmp/gui_c4_find.png
xdotool key Escape; sleep 1

echo ""
echo "=== 5. About button ==="
click_btn 900 30
list_windows
import -window root /tmp/gui_c5_about.png
xdotool key Escape; sleep 1

echo ""
echo "=== 6. expand the tree ==="
click_btn 40 150
import -window root /tmp/gui_c6_tree.png
list_windows

echo ""
echo "=== 7. New folder button (opens an input dialog) ==="
click_btn 750 30
list_windows
import -window root /tmp/gui_c7_newfolder.png
xdotool key Escape; sleep 1

echo ""
echo "=== 8. Delete button (no selection -> error dialog) ==="
click_btn 830 30
list_windows
import -window root /tmp/gui_c8_delete.png
xdotool key Escape; sleep 1

echo ""
echo "=== final stability check ==="
sleep 3
if kill -0 $GUI_PID 2>/dev/null; then
    echo "### GUI STILL RUNNING - STABLE"
else
    wait $GUI_PID 2>/dev/null
    echo "### GUI CRASHED, exit=$?"
fi

echo ""
echo "=== GUI stderr ==="
cat /tmp/gui.out 2>/dev/null
echo "--- (end stderr) ---"
kill $GUI_PID 2>/dev/null
wait $GUI_PID 2>/dev/null
echo "### DONE"
