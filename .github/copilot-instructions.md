# GitHub Copilot Review Instructions for Magda

This document defines code review guidelines for the Magda project, a JUCE/C++ audio DAW framework. Reviews should focus on critical bugs, performance improvements, and code readability to ensure high-quality, maintainable code.

## Review Focus Areas

### 1. Critical Bugs (Highest Priority)
- **Memory Safety**: Check for memory leaks, use-after-free, buffer overflows, and dangling pointers
- **Thread Safety**: Verify proper synchronization in audio processing code, especially in real-time contexts
- **Audio Buffer Issues**: Look for buffer over/underruns, incorrect sample rate handling, and improper buffer management
- **Resource Management**: Ensure proper RAII patterns, especially with JUCE objects and audio resources
- **Null Pointer Dereferences**: Check for proper null checks before dereferencing pointers
- **Integer Overflow/Underflow**: Verify arithmetic operations, especially in sample calculations and buffer indexing
- **Plugin Lifecycle Issues**: Ensure proper plugin initialization, cleanup, and state management
- **JUCE Message Thread Violations**: Verify UI operations happen on the message thread, audio code runs on audio thread

### 2. Performance Improvements (High Priority)
- **Real-time Audio Safety**: Flag allocations, locks, or blocking operations in audio processing callbacks
- **Unnecessary Copies**: Identify opportunities to use move semantics, const references, or std::string_view
- **Algorithm Efficiency**: Look for O(n²) or worse algorithms that could be optimized
- **Cache Locality**: Suggest improvements to data structure layout for better cache performance
- **SIMD Opportunities**: Identify vectorizable loops in DSP code
- **Excessive Memory Allocations**: Flag unnecessary heap allocations, especially in hot paths
- **Lock Contention**: Identify potential bottlenecks from lock contention in multi-threaded code
- **Redundant Operations**: Point out redundant calculations or repeated work

### 3. Code Readability & Clarity (Medium Priority)
- **Naming Conventions**: Ensure consistency with project style (camelCase for functions/variables, CamelCase for classes)
- **Function Complexity**: Flag functions exceeding 80 lines or with cognitive complexity > 15
- **Magic Numbers**: Suggest named constants for literal values, except obvious cases (0, 1, 2)
- **Clear Intent**: Ensure code is self-documenting; suggest refactoring when intent is unclear
- **JUCE Idioms**: Verify proper use of JUCE patterns (e.g., MessageManager, ValueTree, listeners)
- **Modern C++20**: Encourage use of C++20 features where appropriate (concepts, ranges, coroutines if applicable)
- **Error Handling**: Verify appropriate error handling strategies
- **Documentation**: Check that complex algorithms or non-obvious code have explanatory comments
- **TODO Comments**: Read and respect TODO comments in the code being reviewed. If a TODO indicates planned work or known limitations, consider this context when reviewing related changes. Don't request changes that conflict with or duplicate TODO items.

## What NOT to Focus On

- **Style Issues Already Covered by clang-format**: Don't comment on formatting that will be auto-fixed
- **Minor Optimizations**: Avoid nitpicking micro-optimizations that don't impact performance measurably
- **Personal Preferences**: Don't suggest changes based solely on personal style preferences
- **Trivial Issues**: Skip commenting on very minor issues that don't affect functionality or maintainability
- **Pre-existing Issues**: Focus on the code being changed, not unrelated existing code
- **Nitpicking**: Do not nitpick. Focus on substantive issues that genuinely affect correctness, performance, or maintainability. If a comment wouldn't block a merge, it's probably not worth making.

## Resolving Previous Review Comments

- **Mark resolved issues as resolved**: When a subsequent PR push addresses feedback from a previous review iteration, those comments MUST be marked as resolved. Do not leave stale unresolved comments hanging.
- **Don't re-raise resolved issues**: If a concern from a previous review has been addressed in the current diff, do not re-raise it or raise a variation of it.
- **Acknowledge progress**: When re-reviewing after changes, focus only on what's new or still unresolved. Don't repeat feedback that has already been acted upon.

## Refactoring Guidelines (for Copilot Coding Agent)

When assigned refactoring issues, follow these rules strictly:

### Do NOT
- **Extract constants from obvious values** — `0.0`, `0.5`, `1.0`, `2.0`, `60.0`, `PI`, pixel padding values (4, 8, 12, 20), font sizes, etc. are not magic numbers. They are clearer inline than as `FULL_RANGE`, `HALF_CYCLE`, `SECONDS_PER_MINUTE`, or `kOuterPadding`.
- **Split functions under 50 NLOC** — Small functions don't need decomposition regardless of CCN.
- **Create PRs that only rename magic numbers or extract constants** — This is busywork, not refactoring.
- **Split one-liner expressions into separate functions** — `return 1.0f - phase;` does not need a `generateReverseSawWave()` wrapper.
- **Add `using namespace` indirection** — Creating a constants namespace and then `using namespace` in every method adds complexity, not clarity.
- **Make internal rendering constants public** — Paint parameters, timer intervals, and layout values are private implementation details.
- **Couple unrelated timers or constants** — Independent components should not share constants just because the values happen to be the same.
- **Redefine standard constants** — Use `juce::MathConstants<float>::pi`, not a custom `PI` constant.

