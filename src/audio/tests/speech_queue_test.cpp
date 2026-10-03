// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "audio_test_support.hpp"
#include "oa/audio/cd_music.hpp"
#include "oa/audio/sound_system.hpp"

#include <cstdint>

using namespace oa::audio;
using namespace oa::audio::game_audio;
using audio_test::require;

namespace {

AnnouncementRequest request(uint16_t unit, UnitAnnouncementCategory category, uint32_t tick) {
    AnnouncementRequest r{};
    r.unit_index = unit;
    r.unit_sound_category = "ARM_TANK";
    r.category = category;
    r.tick = tick;
    r.local_owner = true;
    r.owner_can_announce = true;
    r.unit_text_enabled = true;
    return r;
}

void queue_maintenance() {
    const auto catalog = UnitSoundCatalog::parse_sound_tdf(R"(
      [ARM_TANK] { select1=tnkselct; ok1=tnkok; arrived1=tnkarrv; }
    )");
    AnnouncementQueue queue;
    require(
        queue.enqueue(request(1, UnitAnnouncementCategory::select, 0)) ==
            AnnouncementEnqueueStatus::queued,
        "queued select"
    );
    require(
        queue.enqueue(request(2, UnitAnnouncementCategory::arrived, 0)) ==
            AnnouncementEnqueueStatus::queued,
        "queued arrived"
    );
    require(
        queue.enqueue(request(1, UnitAnnouncementCategory::under_attack, 0)) ==
            AnnouncementEnqueueStatus::queued,
        "queued under attack"
    );
    queue.remove_unit(1);
    require(queue.size() == 1, "unit's records removed");
    queue.remove_at(5);
    require(queue.size() == 1, "out-of-range removal ignored");
    queue.clear();
    require(queue.size() == 0, "cleared");

    // A presented sound starts its category cooldown; reset clears it.
    AnnouncementPresentationGates gates{10, 10, true, true, true, false, novelty_voice_sounds};
    require(
        queue.enqueue(request(3, UnitAnnouncementCategory::arrived, 0)) ==
            AnnouncementEnqueueStatus::queued,
        "arrived queued"
    );
    const auto shown = queue.pump(catalog, gates, 0, 100);
    require(shown && shown->sound_resource == "sounds/tnkarrv.wav", "arrived presented");
    require(
        queue.enqueue(request(3, UnitAnnouncementCategory::arrived, 101)) !=
            AnnouncementEnqueueStatus::queued,
        "cooling down"
    );
    queue.reset_cooldowns();
    require(
        queue.enqueue(request(3, UnitAnnouncementCategory::arrived, 101)) ==
            AnnouncementEnqueueStatus::queued,
        "cooldown reset"
    );

    gates.novelty_voice = true;
    const auto honk = queue.present_front(catalog, gates, 0, 0);
    require(honk && honk->sound_resource == "sounds/honk.wav", "novelty voice on window 0");
    const auto sing = queue.present_front(catalog, gates, 0, 30);
    require(sing && sing->sound_resource == "sounds/sing.wav", "novelty voice elsewhere");
}

// "+Sing": while the novelty voice is on, every announcement whose sound
// plays plays the first novelty sound on the first 30-tick window of every
// eight and the second on the other seven, 3.1c's honk and sing or a mod's,
// whatever the unit's own sounds; captions stay the unit's, and every gate
// that keeps a unit's own sound silent keeps the novelty sound silent too.
void novelty_voice() {
    const auto catalog = UnitSoundCatalog::parse_sound_tdf(R"(
      [ARM_TANK] { select1=tnkselct; select1text=Ready; select2=tnksel2; arrived1=tnkarrv; }
    )");
    AnnouncementPresentationGates gates{10, 10, true, true, true, true, novelty_voice_sounds};
    const auto heard = [&](UnitAnnouncementCategory category, uint32_t tick, uint16_t rng15) {
        AnnouncementQueue queue;
        require(
            queue.enqueue(request(1, category, tick)) == AnnouncementEnqueueStatus::queued,
            "novelty request queued"
        );
        return queue.present_front(catalog, gates, rng15, tick);
    };

    struct Window {
        uint32_t tick;
        const char* sound;
    };

    for (const auto& [tick, sound] : {
             Window{0, "sounds/honk.wav"},
             Window{29, "sounds/honk.wav"},
             Window{30, "sounds/sing.wav"},
             Window{239, "sounds/sing.wav"},
             Window{240, "sounds/honk.wav"},
             Window{270, "sounds/sing.wav"},
         }) {
        const auto spoken = heard(UnitAnnouncementCategory::select, tick, 0x7fff);
        require(spoken && spoken->sound_resource == sound, "novelty voice follows its windows");
    }
    // A mod's two sounds take the same windows.
    gates.novelty_sounds = {"ewok", "jawa"};
    const auto first = heard(UnitAnnouncementCategory::select, 0, 0);
    require(first && first->sound_resource == "sounds/ewok.wav", "a mod's first novelty sound");
    const auto second = heard(UnitAnnouncementCategory::select, 60, 0);
    require(second && second->sound_resource == "sounds/jawa.wav", "a mod's second novelty sound");
    // The caption is the unit's own: the sound drawn for it gives its text.
    const auto captioned = heard(UnitAnnouncementCategory::select, 30, 0);
    require(
        captioned && captioned->sound_resource == "sounds/jawa.wav" && captioned->text == "Ready",
        "the novelty voice keeps the unit's caption"
    );
    // A category the unit has no sound for, a priority under the unit chat
    // level and the speech switch off all stay silent.
    const auto none = heard(UnitAnnouncementCategory::cannot_comply, 0, 0);
    require(none && !none->sound_resource, "no novelty sound where the unit has none");
    gates.unit_sound_volume = 3; // priorities above 7 only; arrived has 3
    const auto quiet = heard(UnitAnnouncementCategory::arrived, 0, 0);
    require(quiet && !quiet->sound_resource, "the unit chat level gates the novelty voice");
    gates.unit_sound_volume = 10;
    gates.unit_speech_mode = false;
    const auto muted = heard(UnitAnnouncementCategory::select, 0, 0);
    require(muted && !muted->sound_resource, "the speech switch gates the novelty voice");
    // Off again, the unit's own sound plays.
    gates.unit_speech_mode = true;
    gates.novelty_voice = false;
    const auto own = heard(UnitAnnouncementCategory::select, 0, 0);
    require(own && own->sound_resource == "sounds/tnkselct.wav", "the unit's own sound");
}

struct SystemLog {
    std::vector<uint32_t> flags;
    int purges{};
};

void sound_system() {
    audio_test::FakeSink sink;
    audio_test::FakeMusic music;
    audio_test::FakeTimers timers;
    audio_test::FakeFiles files;
    auto mixer = audio_test::make_mixer(sink, music, timers, files);
    AnnouncementQueue queue;
    SystemLog log;

    SoundSystem sound{};
    sound.mixer = mixer.get();
    sound.speech = &queue;
    sound.files = files.table();
    sound.system_sound.context = &log;
    sound.system_sound.play = [](void* c, const uint8_t*, uint32_t, uint32_t flags) {
        static_cast<SystemLog*>(c)->flags.push_back(flags);
        return true;
    };
    sound.system_sound.purge = [](void* c) { ++static_cast<SystemLog*>(c)->purges; };

    std::map<std::string, int32_t> values;
    AudioSettings settings{};
    settings.context = &values;
    settings.read_int = [](void* c, const char* name, int32_t fallback) {
        auto* v = static_cast<std::map<std::string, int32_t>*>(c);
        const auto found = v->find(name);
        return found == v->end() ? fallback : found->second;
    };

    require(sound_init_devices(sound, settings), "devices initialised");
    require(
        sound.mixer_disabled == 0 && sink.open && mixer->cd.device_open == 1, "mixer and cd opened"
    );

    sink.open_result = DeviceResult::failed;
    SoundSystem failing = sound;
    auto second = audio_test::make_mixer(sink, music, timers, files);
    failing.mixer = second.get();
    require(!sound_init_devices(failing, settings), "fatal mixer failure reported");
    sink.open_result = DeviceResult::no_driver;
    auto third = audio_test::make_mixer(sink, music, timers, files);
    failing.mixer = third.get();
    require(
        sound_init_devices(failing, settings) && failing.mixer_disabled == 1,
        "missing driver disables the mixer"
    );

    values["UseWindowsSound"] = 1;
    SoundSystem fallback{};
    fallback.system_sound = sound.system_sound;
    fallback.files = sound.files;
    fallback.speech = &queue;
    require(sound_init_devices(fallback, settings), "fallback init");
    require(fallback.system_sound_only == 1 && fallback.mixer_disabled == 1, "fallback flags");

    const uint8_t wav[4] = {1, 2, 3, 4};
    require(system_sound_play_looping(fallback, WavBlob{wav, 4}), "looping fallback");
    require(log.flags.back() == 0xd, "async memory loop");
    const auto catalog = UnitSoundCatalog::parse_sound_tdf("[X] { }");
    AnnouncementPresentationGates gates{};
    const auto nothing = sound_tick_speech(fallback, catalog, gates, 0, 0);
    require(!nothing && log.flags.back() == 0x15, "speech tick replays the looping sound");

    files.files["sounds/a.wav"] = {9, 9};
    require(
        system_sound_play_file(fallback, "sounds/a.wav") && fallback.resolved_sound.size() == 2,
        "file played and kept"
    );
    require(
        !system_sound_play_file(fallback, "sounds/none.wav") && fallback.resolved_sound.size() == 2,
        "failed load keeps the previous sound"
    );
    sound_stop_all(fallback);
    require(
        log.purges == 1 && fallback.looping_sound.data == nullptr &&
            fallback.resolved_sound.empty(),
        "stop purges the fallback"
    );

    sound_toggle_novelty_voice(sound);
    require(sound.novelty_voice == 1, "novelty on");
    sound_toggle_novelty_voice(sound);
    require(sound.novelty_voice == 0, "novelty off");

    require(
        queue.enqueue(request(4, UnitAnnouncementCategory::select, 1000)) ==
            AnnouncementEnqueueStatus::queued,
        "queued"
    );
    sound_forget_unit(sound, 4);
    require(queue.size() == 0, "forgotten unit");

    sound_schedule_stream(sound, "x.wav", 0, 5);
    require(mixer->stream.timer >= 0, "stream scheduled when the mixer is enabled");
    sound_stop_music(sound);
    sound_shutdown_devices(sound);
    require(!sink.open && mixer->cd.device_open == 0, "shutdown closes everything");
}

} // namespace

int main() {
    queue_maintenance();
    novelty_voice();
    sound_system();
    return 0;
}
