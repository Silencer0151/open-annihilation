// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/platform/system.hpp"
#include "oa/ui/services/commands.hpp"
#include "oa/ui/services/cursor.hpp"
#include "oa/ui/services/label.hpp"
#include "oa/base/bytes.hpp"
#include "oa/base/geometry.hpp"
#include "oa/ui/services/input.hpp"
#include "oa/ui/services/prefs.hpp"
#include "oa/ui/services/sqsh_writer.hpp"
#include "oa/formats/sqsh.hpp"

#include <zlib.h>
#include "oa/ui/services/timers.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
namespace sv = oa::ui::services;
using oa::base::bytes::load_le32;
int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// Manual clock for deterministic timer tests.
struct FakeClock {
    uint32_t ms{};
};

uint32_t fake_tick(void* context) {
    return static_cast<FakeClock*>(context)->ms;
}

void fake_sleep(void* context, uint32_t ms) {
    static_cast<FakeClock*>(context)->ms += ms;
}

std::vector<int> fired;

void record_timer(void* argument) {
    fired.push_back(static_cast<int>(reinterpret_cast<intptr_t>(argument)));
}

void* tag(int value) {
    return reinterpret_cast<void*>(static_cast<intptr_t>(value));
}

void test_timers() {
    FakeClock fake{1000};
    sv::EngineClock clock{{&fake, fake_tick, fake_sleep}, 0};
    sv::TimerTable timers{};
    sv::clock_set_rate(&clock, &timers, 30);
    check(sv::clock_rate(&clock) == 30, "rate stored");
    check(timers.last_tick == 30, "baseline in engine ticks");
    check(sv::clock_now(&clock) == 30, "1000 ms at 30/s");

    check(sv::timers_add(&timers, &clock, 3, tag(1), record_timer) == 0, "first slot");
    check(sv::timers_add(&timers, &clock, 1, tag(2), record_timer) == 1, "second slot");
    check(sv::timers_add(&timers, &clock, 2, tag(3), record_timer) == 2, "third slot");
    fired.clear();
    fake.ms += 100; // 3 ticks
    sv::timers_tick(&timers, &clock);
    check((fired == std::vector<int>{1, 2, 3}), "all due timers fire in slot order");
    check(timers.slots[1].remaining == 1, "reloaded after firing");

    fired.clear();
    fake.ms += 34; // 1 tick (1134*30/1000 = 34)
    sv::timers_tick(&timers, &clock);
    check((fired == std::vector<int>{2}), "only the one-tick timer fires");

    check(sv::timers_remove(&timers, 1), "remove in range");
    check(!sv::timers_remove(&timers, 3), "index beyond add count refused");
    check(!sv::timers_remove(&timers, -1), "negative index refused");
    check(sv::timers_add(&timers, &clock, 5, tag(4), record_timer) == 1, "freed slot reused");
    check(timers.added == 4, "add count never decreases");
    check(sv::timers_remove(&timers, 3), "index 3 now below add count though slot is free");

    for (int i = 0; i < sv::timer_slot_count; ++i) {
        sv::timers_add(&timers, &clock, 1, tag(10 + i), record_timer);
    }
    check(sv::timers_add(&timers, &clock, 1, tag(99), record_timer) == -1, "full table");
    sv::timers_reset(&timers, &clock);
    check(timers.added == 0 && timers.slots[0].interval == sv::timer_slot_free, "reset frees all");
    fired.clear();
    fake.ms += 1000;
    sv::timers_tick(&timers, &clock);
    check(fired.empty(), "free slots never fire");

    sv::FrameRate rate{};
    rate.last_tick_ms = 0;
    for (int frame = 1; frame <= 20; ++frame) {
        sv::frame_rate_update(&rate, static_cast<uint32_t>(frame * 55));
    }
    check(rate.rate == 19, "19 frames counted in the first second");
    sv::frame_rate_update(&rate, 20 * 55 + 5000);
    check(
        rate.accumulated_ms == 1000 && rate.frames == 2 && rate.rate == 19,
        "backlog clamps to one window"
    );
    fake.ms = 7000;
    sv::FrameRate sampled{};
    sampled.last_tick_ms = 5000;
    check(
        sv::frame_rate_sample(&sampled, &clock.clock) == 1 && sampled.accumulated_ms == 1000,
        "two seconds is not clamped but rolls one window"
    );
}

