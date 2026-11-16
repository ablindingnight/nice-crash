// Verifies the player's dump: SHA-1 of main.dol against DUMP_DOL_SHA1 (project.h).
// Reused later by the launcher (milestone 2.5) for its Verified / Unverified badge.
#pragma once
#include <string>

struct DumpCheck {
    std::string dolPath;  // the main.dol that was hashed
    std::string sha1;     // lowercase hex, empty if the file could not be read
    bool verified = false;
};

// discFiles: the DATA partition's files/ folder. The DOL is looked for at dolPath if given, else at
// <discFiles>/../sys/main.dol (the extracted-partition layout).
DumpCheck CheckDump(const std::string& discFiles, const std::string& dolPath);

// Finds the files\ folder (holding tocres.res) and main.dol for a dump path. Accepted layouts:
//   <dump>\DATA\files + <dump>\DATA\sys   (whole-disc extraction)
//   <dump>\files + <dump>\sys             (one partition)
//   <dump> = the files\ folder itself     (main.dol from ..\sys)
// dol is filled only if empty. On failure, why says what's wrong in words for the player.
bool LocateDump(const std::string& dump, std::string& files, std::string& dol, std::string& why);
// The six-character game ID from <files>\..\sys\boot.bin ("REXE01"), or "" if unreadable.
std::string DumpGameId(const std::string& files);
