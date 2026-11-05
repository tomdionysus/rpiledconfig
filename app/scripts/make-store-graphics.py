#!/usr/bin/env python3
#
# RPiLEDConfig - app
#
# Copyright (C) 2026 T. Cully
# Licensed under the Apache License, Version 2.0 - see LICENSE
#
# Google Play's listing graphics in store/, from assets/icon.svg: the 512 px icon and the
# 1024 x 500 feature graphic. Needs rsvg-convert. Run from app/ (make-icons.sh does).
import base64
import os
import subprocess
import tempfile

BACKGROUND = "#0d110e"

os.makedirs("store", exist_ok=True)
subprocess.check_call(["rsvg-convert", "-w", "512", "-h", "512", "assets/icon.svg", "-o", "store/icon-512.png"])

icon = open("assets/icon.svg").read()
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
