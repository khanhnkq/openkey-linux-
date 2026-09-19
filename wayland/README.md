# OpenKey cho Linux

Bộ gõ tiếng Việt **chạy độc lập**, không cần fcitx5, không cần ibus, không cần
quyền đọc bàn phím ở tầng kernel.

Daemon `openkeyd` nói chuyện trực tiếp với compositor Wayland:

```
zwp_input_method_manager_v2       ← nhận phím khi có ô nhập liệu được focus
zwp_virtual_keyboard_manager_v1   ← bơm phím lại cho ứng dụng
```

Engine ghép dấu, bảng mã, macro dùng lại nguyên mã nguồn của
[OpenKey](https://github.com/tuyenvm/OpenKey) (xem `NOTICE`, giấy phép GPL-3.0).

## Vì sao không đi đường addon fcitx5

Bản addon fcitx5 gặp ba trục trặc cố hữu trên Wayland, kiến trúc này tránh hết:

| Vấn đề ở addon fcitx5 | Ở `openkeyd` |
|---|---|
| `commitString()` và `forwardKey()` đi hai đường, thứ tự không đảm bảo | mọi thứ trên **một** kết nối Wayland → thứ tự chắc chắn |
| GTK/Firefox gọi `reset()` mỗi phím, xoá bộ đệm từ | không có `reset()` |
| `forwardKey(BackSpace)` không xoá được gì trên frontend dbus | bơm BackSpace qua virtual keyboard |
| Unicode phải lách qua bảng mã | `commit_string()` nhận UTF-8 thẳng |

## Yêu cầu

- Compositor hỗ trợ `zwp_input_method_v2` (Hyprland, wlroots…)
- `wayland-client`, `xkbcommon`, `wayland-protocols` (XML đã kèm trong `protocols/`)

## Build

```bash
cmake -B build
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

## Chạy

Tắt bộ gõ khác trước — mỗi seat chỉ giữ được **một** chương trình bộ gõ:

```bash
systemctl --user stop fcitx5      # nếu đang dùng fcitx5
../build/wayland/openkeyd -v
```

Thoát: **Ctrl+Alt+Esc** (hoặc Ctrl+C). Cấu hình đọc từ
`~/.config/openkey/openkey.conf`; bật ghi vết bằng khoá `debugLog = 1`.

Cờ hữu ích: `--no-engine` chỉ chuyển tiếp phím (thử kết nối cho an toàn),
`-v` in từng phím.

### Phím tắt đổi Vi/En

Daemon nghe `SIGUSR1` để đổi qua lại Việt/Anh và ghi lại vào
`~/.config/openkey/openkey.conf` (dùng chung file với addon fcitx5):

```bash
pkill -USR1 -x openkeyd
```

`contrib/openkey-toggle.sh` bọc sẵn việc này kèm thông báo, bind vào phím tắt
của compositor:

```
bind = SUPER, F1, exec, ~/.local/bin/openkey-toggle.sh
```

### Cài thành dịch vụ người dùng

```bash
../install-openkeyd.sh          # build + cài vào ~/.local + bật systemd user service
```

### Biến môi trường

Ứng dụng GTK/Qt chỉ dùng bộ gõ của compositor khi **không** bị trỏ sang fcitx:

```
GTK_IM_MODULE=fcitx     ← phải bỏ
QT_IM_MODULE=fcitx      ← phải bỏ
```

## An toàn

Grab bàn phím do compositor quản lý nên tự nhả khi tiến trình thoát hoặc crash —
không có nguy cơ mất bàn phím như cách `EVIOCGRAB`.

## Trạng thái

PoC đã build và chạy, **chưa kiểm thử trong phiên làm việc thật**. Còn thiếu:

- tray/icon + GUI cấu hình
- systemd user service
- `set_preedit_string` để hiện chữ đang gõ có gạch chân
- bảng chọn ứng viên (`zwp_input_popup_surface_v2`)
- phím tắt đổi Vi/En cho chính daemon
- app không dùng `text-input-v3` (game, vài app Electron) chưa có tiếng Việt

## Cấu trúc

```
src/main.cpp          daemon: vòng lặp Wayland, nhận phím, gọi engine, bơm phím
src/openkey_core.*    lớp trung gian không phụ thuộc giao diện
src/engine/           engine OpenKey (nguyên bản, GPL-3.0)
protocols/            XML của input-method-v2 và virtual-keyboard-v1
tests/                test headless cho openkey_core
```