### DO
- **Split God Objects** — Classes with 40+ methods or 1000+ lines need structural decomposition into smaller classes with distinct responsibilities.
- **Extract state machines** — Complex mouse handlers (`mouseDrag`/`mouseUp` with CCN > 20) benefit from strategy/state pattern extraction.
- **Break up monolithic constructors** — Constructors over 150 NLOC should be split into logical initialization helpers (e.g., `setupControls()`, `setupCallbacks()`, `setupLayout()`).
- **Decompose functions over 100 NLOC with CCN > 15** — These have genuine structural problems. Split along logical boundaries, not arbitrary line counts.
- **Focus on structural refactoring** — Move responsibilities to new classes, extract reusable components, simplify inheritance hierarchies.

### Thresholds for Action
Only refactor when a function meets **both** criteria:
- CCN > 15 **AND** NLOC > 50
- OR the file exceeds 1000 lines with clear God Object characteristics (40+ methods, mixed responsibilities)

Files under 300 lines, header files with only declarations, and test files should **never** be refactored.

## Project-Specific Context

### Architecture
- **DAW Core** (`magda/daw/`): Main application using JUCE and Tracktion Engine
- **Agents System** (`magda/agents/`): Multi-agent audio system
- **UI Components** (`magda/daw/ui/`): JUCE-based user interface
- **Audio Engine** (`magda/daw/engine/`): Tracktion Engine wrapper and audio processing

### Key Technologies
- **JUCE**: Cross-platform C++ framework for audio applications
- **Tracktion Engine**: Professional DAW audio engine
- **C++20**: Modern C++ standard with all features enabled
- **CMake**: Build system with Ninja generator
- **Catch2**: Testing framework

### Audio Processing Constraints
- **Real-time Safety**: Audio callbacks must be lock-free, allocation-free, and deterministic
- **Thread Context**: Audio code runs on audio thread, UI code on message thread
- **Buffer Management**: Always validate buffer sizes and sample rates
- **Plugin Hosting**: Proper lifecycle management critical for stability

### Code Quality Standards
- **clang-tidy**: Comprehensive static analysis enabled (see `.clang-tidy`)
- **clang-format**: Automatic code formatting enforced
- **Testing**: Use Catch2 for unit tests, test audio processing logic when possible
- **Documentation**: Complex algorithms should have explanatory comments

## Review Workflow

### Iterative Resolution Process
1. **Initial Review**: Copilot provides focused feedback on critical bugs, performance, and readability
2. **Discussion**: Developer addresses comments, asks questions, or provides justification
3. **Updates**: Developer makes changes based on valid feedback
4. **Re-review**: For significant changes, request another review cycle
5. **Resolution**: Mark comments as resolved once addressed
6. **Approval**: PR is approved when all critical issues are resolved

### Context Awareness
- **Previous Code Reviews**: Consider the context and feedback from previous code review iterations when providing new feedback. Don't repeat comments that have already been addressed or discussed.
- **Review History**: If a developer has provided justification for a design decision in a previous review iteration, respect that context unless new information warrants revisiting the issue.
- **Incremental Changes**: Recognize when changes are part of a larger refactoring or feature implementation. Consider the broader context and don't insist on perfection in intermediate states if the overall direction is sound.

### Review Memory (MCP)
A memory MCP server (`@modelcontextprotocol/server-memory`) is configured to persist review context across PRs. The review agent should:
- **Record decisions**: After a review, store key decisions, accepted patterns, and deferred items as entities in the knowledge graph (e.g., entity type `review_decision`, `accepted_pattern`, `deferred_item`).
- **Query before reviewing**: Before starting a new review, search the memory for relevant past decisions about the files or components being changed.
- **Avoid contradictions**: If a pattern or approach was previously accepted, don't flag it as an issue in a later PR unless there's a concrete new reason.
- **Track deferred work**: When an issue is explicitly deferred to a follow-up PR, record it so it can be checked for completion later without re-raising it prematurely.

### Comment Guidelines
- **Be Specific**: Point to exact line numbers and provide clear explanations
- **Be Constructive**: Suggest solutions, not just problems
- **Provide Context**: Explain why something is an issue (e.g., "causes memory leak" vs. "this looks wrong")
- **Prioritize**: Use "CRITICAL", "Important", or "Suggestion" labels to indicate severity
- **Link to Documentation**: Reference JUCE docs, C++ core guidelines, or project docs when relevant

