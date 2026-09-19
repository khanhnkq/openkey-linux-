#!/bin/bash
# Cai OpenKey (addon fcitx5) tren mot may Arch/CachyOS moi.
#
# CHAY BANG USER THUONG (khong sudo — script tu goi sudo cho phan cai /usr):
#     ./deploy.sh
#
# Lam 5 viec: kiem phu thuoc -> build+test -> cai addon vao /usr (backup +
# restart fcitx5 that, co kiem chung) -> cai script doi Vi/En -> ghi cau hinh
# fcitx5 (layout us, chi keyboard-us + openkey). Cuoi cung in ra phan phai lam
# tay o compositor.
set -e
cd "$(dirname "$0")"

if [ "$(id -u)" -eq 0 ]; then
    echo "Dung chay bang sudo — script tu goi sudo cho phan cai vao /usr." >&2
    exit 1
fi

echo "== 1/5 Kiem tra phu thuoc =="
if command -v pacman >/dev/null 2>&1; then
    miss=()
    for p in cmake extra-cmake-modules fcitx5 pkgconf; do
        pacman -Q "$p" >/dev/null 2>&1 || miss+=("$p")
    done
    if [ ${#miss[@]} -gt 0 ]; then
        echo "   Thieu goi: ${miss[*]}" >&2
        echo "   Chay:  sudo pacman -S --needed ${miss[*]}" >&2
        exit 1
    fi
    echo "   du: cmake extra-cmake-modules fcitx5 pkgconf"
else
    echo "   Khong phai Arch — can: cmake, extra-cmake-modules, fcitx5, pkgconf"
fi

echo "== 2/5 Build + chay test =="
cmake -B build
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure

echo "== 3/5 Cai vao /usr va nap lai fcitx5 =="
sudo cp -f /usr/lib/fcitx5/libopenkey.so /usr/lib/fcitx5/libopenkey.so.bak 2>/dev/null || true
sudo cmake --install build

# Restart fcitx5 that su (fcitx5-remote -r chi reload config, KHONG nap lai .so)
dbus_owner() {
    busctl --user call org.freedesktop.DBus /org/freedesktop/DBus \
        org.freedesktop.DBus GetNameOwner s org.fcitx.Fcitx5 2>/dev/null |
        tr -d '"s ' || true
}
before=$(dbus_owner)
if busctl --user call org.fcitx.Fcitx5 /controller \
        org.fcitx.Fcitx.Controller1 Restart >/dev/null 2>&1; then
    sleep 3
    after=$(dbus_owner)
    if [ -n "$after" ] && [ "$before" != "$after" ]; then
        echo "   da restart fcitx5 ($before -> $after)"
    else
        echo "   KHONG xac nhan duoc restart — chay tay: fcitx5 -d --replace" >&2
    fi
else
    fcitx5 -d --replace 2>/dev/null || true
fi

echo "== 4/5 Cai script doi Vi/En =="
mkdir -p "$HOME/.local/bin"
install -m755 contrib/fcitx5-ime-toggle.sh "$HOME/.local/bin/fcitx5-ime-toggle.sh"
install -m755 contrib/openkey-toggle.sh    "$HOME/.local/bin/openkey-toggle.sh"
echo "   ~/.local/bin/{fcitx5-ime-toggle,openkey-toggle}.sh"

echo "== 5/5 Cau hinh fcitx5: layout us, chi keyboard-us + openkey =="
mkdir -p "$HOME/.config/fcitx5"
CFG="$HOME/.config/fcitx5/config"
PROF="$HOME/.config/fcitx5/profile"
stamp=$(date +%Y%m%d-%H%M%S)
[ -f "$CFG" ] && cp "$CFG" "$CFG.bak-$stamp"
[ -f "$PROF" ] && cp "$PROF" "$PROF.bak-$stamp"

cat > "$PROF" <<'EOF'
[Groups/0]
# Group Name
Name=Default
# Layout
Default Layout=us
# Default Input Method
DefaultIM=openkey

[Groups/0/Items/0]
# Name
Name=keyboard-us
# Layout
# Layout=

[Groups/0/Items/1]
# Name
Name=openkey
# Layout
# Layout=

[GroupOrder]
0=Default
EOF

python3 - "$CFG" <<'PY'
import re, sys, pathlib
p = pathlib.Path(sys.argv[1])
s = p.read_text(encoding='utf-8') if p.exists() else ""
if re.search(r'^ShareInputState=', s, re.M):
    s = re.sub(r'^ShareInputState=.*$', 'ShareInputState=All', s, flags=re.M)
elif "[Behavior]" in s:
    s = s.replace("[Behavior]", "[Behavior]\nShareInputState=All", 1)
else:
    s = s.rstrip() + "\n\n[Behavior]\nShareInputState=All\n"
p.write_text(s, encoding='utf-8')
PY
echo "   profile: Default Layout=us, DefaultIM=openkey, nhom go = keyboard-us + openkey"
echo "   config : ShareInputState=All (mot trang thai Vi/En cho moi app)"

fcitx5-remote -r 2>/dev/null || true
fcitx5-remote -s openkey 2>/dev/null || true

cat <<'MSG'

=========================================================
 CON LAI: cau hinh compositor (lam tay)
=========================================================
Hyprland — them vao env.lua / hyprland.conf:
    env = GTK_IM_MODULE,fcitx
    env = QT_IM_MODULE,fcitx
    env = XMODIFIERS,@im=fcitx

Khoi dong fcitx5:
    exec-once = fcitx5 -d

Phim tat doi Viet/Anh:
    bind = SUPER, F1, exec, ~/.local/bin/fcitx5-ime-toggle.sh
    bind = SUPER SHIFT, SPACE, exec, ~/.local/bin/fcitx5-ime-toggle.sh

Roi dang xuat/dang nhap lai (de bien moi truong co hieu luc).

Go thu:  vieejt -> việt    as -> á    dd -> đ
MSG
