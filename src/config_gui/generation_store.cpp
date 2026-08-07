// SPDX-License-Identifier: GPL-2.0-or-later

#include "generation_store.h"

#include "application_policy.h"
#include "dictionary_contract.h"
#include "dictionary_store.h"
#include "keymap_contract.h"
#include "macro_contract.h"
#include "macro_store.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace unilume::config_gui {
namespace {

std::string systemError(const char *operation)
{
    return std::string(operation) + ": " + std::strerror(errno);
}

bool writeAll(int descriptor, std::string_view text, std::string &error)
{
    std::size_t written = 0;
    while (written < text.size()) {
        const ssize_t result =
            write(descriptor, text.data() + written,
                  text.size() - written);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            error = systemError("write managed configuration resource");
            return false;
        }
        written += static_cast<std::size_t>(result);
    }
    return true;
}

bool atomicWrite(const std::filesystem::path &path,
                 std::string_view text,
                 std::string &error)
{
    std::string pattern = path.string() + ".tmp.XXXXXX";
    std::vector<char> temporary(pattern.begin(), pattern.end());
    temporary.push_back('\0');
    const int descriptor = mkstemp(temporary.data());
    if (descriptor < 0) {
        error = systemError("create managed configuration resource");
        return false;
    }
    const std::filesystem::path temporary_path(temporary.data());
    const bool written =
        fchmod(descriptor, S_IRUSR | S_IWUSR) == 0 &&
        writeAll(descriptor, text, error) &&
        fsync(descriptor) == 0;
    if (!written && error.empty()) {
        error = systemError("sync managed configuration resource");
    }
    const int close_result = close(descriptor);
    if (!written || close_result != 0) {
        std::error_code ignored;
        std::filesystem::remove(temporary_path, ignored);
        if (error.empty()) {
            error = systemError("close managed configuration resource");
        }
        return false;
    }
    if (rename(temporary_path.c_str(), path.c_str()) != 0) {
        error = systemError("replace managed configuration resource");
        std::error_code ignored;
        std::filesystem::remove(temporary_path, ignored);
        return false;
    }
    return true;
}

bool syncDirectory(const std::filesystem::path &path,
                   std::string &error)
{
    const int descriptor =
        open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (descriptor < 0) {
        error = systemError("open configuration generation");
        return false;
    }
    const bool synced = fsync(descriptor) == 0;
    if (!synced) {
        error = systemError("sync configuration generation");
    }
    if (close(descriptor) != 0 && synced) {
        error = systemError("close configuration generation");
        return false;
    }
    return synced;
}

std::filesystem::path createGeneration(
    const std::filesystem::path &root,
    std::string &error)
{
    std::error_code filesystem_error;
    std::filesystem::create_directories(root, filesystem_error);
    if (filesystem_error) {
        error = "create configuration generation directory: " +
                filesystem_error.message();
        return {};
    }
    chmod(root.c_str(), S_IRWXU);
    std::string pattern = (root / "generation-XXXXXX").string();
    if (char *directory = mkdtemp(pattern.data())) {
        return directory;
    }
    error = systemError("create configuration generation");
    return {};
}

} // namespace

GenerationStore::GenerationStore(std::filesystem::path root)
    : root_(std::move(root))
{
}

