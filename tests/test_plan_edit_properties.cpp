#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "PlanEditHarness.hpp"
#include "PlanEditSequence.hpp"
#include "plan/PlanCompiler.hpp"
#include "plan/PlanDiff.hpp"
#include "plan/PlanDump.hpp"
#include "plan/RenderPlan.hpp"

/**
 * @file test_plan_edit_properties.cpp
 * @brief What the differ decided, over edit sequences nobody wrote (#2077).
 *
 * The differ (#2014) and the crossfade of changed edges across a swap (#2019)
 * are pinned elsewhere by pairs of plans I sat down and wrote, which pins them
 * against my reading of the compatibility matrix. The space of pairs is the
 * space of projects squared, and the failures that matter are the ones nobody
 * thought to write down. So this generates the pairs instead, and asserts
 * properties rather than answers.
 *
 * ## What is asserted, and why each one is not a restatement of the code
 *
 * **State compatibility.** An op carries only when it is the same op computing
 * the same thing from the same places, which is checkable from the two plans
 * without asking the differ: build a signature out of the op's kind, its ports,
 * its arity and the keys of whatever feeds each slot, and require the decision
 * to agree with it in both directions. Soundness alone would be satisfied by a
 * differ that carried nothing, so completeness is asserted too: an op whose
 * signature is unchanged and which did not carry is a failure.
 *
 * Then the half a signature cannot see, which is what the decision costs at
 * runtime. The number of delay lines and fade ramps the executor says it
 * adopted is predicted from the two plans and their resolved layouts, and
 * required to match exactly. A differ that carried an op whose delay is now a
 * different length would pass the signature test and fail this one.
 *
 * **Retirement.** carriedFrom is a partial injection from the new plan into the
 * old one, and `retired` is exactly its complement: every old op is adopted once
 * or retired once, never both and never neither. What that is worth on the
 * runtime side is asserted through the store: an instance is destroyed only when
 * neither the live plan nor the model names it, and always on the publishing
 * thread. And because the harness lets go of the epoch it replaced as soon as
 * the new one has prepared against it, every render below is also the assertion
 * that what was carried outlived the epoch it was carried from.
 *
 * **PDC alignment.** Every op that fans in has all its inputs arriving at one
 * latency, and at least one of them waits for nothing, so compensation is what
 * the paths required rather than what the pass felt like adding. Across a swap,
 * a track no edit touched reports the same latency at its own output as it did
 * before: an edit in one branch that silently moves another is exactly what
 * this catches.
 *
 * **Silence and discontinuity outside the changed region.** The one that catches
 * the most. Every track carries an analysis device that keeps what passed
 * through it, and a sequence is rendered twice: once whole, and once with every
 * edit that cannot reach a chosen track removed. The two recordings of that
 * track have to be equal sample for sample. Not close: equal. The two runs
 * publish a different number of plans, swap at different moments and carry
 * different amounts of state across those swaps, and none of that may cost the
 * track a sample.
 *
 * On the track an edit does touch, what is asserted is a bound and a
 * destination rather than a null, because the edit is meant to change what it
 * renders: nothing that is not a number, nothing that has run away, no step
 * where the pass reported it faded every edge it moved, and, once the transient
 * is over, exactly what a session that had just started on the same project
 * renders. That last one is the point of carrying state at all: it may change
 * how the signal gets there, never where it arrives.
 *
 * What is deliberately not asserted there is a level. A fade sits on the edge
 * the edit moved, which is usually inside the chain, and the rest of the chain
 * then scales a mixture of two internal signals: the result is routinely
 * outside the two steady states either side of it, and a bound that said
 * otherwise would be a bound on a project rather than on the engine.
 *
 * ## Failures
 *
 * A property that reports "failed after 4000 random edits" has said nothing
 * actionable, so a failure is shrunk before it is printed: edits are deleted
 * while the property still fails, and what is printed is the minimal sequence
 * and the seed that found it. Edits address their targets by id precisely so
 * that any subsequence of one is still a sequence that runs.
 *
 * A seed that has ever failed goes into kRecordedSeeds and is run on every
 * build, whatever the random sweep happens to draw that day.
 */

using namespace magda;
using magda::edits::Edit;
using magda::edits::EditKind;
using magda::edits::Harness;
using magda::edits::Material;
using magda::edits::Project;
using magda::edits::Published;
using magda::engine::OpKey;
using magda::engine::PlanDiff;
using magda::engine::PreparedLayout;
using magda::engine::RenderPlan;

namespace {

// --- how much is run ---------------------------------------------------------

/// Long enough that a plan is several edits away from the one it started as,
/// short enough that a shrunk failure is something to read.
constexpr int kSequenceLength = 14;

/// The sweep. The plan properties are a compile and a prepare per step, so they
/// are run over far more sequences than the render properties, which render
/// five sessions each.
constexpr int kPlanSeeds = 240;
constexpr int kRenderSeeds = 24;

/// Blocks rendered after every step of a sequence, applied or not, so that two
/// runs of one sequence cover the same stretch of timeline whatever they
/// published. Long enough for the longest delay a generated project can hold
/// and a whole fade on top of it; the properties assert that bound rather than
/// assume it.
constexpr int kBlocksPerStep = 20;

/// And a window shorter than a fade, so the next edit arrives while the last
/// one is still ramping. That is the case the stacking in #2019 exists for, and
/// the only way a running ramp is ever what a swap has to carry.
constexpr int kBlocksMidFade = 2;

/// Seeds that have failed a property. Each one is a case the sweep found once
/// and would only find again by chance, so it is run on every build.
constexpr std::array<std::uint64_t, 0> kRecordedSeeds{};

// --- what a property is ------------------------------------------------------

struct Report;

/// A property is something that can be re-run on any subsequence, which is what
/// lets the shrinker use the same predicate that failed. The report is optional
/// because a shrink run is not coverage: it re-runs the same sequence dozens of
/// times and counting those would make the numbers below say nothing.
using Property = std::function<std::optional<std::string>(const std::vector<Edit>&, Report*)>;

/// What the shrinker asks, which is only whether it still fails.
using Predicate = std::function<std::optional<std::string>(const std::vector<Edit>&)>;

/// Counts printed on every run rather than only on a failure. A property test
/// that has quietly stopped exercising anything looks exactly like one that
/// passes, and these are what says which it is.
struct Report {
    int sequences = 0;
    int publishes = 0;
    std::int64_t carried = 0;
    std::int64_t retired = 0;
    int fadesInserted = 0;
    int fadesUnfaded = 0;
    int delayLinesCarried = 0;
    int fadeRampsCarried = 0;
    int instancesDestroyed = 0;
    std::int64_t samplesCompared = 0;
    int steadyStatesCompared = 0;

