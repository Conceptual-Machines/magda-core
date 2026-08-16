#include "PostFxPanelContent.hpp"

#include <map>

#include "PluginBrowserContent.hpp"
#include "core/ChainNodePath.hpp"
#include "engine/AudioEngine.hpp"
#include "ui/components/chain/DeviceSlotComponent.hpp"
#include "ui/components/chain/NodeComponent.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/themes/SmallButtonLookAndFeel.hpp"

namespace magda::daw::ui {

//==============================================================================
// Container — the viewed component holding the device slots. Owns the plugin
// drop target and paints the drop indicator + empty-state hint.
//==============================================================================
class PostFxPanelContent::Container : public juce::Component, public juce::DragAndDropTarget {
  public:
    explicit Container(PostFxPanelContent& owner) : owner_(owner) {}

    void paint(juce::Graphics& g) override {
        if (owner_.trackId_ == magda::INVALID_TRACK_ID)
            return;

        // Trailing "+" add strip — a slot-shaped placeholder mirroring the FX chain.
        juce::Rectangle<int> appendZone(owner_.appendZoneX(), 0,
                                        PostFxPanelContent::APPEND_ZONE_WIDTH, getHeight());
        const bool appendHi = owner_.dragInsertIndex_ == static_cast<int>(owner_.slots_.size()) ||
                              owner_.dropInsertIndex_ == static_cast<int>(owner_.slots_.size());
        g.setColour(
            DarkTheme::getColour(DarkTheme::ACCENT_PRIMARY).withAlpha(appendHi ? 0.18f : 0.06f));
        g.fillRoundedRectangle(appendZone.reduced(6, 10).toFloat(), 4.0f);
        g.setColour(
            DarkTheme::getColour(DarkTheme::ACCENT_PRIMARY).withAlpha(appendHi ? 0.75f : 0.24f));
        g.drawRoundedRectangle(appendZone.reduced(6, 10).toFloat(), 4.0f, 1.0f);

        // "POST-FX" watermark as upright stacked letters, sitting above the "+"
        // button (which is a child and paints on top of this).
        {
            const juce::String text("POSTFX");
            constexpr int lineH = 12;
            const int stackH = text.length() * lineH;
            const int plusTop = appendZone.getCentreY() - 10;  // "+" is 20px, centred
            const int regionTop = appendZone.getY() + 10;
            int y = juce::jmax(regionTop, (regionTop + plusTop - stackH) / 2);
            g.setColour(DarkTheme::getSecondaryTextColour().withAlpha(0.5f));
            g.setFont(FontManager::getInstance().getUIFont(11.0f));
            for (int i = 0; i < text.length(); ++i) {
                g.drawText(juce::String::charToString(text[i]),
                           juce::Rectangle<int>(appendZone.getX(), y + i * lineH,
                                                appendZone.getWidth(), lineH),
                           juce::Justification::centred);
            }
        }

        // Empty-state hint, centred in the area left of the add strip.
        if (owner_.slots_.empty() && owner_.dragInsertIndex_ < 0 && owner_.dropInsertIndex_ < 0) {
            auto hintArea = getLocalBounds();
            hintArea.setRight(appendZone.getX());
            g.setColour(DarkTheme::getSecondaryTextColour());
            g.setFont(FontManager::getInstance().getUIFont(12.0f));
            g.drawText("Drop effects here to process after the FX chain", hintArea,
                       juce::Justification::centred);
        }

        const int insertIndex =
            owner_.dragInsertIndex_ >= 0 ? owner_.dragInsertIndex_ : owner_.dropInsertIndex_;
        if (insertIndex >= 0) {
            const int x = owner_.indicatorX(insertIndex);
            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_PRIMARY));
            g.fillRect(x - 1, 2, 2, getHeight() - 4);
        }
    }

    // DragAndDropTarget — accept plugin payloads from the browser. Post-fx
    // reorder is handled by the slots' own drag callbacks, not here; and
    // "chainElement" payloads carrying a post-fx Segment step are rejected by
    // the legacy readers anyway, so there is no cross-segment hazard.
    bool isInterestedInDragSource(const SourceDetails& details) override {
        if (owner_.trackId_ == magda::INVALID_TRACK_ID)
            return false;
        if (auto* obj = details.description.getDynamicObject())
            return obj->getProperty("type").toString() == "plugin";
        return false;
    }

    void itemDragEnter(const SourceDetails& details) override {
        updateDrop(details);
    }
    void itemDragMove(const SourceDetails& details) override {
        updateDrop(details);
    }
    void itemDragExit(const SourceDetails&) override {
        owner_.dropInsertIndex_ = -1;
        owner_.layoutSlots();
        repaint();
    }

    void itemDropped(const SourceDetails& details) override {
        const int insertIndex = owner_.dropInsertIndex_ >= 0
                                    ? owner_.dropInsertIndex_
                                    : static_cast<int>(owner_.slots_.size());
        if (auto* obj = details.description.getDynamicObject()) {
            if (obj->getProperty("type").toString() == "plugin" &&
                owner_.trackId_ != magda::INVALID_TRACK_ID) {
                auto device = PostFxPanelContent::deviceInfoFromDragObject(*obj);
                // Post-fx never holds instruments (addDeviceToPostFx also guards).
                if (!device.isInstrument) {
                    magda::TrackManager::getInstance().addDeviceToPostFx(owner_.trackId_, device,
                                                                         insertIndex);
                }
            }
        }
        owner_.dropInsertIndex_ = -1;
        owner_.layoutSlots();
        repaint();
    }

  private:
    void updateDrop(const SourceDetails& details) {
        owner_.dropInsertIndex_ = owner_.calculateInsertIndex(details.localPosition.x);
        owner_.layoutSlots();
        repaint();
    }

    PostFxPanelContent& owner_;
};

