#!/bin/bash

# Create GitHub issues from CSV file
# Usage: ./create_issues.sh

echo "Creating GitHub issues for Magica project..."

# Create issues from CSV data
# High Priority Issues

echo "Creating high priority issues..."

# Issue 1: Fix Build System
gh issue create \
  --title "Fix Build System - gRPC Dependencies" \
  --body "The build system has conflicts with gRPC and protobuf dependencies. Need to:

1. Fix gRPC target naming (use \`grpc++\` instead of \`gRPC::grpc++\`)
2. Configure gRPC CMake options properly before FetchContent_MakeAvailable
3. Remove vcpkg conflicts by setting \`CMAKE_TOOLCHAIN_FILE = \"\"\`
4. Fix protobuf plugin target references

**Current Error:**
\`\`\`
No target \"gRPC::grpc_cpp_plugin\"
Target \"magica_mcp\" links to gRPC::grpc++ but the target was not found
\`\`\`

**Solution:**
- Set gRPC CMake variables before \`FetchContent_MakeAvailable(gRPC)\`
- Use \`grpc++\` and \`gpr\` targets instead of \`gRPC::grpc++\`
- Use \`\$<TARGET_FILE:grpc_cpp_plugin>\` for protobuf generation" \
  --label "build-system,grpc,cmake" \
  --label "High Priority"

# Issue 2: Create Basic UI - Main Window
gh issue create \
  --title "Create Basic UI - Main Window" \
  --body "Create the main application window that's referenced in \`magica_daw_main.cpp\` but doesn't exist.

**Files to create:**
- \`daw/ui/main_window.hpp\`
- \`daw/ui/main_window.cpp\`

**Requirements:**
- JUCE DocumentWindow subclass
- Basic layout with transport controls, track list, timeline
- Connect to DAW engine for play/stop operations
- Integrate with gRPC server for agent communication

**Basic structure:**
\`\`\`cpp
class MainWindow : public juce::DocumentWindow {
    TransportPanel transport_panel_;
    TrackListPanel track_list_;
    TimelinePanel timeline_;
    MixerPanel mixer_;
    
    void onPlayClicked() { daw_engine_->play(); }
    void onStopClicked() { daw_engine_->stop(); }
};
\`\`\`" \
  --label "ui,core" \
  --label "High Priority"

# Issue 3: Create Transport Panel UI
gh issue create \
  --title "Create Transport Panel UI" \
  --body "Create transport controls panel for play, stop, record, and transport state.

**Files to create:**
- \`daw/ui/transport_panel.hpp\`
- \`daw/ui/transport_panel.cpp\`

**Features:**
- Play/Stop/Record buttons
- Tempo display and control
- Time signature display
- Current position display (bars/beats and time)
- Loop controls
- Transport state indicators

**Integration:**
- Connect to TransportInterface
- Update UI based on transport state changes
- Send commands to DAW engine" \
  --label "ui,transport" \
  --label "High Priority"

# Issue 4: Create Track List Panel
gh issue create \
  --title "Create Track List Panel" \
  --body "Create track management panel showing all tracks in the session.

**Files to create:**
- \`daw/ui/track_list.hpp\`
- \`daw/ui/track_list.cpp\`

**Features:**
- List of all tracks (MIDI, Audio, Instrument, Bus)
- Track name editing
- Mute/Solo/Arm buttons per track
- Track type indicators
- Volume faders
- Track selection
- Add/Delete track buttons

**Integration:**
- Connect to TrackInterface
- Update when tracks are added/removed
- Reflect track state changes" \
  --label "ui,tracks" \
  --label "High Priority"

# Issue 5: Create Timeline Panel
gh issue create \
  --title "Create Timeline Panel" \
  --body "Create timeline view showing the session timeline and playhead.

**Files to create:**
- \`daw/ui/timeline.hpp\`
- \`daw/ui/timeline.cpp\`

**Features:**
- Timeline ruler with time and bar/beat markers
- Playhead position indicator
- Zoom controls
- Grid display (bars, beats, ticks)
- Loop region visualization
- Click-to-seek functionality

**Integration:**
- Connect to TransportInterface for playhead position
- Update playhead position in real-time
- Handle timeline clicks for seeking" \
  --label "ui,timeline" \
  --label "High Priority"

# Issue 6: Implement Transport Interface
gh issue create \
  --title "Implement Transport Interface" \
  --body "Implement the TransportInterface to connect UI controls to actual DAW operations.

**Files to create:**
- \`daw/engine/transport_interface_impl.hpp\`
- \`daw/engine/transport_interface_impl.cpp\`

**Implementation:**
- Connect to TracktionEngine for actual transport operations
- Implement all virtual methods from TransportInterface
- Handle play, stop, record, locate, tempo, time signature
- Provide real-time state updates
- Thread-safe operations

**Methods to implement:**
- \`play()\`, \`stop()\`, \`pause()\`, \`record()\`
- \`locate()\`, \`locateMusical()\`
- \`setTempo()\`, \`setTimeSignature()\`
- \`setLooping()\`, \`setLoopRegion()\`" \
  --label "daw-engine,transport" \
  --label "High Priority"

# Issue 7: Implement Track Interface
gh issue create \
  --title "Implement Track Interface" \
  --body "Implement the TrackInterface to handle track management operations.

**Files to create:**
- \`daw/engine/track_interface_impl.hpp\`
- \`daw/engine/track_interface_impl.cpp\`

**Implementation:**
- Connect to TracktionEngine for track operations
- Implement track creation, deletion, naming
- Handle mute, solo, arm operations
- Manage track types (MIDI, Audio, Instrument, Bus)
- Provide track state updates

**Methods to implement:**
- \`createTrack()\`, \`deleteTrack()\`
- \`setTrackName()\`, \`getTrackName()\`
- \`muteTrack()\`, \`soloTrack()\`, \`armTrack()\`
- \`getTracks()\`, \`getTrackCount()\`" \
  --label "daw-engine,tracks" \
  --label "High Priority"

# Issue 8: Implement Clip Interface
gh issue create \
  --title "Implement Clip Interface" \
  --body "Implement the ClipInterface to handle MIDI and audio clip operations.

**Files to create:**
- \`daw/engine/clip_interface_impl.hpp\`
- \`daw/engine/clip_interface_impl.cpp\`

**Implementation:**
- Connect to TracktionEngine for clip operations
- Handle MIDI clip creation and editing
- Manage audio clip operations
- Implement note operations (add, remove, edit)
- Handle clip quantization

**Methods to implement:**
- \`createMidiClip()\`, \`createAudioClip()\`
- \`addNotesToClip()\`, \`removeNotesFromClip()\`
- \`getClipNotes()\`, \`updateClipNotes()\`
- \`quantizeClip()\`, \`getClipInfo()\`" \
  --label "daw-engine,clips" \
  --label "High Priority"

# Issue 9: Create Command Dispatcher
gh issue create \
  --title "Create Command Dispatcher" \
  --body "Create a command dispatcher to route gRPC commands to DAW operations.

**Files to create:**
- \`daw/command_dispatcher.hpp\`
- \`daw/command_dispatcher.cpp\`

**Implementation:**
- Register handlers for all DAW operations
- Connect Command objects to actual DAW interface calls
- Provide error handling and validation
- Support async operations where needed
- Return proper CommandResponse objects

**Handler registration:**
\`\`\`cpp
dispatcher.registerHandler(\"play\", [this](const Command& cmd) {
    transport_interface_->play();
    return CommandResponse(CommandResponse::Status::Success, \"Playing\");
});
\`\`\`

**Commands to handle:**
- Transport: play, stop, record, set_tempo, set_time_signature
- Tracks: create_track, delete_track, mute_track, solo_track
- Clips: create_midi_clip, add_notes, quantize_clip
- Mixer: set_volume, set_pan, add_effect" \
  --label "grpc,daw-engine" \
  --label "High Priority"

# Issue 10: Connect gRPC Server to DAW
gh issue create \
  --title "Connect gRPC Server to DAW" \
  --body "Connect the gRPC server to the DAW engine by registering command handlers.

**Files to modify:**
- \`daw/magica_daw_main.cpp\`
- \`daw/magica.cpp\`

**Implementation:**
- Create CommandDispatcher instance
- Register all command handlers with GrpcMCPServer
- Connect DAW interfaces to command handlers
- Handle gRPC request routing to DAW operations
- Provide proper error responses

**Integration points:**
- Register handlers in main application initialization
- Connect TransportInterface to transport commands
- Connect TrackInterface to track commands
- Connect ClipInterface to clip commands
- Connect MixerInterface to mixer commands" \
  --label "grpc,daw-engine" \
  --label "High Priority"

echo "High priority issues created successfully!"

# Medium Priority Issues
echo "Creating medium priority issues..."

# Issue 11: Create Mixer Panel
gh issue create \
  --title "Create Mixer Panel" \
  --body "Create mixer panel for track volume, pan, and effects.

**Files to create:**
- \`daw/ui/mixer_panel.hpp\`
- \`daw/ui/mixer_panel.cpp\`

**Features:**
- Volume faders for each track
- Pan controls
- Level meters (VU meters)
- Effect slots per track
- Master fader
- Stereo field visualization

**Integration:**
- Connect to MixerInterface
- Real-time level metering
- Volume/pan parameter updates" \
  --label "ui,mixer" \
  --label "Medium Priority"

# Issue 12: Implement Mixer Interface
gh issue create \
  --title "Implement Mixer Interface" \
  --body "Implement the MixerInterface to handle mixing operations.

**Files to create:**
- \`daw/engine/mixer_interface_impl.hpp\`
- \`daw/engine/mixer_interface_impl.cpp\`

**Implementation:**
- Connect to TracktionEngine for mixing operations
- Handle volume and pan controls
- Manage effects and plugins
- Provide real-time metering
- Handle master bus operations

**Methods to implement:**
- \`setTrackVolume()\`, \`getTrackVolume()\`
- \`setTrackPan()\`, \`getTrackPan()\`
- \`addEffect()\`, \`removeEffect()\`
- \`getLevelMeters()\`, \`getMasterVolume()\`" \
  --label "daw-engine,mixer" \
  --label "Medium Priority"

# Issue 13: Add Error Handling and Validation
gh issue create \
  --title "Add Error Handling and Validation" \
  --body "Add comprehensive error handling and validation throughout the system.

**Areas to improve:**
- Command parameter validation
- DAW operation error handling
- gRPC error responses
- UI error feedback
- Logging and debugging

**Implementation:**
- Validate command parameters before execution
- Handle DAW operation failures gracefully
- Provide meaningful error messages
- Add logging for debugging
- Implement retry mechanisms where appropriate
- Add input validation for UI controls" \
  --label "error-handling,validation" \
  --label "Medium Priority"

# Issue 14: Add Unit Tests
gh issue create \
  --title "Add Unit Tests" \
  --body "Create comprehensive unit tests for all components.

**Test files to create:**
- \`tests/test_transport_interface.cpp\`
- \`tests/test_track_interface.cpp\`
- \`tests/test_clip_interface.cpp\`
- \`tests/test_mixer_interface.cpp\`
- \`tests/test_command_dispatcher.cpp\`
- \`tests/test_grpc_server.cpp\`
- \`tests/test_ui_components.cpp\`

**Test coverage:**
- Interface implementations
- Command routing
- gRPC message handling
- UI component behavior
- Error scenarios
- Integration tests" \
  --label "testing,unit-tests" \
  --label "Medium Priority"

# Issue 15: Add Integration Tests
gh issue create \
  --title "Add Integration Tests" \
  --body "Create integration tests for the complete system.

**Test files to create:**
- \`tests/integration/test_agent_daw_communication.cpp\`
- \`tests/integration/test_ui_daw_integration.cpp\`
- \`tests/integration/test_grpc_daw_integration.cpp\`

**Test scenarios:**
- Agent connects and sends commands
- UI updates reflect DAW state changes
- gRPC calls execute DAW operations
- Real-time updates work correctly
- Error handling across system boundaries" \
  --label "testing,integration-tests" \
  --label "Medium Priority"

# Issue 16: Implement gRPC DAW Server
gh issue create \
  --title "Implement gRPC DAW Server" \
  --body "Create the actual gRPC server implementation that's referenced in main.cpp.

**Files to create:**
- \`daw/server/magica_daw_server.hpp\`
- \`daw/server/magica_daw_server.cpp\`

**Implementation:**
- Implement the MagdaDAWService gRPC interface
- Connect to CommandDispatcher for operation execution
- Handle all protobuf message types
- Provide proper gRPC status responses
- Support streaming operations for real-time updates

**Service methods to implement:**
- Transport: Play, Stop, Record, SetTempo, SetTimeSignature
- Tracks: CreateTrack, DeleteTrack, MuteTrack, SoloTrack
- MIDI: CreateMidiClip, AddNotesToClip, QuantizeClip
- Audio: CreateAudioClip, GetAudioClipInfo
- Mixing: SetTrackVolume, SetTrackPan, AddEffect
- Session: CreateSession, LoadSession, SaveSession
- Agents: RegisterAgent, SendMessage, BroadcastMessage" \
  --label "grpc,server" \
  --label "Medium Priority"

echo "Medium priority issues created successfully!"

# Low Priority Issues
echo "Creating low priority issues..."

# Issue 17: Add Documentation
gh issue create \
  --title "Add Documentation" \
  --body "Create comprehensive documentation for the system.

**Documentation to create:**
- \`docs/architecture.md\` - System architecture overview
- \`docs/api_reference.md\` - gRPC API documentation
- \`docs/ui_guide.md\` - User interface guide
- \`docs/agent_development.md\` - Guide for developing agents
- \`docs/development_setup.md\` - Development environment setup
- \`docs/deployment.md\` - Deployment instructions

**Content:**
- Architecture diagrams
- API examples
- Code examples
- Troubleshooting guides
- Best practices" \
  --label "documentation" \
  --label "Low Priority"

# Issue 18: Add CI/CD Pipeline
gh issue create \
  --title "Add CI/CD Pipeline" \
  --body "Set up continuous integration and deployment pipeline.

**Components:**
- GitHub Actions workflow
- Automated testing
- Build verification
- Code quality checks
- Automated deployment

**Workflow steps:**
- Build on multiple platforms
- Run unit and integration tests
- Check code formatting
- Run static analysis
- Generate documentation
- Create release artifacts" \
  --label "ci-cd,automation" \
  --label "Low Priority"

# Issue 19: Add Performance Monitoring
gh issue create \
  --title "Add Performance Monitoring" \
  --body "Add performance monitoring and profiling capabilities.

**Components:**
- Performance metrics collection
- Real-time monitoring
- Profiling tools integration
- Memory usage tracking
- Audio performance monitoring

**Metrics to track:**
- gRPC request latency
- DAW operation performance
- UI responsiveness
- Memory usage
- Audio buffer underruns" \
  --label "performance,monitoring" \
  --label "Low Priority"

# Issue 20: Add Plugin System
gh issue create \
  --title "Add Plugin System" \
  --body "Design and implement a plugin system for extending DAW functionality.

**Components:**
- Plugin interface definition
- Plugin loading mechanism
- Plugin communication protocol
- Plugin management UI
- Plugin marketplace integration

**Plugin types:**
- Audio effects plugins
- MIDI processing plugins
- Analysis plugins
- Export plugins
- Custom UI plugins" \
  --label "plugins,extensibility" \
  --label "Low Priority"

echo "All issues created successfully!"
echo "Total issues created: 20"
echo "High Priority: 10"
echo "Medium Priority: 6"
echo "Low Priority: 4" 