    /// Transients the step bound was actually measured over, and transients it
    /// let through because the pass had refused an edge or something upstream
    /// started flushed. Counted separately because the exemption is the whole
    /// way the bound can quietly stop existing: one refused edge anywhere in a
    /// plan exempts every track in it, so a regression that started reporting
    /// one would disable the bound while every test stayed green.
    int transientsBounded = 0;
    int transientsExempt = 0;

    void print(const char* name) const {
        std::cout << "magda-differ-properties " << name << "\n"
                  << "  sequences=" << sequences << " publishes=" << publishes << "\n"
                  << "  ops carried=" << carried << " retired=" << retired << "\n"
                  << "  fades inserted=" << fadesInserted << " unfaded=" << fadesUnfaded << "\n"
                  << "  adopted delay lines=" << delayLinesCarried << " ramps=" << fadeRampsCarried
                  << "\n"
                  << "  instances destroyed=" << instancesDestroyed
                  << " samples compared=" << samplesCompared
                  << " steady states compared=" << steadyStatesCompared << "\n"
                  << "  transients bounded=" << transientsBounded << " exempt=" << transientsExempt
                  << std::endl;
    }
};

std::string describe(const OpKey& key) {
    return engine::toString(key);
}

/// Everything about an op that has to be unchanged for its runtime state to
/// mean the same thing. Producers appear by key rather than by index: an edit
/// anywhere earlier in the plan moves every op after it, and an identity built
/// on indices would say every op changed.
std::string signatureOf(const RenderPlan& plan, std::size_t op) {
    const auto& node = plan.ops[op];

    std::ostringstream out;
    out << engine::toString(node.kind) << " outputs=";
    for (const auto kind : node.outputs)
        out << engine::toString(kind) << ",";

    out << " inputs=";
    for (const auto& input : node.inputs) {
        if (!input.valid()) {
            out << "none;";
            continue;
        }
        out << describe(plan.ops[static_cast<std::size_t>(input.op)].key) << ":" << input.port
            << ";";
    }

    return out.str();
}

std::map<OpKey, std::size_t> indexByKey(const RenderPlan& plan) {
    std::map<OpKey, std::size_t> byKey;
    for (std::size_t i = 0; i < plan.ops.size(); ++i)
        byKey.emplace(plan.ops[i].key, i);
    return byKey;
}

std::size_t flatPort(const PreparedLayout& layout, const engine::PortRef& ref) {
    return static_cast<std::size_t>(layout.portOffsets[static_cast<std::size_t>(ref.op)] +
                                    ref.port);
}

int latencyAt(const PreparedLayout& layout, const engine::PortRef& ref) {
    return layout.latency.portLatency[flatPort(layout, ref)];
}

// --- the differ's decisions --------------------------------------------------

std::optional<std::string> checkCarry(const RenderPlan& before, const RenderPlan& after,
                                      const PlanDiff& diff) {
    if (diff.carriedFrom.size() != after.ops.size())
        return "carriedFrom has one entry per op of the new plan, and this one does not";

    const auto beforeByKey = indexByKey(before);
    std::vector<int> adoptions(before.ops.size(), 0);
    auto carried = 0;

    for (std::size_t i = 0; i < after.ops.size(); ++i) {
        const auto from = diff.carriedFrom[i];
        const auto found = beforeByKey.find(after.ops[i].key);
        const auto couldCarry = found != beforeByKey.end() &&
                                signatureOf(before, found->second) == signatureOf(after, i);

        if (from == engine::INVALID_OP_ID) {
            if (couldCarry)
                return "an op that was the same op in both plans did not carry: " +
                       describe(after.ops[i].key);
            continue;
        }

        if (from < 0 || static_cast<std::size_t>(from) >= before.ops.size())
            return "carriedFrom names an op the old plan does not have, at " +
                   describe(after.ops[i].key);

        const auto source = static_cast<std::size_t>(from);
        ++adoptions[source];
        ++carried;

        if (before.ops[source].key != after.ops[i].key)
            return "state carried between two different locations: " +
                   describe(before.ops[source].key) + " into " + describe(after.ops[i].key);

        if (signatureOf(before, source) != signatureOf(after, i))
            return "state carried into an op that computes something else: " +
                   describe(after.ops[i].key) + "\n    was: " + signatureOf(before, source) +
                   "\n    now: " + signatureOf(after, i);
    }

    if (carried != diff.carried)
        return "the carried count does not match the decisions";

    for (std::size_t i = 0; i < adoptions.size(); ++i)
        if (adoptions[i] > 1)
            return "one op's state was carried into two: " + describe(before.ops[i].key);

    // Retirement is the complement of adoption, exactly: an op that is neither
    // is state nothing will ever free, and one that is both is state two
    // epochs believe they own.
    std::vector<engine::OpId> expected;
    for (std::size_t i = 0; i < adoptions.size(); ++i)
        if (adoptions[i] == 0)
            expected.push_back(static_cast<engine::OpId>(i));

    if (diff.retired != expected)
        return "what was retired is not what nothing adopted";

    return std::nullopt;
}

// --- latency -----------------------------------------------------------------

std::optional<std::string> checkAlignment(const RenderPlan& plan, const PreparedLayout& layout) {
    for (std::size_t i = 0; i < plan.ops.size(); ++i) {
        const auto& op = plan.ops[i];

        if (layout.latency.delaySamples[i] != 0 && op.kind != engine::OpKind::Delay)
            return "something other than a delay was given samples to hold: " + describe(op.key);
        if (layout.latency.delaySamples[i] < 0)
            return "a delay was asked to hold a negative number of samples: " + describe(op.key);

        // A fade's two sides are deliberately not aligned: where they differ,
        // the executor refuses the ramp rather than fading in from a delay
        // line that starts flushed. That is asserted through the ramp count.
        if (op.kind == engine::OpKind::Crossfade)
            continue;

        std::optional<int> arriving;
        auto connected = 0;
        auto allThroughDelays = true;
        auto smallestDelay = std::numeric_limits<int>::max();

        for (const auto& input : op.inputs) {
            if (!input.valid())
                continue;

            ++connected;
            const auto latency = latencyAt(layout, input);
            if (arriving.has_value() && *arriving != latency)
                return "the inputs of " + describe(op.key) + " arrive at " +
                       std::to_string(*arriving) + " and " + std::to_string(latency) + " samples";
            arriving = latency;

            const auto producer = static_cast<std::size_t>(input.op);
            if (plan.ops[producer].kind != engine::OpKind::Delay)
                allThroughDelays = false;
            else
                smallestDelay = std::min(smallestDelay, layout.latency.delaySamples[producer]);
        }

        if (connected >= 2 && allThroughDelays && smallestDelay != 0)
            return "every path into " + describe(op.key) + " was delayed, so all of them wait " +
                   std::to_string(smallestDelay) + " samples for nothing";
    }

    auto longest = 0;
    for (const auto outputOp : plan.outputOps)
        for (const auto& input : plan.ops[static_cast<std::size_t>(outputOp)].inputs)
            if (input.valid())
                longest = std::max(longest, latencyAt(layout, input));

    if (layout.latency.outputLatency != longest)
        return "the plan reports " + std::to_string(layout.latency.outputLatency) +
               " samples of latency and its outputs are fed at " + std::to_string(longest);

    return std::nullopt;
}

/// The op that records what a track produced, which is also the point its own
/// latency is read at.
std::optional<std::size_t> captureOp(const RenderPlan& plan, TrackId trackId) {
    for (std::size_t i = 0; i < plan.ops.size(); ++i) {
        const auto& key = plan.ops[i].key;
        if (plan.ops[i].kind == engine::OpKind::Device &&
            key.segment == ChainSegment::MixerAnalysis && key.trackId == trackId)
            return i;
    }
    return std::nullopt;
}

int captureLatency(const RenderPlan& plan, const PreparedLayout& layout, TrackId trackId) {
    const auto op = captureOp(plan, trackId);
    if (!op.has_value())
        return -1;
    return latencyAt(layout, engine::PortRef{static_cast<engine::OpId>(*op), 0});
}

// --- what the executor adopted -----------------------------------------------

/// What the executor should say it took over, worked out from the two plans and
/// their layouts alone. An audio line is adopted when the op carried and the
/// line is still the same length; a MIDI line has a capacity resolved from the
/// MIDI graph as well, which is not readable from here, so those are bounded
/// rather than predicted and the bound is exact wherever a plan has none.
struct AdoptionPrediction {
    int audioDelayLines = 0;
    int midiDelayLines = 0;
    int fadeRamps = 0;
};

AdoptionPrediction predictAdoptions(const RenderPlan& before, const PreparedLayout& beforeLayout,
                                    const RenderPlan& after, const PreparedLayout& afterLayout,
                                    const PlanDiff& diff) {
    AdoptionPrediction prediction;

    for (std::size_t i = 0; i < after.ops.size(); ++i) {
        const auto& op = after.ops[i];
        const auto from = diff.carriedFrom[i];

        if (op.kind == engine::OpKind::Delay) {
            const auto samples = afterLayout.latency.delaySamples[i];
            if (samples <= 0)
                continue;

            const auto isMidi = op.outputs.front() == engine::SignalKind::Midi;
            if (isMidi)
                ++prediction.midiDelayLines;

            if (from == engine::INVALID_OP_ID)
                continue;
            if (beforeLayout.latency.delaySamples[static_cast<std::size_t>(from)] != samples)
                continue;

            if (!isMidi)
                ++prediction.audioDelayLines;
            continue;
        }

        if (op.kind != engine::OpKind::Crossfade || from == engine::INVALID_OP_ID)
            continue;

        // A fade whose sides do not arrive together never had a ramp, on
        // either side of the swap, so there is nothing to adopt.
        const auto sidesAgree = [](const RenderPlan& plan, const PreparedLayout& layout,
                                   std::size_t op) {
            const auto& node = plan.ops[op];
            return node.inputs.size() > 1 && node.inputs[0].valid() && node.inputs[1].valid() &&
                   latencyAt(layout, node.inputs[0]) == latencyAt(layout, node.inputs[1]);
        };

        if (sidesAgree(after, afterLayout, i) &&
            before.ops[static_cast<std::size_t>(from)].kind == engine::OpKind::Crossfade &&
            sidesAgree(before, beforeLayout, static_cast<std::size_t>(from)))
            ++prediction.fadeRamps;
    }

    return prediction;
}

// --- the plan properties -----------------------------------------------------

/// The whole of what the store owes, as three set relations.
///
/// A list of what it destroyed answers only the first of them. The second is
/// what says it let go of anything at all: an instance neither keep-set names
/// and that was never destroyed is a leak, and a leak looks exactly like a
/// careful store from inside a list of destructions.
std::optional<std::string> checkRetirementLedger(const Published& published,
                                                 const Project& project) {
    const auto& model = published.modelDevices;

    std::set<engine::DeviceKey> playing;
    for (const auto& op : published.faded.plan.ops)
        if (op.kind == engine::OpKind::Device)
            playing.insert(op.key.deviceKey());

    // Nothing reachable was destroyed.
    for (const auto& key : published.destroyed) {
        if (model.contains(key))
            return "an instance the model still holds was destroyed: " + engine::toString(key);
        if (playing.contains(key))
            return "an instance the live plan still plays was destroyed: " + engine::toString(key);
    }

    // Nothing unreachable was kept.
    for (const auto& key : published.owned)
        if (!model.contains(key) && !playing.contains(key))
            return "an instance nothing names is still owned: " + engine::toString(key);

    // And everything that plays is there to play.
    for (const auto& key : playing)
        if (!published.owned.contains(key))
            return "the live plan plays an instance the store does not own: " +
                   engine::toString(key);

    // The keep-set the store was handed is the model's devices, and this is the
    // other reading of the same walk. A disagreement here is not a retirement
    // bug, but it would make every line above an assertion about the wrong set.
    if (const auto walked = edits::deviceKeys(project); walked != model)
        return "the engine's walk of the model and the harness's do not agree on which "
               "devices exist";

    return std::nullopt;
}

std::optional<std::string> planProperties(const std::vector<Edit>& edits, Report* report) {
    Harness harness(Material::Constant);
    auto project = edits::startingProject();

    auto published = harness.publish(project);
    if (!published.failure.empty())
        return "step 0: " + published.failure;
    if (const auto problem = checkAlignment(published.faded.plan, published.layout))
        return "step 0: " + *problem;
    if (const auto problem = checkRetirementLedger(published, project))
        return "step 0: " + *problem;

    for (std::size_t step = 0; step < edits.size(); ++step) {
        // Read before the edit lands, because that is where the object it
        // names still is: an edit that removes a device names no track at all
        // once the device is gone.
        const auto beforeTracks = project;
        const auto touchedTracks = edits::touched(edits[step], project);

        if (!edits::apply(edits[step], project))
            continue;

        const auto before = published;
        published = harness.publish(project);
        const auto where =
            "step " + std::to_string(step + 1) + " (" + edits::toString(edits[step]) + "): ";

        if (!published.failure.empty())
            return where + published.failure;

        if (const auto problem =
                checkCarry(before.faded.plan, published.faded.plan, published.diff))
            return where + *problem;

        if (const auto problem = checkAlignment(published.faded.plan, published.layout))
            return where + *problem;

        if (published.reportedLatency != published.layout.latency.outputLatency)
            return where + "the executor reports " + std::to_string(published.reportedLatency) +
                   " samples of latency and the layout resolves " +
                   std::to_string(published.layout.latency.outputLatency);

        const auto prediction =
            predictAdoptions(before.faded.plan, before.layout, published.faded.plan,
                             published.layout, published.diff);

        if (published.carriedDelayLines < prediction.audioDelayLines ||
            published.carriedDelayLines > prediction.audioDelayLines + prediction.midiDelayLines)
            return where + "the executor adopted " + std::to_string(published.carriedDelayLines) +
                   " delay lines and the plans say " + std::to_string(prediction.audioDelayLines) +
                   " audio ones, of " + std::to_string(prediction.midiDelayLines) +
                   " MIDI lines it could also adopt";

        if (published.carriedCrossfades != prediction.fadeRamps)
            return where + "the executor adopted " + std::to_string(published.carriedCrossfades) +
                   " fade ramps and the plans say " + std::to_string(prediction.fadeRamps);

        if (const auto problem = checkRetirementLedger(published, project))
            return where + *problem;

        if (harness.ledger().lastDestroyingThread != std::thread::id{} &&
            harness.ledger().lastDestroyingThread != std::this_thread::get_id())
            return where + "an instance was destroyed on a thread that does not publish";

        // A track no edit reached keeps the latency it reported. An edit that
        // moves one branch and silently moves another is the failure this is
        // the only assertion of.
        for (int i = 0; i < edits::kNumBaseTracks; ++i) {
            const auto trackId = edits::kFirstBaseTrack + i;
            const auto reaching = edits::feeds(beforeTracks, trackId);

            if (std::ranges::any_of(touchedTracks, [&reaching](TrackId touchedTrack) {
                    return reaching.contains(touchedTrack);
                }))
                continue;

            const auto was = captureLatency(before.faded.plan, before.layout, trackId);
            const auto now = captureLatency(published.faded.plan, published.layout, trackId);
            if (was != now)
                return where + "track " + std::to_string(trackId) +
                       " was not touched and its latency moved from " + std::to_string(was) +
                       " to " + std::to_string(now);
        }

        if (report != nullptr) {
            ++report->publishes;
            report->carried += published.diff.carried;
            report->retired += static_cast<std::int64_t>(published.diff.retired.size());
            report->fadesInserted += published.faded.inserted;
            report->fadesUnfaded += published.faded.unfaded;
            report->delayLinesCarried += published.carriedDelayLines;
            report->fadeRampsCarried += published.carriedCrossfades;
            report->instancesDestroyed += static_cast<int>(published.destroyed.size());
        }
    }

    return std::nullopt;
}

// --- rendering ---------------------------------------------------------------

/// One run of a sequence. Every step renders the same number of blocks whether
/// or not its edit was applied, so two runs of one sequence cover the same
/// stretch of timeline and can be compared sample for sample.
struct Run {
    std::string failure;
    std::map<TrackId, std::vector<float>> captures;
    std::vector<Published> steps;
};

Run renderRun(const std::vector<Edit>& edits, const std::vector<char>& mask, Material material,
              int blocksPerStep) {
    Run run;
    Harness harness(material);
    auto project = edits::startingProject();

    const auto publish = [&](std::size_t step) {
        auto published = harness.publish(project);
        if (!published.failure.empty())
            run.failure = "step " + std::to_string(step) + ": " + published.failure;

        run.steps.push_back(std::move(published));
    };

    publish(0);
    if (!run.failure.empty())
        return run;
    harness.render(blocksPerStep);

    for (std::size_t step = 0; step < edits.size(); ++step) {
        if (mask[step] != 0 && edits::apply(edits[step], project)) {
            publish(step + 1);
            if (!run.failure.empty())
                return run;
        } else {
            run.steps.push_back(Published{});
        }

        harness.render(blocksPerStep);
    }

    for (int i = 0; i < edits::kNumBaseTracks; ++i) {
        const auto trackId = edits::kFirstBaseTrack + i;
        run.captures[trackId] = harness.capture(trackId);
    }

    return run;
}

/// Which edits of a sequence can reach @p target.
///
/// An edit is relevant when it touches a track that ever fed the target, over
/// every state the whole sequence passes through. A union rather than the state
/// at the time, because an edit that removes a send is the edit that stops a
/// track feeding the target, and it is plainly relevant to it.
///
/// Then a fixed point over that, because relevance travels along the other end
/// of an edit as well: a route from the target's source to a third track is
/// relevant, and it does nothing in a run where the edit that made that third
/// track was dropped. What a kept edit names has to be kept too, or the two
/// runs differ in what the kept edits did rather than in what was dropped.
std::vector<char> relevantTo(const std::vector<Edit>& edits, TrackId target) {
    auto project = edits::startingProject();
    auto reaching = edits::feeds(project, target);

    std::vector<std::set<TrackId>> touchedAt;
    touchedAt.reserve(edits.size());

    for (const auto& edit : edits) {
        touchedAt.push_back(edits::touched(edit, project));
        edits::apply(edit, project);
        for (const auto trackId : edits::feeds(project, target))
            reaching.insert(trackId);
    }

    std::vector<char> mask(edits.size(), 0);
    for (auto grew = true; grew;) {
        grew = false;

        for (std::size_t step = 0; step < edits.size(); ++step) {
            if (mask[step] != 0)
                continue;
            if (std::ranges::none_of(touchedAt[step], [&reaching](TrackId trackId) {
                    return reaching.contains(trackId);
                }))
                continue;

            mask[step] = 1;
            grew = true;
            reaching.insert(touchedAt[step].begin(), touchedAt[step].end());
        }
    }

    return mask;
}

std::optional<std::string> compareCaptures(TrackId trackId, const std::vector<float>& whole,
                                           const std::vector<float>& without, Report* report) {
    if (whole.size() != without.size())
        return "track " + std::to_string(trackId) + " rendered " + std::to_string(whole.size()) +
               " samples with the unrelated edits and " + std::to_string(without.size()) +
               " without them";

    for (std::size_t sample = 0; sample < whole.size(); ++sample)
        if (whole[sample] != without[sample])
            return "track " + std::to_string(trackId) +
                   " heard an edit that cannot reach it, at "
                   "sample " +
                   std::to_string(sample) + ": " + std::to_string(whole[sample]) + " with it and " +
                   std::to_string(without[sample]) + " without";

    if (report != nullptr)
        report->samplesCompared += static_cast<std::int64_t>(whole.size());

    return std::nullopt;
}

std::optional<std::string> nullPropertyWith(const std::vector<Edit>& edits, Report* report,
                                            int blocksPerStep) {
    const std::vector<char> everything(edits.size(), 1);
    const auto whole = renderRun(edits, everything, Material::Ramp, blocksPerStep);
    if (!whole.failure.empty())
        return whole.failure;

    if (report != nullptr)
        for (const auto& step : whole.steps) {
            if (step.faded.plan.ops.empty())
                continue;
            ++report->publishes;
            report->carried += step.diff.carried;
            report->retired += static_cast<std::int64_t>(step.diff.retired.size());
            report->fadesInserted += step.faded.inserted;
            report->fadesUnfaded += step.faded.unfaded;
            report->delayLinesCarried += step.carriedDelayLines;
            report->fadeRampsCarried += step.carriedCrossfades;
            report->instancesDestroyed += static_cast<int>(step.destroyed.size());
        }

    for (int i = 0; i < edits::kNumBaseTracks; ++i) {
        const auto trackId = edits::kFirstBaseTrack + i;
        const auto mask = relevantTo(edits, trackId);

        // Nothing was dropped, so there is nothing this comparison could say.
        if (std::ranges::all_of(mask, [](char kept) { return kept != 0; }))
            continue;

        const auto without = renderRun(edits, mask, Material::Ramp, blocksPerStep);
        if (!without.failure.empty())
            return without.failure;

        if (const auto problem = compareCaptures(trackId, whole.captures.at(trackId),
                                                 without.captures.at(trackId), report))
            return *problem;
    }

    return std::nullopt;
}

std::optional<std::string> nullProperty(const std::vector<Edit>& edits, Report* report) {
    return nullPropertyWith(edits, report, kBlocksPerStep);
}

/// The same assertion with the edits arriving faster than a fade can finish, so
/// every swap lands on an edge that is still ramping. What is being carried
/// across the swap is then a running fade rather than a settled plan, and an
/// unrelated track still has to hear none of it.
std::optional<std::string> nullMidFadeProperty(const std::vector<Edit>& edits, Report* report) {
    return nullPropertyWith(edits, report, kBlocksMidFade);
}

// --- the changed track -------------------------------------------------------

/// Whether anything feeding this track starts from silence after the swap: a
/// delay line that is new or a different length, or a device that has only just
/// been made and holds its own latency in samples it has never seen. Either one
/// is a legitimate reason for the signal to step, and both are readable from
/// the plans rather than guessed at.
///
/// Over everything upstream rather than over the track alone. A plugin that
/// reports new latency rebuilds the compensation around it, and a track two
/// sends downstream of that hears the rebuilt line start empty exactly as the
/// track it happened on does.
bool pathStartsFlushed(const std::set<TrackId>& upstream, const Published& before,
                       const Published& now) {
    for (std::size_t i = 0; i < now.faded.plan.ops.size(); ++i) {
        const auto& op = now.faded.plan.ops[i];
        if (!upstream.contains(op.key.trackId))
            continue;

        const auto from =
            now.diff.carriedFrom.empty() ? engine::INVALID_OP_ID : now.diff.carriedFrom[i];

        if (op.kind == engine::OpKind::Delay) {
            const auto samples = now.layout.latency.delaySamples[i];
            if (samples <= 0)
                continue;
            if (from == engine::INVALID_OP_ID ||
                before.layout.latency.delaySamples[static_cast<std::size_t>(from)] != samples)
                return true;
            continue;
        }

        if (op.kind != engine::OpKind::Device)
            continue;

        const auto latency =
            latencyAt(now.layout, engine::PortRef{static_cast<engine::OpId>(i), 0});
        const auto inputLatency =
            op.inputs.front().valid() ? latencyAt(now.layout, op.inputs.front()) : 0;
        if (latency > inputLatency && from == engine::INVALID_OP_ID)
            return true;
    }

    return false;
}

constexpr float kSteadyTolerance = 1.0e-6f;

/// Far above anything the material can reach through a graph whose every gain
/// is below unity, and far below nothing at all. This is a runaway detector
/// rather than a level check: what a swap leaves the level at is asserted by
/// convergence, and where the level goes on the way there is not a number this
/// can know. A fade sits on the edge the edit moved, which is usually inside
/// the chain, and what the rest of the chain then does to a mixture of two
/// internal signals is not bounded by the two steady states either side of it.
constexpr float kRunawayLevel = 64.0f;

/// How much of a fade may arrive in one sample. A fade is a straight line over
/// fadeSamples, so one sample moves a fadeSamples-th of the distance it covers;
/// fades cascaded along a path compound that, and eight is far more of them
/// than one path ever carries. What this separates is a ramp from a step, and a
/// step moves the whole distance between two samples.
constexpr int kMaxFadeCascade = 8;

std::optional<std::string> checkStepBounds(TrackId trackId, const std::vector<float>& window,
                                           float before, bool faded, bool flushed, Report* report) {
    if (window.empty())
        return std::nullopt;

    for (std::size_t sample = 0; sample < window.size(); ++sample) {
        const auto value = window[sample];
        if (!std::isfinite(value))
            return "track " + std::to_string(trackId) + " rendered something that is not a number";
        if (std::abs(value) > kRunawayLevel)
            return "track " + std::to_string(trackId) + " ran away at sample " +
                   std::to_string(sample) + ": " + std::to_string(value);
    }

    // A path that starts from silence steps by whatever it was carrying, and
    // an edge the pass refused steps by whatever it had left: both are declared
    // divergences rather than failures, and both are counted where they happen.
    if (!faded || flushed) {
        if (report != nullptr)
            ++report->transientsExempt;
        return std::nullopt;
    }

    if (report != nullptr)
        ++report->transientsBounded;

    // Measured against the distance the transient itself covers, so that no
    // level has to be known: a ramp spreads that distance over the fade and a
    // step arrives with all of it at once, whatever the two ends happen to be.
    auto low = before;
    auto high = before;
    for (const auto value : window) {
        low = std::min(low, value);
        high = std::max(high, value);
    }

    const auto allowance = ((high - low) * static_cast<float>(kMaxFadeCascade) /
                            static_cast<float>(edits::fadeSamples())) +
                           kSteadyTolerance;

    auto previous = before;
    for (std::size_t sample = 0; sample < window.size(); ++sample) {
        if (std::abs(window[sample] - previous) > allowance)
            return "track " + std::to_string(trackId) + " stepped at sample " +
                   std::to_string(sample) + ": " + std::to_string(previous) + " to " +
                   std::to_string(window[sample]) + " covers " +
                   std::to_string(std::abs(window[sample] - previous) / (high - low)) +
                   " of the transient in one sample, and every edge that moved was faded";
        previous = window[sample];
    }

    return std::nullopt;
}

/// The changed track, and where a swap leaves it. Rendered with a constant per
/// track, because a bound is measured against a steady state and a ramp has
/// none.
std::optional<std::string> transientProperty(const std::vector<Edit>& edits, Report* report) {
    Harness harness(Material::Constant);
    auto project = edits::startingProject();

    auto published = harness.publish(project);
    if (!published.failure.empty())
        return "step 0: " + published.failure;
    harness.render(kBlocksPerStep);

    std::map<TrackId, std::size_t> read;
    std::map<TrackId, float> steady;
    for (int i = 0; i < edits::kNumBaseTracks; ++i) {
        const auto trackId = edits::kFirstBaseTrack + i;
        const auto samples = harness.capture(trackId);
        read[trackId] = samples.size();
        steady[trackId] = samples.empty() ? 0.0f : samples.back();
    }

    for (std::size_t step = 0; step < edits.size(); ++step) {
        const auto beforeTracks = project;
        if (!edits::apply(edits[step], project))
            continue;

        const auto before = published;
        published = harness.publish(project);
        const auto where =
            "step " + std::to_string(step + 1) + " (" + edits::toString(edits[step]) + "): ";
        if (!published.failure.empty())
            return where + published.failure;

        // Every steady state below is read at the end of this window, so the
        // window has to be longer than everything that is still moving inside
        // it. Asserted rather than assumed: a project that outgrew it would
        // turn every reading into one taken mid-transient, and the bounds
        // would go on passing.
        const auto needed =
            published.layout.latency.outputLatency + edits::fadeSamples() + edits::kBlockSize;
        if (needed > kBlocksPerStep * edits::kBlockSize)
            return where + "the project needs " + std::to_string(needed) +
                   " samples to settle and the window is " +
                   std::to_string(kBlocksPerStep * edits::kBlockSize);

        harness.render(kBlocksPerStep);

        // A session that has just opened this project. What it renders once
        // everything has filled is what carrying state may not change: state
        // decides how the signal got there, never where it arrives.
        Harness fresh(Material::Constant);
        const auto freshPublish = fresh.publish(project);
        if (!freshPublish.failure.empty())
            return where + "on a session that had just started, " + freshPublish.failure;
        fresh.render(kBlocksPerStep);

        for (int i = 0; i < edits::kNumBaseTracks; ++i) {
            const auto trackId = edits::kFirstBaseTrack + i;
            const auto samples = harness.capture(trackId);

            std::vector<float> window(samples.begin() + static_cast<std::ptrdiff_t>(read[trackId]),
                                      samples.end());
            read[trackId] = samples.size();

            // Both sides of the edit: a route that has just moved fed this
            // track a moment ago, and whatever it left flushed is still what
            // this track is hearing.
            auto upstream = edits::feeds(beforeTracks, trackId);
            upstream.merge(edits::feeds(project, trackId));

            if (const auto problem =
                    checkStepBounds(trackId, window, steady[trackId], published.faded.unfaded == 0,
                                    pathStartsFlushed(upstream, before, published), report))
                return where + *problem;

            if (!window.empty())
                steady[trackId] = window.back();

            const auto restarted = fresh.capture(trackId);
            if (restarted.empty() || window.empty())
                continue;

            if (window.back() != restarted.back())
                return where + "track " + std::to_string(trackId) + " settled at " +
                       std::to_string(window.back()) +
                       " and a session that had just started settles at " +
                       std::to_string(restarted.back());

            if (report != nullptr)
                ++report->steadyStatesCompared;
        }

        if (report != nullptr) {
            ++report->publishes;
            report->carried += published.diff.carried;
            report->retired += static_cast<std::int64_t>(published.diff.retired.size());
            report->fadesInserted += published.faded.inserted;
            report->fadesUnfaded += published.faded.unfaded;
            report->delayLinesCarried += published.carriedDelayLines;
            report->fadeRampsCarried += published.carriedCrossfades;
            report->instancesDestroyed += static_cast<int>(published.destroyed.size());
        }
    }

    return std::nullopt;
}

// --- shrinking ---------------------------------------------------------------

constexpr int kMaxShrinkAttempts = 600;

/// Delta debugging, chunks first and then single edits. A sequence is a vector
/// of edits that name their targets by id, so any subsequence of it runs; that
/// is what makes deletion the whole of the shrinking strategy.
std::vector<Edit> shrink(std::vector<Edit> failing, const Predicate& property) {
    auto attempts = 0;
    auto granularity = std::max<std::size_t>(failing.size() / 2, 1);

    while (attempts < kMaxShrinkAttempts) {
        auto removedAny = false;

        for (std::size_t start = 0; start + granularity <= failing.size();) {
            if (attempts++ >= kMaxShrinkAttempts)
                break;

            auto candidate = failing;
            candidate.erase(candidate.begin() + static_cast<std::ptrdiff_t>(start),
                            candidate.begin() + static_cast<std::ptrdiff_t>(start + granularity));

            if (property(candidate).has_value()) {
                failing = std::move(candidate);
                removedAny = true;
            } else {
                ++start;
            }
        }

        if (granularity == 1) {
            if (!removedAny)
                break;
        } else {
            granularity = std::max<std::size_t>(granularity / 2, 1);
        }
    }

    return failing;
}

/// Everything a failure has to say to be worth acting on: what broke, the seed
/// that found it, and the shortest sequence that still breaks it.
std::string reportFailure(const char* name, std::uint64_t seed, const std::vector<Edit>& edits,
                          const Predicate& property) {
    const auto minimal = shrink(edits, property);
    const auto failure = property(minimal);

    std::ostringstream out;
    out << name << " failed\n";
    out << "  seed " << seed << ", " << edits.size() << " edits shrunk to " << minimal.size()
        << "\n";
    out << "  " << (failure.has_value() ? *failure : std::string("(it stopped failing)")) << "\n";
    out << edits::toString(minimal);
    out << "  add " << seed << " to kRecordedSeeds in this file\n";
    return out.str();
}

/// How far the deep sweep goes, overridable so a hunt can be widened without a
/// rebuild. Anything unset or nonsense takes the default.
int fromEnvironment(const char* name, int fallback) {
    const auto* value = std::getenv(name);
    if (value == nullptr)
        return fallback;

    const auto parsed = std::atoi(value);
    return parsed > 0 ? parsed : fallback;
}

/// Enough failures to see the shape of a regression, and not so many that the
/// first one scrolls off the top. A systemic break fails every seed, and four
/// thousand shrunk sequences is not four thousand times as useful as five.
constexpr int kMaxReportedFailures = 5;

Report sweep(const char* name, int seeds, const Property& property, int length = kSequenceLength) {
    Report report;
    const Predicate predicate = [&property](const std::vector<Edit>& edits) {
        return property(edits, nullptr);
    };

    std::vector<std::uint64_t> seedList;
    seedList.reserve(static_cast<std::size_t>(seeds) + kRecordedSeeds.size());
    for (int i = 0; i < seeds; ++i)
        seedList.push_back(static_cast<std::uint64_t>(i) + 1);
    for (const auto seed : kRecordedSeeds)
        seedList.push_back(seed);

    auto failures = 0;
    for (const auto seed : seedList) {
        const auto edits = edits::generate(seed, length);
        ++report.sequences;

        if (!property(edits, &report).has_value())
            continue;

        FAIL_CHECK(reportFailure(name, seed, edits, predicate));
        if (++failures >= kMaxReportedFailures) {
            FAIL_CHECK(std::string(name) + ": stopped after " +
                       std::to_string(kMaxReportedFailures) + " failures, with " +
                       std::to_string(seedList.size() - report.sequences) + " seeds unswept");
            break;
        }
    }

    report.print(name);

    // Numbers that stay at zero are a property that has stopped exercising the
    // thing it is named after, which reads exactly like one that passes.
    CHECK(report.publishes > 0);
    return report;
}

}  // namespace