//==============================================================================
// FaderSideTag - the clickable state tag sitting below the "+" on the add
// strip, mirroring the "POSTFX" watermark above it. Reads and toggles
// TrackChain::postFxPostFader, which moves the whole post-FX section (with the
// mixer analysis rail) across the track fader. Sends are not affected: they sit
// on whichever side their own SendInfo::preFader flag says. Per-device
// placement is out of scope.
//==============================================================================
class PostFxPanelContent::FaderSideTag : public juce::Component, public juce::TooltipClient {
  public:
    explicit FaderSideTag(PostFxPanelContent& owner) : owner_(owner) {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void paint(juce::Graphics& g) override {
        // State, not decoration: full alpha, and accented while hovered.
        g.setColour(hovered_ ? DarkTheme::getColour(DarkTheme::ACCENT_PRIMARY)
                             : DarkTheme::getSecondaryTextColour());
        g.setFont(FontManager::getInstance().getUIFont(9.5f));
        const int lineH = getHeight() / 2;
        g.drawText(isPostFader() ? "POST" : "PRE", juce::Rectangle<int>(0, 0, getWidth(), lineH),
                   juce::Justification::centred);
        g.drawText("FADER", juce::Rectangle<int>(0, lineH, getWidth(), getHeight() - lineH),
                   juce::Justification::centred);
    }

    void mouseEnter(const juce::MouseEvent&) override {
        hovered_ = true;
        repaint();
    }

    void mouseExit(const juce::MouseEvent&) override {
        hovered_ = false;
        repaint();
    }

    void mouseDown(const juce::MouseEvent& e) override {
        // A right-click on a small control means "show me options", not "do the
        // thing"; there is no menu here yet, so it should just do nothing.
        if (owner_.trackId_ == magda::INVALID_TRACK_ID || e.mods.isPopupMenu())
            return;
        // The setter notifies trackDevicesChanged, which relays out and repaints.
        auto& trackManager = magda::TrackManager::getInstance();
        trackManager.setPostFxPostFader(owner_.trackId_,
                                        !trackManager.isPostFxPostFader(owner_.trackId_));
    }

    juce::String getTooltip() override {
        return isPostFader() ? "Post-FX runs after the track fader. Click to move it before the "
                               "fader."
                             : "Post-FX runs before the track fader. Click to move it after the "
                               "fader.";
    }

  private:
    // Always read through the model - the tag caches no state of its own.
    bool isPostFader() const {
        return owner_.trackId_ != magda::INVALID_TRACK_ID &&
               magda::TrackManager::getInstance().isPostFxPostFader(owner_.trackId_);
    }

    PostFxPanelContent& owner_;
    bool hovered_ = false;
};

//==============================================================================
// PostFxPanelContent
//==============================================================================
PostFxPanelContent::PostFxPanelContent() {
    viewport_ = std::make_unique<juce::Viewport>();
    container_ = std::make_unique<Container>(*this);
    viewport_->setViewedComponent(container_.get(), false);
    // No visible scrollbars; allow horizontal scrolling via trackpad/wheel. A
    // shown horizontal scrollbar would reserve vertical space and raise the
    // device's bottom whenever the content overflows the panel width.
    viewport_->setScrollBarsShown(false, false, false, true);
    addAndMakeVisible(*viewport_);

    addButton_.setButtonText("+");
    addButton_.setColour(juce::TextButton::buttonColourId,
                         DarkTheme::getColour(DarkTheme::ACCENT_PRIMARY).withAlpha(0.24f));
    addButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getTextColour());
    addButton_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    addButton_.onClick = [this]() { showAddDeviceMenu(); };
    container_->addAndMakeVisible(addButton_);

