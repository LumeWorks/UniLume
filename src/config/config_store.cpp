// SPDX-License-Identifier: GPL-2.0-or-later

#include "config_store.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace unilume::config {
namespace {

std::string errorText(const char *operation)
{
    return std::string(operation) + ": " + std::strerror(errno);
}

bool writeAll(int descriptor, std::string_view text, std::string &error)
{
    std::size_t written = 0;
    while (written < text.size()) {
        const ssize_t result = write(descriptor, text.data() + written, text.size() - written);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            error = errorText("write configuration");
            return false;
        }
        written += static_cast<std::size_t>(result);
    }
    return true;
}

} // namespace

Store::Store(std::filesystem::path path) : path_(std::move(path)) {}

LoadResult Store::load()
{
    std::ifstream stream(path_, std::ios::binary);
    if (!stream) {
        if (errno == ENOENT) {
            active_ = defaults();
            return {LoadDisposition::missing, active_, {}};
        }
        return {LoadDisposition::rejected, active_, errorText("read configuration")};
    }
    const std::string text((std::istreambuf_iterator<char>(stream)),
                           std::istreambuf_iterator<char>());
    if (!stream.good() && !stream.eof()) {
        return {LoadDisposition::rejected, active_, "read configuration failed"};
    }
    const DecodeResult decoded = decode(text);
    if (!decoded.ok()) {
        return {LoadDisposition::rejected, active_, decoded.error};
    }
    active_ = decoded.snapshot;
    return {decoded.migrated ? LoadDisposition::migrated : LoadDisposition::loaded,
            active_, {}};
}

bool Store::save(const Snapshot &snapshot, std::string *error) const
{
    const std::string validation = validate(snapshot);
    if (!validation.empty()) {
        if (error != nullptr) {
            *error = validation;
        }
        return false;
    }
    const std::filesystem::path parent =
        path_.has_parent_path() ? path_.parent_path() : std::filesystem::path{"."};
    std::error_code filesystem_error;
    std::filesystem::create_directories(parent, filesystem_error);
    if (filesystem_error) {
        if (error != nullptr) {
            *error = "create configuration directory: " + filesystem_error.message();
        }
        return false;
    }
    std::string template_path = path_.string() + ".tmp.XXXXXX";
    std::vector<char> temporary(template_path.begin(), template_path.end());
    temporary.push_back('\0');
    const int descriptor = mkstemp(temporary.data());
    if (descriptor < 0) {
        if (error != nullptr) {
            *error = errorText("create temporary configuration");
        }
        return false;
    }
    const std::filesystem::path temporary_path(temporary.data());
    std::string write_error;
    const std::string text = encode(snapshot);
    const bool written = fchmod(descriptor, S_IRUSR | S_IWUSR) == 0 &&
                         writeAll(descriptor, text, write_error) && fsync(descriptor) == 0;
    if (!written && write_error.empty()) {
        write_error = errorText("sync configuration");
    }
    const int close_result = close(descriptor);
    if (!written || close_result != 0) {
        std::filesystem::remove(temporary_path, filesystem_error);
        if (error != nullptr) {
            *error = write_error.empty() ? errorText("close configuration") : write_error;
        }
        return false;
    }
    if (rename(temporary_path.c_str(), path_.c_str()) != 0) {
        const std::string rename_error = errorText("replace configuration");
        std::filesystem::remove(temporary_path, filesystem_error);
        if (error != nullptr) {
            *error = rename_error;
        }
        return false;
    }
    const int directory = open(parent.c_str(), O_RDONLY | O_DIRECTORY);
    if (directory >= 0) {
        const int sync_result = fsync(directory);
        close(directory);
        if (sync_result != 0) {
            if (error != nullptr) {
                *error = errorText("sync configuration directory");
            }
            return false;
        }
    }
    return true;
}

bool Store::reset(std::string *error)
{
    const Snapshot reset_snapshot = defaults();
    if (!save(reset_snapshot, error)) {
        return false;
    }
    active_ = reset_snapshot;
    return true;
}

} // namespace unilume::config
