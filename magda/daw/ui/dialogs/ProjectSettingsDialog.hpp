#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

#include "../../project/ProjectInfo.hpp"

namespace magda {

/**
 * Per-project settings dialog (File > Project Settings).
 *
 * Two tabs over the authoritative per-project values held in ProjectInfo.
 * Metadata is the title and credits in its ProjectMetadata block; General is
 * total timeline length (bars), working/render sample rate, and render / bounce
 * bit depth. New projects seed the General values from the global Config
 * defaults and this dialog overrides them for the current project; the metadata
 * has no global default, being per-project by nature.
 */
class ProjectSettingsDialog : public juce::Component {
  public:
    ProjectSettingsDialog();
    ~ProjectSettingsDialog() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

    // The hosting window's background is a colour override handed over once at
    // construction, so it does not follow a live theme switch on its own.
    void lookAndFeelChanged() override;

    static void showDialog(juce::Component* parent);

  private:
    /**
     * The Metadata tab: one text field per DAWproject MetaData element, laid out
     * in the order kProjectMetadataFields declares them.
     */
    class MetadataPage : public juce::Component {
      public:
        MetadataPage();

        void resized() override;

        void load(const ProjectInfo& info);
        void apply(ProjectInfo& info) const;

        static int preferredHeight();

      private:
        struct Row {
            juce::Label label;
            juce::TextEditor editor;
        };

        std::array<Row, kProjectMetadataFields.size()> rows_;
    };

    /**
     * The General tab: the technical per-project values.
     *
     * It reads and writes ProjectInfo only. Whether the same values also become
     * the defaults for new projects is asked here but answered by the dialog,
     * which owns the side effects on Config and the live timeline.
     */
    class GeneralPage : public juce::Component {
      public:
        GeneralPage();

        void resized() override;

        void load(const ProjectInfo& info);
        void apply(ProjectInfo& info) const;

        bool shouldSaveAsDefault() const;

        static int preferredHeight();

      private:
        juce::Label lengthLabel_, sampleRateLabel_, renderBitLabel_, bounceBitLabel_;
        juce::Slider lengthSlider_;
        juce::ComboBox sampleRateCombo_, renderBitCombo_, bounceBitCombo_;
        juce::ToggleButton saveAsDefaultBtn_;
    };

    void loadSettings();
    void applySettings();

    // Declared before the TabbedComponent that shows them, so the tabs go first
    // when the dialog is torn down.
    MetadataPage metadataPage_;
    GeneralPage generalPage_;

    juce::TabbedComponent tabs_{juce::TabbedButtonBar::TabsAtTop};
    juce::TextButton okBtn_{"OK"}, cancelBtn_{"Cancel"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProjectSettingsDialog)
};

}  // namespace magda
