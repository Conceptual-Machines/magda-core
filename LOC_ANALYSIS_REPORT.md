# Lines of Code Analysis Report

**Generated:** 2026-02-07 21:43:29 UTC  
**Repository:** Magda Core  
**Analysis Scope:** All C++ files > 1000 LOC (excluding AudioBridge)

## Executive Summary

- **Total Large Files:** 20
- **Total Lines:** 34,487
- **Average File Size:** 1724 lines
- **Largest File:** magda/daw/ui/panels/content/InspectorContent.cpp (3,353 lines)

### Distribution by Category

| Category | Files | Total Lines | Percentage |
|----------|-------|-------------|------------|
| UI Components | 14 | 25,041 | 72.6% |
| Core Logic | 4 | 6,184 | 17.9% |
| Engine | 1 | 1,667 | 4.8% |
| Project | 1 | 1,595 | 4.6% |

## Priority Classification

Files are classified into priority levels based on their size:

- 🔴 **CRITICAL** (>2500 LOC): Immediate refactoring needed
- 🟠 **HIGH** (2000-2500 LOC): Should be addressed soon
- 🟡 **MEDIUM** (1500-2000 LOC): Consider for future refactoring
- 🟢 **LOW** (1000-1500 LOC): Monitor but not urgent

### 🔴 CRITICAL PRIORITY (>2500 LOC)

| File | Lines | Code | Functions | Avg Lines/Func |
|------|-------|------|-----------|----------------|
| `magda/daw/ui/panels/content/InspectorContent.cpp` | 3,353 | 2,635 | 306 | 8.6 |
| `magda/daw/core/TrackManager.cpp` | 2,573 | 1,991 | 222 | 9.0 |
| `magda/daw/ui/components/tracks/TrackContentPanel.cpp` | 2,540 | 1,782 | 266 | 6.7 |

### 🟠 HIGH PRIORITY (2000-2500 LOC)

| File | Lines | Code | Functions | Avg Lines/Func |
|------|-------|------|-----------|----------------|
| `magda/daw/ui/windows/MainWindow.cpp` | 2,400 | 1,823 | 259 | 7.0 |
| `magda/daw/ui/components/tracks/TrackHeadersPanel.cpp` | 2,203 | 1,588 | 206 | 7.7 |
| `magda/daw/ui/components/clips/ClipComponent.cpp` | 2,007 | 1,503 | 185 | 8.1 |

### 🟡 MEDIUM PRIORITY (1500-2000 LOC)

| File | Lines | Code | Functions | Avg Lines/Func |
|------|-------|------|-----------|----------------|
| `magda/daw/ui/views/MainView.cpp` | 1,926 | 1,278 | 173 | 7.4 |
| `magda/daw/ui/views/SessionView.cpp` | 1,681 | 1,259 | 178 | 7.1 |
| `magda/daw/engine/TracktionEngineWrapper.cpp` | 1,667 | 1,212 | 190 | 6.4 |
| `magda/daw/project/ProjectSerializer.cpp` | 1,595 | 1,203 | 88 | 13.7 |

### 🟢 LOWER PRIORITY (1000-1500 LOC)

| File | Lines | Code | Functions | Avg Lines/Func |
|------|-------|------|-----------|----------------|
| `magda/daw/ui/components/timeline/TimelineComponent.cpp` | 1,432 | 1,014 | 130 | 7.8 |
| `magda/daw/ui/panels/content/TrackChainContent.cpp` | 1,422 | 1,075 | 118 | 9.1 |
| `magda/daw/ui/components/waveform/WaveformGridComponent.cpp` | 1,384 | 1,020 | 133 | 7.7 |
| `magda/daw/ui/panels/content/PianoRollContent.cpp` | 1,359 | 976 | 129 | 7.6 |
| `magda/daw/core/ClipManager.cpp` | 1,249 | 918 | 176 | 5.2 |
| `magda/daw/core/SelectionManager.cpp` | 1,235 | 873 | 152 | 5.7 |
| `magda/daw/ui/components/chain/DeviceSlotComponent.cpp` | 1,189 | 874 | 115 | 7.6 |
| `magda/daw/ui/components/chain/NodeComponent.cpp` | 1,133 | 854 | 184 | 4.6 |
| `magda/daw/core/SelectionManager.hpp` | 1,127 | 616 | 29 | 21.2 |
| `magda/daw/ui/panels/content/WaveformEditorContent.cpp` | 1,012 | 738 | 122 | 6.0 |

