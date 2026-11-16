// Nice Crash launcher work (see tasks.h). Windows only: CreateProcess, job objects, the shell.
#define WIN32_LEAN_AND_MEAN
#include "tasks.h"
#include <windows.h>
#include <shellapi.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <algorithm>
#include "dumpcheck.h"
#include "portconfig.h"
#include "project.h"
#include "settings.h"

namespace tasks {

static std::wstring W(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
static std::string A(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
static bool IsFile(const std::string& p) {
    DWORD a = GetFileAttributesA(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
static bool IsDir(const std::string& p) {
    DWORD a = GetFileAttributesA(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
static std::string Full(const std::string& p) {
    char b[MAX_PATH * 4];
    DWORD n = GetFullPathNameA(p.c_str(), sizeof b, b, nullptr);
    std::string r = n ? std::string(b, n) : p;
    while (r.size() > 3 && (r.back() == '\\' || r.back() == '/')) r.pop_back();
    return r;
}
static double Now() { return GetTickCount64() / 1000.0; }

std::string ExeDir() {
    char p[MAX_PATH];
    GetModuleFileNameA(nullptr, p, MAX_PATH);
    std::string s = p;
    return s.substr(0, s.find_last_of("\\/") + 1);
}

std::string RepoRoot() {
    std::string r = Full(ExeDir() + "..");
    return IsFile(r + "\\tools\\gametrial\\link.py") ? r : "";
}

std::string GameExe() {
    if (const char* e = getenv("NICECRASH_GAME_EXE")) return e;  // debugging: run a game built elsewhere
    return ExeDir() + "NiceCrashGame.exe";
}

std::string LogDir() {
    std::string d = portcfg::Dir() + "\\logs";
    CreateDirectoryA(d.c_str(), nullptr);
    return d;
}
std::string LogPath() { return LogDir() + "\\nicecrash_log.txt"; }

std::wstring FormatBytes(long long n) {
    wchar_t b[32];
    if (n >= (1LL << 30)) swprintf(b, 32, L"%.2f GB", n / 1073741824.0);
    else if (n >= (1LL << 20)) swprintf(b, 32, L"%.0f MB", n / 1048576.0);
    else swprintf(b, 32, L"%lld KB", n / 1024);
    return b;
}

std::string ShortHash(const std::string& h) {
    if (h.size() < 8) return h;
    return h.substr(0, 4) + "\xE2\x80\xA6" + h.substr(h.size() - 4);
}

static long long DirSize(const std::string& d, int depth = 0) {
    if (depth > 12) return 0;
    long long sum = 0;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((d + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) sum += DirSize(d + "\\" + fd.cFileName, depth + 1);
        else sum += ((long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return sum;
}

DumpInfo InspectDump(const std::string& path, bool measure) {
    DumpInfo d;
    d.path = Full(path);
    if (!LocateDump(d.path, d.files, d.dol, d.why)) {
        if (!IsDir(d.path) && !IsFile(d.path)) d.why = "The folder isn't there any more: " + d.path;
        else if (d.why.find("no tocres.res") != std::string::npos)
            d.why = "No game files in this folder. Choose the folder that has sys and files in it.";
        return d;
    }
    // Show the dump by its root: <root>\DATA\files (whole disc), <root>\files, or a files folder picked directly.
    {
        std::string root = Full(d.files + "\\..");
        size_t slash = root.find_last_of('\\');
        std::string base = slash == std::string::npos ? root : root.substr(slash + 1);
        if (!_stricmp(base.c_str(), "DATA") && slash != std::string::npos && slash > 2) root = root.substr(0, slash);
        d.path = root;
    }
    d.gameId = DumpGameId(d.files);
    DumpCheck c = CheckDump(d.files, d.dol);
    d.sha1 = c.sha1;
    if (c.sha1.empty()) {
        d.why = "main.dol is missing (sys\\main.dol). Dump or extract your disc again.";
        return d;
    }
    d.found = true;
    d.verified = c.verified;
    if (!d.verified)
        d.why = "This disc doesn't match the retail USA game (REXE01), so Nice Crash can't run it. "
                "Other regions aren't supported yet.";
    if (measure) {
        std::string root = Full(d.files + "\\..");
        d.bytes = DirSize(root);
    }
    return d;
}

static std::string FileDate(const std::string& p) {
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExA(p.c_str(), GetFileExInfoStandard, &a)) return "";
    FILETIME lt;
    SYSTEMTIME st;
    FileTimeToLocalFileTime(&a.ftLastWriteTime, &lt);
    FileTimeToSystemTime(&lt, &st);
    char b[64];
    static const char* mon[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    snprintf(b, sizeof b, "%d %s %d, %02d:%02d", st.wDay, mon[(st.wMonth + 11) % 12], st.wYear, st.wHour, st.wMinute);
    return b;
}

State Refresh() {
    State s;
    std::string path = Settings_Get("launcher.dump_path");
    portcfg::Values v;
    bool haveIni = portcfg::Read(portcfg::DumpIniPath(), v);
    if (path.empty() && haveIni && !v["disc"].empty()) path = Full(v["disc"] + "\\..");
    if (!path.empty()) {
        s.haveDump = true;
        s.dump = InspectDump(path, false);
        s.imported = haveIni && s.dump.found && _stricmp(Full(v["disc"]).c_str(), Full(s.dump.files).c_str()) == 0 &&
                     v["verified"] == "1";
    }
    s.built = IsFile(GameExe());
    if (s.built) s.builtWhen = FileDate(GameExe());
    std::string root = RepoRoot();
    if (!root.empty()) {
        FILE* f = fopen((root + "\\tools\\gametrial\\decomp_ref.txt").c_str(), "r");
        if (f) {
            char b[128] = {};
            if (fgets(b, sizeof b, f)) s.decompRef = std::string(b).substr(0, 7);
            fclose(f);
        }
    }
    return s;
}

// ---------------------------------------------------------------- disc search

static CRITICAL_SECTION s_lock;
static struct LockInit { LockInit() { InitializeCriticalSection(&s_lock); } } s_lockInit;
struct Lk {
    Lk() { EnterCriticalSection(&s_lock); }
    ~Lk() { LeaveCriticalSection(&s_lock); }
};

static bool s_scanRunning = false;
static std::vector<Candidate> s_cands;
static std::wstring s_scanSummary;

static void FindDumps(const std::string& dir, int depth, std::vector<std::string>& out) {
    if (depth < 0 || out.size() > 20) return;
    if (IsFile(dir + "\\sys\\main.dol") || IsFile(dir + "\\DATA\\sys\\main.dol") || IsFile(dir + "\\files\\tocres.res") ||
        IsFile(dir + "\\DATA\\files\\tocres.res")) {
        out.push_back(dir);
        return;
    }
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
            continue;
        if (fd.cFileName[0] == '.' || fd.cFileName[0] == '$') continue;
        if (!_stricmp(fd.cFileName, "node_modules") || !_stricmp(fd.cFileName, "AppData")) continue;
        FindDumps(dir + "\\" + fd.cFileName, depth - 1, out);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

Candidate JudgeDump(const std::string& path) {
    Candidate c;
    c.info = InspectDump(path, true);
    std::string id = c.info.gameId;
    if (c.info.verified) {
        c.kind = 0;
        c.title = L"Wii disc, USA (REXE01)";
    } else if (c.info.found) {
        c.kind = 1;
        c.title = id.empty() ? L"Wii disc (unknown game)"
                             : id.substr(0, 4) == "REXP" ? L"Wii disc, Europe (" + W(id) + L")"
                             : id.substr(0, 4) == "REXJ" ? L"Wii disc, Japan (" + W(id) + L")"
                                                         : L"Wii disc (" + W(id) + L")";
        c.why = id.substr(0, 3) == "REX" && id != "REXE01"
                    ? L"Nice Crash is built from the USA version only. Other regions may come later."
                    : L"This isn't the retail USA disc. Nice Crash only runs REXE01.";
    } else {
        c.kind = 2;
        c.title = id == "REXE01" ? L"Wii disc, USA (REXE01)" : L"Disc folder";
        c.why = W(c.info.why);
    }
    return c;
}

static DWORD WINAPI ScanThread(void*) {
    std::vector<std::string> roots, found;
    const char* home = getenv("USERPROFILE");
    if (home) {
        for (const char* sub : {"Downloads", "Desktop", "Documents"}) roots.push_back(std::string(home) + "\\" + sub);
        roots.push_back(std::string(home) + "\\OneDrive\\Desktop");
        roots.push_back(std::string(home) + "\\OneDrive\\Documents");
    }
    std::string last = Settings_Get("launcher.dump_path");
    portcfg::Values v;
    if (last.empty() && portcfg::Read(portcfg::DumpIniPath(), v) && !v["disc"].empty()) last = Full(v["disc"] + "\\..");
    if (!last.empty()) {
        if (Full(last).size() > 3) FindDumps(Full(last), 0, found);
        FindDumps(Full(last + "\\.."), 1, found);
    }
    for (auto& r : roots)
        if (IsDir(r)) FindDumps(r, 3, found);
    // A whole-disc extraction (DATA\files) can also show up as its DATA folder; keep the outermost.
    std::vector<std::string> uniq;
    for (auto& f : found) {
        std::string ff = Full(f);
        bool dup = false;
        for (auto& u : uniq)
            if (!_stricmp(u.c_str(), ff.c_str()) ||
                (ff.size() > u.size() && !_strnicmp(ff.c_str(), u.c_str(), u.size()) && ff[u.size()] == '\\'))
                dup = true;
        if (!dup) uniq.push_back(ff);
    }
    std::vector<Candidate> cands;
    for (auto& u : uniq) {
        Candidate c = JudgeDump(u);  // normalises the path to the dump's root, so check for repeats again
        bool dup = false;
        for (auto& e : cands) dup |= !_stricmp(e.info.path.c_str(), c.info.path.c_str());
        if (!dup) cands.push_back(c);
    }
    std::stable_sort(cands.begin(), cands.end(), [](const Candidate& a, const Candidate& b) { return a.kind < b.kind; });
    wchar_t sum[128];
    swprintf(sum, 128, L"Searched Downloads, Desktop, Documents · %d found", (int)cands.size());
    Lk lk;
    s_cands = cands;
    s_scanSummary = sum;
    s_scanRunning = false;
    return 0;
}

void StartDiscScan() {
    {
        Lk lk;
        if (s_scanRunning) return;
        s_scanRunning = true;
        s_scanSummary = L"Searching Downloads, Desktop, Documents…";
    }
    HANDLE t = CreateThread(nullptr, 0, ScanThread, nullptr, 0, nullptr);
    if (t) CloseHandle(t);
}
bool DiscScanRunning() { Lk lk; return s_scanRunning; }
std::vector<Candidate> DiscScanResults() { Lk lk; return s_cands; }
std::wstring DiscScanSummary() { Lk lk; return s_scanSummary; }

// ---------------------------------------------------------------- child processes

static HANDLE s_job = nullptr;          // the checklist's current child (killed on cancel)
static volatile LONG s_cancel = 0;

static std::wstring Quote(const std::wstring& a) {
    if (a.find_first_of(L" \t\"") == std::wstring::npos && !a.empty()) return a;
    std::wstring r = L"\"";
    for (wchar_t c : a) {
        if (c == L'"') r += L'\\';
        r += c;
    }
    return r + L"\"";
}

static std::string FindOnPath(const char* exe) {
    char b[MAX_PATH];
    DWORD n = SearchPathA(nullptr, exe, nullptr, MAX_PATH, b, nullptr);
    return n && n < MAX_PATH ? std::string(b) : "";
}

static std::wstring EnvBlock(const std::vector<std::pair<std::string, std::string>>& set) {
    std::vector<std::wstring> vars;
    wchar_t* env = GetEnvironmentStringsW();
    for (wchar_t* p = env; *p; p += wcslen(p) + 1) vars.push_back(p);
    FreeEnvironmentStringsW(env);
    for (auto& kv : set) {
        std::wstring k = W(kv.first) + L"=";
        vars.erase(std::remove_if(vars.begin(), vars.end(),
                                  [&](const std::wstring& v) { return !_wcsnicmp(v.c_str(), k.c_str(), k.size()); }),
                   vars.end());
        if (!kv.second.empty()) vars.push_back(k + W(kv.second));
    }
    std::sort(vars.begin(), vars.end(), [](const std::wstring& a, const std::wstring& b) {
        return _wcsicmp(a.c_str(), b.c_str()) < 0;
    });
    std::wstring block;
    for (auto& v : vars) { block += v; block += L'\0'; }
    block += L'\0';
    return block;
}

// Runs a command, feeding each output line to onLine. Returns the exit code, or -1 if it couldn't start.
template <class F>
static int RunCapture(const std::wstring& cmd, const std::string& cwd,
                      const std::vector<std::pair<std::string, std::string>>& env, F onLine) {
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE rd, wr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return -1;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nul;
    si.hStdOutput = wr;
    si.hStdError = wr;
    PROCESS_INFORMATION pi{};
    std::wstring c = cmd, envb = EnvBlock(env), wcwd = W(cwd);
    HANDLE job = CreateJobObjectA(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{};
    li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof li);
    BOOL ok = CreateProcessW(nullptr, &c[0], nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED, (void*)envb.c_str(),
                             wcwd.empty() ? nullptr : wcwd.c_str(), &si, &pi);
    CloseHandle(wr);
    CloseHandle(nul);
    if (!ok) {
        CloseHandle(rd);
        CloseHandle(job);
        return -1;
    }
    AssignProcessToJobObject(job, pi.hProcess);
    {
        Lk lk;
        s_job = job;
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    std::string buf;
    char tmp[4096];
    DWORD n;
    while (ReadFile(rd, tmp, sizeof tmp, &n, nullptr) && n) {
        buf.append(tmp, n);
        size_t nl;
        while ((nl = buf.find_first_of("\r\n")) != std::string::npos) {
            std::string line = buf.substr(0, nl);
            buf.erase(0, nl + 1);
            if (!line.empty()) onLine(line);
        }
    }
    if (!buf.empty()) onLine(buf);
    CloseHandle(rd);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    {
        Lk lk;
        s_job = nullptr;
    }
    CloseHandle(job);
    return (int)code;
}

// ---------------------------------------------------------------- checklist

static Checklist s_check;

static void SetStep(int i, StepState st, const std::wstring& detail = L"\x01", float progress = -2) {
    Lk lk;
    Step& s = s_check.steps[i];
    if (st == ACTIVE && s.state != ACTIVE) s.t0 = Now();
    if ((st == DONE || st == FAILED || st == SKIPPED) && s.state == ACTIVE) s.t1 = Now();
    if ((st == DONE || st == SKIPPED) && s.state == PENDING) s.t0 = s.t1 = Now();
    s.state = st;
    if (detail != L"\x01") s.detail = detail;
    if (progress != -2) s.progress = progress;
}
static void AddLog(const std::string& line) {
    Lk lk;
    s_check.log.push_back(line);
    if (s_check.log.size() > 400) s_check.log.erase(s_check.log.begin(), s_check.log.begin() + 100);
}
static void Fail(int i, const std::wstring& why) {
    SetStep(i, FAILED, why);
    LogLine("[launcher] %s: %s", A(s_check.steps[i].title).c_str(), A(why).c_str());
    Lk lk;
    s_check.failed = true;
}

struct CheckArgs {
    std::string dump;
    bool rebuild;
};

static bool Cancelled(int step) {
    if (!s_cancel) return false;
    SetStep(step, FAILED, L"Cancelled. Your current game stays as it is.");
    Lk lk;
    s_check.cancelled = true;
    return true;
}

static DWORD WINAPI CheckThread(void* p) {
    CheckArgs args = *(CheckArgs*)p;
    delete (CheckArgs*)p;
    enum { FIND, VERIFY, TOOLS, SOURCE, INDEX, BUILD, SHADERS, READY };
    std::string root = RepoRoot();
    bool haveGame = IsFile(GameExe());
    bool needBuild = args.rebuild || !haveGame;

    // 1. Find the dump.
    SetStep(FIND, ACTIVE);
    AddLog("[launcher] looking at " + args.dump);
    DumpInfo d = InspectDump(args.dump, true);
    if (!d.found && d.sha1.empty() && d.files.empty()) { Fail(FIND, W(d.why)); goto out; }
    if (!d.found) { Fail(FIND, W(d.why)); goto out; }
    SetStep(FIND, DONE, W(d.path) + L" (extracted folder, " + FormatBytes(d.bytes) + L")");
    if (Cancelled(VERIFY)) goto out;

    // 2. Check it's the retail USA disc.
    SetStep(VERIFY, ACTIVE);
    AddLog("[launcher] main.dol sha1 " + d.sha1);
    if (!d.verified) {
        Fail(VERIFY, L"This disc doesn't match the retail USA game, so Nice Crash can't run it.");
        goto out;
    }
    SetStep(VERIFY, DONE, L"main.dol SHA-1 matches REXE01 " + W(ShortHash(d.sha1)));
    if (Cancelled(TOOLS)) goto out;

    // 3. Build tools. A player install will unpack the bundled zig and Python here (handoff Q14); a source
    // checkout uses the developer's own.
    if (!needBuild) {
        SetStep(TOOLS, SKIPPED, L"Not needed: the game is already built");
    } else {
        SetStep(TOOLS, ACTIVE);
        std::string py = FindOnPath("python.exe");
        std::string zig = getenv("ZIG") ? getenv("ZIG") : FindOnPath("zig.exe");
        if (zig.empty()) {  // trial.py also finds a winget zig; it reports its own error if that fails too
            const char* la = getenv("LOCALAPPDATA");
            if (la && IsDir(std::string(la) + "\\Microsoft\\WinGet\\Packages")) zig = "(winget)";
        }
        if (py.empty() || zig.empty()) {
            Fail(TOOLS, py.empty() ? L"Python wasn't found on this PC. The player download will bundle it."
                                   : L"zig wasn't found on this PC. The player download will bundle it.");
            goto out;
        }
        SetStep(TOOLS, DONE, L"Using zig and Python already on this PC (developer setup)");
    }
    if (Cancelled(SOURCE)) goto out;

    // 4. Source.
    if (!needBuild) {
        SetStep(SOURCE, SKIPPED, L"Not needed: the game is already built");
    } else if (root.empty()) {
        Fail(SOURCE, L"The Nice Crash source isn't on this PC. Downloads arrive with the update host.");
        goto out;
    } else {
        State st = Refresh();
        SetStep(SOURCE, DONE,
                L"Using the local source" + (st.decompRef.empty() ? L"" : L" · decomp " + W(st.decompRef)));
    }
    if (Cancelled(INDEX)) goto out;

    // 5. Index the game files with the importer (writes dump.ini and sets.tsv).
    {
        SetStep(INDEX, ACTIVE, L"Reading every resource set and checking its CRCs", -1);
        std::string imp = ExeDir() + "import.exe";
        if (!IsFile(imp)) { Fail(INDEX, L"import.exe is missing next to the launcher."); goto out; }
        std::string summary, lastErr;
        int code = RunCapture(Quote(W(imp)) + L" " + Quote(W(d.path)), ExeDir(), {}, [&](const std::string& l) {
            AddLog("[import] " + l);
            if (l.rfind("sets:", 0) == 0) summary = l;
            if (l.rfind("error:", 0) == 0) lastErr = l.substr(7);
        });
        if (s_cancel) { Cancelled(INDEX); goto out; }
        if (code != 0) {
            Fail(INDEX, code == -1 ? L"Couldn't start import.exe." : W(lastErr.empty() ? "The importer failed." : lastErr));
            goto out;
        }
        int ok = 0, bad = 0;
        long long res = 0;
        sscanf(summary.c_str(), "sets: %d ok, %d failed, %lld resources", &ok, &bad, &res);
        wchar_t b[128];
        swprintf(b, 128, L"%d resource sets, %lld resources, every CRC checked", ok, res);
        SetStep(INDEX, DONE, b, -1);
        Settings_Set("launcher.dump_path", d.path.c_str());
    }
    if (Cancelled(BUILD)) goto out;

    // 6. Build.
    if (!needBuild) {
        State st = Refresh();
        SetStep(BUILD, SKIPPED, W("Already built (" + st.builtWhen + ")"));
    } else {
        SetStep(BUILD, ACTIVE, L"Compiling the decomp into a PC game. This takes a few minutes.", -1);
        FILETIME before{};
        WIN32_FILE_ATTRIBUTE_DATA a;
        if (GetFileAttributesExA(GameExe().c_str(), GetFileExInfoStandard, &a)) before = a.ftLastWriteTime;
        std::string py = FindOnPath("python.exe");
        std::wstring cmd = Quote(W(py)) + L" -I " + Quote(W(root + "\\tools\\gametrial\\link.py"));
        std::string last;
        int code = RunCapture(cmd, root, {{"NICECRASH_DOL", d.dol}, {"PYTHONUNBUFFERED", "1"}}, [&](const std::string& l) {
            AddLog("[build] " + l);
            last = l;
            if (l.size() < 120) SetStep(BUILD, ACTIVE, W(l));
        });
        if (s_cancel) { Cancelled(BUILD); goto out; }
        bool newer = GetFileAttributesExA(GameExe().c_str(), GetFileExInfoStandard, &a) &&
                     CompareFileTime(&a.ftLastWriteTime, &before) > 0;
        if (code != 0 || !newer) {
            LogLine("[launcher] build failed (exit %d): %s", code, last.c_str());
            Fail(BUILD, L"Build failed. Show details for the log.");
            goto out;
        }
        SetStep(BUILD, DONE, L"Built " + W(Refresh().builtWhen));
    }

    // 7. Shaders: the port builds them as the game runs.
    SetStep(SHADERS, SKIPPED, L"Not needed: shaders are made as the game runs");
    SetStep(READY, DONE, L"Press PLAY to start.");
out: {
    Lk lk;
    s_check.running = false;
    s_check.finished = true;
}
    return 0;
}

void StartChecklist(const std::string& dumpPath, bool rebuild) {
    {
        Lk lk;
        if (s_check.running) return;
        s_check = Checklist();
        s_check.title = rebuild ? L"Rebuilding Nice Crash" : L"Getting Nice Crash ready";
        s_check.sub = L"We read your disc dump and build the game on this PC. This takes a few minutes.";
        const wchar_t* titles[] = {L"Find your disc dump", L"Check it's the retail USA disc", L"Unpack the build tools",
                                   L"Get the Nice Crash source", L"Index the game files", L"Build Nice Crash",
                                   L"Prepare the shaders", L"Ready to play"};
        for (auto t : titles) {
            Step s;
            s.title = t;
            s_check.steps.push_back(s);
        }
        s_check.running = true;
        s_cancel = 0;
    }
    HANDLE t = CreateThread(nullptr, 0, CheckThread, new CheckArgs{dumpPath, rebuild}, 0, nullptr);
    if (t) CloseHandle(t);
}

void CancelChecklist() {
    InterlockedExchange(&s_cancel, 1);
    Lk lk;
    if (s_job) TerminateJobObject(s_job, 1);
}

Checklist Snapshot() { Lk lk; return s_check; }

// ---------------------------------------------------------------- the game

static HANDLE s_game = nullptr;
static HWND s_notify = nullptr;
static UINT s_notifyMsg = 0;

void SetGameExitWindow(void* hwnd, unsigned msg) {
    s_notify = (HWND)hwnd;
    s_notifyMsg = msg;
}

bool GameRunning() { Lk lk; return s_game != nullptr; }

void StopGame() {
    Lk lk;
    if (s_game) TerminateProcess(s_game, 1);
}

static std::string RunsPath() { return LogDir() + "\\runs.tsv"; }

std::vector<Run> Runs() {
    std::vector<Run> r;
    FILE* f = fopen(RunsPath().c_str(), "r");
    if (!f) return r;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        Run x;
        char when[64] = {}, status[64] = {};
        int logged = 0;
        if (sscanf(line, "%63[^\t]\t%d\t%x\t%63[^\t]\t%d", when, &x.seconds, &x.code, status, &logged) >= 4) {
            x.when = when;
            x.status = status;
            x.logged = logged != 0;
            r.push_back(x);
        }
    }
    fclose(f);
    std::reverse(r.begin(), r.end());
    if (r.size() > 10) r.resize(10);
    return r;
}

static void AddRun(const Run& x) {
    std::vector<Run> r = Runs();
    std::reverse(r.begin(), r.end());
    r.push_back(x);
    while (r.size() > 10) r.erase(r.begin());
    std::string tmp = RunsPath() + ".tmp";
    FILE* f = fopen(tmp.c_str(), "w");
    if (!f) return;
    for (auto& e : r)
        fprintf(f, "%s\t%d\t%08x\t%s\t%d\n", e.when.c_str(), e.seconds, e.code, e.status.c_str(), e.logged ? 1 : 0);
    fclose(f);
    MoveFileExA(tmp.c_str(), RunsPath().c_str(), MOVEFILE_REPLACE_EXISTING);
}

bool LastRunFailed(Run* run) {
    std::vector<Run> r = Runs();
    if (r.empty() || r[0].status == "OK") return false;
    if (run) *run = r[0];
    return true;
}

std::wstring DescribeExit(unsigned code) {
    wchar_t b[96];
    switch (code) {
    case 0: return L"OK";
    case 0xC0000005: return L"Crashed (access violation)";
    case 0xC0000094: return L"Crashed (divide by zero)";
    case 0xC00000FD: return L"Crashed (stack overflow)";
    case 0xC000001D: return L"Crashed (illegal instruction)";
    case 0x80000003: return L"Crashed (breakpoint)";
    case 0xC0000409: return L"Crashed (stopped by a check)";
    // The port's own exits (runtime/, sdk/).
    case 94: return L"Stopped: couldn't read main.dol";
    case 95: return L"Stopped: no disc configured";
    case 96: return L"Stopped: the game reported a fatal error";
    case 97: return L"Stopped: the game panicked";
    case 98: return L"Stopped: couldn't set up the Wii memory map";
    case 99: return L"Stopped: reached a part of the port not written yet";
    }
    if (code >= 0xC0000000) swprintf(b, 96, L"Crashed (0x%08X)", code);
    else swprintf(b, 96, L"Stopped (exit code %u)", code);
    return b;
}

static void Rotate() {
    std::string p = LogPath();
    DeleteFileA((p + ".4").c_str());
    for (int i = 3; i >= 1; i--)
        MoveFileExA((p + "." + std::to_string(i)).c_str(), (p + "." + std::to_string(i + 1)).c_str(), MOVEFILE_REPLACE_EXISTING);
    MoveFileExA(p.c_str(), (p + ".1").c_str(), MOVEFILE_REPLACE_EXISTING);
}

static std::string Stamp() {
    time_t t = time(nullptr);
    char b[32];
    strftime(b, sizeof b, "%Y-%m-%d %H:%M:%S", localtime(&t));
    return b;
}

void LogLine(const char* fmt, ...) {
    if (!Settings_GetInt("launcher.error_log")) return;
    FILE* f = fopen(LogPath().c_str(), "a");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    fprintf(f, "%s ", Stamp().c_str());
    vfprintf(f, fmt, ap);
    fprintf(f, "\n");
    va_end(ap);
    fclose(f);
}

struct GameWait {
    HANDLE proc;
    double t0;
    std::string when;
    bool logged;
};

static DWORD WINAPI WaitThread(void* p) {
    GameWait w = *(GameWait*)p;
    delete (GameWait*)p;
    WaitForSingleObject(w.proc, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(w.proc, &code);
    Run r;
    r.when = w.when;
    r.seconds = (int)(Now() - w.t0 + 0.5);
    r.code = code;
    r.status = code == 0 ? "OK" : code >= 0xC0000000 ? "Crashed" : "Stopped";
    r.logged = w.logged;
    AddRun(r);
    if (w.logged) LogLine("[launcher] game exited after %d s: %s", r.seconds, A(DescribeExit(code)).c_str());
    {
        Lk lk;
        CloseHandle(s_game);
        s_game = nullptr;
    }
    if (s_notify) PostMessageA(s_notify, s_notifyMsg, code, 0);
    return 0;
}

bool StartGame(std::string& err) {
    if (GameRunning()) { err = "The game is already running."; return false; }
    std::string exe = GameExe();
    if (!IsFile(exe)) { err = "NiceCrashGame.exe isn't built yet."; return false; }
    Settings_Flush();
    // The game reads the console's aspect at boot from NICECRASH_ASPECT (sdk/sys.cpp). "native" is the 16:9 game
    // mode; the host's stretch to the window shape comes with the live settings (handoff step 2).
    std::string aspect = Settings_Get("display.aspect");
    std::vector<std::pair<std::string, std::string>> env = {
        {"NICECRASH_ASPECT", aspect == "4:3" ? "4:3" : "16:9"},
        {"NICECRASH_LAUNCHED", "1"},
    };
    bool logOn = Settings_GetInt("launcher.error_log") != 0;
    env.push_back({"NICECRASH_ERROR_LOG", logOn ? "1" : "0"});

    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE out = INVALID_HANDLE_VALUE;
    if (logOn) {
        Rotate();
        FILE* f = fopen(LogPath().c_str(), "w");
        if (f) {
            fprintf(f, "%s %s %s, launcher %s\n", Stamp().c_str(), PROJECT_NAME, NICECRASH_VERSION, LAUNCHER_VERSION);
            fprintf(f, "%s [launcher] starting %s (aspect %s)\n", Stamp().c_str(), "NiceCrashGame.exe", aspect.c_str());
            fclose(f);
        }
        out = CreateFileA(LogPath().c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_ALWAYS,
                          FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    HANDLE nul = CreateFileA("NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                             OPEN_EXISTING, 0, nullptr);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nul;
    si.hStdOutput = nul;  // the game's progress chatter; errors and warnings go to stderr
    si.hStdError = out != INVALID_HANDLE_VALUE ? out : nul;
    PROCESS_INFORMATION pi{};
    std::wstring cmd = Quote(W(exe)), envb = EnvBlock(env);
    std::string root = RepoRoot();
    std::wstring cwd = W(root.empty() ? ExeDir() : root);  // the game's debug outputs are relative to the checkout
    BOOL ok = CreateProcessW(W(exe).c_str(), &cmd[0], nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, (void*)envb.c_str(), cwd.c_str(), &si, &pi);
    DWORD e = GetLastError();
    if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
    CloseHandle(nul);
    if (!ok) {
        char b[64];
        snprintf(b, sizeof b, "Couldn't start the game (Windows error %lu).", e);
        err = b;
        LogLine("[launcher] %s", b);
        return false;
    }
    CloseHandle(pi.hThread);
    {
        Lk lk;
        s_game = pi.hProcess;
    }
    time_t t = time(nullptr);
    char when[32];
    strftime(when, sizeof when, "%Y-%m-%d %H:%M", localtime(&t));
    HANDLE dup;
    DuplicateHandle(GetCurrentProcess(), pi.hProcess, GetCurrentProcess(), &dup, 0, FALSE, DUPLICATE_SAME_ACCESS);
    HANDLE th = CreateThread(nullptr, 0, WaitThread, new GameWait{dup, Now(), when, logOn}, 0, nullptr);
    if (th) CloseHandle(th);
    return true;
}

// ---------------------------------------------------------------- reports

static void ReplaceAll(std::string& s, const std::string& from, const std::string& to) {
    if (from.empty()) return;
    for (size_t i = 0; (i = s.find(from, i)) != std::string::npos; i += to.size()) s.replace(i, from.size(), to);
}

std::string BugReport() {
    State st = Refresh();
    std::string r;
    r += std::string(PROJECT_NAME) + " bug report\n";
    r += "version " NICECRASH_VERSION ", launcher " LAUNCHER_VERSION "\n";
    r += "build: " + (st.built ? st.builtWhen : std::string("not built")) +
         (st.decompRef.empty() ? "" : ", decomp " + st.decompRef) + "\n";
    r += "dump: " + std::string(st.dump.verified ? "verified REXE01" : "not verified") + ", main.dol sha1 " +
         (st.dump.sha1.empty() ? "-" : st.dump.sha1) + "\n";
    std::vector<Run> runs = Runs();
    r += "\n## Last runs\n";
    for (auto& x : runs)
        r += x.when + "  " + std::to_string(x.seconds) + " s  " + A(DescribeExit(x.code)) + "\n";
    r += "\n## settings.ini (changed values only)\n" + settings::FileText();
    r += "\n## nicecrash_log.txt (last 200 lines)\n";
    FILE* f = fopen(LogPath().c_str(), "r");
    if (f) {
        std::vector<std::string> lines;
        char b[2048];
        while (fgets(b, sizeof b, f)) {
            lines.push_back(b);
            if (lines.size() > 200) lines.erase(lines.begin());
        }
        fclose(f);
        for (auto& l : lines) r += l;
    } else {
        r += "(no log: \"Save error logs\" is off or the game hasn't run yet)\n";
    }
    // Remove the player's home folder and name.
    if (const char* home = getenv("USERPROFILE")) {
        ReplaceAll(r, home, "%USERPROFILE%");
        std::string h2 = home;
        for (auto& c : h2) if (c == '\\') c = '/';
        ReplaceAll(r, h2, "%USERPROFILE%");
    }
    if (const char* user = getenv("USERNAME")) if (strlen(user) >= 3) ReplaceAll(r, user, "<user>");
    return r;
}

bool CopyToClipboard(void* hwnd, const std::string& text) {
    std::wstring w = W(text);
    if (!OpenClipboard((HWND)hwnd)) return false;
    EmptyClipboard();
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t));
    if (!mem) { CloseClipboard(); return false; }
    memcpy(GlobalLock(mem), w.c_str(), (w.size() + 1) * sizeof(wchar_t));
    GlobalUnlock(mem);
    SetClipboardData(CF_UNICODETEXT, mem);
    CloseClipboard();
    return true;
}

void OpenPath(const std::string& path) {
    ShellExecuteW(nullptr, L"open", W(path).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

}  // namespace tasks
