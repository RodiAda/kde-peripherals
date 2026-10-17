#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"

cmake -B build-user -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build-user
cmake --install build-user

echo "Installing the udev rule (needs root) so the daemon can talk to the devices:"
sudo install -Dm644 udev/70-kde-peripherals.rules /etc/udev/rules.d/70-kde-peripherals.rules
sudo udevadm control --reload
sudo udevadm trigger --subsystem-match=hidraw

systemctl --user daemon-reload
systemctl --user restart peripheralsd.service || true

echo "Installed. Enable Mouse, Keyboard and Gamepad in Configure System Tray -> Entries."