// --- the properties ----------------------------------------------------------

TEST_CASE("What the differ decided over random edit sequences", "[engine][plan][diff][property]") {
    const auto report = sweep("carry, retirement and alignment", kPlanSeeds, planProperties);

    CHECK(report.carried > 0);
    CHECK(report.retired > 0);
    CHECK(report.fadesInserted > 0);
    CHECK(report.delayLinesCarried > 0);
    CHECK(report.fadeRampsCarried > 0);
    CHECK(report.instancesDestroyed > 0);
}

TEST_CASE("An edit is inaudible on the tracks it cannot reach", "[engine][plan][diff][property]") {
    const auto report = sweep("null on untouched tracks", kRenderSeeds, nullProperty);

    CHECK(report.samplesCompared > 0);
    CHECK(report.delayLinesCarried > 0);
}

TEST_CASE("An edit arriving mid-fade is still inaudible where it cannot reach",
          "[engine][plan][diff][property]") {
    const auto report = sweep("null across a running fade", kRenderSeeds, nullMidFadeProperty);

    CHECK(report.samplesCompared > 0);
    CHECK(report.fadeRampsCarried > 0);
}

TEST_CASE("A swap leaves the track it changed inside its own bounds",
          "[engine][plan][diff][property]") {
    const auto report = sweep("transient bounds and convergence", kRenderSeeds, transientProperty);

    CHECK(report.fadesInserted > 0);
    CHECK(report.steadyStatesCompared > 0);

    // The bound is skipped wherever the pass refused an edge, which is most
    // steps, so the one thing that says it still exists is that it was reached.
    CHECK(report.transientsBounded > 0);
}

