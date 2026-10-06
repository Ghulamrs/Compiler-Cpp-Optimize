#!/bin/sh
# Every case in tests/cases, compiled and run on this machine.
#
# A case is either a program with a .expected file holding what it prints, or
# a program with a .error file holding text its diagnostic must contain. The
# second kind is how a refusal is tested: a compiler that stops is doing its
# job, and the message it stops with is part of what is being checked.
#
# This suite runs where cxx1 can also assemble and link - it is the host-target
# suite. Assembly for the other two targets is checked by tests/emit.sh, which
# needs no assembler and therefore runs anywhere.
set -e
cd "$(dirname "$0")/.."
CXX1="${CXX1:-./cpp11.exe}"

# Every invocation of the compiler runs under a CPU limit, in a subshell so
# the limit does not outlive it. A parser that loops on bad input is a real
# failure mode - one was inherited from Compiler-C and shipped there unnoticed
# through 425 cases - and without this the suite hangs instead of reporting it.
cxx1() { ( ulimit -t 10; $CXX1 "$@" < /dev/null ); }
OUT=tests/out-run
# The host target, for a case whose .notarget names it - the same rule
# emit.sh applies to every target, said out loud on every run.
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) HOST=arm64-darwin ;;
    *) HOST=x86_64-linux ;;
esac

# One case, its report and verdict written beside its output so that the cases can run at once:
# every line it would have printed goes to $OUT/<case>.report, and pass, fail or skip to .verdict.
one() {
  base=$1; src=tests/cases/$base.cpp
  # `<case>.part.cpp` is the second translation unit of <case>, built into its program, not a case.
  case "$base" in *.part) return ;; esac
  part=""; [ -f "tests/cases/$base.part.cpp" ] && part="tests/cases/$base.part.cpp"
  if [ -f "tests/cases/$base.notarget" ] && grep -q "^$HOST\b" "tests/cases/$base.notarget"; then
    echo "  skip $base for $HOST: $(grep "^$HOST\b" "tests/cases/$base.notarget" | sed "s/^$HOST[[:space:]]*//")"
    return
  fi

  if [ -f "tests/cases/$base.error" ]; then
    want=$(cat "tests/cases/$base.error")
    if cxx1 -S "$src" -o "$OUT/$base.s" 2>"$OUT/$base.err"; then
      echo "FAIL $base: compiled, and should not have"
      echo fail > "$OUT/$base.verdict"
    elif grep -qF "$want" "$OUT/$base.err"; then
      echo pass > "$OUT/$base.verdict"
    else
      echo "FAIL $base: wanted \"$want\", got:"
      sed 's/^/      /' "$OUT/$base.err"
      echo fail > "$OUT/$base.verdict"
    fi
    return
  fi

  if ! cxx1 "$src" $part -o "$OUT/$base" 2>"$OUT/$base.err"; then
    echo "FAIL $base: did not compile"
    sed 's/^/      /' "$OUT/$base.err"
    echo fail > "$OUT/$base.verdict"
    return
  fi
  # **The runner's own stderr is closed around the run**, so the shell's
  # report of a signal does not land in the suite's output. A case that ends
  # by calling abort - which is what `noexcept-terminates` measures - makes
  # the shell write "Abort trap: 6", in a spelling that differs on each of the
  # three machines and reads like a failure when it is the expected result.
  # The case's own stdout and stderr are captured inside the group either way;
  # only the shell's commentary is dropped. A subshell does not do it: the
  # report comes from the shell that waited, not the one that ran.
  { "$OUT/$base" > "$OUT/$base.out" 2>&1 < /dev/null; } 2>/dev/null || true
  if diff -q "tests/cases/$base.expected" "$OUT/$base.out" >/dev/null; then
    echo pass > "$OUT/$base.verdict"
  else
    echo "FAIL $base:"
    diff "tests/cases/$base.expected" "$OUT/$base.out" | sed 's/^/      /'
    echo fail > "$OUT/$base.verdict"
  fi
}

if [ "${1:-}" = --one ]; then one "$2" > "$OUT/$2.report" 2>&1; exit 0; fi

rm -rf "$OUT"; mkdir -p "$OUT"
# All the cases at once, JOBS of them (the machine's processors by default), then the reports in case order.
JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}
for src in tests/cases/*.cpp; do basename "$src" .cpp; done | xargs -P "$JOBS" -n 1 sh "$0" --one
pass=0; fail=0
for src in tests/cases/*.cpp; do
    base=$(basename "$src" .cpp)
    cat "$OUT/$base.report"
    case "$(cat "$OUT/$base.verdict" 2>/dev/null)" in pass) pass=$((pass + 1)) ;; fail) fail=$((fail + 1)) ;; esac
done

echo "run.sh: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
