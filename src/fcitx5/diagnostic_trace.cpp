// SPDX-License-Identifier: GPL-2.0-or-later

#include "diagnostic_trace.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fcitx-utils/log.h>
#include <sstream>
#include <string_view>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <utility>
#include <vector>

#ifndef UNILUME_DIAGNOSTIC_UNILUME_VERSION
#define UNILUME_DIAGNOSTIC_UNILUME_VERSION "unknown"
#endif

#ifndef UNILUME_DIAGNOSTIC_FCITX_VERSION
#define UNILUME_DIAGNOSTIC_FCITX_VERSION "unknown"
#endif

namespace unilume::fcitx5 {
namespace {

std::atomic<std::uint64_t> next_context_id{1};

std::uint64_t monotonicNanoseconds()
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

DiagnosticSettings settingsFromEnvironment()
{
    DiagnosticSettings settings;
    const char *enabled = std::getenv("UNILUME_FCITX_DIAGNOSTICS");
    settings.enabled =
        enabled != nullptr && enabled[0] == '1' && enabled[1] == '\0';
    if (!settings.enabled) {
        return settings;
    }
    const char *path =
        std::getenv("UNILUME_FCITX_DIAGNOSTIC_FILE");
    if (path != nullptr) {
        const std::string_view value(path);
        if (!value.empty() && value.size() <= 4096 &&
            value.find_first_of("\r\n") == std::string_view::npos) {
            settings.export_path = path;
        }
    }
    return settings;
}

const char *eventKindName(DiagnosticTrace::EventKind kind)
{
    switch (kind) {
    case DiagnosticTrace::EventKind::key:
        return "key";
    case DiagnosticTrace::EventKind::reset:
        return "reset";
    case DiagnosticTrace::EventKind::mode_change:
        return "mode_change";
    }
    return "unknown";
}

const char *pathName(TracePath path)
{
    switch (path) {
    case TracePath::direct:
        return "direct";
    case TracePath::preedit:
        return "preedit";
    case TracePath::off:
        return "off";
    }
    return "unknown";
}

const char *capabilityName(TraceCapability capability)
{
    switch (capability) {
    case TraceCapability::unavailable:
        return "unavailable";
    case TraceCapability::ready:
        return "ready";
    case TraceCapability::invalid_cursor:
        return "invalid_cursor";
    case TraceCapability::invalid_utf8:
        return "invalid_utf8";
    case TraceCapability::resource_limit:
        return "resource_limit";
    case TraceCapability::count:
        break;
    }
    return "unknown";
}

const char *errorName(TraceError error)
{
    switch (error) {
    case TraceError::none:
        return "none";
    case TraceError::fallback:
        return "fallback";
    case TraceError::stale_result:
        return "stale_result";
    case TraceError::uncertain_outcome:
        return "uncertain_outcome";
    case TraceError::backend_failure:
        return "backend_failure";
    case TraceError::capability_loss:
        return "capability_loss";
    }
    return "unknown";
}

const char *resetName(TraceResetReason reason)
{
    switch (reason) {
    case TraceResetReason::none:
        return "none";
    case TraceResetReason::focus:
        return "focus";
    case TraceResetReason::navigation:
        return "navigation";
    case TraceResetReason::control_shortcut:
        return "control_shortcut";
    case TraceResetReason::capability_loss:
        return "capability_loss";
    }
    return "unknown";
}

const char *durationName(TraceDurationBucket bucket)
{
    switch (bucket) {
    case TraceDurationBucket::under_1_us:
        return "under_1_us";
    case TraceDurationBucket::under_5_us:
        return "under_5_us";
    case TraceDurationBucket::under_20_us:
        return "under_20_us";
    case TraceDurationBucket::under_100_us:
        return "under_100_us";
    case TraceDurationBucket::under_1_ms:
        return "under_1_ms";
    case TraceDurationBucket::at_least_1_ms:
        return "at_least_1_ms";
    case TraceDurationBucket::count:
        break;
    }
    return "unknown";
}

std::string trustedVersion(std::string_view value)
{
    if (value.empty() || value.size() > 32) {
        return "unknown";
    }
    for (const unsigned char character : value) {
        if (!((character >= '0' && character <= '9') ||
              (character >= 'A' && character <= 'Z') ||
              (character >= 'a' && character <= 'z') ||
              character == '.' || character == '-' ||
              character == '+')) {
            return "unknown";
        }
    }
    return std::string(value);
}

const char *sessionName()
{
    const char *session = std::getenv("XDG_SESSION_TYPE");
    if (session == nullptr) {
        return "unknown";
    }
    const std::string_view value(session);
    if (value == "x11" || value == "wayland" || value == "tty") {
        return session;
    }
    return "unknown";
}

std::string kernelRelease()
{
    utsname information{};
    if (uname(&information) != 0) {
        return "unknown";
    }
    return trustedVersion(information.release);
}

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
            error = errorText("write diagnostic bundle");
            return false;
        }
        written += static_cast<std::size_t>(result);
    }
    return true;
}

} // namespace

