#include "Scan.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <string_view>
#include <system_error>
#include <unordered_map>

namespace magda::media {

namespace {

// Single source of truth for extension -> kind. Keep in sync with the
// Python prototype's scan.py AUDIO_EXTS / PRESET_EXTS / CLIP_EXTS sets.
const std::unordered_map<std::string, std::string_view>& extensionTable() {
    static const std::unordered_map<std::string, std::string_view> kTable = {
        // audio
        {".wav", "audio"},
        {".aif", "audio"},
        {".aiff", "audio"},
        {".mp3", "audio"},
        {".flac", "audio"},
        {".ogg", "audio"},
        {".m4a", "audio"},
        // presets
        {".vstpreset", "preset"},
        {".aupreset", "preset"},
        {".fxp", "preset"},
        {".fxb", "preset"},
        {".mps", "preset"},
        // clips
        {".mid", "clip"},
        {".midi", "clip"},
    };
    return kTable;
}

std::string toLower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// Convert a std::filesystem::file_time to ns since the unix epoch.
// std::filesystem::file_time_type uses an unspecified clock; we go via
// system_clock which is required to be unix-epoch-based on the platforms
// MAGDA targets. C++20 has file_clock::to_sys; on C++17 we approximate.
std::int64_t toUnixNs(std::filesystem::file_time_type ft) {
#if defined(__cpp_lib_chrono) && __cpp_lib_chrono >= 201907L
    auto sys = std::chrono::clock_cast<std::chrono::system_clock>(ft);
#else
    // file_clock and system_clock both have nanosecond precision on the
    // platforms we ship; the offset between them is constant per platform.
    auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ft - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
#endif
    return std::chrono::duration_cast<std::chrono::nanoseconds>(sys.time_since_epoch()).count();
}

}  // namespace

std::optional<ScannedFile> classify(const std::filesystem::path& path) {
    std::string ext = toLower(path.extension().string());
    const auto& table = extensionTable();
    auto it = table.find(ext);
    if (it == table.end()) {
        return std::nullopt;
    }

    std::error_code ec;
    auto sz = std::filesystem::file_size(path, ec);
    if (ec) {
        return std::nullopt;
    }
    auto mt = std::filesystem::last_write_time(path, ec);
    if (ec) {
        return std::nullopt;
    }

    ScannedFile f;
    f.path = path;
    f.kind = std::string(it->second);
    f.format = ext.empty() ? std::string{} : ext.substr(1);  // strip leading dot
    f.sizeBytes = static_cast<std::int64_t>(sz);
    f.mtimeNs = toUnixNs(mt);
    return f;
}

void walk(const std::filesystem::path& root, const std::function<void(const ScannedFile&)>& visit) {
    using Iter = std::filesystem::recursive_directory_iterator;
    using Opt = std::filesystem::directory_options;

    std::error_code rootEc;
    Iter it(root, Opt::follow_directory_symlink | Opt::skip_permission_denied, rootEc);
    if (rootEc) {
        return;
    }
    Iter end;
    while (it != end) {
        std::error_code stepEc;
        const auto& entry = *it;
        if (entry.is_regular_file(stepEc) && !stepEc) {
            if (auto sf = classify(entry.path())) {
                visit(*sf);
            }
        }
        it.increment(stepEc);
        if (stepEc) {
            // Best effort: stop iteration cleanly if the tree changes
            // underneath us mid-walk.
            break;
        }
    }
}

}  // namespace magda::media
