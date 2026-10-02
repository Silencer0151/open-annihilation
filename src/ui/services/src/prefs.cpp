// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/services/prefs.hpp"
#include "oa/base/bytes.hpp"

#include <cstdint>
#include <cstring>
#include <string>

namespace oa::ui::services {
namespace {
using base::bytes::load_le32;

constexpr uint32_t dword_size = 4;
constexpr char hex_digits[] = "0123456789abcdef";

std::string pref_key(const char* application, const char* name) {
    std::string key = std::string(application) + "\\" + name;
    for (char& c : key) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return key;
}

void store_le32(uint8_t* bytes, uint32_t value) noexcept {
    for (uint32_t i = 0; i < dword_size; ++i) {
        bytes[i] = static_cast<uint8_t>(value >> (8 * i));
    }
}

int hex_value(char c) noexcept {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

PrefStatus file_query(
    void* context, const char* application, const char* name, uint8_t* data, uint32_t* size
) {
    try {
        const auto& values = *static_cast<platform::preferences::Values*>(context);
        const auto found = values.find(pref_key(application, name));
        if (found == values.end()) {
            return PrefStatus::missing;
        }
        const std::string& encoded = found->second;
        const std::size_t colon = encoded.find(':');
        if (colon == std::string::npos || (encoded.size() - colon - 1) % 2 != 0) {
            return PrefStatus::failed;
        }
        const std::size_t length = (encoded.size() - colon - 1) / 2;
        if (length > *size) {
            *size = static_cast<uint32_t>(length);
            return PrefStatus::more_data;
        }
        for (std::size_t i = 0; i < length; ++i) {
            const int high = hex_value(encoded[colon + 1 + 2 * i]);
            const int low = hex_value(encoded[colon + 2 + 2 * i]);
            if (high < 0 || low < 0) {
                return PrefStatus::failed;
            }
            data[i] = static_cast<uint8_t>(high << 4 | low);
        }
        *size = static_cast<uint32_t>(length);
        return PrefStatus::ok;
    } catch (...) {
        return PrefStatus::failed;
    }
}

PrefStatus file_store(
    void* context,
    const char* application,
    const char* name,
    PrefType type,
    const uint8_t* data,
    uint32_t size
) {
    try {
        std::string encoded = std::to_string(static_cast<uint32_t>(type)) + ":";
        encoded.reserve(encoded.size() + 2 * static_cast<std::size_t>(size));
        for (uint32_t i = 0; i < size; ++i) {
            encoded += hex_digits[data[i] >> 4];
            encoded += hex_digits[data[i] & 0xf];
        }
        (*static_cast<platform::preferences::Values*>(context))[pref_key(application, name)] =
            std::move(encoded);
        return PrefStatus::ok;
    } catch (...) {
        return PrefStatus::failed;
    }
}

} // namespace

bool pref_access(
    const PrefBackend* backend,
    const char* application,
    const char* name,
    uint8_t* data,
    uint32_t* size,
    PrefType type,
    bool read
) noexcept {
    if (!read) {
        return backend->store(backend->context, application, name, type, data, *size) ==
               PrefStatus::ok;
    }
    const PrefStatus status = backend->query(backend->context, application, name, data, size);
    return status == PrefStatus::ok || status == PrefStatus::more_data;
}

bool pref_read(
    const PrefBackend* backend,
    const char* application,
    const char* name,
    uint8_t* data,
    uint32_t* size
) noexcept {
    return pref_access(backend, application, name, data, size, PrefType::string, true);
}

bool pref_read_dword(
    const PrefBackend* backend, const char* application, const char* name, uint32_t* value
) noexcept {
    uint8_t bytes[dword_size];
    store_le32(bytes, *value);
    uint32_t size = dword_size;
    const bool found =
        pref_access(backend, application, name, bytes, &size, PrefType::string, true);
    *value = load_le32(bytes);
    return found;
}

bool pref_write_binary(
    const PrefBackend* backend,
    const char* application,
    const char* name,
    const uint8_t* data,
    uint32_t size
) noexcept {
    return pref_access(
        backend, application, name, const_cast<uint8_t*>(data), &size, PrefType::binary, false
    );
}

bool pref_write_string(
    const PrefBackend* backend, const char* application, const char* name, const char* text
) noexcept {
    uint32_t size = static_cast<uint32_t>(std::strlen(text) + 1);
    auto* bytes = reinterpret_cast<uint8_t*>(const_cast<char*>(text));
    return pref_access(backend, application, name, bytes, &size, PrefType::string, false);
}

bool pref_write_dword(
    const PrefBackend* backend, const char* application, const char* name, uint32_t value
) noexcept {
    uint8_t bytes[dword_size];
    store_le32(bytes, value);
    uint32_t size = dword_size;
    return pref_access(backend, application, name, bytes, &size, PrefType::dword, false);
}

PrefBackend pref_file_backend(platform::preferences::Values* values) noexcept {
    return {values, file_query, file_store};
}

} // namespace oa::ui::services
