// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ModulationSection.hpp"

#include <map>
#include <utility>
#include <vector>

#include "core/AutomationInfo.hpp"
#include "core/StringTable.hpp"
#include "core/TrackCommands.hpp"
#include "core/TrackManager.hpp"
#include "core/UndoManager.hpp"
#include "drum_grid/DeviceSlotDrumGridBridge.hpp"
#include "modulation/MacroEditorPanel.hpp"
#include "modulation/MacroPanelComponent.hpp"
#include "modulation/ModsPanelComponent.hpp"
#include "modulation/ModulatorEditorPanel.hpp"
#include "modulation/ModulatorTriggerSourceMenu.hpp"
#include "slot/DeviceSlotHeaderSpec.hpp"
#include "slot/DeviceSlotTraits.hpp"
#include "ui/themes/ActiveTheme.hpp"

namespace magda::daw::ui {

namespace {

// A chain node's body (200px less its 24px header), so cards and editors keep their chain size.
constexpr int kPanelHeight = 176;
constexpr int kGap = 6;
constexpr int kHeaderGap = 4;

using DeviceList = std::vector<std::pair<magda::DeviceId, juce::String>>;
using ParamNames = std::map<magda::DeviceId, std::vector<juce::String>>;

magda::TrackManager& tracks() {
    return magda::TrackManager::getInstance();
}

const magda::DeviceInfo* ownerDevice(const magda::ChainNodePath& owner) {
    const auto type = owner.getType();
    if (type != magda::ChainNodeType::Device && type != magda::ChainNodeType::TopLevelDevice)
        return nullptr;
    return tracks().getDeviceInChainByPath(owner);
}

const magda::ModArray* rawMods(const magda::ChainNodePath& owner) {
    if (owner.getType() == magda::ChainNodeType::Track) {
        const auto* track = tracks().getTrack(owner.trackId);
        return track != nullptr ? &track->mods : nullptr;
    }
    if (owner.getType() == magda::ChainNodeType::Rack) {
        const auto* rack = tracks().getRackByPath(owner);
        return rack != nullptr ? &rack->mods : nullptr;
    }
    const auto* device = ownerDevice(owner);
    return device != nullptr ? &device->mods : nullptr;
}

const magda::MacroArray* rawMacros(const magda::ChainNodePath& owner) {
    if (owner.getType() == magda::ChainNodeType::Track) {
        const auto* track = tracks().getTrack(owner.trackId);
        return track != nullptr ? &track->macros : nullptr;
    }
    if (owner.getType() == magda::ChainNodeType::Rack) {
        const auto* rack = tracks().getRackByPath(owner);
        return rack != nullptr ? &rack->macros : nullptr;
    }
    const auto* device = ownerDevice(owner);
    return device != nullptr ? &device->macros : nullptr;
}

/// Which of the two the chain offers this owner: the same rules as its header buttons.
std::pair<bool, bool> offeredModulation(const magda::ChainNodePath& owner) {
    switch (owner.getType()) {
        case magda::ChainNodeType::Track: {
            const auto* track = tracks().getTrack(owner.trackId);
            const bool chainTools = track != nullptr && track->type != magda::TrackType::Chord;
            return {chainTools, chainTools};
        }
        case magda::ChainNodeType::Rack:
            return {true, true};
        case magda::ChainNodeType::Device:
        case magda::ChainNodeType::TopLevelDevice: {
            const auto* device = ownerDevice(owner);
            if (device == nullptr || owner.isPostFx())
                return {false, false};
            const auto visibility =
                getHeaderControlVisibility(makeDeviceSlotTraits(*device), *device,
                                           device->format == magda::PluginFormat::Internal);
            return {visibility.mod, visibility.macro};
        }
        default:
            return {false, false};
    }
}

void collectChainDevices(const std::vector<magda::ChainElement>& elements, DeviceList& devices,
                         ParamNames& params) {
    for (const auto& element : elements) {
        if (magda::isDevice(element)) {
            const auto& device = magda::getDevice(element);
            devices.emplace_back(device.id, device.name);
            params[device.id] = device.paramNamesByIndex();
        } else {
            for (const auto& chain : magda::getRack(element).chains)
                collectChainDevices(chain.elements, devices, params);
        }
    }
}

/// What the link menus offer, as the chain's panels for the same owner do.
void collectLinkTargets(const magda::ChainNodePath& owner, DeviceList& devices,
                        ParamNames& params) {
    if (owner.getType() == magda::ChainNodeType::Track) {
        if (const auto* track = tracks().getTrack(owner.trackId))
            collectChainDevices(track->chain.fxChainElements, devices, params);
    } else if (owner.getType() == magda::ChainNodeType::Rack) {
        const auto* rack = tracks().getRackByPath(owner);
        if (rack == nullptr)
            return;
        for (const auto& chain : rack->chains) {
            // A chain-header sentinel, which the macro link picker groups by.
            devices.emplace_back(magda::INVALID_DEVICE_ID, chain.displayName());
            for (const auto* device : chain.getDevices()) {
                devices.emplace_back(device->id, device->name);
                params[device->id] = device->paramNamesByIndex();
            }
        }
    } else if (const auto* device = ownerDevice(owner)) {
        devices.emplace_back(device->id, device->name);
        params[device->id] = device->paramNamesByIndex();
        drum_grid_slot::appendAvailableDevices(device, devices);
        drum_grid_slot::appendDeviceParamNames(device, params);
    }
}

const magda::DeviceInfo* findDevice(const std::vector<magda::ChainElement>& elements,
                                    magda::DeviceId deviceId) {
    for (const auto& element : elements) {
        if (magda::isDevice(element)) {
            const auto& device = magda::getDevice(element);
            if (device.id == deviceId)
                return &device;
            if (device.pads) {
                for (const auto& pad : device.pads->chains)
                    if (const auto* found = findDevice(pad.elements, deviceId))
                        return found;
            }
        } else {
            for (const auto& chain : magda::getRack(element).chains)
                if (const auto* found = findDevice(chain.elements, deviceId))
                    return found;
        }
    }
    return nullptr;
}

juce::String paramName(magda::TrackId trackId, magda::DeviceId deviceId, int paramIndex) {
    if (const auto* track = tracks().getTrack(trackId))
        if (const auto* device = findDevice(track->chain.fxChainElements, deviceId))
            if (const auto* param = device->findParameterByIndex(paramIndex))
                return param->name;
    return "P" + juce::String(paramIndex);
}

std::vector<std::pair<magda::ModId, juce::String>> enabledModifiers(const magda::ModArray* mods) {
    std::vector<std::pair<magda::ModId, juce::String>> result;
    if (mods != nullptr)
        for (const auto& mod : *mods)
            if (mod.enabled)
                result.emplace_back(mod.id, magda::getModDisplayName(mod));
    return result;
}

int sectionHeight(bool open, bool editorOpen) {
    int height = CollapsibleSectionHeader::kHeight;
    if (open)
        height += kHeaderGap + kPanelHeight + (editorOpen ? kGap + kPanelHeight : 0);
    return height;
}

}  // namespace

ModulationSection::ModulationSection()
    : modsHeader_(tr("inspector.mods"), "inspector.mods"),
      macrosHeader_(tr("inspector.macros"), "inspector.macros"),
      modsPanel_(std::make_unique<ModsPanelComponent>()),
      modEditor_(std::make_unique<ModulatorEditorPanel>()),
      macroPanel_(std::make_unique<MacroPanelComponent>()),
      macroEditor_(std::make_unique<MacroEditorPanel>()) {
    for (auto* header : {&modsHeader_, &macrosHeader_}) {
        header->onCollapsedChanged = [this] { layoutChanged(); };
        addChildComponent(*header);
    }
    // The section header names the panel, so the panel's own header keeps only its pages.
    modsPanel_->setTitleShown(false);
    macroPanel_->setTitleShown(false);
    addChildComponent(*modsPanel_);
    addChildComponent(*modEditor_);
    addChildComponent(*macroPanel_);
    addChildComponent(*macroEditor_);

    wireModsPanel();
    wireModEditor();
    wireMacroPanel();
    wireMacroEditor();
}

ModulationSection::~ModulationSection() = default;

void ModulationSection::wireModsPanel() {
    modsPanel_->onModTargetChanged = [this](int modIndex, const magda::ControlTarget& target) {
        tracks().setModTarget(owner_, modIndex, target);
    };
    modsPanel_->onModLinkRemoved = [this](int modIndex, const magda::ControlTarget& target) {
        tracks().removeModLink(owner_, modIndex, target);
    };
    modsPanel_->onModAllLinksCleared = [this](int modIndex) {
        tracks().clearAllModLinks(owner_, modIndex);
    };
    modsPanel_->onModNameChanged = [this](int modIndex, juce::String name) {
        magda::UndoManager::getInstance().executeCommand(
            std::make_unique<magda::SetModNameCommand>(owner_, modIndex, name));
    };
    modsPanel_->onModClicked = [this](int modIndex) {
        if (openModIndex_ == modIndex)
            hideModEditor();
        else
            showModEditor(modIndex);
    };
    modsPanel_->onAddModRequested = [this](int slotIndex, magda::ModType type,
                                           magda::LFOWaveform waveform) {
        tracks().addMod(owner_, slotIndex, type, waveform);
    };
    modsPanel_->onModRemoveRequested = [this](int modIndex) {
        if (openModIndex_ == modIndex)
            hideModEditor();
        else if (openModIndex_ > modIndex)
            --openModIndex_;
        tracks().removeMod(owner_, modIndex);
        refresh();
    };
    modsPanel_->onModEnableToggled = [this](int modIndex, bool enabled) {
        tracks().setModEnabled(owner_, modIndex, enabled);
    };
}

void ModulationSection::wireModEditor() {
    // Each edit applies to the mod the editor shows.
    const auto toOpenMod = [this](auto apply) {
        return [this, apply](auto value) {
            if (openModIndex_ >= 0)
                apply(openModIndex_, value);
        };
    };
    modEditor_->onNameChanged = toOpenMod([this](int i, juce::String name) {
        magda::UndoManager::getInstance().executeCommand(
            std::make_unique<magda::SetModNameCommand>(owner_, i, name));
    });
    modEditor_->onRateChanged =
        toOpenMod([this](int i, float rate) { tracks().setModRate(owner_, i, rate); });
    modEditor_->onWaveformChanged = toOpenMod([this](int i, magda::LFOWaveform waveform) {
        tracks().setModWaveform(owner_, i, waveform);
    });
    modEditor_->onTempoSyncChanged =
        toOpenMod([this](int i, bool sync) { tracks().setModTempoSync(owner_, i, sync); });
    modEditor_->onSyncDivisionChanged = toOpenMod([this](int i, magda::SyncDivision division) {
        tracks().setModSyncDivision(owner_, i, division);
    });
    modEditor_->onTriggerModeChanged = toOpenMod(
        [this](int i, magda::LFOTriggerMode mode) { tracks().setModTriggerMode(owner_, i, mode); });
    modEditor_->onAudioAttackChanged =
        toOpenMod([this](int i, float ms) { tracks().setModAudioAttack(owner_, i, ms); });
    modEditor_->onAudioReleaseChanged =
        toOpenMod([this](int i, float ms) { tracks().setModAudioRelease(owner_, i, ms); });
    modEditor_->onEnvelopeChanged = [this](const magda::ModInfo& mod) {
        if (openModIndex_ >= 0)
            tracks().setModEnvelope(owner_, openModIndex_, mod);
    };
    modEditor_->onRandomChanged = [this](const magda::ModInfo& mod) {
        if (openModIndex_ >= 0)
            tracks().setModRandom(owner_, openModIndex_, mod);
    };
    modEditor_->onFollowerChanged = [this](const magda::ModInfo& mod) {
        if (openModIndex_ >= 0)
            tracks().setModFollower(owner_, openModIndex_, mod);
    };
    modEditor_->onCurveChanged = [this] {
        // The curve editor writes the points itself; the cards and the engine catch up here.
        modsPanel_->repaintWaveforms();
        tracks().notifyModCurveChanged(owner_);
    };
    modEditor_->onAdvancedClicked = [this] {
        showModulatorTriggerSourceMenu(owner_, openModIndex_);
    };
    modEditor_->onModLinkDeleted = [this](int modIndex, const magda::ControlTarget& target) {
        tracks().removeModLink(owner_, modIndex, target);
    };
    modEditor_->onModLinkBipolarChanged = [this](int modIndex, const magda::ControlTarget& target,
                                                 bool bipolar) {
        tracks().setModLinkBipolar(owner_, modIndex, target, bipolar);
    };
    modEditor_->onModLinkEnabledChanged = [this](int modIndex, const magda::ControlTarget& target,
                                                 bool enabled) {
        tracks().setModLinkEnabled(owner_, modIndex, target, enabled);
    };
    modEditor_->onModLinkAmountChanged = [this](int modIndex, const magda::ControlTarget& target,
                                                float amount) {
        tracks().setModLinkAmount(owner_, modIndex, target, amount);
    };
    modEditor_->setParamNameResolver([this](magda::DeviceId deviceId, int paramIndex) {
        return paramName(owner_.trackId, deviceId, paramIndex);
    });
}

void ModulationSection::wireMacroPanel() {
    macroPanel_->onMacroValueChanged = [this](int macroIndex, float value) {
        tracks().setMacroValue(owner_, macroIndex, value);
    };
    macroPanel_->onMacroTargetChanged = [this](int macroIndex, const magda::ControlTarget& target) {
        tracks().setMacroTarget(owner_, macroIndex, target);
    };
    macroPanel_->onMacroNameChanged = [this](int macroIndex, juce::String name) {
        magda::UndoManager::getInstance().executeCommand(
            std::make_unique<magda::SetMacroNameCommand>(owner_, macroIndex, name));
    };
    macroPanel_->onMacroLinkRemoved = [this](int macroIndex, const magda::ControlTarget& target) {
        tracks().removeMacroLink(owner_, macroIndex, target);
    };
    macroPanel_->onMacroAllLinksCleared = [this](int macroIndex) {
        tracks().clearAllMacroLinks(owner_, macroIndex);
    };
    macroPanel_->onMacroClicked = [this](int macroIndex) {
        if (openMacroIndex_ == macroIndex)
            hideMacroEditor();
        else
            showMacroEditor(macroIndex);
    };
    macroPanel_->onAddPageRequested = [this](int) { tracks().addMacroPage(owner_); };
    macroPanel_->onRemovePageRequested = [this](int) { tracks().removeMacroPage(owner_); };
}

void ModulationSection::wireMacroEditor() {
    macroEditor_->onNameChanged = [this](juce::String name) {
        if (openMacroIndex_ >= 0)
            magda::UndoManager::getInstance().executeCommand(
                std::make_unique<magda::SetMacroNameCommand>(owner_, openMacroIndex_, name));
    };
    macroEditor_->onValueChanged = [this](float value) {
        if (openMacroIndex_ >= 0)
            tracks().setMacroValue(owner_, openMacroIndex_, value);
    };
    macroEditor_->onLinkAmountChanged = [this](const magda::ControlTarget& target, float amount) {
        if (openMacroIndex_ >= 0)
            tracks().setMacroLinkAmount(owner_, openMacroIndex_, target, amount);
    };
    macroEditor_->onLinkRemoved = [this](const magda::ControlTarget& target) {
        if (openMacroIndex_ >= 0)
            tracks().removeMacroLink(owner_, openMacroIndex_, target);
    };
    macroEditor_->onLinkBipolarToggled = [this](const magda::ControlTarget& target, bool bipolar) {
        if (openMacroIndex_ >= 0)
            tracks().setMacroLinkBipolar(owner_, openMacroIndex_, target, bipolar);
    };
    macroEditor_->setParamNameResolver([this](magda::DeviceId deviceId, int paramIndex) {
        return paramName(owner_.trackId, deviceId, paramIndex);
    });
    macroEditor_->setModNameResolver([this](magda::ModId modId, int modParamIndex) {
        if (const auto* mods = rawMods(owner_))
            for (const auto& mod : *mods)
                if (mod.id == modId)
                    return magda::getModParameterDisplayName(mod, modParamIndex);
        return juce::String();
    });
}

void ModulationSection::setOwner(const magda::ChainNodePath& owner) {
    if (owner != owner_) {
        owner_ = owner;
        openModIndex_ = -1;
        openMacroIndex_ = -1;
        modsPanel_->setCurrentPage(0);
        macroPanel_->setCurrentPage(0);
    }
    refresh();
}

void ModulationSection::refresh() {
    const auto [offersMods, offersMacros] = offeredModulation(owner_);
    const auto* mods = offersMods ? rawMods(owner_) : nullptr;
    const auto* macros = offersMacros ? rawMacros(owner_) : nullptr;
    showsMods_ = mods != nullptr;
    showsMacros_ = macros != nullptr;

    DeviceList devices;
    ParamNames params;
    if (showsMods_ || showsMacros_)
        collectLinkTargets(owner_, devices, params);

    if (mods != nullptr) {
        if (openModIndex_ >= static_cast<int>(mods->size()))
            openModIndex_ = -1;
        modsPanel_->setParentPath(owner_);
        modsPanel_->setAvailableDevices(devices);
        modsPanel_->setDeviceParamNames(params);
        modsPanel_->setAvailableModifiers(enabledModifiers(mods));
        modsPanel_->setMods(*mods);
        modsPanel_->setSelectedModIndex(openModIndex_);
        updateModEditor();
    } else {
        openModIndex_ = -1;
    }

    if (macros != nullptr) {
        if (openMacroIndex_ >= static_cast<int>(macros->size()))
            openMacroIndex_ = -1;
        macroPanel_->setParentPath(owner_);
        macroPanel_->setAvailableDevices(devices);
        macroPanel_->setDeviceParamNames(params);
        macroPanel_->setAvailableModifiers(enabledModifiers(mods));
        macroPanel_->setMacros(*macros);
        macroPanel_->setSelectedMacroIndex(openMacroIndex_);
        updateMacroEditor();
    } else {
        openMacroIndex_ = -1;
    }

    layoutChanged();
}

void ModulationSection::refreshMacroValues() {
    if (!showsMacros_)
        return;
    if (const auto* macros = rawMacros(owner_))
        for (size_t i = 0; i < macros->size(); ++i)
            macroPanel_->updateMacroValueDisplay(static_cast<int>(i), (*macros)[i].value);
}

void ModulationSection::revealMods() {
    if (showsMods_ && modsHeader_.isCollapsed()) {
        modsHeader_.setCollapsed(false);
        layoutChanged();
    }
}

void ModulationSection::revealMacros() {
    if (showsMacros_ && macrosHeader_.isCollapsed()) {
        macrosHeader_.setCollapsed(false);
        layoutChanged();
    }
}

void ModulationSection::focus(magda::SelectionType type, int index) {
    switch (type) {
        case magda::SelectionType::Mod:
            openMod(index);
            break;
        case magda::SelectionType::Macro:
            openMacro(index);
            break;
        case magda::SelectionType::ModsPanel:
            revealMods();
            break;
        case magda::SelectionType::MacrosPanel:
            revealMacros();
            break;
        default:
            return;
    }
    const bool mods = type == magda::SelectionType::Mod || type == magda::SelectionType::ModsPanel;
    if (!(mods ? showsMods_ : showsMacros_))
        return;
    const juce::Component& editor =
        mods ? static_cast<const juce::Component&>(*modEditor_) : *macroEditor_;
    const juce::Component& header = mods ? modsHeader_ : macrosHeader_;
    scrollIntoView(editor.isVisible() ? editor : header);
}

void ModulationSection::scrollIntoView(const juce::Component& target) {
    auto* viewport = findParentComponentOfClass<juce::Viewport>();
    auto* content = viewport != nullptr ? viewport->getViewedComponent() : nullptr;
    if (content == nullptr)
        return;
    // From the section header down to the target, as much as the panel shows.
    const auto targetArea = content->getLocalArea(&target, target.getLocalBounds());
    const int top = content->getLocalArea(this, getLocalBounds()).getY();
    const auto visible = viewport->getViewArea();
    if (targetArea.getY() >= visible.getY() && targetArea.getBottom() <= visible.getBottom())
        return;
    const int y = juce::jmax(top, targetArea.getBottom() - visible.getHeight());
    viewport->setViewPosition(visible.getX(), juce::jmin(y, targetArea.getY()));
}

void ModulationSection::openMod(int modIndex) {
    revealMods();
    const auto* mods = showsMods_ ? rawMods(owner_) : nullptr;
    if (mods == nullptr || modIndex < 0 || modIndex >= static_cast<int>(mods->size()))
        return;
    modsPanel_->setCurrentPage(modIndex / magda::MODS_PER_PAGE);
    showModEditor(modIndex);
}

void ModulationSection::openMacro(int macroIndex) {
    revealMacros();
    const auto* macros = showsMacros_ ? rawMacros(owner_) : nullptr;
    if (macros == nullptr || macroIndex < 0 || macroIndex >= static_cast<int>(macros->size()))
        return;
    macroPanel_->setCurrentPage(macroIndex / magda::MACROS_PER_PAGE);
    showMacroEditor(macroIndex);
}

void ModulationSection::showModEditor(int modIndex) {
    openModIndex_ = modIndex;
    modsPanel_->setSelectedModIndex(modIndex);
    updateModEditor();
    layoutChanged();
}

void ModulationSection::hideModEditor() {
    openModIndex_ = -1;
    modsPanel_->setSelectedModIndex(-1);
    modEditor_->setSelectedModIndex(-1);
    layoutChanged();
}

void ModulationSection::showMacroEditor(int macroIndex) {
    openMacroIndex_ = macroIndex;
    macroPanel_->setSelectedMacroIndex(macroIndex);
    updateMacroEditor();
    layoutChanged();
}

void ModulationSection::hideMacroEditor() {
    openMacroIndex_ = -1;
    macroPanel_->setSelectedMacroIndex(-1);
    macroEditor_->setSelectedMacroIndex(-1);
    layoutChanged();
}

void ModulationSection::updateModEditor() {
    const auto* mods = rawMods(owner_);
    if (openModIndex_ < 0 || mods == nullptr || openModIndex_ >= static_cast<int>(mods->size()))
        return;
    // The getter refetches the live mod on each paint: the vector may have moved since.
    auto liveMod = [owner = owner_, index = openModIndex_]() -> const magda::ModInfo* {
        const auto* live = rawMods(owner);
        return live != nullptr && index < static_cast<int>(live->size()) ? &(*live)[index]
                                                                         : nullptr;
    };
    const auto& mod = (*mods)[static_cast<size_t>(openModIndex_)];
    modEditor_->setOwnerPath(owner_.trackId, owner_);
    modEditor_->setModInfo(mod, &mod, std::move(liveMod));
    modEditor_->setSelectedModIndex(openModIndex_);
}

void ModulationSection::updateMacroEditor() {
    const auto* macros = rawMacros(owner_);
    if (openMacroIndex_ < 0 || macros == nullptr ||
        openMacroIndex_ >= static_cast<int>(macros->size()))
        return;
    macroEditor_->setMacroInfo((*macros)[static_cast<size_t>(openMacroIndex_)]);
    macroEditor_->setSelectedMacroIndex(openMacroIndex_);
}

int ModulationSection::getPreferredHeight() const {
    int height = 0;
    if (showsMods_)
        height += sectionHeight(!modsHeader_.isCollapsed(), openModIndex_ >= 0);
    if (showsMods_ && showsMacros_)
        height += 2 * (modsHeader_.isCollapsed() ? 2 : kGap);
    if (showsMacros_)
        height += sectionHeight(!macrosHeader_.isCollapsed(), openMacroIndex_ >= 0);
    return height;
}

void ModulationSection::layoutChanged() {
    resized();
    repaint();
    const int height = getPreferredHeight();
    if (height != getHeight() && onPreferredHeightChanged)
        onPreferredHeightChanged();
}

void ModulationSection::paint(juce::Graphics& g) {
    // The chain's panel surface behind the cards, so they read as they do there.
    for (const auto& backdrop : {modsBackdrop_, macrosBackdrop_}) {
        if (backdrop.isEmpty())
            continue;
        const auto area = backdrop.toFloat();
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_BG));
        g.fillRoundedRectangle(area, 4.0f);
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_FRAME_BORDER));
        g.drawRoundedRectangle(area.reduced(0.5f), 4.0f, 1.0f);
    }
    if (separatorY_ >= 0) {
        g.setColour(ActiveTheme::getColour(ActiveTheme::BORDER));
        g.drawHorizontalLine(separatorY_, 0.0f, static_cast<float>(getWidth()));
    }
}

