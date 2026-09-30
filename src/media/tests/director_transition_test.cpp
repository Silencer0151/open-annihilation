// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The director's transitions: the first and last step of each kind, the
// fade through black, the wipe's edge, the checkerboard's pattern at a
// quarter, half and three quarters, and the refusal of bad sizes and steps.

#include "oa/media/director/transition.hpp"

#include <cstdint>
#include <iostream>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace director = oa::media::director;
using oa::formats::oascript::TransitionKind;

namespace {

int failures = 0;

/// Records a failed expectation with its line.
void expect(
    bool value,
    std::string_view what,
    const std::source_location where = std::source_location::current()
) {
    if (value)
        return;
    ++failures;
    std::cerr << where.file_name() << ':' << where.line() << ": " << what << '\n';
}

/// The byte every channel of the outgoing test frame holds.
inline constexpr uint8_t outgoing_byte = 10;
/// The byte every channel of the incoming test frame holds.
inline constexpr uint8_t incoming_byte = 200;

/// Returns a frame of one byte value, RGB.
std::vector<uint8_t> filled_frame(uint32_t width, uint32_t height, uint8_t value) {
    return std::vector<uint8_t>(size_t{width} * height * 3, value);
}

/// Blends one step of two single-valued frames.
std::vector<uint8_t>
blend(TransitionKind kind, uint32_t step, uint32_t steps, uint32_t width, uint32_t height) {
    const std::vector<uint8_t> outgoing{filled_frame(width, height, outgoing_byte)};
    const std::vector<uint8_t> incoming{filled_frame(width, height, incoming_byte)};
    std::vector<uint8_t> frame(outgoing.size(), 0);
    expect(
        director::blend_transition(kind, step, steps, outgoing, incoming, width, height, frame),
        "the blend is accepted"
    );
    return frame;
}

/// Returns true when every byte of a frame is one value.
bool all_bytes(const std::vector<uint8_t>& frame, uint8_t value) {
    for (const uint8_t byte : frame)
        if (byte != value)
            return false;
    return true;
}

/// Returns one row of a wipe or checkerboard as I (incoming) and O
/// (outgoing) per pixel.
std::string row_pattern(const std::vector<uint8_t>& frame, uint32_t width, uint32_t row) {
    std::string pattern{};
    for (uint32_t column{}; column < width; ++column) {
        const size_t pixel{(size_t{row} * width + column) * 3};
        const bool incoming{
            frame[pixel] == incoming_byte && frame[pixel + 1] == incoming_byte &&
            frame[pixel + 2] == incoming_byte
        };
        const bool outgoing{
            frame[pixel] == outgoing_byte && frame[pixel + 1] == outgoing_byte &&
            frame[pixel + 2] == outgoing_byte
        };
        pattern += incoming ? 'I' : (outgoing ? 'O' : '?');
    }
    return pattern;
}

void test_checkerboard_cell() {
    expect(director::checkerboard_cell(2160) == 240, "4K cell");
    expect(director::checkerboard_cell(1080) == 120, "1080 cell");
    expect(director::checkerboard_cell(20) == 2, "cell rounds down");
    expect(director::checkerboard_cell(8) == 1, "cell at least 1");
    expect(director::checkerboard_cell(0) == 1, "cell of nothing");
}

void test_dissolve() {
    // Step 0 of 4 is at p = 1/5: a = 51.
    expect(all_bytes(blend(TransitionKind::dissolve, 0, 4, 4, 2), 48), "dissolve step 0");
    // Step 3 of 4 is at p = 4/5: a = 204.
    expect(all_bytes(blend(TransitionKind::dissolve, 3, 4, 4, 2), 161), "dissolve last step");
    // One step is at p = 1/2: a = 128, (1280 + 25600 + 128) / 256.
    expect(all_bytes(blend(TransitionKind::dissolve, 0, 1, 4, 2), 105), "dissolve half way");

    // White stays white and black stays black at any weight.
    const std::vector<uint8_t> white{filled_frame(3, 3, 255)};
    const std::vector<uint8_t> black{filled_frame(3, 3, 0)};
    std::vector<uint8_t> frame(white.size(), 7);
    for (uint32_t step{}; step < 9; ++step) {
        expect(
            director::blend_transition(
                TransitionKind::dissolve, step, 9, white, white, 3, 3, frame
            ),
            "white"
        );
        expect(all_bytes(frame, 255), "white stays white");
        expect(
            director::blend_transition(
                TransitionKind::dissolve, step, 9, black, black, 3, 3, frame
            ),
            "black"
        );
        expect(all_bytes(frame, 0), "black stays black");
    }

    // Per byte, not per frame: each channel blends on its own.
    const std::vector<uint8_t> outgoing{0, 100, 255, 30, 60, 90};
    const std::vector<uint8_t> incoming{255, 100, 0, 90, 60, 30};
    std::vector<uint8_t> mixed(6, 0);
    expect(
        director::blend_transition(TransitionKind::dissolve, 0, 1, outgoing, incoming, 2, 1, mixed),
        "mixed"
    );
    expect(mixed == std::vector<uint8_t>{128, 100, 128, 60, 60, 60}, "mixed bytes");
}

void test_fade() {
    // Four steps: q = 102, 204, 307, 409.
    expect(all_bytes(blend(TransitionKind::fade, 0, 4, 4, 2), 6), "fade step 0");
    expect(all_bytes(blend(TransitionKind::fade, 1, 4, 4, 2), 2), "fade step 1");
    expect(all_bytes(blend(TransitionKind::fade, 2, 4, 4, 2), 40), "fade step 2");
    expect(all_bytes(blend(TransitionKind::fade, 3, 4, 4, 2), 120), "fade last step");
    // An odd count passes through black in the middle.
    expect(all_bytes(blend(TransitionKind::fade, 1, 3, 4, 2), 0), "fade through black");
    expect(all_bytes(blend(TransitionKind::fade, 0, 1, 4, 2), 0), "one step is black");
    const std::vector<uint8_t> white{filled_frame(2, 2, 255)};
    std::vector<uint8_t> frame(white.size(), 7);
    expect(
        director::blend_transition(TransitionKind::fade, 0, 1, white, white, 2, 2, frame), "white"
    );
    expect(all_bytes(frame, 0), "white fades to black");
}

void test_wipe() {
    const std::vector<uint8_t> first{blend(TransitionKind::wipe, 0, 4, 10, 3)};
    for (uint32_t row{}; row < 3; ++row)
        expect(row_pattern(first, 10, row) == "IIOOOOOOOO", "wipe step 0");
    const std::vector<uint8_t> last{blend(TransitionKind::wipe, 3, 4, 10, 3)};
    for (uint32_t row{}; row < 3; ++row)
        expect(row_pattern(last, 10, row) == "IIIIIIIIOO", "wipe last step");
    const std::vector<uint8_t> none{blend(TransitionKind::wipe, 0, 20, 10, 1)};
    expect(row_pattern(none, 10, 0) == "OOOOOOOOOO", "wipe before the first column");
}

void test_checkerboard() {
    // 8 by 18 pixels: squares of 2. Three steps: p = 1/4, 1/2, 3/4.
    const std::vector<uint8_t> quarter{blend(TransitionKind::checkerboard, 0, 3, 8, 18)};
    expect(row_pattern(quarter, 8, 0) == "IOOOIOOO", "quarter, row 0");
    expect(row_pattern(quarter, 8, 1) == "IOOOIOOO", "quarter, row 1");
    expect(row_pattern(quarter, 8, 2) == "OOIOOOIO", "quarter, row 2");
    expect(row_pattern(quarter, 8, 17) == "IOOOIOOO", "quarter, last row");
    const std::vector<uint8_t> half{blend(TransitionKind::checkerboard, 1, 3, 8, 18)};
    expect(row_pattern(half, 8, 0) == "IIOOIIOO", "half, row 0");
    expect(row_pattern(half, 8, 3) == "OOIIOOII", "half, row 3");
    expect(row_pattern(half, 8, 4) == "IIOOIIOO", "half, row 4");
    const std::vector<uint8_t> three_quarters{blend(TransitionKind::checkerboard, 2, 3, 8, 18)};
    expect(row_pattern(three_quarters, 8, 0) == "IIIOIIIO", "three quarters, row 0");
    expect(row_pattern(three_quarters, 8, 2) == "IOIIIOII", "three quarters, row 2");

    // 7 by 27 pixels: squares of 3, the last column of squares one pixel
    // wide. At p = 1/3 the even squares have 2 of 3 columns.
    const std::vector<uint8_t> partial{blend(TransitionKind::checkerboard, 0, 2, 7, 27)};
    expect(row_pattern(partial, 7, 0) == "IIOOOOI", "partial squares, row 0");
    expect(row_pattern(partial, 7, 3) == "OOOIIOO", "partial squares, row 3");
    // At p = 2/3 the odd squares have 1 of 3 columns.
    const std::vector<uint8_t> later{blend(TransitionKind::checkerboard, 1, 2, 7, 27)};
    expect(row_pattern(later, 7, 0) == "IIIIOOI", "partial squares later, row 0");
    expect(row_pattern(later, 7, 3) == "IOOIIII", "partial squares later, row 3");
}

void test_refusals() {
    const std::vector<uint8_t> outgoing{filled_frame(4, 2, outgoing_byte)};
    const std::vector<uint8_t> incoming{filled_frame(4, 2, incoming_byte)};
    std::vector<uint8_t> frame(outgoing.size(), 7);
    const std::vector<uint8_t> short_frame(outgoing.size() - 1, 0);
    std::vector<uint8_t> long_frame(outgoing.size() + 3, 7);
    expect(
        !director::blend_transition(
            TransitionKind::dissolve, 2, 2, outgoing, incoming, 4, 2, frame
        ),
        "step past the end"
    );
    expect(
        !director::blend_transition(
            TransitionKind::dissolve, 0, 0, outgoing, incoming, 4, 2, frame
        ),
        "no steps"
    );
    expect(
        !director::blend_transition(
            TransitionKind::dissolve, 0, 1, short_frame, incoming, 4, 2, frame
        ),
        "short outgoing"
    );
    expect(
        !director::blend_transition(TransitionKind::wipe, 0, 1, outgoing, short_frame, 4, 2, frame),
        "short incoming"
    );
    expect(
        !director::blend_transition(
            TransitionKind::fade, 0, 1, outgoing, incoming, 4, 2, long_frame
        ),
        "long frame"
    );
    expect(
        !director::blend_transition(
            TransitionKind::checkerboard, 0, 1, outgoing, incoming, 4, 3, frame
        ),
        "wrong height"
    );
    expect(
        !director::blend_transition(
            static_cast<TransitionKind>(9), 0, 1, outgoing, incoming, 4, 2, frame
        ),
        "unknown kind"
    );
    expect(all_bytes(frame, 7) && all_bytes(long_frame, 7), "nothing written");
}

} // namespace

int main() {
    test_checkerboard_cell();
    test_dissolve();
    test_fade();
    test_wipe();
    test_checkerboard();
    test_refusals();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    return 0;
}
