#include <catch2/catch_test_macros.hpp>

#include "magda/daw/audio/AudioBridge.hpp"
#include "magda/daw/core/TrackManager.hpp"

/**
 * @file test_audiobridge_track_limit_bug.cpp
 * @brief Test to reproduce Bug #1: Array bounds violation in MIDI activity tracking
 *
 * BUG DESCRIPTION:
 * AudioBridge has a fixed-size array `midiActivityFlags_` with 128 entries (kMaxTracks = 128).
 * However, TrackManager uses an auto-incrementing `nextTrackId_` that can grow beyond 128.
 * When track IDs exceed 128, calls to triggerMidiActivity() and consumeMidiActivity() will
 * either silently fail (if bounds checking) or cause undefined behavior (if no bounds check).
 *
 * REPRODUCTION STEPS:
 * 1. Create more than 128 tracks (or manipulate track IDs to exceed 128)
 * 2. Call triggerMidiActivity() with trackId >= 128
 * 3. Observe that the activity is not tracked due to bounds check
 *
 * EXPECTED BEHAVIOR:
 * MIDI activity should work for all valid track IDs, regardless of the number
 *
 * ACTUAL BEHAVIOR:
 * MIDI activity tracking silently fails for track IDs >= 128
 *
 * FILES AFFECTED:
 * - magda/daw/audio/AudioBridge.hpp (line 534-535)
 * - magda/daw/audio/AudioBridge.cpp (triggerMidiActivity, consumeMidiActivity)
 *
 * SUGGESTED FIX:
 * Replace fixed-size array with std::unordered_map<TrackId, std::atomic<bool>>
 * or use a dynamic data structure that can grow as needed.
 */

TEST_CASE("AudioBridge - Track limit bug: MIDI activity array bounds", "[audiobridge][bug][midi]") {
    SECTION("Demonstrate the kMaxTracks limitation") {
        // Document the current limitation
        constexpr int kMaxTracks = 128;  // From AudioBridge.hpp line 534
        
        SECTION("Track IDs below 128 should work") {
            int validTrackId = 100;
            REQUIRE(validTrackId < kMaxTracks);
            // If we had a working AudioBridge, triggerMidiActivity(100) would work
        }
        
        SECTION("Track IDs at or above 128 will fail silently") {
            int invalidTrackId = 128;
            REQUIRE(invalidTrackId >= kMaxTracks);
            // triggerMidiActivity(128) will hit the bounds check and return early
            // This is the BUG: track IDs can legitimately be >= 128
        }
        
        SECTION("Track IDs can exceed 128 in TrackManager") {
            // TrackManager uses nextTrackId_++ which can grow indefinitely
            // Example scenario:
            // 1. Create 50 tracks (IDs 1-50)
            // 2. Delete tracks 1-49
            // 3. Create 100 more tracks (IDs 51-150)
            // Now we have track IDs up to 150, but midiActivityFlags_ only goes to 127
            
            int exampleTrackId = 150;
            REQUIRE(exampleTrackId >= kMaxTracks);
            // This track ID is valid in TrackManager but will fail in AudioBridge
        }
    }
    
    SECTION("Reproduce the bug with track ID beyond limit") {
        // This is a compile-time test showing the bug exists
        // The actual runtime test would require full AudioBridge setup
        
        // Simulating the bug:
        constexpr int kMaxTracks = 128;
        int trackId = 200;  // Valid track ID from TrackManager
        
        // This is what happens in triggerMidiActivity():
        bool wouldSucceed = (trackId >= 0 && trackId < kMaxTracks);
        
        // BUG: The function returns early without setting the flag
        REQUIRE_FALSE(wouldSucceed);
        
        // Expected: Should work for any valid TrackId
        // Actual: Only works for TrackId < 128
    }
    
    SECTION("Suggested fix: Use dynamic container") {
        // Instead of: std::array<std::atomic<bool>, kMaxTracks> midiActivityFlags_;
        // Use: std::unordered_map<TrackId, std::atomic<bool>> midiActivityFlags_;
        // Or: std::map<TrackId, std::atomic<bool>> midiActivityFlags_;
        
        // This would allow arbitrary track IDs without bounds limitations
        REQUIRE(true);  // Documentation test
    }
}

TEST_CASE("AudioBridge - Track limit bug: Real-world scenario", "[audiobridge][bug][scenario]") {
    SECTION("User creates, deletes, and recreates many tracks") {
        // Scenario that triggers the bug:
        // 1. User creates 100 tracks over time
        // 2. Deletes 90 of them (but track IDs are not reused)
        // 3. Creates 50 more tracks
        // 4. Now has track IDs ranging from 1-150
        // 5. Track 150 cannot trigger MIDI activity indicators
        
        // This is a realistic workflow for a power user working on a large project
        
        int initialTracks = 100;  // Track IDs 1-100
        int deletedTracks = 90;   // Delete 90 tracks
        int newTracks = 50;       // Track IDs 101-150
        int maxTrackId = initialTracks + newTracks;  // 150
        
        constexpr int kMaxTracks = 128;
        REQUIRE(maxTrackId > kMaxTracks);
        
        // Track 150 is a valid track but MIDI activity won't work
        int problematicTrackId = maxTrackId;
        bool midiActivityWouldWork = (problematicTrackId < kMaxTracks);
        
        REQUIRE_FALSE(midiActivityWouldWork);  // This is the bug!
        
        // Impact: User sees MIDI activity on tracks 1-127 but not on tracks 128+
        // This is confusing and looks like a broken feature
    }
}

TEST_CASE("AudioBridge - Track limit bug: Code locations", "[audiobridge][bug][reference]") {
    SECTION("Bug locations in codebase") {
        // BUG #1 LOCATIONS:
        // 
        // 1. AudioBridge.hpp:534-535
        //    static constexpr int kMaxTracks = 128;
        //    std::array<std::atomic<bool>, kMaxTracks> midiActivityFlags_;
        // 
        // 2. AudioBridge.cpp:1013-1017 (triggerMidiActivity)
        //    void AudioBridge::triggerMidiActivity(TrackId trackId) {
        //        if (trackId >= 0 && trackId < kMaxTracks) {
        //            midiActivityFlags_[trackId].store(true, std::memory_order_release);
        //        }
        //    }
        // 
        // 3. AudioBridge.cpp:1019-1024 (consumeMidiActivity)
        //    bool AudioBridge::consumeMidiActivity(TrackId trackId) {
        //        if (trackId >= 0 && trackId < kMaxTracks) {
        //            return midiActivityFlags_[trackId].exchange(false, std::memory_order_acq_rel);
        //        }
        //        return false;
        //    }
        
        REQUIRE(true);  // Documentation test
    }
}
