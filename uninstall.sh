#!/bin/bash
set -uo pipefail
cd "$(dirname "$0")"

systemctl --user disable --now peripheralsd.service
if [ -f build-user/install_manifest.txt ]; then
    xargs rm -fv < build-user/install_manifest.txt
else
    rm -fv ~/.local/bin/peripheralsd \
        ~/.local/share/systemd/user/peripheralsd.service \
        ~/.local/share/dbus-1/services/io.github.rodiada.Peripherals.service
    rm -rfv ~/.local/share/plasma/plasmoids/io.github.rodiada.peripherals.{mouse,keyboard,gamepad}
fi
sudo rm -fv /etc/udev/rules.d/70-kde-peripherals.rules
systemctl --user daemon-reload
