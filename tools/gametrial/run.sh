#!/bin/sh
# Rebuild (all units unless ONLY=a.cpp,b.cpp), relink, run with stubs continuing; print the new tail.
cd "$(dirname "$0")/../.."
if [ -n "$ONLY" ]; then python -I tools/gametrial/trial.py --jobs 12 --obj --only "$ONLY" | tail -1; python -I tools/gametrial/link.py --nocompile | tail -1
else python -I tools/gametrial/link.py | tail -1; fi
NICECRASH_UNIMPL=continue timeout ${T:-60} ./build/NiceCrashGame.exe > build/run.out 2> build/run.err
echo "exit $?"
