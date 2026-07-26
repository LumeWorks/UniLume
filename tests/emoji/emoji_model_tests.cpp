// SPDX-License-Identifier: GPL-2.0-or-later

#include "emoji_model.h"

#include <filesystem>
#include <fstream>
#include <iostream>
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
         "unilume-emoji-tests-XXXXXX")
            .string();
    if (char *path = mkdtemp(pattern.data())) {
        return path;
    }
    return {};
}

} // namespace

int main()
{
    using namespace unilume::emoji;

    SearchIndex index;
    expect(index.add("smile", {"😀", "😄"}), "valid fixture loads");
    expect(index.add("smiling face", {"🙂"}), "second fixture loads");
    expect(index.add("grinning cat", {"😺"}), "fuzzy fixture loads");
    expect(index.add("khuôn mặt vui", {"😊"}),
           "UTF-8 fuzzy fixture loads");
    expect(!index.add("", {"x"}), "empty keyword is rejected");

    const auto exact = index.search("SMILE");
    expect(exact.size() >= 2 && exact[0].glyph == "😀" &&
               exact[1].glyph == "😄",
           "exact results are case-insensitive and deterministic");
    const auto prefix = index.search("smil");
    expect(prefix.size() == 3 && prefix[0].keyword == "smile",
           "prefix results precede longer keyword matches");
    const auto fuzzy = index.search("grcat");
    expect(fuzzy.size() == 1 && fuzzy[0].glyph == "😺",
           "subsequence fuzzy match is deterministic");
    const auto utf8_fuzzy = index.search("kmtv");
    expect(utf8_fuzzy.size() == 1 && utf8_fuzzy[0].glyph == "😊",
           "fuzzy search advances by Unicode code point");
    expect(index.search(std::string(max_query_bytes + 1, 'x')).empty(),
           "oversized query is rejected");

    HistorySnapshot history{{"😀", "🙂"}};
    const std::string encoded = encodeHistory(history);
    expect(decodeHistory(encoded).snapshot == history,
           "history codec round trips");
    expect(!decodeHistory("broken").ok(),
           "corrupt history is rejected");
    expect(!decodeHistory(
                "unilume_emoji_history_version=1\n😀\n😀\n")
                .ok(),
           "duplicate history is rejected");

    const std::filesystem::path directory = temporaryDirectory();
    expect(!directory.empty(), "temporary directory is available");
    const std::filesystem::path path = directory / "emoji-history";
    HistoryStore store(path);
    expect(store.load().disposition == HistoryLoadDisposition::missing,
           "missing history starts empty");
    for (std::size_t index = 0; index < max_history_entries + 10; ++index) {
        expect(store.record("emoji-" + std::to_string(index)),
               "history record saves atomically");
    }
    expect(store.active().recent.size() == max_history_entries &&
               store.active().recent.front() == "emoji-73",
           "history is MRU ordered and bounded");
    expect(store.record("emoji-50") &&
               store.active().recent.front() == "emoji-50",
           "recording existing entry moves it to front");
    HistoryStore reloaded(path);
    expect(reloaded.load().ok() &&
               reloaded.active() == store.active(),
           "saved history reloads");
    expect(reloaded.clear() && reloaded.active().recent.empty(),
           "history clears atomically");

    const std::filesystem::path corrupt = directory / "corrupt";
    {
        std::ofstream stream(corrupt, std::ios::binary);
        stream << "not a history";
    }
    HistoryStore corrupt_store(corrupt);
    expect(!corrupt_store.load().ok() &&
               corrupt_store.active().recent.empty(),
           "corrupt file preserves last known good history");
    const std::filesystem::path impossible = directory / "file" / "history";
    {
        std::ofstream stream(directory / "file");
        stream << "not a directory";
    }
    HistoryStore failing_store(impossible);
    std::string error;
    expect(!failing_store.record("😀", &error) && !error.empty() &&
               failing_store.active().recent.empty(),
           "write failure preserves active history");

    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    return failures == 0 ? 0 : 1;
}
