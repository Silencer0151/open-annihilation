// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/platform/machine.hpp"

#include <fstream>
#include <ios>
#include <string>

namespace oa::platform {

bool raspberry_pi_model(std::string_view model) noexcept {
    const std::string_view text = model.substr(0, model.find('\0'));
    return text.starts_with(raspberry_pi_model_prefix);
}

bool model_file_names_raspberry_pi(const std::filesystem::path& model_file) {
    std::ifstream input(model_file, std::ios::binary);
    if (!input)
        return false;
    std::string text(model_file_limit, '\0');
    input.read(text.data(), static_cast<std::streamsize>(text.size()));
    const std::streamsize length = input.gcount();
    if (length <= 0)
        return false;
    text.resize(static_cast<std::size_t>(length));
    return raspberry_pi_model(text);
}

bool running_on_raspberry_pi() {
#if defined(__linux__)
    return model_file_names_raspberry_pi(device_tree_model_path);
#else
    return false;
#endif
}

} // namespace oa::platform
