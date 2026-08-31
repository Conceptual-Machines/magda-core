#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

#include "../../project/ProjectInfo.hpp"

namespace magda {

/**
 * Per-project settings dialog (File > Project Settings).
 *
 * Two tabs over the authoritative per-project values held in ProjectInfo.
 * General is total timeline length (bars), working/render sample rate, and
 * render / bounce bit depth; Metadata is the title and credits in its
 * ProjectMetadata block. New projects seed the General values from the global
 * Config defaults and this dialog overrides them for the current project. The
 * credit fields are seeded the same way, from the Credits block on Preferences >
 * Defaults - but only the ones describing the person, never the title or the
 * year, which belong to the work.
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
     * It reads and writes ProjectInfo only; the dialog owns the side effect on
     * the live timeline.
     */
    class GeneralPage : public juce::Component {
      public:
        GeneralPage();

        void resized() override;

        void load(const ProjectInfo& info);
        void apply(ProjectInfo& info) const;

        static int preferredHeight();

      private:
        juce::Label lengthLabel_, sampleRateLabel_, renderBitLabel_, bounceBitLabel_;
        juce::Slider lengthSlider_;
        juce::ComboBox sampleRateCombo_, renderBitCombo_, bounceBitCombo_;
    };

    void loadSettings();
    void applySettings();

    // Declared before the TabbedComponent that shows them, so the tabs go first
    // when the dialog is torn down.
    GeneralPage generalPage_;
    MetadataPage metadataPage_;

    juce::TabbedComponent tabs_{juce::TabbedButtonBar::TabsAtTop};
    juce::TextButton applyBtn_, cancelBtn_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProjectSettingsDialog)
};

}  // namespace magda
