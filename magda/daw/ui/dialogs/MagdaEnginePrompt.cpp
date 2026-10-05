#include "MagdaEnginePrompt.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdlib>
#include <memory>

#include "core/Config.hpp"
#include "core/StringTable.hpp"
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
    juce::AlertWindow alert{tr("engine_prompt.title"), tr("engine_prompt.message"),
                            juce::MessageBoxIconType::QuestionIcon};

    // Nameless: AlertWindow paints a custom component's name above it, and a
    // ToggleButton takes its text as its name, so the words would appear twice.
    juce::ToggleButton dontShowAgain;

    Prompt() {
        dontShowAgain.setButtonText(tr("engine_prompt.dont_show_again"));
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
    prompt->alert.addButton(tr("engine_prompt.yes"), 1, juce::KeyPress(juce::KeyPress::returnKey));
    prompt->alert.addButton(tr("engine_prompt.no"), 0, juce::KeyPress(juce::KeyPress::escapeKey));

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
