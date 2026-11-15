// Crash reporter: an unhandled exception prints the exception, the faulting address and a symbolised x86 stack
// (dbghelp.dll from Windows reads the PDB lld-link writes next to the exe). Installed before the game's static
// constructors run.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>

static void PrintFrame(HANDLE proc, DWORD64 pc, int i) {
    char buf[sizeof(SYMBOL_INFO) + 256];
    SYMBOL_INFO* si = (SYMBOL_INFO*)buf;
    si->SizeOfStruct = sizeof(SYMBOL_INFO);
    si->MaxNameLen = 255;
    DWORD64 disp = 0;
    IMAGEHLP_LINE64 line{};
    line.SizeOfStruct = sizeof line;
    DWORD ldisp = 0;
    const char* name = SymFromAddr(proc, pc, &disp, si) ? si->Name : "?";
    if (SymGetLineFromAddr64(proc, pc, &ldisp, &line))
        fprintf(stderr, "  #%-2d %08llx %s+0x%llx  %s:%lu\n", i, (unsigned long long)pc, name,
                (unsigned long long)disp, line.FileName, line.LineNumber);
    else
        fprintf(stderr, "  #%-2d %08llx %s+0x%llx\n", i, (unsigned long long)pc, name, (unsigned long long)disp);
}

static LONG WINAPI OnCrash(EXCEPTION_POINTERS* ep) {
    HANDLE proc = GetCurrentProcess();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    SymInitialize(proc, nullptr, TRUE);
    EXCEPTION_RECORD* er = ep->ExceptionRecord;
    fprintf(stderr, "\n[crash] exception 0x%08lx at %p", er->ExceptionCode, er->ExceptionAddress);
    if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2)
        fprintf(stderr, " (%s address %p)", er->ExceptionInformation[0] ? "writing" : "reading",
                (void*)er->ExceptionInformation[1]);
    fprintf(stderr, "\n");
#ifdef _M_IX86
    CONTEXT ctx = *ep->ContextRecord;
    STACKFRAME64 sf{};
    sf.AddrPC.Offset = ctx.Eip;
    sf.AddrPC.Mode = AddrModeFlat;
    sf.AddrFrame.Offset = ctx.Ebp;
    sf.AddrFrame.Mode = AddrModeFlat;
    sf.AddrStack.Offset = ctx.Esp;
    sf.AddrStack.Mode = AddrModeFlat;
    for (int i = 0; i < 32; i++) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_I386, proc, GetCurrentThread(), &sf, &ctx, nullptr,
                         SymFunctionTableAccess64, SymGetModuleBase64, nullptr) || !sf.AddrPC.Offset)
            break;
        PrintFrame(proc, sf.AddrPC.Offset, i);
    }
#endif
    fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}

__attribute__((constructor(101))) static void InstallCrashHandler() {
    SetUnhandledExceptionFilter(OnCrash);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
}
