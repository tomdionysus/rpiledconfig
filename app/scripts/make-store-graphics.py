#!/usr/bin/env python3
#
# RPiLEDConfig - app
#
# Copyright (C) 2026 Tom Cully <mail@tomcully.com>
# Licensed under the Apache License, Version 2.0 - see LICENSE
#
# Google Play's listing graphics in store/, from assets/icon.svg: the 512 px icon and the
# 1024 x 500 feature graphic; and the README's logo in assets/. Needs rsvg-convert. Run from app/ (make-icons.sh does).
import base64
import os
import re
import subprocess
import tempfile

BACKGROUND = "#0d110e"

os.makedirs("store", exist_ok=True)
subprocess.check_call(["rsvg-convert", "-w", "512", "-h", "512", "assets/icon.svg", "-o", "store/icon-512.png"])

icon = open("assets/icon.svg").read()

# The README's logo, on a transparent background: one colour painted through a mask, because the
# icon's black knockout lines would show as black lines if its background were simply removed. Two
# colours, as GitHub's light and dark themes need different ones.
defs = re.search(r"<defs>.*?</defs>", icon, re.S).group(0)
raspberry = re.search(r'<g transform="translate\(85 40\)[^>]*>(.*?)</g>', icon, re.S).group(1)
gear = 'transform="translate(306 38) scale(5.6) translate(-101 -360)"'
sun = 'transform="translate(136 266) scale(10)"'
for name, colour in (("logo-on-dark", "#d4d4d4"), ("logo-on-light", "#3a3a3a")):
    open(f"assets/{name}.svg", "w").write(f"""<?xml version="1.0" encoding="UTF-8"?>
<!-- Made by scripts/make-store-graphics.py from icon.svg. -->
<svg xmlns="http://www.w3.org/2000/svg" width="512" height="512" viewBox="0 0 512 512">
  <title>RPi LED Config</title>
  {defs}
  <mask id="shape" maskUnits="userSpaceOnUse" x="0" y="0" width="512" height="512">
    <g transform="translate(85 40) scale(18)" fill="#ffffff">{raspberry}</g>
    <g {gear}>
      <use href="#gear" fill="none" stroke="#000000" stroke-width="7.8" stroke-linejoin="round" stroke-linecap="round"/>
      <use href="#gear" fill="#ffffff"/>
    </g>
    <g {sun}>
      <use href="#sun" fill="none" stroke="#000000" stroke-width="4.4" stroke-linejoin="round" stroke-linecap="round"/>
      <use href="#sun" fill="#ffffff"/>
    </g>
  </mask>
  <rect width="512" height="512" fill="{colour}" mask="url(#shape)"/>
</svg>
""")
dark = icon.replace('<rect width="512" height="512" fill="#000000"/>', f'<rect width="512" height="512" fill="{BACKGROUND}"/>').replace('stroke="#000000"', f'stroke="{BACKGROUND}"')

with tempfile.TemporaryDirectory() as tmp:
    open(f"{tmp}/dark.svg", "w").write(dark)
    png = subprocess.check_output(["rsvg-convert", "-w", "300", "-h", "300", f"{tmp}/dark.svg"])
    href = "data:image/png;base64," + base64.b64encode(png).decode()
    open(f"{tmp}/feature.svg", "w").write(f"""<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="1024" height="500">
<rect width="1024" height="500" fill="{BACKGROUND}"/>
<image x="70" y="100" width="300" height="300" xlink:href="{href}"/>
<text x="410" y="235" font-family="Helvetica, Arial, sans-serif" font-size="66" font-weight="700" fill="#ffffff">RPi LED Config</text>
<text x="412" y="295" font-family="Helvetica, Arial, sans-serif" font-size="29" fill="#b8c2ba">Set up a Pi's WiFi from its blinking LED</text>
</svg>
""")
    subprocess.check_call(["rsvg-convert", "-w", "1024", "-h", "500", f"{tmp}/feature.svg", "-o", "store/feature-graphic.png"])
