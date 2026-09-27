// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/formats/hpi.hpp"
#include "oa/present/model/model_library.hpp"
#include "oa/present/display.hpp"
#include "oa/present/palette_tables.hpp"
#include "oa/test/game_assets.hpp"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int failures = 0;

#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #x);             \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

using namespace oa::present::model;
using oa::formats::objects3d::FixedVector3;
using oa::formats::objects3d::Model;
using oa::formats::objects3d::Object;
using oa::formats::objects3d::Primitive;

constexpr int32_t unit = 0x10000;

Primitive primitive(
    std::vector<uint16_t> indices, std::string texture = {}, int32_t color = 0, bool colored = false
) {
    Primitive p;
    p.vertex_indices = std::move(indices);
    p.texture_name = std::move(texture);
    p.color_index = color;
    p.is_colored = colored ? 1 : 0;
    return p;
}

// Sequences of 1, 3 and 10 frames with one byte of pixels each.
struct Fixture {
    std::vector<std::unique_ptr<oa::formats::gaf::Sequence>> sequences;
    std::vector<uint8_t> pixels = std::vector<uint8_t>(64, 0x33);
    TextureLibrary library;

    void add(const std::string& name, int frames, bool team) {
        auto sequence = std::make_unique<oa::formats::gaf::Sequence>();
        sequence->name = name;
        TextureSequence entry;
        for (int i = 0; i < frames; ++i) {
            oa::formats::gaf::Frame frame;
            frame.width = 1;
            frame.height = 1;
            frame.duration = 1;
            sequence->frames.push_back(frame);
            oa::Sprite sprite{};
            sprite.width = 1;
            sprite.height = 1;
            sprite.data = pixels.data() + i;
            entry.frames.push_back(sprite);
        }
        entry.sequence = sequence.get();
        entry.team_archive = team;
        library.sequences.emplace(name, std::move(entry));
        sequences.push_back(std::move(sequence));
    }
};

// The selection primitive moves to the front, then the rest are ordered by
// the mean height of their vertices.
void test_sort_object_primitives() {
    Object object;
    object.vertices = {{0, 5 * unit, 0}, {0, 1 * unit, 0}, {0, 3 * unit, 0}, {0, 0, 0}};
    object.primitives = {primitive({0}), primitive({1}), primitive({2}), primitive({3})};
    object.selection_primitive = 3;
    std::vector<uint32_t> order;
    sort_object_primitives(object, order);
    CHECK(order.size() == 4);
    CHECK(order[0] == 3);
    CHECK(order[1] == 1);
    CHECK(order[2] == 2);
    CHECK(order[3] == 0);
    // Without one the first record still stays in place.
    object.selection_primitive = -1;
    sort_object_primitives(object, order);
    CHECK(order[0] == 0);
    CHECK(order[1] == 3);
    CHECK(order[2] == 1);
    CHECK(order[3] == 2);
}

// Texture binding: fixed, animated, team and missing textures.
void test_prepare_model() {
    Fixture fixture;
    fixture.add("plain", 1, false);
    fixture.add("blink", 3, false);
    fixture.add("logo", 10, true);
    ModelLibrary library;
    library.textures = std::move(fixture.library);
    auto source = std::make_shared<Model>();
    Object object;
    object.vertices = {{0, 0, 0}, {unit, 0, 0}, {unit, 0, unit}, {0, 0, unit}};
    object.primitives = {
        primitive({0, 1, 2, 3}, "Plain"),
        primitive({0, 1, 2, 3}, "blink"),
        primitive({0, 1, 2, 3}, "LOGO"),
        primitive({0, 1, 2, 3}, "absent"),
        primitive({0, 1, 2}, {}, 7, true)
    };
    source->objects.push_back(object);
    const std::shared_ptr<const Model> model = source;
    const PreparedModel& prepared = prepare_model(library, model);
    CHECK(&prepare_model(library, model) == &prepared);
    CHECK(prepared.model == model.get());
    CHECK(prepared.objects.size() == 1);
    const auto& primitives = prepared.objects[0].primitives;
    CHECK(primitives.size() == 5);
    CHECK(!prepared.objects[0].skips_first);
    const PreparedPrimitive& plain = primitives[0];
    CHECK(plain.flags == 0);
    CHECK(primitive_texture(plain, false, 0) == &library.textures.sequences.at("plain").frames[0]);
    const PreparedPrimitive& blink = primitives[1];
    CHECK(blink.flags == primitive_animated);
    const PreparedPrimitive& logo = primitives[2];
    CHECK(logo.flags == (primitive_animated | primitive_team));
    CHECK(primitive_texture(logo, false, 4) == &library.textures.sequences.at("logo").frames[4]);
    CHECK(primitive_texture(logo, true, 9) == &library.textures.sequences.at("logo").frames[9]);
    CHECK(primitive_texture(logo, false, 10) == nullptr);
    const PreparedPrimitive& absent = primitives[3];
    CHECK(absent.flags == primitive_colored);
    CHECK(absent.color == missing_texture_color);
    const PreparedPrimitive& colored = primitives[4];
    CHECK(colored.flags == primitive_colored);
    CHECK(colored.color == 7);
    // Only the plain animation is stepped.
    CHECK(library.cursors.size() == 1);
    CHECK(primitive_texture(blink, false, 0) == &library.textures.sequences.at("blink").frames[0]);
    step_texture_animations(library);
    CHECK(primitive_texture(blink, false, 0) == &library.textures.sequences.at("blink").frames[1]);
    CHECK(primitive_texture(blink, true, 0) == &library.textures.sequences.at("blink").frames[0]);
}

