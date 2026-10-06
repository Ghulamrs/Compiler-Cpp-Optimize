#!/usr/bin/env python3
# report.py <dir> <box> "<variants>" - results.csv from img/: the same columns as r50.ps1's.
import csv, os, re, sys
W, box, variants = sys.argv[1], sys.argv[2], sys.argv[3].split()
I = os.path.join(W, "img")
def read(p):
    try:
        with open(p, "rb") as f: return f.read().decode("latin-1").replace("\r\n", "\n")
    except OSError: return None
rows = []
for line in open(os.path.join(W, "list.txt")):
    n = line.split()[0]
    exp = read(os.path.join(W, "corpus", n + ".expected"))
    for v in variants:
        o = os.path.join(I, n + "." + v)
        r = dict(prog=n, variant=v, build="ok", simExit="", simCycles="", ccsCycles="", ccsExit="",
                 simVsCcs="", simVsExp="", ccsVsExp="", ratio="")
        if not os.path.exists(o + ".out"):
            r["build"] = "failed"; rows.append(r); continue
        sim, err = read(o + ".sim.txt"), read(o + ".sim.err") or ""
        m = re.search(r"count=(\d+)", err)
        if "TIMEOUT" in err: r["simExit"] = "TIMEOUT"
        elif m:
            r["simCycles"] = m.group(1)
            r["simExit"] = "C$$EXIT" if "exit=C$$EXIT" in err else (re.search(r"exit=(\S+)", err) or [None, "?"])[1]
        else: r["simExit"] = "none"
        log = read(o + ".ccs.log")
        m = log and re.search(r"RESULT event=\S+ count=(\d+) pc=(0x[0-9a-f]+) exit=(0x[0-9a-f-]+)", log)
        if m:
            r["ccsCycles"] = m.group(1); r["ccsExit"] = "C$$EXIT" if m.group(2) == m.group(3) else "pc=" + m.group(2)
        else: r["ccsExit"] = "none" if log else "notrun"
        cio = read(o + ".ccs.cio")
        if cio is None and r["ccsCycles"]: cio = ""
        if sim is not None and r["ccsCycles"]: r["simVsCcs"] = "same" if sim == cio else "DIFF"
        if sim is not None: r["simVsExp"] = "ok" if sim == exp else "differ"
        if r["ccsCycles"]: r["ccsVsExp"] = "ok" if cio == exp else "differ"
        if r["simCycles"] and r["ccsCycles"] and int(r["ccsCycles"]) > 0:
            r["ratio"] = "%.4f" % (int(r["simCycles"]) / int(r["ccsCycles"]))
        rows.append(r)
with open(os.path.join(W, "results.csv"), "w", newline="") as f:
    w = csv.DictWriter(f, fieldnames=list(rows[0])); w.writeheader(); w.writerows(rows)
print("report:", len(rows), "rows ->", os.path.join(W, "results.csv"))
