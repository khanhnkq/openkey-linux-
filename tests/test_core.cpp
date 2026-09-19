//
//  test_core.cpp
//  fcitx5-openkey
//
//  Test headless cho openkey_core: khong can fcitx5, khong can GUI.
//  Gia lap dung luong cua addon:  PassThrough -> ky tu di thang vao buffer,
//  Replace -> xoa backspaceCount ky tu roi chen text.
//

#include "openkey_core.h"
#include "engine/Macro.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using openkey::Caps;
using openkey::Core;
using openkey::KeyResult;

namespace {

int gFailed = 0;
int gPassed = 0;

// Xoa 1 ky tu UTF-8 o cuoi chuoi.
void utf8PopBack(std::string &s) {
    if (s.empty())
        return;
    size_t i = s.size() - 1;
    while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80)
        i--;
    s.erase(i);
}

// Gia lap 1 input context: go phim va giu "van ban tren man hinh".
struct Typer {
    Core core;
    std::string screen;

    // Phim di thang ra client: client tu sinh ky tu theo layout + shift/caps.
    void appendKey(uint16_t code, Caps caps) {
        if (code == KEY_DELETE) {
            utf8PopBack(screen); // BackSpace that su
            return;
        }
        uint32_t cp = openkey::keyCodeToCodepoint(code, caps);
        if (cp)
            screen += openkey::codepointToUtf8(cp);
    }

    void rawKey(uint16_t code, Caps caps) {
        KeyResult r = core.handleKey(code, caps);
        if (r.action == KeyResult::Action::PassThrough) {
            appendKey(code, caps);
            return;
        }
        for (int i = 0; i < r.backspaceCount; i++)
            utf8PopBack(screen);
        screen += r.text;
        if (r.endSession)
            core.reset();
        // Macro khong an phim kich hoat => client van nhan ky tu do.
        if (!r.consumeKey)
            appendKey(code, caps);
    }

    // Go 1 chuoi ASCII; chu hoa => Shift.
    void type(const std::string &keys) {
        for (char c : keys) {
            bool upper = c >= 'A' && c <= 'Z';
            char lower = upper ? static_cast<char>(c - 'A' + 'a') : c;
            uint16_t code = 0;
            if (!openkey::charToKeyCode(lower, code))
                continue;
            rawKey(code, upper ? Caps::Shift : Caps::None);
        }
    }

    void clear() {
        screen.clear();
        core.reset();
    }
};

void expectEq(const std::string &what, const std::string &got,
              const std::string &want) {
    if (got == want) {
        gPassed++;
        std::printf("  ok   %-28s -> %s\n", what.c_str(), got.c_str());
    } else {
        gFailed++;
        std::printf("  FAIL %-28s -> got '%s', want '%s'\n", what.c_str(),
                    got.c_str(), want.c_str());
    }
}

struct TelexCase {
    const char *keys;
    const char *want;
};

void testTelex() {
    std::printf("[Telex - bang ma Unicode]\n");
    static const TelexCase cases[] = {
        {"as", "á"},
        {"af", "à"},
        {"ar", "ả"},
        {"ax", "ã"},
        {"aj", "ạ"},
        {"aw", "ă"},
        {"aa", "â"},
        {"dd", "đ"},
        {"ee", "ê"},
        {"oo", "ô"},
        {"ow", "ơ"},
        {"uw", "ư"},
        {"w", "ư"},
        {"vieejt", "việt"},
        {"tieengs", "tiếng"},
        {"chaof", "chào"},
        {"tooi", "tôi"},
        {"nuwowsc", "nước"},
        {"ddaay", "đây"},
        {"thuws", "thứ"},
        {"quas", "quá"},
        {"DD", "Đ"},
        {"AW", "Ă"},
    };

    for (const auto &c : cases) {
        Typer t;
        t.type(c.keys);
        expectEq(c.keys, t.screen, c.want);
    }
}

void testWordBreak() {
    std::printf("[Ngan tu / xoa dau bang z]\n");
    Typer t;
    t.type("as");
    t.rawKey(KEY_COMMA, Caps::None);
    t.rawKey(KEY_SPACE, Caps::None);
    t.type("as");
    expectEq("as, as (khong dinh nhau)", t.screen, "á, á");

    // Trong Telex, 'z' la phim xoa dau.
    t.clear();
    t.type("asz");
    expectEq("asz (z xoa dau sac)", t.screen, "a");

    t.clear();
    t.type("vieejtz");
    expectEq("vieejtz (z xoa nang)", t.screen, "viêt");

    // ESC cung cat tu
    t.clear();
    t.type("as");
    t.rawKey(KEY_ESC, Caps::None);
    t.type("as");
    expectEq("as ESC as", t.screen, "áá");
}

