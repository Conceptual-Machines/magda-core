# GitHub Issues for Magica DAW Development

## UI/UX Enhancements

### Issue 1: Implement Mixer/Session View Toggle
**Title:** Add mixer/session area with viewport toggle  
**Labels:** `enhancement`, `ui`, `mixer`  
**Description:**
- Add mixer/session area that can be displayed in the arrangement viewport
- Implement toggle button in UI to switch between arrangement and mixer views
- Start with simple mock interface (no audio engine integration needed)
- Should integrate with existing viewport system

**Acceptance Criteria:**
- [ ] Toggle button switches between arrangement and mixer views
- [ ] Mixer view displays in main arrangement area
- [ ] Basic mixer UI layout with channel strips
- [ ] State is preserved when switching views

---

### Issue 2: Implement Bottom Panel Mode Switching
**Title:** Add piano roll and plugin chain views to bottom panel  
**Labels:** `enhancement`, `ui`, `piano-roll`, `plugin-chain`  
**Description:**
- Add piano roll interface to bottom panel
- Add plugin chain interface to bottom panel  
- Implement toggle mechanism to switch between the two
- Start with simple mock interfaces

**Acceptance Criteria:**
- [ ] Piano roll view displays in bottom panel
- [ ] Plugin chain view displays in bottom panel
- [ ] Toggle mechanism switches between views
- [ ] Views integrate with existing bottom panel architecture

---

### Issue 3: Add Time/Beats Grid Overlay Toggle
**Title:** Implement time/beats grid overlay system  
**Labels:** `enhancement`, `ui`, `timeline`, `grid`  
**Description:**
- Add toggle switch for time vs beats grid overlay
- Implement grid rendering in timeline components
- Should work with existing zoom system
- Grid should adapt to current zoom level

**Acceptance Criteria:**
- [ ] Toggle button switches between time and beats grid
- [ ] Grid overlay renders correctly in timeline
- [ ] Grid adapts to zoom level changes
- [ ] Grid state persists across sessions

---

### Issue 4: Implement Visual Loop Functionality
**Title:** Add visual loop indicators and controls  
**Labels:** `enhancement`, `ui`, `timeline`, `loop`  
**Description:**
- Add loop region visual indicators to timeline
- Implement loop start/end markers
- Add loop enable/disable toggle
- Visual-only implementation (no audio playback integration yet)

**Acceptance Criteria:**
- [ ] Loop region displays visually in timeline
- [ ] Loop markers can be dragged to adjust region
- [ ] Loop toggle button enables/disables loop display
- [ ] Loop state is visually distinct from normal playback

---

### Issue 5: Add AI Prompt Console to Right Panel
**Title:** Implement AI prompt console in right panel  
**Labels:** `enhancement`, `ui`, `ai`, `console`  
**Description:**
- Add console/terminal interface to right panel for AI prompt interaction
- Should allow user to type commands and see AI responses
- Integration point for AI agent system
- Start with basic text input/output interface

**Acceptance Criteria:**
- [ ] Console interface displays in right panel
- [ ] Text input field for typing prompts
- [ ] Scrollable history of prompts and responses
- [ ] Basic command history (up/down arrow navigation)
- [ ] Clear console functionality
- [ ] Ready for AI agent integration

---

## Plugin System Architecture

### Issue 6: Design Node-Based Plugin Chain System
**Title:** Implement node-based FX chain architecture  
**Labels:** `enhancement`, `architecture`, `plugins`, `audio-graph`  
**Description:**
- Design system similar to Ableton/Bitwig for grouping effects
- Support both parallel and sequential configurations
- Use node graph to represent relationships
- Should integrate with future audio engine graph
- Start with data structures and visual representation

**Acceptance Criteria:**
- [ ] Node data structure for representing FX chains
- [ ] Support for parallel and sequential routing
- [ ] Visual node editor interface
- [ ] Ability to save/load chain configurations
- [ ] Architecture ready for audio engine integration

---

## Audio Engine Core

### Issue 7: Implement Basic Metronome
**Title:** Add simple metronome with audio engine integration  
**Labels:** `enhancement`, `audio-engine`, `metronome`, `core`  
**Description:**
- Implement basic metronome functionality
- Should be driven by audio engine timing
- Integrate with transport controls
- Simple click sound generation

**Acceptance Criteria:**
- [ ] Metronome generates click sounds
- [ ] Timing driven by audio engine
- [ ] Integrates with play/stop/tempo controls
- [ ] Volume control for metronome
- [ ] Can be enabled/disabled

---

