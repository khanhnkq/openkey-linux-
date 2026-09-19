#!/bin/bash
# Chay fcitx5 voi ban build trong build/src — KHONG can sudo.
#
# fcitx5 tim thu vien addon qua StandardPathsType::Addon (mac dinh chi co
# /usr/lib/fcitx5) va bien nay override duoc bang FCITX_ADDON_DIRS. Cac file
# .conf cua addon nam o <datadir>/fcitx5/addon (kieu Data) nen khong bi anh
# huong — vi vay chi can them build/src len dau la fcitx5 nap ban moi.
#
# Dung khi dang phat trien. Khi xong thi ./install.sh de cai that.
set -e
cd "$(dirname "$0")"

if [ ! -f build/fcitx5/libopenkey.so ]; then
    echo "Chua build. Chay ./build.sh truoc." >&2
    exit 1
fi

echo "Restart fcitx5 voi ban build tai $PWD/build/src ..."
FCITX_ADDON_DIRS="$PWD/build/fcitx5:/usr/lib/fcitx5" fcitx5 -d --replace

sleep 2

# fcitx5 quay ve DefaultIM trong profile (bamboo) sau khi restart.
fcitx5-remote -s openkey 2>/dev/null || true
echo "Input method hien tai: $(fcitx5-remote -n 2>/dev/null)"
echo
echo "Gio hay go thu trong cua so nay:  vieejt -> việt   as -> á   dd -> đ"
echo "Log debug: ~/.cache/openkey/debug.log"
