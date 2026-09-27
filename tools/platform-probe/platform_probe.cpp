// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::size_t kMaxInputBytes = 256U * 1024U * 1024U;
constexpr std::size_t kMaxImageBytes = 128U * 1024U * 1024U;
constexpr uint64_t kMaxImagePixels = kMaxImageBytes / 3U;

struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgb;
};

struct Options {
    std::string image_path;
    std::optional<std::string> wav_path;
    std::optional<uint64_t> frame_limit;
    bool headless_check = false;
};

struct SdlSession {
    bool initialized = false;

    ~SdlSession() {
        if (initialized) {
            SDL_Quit();
        }
    }

    bool initialize(SDL_InitFlags flags) {
        if (!SDL_Init(flags)) {
            return false;
        }
        initialized = true;
        return true;
    }
};

struct LoadedWav {
    SDL_AudioSpec spec{};
    Uint8* data = nullptr;
    Uint32 length = 0;

    LoadedWav() = default;

    ~LoadedWav() {
        if (data != nullptr) {
            SDL_free(data);
        }
    }

    LoadedWav(const LoadedWav&) = delete;
    LoadedWav& operator=(const LoadedWav&) = delete;
};

void print_usage(std::ostream& out) {
    out << "usage: oa-platform --image PATH [--wav PATH] [--frames N] "
           "[--headless-check]\n";
}

bool parse_uint64(std::string_view text, uint64_t& value) {
    if (text.empty()) {
        return false;
    }
    for (const char character : text) {
        if (std::isdigit(static_cast<unsigned char>(character)) == 0) {
            return false;
        }
    }

    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

bool parse_options(int argc, char* argv[], Options& options, std::string& error) {
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--headless-check") {
            options.headless_check = true;
            continue;
        }

        auto require_value = [&](const char* option, std::string_view& value) -> bool {
            if (index + 1 >= argc) {
                error = std::string(option) + " requires a value";
                return false;
            }
            value = argv[++index];
            if (value.empty()) {
                error = std::string(option) + " requires a non-empty value";
                return false;
            }
            return true;
        };

        std::string_view value;
        if (argument == "--image") {
            if (!require_value("--image", value)) {
                return false;
            }
            options.image_path = value;
        } else if (argument == "--wav") {
            if (!require_value("--wav", value)) {
                return false;
            }
            options.wav_path = std::string(value);
        } else if (argument == "--frames") {
            if (!require_value("--frames", value)) {
                return false;
            }
            uint64_t frames = 0;
            if (!parse_uint64(value, frames)) {
                error = "--frames must be an unsigned decimal integer";
                return false;
            }
            options.frame_limit = frames;
        } else if (argument == "--help" || argument == "-h") {
            error.clear();
            return false;
        } else {
            error = "unknown option: " + std::string(argument);
            return false;
        }
    }

    if (options.image_path.empty()) {
        error = "--image is required";
        return false;
    }
    return true;
}

bool read_bounded_file(const std::string& path, std::vector<uint8_t>& bytes, std::string& error) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        error = "cannot open '" + path + "'";
        return false;
    }

    const std::streampos end = input.tellg();
    if (end < 0) {
        error = "cannot determine size of '" + path + "'";
        return false;
    }
    const auto size = static_cast<uintmax_t>(end);
    if (size > kMaxInputBytes) {
        error = "input file is larger than the 256 MiB safety limit: '" + path + "'";
        return false;
    }
    if (size > static_cast<uintmax_t>(std::numeric_limits<std::size_t>::max())) {
        error = "input file size is not representable on this platform: '" + path + "'";
        return false;
    }

    bytes.resize(static_cast<std::size_t>(size));
    input.seekg(0, std::ios::beg);
    if (!bytes.empty() &&
        !input.read(
            reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())
        )) {
        error = "cannot read '" + path + "'";
        return false;
    }
    return true;
}

bool next_ppm_token(
    const std::vector<uint8_t>& bytes, std::size_t& position, std::string_view& token
) {
    while (position < bytes.size()) {
        const unsigned char character = bytes[position];
        if (std::isspace(character) != 0) {
            ++position;
            continue;
        }
        if (character == '#') {
            while (position < bytes.size() && bytes[position] != '\n') {
                ++position;
            }
            continue;
        }
        break;
    }

    const std::size_t start = position;
    while (position < bytes.size() &&
           std::isspace(static_cast<unsigned char>(bytes[position])) == 0) {
        ++position;
    }
    if (start == position) {
        return false;
    }
    token = std::string_view(reinterpret_cast<const char*>(bytes.data() + start), position - start);
    return true;
}