void test_prefs() {
    oa::platform::preferences::Values values;
    const sv::PrefBackend backend = sv::pref_file_backend(&values);
    const char* app = "Total Annihilation";
    check(sv::pref_write_dword(&backend, app, "MusicVol", 0x20), "write dword");
    check(sv::pref_write_string(&backend, app, "Player", "Commander"), "write string");
    const uint8_t blob[3] = {1, 0, 0xff};
    check(sv::pref_write_binary(&backend, app, "Blob", blob, 3), "write binary");
    check(values.at("total annihilation\\musicvol") == "4:20000000", "dword stored little endian");

    uint32_t volume = 0;
    check(
        sv::pref_read_dword(&backend, "total annihilation", "MUSICVOL", &volume) && volume == 0x20,
        "case-insensitive round trip"
    );
    char name[32];
    uint32_t size = sizeof name;
    check(
        sv::pref_read(&backend, app, "Player", reinterpret_cast<uint8_t*>(name), &size),
        "read string"
    );
    check(size == 10 && std::strcmp(name, "Commander") == 0, "string includes NUL");
    uint8_t out[3]{};
    size = 3;
    check(
        sv::pref_read(&backend, app, "Blob", out, &size) && std::memcmp(out, blob, 3) == 0,
        "binary round trip"
    );

    uint32_t preserved = 0x1234;
    check(
        sv::pref_read_dword(&backend, app, "Player", &preserved),
        "oversized value still reports success"
    );
    check(preserved == 0x1234, "oversized value leaves the buffer untouched");
    check(!sv::pref_read_dword(&backend, app, "Missing", &preserved), "missing value fails");
    values["total annihilation\\broken"] = "4:zz";
    check(!sv::pref_read_dword(&backend, app, "Broken", &preserved), "malformed value fails");
}