TEST_CASE("The differ properties, swept deeply", "[.][deep][engine][plan][diff][property]") {
    // Hidden, because it is minutes rather than seconds. This is the sweep to
    // reach for when the differ or the crossfade pass changes, and the one a
    // recorded seed is expected to come out of:
    //
    //     ./magda_tests "[deep]"
    //     MAGDA_DIFFER_SEQUENCE=60 MAGDA_DIFFER_PLAN_SEEDS=20000 ./magda_tests "[deep]"
    //
    // Longer sequences are worth more than more of them: a project fourteen
    // edits old is a project that still looks like the one it started as, and
    // the pairs that have never been looked at are further out than that.
    const auto planSeeds = fromEnvironment("MAGDA_DIFFER_PLAN_SEEDS", 4000);
    const auto renderSeeds = fromEnvironment("MAGDA_DIFFER_RENDER_SEEDS", 400);
    const auto length = fromEnvironment("MAGDA_DIFFER_SEQUENCE", 40);

    sweep("deep: carry, retirement and alignment", planSeeds, planProperties, length);
    sweep("deep: null on untouched tracks", renderSeeds, nullProperty, length);
    sweep("deep: null across a running fade", renderSeeds, nullMidFadeProperty, length);
    sweep("deep: transient bounds and convergence", renderSeeds, transientProperty, length);
}

