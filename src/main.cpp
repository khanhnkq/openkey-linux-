//
//  openkeyd — OpenKey chay doc lap tren Wayland (khong can fcitx5).
//
//  Noi truc tiep voi compositor qua hai protocol:
//    * zwp_input_method_manager_v2      — nhan phim khi co o nhap lieu focus
//    * zwp_virtual_keyboard_manager_v1  — bom phim lai ra app
//
//  Vi sao lam duoc dieu nay ma khong can quyen kernel:
//    - Compositor goi activate() khi mot o nhap lieu duoc focus; ta
//      grab_keyboard() de nhan phim, va bom lai phim khong dung bang virtual
//      keyboard. Khong doc /dev/input, khong EVIOCGRAB.
//    - Chuoi Unicode gui thang bang commit_string() (UTF-8), nen khong phai
//      lach qua keycode nhu cach uinput.
//    - Moi thu deu di tren CUNG mot ket noi Wayland toi compositor, nen thu tu
//      "xoa roi chen" duoc bao dam — khac han duong fcitx5 (forwardKey va
//      commitString di hai duong khac nhau).
//
//  Toan bo phan quyet dinh (ghep dau, bang ma, macro, cau hinh) dung lai
//  openkey_core — dung chung voi addon fcitx5.
//

#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <set>
#include <string>

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "input-method-unstable-v2-client-protocol.h"
#include "openkey_core.h"
#include "virtual-keyboard-unstable-v1-client-protocol.h"

namespace {

// evdev keycode cua BackSpace (linux/input-event-codes.h: KEY_BACKSPACE).
constexpr uint32_t kKeyBackspace = 14;

// xkb dung keycode = evdev + 8.
constexpr uint32_t kXkbKeycodeOffset = 8;

bool gRunning = true;
// SIGUSR1 = doi qua lai Viet/Anh (de bind vao phim tat cua compositor).
volatile sig_atomic_t gToggleLanguage = 0;

void onSignal(int) { gRunning = false; }

void onSigUsr1(int) { gToggleLanguage = 1; }

void logLine(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
    std::fputc('\n', stderr);
}

struct State {
    // Wayland
    wl_display *display = nullptr;
    wl_registry *registry = nullptr;
    wl_seat *seat = nullptr;
    zwp_input_method_manager_v2 *imManager = nullptr;
    zwp_virtual_keyboard_manager_v1 *vkManager = nullptr;
    zwp_input_method_v2 *im = nullptr;
    zwp_input_method_keyboard_grab_v2 *grab = nullptr;
    zwp_virtual_keyboard_v1 *vk = nullptr;

    // xkb
    xkb_context *xkbCtx = nullptr;
    xkb_keymap *xkbKeymap = nullptr;
    xkb_state *xkbState = nullptr;

    // OpenKey
    std::unique_ptr<openkey::Core> core;
    bool useEngine = true;
    bool verbose = false;

    bool active = false;
    // zwp_input_method_v2.commit(serial) doi serial = so event `done` da nhan.
    uint32_t serial = 0;
    std::string configPath;
    // Phim ta da an (khong chuyen tiep) — nho lai de luc nha cung khong
    // chuyen tiep, neu khong app se bi ket phim.
    std::set<uint32_t> consumeNextRelease;
};

State &state(void *data) { return *static_cast<State *>(data); }

// --------------------------------------------------------------------------
// Bom phim ra app
// --------------------------------------------------------------------------

void imCommit(State &st) {
    if (st.im)
        zwp_input_method_v2_commit(st.im, st.serial);
}

void forwardKey(State &st, uint32_t time, uint32_t key, uint32_t keyState) {
    if (!st.vk)
        return;
    zwp_virtual_keyboard_v1_key(st.vk, time, key, keyState);
}

void sendBackspace(State &st, uint32_t time) {
    if (!st.vk)
        return;
    zwp_virtual_keyboard_v1_key(st.vk, time, kKeyBackspace,
                                WL_KEYBOARD_KEY_STATE_PRESSED);
    zwp_virtual_keyboard_v1_key(st.vk, time, kKeyBackspace,
                                WL_KEYBOARD_KEY_STATE_RELEASED);
}

// --------------------------------------------------------------------------
// Keyboard grab: keymap / key / modifiers / repeat_info
// --------------------------------------------------------------------------

void grabKeymap(void *data, zwp_input_method_keyboard_grab_v2 *,
                uint32_t format, int32_t fd, uint32_t size) {
    State &st = state(data);
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        logLine("openkeyd: keymap format %u khong ho tro, bo qua", format);
        close(fd);
        return;
    }

