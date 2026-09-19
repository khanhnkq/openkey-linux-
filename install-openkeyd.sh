#!/bin/bash
# Build va cai openkeyd (daemon doc lap, KHONG can fcitx5) vao ~/.local.
# Khong can sudo.
set -e
cd "$(dirname "$0")"

cmake -B build
cmake --build build -j"$(nproc)"

mkdir -p "$HOME/.local/bin" "$HOME/.config/systemd/user"
install -m755 build/wayland/openkeyd "$HOME/.local/bin/openkeyd"
install -m755 contrib/openkey-toggle.sh "$HOME/.local/bin/openkey-toggle.sh"
install -m644 contrib/openkeyd.service "$HOME/.config/systemd/user/openkeyd.service"

systemctl --user daemon-reload
systemctl --user enable --now openkeyd.service

echo
echo "OK. Trang thai:"
systemctl --user --no-pager status openkeyd.service | head -5
echo
echo "Nho: phai tat fcitx5 (no giu cho ngoi bo go):"
echo "       fcitx5-remote -e   # hoac: pkill -x fcitx5"
echo "     fcitx5 o may nay do Hyprland khoi dong (execs.lua), khong co systemd unit."
