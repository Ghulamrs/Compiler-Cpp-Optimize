#!/bin/sh
# Every case compiled for all three targets, stopping at assembly.
#
#   tests/emit.sh            compile, and diff against the golden if there is one
#   tests/emit.sh --record   compile, and keep this output as the golden
#
# No assembler and no linker, so this runs on any machine cxx1 builds on -
# which is the point. A backend that cannot be run here can still be checked
# for having produced something rather than having fallen over.
#
# **The golden is what makes "this change emits the same code" a measurement.**
# Counting what compiled says nothing about what came out, so a refactor meant
# to change no behaviour had nothing here to prove it with. Record before it,
# run after it, and the answer is a number of files rather than a claim.
#
# It never fails the run. A changed file may be the whole point of the change,
# and a suite that refused one would be a suite people stopped recording.
set -e
cd "$(dirname "$0")/.."
CXX1="${CXX1:-./cpp11.exe}"
cxx1() { ( ulimit -t 10; $CXX1 "$@" < /dev/null ); }
OUT=tests/out-emit
GOLD=tests/out-emit.golden

# Refused by name, like everything else here: a mistyped flag that ran the
# ordinary suite would look like a recording that quietly did not happen.
record=0
case ${1:-} in
    "")        ;;
    --record)  record=1 ;;
    *)         echo "emit.sh: '$1' is not an option - only --record is" >&2; exit 2 ;;
esac

rm -rf "$OUT"; mkdir -p "$OUT"

pass=0; fail=0
for src in tests/cases/*.cpp; do
    base=$(basename "$src" .cpp)
    [ -f "tests/cases/$base.error" ] && continue
    for target in x86_64-linux x86_64-windows arm64-darwin; do
        # A case may name a target it does not compile for yet, one per line in
        # <case>.notarget. **It has to say why in the file**, because a silent
        # exclusion is how a suite stops testing something without anybody
        # noticing. The line is printed on every run for the same reason.
        if [ -f "tests/cases/$base.notarget" ] &&
           grep -q "^$target\b" "tests/cases/$base.notarget"; then
            echo "  skip $base for $target: $(grep "^$target\b" "tests/cases/$base.notarget" | sed "s/^$target[[:space:]]*//")"
            continue
        fi
        if cxx1 -S -arch "$target" "$src" -o "$OUT/$base.$target.s" \
                 2>"$OUT/$base.$target.err"; then
            pass=$((pass + 1))
        else
            echo "FAIL $base for $target:"
            sed 's/^/      /' "$OUT/$base.$target.err"
            fail=$((fail + 1))
        fi
    done
done

echo "emit.sh: $pass passed, $fail failed"

# **What was recorded, and when.** A golden with no provenance is a number
# nobody can place, so the commit and the date go in beside it and are printed
# on every run that reads it - a stale golden then says so itself.
if [ "$record" = 1 ]; then
    # **Copied file by file, and the duplicates macOS leaves are removed.** A
    # `cp -R` of this directory under ~/Documents came back with 411 real files
    # and 411 named `x 2.s` beside them - the phenomenon CLAUDE.md records - and
    # a golden holding twice what was emitted is a suite lying quietly. Counted
    # afterwards for the same reason: if the two numbers differ, say so and stop.
    rm -rf "$GOLD"
    mkdir -p "$GOLD"
    for f in "$OUT"/*.s "$OUT"/*.err; do
        [ -f "$f" ] || continue
        cp "$f" "$GOLD/$(basename "$f")"
    done
    find "$GOLD" -name '* [0-9].s' -delete
    find "$GOLD" -name '* [0-9].err' -delete
    { echo "recorded $(date -u '+%Y-%m-%d %H:%M UTC')"
      echo "commit   $(git rev-parse --short HEAD 2>/dev/null || echo 'not a git checkout')"
      echo "tree     $(git status --porcelain 2>/dev/null | wc -l | tr -d ' ') file(s) modified when recorded"
    } > "$GOLD/RECORDED"
    kept=$(find "$GOLD" -name '*.s' | wc -l | tr -d ' ')
    made=$(find "$OUT" -name '*.s' | wc -l | tr -d ' ')
    if [ "$kept" != "$made" ]; then
        echo "emit.sh: golden holds $kept files where $made were emitted - not recorded" >&2
        rm -rf "$GOLD"
        exit 1
    fi
    echo "emit.sh: golden recorded, $kept files"
    exit 0
fi

if [ ! -d "$GOLD" ]; then
    # Said out loud rather than skipped, for the reason the .notarget lines are.
    echo "emit.sh: no golden to compare against - tests/emit.sh --record makes one"
    [ "$fail" -eq 0 ]
    exit
fi

# **A name like `auto 2.x86_64-linux.s` is not a file this compiler wrote.** They
# appear under ~/Documents on their own - CLAUDE.md records the phenomenon - and
# they turned up in a golden that had been recorded clean, so deleting them once
# is not enough. Both sides skip them and the count is printed, never silent.
isDuplicate() {
    case $1 in *' '[0-9].s|*' '[0-9][0-9].s) return 0 ;; *) return 1 ;; esac
}

changed=0; added=0; removed=0; shown=0; golden=0; dups=0
for now in "$OUT"/*.s; do
    if isDuplicate "$(basename "$now")"; then dups=$((dups + 1)); continue; fi
    was="$GOLD/$(basename "$now")"
    if [ ! -f "$was" ]; then
        added=$((added + 1))
        continue
    fi
    if cmp -s "$now" "$was"; then continue; fi
    changed=$((changed + 1))
    if [ "$shown" -lt 12 ]; then
        echo "  changed $(basename "$now" .s)"
        shown=$((shown + 1))
    fi
done
# `[ x ] && echo` would end the run under set -e the moment the test is
# false, which is the ordinary case. An if is not a style choice here.
if [ "$changed" -gt "$shown" ]; then echo "  ... and $((changed - shown)) more"; fi
for was in "$GOLD"/*.s; do
    [ -f "$was" ] || continue     # an unmatched glob is the name of the pattern
    if isDuplicate "$(basename "$was")"; then dups=$((dups + 1)); continue; fi
    golden=$((golden + 1))
    [ -f "$OUT/$(basename "$was")" ] || removed=$((removed + 1))
done

sed 's/^/emit.sh: golden /' "$GOLD/RECORDED" 2>/dev/null || true
echo "emit.sh: golden - $changed of $golden files changed, $added added, $removed removed"
if [ "$dups" -gt 0 ]; then
    echo "emit.sh: golden - $dups duplicate ' 2' copies ignored, which macOS made"
fi

[ "$fail" -eq 0 ]