DiagnosticTrace::DiagnosticTrace()
    : DiagnosticTrace(settingsFromEnvironment())
{
}

DiagnosticTrace::DiagnosticTrace(DiagnosticSettings settings)
    : export_path_(std::move(settings.export_path)),
      enabled_(settings.enabled)
{
    if (enabled_) {
        context_id_ =
            next_context_id.fetch_add(1, std::memory_order_relaxed);
    } else {
        export_path_.clear();
    }
}

std::uint64_t DiagnosticTrace::beginEvent() const
{
    return enabled_ ? monotonicNanoseconds() : 0;
}

void DiagnosticTrace::recordDirect(
    core::SubmissionStatus status,
    const core::TransactionMetrics &metrics,
    const ReplacementObservation &replacement,
    std::uint64_t started_at_ns)
{
    if (started_at_ns == 0) {
        return;
    }
    const TraceError error = directError(status, metrics);
    append({
        0,
        replacement.sequence_id,
        metrics.queue_depth,
        metrics.reset_count,
        metrics.stale_result_count,
        metrics.uncertain_outcome_count,
        metrics.fallback_failure_count,
        EventKind::key,
        TracePath::direct,
        capability(replacement),
        error,
        durationBucket(monotonicNanoseconds() - started_at_ns),
        TraceResetReason::none,
        static_cast<std::uint8_t>(status),
        status != core::SubmissionStatus::unhandled,
    });
    previous_metrics_ = metrics;
}

void DiagnosticTrace::recordPreedit(
    const core::PreeditAction &action,
    bool surrounding_available,
    std::uint64_t started_at_ns)
{
    if (started_at_ns == 0) {
        return;
    }
    append({
        0,
        0,
        0,
        reset_events_,
        0,
        0,
        0,
        EventKind::key,
        TracePath::preedit,
        surrounding_available ? TraceCapability::ready
                              : TraceCapability::unavailable,
        TraceError::none,
        durationBucket(monotonicNanoseconds() - started_at_ns),
        TraceResetReason::none,
        0,
        action.handled,
    });
}

void DiagnosticTrace::recordReset(TraceResetReason reason)
{
    if (!enabled_) {
        return;
    }
    ++reset_events_;
    append({
        0,
        0,
        0,
        reset_events_,
        0,
        0,
        0,
        EventKind::reset,
        TracePath::off,
        TraceCapability::unavailable,
        reason == TraceResetReason::capability_loss
            ? TraceError::capability_loss
            : TraceError::none,
        TraceDurationBucket::under_1_us,
        reason,
        0,
        false,
    });
}

void DiagnosticTrace::recordModeChange(
    bool preedit,
    bool surrounding_available)
{
    if (!enabled_) {
        return;
    }
    ++mode_changes_;
    append({
        0,
        0,
        0,
        reset_events_,
        0,
        0,
        0,
        EventKind::mode_change,
        preedit ? TracePath::preedit : TracePath::direct,
        surrounding_available ? TraceCapability::ready
                              : TraceCapability::unavailable,
        TraceError::none,
        TraceDurationBucket::under_1_us,
        TraceResetReason::none,
        0,
        true,
    });
}

DiagnosticSnapshot DiagnosticTrace::snapshot() const
{
    return {
        total_events_,
        size_,
        reset_events_,
        mode_changes_,
        fallback_events_,
        stale_events_,
        uncertain_events_,
        backend_failures_,
        capability_losses_,
        capabilities_,
        durations_,
    };
}

