// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/app/renderer_records.hpp"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <utility>

namespace oa::app::renderer_state {

namespace {

/// The words of the accelerated paths, in the order of AcceleratedPath.
constexpr std::array<std::string_view, 3> path_words{"magnify", "prescale", "blend"};

/// The words of the sentinel's stages, in the order of SentinelStage.
constexpr std::array<std::string_view, 6> sentinel_words{
    "create", "standard", "probe", "accelerated", "path", "running"
};

/// The words of the strikes' stages, in the order of StrikeStage; none has
/// no word.
constexpr std::array<std::string_view, 9> strike_words{
    "", "create", "standard", "probe", "path", "present", "call", "lost", "resets"
};

/// The words of the recorded failures, in the order of RecordedFailure; none
/// has no word.
constexpr std::array<std::string_view, 6> failure_words{
    "", "stopped", "present", "call", "lost", "resets"
};

/// Thousandths in a whole number, for the median of a scale-level record.
constexpr uint32_t thousandths_per_unit = 1000;
/// Decimal digits after the point in a scale-level record's median.
constexpr size_t median_fraction_digits = 3;
/// The first byte that is not a control character.
constexpr unsigned char first_printable = 0x20;
/// The delete character, a control character.
constexpr unsigned char delete_character = 0x7f;
/// The top two bits of a byte that continues a UTF-8 character.
constexpr unsigned char utf8_continuation_mask = 0xc0;
/// Those bits' value in a continuation byte.
constexpr unsigned char utf8_continuation_bits = 0x80;

/// Returns the word of a path.
///
/// @param path the path
/// @return its word
std::string_view path_word(AcceleratedPath path) noexcept {
    return path_words[static_cast<size_t>(path)];
}

/// Reads a path from its word.
///
/// @param word the word
/// @return the path, or nullopt for another word
std::optional<AcceleratedPath> parse_path(std::string_view word) noexcept {
    for (size_t index = 0; index < path_words.size(); ++index)
        if (path_words[index] == word)
            return static_cast<AcceleratedPath>(index);
    return std::nullopt;
}

/// Tells whether a byte is a control character.
///
/// @param byte the byte
/// @return true below a space, and for delete
bool control_byte(char byte) noexcept {
    const auto value = static_cast<unsigned char>(byte);
    return value < first_printable || value == delete_character;
}

/// Splits a value into its words, which single spaces separate.
///
/// @param text the value
/// @param[out] words the words, as views of `text`
/// @return false when the value is empty, holds a control character, or has
///     a space at either end or two in a row
bool split_words(std::string_view text, std::vector<std::string_view>& words) {
    words.clear();
    if (text.empty())
        return false;
    size_t start = 0;
    for (size_t index = 0; index <= text.size(); ++index) {
        if (index < text.size() && control_byte(text[index]))
            return false;
        if (index == text.size() || text[index] == ' ') {
            if (index == start)
                return false;
            words.push_back(text.substr(start, index - start));
            start = index + 1;
        }
    }
    return true;
}

/// Joins words with single spaces.
///
/// @param words the words
/// @param first the first word joined
/// @param end one past the last word joined
/// @return the words, spaced
std::string join_words(const std::vector<std::string_view>& words, size_t first, size_t end) {
    std::string text;
    for (size_t index = first; index < end; ++index) {
        if (index != first)
            text += ' ';
        text += words[index];
    }
    return text;
}

/// Reads a whole number of at most `limit`.
///
/// @param word the digits
/// @param limit the largest value allowed
/// @return the number, or nullopt when the word is not one or is above the
///     limit
std::optional<uint32_t> parse_whole(std::string_view word, uint32_t limit) noexcept {
    if (word.empty())
        return std::nullopt;
    uint32_t value = 0;
    for (const char digit : word) {
        if (digit < '0' || digit > '9')
            return std::nullopt;
        const auto next = static_cast<uint64_t>(value) * 10 + static_cast<uint32_t>(digit - '0');
        if (next > limit)
            return std::nullopt;
        value = static_cast<uint32_t>(next);
    }
    return value;
}

/// Reads a median written with at most median_fraction_digits digits after
/// the point.
///
/// @param word the median, such as 0.55 or 1
/// @return the median in thousandths, or nullopt when the word is not one or
///     is above max_median_thousandths
std::optional<uint32_t> parse_median(std::string_view word) noexcept {
    const size_t point = word.find('.');
    const std::string_view whole = word.substr(0, point);
    std::string_view fraction =
        point == std::string_view::npos ? std::string_view{} : word.substr(point + 1);
    if (point != std::string_view::npos &&
        (fraction.empty() || fraction.size() > median_fraction_digits))
        return std::nullopt;
    const auto units = parse_whole(whole, max_median_thousandths / thousandths_per_unit);
    if (!units)
        return std::nullopt;
    uint32_t thousandths = 0;
    uint32_t scale = thousandths_per_unit;
    for (const char digit : fraction) {
        if (digit < '0' || digit > '9')
            return std::nullopt;
        scale /= 10;
        thousandths += static_cast<uint32_t>(digit - '0') * scale;
    }
    const uint32_t median = *units * thousandths_per_unit + thousandths;
    if (median > max_median_thousandths)
        return std::nullopt;
    return median;
}

/// Writes a median in thousandths with three digits after the point.
///
/// @param thousandths the median
/// @return the median, such as 0.550
std::string format_median(uint32_t thousandths) {
    std::string fraction = std::to_string(thousandths % thousandths_per_unit);
    fraction.insert(0, median_fraction_digits - fraction.size(), '0');
    return std::to_string(thousandths / thousandths_per_unit) + "." + fraction;
}

/// Tells whether a strike or record entry holds anything to write.
///
/// @param entry the entry
/// @return true when it has a strike, a record or a remembered rung
bool holds_anything(const DriverRecords& entry) noexcept {
    return entry.strike.stage != StrikeStage::none ||
           entry.failed_driver.failure != RecordedFailure::none ||
           entry.accelerated_unusable.failure != RecordedFailure::none ||
           entry.scale_level.has_value();
}

/// Returns a driver's entry for changing it, or none.
///
/// @param records the records
/// @param driver the driver
/// @return its entry, or nullptr when it has none
DriverRecords* find_entry(Records& records, std::string_view driver) noexcept {
    for (DriverRecords& entry : records.drivers)
        if (entry.driver == driver)
            return &entry;
    return nullptr;
}

/// Returns the adapter the records are written under.
///
/// @param records the records
/// @return the adapter key's value, or unknown_adapter when there is none
std::string_view written_adapter(const Records& records) noexcept {
    return records.adapter.empty() ? unknown_adapter : std::string_view(records.adapter);
}

/// The stage words of a strike value and where its adapter starts.
struct StrikeWords {
    Strike strike{};
    size_t adapter_start{};
};

/// Reads the stage words at the start of a strike's value.
///
/// @param words the value's words
/// @return the strike and where its adapter starts, or nullopt
std::optional<StrikeWords> parse_strike_stage(const std::vector<std::string_view>& words) {
    if (words.empty())
        return std::nullopt;
    StrikeWords parsed;
    for (size_t index = 1; index < strike_words.size(); ++index)
        if (strike_words[index] == words[0])
            parsed.strike.stage = static_cast<StrikeStage>(index);
    switch (parsed.strike.stage) {
    case StrikeStage::none:
        return std::nullopt;
    case StrikeStage::path: {
        if (words.size() < 2)
            return std::nullopt;
        const auto path = parse_path(words[1]);
        if (!path)
            return std::nullopt;
        parsed.strike.path = *path;
        parsed.adapter_start = 2;
        return parsed;
    }
    case StrikeStage::present:
    case StrikeStage::call:
        if (words.size() < 2 || !valid_driver_name(words[1]))
            return std::nullopt;
        parsed.strike.call = std::string(words[1]);
        parsed.adapter_start = 2;
        return parsed;
    default:
        parsed.adapter_start = 1;
        return parsed;
    }
}

/// Writes the stage words of a strike.
///
/// @param strike the strike
/// @return its stage, with the path or call where it has one
std::string format_strike_stage(const Strike& strike) {
    std::string text(strike_words[static_cast<size_t>(strike.stage)]);
    if (strike.stage == StrikeStage::path)
        text += " " + std::string(path_word(strike.path));
    else if (strike.stage == StrikeStage::present || strike.stage == StrikeStage::call)
        text += " " + strike.call;
    return text;
}

/// Reads a recorded failure from its word, as a key of its kind allows it.
///
/// @param word the word
/// @param failed_driver a failed-driver record, which never names a call;
///     else an accelerated-unusable record, which never names a present
///     error
/// @return the failure, or nullopt for a word the kind does not allow
std::optional<RecordedFailure> parse_failure(std::string_view word, bool failed_driver) noexcept {
    for (size_t index = 1; index < failure_words.size(); ++index) {
        if (failure_words[index] != word)
            continue;
        const auto failure = static_cast<RecordedFailure>(index);
        if (failed_driver && failure == RecordedFailure::call)
            return std::nullopt;
        if (!failed_driver && failure == RecordedFailure::present)
            return std::nullopt;
        return failure;
    }
    return std::nullopt;
}

/// Sets a driver's strike.
///
/// @param[in,out] entry the driver's entry
/// @param strike the strike, StrikeStage::none to clear it
/// @param[in,out] change what changed
void set_strike(DriverRecords& entry, const Strike& strike, Change& change) {
    entry.struck_this_run = strike.stage != StrikeStage::none;
    if (same_strike(entry.strike, strike))
        return;
    entry.strike = strike;
    change.changed = true;
}

/// Records failed-driver against a driver, never against software.
///
/// @param[in,out] entry the driver's entry
/// @param failure what failed
/// @param[in,out] change what changed
void record_failed_driver(DriverRecords& entry, RecordedFailure failure, Change& change) {
    if (entry.driver == software_driver || entry.failed_driver.failure == failure)
        return;
    entry.failed_driver = Record{failure, false};
    change.changed = true;
    change.new_record = true;
}

/// Records accelerated-unusable against a driver, which also takes away its
/// native-density key.
///
/// @param[in,out] records the records
/// @param[in,out] entry the driver's entry
/// @param failure what failed
/// @param[in,out] change what changed
void record_accelerated_unusable(
    Records& records, DriverRecords& entry, RecordedFailure failure, Change& change
) {
    if (records.native_density && records.native_density->driver == entry.driver) {
        records.native_density.reset();
        change.changed = true;
    }
    if (entry.accelerated_unusable.failure == failure)
        return;
    entry.accelerated_unusable = Record{failure, false};
    change.changed = true;
    change.new_record = true;
}

/// Applies a start-up stage's left-over evidence to a driver: a strike, or
/// the record it makes with the same strike from an earlier run before it.
///
/// @param[in,out] records the records
/// @param driver the driver
/// @param strike the stage's strike
/// @param at_once the record is made at the first strike
/// @param[in,out] change what changed
void strike_or_record(
    Records& records, std::string_view driver, const Strike& strike, bool at_once, Change& change
) {
    DriverRecords& entry = driver_entry(records, driver);
    const bool repeated = at_once || (same_strike(entry.strike, strike) && !entry.struck_this_run);
    if (!repeated) {
        set_strike(entry, strike, change);
        return;
    }
    if (strike.stage == StrikeStage::probe || strike.stage == StrikeStage::path)
        record_accelerated_unusable(records, entry, RecordedFailure::stopped, change);
    else
        record_failed_driver(entry, RecordedFailure::stopped, change);
    set_strike(entry, Strike{}, change);
}

/// Clears a driver's strike of a start-up stage it passed.
///
/// @param[in,out] records the records
/// @param driver the driver
/// @param probe_passed the start passed the probe stage as well
/// @param[in,out] change what changed
void clear_startup_strike(
    Records& records, std::string_view driver, bool probe_passed, Change& change
) {
    DriverRecords* entry = find_entry(records, driver);
    if (entry == nullptr)
        return;
    const StrikeStage stage = entry->strike.stage;
    if (stage == StrikeStage::create || stage == StrikeStage::standard ||
        (stage == StrikeStage::probe && probe_passed))
        set_strike(*entry, Strike{}, change);
}

/// Merges one change into another.
///
/// @param[in,out] into the change kept
/// @param from the change merged in
void merge(Change& into, const Change& from) noexcept {
    into.changed = into.changed || from.changed;
    into.new_record = into.new_record || from.new_record;
}

} // namespace

CrashEvidence crash_evidence(bool windows_before_vista, bool linux_system) noexcept {
    return windows_before_vista || linux_system ? CrashEvidence::first_counts
                                                : CrashEvidence::two_in_a_row;
}

bool same_strike(const Strike& first, const Strike& second) noexcept {
    if (first.stage != second.stage)
        return false;
    if (first.stage == StrikeStage::path)
        return first.path == second.path;
    if (first.stage == StrikeStage::present || first.stage == StrikeStage::call)
        return first.call == second.call;
    return true;
}

bool same_records(const Records& first, const Records& second) noexcept {
    // Both are written under the same version, so only what they hold counts.
    constexpr std::string_view version = "compared";
    try {
        return format_records(first, version) == format_records(second, version);
    } catch (...) {
        return false;
    }
}

const DriverRecords* find_driver(const Records& records, std::string_view driver) noexcept {
    for (const DriverRecords& entry : records.drivers)
        if (entry.driver == driver)
            return &entry;
    return nullptr;
}

DriverRecords& driver_entry(Records& records, std::string_view driver) {
    if (DriverRecords* entry = find_entry(records, driver))
        return *entry;
    DriverRecords& entry = records.drivers.emplace_back();
    entry.driver = std::string(driver);
    return entry;
}

bool skips_driver(const Records& records, std::string_view driver) noexcept {
    if (driver == software_driver)
        return false;
    const DriverRecords* entry = find_driver(records, driver);
    return entry != nullptr && entry->failed_driver.failure != RecordedFailure::none;
}

std::vector<std::string_view> failed_driver_list(const Records& records) {
    std::vector<std::string_view> drivers;
    for (const DriverRecords& entry : records.drivers)
        if (skips_driver(records, entry.driver))
            drivers.emplace_back(entry.driver);
    return drivers;
}

bool acceleration_allowed(
    const Records& records, std::string_view driver, bool hardware_acceleration_flag
) noexcept {
    if (hardware_acceleration_flag)
        return true;
    const DriverRecords* entry = find_driver(records, driver);
    return entry == nullptr || entry->accelerated_unusable.failure == RecordedFailure::none;
}

bool has_untold_record(const Records& records) noexcept {
    for (const DriverRecords& entry : records.drivers) {
        if (entry.failed_driver.failure != RecordedFailure::none && !entry.failed_driver.told)
            return true;
        if (entry.accelerated_unusable.failure != RecordedFailure::none &&
            !entry.accelerated_unusable.told)
            return true;
    }
    return false;
}

bool same_sentinel(const Sentinel& first, const Sentinel& second) noexcept {
    return first.stage == second.stage &&
           (first.stage != SentinelStage::path || first.path == second.path) &&
           first.driver == second.driver && first.via == second.via;
}

bool valid_driver_name(std::string_view name) noexcept {
    if (name.empty() || name.size() > max_name_bytes)
        return false;
    return std::all_of(name.begin(), name.end(), [](char byte) {
        return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
               (byte >= '0' && byte <= '9') || byte == '_' || byte == '-';
    });
}

std::string normalise_adapter(std::string_view description) {
    std::string adapter;
    adapter.reserve(std::min(description.size(), max_adapter_bytes));
    for (const char byte : description) {
        const char kept = control_byte(byte) ? ' ' : byte;
        if (kept == ' ' && (adapter.empty() || adapter.back() == ' '))
            continue;
        adapter += kept;
    }
    if (adapter.size() > max_adapter_bytes) {
        size_t end = max_adapter_bytes;
        // Cut before the character the limit falls inside.
        while (end > 0 && (static_cast<unsigned char>(adapter[end]) & utf8_continuation_mask) ==
                              utf8_continuation_bits)
            --end;
        adapter.resize(end);
    }
    while (!adapter.empty() && adapter.back() == ' ')
        adapter.pop_back();
    return adapter.empty() ? std::string(unknown_adapter) : adapter;
}

std::string format_sentinel(const Sentinel& sentinel) {
    std::string text(sentinel_words[static_cast<size_t>(sentinel.stage)]);
    if (sentinel.stage == SentinelStage::path)
        text += " " + std::string(path_word(sentinel.path));
    text += " " + sentinel.driver;
    if (!sentinel.via.empty() && sentinel.stage != SentinelStage::path) {
        text += " " + std::string(via_word) + " ";
        for (size_t index = 0; index < sentinel.via.size(); ++index)
            text += (index == 0 ? "" : ",") + sentinel.via[index];
    }
    return text;
}

std::optional<Sentinel> parse_sentinel(std::string_view text) {
    std::vector<std::string_view> words;
    if (!split_words(text, words))
        return std::nullopt;
    Sentinel sentinel;
    const auto stage = std::find(sentinel_words.begin(), sentinel_words.end(), words[0]);
    if (stage == sentinel_words.end())
        return std::nullopt;
    sentinel.stage = static_cast<SentinelStage>(stage - sentinel_words.begin());
    if (sentinel.stage == SentinelStage::path) {
        if (words.size() != 3)
            return std::nullopt;
        const auto path = parse_path(words[1]);
        if (!path || !valid_driver_name(words[2]))
            return std::nullopt;
        sentinel.path = *path;
        sentinel.driver = std::string(words[2]);
        return sentinel;
    }
    if ((words.size() != 2 && words.size() != 4) || !valid_driver_name(words[1]))
        return std::nullopt;
    sentinel.driver = std::string(words[1]);
    if (words.size() == 4) {
        if (words[2] != via_word)
            return std::nullopt;
        std::string_view list = words[3];
        while (true) {
            const size_t comma = list.find(',');
            const std::string_view name = list.substr(0, comma);
            if (!valid_driver_name(name) || sentinel.via.size() == max_via_drivers)
                return std::nullopt;
            sentinel.via.emplace_back(name);
            if (comma == std::string_view::npos)
                break;
            list.remove_prefix(comma + 1);
        }
    }
    return sentinel;
}

std::string format_trial(const Trial& trial) {
    if (trial.stage == StrikeStage::path)
        return "path " + std::string(path_word(trial.path)) + " " + trial.driver;
    return "probe " + trial.driver;
}

std::optional<Trial> parse_trial(std::string_view text) {
    std::vector<std::string_view> words;
    if (!split_words(text, words))
        return std::nullopt;
    Trial trial;
    if (words[0] == strike_words[static_cast<size_t>(StrikeStage::probe)] && words.size() == 2) {
        trial.stage = StrikeStage::probe;
    } else if (
        words[0] == strike_words[static_cast<size_t>(StrikeStage::path)] && words.size() == 3
    ) {
        const auto path = parse_path(words[1]);
        if (!path)
            return std::nullopt;
        trial.stage = StrikeStage::path;
        trial.path = *path;
    } else {
        return std::nullopt;
    }
    if (!valid_driver_name(words.back()))
        return std::nullopt;
    trial.driver = std::string(words.back());
    return trial;
}

ParsedRecords parse_records(const Values& values, std::string_view engine_version) {
    ParsedRecords parsed;
    Records& records = parsed.records;
    if (const auto adapter = values.find(std::string(adapter_key)); adapter != values.end()) {
        // A value that is not in the kept form is still the adapter it names.
        records.adapter = normalise_adapter(adapter->second);
    }
    const std::string_view adapter = written_adapter(records);
    std::vector<std::string_view> words;
    for (const auto& [key, value] : values) {
        if (key == adapter_key)
            continue;
        if (key == trial_key) {
            records.trial = parse_trial(value);
            if (!records.trial)
                ++parsed.dropped;
            continue;
        }
        if (key == native_density_key) {
            if (split_words(value, words) && words.size() == 2 && valid_driver_name(words[0]) &&
                words[1].size() <= max_version_bytes)
                records.native_density =
                    NativeDensity{std::string(words[0]), std::string(words[1])};
            else
                ++parsed.dropped;
            continue;
        }
        // The keys of a driver: its name follows the key's prefix.
        const std::string_view whole_key = key;
        std::string_view prefix;
        for (const std::string_view candidate :
             {strike_prefix, failed_driver_prefix, accelerated_unusable_prefix, scale_level_prefix})
            if (whole_key.starts_with(candidate))
                prefix = candidate;
        const std::string_view driver = whole_key.substr(prefix.size());
        if (prefix.empty() || !valid_driver_name(driver) || !split_words(value, words)) {
            ++parsed.dropped;
            continue;
        }
        // Words after the stage or failure: the adapter's words, then the
        // version, then for a record possibly the told mark, or for a rung
        // the median.
        size_t adapter_start = 1;
        size_t trailing = 1;
        Strike strike;
        std::optional<RecordedFailure> failure;
        bool told = false;
        std::optional<uint32_t> rung;
        std::optional<uint32_t> median;
        if (prefix == strike_prefix) {
            const auto stage = parse_strike_stage(words);
            if (!stage) {
                ++parsed.dropped;
                continue;
            }
            strike = stage->strike;
            adapter_start = stage->adapter_start;
        } else if (prefix == scale_level_prefix) {
            rung = parse_whole(words[0], max_scale_rung);
            median = parse_median(words.back());
            trailing = 2;
        } else {
            failure = parse_failure(words[0], prefix == failed_driver_prefix);
            told = words.size() >= 4 && words.back() == told_word;
            if (told)
                trailing = 2;
        }
        // At least one word of the adapter and the version.
        if (words.size() < adapter_start + 1 + trailing ||
            (prefix == scale_level_prefix && (!rung || !median)) ||
            (prefix != strike_prefix && prefix != scale_level_prefix && !failure)) {
            ++parsed.dropped;
            continue;
        }
        const size_t version_index = words.size() - trailing;
        if (words[version_index] != engine_version ||
            join_words(words, adapter_start, version_index) != adapter) {
            // Written under another engine version or another adapter.
            ++parsed.dropped;
            continue;
        }
        DriverRecords* entry = find_entry(records, driver);
        if (entry == nullptr) {
            if (records.drivers.size() == max_drivers) {
                ++parsed.dropped;
                continue;
            }
            entry = &driver_entry(records, driver);
        }
        if (prefix == strike_prefix)
            entry->strike = strike;
        else if (prefix == scale_level_prefix)
            entry->scale_level = ScaleLevel{static_cast<uint8_t>(*rung), *median};
        else if (prefix == failed_driver_prefix && driver != software_driver)
            entry->failed_driver = Record{*failure, told};
        else if (prefix == accelerated_unusable_prefix)
            entry->accelerated_unusable = Record{*failure, told};
        else
            ++parsed.dropped; // software is never recorded failed-driver
    }
    // A driver whose only key was dropped leaves no entry behind.
    std::erase_if(records.drivers, [](const DriverRecords& entry) {
        return !holds_anything(entry);
    });
    return parsed;
}

Values format_records(const Records& records, std::string_view engine_version) {
    Values values;
    if (!records.adapter.empty())
        values.emplace(adapter_key, records.adapter);
    if (records.trial)
        values.emplace(trial_key, format_trial(*records.trial));
    if (records.native_density)
        values.emplace(
            native_density_key,
            records.native_density->driver + " " + records.native_density->version
        );
    const std::string written_under =
        std::string(written_adapter(records)) + " " + std::string(engine_version);
    const auto record_value = [&](const Record& record) {
        std::string text =
            std::string(failure_words[static_cast<size_t>(record.failure)]) + " " + written_under;
        if (record.told)
            text += " " + std::string(told_word);
        return text;
    };
    for (const DriverRecords& entry : records.drivers) {
        if (entry.strike.stage != StrikeStage::none)
            values.emplace(
                std::string(strike_prefix) + entry.driver,
                format_strike_stage(entry.strike) + " " + written_under
            );
        if (entry.failed_driver.failure != RecordedFailure::none)
            values.emplace(
                std::string(failed_driver_prefix) + entry.driver, record_value(entry.failed_driver)
            );
        if (entry.accelerated_unusable.failure != RecordedFailure::none)
            values.emplace(
                std::string(accelerated_unusable_prefix) + entry.driver,
                record_value(entry.accelerated_unusable)
            );
        if (entry.scale_level)
            values.emplace(
                std::string(scale_level_prefix) + entry.driver,
                std::to_string(entry.scale_level->rung) + " " + written_under + " " +
                    format_median(entry.scale_level->median_thousandths)
            );
    }
    return values;
}

LeftoverOutcome note_leftover(
    Records& records, LeftoverSentinel kind, const Sentinel& sentinel, const RecordRules& rules
) {
    LeftoverOutcome outcome;
    // Under 2 GiB a left-over trial is kept for a start from 2 GiB to judge.
    const bool judge_trial = records.trial.has_value() && !rules.below_two_gib;
    outcome.unclean_exit = kind != LeftoverSentinel::none || judge_trial;
    if (kind == LeftoverSentinel::read)
        outcome.last_driver = sentinel.driver;
    if (judge_trial) {
        // A system crash keeps the flushed trial even where it loses the
        // sentinel, so the trial decides.
        const Trial trial = *records.trial;
        records.trial.reset();
        outcome.change.changed = true;
        if (!valid_driver_name(trial.driver))
            return outcome;
        Strike strike;
        strike.stage = trial.stage == StrikeStage::path ? StrikeStage::path : StrikeStage::probe;
        strike.path = trial.path;
        outcome.driver = trial.driver;
        outcome.last_driver = trial.driver;
        strike_or_record(
            records,
            trial.driver,
            strike,
            rules.evidence == CrashEvidence::first_counts,
            outcome.change
        );
        return outcome;
    }
    if (kind != LeftoverSentinel::read)
        return outcome;
    if (sentinel.stage != SentinelStage::create && sentinel.stage != SentinelStage::standard)
        return outcome; // the stages a trial covers are struck only through it
    std::string_view driver = sentinel.driver;
    if (driver == software_driver) {
        // SDL presents software through whichever driver of the hint's list
        // starts first, which only a list of one names.
        driver =
            sentinel.via.size() == 1 ? std::string_view(sentinel.via.front()) : std::string_view{};
    }
    if (driver.empty() || driver == software_driver || !valid_driver_name(driver))
        return outcome;
    Strike strike;
    strike.stage =
        sentinel.stage == SentinelStage::create ? StrikeStage::create : StrikeStage::standard;
    outcome.driver = std::string(driver);
    strike_or_record(records, driver, strike, false, outcome.change);
    return outcome;
}

Change note_start_passed(
    Records& records, const Sentinel& passed, bool function_test_ran, const RecordRules& rules
) {
    Change change;
    clear_startup_strike(records, passed.driver, function_test_ran && !rules.below_two_gib, change);
    // A left-over sentinel of software through a list of one strikes that
    // driver, so a clean pass the same way clears it.
    if (passed.driver == software_driver && passed.via.size() == 1)
        clear_startup_strike(records, passed.via.front(), false, change);
    return change;
}

Change note_path_passed(Records& records, std::string_view driver, AcceleratedPath path) {
    Change change;
    DriverRecords* entry = find_entry(records, driver);
    if (entry != nullptr && entry->strike.stage == StrikeStage::path && entry->strike.path == path)
        set_strike(*entry, Strike{}, change);
    return change;
}

Change note_clean_run(Records& records, std::string_view driver, const RecordRules& rules) {
    Change change;
    DriverRecords* entry = find_entry(records, driver);
    if (entry == nullptr || entry->struck_this_run)
        return change;
    const StrikeStage stage = entry->strike.stage;
    if (stage == StrikeStage::present || (stage == StrikeStage::call && !rules.below_two_gib) ||
        stage == StrikeStage::lost || stage == StrikeStage::resets)
        set_strike(*entry, Strike{}, change);
    return change;
}

Change note_running_failure(
    Records& records,
    std::string_view driver,
    const Strike& failure,
    const DriverFacts& facts,
    const RecordRules& rules
) {
    Change change;
    if (facts.loses_device_in_ordinary_use || !valid_driver_name(driver))
        return change;
    if (failure.stage == StrikeStage::call && rules.below_two_gib)
        return change; // no accelerated-only call is made under 2 GiB
    if (failure.stage != StrikeStage::present && failure.stage != StrikeStage::call &&
        failure.stage != StrikeStage::lost && failure.stage != StrikeStage::resets)
        return change;
    if ((failure.stage == StrikeStage::present || failure.stage == StrikeStage::call) &&
        !valid_driver_name(failure.call))
        return change;
    DriverRecords& entry = driver_entry(records, driver);
    if (same_strike(entry.strike, failure) && entry.struck_this_run)
        return change; // the same failure again in the run that struck
    const bool repeated = same_strike(entry.strike, failure);
    const bool software = driver == software_driver;
    switch (failure.stage) {
    case StrikeStage::present:
        if (repeated && !software) {
            record_failed_driver(entry, RecordedFailure::present, change);
            set_strike(entry, Strike{}, change);
        } else {
            set_strike(entry, failure, change);
        }
        break;
    case StrikeStage::call:
        if (repeated) {
            record_accelerated_unusable(records, entry, RecordedFailure::call, change);
            set_strike(entry, Strike{}, change);
        } else {
            set_strike(entry, failure, change);
        }
        break;
    default: {
        const RecordedFailure recorded =
            failure.stage == StrikeStage::lost ? RecordedFailure::lost : RecordedFailure::resets;
        if (!rules.below_two_gib)
            record_accelerated_unusable(records, entry, recorded, change);
        if (repeated && !software) {
            record_failed_driver(entry, recorded, change);
            set_strike(entry, Strike{}, change);
        } else {
            set_strike(entry, failure, change);
        }
        break;
    }
    }
    return change;
}

Change note_adapter(Records& records, std::string_view description) {
    Change change;
    std::string adapter = normalise_adapter(description);
    if (adapter == unknown_adapter || adapter == records.adapter)
        return change;
    const bool known_before = !records.adapter.empty() && records.adapter != unknown_adapter;
    records.adapter = std::move(adapter);
    change.changed = true;
    if (known_before)
        merge(change, clear_failures(records));
    return change;
}

Change mark_told(Records& records, std::string_view driver) {
    Change change;
    DriverRecords* entry = find_entry(records, driver);
    if (entry == nullptr)
        return change;
    for (Record* record : {&entry->failed_driver, &entry->accelerated_unusable}) {
        if (record->failure != RecordedFailure::none && !record->told) {
            record->told = true;
            change.changed = true;
        }
    }
    return change;
}

Change clear_failures(Records& records) noexcept {
    Change change;
    for (const DriverRecords& entry : records.drivers)
        if (holds_anything(entry))
            change.changed = true;
    records.drivers.clear();
    return change;
}

// ---------------------------------------------------------------------------
// Native density

std::optional<std::string_view>
native_density_driver(const Records& records, std::string_view engine_version) noexcept {
    if (!records.native_density || records.native_density->version != engine_version)
        return std::nullopt;
    return std::string_view(records.native_density->driver);
}

Change note_density_run_end(
    Records& records,
    std::string_view driver,
    std::string_view engine_version,
    const DensityRunEnd& run,
    const RecordRules& rules
) {
    if (rules.below_two_gib)
        return {};
    if (run.dropped || (run.accelerated && run.ended_at_or_below_magnify_off))
        return forget_native_density(records, rules);
    // A key that would not be read back as written is not written.
    const bool readable = valid_driver_name(driver) && !engine_version.empty() &&
                          engine_version.size() <= max_version_bytes &&
                          engine_version.find(' ') == std::string_view::npos;
    if (!run.accelerated || !run.function_test_passed || driver == software_driver ||
        !run.flag_honoured || !run.class_measured || !run.above_budget_none || !readable)
        return {};
    if (records.native_density && records.native_density->driver == driver &&
        records.native_density->version == engine_version)
        return {};
    records.native_density = NativeDensity{std::string(driver), std::string(engine_version)};
    Change change;
    change.changed = true;
    return change;
}

Change forget_native_density(Records& records, const RecordRules& rules) noexcept {
    Change change;
    if (rules.below_two_gib || !records.native_density)
        return change;
    records.native_density.reset();
    change.changed = true;
    return change;
}

// ---------------------------------------------------------------------------
// The main menu's notice

std::optional<PendingNotice>
next_notice(const Records& records, const std::vector<std::string>& passed_over) {
    for (const DriverRecords& entry : records.drivers) {
        if (std::find(passed_over.begin(), passed_over.end(), entry.driver) != passed_over.end())
            continue;
        const bool failed_driver =
            entry.failed_driver.failure != RecordedFailure::none && !entry.failed_driver.told;
        const bool unusable = entry.accelerated_unusable.failure != RecordedFailure::none &&
                              !entry.accelerated_unusable.told;
        if (failed_driver)
            return PendingNotice{entry.driver, NoticeKind::failed_driver};
        if (unusable)
            return PendingNotice{entry.driver, NoticeKind::accelerated_unusable};
    }
    return std::nullopt;
}

NoticeAction notice_action(bool unattended, bool marks_told) noexcept {
    if (!unattended)
        return NoticeAction::show;
    return marks_told ? NoticeAction::note_and_mark : NoticeAction::note;
}

// ---------------------------------------------------------------------------
// The sentinel and the trial through a run

bool start_stage_passed(uint32_t frames, uint64_t elapsed_ns) noexcept {
    return frames >= start_stage_frames && elapsed_ns >= start_stage_ns;
}

SentinelLife start_sentinel_life(bool render_driver_named) noexcept {
    SentinelLife life;
    life.kept = !render_driver_named;
    return life;
}

namespace {

/// Returns a write that sets the sentinel alone.
///
/// @param stage the sentinel's new stage
/// @param path the path, for SentinelStage::path
/// @return the write
SentinelWrite sentinel_at(SentinelStage stage, AcceleratedPath path) noexcept {
    SentinelWrite write;
    write.set_sentinel = true;
    write.sentinel = stage;
    write.sentinel_path = path;
    return write;
}

} // namespace

SentinelWrite sentinel_step(SentinelLife& life, LifeEvent event, AcceleratedPath path) noexcept {
    if (!life.kept)
        return SentinelWrite{};
    switch (event) {
    case LifeEvent::creating:
        life.stage = SentinelStage::create;
        return sentinel_at(SentinelStage::create, AcceleratedPath::magnify);
    case LifeEvent::probing:
        life.stage = SentinelStage::standard;
        return sentinel_at(SentinelStage::standard, AcceleratedPath::magnify);
    case LifeEvent::function_test: {
        SentinelWrite write = sentinel_at(SentinelStage::probe, AcceleratedPath::magnify);
        write.write_trial = true;
        write.trial_stage = StrikeStage::probe;
        life.before_trial = life.stage;
        life.stage = SentinelStage::probe;
        life.trial = true;
        return write;
    }
    case LifeEvent::function_test_done:
        if (life.stage != SentinelStage::probe)
            return SentinelWrite{};
        life.stage = SentinelStage::standard;
        return sentinel_at(SentinelStage::standard, AcceleratedPath::magnify);
    case LifeEvent::first_accelerated_frame:
        if (life.stage != SentinelStage::standard)
            return SentinelWrite{};
        life.stage = SentinelStage::accelerated;
        return sentinel_at(SentinelStage::accelerated, AcceleratedPath::magnify);
    case LifeEvent::start_passed: {
        if (life.stage != SentinelStage::standard && life.stage != SentinelStage::accelerated)
            return SentinelWrite{};
        SentinelWrite write = sentinel_at(SentinelStage::running, AcceleratedPath::magnify);
        write.erase_trial = life.trial;
        life.stage = SentinelStage::running;
        life.trial = false;
        return write;
    }
    case LifeEvent::path_first_use: {
        // A stage that stands already covers the path's first frames.
        if (life.stage != SentinelStage::running)
            return SentinelWrite{};
        SentinelWrite write = sentinel_at(SentinelStage::path, path);
        write.write_trial = true;
        write.trial_stage = StrikeStage::path;
        write.trial_path = path;
        life.before_trial = life.stage;
        life.stage = SentinelStage::path;
        life.path = path;
        life.trial = true;
        return write;
    }
    case LifeEvent::path_passed: {
        if (life.stage != SentinelStage::path || life.path != path)
            return SentinelWrite{};
        SentinelWrite write = sentinel_at(SentinelStage::running, AcceleratedPath::magnify);
        write.erase_trial = life.trial;
        life.stage = SentinelStage::running;
        life.trial = false;
        return write;
    }
    }
    return SentinelWrite{};
}

void note_trial_unwritten(SentinelLife& life) noexcept {
    if (!life.trial)
        return;
    life.stage = life.before_trial;
    life.trial = false;
}

} // namespace oa::app::renderer_state
