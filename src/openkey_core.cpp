//
//  openkey_core.cpp
//  fcitx5-openkey
//

#include "openkey_core.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <system_error>

#define LINUX 1
#include "engine/Engine.h"
#include "engine/Vietnamese.h"

// engine/Engine.cpp dinh nghia ham nay (external linkage) nhung khong khai bao
// trong header. Dung lai dung bang _breakCode cua engine thay vi tu chep lai.
bool isWordBreak(const vKeyEvent &event, const vKeyEventState &state,
                 const Uint16 &data);

// ---------------------------------------------------------------------------
// Cac bien toan cuc ma Engine.h yeu cau dinh nghia (xem engine/Engine.h).
// Mac dinh: tieng Viet, Telex, Unicode, bat check chinh ta.
// ---------------------------------------------------------------------------
int vLanguage = 1;
int vInputType = 0;
int vFreeMark = 1;
int vCodeTable = 0;
int vSwitchKeyStatus = 0;
int vCheckSpelling = 1;
int vUseModernOrthography = 0;
int vQuickTelex = 0;
int vRestoreIfWrongSpelling = 1;
int vFixRecommendBrowser = 0;
int vUseMacro = 0;
int vUseMacroInEnglishMode = 0;
int vAutoCapsMacro = 0;
int vUseSmartSwitchKey = 0;
int vUpperCaseFirstChar = 0;
int vTempOffSpelling = 0;
int vAllowConsonantZFWJ = 0;
int vQuickStartConsonant = 0;
int vQuickEndConsonant = 0;
int vRememberCode = 0;
int vOtherLanguage = 0;
int vTempOffOpenKey = 0;

