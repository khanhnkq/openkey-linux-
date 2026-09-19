#!/bin/bash
# Go cac bo go cu, chuyen han sang openkeyd.
#
# CHI CHAY SAU KHI da xac nhan openkeyd go duoc tieng Viet — neu khong ban se
# mat kha nang go tieng Viet cho toi khi cai lai.
#
#   sudo pacman -S ... de cai lai neu can (xem phan cuoi file).
set -e

if [ "$1" != "--da-kiem-tra-openkeyd" ]; then
    cat >&2 <<'MSG'
Dung lai. Hay lam theo thu tu:

  1. fcitx5-remote -e                      # tam tat fcitx5
     (hoac: pkill -x fcitx5 — may nay khong co systemd unit cho fcitx5)
  2. ~/.local/bin/openkeyd -v              # thu go: vieejt -> việt
  3. Neu 1-2 OK thi chay lai:
         ./remove-old-imes.sh --da-kiem-tra-openkeyd

Chay ngay bay gio se khien ban khong go duoc tieng Viet nao ca.
MSG
    exit 1
fi

if ! pgrep -x openkeyd >/dev/null; then
    echo "openkeyd khong chay — bat len truoc: systemctl --user start openkeyd" >&2
    exit 1
fi

B="$HOME/openkey-backup-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$B"
echo "== Backup vao $B =="
cp -r "$HOME/.config/fcitx5" "$B/" 2>/dev/null || true
cp -r "$HOME/.config/ibus" "$B/" 2>/dev/null || true
cp -r "$HOME/.config/hypr" "$B/" 2>/dev/null || true
cp "$HOME/.local/bin/fcitx5-ime-toggle.sh" "$B/" 2>/dev/null || true

echo "== Tat fcitx5 / ibus =="
# fcitx5 tren may nay do Hyprland khoi dong (exec_cmd "fcitx5 -d"), KHONG co
# systemd unit, nen phai bao no tu thoat hoac kill.
fcitx5-remote -e 2>/dev/null || true
sleep 1
pkill -x fcitx5 2>/dev/null || true
systemctl --user stop fcitx5 2>/dev/null || true   # phong khi co unit that
ibus exit 2>/dev/null || true
if pgrep -x fcitx5 >/dev/null; then
    echo "   CANH BAO: fcitx5 van chay — thu: pkill -x fcitx5" >&2
fi

echo "== Go addon openkey cua ban fcitx5 (do chung ta cai truoc day) =="
sudo rm -f /usr/lib/fcitx5/libopenkey.so \
           /usr/lib/fcitx5/libopenkey.so.bak \
           /usr/share/fcitx5/addon/openkey.conf \
           /usr/share/fcitx5/inputmethod/openkey.conf
sudo rm -rf /usr/share/fcitx5/openkey

echo "== Go cac bo go tieng Viet cu =="
for p in fcitx5-bamboo fcitx5-unikey ibus-bamboo ibus-unikey; do
    if pacman -Q "$p" >/dev/null 2>&1; then
        echo "   go $p"
        sudo pacman -Rns --noconfirm "$p"
    fi
done

echo
echo "== Con lai phai lam tay (khong tu sua dotfiles cua ban) =="
cat <<'MSG'
1. Bo cac bien nay khoi cau hinh Hyprland (neu con):
       env = GTK_IM_MODULE,fcitx
       env = QT_IM_MODULE,fcitx
       env = XMODIFIERS,@im=fcitx
       exec-once = fcitx5 -d
   (Neu con GTK_IM_MODULE=fcitx thi Firefox/GTK se di tim fcitx da tat
    va khong thay openkeyd.)

2. Doi phim tat doi Vi/En sang:
       bind = SUPER, F1, exec, ~/.local/bin/openkey-toggle.sh
   (thay cho fcitx5-ime-toggle.sh cu)

3. Khoi dong lai phien (hoac chay lai Hyprland) de moi truong sach.

Cai lai neu can:
    sudo pacman -S fcitx5 fcitx5-bamboo
    cp -r ~/openkey-backup-*/fcitx5 ~/.config/
MSG