bool parse_ppm(const std::string& path, Image& image, std::string& error) {
    std::vector<uint8_t> bytes;
    if (!read_bounded_file(path, bytes, error)) {
        return false;
    }

    std::size_t position = 0;
    std::string_view token;
    if (!next_ppm_token(bytes, position, token) || token != "P6") {
        error = "'" + path + "' is not a binary PPM (P6)";
        return false;
    }

    uint64_t width = 0;
    uint64_t height = 0;
    uint64_t max_value = 0;
    if (!next_ppm_token(bytes, position, token) || !parse_uint64(token, width) ||
        !next_ppm_token(bytes, position, token) || !parse_uint64(token, height) ||
        !next_ppm_token(bytes, position, token) || !parse_uint64(token, max_value)) {
        error = "'" + path + "' has an incomplete P6 header";
        return false;
    }
    if (width == 0 || height == 0 || width > 16384 || height > 16384 ||
        width > std::numeric_limits<uint64_t>::max() / height) {
        error = "'" + path + "' has an invalid or oversized image dimension";
        return false;
    }
    if (max_value == 0 || max_value > 255) {
        error = "'" + path + "' uses unsupported P6 max color value (expected 1..255)";
        return false;
    }

    const uint64_t pixels = width * height;
    if (pixels > kMaxImagePixels || pixels > std::numeric_limits<std::size_t>::max() / 3U) {
        error = "'" + path + "' exceeds the 128 MiB decoded image safety limit";
        return false;
    }

    // The byte after max_value is the mandatory separator before the raster. A
    // CRLF pair is consumed together; further whitespace belongs to the raster.
    if (position >= bytes.size() ||
        std::isspace(static_cast<unsigned char>(bytes[position])) == 0) {
        error = "'" + path + "' is missing the P6 raster separator";
        return false;
    }
    const uint8_t separator = bytes[position++];
    if (separator == '\r' && position < bytes.size() && bytes[position] == '\n') {
        ++position;
    }

    const std::size_t raster_bytes = static_cast<std::size_t>(pixels) * 3U;
    if (raster_bytes > bytes.size() - position) {
        error = "'" + path + "' has a truncated P6 raster";
        return false;
    }

    image.width = static_cast<int>(width);
    image.height = static_cast<int>(height);
    image.rgb.resize(raster_bytes);
    const uint8_t* source = bytes.data() + position;
    if (max_value == 255) {
        std::copy_n(source, raster_bytes, image.rgb.data());
    } else {
        for (std::size_t index = 0; index < raster_bytes; ++index) {
            if (source[index] > max_value) {
                error = "'" + path + "' contains a P6 sample above maxval";
                return false;
            }
            image.rgb[index] = static_cast<uint8_t>(
                (static_cast<uint32_t>(source[index]) * 255U + max_value / 2U) / max_value
            );
        }
    }
    return true;
}

bool load_wav(const std::string& path, LoadedWav& wav, std::string& error) {
    // SDL_LoadWAV allocates the decoded buffer, so bound the source file before
    // handing it to SDL as well as bounding PPM input above.
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        error = "cannot open WAV '" + path + "'";
        return false;
    }
    const std::streampos end = input.tellg();
    if (end < 0 || static_cast<uintmax_t>(end) > kMaxInputBytes) {
        error = "WAV is missing or larger than the 256 MiB safety limit: '" + path + "'";
        return false;
    }

    if (!SDL_LoadWAV(path.c_str(), &wav.spec, &wav.data, &wav.length)) {
        error = "SDL_LoadWAV failed for '" + path + "': " + SDL_GetError();
        return false;
    }
    if (wav.data == nullptr || wav.length == 0 || wav.spec.freq <= 0 || wav.spec.channels <= 0) {
        error = "SDL_LoadWAV returned an empty or invalid WAV: '" + path + "'";
        return false;
    }
    return true;
}