    // fd goc se bi mmap dung; phai dup mot ban de gui cho virtual keyboard.
    const int vkFd = dup(fd);
    void *map = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED) {
        logLine("openkeyd: mmap keymap that bai");
        if (vkFd >= 0)
            close(vkFd);
        return;
    }
    std::string keymapText(static_cast<const char *>(map), size);
    munmap(map, size);

    if (st.xkbKeymap)
        xkb_keymap_unref(st.xkbKeymap);
    if (st.xkbState)
        xkb_state_unref(st.xkbState);
    st.xkbKeymap = xkb_keymap_new_from_string(
        st.xkbCtx, keymapText.c_str(), XKB_KEYMAP_FORMAT_TEXT_V1,
        XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!st.xkbKeymap) {
        logLine("openkeyd: khong compile duoc keymap");
        if (vkFd >= 0)
            close(vkFd);
        return;
    }
    st.xkbState = xkb_state_new(st.xkbKeymap);

    // Virtual keyboard phai dung keymap nay thi phim bom ra moi duoc hieu
    // dung nhu ban phim that.
    if (st.vk && vkFd >= 0)
        zwp_virtual_keyboard_v1_keymap(st.vk, format, vkFd, size);
    if (vkFd >= 0)
        close(vkFd);

    if (st.verbose)
        logLine("openkeyd: da nap keymap");
}

void grabModifiers(void *data, zwp_input_method_keyboard_grab_v2 *,
                   uint32_t, uint32_t depressed, uint32_t latched,
                   uint32_t locked, uint32_t group) {
    State &st = state(data);
    if (st.xkbState) {
        xkb_state_update_mask(st.xkbState, depressed, latched, locked, 0, 0,
                              group);
    }
}

void grabRepeatInfo(void *data, zwp_input_method_keyboard_grab_v2 *,
                    int32_t, int32_t) {
    (void)data;
}

bool modActive(xkb_state *s, const char *name) {
    return s && xkb_state_mod_name_is_active(s, name,
                                             XKB_STATE_MODS_EFFECTIVE) > 0;
}

void grabKey(void *data, zwp_input_method_keyboard_grab_v2 *, uint32_t,
             uint32_t time, uint32_t key, uint32_t keyState) {
    State &st = state(data);

    // Nha phim: luon chuyen tiep, ke ca phim ta da an lúc nhan (neu khong app
    // se ket phim).
    if (keyState == WL_KEYBOARD_KEY_STATE_RELEASED) {
        if (!st.consumeNextRelease.empty()) {
            // Phim ta da an: khong chuyen tiep.
            st.consumeNextRelease.erase(key);
            return;
        }
        forwardKey(st, time, key, keyState);
        return;
    }

    if (!st.xkbState) {
        forwardKey(st, time, key, keyState);
        return;
    }

    const xkb_keysym_t sym =
        xkb_state_key_get_one_sym(st.xkbState, key + kXkbKeycodeOffset);
    const bool shift = modActive(st.xkbState, XKB_MOD_NAME_SHIFT);
    const bool capsLock = modActive(st.xkbState, XKB_MOD_NAME_CAPS);
    const bool ctrl = modActive(st.xkbState, XKB_MOD_NAME_CTRL);
    const bool alt = modActive(st.xkbState, XKB_MOD_NAME_ALT);
    const bool super = modActive(st.xkbState, "Mod4");

    if (st.verbose) {
        char buf[64] = {0};
        xkb_keysym_get_name(sym, buf, sizeof(buf));
        logLine("openkeyd: key=%u sym=%s (%s%s%s)", key, buf,
                shift ? "shift " : "", capsLock ? "caps " : "",
                ctrl ? "ctrl" : "");
    }

    // Phim thoat khan cap: Ctrl+Alt+Esc.
    if (ctrl && alt &&
        (sym == XKB_KEY_Escape || sym == XKB_KEY_Cancel)) {
        logLine("openkeyd: Ctrl+Alt+Esc — thoat");
        gRunning = false;
        return;
    }

    uint16_t code = 0;
    if (!st.useEngine || !st.core ||
        !openkey::keySymToKeyCode(static_cast<uint32_t>(sym), code)) {
        if (st.core)
            st.core->reset();
        forwardKey(st, time, key, keyState);
        return;
    }

    const openkey::KeyResult result = st.core->handleKey(
        code, openkey::capsFromKeyState(static_cast<uint32_t>(sym), shift,
                                        capsLock),
        ctrl || alt || super);

    if (result.action == openkey::KeyResult::Action::PassThrough) {
        forwardKey(st, time, key, keyState);
        return;
    }

    // Xoa chu cu bang BackSpace that (giong ban Win/mac), roi commit chu moi.
    for (int i = 0; i < result.backspaceCount; i++)
        sendBackspace(st, time);

    if (!result.text.empty()) {
        zwp_input_method_v2_commit_string(st.im, result.text.c_str());
        imCommit(st);
    }

    if (!result.consumeKey) {
        // Macro: phim kich hoat van phai di tiep ra app.
        forwardKey(st, time, key, keyState);
        return;
    }

    // Phim da bi an => nho de khong chuyen tiep luc nha.
    st.consumeNextRelease.insert(key);
}