std::string DiagnosticTrace::renderBundle() const
{
    std::ostringstream output;
    output
        << "{\"schema\":1"
        << ",\"unilume_version\":\""
        << trustedVersion(UNILUME_DIAGNOSTIC_UNILUME_VERSION) << '"'
        << ",\"fcitx_version\":\""
        << trustedVersion(UNILUME_DIAGNOSTIC_FCITX_VERSION) << '"'
        << ",\"session\":\"" << sessionName() << '"'
        << ",\"kernel_release\":\"" << kernelRelease() << '"'
        << ",\"context\":" << context_id_
        << ",\"total_events\":" << total_events_
        << ",\"retained_events\":" << size_
        << ",\"resets\":" << reset_events_
        << ",\"mode_changes\":" << mode_changes_
        << ",\"fallbacks\":" << fallback_events_
        << ",\"stale_results\":" << stale_events_
        << ",\"uncertain_outcomes\":" << uncertain_events_
        << ",\"backend_failures\":" << backend_failures_
        << ",\"capability_losses\":" << capability_losses_
        << ",\"capabilities\":{";
    for (std::size_t index = 0; index < capabilities_.size(); ++index) {
        if (index != 0) {
            output << ',';
        }
        output << '"' << capabilityName(
            static_cast<TraceCapability>(index))
               << "\":" << capabilities_[index];
    }
    output
        << "}"
        << ",\"duration_buckets\":{";
    for (std::size_t index = 0; index < durations_.size(); ++index) {
        if (index != 0) {
            output << ',';
        }
        output << '"' << durationName(
            static_cast<TraceDurationBucket>(index))
               << "\":" << durations_[index];
    }
    output << "},\"events\":[";
    const std::size_t first = size_ == capacity ? next_ : 0;
    for (std::size_t offset = 0; offset < size_; ++offset) {
        if (offset != 0) {
            output << ',';
        }
        const Event &event = events_[(first + offset) % capacity];
        output
            << "{\"sequence\":" << event.sequence
            << ",\"backend_sequence\":" << event.backend_sequence
            << ",\"kind\":\"" << eventKindName(event.kind) << '"'
            << ",\"path\":\"" << pathName(event.path) << '"'
            << ",\"capability\":\""
            << capabilityName(event.capability) << '"'
            << ",\"status\":" << static_cast<unsigned int>(event.status)
            << ",\"handled\":" << (event.handled ? "true" : "false")
            << ",\"queue\":" << event.queue_depth
            << ",\"resets\":" << event.reset_count
            << ",\"stale\":" << event.stale_count
            << ",\"uncertain\":" << event.uncertain_count
            << ",\"fallback_failures\":"
            << event.fallback_failure_count
            << ",\"reset_reason\":\""
            << resetName(event.reset_reason) << '"'
            << ",\"error\":\"" << errorName(event.error) << '"'
            << ",\"duration\":\""
            << durationName(event.duration) << "\"}";
    }
    output << "]}\n";
    return output.str();
}

bool DiagnosticTrace::exportBundle(std::string *error) const
{
    if (!enabled_ || export_path_.empty()) {
        return true;
    }
    const std::string payload = renderBundle();
    if (payload.size() > maximum_bundle_bytes) {
        if (error != nullptr) {
            *error = "diagnostic bundle exceeds fixed size limit";
        }
        return false;
    }

    const std::filesystem::path parent =
        export_path_.has_parent_path()
            ? export_path_.parent_path()
            : std::filesystem::path{"."};
    std::error_code filesystem_error;
    std::filesystem::create_directories(parent, filesystem_error);
    if (filesystem_error) {
        if (error != nullptr) {
            *error = "create diagnostic directory failed";
        }
        return false;
    }

    std::filesystem::path previous = export_path_;
    previous += ".previous";
    std::filesystem::remove(previous, filesystem_error);
    filesystem_error.clear();
    if (std::filesystem::exists(export_path_, filesystem_error)) {
        const std::uintmax_t existing_size =
            std::filesystem::file_size(export_path_, filesystem_error);
        if (!filesystem_error &&
            existing_size <= maximum_bundle_bytes) {
            std::filesystem::rename(
                export_path_, previous, filesystem_error);
        } else {
            filesystem_error.clear();
            std::filesystem::remove(export_path_, filesystem_error);
        }
        if (filesystem_error) {
            if (error != nullptr) {
                *error = "rotate diagnostic bundle failed";
            }
            return false;
        }
    }

    std::string template_path =
        export_path_.string() + ".tmp.XXXXXX";
    std::vector<char> temporary(
        template_path.begin(), template_path.end());
    temporary.push_back('\0');
    const int descriptor = mkstemp(temporary.data());
    if (descriptor < 0) {
        if (error != nullptr) {
            *error = errorText("create diagnostic bundle");
        }
        return false;
    }
    const std::filesystem::path temporary_path(temporary.data());
    std::string write_error;
    const bool written =
        fchmod(descriptor, S_IRUSR | S_IWUSR) == 0 &&
        writeAll(descriptor, payload, write_error) &&
        fsync(descriptor) == 0;
    const int close_result = close(descriptor);
    if (!written || close_result != 0 ||
        rename(temporary_path.c_str(), export_path_.c_str()) != 0) {
        if (write_error.empty()) {
            write_error = errorText("replace diagnostic bundle");
        }
        std::filesystem::remove(temporary_path, filesystem_error);
        if (error != nullptr) {
            *error = write_error;
        }
        return false;
    }
    const int directory = open(parent.c_str(), O_RDONLY | O_DIRECTORY);
    if (directory >= 0) {
        const int sync_result = fsync(directory);
        close(directory);
        if (sync_result != 0) {
            if (error != nullptr) {
                *error = errorText("sync diagnostic directory");
            }
            return false;
        }
    }
    return true;
}

