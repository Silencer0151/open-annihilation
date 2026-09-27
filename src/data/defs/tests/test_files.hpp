// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Files test doubles: an in-memory set and a loose host directory.
#pragma once

#include "oa/data/defs/files.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace oa::data::defs::test {

inline std::string fold_path(std::string path) {
    for (char& c : path) {
        if (c == '/')
            c = '\\';
        else if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c + 32);
    }
    return path;
}

struct MemoryFile {
    std::string path;
    std::string text;
    bool archived = false;
};

struct MemoryFiles {
    std::vector<MemoryFile> files;

    const MemoryFile* find(const char* path) const {
        const std::string key = fold_path(path);
        for (const auto& file : files)
            if (fold_path(file.path) == key)
                return &file;
        return nullptr;
    }

    Files view() {
        return Files{
            this,
            [](void* context, const char* path, uint8_t** data, uint32_t* size, bool* archived) {
                const MemoryFile* file = static_cast<MemoryFiles*>(context)->find(path);
                if (file == nullptr)
                    return false;
                *data = static_cast<uint8_t*>(std::malloc(file->text.size() + 1));
                std::memcpy(*data, file->text.data(), file->text.size());
                *size = static_cast<uint32_t>(file->text.size());
                *archived = file->archived;
                return true;
            },
            [](void*, uint8_t* data) { std::free(data); },
            [](void* context, const char* path) {
                return static_cast<MemoryFiles*>(context)->find(path) != nullptr;
            },
            [](void* context,
               const char* directory,
               const char* extension,
               void (*visit)(void*, const char*),
               void* user) {
                const std::string prefix = fold_path(directory) + "\\";
                const std::string suffix = "." + fold_path(extension);
                for (const auto& file : static_cast<MemoryFiles*>(context)->files) {
                    const std::string key = fold_path(file.path);
                    if (key.rfind(prefix, 0) == 0 && key.size() > suffix.size() &&
                        key.compare(key.size() - suffix.size(), suffix.size(), suffix) == 0 &&
                        key.find('\\', prefix.size()) == std::string::npos)
                        visit(user, file.path.c_str() + prefix.size());
                }
            },
        };
    }
};

// Loose files under a host directory, matched case-insensitively, listed in name order.
struct DirectoryFiles {
    std::filesystem::path root;

    std::filesystem::path resolve(const char* path) const {
        std::filesystem::path at = root;
        std::string rest = fold_path(path);
        while (!rest.empty()) {
            const auto slash = rest.find('\\');
            const std::string part = rest.substr(0, slash);
            rest = slash == std::string::npos ? std::string() : rest.substr(slash + 1);
            if (!std::filesystem::is_directory(at))
                return {};
            std::filesystem::path match;
            for (const auto& entry : std::filesystem::directory_iterator(at))
                if (fold_path(entry.path().filename().string()) == part)
                    match = entry.path();
            if (match.empty())
                return {};
            at = match;
        }
        return at;
    }

    Files view() {
        return Files{
            this,
            [](void* context, const char* path, uint8_t** data, uint32_t* size, bool* archived) {
                const auto file = static_cast<DirectoryFiles*>(context)->resolve(path);
                if (file.empty() || !std::filesystem::is_regular_file(file))
                    return false;
                std::ifstream in(file, std::ios::binary);
                const std::string text(
                    (std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()
                );
                *data = static_cast<uint8_t*>(std::malloc(text.size() + 1));
                std::memcpy(*data, text.data(), text.size());
                *size = static_cast<uint32_t>(text.size());
                *archived = false;
                return true;
            },
            [](void*, uint8_t* data) { std::free(data); },
            [](void* context, const char* path) {
                const auto file = static_cast<DirectoryFiles*>(context)->resolve(path);
                return !file.empty() && std::filesystem::is_regular_file(file);
            },
            [](void* context,
               const char* directory,
               const char* extension,
               void (*visit)(void*, const char*),
               void* user) {
                const auto dir = static_cast<DirectoryFiles*>(context)->resolve(directory);
                if (dir.empty() || !std::filesystem::is_directory(dir))
                    return;
                std::vector<std::string> names;
                const std::string suffix = "." + fold_path(extension);
                for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                    const std::string name = entry.path().filename().string();
                    const std::string folded = fold_path(name);
                    if (entry.is_regular_file() && folded.size() > suffix.size() &&
                        folded.compare(folded.size() - suffix.size(), suffix.size(), suffix) == 0)
                        names.push_back(name);
                }
                std::sort(names.begin(), names.end());
                for (const auto& name : names)
                    visit(user, name.c_str());
            },
        };
    }
};

} // namespace oa::data::defs::test
