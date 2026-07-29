// SPDX-License-Identifier: GPL-2.0-or-later

#include "diagnostic_trace.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <vector>

namespace {

bool expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
    }
    return condition;
}

std::string readFile(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    return {
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>(),
    };
}

} // namespace

int main()
{
    using namespace unilume;
    using namespace unilume::fcitx5;

    bool ok = true;
    DiagnosticTrace disabled{DiagnosticSettings{}};
    ok &= expect(
        disabled.beginEvent() == 0,
        "disabled trace must not read the clock");
    disabled.recordReset(TraceResetReason::focus);
    ok &= expect(
        disabled.snapshot().total_events == 0,
        "disabled trace must not retain events");

    DiagnosticTrace trace{DiagnosticSettings{true, {}}};
    ok &= expect(
        sizeof(DiagnosticTrace) <= 16 * 1024,
        "per-context diagnostic state must have a fixed small bound");
    ReplacementObservation ready;
    ready.sequence_id = 17;
    ready.surrounding_available = true;
    ready.cursor_valid = true;
    ready.utf8_valid = true;
    ready.within_resource_limit = true;

    core::TransactionMetrics metrics;
    trace.recordDirect(
        core::SubmissionStatus::fallback,
        metrics,
        ready,
        trace.beginEvent());
    ++metrics.stale_result_count;
    trace.recordDirect(
        core::SubmissionStatus::handled,
        metrics,
        ready,
        trace.beginEvent());
    ++metrics.uncertain_outcome_count;
    trace.recordDirect(
        core::SubmissionStatus::handled,
        metrics,
        ready,
        trace.beginEvent());
    ++metrics.fallback_failure_count;
    trace.recordDirect(
        core::SubmissionStatus::unhandled,
        metrics,
        ready,
        trace.beginEvent());

    ReplacementObservation unavailable;
    trace.recordDirect(
        core::SubmissionStatus::handled,
        metrics,
        unavailable,
        trace.beginEvent());
    ReplacementObservation non_atomic_transport = ready;
    non_atomic_transport.atomic_transport = false;
    trace.recordDirect(
        core::SubmissionStatus::handled,
        metrics,
        non_atomic_transport,
        trace.beginEvent());
    ReplacementObservation invalid_cursor = ready;
    invalid_cursor.cursor_valid = false;
    trace.recordDirect(
        core::SubmissionStatus::handled,
        metrics,
        invalid_cursor,
        trace.beginEvent());
    ReplacementObservation invalid_utf8 = ready;
    invalid_utf8.utf8_valid = false;
    trace.recordDirect(
        core::SubmissionStatus::handled,
        metrics,
        invalid_utf8,
        trace.beginEvent());
    ReplacementObservation resource_limit = ready;
    resource_limit.within_resource_limit = false;
    trace.recordDirect(
        core::SubmissionStatus::handled,
        metrics,
        resource_limit,
        trace.beginEvent());
    trace.recordReset(TraceResetReason::capability_loss);
    trace.recordModeChange(true, unavailable);
    trace.recordPreeditHandoff(unavailable);

    static constexpr std::string_view canary =
        "typed-secret user@example.com token=ABCD1234";
    for (std::size_t index = 0; index < 1'000'000; ++index) {
        trace.recordPreedit(
            {true, canary, canary},
            unavailable,
            trace.beginEvent());
    }
    const DiagnosticSnapshot snapshot = trace.snapshot();
    ok &= expect(
        snapshot.total_events == 1'000'012 &&
            snapshot.retained_events == 64,
        "ring must retain only its fixed capacity");
    ok &= expect(
        snapshot.fallback_events == 1 &&
            snapshot.preedit_handoffs == 1 &&
            snapshot.stale_events == 1 &&
            snapshot.uncertain_events == 1 &&
            snapshot.backend_failures == 1 &&
            snapshot.capability_losses == 1 &&
            snapshot.capabilities[
                static_cast<std::size_t>(
                    TraceCapability::unavailable)] != 0 &&
            snapshot.capabilities[
                static_cast<std::size_t>(
                    TraceCapability::non_atomic_transport)] != 0 &&
            snapshot.capabilities[
                static_cast<std::size_t>(
                    TraceCapability::invalid_cursor)] != 0 &&
            snapshot.capabilities[
                static_cast<std::size_t>(
                    TraceCapability::invalid_utf8)] != 0 &&
            snapshot.capabilities[
                static_cast<std::size_t>(
                    TraceCapability::resource_limit)] != 0,
        "incident counters must distinguish backend outcomes");

    const std::string bundle = trace.renderBundle();
    ok &= expect(
        bundle.find(canary) == std::string::npos &&
            bundle.find("user@example.com") == std::string::npos &&
            bundle.find("ABCD1234") == std::string::npos &&
            bundle.find("commit_bytes") == std::string::npos &&
            bundle.find("preedit_bytes") == std::string::npos &&
            bundle.find("surrounding_bytes") == std::string::npos,
        "bundle must not contain typed, preedit, or secret content");
    ok &= expect(
        bundle.find("\"fallbacks\":1") != std::string::npos &&
            bundle.find("\"preedit_handoffs\":1") !=
                std::string::npos &&
            bundle.find("\"non_atomic_transport\":1") !=
                std::string::npos &&
            bundle.find("\"stale_results\":1") != std::string::npos &&
            bundle.find("\"uncertain_outcomes\":1") !=
                std::string::npos &&
            bundle.find("\"backend_failures\":1") !=
                std::string::npos &&
            bundle.find("\"capability_losses\":1") !=
                std::string::npos,
        "bundle must expose actionable incident counters");
    ok &= expect(
        bundle.size() < 64 * 1024,
        "maximum ring must serialize below the export limit");

    std::string directory_template =
        "/tmp/unilume-diagnostics-test.XXXXXX";
    std::vector<char> directory_buffer(
        directory_template.begin(), directory_template.end());
    directory_buffer.push_back('\0');
    const char *directory = mkdtemp(directory_buffer.data());
    ok &= expect(directory != nullptr, "temporary directory must be created");
    if (directory != nullptr) {
        const std::filesystem::path path =
            std::filesystem::path(directory) / "bundle.json";
        DiagnosticTrace exported{
            DiagnosticSettings{true, path}};
        exported.recordPreedit(
            {true, canary, canary},
            ready,
            exported.beginEvent());
        std::string error;
        ok &= expect(
            exported.exportBundle(&error),
            "first atomic export must succeed");
        exported.recordReset(TraceResetReason::focus);
        ok &= expect(
            exported.exportBundle(&error),
            "second export must rotate and succeed");
        const std::filesystem::path previous =
            path.string() + ".previous";
        ok &= expect(
            std::filesystem::file_size(path) < 64 * 1024 &&
                std::filesystem::file_size(previous) < 64 * 1024,
            "current and previous exports must remain bounded");
        const std::string current = readFile(path);
        const std::string old = readFile(previous);
        ok &= expect(
            current.find(canary) == std::string::npos &&
                old.find(canary) == std::string::npos,
            "rotated files must preserve redaction");
        struct stat information {};
        ok &= expect(
            stat(path.c_str(), &information) == 0 &&
                (information.st_mode & 0777) == 0600,
            "export must be private to the current user");
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }

#ifdef NDEBUG
    constexpr std::size_t iterations = 5'000'000;
    const auto start = std::chrono::steady_clock::now();
    std::uint64_t control = 0;
    for (std::size_t index = 0; index < iterations; ++index) {
        control ^= disabled.beginEvent();
    }
    const auto end = std::chrono::steady_clock::now();
    const double nanoseconds =
        std::chrono::duration<double, std::nano>(end - start).count() /
        static_cast<double>(iterations);
    ok &= expect(
        control == 0 && nanoseconds <= 10.0,
        "disabled beginEvent overhead must stay within 10 ns/key");
#endif

    return ok ? 0 : 1;
}