void testDeleteKeepsEngineInSync() {
    std::printf("[BackSpace giu engine dung nhip]\n");
    Typer t;
    t.type("vieejt");
    expectEq("vieejt", t.screen, "việt");

    t.rawKey(KEY_DELETE, Caps::None); // xoa 't'
    expectEq("vieejt + BS", t.screen, "việ");

    t.type("t"); // go lai 't' -> phai thanh 'việt' chu khong phai 'việtt'
    expectEq("vieejt + BS + t", t.screen, "việt");

    // Go tu moi sau khi xoa het
    t.clear();
    t.type("as");
    t.rawKey(KEY_DELETE, Caps::None);
    t.rawKey(KEY_DELETE, Caps::None);
    expectEq("as + BS BS", t.screen, "");
    t.type("as");
    expectEq("go lai tu dau", t.screen, "á");
}

void testCaps() {
    std::printf("[Viet hoa]\n");
    Typer t;
    t.type("As");
    expectEq("As (shift)", t.screen, "Á");

    t.clear();
    t.type("aS");
    expectEq("aS (shift dau)", t.screen, "á");

    // CapsLock: frontend bao qua KeyState::CapsLock
    t.clear();
    t.rawKey(KEY_A, Caps::CapsLock);
    t.rawKey(KEY_S, Caps::CapsLock);
    expectEq("caps lock + as", t.screen, "Á");

    // CapsLock + Shift => chu thuong
    t.clear();
    t.rawKey(KEY_A, Caps::None); // frontend da ap caps+shift => 'a'
    t.rawKey(KEY_S, Caps::None);
    expectEq("caps lock + shift + as", t.screen, "á");
}

void testCapsFromKeyState() {
    std::printf("[Suy ra caps tu keysym + modifier]\n");
    struct Case {
        const char *what;
        uint32_t sym;
        bool shift;
        bool caps;
        Caps want;
    };
    static const Case cases[] = {
        // Frontend DA ap modifier vao keysym (truong hop binh thuong)
        {"'a', khong gi", 'a', false, false, Caps::None},
        {"'A' (shift)", 'A', true, false, Caps::Shift},
        {"'A' (caps lock)", 'A', false, true, Caps::CapsLock},
        {"'a' (caps lock + shift)", 'a', true, true, Caps::None},
        // Frontend KHONG ap modifier vao keysym
        {"'a' + shift (frontend khong ap)", 'a', true, false, Caps::Shift},
        {"'a' + caps lock (khong ap)", 'a', false, true, Caps::CapsLock},
        // Khong phai chu cai
        {"'1' + shift", '1', true, false, Caps::Shift},
        {"'1'", '1', false, false, Caps::None},
    };
    for (const auto &c : cases) {
        const Caps got = openkey::capsFromKeyState(c.sym, c.shift, c.caps);
        if (got == c.want) {
            gPassed++;
            std::printf("  ok   %-34s -> %d\n", c.what, (int)got);
        } else {
            gFailed++;
            std::printf("  FAIL %-34s -> got %d, want %d\n", c.what, (int)got,
                        (int)c.want);
        }
    }
}

void testRestoreWrongSpelling() {
    std::printf("[Tra lai khi go sai chinh ta]\n");
    // 'z' khong hop le trong tu tieng Viet: engine tra lai dung nhung gi da go.
    Typer t;
    t.type("az");
    expectEq("az", t.screen, "az");
}

void testVni() {
    std::printf("[VNI]\n");
    openkey::Settings s;
    s.inputType = 1;
    openkey::applySettings(s);

    Typer t;
    t.type("a1");
    expectEq("a1", t.screen, "á");
    t.clear();
    t.type("a2");
    expectEq("a2", t.screen, "à");
    t.clear();
    t.type("d9");
    expectEq("d9", t.screen, "đ");
    t.clear();
    t.type("tie6ng1");
    expectEq("tie6ng1", t.screen, "tiếng");

    openkey::applySettings(openkey::Settings{});
}

void testCodeTables() {
    std::printf("[Bang ma - luon commit ra UTF-8]\n");
    openkey::Settings s;
    s.codeTable = 1; // TCVN3
    openkey::applySettings(s);
    Typer t;
    t.type("as");
    expectEq("TCVN3: as", t.screen, "á");
    t.clear();
    t.type("vieejt");
    expectEq("TCVN3: vieejt", t.screen, "việt");

    s.codeTable = 2; // VNI Windows
    openkey::applySettings(s);
    t.clear();
    t.type("ddaay");
    expectEq("VNI-Win: ddaay", t.screen, "đây");

    openkey::applySettings(openkey::Settings{});
}