void grabRelease(void *, zwp_input_method_keyboard_grab_v2 *) {}

// Thu tu event phai khop XML: keymap, key, modifiers, repeat_info.
// (release la request, khong phai event.)
const zwp_input_method_keyboard_grab_v2_listener kGrabListener = {
    grabKeymap, grabKey, grabModifiers, grabRepeatInfo};

// --------------------------------------------------------------------------
// Input method: activate / deactivate / done / unavailable
// --------------------------------------------------------------------------

void imActivate(void *data, zwp_input_method_v2 *) {
    State &st = state(data);
    st.active = true;
    if (st.core)
        st.core->reset();
    st.consumeNextRelease.clear();
    if (!st.grab) {
        st.grab = zwp_input_method_v2_grab_keyboard(st.im);
        zwp_input_method_keyboard_grab_v2_add_listener(st.grab, &kGrabListener,
                                                       &st);
    }
    imCommit(st);
    if (st.verbose)
        logLine("openkeyd: activate");
}

void imDeactivate(void *data, zwp_input_method_v2 *) {
    State &st = state(data);
    st.active = false;
    st.consumeNextRelease.clear();
    if (st.core)
        st.core->reset();
    if (st.grab) {
        zwp_input_method_keyboard_grab_v2_release(st.grab);
        zwp_input_method_keyboard_grab_v2_destroy(st.grab);
        st.grab = nullptr;
    }
    imCommit(st);
    if (st.verbose)
        logLine("openkeyd: deactivate");
}

void imSurroundingText(void *data, zwp_input_method_v2 *, const char *text,
                       uint32_t cursor, uint32_t anchor) {
    State &st = state(data);
    if (st.verbose)
        logLine("openkeyd: surrounding text=\"%s\" cursor=%u anchor=%u", text,
                cursor, anchor);
}

void imTextChangeCause(void *data, zwp_input_method_v2 *, uint32_t cause) {
    State &st = state(data);
    if (st.verbose)
        logLine("openkeyd: text change cause=%u", cause);
}

void imContentType(void *data, zwp_input_method_v2 *, uint32_t hint,
                   uint32_t purpose) {
    State &st = state(data);
    if (st.verbose)
        logLine("openkeyd: content type hint=%u purpose=%u", hint, purpose);
}

void imDone(void *data, zwp_input_method_v2 *) {
    State &st = state(data);
    // `done` bao co trang thai moi; serial dung cho commit chinh la so lan
    // da nhan `done`.
    st.serial++;
    imCommit(st);
}

void imUnavailable(void *data, zwp_input_method_v2 *) {
    State &st = state(data);
    logLine("openkeyd: compositor bao khong dung duoc nua "
            "(co the fcitx5 vua chiem cho) — thoat");
    (void)st;
    gRunning = false;
}

const zwp_input_method_v2_listener kImListener = {
    imActivate,  imDeactivate,   imSurroundingText, imTextChangeCause,
    imContentType, imDone,       imUnavailable};

// --------------------------------------------------------------------------
// Registry
// --------------------------------------------------------------------------

void registryGlobal(void *data, wl_registry *registry, uint32_t name,
                    const char *interface, uint32_t version) {
    State &st = state(data);
    if (std::strcmp(interface, wl_seat_interface.name) == 0) {
        st.seat = static_cast<wl_seat *>(
            wl_registry_bind(registry, name, &wl_seat_interface,
                             version < 7 ? version : 7));
    } else if (std::strcmp(interface,
                          zwp_input_method_manager_v2_interface.name) == 0) {
        st.imManager = static_cast<zwp_input_method_manager_v2 *>(
            wl_registry_bind(registry, name,
                             &zwp_input_method_manager_v2_interface, 1));
    } else if (std::strcmp(
                   interface,
                   zwp_virtual_keyboard_manager_v1_interface.name) == 0) {
        st.vkManager = static_cast<zwp_virtual_keyboard_manager_v1 *>(
            wl_registry_bind(registry, name,
                             &zwp_virtual_keyboard_manager_v1_interface, 1));
    }
}

void registryGlobalRemove(void *, wl_registry *, uint32_t) {}

const wl_registry_listener kRegistryListener = {registryGlobal,
                                                registryGlobalRemove};

// --------------------------------------------------------------------------

