#define WIN32_LEAN_AND_MEAN
#include "portconfig.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include "project.h"

namespace portcfg {

std::string Dir() {
    const char* base = getenv("LOCALAPPDATA");
    std::string d = std::string(base ? base : ".") + "\\" + PROJECT_DIRNAME;
    CreateDirectoryA(d.c_str(), nullptr);
    return d;
}

std::string DumpIniPath() { return Dir() + "/dump.ini"; }
std::string SetIndexPath() { return Dir() + "/sets.tsv"; }

bool Read(const std::string& path, Values& out) {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return false;
    char line[2048];
    while (fgets(line, sizeof line, f)) {
        std::string s = line;
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        if (s.empty() || s[0] == '#' || s[0] == ';') continue;
        size_t eq = s.find('=');
        if (eq != std::string::npos) out[s.substr(0, eq)] = s.substr(eq + 1);
    }
    fclose(f);
    return true;
}

bool Write(const std::string& path, const Values& v, const char* header) {
    FILE* f = fopen(path.c_str(), "w");
    if (!f) return false;
    if (header) fprintf(f, "# %s\n", header);
    for (auto& kv : v) fprintf(f, "%s=%s\n", kv.first.c_str(), kv.second.c_str());
    fclose(f);
    return true;
}

}  // namespace portcfg
