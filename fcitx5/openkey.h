//
//  openkey.h
//  fcitx5-openkey
//
//  Addon fcitx5 bao quanh engine OpenKey (che do SendKey, khong preedit).
//

#ifndef OPENKEY_H
#define OPENKEY_H

#include <memory>
#include <string>

#include <fcitx/addonfactory.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>

#include "openkey_core.h"

namespace fcitx {

class OpenKeyEngine final : public InputMethodEngine {
public:
    OpenKeyEngine(Instance *instance);
    ~OpenKeyEngine() override;

    void keyEvent(const InputMethodEntry &entry, KeyEvent &keyEvent) override;
    void filterKey(const InputMethodEntry &entry, KeyEvent &keyEvent) override;
    void activate(const InputMethodEntry &entry,
                  InputContextEvent &event) override;
    void deactivate(const InputMethodEntry &entry,
                    InputContextEvent &event) override;
    void reset(const InputMethodEntry &entry,
               InputContextEvent &event) override;
    std::string subMode(const InputMethodEntry &entry,
                        InputContext &inputContext) override;

    // Doc lai ~/.config/openkey/openkey.conf (goi tu activate()).
    void reloadConfig();
    std::string configPath() const;
    std::string macroPath() const;

private:
    void loadConfig();
    // Ghi vet hoat dong ra ~/.cache/openkey/debug.log. Chi ghi khi khoa
    // `debugLog` trong openkey.conf bang 1 (mac dinh tat).
    void debug(const std::string &message);

    Instance *instance_;
    std::unique_ptr<openkey::Core> core_;
    openkey::Settings settings_;
    bool configLoaded_ = false;
    bool debugEnabled_ = false;
};

class OpenKeyFactory : public AddonFactory {
public:
    AddonInstance *create(AddonManager *manager) override {
        return new OpenKeyEngine(manager->instance());
    }
};

} // namespace fcitx

#endif // OPENKEY_H
