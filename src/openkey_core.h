//
//  openkey_core.h
//  fcitx5-openkey
//
//  Lop trung gian giua engine OpenKey (../engine) va cac frontend.
//  Khong phu thuoc fcitx => co the test headless bang tests/test_core.cpp.
//
//  Luong giong OpenKey Win/mac:
//    phim -> vKeyHandleEvent() -> {backspaceCount, charData}
//         -> gui BackSpace + commit chu moi (SendKey mode, khong preedit).
//

#ifndef OPENKEY_CORE_H
#define OPENKEY_CORE_H

#include <cstdint>
#include <string>
#include <vector>

// DataType.h chon bang keycode theo platform. Port nay chi chay tren Linux,
// va phai duoc dinh nghia *truoc* khi include de khong lay nham bang mac.
#ifndef LINUX
#define LINUX 1
#endif

#include "engine/DataType.h"

namespace openkey {

// Cung quy uoc voi tham so `capsStatus` cua engine:
//   0 = khong shift/caps, 1 = shift, 2 = caps lock
enum class Caps : uint8_t { None = 0, Shift = 1, CapsLock = 2 };

// Anh xa 1-1 voi cac bien toan cuc ma Engine.h yeu cau (xem engine/Engine.h).
struct Settings {
    int language = 1;                 // 0: English, 1: Vietnamese
    int inputType = 0;                // 0: Telex, 1: VNI, 2: SimpleTelex1, 3: SimpleTelex2
    int codeTable = 0;                // 0: Unicode, 1: TCVN3, 2: VNI-Windows, 3: Unicode Compound, 4: CP1258
    int freeMark = 1;
    int checkSpelling = 1;
    int useModernOrthography = 0;
    int quickTelex = 0;
    int restoreIfWrongSpelling = 1;
    int fixRecommendBrowser = 0;
    int useMacro = 0;
    int useMacroInEnglishMode = 0;
    int autoCapsMacro = 0;
    int useSmartSwitchKey = 0;
    int upperCaseFirstChar = 0;
    int tempOffSpelling = 0;
    int allowConsonantZFWJ = 0;
    int quickStartConsonant = 0;
    int quickEndConsonant = 0;
    int rememberCode = 0;
    int otherLanguage = 0;
    int tempOffOpenKey = 0;
    // 1: ghi vet hoat dong ra ~/.cache/openkey/debug.log (mac dinh tat —
    // bat len khi can chan doan, vi no ghi 1 dong moi phim).
    int debugLog = 0;
};

// Ap cac gia tri trong `s` vao cac bien toan cuc cua engine.
void applySettings(const Settings &s);

// Doc/ghi file cau hinh dang "key = value" (khong can phu thuoc thu vien ngoai).
// Khoa nao khong nhan dien duoc se bi bo qua. Tra ve false neu khong mo duoc file.
bool loadSettingsFile(const std::string &path, Settings &out);
bool saveSettingsFile(const std::string &path, const Settings &s);
std::string defaultSettingsPath();

// ---- Anh xa phim -----------------------------------------------------------

// Ky tu ASCII (thuong hoa deu duoc) -> keycode trong engine/platforms/linux.h.
bool charToKeyCode(char c, uint16_t &out);

// Keysym dac biet (BackSpace, Enter, Space, Tab, Esc, mui ten) -> keycode.
// Tra ve false neu khong phai phim ma engine quan tam.
bool specialKeyToKeyCode(uint32_t keysym, uint16_t &out);

// Ghep 2 ham tren: nhan thang keysym cua fcitx.
bool keySymToKeyCode(uint32_t keysym, uint16_t &out);

// Suy ra trang thai caps ma engine can tu keysym + trang thai modifier.
// Keysym ma frontend gui xuong thuong da phan anh ca Shift lan CapsLock, nhung
// khong phai frontend nao cung vay, nen phai doi chieu voi modifier:
//   - keysym la chu HOA            -> Shift
//   - keysym la chu thuong         -> Shift XOR CapsLock (luat co dien)
//   - con lai (so, dau cau)        -> Shift
Caps capsFromKeyState(uint32_t keysym, bool shift, bool capsLock);

// ---- Ket qua tra ve cho frontend -------------------------------------------

struct KeyResult {
    enum class Action {
        PassThrough, // tra phim lai cho client, khong lam gi
        Replace,     // an phim: xoa `backspaceCount` ky tu roi commit `text`
        Macro,       // an phim: xoa `backspaceCount` ky tu roi commit `text` (noi dung macro)
    };

