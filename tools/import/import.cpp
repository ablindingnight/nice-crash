// Dump importer (phase 0): point it at the player's own dump, verify it, index its RST0 sets, and write
// the config the host reads. The launcher (milestone 2.5) will put a face on this same code.
//
//   import <dump>            <dump> = an extracted disc folder. Accepted layouts:
//                              <dump>\DATA\files + <dump>\DATA\sys   (whole-disc extraction)
//                              <dump>\files + <dump>\sys             (one partition)
//                              <dump> = the files\ folder itself     (main.dol from ..\sys)
//   import <dump> --dol <main.dol>   use this main.dol for the check
//
// Disc images (.iso, .rvz, .wbfs, .ciso) are planned: they need a Wii disc reader (partition decryption with
// the player's own console key material is NOT something we ship), so for now extract the disc first with a
// tool such as Dolphin ("Extract Entire Disc").
//
// Writes %LOCALAPPDATA%\<PROJECT_DIRNAME>\dump.ini and sets.tsv. Never copies or modifies the dump.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>
#include "dumpcheck.h"
#include "portconfig.h"
#include "project.h"
#include "res.h"

int main(int argc, char** argv) {
    std::string dump, dol;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--dol" && i + 1 < argc) dol = argv[++i];
        else if (dump.empty() && a[0] != '-') dump = a;
        else { dump.clear(); break; }
    }
    printf("%s - %s: dump importer\n", PROJECT_NAME, PROJECT_TAGLINE);
    if (dump.empty()) {
        fprintf(stderr, "usage: import <extracted disc folder> [--dol main.dol]\n");
        return 2;
    }
    std::string files, why;
    if (!LocateDump(dump, files, dol, why)) {
        fprintf(stderr, "error: %s\n", why.c_str());
        return 2;
    }
    printf("disc files: %s\nmain.dol:   %s\n", files.c_str(), dol.c_str());

    // 1. Verify.
    DumpCheck dc = CheckDump(files, dol);
    if (dc.sha1.empty()) {
        fprintf(stderr, "error: cannot read %s\n", dc.dolPath.c_str());
        return 3;
    }
    printf("sha1:       %s\n", dc.sha1.c_str());
    if (!dc.verified) {
        fprintf(stderr,
                "error: this is not the supported disc. Only %s retail is supported (main.dol sha1 %s).\n"
                "Other regions/revisions are not supported yet; nothing was written.\n",
                DUMP_REGION_LABEL, DUMP_DOL_SHA1);
        return 3;
    }
    printf("verified:   yes (%s)\n", DUMP_REGION_LABEL);

    // 2. Index the RST0 sets through the ported loader (headers, TOCs and every resident CRC checked).
    res::System rs;
    if (!rs.Init(files)) {
        for (auto& l : rs.log) fprintf(stderr, "  %s\n", l.c_str());
        fprintf(stderr, "error: tocres.res failed to load\n");
        return 4;
    }
    std::vector<std::string> sets;
    WIN32_FIND_DATAA fd;
    for (const char* pat : {"*.res", "*.trk", "*.car", "*.sng"}) {
        HANDLE h = FindFirstFileA((files + "\\" + pat).c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do sets.push_back(fd.cFileName); while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    FILE* idx = fopen(portcfg::SetIndexPath().c_str(), "w");
    if (!idx) {
        fprintf(stderr, "error: cannot write %s\n", portcfg::SetIndexPath().c_str());
        return 5;
    }
    fprintf(idx, "set\tstatus\tresources\tload_size\tfile_size\ttoc\tcreated_utc\n");
    int good = 0, bad = 0;
    long long resources = 0;
    for (auto& sn : sets) {
        bool keep = rs.FindSet(sn) != nullptr;
        size_t logStart = rs.log.size();
        res::Set* s = rs.LoadSet(sn);
        if (s) {
            char when[32];
            time_t t = (time_t)s->hdr.createTime;
            strftime(when, sizeof when, "%Y-%m-%d %H:%M", gmtime(&t));
            fprintf(idx, "%s\tok\t%d\t%d\t%u\t%s\t%s\n", sn.c_str(), s->hdr.resCount, s->hdr.loadSize, s->hdr.fileSize,
                    (s->hdr.flags & 1) ? "tocres" : "internal", when);
            good++;
            resources += s->hdr.resCount;
            if (!keep) rs.UnloadSet(sn);
        } else {
            std::string msg = rs.log.size() > logStart ? rs.log.back() : "failed";
            fprintf(idx, "%s\tFAILED\t\t\t\t\t%s\n", sn.c_str(), msg.c_str());
            fprintf(stderr, "  set %s: %s\n", sn.c_str(), msg.c_str());
            bad++;
        }
    }
    fclose(idx);
    printf("sets:       %d ok, %d failed, %lld resources\n", good, bad, resources);
    if (bad) {
        fprintf(stderr, "error: %d resource sets failed their checks; the dump may be damaged. Config not written.\n", bad);
        return 4;
    }

    // 3. Write the config the host reads.
    char now[32];
    time_t t = time(nullptr);
    strftime(now, sizeof now, "%Y-%m-%d %H:%M:%S", localtime(&t));
    portcfg::Values v;
    v["disc"] = files;
    v["dol"] = dc.dolPath;
    v["dol_sha1"] = dc.sha1;
    v["verified"] = "1";
    v["region"] = DUMP_REGION_LABEL;
    v["sets"] = std::to_string(good);
    v["resources"] = std::to_string(resources);
    v["imported"] = now;
    if (!portcfg::Write(portcfg::DumpIniPath(), v, "written by the dump importer; paths to the player's own dump")) {
        fprintf(stderr, "error: cannot write %s\n", portcfg::DumpIniPath().c_str());
        return 5;
    }
    printf("config:     %s\nindex:      %s\n", portcfg::DumpIniPath().c_str(), portcfg::SetIndexPath().c_str());
    return 0;
}
