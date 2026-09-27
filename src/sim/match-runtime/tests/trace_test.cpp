// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The trace stream of a short fight: two runs of the same match write
// the same stream byte for byte, the last tick's total digest and the unit
// dump match the values pinned below on every platform, and a health point
// taken off one unit between two ticks moves the next tick's unit fold and
// units section alone. Writes <stem>-{a,b,c}.{trace,units} for the
// comparator tests.
#include "combat_fixture.hpp"

#include "oa/sim/match_runtime/match_trace.hpp"
#include "oa/sim/match_runtime/script.hpp"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

using namespace combat_fixture;
namespace trace = oa::sim::trace;

constexpr uint32_t fight_ticks = 90;
constexpr uint32_t perturbed_after = 10;
constexpr std::size_t records_per_tick = 1 + trace::section_count;
constexpr uint32_t fixture_seed = 1;

// The fight's last tick: its total section digest, and the unit dump's size
// and 64-bit FNV-1a with "\n" line ends (the dump is a text file, so a
// platform may end its lines with "\r\n"). The total moves only when what the
// simulation computes changes; the dump pins also cover the dump's text
// format and field labels, so relabelling a field moves them too. Update them
// only with the reason in the commit message.
constexpr uint64_t pinned_final_total = 0xcea2f691000b69b5ull;
constexpr uint64_t pinned_unit_dump_bytes = 166882;
constexpr uint64_t pinned_unit_dump_hash = 0x1288c0ab1348b4f6ull;
constexpr uint64_t fnv_basis = 0xcbf29ce484222325ull;
constexpr uint64_t fnv_prime = 0x100000001b3ull;

struct Record {
    uint32_t words[6]{};
};

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

uint32_t word(const std::string& bytes, std::size_t offset) {
    uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index)
        value |= static_cast<uint32_t>(static_cast<uint8_t>(bytes.at(offset + index)))
                 << (8 * index);
    return value;
}

/// Returns the 64-bit FNV-1a of a byte string.
///
/// @param bytes bytes to hash
/// @return the hash
uint64_t fnv1a(const std::string& bytes) {
    uint64_t hash = fnv_basis;
    for (const char byte : bytes) {
        hash ^= static_cast<uint8_t>(byte);
        hash *= fnv_prime;
    }
    return hash;
}

/// Checks a pinned value, printing the value found when it differs.
///
/// @param what name printed
/// @param found value the run produced
/// @param pinned value pinned in this file
/// @return whether they are equal
bool matches_pinned(const char* what, uint64_t found, uint64_t pinned) {
    if (found != pinned)
        std::cerr << what << ": found 0x" << std::hex << found << ", pinned 0x" << pinned
                  << std::dec << '\n';
    return found == pinned;
}

std::vector<Record> records(const std::string& bytes) {
    CHECK(bytes.size() >= trace::header_size);
    CHECK(word(bytes, 0) == trace::stream_magic && word(bytes, 4) == trace::stream_version);
    CHECK(word(bytes, 8) == trace::record_size && word(bytes, 12) == fixture_seed);
    CHECK((bytes.size() - trace::header_size) % trace::record_size == 0);
    std::vector<Record> out((bytes.size() - trace::header_size) / trace::record_size);
    for (std::size_t index = 0; index < out.size(); ++index)
        for (std::size_t w = 0; w < 6; ++w)
            out[index].words[w] =
                word(bytes, trace::header_size + index * trace::record_size + w * 4);
    return out;
}

// Two units a side within gun range: both of player 0's attack one of player
// 1's, which attacks back; with `perturb`, the other of player 1's units loses
// a health point after tick `perturbed_after`.
void fight(const std::string& stem, bool perturb) {
    Fixture f;
    auto& first = f.spawn(0, 64, 64);
    auto& second = f.spawn(0, 64, 96);
    auto& target = f.spawn(1, 160, 64);
    auto& marked = f.spawn(1, 160, 96);
    CHECK(f.match->record_trace(stem + ".trace", stem + ".units"));
    CHECK(f.match->issue_attack(first.unit_index, target.unit_index, false));
    CHECK(f.match->issue_attack(second.unit_index, target.unit_index, false));
    CHECK(f.match->issue_attack(target.unit_index, first.unit_index, false));
    for (uint32_t tick = 1; tick <= fight_ticks; ++tick) {
        f.run(1);
        if (perturb && tick == perturbed_after)
            marked.record.health = static_cast<int16_t>(marked.record.health - 1);
    }
}

const Record& tick_record_at(const std::vector<Record>& stream, uint32_t tick) {
    return stream.at((tick - 1) * records_per_tick);
}

const Record& section_at(const std::vector<Record>& stream, uint32_t tick, trace::Section section) {
    return stream.at((tick - 1) * records_per_tick + 1 + static_cast<std::size_t>(section));
}

bool same_section(
    const std::vector<Record>& a, const std::vector<Record>& b, uint32_t tick, trace::Section s
) {
    return std::memcmp(&section_at(a, tick, s), &section_at(b, tick, s), sizeof(Record)) == 0;
}

