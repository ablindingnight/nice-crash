// Crash reporter: an unhandled exception prints the exception, the faulting address and a symbolised x86 stack
// (dbghelp.dll from Windows reads the PDB lld-link writes next to the exe). Installed before the game's static
// constructors run.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <cstring>

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
        // Registers pointing into the mapped Wii memory: show the first words (vptrs, fields) of what they point at.
        const DWORD regs[] = {c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi};
        const char* rn[] = {"eax", "ebx", "ecx", "edx", "esi", "edi"};
        for (int r = 0; r < 6; r++) {
            if (regs[r] < 0x80000000u || regs[r] >= 0x94000000u) continue;
            const unsigned* w = (const unsigned*)(uintptr_t)regs[r];
            fprintf(stderr, "  [%s] %08lx:", rn[r], regs[r]);
            for (int i = 0; i < 8; i++) fprintf(stderr, " %08x", w[i]);
            char sb[sizeof(SYMBOL_INFO) + 256];
            SYMBOL_INFO* si = (SYMBOL_INFO*)sb;
            si->SizeOfStruct = sizeof(SYMBOL_INFO);
            si->MaxNameLen = 255;
            DWORD64 disp = 0;
            if (SymFromAddr(proc, w[0], &disp, si))  // the vptr's table
                fprintf(stderr, "  (vptr -> %s+0x%llx)", si->Name, (unsigned long long)disp);
            fprintf(stderr, "\n");
        }
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

// abort() (the C runtime ends the process with 0xC0000409, past the exception filter): print the stack first.
static void OnAbort(int) {
    CONTEXT c{};
    RtlCaptureContext(&c);
    EXCEPTION_RECORD er{};
    er.ExceptionCode = 0xC0000409;
    er.ExceptionAddress = (void*)(uintptr_t)
#ifdef _M_IX86
        c.Eip;
#else
        c.Rip;
#endif
    EXCEPTION_POINTERS ep{&er, &c};
    fprintf(stderr, "\n[crash] abort() called\n");
    OnCrash(&ep);
    fflush(stderr);
    _exit(3);
}

// The C runtime's invalid-parameter check (a bad FILE*, handle or buffer passed to a CRT function) also ends the
// process with 0xC0000409 and no exception: print the stack first.
static void OnInvalidParam(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) {
    fprintf(stderr, "\n[crash] C runtime: invalid parameter\n");
    OnAbort(0);
}

