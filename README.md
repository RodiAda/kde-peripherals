# KDE Peripherals

A KDE Plasma 6 system tray widget and a small user daemon for gaming mice and keyboards
that have no native Linux configuration tool. It shows battery levels and lets you change
DPI, polling rate, sensor options, button mapping and lighting without the vendor's
Windows or web software.

## Supported devices

| Device | Connection | Features | Status |
|---|---|---|---|
| MADLIONS MAD R+ (Compx chip, `373b:1040` MAD 8K dongle) | 2.4 GHz dongle | battery, voltage, DPI stages, polling rate up to 8 kHz, lift-off distance, sensor mode, motion sync, angle snapping, ripple control, sensor angle, debounce, button mapping, sleep timeout, long-distance mode, DPI indicator | tested |
| MAD R, MAD R Major, MAD R Major+ and other ATK/Compx mice | 2.4 GHz dongle | same protocol | untested |
| Royal Kludge RK96 RGB (`258a:0089`) | USB cable | lighting effect, brightness, speed, colour, rainbow mode, lighting timeout | tested |
| Logitech HID++ 2.0 devices | Unifying, Bolt, Lightspeed receivers, Bluetooth, USB | battery, DPI, polling rate, SmartShift, wheel mode, high-resolution and natural scrolling, Fn inversion, Easy-Switch channel | untested |

Devices marked untested show a banner in the widget with a link to report how they work.

### Notes

* **MAD R+**: the dongle relays every request to the mouse. While the mouse sleeps only the
  last known values are shown; move the mouse to wake it up. Writes take effect immediately
  and are stored in the mouse. Factory reset, pairing and firmware update commands are never
  sent.
* **RK96**: the keyboard protocol is write-only, so current settings cannot be read back. The
  widget shows the last values set from it; changes made with Fn shortcuts on the keyboard are
  not visible. Only the wired connection carries configuration commands; over 2.4 GHz and
  Bluetooth the keyboard cannot be configured.
* **Logitech**: only features that cannot break input are exposed. Scroll events are never
  diverted to HID++, buttons are not remapped.

## Installation

### Arch Linux

```
cd packaging/arch
makepkg -si
```

### From source

```
cmake -B build -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
sudo cmake --install build
sudo udevadm control --reload
sudo udevadm trigger --subsystem-match=hidraw
```

Then add "Mouse" and "Keyboard" to the system tray. The daemon starts on demand through D-Bus
activation.

The udev rule `70-kde-peripherals.rules` grants the logged-in user access to the hidraw nodes
of supported devices (`uaccess` tag). Nothing runs as root.

## D-Bus API

Service `io.github.rodiada.Peripherals`, object `/io/github/rodiada/Peripherals`, interface
`io.github.rodiada.Peripherals`:

* property `Devices` (string): JSON array of devices. Each entry has `id`, `kind`, `vendor`,
  `name`, `transport`, `online`, optional `battery`, `charging`, `firmware`, `error`, and a
  `controls` array describing the available settings (`choice`, `toggle`, `slider`, `dpi`,
  `color`, `action`).
* method `Apply(id: s, change: s)`: applies a JSON object of `{key: value}` pairs, where keys
  are control keys from `controls`.
* method `Rescan()`.

```
busctl --user get-property io.github.rodiada.Peripherals /io/github/rodiada/Peripherals \
    io.github.rodiada.Peripherals Devices
```

## Tests

```
cmake -B build -DPERIPHERALS_TESTS=ON
cmake --build build
for t in build/*-test; do $t; done
```

## References

Protocol facts were learned from the official ATK HUB web driver, the official Royal Kludge
software, and open documentation of these protocols in libatk-rs, Linux-ATK, atk-tool,
Rangoli, kludgeknight and Solaar. No code was copied from them.

## Disclaimer

Not affiliated with MADLIONS, ATK, Royal Kludge or Logitech. Writing settings to devices is at
your own risk.

## License

MIT
