#include "PathRules.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

namespace magda::media {

namespace {

// ---- Family keyword vocabulary ------------------------------------------
//
// Same map as derive._PATH_FAMILY_KEYWORDS in the Python prototype.
// Lookups are lowercase-only.
const std::unordered_map<std::string, std::string>& familyKeywords() {
    static const std::unordered_map<std::string, std::string> kMap = {
        // vocals
        {"vocal", "vocal"},
        {"vocals", "vocal"},
        {"vox", "vocal"},
        {"acapella", "vocal"},
        {"acapellas", "vocal"},
        {"voc", "vocal"},
        {"adlib", "vocal"},
        {"adlibs", "vocal"},
        // drums — kept broad on purpose. Sub-category is captured via the
        // path-derived TAG (e.g. "kick" / "snare" / "openhat") so the
        // user can filter further with the tags input without exploding
        // the family column into dozens of values.
        {"kick", "drum"},
        {"kicks", "drum"},
        {"bd", "drum"},
        {"snare", "drum"},
        {"snares", "drum"},
        {"sd", "drum"},
        {"rimshot", "drum"},
        {"rim", "drum"},
        {"clap", "drum"},
        {"claps", "drum"},
        {"hat", "drum"},
        {"hats", "drum"},
        {"hihat", "drum"},
        {"hihats", "drum"},
        {"hh", "drum"},
        {"openhat", "drum"},
        {"openhats", "drum"},
        {"closedhat", "drum"},
        {"closedhats", "drum"},
        {"ohh", "drum"},
        {"chh", "drum"},
        {"tom", "drum"},
        {"toms", "drum"},
        {"cymbal", "drum"},
        {"cymbals", "drum"},
        {"ride", "drum"},
        {"rides", "drum"},
        {"crash", "drum"},
        {"shaker", "drum"},
        {"shakers", "drum"},
        {"tambourine", "drum"},
        {"tamb", "drum"},
        {"cowbell", "drum"},
        {"conga", "drum"},
        {"congas", "drum"},
        {"bongo", "drum"},
        {"bongos", "drum"},
        {"perc", "drum"},
        {"percussion", "drum"},
        {"drum", "drum"},
        {"drums", "drum"},
        // bass
        {"bass", "bass"},
        {"808", "bass"},
        {"sub", "bass"},
        {"subbass", "bass"},
        // lead / pluck
        {"lead", "lead"},
        {"leads", "lead"},
        {"pluck", "lead"},
        {"plucks", "lead"},
        {"arp", "lead"},
        {"arps", "lead"},
        {"arpeggio", "lead"},
        // pad
        {"pad", "pad"},
        {"pads", "pad"},
        // keys
        {"piano", "keys"},
        {"pianos", "keys"},
        {"rhodes", "keys"},
        {"wurli", "keys"},
        {"organ", "keys"},
        {"organs", "keys"},
        {"ep", "keys"},
        {"keys", "keys"},
        // guitar
        {"guitar", "guitar"},
        {"guitars", "guitar"},
        {"gtr", "guitar"},
        {"gtrs", "guitar"},
        // orchestral
        {"strings", "orchestral"},
        {"brass", "orchestral"},
        {"horn", "orchestral"},
        {"horns", "orchestral"},
        {"violin", "orchestral"},
        {"cello", "orchestral"},
        {"woodwind", "orchestral"},
        {"woodwinds", "orchestral"},
        {"flute", "orchestral"},
        // fx / foley
        {"fx", "fx"},
        {"sfx", "fx"},
        {"riser", "fx"},
        {"risers", "fx"},
        {"downer", "fx"},
        {"downlifter", "fx"},
        {"impact", "fx"},
        {"impacts", "fx"},
        {"ambience", "fx"},
        {"ambiences", "fx"},
        {"ambient", "fx"},
        {"foley", "fx"},
        {"sweep", "fx"},
        {"sweeps", "fx"},
    };
    return kMap;
}

// ---- Tokenization helpers -----------------------------------------------

bool isPathSep(char c) {
    return c == '_' || c == '/' || c == '\\' || c == '-' || c == '.' || c == ' ' || c == '\t' ||
           c == ',' || c == '(' || c == ')';
}

std::string toLower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// Split a path component into lowercase tokens on separator characters.
// Mirrors the Python regex r"[_/\-.\s,()]+" but avoids std::regex overhead.
std::vector<std::string> tokenizeLower(std::string_view chunk) {
    std::vector<std::string> out;
    std::string cur;
    cur.reserve(chunk.size());
    for (char c : chunk) {
        if (isPathSep(c)) {
            if (!cur.empty()) {
                out.push_back(toLower(cur));
                cur.clear();
            }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) {
        out.push_back(toLower(cur));
    }
    return out;
}

// Resolve symlinks; on failure (file missing in tests, etc.) return as-is.
std::filesystem::path resolveForInspection(const std::filesystem::path& p) {
    std::error_code ec;
    auto canon = std::filesystem::canonical(p, ec);
    if (ec) {
        return p;
    }
    return canon;
}

// Path components ordered for keyword search: leaf folder first, then up
// toward the root, then the file stem last. Pack-name ancestors are
// deprioritized because they often contain genre words ("Drum & Bass") that
// aren't instrument-family signals.
std::vector<std::string> chunksLeafFirst(const std::filesystem::path& path) {
    std::vector<std::string> parents;
    for (const auto& part : path.parent_path()) {
        parents.push_back(part.string());
    }
    std::reverse(parents.begin(), parents.end());
    parents.push_back(path.stem().string());
    return parents;
}

// ---- Key parser ----------------------------------------------------------
//
// Match a single token like "C", "Cm", "C#", "F#m", "Bb", "Cmin", "Cmajor".
// Root is case-sensitive uppercase ([A-G]) so we don't match 'g' in
// "guitar". Scale alternatives are spelled out per case rather than using
// std::regex_constants::icase, because icase would also relax [A-G] to
// match a-g.
const std::regex& keyTokenRe() {
    static const std::regex kRe(R"(^([A-G])([b#])?)"
                                R"((maj|MAJ|Maj|major|MAJOR|Major)"
                                R"(|min|MIN|Min|minor|MINOR|Minor)"
                                R"(|m|M)?$)");
    return kRe;
}

// Flat -> sharp normalization so the column matches the chroma analyzer's
// vocabulary (PITCH_CLASSES uses sharps).
const std::unordered_map<std::string, std::string>& flatToSharp() {
    static const std::unordered_map<std::string, std::string> kMap = {
        {"Cb", "B"},  {"Db", "C#"}, {"Eb", "D#"}, {"Fb", "E"},
        {"Gb", "F#"}, {"Ab", "G#"}, {"Bb", "A#"},
    };
    return kMap;
}

ParsedKey normalizeKey(const std::string& rootLetter, const std::string& accidental,
                       const std::string& scaleStr) {
    ParsedKey key;
    if (accidental == "#") {
        key.root = rootLetter + "#";
        // B# / E# never appear in producer-named samples; if we see them,
        // fall back to the natural letter rather than emit something
        // outside PITCH_CLASSES.
        if (key.root == "B#" || key.root == "E#") {
            key.root = rootLetter;
        }
    } else if (accidental == "b") {
        const auto& map = flatToSharp();
        auto it = map.find(rootLetter + "b");
        key.root = (it != map.end()) ? it->second : rootLetter;
    } else {
        key.root = rootLetter;
    }

    if (!scaleStr.empty()) {
        std::string lo = toLower(scaleStr);
        if (lo == "m" || lo == "min" || lo == "minor") {
            key.scale = "minor";
        } else if (lo == "maj" || lo == "major") {
            key.scale = "major";
        } else if (scaleStr == "M") {
            // Only uppercase 'M' alone counts as major (lowercase 'm' is
            // minor by producer convention); both lowered to 'm' above.
            key.scale = "major";
        }
    }
    return key;
}

}  // namespace

std::optional<std::string> pathFamilyHint(const std::filesystem::path& path) {
    auto resolved = resolveForInspection(path);
    const auto& map = familyKeywords();
    for (const auto& chunk : chunksLeafFirst(resolved)) {
        for (const auto& tok : tokenizeLower(chunk)) {
            auto it = map.find(tok);
            if (it != map.end()) {
                return it->second;
            }
        }
    }
    return std::nullopt;
}

std::vector<std::pair<std::string, float>> pathTags(const std::filesystem::path& path) {
    auto resolved = resolveForInspection(path);
    const auto& map = familyKeywords();

    std::vector<std::pair<std::string, float>> out;
    std::unordered_set<std::string> seen;
    for (const auto& chunk : chunksLeafFirst(resolved)) {
        for (const auto& tok : tokenizeLower(chunk)) {
            if (seen.count(tok) != 0U) {
                continue;
            }
            if (map.count(tok) != 0U) {
                seen.insert(tok);
                out.emplace_back(tok, 1.0F);
            }
        }
    }
    return out;
}

std::optional<double> parseBpmFromPath(const std::filesystem::path& path) {
    // 2-3 digits, optional decimal, optional separator, then "bpm" (any case).
    // Two/three digit constraint excludes single-digit noise like "9bpm" and
    // anything > 999 which would never be a real tempo.
    static const std::regex kBpmRe(R"((\d{2,3}(?:\.\d+)?)\s*[_\- ]?\s*bpm)", std::regex::icase);

    std::string stem = path.stem().string();
    std::optional<double> result;
    for (auto it = std::sregex_iterator(stem.begin(), stem.end(), kBpmRe);
         it != std::sregex_iterator{}; ++it) {
        try {
            double bpm = std::stod((*it)[1].str());
            if (bpm >= 30.0 && bpm <= 300.0) {
                result = bpm;  // last sensible match wins
            }
        } catch (...) {
            // unparseable group, ignore
        }
    }
    return result;
}

std::optional<ParsedKey> parseKeyFromPath(const std::filesystem::path& path) {
    std::string stem = path.stem().string();
    std::vector<std::string> tokens;
    {
        std::string cur;
        cur.reserve(stem.size());
        for (char c : stem) {
            if (isPathSep(c)) {
                if (!cur.empty()) {
                    tokens.push_back(std::move(cur));
                    cur.clear();
                }
            } else {
                cur.push_back(c);
            }
        }
        if (!cur.empty()) {
            tokens.push_back(std::move(cur));
        }
    }

    const auto& re = keyTokenRe();

    // Iterate right-to-left: trailing key wins ("C_to_F.wav" -> F).
    for (auto i = static_cast<std::ptrdiff_t>(tokens.size()) - 1; i >= 0; --i) {
        std::smatch m;
        if (!std::regex_match(tokens[i], m, re)) {
            continue;
        }
        std::string root = m[1].matched ? m[1].str() : std::string{};
        std::string accidental = m[2].matched ? m[2].str() : std::string{};
        std::string scaleStr = m[3].matched ? m[3].str() : std::string{};

        // Split-token form: "C_minor", "F#_major" — peek the next token.
        if (scaleStr.empty() && i + 1 < static_cast<std::ptrdiff_t>(tokens.size())) {
            std::string nxt = toLower(tokens[i + 1]);
            if (nxt == "major" || nxt == "maj") {
                scaleStr = "maj";
            } else if (nxt == "minor" || nxt == "min") {
                scaleStr = "min";
            }
        }

        return normalizeKey(root, accidental, scaleStr);
    }
    return std::nullopt;
}

}  // namespace magda::media
