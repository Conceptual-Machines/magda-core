#pragma once

#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/AutomationInfo.hpp"
#include "core/TrackInfo.hpp"
#include "exec/EngineSession.hpp"
#include "exec/PlanValues.hpp"
#include "exec/RuntimeStateStore.hpp"
#include "plan/PlanCompiler.hpp"

namespace magda::test {

/**
 * @file EngineSessionScaffold.hpp
 * @brief What every test that drives a live EngineSession needs first.
 *
 * Compiling a plan, resolving its values, collecting the model's IDs and
 * publishing the four of them together is a protocol rather than a call, and a
 * test that is about a modifier or a tap has no business restating it. Every
 * copy of it is a place that has to be edited in lockstep when the protocol
 * changes, and there were two before this existed.
 *
 * Deliberately not a fixture class. What each test wants around the session
 * differs (its own devices, its own factory, its own idea of an edit), and a
 * base class would have to guess at all of it; what they share is the tracks
 * they start from, the MIDI they script, and the publish itself.
 */

/// A plain track feeding the master, with no macros and no modifiers.
inline TrackInfo makeTrack(TrackId id) {
    TrackInfo track;
    track.id = id;
    track.name = "Track " + juce::String(id);
    track.audioOutputDevice = "master";
    track.mods = createDefaultMods(0);
    return track;
}

/// The master at unity, so what leaves it is what reached it.
inline TrackInfo makeMaster() {
    auto master = makeTrack(MASTER_TRACK_ID);
    master.type = TrackType::Master;
    master.audioOutputDevice = {};
    master.volume = 1.0f;
    return master;
}

/**
 * @brief What a test queues for the next block, and what it saw of the blocks
 *        that consumed it.
 *
 * Owned by the test rather than by the session, because the point of scripting
 * MIDI at all is to put a note in one named block.
 */
struct ScriptedMidi {
    juce::MidiBuffer pending;
    int blocks = 0;
};

/// The session's end of it: hands over whatever is queued and clears the queue,
/// so a note is played once.
class ScriptedMidiSource final : public magda::engine::EngineMidiSource {
  public:
    explicit ScriptedMidiSource(ScriptedMidi* script) : script_(script) {}

    void render(const magda::engine::BlockInfo&, juce::MidiBuffer& out) override {
        if (script_ == nullptr)
            return;

        out.addEvents(script_->pending, 0, -1, 0);
        script_->pending.clear();
        ++script_->blocks;
    }

  private:
    ScriptedMidi* script_ = nullptr;
};

/** @brief The plan a publish compiled, and whether the session took it. */
struct PublishedPlan {
    std::shared_ptr<const magda::engine::RenderPlan> plan;
    bool published = false;
};

/**
 * @brief Compile @p tracks and @p master and publish them to @p session.
 *
 * The whole protocol in one call: the plan, the values resolved against it, the
 * IDs of what the model holds, and the context they are all rendered under. A
 * second call with a different track list is what an edit looks like from the
 * outside.
 *
 * @p lanes is the automation the project has, and empty is a project with none.
 * It belongs here rather than beside it because a lane is what puts a mixer
 * value in the parameter table at all, so a test about one publishes it with
 * the tracks or does not have it.
 */
inline PublishedPlan publishProject(magda::engine::EngineSession& session,
                                    const std::vector<TrackInfo>& tracks, const TrackInfo& master,
                                    const magda::engine::RenderContext& context,
                                    std::span<const AutomationLaneInfo> lanes = {}) {
    PublishedPlan result;
    result.plan = std::make_shared<const magda::engine::RenderPlan>(
        magda::engine::compileRenderPlan(tracks, master));

    magda::engine::PlanValues values;
    magda::engine::resolvePlanValues(*result.plan, tracks, master, values, lanes);

    const auto ids = magda::engine::collectRuntimeStateIds(tracks, master);
    result.published = session.publish(result.plan, context, ids, std::move(values)).published;
    return result;
}

}  // namespace magda::test