namespace openkey {

namespace {

// Keysym X11 (trung voi FcitxKey_* cua fcitx5). Co y khong include fcitx
// de module nay build/test doc lap duoc.
constexpr uint32_t XK_space = 0x0020;
constexpr uint32_t XK_BackSpace = 0xFF08;
constexpr uint32_t XK_Tab = 0xFF09;
constexpr uint32_t XK_Linefeed = 0xFF0A;
constexpr uint32_t XK_Return = 0xFF0D;
constexpr uint32_t XK_KP_Enter = 0xFF8D;
constexpr uint32_t XK_Escape = 0xFF1B;
constexpr uint32_t XK_Home = 0xFF50;
constexpr uint32_t XK_Left = 0xFF51;
constexpr uint32_t XK_Up = 0xFF52;
constexpr uint32_t XK_Right = 0xFF53;
constexpr uint32_t XK_Down = 0xFF54;
constexpr uint32_t XK_Prior = 0xFF55;
constexpr uint32_t XK_Next = 0xFF56;
constexpr uint32_t XK_End = 0xFF57;
constexpr uint32_t XK_Delete = 0xFFFF;

// So byte toi da ma 1 lan commit co the tao ra (tranh truong hop engine tra ve
// gia tri rac lam nghen client).
constexpr int kMaxBackspace = MAX_BUFF;

std::string encodeUtf8(uint32_t cp) {
    std::string out;
    if (cp == 0 || cp > 0x10FFFF)
        return out;
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return out;
}

// Engine luu ky tu theo bang ma dang chon. Client fcitx5 luon nhan UTF-8 nen
// phai doi nguoc ve Unicode.
//
// Trong moi bang ma, chi so chan la chu HOA va chi so le la chu thuong
// ({CAPS, NORMAL, CAPS_W, NORMAL_W, roi den cac dau...}). Mot so bang ma cu
// (TCVN3) dung chung 1 byte cho ca hoa lan thuong, nen phai dung `upper` de
// chon dung bien the thay vi lay ket qua dau tien tim thay.
uint32_t tableCodeToUnicode(int table, uint32_t code, bool upper) {
    if (table <= 0 || table >= 5)
        return code;
    const auto &cur = _codeTable[table];
    const auto &uni = _codeTable[0];
    uint32_t fallback = code;
    bool hasFallback = false;
    for (const auto &entry : cur) {
        auto it = uni.find(entry.first);
        if (it == uni.end())
            continue;
        const auto &values = entry.second;
        const auto &unicode = it->second;
        for (size_t i = 0; i < values.size() && i < unicode.size(); i++) {
            if (values[i] != code || unicode[i] == 0)
                continue;
            const bool entryUpper = (i % 2) == 0;
            if (entryUpper == upper)
                return unicode[i];
            if (!hasFallback) {
                fallback = unicode[i];
                hasFallback = true;
            }
        }
    }
    return fallback;
}

std::string restoredKeyText(uint16_t code, Caps caps) {
    return encodeUtf8(keyCodeToCodepoint(code, caps));
}

// Bo 1 ky tu UTF-8 o cuoi.
void dropLastChar(std::string &s) {
    if (s.empty())
        return;
    size_t i = s.size() - 1;
    while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80)
        i--;
    s.erase(i);
}

void dropChars(std::string &s, size_t count) {
    while (count-- > 0 && !s.empty())
        dropLastChar(s);
}

std::string trim(const std::string &s) {
    const char *ws = " \t\r\n";
    size_t b = s.find_first_not_of(ws);
    if (b == std::string::npos)
        return {};
    size_t e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

struct SettingEntry {
    const char *name;
    int Settings::*field;
};

const SettingEntry kSettingEntries[] = {
    {"language", &Settings::language},
    {"inputType", &Settings::inputType},
    {"codeTable", &Settings::codeTable},
    {"freeMark", &Settings::freeMark},
    {"checkSpelling", &Settings::checkSpelling},
    {"useModernOrthography", &Settings::useModernOrthography},
    {"quickTelex", &Settings::quickTelex},
    {"restoreIfWrongSpelling", &Settings::restoreIfWrongSpelling},
    {"fixRecommendBrowser", &Settings::fixRecommendBrowser},
    {"useMacro", &Settings::useMacro},
    {"useMacroInEnglishMode", &Settings::useMacroInEnglishMode},
    {"autoCapsMacro", &Settings::autoCapsMacro},
    {"useSmartSwitchKey", &Settings::useSmartSwitchKey},
    {"upperCaseFirstChar", &Settings::upperCaseFirstChar},
    {"tempOffSpelling", &Settings::tempOffSpelling},
    {"allowConsonantZFWJ", &Settings::allowConsonantZFWJ},
    {"quickStartConsonant", &Settings::quickStartConsonant},
    {"quickEndConsonant", &Settings::quickEndConsonant},
    {"rememberCode", &Settings::rememberCode},
    {"otherLanguage", &Settings::otherLanguage},
    {"tempOffOpenKey", &Settings::tempOffOpenKey},
    {"debugLog", &Settings::debugLog},
};

} // namespace

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

void applySettings(const Settings &s) {
    vLanguage = s.language;
    vInputType = s.inputType;
    vCodeTable = s.codeTable;
    vFreeMark = s.freeMark;
    vCheckSpelling = s.checkSpelling;
    vUseModernOrthography = s.useModernOrthography;
    vQuickTelex = s.quickTelex;
    vRestoreIfWrongSpelling = s.restoreIfWrongSpelling;
    vFixRecommendBrowser = s.fixRecommendBrowser;
    vUseMacro = s.useMacro;
    vUseMacroInEnglishMode = s.useMacroInEnglishMode;
    vAutoCapsMacro = s.autoCapsMacro;
    vUseSmartSwitchKey = s.useSmartSwitchKey;
    vUpperCaseFirstChar = s.upperCaseFirstChar;
    vTempOffSpelling = s.tempOffSpelling;
    vAllowConsonantZFWJ = s.allowConsonantZFWJ;
    vQuickStartConsonant = s.quickStartConsonant;
    vQuickEndConsonant = s.quickEndConsonant;
    vRememberCode = s.rememberCode;
    vOtherLanguage = s.otherLanguage;
    vTempOffOpenKey = s.tempOffOpenKey;
}

std::string defaultSettingsPath() {
    const char *xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg)
        return std::string(xdg) + "/openkey/openkey.conf";
    const char *home = std::getenv("HOME");
    if (home && *home)
        return std::string(home) + "/.config/openkey/openkey.conf";
    return "openkey.conf";
}

