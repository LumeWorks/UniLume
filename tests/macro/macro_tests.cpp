// SPDX-License-Identifier: GPL-2.0-or-later

#include "macro_contract.h"
#include "macro_store.h"
#include "unilume_context.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

int failures = 0;

void expect(const char *name, bool condition)
{
    if (!condition) {
        std::cerr << "FAIL " << name << '\n';
        ++failures;
    }
}

std::filesystem::path temporaryDirectory()
{
    std::string pattern =
        (std::filesystem::temp_directory_path() / "unilume-macro-XXXXXX").string();
    char *directory = mkdtemp(pattern.data());
    if (!directory) {
        std::exit(EXIT_FAILURE);
    }
    return directory;
}

std::size_t previousCharacter(std::string_view text, std::size_t position)
{
    --position;
    while (position > 0 &&
           (static_cast<unsigned char>(text[position]) & 0xc0) == 0x80) {
        --position;
    }
    return position;
}

std::string compose(const std::vector<unilume::macro::Entry> &macros,
                    std::string_view keys)
{
    UlEngineContext *context{};
    if (ul_engine_create(UL_INPUT_METHOD_TELEX, &context) != UL_STATUS_OK) {
        return {};
    }
    std::vector<UlMacroEntry> entries;
    for (const auto &entry : macros) {
        entries.push_back(
            {entry.key.data(), entry.key.size(),
             entry.text.data(), entry.text.size()});
    }
    const UlMacroOptions options{
        1, UL_MACRO_TRIGGER_WORD_BOUNDARY, UL_MACRO_CAPITALIZATION_EXACT};
    if (ul_engine_set_macros(
            context, entries.data(), entries.size(), &options) != UL_STATUS_OK) {
        ul_engine_destroy(context);
        return {};
    }
    std::string output;
    char buffer[4096];
    for (const char key : keys) {
        UlEngineEdit edit{};
        const UlStatus status = ul_engine_process_ascii(
            context,
            static_cast<unsigned char>(key),
            0,
            0,
            buffer,
            sizeof(buffer),
            &edit);
        if (status != UL_STATUS_OK) {
            output.clear();
            break;
        }
        if (!edit.handled) {
            output += key;
            continue;
        }
        for (int count = 0;
             count < edit.delete_before_cursor && !output.empty();
             ++count) {
            output.erase(previousCharacter(output, output.size()));
        }
        output.append(buffer, edit.output_size);
    }
    ul_engine_destroy(context);
    return output;
}

} // namespace

