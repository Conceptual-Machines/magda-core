#include "FourOscConversionPrompt.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "audio/FourOscMigration.hpp"
#include "core/Config.hpp"
#include "core/TrackManager.hpp"
#include "project/ProjectManager.hpp"

namespace magda::daw::ui {

namespace {

namespace audio = magda::daw::audio;

/// The alert and the tickbox it carries. AlertWindow does not own a custom
/// component, so the two are held together and read in the callback.
struct Prompt {
    juce::AlertWindow alert;

    // Nameless on purpose. AlertWindow paints a custom component's name as a
    // label above it (juce_AlertWindow.cpp), and a ToggleButton built from a
    // string takes that string as its name as well as its text, so the words
    // appeared twice.
    juce::ToggleButton dontAskAgain;

    Prompt(const juce::String& title, const juce::String& message)
        : alert(title, message, juce::MessageBoxIconType::QuestionIcon) {
        dontAskAgain.setButtonText("Don't ask again");
    }
};

/// Convert, then save as a project of its own. The one that was opened is
/// never written to, so a 4OSC project stays openable on the Tracktion
/// engine.
void convertAndSave() {
    auto& projects = ProjectManager::getInstance();
    const auto destination = audio::convertedProjectFileFor(projects.getCurrentProjectFile(),
                                                            projects.requiresV1ProjectCopy());

    audio::convertFourOscDevices(TrackManager::getInstance());

    // The conversion writes the model directly. Without this a failed save
    // below leaves it in the session with nothing to say it is unsaved, and
    // the window closes without asking.
    projects.markDirty();

    // An unsaved project has nowhere to sit beside, so it keeps whatever the
    // conversion did and the user names it at their next save.
    if (destination == juce::File{})
        return;

    // Copies rather than moves the media: the project being converted stays
    // where it is and its .mgd still names the files it always did.
    if (!projects.saveProjectAs(destination, ProjectManager::MediaTransfer::Copy))
        juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                         .withIconType(juce::MessageBoxIconType::WarningIcon)
                                         .withTitle("Could not save the converted project")
                                         .withMessage(projects.getLastError() +
                                                      "\n\nThe conversion is still in this "
                                                      "session. Save it somewhere with Save As.")
                                         .withButton("OK"),
                                     nullptr);
}

}  // namespace

void offerFourOscConversion() {
    auto& projects = ProjectManager::getInstance();
    const auto v1Copy = projects.requiresV1ProjectCopy();
    if (!v1Copy && Config::getInstance().getSkipFourOscConversionPrompt())
        return;

    // Already through this engine's migration, so there is nothing to offer.
    // An empty word is a project saved before the field existed, which is a
    // Tracktion project.
    if (!v1Copy && projects.getCurrentProjectInfo().savedWithEngine == "magda")
        return;

    if (projects.getCurrentProjectFile() == juce::File{})
        return;

    auto& tracks = TrackManager::getInstance();
    const auto* master = tracks.getTrack(MASTER_TRACK_ID);
    if (master == nullptr)
        return;

    const auto candidates = audio::findFourOscDevices(tracks.getTracks(), *master);

    auto prompt = std::make_shared<Prompt>(v1Copy ? "Save as a new v1 project?"
                                                  : "Open as a MAGDA Engine project?",
                                           audio::describeMigration(candidates, v1Copy));

    if (!v1Copy) {
        prompt->dontAskAgain.setSize(200, 24);
        prompt->alert.addCustomComponent(&prompt->dontAskAgain);
    }
    prompt->alert.addButton(v1Copy ? "Save v1 copy" : "Create it", 1,
                            juce::KeyPress(juce::KeyPress::returnKey));
    prompt->alert.addButton("Not now", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    const auto source = projects.getCurrentProjectFile();
    const auto generation = projects.getProjectGeneration();
    const auto onDismissed = [prompt, source, generation](int result) {
        // Remembered whichever button was pressed: somebody who ticks it and
        // converts this project does not want asking about the next.
        if (prompt->dontAskAgain.getToggleState())
            Config::getInstance().setSkipFourOscConversionPrompt(true);

        if (result != 1)
            return;

        // Off the modal callback, so the alert is gone before a save dialog or
        // an error of its own can appear behind it.
        juce::MessageManager::callAsync([source, generation] {
            auto& projects = ProjectManager::getInstance();
            if (projects.getProjectGeneration() != generation ||
                projects.getCurrentProjectFile() != source)
                return;
            convertAndSave();
        });
    };

    prompt->alert.enterModalState(true, juce::ModalCallbackFunction::create(onDismissed), false);
}

}  // namespace magda::daw::ui