    Action action = Action::PassThrough;
    int backspaceCount = 0;
    std::string text;        // UTF-8
    bool endSession = false; // goi reset() sau phim nay
    // false voi macro: phim da kich hoat macro (vd dau '.') van phai duoc
    // gui tiep ra client, vi no khong nam trong noi dung thay the.
    bool consumeKey = true;
};

// Lop bao quanh engine. Engine dung trang thai toan cuc nen chi can 1 instance.
class Core {
public:
    Core();
    ~Core();

    Core(const Core &) = delete;
    Core &operator=(const Core &) = delete;

    // Bat dau tu moi (tuong duong startNewSession() cua engine).
    void reset();

    // `code` la keycode OpenKey, `caps` tinh tu trang thai shift/caps lock cua
    // phim vua bam. `otherControlKey` = co Ctrl/Alt/Super.
    KeyResult handleKey(uint16_t code, Caps caps, bool otherControlKey = false);

    // Tien ich: tu ky tu ASCII.
    KeyResult handleChar(char c, Caps caps, bool otherControlKey = false);

    // Ma ket qua tho cua engine (vDoNothing/vWillProcess/...), chu yeu cho test.
    uint8_t rawCode() const;

    // Chu dang hien tren man hinh cua tu hien tai, tinh tu luc bat dau tu.
    // Dung de doi chieu voi surrounding text cua client: client (GTK/Firefox)
    // goi reset() sau MOI phim vi no tu chen ky tu khi ta pass-through, nen
    // khong the tin reset() de xoa trang thai — phai tu kiem chung.
    const std::string &screenWord() const;

private:
    // Than cua handleKey, khong kem chot an toan (de co the thu lai).
    KeyResult handleKeyOnce(uint16_t code, Caps caps, bool otherControlKey);
    void clearScreenWord();
    void notePassThrough(uint16_t code, Caps caps);
    void noteReplaced(const KeyResult &result, uint16_t code, Caps caps);
    vKeyHookState *state_ = nullptr;
    std::string screenWord_;
};

// Doi du lieu engine tra ve (charData/macroData) thanh codepoint Unicode.
uint32_t engineDataToCodepoint(uint32_t data);

// Codepoint cua mot keycode (a-z, 0-9, dau cau) voi trang thai caps tuong ung.
// Tra ve 0 neu keycode khong sinh ra ky tu nao (BackSpace, mui ten...).
uint32_t keyCodeToCodepoint(uint16_t code, Caps caps = Caps::None);

// Nhu tren nhung tra ve chuoi UTF-8.
std::string engineDataToUtf8(uint32_t data);

// Ma hoa 1 codepoint thanh UTF-8 (rong neu khong hop le).
std::string codepointToUtf8(uint32_t cp);

// Duyet nguoc charData[0..count) -> chuoi UTF-8 dung thu tu go.
std::string engineCharDataToUtf8(const Uint32 *data, int count);

// Lenh thay the co hop ly khong: engine khong duoc doi xoa nhieu ky tu hon so
// ky tu ta biet dang co tren man hinh. Engine dem `_index` (tu cua no) va
// `_specialChar`/`_spaceCount` (dau cau/khoang trang di kem) rieng, nen sau khi
// BackSpace di qua cac vung do thi `_index` co the lech — luu y ca
// checkRestoreIfWrongSpelling() dat hBPC = _index. Vuot qua gioi han nay thi
// thay the se an mat chu ben ngoai tu => phai bat dau lai thay vi ap dung.
bool isReplaceSane(int backspaceCount, const std::string &screenWord);

// So ky tu (codepoint) trong chuoi UTF-8.
size_t utf8Length(const std::string &text);

// Kiem tra `cursorChars` ky tu dau cua `text` co ket thuc bang `suffix` khong.
// Dung de doi chieu voi SurroundingText cua fcitx5 (cursor() tinh theo ky tu).
bool surroundEndsWith(const std::string &text, unsigned int cursorChars,
                      const std::string &suffix);

// Ten hien thi cua bang ma / kieu go (dung cho subMode + config UI).
const char *inputTypeName(int inputType);
const char *codeTableName(int codeTable);

} // namespace openkey

#endif // OPENKEY_CORE_H
