// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Mission briefing text layout.
#include "oa/ui/campaign/briefing_text.hpp"

#include <cstdint>
#include <cstring>

namespace oa::ui::campaign {
namespace {

bool at_end(char c) {
    return c == '\0' || c == kTextEnd;
}

HighlightColor highlight_color(char code) {
    if (code == 'R')
        return HighlightColor::red;
    if (code == 'Y')
        return HighlightColor::yellow;
    if (code == 'G')
        return HighlightColor::green;
    return HighlightColor::red;
}

} // namespace

std::size_t wrap_text(
    const char* text,
    int32_t width,
    MeasureText measure,
    void* context,
    char* out,
    std::size_t capacity
) {
    if (capacity == 0)
        return 0;
    std::memset(out, 0, capacity);
    const char* src = text;
    std::size_t n = 0;
    std::size_t line_start = 0;
    while (!at_end(*src) && n + 3 < capacity) {
        out[n++] = *src;
        const char next = src[1];
        ++src;
        if ((next == ' ' || next == '\n' || next == '-') &&
            width <= measure(context, out + line_start)) {
            // Walk back to the separator that ended the previous word.
            std::size_t back = 0;
            while (back + 1 < n - line_start &&
                   src[-1 - static_cast<std::ptrdiff_t>(back)] != ' ' &&
                   src[-1 - static_cast<std::ptrdiff_t>(back)] != '-')
                ++back;
            const char separator = src[-1 - static_cast<std::ptrdiff_t>(back)];
            if (separator == ' ' || separator == '-') {
                for (std::size_t i = 0; i <= back; ++i)
                    out[n - i] = '\0';
                src -= back;
                n -= back;
                out[n - 1] = '\r';
                out[n] = '\n';
                ++n;
                line_start = n;
            }
        }
        if (*src == '\n')
            line_start = n + 1;
    }
    out[n] = '\0';
    return n;
}

std::size_t reflow_span_text(const char* text, char* out, std::size_t capacity) {
    if (capacity == 0)
        return 0;
    std::memset(out, 0, capacity);
    std::size_t n = 0;
    bool inside = false;
    char code = '\0';
    for (const char* p = text; *p != '\0' && *p != kTextEnd && n + 5 < capacity; ++p) {
        out[n] = *p;
        if (*p == kSpanMarker) {
            inside = !inside;
            code = p[1];
        }
        if (*p == '\n' && inside && n > 0) {
            out[n - 1] = kSpanMarker;
            out[n] = '\r';
            out[n + 1] = '\n';
            out[n + 2] = kSpanMarker;
            n += 3;
            out[n] = code;
        }
        ++n;
    }
    out[n] = '\0';
    return n;
}

const char* find_text_page(const char* text, int32_t lines_per_page, int32_t page) {
    if (page == 0)
        return text;
    if (text == nullptr || *text == '\0')
        return nullptr;
    const int32_t target = lines_per_page * page;
    int32_t lines = 0;
    bool found = false;
    const char* p = text;
    do {
        const char c = *p;
        if (c == kTextEnd)
            break;
        if (found)
            return p;
        ++p;
        if (c == '\n' && ++lines == target)
            found = true;
    } while (*p != '\0');
    return found ? p : nullptr;
}

void briefing_pager_reset(BriefingPager* pager, const char* text) {
    pager->text = text;
    pager->page = 0;
    pager->fresh = true;
}

void briefing_next_page(
    BriefingPager* pager,
    const BriefingRegion* region,
    MeasureText measure,
    void* context,
    BriefingPage* page
) {
    page->row_count = 0;
    page->highlight_count = 0;
    page->more = MoreLabel::none;
    if (pager->text == nullptr || region->line_height <= 0)
        return;
    if (pager->fresh) {
        pager->page = -1;
        pager->fresh = false;
    }
    const int32_t line_height = region->line_height;
    const int32_t lines = region->height / line_height;
    const int32_t x = region->x + 5;
    int32_t y = region->y + line_height / 2;
    ++pager->page;
    const char* p = find_text_page(pager->text, lines, pager->page);
    if (p == nullptr) {
        pager->page = 0;
        p = find_text_page(pager->text, lines, 0);
    }
    if (find_text_page(pager->text, lines, pager->page + 1) != nullptr)
        page->more = MoreLabel::more;
    else if (pager->page != 0)
        page->more = MoreLabel::back_to_start;
    page->page = pager->page;

    bool opens = true;
    for (int32_t row = 0; row < lines && page->row_count < kMaxRows; ++row) {
        BriefingRow& out = page->rows[page->row_count++];
        std::memset(out.text, 0, sizeof(out.text));
        out.x = x;
        out.y = y;
        y += line_height;
        std::size_t length = 0;
        char c = *p;
        while (c != '\n') {
            if (at_end(c))
                break;
            if (c == kSpanMarker) {
                if (!opens) {
                    ++p;
                    opens = true;
                } else {
                    opens = false;
                    const HighlightColor color = highlight_color(p[1]);
                    p += 2;
                    if (page->highlight_count < kMaxHighlights) {
                        BriefingHighlight& highlight = page->highlights[page->highlight_count++];
                        std::memset(highlight.text, 0, sizeof(highlight.text));
                        highlight.x = measure(context, out.text) + x;
                        highlight.y = out.y;
                        highlight.color = color;
                        for (std::size_t i = 0;
                             i < kRowTextBytes - 1 && !at_end(p[i]) && p[i] != kSpanMarker;
                             ++i)
                            highlight.text[i] = p[i];
                    }
                }
                if (at_end(*p))
                    break;
            }
            if (length < kRowTextBytes - 1)
                out.text[length++] = *p;
            ++p;
            c = *p;
        }
        if (!at_end(*p))
            ++p;
    }
}

const char* more_label_text(MoreLabel label) {
    switch (label) {
    case MoreLabel::more:
        return "MORE...";
    case MoreLabel::back_to_start:
        return "BACK TO START";
    case MoreLabel::none:
        break;
    }
    return "";
}

} // namespace oa::ui::campaign
