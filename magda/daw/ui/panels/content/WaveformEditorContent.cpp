#include "WaveformEditorContent.hpp"

#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "audio/AudioThumbnailManager.hpp"

namespace magda::daw::ui {

WaveformEditorContent::WaveformEditorContent() {
    setName("WaveformEditor");

    // Register as ClipManager listener
    magda::ClipManager::getInstance().addListener(this);

    // Check if there's already a selected audio clip
    magda::ClipId selectedClip = magda::ClipManager::getInstance().getSelectedClip();
    if (selectedClip != magda::INVALID_CLIP_ID) {
        const auto* clip = magda::ClipManager::getInstance().getClip(selectedClip);
        if (clip && clip->type == magda::ClipType::Audio) {
            editingClipId_ = selectedClip;
        }
    }
}

WaveformEditorContent::~WaveformEditorContent() {
    magda::ClipManager::getInstance().removeListener(this);
}

void WaveformEditorContent::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getPanelBackgroundColour());

    auto bounds = getLocalBounds();

    // Header area (time axis)
    auto headerArea = bounds.removeFromTop(HEADER_HEIGHT);
    paintHeader(g, headerArea);

    // Waveform area
    auto waveformArea = bounds.reduced(SIDE_MARGIN, 10);

    if (editingClipId_ != magda::INVALID_CLIP_ID) {
        const auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
        if (clip && clip->type == magda::ClipType::Audio) {
            paintWaveform(g, waveformArea, *clip);
        } else {
            paintNoClipMessage(g, waveformArea);
        }
    } else {
        paintNoClipMessage(g, waveformArea);
    }
}

void WaveformEditorContent::paintHeader(juce::Graphics& g, juce::Rectangle<int> area) {
    g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
    g.fillRect(area);

    // Draw time markers
    g.setColour(DarkTheme::getSecondaryTextColour());
    g.setFont(FontManager::getInstance().getUIFont(9.0f));

    const auto* clip = editingClipId_ != magda::INVALID_CLIP_ID
                           ? magda::ClipManager::getInstance().getClip(editingClipId_)
                           : nullptr;

    double lengthSeconds = clip ? clip->length : 10.0;

    // Draw markers every second
    for (int sec = 0; sec <= static_cast<int>(lengthSeconds) + 1; sec++) {
        int x = SIDE_MARGIN + static_cast<int>(sec * horizontalZoom_);
        if (x < area.getRight() - SIDE_MARGIN) {
            g.drawVerticalLine(x, area.getY(), area.getBottom());

            // Format time as MM:SS
            int minutes = sec / 60;
            int seconds = sec % 60;
            juce::String timeStr = juce::String::formatted("%d:%02d", minutes, seconds);
            g.drawText(timeStr, x + 2, area.getY(), 40, area.getHeight(),
                       juce::Justification::centredLeft, false);
        }
    }

    // Border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(area);
}

