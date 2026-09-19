//
//  openkey.cpp
//  fcitx5-openkey
//
//  Wrapper fcitx5 cho engine OpenKey. Toan bo logic nam o openkey_core.
//  Che do SendKey (khong preedit) giong OpenKey tren Windows/macOS:
//    phim -> engine -> gui BackSpace + commit chu moi.
//

#include "openkey.h"
#include "openkey_core.h"

#include <fcitx/inputcontext.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "engine/Engine.h"
#include "engine/Macro.h"

namespace fcitx {

namespace {

std::string debugPath() {
    const char *cache = std::getenv("XDG_CACHE_HOME");
    std::string dir;
    if (cache && *cache) {
        dir = std::string(cache) + "/openkey";
    } else {
        const char *home = std::getenv("HOME");
        dir = home && *home ? std::string(home) + "/.cache/openkey"
                            : std::string("/tmp/openkey");
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir + "/debug.log";
}

std::ofstream &debugFile() {
    static std::ofstream out(debugPath(), std::ios::app);
    return out;
}

std::string hex(uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%x", v);
    return buf;
}

} // namespace

void OpenKeyEngine::debug(const std::string &message) {
    if (!debugEnabled_)
        return;
    auto &out = debugFile();
    if (!out.is_open())
        return;
    out << message << "\n";
    out.flush();
}

std::string OpenKeyEngine::configPath() const {
    return openkey::defaultSettingsPath();
}

void OpenKeyEngine::loadConfig() {
    openkey::Settings s = settings_;
    const bool loaded = openkey::loadSettingsFile(configPath(), s);
    if (loaded) {
        settings_ = s;
    } else if (!configLoaded_) {
        // Lan dau chay: ghi file mau de nguoi dung biet cho chinh.
        openkey::saveSettingsFile(configPath(), settings_);
    }
    configLoaded_ = true;
    openkey::applySettings(settings_);
    debugEnabled_ = settings_.debugLog != 0;
    if (settings_.useMacro && !macroPath().empty())
        readFromFile(macroPath(), false);
    core_->reset();
    debug("loadConfig path=" + configPath() + " read=" +
          (loaded ? "yes" : "no") + " lang=" +
          std::to_string(settings_.language) + " inputType=" +
          std::to_string(settings_.inputType) + " codeTable=" +
          std::to_string(settings_.codeTable));
}

std::string OpenKeyEngine::macroPath() const {
    std::string path = configPath();
    size_t slash = path.find_last_of('/');
    if (slash == std::string::npos)
        return "macro.txt";
    return path.substr(0, slash) + "/macro.txt";
}

void OpenKeyEngine::reloadConfig() { loadConfig(); }

OpenKeyEngine::OpenKeyEngine(Instance *instance)
    : instance_(instance), core_(new openkey::Core) {
    loadConfig();
    debug("=== construct ===");
}

OpenKeyEngine::~OpenKeyEngine() = default;

void OpenKeyEngine::keyEvent(const InputMethodEntry &entry, KeyEvent &keyEvent) {
    FCITX_UNUSED(entry);
    const Key rawKey = keyEvent.rawKey();
    const KeyStates states = rawKey.states();
    auto *ic = keyEvent.inputContext();
    debug("keyEvent program=" + (ic ? ic->program() : std::string("?")) +
          " entry=" + entry.name() +
          " sym=" + hex(static_cast<uint32_t>(rawKey.sym())) +
          " key.sym=" + hex(static_cast<uint32_t>(keyEvent.key().sym())) +
          " code=" + std::to_string(static_cast<uint32_t>(rawKey.code())) +
          " states=" + std::to_string(states.toInteger()) +
          " release=" + (keyEvent.isRelease() ? "1" : "0"));

    if (keyEvent.isRelease())
        return;

    // Ctrl/Alt/Super: khong go tieng Viet, nhung van phai bao engine de no cat
    // tu va (khi bat macro) khop macro trong che do tieng Anh.
    const bool otherControlKey = states.test(KeyState::Ctrl) ||
                                 states.test(KeyState::Alt) ||
                                 states.test(KeyState::Super);

    uint16_t code = 0;
    if (!openkey::keySymToKeyCode(static_cast<uint32_t>(rawKey.sym()), code)) {
        // Phim khong lien quan (F1..F12, multimedia, dead key...): cat tu de
        // lan go sau khong bi ghep nham vao tu truoc.
        debug("  -> khong map duoc keysym, reset");
        core_->reset();
        return;
    }

    // Client (GTK/Firefox) tu chen ky tu khi ta pass-through, nen no goi
    // reset() sau MOI phim — khong the tin reset() de xoa trang thai (xem
    // OpenKeyEngine::reset). Doi chieu voi surrounding text de biet chu ta
    // tuong dang co tren man hinh co con dung khong.
    //
    // Hien chi ghi log: cache surrounding text cua fcitx5 co the tre mot nhip
    // so voi commit cua chinh ta, nen chua the dung no de quyet dinh.
    // TODO: khi co du lieu that thi dung no de phat hien con tro bi doi cho.
    if (ic && !core_->screenWord().empty()) {
        const auto &surrounding = ic->surroundingText();
        if (surrounding.isValid()) {
            const bool match = openkey::surroundEndsWith(
                surrounding.text(), surrounding.cursor(), core_->screenWord());
            debug("  surrounding valid=1 cursor=" +
                  std::to_string(surrounding.cursor()) + " chars=" +
                  std::to_string(openkey::utf8Length(surrounding.text())) +
                  " screenWord='" + core_->screenWord() + "' match=" +
                  (match ? "1" : "0"));
        } else {
            debug("  surrounding valid=0 screenWord='" + core_->screenWord() +
                  "'");
        }
    }

    const openkey::KeyResult result =
        core_->handleKey(
            code,
            openkey::capsFromKeyState(static_cast<uint32_t>(rawKey.sym()),
                                      states.test(KeyState::Shift),
                                      states.test(KeyState::CapsLock)),
            otherControlKey);

    if (result.action == openkey::KeyResult::Action::PassThrough) {
        debug("  -> passThrough (engine code=" +
              std::to_string(core_->rawCode()) + ") screenWord='" +
              core_->screenWord() + "'");
        return;
    }

    debug("  -> action=" + std::to_string(static_cast<int>(result.action)) +
          " backspace=" + std::to_string(result.backspaceCount) + " text='" +
          result.text + "' consume=" + (result.consumeKey ? "1" : "0"));

    // Macro: phim kich hoat ('.', ',', Enter...) khong thuoc noi dung thay the
    // nen van de fcitx gui tiep ra client.
    if (result.consumeKey)
        keyEvent.filterAndAccept();

    // Xoa chu cu. Hai duong khac nhau tuy client:
    //
    //  - Client co bao surrounding text (GTK/Firefox, frontend dbus): dung
    //    deleteSurroundingText. Day la duong fcitx5-unikey dung. Voi cac client
    //    nay forwardKey(BackSpace) KHONG xoa duoc gi (kitty thi nguoc lai).
    //  - Client khong bao surrounding text (kitty, text-input-v3): gui
    //    BackSpace that, giong OpenKey ben Win/mac.
    if (result.backspaceCount > 0) {
        const auto &surrounding = ic->surroundingText();
        if (surrounding.isValid()) {
            debug("  xoa " + std::to_string(result.backspaceCount) +
                  " ky tu bang deleteSurroundingText");
            ic->deleteSurroundingText(-result.backspaceCount,
                                      result.backspaceCount);
        } else {
            debug("  xoa " + std::to_string(result.backspaceCount) +
                  " ky tu bang forwardKey BackSpace");
            for (int i = 0; i < result.backspaceCount; i++)
                ic->forwardKey(Key(FcitxKey_BackSpace));
        }
    }

    if (!result.text.empty())
        ic->commitString(result.text);
}

void OpenKeyEngine::filterKey(const InputMethodEntry &entry,
                              KeyEvent &keyEvent) {
    FCITX_UNUSED(entry);
    // Chi ghi log: de biet fcitx5 goi filterKey hay keyEvent.
    debug("filterKey sym=" +
          hex(static_cast<uint32_t>(keyEvent.rawKey().sym())) + " release=" +
          (keyEvent.isRelease() ? "1" : "0"));
}

void OpenKeyEngine::activate(const InputMethodEntry &entry,
                             InputContextEvent &event) {
    FCITX_UNUSED(event);
    debug("activate entry=" + entry.name());
    loadConfig();
}

void OpenKeyEngine::deactivate(const InputMethodEntry &entry,
                               InputContextEvent &event) {
    FCITX_UNUSED(entry);
    FCITX_UNUSED(event);
    debug("deactivate");
    core_->reset();
}

void OpenKeyEngine::reset(const InputMethodEntry &entry,
                          InputContextEvent &event) {
    FCITX_UNUSED(entry);
    FCITX_UNUSED(event);
    // KHONG xoa trang thai o day. GTK/Firefox goi reset() sau moi phim (vi
    // client tu chen ky tu khi ta pass-through), xoa o day se lam mat bo dem
    // tu => khong go duoc tieng Viet. Viec phat hien "chu tren man hinh da
    // khac" do phan kiem tra surrounding text trong keyEvent lo.
    // activate()/deactivate() van xoa vi do la doi input method / doi cua so.
    debug("reset (bo qua, khong xoa trang thai) screenWord='" +
          core_->screenWord() + "'");
}

std::string OpenKeyEngine::subMode(const InputMethodEntry &entry,
                                   InputContext &inputContext) {
    FCITX_UNUSED(entry);
    FCITX_UNUSED(inputContext);
    std::string mode = openkey::inputTypeName(settings_.inputType);
    if (settings_.codeTable != 0) {
        mode += " · ";
        mode += openkey::codeTableName(settings_.codeTable);
    }
    return mode;
}

} // namespace fcitx

FCITX_ADDON_FACTORY_V2(openkey, fcitx::OpenKeyFactory)
