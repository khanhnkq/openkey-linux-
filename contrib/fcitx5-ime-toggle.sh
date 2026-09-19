#!/bin/sh
# Toggle bo go fcitx5: OpenKey <-> English (keyboard-us) + notify.
# Duoc goi tu phim tat Hyprland (SUPER + F1 / SUPER + SHIFT + SPACE).
# (fcitx5 trigger mac dinh da tat trong ~/.config/fcitx5/config de Hyprland
# la dau moi duy nhat, tranh double-toggle.)
#
# Muon doi bo go tieng Viet (vd bamboo/unikey) thi export VI_IM truoc khi
# goi script, hoac doi gia tri mac dinh o duoi.

VI_IM="${VI_IM:-openkey}"
EN_IM="keyboard-us"

show_vi() {
    notify-send -a "IME" -t 2000 "Bộ gõ tiếng Việt" "Chế độ: Tiếng Việt ($VI_IM)" >/dev/null 2>&1
}

show_en() {
    notify-send -a "IME" -t 2000 "Bộ gõ tiếng Việt" "Chế độ: Tiếng Anh — nhấn Super+F1 để chuyển sang tiếng Việt" >/dev/null 2>&1
}

cur=$(fcitx5-remote -n 2>/dev/null)
if [ "$cur" = "$VI_IM" ]; then
    fcitx5-remote -s "$EN_IM" >/dev/null 2>&1
    show_en
else
    fcitx5-remote -s "$VI_IM" >/dev/null 2>&1
    # Doi fcitx5 ap dung roi moi bao ( tranh bao sai khi daemon chua nhan )
    i=0
    while [ $i -lt 10 ]; do
        cur=$(fcitx5-remote -n 2>/dev/null)
        if [ "$cur" = "$VI_IM" ]; then
            break
        fi
        sleep 0.1
        i=$((i + 1))
    done
    if [ "$cur" = "$VI_IM" ]; then
        show_vi
    else
        show_en
    fi
fi
