// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ProgramChangeLaneComponent.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "../../themes/ActiveTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "VelocityLaneUtils.hpp"
#include "core/ClipManager.hpp"
#include "core/MidiNoteCommands.hpp"
#include "core/UndoManager.hpp"
#include "ui/components/common/MagdaAlertWindow.hpp"

namespace magda {

namespace {

constexpr int kHandleInset = 3;
/// How close to the line between two blocks, in pixels, a press grabs it.
constexpr int kEdgeGrabPixels = 4;
constexpr int kHandlePadding = 5;
constexpr int kMinHandleWidth = 12;

juce::Font labelFont() {
    return FontManager::getInstance().getUIFont(10.0f);
}

/** @brief A 0-127 value from a text field; empty is no value. */
std::optional<int> parseMidiByte(const juce::String& text) {
    const auto trimmed = text.trim();
    if (trimmed.isEmpty())
        return std::nullopt;
    return juce::jlimit(0, 127, trimmed.getIntValue());
}

juce::String byteText(int value) {
    return value >= 0 ? juce::String(value) : juce::String();
}

}  // namespace

juce::String programChangeLabel(const MidiProgramChangeData& change) {
    auto label = "Program " + juce::String(change.program);
    if (change.bankMsb < 0 && change.bankLsb < 0)
        return label;
    const auto part = [](int value) {
        return value >= 0 ? juce::String(value) : juce::String("-");
    };
    return label + juce::String(juce::CharPointer_UTF8(" \xc2\xb7 Bank ")) + part(change.bankMsb) +
           "/" + part(change.bankLsb);
}

ProgramChangeLaneComponent::ProgramChangeLaneComponent() {
    setName("ProgramChangeLane");
    setWantsKeyboardFocus(true);
}

// ============================================================================
// Configuration
// ============================================================================

void ProgramChangeLaneComponent::setClip(ClipId clipId) {
    if (clipId_ != clipId) {
        clipId_ = clipId;
        selected_.reset();
        repaint();
    }
}

void ProgramChangeLaneComponent::setPixelsPerBeat(double ppb) {
    pixelsPerBeat_ = ppb;
    repaint();
}

void ProgramChangeLaneComponent::setScrollOffset(int offsetX) {
    scrollOffsetX_ = offsetX;
    repaint();
}

void ProgramChangeLaneComponent::setLeftPadding(int padding) {
    leftPadding_ = padding;
    repaint();
}

void ProgramChangeLaneComponent::setRelativeMode(bool relative) {
    relativeMode_ = relative;
    repaint();
}

void ProgramChangeLaneComponent::setClipStartBeats(double startBeats) {
    clipStartBeats_ = startBeats;
    repaint();
}

void ProgramChangeLaneComponent::setClipLengthBeats(double lengthBeats) {
    clipLengthBeats_ = lengthBeats;
    repaint();
}

void ProgramChangeLaneComponent::setLoopRegion(double, double, bool) {}

void ProgramChangeLaneComponent::refreshEvents() {
    const auto* clip = ClipManager::getInstance().getClip(clipId_);
    if (selected_ && (clip == nullptr || *selected_ >= clip->midiProgramChangeData.size()))
        selected_.reset();
    repaint();
}

// ============================================================================
// Geometry
// ============================================================================

double ProgramChangeLaneComponent::displayBeat(double beat) const {
    return relativeMode_ ? beat : clipStartBeats_ + beat;
}

double ProgramChangeLaneComponent::clipBeat(double beat) const {
    return std::max(0.0, relativeMode_ ? beat : beat - clipStartBeats_);
}

double ProgramChangeLaneComponent::laneEndBeat() const {
    return clipLengthBeats_ > 0.0 ? displayBeat(clipLengthBeats_) : xToBeat(getWidth());
}

bool ProgramChangeLaneComponent::insideClip(int x) const {
    const auto beat = xToBeat(x);
    return beat >= displayBeat(0.0) && beat < laneEndBeat();
}

int ProgramChangeLaneComponent::beatToX(double beat) const {
    return velocity_lane::beatToPixel(beat, pixelsPerBeat_, leftPadding_, scrollOffsetX_);
}

double ProgramChangeLaneComponent::xToBeat(int x) const {
    return velocity_lane::pixelToBeat(x, pixelsPerBeat_, leftPadding_, scrollOffsetX_);
}

std::vector<ProgramChangeLaneComponent::Block> ProgramChangeLaneComponent::blocks() const {
    std::vector<Block> out;
    const auto* clip = ClipManager::getInstance().getClip(clipId_);
    if (clip == nullptr || !clip->isMidi())
        return out;

    const auto& changes = clip->midiProgramChangeData;
    for (size_t i = 0; i < changes.size(); ++i) {
        const auto start = dragging_ && dragPreviewBeat_ && i == dragIndex_
                               ? *dragPreviewBeat_
                               : displayBeat(changes[i].beatPosition);
        out.push_back({i, start, 0.0});
    }
    std::ranges::stable_sort(out, {}, &Block::startBeat);

    const auto end = laneEndBeat();
    for (size_t i = 0; i < out.size(); ++i)
        out[i].endBeat =
            i + 1 < out.size() ? out[i + 1].startBeat : std::max(end, out[i].startBeat);
    return out;
}

juce::Rectangle<int> ProgramChangeLaneComponent::handleArea(
    const Block& block, const MidiProgramChangeData& change) const {
    const int x0 = beatToX(block.startBeat);
    const int labelWidth =
        juce::GlyphArrangement::getStringWidthInt(labelFont(), programChangeLabel(change));
    const int width = std::min(labelWidth + 2 * kHandlePadding, beatToX(block.endBeat) - x0 - 1);
    return {x0, kHandleInset, std::max(kMinHandleWidth, width), getHeight() - 2 * kHandleInset};
}

std::optional<size_t> ProgramChangeLaneComponent::handleAt(juce::Point<int> position) const {
    const auto* clip = ClipManager::getInstance().getClip(clipId_);
    if (clip == nullptr)
        return std::nullopt;
    const auto all = blocks();
    // Later blocks first: a squeezed block's handle sits over the one before it.
    for (auto it = all.rbegin(); it != all.rend(); ++it)
        if (handleArea(*it, clip->midiProgramChangeData[it->index]).contains(position))
            return it->index;
    return std::nullopt;
}

std::optional<size_t> ProgramChangeLaneComponent::boundaryAt(int x) const {
    const auto all = blocks();
    for (size_t i = 1; i < all.size(); ++i)
        if (std::abs(beatToX(all[i].startBeat) - x) <= kEdgeGrabPixels)
            return all[i].index;
    return std::nullopt;
}

// ============================================================================
// Painting
// ============================================================================

void ProgramChangeLaneComponent::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds();
    g.setColour(ActiveTheme::getColour(ActiveTheme::BACKGROUND_ALT));
    g.fillRect(bounds);
    g.setColour(ActiveTheme::getColour(ActiveTheme::BORDER));
    g.drawHorizontalLine(0, 0.0f, static_cast<float>(bounds.getWidth()));