## Detailed File Analysis

### 1. magda/daw/ui/panels/content/InspectorContent.cpp 🔴 CRITICAL

**Metrics:**
- Total Lines: 3,353
- Code Lines: 2,635
- Comment Lines: 306
- Blank Lines: 412
- Functions: 306
- Classes: 2
- Average Lines per Function: 8.6

**Refactoring Suggestions:**
- Split into separate inspector types (ClipInspector, TrackInspector, DeviceInspector, etc.)
- Extract property editing widgets into reusable components
- Create a factory pattern for inspector creation
- Move update logic into separate controller classes

---

### 2. magda/daw/core/TrackManager.cpp 🔴 CRITICAL

**Metrics:**
- Total Lines: 2,573
- Code Lines: 1,991
- Comment Lines: 254
- Blank Lines: 328
- Functions: 222
- Classes: 0
- Average Lines per Function: 9.0

**Refactoring Suggestions:**
- Split into TrackFactory (creation), TrackRegistry (storage), and TrackOperations (business logic)
- Extract track validation into separate validator classes
- Move track state management into dedicated state objects
- Create command objects for track operations (Command pattern)

---

### 3. magda/daw/ui/components/tracks/TrackContentPanel.cpp 🔴 CRITICAL

**Metrics:**
- Total Lines: 2,540
- Code Lines: 1,782
- Comment Lines: 346
- Blank Lines: 412
- Functions: 266
- Classes: 1
- Average Lines per Function: 6.7

**Refactoring Suggestions:**
- Separate rendering logic into TrackRenderer class
- Extract mouse/keyboard interaction into InteractionHandler
- Move layout calculations into LayoutManager
- Create separate components for clip display vs. automation lanes

---

### 4. magda/daw/ui/windows/MainWindow.cpp 🟠 HIGH

**Metrics:**
- Total Lines: 2,400
- Code Lines: 1,823
- Comment Lines: 216
- Blank Lines: 361
- Functions: 259
- Classes: 3
- Average Lines per Function: 7.0

**Refactoring Suggestions:**
- Extract menu handling into MenuManager (if not already separate)
- Move toolbar logic into ToolbarManager
- Split layout logic into LayoutManager
- Create separate dialog manager for modal interactions

---

### 5. magda/daw/ui/components/tracks/TrackHeadersPanel.cpp 🟠 HIGH

**Metrics:**
- Total Lines: 2,203
- Code Lines: 1,588
- Comment Lines: 270
- Blank Lines: 345
- Functions: 206
- Classes: 2
- Average Lines per Function: 7.7

**Refactoring Suggestions:**
- Separate header rendering from interaction logic
- Extract drag-and-drop handling into DragHandler
- Create reusable header components (name, controls, meters)
- Move state visualization into separate renderer

---

### 6. magda/daw/ui/components/clips/ClipComponent.cpp 🟠 HIGH

**Metrics:**
- Total Lines: 2,007
- Code Lines: 1,503
- Comment Lines: 223
- Blank Lines: 281
- Functions: 185
- Classes: 0
- Average Lines per Function: 8.1

**Refactoring Suggestions:**
- Consider applying SOLID principles (especially Single Responsibility)
- Look for opportunities to extract cohesive subsystems
- Evaluate if multiple responsibilities can be separated
- Consider using composition over large inheritance hierarchies

---

### 7. magda/daw/ui/views/MainView.cpp 🟡 MEDIUM