bool loadSettingsFile(const std::string &path, Settings &out) {
    std::ifstream in(path);
    if (!in.is_open())
        return false;
    std::string line;
    while (std::getline(in, line)) {
        size_t hash = line.find('#');
        if (hash != std::string::npos)
            line = line.substr(0, hash);
        size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        std::string key = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));
        if (key.empty() || value.empty())
            continue;
        for (const auto &entry : kSettingEntries) {
            if (key != entry.name)
                continue;
            out.*(entry.field) = std::atoi(value.c_str());
            break;
        }
    }
    return true;
}

bool saveSettingsFile(const std::string &path, const Settings &s) {
    size_t slash = path.find_last_of('/');
    if (slash != std::string::npos) {
        std::error_code ec;
        std::filesystem::create_directories(path.substr(0, slash), ec);
        if (ec)
            return false;
    }
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open())
        return false;
    out << "# fcitx5-openkey\n";
    for (const auto &entry : kSettingEntries)
        out << entry.name << " = " << (s.*(entry.field)) << "\n";
    return out.good();
}

// ---------------------------------------------------------------------------
// Anh xa phim
// ---------------------------------------------------------------------------

bool charToKeyCode(char c, uint16_t &out) {
    switch (c) {
    case 'a': case 'A': out = KEY_A; return true;
    case 'b': case 'B': out = KEY_B; return true;
    case 'c': case 'C': out = KEY_C; return true;
    case 'd': case 'D': out = KEY_D; return true;
    case 'e': case 'E': out = KEY_E; return true;
    case 'f': case 'F': out = KEY_F; return true;
    case 'g': case 'G': out = KEY_G; return true;
    case 'h': case 'H': out = KEY_H; return true;
    case 'i': case 'I': out = KEY_I; return true;
    case 'j': case 'J': out = KEY_J; return true;
    case 'k': case 'K': out = KEY_K; return true;
    case 'l': case 'L': out = KEY_L; return true;
    case 'm': case 'M': out = KEY_M; return true;
    case 'n': case 'N': out = KEY_N; return true;
    case 'o': case 'O': out = KEY_O; return true;
    case 'p': case 'P': out = KEY_P; return true;
    case 'q': case 'Q': out = KEY_Q; return true;
    case 'r': case 'R': out = KEY_R; return true;
    case 's': case 'S': out = KEY_S; return true;
    case 't': case 'T': out = KEY_T; return true;
    case 'u': case 'U': out = KEY_U; return true;
    case 'v': case 'V': out = KEY_V; return true;
    case 'w': case 'W': out = KEY_W; return true;
    case 'x': case 'X': out = KEY_X; return true;
    case 'y': case 'Y': out = KEY_Y; return true;
    case 'z': case 'Z': out = KEY_Z; return true;
    case '1': out = KEY_1; return true;
    case '2': out = KEY_2; return true;
    case '3': out = KEY_3; return true;
    case '4': out = KEY_4; return true;
    case '5': out = KEY_5; return true;
    case '6': out = KEY_6; return true;
    case '7': out = KEY_7; return true;
    case '8': out = KEY_8; return true;
    case '9': out = KEY_9; return true;
    case '0': out = KEY_0; return true;
    case '`': out = KEY_BACKQUOTE; return true;
    case '-': out = KEY_MINUS; return true;
    case '=': out = KEY_EQUALS; return true;
    case '[': out = KEY_LEFT_BRACKET; return true;
    case ']': out = KEY_RIGHT_BRACKET; return true;
    case '\\': out = KEY_BACK_SLASH; return true;
    case ';': out = KEY_SEMICOLON; return true;
    case '\'': out = KEY_QUOTE; return true;
    case ',': out = KEY_COMMA; return true;
    case '.': out = KEY_DOT; return true;
    case '/': out = KEY_SLASH; return true;
    case ' ': out = KEY_SPACE; return true;
    default: return false;
    }
}