void testMacro() {
    std::printf("[Macro]\n");
    openkey::Settings s;
    s.useMacro = 1;
    openkey::applySettings(s);

    addMacro("btw", "by the way");
    addMacro("vn", "Việt Nam");

    Typer t;
    // '.' la macro break code: noi dung thay the + chinh dau '.' di tiep.
    t.type("btw.");
    expectEq("btw.", t.screen, "by the way.");

    t.clear();
    t.type("vn ");
    expectEq("vn + space", t.screen, "Việt Nam ");

    t.clear();
    t.type("btw,");
    expectEq("btw,", t.screen, "by the way,");

    t.clear();
    t.type("xyz.");
    expectEq("xyz. (khong co macro)", t.screen, "xyz.");

    // Doc tu file dinh dang UniKey
    const char *path = "/tmp/openkey_test_macro.txt";
    {
        std::FILE *f = std::fopen(path, "w");
        if (f) {
            std::fputs(";Compatible OpenKey Macro Data file for UniKey*** "
                       "version=1 ***\n",
                       f);
            std::fputs("ko:không\n", f);
            std::fputs("ok:ổn\n", f);
            std::fclose(f);
        }
    }
    readFromFile(path, false);
    t.clear();
    t.type("ko.");
    expectEq("macro tu file", t.screen, "không.");
    std::remove(path);

    deleteMacro("btw");
    deleteMacro("vn");
    deleteMacro("ko");
    deleteMacro("ok");
    openkey::applySettings(openkey::Settings{});
}

void testEnglishMode() {
    std::printf("[Che do tieng Anh]\n");
    openkey::Settings s;
    s.language = 0;
    openkey::applySettings(s);

    Typer t;
    t.type("vieejt");
    expectEq("language=0: khong go Viet", t.screen, "vieejt");

    // English + macro in english mode. O che do tieng Anh, engine kich hoat
    // macro bang SPACE (xem vEnglishMode), khong phai dau cau.
    s.useMacro = 1;
    s.useMacroInEnglishMode = 1;
    openkey::applySettings(s);
    addMacro("btw", "by the way");
    t.clear();
    t.type("btw ");
    expectEq("language=0 + macro", t.screen, "by the way ");
    deleteMacro("btw");

    openkey::applySettings(openkey::Settings{});
}

void testQuickTelex() {
    std::printf("[Go nhanh (quick telex)]\n");
    openkey::Settings s;
    s.quickTelex = 1;
    openkey::applySettings(s);

    Typer t;
    t.type("cc");
    expectEq("quickTelex: cc", t.screen, "ch");
    t.clear();
    t.type("gg");
    expectEq("quickTelex: gg", t.screen, "gi");
    t.clear();
    t.type("nn");
    expectEq("quickTelex: nn", t.screen, "ng");
    t.clear();
    t.type("tieengs");
    expectEq("quickTelex: tieengs", t.screen, "tiếng");

    openkey::applySettings(openkey::Settings{});
}

void testScreenWord() {
    std::printf("[Theo doi chu dang hien tren man hinh]\n");
    // screenWord phai khop voi nhung gi client hien thi, ke ca khi
    // client tu chen ky tu (passThrough) hay ta thay the (Replace).
    struct Case {
        const char *keys;
        const char *wantWord;   // Core::screenWord() — chu cua TU hien tai
        const char *wantScreen; // van ban client that su hien
    };
    static const Case cases[] = {
        {"a", "a", "a"},
        {"as", "á", "á"}, // 's' thay 'a' bang 'á'
        {"vieejt", "việt", "việt"},
        {"ddaay", "đây", "đây"},
        {"as ", "", "á "},  // space cat tu
        {"as,", "", "á,"},  // dau phay cat tu
        {"asz", "a", "a"},  // 'z' xoa dau
    };
    for (const auto &c : cases) {
        Typer t;
        t.type(c.keys);
        expectEq(std::string(c.keys) + " [screenWord]", t.core.screenWord(),
                 c.wantWord);
        expectEq(std::string(c.keys) + " [client]", t.screen, c.wantScreen);
    }

    // BackSpace: client xoa 1 ky tu, screenWord phai theo kip
    Typer t;
    t.type("vieejt");
    t.rawKey(KEY_DELETE, Caps::None);
    expectEq("vieejt + BS", t.core.screenWord(), "việ");

    // reset() phai xoa sach
    t.core.reset();
    expectEq("sau reset()", t.core.screenWord(), "");
}

