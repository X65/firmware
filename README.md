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
cd src/pico-sdk
git submodule update --init
cd ../..
```

To debug Pico RIA or Pico VPU code, you need a Debug Probe or a Pi Pico as a Picoprobe.

The VSCode launch settings connect to a remote debug session. I use multiple terminals for the debugger and console. You'll also want to add a udev rule to avoid a sudo nightmare. The following are rough notes, you may need to install software which is beyond the scope of this README.

Create `/etc/udev/rules.d/99-pico.rules` with:

```
#Picoprobe
SUBSYSTEM=="usb", ATTRS{idVendor}=="2e8a", MODE="0666"
```

Debug terminal:

```
openocd -f interface/cmsis-dap.cfg -c "adapter speed 5000" -f target/rp2350.cfg -c "program build/src/rp816.elf verify reset exit"
```

Console terminal:

```
picocom -b 115200 /dev/ttyACM0
```

WSL (Windows Subsystem for Linux) can forward the Picoprobe to Linux:

```
PS> usbipd list
BUSID  DEVICE
7-4    CMSIS-DAP v2 Interface, USB Serial Device (COM6)

PS> usbipd wsl attach --busid 7-4
```

WSL needs udev started. Create `/etc/wsl.conf` with:

```
[boot]
command="service udev start"
```