bool specialKeyToKeyCode(uint32_t keysym, uint16_t &out) {
    switch (keysym) {
    case XK_BackSpace: out = KEY_DELETE; return true;
    case XK_Return:
    case XK_KP_Enter:
    case XK_Linefeed: out = KEY_ENTER; return true;
    case XK_space: out = KEY_SPACE; return true;
    case XK_Tab: out = KEY_TAB; return true;
    case XK_Escape: out = KEY_ESC; return true;
    case XK_Left: out = KEY_LEFT; return true;
    case XK_Right: out = KEY_RIGHT; return true;
    case XK_Up: out = KEY_UP; return true;
    case XK_Down: out = KEY_DOWN; return true;
    // Cac phim con lai khong co keycode rieng trong platforms/linux.h nhung van
    // phai cat tu (word break) giong nhu ESC.
    case XK_Home:
    case XK_End:
    case XK_Prior:
    case XK_Next:
    case XK_Delete: out = KEY_ESC; return true;
    default: return false;
    }
}

bool keySymToKeyCode(uint32_t keysym, uint16_t &out) {
    if (keysym < 0x80) {
        if (charToKeyCode(static_cast<char>(keysym), out))
            return true;
    }
    return specialKeyToKeyCode(keysym, out);
}

Caps capsFromKeyState(uint32_t keysym, bool shift, bool capsLock) {
    const bool isUpper = keysym >= 'A' && keysym <= 'Z';
    const bool isLower = keysym >= 'a' && keysym <= 'z';

    bool upper;
    if (isUpper)
        upper = true;
    else if (isLower)
        // Frontend da ap CapsLock thi keysym da la chu hoa; neu van la chu
        // thuong thi phai tu suy ra. Shift XOR CapsLock.
        upper = shift != capsLock;
    else
        upper = shift;

    if (!upper)
        return Caps::None;
    return capsLock && !shift ? Caps::CapsLock : Caps::Shift;
}

// ---------------------------------------------------------------------------
// Chuyen du lieu engine -> Unicode/UTF-8
// ---------------------------------------------------------------------------

uint32_t engineDataToCodepoint(uint32_t data) {
    if (data & PURE_CHARACTER_MASK)
        return data & 0xFFFFFF;
    if (!(data & CHAR_CODE_MASK)) {
        // La keycode: doi sang ky tu ASCII (a-z, 0-9, dau cau).
        Uint16 ch = keyCodeToCharacter(data);
        if (ch != 0)
            return ch;
        return 0;
    }
    uint32_t code = getCharacterCode(data) & 0xFFFFFF;
    return tableCodeToUnicode(vCodeTable, code, (data & CAPS_MASK) != 0);
}

uint32_t keyCodeToCodepoint(uint16_t code, Caps caps) {
    Uint32 withCaps = code;
    if (caps != Caps::None)
        withCaps |= CAPS_MASK;
    Uint16 ch = keyCodeToCharacter(withCaps);
    if (ch == 0 && caps != Caps::None)
        ch = keyCodeToCharacter(code);
    return ch;
}

std::string engineDataToUtf8(uint32_t data) {
    return encodeUtf8(engineDataToCodepoint(data));
}

std::string codepointToUtf8(uint32_t cp) { return encodeUtf8(cp); }

std::string engineCharDataToUtf8(const Uint32 *data, int count) {
    if (!data || count <= 0)
        return {};
    if (count > MAX_BUFF)
        count = MAX_BUFF;
    // charData duoc engine ghi theo chieu nguoc (xem hData[_index - 1 - i]).
    std::string out;
    for (int i = count - 1; i >= 0; i--)
        out += engineDataToUtf8(data[i]);
    return out;
}

size_t utf8Length(const std::string &text) {
    size_t n = 0;
    for (unsigned char c : text) {
        if ((c & 0xC0) != 0x80)
            n++;
    }
    return n;
}