void runs_repeat(const std::string& stem) {
    fight(stem + "-a", false);
    fight(stem + "-b", false);
    const auto a = read_file(stem + "-a.trace");
    CHECK(!a.empty() && a == read_file(stem + "-b.trace"));
    CHECK(read_file(stem + "-a.units") == read_file(stem + "-b.units"));
    const auto stream = records(a);
    CHECK(stream.size() == fight_ticks * records_per_tick);
    bool shots = false;
    for (uint32_t tick = 1; tick <= fight_ticks; ++tick) {
        const auto& record = tick_record_at(stream, tick);
        CHECK(
            record.words[0] == static_cast<uint32_t>(trace::RecordKind::tick) &&
            record.words[1] == tick
        );
        for (uint32_t s = 0; s < trace::section_count; ++s) {
            const auto& section = section_at(stream, tick, static_cast<trace::Section>(s));
            CHECK(section.words[0] == static_cast<uint32_t>(trace::RecordKind::section));
            CHECK(section.words[1] == tick && section.words[2] == s);
        }
        shots = shots || section_at(stream, tick, trace::Section::projectiles).words[5] != 0;
    }
    // All four units are mobile and live at the start; the fight fires shots,
    // spends health and draws from the shared generator.
    CHECK(tick_record_at(stream, 1).words[2] == 4u);
    CHECK(section_at(stream, 1, trace::Section::units).words[5] == 4u);
    CHECK(shots);
    CHECK(tick_record_at(stream, 1).words[3] != tick_record_at(stream, fight_ticks).words[3]);
    CHECK(tick_record_at(stream, 1).words[5] != tick_record_at(stream, fight_ticks).words[5]);
    const auto& total = section_at(stream, fight_ticks, trace::Section::total);
    const uint64_t final_total = total.words[3] | (static_cast<uint64_t>(total.words[4]) << 32);
    auto units = read_file(stem + "-a.units");
    std::erase(units, '\r');
    // Every pinned value is compared, so a failure prints all that moved.
    bool pinned = matches_pinned("final total digest", final_total, pinned_final_total);
    pinned = matches_pinned("unit dump bytes", units.size(), pinned_unit_dump_bytes) && pinned;
    pinned = matches_pinned("unit dump hash", fnv1a(units), pinned_unit_dump_hash) && pinned;
    CHECK(pinned);
    std::cout << "match trace runs repeat passed\n";
}

void perturbation_moves_units_only(const std::string& stem) {
    fight(stem + "-c", true);
    const auto a = records(read_file(stem + "-a.trace"));
    const auto c = records(read_file(stem + "-c.trace"));
    CHECK(a.size() == c.size());
    const auto before = perturbed_after * records_per_tick * sizeof(Record);
    CHECK(std::memcmp(a.data(), c.data(), before) == 0);
    const auto tick = perturbed_after + 1;
    const auto& record_a = tick_record_at(a, tick);
    const auto& record_c = tick_record_at(c, tick);
    CHECK(record_a.words[2] == record_c.words[2]);
    CHECK(record_a.words[3] != record_c.words[3]);
    CHECK(record_a.words[4] == record_c.words[4] && record_a.words[5] == record_c.words[5]);
    CHECK(!same_section(a, c, tick, trace::Section::units));
    CHECK(!same_section(a, c, tick, trace::Section::total));
    for (auto s :
         {trace::Section::weapons,
          trace::Section::orders,
          trace::Section::projectiles,
          trace::Section::players,
          trace::Section::random})
        CHECK(same_section(a, c, tick, s));
    std::cout << "match trace perturbation passed\n";
}

// A second record_trace restarts the stream from the next tick, as a new
// match does. One whose stream or unit dump cannot be created is refused,
// writes no header, and leaves the running stream going.
void restart_and_refusal(const std::string& stem) {
    Fixture f;
    f.spawn(0, 64, 64);
    const auto restarted = stem + "-restart.trace";
    CHECK(f.match->record_trace(restarted));
    f.run(3);
    CHECK(f.match->record_trace(restarted));
    f.run(2);
    const auto stream = records(read_file(restarted));
    CHECK(stream.size() == 2 * records_per_tick);
    CHECK(tick_record_at(stream, 1).words[1] == 4u && stream.at(records_per_tick).words[1] == 5u);

    const auto missing = stem + "-missing/";
    CHECK(!f.match->record_trace(missing + "stream.trace"));
    const auto orphan = stem + "-orphan.trace";
    CHECK(!f.match->record_trace(orphan, missing + "stream.units"));
    f.run(2);
    CHECK(read_file(orphan).empty());
    CHECK(records(read_file(restarted)).size() == 4 * records_per_tick);
    std::cout << "match trace restart and refusal passed\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: oa-match-trace-test OUTPUT_STEM\n";
        return 2;
    }
    try {
        runs_repeat(argv[1]);
        perturbation_moves_units_only(argv[1]);
        restart_and_refusal(argv[1]);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
