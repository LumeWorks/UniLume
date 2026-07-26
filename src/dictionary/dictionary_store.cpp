// SPDX-License-Identifier: GPL-2.0-or-later

#include "dictionary_store.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace unilume::dictionary {
namespace {

std::string errorText(const char *operation)
{
    return std::string(operation) + ": " + std::strerror(errno);
}

bool writeAll(int descriptor, std::string_view text, std::string &error)
{
    std::size_t written = 0;
    while (written < text.size()) {
        const ssize_t result =
            write(descriptor, text.data() + written, text.size() - written);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            error = errorText("write dictionary");
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
    std::error_code filesystem_error;
    const bool exists = std::filesystem::exists(path_, filesystem_error);
    if (filesystem_error) {
        return {LoadDisposition::rejected, active_,
                "inspect dictionary: " + filesystem_error.message()};
    }
    if (!exists) {
        return {LoadDisposition::missing, active_, {}};
    }
    const std::uintmax_t size =
        std::filesystem::file_size(path_, filesystem_error);
    if (filesystem_error || size > max_serialized_bytes) {
        return {LoadDisposition::rejected, active_,
                filesystem_error
                    ? "inspect dictionary size: " + filesystem_error.message()
                    : "dictionary exceeds size limit"};
    }
    errno = 0;
    std::ifstream stream(path_, std::ios::binary);
    if (!stream) {
        return {LoadDisposition::rejected, active_,
                errorText("read dictionary")};
    }
    std::string text(static_cast<std::size_t>(size), '\0');
    stream.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (stream.gcount() != static_cast<std::streamsize>(text.size())) {
        return {LoadDisposition::rejected, active_,
                "dictionary changed while reading"};
    }
    char extra = 0;
    if (stream.get(extra)) {
        return {LoadDisposition::rejected, active_,
                "dictionary changed while reading"};
    }
    DecodeResult decoded = decode(text);
    if (!decoded.ok()) {
        return {LoadDisposition::rejected, active_, std::move(decoded.error)};
    }
    active_ = std::move(decoded.snapshot);
    return {LoadDisposition::loaded, active_, {}};
}

bool Store::save(const Snapshot &snapshot, std::string *error) const
{
    const std::string validation = validate(snapshot);
    if (!validation.empty()) {
        if (error) {
            *error = validation;
        }
        return false;
    }
    const std::filesystem::path parent = path_.has_parent_path()
                                             ? path_.parent_path()
                                             : std::filesystem::path{"."};
    std::error_code filesystem_error;
    std::filesystem::create_directories(parent, filesystem_error);
    if (filesystem_error) {
        if (error) {
            *error =
                "create dictionary directory: " + filesystem_error.message();
        }
        return false;
    }
    std::string template_path = path_.string() + ".tmp.XXXXXX";
    std::vector<char> temporary(template_path.begin(), template_path.end());
    temporary.push_back('\0');
    const int descriptor = mkstemp(temporary.data());
    if (descriptor < 0) {
        if (error) {
            *error = errorText("create temporary dictionary");
        }
        return false;
    }
    const std::filesystem::path temporary_path(temporary.data());
    std::string write_error;
    const std::string text = encode(snapshot);
    const bool written = fchmod(descriptor, S_IRUSR | S_IWUSR) == 0 &&
                         writeAll(descriptor, text, write_error) &&
                         fsync(descriptor) == 0;
    if (!written && write_error.empty()) {
        write_error = errorText("sync dictionary");
    }
    const int close_result = close(descriptor);
    if (!written || close_result != 0) {
        std::filesystem::remove(temporary_path, filesystem_error);
        if (error) {
            *error = write_error.empty() ? errorText("close dictionary")
                                         : write_error;
        }
        return false;
    }
    if (rename(temporary_path.c_str(), path_.c_str()) != 0) {
        const std::string rename_error = errorText("replace dictionary");
        std::filesystem::remove(temporary_path, filesystem_error);
        if (error) {
            *error = rename_error;
        }
        return false;
    }
    const int directory = open(parent.c_str(), O_RDONLY | O_DIRECTORY);
    if (directory >= 0) {
        const int sync_result = fsync(directory);
        close(directory);
        if (sync_result != 0) {
            if (error) {
                *error = errorText("sync dictionary directory");
            }
            return false;
        }
    }
    return true;
}

} // namespace unilume::dictionary
