#include "startup_log.h"

#include <windows.h>

#include <algorithm>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;
namespace disruptor::launcher {
namespace {
constexpr std::streamoff most_bytes = 16384;

// Bytes that are not UTF-8 come out as U+FFFD: a log in another encoding is still shown.
std::wstring decode(const std::string& text) {
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(static_cast<size_t>(std::max(size, 0)), L'\0');
    if (size > 0) MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
    return result;
}
} // namespace

std::wstring log_tail(const fs::path& log, size_t lines) {
    std::ifstream file(log, std::ios::binary | std::ios::ate);
    if (!file) return {};
    const std::streamoff size = file.tellg();
    const std::streamoff start = std::max<std::streamoff>(0, size - most_bytes);
    std::string text(static_cast<size_t>(size - start), '\0');
    file.seekg(start);
    file.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<size_t>(std::max<std::streamsize>(file.gcount(), 0)));
    if (start > 0) text.erase(0, std::min(text.find('\n'), text.size() - 1) + 1);

    std::vector<std::string> kept;
    size_t end = text.size();
    while (end > 0 && kept.size() < lines) {
        const size_t newline = text.rfind('\n', end - 1);
        const size_t begin = newline == std::string::npos ? 0 : newline + 1;
        std::string line = text.substr(begin, end - begin);
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (!line.empty()) kept.push_back(std::move(line));
        end = newline == std::string::npos ? 0 : newline;
    }
    std::string joined;
    for (auto line = kept.rbegin(); line != kept.rend(); ++line) joined += *line + "\r\n";
    return decode(joined);
}

} // namespace disruptor::launcher
