#pragma once

#include <string_view>

namespace sts::engine {

struct Version {
    int major;
    int minor;
    int patch;
};

constexpr Version kVersion{0, 1, 0};

std::string_view VersionString();

}  // namespace sts::engine