// --- the harness itself ------------------------------------------------------
//
// A property test that cannot fail looks exactly like one that passes, and a
// generator that has drifted into producing nothing is the most likely way for
// that to happen. These are what says the sweep above is measuring something.

TEST_CASE("A generated device delays by exactly what it reports",
          "[engine][plan][diff][property]") {
    // The premise everything about latency here rests on. A device that
    // reported more than it delayed would leave the plan compensating for
    // samples the signal never spent: the alignment property would go on
    // passing, because it reads the plan, and every render under it would be
    // running on a graph misaligned in fact. Pinned at the largest latency the
    // generator can hand out, and at nothing, because both ends of the range
    // are where a ring gets its arithmetic wrong.
    for (const auto latency : {0, 1, edits::kMaxDeviceLatency / 2, edits::kMaxDeviceLatency}) {
        INFO("latency " << latency);

        edits::Ledger ledger;
        edits::TestDevice device(engine::DeviceKey{ChainSegment::Fx, 4}, 1.0f, ledger);
        device.setLatency(latency);
        REQUIRE(device.honoursItsLatency());
        REQUIRE(device.latencySamples() == latency);

        const engine::RenderContext context{edits::kSampleRate, edits::kBlockSize,
                                            edits::kNumChannels};
        device.prepare(context);

        // An impulse, and then enough silence for it to come out however far it
        // is held. Read across block boundaries, because a ring that wrapped
        // wrong is a ring that is right for the first block only.
        constexpr int kBlocks = 4;
        std::vector<float> stream;
        juce::AudioBuffer<float> buffer(edits::kNumChannels, edits::kBlockSize);

        for (int block = 0; block < kBlocks; ++block) {
            buffer.clear();
            if (block == 0)
                buffer.setSample(0, 0, 1.0f);

            engine::DeviceBlock deviceBlock;
            deviceBlock.block.numSamples = edits::kBlockSize;
            deviceBlock.audio = juce::dsp::AudioBlock<float>(buffer);
            device.process(deviceBlock);

            for (int sample = 0; sample < edits::kBlockSize; ++sample)
                stream.push_back(buffer.getSample(0, sample));
        }

        for (std::size_t sample = 0; sample < stream.size(); ++sample) {
            INFO("sample " << sample);
            REQUIRE(stream[sample] == (sample == static_cast<std::size_t>(latency) ? 1.0f : 0.0f));
        }
    }
}

