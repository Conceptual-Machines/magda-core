#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "magda/core/interfaces/transport_interface.hpp"
#include "magda/core/interfaces/track_interface.hpp"
#include "magda/core/interfaces/clip_interface.hpp"
#include "magda/core/interfaces/mixer_interface.hpp"
#include <vector>
#include <string>
#include <map>

// Mock implementation of TransportInterface for testing
class MockTransportInterface : public TransportInterface {
private:
    bool playing_ = false;
    bool recording_ = false;
    double position_ = 0.0;
    double tempo_ = 120.0;
    int time_sig_num_ = 4;
    int time_sig_den_ = 4;
    bool looping_ = false;
    
public:
    void play() override { playing_ = true; }
    void stop() override { playing_ = false; recording_ = false; }
    void pause() override { playing_ = false; }
    void record() override { recording_ = true; playing_ = true; }
    
    void locate(double position_seconds) override { position_ = position_seconds; }
    void locateMusical(int bar, int beat, int tick = 0) override {
        // Simple conversion for testing
        position_ = (bar - 1) * 4.0 + (beat - 1) + tick / 1000.0;
    }
    
    double getCurrentPosition() const override { return position_; }
    void getCurrentMusicalPosition(int& bar, int& beat, int& tick) const override {
        bar = static_cast<int>(position_ / 4.0) + 1;
        beat = static_cast<int>(position_) % 4 + 1;
        tick = static_cast<int>((position_ - static_cast<int>(position_)) * 1000);
    }
    
    bool isPlaying() const override { return playing_; }
    bool isRecording() const override { return recording_; }
    
    void setTempo(double bpm) override { tempo_ = bpm; }
    double getTempo() const override { return tempo_; }
    
    void setTimeSignature(int numerator, int denominator) override {
        time_sig_num_ = numerator;
        time_sig_den_ = denominator;
    }
    void getTimeSignature(int& numerator, int& denominator) const override {
        numerator = time_sig_num_;
        denominator = time_sig_den_;
    }
    
    void setLooping(bool enabled) override { looping_ = enabled; }
    void setLoopRegion(double start_seconds, double end_seconds) override {
        // Store for testing if needed
    }
    bool isLooping() const override { return looping_; }
};

// Mock implementation of TrackInterface for testing
class MockTrackInterface : public TrackInterface {
private:
    struct Track {
        std::string name;
        bool muted = false;
        bool solo = false;
        bool armed = false;
        int r = 128, g = 128, b = 128;
    };
    
    std::map<std::string, Track> tracks_;
    int next_id_ = 1;
    
    std::string generateId() {
        return "track_" + std::to_string(next_id_++);
    }
    
public:
    std::string createAudioTrack(const std::string& name) override {
        std::string id = generateId();
        tracks_[id] = Track{name, false, false, false, 128, 128, 128};
        return id;
    }
    
    std::string createMidiTrack(const std::string& name) override {
        std::string id = generateId();
        tracks_[id] = Track{name, false, false, false, 128, 128, 128};
        return id;
    }
    
    void deleteTrack(const std::string& track_id) override {
        tracks_.erase(track_id);
    }
    
    void setTrackName(const std::string& track_id, const std::string& name) override {
        if (tracks_.find(track_id) != tracks_.end()) {
            tracks_[track_id].name = name;
        }
    }
    
    std::string getTrackName(const std::string& track_id) const override {
        auto it = tracks_.find(track_id);
        return (it != tracks_.end()) ? it->second.name : "";
    }
    
    void setTrackMuted(const std::string& track_id, bool muted) override {
        if (tracks_.find(track_id) != tracks_.end()) {
            tracks_[track_id].muted = muted;
        }
    }
    
    bool isTrackMuted(const std::string& track_id) const override {
        auto it = tracks_.find(track_id);
        return (it != tracks_.end()) ? it->second.muted : false;
    }
    
    void setTrackSolo(const std::string& track_id, bool solo) override {
        if (tracks_.find(track_id) != tracks_.end()) {
            tracks_[track_id].solo = solo;
        }
    }
    
    bool isTrackSolo(const std::string& track_id) const override {
        auto it = tracks_.find(track_id);
        return (it != tracks_.end()) ? it->second.solo : false;
    }
    
    void setTrackArmed(const std::string& track_id, bool armed) override {
        if (tracks_.find(track_id) != tracks_.end()) {
            tracks_[track_id].armed = armed;
        }
    }
    
    bool isTrackArmed(const std::string& track_id) const override {
        auto it = tracks_.find(track_id);
        return (it != tracks_.end()) ? it->second.armed : false;
    }
    
    void setTrackColor(const std::string& track_id, int r, int g, int b) override {
        if (tracks_.find(track_id) != tracks_.end()) {
            tracks_[track_id].r = r;
            tracks_[track_id].g = g;
            tracks_[track_id].b = b;
        }
    }
    
    std::vector<std::string> getAllTrackIds() const override {
        std::vector<std::string> ids;
        for (const auto& pair : tracks_) {
            ids.push_back(pair.first);
        }
        return ids;
    }
    
