#include "FaustMetadataParser.hpp"

#include <cmath>

namespace magda::daw::audio {

namespace {

// True iff `key` is one of the annotation keys we recognise. Used by
// parseFaustLabel to decide whether to strip the `[…]` from the clean
// label or keep it intact for forward-compatibility.
bool isKnownKey(const juce::String& key) {
    return key == "idx" || key == "unit" || key == "scale" || key == "style" || key == "role" ||
           key == "hidden" || key == "gate" || key == "scaleAnchor" || key == "scaleanchor";
}

// `[style:menu{'A':0;'B':1}]` payloads — the value passed to
// applyFaustAnnotation is `menu{'A':0;'B':1}`. Pull the inside of the
// braces out; everything before `{` is the kind ("menu" / "radio" /
// "knob" / …).
struct StyleParts {
    juce::String kind;
    juce::String braceBody;  // empty if no `{…}` section
    bool hasBraces = false;
};

StyleParts splitStyleValue(const juce::String& value) {
    StyleParts out;
    const auto open = value.indexOf("{");
    if (open < 0) {
        out.kind = value.trim();
        return out;
    }
    out.kind = value.substring(0, open).trim();
    const auto close = value.lastIndexOf("}");
    if (close > open) {
        out.braceBody = value.substring(open + 1, close);
        out.hasBraces = true;
    }
    return out;
}

// Strip surrounding quotes (single OR double) from a token. Faust uses
// single quotes around menu labels but we accept either to be friendly
// to LLM output that occasionally goes rogue.
juce::String unquote(const juce::String& s) {
    auto t = s.trim();
    if (t.length() >= 2) {
        const auto first = t[0];
        const auto last = t[t.length() - 1];
        if ((first == '\'' && last == '\'') || (first == '"' && last == '"'))
            return t.substring(1, t.length() - 1);
    }
    return t;
}

}  // namespace

std::vector<std::pair<float, juce::String>> parseMenuChoices(const juce::String& payload) {
    std::vector<std::pair<float, juce::String>> out;
    if (payload.isEmpty())
        return out;

    // Split on `;` — Faust separates entries with semicolons. Each
    // entry is `'<label>':<value>`.
    juce::StringArray entries;
    entries.addTokens(payload, ";", "");
    for (auto& entry : entries) {
        auto trimmed = entry.trim();
        if (trimmed.isEmpty())
            continue;
        const auto colon = trimmed.indexOfChar(':');
        if (colon < 0)
            continue;  // malformed entry, skip silently
        auto label = unquote(trimmed.substring(0, colon));
        auto valueText = trimmed.substring(colon + 1).trim();
        if (label.isEmpty() || valueText.isEmpty())
            continue;
        const auto value = valueText.getFloatValue();
        out.emplace_back(value, label);
    }
    return out;
}

bool applyFaustAnnotation(const juce::String& key, const juce::String& value,
                          ControlMetadata& metadata) {
    if (key == "idx") {
        metadata.slotIndex = value.trim().getIntValue();
        return true;
    }
    if (key == "unit") {
        metadata.unit = value.trim();
        return true;
    }
    if (key == "scale") {
        const auto v = value.trim().toLowerCase();
        metadata.logScale = (v == "log");
        // exp / lin recognised but not surfaced — see header comment.
        return true;
    }
    if (key == "style") {
        const auto parts = splitStyleValue(value);
        const auto kind = parts.kind.toLowerCase();
        if (kind == "menu" || kind == "radio") {
            metadata.isMenuStyle = true;
            metadata.menuChoices = parseMenuChoices(parts.braceBody);
        }
        // Other style values (knob / led / numerical) are recognised
        // and stripped from the clean label, but don't surface a flag —
        // they're purely visual hints we don't act on.
        return true;
    }
    if (key == "role") {
        const auto v = value.trim().toLowerCase();
        if (v == "projecttempo" || v == "project_tempo")
            metadata.role = FaustControlRole::ProjectTempo;
        else
            metadata.role = FaustControlRole::User;
        return true;
    }
    if (key == "hidden") {
        const auto v = value.trim();
        // Treat any non-zero / non-"false" value as true. Faust convention
        // is `[hidden:1]` but be lenient about "true" / "yes" / etc.
        metadata.hidden =
            !(v == "0" || v.equalsIgnoreCase("false") || v.equalsIgnoreCase("no") || v.isEmpty());
        return true;
    }
    if (key == "scaleAnchor" || key == "scaleanchor") {
        // [scaleAnchor:N] — places the slider's drag-midpoint at the
        // real-units value N. Required for `[scale:log]` to actually
        // feel logarithmic (without an off-centre anchor, MAGDA's
        // skewFactor collapses to 1.0 and drag is linear).
        const float n = value.trim().getFloatValue();
        if (std::isfinite(n))
            metadata.scaleAnchor = n;
        return true;
    }
    if (key == "gate") {
        auto v = value.trim();
        // `[gate:!N]` — leading `!` negates the condition (param is active
        // when the gate slot is OFF / < 0.5). The remainder is the slot index.
        if (v.startsWith("!")) {
            metadata.gateNegated = true;
            v = v.substring(1).trim();
        } else {
            metadata.gateNegated = false;
        }
        // Accept only valid non-negative integers; silently ignore malformed
        // values so a typo in the .dsp doesn't crash the parser.
        const int idx = v.getIntValue();
        if (idx >= 0 || v == "0")
            metadata.gateSlotIndex = idx;
        return true;
    }
    return false;
}

ParsedLabel parseFaustLabel(const juce::String& rawLabel) {
    ParsedLabel out;
    if (rawLabel.isEmpty())
        return out;

    juce::String clean;
    clean.preallocateBytes(static_cast<size_t>(rawLabel.getNumBytesAsUTF8()));

    const int len = rawLabel.length();
    int i = 0;
    while (i < len) {
        const auto c = rawLabel[i];
        if (c != '[') {
            clean += c;
            ++i;
            continue;
        }
        // Find the matching closing bracket. Faust style payloads can
        // contain `{…}` braces but not nested `[`, so a forward scan
        // for the next `]` is sufficient.
        int j = i + 1;
        while (j < len && rawLabel[j] != ']')
            ++j;
        if (j >= len) {
            // Unterminated bracket — keep verbatim so we don't silently
            // chew malformed input.
            clean += rawLabel.substring(i);
            break;
        }

        const auto inner = rawLabel.substring(i + 1, j);
        const auto colon = inner.indexOfChar(':');
        bool stripped = false;
        if (colon > 0) {
            const auto key = inner.substring(0, colon).trim().toLowerCase();
            const auto value = inner.substring(colon + 1);
            if (isKnownKey(key)) {
                applyFaustAnnotation(key, value, out.metadata);
                stripped = true;
            }
        }
        if (!stripped) {
            // Unknown annotation — keep it visible in the label.
            clean += rawLabel.substring(i, j + 1);
        }
        i = j + 1;
    }

    // Collapse runs of whitespace introduced by stripping annotations.
    juce::String collapsed;
    bool inSpace = false;
    for (int k = 0; k < clean.length(); ++k) {
        const auto c = clean[k];
        const bool isSpace = (c == ' ' || c == '\t');
        if (isSpace) {
            if (!inSpace && collapsed.isNotEmpty())
                collapsed += ' ';
            inSpace = true;
        } else {
            collapsed += c;
            inSpace = false;
        }
    }
    out.cleanLabel = collapsed.trim();
    return out;
}

void mergeFaustMetadata(ControlMetadata& parent, const ControlMetadata& child) {
    if (child.slotIndex != -1)
        parent.slotIndex = child.slotIndex;
    if (child.unit.isNotEmpty())
        parent.unit = child.unit;
    if (child.logScale)
        parent.logScale = true;
    if (child.isMenuStyle) {
        parent.isMenuStyle = true;
        parent.menuChoices = child.menuChoices;
    }
    // Role/hidden follow the same "non-default child wins" rule as the
    // other tags: a control-level annotation overrides a group-level
    // default. We can't distinguish "child explicitly said User" from
    // "child didn't say role" without a tri-state, but the only role
    // worth inheriting is ProjectTempo and that should always be
    // declared at the control itself, so the simple override is safe.
    if (child.role != FaustControlRole::User)
        parent.role = child.role;
    if (child.hidden)
        parent.hidden = true;
    // Gate: child wins when it explicitly declares a gate (index != -1).
    // Group-level gate tags are unusual but use the same child-wins rule as
    // every other annotation so the merge logic stays uniform.
    if (child.gateSlotIndex != -1) {
        parent.gateSlotIndex = child.gateSlotIndex;
        parent.gateNegated = child.gateNegated;
    }
    // scaleAnchor: NaN sentinel means "unset", any finite value wins.
    if (std::isfinite(child.scaleAnchor))
        parent.scaleAnchor = child.scaleAnchor;
}

}  // namespace magda::daw::audio
