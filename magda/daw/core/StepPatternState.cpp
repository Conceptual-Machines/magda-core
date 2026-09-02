#include "StepPatternState.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace magda::step_pattern {

namespace {

namespace seq = daw::audio::sequencer;

// The retired plugins' element and property names. Frozen: saved projects and
// presets are written in them, and the devices read the same spellings back
// out of the tree they are restored from.
const juce::Identifier kNumSteps("seqNumSteps");
const juce::Identifier kStepTree("STEP");
const juce::Identifier kStepIndex("idx");
const juce::Identifier kStepNote("note");
const juce::Identifier kStepOctave("oct");
const juce::Identifier kStepGate("gate");
const juce::Identifier kStepAccent("accent");
const juce::Identifier kStepGlide("glide");
const juce::Identifier kStepTie("tie");
const juce::Identifier kStepProbability("prob");
const juce::Identifier kStepVelocity("vel");
const juce::Identifier kNoteTree("NOTE");
const juce::Identifier kNoteNumber("note");
const juce::Identifier kNoteVelocity("vel");

/// A property of @p node, or @p fallback when it carries none.
template <typename T>
T propertyOr(const device_state::Node& node, const juce::Identifier& name, T fallback) {
    const auto* value = node.props.getVarPointer(name);
    return value != nullptr ? static_cast<T>(*value) : fallback;
}

/// The step index a `STEP` node claims, or -1 when it claims none.
int stepIndexOf(const device_state::Node& node) {
    return propertyOr<int>(node, kStepIndex, -1);
}

int patternLengthOf(const device_state::Doc& doc) {
    return std::clamp(propertyOr<int>(doc.root, kNumSteps, 16), 1, seq::kMaxSteps);
}

/// Drop every `STEP` child, so the pattern being written is the whole pattern.
void removeStepNodes(device_state::Doc& doc) {
    auto& children = doc.root.children;
    children.erase(std::remove_if(children.begin(), children.end(),
                                  [](const device_state::Node& node) {
                                      return node.type == kStepTree.toString();
                                  }),
                   children.end());
}

}  // namespace

MonoPattern readMono(const device_state::Doc& doc) {
    MonoPattern pattern;
    pattern.length = patternLengthOf(doc);

    for (const auto& node : doc.root.children) {
        if (node.type != kStepTree.toString())
            continue;
        const int index = stepIndexOf(node);
        if (index < 0 || index >= seq::kMaxSteps)
            continue;

        auto& step = pattern.steps[static_cast<size_t>(index)];
        step.noteNumber = std::clamp(propertyOr<int>(node, kStepNote, 60), 0, 127);
        step.octaveShift = std::clamp(propertyOr<int>(node, kStepOctave, 0), -2, 2);
        step.gate = propertyOr<bool>(node, kStepGate, true);
        step.accent = propertyOr<bool>(node, kStepAccent, false);
        step.glide = propertyOr<bool>(node, kStepGlide, false);
        step.tie = propertyOr<bool>(node, kStepTie, false);
    }

    return pattern;
}

void writeMono(device_state::Doc& doc, const MonoPattern& pattern) {
    doc.root.props.set(kNumSteps, pattern.playingLength());
    removeStepNodes(doc);

    // Every step, not just the playing ones: shortening a pattern in the UI and
    // lengthening it again must not lose what the hidden steps held. A step
    // nobody has touched is written as nothing at all - absence and a default
    // step read back the same.
    const daw::audio::sequencer::MonoStep defaults;
    for (int i = 0; i < seq::kMaxSteps; ++i) {
        const auto& step = pattern.steps[static_cast<size_t>(i)];
        if (step == defaults)
            continue;

        device_state::Node node;
        node.type = kStepTree.toString();
        node.props.set(kStepIndex, i);
        node.props.set(kStepNote, std::clamp(step.noteNumber, 0, 127));
        node.props.set(kStepOctave, std::clamp(step.octaveShift, -2, 2));
        node.props.set(kStepGate, step.gate);
        node.props.set(kStepAccent, step.accent);
        node.props.set(kStepGlide, step.glide);
        node.props.set(kStepTie, step.tie);
        doc.root.children.push_back(std::move(node));
    }
}

PolyPattern readPoly(const device_state::Doc& doc) {
    PolyPattern pattern;
    pattern.length = patternLengthOf(doc);

    for (const auto& node : doc.root.children) {
        if (node.type != kStepTree.toString())
            continue;
        const int index = stepIndexOf(node);
        if (index < 0 || index >= seq::kMaxSteps)
            continue;

        auto& step = pattern.steps[static_cast<size_t>(index)];
        step.gate = propertyOr<bool>(node, kStepGate, true);
        step.tie = propertyOr<bool>(node, kStepTie, false);
        step.probability = std::clamp(propertyOr<float>(node, kStepProbability, 1.0f), 0.0f, 1.0f);
        step.velocity = std::clamp(propertyOr<int>(node, kStepVelocity, 100), 1, 127);

        step.noteCount = 0;
        for (const auto& noteNode : node.children) {
            if (noteNode.type != kNoteTree.toString())
                continue;
            if (step.noteCount >= seq::kMaxNotesPerStep)
                break;

            auto& note = step.notes[static_cast<size_t>(step.noteCount)];
            note.noteNumber = std::clamp(propertyOr<int>(noteNode, kNoteNumber, 60), 0, 127);
            note.velocity = std::clamp(propertyOr<int>(noteNode, kNoteVelocity, 0), 0, 127);
            ++step.noteCount;
        }
    }

    return pattern;
}

void writePoly(device_state::Doc& doc, const PolyPattern& pattern) {
    doc.root.props.set(kNumSteps, pattern.playingLength());
    removeStepNodes(doc);

    // Every step, not just the playing ones, and only the ones that differ
    // from a default step (see writeMono).
    const daw::audio::sequencer::PolyStep defaults;
    for (int i = 0; i < seq::kMaxSteps; ++i) {
        const auto& step = pattern.steps[static_cast<size_t>(i)];
        if (step == defaults)
            continue;

        device_state::Node node;
        node.type = kStepTree.toString();
        node.props.set(kStepIndex, i);
        node.props.set(kStepGate, step.gate);
        node.props.set(kStepTie, step.tie);
        node.props.set(kStepProbability, std::clamp(step.probability, 0.0f, 1.0f));
        node.props.set(kStepVelocity, std::clamp(step.velocity, 1, 127));

        const int noteCount = std::min(step.noteCount, seq::kMaxNotesPerStep);
        for (int n = 0; n < noteCount; ++n) {
            const auto& note = step.notes[static_cast<size_t>(n)];
            device_state::Node noteNode;
            noteNode.type = kNoteTree.toString();
            noteNode.props.set(kNoteNumber, std::clamp(note.noteNumber, 0, 127));
            if (note.velocity > 0)
                noteNode.props.set(kNoteVelocity, std::clamp(note.velocity, 1, 127));
            node.children.push_back(std::move(noteNode));
        }

        doc.root.children.push_back(std::move(node));
    }
}

MonoPattern monoPatternOf(const juce::String& deviceStateText) {
    if (auto doc = device_state::decode(deviceStateText))
        return readMono(*doc);
    return {};
}

PolyPattern polyPatternOf(const juce::String& deviceStateText) {
    if (auto doc = device_state::decode(deviceStateText))
        return readPoly(*doc);
    return {};
}

}  // namespace magda::step_pattern
