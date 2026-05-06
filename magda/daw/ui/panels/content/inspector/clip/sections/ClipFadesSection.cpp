#include "ClipFadesSection.hpp"

#include "../../../../../themes/DarkTheme.hpp"
#include "../../../../../themes/FontManager.hpp"
#include "../../../../../themes/SmallButtonLookAndFeel.hpp"
#include "BinaryData.h"
#include "core/ClipBatchEdit.hpp"
#include "core/ClipPropertyCommands.hpp"
#include "core/UndoManager.hpp"

namespace magda::daw::ui {

namespace {
constexpr int SECTION_H = 14;
constexpr int ROW_H = 22;
constexpr int BTN_H = 24;
constexpr int GAP = 4;

// launchFadeSamples <-> ms conversion (44100 Hz fixed rate for UI display)
constexpr double kSampleRate = 44100.0;
inline double samplesToMs(int s) {
    return s / kSampleRate * 1000.0;
}
inline int msToSamples(double ms) {
    return static_cast<int>(std::round(ms / 1000.0 * kSampleRate));
}
}  // namespace

ClipFadesSection::ClipFadesSection() {
    initControls();
}

ClipFadesSection::~ClipFadesSection() = default;

void ClipFadesSection::initControls() {
    // Section label
    sectionLabel_.setText("Fades", juce::dontSendNotification);
    sectionLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    sectionLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    addChildComponent(sectionLabel_);

    // ── Arrangement-only controls ──

    fadeInValue_ =
        std::make_unique<magda::DraggableValueLabel>(magda::DraggableValueLabel::Format::Raw);
    fadeInValue_->setRange(0.0, 30.0, 0.0);
    fadeInValue_->setSuffix(" s");
    fadeInValue_->setDecimalPlaces(3);
    fadeInValue_->setDrawBackground(false);
    fadeInValue_->setDrawBorder(true);
    fadeInValue_->setShowFillIndicator(false);
    fadeInValue_->setTooltip("Fade In");
    fadeInValue_->onValueChange = [this]() {
        double current = fadeInValue_->getValue();
        double delta = current - multiFadeInDragStart_;
        magda::ClipBatchEdit batch("Set Clip Fade In", selectedClipIds_.size());
        for (auto cid : selectedClipIds_) {
            const auto* c = magda::ClipManager::getInstance().getClip(cid);
            if (c && c->isAudio() && c->view != magda::ClipView::Session) {
                double newVal = juce::jmax(0.0, c->fadeIn + delta);
                batch.execute(std::make_unique<magda::SetClipFadeInCommand>(cid, newVal));
            }
        }
        multiFadeInDragStart_ = current;
    };
    addChildComponent(*fadeInValue_);

    fadeOutValue_ =
        std::make_unique<magda::DraggableValueLabel>(magda::DraggableValueLabel::Format::Raw);
    fadeOutValue_->setRange(0.0, 30.0, 0.0);
    fadeOutValue_->setSuffix(" s");
    fadeOutValue_->setDecimalPlaces(3);
    fadeOutValue_->setDrawBackground(false);
    fadeOutValue_->setDrawBorder(true);
    fadeOutValue_->setShowFillIndicator(false);
    fadeOutValue_->setTooltip("Fade Out");
    fadeOutValue_->onValueChange = [this]() {
        double current = fadeOutValue_->getValue();
        double delta = current - multiFadeOutDragStart_;
        magda::ClipBatchEdit batch("Set Clip Fade Out", selectedClipIds_.size());
        for (auto cid : selectedClipIds_) {
            const auto* c = magda::ClipManager::getInstance().getClip(cid);
            if (c && c->isAudio() && c->view != magda::ClipView::Session) {
                double newVal = juce::jmax(0.0, c->fadeOut + delta);
                batch.execute(std::make_unique<magda::SetClipFadeOutCommand>(cid, newVal));
            }
        }
        multiFadeOutDragStart_ = current;
    };
    addChildComponent(*fadeOutValue_);

    // Fade curve type buttons (Linear, Convex, Concave, S-Curve)
    struct FadeTypeIcon {
        const char* name;
        const char* data;
        size_t size;
        const char* tooltip;
    };
    FadeTypeIcon fadeTypeIcons[] = {
        {"Linear", BinaryData::fade_linear_svg, BinaryData::fade_linear_svgSize, "Linear"},
        {"Convex", BinaryData::fade_convex_svg, BinaryData::fade_convex_svgSize, "Convex"},
        {"Concave", BinaryData::fade_concave_svg, BinaryData::fade_concave_svgSize, "Concave"},
        {"SCurve", BinaryData::fade_scurve_svg, BinaryData::fade_scurve_svgSize, "S-Curve"},
    };
    auto setupTypeBtn = [this](std::unique_ptr<magda::SvgButton>& btn, const FadeTypeIcon& icon) {
        btn = std::make_unique<magda::SvgButton>(icon.name, icon.data, icon.size);
        btn->setOriginalColor(juce::Colour(0xFFE3E3E3));
        btn->setNormalColor(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        btn->setHoverColor(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        btn->setActiveColor(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        btn->setBorderColor(DarkTheme::getColour(DarkTheme::BORDER));
        btn->setBorderThickness(1.0f);
        btn->setTooltip(icon.tooltip);
        btn->setClickingTogglesState(false);
        addChildComponent(*btn);
    };
    for (int i = 0; i < 4; ++i) {
        setupTypeBtn(fadeInTypeButtons_[i], fadeTypeIcons[i]);
        int fadeType = i + 1;
        fadeInTypeButtons_[i]->onClick = [this, i, fadeType]() {
            magda::ClipBatchEdit batch("Set Clip Fade In Type", selectedClipIds_.size());
            for (auto cid : selectedClipIds_)
                batch.execute(std::make_unique<magda::SetClipFadeInTypeCommand>(cid, fadeType));
            for (int j = 0; j < 4; ++j)
                fadeInTypeButtons_[j]->setActive(j == i);
        };
        setupTypeBtn(fadeOutTypeButtons_[i], fadeTypeIcons[i]);
        fadeOutTypeButtons_[i]->onClick = [this, i, fadeType]() {
            magda::ClipBatchEdit batch("Set Clip Fade Out Type", selectedClipIds_.size());
            for (auto cid : selectedClipIds_)
                batch.execute(std::make_unique<magda::SetClipFadeOutTypeCommand>(cid, fadeType));
            for (int j = 0; j < 4; ++j)
                fadeOutTypeButtons_[j]->setActive(j == i);
        };
    }

    // Fade behaviour buttons (Gain Fade, Speed Ramp)
    struct FadeBehaviourIcon {
        const char* name;
        const char* data;
        size_t size;
        const char* tooltip;
    };
    FadeBehaviourIcon fadeBehaviourIcons[] = {
        {"GainFade", BinaryData::fade_gain_svg, BinaryData::fade_gain_svgSize, "Gain Fade"},
        {"SpeedRamp", BinaryData::fade_speedramp_svg, BinaryData::fade_speedramp_svgSize,
         "Speed Ramp"},
    };
    auto setupBehaviourBtn = [this](std::unique_ptr<magda::SvgButton>& btn,
                                    const FadeBehaviourIcon& icon) {
        btn = std::make_unique<magda::SvgButton>(icon.name, icon.data, icon.size);
        btn->setOriginalColor(juce::Colour(0xFFE3E3E3));
        btn->setNormalColor(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        btn->setHoverColor(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        btn->setActiveColor(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        btn->setBorderColor(DarkTheme::getColour(DarkTheme::BORDER));
        btn->setBorderThickness(1.0f);
        btn->setTooltip(icon.tooltip);
        btn->setClickingTogglesState(false);
        addChildComponent(*btn);
    };
    for (int i = 0; i < 2; ++i) {
        setupBehaviourBtn(fadeInBehaviourButtons_[i], fadeBehaviourIcons[i]);
        fadeInBehaviourButtons_[i]->onClick = [this, i]() {
            magda::ClipBatchEdit batch("Set Clip Fade In Behaviour", selectedClipIds_.size());
            for (auto cid : selectedClipIds_)
                batch.execute(std::make_unique<magda::SetClipFadeInBehaviourCommand>(cid, i));
            for (int j = 0; j < 2; ++j)
                fadeInBehaviourButtons_[j]->setActive(j == i);
        };
        setupBehaviourBtn(fadeOutBehaviourButtons_[i], fadeBehaviourIcons[i]);
        fadeOutBehaviourButtons_[i]->onClick = [this, i]() {
            magda::ClipBatchEdit batch("Set Clip Fade Out Behaviour", selectedClipIds_.size());
            for (auto cid : selectedClipIds_)
                batch.execute(std::make_unique<magda::SetClipFadeOutBehaviourCommand>(cid, i));
            for (int j = 0; j < 2; ++j)
                fadeOutBehaviourButtons_[j]->setActive(j == i);
        };
    }

    autoCrossfadeToggle_.setButtonText("AUTO-XFADE");
    autoCrossfadeToggle_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    autoCrossfadeToggle_.setColour(juce::TextButton::buttonColourId,
                                   DarkTheme::getColour(DarkTheme::SURFACE));
    autoCrossfadeToggle_.setColour(juce::TextButton::buttonOnColourId,
                                   DarkTheme::getAccentColour().withAlpha(0.3f));
    autoCrossfadeToggle_.setColour(juce::TextButton::textColourOffId,
                                   DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    autoCrossfadeToggle_.setColour(juce::TextButton::textColourOnId, DarkTheme::getAccentColour());
    autoCrossfadeToggle_.onClick = [this]() {
        auto pid = primaryClipId();
        if (pid == magda::INVALID_CLIP_ID)
            return;
        const auto* clip = magda::ClipManager::getInstance().getClip(pid);
        if (!clip)
            return;
        bool newState = !clip->autoCrossfade;
        magda::ClipBatchEdit batch("Set Clip Auto Crossfade", selectedClipIds_.size());
        for (auto cid : selectedClipIds_)
            batch.execute(std::make_unique<magda::SetClipPropertyCommand>(
                cid, "Set Clip Auto Crossfade", [newState](auto& manager, magda::ClipId id) {
                    manager.setAutoCrossfade(id, newState);
                }));
    };
    addChildComponent(autoCrossfadeToggle_);

    // ── Session-only controls ──

    launchFadeLabel_.setText("Launch", juce::dontSendNotification);
    launchFadeLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    launchFadeLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    launchFadeLabel_.setTooltip("Launch fade smoothing (0 = preserve transient)");
    addChildComponent(launchFadeLabel_);

    launchFadeValue_ =
        std::make_unique<magda::DraggableValueLabel>(magda::DraggableValueLabel::Format::Raw);
    launchFadeValue_->setRange(0.0, 50.0, 0.0);  // 0–50ms
    launchFadeValue_->setSuffix(" ms");
    launchFadeValue_->setDecimalPlaces(1);
    launchFadeValue_->setDrawBackground(false);
    launchFadeValue_->setDrawBorder(true);
    launchFadeValue_->setShowFillIndicator(false);
    launchFadeValue_->setTooltip("Launch fade smoothing (0 = preserve transient)");
    launchFadeValue_->onValueChange = [this]() {
        int samples = msToSamples(launchFadeValue_->getValue());
        magda::ClipBatchEdit batch("Set Clip Launch Fade", selectedClipIds_.size());
        for (auto cid : selectedClipIds_) {
            const auto* c = magda::ClipManager::getInstance().getClip(cid);
            if (c && c->isAudio() && c->view == magda::ClipView::Session)
                batch.execute(
                    std::make_unique<magda::SetClipLaunchFadeSamplesCommand>(cid, samples));
        }
    };
    addChildComponent(*launchFadeValue_);
}

// ─────────────────────────────────────────────────────────────────────────────

void ClipFadesSection::setClip(magda::ClipId clipId) {
    std::unordered_set<magda::ClipId> s;
    if (clipId != magda::INVALID_CLIP_ID)
        s.insert(clipId);
    setSelectedClips(s);
}

void ClipFadesSection::setSelectedClips(const std::unordered_set<magda::ClipId>& clipIds) {
    selectedClipIds_ = clipIds;
    update();
}

// ─────────────────────────────────────────────────────────────────────────────

void ClipFadesSection::update() {
    auto pid = primaryClipId();
    if (pid == magda::INVALID_CLIP_ID) {
        setVisible(false);
        return;
    }

    const auto* clip = magda::ClipManager::getInstance().getClip(pid);
    if (!clip || !clip->isAudio()) {
        setVisible(false);
        return;
    }

    bool isSession = (clip->view == magda::ClipView::Session);

    sectionLabel_.setVisible(true);

    // Arrangement controls
    fadeInValue_->setVisible(!isSession);
    fadeOutValue_->setVisible(!isSession);
    for (int i = 0; i < 4; ++i) {
        fadeInTypeButtons_[i]->setVisible(!isSession);
        fadeOutTypeButtons_[i]->setVisible(!isSession);
    }
    for (int i = 0; i < 2; ++i) {
        fadeInBehaviourButtons_[i]->setVisible(!isSession);
        fadeOutBehaviourButtons_[i]->setVisible(!isSession);
    }
    autoCrossfadeToggle_.setVisible(!isSession);

    // Session controls
    launchFadeLabel_.setVisible(isSession);
    launchFadeValue_->setVisible(isSession);

    // Update values
    if (isSession) {
        launchFadeValue_->setValue(samplesToMs(clip->launchFadeSamples),
                                   juce::dontSendNotification);
    } else {
        bool isMulti = selectedClipIds_.size() > 1;
        if (isMulti) {
            // Show midpoint, set drag starts
            double minIn = clip->fadeIn, maxIn = clip->fadeIn;
            double minOut = clip->fadeOut, maxOut = clip->fadeOut;
            for (auto cid : selectedClipIds_) {
                if (cid == pid)
                    continue;
                const auto* c = magda::ClipManager::getInstance().getClip(cid);
                if (c && c->isAudio()) {
                    minIn = juce::jmin(minIn, (double)c->fadeIn);
                    maxIn = juce::jmax(maxIn, (double)c->fadeIn);
                    minOut = juce::jmin(minOut, (double)c->fadeOut);
                    maxOut = juce::jmax(maxOut, (double)c->fadeOut);
                }
            }
            double midIn = (minIn + maxIn) / 2.0;
            double midOut = (minOut + maxOut) / 2.0;
            fadeInValue_->setValue(midIn, juce::dontSendNotification);
            fadeOutValue_->setValue(midOut, juce::dontSendNotification);
            multiFadeInDragStart_ = midIn;
            multiFadeOutDragStart_ = midOut;
            if (std::abs(maxIn - minIn) > 0.001)
                fadeInValue_->setTextOverride("-");
            else
                fadeInValue_->clearTextOverride();
            if (std::abs(maxOut - minOut) > 0.001)
                fadeOutValue_->setTextOverride("-");
            else
                fadeOutValue_->clearTextOverride();
        } else {
            fadeInValue_->setValue(clip->fadeIn, juce::dontSendNotification);
            fadeOutValue_->setValue(clip->fadeOut, juce::dontSendNotification);
            fadeInValue_->clearTextOverride();
            fadeOutValue_->clearTextOverride();
            multiFadeInDragStart_ = clip->fadeIn;
            multiFadeOutDragStart_ = clip->fadeOut;
            for (int i = 0; i < 4; ++i) {
                fadeInTypeButtons_[i]->setActive(i == clip->fadeInType - 1);
                fadeOutTypeButtons_[i]->setActive(i == clip->fadeOutType - 1);
            }
            for (int i = 0; i < 2; ++i) {
                fadeInBehaviourButtons_[i]->setActive(i == clip->fadeInBehaviour);
                fadeOutBehaviourButtons_[i]->setActive(i == clip->fadeOutBehaviour);
            }
            autoCrossfadeToggle_.setToggleState(clip->autoCrossfade, juce::dontSendNotification);
        }
    }

    setVisible(true);
    if (getWidth() > 0 && getHeight() > 0)
        resized();
}

// ─────────────────────────────────────────────────────────────────────────────

bool ClipFadesSection::isAnyValueDragging() const {
    return (fadeInValue_ && fadeInValue_->isDragging()) ||
           (fadeOutValue_ && fadeOutValue_->isDragging()) ||
           (launchFadeValue_ && launchFadeValue_->isDragging());
}

int ClipFadesSection::getPreferredHeight() const {
    if (!isVisible())
        return 0;

    auto pid = primaryClipId();
    const auto* clip = magda::ClipManager::getInstance().getClip(pid);
    if (!clip || !clip->isAudio())
        return 0;

    bool isSession = (clip->view == magda::ClipView::Session);

    if (isSession) {
        // Section label + gap + launch row
        return SECTION_H + GAP + ROW_H;
    } else {
        // Section label + gap + fadeIn|fadeOut row + gap + type btns + gap + behaviour btns + gap +
        // AUTO-XFADE
        return SECTION_H + GAP + ROW_H + GAP + BTN_H + GAP + BTN_H + GAP + ROW_H;
    }
}

// ─────────────────────────────────────────────────────────────────────────────

void ClipFadesSection::resized() {
    auto b = getLocalBounds();
    if (b.getWidth() < 1 || b.getHeight() < 1)
        return;

    // Section label
    sectionLabel_.setBounds(b.removeFromTop(SECTION_H));
    b.removeFromTop(GAP);

    auto pid = primaryClipId();
    const auto* clip = magda::ClipManager::getInstance().getClip(pid);
    if (!clip)
        return;

    bool isSession = (clip->view == magda::ClipView::Session);

    if (isSession) {
        // Launch row: label (42px) + gap (4px) + value
        auto row = b.removeFromTop(ROW_H);
        launchFadeLabel_.setBounds(row.removeFromLeft(42));
        row.removeFromLeft(GAP);
        if (launchFadeValue_)
            launchFadeValue_->setBounds(row);
    } else {
        const int colGap = 8;
        int halfW = juce::jmax(1, (b.getWidth() - colGap) / 2);
        const int btnSize = 24;
        const int btnGap = 2;

        // fadeIn | fadeOut row (2 columns)
        {
            auto row = b.removeFromTop(ROW_H);
            fadeInValue_->setBounds(row.removeFromLeft(halfW));
            row.removeFromLeft(colGap);
            fadeOutValue_->setBounds(row);
        }
        b.removeFromTop(GAP);

        // Fade type buttons row
        {
            auto row = b.removeFromTop(BTN_H);
            auto left = row.removeFromLeft(halfW);
            row.removeFromLeft(colGap);
            auto right = row;
            for (int i = 0; i < 4; ++i) {
                if (fadeInTypeButtons_[i])
                    fadeInTypeButtons_[i]->setBounds(left.removeFromLeft(btnSize));
                if (i < 3)
                    left.removeFromLeft(btnGap);
                if (fadeOutTypeButtons_[i])
                    fadeOutTypeButtons_[i]->setBounds(right.removeFromLeft(btnSize));
                if (i < 3)
                    right.removeFromLeft(btnGap);
            }
        }
        b.removeFromTop(GAP);

        // Fade behaviour buttons row
        {
            auto row = b.removeFromTop(BTN_H);
            auto left = row.removeFromLeft(halfW);
            row.removeFromLeft(colGap);
            auto right = row;
            for (int i = 0; i < 2; ++i) {
                if (fadeInBehaviourButtons_[i])
                    fadeInBehaviourButtons_[i]->setBounds(left.removeFromLeft(btnSize));
                if (i < 1)
                    left.removeFromLeft(btnGap);
                if (fadeOutBehaviourButtons_[i])
                    fadeOutBehaviourButtons_[i]->setBounds(right.removeFromLeft(btnSize));
                if (i < 1)
                    right.removeFromLeft(btnGap);
            }
        }
        b.removeFromTop(GAP);

        // AUTO-XFADE full width
        autoCrossfadeToggle_.setBounds(b.removeFromTop(ROW_H).reduced(0, 1));
    }
}

}  // namespace magda::daw::ui
