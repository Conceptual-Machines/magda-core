// Keeps the Windows test binaries from ever blocking on a modal dialog.
//
// A Debug build links the debug CRT, whose diagnostics (failed assert, invalid
// parameter, heap corruption caught at free) default to a MessageBox with
// Abort/Retry/Ignore. On a headless CI runner nobody clicks it, so the process
// sits on the dialog until the job's timeout kills it -- the run reports
// "timed out after 5 minutes" and the actual message is never printed, because
// it went to a window instead of stderr.
//
// That is how the Windows CI hang presented: the suite went silent after the
// Faust patch-metadata tests and a minidump of the stuck process contained
// "HEAP CORRUPTION DETECTED ... wrote to memory before start of heap buffer".
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

namespace {

    #ifdef _DEBUG
// Returning TRUE means "handled": _CrtDbgReport skips its default reporting,
// which is what suppresses the dialog.
int crtReportHook(int reportType, char* message, int* returnValue) {
    if (message != nullptr) {
        std::fputs(message, stderr);
    }

    if (reportType == _CRT_ERROR || reportType == _CRT_ASSERT) {
        std::fputs("\nmagda tests: fatal CRT diagnostic (see above), exiting 3\n", stderr);
        std::fflush(stderr);
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
    #endif
    }
};

const DialogSuppressor suppressor;

}  // namespace

#endif  // _WIN32
