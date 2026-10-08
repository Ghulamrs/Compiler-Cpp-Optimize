#!/bin/sh
# Every case's linkage names, against clang's, for all three ABIs.
#
# This is the suite that says the platform ABI was conformed to rather than
# approximated. It needs clang - which can be asked for Microsoft names on any
# machine, with -target x86_64-pc-windows-msvc - and skips itself where there
# is none, saying so rather than passing quietly.
set -e
cd "$(dirname "$0")/.."

# --reasons: no compiling - every .nonames line counted under the kinds of reason it gives,
# a line saying "the same" taking its file's first line's kinds; a wrapped line joins the one before.
if [ "${1:-}" = --reasons ]; then
    for f in tests/cases/*.nonames; do
        awk -v c="$(basename "$f" .nonames)" '
            $1 ~ /^(x86_64-linux|x86_64-windows|arm64-darwin|tms6747)$/ { if (t != "") print c "\t" t "\t" r; t = $1; $1 = ""; r = substr($0, 2); next }
            NF { r = r " " $0 }
            END { if (t != "") print c "\t" t "\t" r }' "$f"
    done | awk -F '\t' '
    function tags(s,   r) {
        r = ""
        if (s ~ /standard library header|includes <|includes a C\+\+|header cxx1 provides|cannot compile|refuses on the cross/) r = r " header"
        if (s ~ /[Cc]losure|lambda/) r = r " closure"
        if (s ~ /COMDAT|C1|C2|D1|D2|strong symbol|inline constructor|in-class/) r = r " c1c2"
        if (s ~ /static local|static-local/) r = r " static-local"
        if (s ~ /__cxx1_vec|element loop/) r = r " vec-loop"
        if (s ~ /memset|memcpy/) r = r " mem"
        if (s ~ /guard_abort|Unwind_Resume|cleanup region|landing pad|call_terminate|except_table|__cxa_free_exception|get_exception_ptr/) r = r " eh"
        if (s ~ /fold|discard|nothing calls|emits no symbol/) r = r " folded"
        if (s ~ /initializer_list/) r = r " init-list"
        if (s ~ /ThrowInfo|RTTI|type_info|_TI[0-9]|\?\?_R/) r = r " rtti"
        if (s ~ /symbolic|XT_|expression of the parameters/) r = r " expr-arg"
        if (s ~ /namespace/) r = r " namespace"
        return r
    }
    { total++; files[$1] = 1; k = tags($3); if ($3 ~ /^([Tt]he )?[Ss]ame/) k = k first[$1]; if (!($1 in first)) first[$1] = k
      if (k == "") { other[++no] = $1 " " $2 ": " substr($3, 1, 90); next }
      split(k, a, " "); delete seen
      for (i in a) if (a[i] != "" && !(a[i] in seen)) { seen[a[i]] = 1; lines[a[i]]++; if (!((a[i], $1) in cs)) { cs[a[i], $1] = 1; cases[a[i]]++ } } }
    END {
      d["header"] = "a library header clang cannot compile for that target - nothing to compare"
      d["closure"] = "a closure type, whose name nobody is obliged to match"
      d["c1c2"] = "C1/D1 beside C2/D2, or an inline definition strong where clang folds it (COMDAT)"
      d["static-local"] = "a static local named by its own symbol (CONFORMANCE.md)"
      d["vec-loop"] = "cxx1s own array element loops where clang writes inline code"
      d["mem"] = "clang calls memcpy/memset where cxx1 writes the copy inline"
      d["eh"] = "exception emission: a landing pad, guard abort or terminate one side has"
      d["folded"] = "clang folds or discards a symbol cxx1 emits"
      d["init-list"] = "std::initializer_list from the libraries, not the language"
      d["rtti"] = "RTTI or ThrowInfo records spelled or emitted differently"
      d["expr-arg"] = "a non-type argument spelled symbolically by clang, folded by cxx1"
      d["namespace"] = "a specialization spelled without its namespace"
      nf = 0; for (c in files) nf++
      printf "%d .nonames files, %d target lines; a line may give several kinds\n\n", nf, total
      printf "%-13s %6s %6s  %s\n", "reason", "lines", "cases", "what"
      for (t in lines) printf "%-13s %6d %6d  %s\n", t, lines[t], cases[t], d[t] | "sort -k2 -nr"
      close("sort -k2 -nr")
      printf "\nunclassified, %d lines:\n", no
      for (i = 1; i <= no; i++) print "  " other[i]
      print "\nnot a .nonames, a host limit: on the Windows box clang has no Darwin headers, so"
      print "ifdef-comment and using-declaration-chain, which include <cstdio>, fail for arm64-darwin"
      print "there and are compared on the Mac (measured 2026-10-08, clang 19.1.5)."
    }'
    exit 0
fi

if ! command -v clang++ > /dev/null 2>&1; then
    echo "names.sh: skipped - no clang++ to ask"
    exit 0
fi

# Every case at once, JOBS of them (the machine's processors by default), each through
# tools/mangled-names into a report of its own, read back in case order.
OUT=tests/out-names
if [ "${1:-}" = --one ]; then
    if out=$(tools/mangled-names "tests/cases/$2.cpp" 2>&1); then echo pass > "$OUT/$2.verdict"
    else { echo "FAIL $2:"; echo "$out" | sed 's/^/      /'; } > "$OUT/$2.report"; echo fail > "$OUT/$2.verdict"; fi
    exit 0
fi
rm -rf "$OUT"; mkdir -p "$OUT"
JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}
cases() {
    for src in tests/cases/*.cpp; do
        case "$(basename "$src")" in *" "[0-9]*) continue;; esac
        base=$(basename "$src" .cpp)
        [ -f "tests/cases/$base.error" ] && continue
        echo "$base"
    done
}
cases | xargs -P "$JOBS" -n 1 sh "$0" --one
for base in $(cases); do [ -f "$OUT/$base.report" ] && cat "$OUT/$base.report"; done
pass=$(cat "$OUT"/*.verdict 2>/dev/null | grep -c pass || true)
fail=$(cat "$OUT"/*.verdict 2>/dev/null | grep -c fail || true)

echo "names.sh: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
