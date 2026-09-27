#!/bin/sh
# Compiler++ built by each box's reference compiler at -O2 and by the installed RIDE's cpp11 -O2, then
# 2000 compiles of inverse.cpp with each build - the Mac here, the Linux and Windows boxes at the same
# time. Each box's own script is inv-bench.sh (Mac, Linux) or inv-bench.cmd with inv-bench.ps1 (Windows).
#
#   tools/compilerpp-bench/run-all.sh              the three boxes
#   N=500 tools/compilerpp-bench/run-all.sh        fewer compiles per round
#
# CXXSRC is Compiler++'s source directory (default ../Compiler++/Compiler++ beside this tree), LINUX_BOX
# and LINUX_KEY the Linux box's ssh target and key, WIN_BOX the Windows box's ssh alias. RIDE 4.5 has to
# be installed on each: /usr/local/bin/cpp11 on the two Unix boxes, C:\Program Files\RIDE 4.5 on Windows.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
CXXSRC=${CXXSRC:-$ROOT/../Compiler++/Compiler++}
LINUX_BOX=${LINUX_BOX:-ec2-user@52.202.164.123}
LINUX_KEY=${LINUX_KEY:-$HOME/Documents/Claude/myMorningWalk.pem}
WIN_BOX=${WIN_BOX:-windows}
N=${N:-2000}
[ -f "$CXXSRC/main.cpp" ] || { echo "run-all.sh: no Compiler++ sources at $CXXSRC - set CXXSRC"; exit 2; }

WORK=$(mktemp -d "${TMPDIR:-/tmp}/compilerpp-bench.XXXXXX")
mkdir -p "$WORK/pack/src" "$WORK/mac"
cp "$CXXSRC"/*.cpp "$CXXSRC"/*.h "$WORK/pack/src/"
cp "$HERE/inverse.cpp" "$HERE/inv-bench.sh" "$HERE/inv-bench.cmd" "$HERE/inv-bench.ps1" "$WORK/pack/"
# the box's copy of the Windows script takes its count from here
sed -i.bak "s/^\\\$N = [0-9]*;/\$N = $N;/" "$WORK/pack/inv-bench.ps1" && rm -f "$WORK/pack/inv-bench.ps1.bak"
COPYFILE_DISABLE=1 tar --no-xattrs -C "$WORK/pack" -czf "$WORK/invbench.tgz" . 2>/dev/null ||
    COPYFILE_DISABLE=1 tar -C "$WORK/pack" -czf "$WORK/invbench.tgz" .

# Staged first, both boxes, so that the three runs start together.
ssh -n "$WIN_BOX" 'if exist C:\ride-verify\invbench rmdir /s /q C:\ride-verify\invbench' > /dev/null 2>&1
ssh -n "$WIN_BOX" 'mkdir C:\ride-verify\invbench' > /dev/null || { echo "run-all.sh: cannot reach $WIN_BOX"; exit 1; }
scp -q "$WORK/invbench.tgz" "$WIN_BOX:C:/ride-verify/invbench/invbench.tgz" || exit 1
scp -q -i "$LINUX_KEY" "$WORK/invbench.tgz" "$LINUX_BOX:/tmp/invbench.tgz" || { echo "run-all.sh: cannot reach $LINUX_BOX"; exit 1; }

start=$(date +%s)
( cd "$WORK/mac" && N=$N sh "$HERE/inv-bench.sh" "$CXXSRC" clang++ /usr/local/bin/cpp11 > "$WORK/mac.result" 2>&1 ) &
( ssh -n -i "$LINUX_KEY" "$LINUX_BOX" "rm -rf ~/invbench && mkdir ~/invbench && cd ~/invbench && tar xzf /tmp/invbench.tgz && N=$N sh inv-bench.sh src g++ /usr/local/bin/cpp11" \
      > "$WORK/linux.result" 2>&1 ) &
( ssh -n "$WIN_BOX" 'cd /d C:\ride-verify\invbench && tar xzf invbench.tgz && inv-bench.cmd' > "$WORK/windows.result" 2>&1 ) &
wait
for box in mac linux windows; do echo "=== $box"; tr -d '\r' < "$WORK/$box.result"; done
echo "=== the three boxes together: $(( $(date +%s) - start ))s; everything is in $WORK"