    const auto* clip = ClipManager::getInstance().getClip(clipId_);
    const auto all = blocks();
    g.setFont(labelFont());
    if (clip == nullptr || all.empty()) {
        g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
        g.drawText("Click to add a program change", bounds.reduced(8, 0),
                   juce::Justification::centredLeft, true);
    }

    if (clip != nullptr) {
        const auto colour = clip->colour;
        for (const auto& block : all) {
            const int x0 = beatToX(block.startBeat);
            const int x1 = beatToX(block.endBeat);
            if (x1 < 0 || x0 > bounds.getWidth())
                continue;
            const auto& change = clip->midiProgramChangeData[block.index];
            const bool isSelected = selected_ == block.index;

            const auto body = juce::Rectangle<int>(x0, kHandleInset, std::max(1, x1 - x0 - 1),
                                                   bounds.getHeight() - 2 * kHandleInset)
                                  .toFloat();
            g.setColour(colour.withAlpha(isSelected ? 0.55f : 0.4f));
            g.fillRoundedRectangle(body, 3.0f);
            g.setColour(isSelected ? ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY) : colour);
            g.drawRoundedRectangle(body.reduced(0.5f), 3.0f, 1.0f);

            const auto handle = handleArea(block, change).toFloat();
            g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
            g.drawText(programChangeLabel(change), handle.reduced(kHandlePadding, 0.0f),
                       juce::Justification::centredLeft, true);
        }
    }

    if (addHoverX_) {
        const auto x = static_cast<float>(*addHoverX_);
        g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY).withAlpha(0.8f));
        g.drawVerticalLine(*addHoverX_, 2.0f, static_cast<float>(bounds.getHeight() - 2));
        g.drawText("+", juce::Rectangle<float>(x + 3.0f, 2.0f, 12.0f, 12.0f),
                   juce::Justification::centredLeft, false);
    }
}