void WaveformEditorContent::paintWaveform(juce::Graphics& g, juce::Rectangle<int> area,
                                          const magda::ClipInfo& clip) {
    // Background
    g.setColour(DarkTheme::getColour(DarkTheme::TRACK_BACKGROUND));
    g.fillRoundedRectangle(area.toFloat(), 4.0f);

    if (clip.audioSources.empty()) {
        paintNoClipMessage(g, area);
        return;
    }

    const auto& source = clip.audioSources[0];

    // Calculate waveform rectangle based on source position and length
    int positionPixels = static_cast<int>(source.position * horizontalZoom_);
    int widthPixels = static_cast<int>(source.length * horizontalZoom_);
    widthPixels = juce::jmin(widthPixels, area.getWidth() - positionPixels);

    auto waveformRect = juce::Rectangle<int>(area.getX() + positionPixels, area.getY(), widthPixels,
                                             area.getHeight());

    // Draw waveform background (source block)
    g.setColour(clip.colour.darker(0.4f));
    g.fillRoundedRectangle(waveformRect.toFloat(), 3.0f);

    // Draw real waveform from audio thumbnail
    if (source.filePath.isNotEmpty()) {
        auto& thumbnailManager = magda::AudioThumbnailManager::getInstance();
        double displayStart = source.offset;
        double displayEnd = source.offset + source.length;

        thumbnailManager.drawWaveform(g, waveformRect.reduced(0, 4), source.filePath, displayStart,
                                      displayEnd, clip.colour.brighter(0.2f));
    }

    // Draw center line
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawHorizontalLine(waveformRect.getCentreY(), waveformRect.getX(), waveformRect.getRight());

    // Clip info overlay
    g.setColour(clip.colour);
    g.setFont(FontManager::getInstance().getUIFont(12.0f));
    g.drawText(clip.name, waveformRect.reduced(8, 4), juce::Justification::topLeft, true);

    // File path
    if (source.filePath.isNotEmpty()) {
        g.setColour(DarkTheme::getSecondaryTextColour());
        g.setFont(FontManager::getInstance().getUIFont(10.0f));
        g.drawText(source.filePath, waveformRect.reduced(8, 4).translated(0, 16),
                   juce::Justification::topLeft, true);
    }

    // Border around source block
    g.setColour(clip.colour.withAlpha(0.5f));
    g.drawRoundedRectangle(waveformRect.toFloat(), 3.0f, 1.0f);

    // Draw trim handles when hovering
    // Left edge handle
    g.setColour(clip.colour.brighter(0.4f));
    g.fillRect(waveformRect.getX(), waveformRect.getY(), 3, waveformRect.getHeight());
    // Right edge handle
    g.fillRect(waveformRect.getRight() - 3, waveformRect.getY(), 3, waveformRect.getHeight());
}

void WaveformEditorContent::paintNoClipMessage(juce::Graphics& g, juce::Rectangle<int> area) {
    g.setColour(DarkTheme::getSecondaryTextColour());
    g.setFont(FontManager::getInstance().getUIFont(14.0f));
    g.drawText("No audio clip selected", area, juce::Justification::centred, false);

    g.setFont(FontManager::getInstance().getUIFont(11.0f));
    g.drawText("Select an audio clip to view its waveform", area.translated(0, 24),
               juce::Justification::centred, false);
}

void WaveformEditorContent::resized() {
    // Nothing special to layout
}

void WaveformEditorContent::onActivated() {
    // Check for selected audio clip
    magda::ClipId selectedClip = magda::ClipManager::getInstance().getSelectedClip();
    if (selectedClip != magda::INVALID_CLIP_ID) {
        const auto* clip = magda::ClipManager::getInstance().getClip(selectedClip);
        if (clip && clip->type == magda::ClipType::Audio) {
            editingClipId_ = selectedClip;
        }
    }
    repaint();
}

void WaveformEditorContent::onDeactivated() {
    // Nothing to do
}

// ============================================================================
// Mouse Interaction
// ============================================================================

void WaveformEditorContent::mouseDown(const juce::MouseEvent& event) {
    if (editingClipId_ == magda::INVALID_CLIP_ID)
        return;

    auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
    if (!clip || clip->type != magda::ClipType::Audio || clip->audioSources.empty())
        return;

    const auto& source = clip->audioSources[0];
    int x = event.x;

    if (isNearLeftEdge(x, source)) {
        dragMode_ = DragMode::ResizeLeft;
    } else if (isNearRightEdge(x, source)) {
        dragMode_ = DragMode::ResizeRight;
    } else if (isInsideWaveform(x, source)) {
        dragMode_ = DragMode::Move;
    } else {
        dragMode_ = DragMode::None;
        return;
    }

    dragStartX_ = x;
    dragStartPosition_ = source.position;
    dragStartAudioOffset_ = source.offset;
    dragStartLength_ = source.length;
}

