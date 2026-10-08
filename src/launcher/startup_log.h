#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

namespace disruptor::launcher {

// The last lines of the game's startup.log, empty when there is none.
std::wstring log_tail(const std::filesystem::path& log, size_t lines);

} // namespace disruptor::launcher
