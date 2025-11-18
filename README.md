# X65

Modern 8-bit microcomputer

X65 is an effort to build an 8-bit microcomputer for the modern era. It uses best of breed components and strives to keep 8-bit feeling, while being usable for daily basis computing activities.

## Dev Setup

This is only for building the RP2350 software.
For writing 65816 CPU software, see [rp6502-vscode](https://github.com/picocomputer/rp6502-vscode).

Install the C/C++ toolchain for the Raspberry Pi Pico.
For more information, read [Getting started with the Raspberry Pi Pico](https://rptl.io/pico-get-started).

```
sudo apt install cmake gcc-arm-none-eabi libnewlib-arm-none-eabi build-essential gdb-multiarch
```

```
sudo dnf install cmake gcc-arm-linux-gnu arm-none-eabi-newlib gcc-c++-arm-linux-gnu
```

All dependencies are submodules. The following will download the correct version of all SDKs.
It will take an extremely long time to recurse the project, so do this instead:

```
git submodule update --init
```

To debug Pico RIA or Pico VPU code, you need a Debug Probe or a Pi Pico as a Picoprobe.

The Pi Pico VSCode Extension will need this additional software:

```
sudo apt install build-essential gdb-multiarch pkg-config libftdi1-dev libhidapi-hidraw0
```

Create `/etc/udev/rules.d/99-pico.rules` with:

```
#Raspberry Pi Foundation
SUBSYSTEM=="usb", ATTRS{idVendor}=="2e8a", MODE="0666"
```

Debug terminal:

```
openocd -f interface/cmsis-dap.cfg -c "adapter speed 5000" -f target/rp2350.cfg -c "program build/src/x65_ria.elf verify reset exit"
```

Console terminal:

```
picocom -b 115200 /dev/ttyACM0
```

WSL (Windows Subsystem for Linux) can forward the Picoprobe to Linux:

```
PS> winget install usbipd
PS> usbipd list

PS> usbipd wsl attach --busid 7-4
```

WSL needs udev started. Create `/etc/wsl.conf` with:

```
[boot]
command="service udev start"
```

VSCode Serial Monitor doesn't yet send breaks or let you slow down a paste. Minicom is still useful\.

```
minicom -c on -b 115200 -o -D /dev/ttyACM0
```