TEST_CASE("The generator never asks for a latency a device cannot honour",
          "[engine][plan][diff][property]") {
    // The other end of the same claim, and the one that catches the two
    // constants drifting apart again: whatever the generator writes into an
    // edit has to be a number the thing playing it can delay by.
    for (int seed = 1; seed <= 64; ++seed)
        for (const auto& edit :
             edits::generate(static_cast<std::uint64_t>(seed), kSequenceLength)) {
            INFO(edits::toString(edit));
            REQUIRE(edit.latencySamples >= 0);
            REQUIRE(edit.latencySamples <= edits::kMaxDeviceLatency);
        }
}

TEST_CASE("The generator covers its own vocabulary", "[engine][plan][diff][property]") {
    std::set<int> kinds;
    auto applied = 0;
    auto total = 0;

    for (int i = 0; i < 64; ++i) {
        const auto edits = edits::generate(static_cast<std::uint64_t>(i) + 1, kSequenceLength);
        auto project = edits::startingProject();

        for (const auto& edit : edits) {
            kinds.insert(static_cast<int>(edit.kind));
            ++total;
            applied += edits::apply(edit, project) ? 1 : 0;
        }
    }

    // Every kind of edit, and most of them landing rather than missing: a
    // generator that mostly names things the project does not have would run
    // the same three plans over and over.
    CHECK(kinds.size() == static_cast<std::size_t>(edits::kNumEditKinds));
    CHECK(applied * 2 > total);
}

