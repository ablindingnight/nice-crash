// Host SDK: OS threads and mutexes with the Wii's single-core semantics.
//
// Every OSThread is a Windows thread, but only the thread holding the "CPU" lock runs game code, like the
// Wii's single Broadway core. A thread gives the CPU up only where a Wii thread would block: OSSleepTicks,
// waiting on a held OSMutex, and (later) message queues / VI retrace. So game code never sees two of its threads
// running at once, and OSDisableInterrupts sections stay safe.
// Simplification for now: resuming a higher-priority thread does not preempt the caller immediately; the new
// thread runs at the caller's next blocking point.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include "sdk.h"

static SRWLOCK g_cpu = SRWLOCK_INIT;
static CONDITION_VARIABLE g_changed = CONDITION_VARIABLE_INIT;  // a mutex was released / state changed

struct HostThread {
    void* wii;  // the game's OSThread object
    void* (*fn)(void*);
    void* arg;
    s32 prio;
    HANDLE handle;
    DWORD tid;
    bool started, done;
    void* result;
};

struct HostMutex {
    void* wii;
    DWORD owner;  // Windows thread id, 0 = free
    int count;
};

static HostThread g_threads[16];
static int g_nthreads;
static HostMutex g_mutexes[64];
static int g_nmutexes;

// Time the Wii CPU is held (the game working, as opposed to waiting): NICECRASH_FRAMETIME in sdk/gxrender.cpp.
static LARGE_INTEGER g_cpuSince;
static long long g_cpuBusy;
void sdk_cpu_acquire() {
    AcquireSRWLockExclusive(&g_cpu);
    QueryPerformanceCounter(&g_cpuSince);
}
void sdk_cpu_release() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    g_cpuBusy += now.QuadPart - g_cpuSince.QuadPart;
    ReleaseSRWLockExclusive(&g_cpu);
}
// Milliseconds the CPU has been held so far (called by the holder).
double sdk_cpu_busy_ms() {
    static LARGE_INTEGER f;
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (double)(g_cpuBusy + (now.QuadPart - g_cpuSince.QuadPart)) * 1000.0 / (double)f.QuadPart;
}

// Give the CPU away for `ms` milliseconds (0 = just let others run).
void sdk_cpu_yield(DWORD ms) {
    sdk_cpu_release();
    Sleep(ms);
    sdk_cpu_acquire();
}

static HostThread* FindThread(void* wii) {
    for (int i = 0; i < g_nthreads; i++)
        if (g_threads[i].wii == wii) return &g_threads[i];
    return nullptr;
}

static HostMutex* FindMutex(void* wii, bool create) {
    for (int i = 0; i < g_nmutexes; i++)
        if (g_mutexes[i].wii == wii) return &g_mutexes[i];
    if (!create || g_nmutexes == 64) return nullptr;
    HostMutex* m = &g_mutexes[g_nmutexes++];
    m->wii = wii;
    m->owner = 0;
    m->count = 0;
    return m;
}

static DWORD WINAPI ThreadMain(void* p) {
    HostThread* t = (HostThread*)p;
    sdk_cpu_acquire();
    sdk_log("thread %p (prio %d) starts", t->wii, t->prio);
    t->result = t->fn(t->arg);
    t->done = true;
    sdk_log("thread %p returned", t->wii);
    WakeAllConditionVariable(&g_changed);
    sdk_cpu_release();
    return 0;
}

