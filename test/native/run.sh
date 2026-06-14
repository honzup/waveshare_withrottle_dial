#!/usr/bin/env bash
# Standalone native test runner: compiles a Unity test with g++ and runs it.
# Usage: bash test/native/run.sh [test_name]   (no arg = run every test/test_*/)
set -euo pipefail
cd "$(dirname "$0")/../.."          # repo root
ROOT="$(pwd)"
UNITY="$ROOT/test/native/unity"
INCLUDES=(-I"$UNITY"
          -Icomponents/loco_ref/include
          -Icomponents/horn_resolver/include
          -Icomponents/recents
          -Icomponents/jmri_discovery/include
          -Icomponents/netprov/include)

# Pure (ESP-IDF-free) component sources — included if they exist yet.
PURE=()
for s in components/loco_ref/loco_ref.cpp \
         components/horn_resolver/horn_resolver.cpp \
         components/recents/recents_serialize.cpp \
         components/jmri_discovery/choose_server.cpp \
         components/netprov/netprov_validation.cpp; do
  [ -f "$s" ] && PURE+=("$s")
done

run_one() {
  local name="$1"
  local dir="test/$name"
  [ -d "$dir" ] || { echo "No such test dir: $dir"; return 2; }
  local bin; bin="$(mktemp -d)/t"
  echo "==> building $name"
  g++ -std=gnu++17 -Wall -Wextra "${INCLUDES[@]}" \
      "$dir"/*.cpp "${PURE[@]}" "$UNITY/unity.c" -o "$bin"
  echo "==> running $name"
  "$bin"
}

if [ $# -ge 1 ]; then
  # accept either "test_foo" or "foo"
  case "$1" in test_*) run_one "$1";; *) run_one "test_$1";; esac
else
  rc=0
  for d in test/test_*/; do run_one "$(basename "$d")" || rc=$?; done
  exit $rc
fi
