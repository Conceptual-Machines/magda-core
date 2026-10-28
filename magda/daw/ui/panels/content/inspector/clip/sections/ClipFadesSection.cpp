// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ClipFadesSection.hpp"

#include "../../../../../themes/ActiveTheme.hpp"
#include "../../../../../themes/FontManager.hpp"
#include "../../../../../themes/SmallButtonLookAndFeel.hpp"
#include "../ClipInspectorStyle.hpp"
#include "BinaryData.h"
#include "core/ClipBatchEdit.hpp"
#include "core/ClipCommands.hpp"
#include "core/ClipPropertyCommands.hpp"
#include "core/UndoManager.hpp"

namespace magda::daw::ui {

namespace {
constexpr int SECTION_H = CollapsibleSectionHeader::kHeight;
constexpr int CAPTION_H = clip_style::kCaptionHeight;
constexpr int ROW_H = clip_style::kFieldHeight;
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
    header_.setAccessory(&autoCrossfadeSwitch_, ToggleSwitch::kWidth);
    header_.onCollapsedChanged = [this]() {
        update();
        if (onCollapsedChanged)
            onCollapsedChanged();
    };
    addAndMakeVisible(header_);

    clip_style::caption(inLabel_, "In");
    clip_style::caption(outLabel_, "Out");
    clip_style::caption(xfadeInLabel_, "Crossfade in");
    clip_style::caption(xfadeOutLabel_, "Crossfade out");
    for (auto* label : {&inLabel_, &outLabel_, &xfadeInLabel_, &xfadeOutLabel_})
        addChildComponent(*label);

    // ── Arrangement-only controls ──

    fadeInValue_ =
        std::make_unique<magda::DraggableValueLabel>(magda::DraggableValueLabel::Format::Raw);
    fadeInValue_->setRange(0.0, 30.0, 0.0);
    fadeInValue_->setDecimalPlaces(3);
    clip_style::field(*fadeInValue_, "s");
    fadeInValue_->setTooltip("Fade In");
    fadeInValue_->onValueChange = [this]() {
        double current = fadeInValue_->getValue();
        double delta = current - multiFadeInDragStart_;
        magda::ClipBatchEdit batch("Set Clip Fade In", selectedClipIds_.size());
        for (auto cid : selectedClipIds_) {
            const auto* c = magda::ClipManager::getInstance().getClip(cid);
            if (c && c->isAudio() && c->view != magda::ClipView::Session) {
                double newVal = juce::jmax(0.0, magda::audioEventRef(*c).fadeInSeconds + delta);
                batch.execute(std::make_unique<magda::SetClipFadeInCommand>(cid, newVal));
            }
        }
        multiFadeInDragStart_ = current;
    };
    addChildComponent(*fadeInValue_);

