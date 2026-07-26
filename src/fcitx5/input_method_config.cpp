// SPDX-License-Identifier: GPL-2.0-or-later

#include "input_method_config.h"

namespace unilume::fcitx5 {
namespace {

bool isOneOf(const fcitx::RawConfig &source,
             const char *path,
             std::initializer_list<const char *> values)
{
    const std::string *value = source.valueByPath(path);
    if (!value) {
        return true;
    }
    for (const char *allowed : values) {
        if (*value == allowed) {
            return true;
        }
    }
    return false;
}

} // namespace

UlInputMethod toUlInputMethod(ConfigInputMethod method)
{
    switch (method) {
    case ConfigInputMethod::Telex:
        return UL_INPUT_METHOD_TELEX;
    case ConfigInputMethod::VNI:
        return UL_INPUT_METHOD_VNI;
    case ConfigInputMethod::VIQR:
        return UL_INPUT_METHOD_VIQR;
    }
    return UL_INPUT_METHOD_TELEX;
}

bool loadInputMethodConfig(InputMethodConfig &destination,
                           const fcitx::RawConfig &source)
{
    if (!isOneOf(source, "InputMethod", {"Telex", "VNI", "VIQR"}) ||
        !isOneOf(source, "OutputCharset", {"UTF8"})) {
        return false;
    }
    // Do not reset omitted fields: Fcitx may submit a partial update from its
    // configuration UI. Validation above makes every supplied field closed.
    destination.load(source, false);
    return true;
}

} // namespace unilume::fcitx5
