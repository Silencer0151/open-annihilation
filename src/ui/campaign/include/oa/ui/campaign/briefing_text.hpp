// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Mission briefing text: word wrap, colour-span reflow and the paged
// TextRegion layout of MSNBRIEF.GUI.
//
// Briefing files mark highlighted words as `&Xword&`, where X selects the
// highlight colour (G green, Y yellow, R and anything else red). The pager
// emits plain rows plus highlight entries drawn over them as blinking words.
#pragma once

#include <cstddef>
#include <cstdint>

namespace oa::ui::campaign {

// Text byte that ends a briefing buffer as well as NUL.
inline constexpr char kTextEnd = static_cast<char>(0xff);
inline constexpr char kSpanMarker = '&';
inline constexpr std::size_t kRowTextBytes = 0x80;
inline constexpr std::size_t kMaxRows = 64;
// MSNBRIEF allocates a 15-entry blink-word table for the highlights.
inline constexpr std::size_t kMaxHighlights = 15;
// Extra bytes the reflow buffer reserves over the source length.
inline constexpr std::size_t kReflowSlack = 0x32;

// Highlight colour slots; the palette index is side_colors[side * 4 + slot].
enum class HighlightColor : uint8_t { green = 1, yellow = 2, red = 3 };

using MeasureText = int32_t (*)(void* context, const char* text);

/// Wraps text at spaces, hyphens and newlines so no line measures `width` or more.
///
/// Copies `text` into `out`, breaking before the word that first makes a line
/// reach `width`; breaks are found only at a following space, hyphen or
/// newline and are written as CR LF, the CR replacing the separator (so a
/// hyphen at a break is lost). A word longer than the line is left unbroken.
///
/// @param text Source text, ended by NUL or kTextEnd.
/// @param width Line width in pixels.
/// @param measure Measures a line in pixels.
/// @param context Context passed to `measure`.
/// @param[out] out Destination buffer; cleared first.
/// @param capacity Size of `out` in bytes; the output stops short of it.
/// @return Output length in bytes; 0 for no capacity.
std::size_t wrap_text(
    const char* text,
    int32_t width,
    MeasureText measure,
    void* context,
    char* out,
    std::size_t capacity
);

/// Closes and reopens a colour span around every line break inside it so each row carries balanced markers.
///
/// A break inside a span becomes "&\\r\\n&X", X being the span's colour code.
///
/// @param text Wrapped briefing text, ended by NUL or kTextEnd.
/// @param[out] out Destination buffer, strlen(text) + kReflowSlack bytes; cleared first.
/// @param capacity Size of `out` in bytes; the output stops short of it.
/// @return Output length in bytes; 0 for no capacity.
std::size_t reflow_span_text(const char* text, char* out, std::size_t capacity);

/// Finds the start of a page of text.
///
/// @param text Wrapped briefing text, ended by NUL or kTextEnd.
/// @param lines_per_page Rows per page.
/// @param page Page index; 0 is the text itself.
/// @return The character after newline number lines_per_page * page, or null
///         past the end (or for empty text).
[[nodiscard]] const char* find_text_page(const char* text, int32_t lines_per_page, int32_t page);

struct BriefingRegion {
    int32_t x;
    int32_t y;
    int32_t height;
    int32_t line_height; // font height + 2
};

struct BriefingRow {
    char text[kRowTextBytes];
    int32_t x;
    int32_t y;
};

struct BriefingHighlight {
    char text[kRowTextBytes];
    int32_t x;
    int32_t y;
    HighlightColor color;
};

enum class MoreLabel : uint8_t { none, more, back_to_start };

struct BriefingPage {
    BriefingRow rows[kMaxRows];
    uint32_t row_count;
    BriefingHighlight highlights[kMaxHighlights];
    uint32_t highlight_count;
    MoreLabel more;
    int32_t page;
};

// Paging state kept between MOREBAR clicks.
struct BriefingPager {
    const char* text; // wrapped and reflowed briefing
    int32_t page;
    bool fresh; // next layout restarts at page 0
};

/// Starts paging a briefing text; the next layout shows page 0.
///
/// @param[out] pager Paging state.
/// @param text Wrapped and reflowed briefing text.
void briefing_pager_reset(BriefingPager* pager, const char* text);

/// Advances to the next page of the TextRegion (wrapping to the first) and lays it out.
///
/// Rows start 5 pixels right of the region and half a line down, one per
/// line; every colour span adds a blinking highlight positioned after the row
/// text before it. The MOREBAR label becomes MORE... while a further page
/// exists, BACK TO START on the last of several pages, or nothing.
///
/// @param[in,out] pager Paging state; its page advances.
/// @param region TextRegion position, height and line height in pixels.
/// @param measure Measures text in pixels.
/// @param context Context passed to `measure`.
/// @param[out] page Rows, highlights and MOREBAR label of the page; empty
///                  without text or a line height.
void briefing_next_page(
    BriefingPager* pager,
    const BriefingRegion* region,
    MeasureText measure,
    void* context,
    BriefingPage* page
);

/// Names the MOREBAR caption of a label.
///
/// @param label MOREBAR label.
/// @return "MORE...", "BACK TO START" or "".
[[nodiscard]] const char* more_label_text(MoreLabel label);

} // namespace oa::ui::campaign
