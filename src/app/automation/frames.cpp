// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The automation endpoint's frame request (frames.hpp).
#include "frames.hpp"

#include "oa/app/automation_host.hpp"
#include "oa/app/check_host.hpp"
#include "oa/app/frame_coordinates.hpp"
#include "oa/formats/png.hpp"
#include "oa/ui/frontend_renderer.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace oa::app::automation {
namespace {

namespace png = oa::formats::png;
namespace renderer = oa::ui::frontend_renderer;

// The operation the frame request names.
constexpr std::string_view kFrameOp = "frame";
// Bytes a pixel of an RGB frame, one for each of its channels.
constexpr int64_t kRgbBytes = 3;
constexpr size_t kRgbChannels = 3;
// Bits a sample of an RGB frame.
constexpr uint8_t kRgbSampleBits = 8;
// No frame is wider or taller, which bounds a region's numbers.
constexpr int64_t kFrameExtentLimit = int64_t{1} << 16;
// The digits of a hash, written as text.
constexpr size_t kHashDigits = 16;
// The numbers of a rectangle: x, y, w and h.
constexpr size_t kRectNumbers = 4;

// Which frame a request asks for.
enum class FrameSource : uint8_t {
    presented, // as the game presented it in its window
    composed,  // as the game composed it, before it was drawn in the window
};

// The form an answer gives a frame in.
enum class FrameFormat : uint8_t {
    rgb,  // the pixels, three bytes each, rows from the top
    png,  // a PNG file of them
    hash, // their hash and mean colour, without the pixels
};

// Whose pixels a region's numbers count.
enum class RegionSpace : uint8_t {
    game,   // the canvas's
    window, // the window's
};

// One value a field may name.
template <typename Value>
struct FieldChoice {
    std::string_view name;
    Value value{};
};

// The values the request's source, format and space name.
constexpr std::array kSources = {
    FieldChoice<FrameSource>{"presented", FrameSource::presented},
    FieldChoice<FrameSource>{"composed", FrameSource::composed},
};

constexpr std::array kFormats = {
    FieldChoice<FrameFormat>{"rgb", FrameFormat::rgb},
    FieldChoice<FrameFormat>{"png", FrameFormat::png},
    FieldChoice<FrameFormat>{"hash", FrameFormat::hash},
};

constexpr std::array kSpaces = {
    FieldChoice<RegionSpace>{"game", RegionSpace::game},
    FieldChoice<RegionSpace>{"window", RegionSpace::window},
};

// A rectangle in whole pixels.
struct PixelRect {
    int64_t x{};
    int64_t y{};
    int64_t width{};
    int64_t height{};
};

// What a frame request asks for.
struct FrameAsk {
    FrameSource source{FrameSource::presented};
    FrameFormat format{FrameFormat::rgb};
    RegionSpace space{RegionSpace::game};
    std::optional<PixelRect> region; ///< as the request gives it; none for the whole frame
};

// The presented frame a held request waits for. The endpoint is one for
// the whole process (extension.cpp), and so is this.
struct PresentedFrame {
    renderer::Surface frame;  ///< the frame the game presented, once copied; empty until then
    bool capturing{};         ///< the game has been asked to copy the frames it presents into it
    uint32_t frames_waited{}; ///< presented stages passed without a frame
};

/// Returns the presented frame a held request waits for.
///
/// @return the one record, made on first use
PresentedFrame& presented_frame() {
    static PresentedFrame presented;
    return presented;
}

/// Reads a field that names one of a few values.
///
/// @param fields the request
/// @param field the field's name
/// @param choices the values it may name
/// @param[in,out] value the value it names; left as it is when the field is missing or null
/// @return false when the field names none of them
template <typename Value, size_t Count>
bool read_choice(
    const Json& fields,
    std::string_view field,
    const std::array<FieldChoice<Value>, Count>& choices,
    Value& value
) {
    const Json* given = fields.find(field);
    if (given == nullptr || given->type() == JsonType::null)
        return true;
    const std::string* text = given->string();
    for (const FieldChoice<Value>& choice : choices) {
        if (text != nullptr && *text == choice.name) {
            value = choice.value;
            return true;
        }
    }
    return false;
}

/// Reads a request's region: [x, y, w, h], whole numbers, the corner at
/// or after the origin and the size above 0.
///
/// @param fields the request
/// @param[out] region the region; left as it is when the field is missing or null
/// @return false when the field is no such region
bool read_region(const Json& fields, std::optional<PixelRect>& region) {
    const Json* given = fields.find("region");
    if (given == nullptr || given->type() == JsonType::null)
        return true;
    const auto elements = given->elements();
    if (given->type() != JsonType::array || elements.size() != kRectNumbers)
        return false;
    std::array<int64_t, kRectNumbers> numbers{};
    for (size_t index = 0; index < numbers.size(); ++index) {
        const std::optional<int64_t> number = elements[index].integer();
        if (!number || *number < 0 || *number > kFrameExtentLimit)
            return false;
        numbers[index] = *number;
    }
    const PixelRect given_region{numbers[0], numbers[1], numbers[2], numbers[3]};
    if (given_region.width == 0 || given_region.height == 0)
        return false;
    region = given_region;
    return true;
}

/// Reads what a frame request asks for, or refuses it.
///
/// @param request the request
/// @param[in,out] answer the answer, refused when a field is wrong
/// @param[out] ask what it asks for
/// @return false when it was refused
bool read_ask(const Request& request, Answer& answer, FrameAsk& ask) {
    const Json& fields = request.fields;
    if (!read_choice(fields, "source", kSources, ask.source)) {
        answer.refuse("bad_request", "source is presented or composed", "source");
        return false;
    }
    if (!read_choice(fields, "format", kFormats, ask.format)) {
        answer.refuse("bad_request", "format is rgb, png or hash", "format");
        return false;
    }
    if (!read_choice(fields, "space", kSpaces, ask.space)) {
        answer.refuse("bad_request", "space is game or window", "space");
        return false;
    }
    if (!read_region(fields, ask.region)) {
        answer.refuse(
            "bad_request", "region is [x, y, w, h] in whole pixels, w and h above 0", "region"
        );
        return false;
    }
    return true;
}

/// Returns the name a request gives a frame's source.
///
/// @param source the source
/// @return its name
std::string_view source_name(FrameSource source) {
    for (const auto& choice : kSources)
        if (choice.value == source)
            return choice.name;
    return {};
}

// Where a presented frame lies in the window, and the canvas it shows.
struct FramePlacement {
    PixelRect in_window;     ///< the frame's rectangle, in the window's pixels
    int64_t canvas_width{};  ///< the canvas's width, in its own pixels
    int64_t canvas_height{}; ///< the canvas's height, in its own pixels
};

/// Returns where a frame the game presented lies in its window. The game
/// presents the canvas's picture in the rectangle hello gives as the
/// canvas's, and the frame is that rectangle: on the menus the 640x480
/// canvas scaled into the window, its bars left out; in a match, drawn at
/// the window's size, the whole window.
///
/// @param host the check host
/// @param frame the frame presented
/// @return its placement
FramePlacement presented_placement(const CheckHost& host, const renderer::Surface& frame) {
    SDL_Window* window = SDL_GetWindowFromID(host.window_id(host.context));
    SDL_Renderer* sdl_renderer = window != nullptr ? SDL_GetRenderer(window) : nullptr;
    int logical_width = 0;
    int logical_height = 0;
    SDL_RendererLogicalPresentation mode = SDL_LOGICAL_PRESENTATION_DISABLED;
    SDL_FRect reckoned{};
    if (sdl_renderer != nullptr &&
        SDL_GetRenderLogicalPresentation(sdl_renderer, &logical_width, &logical_height, &mode) &&
        mode != SDL_LOGICAL_PRESENTATION_DISABLED &&
        SDL_GetRenderLogicalPresentationRect(sdl_renderer, &reckoned)) {
        const SDL_Rect drawn = drawn_frame_rect(reckoned);
        return {{drawn.x, drawn.y, frame.width, frame.height}, logical_width, logical_height};
    }
    return {{0, 0, frame.width, frame.height}, frame.width, frame.height};
}

/// Finds the pixels of a frame a request asks for: the whole frame, or its
/// region. A region of a presented frame in the game's space is one of the
/// canvas, and covers every pixel of the frame that shows part of it; one
/// in the window's space is one of the window, which must lie in the frame.
/// A region of a composed frame is in the frame's own pixels.
///
/// @param ask what the request asks for
/// @param placement where a presented frame lies; ignored for a composed one
/// @param frame the frame
/// @return the pixels, in the frame's; none when the region lies outside it
std::optional<PixelRect>
frame_region(const FrameAsk& ask, const FramePlacement& placement, const renderer::Surface& frame) {
    if (!ask.region)
        return PixelRect{0, 0, frame.width, frame.height};
    PixelRect region = *ask.region;
    if (ask.source == FrameSource::presented && ask.space == RegionSpace::game) {
        const int64_t canvas_width = placement.canvas_width;
        const int64_t canvas_height = placement.canvas_height;
        if (canvas_width <= 0 || canvas_height <= 0 || region.x + region.width > canvas_width ||
            region.y + region.height > canvas_height)
            return std::nullopt;
        const int64_t left = region.x * frame.width / canvas_width;
        const int64_t top = region.y * frame.height / canvas_height;
        const int64_t right =
            ((region.x + region.width) * frame.width + canvas_width - 1) / canvas_width;
        const int64_t bottom =
            ((region.y + region.height) * frame.height + canvas_height - 1) / canvas_height;
        region = {left, top, right - left, bottom - top};
    } else if (ask.source == FrameSource::presented) {
        region.x -= placement.in_window.x;
        region.y -= placement.in_window.y;
    }
    if (region.x < 0 || region.y < 0 || region.width <= 0 || region.height <= 0 ||
        region.x + region.width > frame.width || region.y + region.height > frame.height)
        return std::nullopt;
    return region;
}

/// Copies a region's pixels out of a frame.
///
/// @param frame the frame, whose pixels hold every row
/// @param region the region, inside the frame
/// @return its pixels, three bytes each, rows from the top
std::vector<uint8_t> region_pixels(const renderer::Surface& frame, const PixelRect& region) {
    const auto row_bytes = static_cast<size_t>(region.width * kRgbBytes);
    std::vector<uint8_t> pixels(row_bytes * static_cast<size_t>(region.height));
    for (int64_t row = 0; row < region.height; ++row) {
        const auto from =
            static_cast<size_t>(((region.y + row) * frame.width + region.x) * kRgbBytes);
        std::memcpy(
            pixels.data() + static_cast<size_t>(row) * row_bytes, frame.rgb.data() + from, row_bytes
        );
    }
    return pixels;
}

/// Returns a hash as the protocol writes it: 16 lowercase hexadecimal digits.
///
/// @param hash the hash
/// @return its text
std::string hash_text(uint64_t hash) {
    constexpr std::string_view kDigits = "0123456789abcdef";
    constexpr unsigned kDigitBits = 4;
    std::string text(kHashDigits, '0');
    for (size_t digit = kHashDigits; digit-- > 0; hash >>= kDigitBits)
        text[digit] = kDigits[hash % kDigits.size()];
    return text;
}

/// Writes a rectangle as the protocol does: [x, y, w, h].
///
/// @param[in,out] json the answer's JSON
/// @param rect the rectangle
void write_rect(JsonWriter& json, const PixelRect& rect) {
    json.begin_array();
    json.integer(rect.x);
    json.integer(rect.y);
    json.integer(rect.width);
    json.integer(rect.height);
    json.end_array();
}

/// Writes what an answer says of the pixels it gives: their size, the
/// frame's source, where a presented frame lies in the window, and the
/// request's region as it gave it.
///
/// @param[in,out] json the answer's JSON
/// @param ask what the request asked for
/// @param placement where a presented frame lies
/// @param region the pixels, in the frame's
void write_pixels_described(
    JsonWriter& json, const FrameAsk& ask, const FramePlacement& placement, const PixelRect& region
) {
    json.key("width");
    json.integer(region.width);
    json.key("height");
    json.integer(region.height);
    json.key("source");
    json.string(source_name(ask.source));
    if (ask.source == FrameSource::presented) {
        json.key("window_rect");
        write_rect(json, placement.in_window);
    }
    if (ask.region) {
        json.key("region");
        write_rect(json, *ask.region);
    }
}

/// Answers a frame request from the frame it asked for.
///
/// @param ask what the request asks for
/// @param host the check host
/// @param frame the frame
/// @param[in,out] answer the answer, begun
void answer_from(
    const FrameAsk& ask, const CheckHost& host, const renderer::Surface& frame, Answer& answer
) {
    if (frame.width == 0 || frame.height == 0 ||
        frame.rgb.size() < static_cast<size_t>(frame.width) * frame.height * kRgbBytes) {
        answer.refuse("no_window", "the game holds no such frame");
        return;
    }
    const FramePlacement placement =
        ask.source == FrameSource::presented ? presented_placement(host, frame) : FramePlacement{};
    const std::optional<PixelRect> region = frame_region(ask, placement, frame);
    if (!region) {
        answer.refuse("bad_request", "the region is outside the frame", "region");
        return;
    }
    std::vector<uint8_t> pixels = region_pixels(frame, *region);
    JsonWriter& json = answer.json;
    if (ask.format == FrameFormat::hash) {
        std::array<uint64_t, kRgbChannels> sums{};
        for (size_t at = 0; at < pixels.size(); ++at)
            sums[at % kRgbChannels] += pixels[at];
        const auto count = static_cast<uint64_t>(region->width * region->height);
        json.key("hash");
        json.string(hash_text(fnv1a_64(pixels)));
        json.key("mean");
        json.begin_array();
        for (const uint64_t sum : sums)
            json.integer(static_cast<int64_t>(sum / count));
        json.end_array();
        write_pixels_described(json, ask, placement, *region);
        return;
    }
    if (ask.format == FrameFormat::png) {
        const png::Image image{
            {static_cast<uint32_t>(region->width),
             static_cast<uint32_t>(region->height),
             kRgbSampleBits,
             png::ColorType::rgb,
             png::Interlace::none},
            {},
            pixels
        };
        if (!png::write_stored(image, &answer.payload)) {
            answer.refuse("internal", "the frame could not be written as a PNG file");
            return;
        }
    } else {
        answer.payload = std::move(pixels);
    }
    json.key("image");
    json.begin_object();
    json.key("format");
    json.string(ask.format == FrameFormat::png ? "png" : "rgb");
    write_pixels_described(json, ask, placement, *region);
    json.end_object();
}

/// Stops the game copying the frames it presents, and forgets the last one.
///
/// @param host the automation host
/// @param[in,out] presented the record of the frame waited for
void stop_capture(const AutomationHost& host, PresentedFrame& presented) {
    if (presented.capturing)
        host.stop_frame_capture(host.context, &presented.frame);
    presented.capturing = false;
    presented.frame = {};
    presented.frames_waited = 0;
}

/// Has the game copy the frames it presents into the record's frame.
///
/// @param host the automation host
/// @param[in,out] presented the record of the frame waited for
/// @return false when the game copies them somewhere else
bool start_capture(const AutomationHost& host, PresentedFrame& presented) {
    if (!host.start_frame_capture(host.context, &presented.frame))
        return false;
    presented.capturing = true;
    return true;
}

} // namespace

