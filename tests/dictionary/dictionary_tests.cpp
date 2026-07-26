// SPDX-License-Identifier: GPL-2.0-or-later

#include "dictionary_contract.h"
#include "dictionary_store.h"
#include "engine_context.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

void require(bool condition, const char *message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
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

std::string type(unilume::core::EngineContext &engine, std::string_view keys)
{
    std::string document;
    for (const char key : keys) {
        const unilume::core::KeyResult result = engine.process(
            {unilume::core::KeyKind::text, std::string_view{&key, 1}});
        require(result.handled && !result.require_fallback,
                "engine policy processing failed");
        std::size_t position = document.size();
        for (int count = 0; count < result.delete_before_cursor; ++count) {
            require(position > 0, "engine deleted beyond document");
            position = previousCharacter(document, position);
        }
        document.erase(position);
        document.append(result.commit_text);
    }
    return document;
}

std::filesystem::path temporaryDirectory()
{
    std::array<char, 64> pattern{};
    const std::string seed = "/tmp/unilume-dictionary-test.XXXXXX";
    std::copy(seed.begin(), seed.end(), pattern.begin());
    char *created = mkdtemp(pattern.data());
    require(created != nullptr, "cannot create dictionary test directory");
    return created;
}

} // namespace

int main()
{
    try {
        using namespace unilume;
        const dictionary::DecodeResult decoded =
            dictionary::decode("unilume_dictionary_version=1\n"
                               "keep\túe\n"
                               "restore\tGitHub123\n");
        require(decoded.ok(), "valid dictionary rejected");
        require(decoded.snapshot.table->restore_words.front() == "github123",
                "restore key was not folded");
        const dictionary::DecodeResult round_trip =
            dictionary::decode(dictionary::encode(decoded.snapshot));
        require(round_trip.ok() && round_trip.snapshot == decoded.snapshot,
                "dictionary round-trip failed");
        require(dictionary::restores(decoded.snapshot, "GITHUB123"),
                "ASCII-insensitive restore lookup failed");
        require(dictionary::keeps(decoded.snapshot, "úe"),
                "exact NFC keep lookup failed");
        const dictionary::Snapshot shared_copy = decoded.snapshot;
        require(shared_copy.table == decoded.snapshot.table,
                "dictionary snapshot copy duplicated the immutable table");
        require(
            !dictionary::decode("unilume_dictionary_version=1\nkeep\tu\xcc\x81"
                                "e\n")
                 .ok(),
            "decomposed Unicode accepted");
        require(!dictionary::decode(
                     "unilume_dictionary_version=1\nrestore\tbad-word\n")
                     .ok(),
                "unreachable restore entry accepted");
        require(!dictionary::decode(
                     "unilume_dictionary_version=1\nrestore\tx\nrestore\tX\n")
                     .ok(),
                "folded duplicate accepted");

        dictionary::Snapshot policy =
            dictionary::makeSnapshot(true, {"úe"}, {"as", "vn"});

        core::EngineContext restore;
        restore.setDictionary(policy);
        require(type(restore, "as ") == "as ", "literal restore policy failed");
        core::EngineContext isolated;
        require(type(isolated, "as ") == "á ",
                "dictionary leaked into another engine context");

        core::EngineContext keep;
        keep.setDictionary(policy);
        require(type(keep, "ues ") == "úe ", "custom valid-word policy failed");

        macro::Snapshot macros;
        macros.enabled = true;
        macros.entries.push_back({"vn", "macro"});
        core::EngineContext precedence;
        precedence.setDictionary(policy);
        precedence.setMacros(macros);
        require(type(precedence, "vn ") == "macro ",
                "macro must take precedence over dictionary");

        core::EngineContext literal;
        literal.setDictionary(policy);
        require(type(literal, "http://as.example ") == "http://as.example ",
                "URL literal mode must bypass dictionary");

        const std::filesystem::path directory = temporaryDirectory();
        const std::filesystem::path path = directory / "personal.dict";
        dictionary::Store store(path);
        std::string error;
        require(store.save(policy, &error), "atomic dictionary save failed");
        struct stat saved_status{};
        require(stat(path.c_str(), &saved_status) == 0 &&
                    (saved_status.st_mode & 0777) == 0600,
                "dictionary save permissions are not private");
        const dictionary::LoadResult loaded = store.load();
        require(loaded.ok() &&
                    loaded.disposition == dictionary::LoadDisposition::loaded &&
                    loaded.snapshot == policy,
                "saved dictionary did not reload");
        {
            std::ofstream corrupt(path, std::ios::binary | std::ios::trunc);
            corrupt << "corrupt\n";
        }
        const dictionary::LoadResult rejected = store.load();
        require(!rejected.ok() && store.active() == policy,
                "corrupt reload replaced last-known-good snapshot");
        const std::filesystem::path oversized = directory / "oversized.dict";
        {
            std::ofstream file(oversized, std::ios::binary);
            file.seekp(
                static_cast<std::streamoff>(dictionary::max_serialized_bytes));
            file.put('x');
        }
        dictionary::Store oversized_store(oversized);
        require(!oversized_store.load().ok(),
                "oversized dictionary was read or accepted");
        std::filesystem::remove_all(directory);
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