    faderSideTag_ = std::make_unique<FaderSideTag>(*this);
    container_->addAndMakeVisible(*faderSideTag_);

    magda::TrackManager::getInstance().addListener(this);
}

PostFxPanelContent::~PostFxPanelContent() {
    magda::TrackManager::getInstance().removeListener(this);
    addButton_.setLookAndFeel(nullptr);
}

void PostFxPanelContent::setTrack(magda::TrackId trackId) {
    if (trackId_ == trackId)
        return;
    trackId_ = trackId;
    rebuildSlots();
    layoutSlots();
    repaint();
}

void PostFxPanelContent::tracksChanged() {
    if (trackId_ != magda::INVALID_TRACK_ID &&
        magda::TrackManager::getInstance().getTrack(trackId_) == nullptr) {
        trackId_ = magda::INVALID_TRACK_ID;
    }
    rebuildSlots();
    layoutSlots();
    repaint();
}

void PostFxPanelContent::trackDevicesChanged(magda::TrackId trackId) {
    if (trackId != trackId_)
        return;
    rebuildSlots();
    layoutSlots();
    repaint();  // covers the tag too: it is a child of the viewed container
}

void PostFxPanelContent::rebuildSlots() {
    slots_.clear();

    if (trackId_ == magda::INVALID_TRACK_ID) {
        addButton_.setEnabled(false);
        return;
    }
    addButton_.setEnabled(true);

    const auto& elements = magda::TrackManager::getInstance().getPostFxChainElements(trackId_);
    for (const auto& element : elements) {
        const auto& device = element.device;
        auto slot = std::make_unique<DeviceSlotComponent>(device);
        slot->setNodePath(magda::ChainNodePath::postFxDevice(trackId_, device.id));

        slot->onDeviceLayoutChanged = [this]() {
            layoutSlots();
            repaint();
        };

        slot->onDragStart = [this](NodeComponent* node, const juce::MouseEvent&) {
            draggedNode_ = node;
            dragOriginalIndex_ = findSlotIndex(node);
            dragInsertIndex_ = dragOriginalIndex_;
            dragGhostImage_ = node->createComponentSnapshot(node->getLocalBounds());
            node->setAlpha(0.4f);
            layoutSlots();
            container_->repaint();
        };

        slot->onDragMove = [this](NodeComponent*, const juce::MouseEvent& e) {
            auto pos = e.getEventRelativeTo(container_.get()).getPosition();
            dragInsertIndex_ = calculateInsertIndex(pos.x);
            dragMousePos_ = pos;
            container_->repaint();
        };

        slot->onDragEnd = [this](NodeComponent* node, const juce::MouseEvent&) {
            node->setAlpha(1.0f);
            dragGhostImage_ = juce::Image();

            const int from = dragOriginalIndex_;
            const int insert = dragInsertIndex_;
            const auto track = trackId_;

            draggedNode_ = nullptr;
            dragOriginalIndex_ = -1;
            dragInsertIndex_ = -1;
            layoutSlots();
            container_->repaint();

            // Defer the model mutation: committing now would rebuild slots_ and
            // destroy `node` (and this lambda) while still inside its mouseUp.
            juce::Component::SafePointer<PostFxPanelContent> safe(this);
            juce::MessageManager::callAsync([safe, track, from, insert]() {
                if (safe != nullptr)
                    safe->applyReorder(track, from, insert);
            });
        };

        container_->addAndMakeVisible(*slot);
        slots_.push_back(std::move(slot));
    }
}

