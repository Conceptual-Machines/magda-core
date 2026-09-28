#include "MagdaEnginePrompt.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdlib>

#include "core/Config.hpp"
#include "engine/AudioEngineChoice.hpp"

namespace magda::daw::ui {

namespace {

bool shouldAsk() {
#if MAGDA_HAS_NATIVE_ENGINE
    auto& config = Config::getInstance();
    if (config.getMagdaEnginePromptShown())
        return false;

    // The smoke run and bug repros pick the engine this way; a prompt would stall them.
    if (std::getenv("MAGDA_AUDIO_ENGINE") != nullptr)
        return false;

    return parseAudioEngine(config.getAudioEngine()) != AudioEngineChoice::Magda;
#else
    return false;
#endif
}

}  // namespace

void offerMagdaEngineOnFirstLaunch(std::function<void()> then) {
    if (!shouldAsk()) {
        then();
        return;
    }

    const auto options =
        juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::QuestionIcon)
            .withTitle("Try the MAGDA Engine?")
            .withMessage("MAGDA has its own audio engine, now in beta. Would you like to use it?"
                         "\n\nYou can switch engines any time in Audio/MIDI Settings.")
            .withButton("Yes")
            .withButton("No");

    juce::AlertWindow::showAsync(options, [then = std::move(then)](int result) {
        auto& config = Config::getInstance();
        config.setMagdaEnginePromptShown(true);
        if (result == 1)
            config.setAudioEngine(settingWordFor(AudioEngineChoice::Magda));
        config.save();
        then();
    });
}

}  // namespace magda::daw::ui
