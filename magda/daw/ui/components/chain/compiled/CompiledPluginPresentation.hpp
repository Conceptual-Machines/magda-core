#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>
#include <span>

#include "core/DeviceInfo.hpp"
#include "params/ParamLinkResolver.hpp"

namespace magda::daw::audio {
class MagdaDevice;
}

namespace magda::daw::ui {

/**
 * @brief Adapter over a compiled-Faust device's inline curve view.
 *
 * Each compiled plugin that wants a custom curve view (LFO trace,
 * transfer curve, beat grid …) implements this interface so the slot
 * component never has to switch on concrete view types.
 */
class CompiledDevicePanel {
  public:
    virtual ~CompiledDevicePanel() = default;
    virtual juce::Component& component() = 0;
    virtual void updateFromDevice(const magda::DeviceInfo&) = 0;
    virtual void updateFromDevice(const magda::DeviceInfo& device, const ParamLinkContext*) {
        updateFromDevice(device);
    }
    /// The device rendering behind this slot, whichever engine holds it (#2585).
    /// The panel keeps the handle: its views poll the DSP on a timer, and the
    /// instance must not be freed under them by a plan rebuild.
    virtual void bindDevice(std::shared_ptr<magda::daw::audio::MagdaDevice> device) = 0;
    virtual void setOnParameterChanged(std::function<void(int slotIndex, float displayValue)>) = 0;
    virtual void setOnLinkRequested(std::function<void(int slotIndex, float amount)>) {}
    virtual void setOnLinkAmountChanged(std::function<void(int slotIndex, float amount)>) {}
    /// Host wiring: invoked when the panel wants the automation lane for one of
    /// its slots shown (e.g. a right-click "Show Automation Lane" on a control).
    virtual void setOnShowAutomationLane(std::function<void(int slotIndex)>) {}
    virtual int preferredHeight() const = 0;

    /// True when the panel wants the host slot to hide its param grid and
    /// give the panel the entire body area. Default false; the 8-band EQ
    /// flips this to expose a "collapse knobs" toggle so the curve can take
    /// the whole slot.
    virtual bool wantsFullBody() const {
        return false;
    }

    /// Called by the host slot wiring once. The panel invokes the callback
    /// whenever its preferred layout changes (e.g. user toggles collapsed),
    /// which triggers a parent `resized()` pass to honour the new request.
    virtual void setOnLayoutChanged(std::function<void()>) {}

    /// Resolves the device's mods and macros afresh on each call. A panel reads
    /// them through this rather than keeping a context, whose pointers a chain
    /// rebuild frees.
    virtual void setLinkContextProvider(std::function<std::optional<ParamLinkContext>()>) {}
};

/**
 * @brief Presentation contract for a compiled plugin, complementing the
 *        audio-side CompiledPluginSpec.
 *
 * Kept here (UI module) so the audio registry never grows dependencies
 * on juce::Component / curve-view factories.
 */
struct CompiledPresentationSpec {
    const char* pluginId;
    int layoutCellCount;
    int layoutCellsPerRow;
    /// nullptr = no custom curve view; the slot falls back to the param-grid only.
    std::unique_ptr<CompiledDevicePanel> (*createPanel)(juce::String pluginId);
    /// Minimum fraction of the device slot body that the curve panel must
    /// occupy, expressed as `numerator / denominator`. Defaults to 3/4 to
    /// keep the existing curve-dominant layout for plugins like Reverb /
    /// Multiband. Plugins with a deep param grid (e.g. the 8-band EQ) can
    /// drop the numerator so the grid claims more of the body.
    int visualMinFractionNumerator = 3;
    int visualMinFractionDenominator = 4;
    /// When > 0, overrides the default device slot width (in pixels).
    /// Lets plugins with denser surfaces (e.g. the 8-band EQ's column
    /// strips) opt out of the global `BASE_SLOT_WIDTH` and request a
    /// wider host slot so cells don't truncate their labels.
    int preferredSlotWidth = 0;
    /// When true, the param grid fills cells top-to-bottom first
    /// (paramIndex N at grid position (row, col) → row = N % numRows,
    /// col = N / numRows). Default false = row-major, matching every
    /// existing compiled plugin. EQ flips this so each band becomes a
    /// vertical strip rather than half a row.
    bool columnMajorGrid = false;
    /// Optional device-specific enablement for a parameter slot. This changes
    /// interaction/presentation only; the parameter remains automatable.
    bool (*isParameterEnabled)(const magda::DeviceInfo& device, int slotIndex) = nullptr;
    /// The slots that remain as knobs, in order; -1 leaves a cell empty to group them.
    /// Empty keeps every slot in the grid.
    std::span<const int> knobSlots;
    /// Most knob columns beside the faceplate; 0 takes the default of two.
    int knobColumns = 0;
    /// The faceplate's width beside the knobs; 0 takes the default.
    int faceplateWidth = 0;
    /// Discrete slots shown in a strip across the top of the faceplate.
    std::span<const int> faceplateSlots;
    /// Set, the faceplate sits under these band knobs, one row, each band's in its third, low
    /// to high; knobSlots stand in a column on the left.
    std::span<const int> bandSlots;
    /// The device's own dry/wet slot: the side strip's mix knob drives it, so the grid drops it.
    int mixSlot = -1;
};

/// All presentation specs in stable iteration order. Each spec is defined
/// next to the device's curve view (or in its wrapper if there's no curve
/// view) via a named accessor; the aggregator below explicitly lists them.
std::span<const CompiledPresentationSpec* const> getAllCompiledPresentations();

/// Returns null if `pluginId` isn't a compiled plugin we recognise.
const CompiledPresentationSpec* findCompiledPresentation(const juce::String& pluginId);

}  // namespace magda::daw::ui
