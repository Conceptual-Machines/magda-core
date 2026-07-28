#include "AudioEngine.hpp"

#include "../core/UndoManager.hpp"

namespace magda {

std::unique_ptr<UndoableCommand> AudioEngine::createTempoSequenceRippleCommand(
    TempoSequenceRippleMode /*mode*/, double /*startBeat*/, double /*endBeat*/) {
    return nullptr;
}

}  // namespace magda