void testSurroundEndsWith() {
    std::printf("[Doi chieu surrounding text]\n");
    struct Case {
        const char *what;
        const char *text;
        unsigned cursor;
        const char *suffix;
        bool want;
    };
    static const Case cases[] = {
        {"khop o cuoi", "hello việt", 10, "việt", true},
        {"khop giua chuoi", "việt hay", 4, "việt", true},
        {"lech", "hello", 5, "việt", false},
        {"con tro o dau", "hello", 0, "a", false},
        {"suffix rong", "hello", 5, "", true},
        {"suffix dai hon con tro", "ab", 1, "ab", false},
        {"con tro vuot chuoi", "ab", 9, "b", false},
        {"chuoi rong", "", 0, "", true},
        {"chuoi rong + suffix", "", 0, "a", false},
        {"ASCII", "abc", 3, "bc", true},
        {"ASCII lech", "abc", 3, "bd", false},
        {"tieng Viet nhieu dau", "nước", 4, "ước", true},
    };
    for (const auto &c : cases) {
        const bool got =
            openkey::surroundEndsWith(c.text, c.cursor, c.suffix);
        if (got == c.want) {
            gPassed++;
            std::printf("  ok   %-26s -> %d\n", c.what, (int)got);
        } else {
            gFailed++;
            std::printf("  FAIL %-26s -> got %d, want %d\n", c.what, (int)got,
                        (int)c.want);
        }
    }
}

void testReplaceSane() {
    std::printf("[Chot an toan: khong xoa qua so ky tu dang co]\n");
    struct Case {
        const char *what;
        int backspace;
        const char *screenWord;
        bool want;
    };
    static const Case cases[] = {
        {"xoa 0", 0, "", true},
        {"xoa 0 khi co chu", 0, "việt", true},
        {"xoa 1 / co 1", 1, "a", true},
        {"xoa 1 / co 3", 1, "viê", true},
        {"xoa 3 / co 3", 3, "viê", true},
        {"xoa 4 / co 1 => lech", 4, "o", false},
        {"xoa 4 / co 0 => lech", 4, "", false},
        {"xoa 2 / co 1 => lech", 2, "a", false},
        {"am", -1, "a", false},
    };
    for (const auto &c : cases) {
        const bool got = openkey::isReplaceSane(c.backspace, c.screenWord);
        if (got == c.want) {
            gPassed++;
            std::printf("  ok   %-24s -> %d\n", c.what, (int)got);
        } else {
            gFailed++;
            std::printf("  FAIL %-24s -> got %d, want %d\n", c.what, (int)got,
                        (int)c.want);
        }
    }

    // Va khong duoc keu oan trong luong go binh thuong
    Typer t;
    t.type("vieejt");
    expectEq("vieejt van dung", t.screen, "việt");
    t.clear();
    t.type("nuwowsc");
    expectEq("nuwowsc van dung", t.screen, "nước");
    t.clear();
    t.type("xin chaof ");
    expectEq("cau co dau cach", t.screen, "xin chào ");
}

void testSettingsFile() {
    std::printf("[File cau hinh]\n");
    const char *path = "/tmp/openkey_test_settings.conf";
    openkey::Settings s;
    s.inputType = 1;
    s.codeTable = 2;
    s.useMacro = 1;
    s.checkSpelling = 0;
    s.debugLog = 1;
    if (!openkey::saveSettingsFile(path, s)) {
        gFailed++;
        std::printf("  FAIL khong ghi duoc %s\n", path);
        return;
    }
    openkey::Settings loaded;
    if (!openkey::loadSettingsFile(path, loaded)) {
        gFailed++;
        std::printf("  FAIL khong doc duoc %s\n", path);
        return;
    }
    expectEq("inputType round-trip", std::to_string(loaded.inputType),
             std::to_string(s.inputType));
    expectEq("codeTable round-trip", std::to_string(loaded.codeTable),
             std::to_string(s.codeTable));
    expectEq("useMacro round-trip", std::to_string(loaded.useMacro),
             std::to_string(s.useMacro));
    expectEq("checkSpelling round-trip", std::to_string(loaded.checkSpelling),
             std::to_string(s.checkSpelling));
    expectEq("debugLog round-trip", std::to_string(loaded.debugLog),
             std::to_string(s.debugLog));

    // Gia tri mac dinh khong bi file lam hong
    expectEq("language giu mac dinh", std::to_string(loaded.language),
             std::to_string(openkey::Settings{}.language));
    std::remove(path);
}

} // namespace

int main() {
    std::printf("fcitx5-openkey core tests\n\n");
    testTelex();
    testWordBreak();
    testDeleteKeepsEngineInSync();
    testCaps();
    testCapsFromKeyState();
    testRestoreWrongSpelling();
    testVni();
    testCodeTables();
    testMacro();
    testEnglishMode();
    testQuickTelex();
    testScreenWord();
    testSurroundEndsWith();
    testReplaceSane();
    testSettingsFile();

    std::printf("\n%d passed, %d failed\n", gPassed, gFailed);
    return gFailed == 0 ? 0 : 1;
}