void WaveformEditorContent::mouseDrag(const juce::MouseEvent& event) {
    if (dragMode_ == DragMode::None)
        return;
    if (editingClipId_ == magda::INVALID_CLIP_ID)
        return;

    auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
    if (!clip || clip->audioSources.empty())
        return;

    auto& source = clip->audioSources[0];
    double deltaSeconds = (event.x - dragStartX_) / horizontalZoom_;

    switch (dragMode_) {
        case DragMode::ResizeLeft: {
            // Trim from left: advance both offset and position, decrease length
            double newOffset = std::max(0.0, dragStartAudioOffset_ + deltaSeconds);
            double offsetDelta = newOffset - dragStartAudioOffset_;
            source.offset = newOffset;
            source.position = std::max(0.0, dragStartPosition_ + offsetDelta);
            source.length = std::max(0.1, dragStartLength_ - offsetDelta);
            break;
        }
        case DragMode::ResizeRight: {
            // Trim from right: only change length
            source.length = std::max(0.1, dragStartLength_ + deltaSeconds);
            break;
        }
        case DragMode::Move: {
            // Move: only change position (slides block, same audio content)
            source.position = std::max(0.0, dragStartPosition_ + deltaSeconds);
            break;
        }
        default:
            break;
    }

    magda::ClipManager::getInstance().forceNotifyClipPropertyChanged(editingClipId_);
    repaint();
}

void WaveformEditorContent::mouseUp(const juce::MouseEvent& /*event*/) {
    dragMode_ = DragMode::None;
}

void WaveformEditorContent::mouseMove(const juce::MouseEvent& event) {
    if (editingClipId_ == magda::INVALID_CLIP_ID) {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }

    const auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
    if (!clip || clip->audioSources.empty()) {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }

    const auto& source = clip->audioSources[0];
    int x = event.x;

    if (isNearLeftEdge(x, source) || isNearRightEdge(x, source)) {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    } else if (isInsideWaveform(x, source)) {
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    } else {
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
}

// ============================================================================
// ClipManagerListener
// ============================================================================

void WaveformEditorContent::clipsChanged() {
    // Check if our clip was deleted
    if (editingClipId_ != magda::INVALID_CLIP_ID) {
        const auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
        if (!clip) {
            editingClipId_ = magda::INVALID_CLIP_ID;
        }
    }
    repaint();
}

void WaveformEditorContent::clipPropertyChanged(magda::ClipId clipId) {
    if (clipId == editingClipId_) {
        repaint();
    }
}

void WaveformEditorContent::clipSelectionChanged(magda::ClipId clipId) {
    // Auto-switch to the selected clip if it's an audio clip
    if (clipId != magda::INVALID_CLIP_ID) {
        const auto* clip = magda::ClipManager::getInstance().getClip(clipId);
        if (clip && clip->type == magda::ClipType::Audio) {
            editingClipId_ = clipId;
            repaint();
        }
    }
}

// ============================================================================
// Public Methods
// ============================================================================

void WaveformEditorContent::setClip(magda::ClipId clipId) {
    if (editingClipId_ != clipId) {
        editingClipId_ = clipId;
        repaint();
    }
}

// ============================================================================
// Hit Testing Helpers
// ============================================================================

juce::Rectangle<int> WaveformEditorContent::getWaveformArea() const {
    auto bounds = getLocalBounds();
    bounds.removeFromTop(HEADER_HEIGHT);
    return bounds.reduced(SIDE_MARGIN, 10);
}

bool WaveformEditorContent::isNearLeftEdge(int x, const magda::AudioSource& source) const {
    auto area = getWaveformArea();
    int leftEdgeX = area.getX() + static_cast<int>(source.position * horizontalZoom_);
    return std::abs(x - leftEdgeX) <= EDGE_GRAB_DISTANCE;
}

bool WaveformEditorContent::isNearRightEdge(int x, const magda::AudioSource& source) const {
    auto area = getWaveformArea();
    int rightEdgeX =
        area.getX() + static_cast<int>((source.position + source.length) * horizontalZoom_);
    return std::abs(x - rightEdgeX) <= EDGE_GRAB_DISTANCE;
}

bool WaveformEditorContent::isInsideWaveform(int x, const magda::AudioSource& source) const {
    auto area = getWaveformArea();
    int leftEdgeX = area.getX() + static_cast<int>(source.position * horizontalZoom_);
    int rightEdgeX =
        area.getX() + static_cast<int>((source.position + source.length) * horizontalZoom_);
    return x > leftEdgeX + EDGE_GRAB_DISTANCE && x < rightEdgeX - EDGE_GRAB_DISTANCE;
}

}  // namespace magda::daw::ui
