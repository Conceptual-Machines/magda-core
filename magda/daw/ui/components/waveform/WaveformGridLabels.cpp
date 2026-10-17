#include <algorithm>
#include <cmath>
#include <utility>

#include "../../themes/ActiveTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "../common/EditTool.hpp"
#include "../common/MagdaAlertWindow.hpp"
#include "WaveformGridComponent.hpp"
#include "project/ProjectManager.hpp"

namespace magda::daw::ui {

namespace {

constexpr float kEdgeGrab = 4.0f;
constexpr float kPointHitRadius = 5.0f;
constexpr int kDrawThreshold = 3;
constexpr int kColourMenuBase = 100;

}  // namespace

/// A point label's handle: the warp flag's shape, in the label's colour.
void WaveformGridComponent::paintLabelFlag(juce::Graphics& g, float x, float top,
                                           juce::Colour colour) {
    juce::Path flag;
    flag.startNewSubPath(x - 5.0f, top);
    flag.lineTo(x + 5.0f, top);
    flag.lineTo(x + 5.0f, top + 7.0f);
    flag.lineTo(x, top + 11.0f);
    flag.lineTo(x - 5.0f, top + 7.0f);
    flag.closeSubPath();
    g.setColour(colour);
    g.fillPath(flag);
}

bool WaveformGridComponent::placesLabels() const {
    return !gainEnvelopeEditor_ &&
           magda::EditToolState::sampleEditor().getTool() == magda::EditTool::Label;
}

const std::vector<magda::LabelRegion>& WaveformGridComponent::shownLabels(
    const magda::ClipInfo& clip) const {
    return labelPreview_ ? *labelPreview_ : magda::audioEventRef(clip).labelRegions;
}

std::pair<float, float> WaveformGridComponent::labelSpan(const magda::LabelRegion& label) const {
    const auto x = [this](double source) {
        return static_cast<float>(timeToPixelF(sourceToDisplayTime(source)));
    };
    return {x(label.sourceStart), x(label.sourceEnd)};
}

double WaveformGridComponent::labelSourceAt(int x, bool snap) const {
    auto display = pixelToTime(x);
    if (snap && snapEnabled_)
        display = snapTimeToGrid(display);
    const auto* clip = getClip();
    const auto duration = clip ? magda::audioEventRef(*clip).sourceDurationSeconds() : 0.0;
    const auto source = displayTimeToSource(display);
    return duration > 0.0 ? std::clamp(source, 0.0, duration) : std::max(0.0, source);
}

juce::Rectangle<int> WaveformGridComponent::labelTabBand() const {
    const auto bounds = getLocalBounds().reduced(LEFT_PADDING, TOP_PADDING);
    return bounds.withHeight(LABEL_TAB_HEIGHT);
}

WaveformGridComponent::LabelHit WaveformGridComponent::findLabelAt(juce::Point<int> point) const {
    const auto* clip = getClip();
    if (clip == nullptr || gainEnvelopeEditor_ || !labelTabBand().contains(point))
        return {};

    const auto px = static_cast<float>(point.x);
    const auto& labels = shownLabels(*clip);
    for (int i = static_cast<int>(labels.size()) - 1; i >= 0; --i) {
        const auto& label = labels[static_cast<size_t>(i)];
        const auto [xs, xe] = labelSpan(label);
        if (label.isPoint()) {
            if (std::abs(px - xs) <= kPointHitRadius)
                return {i, LabelPart::Body};
            continue;
        }
        if (std::abs(px - xs) <= kEdgeGrab)
            return {i, LabelPart::Start};
        if (std::abs(px - xe) <= kEdgeGrab)
            return {i, LabelPart::End};
        if (px > std::min(xs, xe) && px < std::max(xs, xe))
            return {i, LabelPart::Body};
    }
    return {};
}

void WaveformGridComponent::paintLabels(juce::Graphics& g, const magda::ClipInfo& clip) {
    const auto& labels = shownLabels(clip);
    if (labels.empty() || gainEnvelopeEditor_)
        return;

    const auto bounds = getLocalBounds().reduced(LEFT_PADDING, TOP_PADDING).toFloat();
    const auto band = labelTabBand().toFloat();
    g.setFont(FontManager::getInstance().getUIFont(10.0f));

    for (size_t i = 0; i < labels.size(); ++i) {
        const auto& label = labels[i];
        const auto [xs, xe] = labelSpan(label);
        const auto left = std::min(xs, xe);
        const auto right = std::max(xs, xe);
        if (right < -120.0f || left > static_cast<float>(getWidth()))
            continue;

        const bool emphasised =
            static_cast<int>(i) == hoveredLabelIndex_ || static_cast<int>(i) == labelDragIndex_;
        auto textArea = juce::Rectangle<float>();
        if (label.isPoint()) {
            g.setColour(label.colour.withAlpha(emphasised ? 0.85f : 0.45f));
            g.fillRect(juce::Rectangle<float>(xs - 0.5f, bounds.getY(), 1.0f, bounds.getHeight()));
            paintLabelFlag(g, xs, band.getY(),
                           emphasised ? label.colour.brighter(0.2f) : label.colour);
            textArea = {xs + 7.0f, band.getY(), 120.0f, band.getHeight()};
        } else {
            g.setColour(label.colour.withAlpha(emphasised ? 0.16f : 0.1f));
            g.fillRect(
                juce::Rectangle<float>(left, bounds.getY(), right - left, bounds.getHeight()));
            const auto tab =
                juce::Rectangle<float>(left, band.getY(), right - left, band.getHeight());
            g.setColour(label.colour.withAlpha(emphasised ? 0.8f : 0.55f));
            g.fillRect(tab);
            textArea = tab.reduced(4.0f, 0.0f);
        }

        if (textArea.getWidth() > 12.0f) {
            g.setColour(ActiveTheme::getColour(emphasised ? ActiveTheme::TEXT_PRIMARY
                                                          : ActiveTheme::TEXT_SECONDARY));
            g.drawFittedText(juce::String(label.name), textArea.toNearestInt(),
                             juce::Justification::centredLeft, 1, 1.0f);
        }
    }
}

bool WaveformGridComponent::labelMouseDown(const juce::MouseEvent& event,
                                           const magda::ClipInfo& clip) {
    const auto hit = findLabelAt(event.getPosition());
    if (hit.index >= 0) {
        if (event.mods.isPopupMenu()) {
            showLabelMenu(hit.index, localPointToGlobal(event.getPosition()));
            return true;
        }
        labelDragIndex_ = hit.index;
        labelDragOriginal_ =
            magda::audioEventRef(clip).labelRegions[static_cast<size_t>(hit.index)];
        labelDragStartSource_ = labelSourceAt(event.x, false);
        dragMode_ = hit.part == LabelPart::Start ? DragMode::ResizeLabelStart
                    : hit.part == LabelPart::End ? DragMode::ResizeLabelEnd
                                                 : DragMode::MoveLabel;
        return true;
    }

    if (event.mods.isPopupMenu() || !placesLabels() || !isInsideWaveform(event.x, clip))
        return false;
    // Placed exactly where clicked, as a warp marker is; a drag snaps.
    labelDragStartSource_ = labelSourceAt(event.x, false);
    labelDragIndex_ = -1;
    dragMode_ = DragMode::DrawLabel;
    return true;
}

bool WaveformGridComponent::labelMouseDrag(const juce::MouseEvent& event) {
    const bool labelGesture =
        dragMode_ == DragMode::DrawLabel || dragMode_ == DragMode::MoveLabel ||
        dragMode_ == DragMode::ResizeLabelStart || dragMode_ == DragMode::ResizeLabelEnd;
    if (!labelGesture)
        return false;
    const auto* clip = getClip();
    if (clip == nullptr || (!labelPreview_ && event.getDistanceFromDragStart() < kDrawThreshold))
        return true;

    auto edited = magda::audioEventRef(*clip).labelRegions;
    const auto at = labelSourceAt(event.x, true);
    if (dragMode_ == DragMode::DrawLabel) {
        if (labelDragIndex_ < 0) {
            edited.push_back(makeLabel(*clip, labelDragStartSource_, at));
            labelDragIndex_ = static_cast<int>(edited.size()) - 1;
        } else {
            edited.push_back(labelPreview_->back());
        }
        auto& drawn = edited.back();
        drawn.sourceStart = std::min(labelDragStartSource_, at);
        drawn.sourceEnd = std::max(labelDragStartSource_, at);
    } else {
        auto& label = edited[static_cast<size_t>(labelDragIndex_)];
        if (dragMode_ == DragMode::MoveLabel) {
            const auto length = labelDragOriginal_.sourceEnd - labelDragOriginal_.sourceStart;
            auto start = labelDragOriginal_.sourceStart +
                         (labelSourceAt(event.x, false) - labelDragStartSource_);
            if (snapEnabled_)
                start = displayTimeToSource(snapTimeToGrid(sourceToDisplayTime(start)));
            const auto duration = magda::audioEventRef(*clip).sourceDurationSeconds();
            start = std::clamp(start, 0.0, std::max(0.0, duration - length));
            label.sourceStart = start;
            label.sourceEnd = start + length;
        } else if (dragMode_ == DragMode::ResizeLabelStart) {
            label.sourceStart = at;
        } else {
            label.sourceEnd = at;
        }
    }
    labelPreview_ = std::move(edited);
    repaint();
    return true;
}

bool WaveformGridComponent::labelMouseUp() {
    const auto mode = dragMode_;
    if (mode != DragMode::DrawLabel && mode != DragMode::MoveLabel &&
        mode != DragMode::ResizeLabelStart && mode != DragMode::ResizeLabelEnd)
        return false;

    dragMode_ = DragMode::None;
    labelDragIndex_ = -1;
    auto edited = std::exchange(labelPreview_, std::nullopt);
    const auto* clip = getClip();
    if (!edited && mode == DragMode::DrawLabel && clip != nullptr) {
        // The second click of a double-click lands on the point the first one placed.
        const auto& labels = magda::audioEventRef(*clip).labelRegions;
        const auto placedHere = [at = labelDragStartSource_](const magda::LabelRegion& l) {
            return l.isPoint() && std::abs(l.sourceStart - at) < 1e-6;
        };
        if (std::ranges::none_of(labels, placedHere)) {
            edited = labels;
            edited->push_back(makeLabel(*clip, labelDragStartSource_, labelDragStartSource_));
        }
    }
    if (edited)
        commitLabels(std::move(*edited), mode == DragMode::DrawLabel ? "Add Label" : "Edit Label");
    repaint();
    return true;
}

bool WaveformGridComponent::labelMouseMove(const juce::MouseEvent& event) {
    const auto hit = findLabelAt(event.getPosition());
    if (hit.index != hoveredLabelIndex_) {
        hoveredLabelIndex_ = hit.index;
        repaint();
    }
    if (hit.index < 0) {
        const auto* clip = getClip();
        if (!placesLabels() || clip == nullptr || event.mods.isShiftDown() ||
            !isInsideWaveform(event.x, *clip))
            return false;
        setMarkerGuide(event.x);
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return true;
    }
    setMarkerGuide(-1);
    setMouseCursor(hit.part == LabelPart::Body ? juce::MouseCursor::DraggingHandCursor
                                               : juce::MouseCursor::LeftRightResizeCursor);
    return true;
}

void WaveformGridComponent::commitLabels(std::vector<magda::LabelRegion> labels,
                                         const juce::String& description) {
    if (onLabelsEdited)
        onLabelsEdited(std::move(labels), description);
}

magda::LabelRegion WaveformGridComponent::makeLabel(const magda::ClipInfo& clip, double start,
                                                    double end) const {
    const int count = static_cast<int>(magda::audioEventRef(clip).labelRegions.size());
    const auto& defaults = magda::ProjectManager::getInstance().getCurrentProjectInfo().defaults;
    return {"Label " + std::to_string(count + 1), juce::Colour(defaults.colourForIndex(count)),
            std::min(start, end), std::max(start, end)};
}

void WaveformGridComponent::showLabelMenu(int index, juce::Point<int> screenPosition) {
    auto makeChip = [](juce::Colour colour) {
        juce::Image chip(juce::Image::ARGB, 14, 14, true);
        juce::Graphics cg(chip);
        cg.setColour(colour);
        cg.fillRoundedRectangle(0.0f, 0.0f, 14.0f, 14.0f, 2.0f);
        auto drawable = std::make_unique<juce::DrawableImage>();
        drawable->setImage(chip);
        return drawable;
    };

    const auto palette =
        magda::ProjectManager::getInstance().getCurrentProjectInfo().defaults.colourPalette;
    juce::PopupMenu colourMenu;
    for (size_t i = 0; i < palette.size(); ++i)
        colourMenu.addItem(kColourMenuBase + static_cast<int>(i), palette[i].name, true, false,
                           makeChip(juce::Colour(palette[i].colour)));

    juce::PopupMenu menu;
    menu.addItem(1, "Rename Label...");
    menu.addSubMenu("Colour", colourMenu);
    menu.addSeparator();
    menu.addItem(2, "Delete Label");

    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetScreenArea({screenPosition.x, screenPosition.y, 1, 1}),
        [safeThis = juce::Component::SafePointer<WaveformGridComponent>(this), index,
         palette](int result) {
            const auto* clip = safeThis != nullptr ? safeThis->getClip() : nullptr;
            if (clip == nullptr || result == 0)
                return;
            auto edited = magda::audioEventRef(*clip).labelRegions;
            if (index >= static_cast<int>(edited.size()))
                return;
            if (result == 1) {
                safeThis->showRenameLabelDialog(index);
            } else if (result == 2) {
                edited.erase(edited.begin() + index);
                safeThis->commitLabels(std::move(edited), "Delete Label");
            } else if (const auto i = static_cast<size_t>(result - kColourMenuBase);
                       result >= kColourMenuBase && i < palette.size()) {
                edited[static_cast<size_t>(index)].colour = juce::Colour(palette[i].colour);
                safeThis->commitLabels(std::move(edited), "Recolour Label");
            }
        });
}

