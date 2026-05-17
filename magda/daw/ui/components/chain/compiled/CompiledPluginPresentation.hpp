#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <tracktion_engine/tracktion_engine.h>

#include <functional>
#include <memory>
#include <span>

#include "core/DeviceInfo.hpp"

namespace magda::daw::ui {

namespace te = tracktion::engine;

struct ParamLinkContext;

/// Tagged kind for the legacy *UI components a compiled plugin should
/// suppress (so the wrong UI doesn't claim the slot via `containsIgnoreCase`).
/// Grow this enum if a new legacy UI needs to be suppressed.
enum class LegacyUiKind {
    Delay,
    Chorus,
    Phaser,
    Reverb,
    Equaliser,
};

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
    virtual void bindPlugin(te::Plugin*) = 0;
    virtual void setOnParameterChanged(std::function<void(int slotIndex, float displayValue)>) = 0;
    virtual void setOnLinkRequested(std::function<void(int slotIndex, float amount)>) {}
    virtual void setOnLinkAmountChanged(std::function<void(int slotIndex, float amount)>) {}
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
    std::span<const LegacyUiKind> suppressLegacyUis;
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
};

/// All presentation specs in stable iteration order. Each spec is defined
/// next to the device's curve view (or in its wrapper if there's no curve
/// view) via a named accessor; the aggregator below explicitly lists them.
std::span<const CompiledPresentationSpec* const> getAllCompiledPresentations();

/// Returns null if `pluginId` isn't a compiled plugin we recognise.
const CompiledPresentationSpec* findCompiledPresentation(const juce::String& pluginId);

/// Convenience for legacy-UI gating: true if `pluginId` is a compiled
/// plugin that wants the named legacy UI suppressed.
bool shouldSuppressLegacyUi(const juce::String& pluginId, LegacyUiKind kind);

}  // namespace magda::daw::ui
