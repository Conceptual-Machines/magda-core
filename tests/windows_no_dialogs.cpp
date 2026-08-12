// Keeps the Windows test binaries from ever blocking on a modal dialog.
//
// A Debug build links the debug CRT, whose diagnostics (failed assert, invalid
// parameter, heap corruption caught at free) default to a MessageBox with
// Abort/Retry/Ignore. On a headless CI runner nobody clicks it, so the process
// sits on the dialog until the job's timeout kills it -- the run reports
// "timed out after 5 minutes" and the actual message is never printed, because
// it went to a window instead of stderr.
//
// That is how the Windows CI hang of July 2026 presented: the suite went silent
// partway through and a minidump of the stuck process contained "HEAP
// CORRUPTION DETECTED ... wrote to memory before start of heap buffer" -- a
// buffer underrun in faust's pathToContent, reached only on the wasm backend.
//
// So: route every CRT report to stderr, and make the fatal ones exit instead of
// returning into a corrupt program. Static init runs before any test, and the
// diagnostics we care about all fire well after it.

#ifdef _WIN32

    #include <cstdio>
    #include <cstdlib>

    #ifdef _DEBUG
        #include <crtdbg.h>
    #endif

    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>

    // Must come after windows.h.
    #include <dbghelp.h>
    #pragma comment(lib, "dbghelp.lib")

namespace {

// Prints the current call stack to stderr, symbolized from the PDB next to the
// exe when one exists (module+offset otherwise). Called when a fatal CRT
// diagnostic fires: the stack of the allocation that tripped the check is the
// single most useful fact for finding the code that corrupted the heap.
void printStackTrace() {
    void* frames[62];
    const USHORT count = CaptureStackBackTrace(0, 62, frames, nullptr);

    const HANDLE process = GetCurrentProcess();
    SymSetOptions(SymGetOptions() | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    const bool symbols = SymInitialize(process, nullptr, TRUE) != 0;

    std::fputs("--- stack at CRT report ---\n", stderr);
    for (USHORT i = 0; i < count; ++i) {
        const auto address = reinterpret_cast<DWORD64>(frames[i]);

        char symbolBuffer[sizeof(SYMBOL_INFO) + 256] = {};
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;

        DWORD64 displacement = 0;
        if (symbols && SymFromAddr(process, address, &displacement, symbol) != 0) {
            IMAGEHLP_LINE64 line = {};
            line.SizeOfStruct = sizeof(line);
            DWORD lineDisplacement = 0;
            if (SymGetLineFromAddr64(process, address, &lineDisplacement, &line) != 0) {
                std::fprintf(stderr, "  %2u: %s +0x%llx  [%s:%lu]\n", i, symbol->Name,
                             static_cast<unsigned long long>(displacement), line.FileName,
                             line.LineNumber);
            } else {
                std::fprintf(stderr, "  %2u: %s +0x%llx\n", i, symbol->Name,
                             static_cast<unsigned long long>(displacement));
            }
            continue;
        }

        HMODULE module = nullptr;
        char moduleName[MAX_PATH] = {};
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(address), &module) != 0 &&
            GetModuleFileNameA(module, moduleName, MAX_PATH) != 0) {
            std::fprintf(
                stderr, "  %2u: %s+0x%llx\n", i, moduleName,
                static_cast<unsigned long long>(address - reinterpret_cast<DWORD64>(module)));
        } else {
            std::fprintf(stderr, "  %2u: 0x%llx\n", i, static_cast<unsigned long long>(address));
        }
    }
    std::fputs("--- end stack ---\n", stderr);
}

    #ifdef _DEBUG
// Returning TRUE means "handled": _CrtDbgReport skips its default reporting,
// which is what suppresses the dialog.
int crtReportHook(int reportType, char* message, int* returnValue) {
    if (message != nullptr) {
        std::fputs(message, stderr);
    }

    if (reportType == _CRT_ERROR || reportType == _CRT_ASSERT) {
        printStackTrace();
        std::fputs("\nmagda tests: fatal CRT diagnostic (see above), exiting 3\n", stderr);
        // Flush every stream, not just stderr. _Exit below skips the CRT's own
        // flushing, and the test runner's stdout is block-buffered whenever it
        // is redirected to a file or a CI log -- without this, the report
        // arrives with no record of which tests had run, which is most of what
        // makes it actionable.
        std::fflush(nullptr);
        // _Exit, not abort(): abort() in a debug build routes back through the
        // CRT's own fault reporting, and static destructors would run over
        // whatever state just tripped the diagnostic.
        std::_Exit(3);
    }

    std::fflush(stderr);
    if (returnValue != nullptr) {
        *returnValue = 0;  // Warnings: carry on.
    }
    return TRUE;
}
    #endif

struct DialogSuppressor {
    DialogSuppressor() {
        // No "application stopped working" box, and no drive-not-ready prompts.
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);

        // Silence abort()'s own message box, and keep it from handing off to WER.
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);

    #ifdef _DEBUG
        const int reportTypes[] = {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT};
        for (const int reportType : reportTypes) {
            _CrtSetReportMode(reportType, _CRTDBG_MODE_FILE);
            _CrtSetReportFile(reportType, _CRTDBG_FILE_STDERR);
        }
        _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, crtReportHook);

        // Opt-in heap forensics. By default the debug heap only inspects a
        // block's guard bytes when that block is freed, so a stray write is
        // reported wherever the victim happens to be released, which can be far
        // from the code at fault. This walks the whole heap on every allocation
        // and free, pinning the report to the first allocation after the bad
        // write. Slow: prefer running it over a subset of the suite.
        if (const char* checkHeap = std::getenv("MAGDA_TEST_HEAP_CHECK");
            checkHeap != nullptr && checkHeap[0] == '1') {
            _CrtSetDbgFlag(_CrtSetDbgFlag(_CRTDBG_REPORT_FLAG) | _CRTDBG_ALLOC_MEM_DF |
                           _CRTDBG_CHECK_ALWAYS_DF);
            std::fputs("magda tests: MAGDA_TEST_HEAP_CHECK=1, checking the heap on every "
                       "alloc/free\n",
                       stderr);
        }
    #endif
    }
};

const DialogSuppressor suppressor;

}  // namespace

#endif  // _WIN32
