// SPDX-License-Identifier: GPL-2.0-or-later

#include "input_method_config.h"

#include <fcitx-config/rawconfig.h>

#include <iostream>

namespace {

bool expect(bool condition, const char *message)
{
    if (condition) {
        return true;
    }
    std::cerr << message << '\n';
    return false;
}

} // namespace

int main()
{
    using namespace unilume::fcitx5;

    bool ok = true;
    InputMethodConfig config;
    fcitx::RawConfig raw;
    raw["InputMethod"] = "VNI";
    raw["OutputCharset"] = "UTF8";
    ok &= expect(loadInputMethodConfig(config, raw),
                 "valid Fcitx configuration must load");
    ok &= expect(*config.input_method == ConfigInputMethod::VNI,
                 "Fcitx config must apply VNI");
    ok &= expect(*config.output_charset == ConfigOutputCharset::UTF8,
                 "Fcitx config must retain UTF8");
    ok &= expect(toUlInputMethod(*config.input_method) == UL_INPUT_METHOD_VNI,
                 "VNI must map to the UniLume context API");

    fcitx::RawConfig invalid_method;
    invalid_method["InputMethod"] = "Unknown";
    ok &= expect(!loadInputMethodConfig(config, invalid_method),
                 "unknown input method must be rejected");
    ok &= expect(*config.input_method == ConfigInputMethod::VNI,
                 "unknown input method must not replace active configuration");

    fcitx::RawConfig invalid_charset;
    invalid_charset["OutputCharset"] = "TCVN3";
    ok &= expect(!loadInputMethodConfig(config, invalid_charset),
                 "unsupported charset must be rejected");
    ok &= expect(*config.output_charset == ConfigOutputCharset::UTF8,
                 "unsupported charset must not replace UTF8");

    fcitx::RawConfig description;
    config.dumpDescription(description);
    ok &= expect(description.hasSubItems(),
                 "config metadata must contain declared options");

    return ok ? 0 : 1;
}
