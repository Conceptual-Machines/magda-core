#include "MagdaEnginePrompt.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdlib>
#include <memory>

#include "core/Config.hpp"
#include "engine/AudioEngineChoice.hpp"

namespace magda::daw::ui {

namespace {

bool shouldAsk() {
#if MAGDA_HAS_NATIVE_ENGINE
    auto& config = Config::getInstance();
    if (config.getSkipMagdaEnginePrompt())
        return false;

    // The smoke run and bug repros pick the engine this way; a prompt would stall them.
    if (std::getenv("MAGDA_AUDIO_ENGINE") != nullptr)
        return false;

    return parseAudioEngine(config.getAudioEngine()) != AudioEngineChoice::Magda;
#else
    return false;
#endif
}

/// AlertWindow does not own a custom component, so the two are held together.
struct Prompt {
    juce::AlertWindow alert{"Try the MAGDA Engine?",
                            "MAGDA has its own audio engine, now in beta. Would you like to use "
                            "it?\n\nYou can switch engines any time in Audio/MIDI Settings.",
                            juce::MessageBoxIconType::QuestionIcon};

    // Nameless: AlertWindow paints a custom component's name above it, and a
    // ToggleButton takes its text as its name, so the words would appear twice.
    juce::ToggleButton dontShowAgain;

    Prompt() {
        dontShowAgain.setButtonText("Don't show again");
    }
};

}  // namespace

void offerMagdaEngineAtLaunch(std::function<void()> then) {
    if (!shouldAsk()) {
        then();
        return;
    }

    auto prompt = std::make_shared<Prompt>();
    prompt->dontShowAgain.setSize(200, 24);
    prompt->alert.addCustomComponent(&prompt->dontShowAgain);
    prompt->alert.addButton("Yes", 1, juce::KeyPress(juce::KeyPress::returnKey));
    prompt->alert.addButton("No", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    const auto onDismissed = [prompt, then = std::move(then)](int result) {
        auto& config = Config::getInstance();
        if (prompt->dontShowAgain.getToggleState())
            config.setSkipMagdaEnginePrompt(true);
        if (result == 1)
            config.setAudioEngine(settingWordFor(AudioEngineChoice::Magda));
        config.save();
        then();
    };

    prompt->alert.enterModalState(true, juce::ModalCallbackFunction::create(onDismissed), false);
}

}  // namespace magda::daw::ui
