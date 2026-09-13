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
CXX1="${CXX1:-./cxx1i.exe}"

# Every invocation of the compiler runs under a CPU limit, in a subshell so
# the limit does not outlive it. A parser that loops on bad input is a real
# failure mode - one was inherited from Compiler-C and shipped there unnoticed
# through 425 cases - and without this the suite hangs instead of reporting it.
cxx1() { ( ulimit -t 10; $CXX1 "$@" < /dev/null ); }
OUT=tests/out-run
rm -rf "$OUT"; mkdir -p "$OUT"
# The host target, for a case whose .notarget names it - the same rule
# emit.sh applies to every target, said out loud on every run.
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) HOST=arm64-darwin ;;
    *) HOST=x86_64-linux ;;
esac

pass=0; fail=0
for src in tests/cases/*.cpp; do
    base=$(basename "$src" .cpp)
    if [ -f "tests/cases/$base.notarget" ] && grep -q "^$HOST\b" "tests/cases/$base.notarget"; then
        echo "  skip $base for $HOST: $(grep "^$HOST\b" "tests/cases/$base.notarget" | sed "s/^$HOST[[:space:]]*//")"
        continue
    fi

    if [ -f "tests/cases/$base.error" ]; then
        want=$(cat "tests/cases/$base.error")
        if cxx1 -S "$src" -o "$OUT/$base.s" 2>"$OUT/$base.err"; then
            echo "FAIL $base: compiled, and should not have"
            fail=$((fail + 1))
        elif grep -qF "$want" "$OUT/$base.err"; then
            pass=$((pass + 1))
        else
            echo "FAIL $base: wanted \"$want\", got:"
            sed 's/^/      /' "$OUT/$base.err"
            fail=$((fail + 1))
        fi
        continue
    fi

    if ! cxx1 "$src" -o "$OUT/$base" 2>"$OUT/$base.err"; then
        echo "FAIL $base: did not compile"
        sed 's/^/      /' "$OUT/$base.err"
        fail=$((fail + 1))
        continue
    fi
    # **The runner's own stderr is closed around the run**, so the shell's
    # report of a signal does not land in the suite's output. A case that ends
    # by calling abort - which is what `noexcept-terminates` measures - makes
    # the shell write "Abort trap: 6", in a spelling that differs on each of the
    # three machines and reads like a failure when it is the expected result.
    # The case's own stdout and stderr are captured inside the group either way;
    # only the shell's commentary is dropped. A subshell does not do it: the
    # report comes from the shell that waited, not the one that ran.
    { "$OUT/$base" > "$OUT/$base.out" 2>&1; } 2>/dev/null || true
    if diff -q "tests/cases/$base.expected" "$OUT/$base.out" >/dev/null; then
        pass=$((pass + 1))
    else
        echo "FAIL $base:"
        diff "tests/cases/$base.expected" "$OUT/$base.out" | sed 's/^/      /'
        fail=$((fail + 1))
    fi
done

echo "run.sh: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
