#!/bin/sh
# Pin the decomp commit the port builds from (tools/gametrial/decomp.py). Usage: pin.sh [commit|HEAD|live]
cd "$(dirname "$0")"
ref=${1:-HEAD}
if [ "$ref" != live ]; then ref=$(git -C ../../../excite-truck rev-parse "$ref") || exit 1; fi
echo "$ref" > decomp_ref.txt
echo "decomp pinned to $ref"