bool isReplaceSane(int backspaceCount, const std::string &screenWord) {
    if (backspaceCount < 0)
        return false;
    return static_cast<size_t>(backspaceCount) <= utf8Length(screenWord);
}

bool surroundEndsWith(const std::string &text, unsigned int cursorChars,
                      const std::string &suffix) {
    if (text.empty())
        return cursorChars == 0 && suffix.empty();

    // boundaries[k] = byte offset bat dau cua ky tu thu k.
    std::vector<size_t> boundaries;
    boundaries.reserve(text.size());
    boundaries.push_back(0);
    for (size_t i = 1; i < text.size(); i++) {
        if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80)
            boundaries.push_back(i);
    }

    const size_t totalChars = boundaries.size();
    if (cursorChars > totalChars)
        return false;

    const size_t suffixChars = utf8Length(suffix);
    if (suffixChars > cursorChars)
        return false;
    if (suffixChars == 0)
        return true;

    const size_t end =
        cursorChars == totalChars ? text.size() : boundaries[cursorChars];
    const size_t begin = boundaries[cursorChars - suffixChars];
    return text.compare(begin, end - begin, suffix) == 0;
}

const char *inputTypeName(int inputType) {
    switch (inputType) {
    case 0: return "Telex";
    case 1: return "VNI";
    case 2: return "Simple Telex 1";
    case 3: return "Simple Telex 2";
    default: return "Telex";
    }
}

const char *codeTableName(int codeTable) {
    switch (codeTable) {
    case 0: return "Unicode";
    case 1: return "TCVN3 (ABC)";
    case 2: return "VNI Windows";
    case 3: return "Unicode Compound";
    case 4: return "Vietnamese locale CP1258";
    default: return "Unicode";
    }
}

// ---------------------------------------------------------------------------
// Core
// ---------------------------------------------------------------------------

Core::Core() { state_ = static_cast<vKeyHookState *>(vKeyInit()); }

Core::~Core() = default;

void Core::reset() {
    startNewSession();
    // startNewSession() cua engine khong xoa bo dem phim macro, nen sau khi
    // thay the macro xong phai xoa tay, neu khong macro ke tiep se bi noi
    // khoa ("btw" + "vn" thanh "btwvn").
    if (state_)
        state_->macroKey.clear();
    clearScreenWord();
}

uint8_t Core::rawCode() const { return state_ ? state_->code : 0; }

const std::string &Core::screenWord() const { return screenWord_; }

void Core::clearScreenWord() { screenWord_.clear(); }

// Client tu chen ky tu khi ta pass-through => cap nhat lai chu dang hien.
// extCode: 1 = word break, 2 = delete, 3 = normal key (xem engine/Engine.cpp).
//
// Khong the chi dua vao extCode: voi dau cau nhu ',' engine dat extCode = 3
// (vi no nam trong _charKeyCode) nhung tu van bi cat. Nen hoi thang engine.
void Core::notePassThrough(uint16_t code, Caps caps) {
    if (!state_) {
        screenWord_.clear();
        return;
    }
    const bool wordBreak =
        state_->extCode == 1 ||
        isWordBreak(vKeyEvent::Keyboard, vKeyEventState::KeyDown, code) ||
        code == KEY_SPACE; // SPACE khong nam trong _breakCode cua engine
    if (wordBreak) {
        screenWord_.clear();
        return;
    }
    switch (state_->extCode) {
    case 2:
        dropLastChar(screenWord_);
        break;
    case 3:
        screenWord_ += restoredKeyText(code, caps);
        break;
    default:
        break;
    }
}

void Core::noteReplaced(const KeyResult &result, uint16_t code, Caps caps) {
    dropChars(screenWord_,
              std::min<size_t>(result.backspaceCount, utf8Length(screenWord_)));
    screenWord_ += result.text;
    if (!result.consumeKey) {
        // Phim kich hoat macro van duoc gui tiep ra client.
        screenWord_ += restoredKeyText(code, caps);
    }
}

