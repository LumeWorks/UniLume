// SPDX-License-Identifier: GPL-2.0-or-later

#include "keymap_contract.h"
#include "unilume_context.h"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char *message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::string type(UlEngineContext *context, std::string_view keys)
{
    std::string document;
    std::array<char, 4096> output{};
    for (const unsigned char key : keys) {
        UlEngineEdit edit{};
        require(ul_engine_process_ascii(
                    context, key, 0, 0, output.data(), output.size(), &edit) ==
                    UL_STATUS_OK,
                "key processing failed");
        for (int count = 0; count < edit.delete_before_cursor; ++count) {
            require(!document.empty(), "delete exceeds document");
            std::size_t position = document.size() - 1;
            while (position > 0 &&
                   (static_cast<unsigned char>(document[position]) & 0xc0) ==
                       0x80) {
                --position;
            }
            document.erase(position);
        }
        document.append(output.data(), edit.output_size);
        if (!edit.handled && edit.output_size == 0) {
            document += static_cast<char>(key);
        }
    }
    return document;
}

} // namespace

int main()
{
    try {
        using namespace unilume::keymap;

        const DecodeResult parsed = decode(
            "; legacy-compatible map\n"
            "j = Tone1\n"
            "f = Tone2\n"
            "x = Tone3\n");
        require(parsed.ok(), "valid keymap rejected");
        const DecodeResult round_trip = decode(encode(parsed.snapshot));
        require(round_trip.ok() && round_trip.snapshot == parsed.snapshot,
                "keymap round-trip failed");

        const DecodeResult duplicate = decode("j = Tone1\nJ = Tone2\n");
        require(!duplicate.ok() && duplicate.line == 2 &&
                    duplicate.field == "key",
                "case conflict was not located");
        const DecodeResult modifier = decode("Ctrl+j = Tone1\n");
        require(!modifier.ok() && modifier.field == "key",
                "modifier conflict was not rejected");
        require(!decode("= = Tone1\n").ok(), "reserved key accepted");
        require(!decode("j = Unknown\n").ok(), "unknown action accepted");
        require(!decode("").ok(), "empty keymap accepted");

        UlEngineContext *custom{};
        UlEngineContext *builtin{};
        require(ul_engine_create(UL_INPUT_METHOD_TELEX, &custom) ==
                    UL_STATUS_OK &&
                    ul_engine_create(UL_INPUT_METHOD_TELEX, &builtin) ==
                    UL_STATUS_OK,
                "cannot create test contexts");
        const UlKeymapEntry entries[]{{'q', UL_KEYMAP_TONE1}};
        require(ul_engine_set_keymap(custom, entries, 1) == UL_STATUS_OK,
                "valid keymap activation failed");
        require(type(custom, "aq") == "á", "custom mapping output mismatch");
        require(type(builtin, "aq") == "aq",
                "custom map leaked to another context");

        const UlKeymapEntry invalid[]{{'q', UL_KEYMAP_TONE1},
                                      {'Q', UL_KEYMAP_TONE2}};
        require(ul_engine_set_keymap(custom, invalid, 2) != UL_STATUS_OK,
                "invalid keymap activated");
        ul_engine_reset(custom);
        require(type(custom, "aq") == "á",
                "invalid activation replaced last-known-good map");
        require(ul_engine_set_input_method(custom, UL_INPUT_METHOD_TELEX) ==
                    UL_STATUS_OK,
                "cannot revert to built-in map");
        require(type(custom, "as") == "á", "built-in revert failed");

        ul_engine_destroy(custom);
        ul_engine_destroy(builtin);
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
