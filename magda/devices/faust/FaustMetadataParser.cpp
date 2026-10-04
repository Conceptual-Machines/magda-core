#include "devices/faust/FaustMetadataParser.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace magda::devices::faust {

namespace {

bool isBlank(char c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && isBlank(text.front()))
        text.remove_prefix(1);
    while (!text.empty() && isBlank(text.back()))
        text.remove_suffix(1);
    return text;
}

std::string toLower(std::string_view text) {
    std::string lower(text);
    std::ranges::transform(lower, lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower;
}

/// Leading blanks, an optional sign and digits; 0 when there are none.
int readInt(std::string_view text) {
    text = trim(text);
    bool negative = false;
    if (!text.empty() && (text.front() == '-' || text.front() == '+')) {
        negative = text.front() == '-';
        text.remove_prefix(1);
    }
    long long value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9')
            break;
        value = value * 10 + (c - '0');
        if (value > 0x7fffffffLL)
            break;
    }
    return static_cast<int>(negative ? -value : value);
}

/// Leading blanks, sign, digits, point and exponent; 0 when there is no number.
float readFloat(std::string_view text) {
    text = trim(text);
    std::size_t end = 0;
    const auto digitsFrom = [&text](std::size_t at) {
        while (at < text.size() && text[at] >= '0' && text[at] <= '9')
            ++at;
        return at;
    };
    if (end < text.size() && (text[end] == '-' || text[end] == '+'))
        ++end;
    end = digitsFrom(end);
    if (end < text.size() && text[end] == '.')
        end = digitsFrom(end + 1);
    if (end < text.size() && (text[end] == 'e' || text[end] == 'E')) {
        auto exponent = end + 1;
        if (exponent < text.size() && (text[exponent] == '-' || text[exponent] == '+'))
            ++exponent;
        const auto exponentEnd = digitsFrom(exponent);
        if (exponentEnd > exponent)
            end = exponentEnd;
    }
    const std::string number(text.substr(0, end));
    return static_cast<float>(std::strtod(number.c_str(), nullptr));
}

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::ranges::equal(a, b, [](unsigned char x, unsigned char y) {
               return std::tolower(x) == std::tolower(y);
           });
}

// Keys whose `[…]` parseFaustLabel strips; unknown ones stay in the label.
bool isKnownKey(std::string_view key) {
    return key == "idx" || key == "unit" || key == "scale" || key == "style" || key == "role" ||
           key == "hidden" || key == "gate" || key == "scaleAnchor" || key == "scaleanchor" ||
           key == "tooltip" || key == "width";
}

/// `menu{'A':0;'B':1}` as its kind and the inside of its braces.
struct StyleParts {
    std::string_view kind;
    std::string_view braceBody;
};

StyleParts splitStyleValue(std::string_view value) {
    const auto open = value.find('{');
    if (open == std::string_view::npos)
        return {trim(value), {}};
    StyleParts out{trim(value.substr(0, open)), {}};
    const auto close = value.rfind('}');
    if (close != std::string_view::npos && close > open)
        out.braceBody = value.substr(open + 1, close - open - 1);
    return out;
}

// Single or double quotes: LLM-written dsp sometimes uses double.
std::string_view unquote(std::string_view s) {
    auto t = trim(s);
    if (t.size() >= 2 &&
        ((t.front() == '\'' && t.back() == '\'') || (t.front() == '"' && t.back() == '"')))
        return t.substr(1, t.size() - 2);
    return t;
}

}  // namespace

std::vector<std::pair<float, std::string>> parseMenuChoices(std::string_view payload) {
    std::vector<std::pair<float, std::string>> out;
    while (!payload.empty()) {
        const auto semicolon = payload.find(';');
        const auto entry = trim(payload.substr(0, semicolon));
        payload = semicolon == std::string_view::npos ? std::string_view{}
                                                      : payload.substr(semicolon + 1);
        const auto colon = entry.find(':');
        if (entry.empty() || colon == std::string_view::npos)
            continue;
        const auto label = unquote(entry.substr(0, colon));
        const auto valueText = trim(entry.substr(colon + 1));
        if (label.empty() || valueText.empty())
            continue;
        out.emplace_back(readFloat(valueText), std::string(label));
    }
    return out;
}