int main()
{
    using namespace unilume::macro;

    Snapshot snapshot;
    snapshot.enabled = true;
    snapshot.entries = {
        {"brb", "be right back"},
        {"VN", "Việt Nam"},
        {"tab", "cột\tmới"},
        {"line", "một\nhai"},
    };
    const std::string encoded = encode(snapshot);
    const DecodeResult decoded = decode(encoded);
    expect("canonical round trip", decoded.ok() && decoded.snapshot == snapshot);
    expect("canonical entry order preserved",
           encoded.find("brb\t") < encoded.find("VN\t"));

    const DecodeResult legacy_utf8 = decode(
        "DO NOT DELETE THIS LINE*** version=1 ***\n"
        "vn:Việt Nam\n");
    expect("legacy UTF-8 migration",
           legacy_utf8.ok() && legacy_utf8.migrated &&
               legacy_utf8.snapshot.entries[0].text == "Việt Nam");
    const DecodeResult legacy_viqr = decode("vn:Vie^.t Nam\n");
    expect("legacy VIQR migration",
           legacy_viqr.ok() && legacy_viqr.migrated &&
               legacy_viqr.snapshot.entries[0].text == "Việt Nam");

    Snapshot duplicate;
    duplicate.entries = {{"x", "one"}, {"x", "two"}};
    expect("duplicate rejected", !validate(duplicate).empty());
    expect("malformed escape rejected",
           !decode("unilume_macro_version=1\nx\tbad\\q\n").ok());
    expect("invalid UTF-8 rejected",
           !decode("unilume_macro_version=1\nx\t\xff\n").ok());
    Snapshot oversized;
    for (int index = 0; index < 33; ++index) {
        oversized.entries.push_back(
            {std::to_string(index), std::string(1023, 'x')});
    }
    expect("aggregate engine storage limit enforced",
           !validate(oversized).empty());

    const std::filesystem::path directory = temporaryDirectory();
    const std::filesystem::path path = directory / "macros";
    Store store(path);
    std::string error;
    expect("atomic macro save", store.save(snapshot, &error));
    struct stat saved_status {};
    expect("macro save uses private permissions",
           stat(path.c_str(), &saved_status) == 0 &&
               (saved_status.st_mode & 0777) == 0600);
    Store loaded_store(path);
    const LoadResult loaded = loaded_store.load();
    expect("saved macro load",
           loaded.disposition == LoadDisposition::loaded &&
               loaded.snapshot == snapshot);
    std::ofstream(path, std::ios::trunc)
        << "unilume_macro_version=1\nbroken\n";
    const Snapshot before = loaded_store.active();
    expect("corrupt macro rejected",
           loaded_store.load().disposition == LoadDisposition::rejected);
    expect("corrupt macro preserves active", loaded_store.active() == before);
    const std::filesystem::path oversized_path = directory / "oversized";
    {
        std::ofstream oversized_file(oversized_path, std::ios::binary);
        oversized_file.seekp(static_cast<std::streamoff>(max_serialized_bytes));
        oversized_file.put('x');
    }
    Store oversized_store(oversized_path);
    expect("oversized file rejected before parsing",
           oversized_store.load().disposition == LoadDisposition::rejected);

    expect("lowercase expansion",
           compose({{"brb", "be right back"}}, "brb ") == "be right back ");
    expect("Unicode expansion",
           compose({{"vn", "Việt Nam"}}, "vn!") == "Việt Nam!");
    expect("capitalization is exact",
           compose({{"vn", "lower"}, {"VN", "UPPER"}}, "VN ") == "UPPER ");
    expect("macro output is not recursively expanded",
           compose({{"a", "b"}, {"b", "loop"}}, "a ") == "b ");
    expect("punctuation triggers expansion",
           compose({{"sig", "xin chào"}}, "sig,") == "xin chào,");
    const std::string maximum_text(1023, 'x');
    expect("maximum replacement is not truncated",
           compose({{"abcdefghijklmno", maximum_text}}, "abcdefghijklmno ") ==
               maximum_text + " ");
    Snapshot long_key;
    long_key.entries = {{std::string(16, 'k'), "value"}};
    expect("key character limit enforced", !validate(long_key).empty());

    UlEngineContext *first{};
    UlEngineContext *second{};
    expect("create isolated contexts",
           ul_engine_create(UL_INPUT_METHOD_TELEX, &first) == UL_STATUS_OK &&
               ul_engine_create(UL_INPUT_METHOD_TELEX, &second) == UL_STATUS_OK);
    const UlMacroEntry duplicated[]{{"x", 1, "one", 3},
                                    {"x", 1, "two", 3}};
    const UlMacroOptions options{
        1, UL_MACRO_TRIGGER_WORD_BOUNDARY, UL_MACRO_CAPITALIZATION_EXACT};
    expect("typed API rejects duplicate",
           ul_engine_set_macros(first, duplicated, 2, &options) ==
               UL_STATUS_DUPLICATE);
    const char invalid[] = {static_cast<char>(0xff)};
    const UlMacroEntry invalid_entry{"x", 1, invalid, 1};
    expect("typed API rejects invalid UTF-8",
           ul_engine_set_macros(first, &invalid_entry, 1, &options) ==
               UL_STATUS_INVALID_UTF8);
    expect("contexts remain independently configurable",
           ul_engine_set_macros(second, nullptr, 0, &options) == UL_STATUS_OK);
    ul_engine_destroy(first);
    ul_engine_destroy(second);

    std::filesystem::remove_all(directory);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
