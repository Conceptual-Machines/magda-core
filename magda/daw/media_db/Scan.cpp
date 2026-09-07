#include "Scan.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <vector>

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
    // Manual stack-based recursion instead of recursive_directory_iterator.
    // The recursive iterator stops the *entire* walk on any per-entry
    // failure (permission denied not caught by skip_permission_denied,
    // network-mount blip, vanishing directory, symlink loop, etc.), so a
    // single bad subtree would leave the rest of the tree un-indexed.
    // Here each directory is iterated independently with its own
    // error_code; failures inside one directory only skip that directory's
    // remaining entries, not the rest of the walk.
    using Opt = std::filesystem::directory_options;
    constexpr auto kOpts = Opt::follow_directory_symlink | Opt::skip_permission_denied;

    std::vector<std::filesystem::path> stack;
    stack.push_back(root);

    while (!stack.empty()) {
        auto dir = std::move(stack.back());
        stack.pop_back();

        std::error_code dirEc;
        std::filesystem::directory_iterator it;
        try {
            it = std::filesystem::directory_iterator(dir, kOpts, dirEc);
        } catch (const std::exception&) {
            continue;  // can't open this directory; carry on with siblings
        }
        if (dirEc) {
            continue;
        }

        const std::filesystem::directory_iterator end;
        while (it != end) {
            try {
                const auto& entry = *it;

                std::error_code regEc;
                if (entry.is_regular_file(regEc) && !regEc) {
                    if (auto sf = classify(entry.path())) {
                        try {
                            visit(*sf);
                        } catch (...) {
                            // A single file's processing failure must not
                            // sink the whole scan; the indexer does its own
                            // accounting under processOneFile.
                        }
                    }
                } else {
                    std::error_code dirCheckEc;
                    if (entry.is_directory(dirCheckEc) && !dirCheckEc) {
                        stack.push_back(entry.path());
                    }
                }
            } catch (const std::exception&) {
                // Bad entry — skip and try to keep going in this directory.
            }

            std::error_code incEc;
            it.increment(incEc);
            if (incEc) {
                // Lost iteration position; abandon this directory but
                // continue with the rest of the stacked subtree.
                break;
            }
        }
    }
}

}  // namespace magda::media