void ModulationSection::resized() {
    auto bounds = getLocalBounds();
    modsBackdrop_ = {};
    macrosBackdrop_ = {};
    separatorY_ = -1;

    // One section: its header, then the cards and the open item's editor beneath them.
    const auto layoutSection = [&bounds](CollapsibleSectionHeader& header, juce::Component& panel,
                                         juce::Component& editor, bool editorOpen,
                                         juce::Rectangle<int>& backdrop) {
        header.setBounds(bounds.removeFromTop(CollapsibleSectionHeader::kHeight));
        const bool open = !header.isCollapsed();
        panel.setVisible(open);
        editor.setVisible(open && editorOpen);
        if (open) {
            bounds.removeFromTop(kHeaderGap);
            backdrop = bounds.removeFromTop(kPanelHeight);
            panel.setBounds(backdrop);
            if (editorOpen) {
                bounds.removeFromTop(kGap);
                editor.setBounds(bounds.removeFromTop(kPanelHeight));
            }
        }
        return open;
    };

    modsHeader_.setVisible(showsMods_);
    macrosHeader_.setVisible(showsMacros_);
    if (!showsMods_) {
        modsPanel_->setVisible(false);
        modEditor_->setVisible(false);
    }
    if (!showsMacros_) {
        macroPanel_->setVisible(false);
        macroEditor_->setVisible(false);
    }

    if (showsMods_) {
        const bool open =
            layoutSection(modsHeader_, *modsPanel_, *modEditor_, openModIndex_ >= 0, modsBackdrop_);
        if (showsMacros_) {
            const int pad = open ? kGap : 2;
            bounds.removeFromTop(pad);
            separatorY_ = bounds.getY();
            bounds.removeFromTop(pad);
        }
    }
    if (showsMacros_)
        layoutSection(macrosHeader_, *macroPanel_, *macroEditor_, openMacroIndex_ >= 0,
                      macrosBackdrop_);
}

}  // namespace magda::daw::ui
