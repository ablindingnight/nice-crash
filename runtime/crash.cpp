// Crash reporter: an unhandled exception prints the exception, the faulting address and a symbolised x86 stack
// (dbghelp.dll from Windows reads the PDB lld-link writes next to the exe). Installed before the game's static
// constructors run.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
#include <cstdlib>

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
    {
        const CONTEXT* c = ep->ContextRecord;
        fprintf(stderr, "  eax %08lx ebx %08lx ecx %08lx edx %08lx esi %08lx edi %08lx ebp %08lx esp %08lx\n", c->Eax,
                c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
        const unsigned char* ip = (const unsigned char*)c->Eip;
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery(ip, &mbi, sizeof mbi) && mbi.State == MEM_COMMIT) {
            fprintf(stderr, "  code at eip:");
            for (int i = 0; i < 12; i++) fprintf(stderr, " %02x", ip[i]);
            fprintf(stderr, "\n");
        }
    }
    {
        // A jump through a bad pointer leaves EIP outside any code: list the stack's likely return addresses.
        HMODULE mod = GetModuleHandleA(nullptr);
        IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)((char*)mod + ((IMAGE_DOS_HEADER*)mod)->e_lfanew);
        uintptr_t lo = (uintptr_t)mod, hi = lo + nt->OptionalHeader.SizeOfImage;
        uintptr_t eip = ep->ContextRecord->Eip;
        if (eip < lo || eip >= hi) {
            fprintf(stderr, "  EIP is outside the exe; EBP chain:\n");
            uintptr_t* bp = (uintptr_t*)ep->ContextRecord->Ebp;
            for (int i = 0; i < 16 && bp; i++) {
                MEMORY_BASIC_INFORMATION mbi;
                if (!VirtualQuery(bp, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT) break;
                PrintFrame(proc, bp[1], i);
                bp = (uintptr_t*)bp[0];
            }
            fprintf(stderr, "  return addresses found on the stack:\n");
            uintptr_t* sp = (uintptr_t*)ep->ContextRecord->Esp;
            int found = 0;
            for (int i = 0; i < 512 && found < 12; i++) {
                MEMORY_BASIC_INFORMATION mbi;
                if (!VirtualQuery(sp + i, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT) break;
                if (sp[i] >= lo && sp[i] < hi) {
                    PrintFrame(proc, sp[i], found++);
                }
            }
        }
    }
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
    ULONG guarantee = 64 * 1024;  // stack reserved for the handler, so a stack overflow is reported too
    SetThreadStackGuarantee(&guarantee);
    SetUnhandledExceptionFilter(OnCrash);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
}

// NICECRASH_STACKDUMP=<seconds>: after that long, print the main thread's stack (where is it spinning?).
static HANDLE g_mainThread;
static DWORD WINAPI Watchdog(void* p) {
    Sleep((DWORD)(uintptr_t)p * 1000);
    HANDLE proc = GetCurrentProcess();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    SymInitialize(proc, nullptr, TRUE);
    SuspendThread(g_mainThread);
    CONTEXT ctx{};
    ctx.ContextFlags = CONTEXT_FULL;
    GetThreadContext(g_mainThread, &ctx);
    fprintf(stderr, "\n[stackdump] main thread after %u s:\n", (unsigned)(uintptr_t)p);
#ifdef _M_IX86
    STACKFRAME64 sf{};
    sf.AddrPC.Offset = ctx.Eip;
    sf.AddrPC.Mode = AddrModeFlat;
    sf.AddrFrame.Offset = ctx.Ebp;
    sf.AddrFrame.Mode = AddrModeFlat;
    sf.AddrStack.Offset = ctx.Esp;
    sf.AddrStack.Mode = AddrModeFlat;
    for (int i = 0; i < 24; i++) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_I386, proc, g_mainThread, &sf, &ctx, nullptr, SymFunctionTableAccess64,
                         SymGetModuleBase64, nullptr) || !sf.AddrPC.Offset)
            break;
        PrintFrame(proc, sf.AddrPC.Offset, i);
    }
