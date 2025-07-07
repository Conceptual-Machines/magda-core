#include "tracktion_engine_wrapper.hpp"
#include <iostream>

namespace magica {

TracktionEngineWrapper::TracktionEngineWrapper() = default;

TracktionEngineWrapper::~TracktionEngineWrapper() {
    shutdown();
}

bool TracktionEngineWrapper::initialize() {
    try {
        // Initialize Tracktion Engine
        engine_ = std::make_unique<tracktion::Engine>("MagicaDAW");
        
        // For now, just initialize without creating an edit
        // This allows the build to succeed and we can expand functionality later
        std::cout << "Tracktion Engine initialized successfully (minimal mode)" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "ERROR: Failed to initialize Tracktion Engine: " << e.what() << std::endl;
        return false;
    }
}

void TracktionEngineWrapper::shutdown() {
    if (currentEdit_) {
        currentEdit_.reset();
    }
    if (engine_) {
        engine_.reset();
    }
    std::cout << "Tracktion Engine shutdown complete" << std::endl;
}

CommandResponse TracktionEngineWrapper::processCommand(const Command& command) {
    const auto& type = command.getType();
    
    try {
        if (type == "play") {
            play();
            return CommandResponse(CommandResponse::Status::Success, "Playback started");
        }
        else if (type == "stop") {
            stop();
            return CommandResponse(CommandResponse::Status::Success, "Playback stopped");
        }
        else if (type == "createTrack") {
            // Simple parameter parsing - in a real implementation you'd parse JSON
            auto trackId = createMidiTrack("New Track");
            
            juce::DynamicObject::Ptr obj = new juce::DynamicObject();
            obj->setProperty("trackId", juce::String(trackId));
            juce::var responseData(obj.get());
            
            auto response = CommandResponse(CommandResponse::Status::Success, "Track created");
            response.setData(responseData);
            return response;
        }
        else {
            return CommandResponse(CommandResponse::Status::Error, 
                                 "Unknown command");
        }
    }
    catch (const std::exception& e) {
        return CommandResponse(CommandResponse::Status::Error, 
                             "Command execution failed: " + std::string(e.what()));
    }
}

// TransportInterface implementation
void TracktionEngineWrapper::play() {
    if (currentEdit_) {
        currentEdit_->getTransport().play(false);
        std::cout << "Playback started" << std::endl;
    }
}

void TracktionEngineWrapper::stop() {
    if (currentEdit_) {
        currentEdit_->getTransport().stop(false, false);
        std::cout << "Playback stopped" << std::endl;
    }
}

void TracktionEngineWrapper::pause() {
    stop(); // Tracktion doesn't distinguish between stop and pause
}

void TracktionEngineWrapper::record() {
    if (currentEdit_) {
        currentEdit_->getTransport().record(false);
        std::cout << "Recording started" << std::endl;
    }
}

void TracktionEngineWrapper::locate(double position_seconds) {
    if (currentEdit_) {
        currentEdit_->getTransport().setPosition(tracktion::TimePosition::fromSeconds(position_seconds));
    }
}

void TracktionEngineWrapper::locateMusical(int bar, int beat, int tick) {
    // Convert musical position to time
    if (currentEdit_) {
        auto& tempoSequence = currentEdit_->tempoSequence;
        auto beatPosition = tracktion::BeatPosition::fromBeats(bar * 4.0 + beat - 1.0 + tick / 1000.0);
        auto timePosition = tempoSequence.beatsToTime(beatPosition);
        currentEdit_->getTransport().setPosition(timePosition);
    }
}

double TracktionEngineWrapper::getCurrentPosition() const {
    if (currentEdit_) {
        return currentEdit_->getTransport().position.get().inSeconds();
    }
    return 0.0;
}

void TracktionEngineWrapper::getCurrentMusicalPosition(int& bar, int& beat, int& tick) const {
    if (currentEdit_) {
        auto position = tracktion::TimePosition::fromSeconds(getCurrentPosition());
        auto& tempoSequence = currentEdit_->tempoSequence;
        auto beatPosition = tempoSequence.timeToBeats(position);
        auto beats = beatPosition.inBeats();
        
        bar = static_cast<int>(beats / 4.0) + 1;
        beat = static_cast<int>(beats) % 4 + 1;
        tick = static_cast<int>((beats - static_cast<int>(beats)) * 1000);
    }
}

bool TracktionEngineWrapper::isPlaying() const {
    if (currentEdit_) {
        return currentEdit_->getTransport().isPlaying();
    }
    return false;
}

bool TracktionEngineWrapper::isRecording() const {
    if (currentEdit_) {
        return currentEdit_->getTransport().isRecording();
    }
    return false;
}

void TracktionEngineWrapper::setTempo(double bpm) {
    if (currentEdit_) {
        // Simplified tempo setting - just log it for now
        std::cout << "Set tempo: " << bpm << " BPM" << std::endl;
    }
}

double TracktionEngineWrapper::getTempo() const {
    if (currentEdit_) {
        auto timePos = tracktion::TimePosition::fromSeconds(0.0);
        return currentEdit_->tempoSequence.getTempoAt(timePos).getBpm();
    }
    return 120.0;
}

void TracktionEngineWrapper::setTimeSignature(int numerator, int denominator) {
    if (currentEdit_) {
        // Time signature handling in Tracktion Engine - simplified for now
        std::cout << "Set time signature: " << numerator << "/" << denominator << std::endl;
    }
}

void TracktionEngineWrapper::getTimeSignature(int& numerator, int& denominator) const {
    if (currentEdit_) {
        // Time signature handling in Tracktion Engine - simplified for now
        numerator = 4;
        denominator = 4;
    } else {
        numerator = 4;
        denominator = 4;
    }
}

void TracktionEngineWrapper::setLooping(bool enabled) {
    if (currentEdit_) {
        currentEdit_->getTransport().looping = enabled;
    }
}

void TracktionEngineWrapper::setLoopRegion(double start_seconds, double end_seconds) {
    if (currentEdit_) {
        auto startPos = tracktion::TimePosition::fromSeconds(start_seconds);
        auto endPos = tracktion::TimePosition::fromSeconds(end_seconds);
        currentEdit_->getTransport().setLoopRange(tracktion::TimeRange(startPos, endPos));
    }
}

bool TracktionEngineWrapper::isLooping() const {
    if (currentEdit_) {
        return currentEdit_->getTransport().looping;
    }
    return false;
}

// TrackInterface implementation
std::string TracktionEngineWrapper::createAudioTrack(const std::string& name) {
    if (!currentEdit_) return "";
    
    auto trackId = generateTrackId();
    auto tracks = tracktion::getAudioTracks(*currentEdit_);
    auto insertPoint = tracktion::TrackInsertPoint(nullptr, nullptr);
    auto track = currentEdit_->insertNewAudioTrack(insertPoint, nullptr);
    if (track) {
        track->setName(name);
        trackMap_[trackId] = track;
        std::cout << "Created audio track: " << name << " (ID: " << trackId << ")" << std::endl;
    }
    return trackId;
}

std::string TracktionEngineWrapper::createMidiTrack(const std::string& name) {
    if (!currentEdit_) return "";
    
    auto trackId = generateTrackId();
    auto insertPoint = tracktion::TrackInsertPoint(nullptr, nullptr);
    auto track = currentEdit_->insertNewAudioTrack(insertPoint, nullptr);
    if (track) {
        track->setName(name);
        trackMap_[trackId] = track;
        std::cout << "Created MIDI track: " << name << " (ID: " << trackId << ")" << std::endl;
    }
    return trackId;
}

void TracktionEngineWrapper::deleteTrack(const std::string& track_id) {
    auto it = trackMap_.find(track_id);
    if (it != trackMap_.end() && currentEdit_) {
        currentEdit_->deleteTrack(it->second.get());
        trackMap_.erase(it);
        std::cout << "Deleted track ID: " << track_id << std::endl;
    }
}

void TracktionEngineWrapper::setTrackName(const std::string& track_id, const std::string& name) {
    auto track = findTrackById(track_id);
    if (track) {
        track->setName(name);
    }
}

std::string TracktionEngineWrapper::getTrackName(const std::string& track_id) const {
    auto track = findTrackById(track_id);
    return track ? track->getName().toStdString() : "";
}

void TracktionEngineWrapper::setTrackMuted(const std::string& track_id, bool muted) {
    auto track = findTrackById(track_id);
    if (track) {
        track->setMute(muted);
    }
}

bool TracktionEngineWrapper::isTrackMuted(const std::string& track_id) const {
    auto track = findTrackById(track_id);
    return track ? track->isMuted(false) : false;
}

void TracktionEngineWrapper::setTrackSolo(const std::string& track_id, bool solo) {
    auto track = findTrackById(track_id);
    if (track) {
        track->setSolo(solo);
    }
}

bool TracktionEngineWrapper::isTrackSolo(const std::string& track_id) const {
    auto track = findTrackById(track_id);
    return track ? track->isSolo(false) : false;
}

void TracktionEngineWrapper::setTrackArmed(const std::string& track_id, bool armed) {
    auto track = findTrackById(track_id);
    if (track) {
        if (auto audioTrack = dynamic_cast<tracktion::AudioTrack*>(track)) {
            // Simplified - in real implementation would set input recording
            std::cout << "Set track armed (stub): " << track_id << " = " << armed << std::endl;
        }
    }
}

bool TracktionEngineWrapper::isTrackArmed(const std::string& track_id) const {
    auto track = findTrackById(track_id);
    if (track) {
        if (auto audioTrack = dynamic_cast<tracktion::AudioTrack*>(track)) {
            // Simplified - in real implementation would check input recording
            return false;
        }
    }
    return false;
}

void TracktionEngineWrapper::setTrackColor(const std::string& track_id, int r, int g, int b) {
    auto track = findTrackById(track_id);
    if (track) {
        track->setColour(juce::Colour::fromRGB(r, g, b));
    }
}

std::vector<std::string> TracktionEngineWrapper::getAllTrackIds() const {
    std::vector<std::string> ids;
    for (const auto& pair : trackMap_) {
        ids.push_back(pair.first);
    }
    return ids;
}

bool TracktionEngineWrapper::trackExists(const std::string& track_id) const {
    return trackMap_.find(track_id) != trackMap_.end();
}

// ClipInterface implementation
std::string TracktionEngineWrapper::addMidiClip(const std::string& track_id, 
                                               double start_time, 
                                               double length, 
                                               const std::vector<MidiNote>& notes) {
    // TODO: Implement MIDI clip creation with notes
    auto clipId = generateClipId();
    std::cout << "Created MIDI clip (stub): " << clipId << std::endl;
    return clipId;
}

std::string TracktionEngineWrapper::addAudioClip(const std::string& track_id,
                                                double start_time,
                                                const std::string& audio_file_path) {
    // TODO: Implement audio clip creation
    auto clipId = generateClipId();
    std::cout << "Created audio clip (stub): " << clipId << std::endl;
    return clipId;
}

void TracktionEngineWrapper::deleteClip(const std::string& clip_id) {
    // TODO: Implement clip deletion
    std::cout << "Deleted clip (stub): " << clip_id << std::endl;
}

void TracktionEngineWrapper::moveClip(const std::string& clip_id, double new_start_time) {
    // TODO: Implement clip moving
    std::cout << "Moved clip (stub): " << clip_id << " to " << new_start_time << std::endl;
}

void TracktionEngineWrapper::resizeClip(const std::string& clip_id, double new_length) {
    // TODO: Implement clip resizing
    std::cout << "Resized clip (stub): " << clip_id << " to " << new_length << std::endl;
}

double TracktionEngineWrapper::getClipStartTime(const std::string& clip_id) const {
    // TODO: Implement clip start time retrieval
    return 0.0;
}

double TracktionEngineWrapper::getClipLength(const std::string& clip_id) const {
    // TODO: Implement clip length retrieval
    return 1.0;
}

void TracktionEngineWrapper::addNoteToMidiClip(const std::string& clip_id, const MidiNote& note) {
    // TODO: Implement note addition to MIDI clip
    std::cout << "Added note to MIDI clip (stub): " << clip_id << std::endl;
}

void TracktionEngineWrapper::removeNotesFromMidiClip(const std::string& clip_id, 
                                                    double start_time, 
                                                    double end_time) {
    // TODO: Implement note removal from MIDI clip
    std::cout << "Removed notes from MIDI clip (stub): " << clip_id << std::endl;
}

std::vector<MidiNote> TracktionEngineWrapper::getMidiClipNotes(const std::string& clip_id) const {
    // TODO: Implement note retrieval from MIDI clip
    return {};
}

std::vector<std::string> TracktionEngineWrapper::getTrackClips(const std::string& track_id) const {
    // TODO: Implement track clip retrieval
    return {};
}

bool TracktionEngineWrapper::clipExists(const std::string& clip_id) const {
    return clipMap_.find(clip_id) != clipMap_.end();
}

// MixerInterface implementation - simplified with stubs
void TracktionEngineWrapper::setTrackVolume(const std::string& track_id, double volume) {
    auto track = findTrackById(track_id);
    if (track) {
        // Simplified - just log the operation
        std::cout << "Set track volume: " << track_id << " = " << volume << std::endl;
    }
}

double TracktionEngineWrapper::getTrackVolume(const std::string& track_id) const {
    auto track = findTrackById(track_id);
    if (track) {
        // Return default volume
        return 1.0;
    }
    return 1.0;
}

void TracktionEngineWrapper::setTrackPan(const std::string& track_id, double pan) {
    auto track = findTrackById(track_id);
    if (track) {
        // Simplified - just log the operation
        std::cout << "Set track pan: " << track_id << " = " << pan << std::endl;
    }
}

double TracktionEngineWrapper::getTrackPan(const std::string& track_id) const {
    auto track = findTrackById(track_id);
    if (track) {
        // Return center pan
        return 0.0;
    }
    return 0.0;
}

void TracktionEngineWrapper::setMasterVolume(double volume) {
    if (currentEdit_) {
        currentEdit_->getMasterVolumePlugin()->setVolumeDb(juce::Decibels::gainToDecibels(volume));
    }
}

double TracktionEngineWrapper::getMasterVolume() const {
    if (currentEdit_) {
        return juce::Decibels::decibelsToGain(currentEdit_->getMasterVolumePlugin()->getVolumeDb());
    }
    return 1.0;
}

std::string TracktionEngineWrapper::addEffect(const std::string& track_id, const std::string& effect_name) {
    // TODO: Implement effect addition
    auto effectId = generateEffectId();
    std::cout << "Added effect (stub): " << effect_name << " to track " << track_id << std::endl;
    return effectId;
}

void TracktionEngineWrapper::removeEffect(const std::string& effect_id) {
    // TODO: Implement effect removal
    std::cout << "Removed effect (stub): " << effect_id << std::endl;
}

void TracktionEngineWrapper::setEffectParameter(const std::string& effect_id, 
                                               const std::string& parameter_name, 
                                               double value) {
    // TODO: Implement effect parameter setting
    std::cout << "Set effect parameter (stub): " << effect_id << "." << parameter_name << " = " << value << std::endl;
}

double TracktionEngineWrapper::getEffectParameter(const std::string& effect_id, 
                                                 const std::string& parameter_name) const {
    // TODO: Implement effect parameter retrieval
    return 0.0;
}

void TracktionEngineWrapper::setEffectEnabled(const std::string& effect_id, bool enabled) {
    // TODO: Implement effect enable/disable
    std::cout << "Set effect enabled (stub): " << effect_id << " = " << enabled << std::endl;
}

bool TracktionEngineWrapper::isEffectEnabled(const std::string& effect_id) const {
    // TODO: Implement effect enabled check
    return true;
}

std::vector<std::string> TracktionEngineWrapper::getAvailableEffects() const {
    // TODO: Implement available effects retrieval
    return {"Reverb", "Delay", "EQ", "Compressor"};
}

std::vector<std::string> TracktionEngineWrapper::getTrackEffects(const std::string& track_id) const {
    // TODO: Implement track effects retrieval
    return {};
}

// Legacy methods for backward compatibility
std::string TracktionEngineWrapper::createAudioClip(const std::string& track_id, const std::string& file_path,
                                                   double start_time, double length) {
    return addAudioClip(track_id, start_time, file_path);
}

std::string TracktionEngineWrapper::createMidiClip(const std::string& track_id, double start_time, double length) {
    return addMidiClip(track_id, start_time, length, {});
}

void TracktionEngineWrapper::setClipPosition(const std::string& clip_id, double start_time) {
    moveClip(clip_id, start_time);
}

double TracktionEngineWrapper::getClipPosition(const std::string& clip_id) const {
    return getClipStartTime(clip_id);
}

void TracktionEngineWrapper::setClipLength(const std::string& clip_id, double length) {
    resizeClip(clip_id, length);
}

std::vector<std::string> TracktionEngineWrapper::getClipsInTrack(const std::string& track_id) const {
    return getTrackClips(track_id);
}

// Helper methods
tracktion::Track* TracktionEngineWrapper::findTrackById(const std::string& track_id) const {
    auto it = trackMap_.find(track_id);
    return (it != trackMap_.end()) ? it->second.get() : nullptr;
}

tracktion::Clip* TracktionEngineWrapper::findClipById(const std::string& clip_id) const {
    auto it = clipMap_.find(clip_id);
    return (it != clipMap_.end()) ? static_cast<tracktion::Clip*>(it->second.get()) : nullptr;
}

std::string TracktionEngineWrapper::generateTrackId() {
    return "track_" + std::to_string(nextTrackId_++);
}

std::string TracktionEngineWrapper::generateClipId() {
    return "clip_" + std::to_string(nextClipId_++);
}

std::string TracktionEngineWrapper::generateEffectId() {
    return "effect_" + std::to_string(nextEffectId_++);
}

} // namespace magica 