void PostFxPanelContent::applyReorder(magda::TrackId trackId, int fromIndex, int insertIndex) {
    if (trackId == magda::INVALID_TRACK_ID || fromIndex < 0 || insertIndex < 0)
        return;
    // insertIndex is an insertion point in the current list (0..N). Dropping in
    // place (at fromIndex or fromIndex+1) is a no-op.
    if (insertIndex == fromIndex || insertIndex == fromIndex + 1)
        return;
    const int target = insertIndex > fromIndex ? insertIndex - 1 : insertIndex;
    magda::TrackManager::getInstance().movePostFxDevice(trackId, fromIndex, target);
}

int PostFxPanelContent::findSlotIndex(const NodeComponent* node) const {
    for (size_t i = 0; i < slots_.size(); ++i) {
        if (static_cast<const NodeComponent*>(slots_[i].get()) == node)
            return static_cast<int>(i);
    }
    return -1;
}

int PostFxPanelContent::calculateInsertIndex(int xInContainer) const {
    for (size_t i = 0; i < slots_.size(); ++i) {
        const int midX = slots_[i]->getX() + slots_[i]->getWidth() / 2;
        if (xInContainer < midX)
            return static_cast<int>(i);
    }
    return static_cast<int>(slots_.size());
}

int PostFxPanelContent::indicatorX(int insertIndex) const {
    const bool dragActive = dragInsertIndex_ >= 0 || dropInsertIndex_ >= 0;
    const int leftPad = LEFT_PADDING + (dragActive ? DRAG_PADDING : 0);
    if (slots_.empty())
        return leftPad;
    if (insertIndex <= 0)
        return juce::jmax(2, slots_.front()->getX() - SLOT_SPACING / 2);
    if (insertIndex >= static_cast<int>(slots_.size()))
        return slots_.back()->getRight() + SLOT_SPACING / 2;
    return slots_[static_cast<size_t>(insertIndex) - 1]->getRight() + SLOT_SPACING / 2;
}

void PostFxPanelContent::resized() {
    viewport_->setBounds(getLocalBounds());  // no header — "POST-FX" is a watermark
    layoutSlots();
}

int PostFxPanelContent::contentWidth() const {
    const bool dragActive = dragInsertIndex_ >= 0 || dropInsertIndex_ >= 0;
    int x = LEFT_PADDING + (dragActive ? DRAG_PADDING : 0);
    for (const auto& slot : slots_)
        x += slot->getPreferredWidth() + SLOT_SPACING;
    const int needed = x + APPEND_ZONE_WIDTH + LEFT_PADDING;
    return juce::jmax(needed, viewport_ ? viewport_->getWidth() : needed);
}

int PostFxPanelContent::appendZoneX() const {
    // Pinned to the right edge of the container, so the "+" sits all the way
    // right (and hugs the panel's right margin when devices fit).
    return contentWidth() - APPEND_ZONE_WIDTH - LEFT_PADDING;
}