int run_probe(const Options& options, const Image& image, LoadedWav& wav) {
    if (options.headless_check) {
        std::cout << "headless check passed: " << image.width << 'x' << image.height;
        if (options.wav_path) {
            std::cout << ", WAV " << wav.length << " bytes";
        }
        std::cout << '\n';
        return 0;
    }

    const int window_width = std::clamp(image.width, 320, 1280);
    const int window_height = std::clamp(image.height, 200, 720);
    SDL_Window* window = SDL_CreateWindow(
        "Open Annihilation platform probe", window_width, window_height, SDL_WINDOW_RESIZABLE
    );
    if (window == nullptr) {
        std::cerr << "platform probe: SDL_CreateWindow failed: " << SDL_GetError() << '\n';
        return 1;
    }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (renderer == nullptr) {
        std::cerr << "platform probe: SDL_CreateRenderer failed: " << SDL_GetError() << '\n';
        SDL_DestroyWindow(window);
        return 1;
    }
    SDL_Texture* texture = SDL_CreateTexture(
        renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STATIC, image.width, image.height
    );
    if (texture == nullptr) {
        std::cerr << "platform probe: SDL_CreateTexture failed: " << SDL_GetError() << '\n';
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        return 1;
    }

    int result = 0;
    if (!SDL_UpdateTexture(texture, nullptr, image.rgb.data(), image.width * 3)) {
        std::cerr << "platform probe: SDL_UpdateTexture failed: " << SDL_GetError() << '\n';
        result = 1;
    } else if (!SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST)) {
        std::cerr << "platform probe: SDL_SetTextureScaleMode failed: " << SDL_GetError() << '\n';
        result = 1;
    } else if (!SDL_SetRenderLogicalPresentation(
                   renderer, image.width, image.height, SDL_LOGICAL_PRESENTATION_LETTERBOX
               )) {
        std::cerr << "platform probe: SDL_SetRenderLogicalPresentation failed: " << SDL_GetError()
                  << '\n';
        result = 1;
    }

    SDL_AudioStream* audio_stream = nullptr;
    if (result == 0 && options.wav_path) {
        audio_stream = SDL_OpenAudioDeviceStream(
            SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &wav.spec, nullptr, nullptr
        );
        if (audio_stream == nullptr) {
            std::cerr << "platform probe: SDL_OpenAudioDeviceStream failed: " << SDL_GetError()
                      << '\n';
            result = 1;
        } else if (!SDL_PutAudioStreamData(audio_stream, wav.data, wav.length)) {
            std::cerr << "platform probe: SDL_PutAudioStreamData failed: " << SDL_GetError()
                      << '\n';
            result = 1;
        } else if (!SDL_FlushAudioStream(audio_stream)) {
            std::cerr << "platform probe: SDL_FlushAudioStream failed: " << SDL_GetError() << '\n';
            result = 1;
        } else if (!SDL_ResumeAudioStreamDevice(audio_stream)) {
            std::cerr << "platform probe: SDL_ResumeAudioStreamDevice failed: " << SDL_GetError()
                      << '\n';
            result = 1;
        }
    }

    uint64_t frames_rendered = 0;
    if (result == 0) {
        const char* video_driver = SDL_GetCurrentVideoDriver();
        const char* renderer_name = SDL_GetRendererName(renderer);
        const char* audio_driver = options.wav_path ? SDL_GetCurrentAudioDriver() : nullptr;
        std::cout << "SDL startup: video=" << (video_driver != nullptr ? video_driver : "unknown")
                  << ", renderer=" << (renderer_name != nullptr ? renderer_name : "unknown")
                  << ", audio="
                  << (options.wav_path ? (audio_driver != nullptr ? audio_driver : "unknown")
                                       : "disabled")
                  << '\n';

        bool running = true;
        while (running && (!options.frame_limit || frames_rendered < *options.frame_limit)) {
            SDL_Event event{};
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT ||
                    event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED ||
                    (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)) {
                    running = false;
                }
            }

            if (!running) {
                break;
            }
            if (!SDL_RenderClear(renderer) ||
                !SDL_RenderTexture(renderer, texture, nullptr, nullptr) ||
                !SDL_RenderPresent(renderer)) {
                std::cerr << "platform probe: render failed: " << SDL_GetError() << '\n';
                result = 1;
                break;
            }
            ++frames_rendered;
            SDL_Delay(16);
        }
    }

    if (audio_stream != nullptr) {
        SDL_DestroyAudioStream(audio_stream);
    }
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    if (result == 0) {
        std::cout << "platform probe: completed " << frames_rendered << " frame(s)\n";
    }
    return result;
}

int run(int argc, char* argv[]) {
    Options options;
    std::string error;
    if (!parse_options(argc, argv, options, error)) {
        if (!error.empty()) {
            std::cerr << "platform probe: " << error << '\n';
        }
        print_usage(error.empty() ? std::cout : std::cerr);
        return error.empty() ? 0 : 2;
    }

    Image image;
    if (!parse_ppm(options.image_path, image, error)) {
        std::cerr << "platform probe: " << error << '\n';
        return 2;
    }

    LoadedWav wav;
    if (options.wav_path && !load_wav(*options.wav_path, wav, error)) {
        std::cerr << "platform probe: " << error << '\n';
        return 2;
    }
    SdlSession sdl;
    const SDL_InitFlags flags =
        options.headless_check
            ? 0
            : static_cast<SDL_InitFlags>(SDL_INIT_VIDEO | (options.wav_path ? SDL_INIT_AUDIO : 0));
    if (!sdl.initialize(flags)) {
        std::cerr << "platform probe: SDL initialization failed: " << SDL_GetError() << '\n';
        return 1;
    }
    const int result = run_probe(options, image, wav);
    return result;
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        return run(argc, argv);
    } catch (const std::bad_alloc&) {
        std::cerr << "platform probe: input exceeds available memory\n";
        return 2;
    }
}
