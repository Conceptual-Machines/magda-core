---
name: crash-analysis
description: Analyze MAGDA crash logs and Windows minidumps. Use when the user reports a crash, provides a crash log or .dmp file, or asks to investigate a crash.
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

---

## Windows Crash Analysis (.dmp files)

Windows users send `.dmp` minidump files. Use the scripts below — **never read .dmp files directly**.

### Quick parse (no symbols)

```bash
python3 .claude/skills/crash-analysis/parse-minidump.py /path/to/file.dmp
```

### With PDB symbolization (function names + offsets)

From v0.4.6 onwards, each release includes `MAGDA-X.Y.Z-Windows-x86_64.pdb`.
Download it from the GitHub release assets, then:

```bash
python3 .claude/skills/crash-analysis/parse-minidump.py file.dmp --pdb MAGDA-X.Y.Z-Windows-x86_64.pdb
```

### Semi-automated analysis (parse + Claude + optional Linear issue)

Requires: `pip install anthropic`

```bash
# Parse + Claude analysis (downloads PDB automatically for known versions)
ANTHROPIC_API_KEY=... GITHUB_TOKEN=... \
  python3 .claude/skills/crash-analysis/analyze-crash.py \
    --dump file.dmp --version 0.4.6

# Also create a Linear issue
ANTHROPIC_API_KEY=... GITHUB_TOKEN=... LINEAR_API_KEY=... LINEAR_TEAM_ID=... \
  python3 .claude/skills/crash-analysis/analyze-crash.py \
    --dump file.dmp --version 0.4.6 --create-issue

# With a local PDB (skips download)
ANTHROPIC_API_KEY=... \
  python3 .claude/skills/crash-analysis/analyze-crash.py \
    --dump file.dmp --pdb MAGDA.pdb
```

### Reading the output

| Field | What to look for |
|-------|-----------------|
| Exception code | `0xC0000005` = access violation (most common) |
| AV address | `0x0` = null deref; `0xFFFFFFFFFFFFFFFF` = sentinel/corruption |
| Rcx register | In x64, this is `this` — garbage here means the object is corrupt/freed |
| Stack scan | MAGDA.exe offsets narrow the subsystem; with PDB they resolve to function names |

### Common Windows crash patterns

| Pattern | Likely cause |
|---------|-------------|
| AV read at 0x0 | Null pointer dereference |
| AV read at 0xFFFFFFFFFFFFFFFF | Dangling `std::list` iterator or sentinel value used as pointer |
| Rcx = ASCII/UTF bytes | String buffer being used as `this` pointer — memory corruption or bad cast |
| Rbp = small integer (0, 1, 2) | Stack frame pointer corrupted — look for buffer overflows |
| Crash on project reopen | Serialization/deserialization bug, often with non-ASCII paths on Windows |