void test_geometry() {
    const oa::Rect32 rect{10, 20, 30, 40};
    check(
        oa::base::geometry::rect_contains_point(&rect, 10, 40) &&
            oa::base::geometry::rect_contains_point(&rect, 30, 20),
        "edges inclusive"
    );
    check(
        !oa::base::geometry::rect_contains_point(&rect, 9, 25) &&
            !oa::base::geometry::rect_contains_point(&rect, 15, 41),
        "outside"
    );
    const oa::Rect32 inner{12, 22, 30, 40};
    const oa::Rect32 wide{5, 22, 30, 40};
    check(
        oa::base::geometry::rect_contains_rect(&inner, &rect) &&
            !oa::base::geometry::rect_contains_rect(&wide, &rect),
        "containment"
    );
    const oa::Rect32 touching{30, 40, 50, 60};
    const oa::Rect32 apart{31, 0, 50, 60};
    check(
        oa::base::geometry::rects_overlap(&rect, &touching) &&
            !oa::base::geometry::rects_overlap(&rect, &apart),
        "overlap is inclusive"
    );

    check(
        oa::base::geometry::trig_sin(0) == 0 && oa::base::geometry::trig_sin(0x4000) == 8192 &&
            oa::base::geometry::trig_sin(0xc000) == -8192,
        "sine quadrants"
    );
    check(
        oa::base::geometry::trig_cos(0) == 8192 && oa::base::geometry::trig_cos(0x8000) == -8192,
        "cosine quadrants"
    );
    check(
        oa::base::geometry::trig_cos(0xff80) == oa::base::geometry::trig_sin(0x3f80),
        "cosine reads the extra quarter"
    );
    check(
        oa::base::geometry::mul_high32(0x40000000, 8) == 2 &&
            oa::base::geometry::mul_high32(-1, 1) == -1,
        "high dword"
    );
    check(
        oa::base::geometry::mul_div(1000, 3, 7) == 428 &&
            oa::base::geometry::mul_div(-1000, 3, 7) == -428 &&
            oa::base::geometry::mul_div(5, 5, 0) == 0,
        "mul_div truncates toward zero"
    );

    oa::base::geometry::RotationMatrix identity{};
    oa::base::geometry::rotation_matrix_build(&identity, 0, 0, 0);
    check(
        identity.m[0][0] == 8192 && identity.m[1][1] == 8192 && identity.m[2][2] == 8192 &&
            identity.m[0][1] == 0,
        "zero angles give identity"
    );
    const int32_t v[3] = {100, -200, 300};
    int32_t out[3];
    oa::base::geometry::rotation_matrix_apply(&identity, v, out);
    check(out[0] == 100 && out[1] == -200 && out[2] == 300, "identity transform");
    oa::base::geometry::RotationMatrix quarter{};
    oa::base::geometry::rotation_matrix_build(&quarter, 0, 0, 0x4000);
    oa::base::geometry::rotation_matrix_apply(&quarter, v, out);
    check(out[0] == 200 && out[1] == 100 && out[2] == 300, "quarter turn about the third angle");

    const oa::base::geometry::Vec3f d =
        oa::base::geometry::vec3f_sub({1.0f, 2.0f, 3.0f}, {4.0f, 6.0f, 3.0f});
    check(d.x == 3.0f && d.y == 4.0f && d.z == 0.0f, "vector difference");
    check(oa::base::geometry::vec3f_length(d) == 5.0, "length");
    const oa::base::geometry::Vec3f n = oa::base::geometry::vec3f_normalize(d);
    check(n.x == 0.6f && n.y == 0.8f && n.z == 0.0f, "normalize");
    check(
        std::isnan(oa::base::geometry::vec3f_normalize({0, 0, 0}).x),
        "zero vector normalizes to NaN"
    );
    const oa::base::geometry::Vec3f z =
        oa::base::geometry::vec3f_cross({1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
    check(z.x == 0.0f && z.y == 0.0f && z.z == 1.0f, "x cross y is z");
    const oa::base::geometry::Vec3f c =
        oa::base::geometry::vec3f_cross({1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f});
    check(c.x == -3.0f && c.y == 6.0f && c.z == -3.0f, "cross product components");
    // Integer to - from per component, wrapped, then converted.
    const oa::base::geometry::Vec3f e = oa::base::geometry::vec3f_int_delta(1, 2, 3, 4, 0, -3);
    check(e.x == 3.0f && e.y == -2.0f && e.z == -6.0f, "integer point difference");
    const oa::base::geometry::Vec3f w =
        oa::base::geometry::vec3f_int_delta(-1, 0, 0, INT32_MAX, 16777217, 0);
    check(
        w.x == -2147483648.0f && w.y == 16777216.0f && w.z == 0.0f,
        "wrapped difference, rounded to float"
    );
}

std::vector<std::string> calls;

void handler_a(sv::TokenLine* line) {
    calls.push_back(std::string("a:") + sv::token_line_get(line, 1, "-"));
}

void handler_b(sv::TokenLine* line) {
    calls.push_back(std::string("b:") + std::to_string(sv::token_line_get_int(line, 1, -7)));
}

void handler_fallback(sv::TokenLine* line) {
    calls.push_back(std::string("?:") + sv::token_line_get(line, 0, ""));
}

void test_commands() {
    sv::TokenLine line;
    sv::token_line_clear(&line);
    sv::token_line_parse(&line, "  give  ARM 12 # comment", nullptr);
    check(line.count == 3, "three tokens before comment");
    check(std::strcmp(sv::token_line_get(&line, 1, ""), "ARM") == 0, "token text");
    check(
        sv::token_line_get_int(&line, 2, 0) == 12 && sv::token_line_get_int(&line, 5, -1) == -1,
        "int tokens"
    );
    check(std::strcmp(sv::token_line_get(&line, 3, "none"), "none") == 0, "fallback past count");
    sv::token_line_parse(&line, "a#b c", nullptr);
    check(
        line.count == 1 && std::strcmp(line.tokens[0], "a") == 0, "hash inside token ends the line"
    );

    std::string many;
    for (int i = 0; i < 25; ++i) {
        many += "t ";
    }
    sv::token_line_parse(&line, many.c_str(), nullptr);
    check(line.count == sv::token_slot_count, "slots capped at 20");
    const std::string longest(200, 'x');
    sv::token_line_parse(&line, ("ab " + longest + " tail").c_str(), nullptr);
    check(
        line.count == 2 && std::strlen(line.tokens[1]) == sv::token_text_limit - 3,
        "buffer fill stops parsing"
    );

    check(
        sv::parse_int("  -42xyz") == -42 && sv::parse_int("+7") == 7 && sv::parse_int("x") == 0,
        "parse_int"
    );
    check(sv::parse_int("4294967297") == 1, "parse_int wraps");
    check(
        sv::parse_double(" 2.5e2") == 250.0 && sv::parse_double("0x10") == 0.0 &&
            sv::parse_double("inf") == 0.0,
        "parse_double plain decimal only"
    );
    sv::token_line_parse(&line, "speed 1.5", nullptr);
    check(
        sv::token_line_get_double(&line, 1, 0.0f) == 1.5 &&
            sv::token_line_get_double(&line, 2, 0.25f) == 0.25,
        "double tokens"
    );

    sv::TokenLine arguments;
    sv::token_line_parse(&arguments, "first second", nullptr);
    sv::token_line_parse(&line, "cmd %1 %0 %5 %x", nullptr);
    sv::token_line_expand_arguments(&line, &arguments);
    check(
        std::strcmp(line.tokens[1], "second") == 0 && std::strcmp(line.tokens[2], "first") == 0,
        "expanded"
    );
    check(std::strcmp(line.tokens[3], "%5") == 0, "out of range left alone");
    check(std::strcmp(line.tokens[4], "first") == 0, "%x parses as index 0");

    static sv::CommandTable table{};
    const sv::CommandRegistration list[] = {
        {"echo", handler_a, 0x1},
        {"count", handler_b, 0x2},
        {nullptr, nullptr, 0},
    };
    check(sv::command_table_register(&table, list), "register list");
    check(
        sv::command_table_set(&table, "ECHO", handler_a, 0x5) && table.count == 2,
        "case-insensitive replace"
    );
    calls.clear();
    sv::token_line_parse(&line, "Echo hello", nullptr);
    check(sv::command_dispatch(&table, &line, 0x4) == 0x5, "dispatch returns full entry mask");
    sv::token_line_parse(&line, "count 9", nullptr);
    check(sv::command_dispatch(&table, &line, 0x1) == 0, "mask mismatch without fallback");
    sv::command_table_set_fallback(&table, handler_fallback, 0x8);
    sv::token_line_parse(&line, "unknown", nullptr);
    check(sv::command_dispatch(&table, &line, 0x8) == 0x8, "fallback runs");
    sv::token_line_parse(&line, "", nullptr);
    check(sv::command_dispatch(&table, &line, 0xff) == 0, "empty line does nothing");
    check((calls == std::vector<std::string>{"a:hello", "?:unknown"}), "handler order");

    calls.clear();
    const char script[] = "echo %0\r\n# note\ncount %1\nbogus";
    const uint32_t result = sv::command_run_script(
        &table, script, static_cast<int32_t>(std::strlen(script)), &arguments, 0xf
    );
    check(result == (0x5u | 0x2u | 0x8u), "script ORs results");
    check(
        (calls == std::vector<std::string>{"a:first", "b:0", "?:bogus"}),
        "script lines dispatched in order"
    );
    sv::command_table_clear_fallback(&table);
    check(table.fallback == nullptr && table.fallback_mask == 0, "fallback cleared");

    const char text[] = "one\ntwo\0three";
    check(sv::text_line_offset(text, sizeof text, 0) == 0, "line 0");
    check(sv::text_line_offset(text, sizeof text, 2) == 8, "NUL also ends a line");
    check(sv::text_line_offset(text, 4, 5) == 4, "bounded by length");
}

bool control_held = false;

bool fake_key_down(void*, uint8_t code) {
    return code == sv::key_code::control && control_held;
}

void test_input() {
    sv::KeyQueue keys{};
    sv::key_queue_set_capacity(&keys, 50);
    check(keys.capacity == 30, "capacity clamped");
    sv::key_queue_set_capacity(&keys, 4);
    check(
        sv::key_queue_push(&keys, 'a') && sv::key_queue_push(&keys, 'b') &&
            sv::key_queue_push(&keys, 'c'),
        "three fit"
    );
    check(!sv::key_queue_push(&keys, 'd'), "one slot stays free");
    check(sv::key_queue_peek(&keys) == 'a' && sv::key_queue_pop(&keys) == 'a', "fifo");
    check(sv::key_queue_push(&keys, 'd'), "wraps after a pop");
    check(
        sv::key_queue_pop(&keys) == 'b' && sv::key_queue_pop(&keys) == 'c' &&
            sv::key_queue_pop(&keys) == 'd',
        "order across wrap"
    );
    check(sv::key_queue_pop(&keys) == 0 && sv::key_queue_peek(&keys) == 0, "empty yields 0");
    sv::KeyQueue broken{};
    check(!sv::key_queue_push(&broken, 1), "zero capacity refuses");

    sv::key_queue_set_capacity(&keys, 30);
    const sv::Input input{nullptr, fake_key_down, nullptr};

    struct Case {
        int32_t vk;
        bool system;
        bool control;
        int32_t expected; // -1 = nothing queued
    };

    const Case cases[] = {
        {0x70, false, false, 0xe2}, {0x7b, false, false, 0xed}, {0x70, false, true, 0xce},
        {0x7b, false, true, 0xd9},  {0x25, false, false, 0xf4}, {0x28, false, true, 0xf7},
        {0x13, false, false, 0xf8}, {0x2e, false, false, 0xef}, {0x41, false, false, -1},
        {0x41, true, false, 'a'},   {0x41, false, true, 0xaa},  {0x35, true, false, '5'},
        {0x35, false, true, 0xc9},  {0xba, true, false, ';'},   {0xc0, false, true, '`'},
        {0xdc, true, false, '\\'},  {0xde, true, false, '\''},  {0xc1, true, false, -1},
        {0x20, true, false, -1},    {0x7c, true, false, -1},
    };
    for (const Case& c : cases) {
        control_held = c.control;
        const bool queued = sv::key_queue_push_virtual_key(&keys, &input, c.vk, c.system);
        const uint32_t code = sv::key_queue_pop(&keys);
        if (c.expected < 0) {
            check(!queued && code == 0, "key produces nothing");
        } else if (!queued || code != static_cast<uint32_t>(c.expected)) {
            std::fprintf(stderr, "vk %#x -> %#x, expected %#x\n", c.vk, code, c.expected);
            check(false, "virtual key translation");
        }
    }

    sv::PointerEvent storage[3]{};
    sv::PointerQueue pointer{3, storage, 0, 0, {}};
    const sv::PointerEvent move{5, 6, 0, 1, 0x200, 0};
    sv::pointer_queue_set_current(&pointer, &move);
    sv::PointerEvent out{};
    check(
        !sv::pointer_queue_pop(&pointer, &out) && out.x == 5 && out.message == 0x200,
        "empty returns latest move"
    );
    const sv::PointerEvent press{7, 8, 1, 2, 0x201, 1};
    check(
        sv::pointer_queue_push(&pointer, &press) && sv::pointer_queue_push(&pointer, &press),
        "two fit"
    );
    check(!sv::pointer_queue_push(&pointer, &press), "ring full");
    check(
        sv::pointer_queue_peek(&pointer, &out) && out.message == 0x201 && pointer.tail == 0,
        "peek keeps"
    );
    check(
        sv::pointer_queue_pop(&pointer, &out) && sv::pointer_queue_pop(&pointer, &out), "pop both"
    );
    check(pointer.tail == 2 && !sv::pointer_queue_pop(&pointer, &out), "drained");
    check(sv::pointer_queue_push(&pointer, &press) && pointer.head == 0, "head wraps at capacity");
    sv::pointer_queue_reset(&pointer);
    check(pointer.head == 0 && pointer.tail == 0, "reset");
}

// Records every drawing call as text so tests can assert the exact sequence.
struct DrawLog {
    std::vector<std::string> calls;
    oa::Surface screen{640, 480, 640, nullptr, 0, 0, 0, 0, {0, 0, 639, 479}, 0, 0, 0};
    oa::Surface surfaces[3]{};
    int created = 0;
    int freed = 0;
};

std::string surface_name(DrawLog* log, const oa::Surface* surface) {
    if (surface == nullptr) {
        return "screen";
    }
    for (int i = 0; i < 3; ++i) {
        if (surface == &log->surfaces[i]) {
            return "s" + std::to_string(i);
        }
    }
    return "locked";
}

void log_call(void* context, const std::string& text) {
    static_cast<DrawLog*>(context)->calls.push_back(text);
}

void fake_blit(void* context, oa::Surface* dst, const oa::Surface* src, int32_t x, int32_t y) {
    auto* log = static_cast<DrawLog*>(context);
    log_call(
        context,
        "blit " + surface_name(log, dst) + "<-" + surface_name(log, src) + " " + std::to_string(x) +
            "," + std::to_string(y)
    );
}

void fake_copy(void* context, oa::Surface* dst, const oa::Surface* src, int32_t x, int32_t y) {
    auto* log = static_cast<DrawLog*>(context);
    log_call(
        context,
        "copy " + surface_name(log, dst) + "<-" + surface_name(log, src) + " " + std::to_string(x) +
            "," + std::to_string(y)
    );
}

void fake_sprite(void* context, oa::Surface* dst, const oa::Sprite*, int32_t x, int32_t y) {
    auto* log = static_cast<DrawLog*>(context);
    log_call(
        context,
        "sprite " + surface_name(log, dst) + " " + std::to_string(x) + "," + std::to_string(y)
    );
}

void fake_reset_clip(void* context, oa::Surface* dst) {
    log_call(context, "clip " + surface_name(static_cast<DrawLog*>(context), dst));
}

bool fake_lock_screen(void* context, oa::Surface* out) {
    *out = static_cast<DrawLog*>(context)->screen;
    return true;
}

void fake_present(void* context, const oa::Surface*, const oa::Rect32* a, const oa::Rect32* b) {
    log_call(
        context,
        "present " + std::to_string(a->x1) + "," + std::to_string(a->y1) + "," +
            std::to_string(a->x2) + "," + std::to_string(a->y2) + " " + std::to_string(b->x1) +
            "," + std::to_string(b->y1) + "," + std::to_string(b->x2) + "," + std::to_string(b->y2)
    );
}

oa::Surface* fake_create(void* context, const char*, int32_t, int32_t) {
    auto* log = static_cast<DrawLog*>(context);
    return &log->surfaces[log->created++];
}

void fake_free(void* context, oa::Surface*) {
    ++static_cast<DrawLog*>(context)->freed;
}

int32_t pointer_x = 100;
int32_t pointer_y = 50;

void fake_cursor_position(void*, int32_t* x, int32_t* y) {
    *x = pointer_x;
    *y = pointer_y;
}

void test_cursor() {
    DrawLog log;
    const sv::CursorDraw draw{
        &log,
        fake_blit,
        fake_copy,
        fake_sprite,
        fake_reset_clip,
        fake_lock_screen,
        fake_present,
        fake_create,
        fake_free
    };
    const sv::Input input{nullptr, fake_key_down, fake_cursor_position};
    oa::platform::TokenLock lock;
    oa::platform::token_lock_reset(&lock);
    static sv::CursorState state{};
    state.draw = &draw;
    state.input = &input;
    state.frame_lock = &lock;
    check(sv::pointer_capture_init(&state, 8, 0), "capture init");
    check(log.created == 3 && state.hide_count == 1 && state.thread_started == 0, "init state");
    const oa::Sprite arrow{16, 20, 2, 3, 0, 0, 0, 0, 0, nullptr, nullptr};
    sv::cursor_set_image(&state, &arrow);
    check(sv::cursor_image(&state) == &arrow && lock.word.load() == 0, "image set under lock");

    sv::cursor_show(&state);
    check(state.hide_count == 0 && state.x == 98 && state.y == 47, "show places hotspot");
    check(
        log.surfaces[0].width == 16 && log.surfaces[0].pitch == 16 && log.surfaces[0].height == 20,
        "save shaped"
    );
    check(
        (log.calls == std::vector<std::string>{"blit s0<-screen -98,-47", "sprite screen 100,50"}),
        "show sequence"
    );
    log.calls.clear();
    sv::cursor_hide(&state);
    sv::cursor_hide(&state);
    check(
        state.hide_count == 2 && (log.calls == std::vector<std::string>{"blit screen<-s0 98,47"}),
        "only the first hide restores"
    );
    sv::cursor_show(&state);
    check(state.hide_count == 1 && log.calls.size() == 1, "nested show stays hidden");

    log.calls.clear();
    state.overlay_enabled = 1;
    pointer_x = 110;
    pointer_y = 55;
    sv::cursor_repaint(&state);
    const std::vector<std::string> repaint{
        "copy s1<-locked -108,-52",
        "copy s1<-s0 -10,-5",
        "copy s2<-s1 0,0",
        "clip s2",
        "sprite s2 2,3",
        "copy s0<-s2 10,5",
        "copy locked<-s0 98,47",
        "copy locked<-s2 108,52",
        "copy s0<-s1 0,0",
        "present 98,47,114,67 108,52,124,72",
    };
    check(log.calls == repaint, "threaded repaint sequence");
    check(state.x == 108 && state.y == 52, "repaint moves the saved origin");

    state.threaded = 1;
    log.calls.clear();
    sv::cursor_hide(&state);
    sv::cursor_show(&state);
    sv::cursor_draw(&state);
    check(log.calls.empty(), "threaded cursor ignores synchronous calls");
    state.threaded = 0;

    check(sv::cursor_thread_start(&state), "thread starts");
    check(state.threaded == 1, "thread owns the cursor");
    oa::platform::sleep_ms(50);
    state.display_flags = sv::display_flag_threaded_cursor;
    sv::pointer_capture_shutdown(&state);
    check(
        state.thread_started == 0 && state.stop_request.load() == 0,
        "thread stopped and acknowledged"
    );
    check(log.freed == 3 && state.pointer.events == nullptr, "shutdown frees");
    sv::pointer_capture_shutdown(&state);
    check(log.freed == 3, "second shutdown is a no-op");
}

struct LabelLog {
    std::vector<std::string> calls;
    bool locked = false;
};

int32_t fake_measure(void*, const void*, const char* text) {
    return static_cast<int32_t>(std::strlen(text)) * 6;
}

bool fake_label_lock(void* context, oa::Surface* out) {
    static_cast<LabelLog*>(context)->locked = true;
    *out = oa::Surface{320, 200, 320, nullptr, 0, 0, 0, 0, {0, 0, 319, 199}, 0, 0, 0};
    return true;
}

void fake_label_unlock(void* context) {
    static_cast<LabelLog*>(context)->calls.push_back("unlock");
}

void fake_draw_label(
    void* context,
    oa::Surface*,
    const sv::TextState* state,
    const char*,
    int32_t x,
    int32_t y,
    int32_t
) {
    static_cast<LabelLog*>(context)->calls.push_back(
        std::to_string(state->color) + "@" + std::to_string(x) + "," + std::to_string(y)
    );
}

void test_label() {
    LabelLog log;
    const sv::LabelDraw draw{
        &log, fake_measure, fake_label_lock, fake_label_unlock, fake_draw_label
    };
    sv::TextState text{nullptr, 9, 0, 4};
    sv::label_draw_outlined(&text, &draw, nullptr, "Paused", 0, 255, 100);
    check(log.locked, "null target locks the screen");
    check(
        (log.calls ==
         std::vector<std::string>{
             "0@141,100", "0@143,100", "0@142,99", "0@142,101", "255@142,100", "unlock"
         }),
        "outline then text, centred"
    );
    check(text.background == 4, "outline leaves the background transparent");
    log.calls.clear();
    oa::Surface target{101, 20, 101, nullptr, 0, 0, 0, 0, {0, 0, 100, 19}, 0, 0, 0};
    sv::label_draw_outlined(
        &text, &draw, &target, "ab", sv::text_color_keep, sv::text_color_keep, 5
    );
    check(
        log.calls.size() == 5 && log.calls[4] == "255@44,5", "keep colours, odd width rounds down"
    );
}

void test_sqsh() {
    std::vector<uint8_t> input(3000);
    for (std::size_t i = 0; i < input.size(); ++i) {
        input[i] = static_cast<uint8_t>((i * 7) % 13 + (i / 100));
    }
    for (int32_t compression : {1, 2}) {
        for (int32_t encrypt : {0, 1}) {
            std::vector<uint8_t> out(4096, 0xcc);
            uint32_t size = static_cast<uint32_t>(out.size());
            check(
                sv::sqsh_write_chunk(out.data(), &size, input.data(), 3000, compression, encrypt) ==
                    sv::SqshWriteStatus::ok,
                "chunk written"
            );
            check(
                std::memcmp(out.data(), "SQSH", 4) == 0 && out[4] == 2 && out[5] == compression &&
                    out[6] == encrypt,
                "chunk header"
            );
            const uint32_t stored = load_le32(&out[7]);
            check(size == stored + 19 && load_le32(&out[11]) == 3000 && stored < 3000, "sizes");
            std::vector<uint8_t> payload(out.begin() + 19, out.begin() + 19 + stored);
            check(
                load_le32(&out[15]) == oa::formats::sqsh::chunk_checksum(payload),
                "checksum over stored bytes"
            );
            if (encrypt != 0) {
                oa::formats::sqsh::decrypt_chunk(payload);
            }
            std::vector<uint8_t> decoded;
            if (compression == 1) {
                decoded = oa::formats::sqsh::decode_lz77(payload, 3000).value.value();
            } else {
                decoded.resize(3000);
                uLongf length = 3000;
                check(
                    uncompress(decoded.data(), &length, payload.data(), stored) == Z_OK &&
                        length == 3000,
                    "zlib payload"
                );
            }
            check(decoded == input, "payload round trip");
        }
    }
    std::vector<uint8_t> small(40);
    uint32_t size = 40;
    check(
        sv::sqsh_write_chunk(small.data(), &size, input.data(), 3000, 1, 0) ==
            sv::SqshWriteStatus::output_too_small,
        "lz77 overflow refused"
    );
    size = 40;
    check(
        sv::sqsh_write_chunk(small.data(), &size, input.data(), 3000, 2, 0) ==
            sv::SqshWriteStatus::output_too_small,
        "zlib overflow refused"
    );
    size = 40;
    check(
        sv::sqsh_write_chunk(small.data(), &size, input.data(), 10, 0, 0) ==
                sv::SqshWriteStatus::ok &&
            size == 29 && load_le32(&small[7]) == 10,
        "type 0 writes only the header"
    );
    check(
        sv::sqsh_write_chunk(small.data(), &size, input.data(), 10, 4, 0) ==
            sv::SqshWriteStatus::bad_compression,
        "type 4 refused"
    );
    check(
        sv::sqsh_write_chunk(small.data(), &size, input.data(), 0, 1, 0) ==
            sv::SqshWriteStatus::invalid_argument,
        "empty input refused"
    );
}
} // namespace

int main() {
    test_timers();
    test_prefs();
    test_geometry();
    test_commands();
    test_input();
    test_cursor();
    test_label();
    test_sqsh();
    if (failures != 0) {
        std::fprintf(stderr, "%d engine service check(s) failed\n", failures);
        return 1;
    }
    std::puts("engine services ok");
    return 0;
}
