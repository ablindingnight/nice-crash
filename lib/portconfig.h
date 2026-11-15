// The imported-dump config the importer writes and the host reads:
//   %LOCALAPPDATA%\<PROJECT_DIRNAME>\dump.ini   key=value lines (disc, dol, dol_sha1, verified, ...)
//   %LOCALAPPDATA%\<PROJECT_DIRNAME>\sets.tsv   the RST0 set index
// Only paths, hashes and counts: never game data.
#pragma once
#include <map>
#include <string>

namespace portcfg {

std::string Dir();                 // created on demand
std::string DumpIniPath();         // Dir() + "\dump.ini"
std::string SetIndexPath();        // Dir() + "\sets.tsv"

typedef std::map<std::string, std::string> Values;
bool Read(const std::string& path, Values& out);
bool Write(const std::string& path, const Values& v, const char* header);

}  // namespace portcfg