void WaveformGridComponent::showRenameLabelDialog(int index) {
    const auto* clip = getClip();
    if (clip == nullptr ||
        index >= static_cast<int>(magda::audioEventRef(*clip).labelRegions.size()))
        return;

    auto* alert = new magda::MagdaAlertWindow("Rename Label", "", juce::MessageBoxIconType::NoIcon);
    alert->addTextEditor(
        "name",
        juce::String(magda::audioEventRef(*clip).labelRegions[static_cast<size_t>(index)].name),
        "Name:");
    alert->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey));
    alert->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    juce::Component::SafePointer<WaveformGridComponent> safeThis(this);
    alert->enterModalState(
        true, juce::ModalCallbackFunction::create([alert, safeThis, index](int result) {
            const auto name = alert->getTextEditorContents("name").trim();
            delete alert;
            const auto* current = safeThis != nullptr ? safeThis->getClip() : nullptr;
            if (result != 1 || name.isEmpty() || current == nullptr)
                return;
            auto edited = magda::audioEventRef(*current).labelRegions;
            if (index >= static_cast<int>(edited.size()))
                return;
            edited[static_cast<size_t>(index)].name = name.toStdString();
            safeThis->commitLabels(std::move(edited), "Rename Label");
        }));
}

}  // namespace magda::daw::ui