void PostFxPanelContent::layoutSlots() {
    if (!container_ || !viewport_ || !faderSideTag_)
        return;

    const bool dragActive = dragInsertIndex_ >= 0 || dropInsertIndex_ >= 0;
    const int leftPad = LEFT_PADDING + (dragActive ? DRAG_PADDING : 0);
    const int totalW = contentWidth();
    // Full viewport height — NOT getMaximumVisibleHeight(), which subtracts the
    // horizontal scrollbar's reserved strip and would shrink the slot (raising
    // the device's bottom) once the content overflows the panel width.
    const int contentH = viewport_->getHeight();
    container_->setSize(totalW, contentH);

    int x = leftPad;
    for (auto& slot : slots_) {
        const int w = slot->getPreferredWidth();
        slot->setBounds(x, 0, w, contentH);
        x += w + SLOT_SPACING;
    }

    constexpr int buttonSize = 20;
    const int appendX = appendZoneX();
    addButton_.setVisible(trackId_ != magda::INVALID_TRACK_ID);
    addButton_.setBounds(appendX + juce::jmax(0, (APPEND_ZONE_WIDTH - buttonSize) / 2),
                         juce::jmax(0, (contentH - buttonSize) / 2), buttonSize, buttonSize);
    addButton_.toFront(false);

    // Pre/post-fader tag, in the mirror position below the "+". Hidden on the
    // master track: the native compiler pins the master post-FX stage pre-fader
    // whatever the flag says, so a toggle there would lie. Dropped entirely when
    // the strip is too short to hold it clear of the "+".
    constexpr int tagHeight = 2 * TAG_LINE_HEIGHT;
    const int stripBottom = contentH - 10;  // matches the strip's .reduced(6, 10)
    const int roomBelowButton = stripBottom - addButton_.getBottom();
    const bool showTag = trackId_ != magda::INVALID_TRACK_ID &&
                         trackId_ != magda::MASTER_TRACK_ID && roomBelowButton >= tagHeight + 4;
    faderSideTag_->setVisible(showTag);
    if (showTag) {
        faderSideTag_->setBounds(appendX,
                                 addButton_.getBottom() + (roomBelowButton - tagHeight) / 2,
                                 APPEND_ZONE_WIDTH, tagHeight);
        faderSideTag_->toFront(false);
    }
}

void PostFxPanelContent::paint(juce::Graphics& g) {
    // No header bar; the "POST-FX" watermark runs vertically down the add strip
    // (drawn by the container).
    g.fillAll(DarkTheme::getColour(DarkTheme::BACKGROUND));
}

