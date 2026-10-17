#include "ConsoleTranscript.hpp"

#include <algorithm>
#include <cmath>

#include "../../themes/ActiveTheme.hpp"
#include "../../themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {

const juce::juce_wchar kUserMark = 0x25CF;   // ●
const juce::juce_wchar kMagdaMark = 0x25C6;  // ◆
const juce::juce_wchar kDimMark = 0x200B;    // zero-width space opens a dimmed line

constexpr int kPad = 12;
constexpr int kLabelHeight = 18;
constexpr int kBlockGap = 14;
constexpr int kBubblePadX = 12;
constexpr int kBubblePadY = 8;

juce::Font bodyFont() {
    return FontManager::getInstance().getUIFont(13.0f);
}

juce::Font labelFont() {
    return FontManager::getInstance().getMonoFont(10.5f).withExtraKerningFactor(0.08f);
}

}  // namespace

class ConsoleTranscript::Content : public juce::Component {
  public:
    explicit Content(ConsoleTranscript& owner) : owner_(owner) {}

    void paint(juce::Graphics& g) override {
        if (owner_.blocks_.empty() && owner_.emptyHint_.isNotEmpty()) {
            paintEmptyState(g);
            return;
        }
        for (const auto& b : owner_.blocks_) {
            if (b.role == Role::User) {
                if (b.showLabel) {
                    const auto label =
                        juce::Rectangle<int>(b.bounds.getX(), b.bounds.getY() - kLabelHeight,
                                             b.bounds.getWidth(), kLabelHeight - 4);
                    drawUserLabel(g, label, b.target);
                }
                const auto bubble = b.bounds.toFloat();
                g.setColour(ActiveTheme::getColour(ActiveTheme::SURFACE));
                g.fillRoundedRectangle(bubble, 6.0f);
                g.setColour(ActiveTheme::getAccentColour().withAlpha(0.35f));
                g.drawRoundedRectangle(bubble.reduced(0.5f), 6.0f, 1.0f);
                b.layout.draw(g, bubble.reduced(kBubblePadX, kBubblePadY));
            } else {
                if (b.showLabel) {
                    g.setFont(labelFont());
                    g.setColour(ActiveTheme::getSecondaryTextColour());
                    g.drawText("MAGDA", b.bounds.getX(), b.bounds.getY() - kLabelHeight,
                               b.bounds.getWidth(), kLabelHeight - 4,
                               juce::Justification::bottomLeft, false);
                }
                b.layout.draw(g, b.bounds.toFloat());
            }
        }
    }

  private:
    void paintEmptyState(juce::Graphics& g) {
        const auto area = getLocalBounds().reduced(kPad * 2);
        const auto c = area.getCentre().toFloat().translated(0.0f, -22.0f);
        juce::Path diamond;
        diamond.addQuadrilateral(c.x, c.y - 8.0f, c.x + 8.0f, c.y, c.x, c.y + 8.0f, c.x - 8.0f,
                                 c.y);
        g.setColour(ActiveTheme::getSecondaryTextColour());
        g.strokePath(diamond, juce::PathStrokeType(1.3f));
        g.setFont(bodyFont());
        g.drawFittedText(owner_.emptyHint_, area.withTop(juce::roundToInt(c.y) + 18).withHeight(40),
                         juce::Justification::centredTop, 3);
    }

    static void drawUserLabel(juce::Graphics& g, juce::Rectangle<int> area,
                              const juce::String& target) {
        const auto font = labelFont();
        g.setFont(font);
        const juce::String you = "YOU";
        if (target.isEmpty()) {
            g.setColour(ActiveTheme::getSecondaryTextColour());
            g.drawText(you, area, juce::Justification::bottomRight, false);
            return;
        }
        const auto arrow = juce::String::fromUTF8(" \xe2\x86\x92 ");
        const int targetWidth =
            juce::roundToInt(juce::GlyphArrangement::getStringWidth(font, target)) + 1;
        g.setColour(ActiveTheme::getAccentColour());
        g.drawText(target, area.removeFromRight(targetWidth), juce::Justification::bottomRight,
                   false);
        g.setColour(ActiveTheme::getSecondaryTextColour());
        g.drawText(you + arrow, area, juce::Justification::bottomRight, false);
    }

    ConsoleTranscript& owner_;
};

ConsoleTranscript::ConsoleTranscript() : content_(std::make_unique<Content>(*this)) {
    viewport_.setViewedComponent(content_.get(), false);
    viewport_.setScrollBarsShown(true, false);
    viewport_.setScrollBarThickness(6);
    addAndMakeVisible(viewport_);
}

ConsoleTranscript::~ConsoleTranscript() = default;

void ConsoleTranscript::setText(const juce::String& text) {
    if (text == text_)
        return;
    text_ = text;
    rebuild();
}

void ConsoleTranscript::append(const juce::String& text) {
    setText(text_ + text);
}