**Metrics:**
- Total Lines: 1,926
- Code Lines: 1,278
- Comment Lines: 290
- Blank Lines: 358
- Functions: 173
- Classes: 1
- Average Lines per Function: 7.4

---

### 8. magda/daw/ui/views/SessionView.cpp 🟡 MEDIUM

**Metrics:**
- Total Lines: 1,681
- Code Lines: 1,259
- Comment Lines: 156
- Blank Lines: 266
- Functions: 178
- Classes: 8
- Average Lines per Function: 7.1

---

### 9. magda/daw/engine/TracktionEngineWrapper.cpp 🟡 MEDIUM

**Metrics:**
- Total Lines: 1,667
- Code Lines: 1,212
- Comment Lines: 170
- Blank Lines: 285
- Functions: 190
- Classes: 0
- Average Lines per Function: 6.4

---

### 10. magda/daw/project/ProjectSerializer.cpp 🟡 MEDIUM

**Metrics:**
- Total Lines: 1,595
- Code Lines: 1,203
- Comment Lines: 137
- Blank Lines: 255
- Functions: 88
- Classes: 0
- Average Lines per Function: 13.7

---

### 11. magda/daw/ui/components/timeline/TimelineComponent.cpp 🟢 LOW

**Metrics:**
- Total Lines: 1,432
- Code Lines: 1,014
- Comment Lines: 200
- Blank Lines: 218
- Functions: 130
- Classes: 0
- Average Lines per Function: 7.8

---

### 12. magda/daw/ui/panels/content/TrackChainContent.cpp 🟢 LOW

**Metrics:**
- Total Lines: 1,422
- Code Lines: 1,075
- Comment Lines: 155
- Blank Lines: 192
- Functions: 118
- Classes: 4
- Average Lines per Function: 9.1

---

### 13. magda/daw/ui/components/waveform/WaveformGridComponent.cpp 🟢 LOW

**Metrics:**
- Total Lines: 1,384
- Code Lines: 1,020
- Comment Lines: 153
- Blank Lines: 211
- Functions: 133
- Classes: 1
- Average Lines per Function: 7.7

---

### 14. magda/daw/ui/panels/content/PianoRollContent.cpp 🟢 LOW

**Metrics:**
- Total Lines: 1,359
- Code Lines: 976
- Comment Lines: 182
- Blank Lines: 201
- Functions: 129
- Classes: 2
- Average Lines per Function: 7.6

---

### 15. magda/daw/core/ClipManager.cpp 🟢 LOW

**Metrics:**
- Total Lines: 1,249
- Code Lines: 918
- Comment Lines: 141
- Blank Lines: 190
- Functions: 176
- Classes: 0
- Average Lines per Function: 5.2

---

### 16. magda/daw/core/SelectionManager.cpp 🟢 LOW

**Metrics:**
- Total Lines: 1,235
- Code Lines: 873
- Comment Lines: 131
- Blank Lines: 231
- Functions: 152
- Classes: 0
- Average Lines per Function: 5.7

---

### 17. magda/daw/ui/components/chain/DeviceSlotComponent.cpp 🟢 LOW

**Metrics:**
- Total Lines: 1,189
- Code Lines: 874
- Comment Lines: 155
- Blank Lines: 160
- Functions: 115
- Classes: 0
- Average Lines per Function: 7.6

---

### 18. magda/daw/ui/components/chain/NodeComponent.cpp 🟢 LOW

**Metrics:**
- Total Lines: 1,133
- Code Lines: 854
- Comment Lines: 123
- Blank Lines: 156
- Functions: 184
- Classes: 0
- Average Lines per Function: 4.6

---

### 19. magda/daw/core/SelectionManager.hpp 🟢 LOW

**Metrics:**
- Total Lines: 1,127
- Code Lines: 616
- Comment Lines: 334
- Blank Lines: 177
- Functions: 29
- Classes: 15
- Average Lines per Function: 21.2

---

### 20. magda/daw/ui/panels/content/WaveformEditorContent.cpp 🟢 LOW

