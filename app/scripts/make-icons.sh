#!/bin/sh
#
# RPiLEDConfig - app
#
# Copyright (C) 2026 T. Cully
# Licensed under the Apache License, Version 2.0 - see LICENSE
#
# Makes the PNGs in assets/ from assets/icon.svg: the launcher icon (on black), the adaptive
# icon's foreground (no background, inside the central safe zone), the splash image and the
# home and title-bar icons (on the app's background, #0d110e). Needs rsvg-convert (MacPorts
# librsvg). Run from app/ after changing icon.svg, then `npx expo prebuild --platform android`.
set -e
cd "$(dirname "$0")/../assets"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

python3 - "$tmp" <<'PY'
import sys
tmp = sys.argv[1]
s = open("icon.svg").read()
rect = '<rect width="512" height="512" fill="#000000"/>'
head, body = s.split(rect)
dark = s.replace(rect, '<rect width="512" height="512" fill="#0d110e"/>').replace('stroke="#000000"', 'stroke="#0d110e"')
open(f"{tmp}/dark.svg", "w").write(dark)
fg = head + '<g transform="translate(256 256) scale(0.62) translate(-256 -256)">' + body.replace("</svg>", "</g>\n</svg>")
open(f"{tmp}/foreground.svg", "w").write(fg)
open(f"{tmp}/splash.svg", "w").write(fg.replace('stroke="#000000"', 'stroke="#0d110e"'))
PY

rsvg-convert -w 1024 -h 1024 icon.svg -o icon.png
rsvg-convert -w 1024 -h 1024 "$tmp/foreground.svg" -o adaptive-foreground.png
rsvg-convert -w 1024 -h 1024 "$tmp/splash.svg" -o splash-icon.png
rsvg-convert -w 288 -h 288 "$tmp/dark.svg" -o hero-icon.png
rsvg-convert -w 96 -h 96 "$tmp/dark.svg" -o title-icon.png
