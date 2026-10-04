#!/bin/bash
# scen_collect.sh <round dir> - on a box: res.tgz of a round's simulator results, native runs and expected outputs.
cd "$1" && tar -czf res.tgz $(ls -d b/*/*/sim.r* native/run.* native/build*.log progress.log plan-build.txt progs/*/expected 2>/dev/null) && ls -la res.tgz | awk '{print $5}'
