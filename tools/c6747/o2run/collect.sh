#!/bin/bash
# collect.sh: image sizes and assembly checksums on the box, then results.tgz of everything small.
cd "$(dirname "$0")"
for d in b/*/*; do
  [ -f "$d/image.out" ] && stat -c %s "$d/image.out" > "$d/image.bytes"
  case $d in */cpp11-*) (cd "$d" && for s in *.s; do [ -f "$s" ] && echo "$(tr -d '\r' < "$s" | md5sum | cut -c1-32) $s"; done > s.md5);; esac
done
tar --exclude='*.out' --exclude='*.map' --exclude='src.*' --exclude='*.s' --exclude='vm.raw' \
    --exclude='progs/*/*.cpp' --exclude='progs/*/*.h' --exclude='progs/*/*.c' \
    -czf results.tgz b progs info.txt progress.log plan-build.txt plan-sim.txt lanes/*.log lanes/all.txt lanes/unmeasured.txt lanes/lanes-failed.txt 2>/dev/null
ls -la results.tgz