// A live model is prepared once: the same handle, or any handle sharing its
// ownership, gets the same entry and registers no further cursors.
void test_prepare_model_once() {
    Fixture fixture;
    fixture.add("blink", 3, false);
    ModelLibrary library;
    library.textures = std::move(fixture.library);
    auto source = std::make_shared<Model>();
    Object object;
    object.primitives = {primitive({}, "blink")};
    source->objects.push_back(object);
    const std::shared_ptr<const Model> model = source;
    const PreparedModel& prepared = prepare_model(library, model);
    CHECK(&prepare_model(library, model) == &prepared);
    const std::shared_ptr<const Model> shared = model;
    CHECK(&prepare_model(library, shared) == &prepared);
    CHECK(find_prepared_model(library, *model) == &prepared);
    CHECK(library.models.size() == 1);
    CHECK(library.cursors.size() == 1);
    // A handle with its own control block that points at the live model (an
    // aliasing handle) reaches the same entry and frees nothing.
    const std::shared_ptr<const Model> aliasing(std::make_shared<int>(0), model.get());
    CHECK(&prepare_model(library, aliasing) == &prepared);
    CHECK(library.models.size() == 1);
    CHECK(library.cursors.size() == 1);
    bool rejected = false;
    try {
        prepare_model(library, std::shared_ptr<const Model>{});
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    CHECK(rejected);
    CHECK(library.models.size() == 1);
}

/// Adds the textures the address-reuse and release tests bind: a fixed texture, three plain animations and a team texture.
///
/// @param[in,out] fixture texture fixture the sequences are added to
void add_reuse_textures(Fixture& fixture) {
    fixture.add("plain", 1, false);
    fixture.add("flash", 2, false);
    fixture.add("blink", 3, false);
    fixture.add("pulse", 4, false);
    fixture.add("logo", 10, true);
}

/// Builds one flat object of three animated primitives, which register three cursors.
///
/// @return the model
Model three_blinks() {
    Model model;
    Object object;
    object.vertices = {{0, 0, 0}, {unit, 0, 0}, {unit, 0, unit}, {0, 0, unit}};
    object.primitives = {
        primitive({0, 1, 2, 3}, "blink"),
        primitive({0, 1, 2, 3}, "blink"),
        primitive({0, 1, 2, 3}, "blink")
    };
    model.objects.push_back(object);
    return model;
}

/// Builds a model of two objects whose primitives the load-time sort reorders.
///
/// The first object holds four primitives that the sort puts in file order
/// 0, 3, 2, 1 (the first stays; the rest by mean heights 3, 1 and 0): a fixed
/// texture, a two-frame animation, a coloured primitive and a four-frame
/// animation. The second holds one team texture. Two cursors register.
///
/// @return the model
Model sorted_pair() {
    Model model;
    Object first;
    first.vertices = {{0, 5 * unit, 0}, {0, 3 * unit, 0}, {0, 1 * unit, 0}, {0, 0, 0}};
    first.primitives = {
        primitive({0}, "plain"),
        primitive({1}, "flash"),
        primitive({2}, {}, 9, true),
        primitive({3}, "pulse")
    };
    Object second;
    second.vertices = {{0, 0, 0}};
    second.primitives = {primitive({0}, "logo")};
    model.objects = {first, second};
    return model;
}

/// Destroys a model built in caller-owned storage and leaves the storage.
///
/// @param model model to destroy in place
void destroy_in_place(const Model* model) {
    std::destroy_at(model);
}

// A model freed and another built at its address is prepared afresh: its own
// objects, primitive order and textures, and only its own cursors step.
void test_prepare_model_at_reused_address() {
    Fixture fixture;
    add_reuse_textures(fixture);
    ModelLibrary library;
    library.textures = std::move(fixture.library);
    const auto& sequences = library.textures.sequences;
    // Both models live in this one buffer, so the second takes the first's
    // address by construction.
    alignas(Model) std::byte storage[sizeof(Model)];
    const Model* address = ::new (static_cast<void*>(storage)) Model(three_blinks());
    std::shared_ptr<const Model> first(address, destroy_in_place);
    const PreparedModel& earlier = prepare_model(library, first);
    CHECK(earlier.objects.size() == 1);
    CHECK(earlier.objects[0].primitives.size() == 3);
    CHECK(library.cursors.size() == 3);
    first.reset();

    const Model* reused = ::new (static_cast<void*>(storage)) Model(sorted_pair());
    CHECK(reused == address);
    const std::shared_ptr<const Model> second(reused, destroy_in_place);
    CHECK(find_prepared_model(library, *second) == nullptr);
    const PreparedModel& fresh = prepare_model(library, second);
    // Stepping walks every registered cursor: one left pointing into the
    // earlier model's erased entry would be touched here.
    step_texture_animations(library);
    CHECK(fresh.model == reused);
    CHECK(find_prepared_model(library, *second) == &fresh);
    CHECK(&prepare_model(library, second) == &fresh);
    CHECK(library.models.size() == 1);
    CHECK(fresh.objects.size() == 2);
    const auto& primitives = fresh.objects[0].primitives;
    CHECK(primitives.size() == 4);
    CHECK(!fresh.objects[0].skips_first);
    if (fresh.objects.size() != 2 || primitives.size() != 4 ||
        fresh.objects[1].primitives.size() != 1)
        return;
    CHECK(primitives[0].source_index == 0);
    CHECK(primitives[1].source_index == 3);
    CHECK(primitives[2].source_index == 2);
    CHECK(primitives[3].source_index == 1);
    CHECK(primitives[0].flags == 0);
    CHECK(primitive_texture(primitives[0], false, 0) == &sequences.at("plain").frames[0]);
    CHECK(primitives[1].flags == primitive_animated);
    CHECK(primitives[1].texture == &sequences.at("pulse"));
    CHECK(primitives[2].flags == primitive_colored);
    CHECK(primitives[2].color == 9);
    CHECK(primitives[3].flags == primitive_animated);
    CHECK(primitives[3].texture == &sequences.at("flash"));
    const PreparedPrimitive& logo = fresh.objects[1].primitives[0];
    CHECK(logo.flags == (primitive_animated | primitive_team));
    CHECK(primitive_texture(logo, false, 3) == &sequences.at("logo").frames[3]);
    // The earlier model's three cursors are gone; the new model's two
    // animations stepped, in registration order.
    CHECK(library.cursors.size() == 2);
    if (library.cursors.size() != 2)
        return;
    CHECK(library.cursors[0] == &primitives[1].cursor);
    CHECK(library.cursors[1] == &primitives[3].cursor);
    CHECK(primitive_texture(primitives[1], false, 0) == &sequences.at("pulse").frames[1]);
    CHECK(primitive_texture(primitives[3], false, 0) == &sequences.at("flash").frames[1]);
}

// Releasing expired entries drops a freed model's entry and cursors, keeps the
// live models' cursors in registration order, and steps only those.
void test_release_expired_models() {
    Fixture fixture;
    add_reuse_textures(fixture);
    ModelLibrary library;
    library.textures = std::move(fixture.library);
    const auto& sequences = library.textures.sequences;
    const std::shared_ptr<const Model> kept = std::make_shared<const Model>(three_blinks());
    std::shared_ptr<const Model> freed = std::make_shared<const Model>(sorted_pair());
    const std::shared_ptr<const Model> last = std::make_shared<const Model>(sorted_pair());
    const PreparedModel& kept_prepared = prepare_model(library, kept);
    prepare_model(library, freed);
    const PreparedModel& last_prepared = prepare_model(library, last);
    CHECK(library.cursors.size() == 7);
    CHECK(release_expired_models(library) == 0);
    CHECK(library.models.size() == 3);
    const Model* freed_address = freed.get();
    freed.reset();
    CHECK(release_expired_models(library) == 1);
    // A cursor left pointing into the released entry would be touched here.
    step_texture_animations(library);
    CHECK(library.models.size() == 2);
    CHECK(!library.models.contains(freed_address));
    CHECK(find_prepared_model(library, *kept) == &kept_prepared);
    CHECK(find_prepared_model(library, *last) == &last_prepared);
    CHECK(library.cursors.size() == 5);
    const auto& blinks = kept_prepared.objects[0].primitives;
    const auto& pair = last_prepared.objects[0].primitives;
    if (library.cursors.size() != 5 || blinks.size() != 3 || pair.size() != 4)
        return;
    CHECK(library.cursors[0] == &blinks[0].cursor);
    CHECK(library.cursors[1] == &blinks[1].cursor);
    CHECK(library.cursors[2] == &blinks[2].cursor);
    CHECK(library.cursors[3] == &pair[1].cursor);
    CHECK(library.cursors[4] == &pair[3].cursor);
    for (const auto& blink : blinks)
        CHECK(primitive_texture(blink, false, 0) == &sequences.at("blink").frames[1]);
    CHECK(primitive_texture(pair[1], false, 0) == &sequences.at("pulse").frames[1]);
    CHECK(primitive_texture(pair[3], false, 0) == &sequences.at("flash").frames[1]);
    CHECK(release_expired_models(library) == 0);
    CHECK(library.cursors.size() == 5);
}

oa::Palette gray_palette() {
    oa::Palette palette{};
    for (int i = 0; i < OA_PALETTE_COLORS; ++i)
        palette.entries[i] = {
            static_cast<uint8_t>(i), static_cast<uint8_t>(i), static_cast<uint8_t>(i), 0
        };
    return palette;
}

void test_display_binding() {
    ModelDisplay display;
    build_model_display(display, gray_palette());
    CHECK(display.context.alpha_table != nullptr);
    CHECK(display.alpha[10 * 256 + 10] == 10);
    oa::present::DisplayContext* previous = oa::present::display_context();
    oa::present::bind_display(nullptr);
    const DisplayScope scope = enter_display(&display);
    CHECK(scope.bound);
    CHECK(oa::present::display_context() == &display.context);
    leave_display(scope);
    CHECK(oa::present::display_context() == nullptr);
    // A complete display stays bound.
    oa::present::bind_display(&display.context);
    ModelDisplay other;
    const DisplayScope kept = enter_display(&other);
    CHECK(!kept.bound);
    CHECK(oa::present::display_context() == &display.context);
    leave_display(kept);
    oa::present::bind_display(previous);
}

// The shipped texture archives load with readable frames; logos.gaf holds
// the ten-frame team sequences.
void test_texture_library_assets(oa::AssetStore& assets) {
    const TextureLibrary library = load_texture_library(assets);
    CHECK(!library.sequences.empty());
    const auto logo = library.sequences.find("armlogo");
    if (logo != library.sequences.end()) {
        CHECK(logo->second.team_archive);
        CHECK(logo->second.frames.size() == 10);
    }
    std::size_t readable = 0;
    for (const auto& [name, sequence] : library.sequences)
        for (const auto& frame : sequence.frames)
            readable += frame.data != nullptr ? 1 : 0;
    CHECK(readable > 100);
}

} // namespace

int main(int argc, char** argv) {
    if (oa::test::game_data_requested(argc, argv)) {
        auto assets = oa::test::require_game_assets("the installed texture archives");
        test_texture_library_assets(assets);
    } else {
        test_sort_object_primitives();
        test_prepare_model();
        test_prepare_model_once();
        test_prepare_model_at_reused_address();
        test_release_expired_models();
        test_display_binding();
    }
    if (failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("model library tests passed");
    return EXIT_SUCCESS;
}
