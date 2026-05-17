#include "StyledText.hpp"

#include "DarkTheme.hpp"

namespace magda {

namespace {

enum class StyleTag { Bold, Warning };

struct ParseState {
    std::vector<StyleTag> stack;
    juce::String pending;
    std::vector<StyledTextRun> runs;
};

StyledTextRun buildRunFromStack(const juce::String& text, const std::vector<StyleTag>& stack) {
    StyledTextRun run;
    run.text = text;
    for (auto tag : stack) {
        if (tag == StyleTag::Bold) {
            run.bold = true;
        } else if (tag == StyleTag::Warning) {
            run.hasColour = true;
            run.colour = DarkTheme::getColour(DarkTheme::STATUS_WARNING);
        }
    }
    return run;
}

void flushPending(ParseState& s) {
    if (s.pending.isEmpty()) {
        return;
    }
    s.runs.push_back(buildRunFromStack(s.pending, s.stack));
    s.pending.clear();
}

// Returns -1 if no recognised tag at position i. Otherwise advances *outNext past the closing '>'
// and applies the tag to the parser state.
bool handleTagAt(const juce::String& source, int i, int& outNext, ParseState& s) {
    const int len = source.length();
    if (i >= len || source[i] != '<') {
        return false;
    }
    const int close = source.indexOfChar(i + 1, '>');
    if (close < 0) {
        return false;
    }
    const juce::String inside = source.substring(i + 1, close).trim();
    if (inside.isEmpty()) {
        return false;
    }

    const bool isClose = inside.startsWithChar('/');
    const juce::String name = isClose ? inside.substring(1).trim() : inside;

    if (isClose) {
        // </> pops the top; </name> pops if top matches that name (otherwise treat as literal).
        if (s.stack.empty()) {
            return false;
        }
        if (name.isEmpty()) {
            flushPending(s);
            s.stack.pop_back();
            outNext = close + 1;
            return true;
        }
        StyleTag top = s.stack.back();
        const bool matches = (name == "b" && top == StyleTag::Bold) ||
                             (name == "warning" && top == StyleTag::Warning);
        if (!matches) {
            return false;
        }
        flushPending(s);
        s.stack.pop_back();
        outNext = close + 1;
        return true;
    }

    // Open tag
    StyleTag tag;
    if (name == "b") {
        tag = StyleTag::Bold;
    } else if (name == "warning") {
        tag = StyleTag::Warning;
    } else {
        return false;
    }
    flushPending(s);
    s.stack.push_back(tag);
    outNext = close + 1;
    return true;
}

}  // namespace

std::vector<StyledTextRun> parseStyledText(const juce::String& source) {
    ParseState s;
    const int len = source.length();
    int i = 0;
    while (i < len) {
        if (source[i] == '<') {
            int next = i;
            if (handleTagAt(source, i, next, s)) {
                i = next;
                continue;
            }
        }
        s.pending += source.substring(i, i + 1);
        ++i;
    }
    flushPending(s);
    return s.runs;
}

juce::String stripStyleTags(const juce::String& source) {
    juce::String out;
    auto runs = parseStyledText(source);
    for (auto& r : runs) {
        out += r.text;
    }
    return out;
}

StyledTextDisplay::StyledTextDisplay() : baseFont_(juce::FontOptions{}) {}

void StyledTextDisplay::setStyledText(const juce::String& sourceWithTags) {
    rawSource_ = sourceWithTags;
    rebuildAttributed();
    repaint();
}

void StyledTextDisplay::setBaseFont(const juce::Font& font) {
    baseFont_ = font;
    rebuildAttributed();
    repaint();
}

void StyledTextDisplay::setBaseColour(juce::Colour colour) {
    baseColour_ = colour;
    rebuildAttributed();
    repaint();
}

void StyledTextDisplay::rebuildAttributed() {
    attributed_.clear();
    attributed_.setJustification(juce::Justification::topLeft);
    attributed_.setWordWrap(juce::AttributedString::byWord);

    auto runs = parseStyledText(rawSource_);
    for (auto& r : runs) {
        if (r.text.isEmpty()) {
            continue;
        }
        juce::Font font = baseFont_;
        if (r.bold) {
            font = font.boldened();
        }
        const juce::Colour colour = r.hasColour ? r.colour : baseColour_;
        attributed_.append(r.text, font, colour);
    }
}

void StyledTextDisplay::paint(juce::Graphics& g) {
    attributed_.draw(g, getLocalBounds().toFloat());
}

}  // namespace magda