### Review Expectations
- **Critical Bugs**: MUST be addressed before approval
- **Performance Issues**: Should be addressed or explicitly deferred with justification
- **Readability Issues**: Address if they significantly impact maintainability
- **Minor Suggestions**: Can be deferred to follow-up PRs if they don't block the main goal

### Preventing Infinite Review Loops

To avoid unbounded iteration and ensure review convergence:

#### 1. Demand Evidence for Critical Claims
For any "CRITICAL" bug report, require one of:
- **Minimal reproduction scenario**: Specific inputs and steps that trigger the issue
- **Failing test**: A test that demonstrates the problem (write it, see it fail, then fix)
- **Specific code path**: "Function A calls B with X, which makes Y null, causing Z to crash"

**Rule**: No concrete evidence = downgrade to non-blocking suggestion. Critical bugs must be demonstrable, not speculative.

#### 2. Convert Critical Issues to Tests
The ultimate loop-killer is testable claims:
- If a reviewer claims "state can be inconsistent", write a unit test for that state transition
- Add assertions or invariants in code to guard against the claimed issue
- If a test can't be written without inventing unrealistic conditions, the claim was likely speculative

After adding tests/invariants for real edge cases, most speculative concerns are resolved.

#### 3. Prefer Minimal Fixes Over Refactoring
Refactors create new surface area for fresh concerns. Instead:
- Apply **minimal fix** to address the witnessed issue
- Add **targeted guard** (assertion, null check, bounds check)
- Add **targeted test** to prevent regression
- Merge and move on

Save refactors for dedicated PRs with comprehensive test coverage already in place.

#### 4. Establish a "Diff Freeze" Point
Once you believe correctness is covered:
- **Freeze changes** except for bug fixes with concrete evidence
- **Ignore** further purely speculative "critical" comments
- Document this decision in the PR

Without a freeze point, you're in unbounded search space.

#### 5. Clear Merge Criteria
**Merge when**:
- CI is green
- Tests added for each critical claim with evidence
- Developer provides short risk assessment note
- No unreproduced crash/data-loss/security issues with evidence remain

**Don't merge when**:
- Reproducible crash, data loss, or security vulnerability exists with concrete evidence
- Required tests are missing for witnessed critical issues

## Examples

### ❌ BAD: Audio Thread Allocation
```cpp
void processAudioBlock(AudioBuffer<float>& buffer) {
    auto tempBuffer = std::make_unique<float[]>(buffer.getNumSamples()); // CRITICAL: Allocation in audio thread!
    // ...
}
```
**Review Comment**: "CRITICAL: `std::make_unique` allocates memory on the audio thread, which can cause dropouts. Pre-allocate in the constructor or use a fixed-size buffer."

### ✅ GOOD: Pre-allocated Buffer
```cpp
class Processor {
    std::vector<float> tempBuffer_;
public:
    void prepareToPlay(int samplesPerBlock) {
        tempBuffer_.resize(samplesPerBlock);
    }
    void processAudioBlock(AudioBuffer<float>& buffer) {
        // Use pre-allocated tempBuffer_ - safe for real-time
    }
}
```

### ❌ BAD: Thread Safety Issue
```cpp
void processAudioBlock(AudioBuffer<float>& buffer) {
    for (int i = 0; i < sharedData_.size(); ++i) { // CRITICAL: Race condition!
        // ...
    }
}
void setSharedData(std::vector<float> data) {
    sharedData_ = std::move(data); // Can be called from UI thread
}
```
**Review Comment**: "CRITICAL: `sharedData_` accessed from both audio and UI threads without synchronization. Use `std::atomic` or lock-free structures, or ensure thread-safe communication via JUCE's message thread."

### ❌ BAD: Unnecessary Copy
```cpp
void processClips(std::vector<Clip> clips) { // Important: Pass by value creates copy
    for (const auto& clip : clips) {
        // ...
    }
}
```
**Review Comment**: "Important: `clips` passed by value creates an unnecessary copy. Use `const std::vector<Clip>&` instead for better performance."

### ✅ GOOD: Const Reference
```cpp
void processClips(const std::vector<Clip>& clips) {
    for (const auto& clip : clips) {
        // ...
    }
}
```

### ❌ BAD: Unclear Intent
```cpp
if (x > 0.5) return true; else return false;
```
**Review Comment**: "Suggestion: Can be simplified to `return x > 0.5;` for better readability."

## Additional Resources
- [JUCE Documentation](https://docs.juce.com/)
- [JUCE Forum - Audio Thread Best Practices](https://forum.juce.com/)
- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/)
- [Real-Time Audio Programming 101](http://www.rossbencina.com/code/real-time-audio-programming-101-time-waits-for-nothing)

## Questions?
If you're unsure about a review comment, discuss it in the PR. The goal is collaborative improvement, not just compliance.