// ============================================================================
// Mouse and keys
// ============================================================================

void ProgramChangeLaneComponent::mouseMove(const juce::MouseEvent& e) {
    const bool onEdge = boundaryAt(e.x).has_value();
    const bool onHandle = !onEdge && handleAt(e.getPosition()).has_value();
    setMouseCursor(onEdge     ? juce::MouseCursor::LeftRightResizeCursor
                   : onHandle ? juce::MouseCursor::DraggingHandCursor
                              : juce::MouseCursor::NormalCursor);
    const auto hover =
        onEdge || onHandle || !insideClip(e.x) ? std::nullopt : std::optional<int>(e.x);
    if (hover != addHoverX_) {
        addHoverX_ = hover;
        repaint();
    }
}

void ProgramChangeLaneComponent::mouseExit(const juce::MouseEvent&) {
    if (std::exchange(addHoverX_, std::nullopt))
        repaint();
}

void ProgramChangeLaneComponent::mouseDown(const juce::MouseEvent& e) {
    grabKeyboardFocus();
    if (e.mods.isPopupMenu() || e.getNumberOfClicks() > 1)
        return;

    // A block's end is the next change's start, so dragging it moves that change.
    if (const auto edge = boundaryAt(e.x)) {
        selected_ = edge;
        dragging_ = true;
        dragIndex_ = *edge;
        dragOffsetBeats_ = 0.0;
        repaint();
        return;
    }

    const auto hit = handleAt(e.getPosition());
    if (!hit) {
        if (!insideClip(e.x))
            return;
        addHoverX_.reset();
        openEditor(std::nullopt, clipBeat(xToBeat(e.x)));
        return;
    }

    const auto all = blocks();
    const auto block = std::ranges::find(all, *hit, &Block::index);
    selected_ = hit;
    dragging_ = true;
    dragIndex_ = *hit;
    dragOffsetBeats_ = xToBeat(e.x) - block->startBeat;
    repaint();
}

void ProgramChangeLaneComponent::mouseDrag(const juce::MouseEvent& e) {
    if (!dragging_ || e.mouseWasClicked())
        return;

    // Between its neighbours, so dragging never reorders the changes.
    const auto saved = std::exchange(dragPreviewBeat_, std::nullopt);
    const auto all = blocks();
    dragPreviewBeat_ = saved;
    const auto at = std::ranges::find(all, dragIndex_, &Block::index);
    if (at == all.end())
        return;
    const double low = at == all.begin() ? displayBeat(0.0) : std::prev(at)->startBeat;
    const double high = std::next(at) == all.end() ? laneEndBeat() : std::next(at)->startBeat;

    dragPreviewBeat_ = std::clamp(xToBeat(e.x) - dragOffsetBeats_, low, std::max(low, high));
    repaint();
}

