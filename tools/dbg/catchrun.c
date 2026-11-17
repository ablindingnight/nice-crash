// catchrun: run a program under a minimal debugger and, when it dies (a fast fail such as 0xC0000409 that no
// in-process handler sees, or any second-chance exception), print the faulting thread's registers, its frame-pointer
// chain and the return-address-looking words on its stack. Symbolise the output with tools/dbg/symbolize.py.
// Build (32-bit, like the game): zig cc -target x86-windows-gnu -O2 tools/dbg/catchrun.c -o build/catchrun.exe
// Usage: catchrun.exe <program> [args...]   (stdin/stdout/stderr and the environment are inherited)
#include <windows.h>
#include <stdio.h>
#include <string.h>

static int Read(HANDLE p, DWORD a, void* out, SIZE_T n) {
    SIZE_T got = 0;
    return ReadProcessMemory(p, (LPCVOID)(ULONG_PTR)a, out, n, &got) && got == n;
}

static void Dump(HANDLE proc, HANDLE th, const EXCEPTION_RECORD* er, DWORD tid) {
    CONTEXT c;
    memset(&c, 0, sizeof c);
    c.ContextFlags = CONTEXT_FULL;
    GetThreadContext(th, &c);
    fprintf(stderr, "[catchrun] thread %lu exception %08lx at %08lx (params", tid, er->ExceptionCode,
            (DWORD)(ULONG_PTR)er->ExceptionAddress);
    for (DWORD i = 0; i < er->NumberParameters && i < 4; i++) fprintf(stderr, " %08lx", (DWORD)er->ExceptionInformation[i]);
    fprintf(stderr, ")\n[catchrun] eip %08lx esp %08lx ebp %08lx eax %08lx ebx %08lx ecx %08lx edx %08lx esi %08lx edi %08lx\n",
            c.Eip, c.Esp, c.Ebp, c.Eax, c.Ebx, c.Ecx, c.Edx, c.Esi, c.Edi);
    DWORD ebp = c.Ebp;
    for (int i = 0; i < 48 && ebp; i++) {
        DWORD fr[2];
        if (!Read(proc, ebp, fr, sizeof fr)) break;
        fprintf(stderr, "[catchrun] frame %2d ret %08lx\n", i, fr[1]);
        if (fr[0] <= ebp) break;
        ebp = fr[0];
    }
    static DWORD stack[2048];
    if (Read(proc, c.Esp, stack, sizeof stack) || Read(proc, c.Esp, stack, 1024)) {
        for (int i = 0; i < 2048; i++)
            if (stack[i] >= 0x00401000 && stack[i] < 0x02000000)
                fprintf(stderr, "[catchrun] stack +%04x %08lx\n", i * 4, stack[i]);
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: catchrun <program> [args...]\n");
        return 2;
    }
    char cmd[8192] = "";
    for (int i = 1; i < argc; i++) {
        strcat(cmd, "\"");
        strcat(cmd, argv[i]);
        strcat(cmd, "\" ");
    }
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, DEBUG_ONLY_THIS_PROCESS, NULL, NULL, &si, &pi)) {
        fprintf(stderr, "catchrun: cannot start %s (%lu)\n", argv[1], GetLastError());
        return 2;
    }
    HANDLE threads[4096];
    DWORD tids[4096];
    int nth = 0;
    threads[nth] = pi.hThread, tids[nth++] = pi.dwThreadId;
    DWORD exitCode = 0;
    for (;;) {
        DEBUG_EVENT ev;
        if (!WaitForDebugEvent(&ev, INFINITE)) break;
        DWORD cont = DBG_CONTINUE;
        switch (ev.dwDebugEventCode) {
        case CREATE_THREAD_DEBUG_EVENT:
            if (nth < 4096) threads[nth] = ev.u.CreateThread.hThread, tids[nth++] = ev.dwThreadId;
            break;
        case OUTPUT_DEBUG_STRING_EVENT: {  // OutputDebugString (drivers explain themselves here)
            char s[1024];
            DWORD n = ev.u.DebugString.nDebugStringLength;
            if (n > sizeof s - 1) n = sizeof s - 1;
            if (!ev.u.DebugString.fUnicode && Read(pi.hProcess, (DWORD)(ULONG_PTR)ev.u.DebugString.lpDebugStringData, s, n)) {
                s[n] = 0;
                fprintf(stderr, "[catchrun] debug: %s%s", s, n && s[n - 1] == 10 ? "" : "\n");
            }
            break;
        }
        case LOAD_DLL_DEBUG_EVENT:
            if (ev.u.LoadDll.hFile) CloseHandle(ev.u.LoadDll.hFile);
            break;
        case CREATE_PROCESS_DEBUG_EVENT:
            if (ev.u.CreateProcessInfo.hFile) CloseHandle(ev.u.CreateProcessInfo.hFile);
            break;
        case EXCEPTION_DEBUG_EVENT: {
            const EXCEPTION_RECORD* er = &ev.u.Exception.ExceptionRecord;
            DWORD code = er->ExceptionCode;
            if (code == EXCEPTION_BREAKPOINT || code == 0x4000001F /* WOW64 breakpoint */ ||
                code == 0x406D1388 /* thread name */ || code == 0x40010006 /* debug print */ ||
                code == 0x4001000A) {
                cont = DBG_CONTINUE;
                break;
            }
            int fatal = !ev.u.Exception.dwFirstChance || code == 0xC0000409 || code == 0xC0000374;
            if (fatal) {
                HANDLE th = NULL;
                for (int i = 0; i < nth; i++)
                    if (tids[i] == ev.dwThreadId) th = threads[i];
                if (th) Dump(pi.hProcess, th, er, ev.dwThreadId);
                fflush(stderr);
            }
            cont = DBG_EXCEPTION_NOT_HANDLED;
            break;
        }
        case EXIT_PROCESS_DEBUG_EVENT:
            exitCode = ev.u.ExitProcess.dwExitCode;
            fprintf(stderr, "[catchrun] exit %08lx\n", exitCode);
            ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, DBG_CONTINUE);
            return (int)exitCode;
        }
        ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, cont);
    }
    return 1;
}
