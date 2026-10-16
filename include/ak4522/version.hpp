#pragma once

#include <string_view>

namespace ak4522 {

// Library version string, for example "0.0.1". Alpha releases make no
// compatibility guarantees.
[[nodiscard]] std::string_view versionString() noexcept;

} // namespace ak4522
