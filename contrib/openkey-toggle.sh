#!/bin/sh
# Doi Viet <-> Anh cho openkeyd. Bind vao phim tat cua compositor, vi du:
#   bind = SUPER, F1, exec, ~/.local/bin/openkey-toggle.sh
#   bind = SUPER SHIFT, SPACE, exec, ~/.local/bin/openkey-toggle.sh

if ! pkill -USR1 -x openkeyd 2>/dev/null; then
    notify-send -a "IME" -t 3000 "OpenKey" "openkeyd chưa chạy" >/dev/null 2>&1
    exit 1
fi

# openkeyd ghi trang thai moi vao cau hinh; doc lai de bao cho dung.
lang=$(sed -n 's/^language *= *//p' "$HOME/.config/openkey/openkey.conf" 2>/dev/null | head -1)
if [ "$lang" = "0" ]; then
    notify-send -a "IME" -t 1500 "Bộ gõ tiếng Việt" "Chế độ: Tiếng Anh" >/dev/null 2>&1
else
    notify-send -a "IME" -t 1500 "Bộ gõ tiếng Việt" "Chế độ: Tiếng Việt (OpenKey)" >/dev/null 2>&1
fi