KeyResult Core::handleKey(uint16_t code, Caps caps, bool otherControlKey) {
    // Chu cua tu truoc khi xy ly phim nay — de kiem tra lenh thay the co doi
    // xoa qua nhieu so voi nhung gi dang that su co tren man hinh khong.
    const std::string before = screenWord_;

    const KeyResult result = handleKeyOnce(code, caps, otherControlKey);
    if (result.action != KeyResult::Action::Replace ||
        isReplaceSane(result.backspaceCount, before))
        return result;

    // Engine lech trang thai (thuong sau khi BackSpace di qua vung
    // _specialChar/_spaceCount cua engine). Bat dau lai roi thu lai 1 lan.
    reset();
    const std::string afterReset = screenWord_;
    const KeyResult retry = handleKeyOnce(code, caps, otherControlKey);
    if (retry.action == KeyResult::Action::Replace &&
        !isReplaceSane(retry.backspaceCount, afterReset)) {
        // Van lech: de phim di qua binh thuong con hon an mat chu cua nguoi dung.
        reset();
        return KeyResult{};
    }
    return retry;
}

KeyResult Core::handleKeyOnce(uint16_t code, Caps caps, bool otherControlKey) {
    KeyResult result;
    if (!state_)
        return result;

    // Che do tieng Anh: khong go tieng Viet, chi con macro neu duoc bat.
    if (vLanguage == 0) {
        clearScreenWord();
        if (!(vUseMacro && vUseMacroInEnglishMode))
            return result;
        vEnglishMode(vKeyEventState::KeyDown, code, caps != Caps::None,
                     otherControlKey);
        if (state_->code != vReplaceMaro)
            return result;
        result.action = KeyResult::Action::Macro;
        result.backspaceCount = std::min<int>(state_->backspaceCount, kMaxBackspace);
        for (Uint32 d : state_->macroData)
            result.text += engineDataToUtf8(d);
        result.consumeKey = false;
        result.endSession = true;
        reset();
        return result;
    }

    vKeyHandleEvent(vKeyEvent::Keyboard, vKeyEventState::KeyDown, code,
                    static_cast<Uint8>(caps), otherControlKey);

    const Byte engineCode = state_->code;

    if (engineCode == vDoNothing) {
        notePassThrough(code, caps); // de phim di qua; engine tu reset session
        return result;
    }

    if (engineCode == vReplaceMaro) {
        result.action = KeyResult::Action::Macro;
        result.backspaceCount = std::min<int>(state_->backspaceCount, kMaxBackspace);
        for (Uint32 d : state_->macroData)
            result.text += engineDataToUtf8(d);
        // Phim kich hoat macro ('.', ',', Enter...) khong thuoc noi dung thay
        // the nen de no di tiep ra client.
        result.consumeKey = false;
        result.endSession = true;
        reset();
        return result;
    }

    if (engineCode == vWillProcess || engineCode == vRestore ||
        engineCode == vRestoreAndStartNewSession || engineCode == vBreakWord) {
        if (state_->backspaceCount > kMaxBackspace) {
            // Du lieu bat thuong: khong an phim.
            return result;
        }
        result.action = KeyResult::Action::Replace;
        result.backspaceCount = state_->backspaceCount;
        result.text = engineCharDataToUtf8(state_->charData, state_->newCharCount);
        if (engineCode == vRestore || engineCode == vRestoreAndStartNewSession)
            result.text += restoredKeyText(code, caps);
        noteReplaced(result, code, caps);
        if (engineCode == vRestoreAndStartNewSession || engineCode == vBreakWord) {
            result.endSession = true;
            reset();
        }
        return result;
    }

    return result;
}

KeyResult Core::handleChar(char c, Caps caps, bool otherControlKey) {
    uint16_t code = 0;
    if (!charToKeyCode(c, code))
        return KeyResult{};
    return handleKey(code, caps, otherControlKey);
}

} // namespace openkey
