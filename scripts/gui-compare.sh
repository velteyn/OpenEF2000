#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 velteyn
#
# Render a GUI screen and place it beside an original DOSBox-X capture so the
# two can be compared. Both images stay in images/, which is ignored: original
# artwork and screenshots must never be committed (see AGENTS.md section 2).
#
# Usage:
#   scripts/gui-compare.sh --data DIR --screen arcade --reference capture.png
#
# The reference is scaled to the native 640x400 GUI mode; our render is written
# at that size too. The side-by-side file is images/compare-<screen>.png.

set -euo pipefail

data=""
screen=""
reference=""
out=""

usage() {
    cat >&2 <<'EOF'
usage: gui-compare.sh --data DIR --screen SCREEN --reference CAPTURE.png
                      [--out PNG]

Renders the screen to images/ours-SCREEN.png and writes a side-by-side
comparison to images/compare-SCREEN.png (references are scaled to 640x400).
Captures stay untracked; never commit original artwork.
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --data) data=${2:-}; shift 2 ;;
        --screen) screen=${2:-}; shift 2 ;;
        --reference) reference=${2:-}; shift 2 ;;
        --out) out=${2:-}; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [ -z "$data" ] || [ -z "$screen" ] || [ -z "$reference" ]; then
    usage >&2
    exit 2
fi

root=$(cd "$(dirname "$0")/.." && pwd)
binary="$root/build/openef2000"
if [ ! -x "$binary" ]; then
    echo "build first: cmake -S . -B build -DCMAKE_BUILD_TYPE=Release" >&2
    exit 1
fi
if [ ! -f "$reference" ]; then
    echo "no such reference capture: $reference" >&2
    exit 1
fi

mkdir -p "$root/images"
ours="$root/images/ours-$screen.png"
"$binary" --data "$data" --gui "$screen" --gui-render "$ours" >/dev/null
out=${out:-$root/images/compare-$screen.png}

python3 - "$reference" "$ours" "$out" "$screen" <<'PY'
import sys

try:
    from PIL import Image, ImageDraw
except ImportError:
    print("Pillow is not installed; the render is at", sys.argv[2])
    raise SystemExit(0)

reference, ours, out, screen = sys.argv[1:5]
left = Image.open(reference).convert("RGB").resize((640, 400), Image.NEAREST)
right = Image.open(ours).convert("RGB")
if right.size != (640, 400):
    right = right.resize((640, 400), Image.NEAREST)

canvas = Image.new("RGB", (1280, 424), (20, 20, 24))
canvas.paste(left, (0, 24))
canvas.paste(right, (640, 24))
pen = ImageDraw.Draw(canvas)
pen.text((8, 6), "DOSBox-X capture", fill=(232, 232, 236))
pen.text((648, 6), "OpenEF2000 render", fill=(232, 232, 236))
canvas.save(out)
print("wrote", out, "for", screen)
PY

echo "Record factual differences (layout, colours, text, widgets) in"
echo "docs/ENGINE-NOTES.md; images/ is ignored, so neither capture is committed."