    fadeOutValue_ =
        std::make_unique<magda::DraggableValueLabel>(magda::DraggableValueLabel::Format::Raw);
    fadeOutValue_->setRange(0.0, 30.0, 0.0);
    fadeOutValue_->setDecimalPlaces(3);
    clip_style::field(*fadeOutValue_, "s");
    fadeOutValue_->setTooltip("Fade Out");
    fadeOutValue_->onValueChange = [this]() {
        double current = fadeOutValue_->getValue();
        double delta = current - multiFadeOutDragStart_;
        magda::ClipBatchEdit batch("Set Clip Fade Out", selectedClipIds_.size());
        for (auto cid : selectedClipIds_) {
            const auto* c = magda::ClipManager::getInstance().getClip(cid);
            if (c && c->isAudio() && c->view != magda::ClipView::Session) {
                double newVal = juce::jmax(0.0, magda::audioEventRef(*c).fadeOutSeconds + delta);
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
        btn->setNormalColor(ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
        btn->setHoverColor(ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
        btn->setActiveColor(ActiveTheme::getColour(ActiveTheme::ACCENT_PRIMARY));
        btn->setNormalBackgroundColor(ActiveTheme::SURFACE);
        btn->setActiveBackgroundColor(ActiveTheme::getAccentColour().withAlpha(0.22f));
        btn->setBorderColor(ActiveTheme::getColour(ActiveTheme::BORDER));
        btn->setBorderThickness(1.0f);
        btn->setCornerRadius(0.0f);
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
        btn->setNormalColor(ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
        btn->setHoverColor(ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
        btn->setActiveColor(ActiveTheme::getColour(ActiveTheme::ACCENT_PRIMARY));
        btn->setNormalBackgroundColor(ActiveTheme::SURFACE);
        btn->setActiveBackgroundColor(ActiveTheme::getAccentColour().withAlpha(0.22f));
        btn->setBorderColor(ActiveTheme::getColour(ActiveTheme::BORDER));
        btn->setBorderThickness(1.0f);
        btn->setCornerRadius(0.0f);
        btn->setTooltip(icon.tooltip);
        btn->setClickingTogglesState(false);
        addChildComponent(*btn);
    };
    for (int i = 0; i < 2; ++i) {
        setupBehaviourBtn(fadeInBehaviourButtons_[i], fadeBehaviourIcons[i]);
        setupBehaviourBtn(fadeOutBehaviourButtons_[i], fadeBehaviourIcons[i]);
        // The ramp's wave spans its whole viewBox, so it reads larger than the other glyphs.
        if (i == 1) {
            fadeInBehaviourButtons_[i]->setIconPadding(6.5f);
            fadeOutBehaviourButtons_[i]->setIconPadding(6.5f);
        }
        fadeInBehaviourButtons_[i]->onClick = [this, i]() {
            magda::ClipBatchEdit batch("Set Clip Fade In Behaviour", selectedClipIds_.size());
            for (auto cid : selectedClipIds_)
                batch.execute(std::make_unique<magda::SetClipFadeInBehaviourCommand>(cid, i));
            for (int j = 0; j < 2; ++j)
                fadeInBehaviourButtons_[j]->setActive(j == i);
        };
        fadeOutBehaviourButtons_[i]->onClick = [this, i]() {
            magda::ClipBatchEdit batch("Set Clip Fade Out Behaviour", selectedClipIds_.size());
            for (auto cid : selectedClipIds_)
                batch.execute(std::make_unique<magda::SetClipFadeOutBehaviourCommand>(cid, i));
            for (int j = 0; j < 2; ++j)
                fadeOutBehaviourButtons_[j]->setActive(j == i);
        };
    }

    // Crossfade duration values (#1499): resize the overlap with the
    // neighbouring clip around the joint centre. Beat-domain — the crossfade
    // is placement geometry, not a stored seconds fade.
    auto setupXfadeValue = [this](std::unique_ptr<magda::DraggableValueLabel>& value,
                                  const char* tooltip, bool atStart) {
        value =
            std::make_unique<magda::DraggableValueLabel>(magda::DraggableValueLabel::Format::Raw);
        value->setRange(0.0, 64.0, 0.0);
        value->setDecimalPlaces(2);
        clip_style::field(*value, "beats");
        value->setTooltip(tooltip);
        value->onValueChange = [this, atStart]() {
            const ClipId leftId = atStart ? xfadeInLeftId_ : xfadeOutLeftId_;
            const ClipId rightId = atStart ? xfadeInRightId_ : xfadeOutRightId_;
            auto& cm = magda::ClipManager::getInstance();
            const auto* left = cm.getClip(leftId);
            const auto* right = cm.getClip(rightId);
            if (!left || !right)
                return;
            const double dur =
                juce::jmax(0.0, atStart ? xfadeInValue_->getValue() : xfadeOutValue_->getValue());
            // Joint centre works for both the overlapping and the butted state,
            // so dragging through 0 and back up keeps working mid-gesture.
            const double centre = (right->placement.startBeat + left->placement.endBeat()) * 0.5;
            magda::UndoManager::getInstance().executeCommand(
                std::make_unique<magda::SetCrossfadeCommand>(leftId, rightId, centre - dur * 0.5,
                                                             centre + dur * 0.5));
        };
        addChildComponent(*value);
    };
    setupXfadeValue(xfadeInValue_, "Crossfade with previous clip (beats)", true);
    setupXfadeValue(xfadeOutValue_, "Crossfade with next clip (beats)", false);

    autoCrossfadeSwitch_.setTooltip("Crossfade automatically where clips overlap");
    autoCrossfadeSwitch_.onClick = [this]() {
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

    // ── Session-only controls ──

    clip_style::caption(launchFadeLabel_, "Launch");
    launchFadeLabel_.setTooltip("Launch fade smoothing (0 = preserve transient)");
    addChildComponent(launchFadeLabel_);

    launchFadeValue_ =
        std::make_unique<magda::DraggableValueLabel>(magda::DraggableValueLabel::Format::Raw);
    launchFadeValue_->setRange(0.0, 50.0, 0.0);  // 0–50ms
    launchFadeValue_->setDecimalPlaces(1);
    clip_style::field(*launchFadeValue_, "ms");
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
    // Only reassign on a real change. The batch handlers iterate
    // selectedClipIds_ while each executed command synchronously notifies the
    // host panel, which calls back into here with the same selection —
    // reassigning the set mid-iteration invalidates the loop's iterators and
    // the edit silently stops after the first clip.
    if (clipIds != selectedClipIds_)
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
    const bool open = !header_.isCollapsed();
    const bool showFades = open && !isSession;

    header_.setCaption(isSession ? juce::String() : juce::String("auto-xfade"));
    autoCrossfadeSwitch_.setVisible(!isSession);

    inLabel_.setVisible(showFades);
    outLabel_.setVisible(showFades);
    fadeInValue_->setVisible(showFades);
    fadeOutValue_->setVisible(showFades);
    for (int i = 0; i < 4; ++i) {
        fadeInTypeButtons_[i]->setVisible(showFades);
        fadeOutTypeButtons_[i]->setVisible(showFades);
    }
    for (int i = 0; i < 2; ++i) {
        fadeInBehaviourButtons_[i]->setVisible(showFades);
        fadeOutBehaviourButtons_[i]->setVisible(showFades);
    }

    // Crossfade rows: single arrangement clip whose edge has a crossfade OR a
    // crossfade-capable joint (abutting audio neighbour, value 0). Keying the
    // row off the joint — not the overlap — keeps it alive when the value is
    // dragged down to 0, and doubles as the creation affordance.
    hasXfadeIn_ = false;
    hasXfadeOut_ = false;
    xfadeInLeftId_ = xfadeInRightId_ = magda::INVALID_CLIP_ID;
    xfadeOutLeftId_ = xfadeOutRightId_ = magda::INVALID_CLIP_ID;
    if (!isSession && selectedClipIds_.size() == 1) {
        auto& cm = magda::ClipManager::getInstance();
        double xfadeInBeats = 0.0;
        double xfadeOutBeats = 0.0;
        if (auto xf = cm.getCrossfadeAtStart(pid)) {
            hasXfadeIn_ = true;
            xfadeInLeftId_ = xf->leftClipId;
            xfadeInRightId_ = xf->rightClipId;
            xfadeInBeats = xf->lengthBeats();
        } else if (auto prev = cm.findCrossfadeNeighbour(pid, true);
                   prev != magda::INVALID_CLIP_ID) {
            hasXfadeIn_ = true;
            xfadeInLeftId_ = prev;
            xfadeInRightId_ = pid;
        }
        if (auto xf = cm.getCrossfadeAtEnd(pid)) {
            hasXfadeOut_ = true;
            xfadeOutLeftId_ = xf->leftClipId;
            xfadeOutRightId_ = xf->rightClipId;
            xfadeOutBeats = xf->lengthBeats();
        } else if (auto next = cm.findCrossfadeNeighbour(pid, false);
                   next != magda::INVALID_CLIP_ID) {
            hasXfadeOut_ = true;
            xfadeOutLeftId_ = pid;
            xfadeOutRightId_ = next;
        }
        if (hasXfadeIn_ && !xfadeInValue_->isDragging())
            xfadeInValue_->setValue(xfadeInBeats, juce::dontSendNotification);
        if (hasXfadeOut_ && !xfadeOutValue_->isDragging())
            xfadeOutValue_->setValue(xfadeOutBeats, juce::dontSendNotification);
    }
    hasXfadeIn_ = hasXfadeIn_ && open;
    hasXfadeOut_ = hasXfadeOut_ && open;
    xfadeInValue_->setVisible(hasXfadeIn_);
    xfadeOutValue_->setVisible(hasXfadeOut_);
    xfadeInLabel_.setVisible(hasXfadeIn_);
    xfadeOutLabel_.setVisible(hasXfadeOut_);

    // Session controls
    launchFadeLabel_.setVisible(isSession && open);
    launchFadeValue_->setVisible(isSession && open);

    // Update values
    if (isSession) {
        launchFadeValue_->setValue(samplesToMs(clip->launchFadeSamples),
                                   juce::dontSendNotification);
    } else {
        bool isMulti = selectedClipIds_.size() > 1;
        if (isMulti) {
            // Show midpoint, set drag starts
            double minIn = magda::audioEventRef(*clip).fadeInSeconds,
                   maxIn = magda::audioEventRef(*clip).fadeInSeconds;
            double minOut = magda::audioEventRef(*clip).fadeOutSeconds,
                   maxOut = magda::audioEventRef(*clip).fadeOutSeconds;
            for (auto cid : selectedClipIds_) {
                if (cid == pid)
                    continue;
                const auto* c = magda::ClipManager::getInstance().getClip(cid);
                if (c && c->isAudio()) {
                    minIn = juce::jmin(minIn, magda::audioEventRef(*c).fadeInSeconds);
                    maxIn = juce::jmax(maxIn, magda::audioEventRef(*c).fadeInSeconds);
                    minOut = juce::jmin(minOut, magda::audioEventRef(*c).fadeOutSeconds);
                    maxOut = juce::jmax(maxOut, magda::audioEventRef(*c).fadeOutSeconds);
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
            // The toggle acts on (and flips against) the primary clip's state,
            // so reflect that state — otherwise multi-select clicks look inert.
            autoCrossfadeSwitch_.setOn(clip->autoCrossfade);
        } else {
            fadeInValue_->setValue(magda::audioEventRef(*clip).fadeInSeconds,
                                   juce::dontSendNotification);
            fadeOutValue_->setValue(magda::audioEventRef(*clip).fadeOutSeconds,
                                    juce::dontSendNotification);
            fadeInValue_->clearTextOverride();
            fadeOutValue_->clearTextOverride();
            multiFadeInDragStart_ = magda::audioEventRef(*clip).fadeInSeconds;
            multiFadeOutDragStart_ = magda::audioEventRef(*clip).fadeOutSeconds;
            for (int i = 0; i < 4; ++i) {
                fadeInTypeButtons_[i]->setActive(i == magda::audioEventRef(*clip).fadeInType - 1);
                fadeOutTypeButtons_[i]->setActive(i == magda::audioEventRef(*clip).fadeOutType - 1);
            }
            for (int i = 0; i < 2; ++i) {
                fadeInBehaviourButtons_[i]->setActive(i ==
                                                      magda::audioEventRef(*clip).fadeInBehaviour);
                fadeOutBehaviourButtons_[i]->setActive(
                    i == magda::audioEventRef(*clip).fadeOutBehaviour);
            }
            autoCrossfadeSwitch_.setOn(clip->autoCrossfade);
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
           (xfadeInValue_ && xfadeInValue_->isDragging()) ||
           (xfadeOutValue_ && xfadeOutValue_->isDragging()) ||
           (launchFadeValue_ && launchFadeValue_->isDragging());
}

int ClipFadesSection::getPreferredHeight() const {
    if (!isVisible())
        return 0;

    auto pid = primaryClipId();
    const auto* clip = magda::ClipManager::getInstance().getClip(pid);
    if (!clip || !clip->isAudio())
        return 0;
    if (header_.isCollapsed())
        return SECTION_H;

    if (clip->view == magda::ClipView::Session)
        return SECTION_H + ROW_H + GAP;

    int h = SECTION_H + CAPTION_H + ROW_H + GAP + BTN_H + GAP + BTN_H + GAP;
    if (hasXfadeIn_ || hasXfadeOut_)
        h += CAPTION_H + ROW_H + GAP;
    return h;
}

// ─────────────────────────────────────────────────────────────────────────────

void ClipFadesSection::resized() {
    auto b = getLocalBounds();
    if (b.getWidth() < 1 || b.getHeight() < 1)
        return;

    header_.setBounds(b.removeFromTop(SECTION_H));
    if (header_.isCollapsed())
        return;

    auto pid = primaryClipId();
    const auto* clip = magda::ClipManager::getInstance().getClip(pid);
    if (!clip)
        return;

    if (clip->view == magda::ClipView::Session) {
        auto row = b.removeFromTop(ROW_H);
        launchFadeLabel_.setBounds(row.removeFromLeft(56));
        if (launchFadeValue_)
            launchFadeValue_->setBounds(row);
        return;
    }

    const int colGap = 8;
    const int halfW = juce::jmax(1, (b.getWidth() - colGap) / 2);
    auto columns = [&](int height) {
        auto row = b.removeFromTop(height);
        auto left = row.removeFromLeft(halfW);
        row.removeFromLeft(colGap);
        return std::pair{left, row};
    };
    // Buttons share the column edge to edge, so their borders join into one strip.
    auto strip = [](juce::Rectangle<int> area, std::unique_ptr<magda::SvgButton>* buttons,
                    int count) {
        const int w = area.getWidth() / count;
        for (int i = 0; i < count; ++i) {
            auto cell = i == count - 1 ? area : area.removeFromLeft(w);
            buttons[i]->setBounds(i == 0 ? cell : cell.withTrimmedLeft(-1));
        }
    };

    {
        auto [left, right] = columns(CAPTION_H);
        inLabel_.setBounds(left);
        outLabel_.setBounds(right);
    }
    {
        auto [left, right] = columns(ROW_H);
        fadeInValue_->setBounds(left);
        fadeOutValue_->setBounds(right);
    }
    b.removeFromTop(GAP);
    {
        auto [left, right] = columns(BTN_H);
        strip(left, fadeInTypeButtons_, 4);
        strip(right, fadeOutTypeButtons_, 4);
    }
    b.removeFromTop(GAP);
    {
        auto [left, right] = columns(BTN_H);
        strip(left, fadeInBehaviourButtons_, 2);
        strip(right, fadeOutBehaviourButtons_, 2);
    }
    b.removeFromTop(GAP);

    if (hasXfadeIn_ || hasXfadeOut_) {
        {
            auto [left, right] = columns(CAPTION_H);
            xfadeInLabel_.setBounds(left);
            xfadeOutLabel_.setBounds(right);
        }
        auto [left, right] = columns(ROW_H);
        xfadeInValue_->setBounds(left);
        xfadeOutValue_->setBounds(right);
    }
}

}  // namespace magda::daw::ui
