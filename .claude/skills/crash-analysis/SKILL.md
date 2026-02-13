---
name: crash-analysis
description: Analyze macOS crash logs for MAGDA. Use when the user reports a crash, provides a crash log, or asks to investigate a crash. Parses .ips and .crash files to extract only the relevant thread.
---

# Crash Analysis

## Parsing Crash Logs

**Never read crash log files directly** — they are huge and waste context. Use the parser script:

```bash
# Extract crashed thread only (default)
.claude/skills/crash-analysis/parse-crash.sh <crash-file>

# Extract all threads (when you need more context)
.claude/skills/crash-analysis/parse-crash.sh <crash-file> --all-threads
```

Supports both `.ips` (JSON, modern macOS) and `.crash` (text, older) formats.

## Finding Crash Logs

macOS crash logs are stored at:
```
~/Library/Logs/DiagnosticReports/MAGDA-*.ips
```

To find the most recent crash:
```bash
ls -t ~/Library/Logs/DiagnosticReports/MAGDA-*.ips | head -1
```

## Analysis Workflow

1. Parse the crash log with the script
2. Identify the crash type (SIGSEGV, SIGABRT, EXC_BAD_ACCESS, etc.)
3. Look at the top frames for MAGDA code (source file + line number are included)
4. If the crash is in a third-party plugin (e.g. "Kick 3", "Serum"), note it — host can't fix plugin bugs
5. If the crash is in JUCE internals, check what MAGDA code triggered it (look further down the stack)
6. Search the codebase for the relevant source file and line to understand the bug

## Common Crash Patterns

| Signal | Meaning | Typical Cause |
|--------|---------|---------------|
| SIGSEGV (EXC_BAD_ACCESS) | Null/dangling pointer | Use-after-free, null deref |
| SIGABRT | Assertion/abort | JUCE assertion, malloc corruption, std::abort |
| EXC_BAD_INSTRUCTION | Illegal instruction | Undefined behavior, bad vtable |
| EXC_BREAKPOINT | Debugger trap | __builtin_trap, Swift precondition |

### Plugin crashes during shutdown
If the crash is in a plugin dylib during `exit()` / `__cxa_finalize_ranges`, it's a buggy plugin static destructor. The `_exit(0)` workaround in `magda_daw_main.cpp` should prevent these.

### Timer callback crashes
Crashes in `juce::Timer::TimerThread::callTimers()` often mean a component was deleted while its timer was still running. Check that `stopTimer()` is called in destructors.

### Audio thread crashes
Crashes in threads named "JUCE Audio" or "Tracktion" are audio-thread issues. Common causes: allocating memory, locking mutexes, or accessing deleted objects from the audio callback.