StageResult GenerationStore::stage(const Settings &source) const
{
    const ValidationResult validation = validate(source);
    if (!validation.ok()) {
        return {.error = validation.errors.front().field + ": " +
                         validation.errors.front().message};
    }

    StageResult result;
    result.settings = source;
    result.generation = createGeneration(root_, result.error);
    if (result.generation.empty()) {
        return result;
    }

    const auto fail = [&result, this](std::string error) {
        result.error = std::move(error);
        std::string ignored;
        const bool removed = discard(result.generation, &ignored);
        (void)removed;
        result.generation.clear();
    };

    if (!source.resources.macros.empty()) {
        macro::DecodeResult decoded =
            macro::decode(source.resources.macros);
        const std::filesystem::path path =
            result.generation / "macros.conf";
        macro::Store store(path);
        std::string error;
        if (!decoded.ok() || !store.save(decoded.snapshot, &error)) {
            fail(decoded.ok() ? std::move(error) : decoded.error);
            return result;
        }
        result.settings.values["MacroFile"] = path.string();
    } else {
        result.settings.values["MacroFile"].clear();
    }

    if (!source.resources.dictionary.empty()) {
        dictionary::DecodeResult decoded =
            dictionary::decode(source.resources.dictionary);
        const std::filesystem::path path =
            result.generation / "dictionary.conf";
        dictionary::Store store(path);
        std::string error;
        if (!decoded.ok() || !store.save(decoded.snapshot, &error)) {
            fail(decoded.ok() ? std::move(error) : decoded.error);
            return result;
        }
        result.settings.values["DictionaryFile"] = path.string();
    } else {
        result.settings.values["DictionaryFile"].clear();
    }

    if (!source.resources.keymap.empty()) {
        const keymap::DecodeResult decoded =
            keymap::decode(source.resources.keymap);
        const std::filesystem::path path =
            result.generation / "keymap.conf";
        std::string error;
        if (!decoded.ok() ||
            !atomicWrite(path, keymap::encode(decoded.snapshot), error)) {
            fail(decoded.ok() ? std::move(error) : decoded.error);
            return result;
        }
        result.settings.values["KeymapFile"] = path.string();
    } else {
        result.settings.values["KeymapFile"].clear();
    }

    if (!source.resources.application_policy.empty()) {
        const policy::DecodeResult decoded =
            policy::decode(source.resources.application_policy);
        const std::filesystem::path path =
            result.generation / "applications.conf";
        std::string error;
        if (!decoded.ok()) {
            fail(decoded.error);
            return result;
        }
        // Issue #127 migration: the first time a legacy policy (using
        // automatic/direct/safe-preedit modes) is rewritten to the new
        // adaptive/off vocabulary, back up the existing file so the user
        // can recover the original rules if the migration misbehaves.
        if (decoded.legacy_modes) {
            std::error_code ec;
            if (std::filesystem::exists(path, ec) && !ec) {
                const std::filesystem::path backup =
                    path.string() + ".legacy.bak";
                std::filesystem::copy_file(
                    path, backup,
                    std::filesystem::copy_options::overwrite_existing, ec);
                if (ec) {
                    fail("backup legacy application policy: " + ec.message());
                    return result;
                }
            }
        }
        if (!atomicWrite(path, policy::encode(decoded.snapshot), error)) {
            fail(std::move(error));
            return result;
        }
        result.settings.values["ApplicationPolicyFile"] = path.string();
    } else {
        result.settings.values["ApplicationPolicyFile"].clear();
    }

    const ValidationResult staged_validation = validate(result.settings);
    if (!staged_validation.ok()) {
        fail(staged_validation.errors.front().field + ": " +
             staged_validation.errors.front().message);
        return result;
    }
    std::string sync_error;
    if (!syncDirectory(result.generation, sync_error)) {
        fail(std::move(sync_error));
    }
    return result;
}

bool GenerationStore::discard(
    const std::filesystem::path &generation,
    std::string *error) const
{
    if (!owns(generation)) {
        if (error) {
            *error = "refusing to remove an unmanaged generation";
        }
        return false;
    }
    std::error_code filesystem_error;
    std::filesystem::remove_all(generation, filesystem_error);
    if (filesystem_error) {
        if (error) {
            *error = "remove configuration generation: " +
                     filesystem_error.message();
        }
        return false;
    }
    return true;
}

void GenerationStore::collect(
    const std::filesystem::path &active_generation,
    std::size_t keep) const
{
    keep = std::max<std::size_t>(keep, 1);
    std::error_code filesystem_error;
    std::vector<std::filesystem::directory_entry> generations;
    for (std::filesystem::directory_iterator iterator(root_,
                                                       filesystem_error);
         !filesystem_error &&
         iterator != std::filesystem::directory_iterator();
         iterator.increment(filesystem_error)) {
        if (iterator->is_directory() &&
            iterator->path().filename().string().starts_with(
                "generation-")) {
            generations.push_back(*iterator);
        }
    }
    std::sort(generations.begin(), generations.end(),
              [](const auto &left, const auto &right) {
                  return left.last_write_time() >
                         right.last_write_time();
              });
    std::vector<std::filesystem::path> retained;
    if (owns(active_generation)) {
        retained.push_back(active_generation);
    }
    for (const auto &generation : generations) {
        if (std::find(retained.begin(), retained.end(),
                      generation.path()) != retained.end()) {
            continue;
        }
        if (retained.size() < keep) {
            retained.push_back(generation.path());
            continue;
        }
        std::string ignored;
        const bool removed = discard(generation.path(), &ignored);
        (void)removed;
    }
}

bool GenerationStore::owns(
    const std::filesystem::path &generation) const
{
    if (generation.empty() ||
        generation.parent_path().lexically_normal() !=
            root_.lexically_normal()) {
        return false;
    }
    return generation.filename().string().starts_with("generation-");
}

} // namespace unilume::config_gui