// Doi Viet <-> Anh, ghi lai vao cung file cau hinh voi addon fcitx5.
void toggleLanguage(State &st) {
    openkey::Settings settings;
    openkey::loadSettingsFile(openkey::defaultSettingsPath(), settings);
    settings.language = settings.language ? 0 : 1;
    openkey::applySettings(settings);
    openkey::saveSettingsFile(openkey::defaultSettingsPath(), settings);
    if (st.core)
        st.core->reset();
    logLine("openkeyd: doi sang %s",
            settings.language ? "Tiếng Việt" : "English");
}

void applyEngineConfig(State &st, bool useEngine) {
    openkey::Settings settings;
    if (openkey::loadSettingsFile(openkey::defaultSettingsPath(), settings))
        logLine("openkeyd: doc cau hinh tu %s",
                openkey::defaultSettingsPath().c_str());
    else
        logLine("openkeyd: khong co %s, dung mac dinh",
                openkey::defaultSettingsPath().c_str());
    openkey::applySettings(settings);
    st.core = std::make_unique<openkey::Core>();
    st.useEngine = useEngine;
}

[[noreturn]] void usage(const char *argv0) {
    std::fprintf(stderr,
                 "Cach dung: %s [tuy chon]\n"
                 "  --no-engine   chi chuyen tiep phim, khong go tieng Viet\n"
                 "                (dung de thu ket noi truoc cho an toan)\n"
                 "  -v, --verbose in tung phim ra stderr\n"
                 "  -h, --help    hien tro giup\n"
                 "\n"
                 "Luu y: chi mot chuong trinh bo go duoc giu cho ngoi nay, nen\n"
                 "phai tat fcitx5 truoc (systemctl --user stop fcitx5).\n",
                 argv0);
    std::exit(0);
}

} // namespace

int main(int argc, char **argv) {
    State st;
    bool useEngine = true;

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--no-engine") == 0)
            useEngine = false;
        else if (std::strcmp(argv[i], "-v") == 0 ||
                 std::strcmp(argv[i], "--verbose") == 0)
            st.verbose = true;
        else
            usage(argv[0]);
    }

    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);
    signal(SIGUSR1, onSigUsr1);
    signal(SIGPIPE, SIG_IGN);

    applyEngineConfig(st, useEngine);

    st.xkbCtx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!st.xkbCtx) {
        logLine("openkeyd: khong tao duoc xkb context");
        return 1;
    }

    st.display = wl_display_connect(nullptr);
    if (!st.display) {
        logLine("openkeyd: khong ket noi duoc compositor Wayland");
        return 1;
    }
    st.registry = wl_display_get_registry(st.display);
    wl_registry_add_listener(st.registry, &kRegistryListener, &st);
    wl_display_roundtrip(st.display);

    if (!st.seat || !st.imManager || !st.vkManager) {
        logLine("openkeyd: compositor thieu protocol can thiet "
                "(seat=%d input_method_v2=%d virtual_keyboard=%d)",
                st.seat ? 1 : 0, st.imManager ? 1 : 0, st.vkManager ? 1 : 0);
        return 1;
    }

    st.vk = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(st.vkManager,
                                                                   st.seat);
    st.im = zwp_input_method_manager_v2_get_input_method(st.imManager, st.seat);
    zwp_input_method_v2_add_listener(st.im, &kImListener, &st);
    wl_display_roundtrip(st.display);

    logLine("openkeyd: dang chay (%s). Ctrl+Alt+Esc de thoat.",
            useEngine ? "bat go tieng Viet" : "chi chuyen tiep phim");

    while (gRunning) {
        if (gToggleLanguage) {
            gToggleLanguage = 0;
            toggleLanguage(st);
        }
        if (wl_display_dispatch(st.display) == -1) {
            if (errno == EINTR)
                continue;
            logLine("openkeyd: mat ket noi Wayland");
            break;
        }
    }

    if (st.grab) {
        zwp_input_method_keyboard_grab_v2_release(st.grab);
        zwp_input_method_keyboard_grab_v2_destroy(st.grab);
    }
    if (st.im) {
        zwp_input_method_v2_destroy(st.im);
    }
    if (st.vk)
        zwp_virtual_keyboard_v1_destroy(st.vk);
    if (st.imManager)
        zwp_input_method_manager_v2_destroy(st.imManager);
    if (st.vkManager)
        zwp_virtual_keyboard_manager_v1_destroy(st.vkManager);
    if (st.xkbState)
        xkb_state_unref(st.xkbState);
    if (st.xkbKeymap)
        xkb_keymap_unref(st.xkbKeymap);
    if (st.xkbCtx)
        xkb_context_unref(st.xkbCtx);
    if (st.registry)
        wl_registry_destroy(st.registry);
    if (st.seat)
        wl_seat_destroy(st.seat);
    wl_display_disconnect(st.display);
    logLine("openkeyd: da thoat");
    return 0;
}