extern "C" {

int OSCreateThread(void* thread, void* (*fn)(void*), void* arg, void* stack, u32 stackSize, s32 prio, u16 attr) {
    if (g_nthreads == 16) return 0;
    HostThread* t = FindThread(thread);
    if (!t) t = &g_threads[g_nthreads++];
    *t = HostThread{};
    t->wii = thread;
    t->fn = fn;
    t->arg = arg;
    t->prio = prio;
    sdk_log("OSCreateThread %p prio %d", thread, prio);
    return 1;
}

s32 OSResumeThread(void* thread) {
    HostThread* t = FindThread(thread);
    if (!t) return 0;
    if (!t->started) {
        t->started = true;
        t->handle = CreateThread(nullptr, 1 << 20, ThreadMain, t, 0, &t->tid);
        return 1;  // previous suspend count
    }
    return 0;
}

void OSSleepTicks(s64 ticks) {
    // Ticks run at bus clock / 4 = 60.75 MHz (runtime/wiimem.cpp sets the bus clock).
    s64 ms = ticks / 60750;
    sdk_cpu_yield(ms > 0 ? (DWORD)ms : 0);
}

void OSYieldThread(void) { sdk_cpu_yield(0); }

void OSInitMutex(void* mutex) {
    HostMutex* m = FindMutex(mutex, true);
    if (m) {
        m->owner = 0;
        m->count = 0;
    }
}

void OSLockMutex(void* mutex) {
    HostMutex* m = FindMutex(mutex, true);
    DWORD me = GetCurrentThreadId();
    while (m->owner && m->owner != me) SleepConditionVariableSRW(&g_changed, &g_cpu, INFINITE, 0);
    m->owner = me;
    m->count++;
}

int OSTryLockMutex(void* mutex) {
    HostMutex* m = FindMutex(mutex, true);
    DWORD me = GetCurrentThreadId();
    if (m->owner && m->owner != me) return 0;
    m->owner = me;
    m->count++;
    return 1;
}

void OSUnlockMutex(void* mutex) {
    HostMutex* m = FindMutex(mutex, false);
    if (!m || m->owner != GetCurrentThreadId()) return;
    if (--m->count == 0) {
        m->owner = 0;
        WakeAllConditionVariable(&g_changed);
    }
}

// ---- Message queues (used by the HOME menu's sound thread) ----
// The game's OSMessageQueue: two thread queues (unused on the host), then the ring of messages. A thread that
// blocks on a full or empty queue gives the CPU away until another thread changes it (as on the Wii).
struct OSMessageQueue {
    void* sendQueue[2];
    void* receiveQueue[2];
    void** msgArray;  // 0x10
    s32 msgCount;     // 0x14
    s32 firstIndex;   // 0x18
    s32 usedCount;    // 0x1C
};
enum { OS_MESSAGE_BLOCK = 1 };

void OSInitMessageQueue(OSMessageQueue* q, void** msgArray, s32 msgCount) {
    q->sendQueue[0] = q->sendQueue[1] = q->receiveQueue[0] = q->receiveQueue[1] = nullptr;
    q->msgArray = msgArray;
    q->msgCount = msgCount;
    q->firstIndex = 0;
    q->usedCount = 0;
}

int OSSendMessage(OSMessageQueue* q, void* msg, s32 flags) {
    while (q->usedCount >= q->msgCount) {
        if (!(flags & OS_MESSAGE_BLOCK)) return 0;
        SleepConditionVariableSRW(&g_changed, &g_cpu, INFINITE, 0);
    }
    q->msgArray[(q->firstIndex + q->usedCount) % q->msgCount] = msg;
    q->usedCount++;
    WakeAllConditionVariable(&g_changed);
    return 1;
}

int OSJamMessage(OSMessageQueue* q, void* msg, s32 flags) {
    while (q->usedCount >= q->msgCount) {
        if (!(flags & OS_MESSAGE_BLOCK)) return 0;
        SleepConditionVariableSRW(&g_changed, &g_cpu, INFINITE, 0);
    }
    q->firstIndex = (q->firstIndex + q->msgCount - 1) % q->msgCount;
    q->msgArray[q->firstIndex] = msg;
    q->usedCount++;
    WakeAllConditionVariable(&g_changed);
    return 1;
}

int OSReceiveMessage(OSMessageQueue* q, void** msg, s32 flags) {
    while (q->usedCount == 0) {
        if (!(flags & OS_MESSAGE_BLOCK)) return 0;
        SleepConditionVariableSRW(&g_changed, &g_cpu, INFINITE, 0);
    }
    if (msg) *msg = q->msgArray[q->firstIndex];
    q->firstIndex = (q->firstIndex + 1) % q->msgCount;
    q->usedCount--;
    WakeAllConditionVariable(&g_changed);
    return 1;
}

}  // extern "C"
