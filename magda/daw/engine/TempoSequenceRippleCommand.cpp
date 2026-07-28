#include "TempoSequenceRippleCommand.hpp"

#include "TracktionEngineWrapper.hpp"

namespace magda {

namespace te = tracktion;

TempoSequenceRippleCommand::TempoSequenceRippleCommand(te::Edit& edit, Mode mode, double startBeat,
                                                       double endBeat)
    : edit_(edit), mode_(mode), startBeat_(startBeat), endBeat_(endBeat) {}

TempoSequenceRippleCommand::Snapshot TempoSequenceRippleCommand::readSequences() const {
    Snapshot s;
    auto& ts = edit_.tempoSequence;

    for (int i = 0; i < ts.getNumTempos(); ++i)
        if (auto* t = ts.getTempo(i))
            s.tempos.push_back({t->getStartBeat().inBeats(), t->getBpm(), t->getCurve()});

    for (int i = 0; i < ts.getNumTimeSigs(); ++i)
        if (auto* t = ts.getTimeSig(i))
            s.timeSigs.push_back({t->getStartBeat().inBeats(), t->numerator.get(),
                                  t->denominator.get(), t->triplets.get()});

    auto& ps = edit_.pitchSequence;
    for (int i = 0; i < ps.getNumPitches(); ++i)
        if (auto* p = ps.getPitch(i))
            s.pitches.push_back({p->getStartBeatNumber().inBeats(), p->getPitch()});

    return s;
}

TempoSequenceRippleCommand::Snapshot TempoSequenceRippleCommand::rippled(const Snapshot& in) const {
    Snapshot out;
    out.tempos = temporipple::rippleEvents(in.tempos, mode_, startBeat_, endBeat_);
    out.timeSigs = temporipple::rippleEvents(in.timeSigs, mode_, startBeat_, endBeat_);
    out.pitches = temporipple::rippleEvents(in.pitches, mode_, startBeat_, endBeat_);
    return out;
}

void TempoSequenceRippleCommand::applySequences(const Snapshot& s) {
    auto& ts = edit_.tempoSequence;
    auto& ps = edit_.pitchSequence;

    // Clips and automation are beat-native (Clip::syncType == syncBarsBeats):
    // TE only re-anchors them across a tempo change when the change runs through
    // its remapper. The mutators below pass remapEdit=false, so snapshot every
    // clip/automation beat up front and remap once after the rebuild. Without
    // it, clips stay pinned to stale wall-clock seconds and drift off the grid.
    te::EditTimecodeRemapperSnapshot snap;
    snap.savePreChangeState(edit_);

    // --- Tempo settings (rebuild in place; keep the beat-0 anchor) ---
    for (int i = ts.getNumTempos() - 1; i >= 1; --i)
        ts.removeTempo(i, false);
    if (!s.tempos.empty()) {
        if (auto* t0 = ts.getTempo(0))
            t0->set(te::BeatPosition(), s.tempos[0].bpm, s.tempos[0].curve, false);
        for (size_t i = 1; i < s.tempos.size(); ++i)
            ts.insertTempo(te::BeatPosition::fromBeats(s.tempos[i].beat), s.tempos[i].bpm,
                           s.tempos[i].curve);
    }

    // --- Time signatures ---
    for (int i = ts.getNumTimeSigs() - 1; i >= 1; --i)
        ts.removeTimeSig(i);
    if (!s.timeSigs.empty()) {
        if (auto* g0 = ts.getTimeSig(0)) {
            g0->startBeatNumber = te::BeatPosition();
            g0->numerator = s.timeSigs[0].numerator;
            g0->denominator = s.timeSigs[0].denominator;
            g0->triplets = s.timeSigs[0].triplets;
        }
        for (size_t i = 1; i < s.timeSigs.size(); ++i) {
            if (auto g = ts.insertTimeSig(te::BeatPosition::fromBeats(s.timeSigs[i].beat))) {
                g->numerator = s.timeSigs[i].numerator;
                g->denominator = s.timeSigs[i].denominator;
                g->triplets = s.timeSigs[i].triplets;
            }
        }
    }

    // --- Pitch settings (clear() leaves a single default pitch at beat 0) ---
    ps.clear();
    if (!s.pitches.empty()) {
        if (auto* p0 = ps.getPitch(0)) {
            p0->setStartBeat(te::BeatPosition());
            p0->setPitch(s.pitches[0].pitch);
        }
        for (size_t i = 1; i < s.pitches.size(); ++i)
            ps.insertPitch(te::BeatPosition::fromBeats(s.pitches[i].beat), s.pitches[i].pitch);
    }

    // Reposition every clip / automation point to its saved bar/beat under the
    // new tempo map. This is what makes clips follow the rippled tempo.
    snap.remapEdit(edit_);
}

void TempoSequenceRippleCommand::execute() {
    before_ = readSequences();
    const Snapshot after = rippled(before_);
    changed_ = !(temporipple::sameBeats(before_.tempos, after.tempos) &&
                 temporipple::sameBeats(before_.timeSigs, after.timeSigs) &&
                 temporipple::sameBeats(before_.pitches, after.pitches));
    if (changed_)
        applySequences(after);
}

void TempoSequenceRippleCommand::undo() {
    if (changed_)
        applySequences(before_);
}

juce::String TempoSequenceRippleCommand::getDescription() const {
    return "Ripple Tempo Sequence";
}

std::unique_ptr<UndoableCommand> TracktionEngineWrapper::createTempoSequenceRippleCommand(
    TempoSequenceRippleMode mode, BeatPosition start, BeatPosition end) {
    if (!currentEdit_)
        return nullptr;
    TempoSequenceRippleCommand::Mode commandMode;
    switch (mode) {
        case TempoSequenceRippleMode::Insert:
            commandMode = TempoSequenceRippleCommand::Mode::Insert;
            break;
        case TempoSequenceRippleMode::Delete:
            commandMode = TempoSequenceRippleCommand::Mode::Delete;
            break;
        case TempoSequenceRippleMode::Duplicate:
            commandMode = TempoSequenceRippleCommand::Mode::Duplicate;
            break;
    }
    return std::make_unique<TempoSequenceRippleCommand>(*currentEdit_, commandMode, start.value,
                                                        end.value);
}

}  // namespace magda
