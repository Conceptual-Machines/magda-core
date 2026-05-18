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

// Return the file_time's ns count directly. Used as an opaque identifier
// for "same file state" against the DB row; we never interpret the value
// as a human timestamp, so the file_clock epoch (vs unix epoch) doesn't
// matter. clock_cast<system_clock>(file_time) is tempting but on macOS
// libc++ it samples both clocks at call time, producing a ±1µs wobble
// across calls of the same untouched file - that broke rescan skip
// detection.
std::int64_t toFileTimeNs(std::filesystem::file_time_type ft) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(ft.time_since_epoch()).count();
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
    f.mtimeNs = toFileTimeNs(mt);
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
