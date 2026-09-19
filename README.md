# OpenKey cho Linux

Bộ gõ tiếng Việt cho Linux, dùng lại **engine của
[OpenKey](https://github.com/tuyenvm/OpenKey)** (bản macOS/Windows) — xem
`NOTICE` và `LICENSE` (GPL-3.0).

Một engine, **hai frontend**:

| | `fcitx5/` | `wayland/` |
|---|---|---|
| Cách chạy | addon nạp trong fcitx5 | daemon độc lập `openkeyd` |
| Phụ thuộc | fcitx5 | compositor hỗ trợ `zwp_input_method_v2` |
| Quyền | không cần gì thêm | không cần gì thêm |
| Trạng thái | **đang dùng, đã kiểm chứng** (Hyprland, kitty, Firefox) | PoC, chưa kiểm chứng trong phiên thật |

## Cài nhanh (đường fcitx5)

```bash
git clone <repo-nay> && cd openkey-linux
./deploy.sh
```

`deploy.sh` tự: kiểm phụ thuộc → build + test → cài addon vào `/usr` (backup +
**restart fcitx5 thật**, có kiểm chứng qua DBus) → cài script đổi Vi/En → ghi
cấu hình fcitx5 (`Default Layout=us`, nhóm gõ chỉ `keyboard-us` + `openkey`,
`DefaultIM=openkey`, `ShareInputState=All`). Cuối cùng in ra 3 dòng cần thêm
vào compositor.

Yêu cầu: `cmake`, `extra-cmake-modules`, `fcitx5`, `pkgconf` (+ `base-devel`).

## Cài daemon độc lập (thử nghiệm)

```bash
./install-openkeyd.sh     # build + cài ~/.local/bin + systemd user service
```

Daemon nói chuyện trực tiếp với compositor qua `zwp_input_method_manager_v2`
(nhận phím) và `zwp_virtual_keyboard_manager_v1` (bơm phím lại), nên **không cần
fcitx5**. Xem `wayland/README.md`.

## Build tay

```bash
cmake -B build
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

Tùy chọn: `-DWITH_FCITX5=OFF` (không có fcitx5 dev), `-DWITH_WAYLAND=OFF`
(không có wayland/xkbcommon).

## Cấu hình

Addon đọc `~/.config/openkey/openkey.conf` **mỗi lần chuyển sang input method
OpenKey**. Các khoá chính: `inputType` (0 Telex, 1 VNI), `codeTable`,
`checkSpelling`, `quickTelex`, `useMacro` (+ `~/.config/openkey/macro.txt` theo
định dạng UniKey), `freeMark`, `useModernOrthography`, `debugLog`
(1 = ghi `~/.cache/openkey/debug.log`).

## Bẫy đã gặp — đọc trước khi sửa

1. **`fcitx5-remote -r` KHÔNG nạp lại addon.** Nó chỉ reload config. Sau khi
   cài `.so` mới phải restart fcitx5 thật (`fcitx5 -d --replace`, hoặc
   `busctl --user call org.fcitx.Fcitx5 /controller org.fcitx.Fcitx.Controller1 Restart`).
2. **GTK/Firefox gọi `reset()` sau MỖI phím** khi ta pass-through (client tự
   chèn ký tự → nội dung đổi → GTK reset IM context). Vì vậy
   `OpenKeyEngine::reset()` cố ý **không** xoá trạng thái; chỉ `activate()` /
   `deactivate()` mới xoá.
3. **`forwardKey(BackSpace)` không xoá được gì trên frontend dbus của GTK.**
   Client nào báo `surroundingText()` hợp lệ thì phải dùng
   `deleteSurroundingText()`; kitty (text-input-v3, không có surrounding text)
   thì `forwardKey` lại đúng.
4. **`ShareInputState=Program`** làm mỗi app nhớ trạng thái Vi/En riêng → phải
   chuyển lại mỗi app. Để `All` nếu không muốn vậy.
5. **Engine đếm `_index` tách rời `_specialChar`/`_spaceCount`**, nên sau khi
   BackSpace đi qua các vùng đó, lệnh xoá có thể ăn vào chữ ngoài từ.
   `Core::handleKey()` có chốt `isReplaceSane()` để bắt đầu lại thay vì xoá bừa.

## Chuỗi Telex (engine là bản OpenKey, không tự sửa như UniKey)

| Muốn ra | Gõ |
|---|---|
| `ươ` | `uow` hoặc `wow` (**không** phải `wo`) |
| `được` | `dduowcj`, `ddwowcj` |
| `ủa` | `uar` |
| `nước` | `nuwowsc` |
| xoá dấu | `z` |

## Cấu trúc

```
src/            lop dung chung: openkey_core (khong phu thuoc giao dien) + engine OpenKey
fcitx5/         addon fcitx5 (openkey.cpp, .conf, CMake)
wayland/        daemon doc lap openkeyd + XML protocol
tests/          test headless cho openkey_core (109 case)
contrib/        script doi Vi/En, systemd service
deploy.sh       cai dat tren may moi (duong fcitx5)
install-openkeyd.sh   cai daemon doc lap
remove-old-imes.sh    go cac bo go cu (co backup + chot an toan)
```

## Giấy phép

GPL-3.0. Engine và thuật toán gõ lấy từ OpenKey của Tuyen Mai — xem `NOTICE`.
Đây là bản chỉnh sửa/phát triển thêm, không phải bản phát hành chính thức.