TEST_CASE("A sequence that fails shrinks to the edits that made it fail",
          "[engine][plan][diff][property]") {
    const auto edits = edits::generate(1, kSequenceLength);
    REQUIRE(edits.size() > 2);

    // Fails on nothing but the presence of one particular edit, so the minimal
    // failing sequence is known: exactly that edit, and nothing else.
    const auto marker = edits[edits.size() / 2];
    const Predicate property = [&marker](const std::vector<Edit>& candidate) {
        return std::ranges::any_of(candidate,
                                   [&marker](const Edit& edit) {
                                       return edits::toString(edit) == edits::toString(marker);
                                   })
                   ? std::optional<std::string>("the marker is here")
                   : std::nullopt;
    };

    const auto minimal = shrink(edits, property);
    REQUIRE(minimal.size() == 1);
    CHECK(edits::toString(minimal.front()) == edits::toString(marker));
}

TEST_CASE("Dropping an edit a track can hear does change what it renders",
          "[engine][plan][diff][property]") {
    // The other half of the null property, and the one that says it is looking
    // at anything: dropping the edits that can reach a track has to change what
    // that track renders. Without this, a null on the untouched tracks would
    // also pass if the harness were recording silence.
    auto differed = 0;

    for (int seed = 1; seed <= 8 && differed == 0; ++seed) {
        const auto edits = edits::generate(static_cast<std::uint64_t>(seed), kSequenceLength);
        const std::vector<char> everything(edits.size(), 1);
        const auto whole = renderRun(edits, everything, Material::Ramp, kBlocksPerStep);
        REQUIRE(whole.failure.empty());

        for (int i = 0; i < edits::kNumBaseTracks; ++i) {
            const auto trackId = edits::kFirstBaseTrack + i;

            // The complement of the null property's mask: everything that
            // could reach this track, and nothing else.
            const auto relevant = relevantTo(edits, trackId);
            std::vector<char> unrelatedOnly(edits.size(), 0);
            for (std::size_t step = 0; step < edits.size(); ++step)
                unrelatedOnly[step] = relevant[step] != 0 ? 0 : 1;

            if (std::ranges::none_of(relevant, [](char kept) { return kept != 0; }))
                continue;

            const auto without = renderRun(edits, unrelatedOnly, Material::Ramp, kBlocksPerStep);
            REQUIRE(without.failure.empty());

            if (whole.captures.at(trackId) != without.captures.at(trackId))
                ++differed;
        }
    }

    CHECK(differed > 0);
}