uint64_t fnv1a_64(std::span<const uint8_t> bytes) noexcept {
    uint64_t hash = fnv1a_offset_basis;
    for (const uint8_t byte : bytes) {
        hash ^= byte;
        hash *= fnv1a_prime;
    }
    return hash;
}

void answer_frame(Endpoint& endpoint, const Request& request, Answer& answer) {
    FrameAsk ask;
    if (!read_ask(request, answer, ask))
        return;
    if (ask.source == FrameSource::presented) {
        const CheckHost& check = endpoint.check_host();
        if (check.window_id(check.context) == 0) {
            answer.refuse("no_window", "the game has no window to present frames in");
            return;
        }
        PresentedFrame& presented = presented_frame();
        const AutomationHost& host = endpoint.automation_host();
        stop_capture(host, presented);
        if (!start_capture(host, presented)) {
            answer.refuse("busy", "the game copies the frames it presents for another purpose");
            return;
        }
    }
    answer.held = true;
}

void serve_frame_request(Endpoint& endpoint, FrameStage stage) {
    PresentedFrame& presented = presented_frame();
    const AutomationHost& host = endpoint.automation_host();
    const Request* held = endpoint.held_request();
    if (held == nullptr || held->op != kFrameOp) {
        // No frame request waits: the client left, or its request was answered.
        if (presented.capturing)
            stop_capture(host, presented);
        return;
    }
    FrameAsk ask;
    Answer answer = endpoint.begin_answer(held->id);
    if (read_ask(*held, answer, ask) && ask.source == FrameSource::presented &&
        !start_capture(host, presented)) {
        // Asked again of the runtime that runs now, which a switch of the
        // mod may have replaced since the request was taken.
        answer.refuse("busy", "the game copies the frames it presents for another purpose");
    }
    if (answer.error_code.empty()) {
        if (stage != FrameStage::presented)
            return;
        if (ask.source == FrameSource::presented && presented.frame.width == 0) {
            if (++presented.frames_waited < presented_frame_wait_limit)
                return;
            answer.refuse("no_window", "the game has presented no frame");
        } else {
            const CheckHost& check = endpoint.check_host();
            const renderer::Surface* frame = ask.source == FrameSource::presented
                                                 ? &presented.frame
                                                 : check.surface(check.context);
            if (frame != nullptr)
                answer_from(ask, check, *frame, answer);
            else
                answer.refuse("no_window", "the game holds no such frame");
        }
    }
    stop_capture(host, presented);
    endpoint.answer_held(answer);
}

} // namespace oa::app::automation