#endif
    fflush(stderr);
    ResumeThread(g_mainThread);
    return 0;
}

__attribute__((constructor(103))) static void InstallWatchdog() {
    const char* s = getenv("NICECRASH_STACKDUMP");
    if (!s) return;
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_mainThread, 0, FALSE,
                    DUPLICATE_SAME_ACCESS);
    CreateThread(nullptr, 0, Watchdog, (void*)(uintptr_t)atoi(s), 0, nullptr);
}

// port_watch(addr): debugging aid. Puts a 4-byte hardware write watch (DR0) on addr in every thread of the process;
// the first write prints the writer's stack and exits.
#include <tlhelp32.h>
static DWORD WINAPI SetWatch(void* addr) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te{};
    te.dwSize = sizeof te;
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != GetCurrentProcessId() || te.th32ThreadID == GetCurrentThreadId()) continue;
        HANDLE t = OpenThread(THREAD_ALL_ACCESS, FALSE, te.th32ThreadID);
        if (!t) continue;
        SuspendThread(t);
        CONTEXT c{};
        c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        GetThreadContext(t, &c);
        c.Dr0 = (DWORD)(uintptr_t)addr;
        c.Dr7 = (c.Dr7 & ~0xF0003u) | 1u | (1u << 16) | (3u << 18);  // L0, write, 4 bytes
        SetThreadContext(t, &c);
        ResumeThread(t);
        CloseHandle(t);
    }
    CloseHandle(snap);
    return 0;
}

static int g_watchAny = 1;
static unsigned g_watchValue, g_watchLo, g_watchHi;
static LONG WINAPI OnWatch(EXCEPTION_POINTERS* ep) {
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP || !(ep->ContextRecord->Dr6 & 1))
        return EXCEPTION_CONTINUE_SEARCH;
    {
        unsigned v = *(volatile unsigned*)(uintptr_t)ep->ContextRecord->Dr0;
        if (g_watchAny == 0 && v != g_watchValue) return EXCEPTION_CONTINUE_EXECUTION;
        if (g_watchAny == 2 && v >= g_watchLo && v < g_watchHi) return EXCEPTION_CONTINUE_EXECUTION;
        fprintf(stderr, "\n[watch] write to %08lx, new value %08x\n", (unsigned long)ep->ContextRecord->Dr0, v);
    }
    OnCrash(ep);
    ExitProcess(97);
}

extern "C" void port_watch(void* addr);
// Like port_watch, but only stops when the watched word leaves [lo, hi).
extern "C" void port_watch_range(void* addr, unsigned lo, unsigned hi) {
    g_watchAny = 2;
    g_watchLo = lo;
    g_watchHi = hi;
    port_watch(addr);
}

// Like port_watch, but only stops when the watched word becomes `value`.
extern "C" void port_watch_value(void* addr, unsigned value) {
    g_watchAny = 0;
    g_watchValue = value;
    port_watch(addr);
}

extern "C" void port_watch(void* addr) {
    static bool installed;
    if (!installed) AddVectoredExceptionHandler(1, OnWatch), installed = true;
    HANDLE h = CreateThread(nullptr, 0, SetWatch, addr, 0, nullptr);
    WaitForSingleObject(h, INFINITE);
    CloseHandle(h);
}

// port_stack_here(why): debugging aid, prints the calling thread's stack (symbolised) without stopping.
extern "C" void port_stack_here(const char* why) {
    HANDLE proc = GetCurrentProcess();
    static bool init;
    if (!init) {
        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
        SymInitialize(proc, nullptr, TRUE);
        init = true;
    }
    void* frames[24];
    USHORT n = RtlCaptureStackBackTrace(1, 24, frames, nullptr);
    fprintf(stderr, "[stack] %s\n", why);
    for (USHORT i = 0; i < n; i++) PrintFrame(proc, (DWORD64)(uintptr_t)frames[i], i);
    fflush(stderr);
}
