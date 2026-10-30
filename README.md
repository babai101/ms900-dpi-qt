# ms900-dpi-gui (Qt)

A small Qt Widgets app that sets the hardware DPI of a Dell Pro Premium Mouse MS900 on Linux.
It builds against Qt 6, or Qt 5.15.

## Build and install

```bash
# Debian/Ubuntu: sudo apt install build-essential cmake qt6-base-dev
# Fedora:        sudo dnf install gcc-c++ cmake qt6-qtbase-devel
# Arch:          sudo pacman -S base-devel cmake qt6-base
cmake -B build
cmake --build build -j
sudo cmake --install build          # binary, udev rule, app-menu entry
sudo udevadm control --reload && sudo udevadm trigger   # then replug the receiver
```

## Use

Launch **MS900 DPI** from your app menu, or run `ms900-dpi-gui`.
Set a value with the slider or the spin box (200-DPI steps), then press **Apply**.
Clicking a preset applies it right away. The status line turns green when the mouse
confirms the change.

## Uninstall

```bash
sudo xargs rm -v < build/install_manifest.txt
sudo udevadm control --reload
rm -f ~/.cache/ms900-dpi
```

Without the build folder, delete these by hand: `/usr/local/bin/ms900-dpi-gui`,
`/usr/local/share/applications/ms900-dpi.desktop` and `/etc/udev/rules.d/70-ms900-dpi.rules`.

## Layout

- `src/ms900.c`, `src/ms900.h`: the protocol in plain C (hidraw discovery, send, ack)
- `src/main.cpp`: the Qt window

## Protocol

HID SET_REPORT (Feature), report ID `0x08`, 32 bytes, on receiver interface 2:
`08 13 01 0d 02 <dpi lo> <dpi hi>` followed by zeros.
The mouse acks with the input report `01 0b 01 02 0d <nonce> 01`.
Valid values are 800–8000 in steps of 200.

Not affiliated with or endorsed by Dell. The protocol was worked out from USB traffic captures.
