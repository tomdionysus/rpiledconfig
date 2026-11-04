#!/bin/sh
#
# RPiLEDConfig - app
#
# Copyright (C) 2026 T. Cully
# Licensed under the Apache License, Version 2.0 - see LICENSE
#
# Makes every icon from assets/icon.svg: the launcher icons (legacy, on black; adaptive
# foreground, no background, inside the central safe zone) and the splash image in
# android/app/src/main/res, and the home and title-bar icons in assets/ (on the app's
# background, #0d110e). Needs rsvg-convert (MacPorts librsvg). Run from app/ after
# changing icon.svg.
set -e
cd "$(dirname "$0")/.."
RES=android/app/src/main/res
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

python3 - "$tmp" <<'PY'
import sys
tmp = sys.argv[1]
s = open("assets/icon.svg").read()
rect = '<rect width="512" height="512" fill="#000000"/>'
head, body = s.split(rect)
dark = s.replace(rect, '<rect width="512" height="512" fill="#0d110e"/>').replace('stroke="#000000"', 'stroke="#0d110e"')
open(f"{tmp}/dark.svg", "w").write(dark)
fg = head + '<g transform="translate(256 256) scale(0.62) translate(-256 -256)">' + body.replace("</svg>", "</g>\n</svg>")
open(f"{tmp}/foreground.svg", "w").write(fg)
open(f"{tmp}/splash.svg", "w").write(fg.replace('stroke="#000000"', 'stroke="#0d110e"'))
PY

# Launcher: ic_launcher and ic_launcher_round (48 dp) and the adaptive foreground (108 dp).
for density in "mdpi 1" "hdpi 1.5" "xhdpi 2" "xxhdpi 3" "xxxhdpi 4"; do
  set -- $density
  dir=$RES/mipmap-$1
  mkdir -p "$dir"
  size=$(python3 -c "print(int(48 * $2))")
  fg=$(python3 -c "print(int(108 * $2))")
  rsvg-convert -w "$size" -h "$size" assets/icon.svg -o "$dir/ic_launcher.png"
  rsvg-convert -w "$size" -h "$size" assets/icon.svg -o "$dir/ic_launcher_round.png"
  rsvg-convert -w "$fg" -h "$fg" "$tmp/foreground.svg" -o "$dir/ic_launcher_foreground.png"
done

# The splash image: 200 dp of icon on the app's background, the same by day and night.
for density in "mdpi 1" "hdpi 1.5" "xhdpi 2" "xxhdpi 3" "xxxhdpi 4"; do
  set -- $density
  dir=$RES/drawable-$1
  mkdir -p "$dir"
  size=$(python3 -c "print(int(200 * $2))")
  rsvg-convert -w "$size" -h "$size" "$tmp/splash.svg" -o "$dir/splashscreen_logo.png"
  rm -rf "$RES/drawable-night-$1"
done

rsvg-convert -w 288 -h 288 "$tmp/dark.svg" -o assets/hero-icon.png
rsvg-convert -w 96 -h 96 "$tmp/dark.svg" -o assets/title-icon.png