void ConsoleTranscript::appendDim(const juce::String& text) {
    juce::StringArray lines;
    lines.addLines(text);
    for (auto& line : lines)
        if (line.isNotEmpty())
            line = juce::String::charToString(kDimMark) + line;
    append(lines.joinIntoString("\n"));
}

juce::String ConsoleTranscript::getPlainText() const {
    return text_.removeCharacters(juce::String::charToString(kDimMark));
}

void ConsoleTranscript::noteNextUserTarget(const juce::String& target) {
    int users = 0;
    for (const auto& b : blocks_)
        users += b.role == Role::User ? 1 : 0;
    while (userTargets_.size() < users)
        userTargets_.add({});
    userTargets_.removeRange(users, userTargets_.size());
    userTargets_.add(target);
}

void ConsoleTranscript::setEmptyHint(const juce::String& hint) {
    emptyHint_ = hint;
    repaint();
}

void ConsoleTranscript::resized() {
    viewport_.setBounds(getLocalBounds());
    layoutBlocks();
}

void ConsoleTranscript::lookAndFeelChanged() {
    rebuild();
}

void ConsoleTranscript::rebuild() {
    blocks_.clear();
    juce::StringArray lines;
    lines.addLines(text_);

    int userIndex = 0;
    for (const auto& raw : lines) {
        const bool dim = raw.startsWithChar(kDimMark);
        const auto line = dim ? raw.substring(1) : raw;
        const bool opensUser = line.startsWithChar(kUserMark);
        const bool opensMagda = line.startsWithChar(kMagdaMark);

        if (opensUser || opensMagda || blocks_.empty() || blocks_.back().dim != dim) {
            Block b;
            b.role = opensUser ? Role::User : Role::Magda;
            b.dim = dim;
            if (opensUser)
                b.target = userTargets_[userIndex++];
            b.text = opensUser || opensMagda ? line.substring(1).trimStart() : line;
            // The greeting line is the console's empty state, not a message.
            if (opensMagda && b.text == "MAGDA")
                continue;
            blocks_.push_back(std::move(b));
        } else {
            blocks_.back().text += "\n" + line;
        }
    }

    for (auto& b : blocks_) {
        b.text = b.text.trimEnd();
        if (b.text.startsWith("[!] ")) {
            b.error = true;
            b.text = b.text.substring(4);
        }
        if (b.role == Role::Magda && b.text.startsWith("Thinking"))
            b.dim = true;
    }
    blocks_.erase(std::remove_if(blocks_.begin(), blocks_.end(),
                                 [](const Block& b) { return b.text.isEmpty(); }),
                  blocks_.end());

    layoutBlocks();
}

void ConsoleTranscript::layoutBlocks() {
    const int width = juce::jmax(80, viewport_.getMaximumVisibleWidth());
    const bool wasAtBottom =
        viewport_.getViewPositionY() + viewport_.getViewHeight() >= content_->getHeight() - 4;

    int y = kPad;
    Role previous = Role::User;
    bool first = true;
    for (auto& b : blocks_) {
        juce::AttributedString s;
        s.setWordWrap(juce::AttributedString::byWord);
        const auto colour = b.error ? ActiveTheme::getColour(ActiveTheme::STATUS_ERROR)
                            : b.dim ? ActiveTheme::getColour(ActiveTheme::TEXT_DIM)
                                    : ActiveTheme::getTextColour();
        s.append(b.text, bodyFont(), colour);

        b.showLabel = b.role == Role::User || first || previous != Role::Magda;
        if (!first)
            y += b.showLabel ? kBlockGap : 4;
        if (b.showLabel)
            y += kLabelHeight;

        if (b.role == Role::User) {
            const int maxText = juce::roundToInt((width - kPad * 2) * 0.85f) - kBubblePadX * 2;
            b.layout.createLayout(s, static_cast<float>(maxText));
            const int textWidth =
                juce::jmin(maxText, static_cast<int>(std::ceil(b.layout.getWidth())) + 1);
            const int w = textWidth + kBubblePadX * 2;
            const int h = static_cast<int>(std::ceil(b.layout.getHeight())) + kBubblePadY * 2;
            b.layout.createLayout(s, static_cast<float>(textWidth));
            b.bounds = {width - kPad - w, y, w, h};
        } else {
            b.layout.createLayout(s, static_cast<float>(width - kPad * 2));
            b.bounds = {kPad, y, width - kPad * 2,
                        static_cast<int>(std::ceil(b.layout.getHeight()))};
        }
        y = b.bounds.getBottom();
        previous = b.role;
        first = false;
    }

    content_->setSize(width, juce::jmax(y + kPad, viewport_.getHeight()));
    if (wasAtBottom)
        viewport_.setViewPosition(0, content_->getHeight());
    content_->repaint();
    repaint();
}

}  // namespace magda::daw::ui
