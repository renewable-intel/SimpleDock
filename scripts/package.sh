#!/bin/bash
# Build the release archive: build/release/SimpleDock.lha (+ SimpleDock.readme).
# Needs a release build in build/SimpleDock (scripts/build.sh).
set -e
cd "$(dirname "$0")/.."
R=build/release
rm -rf "$R"; mkdir -p "$R/SimpleDock"
cp build/SimpleDock "$R/SimpleDock/SimpleDock"
cp dist/ReadMe "$R/SimpleDock/ReadMe"
cp LICENSE "$R/SimpleDock/LICENSE"
python3 - "$R" <<'PY'
import sys, os
sys.path.insert(0, "scripts")
import mkicon as mk
r = sys.argv[1]
mk.write_icon(os.path.join(r, "SimpleDock", "SimpleDock.info"), mk.WBTOOL, mk.TOOLTYPES)
mk.write_icon(os.path.join(r, "SimpleDock", "ReadMe.info"), mk.WBPROJECT,
              tool="SYS:Utilities/MultiView", art=mk.shade_page)
PY
python3 scripts/lha.py "$R/SimpleDock.lha" "$R" SimpleDock   # no drawer icon: Workbench uses its own
cp dist/SimpleDock.readme "$R/SimpleDock.readme"
ls -l "$R"
