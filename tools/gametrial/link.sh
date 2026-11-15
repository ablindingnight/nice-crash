#!/bin/sh
# Link wrapper: hands the DOL path from the importer's dump.ini to link.py (a Microsoft Store Python cannot see
# %LOCALAPPDATA%\NiceCrash because of its filesystem virtualisation). Arguments go to link.py.
cd "$(dirname "$0")/../.."
INI="$LOCALAPPDATA/NiceCrash/dump.ini"
[ -f "$INI" ] && export NICECRASH_DOL="$(sed -n 's/^dol=//p' "$INI" | tr -d '\r')"
exec python -I tools/gametrial/link.py "$@"
