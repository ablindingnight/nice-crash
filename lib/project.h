// The ONE place the project's name lives. The port and launcher get their own name (to be chosen with
// the user); the game's real title is used only to describe the player's own dump.
#pragma once

#define PROJECT_NAME "Nice Crash"
#define PROJECT_TAGLINE "Decomp Project"

// The supported dump: Wii (USA), REXE01. SHA-1 of DATA/sys/main.dol, the same value as the decomp's
// config/REXE01/build.sha1. A hash, not game data.
#define DUMP_REGION_LABEL "Wii (USA), REXE01"
#define DUMP_DOL_SHA1 "e51685814152f377e5c76a50067d8132e25f672b"

// Per-user data folder name under %LOCALAPPDATA% (imported-dump config and index). Renamed with the project.
#define PROJECT_DIRNAME "NiceCrash"

// Versions shown by the launcher (title bar, Home, About, bug reports).
#define LAUNCHER_VERSION "0.1.0"
#define NICECRASH_VERSION "0.1.0"

// Flip to 1 at the first release known to work end to end. Until then "Save error logs" (launcher.error_log)
// defaults to on; after it, new installs default to off and a player's own choice is kept (settings.ini stores
// only values the player changed).
#define NICECRASH_KNOWN_WORKING_RELEASE 0