### Issue 8: Implement Audio-Driven Playhead
**Title:** Add playhead driven by audio engine  
**Labels:** `enhancement`, `audio-engine`, `playhead`, `transport`  
**Description:**
- Implement playhead that moves based on audio engine timing
- Should update UI smoothly during playback
- Integrate with existing timeline components
- Ensure sample-accurate positioning

**Acceptance Criteria:**
- [ ] Playhead position updates during audio playback
- [ ] Smooth visual updates in timeline
- [ ] Sample-accurate positioning
- [ ] Integrates with zoom and scroll system
- [ ] Works with loop functionality

---

## AI Agent System

### Issue 9: Implement "Add Track" DAW API Function
**Title:** Create DAW API for track management  
**Labels:** `enhancement`, `api`, `tracks`, `ai-ready`  
**Description:**
- Implement "add track" function in DAW core
- Expose through API interface for external use
- Design for AI agent consumption
- Should integrate with existing track system

**Acceptance Criteria:**
- [ ] Add track function in DAW core
- [ ] API endpoint for adding tracks
- [ ] Support different track types (audio, MIDI, etc.)
- [ ] Returns track ID and status
- [ ] Error handling for invalid requests

---

### Issue 10: Design Two-Agent AI System Architecture
**Title:** Implement MCP-style dual AI agent system  
**Labels:** `enhancement`, `ai`, `agents`, `architecture`  
**Description:**
- Design two-agent system: prompt parser + function-specific agents
- Implement MCP (Model Context Protocol) style architecture
- Start with commercial LLM integration
- Design for eventual local LLM support

**Acceptance Criteria:**
- [ ] Prompt parsing agent that interprets user commands
- [ ] Function-specific agent for DAW operations
- [ ] MCP-style protocol between agents
- [ ] Commercial LLM integration (OpenAI/Anthropic)
- [ ] Architecture ready for local LLM plugins

---

### Issue 11: Add Local LLM Support Framework
**Title:** Implement local LLM integration framework  
**Labels:** `enhancement`, `ai`, `local-llm`, `research`  
**Description:**
- Research and implement local LLM integration
- Support for models like Llama, Mistral, etc.
- Performance optimization for real-time use
- Fallback to commercial LLMs when needed

**Acceptance Criteria:**
- [ ] Local LLM inference framework
- [ ] Support for popular open models
- [ ] Performance benchmarking
- [ ] Graceful fallback to commercial APIs
- [ ] Memory and CPU usage optimization

---

## Technical Debt & Infrastructure

### Issue 12: Add Code Quality Tools
**Title:** Integrate clang-format, clang-tidy, and code quality tools  
**Labels:** `development`, `code-quality`, `tooling`, `ci`  
**Description:**
- Add clang-format for consistent code formatting
- Integrate clang-tidy for static analysis and linting
- Add pre-commit hooks for automatic formatting
- Include code quality checks in CI pipeline
- Add configuration files for consistent standards

**Acceptance Criteria:**
- [ ] `.clang-format` configuration file added
- [ ] `.clang-tidy` configuration file added  
- [ ] Pre-commit hooks for automatic formatting
- [ ] CI integration for code quality checks
- [ ] Documentation for code style guidelines
- [ ] Make target for formatting code (`make format`)
- [ ] Make target for running lints (`make lint`)

---

### Issue 13: Expand JUCE Test Coverage
**Title:** Add comprehensive test coverage for core components  
**Labels:** `testing`, `juce`, `quality`  
**Description:**
- Expand JUCE test coverage beyond ZoomManager
- Add tests for timeline, transport, and core DAW functions
- Ensure CI runs all tests reliably

**Acceptance Criteria:**
- [ ] Test coverage for TimelineComponent
- [ ] Test coverage for Transport controls
- [ ] Test coverage for Track management
- [ ] All tests pass in CI
- [ ] Coverage reporting integrated

---

## Priority Order Recommendation:

1. **High Priority (Start immediately):**
   - Issue 3: Time/Beats Grid Toggle (builds on existing timeline work)
   - Issue 4: Visual Loop Functionality (extends timeline capabilities)
   - Issue 5: AI Prompt Console (foundation for user interaction with AI)
   - Issue 9: Add Track API (foundation for AI system)

2. **Medium Priority (Next sprint):**
   - Issue 1: Mixer View Toggle
   - Issue 2: Bottom Panel Switching
   - Issue 7: Basic Metronome
   - Issue 12: Code Quality Tools

3. **Lower Priority (Future sprints):**
   - Issue 6: Node-based Plugin System
   - Issue 8: Audio-Driven Playhead
   - Issue 10: AI Agent System
   - Issue 11: Local LLM Support
   - Issue 13: Expanded Test Coverage

Each issue is designed to be incrementally implementable and builds on the existing architecture. 