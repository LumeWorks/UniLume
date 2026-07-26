// SPDX-License-Identifier: GPL-2.0-or-later

#include "config_snapshot.h"
#include "config_store.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {

int failures = 0;

void expect(const char *name, bool value)
{
    if (!value) {
        std::cerr << "FAIL " << name << '\n';
        ++failures;
    }
}

std::filesystem::path temporaryDirectory()
{
    std::string pattern = (std::filesystem::temp_directory_path() / "unilume-config-XXXXXX").string();
    char *directory = mkdtemp(pattern.data());
    if (directory == nullptr) {
        std::cerr << "could not create temporary directory\n";
        std::exit(EXIT_FAILURE);
    }
    return directory;
}

std::string fixture(std::string_view name)
{
    std::ifstream stream(std::filesystem::path(UNILUME_CONFIG_FIXTURE_DIR) /
                         std::string(name));
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

} // namespace

int main()
{
    using namespace unilume::config;

    Snapshot expected = defaults();
    expected.input_method = InputMethod::viqr;
    expected.spell_check = false;
    expected.modern_tone = true;
    expected.macro_enabled = true;
    const std::string encoded = encode(expected);
    const DecodeResult round_trip = decode(encoded);
    expect("round-trip decode", round_trip.ok());
    expect("round-trip snapshot", round_trip.snapshot == expected);

    const DecodeResult migration = decode(fixture("v0.cfg"));
    expect("v0 migration accepted", migration.ok() && migration.migrated);
    expect("v0 migration version", migration.snapshot.version == schema_version);
    expect("v0 migration input method", migration.snapshot.input_method == InputMethod::vni);
    const DecodeResult v1 = decode(fixture("v1.cfg"));
    expect("v1 fixture accepted", v1.ok() && !v1.migrated);
    expect("v1 fixture contents", v1.snapshot.input_method == InputMethod::viqr &&
                                    v1.snapshot.macro_enabled);

    expect("reject unknown key", !decode("schema_version=1\nunknown=true\n").ok());
    expect("reject duplicate key", !decode("schema_version=1\ninput_method=telex\ninput_method=vni\n").ok());
    expect("reject future version", !decode("schema_version=2\n").ok());
    expect("reject invalid bool", !decode("schema_version=1\nspell_check=yes\n").ok());
    expect("reject partial config", !decode("schema_version=1\ninput_method=telex\n").ok());
    expect("reject corrupt fixture", !decode(fixture("corrupt-unknown.cfg")).ok());

    const std::filesystem::path directory = temporaryDirectory();
    const std::filesystem::path config_path = directory / "config";
    Store store(config_path);
    expect("missing config defaults", store.load().disposition == LoadDisposition::missing);
    std::string error;
    expect("atomic save", store.save(expected, &error));
    Store reloaded(config_path);
    const LoadResult loaded = reloaded.load();
    expect("load saved config", loaded.disposition == LoadDisposition::loaded);
    expect("saved snapshot", loaded.snapshot == expected);

    std::ofstream(config_path.string() + ".tmp.interrupted") << "schema_version=2\n";
    Store after_interruption(config_path);
    expect("interrupted temporary file ignored",
           after_interruption.load().snapshot == expected);

    std::ofstream(config_path, std::ios::trunc) << "schema_version=1\nunknown=true\n";
    const Snapshot before_rejected_reload = after_interruption.active();
    const LoadResult rejected = after_interruption.load();
    expect("corrupt config rejected", rejected.disposition == LoadDisposition::rejected);
    expect("corrupt config keeps active snapshot",
           after_interruption.active() == before_rejected_reload);
    expect("reset to defaults", after_interruption.reset(&error));
    Store reset_store(config_path);
    expect("reset persisted defaults", reset_store.load().snapshot == defaults());

    std::filesystem::remove_all(directory);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
