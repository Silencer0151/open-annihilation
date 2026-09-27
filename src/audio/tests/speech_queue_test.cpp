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
    AnnouncementPresentationGates gates{10, 10, true, true, true, false};
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
    sound_system();
    return 0;
}