    bool trackExists(const std::string& track_id) const override {
        return tracks_.find(track_id) != tracks_.end();
    }
};

TEST_CASE("TransportInterface Mock Implementation", "[transport]") {
    MockTransportInterface transport;
    
    SECTION("Initial state") {
        REQUIRE(transport.isPlaying() == false);
        REQUIRE(transport.isRecording() == false);
        REQUIRE(transport.getCurrentPosition() == Catch::Approx(0.0));
        REQUIRE(transport.getTempo() == Catch::Approx(120.0));
    }
    
    SECTION("Playback control") {
        transport.play();
        REQUIRE(transport.isPlaying() == true);
        
        transport.stop();
        REQUIRE(transport.isPlaying() == false);
        REQUIRE(transport.isRecording() == false);
    }
    
    SECTION("Recording") {
        transport.record();
        REQUIRE(transport.isRecording() == true);
        REQUIRE(transport.isPlaying() == true);
    }
    
    SECTION("Position control") {
        transport.locate(10.5);
        REQUIRE(transport.getCurrentPosition() == Catch::Approx(10.5));
        
        transport.locateMusical(2, 3, 500);
        int bar, beat, tick;
        transport.getCurrentMusicalPosition(bar, beat, tick);
        REQUIRE(bar == 2);
        REQUIRE(beat == 3);
        REQUIRE(tick == 500);
    }
    
    SECTION("Tempo and time signature") {
        transport.setTempo(140.0);
        REQUIRE(transport.getTempo() == Catch::Approx(140.0));
        
        transport.setTimeSignature(3, 8);
        int num, den;
        transport.getTimeSignature(num, den);
        REQUIRE(num == 3);
        REQUIRE(den == 8);
    }
    
    SECTION("Looping") {
        REQUIRE(transport.isLooping() == false);
        transport.setLooping(true);
        REQUIRE(transport.isLooping() == true);
    }
}

TEST_CASE("TrackInterface Mock Implementation", "[track]") {
    MockTrackInterface tracks;
    
    SECTION("Create tracks") {
        std::string audio_id = tracks.createAudioTrack("Audio Track");
        std::string midi_id = tracks.createMidiTrack("MIDI Track");
        
        REQUIRE(tracks.trackExists(audio_id) == true);
        REQUIRE(tracks.trackExists(midi_id) == true);
        REQUIRE(tracks.getTrackName(audio_id) == "Audio Track");
        REQUIRE(tracks.getTrackName(midi_id) == "MIDI Track");
    }
    
    SECTION("Track properties") {
        std::string track_id = tracks.createAudioTrack("Test Track");
        
        // Test mute
        REQUIRE(tracks.isTrackMuted(track_id) == false);
        tracks.setTrackMuted(track_id, true);
        REQUIRE(tracks.isTrackMuted(track_id) == true);
        
        // Test solo
        REQUIRE(tracks.isTrackSolo(track_id) == false);
        tracks.setTrackSolo(track_id, true);
        REQUIRE(tracks.isTrackSolo(track_id) == true);
        
        // Test arm
        REQUIRE(tracks.isTrackArmed(track_id) == false);
        tracks.setTrackArmed(track_id, true);
        REQUIRE(tracks.isTrackArmed(track_id) == true);
    }
    
    SECTION("Track management") {
        std::string track1 = tracks.createAudioTrack("Track 1");
        std::string track2 = tracks.createMidiTrack("Track 2");
        
        auto all_tracks = tracks.getAllTrackIds();
        REQUIRE(all_tracks.size() == 2);
        
        tracks.deleteTrack(track1);
        REQUIRE(tracks.trackExists(track1) == false);
        REQUIRE(tracks.trackExists(track2) == true);
        
        all_tracks = tracks.getAllTrackIds();
        REQUIRE(all_tracks.size() == 1);
    }
    
    SECTION("Track naming") {
        std::string track_id = tracks.createAudioTrack("Original Name");
        REQUIRE(tracks.getTrackName(track_id) == "Original Name");
        
        tracks.setTrackName(track_id, "New Name");
        REQUIRE(tracks.getTrackName(track_id) == "New Name");
    }
}

TEST_CASE("MidiNote Structure", "[clip]") {
    SECTION("Create MIDI note") {
        MidiNote note(60, 100, 0.0, 1.0);  // Middle C, velocity 100, start at 0, duration 1 beat
        
        REQUIRE(note.note == 60);
        REQUIRE(note.velocity == 100);
        REQUIRE(note.start == Catch::Approx(0.0));
        REQUIRE(note.duration == Catch::Approx(1.0));
    }
    
    SECTION("MIDI note boundaries") {
        MidiNote low_note(0, 1, 0.0, 0.25);
        MidiNote high_note(127, 127, 4.0, 2.0);
        
        REQUIRE(low_note.note == 0);
        REQUIRE(low_note.velocity == 1);
        REQUIRE(high_note.note == 127);
        REQUIRE(high_note.velocity == 127);
    }
} 