**Metrics:**
- Total Lines: 1,012
- Code Lines: 738
- Comment Lines: 128
- Blank Lines: 146
- Functions: 122
- Classes: 3
- Average Lines per Function: 6.0

---

## General Refactoring Guidelines

### UI Components (14 files)

UI files tend to be large due to:
- Complex event handling (mouse, keyboard, drag-drop)
- Layout calculations and painting logic
- State management and updates
- Multiple widget configurations

**Recommended Patterns:**
1. **Separation of Concerns**
   - Renderer classes for painting
   - Controller classes for interaction
   - Model classes for state

2. **Command Pattern**
   - Extract actions into command objects
   - Enables undo/redo functionality
   - Simplifies testing

3. **Component Composition**
   - Break large panels into smaller, reusable components
   - Use dependency injection for component communication

4. **State Management**
   - Centralize state in dedicated state objects
   - Use observer pattern for updates
   - Separate view state from model state

### Core Logic (4 files)

Core files contain business logic that should be:
- Well-tested (unit tests)
- Independent of UI concerns
- Easy to reason about

**Recommended Patterns:**
1. **Manager Decomposition**
   - Split XManager into XFactory, XRegistry, XOperations
   - Each handles a specific responsibility

2. **Command Objects**
   - Encapsulate operations as objects
   - Makes operations reusable and testable
   - Enables logging and undo functionality

3. **Facade Pattern**
   - Create simple interfaces for complex subsystems
   - Hide implementation details
   - Ease testing through interface mocking

### Engine Files (1 file)

Engine wrappers often grow large due to:
- Many API surface area requirements
- Complex initialization sequences
- State synchronization logic

**Recommended Patterns:**
1. **Adapter Pattern**
   - Separate adapter layer from business logic
   - Makes engine swapping easier
   - Isolates third-party dependencies

2. **Builder Pattern**
   - Complex initialization sequences
   - Separate construction from representation

## Metrics Interpretation

### Lines per Function
- **< 20**: Excellent - small, focused functions
- **20-30**: Good - manageable complexity
- **30-50**: Fair - consider breaking down
- **> 50**: Poor - functions are too complex

### Total Lines of Code
- **< 500**: Small file
- **500-1000**: Medium file
- **1000-1500**: Large file - monitor
- **1500-2500**: Very large - plan refactoring
- **> 2500**: Critical - immediate attention needed

### Function Count
High function counts can indicate:
- Good decomposition (positive)
- Missing abstraction opportunities (negative)
- Potential for grouping related functions into classes

## Next Steps

1. **Immediate Actions** (Critical Priority)
   - InspectorContent.cpp: Break into separate inspector types
   - TrackManager.cpp: Split into Factory/Registry/Operations
   - TrackContentPanel.cpp: Extract rendering/interaction/layout

2. **Short-term** (High Priority)
   - MainWindow.cpp: Extract menu/toolbar/layout managers
   - TrackHeadersPanel.cpp: Separate rendering from interaction
   - ClipComponent.cpp: Break into smaller components

3. **Medium-term** (Medium Priority)
   - Review and refactor remaining files >1500 LOC
   - Establish patterns from successful refactorings
   - Update coding guidelines based on learnings

4. **Ongoing**
   - Monitor file sizes during code reviews
   - Establish LOC limits in coding standards
   - Create refactoring tasks when files exceed thresholds
   - Consider using static analysis tools for complexity metrics

## Tools for Monitoring

Consider integrating:
- **cloc**: Count Lines of Code tool
- **lizard**: Cyclomatic Complexity analyzer
- **cppcheck**: Static analysis
- **clang-tidy**: Code quality checks

## Conclusion

The analysis identified 20 files exceeding 1000 LOC, with 3 critical files over 2500 lines. The majority (70%) are UI components, which tend to grow large due to event handling and rendering logic. Prioritized refactoring of the largest files will significantly improve code maintainability and testability.

**Key Takeaway:** Focus on the 3 critical files first, then systematically address high-priority files. Apply consistent refactoring patterns across similar file types to maximize efficiency and code quality.