__attribute__((constructor(101))) static void InstallCrashHandler() {
    _set_invalid_parameter_handler(OnInvalidParam);
    ULONG guarantee = 64 * 1024;  // stack reserved for the handler, so a stack overflow is reported too
    SetThreadStackGuarantee(&guarantee);
    SetUnhandledExceptionFilter(OnCrash);
    signal(SIGABRT, OnAbort);
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

// NICECRASH_FPTRAP=<retrace>: NaN hunting. From that frame on, invalid floating-point operations (0/0, inf-inf,
// NaN->int, x87 stack underflow) raise an exception; each new faulting instruction is logged once with its stack,
// the faulting thread's invalid-operation mask is restored for the retry (so it produces the NaN as before), and
// port_fptrap_rearm (called once per frame by the input code) unmasks again. Debugging aid only.
static thread_local bool t_fpStep;  // masked for one instruction; unmask again on the single-step
static LONG WINAPI OnFpTrap(EXCEPTION_POINTERS* ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
#ifdef _M_IX86
    if (code == EXCEPTION_SINGLE_STEP && t_fpStep) {
        t_fpStep = false;
        *(DWORD*)(ep->ContextRecord->ExtendedRegisters + 24) &= ~0xBFu;
        ep->ContextRecord->FloatSave.ControlWord &= ~1u;
        ep->ContextRecord->FloatSave.StatusWord &= ~0x80FFu;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
#endif
    if (code != STATUS_FLOAT_MULTIPLE_TRAPS && code != STATUS_FLOAT_INVALID_OPERATION &&
        code != STATUS_FLOAT_STACK_CHECK && code != STATUS_FLOAT_MULTIPLE_FAULTS)
        return EXCEPTION_CONTINUE_SEARCH;
#ifdef _M_IX86
    CONTEXT* c = ep->ContextRecord;
    static uintptr_t seen[256];
    static int nseen;
    uintptr_t eip = c->Eip;
    bool fresh = true;
    for (int i = 0; i < nseen; i++) fresh &= seen[i] != eip;
    if (fresh && nseen < 256) {
        seen[nseen++] = eip;
        {  // the handler itself runs with the trapping thread's FPU state: mask while symbolising
            unsigned short cw;
            __asm__ volatile("fnclex; fnstcw %0" : "=m"(cw));
            cw |= 1;
            __asm__ volatile("fldcw %0" : : "m"(cw));
            unsigned int csr;
            __asm__ volatile("stmxcsr %0" : "=m"(csr));
            csr |= 0x80u;
            __asm__ volatile("ldmxcsr %0" : : "m"(csr));
        }
        static bool init;
        HANDLE proc = GetCurrentProcess();
        if (!init) {
            SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
            SymInitialize(proc, nullptr, TRUE);
            init = true;
        }
        extern unsigned sdk_vi_retrace_count();
        fprintf(stderr, "[fptrap] 0x%08lx at retrace %u:\n", code, sdk_vi_retrace_count());
        {
            const float* x = (const float*)(c->ExtendedRegisters + 160);  // xmm0..7, low lanes
            fprintf(stderr, "  xmm:");
            for (int i = 0; i < 8; i++) fprintf(stderr, " %d=%g", i, x[i * 4]);
            const unsigned char* ip = (const unsigned char*)c->Eip;
            fprintf(stderr, "\n  code:");
            for (int i = 0; i < 8; i++) fprintf(stderr, " %02x", ip[i]);
            fprintf(stderr, "\n");
        }
        CONTEXT ctx = *c;
        STACKFRAME64 sf{};
        sf.AddrPC.Offset = ctx.Eip;
        sf.AddrPC.Mode = AddrModeFlat;
        sf.AddrFrame.Offset = ctx.Ebp;
        sf.AddrFrame.Mode = AddrModeFlat;
        sf.AddrStack.Offset = ctx.Esp;
        sf.AddrStack.Mode = AddrModeFlat;
        for (int i = 0; i < 6; i++) {
            if (!StackWalk64(IMAGE_FILE_MACHINE_I386, proc, GetCurrentThread(), &sf, &ctx, nullptr,
                             SymFunctionTableAccess64, SymGetModuleBase64, nullptr) || !sf.AddrPC.Offset)
                break;
            PrintFrame(proc, sf.AddrPC.Offset, i);
        }
        fflush(stderr);
    }
    // Mask invalid again for the retry: SSE MXCSR (in the extended registers, offset 24) and the x87 control word;
    // clear the x87 exception flags so the pending exception is not raised again.
    *(DWORD*)(c->ExtendedRegisters + 24) |= 0x80;
    *(DWORD*)(c->ExtendedRegisters + 24) &= ~0x3Fu;
    c->FloatSave.ControlWord |= 1;
    c->FloatSave.StatusWord &= ~0x80FFu;
    c->EFlags |= 0x100;  // trap after the retried instruction, then unmask again
    t_fpStep = true;
    return EXCEPTION_CONTINUE_EXECUTION;
#else
    return EXCEPTION_CONTINUE_SEARCH;
#endif
}

extern "C" void port_fptrap_rearm() {
    static bool installed;
    if (!installed) AddVectoredExceptionHandler(1, OnFpTrap), installed = true;
    unsigned short cw;
    __asm__ volatile("fnclex; fnstcw %0" : "=m"(cw));
    cw &= ~1;
    __asm__ volatile("fldcw %0" : : "m"(cw));
    unsigned int csr;
    __asm__ volatile("stmxcsr %0" : "=m"(csr));
    csr &= ~0x3Fu;
    csr &= ~0x80u;
    __asm__ volatile("ldmxcsr %0" : : "m"(csr));
}

// NICECRASH_PROF=<start s>:<seconds>: a sampling profiler. After <start> seconds it samples the instruction pointer
// of every thread of the process about once a millisecond for <seconds>, then prints the functions with the most
// samples (flat profile; samples in Windows wait functions are idle threads and listed apart).
#include <tlhelp32.h>
#include <mmsystem.h>
#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>
struct ProfSample { DWORD tid; DWORD pc; DWORD exe; };
static std::string ProfName(HANDLE proc, DWORD64 pc) {
    char buf[sizeof(SYMBOL_INFO) + 256];
    SYMBOL_INFO* si = (SYMBOL_INFO*)buf;
    si->SizeOfStruct = sizeof(SYMBOL_INFO);
    si->MaxNameLen = 255;
    DWORD64 disp = 0;
    std::string name = SymFromAddr(proc, pc, &disp, si) ? si->Name : "?";
    IMAGEHLP_MODULE64 mi{};
    mi.SizeOfStruct = sizeof mi;
    if (SymGetModuleInfo64(proc, pc, &mi)) name = std::string(mi.ModuleName) + "!" + name;
    return name.size() > 110 ? name.substr(0, 110) : name;
}
static DWORD WINAPI Profiler(void*) {
    int start = 0, dur = 0;
    if (sscanf(getenv("NICECRASH_PROF"), "%d:%d", &start, &dur) != 2 || dur <= 0) return 0;
    Sleep((DWORD)start * 1000);
    HANDLE proc = GetCurrentProcess();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(proc, nullptr, TRUE);
    // The exe's address range: each sample is also charged to the first frame on its stack inside the game exe
    // (driver and Windows time lands on the port function that called it).
    BYTE* base = (BYTE*)GetModuleHandleA(nullptr);
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
    DWORD lo = (DWORD)(uintptr_t)base, hi = lo + nt->OptionalHeader.SizeOfImage;
    timeBeginPeriod(1);
    std::vector<ProfSample> samples;
    samples.reserve((size_t)dur * 1000 * 16);  // no reallocation while a thread is suspended
    std::vector<std::pair<HANDLE, DWORD>> threads;
    DWORD self = GetCurrentThreadId(), pid = GetCurrentProcessId(), end = GetTickCount() + (DWORD)dur * 1000,
          refresh = 0;
    while ((int)(GetTickCount() - end) < 0) {
        if ((int)(GetTickCount() - refresh) >= 0) {
            for (auto& t : threads) CloseHandle(t.first);
            threads.clear();
            HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
            THREADENTRY32 te{};
            te.dwSize = sizeof te;
            for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te))
                if (te.th32OwnerProcessID == pid && te.th32ThreadID != self)
                    if (HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                                              FALSE, te.th32ThreadID))
                        threads.push_back({h, te.th32ThreadID});
            CloseHandle(snap);
            refresh = GetTickCount() + 500;
        }
        for (auto& t : threads) {
            if (SuspendThread(t.first) == (DWORD)-1) continue;
            CONTEXT c{};
            c.ContextFlags = CONTEXT_FULL;
            if (GetThreadContext(t.first, &c)) {
#ifdef _M_IX86
                // Frame-pointer chain from EBP (no allocation or locks while the thread is suspended: it may hold
                // the heap lock). Driver code without frame pointers is skipped up to the nearest caller using EBP.
                ProfSample s{t.second, c.Eip, 0};
                if (c.Eip >= lo && c.Eip < hi) {
                    s.exe = c.Eip;
                } else {
                    DWORD fp = c.Ebp;
                    for (int k = 0; k < 64 && fp > c.Esp && fp < c.Esp + 0x400000 && !(fp & 3); k++) {
                        DWORD ret = ((DWORD*)(uintptr_t)fp)[1], next = ((DWORD*)(uintptr_t)fp)[0];
                        if (ret >= lo && ret < hi) { s.exe = ret; break; }
                        if (next <= fp) break;
                        fp = next;
                    }
                }
                if (samples.size() < samples.capacity()) samples.push_back(s);
#endif
            }
            ResumeThread(t.first);
        }
        Sleep(1);
    }
    timeEndPeriod(1);
    std::unordered_map<DWORD, unsigned> perThread;
    for (auto& s : samples) perThread[s.tid]++;
    // Busy samples: not parked in a wait. A thread's "busy" share is what matters.
    auto idle = [&](const std::string& n) {
        return n.find("Wait") != std::string::npos || n.find("DelayExecution") != std::string::npos ||
               n.find("RemoveIoCompletion") != std::string::npos;
    };
    std::unordered_map<DWORD, std::string> nameCache;
    auto nameOf = [&](DWORD pc) -> const std::string& {
        auto it = nameCache.find(pc);
        if (it == nameCache.end()) it = nameCache.emplace(pc, ProfName(proc, pc)).first;
        return it->second;
    };
    fprintf(stderr, "[prof] %zu samples over %d s\n", samples.size(), dur);
    for (auto& pt : perThread) {
        std::unordered_map<std::string, unsigned> self_, incl;
        unsigned busy = 0;
        for (auto& s : samples) {
            if (s.tid != pt.first) continue;
            const std::string& n = nameOf(s.pc);
            if (idle(n)) {
                self_["(idle) " + n]++;
                incl["(idle) " + (s.exe ? nameOf(s.exe) : std::string("(no exe frame)"))]++;
                continue;
            }
            busy++;
            self_[n]++;
            incl[s.exe ? nameOf(s.exe) : "(no exe frame)"]++;
        }
        if (busy * 20 < pt.second) continue;  // mostly idle thread
        fprintf(stderr, "[prof] thread %lu: %u samples, %u busy (%.0f%%)\n", pt.first, pt.second, busy,
                100.0 * busy / pt.second);
        for (auto* m : {&self_, &incl}) {
            std::vector<std::pair<unsigned, std::string>> rows;
            for (auto& kv : *m) rows.push_back({kv.second, kv.first});
            std::sort(rows.rbegin(), rows.rend());
            fprintf(stderr, "[prof]   %s\n", m == &self_ ? "self:" : "by first game-exe frame:");
            for (size_t i = 0; i < rows.size() && i < 30; i++)
                fprintf(stderr, "[prof]   %6.2f%% %6u  %s\n", 100.0 * rows[i].first / pt.second, rows[i].first,
                        rows[i].second.c_str());
        }
    }
    // NICECRASH_PROFLINES=<substring>: busy samples inside functions whose name contains it, by source line.
    if (const char* want = getenv("NICECRASH_PROFLINES")) {
        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
        std::unordered_map<std::string, unsigned> lines;
        unsigned n = 0;
        for (auto& s : samples) {
            if (nameOf(s.pc).find(want) == std::string::npos) continue;
            IMAGEHLP_LINE64 line{};
            line.SizeOfStruct = sizeof line;
            DWORD ldisp = 0;
            char key[300];
            if (SymGetLineFromAddr64(proc, s.pc, &ldisp, &line)) {
                const char* f = strrchr(line.FileName, 92);  // backslash
                snprintf(key, sizeof key, "%s:%lu", f ? f + 1 : line.FileName, line.LineNumber);
            } else {
                snprintf(key, sizeof key, "%08lx", s.pc);
            }
            lines[key]++;
            n++;
        }
        std::vector<std::pair<unsigned, std::string>> rows;
        for (auto& kv : lines) rows.push_back({kv.second, kv.first});
        std::sort(rows.rbegin(), rows.rend());
        fprintf(stderr, "[prof] lines in *%s*: %u samples\n", want, n);
        for (size_t i = 0; i < rows.size() && i < 40; i++)
            fprintf(stderr, "[prof]   %6u  %s\n", rows[i].first, rows[i].second.c_str());
    }
    fflush(stderr);
    return 0;
}
__attribute__((constructor(102))) static void InstallProfiler() {
    if (getenv("NICECRASH_PROF")) CloseHandle(CreateThread(nullptr, 0, Profiler, nullptr, 0, nullptr));
}
