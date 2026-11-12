#!/bin/bash
# Interactive GUI test: drive the real widgets with xdotool.
PROJ="${PROJ:-/home/tuananh/fs_explorer}"
cd "$PROJ" || exit 1
exec > /tmp/gui_interact_out.txt 2>&1

export DISPLAY=:99
export GSETTINGS_BACKEND=memory

pkill -f "Xvfb :99" 2>/dev/null
pkill -f fs_explorer 2>/dev/null
Xvfb :99 -screen 0 1400x900x24 >/tmp/xvfb.log 2>&1 &
sleep 3

# Prepare a deterministic sandbox.
rm -rf /tmp/fse_gui
mkdir -p /tmp/fse_gui/docs /tmp/fse_gui/images
echo "hello world" > /tmp/fse_gui/readme.txt
echo "abcdefghij"  > /tmp/fse_gui/docs/manual.pdf
dd if=/dev/zero of=/tmp/fse_gui/images/big.bin bs=1K count=64 2>/dev/null
ln -s /tmp/fse_gui/readme.txt /tmp/fse_gui/link.txt

if ! command -v xdotool >/dev/null 2>&1; then
    echo "### XDOTOOL NOT AVAILABLE"
    exit 3
fi
echo "### xdotool available"

./build/fs_explorer --path /tmp/fse_gui >/tmp/gui.out 2>&1 &
GUI_PID=$!
sleep 6

if ! kill -0 $GUI_PID 2>/dev/null; then
    echo "### GUI EXITED EARLY"
    cat /tmp/gui.out
    exit 1
fi
echo "### GUI running (pid $GUI_PID)"

WIN=$(xdotool search --name "File System Explorer" | head -1)
echo "### window id: $WIN"
xdotool windowactivate "$WIN" 2>/dev/null
sleep 1

# ---- 1. Click a file row in the list and check the metadata panel -------
# The file list starts around x=430, y=150. Row 2 (readme.txt) is around y=175.
xdotool mousemove 500 178 click 1
sleep 2
echo "### clicked row 2"
import -window root /tmp/gui_step1_metadata.png
echo "### screenshot 1 saved"

# ---- 2. Click the "Analyze" toolbar button ------------------------------
xdotool mousemove 480 30 click 1
sleep 3
echo "### clicked Analyze"
import -window root /tmp/gui_step2_analyze.png
echo "### screenshot 2 saved"
# Close the dialog if one is open (Escape).
xdotool key Escape
sleep 1

# ---- 3. Click the "Disk" toolbar button ---------------------------------
xdotool mousemove 545 30 click 1
sleep 3
echo "### clicked Disk"
import -window root /tmp/gui_step3_disk.png
echo "### screenshot 3 saved"
xdotool key Escape
sleep 1

# ---- 4. Click the "Find" toolbar button ---------------------------------
xdotool mousemove 600 30 click 1
sleep 3
echo "### clicked Find"
import -window root /tmp/gui_step4_find.png
echo "### screenshot 4 saved"
xdotool key Escape
sleep 1

# ---- 5. Expand the tree node on the left --------------------------------
xdotool mousemove 40 150 click 1
sleep 2
echo "### clicked tree"
import -window root /tmp/gui_step5_tree.png
echo "### screenshot 5 saved"

# ---- 6. Type a path into the path entry and press Go --------------------
sleep 1
echo "### final window list"
xwininfo -root -tree 2>&1 | head -20

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