void PostFxPanelContent::showAddDeviceMenu() {
    if (trackId_ == magda::INVALID_TRACK_ID)
        return;

    juce::PopupMenu menu;

    // Internal devices (skip instruments — nothing generates sound post-fader).
    auto internals = magda::daw::ui::PluginBrowserContent::getInternalPlugins();
    juce::PopupMenu internalMenu;
    std::vector<magda::daw::ui::PluginBrowserInfo> internalEffects;
    for (const auto& entry : internals) {
        if (entry.category == "Instrument")
            continue;
        internalEffects.push_back(entry);
        internalMenu.addItem(static_cast<int>(internalEffects.size()), entry.name);
    }
    menu.addSubMenu("Internal", internalMenu);

    juce::Array<juce::PluginDescription> externalPlugins;
    if (auto* engine = magda::TrackManager::getInstance().getAudioEngine()) {
        externalPlugins = engine->getPreferredPluginTypes();
    }
    if (!externalPlugins.isEmpty()) {
        std::map<juce::String, juce::PopupMenu> byManufacturer;
        for (int i = 0; i < externalPlugins.size(); ++i) {
            const auto& desc = externalPlugins[i];
            if (desc.isInstrument)
                continue;
            auto manufacturer = desc.manufacturerName.isEmpty() ? "Unknown" : desc.manufacturerName;
            byManufacturer[manufacturer].addItem(1000 + i, desc.name);
        }
        for (auto& [manufacturer, subMenu] : byManufacturer)
            menu.addSubMenu(manufacturer, subMenu);
    }

    auto safeThis = juce::Component::SafePointer<PostFxPanelContent>(this);
    auto trackId = trackId_;
    auto capturedExternal =
        std::make_shared<juce::Array<juce::PluginDescription>>(std::move(externalPlugins));
    auto capturedInternal = std::make_shared<std::vector<magda::daw::ui::PluginBrowserInfo>>(
        std::move(internalEffects));

    menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis, trackId, capturedExternal,
                                                    capturedInternal](int result) {
        if (result == 0 || safeThis == nullptr)
            return;

        magda::DeviceInfo device;
        if (result >= 1 && result <= static_cast<int>(capturedInternal->size())) {
            const auto& entry = (*capturedInternal)[static_cast<size_t>(result - 1)];
            device.name = entry.name;
            device.manufacturer = "MAGDA";
            device.pluginId = entry.uniqueId;
            if (entry.subcategory == "MIDI")
                device.deviceType = magda::DeviceType::MIDI;
            device.format = magda::PluginFormat::Internal;
        } else if (result >= 1000) {
            const int idx = result - 1000;
            if (idx < 0 || idx >= capturedExternal->size())
                return;
            const auto& desc = (*capturedExternal)[idx];
            device.name = desc.name;
            device.manufacturer = desc.manufacturerName;
            device.pluginId = desc.createIdentifierString();
            device.uniqueId = desc.createIdentifierString();
            device.fileOrIdentifier = desc.fileOrIdentifier;
            if (desc.pluginFormatName == "VST3")
                device.format = magda::PluginFormat::VST3;
            else if (desc.pluginFormatName == "AU" || desc.pluginFormatName == "AudioUnit")
                device.format = magda::PluginFormat::AU;
            else if (desc.pluginFormatName == "LV2")
                device.format = magda::PluginFormat::LV2;
            else
                device.format = magda::PluginFormat::Internal;
        } else {
            return;
        }

        magda::TrackManager::getInstance().addDeviceToPostFx(trackId, device);
    });
}

magda::DeviceInfo PostFxPanelContent::deviceInfoFromDragObject(const juce::DynamicObject& obj) {
    magda::DeviceInfo device;
    device.name = obj.getProperty("name").toString().toStdString();
    device.manufacturer = obj.getProperty("manufacturer").toString().toStdString();
    auto uniqueId = obj.getProperty("uniqueId").toString();
    device.pluginId = uniqueId.isNotEmpty() ? uniqueId
                                            : obj.getProperty("name").toString() + "_" +
                                                  obj.getProperty("format").toString();
    const auto rawCategory = obj.hasProperty("rawCategory")
                                 ? obj.getProperty("rawCategory").toString()
                                 : obj.getProperty("category").toString();
    const auto rawSubcategory = obj.hasProperty("rawSubcategory")
                                    ? obj.getProperty("rawSubcategory").toString()
                                    : obj.getProperty("subcategory").toString();
    device.isInstrument = rawCategory.isNotEmpty()
                              ? rawCategory == "Instrument"
                              : static_cast<bool>(obj.getProperty("isInstrument"));
    if (rawSubcategory == "MIDI")
        device.deviceType = magda::DeviceType::MIDI;
    else if (device.isInstrument)
        device.deviceType = magda::DeviceType::Instrument;
    device.browserCategoryOverride = obj.getProperty("categoryOverride").toString();
    device.uniqueId = obj.getProperty("uniqueId").toString();
    device.fileOrIdentifier = obj.getProperty("fileOrIdentifier").toString();

    const juce::String format = obj.getProperty("format").toString();
    if (format == "VST3")
        device.format = magda::PluginFormat::VST3;
    else if (format == "AU")
        device.format = magda::PluginFormat::AU;
    else if (format == "LV2")
        device.format = magda::PluginFormat::LV2;
    else if (format == "Internal")
        device.format = magda::PluginFormat::Internal;

    return device;
}

}  // namespace magda::daw::ui
