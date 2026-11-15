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
