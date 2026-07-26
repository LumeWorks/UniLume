// SPDX-License-Identifier: GPL-2.0-or-later

#include "backup_codec.h"
#include "generation_store.h"
#include "input_method_config.h"
#include "settings_model.h"

#include "application_policy.h"
#include "dictionary_store.h"
#include "keymap_contract.h"
#include "macro_store.h"

#include <fcitx-config/rawconfig.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <unistd.h>

namespace {

int failures = 0;

void expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL " << message << '\n';
        ++failures;
    }
}

std::filesystem::path temporaryDirectory()
{
    std::string pattern =
        (std::filesystem::temp_directory_path() /
         "unilume-config-gui-tests-XXXXXX")
            .string();
    if (char *path = mkdtemp(pattern.data())) {
        return path;
    }
    return {};
}

std::string asVersionZero(std::string current)
{
    const std::string version_one = "unilume_backup_version=1\n";
    current.replace(0, version_one.size(),
                    "unilume_backup_version=0\n");
    return current;
}

} // namespace

int main()
{
    using namespace unilume::config_gui;

    Settings settings = defaultSettings();
    const ValidationResult default_validation = validate(settings);
    for (const ValidationError &error : default_validation.errors) {
        std::cerr << "DEFAULT " << error.field << ": "
                  << error.message << '\n';
    }
    expect(default_validation.ok(), "default settings validate");
    expect(settings.values.size() == allConfigKeys().size(),
           "every production option has a model value");

    std::set<std::string_view> described;
    for (const FieldDescriptor &descriptor : fieldDescriptors()) {
        expect(described.emplace(descriptor.key).second,
               "field descriptors are unique");
    }
    expect(described.size() == allConfigKeys().size(),
           "every production option has a descriptor");
    unilume::fcitx5::InputMethodConfig production_configuration;
    fcitx::RawConfig production_raw;
    production_configuration.save(production_raw);
    std::set<std::string> production_keys;
    for (const std::string &key : production_raw.subItems()) {
        production_keys.emplace(key);
    }
    std::set<std::string> descriptor_keys;
    for (const std::string_view key : described) {
        descriptor_keys.emplace(key);
    }
    expect(production_keys == descriptor_keys,
           "descriptors exactly match independently serialized "
           "production options");

    settings.values["InputMethod"] = "unsupported";
    ValidationResult invalid = validate(settings);
    expect(!invalid.ok() &&
               invalid.errors.front().field == "InputMethod",
           "field validation reports the exact invalid option");
    settings = defaultSettings();
    settings.values["EmojiHotkey"] =
        settings.values["CycleModeHotkey"];
    invalid = validate(settings);
    expect(!invalid.ok(), "conflicting hotkeys are rejected");
    settings = defaultSettings();
    settings.resources.dictionary = "broken";
    invalid = validate(settings);
    expect(!invalid.ok(), "invalid resource data cannot be saved");

    settings = defaultSettings();
    settings.values["MacroFile"] = "/machine-specific/macros.conf";
    settings.values["DictionaryFile"] =
        "/machine-specific/dictionary.conf";
    settings.values["KeymapFile"] = "/machine-specific/keymap.conf";
    settings.values["ApplicationPolicyFile"] =
        "/machine-specific/applications.conf";
    const std::string backup = encodeBackup(settings);
    expect(!backup.empty(), "valid backup encodes");
    const BackupDecodeResult decoded = decodeBackup(backup);
    expect(decoded.ok() &&
               value(decoded.settings, "MacroFile").empty() &&
               value(decoded.settings, "DictionaryFile").empty() &&
               value(decoded.settings, "KeymapFile").empty() &&
               value(decoded.settings,
                     "ApplicationPolicyFile")
                   .empty() &&
               decoded.settings.resources == settings.resources &&
               !decoded.migrated,
           "backup replaces machine paths with portable resources");
    const BackupDecodeResult migrated =
        decodeBackup(asVersionZero(backup));
    expect(migrated.ok() && migrated.migrated,
           "older backup version migrates through current defaults");
    expect(!decodeBackup("unilume_backup_version=99\nend\n").ok(),
           "future backup version is rejected");
    expect(!decodeBackup(backup.substr(0, backup.size() - 2)).ok(),
           "truncated backup is rejected");

    const std::filesystem::path directory = temporaryDirectory();
    expect(!directory.empty(), "temporary directory is available");
    GenerationStore store(directory / "generations");
    StageResult staged = store.stage(settings);
    expect(staged.ok() &&
               std::filesystem::is_directory(staged.generation),
           "validated settings stage in a private generation");
    expect(!value(staged.settings, "MacroFile").empty() &&
               value(staged.settings, "DictionaryFile").empty() &&
               !value(staged.settings, "ApplicationPolicyFile").empty(),
           "managed resources receive generation paths");
    unilume::macro::Store macros(value(staged.settings, "MacroFile"));
    expect(macros.load().ok(),
           "staging uses real production resource stores");
    StageResult newer = store.stage(settings);
    StageResult newest = store.stage(settings);
    expect(newer.ok() && newest.ok(),
           "multiple generations can be staged");
    store.collect(staged.generation, 2);
    std::size_t generation_count = 0;
    for (const auto &entry :
         std::filesystem::directory_iterator(directory /
                                             "generations")) {
        generation_count += entry.is_directory() ? 1 : 0;
    }
    expect(generation_count == 2 &&
               std::filesystem::exists(staged.generation),
           "collection retains the active and one rollback generation");
    expect(!store.discard(directory),
           "generation cleanup refuses an unmanaged target");
    expect(store.discard(staged.generation) &&
               !std::filesystem::exists(staged.generation),
           "owned generation can be discarded");

    {
        std::ofstream obstacle(directory / "not-a-directory");
        obstacle << "x";
    }
    GenerationStore failing(directory / "not-a-directory" / "child");
    expect(!failing.stage(settings).ok(),
           "staging failure preserves the source settings");

    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    return failures == 0 ? 0 : 1;
}
