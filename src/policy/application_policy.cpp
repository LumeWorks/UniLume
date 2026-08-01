// SPDX-License-Identifier: GPL-2.0-or-later

#include "application_policy.h"

#include <algorithm>
#include <set>
#include <utility>

namespace unilume::policy {
namespace {

constexpr std::string_view header = "unilume_app_policy_version=1\n";

bool validPattern(std::string_view pattern)
{
    if (pattern.empty() || pattern.size() > max_identity_bytes) {
        return false;
    }
    for (const unsigned char character : pattern) {
        const bool ascii_alphanumeric =
            (character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9');
        if (!ascii_alphanumeric &&
            character != '.' && character != '_' && character != '-') {
            return false;
        }
    }
    return true;
}

bool parseMode(std::string_view text, ApplicationMode &mode)
{
    if (text == "automatic") {
        mode = ApplicationMode::automatic;
    } else if (text == "direct") {
        mode = ApplicationMode::direct;
    } else if (text == "safe-preedit") {
        mode = ApplicationMode::safe_preedit;
    } else if (text == "off") {
        mode = ApplicationMode::off;
    } else {
        return false;
    }
    return true;
}

bool exactLess(const Rule &left, const Rule &right)
{
    return left.pattern < right.pattern;
}

bool prefixLess(const Rule &left, const Rule &right)
{
    if (left.pattern.size() != right.pattern.size()) {
        return left.pattern.size() > right.pattern.size();
    }
    return left.pattern < right.pattern;
}

} // namespace

std::string_view modeName(ApplicationMode mode)
{
    switch (mode) {
    case ApplicationMode::automatic:
        return "automatic";
    case ApplicationMode::direct:
        return "direct";
    case ApplicationMode::safe_preedit:
        return "safe-preedit";
    case ApplicationMode::off:
        return "off";
    }
    return {};
}

std::string validate(const Snapshot &snapshot)
{
    if (!snapshot.table || modeName(snapshot.table->default_mode).empty()) {
        return "application policy table is invalid";
    }
    const Table &table = *snapshot.table;
    if (table.exact_rules.size() + table.prefix_rules.size() > max_rules) {
        return "too many application policy rules";
    }
    if (!std::is_sorted(table.exact_rules.begin(),
                        table.exact_rules.end(), exactLess) ||
        !std::is_sorted(table.prefix_rules.begin(),
                        table.prefix_rules.end(), prefixLess)) {
        return "application policy rules are not canonical";
    }
    std::set<std::pair<MatchKind, std::string>> seen;
    for (const auto [rules, expected_kind] :
         {std::pair{&table.exact_rules, MatchKind::exact},
          std::pair{&table.prefix_rules, MatchKind::prefix}}) {
        for (const Rule &rule : *rules) {
            if (!validPattern(rule.pattern) ||
                modeName(rule.mode).empty() ||
                rule.kind != expected_kind ||
                !seen.emplace(rule.kind, rule.pattern).second) {
                return "invalid or conflicting application policy rule";
            }
        }
    }
    return {};
}

DecodeResult decode(std::string_view text)
{
    if (text.size() > max_serialized_bytes) {
        return {.field = "file", .error = "application policy exceeds size limit"};
    }
    if (!text.starts_with(header)) {
        return {.line = 1, .field = "version",
                .error = "unsupported application policy version"};
    }
    auto table = std::make_shared<Table>();
    bool has_default = false;
    bool legacy_modes = false;
    std::set<std::pair<MatchKind, std::string>> seen;
    std::size_t offset = header.size();
    std::size_t line_number = 1;
    while (offset < text.size()) {
        ++line_number;
        const std::size_t end = text.find('\n', offset);
        std::string_view line = text.substr(
            offset, end == std::string_view::npos ? text.size() - offset
                                                  : end - offset);
        offset = end == std::string_view::npos ? text.size() : end + 1;
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }
        std::vector<std::string_view> fields;
        std::size_t field_offset = 0;
        for (;;) {
            const std::size_t separator = line.find('\t', field_offset);
            fields.push_back(line.substr(
                field_offset,
                separator == std::string_view::npos
                    ? line.size() - field_offset
                    : separator - field_offset));
            if (separator == std::string_view::npos) {
                break;
            }
            field_offset = separator + 1;
        }
        if (fields.size() == 2 && fields[0] == "default") {
            if (has_default ||
                !parseMode(fields[1], table->default_mode)) {
                return {.line = line_number, .field = "default",
                        .error = "invalid or duplicate default mode"};
            }
            legacy_modes = legacy_modes || fields[1] == "automatic" ||
                           fields[1] == "safe-preedit";
            has_default = true;
            continue;
        }
        if (fields.size() != 3 || fields[0] != "rule") {
            return {.line = line_number, .field = "entry",
                    .error = "entry must be default or three-field rule"};
        }
        std::string_view pattern = fields[1];
        MatchKind kind = MatchKind::exact;
        const std::size_t wildcard = pattern.find('*');
        if (wildcard != std::string_view::npos) {
            if (wildcard + 1 != pattern.size() ||
                pattern.find('*', wildcard + 1) != std::string_view::npos) {
                return {.line = line_number, .field = "pattern",
                        .error = "wildcard is allowed only once at the end"};
            }
            pattern.remove_suffix(1);
            kind = MatchKind::prefix;
        }
        ApplicationMode mode{};
        if (!validPattern(pattern)) {
            return {.line = line_number, .field = "pattern",
                    .error = "invalid application identity pattern"};
        }
        if (!parseMode(fields[2], mode)) {
            return {.line = line_number, .field = "mode",
                    .error = "unknown application mode"};
        }
        legacy_modes = legacy_modes || fields[2] == "automatic" ||
                       fields[2] == "safe-preedit";
        if (!seen.emplace(kind, std::string(pattern)).second) {
            return {.line = line_number, .field = "pattern",
                    .error = "duplicate or conflicting rule"};
        }
        Rule rule{kind, std::string(pattern), mode};
        (kind == MatchKind::exact ? table->exact_rules
                                  : table->prefix_rules)
            .push_back(std::move(rule));
        if (table->exact_rules.size() + table->prefix_rules.size() >
            max_rules) {
            return {.line = line_number, .field = "file",
                    .error = "too many application policy rules"};
        }
    }
    if (!has_default) {
        return {.line = line_number, .field = "default",
                .error = "application policy requires one default"};
    }
    std::sort(table->exact_rules.begin(), table->exact_rules.end(), exactLess);
    std::sort(table->prefix_rules.begin(), table->prefix_rules.end(), prefixLess);
    return {.snapshot = {std::move(table)},
            .legacy_modes = legacy_modes};
}

std::string encode(const Snapshot &snapshot)
{
    if (!validate(snapshot).empty()) {
        return {};
    }
    std::string result(header);
    result += "default\t";
    result += modeName(snapshot.table->default_mode);
    result += '\n';
    for (const Rule &rule : snapshot.table->exact_rules) {
        result += "rule\t";
        result += rule.pattern;
        result += '\t';
        result += modeName(rule.mode);
        result += '\n';
    }
    for (const Rule &rule : snapshot.table->prefix_rules) {
        result += "rule\t";
        result += rule.pattern;
        result += "*\t";
        result += modeName(rule.mode);
        result += '\n';
    }
    return result;
}

Resolution resolve(const Snapshot &snapshot,
                   std::string_view application_identity)
{
    if (!snapshot.table || application_identity.empty()) {
        return {ApplicationMode::automatic,
                ResolutionSource::missing_identity, {}};
    }
    const Table &table = *snapshot.table;
    const auto exact = std::lower_bound(
        table.exact_rules.begin(), table.exact_rules.end(),
        application_identity,
        [](const Rule &rule, std::string_view identity) {
            return rule.pattern < identity;
        });
    if (exact != table.exact_rules.end() &&
        exact->pattern == application_identity) {
        return {exact->mode,
                ResolutionSource::exact_rule, exact->pattern};
    }
    for (const Rule &rule : table.prefix_rules) {
        if (application_identity.starts_with(rule.pattern)) {
            return {rule.mode,
                    ResolutionSource::prefix_rule, rule.pattern};
        }
    }
    return {table.default_mode,
            ResolutionSource::default_rule, {}};
}

} // namespace unilume::policy
