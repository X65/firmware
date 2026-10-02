#!/usr/bin/env python3
"""Compile and run the actual HCD against a host-side USB register model."""

import os
from pathlib import Path
import subprocess
import tempfile


HERE = Path(__file__).resolve().parent
HEADERS = (
    "tusb_option.h",
    "pico.h",
    "pico/time.h",
    "portable/raspberrypi/rp2040/rp2040_usb.h",
    "osal/osal.h",
    "host/hcd.h",
    "host/usbh.h",
)

with tempfile.TemporaryDirectory(prefix="x65-usb-test-") as directory:
    build = Path(directory)
    for name in HEADERS:
        header = build / name
        header.parent.mkdir(parents=True, exist_ok=True)
        header.write_text("// Provided by mock_hardware.h in the test translation unit.\n")
    for rp2350 in (1, 0):
        executable = build / f"hcd-test-{rp2350}"
        subprocess.run(
            [os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
             "-Wno-unused-parameter", "-g", f"-DPICO_RP2350={rp2350}",
             "-I", str(build), str(HERE / "hcd_test.cpp"), "-o", str(executable)],
            check=True,
        )
        subprocess.run([str(executable)], check=True)
