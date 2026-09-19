#!/bin/sh
# Doi Viet <-> Anh bang mot phim tat duy nhat.
#
# Neu openkeyd (ban doc lap) dang chay thi bao no qua SIGUSR1; khong thi dung
# addon OpenKey cua fcitx5. Nho vay cung phim tat nay chay duoc o ca hai kieu
# cai dat — khong phu thuoc vao viec dang dung bo go nao.

if pkill -USR1 -x openkeyd 2>/dev/null; then
    # openkeyd ghi trang thai moi vao cau hinh; doc lai de bao cho dung.
    lang=$(sed -n 's/^language *= *//p' "$HOME/.config/openkey/openkey.conf" 2>/dev/null | head -1)
    if [ "$lang" = "0" ]; then
        notify-send -a "IME" -t 1500 "Bộ gõ tiếng Việt" "Chế độ: Tiếng Anh" >/dev/null 2>&1
    else
        notify-send -a "IME" -t 1500 "Bộ gõ tiếng Việt" "Chế độ: Tiếng Việt (OpenKey)" >/dev/null 2>&1
    fi
    exit 0
fi

exec "$HOME/.local/bin/fcitx5-ime-toggle.sh"
