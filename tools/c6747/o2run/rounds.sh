#!/bin/bash
# rounds.sh <stamp> <HHMM> - starts round 1 of the two scenarios on both boxes under <stamp>, then, for each
# box, round N+1 as soon as round N is done, while the clock is before <HHMM> (PKT, the Mac's clock); a box
# whose round ends at or after it stops. Rounds are <stamp>, <stamp>-r2, <stamp>-r3, ...
#   tools/c6747/o2run/rounds.sh s1004-231455 2336       (04-10-2026: 4 rounds on Windows, 3 on Linux)
S=$(cd "$(dirname "$0")" && pwd); BASE=$1; CUT=$2
K=~/Documents/Claude/myMorningWalk.pem
isdone() { if [ $1 = windows ]; then ssh -o ConnectTimeout=15 windows "if exist C:\\cxx1\\o2run\\$2\\rep.done (exit 0) else (exit 1)" 2>/dev/null
           else ssh -i $K -o ConnectTimeout=15 ec2-user@52.202.164.123 "test -f ~/o2run/$2/rep.done" 2>/dev/null; fi; }
stamp() { [ $2 = 1 ] && echo $BASE || echo $BASE-r$2; }
python3 $S/scen.py windows $BASE; python3 $S/scen.py linux $BASE
wr=1; lr=1; wend=0; lend=0
until [ $wend = 1 ] && [ $lend = 1 ]; do
  for b in windows linux; do
    if [ $b = windows ]; then r=$wr; e=$wend; else r=$lr; e=$lend; fi
    [ $e = 1 ] && continue
    st=$(stamp $b $r)
    if isdone $b $st; then
      echo "$b round $r done $(date +%H:%M:%S)"
      if [ "$(date +%H%M)" -lt $CUT ]; then n=$((r + 1)); python3 $S/scen.py $b $(stamp $b $n)
      else n=$r; echo "$b: last round $r (after $CUT)"; [ $b = windows ] && wend=1 || lend=1; fi
      [ $b = windows ] && wr=$n || lr=$n
    fi
  done
  sleep 10
done
echo "all rounds done $(date +%H:%M:%S) windows=$wr linux=$lr"