void ProgramChangeLaneComponent::mouseUp(const juce::MouseEvent&) {
    const auto preview = std::exchange(dragPreviewBeat_, std::nullopt);
    dragging_ = false;
    if (!preview)
        return;

    const auto* clip = ClipManager::getInstance().getClip(clipId_);
    if (clip == nullptr || dragIndex_ >= clip->midiProgramChangeData.size())
        return;
    auto changes = clip->midiProgramChangeData;
    const auto moved = clipBeat(*preview);
    if (std::abs(changes[dragIndex_].beatPosition - moved) < 1e-9) {
        repaint();
        return;
    }
    changes[dragIndex_].beatPosition = moved;
    commit(std::move(changes), "Move Program Change");
}

void ProgramChangeLaneComponent::mouseDoubleClick(const juce::MouseEvent& e) {
    if (const auto hit = handleAt(e.getPosition()))
        openEditor(hit, 0.0);
}

bool ProgramChangeLaneComponent::keyPressed(const juce::KeyPress& key) {
    if (key != juce::KeyPress::deleteKey && key != juce::KeyPress::backspaceKey)
        return false;
    const auto* clip = ClipManager::getInstance().getClip(clipId_);
    if (!selected_ || clip == nullptr || *selected_ >= clip->midiProgramChangeData.size())
        return false;

    auto changes = clip->midiProgramChangeData;
    changes.erase(changes.begin() + static_cast<std::ptrdiff_t>(*selected_));
    selected_.reset();
    commit(std::move(changes), "Delete Program Change");
    return true;
}

// ============================================================================
// Editing
// ============================================================================

void ProgramChangeLaneComponent::openEditor(std::optional<size_t> index, double clipBeatPosition) {
    const auto* clip = ClipManager::getInstance().getClip(clipId_);
    if (clip == nullptr || !clip->isMidi())
        return;

    MidiProgramChangeData initial;
    initial.beatPosition = clipBeatPosition;
    if (index) {
        if (*index >= clip->midiProgramChangeData.size())
            return;
        initial = clip->midiProgramChangeData[*index];
    }

    auto* alert = new MagdaAlertWindow(index ? "Edit Program Change" : "Add Program Change",
                                       "Leave a bank field empty to send no bank select.",
                                       juce::MessageBoxIconType::QuestionIcon);
    alert->addTextEditor("program", juce::String(initial.program), "Program (0-127):");
    alert->addTextEditor("bankMsb", byteText(initial.bankMsb), "Bank MSB (0-127):");
    alert->addTextEditor("bankLsb", byteText(initial.bankLsb), "Bank LSB (0-127):");
    alert->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    alert->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    juce::Component::SafePointer<ProgramChangeLaneComponent> safeThis(this);
    const auto clipId = clipId_;
    alert->enterModalState(
        true,
        juce::ModalCallbackFunction::create([safeThis, alert, index, initial, clipId](int result) {
            auto* self = safeThis.getComponent();
            const auto* current = ClipManager::getInstance().getClip(clipId);
            if (result != 1 || self == nullptr || current == nullptr || self->clipId_ != clipId)
                return;

            auto change = initial;
            change.program =
                parseMidiByte(alert->getTextEditorContents("program")).value_or(initial.program);
            change.bankMsb = parseMidiByte(alert->getTextEditorContents("bankMsb")).value_or(-1);
            change.bankLsb = parseMidiByte(alert->getTextEditorContents("bankLsb")).value_or(-1);

            auto changes = current->midiProgramChangeData;
            if (index) {
                if (*index >= changes.size())
                    return;
                changes[*index] = change;
                self->selected_ = index;
            } else {
                changes.push_back(change);
                self->selected_ = changes.size() - 1;
            }
            self->commit(std::move(changes), index ? "Edit Program Change" : "Add Program Change");
        }),
        true);
}

void ProgramChangeLaneComponent::commit(std::vector<MidiProgramChangeData> changes,
                                        const juce::String& description) {
    const auto* clip = ClipManager::getInstance().getClip(clipId_);
    if (clip == nullptr || !clip->isMidi())
        return;
    auto before = clip->midiEventState();
    auto after = before;
    after.programChanges = std::move(changes);
    UndoManager::getInstance().executeCommand(std::make_unique<SetMidiEventStateCommand>(
        clipId_, std::move(before), std::move(after), description));
    refreshEvents();
}

}  // namespace magda
