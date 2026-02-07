# How to Create the AudioBridge Refactoring Issue on GitHub

This guide shows how to create a GitHub issue from the comprehensive documentation in `docs/issues/audiobridge-refactoring.md`.

## Quick Create

1. Go to: https://github.com/Conceptual-Machines/magda-core/issues/new
2. Copy the content from `GITHUB_ISSUE_TEMPLATES.md` under "Issue #4"
3. Add labels: `refactoring`, `architecture`, `code-quality`, `audio`
4. Submit

## Full Create (with detailed plan)

If you want to include the complete implementation plan:

1. Go to: https://github.com/Conceptual-Machines/magda-core/issues/new
2. Use this template:

```markdown
# Refactor: AudioBridge into Focused Modules

The `AudioBridge` class has grown to **3,592 lines** with **70+ methods** and **19 distinct responsibilities**. This exceeds reasonable context size for both human developers and AI assistants.

## Quick Facts

- **Current Size**: 3,592 LOC (657 header + 2,935 implementation)
- **Methods**: 70+
- **Responsibilities**: 19 distinct areas
- **Status**: Exceeds project refactoring thresholds (800 LOC, CCN > 15)

## Problem

1. **Context Overflow** - Exceeds LLM context windows
2. **Testing Complexity** - Hard to unit test individual responsibilities  
3. **Thread Safety Complexity** - Multiple threading contexts spread across concerns
4. **Single Responsibility Violation** - God object antipattern
5. **High Coupling** - Changes ripple unpredictably

## Proposed Solution

Break into **12 focused modules** organized in 4 phases:

**Phase 1 (Low Risk):** Pure data managers
- TransportStateManager (~150 LOC)
- MidiActivityMonitor (~200 LOC) - Also fixes track ID >= 128 bug
- ParameterManager (~200 LOC)

**Phase 2 (Medium Risk):** Independent features
- WarpMarkerManager (~300 LOC)
- PluginWindowBridge (~150 LOC)
- MixerController (~250 LOC)

**Phase 3 (Higher Risk):** Core mappers
- TrackMappingManager (~300 LOC)
- PluginManager (~400 LOC)
- ClipSynchronizer (~500 LOC)

**Phase 4 (Highest Risk):** Routing & metering
- AudioRoutingManager (~200 LOC)
- MidiRoutingManager (~250 LOC)
- MeteringManager (~300 LOC)

**Result:** AudioBridge becomes thin coordinator (~500 LOC)

## Benefits

- **Testability** - Each module unit testable in isolation
- **Clarity** - Clear boundaries and single responsibilities
- **Maintainability** - Changes localized to specific modules
- **AI-Friendly** - Each module fits in LLM context windows
- **Thread Safety** - Easier to verify lock-free correctness

## Detailed Documentation

See **[docs/issues/audiobridge-refactoring.md](docs/issues/audiobridge-refactoring.md)** for:
- Complete responsibility analysis
- Module specifications with estimated LOC
- Phase-by-phase implementation plan
- Risk mitigation strategies
- Testing approach
- Thread safety considerations
- Success criteria

## Related Issues

- Issue #1: MIDI activity tracking fails for track IDs >= 128 (will be fixed in MidiActivityMonitor)
- See `BUG_ANALYSIS_REPORT.md` for other AudioBridge issues

## Next Steps

1. Review and approve refactoring approach
2. Start with Phase 1 (low risk, pure data managers)
3. Create PR for each module extraction
4. Verify tests pass and no performance regression
5. Continue with subsequent phases
```

3. Add labels: `refactoring`, `architecture`, `code-quality`, `audio`
4. Optionally add to project board or milestone
5. Submit

## Alternative: Use GitHub CLI

```bash
# Install gh CLI if not already installed
# https://cli.github.com/

# Create issue from file
gh issue create \
  --repo Conceptual-Machines/magda-core \
  --title "Refactor: AudioBridge into Focused Modules" \
  --body-file docs/issues/audiobridge-refactoring.md \
  --label "refactoring,architecture,code-quality,audio"
```

## Tips

- **Link to detailed doc**: Always reference `docs/issues/audiobridge-refactoring.md` for full details
- **Keep it actionable**: Focus on the high-level plan in the issue, details in the doc
- **Update both**: When implementation progresses, update both the issue and the doc
- **Track progress**: Use issue checkboxes to track which phases/modules are complete

## After Creating

1. Link the issue number back in `docs/issues/audiobridge-refactoring.md`
2. Reference it in any related PRs
3. Update progress as modules are extracted
4. Close when AudioBridge is successfully refactored
