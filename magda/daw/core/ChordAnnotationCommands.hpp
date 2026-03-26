#pragma once

#include "ClipInfo.hpp"
#include "ClipManager.hpp"
#include "UndoManager.hpp"

namespace magda {

class AddChordAnnotationCommand : public UndoableCommand {
  public:
    AddChordAnnotationCommand(ClipId clipId, ClipInfo::ChordAnnotation annotation)
        : clipId_(clipId), annotation_(std::move(annotation)) {}

    void execute() override {
        auto& cm = ClipManager::getInstance();
        if (auto* clip = cm.getClip(clipId_)) {
            insertedIndex_ = clip->chordAnnotations.size();
        }
        cm.addChordAnnotation(clipId_, annotation_);
        executed_ = true;
    }

    void undo() override {
        if (executed_) {
            ClipManager::getInstance().removeChordAnnotation(clipId_, insertedIndex_);
        }
    }

    juce::String getDescription() const override {
        return "Add Chord Annotation";
    }

  private:
    ClipId clipId_;
    ClipInfo::ChordAnnotation annotation_;
    size_t insertedIndex_ = 0;
    bool executed_ = false;
};

class RemoveChordAnnotationCommand : public UndoableCommand {
  public:
    RemoveChordAnnotationCommand(ClipId clipId, size_t index) : clipId_(clipId), index_(index) {}

    void execute() override {
        auto& cm = ClipManager::getInstance();
        if (auto* clip = cm.getClip(clipId_)) {
            if (index_ < clip->chordAnnotations.size()) {
                removedAnnotation_ = clip->chordAnnotations[index_];
                captured_ = true;
            }
        }
        cm.removeChordAnnotation(clipId_, index_);
    }

    void undo() override {
        if (captured_) {
            auto& cm = ClipManager::getInstance();
            if (auto* clip = cm.getClip(clipId_)) {
                auto pos =
                    clip->chordAnnotations.begin() +
                    static_cast<ptrdiff_t>(juce::jmin(index_, clip->chordAnnotations.size()));
                clip->chordAnnotations.insert(pos, removedAnnotation_);
                cm.forceNotifyClipPropertyChanged(clipId_);
            }
        }
    }

    juce::String getDescription() const override {
        return "Remove Chord Annotation";
    }

  private:
    ClipId clipId_;
    size_t index_;
    ClipInfo::ChordAnnotation removedAnnotation_;
    bool captured_ = false;
};

class ClearChordAnnotationsCommand : public UndoableCommand {
  public:
    explicit ClearChordAnnotationsCommand(ClipId clipId) : clipId_(clipId) {}

    void execute() override {
        auto& cm = ClipManager::getInstance();
        if (auto* clip = cm.getClip(clipId_)) {
            removedAnnotations_ = clip->chordAnnotations;
        }
        cm.clearChordAnnotations(clipId_);
    }

    void undo() override {
        auto& cm = ClipManager::getInstance();
        if (auto* clip = cm.getClip(clipId_)) {
            clip->chordAnnotations = removedAnnotations_;
            cm.forceNotifyClipPropertyChanged(clipId_);
        }
    }

    juce::String getDescription() const override {
        return "Clear Chord Annotations";
    }

  private:
    ClipId clipId_;
    std::vector<ClipInfo::ChordAnnotation> removedAnnotations_;
};

}  // namespace magda
