#!/bin/bash
# Build + test tat ca target (addon fcitx5, daemon openkeyd, test core).
set -e
cd "$(dirname "$0")"
cmake -B build
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
echo
echo "OK:  build/fcitx5/libopenkey.so   (addon fcitx5)"
echo "     build/wayland/openkeyd       (daemon doc lap)"