bool applyFaustAnnotation(std::string_view key, std::string_view value, ControlMetadata& metadata) {
    if (key == "idx") {
        metadata.slotIndex = readInt(value);
        return true;
    }
    if (key == "unit") {
        metadata.unit = std::string(trim(value));
        return true;
    }
    if (key == "scale") {
        // exp and lin are recognised but not surfaced.
        metadata.logScale = toLower(trim(value)) == "log";
        return true;
    }
    if (key == "style") {
        const auto parts = splitStyleValue(value);
        const auto kind = toLower(parts.kind);
        if (kind == "menu" || kind == "radio") {
            metadata.choiceStyle =
                (kind == "radio") ? FaustChoiceStyle::Radio : FaustChoiceStyle::Menu;
            metadata.menuChoices = parseMenuChoices(parts.braceBody);
        } else if (kind == "led") {
            metadata.outputStyle = FaustOutputStyle::Led;
        } else if (kind == "numerical") {
            metadata.outputStyle = FaustOutputStyle::Numerical;
        }
        return true;
    }
    if (key == "width") {
        // Below 1 is ignored rather than clamped to a zero-width cell.
        const int n = readInt(value);
        if (n >= 1)
            metadata.widthCells = n;
        return true;
    }
    if (key == "tooltip") {
        // Authors carry the quotes over from the `declare` form, where they are required.
        auto v = trim(value);
        if (v.size() >= 2 && v.front() == '"' && v.back() == '"')
            v = v.substr(1, v.size() - 2);
        metadata.tooltip = std::string(v);
        return true;
    }
    if (key == "role") {
        const auto v = toLower(trim(value));
        metadata.role = (v == "projecttempo" || v == "project_tempo")
                            ? FaustControlRole::ProjectTempo
                            : FaustControlRole::User;
        return true;
    }
    if (key == "hidden") {
        const auto v = trim(value);
        metadata.hidden =
            !(v == "0" || equalsIgnoreCase(v, "false") || equalsIgnoreCase(v, "no") || v.empty());
        return true;
    }
    if (key == "scaleAnchor" || key == "scaleanchor") {
        const float n = readFloat(value);
        if (std::isfinite(n))
            metadata.scaleAnchor = n;
        return true;
    }
    if (key == "gate") {
        auto v = trim(value);
        metadata.gateNegated = v.starts_with('!');
        if (metadata.gateNegated)
            v = trim(v.substr(1));
        // A malformed index is ignored so a typo in the dsp cannot break the harvest.
        const int idx = readInt(v);
        if (idx >= 0 || v == "0")
            metadata.gateSlotIndex = idx;
        return true;
    }
    return false;
}

ParsedLabel parseFaustLabel(std::string_view rawLabel) {
    ParsedLabel out;
    if (rawLabel.empty())
        return out;

    std::string clean;
    clean.reserve(rawLabel.size());

    std::size_t i = 0;
    while (i < rawLabel.size()) {
        if (rawLabel[i] != '[') {
            clean += rawLabel[i++];
            continue;
        }
        // Style payloads hold braces but never a nested '[', so the next ']' closes it.
        const auto j = rawLabel.find(']', i + 1);
        if (j == std::string_view::npos) {
            clean += rawLabel.substr(i);
            break;
        }

        const auto inner = rawLabel.substr(i + 1, j - i - 1);
        const auto colon = inner.find(':');
        bool stripped = false;
        if (colon != std::string_view::npos && colon > 0) {
            const auto key = toLower(trim(inner.substr(0, colon)));
            if (isKnownKey(key)) {
                applyFaustAnnotation(key, inner.substr(colon + 1), out.metadata);
                stripped = true;
            }
        }
        if (!stripped)
            clean += rawLabel.substr(i, j - i + 1);
        i = j + 1;
    }

    // Collapse the runs of spaces and tabs that stripping leaves behind.
    std::string collapsed;
    bool inSpace = false;
    for (const char c : clean) {
        if (c == ' ' || c == '\t') {
            if (!inSpace && !collapsed.empty())
                collapsed += ' ';
            inSpace = true;
        } else {
            collapsed += c;
            inSpace = false;
        }
    }
    out.cleanLabel = std::string(trim(collapsed));
    return out;
}

void mergeFaustMetadata(ControlMetadata& parent, const ControlMetadata& child) {
    if (child.slotIndex != -1)
        parent.slotIndex = child.slotIndex;
    if (!child.unit.empty())
        parent.unit = child.unit;
    if (child.logScale)
        parent.logScale = true;
    if (child.isChoiceStyle()) {
        parent.choiceStyle = child.choiceStyle;
        parent.menuChoices = child.menuChoices;
    }
    if (child.outputStyle != FaustOutputStyle::Bar)
        parent.outputStyle = child.outputStyle;
    if (!child.tooltip.empty())
        parent.tooltip = child.tooltip;
    if (child.widthCells > 1)
        parent.widthCells = child.widthCells;
    // A non-default child wins. Only ProjectTempo is worth inheriting, and it is always declared
    // on the control itself, so no tri-state is needed.
    if (child.role != FaustControlRole::User)
        parent.role = child.role;
    if (child.hidden)
        parent.hidden = true;
    if (child.gateSlotIndex != -1) {
        parent.gateSlotIndex = child.gateSlotIndex;
        parent.gateNegated = child.gateNegated;
    }
    if (std::isfinite(child.scaleAnchor))
        parent.scaleAnchor = child.scaleAnchor;
}

}  // namespace magda::devices::faust