void DiagnosticTrace::flush() const
{
    if (!enabled_ ||
        (total_events_ == 0 && reset_events_ == 0 &&
         mode_changes_ == 0)) {
        return;
    }
    FCITX_INFO()
        << "UniLume diagnostic summary"
        << " total_events=" << total_events_
        << " retained_events=" << size_
        << " resets=" << reset_events_
        << " mode_changes=" << mode_changes_
        << " fallbacks=" << fallback_events_
        << " stale=" << stale_events_
        << " uncertain=" << uncertain_events_
        << " backend_failures=" << backend_failures_
        << " capability_losses=" << capability_losses_
        << " context=" << context_id_;
    const std::size_t first = size_ == capacity ? next_ : 0;
    for (std::size_t offset = 0; offset < size_; ++offset) {
        const Event &event = events_[(first + offset) % capacity];
        FCITX_INFO()
            << "UniLume diagnostic event"
            << " sequence=" << event.sequence
            << " backend_sequence=" << event.backend_sequence
            << " kind=" << eventKindName(event.kind)
            << " path=" << pathName(event.path)
            << " capability=" << capabilityName(event.capability)
            << " status=" << static_cast<unsigned int>(event.status)
            << " handled=" << event.handled
            << " queue=" << event.queue_depth
            << " reset_reason=" << resetName(event.reset_reason)
            << " error=" << errorName(event.error)
            << " duration=" << durationName(event.duration);
    }
    std::string error;
    if (!exportBundle(&error)) {
        FCITX_WARN()
            << "UniLume diagnostic bundle export failed: " << error;
    }
}

TraceError DiagnosticTrace::directError(
    core::SubmissionStatus status,
    const core::TransactionMetrics &metrics) const
{
    if (metrics.fallback_failure_count >
        previous_metrics_.fallback_failure_count) {
        return TraceError::backend_failure;
    }
    if (metrics.uncertain_outcome_count >
        previous_metrics_.uncertain_outcome_count) {
        return TraceError::uncertain_outcome;
    }
    if (metrics.stale_result_count >
        previous_metrics_.stale_result_count) {
        return TraceError::stale_result;
    }
    if (status == core::SubmissionStatus::fallback) {
        return TraceError::fallback;
    }
    return TraceError::none;
}

TraceCapability DiagnosticTrace::capability(
    const ReplacementObservation &replacement)
{
    if (!replacement.surrounding_available) {
        return TraceCapability::unavailable;
    }
    if (!replacement.within_resource_limit) {
        return TraceCapability::resource_limit;
    }
    if (!replacement.utf8_valid) {
        return TraceCapability::invalid_utf8;
    }
    if (!replacement.cursor_valid) {
        return TraceCapability::invalid_cursor;
    }
    return TraceCapability::ready;
}

TraceDurationBucket DiagnosticTrace::durationBucket(
    std::uint64_t duration_ns)
{
    if (duration_ns < 1'000) {
        return TraceDurationBucket::under_1_us;
    }
    if (duration_ns < 5'000) {
        return TraceDurationBucket::under_5_us;
    }
    if (duration_ns < 20'000) {
        return TraceDurationBucket::under_20_us;
    }
    if (duration_ns < 100'000) {
        return TraceDurationBucket::under_100_us;
    }
    if (duration_ns < 1'000'000) {
        return TraceDurationBucket::under_1_ms;
    }
    return TraceDurationBucket::at_least_1_ms;
}

void DiagnosticTrace::append(Event event)
{
    if (!enabled_) {
        return;
    }
    event.sequence = ++total_events_;
    if (event.kind == EventKind::key) {
        ++durations_[static_cast<std::size_t>(event.duration)];
    }
    if (event.kind != EventKind::reset) {
        ++capabilities_[static_cast<std::size_t>(event.capability)];
    }
    switch (event.error) {
    case TraceError::fallback:
        ++fallback_events_;
        break;
    case TraceError::stale_result:
        ++stale_events_;
        break;
    case TraceError::uncertain_outcome:
        ++uncertain_events_;
        break;
    case TraceError::backend_failure:
        ++backend_failures_;
        break;
    case TraceError::none:
        break;
    case TraceError::capability_loss:
        ++capability_losses_;
        break;
    }
    events_[next_] = event;
    next_ = (next_ + 1) % capacity;
    if (size_ < capacity) {
        ++size_;
    }
}

} // namespace unilume::fcitx5
