#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>

#include "ParamSlotComponent.hpp"
#include "SamplerUI.hpp"
#include "ui/components/common/SvgButton.hpp"

namespace tracktion {
inline namespace engine {
class Plugin;
}
}  // namespace tracktion

namespace magda::daw::audio {
class MagdaSamplerPlugin;
}

namespace magda::daw::ui {

/**
 * @brief A minimal device slot for a single plugin in a pad's FX chain.
 *
 * Layout:
 *   [PluginName    [UI] [On] [x]]   <- 18px header
 *   [                            ]
 *   [ SamplerUI / Param Grid     ]   <- Content
 *   [                            ]
 */
class PadDeviceSlot : public juce::Component {
  public:
    PadDeviceSlot();
    ~PadDeviceSlot() override;

    void setPlugin(tracktion::engine::Plugin* plugin);
    void setSampler(daw::audio::MagdaSamplerPlugin* sampler);
    void clear();
    int getPreferredWidth() const;
    void setPreferredWidth(int width) {
        preferredWidth_ = width;
    }

    tracktion::engine::Plugin* getPlugin() const {
        return plugin_;
    }
    bool isCollapsed() const {
        return collapsed_;
    }
    void setCollapsed(bool collapsed);
    void setSelected(bool selected) {
        selected_ = selected;
        repaint();
    }

    // Callbacks
    std::function<void()> onDeleteClicked;
    std::function<void()> onLayoutChanged;
    std::function<void()> onClicked;

    // Provide sampler pointer for SamplerUI wiring
    std::function<daw::audio::MagdaSamplerPlugin*()> getSampler;

    // Provide callbacks for file operations
    std::function<void(const juce::File&)> onSampleDropped;
    std::function<void()> onLoadSampleRequested;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

  private:
    static constexpr int HEADER_HEIGHT = 16;
    static constexpr int SLOT_WIDTH = 384;  // 8 cols × 48px PARAM_CELL_WIDTH
    static constexpr int SAMPLER_SLOT_WIDTH = 650;
    static constexpr int COLLAPSED_WIDTH = 48;
    static constexpr int PLUGIN_PARAM_SLOTS = 32;

    tracktion::engine::Plugin* plugin_ = nullptr;
    int preferredWidth_ = SLOT_WIDTH;
    bool collapsed_ = false;
    bool selected_ = false;

    // Header
    juce::Label nameLabel_;
    juce::TextButton deleteButton_;
    std::unique_ptr<magda::SvgButton> uiButton_;
    std::unique_ptr<magda::SvgButton> onButton_;

    // Content — one of these visible at a time
    std::unique_ptr<SamplerUI> samplerUI_;
    std::array<std::unique_ptr<ParamSlotComponent>, PLUGIN_PARAM_SLOTS> paramSlots_;

    void setupForSampler(daw::audio::MagdaSamplerPlugin* sampler);
    void setupForExternalPlugin(tracktion::engine::Plugin* plugin);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PadDeviceSlot)
};

}  // namespace magda::daw::ui
